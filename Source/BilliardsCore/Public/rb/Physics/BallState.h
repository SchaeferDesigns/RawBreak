#pragma once

// Per-ball physical properties and kinematic state (world frame, SI units).
// Owner: WP-1 (motion, slate & cue strike).
//
// Frame: right-handed, origin at the center of the table bed, +x toward the foot rail, +y across
// (left for a player at the head end), +z up; cloth at z = 0, a resting ball's center at z = R.

#include "rb/Config.h"
#include "rb/Core/Ids.h"
#include "rb/Math/Vec3.h"

namespace rb
{
	// Solid homogeneous sphere: I = k m R^2 with k = 2/5. Every spec formula (motion A/B/C, collisions
	// 2, 4, 5.4) is written for k = 2/5; the implementation generalises them with the per-ball
	// k = I / (m R^2) (5/2 -> 1/k, 2/7 -> k/(1+k), 5/7 -> 1/(1+k), 7/2 -> (1+k)/k, 7/10 -> (1+k)/2), so
	// that they reduce mathematically to the spec forms for k = 2/5 (spec golden values then agree to
	// rounding, far inside every spec tolerance; Docs/architecture.md section 7.2).
	inline constexpr double kSolidSphereInertiaFactor = 0.4;

	inline constexpr double kDefaultBallRadius = 0.028575;     // [m] WPA 2.25 in
	inline constexpr double kDefaultBallMass = 0.17009713875;  // [kg] 6 oz

	// Per-ball radius, mass and moment of inertia. Never hard-code a single R or m (equipment 12.7):
	// oversized/heavier bar cue balls and lighter worn object balls are first-class.
	// The default is exactly MakeBallSpec(kDefaultBallRadius, kDefaultBallMass) (same expression order),
	// so default-constructed specs satisfy I == 0.4 m R^2 bitwise (1e-12 invariant tests).
	struct BallSpec
	{
		double Radius = kDefaultBallRadius; // R [m]
		double Mass = kDefaultBallMass;     // m [kg]
		double Inertia = kSolidSphereInertiaFactor * kDefaultBallMass * kDefaultBallRadius * kDefaultBallRadius; // I [kg m^2]
	};

	// Solid homogeneous sphere: I = (2/5) m R^2.
	constexpr BallSpec MakeBallSpec(double Radius, double Mass) { return {Radius, Mass, kSolidSphereInertiaFactor * Mass * Radius * Radius}; }

	// k = I / (m R^2): 0.4 for a solid homogeneous ball. The simulator rejects specs with k outside
	// (0, 2/3] (2/3 = thin shell) or non-positive R, m (SimStatus::InvalidInput).
	constexpr double InertiaFactor(const BallSpec& Spec) { return Spec.Inertia / (Spec.Mass * Spec.Radius * Spec.Radius); }

	// Instantaneous ball state.
	struct BallState
	{
		Vec3 Position;  // r: center [m]
		Vec3 Velocity;  // v: center velocity [m/s]
		Vec3 Omega;     // w: angular velocity, world frame [rad/s]; +w_z = counter-clockwise from above
		MotionState State = MotionState::Stationary;
	};
}
