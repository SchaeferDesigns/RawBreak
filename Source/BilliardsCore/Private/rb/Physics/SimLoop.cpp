#include "rb/Core/FpGuard.h"
// Owner: WP-6a (simulator core loop). Spec: Docs/architecture.md 8.1-8.3 (slots, versions, queue), 8.7 (observers), 8.10
// (recording, the orientation law), 8.11 (tilt chains: re-anchor, refresh slots; orientation of marked balls).
// This file: the services of SimInternal.h (ReplaceSegment, EmitEvent, MakeTerminal, BallStateAt, BallStateForEvent), segment
// replacement and recording, slot predictions, observers and tip-contact ends, the stop of all balls.
#include "SimInternal.h"

#include "rb/Math/Polynomial.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/Playback.h"
#include "rb/Shot/ShotRecordBuilder.h"

namespace rb::sim
{
	namespace
	{
		const BallSpec& SpecOf(const Workspace& Ws, int Ball) { return Ws.Input->Balls[Ball].Spec; }

		bool Translates(MotionState State)
		{
			return State == MotionState::Sliding || State == MotionState::Rolling || State == MotionState::Airborne || State == MotionState::PocketPivot ||
				State == MotionState::PocketFall;
		}

		// Local time limit of a segment for searches: its end, capped by the time horizon.
		double SegmentLimit(const Workspace& Ws, const MotionSegment& Seg)
		{
			const double Horizon = Ws.Params.Numerics.TimeHorizon;
			return Seg.TauEnd < kInfinity ? Min(Seg.T0 + Seg.TauEnd, Horizon) : Horizon;
		}

		TrajectorySegment AsTrajectory(const MotionSegment& Seg)
		{
			TrajectorySegment T;
			T.Motion = Seg;
			T.T1 = kInfinity;
			T.Kind = SegmentKind::Analytic;
			return T;
		}

		void PushTrack(Workspace& Ws, int Ball, const TrajectorySegment& Segment)
		{
			std::vector<TrajectorySegment>& Track = Ws.Result->Tracks[Ball].Segments;
			if (!Ws.Cache.TrackFull[Ball] && static_cast<int>(Track.size()) < Ws.Caps.MaxSegmentsPerBall)
			{
				Track.push_back(Segment);
			}
			else
			{
				Ws.Result->Diagnostics.TrajectoryOverflow = true;
				Ws.Cache.TrackFull[Ball] = true; // the track ends here (its last segment is closed): no later piece is appended
			}
		}

		// Adaptive Sampled pieces of a pivot over [From, To] (architecture 8.8 sampling rules: a new piece when the chord would
		// deviate from the path by more than SampleTolerance, at the latest after SampleMaxInterval); Omega0 of each piece is its
		// rotation (Simpson integral of w) over its duration, so the orientation law of the piece is exact for that rotation.
		// Returns the orientation at To; appends the pieces to the track when Record.
		Quat SamplePivot(Workspace& Ws, int Ball, double From, double To, Quat Q, bool Record)
		{
			const PivotPath& Path = Ws.Balls[Ball].Pivot;
			const NumericsConfig& N = Ws.Params.Numerics;
			double Ta = From;
			BallState Sa = EvaluatePivot(Path, Ta - Path.T0);
			for (int Pieces = 0; Ta < To && Pieces < 100000; ++Pieces)
			{
				double Dt = Min(N.SampleMaxInterval, To - Ta);
				double Tb = Ta + Dt;
				BallState Sb;
				for (int Halvings = 0; Halvings < 40; ++Halvings)
				{
					Tb = Dt >= To - Ta ? To : Ta + Dt;
					Sb = EvaluatePivot(Path, Tb - Path.T0);
					bool WithinTolerance = true;
					for (int k = 1; k <= 3 && WithinTolerance; ++k)
					{
						const double F = 0.25 * static_cast<double>(k);
						const Vec3 Chord = Sa.Position + (Sb.Position - Sa.Position) * F;
						const Vec3 OnPath = EvaluatePivot(Path, Ta + (Tb - Ta) * F - Path.T0).Position;
						WithinTolerance = LengthSquared(OnPath - Chord) <= N.SampleTolerance * N.SampleTolerance;
					}
					if (WithinTolerance)
					{
						break;
					}
					Dt *= 0.5;
				}
				const double Duration = Tb - Ta;
				if (!(Duration > 0.0))
				{
					break;
				}
				const BallState Sm = EvaluatePivot(Path, 0.5 * (Ta + Tb) - Path.T0);
				const Vec3 Rotation = (Sa.Omega + Sm.Omega * 4.0 + Sb.Omega) * (Duration / 6.0);
				TrajectorySegment Piece;
				Piece.Kind = SegmentKind::Sampled;
				Piece.Motion.State = MotionState::PocketPivot;
				Piece.Motion.T0 = Ta;
				Piece.Motion.TauEnd = Duration;
				Piece.Motion.Radius = Path.Radius;
				Piece.Motion.Pos0 = Sa.Position;
				Piece.Motion.Vel0 = (Sb.Position - Sa.Position) / Duration;
				Piece.Motion.Omega0 = Rotation / Duration;
				Piece.T1 = Tb;
				Piece.EndPosition = Sb.Position;
				Piece.Orientation0 = Q;
				Q = SegmentOrientationAt(Q, Piece, Duration);
				if (Record)
				{
					PushTrack(Ws, Ball, Piece);
				}
				Ta = Tb;
				Sa = Sb;
			}
			return Q;
		}

		// Mean angular velocity of an island member's open Sampled stretch [From, To]: BallSlot::RotationAccumulator (the integral of
		// w since SampleStart) over the duration, so the orientation law of the piece rotates by exactly the accumulated vector.
		Vec3 IslandMeanOmega(const BallSlot& Slot, double From, double To)
		{
			const double Duration = To - From;
			return Duration > 0.0 ? Slot.RotationAccumulator / Duration : Vec3{};
		}

		bool ObserverLess(const Observer& A, const Observer& B)
		{
			if (A.Time != B.Time)
			{
				return A.Time < B.Time;
			}
			if (A.Kind != B.Kind)
			{
				return A.Kind < B.Kind;
			}
			return A.Index < B.Index;
		}

		// Sorted insert; a full list keeps the earliest observers (a later one is dropped).
		void InsertObserver(BallSlot& Slot, const Observer& O)
		{
			FixedVector<Observer, kMaxObserversPerBall>& List = Slot.Observers;
			if (List.IsFull())
			{
				if (!ObserverLess(O, List.Back()))
				{
					return;
				}
				List.PopBack();
			}
			List.PushBack(O);
			for (int i = List.Size() - 1; i > 0 && ObserverLess(List[i], List[i - 1]); --i)
			{
				const Observer Tmp = List[i];
				List[i] = List[i - 1];
				List[i - 1] = Tmp;
			}
		}

		// First local time in [Lo, Hi] at which F rises through 0 (Lo itself if F(Lo) > 0); kInfinity if none.
		double FirstUpCrossing(Polynomial F, double Lo, double Hi)
		{
			if (!(Hi >= Lo))
			{
				return kInfinity;
			}
			F.Trim(0.0);
			if (F.Eval(Lo) > 0.0)
			{
				return Lo;
			}
			if (F.Degree == 0 || !(Hi > Lo))
			{
				return kInfinity;
			}
			double Roots[Polynomial::kMaxDegree];
			const int Count = SolveInInterval(F, Lo, Hi, Roots);
			const Polynomial D = F.Derivative();
			for (int i = 0; i < Count; ++i)
			{
				if (D.Eval(Roots[i]) > 0.0)
				{
					return Roots[i];
				}
			}
			return kInfinity;
		}

		// p(tau) . N - Offset for a plan direction N (a quadratic in tau).
		Polynomial PlanAlong(const MotionSegment& Seg, const Vec2& Origin, const Vec2& N, double Offset)
		{
			Polynomial F;
			F.Degree = 2;
			F.c[0] = Dot(XY(Seg.Pos0) - Origin, N) - Offset;
			F.c[1] = Dot(XY(Seg.Vel0), N);
			F.c[2] = Dot(XY(Seg.Accel2), N);
			return F;
		}

		// |q(tau) - Center|^2 - Radius^2 in the plan (a quartic in tau).
		Polynomial PlanDistanceSquared(const MotionSegment& Seg, const Vec2& Center, double Radius)
		{
			Polynomial F;
			F.Degree = 4;
			const double C[2] = {Seg.Pos0.x - Center.x, Seg.Pos0.y - Center.y};
			const double B[2] = {Seg.Vel0.x, Seg.Vel0.y};
			const double A[2] = {Seg.Accel2.x, Seg.Accel2.y};
			for (int k = 0; k < 2; ++k)
			{
				F.c[0] += C[k] * C[k];
				F.c[1] += 2.0 * B[k] * C[k];
				F.c[2] += B[k] * B[k] + 2.0 * A[k] * C[k];
				F.c[3] += 2.0 * A[k] * B[k];
				F.c[4] += A[k] * A[k];
			}
			F.c[0] -= Radius * Radius;
			return F;
		}

		double CushionContactOffset(const Workspace& Ws, double Radius, double Height)
		{
			return ComputeCushionContact(Radius, Height, Ws.Detection.NoseProfileRadius, Ws.Detection.PooltoolCompat).HorizontalOffset;
		}

		// Time at which the ball leaves rail feature Rail (0..5 nose, 6 + 2 pocket + side jaw arc / facing) by LeaveDistance on its
		// current segment, searched from From (absolute; kInfinity if not within the segment).
		double RailLeaveTime(const Workspace& Ws, int Ball, int Rail, double From)
		{
			const MotionSegment& Seg = Ws.Balls[Ball].Seg;
			const TableGeometry& Table = *Ws.Input->Table;
			const double Leave = Ws.Params.Numerics.LeaveDistance;
			const double R = SpecOf(Ws, Ball).Radius;
			const double Lo = Max(From, Seg.T0) - Seg.T0;
			const double Hi = SegmentLimit(Ws, Seg) - Seg.T0;
			if (Rail < kCushionCount)
			{
				if (Rail >= Table.Noses.Size() || !Table.Noses[Rail].Present)
				{
					return kInfinity;
				}
				const NoseSegment& Nose = Table.Noses[Rail];
				const double Tau = FirstUpCrossing(PlanAlong(Seg, Nose.Start, Nose.InwardNormal, CushionContactOffset(Ws, R, Nose.Height) + Leave), Lo, Hi);
				return Tau < kInfinity ? Seg.T0 + Tau : kInfinity;
			}
			const int Index = Rail - kCushionCount;
			const Vec3 P = PositionAt(Seg, Lo);
			double Latest = Seg.T0 + Lo;
			if (Index < Table.JawArcs.Size() && loop::FeatureGap(Ws, {TableFeatureKind::JawArc, static_cast<std::uint8_t>(Index), 0}, P, R) <= Leave)
			{
				const JawArc& Arc = Table.JawArcs[Index];
				const double Tau = FirstUpCrossing(PlanDistanceSquared(Seg, Arc.Center, Arc.Radius + CushionContactOffset(Ws, R, Arc.Height) + Leave), Lo, Hi);
				Latest = Max(Latest, Tau < kInfinity ? Seg.T0 + Tau : kInfinity);
			}
			if (Index < Table.Facings.Size() && loop::FeatureGap(Ws, {TableFeatureKind::FacingFace, static_cast<std::uint8_t>(Index), 0}, P, R) <= Leave)
			{
				const Facing& Face = Table.Facings[Index];
				const double Sf = FacingContactOffset(R, Face.TopHeight, Face.Backdraft);
				const double Tau = FirstUpCrossing(PlanAlong(Seg, Face.Start, Face.PocketNormal, Sf + Leave), Lo, Hi);
				Latest = Max(Latest, Tau < kInfinity ? Seg.T0 + Tau : kInfinity);
			}
			return Latest;
		}

		// Pair observers owned by Owner with partner Partner (jump-over while airborne, ball freeze-leave; architecture 8.7).
		void AddPairObservers(Workspace& Ws, int Owner, int Partner, double From)
		{
			if (Owner == Partner || !loop::IsLive(Ws, Partner) || Ws.Balls[Partner].InIsland)
			{
				return;
			}
			BallSlot& O = Ws.Balls[Owner];
			const BallSlot& P = Ws.Balls[Partner];
			const std::uint32_t Bit = 1u << Partner;
			const double Start = Max(From, Max(O.Seg.T0, P.Seg.T0));
			const double Horizon = Ws.Params.Numerics.TimeHorizon;
			const double RO = SpecOf(Ws, Owner).Radius;
			const double RP = SpecOf(Ws, Partner).Radius;
			if ((O.JumpPending & Bit) != 0u)
			{
				const ContactPrediction Leave = PredictPlanDistanceCrossing(O.Seg, RO, P.Seg, RP, false, Horizon, Ws.Params.Numerics);
				if (Leave.Found && Leave.Time >= Start)
				{
					InsertObserver(O, {Leave.Time, ObserverKind::JumpLeave, static_cast<std::uint8_t>(Partner), 0});
				}
			}
			else if (O.Seg.State == MotionState::Airborne)
			{
				const ContactPrediction Enter = PredictPlanDistanceCrossing(O.Seg, RO, P.Seg, RP, true, Horizon, Ws.Params.Numerics);
				if (Enter.Found && Enter.Time >= Start)
				{
					InsertObserver(O, {Enter.Time, ObserverKind::JumpEnter, static_cast<std::uint8_t>(Partner), 0});
				}
			}
			if (Owner < Partner && (O.InitialFreezeBalls & Bit) != 0u && (Translates(O.Seg.State) || Translates(P.Seg.State)))
			{
				const double RefTime = Max(O.Seg.T0, P.Seg.T0);
				const double End = Min(SegmentLimit(Ws, O.Seg), SegmentLimit(Ws, P.Seg));
				const Polynomial F = BallBallGapPolynomial(O.Seg, RO + Ws.Params.Numerics.LeaveDistance, P.Seg, RP, RefTime);
				const double Tau = FirstUpCrossing(F, Start - RefTime, End - RefTime);
				if (Tau < kInfinity)
				{
					InsertObserver(O, {RefTime + Tau, ObserverKind::FreezeLeave, static_cast<std::uint8_t>(32 + Partner), 0});
				}
			}
		}

		Aabb3 BoxOf(Workspace& Ws, int Ball)
		{
			LoopCache& Cache = Ws.Cache;
			const BallSlot& Slot = Ws.Balls[Ball];
			if (!Cache.BoxValid[Ball] || Cache.BoxVersion[Ball] != Slot.Version)
			{
				Cache.Box[Ball] = SweptBounds(Slot.Seg, 0.0, Ws.Params.Numerics.TimeHorizon - Slot.Seg.T0);
				Cache.BoxVersion[Ball] = Slot.Version;
				Cache.BoxValid[Ball] = true;
			}
			return Cache.Box[Ball];
		}

		void LogOverlap(Workspace& Ws, int A, int B, double Magnitude)
		{
			++Ws.Result->Diagnostics.OverlapWarnings;
			ShotEvent Event;
			Event.Time = Ws.Now;
			Event.Type = ShotEventType::Diagnostic;
			Event.A = static_cast<BallId>(A);
			Event.B = static_cast<BallId>(B);
			Event.Value = Magnitude;
			EmitEvent(Ws, Event);
		}

		EventTier TierOf(TableFeatureKind Kind)
		{
			switch (Kind)
			{
			case TableFeatureKind::NoseSegment:
			case TableFeatureKind::JawArc:
			case TableFeatureKind::FacingFace:
			case TableFeatureKind::FacingTopEdge:
			case TableFeatureKind::RailTop:
			case TableFeatureKind::RailTopEdge:
			case TableFeatureKind::SupportExit:
				return EventTier::Cushion;
			case TableFeatureKind::DropEdge:
			case TableFeatureKind::LinerWall:
			case TableFeatureKind::RimTorus:
			case TableFeatureKind::CaptureDepth:
			case TableFeatureKind::PocketExit:
			case TableFeatureKind::CaptureCircle:
				return EventTier::Pocket;
			case TableFeatureKind::SlateLanding:
				return EventTier::Slate;
			case TableFeatureKind::OuterBoundary:
			case TableFeatureKind::LampApex:
				return EventTier::Boundary;
			case TableFeatureKind::None:
				break;
			}
			return EventTier::Transition;
		}

		void PredictEndSlot(Workspace& Ws, int Ball)
		{
			const BallSlot& Slot = Ws.Balls[Ball];
			const MotionSegment& Seg = Slot.Seg;
			if (!(Seg.TauEnd < kInfinity))
			{
				return;
			}
			QueuedEvent E;
			E.Time = Seg.T0 + Seg.TauEnd;
			E.BallA = static_cast<std::uint8_t>(Ball);
			E.VersionA = Slot.Version;
			if (Seg.State == MotionState::Airborne)
			{
				E.Tier = EventTier::Slate; // landing (the end slot is its only owner, review item 22)
				E.Kind = QueuedEventKind::Transition;
				E.FeatureKind = kTransitionFeature;
			}
			else if (Seg.Tilt.Active && Seg.Tilt.EndsInRefresh)
			{
				E.Tier = EventTier::Transition;
				E.Kind = QueuedEventKind::TiltRefresh;
				E.FeatureKind = kTiltRefreshFeature;
			}
			else
			{
				E.Tier = EventTier::Transition; // motion transition or pivot end
				E.Kind = QueuedEventKind::Transition;
				E.FeatureKind = kTransitionFeature;
			}
			loop::Push(Ws, E);
		}

		void PredictTableSlot(Workspace& Ws, int Ball)
		{
			const BallSlot& Slot = Ws.Balls[Ball];
			if (!Translates(Slot.Seg.State))
			{
				return;
			}
			const PhysicsParams& Params = Ws.Params;
			const FeaturePrediction P = PredictTableEvent(Slot.Seg, SpecOf(Ws, Ball), Slot.Context, *Ws.Input->Table, Ws.Input->Environment, Ws.Detection,
				Params.Gravity, Params.Numerics.TimeHorizon, Params.Numerics);
			++Ws.Result->Diagnostics.Predictions;
			if ((P.Contact.Flags & ContactFlags::Overlap) != 0u)
			{
				LogOverlap(Ws, Ball, kNoBall, 0.0);
			}
			if (!P.Contact.Found)
			{
				return;
			}
			QueuedEvent E;
			E.Time = P.Contact.Time;
			E.Tier = TierOf(P.Feature.Kind);
			E.Kind = QueuedEventKind::TableFeature;
			E.BallA = static_cast<std::uint8_t>(Ball);
			E.FeatureKind = static_cast<std::uint8_t>(P.Feature.Kind);
			E.FeatureIndex = P.Feature.Index;
			E.FeatureSub = P.Feature.SubIndex;
			E.Flags = P.Contact.Flags;
			E.VersionA = Slot.Version;
			loop::Push(Ws, E);
		}

		void PredictPair(Workspace& Ws, int I, int J)
		{
			const BallSlot& A = Ws.Balls[I];
			const BallSlot& B = Ws.Balls[J];
			if (!Translates(A.Seg.State) && !Translates(B.Seg.State))
			{
				return; // stationary / spinning pairs never meet (8.2)
			}
			const double RA = SpecOf(Ws, I).Radius;
			const double RB = SpecOf(Ws, J).Radius;
			const NumericsConfig& N = Ws.Params.Numerics;
			if (!BoxOf(Ws, I).Inflated(RA + RB + N.ContactTol).Overlaps(BoxOf(Ws, J)))
			{
				return; // swept-AABB broad phase (collisions 3.5, prior-art 5.10)
			}
			const ContactPrediction P = PredictBallBall(A.Seg, RA, B.Seg, RB, N.TimeHorizon, N);
			++Ws.Result->Diagnostics.Predictions;
			if ((P.Flags & ContactFlags::Overlap) != 0u)
			{
				const double RefTime = Max(A.Seg.T0, B.Seg.T0);
				LogOverlap(Ws, I, J, (RA + RB) - Length(PositionAt(B.Seg, RefTime - B.Seg.T0) - PositionAt(A.Seg, RefTime - A.Seg.T0)));
			}
			if (!P.Found)
			{
				return;
			}
			QueuedEvent E;
			E.Time = P.Time;
			E.Tier = EventTier::BallBall;
			E.Kind = QueuedEventKind::BallBall;
			E.BallA = static_cast<std::uint8_t>(I);
			E.BallB = static_cast<std::uint8_t>(J);
			E.Flags = P.Flags;
			E.VersionA = A.Version;
			E.VersionB = B.Version;
			loop::Push(Ws, E);
		}

		void EmitObserver(Workspace& Ws, int Ball)
		{
			BallSlot& Slot = Ws.Balls[Ball];
			const Observer O = Slot.Observers[0];
			Slot.Observers.RemoveAt(0);
			// A pair observer was predicted against the partner's event-mode segment. Once the partner is integrated by an island
			// that segment no longer describes it: the observer is void (the island evaluates its members' freeze-leave and
			// jump-over distances, 8.7), and the partner's return to event mode recomputes it (PredictBalls ->
			// RecomputePairObservers).
			const bool PairObserver = O.Kind == ObserverKind::JumpEnter || O.Kind == ObserverKind::JumpLeave || (O.Kind == ObserverKind::FreezeLeave && O.Index >= 32);
			if (PairObserver)
			{
				const int Partner = O.Kind == ObserverKind::FreezeLeave ? O.Index - 32 : O.Index;
				if (Partner < kMaxBalls && Ws.Balls[Partner].InIsland)
				{
					return;
				}
			}
			switch (O.Kind)
			{
			case ObserverKind::LineCross:
			{
				ShotEvent Event;
				Event.Time = O.Time;
				Event.Type = ShotEventType::BallLineCross;
				Event.A = static_cast<BallId>(Ball);
				Event.Feature = O.Index;
				Event.SubFeature = static_cast<std::uint8_t>(O.Direction > 0 ? 0 : 1);
				Event.Pre[0] = BallStateAt(Ws, Ball, O.Time); // the record's position, whatever EventStates (EmitEvent)
				Event.Post[0] = Event.Pre[0];
				EmitEvent(Ws, Event);
				break;
			}
			case ObserverKind::FreezeLeave:
				if (O.Index < 32)
				{
					Slot.InitialFreezeRails &= ~(1u << O.Index);
				}
				else
				{
					const int Other = O.Index - 32;
					Slot.InitialFreezeBalls &= ~(1u << Other);
					Ws.Balls[Other].InitialFreezeBalls &= ~(1u << Ball);
				}
				break;
			case ObserverKind::JumpEnter:
			{
				const int Other = O.Index;
				Slot.JumpPending |= 1u << Other;
				Slot.ContactSinceJump &= ~(1u << Other);
				if (loop::IsLive(Ws, Other) && !Ws.Balls[Other].InIsland)
				{
					const ContactPrediction Leave = PredictPlanDistanceCrossing(Slot.Seg, SpecOf(Ws, Ball).Radius, Ws.Balls[Other].Seg, SpecOf(Ws, Other).Radius, false,
						Ws.Params.Numerics.TimeHorizon, Ws.Params.Numerics);
					if (Leave.Found && Leave.Time >= O.Time)
					{
						InsertObserver(Slot, {Leave.Time, ObserverKind::JumpLeave, O.Index, 0});
					}
				}
				break;
			}
			case ObserverKind::JumpLeave:
			{
				const std::uint32_t Bit = 1u << O.Index;
				if ((Slot.JumpPending & Bit) != 0u)
				{
					if ((Slot.ContactSinceJump & Bit) == 0u)
					{
						ShotEvent Event;
						Event.Time = O.Time;
						Event.Type = ShotEventType::BallJumpedOver;
						Event.A = static_cast<BallId>(Ball);
						Event.B = static_cast<BallId>(O.Index);
						Event.Pre[0] = BallStateAt(Ws, Ball, O.Time);
						Event.Pre[1] = loop::CurrentState(Ws, O.Index, O.Time);
						Event.Post[0] = Event.Pre[0];
						Event.Post[1] = Event.Pre[1];
						EmitEvent(Ws, Event);
					}
					Slot.JumpPending &= ~Bit;
					Slot.ContactSinceJump &= ~Bit;
				}
				break;
			}
			}
		}
	}

	// =================================================================================================================
	// Services (SimInternal.h)
	// =================================================================================================================
	BallState BallStateAt(const Workspace& Ws, int Ball, double Time)
	{
		const BallSlot& Slot = Ws.Balls[Ball];
		if (Slot.Seg.State == MotionState::PocketPivot)
		{
			return EvaluatePivot(Slot.Pivot, Time - Slot.Pivot.T0);
		}
		return EvaluateSegment(Slot.Seg, Time - Slot.Seg.T0);
	}

	BallState BallStateForEvent(const Workspace& Ws, int Ball, double Time)
	{
		// Exact re-anchor (architecture 8.11, human-factors 4.5.3): position from the piece, velocity and spin from the exact
		// pursuit state; EvaluateSegmentForEvent is EvaluateSegment bitwise for every non-tilt segment (A-SIM-9).
		const BallSlot& Slot = Ws.Balls[Ball];
		if (Slot.Seg.State == MotionState::PocketPivot)
		{
			return EvaluatePivot(Slot.Pivot, Time - Slot.Pivot.T0);
		}
		return EvaluateSegmentForEvent(Slot.Seg, Time - Slot.Seg.T0);
	}

	void ReplaceSegment(Workspace& Ws, int Ball, const BallState& State, double Time)
	{
		loop::SetSegment(Ws, Ball, State, Time, false);
		loop::PredictBalls(Ws, 1u << Ball);
	}

	void EmitEvent(Workspace& Ws, const ShotEvent& Event)
	{
		ShotResult& Result = *Ws.Result;
		const SimInput& Input = *Ws.Input;

		// Loop bookkeeping from the events themselves (island records included).
		const int A = Event.A;
		const int B = Event.B;
		switch (Event.Type)
		{
		case ShotEventType::BallBall:
			if (A >= 0 && A < kMaxBalls && B >= 0 && B < kMaxBalls)
			{
				if ((Ws.Balls[A].JumpPending & (1u << B)) != 0u)
				{
					Ws.Balls[A].ContactSinceJump |= 1u << B;
				}
				if ((Ws.Balls[B].JumpPending & (1u << A)) != 0u)
				{
					Ws.Balls[B].ContactSinceJump |= 1u << A;
				}
				for (TipSlot& Tip : Ws.Tips)
				{
					const int Struck = Tip.Path.StruckBall;
					const int Other = Struck == A ? B : (Struck == B ? A : kNoBall);
					if (Struck != kNoBall && Other != kNoBall && Other != Tip.FrozenTarget)
					{
						Tip.StruckTouchedOther = true;
					}
				}
			}
			break;
		case ShotEventType::BallCushion:
		case ShotEventType::BallJaw:
		case ShotEventType::BallRailTop:
		case ShotEventType::BallLiner:
			for (TipSlot& Tip : Ws.Tips)
			{
				if (Tip.Path.StruckBall != kNoBall && Tip.Path.StruckBall == Event.A)
				{
					Tip.StruckTouchedOther = true;
				}
			}
			break;
		default:
			break;
		}

		// Physics log (switches, capacity).
		bool Log = true;
		if (Event.Type == ShotEventType::MotionTransition || Event.Type == ShotEventType::TiltRefresh)
		{
			Log = Input.Record.LogTransitions;
		}
		else if (Event.Type == ShotEventType::BallLineCross || Event.Type == ShotEventType::BallJumpedOver)
		{
			Log = Input.Record.LogObservers;
		}
		if (Log)
		{
			if (static_cast<int>(Result.Events.size()) < Ws.Caps.MaxLoggedEvents)
			{
				Result.Events.push_back(Event);
				if (!Input.Record.EventStates)
				{
					ShotEvent& Back = Result.Events.back();
					Back.Pre[0] = Back.Pre[1] = Back.Post[0] = Back.Post[1] = BallState{};
				}
			}
			else
			{
				Result.Diagnostics.EventLogOverflow = true;
			}
		}

		// Rules record: independent of the logging switches (RUL pitfall 17); tilt refreshes never belong to it.
		if (Input.Record.ShotRecord && Event.Type != ShotEventType::TiltRefresh && IsRecordRelevant(Event.Type))
		{
			// The overflow point is this simulator's MaxRecordEvents, not the vector's capacity (a ShotResult reserved larger by
			// another simulator must give the same record: deterministic, like the event log and the tracks).
			if (static_cast<int>(Result.Record.Events.size()) >= Ws.Caps.MaxRecordEvents)
			{
				Result.Record.Truncated = true;
				Result.Diagnostics.RecordOverflow = true;
			}
			else if (!AppendRecordEvent(Event, Result.Record))
			{
				Result.Diagnostics.RecordOverflow = true;
			}
		}
	}

	void MakeTerminal(Workspace& Ws, int Ball, MotionState Terminal, double Time, PocketId Pocket, OffTableReason Reason)
	{
		BallSlot& Slot = Ws.Balls[Ball];
		const Vec3 P = loop::CurrentPosition(Ws, Ball, Time);
		const Quat Q = loop::CloseOpenSegment(Ws, Ball, Time, P);
		++Slot.Version;
		Slot.InIsland = false;
		Slot.Observers.Clear();
		if (Pocket != PocketId::None)
		{
			Slot.Context.Pocket = Pocket;
		}
		MotionSegment Seg;
		Seg.State = Terminal == MotionState::Pocketed ? MotionState::Pocketed : MotionState::OffTable;
		Seg.T0 = Time;
		Seg.TauEnd = kInfinity;
		Seg.Radius = SpecOf(Ws, Ball).Radius;
		Seg.SupportZ = loop::SupportHeight(Ws, Ball);
		Seg.Pos0 = P;
		Slot.Seg = Seg;
		Slot.Orientation0 = Q;
		if (Ws.Input->Record.Trajectories)
		{
			TrajectorySegment T = AsTrajectory(Seg);
			T.Kind = SegmentKind::Terminal;
			T.Orientation0 = Q;
			PushTrack(Ws, Ball, T);
		}
		BallFinal& Final = Ws.Result->Finals[Ball];
		Final.Status = Seg.State == MotionState::Pocketed ? BallFinalStatus::Pocketed : BallFinalStatus::OffTable;
		Final.State = BallState{};
		Final.State.Position = P;
		Final.State.State = Seg.State;
		Final.Orientation = Q;
		Final.Pocket = Slot.Context.Pocket;
		Final.OffReason = Reason;
		Final.Time = Time;
		loop::PredictBalls(Ws, 1u << Ball); // tips and the pair observers of other balls lose this ball
	}

	// =================================================================================================================
	// Loop internals
	// =================================================================================================================
	namespace loop
	{
		bool IsLive(const Workspace& Ws, int Ball)
		{
			return Ball >= 0 && Ball < kMaxBalls && Ws.Balls[Ball].InPlay && !IsTerminal(Ws.Balls[Ball].Seg.State);
		}

		bool TracksOrientation(const Workspace& Ws, int Ball)
		{
			return Ws.Input->Record.Trajectories || (Ws.Params.ChalkCling && !Ws.Input->Balls[Ball].ChalkMarks.IsEmpty());
		}

		ClothParams SupportSurface(const Workspace& Ws, int Ball)
		{
			return Ws.Balls[Ball].Context.Support == SupportKind::RailCap ? RailCapSurface(Ws.Params.PocketContacts) : Ws.Params.Cloth;
		}

		double SupportHeight(const Workspace& Ws, int Ball)
		{
			return Ws.Balls[Ball].Context.Support == SupportKind::RailCap ? Ws.Input->Table->Spec.RailTopZ : 0.0;
		}

		BallState CurrentState(const Workspace& Ws, int Ball, double Time)
		{
			const BallSlot& Slot = Ws.Balls[Ball];
			if (Slot.InIsland && Ws.Island.Active)
			{
				const int Body = Ws.Island.Solver.FindBody(Ball);
				if (Body >= 0)
				{
					const IslandBody& B = Ws.Island.Solver.Body(Body);
					BallState S;
					S.Position = B.Position;
					S.Velocity = B.Velocity;
					S.Omega = B.Omega;
					S.State = B.ClothSupport ? MotionState::Sliding : MotionState::Airborne;
					return S;
				}
			}
			return BallStateAt(Ws, Ball, Time);
		}

		Vec3 CurrentPosition(const Workspace& Ws, int Ball, double Time) { return CurrentState(Ws, Ball, Time).Position; }

		Quat CloseOpenSegment(Workspace& Ws, int Ball, double Time, const Vec3& EndPosition)
		{
			BallSlot& Slot = Ws.Balls[Ball];
			const bool Tracked = TracksOrientation(Ws, Ball);
			Quat Q = Slot.Orientation0;
			const bool Pivot = Slot.Seg.State == MotionState::PocketPivot && !Slot.InIsland;
			// After this ball's track overflowed, its last recorded segment is not the current one (and stays closed): the
			// orientation continues from the slot like without trajectories.
			if (Ws.Input->Record.Trajectories && !Ws.Cache.TrackFull[Ball])
			{
				std::vector<TrajectorySegment>& Track = Ws.Result->Tracks[Ball].Segments;
				if (Track.empty())
				{
					return Q;
				}
				TrajectorySegment& Last = Track.back();
				if (Last.T1 == kInfinity)
				{
					if (Pivot && Last.Kind == SegmentKind::Sampled && Last.Motion.State == MotionState::PocketPivot)
					{
						// The pivot placeholder becomes its adaptive Sampled pieces.
						const Quat Q0 = Last.Orientation0;
						Track.pop_back();
						Q = SamplePivot(Ws, Ball, Slot.Pivot.T0, Time, Q0, true);
					}
					else
					{
						Last.T1 = Time;
						if (Last.Kind == SegmentKind::Sampled)
						{
							Last.EndPosition = EndPosition; // left open by an island
							if (Slot.InIsland)
							{
								Last.Motion.Omega0 = IslandMeanOmega(Slot, Last.Motion.T0, Time); // BallSlot: Omega0 = accumulator / duration
							}
						}
						Q = Last.Kind == SegmentKind::Terminal ? Last.Orientation0 : SegmentOrientationAt(Last.Orientation0, Last, Time - Last.Motion.T0);
					}
				}
				else
				{
					Q = SegmentOrientationAt(Last.Orientation0, Last, Last.T1 - Last.Motion.T0); // closed by an island
				}
				Slot.Orientation0 = Q;
				return Q;
			}
			if (Tracked && Slot.InIsland)
			{
				// Leaving an island without a recorded track: Orientation0 is the orientation at SampleStart and RotationAccumulator
				// the rotation since then (BallSlot), advanced by the law of the equivalent Sampled piece - bitwise what the
				// recorded piece gives with Trajectories on (8.10; the chalk cling of marked balls, the Finals orientation).
				TrajectorySegment Piece;
				Piece.Kind = SegmentKind::Sampled;
				Piece.Motion.State = Slot.Seg.State;
				Piece.Motion.T0 = Slot.SampleStart;
				Piece.Motion.Omega0 = IslandMeanOmega(Slot, Slot.SampleStart, Time);
				Piece.T1 = Time;
				Piece.Orientation0 = Q;
				Q = SegmentOrientationAt(Q, Piece, Time - Slot.SampleStart);
				Slot.Orientation0 = Q;
			}
			else if (Tracked)
			{
				if (Pivot)
				{
					Q = SamplePivot(Ws, Ball, Slot.Pivot.T0, Time, Q, false);
				}
				else if (!IsTerminal(Slot.Seg.State))
				{
					Q = SegmentOrientationAt(Q, AsTrajectory(Slot.Seg), Time - Slot.Seg.T0);
				}
				Slot.Orientation0 = Q;
			}
			return Q;
		}

		void SetSegment(Workspace& Ws, int Ball, const BallState& State, double Time, bool Initial)
		{
			BallSlot& Slot = Ws.Balls[Ball];
			const BallSpec& Spec = SpecOf(Ws, Ball);
			const PhysicsParams& Params = Ws.Params;
			const Quat Q = Initial ? Slot.Orientation0 : CloseOpenSegment(Ws, Ball, Time, State.Position);
			++Slot.Version;
			Slot.InIsland = false;

			BallState S = State;
			bool IsPivot = false;
			if (S.State == MotionState::PocketPivot)
			{
				const int Pocket = static_cast<int>(Slot.Context.Pocket);
				const TableGeometry& Table = *Ws.Input->Table;
				if (Slot.Context.Pocket != PocketId::None && Pocket < Table.Pockets.Size())
				{
					Slot.Pivot = MakePivotPath(S, Time, Table.Pockets[Pocket], Spec, Params.Gravity, Params.Numerics);
					if (Slot.Pivot.Result.Immediate)
					{
						S = PivotLeaveState(Slot.Pivot);
					}
					else
					{
						// First piece of the piecewise detection proxy (WP-10, VAL ROB-11; SimInternal.h).
						const PivotProxyPiece Piece = MakePivotProxyPiece(Slot.Pivot, 0.0, Slot.Pivot.T0, kPivotProxyTolerance);
						Slot.Seg = Piece.Seg;
						Slot.PivotPieceU = Piece.UpperU;
						Slot.PivotPieceLast = Piece.Last;
						IsPivot = true;
					}
				}
				else
				{
					S.State = MotionState::PocketFall; // no pocket in the context: ballistic
				}
			}
			if (!IsPivot)
			{
				Slot.Seg = MakeSegment(S, Time, Spec, SupportSurface(Ws, Ball), SupportHeight(Ws, Ball), Params.Gravity, Params.Tilt);
			}
			Slot.Orientation0 = Q;
			if (Slot.Seg.State == MotionState::Airborne)
			{
				const double Vz = Max(Slot.Seg.Vel0.z, 0.0);
				Slot.SequenceMaxZ = Max(Slot.SequenceMaxZ, Slot.Seg.Pos0.z + Vz * Vz / (2.0 * Params.Gravity));
			}
			if (Ws.Input->Record.Trajectories)
			{
				TrajectorySegment T = AsTrajectory(Slot.Seg);
				T.Kind = IsPivot ? SegmentKind::Sampled : SegmentKind::Analytic; // a pivot is recorded as Sampled pieces when closed
				T.Orientation0 = Q;
				PushTrack(Ws, Ball, T);
			}
			ComputeObservers(Ws, Ball, Time, true);
		}

		void AdvancePivotPiece(Workspace& Ws, int Ball, double Time)
		{
			// A node of the pivot's detection proxy (SimInternal.h): the next piece from the exact node of the true path. The
			// open track placeholder of the pivot and Orientation0 (the orientation at the pivot start) stay: the pivot is
			// recorded as Sampled pieces of the true path when it is closed (CloseOpenSegment).
			BallSlot& Slot = Ws.Balls[Ball];
			const PivotProxyPiece Piece = MakePivotProxyPiece(Slot.Pivot, Slot.PivotPieceU, Time, kPivotProxyTolerance);
			++Slot.Version;
			Slot.Seg = Piece.Seg;
			Slot.PivotPieceU = Piece.UpperU;
			Slot.PivotPieceLast = Piece.Last;
			ComputeObservers(Ws, Ball, Time, true);
			PredictBalls(Ws, 1u << Ball);
		}

		void PredictBalls(Workspace& Ws, std::uint32_t Changed)
		{
			for (int c = 0; c < kMaxBalls; ++c)
			{
				if ((Changed & (1u << c)) != 0u && IsLive(Ws, c) && !Ws.Balls[c].InIsland)
				{
					PredictEndSlot(Ws, c);
					PredictTableSlot(Ws, c);
				}
			}
			for (int c = 0; c < kMaxBalls; ++c)
			{
				if ((Changed & (1u << c)) == 0u || !IsLive(Ws, c) || Ws.Balls[c].InIsland)
				{
					continue;
				}
				for (int j = 0; j < kMaxBalls; ++j)
				{
					if (j == c || !IsLive(Ws, j) || Ws.Balls[j].InIsland || ((Changed & (1u << j)) != 0u && j < c))
					{
						continue; // each pair once
					}
					PredictPair(Ws, c < j ? c : j, c < j ? j : c);
				}
			}
			for (int c = 0; c < kMaxBalls; ++c)
			{
				if ((Changed & (1u << c)) == 0u)
				{
					continue;
				}
				for (int j = 0; j < kMaxBalls; ++j)
				{
					if (j != c && IsLive(Ws, j) && !Ws.Balls[j].InIsland)
					{
						RecomputePairObservers(Ws, j, c, Ws.Balls[c].Seg.T0);
					}
				}
			}
			PredictTips(Ws, -1, 0.0);
		}

		void PredictTips(Workspace& Ws, int ExcludeBall, double ExcludeTime)
		{
			const NumericsConfig& N = Ws.Params.Numerics;
			for (int s = 0; s < kMaxStrikes; ++s)
			{
				TipSlot& Tip = Ws.Tips[s];
				if (!Tip.Moving || Tip.InIsland)
				{
					continue;
				}
				++Tip.Version; // one live tip-slot entry (8.2)
				if (!(Tip.Path.StopTime > Ws.Now))
				{
					Tip.Moving = false;
					continue;
				}
				// The search starts at Now, never before: the path's own start can lie in the past (a later re-prediction of an
				// unchanged path), where a contact that was already processed without an impulse would be found again and queued
				// before Now (event time running backwards). Re-based exactly like EvaluateSegment's quadratic.
				MotionSegment TipSeg = CueTipAsSegment(Tip.Path);
				if (Ws.Now > TipSeg.T0)
				{
					const double Tau = Ws.Now - TipSeg.T0;
					TipSeg.Pos0 = TipSeg.Pos0 + TipSeg.Vel0 * Tau + TipSeg.Accel2 * (Tau * Tau);
					TipSeg.Vel0 = TipSeg.Vel0 + TipSeg.Accel2 * (2.0 * Tau);
					TipSeg.T0 = Ws.Now;
					TipSeg.TauEnd = Tip.Path.StopTime - Ws.Now;
				}
				const double TipRadius = Tip.Path.DomeRadius;
				const Aabb3 TipBox = SweptBounds(TipSeg, 0.0, kInfinity);
				ContactPrediction Best;
				int BestBall = -1;
				for (int j = 0; j < kMaxBalls; ++j)
				{
					if (!IsLive(Ws, j) || Ws.Balls[j].InIsland)
					{
						continue;
					}
					const double Rj = SpecOf(Ws, j).Radius;
					if (!TipBox.Inflated(TipRadius + Rj + N.ContactTol).Overlaps(BoxOf(Ws, j)))
					{
						continue;
					}
					MotionSegment From = TipSeg;
					if (Tip.StrikeContactOpen && j == Tip.Path.StruckBall)
					{
						// The strike's own contact (SimInternal.h): search only from the separation of the dome and the ball on.
						const MotionSegment& BallSeg = Ws.Balls[j].Seg;
						const double RefTime = Max(From.T0, BallSeg.T0);
						const double Gap = Length(PositionAt(BallSeg, RefTime - BallSeg.T0) - PositionAt(From, RefTime - From.T0)) - (TipRadius + Rj);
						if (Gap > N.ContactTol)
						{
							Tip.StrikeContactOpen = false; // seen apart: an ordinary tip slot from now on
						}
						else
						{
							const double End = Min(From.T0 + From.TauEnd, SegmentLimit(Ws, BallSeg));
							const double Separation = FirstUpCrossing(BallBallGapPolynomial(From, TipRadius + N.ContactTol, BallSeg, Rj, RefTime), 0.0, End - RefTime);
							if (!(Separation < kInfinity))
							{
								continue; // still touching until the tip or the ball segment ends (the next segment re-predicts)
							}
							const double Tau = RefTime + Separation - From.T0;
							From.Pos0 = From.Pos0 + From.Vel0 * Tau + From.Accel2 * (Tau * Tau);
							From.Vel0 = From.Vel0 + From.Accel2 * (2.0 * Tau);
							From.T0 += Tau;
							From.TauEnd -= Tau;
						}
					}
					const ContactPrediction P = PredictBallBall(From, TipRadius, Ws.Balls[j].Seg, Rj, N.TimeHorizon, N);
					++Ws.Result->Diagnostics.Predictions;
					if (!P.Found || (j == ExcludeBall && P.Time == ExcludeTime))
					{
						continue;
					}
					if (BestBall < 0 || P.Time < Best.Time)
					{
						Best = P;
						BestBall = j;
					}
				}
				if (BestBall < 0)
				{
					continue;
				}
				QueuedEvent E;
				E.Time = Best.Time;
				E.Tier = EventTier::Strike;
				E.Kind = QueuedEventKind::TipContact;
				E.BallA = static_cast<std::uint8_t>(BestBall);
				E.FeatureIndex = static_cast<std::uint8_t>(s);
				E.Flags = Best.Flags;
				E.VersionA = Ws.Balls[BestBall].Version;
				E.VersionB = Tip.Version;
				Push(Ws, E);
			}
		}

		bool IsValid(const Workspace& Ws, const QueuedEvent& Event)
		{
			const int A = Event.BallA;
			if (A >= kMaxBalls || !IsLive(Ws, A) || Ws.Balls[A].InIsland || Ws.Balls[A].Version != Event.VersionA)
			{
				return false;
			}
			switch (Event.Kind)
			{
			case QueuedEventKind::BallBall:
			{
				const int B = Event.BallB;
				return B < kMaxBalls && IsLive(Ws, B) && !Ws.Balls[B].InIsland && Ws.Balls[B].Version == Event.VersionB;
			}
			case QueuedEventKind::TipContact:
			{
				const int S = Event.FeatureIndex;
				return S < kMaxStrikes && Ws.Tips[S].Moving && !Ws.Tips[S].InIsland && Ws.Tips[S].Version == Event.VersionB;
			}
			case QueuedEventKind::CueStrike:
			case QueuedEventKind::TableFeature:
			case QueuedEventKind::Transition:
			case QueuedEventKind::TiltRefresh:
				break;
			}
			return true;
		}

		void Push(Workspace& Ws, const QueuedEvent& Event)
		{
			if (Ws.Queue.Push(Event))
			{
				return;
			}
			Ws.Queue.Compact([&Ws](const QueuedEvent& X) { return IsValid(Ws, X); });
			if (!Ws.Queue.Push(Event))
			{
				ShotEvent Lost; // cannot happen with <= 326 live entries (EventQueue.h); logged, never silent
				Lost.Time = Ws.Now;
				Lost.Type = ShotEventType::Diagnostic;
				Lost.A = static_cast<BallId>(Event.BallA);
				Lost.Value = -1.0;
				EmitEvent(Ws, Lost);
			}
		}

		double EarliestPending(const Workspace& Ws)
		{
			double Earliest = kInfinity;
			for (const TipSlot& Tip : Ws.Tips)
			{
				if (Tip.ContactBall != kNoBall && !Tip.InIsland)
				{
					Earliest = Min(Earliest, Tip.ContactEnd);
				}
			}
			for (const BallSlot& Slot : Ws.Balls)
			{
				if (!Slot.Observers.IsEmpty() && !Slot.InIsland) // island members: observers come from the island (8.7)
				{
					Earliest = Min(Earliest, Slot.Observers[0].Time);
				}
			}
			return Earliest;
		}

		void EmitPending(Workspace& Ws, double Time, bool Inclusive)
		{
			for (;;)
			{
				// Earliest pending item; ties: tip-contact ends (strike order) before observers (ball id, then kind and line).
				double Best = kInfinity;
				int BestTip = -1;
				int BestBall = -1;
				for (int s = 0; s < kMaxStrikes; ++s)
				{
					const TipSlot& Tip = Ws.Tips[s];
					if (Tip.ContactBall != kNoBall && !Tip.InIsland && Tip.ContactEnd < Best)
					{
						Best = Tip.ContactEnd;
						BestTip = s;
					}
				}
				for (int b = 0; b < kMaxBalls; ++b)
				{
					const BallSlot& Slot = Ws.Balls[b];
					if (!Slot.Observers.IsEmpty() && !Slot.InIsland && Slot.Observers[0].Time < Best)
					{
						Best = Slot.Observers[0].Time;
						BestTip = -1;
						BestBall = b;
					}
				}
				if ((BestTip < 0 && BestBall < 0) || (Inclusive ? Best > Time : Best >= Time))
				{
					return;
				}
				if (BestTip >= 0)
				{
					EmitTipContactEnd(Ws, BestTip);
				}
				else
				{
					EmitObserver(Ws, BestBall);
				}
			}
		}

		void ComputeObservers(Workspace& Ws, int Ball, double From, bool IncludeFrom)
		{
			BallSlot& Slot = Ws.Balls[Ball];
			Slot.Observers.Clear();
			if (!IsLive(Ws, Ball) || Slot.InIsland)
			{
				return;
			}
			const MotionSegment& Seg = Slot.Seg;
			if (Translates(Seg.State))
			{
				LineCrossing Crossings[2 * kTableLineCount];
				const int Count = PredictLineCrossings(Seg, Ws.Input->Table->Landmarks, From, SegmentLimit(Ws, Seg), Ws.Params.Numerics.LineCrossEps, IncludeFrom,
					Crossings, 2 * kTableLineCount);
				for (int i = 0; i < Count; ++i)
				{
					InsertObserver(Slot, {Crossings[i].Time, ObserverKind::LineCross, static_cast<std::uint8_t>(Crossings[i].Line), Crossings[i].Direction});
				}
				for (int Rail = 0; Rail < kRailFeatureCount; ++Rail)
				{
					if ((Slot.InitialFreezeRails & (1u << Rail)) != 0u)
					{
						const double T = RailLeaveTime(Ws, Ball, Rail, From);
						if (T < kInfinity)
						{
							InsertObserver(Slot, {T, ObserverKind::FreezeLeave, static_cast<std::uint8_t>(Rail), 0});
						}
					}
				}
			}
			for (int Partner = 0; Partner < kMaxBalls; ++Partner)
			{
				AddPairObservers(Ws, Ball, Partner, From);
			}
		}

		void RecomputePairObservers(Workspace& Ws, int Owner, int Partner, double From)
		{
			BallSlot& Slot = Ws.Balls[Owner];
			for (int i = Slot.Observers.Size() - 1; i >= 0; --i)
			{
				const Observer& O = Slot.Observers[i];
				const bool Jump = (O.Kind == ObserverKind::JumpEnter || O.Kind == ObserverKind::JumpLeave) && O.Index == Partner;
				const bool Freeze = O.Kind == ObserverKind::FreezeLeave && O.Index == 32 + Partner;
				if (Jump || Freeze)
				{
					Slot.Observers.RemoveAt(i);
				}
			}
			if (IsLive(Ws, Owner) && !Slot.InIsland)
			{
				AddPairObservers(Ws, Owner, Partner, From);
			}
		}

		void StopAllBalls(Workspace& Ws, double Time)
		{
			EmitPending(Ws, Time, true);
			for (int b = 0; b < kMaxBalls; ++b)
			{
				if (!IsLive(Ws, b))
				{
					continue;
				}
				BallSlot& Slot = Ws.Balls[b];
				BallState S;
				S.Position = CurrentPosition(Ws, b, Time);
				S.State = MotionState::Stationary; // stopped where it is (collisions 7.3 guard 4)
				const Quat Q = CloseOpenSegment(Ws, b, Time, S.Position);
				++Slot.Version;
				Slot.InIsland = false;
				Slot.Observers.Clear();
				Slot.Seg = MakeSegment(S, Time, SpecOf(Ws, b), SupportSurface(Ws, b), SupportHeight(Ws, b), Ws.Params.Gravity);
				Slot.Orientation0 = Q;
				if (Ws.Input->Record.Trajectories)
				{
					TrajectorySegment T = AsTrajectory(Slot.Seg);
					T.Orientation0 = Q;
					PushTrack(Ws, b, T);
				}
			}
			for (TipSlot& Tip : Ws.Tips)
			{
				Tip.Moving = false;
				Tip.InIsland = false;
				if (Tip.ContactBall != kNoBall && Tip.ContactEnd > Time)
				{
					Tip.ContactEnd = Time;
				}
				if (Tip.OpenCueTipSegment >= 0 && Tip.OpenCueTipSegment < static_cast<int>(Ws.Result->CueTips.size()))
				{
					CueTipSegment& Piece = Ws.Result->CueTips[static_cast<std::size_t>(Tip.OpenCueTipSegment)];
					Piece.T1 = Min(Piece.T1, Time);
				}
			}
			Ws.Island.Active = false;
			Ws.Now = Time;
		}
	}
}
