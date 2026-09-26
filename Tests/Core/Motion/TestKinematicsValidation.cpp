// prior-art-and-validation 9.1 KIN-01 ... KIN-08 and 9.2 CLOTH-01 ... CLOTH-03 (WP-1).
// Parameters (VAL 9): R = 0.028575, m = 0.17009713875, g = 9.81, mu_s 0.2, mu_r 0.01, alpha_sp 10.
// Tolerances: Tier A "rel 1e-9" applies to the closed forms (computed in the test); the printed 6-7 digit values are
// checked at +-1 unit in their last digit (the motion spec's verification log, correction 1, for the same situation).

#include "rbtest.h"

#include "Motion/MotionTestUtil.h"

#include "rb/Physics/Motion.h"

#include <cmath>

using namespace mottest;
using rb::BallState;
using rb::MotionSegment;
using rb::MotionState;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;
	const rb::ClothParams kValCloth{0.2, 0.01, 10.0};

	MotionSegment ValSeg(const BallState& S, const rb::ClothParams& Cloth = kValCloth)
	{
		return rb::MakeSegment(S, 0.0, ValSpec(), Cloth, 0.0, kValG);
	}

	double Rel(double X) { return 1e-9 * std::fabs(X); }

	// Straight shot along +x with R w_y = RwY: slide time, slide distance, final rolling speed (VAL 6.1).
	void CheckStraight(double RwY, double Tau, double Distance, double FinalV)
	{
		const double V0 = 2.0;
		const BallState S = OnCloth({V0, 0.0, 0.0}, {0.0, RwY / kR, 0.0});
		RB_REQUIRE(S.State == MotionState::Sliding);
		const MotionSegment M = ValSeg(S);
		const BallState End = rb::SegmentEndState(M, kNumerics);
		const double U0 = V0 - RwY;
		const double TauExact = 2.0 * std::fabs(U0) / (7.0 * 0.2 * kValG);
		const double MuG = 0.2 * kValG;
		const double DExact = V0 * TauExact - 0.5 * MuG * (U0 > 0.0 ? 1.0 : -1.0) * TauExact * TauExact;
		RB_CHECK_NEAR(M.TauEnd, TauExact, Rel(TauExact));
		RB_CHECK_NEAR(End.Position.x, DExact, Rel(DExact));
		RB_CHECK_NEAR(End.Velocity.x, V0 - 2.0 / 7.0 * U0, Rel(FinalV));
		RB_CHECK_NEAR(M.TauEnd, Tau, 1e-6);
		RB_CHECK_NEAR(End.Position.x, Distance, 1e-6);
		RB_CHECK_NEAR(End.Velocity.x, FinalV, 1e-6);
		RB_CHECK(End.State == MotionState::Rolling);
	}
}

RB_TEST(VAL_KIN01_StunThenRoll)
{
	const BallRun Run = RunBall(OnCloth({2.0, 0.0, 0.0}, Vec3::Zero()), ValSpec(), kValCloth, rb::SlateParams{}, kValG);
	RB_REQUIRE(Run.Events.size() == 2u);
	const RunEvent& Roll = Run.Events[0];
	RB_CHECK(Roll.State.State == MotionState::Rolling);
	RB_CHECK_NEAR(Roll.Time, 0.291248, 1e-6);
	RB_CHECK_NEAR(Roll.Time, 4.0 / (7.0 * 0.2 * kValG), Rel(Roll.Time));
	RB_CHECK_NEAR(Roll.State.Position.x, 0.499282, 1e-6);
	RB_CHECK_NEAR(Roll.State.Position.x, 48.0 / (49.0 * 0.2 * kValG), Rel(Roll.State.Position.x));
	RB_CHECK_NEAR(Roll.State.Velocity.x, 1.428571, 1e-6);
	RB_CHECK_NEAR(Roll.State.Velocity.x, 10.0 / 7.0, Rel(1.0));
	const RunEvent& Stop = Run.Events[1];
	RB_CHECK(Stop.State.State == MotionState::Stationary);
	const double VRoll = 10.0 / 7.0;
	RB_CHECK_NEAR(Stop.Time - Roll.Time, 14.5624, 1e-4);
	RB_CHECK_NEAR(Stop.Time - Roll.Time, VRoll / (0.01 * kValG), Rel(14.5624));
	RB_CHECK_NEAR(Stop.State.Position.x - Roll.State.Position.x, 10.4017, 1e-4);
	RB_CHECK_NEAR(Stop.State.Position.x - Roll.State.Position.x, VRoll * VRoll / (2.0 * 0.01 * kValG), Rel(10.4017));
}

RB_TEST(VAL_KIN02_Draw)
{
	RB_CHECK_NEAR(-2.0 / kR, -69.991, 1e-3);
	CheckStraight(-2.0, 0.582496, 0.832137, 0.857143);
}

RB_TEST(VAL_KIN03_HalfRollFollow)
{
	CheckStraight(1.0, 0.145624, 0.270445, 1.714286);
}

RB_TEST(VAL_KIN04_Overspin)
{
	CheckStraight(4.0, 0.291248, 0.665710, 2.571429);
}

RB_TEST(VAL_KIN05_CoriolisInvariantAnyFriction)
{
	const Vec3 V0{1.0, 0.5, 0.0};
	const Vec3 W0{10.0, -20.0, 5.0};
	const Vec3 U0 = rb::SlipVelocity(V0, W0, kR);
	RB_CHECK_NEAR(std::hypot(U0.x, U0.y), 1.756990, 1e-6);
	for (double MuS : {0.15, 0.2, 0.4})
	{
		const rb::ClothParams Cloth{MuS, 0.01, 10.0};
		const MotionSegment M = ValSeg(OnCloth(V0, W0), Cloth);
		const double TauExact = 2.0 * std::hypot(U0.x, U0.y) / (7.0 * MuS * kValG);
		RB_CHECK_NEAR(M.TauEnd, TauExact, Rel(TauExact));
		if (MuS == 0.2)
		{
			RB_CHECK_NEAR(M.TauEnd, 0.255860, 1e-6);
		}
		const BallState End = rb::SegmentEndState(M, kNumerics);
		RB_CHECK(End.State == MotionState::Rolling);
		RB_CHECK_NEAR(End.Velocity.x, 0.551000, 1e-6);
		RB_CHECK_NEAR(End.Velocity.y, 0.275500, 1e-6);
		RB_CHECK_NEAR(End.Velocity.x, V0.x - 2.0 / 7.0 * U0.x, Rel(0.551));
		RB_CHECK_NEAR(End.Velocity.y, V0.y - 2.0 / 7.0 * U0.y, Rel(0.2755));
		RB_CHECK(End.Velocity.z == 0.0);
	}
}

RB_TEST(VAL_KIN06_SlidingParabola)
{
	const Vec3 V0{1.0, 0.5, 0.0};
	const Vec3 W0{10.0, -20.0, 5.0};
	const BallState S = OnCloth(V0, W0);
	const MotionSegment M = ValSeg(S);
	const Vec3 U0 = rb::SlipVelocity(V0, W0, kR);
	const double ULen = std::hypot(U0.x, U0.y);
	for (int j = 0; j < 100; ++j)
	{
		const double T = M.TauEnd * j / 99.0;
		const BallState E = rb::EvaluateSegment(M, T);
		const double X = S.Position.x + V0.x * T - 0.5 * 0.2 * kValG * (U0.x / ULen) * T * T;
		const double Y = S.Position.y + V0.y * T - 0.5 * 0.2 * kValG * (U0.y / ULen) * T * T;
		RB_CHECK_NEAR(E.Position.x, X, 1e-12);
		RB_CHECK_NEAR(E.Position.y, Y, 1e-12);
		RB_CHECK(E.Position.z == kR);
	}
}

RB_TEST(VAL_KIN07_SpinningInPlace)
{
	const MotionSegment M = ValSeg(OnCloth(Vec3::Zero(), {0.0, 0.0, 20.0}));
	RB_REQUIRE(M.State == MotionState::Spinning);
	RB_CHECK_NEAR(M.TauEnd, 2.0, Rel(2.0));
	// Rotation angle: Simpson's rule is exact for the linear w_z(t).
	const int Panels = 64;
	double Angle = 0.0;
	for (int i = 0; i <= Panels; ++i)
	{
		const double Weight = (i == 0 || i == Panels) ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
		Angle += Weight * rb::OmegaZAt(M, M.TauEnd * i / Panels);
	}
	Angle *= M.TauEnd / (3.0 * Panels);
	RB_CHECK_NEAR(Angle, 20.0, Rel(20.0));
	RB_CHECK(rb::SegmentEndState(M, kNumerics).State == MotionState::Stationary);
}

RB_TEST(VAL_KIN08_SideSpinDecayWhileRolling)
{
	const BallState S = OnCloth({1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 10.0});
	RB_REQUIRE(S.State == MotionState::Rolling);
	const MotionSegment M = ValSeg(S);
	RB_CHECK_NEAR(rb::EvaluateSegment(M, 0.5).Omega.z, 5.0, Rel(5.0));
	RB_CHECK(rb::EvaluateSegment(M, 1.0).Omega.z == 0.0);
	RB_CHECK(rb::EvaluateSegment(M, 3.0).Omega.z == 0.0);
	// Rolling unaffected by the spin: same deceleration as without w_z.
	const MotionSegment Plain = ValSeg(OnCloth({1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 0.0}));
	RB_CHECK(M.Accel2 == Plain.Accel2 && M.TauEnd == Plain.TauEnd);
	RB_CHECK_NEAR(M.TauEnd, 10.19368, 1e-5);
	RB_CHECK_NEAR(M.TauEnd, 1.0 / (0.01 * kValG), Rel(M.TauEnd));
	RB_CHECK(rb::SegmentEndState(M, kNumerics).State == MotionState::Stationary);
}

RB_TEST(VAL_CLOTH01_SnookerRollingDeceleration)
{
	const rb::ClothParams Snooker{0.2, 0.0127, 10.0};
	const MotionSegment M = ValSeg(OnCloth({1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 0.0}), Snooker);
	const double Decel = -2.0 * M.Accel2.x;
	RB_CHECK(Decel >= 0.124 && Decel <= 0.126);
	RB_CHECK_NEAR(Decel, 0.0127 * kValG, 1e-15);
}

RB_TEST(VAL_CLOTH02_DefaultPoolPresetRanges)
{
	for (const rb::ClothParams& C : {rb::kClothDefault, rb::kClothWorstedFast, rb::kClothNappedBar})
	{
		RB_CHECK(C.SlidingFriction >= 0.15 && C.SlidingFriction <= 0.40);
		RB_CHECK(C.RollingResistance >= 0.005 && C.RollingResistance <= 0.015);
		RB_CHECK(C.SpinDeceleration >= 5.0 && C.SpinDeceleration <= 15.0);
		// Sliding vs rolling deceleration of actual segments.
		const MotionSegment Slide = ValSeg(OnCloth({1.0, 0.0, 0.0}, Vec3::Zero()), C);
		const MotionSegment Roll = ValSeg(OnCloth({1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 0.0}), C);
		const double Ratio = Slide.Accel2.x / Roll.Accel2.x;
		RB_CHECK(Ratio >= 10.0 && Ratio <= 40.0);
	}
	RB_CHECK(rb::ClothParamsFor(rb::ClothPreset::Default).RollingResistance == rb::kClothDefault.RollingResistance);
}

RB_TEST(VAL_CLOTH03_LagTime)
{
	const double L = 2.54;
	for (const rb::ClothParams& C : {kValCloth, rb::kClothWorstedFast})
	{
		const double MuR = C.RollingResistance;
		const double V = std::sqrt(2.0 * MuR * kValG * L);
		const BallState S = OnCloth({-V, 0.0, 0.0}, {0.0, -V / kR, 0.0}); // foot rail toward the head rail
		RB_REQUIRE(S.State == MotionState::Rolling);
		const MotionSegment M = ValSeg(S, C);
		const BallState End = rb::SegmentEndState(M, kNumerics);
		RB_CHECK_NEAR(M.TauEnd, std::sqrt(2.0 * L / (MuR * kValG)), 1e-6);
		RB_CHECK_NEAR(-End.Position.x, L, 1e-9);
		if (MuR == 0.01)
		{
			RB_CHECK_NEAR(M.TauEnd, 7.196, 1e-3);
		}
		else
		{
			RB_CHECK(M.TauEnd >= 7.0 && M.TauEnd <= 9.0); // tournament cloth preset (Tier C)
		}
	}
}
