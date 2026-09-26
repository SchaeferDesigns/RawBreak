#include "rb/Core/FpGuard.h"
// Owner: WP-1 (motion, slate & cue strike). Spec: physics-motion-and-cue C.3, C.4; physics-collisions 2.4 step 6.
#include "rb/Physics/Slate.h"

namespace rb
{
	SlateImpactResult ResolveSlateImpact(const Vec3& Velocity, const Vec3& Omega, const BallSpec& Spec, double Restitution, double SlidingFriction,
		const SlateParams& Slate, int BounceIndex, double Gravity, const NumericsConfig& Numerics)
	{
		SlateImpactResult Out;
		Out.Velocity = Velocity;
		Out.Omega = Omega;
		const double Wn = -Velocity.z;
		if (!(Wn > 0.0))
		{
			return Out; // not moving into the support: no impact
		}

		const double Radius = Spec.Radius;
		const double InertiaK = InertiaFactor(Spec);
		const double Pi = (1.0 + Restitution) * Wn; // normal impulse per unit mass (through the center: no torque)
		Out.NormalImpulsePerMass = Pi;

		// Coulomb friction impulse at the bottom contact point: dv_h = -min(mu Pi, (k/(1+k)) |u|) u_hat.
		const double Ux = Velocity.x - Radius * Omega.y;
		const double Uy = Velocity.y + Radius * Omega.x;
		const double SlipSpeed = Sqrt(Ux * Ux + Uy * Uy);
		if (SlipSpeed > Numerics.EpsV)
		{
			const double StickFraction = InertiaK / (1.0 + InertiaK); // 2/7 for k = 2/5
			const double Friction = SlidingFriction * Pi;
			if (StickFraction * SlipSpeed <= Friction)
			{
				// Stick: v_h' = v_h - (k/(1+k)) u = L_c (the Coriolis invariant), u' = 0 exactly (w_h' := z_hat x v' / R).
				Out.Stick = true;
				const double Vx = Velocity.x - StickFraction * Ux;
				const double Vy = Velocity.y - StickFraction * Uy;
				Out.Velocity.x = Vx;
				Out.Velocity.y = Vy;
				Out.Omega.x = -Vy / Radius;
				Out.Omega.y = Vx / Radius;
			}
			else
			{
				// Slip: |dv_h| = mu Pi; w' = w - (1/(k R)) z_hat x dv_h (only w_x, w_y change).
				const double Scale = Friction / SlipSpeed;
				const double Dvx = -Scale * Ux;
				const double Dvy = -Scale * Uy;
				const double InvKR = 1.0 / (InertiaK * Radius);
				Out.Velocity.x = Velocity.x + Dvx;
				Out.Velocity.y = Velocity.y + Dvy;
				Out.Omega.x = Omega.x + InvKR * Dvy;
				Out.Omega.y = Omega.y - InvKR * Dvx;
			}
		}

		// C.4 Zeno guard: sub-h_min rebounds and the N_max-th impact of a sequence settle the ball.
		const double Vz = Restitution * Wn;
		Out.Velocity.z = Vz;
		if (Vz < MinBounceSpeed(Slate, Gravity) || BounceIndex >= Slate.MaxBounces)
		{
			Out.Velocity.z = 0.0;
			Out.Settled = true;
		}
		return Out;
	}

	void ApplyTableReaction(BallState& S, bool WasOnSurface, const BallSpec& Spec, const ClothParams& Surface, const SlateParams& Slate,
		double Gravity, const NumericsConfig& Numerics)
	{
		if (!WasOnSurface)
		{
			return;
		}
		if (S.Velocity.z < 0.0)
		{
			const SlateImpactResult Impact =
				ResolveSlateImpact(S.Velocity, S.Omega, Spec, Slate.Restitution, Surface.SlidingFriction, Slate, 1, Gravity, Numerics);
			S.Velocity = Impact.Velocity;
			S.Omega = Impact.Omega;
		}
		else if (S.Velocity.z > 0.0 && S.Velocity.z < MinBounceSpeed(Slate, Gravity))
		{
			S.Velocity.z = 0.0;
		}
	}
}
