#pragma once

// Cue equipment presets (equipment 8, physics-motion-and-cue B.9). Header-only data.
// Owner: WP-2 (equipment & table geometry).

#include "rb/Config.h"
#include "rb/Core/Constants.h"

#include <cstdint>

namespace rb
{
	struct CueSpec
	{
		double Mass = 19.0 * kOunce;        // M [kg] whole cue (the stress wave crosses it within the contact time)
		double EndMass = 0.0085;            // m_e [kg] effective shaft end mass (squirt, B.7); m/m_e = 20 for m = 0.170
		double TipRestitution = 0.73;       // e_tip [1]: leather 0.71-0.75, phenolic 0.81-0.87
		double TipFriction = 0.6;           // mu_tip [1] well chalked; ~0.35-0.4 worn (miscue limit B.4)
		double TipFrictionKinetic = 0.6;    // mu_tip,k [1] miscue branch (B.5), TUNING 0.3-0.6
		double TipDomeRadius = 0.0106;      // r_tip [m] (nickel shape; motion spec uses ~R/3 = 9.5 mm)
		double TipDiameter = 0.01275;       // [m]
		double Length = 1.4732;             // [m]
		double ContactTime = 1.0e-3;        // [s] tip-ball contact duration (0.8 ms hard/break .. 1.5 ms soft), records/audio
		double FollowThroughDistance = 0.12;// [m] TUNING (architecture decision): the cue decelerates uniformly to rest over
		                                    //     this distance after impact; used to detect tip re-contacts (double hit / push)
		bool JumpCue = false;               // selects the jump-cue pinch schedule (B.8.2)
	};

	enum class CuePreset : std::uint8_t
	{
		Playing19oz, // standard playing cue, leather tip
		Break21oz,   // phenolic break tip
		Jump9oz,     // light jump cue (motion spec default 9 oz, TUNING; equipment lists ~10 oz)
		House19oz,   // bar house cue (worn tip, thicker shaft), ESTIMATE
	};

	inline constexpr CueSpec kCuePlaying19oz{19.0 * kOunce, 0.0085, 0.73, 0.6, 0.6, 0.0106, 0.01275, 1.4732, 1.0e-3, 0.12, false};
	inline constexpr CueSpec kCueBreak21oz{21.0 * kOunce, 0.170 / 15.0, 0.85, 0.6, 0.6, 0.0106, 0.0135, 1.4732, 0.8e-3, 0.15, false};
	inline constexpr CueSpec kCueJump9oz{9.0 * kOunce, 0.170 / 15.0, 0.85, 0.6, 0.6, 0.0106, 0.01375, 1.016, 0.8e-3, 0.08, true};
	inline constexpr CueSpec kCueHouse19oz{19.0 * kOunce, 0.170 / 15.0, 0.71, 0.5, 0.5, 0.0106, 0.0125, 1.4478, 1.2e-3, 0.12, false};

	constexpr CueSpec GetCueSpec(CuePreset Preset)
	{
		switch (Preset)
		{
		case CuePreset::Break21oz: return kCueBreak21oz;
		case CuePreset::Jump9oz: return kCueJump9oz;
		case CuePreset::House19oz: return kCueHouse19oz;
		case CuePreset::Playing19oz: break;
		}
		return kCuePlaying19oz;
	}
}
