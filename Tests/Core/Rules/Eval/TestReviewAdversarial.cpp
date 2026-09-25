// Owner: WP-8 (rules facts & evaluation). Adversarial review tests for EvaluateShot: interactions the spec tests of
// rules.md 17 do not pin down (fouls together with the game ball, break fouls with scratches, foul counters across
// innings, push-out edge cases, 14.1 bookkeeping, Blackball spotting and variant switches).

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kEight = rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball);
	const rr::RulesConfig kNine = rr::MakeRulesConfig(rr::RulesPreset::Wpa9Ball);
	const rr::RulesConfig kTen = rr::MakeRulesConfig(rr::RulesPreset::Wpa10Ball);
	const rr::RulesConfig kStraight = rr::MakeRulesConfig(rr::RulesPreset::Wpa14_1);
	const rr::RulesConfig kBlack = rr::MakeRulesConfig(rr::RulesPreset::WpaBlackball);
	constexpr auto P0 = rb::PocketId::HeadRight;
	constexpr auto P1 = rb::PocketId::SideRight;
	constexpr auto P2 = rb::PocketId::FootRight;
	constexpr auto P3 = rb::PocketId::FootLeft;
	constexpr auto P5 = rb::PocketId::HeadLeft;

	// Break with the cue ball in hand at (-0.9, 0), crossing the head string and hitting FirstBall at t = 0.5.
	Shot BreakFrom(int FirstBall, bool FullRack)
	{
		Shot S;
		if (FullRack)
		{
			S.Rack15();
		}
		S.InHand(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
		S.Cross(0.2, 0, rb::TableLine::HeadString, +1).Hit(0.5, 0, FirstBall, FullRack ? TriangleSite(FirstBall - 1) : rb::Vec2{0.6, 0.0});
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

	rr::GameState Assigned(rr::Discipline Game, const Shot& S)
	{
		rr::GameState G = StateFor(Game, S);
		G.TableOpen = false;
		G.Players[A].Group = rr::BallGroup::Solids;
		G.Players[B].Group = rr::BallGroup::Stripes;
		return G;
	}

	// Open 8-ball table with every stripe already off the table.
	Shot StripesGone()
	{
		Shot S;
		S.OnRange(1, 8);
		for (int b = 9; b <= 15; ++b)
		{
			S.Presence(b, rb::BallPresence::Pocketed);
		}
		return S;
	}

	// A (solids) has cleared the group: on the 8.
	Shot SolidsGone()
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

// ---------------------------------------------------------------------------------------------
// 8-ball
// ---------------------------------------------------------------------------------------------

// R 4.3(g)+(h): the 8 off the table AND a scratch on a break with the legal count -> 8 spotted, only BIH above HS.
RB_TEST(Rules_Review_EightBreakEightOffPlusScratchSpotsEightKitchenOnly)
{
	Shot S = BreakFrom(1, true);
	RailsFor(S, {2, 3, 4, 5, 6});
	S.Off(1.0, 8).Pot(1.4, 0, P0);
	const rr::ShotOutcome O = Evaluate(kEight, BreakStateFor(rr::Discipline::EightBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable) && O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(O.Enforced == rr::Foul::CueBallScratch);
	RB_CHECK(SpotsAre(O, {8}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAboveHeadString));
	RB_CHECK(O.Options.IsEmpty());
}

// R 4.3(d) combined with an object ball off the table: the off-table ball counts toward the rail count (R 2.7) but the
// count still fails; accepting the table comes with BIH above HS (INTERPRETATION of 6.2); 13 stays out.
RB_TEST(Rules_Review_EightIllegalBreakWithBallOffAcceptGivesKitchen)
{
	Shot S = BreakFrom(1, true);
	RailsFor(S, {2, 3});
	S.Off(1.0, 13);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.NumObjectBallsDrivenToRail == 3);
	const rr::ShotOutcome O = rr::EvaluateShot(kEight, Table9Ft(), BreakStateFor(rr::Discipline::EightBall, S), {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::AcceptTable, rr::Option::RerackDeciderBreaks, rr::Option::RerackOffenderBreaks}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InHandAboveHeadString);
	RB_CHECK(SpotsAre(O, {}));
}

// R 4.4 (2025): the 8 first is legal once a group is off the table; if it drives the CALLED ball in, the shooter
// gets that ball's group.
RB_TEST(Rules_Review_EightFirstComboMakesCalledBallAssignsGroup)
{
	Shot S = StripesGone();
	S.Hit(0.3, 0, 8).Hit(0.6, 8, 3).Pot(1.0, 3, P2).Rail(1.2, 8);
	const rr::ShotOutcome O = Evaluate(kEight, StateFor(rr::Discipline::EightBall, S), Declare(3, P2), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::Solids);
}

// Calling the 8 on an open table claims the cleared group (auto-claim): the shooter is on the 8, a solid first is 3.2.
RB_TEST(Rules_Review_CalledEightOnOpenTableMakesOtherBallFirstAFoul)
{
	Shot S = StripesGone();
	S.Hit(0.3, 0, 3).Rail(0.8, 3);
	const rr::ShotOutcome O = Evaluate(kEight, StateFor(rr::Discipline::EightBall, S), Declare(8, P3), S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(rr::LegalFirstContactMask(kEight, StateFor(rr::Discipline::EightBall, S), Declare(8, P3)) == Bit(8));
}

// On the 8: the 8 made in the called pocket after hitting an opponent ball first is a foul -> loss (R 4.8(a)).
RB_TEST(Rules_Review_EightMadeAfterWrongBallFirstLoses)
{
	Shot S = SolidsGone();
	S.Hit(0.3, 0, 10).Hit(0.6, 10, 8).Pot(1.0, 8, P5);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(rr::Discipline::EightBall, S), Declare(8, P5), S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(a)");
}

// 8-ball on the 8 via the last ball of the OPPONENT's group being cleared this shot does not help: groups are judged
// at shot start (pitfall 1). A = solids with 7 left; the 8 made with 7 on the same shot is a loss even if called.
RB_TEST(Rules_Review_EightAndLastBallCalledEightStillLoses)
{
	Shot S;
	S.On({7, 8, 9, 10});
	for (const int b : {1, 2, 3, 4, 5, 6, 11, 12, 13, 14, 15})
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 7).Pot(0.9, 7, P0).Hit(0.5, 7, 8).Pot(1.2, 8, P3);
	const rr::ShotOutcome O = Evaluate(kEight, Assigned(rr::Discipline::EightBall, S), Declare(8, P3), S);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(std::string_view(O.RuleRef) == "R 4.8(b)");
}

// FoulCueBall FreeShotPlusVisit (12.1/12.5): a foul gives the incoming player a free shot + one extra visit, and the
// free shot suspends 3.2 in the 8-ball evaluator too.
RB_TEST(Rules_Review_EightFreeShotVariantSuspendsWrongBallFirst)
{
	rr::RulesConfig C = kEight;
	C.FoulCueBall = rr::FoulCueBallMode::FreeShotPlusVisit;

	Shot Foul;
	Foul.OnRange(1, 15);
	Foul.Hit(0.3, 0, 12).Rail(0.7, 12);
	const rr::ShotOutcome O = Evaluate(C, Assigned(rr::Discipline::EightBall, Foul), Declare(3, P2), Foul);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(O.Next == rr::NextAction::Pass && O.NextShooter == B);
	RB_CHECK(O.NextFreeShot);
	RB_CHECK(O.NextVisits == 1);
	RB_CHECK(O.NextCueBall == rr::CueBallNext::InPosition);

	Shot Free; // B (stripes) on the free shot hits a solid first: no 3.2 foul
	Free.OnRange(1, 15);
	Free.Hit(0.3, 0, 3).Rail(0.7, 3);
	rr::GameState G = Assigned(rr::Discipline::EightBall, Free);
	G.Shooter = B;
	G.FreeShot = true;
	G.VisitsRemaining = 1;
	const rr::ShotOutcome OF = Evaluate(C, G, Declare(12, P2), Free);
	RB_CHECK(!OF.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(!OF.AnyFoul);
	RB_CHECK(IsContinue(OF, B)); // the extra visit: B plays on
	RB_CHECK(OF.NextVisits == 0);
	RB_CHECK(!OF.NextFreeShot);
}

// TwoVisits: a foul-free miss uses up a visit; without visits left the turn passes.
RB_TEST(Rules_Review_EightTwoVisitsMissConsumesVisit)
{
	rr::RulesConfig C = kEight;
	C.FoulCueBall = rr::FoulCueBallMode::TwoVisits;
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Rail(0.7, 3);
	rr::GameState G = Assigned(rr::Discipline::EightBall, S);
	G.VisitsRemaining = 1;
	const rr::ShotOutcome O = Evaluate(C, G, Declare(3, P2), S);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.NextVisits == 0);
	Shot S2;
	S2.OnRange(1, 15);
	S2.Hit(0.3, 0, 3).Rail(0.7, 3);
	G.VisitsRemaining = 0;
	RB_CHECK(IsPass(Evaluate(C, G, Declare(3, P2), S2), B, rr::CueBallNext::InPosition));
}

// APA (Scoop = Foul, 12.3): a scooped cue ball is a foul; under WPA it is a miscue (G19).
RB_TEST(Rules_Review_ApaScoopIsFoul)
{
	auto ScoopShot = []() {
		Shot S;
		S.OnRange(1, 15);
		S.Record.Stroke.Strokes[0].TipClothContact = true;
		S.Airborne(0.02, 0).Add(rb::RecordEventType::BallLand, 0.1, 0);
		S.Hit(0.3, 0, 3).Rail(0.7, 3);
		return S;
	};
	Shot S = ScoopShot();
	const rr::ShotOutcome Apa = Evaluate(rr::MakeRulesConfig(rr::RulesPreset::Apa8Ball), Assigned(rr::Discipline::EightBall, S), {}, S);
	RB_CHECK(Apa.Detected.Has(rr::Foul::IllegalScoop));
	RB_CHECK(IsPass(Apa, B, rr::CueBallNext::InHandAnywhere));
	Shot W = ScoopShot();
	RB_CHECK(!Evaluate(kEight, Assigned(rr::Discipline::EightBall, W), Declare(3, P2), W).AnyFoul);
}

// Bar / house rules (12.6): no rail-after-contact requirement; a foul gives BIH behind the head string and the next
// shot is then judged under 3.11.
RB_TEST(Rules_Review_HouseRulesKitchenFoulAndNoRailRequirement)
{
	const rr::RulesConfig House = rr::MakeRulesConfig(rr::RulesPreset::BarHouse8Ball);
	Shot NoRail;
	NoRail.OnRange(1, 15);
	NoRail.Hit(0.3, 0, 3);
	const rr::ShotOutcome O1 = Evaluate(House, Assigned(rr::Discipline::EightBall, NoRail), {}, NoRail);
	RB_CHECK(!O1.AnyFoul);
	RB_CHECK(IsPass(O1, B, rr::CueBallNext::InPosition));

	Shot Wrong;
	Wrong.OnRange(1, 15);
	Wrong.Hit(0.3, 0, 12).Rail(0.7, 12);
	const rr::ShotOutcome O2 = Evaluate(House, Assigned(rr::Discipline::EightBall, Wrong), {}, Wrong);
	RB_CHECK(O2.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(IsPass(O2, B, rr::CueBallNext::InHandAboveHeadString));

	Shot Kitchen; // B (stripes) in hand behind the head string plays a stripe lying above the head string directly
	Kitchen.OnRange(1, 15);
	Kitchen.On(12, {-0.9, 0.2});
	Kitchen.InHand(rb::CueBallInHand::AboveHeadString, {-1.0, 0.0});
	Kitchen.Hit(0.3, 0, 12, {-0.9, 0.2}).Rail(0.6, 12, rb::CushionId::LeftHead).Cross(0.9, 0, rb::TableLine::HeadString, +1);
	rr::GameState G = Assigned(rr::Discipline::EightBall, Kitchen);
	G.Shooter = B;
	G.CueBall = rr::CueBallNext::InHandAboveHeadString;
	const rr::ShotOutcome O3 = Evaluate(House, G, {}, Kitchen);
	RB_CHECK(O3.Detected.Has(rr::Foul::BadPlayAboveHeadStringP1));
	RB_CHECK(IsPass(O3, A, rr::CueBallNext::InHandAboveHeadString));
}

// EightOnBreak = Rerack variant: the same breaker re-breaks a fresh 15-ball rack.
RB_TEST(Rules_Review_EightOnBreakRerackVariant)
{
	rr::RulesConfig C = kEight;
	C.EightOnBreak = rr::EightOnBreakRule::Rerack;
	Shot S = BreakFrom(1, true);
	S.Pot(1.0, 8, P3);
	const rr::ShotOutcome O = Evaluate(C, BreakStateFor(rr::Discipline::EightBall, S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.Next == rr::NextAction::RerackAndBreak);
	RB_CHECK(O.NextShooter == A);
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::Rerack15);
	RB_CHECK(O.NextCueBall == rr::CueBallNext::InHandAboveHeadString);
}

// ---------------------------------------------------------------------------------------------
// 9-ball
// ---------------------------------------------------------------------------------------------

// Break foul (3.2) with the 9 pocketed: 9 spotted, BIH anywhere, counts as a foul (R 5.6, 5.7).
RB_TEST(Rules_Review_NineBreakWrongBallFirstWithNineSpotsNine)
{
	Shot S = BreakFrom(2, false);
	S.OnRange(1, 9);
	S.Pot(1.0, 9, P3).Pot(1.1, 4, P2);
	const rr::ShotOutcome O = Evaluate(kNine, BreakStateFor(rr::Discipline::NineBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(O.Next != rr::NextAction::RackWon);
	RB_CHECK(SpotsAre(O, {9})); // 4 stays down
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(O.FoulsAfter[A] == 1);
	RB_CHECK(!O.NextPushOutAvailable);
}

// A break scratch is a foul: the three-ball rule is not evaluated (no options), BIH anywhere (7.2).
RB_TEST(Rules_Review_NineBreakScratchSkipsThreeBallRule)
{
	Shot S = BreakFrom(1, false);
	S.OnRange(1, 9);
	S.Pot(1.0, 3, P2).Pot(1.3, 0, P0);
	const rr::ShotOutcome O = Evaluate(kNine, BreakStateFor(rr::Discipline::NineBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(O.Options.IsEmpty());
	RB_CHECK(!O.NextPushOutAvailable);
}

// Push-out: 3.2 and 3.3 suspended (wrong ball first AND no rail is fine), other fouls still apply (double hit).
RB_TEST(Rules_Review_NinePushOutSuspendsOnlyContactAndRail)
{
	Shot Legal;
	Legal.OnRange(1, 9);
	Legal.Hit(0.3, 0, 5);
	rr::GameState G = StateFor(rr::Discipline::NineBall, Legal);
	G.PushOutAvailable = true;
	G.Players[A].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kNine, G, Kind(rr::ShotKind::PushOut), Legal);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(O.FoulsAfter[A] == 0); // a legal push-out resets the counter
	RB_CHECK(!O.NextPushOutAvailable);

	Shot Double;
	Double.OnRange(1, 9);
	Double.Tips({{0.0, 0.0012}, {0.0150, 0.0158}});
	Double.Hit(0.3, 0, 5);
	G.Players[A].ConsecutiveFouls = 0;
	const rr::ShotOutcome OD = Evaluate(kNine, G, Kind(rr::ShotKind::PushOut), Double);
	RB_CHECK(OD.Detected.Bits == (1u << static_cast<unsigned>(rr::Foul::DoubleHit)));
	RB_CHECK(IsPass(OD, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(OD.FoulsAfter[A] == 1);
}

// Third consecutive foul committed on a push-out loses the rack; the pocketed 9 is still listed for spotting.
RB_TEST(Rules_Review_NineThirdFoulOnPushOutLosesRack)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 9).Pot(0.8, 9, P3).Pot(1.2, 0, P2);
	rr::GameState G = StateFor(rr::Discipline::NineBall, S);
	G.PushOutAvailable = true;
	G.Players[A].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kNine, G, Kind(rr::ShotKind::PushOut), S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch) && O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(O.Enforced == rr::Foul::ThreeConsecutiveFouls);
	RB_CHECK(IsRackWon(O, B));
	RB_CHECK(O.FoulsAfter[A] == 0);
}

// Foul counters across innings: only the shooter's counter moves; the opponent's is carried unchanged.
RB_TEST(Rules_Review_NineFoulCountersAcrossInnings)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1); // no rail
	rr::GameState G = StateFor(rr::Discipline::NineBall, S);
	G.Players[A].ConsecutiveFouls = 1;
	G.Players[B].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kNine, G, {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(!O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(O.FoulsAfter[A] == 2);
	RB_CHECK(O.FoulsAfter[B] == 2);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));

	Shot T; // B, on 2 fouls, now plays legally: B's counter resets, A's stays at 2
	T.OnRange(1, 9);
	T.Hit(0.3, 0, 1).Rail(0.6, 1);
	rr::GameState H = StateFor(rr::Discipline::NineBall, T, B);
	H.Players[A].ConsecutiveFouls = 2;
	H.Players[B].ConsecutiveFouls = 2;
	const rr::ShotOutcome P = Evaluate(kNine, H, {}, T);
	RB_CHECK(P.FoulsAfter[B] == 0);
	RB_CHECK(P.FoulsAfter[A] == 2);
	RB_CHECK(IsPass(P, A, rr::CueBallNext::InPosition));
}

// LEGACY three-ball rule (12.2): a ball resting ON the head string counts as having reached it; WPA 2025 needs the
// center beyond it.
RB_TEST(Rules_Review_NineLegacyReachCountsBallOnHeadString)
{
	auto Break = []() {
		Shot S = BreakFrom(1, false);
		S.OnRange(1, 9);
		RailsFor(S, {2, 3, 4, 5, 6, 7});
		S.Cross(1.5, 4, rb::TableLine::HeadString, -1).Cross(1.51, 5, rb::TableLine::HeadString, -1);
		S.EndAt(7, {-0.635, 0.1});
		return S;
	};
	Shot W = Break();
	const rr::ShotOutcome Wpa = Evaluate(kNine, BreakStateFor(rr::Discipline::NineBall, W), {}, W);
	RB_CHECK(IsDecide(Wpa, B));
	Shot L = Break();
	const rr::ShotOutcome Legacy = Evaluate(rr::MakeRulesConfig(rr::RulesPreset::WpaLegacy9Ball), BreakStateFor(rr::Discipline::NineBall, L), {}, L);
	RB_CHECK(!Legacy.AnyFoul);
	RB_CHECK(IsPass(Legacy, B, rr::CueBallNext::InPosition));
	RB_CHECK(Legacy.NextPushOutAvailable);
}

// ---------------------------------------------------------------------------------------------
// 10-ball
// ---------------------------------------------------------------------------------------------

// The 10 as the last ball made in the called pocket with a scratch: foul, 10 spotted, no win.
RB_TEST(Rules_Review_TenLastBallWithScratchIsSpotted)
{
	Shot S;
	S.On({10});
	for (int b = 1; b <= 9; ++b)
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 10).Pot(0.9, 10, P3).Pot(1.3, 0, P2);
	const rr::ShotOutcome O = Evaluate(kTen, StateFor(rr::Discipline::TenBall, S), Declare(10, P3), S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(O.Next != rr::NextAction::RackWon);
	RB_CHECK(SpotsAre(O, {10}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

// A push-out never wins: the 10 made on it is spotted (R 6.4, 6.8).
RB_TEST(Rules_Review_TenPushOutPocketingTenIsSpotted)
{
	Shot S;
	S.OnRange(2, 10);
	S.Hit(0.3, 0, 10).Pot(0.9, 10, P3).Hit(0.4, 0, 6).Pot(1.1, 6, P2);
	rr::GameState G = StateFor(rr::Discipline::TenBall, S);
	G.PushOutAvailable = true;
	const rr::ShotOutcome O = Evaluate(kTen, G, Kind(rr::ShotKind::PushOut), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(SpotsAre(O, {10})); // 6 stays down
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(OptionsAre(O, {rr::Option::ShootFromPosition, rr::Option::PassBack}));
}

// Break scratch with the 10 pocketed: standard foul, 10 spotted, BIH anywhere, no push-out.
RB_TEST(Rules_Review_TenBreakScratchSpotsTen)
{
	Shot S = BreakFrom(1, false);
	S.OnRange(1, 10);
	S.Pot(1.0, 10, P3).Pot(1.2, 0, P0);
	const rr::ShotOutcome O = Evaluate(kTen, BreakStateFor(rr::Discipline::TenBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(SpotsAre(O, {10}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
	RB_CHECK(!O.NextPushOutAvailable);
}

// EarlyTenWins (non-WPA variant): the called 10 wins even when it is not the last ball.
RB_TEST(Rules_Review_TenEarlyTenWinsVariant)
{
	Shot S;
	S.On({9, 10});
	for (int b = 1; b <= 8; ++b)
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 9).Hit(0.6, 9, 10).Pot(1.2, 10, P3).Rail(1.0, 9);
	rr::RulesConfig C = kTen;
	C.TenOnlyBall = rr::TenOnlyBallMoment::EarlyTenWins;
	const rr::ShotOutcome O = Evaluate(C, StateFor(rr::Discipline::TenBall, S), Declare(10, P3), S);
	RB_CHECK(IsRackWon(O, A));
	RB_CHECK(SpotsAre(O, {}));
}

// A wrongfully pocketed ball (R 6.6) is not a foul and resets the shooter's counter.
RB_TEST(Rules_Review_TenWrongfulPocketResetsFoulCounter)
{
	Shot S;
	S.OnRange(2, 10);
	S.Hit(0.3, 0, 2).Pot(0.9, 2, P2);
	rr::GameState G = StateFor(rr::Discipline::TenBall, S);
	G.Players[A].ConsecutiveFouls = 2;
	const rr::ShotOutcome O = Evaluate(kTen, G, Declare(2, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsDecide(O, B));
	RB_CHECK(O.FoulsAfter[A] == 0);
}

// ---------------------------------------------------------------------------------------------
// 14.1
// ---------------------------------------------------------------------------------------------

namespace
{
	Shot OpeningBreak()
	{
		Shot S = BreakFrom(1, true);
		S.On(0, {-0.9, 0.1});
		S.Record.Start.PlacedPosition = {-0.9, 0.1};
		return S;
	}
}

// A STANDARD foul on the opening break (requirement met by the called ball, S24) counts toward three fouls: on the
// third it is -1 -15, all 15 re-racked, the offender breaks, nothing is spotted.
RB_TEST(Rules_Review_StraightStandardFoulOnOpeningBreakCanBeThirdFoul)
{
	Shot S = OpeningBreak();
	S.Pot(1.0, 5, P2).Rail(0.8, 3).Pot(1.3, 0, P0);
	rr::GameState G = BreakStateFor(rr::Discipline::StraightPool, S);
	G.Players[A].ConsecutiveFouls = 2;
	G.Players[A].Score = 7;
	const rr::ShotOutcome O = Evaluate(kStraight, G, Declare(5, P2), S);
	RB_CHECK(!O.Detected.Has(rr::Foul::BreakingFoul141));
	RB_CHECK(O.Detected.Has(rr::Foul::ThreeConsecutiveFouls));
	RB_CHECK(O.Enforced == rr::Foul::ThreeConsecutiveFouls);
	RB_CHECK(O.ScoreDelta[A] == -16);
	RB_CHECK(O.ScoreDelta[B] == 0);
	RB_CHECK(O.FoulsAfter[A] == 0);
	RB_CHECK(SpotsAre(O, {}));
	RB_CHECK(O.Next == rr::NextAction::RerackAndBreak && O.NextShooter == A);
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::Rerack15);
}

// Breaking foul: every pocketed and every off-table object ball is listed for spotting (ascending), -2 only.
RB_TEST(Rules_Review_StraightBreakingFoulSpotsPocketedAndOffTableBalls)
{
	Shot S = OpeningBreak();
	S.Pot(1.0, 12, P3).Off(1.1, 7);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.NumObjectBallsDrivenToRailAfterRackContact == 2);
	RB_CHECK(!F.CueBallDrivenToRailAfterRackContact);
	const rr::ShotOutcome O = rr::EvaluateShot(kStraight, Table9Ft(), BreakStateFor(rr::Discipline::StraightPool, S), Declare(5, P2), F);
	RB_CHECK(O.Detected.Has(rr::Foul::BreakingFoul141) && O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(O.Enforced == rr::Foul::BreakingFoul141);
	RB_CHECK(O.ScoreDelta[A] == -2);
	RB_CHECK(SpotsAre(O, {7, 12}));
	RB_CHECK(O.CueBallIfAccepted == rr::CueBallNext::InPosition);
}

// A called ball made while another ball leaves the table: standard foul (-1), both spotted, CB stays.
RB_TEST(Rules_Review_StraightCalledBallWithBallOffTableIsFoul)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Pot(0.8, 3, P1).Hit(0.5, 0, 9).Off(0.9, 9);
	const rr::ShotOutcome O = Evaluate(kStraight, StateFor(rr::Discipline::StraightPool, S), Declare(3, P1), S);
	RB_CHECK(O.Detected.Bits == (1u << static_cast<unsigned>(rr::Foul::ObjectBallOffTable)));
	RB_CHECK(O.ScoreDelta[A] == -1);
	RB_CHECK(SpotsAre(O, {3, 9}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::None);
}

// Re-rack checks only follow SCORING shots (9.5): a miss that pockets the second-to-last ball spots it; no re-rack.
RB_TEST(Rules_Review_StraightMissLeavingOneBallDoesNotRerack)
{
	Shot S;
	S.On({3, 7});
	for (const int b : {1, 2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15})
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 3).Hit(0.6, 3, 7).Pot(1.0, 7, P2).Rail(1.1, 3);
	const rr::ShotOutcome O = Evaluate(kStraight, StateFor(rr::Discipline::StraightPool, S), Declare(3, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 0);
	RB_CHECK(SpotsAre(O, {7}));
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::None);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

// Integration (WP-9 PlanRerack14): a scoring shot leaving one ball passes the FINAL positions of the cue ball and the
// 15th ball to the re-rack plan (S13 geometry: 15th in the rack area, CB well away -> 15th to the head spot).
RB_TEST(Integ_Rules_Review_StraightRerack14UsesFinalPositions)
{
	Shot S;
	S.On({3, 5});
	for (const int b : {1, 2, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15})
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 3).Pot(0.8, 3, P1).Hit(0.5, 0, 5);
	S.EndAt(5, {0.70, 0.05});
	S.EndAt(0, {-0.2, 0.3});
	const rr::ShotOutcome O = Evaluate(kStraight, StateFor(rr::Discipline::StraightPool, S), Declare(3, P1), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 1);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.Rack.Kind == rr::RackCommandKind::Rerack14);
	RB_CHECK(O.Rack.FifteenthBall == 5);
	RB_CHECK(O.Rack.FifteenthBallPlacement == rr::PlacementCommand::ToHeadSpot);
	RB_CHECK(O.Rack.CueBallPlacement == rr::PlacementCommand::Keep);
}

// ---------------------------------------------------------------------------------------------
// Blackball (12.4)
// ---------------------------------------------------------------------------------------------

// The black driven off the table in normal play is a foul, not a loss: it is spotted first and the opponent gets a
// free shot (12.4 spotting order "black, then the next shooter's group, then the rest").
RB_TEST(Rules_Review_BlackballBlackOffTableIsSpottedFoul)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Rail(0.5, 3).Hit(0.6, 3, 8).Off(1.0, 8);
	const rr::ShotOutcome O = Evaluate(kBlack, Assigned(rr::Discipline::Blackball, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(O.Next != rr::NextAction::RackWon);
	RB_CHECK(SpotsAre(O, {8}));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
	RB_CHECK(O.NextFreeShot);
}

// Spotting order: black, then the NEXT shooter's group (B = stripes), then the rest.
RB_TEST(Rules_Review_BlackballSpotOrderBlackThenNextShooterGroup)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 2).Hit(0.5, 2, 3).Off(0.8, 3).Hit(0.6, 2, 12).Off(0.9, 12).Hit(0.7, 2, 8).Off(1.0, 8).Rail(1.1, 2);
	const rr::ShotOutcome O = Evaluate(kBlack, Assigned(rr::Discipline::Blackball, S), {}, S);
	RB_CHECK(SpotsAre(O, {8, 12, 3}));
	RB_CHECK(O.NextFreeShot);
}

// The last own ball and the black on one legal shot win (loss only if own balls remain AFTER the shot, 12.4).
RB_TEST(Rules_Review_BlackballLastOwnBallAndBlackTogetherWins)
{
	Shot S;
	S.On({7, 8, 9, 10});
	for (const int b : {1, 2, 3, 4, 5, 6, 11, 12, 13, 14, 15})
	{
		S.Presence(b, rb::BallPresence::Pocketed);
	}
	S.Hit(0.3, 0, 7).Pot(0.9, 7, P0).Hit(0.5, 7, 8).Pot(1.2, 8, P3);
	const rr::ShotOutcome O = Evaluate(kBlack, Assigned(rr::Discipline::Blackball, S), {}, S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsRackWon(O, A));

	Shot T; // the black with an own ball still on the table afterwards loses
	T.On({6, 7, 8, 9, 10});
	for (const int b : {1, 2, 3, 4, 5, 11, 12, 13, 14, 15})
	{
		T.Presence(b, rb::BallPresence::Pocketed);
	}
	T.Hit(0.3, 0, 7).Pot(0.9, 7, P0).Hit(0.5, 7, 8).Pot(1.2, 8, P3);
	RB_CHECK(IsRackWon(Evaluate(kBlack, Assigned(rr::Discipline::Blackball, T), {}, T), B));
}

// Potting only an opponent ball is a foul (free shot); with an own ball as well it is fine.
RB_TEST(Rules_Review_BlackballOpponentBallOnlyIsFoul)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Hit(0.5, 3, 12).Pot(0.9, 12, P2).Rail(1.0, 3);
	const rr::ShotOutcome O = Evaluate(kBlack, Assigned(rr::Discipline::Blackball, S), {}, S);
	RB_CHECK(O.Detected.Bits == (1u << static_cast<unsigned>(rr::Foul::PottedOpponentBallOnly)));
	RB_CHECK(O.NextFreeShot);
	RB_CHECK(O.Next == rr::NextAction::Pass && O.NextShooter == B);

	Shot T;
	T.OnRange(1, 15);
	T.Hit(0.3, 0, 3).Pot(0.8, 3, P1).Hit(0.5, 0, 12).Pot(0.9, 12, P2);
	const rr::ShotOutcome P = Evaluate(kBlack, Assigned(rr::Discipline::Blackball, T), {}, T);
	RB_CHECK(!P.AnyFoul);
	RB_CHECK(IsContinue(P, A));
}

// Open table: potting one group on a normal shot assigns it, on a free shot it does not; the black first is a foul
// and the black potted on an open table loses.
RB_TEST(Rules_Review_BlackballOpenTableGroupsAndBlack)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.3, 0, 3).Pot(0.8, 3, P1);
	rr::GameState G = StateFor(rr::Discipline::Blackball, S);
	const rr::ShotOutcome O = Evaluate(kBlack, G, {}, S);
	RB_CHECK(IsContinue(O, A));
	RB_CHECK(O.AssignShooterGroup == rr::BallGroup::Solids);

	Shot Free;
	Free.OnRange(1, 15);
	Free.Hit(0.3, 0, 3).Pot(0.8, 3, P1);
	G.FreeShot = true;
	const rr::ShotOutcome OF = Evaluate(kBlack, G, {}, Free);
	RB_CHECK(IsContinue(OF, A));
	RB_CHECK(OF.AssignShooterGroup == rr::BallGroup::None);
	G.FreeShot = false;

	Shot BlackFirst;
	BlackFirst.OnRange(1, 15);
	BlackFirst.Hit(0.3, 0, 8).Rail(0.7, 8);
	RB_CHECK(Evaluate(kBlack, G, {}, BlackFirst).Detected.Has(rr::Foul::WrongBallFirst));

	Shot BlackIn;
	BlackIn.OnRange(1, 15);
	BlackIn.Hit(0.3, 0, 3).Hit(0.5, 3, 8).Pot(1.0, 8, P3).Rail(1.1, 3);
	RB_CHECK(IsRackWon(Evaluate(kBlack, G, {}, BlackIn), B));
}
