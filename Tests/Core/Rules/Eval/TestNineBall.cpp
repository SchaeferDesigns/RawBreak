// Owner: WP-8 (rules facts & evaluation). rules.md 17.4 9-ball (N01-N18, N21, N23), WPA R 5 and Reg 16.

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kNine = rr::MakeRulesConfig(rr::RulesPreset::Wpa9Ball);
	constexpr auto P1 = rb::PocketId::SideRight;
	constexpr auto P2 = rb::PocketId::FootRight;
	constexpr auto P3 = rb::PocketId::FootLeft;

	// 9-ball break: balls 1..9, cue ball in hand above the head string, crosses it, hits FirstBall at t = 0.5.
	Shot BreakShot(int FirstBall = 1)
	{
		Shot S;
		S.OnRange(1, 9);
		S.InHand(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
		S.Cross(0.2, 0, rb::TableLine::HeadString, +1).Hit(0.5, 0, FirstBall);
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

	void CrossHeadString(Shot& S, std::initializer_list<int> Balls)
	{
		double T = 1.5;
		for (const int Ball : Balls)
		{
			S.Cross(T, Ball, rb::TableLine::HeadString, -1);
			T += 0.01;
		}
	}

	rr::GameState BreakState(const Shot& S) { return BreakStateFor(rr::Discipline::NineBall, S); }
	rr::GameState Normal(const Shot& S) { return StateFor(rr::Discipline::NineBall, S); }

	rr::GameState AfterBreak(const Shot& S)
	{
		rr::GameState G = Normal(S);
		G.PushOutAvailable = true;
		return G;
	}

	// N05 / N07 / N21: nothing pocketed, 6 balls to a rail, 2 balls across the head string.
	Shot ThreeBallRuleFailShot()
	{
		Shot S = BreakShot();
		RailsFor(S, {2, 3, 4, 5, 6, 7});
		CrossHeadString(S, {4, 5});
		return S;
	}
}

RB_TEST(Rules_N01_LegalBreakWithBallContinuesPushOutAvailable)
{
	Shot S = BreakShot();
	S.Pot(1.0, 3, P2);
	RailsFor(S, {2, 4, 5, 6, 7});
	CrossHeadString(S, {4, 5, 6});
	const rr::ShotOutcome O = Evaluate(kNine, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.NextPushOutAvailable);
}

RB_TEST(Rules_N02_NineOnLegalBreakWins)
{
	Shot S = BreakShot();
	S.Pot(1.0, 9, P3).Pot(1.1, 5, P2);
	CrossHeadString(S, {2, 3});
	const rr::ShotOutcome O = Evaluate(kNine, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));
}

RB_TEST(Rules_N03_BreakWrongBallFirstIsFoul)
{
	Shot S = BreakShot(2);
	S.Pot(1.0, 6, P2);
	const rr::ShotOutcome O = Evaluate(kNine, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(SpotsAre(O, {})); // 6 stays down
}

RB_TEST(Rules_N04_BreakTooFewRailsIsFoul)
{
	Shot S = BreakShot();
	RailsFor(S, {2, 3, 4});
	const rr::ShotOutcome O = Evaluate(kNine, BreakState(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::BreakTooFewRails));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_N05_ThreeBallRuleFailGivesChoice)
{
	Shot S = ThreeBallRuleFailShot();
	const rr::ShotOutcome O = Evaluate(kNine, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTableNoPushOut, rr::Option::HandBackPushOutAllowed}));
	RB_CHECK(!O.NextPushOutAvailable);
}

RB_TEST(Rules_N06_NineOnFailingBreakIsSpotted)
{
	Shot S = BreakShot();
	S.Pot(1.0, 9, P3);
	CrossHeadString(S, {4});
	const rr::ShotOutcome O = Evaluate(kNine, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {9}));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTableNoPushOut, rr::Option::HandBackPushOutAllowed}));
}

RB_TEST(Rules_N07_BallStoppingShortOfHeadStringDoesNotCount)
{
	Shot S = ThreeBallRuleFailShot();
	S.EndAt(7, {-0.6349, 0.1}); // never beyond the head string (no crossing event)
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.CountPocketedOrCrossedHeadString == 2);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), BreakState(S), {}, F);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTableNoPushOut, rr::Option::HandBackPushOutAllowed}));
}

RB_TEST(Rules_N08_PushOutSuspendsContactAndRailRules)
{
	Shot S;
	S.OnRange(1, 9);
	const rr::ShotOutcome O = Evaluate(kNine, AfterBreak(S), Kind(rr::ShotKind::PushOut), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::ShootFromPosition, rr::Option::PassBack}));
}

RB_TEST(Rules_N09_ScratchOnPushOutIsStandardFoul)
{
	Shot S;
	S.OnRange(1, 9);
	S.Pot(1.0, 0, P2);
	const rr::ShotOutcome O = Evaluate(kNine, AfterBreak(S), Kind(rr::ShotKind::PushOut), S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(O.FoulsAfter[A] == 1);
}

RB_TEST(Rules_N10_NineOnPushOutIsSpotted)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 9).Pot(1.0, 9, P3);
	const rr::ShotOutcome O = Evaluate(kNine, AfterBreak(S), Kind(rr::ShotKind::PushOut), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {9}));
	RB_CHECK(IsDecide(O, B));
}

RB_TEST(Rules_N11_BallOnPushOutStaysDown)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 4).Pot(1.0, 4, P3);
	const rr::ShotOutcome O = Evaluate(kNine, AfterBreak(S), Kind(rr::ShotKind::PushOut), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {}));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::ShootFromPosition, rr::Option::PassBack}));
}

RB_TEST(Rules_N12_ComboOnNineWins)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Hit(0.6, 1, 9).Pot(1.1, 9, P3);
	const rr::ShotOutcome O = Evaluate(kNine, Normal(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));
}

RB_TEST(Rules_N13_NineWithScratchIsSpotted)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Hit(0.6, 1, 9).Pot(1.1, 9, P3).Pot(1.4, 0, P1);
	const rr::ShotOutcome O = Evaluate(kNine, Normal(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(SpotsAre(O, {9}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_N14_TieSetWithLowestBallIsLegal)
{
	Shot S;
	S.On({3, 4, 5, 6, 7, 8, 9});
	S.Hit(0.3000, 0, 5).Hit(0.3002, 0, 3).Rail(0.7, 3);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.FirstContactTieSet.Size() == 2 && F.FirstContactTieSet[0] == 3 && F.FirstContactTieSet[1] == 5);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), Normal(S), {}, F);
	RB_CHECK(!O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(!O.AnyFoul);
}

RB_TEST(Rules_N15_ThirdConsecutiveFoulLosesRack)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1); // no rail
	rr::GameState G = Normal(S);
	G.Players[A].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kNine, G, {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(O.Enforced == rr::Foul::ThreeConsecutiveFouls);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(O.FoulsAfter[A] == 0);
}

RB_TEST(Rules_N16_LegalShotResetsFoulCounter)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Rail(0.7, 1);
	rr::GameState G = Normal(S);
	G.Players[A].ConsecutiveFouls = 2;
	G.Players[B].ConsecutiveFouls = 1;
	const rr::ShotOutcome O = Evaluate(kNine, G, {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.FoulsAfter[A] == 0);
	RB_CHECK(O.FoulsAfter[B] == 1);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_N17_NineOffTableIsSpotted)
{
	Shot S;
	S.OnRange(2, 9);
	S.Hit(0.3, 0, 2).Hit(0.6, 2, 9).Off(1.0, 9).Rail(1.1, 2);
	const rr::ShotOutcome O = Evaluate(kNine, Normal(S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(SpotsAre(O, {9}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_N18_AnyBallPocketedContinuesWithoutCalls)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Hit(0.6, 1, 6).Pot(1.2, 6, rb::PocketId::SideLeft);
	const rr::ShotOutcome O = Evaluate(kNine, Normal(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_N21_ThreeBallRuleOffPassesWithPushOut)
{
	Shot S = ThreeBallRuleFailShot();
	rr::RulesConfig C = kNine;
	C.ThreeBallRule = false;
	const rr::ShotOutcome O = Evaluate(C, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(O.NextPushOutAvailable);
}

RB_TEST(Rules_N23_ThreeBallRuleScopeReg16VersusLiteral)
{
	Shot S = BreakShot();
	S.Pot(1.0, 4, P2);
	RailsFor(S, {2, 3, 5, 6, 7});
	CrossHeadString(S, {6});
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.CountPocketedOrCrossedHeadString == 2);

	RB_CHECK(kNine.ThreeBallScope == rr::ThreeBallRuleScope::Reg16Combined);
	const rr::ShotOutcome Reg16 = rr::EvaluateShot(kNine, Table9Ft(), BreakState(S), {}, F);
	RB_CHECK(!Reg16.AnyFoul);
	RB_CHECK(IsDecide(Reg16, B));
	RB_CHECK(OptionsAre(Reg16, {rr::Option::AcceptTableNoPushOut, rr::Option::HandBackPushOutAllowed}));

	rr::RulesConfig Literal = kNine;
	Literal.ThreeBallScope = rr::ThreeBallRuleScope::OnlyIfNothingPocketed;
	const rr::ShotOutcome R53 = rr::EvaluateShot(Literal, Table9Ft(), BreakState(S), {}, F);
	RB_CHECK(IsContinue(R53, A));
	RB_CHECK(R53.NextPushOutAvailable);
}
