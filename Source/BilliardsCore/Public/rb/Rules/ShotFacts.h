#pragma once

// Derived per-shot facts F1-F13 (rules.md 3.5): a pure function of the ShotRecord (+ landmarks and
// tolerances). The evaluators read ONLY these facts and the GameState at shot start.
// Owner: WP-8 (rules facts & evaluation).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Vec2.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>

namespace rb::rules
{
	struct PocketedBall
	{
		BallId Ball = kNoBall;
		PocketId Pocket = PocketId::None;
		double Time = 0.0;
	};

	struct BallContactEntry
	{
		BallId Partner = kNoBall;
		double Time = 0.0;
	};

	enum class RailContactKind : std::uint8_t
	{
		Cushion, // Id = CushionId
		Jaw,     // Id = PocketId, Side = JawSide
		RailTop, // Id = CushionId (0xFF for a pocket surround)
		Liner,   // Id = PocketId (liner / back wall / rim from inside)
	};

	// One rail contact of a ball (F13 ordered list; used by the ObviousAssist call inference of 4.5:
	// "no cushion other than the jaws of p").
	struct RailContactEntry
	{
		RailContactKind Kind = RailContactKind::Cushion;
		std::uint8_t Id = 0xFF;
		std::int8_t Side = 0;
		double Time = 0.0;
		bool ContinuesInitialFreeze = false;
	};

	inline constexpr int kMaxBallContactEntries = 32;
	inline constexpr int kMaxRailContactEntries = 32;

	// F13 per-ball summary.
	struct BallShotSummary
	{
		FixedVector<BallContactEntry, kMaxBallContactEntries> BallContacts; // ordered ball-ball contacts
		bool BallContactsOverflow = false;   // more contacts than capacity (the first ones are kept)
		FixedVector<RailContactEntry, kMaxRailContactEntries> RailContacts; // ordered cushion/jaw/rail-top/liner contacts
		bool RailContactsOverflow = false;
		int RailContactsBefore = 0;   // cushion/jaw/rail-top/liner contacts before t1 (all, also beyond capacity)
		int RailContactsAfter = 0;    // ... at or after t1 (continuesInitialFreeze contacts excluded)
		bool Pocketed = false;
		PocketId Pocket = PocketId::None;
		double PocketedTime = 0.0;
		bool OffTable = false;
		bool CrossedHeadString = false;   // center passed the head string (direction-aware, see F6/F3)
		bool CrossedCenterString = false;
		bool CrossedLongString = false;   // lag (4.1 bad lag a)
		BallEndStatus EndStatus = BallEndStatus::NotUsed;
		Vec2 FinalPosition;
		double Radius = 0.0;          // R_b from the start snapshot [m]
		std::uint32_t FrozenToRail = 0;
		std::uint32_t FrozenToBalls = 0;
	};

	struct ShotFacts
	{
		// F1 first contact and tie set (tie resolution needs the legality predicate: ResolveFirstContact)
		BallId EarliestContact = kNoBall;     // earliest CB-OB contact (lowest id on exact ties)
		double FirstContactTime = kInfinity;  // t1
		FixedVector<BallId, kRulesBallCount> FirstContactTieSet; // OBs first touched by the CB within t1 + eps_tie, ascending ids

		// F2 driven to a rail after first contact (bit per ball incl. the CB), two presumption variants
		std::uint32_t DrivenToRailAfterContactStrict = 0; // events at t >= t1
		std::uint32_t DrivenToRailAfterContactLegal = 0;  // CB rail contacts in [t1 - eps_tie, t1) count as after (R 3.3)
		constexpr bool AnyBallDrivenToRailAfterFirstContact(bool FirstContactWasLegal) const
		{
			return (FirstContactWasLegal ? DrivenToRailAfterContactLegal : DrivenToRailAfterContactStrict) != 0u;
		}

		// F3 break counters
		int NumObjectBallsDrivenToRail = 0;
		bool CueBallContactedRack = false;               // 14.1 opening break: CB touched any OB
		bool CueBallDrivenToRailAfterRackContact = false;
		int NumObjectBallsDrivenToRailAfterRackContact = 0;
		int CountPocketedOrCrossedHeadString = 0;        // 9-ball three-ball rule (Reg 16), each ball once
		int CountPocketedOrReachedHeadString = 0;        // LEGACY variant (ThreeBallRuleReach)
		int NumObjectBallsCrossedCenterString = 0;       // Blackball break

		// F4 pocketing (after the settle window)
		FixedVector<PocketedBall, kRulesBallCount> Pocketed; // in time order
		bool CueBallPocketed = false;   // incl. BallTouchesPocketedBall and a supported CB
		bool AnyObjectBallPocketed = false;
		constexpr bool IsPocketed(int Ball) const
		{
			for (const PocketedBall& P : Pocketed)
			{
				if (P.Ball == Ball) return true;
			}
			return false;
		}
		constexpr PocketId PocketOf(int Ball) const
		{
			for (const PocketedBall& P : Pocketed)
			{
				if (P.Ball == Ball) return P.Pocket;
			}
			return PocketId::None;
		}

		// F5 off table (R 2.6)
		bool CueBallOffTable = false;
		std::uint32_t ObjectBallsOffTable = 0; // bit per ball
		constexpr bool IsOffTable(int Ball) const { return Ball == kCueBallId ? CueBallOffTable : ((ObjectBallsOffTable >> Ball) & 1u) != 0u; }

		// F6 head-string facts
		bool CueBallCrossedHeadString = false;
		bool CueBallCrossedHeadStringBeforeFirstContact = false;
		bool CueBallContactedBallOnOrBelowHeadString = false;
		std::uint32_t AboveHeadStringAtFirstCueBallContact = 0; // bit per OB: position above HS when the CB first touched it
		                                                        // (FirstContactBallAboveHeadString = bit of ResolveFirstContact)

		// F7-F10 stroke facts (of the cue ball's stroke; lag facts per ball: rb::rules::DeriveLagBallFacts)
		bool DoubleHit = false;
		bool PushShot = false;
		bool CueBallAirborne = false;
		bool Scoop = false;
		std::uint32_t JumpedOver = 0;       // bit per OB (F9 approximation)
		bool NonTipBallContact = false;     // any ball touched by equipment (all-ball fouls)
		bool NonTipCueBallContact = false;  // the CB touched by equipment (CueBallOnly scope)
		bool Miscue = false;

		// F11, F12
		bool CueBallPlacementLegal = true;
		bool BallsMovingAtStart = false;
		bool FootOnFloor = true;
		bool TemplateTouched = false;
		bool ShotClockExpired = false;

		// F13
		BallShotSummary Balls[kRulesBallCount];

		double StopTime = 0.0;
		bool RecordTruncated = false; // simulation aborted / record overflow -> replay the shot
	};

	// One pass over the record (Record.Events sorted by (Time, Sequence)). ShotClockLimit [s]: allowed time for this
	// shot (kInfinity = no clock); F12 ShotClockExpired = Start.ShotClockElapsed > ShotClockLimit.
	//  - 3.4 settle window: events later than End.StopTime + Tolerances.SettleWindow are post-shot settling and are
	//    ignored; a ball that dropped then is OnTable at its last in-window position (R 1.8). StopTime <= 0: no window.
	//  - "Driven to a rail" (F2/F3): BallCushion / BallJaw without ContinuesInitialFreeze, BallRailTop, BallLiner,
	//    being pocketed (BallPocketed, SupportedOverPocket, a cue ball's BallTouchesPocketedBall) or off the table.
	//  - F5 wins over F4: a ball that left the table (BallOffTable, or BallExternalContact other than Template) is
	//    not in Pocketed even if it dropped afterwards. End-snapshot statuses fill in balls the log does not cover,
	//    but only for balls on the table at shot start (a ball pocketed earlier may still be reported Pocketed).
	//  - F10: contacts with the cue ball while it is in hand (Start.InHand != No, Time < 0: placing / adjusting it)
	//    are not touched-ball fouls (R 3.6 / 1.6); every other NonTipContact is.
	//  - F7/F8 judge the cue ball's TipContacts; the frozen-ball envelope of an interval is read from the
	//    TipBallBegin / TipBallEnd events at exactly its Start / End (B = f declared in Start.FrozenToCueBall,
	//    Value <= FrozenEnvelope, !OtherContactBefore at both ends).
	//  - Line crossings are direction-aware: the cue ball crosses the head string toward the foot (+1), object balls
	//    toward the head (-1); center / long string crossings count in either direction.
	RB_API void DeriveShotFacts(const ShotRecord& Record, const RulesTable& Table, const RulesTolerances& Tolerances, double ShotClockLimit, ShotFacts& Out);

	// F1 tie rule: if any ball of the tie set is legal (bit in LegalMask), the first contact is that
	// legal ball (lowest id); otherwise EarliestContact; kNoBall if the CB touched nothing.
	RB_API int ResolveFirstContact(const ShotFacts& Facts, std::uint32_t LegalMask);
}
