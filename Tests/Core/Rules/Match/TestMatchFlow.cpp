// Owner: WP-9 (rules table procedures & match). rules.md 11 match state machine, 4.2 break order,
// 4.8 stalemate / concession, 9.5 continuation racks in ApplyShot, Reg 27 doubles, 12.4 free shot;
// tests M01-M06, N20, N22, S21, A-DBL-1, A-VIS-1. Match tests feed hand-built ShotOutcomes; the
// Integ_ variants use WP-8's EvaluateShot or WP-2's rack lattice.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Rules/Evaluate.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

namespace
{
	const ShotFacts kNoFacts{};

	void CueBallEndsAt(ShotFacts& F, double X, double Y)
	{
		F.Balls[kCueBallId].EndStatus = BallEndStatus::OnTable;
		F.Balls[kCueBallId].FinalPosition = {X, Y};
	}

	void Pocket(ShotFacts& F, int Ball, PocketId P)
	{
		PocketedBall B;
		B.Ball = static_cast<BallId>(Ball);
		B.Pocket = P;
		F.Pocketed.PushBack(B);
		F.Balls[Ball].Pocketed = true;
		F.Balls[Ball].Pocket = P;
		F.Balls[Ball].EndStatus = BallEndStatus::Pocketed;
		if (Ball == kCueBallId)
		{
			F.CueBallPocketed = true;
		}
		else
		{
			F.AnyObjectBallPocketed = true;
		}
	}

	ShotOutcome FoulPass(int Shooter, int FoulsAfter)
	{
		ShotOutcome O = PassOutcome(1 - Shooter, CueBallNext::InHandAnywhere);
		O.AnyFoul = true;
		O.Detected.Add(Foul::WrongBallFirst);
		O.Enforced = Foul::WrongBallFirst;
		O.FoulsAfter[Shooter] = FoulsAfter;
		return O;
	}
}

RB_TEST(Rules_M01_AlternateBreakIndependentOfWinners)
{
	MatchConfig C = MakeMatchConfig(Discipline::NineBall, 3);
	C.Rules.Breaks = BreakOrder::Alternate;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0)); // A won the lag and breaks
	RB_CHECK(S.Phase == MatchPhase::AwaitShot);
	RB_CHECK(S.Game.RackBreaker == 0 && S.Game.Shooter == 0 && S.Game.IsBreakShot);
	RB_CHECK(S.Game.CueBall == CueBallNext::InHandAboveHeadString);
	RB_CHECK(S.RackNumber == 1);

	RB_REQUIRE(ApplyShot(C, S, RackWonOutcome(0), kNoFacts) == ErrorCode::Ok); // A wins rack 1
	RB_CHECK(S.Phase == MatchPhase::RackOver);
	RB_CHECK(S.RackWins[0] == 1 && S.RackWins[1] == 0);
	RB_CHECK(S.Game.RackBreaker == 1);
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 9)) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 1 && S.Game.IsBreakShot && S.RackNumber == 2); // rack 2: B

	RB_REQUIRE(WinRack(C, S, 0)); // A wins rack 2 although B broke
	RB_CHECK(S.Game.RackBreaker == 0 && S.Game.Shooter == 0); // rack 3: A
	RB_REQUIRE(WinRack(C, S, 1));
	RB_CHECK(S.Phase == MatchPhase::AwaitShot);
	RB_CHECK(S.RackWins[0] == 2 && S.RackWins[1] == 1);
	RB_CHECK(S.Game.RackBreaker == 1); // rack 4: B
}

RB_TEST(Rules_M02_WinnerBreaks)
{
	MatchConfig C = MakeMatchConfig(Discipline::NineBall, 7);
	C.Rules.Breaks = BreakOrder::WinnerBreaks;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(WinRack(C, S, 1));
	RB_CHECK(S.Game.RackBreaker == 1);
	RB_REQUIRE(WinRack(C, S, 1));
	RB_CHECK(S.Game.RackBreaker == 1);
	RB_REQUIRE(WinRack(C, S, 0));
	RB_CHECK(S.Game.RackBreaker == 0);

	C.Rules.Breaks = BreakOrder::LoserBreaks;
	RB_REQUIRE(WinRack(C, S, 0));
	RB_CHECK(S.Game.RackBreaker == 1);
}

RB_TEST(Rules_M03_NineBallStalemateReplaysRackSameBreaker)
{
	MatchConfig C = MakeMatchConfig(Discipline::NineBall, 7);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(WinRack(C, S, 0)); // rack 1 (A broke)
	RB_REQUIRE(WinRack(C, S, 1)); // rack 2 (B broke)
	RB_REQUIRE(WinRack(C, S, 0)); // rack 3 (A broke)
	RB_CHECK(S.RackNumber == 4 && S.Game.RackBreaker == 1);
	RB_CHECK(S.RackWins[0] == 2 && S.RackWins[1] == 1);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok); // play goes on in rack 4
	RB_CHECK(S.Game.Shooter == 0);

	RB_REQUIRE(DeclareStalemate(C, S) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::RackSetup);
	RB_CHECK(S.Game.RackBreaker == 1);
	RB_CHECK(S.RackWins[0] == 2 && S.RackWins[1] == 1);
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 9)) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 1 && S.Game.IsBreakShot);
	RB_CHECK(S.RackWins[0] == 2 && S.RackWins[1] == 1);
}

RB_TEST(Rules_M04_StraightPoolStalemateNewLagScoresKept)
{
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1), kNoFacts) == ErrorCode::Ok);
	S.Game.Players[0].Score = 47;
	S.Game.Players[1].Score = 33;
	S.Game.Players[0].ConsecutiveFouls = 2;

	RB_REQUIRE(DeclareStalemate(C, S) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::Lag);
	LagResult Lag;
	Lag.Outcome = LagOutcome::SecondWins;
	RB_REQUIRE(ApplyLagResult(C, S, Lag) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::LagWinnerChooses && S.Decider == 1);
	RB_REQUIRE(ChooseBreaker(C, S, 0) == ErrorCode::Ok); // B makes A take the opening break
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 15)) == ErrorCode::Ok);
	RB_CHECK(S.Game.IsBreakShot); // opening break
	RB_CHECK(S.Game.Shooter == 0);
	RB_CHECK(S.Game.Players[0].Score == 47 && S.Game.Players[1].Score == 33);
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 2); // 14.1 counters carried over (rules.md 14 #17)
	RB_CHECK(S.FirstBreaker == 0);
}

RB_TEST(Rules_M05_ConcessionLosesMatch)
{
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	Concede(S, 0);
	RB_CHECK(S.Phase == MatchPhase::MatchOver);
	RB_CHECK(S.Winner == 1);
	RB_CHECK(ApplyShot(C, S, PassOutcome(1), kNoFacts) == ErrorCode::InvalidState);
}

RB_TEST(Rules_M06_HillHillRackWinsMatch)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall, 5);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	S.RackWins[0] = 4;
	S.RackWins[1] = 4;
	RB_REQUIRE(ApplyShot(C, S, RackWonOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::MatchOver);
	RB_CHECK(S.Winner == 0);
	RB_CHECK(S.RackWins[0] == 5);
	RackAssignment Rack;
	RB_CHECK(SetupRack(C, S, Rack) == ErrorCode::InvalidState);
}

RB_TEST(Rules_Match_SetsDecideTheMatch)
{
	MatchConfig C = MakeMatchConfig(Discipline::NineBall, 99);
	C.RacksPerSet = 2;
	C.SetsToWin = 2;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(WinRack(C, S, 0));
	RB_REQUIRE(WinRack(C, S, 0)); // set 1 to A
	RB_CHECK(S.SetWins[0] == 1 && S.RackWins[0] == 0 && S.RackWins[1] == 0);
	RB_REQUIRE(WinRack(C, S, 1));
	RB_REQUIRE(WinRack(C, S, 0));
	RB_CHECK(S.Phase != MatchPhase::MatchOver);
	RB_REQUIRE(WinRack(C, S, 0)); // set 2 to A
	RB_CHECK(S.Phase == MatchPhase::MatchOver && S.Winner == 0 && S.SetWins[0] == 2);
}

RB_TEST(Rules_N20_NewRackResetsFoulCounters)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, FoulPass(0, 1), kNoFacts) == ErrorCode::Ok); // A fouls
	RB_REQUIRE(ApplyShot(C, S, FoulPass(1, 1), kNoFacts) == ErrorCode::Ok); // B fouls
	RB_REQUIRE(ApplyShot(C, S, FoulPass(0, 2), kNoFacts) == ErrorCode::Ok); // A fouls again
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 2);
	RB_CHECK(S.Game.Players[1].ConsecutiveFouls == 1); // only the shooter's counter is taken from an outcome
	RB_CHECK(S.Game.Shooter == 1);
	RB_CHECK(S.Game.CueBall == CueBallNext::InHandAnywhere);

	RB_REQUIRE(ApplyShot(C, S, RackWonOutcome(1), kNoFacts) == ErrorCode::Ok); // B wins the rack
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 9)) == ErrorCode::Ok);
	RB_CHECK(S.Game.Players[0].ConsecutiveFouls == 0);
	RB_CHECK(S.Game.Players[1].ConsecutiveFouls == 0);
}

RB_TEST(Rules_Match_ThreeFoulWarningShown)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, FoulPass(0, 2), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 0);
	RB_CHECK(GetShotConstraints(C, S).ThreeFoulWarning); // the display is the mandatory warning (Reg 8)
}

RB_TEST(Rules_N22_HandBackPushOutThenPassBack)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	// N05: three-ball rule failed on A's break -> B decides.
	RB_REQUIRE(ApplyShot(C, S, DecideOutcome(1, {Option::AcceptTableNoPushOut, Option::HandBackPushOutAllowed}), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::AwaitDecision && S.Decider == 1);
	RB_CHECK(ApplyOption(C, S, Option::PassBack) == ErrorCode::InvalidOption); // not offered
	RB_REQUIRE(ApplyOption(C, S, Option::HandBackPushOutAllowed) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::AwaitShot);
	RB_CHECK(S.Game.Shooter == 0 && S.Game.PushOutAvailable);
	RB_CHECK(GetShotConstraints(C, S).PushOutAllowed);

	ShotDeclaration Push;
	Push.Kind = ShotKind::PushOut;
	RB_REQUIRE(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, DecideOutcome(1, {Option::ShootFromPosition, Option::PassBack}), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(ApplyOption(C, S, Option::PassBack) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 0);                  // A must shoot
	RB_CHECK(S.Game.CueBall == CueBallNext::InPosition); // from position
	RB_CHECK(!S.Game.PushOutAvailable);             // no second push-out
	RB_CHECK(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::InvalidDeclaration);
	ShotDeclaration Normal;
	RB_CHECK(ValidateDeclaration(C, S, Normal, nullptr) == ErrorCode::Ok);
}

RB_TEST(Integ_Rules_N22_EndToEndWithEvaluateShot)
{
	// Needs WP-8 EvaluateShot (three-ball rule and push-out paths).
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));

	ShotFacts Break;
	Break.EarliestContact = 1;
	Break.FirstContactTime = 0.1;
	Break.FirstContactTieSet.PushBack(1);
	Break.NumObjectBallsDrivenToRail = 6;
	Break.CountPocketedOrCrossedHeadString = 2;
	Break.DrivenToRailAfterContactStrict = 0x7Eu;
	Break.DrivenToRailAfterContactLegal = 0x7Eu;
	CueBallEndsAt(Break, -0.3, 0.2);
	ShotDeclaration BreakShot;
	BreakShot.Kind = ShotKind::Break;
	const Vec2 Kitchen{-0.9, 0.1};
	RB_REQUIRE(ValidateDeclaration(C, S, BreakShot, &Kitchen) == ErrorCode::Ok);
	const ShotOutcome O1 = EvaluateShot(C.Rules, C.Table, S.Game, BreakShot, Break);
	RB_CHECK(O1.Next == NextAction::AwaitDecision && O1.NextShooter == 1);
	RB_REQUIRE(ApplyShot(C, S, O1, Break) == ErrorCode::Ok);
	RB_REQUIRE(S.Phase == MatchPhase::AwaitDecision);
	RB_REQUIRE(ApplyOption(C, S, Option::HandBackPushOutAllowed) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 0 && S.Game.PushOutAvailable);

	ShotDeclaration Push;
	Push.Kind = ShotKind::PushOut;
	RB_REQUIRE(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::Ok);
	ShotFacts PushFacts; // CB touches nothing, no rail: legal on a push-out
	CueBallEndsAt(PushFacts, 0.1, -0.2);
	const ShotOutcome O2 = EvaluateShot(C.Rules, C.Table, S.Game, Push, PushFacts);
	RB_CHECK(O2.Next == NextAction::AwaitDecision && O2.NextShooter == 1);
	RB_REQUIRE(ApplyShot(C, S, O2, PushFacts) == ErrorCode::Ok);
	RB_REQUIRE(S.Phase == MatchPhase::AwaitDecision);
	RB_REQUIRE(ApplyOption(C, S, Option::PassBack) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 0 && !S.Game.PushOutAvailable);
	RB_CHECK(S.Game.Balls[0].Position.x == 0.1);
	RB_CHECK(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::InvalidDeclaration);
}

RB_TEST(Rules_S21_FoulRightAfterRerackSpotsOnEmptyApex)
{
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	GameState& G = S.Game;
	const RackAssignment Rack = HandRack(1, 14, true); // the 14-ball rack, apex empty
	for (int b = 1; b <= 14; ++b)
	{
		Place(G, b, Rack.Position[b].x, Rack.Position[b].y);
	}
	Place(G, 15, 0.0, 0.3);
	Place(G, 0, -0.9, -0.2);
	G.IsBreakShot = false;
	G.CueBall = CueBallNext::InPosition;

	// A fouls and pockets ball 6: -1, 6 spotted, turn passes, CB stays where it stopped.
	ShotOutcome O = PassOutcome(1, CueBallNext::InPosition);
	O.AnyFoul = true;
	O.Detected.Add(Foul::NoRailAfterContact);
	O.Enforced = Foul::NoRailAfterContact;
	O.ScoreDelta[0] = -1;
	O.FoulsAfter[0] = 1;
	O.BallsToSpot.PushBack(6);
	ShotFacts F;
	Pocket(F, 6, PocketId::FootLeft);
	CueBallEndsAt(F, -0.5, 0.1);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	RB_CHECK(G.Balls[6].Kind == BallStatusKind::OnTable);
	RB_CHECK_NEAR(G.Balls[6].Position.x, 0.635, 1e-9);
	RB_CHECK_NEAR(G.Balls[6].Position.y, 0.0, 1e-12);
	RB_CHECK(G.Players[0].Score == -1);
	RB_CHECK(G.Players[0].ConsecutiveFouls == 1);
	RB_CHECK(G.Shooter == 1);
	RB_CHECK(G.Balls[0].Position.x == -0.5 && G.Balls[0].Position.y == 0.1);
}

RB_TEST(Rules_Match_ApplyShotBallStatusAndGroups)
{
	// 8-ball: called ball made on an open table -> groups assigned after the shot (pitfall 24); balls
	// pocketed / driven off leave the table, others move to their final positions.
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	RB_REQUIRE(ApplyShot(C, S, ContinueOutcome(0), kNoFacts) == ErrorCode::Ok); // legal break, ball made
	ShotOutcome O = ContinueOutcome(0);
	O.AssignShooterGroup = BallGroup::Stripes;
	ShotFacts F;
	Pocket(F, 12, PocketId::SideLeft);
	F.ObjectBallsOffTable = 1u << 3;
	F.Balls[5].EndStatus = BallEndStatus::OnTable;
	F.Balls[5].FinalPosition = {0.1, 0.2};
	CueBallEndsAt(F, -0.4, 0.0);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	const GameState& G = S.Game;
	RB_CHECK(!G.TableOpen);
	RB_CHECK(G.Players[0].Group == BallGroup::Stripes && G.Players[1].Group == BallGroup::Solids);
	RB_CHECK(G.Balls[12].Kind == BallStatusKind::Pocketed);
	RB_CHECK(G.Balls[3].Kind == BallStatusKind::OutOfPlay); // 8-ball: stays out
	RB_CHECK(G.Balls[5].Position.x == 0.1 && G.Balls[5].Position.y == 0.2);
	RB_CHECK(G.Balls[0].Kind == BallStatusKind::OnTable && G.Balls[0].Position.x == -0.4);
	RB_CHECK(!G.IsBreakShot && G.Shooter == 0);

	// Scratch: the cue ball is in hand (off the table) for the opponent.
	ShotOutcome Foul = PassOutcome(1, CueBallNext::InHandAnywhere);
	Foul.AnyFoul = true;
	ShotFacts Scratch;
	Pocket(Scratch, kCueBallId, PocketId::HeadLeft);
	RB_REQUIRE(ApplyShot(C, S, Foul, Scratch) == ErrorCode::Ok);
	RB_CHECK(S.Game.Balls[0].Kind == BallStatusKind::Pocketed);
	RB_CHECK(S.Game.CueBall == CueBallNext::InHandAnywhere && S.Game.Shooter == 1);
}

RB_TEST(Rules_Match_EightBallBreakOptions)
{
	// E05/E06/E02 option effects (11.3 table) on hand-built outcomes.
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	{
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		ShotFacts F;
		Pocket(F, 8, PocketId::FootRight);
		Pocket(F, 4, PocketId::SideRight);
		CueBallEndsAt(F, -0.2, 0.0);
		RB_REQUIRE(ApplyShot(C, S, DecideOutcome(0, {Option::Spot8ContinueFromPosition, Option::RerackDeciderBreaks}), F) == ErrorCode::Ok);
		RB_REQUIRE(ApplyOption(C, S, Option::Spot8ContinueFromPosition) == ErrorCode::Ok);
		RB_CHECK(S.Game.Shooter == 0 && S.Game.CueBall == CueBallNext::InPosition && S.Game.TableOpen);
		RB_CHECK(S.Game.Balls[8].Kind == BallStatusKind::OnTable);
		RB_CHECK(S.Game.Balls[4].Kind == BallStatusKind::Pocketed);
	}
	{
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		ShotFacts F;
		Pocket(F, 8, PocketId::FootRight);
		Pocket(F, kCueBallId, PocketId::HeadRight);
		RB_REQUIRE(ApplyShot(C, S, DecideOutcome(1, {Option::Spot8BallInHandAboveHeadString, Option::RerackDeciderBreaks}), F) == ErrorCode::Ok);
		RB_REQUIRE(ApplyOption(C, S, Option::RerackDeciderBreaks) == ErrorCode::Ok);
		RB_CHECK(S.Phase == MatchPhase::RackSetup && S.Game.RackBreaker == 1); // INTERPRETATION: the incoming player breaks
		RB_CHECK(S.RackWins[0] == 0 && S.RackWins[1] == 0);
	}
	{
		MatchState S;
		RB_REQUIRE(StartToFirstBreak(C, S, 0));
		ShotOutcome O = DecideOutcome(1, {Option::AcceptTable, Option::RerackDeciderBreaks, Option::RerackOffenderBreaks});
		O.CueBallIfAccepted = CueBallNext::InHandAboveHeadString;
		RB_REQUIRE(ApplyShot(C, S, O, kNoFacts) == ErrorCode::Ok);
		MatchState Accept = S;
		RB_REQUIRE(ApplyOption(C, Accept, Option::AcceptTable) == ErrorCode::Ok);
		RB_CHECK(Accept.Game.Shooter == 1 && Accept.Game.CueBall == CueBallNext::InHandAboveHeadString);
		RB_REQUIRE(ApplyOption(C, S, Option::RerackOffenderBreaks) == ErrorCode::Ok);
		RB_CHECK(S.Phase == MatchPhase::RackSetup && S.Game.RackBreaker == 0);
	}
}

RB_TEST(Rules_Match_StraightPoolThreeFoulRerackAndOpeningBreak)
{
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	S.Game.IsBreakShot = false;
	S.Game.Players[0].Score = 20;
	ShotOutcome O;
	O.Next = NextAction::RerackAndBreak;
	O.NextShooter = 0;
	O.AnyFoul = true;
	O.ScoreDelta[0] = -16;
	O.FoulsAfter[0] = 0;
	O.NextCueBall = CueBallNext::InHandAboveHeadString;
	O.Rack.Kind = RackCommandKind::None;
	RB_REQUIRE(ApplyShot(C, S, O, kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::RackSetup && S.Game.RackBreaker == 0);
	RB_CHECK(S.Game.Players[0].Score == 4);
	RB_REQUIRE(SetupRackWith(C, S, HandRack(1, 15)) == ErrorCode::Ok);
	RB_CHECK(S.Game.IsBreakShot && S.Game.Shooter == 0 && S.Game.CueBall == CueBallNext::InHandAboveHeadString);
	RB_CHECK(S.Game.Players[0].Score == 4);

	// 14.1 match won at the target (MatchWon from the evaluator).
	ShotOutcome Win = ContinueOutcome(0);
	Win.Next = NextAction::MatchWon;
	Win.Winner = 0;
	Win.ScoreDelta[0] = 96;
	RB_REQUIRE(ApplyShot(C, S, Win, kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::MatchOver && S.Winner == 0 && S.Game.Players[0].Score == 100);
}

RB_TEST(Rules_Match_StalemateWarningAfterInningsWithoutProgress)
{
	MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	C.Rules.StalemateInnings = 4;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	for (int i = 0; i < 3; ++i)
	{
		RB_REQUIRE(ApplyShot(C, S, PassOutcome(1 - S.Game.Shooter), kNoFacts) == ErrorCode::Ok);
	}
	RB_CHECK(!S.StalemateWarning && S.InningsWithoutProgress == 3);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1 - S.Game.Shooter), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(S.StalemateWarning);
	// a pocketed ball in an inning is progress
	ShotFacts Made;
	Made.AnyObjectBallPocketed = true;
	RB_REQUIRE(ApplyShot(C, S, ContinueOutcome(S.Game.Shooter), Made) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(1 - S.Game.Shooter), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(!S.StalemateWarning && S.InningsWithoutProgress == 0);
}

RB_TEST(ARCH_DBL1_DoublesPassBackAfterPushOutGoesToPartner)
{
	MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	C.Doubles = true;
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0)); // team A breaks with member 0
	RB_CHECK(GetShotConstraints(C, S).Member == 0);
	RB_CHECK(S.TeamBreaker[0] == 0);

	ShotOutcome Break = PassOutcome(1);
	Break.NextPushOutAvailable = true;
	RB_REQUIRE(ApplyShot(C, S, Break, kNoFacts) == ErrorCode::Ok); // legal break, nothing made
	RB_CHECK(S.Game.Shooter == 1);
	RB_CHECK(GetShotConstraints(C, S).Member == 0); // team B, member 0 pushes out
	ShotDeclaration Push;
	Push.Kind = ShotKind::PushOut;
	RB_REQUIRE(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::Ok);
	RB_REQUIRE(ApplyShot(C, S, DecideOutcome(0, {Option::ShootFromPosition, Option::PassBack}), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(ApplyOption(C, S, Option::PassBack) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 1);
	RB_CHECK(GetShotConstraints(C, S).Member == 1); // the partner of the member who pushed out

	// A legal miss: team A's next inning is played by A's other member.
	RB_REQUIRE(ApplyShot(C, S, PassOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(GetShotConstraints(C, S).Member == 1);
	// Continuing does not change the member.
	RB_REQUIRE(ApplyShot(C, S, ContinueOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_CHECK(GetShotConstraints(C, S).Member == 1);

	// Team breakers alternate: rack 2 team B (first break of B: member 0), rack 3 team A member 1.
	RB_REQUIRE(WinRack(C, S, 0));
	RB_CHECK(S.Game.Shooter == 1 && GetShotConstraints(C, S).Member == 0);
	RB_REQUIRE(WinRack(C, S, 0));
	RB_CHECK(S.Game.Shooter == 0 && GetShotConstraints(C, S).Member == 1);
}

RB_TEST(ARCH_VIS1_BlackballFreeShotPlacementChoices)
{
	const MatchConfig C = MakeMatchConfig(Discipline::Blackball);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	const ShotConstraints BreakC = GetShotConstraints(C, S);
	RB_CHECK(BreakC.PlacementRegion == CueBallNext::InHandBaulk);
	RB_CHECK(BreakC.PlacementChoices == CueBallChoiceBit(CueBallNext::InHandBaulk));
	RB_CHECK(!BreakC.FreeShot);

	// A fouls: B gets a free shot, CB where it lies or in hand in baulk.
	ShotOutcome O = PassOutcome(1, CueBallNext::InPosition);
	O.AnyFoul = true;
	O.NextFreeShot = true;
	O.NextVisits = 1;
	ShotFacts F;
	CueBallEndsAt(F, -0.2, 0.1);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	const ShotConstraints Free = GetShotConstraints(C, S);
	RB_CHECK(Free.FreeShot);
	RB_CHECK(Free.PlacementChoices == (CueBallChoiceBit(CueBallNext::InPosition) | CueBallChoiceBit(CueBallNext::InHandBaulk)));
	RB_CHECK(Free.PlacementRegion == CueBallNext::InPosition);
	RB_CHECK(Free.VisitsRemaining == 1);
	const ShotDeclaration Normal;
	RB_CHECK(ValidateDeclaration(C, S, Normal, nullptr) == ErrorCode::Ok);                       // from where it lies
	const Vec2 InBaulkSpot{-0.9, 0.1};
	const Vec2 OutsideBaulk{-0.5, 0.1};
	RB_CHECK(ValidateDeclaration(C, S, Normal, &InBaulkSpot) == ErrorCode::Ok);                    // in baulk
	RB_CHECK(ValidateDeclaration(C, S, Normal, &OutsideBaulk) == ErrorCode::InvalidDeclaration);   // not in baulk

	// After a potted cue ball only "in hand in baulk" remains.
	ShotOutcome Scratch = PassOutcome(0, CueBallNext::InHandBaulk);
	Scratch.AnyFoul = true;
	Scratch.NextFreeShot = true;
	ShotFacts Potted;
	Pocket(Potted, kCueBallId, PocketId::FootLeft);
	RB_REQUIRE(ApplyShot(C, S, Scratch, Potted) == ErrorCode::Ok);
	const ShotConstraints InHand = GetShotConstraints(C, S);
	RB_CHECK(InHand.FreeShot);
	RB_CHECK(InHand.PlacementChoices == CueBallChoiceBit(CueBallNext::InHandBaulk));
	RB_CHECK(ValidateDeclaration(C, S, Normal, nullptr) == ErrorCode::InvalidDeclaration);
}

RB_TEST(Integ_Rules_M01_SetupRackGeneratesSeededRacks)
{
	// Needs the WP-2 lattice (GenerateRack).
	MatchConfig C = MakeMatchConfig(Discipline::NineBall, 3);
	MatchState S;
	StartMatch(C, S);
	LagResult Lag;
	Lag.Outcome = LagOutcome::FirstWins;
	RB_REQUIRE(ApplyLagResult(C, S, Lag) == ErrorCode::Ok);
	RB_REQUIRE(ChooseBreaker(C, S, 0) == ErrorCode::Ok);
	MatchState Copy = S;
	RackAssignment Rack;
	RB_REQUIRE(SetupRack(C, S, Rack) == ErrorCode::Ok);
	RB_CHECK(S.Phase == MatchPhase::AwaitShot && S.RackCounter == 1 && S.RackNumber == 1);
	RB_CHECK(S.Game.Balls[9].Kind == BallStatusKind::OnTable);
	RB_CHECK(S.Game.Balls[9].Position == C.Table.FootSpot);
	RB_CHECK(S.Game.Balls[10].Kind == BallStatusKind::NotUsed);
	RackAssignment Again;
	RB_REQUIRE(SetupRack(C, Copy, Again) == ErrorCode::Ok); // same seed and counter -> same rack
	for (int b = 1; b <= 9; ++b)
	{
		RB_CHECK(Rack.Position[b] == Again.Position[b]);
	}
	RB_REQUIRE(ApplyShot(C, S, RackWonOutcome(0), kNoFacts) == ErrorCode::Ok);
	RB_REQUIRE(SetupRack(C, S, Rack) == ErrorCode::Ok);
	RB_CHECK(S.Game.Shooter == 1 && S.RackCounter == 2);
}

RB_TEST(Integ_Rules_S12_Rerack14ExecutedByApplyShot)
{
	// Needs the WP-2 lattice (GenerateStraightPoolRack).
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	GameState& G = S.Game;
	for (int b = 1; b <= 13; ++b)
	{
		G.Balls[b].Kind = BallStatusKind::Pocketed;
	}
	Place(G, 14, 1.0, -0.4);
	Place(G, 15, 0.0, 0.3);
	Place(G, 0, -0.5, -0.3);
	G.IsBreakShot = false;
	G.CueBall = CueBallNext::InPosition;

	ShotFacts F; // A pockets the 14th ball; the 15th and the CB stay out of the rack area
	Pocket(F, 14, PocketId::FootRight);
	CueBallEndsAt(F, -0.9, -0.2);
	F.Balls[15].EndStatus = BallEndStatus::OnTable;
	F.Balls[15].FinalPosition = {0.0, 0.3};
	ShotOutcome O = ContinueOutcome(0);
	O.ScoreDelta[0] = 1;
	O.Rack = PlanRerack14({-0.9, -0.2}, 15, {0.0, 0.3}, C.Table, C.Rules.Tolerances);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	for (int b = 1; b <= 14; ++b)
	{
		RB_CHECK(G.Balls[b].Kind == BallStatusKind::OnTable);
		RB_CHECK(InterferesWithRack(G.Balls[b].Position, kR, C.Table));
		RB_CHECK(!(G.Balls[b].Position == C.Table.FootSpot)); // apex site empty
	}
	RB_CHECK(G.Balls[15].Position.x == 0.0 && G.Balls[15].Position.y == 0.3);
	RB_CHECK(G.Balls[0].Position.x == -0.9 && G.Balls[0].Position.y == -0.2);
	RB_CHECK(G.Shooter == 0 && G.CueBall == CueBallNext::InPosition && S.Phase == MatchPhase::AwaitShot);
	RB_CHECK(G.Players[0].Score == 1);
}

RB_TEST(Integ_Rules_S18_Rerack15ExecutedByApplyShot)
{
	// Needs the WP-2 lattice (GenerateStraightPoolRack).
	const MatchConfig C = MakeMatchConfig(Discipline::StraightPool);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	GameState& G = S.Game;
	for (int b = 1; b <= 13; ++b)
	{
		G.Balls[b].Kind = BallStatusKind::Pocketed;
	}
	Place(G, 14, 1.0, -0.4);
	Place(G, 15, 0.70, 0.05);
	Place(G, 0, -0.5, -0.3);
	G.IsBreakShot = false;
	S.Clock.ExtensionsUsed[0] = 1;

	ShotFacts F;
	Pocket(F, 14, PocketId::FootRight);
	CueBallEndsAt(F, 0.75, 0.0);
	ShotOutcome O = ContinueOutcome(0);
	O.ScoreDelta[0] = 1;
	O.Rack = PlanRerack14({0.75, 0.0}, 15, {0.70, 0.05}, C.Table, C.Rules.Tolerances);
	RB_REQUIRE(O.Rack.Kind == RackCommandKind::Rerack15);
	RB_REQUIRE(ApplyShot(C, S, O, F) == ErrorCode::Ok);
	for (int b = 1; b <= 15; ++b)
	{
		RB_CHECK(G.Balls[b].Kind == BallStatusKind::OnTable);
		RB_CHECK(InterferesWithRack(G.Balls[b].Position, kR, C.Table));
	}
	RB_CHECK(G.Balls[0].Kind == BallStatusKind::Pocketed);
	RB_CHECK(G.CueBall == CueBallNext::InHandAboveHeadString);
	RB_CHECK(S.Clock.ExtensionsUsed[0] == 0); // new 15-ball rack
	RB_CHECK(G.Shooter == 0 && !G.IsBreakShot);
}
