// physics-motion-and-cue Part B tests T-B1 ... T-B20 (WP-1).
// Common constants: g 9.80665, R 0.028575, m 0.170, mu_s 0.2, e_slate 0.6, h_min 2 mm, lambda 0, squirt off,
// M19 = 19 oz, phi = 0, r0 = (0, 0, R). "exact" = +-1 unit in the last listed digit.

#include "rbtest.h"

#include "CueStrike/CueTestUtil.h"

#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"

#include <cmath>

using namespace mottest;
using namespace cuetest;
using rb::BallState;
using rb::MotionState;
using rb::StrikeResult;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	// Velocity at the start of Rolling after the strike (flight, slate bounces and sliding in between).
	Vec3 RollingStartVelocity(const StrikeResult& R)
	{
		const BallRun Run = RunBall(R.State, MotSpec(), MotCloth(), MotSlate(), kG);
		const RunEvent* Roll = FirstEventInto(Run, MotionState::Rolling);
		return Roll != nullptr ? Roll->State.Velocity : Vec3{1e9, 1e9, 1e9};
	}
}

RB_TEST(MOT_TB1_LeckieGreenspanParity)
{
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.0, 0.0, kM19, 1.0));
	RB_REQUIRE(R.Error == rb::ErrorCode::Ok);
	RB_CHECK_NEAR(R.State.Velocity.x, 3.0404167, 1e-7);
	RB_CHECK_NEAR(R.State.Velocity.x, 2.0 * 2.0 / (1.0 + kM / kM19), 1e-9 * 3.04);
	RB_CHECK(R.State.Velocity.y == 0.0 && R.State.Velocity.z == 0.0);
	RB_CHECK(R.State.Omega == Vec3::Zero());
	RB_CHECK(!R.Miscue && R.State.State == MotionState::Sliding);
}

RB_TEST(MOT_TB2_AlciatoreTPA30Limits)
{
	const StrikeResult R = Strike(Input(1.0, 0.0, 0.0, 0.0, 3.0 * kM, 1.0));
	RB_CHECK_NEAR(R.State.Velocity.x, 1.5, 1e-9 * 1.5);
	const StrikeResult Heavy = Strike(Input(1.0, 0.0, 0.0, 0.0, 1e15, 1.0));
	RB_CHECK_NEAR(Heavy.State.Velocity.x, 2.0, 1e-9 * 2.0);
}

RB_TEST(MOT_TB3_CenterLeatherTip)
{
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.0, 0.0, kM19, 0.75));
	RB_CHECK_NEAR(R.State.Velocity.x, 2.6603646, 1e-7);
	RB_CHECK(R.State.Omega == Vec3::Zero());
}

RB_TEST(MOT_TB4_NaturalRoll)
{
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.0, 0.4, kM19, 0.75));
	RB_CHECK_NEAR(R.State.Velocity.x, 2.0400917, 1e-7);
	RB_CHECK_NEAR(R.State.Omega.x, 0.0, 1e-12);
	RB_CHECK_NEAR(R.State.Omega.y, 71.394286, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.z, 0.0, 1e-12);
	const Vec3 U = rb::SlipVelocity(R.State.Velocity, R.State.Omega, kR);
	RB_CHECK_NEAR(rb::Length(U), 0.0, 1e-12);
	RB_CHECK(R.State.State == MotionState::Rolling);
}

RB_TEST(MOT_TB5_MaxDraw)
{
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.0, -0.5, kM19, 0.75));
	RB_CHECK(!R.Miscue);
	RB_CHECK_NEAR(R.State.Velocity.x, 1.8035574, 1e-7);
	RB_CHECK_NEAR(R.State.Omega.y, -78.895775, 1e-6);
	const double Srf = std::fabs(R.State.Omega.y) * kR / rb::Length(R.State.Velocity);
	RB_CHECK_NEAR(Srf, 1.25, 1e-9 * 1.25); // SRF = (5/2) rho
}

RB_TEST(MOT_TB6_RightEnglish)
{
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.3, 0.0, kM19, 0.75));
	RB_CHECK_NEAR(R.State.Velocity.x, 2.2718287, 1e-7);
	RB_CHECK_NEAR(R.State.Velocity.y, 0.0, 1e-12);
	RB_CHECK_NEAR(R.State.Omega.x, 0.0, 1e-12);
	RB_CHECK_NEAR(R.State.Omega.y, 0.0, 1e-12);
	RB_CHECK_NEAR(R.State.Omega.z, 59.628049, 1e-6); // right English -> w_z > 0
}

RB_TEST(MOT_TB7_Azimuth)
{
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.3, 0.2, kM19, 0.75, 0.5 * rb::kPi));
	RB_CHECK_NEAR(R.State.Velocity.x, 0.0, 1e-7);
	RB_CHECK_NEAR(R.State.Velocity.y, 2.1333540, 1e-7);
	RB_CHECK_NEAR(R.State.Omega.x, -37.329028, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.y, 0.0, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.z, 55.993542, 1e-6);
}

RB_TEST(MOT_TB8_SquirtAngle)
{
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.5, 15.0)), 3.4657, 1e-4);
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.5, 40.0)), 1.4463, 1e-4);
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.5, 15.0)), 3.466, 1e-3); // TP A.31 published
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.5, 40.0)), 1.446, 1e-3);
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.5, 20.0)), 2.7094, 1e-4);
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.25, 20.0)), 1.4850, 1e-4);
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.5, 30.0)), 1.886, 1e-3); // B.7 table (pooltool default)
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.25, 15.0)), 1.889, 1e-3);
}

RB_TEST(MOT_TB9_SquirtInStrike)
{
	rb::CueStrikeInput In = Input(2.0, 0.0, 0.5, 0.0, kM19, 0.75);
	In.SquirtEnabled = true;
	const StrikeResult R = Strike(In);
	RB_CHECK(!R.Miscue);
	RB_CHECK_NEAR(R.State.Velocity.x, 1.8015412, 1e-7);
	RB_CHECK_NEAR(R.State.Velocity.y, 0.0852558, 1e-7); // deflected LEFT (+y) by right English
	RB_CHECK_NEAR(R.State.Velocity.z, 0.0, 1e-12);
	RB_CHECK_NEAR(Degrees(std::atan2(R.State.Velocity.y, R.State.Velocity.x)), 2.7094, 1e-4);
	RB_CHECK_NEAR(Degrees(R.SquirtAngle), 2.7094, 1e-4);
	RB_CHECK_NEAR(rb::Length(R.State.Velocity), 1.8035574, 1e-7);
	// Spin is not modified by squirt.
	const StrikeResult NoSquirt = Strike(Input(2.0, 0.0, 0.5, 0.0, kM19, 0.75));
	RB_CHECK_NEAR(R.State.Omega.z, NoSquirt.State.Omega.z, 1e-12);
	RB_CHECK_NEAR(rb::Length(NoSquirt.State.Velocity), rb::Length(R.State.Velocity), 1e-12);
}

RB_TEST(MOT_TB10_MiscueBoundary)
{
	const double RhoMax = rb::MiscueLimit(0.6);
	RB_CHECK_NEAR(RhoMax, 0.5144958, 1e-7);
	const StrikeResult Grip = Strike(Input(2.0, 0.0, 0.514, 0.0, kM19, 0.75));
	RB_CHECK(!Grip.Miscue);
	RB_CHECK_NEAR(rb::Length(Grip.State.Velocity), 1.7712, 1e-4);
	const StrikeResult Slip = Strike(Input(2.0, 0.0, 0.515, 0.0, kM19, 0.75));
	RB_CHECK(Slip.Miscue);
	RB_CHECK_NEAR(rb::Length(Slip.State.Velocity), 1.7700, 1e-4);
	// Continuity at rho_max.
	const StrikeResult Below = Strike(Input(2.0, 0.0, RhoMax * (1.0 - 1e-9), 0.0, kM19, 0.75));
	const StrikeResult Above = Strike(Input(2.0, 0.0, RhoMax * (1.0 + 1e-9), 0.0, kM19, 0.75));
	RB_CHECK(!Below.Miscue && Above.Miscue);
	for (const StrikeResult* R : {&Below, &Above})
	{
		RB_CHECK_NEAR(rb::Length(R->State.Velocity), 1.7700245, 1e-4);
		RB_CHECK_NEAR(R->State.Omega.z, 79.6737, 1e-4);
		RB_CHECK_NEAR(R->CueSpeedAfter, 1.4414, 1e-4);
	}
	RB_CHECK_NEAR(rb::Length(Below.State.Velocity), rb::Length(Above.State.Velocity), 1e-7);
	RB_CHECK_NEAR(Below.State.Velocity.y, Above.State.Velocity.y, 1e-7);
	RB_CHECK_NEAR(Below.State.Omega.z, Above.State.Omega.z, 1e-6);
	RB_CHECK_NEAR(Below.CueSpeedAfter, Above.CueSpeedAfter, 1e-7);
	// Deep miscue a = 0.6: kicked left along the friction-cone edge.
	const StrikeResult Deep = Strike(Input(2.0, 0.0, 0.6, 0.0, kM19, 0.75));
	RB_CHECK(Deep.Miscue);
	RB_CHECK_NEAR(Deep.State.Velocity.x, 1.7542, 1e-4);
	RB_CHECK_NEAR(Deep.State.Velocity.y, 0.1815, 1e-4);
	RB_CHECK_NEAR(Deep.State.Velocity.z, 0.0, 1e-12);
	RB_CHECK_NEAR(Degrees(std::atan2(Deep.State.Velocity.y, Deep.State.Velocity.x)), 5.906, 1e-3);
	RB_CHECK_NEAR(Deep.State.Omega.z, 79.385, 1e-4); // the table tolerance (model 79.384906)
	RB_CHECK(Deep.SquirtAngle == 0.0);
}

RB_TEST(MOT_TB11_TipConversion)
{
	const rb::Vec2 Contact = rb::AimToContactOffset({2.0 / 3.0, 0.0}, kR, kR / 3.0);
	RB_CHECK_NEAR(Contact.x, 0.5, 1e-15);
	RB_CHECK(Contact.y == 0.0);
	const rb::Vec2 Both = rb::AimToContactOffset({-0.4, 0.2}, kR, kR / 3.0);
	RB_CHECK_NEAR(Both.x, -0.3, 1e-15);
	RB_CHECK_NEAR(Both.y, 0.15, 1e-15);
}

RB_TEST(MOT_TB12_SeparationMargin)
{
	RB_CHECK_NEAR(std::sqrt(rb::SeparationMargin(0.0, 0.73, kM, kM19)), 0.6198, 1e-4);
	RB_CHECK_NEAR(std::sqrt(rb::SeparationMargin(0.0, 1.0, kM, 3.0 * kM)), 0.7303, 1e-4);
	RB_CHECK_NEAR(std::sqrt(rb::SeparationMargin(0.0, 1.0, kM, 3.0 * kM)), 0.73, 1e-2); // TP A.30
	// Reported by the strike: margin = k e (1 + m/M) - rho^2; the ball outruns the cue iff it is positive.
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.3, 0.4, kM19, 0.73));
	RB_CHECK_NEAR(R.SeparationMargin, 0.4 * 0.73 * (1.0 + kM / kM19) - 0.25, 1e-12);
	RB_CHECK(R.State.Velocity.x > R.CueSpeedAfter);
}

RB_TEST(MOT_TB13_BreakHop)
{
	const StrikeResult R = Strike(Input(8.5, 5.0, 0.0, 0.1, kM21, 0.85));
	RB_REQUIRE(R.Error == rb::ErrorCode::Ok);
	RB_CHECK(R.Lambda == 0.0);
	RB_CHECK_NEAR(R.VelocityAfterTip.x, 11.953110, 1e-6);
	RB_CHECK_NEAR(R.VelocityAfterTip.y, 0.0, 1e-12);
	RB_CHECK_NEAR(R.VelocityAfterTip.z, -1.045762, 1e-6);
	RB_CHECK_NEAR(R.OmegaAfterTip.y, 104.976106, 1e-6);
	RB_CHECK(R.SlateContact && !R.SlateStick);
	RB_CHECK_NEAR(R.State.Velocity.x, 11.618466, 1e-6);
	RB_CHECK_NEAR(R.State.Velocity.z, 0.627457, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.y, 134.253771, 1e-6);
	RB_CHECK(R.State.State == MotionState::Airborne);
	const rb::MotionSegment Flight = rb::MakeSegment(R.State, 0.0, MotSpec(), MotCloth(), 0.0, kG);
	RB_CHECK_NEAR(Flight.TauEnd, 0.1279656, 1e-7);
	RB_CHECK_NEAR(rb::SegmentEndState(Flight, kNumerics).Position.x, 1.486764, 1e-6);
	RB_CHECK_NEAR(R.State.Velocity.z * R.State.Velocity.z / (2.0 * kG), 0.0200732, 1e-7);
}

RB_TEST(MOT_TB14_JumpShot)
{
	rb::CueStrikeInput In = Input(4.0, 50.0, 0.0, 0.0, kM9, 0.85);
	In.LambdaOverride = -1.0; // the jump-cue schedule (M <= 0.35 kg) gives lambda(50 deg) = 0
	const StrikeResult R = Strike(In);
	RB_CHECK(R.Lambda == 0.0);
	RB_CHECK_NEAR(R.VelocityAfterTip.x, 2.854629, 1e-6);
	RB_CHECK_NEAR(R.VelocityAfterTip.z, -3.402014, 1e-6);
	RB_CHECK(R.SlateContact && R.SlateStick);
	RB_CHECK_NEAR(R.State.Velocity.x, 2.039021, 1e-6);
	RB_CHECK_NEAR(R.State.Velocity.z, 2.041209, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.y, 71.356807, 1e-6);
	const Vec3 U = rb::SlipVelocity(R.State.Velocity, R.State.Omega, kR);
	RB_CHECK_NEAR(std::hypot(U.x, U.y), 0.0, 1e-12);
	RB_CHECK(R.State.State == MotionState::Airborne);
	RB_CHECK_NEAR(R.State.Velocity.z * R.State.Velocity.z / (2.0 * kG), 0.212434, 1e-6);
	const rb::MotionSegment Flight = rb::MakeSegment(R.State, 0.0, MotSpec(), MotCloth(), 0.0, kG);
	RB_CHECK_NEAR(Flight.TauEnd, 0.4162907, 1e-7);
	RB_CHECK_NEAR(rb::SegmentEndState(Flight, kNumerics).Position.x, 0.8488254, 1e-7);
}

RB_TEST(MOT_TB15_MasseDirectionCoriolis)
{
	const double Theta = 75.0 * rb::kDegToRad;
	const double Expected = std::atan2(-0.4 * std::sin(Theta), std::cos(Theta) - 0.3);
	RB_CHECK_NEAR(Degrees(Expected), -96.0839, 1e-4);
	struct Case
	{
		double Lambda, J, Lx, Ly;
	};
	for (const Case& C : {Case{0.0, 0.378876, -0.065557, -0.615069}, Case{1.0, 0.729707, -0.126261, -1.184610}})
	{
		rb::CueStrikeInput In = Input(2.5, 75.0, 0.4, -0.3, kM19, 0.73);
		In.LambdaOverride = C.Lambda;
		const StrikeResult R = Strike(In);
		RB_REQUIRE(R.Error == rb::ErrorCode::Ok && !R.Miscue);
		RB_CHECK_NEAR(R.Impulse, C.J, 1e-6);
		const Vec3 L = rb::CoriolisInvariant(R.State.Velocity, R.State.Omega, kR);
		RB_CHECK_NEAR(L.x, C.Lx, 1e-6);
		RB_CHECK_NEAR(L.y, C.Ly, 1e-6);
		const Vec3 V = RollingStartVelocity(R);
		RB_CHECK_NEAR(V.x, C.Lx, 1e-6);
		RB_CHECK_NEAR(V.y, C.Ly, 1e-6);
		RB_CHECK_NEAR(std::atan2(V.y, V.x), Expected, 1e-6);
	}
}

RB_TEST(MOT_TB16_LevelSideSpinNoSwerve)
{
	rb::CueStrikeInput In = Input(3.0, 0.0, 0.4, 0.0, kM19, 0.73);
	In.SquirtEnabled = true;
	const StrikeResult R = Strike(In);
	RB_CHECK_NEAR(R.State.Velocity.x, 3.0227863, 1e-7);
	RB_CHECK_NEAR(R.State.Velocity.y, 0.1199320, 1e-7);
	RB_CHECK_NEAR(R.State.Omega.x, 0.0, 1e-12);
	RB_CHECK_NEAR(R.State.Omega.y, 0.0, 1e-12);
	RB_CHECK_NEAR(R.State.Omega.z, 105.867527, 1e-6);
	const Vec3 U = rb::SlipVelocity(R.State.Velocity, R.State.Omega, kR);
	RB_CHECK_NEAR(U.x * R.State.Velocity.y - U.y * R.State.Velocity.x, 0.0, 1e-12); // u parallel to v
	const double Direction = Degrees(std::atan2(R.State.Velocity.y, R.State.Velocity.x));
	RB_CHECK_NEAR(Direction, 2.2721, 1e-4);
	const Vec3 V = RollingStartVelocity(R);
	RB_CHECK_NEAR(Degrees(std::atan2(V.y, V.x)), Direction, 1e-9); // straight until rolling
}

RB_TEST(MOT_TB17_ElevatedSwerve)
{
	rb::CueStrikeInput In = Input(2.0, 10.0, 0.4, 0.0, kM19, 0.73);
	In.SquirtEnabled = true;
	const StrikeResult R = Strike(In);
	RB_CHECK_NEAR(R.State.Velocity.x, 1.875138, 1e-6);
	RB_CHECK_NEAR(R.State.Velocity.y, 0.056234, 1e-6);
	RB_CHECK_NEAR(R.State.Velocity.z, 0.209961, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.x, 10.180472, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.y, 9.574597, 1e-6);
	RB_CHECK_NEAR(R.State.Omega.z, 69.506108, 1e-6);
	RB_CHECK(R.State.State == MotionState::Airborne);
	const Vec3 V = RollingStartVelocity(R);
	RB_CHECK_NEAR(V.x, 1.417554, 1e-6);
	RB_CHECK_NEAR(V.y, -0.042949, 1e-6);
	RB_CHECK_NEAR(Degrees(std::atan2(V.y, V.x)), -1.7354, 1e-4); // squirts left, curves to the right
}

RB_TEST(MOT_TB18_InvalidInput)
{
	BallState Ball = Resting();
	Ball.Position = {0.3, -0.2, kR};
	struct Bad
	{
		double V, ThetaDeg, A, B;
		rb::ErrorCode Code;
	};
	const Bad Cases[] = {
		{2.0, 0.0, 1.0, 0.0, rb::ErrorCode::CueOffsetTooLarge},
		{2.0, 0.0, 0.0, -1.0, rb::ErrorCode::CueOffsetTooLarge},
		{2.0, 0.0, 0.96, 0.0, rb::ErrorCode::CueOffsetTooLarge},
		{2.0, 0.0, 0.6, 0.74, rb::ErrorCode::CueOffsetTooLarge}, // rho = 0.9527
		{2.0, 90.0, 0.0, 0.0, rb::ErrorCode::CueElevationOutOfRange},
		{2.0, -1.0, 0.0, 0.0, rb::ErrorCode::CueElevationOutOfRange},
		{-0.1, 0.0, 0.0, 0.0, rb::ErrorCode::CueSpeedOutOfRange},
		{15.5, 0.0, 0.0, 0.0, rb::ErrorCode::CueSpeedOutOfRange},
	};
	for (const Bad& C : Cases)
	{
		rb::CueStrikeInput In = Input(C.V, C.ThetaDeg, C.A, C.B, kM19, 0.73);
		if (C.ThetaDeg == 90.0)
		{
			In.Elevation = 0.5 * rb::kPi;
		}
		RB_CHECK(rb::ValidateCueStrike(In) == C.Code);
		const StrikeResult R = rb::StrikeCueBall(In, Ball, MotSpec(), MotCloth(), MotSlate(), rb::PinchParams{}, kG, kNumerics);
		RB_CHECK(R.Error == C.Code);
		RB_CHECK(SameBits(R.State.Position, Ball.Position) && SameBits(R.State.Velocity, Ball.Velocity) && SameBits(R.State.Omega, Ball.Omega));
		RB_CHECK(R.State.State == Ball.State);
	}
	// rho = 0.937 is a legal (miscue) input; NaN and a moving ball are rejected.
	RB_CHECK(rb::ValidateCueStrike(Input(2.0, 0.0, 0.6, 0.72, kM19, 0.73)) == rb::ErrorCode::Ok);
	RB_CHECK(rb::ValidateCueStrike(Input(std::nan(""), 0.0, 0.0, 0.0, kM19, 0.73)) == rb::ErrorCode::CueSpeedOutOfRange);
	BallState Moving = Ball;
	Moving.Velocity = {0.1, 0.0, 0.0};
	Moving.State = MotionState::Rolling;
	const StrikeResult R = rb::StrikeCueBall(Input(2.0, 0.0, 0.0, 0.0, kM19, 0.73), Moving, MotSpec(), MotCloth(), MotSlate(), rb::PinchParams{}, kG, kNumerics);
	RB_CHECK(R.Error == rb::ErrorCode::BallNotAtRest && SameBits(R.State.Velocity, Moving.Velocity));
}

RB_TEST(MOT_TB19_CoriolisVsTPA19Published)
{
	const double A = 0.5 * std::cos(45.0 * rb::kDegToRad); // 0.353553
	RB_CHECK_NEAR(A, 0.353553, 1e-6);
	struct Case
	{
		double B, ThetaDeg, Published, Listed;
	};
	for (const Case& C : {Case{-A, 4.0, 2.193, 2.1931}, Case{A, 3.0, 0.784, 0.7840}})
	{
		const double Theta = C.ThetaDeg * rb::kDegToRad;
		const double Delta = std::atan2(-A * std::sin(Theta), std::cos(Theta) + C.B);
		RB_CHECK(Delta < 0.0); // to the right
		RB_CHECK_NEAR(Degrees(-Delta), C.Listed, 1e-4);
		RB_CHECK_NEAR(Degrees(-Delta), C.Published, 1e-3);
		// The strike's Coriolis invariant points exactly there (squirt off), whatever the slate does.
		const StrikeResult R = Strike(Input(2.0, C.ThetaDeg, A, C.B, kM19, 0.73));
		RB_REQUIRE(R.Error == rb::ErrorCode::Ok && !R.Miscue);
		const Vec3 L = rb::CoriolisInvariant(R.State.Velocity, R.State.Omega, kR);
		RB_CHECK_NEAR(std::atan2(L.y, L.x), Delta, 1e-12);
	}
}

RB_TEST(MOT_TB20_MiscueUpwardGuard)
{
	const StrikeResult R = Strike(Input(2.0, 0.0, 0.0, -0.6, kM19, 0.75));
	RB_REQUIRE(R.Error == rb::ErrorCode::Ok);
	RB_CHECK(R.Miscue);
	RB_CHECK_NEAR(R.ImpulseDirection.x, 0.9947, 1e-3);
	RB_CHECK_NEAR(R.ImpulseDirection.y, 0.0, 1e-12);
	RB_CHECK_NEAR(R.ImpulseDirection.z, 0.1029, 1e-3); // w_n < 0: kicked upward
	RB_CHECK_NEAR(R.VelocityAfterTip.x, 1.7542, 1e-3);
	RB_CHECK_NEAR(R.VelocityAfterTip.z, 0.1815, 1e-3);
	RB_CHECK_NEAR(R.OmegaAfterTip.y, -79.385, 1e-3);
	RB_CHECK(!R.SlateContact);
	RB_CHECK(R.State.Velocity.z == 0.0); // 0.1815 < v_z_min: no micro-hop
	RB_CHECK(R.State.State == MotionState::Sliding);
}
