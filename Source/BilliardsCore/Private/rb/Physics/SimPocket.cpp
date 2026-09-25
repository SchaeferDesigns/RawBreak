#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.9; physics-collisions 5.4, 6.1.
#include "SimInternal.h"

namespace rb::sim
{
	bool ProcessPocketEvent(Workspace& /*Ws*/, int /*Ball*/, const TableFeatureRef& /*Feature*/, double /*Time*/)
	{
		// TODO(WP-6b): pocket state machine (DropEdge -> pivot / fall, liner, rim torus, capture, exit, capture circle,
		// pivot truncation by facings / jaws / balls on the proxy).
		return false;
	}

	void RouteLanding(Workspace& /*Ws*/, int /*Ball*/, double /*Time*/)
	{
		// TODO(WP-6b): collisions 6.1 routing (capture circle -> PocketFall; annulus -> torus must have fired;
		// surface -> ResolveSlateImpact; behind a nose -> rail-top event must have fired: MissedEvents).
	}
}
