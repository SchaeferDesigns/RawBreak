// Adversarial review tests of the motion / slate / tilt API (WP-1 review; no spec IDs): symmetry and determinism,
// conservation and dissipation, extreme and near-singular inputs, NaN guards, no heap allocation in the hot paths.
// Regression tests of the review fixes: MOT_Review_TinySlopesStayFiniteAndLevel (NaN / wrong pursuit for tiny drives) and
// MOT_Review_NapTurnPerPieceAtMostFiftyMilliradians (the |dbeta| <= 0.05 rad refresh rule of human-factors 4.5.6).

#include "rbtest.h"

#include "Motion/MotionTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

#include <cmath>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
	#include <crtdbg.h>
	#define RB_REVIEW_ALLOC_HOOK 1
#else
	#define RB_REVIEW_ALLOC_HOOK 0
#endif

using namespace mottest;
using rb::BallState;
using rb::MotionSegment;
using rb::MotionState;
using rb::PursuitState;
using rb::TiltParams;
using rb::Vec2;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	TiltParams Slope(double Sx, double Sy, double Tolerance = 5e-5)
	{
		TiltParams Tilt;
		Tilt.Slope = {Sx, Sy};
		Tilt.Tolerance = Tolerance;
		return Tilt;
	}

	bool Finite(const Vec3& V) { return std::isfinite(V.x) && std::isfinite(V.y) && std::isfinite(V.z); }
	bool Finite(const Vec2& V) { return std::isfinite(V.x) && std::isfinite(V.y); }
	bool Finite(const BallState& S) { return Finite(S.Position) && Finite(S.Velocity) && Finite(S.Omega); }

	// Mirror y -> -y: positions / velocities (x, -y, z); angular velocity is a pseudovector: (-w_x, w_y, -w_z).
	Vec3 MirrorP(const Vec3& V) { return {V.x, -V.y, V.z}; }
	Vec3 MirrorW(const Vec3& W) { return {-W.x, W.y, -W.z}; }
	// Rotation by +90 deg about z (a proper rotation: vectors and pseudovectors alike).
	Vec3 Rot90(const Vec3& V) { return {-V.y, V.x, V.z}; }

	double NearRel(double A, double B) { return std::fabs(A - B) / rb::Max(1.0, rb::Max(std::fabs(A), std::fabs(B))); }

	// Mechanical energy on a tilted bed: kinetic + m g (height above the start), height = s . r_h (bed gradient s).
	double TiltedEnergy(const BallState& S, const rb::BallSpec& Spec, const TiltParams& Tilt)
	{
		return 0.5 * Spec.Mass * rb::LengthSquared(S.Velocity) + 0.5 * Spec.Inertia * rb::LengthSquared(S.Omega) +
			Spec.Mass * kG * (Tilt.Slope.x * S.Position.x + Tilt.Slope.y * S.Position.y);
	}
}

// ---------------------------------------------------------------------------------------------
// Symmetry and determinism
// ---------------------------------------------------------------------------------------------

RB_TEST(MOT_Review_MirrorAndRotationInvariance)
{
	rb::Rng Rng(0x5E1Fu);
	const rb::BallSpec Spec = MotSpec();
	for (int Case = 0; Case < 400; ++Case)
	{
		const Vec3 V{Rng.NextUniform(-3.0, 3.0), Rng.NextUniform(-3.0, 3.0), 0.0};
		const Vec3 W{Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-50.0, 50.0)};
		const double SlopeX = Case % 3 == 0 ? 0.0 : Rng.NextUniform(-3e-3, 3e-3);
		const double SlopeY = Case % 3 == 0 ? 0.0 : Rng.NextUniform(-3e-3, 3e-3);
		const BallState S = OnCloth(V, W);
		const BallState M = OnCloth(MirrorP(V), MirrorW(W));
		const BallState Q = OnCloth(Rot90(V), Rot90(W));
		RB_REQUIRE(S.State == M.State && S.State == Q.State);
		const BallRun A = RunBall(S, Spec, MotCloth(), MotSlate(), kG, Slope(SlopeX, SlopeY));
		const BallRun B = RunBall(M, Spec, MotCloth(), MotSlate(), kG, Slope(SlopeX, -SlopeY));
		const BallRun C = RunBall(Q, Spec, MotCloth(), MotSlate(), kG, Slope(-SlopeY, SlopeX));
		RB_REQUIRE(A.Segments.size() == B.Segments.size() && A.Segments.size() == C.Segments.size());
		for (std::size_t i = 0; i < A.Events.size(); ++i)
		{
			RB_CHECK(NearRel(A.Events[i].Time, B.Events[i].Time) <= 1e-14 && NearRel(A.Events[i].Time, C.Events[i].Time) <= 1e-14);
			const BallState& Ea = A.Events[i].State;
			const Vec3 Pm = MirrorP(B.Events[i].State.Position);
			const Vec3 Vm = MirrorP(B.Events[i].State.Velocity);
			const Vec3 Wm = MirrorW(B.Events[i].State.Omega);
			// Rotate C back by -90 deg: (x, y) -> (y, -x).
			const Vec3 Pr{C.Events[i].State.Position.y, -C.Events[i].State.Position.x, C.Events[i].State.Position.z};
			const Vec3 Vr{C.Events[i].State.Velocity.y, -C.Events[i].State.Velocity.x, C.Events[i].State.Velocity.z};
			const Vec3 Wr{C.Events[i].State.Omega.y, -C.Events[i].State.Omega.x, C.Events[i].State.Omega.z};
			RB_CHECK(Ea.State == B.Events[i].State.State && Ea.State == C.Events[i].State.State);
			for (const Vec3* P : {&Pm, &Pr})
			{
				RB_CHECK(rb::Length(*P - Ea.Position) <= 1e-13);
			}
			for (const Vec3* Vx : {&Vm, &Vr})
			{
				RB_CHECK(rb::Length(*Vx - Ea.Velocity) <= 1e-13);
			}
			for (const Vec3* Wx : {&Wm, &Wr})
			{
				RB_CHECK(rb::Length(*Wx - Ea.Omega) <= 1e-10);
			}
		}
	}
}

RB_TEST(MOT_Review_BitwiseDeterminism)
{
	// The same input twice (and interleaved with other work) gives bit-identical chains.
	const rb::BallSpec Spec = MotSpec();
	const BallState S = OnCloth({1.3, -0.4, 0.0}, {12.0, 30.0, -9.0});
	const TiltParams Tilt = Slope(1.7e-3, -0.9e-3);
	const BallRun A = RunBall(S, Spec, MotCloth(), MotSlate(), kG, Tilt);
	const BallRun Noise = RunBall(OnCloth({0.2, 0.9, 0.0}, Vec3::Zero()), Spec, MotCloth(), MotSlate(), kG, Slope(-2e-3, 1e-3));
	const BallRun B = RunBall(S, Spec, MotCloth(), MotSlate(), kG, Tilt);
	RB_CHECK(!Noise.Segments.empty());
	RB_REQUIRE(A.Segments.size() == B.Segments.size());
	for (std::size_t i = 0; i < A.Segments.size(); ++i)
	{
		RB_CHECK(SameSegment(A.Segments[i], B.Segments[i]));
		const double Tau = 0.37 * (A.Segments[i].TauEnd < rb::kInfinity ? A.Segments[i].TauEnd : 1.0);
		const BallState Ea = rb::EvaluateSegmentForEvent(A.Segments[i], Tau);
		const BallState Eb = rb::EvaluateSegmentForEvent(B.Segments[i], Tau);
		RB_CHECK(SameBits(Ea.Position, Eb.Position) && SameBits(Ea.Velocity, Eb.Velocity) && SameBits(Ea.Omega, Eb.Omega));
	}
}

// ---------------------------------------------------------------------------------------------
// Conservation and dissipation
// ---------------------------------------------------------------------------------------------

RB_TEST(MOT_Review_SlateImpactNeverAddsEnergy)
{
	rb::Rng Rng(0xE5A7u);
	const rb::SlateParams Slate = MotSlate();
	for (double InertiaK : {0.4, 0.5, 2.0 / 3.0})
	{
		rb::BallSpec Spec = MotSpec();
		Spec.Inertia = InertiaK * Spec.Mass * Spec.Radius * Spec.Radius;
		for (int Case = 0; Case < 5000; ++Case)
		{
			const Vec3 V{Rng.NextUniform(-8.0, 8.0), Rng.NextUniform(-8.0, 8.0), -Rng.NextUniform(1e-6, 6.0)};
			const Vec3 W{Rng.NextUniform(-400.0, 400.0), Rng.NextUniform(-400.0, 400.0), Rng.NextUniform(-400.0, 400.0)};
			const double E = Rng.NextUniform(0.0, 1.0);
			const double Mu = Rng.NextUniform(0.0, 0.5);
			const rb::SlateImpactResult Hit = rb::ResolveSlateImpact(V, W, Spec, E, Mu, Slate, 1 + Case % 12, kG, kNumerics);
			const double Before = 0.5 * Spec.Mass * rb::LengthSquared(V) + 0.5 * Spec.Inertia * rb::LengthSquared(W);
			const double After = 0.5 * Spec.Mass * rb::LengthSquared(Hit.Velocity) + 0.5 * Spec.Inertia * rb::LengthSquared(Hit.Omega);
			RB_CHECK(After <= Before * (1.0 + 1e-14));
			// The friction impulse never reverses the slip (Coulomb): u' . u >= 0.
			const Vec3 U0 = rb::SlipVelocity(V, W, kR);
			const Vec3 U1 = rb::SlipVelocity(Hit.Velocity, Hit.Omega, kR);
			RB_CHECK(U0.x * U1.x + U0.y * U1.y >= -1e-12);
			// Angular momentum about the contact point (horizontal part) is unchanged (A.5 / C.3, general k).
			const Vec3 L0 = rb::CoriolisInvariant(V, W, kR, InertiaK);
			const Vec3 L1 = rb::CoriolisInvariant(Hit.Velocity, Hit.Omega, kR, InertiaK);
			RB_CHECK(std::fabs(L0.x - L1.x) <= 1e-12 && std::fabs(L0.y - L1.y) <= 1e-12);
		}
	}
}

RB_TEST(MOT_Review_TiltedEnergyNonIncreasingAtNodes)
{
	// On a tilted bed the mechanical energy incl. the slope's potential never grows along the exact solution (the
	// nodes are exact; inside a piece the position is only within eps_tilt of it).
	rb::Rng Rng(0x71E7u);
	const rb::BallSpec Spec = MotSpec();
	for (int Case = 0; Case < 300; ++Case)
	{
		const double Angle = Rng.NextUniform(0.0, rb::kTwoPi);
		const double Mag = Rng.NextUniform(1e-4, 6.9e-3);
		const TiltParams Tilt = Slope(Mag * std::cos(Angle), Mag * std::sin(Angle));
		const BallState S = OnCloth({Rng.NextUniform(-2.0, 2.0), Rng.NextUniform(-2.0, 2.0), 0.0},
			{Rng.NextUniform(-100.0, 100.0), Rng.NextUniform(-100.0, 100.0), Rng.NextUniform(-30.0, 30.0)});
		const BallRun Run = RunBall(S, Spec, MotCloth(), MotSlate(), kG, Tilt);
		RB_CHECK(Run.Final.State == MotionState::Stationary);
		double Energy = TiltedEnergy(S, Spec, Tilt);
		for (const RunEvent& E : Run.Events)
		{
			const double After = TiltedEnergy(E.State, Spec, Tilt);
			RB_CHECK(After <= Energy + 1e-12 * rb::Max(1.0, std::fabs(Energy)));
			Energy = After;
		}
	}
}

RB_TEST(MOT_Review_NapTurnPerPieceAtMostFiftyMilliradians)
{
	// Human-factors 4.5.6: with nap resistance (eta_n > 0) the rolling resistance is frozen per piece, so the refresh rule
	// adds |dbeta| <= 0.05 rad. Every piece except the tail (|x_i| <= x_tail, runs to the exact stop) turns by at most
	// 0.05 rad; the chain still ends at the exact stop and within eps_tilt of the pursuit of each piece.
	const rb::BallSpec Spec = MotSpec();
	rb::Rng Rng(0x4A9u);
	int TurnLimited = 0;
	for (int Case = 0; Case < 60; ++Case)
	{
		TiltParams Nap;
		Nap.NapPseudoSlope = {2e-4, 0.0};
		Nap.NapResistance = 0.1;
		if (Case % 2 == 1)
		{
			Nap.Slope = {Rng.NextUniform(-2e-3, 2e-3), Rng.NextUniform(-2e-3, 2e-3)};
		}
		const double Angle = Rng.NextUniform(0.0, rb::kTwoPi);
		const double Speed = Rng.NextUniform(0.05, 1.2);
		const Vec2 V{Speed * std::cos(Angle), Speed * std::sin(Angle)};
		const BallRun Run = RunBall(OnCloth({V.x, V.y, 0.0}, {-V.y / kR, V.x / kR, 0.0}), Spec, MotCloth(), MotSlate(), kG, Nap);
		RB_CHECK(Run.Final.State == MotionState::Stationary);
		for (const MotionSegment& S : Run.Segments)
		{
			if (!S.Tilt.Active || S.State != MotionState::Rolling)
			{
				continue;
			}
			const double GNorm = rb::Length(S.Tilt.G);
			const double XTail = std::sqrt(Nap.Tolerance * (S.Tilt.K - GNorm) / 4.0);
			if (rb::Length(S.Tilt.X0) <= XTail)
			{
				continue; // tail piece: runs to the exact stop (freezing error there <= eta_n K pi T_tail^2 / 2 << eps_tilt)
			}
			// Direction at the end: the node's x, or G itself at the stop (the roll ends aligned with G, 4.5.2).
			const Vec2 End = S.Tilt.EndsInRefresh ? S.Tilt.XEnd : S.Tilt.G;
			const double Turn = std::atan2(std::fabs(rb::Cross(S.Tilt.X0, End)), rb::Dot(S.Tilt.X0, End));
			RB_CHECK(Turn <= 0.05 + 1e-12);
			TurnLimited += std::fabs(Turn - 0.05) <= 1e-9 ? 1 : 0;
		}
	}
	RB_CHECK(TurnLimited > 20); // the rule is active (it bounds many pieces exactly)
}

RB_TEST(MOT_Review_RandomTiltChainsStayWithinTolerance)
{
	// Random sliding / rolling starts, inertia factors, cloths, slopes up to the validity limit and both tolerances: every
	// piece stays within eps_tilt of the exact pursuit path re-anchored at its start (nodes are exact), velocities at the
	// nodes are continuous, and the ball comes to rest.
	rb::Rng Rng(0xC4A1u);
	int Pieces = 0;
	for (int Case = 0; Case < 300; ++Case)
	{
		const double InertiaK = Rng.NextUniform(0.3, 2.0 / 3.0);
		rb::BallSpec Spec = MotSpec();
		Spec.Inertia = InertiaK * Spec.Mass * Spec.Radius * Spec.Radius;
		const rb::ClothParams Cloth{Rng.NextUniform(0.15, 0.4), Rng.NextUniform(0.005, 0.015), Rng.NextUniform(5.0, 15.0)};
		const double Angle = Rng.NextUniform(0.0, rb::kTwoPi);
		const double Mag = Rng.NextUniform(0.0, 0.49 * Cloth.RollingResistance * (1.0 + InertiaK)); // |G| <= 0.49 K rolling
		const TiltParams Tilt = Slope(Mag * std::cos(Angle), Mag * std::sin(Angle), Case % 2 == 0 ? 5e-5 : 5e-4);
		const BallState S = OnCloth({Rng.NextUniform(-3.0, 3.0), Rng.NextUniform(-3.0, 3.0), 0.0},
			{Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-40.0, 40.0)});
		const BallRun Run = RunBall(S, Spec, Cloth, MotSlate(), kG, Tilt);
		RB_CHECK(Run.Final.State == MotionState::Stationary);
		for (std::size_t i = 0; i < Run.Segments.size(); ++i)
		{
			const MotionSegment& Seg = Run.Segments[i];
			if (!Seg.Tilt.Active)
			{
				continue;
			}
			++Pieces;
			const bool Sliding = Seg.State == MotionState::Sliding;
			const Vec2 Lc0 = XY(Seg.Vel0) - Seg.Tilt.X0 * Seg.Tilt.Cs;
			double Deviation = 0.0;
			for (int j = 1; j < 64; ++j)
			{
				const double Tau = Seg.TauEnd * j / 64.0;
				const PursuitState P = rb::EvaluatePursuit(Seg.Tilt.X0, Seg.Tilt.G, Seg.Tilt.K, Tau);
				const Vec2 Exact = Sliding ? Lc0 * Tau + Seg.Tilt.G * (0.5 * (1.0 - Seg.Tilt.Cs) * Tau * Tau) + P.Integral * Seg.Tilt.Cs : P.Integral;
				Deviation = rb::Max(Deviation, rb::Length(XY(rb::PositionAt(Seg, Tau) - Seg.Pos0) - Exact));
			}
			RB_CHECK(Deviation <= Tilt.Tolerance * (1.0 + 1e-9));
			if (Seg.Tilt.EndsInRefresh && i + 1 < Run.Segments.size())
			{
				// The next piece starts from the exact node: same position and velocity bits as SegmentEndState.
				const BallState Node = rb::SegmentEndState(Seg, kNumerics);
				RB_CHECK(SameBits(Run.Segments[i + 1].Pos0, Node.Position) && SameBits(Run.Segments[i + 1].Vel0, Node.Velocity));
				RB_CHECK(Run.Segments[i + 1].State == Seg.State);
			}
		}
	}
	RB_CHECK(Pieces > 1000);
}

// ---------------------------------------------------------------------------------------------
// Extreme and near-singular inputs
// ---------------------------------------------------------------------------------------------

RB_TEST(MOT_Review_TinySlopesStayFiniteAndLevel)
{
	// A slope that is not exactly zero but vanishingly small (denormal products) must neither produce NaN nor Zeno
	// chains: the result approaches the level motion.
	const rb::BallSpec Spec = MotSpec();
	for (double Mag : {1e-12, 1e-17, 1e-19, 1e-21, 1e-100, 1e-160, 1e-170, 1e-200, 1e-300, 5e-324})
	{
		for (const BallState& S : {OnCloth({0.3, 0.4, 0.0}, {-0.4 / kR, 0.3 / kR, 2.0}), OnCloth({0.3, 0.4, 0.0}, {20.0, -5.0, 1.0})})
		{
			const BallRun Level = RunBall(S, Spec, MotCloth(), MotSlate(), kG);
			const BallRun Tilted = RunBall(S, Spec, MotCloth(), MotSlate(), kG, Slope(Mag, -Mag), 0.0, 200);
			RB_CHECK(Tilted.Final.State == MotionState::Stationary);
			RB_CHECK(Tilted.Segments.size() <= 8u);
			RB_CHECK(Finite(Tilted.Final));
			for (const MotionSegment& Seg : Tilted.Segments)
			{
				RB_CHECK(Finite(Seg.Accel2) && Finite(Seg.OmegaDotH) && std::isfinite(Seg.TauEnd) == (Seg.TauEnd < rb::kInfinity));
				RB_CHECK(Finite(rb::EvaluateSegmentForEvent(Seg, 0.5 * (Seg.TauEnd < rb::kInfinity ? Seg.TauEnd : 1.0))));
			}
			RB_CHECK(rb::Length(Tilted.Final.Position - Level.Final.Position) <= 1e-9 + 1e3 * Mag);
			RB_CHECK(std::fabs(Tilted.StopTime - Level.StopTime) <= 1e-9 + 1e3 * Mag);
		}
		// The pursuit functions directly.
		const Vec2 G{Mag, -Mag};
		const PursuitState P = rb::EvaluatePursuit({0.3, 0.4}, G, 0.0980665, 1.0);
		RB_CHECK(Finite(P.X) && Finite(P.Integral));
		RB_CHECK(std::isfinite(rb::PursuitStopTime({0.3, 0.4}, G, 0.0980665)));
		RB_CHECK_NEAR(P.X.x, 0.3 * (1.0 - 0.0980665 / 0.5), 1e-9 + 1e3 * Mag);
		RB_CHECK_NEAR(P.X.y, 0.4 * (1.0 - 0.0980665 / 0.5), 1e-9 + 1e3 * Mag);
	}
}

RB_TEST(MOT_Review_NearCollinearPursuitStaysMonotone)
{
	// x0 within a few ulps of G (downhill) or -G (uphill): finite, |x| decreasing, the stop time between the collinear
	// limits, and continuous with the exactly collinear quadratic.
	const double K = 0.0980665;
	const Vec2 G{0.0, -0.035};
	for (double Beta : {1e-300, 1e-200, 1e-17, 1e-12, rb::kPi - 1e-15, rb::kPi - 1e-12, rb::kPi - 1e-9})
	{
		const Vec2 X0 = Vec2{std::sin(Beta), -std::cos(Beta)} * 0.6;
		const double TStop = rb::PursuitStopTime(X0, G, K);
		const bool Up = Beta > 1.0;
		const double Collinear = 0.6 / (Up ? K + 0.035 : K - 0.035);
		RB_CHECK(std::isfinite(TStop));
		RB_CHECK(NearRel(TStop, Collinear) <= 1e-8);
		double Last = 0.6;
		for (double F : {0.01, 0.3, 0.7, 0.99, 0.999999, 1.0})
		{
			const PursuitState P = rb::EvaluatePursuit(X0, G, K, F * TStop);
			RB_CHECK(Finite(P.X) && Finite(P.Integral));
			const double Len = rb::Length(P.X);
			RB_CHECK(Len <= Last + 1e-15);
			Last = Len;
		}
		// Tilt chain of a rolling ball with this direction terminates with a handful of pieces.
		const rb::BallSpec Spec = MotSpec();
		const Vec2 V = X0;
		TiltParams Tilt = Slope(0.0, 0.035 * (1.0 + rb::InertiaFactor(Spec)) / kG);
		const BallRun Run = RunBall(OnCloth({V.x, V.y, 0.0}, {-V.y / kR, V.x / kR, 0.0}), Spec, MotCloth(), MotSlate(), kG, Tilt, 0.0, 400);
		RB_CHECK(Run.Final.State == MotionState::Stationary);
		RB_CHECK(Run.Segments.size() <= 40u);
	}
}

RB_TEST(MOT_Review_ExtremeSpeedsAndSpins)
{
	const rb::BallSpec Spec = MotSpec();
	for (double Speed : {1e-8, 1e-3, 30.0, 1e4, 1e8})
	{
		for (double Spin : {0.0, 1e3, 1e6})
		{
			const BallState S = OnCloth({Speed, 0.25 * Speed, 0.0}, {Spin, -0.5 * Spin, 0.1 * Spin});
			for (const TiltParams& Tilt : {TiltParams{}, Slope(2e-3, 1e-3)})
			{
				const MotionSegment M = rb::MakeSegment(S, 3.0, Spec, MotCloth(), 0.0, kG, Tilt);
				RB_CHECK(M.TauEnd >= 0.0);
				RB_CHECK(Finite(M.Accel2) && Finite(M.OmegaDotH));
				const double Tau = M.TauEnd < rb::kInfinity ? 0.5 * M.TauEnd : 1.0;
				RB_CHECK(Finite(rb::EvaluateSegment(M, Tau)) && Finite(rb::EvaluateSegmentForEvent(M, Tau)));
				const BallState End = rb::SegmentEndState(M, kNumerics);
				RB_CHECK(Finite(End));
				if (!M.Tilt.Active && S.State == MotionState::Sliding)
				{
					// The Coriolis invariant holds at any scale (relative).
					const Vec3 L = rb::CoriolisInvariant(S.Velocity, S.Omega, kR);
					RB_CHECK(rb::Length(Vec3{End.Velocity.x - L.x, End.Velocity.y - L.y, 0.0}) <= 1e-12 * rb::Max(1.0, rb::Length(L)));
				}
			}
		}
	}
	// A very high flight lands at the exact time and speed.
	BallState High;
	High.Position = {0.0, 0.0, kR + 1e3};
	High.Velocity = {1.0, 0.0, 50.0};
	High.State = MotionState::Airborne;
	const MotionSegment Flight = rb::MakeSegment(High, 0.0, Spec, MotCloth(), 0.0, kG);
	const BallState Land = rb::SegmentEndState(Flight, kNumerics);
	RB_CHECK_NEAR(Land.Velocity.z, -std::sqrt(2500.0 + 2.0 * kG * 1e3), 1e-12 * 150.0);
	RB_CHECK_NEAR(rb::PositionAt(Flight, Flight.TauEnd).z, kR, 1e-9);
}

RB_TEST(MOT_Review_NonFiniteTimesAreClamped)
{
	const rb::BallSpec Spec = MotSpec();
	const MotionSegment M = rb::MakeSegment(OnCloth({1.0, 0.2, 0.0}, {3.0, 4.0, 5.0}), 0.0, Spec, MotCloth(), 0.0, kG, Slope(1e-3, 0.0));
	RB_REQUIRE(M.Tilt.Active);
	for (double Tau : {std::nan(""), -rb::kInfinity, -1.0, rb::kInfinity})
	{
		RB_CHECK(Finite(rb::EvaluateSegment(M, Tau)));
		RB_CHECK(Finite(rb::EvaluateSegmentForEvent(M, Tau)));
	}
	const PursuitState P = rb::EvaluatePursuit({0.5, 0.1}, {0.0, -0.007}, 0.0980665, rb::kInfinity);
	RB_CHECK(P.X == Vec2{} && Finite(P.Integral));
	const PursuitState Q = rb::EvaluatePursuit({0.5, 0.1}, {0.0, -0.007}, 0.0980665, std::nan(""));
	RB_CHECK(Finite(Q.X) && Finite(Q.Integral));
	RB_CHECK(rb::LandingTau(std::nan(""), -1.0, kR, kG) == 0.0);
}

// ---------------------------------------------------------------------------------------------
// No heap allocation in the hot paths (architecture 2 / 12): the debug CRT counts every allocation of the process
// while the WP-1 functions of the event loop run (MSVC Debug only; other builds run the same calls unchecked).
// ---------------------------------------------------------------------------------------------

namespace
{
#if RB_REVIEW_ALLOC_HOOK
	long& AllocationCount()
	{
		static long Count = 0;
		return Count;
	}

	int CountAllocations(int AllocType, void*, size_t, int, long, const unsigned char*, int)
	{
		if (AllocType == _HOOK_ALLOC || AllocType == _HOOK_REALLOC)
		{
			++AllocationCount();
		}
		return 1;
	}
#endif

	// Every WP-1 function the event loop calls per event, on level and tilted tables; returns a checksum.
	double HotPathWorkload()
	{
		const rb::BallSpec Spec = MotSpec();
		const rb::NumericsConfig Numerics;
		double Sum = 0.0;
		const TiltParams Tilts[2] = {TiltParams{}, [] {
										 TiltParams T;
										 T.Slope = {1.2e-3, -0.8e-3};
										 T.NapPseudoSlope = {2e-4, 0.0};
										 T.NapResistance = 0.1;
										 return T;
									 }()};
		BallState States[4];
		States[0] = OnCloth({1.1, 0.4, 0.0}, {20.0, -30.0, 7.0});
		States[1] = OnCloth({0.6, -0.3, 0.0}, {0.3 / kR, 0.6 / kR, -4.0});
		States[2] = OnCloth(Vec3::Zero(), {0.0, 0.0, 9.0});
		States[3].Position = {0.0, 0.0, kR + 0.01};
		States[3].Velocity = {1.0, 0.2, 0.7};
		States[3].State = MotionState::Airborne;
		for (const TiltParams& Tilt : Tilts)
		{
			for (BallState S : States)
			{
				rb::ClassifyState(S, kR, 0.0, Numerics);
				const MotionSegment M = rb::MakeSegment(S, 0.5, Spec, MotCloth(), 0.0, kG, Tilt);
				const double Tau = M.TauEnd < rb::kInfinity ? 0.3 * M.TauEnd : 0.3;
				Sum += rb::EvaluateSegment(M, Tau).Position.x + rb::EvaluateSegmentForEvent(M, Tau).Velocity.x;
				Sum += rb::SegmentEndState(M, Numerics).Velocity.y;
			}
		}
		Sum += rb::EvaluatePursuit({0.5, 0.2}, {0.004, -0.006}, 0.098, 1.7).X.x + rb::PursuitStopTime({0.5, 0.2}, {0.004, -0.006}, 0.098);
		Sum += rb::TiltPieceDuration(0.5, 0.098, 0.007, 1.0, 0.6, 5e-5, 2.0, 5.0);
		Sum += rb::LandingTau(kR + 0.02, -0.4, kR, kG);
		const rb::SlateImpactResult Hit = rb::ResolveSlateImpact({1.0, 0.0, -1.5}, {0.0, -80.0, 3.0}, Spec, 0.6, 0.2, MotSlate(), 1, kG, Numerics);
		Sum += Hit.Velocity.z;
		BallState Pressed = States[1];
		Pressed.Velocity.z = -0.05;
		rb::ApplyTableReaction(Pressed, true, Spec, MotCloth(), MotSlate(), kG, Numerics);
		Sum += Pressed.Velocity.x;
		rb::CueStrikeInput In;
		In.Speed = 3.0;
		In.Elevation = 0.2;
		In.OffsetA = 0.3;
		In.OffsetB = -0.2;
		BallState Rest;
		Rest.Position = {0.0, 0.0, kR};
		const rb::StrikeResult Strike = rb::StrikeCueBall(In, Rest, Spec, MotCloth(), MotSlate(), rb::PinchParams{}, kG, Numerics);
		Sum += Strike.State.Velocity.x;
		rb::CueTipPath Path = rb::MakeCueTipPath(In, Strike, Rest.Position, kR);
		Sum += rb::CueTipAsSegment(Path).TauEnd;
		BallState Other;
		Other.Position = Path.Start + Path.Direction * (0.01 + kR + Path.DomeRadius);
		Other.State = MotionState::Stationary;
		const rb::TipRecontactResult Tip = rb::ResolveTipRecontact(Path, 0.005, Other, Spec, In.Cue, MotCloth(), MotSlate(), kG, Numerics);
		Sum += Tip.Impulse;
		return Sum;
	}
}

RB_TEST(MOT_Review_NoHeapAllocationInHotPaths)
{
	const double Warm = HotPathWorkload(); // first call outside the window (lazy CRT initialisation, if any)
#if RB_REVIEW_ALLOC_HOOK
	AllocationCount() = 0;
	const _CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(&CountAllocations);
	{
		const std::vector<double> Control(8, 1.0); // positive control: the hook sees heap allocations
		RB_CHECK(AllocationCount() >= 1 && Control.size() == 8u);
	}
	AllocationCount() = 0;
	const double Sum = HotPathWorkload();
	const long Allocations = AllocationCount();
	_CrtSetAllocHook(Previous);
	RB_CHECK(Allocations == 0);
	RB_CHECK(SameBits(Sum, Warm)); // and bitwise deterministic
#else
	RB_CHECK(SameBits(HotPathWorkload(), Warm));
#endif
}
