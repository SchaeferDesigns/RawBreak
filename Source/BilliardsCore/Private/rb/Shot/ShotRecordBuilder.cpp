#include "rb/Core/FpGuard.h"
// Owner: WP-7 (output, playback & tools). Spec: rules.md 2.1-2.2, 3.1-3.4, 4.11; physics-collisions 8.13;
// Docs/architecture.md 8.6, 8.10.
#include "rb/Shot/ShotRecordBuilder.h"

#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		constexpr std::uint32_t Bit(int Index) { return 1u << static_cast<unsigned>(Index); }

		bool InPlayBall(const SimInput& Input, int Ball) { return Ball >= 0 && Ball < kMaxBalls && Input.Balls[Ball].InPlay; }

		// A ball resting on (or moving along) the cloth: a surface state at the cloth height (not the rail cap).
		bool OnCloth(const BallState& S, double Radius, double Tolerance)
		{
			return IsOnSurface(S.State) && Abs(S.Position.z - Radius) <= Tolerance;
		}

		// Plan direction D lies on the arc [From, From + Sweep] (CCW) about a jaw center (From in (-pi, pi], JawArc).
		bool InArcRange(const Vec2& D, double From, double Sweep)
		{
			double Rel = Atan2(D.y, D.x) - From; // in [-2 pi, 2 pi) for a From of the geometry
			if (Rel < 0.0)
			{
				Rel += kTwoPi;
			}
			if (Rel >= kTwoPi)
			{
				Rel -= kTwoPi;
			}
			return Rel >= 0.0 && Rel <= Sweep;
		}

		// Rail features (Core/Ids.h) a ball on the cloth at plan position P touches within Tolerance (rules.md 3.1, 4.11):
		// nose lines at R_c, jaw arcs at r_j + R_c on their exposed arc, facing plan lines at s_f inside the opening.
		std::uint32_t FrozenRails(const TableGeometry& G, const CushionParams& Cushion, const Vec2& P, double R, double Tolerance)
		{
			if (!(R > 0.0))
			{
				return 0;
			}
			std::uint32_t Mask = 0;
			for (int i = 0; i < G.Noses.Size(); ++i)
			{
				const NoseSegment& N = G.Noses[i];
				if (!N.Present)
				{
					continue;
				}
				const Vec2 D = P - N.Start;
				const double Along = Dot(D, N.Direction);
				const double Distance = Dot(D, N.InwardNormal);
				if (Along < 0.0 || Along > N.Length || !(Distance > 0.0))
				{
					continue;
				}
				const double Rc = ComputeCushionContact(R, N.Height, Cushion.NoseProfileRadius, Cushion.PooltoolCompat).HorizontalOffset;
				if (Distance - Rc <= Tolerance)
				{
					const int Feature = N.Cushion != CushionId::None ? RailFeatureOfCushion(N.Cushion) : i;
					Mask |= Bit(Feature);
				}
			}
			for (const JawArc& A : G.JawArcs)
			{
				const Vec2 D = P - A.Center;
				const double Distance = Length(D);
				if (!(Distance > A.Radius) || !InArcRange(D, A.AngleFrom, A.AngleSweep))
				{
					continue;
				}
				const double Rc = ComputeCushionContact(R, A.Height, Cushion.NoseProfileRadius, Cushion.PooltoolCompat).HorizontalOffset;
				if (Distance - (A.Radius + Rc) <= Tolerance)
				{
					Mask |= Bit(RailFeatureOfJaw(A.Pocket, A.Side));
				}
			}
			for (const Facing& F : G.Facings)
			{
				const Vec2 D = P - F.Start;
				const double Along = Dot(D, F.Direction);
				const double Distance = Dot(D, F.PocketNormal);
				if (Along < 0.0 || Along > F.Length || !(Distance > 0.0))
				{
					continue;
				}
				if (Distance - FacingContactOffset(R, F.TopHeight, F.Backdraft) <= Tolerance)
				{
					Mask |= Bit(RailFeatureOfJaw(F.Pocket, F.Side));
				}
			}
			return Mask;
		}

		// Gap between two ball surfaces (3-D center distance minus the radii) <= Tolerance.
		bool BallsTouch(const Vec3& A, double Ra, const Vec3& B, double Rb, double Tolerance)
		{
			return Length(B - A) - (Ra + Rb) <= Tolerance;
		}

		// Stable insertion sort by (Time, Sequence): appends arrive (nearly) in time order, so this is O(n) in practice.
		void SortRecordEvents(std::vector<RecordEvent>& Events)
		{
			const std::size_t Count = Events.size();
			for (std::size_t i = 1; i < Count; ++i)
			{
				const RecordEvent& Prev = Events[i - 1];
				const RecordEvent& Cur = Events[i];
				if (!(Cur.Time < Prev.Time || (Cur.Time == Prev.Time && Cur.Sequence < Prev.Sequence)))
				{
					continue;
				}
				const RecordEvent Moving = Cur;
				std::size_t j = i;
				while (j > 0
					&& (Moving.Time < Events[j - 1].Time || (Moving.Time == Events[j - 1].Time && Moving.Sequence < Events[j - 1].Sequence)))
				{
					Events[j] = Events[j - 1];
					--j;
				}
				Events[j] = Moving;
			}
		}

		struct TipInterval
		{
			int Strike = 0;
			BallId Ball = kNoBall;
			double Start = 0.0;
			double End = 0.0;
		};

		constexpr int kMaxTipIntervals = 4 * kMaxTipContacts;

		bool Precedes(const TipInterval& A, const TipInterval& B)
		{
			if (A.Start != B.Start)
			{
				return A.Start < B.Start;
			}
			if (A.Ball != B.Ball)
			{
				return A.Ball < B.Ball;
			}
			return A.Strike < B.Strike;
		}

		// Tip contacts of every (strike, ball) from the record's TipBallBegin / TipBallEnd events, merged and sorted
		// (FinishShotRecord). Returns false if the events are inconsistent (unpaired) or a list overflowed.
		bool BuildTipContacts(const SimInput& Input, const ShotResult& Result, ShotRecord& Out)
		{
			bool Complete = true;
			bool Open[kMaxStrikes][kMaxBalls] = {};
			double OpenStart[kMaxStrikes][kMaxBalls] = {};
			TipInterval Merged[kMaxTipIntervals];
			int MergedCount = 0;

			const auto Add = [&](int Strike, int Ball, double Start, double End) {
				const double ContactTime = Strike < Input.Strikes.Size() ? Input.Strikes[Strike].Input.Cue.ContactTime : 0.0;
				for (int i = MergedCount - 1; i >= 0; --i)
				{
					TipInterval& M = Merged[i];
					if (M.Strike == Strike && M.Ball == Ball)
					{
						if (Start - M.End < ContactTime)
						{
							M.End = End > M.End ? End : M.End; // closer than one contact time: one long contact
							return;
						}
						break;
					}
				}
				if (MergedCount >= kMaxTipIntervals)
				{
					Complete = false;
					return;
				}
				TipInterval& N = Merged[MergedCount++];
				N.Strike = Strike;
				N.Ball = static_cast<BallId>(Ball);
				N.Start = Start;
				N.End = End;
			};

			for (const RecordEvent& E : Out.Events)
			{
				const bool Begin = E.Type == RecordEventType::TipBallBegin;
				if (!Begin && E.Type != RecordEventType::TipBallEnd)
				{
					continue;
				}
				const int Strike = E.Feature;
				const int Ball = E.A;
				if (Strike >= kMaxStrikes || Ball < 0 || Ball >= kMaxBalls)
				{
					Complete = false;
					continue;
				}
				if (Begin)
				{
					if (!Open[Strike][Ball])
					{
						Open[Strike][Ball] = true;
						OpenStart[Strike][Ball] = E.Time;
					}
					continue;
				}
				if (!Open[Strike][Ball])
				{
					Complete = false; // an end without a begin
					continue;
				}
				Open[Strike][Ball] = false;
				Add(Strike, Ball, OpenStart[Strike][Ball], E.Time);
			}
			for (int s = 0; s < kMaxStrikes; ++s)
			{
				for (int b = 0; b < kMaxBalls; ++b)
				{
					if (Open[s][b])
					{
						// Still in contact when the record ended (aborted shot): close it at the stop time.
						Complete = false;
						Add(s, b, OpenStart[s][b], Result.StopTime > OpenStart[s][b] ? Result.StopTime : OpenStart[s][b]);
					}
				}
			}

			// Sort by (Start, Ball, Strike).
			for (int i = 1; i < MergedCount; ++i)
			{
				const TipInterval Moving = Merged[i];
				int j = i;
				while (j > 0 && Precedes(Moving, Merged[j - 1]))
				{
					Merged[j] = Merged[j - 1];
					--j;
				}
				Merged[j] = Moving;
			}

			StrokeRecord& Stroke = Out.Stroke;
			for (int i = 0; i < MergedCount; ++i)
			{
				const TipInterval& M = Merged[i];
				TipContact Contact;
				Contact.Ball = M.Ball;
				Contact.Strike = M.Strike;
				Contact.Start = M.Start;
				Contact.End = M.End;
				if (!Stroke.TipContacts.PushBack(Contact))
				{
					Stroke.Overflow = true;
				}
			}
			for (int i = 0; i < MergedCount; ++i)
			{
				const TipInterval& M = Merged[i];
				if (M.Strike >= Input.Strikes.Size() || Input.Strikes[M.Strike].Ball == M.Ball)
				{
					continue;
				}
				// rules F10: the follow-through tip touched a ball other than the one it struck.
				NonTipContact Touch;
				Touch.Ball = M.Ball;
				Touch.Source = NonTipSource::CueTip;
				Touch.Time = M.Start;
				if (!Stroke.NonTipContacts.PushBack(Touch))
				{
					Stroke.Overflow = true;
				}
			}
			return Complete;
		}
	}

	std::uint32_t FrozenRailFeatures(const TableGeometry& Geometry, const CushionParams& Cushion, const Vec2& P, double Radius, double Tolerance)
	{
		return FrozenRails(Geometry, Cushion, P, Radius, Tolerance);
	}

	void BuildShotStartSnapshot(const SimInput& Input, ShotStartSnapshot& Out)
	{
		Out = ShotStartSnapshot{};
		const ShotContext& Context = Input.Context;
		const double Tolerance = Context.FrozenTolerance;

		bool AtRest = true;
		for (int b = 0; b < kMaxBalls; ++b)
		{
			const SimBall& Ball = Input.Balls[b];
			if (!Ball.InPlay)
			{
				continue;
			}
			Out.Presence[b] = BallPresence::OnTable;
			Out.Position[b] = XY(Ball.State.Position);
			Out.Radius[b] = Ball.Spec.Radius;
			Out.State[b] = Ball.State.State;
			if (IsMoving(Ball.State.State))
			{
				AtRest = false;
			}
		}
		Out.AllBallsAtRest = AtRest;

		Out.InHand = Context.InHand;
		Out.PlacedPosition = Context.PlacedPosition;
		Out.TemplatePresent = Context.TemplatePresent;
		Out.ShotClockElapsed = Context.ShotClockElapsed;
		Out.FootOnFloor = Context.FootOnFloor;
		if (Context.InHand != CueBallInHand::No && Input.Table != nullptr)
		{
			Out.PlacementOverPocket = IsOverPocketOpening(*Input.Table, Context.PlacedPosition);
		}

		// Frozen-ball declarations (rules.md 4.11), balls on the cloth only.
		for (int b = 0; b < kMaxBalls; ++b)
		{
			const SimBall& Ball = Input.Balls[b];
			if (!Ball.InPlay || !OnCloth(Ball.State, Ball.Spec.Radius, Tolerance))
			{
				continue;
			}
			if (Input.Table != nullptr)
			{
				Out.FrozenToRail[b] = FrozenRails(*Input.Table, Input.Params.Cushion, XY(Ball.State.Position), Ball.Spec.Radius, Tolerance);
			}
		}
		if (InPlayBall(Input, kCueBallId))
		{
			const SimBall& Cue = Input.Balls[kCueBallId];
			for (int b = 1; b < kMaxBalls; ++b)
			{
				const SimBall& Ball = Input.Balls[b];
				if (Ball.InPlay && BallsTouch(Cue.State.Position, Cue.Spec.Radius, Ball.State.Position, Ball.Spec.Radius, Tolerance))
				{
					Out.FrozenToCueBall |= Bit(b);
				}
			}
		}
	}

	bool IsRecordRelevant(ShotEventType Type)
	{
		switch (Type)
		{
		case ShotEventType::TipContactBegin:
		case ShotEventType::TipContactEnd:
		case ShotEventType::BallBall:
		case ShotEventType::BallCushion:
		case ShotEventType::BallJaw:
		case ShotEventType::BallRailTop:
		case ShotEventType::BallAirborne:
		case ShotEventType::BallLand:
		case ShotEventType::BallPocketEnter:
		case ShotEventType::BallPocketRim:
		case ShotEventType::BallLiner:
		case ShotEventType::BallPocketExit:
		case ShotEventType::BallPocketed:
		case ShotEventType::BallOffTable:
		case ShotEventType::BallExternalContact:
		case ShotEventType::MotionTransition:
		case ShotEventType::BallLineCross:
		case ShotEventType::BallJumpedOver: return true;
		case ShotEventType::CueStrike:
		case ShotEventType::TipRecontact:
		case ShotEventType::BallSlate:
		case ShotEventType::IslandBegin:
		case ShotEventType::IslandRigid:
		case ShotEventType::IslandEnd:
		case ShotEventType::ZenoGuard:
		case ShotEventType::Diagnostic:
		case ShotEventType::TiltRefresh: return false;
		}
		return false;
	}

	bool ToRecordEvent(const ShotEvent& Event, std::uint32_t Sequence, RecordEvent& Out)
	{
		RecordEvent R;
		switch (Event.Type)
		{
		case ShotEventType::TipContactBegin: R.Type = RecordEventType::TipBallBegin; break;
		case ShotEventType::TipContactEnd: R.Type = RecordEventType::TipBallEnd; break;
		case ShotEventType::BallBall: R.Type = RecordEventType::BallBall; break;
		case ShotEventType::BallCushion: R.Type = RecordEventType::BallCushion; break;
		case ShotEventType::BallJaw: R.Type = RecordEventType::BallJaw; break;
		case ShotEventType::BallRailTop: R.Type = RecordEventType::BallRailTop; break;
		case ShotEventType::BallAirborne: R.Type = RecordEventType::BallAirborne; break;
		case ShotEventType::BallLand: R.Type = RecordEventType::BallLand; break;
		case ShotEventType::BallPocketEnter: R.Type = RecordEventType::BallPocketEnter; break;
		case ShotEventType::BallPocketRim:
		case ShotEventType::BallLiner: R.Type = RecordEventType::BallLiner; break;
		case ShotEventType::BallPocketExit: R.Type = RecordEventType::BallPocketExit; break;
		case ShotEventType::BallPocketed: R.Type = RecordEventType::BallPocketed; break;
		case ShotEventType::BallOffTable: R.Type = RecordEventType::BallOffTable; break;
		case ShotEventType::BallExternalContact: R.Type = RecordEventType::BallExternalContact; break;
		case ShotEventType::MotionTransition: R.Type = RecordEventType::MotionTransition; break;
		case ShotEventType::BallLineCross: R.Type = RecordEventType::BallLineCross; break;
		case ShotEventType::BallJumpedOver: R.Type = RecordEventType::BallJumpedOver; break;
		case ShotEventType::CueStrike:
		case ShotEventType::TipRecontact:
		case ShotEventType::BallSlate:
		case ShotEventType::IslandBegin:
		case ShotEventType::IslandRigid:
		case ShotEventType::IslandEnd:
		case ShotEventType::ZenoGuard:
		case ShotEventType::Diagnostic:
		case ShotEventType::TiltRefresh: return false;
		}

		R.Time = Event.Time;
		R.Sequence = Sequence;
		R.A = Event.A;
		R.B = Event.B;
		R.Feature = Event.Feature;
		R.Normal = Event.Normal;
		R.CutAngle = Event.CutAngle;
		R.Value = Event.Value;
		R.PositionA = XY(Event.Pre[0].Position);
		if (Event.B != kNoBall)
		{
			R.PositionB = XY(Event.Pre[1].Position);
		}

		switch (R.Type)
		{
		case RecordEventType::BallCushion:
			R.ContinuesInitialFreeze = (Event.Flags & ShotEventFlags::ContinuesInitialFreeze) != 0u;
			break;
		case RecordEventType::BallJaw:
		{
			R.ContinuesInitialFreeze = (Event.Flags & ShotEventFlags::ContinuesInitialFreeze) != 0u;
			const int Side = Event.SubFeature & 0x0F;
			R.Side = static_cast<std::int8_t>(Side <= static_cast<int>(JawSide::Outgoing) ? Side : 0);
			break;
		}
		case RecordEventType::BallLineCross:
			R.Side = static_cast<std::int8_t>(Event.SubFeature == 0 ? 1 : (Event.SubFeature == 1 ? -1 : 0));
			break;
		case RecordEventType::BallAirborne:
		case RecordEventType::BallLand: R.ZMax = Event.Value; break;
		case RecordEventType::MotionTransition:
			R.From = Event.From;
			R.To = Event.To;
			break;
		case RecordEventType::TipBallBegin:
		case RecordEventType::TipBallEnd:
			R.OtherContactBefore = Event.SubFeature == 1;
			if (Event.B == kNoBall)
			{
				R.Value = 0.0;
			}
			break;
		case RecordEventType::BallBall:
		case RecordEventType::BallRailTop:
		case RecordEventType::BallLiner:
		case RecordEventType::BallPocketEnter:
		case RecordEventType::BallPocketExit:
		case RecordEventType::BallPocketed:
		case RecordEventType::BallTouchesPocketedBall:
		case RecordEventType::SupportedOverPocket:
		case RecordEventType::BallExternalContact:
		case RecordEventType::BallOffTable:
		case RecordEventType::BallJumpedOver: break;
		}
		Out = R;
		return true;
	}

	void BeginShotRecord(const SimInput& Input, ShotRecord& Out)
	{
		Out.Events.clear();
		BuildShotStartSnapshot(Input, Out.Start);
		Out.Stroke = StrokeRecord{};
		Out.End = ShotEndSnapshot{};
		Out.Truncated = false;
	}

	bool AppendRecordEvent(const ShotEvent& Event, ShotRecord& Out)
	{
		if (!IsRecordRelevant(Event.Type))
		{
			return false;
		}
		if (Out.Events.size() >= Out.Events.capacity())
		{
			Out.Truncated = true; // never reallocate inside the event loop
			return false;
		}
		RecordEvent R;
		ToRecordEvent(Event, static_cast<std::uint32_t>(Out.Events.size()), R);
		Out.Events.push_back(R);
		return true;
	}

	void FinishShotRecord(const SimInput& Input, const ShotResult& Result, ShotRecord& Out)
	{
		SortRecordEvents(Out.Events);

		// Pocket surrounds on the rail top: the pocket from the geometry (the physics event carries the polygon).
		if (Input.Table != nullptr)
		{
			const TableGeometry& G = *Input.Table;
			for (RecordEvent& E : Out.Events)
			{
				if (E.Type != RecordEventType::BallRailTop || E.Feature != 0xFF)
				{
					continue;
				}
				const double Index = E.Value;
				if (Index >= 0.0 && Index < static_cast<double>(G.RailTops.Size()))
				{
					const PocketId Pocket = G.RailTops[static_cast<int>(Index)].Pocket;
					E.Side = static_cast<std::int8_t>(Pocket != PocketId::None ? static_cast<int>(Pocket) : 0);
				}
			}
		}

		// Stroke (rules.md 3.2).
		StrokeRecord& Stroke = Out.Stroke;
		Stroke = StrokeRecord{};
		for (int i = 0; i < Input.Strikes.Size(); ++i)
		{
			const StrikeRequest& Request = Input.Strikes[i];
			StrokeInfo Info;
			Info.Ball = Request.Ball;
			Info.TipClothContact = Request.Input.TipTouchesCloth;
			Info.Miscue = i < Result.Strikes.Size() && Result.Strikes[i].Result.Miscue;
			Info.CueElevation = Request.Input.Elevation;
			Stroke.Strokes.PushBack(Info);
		}
		for (const NonTipContact& C : Input.Context.NonTipContacts)
		{
			if (!Stroke.NonTipContacts.PushBack(C))
			{
				Stroke.Overflow = true;
			}
		}
		bool Complete = BuildTipContacts(Input, Result, Out);

		// End snapshot (rules.md 3.4).
		ShotEndSnapshot& End = Out.End;
		End = ShotEndSnapshot{};
		End.StopTime = Result.StopTime;
		Vec3 Rest[kMaxBalls];
		bool Resting[kMaxBalls] = {};
		const double Tolerance = Input.Context.FrozenTolerance;
		for (int b = 0; b < kMaxBalls; ++b)
		{
			const SimBall& Ball = Input.Balls[b];
			if (!Ball.InPlay)
			{
				continue;
			}
			const BallFinal& Final = Result.Finals[b];
			BallEnd& E = End.Balls[b];
			switch (Final.Status)
			{
			case BallFinalStatus::OnTable:
				E.Status = BallEndStatus::OnTable;
				Rest[b] = Final.State.Position;
				Resting[b] = OnCloth(Final.State, Ball.Spec.Radius, Tolerance);
				break;
			case BallFinalStatus::Pocketed:
				E.Status = BallEndStatus::Pocketed;
				E.Pocket = Final.Pocket;
				break;
			case BallFinalStatus::OffTable: E.Status = BallEndStatus::OffTable; break;
			case BallFinalStatus::NotInPlay:
				// No final status for a simulated ball (the simulation did not finish): keep it where it started.
				E.Status = BallEndStatus::OnTable;
				Rest[b] = Ball.State.Position;
				Resting[b] = OnCloth(Ball.State, Ball.Spec.Radius, Tolerance);
				Complete = false;
				break;
			}
			E.Position = XY(Final.Status == BallFinalStatus::NotInPlay ? Ball.State.Position : Final.State.Position);
		}
		for (int b = 0; b < kMaxBalls; ++b)
		{
			if (!Resting[b])
			{
				continue;
			}
			const double Rb = Input.Balls[b].Spec.Radius;
			BallEnd& E = End.Balls[b];
			if (Input.Table != nullptr)
			{
				E.FrozenToRail = FrozenRails(*Input.Table, Input.Params.Cushion, XY(Rest[b]), Rb, Tolerance);
			}
			for (int j = 0; j < kMaxBalls; ++j)
			{
				if (j != b && Resting[j] && BallsTouch(Rest[b], Rb, Rest[j], Input.Balls[j].Spec.Radius, Tolerance))
				{
					E.FrozenToBalls |= Bit(j);
				}
			}
		}

		const SimStatus Status = Result.Status;
		const bool Failed = Status == SimStatus::InvalidInput || Status == SimStatus::Aborted || Status == SimStatus::HorizonReached
			|| Result.Diagnostics.IslandBudgetExceeded || Result.Diagnostics.RecordOverflow;
		if (!Complete || Stroke.Overflow || Failed)
		{
			Out.Truncated = true;
		}
	}

	void BuildShotRecord(const SimInput& Input, const ShotResult& Result, ShotRecord& Out)
	{
		BeginShotRecord(Input, Out);
		if (Out.Events.capacity() < Result.Events.size())
		{
			Out.Events.reserve(Result.Events.size()); // standalone path only (never inside the event loop)
		}
		for (const ShotEvent& Event : Result.Events)
		{
			AppendRecordEvent(Event, Out);
		}
		FinishShotRecord(Input, Result, Out);
		if (Result.Diagnostics.EventLogOverflow || !Input.Record.LogObservers || !Input.Record.EventStates)
		{
			Out.Truncated = true; // the log lacks events or positions the record needs
		}
	}

	rules::RulesTable BuildRulesTable(const TableGeometry& Geometry, double NominalRadius, const double* Radii, int Count)
	{
		rules::RulesTable T = rules::MakeRulesTable(Geometry.Spec.Length, Geometry.Spec.Width, NominalRadius);
		if (Geometry.HalfLength > 0.0)
		{
			// Built geometry: its landmarks (the same expressions, the single source).
			const TableLandmarks& L = Geometry.Landmarks;
			T.HeadStringX = L.HeadStringX;
			T.FootStringX = L.FootStringX;
			T.BaulkX = L.BaulkX;
			T.HeadSpot = L.HeadSpot;
			T.FootSpot = L.FootSpot;
			T.CenterSpot = L.CenterSpot;
		}
		for (int b = 0; b < rules::kRulesBallCount; ++b)
		{
			T.BallRadius[b] = (Radii != nullptr && b < Count && Radii[b] > 0.0) ? Radii[b] : NominalRadius;
		}
		T.PocketCount = 0;
		for (int k = 0; k < Geometry.Pockets.Size() && k < kPocketCount; ++k)
		{
			const PocketGeometry& P = Geometry.Pockets[k];
			const int Index = P.Id != PocketId::None ? static_cast<int>(P.Id) : k;
			if (Index < 0 || Index >= kPocketCount)
			{
				continue;
			}
			rules::PocketOpening& O = T.Pockets[Index];
			O.JawPoint[0] = P.JawPoint[0];
			O.JawPoint[1] = P.JawPoint[1];
			O.Axis = P.Axis;
			O.CaptureCenter = P.CaptureCenter;
			O.DropEdgeRadius = P.DropEdgeRadius;
			T.PocketCount = Index + 1 > T.PocketCount ? Index + 1 : T.PocketCount;
		}
		return T;
	}
}
