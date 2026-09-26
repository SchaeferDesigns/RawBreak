// WP-3: ball-ball frictional impulse (physics-collisions 2, tests 9.1 BB-1 ... BB-10 incl. BB-3b).
#include "rbtest.h"

#include "BallBallTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

using namespace rbbb;

namespace
{
	constexpr double kDeg = rb::kDegToRad;
}

RB_TEST(COL_BB1_HeadOnStun)
{
	const CutShot S = MakeCut(1.0, 0.0);
	const rb::BallBallImpulse I = Resolve(S, Params(0.95));
	RB_REQUIRE(I.Approaching);
	RB_CHECK_NEAR(I.Velocity1.x, 0.025, 1e-12);
	RB_CHECK_NEAR(I.Velocity1.y, 0.0, 1e-12);
	RB_CHECK_NEAR(I.Velocity1.z, 0.0, 1e-12);
	RB_CHECK_NEAR(I.Velocity2.x, 0.975, 1e-12);
	RB_CHECK_NEAR(I.Velocity2.y, 0.0, 1e-12);
	RB_CHECK_NEAR(I.Velocity2.z, 0.0, 1e-12);
	RB_CHECK(rb::Length(I.Omega1) <= 1e-12 && rb::Length(I.Omega2) <= 1e-12);
	RB_CHECK(I.TangentImpulse == 0.0);
}

RB_TEST(COL_BB2_HeadOnRollingImpulse)
{
	// Impulse part (before the table step of 2.4 step 6).
	const CutShot S = MakeCut(1.0, 0.0, {0.0, 1.0 / kR, 0.0});
	const rb::BallBallImpulse I = Resolve(S, Params(0.95));
	RB_REQUIRE(I.Approaching);
	RB_CHECK_NEAR(I.Mu, 0.0463351, 1e-6);
	RB_CHECK_NEAR(I.NormalImpulse, 0.1658447, 1e-6);
	RB_CHECK_NEAR(I.TangentImpulse, 0.0076844, 1e-6);
	RB_CHECK(!I.Stick); // slide
	RB_CHECK_NEAR(I.Velocity1.x, 0.025, 1e-6);
	RB_CHECK_NEAR(I.Velocity1.y, 0.0, 1e-6);
	RB_CHECK_NEAR(I.Velocity1.z, 0.0451767, 1e-6);
	RB_CHECK_NEAR(I.Velocity2.x, 0.975, 1e-6);
	RB_CHECK_NEAR(I.Velocity2.y, 0.0, 1e-6);
	RB_CHECK_NEAR(I.Velocity2.z, -0.0451767, 1e-6);
	RB_CHECK_NEAR(kR * I.Omega1.x, 0.0, 1e-6);
	RB_CHECK_NEAR(kR * I.Omega1.y, 0.8870582, 1e-6);
	RB_CHECK_NEAR(kR * I.Omega1.z, 0.0, 1e-6);
	RB_CHECK_NEAR(kR * I.Omega2.x, 0.0, 1e-6);
	RB_CHECK_NEAR(kR * I.Omega2.y, -0.1129418, 1e-6);
	RB_CHECK_NEAR(kR * I.Omega2.z, 0.0, 1e-6);
}

RB_TEST(Integ_COL_BB2_TableStepAfterRollingImpact)
{
	// 2.4 step 6 with the motion spec's slate reaction (WP-1 ApplyTableReaction): CB hop suppressed; OB pressed into the
	// cloth, C.3 with e_slate 0.6, mu_s 0.2 gives v_z = 0.027106 < v_z_min = 0.198057 -> 0, v_x 0.960543, R w_y -0.076800.
	const CutShot S = MakeCut(1.0, 0.0, {0.0, 1.0 / kR, 0.0});
	const rb::BallBallImpulse I = Resolve(S, Params(0.95));
	const rb::BallSpec Spec = rb::MakeBallSpec(kR, kM);
	const rb::ClothParams Cloth{0.2, 0.010, 10.0};
	const rb::SlateParams Slate{0.6, 0.002, 10};
	const rb::NumericsConfig N;
	rb::BallState Cue{S.Cue.Position, I.Velocity1, I.Omega1, rb::MotionState::Rolling};
	rb::BallState Object{S.Object.Position, I.Velocity2, I.Omega2, rb::MotionState::Stationary};
	rb::ApplyTableReaction(Cue, true, Spec, Cloth, Slate, 9.80665, N);
	rb::ApplyTableReaction(Object, true, Spec, Cloth, Slate, 9.80665, N);
	RB_CHECK_NEAR(Cue.Velocity.z, 0.0, 1e-12);
	RB_CHECK_NEAR(Object.Velocity.z, 0.0, 1e-12);
	RB_CHECK_NEAR(Object.Velocity.x, 0.960543, 1e-6);
	RB_CHECK_NEAR(kR * Object.Omega.y, -0.076800, 1e-6);
	RB_CHECK_NEAR(rb::MinBounceSpeed(Slate, 9.80665), 0.198057, 1e-6);
}

RB_TEST(COL_BB3_StunThrowTableElastic)
{
	// e_b = 1 must equal TP A.14 to 1e-4 deg; the OB is thrown toward the CB's travel direction (negative sign).
	const double Speeds[3] = {0.447, 1.341, 3.129};
	const double Phis[3] = {10.0, 30.0, 45.0};
	const double Expected[3][3] = {{1.4430, 1.4430, 1.4430}, {4.7150, 3.5491, 1.6976}, {4.9451, 2.7734, 1.1273}};
	const bool ExpectStick[3][3] = {{true, true, true}, {true, false, false}, {false, false, false}};
	for (int p = 0; p < 3; ++p)
	{
		for (int v = 0; v < 3; ++v)
		{
			const rb::BallBallImpulse I = Resolve(MakeCut(Speeds[v], Phis[p] * kDeg), Params(1.0));
			const double Theta = ThrowDeg(I);
			RB_CHECK(Theta < 0.0);
			RB_CHECK_NEAR(-Theta, Expected[p][v], 1e-4);
			RB_CHECK_NEAR(-Theta, AlciatoreThrowDeg(Speeds[v], Phis[p] * kDeg, 0.0, 0.0, 1.0), 1e-9);
			RB_CHECK(I.Stick == ExpectStick[p][v]);
		}
	}
}

RB_TEST(COL_BB3b_StunThrowTableRestitution095)
{
	const double Speeds[3] = {0.447, 1.341, 3.129};
	for (int v = 0; v < 3; ++v)
	{
		RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(Speeds[v], 10.0 * kDeg), Params(0.95))), 1.4799, 1e-4);
	}
	RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(0.447, 30.0 * kDeg), Params(0.95))), 4.8353, 1e-4);
	// Slide cases equal the e_b = 1 values (tan(theta) = mu).
	RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(1.341, 30.0 * kDeg), Params(0.95))), 3.5491, 1e-4);
	RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(3.129, 30.0 * kDeg), Params(0.95))), 1.6976, 1e-4);
	RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(0.447, 45.0 * kDeg), Params(0.95))), 4.9451, 1e-4);
	RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(1.341, 45.0 * kDeg), Params(0.95))), 2.7734, 1e-4);
	RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(3.129, 45.0 * kDeg), Params(0.95))), 1.1273, 1e-4);
	for (int v = 0; v < 3; ++v)
	{
		for (double Phi : {10.0, 30.0, 45.0})
		{
			RB_CHECK_NEAR(-ThrowDeg(Resolve(MakeCut(Speeds[v], Phi * kDeg), Params(0.95))), AlciatoreThrowDeg(Speeds[v], Phi * kDeg, 0.0, 0.0, 0.95), 1e-9);
		}
	}
}

RB_TEST(COL_BB4_RollingCutThrow)
{
	const double V = 1.341;
	const rb::BallBallImpulse I = Resolve(MakeCut(V, 30.0 * kDeg, {0.0, V / kR, 0.0}), Params(1.0));
	RB_CHECK_NEAR(-ThrowDeg(I), 1.00422, 1e-4);
	RB_CHECK_NEAR(-ThrowDeg(I), AlciatoreThrowDeg(V, 30.0 * kDeg, V / kR, 0.0, 1.0), 1e-9);
}

RB_TEST(COL_BB5_StraightStunWithSideSpin)
{
	const double Speeds[3] = {0.447, 1.341, 3.129};
	const double Theta[3] = {4.0856, 3.5491, 1.6976};
	const double ObSpin[3] = {-0.07982, -0.20793, -0.23184};
	for (int v = 0; v < 3; ++v)
	{
		const double V = Speeds[v];
		const rb::BallBallImpulse I = Resolve(MakeCut(V, 0.0, {0.0, 0.0, 0.5 * V / kR}), Params(1.0));
		RB_CHECK_NEAR(ThrowDeg(I), Theta[v], 1e-4); // right English throws the OB to +y
		RB_CHECK_NEAR(kR * I.Omega2.z, ObSpin[v], 1e-4);
	}
}

RB_TEST(COL_BB6_GearingOutsideEnglish)
{
	const double V = 1.0;
	const double Phi = 30.0 * kDeg;
	const rb::BallBallImpulse I = Resolve(MakeCut(V, Phi, {0.0, 0.0, V * rb::Sin(Phi) / kR}), Params(0.95));
	RB_CHECK(I.TangentImpulse == 0.0);
	RB_CHECK(rb::Abs(ThrowDeg(I)) < 1e-9);
}

RB_TEST(COL_BB7_SpinTransferEqualDw)
{
	const rb::BallBallImpulse I = Resolve(MakeCut(0.5, 0.0, {0.0, 0.0, 0.5 / kR}), Params(0.95));
	RB_CHECK_NEAR(kR * I.Omega2.z, -0.0885258, 1e-6);
	RB_CHECK_NEAR(kR * I.Omega1.z, 0.4114742, 1e-6);
	// Equal dw for equal balls ("gear" coupling, 2.3 property 4).
	RB_CHECK_NEAR(I.Omega1.z - 0.5 / kR, I.Omega2.z, 1e-9);
}

RB_TEST(COL_BB8_AirborneCueBallOnObjectBall)
{
	const rb::Vec3 R1{0.0, 0.0, kR + 0.02};
	const rb::Vec3 R2 = R1 + rb::Vec3{rb::Sqrt(4.0 * kR * kR - 0.02 * 0.02), 0.0, -0.02};
	const rb::NumericsConfig N;
	const rb::BallBallImpulse I = rb::ResolveBallBall(Ball(R1, {2.0, 0.0, -0.5}), Ball(R2, {}), Params(0.95), N.RestSpeed, N.EpsV);
	RB_REQUIRE(I.Approaching);
	RB_CHECK(I.Stick);
	RB_CHECK_NEAR(I.Velocity1.x, 0.117425, 1e-6);
	RB_CHECK_NEAR(I.Velocity1.y, 0.0, 1e-6);
	RB_CHECK_NEAR(I.Velocity1.z, 0.167983, 1e-6);
	RB_CHECK_NEAR(I.Velocity2.x, 1.882575, 1e-6);
	RB_CHECK_NEAR(I.Velocity2.y, 0.0, 1e-6);
	RB_CHECK_NEAR(I.Velocity2.z, -0.667983, 1e-6);
}

namespace
{
	// Checks the 2.3 invariants on one random pair; returns false on a violation.
	struct InvariantResult
	{
		double Momentum = 0.0;        // relative
		double AngularMomentum = 0.0; // relative, about the origin
		double EnergyGain = 0.0;      // [J]
		double TwistChange = 0.0;     // |d(w . n)| [rad/s] (max of both balls)
	};

	InvariantResult CheckInvariants(const rb::ImpactBody& A, const rb::ImpactBody& B, const rb::BallBallParams& P)
	{
		const rb::NumericsConfig N;
		const rb::BallBallImpulse I = rb::ResolveBallBall(A, B, P, N.RestSpeed, N.EpsV);
		InvariantResult Out;
		const rb::Vec3 P0 = A.Velocity * A.Mass + B.Velocity * B.Mass;
		const rb::Vec3 P1 = I.Velocity1 * A.Mass + I.Velocity2 * B.Mass;
		Out.Momentum = rb::Length(P1 - P0) / rb::Max(rb::Length(P0), 1e-300);
		const rb::Vec3 L0 = rb::Cross(A.Position, A.Velocity) * A.Mass + rb::Cross(B.Position, B.Velocity) * B.Mass + A.Omega * A.Inertia + B.Omega * B.Inertia;
		const rb::Vec3 L1 = rb::Cross(A.Position, I.Velocity1) * A.Mass + rb::Cross(B.Position, I.Velocity2) * B.Mass + I.Omega1 * A.Inertia + I.Omega2 * B.Inertia;
		const double LScale = rb::Length(rb::Cross(A.Position, A.Velocity)) * A.Mass + rb::Length(rb::Cross(B.Position, B.Velocity)) * B.Mass +
			rb::Length(A.Omega) * A.Inertia + rb::Length(B.Omega) * B.Inertia;
		Out.AngularMomentum = rb::Length(L1 - L0) / rb::Max(LScale, 1e-300);
		const double E0 = 0.5 * (A.Mass * rb::LengthSquared(A.Velocity) + B.Mass * rb::LengthSquared(B.Velocity) + A.Inertia * rb::LengthSquared(A.Omega) +
			B.Inertia * rb::LengthSquared(B.Omega));
		const double E1 = 0.5 * (A.Mass * rb::LengthSquared(I.Velocity1) + B.Mass * rb::LengthSquared(I.Velocity2) + A.Inertia * rb::LengthSquared(I.Omega1) +
			B.Inertia * rb::LengthSquared(I.Omega2));
		Out.EnergyGain = E1 - E0;
		const rb::Vec3 Nrm = I.Normal;
		Out.TwistChange = rb::Max(rb::Abs(rb::Dot(I.Omega1 - A.Omega, Nrm)), rb::Abs(rb::Dot(I.Omega2 - B.Omega, Nrm)));
		return Out;
	}

	void RandomPair(rb::Rng& G, rb::ImpactBody& A, rb::ImpactBody& B)
	{
		const double R1 = G.NextUniform(0.028575, 0.0302); // pool ball .. oversized cue ball
		const double R2 = G.NextUniform(0.028575, 0.0286);
		const double M1 = G.NextUniform(0.156, 0.2);
		const double M2 = G.NextUniform(0.156, 0.170);
		rb::Vec3 N{G.NextNormal(), G.NextNormal(), G.NextNormal()};
		N = rb::Normalized(N);
		A.Radius = R1;
		A.Mass = M1;
		A.Inertia = 0.4 * M1 * R1 * R1;
		B.Radius = R2;
		B.Mass = M2;
		B.Inertia = 0.4 * M2 * R2 * R2;
		A.Position = {G.NextNormal(), G.NextNormal(), G.NextNormal()};
		B.Position = A.Position + N * (R1 + R2);
		auto RandomVec = [&G](double Limit) { return rb::Vec3{G.NextUniform(-Limit, Limit), G.NextUniform(-Limit, Limit), G.NextUniform(-Limit, Limit)}; };
		// |v| in [0, 10] m/s, w in [-300, 300] rad/s.
		A.Velocity = rb::Normalized(RandomVec(1.0)) * G.NextUniform(0.0, 10.0);
		B.Velocity = rb::Normalized(RandomVec(1.0)) * G.NextUniform(0.0, 10.0);
		if (rb::Dot(A.Velocity - B.Velocity, N) <= 0.0)
		{
			const rb::Vec3 Swap = A.Velocity;
			A.Velocity = B.Velocity;
			B.Velocity = Swap;
		}
		A.Omega = RandomVec(300.0);
		B.Omega = RandomVec(300.0);
	}
}

RB_TEST(COL_BB9_InvariantsOverRandomPairs)
{
	rb::Rng G(0xB9B9B9B9ull);
	const rb::BallBallParams P = Params(0.95);
	double Worst[4] = {};
	int Approaching = 0;
	for (int k = 0; k < 100000; ++k)
	{
		rb::ImpactBody A;
		rb::ImpactBody B;
		RandomPair(G, A, B);
		if (rb::Dot(A.Velocity - B.Velocity, rb::Normalized(B.Position - A.Position)) <= 0.0)
		{
			continue;
		}
		++Approaching;
		const InvariantResult R = CheckInvariants(A, B, P);
		Worst[0] = rb::Max(Worst[0], R.Momentum);
		Worst[1] = rb::Max(Worst[1], R.AngularMomentum);
		Worst[2] = rb::Max(Worst[2], R.EnergyGain);
		Worst[3] = rb::Max(Worst[3], R.TwistChange);
	}
	RB_CHECK(Approaching > 90000);
	RB_CHECK(Worst[0] <= 1e-12);
	RB_CHECK(Worst[1] <= 1e-12);
	RB_CHECK(Worst[2] <= 1e-12);
	RB_CHECK(Worst[3] <= 1e-12); // point contact transmits no twist (pitfall 3); rounding only
}

RB_TEST(COL_BB10_MirrorSymmetry)
{
	rb::Rng G(0xB10B10ull);
	const rb::BallBallParams P = Params(0.95);
	const rb::NumericsConfig N;
	auto Mirror = [](const rb::Vec3& V) { return rb::Vec3{V.x, -V.y, V.z}; };
	auto MirrorSpin = [](const rb::Vec3& W) { return rb::Vec3{-W.x, W.y, -W.z}; }; // pseudovector under y -> -y
	for (int k = 0; k < 2000; ++k)
	{
		rb::ImpactBody A;
		rb::ImpactBody B;
		RandomPair(G, A, B);
		rb::ImpactBody Am = A;
		rb::ImpactBody Bm = B;
		Am.Position = Mirror(A.Position);
		Am.Velocity = Mirror(A.Velocity);
		Am.Omega = MirrorSpin(A.Omega);
		Bm.Position = Mirror(B.Position);
		Bm.Velocity = Mirror(B.Velocity);
		Bm.Omega = MirrorSpin(B.Omega);
		const rb::BallBallImpulse I = rb::ResolveBallBall(A, B, P, N.RestSpeed, N.EpsV);
		const rb::BallBallImpulse Im = rb::ResolveBallBall(Am, Bm, P, N.RestSpeed, N.EpsV);
		RB_REQUIRE(I.Approaching == Im.Approaching);
		const rb::Vec3 D[4] = {Mirror(I.Velocity1) - Im.Velocity1, Mirror(I.Velocity2) - Im.Velocity2, MirrorSpin(I.Omega1) - Im.Omega1,
			MirrorSpin(I.Omega2) - Im.Omega2};
		for (const rb::Vec3& Diff : D)
		{
			RB_CHECK(rb::Length(Diff) <= 1e-14 * rb::Max(1.0, rb::Length(A.Omega) + rb::Length(B.Omega)));
		}
	}
	// The COL 9 cut set-up mirrored: throw changes sign exactly.
	for (double Phi : {10.0, 30.0, 45.0})
	{
		const double T = ThrowDeg(Resolve(MakeCut(1.341, Phi * kDeg, {0.0, 20.0, 15.0}), P));
		const double Tm = ThrowDeg(Resolve(MakeCut(1.341, -Phi * kDeg, {0.0, 20.0, -15.0}), P));
		RB_CHECK_NEAR(T, -Tm, 1e-14);
	}
}

RB_TEST(BallBall_FrictionLawValues)
{
	// 2.1: mu_b(0) = 0.1180, (0.5) = 0.0726, (1) = 0.0463, (2) = 0.0222, (5) = 0.0104; cling multiplies; test modes.
	const rb::BallBallParams P = Params(0.95);
	RB_CHECK_NEAR(rb::BallBallFriction(0.0, P), 0.1180, 5e-5);
	RB_CHECK_NEAR(rb::BallBallFriction(0.5, P), 0.0726, 5e-5);
	RB_CHECK_NEAR(rb::BallBallFriction(1.0, P), 0.0463, 5e-5);
	RB_CHECK_NEAR(rb::BallBallFriction(2.0, P), 0.0222, 5e-5);
	RB_CHECK_NEAR(rb::BallBallFriction(5.0, P), 0.0104, 5e-5);
	rb::BallBallParams Dirty = P;
	Dirty.ClingFactor = 1.5;
	RB_CHECK_NEAR(rb::BallBallFriction(1.0, Dirty), 1.5 * rb::BallBallFriction(1.0, P), 1e-15);
	RB_CHECK(rb::BallBallFriction(0.3, Params(0.95, rb::BallBallFrictionModel::Constant)) == 0.06);
	RB_CHECK(rb::BallBallFriction(0.3, Params(0.95, rb::BallBallFrictionModel::None)) == 0.0);
}

RB_TEST(BallBall_SeparatingAndMicroImpacts)
{
	// Step 1: separating / grazing pairs are not resolved; step 2: v_n < v_rest uses e = 0 (7.3).
	CutShot S = MakeCut(-0.5, 0.0);
	rb::BallBallImpulse I = Resolve(S, Params(0.95));
	RB_CHECK(!I.Approaching);
	RB_CHECK(I.Velocity1 == S.Cue.Velocity && I.Velocity2 == S.Object.Velocity);
	S = MakeCut(0.0, 0.0);
	RB_CHECK(!Resolve(S, Params(0.95)).Approaching);
	S = MakeCut(1.5e-3, 0.0);
	I = Resolve(S, Params(0.95));
	RB_REQUIRE(I.Approaching);
	RB_CHECK(I.RestitutionUsed == 0.0);
	RB_CHECK_NEAR(I.Velocity1.x, 0.75e-3, 1e-15);
	RB_CHECK_NEAR(I.Velocity2.x, 0.75e-3, 1e-15);
	S = MakeCut(2.5e-3, 0.0);
	RB_CHECK(Resolve(S, Params(0.95)).RestitutionUsed == 0.95);
}

RB_TEST(BallBall_UnequalBallsUsePerBallInertia)
{
	// k_t = 1/m1 + 1/m2 + R1^2/I1 + R2^2/I2 with a non-solid inertia: the stop-slip branch removes the tangential slip exactly.
	rb::ImpactBody A = Ball({0.0, 0.0, 0.0301}, {1.0, 0.0, 0.0}, {0.0, 0.0, 40.0});
	A.Radius = 0.0301;
	A.Mass = 0.19;
	A.Inertia = 0.35 * A.Mass * A.Radius * A.Radius; // k = 0.35
	rb::ImpactBody B = Ball({A.Radius + kR, 0.0, 0.0301}, {});
	rb::BallBallParams P = Params(0.95, rb::BallBallFrictionModel::Constant);
	P.MuConstant = 10.0; // forces the stop-slip cap
	const rb::NumericsConfig N;
	const rb::BallBallImpulse I = rb::ResolveBallBall(A, B, P, N.RestSpeed, N.EpsV);
	RB_REQUIRE(I.Approaching && I.Stick);
	const rb::Vec3 Nrm = I.Normal;
	const rb::Vec3 SlipAfter = (I.Velocity1 - I.Velocity2) + rb::Cross(I.Omega1 * A.Radius + I.Omega2 * B.Radius, Nrm);
	RB_CHECK(rb::Length(SlipAfter - Nrm * rb::Dot(SlipAfter, Nrm)) < 1e-14);
}

RB_TEST(BallBall_CutAngle)
{
	RB_CHECK_NEAR(rb::CutAngle({1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), 0.0, 1e-15);
	RB_CHECK_NEAR(rb::CutAngle({2.0, 0.0, 0.5}, {rb::Cos(0.5), rb::Sin(0.5), -0.3}), 0.5, 1e-14); // horizontal parts only
	RB_CHECK_NEAR(rb::CutAngle({0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}), 0.5 * rb::kPi, 1e-15);
	RB_CHECK(rb::CutAngle({0.0, 0.0, -1.0}, {1.0, 0.0, 0.0}) == 0.0); // no horizontal direction
	const CutShot S = MakeCut(1.0, 30.0 * kDeg);
	const rb::BallBallImpulse I = Resolve(S, Params(0.95));
	RB_CHECK_NEAR(rb::CutAngle(S.Cue.Velocity, I.Normal), 30.0 * kDeg, 1e-14);
}
