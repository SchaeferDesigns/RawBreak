// WP-6b: bounded island features from the table geometry (architecture 8.8, review item 26) and the geometry helpers of the
// landing / rail-top routing. Standalone (WP-2 geometry and WP-5 queries are merged).
#include "rbtest.h"

#include "SimWorkspaceTestUtil.h"

#include "rb/Physics/Detect.h"

using namespace isltest;
using rb::TableFeatureKind;
using rb::TableFeatureRef;

namespace
{
	TableFeatureRef Ref(TableFeatureKind Kind, int Index, int Sub = 0)
	{
		TableFeatureRef R;
		R.Kind = Kind;
		R.Index = static_cast<std::uint8_t>(Index);
		R.SubIndex = static_cast<std::uint8_t>(Sub);
		return R;
	}

	double Rc(const rb::TableGeometry& T) { return rb::ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset; }

	bool InsidePolygon(const rb::RailTopPolygon& Poly, const Vec2& P, double Slack)
	{
		for (int i = 0; i < Poly.VertexCount; ++i)
		{
			const Vec2 A = Poly.Vertices[i];
			const Vec2 E = Poly.Vertices[(i + 1) % Poly.VertexCount] - A;
			if (rb::Cross(E, P - A) < -Slack * rb::Length(E))
			{
				return false;
			}
		}
		return true;
	}
}

RB_TEST(Islands_FeaturesMatchTheEventModeContactDistances)
{
	// A ball on the cloth exactly at the event-mode contact distance of a nose (R_c), a jaw arc (r_j + R_c from the arc center,
	// on the exposed arc) and a facing face (s_f from the plan line) has an island gap of 0: the island and the detector agree on
	// where a contact starts, so a member cannot tunnel between the two modes.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = S->Table;
	const rb::PhysicsParams& P = S->Input.Params;
	rb::IslandFeature F[rb::sim::kMaxFeaturePieces];

	// Nose C3 (y = +W/2), inward normal -y.
	const int C3 = static_cast<int>(rb::CushionId::LeftFoot);
	RB_REQUIRE(rb::sim::MakeIslandFeatures(Ref(TableFeatureKind::NoseSegment, C3), T, P, F) == 1);
	RB_CHECK(F[0].Kind == rb::IslandFeatureKind::EdgeLine);
	RB_CHECK(F[0].RailFeature == rb::RailFeatureOfCushion(rb::CushionId::LeftFoot));
	RB_CHECK(F[0].Friction == P.Cushion.Friction);
	RB_CHECK(F[0].Restitution.Max == P.Cushion.Restitution.Max && F[0].Restitution.Slope == P.Cushion.Restitution.Slope);
	const rb::NoseSegment& Nose = T.Noses[C3];
	const Vec2 Mid = (Nose.Start + Nose.End) * 0.5;
	const Vec2 OnCloth = Mid + Nose.InwardNormal * Rc(T);
	RB_CHECK_NEAR(rb::sim::IslandFeatureGap(F[0], rb::ToVec3(OnCloth, kR), kR), 0.0, 1e-12);
	RB_CHECK(rb::sim::IslandFeatureGap(F[0], rb::ToVec3(OnCloth + Nose.InwardNormal * 1e-3, kR), kR) > 5e-4);

	// Jaw arc of FOOT_LEFT, outgoing side: the middle of the exposed arc.
	const int JawIndex = 2 * kFootLeft + 1;
	RB_REQUIRE(rb::sim::MakeIslandFeatures(Ref(TableFeatureKind::JawArc, JawIndex), T, P, F) == 1);
	const rb::JawArc& Arc = T.JawArcs[JawIndex];
	RB_CHECK(F[0].Kind == rb::IslandFeatureKind::JawCircle);
	RB_CHECK(F[0].RailFeature == rb::RailFeatureOfJaw(rb::PocketId::FootLeft, rb::JawSide::Outgoing));
	const double MidAngle = Arc.AngleFrom + 0.5 * Arc.AngleSweep;
	const Vec2 AtJaw = Arc.Center + Vec2{rb::Cos(MidAngle), rb::Sin(MidAngle)} * (Arc.Radius + Rc(T));
	RB_CHECK_NEAR(rb::sim::IslandFeatureGap(F[0], rb::ToVec3(AtJaw, kR), kR), 0.0, 1e-12);
	// Outside the exposed arc the jaw circle is no contact (the nose / facing take over).
	const double Behind = Arc.AngleFrom - 0.5 * (rb::kTwoPi - Arc.AngleSweep);
	RB_CHECK(rb::sim::IslandFeatureGap(F[0], rb::ToVec3(Arc.Center + Vec2{rb::Cos(Behind), rb::Sin(Behind)} * (Arc.Radius + Rc(T)), kR), kR) == rb::kInfinity);

	// Facing face (undercut plane): a ball on the shelf at s_f from the plan line, halfway along the facing.
	RB_REQUIRE(rb::sim::MakeIslandFeatures(Ref(TableFeatureKind::FacingFace, JawIndex), T, P, F) == 1);
	const rb::Facing& Face = T.Facings[JawIndex];
	RB_CHECK(F[0].Kind == rb::IslandFeatureKind::FacingPlane);
	RB_CHECK(F[0].RestitutionScale == P.Cushion.FacingRestitutionScale && F[0].Friction == P.Cushion.FacingFriction);
	const double Sf = rb::FacingContactOffset(kR, Face.TopHeight, Face.Backdraft);
	const Vec2 OnShelf = Face.Start + Face.Direction * (0.3 * Face.Length) + Face.PocketNormal * Sf;
	RB_CHECK_NEAR(rb::sim::IslandFeatureGap(F[0], rb::ToVec3(OnShelf, kR), kR), 0.0, 1e-12);
	// The face normal points into the pocket and down by beta_v (the detector's contact frame).
	const rb::BallState OnShelfState{rb::ToVec3(OnShelf, kR), {}, {}, rb::MotionState::Rolling};
	const rb::FixedContact Contact = rb::MakeFixedContact(Ref(TableFeatureKind::FacingFace, JawIndex), T, OnShelfState, rb::BallSpec{}, rb::DetectOptions{});
	RB_CHECK_NEAR(rb::Length(F[0].Normal - Contact.Normal), 0.0, 1e-12);

	// Facing top edge: a line at h along the facing.
	RB_REQUIRE(rb::sim::MakeIslandFeatures(Ref(TableFeatureKind::FacingTopEdge, JawIndex), T, P, F) == 1);
	RB_CHECK(F[0].Kind == rb::IslandFeatureKind::EdgeLine);
	RB_CHECK_NEAR(F[0].Point.z, Face.TopHeight, 0.0);
	RB_CHECK_NEAR(F[0].Length, Face.Length, 1e-15);

	// Region features and pocket interiors are no island contacts.
	for (TableFeatureKind K : {TableFeatureKind::DropEdge, TableFeatureKind::LinerWall, TableFeatureKind::RimTorus, TableFeatureKind::CaptureDepth,
			 TableFeatureKind::PocketExit, TableFeatureKind::CaptureCircle, TableFeatureKind::OuterBoundary, TableFeatureKind::SupportExit})
	{
		RB_CHECK(rb::sim::MakeIslandFeatures(Ref(K, 0), T, P, F) == 0);
	}
}

RB_TEST(Islands_RailTopFeaturesArePlanesEdgesAndFlatCutRims)
{
	// Every rail-top polygon becomes a bounded Plane (constant e_rt, mu_rt, rail-top rolling); its physical straight edges the
	// RailTopEdgePieces (a second piece carries the piece bit and maps back to the same source); the cut rim of a FLAT polygon is a
	// horizontal circle whose exposed arcs lie on the polygon; a sloped top's rim is no island feature (members leave over the cut).
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = S->Table;
	const rb::PhysicsParams& P = S->Input.Params;
	rb::IslandFeature F[rb::sim::kMaxFeaturePieces];
	int FlatCuts = 0;
	int SlopedCuts = 0;
	int SplitEdges = 0;
	for (int i = 0; i < T.RailTops.Size(); ++i)
	{
		const rb::RailTopPolygon& Poly = T.RailTops[i];
		RB_REQUIRE(rb::sim::MakeIslandFeatures(Ref(TableFeatureKind::RailTop, i), T, P, F) == 1);
		RB_CHECK(F[0].Kind == rb::IslandFeatureKind::Plane);
		RB_CHECK(F[0].VertexCount == Poly.VertexCount && F[0].HasCut == Poly.HasCut);
		RB_CHECK(F[0].Restitution.Max == P.PocketContacts.RailTopRestitution && F[0].Restitution.Min == P.PocketContacts.RailTopRestitution);
		RB_CHECK(F[0].Friction == P.PocketContacts.RailTopFriction && F[0].RollingResistance == P.PocketContacts.RailTopRollingResistance);
		// A ball 1 mm above the plane over the polygon's centroid (outside the cut): gap 1 mm.
		Vec2 Centroid;
		for (int v = 0; v < Poly.VertexCount; ++v)
		{
			Centroid += Poly.Vertices[v] * (1.0 / Poly.VertexCount);
		}
		if (!Poly.HasCut || rb::Length(Centroid - Poly.CutCenter) > Poly.CutRadius + kR)
		{
			const Vec3 OnPlane = rb::ToVec3(Centroid, rb::sim::RailTopHeightAt(Poly, Centroid));
			RB_CHECK_NEAR(rb::sim::IslandFeatureGap(F[0], OnPlane + Poly.PlaneNormal * (kR + 1e-3), kR), 1e-3, 1e-12);
		}
		for (int e = 0; e < Poly.VertexCount; ++e)
		{
			Vec3 From[2];
			Vec3 To[2];
			const int Pieces = rb::RailTopEdgePieces(T, i, e, From, To);
			const int N = rb::sim::MakeIslandFeatures(Ref(TableFeatureKind::RailTopEdge, i, e), T, P, F);
			RB_CHECK(N == Pieces);
			for (int k = 0; k < N; ++k)
			{
				RB_CHECK(F[k].Kind == rb::IslandFeatureKind::EdgeLine);
				RB_CHECK_NEAR(rb::Length(F[k].Point - From[k]), 0.0, 0.0);
				RB_CHECK_NEAR(F[k].Length, rb::Length(To[k] - From[k]), 1e-15);
				const TableFeatureRef Back = rb::sim::SourceOf(F[k]);
				RB_CHECK(Back.Kind == TableFeatureKind::RailTopEdge && Back.Index == i && Back.SubIndex == e);
			}
			SplitEdges += N == 2 ? 1 : 0;
			if (N == 2)
			{
				RB_CHECK(F[0].SourceSub != F[1].SourceSub); // distinct sources: AddFeature de-duplicates by source
			}
		}
		const int Rim = rb::sim::MakeIslandFeatures(Ref(TableFeatureKind::RailTopEdge, i, rb::kCutRimEdge), T, P, F);
		const bool Flat = Poly.PlaneNormal.x == 0.0 && Poly.PlaneNormal.y == 0.0;
		if (!Poly.HasCut || !Flat)
		{
			RB_CHECK(Rim == 0);
			SlopedCuts += Poly.HasCut ? 1 : 0;
			continue;
		}
		++FlatCuts;
		RB_CHECK(Rim >= 1);
		for (int k = 0; k < Rim; ++k)
		{
			RB_CHECK(F[k].Kind == rb::IslandFeatureKind::JawCircle);
			RB_CHECK_NEAR(F[k].Radius, Poly.CutRadius, 0.0);
			RB_CHECK_NEAR(F[k].Point.z, Poly.PlanePoint.z, 0.0);
			RB_CHECK(F[k].AngleSweep > 0.0);
			const TableFeatureRef Back = rb::sim::SourceOf(F[k]);
			RB_CHECK(Back.Kind == TableFeatureKind::RailTopEdge && Back.Index == i && Back.SubIndex == rb::kCutRimEdge);
			// The exposed arc lies on the polygon (sampled, incl. both ends within rounding).
			for (int s = 0; s <= 8; ++s)
			{
				const double A = F[k].AngleFrom + F[k].AngleSweep * s / 8.0;
				RB_CHECK(InsidePolygon(Poly, Poly.CutCenter + Vec2{rb::Cos(A), rb::Sin(A)} * Poly.CutRadius, 1e-9));
			}
			// Just outside the arc's ends the rim leaves the polygon (the arcs are maximal).
			if (F[k].AngleSweep < rb::kTwoPi - 1e-6)
			{
				const double Out = F[k].AngleFrom - 1e-4;
				RB_CHECK(!InsidePolygon(Poly, Poly.CutCenter + Vec2{rb::Cos(Out), rb::Sin(Out)} * Poly.CutRadius, 0.0));
			}
		}
	}
	RB_CHECK(FlatCuts > 0);
	std::printf("  rail tops: %d polygons, %d flat cut rims, %d sloped cuts, %d split edges\n", T.RailTops.Size(), FlatCuts, SlopedCuts, SplitEdges);
}

