#include "rb/Core/FpGuard.h"
// Owner: WP-3 (ball-ball & compliant islands). Spec: physics-collisions 2.
#include "rb/Physics/BallBall.h"

namespace rb
{
	double BallBallFriction(double /*SlipSpeed*/, const BallBallParams& /*Params*/)
	{
		// TODO(WP-3): Alciatore k_cling (a + b exp(-c s)) / constant / none (2.1).
		return 0.0;
	}

	BallBallImpulse ResolveBallBall(const ImpactBody& Ball1, const ImpactBody& Ball2, const BallBallParams& /*Params*/, double /*RestSpeed*/, double /*EpsV*/)
	{
		// TODO(WP-3): 2.2-2.4 steps 1-5 (normal impulse, Coulomb + stop-slip cap, unequal radii/masses).
		BallBallImpulse Result;
		Result.Velocity1 = Ball1.Velocity;
		Result.Omega1 = Ball1.Omega;
		Result.Velocity2 = Ball2.Velocity;
		Result.Omega2 = Ball2.Omega;
		return Result;
	}

	double CutAngle(const Vec3& /*CueBallVelocityBefore*/, const Vec3& /*Normal*/)
	{
		// TODO(WP-3): acos(v_cb_h_hat . n_hat_h) (2.4).
		return 0.0;
	}

	double ChalkMarkWeight(const BallChalkMarks& /*Marks*/, const Quat& /*Orientation*/, double /*Radius*/, const Vec3& /*ContactDir*/)
	{
		// TODO(WP-3): human-factors 4.3 chi = SUM Strength exp(-(R delta / Radius)^2) (HF-S07).
		return 0.0;
	}

	double ContactClingFactor(double /*Chi1*/, double /*Chi2*/, const BallBallParams& Params)
	{
		// TODO(WP-3): human-factors 4.3 k_venue + (max(k_chalk, k_venue) - k_venue) min(1, chi_1 + chi_2).
		return Params.ClingFactor;
	}
}
