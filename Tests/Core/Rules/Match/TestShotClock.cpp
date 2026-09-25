// Owner: WP-9 (rules table procedures & match). Shot clock (rules.md 4.10, Reg 18); tests C01-C03.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Rules/Evaluate.h"
#include "rb/Rules/ShotFacts.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

namespace
{
	const ShotFacts kNoFacts{};

	MatchConfig ClockConfig(Discipline Game)
	{
		MatchConfig C = MakeMatchConfig(Game);
		C.Clock.Enabled = true;
		C.Clock.ShotTime = 35.0;
		C.Clock.ExtensionTime = 25.0;
		C.Clock.ExtensionsPerRack = 1;
		return C;
	}

	// A mid-rack shot (not the first shot after the break) of Shooter.
	bool ToMidRackShot(const MatchConfig& C, MatchState& S, int Shooter)
	{
		if (!StartToFirstBreak(C, S, 0)) return false;
		if (ApplyShot(C, S, ContinueOutcome(0), kNoFacts) != ErrorCode::Ok) return false; // the break
		const ShotOutcome Second = Shooter == 0 ? ContinueOutcome(0) : PassOutcome(1);
		return ApplyShot(C, S, Second, kNoFacts) == ErrorCode::Ok;
	}
}

RB_TEST(Rules_C01_ShotClockExpiryIsDetected)
{
	const MatchConfig C = ClockConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(ToMidRackShot(C, S, 0));
	RB_CHECK(!S.ShotAfterBreak);
	RB_CHECK(ShotClockAllowed(C, S) == 35.0);
	RB_CHECK(GetShotConstraints(C, S).ShotClockAllowed == 35.0);
	RB_CHECK(ShotClockExpired(C, S, 35.2)); // tip contact at 35.2 s -> foul 3.14
	RB_CHECK(!ShotClockExpired(C, S, 35.0));
	RB_CHECK(!ShotClockExpired(C, S, 20.0));

	// clock off: never expires, allowed time 0
	MatchConfig Off = C;
	Off.Clock.Enabled = false;
	RB_CHECK(ShotClockAllowed(Off, S) == 0.0);
	RB_CHECK(!ShotClockExpired(Off, S, 1000.0));
	RB_CHECK(RequestShotClockExtension(Off, S) == ErrorCode::InvalidOption);
}

RB_TEST(Rules_C01_ShotAfterBreakAllowance)
{
	// INTERPRETATION of Reg 18: the first shot after a break may get up to AfterBreakMax, never > 60 s.
	MatchConfig C = ClockConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_CHECK(ShotClockAllowed(C, S) == 35.0); // the break itself
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.ShotAfterBreak);
	RB_CHECK(ShotClockAllowed(C, S) == 60.0);
	C.Clock.AfterBreakMax = 90.0;
	RB_CHECK(ShotClockAllowed(C, S) == 60.0);
	C.Clock.AfterBreakMax = 35.0; // organizer gives no extra time
	RB_CHECK(ShotClockAllowed(C, S) == 35.0);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(!S.ShotAfterBreak);
}

RB_TEST(Integ_Rules_C01_ShotClockFoulCountsTowardThreeFouls)
{
	// Needs WP-8 DeriveShotFacts (F12) and EvaluateShot (standard foul, R 3.13).
	const MatchConfig C = ClockConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(ToMidRackShot(C, S, 0));

	ShotRecord Record;
	Record.Start.ShotClockElapsed = 35.2;
	ShotFacts Derived;
	DeriveShotFacts(Record, C.Table, C.Rules.Tolerances, ShotClockAllowed(C, S), Derived);
	RB_CHECK(Derived.ShotClockExpired);

	// Otherwise legal shot: lowest ball 1 hit first, driven to a rail.
	ShotFacts F;
	F.EarliestContact = 1;
	F.FirstContactTime = 0.2;
	F.FirstContactTieSet.PushBack(1);
	F.DrivenToRailAfterContactStrict = 1u << 1;
	F.DrivenToRailAfterContactLegal = 1u << 1;
	F.NumObjectBallsDrivenToRail = 1;
	F.ShotClockExpired = true;
	const ShotDeclaration Normal;
	const ShotOutcome First = EvaluateShot(C.Rules, C.Table, S.Game, Normal, F);
	RB_CHECK(First.Detected.Has(Foul::SlowPlay));
	RB_CHECK(First.Next == NextAction::Pass);
	RB_CHECK(First.NextCueBall == CueBallNext::InHandAnywhere);
	RB_CHECK(First.FoulsAfter[0] == 1);

	GameState OnTwo = S.Game;
	OnTwo.Players[0].ConsecutiveFouls = 2;
	const ShotOutcome Third = EvaluateShot(C.Rules, C.Table, OnTwo, Normal, F);
	RB_CHECK(Third.Next == NextAction::RackWon);
	RB_CHECK(Third.Winner == 1);
}

RB_TEST(Rules_C02_OneExtensionPerRack)
{
	const MatchConfig C = ClockConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(ToMidRackShot(C, S, 0));
	RB_REQUIRE(RequestShotClockExtension(C, S) == ErrorCode::Ok);
	RB_CHECK(ShotClockAllowed(C, S) == 60.0);
	RB_CHECK(!ShotClockExpired(C, S, 50.0)); // tip contact at 50 s: no foul
	RB_CHECK(ShotClockExpired(C, S, 60.5));
	RB_CHECK(RequestShotClockExtension(C, S) == ErrorCode::InvalidOption); // second extension refused

	RB_REQUIRE(ApplyShot(C, S, ContinueOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(ShotClockAllowed(C, S) == 35.0); // extension only for the shot it was requested for
	RB_CHECK(RequestShotClockExtension(C, S) == ErrorCode::InvalidOption); // still the same rack
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(RequestShotClockExtension(C, S) == ErrorCode::Ok); // the opponent has his own

	// A new rack gives every player a new extension.
	RB_REQUIRE(ApplyShot(C, S, RackWonOutcome(1), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 9)) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok); // B's break, A to shoot
	RB_REQUIRE(ApplyShot(C, S, ContinueOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 0);
	RB_CHECK(RequestShotClockExtension(C, S) == ErrorCode::Ok);
}

RB_TEST(Rules_C03_ClockStartsWhenSpinningStops)
{
	// Previous shot: the last ball stops rolling at 4.0 s but keeps spinning until 4.6 s.
	ShotRecord Previous;
	RecordEvent Roll;
	Roll.Type = RecordEventType::MotionTransition;
	Roll.A = 3;
	Roll.Time = 4.0;
	Roll.From = MotionState::Rolling;
	Roll.To = MotionState::Spinning;
	RecordEvent Stop = Roll;
	Stop.Time = 4.6;
	Stop.Sequence = 1;
	Stop.From = MotionState::Spinning;
	Stop.To = MotionState::Stationary;
	Previous.Events.push_back(Roll);
	Previous.Events.push_back(Stop);
	Previous.End.StopTime = 4.6; // tStop includes spin (rules.md 3.4)
	const double Start = ShotClockStartTime(Previous.End);
	RB_CHECK(Start == 4.6);

	const MatchConfig C = ClockConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(ToMidRackShot(C, S, 0));
	// Tip contact 39.5 s after the previous stroke: 35.5 s after rolling stopped, but only 34.9 s after
	// the spin stopped -> no foul.
	RB_CHECK(!ShotClockExpired(C, S, 39.5 - Start));
	RB_CHECK(ShotClockExpired(C, S, 39.5 - 4.0));
}
