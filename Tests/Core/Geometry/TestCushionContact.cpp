// Owner: WP-2 (equipment & table geometry). Cushion contact geometry: equipment 4.1, 12.2 (T-CUSH-2..5),
// physics-collisions 4.1, 4.9 (C-G1), 5.3 (facing contact offset s_f).

#include "rbtest.h"

#include "Geometry/GeometryTestUtil.h"

#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"

#include <cmath>

using namespace rb;

RB_TEST(EQP_TCUSH2_ContactElevationAngle)
{
	// Spec: 0.2733943 rad (15.66427 deg), 1e-6 rad. The printed radian value contradicts the printed degrees
	// (15.66427 deg = 0.2733931 rad) and asin(0.27) = 0.27339303 (collisions C-G1, verification log 15.6643 deg):
	// the degree value is checked at the stated 1e-6 rad and the radian value is the one it implies.
	const CushionContactGeometry G = ComputeCushionContact(kBallRadius, kCushionNoseHeight, 0.0, false);
	RB_CHECK_NEAR(G.Theta * kRadToDeg, 15.66427, 1e-6 * kRadToDeg);
	RB_CHECK_NEAR(G.Theta, 15.66427 * kDegToRad, 1e-6);
	RB_CHECK_NEAR(G.Theta, 0.27339303, 1e-8);
	RB_CHECK_NEAR(G.Theta, kCushionContactAngle, 1e-15);
	RB_CHECK_NEAR(G.SinTheta, Sin(G.Theta), 1e-15);
	RB_CHECK_NEAR(G.CosTheta, Cos(G.Theta), 1e-15);
}

RB_TEST(EQP_TCUSH3_HorizontalContactOffset)
{
	const CushionContactGeometry G = ComputeCushionContact(kBallRadius, kCushionNoseHeight, 0.0, false);
	RB_CHECK_NEAR(G.HorizontalOffset, 0.02751373, 1e-8);
	RB_CHECK_NEAR(kBallRadius - G.HorizontalOffset, 0.00106127, 1e-8);
	RB_CHECK_NEAR(G.HorizontalOffset, kBallRadius * G.CosTheta, 1e-15);
	RB_CHECK_NEAR(G.HorizontalOffset, kCushionContactOffsetXY, 1e-7);
}

RB_TEST(EQP_TCUSH4_OversizedCueBall)
{
	const double R = 0.0301625;
	const CushionContactGeometry G = ComputeCushionContact(R, kCushionNoseHeight, 0.0, false);
	RB_CHECK_NEAR(G.Theta * kRadToDeg, 11.7217, 1e-3);
	RB_CHECK_NEAR(kCushionNoseHeight / (2.0 * R), 0.60158, 1e-5);
	// collisions 4.9: the table-reaction margin tan(theta_c) = 0.207 is smaller than 0.2804 for the standard ball.
	RB_CHECK_NEAR(G.SinTheta / G.CosTheta, 0.207, 1e-3);
	const CushionContactGeometry Std = ComputeCushionContact(kBallRadius, kCushionNoseHeight, 0.0, false);
	RB_CHECK_NEAR(Std.SinTheta / Std.CosTheta, 0.2804, 1e-4);
}

RB_TEST(EQP_TCUSH5_BallFrozenToLeftRail)
{
	TableGeometry G;
	RB_REQUIRE(BuildTableGeometry(kTableNineFootPro, G) == ErrorCode::Ok);
	const NoseSegment& Left = G.Noses[static_cast<int>(CushionId::LeftFoot)];
	RB_CHECK(Left.Start.y == 0.635 && Left.InwardNormal.x == 0.0 && Left.InwardNormal.y == -1.0);
	const double Physical = ComputeCushionContact(kBallRadius, Left.Height, 0.0, false).HorizontalOffset;
	const double Compat = ComputeCushionContact(kBallRadius, Left.Height, 0.0, true).HorizontalOffset;
	RB_CHECK_NEAR(Left.Start.y + Left.InwardNormal.y * Physical, 0.6074863, 1e-7);
	RB_CHECK_NEAR(Left.Start.y + Left.InwardNormal.y * Compat, 0.606425, 1e-7);
}

RB_TEST(COL_CG1_CushionContactGeometry)
{
	const CushionContactGeometry G = ComputeCushionContact(0.028575, 0.635 * 2.0 * 0.028575, 0.0, false);
	RB_CHECK_NEAR(G.SinTheta, 0.27, 1e-8);
	RB_CHECK_NEAR(G.Theta, 0.2733930315, 1e-8);
	RB_CHECK_NEAR(G.Theta * kRadToDeg, 15.664, 5e-4); // printed to 3 decimals
	RB_CHECK_NEAR(G.CosTheta, 0.96286, 5e-6);
	RB_CHECK_NEAR(G.HorizontalOffset, 0.02751373, 1e-8);
}

RB_TEST(Geometry_CushionContactConstantsMatchTheComputation)
{
	// Review fix: kCushionContactOffsetXY was typed as 0.027513733700867 (5e-12 m off R_c); both constants are now the
	// exact doubles ComputeCushionContact returns for the standard ball (equipment 11.1: DERIVED, one source).
	const CushionContactGeometry G = ComputeCushionContact(kBallRadius, kCushionNoseHeight, 0.0, false);
	RB_CHECK(G.HorizontalOffset == kCushionContactOffsetXY);
	RB_CHECK(G.Theta == kCushionContactAngle);
	RB_CHECK_NEAR(kCushionContactOffsetXY, kBallRadius * Cos(kCushionContactAngle), 1e-17);
}

RB_TEST(Geometry_CushionContactNonFiniteInput)
{
	// Adversarial: non-finite radius or nose height gives the neutral default (no NaN leaks into a contact frame).
	for (const double Bad : {std::nan(""), kInfinity, -kInfinity})
	{
		for (const CushionContactGeometry& G :
			{ComputeCushionContact(Bad, kCushionNoseHeight, 0.0, false), ComputeCushionContact(kBallRadius, Bad, 0.0, false),
				ComputeCushionContact(kBallRadius, kCushionNoseHeight, Bad, false)})
		{
			RB_CHECK(G.SinTheta == 0.0 && G.CosTheta == 1.0 && G.Theta == 0.0 && G.HorizontalOffset == 0.0);
		}
	}
}

RB_TEST(Geometry_CushionContactWithNoseRadius)
{
	// sin(theta) = (h - R)/(R + r_n), R_c = sqrt((R + r_n)^2 - (h - R)^2) (collisions 4.10); r_n = 1 mm art value.
	const double R = kBallRadius;
	const double Rn = 0.001;
	const double H = kCushionNoseHeight;
	const CushionContactGeometry G = ComputeCushionContact(R, H, Rn, false);
	RB_CHECK_NEAR(G.SinTheta, (H - R) / (R + Rn), 1e-15);
	RB_CHECK_NEAR(G.HorizontalOffset, Sqrt((R + Rn) * (R + Rn) - (H - R) * (H - R)), 1e-15);
	RB_CHECK_NEAR(ComputeCushionContact(R, H, Rn, true).HorizontalOffset, R, 0.0);
	// Degenerate input: nose above the ball (clamped, no NaN) and zero radius.
	const CushionContactGeometry High = ComputeCushionContact(0.01, 0.05, 0.0, false);
	RB_CHECK(High.SinTheta == 1.0 && High.HorizontalOffset == 0.0 && IsFinite(High.Theta));
	RB_CHECK(ComputeCushionContact(0.0, H, 0.0, false).HorizontalOffset == 0.0);
}

RB_TEST(Geometry_FacingContactOffset)
{
	// collisions 5.3: s_f = (R - (h - R) sin(beta_v)) / cos(beta_v) = 27.573 mm for 12 deg, before R_c = 27.514 mm.
	const double Sf = FacingContactOffset(kBallRadius, kCushionNoseHeight, 12.0 * kDegToRad);
	RB_CHECK_NEAR(Sf, 0.027573, 5e-7);
	RB_CHECK(Sf > ComputeCushionContact(kBallRadius, kCushionNoseHeight, 0.0, false).HorizontalOffset);
	// The undercut face touches a resting ball at z = R (1 + sin beta_v): 34.52 mm (12 deg), 35.97 mm (15 deg).
	RB_CHECK_NEAR(kBallRadius * (1.0 + Sin(12.0 * kDegToRad)), 0.03452, 1e-5);
	RB_CHECK_NEAR(kBallRadius * (1.0 + Sin(15.0 * kDegToRad)), 0.03597, 1e-5);
	RB_CHECK_NEAR(FacingContactOffset(kBallRadius, kCushionNoseHeight, 0.0), kBallRadius, 1e-15);
}
