// Owner: WP-9 (rules table procedures & match). Declaration validation before the stroke (rules.md
// 16.16, 4.5, 7.3, 8.4): tests N19, T11, A-PLACE-1.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

namespace
{
	const ShotFacts kNoFacts{};

	ShotDeclaration Declare(ShotKind Kind, int Ball = kNoBall, PocketId Pocket = PocketId::None)
	{
		ShotDeclaration D;
		D.Kind = Kind;
		D.Called.Ball = static_cast<BallId>(Ball);
		D.Called.Pocket = Pocket;
		return D;
	}

	// Brings a fresh match to a normal (non-break) shot of Shooter with the cue ball in position.
	bool ToNormalShot(const MatchConfig& C, MatchState& S, int Shooter)
	{
		if (!StartToFirstBreak(C, S, 0)) return false;
		ShotFacts F;
		F.Balls[kCueBallId].EndStatus = BallEndStatus::OnTable;
		F.Balls[kCueBallId].FinalPosition = {-0.3, 0.1};
		const ShotOutcome O = Shooter == 0 ? ContinueOutcome(0) : PassOutcome(1);
		return ApplyShot(C, S, O, F) == ErrorCode::Ok;
	}
}

RB_TEST(Rules_N19_PushOutOnlyRightAfterTheBreak)
{
	const MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	MatchState S;
	RB_REQUIRE(StartToFirstBreak(C, S, 0));
	const ShotDeclaration Push = Declare(ShotKind::PushOut);
	const Vec2 Kitchen{-0.9, 0.0};
	RB_CHECK(ValidateDeclaration(C, S, Push, &Kitchen) == ErrorCode::InvalidDeclaration); // never on the break

	// Shot 1: legal break, A pocketed a ball -> the push-out window opens for shot 2.
	ShotOutcome Break = ContinueOutcome(0);
	Break.NextPushOutAvailable = true;
	ShotFacts F;
	F.Balls[kCueBallId].EndStatus = BallEndStatus::OnTable;
	F.Balls[kCueBallId].FinalPosition = {-0.3, 0.1};
	RB_REQUIRE(ApplyShot(C, S, Break, F) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::Ok);
	RB_CHECK(GetShotConstraints(C, S).PushOutAllowed);

	// Shot 2: A plays a normal shot and pockets another ball; shot 3: no push-out any more.
	RB_REQUIRE(ApplyShot(C, S, ContinueOutcome(0), F) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Push, nullptr) == ErrorCode::InvalidDeclaration);
	RB_CHECK(!GetShotConstraints(C, S).PushOutAllowed);
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal), nullptr) == ErrorCode::Ok);

	// Push-outs do not exist in 8-ball or 14.1.
	for (const Discipline Game : {Discipline::EightBall, Discipline::StraightPool})
	{
		const MatchConfig Other = MakeMatchConfig(Game);
		MatchState T;
		RB_REQUIRE(ToNormalShot(Other, T, 0));
		T.Game.PushOutAvailable = true;
		RB_CHECK(ValidateDeclaration(Other, T, Push, nullptr) == ErrorCode::InvalidDeclaration);
	}
}

RB_TEST(Rules_T11_NoSafetyInTenBall)
{
	const MatchConfig C = MakeMatchConfig(Discipline::TenBall);
	MatchState S;
	RB_REQUIRE(ToNormalShot(C, S, 1));
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Safety), nullptr) == ErrorCode::InvalidDeclaration);
	RB_CHECK(!GetShotConstraints(C, S).SafetyAllowed);
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Safety, 2, PocketId::SideRight), nullptr) == ErrorCode::InvalidDeclaration);
	// a called shot is fine; calling is required (Explicit)
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 2, PocketId::SideRight), nullptr) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal), nullptr) == ErrorCode::InvalidDeclaration);
	RB_CHECK(GetShotConstraints(C, S).CallRequired);

	// no safety in 9-ball either; allowed in 8-ball and 14.1 (not on the break)
	const MatchConfig Nine = MakeMatchConfig(Discipline::NineBall);
	MatchState N;
	RB_REQUIRE(ToNormalShot(Nine, N, 1));
	RB_CHECK(ValidateDeclaration(Nine, N, Declare(ShotKind::Safety), nullptr) == ErrorCode::InvalidDeclaration);
	const MatchConfig Eight = MakeMatchConfig(Discipline::EightBall);
	MatchState E;
	RB_REQUIRE(ToNormalShot(Eight, E, 1));
	RB_CHECK(ValidateDeclaration(Eight, E, Declare(ShotKind::Safety), nullptr) == ErrorCode::Ok);
	RB_CHECK(GetShotConstraints(Eight, E).SafetyAllowed);
	MatchState EBreak;
	RB_REQUIRE(StartToFirstBreak(Eight, EBreak, 0));
	const Vec2 Kitchen{-0.9, 0.0};
	RB_CHECK(ValidateDeclaration(Eight, EBreak, Declare(ShotKind::Safety), &Kitchen) == ErrorCode::InvalidDeclaration);
	RB_CHECK(ValidateDeclaration(Eight, EBreak, Declare(ShotKind::Break), &Kitchen) == ErrorCode::Ok);
}

RB_TEST(Rules_Match_CallValidation)
{
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	MatchState S;
	RB_REQUIRE(ToNormalShot(C, S, 1));
	// open table: any object ball but the 8 (no claim)
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 3, PocketId::FootRight), nullptr) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 12, PocketId::FootRight), nullptr) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 8, PocketId::FootRight), nullptr) == ErrorCode::InvalidDeclaration);
	// malformed calls
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 3), nullptr) == ErrorCode::InvalidDeclaration);
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, kNoBall, PocketId::FootRight), nullptr) == ErrorCode::InvalidDeclaration);
	S.Game.Balls[5].Kind = BallStatusKind::Pocketed;
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 5, PocketId::FootRight), nullptr) == ErrorCode::InvalidDeclaration);
	// explicit mode: a call is required
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal), nullptr) == ErrorCode::InvalidDeclaration);
	// closed table: own group only
	S.Game.TableOpen = false;
	S.Game.Players[1].Group = BallGroup::Stripes;
	S.Game.Players[0].Group = BallGroup::Solids;
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 12, PocketId::SideLeft), nullptr) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Declare(ShotKind::Normal, 3, PocketId::SideLeft), nullptr) == ErrorCode::InvalidDeclaration);
	// a claim needs an open table
	ShotDeclaration Claim = Declare(ShotKind::Normal, 12, PocketId::SideLeft);
	Claim.ClaimedClearedGroup = BallGroup::Solids;
	RB_CHECK(ValidateDeclaration(C, S, Claim, nullptr) == ErrorCode::InvalidDeclaration);

	// ObviousAssist: no call required
	MatchConfig Casual = C;
	Casual.Rules.Calls = CallMode::ObviousAssist;
	RB_CHECK(ValidateDeclaration(Casual, S, Declare(ShotKind::Normal), nullptr) == ErrorCode::Ok);
	RB_CHECK(!GetShotConstraints(Casual, S).CallRequired);
	// 9-ball has no calls
	const MatchConfig Nine = MakeMatchConfig(Discipline::NineBall);
	MatchState N;
	RB_REQUIRE(ToNormalShot(Nine, N, 1));
	RB_CHECK(ValidateDeclaration(Nine, N, Declare(ShotKind::Normal), nullptr) == ErrorCode::Ok);
	RB_CHECK(!GetShotConstraints(Nine, N).CallRequired);
}

RB_TEST(Integ_Rules_Match_ClaimClearedGroupOnOpenTable)
{
	// Needs WP-8 GroupCleared (E25/E32/E34 context: all stripes already off the table).
	const MatchConfig C = MakeMatchConfig(Discipline::EightBall);
	MatchState S;
	RB_REQUIRE(ToNormalShot(C, S, 1));
	for (int b = 9; b <= 15; ++b)
	{
		S.Game.Balls[b].Kind = BallStatusKind::OutOfPlay;
	}
	RB_CHECK(GetShotConstraints(C, S).MayClaimClearedGroup);
	ShotDeclaration D = Declare(ShotKind::Normal, 8, PocketId::FootLeft);
	RB_CHECK(ValidateDeclaration(C, S, D, nullptr) == ErrorCode::InvalidDeclaration); // unclaimed 8
	CompleteDeclaration(C, S, D);
	RB_CHECK(D.ClaimedClearedGroup == BallGroup::Stripes);
	RB_CHECK(ValidateDeclaration(C, S, D, nullptr) == ErrorCode::Ok);
	ShotDeclaration Wrong = D;
	Wrong.ClaimedClearedGroup = BallGroup::Solids; // solids are still on the table
	RB_CHECK(ValidateDeclaration(C, S, Wrong, nullptr) == ErrorCode::InvalidDeclaration);
}

RB_TEST(ARCH_PLACE1_PlacementOverPocketOrOverlapRejected)
{
	MatchConfig C = MakeMatchConfig(Discipline::NineBall);
	C.Table = NineFootTableWithPockets();
	C.Table.BallRadius[0] = 0.0301625; // oversized bar cue ball
	MatchState S;
	RB_REQUIRE(ToNormalShot(C, S, 1));
	S.Game.CueBall = CueBallNext::InHandAnywhere; // after a foul
	for (int b = 1; b <= 9; ++b)
	{
		S.Game.Balls[b].Kind = BallStatusKind::Pocketed;
	}
	Place(S.Game, 3, 0.2, 0.1);
	const ShotDeclaration Normal;

	const Vec2 Legal{-0.3, 0.2};
	const Vec2 OverCorner{-1.235, -0.600};
	const Vec2 OverSide{0.0, -0.600};
	const Vec2 OverlapsPerBall{0.2 + 0.058, 0.1};  // legal for two nominal balls (0.05715), not for R_cb + R_3 = 0.0587375
	const Vec2 ClearPerBall{0.2 + 0.0588, 0.1};
	RB_CHECK(ValidateDeclaration(C, S, Normal, &Legal) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Normal, &OverCorner) == ErrorCode::InvalidDeclaration);
	RB_CHECK(ValidateDeclaration(C, S, Normal, &OverSide) == ErrorCode::InvalidDeclaration);
	RB_CHECK(ValidateDeclaration(C, S, Normal, &OverlapsPerBall) == ErrorCode::InvalidDeclaration);
	RB_CHECK(ValidateDeclaration(C, S, Normal, &ClearPerBall) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Normal, nullptr) == ErrorCode::InvalidDeclaration); // in hand: placement required

	// In position: the cue ball must not be moved.
	S.Game.CueBall = CueBallNext::InPosition;
	RB_CHECK(ValidateDeclaration(C, S, Normal, nullptr) == ErrorCode::Ok);
	RB_CHECK(ValidateDeclaration(C, S, Normal, &Legal) == ErrorCode::InvalidDeclaration);

	// In hand above the head string: strictly above.
	S.Game.CueBall = CueBallNext::InHandAboveHeadString;
	const Vec2 OnString{-0.635, 0.2};
	const Vec2 Kitchen{-0.9, 0.2};
	RB_CHECK(ValidateDeclaration(C, S, Normal, &OnString) == ErrorCode::InvalidDeclaration);
	RB_CHECK(ValidateDeclaration(C, S, Normal, &Kitchen) == ErrorCode::Ok);

	// Sim input mode: free placement is not refused (it becomes foul 3.10 in the evaluation).
	C.Rules.Input = InputMode::Sim;
	RB_CHECK(ValidateDeclaration(C, S, Normal, &OnString) == ErrorCode::Ok);
}
