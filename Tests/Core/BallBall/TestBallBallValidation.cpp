// WP-3: prior-art-and-validation 9.3 (BB-01 ... BB-08) and 9.4 (THR-01 ... THR-10) on the ball-ball impulse.
// VAL tables use g = 9.81 (irrelevant for the impulse) and m = 0.17009713875 kg (pinned in BallBallTestUtil.h).
#include "rbtest.h"

#include "BallBallTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/Motion.h"

using namespace rbbb;

namespace
{
	constexpr double kDeg = rb::kDegToRad;

	double AngleDeg(const rb::Vec3& A, const rb::Vec3& B)
	{
		return rb::Atan2(rb::Length(rb::Cross(A, B)), rb::Dot(A, B)) * rb::kRadToDeg;
	}

	// Final rolling velocity after the post-impact slide (motion spec A.5 Coriolis invariant, k = 2/5).
	rb::Vec3 RollingVelocity(const rb::Vec3& V, const rb::Vec3& W)
	{
		return rb::CoriolisInvariant(V, W, kR, 0.4);
	}

	// Final CB deflection [deg] from +x after a natural-roll impact at cut angle Phi (BB-03 ... BB-05).
	double RollingDeflection(double PhiRad, double Restitution)
	{
		const rb::BallBallImpulse I = Resolve(MakeCut(1.0, PhiRad, {0.0, 1.0 / kR, 0.0}), Params(Restitution, rb::BallBallFrictionModel::None));
		const rb::Vec3 F = RollingVelocity(I.Velocity1, I.Omega1);
		return rb::Atan2(-F.y, F.x) * rb::kRadToDeg;
	}

	// Golden-section maximum of RollingDeflection over Phi in [Lo, Hi] [deg]; returns the maximising Phi [deg].
	double MaximiseDeflection(double Restitution, double Lo, double Hi, double& Max)
	{
		const double G = 0.5 * (rb::Sqrt(5.0) - 1.0);
		double A = Lo;
		double B = Hi;
		double C = B - G * (B - A);
		double D = A + G * (B - A);
		for (int k = 0; k < 200 && B - A > 1e-12; ++k)
		{
			if (RollingDeflection(C * kDeg, Restitution) > RollingDeflection(D * kDeg, Restitution))
			{
				B = D;
			}
			else
			{
				A = C;
			}
			C = B - G * (B - A);
			D = A + G * (B - A);
		}
		const double Phi = 0.5 * (A + B);
		Max = RollingDeflection(Phi * kDeg, Restitution);
		return Phi;
	}
}

RB_TEST(VAL_BB01_NinetyDegreeRule)
{
	for (double Phi : {10.0, 30.0, 45.0, 60.0, 80.0})
	{
		const double V = 1.5;
		const rb::BallBallImpulse I = Resolve(MakeCut(V, Phi * kDeg), Params(1.0, rb::BallBallFrictionModel::None));
		RB_CHECK_NEAR(AngleDeg(I.Velocity1, I.Velocity2), 90.0, 1e-7);
		RB_CHECK_NEAR(rb::Length(I.Velocity1) / (V * rb::Sin(Phi * kDeg)), 1.0, 1e-9);
		RB_CHECK_NEAR(rb::Length(I.Velocity2) / (V * rb::Cos(Phi * kDeg)), 1.0, 1e-9);
	}
}

RB_TEST(VAL_BB02_ConstantFrictionTpA5)
{
	// TP A.5 mode: e_b = 0.94, constant mu_b = 0.06, stun, phi = 30 deg.
	const double Phi = 30.0 * kDeg;
	const rb::Vec3 Tangent{rb::Sin(Phi), -rb::Cos(Phi), 0.0}; // the CB's tangent line (perpendicular to n_hat)
	const rb::BallBallImpulse I = Resolve(MakeCut(1.0, Phi), Params(0.94, rb::BallBallFrictionModel::Constant));
	RB_CHECK_NEAR(-ThrowDeg(I), 3.4336, 1e-3);
	RB_CHECK_NEAR(AngleDeg(I.Velocity1, Tangent), 3.3073, 1e-3);
	RB_CHECK_NEAR(AngleDeg(I.Velocity1, I.Velocity2), 83.2591, 1e-3);
	const rb::BallBallImpulse NoFriction = Resolve(MakeCut(1.0, Phi), Params(0.94, rb::BallBallFrictionModel::None));
	RB_CHECK_NEAR(AngleDeg(NoFriction.Velocity1, NoFriction.Velocity2), 87.0255, 1e-3);
	const rb::BallBallImpulse Elastic = Resolve(MakeCut(1.0, Phi), Params(1.0, rb::BallBallFrictionModel::Constant));
	RB_CHECK_NEAR(AngleDeg(Elastic.Velocity1, Elastic.Velocity2), 86.5664, 1e-3);
}

RB_TEST(VAL_BB03_ThirtyDegreeRuleNaturalRoll)
{
	// e_b = 1, no ball friction, natural roll 1 m/s. The half, 1/4 and 3/4-ball hits are phi = asin(1/2), asin(3/4),
	// asin(1/4) (the table prints them rounded to 30, 48.590, 14.478 deg). Oracle: atan(sin cos / (sin^2 + 2/5)).
	const double Sines[3] = {0.5, 0.75, 0.25};
	const double Printed[3] = {33.6705, 27.2669, 27.6265};
	for (int k = 0; k < 3; ++k)
	{
		const double Phi = rb::Asin(Sines[k]);
		const double Oracle = rb::Atan(rb::Sin(Phi) * rb::Cos(Phi) / (rb::Square(rb::Sin(Phi)) + 0.4)) * rb::kRadToDeg;
		RB_CHECK_NEAR(RollingDeflection(Phi, 1.0), Oracle, 1e-6);
		RB_CHECK_NEAR(RollingDeflection(Phi, 1.0), Printed[k], 1e-4);
	}
	const rb::BallBallImpulse I = Resolve(MakeCut(1.0, 30.0 * kDeg, {0.0, 1.0 / kR, 0.0}), Params(1.0, rb::BallBallFrictionModel::None));
	RB_CHECK_NEAR(rb::Length(RollingVelocity(I.Velocity1, I.Omega1)), 0.557875, 1e-6);
}

RB_TEST(VAL_BB04_ThirtyDegreeRuleMaximum)
{
	double Max = 0.0;
	const double Phi = MaximiseDeflection(1.0, 10.0, 50.0, Max);
	RB_CHECK_NEAR(Max, 33.7490, 1e-4);
	RB_CHECK_NEAR(Phi, 28.1255, 0.01);
	RB_CHECK_NEAR(Phi, rb::Asin(rb::Sqrt(2.0 / 9.0)) * rb::kRadToDeg, 0.01);
}

RB_TEST(VAL_BB05_RollingCutRestitution094)
{
	RB_CHECK_NEAR(RollingDeflection(30.0 * kDeg, 0.94), 31.9876, 1e-4);
	double Max = 0.0;
	const double Phi = MaximiseDeflection(0.94, 10.0, 50.0, Max);
	RB_CHECK_NEAR(Max, 32.0091, 1e-4);
	RB_CHECK_NEAR(Phi, 28.995, 0.01);
}

RB_TEST(VAL_BB06_HeadOnRollingFollowSpeed)
{
	// (5/7)(1 - e)/2 v + (2/7) v forward (no ball friction: the VAL oracle is frictionless).
	const double Expected[2] = {0.285714, 0.303571};
	const double Restitution[2] = {1.0, 0.95};
	for (int k = 0; k < 2; ++k)
	{
		const rb::BallBallImpulse I = Resolve(MakeCut(1.0, 0.0, {0.0, 1.0 / kR, 0.0}), Params(Restitution[k], rb::BallBallFrictionModel::None));
		const rb::Vec3 F = RollingVelocity(I.Velocity1, I.Omega1);
		const double Exact = (5.0 / 7.0) * (1.0 - Restitution[k]) / 2.0 + 2.0 / 7.0;
		RB_CHECK_NEAR(F.x / Exact, 1.0, 1e-9);
		RB_CHECK_NEAR(F.x, Expected[k], 1e-6);
		RB_CHECK(rb::Abs(F.y) <= 1e-15);
	}
}

RB_TEST(VAL_BB07_HeadOnStunStopShot)
{
	const rb::BallBallImpulse I = Resolve(MakeCut(1.0, 0.0), Params(0.95));
	RB_CHECK_NEAR(I.Velocity1.x / 0.025, 1.0, 1e-9);
	RB_CHECK_NEAR(I.Velocity2.x / 0.975, 1.0, 1e-9);
	const rb::Vec3 F = RollingVelocity(I.Velocity1, I.Omega1);
	RB_CHECK_NEAR(F.x / ((5.0 / 7.0) * 0.025), 1.0, 1e-9);
	RB_CHECK_NEAR(F.x, 0.017857, 1e-6);
}

RB_TEST(VAL_BB08_InvariantsAllModels)
{
	// 1e5 random cases over all friction models: momentum (rel 1e-12), KE non-increasing (<= +1e-12 J), angular momentum about
	// the contact point conserved for the frictional models.
	rb::Rng G(0x0B08B08ull);
	const rb::NumericsConfig N;
	const rb::BallBallFrictionModel Models[3] = {rb::BallBallFrictionModel::Alciatore, rb::BallBallFrictionModel::Constant, rb::BallBallFrictionModel::None};
	double WorstP = 0.0;
	double WorstE = -1.0;
	double WorstL = 0.0;
	for (int k = 0; k < 100000; ++k)
	{
		const rb::BallBallParams P = Params(G.NextUniform(0.9, 1.0), Models[k % 3]);
		const double V = G.NextUniform(0.0, 10.0);
		const double Phi = G.NextUniform(-89.0, 89.0) * kDeg;
		const rb::Vec3 W{G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0)};
		CutShot S = MakeCut(V, Phi, W);
		S.Object.Velocity = {G.NextUniform(-1.0, 1.0), G.NextUniform(-1.0, 1.0), 0.0};
		S.Object.Omega = {G.NextUniform(-100.0, 100.0), G.NextUniform(-100.0, 100.0), G.NextUniform(-100.0, 100.0)};
		const rb::BallBallImpulse I = rb::ResolveBallBall(S.Cue, S.Object, P, N.RestSpeed, N.EpsV);
		if (!I.Approaching)
		{
			continue;
		}
		const rb::Vec3 P0 = (S.Cue.Velocity + S.Object.Velocity) * kM;
		const rb::Vec3 P1 = (I.Velocity1 + I.Velocity2) * kM;
		WorstP = rb::Max(WorstP, rb::Length(P1 - P0) / rb::Max(rb::Length(P0), 1e-3));
		const double E0 = 0.5 * kM * (rb::LengthSquared(S.Cue.Velocity) + rb::LengthSquared(S.Object.Velocity)) +
			0.5 * kI * (rb::LengthSquared(S.Cue.Omega) + rb::LengthSquared(S.Object.Omega));
		const double E1 = 0.5 * kM * (rb::LengthSquared(I.Velocity1) + rb::LengthSquared(I.Velocity2)) + 0.5 * kI * (rb::LengthSquared(I.Omega1) + rb::LengthSquared(I.Omega2));
		WorstE = rb::Max(WorstE, E1 - E0);
		// About the contact point C = r1 + R n.
		const rb::Vec3 C = S.Cue.Position + I.Normal * kR;
		auto L = [&](const rb::Vec3& V1, const rb::Vec3& W1, const rb::Vec3& V2, const rb::Vec3& W2)
		{
			return rb::Cross(S.Cue.Position - C, V1) * kM + rb::Cross(S.Object.Position - C, V2) * kM + (W1 + W2) * kI;
		};
		const rb::Vec3 L0 = L(S.Cue.Velocity, S.Cue.Omega, S.Object.Velocity, S.Object.Omega);
		const rb::Vec3 L1 = L(I.Velocity1, I.Omega1, I.Velocity2, I.Omega2);
		const double Scale = kM * kR * (rb::Length(S.Cue.Velocity) + rb::Length(S.Object.Velocity)) + kI * (rb::Length(S.Cue.Omega) + rb::Length(S.Object.Omega));
		WorstL = rb::Max(WorstL, rb::Length(L1 - L0) / rb::Max(Scale, 1e-12));
	}
	RB_CHECK(WorstP <= 1e-12);
	RB_CHECK(WorstE <= 1e-12);
	RB_CHECK(WorstL <= 1e-12);
}

RB_TEST(VAL_THR01_StunThirtyDegrees)
{
	// Alciatore-law model (TP A.14 assumes the elastic normal impulse): e_b = 1.
	const double Speeds[3] = {0.447, 1.341, 3.129};
	const double Expected[3] = {4.715, 3.549, 1.698};
	for (int v = 0; v < 3; ++v)
	{
		RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(Speeds[v], 30.0 * kDeg), Params(1.0))), Expected[v], 0.02);
	}
}

RB_TEST(VAL_THR02_StunTenDegreesSpeedIndependent)
{
	for (double V : {0.447, 1.341, 3.129})
	{
		RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(V, 10.0 * kDeg), Params(1.0))), 1.443, 0.02);
	}
}

RB_TEST(VAL_THR03_NaturalRollThirtyDegrees)
{
	const double Speeds[3] = {0.447, 1.341, 3.129};
	const double Expected[3] = {2.186, 1.004, 0.388};
	for (int v = 0; v < 3; ++v)
	{
		const double V = Speeds[v];
		RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(V, 30.0 * kDeg, {0.0, V / kR, 0.0}), Params(1.0))), Expected[v], 0.02);
	}
}

RB_TEST(VAL_THR04_StraightStunSideSpin)
{
	// R w_z = 1.25 pE v; pE = 25 / 50 / 100 %.
	const double Speeds[3] = {0.447, 1.341, 3.129};
	const double Percent[3] = {0.25, 0.5, 1.0};
	const double Expected[3][3] = {{2.556, 2.556, 2.556}, {5.102, 3.053, 1.307}, {3.933, 1.569, 0.658}};
	for (int p = 0; p < 3; ++p)
	{
		for (int v = 0; v < 3; ++v)
		{
			const double V = Speeds[v];
			const rb::BallBallImpulse I = Resolve(MakeCut(V, 0.0, {0.0, 0.0, 1.25 * Percent[p] * V / kR}), Params(1.0));
			RB_CHECK_NEAR(ThrowDeg(I), Expected[p][v], 0.02);
		}
	}
}

RB_TEST(VAL_THR05_GearingOutsideEnglishNoThrow)
{
	for (double Phi : {15.0, 30.0, 45.0})
	{
		for (double V : {0.447, 1.341, 3.129})
		{
			const rb::BallBallImpulse I = Resolve(MakeCut(V, Phi * kDeg, {0.0, 0.0, V * rb::Sin(Phi * kDeg) / kR}), Params(1.0));
			RB_CHECK(rb::Abs(ThrowDeg(I)) <= 1e-6);
		}
	}
}

RB_TEST(VAL_THR06_LeftEnglishThrowsRight)
{
	// Left English: tip on the CB's +y side for a CB moving +x -> w_z < 0 (r x F = (0, b, 0) x (F, 0, 0) = (0, 0, -bF)).
	const double Offset = 0.3 * kR * rb::Cos(0.0); // tip contact on the +y side
	const double Wz = rb::Cross(rb::Vec3{0.0, Offset, 0.0}, rb::Vec3{1.0, 0.0, 0.0}).z;
	RB_CHECK(Wz < 0.0);
	const rb::BallBallImpulse I = Resolve(MakeCut(1.341, 0.0, {0.0, 0.0, -0.5 * 1.341 / kR}), Params(0.95));
	RB_CHECK(I.Velocity2.y < 0.0); // right of the line of centres
	RB_CHECK(ThrowDeg(I) < 0.0);
}

RB_TEST(VAL_THR07_RealityStunTenDegrees)
{
	for (double V : {0.45, 1.0, 2.0, 3.1})
	{
		const double T = -ThrowDeg(Resolve(MakeCut(V, 10.0 * kDeg), Params(0.95)));
		RB_CHECK(T >= 1.0 && T <= 2.0);
	}
}

RB_TEST(VAL_THR08_RealitySlowStunThirtyToThirtyFive)
{
	for (double Phi : {30.0, 32.5, 35.0})
	{
		const double T = -ThrowDeg(Resolve(MakeCut(0.45, Phi * kDeg), Params(0.95)));
		RB_CHECK(T >= 4.5 && T <= 6.0);
	}
}

RB_TEST(VAL_THR09_RealityFastThrowLessThanHalfOfSlow)
{
	const double Slow = -ThrowDeg(Resolve(MakeCut(0.45, 30.0 * kDeg), Params(0.95)));
	const double Fast = -ThrowDeg(Resolve(MakeCut(3.1, 30.0 * kDeg), Params(0.95)));
	RB_CHECK(Fast < 0.5 * Slow);
}

RB_TEST(VAL_THR10_FollowAndDrawReduceThrowEqually)
{
	const double V = 1.341;
	const double Stun = -ThrowDeg(Resolve(MakeCut(V, 30.0 * kDeg), Params(0.95)));
	for (double Fraction : {0.5, 1.0})
	{
		const double Follow = -ThrowDeg(Resolve(MakeCut(V, 30.0 * kDeg, {0.0, Fraction * V / kR, 0.0}), Params(0.95)));
		const double Draw = -ThrowDeg(Resolve(MakeCut(V, 30.0 * kDeg, {0.0, -Fraction * V / kR, 0.0}), Params(0.95)));
		RB_CHECK(Follow < Stun && Draw < Stun);
		RB_CHECK_NEAR(Stun - Follow, Stun - Draw, 0.05);
	}
}
