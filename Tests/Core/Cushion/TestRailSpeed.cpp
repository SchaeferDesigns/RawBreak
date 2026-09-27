// Owner: WP-4. WPA rail-speed calibration (collisions 4.8 / CAL-1, equipment T-CAL-1, prior-art CUSH-05): a stun shot
// from the head spot through the foot spot on the 9-ft table, 1D along the long axis, perpendicular cushion hits through
// the real dispatcher (ResolveFixedContact: Mathavan, default e_c law, mu_w, the cloth's mu_s during the impact).
//
// The motion between the rails is the spec's 1D reference (collisions 4.8, Tools/reference/cal.py) written for a
// general inertia factor k: sliding a = -mu_s g sgn(u), dw_y = mu_s g sgn(u) / (k R) until u = v - R w_y = 0, then
// rolling a = -mu_r g sgn(v). The Integ_ variant replaces it by WP-1's MakeSegment / SegmentEndState.

#include "Cushion/CushionTestUtil.h"

#include "rb/Core/Tolerances.h"
#include "rb/Physics/Motion.h"

#include <cstdio>

using namespace rbcushiontest;

namespace
{
	constexpr double kTableLength = 2.54; // 9-ft playing length L [m]

	struct RailRun
	{
		double Lengths = 0.0;  // path length / L
		int Hits = 0;          // cushion impacts
		double MaxVz = 0.0;    // largest |v_z| after a cushion impact (the ball must stay on the slate)
	};

	// Perpendicular hit on the end rail ahead (Sign = direction of travel along x) through the dispatcher.
	void CushionHit(double Sign, double& V, double& Wy, const rb::BallSpec& Ball, const rb::ClothParams& Cloth, RailRun& Run)
	{
		rb::FixedContact Contact;
		Contact.Kind = rb::FixedContactKind::NoseEdge;
		Contact.BallOnCloth = true;
		Contact.IntoFeature = {Sign, 0.0, 0.0};
		Contact.Elevation = EdgeElevation(kH, Ball.Radius);
		const rb::CushionFrame Frame = rb::MakeCushionFrame(Contact.IntoFeature);
		Contact.Normal = Frame.Y * (-rb::Cos(Contact.Elevation)) + Frame.Z * (-rb::Sin(Contact.Elevation));
		rb::BallState S;
		S.Velocity = {V, 0.0, 0.0};
		S.Omega = {0.0, Wy, 0.0};
		const rb::CushionImpactResult R = rb::ResolveFixedContact(Contact, S, Ball, rb::CushionParams{}, rb::PocketContactParams{}, Cloth, rb::NumericsConfig{});
		V = R.Velocity.x;
		Wy = R.Omega.y;
		Run.MaxVz = rb::Max(Run.MaxVz, rb::Abs(R.Velocity.z));
		++Run.Hits;
	}

	// The spec's 1D reference: start at the head spot (x = -L/4) with a stun ball moving +x at V0.
	RailRun RunReference(double V0, const rb::ClothParams& Cloth, double Gravity)
	{
		const rb::BallSpec Ball = PoolBall();
		const double K = rb::InertiaFactor(Ball);
		const double Rc = kR * rb::Cos(EdgeElevation(kH, kR));
		const double Wall = 0.5 * kTableLength - Rc;
		RailRun Run;
		double X = -0.25 * kTableLength;
		double V = V0;
		double Wy = 0.0;
		double Distance = 0.0;
		for (int Phase = 0; Phase < 200; ++Phase)
		{
			const double U = V - kR * Wy;
			double A = 0.0;
			double WDot = 0.0;
			double Duration = 0.0;
			if (rb::Abs(U) > 1e-12)
			{
				A = -Cloth.SlidingFriction * Gravity * rb::SignNonZero(U);
				WDot = Cloth.SlidingFriction * Gravity * rb::SignNonZero(U) / (K * kR);
				Duration = K * rb::Abs(U) / ((1.0 + K) * Cloth.SlidingFriction * Gravity);
			}
			else
			{
				if (!(rb::Abs(V) > 1e-12))
				{
					break;
				}
				A = -Cloth.RollingResistance * Gravity * rb::SignNonZero(V);
				WDot = A / kR;
				Duration = rb::Abs(V) / (Cloth.RollingResistance * Gravity);
			}
			// Time to the rail ahead: X + V t + A t^2 / 2 = +-Wall (the motion is monotone within a phase here).
			const double Sign = rb::SignNonZero(V);
			const double Gap = Sign * Wall - X;
			double Hit = -1.0;
			const double Disc = V * V + 2.0 * A * Gap;
			if (Disc >= 0.0)
			{
				const double Root = (-V + Sign * rb::Sqrt(Disc)) / A; // the earlier root for a decelerating ball
				if (Root > 0.0 && Root <= Duration)
				{
					Hit = Root;
				}
			}
			const double T = Hit > 0.0 ? Hit : Duration;
			const double NewX = X + V * T + 0.5 * A * T * T;
			Distance += rb::Abs(NewX - X);
			X = NewX;
			V += A * T;
			Wy += WDot * T;
			if (Hit > 0.0)
			{
				X = Sign * Wall;
				CushionHit(Sign, V, Wy, Ball, Cloth, Run);
			}
			else if (rb::Abs(U) > 1e-12)
			{
				Wy = V / kR; // exact end of sliding (the rolling constraint, no micro-slide)
			}
			else
			{
				V = 0.0;
				Wy = 0.0;
			}
		}
		Run.Lengths = Distance / kTableLength;
		return Run;
	}

	// The same shot with WP-1's closed-form segments (integration).
	RailRun RunWithSegments(double V0, const rb::ClothParams& Cloth, double Gravity)
	{
		const rb::BallSpec Ball = PoolBall();
		const rb::NumericsConfig Numerics;
		const double Wall = 0.5 * kTableLength - kR * rb::Cos(EdgeElevation(kH, kR));
		RailRun Run;
		rb::BallState S;
		S.Position = {-0.25 * kTableLength, 0.0, kR};
		S.Velocity = {V0, 0.0, 0.0};
		S.State = rb::MotionState::Sliding;
		double T0 = 0.0;
		double Distance = 0.0;
		for (int Phase = 0; Phase < 200; ++Phase)
		{
			rb::ClassifyState(S, kR, 0.0, Numerics);
			if (!rb::IsMoving(S.State))
			{
				break;
			}
			const rb::MotionSegment Seg = rb::MakeSegment(S, T0, Ball, Cloth, 0.0, Gravity);
			const double Sign = rb::SignNonZero(Seg.Vel0.x);
			const double Gap = Sign * Wall - Seg.Pos0.x;
			const double A = Seg.Accel2.x;
			double Hit = -1.0;
			const double Disc = Seg.Vel0.x * Seg.Vel0.x + 4.0 * A * Gap;
			if (A != 0.0 && Disc >= 0.0)
			{
				const double Root = (-Seg.Vel0.x + Sign * rb::Sqrt(Disc)) / (2.0 * A);
				if (Root > 0.0 && Root <= Seg.TauEnd)
				{
					Hit = Root;
				}
			}
			if (Hit > 0.0)
			{
				rb::BallState At = rb::EvaluateSegment(Seg, Hit);
				Distance += rb::Abs(At.Position.x - Seg.Pos0.x);
				double V = At.Velocity.x;
				double Wy = At.Omega.y;
				CushionHit(Sign, V, Wy, Ball, Cloth, Run);
				S = At;
				S.Position.x = Sign * Wall;
				S.Velocity = {V, 0.0, 0.0};
				S.Omega = {0.0, Wy, 0.0};
				S.State = rb::MotionState::Sliding;
				T0 += Hit;
			}
			else
			{
				S = rb::SegmentEndState(Seg, Numerics);
				Distance += rb::Abs(S.Position.x - Seg.Pos0.x);
				T0 += Seg.TauEnd;
			}
		}
		Run.Lengths = Distance / kTableLength;
		return Run;
	}

	// v0 [m/s] for which the shot travels Target lengths (bisection, prior-art CUSH-05).
	double SpeedForLengths(double Target, const rb::ClothParams& Cloth, double Gravity)
	{
		double Lo = 1.0;
		double Hi = 15.0;
		for (int i = 0; i < 50; ++i)
		{
			const double Mid = 0.5 * (Lo + Hi);
			if (RunReference(Mid, Cloth, Gravity).Lengths < Target)
			{
				Lo = Mid;
			}
			else
			{
				Hi = Mid;
			}
		}
		return 0.5 * (Lo + Hi);
	}
}

RB_TEST(COL_CAL1_WpaRailSpeed)
{
	// Default e_c law: fast cloth (mu_r = 0.007, mu_s = 0.2 as in the reference) at 6.0 m/s: 4.17 lengths (>= 4.0);
	// default cloth (mu_r = 0.010) at 7.0 m/s: 4.18; the ball never leaves the slate (v_z = 0 at every hit). +-0.05.
	const RailRun Fast = RunReference(6.0, {0.2, 0.007, 10.0}, kG);
	const RailRun Default = RunReference(7.0, {0.2, 0.010, 10.0}, kG);
	std::printf("  CAL-1: fast cloth 6.0 m/s: %.4f lengths (%d hits); default cloth 7.0 m/s: %.4f lengths (%d hits)\n", Fast.Lengths, Fast.Hits,
		Default.Lengths, Default.Hits);
	RB_CHECK_NEAR(Fast.Lengths, 4.17, 0.05);
	RB_CHECK_NEAR(Default.Lengths, 4.18, 0.05);
	RB_CHECK(Fast.Lengths >= 4.0);
	RB_CHECK(Fast.MaxVz == 0.0 && Default.MaxVz == 0.0);
	RB_CHECK(Fast.Hits == 4 && Default.Hits == 4);
}

RB_TEST(EQP_TCAL1_RailSpeedWorstedFast)
{
	// equipment T-CAL-1 at the physics spec's "firm" speed (6.0 m/s, collisions 4.8) on CLOTH = WORSTED_860 (the
	// kClothWorstedFast preset, mu_s = 0.17 also during the impacts): >= 4.0 and about <= 4.5 lengths, z <= R + 1 mm.
	const RailRun Run = RunReference(6.0, rb::kClothWorstedFast, kG);
	std::printf("  T-CAL-1: WorstedFast at 6.0 m/s: %.4f lengths (oracle 4.290)\n", Run.Lengths);
	RB_CHECK(Run.Lengths >= 4.0);
	RB_CHECK(Run.Lengths <= 4.5);
	RB_CHECK_NEAR(Run.Lengths, 4.290, 0.02);
	RB_CHECK(Run.MaxVz == 0.0); // the constrained model never lifts the ball: z stays R
}

RB_TEST(VAL_CUSH05_WpaSpeedTestBisection)
{
	// Prior-art defaults (g = 9.81, mu_s = 0.2, mu_r = 0.010): v0 for exactly 4.0 and 4.5 lengths by bisection; FLAG if
	// v0(4.0) is outside [2.5, 5.0] m/s or the ball leaves the slate. Tier D: with the default e_c law the flag is
	// expected to fire until the cushion is calibrated on a real table (architecture 15 row 13, collisions OQ 2 / 10);
	// the test records the speeds (oracle 6.433 / 8.025 m/s) and fails only on a broken search or a slate departure.
	const rb::ClothParams Cloth{0.2, 0.010, 10.0};
	const double V40 = SpeedForLengths(4.0, Cloth, 9.81);
	const double V45 = SpeedForLengths(4.5, Cloth, 9.81);
	const bool Flag = V40 < 2.5 || V40 > 5.0 || RunReference(V40, Cloth, 9.81).MaxVz > 0.0;
	std::printf("  CUSH-05: v0(4.0 lengths) = %.4f m/s, v0(4.5 lengths) = %.4f m/s -> FLAG %s (expected: fires until calibrated)\n", V40, V45,
		Flag ? "raised" : "clear");
	RB_CHECK_NEAR(RunReference(V40, Cloth, 9.81).Lengths, 4.0, 1e-3);
	RB_CHECK_NEAR(RunReference(V45, Cloth, 9.81).Lengths, 4.5, 1e-3);
	RB_CHECK(V45 > V40);
	RB_CHECK_NEAR(V40, 6.433, 0.02);
	RB_CHECK_NEAR(V45, 8.025, 0.02);
	RB_CHECK(RunReference(V45, Cloth, 9.81).MaxVz == 0.0);
}

RB_TEST(Integ_COL_CAL1_RailSpeedWithMotionSegments)
{
	// The same calibration with WP-1's motion segments instead of the 1D reference kinematics.
	const RailRun Fast = RunWithSegments(6.0, {0.2, 0.007, 10.0}, kG);
	const RailRun Default = RunWithSegments(7.0, {0.2, 0.010, 10.0}, kG);
	RB_CHECK_NEAR(Fast.Lengths, RunReference(6.0, {0.2, 0.007, 10.0}, kG).Lengths, 1e-6);
	RB_CHECK_NEAR(Default.Lengths, RunReference(7.0, {0.2, 0.010, 10.0}, kG).Lengths, 1e-6);
	RB_CHECK_NEAR(Fast.Lengths, 4.17, 0.05);
	RB_CHECK_NEAR(Default.Lengths, 4.18, 0.05);
}
