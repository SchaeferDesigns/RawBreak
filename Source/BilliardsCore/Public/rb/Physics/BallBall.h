#pragma once

// Ball-ball collision: instantaneous rigid frictional impulse in 3D vector form with Alciatore's
// speed-dependent friction (physics-collisions 2). Unequal radii and masses supported; the contact
// normal is 3D (airborne balls, oversized cue balls).
// Owner: WP-3 (ball-ball & compliant islands).
//
// Sign conventions (collisions pitfall 1): n_hat from ball 1 to ball 2; impulse on ball 1 is
// P1 = -J_n n_hat - J_t t_hat; ball 2 receives -P1.
// Inertia: the tangential compliance uses the per-ball inertia, k_t = 1/m1 + 1/m2 + R1^2/I1 + R2^2/I2
// (= (7/2) k_n for solid spheres, collisions 2.3), so per-ball I is honoured.
//
// Cling (human-factors 4.3, HF-40/41): BallBallParams::ClingFactor is the venue's ball dirt k_venue; with
// PhysicsParams::ChalkCling the simulator replaces it per contact by ContactClingFactor from the chalk marks
// of both balls (ResolveBallBall itself is unchanged: it reads Params.ClingFactor). Off by default, so every
// COL test is unchanged.

#include "rb/Config.h"
#include "rb/Core/FixedVector.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Vec3.h"

#include <cstdint>

namespace rb
{
	// Chalk mark on a ball (human-factors 4.3, HF-40): a blue spot left by a tip contact, fixed in the ball's
	// BODY frame (it rotates with the ball). Deposited, faded and wiped by rb::human (rb/Human/BallMarks.h).
	struct ChalkMark
	{
		Vec3 BodyDir;          // unit direction from the ball center to the mark, body frame [1]
		double Strength = 0.0; // [0, 1] (0.3 + 0.7 c at deposit; 1 after a miscue)
		double Radius = 0.0;   // [m] mark radius on the surface (2.5 mm; 4 mm after a miscue)
	};

	inline constexpr int kMaxChalkMarks = 8;
	using BallChalkMarks = FixedVector<ChalkMark, kMaxChalkMarks>;
	enum class BallBallFrictionModel : std::uint8_t
	{
		Alciatore,  // mu_b(s) = k_cling (a + b exp(-c s)), frozen at the pre-impact slip speed (default)
		Constant,   // mu_b = MuConstant (TP A.5 test mode, prior-art BB-02)
		None,       // frictionless (90-degree-rule tests, prior-art BB-01)
	};

	enum class BallBallModel : std::uint8_t
	{
		FrictionalImpulse, // collisions 2.3-2.4 (baseline)
		Mathavan2014,      // optional A/B model (collisions 2.8) - RESERVED, not implemented in v1
	};

	struct BallBallParams
	{
		BallBallModel Model = BallBallModel::FrictionalImpulse;
		double Restitution = 0.95;        // e_b [1], range 0.92-0.98
		BallBallFrictionModel Friction = BallBallFrictionModel::Alciatore;
		double MuA = 9.951e-3;            // a_mu [1]   (TP A.14 fit to Marlow)
		double MuB = 0.108;               // b_mu [1]
		double MuC = 1.088;               // c_mu [s/m]
		double MuConstant = 0.06;         // [1] for BallBallFrictionModel::Constant
		double ClingFactor = 1.0;         // k_cling = k_venue: 1 clean, 1.3 dive-bar balls (HF-41), 1.5 dirty (TP A.14)
		double ChalkClingFactor = 2.5;    // k_chalk: a fully chalked contact (TP A.14 cling/skid 2.5; human-factors 4.3); used
		                                  //   only with PhysicsParams::ChalkCling
	};

	// Weight chi of a ball's chalk marks at one contact (human-factors 4.3): chi = SUM_j Strength_j
	// exp(-(R delta_j / Radius_j)^2), delta_j = angle between mark j (world frame: Rotate(Orientation, BodyDir))
	// and ContactDir (unit, from the ball center to the contact point, world frame). Owner: WP-3.
	RB_API double ChalkMarkWeight(const BallChalkMarks& Marks, const Quat& Orientation, double Radius, const Vec3& ContactDir);

	// k_cling of one contact: k_venue + (max(k_chalk, k_venue) - k_venue) min(1, Chi1 + Chi2) with k_venue =
	// Params.ClingFactor, k_chalk = Params.ChalkClingFactor (HF-S07: one full mark on the contact gives 2.5). Owner: WP-3.
	RB_API double ContactClingFactor(double Chi1, double Chi2, const BallBallParams& Params);

	// Rigid body state of one ball at the contact instant.
	struct ImpactBody
	{
		Vec3 Position;
		Vec3 Velocity;
		Vec3 Omega;
		double Radius = 0.0;
		double Mass = 0.0;
		double Inertia = 0.0;
	};

	struct BallBallImpulse
	{
		bool Approaching = false;   // v_n > 0; if false nothing was applied (collisions 2.4 step 1)
		Vec3 Velocity1, Omega1;     // after the impulse, BEFORE the table step (2.4 step 6)
		Vec3 Velocity2, Omega2;
		Vec3 Normal;                // n_hat (ball 1 -> ball 2)
		Vec3 TangentDirection;      // t_hat (0 if |s_t| < eps_v)
		double NormalSpeed = 0.0;   // v_n = (v1 - v2) . n_hat [m/s]
		double RestitutionUsed = 0.0; // e_b, or 0 for a micro-impact v_n < v_rest (7.3)
		double SlipSpeed = 0.0;     // |s_t| before the impulse [m/s]
		double Mu = 0.0;            // friction coefficient used
		double NormalImpulse = 0.0; // J_n [N s]
		double TangentImpulse = 0.0;// J_t [N s]
		bool Stick = false;         // stop-slip cap active (J_t = |s_t| / k_t)
	};

	// mu_b(s) for the configured friction model (2.1): 0.1180 at 0, 0.0463 at 1 m/s (Alciatore).
	RB_API double BallBallFriction(double SlipSpeed, const BallBallParams& Params);

	// Collisions 2.4 steps 1-5 (impulse only). RestSpeed = NumericsConfig::RestSpeed (e := 0 below it),
	// EpsV = NumericsConfig::EpsV. The table step (step 6) and classification are done by the caller
	// with ApplyTableReaction / ClassifyState (rb/Physics/Slate.h, Motion.h).
	RB_API BallBallImpulse ResolveBallBall(const ImpactBody& Ball1, const ImpactBody& Ball2, const BallBallParams& Params, double RestSpeed, double EpsV);

	// cutAngle for the rules log: angle between the cue ball's pre-impact HORIZONTAL velocity and the
	// horizontal part of n_hat; 0 = full hit [rad] (collisions 2.4).
	RB_API double CutAngle(const Vec3& CueBallVelocityBefore, const Vec3& Normal);
}
