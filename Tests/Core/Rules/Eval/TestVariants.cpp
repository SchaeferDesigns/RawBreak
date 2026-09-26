// Owner: WP-8 (rules facts & evaluation). rules.md 17.7 variants (V01-V09): APA 8-ball (12.3), WPA Blackball
// (12.4), FoulScope = CueBallOnly (12.2), LastPocketRule (12.6); plus the RulesConfig presets (12.1-12.6).

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kWpa8 = rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball);
	const rr::RulesConfig kApa = rr::MakeRulesConfig(rr::RulesPreset::Apa8Ball);
	const rr::RulesConfig kBlack = rr::MakeRulesConfig(rr::RulesPreset::WpaBlackball);
	constexpr auto P0 = rb::PocketId::HeadRight;
	constexpr auto P2 = rb::PocketId::FootRight;
	constexpr auto P3 = rb::PocketId::FootLeft;
	constexpr auto P5 = rb::PocketId::HeadLeft;

	// 8-ball / Blackball break from a real triangle: apex ball 1 on the foot spot is hit first.
	Shot RackBreak(rb::CueBallInHand Region, rb::Vec2 CueBall)
	{
		Shot S;
		S.Rack15();
		S.InHand(Region, CueBall);
		S.Cross(0.2, 0, rb::TableLine::HeadString, +1).Hit(0.5, 0, 1, TriangleSite(0));
		return S;
	}

	rr::GameState SolidsCleared(const Shot& S)
	{
		rr::GameState G = StateFor(rr::Discipline::EightBall, S);
		G.TableOpen = false;
		G.Players[A].Group = rr::BallGroup::Solids;
		G.Players[B].Group = rr::BallGroup::Stripes;
		return G;
	}

	Shot SolidsGoneShot()
	{
		Shot S;
		S.On({8, 9, 10, 11, 12, 13, 14, 15});
		for (int b = 1; b <= 7; ++b)
		{
			S.Presence(b, rb::BallPresence::Pocketed);
		}
		return S;
	}
}

RB_TEST(Rules_V01_ApaEightOnBreakWins)
{
	Shot S = RackBreak(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
	S.Pot(1.0, 8, P3);
	const rr::ShotOutcome O = Evaluate(kApa, BreakStateFor(rr::Discipline::EightBall, S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));
}

RB_TEST(Rules_V02_ApaEightOnBreakWithScratchLoses)
{
	Shot S = RackBreak(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
	S.Pot(1.0, 8, P3).Pot(1.2, 0, P2);
	const rr::ShotOutcome O = Evaluate(kApa, BreakStateFor(rr::Discipline::EightBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsRackWon(O, B));
}

RB_TEST(Rules_V03_ApaScratchWhileShootingEightLoses)
{
	Shot S = SolidsGoneShot();
	S.Hit(0.3, 0, 8).Rail(0.7, 8).Pot(1.2, 0, P2);
	const rr::ShotOutcome Apa = Evaluate(kApa, SolidsCleared(S), Declare(8, P5), S);
	RB_CHECK(IsRackWon(Apa, B));

	Shot W = SolidsGoneShot();
	W.Hit(0.3, 0, 8).Rail(0.7, 8).Pot(1.2, 0, P2);
	const rr::ShotOutcome Wpa = Evaluate(kWpa8, SolidsCleared(W), Declare(8, P5), W);
	RB_CHECK(Wpa.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(Wpa, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_V04_ApaBreakWithOneGroupAssignsIt)
{
	Shot S = RackBreak(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
	S.Pot(1.0, 2, P2).Pot(1.3, 5, P3);
	const rr::ShotOutcome O = Evaluate(kApa, BreakStateFor(rr::Discipline::EightBall, S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::Solids);

	Shot W = RackBreak(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0}); // WPA: balls made on the break never assign
	W.Pot(1.0, 2, P2).Pot(1.3, 5, P3);
	const rr::ShotOutcome Wpa = Evaluate(kWpa8, BreakStateFor(rr::Discipline::EightBall, W), {}, W);
	RB_CHECK(Wpa.AssignShooterGroup == rr::BallGroup::None);
}

RB_TEST(Rules_V05_BlackballBreakNeedsTwoBallsAcrossCenter)
{
	for (int Crossing = 1; Crossing <= 2; ++Crossing)
	{
		Shot S = RackBreak(rb::CueBallInHand::Baulk, {-0.9, 0.0});
		for (int k = 0; k < Crossing; ++k)
		{
			S.Cross(1.0 + 0.1 * k, 2 + k, rb::TableLine::CenterString, -1);
		}
		const rr::ShotOutcome O = Evaluate(kBlack, BreakStateFor(rr::Discipline::Blackball, S), {}, S);
		if (Crossing == 1)
		{
			RB_CHECK(O.Detected.Has(rr::Foul::BlackballBreakFoul));
			RB_CHECK(O.Next == rr::NextAction::Pass && O.NextShooter == B);
			RB_CHECK(O.NextFreeShot);
		}
		else
		{
			RB_CHECK(!O.AnyFoul);
			RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
		}
	}
}

RB_TEST(Rules_V06_BlackballBlackOnBreakRerackScratchIgnored)
{
	Shot S = RackBreak(rb::CueBallInHand::Baulk, {-0.9, 0.0});
	S.Pot(1.0, 8, P3).Pot(1.2, 0, P2);
	const rr::ShotOutcome O = Evaluate(kBlack, BreakStateFor(rr::Discipline::Blackball, S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.Next == rr::NextAction::RerackAndBreak);
	RB_CHECK(O.NextShooter == A);
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::Rerack15);
}

RB_TEST(Rules_V07_BlackballFreeShotSuspendsWrongBallFirst)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 12).Rail(0.7, 12);
	rr::GameState G = StateFor(rr::Discipline::Blackball, S);
	G.TableOpen = false;
	G.Players[A].Group = rr::BallGroup::Solids; // reds
	G.Players[B].Group = rr::BallGroup::Stripes;
	G.FreeShot = true;
	const rr::ShotOutcome O = Evaluate(kBlack, G, {}, S);
	RB_CHECK(!O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(!O.AnyFoul);

	Shot S2; // the same shot without the free shot is a foul
	S2.OnRange(1, 15);
	S2.Hit(0.3, 0, 12).Rail(0.7, 12);
	G.FreeShot = false;
	RB_CHECK(Evaluate(kBlack, G, {}, S2).Detected.Has(rr::Foul::WrongBallFirst));
}

RB_TEST(Rules_V08_CueBallOnlyScopeIgnoresObjectBallTouches)
{
	rr::RulesConfig C = rr::MakeRulesConfig(rr::RulesPreset::Wpa9Ball);
	C.Fouls = rr::FoulScope::CueBallOnly;
	C.Input = rr::InputMode::Sim;
	rb::NonTipContact Touch;
	Touch.Source = rb::NonTipSource::BridgeHand;
	Touch.Time = -0.8;

	Shot S;
	S.OnRange(1, 9);
	Touch.Ball = 7;
	S.Record.Stroke.NonTipContacts.PushBack(Touch);
	S.Hit(0.3, 0, 1).Rail(0.7, 1);
	const rr::ShotOutcome O = Evaluate(C, StateFor(rr::Discipline::NineBall, S), {}, S);
	RB_CHECK(!O.AnyFoul);

	Shot S2;
	S2.OnRange(1, 9);
	Touch.Ball = 0;
	S2.Record.Stroke.NonTipContacts.PushBack(Touch);
	S2.Hit(0.3, 0, 1).Rail(0.7, 1);
	const rr::ShotOutcome O2 = Evaluate(C, StateFor(rr::Discipline::NineBall, S2), {}, S2);
	RB_CHECK(O2.Detected.Has(rr::Foul::TouchedBall));
}

RB_TEST(Rules_V09_LastPocketRuleVariant)
{
	Shot S = SolidsGoneShot();
	S.Hit(0.3, 0, 8).Pot(0.9, 8, P0);
	rr::GameState G = SolidsCleared(S);
	G.LastGroupBallPocket[static_cast<int>(rr::BallGroup::Solids)] = P2; // A's last solid went into P2
	rr::RulesConfig Last = kWpa8;
	Last.LastPocketRule = true;
	const rr::ShotFacts F = S.Facts();
	const rr::ShotOutcome Variant = rr::EvaluateShot(Last, Table9Ft(), G, Declare(8, P0), F);
	RB_CHECK(IsRackWon(Variant, B));
	const rr::ShotOutcome Wpa = rr::EvaluateShot(kWpa8, Table9Ft(), G, Declare(8, P0), F);
	RB_CHECK(IsRackWon(Wpa, A));

	// The bar/house preset has the rule on.
	const rr::RulesConfig House = rr::MakeRulesConfig(rr::RulesPreset::BarHouse8Ball);
	RB_CHECK(House.LastPocketRule);
	RB_CHECK(IsRackWon(rr::EvaluateShot(House, Table9Ft(), G, Declare(8, P0), F), B));
}

RB_TEST(Rules_Config_PresetsFollowSpecDefaults)
{
	const rr::RulesConfig Wpa9 = rr::MakeRulesConfig(rr::RulesPreset::Wpa9Ball);
	RB_CHECK(Wpa9.Fouls == rr::FoulScope::AllBall);
	RB_CHECK(Wpa9.Scoop == rr::ScoopPolicy::WpaMiscue);
	RB_CHECK(Wpa9.ThreeFoulRule && Wpa9.ThreeBallRule);
	RB_CHECK(Wpa9.ThreeBallScope == rr::ThreeBallRuleScope::Reg16Combined);
	RB_CHECK(Wpa9.NineBallRack == rr::NineBallRackRule::NineOnSpot);
	RB_CHECK(!kWpa8.ThreeFoulRule);
	RB_CHECK(kWpa8.EightOnBreak == rr::EightOnBreakRule::SpotOrRebreakOption);
	RB_CHECK(kWpa8.EightOnBreakWithFoul == rr::EightOnBreakWithFoulRule::OpponentOption);
	RB_CHECK(kWpa8.OpenTableEightFirstFoul && kWpa8.OpenTableEightGroupGoneException);
	RB_CHECK(kWpa8.FoulCueBall == rr::FoulCueBallMode::InHandAnywhere);
	RB_CHECK(rr::MakeRulesConfig(rr::RulesPreset::Wpa10Ball).TenOnlyBall == rr::TenOnlyBallMoment::ShotStart);
	RB_CHECK(rr::MakeRulesConfig(rr::RulesPreset::Wpa14_1).TargetPoints == 100);
	const rr::RulesConfig Legacy = rr::MakeRulesConfig(rr::RulesPreset::WpaLegacy9Ball);
	RB_CHECK(Legacy.NineBallRack == rr::NineBallRackRule::OneOnSpot && Legacy.ThreeBallRuleReach);
	RB_CHECK(kApa.Calls == rr::CallMode::EightOnly && kApa.Fouls == rr::FoulScope::CueBallOnly && kApa.Scoop == rr::ScoopPolicy::Foul);
	RB_CHECK(kApa.EightOnBreak == rr::EightOnBreakRule::Win && kApa.EightOnBreakWithFoul == rr::EightOnBreakWithFoulRule::Lose);
	RB_CHECK(kApa.SpotJumpedObjectBalls && kApa.BreakAssignsGroup == rr::BreakAssignsGroupRule::IfOnlyOneGroupPocketed);
	RB_CHECK(kBlack.JumpShots == rr::JumpShotRule::Illegal && kBlack.Calls == rr::CallMode::None);
	RB_CHECK(rr::DisciplineOf(rr::RulesPreset::WpaBlackball) == rr::Discipline::Blackball);
	RB_CHECK(rr::DisciplineOf(rr::RulesPreset::Apa8Ball) == rr::Discipline::EightBall);
	const rr::RulesConfig House = rr::MakeRulesConfig(rr::RulesPreset::BarHouse8Ball);
	RB_CHECK(!House.RailAfterContactRequired && House.FoulCueBall == rr::FoulCueBallMode::InHandBehindHeadString);
}
