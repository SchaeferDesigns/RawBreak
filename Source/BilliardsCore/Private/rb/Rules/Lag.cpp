#include "rb/Core/FpGuard.h"
// Owner: WP-9 (rules table procedures & match). Spec: rules.md 4.1.
#include "rb/Rules/Lag.h"

namespace rb::rules
{
	LagBallFacts DeriveLagBallFacts(const ShotRecord& /*Record*/, int /*Ball*/, const RulesTable& /*Table*/, const RulesTolerances& /*Tolerances*/)
	{
		// TODO(WP-9): bad-lag conditions (a)-(e) + stroke fouls, distance to the head cushion nose.
		return {};
	}

	LagResult EvaluateLag(const LagBallFacts& First, const LagBallFacts& Second, const RulesTolerances& /*Tolerances*/)
	{
		// TODO(WP-9): winner / re-lag (both bad or |d_A - d_B| <= eps_lag).
		LagResult Result;
		Result.First = First;
		Result.Second = Second;
		return Result;
	}

	void LagStartPositions(const RulesTable& Table, Vec2& First, Vec2& Second)
	{
		// TODO(WP-9): (x_HS - R - 0.01, -W/4) / (x_HS - R - 0.01, +W/4).
		First = Table.HeadSpot;
		Second = Table.HeadSpot;
	}
}
