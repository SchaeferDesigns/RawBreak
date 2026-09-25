#pragma once

// Rack lattice geometry and realistic micro-gaps (equipment 9, physics-collisions 3.9.5). Which ball
// sits on which site is a RULES decision (rb/Rules/TableRules.h GenerateRack).
// Owner: WP-2 (equipment & table geometry).

#include "rb/Config.h"
#include "rb/Math/Vec2.h"

#include <cstdint>

namespace rb
{
	inline constexpr int kMaxRackSites = 15;

	enum class RackShape : std::uint8_t
	{
		Triangle15, // rows 1,2,3,4,5 (8-ball, 14.1, Blackball)
		Triangle10, // rows 1,2,3,4 (10-ball)
		Diamond9,   // rows 1,2,3,2,1 (9-ball)
	};

	enum class RackAnchor : std::uint8_t
	{
		ApexOnFootSpot,   // apex (row 0) ball center on the foot spot (8-ball, 10-ball, 14.1, legacy 9-ball)
		CenterOnFootSpot, // third-row center ball (row 2, index 1) on the foot spot (WPA 2025 9-ball, Blackball)
	};

	struct RackSite
	{
		int Row = 0;    // 0 = apex (toward the head), rows grow toward +x
		int Index = 0;  // 0 .. n_row - 1, increasing with y
		Vec2 Position;  // frozen-lattice center [m]
	};

	// Per-contact micro-gap distribution (collisions 3.9.5 presets, ESTIMATE; prior-art 5.6 mixture).
	// A gap g >= 0 is drawn per nominally touching pair: with probability OutlierProbability uniform in
	// [OutlierMin, OutlierMax], otherwise max(0, Mean + Jitter (2u - 1)).
	struct RackGapParams
	{
		double Mean = 0.0;               // [m]
		double Jitter = 0.0;             // [m] half-width of the uniform jitter around Mean (clamped at 0)
		double OutlierProbability = 0.0; // [1] share of "loose" contacts (prior-art 5.6: a few 0.1-0.5 mm gaps)
		double OutlierMin = 0.0;         // [m]
		double OutlierMax = 0.0;         // [m]
	};

	// DECISION (Docs/architecture.md section 15): the collisions 3.9.5 uniform presets are the game presets;
	// the prior-art 5.6 mixture is an extra preset for break statistics (BRK tests) and rack-quality tuning.
	inline constexpr RackGapParams kRackGapNone{0.0, 0.0, 0.0, 0.0, 0.0};
	inline constexpr RackGapParams kRackGapTightTemplate{0.005e-3, 0.005e-3, 0.0, 0.0, 0.0};
	inline constexpr RackGapParams kRackGapWoodenRack{0.02e-3, 0.02e-3, 0.0, 0.0, 0.0};
	inline constexpr RackGapParams kRackGapSloppyBar{0.08e-3, 0.08e-3, 0.0, 0.0, 0.0};
	inline constexpr RackGapParams kRackGapMixture{0.0, 0.02e-3, 0.1, 0.1e-3, 0.5e-3}; // prior-art 5.6 (ESTIMATE)

	// Apex (row 0) center x for a rack whose anchor sits on the foot spot: row spacing (sqrt 3 / 2) D.
	RB_API double RackApexX(RackShape Shape, RackAnchor Anchor, double FootSpotX, double BallDiameter);

	// Site index (BuildRackLattice order) of the ball that sits on the foot spot: 0 for ApexOnFootSpot,
	// the (row 2, index 1) site for CenterOnFootSpot (4 for every shape).
	RB_API int RackAnchorSiteIndex(RackShape Shape, RackAnchor Anchor);

	// Frozen lattice: x = ApexX + row (sqrt 3 / 2) D, y = (j - (n_row - 1)/2) D, sites ordered by row
	// then index. Out needs kMaxRackSites entries; returns the site count (15, 10 or 9). D is the LARGEST
	// diameter of the balls to be racked (worn bar balls, rules GenerateRack), so no pair can overlap.
	RB_API int BuildRackLattice(RackShape Shape, double ApexX, double BallDiameter, RackSite* Out);

	// Realistic micro-gaps (collisions 3.9.5), deterministic for a given Seed. The ball at AnchorIndex
	// (RackAnchorSiteIndex) keeps its lattice position (it stays exactly on the foot spot).
	// Algorithm (DECISION, section 15 of Docs/architecture.md; 30 target gaps vs 28 free coordinates for 15
	// balls, so gaps are realised in the least-squares sense):
	//   1. Draw one target gap g_c per nominally touching pair c in canonical pair order (i < j, sorted by
	//      i then j) with rb::Rng(Seed).
	//   2. Row expansion: rows move away from the anchor row along x by the mean target gap of the
	//      contacts between consecutive rows times 2/sqrt(3); balls in a row move apart along y by the
	//      target gaps of their in-row contacts (anchor row centered on the anchor ball).
	//   3. Relaxation: 32 Gauss-Seidel sweeps over the pairs in canonical order moving both balls of a
	//      pair (never the anchor) half-way toward |p_i - p_j| = D + g_c.
	//   4. Projection: one sweep that separates any pair closer than D to exactly D (anchor fixed).
	// Positions in/out (Count entries, lattice order).
	RB_API void ApplyRackGaps(Vec2* Positions, int Count, int AnchorIndex, double BallDiameter, const RackGapParams& Gaps, std::uint64_t Seed);

	// Number of pairs with |p_i - p_j| <= D + Tolerance (T-RACK-2: 30 for a frozen 15-ball rack).
	RB_API int CountTouchingPairs(const Vec2* Positions, int Count, double BallDiameter, double Tolerance);

	// Inner side of a perfectly tight rack device (T-RACK-7): (4 + sqrt3) D, (3 + sqrt3) D, (2 + 2/sqrt3) D.
	RB_API double RackInnerSide(RackShape Shape, double BallDiameter);
}
