#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 9; physics-collisions 3.9.5.
#include "rb/Geometry/RackLayout.h"

namespace rb
{
	double RackApexX(RackShape /*Shape*/, RackAnchor /*Anchor*/, double FootSpotX, double /*BallDiameter*/)
	{
		// TODO(WP-2): apex x for ApexOnFootSpot / CenterOnFootSpot (x_FS - sqrt(3) D).
		return FootSpotX;
	}

	int RackAnchorSiteIndex(RackShape /*Shape*/, RackAnchor Anchor)
	{
		// TODO(WP-2): 0 for ApexOnFootSpot; the (row 2, index 1) site (4) for CenterOnFootSpot.
		return Anchor == RackAnchor::ApexOnFootSpot ? 0 : 4;
	}

	int BuildRackLattice(RackShape /*Shape*/, double /*ApexX*/, double /*BallDiameter*/, RackSite* /*Out*/)
	{
		// TODO(WP-2): frozen lattice (equipment 9.2).
		return 0;
	}

	void ApplyRackGaps(Vec2* /*Positions*/, int /*Count*/, int /*AnchorIndex*/, double /*BallDiameter*/, const RackGapParams& /*Gaps*/,
		std::uint64_t /*Seed*/)
	{
		// TODO(WP-2): seeded per-contact micro-gaps (collisions 3.9.5, algorithm in RackLayout.h); anchor ball fixed.
	}

	int CountTouchingPairs(const Vec2* /*Positions*/, int /*Count*/, double /*BallDiameter*/, double /*Tolerance*/)
	{
		// TODO(WP-2): T-RACK-2 helper.
		return 0;
	}

	double RackInnerSide(RackShape /*Shape*/, double /*BallDiameter*/)
	{
		// TODO(WP-2): T-RACK-7 inner side lengths.
		return 0.0;
	}
}
