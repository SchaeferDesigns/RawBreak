// Owner: WP-5 (event detection). Pocket elements (physics-collisions 5.3, 5.4, 6.1; architecture 8.2, 8.9): drop edge,
// liner / back wall with its height rules, the degree-8 rim torus, capture depth / circle, pocket exit, and A-DET-1
// (airborne ball over a corner pocket). The 9FT_PRO FOOT_LEFT pocket is built by hand (DetectTestUtil.h).

#include "rbtest.h"

#include "DetectTestUtil.h"

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
	const Vec3 kAxis{std::sqrt(0.5), std::sqrt(0.5), 0.0}; // pocket axis, out of the table

	Vec3 AlongAxis(const rb::PocketGeometry& P, double S, double Z) { return rb::ToVec3(P.CaptureCenter, Z) + kAxis * S; }

	// A table holding only the pockets (P3 real, the others far away) and a large outer boundary.
	rb::TableGeometry PocketTable()
	{
		rb::TableGeometry T;
		for (int i = 0; i < rb::kPocketCount; ++i)
		{
			rb::PocketGeometry P = CornerPocket();
			P.Id = static_cast<rb::PocketId>(i);
			if (i != static_cast<int>(rb::PocketId::FootLeft))
			{
				P.CaptureCenter = {-10.0 - i, -10.0};
			}
			T.Pockets.PushBack(P);
		}
		T.OuterBoundary = {{-5.0, -5.0}, {5.0, 5.0}};
		return T;
	}

	rb::BallTableContext InPocket(rb::PocketId P)
	{
		rb::BallTableContext C;
		C.Pocket = P;
		return C;
	}

	rb::FeaturePrediction Dispatch(const MotionSegment& S, const rb::TableGeometry& T, const rb::BallTableContext& C = {})
	{
		return rb::PredictTableEvent(S, rb::BallSpec{}, C, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
	}

	// Distance of the center from the rim torus core circle (radius a_d at z = -r_d) minus (R + r_d).
	double TorusGap(const rb::PocketGeometry& P, const Vec3& C)
	{
		const double Rho = rb::Length(rb::XY(C) - P.CaptureCenter);
		return std::sqrt(rb::Square(Rho - P.DropEdgeRadius) + rb::Square(C.z + P.DropRadius)) - (kR + P.DropRadius);
	}
}

RB_TEST(ARCH_DET1_AirborneBallOverCornerPocketMeetsBackWallAndRim)
{
	const rb::PocketGeometry P = CornerPocket();
	const rb::TableGeometry T = PocketTable();
	const double Inner = P.CaptureRadius - kR;

	// (a) Low flight across the hole along the axis: nothing on the front arc (no wall above -r_d, far above the rim),
	//     then the back wall at horizontal distance r_p - R behind C_cap, below WallTopZ.
	const MotionSegment Low = Airborne(AlongAxis(P, -0.12, kR + 0.005), kAxis * 2.0 + Vec3{0.0, 0.0, 0.5});
	const ContactPrediction Wall = rb::PredictLinerWall(Low, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(Wall.Found);
	RB_CHECK_NEAR(Wall.Time, (0.12 + Inner) / 2.0, 1e-12);
	RB_CHECK(rb::PositionAt(Low, Wall.Time).z <= P.WallTopZ);
	RB_CHECK(!rb::PredictRimTorus(Low, kR, P, rb::kInfinity, Numerics()).Found);
	const rb::FeaturePrediction D = Dispatch(Low, T);
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == rb::TableFeatureKind::LinerWall);
	RB_CHECK(D.Feature.Index == static_cast<int>(rb::PocketId::FootLeft));
	RB_CHECK(D.Contact.Time == Wall.Time);

	// (b) The same flight 5 cm higher crosses the back wall above WallTopZ: the wall is ignored there (the rail top owns it).
	const MotionSegment High = Airborne(AlongAxis(P, -0.12, kR + 0.05), kAxis * 2.0 + Vec3{0.0, 0.0, 0.5});
	RB_CHECK(rb::PositionAt(High, (0.12 + Inner) / 2.0).z > P.WallTopZ);
	RB_CHECK(!rb::PredictLinerWall(High, kR, P, rb::kInfinity, Numerics()).Found);
	RB_CHECK(!Dispatch(High, T).Contact.Found);

	// (c) A ball dropping onto the rounded rim (annulus r_p < rho < a_d, front arc). The rounding lies below the shelf, so
	//     every torus contact has the center below z = R: the airborne segment ends with its z = R landing first (no torus
	//     event inside its window) and the continuation without slate (PocketFall, collisions 6.1 step 1) meets the torus.
	const double Rho0 = P.DropEdgeRadius - 0.002;
	const MotionSegment Drop = Airborne(AlongAxis(P, -Rho0, kR + 0.03), {});
	const double Zc = -P.DropRadius + std::sqrt(rb::Square(kR + P.DropRadius) - 0.002 * 0.002);
	RB_CHECK(Zc < kR);
	RB_CHECK(!rb::PredictRimTorus(Drop, kR, P, rb::kInfinity, Numerics()).Found);
	const MotionSegment Fall = PocketFall(rb::PositionAt(Drop, Drop.TauEnd), rb::VelocityAt(Drop, Drop.TauEnd), kG, Drop.TauEnd);
	const ContactPrediction Rim = rb::PredictRimTorus(Fall, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(Rim.Found);
	RB_CHECK_NEAR(Rim.Time, std::sqrt((kR + 0.03 - Zc) / (0.5 * kG)), 1e-12);
	RB_CHECK_NEAR(TorusGap(P, rb::PositionAt(Fall, Rim.Time - Fall.T0)), 0.0, 1e-12);
	const rb::FeaturePrediction DR = Dispatch(Fall, T, InPocket(rb::PocketId::FootLeft));
	RB_REQUIRE(DR.Contact.Found);
	RB_CHECK(DR.Feature.Kind == rb::TableFeatureKind::RimTorus);
	RB_CHECK(DR.Contact.Time == Rim.Time);
	// Just outside a_d the ball lands on the flat shelf; its continuation never meets the torus (rho > a_d is not rounding).
	const MotionSegment Shelf = Airborne(AlongAxis(P, -(P.DropEdgeRadius + 0.002), kR + 0.03), {});
	RB_CHECK(!rb::PredictRimTorus(PocketFall(rb::PositionAt(Shelf, Shelf.TauEnd), rb::VelocityAt(Shelf, Shelf.TauEnd)), kR, P, rb::kInfinity, Numerics()).Found);
}

RB_TEST(Detect_RimTorusAndFrontLinerFromInsideThePocket)
{
	const rb::PocketGeometry P = CornerPocket();
	const rb::TableGeometry T = PocketTable();

	// Moving back toward the table just below the shelf level: meets the rounding from inside (degree 8, infinite window).
	const MotionSegment Shallow = PocketFall(AlongAxis(P, -0.01, -0.002), kAxis * -1.0 + Vec3{0.0, 0.0, 0.3});
	const ContactPrediction Rim = rb::PredictRimTorus(Shallow, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(Rim.Found);
	const double Ref = BruteForceFirstContact([&](double Tau) { return TorusGap(P, rb::PositionAt(Shallow, Tau)); }, 0.1);
	RB_CHECK_NEAR(Rim.Time, Ref, 1e-9);
	RB_CHECK(!rb::PredictLinerWall(Shallow, kR, P, rb::kInfinity, Numerics()).Found || rb::PredictLinerWall(Shallow, kR, P, rb::kInfinity, Numerics()).Time > Rim.Time);

	// Deeper, the front wall exists (contact z < -r_d): liner at horizontal distance r_p - R.
	const MotionSegment Deep = PocketFall(AlongAxis(P, -0.01, -0.02), kAxis * -1.0);
	const ContactPrediction Liner = rb::PredictLinerWall(Deep, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(Liner.Found);
	RB_CHECK_NEAR(Liner.Time, P.CaptureRadius - kR - 0.01, 1e-12);
	RB_CHECK(!rb::PredictRimTorus(Deep, kR, P, rb::kInfinity, Numerics()).Found);

	// Outside the hole wall is free space (shelf, table), not material: a ball hopping on the shelf beside the front arc,
	// moving away from the pocket, has no liner contact (and no overlap diagnostic).
	const double Side = (-135.0 + 80.0) * rb::kDegToRad;
	const Vec2 Beside = P.CaptureCenter + Vec2{std::cos(Side), std::sin(Side)} * 0.09;
	const ContactPrediction Hop = rb::PredictLinerWall(Airborne({Beside.x, Beside.y, kR + 0.001}, Vec3{std::cos(Side), std::sin(Side), 0.0} * 0.5 + Vec3{0.0, 0.0, 0.2}),
		kR, P, rb::kInfinity, Numerics());
	RB_CHECK(!Hop.Found);
	RB_CHECK(Hop.Flags == 0);

	// Dispatcher in PocketFall: the earliest of facings / arcs / liner / rim / capture depth / exit of the ball's pocket.
	const rb::FeaturePrediction D = Dispatch(Deep, T, InPocket(rb::PocketId::FootLeft));
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == rb::TableFeatureKind::LinerWall);
	RB_CHECK(D.Contact.Time == Liner.Time);
	const MotionSegment Straight = PocketFall(AlongAxis(P, 0.0, 0.0), {});
	const rb::FeaturePrediction C = Dispatch(Straight, T, InPocket(rb::PocketId::FootLeft));
	RB_REQUIRE(C.Contact.Found);
	RB_CHECK(C.Feature.Kind == rb::TableFeatureKind::CaptureDepth);
	RB_CHECK_NEAR(C.Contact.Time, std::sqrt(2.0 * kR / kG), 1e-12); // z = -R
}

RB_TEST(Detect_DropEdgeAlongTheAxisAndLipHang)
{
	const rb::PocketGeometry P = CornerPocket();
	// P-3 approach: rolling at 1 m/s along the axis from 0.3 m out; the drop edge is at rho = a_d (not r_p).
	const MotionSegment Roll = Rolling(AlongAxis(P, -0.3, kR), kAxis);
	const ContactPrediction E = rb::PredictDropEdge(Roll, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(E.Found);
	const double A = 0.5 * kMuR * kG;
	const double Dist = 0.3 - P.DropEdgeRadius;
	RB_CHECK_NEAR(E.Time, (1.0 - std::sqrt(1.0 - 4.0 * A * Dist)) / (2.0 * A), 1e-12);
	RB_CHECK_NEAR(rb::Length(rb::VelocityAt(Roll, E.Time)), 0.97, 0.01); // "about 0.97 m/s > 0.572": immediate leave

	// P-4 lip hang: v0 = 0.0990285 m/s rolls 0.05 m. Stopping 0.5 mm outside a_d: no event; 0.5 mm inside: it falls.
	const Vec3 V0 = kAxis * 0.0990285;
	RB_CHECK(!rb::PredictDropEdge(Rolling(AlongAxis(P, -(P.DropEdgeRadius + 0.0005 + 0.05), kR), V0), kR, P, rb::kInfinity, Numerics()).Found);
	RB_CHECK(rb::PredictDropEdge(Rolling(AlongAxis(P, -(P.DropEdgeRadius - 0.0005 + 0.05), kR), V0), kR, P, rb::kInfinity, Numerics()).Found);
	// A stationary ball exactly on the circle is not crossed (5.5).
	RB_CHECK(!rb::PredictDropEdge(Stationary(AlongAxis(P, -P.DropEdgeRadius, kR)), kR, P, rb::kInfinity, Numerics()).Found);

	// Crossing the circle on the BACK arc is not a drop edge (the rounding exists only between the facings).
	RB_CHECK(!rb::PredictDropEdge(Rolling(AlongAxis(P, 0.2, kR), kAxis * -1.0), kR, P, rb::kInfinity, Numerics()).Found);

	// CaptureCircle model (XREF-01): the center enters r_p.
	const ContactPrediction Cap = rb::PredictCaptureCircle(Roll, P, rb::kInfinity, Numerics());
	RB_REQUIRE(Cap.Found);
	RB_CHECK_NEAR(rb::Length(rb::XY(rb::PositionAt(Roll, Cap.Time)) - P.CaptureCenter), P.CaptureRadius, 1e-12);
}

RB_TEST(Detect_PocketExitNeedsTheCenterAboveR)
{
	const rb::PocketGeometry P = CornerPocket();
	// Bounced up hard and back over the table: the center passes a_d above z = R -> BallPocketExit.
	const MotionSegment Out = PocketFall(AlongAxis(P, -0.03, 0.0), kAxis * -2.0 + Vec3{0.0, 0.0, 3.0});
	const ContactPrediction E = rb::PredictPocketExit(Out, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(E.Found);
	RB_CHECK_NEAR(E.Time, (P.DropEdgeRadius - 0.03) / 2.0, 1e-12);
	RB_CHECK(rb::PositionAt(Out, E.Time).z > kR);
	// Too low: crossing a_d below z = R is not an exit (the rim torus would have been hit).
	const MotionSegment Low = PocketFall(AlongAxis(P, -0.03, 0.0), kAxis * -2.0 + Vec3{0.0, 0.0, 1.5});
	RB_CHECK(!rb::PredictPocketExit(Low, kR, P, rb::kInfinity, Numerics()).Found);
	// Leaving the pivot inward (on the circle at z = R, moving into the hole) is not an exit either.
	const MotionSegment Leave = PocketFall(AlongAxis(P, -P.DropEdgeRadius, kR), kAxis * 0.8);
	RB_CHECK(!rb::PredictPocketExit(Leave, kR, P, rb::kInfinity, Numerics()).Found);
	// Capture depth: z = -R.
	const ContactPrediction Cap = rb::PredictCaptureDepth(Leave, kR, rb::kInfinity, Numerics());
	RB_REQUIRE(Cap.Found);
	RB_CHECK_NEAR(rb::PositionAt(Leave, Cap.Time).z, -kR, 1e-12);
}
