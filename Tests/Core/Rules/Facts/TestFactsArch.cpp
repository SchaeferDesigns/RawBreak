// Owner: WP-8 (rules facts & evaluation). Architecture tests A-CALL-1 (ObviousAssist call inference from the
// ordered rail-contact lists, rules.md 4.5) and A-FACT-1 (overflow flags of the F13 ordered contact lists), plus
// start-state helpers of Evaluate.h.

#include "Rules/Facts/RulesTestUtil.h"

using namespace rbrules;
namespace rr = rb::rules;

namespace
{
	rr::RulesConfig ObviousAssist8()
	{
		rr::RulesConfig C = rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball);
		C.Calls = rr::CallMode::ObviousAssist;
		return C;
	}

	// Open 8-ball table, no call made: the cue ball hits 3 directly, 3 drops in P2 (FootRight).
	Shot DirectPot()
	{
		Shot S;
		S.OnRange(1, 15);
		S.Hit(0.3, 0, 3);
		return S;
	}
}

RB_TEST(ARCH_CALL1_ObviousAssistOnlyJawsOfOwnPocket)
{
	const rr::RulesConfig C = ObviousAssist8();

	// Only the jaws of P2 before dropping in P2: obvious -> inferred call (3, P2), A continues with solids.
	{
		Shot S = DirectPot();
		S.Jaw(0.8, 3, rb::PocketId::FootRight, rb::JawSide::Incoming).Jaw(0.82, 3, rb::PocketId::FootRight, rb::JawSide::Outgoing);
		S.Pot(0.9, 3, rb::PocketId::FootRight);
		const rr::ShotFacts F = S.Facts();
		const rr::GameState G = StateFor(rr::Discipline::EightBall, S);
		const rr::Call Called = rr::ResolveCall(C, G, {}, F);
		RB_CHECK(Called.Ball == 3);
		RB_CHECK(Called.Pocket == rb::PocketId::FootRight);
		const rr::ShotOutcome O = rr::EvaluateShot(C, Table9Ft(), G, {}, F);
		RB_CHECK(IsContinue(O, A));
		RB_CHECK(O.AssignShooterGroup == rr::BallGroup::Solids);
	}
	// A jaw of ANOTHER pocket (P1) before dropping in P2: not obvious -> no call, the turn passes.
	{
		Shot S = DirectPot();
		S.Jaw(0.6, 3, rb::PocketId::SideRight, rb::JawSide::Outgoing).Pot(0.9, 3, rb::PocketId::FootRight);
		const rr::ShotFacts F = S.Facts();
		const rr::GameState G = StateFor(rr::Discipline::EightBall, S);
		RB_CHECK(rr::ResolveCall(C, G, {}, F).Ball == rb::kNoBall);
		const rr::ShotOutcome O = rr::EvaluateShot(C, Table9Ft(), G, {}, F);
		RB_CHECK(IsPass(O, B, rr::CueBallNext::InPosition));
		RB_CHECK(O.AssignShooterGroup == rr::BallGroup::None);
	}
	// A cushion (bank) before dropping: not obvious.
	{
		Shot S = DirectPot();
		S.Rail(0.6, 3, rb::CushionId::RightFoot).Pot(0.9, 3, rb::PocketId::FootRight);
		RB_CHECK(rr::ResolveCall(C, StateFor(rr::Discipline::EightBall, S), {}, S.Facts()).Ball == rb::kNoBall);
	}
	// A kiss off another ball before dropping: not obvious.
	{
		Shot S = DirectPot();
		S.Hit(0.6, 3, 9).Pot(0.9, 3, rb::PocketId::FootRight);
		RB_CHECK(rr::ResolveCall(C, StateFor(rr::Discipline::EightBall, S), {}, S.Facts()).Ball == rb::kNoBall);
	}
	// The cue ball touched a cushion before hitting 3 (kick): not obvious.
	{
		Shot S;
		S.OnRange(1, 15);
		S.Rail(0.1, 0, rb::CushionId::Head).Hit(0.3, 0, 3).Pot(0.9, 3, rb::PocketId::FootRight);
		RB_CHECK(rr::ResolveCall(C, StateFor(rr::Discipline::EightBall, S), {}, S.Facts()).Ball == rb::kNoBall);
	}
	// An explicit call always wins over the inference; Explicit mode never infers.
	{
		Shot S = DirectPot();
		S.Pot(0.9, 3, rb::PocketId::FootRight);
		const rr::ShotFacts F = S.Facts();
		const rr::GameState G = StateFor(rr::Discipline::EightBall, S);
		RB_CHECK(rr::ResolveCall(C, G, Declare(5, rb::PocketId::SideLeft), F).Ball == 5);
		RB_CHECK(rr::ResolveCall(rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball), G, {}, F).Ball == rb::kNoBall);
	}
}

RB_TEST(ARCH_FACT1_OrderedContactListsFlagOverflow)
{
	Shot S;
	S.OnRange(1, 15);
	S.Hit(0.001, 0, 1);
	for (int i = 0; i < 40; ++i)
	{
		S.Hit(0.01 + 0.01 * i, 1, 2);
		S.Rail(0.015 + 0.01 * i, 1, rb::CushionId::Foot);
	}
	const rr::ShotFacts F = S.Facts();
	const rr::BallShotSummary& One = F.Balls[1];
	RB_CHECK(One.BallContacts.Size() == rr::kMaxBallContactEntries);
	RB_CHECK(One.BallContactsOverflow);
	RB_CHECK(One.BallContacts[0].Partner == 0);           // the first entries are kept
	RB_CHECK_NEAR(One.BallContacts[1].Time, 0.01, 1e-12);
	RB_CHECK(One.RailContacts.Size() == rr::kMaxRailContactEntries);
	RB_CHECK(One.RailContactsOverflow);
	RB_CHECK_NEAR(One.RailContacts[0].Time, 0.015, 1e-12);
	RB_CHECK(One.RailContactsAfter == 40);                // counts are exact beyond the capacity
	RB_CHECK(One.RailContactsBefore == 0);
	RB_CHECK(F.Balls[2].BallContacts.Size() == rr::kMaxBallContactEntries);
	RB_CHECK(F.Balls[2].BallContactsOverflow);
	RB_CHECK(!F.Balls[3].BallContactsOverflow && !F.Balls[3].RailContactsOverflow);
	RB_CHECK(!F.Balls[0].BallContactsOverflow);           // the cue ball had one contact

	Shot Small;
	Small.OnRange(1, 3);
	Small.Hit(0.1, 0, 1).Rail(0.2, 1);
	const rr::ShotFacts G = Small.Facts();
	RB_CHECK(G.Balls[1].BallContacts.Size() == 1 && !G.Balls[1].BallContactsOverflow);
	RB_CHECK(G.Balls[1].RailContacts.Size() == 1 && !G.Balls[1].RailContactsOverflow);
}

RB_TEST(Rules_Helpers_StartStateQueries)
{
	Shot S;
	S.On({3, 4, 8, 12});
	rr::GameState G = StateFor(rr::Discipline::EightBall, S);
	RB_CHECK(rr::LowestObjectBallAtStart(G) == 3);
	RB_CHECK(rr::CountObjectBallsOnTable(G) == 4);
	RB_CHECK(!rr::GroupCleared(G, rr::BallGroup::Solids));
	RB_CHECK(!rr::GroupCleared(G, rr::BallGroup::Stripes));
	RB_CHECK(!rr::GroupCleared(G, rr::BallGroup::None));
	G.Balls[12].Kind = rr::BallStatusKind::OutOfPlay;
	RB_CHECK(rr::GroupCleared(G, rr::BallGroup::Stripes));

	// Open table, stripes gone: the 8 is a legal first contact (R 4.4 2025), but not under the 2016 LEGACY rule.
	rr::RulesConfig C = rr::MakeRulesConfig(rr::RulesPreset::Wpa8Ball);
	RB_CHECK(rr::LegalFirstContactMask(C, G, {}) == (Bit(3) | Bit(4) | Bit(8)));
	C.OpenTableEightGroupGoneException = false;
	RB_CHECK(rr::LegalFirstContactMask(C, G, {}) == (Bit(3) | Bit(4)));

	// 9-ball: the lowest ball; push-out: any ball.
	rr::GameState N = StateFor(rr::Discipline::NineBall, S);
	const rr::RulesConfig Nine = rr::MakeRulesConfig(rr::RulesPreset::Wpa9Ball);
	RB_CHECK(rr::LegalFirstContactMask(Nine, N, {}) == Bit(3));
	RB_CHECK(rr::LegalFirstContactMask(Nine, N, Kind(rr::ShotKind::PushOut)) == (Bit(3) | Bit(4) | Bit(8) | Bit(12)));
}

RB_TEST(Rules_Facts_PocketedListInTimeOrderAndOffTableWins)
{
	Shot S;
	S.OnRange(1, 9);
	S.Hit(0.3, 0, 1).Pot(1.4, 6, rb::PocketId::SideLeft).Pot(1.1, 2, rb::PocketId::FootLeft);
	S.Add(rb::RecordEventType::BallExternalContact, 0.9, 5).Feature = static_cast<std::uint8_t>(rb::ExternalObject::Lamp);
	S.Pot(1.6, 5, rb::PocketId::HeadLeft); // came back off the lamp and dropped: still off the table (R 2.6)
	const rr::ShotFacts F = S.Facts();
	RB_CHECK(F.Pocketed.Size() == 2);
	RB_CHECK(F.Pocketed[0].Ball == 2 && F.Pocketed[1].Ball == 6);
	RB_CHECK(!F.IsPocketed(5));
	RB_CHECK(F.IsOffTable(5));
	RB_CHECK(F.NumObjectBallsDrivenToRail == 3);
	RB_CHECK(F.CountPocketedOrCrossedHeadString == 2);
	RB_CHECK(F.Balls[2].Pocket == rb::PocketId::FootLeft);
}
