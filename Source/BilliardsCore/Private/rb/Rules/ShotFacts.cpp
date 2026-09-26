#include "rb/Core/FpGuard.h"
// Owner: WP-8 (rules facts & evaluation). Spec: rules.md 3.5, 3.6.
#include "rb/Rules/ShotFacts.h"

#include "rb/Math/Scalar.h"

#include <bit>

namespace rb::rules
{
	namespace
	{
		constexpr bool IsRulesBall(int Ball) { return Ball >= 0 && Ball < kRulesBallCount; }
		constexpr bool IsObjectBallId(int Ball) { return Ball >= 1 && Ball < kRulesBallCount; }
		constexpr std::uint32_t BallBit(int Ball) { return 1u << static_cast<unsigned>(Ball); }
		constexpr std::uint32_t kObjectBallMask = ((1u << kRulesBallCount) - 1u) & ~1u; // ids 1..15

		int CountBits(std::uint32_t Mask) { return std::popcount(Mask); }

		// "Driven to a rail" (R 2.1, 2.7; rules.md F2): a rail contact (cushion, jaw, rail top, pocket liner) that
		// does not merely continue a freeze from shot start, being pocketed (also supported over a pocket, and a cue
		// ball touching a pocketed ball, R 2.2), or leaving the table (incl. an outside object sending it back, R 2.6).
		bool IsDrivenToRailEvent(const RecordEvent& E)
		{
			switch (E.Type)
			{
			case RecordEventType::BallCushion:
			case RecordEventType::BallJaw: return !E.ContinuesInitialFreeze;
			case RecordEventType::BallRailTop:
			case RecordEventType::BallLiner:
			case RecordEventType::BallPocketed:
			case RecordEventType::SupportedOverPocket:
			case RecordEventType::BallOffTable: return true;
			case RecordEventType::BallTouchesPocketedBall: return E.A == kCueBallId;
			case RecordEventType::BallExternalContact: return E.Feature != static_cast<std::uint8_t>(ExternalObject::Template);
			default: return false;
			}
		}

		bool IsTerminalEvent(const RecordEvent& E)
		{
			return E.Type == RecordEventType::BallPocketed || E.Type == RecordEventType::SupportedOverPocket || E.Type == RecordEventType::BallOffTable
				|| (E.Type == RecordEventType::BallExternalContact && E.Feature != static_cast<std::uint8_t>(ExternalObject::Template));
		}

		PocketId PocketFromFeature(std::uint8_t Feature)
		{
			return Feature < kPocketCount ? static_cast<PocketId>(Feature) : PocketId::None;
		}

		// Pocketed list in time order (stable for equal times); a ball is listed once (its first pocketing).
		void AddPocketed(ShotFacts& Out, int Ball, PocketId Pocket, double Time)
		{
			if (Out.IsPocketed(Ball))
			{
				return;
			}
			PocketedBall Entry;
			Entry.Ball = static_cast<BallId>(Ball);
			Entry.Pocket = Pocket;
			Entry.Time = Time;
			if (!Out.Pocketed.PushBack(Entry))
			{
				return; // capacity = one entry per rules ball: cannot overflow for ids 0..15
			}
			for (int i = Out.Pocketed.Size() - 1; i > 0 && Out.Pocketed[i - 1].Time > Out.Pocketed[i].Time; --i)
			{
				const PocketedBall Tmp = Out.Pocketed[i - 1];
				Out.Pocketed[i - 1] = Out.Pocketed[i];
				Out.Pocketed[i] = Tmp;
			}
		}

		double RadiusOf(const ShotStartSnapshot& Start, const RulesTable& Table, int Ball)
		{
			return Start.Radius[Ball] > 0.0 ? Start.Radius[Ball] : Table.BallRadius[Ball];
		}

		// The cue ball's stroke (normal shot: the only stroke).
		StrokeInfo CueBallStroke(const StrokeRecord& Stroke)
		{
			for (const StrokeInfo& S : Stroke.Strokes)
			{
				if (S.Ball == kCueBallId)
				{
					return S;
				}
			}
			return Stroke.Strokes.IsEmpty() ? StrokeInfo{} : Stroke.Strokes[0];
		}

		const RecordEvent* FindTipEvent(const ShotRecord& Record, RecordEventType Type, int Ball, double Time)
		{
			for (const RecordEvent& E : Record.Events)
			{
				if (E.Type == Type && E.A == Ball && E.Time == Time)
				{
					return &E;
				}
			}
			return nullptr;
		}

		// F7 frozen-ball envelope (DERIVED from R 3.7 para 2): the tip contact happens while the cue ball is within
		// d_sep of a ball f it was (declared) frozen to at shot start and that the stroke goes into, and the cue ball
		// has touched no other ball or rail yet. Judged at both ends of the contact from the record's tip events
		// (B = f, Value = gap to f, OtherContactBefore).
		bool InsideFrozenEnvelope(const ShotRecord& Record, const TipContact& Contact, const RulesTolerances& Tol)
		{
			const RecordEvent* Begin = FindTipEvent(Record, RecordEventType::TipBallBegin, Contact.Ball, Contact.Start);
			const RecordEvent* End = FindTipEvent(Record, RecordEventType::TipBallEnd, Contact.Ball, Contact.End);
			if (Begin == nullptr || End == nullptr)
			{
				return false;
			}
			const RecordEvent* Ends[2] = {Begin, End};
			for (const RecordEvent* E : Ends)
			{
				if (!IsObjectBallId(E->B) || E->B != Begin->B)
				{
					return false;
				}
				if ((Record.Start.FrozenToCueBall & BallBit(E->B)) == 0u)
				{
					return false; // not declared frozen (R 4.11)
				}
				if (E->Value > Tol.FrozenEnvelope || E->OtherContactBefore)
				{
					return false;
				}
			}
			return true;
		}
	}

	void DeriveShotFacts(const ShotRecord& Record, const RulesTable& Table, const RulesTolerances& Tolerances, double ShotClockLimit, ShotFacts& Out)
	{
		Out = ShotFacts{};
		const ShotStartSnapshot& Start = Record.Start;
		const ShotEndSnapshot& End = Record.End;
		Out.StopTime = End.StopTime;
		Out.RecordTruncated = Record.Truncated;

		// 3.4 settle window: a ball that drops in [tStop, tStop + T_settle] is pocketed on this shot; later events
		// (settling) do not belong to the shot. StopTime <= 0 means "unknown" (no window applied).
		const double Cutoff = End.StopTime > 0.0 ? End.StopTime + Tolerances.SettleWindow : kInfinity;

		// ------------------------------------------------------------------------------------------
		// Pass 1 - F1: first cue-ball contact of every object ball, t1, tie set.
		// ------------------------------------------------------------------------------------------
		double FirstCueContact[kRulesBallCount];
		Vec2 FirstCueContactPosition[kRulesBallCount];
		for (int b = 0; b < kRulesBallCount; ++b)
		{
			FirstCueContact[b] = kInfinity;
		}
		for (const RecordEvent& E : Record.Events)
		{
			if (E.Time > Cutoff || E.Type != RecordEventType::BallBall)
			{
				continue;
			}
			int Other = -1;
			Vec2 OtherPosition;
			if (E.A == kCueBallId && IsObjectBallId(E.B))
			{
				Other = E.B;
				OtherPosition = E.PositionB;
			}
			else if (E.B == kCueBallId && IsObjectBallId(E.A))
			{
				Other = E.A;
				OtherPosition = E.PositionA;
			}
			if (Other > 0 && E.Time < FirstCueContact[Other])
			{
				FirstCueContact[Other] = E.Time;
				FirstCueContactPosition[Other] = OtherPosition;
			}
		}
		for (int b = 1; b < kRulesBallCount; ++b)
		{
			if (FirstCueContact[b] < Out.FirstContactTime)
			{
				Out.FirstContactTime = FirstCueContact[b];
				Out.EarliestContact = static_cast<BallId>(b);
			}
		}
		const double T1 = Out.FirstContactTime;
		if (Out.EarliestContact != kNoBall)
		{
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if (FirstCueContact[b] <= T1 + Tolerances.TieWindow)
				{
					Out.FirstContactTieSet.PushBack(static_cast<BallId>(b));
				}
				if (FirstCueContact[b] < kInfinity && AboveHeadString(FirstCueContactPosition[b], Table, Tolerances.Line))
				{
					Out.AboveHeadStringAtFirstCueBallContact |= BallBit(b);
				}
			}
		}
		// R 3.3 presumption for a legal first contact: cue-ball rail contacts in [t1 - eps_tie, t1) count as after it.
		const double T1Legal = T1 - Tolerances.TieWindow;

		// ------------------------------------------------------------------------------------------
		// Pass 2 - F2-F6, F9, F12, F13 from the event log.
		// ------------------------------------------------------------------------------------------
		Vec2 LastPosition[kRulesBallCount];
		bool LateTerminal[kRulesBallCount] = {};
		bool TerminalInWindow[kRulesBallCount] = {};
		std::uint32_t DrivenAny = 0;
		std::uint32_t OffTableMask = 0;
		std::uint32_t CrossedHeadMask = 0;
		std::uint32_t CrossedCenterMask = 0;
		for (int b = 0; b < kRulesBallCount; ++b)
		{
			LastPosition[b] = Start.Position[b];
		}

		auto MarkDriven = [&](int Ball, double Time) {
			if (IsObjectBallId(Ball))
			{
				DrivenAny |= BallBit(Ball);
			}
			if (Time >= T1)
			{
				Out.DrivenToRailAfterContactStrict |= BallBit(Ball);
			}
			if (Time >= T1 || (Ball == kCueBallId && Time >= T1Legal))
			{
				Out.DrivenToRailAfterContactLegal |= BallBit(Ball);
			}
		};

		auto AddContact = [&](int Ball, int Partner, double Time) {
			BallShotSummary& S = Out.Balls[Ball];
			BallContactEntry Entry;
			Entry.Partner = static_cast<BallId>(Partner);
			Entry.Time = Time;
			if (!S.BallContacts.PushBack(Entry))
			{
				S.BallContactsOverflow = true;
			}
		};

		for (const RecordEvent& E : Record.Events)
		{
			const int A = E.A;
			if (!IsRulesBall(A))
			{
				continue;
			}
			if (E.Time > Cutoff)
			{
				if (IsTerminalEvent(E))
				{
					LateTerminal[A] = true;
				}
				continue;
			}
			LastPosition[A] = E.PositionA;
			BallShotSummary& S = Out.Balls[A];

			switch (E.Type)
			{
			case RecordEventType::BallBall:
			{
				const int B = E.B;
				if (!IsRulesBall(B))
				{
					break;
				}
				LastPosition[B] = E.PositionB;
				AddContact(A, B, E.Time);
				AddContact(B, A, E.Time);
				if (A == kCueBallId && IsObjectBallId(B) && !AboveHeadString(E.PositionB, Table, Tolerances.Line))
				{
					Out.CueBallContactedBallOnOrBelowHeadString = true;
				}
				else if (B == kCueBallId && IsObjectBallId(A) && !AboveHeadString(E.PositionA, Table, Tolerances.Line))
				{
					Out.CueBallContactedBallOnOrBelowHeadString = true;
				}
				break;
			}
			case RecordEventType::BallCushion:
			case RecordEventType::BallJaw:
			case RecordEventType::BallRailTop:
			case RecordEventType::BallLiner:
			{
				RailContactEntry Entry;
				Entry.Kind = E.Type == RecordEventType::BallCushion ? RailContactKind::Cushion
					: (E.Type == RecordEventType::BallJaw ? RailContactKind::Jaw
						: (E.Type == RecordEventType::BallRailTop ? RailContactKind::RailTop : RailContactKind::Liner));
				Entry.Id = E.Feature;
				Entry.Side = E.Side;
				Entry.Time = E.Time;
				Entry.ContinuesInitialFreeze = E.ContinuesInitialFreeze;
				if (!S.RailContacts.PushBack(Entry))
				{
					S.RailContactsOverflow = true;
				}
				if (E.Time < T1)
				{
					++S.RailContactsBefore;
				}
				else if (!E.ContinuesInitialFreeze)
				{
					++S.RailContactsAfter;
				}
				break;
			}
			case RecordEventType::BallPocketed:
			case RecordEventType::SupportedOverPocket:
				TerminalInWindow[A] = true;
				AddPocketed(Out, A, PocketFromFeature(E.Feature), E.Time);
				break;
			case RecordEventType::BallTouchesPocketedBall:
				if (A == kCueBallId)
				{
					AddPocketed(Out, A, PocketFromFeature(E.Feature), E.Time); // R 2.2: counts as pocketed even if it comes back out
				}
				break;
			case RecordEventType::BallOffTable:
				TerminalInWindow[A] = true;
				OffTableMask |= BallBit(A);
				break;
			case RecordEventType::BallExternalContact:
				if (E.Feature == static_cast<std::uint8_t>(ExternalObject::Template))
				{
					Out.TemplateTouched = true;
				}
				else
				{
					TerminalInWindow[A] = true;
					OffTableMask |= BallBit(A); // R 2.6: an outside object sent it back
				}
				break;
			case RecordEventType::BallAirborne:
				if (A == kCueBallId)
				{
					Out.CueBallAirborne = true;
				}
				break;
			case RecordEventType::BallLineCross:
				if (E.Feature == static_cast<std::uint8_t>(TableLine::HeadString))
				{
					// Direction-aware (pitfall 20): the cue ball crosses toward the foot, object balls toward the head.
					if (A == kCueBallId && E.Side > 0)
					{
						Out.CueBallCrossedHeadString = true;
						S.CrossedHeadString = true;
						if (E.Time < T1)
						{
							Out.CueBallCrossedHeadStringBeforeFirstContact = true;
						}
					}
					else if (A != kCueBallId && E.Side < 0)
					{
						S.CrossedHeadString = true;
						CrossedHeadMask |= BallBit(A);
					}
				}
				else if (E.Feature == static_cast<std::uint8_t>(TableLine::CenterString))
				{
					S.CrossedCenterString = true;
					CrossedCenterMask |= BallBit(A);
				}
				else if (E.Feature == static_cast<std::uint8_t>(TableLine::LongString))
				{
					S.CrossedLongString = true;
				}
				break;
			case RecordEventType::BallJumpedOver:
				if (A == kCueBallId && IsObjectBallId(E.B))
				{
					Out.JumpedOver |= BallBit(E.B);
				}
				break;
			default:
				break;
			}

			if (IsDrivenToRailEvent(E))
			{
				MarkDriven(A, E.Time);
			}
		}

		// End snapshot: supported balls (3.4), and terminal statuses the event log does not carry (defensive). Only a
		// ball that was on the table at shot start can be pocketed / leave the table on THIS shot: a ball pocketed on an
		// earlier shot may still be reported as Pocketed at the end (it lies in the pocket, pitfall 6). The cue ball is
		// always in play (in hand = placed on the table).
		auto InPlayAtStart = [&Start](int Ball) { return Ball == kCueBallId || Start.Presence[Ball] == BallPresence::OnTable; };
		for (const SupportedBall& Sup : End.Supported)
		{
			if (IsRulesBall(Sup.Ball) && InPlayAtStart(Sup.Ball) && !Out.IsPocketed(Sup.Ball))
			{
				AddPocketed(Out, Sup.Ball, Sup.Pocket, End.StopTime);
				MarkDriven(Sup.Ball, End.StopTime);
			}
		}
		for (int b = 0; b < kRulesBallCount; ++b)
		{
			if (TerminalInWindow[b] || LateTerminal[b] || !InPlayAtStart(b))
			{
				continue;
			}
			if (End.Balls[b].Status == BallEndStatus::Pocketed && !Out.IsPocketed(b))
			{
				AddPocketed(Out, b, End.Balls[b].Pocket, End.StopTime);
				MarkDriven(b, End.StopTime);
			}
			else if (End.Balls[b].Status == BallEndStatus::OffTable && (OffTableMask & BallBit(b)) == 0u)
			{
				OffTableMask |= BallBit(b);
				MarkDriven(b, End.StopTime);
			}
		}

		// F5 wins over F4: a ball that left the table (e.g. off the lamp) is off the table even if it then dropped.
		for (int i = Out.Pocketed.Size() - 1; i >= 0; --i)
		{
			if ((OffTableMask & BallBit(Out.Pocketed[i].Ball)) != 0u)
			{
				Out.Pocketed.RemoveAt(i);
			}
		}

		// F4, F5
		std::uint32_t PocketedMask = 0;
		for (const PocketedBall& P : Out.Pocketed)
		{
			PocketedMask |= BallBit(P.Ball);
			BallShotSummary& S = Out.Balls[P.Ball];
			S.Pocketed = true;
			S.Pocket = P.Pocket;
			S.PocketedTime = P.Time;
		}
		Out.CueBallPocketed = (PocketedMask & 1u) != 0u;
		Out.AnyObjectBallPocketed = (PocketedMask & kObjectBallMask) != 0u;
		Out.CueBallOffTable = (OffTableMask & 1u) != 0u;
		Out.ObjectBallsOffTable = OffTableMask & kObjectBallMask;

		// F3
		Out.NumObjectBallsDrivenToRail = CountBits(DrivenAny & kObjectBallMask);
		Out.CueBallContactedRack = Out.EarliestContact != kNoBall;
		Out.CueBallDrivenToRailAfterRackContact = Out.CueBallContactedRack && (Out.DrivenToRailAfterContactLegal & 1u) != 0u;
		Out.NumObjectBallsDrivenToRailAfterRackContact = CountBits(Out.DrivenToRailAfterContactStrict & kObjectBallMask);
		const std::uint32_t PocketedObjectBalls = PocketedMask & kObjectBallMask;
		Out.CountPocketedOrCrossedHeadString = CountBits(PocketedObjectBalls | (CrossedHeadMask & kObjectBallMask));
		Out.NumObjectBallsCrossedCenterString = CountBits(CrossedCenterMask & kObjectBallMask);

		// F13 per-ball end state (a ball that dropped after the settle window is restored where it stood, R 1.8).
		std::uint32_t ReachedHeadMask = 0;
		for (int b = 0; b < kRulesBallCount; ++b)
		{
			BallShotSummary& S = Out.Balls[b];
			S.Radius = RadiusOf(Start, Table, b);
			S.OffTable = (OffTableMask & BallBit(b)) != 0u;
			const BallEnd& EndBall = End.Balls[b];
			if (S.Pocketed)
			{
				S.EndStatus = BallEndStatus::Pocketed;
				S.FinalPosition = LastPosition[b];
			}
			else if (S.OffTable)
			{
				S.EndStatus = BallEndStatus::OffTable;
				S.FinalPosition = LastPosition[b];
			}
			else if (LateTerminal[b])
			{
				S.EndStatus = BallEndStatus::OnTable;
				S.FinalPosition = LastPosition[b];
			}
			else if (EndBall.Status == BallEndStatus::OnTable)
			{
				S.EndStatus = BallEndStatus::OnTable;
				S.FinalPosition = EndBall.Position;
				S.FrozenToRail = EndBall.FrozenToRail;
				S.FrozenToBalls = EndBall.FrozenToBalls;
			}
			else
			{
				S.EndStatus = Start.Presence[b] == BallPresence::OnTable ? BallEndStatus::OnTable : EndBall.Status;
				S.FinalPosition = LastPosition[b];
			}
			if (IsObjectBallId(b) && S.EndStatus == BallEndStatus::OnTable && Start.Presence[b] == BallPresence::OnTable
				&& S.FinalPosition.x <= Table.HeadStringX + Tolerances.Line)
			{
				ReachedHeadMask |= BallBit(b); // LEGACY "reached the head string" (at rest on or above it)
			}
		}
		Out.CountPocketedOrReachedHeadString = CountBits(PocketedObjectBalls | ((CrossedHeadMask | ReachedHeadMask) & kObjectBallMask));

		// F7 double hit, F8 push shot (cue-ball tip contacts; the frozen-ball envelope exempts re-contacts / long contacts).
		{
			TipContact CueContacts[kMaxTipContacts];
			int CueContactCount = 0;
			for (const TipContact& C : Record.Stroke.TipContacts)
			{
				if (C.Ball != kCueBallId)
				{
					continue; // tip contacts on other balls are NonTipContact{CueTip} (F10)
				}
				int i = CueContactCount++;
				CueContacts[i] = C;
				for (; i > 0 && CueContacts[i - 1].Start > CueContacts[i].Start; --i)
				{
					const TipContact Tmp = CueContacts[i - 1];
					CueContacts[i - 1] = CueContacts[i];
					CueContacts[i] = Tmp;
				}
			}
			for (int i = 0; i < CueContactCount; ++i)
			{
				const bool Inside = InsideFrozenEnvelope(Record, CueContacts[i], Tolerances);
				if (i >= 1 && !Inside)
				{
					Out.DoubleHit = true; // F7 (a): the cue touched the cue ball again
				}
				if (!Inside && CueContacts[i].End - CueContacts[i].Start > Tolerances.PushDuration)
				{
					Out.PushShot = true; // F8
				}
			}
			for (const RecordEvent& E : Record.Events)
			{
				if (E.Time > Cutoff || E.Type != RecordEventType::BallBall || E.A != kCueBallId || !IsObjectBallId(E.B))
				{
					continue;
				}
				if ((Start.FrozenToCueBall & BallBit(E.B)) != 0u || E.CutAngle >= Tolerances.GrazeAngle)
				{
					continue; // frozen at start (R 3.7 para 2) or a barely grazing hit (para 1)
				}
				for (int i = 0; i < CueContactCount; ++i)
				{
					if (CueContacts[i].Start <= E.Time && E.Time <= CueContacts[i].End)
					{
						Out.DoubleHit = true; // F7 (b): the cue ball hit a ball while the tip was still on it
					}
				}
			}
		}

		// F9, F10
		const StrokeInfo Stroke = CueBallStroke(Record.Stroke);
		Out.Miscue = Stroke.Miscue;
		Out.Scoop = Stroke.TipClothContact && Out.CueBallAirborne;
		for (const NonTipContact& C : Record.Stroke.NonTipContacts)
		{
			// R 3.6 / 1.6: touching the cue ball is no foul while it is in hand, i.e. before the stroke (t < 0) of a
			// shot that started with the cue ball in hand (placing and adjusting it by hand or cue).
			if (C.Ball == kCueBallId && Start.InHand != CueBallInHand::No && C.Time < 0.0)
			{
				continue;
			}
			Out.NonTipBallContact = true;
			if (C.Ball == kCueBallId)
			{
				Out.NonTipCueBallContact = true;
			}
		}

		// F11 cue-ball placement (R 1.6, 3.10) with per-ball radii.
		if (Start.InHand != CueBallInHand::No)
		{
			const Vec2 P = Start.PlacedPosition;
			const double Rc = RadiusOf(Start, Table, kCueBallId);
			bool Legal = Abs(P.x) <= 0.5 * Table.Length - Rc && Abs(P.y) <= 0.5 * Table.Width - Rc && !Start.PlacementOverPocket;
			for (int b = 1; b < kMaxBalls && Legal; ++b)
			{
				if (Start.Presence[b] != BallPresence::OnTable)
				{
					continue;
				}
				const double Rj = b < kRulesBallCount ? RadiusOf(Start, Table, b) : Start.Radius[b];
				if (Length(Start.Position[b] - P) < Rc + Rj - Tolerances.PlacementOverlap)
				{
					Legal = false;
				}
			}
			if (Start.InHand == CueBallInHand::AboveHeadString && !AboveHeadString(P, Table, Tolerances.Line))
			{
				Legal = false;
			}
			if (Start.InHand == CueBallInHand::Baulk && !InBaulk(P, Table, Tolerances.Line))
			{
				Legal = false;
			}
			Out.CueBallPlacementLegal = Legal;
		}

		// F12
		Out.BallsMovingAtStart = !Start.AllBallsAtRest;
		Out.FootOnFloor = Start.FootOnFloor;
		Out.ShotClockExpired = Start.ShotClockElapsed > ShotClockLimit;
	}

	int ResolveFirstContact(const ShotFacts& Facts, std::uint32_t LegalMask)
	{
		// F1 tie rule (R 3.2): a legal ball in the tie set is the first contact (lowest id, INTERPRETATION).
		for (const BallId B : Facts.FirstContactTieSet)
		{
			if ((LegalMask & BallBit(B)) != 0u)
			{
				return B;
			}
		}
		return Facts.EarliestContact;
	}
}
