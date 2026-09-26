// Owner: WP-5 (event detection). The table dispatcher PredictTableEvent (architecture 8.2), island feature queries
// QueryTableFeatures (8.8) and contact frames MakeFixedContact (collisions 4.1, 4.6, 5.3, 6.2). Standalone tests use
// hand-built geometry; tests that need WP-2's implementation (ComputeCushionContact, FacingContactOffset,
// BuildTableGeometry) carry the Integ_ prefix (architecture 18).

#include "rbtest.h"

#include "DetectTestUtil.h"

#include "rb/Equipment/TableSpec.h"

using namespace detecttest;
using rb::ContactPrediction;
using rb::FeaturePrediction;
using rb::MotionSegment;
using rb::TableFeatureKind;

namespace
{
	constexpr int kFootLeft = static_cast<int>(rb::PocketId::FootLeft);

	// Hand-built table: every container full (index = id) with far-away dummies, the RAIL_LEFT nose (C3, twice: C3 and C4
	// identical, for the tie-break), the FOOT_LEFT pocket with its jaw arcs / facings (index 6, 7) and a corner surround.
	rb::TableGeometry HandTable()
	{
		rb::TableGeometry T;
		for (int i = 0; i < rb::kCushionCount; ++i)
		{
			rb::NoseSegment N = MakeNose({1.1, 0.635}, {0.1, 0.635}, {0.0, -1.0});
			N.Cushion = static_cast<rb::CushionId>(i);
			N.Present = i == 3 || i == 4;
			T.Noses.PushBack(N);
		}
		for (int i = 0; i < 2 * rb::kPocketCount; ++i)
		{
			rb::JawArc J = MakeJaw({-20.0 - i, -20.0}, 0.004, -rb::kPi, rb::kTwoPi);
			rb::Facing F = MakeFacing({-20.0 - i, -21.0}, {-20.0 - i, -21.05}, {1.0, 0.0});
			J.Pocket = F.Pocket = static_cast<rb::PocketId>(i / 2);
			J.Side = F.Side = static_cast<rb::JawSide>(i % 2);
			if (i / 2 == kFootLeft)
			{
				J.Center = i % 2 == 0 ? Vec2{1.19, 0.64} : Vec2{1.275, 0.555};
				F.Start = J.Center;
				F.End = J.Center + Vec2{0.06, 0.05};
				F.Direction = rb::Normalized(F.End - F.Start);
				F.Length = rb::Length(F.End - F.Start);
				F.PocketNormal = i % 2 == 0 ? rb::PerpCcw(F.Direction) * -1.0 : rb::PerpCcw(F.Direction);
			}
			T.JawArcs.PushBack(J);
			T.Facings.PushBack(F);
		}
		for (int i = 0; i < rb::kPocketCount; ++i)
		{
			rb::PocketGeometry P = CornerPocket();
			P.Id = static_cast<rb::PocketId>(i);
			if (i != kFootLeft)
			{
				P.CaptureCenter = {-10.0 - i, -10.0};
			}
			T.Pockets.PushBack(P);
		}
		rb::RailTopPolygon S;
		S.Kind = rb::RailTopKind::RailCap;
		S.PlanePoint = {0.0, 0.0, 0.048};
		S.PlaneNormal = {0.0, 0.0, 1.0};
		const Vec2 V[4] = {{1.25, 0.62}, {1.40, 0.62}, {1.40, 0.77}, {1.25, 0.77}};
		for (const Vec2& X : V)
		{
			S.Vertices[S.VertexCount++] = X;
		}
		S.Edges[0] = rb::RailEdgeKind::Seam;
		S.Edges[1] = rb::RailEdgeKind::OuterEdge;
		S.Edges[2] = rb::RailEdgeKind::OuterEdge;
		S.Edges[3] = rb::RailEdgeKind::Seam;
		S.HasCut = true;
		S.CutCenter = {1.302615, 0.667615};
		S.CutRadius = 0.062;
		T.RailTops.PushBack(S);
		T.OuterBoundary = {{-1.4478, -0.8128}, {1.4478, 0.8128}};
		T.PlayingArea = {{-1.27, -0.635}, {1.27, 0.635}};
		return T;
	}

	FeaturePrediction Dispatch(const MotionSegment& S, const rb::TableGeometry& T, const rb::BallTableContext& C = {},
		const rb::EnvironmentSpec& E = rb::EnvironmentSpec{})
	{
		return rb::PredictTableEvent(S, rb::BallSpec{}, C, T, E, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
	}

	rb::BallState StateAt(const Vec3& P, rb::MotionState S)
	{
		rb::BallState B;
		B.Position = P;
		B.State = S;
		return B;
	}

	bool KeyLessOrEqual(const rb::TableFeatureRef& A, const rb::TableFeatureRef& B)
	{
		if (A.Kind != B.Kind)
		{
			return A.Kind < B.Kind;
		}
		if (A.Index != B.Index)
		{
			return A.Index < B.Index;
		}
		return A.SubIndex <= B.SubIndex;
	}
}

RB_TEST(Detect_TableEventStateFilteringAndTieBreak)
{
	const rb::TableGeometry T = HandTable();
	// No translation, or terminal: nothing to predict even right next to features.
	RB_CHECK(!Dispatch(Stationary({0.5, 0.635 - 0.0275, kR}), T).Contact.Found);
	MotionSegment Spin = Stationary({0.5, 0.635 - 0.0275, kR});
	Spin.State = rb::MotionState::Spinning;
	RB_CHECK(!Dispatch(Spin, T).Contact.Found);
	MotionSegment Gone = Rolling({0.5, 0.3, kR}, {0.0, 1.0, 0.0});
	Gone.State = rb::MotionState::Pocketed;
	RB_CHECK(!Dispatch(Gone, T).Contact.Found);

	// Airborne into RAIL_LEFT at nose height: noses C3 and C4 are identical -> the tie goes to the lower index (C3).
	MotionSegment Fly = Airborne({0.5, 0.45, kNoseH + 0.012}, {0.0, 2.0, 0.3});
	const FeaturePrediction D = Dispatch(Fly, T);
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == TableFeatureKind::NoseSegment);
	RB_CHECK(D.Feature.Index == 3);
	RB_CHECK(D.Contact.Time == rb::PredictNoseAirborne(Fly, kR, T.Noses[3], 0.0, rb::kInfinity, Numerics()).Time);

	// TimeLimit before the contact: nothing (the landing belongs to the end slot, never to the table slot).
	RB_CHECK(!rb::PredictTableEvent(Fly, rb::BallSpec{}, {}, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, D.Contact.Time - 1e-6, Numerics())
				  .Contact.Found);
	MotionSegment Hop = Airborne({0.0, 0.0, kR}, {0.5, 0.0, 1.0});
	RB_CHECK(!Dispatch(Hop, T).Contact.Found);

	// Over the lamp: LampApex.
	rb::EnvironmentSpec Bar;
	Bar.LampUndersideZ = 0.84;
	const FeaturePrediction Lamp = Dispatch(Airborne({0.0, 0.0, kR}, {0.0, 0.0, 4.5}), T, {}, Bar);
	RB_REQUIRE(Lamp.Contact.Found);
	RB_CHECK(Lamp.Feature.Kind == TableFeatureKind::LampApex);

	// Off the table over the rail with no rail top in the way: OuterBoundary.
	const FeaturePrediction Off = Dispatch(Airborne({-1.3, 0.0, 0.2}, {-2.0, 0.0, 0.5}), T);
	RB_REQUIRE(Off.Contact.Found);
	RB_CHECK(Off.Feature.Kind == TableFeatureKind::OuterBoundary);
}

RB_TEST(Detect_TableEventPocketPivotAndPocketFallUseTheirPocket)
{
	const rb::TableGeometry T = HandTable();
	rb::BallTableContext InPocket;
	InPocket.Pocket = rb::PocketId::FootLeft;

	// Pivot proxy heading into jaw 6 (sphere approximation of the rounded point).
	MotionSegment Proxy = Stationary({1.19 - 0.1, 0.64, kNoseH});
	Proxy.State = rb::MotionState::PocketPivot;
	Proxy.Vel0 = {1.0, 0.0, 0.0};
	Proxy.TauEnd = 0.2;
	const FeaturePrediction J = Dispatch(Proxy, T, InPocket);
	RB_REQUIRE(J.Contact.Found);
	RB_CHECK(J.Feature.Kind == TableFeatureKind::JawArc);
	RB_CHECK(J.Feature.Index == 2 * kFootLeft);
	RB_CHECK_NEAR(J.Contact.Time, 0.1 - (kR + 0.004), 1e-12);
	// Without a pocket context nothing is predicted for pivot / fall states.
	RB_CHECK(!Dispatch(Proxy, T).Contact.Found);

	// PocketFall straight down in the hole: capture depth of its pocket.
	const FeaturePrediction C = Dispatch(PocketFall({1.302615, 0.667615, 0.0}, {}), T, InPocket);
	RB_REQUIRE(C.Contact.Found);
	RB_CHECK(C.Feature.Kind == TableFeatureKind::CaptureDepth);
	RB_CHECK(C.Feature.Index == kFootLeft);
}

RB_TEST(Detect_QueryTableFeaturesOrderAndOverflow)
{
	const rb::TableGeometry T = HandTable();
	const rb::Aabb3 Corner{{1.10, 0.50, -0.05}, {1.45, 0.80, 0.10}};
	rb::TableFeatureRef Out[64];
	bool Overflow = true;
	const int N = rb::QueryTableFeatures(Corner, T, rb::DetectOptions{}, Out, 64, Overflow);
	RB_CHECK(!Overflow);
	RB_REQUIRE(N > 0);
	bool Sorted = true;
	bool HasNose3 = false, HasJaw6 = false, HasJaw7 = false, HasFacing6 = false, HasDrop = false, HasTop = false, HasCutRim = false, HasFar = false;
	for (int i = 0; i < N; ++i)
	{
		if (i > 0 && !(KeyLessOrEqual(Out[i - 1], Out[i]) && !(Out[i - 1].Kind == Out[i].Kind && Out[i - 1].Index == Out[i].Index && Out[i - 1].SubIndex == Out[i].SubIndex)))
		{
			Sorted = false;
		}
		HasNose3 |= Out[i].Kind == TableFeatureKind::NoseSegment && Out[i].Index == 3;
		HasJaw6 |= Out[i].Kind == TableFeatureKind::JawArc && Out[i].Index == 6;
		HasJaw7 |= Out[i].Kind == TableFeatureKind::JawArc && Out[i].Index == 7;
		HasFacing6 |= Out[i].Kind == TableFeatureKind::FacingFace && Out[i].Index == 6;
		HasDrop |= Out[i].Kind == TableFeatureKind::DropEdge && Out[i].Index == kFootLeft;
		HasTop |= Out[i].Kind == TableFeatureKind::RailTop && Out[i].Index == 0;
		HasCutRim |= Out[i].Kind == TableFeatureKind::RailTopEdge && Out[i].Index == 0 && Out[i].SubIndex == rb::kCutRimEdge;
		HasFar |= Out[i].Kind == TableFeatureKind::JawArc && Out[i].Index < 6;
	}
	RB_CHECK(Sorted);
	RB_CHECK(HasNose3 && HasJaw6 && HasJaw7 && HasFacing6 && HasDrop && HasTop && HasCutRim);
	RB_CHECK(!HasFar);
	// Capacity: the first entries in order, the rest flagged.
	rb::TableFeatureRef Few[3];
	RB_CHECK(rb::QueryTableFeatures(Corner, T, rb::DetectOptions{}, Few, 3, Overflow) == 3);
	RB_CHECK(Overflow);
	RB_CHECK(Few[0].Kind == Out[0].Kind && Few[2].Index == Out[2].Index);
	// An empty region far from everything.
	RB_CHECK(rb::QueryTableFeatures({{0.0, 0.0, 0.0}, {0.01, 0.01, 0.01}}, T, rb::DetectOptions{}, Out, 64, Overflow) == 0 && !Overflow);
}

RB_TEST(Detect_MakeFixedContactFramesForGriContacts)
{
	const rb::TableGeometry T = HandTable();
	const rb::BallSpec Spec{};

	// G-1: airborne, center 10 mm above the nose -> k_hat = (0, -0.936766, 0.349956) (RAIL_LEFT: local = world axes).
	const Vec3 C1{0.5, 0.635 - std::sqrt(kR * kR - 0.01 * 0.01), kNoseH + 0.01};
	const rb::FixedContact N = rb::MakeFixedContact({TableFeatureKind::NoseSegment, 3, 0}, T, StateAt(C1, rb::MotionState::Airborne), Spec, {});
	RB_CHECK(N.Kind == rb::FixedContactKind::NoseEdge);
	RB_CHECK(!N.BallOnCloth);
	RB_CHECK(N.RailFeature == 3);
	RB_CHECK_NEAR(N.Normal.x, 0.0, 1e-12);
	RB_CHECK_NEAR(N.Normal.y, -0.936766, 1e-6);
	RB_CHECK_NEAR(N.Normal.z, 0.349956, 1e-6);
	RB_CHECK_NEAR(N.IntoFeature.y, 1.0, 1e-12);

	// Facing face: PocketNormal tilted down by the back draft; on the shelf it goes to Mathavan with theta = beta_v.
	const rb::Facing& F = T.Facings[6];
	const rb::FixedContact Face = rb::MakeFixedContact({TableFeatureKind::FacingFace, 6, 0}, T, StateAt({1.2, 0.6, kR}, rb::MotionState::Rolling), Spec, {});
	RB_CHECK(Face.Kind == rb::FixedContactKind::FacingFace && Face.BallOnCloth);
	RB_CHECK(Face.Pocket == rb::PocketId::FootLeft && Face.RailFeature == rb::RailFeatureOfJaw(rb::PocketId::FootLeft, rb::JawSide::Incoming));
	RB_CHECK_NEAR(Face.Elevation, F.Backdraft, 1e-15);
	RB_CHECK_NEAR(Face.Normal.z, -std::sin(F.Backdraft), 1e-15);
	RB_CHECK_NEAR(rb::Dot(rb::XY(Face.Normal), F.PocketNormal), std::cos(F.Backdraft), 1e-15);

	// Liner: horizontal toward the axis, tilted down by beta_l (the undercut wall deflects down).
	const rb::PocketGeometry& P = T.Pockets[kFootLeft];
	const Vec3 AtWall = rb::ToVec3(P.CaptureCenter + Vec2{0.0, P.CaptureRadius - kR}, -0.03);
	const rb::FixedContact L = rb::MakeFixedContact({TableFeatureKind::LinerWall, static_cast<std::uint8_t>(kFootLeft), 0}, T,
		StateAt(AtWall, rb::MotionState::PocketFall), Spec, {});
	RB_CHECK(L.Kind == rb::FixedContactKind::Liner && L.Pocket == rb::PocketId::FootLeft);
	RB_CHECK_NEAR(L.Normal.y, -std::cos(P.LinerUndercut), 1e-12);
	RB_CHECK_NEAR(L.Normal.z, -std::sin(P.LinerUndercut), 1e-12);

	// Rim torus: from the core circle point (a_d, -r_d) to the center.
	const Vec3 AtRim = rb::ToVec3(P.CaptureCenter + Vec2{0.0, P.DropEdgeRadius - 0.01}, -P.DropRadius + std::sqrt(rb::Square(kR + P.DropRadius) - 1e-4));
	const rb::FixedContact Rim = rb::MakeFixedContact({TableFeatureKind::RimTorus, static_cast<std::uint8_t>(kFootLeft), 0}, T,
		StateAt(AtRim, rb::MotionState::PocketFall), Spec, {});
	RB_CHECK(Rim.Kind == rb::FixedContactKind::RimTorus);
	RB_CHECK_NEAR(Rim.Normal.y, -0.01 / (kR + P.DropRadius), 1e-12);
	RB_CHECK(Rim.Normal.z > 0.0);

	// Rail-top plane and the cut rim.
	const rb::FixedContact Top = rb::MakeFixedContact({TableFeatureKind::RailTop, 0, 0}, T, StateAt({1.39, 0.75, 0.048 + kR}, rb::MotionState::Airborne), Spec, {});
	RB_CHECK(Top.Kind == rb::FixedContactKind::RailTop && Top.Normal == Vec3::UnitZ());
	const Vec2 Dir{std::sqrt(0.5), std::sqrt(0.5)};
	const Vec2 H = T.RailTops[0].CutCenter + Dir * (0.062 - 0.01);
	const rb::FixedContact Cut = rb::MakeFixedContact({TableFeatureKind::RailTopEdge, 0, static_cast<std::uint8_t>(rb::kCutRimEdge)}, T,
		StateAt(rb::ToVec3(H, 0.048 + std::sqrt(kR * kR - 1e-4)), rb::MotionState::Airborne), Spec, {});
	RB_CHECK(Cut.Kind == rb::FixedContactKind::RailTopEdge);
	RB_CHECK_NEAR(rb::Dot(rb::XY(Cut.Normal), Dir), -0.01 / kR, 1e-12);

	// Region events are not contacts: slate parity frame, pocket ids carried.
	RB_CHECK(rb::MakeFixedContact({TableFeatureKind::SlateLanding, 0, 0}, T, StateAt({}, rb::MotionState::Airborne), Spec, {}).Kind == rb::FixedContactKind::Slate);
	RB_CHECK(rb::MakeFixedContact({TableFeatureKind::DropEdge, static_cast<std::uint8_t>(kFootLeft), 0}, T, StateAt({}, rb::MotionState::Rolling), Spec, {}).Pocket ==
		rb::PocketId::FootLeft);
}

// -------------------------------------------------------------------------------------------------
// Integration (WP-2: ComputeCushionContact, FacingContactOffset, BuildTableGeometry)
// -------------------------------------------------------------------------------------------------

RB_TEST(Integ_Detect_TableEventOnClothNoseUsesTheCushionContactOffset)
{
	// D-8 through the dispatcher: the contact distance R_c comes from WP-2's ComputeCushionContact.
	const rb::TableGeometry T = HandTable();
	const MotionSegment S = Rolling({0.5, 0.0, kR}, {0.0, 1.0, 0.0});
	const FeaturePrediction D = Dispatch(S, T);
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == TableFeatureKind::NoseSegment && D.Feature.Index == 3);
	RB_CHECK_NEAR(D.Contact.Time, 0.6267471125, 1e-9);

	const rb::FixedContact N = rb::MakeFixedContact(D.Feature, T, StateAt(rb::PositionAt(S, D.Contact.Time), rb::MotionState::Rolling), rb::BallSpec{}, {});
	RB_CHECK(N.BallOnCloth);
	RB_CHECK_NEAR(N.Elevation, 0.2733943, 1e-6); // theta_c = 15.664 deg
	RB_CHECK_NEAR(N.Normal.y, -std::cos(0.2733943), 1e-6);
	RB_CHECK_NEAR(N.Normal.z, -std::sin(0.2733943), 1e-6);
}

RB_TEST(Integ_Detect_NineFootProCornerApproachReachesTheDropEdgeFirst)
{
	// P-3 approach on the built 9FT_PRO table: rolling at 1 m/s along the FOOT_LEFT pocket axis from 0.3 m out, no facing,
	// jaw or nose event comes before BallPocketEnter at rho = a_d.
	rb::TableGeometry T;
	RB_REQUIRE(rb::BuildTableGeometry(rb::kTableNineFootPro, T) == rb::ErrorCode::Ok);
	RB_REQUIRE(T.Pockets.Size() == rb::kPocketCount);
	const rb::PocketGeometry& P = T.Pockets[kFootLeft];
	const Vec3 Axis = rb::ToVec3(P.Axis);
	const MotionSegment S = Rolling(rb::ToVec3(P.CaptureCenter, kR) - Axis * 0.3, Axis);
	const FeaturePrediction D = Dispatch(S, T);
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == TableFeatureKind::DropEdge);
	RB_CHECK(D.Feature.Index == kFootLeft);
	RB_CHECK_NEAR(rb::Length(rb::XY(rb::PositionAt(S, D.Contact.Time)) - P.CaptureCenter), P.DropEdgeRadius, 1e-12);

	// The island query around that corner finds its jaws and facings.
	rb::TableFeatureRef Out[64];
	bool Overflow = false;
	const rb::Aabb3 Region{rb::ToVec3(P.CaptureCenter, 0.0) - Vec3{0.12, 0.12, 0.0}, rb::ToVec3(P.CaptureCenter, 0.06) + Vec3{0.12, 0.12, 0.0}};
	const int N = rb::QueryTableFeatures(Region, T, rb::DetectOptions{}, Out, 64, Overflow);
	int Jaws = 0;
	for (int i = 0; i < N; ++i)
	{
		Jaws += Out[i].Kind == TableFeatureKind::JawArc && (Out[i].Index == 2 * kFootLeft || Out[i].Index == 2 * kFootLeft + 1) ? 1 : 0;
	}
	RB_CHECK(Jaws == 2);
}
