#include "rb/Core/FpGuard.h"
// Owner: WP-3 (ball-ball & compliant islands). Spec: physics-collisions 2 (frictional impulse, throw, spin transfer),
// 7.3 (micro-impacts); human-factors 4.3 (chalk-mark cling).
#include "rb/Physics/BallBall.h"

#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		// Angle between two non-zero vectors [rad] in [0, pi]; atan2(|a x b|, a . b) is accurate at 0 and pi (acos is not)
		// and invariant to the lengths of a and b.
		double AngleBetween(const Vec3& A, const Vec3& B)
		{
			return Atan2(Length(Cross(A, B)), Dot(A, B));
		}
	}

	double BallBallFriction(double SlipSpeed, const BallBallParams& Params)
	{
		switch (Params.Friction)
		{
		case BallBallFrictionModel::Constant: return Params.MuConstant;
		case BallBallFrictionModel::None: return 0.0;
		case BallBallFrictionModel::Alciatore: break;
		}
		// 2.1: mu_b(s) = k_cling (a + b exp(-c s)), s = |s_t| [m/s] (TP A.14 fit to Marlow).
		return Params.ClingFactor * (Params.MuA + Params.MuB * Exp(-Params.MuC * SlipSpeed));
	}

	BallBallImpulse ResolveBallBall(const ImpactBody& Ball1, const ImpactBody& Ball2, const BallBallParams& Params, double RestSpeed, double EpsV)
	{
		// BallBallModel::Mathavan2014 is reserved (2.8, not implemented in v1): every model resolves with the frictional
		// impulse of 2.3-2.4.
		BallBallImpulse Result;
		Result.Velocity1 = Ball1.Velocity;
		Result.Omega1 = Ball1.Omega;
		Result.Velocity2 = Ball2.Velocity;
		Result.Omega2 = Ball2.Omega;

		// 2.2: n_hat from ball 1 to ball 2 in 3D (never flattened: airborne balls, unequal radii, pitfall 4).
		const Vec3 D = Ball2.Position - Ball1.Position;
		const double Distance = Length(D);
		if (!(Distance > 0.0))
		{
			return Result; // coincident centres: no defined normal (corrupt state), nothing applied
		}
		const Vec3 N = D / Distance;
		Result.Normal = N;

		// Step 1: approach test; separating or grazing contacts are no impact (3.6).
		const Vec3 RelativeVelocity = Ball1.Velocity - Ball2.Velocity;
		const double Vn = Dot(RelativeVelocity, N);
		Result.NormalSpeed = Vn;
		if (!(Vn > 0.0))
		{
			return Result;
		}
		Result.Approaching = true;

		// Step 2: micro-collision damping (7.3).
		const double E = Vn < RestSpeed ? 0.0 : Params.Restitution;
		Result.RestitutionUsed = E;

		// Step 3: normal impulse with k_n = 1/m1 + 1/m2.
		const double InvMass1 = 1.0 / Ball1.Mass;
		const double InvMass2 = 1.0 / Ball2.Mass;
		const double Kn = InvMass1 + InvMass2;
		const double Jn = (1.0 + E) * Vn / Kn;
		Result.NormalImpulse = Jn;

		// Step 4: tangential slip of the contact material points, s = (v1 - v2) + (R1 w1 + R2 w2) x n_hat (2.2).
		const Vec3 S = RelativeVelocity + Cross(Ball1.Omega * Ball1.Radius + Ball2.Omega * Ball2.Radius, N);
		const Vec3 St = S - N * Dot(S, N);
		const double SlipSpeed = Length(St);
		Result.SlipSpeed = SlipSpeed;

		double Jt = 0.0;
		Vec3 T;
		if (SlipSpeed >= EpsV && SlipSpeed > 0.0)
		{
			T = St / SlipSpeed;
			// k_t = 1/m1 + 1/m2 + R1^2/I1 + R2^2/I2 (per-ball inertia; (7/2) k_n for solid spheres, 2.3).
			const double Kt = Kn + Ball1.Radius * Ball1.Radius / Ball1.Inertia + Ball2.Radius * Ball2.Radius / Ball2.Inertia;
			const double Mu = BallBallFriction(SlipSpeed, Params); // frozen at the pre-impact slip (pitfall 2)
			const double Coulomb = Mu * Jn;
			const double StopSlip = SlipSpeed / Kt;
			Result.Mu = Mu;
			if (StopSlip <= Coulomb)
			{
				Jt = StopSlip;
				Result.Stick = true;
			}
			else
			{
				Jt = Coulomb;
			}
		}
		Result.TangentDirection = T;
		Result.TangentImpulse = Jt;

		// Step 5: P1 = -J_n n_hat - J_t t_hat on ball 1, -P1 on ball 2 (2.3).
		const Vec3 Pt = T * (-Jt);
		const Vec3 P1 = N * (-Jn) + Pt;
		Result.Velocity1 = Ball1.Velocity + P1 * InvMass1;
		Result.Velocity2 = Ball2.Velocity - P1 * InvMass2;
		// w1' = w1 + (R1 n) x P1 / I1, w2' = w2 + R2 (n x P1) / I2. The normal part of P1 has no moment about either
		// centre, so only the tangential impulse is used: the change is exactly perpendicular to n_hat (pitfall 3).
		const Vec3 NCrossPt = Cross(N, Pt);
		Result.Omega1 = Ball1.Omega + NCrossPt * (Ball1.Radius / Ball1.Inertia);
		Result.Omega2 = Ball2.Omega + NCrossPt * (Ball2.Radius / Ball2.Inertia);
		return Result;
	}

	double CutAngle(const Vec3& CueBallVelocityBefore, const Vec3& Normal)
	{
		// 2.4: angle between the cue ball's pre-impact HORIZONTAL velocity and the horizontal part of n_hat; 0 = full hit.
		const Vec3 V = Planar(CueBallVelocityBefore);
		const Vec3 N = Planar(Normal);
		if (LengthSquared(V) == 0.0 || LengthSquared(N) == 0.0)
		{
			return 0.0; // no horizontal direction (a vertical hit or a ball at rest): reported as a full hit
		}
		return AngleBetween(V, N);
	}

	double ChalkMarkWeight(const BallChalkMarks& Marks, const Quat& Orientation, double Radius, const Vec3& ContactDir)
	{
		// human-factors 4.3: chi = SUM_j Strength_j exp(-(R delta_j / Radius_j)^2), delta_j = angle between the mark (world
		// frame) and the contact direction. Marks with a non-positive radius carry no weight.
		if (LengthSquared(ContactDir) == 0.0)
		{
			return 0.0;
		}
		double Chi = 0.0;
		for (const ChalkMark& Mark : Marks)
		{
			if (!(Mark.Radius > 0.0) || LengthSquared(Mark.BodyDir) == 0.0)
			{
				continue;
			}
			const Vec3 WorldDir = Rotate(Orientation, Mark.BodyDir);
			const double Arc = Radius * AngleBetween(WorldDir, ContactDir) / Mark.Radius;
			Chi += Mark.Strength * Exp(-(Arc * Arc));
		}
		return Chi;
	}

	double ContactClingFactor(double Chi1, double Chi2, const BallBallParams& Params)
	{
		// human-factors 4.3: k_venue + (max(k_chalk, k_venue) - k_venue) min(1, chi_1 + chi_2): interpolates between the
		// venue's ball dirt and a fully chalked contact (2.5, TP A.14), never above either (HF-S07).
		const double Venue = Params.ClingFactor;
		const double Chalked = Max(Params.ChalkClingFactor, Venue);
		const double Weight = Clamp(Chi1 + Chi2, 0.0, 1.0);
		return Venue + (Chalked - Venue) * Weight;
	}
}
