// Owner: WP-9 (rules table procedures & match). F11 cue-ball placement predicates (rules.md 3.5 F11,
// 4.4, 16.16): pocket openings, per-ball overlaps, strict regions. (G27 itself is WP-8's evaluator test.)

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

RB_TEST(Rules_Table_OverPocketOpening)
{
	const RulesTable NoPockets = NineFootTable();
	RB_CHECK(!OverPocketOpening({-1.26, -0.625}, NoPockets)); // PocketCount == 0: never

	const RulesTable T = NineFootTableWithPockets();
	RB_CHECK(!OverPocketOpening({0.0, 0.0}, T));
	RB_CHECK(!OverPocketOpening({-0.635, 0.3}, T));
	// beyond the corner mouth line of P0, between the jaw points
	RB_CHECK(OverPocketOpening({-1.235, -0.600}, T));
	// just inside the P0 mouth line
	RB_CHECK(!OverPocketOpening({-1.20, -0.58}, T));
	// every corner by symmetry
	RB_CHECK(OverPocketOpening({1.235, -0.600}, T));
	RB_CHECK(OverPocketOpening({1.235, 0.600}, T));
	RB_CHECK(OverPocketOpening({-1.235, 0.600}, T));
	// inside a side pocket's drop-edge circle, in front of the mouth line
	RB_CHECK(OverPocketOpening({0.0, -0.600}, T));
	RB_CHECK(OverPocketOpening({0.01, 0.600}, T));
	RB_CHECK(!OverPocketOpening({0.0, -0.590}, T));
	// along a rail far from any pocket: not over an opening
	RB_CHECK(!OverPocketOpening({-0.635, -0.606}, T));
}

RB_TEST(Rules_Table_CueBallPlacementRegionAndBounds)
{
	const RulesTable T = NineFootTable();
	const RulesTolerances Tol;
	const GameState S = EmptyState(Discipline::EightBall);
	// strictly above the head string (G27 geometry)
	RB_CHECK(!CueBallPlacementLegal(S, {-0.635, 0.0}, CueBallNext::InHandAboveHeadString, T, Tol));
	RB_CHECK(!CueBallPlacementLegal(S, {-0.635 - 0.5e-6, 0.0}, CueBallNext::InHandAboveHeadString, T, Tol));
	RB_CHECK(CueBallPlacementLegal(S, {-0.636, 0.0}, CueBallNext::InHandAboveHeadString, T, Tol));
	RB_CHECK(CueBallPlacementLegal(S, {-0.635, 0.0}, CueBallNext::InHandAnywhere, T, Tol));
	// baulk (Blackball): x < -0.762 - eps
	RB_CHECK(CueBallPlacementLegal(S, {-0.80, 0.1}, CueBallNext::InHandBaulk, T, Tol));
	RB_CHECK(!CueBallPlacementLegal(S, {-0.70, 0.1}, CueBallNext::InHandBaulk, T, Tol));
	// playing-surface bounds |x| <= L/2 - R, |y| <= W/2 - R
	RB_CHECK(CueBallPlacementLegal(S, {1.27 - kR, 0.635 - kR}, CueBallNext::InHandAnywhere, T, Tol));
	RB_CHECK(!CueBallPlacementLegal(S, {1.27 - kR + 1e-6, 0.0}, CueBallNext::InHandAnywhere, T, Tol));
	RB_CHECK(!CueBallPlacementLegal(S, {0.0, -0.635 + kR - 1e-6}, CueBallNext::InHandAnywhere, T, Tol));
}

RB_TEST(Rules_Table_CueBallPlacementOverlapPerBallRadius)
{
	RulesTable T = NineFootTable();
	const RulesTolerances Tol;
	GameState S = EmptyState(Discipline::NineBall);
	Place(S, 3, 0.2, 0.1);
	S.Balls[0].Kind = BallStatusKind::OnTable; // the cue ball itself never blocks its own placement
	S.Balls[0].Position = {0.2, 0.1};
	const double Touch = 2.0 * kR;
	RB_CHECK(CueBallPlacementLegal(S, {0.2 + Touch, 0.1}, CueBallNext::InHandAnywhere, T, Tol));
	RB_CHECK(CueBallPlacementLegal(S, {0.2 + Touch - 0.5e-7, 0.1}, CueBallNext::InHandAnywhere, T, Tol));
	RB_CHECK(!CueBallPlacementLegal(S, {0.2 + Touch - 2e-7, 0.1}, CueBallNext::InHandAnywhere, T, Tol));
	// oversized cue ball: the nominal-radius placement now overlaps
	T.BallRadius[0] = 0.0301625;
	RB_CHECK(!CueBallPlacementLegal(S, {0.2 + Touch, 0.1}, CueBallNext::InHandAnywhere, T, Tol));
	RB_CHECK(CueBallPlacementLegal(S, {0.2 + kR + 0.0301625, 0.1}, CueBallNext::InHandAnywhere, T, Tol));
	// balls not on the table do not block
	S.Balls[3].Kind = BallStatusKind::Pocketed;
	RB_CHECK(CueBallPlacementLegal(S, {0.2, 0.1}, CueBallNext::InHandAnywhere, T, Tol));
}

RB_TEST(Rules_Table_CueBallPlacementNotOverPocket)
{
	const RulesTable T = NineFootTableWithPockets();
	const GameState S = EmptyState(Discipline::NineBall);
	RB_CHECK(!CueBallPlacementLegal(S, {-1.235, -0.600}, CueBallNext::InHandAnywhere, T, RulesTolerances{}));
	RB_CHECK(!CueBallPlacementLegal(S, {0.0, -0.600}, CueBallNext::InHandAnywhere, T, RulesTolerances{}));
	RB_CHECK(CueBallPlacementLegal(S, {0.3, -0.600}, CueBallNext::InHandAnywhere, T, RulesTolerances{}));
}
