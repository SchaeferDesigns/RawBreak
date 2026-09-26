// Owner: WP-8 (rules facts & evaluation). rules.md 17.6 14.1 continuous (S01-S11, S19, S20, S22, S24, S25),
// WPA R 7. S19 needs WP-9's PlanRerack15AfterFifteenthPocketed (integration dependency, architecture.md 17 WP-8).

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kStraight = rr::MakeRulesConfig(rr::RulesPreset::Wpa14_1);
	constexpr auto P0 = rb::PocketId::HeadRight;
	constexpr auto P1 = rb::PocketId::SideRight;
	constexpr auto P2 = rb::PocketId::FootRight;
	constexpr auto P3 = rb::PocketId::FootLeft;

	// Opening break: full rack, cue ball in hand above the head string, first rack contact (ball 1) at t = 0.5.
	Shot OpeningBreak()
	{
		Shot S;
		S.Rack15();
		S.InHand(rb::CueBallInHand::AboveHeadString, {-0.9, 0.1});
		S.Cross(0.2, 0, rb::TableLine::HeadString, +1).Hit(0.5, 0, 1, rbrules::TriangleSite(0));
		return S;
	}

	rr::GameState BreakState(const Shot& S) { return BreakStateFor(rr::Discipline::StraightPool, S); }
	rr::GameState Normal(const Shot& S) { return StateFor(rr::Discipline::StraightPool, S); }
}

RB_TEST(Rules_S01_OpeningBreakCalledBallMadeScores)
{
	Shot S = OpeningBreak();
	S.Pot(1.2, 5, P2);
	const rr::ShotOutcome O = Evaluate(kStraight, BreakState(S), Declare(5, P2), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 1);
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_S02_OpeningBreakRailRequirementUncalledBallSpotted)
{
	Shot S = OpeningBreak();
	S.Rail(0.9, 0).Rail(0.8, 3).Rail(0.85, 11).Pot(1.1, 7, P3);
	const rr::ShotOutcome O = Evaluate(kStraight, BreakState(S), {}, S); // no call
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 0);
	RB_CHECK(SpotsAre(O, {7}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_S03_OpeningBreakFailureIsBreakingFoul)
{
	Shot S = OpeningBreak();
	S.Rail(0.9, 0).Rail(0.8, 3);
	rr::GameState G = BreakState(S);
	G.Players[A].ConsecutiveFouls = 1;
	const rr::ShotOutcome O = Evaluate(kStraight, G, Declare(5, P2), S);
	RB_CHECK(O.Detected.Has(rr::Foul::BreakingFoul141));
	RB_CHECK(O.Enforced == rr::Foul::BreakingFoul141);
	RB_CHECK(O.ScoreDelta[A] == -2);
	RB_CHECK(O.FoulsAfter[A] == 1); // not counted (R 7.11)
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::RequireRebreak}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InPosition);
}

RB_TEST(Rules_S04_BreakingFoulWithScratchIsMinusTwoOnly)
{
	Shot S = OpeningBreak();
	S.Rail(0.8, 3).Pot(1.3, 0, P0);
	const rr::ShotOutcome O = Evaluate(kStraight, BreakState(S), Declare(5, P2), S);
	RB_CHECK(O.Detected.Has(rr::Foul::BreakingFoul141));
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(O.Enforced == rr::Foul::BreakingFoul141);
	RB_CHECK(O.ScoreDelta[A] == -2);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::RequireRebreak}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InHandAboveHeadString);
}

RB_TEST(Rules_S05_LegalOpeningBreakWithScratchIsStandardFoul)
{
	Shot S = OpeningBreak();
	S.Rail(0.8, 3).Rail(0.85, 11).Pot(1.3, 0, P0);
	const rr::ShotOutcome O = Evaluate(kStraight, BreakState(S), Declare(5, P2), S);
	RB_CHECK(!O.Detected.Has(rr::Foul::BreakingFoul141));
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(O.ScoreDelta[A] == -1);
	RB_CHECK(O.FoulsAfter[A] == 1);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAboveHeadString));
}

RB_TEST(Rules_S06_ExtraBallsOnScoringShotScore)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Pot(0.8, 3, P1).Hit(0.5, 0, 7).Pot(1.0, 7, P2).Hit(0.6, 7, 9).Pot(1.1, 9, P3);
	const rr::ShotOutcome O = Evaluate(kStraight, Normal(S), Declare(3, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 3);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(SpotsAre(O, {}));
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::None);
}

RB_TEST(Rules_S07_MissSpotsPocketedBalls)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Hit(0.6, 3, 7).Pot(1.0, 7, P2).Rail(1.1, 3);
	const rr::ShotOutcome O = Evaluate(kStraight, Normal(S), Declare(3, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 0);
	RB_CHECK(SpotsAre(O, {7}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_S08_SafetySpotsBallAndResetsCounter)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 7).Pot(1.0, 7, P2);
	rr::GameState G = Normal(S);
	G.Players[A].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kStraight, G, Kind(rr::ShotKind::Safety), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {7}));
	RB_CHECK(O.ScoreDelta[A] == 0);
	RB_CHECK(O.FoulsAfter[A] == 0);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_S09_NoRailIsMinusOneCueBallStays)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3);
	const rr::ShotOutcome O = Evaluate(kStraight, Normal(S), Declare(3, P1), S);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(O.ScoreDelta[A] == -1);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_S10_ThirdFoulIsMinusSixteenAndOpeningBreak)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3);
	rr::GameState G = Normal(S);
	G.Players[A].ConsecutiveFouls = 2;
	G.Players[A].Score = 20;
	const rr::ShotOutcome O = Evaluate(kStraight, G, Declare(3, P1), S);
	RB_CHECK(O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(O.ScoreDelta[A] == -16);
	RB_CHECK(G.Players[A].Score + O.ScoreDelta[A] == 4);
	RB_CHECK(O.FoulsAfter[A] == 0);
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::Rerack15);
	RB_CHECK(O.Next == rr::NextAction::RerackAndBreak);
	RB_CHECK(O.NextShooter == A);
	RB_CHECK(O.NextCueBall == rr::CueBallNext::InHandAboveHeadString);
}

RB_TEST(Rules_S11_ScoreCanGoNegative)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Pot(1.0, 0, P2);
	const rr::ShotOutcome O = Evaluate(kStraight, Normal(S), Declare(3, P1), S);
	RB_CHECK(O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == -1); // 0 -> -1
}

// Integration: needs WP-9's PlanRerack15AfterFifteenthPocketed (rules.md 9.5, R 7.8(a)).
RB_TEST(Integ_Rules_S19_FourteenthAndFifteenthTogetherRerackAll)
{
	Shot S;
	S.On({4, 11});
	for (const int b : {1, 2, 3, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15})
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 4).Pot(0.9, 4, P1).Hit(0.6, 4, 11).Pot(1.2, 11, P3);
	S.EndAt(0, {-0.2, 0.3});
	const rr::ShotOutcome O = Evaluate(kStraight, Normal(S), Declare(4, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 2);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::Rerack15);
	RB_CHECK(O.Rack.CueBallPlacement == rr::PlacementCommand::Keep);
}

RB_TEST(Rules_S20_ReachingTargetWinsMatchImmediately)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Pot(0.8, 3, P1).Hit(0.5, 3, 9).Pot(1.1, 9, P3);
	rr::GameState G = Normal(S);
	G.Players[A].Score = 98;
	const rr::ShotOutcome O = Evaluate(kStraight, G, Declare(3, P1), S);
	RB_CHECK(kStraight.TargetPoints == 100);
	RB_CHECK(O.ScoreDelta[A] == 2);
	RB_CHECK(O.Next == rr::NextAction::MatchWon);
	RB_CHECK(O.Winner == A);
}

RB_TEST(Rules_S22_AnyBallMayBeHitFirst)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 12).Hit(0.6, 12, 2).Pot(1.0, 2, P1);
	const rr::ShotOutcome O = Evaluate(kStraight, Normal(S), Declare(2, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(!O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(O.ScoreDelta[A] == 1);
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_S24_CalledBallOnOpeningBreakWithScratchIsStandardFoul)
{
	Shot S = OpeningBreak();
	S.Pot(1.0, 5, P2).Rail(0.8, 3).Pot(1.3, 0, P0);
	const rr::ShotOutcome O = Evaluate(kStraight, BreakState(S), Declare(5, P2), S);
	RB_CHECK(!O.Detected.Has(rr::Foul::BreakingFoul141));
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(O.ScoreDelta[A] == -1);
	RB_CHECK(SpotsAre(O, {5}));
	RB_CHECK(O.FoulsAfter[A] == 1);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAboveHeadString));
}

RB_TEST(Rules_S25_BreakingFoulNeverBecomesThirdFoul)
{
	Shot S = OpeningBreak();
	S.Rail(0.8, 3).Pot(1.3, 0, P0);
	rr::GameState G = BreakState(S);
	G.Players[A].ConsecutiveFouls = 2; // carried over the stalemate re-lag
	const rr::ShotOutcome O = Evaluate(kStraight, G, Declare(5, P2), S);
	RB_CHECK(O.Enforced == rr::Foul::BreakingFoul141);
	RB_CHECK(!O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(O.ScoreDelta[A] == -2);
	RB_CHECK(O.FoulsAfter[A] == 2);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::RequireRebreak}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InHandAboveHeadString);
}
