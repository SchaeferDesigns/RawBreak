// Owner: WP-5 (event detection). Slate landing, rail-top planes and edges, support exit, outer boundary and lamp apex
// (motion C.2; physics-collisions 6.1-6.3; architecture 8.2, 8.9): A-DET-2, A-DET-3 and the predictor halves of O-1 / O-2.

#include "rbtest.h"

#include "DetectTestUtil.h"

#include <initializer_list>

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
	constexpr double kRailTopZ = 0.048;

	rb::RailTopPolygon Polygon(rb::RailTopKind Kind, const Vec3& Point, const Vec3& Normal, std::initializer_list<Vec2> Vertices,
		std::initializer_list<rb::RailEdgeKind> Edges)
	{
		rb::RailTopPolygon P;
		P.Kind = Kind;
		P.PlanePoint = Point;
		P.PlaneNormal = rb::Normalized(Normal);
		for (const Vec2& V : Vertices)
		{
			P.Vertices[P.VertexCount++] = V;
		}
		int i = 0;
		for (rb::RailEdgeKind E : Edges)
		{
			P.Edges[i++] = E;
		}
		return P;
	}

	// Flat cap square around the 9FT_PRO FOOT_LEFT corner cut (pocket surround), CCW.
	rb::RailTopPolygon CornerSurround()
	{
		rb::RailTopPolygon P = Polygon(rb::RailTopKind::RailCap, {0.0, 0.0, kRailTopZ}, {0.0, 0.0, 1.0}, {{1.25, 0.62}, {1.40, 0.62}, {1.40, 0.77}, {1.25, 0.77}},
			{rb::RailEdgeKind::Seam, rb::RailEdgeKind::OuterEdge, rb::RailEdgeKind::OuterEdge, rb::RailEdgeKind::Seam});
		P.HasCut = true;
		P.CutCenter = {1.302615, 0.667615};
		P.CutRadius = 0.062;
		P.Pocket = rb::PocketId::FootLeft;
		return P;
	}

	// Sloped cushion top of RAIL_LEFT: from the nose (y = 0.635, z = h) up to the cushion back (y = 0.6858, z = RailTopZ).
	rb::RailTopPolygon LeftCushionTop()
	{
		const double Slope = (kRailTopZ - kNoseH) / 0.0508;
		return Polygon(rb::RailTopKind::CushionTop, {0.0, 0.635, kNoseH}, {0.0, -Slope, 1.0}, {{-1.0, 0.635}, {1.0, 0.635}, {1.0, 0.6858}, {-1.0, 0.6858}},
			{rb::RailEdgeKind::Nose, rb::RailEdgeKind::Seam, rb::RailEdgeKind::CushionBack, rb::RailEdgeKind::Seam});
	}

	// Flat cap strips of RAIL_LEFT behind the cushion back, split by a seam at x = 0 (the right one with a fictional cut).
	rb::RailTopPolygon LeftCap(bool Right)
	{
		const double X0 = Right ? 0.0 : -1.0;
		const double X1 = Right ? 1.0 : 0.0;
		rb::RailTopPolygon P = Polygon(rb::RailTopKind::RailCap, {0.0, 0.0, kRailTopZ}, {0.0, 0.0, 1.0}, {{X0, 0.6858}, {X1, 0.6858}, {X1, 0.8128}, {X0, 0.8128}},
			{rb::RailEdgeKind::CushionBack, rb::RailEdgeKind::Seam, rb::RailEdgeKind::OuterEdge, rb::RailEdgeKind::Seam});
		if (Right)
		{
			P.HasCut = true;
			P.CutCenter = {0.5, 0.75};
			P.CutRadius = 0.03;
		}
		return P;
	}

	MotionSegment OnCap(const Vec3& PlanStart, const Vec3& V) { return Rolling({PlanStart.x, PlanStart.y, kRailTopZ + kR}, V); }
}

RB_TEST(ARCH_DET2_RailTopPlaneContactsIncludePocketSurroundsButNotTheCut)
{
	const rb::RailTopPolygon Surround = CornerSurround();
	// On the pocket surround, outside the cut disc: plane contact at z = RailTopZ + R.
	const MotionSegment Fall = Airborne({1.39, 0.75, 0.2}, {});
	const ContactPrediction Plane = rb::PredictRailTop(Fall, kR, Surround, rb::kInfinity, Numerics());
	RB_REQUIRE(Plane.Found);
	RB_CHECK_NEAR(Plane.Time, std::sqrt((0.2 - kRailTopZ - kR) / (0.5 * kG)), 1e-12);

	// Over the cut disc there is no surface: no plane contact; dead center over the hole no rim contact either.
	const MotionSegment Hole = Airborne({1.302615, 0.667615, 0.2}, {});
	RB_CHECK(!rb::PredictRailTop(Hole, kR, Surround, rb::kInfinity, Numerics()).Found);
	RB_CHECK(!rb::PredictRailTopEdge(Hole, kR, Surround, rb::kCutRimEdge, rb::kInfinity, Numerics()).Found);

	// 10 mm inside the cut, toward the surround's corner: the ball meets the cut rim (degree 8), not the plane.
	const Vec2 Dir{std::sqrt(0.5), std::sqrt(0.5)};
	const Vec2 Start = Surround.CutCenter + Dir * (Surround.CutRadius - 0.01);
	const MotionSegment Rim = Airborne({Start.x, Start.y, 0.2}, {});
	RB_CHECK(!rb::PredictRailTop(Rim, kR, Surround, rb::kInfinity, Numerics()).Found);
	const ContactPrediction Edge = rb::PredictRailTopEdge(Rim, kR, Surround, rb::kCutRimEdge, rb::kInfinity, Numerics());
	RB_REQUIRE(Edge.Found);
	const double ZContact = kRailTopZ + std::sqrt(kR * kR - 0.01 * 0.01);
	RB_CHECK_NEAR(Edge.Time, std::sqrt((0.2 - ZContact) / (0.5 * kG)), 1e-12);

	// Sloped cushion top: plane contact with the tilted normal; 5 mm in front of the nose line the plane still comes first
	// (its contact point lies behind the nose line) and the nose edge would only be touched later (4.10 / 6.2 coverage).
	const rb::RailTopPolygon Top = LeftCushionTop();
	const MotionSegment OnSlope = Airborne({0.0, 0.66, 0.3}, {});
	const ContactPrediction S = rb::PredictRailTop(OnSlope, kR, Top, rb::kInfinity, Numerics());
	RB_REQUIRE(S.Found);
	const Vec3 N = Top.PlaneNormal;
	const double ZPlane = kNoseH + (kR - N.y * 0.025) / N.z; // n . (p - x0) = R at y = 0.66
	RB_CHECK_NEAR(S.Time, std::sqrt((0.3 - ZPlane) / (0.5 * kG)), 1e-12);
	const MotionSegment NearNose = Airborne({0.0, 0.630, 0.3}, {});
	const ContactPrediction Slope = rb::PredictRailTop(NearNose, kR, Top, rb::kInfinity, Numerics());
	const rb::NoseSegment Nose = MakeNose({1.0, 0.635}, {-1.0, 0.635}, {0.0, -1.0});
	const ContactPrediction NoseHit = rb::PredictNoseAirborne(NearNose, kR, Nose, 0.0, rb::kInfinity, Numerics());
	RB_REQUIRE(Slope.Found);
	RB_CHECK(!NoseHit.Found || NoseHit.Time > Slope.Time);
	// Straight edges of kind Nose / Seam are not rail-top edge contacts.
	RB_CHECK(!rb::PredictRailTopEdge(NearNose, kR, Top, 0, rb::kInfinity, Numerics()).Found);
	RB_CHECK(!rb::PredictRailTopEdge(NearNose, kR, Top, 1, rb::kInfinity, Numerics()).Found);
}

RB_TEST(ARCH_DET3_SupportExitAcrossSeamRidgeAndCut)
{
	const rb::RailTopPolygon Left = LeftCap(false);
	const rb::RailTopPolygon Right = LeftCap(true);
	const double A = 0.5 * kMuR * kG;
	int Edge = -1;

	// Across the seam at x = 0 (edge 1): continue on the neighbouring polygon.
	const MotionSegment ToSeam = OnCap({-0.5, 0.75, 0.0}, {1.0, 0.0, 0.0});
	const ContactPrediction Seam = rb::PredictSupportExit(ToSeam, Left, rb::kInfinity, Edge);
	RB_REQUIRE(Seam.Found);
	RB_CHECK(Edge == 1);
	RB_CHECK_NEAR(Seam.Time, (1.0 - std::sqrt(1.0 - 4.0 * A * 0.5)) / (2.0 * A), 1e-12);
	// Continuing on the right polygon from the seam point (exactly on its edge 3, moving inward): no immediate exit.
	const MotionSegment After = OnCap(rb::PositionAt(ToSeam, Seam.Time), rb::VelocityAt(ToSeam, Seam.Time));
	const ContactPrediction Next = rb::PredictSupportExit(After, Right, rb::kInfinity, Edge);
	RB_CHECK(!Next.Found || Next.Time > 0.0);

	// Toward the table across the ridge (edge 0, CushionBack): onto the sloped top.
	const ContactPrediction Ridge = rb::PredictSupportExit(OnCap({-0.5, 0.75, 0.0}, {0.0, -1.0, 0.0}), Left, rb::kInfinity, Edge);
	RB_REQUIRE(Ridge.Found);
	RB_CHECK(Edge == 0);
	RB_CHECK_NEAR(Ridge.Time, (1.0 - std::sqrt(1.0 - 4.0 * A * (0.75 - 0.6858))) / (2.0 * A), 1e-12);

	// Into the cut disc (drops into the hole): kCutRimEdge when the CENTER enters the disc.
	const MotionSegment ToCut = OnCap({0.3, 0.75, 0.0}, {1.0, 0.0, 0.0});
	const ContactPrediction Cut = rb::PredictSupportExit(ToCut, Right, rb::kInfinity, Edge);
	RB_REQUIRE(Cut.Found);
	RB_CHECK(Edge == rb::kCutRimEdge);
	RB_CHECK_NEAR(rb::Length(rb::XY(rb::PositionAt(ToCut, Cut.Time)) - Right.CutCenter), Right.CutRadius, 1e-12);

	// Over the outer edge (edge 2); a ball at rest never exits.
	RB_CHECK(rb::PredictSupportExit(OnCap({-0.5, 0.75, 0.0}, {0.0, 1.0, 0.0}), Left, rb::kInfinity, Edge).Found && Edge == 2);
	RB_CHECK(!rb::PredictSupportExit(Stationary({-0.5, 0.75, kRailTopZ + kR}), Left, rb::kInfinity, Edge).Found && Edge == -1);

	// Dispatcher for a ball on the cap: SupportExit of its polygon (SubIndex = edge) or the outer boundary.
	rb::TableGeometry T;
	T.RailTops.PushBack(Left);
	T.RailTops.PushBack(Right);
	T.OuterBoundary = {{-1.4478, -0.8128}, {1.4478, 0.8128}};
	rb::BallTableContext Cap;
	Cap.Support = rb::SupportKind::RailCap;
	Cap.SupportPolygon = 1;
	MotionSegment Roll = ToCut;
	Roll.SupportZ = kRailTopZ;
	const rb::FeaturePrediction D =
		rb::PredictTableEvent(Roll, rb::BallSpec{}, Cap, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == rb::TableFeatureKind::SupportExit);
	RB_CHECK(D.Feature.Index == 1);
	RB_CHECK(D.Feature.SubIndex == rb::kCutRimEdge);
	RB_CHECK(D.Contact.Time == Cut.Time);
}

RB_TEST(Detect_RailTopOuterEdgeIsClippedByAFallingBall)
{
	const rb::RailTopPolygon Cap = LeftCap(false);
	// Leaves the cap over the outer edge 1 mm above resting height: the plane contact point lies beyond the polygon, the
	// edge line (y = 0.8128, z = RailTopZ) is met with the center beyond it.
	const MotionSegment Off = Airborne({-0.5, 0.81, kRailTopZ + kR + 0.001}, {0.0, 0.3, 0.0});
	RB_CHECK(!rb::PredictRailTop(Off, kR, Cap, rb::kInfinity, Numerics()).Found);
	const ContactPrediction E = rb::PredictRailTopEdge(Off, kR, Cap, 2, rb::kInfinity, Numerics());
	RB_REQUIRE(E.Found);
	const auto Gap = [&](double Tau)
	{
		const Vec3 C = rb::PositionAt(Off, Tau);
		return std::sqrt(rb::Square(C.y - 0.8128) + rb::Square(C.z - kRailTopZ)) - kR;
	};
	RB_CHECK_NEAR(E.Time, BruteForceFirstContact(Gap, Off.TauEnd), 1e-9);
	RB_CHECK(rb::PositionAt(Off, E.Time).y >= 0.8128);
}

RB_TEST(Detect_SlateLandingIsTheLaterRootOfTheFlight)
{
	const MotionSegment Hop = Airborne({0.0, 0.0, kR}, {2.0, 0.0, 1.5}, kG, 0.25);
	const ContactPrediction L = rb::PredictSlateLanding(Hop, kR, rb::kInfinity);
	RB_REQUIRE(L.Found);
	RB_CHECK_NEAR(L.Time, 0.25 + 2.0 * 1.5 / kG, 1e-12);
	RB_CHECK(L.Time <= Hop.T0 + Hop.TauEnd);
	RB_CHECK(!rb::PredictSlateLanding(Hop, kR, 0.3).Found);                                  // beyond TimeLimit
	RB_CHECK(!rb::PredictSlateLanding(Rolling({0.0, 0.0, kR}, {1.0, 0.0, 0.0}), kR, rb::kInfinity).Found); // not airborne
	// Dropped from a height: the later root of z = R.
	const ContactPrediction Drop = rb::PredictSlateLanding(Airborne({0.0, 0.0, kR + 0.05}, {}), kR, rb::kInfinity);
	RB_REQUIRE(Drop.Found);
	RB_CHECK_NEAR(Drop.Time, std::sqrt(0.1 / kG), 1e-12);
}

RB_TEST(Detect_OuterBoundaryAndLampApex)
{
	// O-1: launched (0, 3, 2.5) from (0, 0.5, R) on the 9-ft table: the center crosses y = W/2 + RAIL_WIDTH_TOTAL = 0.8128
	// at t = 0.104267 s with z = 0.23593 m.
	const rb::Aabb2 Outer{{-1.4478, -0.8128}, {1.4478, 0.8128}};
	const MotionSegment Fly = Airborne({0.0, 0.5, kR}, {0.0, 3.0, 2.5});
	const ContactPrediction Off = rb::PredictOuterBoundary(Fly, Outer, rb::kInfinity);
	RB_REQUIRE(Off.Found);
	RB_CHECK_NEAR(Off.Time, (0.8128 - 0.5) / 3.0, 1e-12);
	RB_CHECK_NEAR(rb::PositionAt(Fly, Off.Time).z, 0.23593, 1e-5);
	// A ball already outside is off the table at once.
	const ContactPrediction Out = rb::PredictOuterBoundary(Stationary({0.0, 0.9, kR}), Outer, rb::kInfinity);
	RB_CHECK(Out.Found && Out.Time == 0.0 && Out.Flags == rb::ContactFlags::AtStart);
	RB_CHECK(!rb::PredictOuterBoundary(Rolling({0.0, 0.0, kR}, {0.3, 0.1, 0.0}), Outer, rb::kInfinity).Found);

	// O-2: v_z0 = 4.5 m/s from the cloth, bar lamp underside 0.84 m over the whole table: event at the apex,
	// z_max = R + 1.0325 m.
	rb::EnvironmentSpec Bar;
	Bar.LampUndersideZ = 0.84;
	const MotionSegment Up = Airborne({0.0, 0.0, kR}, {0.0, 0.0, 4.5});
	const ContactPrediction Lamp = rb::PredictLampApex(Up, kR, Bar, kG, rb::kInfinity);
	RB_REQUIRE(Lamp.Found);
	RB_CHECK_NEAR(Lamp.Time, 4.5 / kG, 1e-12);
	RB_CHECK_NEAR(rb::PositionAt(Up, 4.5 / kG).z - kR, 1.0325, 1e-4);
	rb::EnvironmentSpec High = Bar;
	High.LampUndersideZ = 1.2;
	RB_CHECK(!rb::PredictLampApex(Up, kR, High, kG, rb::kInfinity).Found);
	rb::EnvironmentSpec Aside = Bar;
	Aside.LampFootprint = {{1.0, 1.0}, {2.0, 2.0}};
	RB_CHECK(!rb::PredictLampApex(Up, kR, Aside, kG, rb::kInfinity).Found);
	RB_CHECK(!rb::PredictLampApex(Up, kR, Bar, kG, 0.1).Found); // apex after TimeLimit
}
