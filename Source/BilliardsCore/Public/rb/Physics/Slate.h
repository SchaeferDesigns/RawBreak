#pragma once

// Slate (cloth on slate) impacts and bounce termination (physics-motion-and-cue C.2-C.5), and the
// table reaction applied after impulsive events (physics-collisions 2.4 step 6 / 2.5).
// Owner: WP-1 (motion, slate & cue strike).

#include "rb/Config.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"

namespace rb
{
	struct SlateParams
	{
		double Restitution = 0.6;        // e_slate [1], range 0.5-0.7 (TP B.10; pooltool 0.5)
		double MinBounceHeight = 0.002;  // h_min [m], range 1-5 mm (TUNING, C.4). 0 disables the guard (T-C3, T-C5)
		int MaxBounces = 10;             // N_max slate impacts per airborne sequence; on the N_max-th set v_z := 0
	};

	// v_z_min = sqrt(2 g h_min) [m/s] (0.1980571 m/s for the defaults).
	inline double MinBounceSpeed(const SlateParams& Slate, double Gravity) { return Sqrt(2.0 * Gravity * Slate.MinBounceHeight); }

	// Later root of z(tau) = R for a ballistic flight (C.2): (v_z0 + sqrt(v_z0^2 + 2 g (z0 - R))) / g, evaluated
	// without cancellation for a falling ball (v_z0 < 0: 2 (z0 - R) / (sqrt(.) - v_z0)). A ball at or below the
	// plane that does not rise back to it (inconsistent input) lands at once (0). Z0 is relative to the landing
	// support (MakeSegment passes z0 - SupportZ).
	inline double LandingTau(double Z0, double Vz0, double Radius, double Gravity)
	{
		const double Height = Z0 - Radius;
		const double Disc = Vz0 * Vz0 + 2.0 * Gravity * Height;
		if (!(Disc >= 0.0))
		{
			return 0.0;
		}
		const double Root = Sqrt(Disc);
		if (Vz0 > 0.0)
		{
			return (Vz0 + Root) / Gravity;
		}
		const double Den = Root - Vz0;
		return (Height > 0.0 && Den > 0.0) ? 2.0 * Height / Den : 0.0;
	}

	struct SlateImpactResult
	{
		Vec3 Velocity;                 // after the impact [m/s]
		Vec3 Omega;                    // after the impact [rad/s]; w_z is never changed (C.3, T-C7)
		double NormalImpulsePerMass = 0.0; // Pi = (1 + e) w_n [m/s]
		bool Stick = false;            // stick branch: slip u' = 0 exactly
		bool Settled = false;          // v_z was set to 0 by the v_z_min or N_max guard (C.4)
	};

	// C.3 + C.4 for a ball at the support with v_z < 0 (w_n = -v_z > 0), k = InertiaFactor(Spec):
	//   v_z' = e w_n; Pi = (1 + e) w_n; u = v_h + R (z_hat x w_h);
	//   dv_h = -min(mu_s Pi, (k/(1+k))|u|) u_hat (0 if |u| <= EpsV); w' = w - (1/(k R)) (z_hat x dv_h)
	//   ((2/7) and 5/(2R) for k = 2/5); then v_z' := 0 if v_z' < v_z_min or BounceIndex >= N_max
	//   (BounceIndex is 1-based within the current airborne sequence). Restitution/friction are passed
	//   explicitly so that GRI parity tests (collisions G-3) and the cue-strike pinch (B.8.3, e_eff) can
	//   reuse it. Stick branch: v_h' = L_c exactly and w_h' := z_hat x v_h' / R (u' = 0, implementation note 2).
	//   v_z >= 0 (not moving into the support): returned unchanged, no guard applied.
	RB_API SlateImpactResult ResolveSlateImpact(const Vec3& Velocity, const Vec3& Omega, const BallSpec& Spec, double Restitution, double SlidingFriction,
		const SlateParams& Slate, int BounceIndex, double Gravity, const NumericsConfig& Numerics);

	// physics-collisions 2.4 step 6: for a ball that was ON A SUPPORT (cloth or flat rail cap) before an
	// impulsive event (ball-ball, cushion, tip), resolve a downward v_z by ResolveSlateImpact at the same
	// timestamp (friction of Surface), or snap a small upward v_z < v_z_min to 0. Airborne balls
	// (WasOnSurface = false) are left unchanged. Does not classify; the caller classifies afterwards.
	RB_API void ApplyTableReaction(BallState& S, bool WasOnSurface, const BallSpec& Spec, const ClothParams& Surface, const SlateParams& Slate,
		double Gravity, const NumericsConfig& Numerics);
}
