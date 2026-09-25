// Owner: WP-8 (rules facts & evaluation). rules.md 17.3 8-ball (E01-E34), WPA R 4.

#include "Rules/Facts/RulesTestUtil.h"

#include "rb/Rules/TableRules.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kEight = rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball);
	constexpr auto P0 = rb::PocketId::HeadRight;
	constexpr auto P1 = rb::PocketId::SideRight;
	constexpr auto P2 = rb::PocketId::FootRight;
	constexpr auto P3 = rb::PocketId::FootLeft;
	constexpr auto P4 = rb::PocketId::SideLeft;
	constexpr auto P5 = rb::PocketId::HeadLeft;

	// Break: rack of 15, cue ball in hand above the head string at (-0.9, 0), crosses the head string and hits
	// the apex ball 1 at t = 0.5.
	Shot BreakShot()
	{
		Shot S;
		S.Rack15();
		S.InHand(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
		S.Cross(0.2, 0, rb::TableLine::HeadString, +1).Hit(0.5, 0, 1, rbrules::TriangleSite(0));
		return S;
	}

	void RailsFor(Shot& S, std::initializer_list<int> Balls)
	{
		double T = 0.8;
		for (const int Ball : Balls)
		{
			S.Rail(T, Ball);
			T += 0.01;
		}
	}

	rr::GameState BreakState(const Shot& S) { return BreakStateFor(rr::Discipline::EightBall, S); }

	rr::GameState OpenState(const Shot& S, int Shooter = A) { return StateFor(rr::Discipline::EightBall, S, Shooter); }

	// Groups assigned: A = solids, B = stripes.
	rr::GameState Assigned(const Shot& S)
	{
		rr::GameState G = StateFor(rr::Discipline::EightBall, S);
		G.TableOpen = false;
		G.Players[A].Group = rr::BallGroup::Solids;
		G.Players[B].Group = rr::BallGroup::Stripes;
		return G;
	}

	// A has cleared the solids (1-7 pocketed earlier): A is on the 8.
	Shot SolidsClearedShot()
	{
		Shot S;
		S.On({8, 9, 10, 11, 12, 13, 14, 15});
		for (int b = 1; b <= 7; ++b)
		{
			S.Presence(b, rb::BallPresence::Pocketed);
		}
		return S;
	}

	// Open table with every stripe already off the table (E25, E26, E32, E34).
	Shot StripesGoneShot()
	{
		Shot S;
		S.OnRange(1, 8);
		for (int b = 9; b <= 15; ++b)
		{
			S.Presence(b, rb::BallPresence::Pocketed);
		}
		return S;
	}
}

// ---------------------------------------------------------------------------------------------
// Break (R 4.3)
// ---------------------------------------------------------------------------------------------

RB_TEST(Rules_E01_LegalBreakNothingPocketedPasses)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4, 5, 6});
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
}

RB_TEST(Rules_E02_IllegalBreakGivesIncomingPlayerThreeOptions)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4});
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::RerackDeciderBreaks, rr::Option::RerackOffenderBreaks}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InPosition);
}

RB_TEST(Rules_E03_IllegalBreakWithScratchAcceptGivesKitchen)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4});
	S.Pot(1.5, 0, P2);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::RerackDeciderBreaks, rr::Option::RerackOffenderBreaks}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InHandAboveHeadString);
}

RB_TEST(Rules_E04_BreakPocketingKeepsTableOpen)
{
	Shot S = BreakShot();
	S.Pot(1.0, 2, P2).Pot(1.2, 11, P3);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
}

RB_TEST(Rules_E05_EightOnBreakWithoutFoulBreakerChooses)
{
	Shot S = BreakShot();
	S.Pot(1.0, 8, P3).Pot(1.1, 4, P2);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsDecide(O, A));
	RB_CHECK(OptionsAre(O, {rr::Option::Spot8ContinueFromPosition, rr::Option::RerackDeciderBreaks}));
}

RB_TEST(Rules_E06_EightOnBreakWithScratchOpponentChooses)
{
	Shot S = BreakShot();
	S.Pot(1.0, 8, P3).Pot(1.4, 0, P0);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::Spot8BallInHandAboveHeadString, rr::Option::RerackDeciderBreaks}));
}

RB_TEST(Rules_E07_BreakScratchWithLegalCountGivesKitchenOnly)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4, 5, 6, 7});
	S.Pot(1.4, 0, P0);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAboveHeadString));
	RB_CHECK(O.Options.IsEmpty());
}

RB_TEST(Rules_E08_ObjectBallOffOnBreakStaysOut)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4, 5, 6});
	S.Off(1.0, 13);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(SpotsAre(O, {})); // 13 stays out of play
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::BallInHandAboveHeadString}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InPosition);
}

RB_TEST(Rules_E09_EightOffOnBreakIsSpotted)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4, 5, 6});
	S.Off(1.0, 8);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(SpotsAre(O, {8}));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::BallInHandAboveHeadString}));
}

// Needs WP-9's SpotBall (rules.md 4.3): the 8 goes on the foot spot when it is free, else behind the ball on it.
RB_TEST(Integ_Rules_E09_EightSpottedPerSpottingRule)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4, 5, 6});
	S.Off(1.0, 8);
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_REQUIRE(SpotsAre(O, {8}));

	rr::GameState After = BreakState(S); // the rack broke open: every object ball away from the long string
	for (int b = 1; b < rr::kRulesBallCount; ++b)
	{
		After.Balls[b].Position = DefaultPosition(b);
	}
	After.Balls[0].Position = {-0.3, 0.3};
	After.Balls[8].Kind = rr::BallStatusKind::OutOfPlay;
	const rb::Vec2 Free = rr::SpotBall(After, 8, Table9Ft(), rb::RulesTolerances{});
	RB_CHECK_NEAR(Free.x, 0.635, 1e-9);
	RB_CHECK_NEAR(Free.y, 0.0, 1e-9);

	After.Balls[1].Position = {0.635, 0.0}; // foot spot occupied -> touching it on the foot-rail side (G23 geometry)
	const rb::Vec2 Behind = rr::SpotBall(After, 8, Table9Ft(), rb::RulesTolerances{});
	RB_CHECK_NEAR(Behind.x, 0.692150, 1e-9);
	RB_CHECK_NEAR(Behind.y, 0.0, 1e-9);
}

// ---------------------------------------------------------------------------------------------
// Open table, groups, calls (R 4.4-4.6)
// ---------------------------------------------------------------------------------------------

RB_TEST(Rules_E10_CalledBallMadeByCombinationAssignsGroup)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 12).Hit(0.6, 3, 12).Pot(1.0, 3, P2);
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), Declare(3, P2), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::Solids);
}

RB_TEST(Rules_E11_EightFirstOnOpenTableIsFoul)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 8).Rail(0.7, 8);
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), Declare(3, P2), S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
}

RB_TEST(Rules_E12_UncalledBallDropsTurnPasses)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Rail(0.6, 3).Hit(0.9, 3, 11).Pot(1.3, 11, P4);
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), Declare(3, P2), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
	RB_CHECK(SpotsAre(O, {})); // 11 stays down
}

RB_TEST(Rules_E13_CalledBallWithScratchKeepsTableOpen)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Pot(0.9, 3, P2).Pot(1.2, 0, P3);
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), Declare(3, P2), S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
	RB_CHECK(SpotsAre(O, {})); // 3 stays down
}

RB_TEST(Rules_E14_OpponentGroupFirstIsFoul)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 10).Rail(0.7, 10);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(5, P1), S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_E15_CalledBallInWrongPocketPasses)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 5).Pot(0.9, 5, P4);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(5, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(SpotsAre(O, {})); // 5 stays down
}

RB_TEST(Rules_E16_ExtraOpponentBallStaysDown)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 5).Pot(0.9, 5, P1).Hit(0.5, 5, 12).Pot(1.1, 12, P3);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(5, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(SpotsAre(O, {}));
}

// ---------------------------------------------------------------------------------------------
// Loss of the rack (R 4.8, 4.10)
// ---------------------------------------------------------------------------------------------

RB_TEST(Rules_E17_EightWithLastGroupBallLoses)
{
	Shot S;
	S.On({7, 8, 9, 10, 11, 12, 13, 14, 15});
	for (int b = 1; b <= 6; ++b)
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 7).Pot(0.9, 7, P0).Hit(0.6, 0, 8).Pot(1.4, 8, P3);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(7, P0), S);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(b)");
}

RB_TEST(Rules_E18_EightWithScratchLoses)
{
	Shot S = SolidsClearedShot();
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P5).Pot(1.3, 0, P2);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(8, P5), S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(a)");
}

RB_TEST(Rules_E19_EightInUncalledPocketLoses)
{
	Shot S = SolidsClearedShot();
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P0);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(8, P5), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(c)");
}

RB_TEST(Rules_E20_EightInCalledPocketWins)
{
	Shot S = SolidsClearedShot();
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P5);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(8, P5), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));
}

RB_TEST(Rules_E21_EightOffTableLoses)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Hit(0.6, 3, 8).Off(1.0, 8);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(3, P2), S);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(d)");
}

RB_TEST(Rules_E22_ScratchOnTheEightWithoutPocketingIsStandardFoul)
{
	Shot S = SolidsClearedShot();
	S.Hit(0.3, 0, 8).Rail(0.7, 8).Pot(1.3, 0, P2);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(8, P5), S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_E23_SafetyPocketingOwnBallPasses)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Pot(0.9, 3, P2);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Kind(rr::ShotKind::Safety), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(SpotsAre(O, {})); // 3 stays down
}

RB_TEST(Rules_E24_EightOnSafetyLoses)
{
	Shot S = SolidsClearedShot();
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P5);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Kind(rr::ShotKind::Safety), S);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(c)");
}

// ---------------------------------------------------------------------------------------------
// R 4.4 (2025): 8 first on an open table once a group is completely off the table
// ---------------------------------------------------------------------------------------------

RB_TEST(Rules_E25_ClaimedClearedGroupEightWins)
{
	Shot S = StripesGoneShot();
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P3);
	rr::ShotDeclaration D = Declare(8, P3);
	D.ClaimedClearedGroup = rr::BallGroup::Stripes;
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), D, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));
}

RB_TEST(Rules_E26_ClaimedEightMissedTableStaysOpen)
{
	Shot S = StripesGoneShot();
	S.Hit(0.3, 0, 8).Rail(0.8, 8);
	rr::ShotDeclaration D = Declare(8, P3);
	D.ClaimedClearedGroup = rr::BallGroup::Stripes;
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), D, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
}

RB_TEST(Rules_E27_EightFirstAndPocketedOnOpenTableLoses)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P3);
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), Declare(3, P2), S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(a)");
}

RB_TEST(Rules_E28_NoThreeFoulRuleInEightBall)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 12).Rail(0.7, 12);
	rr::GameState G = Assigned(S);
	G.Players[A].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kEight, G, Declare(3, P2), S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(!O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_E29_NoRailAfterContactIsFoul)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(S), Declare(3, P2), S);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_E30_LastOpponentBallOffTableStaysOut)
{
	Shot S;
	S.On({1, 2, 3, 4, 5, 6, 7, 8, 13});
	for (const int b : {9, 10, 11, 12, 14, 15})
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 2).Hit(0.6, 2, 13).Off(1.0, 13).Rail(1.1, 2);
	const rr::ShotFacts F = S.Facts();
	const rr::GameState G = Assigned(S);
	const rr::ShotOutcome O = rr::EvaluateShot(kEight, Table9Ft(), G, Declare(2, P1), F);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(SpotsAre(O, {})); // 13 stays out
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));

	// Next shot (B, stripes): 13 is out of play, so B's group is cleared and B is on the 8.
	rr::GameState Next = G;
	Next.Shooter = B;
	Next.Balls[13].Kind = rr::BallStatusKind::OutOfPlay;
	RB_CHECK(rr::GroupCleared(Next, rr::BallGroup::Stripes));
	RB_CHECK(rr::LegalFirstContactMask(kEight, Next, Declare(8, P3)) == Bit(8));
}

RB_TEST(Rules_E31_KitchenFirstContactAboveHeadStringAfterBreakFoul)
{
	Shot S;
	S.OnRange(1, 15);
	S.On(4, {-0.90, 0.10});
	S.InHand(rb::CueBallInHand::AboveHeadString, {-1.0, -0.2});
	S.Hit(0.3, 0, 4, {-0.90, 0.10}).Rail(0.6, 4, rb::CushionId::LeftHead);
	rr::GameState G = OpenState(S, B);
	G.CueBall = rr::CueBallNext::InHandAboveHeadString;
	const rr::ShotOutcome O = Evaluate(kEight, G, Declare(4, P5), S);
	RB_CHECK(O.Detected.Has(rr::Foul::BadPlayAboveHeadStringP1) || O.Detected.Has(rr::Foul::BadPlayAboveHeadStringP2));
	RB_CHECK(IsPass(O, A, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_E32_OpenTableEightFirstAfterGroupCleared)
{
	Shot S = StripesGoneShot();
	S.Hit(0.3, 0, 8).Rail(0.8, 8);
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), Declare(3, P2), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
}

RB_TEST(Rules_E33_BreakNeverLeavingKitchenIsFoulAndIllegalBreak)
{
	Shot S;
	S.Rack15();
	S.InHand(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
	S.EndAt(0, {-0.7, 0.0}); // rolls slowly, stops above the head string, touches no ball
	const rr::ShotOutcome O = Evaluate(kEight, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::BadPlayAboveHeadStringP2));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::RerackDeciderBreaks, rr::Option::RerackOffenderBreaks}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InHandAboveHeadString);
}

RB_TEST(Rules_E34_UnclaimedEightPocketedLoses)
{
	Shot S = StripesGoneShot();
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P3);
	const rr::ShotOutcome O = Evaluate(kEight, OpenState(S), Declare(3, P2), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(b)");
}
