// Owner: WP-5 (event detection). Cushion noses, jaw arcs and facings: physics-collisions 4.10, 5.3, 9.2 (D-8, D-9) and
// the detection side of 7.1 / 7.3 (resting, rolling along and slip-pressed into a rail). Features are built by hand.

#include "rbtest.h"

#include "DetectTestUtil.h"

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
	// RAIL_LEFT (C3/C4 at y = +W/2 = 0.635), counter-clockwise: runs toward -x, inward normal -y.
	rb::NoseSegment LeftRail(double XFrom = 1.2, double XTo = -1.2) { return MakeNose({XFrom, 0.635}, {XTo, 0.635}, {0.0, -1.0}); }

	ContactPrediction OnCloth(const MotionSegment& S, const rb::NoseSegment& Nose, double TimeLimit = rb::kInfinity)
	{
		return rb::PredictNoseOnCloth(S, kR, Nose, NoseContactOffset(), TimeLimit, Numerics());
	}
}

RB_TEST(COL_D8_RollingBallMeetsNoseLine)
{
	const MotionSegment S = Rolling({0.0, 0.0, kR}, {0.0, 1.0, 0.0});
	RB_CHECK_NEAR(NoseContactOffset(), 0.02751373, 1e-8); // C-G1 value of R_c
	const ContactPrediction P = OnCloth(S, LeftRail());
	RB_REQUIRE(P.Found);
	RB_CHECK(P.Flags == 0);
	RB_CHECK_NEAR(P.Time, 0.6267471125, 1e-9);
	RB_CHECK_NEAR(rb::PositionAt(S, P.Time).y, 0.6074863, 1e-7);
}

RB_TEST(COL_D9_RollingBallMeetsRoundedJawPoint)
{
	const MotionSegment S = Rolling({0.0, 0.0, kR}, {1.0, 0.0, 0.0});
	// Exposed arc facing the approaching ball (directions from O between -180 and -90 deg).
	const rb::JawArc Jaw = MakeJaw({0.3, 0.02}, 0.004, -rb::kPi, 0.5 * rb::kPi);
	const ContactPrediction P = rb::PredictJawArcOnCloth(S, kR, Jaw, NoseContactOffset(), rb::kInfinity, Numerics());
	RB_REQUIRE(P.Found);
	RB_CHECK_NEAR(P.Time, 0.2794758674, 1e-9); // the second root (0.32968 s) is the exit, never reported
	RB_CHECK_NEAR(rb::Length(rb::XY(rb::PositionAt(S, P.Time)) - Jaw.Center), 0.004 + NoseContactOffset(), 1e-12);

	// The same circle with its exposed arc on the far side: the contact direction is not on the arc -> no jaw event.
	const rb::JawArc Hidden = MakeJaw({0.3, 0.02}, 0.004, 0.0, 0.5 * rb::kPi);
	RB_CHECK(!rb::PredictJawArcOnCloth(S, kR, Hidden, NoseContactOffset(), rb::kInfinity, Numerics()).Found);
}

RB_TEST(Detect_NoseOnClothSegmentExtentWindowAndStartRules)
{
	const double Rc = NoseContactOffset();
	// The same roll into a nose segment that ends before the contact point: no event (the jaw arc covers the joint).
	RB_CHECK(!OnCloth(Rolling({0.0, 0.0, kR}, {0.0, 1.0, 0.0}), LeftRail(-0.1, -1.2)).Found);
	// ... and one whose end is within SegmentParamSlack of it still reports it (watertight joints, prior-art 5.7).
	RB_CHECK(OnCloth(Rolling({0.0, 0.0, kR}, {0.0, 1.0, 0.0}), LeftRail(1.2, 5e-10)).Found);
	// TimeLimit cuts the window.
	RB_CHECK(!OnCloth(Rolling({0.0, 0.0, kR}, {0.0, 1.0, 0.0}), LeftRail(), 0.6).Found);

	// Frozen to the rail and struck into it: contact at the window start (ROB-04 detection side).
	const ContactPrediction Into = OnCloth(Sliding({0.2, 0.635 - Rc, kR}, {0.0, 1.0, 0.0}, {}, kMuS, kG, 1.5), LeftRail());
	RB_REQUIRE(Into.Found);
	RB_CHECK(Into.Time == 1.5);
	RB_CHECK(Into.Flags == rb::ContactFlags::AtStart);
	// ... at contact +- 1e-12 m as well.
	RB_CHECK(OnCloth(Sliding({0.2, 0.635 - Rc - 1e-12, kR}, {0.0, 1.0, 0.0}, {}), LeftRail()).Time == 0.0);
	RB_CHECK(OnCloth(Sliding({0.2, 0.635 - Rc + 1e-12, kR}, {0.0, 1.0, 0.0}, {}), LeftRail()).Time == 0.0);

	// Resting against the rail, or rolling exactly along it: no event (7.1, Z-1 / Z-2 detection side).
	RB_CHECK(!OnCloth(Stationary({0.2, 0.635 - Rc, kR}), LeftRail()).Found);
	RB_CHECK(!OnCloth(Rolling({0.5, 0.635 - Rc, kR}, {-1.0, 0.0, 0.0}), LeftRail()).Found);
	// Rebounding away from the rail after a resolution: no re-detection.
	RB_CHECK(!OnCloth(Rolling({0.5, 0.635 - Rc, kR}, {-1.0, -0.5, 0.0}), LeftRail()).Found);

	// Slip-pressed into the rail with zero normal speed (e.g. after an e = 0 micro-impact, 7.1): pressing at the start.
	const MotionSegment Pressed = Sliding({0.2, 0.635 - Rc, kR}, {1.0, 0.0, 0.0}, {-20.0, 0.0, 0.0});
	RB_CHECK(Pressed.Accel2.y > 0.0); // slip acceleration into the rail
	const ContactPrediction Press = OnCloth(Pressed, LeftRail());
	RB_REQUIRE(Press.Found);
	RB_CHECK(Press.Time == 0.0);
	RB_CHECK(Press.Flags == (rb::ContactFlags::AtStart | rb::ContactFlags::Pressing));

	// Deep overlap (corrupt state) is flagged.
	const ContactPrediction Deep = OnCloth(Sliding({0.2, 0.635 - Rc + 5e-6, kR}, {0.0, 1.0, 0.0}, {}), LeftRail());
	RB_CHECK(Deep.Found && Deep.Flags == (rb::ContactFlags::AtStart | rb::ContactFlags::Overlap));
}

RB_TEST(Detect_NoseAirborneEdgeContactAndTableSideRule)
{
	const rb::NoseSegment Nose = LeftRail();
	// Straight in at 2 m/s with the center 10 mm above the nose height, no gravity drift (G-1 geometry): the contact is
	// at |q| = R, i.e. 26.768 mm in front of the nose line.
	MotionSegment S = Airborne({0.3, 0.3, kNoseH + 0.01}, {0.0, 2.0, 0.0});
	S.Accel2 = {};
	S.TauEnd = 1.0;
	const ContactPrediction P = rb::PredictNoseAirborne(S, kR, Nose, 0.0, rb::kInfinity, Numerics());
	RB_REQUIRE(P.Found);
	const double Front = std::sqrt(kR * kR - 0.01 * 0.01);
	RB_CHECK_NEAR(rb::PositionAt(S, P.Time).y, 0.635 - Front, 1e-12);

	// O-1: launched (0, 3, 2.5) from (0, 0.5, R): the center passes the nose line 131 mm up - no nose contact.
	const MotionSegment Fly = Airborne({0.0, 0.5, kR}, {0.0, 3.0, 2.5});
	RB_CHECK(!rb::PredictNoseAirborne(Fly, kR, Nose, 0.0, rb::kInfinity, Numerics()).Found);

	// A ball over the rail (center behind the nose line) falling onto the nose edge from above-behind: not a nose
	// contact (the rail-top planes own it, 4.10 / 6.2).
	MotionSegment Behind = Airborne({0.3, 0.645, kNoseH + 0.05}, {0.0, 0.0, 0.0});
	RB_CHECK(!rb::PredictNoseAirborne(Behind, kR, Nose, 0.0, rb::kInfinity, Numerics()).Found);
	// ... while a ball on the table side falling onto it is one.
	MotionSegment Front2 = Airborne({0.3, 0.625, kNoseH + 0.05}, {0.0, 0.0, 0.0});
	const ContactPrediction Fall = rb::PredictNoseAirborne(Front2, kR, Nose, 0.0, rb::kInfinity, Numerics());
	RB_REQUIRE(Fall.Found);
	const Vec3 C = rb::PositionAt(Front2, Fall.Time);
	RB_CHECK_NEAR(std::sqrt(rb::Square(C.y - 0.635) + rb::Square(C.z - kNoseH)), kR, 1e-12);
}

RB_TEST(Detect_JawArcAirborneUsesTheSphereApproximation)
{
	// A ball moving horizontally at z = R meets the arc's center sphere at the plan distance
	// sqrt((R + r_j)^2 - (h - R)^2) = 31.648 mm (4.10), not at the exact r_j + R_c = 31.514 mm.
	const rb::JawArc Jaw = MakeJaw({0.3, 0.0}, 0.004, -rb::kPi, 0.5 * rb::kPi);
	MotionSegment S = Airborne({0.0, 0.0, kR}, {1.0, 0.0, 0.0});
	S.Accel2 = {};
	S.TauEnd = 1.0;
	const ContactPrediction P = rb::PredictJawArcAirborne(S, kR, Jaw, rb::kInfinity, Numerics());
	RB_REQUIRE(P.Found);
	RB_CHECK_NEAR(0.3 - rb::PositionAt(S, P.Time).x, 0.0316481522752514, 1e-12);
}

RB_TEST(Detect_FacingOnShelfAndAirborneAndTopEdge)
{
	// A facing line along +x at y = 0.7, pocket side -y (PocketNormal (0, -1)), back draft 12 deg.
	const rb::Facing Face = MakeFacing({0.0, 0.7}, {0.1, 0.7}, {0.0, -1.0});
	const double Sf = (kR - (kNoseH - kR) * std::sin(Face.Backdraft)) / std::cos(Face.Backdraft);
	RB_CHECK_NEAR(Sf, 0.027573, 1e-6); // 5.3: s_f = 27.573 mm

	const MotionSegment OnShelf = Rolling({0.05, 0.6, kR}, {0.0, 1.0, 0.0});
	const ContactPrediction P = rb::PredictFacingOnShelf(OnShelf, kR, Face, Sf, rb::kInfinity, Numerics());
	RB_REQUIRE(P.Found);
	RB_CHECK_NEAR(rb::PositionAt(OnShelf, P.Time).y, 0.7 - Sf, 1e-12);
	// Beyond the facing's extent: nothing.
	RB_CHECK(!rb::PredictFacingOnShelf(Rolling({0.2, 0.6, kR}, {0.0, 1.0, 0.0}), kR, Face, Sf, rb::kInfinity, Numerics()).Found);

	// Airborne at z = R toward the undercut face: plane distance R, contact point R (1 + sin beta) high.
	MotionSegment Fly = Airborne({0.05, 0.6, kR}, {0.0, 1.0, 0.0});
	Fly.Accel2 = {};
	Fly.TauEnd = 1.0;
	const ContactPrediction A = rb::PredictFacingAirborne(Fly, kR, Face, rb::kInfinity, Numerics());
	RB_REQUIRE(A.Found);
	const Vec3 N = Vec3{0.0, -std::cos(Face.Backdraft), -std::sin(Face.Backdraft)};
	RB_CHECK_NEAR(rb::Dot(N, rb::PositionAt(Fly, A.Time) - Vec3{0.0, 0.7, kNoseH}), kR, 1e-12);
	RB_CHECK_NEAR(rb::PositionAt(Fly, A.Time).y, 0.7 - Sf, 1e-12); // at z = R the plane distance R is the shelf distance s_f

	// Flying above the face: its contact point would be above h -> no face contact, but the top edge is hit.
	MotionSegment High = Airborne({0.05, 0.6, kNoseH + 0.02}, {0.0, 1.0, 0.0});
	High.Accel2 = {};
	High.TauEnd = 1.0;
	RB_CHECK(!rb::PredictFacingAirborne(High, kR, Face, rb::kInfinity, Numerics()).Found);
	const ContactPrediction E = rb::PredictFacingTopEdge(High, kR, Face, rb::kInfinity, Numerics());
	RB_REQUIRE(E.Found);
	const Vec3 C = rb::PositionAt(High, E.Time);
	RB_CHECK_NEAR(std::sqrt(rb::Square(C.y - 0.7) + rb::Square(C.z - kNoseH)), kR, 1e-12);
}
