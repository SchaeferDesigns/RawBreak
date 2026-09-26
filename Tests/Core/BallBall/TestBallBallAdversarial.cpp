// WP-3 review: adversarial tests of the ball-ball impulse and the cling helpers that the spec does not list (conservation over
// the whole parameter range incl. non-solid inertia and heavy cling, exact mirror symmetry, extreme and degenerate inputs,
// NaN / Inf guards, bitwise determinism).
#include "rbtest.h"

#include "BallBallTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Math/Quat.h"

#include <cstring>
#include <limits>

using namespace rbbb;

namespace
{
	bool SameBits(const rb::Vec3& A, const rb::Vec3& B) { return std::memcmp(&A, &B, sizeof(rb::Vec3)) == 0; }

	bool AllFinite(const rb::Vec3& V) { return rb::IsFinite(V.x) && rb::IsFinite(V.y) && rb::IsFinite(V.z); }

	struct Totals
	{
		rb::Vec3 Momentum;
		rb::Vec3 AngularMomentum; // about the origin
		double Energy = 0.0;
	};

	Totals Measure(const rb::ImpactBody& A, const rb::ImpactBody& B, const rb::Vec3& VA, const rb::Vec3& WA, const rb::Vec3& VB, const rb::Vec3& WB)
	{
		Totals T;
		T.Momentum = VA * A.Mass + VB * B.Mass;
		T.AngularMomentum = rb::Cross(A.Position, VA) * A.Mass + rb::Cross(B.Position, VB) * B.Mass + WA * A.Inertia + WB * B.Inertia;
		T.Energy = 0.5 * (A.Mass * rb::LengthSquared(VA) + B.Mass * rb::LengthSquared(VB) + A.Inertia * rb::LengthSquared(WA) +
			B.Inertia * rb::LengthSquared(WB));
		return T;
	}
}

RB_TEST(BallBall_Adv_InvariantsNonSolidInertiaHeavyClingAnyRestitution)
{
	// 2.3 properties 1-3 and 5 for every friction model, e in [0, 1], k_cling up to 3, per-ball inertia factors k in [0.3, 0.7]
	// (hollow / weighted balls), random 3D geometry with ball positions near the origin (angular momentum about the origin).
	rb::Rng G(0xADB0B0ull);
	const rb::NumericsConfig N;
	const rb::BallBallFrictionModel Models[3] = {rb::BallBallFrictionModel::Alciatore, rb::BallBallFrictionModel::Constant, rb::BallBallFrictionModel::None};
	double WorstP = 0.0;
	double WorstL = 0.0;
	double WorstE = -1.0;
	double WorstTwist = 0.0;
	int Stick = 0;
	int Slide = 0;
	for (int k = 0; k < 50000; ++k)
	{
		rb::BallBallParams P = Params(G.NextUniform(0.0, 1.0), Models[k % 3]);
		P.ClingFactor = G.NextUniform(1.0, 3.0);
		P.MuConstant = G.NextUniform(0.0, 0.5);
		rb::ImpactBody A;
		rb::ImpactBody B;
		A.Radius = G.NextUniform(0.026, 0.0302);
		B.Radius = G.NextUniform(0.026, 0.0302);
		A.Mass = G.NextUniform(0.14, 0.2);
		B.Mass = G.NextUniform(0.14, 0.2);
		A.Inertia = G.NextUniform(0.3, 0.7) * A.Mass * A.Radius * A.Radius;
		B.Inertia = G.NextUniform(0.3, 0.7) * B.Mass * B.Radius * B.Radius;
		const rb::Vec3 Nrm = rb::Normalized(rb::Vec3{G.NextNormal(), G.NextNormal(), G.NextNormal()});
		A.Position = rb::Vec3{G.NextUniform(-0.1, 0.1), G.NextUniform(-0.1, 0.1), A.Radius};
		B.Position = A.Position + Nrm * (A.Radius + B.Radius);
		A.Velocity = {G.NextUniform(-5.0, 5.0), G.NextUniform(-5.0, 5.0), G.NextUniform(-1.0, 1.0)};
		B.Velocity = {G.NextUniform(-5.0, 5.0), G.NextUniform(-5.0, 5.0), G.NextUniform(-1.0, 1.0)};
		A.Omega = {G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0)};
		B.Omega = {G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0)};
		const rb::BallBallImpulse I = rb::ResolveBallBall(A, B, P, N.RestSpeed, N.EpsV);
		if (!I.Approaching)
		{
			continue;
		}
		(I.Stick ? Stick : Slide) += 1;
		const Totals T0 = Measure(A, B, A.Velocity, A.Omega, B.Velocity, B.Omega);
		const Totals T1 = Measure(A, B, I.Velocity1, I.Omega1, I.Velocity2, I.Omega2);
		const double PScale = A.Mass * rb::Length(A.Velocity) + B.Mass * rb::Length(B.Velocity);
		const double LScale = rb::Length(rb::Cross(A.Position, A.Velocity)) * A.Mass + rb::Length(rb::Cross(B.Position, B.Velocity)) * B.Mass +
			rb::Length(A.Omega) * A.Inertia + rb::Length(B.Omega) * B.Inertia;
		WorstP = rb::Max(WorstP, rb::Length(T1.Momentum - T0.Momentum) / PScale);
		WorstL = rb::Max(WorstL, rb::Length(T1.AngularMomentum - T0.AngularMomentum) / LScale);
		WorstE = rb::Max(WorstE, T1.Energy - T0.Energy);
		WorstTwist = rb::Max(WorstTwist, rb::Max(rb::Abs(rb::Dot(I.Omega1 - A.Omega, I.Normal)), rb::Abs(rb::Dot(I.Omega2 - B.Omega, I.Normal))));
	}
	RB_CHECK(Stick > 1000 && Slide > 1000); // both branches exercised
	RB_CHECK(WorstP <= 1e-13);
	RB_CHECK(WorstL <= 1e-13);
	RB_CHECK(WorstE <= 1e-12);
	RB_CHECK(WorstTwist <= 1e-12);
}

RB_TEST(BallBall_Adv_StickBranchEndsSlipExactlyAnyInertia)
{
	// The stop-slip cap removes the tangential slip exactly (k_t with per-ball inertia, 2.3), for every geometry.
	rb::Rng G(0x57C4ull);
	const rb::NumericsConfig N;
	double Worst = 0.0;
	int Sticks = 0;
	for (int k = 0; k < 20000; ++k)
	{
		rb::BallBallParams P = Params(G.NextUniform(0.0, 1.0), rb::BallBallFrictionModel::Constant);
		P.MuConstant = 1e3; // always the stop-slip branch
		rb::ImpactBody A = Ball({}, {G.NextUniform(-3.0, 3.0), G.NextUniform(-3.0, 3.0), 0.0}, {G.NextUniform(-200.0, 200.0), G.NextUniform(-200.0, 200.0), G.NextUniform(-200.0, 200.0)});
		rb::ImpactBody B = Ball({}, {G.NextUniform(-3.0, 3.0), G.NextUniform(-3.0, 3.0), 0.0}, {G.NextUniform(-200.0, 200.0), G.NextUniform(-200.0, 200.0), G.NextUniform(-200.0, 200.0)});
		A.Radius = G.NextUniform(0.026, 0.0302);
		B.Radius = G.NextUniform(0.026, 0.0302);
		A.Inertia = G.NextUniform(0.3, 0.7) * A.Mass * A.Radius * A.Radius;
		B.Inertia = G.NextUniform(0.3, 0.7) * B.Mass * B.Radius * B.Radius;
		const rb::Vec3 Nrm = rb::Normalized(rb::Vec3{G.NextNormal(), G.NextNormal(), 0.2 * G.NextNormal()});
		B.Position = Nrm * (A.Radius + B.Radius);
		const rb::BallBallImpulse I = rb::ResolveBallBall(A, B, P, N.RestSpeed, N.EpsV);
		if (!I.Approaching || I.SlipSpeed < 1e-6 || !I.Stick) // a near-grazing hit (tiny J_n) can still slide at mu = 1000
		{
			continue;
		}
		++Sticks;
		const rb::Vec3 S = (I.Velocity1 - I.Velocity2) + rb::Cross(I.Omega1 * A.Radius + I.Omega2 * B.Radius, I.Normal);
		Worst = rb::Max(Worst, rb::Length(S - I.Normal * rb::Dot(S, I.Normal)) / I.SlipSpeed);
	}
	RB_CHECK(Sticks > 5000);
	RB_CHECK(Worst <= 1e-12);
}

RB_TEST(BallBall_Adv_MirrorIsBitExact)
{
	// COL BB-10 at its 1e-14 tolerance and beyond: y -> -y (w_x, w_z flip) mirrors every output bit for bit, because every
	// operation of the impulse commutes exactly with a sign flip.
	rb::Rng G(0x3141ull);
	const rb::NumericsConfig N;
	auto Mirror = [](const rb::Vec3& V) { return rb::Vec3{V.x, -V.y, V.z}; };
	auto MirrorSpin = [](const rb::Vec3& W) { return rb::Vec3{-W.x, W.y, -W.z}; };
	int Checked = 0;
	for (int k = 0; k < 20000; ++k)
	{
		const rb::BallBallParams P = Params(G.NextUniform(0.9, 1.0), k % 2 == 0 ? rb::BallBallFrictionModel::Alciatore : rb::BallBallFrictionModel::Constant);
		rb::ImpactBody A = Ball({G.NextUniform(-1.0, 1.0), G.NextUniform(-1.0, 1.0), kR}, {G.NextUniform(-8.0, 8.0), G.NextUniform(-8.0, 8.0), G.NextUniform(-1.0, 1.0)},
			{G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0), G.NextUniform(-300.0, 300.0)});
		const rb::Vec3 Nrm = rb::Normalized(rb::Vec3{G.NextNormal(), G.NextNormal(), 0.3 * G.NextNormal()});
		rb::ImpactBody B = Ball(A.Position + Nrm * (2.0 * kR), {G.NextUniform(-2.0, 2.0), G.NextUniform(-2.0, 2.0), 0.0},
			{G.NextUniform(-100.0, 100.0), G.NextUniform(-100.0, 100.0), G.NextUniform(-100.0, 100.0)});
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
		if (!I.Approaching)
		{
			continue;
		}
		++Checked;
		RB_CHECK(SameBits(Mirror(I.Velocity1), Im.Velocity1) && SameBits(Mirror(I.Velocity2), Im.Velocity2));
		RB_CHECK(SameBits(MirrorSpin(I.Omega1), Im.Omega1) && SameBits(MirrorSpin(I.Omega2), Im.Omega2));
		RB_CHECK(I.NormalImpulse == Im.NormalImpulse && I.TangentImpulse == Im.TangentImpulse && I.Stick == Im.Stick);
	}
	RB_CHECK(Checked > 5000);
}

RB_TEST(BallBall_Adv_DeterministicBits)
{
	// Same input -> same bits (no hidden state).
	const CutShot S = MakeCut(2.7, 0.61, {3.0, -40.0, 17.0});
	const rb::BallBallImpulse A = Resolve(S, Params(0.95));
	for (int k = 0; k < 3; ++k)
	{
		const rb::BallBallImpulse B = Resolve(S, Params(0.95));
		RB_CHECK(SameBits(A.Velocity1, B.Velocity1) && SameBits(A.Velocity2, B.Velocity2) && SameBits(A.Omega1, B.Omega1) && SameBits(A.Omega2, B.Omega2));
	}
}

RB_TEST(BallBall_Adv_ExtremeAndDegenerateInputs)
{
	const rb::NumericsConfig N;
	// Grazing: the normal speed is a hair above 0 while the tangential speed is large: e = 0 (micro-impact), friction is capped by
	// mu J_n, so nothing blows up and energy does not grow.
	{
		const double Phi = 0.5 * rb::kPi - 1e-12;
		const CutShot S = MakeCut(5.0, Phi, {0.0, 5.0 / kR, 30.0});
		const rb::BallBallImpulse I = Resolve(S, Params(0.95));
		RB_REQUIRE(I.Approaching);
		RB_CHECK(I.RestitutionUsed == 0.0);
		RB_CHECK(I.TangentImpulse <= I.Mu * I.NormalImpulse);
		RB_CHECK(AllFinite(I.Velocity1) && AllFinite(I.Omega1) && AllFinite(I.Velocity2) && AllFinite(I.Omega2));
		RB_CHECK(rb::Length(I.Velocity2) < 1e-9);
	}
	// Huge speeds and spins (far outside play): finite, momentum conserved, energy not increased.
	for (double V : {1e-12, 1e-6, 50.0, 1e4})
	{
		const CutShot S = MakeCut(V, 0.3, {1e4, -2e4, 3e4});
		const rb::BallBallImpulse I = Resolve(S, Params(0.95));
		RB_REQUIRE(I.Approaching);
		RB_CHECK(AllFinite(I.Velocity1) && AllFinite(I.Omega1) && AllFinite(I.Velocity2) && AllFinite(I.Omega2));
		const rb::Vec3 P0 = S.Cue.Velocity * kM;
		const rb::Vec3 P1 = (I.Velocity1 + I.Velocity2) * kM;
		RB_CHECK(rb::Length(P1 - P0) <= 1e-13 * rb::Max(rb::Length(P0), 1e-300));
		const double E0 = 0.5 * kM * rb::LengthSquared(S.Cue.Velocity) + 0.5 * kI * rb::LengthSquared(S.Cue.Omega);
		const double E1 = 0.5 * kM * (rb::LengthSquared(I.Velocity1) + rb::LengthSquared(I.Velocity2)) + 0.5 * kI * (rb::LengthSquared(I.Omega1) + rb::LengthSquared(I.Omega2));
		RB_CHECK(E1 <= E0 * (1.0 + 1e-14));
	}
	// Slip exactly at eps_v: no friction (t_hat undefined below eps_v); just above: friction.
	{
		CutShot S = MakeCut(1.0, 0.0);
		S.Cue.Velocity.y = 0.5e-9;
		RB_CHECK(Resolve(S, Params(0.95)).TangentImpulse == 0.0);
		S.Cue.Velocity.y = 2e-9;
		RB_CHECK(Resolve(S, Params(0.95)).TangentImpulse > 0.0);
	}
	// Coincident centres (corrupt state): nothing applied, no NaN.
	{
		const rb::ImpactBody A = Ball({0.0, 0.0, kR}, {1.0, 0.0, 0.0});
		const rb::BallBallImpulse I = rb::ResolveBallBall(A, A, Params(0.95), N.RestSpeed, N.EpsV);
		RB_CHECK(!I.Approaching && I.Velocity1 == A.Velocity && I.Velocity2 == A.Velocity);
	}
	// NaN / Inf velocities never count as approaching and nothing is applied.
	{
		const double Nan = std::numeric_limits<double>::quiet_NaN();
		CutShot S = MakeCut(1.0, 0.0);
		S.Cue.Velocity.x = Nan;
		RB_CHECK(!Resolve(S, Params(0.95)).Approaching);
		S = MakeCut(1.0, 0.0);
		S.Object.Position.x = Nan;
		RB_CHECK(!Resolve(S, Params(0.95)).Approaching);
	}
	// CutAngle stays in [0, pi] and is finite for any direction.
	RB_CHECK_NEAR(rb::CutAngle({-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), rb::kPi, 1e-15);
	RB_CHECK(rb::CutAngle({1e-60, 0.0, 0.0}, {0.0, 1e-60, 0.0}) == 0.5 * rb::kPi);
}

RB_TEST(BallBall_Adv_ChalkWeightGuards)
{
	// Degenerate marks and contact directions: no NaN; the weight is invariant to the length of ContactDir and of BodyDir.
	rb::BallChalkMarks Marks;
	rb::ChalkMark Mark;
	Mark.BodyDir = {0.0, 3.0, 0.0}; // not unit
	Mark.Strength = 0.8;
	Mark.Radius = 2.5e-3;
	Marks.PushBack(Mark);
	const rb::Quat Q = rb::FromAxisAngle({1.0, 1.0, 0.0}, 0.0);
	const double Unit = rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, {0.0, 1.0, 0.0});
	RB_CHECK_NEAR(Unit, 0.8, 1e-15);
	RB_CHECK(rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, {0.0, 1e-100, 0.0}) == Unit);
	RB_CHECK(rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, {}) == 0.0);
	rb::ChalkMark Empty;
	Empty.Strength = 1.0;
	Empty.Radius = 2.5e-3; // zero body direction: ignored
	Marks.PushBack(Empty);
	RB_CHECK(rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, {0.0, 1.0, 0.0}) == Unit);
	RB_CHECK(rb::IsFinite(rb::ChalkMarkWeight(Marks, Q, kR, {0.0, -1.0, 0.0})));
	// Opposite side of the ball: exp(-(R pi / r)^2) underflows to 0.
	RB_CHECK(rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, {0.0, -1.0, 0.0}) == 0.0);
	// ContactClingFactor: negative weights clamp to k_venue, huge weights saturate, the result never leaves [k_venue, max(k_chalk, k_venue)].
	const rb::BallBallParams P = Params(0.95);
	RB_CHECK(rb::ContactClingFactor(-5.0, 0.0, P) == P.ClingFactor);
	RB_CHECK(rb::ContactClingFactor(1e300, 1e300, P) == P.ChalkClingFactor);
	RB_CHECK(rb::ContactClingFactor(rb::kInfinity, 0.0, P) == P.ChalkClingFactor);
}
