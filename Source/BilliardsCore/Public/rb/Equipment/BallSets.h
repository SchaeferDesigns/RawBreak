#pragma once

// Ball set presets with per-ball radius, mass and inertia (equipment 6).
// Owner: WP-2 (equipment & table geometry).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Physics/BallState.h"

#include <cstdint>

namespace rb
{
	enum class BallSetPreset : std::uint8_t
	{
		StandardPool,       // 16 x (57.15 mm, 6 oz) - WPA / Super Aramith Pro, pooltool parity
		DiveBar,            // magnetic 167 g cue ball; object balls m ~ N(0.163, 0.003) clamped [0.155, 0.167],
		                    // D uniform in [57.00, 57.15] mm (ESTIMATE), sampled with the seed (equipment 6.3)
		OldBarOversizedCue, // DiveBar object balls + oversized 60.325 mm / 0.2211 kg cue ball
		Snooker,            // 22 x 52.5 mm, 0.140 kg (pooltool) - capacity check for kMaxBalls
		Blackball,          // 15 x 50.8 mm object balls + 47.6 mm cue ball (UK pub pool, rules.md 12.4); masses ESTIMATE
	};

	struct BallSet
	{
		int Count = 0;              // balls 0..Count-1 are defined
		BallSpec Balls[kMaxBalls];  // index = ball id (0 = cue ball)
	};

	inline constexpr BallSpec kStandardPoolBall = MakeBallSpec(0.028575, 0.17009713875);
	inline constexpr BallSpec kMagneticCueBall = MakeBallSpec(0.028575, 0.167);
	inline constexpr BallSpec kOversizedCueBall = MakeBallSpec(0.0301625, 0.2211);

	// Deterministic for a given (Preset, Seed): the seed comes from the game layer / replay.
	RB_API ErrorCode BuildBallSet(BallSetPreset Preset, std::uint64_t Seed, BallSet& Out);
}
