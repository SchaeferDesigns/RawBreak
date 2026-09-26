// Owner: WP-9 (rules table procedures & match). rules.md 4.3 spotting, 4.4 spot request; tests G22-G26,
// G32, A-SPOT-1.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Math/Scalar.h"
#include "rb/Rules/Evaluate.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

RB_TEST(Rules_G22_SpotOnFreeFootSpot)
{
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::NineBall);
	Place(S, 0, -0.5, 0.3);  // cue ball off the long string
	Place(S, 4, 0.9, 0.2);   // object ball off the long string
	const Vec2 P = SpotBall(S, 9, Table, RulesTolerances{});
	RB_CHECK_NEAR(P.x, 0.635000, 1e-6);
	RB_CHECK_NEAR(P.y, 0.0, 1e-12);
}

RB_TEST(Rules_G23_SpotTouchingBallOnFootSpot)
{
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::NineBall);
	Place(S, 5, 0.635, 0.0);
	const Vec2 P = SpotBall(S, 9, Table, RulesTolerances{});
	RB_CHECK_NEAR(P.x, 0.692150, 1e-9);
	RB_CHECK_NEAR(P.y, 0.0, 1e-12);
}

RB_TEST(Rules_G24_SpotTangentToOffLineBall)
{
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::NineBall);
	Place(S, 5, 0.645, 0.020);
	const Vec2 P = SpotBall(S, 9, Table, RulesTolerances{});
	RB_CHECK_NEAR(P.x, 0.698536, 1e-6);
	RB_CHECK_NEAR(P.x, 0.645 + Sqrt(0.05715 * 0.05715 - 0.02 * 0.02), 1e-12);
	RB_CHECK_NEAR(P.y, 0.0, 1e-12);
	// exact tangency: the spotted ball touches ball 5
	RB_CHECK_NEAR(Length(P - S.Balls[5].Position), 2.0 * kR, 1e-12);
}

RB_TEST(Rules_G25_SpotKeepsGapToCueBall)
{
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::NineBall);
	Place(S, 0, 0.635, 0.0);
	const Vec2 P = SpotBall(S, 9, Table, RulesTolerances{});
	RB_CHECK_NEAR(P.x, 0.693150, 1e-9); // 2R + 1 mm gap
	RB_CHECK_NEAR(P.y, 0.0, 1e-12);
}

RB_TEST(Rules_G26_WholeFootSideBlockedSpotsTowardHead)
{
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	const int Ids[11] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	for (int k = 0; k <= 10; ++k)
	{
		Place(S, Ids[k], 0.635 + k * 0.05715, 0.0);
	}
	const Vec2 P = SpotBall(S, 3, Table, RulesTolerances{});
	RB_CHECK_NEAR(P.x, 0.577850, 1e-6); // touching the foot-spot ball on its head side
	RB_CHECK_NEAR(P.y, 0.0, 1e-12);
}

RB_TEST(Rules_Table_SpotBallsInGivenOrder)
{
	// Several balls: spotted one after another (ascending ids passed by the caller), each one sees the
	// previously spotted ones.
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	S.Balls[3].Kind = BallStatusKind::Pocketed;
	S.Balls[7].Kind = BallStatusKind::OutOfPlay;
	const BallId Order[2] = {3, 7};
	SpotBalls(S, Order, 2, Table, RulesTolerances{});
	RB_CHECK(S.Balls[3].Kind == BallStatusKind::OnTable);
	RB_CHECK(S.Balls[7].Kind == BallStatusKind::OnTable);
	RB_CHECK_NEAR(S.Balls[3].Position.x, 0.635, 1e-12);
	RB_CHECK_NEAR(S.Balls[7].Position.x, 0.635 + 2.0 * kR, 1e-12);
}

RB_TEST(Rules_G32_SpotRequestNearestHeadString)
{
	// 14.1 after B scratched: A has BIH above HS, all object balls above HS.
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	S.CueBall = CueBallNext::InHandAboveHeadString;
	Place(S, 3, -0.90, 0.10);
	Place(S, 12, -0.80, -0.20);
	const std::uint32_t AllOnTable = (1u << 3) | (1u << 12);
	const int Candidate = SpotRequestCandidate(S, AllOnTable, Table, RulesTolerances{});
	RB_CHECK(Candidate == 12); // 0.165 m from the head string
	const BallId Ball = static_cast<BallId>(Candidate);
	SpotBalls(S, &Ball, 1, Table, RulesTolerances{});
	RB_CHECK_NEAR(S.Balls[12].Position.x, 0.635, 1e-9);
	RB_CHECK_NEAR(S.Balls[12].Position.y, 0.0, 1e-12);
}

RB_TEST(Rules_G32_SpotRequestBlockedByBallOnHeadString)
{
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	S.CueBall = CueBallNext::InHandAboveHeadString;
	Place(S, 3, -0.90, 0.10);
	Place(S, 12, -0.635, -0.20); // on the head string: playable, no request
	const std::uint32_t Mask = (1u << 3) | (1u << 12);
	RB_CHECK(SpotRequestCandidate(S, Mask, Table, RulesTolerances{}) == -1);
	// not in hand above HS: no request
	S.Balls[12].Position = {-0.80, -0.20};
	RB_CHECK(SpotRequestCandidate(S, Mask, Table, RulesTolerances{}) == 12);
	S.CueBall = CueBallNext::InHandAnywhere;
	RB_CHECK(SpotRequestCandidate(S, Mask, Table, RulesTolerances{}) == -1);
	// only legal balls count: an illegal ball below the string does not block
	S.CueBall = CueBallNext::InHandAboveHeadString;
	Place(S, 5, 0.3, 0.0);
	RB_CHECK(SpotRequestCandidate(S, Mask, Table, RulesTolerances{}) == 12);
}

RB_TEST(Integ_Rules_G32_RequestSpotThroughMatch)
{
	// Needs WP-8 LegalFirstContactMask (14.1: every object ball on the table is legal).
	MatchConfig Config = MakeMatchConfig(Discipline::StraightPool);
	MatchState State;
	RB_REQUIRE(StartToFirstBreak(Config, State, 0));
	GameState& G = State.Game;
	for (int b = 1; b < kRulesBallCount; ++b)
	{
		G.Balls[b].Kind = BallStatusKind::Pocketed;
	}
	Place(G, 3, -0.90, 0.10);
	Place(G, 12, -0.80, -0.20);
	G.IsBreakShot = false;
	G.CueBall = CueBallNext::InHandAboveHeadString;
	G.Balls[0].Kind = BallStatusKind::Pocketed;
	RB_CHECK(GetShotConstraints(Config, State).MayRequestSpot);
	RB_REQUIRE(RequestSpot(Config, State) == ErrorCode::Ok);
	RB_CHECK_NEAR(State.Game.Balls[12].Position.x, 0.635, 1e-9);
	RB_CHECK_NEAR(State.Game.Balls[12].Position.y, 0.0, 1e-12);
	RB_CHECK(RequestSpot(Config, State) == ErrorCode::InvalidOption); // 12 is now below the head string
}

RB_TEST(ARCH_SPOT1_SpotNextToOversizedCueBall)
{
	// 60.325 mm cue ball (bar table) on the foot spot: the 9 keeps exactly delta_cbGap to it, no overlap.
	RulesTable Table = NineFootTable();
	Table.BallRadius[0] = 0.0603250 / 2.0;
	GameState S = EmptyState(Discipline::NineBall);
	Place(S, 0, 0.635, 0.0);
	const RulesTolerances Tol;
	const Vec2 P = SpotBall(S, 9, Table, Tol);
	const double Gap = Length(P - S.Balls[0].Position) - (Table.BallRadius[0] + Table.BallRadius[9]);
	RB_CHECK_NEAR(Gap, Tol.SpotCueBallGap, 1e-9);
	RB_CHECK(Gap > 0.0);
	RB_CHECK_NEAR(P.x, 0.635 + 0.0301625 + 0.028575 + 0.001, 1e-9);

	// Cue ball off the long string: tangent-plus-gap through the per-ball D = R_b + R_cb + gap.
	S.Balls[0].Position = {0.64, 0.03};
	const Vec2 Q = SpotBall(S, 9, Table, Tol);
	RB_CHECK_NEAR(Length(Q - S.Balls[0].Position), Table.BallRadius[0] + Table.BallRadius[9] + Tol.SpotCueBallGap, 1e-9);

	// Spotting a small ball next to object balls: exact tangency with per-ball radii.
	Table.BallRadius[4] = 0.0254;
	GameState T = EmptyState(Discipline::NineBall);
	Place(T, 4, 0.635, 0.0);
	const Vec2 U = SpotBall(T, 9, Table, Tol);
	RB_CHECK_NEAR(U.x, 0.635 + 0.0254 + 0.028575, 1e-12);
}
