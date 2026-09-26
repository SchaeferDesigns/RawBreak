// physics-motion-and-cue Part C tests T-C1 ... T-C8 and prior-art-and-validation AIR-01 ... AIR-03 (WP-1).

#include "rbtest.h"

#include "Motion/MotionTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

#include <cmath>

using namespace mottest;
using rb::BallState;
using rb::MotionSegment;
using rb::MotionState;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	BallState Flying(const Vec3& Position, const Vec3& Velocity, const Vec3& Omega)
	{
		BallState S;
		S.Position = Position;
		S.Velocity = Velocity;
		S.Omega = Omega;
		S.State = MotionState::Airborne;
		rb::ClassifyState(S, kR, 0.0, kNumerics);
		return S;
	}

	constexpr double kMph = 0.44704;
	constexpr double kFoot = 0.3048;
	constexpr double kRps = 2.0 * rb::kPi;
}

RB_TEST(MOT_TC1_Ballistics)
{
	const BallState S = Flying({0.0, 0.0, kR}, {0.0, 0.0, 1.0}, Vec3::Zero());
	RB_REQUIRE(S.State == MotionState::Airborne);
	const MotionSegment M = rb::MakeSegment(S, 0.0, MotSpec(), MotCloth(), 0.0, kG);
	RB_CHECK_NEAR(M.TauEnd, 0.2039432, 1e-7);
	RB_CHECK_NEAR(M.TauEnd, 2.0 / kG, 1e-9 * M.TauEnd);
	const BallState Apex = rb::EvaluateSegment(M, 1.0 / kG);
	RB_CHECK_NEAR(Apex.Position.z - kR, 0.0509858, 1e-7);
	RB_CHECK_NEAR(Apex.Velocity.z, 0.0, 1e-15);
	const BallState Land = rb::SegmentEndState(M, kNumerics);
	RB_CHECK(Land.State == MotionState::Airborne); // landings are routed by the simulator
	RB_CHECK(Land.Position.z == kR);
	RB_CHECK_NEAR(Land.Velocity.z, -1.0, 1e-15);
}

RB_TEST(MOT_TC2_MultiBounceToRolling)
{
	const BallRun Run = RunBall(Flying({0.0, 0.0, kR}, {1.0, 0.0, 1.0}, Vec3::Zero()), MotSpec(), MotCloth(), MotSlate(), kG);
	const double Times[4] = {0.203943, 0.326309, 0.399729, 0.443780};
	const double Xs[4] = {0.203943, 0.291347, 0.343790, 0.375256};
	const double VzOut[4] = {0.6, 0.36, 0.216, 0.0};
	RB_REQUIRE(Run.Events.size() >= 4u);
	for (int i = 0; i < 4; ++i)
	{
		const RunEvent& E = Run.Events[static_cast<std::size_t>(i)];
		RB_CHECK(E.Landing && E.Bounce == i + 1);
		RB_CHECK_NEAR(E.Time, Times[i], 1e-6);
		RB_CHECK_NEAR(E.State.Position.x, Xs[i], 1e-6);
		RB_CHECK_NEAR(E.Impact.Velocity.z, VzOut[i], 1e-6);
		RB_CHECK_NEAR(E.State.Velocity.x, 0.714286, 1e-6); // stick at the first impact, unchanged afterwards
		RB_CHECK_NEAR(E.State.Omega.y, 24.996875, 1e-6);
	}
	RB_CHECK(Run.Events[0].Impact.Stick);
	RB_CHECK(Run.Events[3].Impact.Settled); // 0.1296 < v_z_min
	RB_CHECK(Run.Events[3].State.State == MotionState::Rolling);
	RB_CHECK(Run.Events[2].State.State == MotionState::Airborne);
	RB_CHECK_NEAR(0.6 * 0.216, 0.1296, 1e-12);
}

RB_TEST(MOT_TC3_AlciatoreBounceModel)
{
	rb::SlateParams Free = MotSlate();
	Free.MinBounceHeight = 0.0; // "ignore v_z_min": the free bounce sequence of TP B.10
	Free.MaxBounces = 1000;
	const rb::BallSpec Spec = MotSpec();

	struct Published
	{
		double Mph, Rps, Deg, HopFt;
	};
	const Published Five[5] = {{10.855, 32.897, 3.091, 0.848}, {10.658, 31.731, 1.889, 0.500}, {10.542, 31.031, 1.145, 0.297},
		{10.473, 30.612, 0.692, 0.177}, {10.432, 30.360, 0.417, 0.106}};
	const Published Twelve[2] = {{10.300, 30.205, 7.792, 1.906}, {9.794, 27.423, 4.908, 1.093}};

	for (int Case = 0; Case < 2; ++Case)
	{
		Vec3 V{4.985123, 0.0, -0.436142}; // 11.194 mph at 5 deg down, 34.841 rps backspin
		if (Case == 1)
		{
			const double Speed = std::hypot(V.x, V.z);
			const double Down = 12.0 * rb::kDegToRad;
			V = {Speed * std::cos(Down), 0.0, -Speed * std::sin(Down)};
		}
		Vec3 W{0.0, -218.9125, 0.0};
		Vec3 P{0.0, 0.0, kR};
		const int Bounces = Case == 0 ? 5 : 2;
		for (int b = 0; b < Bounces; ++b)
		{
			const rb::SlateImpactResult Hit = rb::ResolveSlateImpact(V, W, Spec, 0.6, 0.2, Free, b + 1, kG, kNumerics);
			RB_CHECK(!Hit.Stick && !Hit.Settled);
			RB_CHECK(Hit.Omega.z == W.z);
			if (Case == 0 && b == 0)
			{
				RB_CHECK_NEAR(Hit.Velocity.x, 4.845558, 1e-6);
				RB_CHECK_NEAR(Hit.Velocity.z, 0.261685, 1e-6);
				RB_CHECK_NEAR(Hit.Omega.y, -206.7020, 1e-4);
			}
			BallState S;
			S.Position = P;
			S.Velocity = Hit.Velocity;
			S.Omega = Hit.Omega;
			rb::ClassifyState(S, kR, 0.0, kNumerics);
			RB_REQUIRE(S.State == MotionState::Airborne);
			const MotionSegment M = rb::MakeSegment(S, 0.0, Spec, MotCloth(), 0.0, kG);
			const BallState Land = rb::SegmentEndState(M, kNumerics);
			const double Hop = Land.Position.x - P.x;
			const Published& Ref = Case == 0 ? Five[b] : Twelve[b];
			RB_CHECK(PubNear(rb::Length(Hit.Velocity) / kMph, Ref.Mph, 3));
			RB_CHECK(PubNear(std::fabs(Hit.Omega.y) / kRps, Ref.Rps, 3));
			RB_CHECK(PubNear(std::atan2(Hit.Velocity.z, Hit.Velocity.x) / rb::kDegToRad, Ref.Deg, 3));
			RB_CHECK(PubNear(Hop / kFoot, Ref.HopFt, 3));
			if (Case == 0 && b == 0)
			{
				RB_CHECK_NEAR(Hop, 0.2586, 1e-4);
			}
			V = Land.Velocity;
			W = Land.Omega;
			P = Land.Position;
		}
	}
}

RB_TEST(MOT_TC4_StickSlipContinuity)
{
	const rb::BallSpec Spec = MotSpec();
	const rb::SlateParams Slate = MotSlate();
	const double Mu = 0.2;
	const double E = 0.6;
	const double Wn = 1.3;
	const double Boundary = 3.5 * Mu * (1.0 + E) * Wn; // |u| = (7/2) mu_s (1 + e) w_n
	// v = (0.4, -0.1), u along (3, 4)/5 with |u| = Boundary: w_h from u = v + R z_hat x w.
	const double Ux = 0.6 * Boundary;
	const double Uy = 0.8 * Boundary;
	const Vec3 V{0.4, -0.1, -Wn};
	const Vec3 W{(Uy - V.y) / kR, -(Ux - V.x) / kR, 17.0};
	const rb::SlateImpactResult At = rb::ResolveSlateImpact(V, W, Spec, E, Mu, Slate, 1, kG, kNumerics);
	// Both branch formulas at the boundary.
	const Vec3 U = rb::SlipVelocity(V, W, kR);
	const double ULen = std::hypot(U.x, U.y);
	const Vec3 DvStick{-2.0 / 7.0 * U.x, -2.0 / 7.0 * U.y, 0.0};
	const Vec3 DvSlip{-Mu * (1.0 + E) * Wn * U.x / ULen, -Mu * (1.0 + E) * Wn * U.y / ULen, 0.0};
	for (const Vec3& Dv : {DvStick, DvSlip})
	{
		const Vec3 V1{V.x + Dv.x, V.y + Dv.y, E * Wn};
		const Vec3 W1{W.x + 2.5 / kR * Dv.y, W.y - 2.5 / kR * Dv.x, W.z};
		RB_CHECK_NEAR(At.Velocity.x, V1.x, 1e-12);
		RB_CHECK_NEAR(At.Velocity.y, V1.y, 1e-12);
		RB_CHECK_NEAR(At.Velocity.z, V1.z, 1e-12);
		RB_CHECK_NEAR(At.Omega.x, W1.x, 1e-12);
		RB_CHECK_NEAR(At.Omega.y, W1.y, 1e-12);
		RB_CHECK(At.Omega.z == W1.z);
	}
	// Nudged to either side of the boundary: the two branches of the implementation agree.
	const double Nudge = 1e-14;
	const Vec3 WStick{(Uy * (1.0 - Nudge) - V.y) / kR, -(Ux * (1.0 - Nudge) - V.x) / kR, 17.0};
	const Vec3 WSlip{(Uy * (1.0 + Nudge) - V.y) / kR, -(Ux * (1.0 + Nudge) - V.x) / kR, 17.0};
	const rb::SlateImpactResult Stick = rb::ResolveSlateImpact(V, WStick, Spec, E, Mu, Slate, 1, kG, kNumerics);
	const rb::SlateImpactResult Slip = rb::ResolveSlateImpact(V, WSlip, Spec, E, Mu, Slate, 1, kG, kNumerics);
	RB_CHECK(Stick.Stick && !Slip.Stick);
	RB_CHECK_NEAR(Stick.Velocity.x, Slip.Velocity.x, 1e-12);
	RB_CHECK_NEAR(Stick.Velocity.y, Slip.Velocity.y, 1e-12);
	RB_CHECK_NEAR(Stick.Omega.x, Slip.Omega.x, 1e-12);
	RB_CHECK_NEAR(Stick.Omega.y, Slip.Omega.y, 1e-12);
	const Vec3 UStick = rb::SlipVelocity(Stick.Velocity, Stick.Omega, kR);
	RB_CHECK_NEAR(UStick.x, 0.0, 1e-14);
	RB_CHECK_NEAR(UStick.y, 0.0, 1e-14);
}

RB_TEST(MOT_TC5_ZenoCap)
{
	rb::SlateParams Slate = MotSlate();
	Slate.Restitution = 0.999;
	Slate.MinBounceHeight = 0.0; // v_z_min = 0
	rb::ClothParams Cloth = MotCloth();
	const BallRun Run = RunBall(Flying({0.0, 0.0, kR}, {0.0, 0.0, 1.0}, Vec3::Zero()), MotSpec(), Cloth, Slate, kG);
	int Landings = 0;
	for (const RunEvent& E : Run.Events)
	{
		Landings += E.Landing ? 1 : 0;
	}
	RB_CHECK(Landings == 10);
	RB_REQUIRE(!Run.Events.empty());
	RB_CHECK(Run.Events.back().Impact.Settled);
	RB_CHECK(Run.Events.back().Impact.Velocity.z == 0.0);
	RB_CHECK(Run.Final.State == MotionState::Stationary);
	RB_CHECK(Run.Final.Velocity.z == 0.0);
}

RB_TEST(MOT_TC6_InvariantThroughBounceProperty)
{
	rb::Rng Rng(0xC6u);
	const rb::BallSpec Spec = MotSpec();
	const rb::SlateParams Slate = MotSlate();
	int Stick = 0;
	int Slip = 0;
	for (int Case = 0; Case < 20000; ++Case)
	{
		const Vec3 V{Rng.NextUniform(-6.0, 6.0), Rng.NextUniform(-6.0, 6.0), -Rng.NextUniform(1e-3, 5.0)};
		const Vec3 W{Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0)};
		const rb::SlateImpactResult Hit = rb::ResolveSlateImpact(V, W, Spec, 0.6, 0.2, Slate, 1, kG, kNumerics);
		(Hit.Stick ? Stick : Slip) += 1;
		const Vec3 L0 = rb::CoriolisInvariant(V, W, kR);
		const Vec3 L1 = rb::CoriolisInvariant(Hit.Velocity, Hit.Omega, kR);
		RB_CHECK_NEAR(L1.x, L0.x, 1e-12);
		RB_CHECK_NEAR(L1.y, L0.y, 1e-12);
	}
	RB_CHECK(Stick > 1000 && Slip > 1000);
}

RB_TEST(MOT_TC7_OmegaZPreservedProperty)
{
	rb::Rng Rng(0xC7u);
	const rb::BallSpec Spec = MotSpec();
	const rb::SlateParams Slate = MotSlate();
	int Stick = 0;
	int Slip = 0;
	for (int Case = 0; Case < 20000; ++Case)
	{
		const Vec3 V{Rng.NextUniform(-6.0, 6.0), Rng.NextUniform(-6.0, 6.0), -Rng.NextUniform(1e-3, 5.0)};
		const Vec3 W{Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0)};
		const rb::SlateImpactResult Hit = rb::ResolveSlateImpact(V, W, Spec, 0.6, 0.2, Slate, 1 + Case % 10, kG, kNumerics);
		(Hit.Stick ? Stick : Slip) += 1;
		RB_CHECK(Hit.Omega.z == W.z); // exact
	}
	RB_CHECK(Stick > 1000 && Slip > 1000);
}

RB_TEST(MOT_TC8_AirborneSpinConstant)
{
	rb::Rng Rng(0xC8u);
	for (int Case = 0; Case < 1000; ++Case)
	{
		const BallState S = Flying({0.3, -0.2, kR + Rng.NextUniform(0.0, 0.05)},
			{Rng.NextUniform(-5.0, 5.0), Rng.NextUniform(-5.0, 5.0), Rng.NextUniform(0.01, 3.0)},
			{Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0)});
		RB_REQUIRE(S.State == MotionState::Airborne);
		const MotionSegment M = rb::MakeSegment(S, 1.5, MotSpec(), MotCloth(), 0.0, kG);
		for (int j = 0; j <= 10; ++j)
		{
			const BallState E = rb::EvaluateSegment(M, M.TauEnd * j / 10.0);
			RB_CHECK(E.Omega == S.Omega);
			RB_CHECK(E.Velocity.x == S.Velocity.x && E.Velocity.y == S.Velocity.y);
		}
		const BallState Land = rb::SegmentEndState(M, kNumerics);
		RB_CHECK(Land.Omega == S.Omega);
		RB_CHECK(Land.Velocity.x == S.Velocity.x && Land.Velocity.y == S.Velocity.y);
	}
}

// ---------------------------------------------------------------------------------------------
// prior-art-and-validation 9.10 (g = 9.81, e_t = 0.5)
// ---------------------------------------------------------------------------------------------

namespace
{
	BallRun VerticalDrop(double MinBounceHeight)
	{
		rb::SlateParams Slate;
		Slate.Restitution = 0.5;
		Slate.MinBounceHeight = MinBounceHeight;
		Slate.MaxBounces = 10;
		return RunBall(Flying({0.0, 0.0, kR + 0.05}, Vec3::Zero(), Vec3::Zero()), ValSpec(), rb::kClothDefault, Slate, kValG);
	}

	int LandingCount(const BallRun& Run)
	{
		int Count = 0;
		for (const RunEvent& E : Run.Events)
		{
			Count += E.Landing ? 1 : 0;
		}
		return Count;
	}
}

RB_TEST(VAL_AIR01_VerticalDropPooltoolMinHeight)
{
	const BallRun Run = VerticalDrop(0.005);
	RB_CHECK(LandingCount(Run) == 2);
	RB_REQUIRE(Run.Events.size() >= 2u);
	RB_CHECK_NEAR(Run.Events[0].Time, 0.100964, 1e-6);
	RB_CHECK_NEAR(Run.Events[1].Time, 0.201928, 1e-6);
	RB_CHECK(Run.Events[1].Impact.Settled && Run.Events[1].State.Velocity.z == 0.0);
	RB_CHECK(Run.Final.State == MotionState::Stationary);
}

RB_TEST(VAL_AIR02_VerticalDropHalfMillimetre)
{
	const BallRun Run = VerticalDrop(0.0005); // pinned explicitly (architecture 15 row 1)
	RB_CHECK(LandingCount(Run) == 4);
	RB_REQUIRE(Run.Events.size() >= 4u);
	RB_CHECK_NEAR(Run.Events[0].Time, 0.100964, 1e-6);
	RB_CHECK_NEAR(Run.Events[1].Time, 0.201928, 1e-6);
	RB_CHECK_NEAR(Run.Events[2].Time, 0.252410, 1e-6);
	RB_CHECK_NEAR(Run.Events[3].Time, 0.277651, 1e-6);
	RB_CHECK(!Run.Events[2].Impact.Settled && Run.Events[3].Impact.Settled);
	RB_CHECK(Run.Final.State == MotionState::Stationary);
}

RB_TEST(VAL_AIR03_BounceApexRatio)
{
	const BallRun Run = VerticalDrop(0.0005);
	double Previous = 0.05; // drop height
	int Checked = 0;
	for (const RunEvent& E : Run.Events)
	{
		if (!E.Landing || E.Impact.Settled)
		{
			continue;
		}
		const double Apex = E.Impact.Velocity.z * E.Impact.Velocity.z / (2.0 * kValG);
		RB_CHECK_NEAR(Apex / Previous, 0.25, 1e-9 * 0.25);
		Previous = Apex;
		++Checked;
	}
	RB_CHECK(Checked == 3);
}
