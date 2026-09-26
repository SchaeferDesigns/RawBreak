// Owner: WP-9 (rules table procedures & match). Adversarial match-level cases from the WP-9 review:
// rule interactions fed with hand-built ShotOutcomes shaped exactly like the rules.md 10.x pseudocode
// results (the evaluator itself is WP-8's): three-foul penalty re-rack, fouls + money ball + scratch,
// break fouls with spotting next to the cue ball, three-foul warnings across innings, push-out window
// edges, 14.1 breaking foul on two fouls (S25 flow), stalemate while a decision is pending, concession.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Math/Scalar.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

namespace
{
	const ShotFacts kNoFacts{};

	// Moves every object ball of the current rack off the long string (|y| = 0.4) so that spotting
	// geometry depends only on the balls a test places itself.
	void ClearLongString(GameState& G)
	{
		for (int b = 1; b < kRulesBallCount; ++b)
		{
			if (G.Balls[b].Kind == BallStatusKind::OnTable)
			{
				G.Balls[b].Position = {-1.0 + 0.1 * b, (b % 2 == 0) ? 0.4 : -0.4};
			}
		}
	}
}

RB_TEST(Rules_Match_S10_ThreeFoulPenaltyRerackLeftToSetupRack)
{
	// rules.md 10.6 returns Rack = Rerack15 TOGETHER with RerackAndBreak for the third foul. The new
	// 15-ball rack is SetupRack's job: ApplyShot must not also rack a continuation rack (that consumed a
	// rack seed for a rack that is thrown away, and failed whenever a continuation rack cannot be built).
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1), kNoFacts) == ErrorCode::Ok); // opening break, B to shoot
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok); // B misses
	S.Game.Players[0].Score = 20;
	S.Game.Players[0].ConsecutiveFouls = 2;
	S.Game.Players[1].ConsecutiveFouls = 1;
	const std::uint64_t CounterBefore = S.RackCounter;

	ShotOutcome O;
	O.Next = NextAction::RerackAndBreak;
	O.NextShooter = 0;
	O.AnyFoul = true;
	O.Detected.Add(Foul::NoRailAfterContact);
	O.Detected.Add(Foul::ThreeConsecutiveFouls);
	O.Enforced = Foul::ThreeConsecutiveFouls;
	O.ScoreDelta[0] = -16;
	O.FoulsAfter[0] = 0;
	O.Rack.Kind = RackCommandKind::Rerack15;
	O.NextCueBall = CueBallNext::InHandAboveHeadString;
	O.BallsToSpot.PushBack(7);
	ShotFacts F;
	FactsPocketed(F, 7, PocketId::SideLeft);
	FactsRestAt(F, kCueBallId, 0.2, 0.1);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::RackSetup);
	RB_CHECK(S.Game.RackBreaker == 0 && S.Game.Shooter == 0); // the offender takes the opening break
	RB_CHECK(S.Game.Players[0].Score == 4);                    // S10: 20 - 16
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 0);
	RB_CHECK(S.Game.Players[1].ConsecutiveFouls == 1);         // the opponent's counter is untouched
	RB_CHECK(S.RackCounter == CounterBefore);                  // no continuation rack was generated

	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 15)) == ErrorCode::Ok);
	RB_CHECK(S.Game.IsBreakShot && S.Game.Shooter == 0);
	RB_CHECK(S.Game.CueBall == CueBallNext::InHandAboveHeadString);
	RB_CHECK(S.Game.Players[0].Score == 4);
	RB_CHECK(S.Game.Players[1].ConsecutiveFouls == 1); // 14.1 keeps the counters across racks
	RB_CHECK(!GetShotConstraints(C, S).ThreeFoulWarning);
}

RB_TEST(Rules_Match_RackWonIgnoresStaleRackCommand)
{
	// A rack command on an outcome that ends the rack (or the match) must not be executed either.
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	ShotOutcome Win = ContinueOutcome(0);
	Win.Next = NextAction::MatchWon;
	Win.Winner = 0;
	Win.ScoreDelta[0] = 100;
	Win.Rack.Kind = RackCommandKind::Rerack14;
	RB_REQUIRE(ApplyShot(C, S, Win, kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::MatchOver && S.Winner == 0);
	RB_CHECK(S.RackCounter == 0);
}

RB_TEST(Rules_Match_ApplyShotErrorLeavesStateUnchanged)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	ShotOutcome Bad = RackWonOutcome(-1); // no winner
	Bad.ScoreDelta[0] = 5;
	Bad.FoulsAfter[0] = 2;
	Bad.BallsToSpot.PushBack(9);
	ShotFacts F;
	FactsPocketed(F, 9, PocketId::FootLeft);
	const MatchState Before = S;
	RB_CHECK(ApplyShot(C, S, Bad, F) == ErrorCode::InvalidArgument);
	RB_CHECK(S.Phase == Before.Phase);
	RB_CHECK(S.Game.Players[0].Score == 0 && S.Game.Players[0].ConsecutiveFouls == 0);
	RB_CHECK(S.Game.Balls[9].Kind == BallStatusKind::OnTable);
	RB_CHECK(S.Game.Balls[9].Position == Before.Game.Balls[9].Position);
	RB_CHECK(S.Game.IsBreakShot);
}

RB_TEST(Rules_Match_ConcedeAfterMatchOverKeepsResult)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall, 1);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, RackWonOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(S.Phase == MatchPhase::MatchOver && S.Winner == 0);
	Concede(S, 0); // the winner "concedes" after the end: no effect
	RB_CHECK(S.Phase == MatchPhase::MatchOver && S.Winner == 0);

	MatchState T;
	RB_REQUIRE(StartToFirstBreak(C, T, 0));
	Concede(T, 7); // not a player: ignored
	RB_CHECK(T.Phase == MatchPhase::AwaitShot);
	Concede(T, 1);
	RB_CHECK(T.Phase == MatchPhase::MatchOver && T.Winner == 0);
}

RB_TEST(Rules_Match_NineBallFoulWithNineAndScratchSpotsNineBehindFootSpotBall)
{
	// N13-like shape at the match level: B fouls, the 9 drops and the cue ball scratches. The 9 is
	// spotted (4.3) - touching ball 5 that sits on the foot spot - the scratched cue ball does not block,
	// A gets ball in hand anywhere and no push-out, B's counter goes to 1, A's stays.
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	ShotOutcome Break = PassOutcome(1);
	Break.NextPushOutAvailable = true;
	RB_REQUIRE(ApplyShot(C, S, Break, kNoFacts) == ErrorCode::Ok);
	GameState& G = S.Game;
	ClearLongString(G);
	Place(G, 5, 0.635, 0.0);
	Place(G, 9, 0.2, -0.3);
	Place(G, 0, 0.1, 0.0);
	G.Players[0].ConsecutiveFouls = 1;

	ShotOutcome O = StandardFoul(1, 1, Foul::CueBallScratch);
	O.BallsToSpot.PushBack(9);
	ShotFacts F;
	FactsPocketed(F, 9, PocketId::FootRight);
	FactsPocketed(F, kCueBallId, PocketId::FootLeft);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	RB_CHECK(G.Balls[9].Kind == BallStatusKind::OnTable);
	RB_CHECK_NEAR(G.Balls[9].Position.x, 0.692150, 1e-9);
	RB_CHECK_NEAR(G.Balls[9].Position.y, 0.0, 1e-12);
	RB_CHECK(G.Balls[0].Kind == BallStatusKind::Pocketed);
	RB_CHECK(G.Shooter == 0 && G.CueBall == CueBallNext::InHandAnywhere);
	RB_CHECK(!G.PushOutAvailable);
	RB_CHECK(G.Players[1].ConsecutiveFouls == 1);
	RB_CHECK(G.Players[0].ConsecutiveFouls == 1);
	const ShotConstraints K = GetShotConstraints(C, S);
	RB_CHECK(K.PlacementRegion == CueBallNext::InHandAnywhere);
	RB_CHECK(!K.PushOutAllowed);
	RB_CHECK(!K.ThreeFoulWarning);
	const ShotDeclaration Push{ShotKind::PushOut, {}, BallGroup::None};
	const Vec2 Anywhere{0.1, 0.0};
	RB_CHECK(ValidateDeclaration(C, S, Push, &Anywhere) == ErrorCode::InvalidDeclaration);
}

RB_TEST(Rules_Match_EightOffTableOnBreakSpottedWithCueBallGap)
{
	// E09 shape: the 8 leaves the table on a legal-count break and is spotted (R 4.7) while the cue ball
	// rests next to the foot spot: the 8 keeps delta_cbGap to the cue ball. Then B decides: accept, or
	// ball in hand above the head string (first shot after the break: 3.11 region, placement required).
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	ClearLongString(S.Game);

	ShotOutcome O = DecideOutcome(1, {Option::AcceptTable, Option::BallInHandAboveHeadString});
	O.AnyFoul = true;
	O.Detected.Add(Foul::ObjectBallOffTable);
	O.Enforced = Foul::ObjectBallOffTable;
	O.CueBallIfAccepted = CueBallNext::InPosition;
	O.BallsToSpot.PushBack(8);
	ShotFacts F;
	FactsOffTable(F, 8);
	FactsOffTable(F, 13);
	FactsRestAt(F, kCueBallId, 0.64, 0.01);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	const GameState& G = S.Game;
	RB_CHECK(S.Phase == MatchPhase::AwaitDecision && S.Decider == 1);
	RB_CHECK(G.Balls[13].Kind == BallStatusKind::OutOfPlay); // 8-ball: stays out
	RB_CHECK(G.Balls[8].Kind == BallStatusKind::OnTable);
	const double D = 2.0 * kR + RulesTolerances{}.SpotCueBallGap;
	RB_CHECK_NEAR(G.Balls[8].Position.x, 0.64 + Sqrt(D * D - 0.01 * 0.01), 1e-12);
	RB_CHECK_NEAR(Length(G.Balls[8].Position - G.Balls[0].Position), D, 1e-12);

	MatchState Accept = S;
	RB_REQUIRE(ApplyOption(C, Accept, Option::AcceptTable) == ErrorCode::Ok);
	RB_CHECK(Accept.Game.Shooter == 1 && Accept.Game.CueBall == CueBallNext::InPosition && Accept.ShotAfterBreak);
	RB_CHECK(Accept.Game.TableOpen && !Accept.Game.IsBreakShot);

	RB_REQUIRE(ApplyOption(C, S, Option::BallInHandAboveHeadString) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 1 && S.Game.CueBall == CueBallNext::InHandAboveHeadString);
	const ShotDeclaration Normal{ShotKind::Normal, {3, PocketId::FootLeft}, BallGroup::None};
	const Vec2 Kitchen{-0.9, 0.0};
	const Vec2 OnString{-0.635, 0.0};
	RB_CHECK(ValidateDeclaration(C, S, Normal, &Kitchen) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Normal, &OnString) == ErrorCode::InvalidDeclaration);
	RB_CHECK(ValidateDeclaration(C, S, Normal, nullptr) == ErrorCode::InvalidDeclaration);
}

RB_TEST(Rules_Match_EightOnBreakWithScratchSpotOptionPutsEightOnFootSpot)
{
	// E06 shape: 8 + scratch on the break -> B chooses Spot8BallInHandAboveHeadString: the 8 goes to the
	// foot spot (the scratched cue ball is off the table), B shoots in hand above the head string.
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	ClearLongString(S.Game);
	ShotOutcome O = DecideOutcome(1, {Option::Spot8BallInHandAboveHeadString, Option::RerackDeciderBreaks});
	O.AnyFoul = true;
	O.Detected.Add(Foul::CueBallScratch);
	O.Enforced = Foul::CueBallScratch;
	ShotFacts F;
	FactsPocketed(F, 8, PocketId::FootLeft);
	FactsPocketed(F, kCueBallId, PocketId::FootRight);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	RB_CHECK(S.Game.Balls[8].Kind == BallStatusKind::Pocketed); // stays down until the decision
	RB_REQUIRE(ApplyOption(C, S, Option::Spot8BallInHandAboveHeadString) == ErrorCode::Ok);
	RB_CHECK(S.Game.Balls[8].Kind == BallStatusKind::OnTable);
	RB_CHECK_NEAR(S.Game.Balls[8].Position.x, 0.635, 1e-12);
	RB_CHECK(S.Game.Balls[0].Kind == BallStatusKind::Pocketed);
	RB_CHECK(S.Game.Shooter == 1 && S.Game.CueBall == CueBallNext::InHandAboveHeadString && S.Game.TableOpen);
	RB_CHECK(S.Phase == MatchPhase::AwaitShot && S.Decider == -1);
	RB_CHECK(ApplyOption(C, S, Option::RerackDeciderBreaks) == ErrorCode::InvalidState); // decision consumed
}

RB_TEST(Rules_Match_ThreeFoulWarningAcrossInnings)
{
	// R 3.13 / Reg 8: fouls count across innings until the player makes a legal shot; the warning is
	// shown whenever the shooter comes to the table on two fouls; the opponent's shots never touch it.
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, StandardFoul(0, 1), kNoFacts) == ErrorCode::Ok);    // A foul 1
	RB_CHECK(!GetShotConstraints(C, S).ThreeFoulWarning);                          // B on 0
	RB_REQUIRE(ApplyShot(C, S, StandardFoul(1, 1), kNoFacts) == ErrorCode::Ok);    // B foul 1
	RB_REQUIRE(ApplyShot(C, S, StandardFoul(0, 2), kNoFacts) == ErrorCode::Ok);    // A foul 2
	RB_CHECK(!GetShotConstraints(C, S).ThreeFoulWarning);                          // B on 1
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok);        // B legal miss -> B 0
	RB_CHECK(S.Game.Players[1].ConsecutiveFouls == 0);
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 2);
	RB_CHECK(GetShotConstraints(C, S).ThreeFoulWarning);                           // A comes to the table on two
	RB_REQUIRE(ApplyShot(C, S, ContinueOutcome(0), kNoFacts) == ErrorCode::Ok);    // A legal shot -> 0
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 0);
	RB_CHECK(!GetShotConstraints(C, S).ThreeFoulWarning);

	// Third foul: loss of the rack (RackWon for the opponent), counters reset with the new rack.
	RB_REQUIRE(ApplyShot(C, S, StandardFoul(0, 1), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, StandardFoul(0, 2), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(GetShotConstraints(C, S).ThreeFoulWarning);
	ShotOutcome Loss = RackWonOutcome(1);
	Loss.AnyFoul = true;
	Loss.Detected.Add(Foul::ThreeConsecutiveFouls);
	Loss.Enforced = Foul::ThreeConsecutiveFouls;
	Loss.FoulsAfter[0] = 0;
	RB_REQUIRE(ApplyShot(C, S, Loss, kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::RackOver && S.RackWins[1] == 1);
	RB_CHECK(S.Game.RackBreaker == 1); // Alternate: A broke rack 1
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 9)) == ErrorCode::Ok);
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 0 && S.Game.Players[1].ConsecutiveFouls == 0);

	// No three-foul rule (8-ball, or the switch off): never a warning.
	MatchConfig NoRule = C;
	NoRule.Rules.ThreeFoulRule = false;
	MatchState T = S;
	T.Game.Players[T.Game.Shooter].ConsecutiveFouls = 2;
	RB_CHECK(!GetShotConstraints(NoRule, T).ThreeFoulWarning);
	RB_CHECK(GetShotConstraints(C, T).ThreeFoulWarning);
	const MatchConfig Eight = MakeMatchConfig(Discipline::EightBall);
	MatchState E;
	RB_REQUIRE(StartToFirstBreak(Eight, E, 0));
	E.Game.Players[0].ConsecutiveFouls = 2;
	RB_CHECK(!GetShotConstraints(Eight, E).ThreeFoulWarning);
}

RB_TEST(Rules_Match_PushOutWindowEdges)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	const ShotDeclaration Push{ShotKind::PushOut, {}, BallGroup::None};
	const Vec2 Spot{0.0, 0.3};

	// (1) A foul on the break closes the window: B has ball in hand but no push-out.
	{
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		RB_REQUIRE(ApplyShot(C, S, StandardFoul(0, 1, Foul::BreakTooFewRails), kNoFacts) == ErrorCode::Ok);
		RB_CHECK(!GetShotConstraints(C, S).PushOutAllowed);
		RB_CHECK(ValidateDeclaration(C, S, Push, &Spot) == ErrorCode::InvalidDeclaration);
	}
	// (2) A push-out that is itself a foul: standard foul, no decision, no second push-out for A.
	{
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		ShotOutcome Break = PassOutcome(1);
		Break.NextPushOutAvailable = true;
		RB_REQUIRE(ApplyShot(C, S, Break, kNoFacts) == ErrorCode::Ok);
		RB_REQUIRE(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::Ok);
		ShotFacts Scratch;
		FactsPocketed(Scratch, kCueBallId, PocketId::SideLeft);
		RB_REQUIRE(ApplyShot(C, S, StandardFoul(1, 1, Foul::CueBallScratch), Scratch) == ErrorCode::Ok);
		RB_CHECK(S.Phase == MatchPhase::AwaitShot && S.Game.Shooter == 0);
		RB_CHECK(S.Game.CueBall == CueBallNext::InHandAnywhere && !S.Game.PushOutAvailable);
		RB_CHECK(ValidateDeclaration(C, S, Push, &Spot) == ErrorCode::InvalidDeclaration);
		RB_CHECK(S.Game.Players[1].ConsecutiveFouls == 1); // N09: the pusher's counter +1
	}
	// (3) After a foul-free push-out the other player shoots from position: no push-out for him either.
	{
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		ShotOutcome Break = ContinueOutcome(0); // A made a ball: A may push out
		Break.NextPushOutAvailable = true;
		RB_REQUIRE(ApplyShot(C, S, Break, kNoFacts) == ErrorCode::Ok);
		RB_REQUIRE(ApplyShot(C, S, DecideOutcome(1, {Option::ShootFromPosition, Option::PassBack}), kNoFacts) == ErrorCode::Ok);
		RB_REQUIRE(ApplyOption(C, S, Option::ShootFromPosition) == ErrorCode::Ok);
		RB_CHECK(S.Game.Shooter == 1 && S.Game.CueBall == CueBallNext::InPosition);
		RB_CHECK(!GetShotConstraints(C, S).PushOutAllowed);
		RB_CHECK(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::InvalidDeclaration);
	}
	// (4) Accepting a failed three-ball-rule break closes the window (AcceptTableNoPushOut).
	{
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		RB_REQUIRE(ApplyShot(C, S, DecideOutcome(1, {Option::AcceptTableNoPushOut, Option::HandBackPushOutAllowed}), kNoFacts) ==
			ErrorCode::Ok);
		RB_REQUIRE(ApplyOption(C, S, Option::AcceptTableNoPushOut) == ErrorCode::Ok);
		RB_CHECK(S.Game.Shooter == 1 && !S.Game.PushOutAvailable);
		RB_CHECK(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::InvalidDeclaration);
	}
	// (5) 10-ball: a wrongfully pocketed ball outside the window - neither choice opens a push-out.
	{
		const MatchConfig Ten = MakeMatchConfig(Discipline::TenBall);
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(Ten, S, 0));
		RB_REQUIRE(ApplyShot(Ten, S, PassOutcome(1), kNoFacts) == ErrorCode::Ok);
		RB_REQUIRE(ApplyShot(Ten, S, DecideOutcome(0, {Option::ShootFromPosition, Option::PassBack}), kNoFacts) == ErrorCode::Ok);
		RB_REQUIRE(ApplyOption(Ten, S, Option::PassBack) == ErrorCode::Ok);
		RB_CHECK(S.Game.Shooter == 1 && !S.Game.PushOutAvailable);
		RB_CHECK(ValidateDeclaration(Ten, S, Push, nullptr) == ErrorCode::InvalidDeclaration);
		RB_CHECK(GetShotConstraints(Ten, S).CallRequired);
	}
}

RB_TEST(Rules_Match_StraightPoolFoulSpotsSeveralBallsPastCueBall)
{
	// 14.1 standard foul (R 7.9): -1, both pocketed balls spotted in ascending order, the cue ball stays
	// where it stopped - here next to the foot spot, so the 3 goes behind it (keeping delta_cbGap) and the
	// 7 touches the 3.
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1), kNoFacts) == ErrorCode::Ok); // opening break
	ClearLongString(S.Game);
	ShotOutcome O = StandardFoul(1, 1, Foul::NoRailAfterContact, CueBallNext::InPosition);
	O.ScoreDelta[1] = -1;
	O.BallsToSpot.PushBack(3);
	O.BallsToSpot.PushBack(7);
	ShotFacts F;
	FactsPocketed(F, 7, PocketId::FootLeft);
	FactsPocketed(F, 3, PocketId::FootRight);
	FactsRestAt(F, kCueBallId, 0.66, 0.01);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	const GameState& G = S.Game;
	const double D = 2.0 * kR + RulesTolerances{}.SpotCueBallGap;
	const double X3 = 0.66 + Sqrt(D * D - 0.01 * 0.01);
	RB_CHECK_NEAR(G.Balls[3].Position.x, X3, 1e-12);
	RB_CHECK_NEAR(G.Balls[7].Position.x, X3 + 2.0 * kR, 1e-12);
	RB_CHECK(G.Balls[3].Position.y == 0.0 && G.Balls[7].Position.y == 0.0);
	RB_CHECK(G.Balls[0].Kind == BallStatusKind::OnTable && G.Balls[0].Position.x == 0.66);
	RB_CHECK(G.CueBall == CueBallNext::InPosition && G.Shooter == 0);
	RB_CHECK(G.Players[1].Score == -1 && G.Players[1].ConsecutiveFouls == 1);
}

RB_TEST(Rules_Match_StraightPoolBreakingFoulOnTwoFoulsFlow)
{
	// S25 at the match level: after a stalemate re-lag A is on two fouls (carried over) and fails the
	// opening break with a scratch: -2 only, the counter STAYS 2 (a breaking foul never counts, R 7.11),
	// B decides. RequireRebreak: A breaks again (still warned: a standard foul on the re-break could be
	// his third). AcceptTable: B shoots with the cue ball in hand above the head string.
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1), kNoFacts) == ErrorCode::Ok);
	S.Game.Players[0].Score = 30;
	S.Game.Players[0].ConsecutiveFouls = 2;
	RB_REQUIRE(DeclareStalemate(C, S) == ErrorCode::Ok);
	LagResult Lag;
	Lag.Outcome = LagOutcome::SecondWins;
	RB_REQUIRE(ApplyLagResult(C, S, Lag) == ErrorCode::Ok);
	RB_REQUIRE(ChooseBreaker(C, S, 0) == ErrorCode::Ok); // B makes A break
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 15)) == ErrorCode::Ok);
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 2 && S.Game.IsBreakShot);
	RB_CHECK(GetShotConstraints(C, S).ThreeFoulWarning);

	ShotOutcome O = DecideOutcome(1, {Option::AcceptTable, Option::RequireRebreak});
	O.AnyFoul = true;
	O.Detected.Add(Foul::BreakingFoul141);
	O.Detected.Add(Foul::CueBallScratch);
	O.Enforced = Foul::BreakingFoul141;
	O.ScoreDelta[0] = -2;
	O.FoulsAfter[0] = 2;
	O.CueBallIfAccepted = CueBallNext::InHandAboveHeadString;
	ShotFacts F;
	FactsPocketed(F, kCueBallId, PocketId::HeadLeft);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	RB_CHECK(S.Game.Players[0].Score == 28 && S.Game.Players[0].ConsecutiveFouls == 2);
	RB_CHECK(S.Phase == MatchPhase::AwaitDecision && S.Decider == 1);

	MatchState Accept = S;
	RB_REQUIRE(ApplyOption(C, Accept, Option::AcceptTable) == ErrorCode::Ok);
	RB_CHECK(Accept.Game.Shooter == 1 && Accept.Game.CueBall == CueBallNext::InHandAboveHeadString);
	RB_CHECK(!Accept.Game.IsBreakShot && Accept.ShotAfterBreak);

	RB_REQUIRE(ApplyOption(C, S, Option::RequireRebreak) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::RackSetup && S.Game.RackBreaker == 0);
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 15)) == ErrorCode::Ok);
	RB_CHECK(S.Game.IsBreakShot && S.Game.Shooter == 0 && S.Game.CueBall == CueBallNext::InHandAboveHeadString);
	RB_CHECK(S.Game.Players[0].Score == 28 && S.Game.Players[0].ConsecutiveFouls == 2);
	RB_CHECK(GetShotConstraints(C, S).ThreeFoulWarning);
}

RB_TEST(Rules_Match_StalemateWhileDecisionPending)
{
	// E02 pending (B decides after A's illegal break) when a stalemate is agreed: the rack is replayed by
	// its original breaker, the pending options are gone.
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, DecideOutcome(1, {Option::AcceptTable, Option::RerackDeciderBreaks, Option::RerackOffenderBreaks}), kNoFacts) ==
		ErrorCode::Ok);
	RB_REQUIRE(DeclareStalemate(C, S) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::RackSetup && S.Game.RackBreaker == 0 && S.Decider == -1);
	RB_CHECK(S.PendingOutcome.Options.Size() == 0);
	RB_CHECK(ApplyOption(C, S, Option::AcceptTable) == ErrorCode::InvalidState);
	RB_CHECK(DeclareStalemate(C, S) == ErrorCode::InvalidState); // not again from RackSetup
}

RB_TEST(Rules_Match_BlackballBlackOnBreakSameBreakerSameMember)
{
	// 12.4: black potted on the break -> re-rack, the same player (in Doubles: the same member) breaks again.
	MatchConfig C = MakeMatchConfig(Discipline::Blackball);
	C.Doubles = true;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 1));
	RB_CHECK(GetShotConstraints(C, S).Member == 0);
	ShotOutcome O;
	O.Next = NextAction::RerackAndBreak;
	O.NextShooter = 1;
	ShotFacts F;
	FactsPocketed(F, 8, PocketId::SideLeft);
	FactsPocketed(F, kCueBallId, PocketId::FootLeft); // ignored by the rules; the rack is replayed
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::RackSetup && S.Game.RackBreaker == 1);
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 15)) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 1 && S.Game.IsBreakShot && S.Game.CueBall == CueBallNext::InHandBaulk);
	RB_CHECK(GetShotConstraints(C, S).Member == 0);
	RB_CHECK(S.RackWins[0] == 0 && S.RackWins[1] == 0);
}

RB_TEST(Rules_Match_DoublesWinnerBreaksAlternatesTeamBreakers)
{
	// Doubles with WinnerBreaks: a team that keeps winning keeps breaking, its members alternate.
	MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	C.Doubles = true;
	C.Rules.Breaks = BreakOrder::WinnerBreaks;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_CHECK(GetShotConstraints(C, S).Member == 0);
	RB_REQUIRE(WinRack(C, S, 0));
	RB_CHECK(S.Game.Shooter == 0 && GetShotConstraints(C, S).Member == 1);
	RB_REQUIRE(WinRack(C, S, 0));
	RB_CHECK(S.Game.Shooter == 0 && GetShotConstraints(C, S).Member == 0);
	RB_REQUIRE(WinRack(C, S, 1));
	RB_CHECK(S.Game.Shooter == 1 && GetShotConstraints(C, S).Member == 0);
}

RB_TEST(Integ_Rules_Match_ContinuationRackNeverOverlapsKeptBall)
{
	// Needs the WP-2 lattice and micro-gaps. The 15th ball rests just behind the outline (it does NOT
	// interfere, 5.2, so it stays), the cue ball just beside it. Micro-gaps push the rack slightly beyond
	// the tight outline; the continuation rack must still not overlap a ball that stays on the table.
	MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	C.RackGaps = kRackGapSloppyBar;
	const RackOutline Outline = StraightPoolRackOutline(C.Table);
	const Vec2 Fifteenth{Outline.BackLeft.x + kR + 2e-5, 0.0};
	const Vec2 Cue{Outline.BackLeft.x + kR + 2e-5, -0.10};
	RB_REQUIRE(!InterferesWithRack(Fifteenth, kR, C.Table) && !InterferesWithRack(Cue, kR, C.Table));
	for (std::uint64_t Seed = 0; Seed < 20; ++Seed)
	{
		C.Seed = Seed;
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		GameState& G = S.Game;
		for (int b = 1; b <= 13; ++b)
		{
			G.Balls[b].Kind = BallStatusKind::Pocketed;
		}
		Place(G, 14, -0.3, 0.3);
		Place(G, 15, Fifteenth.x, Fifteenth.y);
		Place(G, 0, -0.5, -0.3);
		G.IsBreakShot = false;
		G.CueBall = CueBallNext::InPosition;

		ShotFacts F;
		FactsPocketed(F, 14, PocketId::SideLeft);
		FactsRestAt(F, kCueBallId, Cue.x, Cue.y);
		ShotOutcome O = ContinueOutcome(0);
		O.ScoreDelta[0] = 1;
		O.Rack = PlanRerack14(Cue, 15, Fifteenth, C.Table, C.Rules.Tolerances);
		RB_REQUIRE(O.Rack.Kind == RackCommandKind::Rerack14);
		RB_REQUIRE(O.Rack.CueBallPlacement == PlacementCommand::Keep && O.Rack.FifteenthBallPlacement == PlacementCommand::Keep);
		RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
		for (int b = 1; b <= 14; ++b)
		{
			RB_CHECK(G.Balls[b].Kind == BallStatusKind::OnTable);
			RB_CHECK(Length(G.Balls[b].Position - G.Balls[15].Position) >= 2.0 * kR - 1e-7);
			RB_CHECK(Length(G.Balls[b].Position - G.Balls[0].Position) >= 2.0 * kR - 1e-7);
		}
	}
}

RB_TEST(Integ_Rules_S21_SpotOntoApexOfGappedContinuationRack)
{
	// Needs the WP-2 lattice and micro-gaps. S21 end to end: a Rerack14 built by ApplyShot (apex empty,
	// wooden-rack gaps, the empty apex is the gap anchor), then A fouls and pockets a rack ball: it is
	// spotted exactly on the empty apex (foot spot).
	MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	C.RackGaps = kRackGapWoodenRack;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	GameState& G = S.Game;
	for (int b = 1; b <= 13; ++b)
	{
		G.Balls[b].Kind = BallStatusKind::Pocketed;
	}
	Place(G, 14, -0.3, 0.3);
	Place(G, 15, 0.0, 0.3);
	Place(G, 0, -0.5, -0.3);
	G.IsBreakShot = false;
	G.CueBall = CueBallNext::InPosition;
	ShotFacts Score;
	FactsPocketed(Score, 14, PocketId::SideLeft);
	FactsRestAt(Score, kCueBallId, -0.9, -0.2);
	ShotOutcome O = ContinueOutcome(0);
	O.ScoreDelta[0] = 1;
	O.Rack = PlanRerack14({-0.9, -0.2}, 15, {0.0, 0.3}, C.Table, C.Rules.Tolerances);
	RB_REQUIRE(ApplyShot(C, S, O, Score) == ErrorCode::Ok);
	for (int b = 1; b <= 14; ++b)
	{
		RB_REQUIRE(G.Balls[b].Kind == BallStatusKind::OnTable);
		RB_CHECK(!(G.Balls[b].Position == C.Table.FootSpot));
	}

	// A fouls (no rail) and pockets ball 6 of the fresh rack: -1, 6 on the empty apex, CB stays.
	ShotOutcome FoulShot = StandardFoul(0, 1, Foul::NoRailAfterContact, CueBallNext::InPosition);
	FoulShot.ScoreDelta[0] = -1;
	FoulShot.BallsToSpot.PushBack(6);
	ShotFacts F;
	FactsPocketed(F, 6, PocketId::FootLeft);
	FactsRestAt(F, kCueBallId, -0.4, 0.1);
	RB_REQUIRE(ApplyShot(C, S, FoulShot, F) == ErrorCode::Ok);
	RB_CHECK(G.Balls[6].Position == C.Table.FootSpot);
	RB_CHECK(G.Players[0].Score == 0 && G.Players[0].ConsecutiveFouls == 1 && G.Shooter == 1);
	for (int b = 1; b <= 15; ++b)
	{
		if (b != 6)
		{
			RB_CHECK(Length(G.Balls[b].Position - G.Balls[6].Position) >= 2.0 * kR - 1e-6);
		}
	}
}
