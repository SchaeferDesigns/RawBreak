// Owner: WP-8 (rules facts & evaluation). Adversarial review tests for DeriveShotFacts (rules.md 3.4-3.5, F4, F5,
// F10, F12): record shapes the spec tests do not cover.

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	const rr::RulesConfig kNine = rr::MakeRulesConfig(rr::RulesPreset::Wpa9Ball);
	const rr::RulesConfig kEight = rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball);
}

// A ball pocketed on an EARLIER shot may still be reported as Pocketed in the end snapshot (it lies in the pocket,
// pitfall 6); an OutOfPlay 8-ball ball may be reported OffTable. Neither happened on THIS shot (F4/F5).
RB_TEST(Rules_Review_EarlierPocketedBallInEndSnapshotIsNotPocketedAgain)
{
	Shot S;
	S.OnRange(1, 9);
	S.Presence(4, rb::BallPresence::Pocketed);
	S.Record.End.Balls[4].Status = rb::BallEndStatus::Pocketed;
	S.Record.End.Balls[4].Pocket = rb::PocketId::FootLeft;
	S.Hit(0.3, 0, 1); // no rail, nothing pocketed on this shot
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(!F.IsPocketed(4));
	RB_CHECK(!F.AnyObjectBallPocketed);
	RB_CHECK(F.Pocketed.IsEmpty());
	RB_CHECK(F.NumObjectBallsDrivenToRail == 0);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));

	Shot S2;
	S2.OnRange(1, 15);
	S2.Presence(13, rb::BallPresence::OutOfPlay);
	S2.Record.End.Balls[13].Status = rb::BallEndStatus::OffTable;
	S2.Hit(0.3, 0, 3).Rail(0.6, 3);
	const rr::ShotFacts F2 = S2.Facts();
	RB_CHECK(!F2.IsOffTable(13));
	RB_CHECK(F2.ObjectBallsOffTable == 0u);
	rr::GameState G = StateFor(rr::Discipline::EightBall, S2);
	G.TableOpen = false;
	G.Players[A].Group = rr::BallGroup::Solids;
	G.Players[B].Group = rr::BallGroup::Stripes;
	const rr::ShotOutcome O2 = rr::EvaluateShot(kEight, Table9Ft(), G, Declare(3, rb::PocketId::FootRight), F2);
	RB_CHECK(!O2.AnyFoul);
	RB_CHECK(IsPass(O2, B, rr::CueBallNext::InPosition));
}

// R 3.6 / 1.6: the cue ball may be handled while it is in hand (before the stroke); touching an object ball while
// placing it, touching the cue ball after the stroke started, or touching a cue ball that is not in hand are fouls.
RB_TEST(Rules_Review_HandlingInHandCueBallBeforeStrokeIsNoFoul)
{
	auto ShotWithTouch = [](bool InHand, int Ball, rb::NonTipSource Source, double Time) {
		Shot S;
		S.OnRange(1, 9);
		if (InHand)
		{
			S.InHand(rb::CueBallInHand::Anywhere, {-0.5, 0.2});
		}
		rb::NonTipContact Touch;
		Touch.Ball = static_cast<rb::BallId>(Ball);
		Touch.Source = Source;
		Touch.Time = Time;
		S.Record.Stroke.NonTipContacts.PushBack(Touch);
		S.Hit(0.3, 0, 1).Rail(0.6, 1);
		return S;
	};
	rr::RulesConfig C = kNine;
	C.Input = rr::InputMode::Sim;

	Shot Placing = ShotWithTouch(true, 0, rb::NonTipSource::PlacingHand, -2.0);
	const rr::ShotFacts F = Placing.Facts();
	RB_CHECK(!F.NonTipBallContact);
	RB_CHECK(!F.NonTipCueBallContact);
	rr::GameState G = StateFor(rr::Discipline::NineBall, Placing);
	G.CueBall = rr::CueBallNext::InHandAnywhere;
	RB_CHECK(!rr::EvaluateShot(C, Table9Ft(), G, {}, F).AnyFoul);

	Shot Adjust = ShotWithTouch(true, 0, rb::NonTipSource::Ferrule, -0.4); // adjusting with the cue is allowed too
	RB_CHECK(!Adjust.Facts().NonTipBallContact);

	Shot ObjectBall = ShotWithTouch(true, 5, rb::NonTipSource::CueBallInHandTouch, -1.0);
	RB_CHECK(ObjectBall.Facts().NonTipBallContact);
	RB_CHECK(Evaluate(C, G, {}, ObjectBall).Detected.Has(rr::Foul::TouchedBall));

	Shot AfterStroke = ShotWithTouch(true, 0, rb::NonTipSource::Body, 0.4);
	RB_CHECK(AfterStroke.Facts().NonTipCueBallContact);
	RB_CHECK(Evaluate(C, G, {}, AfterStroke).Detected.Has(rr::Foul::TouchedBall));

	Shot NotInHand = ShotWithTouch(false, 0, rb::NonTipSource::Shaft, -0.5);
	RB_CHECK(NotInHand.Facts().NonTipCueBallContact);
	RB_CHECK(Evaluate(C, StateFor(rr::Discipline::NineBall, NotInHand), {}, NotInHand).Detected.Has(rr::Foul::TouchedBall));
}

// 3.4 settle window for the CUE ball: hanging on the lip and dropping inside the window is a scratch; dropping later
// is settling (restored to the lip, no foul).
RB_TEST(Rules_Review_CueBallHangingOnLipSettleWindow)
{
	const rb::Vec2 Lip{1.27 - 0.05, 0.635 - 0.05};
	auto HangingCue = [&Lip](double DropTime) {
		Shot S;
		S.OnRange(1, 9);
		S.Hit(0.3, 0, 1).Rail(0.6, 1);
		S.Add(rb::RecordEventType::MotionTransition, 5.0, 0).PositionA = Lip;
		S.StopAt(6.0);
		S.Add(rb::RecordEventType::BallPocketEnter, DropTime, 0).Feature = static_cast<std::uint8_t>(rb::PocketId::FootLeft);
		S.Add(rb::RecordEventType::BallPocketed, DropTime + 0.1, 0).Feature = static_cast<std::uint8_t>(rb::PocketId::FootLeft);
		S.Record.End.Balls[0].Status = rb::BallEndStatus::Pocketed;
		S.Record.End.Balls[0].Pocket = rb::PocketId::FootLeft;
		return S;
	};
	Shot Inside = HangingCue(9.0);
	const rr::ShotOutcome In = Evaluate(kNine, StateFor(rr::Discipline::NineBall, Inside), {}, Inside);
	RB_CHECK(In.Detected.Has(rr::Foul::CueBallScratch));
	RB_CHECK(IsPass(In, B, rr::CueBallNext::InHandAnywhere));

	Shot Late = HangingCue(11.5);
	const rr::ShotFacts F = Late.Facts();
	RB_CHECK(!F.CueBallPocketed);
	RB_CHECK(F.Balls[0].EndStatus == rb::BallEndStatus::OnTable);
	RB_CHECK_NEAR(F.Balls[0].FinalPosition.x, Lip.x, 1e-12);
	RB_CHECK_NEAR(F.Balls[0].FinalPosition.y, Lip.y, 1e-12);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, Late), {}, F);
	RB_CHECK(!O.AnyFoul);
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
}

// A ball touching the (removed) rack template on the rail is NOT off the table (F5); it is foul 3.15 only when the
// template is modelled (UseRackTemplate).
RB_TEST(Rules_Review_TemplateContactIsNotOffTable)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Rail(0.6, 1).Hit(0.7, 1, 6);
	S.Add(rb::RecordEventType::BallExternalContact, 0.9, 6).Feature = static_cast<std::uint8_t>(rb::ExternalObject::Template);
	S.EndAt(6, {0.9, 0.4});
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.TemplateTouched);
	RB_CHECK(!F.IsOffTable(6));
	RB_CHECK(F.ObjectBallsOffTable == 0u);
	const rr::GameState G = StateFor(rr::Discipline::NineBall, S);
	RB_CHECK(!rr::EvaluateShot(kNine, Table9Ft(), G, {}, F).AnyFoul);
	rr::RulesConfig C = kNine;
	C.UseRackTemplate = true;
	const rr::ShotOutcome O = rr::EvaluateShot(C, Table9Ft(), G, {}, F);
	RB_CHECK(O.Detected.Bits == (1u << static_cast<unsigned>(rr::Foul::TemplateFoul)));
	RB_CHECK(IsPass(O, B, rr::CueBallNext::InHandAnywhere));
}

// F2 for the cue ball: a cue ball frozen to a rail that rolls along it (continuesInitialFreeze) is not driven to that
// rail; the tie presumption (G03) does not revive such a contact.
RB_TEST(Rules_Review_CueBallFrozenToRailIsNotDrivenToIt)
{
	Shot S;
	S.OnRange(1, 9);
	S.On(0, {-0.3, -0.635 + kR});
	S.Record.Start.FrozenToRail[0] = 1u << rb::RailFeatureOfCushion(rb::CushionId::RightHead);
	S.Rail(0.2998, 0, rb::CushionId::RightHead, true).Hit(0.3, 0, 1).Rail(0.4, 0, rb::CushionId::RightHead, true);
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.DrivenToRailAfterContactLegal == 0u);
	RB_CHECK(F.DrivenToRailAfterContactStrict == 0u);
	const rr::ShotOutcome O = rr::EvaluateShot(kNine, Table9Ft(), StateFor(rr::Discipline::NineBall, S), {}, F);
	RB_CHECK(O.Detected.Has(rr::Foul::NoRailAfterContact));
}

// F11 with the region "in baulk" (Blackball): on the baulk line is not in baulk.
RB_TEST(Rules_Review_BaulkPlacementMustBeStrictlyInBaulk)
{
	Shot On;
	On.OnRange(1, 15);
	On.InHand(rb::CueBallInHand::Baulk, {-0.762, 0.1});
	RB_CHECK(!On.Facts().CueBallPlacementLegal);
	Shot In;
	In.OnRange(1, 15);
	In.InHand(rb::CueBallInHand::Baulk, {-0.7621, 0.1});
	RB_CHECK(In.Facts().CueBallPlacementLegal);
	Shot Overlap; // touching another ball's footprint is illegal
	Overlap.OnRange(1, 15);
	Overlap.On(5, {-0.9, 0.1});
	Overlap.InHand(rb::CueBallInHand::Baulk, {-0.9 + 2.0 * kR - 1e-4, 0.1});
	RB_CHECK(!Overlap.Facts().CueBallPlacementLegal);
}
