#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.9; physics-collisions 6.2, 6.3.
#include "SimInternal.h"

namespace rb::sim
{
	bool ProcessRailTopEvent(Workspace& /*Ws*/, int /*Ball*/, const TableFeatureRef& /*Feature*/, double /*Time*/)
	{
		// TODO(WP-6b): GRI on the plane / edge; low bounce on the sloped cushion top -> rigid island with Plane and EdgeLine
		// features; settled on the flat cap -> analytic segment with SupportZ = RailTopZ; SupportExit routing (Seam ->
		// context, CushionBack -> rigid island, OuterEdge -> OffTable Floor via the boundary, cut -> Airborne); rest on
		// the cap -> OffTable(RestsOnRailOrFrame).
		return false;
	}
}
