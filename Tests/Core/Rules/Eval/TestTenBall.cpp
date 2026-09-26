// Owner: WP-8 (rules facts & evaluation). rules.md 17.5 10-ball (T01-T10, T12, T13), WPA R 6.

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kTen = rr::MakeRulesConfig(rr::RulesPreset::Wpa10Ball);
	constexpr auto P0 = rb::PocketId::HeadRight;
	constexpr auto P1 = rb::PocketId::SideRight;
	constexpr auto P2 = rb::PocketId::FootRight;
	constexpr auto P3 = rb::PocketId::FootLeft;
	constexpr auto P4 = rb::PocketId::SideLeft;

	Shot BreakShot()
	{
		Shot S;
		S.OnRange(1, 10);
		S.InHand(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
		S.Cross(0.2, 0, rb::TableLine::HeadString, +1).Hit(0.5, 0, 1);
		return S;
	}

	rr::GameState BreakState(const Shot& S) { return BreakStateFor(rr::Discipline::TenBall, S); }
	rr::GameState Normal(const Shot& S) { return StateFor(rr::Discipline::TenBall, S); }

	// Only 9 and 10 left (T06, T13).
	Shot NineAndTenLeft()
	{
		Shot S;
		S.On({9, 10});
		for (int b = 1; b <= 8; ++b)
		{
			S.Presence(b, rb::BallPresence::Pocketed);
		}
		return S;
	}

	Shot OnlyTenLeft()
	{
		Shot S;
		S.On({10});
		for (int b = 1; b <= 9; ++b)
		{
			S.Presence(b, rb::BallPresence::Pocketed);
		}
		return S;
	}
}

RB_TEST(Rules_T01_LegalBreakWithBallContinuesPushOutAvailable)
{
	Shot S = BreakShot();
	S.Pot(1.0, 4, P2);
	for (int b = 2; b <= 6; ++b)
	{
		S.Rail(0.8 + 0.01 * b, b);
	}
	const rr::ShotOutcome O = Evaluate(kTen, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.NextPushOutAvailable);
}

RB_TEST(Rules_T02_TenOnBreakIsSpotted)
{
	Shot S = BreakShot();
	S.Pot(1.0, 10, P3);
	const rr::ShotOutcome O = Evaluate(kTen, BreakState(S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {10}));
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_T03_CalledBallMadeContinues)
{
	Shot S;
	S.OnRange(2, 10);
	S.Hit(0.3, 0, 2).Pot(0.9, 2, P1);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(2, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_T04_WrongfullyPocketedBallGivesOpponentChoice)
{
	Shot S;
	S.OnRange(2, 10);
	S.Hit(0.3, 0, 2).Pot(0.9, 2, P2);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(2, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::ShootFromPosition, rr::Option::PassBack}));
	RB_CHECK(SpotsAre(O, {})); // 2 stays down
}

RB_TEST(Rules_T05_TenMadeEarlyIsSpotted)
{
	Shot S;
	S.OnRange(2, 10);
	S.Hit(0.3, 0, 2).Pot(0.9, 2, P1).Hit(0.5, 2, 10).Pot(1.2, 10, P3);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(2, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {10}));
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_T06_TenBeforeLastBallIsNotAWin)
{
	Shot S = NineAndTenLeft();
	S.Hit(0.3, 0, 9).Hit(0.6, 9, 10).Pot(1.2, 10, P3).Rail(1.0, 9);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(10, P3), S);
	RB_CHECK(kTen.TenOnlyBall == rr::TenOnlyBallMoment::ShotStart);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.Next != rr::NextAction::RackWon);
	RB_CHECK(SpotsAre(O, {10}));
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_T07_TenAsLastBallWins)
{
	Shot S = OnlyTenLeft();
	S.Hit(0.3, 0, 10).Pot(0.9, 10, P3);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(10, P3), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));
}

RB_TEST(Rules_T08_LastTenInWrongPocketIsSpotted)
{
	Shot S = OnlyTenLeft();
	S.Hit(0.3, 0, 10).Pot(0.9, 10, P4);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(10, P3), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {10}));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::ShootFromPosition, rr::Option::PassBack}));
}

RB_TEST(Rules_T09_WrongBallFirstWithTenSpotsTen)
{
	Shot S;
	S.OnRange(3, 10);
	S.Hit(0.3, 0, 5).Hit(0.6, 5, 10).Pot(1.1, 10, P2);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(3, P1), S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(SpotsAre(O, {10}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_T10_ThirdConsecutiveFoulLosesRack)
{
	Shot S;
	S.OnRange(2, 10);
	S.Hit(0.3, 0, 2).Pot(0.9, 0, P0);
	rr::GameState G = Normal(S);
	G.Players[A].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kTen, G, Declare(2, P1), S);
	RB_CHECK(O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 6.10");
}

RB_TEST(Rules_T12_CalledMissNothingPocketedPasses)
{
	Shot S;
	S.OnRange(4, 10);
	S.Hit(0.3, 0, 4).Rail(0.7, 4);
	const rr::ShotOutcome O = Evaluate(kTen, Normal(S), Declare(4, P0), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_T13_TenAsLastBallAtPocketingWins)
{
	Shot S = NineAndTenLeft();
	S.Hit(0.3, 0, 9).Hit(0.7, 9, 10).Pot(1.20, 9, P1).Pot(1.55, 10, P3);
	rr::RulesConfig C = kTen;
	C.TenOnlyBall = rr::TenOnlyBallMoment::AtPocketing;
	const rr::ShotOutcome O = Evaluate(C, Normal(S), Declare(10, P3), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));

	Shot S2 = NineAndTenLeft(); // same shot under the ShotStart default: not a win
	S2.Hit(0.3, 0, 9).Hit(0.7, 9, 10).Pot(1.20, 9, P1).Pot(1.55, 10, P3);
	const rr::ShotOutcome O2 = Evaluate(kTen, Normal(S2), Declare(10, P3), S2);
	RB_CHECK(IsContinue(O2, A));
	RB_CHECK(SpotsAre(O2, {10}));
}
