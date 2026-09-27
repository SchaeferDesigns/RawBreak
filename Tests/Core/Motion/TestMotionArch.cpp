// Architecture tests A-MOT-1 (general inertia factor), A-MOT-2 (segments on the flat rail cap) and robustness /
// throughput checks of the segment API that the event loop builds on (WP-1).

#include "rbtest.h"

#include "Motion/MotionTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Cushion.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace mottest;
using rb::BallState;
using rb::MotionSegment;
using rb::MotionState;
using rb::Vec2;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	rb::BallSpec SpecWithK(double InertiaK)
	{
		rb::BallSpec Spec = MotSpec();
		Spec.Inertia = InertiaK * Spec.Mass * Spec.Radius * Spec.Radius;
		return Spec;
	}

	bool AllFinite(const BallState& S)
	{
		return std::isfinite(S.Position.x) && std::isfinite(S.Position.y) && std::isfinite(S.Position.z) && std::isfinite(S.Velocity.x) &&
			std::isfinite(S.Velocity.y) && std::isfinite(S.Velocity.z) && std::isfinite(S.Omega.x) && std::isfinite(S.Omega.y) && std::isfinite(S.Omega.z);
	}
}

RB_TEST(ARCH_MOT1_GeneralInertiaFactor)
{
	rb::Rng Rng(0xA0101u);
	for (double InertiaK : {0.35, 0.4, 0.5, 2.0 / 3.0})
	{
		const rb::BallSpec Spec = SpecWithK(InertiaK);
		RB_CHECK_NEAR(rb::InertiaFactor(Spec), InertiaK, 1e-15);
		for (int Case = 0; Case < 500; ++Case)
		{
			const Vec3 V{Rng.NextUniform(-5.0, 5.0), Rng.NextUniform(-5.0, 5.0), 0.0};
			const Vec3 W{Rng.NextUniform(-200.0, 200.0), Rng.NextUniform(-200.0, 200.0), Rng.NextUniform(-50.0, 50.0)};
			const BallState S = OnCloth(V, W);
			if (S.State != MotionState::Sliding)
			{
				continue;
			}
			const MotionSegment M = rb::MakeSegment(S, 0.0, Spec, MotCloth(), 0.0, kG);
			const Vec3 U0 = rb::SlipVelocity(V, W, kR);
			const double ULen = std::hypot(U0.x, U0.y);
			// Slide duration k |u| / ((1 + k) mu_s g); spin rate mu_s g / (k R).
			const double Duration = InertiaK * ULen / ((1.0 + InertiaK) * 0.2 * kG);
			RB_CHECK_NEAR(M.TauEnd, Duration, 1e-12 * Duration);
			RB_CHECK_NEAR(rb::SlideDuration(ULen, 0.2, kG, InertiaK), Duration, 1e-12 * Duration);
			RB_CHECK_NEAR(std::hypot(M.OmegaDotH.x, M.OmegaDotH.y), 0.2 * kG / (InertiaK * kR), 1e-9);
			// The slide ends with u = 0 by its own evolution, at the Coriolis invariant L_c(k).
			const BallState AtEnd = rb::EvaluateSegment(M, M.TauEnd);
			const Vec3 UEnd = rb::SlipVelocity(AtEnd.Velocity, AtEnd.Omega, kR);
			RB_CHECK_NEAR(UEnd.x, 0.0, 1e-12);
			RB_CHECK_NEAR(UEnd.y, 0.0, 1e-12);
			const Vec3 L = rb::CoriolisInvariant(V, W, kR, InertiaK);
			const BallState End = rb::SegmentEndState(M, kNumerics);
			RB_CHECK_NEAR(End.Velocity.x, L.x, 1e-12);
			RB_CHECK_NEAR(End.Velocity.y, L.y, 1e-12);
			// L_c(k) is constant along the slide.
			const BallState Mid = rb::EvaluateSegment(M, 0.37 * M.TauEnd);
			const Vec3 LMid = rb::CoriolisInvariant(Mid.Velocity, Mid.Omega, kR, InertiaK);
			RB_CHECK_NEAR(LMid.x, L.x, 1e-12);
			RB_CHECK_NEAR(LMid.y, L.y, 1e-12);
			// Slate impacts keep L_c(k) and w_z; the stick branch ends with u' = 0.
			const Vec3 Down{V.x, V.y, -Rng.NextUniform(0.05, 3.0)};
			const rb::SlateImpactResult Hit = rb::ResolveSlateImpact(Down, W, Spec, 0.6, 0.2, MotSlate(), 1, kG, kNumerics);
			const Vec3 LHit = rb::CoriolisInvariant(Hit.Velocity, Hit.Omega, kR, InertiaK);
			RB_CHECK_NEAR(LHit.x, L.x, 1e-12);
			RB_CHECK_NEAR(LHit.y, L.y, 1e-12);
			RB_CHECK(Hit.Omega.z == W.z);
			if (Hit.Stick)
			{
				const Vec3 UHit = rb::SlipVelocity(Hit.Velocity, Hit.Omega, kR);
				RB_CHECK_NEAR(std::hypot(UHit.x, UHit.y), 0.0, 1e-13);
			}
		}
	}
}

RB_TEST(ARCH_MOT2_SegmentsOnTheFlatRailCap)
{
	const double Cap = 0.048; // RailTopZ of the table presets
	const rb::ClothParams CapSurface = rb::RailCapSurface(rb::PocketContactParams{});
	const rb::BallSpec Spec = MotSpec();

	// Classification relative to the support plane z = SupportZ.
	BallState S;
	S.Position = {1.0, 0.7, Cap + kR + 3e-10};
	S.Velocity = {0.3, 0.1, 0.0};
	S.Omega = {-0.1 / kR, 0.3 / kR, 2.0};
	RB_CHECK(rb::ClassifyState(S, kR, Cap, kNumerics) == MotionState::Rolling);
	RB_CHECK(S.Position.z == Cap + kR);
	BallState Above = S;
	Above.Position.z = Cap + kR + 1e-6;
	RB_CHECK(rb::ClassifyState(Above, kR, Cap, kNumerics) == MotionState::Airborne);
	BallState OverCloth = S;
	RB_CHECK(rb::ClassifyState(OverCloth, kR, 0.0, kNumerics) == MotionState::Airborne); // the same height is airborne over the cloth

	// Rolling to rest on the cap with the rail-cap friction; the height never changes.
	const MotionSegment Roll = rb::MakeSegment(S, 2.0, Spec, CapSurface, Cap, kG);
	RB_CHECK(Roll.SupportZ == Cap && Roll.Pos0.z == Cap + kR);
	RB_CHECK_NEAR(Roll.TauEnd, std::hypot(0.3, 0.1) / (CapSurface.RollingResistance * kG), 1e-12);
	for (int j = 0; j <= 10; ++j)
	{
		RB_CHECK(rb::EvaluateSegment(Roll, Roll.TauEnd * j / 10.0).Position.z == Cap + kR);
	}
	const BallState Rest = rb::SegmentEndState(Roll, kNumerics);
	RB_CHECK(Rest.Position.z == Cap + kR);
	RB_CHECK(Rest.State == MotionState::Spinning || Rest.State == MotionState::Stationary);
	RB_CHECK(Rest.Velocity == Vec3::Zero());

	// Sliding on the cap uses the cap's sliding friction and ends rolling on the cap.
	BallState Slide;
	Slide.Position = {1.0, 0.7, Cap + kR};
	Slide.Velocity = {0.5, 0.0, 0.0};
	RB_CHECK(rb::ClassifyState(Slide, kR, Cap, kNumerics) == MotionState::Sliding);
	const MotionSegment SlideSeg = rb::MakeSegment(Slide, 0.0, Spec, CapSurface, Cap, kG);
	RB_CHECK_NEAR(-2.0 * SlideSeg.Accel2.x, CapSurface.SlidingFriction * kG, 1e-15);
	const BallState Rolled = rb::SegmentEndState(SlideSeg, kNumerics);
	RB_CHECK(Rolled.State == MotionState::Rolling && Rolled.Position.z == Cap + kR);
	RB_CHECK_NEAR(Rolled.Velocity.x, 5.0 / 7.0 * 0.5, 1e-12);

	// A hop from the cap lands back on the cap plane when the segment refers to it.
	BallState Hop;
	Hop.Position = {1.0, 0.7, Cap + kR};
	Hop.Velocity = {0.2, 0.0, 0.5};
	RB_CHECK(rb::ClassifyState(Hop, kR, Cap, kNumerics) == MotionState::Airborne);
	const MotionSegment Flight = rb::MakeSegment(Hop, 0.0, Spec, CapSurface, Cap, kG);
	RB_CHECK_NEAR(Flight.TauEnd, 1.0 / kG, 1e-15);
	const BallState Land = rb::SegmentEndState(Flight, kNumerics);
	RB_CHECK(Land.Position.z == Cap + kR && Land.State == MotionState::Airborne);
	RB_CHECK_NEAR(Land.Velocity.z, -0.5, 1e-15);
	// Table reaction on the cap after an impulse: a downward v_z is resolved with the cap's friction.
	BallState Pressed = S;
	Pressed.Velocity.z = -0.05;
	rb::ApplyTableReaction(Pressed, true, Spec, CapSurface, MotSlate(), kG, kNumerics);
	RB_CHECK(Pressed.Velocity.z == 0.0); // 0.03 m/s rebound < v_z_min
	RB_CHECK(rb::ClassifyState(Pressed, kR, Cap, kNumerics) != MotionState::Airborne);
}

// ---------------------------------------------------------------------------------------------
// Robustness of the segment API (no spec IDs)
// ---------------------------------------------------------------------------------------------

RB_TEST(MOT_Robust_DegenerateSegmentsStayFinite)
{
	const rb::BallSpec Spec = MotSpec();
	// States whose classification disagrees with their kinematics (zero slip, zero speed, zero spin).
	BallState Slide;
	Slide.Position = {0.0, 0.0, kR};
	Slide.Velocity = {1.0, 0.0, 0.0};
	Slide.Omega = {0.0, 1.0 / kR, 0.0};
	Slide.State = MotionState::Sliding;
	BallState Roll = Slide;
	Roll.Velocity = Vec3::Zero();
	Roll.Omega = Vec3::Zero();
	Roll.State = MotionState::Rolling;
	BallState Spin = Roll;
	Spin.State = MotionState::Spinning;
	for (const BallState& S : {Slide, Roll, Spin})
	{
		for (const rb::TiltParams& Tilt : {rb::TiltParams{}, [] {
				 rb::TiltParams T;
				 T.Slope = {1e-3, 0.0};
				 return T;
			 }()})
		{
			const MotionSegment M = rb::MakeSegment(S, 0.0, Spec, MotCloth(), 0.0, kG, Tilt);
			RB_CHECK(M.TauEnd >= 0.0 && M.TauEnd < 1e-12);
			const BallState E = rb::EvaluateSegment(M, 0.5);
			RB_CHECK(AllFinite(E));
			const BallState End = rb::SegmentEndState(M, kNumerics);
			RB_CHECK(AllFinite(End));
			RB_CHECK(End.State == (S.State == MotionState::Sliding ? MotionState::Rolling : MotionState::Stationary));
			RB_CHECK(AllFinite(rb::EvaluateSegmentForEvent(M, 0.1)));
		}
	}
	// Frictionless supports: no end of the phase, no NaN.
	const rb::ClothParams Ice{0.0, 0.0, 0.0};
	const MotionSegment Glide = rb::MakeSegment(OnCloth({1.0, 0.0, 0.0}, {0.0, 0.0, 3.0}), 0.0, Spec, Ice, 0.0, kG);
	RB_CHECK(!(Glide.TauEnd < rb::kInfinity));
	const BallState G = rb::EvaluateSegment(Glide, 2.0);
	RB_CHECK(AllFinite(G) && G.Position.x == 2.0 && G.Omega.z == 3.0);
	// An airborne ball below its landing plane lands at once (the router decides); never NaN.
	BallState Low;
	Low.Position = {0.0, 0.0, kR - 1e-3};
	Low.Velocity = {0.5, 0.0, -1.0};
	Low.State = MotionState::Airborne;
	const MotionSegment Fall = rb::MakeSegment(Low, 0.0, Spec, MotCloth(), 0.0, kG);
	RB_CHECK(Fall.TauEnd == 0.0);
	RB_CHECK(AllFinite(rb::SegmentEndState(Fall, kNumerics)));
	// Terminal states are frozen.
	BallState Pocketed = Low;
	Pocketed.State = MotionState::Pocketed;
	const MotionSegment Frozen = rb::MakeSegment(Pocketed, 1.0, Spec, MotCloth(), 0.0, kG);
	RB_CHECK(rb::EvaluateSegment(Frozen, 5.0).Position == Low.Position && rb::EvaluateSegment(Frozen, 5.0).Velocity == Vec3::Zero());
}

RB_TEST(MOT_Robust_LandingTimeWithoutCancellation)
{
	// Falling balls just above the cloth: the later root must keep full relative precision.
	for (double Height : {1e-12, 1e-9, 1e-6, 1e-3, 0.05})
	{
		for (double Vz : {-3.0, -0.2, 0.0, 0.2, 3.0})
		{
			const double Z0 = kR + Height;
			const double H = Z0 - kR; // the height the function sees (exact difference of doubles)
			const double Tau = rb::LandingTau(Z0, Vz, kR, kG);
			RB_CHECK(Tau > 0.0);
			const double Z = H + Vz * Tau - 0.5 * kG * Tau * Tau; // residual of z(tau) = R
			const double Scale = rb::Max(H, rb::Max(std::fabs(Vz) * Tau, 0.5 * kG * Tau * Tau));
			RB_CHECK(std::fabs(Z) <= 1e-14 * Scale);
		}
	}
	RB_CHECK(rb::LandingTau(kR, -1.0, kR, kG) == 0.0);
	RB_CHECK_NEAR(rb::LandingTau(kR, 1.0, kR, kG), 2.0 / kG, 1e-16);
	// A small falling height: the naive (v + sqrt(v^2 + 2gh))/g loses all digits.
	const double Tiny = (kR + 1e-12) - kR;
	const double Tau = rb::LandingTau(kR + 1e-12, -3.0, kR, kG);
	RB_CHECK_NEAR(Tau, Tiny / 3.0, 1e-9 * Tiny);
}

RB_TEST(MOT_Robust_SpinClampIsExact)
{
	rb::Rng Rng(0x5C1u);
	for (int Case = 0; Case < 2000; ++Case)
	{
		const double Wz = Rng.NextUniform(-80.0, 80.0);
		const double Alpha = Rng.NextUniform(3.0, 17.0);
		const rb::ClothParams Cloth{0.2, 0.01, Alpha};
		const MotionSegment M = rb::MakeSegment(OnCloth({0.5, 0.0, 0.0}, {0.0, 0.5 / kR, Wz}), 0.0, MotSpec(), Cloth, 0.0, kG);
		RB_CHECK_NEAR(M.OmegaZStopTau, std::fabs(Wz) / Alpha, 1e-15 * M.OmegaZStopTau + 1e-300);
		for (double F : {0.0, 0.25, 0.999999999, 1.0, 1.5})
		{
			const double W = rb::OmegaZAt(M, F * M.OmegaZStopTau);
			RB_CHECK(W == 0.0 || (W > 0.0) == (Wz > 0.0)); // never flips its sign
			if (F >= 1.0)
			{
				RB_CHECK(W == 0.0);
			}
		}
	}
}

RB_TEST(MOT_Robust_SegmentContinuityAndEnergyAcrossTransitions)
{
	rb::Rng Rng(0xE7u);
	const rb::BallSpec Spec = MotSpec();
	for (int Case = 0; Case < 500; ++Case)
	{
		BallState S = OnCloth({Rng.NextUniform(-4.0, 4.0), Rng.NextUniform(-4.0, 4.0), Rng.NextUniform(-0.5, 1.5)},
			{Rng.NextUniform(-250.0, 250.0), Rng.NextUniform(-250.0, 250.0), Rng.NextUniform(-60.0, 60.0)});
		if (S.State == MotionState::Airborne && S.Velocity.z < 0.0)
		{
			continue;
		}
		const BallRun Run = RunBall(S, Spec, MotCloth(), MotSlate(), kG);
		RB_CHECK(Run.Final.State == MotionState::Stationary);
		double Energy = rb::MechanicalEnergy(S, Spec, kG);
		for (std::size_t i = 0; i < Run.Events.size(); ++i)
		{
			const MotionSegment& Seg = Run.Segments[i];
			const RunEvent& E = Run.Events[i];
			// Position continuity: the end state starts where the segment ends (landings snap z to the plane).
			const Vec3 P = rb::PositionAt(Seg, Seg.TauEnd);
			RB_CHECK(std::fabs(E.State.Position.x - P.x) <= 1e-12 && std::fabs(E.State.Position.y - P.y) <= 1e-12);
			RB_CHECK(std::fabs(E.State.Position.z - P.z) <= 1e-12);
			const double After = rb::MechanicalEnergy(E.State, Spec, kG);
			RB_CHECK(After <= Energy * (1.0 + 1e-12) + 1e-15);
			Energy = After;
			RB_CHECK(Run.Segments[i + 1].T0 == E.Time);
		}
	}
}

RB_TEST(MOT_Slow_SegmentThroughput)
{
	// Measured in Release; printed for the report (no gate: A-PERF-1 is WP-10's).
	using Clock = std::chrono::steady_clock;
	const rb::BallSpec Spec = MotSpec();
	const int N = 200000;
	volatile double Sink = 0.0;
	rb::TiltParams Tilt;
	Tilt.Slope = {1e-3, 1e-3};
	const BallState States[3] = {OnCloth({2.0, 0.3, 0.0}, {30.0, -10.0, 5.0}), OnCloth({0.7, -0.2, 0.0}, {0.2 / kR, 0.7 / kR, 3.0}),
		OnCloth({0.0, 0.0, 0.5}, {3.0, 4.0, 5.0})};
	auto Time = [&](const char* Name, auto&& Body) {
		const Clock::time_point T0 = Clock::now();
		for (int i = 0; i < N; ++i)
		{
			Body(i);
		}
		const double Ns = std::chrono::duration<double, std::nano>(Clock::now() - T0).count() / N;
		std::printf("  %-44s %8.1f ns\n", Name, Ns);
	};
	Time("MakeSegment (level, sliding/rolling/flight)", [&](int i) { Sink = Sink + rb::MakeSegment(States[i % 3], 0.0, Spec, MotCloth(), 0.0, kG).TauEnd; });
	const MotionSegment Level = rb::MakeSegment(States[0], 0.0, Spec, MotCloth(), 0.0, kG);
	Time("EvaluateSegment (sliding)", [&](int i) { Sink = Sink + rb::EvaluateSegment(Level, 1e-6 * (i % 1000)).Position.x; });
	Time("SegmentEndState (sliding)", [&](int) { Sink = Sink + rb::SegmentEndState(Level, kNumerics).Velocity.x; });
	Time("MakeSegment (tilted chain piece)", [&](int i) { Sink = Sink + rb::MakeSegment(States[i % 2], 0.0, Spec, MotCloth(), 0.0, kG, Tilt).TauEnd; });
	const MotionSegment Piece = rb::MakeSegment(States[1], 0.0, Spec, MotCloth(), 0.0, kG, Tilt);
	Time("EvaluateSegmentForEvent (tilt piece)", [&](int i) { Sink = Sink + rb::EvaluateSegmentForEvent(Piece, 1e-4 * (i % 1000)).Velocity.x; });
	Time("EvaluatePursuit", [&](int i) { Sink = Sink + rb::EvaluatePursuit({0.7, -0.2}, {0.005, 0.003}, 0.098, 1e-3 * (i % 1000)).X.x; });
	rb::CueStrikeInput In;
	In.Speed = 3.0;
	In.OffsetA = 0.2;
	In.OffsetB = 0.1;
	In.Elevation = 0.1;
	BallState Rest;
	Rest.Position = {0.0, 0.0, kR};
	Time("StrikeCueBall", [&](int i) {
		In.Azimuth = 1e-3 * (i % 1000);
		Sink = Sink + rb::StrikeCueBall(In, Rest, Spec, MotCloth(), MotSlate(), rb::PinchParams{}, kG, kNumerics).State.Velocity.x;
	});
	RB_CHECK(std::isfinite(Sink));
}
