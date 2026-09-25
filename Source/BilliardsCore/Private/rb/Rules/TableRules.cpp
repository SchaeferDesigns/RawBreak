#include "rb/Core/FpGuard.h"
// Owner: WP-9 (rules table procedures & match). Spec: rules.md 4.3, 4.4, 5, 9.5.
#include "rb/Rules/TableRules.h"

namespace rb::rules
{
	ErrorCode GenerateRack(Discipline /*Game*/, const RulesConfig& /*Config*/, const RulesTable& /*Table*/, std::uint64_t /*Seed*/, bool /*ApexEmpty*/,
		const RackGapParams& /*Gaps*/, RackAssignment& Out)
	{
		// TODO(WP-9): fill rules per discipline (5.1), seeded permutation, lattice (largest racked radius) + ApplyRackGaps with
		// the anchor site fixed (WP-2).
		Out = RackAssignment{};
		return ErrorCode::NotImplemented;
	}

	Vec2 SpotBall(const GameState& /*State*/, int /*Ball*/, const RulesTable& Table, const RulesTolerances& /*Tolerances*/)
	{
		// TODO(WP-9): exact interval algorithm of 4.3 with per-ball radii D_j = R_b + R_j (+ gap for the CB), pitfall 26 tolerance.
		return Table.FootSpot;
	}

	void SpotBalls(GameState& /*State*/, const BallId* /*Balls*/, int /*Count*/, const RulesTable& /*Table*/, const RulesTolerances& /*Tolerances*/)
	{
		// TODO(WP-9): spot in the given order, updating State.
	}

	int SpotRequestCandidate(const GameState& /*State*/, std::uint32_t /*LegalMask*/, const RulesTable& /*Table*/, const RulesTolerances& /*Tolerances*/)
	{
		// TODO(WP-9): 4.4 spot request.
		return -1;
	}

	RackOutline StraightPoolRackOutline(const RulesTable& /*Table*/)
	{
		// TODO(WP-9): 5.2 outline vertices.
		return {};
	}

	bool InterferesWithRack(const Vec2& /*P*/, double /*Radius*/, const RulesTable& /*Table*/)
	{
		// TODO(WP-9): dist(P, outline triangle) < Radius (the ball's own radius).
		return false;
	}

	bool BlocksSpot(const Vec2& /*Spot*/, double /*SpotRadius*/, const Vec2& /*Other*/, double /*OtherRadius*/)
	{
		// TODO(WP-9): |Other - Spot| < SpotRadius + OtherRadius.
		return false;
	}

	RackCommand PlanRerack14(const Vec2& /*CueBall*/, int /*FifteenthBall*/, const Vec2& /*FifteenthBallPosition*/, const RulesTable& /*Table*/,
		const RulesTolerances& /*Tolerances*/)
	{
		// TODO(WP-9): 9.5 Table 1.
		return {};
	}

	RackCommand PlanRerack15AfterFifteenthPocketed(const Vec2& /*CueBall*/, const RulesTable& /*Table*/)
	{
		// TODO(WP-9): all 15 racked; CB in hand above HS if it interferes, else keep.
		return {};
	}

	bool OverPocketOpening(const Vec2& /*P*/, const RulesTable& /*Table*/)
	{
		// TODO(WP-9): beyond a mouth line between the jaw points, or inside a drop-edge circle; false without pockets.
		return false;
	}

	bool CueBallPlacementLegal(const GameState& /*State*/, const Vec2& /*P*/, CueBallNext /*Region*/, const RulesTable& /*Table*/,
		const RulesTolerances& /*Tolerances*/)
	{
		// TODO(WP-9): F11 with per-ball radii, pocket openings and the region predicate.
		return true;
	}
}
