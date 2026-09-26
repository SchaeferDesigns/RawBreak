#pragma once

// Venue state seeded once per venue (human-factors 4.4, 4.5.5, 4.7; HF-23, HF-30, HF-41, HF-43, HF-50, HF-B11): the
// same venue seed gives the same house cues, ball set seed, table slopes and ball dirt on every visit. The result
// reaches the physics as a rb::TableCondition (MakePhysicsParams(Spec, Condition)) and the ball set seed.
// Owner: WP-11 (player model). Part of rb::human.

#include "rb/Config.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Human/CueState.h"
#include "rb/Human/TipState.h"
#include "rb/Math/Vec2.h"
#include "rb/Physics/Simulator.h"

#include <cstdint>

namespace rb::human
{
	enum class VenueKind : std::uint8_t
	{
		DiveBar,  // slope |s| U[0.5, 2.5] mm/m, ball cling 1.3, bar chalk cube, house-cue rack
		PoolHall, // |s| U[0, 0.3] mm/m
		Arena,    // |s| U[0, 0.1] mm/m
	};

	// Hash purposes of the venue seed (HashKeys(VenueSeed, Purpose, ...)).
	inline constexpr std::uint64_t kVenueSlopePurpose = 1;
	inline constexpr std::uint64_t kVenueHouseCuePurpose = 2;
	inline constexpr std::uint64_t kVenueBallSetPurpose = 3;

	// Table slope (4.5.5): magnitude uniform in the venue's range, direction uniform, from
	// HashKeys(VenueSeed, kVenueSlopePurpose, TableIndex, 0 / 1). The first table of a new career is capped at 1 mm/m
	// (fairness: 17 mm lag drift, 46 mm on a 0.5 m/s roll). Nightly drift after bumps is V2.
	RB_API Vec2 SeedTableSlope(std::uint64_t VenueSeed, int TableIndex, VenueKind Kind, bool FirstCareerTable);

	// k_venue of the venue's balls (HF-41): 1.3 dive bar, 1.0 otherwise (EST).
	RB_API double VenueBallCling(VenueKind Kind);

	// The physics view of one venue table: Slope from SeedTableSlope, BallCling from VenueBallCling, ChalkCling as the
	// game's HF-40 switch (V2, default off).
	RB_API TableCondition MakeVenueTableCondition(std::uint64_t VenueSeed, int TableIndex, VenueKind Kind, bool FirstCareerTable, bool ChalkCling);

	// Seed of BuildBallSet for the venue's ball set (HF-43): HashKeys(VenueSeed, kVenueBallSetPurpose, TableIndex).
	RB_API std::uint64_t VenueBallSetSeed(std::uint64_t VenueSeed, int TableIndex);

	// ---------------------------------------------------------------------------------------------
	// House cues (4.4, HF-30): the wall rack of a venue, same defects on every visit until the bar "replaces" a cue
	// ---------------------------------------------------------------------------------------------

	inline constexpr int kMaxHouseCues = 16;

	struct HouseCue
	{
		CueSpec Spec;           // mass U{18, 19, 20, 21} oz, 57 in (short cues 48 / 52 in), m / m_e = 15; tip fields = Tip's
		CueBodyState Body;      // bow s_w: 60 % < 0.5 mm, 30 % 0.5-2 mm, 10 % 2-5 mm; WarpKnown = false until noticed
		TipState Tip;           // w_tip 11-13 mm, r_dome 12-20 mm, glaze 0.3-0.9, overhang 0-1 mm, 10 % loose, e_tip 0.68-0.72:
		                        //   AUTHORITATIVE (ExecuteStroke writes mu, r_dome, e_tip from TipState into the strike); SeedHouseCue
		                        //   sets Spec.TipRestitution / TipDomeRadius equal so that both views agree
		bool Short = false;     // 48 / 52 in cue near a wall (HF-32)
	};

	// One rack slot: HashKeys(VenueSeed, kVenueHouseCuePurpose, RackSlot, Generation, field) per drawn field. The bar
	// "replacing" the cue (career event) = the next Generation.
	RB_API HouseCue SeedHouseCue(std::uint64_t VenueSeed, int RackSlot, std::uint32_t Generation);

	// The bar's chalk cube (HF-23): RailRat grade, cap 0.7.
	constexpr ChalkCube BarChalkCube() { return {ChalkGrade::RailRat, 0.0, true}; }
}
