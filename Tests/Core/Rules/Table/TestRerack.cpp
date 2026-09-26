// Owner: WP-9 (rules table procedures & match). rules.md 5.2 14.1 rack outline, 9.5 continuation
// re-racks (Table 1); tests K05, S12-S18, S21, S23.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Math/Scalar.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

namespace
{
	RackCommand Plan14(Vec2 Cue, Vec2 Fifteenth, int FifteenthBall = 15)
	{
		return PlanRerack14(Cue, FifteenthBall, Fifteenth, NineFootTable(), RulesTolerances{});
	}
}

RB_TEST(Rules_K05_StraightPoolOutlineVertices)
{
	const RackOutline O = StraightPoolRackOutline(NineFootTable());
	RB_CHECK_NEAR(O.Apex.x, 0.577850, 1e-6);
	RB_CHECK_NEAR(O.Apex.y, 0.0, 1e-12);
	RB_CHECK_NEAR(O.BackLeft.x, 0.861548, 1e-6);
	RB_CHECK_NEAR(O.BackLeft.y, 0.163793, 1e-6);
	RB_CHECK_NEAR(O.BackRight.x, 0.861548, 1e-6);
	RB_CHECK_NEAR(O.BackRight.y, -0.163793, 1e-6);
	RB_CHECK_NEAR(Length(O.BackLeft - O.Apex), 0.327587, 1e-6);
	RB_CHECK_NEAR(Length(O.BackLeft - O.BackRight), 0.327587, 1e-6);
}

RB_TEST(Rules_K05_OutlineInterference)
{
	const RulesTable Table = NineFootTable();
	RB_CHECK(InterferesWithRack({0.552133, 0.0}, kR, Table));  // 0.9 R from the apex vertex
	RB_CHECK(!InterferesWithRack({0.546418, 0.0}, kR, Table)); // 1.1 R
	RB_CHECK(InterferesWithRack({0.70, 0.05}, kR, Table));     // inside
	RB_CHECK(InterferesWithRack({0.635, 0.0}, kR, Table));     // foot spot
	RB_CHECK(!InterferesWithRack({0.0, 0.3}, kR, Table));
	RB_CHECK(!InterferesWithRack({0.95, 0.0}, kR, Table));     // behind the back edge (0.861548 + R < 0.95)
	RB_CHECK(InterferesWithRack({0.88, 0.0}, kR, Table));      // overlaps the back edge
	// per-ball radius: the same center interferes for a bigger ball
	RB_CHECK(!InterferesWithRack({0.546418, 0.0}, kR, Table));
	RB_CHECK(InterferesWithRack({0.546418, 0.0}, 0.0320, Table));
}

RB_TEST(Rules_Table_BlocksSpotStrictOverlap)
{
	const Vec2 Head{-0.635, 0.0};
	RB_CHECK(BlocksSpot(Head, kR, {-0.64, 0.01}, kR));
	RB_CHECK(!BlocksSpot(Head, kR, {-0.635 + 2.0 * kR + 1e-9, 0.0}, kR)); // just beyond tangency: not blocking
	RB_CHECK(!BlocksSpot(Head, kR, {-0.9, 0.3}, kR));
}

RB_TEST(Rules_S12_NeitherInterferesBothStay)
{
	const RackCommand C = Plan14({-0.9, -0.2}, {0.0, 0.3});
	RB_CHECK(C.Kind == RackCommandKind::Rerack14);
	RB_CHECK(C.FifteenthBall == 15);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::Keep);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::Keep);
}

RB_TEST(Rules_S13_FifteenthInRackToHeadSpot)
{
	const RackCommand C = Plan14({-0.2, 0.3}, {0.70, 0.05});
	RB_CHECK(C.Kind == RackCommandKind::Rerack14);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::ToHeadSpot);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::Keep);
}

RB_TEST(Rules_S14_FifteenthInRackHeadSpotBlockedToCenterSpot)
{
	// CB 0.0112 m from the head spot blocks it.
	const RackCommand C = Plan14({-0.64, 0.01}, {0.70, 0.05});
	RB_CHECK(C.Kind == RackCommandKind::Rerack14);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::ToCenterSpot);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::Keep);
}

RB_TEST(Rules_S15_CueInRackFifteenthBelowHeadStringCueInHand)
{
	const RackCommand C = Plan14({0.75, 0.0}, {-0.2, 0.3});
	RB_CHECK(C.Kind == RackCommandKind::Rerack14);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::Keep);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::InHandAboveHeadString);
}

RB_TEST(Rules_S16_CueInRackFifteenthAboveHeadStringCueToHeadSpot)
{
	const RackCommand C = Plan14({0.75, 0.0}, {-0.9, 0.3});
	RB_CHECK(C.Kind == RackCommandKind::Rerack14);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::Keep);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::ToHeadSpot);
}

RB_TEST(Rules_S17_CueInRackFifteenthBlocksHeadSpotCueToCenterSpot)
{
	// 15th 0.032 m from the head spot.
	const RackCommand C = Plan14({0.75, 0.0}, {-0.66, 0.02});
	RB_CHECK(C.Kind == RackCommandKind::Rerack14);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::Keep);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::ToCenterSpot);
}

RB_TEST(Rules_S18_BothInRackRerackAllFifteen)
{
	const RackCommand C = Plan14({0.75, 0.0}, {0.70, 0.05});
	RB_CHECK(C.Kind == RackCommandKind::Rerack15);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::IntoRack);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::InHandAboveHeadString);
}

RB_TEST(Rules_S23_FifteenthOnHeadStringCountsAsBelow)
{
	const RackCommand C = Plan14({0.75, 0.0}, {-0.635, 0.3});
	RB_CHECK(C.Kind == RackCommandKind::Rerack14);
	RB_CHECK(C.FifteenthBallPlacement == PlacementCommand::Keep);
	RB_CHECK(C.CueBallPlacement == PlacementCommand::InHandAboveHeadString);
}

RB_TEST(Rules_Table_Rerack15PlanAfterFifteenthPocketed)
{
	// Supporting check for WP-8's S19 (its evaluator calls this plan): CB away from the rack stays,
	// CB in the rack goes in hand above the head string.
	const RulesTable Table = NineFootTable();
	const RackCommand Stay = PlanRerack15AfterFifteenthPocketed({-0.2, 0.3}, Table);
	RB_CHECK(Stay.Kind == RackCommandKind::Rerack15);
	RB_CHECK(Stay.FifteenthBall == kNoBall);
	RB_CHECK(Stay.CueBallPlacement == PlacementCommand::Keep);
	const RackCommand InHand = PlanRerack15AfterFifteenthPocketed({0.75, 0.0}, Table);
	RB_CHECK(InHand.Kind == RackCommandKind::Rerack15);
	RB_CHECK(InHand.CueBallPlacement == PlacementCommand::InHandAboveHeadString);
}

RB_TEST(Rules_S21_SpotOntoEmptyApexOfFourteenBallRack)
{
	// Right after a 14-ball re-rack (apex empty): a spotted ball goes onto the empty apex (pitfall 26:
	// the two second-row balls are exactly 2R from the foot spot).
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	const RackAssignment Rack = HandRack(1, 14, true);
	for (int b = 1; b <= 14; ++b)
	{
		RB_REQUIRE(Rack.Racked[b]);
		Place(S, b, Rack.Position[b].x, Rack.Position[b].y);
	}
	Place(S, 15, 0.0, 0.3);
	Place(S, 0, -0.9, -0.2);
	S.Balls[6].Kind = BallStatusKind::Pocketed; // A fouls and pockets ball 6
	const Vec2 P = SpotBall(S, 6, Table, RulesTolerances{});
	RB_CHECK_NEAR(P.x, 0.635, 1e-9);
	RB_CHECK_NEAR(P.y, 0.0, 1e-12);
}
