#pragma once

// Cue body state: bow (warp) and its orientation in the hand, taper for the executed-pose clearance tests
// (human-factors 4.4, 3.6; HF-30..HF-34). The physics cue (mass, end mass, length, tip restitution / friction /
// dome radius) is rb::CueSpec (rb/Equipment/Cue.h); ExecuteStroke fills the tip fields per stroke from TipState.
// Owner: WP-11 (player model). Part of rb::human.

#include "rb/Config.h"
#include "rb/Equipment/Cue.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"

#include <cstdint>

namespace rb::human
{
	struct CueBodyState
	{
		double BowSag = 0.0;           // s_w [m] sag of the circular bow over the cue length (0 = straight)
		bool WarpKnown = true;         // the shooter noticed the bow (roll test) and holds it up: chi = 0 (4.4)
		double TipRadius = 0.0065;     // r_t [m] cue radius at the tip (UE 5.5 taper r(s) = r_t + (r_b - r_t) s / L)
		double ButtRadius = 0.0159;    // r_b [m]
		double FerruleLength = 0.025;  // [m] s* <= this: Ferrule contact (3.6)
		double ShaftLength = 0.74;     // [m] s* <= this: Shaft contact, beyond: Butt
	};

	// Warp of one pickup (4.4, DERIVED): gamma = 4 s_w s_e / L^2 (k_w = s_e / L), chi = 0 if WarpKnown else
	// 2 pi U01(HashKeys(MatchSeed, S, 10, CuePickupIndex)); azimuth error gamma sin(chi) (toward the left for chi = +90 deg),
	// elevation error gamma cos(chi) (HF-T10). Equipment: neither scaled by NoiseScale nor affected by ChannelMask (masking
	// channel 10 must not turn a sideways bow into an elevation error: the diagnosis removes the warp only in step 1,
	// BowSag = 0, so every step stays a single change, 3.9). Rollout keys draw chi with their own shooter key S.
	struct WarpEffect
	{
		double Gamma = 0.0;          // [rad]
		double Roll = 0.0;           // chi [rad]
		double AzimuthError = 0.0;   // dphi_w [rad]
		double ElevationError = 0.0; // dth_w [rad]
	};

	RB_API WarpEffect ComputeWarp(const CueBodyState& Cue, const CueSpec& Spec, const NoiseKey& Key, const HumanParams& Params);

	// Smallest bow the automatic pickup roll test notices: 1 mm at WarpCheck habit 0 -> 0.3 mm at 1, linear (HF-31, TUNING).
	RB_API double NoticeableBow(double WarpCheckHabit);

	// Result of the automatic 1-2 s pickup roll test (A mode, always done): BowSag >= NoticeableBow(habit). In R mode the
	// player judges the wobble and the game sets WarpKnown. A hidden warp is therefore at most 1 mm (4.4).
	RB_API bool AutoRollTestNotices(const CueBodyState& Cue, double WarpCheckHabit);
}
