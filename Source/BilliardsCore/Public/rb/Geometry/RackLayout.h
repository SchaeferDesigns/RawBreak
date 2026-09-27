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
	// [OutlierMin, OutlierMax], otherwise max(0, Mean + Jitter (2u - 1)) (draw order: ApplyRackGaps).
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

	// Apex (row 0) center x for a rack whose anchor sits on the foot spot: FootSpotX for ApexOnFootSpot,
	// FootSpotX - 2 dx (= x_FS - sqrt(3) D) for CenterOnFootSpot, with the lattice row spacing dx of
	// BuildRackLattice. dx is (sqrt 3 / 2) D rounded UP to a multiple of 2^-44 m (< 6e-14 m): then
	// ApexX + 2 dx == FootSpotX exactly (|FootSpotX| < 256 m), so the anchor ball lies bitwise on the spot,
	// and no lattice distance is below D.
	RB_API double RackApexX(RackShape Shape, RackAnchor Anchor, double FootSpotX, double BallDiameter);

	// Site index (BuildRackLattice order) of the ball that sits on the foot spot: 0 for ApexOnFootSpot,
	// the (row 2, index 1) site for CenterOnFootSpot (4 for every shape).
	RB_API int RackAnchorSiteIndex(RackShape Shape, RackAnchor Anchor);

	// Frozen lattice: x = ApexX + row dx (dx = (sqrt 3 / 2) D, see RackApexX), y = (j - (n_row - 1)/2) D
	// (exactly antisymmetric in y), sites ordered by row then index. Out needs kMaxRackSites entries; returns
	// the site count (15, 10 or 9), or 0 for a null Out, an unknown shape, a non-finite ApexX or D <= 0. D is
	// the LARGEST diameter of the balls to be racked (worn bar balls, rules GenerateRack), so no pair can
	// overlap. The lattice is centred on y = 0 (callers add the long-string y).
	RB_API int BuildRackLattice(RackShape Shape, double ApexX, double BallDiameter, RackSite* Out);

	// Realistic micro-gaps (collisions 3.9.5), deterministic for a given Seed. The ball at AnchorIndex
	// (RackAnchorSiteIndex) keeps its lattice position bitwise (it stays exactly on the foot spot).
	// Algorithm (DECISION, section 15 of Docs/architecture.md; for 15 balls 30 target gaps vs 28 free
	// coordinates, rank 27 (a rotation about the anchor changes no gap), so gaps are realised in the
	// least-squares sense):
	//   1. Nominal contacts: input pairs with |p_i - p_j| <= D (1 + 1e-7), in canonical pair order (i < j,
	//      sorted by i then j). One target gap g_c per contact from rb::Rng(Seed), two draws per contact
	//      always (u_o, u): g = OutlierMin + (OutlierMax - OutlierMin) u if u_o < OutlierProbability, else
	//      Mean + Jitter (2u - 1); then g = max(0, g). If every g_c is 0 (kRackGapNone) the positions are
	//      left unchanged (the frozen lattice is the exact solution).
	//   2. Row expansion. Rows = balls grouped by x (within D/4), each sorted by y. Rows move away from the
	//      anchor row along x by the mean target gap of the contacts between consecutive rows times 2/sqrt(3)
	//      (cumulative); balls in a row move apart along y by the cumulative target gaps of their in-row
	//      contacts, the anchor row centred on the anchor ball, every other row on the midpoint of its extremes.
	//   3. Relaxation: 32 Gauss-Seidel sweeps over the contacts in canonical order, each projecting the pair
	//      onto |p_i - p_j| = D + g_c with each ball moving half of the correction (a pair with the anchor:
	//      the other ball moves all of it; the anchor never moves).
	//   4. Projection: sweeps over ALL pairs in canonical order separating any pair closer than D to
	//      D (1 + 1e-9) (5.7e-11 m for pool balls, below the physics touching tolerance), repeated until no pair
	//      is closer than D (at most 256 sweeps; tens are needed only for the outliers of kRackGapMixture). A
	//      rack still overlapping after that (never observed) is scaled about the anchor until its closest pair
	//      is D (1 + 1e-9).
	// Result: anchor unchanged, no pair closer than D. For the per-contact uniform presets (tight, wooden,
	// sloppy) the mean realised gap of a rack is within 20 % of its mean target gap (A-RACK-1; the per-contact
	// residual is the least-squares one, ~10-15 % of the mean). kRackGapMixture's isolated 0.1-0.5 mm outliers
	// are geometrically inconsistent with their zero-gap neighbours: the projection that removes the resulting
	// overlaps raises the realised mean by ~20-25 % on average. No allocation; ~10-100 us.
	// Positions in/out (Count entries, lattice order); invalid arguments (null, Count outside [2, 15], anchor
	// out of range, D <= 0) leave them unchanged.
	RB_API void ApplyRackGaps(Vec2* Positions, int Count, int AnchorIndex, double BallDiameter, const RackGapParams& Gaps, std::uint64_t Seed);

	// Number of pairs with |p_i - p_j| <= D + Tolerance (T-RACK-2: 30 for a frozen 15-ball rack).
	RB_API int CountTouchingPairs(const Vec2* Positions, int Count, double BallDiameter, double Tolerance);

	// Inner side of a perfectly tight rack device (T-RACK-7): (4 + sqrt3) D, (3 + sqrt3) D, (2 + 2/sqrt3) D.
	RB_API double RackInnerSide(RackShape Shape, double BallDiameter);
}
