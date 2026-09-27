// WP-6b: landing routing and rail-top geometry helpers (physics-collisions 6.1, 6.2) and the pocket / rail-top hooks of
// Private/rb/Physics/SimInternal.h driven on a hand-built workspace. Standalone: only WP-6b's own decisions and bookkeeping are
// checked (the loop's services are WP-6a's).
#include "rbtest.h"

#include "../Islands/SimWorkspaceTestUtil.h"

#include "rb/Physics/PocketDrop.h"

using namespace isltest;

namespace
{
	rb::TableFeatureRef Ref(rb::TableFeatureKind Kind, int Index, int Sub = 0)
	{
		rb::TableFeatureRef R;
		R.Kind = Kind;
		R.Index = static_cast<std::uint8_t>(Index);
		R.SubIndex = static_cast<std::uint8_t>(Sub);
		return R;
	}

	// A ball rolling (no slip) at Speed along the FOOT_LEFT pocket axis with its center exactly on the drop-edge circle.
	std::unique_ptr<Scene> AtTheDropEdge(double Speed)
	{
		std::unique_ptr<Scene> S = MakeScene();
		const rb::PocketGeometry& G = S->Table.Pockets[kFootLeft];
		const Vec3 Axis = rb::ToVec3(G.Axis);
		const Vec3 V = Axis * Speed;
		Place(*S, 0, rb::ToVec3(G.CaptureCenter, kR) - Axis * G.DropEdgeRadius, V, rb::RollingOmegaH(V, kR));
		return S;
	}
}

RB_TEST(PocketFlow_LandingClassificationFollowsCollisions61)
{
	// Landing routing (collisions 6.1): inside the capture circle, or in the rounded annulus r_p < rho < a_d on the front arc ->
	// the pocket (no slate); the playing surface and the shelf -> slate impact; behind a nose line -> the rail (missed event).
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = S->Table;
	const rb::PocketGeometry& G = T.Pockets[kFootLeft];
	const Vec2 C = G.CaptureCenter;
	int Pocket = -2;
	using rb::sim::LandingSurface;
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::GeometricLevelA, C, Pocket) == LandingSurface::PocketHole && Pocket == kFootLeft);
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::GeometricLevelA, C - G.Axis * (G.DropEdgeRadius - 1e-3), Pocket) == LandingSurface::PocketHole &&
		Pocket == kFootLeft);
	RB_CHECK(rb::sim::OnFrontArc(G, C - G.Axis * G.CaptureRadius));
	RB_CHECK(!rb::sim::OnFrontArc(G, C + G.Axis * G.CaptureRadius));
	// The annulus behind the hole lies under the rail: not a pocket landing.
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::GeometricLevelA, C + G.Axis * (G.CaptureRadius + 2e-3), Pocket) == LandingSurface::OverRail);
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::GeometricLevelA, Vec2{0.1, 0.2}, Pocket) == LandingSurface::PlayingSurface && Pocket == -1);
	// Beyond the side pocket's mouth line (outside the nose rectangle), next to a jaw point, outside a_d: the shelf.
	const rb::PocketGeometry& Side = T.Pockets[static_cast<int>(rb::PocketId::SideLeft)];
	const Vec2 Shelf = Side.JawPoint[0] + (Side.MouthMid - Side.JawPoint[0]) * 0.1 + Side.Axis * 2e-3;
	RB_REQUIRE(!T.PlayingArea.Contains(Shelf) && rb::Length(Shelf - Side.CaptureCenter) > Side.DropEdgeRadius);
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::GeometricLevelA, Shelf, Pocket) == LandingSurface::Shelf);
	// The corner shelf in front of the drop edge lies inside the nose rectangle (the mouth cuts its corner): the playing surface.
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::GeometricLevelA, G.MouthMid + G.Axis * 1e-3, Pocket) == LandingSurface::PlayingSurface);
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::GeometricLevelA, Vec2{0.3, T.HalfWidth + 0.05}, Pocket) == LandingSurface::OverRail);
	// pooltool circle pockets (XREF-01): only the capture circle.
	const Vec2 Annulus = C - G.Axis * (0.5 * (G.CaptureRadius + G.DropEdgeRadius));
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::CaptureCircle, Annulus, Pocket) != LandingSurface::PocketHole);
	RB_CHECK(rb::sim::ClassifyLandingPoint(T, rb::PocketModel::CaptureCircle, C, Pocket) == LandingSurface::PocketHole);
	RB_CHECK(rb::sim::PocketContaining(T, rb::PocketModel::GeometricLevelA, Annulus) == kFootLeft);
	RB_CHECK(rb::sim::PocketContaining(T, rb::PocketModel::CaptureCircle, Annulus) == -1);
	RB_CHECK(rb::sim::PocketContaining(T, rb::PocketModel::GeometricLevelA, Vec2{}) == -1);
}

RB_TEST(PocketFlow_RailTopPolygonLookupAndLowRebound)
{
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = S->Table;
	// On the left cap behind the cushion back, and on the left cushion top.
	const Vec2 OnCap{0.3, T.HalfWidth + T.Spec.CushionWidth + 0.05};
	const int Cap = rb::sim::FindRailTopPolygon(T, OnCap, false, rb::RailTopKind::RailCap, Vec2{}, 0.0);
	RB_REQUIRE(Cap >= 0);
	RB_CHECK(T.RailTops[Cap].Kind == rb::RailTopKind::RailCap);
	RB_CHECK_NEAR(rb::sim::RailTopHeightAt(T.RailTops[Cap], OnCap), T.Spec.RailTopZ, 1e-15);
	const Vec2 OnSlope{0.3, T.HalfWidth + 0.02};
	const int Slope = rb::sim::FindRailTopPolygon(T, OnSlope, true, rb::RailTopKind::CushionTop, Vec2{}, 0.0);
	RB_REQUIRE(Slope >= 0);
	RB_CHECK(T.RailTops[Slope].Kind == rb::RailTopKind::CushionTop);
	RB_CHECK(rb::sim::FindRailTopPolygon(T, OnSlope, false, rb::RailTopKind::RailCap, Vec2{}, 0.0) == -1);
	// The cushion-top plane runs from h at the nose line to RailTopZ at the cushion back.
	RB_CHECK_NEAR(rb::sim::RailTopHeightAt(T.RailTops[Slope], Vec2{0.3, T.HalfWidth}), T.Spec.CushionNoseHeight, 1e-12);
	RB_CHECK_NEAR(rb::sim::RailTopHeightAt(T.RailTops[Slope], Vec2{0.3, T.HalfWidth + T.Spec.CushionWidth}), T.Spec.RailTopZ, 1e-12);
	// A seam shared by two cap polygons: the one the motion points into wins.
	int Seams = 0;
	for (int i = 0; i < T.RailTops.Size(); ++i)
	{
		const rb::RailTopPolygon& Poly = T.RailTops[i];
		for (int e = 0; e < Poly.VertexCount; ++e)
		{
			if (Poly.Kind != rb::RailTopKind::RailCap || Poly.Edges[e] != rb::RailEdgeKind::Seam)
			{
				continue;
			}
			const Vec2 A = Poly.Vertices[e];
			const Vec2 B = Poly.Vertices[(e + 1) % Poly.VertexCount];
			const Vec2 M = (A + B) * 0.5;
			const Vec2 Out = rb::Normalized(Vec2{(B - A).y, -(B - A).x});
			const int Neighbour = rb::sim::FindRailTopPolygon(T, M + Out * 1e-4, false, rb::RailTopKind::RailCap, Vec2{}, 0.0);
			if (Neighbour < 0 || Neighbour == i || (Poly.HasCut && rb::Length(M - Poly.CutCenter) < Poly.CutRadius + 1e-3))
			{
				continue;
			}
			++Seams;
			RB_CHECK(rb::sim::FindRailTopPolygon(T, M, false, rb::RailTopKind::RailCap, Out * 1e-6, 1e-9) == Neighbour);
			RB_CHECK(rb::sim::FindRailTopPolygon(T, M, false, rb::RailTopKind::RailCap, -Out * 1e-6, 1e-9) == i);
		}
	}
	RB_CHECK(Seams > 0);
	// Low rebound (collisions 6.2): below h_min along the normal against gravity.
	const Vec3 Up{0.0, 0.0, 1.0};
	RB_CHECK(rb::sim::IsLowRebound({0.3, 0.0, 0.19}, Up, kG, 0.002));   // 0.19^2 / 2g = 1.8 mm
	RB_CHECK(!rb::sim::IsLowRebound({0.0, 0.0, 0.21}, Up, kG, 0.002));  // 2.2 mm
	RB_CHECK(rb::sim::IsLowRebound({1.0, 0.0, -0.1}, Up, kG, 0.002));   // no rebound at all
	RB_CHECK(!rb::sim::IsLowRebound({0.0, 0.0, 0.0}, rb::Vec3{1.0, 0.0, 0.0}, kG, 0.002)); // a wall is no support
}

RB_TEST(PocketFlow_DropEdgeStartsThePivotOrTheFreeFall)
{
	// Collisions 5.4: v_perp < sqrt(g rho) -> PIVOT macro-step about the rounding axis (the pivot path of the ball, the pocket in
	// the context); v_perp >= sqrt(g rho) -> immediate free fall (no pivot).
	for (int Fast = 0; Fast < 2; ++Fast)
	{
		const double Speed = Fast != 0 ? 0.9 : 0.2;
		std::unique_ptr<Scene> S = AtTheDropEdge(Speed);
		RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
		std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
		const rb::PocketGeometry& G = S->Table.Pockets[kFootLeft];
		RB_REQUIRE(rb::sim::ProcessPocketEvent(*Ws, 0, Ref(rb::TableFeatureKind::DropEdge, kFootLeft), 0.0));
		RB_CHECK(Ws->Balls[0].Context.Pocket == rb::PocketId::FootLeft);
		if (Fast != 0)
		{
			RB_CHECK(Speed * Speed >= kG * (kR + G.DropRadius));
			continue;
		}
		const rb::PivotPath& Pivot = Ws->Balls[0].Pivot;
		RB_CHECK(Pivot.Pocket == rb::PocketId::FootLeft);
		RB_CHECK(!Pivot.Result.Immediate);
		RB_CHECK_NEAR(Pivot.NormalSpeed0, Speed, 1e-12);
		RB_CHECK_NEAR(Pivot.Rho, kR + G.DropRadius, 1e-15);
		const rb::PivotResult Expected = rb::ComputePivot(Speed, kR + G.DropRadius, 0.4, kG, S->Input.Params.Numerics);
		RB_CHECK_NEAR(Pivot.Result.Duration, Expected.Duration, 1e-12);
		RB_CHECK_NEAR(Pivot.Result.LeaveAngle, Expected.LeaveAngle, 1e-12);
	}
}

RB_TEST(PocketFlow_HooksLeaveForeignEventsToTheLoop)
{
	// ProcessPocketEvent / ProcessRailTopEvent consume only their own kinds and states (return false otherwise).
	std::unique_ptr<Scene> S = AtTheDropEdge(0.2);
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	RB_CHECK(!rb::sim::ProcessPocketEvent(*Ws, 0, Ref(rb::TableFeatureKind::NoseSegment, 0), 0.0));
	RB_CHECK(!rb::sim::ProcessPocketEvent(*Ws, 0, Ref(rb::TableFeatureKind::PocketExit, kFootLeft), 0.0)); // not in a pocket
	RB_CHECK(!rb::sim::ProcessPocketEvent(*Ws, 0, Ref(rb::TableFeatureKind::JawArc, 2 * kFootLeft), 0.0)); // not pivoting
	RB_CHECK(!rb::sim::ProcessPocketEvent(*Ws, 0, Ref(rb::TableFeatureKind::None, kFootLeft), 0.0));     // no pivot to end
	RB_CHECK(!rb::sim::ProcessRailTopEvent(*Ws, 0, Ref(rb::TableFeatureKind::DropEdge, kFootLeft), 0.0));
	RB_CHECK(!rb::sim::ProcessRailTopEvent(*Ws, 0, Ref(rb::TableFeatureKind::SupportExit, 0, 0), 0.0));  // not on the cap
	RB_CHECK(!Ws->Island.Active);
}

RB_TEST(PocketFlow_LandingIntoTheHoleAndOntoTheSlate)
{
	// RouteLanding at the end of a flight (collisions 6.1): over the capture circle the ball continues in the pocket (context
	// set, no bounce counted); over the table it bounces (bounce index, max height of the sequence kept for BallLand).
	for (int OverHole = 0; OverHole < 2; ++OverHole)
	{
		std::unique_ptr<Scene> S = MakeScene();
		const rb::PocketGeometry& G = S->Table.Pockets[kFootLeft];
		const Vec2 Where = OverHole != 0 ? G.CaptureCenter : Vec2{0.2, 0.1};
		Place(*S, 0, rb::ToVec3(Where, kR + 0.05), {0.1, 0.0, 0.0});
		std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
		rb::sim::BallSlot& B = Ws->Balls[0];
		RB_REQUIRE(B.Seg.State == rb::MotionState::Airborne && B.Seg.TauEnd < rb::kInfinity);
		rb::sim::RouteLanding(*Ws, 0, B.Seg.T0 + B.Seg.TauEnd);
		if (OverHole != 0)
		{
			RB_CHECK(B.Context.Pocket == rb::PocketId::FootLeft);
			RB_CHECK(B.BounceIndex == 0);
			RB_CHECK(S->Result.Diagnostics.MissedEvents == 0);
			continue;
		}
		// v_z = sqrt(2 g 0.05) = 0.99 m/s: e_slate 0.6 -> 0.59 m/s, above v_z_min: still airborne, bounce 1 of the sequence.
		RB_CHECK(B.Context.Pocket == rb::PocketId::None);
		RB_CHECK(B.BounceIndex == 1);
		const double Rebound = 0.6 * rb::Sqrt(2.0 * kG * 0.05);
		RB_CHECK_NEAR(B.SequenceMaxZ, rb::Max(kR + 0.05, kR + Rebound * Rebound / (2.0 * kG)), 1e-9);
	}
}

RB_TEST(PocketFlow_CapToSlopeStartsARigidRailTopIsland)
{
	// A ball rolling on the flat cap across the cushion-back ridge (SupportExit, CushionBack) continues in a rigid island with the
	// rail-top features (collisions 6.2, architecture 8.9).
	std::unique_ptr<Scene> S = MakeScene();
	const rb::TableGeometry& T = S->Table;
	const double Ridge = T.HalfWidth + T.Spec.CushionWidth;
	Place(*S, 0, {0.0, 0.0, kR});
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	const Vec2 P{0.3, Ridge};
	const int Cap = rb::sim::FindRailTopPolygon(T, P + Vec2{0.0, 1e-6}, false, rb::RailTopKind::RailCap, Vec2{}, 0.0);
	RB_REQUIRE(Cap >= 0);
	int RidgeEdge = -1;
	const rb::RailTopPolygon& Poly = T.RailTops[Cap];
	for (int e = 0; e < Poly.VertexCount; ++e)
	{
		RidgeEdge = Poly.Edges[e] == rb::RailEdgeKind::CushionBack ? e : RidgeEdge;
	}
	RB_REQUIRE(RidgeEdge >= 0);
	rb::BallState OnCap;
	OnCap.Position = rb::ToVec3(P, T.Spec.RailTopZ + kR);
	OnCap.Velocity = {0.0, -0.2, 0.0};
	OnCap.Omega = rb::RollingOmegaH(OnCap.Velocity, kR);
	OnCap.State = rb::MotionState::Rolling;
	rb::sim::BallSlot& B = Ws->Balls[0];
	B.Context.Support = rb::SupportKind::RailCap;
	B.Context.SupportPolygon = static_cast<std::uint8_t>(Cap);
	B.Seg = rb::MakeSegment(OnCap, 1.0, S->Input.Balls[0].Spec, rb::RailCapSurface(S->Input.Params.PocketContacts), T.Spec.RailTopZ, kG);
	RB_REQUIRE(rb::sim::ProcessRailTopEvent(*Ws, 0, Ref(rb::TableFeatureKind::SupportExit, Cap, RidgeEdge), 1.0));
	RB_REQUIRE(Ws->Island.Active);
	RB_CHECK(Ws->Island.Solver.Mode() == rb::CliMode::Rigid);
	RB_CHECK(B.InIsland);
	RB_CHECK(!Ws->Island.Solver.Body(0).ClothSupport);
	int Planes = 0;
	for (int f = 0; f < Ws->Island.Solver.FeatureCount(); ++f)
	{
		Planes += Ws->Island.Solver.Feature(f).Kind == rb::IslandFeatureKind::Plane ? 1 : 0;
	}
	RB_CHECK(Planes >= 2); // the cap and the sloped cushion top
}
