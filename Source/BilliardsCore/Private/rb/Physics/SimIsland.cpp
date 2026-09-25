#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.8; physics-collisions 3.6-3.9, 6.2, 7.3.
#include "SimInternal.h"

namespace rb::sim
{
	void StartIsland(Workspace& /*Ws*/, const IslandSeed& /*Seed*/, double /*Time*/)
	{
		// TODO(WP-6b): BFS over balls (delta_cl) and table features (QueryTableFeatures), IslandFeature construction from
		// TableGeometry, member hand-off at Time, tips as IslandTip, IslandBegin, Zeno history reset of member pairs.
	}

	bool AdvanceIsland(Workspace& /*Ws*/, double /*UntilTime*/)
	{
		// TODO(WP-6b): steps; joining of balls AND features each step (reach bound); member exits (drop edge, cloth
		// region, rail top); records -> BallBall / BallCushion / BallJaw / TipContact events; observers of members;
		// adaptive Sampled recording; SustainedContact / CompliantMaxDuration -> Rigid (IslandRigid); CanExit / rest ->
		// exit; MaxIslandSteps budget.
		return true;
	}

	double NextIslandStepTime(const Workspace& Ws)
	{
		// TODO(WP-6b): Solver.Time() + current step size while active.
		return Ws.Island.Active ? Ws.Island.Solver.Time() : kInfinity;
	}
}
