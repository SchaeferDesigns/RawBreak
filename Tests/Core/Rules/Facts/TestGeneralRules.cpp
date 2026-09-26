// Owner: WP-8 (rules facts & evaluation). rules.md 17.1 derived facts and general rules (G01-G21, G27-G31).
// Every test builds a ShotRecord by hand, derives the facts (F1-F13) and evaluates the shot.

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kNine = rr::MakeRulesConfig(rr::RulesPreset::Wpa9Ball);
	const rr::RulesConfig kEight = rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball);
	const rr::RulesConfig kStraight = rr::MakeRulesConfig(rr::RulesPreset::Wpa14_1);

	// 8-ball, groups assigned: A = solids, B = stripes.
	rr::GameState EightBallAssigned(const Shot& S)
	{
		rr::GameState G = StateFor(rr::Discipline::EightBall, S);
		G.TableOpen = false;
		G.Players[A].Group = rr::BallGroup::Solids;
		G.Players[B].Group = rr::BallGroup::Stripes;
		return G;
	}

	// 14.1 normal shot with the cue ball in hand above the head string at (-0.9, 0) (G28-G31).
	Shot StraightPoolKitchenShot(rb::Vec2 Ball7)
	{
		Shot S;
		S.OnRange(1, 15);
		S.On(7, Ball7);
		S.InHand(rb::CueBallInHand::AboveHeadString, {-0.9, 0.0});
		return S;
	}

	rr::GameState KitchenState(const Shot& S)
	{
		rr::GameState G = StateFor(rr::Discipline::StraightPool, S);
		G.CueBall = rr::CueBallNext::InHandAboveHeadString;
		return G;
	}
}

RB_TEST(Rules_G01_TieWithinWindowFavoursLegalBall)
{
	Shot S;
	S.On({3, 5, 7, 9});
	S.Hit(0.1000, 0, 7).Hit(0.1003, 0, 3).Rail(0.4, 3);
	const rr::GameState G = StateFor(rr::Discipline::NineBall, S);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.EarliestContact == 7);
	RB_CHECK(F.FirstContactTieSet.Size() == 2);
	RB_CHECK(rr::ResolveFirstContact(F, Bit(3)) == 3); // 0.3 ms <= 0.5 ms: FC = 3
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), G, {}, F);
	RB_CHECK(!O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_G02_ContactOutsideTieWindowIsWrongBallFirst)
{
	Shot S;
	S.On({3, 5, 7, 9});
	S.Hit(0.1000, 0, 7).Hit(0.1010, 0, 3).Rail(0.4, 3);
	const rr::GameState G = StateFor(rr::Discipline::NineBall, S);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.FirstContactTieSet.Size() == 1);
	RB_CHECK(rr::ResolveFirstContact(F, Bit(3)) == 7);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), G, {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::WrongBallFirst));
	RB_CHECK(O.Enforced == rr::Foul::WrongBallFirst);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_G03_CueBallRailJustBeforeLegalContactCountsAsAfter)
{
	Shot S;
	S.On({2, 4, 6});
	S.On(2, {0.3, 0.635 - kR - 0.005}); // 5 mm off cushion C3
	S.Rail(0.25000, 0, rb::CushionId::LeftFoot).Hit(0.25030, 0, 2);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.DrivenToRailAfterContactLegal == Bit(0));
	RB_CHECK(F.DrivenToRailAfterContactStrict == 0u);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(!O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_G04_CueBallRailOutsideTieWindowIsNoRailFoul)
{
	Shot S;
	S.On({2, 4, 6});
	S.On(2, {0.3, 0.635 - kR - 0.005});
	S.Rail(0.24870, 0, rb::CushionId::LeftFoot).Hit(0.25030, 0, 2);
	const rr::ShotOutcome O = Evaluate(kNine, StateFor(rr::Discipline::NineBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(O.Detected.Bits == (1u << static_cast<unsigned>(rr::Foul::NoRailAfterContact)));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_G05_FrozenBallContinuingFreezeIsNotDrivenToRail)
{
	Shot S;
	S.OnRange(1, 15);
	S.On(5, {1.27 - kR, 0.1}); // frozen to C2
	S.Record.Start.FrozenToRail[5] = 1u << rb::RailFeatureOfCushion(rb::CushionId::Foot);
	S.Hit(0.3, 0, 5).Rail(0.3001, 5, rb::CushionId::Foot, true);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.Balls[5].RailContactsAfter == 0);
	RB_CHECK(!F.AnyBallDrivenToRailAfterFirstContact(true));
	const rr::ShotOutcome O = rr::EvaluateShot(kEight, Table9Ft(), EightBallAssigned(S), Declare(5, rb::PocketId::FootLeft), F);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_G06_FrozenBallLeavingAndReturningIsDrivenToRail)
{
	Shot S;
	S.OnRange(1, 15);
	S.On(5, {1.27 - kR, 0.1});
	S.Record.Start.FrozenToRail[5] = 1u << rb::RailFeatureOfCushion(rb::CushionId::Foot);
	S.Hit(0.3, 0, 5).Rail(0.3001, 5, rb::CushionId::Foot, true).Rail(1.2, 5, rb::CushionId::Foot, false);
	const rr::ShotOutcome O = Evaluate(kEight, EightBallAssigned(S), Declare(5, rb::PocketId::FootLeft), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_G07_JawContactIsARailContact)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Jaw(0.9, 1, rb::PocketId::FootRight, rb::JawSide::Outgoing);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.Balls[1].RailContacts.Size() == 1);
	RB_CHECK(F.Balls[1].RailContacts[0].Kind == rr::RailContactKind::Jaw);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_G08_CueBallTouchingPocketedBallIsScratch)
{
	Shot S;
	S.OnRange(1, 9);
	S.Presence(4, rb::BallPresence::Pocketed); // pocketed on an earlier shot, lying in P3
	S.Hit(0.3, 0, 1).Rail(0.6, 1);
	S.Add(rb::RecordEventType::BallPocketEnter, 1.0, 0).Feature = static_cast<std::uint8_t>(rb::PocketId::FootLeft);
	S.Add(rb::RecordEventType::BallTouchesPocketedBall, 1.05, 0, 4).Feature = static_cast<std::uint8_t>(rb::PocketId::FootLeft);
	S.Add(rb::RecordEventType::BallPocketExit, 1.1, 0).Feature = static_cast<std::uint8_t>(rb::PocketId::FootLeft);
	S.EndAt(0, {1.1, 0.5}); // rebounded onto the cloth
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.CueBallPocketed);
	const rr::GameState G = StateFor(rr::Discipline::NineBall, S);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), G, {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_G09_SupportedOverPocketCountsAsPocketed)
{
	Shot S;
	S.On({2, 4, 6, 9});
	S.Hit(0.3, 0, 2).Rail(0.6, 2);
	S.StopAt(4.0);
	rb::RecordEvent& E = S.Add(rb::RecordEventType::SupportedOverPocket, 4.0, 4);
	E.Feature = static_cast<std::uint8_t>(rb::PocketId::SideRight);
	E.Value = static_cast<double>(Bit(6));
	rb::SupportedBall Sup;
	Sup.Ball = 4;
	Sup.Pocket = rb::PocketId::SideRight;
	Sup.Supporters = Bit(6);
	S.Record.End.Supported.PushBack(Sup);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.IsPocketed(4));
	RB_CHECK(F.PocketOf(4) == rb::PocketId::SideRight);
	RB_CHECK(F.Pocketed.Size() == 1);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
}

namespace
{
	// G10/G11: ball 5 motionless on the P0 lip at tStop = 6.0, drops later.
	Shot HangingBallShot(double DropTime)
	{
		const rb::Vec2 Lip{-1.27 + 0.05, -0.635 + 0.05};
		Shot S;
		S.OnRange(1, 9);
		S.Hit(0.3, 0, 1).Rail(0.6, 1).Hit(1.0, 1, 5);
		S.Add(rb::RecordEventType::MotionTransition, 5.2, 5).PositionA = Lip; // comes to rest on the lip
		S.StopAt(6.0);
		S.Add(rb::RecordEventType::BallPocketEnter, DropTime, 5).Feature = static_cast<std::uint8_t>(rb::PocketId::HeadRight);
		S.Add(rb::RecordEventType::BallPocketed, DropTime + 0.1, 5).Feature = static_cast<std::uint8_t>(rb::PocketId::HeadRight);
		S.Record.End.Balls[5].Status = rb::BallEndStatus::Pocketed;
		S.Record.End.Balls[5].Pocket = rb::PocketId::HeadRight;
		return S;
	}
}

RB_TEST(Rules_G10_BallDroppingInsideSettleWindowIsPocketed)
{
	Shot S = HangingBallShot(9.0);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.IsPocketed(5));
	RB_CHECK(F.PocketOf(5) == rb::PocketId::HeadRight);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_G11_BallDroppingAfterSettleWindowIsRestored)
{
	Shot S = HangingBallShot(11.5);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(!F.IsPocketed(5));
	RB_CHECK(!F.AnyObjectBallPocketed);
	RB_CHECK(F.Balls[5].EndStatus == rb::BallEndStatus::OnTable);
	RB_CHECK_NEAR(F.Balls[5].FinalPosition.x, -1.27 + 0.05, 1e-12);
	RB_CHECK_NEAR(F.Balls[5].FinalPosition.y, -0.635 + 0.05, 1e-12);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_G12_BallOffLampIsOffTableAndStaysOut)
{
	Shot S;
	S.On({3, 5, 6, 9});
	S.Hit(0.3, 0, 3).Hit(0.5, 3, 6).Rail(0.7, 3);
	S.Add(rb::RecordEventType::BallAirborne, 0.55, 6).ZMax = 0.9;
	S.Add(rb::RecordEventType::BallExternalContact, 0.8, 6).Feature = static_cast<std::uint8_t>(rb::ExternalObject::Lamp);
	S.Add(rb::RecordEventType::BallLand, 1.1, 6);
	S.EndAt(6, {0.5, 0.2}); // landed on the cloth
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.IsOffTable(6));
	RB_CHECK(F.ObjectBallsOffTable == Bit(6));
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::ObjectBallOffTable));
	RB_CHECK(SpotsAre(O, {})); // only the 9 would be spotted
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_G13_RailTopContactIsRailNotOffTable)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Hit(0.5, 1, 6).RailTop(0.7, 6);
	S.EndAt(6, {0.9, 0.5}); // fell back onto the cloth
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(!F.IsOffTable(6));
	RB_CHECK((F.DrivenToRailAfterContactStrict & Bit(6)) != 0u);
	RB_CHECK(F.Balls[6].RailContacts.Size() == 1 && F.Balls[6].RailContacts[0].Kind == rr::RailContactKind::RailTop);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(!O.AnyFoul);
}

RB_TEST(Rules_G14_SecondTipContactIsDoubleHit)
{
	Shot S;
	S.OnRange(1, 9);
	S.Tips({{0.0, 0.0012}, {0.0150, 0.0158}});
	S.Hit(0.3, 0, 1).Rail(0.6, 1);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.DoubleHit);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::DoubleHit));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

namespace
{
	// G15/G16: object ball 1 3 mm from the cue ball (not frozen); hit while the tip is still on the cue ball.
	Shot CloseBallShot(double CutDeg)
	{
		Shot S;
		S.OnRange(1, 9);
		S.On(1, {-0.30 + 2.0 * kR + 0.003, 0.0});
		S.Tips({{0.0, 0.0014}});
		S.Hit(0.0006, 0, 1, {-0.30 + 2.0 * kR + 0.003, 0.0}, CutDeg * rb::kDegToRad).Rail(0.4, 1);
		return S;
	}
}

RB_TEST(Rules_G15_BallHitDuringTipContactIsDoubleHit)
{
	Shot S = CloseBallShot(5.0);
	const rr::ShotOutcome O = Evaluate(kNine, StateFor(rr::Discipline::NineBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::DoubleHit));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_G16_BarelyGrazingHitIsNoDoubleHit)
{
	Shot S = CloseBallShot(82.0);
	const rr::ShotOutcome O = Evaluate(kNine, StateFor(rr::Discipline::NineBall, S), {}, S);
	RB_CHECK(!O.Detected.Has(rr::Foul::DoubleHit));
	RB_CHECK(!O.AnyFoul);
}

RB_TEST(Rules_G17_FrozenBallEnvelopeExemptsReContacts)
{
	Shot S;
	S.OnRange(1, 9);
	S.On(1, {-0.30 + 2.0 * kR, 0.0});
	S.Record.Start.FrozenToCueBall = Bit(1); // declared frozen
	S.Tips({{0.0, 0.0018}, {0.0031, 0.0036}});
	auto TipEvent = [&S](rb::RecordEventType Type, double T, double Gap) {
		rb::RecordEvent& E = S.Add(Type, T, 0, 1); // B = f = ball 1
		E.Value = Gap;
		E.OtherContactBefore = false;
	};
	TipEvent(rb::RecordEventType::TipBallBegin, 0.0, 0.0);
	TipEvent(rb::RecordEventType::TipBallEnd, 0.0018, 0.0012);
	TipEvent(rb::RecordEventType::TipBallBegin, 0.0031, 0.002); // CB 2 mm from ball 1, no other contact yet
	TipEvent(rb::RecordEventType::TipBallEnd, 0.0036, 0.0025);
	S.Hit(0.0, 0, 1, {-0.30 + 2.0 * kR, 0.0}).Rail(0.5, 1);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(!F.DoubleHit);
	RB_CHECK(!F.PushShot);
	RB_CHECK(rr::ResolveFirstContact(F, Bit(1)) == 1);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(!O.Detected.Has(rr::Foul::DoubleHit));
	RB_CHECK(!O.Detected.Has(rr::Foul::PushShot));
	RB_CHECK(!O.AnyFoul);
}

RB_TEST(Rules_G18_LongTipContactIsPushShot)
{
	Shot S;
	S.OnRange(1, 9);
	S.Tips({{0.0, 0.0062}});
	S.Hit(0.3, 0, 1).Rail(0.6, 1);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.PushShot);
	RB_CHECK(!F.DoubleHit);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::PushShot));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

namespace
{
	// G19/G20: scooped / jumped cue ball flies over 5, then hits the lowest ball 2 which drops.
	Shot JumpOverFiveShot(bool Scoop)
	{
		Shot S;
		S.OnRange(2, 9);
		S.Record.Stroke.Strokes[0].TipClothContact = Scoop;
		S.Airborne(0.05, 0);
		S.Add(rb::RecordEventType::BallJumpedOver, 0.12, 0, 5);
		S.Add(rb::RecordEventType::BallLand, 0.2, 0);
		S.Hit(0.3, 0, 2).Pot(0.9, 2, rb::PocketId::FootLeft);
		return S;
	}
}

RB_TEST(Rules_G19_ScoopIsTreatedAsMiscueNotAFoul)
{
	Shot S = JumpOverFiveShot(true);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.Scoop);
	RB_CHECK(F.CueBallAirborne);
	RB_CHECK(F.JumpedOver == Bit(5));
	RB_CHECK(kNine.Scoop == rr::ScoopPolicy::WpaMiscue);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsContinue(O, A));
}

RB_TEST(Rules_G20_JumpOverBallIsFoulOnlyInBlackball)
{
	Shot S = JumpOverFiveShot(false);
	S.On(1, DefaultPosition(1)).OnRange(10, 15);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(!F.Scoop);

	// Blackball (WPA ch. 8, R 8.13.3): foul -> free shot for B.
	const rr::RulesConfig Black = rr::MakeRulesConfig(rr::RulesPreset::WpaBlackball);
	const rr::ShotOutcome OB = rr::EvaluateShot(Black, Table9Ft(), StateFor(rr::Discipline::Blackball, S), {}, F);
	RB_CHECK(OB.Detected.Has(rr::Foul::JumpedOverBall));
	RB_CHECK(OB.Next == rr::NextAction::Pass && OB.NextShooter == B);
	RB_CHECK(OB.NextFreeShot);

	// 9-ball WPA: legal.
	Shot S9 = JumpOverFiveShot(false);
	const rr::ShotOutcome O9 = Evaluate(kNine, StateFor(rr::Discipline::NineBall, S9), {}, S9);
	RB_CHECK(!O9.AnyFoul);
	RB_CHECK(IsContinue(O9, A));
}

RB_TEST(Rules_G21_EquipmentTouchingBallIsFoulInSimMode)
{
	Shot S;
	S.OnRange(1, 9);
	rb::NonTipContact Touch;
	Touch.Ball = 7;
	Touch.Source = rb::NonTipSource::BridgeHand;
	Touch.Time = -1.5; // while aiming
	S.Record.Stroke.NonTipContacts.PushBack(Touch);
	S.Hit(0.3, 0, 1).Rail(0.6, 1);
	rr::RulesConfig C = kNine;
	C.Input = rr::InputMode::Sim;
	const rr::ShotOutcome O = Evaluate(C, StateFor(rr::Discipline::NineBall, S), {}, S);
	RB_CHECK(O.Detected.Has(rr::Foul::TouchedBall));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

RB_TEST(Rules_G27_PlacementOnTheHeadStringIsNotAboveIt)
{
	Shot S;
	S.OnRange(1, 9);
	S.InHand(rb::CueBallInHand::AboveHeadString, {-0.635, 0.1});
	S.Cross(0.01, 0, rb::TableLine::HeadString, +1).Hit(0.5, 0, 1);
	for (int b = 2; b <= 6; ++b)
	{
		S.Rail(0.9, b);
	}
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(!F.CueBallPlacementLegal);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), BreakStateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::BadCueBallPlacement));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));

	Shot Legal;
	Legal.OnRange(1, 9);
	Legal.InHand(rb::CueBallInHand::AboveHeadString, {-0.6351, 0.1});
	RB_CHECK(Legal.Facts().CueBallPlacementLegal);
}

RB_TEST(Rules_G28_KitchenFirstContactAboveHeadStringIsFoulPara1)
{
	Shot S = StraightPoolKitchenShot({-0.8, 0.2});
	S.Hit(0.3, 0, 7, {-0.8, 0.2}).Rail(0.5, 7, rb::CushionId::LeftHead).Cross(0.8, 0, rb::TableLine::HeadString, +1);
	const rr::ShotOutcome O = Evaluate(kStraight, KitchenState(S), Declare(7, rb::PocketId::HeadLeft), S);
	RB_CHECK(O.Detected.Has(rr::Foul::BadPlayAboveHeadStringP1));
	RB_CHECK(!O.Detected.Has(rr::Foul::BadPlayAboveHeadStringP2));
	RB_CHECK(O.ScoreDelta[A] == -1);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_G29_KitchenShotNeverCrossingIsFoulPara2)
{
	Shot S = StraightPoolKitchenShot({-0.8, 0.2});
	S.Hit(0.3, 0, 7, {-0.8, 0.2}).Rail(0.5, 7, rb::CushionId::LeftHead);
	const rr::ShotOutcome O = Evaluate(kStraight, KitchenState(S), Declare(7, rb::PocketId::HeadLeft), S);
	RB_CHECK(O.Detected.Has(rr::Foul::BadPlayAboveHeadStringP2));
	RB_CHECK(O.ScoreDelta[A] == -1);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAboveHeadString));
}

RB_TEST(Rules_G30_BallOnTheHeadStringIsPlayableFromKitchen)
{
	Shot S = StraightPoolKitchenShot({-0.635, 0.2});
	S.Hit(0.3, 0, 7, {-0.635, 0.2}).Rail(0.6, 7, rb::CushionId::LeftHead);
	const rr::ShotOutcome O = Evaluate(kStraight, KitchenState(S), Declare(7, rb::PocketId::HeadLeft), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(O.ScoreDelta[A] == 0);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

RB_TEST(Rules_G31_KickFromBeyondHeadStringIsLegal)
{
	Shot S = StraightPoolKitchenShot({-0.8, 0.2});
	S.Cross(0.2, 0, rb::TableLine::HeadString, +1).Rail(0.5, 0, rb::CushionId::Foot).Cross(0.9, 0, rb::TableLine::HeadString, -1);
	S.Hit(1.2, 0, 7, {-0.8, 0.2}).Rail(1.4, 7, rb::CushionId::LeftHead);
	const rr::ShotOutcome O = Evaluate(kStraight, KitchenState(S), Declare(7, rb::PocketId::HeadLeft), S);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}
