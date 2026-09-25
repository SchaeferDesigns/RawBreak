#pragma once

// Project-wide capacities, units and physical constants.
// Owner: WP-0 (architecture, frozen). Changes need architect sign-off (see Docs/architecture.md).

#include "rb/Config.h"

#include <cstdint>
#include <limits>

namespace rb
{
	// ---------------------------------------------------------------------------------------------
	// Capacities (every fixed-capacity container used inside the event loop is sized from these)
	// ---------------------------------------------------------------------------------------------

	// Maximum number of balls in one simulation, indexed by ball id 0..kMaxBalls-1.
	// 24 covers snooker (15 reds + 6 colours + cue ball = 22), pool/Blackball (16) and carom (3);
	// it keeps per-ball bit masks within 32 bits and pair tables small (276 pairs).
	inline constexpr int kMaxBalls = 24;

	// Number of unordered ball pairs, kMaxBalls * (kMaxBalls - 1) / 2.
	inline constexpr int kMaxBallPairs = kMaxBalls * (kMaxBalls - 1) / 2;

	// Ball id 0 is always the (first) cue ball; 1..15 are the numbered pool object balls.
	inline constexpr int kCueBallId = 0;

	// Pool disciplines (8/9/10-ball, 14.1, Blackball) use ball ids 0..15.
	inline constexpr int kPoolBallCount = 16;

	// Cue strikes in one simulation: 1 for every normal shot, 2 for the lag (rules.md 4.1: both lag
	// strokes are simulated together, both tip contacts at t = 0).
	inline constexpr int kMaxStrikes = 2;

	// ---------------------------------------------------------------------------------------------
	// Units (exact definitions; SI everywhere inside the core)
	// ---------------------------------------------------------------------------------------------

	inline constexpr double kInch = 0.0254;           // [m]
	inline constexpr double kFoot = 0.3048;           // [m]
	inline constexpr double kOunce = 0.028349523125;  // [kg] avoirdupois
	inline constexpr double kMph = 0.44704;           // [m/s]
	inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
	inline constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

	// ---------------------------------------------------------------------------------------------
	// Physical constants
	// ---------------------------------------------------------------------------------------------

	// Standard gravity [m/s^2]; default of PhysicsParams::Gravity (physics-motion-and-cue A.9).
	// Note: prior-art-and-validation test tables use g = 9.81 and must pin it explicitly.
	inline constexpr double kStandardGravity = 9.80665;

	inline constexpr double kInfinity = std::numeric_limits<double>::infinity();
}
