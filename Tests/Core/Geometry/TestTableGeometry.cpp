// Owner: WP-2 (equipment & table geometry). BuildTableGeometry: equipment 3, 5.3, 12.8, 13 (T-GEOM-1..6,
// T-POCKET-1..9), physics-collisions 5.1-5.3 (P-1), 6.2 and architecture A-GEO-1, A-GEO-2.

#include "rbtest.h"

#include "Geometry/GeometryTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"
#include "rb/Rules/RulesTypes.h"

#include <vector>

using namespace rb;
using namespace rb::geotest;

namespace
{
	TableGeometry Build(const TableSpec& Spec)
	{
		TableGeometry G;
		const ErrorCode Code = BuildTableGeometry(Spec, G);
		RB_CHECK(Code == ErrorCode::Ok);
		return G;
	}

	const PocketGeometry& PocketOf(const TableGeometry& G, PocketId P) { return G.Pockets[static_cast<int>(P)]; }
	const JawArc& ArcOf(const TableGeometry& G, PocketId P, JawSide S) { return G.JawArcs[2 * static_cast<int>(P) + static_cast<int>(S)]; }
	const Facing& FacingOf(const TableGeometry& G, PocketId P, JawSide S) { return G.Facings[2 * static_cast<int>(P) + static_cast<int>(S)]; }

	bool HasSight(const TableGeometry& G, double X, double Y, double Tol)
	{
		for (const Sight& S : G.Sights)
		{
			if (Near(S.Position.x, X, Tol) && Near(S.Position.y, Y, Tol))
			{
				return true;
			}
		}
		return false;
	}

	Aabb2 BoxOf(const Vec2* V, int N, double Margin)
	{
		Aabb2 Box{V[0], V[0]};
		for (int i = 1; i < N; ++i)
		{
			Box.Lo = {Min(Box.Lo.x, V[i].x), Min(Box.Lo.y, V[i].y)};
			Box.Hi = {Max(Box.Hi.x, V[i].x), Max(Box.Hi.y, V[i].y)};
		}
		Box.Lo -= Vec2{Margin, Margin};
		Box.Hi += Vec2{Margin, Margin};
		return Box;
	}

	// Rail-top coverage check of A-GEO-1 (bounding boxes prefilter the convex tests).
	struct Coverage
	{
		static constexpr double Eps = 1e-9;
		const TableGeometry& G;
		Aabb2 PolyBox[kMaxRailTopPolygons];
		Vec2 Opening[kPocketCount][5];
		int OpeningCount[kPocketCount] = {};
		Aabb2 OpeningBox[kPocketCount];

		explicit Coverage(const TableGeometry& Geometry) : G(Geometry)
		{
			for (int i = 0; i < G.RailTops.Size(); ++i)
			{
				PolyBox[i] = BoxOf(G.RailTops[i].Vertices, G.RailTops[i].VertexCount, 2.0 * Eps);
			}
			for (int k = 0; k < G.Pockets.Size(); ++k)
			{
				OpeningCount[k] = PocketOpening(G, k, Opening[k]);
				OpeningBox[k] = BoxOf(Opening[k], OpeningCount[k], 2.0 * Eps);
			}
		}

		// False on a violation: outside the openings and the cut discs the rail top is covered exactly once,
		// inside them never; samples within Eps of any boundary are ambiguous and skipped.
		bool Ok(const Vec2& P) const
		{
			if (Abs(P.x) < G.HalfLength + Eps && Abs(P.y) < G.HalfWidth + Eps)
			{
				return true; // playing area (and its boundary)
			}
			int Strict = 0;
			int Loose = 0;
			for (int i = 0; i < G.RailTops.Size(); ++i)
			{
				if (PolyBox[i].Contains(P))
				{
					Strict += OverSurface(G.RailTops[i], P, Eps) ? 1 : 0;
					Loose += OverSurface(G.RailTops[i], P, -Eps) ? 1 : 0;
				}
			}
			if (Strict != Loose)
			{
				return true;
			}
			bool Excluded = false;
			for (int k = 0; k < G.Pockets.Size(); ++k)
			{
				if (OpeningBox[k].Contains(P))
				{
					const bool In = InConvex(Opening[k], OpeningCount[k], P, Eps);
					if (In != InConvex(Opening[k], OpeningCount[k], P, -Eps))
					{
						return true;
					}
					Excluded = Excluded || In;
				}
				const double Dist = Length(P - G.Pockets[k].CaptureCenter);
				if (Abs(Dist - G.Pockets[k].CaptureRadius) < Eps)
				{
					return true;
				}
				Excluded = Excluded || Dist < G.Pockets[k].CaptureRadius;
			}
			return Strict == (Excluded ? 0 : 1);
		}
	};

	int CoverageViolations(const TableGeometry& G, double X0, double X1, double Y0, double Y1, double Step)
	{
		const Coverage Model(G);
		int Violations = 0;
		for (double X = X0; X <= X1; X += Step)
		{
			for (double Y = Y0; Y <= Y1; Y += Step)
			{
				Violations += Model.Ok({X, Y}) ? 0 : 1;
			}
		}
		return Violations;
	}

	// Structural checks of every rail-top polygon (A-GEO-1): convex CCW, 3..8 vertices, unit plane normal, the
	// plane at the polygon's vertices between h and RailTopZ (cushion top) or at RailTopZ (cap), edge kinds on the
	// right lines.
	void CheckPolygonStructure(const TableGeometry& G)
	{
		const TableSpec& S = G.Spec;
		for (const RailTopPolygon& Poly : G.RailTops)
		{
			RB_CHECK(Poly.VertexCount >= 3 && Poly.VertexCount <= kMaxRailTopVertices);
			RB_CHECK_NEAR(Length(Poly.PlaneNormal), 1.0, 1e-15);
			RB_CHECK(Poly.PlaneNormal.z > 0.0);
			double Area2 = 0.0;
			for (int i = 0; i < Poly.VertexCount; ++i)
			{
				const Vec2 A = Poly.Vertices[i];
				const Vec2 B = Poly.Vertices[(i + 1) % Poly.VertexCount];
				const Vec2 C = Poly.Vertices[(i + 2) % Poly.VertexCount];
				RB_CHECK(Cross(B - A, C - B) >= -1e-15); // convex, CCW (collinear split vertices allowed)
				Area2 += Cross(A, B);
				// Plane height at the vertex.
				const Vec3& N = Poly.PlaneNormal;
				const double Z = Poly.PlanePoint.z - (N.x * (A.x - Poly.PlanePoint.x) + N.y * (A.y - Poly.PlanePoint.y)) / N.z;
				if (Poly.Kind == RailTopKind::RailCap)
				{
					RB_CHECK(Z == S.RailTopZ);
				}
				else
				{
					RB_CHECK(Z >= S.CushionNoseHeight - 1e-12 && Z <= S.RailTopZ + 1e-12);
				}
				const RailEdgeKind Kind = Poly.Edges[i];
				if (Kind == RailEdgeKind::OuterEdge)
				{
					const bool OnX = Near(Abs(A.x), G.OuterBoundary.Hi.x, 1e-12) && Near(Abs(B.x), G.OuterBoundary.Hi.x, 1e-12);
					const bool OnY = Near(Abs(A.y), G.OuterBoundary.Hi.y, 1e-12) && Near(Abs(B.y), G.OuterBoundary.Hi.y, 1e-12);
					RB_CHECK(OnX || OnY);
				}
				if (Kind == RailEdgeKind::Nose)
				{
					const bool OnX = Near(Abs(A.x), G.HalfLength, 1e-12) && Near(Abs(B.x), G.HalfLength, 1e-12);
					const bool OnY = Near(Abs(A.y), G.HalfWidth, 1e-12) && Near(Abs(B.y), G.HalfWidth, 1e-12);
					RB_CHECK(OnX || OnY);
					RB_CHECK(Poly.Kind == RailTopKind::CushionTop);
					RB_CHECK(Z == S.CushionNoseHeight || Near(Z, S.CushionNoseHeight, 1e-15));
				}
				if (Kind == RailEdgeKind::CushionBack)
				{
					const double Back = S.CushionWidth;
					const bool OnX = Near(Abs(A.x), G.HalfLength + Back, 1e-12) && Near(Abs(B.x), G.HalfLength + Back, 1e-12);
					const bool OnY = Near(Abs(A.y), G.HalfWidth + Back, 1e-12) && Near(Abs(B.y), G.HalfWidth + Back, 1e-12);
					RB_CHECK(OnX || OnY);
				}
			}
			RB_CHECK(Area2 > 0.0);
			if (Poly.HasCut)
			{
				const PocketGeometry& P = G.Pockets[static_cast<int>(Poly.Pocket)];
				RB_CHECK(Poly.CutCenter == P.CaptureCenter && Poly.CutRadius == P.CaptureRadius);
			}
		}
	}

	// T-GEOM-6: mirror of a rail-top polygon among the polygons (vertices as a set, every edge with its kind, plane,
	// cut, ids).
	bool HasMirroredPolygon(const TableGeometry& G, const RailTopPolygon& In, double Sx, double Sy)
	{
		constexpr double Tol = 1e-12;
		for (const RailTopPolygon& Poly : G.RailTops)
		{
			if (Poly.VertexCount != In.VertexCount || Poly.Kind != In.Kind || Poly.HasCut != In.HasCut)
			{
				continue;
			}
			if (!Near(Poly.PlaneNormal, Vec3{Sx * In.PlaneNormal.x, Sy * In.PlaneNormal.y, In.PlaneNormal.z}, Tol))
			{
				continue;
			}
			if (Poly.HasCut && (!Near(Poly.CutCenter, Mirror(In.CutCenter, Sx, Sy), Tol) || Poly.CutRadius != In.CutRadius))
			{
				continue;
			}
			bool AllEdges = true;
			for (int i = 0; i < In.VertexCount && AllEdges; ++i)
			{
				const Vec2 A = Mirror(In.Vertices[i], Sx, Sy);
				const Vec2 B = Mirror(In.Vertices[(i + 1) % In.VertexCount], Sx, Sy);
				bool Found = false;
				for (int j = 0; j < Poly.VertexCount && !Found; ++j)
				{
					const Vec2 C = Poly.Vertices[j];
					const Vec2 D = Poly.Vertices[(j + 1) % Poly.VertexCount];
					Found = Poly.Edges[j] == In.Edges[i] && ((Near(A, C, Tol) && Near(B, D, Tol)) || (Near(A, D, Tol) && Near(B, C, Tol)));
				}
				AllEdges = Found;
			}
			if (AllEdges)
			{
				return true;
			}
		}
		return false;
	}

	// Front arc = the capture circle inside the opening (collisions 5.3), sampled at 3600 angles per pocket: every point of
	// the front half of the r_p circle inside the opening is on the arc, and so is every point of the front half of the
	// drop-edge circle a_d inside the opening (for any ball size); no angle at which both circles lie outside the opening
	// is. Returns the number of violating samples (-1 if a pocket has no front sample).
	int FrontArcViolations(const TableGeometry& G)
	{
		int Violations = 0;
		for (int k = 0; k < G.Pockets.Size(); ++k)
		{
			const PocketGeometry& P = G.Pockets[k];
			Vec2 Opening[5];
			const int N = PocketOpening(G, k, Opening);
			int Front = 0;
			for (int s = 0; s < 3600; ++s)
			{
				const double Angle = (static_cast<double>(s) + 0.5) * kTwoPi / 3600.0 - kPi;
				const Vec2 Dir{Cos(Angle), Sin(Angle)};
				const bool OnArc = ArcContainsAngle(P.FrontArcFrom, P.FrontArcSweep, Angle);
				const Vec2 Wall = P.CaptureCenter + Dir * P.CaptureRadius;
				const Vec2 Edge = P.CaptureCenter + Dir * P.DropEdgeRadius;
				// Front half only: behind the capture center a bar corner's shallow hole ends ~4 mm in front of the
				// cushion-back corner, where the vertical rail faces stand just behind the wall (back wall, not front).
				if (Dot(Dir, P.Axis) < 0.0 && InConvex(Opening, N, Wall, 1e-9))
				{
					Violations += OnArc ? 0 : 1;
					++Front;
				}
				// Every drop-edge point a ball can roll onto (in the opening, in front of the capture center) is on the arc.
				if (Dot(Dir, P.Axis) < 0.0 && InConvex(Opening, N, Edge, 1e-9))
				{
					Violations += OnArc ? 0 : 1;
				}
				// Where both circles are under the rail there is no front arc (back wall up to WallTopZ).
				if (!InConvex(Opening, N, Wall, -1e-9) && !InConvex(Opening, N, Edge, -1e-9))
				{
					Violations += OnArc ? 1 : 0;
				}
			}
			if (Front == 0)
			{
				return -1;
			}
		}
		return Violations;
	}

	// Signed area of a closed polyline.
	double SignedArea(const Vec2* P, int N)
	{
		double A = 0.0;
		for (int i = 0; i < N; ++i)
		{
			A += Cross(P[i], P[(i + 1) % N]);
		}
		return 0.5 * A;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Sights, spots, strings (equipment 3)
// ---------------------------------------------------------------------------------------------------------

RB_TEST(EQP_TGEOM1_NineFootDiamonds)
{
	const TableGeometry G = Build(kTableNineFootPro);
	RB_CHECK(G.Sights.Size() == 18);
	int Long = 0;
	for (const double X : {-0.9525, -0.635, -0.3175, 0.3175, 0.635, 0.9525})
	{
		for (const double Y : {-0.7286625, 0.7286625})
		{
			RB_CHECK(HasSight(G, X, Y, 1e-9));
			++Long;
		}
	}
	for (const double Y : {-0.3175, 0.0, 0.3175})
	{
		RB_CHECK(HasSight(G, 1.3636625, Y, 1e-9));
		RB_CHECK(HasSight(G, -1.3636625, Y, 1e-9));
	}
	RB_CHECK(Long == 12);
	int OnLong = 0;
	for (const Sight& S : G.Sights)
	{
		RB_CHECK(S.Position.z == kTableNineFootPro.RailTopZ);
		OnLong += S.OnLongRail ? 1 : 0;
		// Projected diamond on the nose line, sight inset behind it.
		RB_CHECK_NEAR(Length(XY(S.Position) - S.NoseLinePoint), kSightInsetFromNose, 1e-12);
		RB_CHECK(S.OnLongRail ? (S.Index >= 1 && S.Index <= 7 && S.Index != 4) : (S.Index >= 1 && S.Index <= 3));
		const NoseSegment& Nose = G.Noses[static_cast<int>(S.NearestCushion)];
		RB_CHECK(Nose.Present);
		RB_CHECK_NEAR(Dot(S.NoseLinePoint - Nose.Start, Nose.InwardNormal), 0.0, 1e-12); // on that cushion's line
	}
	RB_CHECK(OnLong == 12);
	RB_CHECK_NEAR(G.Landmarks.DiamondSpacing, 0.3175, 1e-12);
}

RB_TEST(EQP_TGEOM2_SevenFootDiamonds)
{
	const TableGeometry G = Build(kTableSevenFootBar);
	RB_CHECK(G.Sights.Size() == 18);
	for (const double X : {-0.762, -0.508, -0.254, 0.254, 0.508, 0.762})
	{
		RB_CHECK(HasSight(G, X, 0.6016625, 1e-9));
		RB_CHECK(HasSight(G, X, -0.6016625, 1e-9));
	}
	for (const double Y : {-0.254, 0.0, 0.254})
	{
		RB_CHECK(HasSight(G, 1.1096625, Y, 1e-9));
		RB_CHECK(HasSight(G, -1.1096625, Y, 1e-9));
	}
}

RB_TEST(EQP_TGEOM3_Spots)
{
	const TableGeometry Nine = Build(kTableNineFootPro);
	RB_CHECK_NEAR(Nine.Landmarks.FootSpot.x, 0.635, 1e-12);
	RB_CHECK(Nine.Landmarks.FootSpot.y == 0.0);
	RB_CHECK_NEAR(Nine.Landmarks.HeadSpot.x, -0.635, 1e-12);
	RB_CHECK(Nine.Landmarks.HeadSpot.y == 0.0);
	RB_CHECK(Nine.Landmarks.CenterSpot.x == 0.0 && Nine.Landmarks.CenterSpot.y == 0.0);
	RB_CHECK_NEAR(Nine.Landmarks.HeadStringX, -0.635, 1e-12);
	RB_CHECK_NEAR(Nine.Landmarks.FootStringX, 0.635, 1e-12);
	RB_CHECK_NEAR(Nine.Landmarks.BaulkX, -1.27 + 0.508, 1e-12);
	const TableGeometry Bar = Build(kTableSevenFootBar);
	RB_CHECK_NEAR(Bar.Landmarks.FootSpot.x, 0.508, 1e-12);
	RB_CHECK_NEAR(Bar.Landmarks.HeadSpot.x, -0.508, 1e-12);
	RB_CHECK(Bar.Landmarks.FootSpot.y == 0.0 && Bar.Landmarks.HeadSpot.y == 0.0);
}

RB_TEST(Geometry_LandmarksMatchTheRulesTable)
{
	// The rules' hand-built table (rules::MakeRulesTable) and BuildRulesTable's source agree bitwise.
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry G = Build(GetTableSpec(Preset));
		const rules::RulesTable T = rules::MakeRulesTable(G.Spec.Length, G.Spec.Width, kBallRadius);
		RB_CHECK(G.Landmarks.HeadStringX == T.HeadStringX && G.Landmarks.FootStringX == T.FootStringX && G.Landmarks.BaulkX == T.BaulkX);
		RB_CHECK(G.Landmarks.HeadSpot == T.HeadSpot && G.Landmarks.FootSpot == T.FootSpot && G.Landmarks.CenterSpot == T.CenterSpot);
	}
}

RB_TEST(EQP_TGEOM4_AboveHeadStringExcludesTheString)
{
	const TableGeometry G = Build(kTableNineFootPro);
	RB_CHECK(IsAboveHeadString(-0.6351, G.Landmarks));
	RB_CHECK(!IsAboveHeadString(-0.6350, G.Landmarks));
	RB_CHECK(!IsAboveHeadString(0.0, G.Landmarks));
}

// ---------------------------------------------------------------------------------------------------------
// Noses and pockets (equipment 5.3)
// ---------------------------------------------------------------------------------------------------------

RB_TEST(EQP_TGEOM5_NoseLengthSum)
{
	const TableGeometry G = Build(kTableNineFootPro);
	double Sum = 0.0;
	for (int k = 0; k < kCushionCount; ++k)
	{
		const Vec2 From = G.Pockets[k].JawPoint[static_cast<int>(JawSide::Outgoing)];
		const Vec2 To = G.Pockets[(k + 1) % kPocketCount].JawPoint[static_cast<int>(JawSide::Incoming)];
		const double BetweenVirtualPoints = Length(To - From);
		Sum += BetweenVirtualPoints;
		// The physics segment ends at the jaw-arc tangent points.
		const double Trim = Length(G.JawArcs[2 * k + 1].TangentOnNose - From) + Length(G.JawArcs[2 * ((k + 1) % kPocketCount)].TangentOnNose - To);
		RB_CHECK_NEAR(G.Noses[k].Length + Trim, BetweenVirtualPoints, 1e-12);
	}
	RB_CHECK_NEAR(Sum, 6.719422, 1e-6);
}

RB_TEST(EQP_TPOCKET1_FootLeftJawPoints)
{
	const TableGeometry G = Build(kTableNineFootPro);
	const PocketGeometry& P = PocketOf(G, PocketId::FootLeft);
	const Vec2 Long = P.JawPoint[static_cast<int>(JawSide::Outgoing)]; // P_L, starts C3
	const Vec2 End = P.JawPoint[static_cast<int>(JawSide::Incoming)];  // P_E, ends C2
	RB_CHECK_NEAR(Long.x, 1.1891777, 1e-7);
	RB_CHECK_NEAR(Long.y, 0.635, 1e-7);
	RB_CHECK_NEAR(End.x, 1.27, 1e-7);
	RB_CHECK_NEAR(End.y, 0.5541777, 1e-7);
	RB_CHECK_NEAR(Length(Long - End), 0.1143, 1e-7);
	RB_CHECK_NEAR(P.MouthMid.x, 1.229589, 1e-6);
	RB_CHECK_NEAR(P.MouthMid.y, 0.594589, 1e-6);
	// Facing direction from P_L (equipment 5.3 table: f_L = (0.78801, 0.61566)).
	const Facing& F = FacingOf(G, PocketId::FootLeft, JawSide::Outgoing);
	RB_CHECK_NEAR(F.Direction.x, 0.78801, 1e-5);
	RB_CHECK_NEAR(F.Direction.y, 0.61566, 1e-5);
	// Side jaw points (SIDE_LEFT) and f_+ (from the +x jaw).
	const PocketGeometry& S = PocketOf(G, PocketId::SideLeft);
	RB_CHECK_NEAR(S.JawPoint[static_cast<int>(JawSide::Incoming)].x, 0.0635, 1e-7);
	RB_CHECK_NEAR(S.JawPoint[static_cast<int>(JawSide::Outgoing)].x, -0.0635, 1e-7);
	const Facing& Fp = FacingOf(G, PocketId::SideLeft, JawSide::Incoming);
	RB_CHECK_NEAR(Fp.Direction.x, -0.24192, 1e-5);
	RB_CHECK_NEAR(Fp.Direction.y, 0.97030, 1e-5);
	// 7-ft BAR column of the same table.
	const TableGeometry Bar = Build(kTableSevenFootBar);
	const PocketGeometry& Pb = PocketOf(Bar, PocketId::FootLeft);
	RB_CHECK_NEAR(Pb.JawPoint[static_cast<int>(JawSide::Outgoing)].x, 0.928443, 1e-6);
	RB_CHECK_NEAR(Pb.JawPoint[static_cast<int>(JawSide::Incoming)].y, 0.420443, 1e-6);
	RB_CHECK_NEAR(FacingOf(Bar, PocketId::FootLeft, JawSide::Outgoing).Direction.x, 0.74314, 1e-5);
	RB_CHECK_NEAR(FacingOf(Bar, PocketId::SideLeft, JawSide::Incoming).Direction.x, -0.17365, 1e-5);
}

RB_TEST(EQP_TPOCKET2_CornerThroat)
{
	const TableGeometry G = Build(kTableNineFootPro);
	RB_CHECK_NEAR(PocketOf(G, PocketId::FootLeft).Throat, 0.0941884, 1e-7);
	RB_CHECK_NEAR(CornerThroat(0.1143, 142.0 * kDegToRad, 0.0508), 0.0941884, 1e-7);
	RB_CHECK_NEAR(CornerThroat(0.1143, 142.0 * kDegToRad, 0.0508) / kInch, 3.7082, 1e-4);
	// Direct check: the distance between the facing lines, 2 in behind the long-rail nose line, along the mouth.
	const Facing& A = FacingOf(G, PocketId::FootLeft, JawSide::Outgoing);
	const Facing& B = FacingOf(G, PocketId::FootLeft, JawSide::Incoming);
	const PocketGeometry& P = PocketOf(G, PocketId::FootLeft);
	// Point on facing A at depth t = 2 in behind its rail (y = W/2 + t), same on B (x = L/2 + t).
	const double T = 0.0508;
	const Vec2 Pa = P.JawPoint[1] + A.Direction * (T / A.Direction.y);
	const Vec2 Pb = P.JawPoint[0] + B.Direction * (T / B.Direction.x);
	RB_CHECK_NEAR(Length(Pa - Pb), 0.0941884, 1e-7);
}

RB_TEST(EQP_TPOCKET3_MouthMinusThroatMatchesTdf)
{
	const double M = 0.1143;
	RB_CHECK_NEAR((M - CornerThroat(M, 141.7 * kDegToRad, 0.0508)) / kInch, 0.7530, 0.005);
	RB_CHECK_NEAR((M - CornerThroat(M, 142.6 * kDegToRad, 0.0508)) / kInch, 0.8710, 0.005);
	RB_CHECK_NEAR((M - CornerThroat(M, 145.3 * kDegToRad, 0.0508)) / kInch, 1.256, 0.005);
	// WPA 142 deg: 0.792 in (TDF 3/4-7/8 band).
	RB_CHECK_NEAR((M - CornerThroat(M, 142.0 * kDegToRad, 0.0508)) / kInch, 0.792, 0.001);
}

RB_TEST(EQP_TPOCKET4_SideThroat)
{
	const TableGeometry G = Build(kTableNineFootPro);
	RB_CHECK_NEAR(PocketOf(G, PocketId::SideLeft).Throat, 0.1016683, 1e-7);
	RB_CHECK_NEAR(SideThroat(0.127, 104.0 * kDegToRad, 0.0508) / kInch, 4.0027, 1e-4);
	// Direct: facings 2 in behind the nose line.
	const Facing& A = FacingOf(G, PocketId::SideLeft, JawSide::Incoming);
	const Facing& B = FacingOf(G, PocketId::SideLeft, JawSide::Outgoing);
	const PocketGeometry& P = PocketOf(G, PocketId::SideLeft);
	const Vec2 Pa = P.JawPoint[0] + A.Direction * (0.0508 / A.Direction.y);
	const Vec2 Pb = P.JawPoint[1] + B.Direction * (0.0508 / B.Direction.y);
	RB_CHECK_NEAR(Length(Pa - Pb), 0.1016683, 1e-7);
}

RB_TEST(EQP_TPOCKET5_Convergence)
{
	const TableGeometry G = Build(kTableNineFootPro);
	for (int k = 0; k < kPocketCount; ++k)
	{
		const PocketGeometry& P = G.Pockets[k];
		const double Expected = P.Kind == PocketKind::Corner ? 7.0 * kDegToRad : 14.0 * kDegToRad;
		RB_CHECK_NEAR(P.Convergence, Expected, 1e-9);
		RB_CHECK_NEAR(P.FacingAngle, kPi - P.CutAngle, 1e-15);
	}
	RB_CHECK_NEAR(PocketOf(G, PocketId::FootLeft).FacingAngle, 38.0 * kDegToRad, 1e-9);
	// pooltool mapping: corner_pocket_angle 5.3 deg <-> C = 140.3 deg (TABLE_7FT_78).
	const TableGeometry P78 = Build(kTableSevenFoot78);
	RB_CHECK_NEAR(PocketOf(P78, PocketId::FootLeft).Convergence, 5.3 * kDegToRad, 1e-9);
	RB_CHECK_NEAR(PocketOf(P78, PocketId::SideLeft).Convergence, 7.14 * kDegToRad, 1e-9);
	// Each facing leans toward the axis by beta: angle between the facing and the axis.
	for (int k = 0; k < kPocketCount; ++k)
	{
		const PocketGeometry& P = G.Pockets[k];
		for (int s = 0; s < 2; ++s)
		{
			const Facing& F = G.Facings[2 * k + s];
			RB_CHECK_NEAR(Acos(Dot(F.Direction, P.Axis)), P.Convergence, 1e-9);
		}
	}
}

RB_TEST(EQP_TPOCKET6_JawRounding)
{
	const TableGeometry G = Build(kTableNineFootPro);
	for (int k = 0; k < kPocketCount; ++k)
	{
		const bool Corner = G.Pockets[k].Kind == PocketKind::Corner;
		for (int s = 0; s < 2; ++s)
		{
			const JawArc& A = G.JawArcs[2 * k + s];
			RB_CHECK(A.Radius == 0.004);
			RB_CHECK_NEAR(Length(A.TangentOnNose - A.VirtualPoint), Corner ? 0.0013773 : 0.0031252, 1e-7);
			RB_CHECK_NEAR(Length(A.TangentOnFacing - A.VirtualPoint), Corner ? 0.0013773 : 0.0031252, 1e-7);
			if (Corner)
			{
				RB_CHECK_NEAR(Length(A.Center - A.VirtualPoint), 0.0042305, 1e-7);
			}
			// Tangency: the tangent points lie on the circle, at the ends of the exposed arc.
			RB_CHECK_NEAR(Length(A.TangentOnNose - A.Center), A.Radius, 1e-15);
			RB_CHECK_NEAR(Length(A.TangentOnFacing - A.Center), A.Radius, 1e-15);
			const Vec2 E0 = A.Center + Vec2{Cos(A.AngleFrom), Sin(A.AngleFrom)} * A.Radius;
			const Vec2 E1 = A.Center + Vec2{Cos(A.AngleFrom + A.AngleSweep), Sin(A.AngleFrom + A.AngleSweep)} * A.Radius;
			const bool Ends = (Near(E0, A.TangentOnNose, 1e-15) && Near(E1, A.TangentOnFacing, 1e-15)) ||
				(Near(E0, A.TangentOnFacing, 1e-15) && Near(E1, A.TangentOnNose, 1e-15));
			RB_CHECK(Ends);
			RB_CHECK_NEAR(A.AngleSweep, kPi - G.Pockets[k].CutAngle, 1e-15);
			// The exposed arc faces away from the material: its midpoint is closer to the virtual point than the center.
			const Vec2 Mid = A.Center + Vec2{Cos(A.AngleFrom + 0.5 * A.AngleSweep), Sin(A.AngleFrom + 0.5 * A.AngleSweep)} * A.Radius;
			RB_CHECK(Length(Mid - A.VirtualPoint) < Length(A.Center - A.VirtualPoint));
			RB_CHECK(A.Height == kCushionNoseHeight);
		}
	}
}

RB_TEST(EQP_TPOCKET7_NineFootCaptureCenters)
{
	const TableGeometry G = Build(kTableNineFootPro);
	RB_CHECK_NEAR(PocketOf(G, PocketId::FootLeft).CaptureCenter.x, 1.302615, 1e-6);
	RB_CHECK_NEAR(PocketOf(G, PocketId::FootLeft).CaptureCenter.y, 0.667615, 1e-6);
	RB_CHECK_NEAR(PocketOf(G, PocketId::SideLeft).CaptureCenter.x, 0.0, 1e-6);
	RB_CHECK_NEAR(PocketOf(G, PocketId::SideLeft).CaptureCenter.y, 0.704263, 1e-6);
	RB_CHECK(PocketOf(G, PocketId::FootLeft).CaptureRadius == 0.062 && PocketOf(G, PocketId::SideLeft).CaptureRadius == 0.0645);
}

RB_TEST(EQP_TPOCKET8_SevenFootCaptureCenters)
{
	const TableGeometry G = Build(kTableSevenFootBar);
	RB_CHECK_NEAR(PocketOf(G, PocketId::FootLeft).CaptureCenter.x, 1.020552, 1e-6);
	RB_CHECK_NEAR(PocketOf(G, PocketId::FootLeft).CaptureCenter.y, 0.512552, 1e-6);
	RB_CHECK_NEAR(PocketOf(G, PocketId::SideLeft).CaptureCenter.x, 0.0, 1e-6);
	RB_CHECK_NEAR(PocketOf(G, PocketId::SideLeft).CaptureCenter.y, 0.5725, 1e-6);
}

RB_TEST(EQP_TPOCKET9_BallFitsEveryThroat)
{
	double Smallest = kInfinity;
	TablePreset SmallestPreset = TablePreset::Custom;
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry G = Build(GetTableSpec(Preset));
		RB_CHECK(G.Pockets.Size() == kPocketCount);
		for (const PocketGeometry& P : G.Pockets)
		{
			RB_CHECK(P.Throat > kBallDiameter);
			if (P.Throat < Smallest)
			{
				Smallest = P.Throat;
				SmallestPreset = Preset;
			}
		}
	}
	RB_CHECK(SmallestPreset == TablePreset::NineFootTight);
	RB_CHECK_NEAR(Smallest, 0.087838, 1e-6);
}

RB_TEST(COL_P1_ThroatsAndFacingLengths)
{
	const TableGeometry G = Build(kTableNineFootPro);
	RB_CHECK_NEAR(PocketOf(G, PocketId::FootLeft).Throat, 0.0941884, 1e-6);
	RB_CHECK_NEAR(PocketOf(G, PocketId::SideLeft).Throat, 0.1016683, 1e-6);
	for (int s = 0; s < 2; ++s)
	{
		const Facing& Corner = FacingOf(G, PocketId::FootLeft, static_cast<JawSide>(s));
		const Facing& Side = FacingOf(G, PocketId::SideLeft, static_cast<JawSide>(s));
		// Spec values are printed to 5 decimals (0.08251 / 0.05236 m): +-1 unit of the last printed digit; the
		// derived CushionWidth / sin(phi) and CushionWidth / cos(beta) at the stated 1e-6.
		RB_CHECK_NEAR(Corner.LengthFromVirtualPoint, 0.08251, 1e-5);
		RB_CHECK_NEAR(Side.LengthFromVirtualPoint, 0.05236, 1e-5);
		RB_CHECK_NEAR(Corner.LengthFromVirtualPoint, 0.0825129, 1e-6);
		RB_CHECK_NEAR(Side.LengthFromVirtualPoint, 0.0523552, 1e-6);
		RB_CHECK_NEAR(Corner.Length, Corner.LengthFromVirtualPoint - 0.0013773, 1e-7);
	}
}

// ---------------------------------------------------------------------------------------------------------
// Pocket elements (physics-collisions 5.1-5.3)
// ---------------------------------------------------------------------------------------------------------

RB_TEST(Geometry_NosesMeetTheJawTangentPoints)
{
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry G = Build(GetTableSpec(Preset));
		RB_CHECK(G.Noses.Size() == kCushionCount);
		for (int k = 0; k < kCushionCount; ++k)
		{
			const NoseSegment& N = G.Noses[k];
			RB_CHECK(N.Present && N.Cushion == static_cast<CushionId>(k));
			RB_CHECK(N.Start == G.JawArcs[2 * k + 1].TangentOnNose);
			RB_CHECK(N.End == G.JawArcs[2 * ((k + 1) % kPocketCount)].TangentOnNose);
			RB_CHECK_NEAR(Length(N.End - N.Start), N.Length, 1e-15);
			RB_CHECK(Near(N.Start + N.Direction * N.Length, N.End, 1e-15));
			RB_CHECK(Near(N.InwardNormal, PerpCcw(N.Direction), 0.0));
			RB_CHECK(N.Height == G.Spec.CushionNoseHeight);
			// On the rectangle of nose lines; the inward normal points to the table center.
			RB_CHECK(Near(Abs(N.Start.x), G.HalfLength, 0.0) || Near(Abs(N.Start.y), G.HalfWidth, 0.0));
			RB_CHECK(Dot(Vec2{} - N.Start, N.InwardNormal) > 0.0);
		}
	}
}

RB_TEST(Geometry_FacingsRunFromTheJawToTheCushionBack)
{
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry G = Build(GetTableSpec(Preset));
		for (int k = 0; k < kPocketCount; ++k)
		{
			const PocketGeometry& P = G.Pockets[k];
			for (int s = 0; s < 2; ++s)
			{
				const Facing& F = G.Facings[2 * k + s];
				const JawArc& A = G.JawArcs[2 * k + s];
				RB_CHECK(F.Pocket == P.Id && F.Side == static_cast<JawSide>(s) && A.Pocket == P.Id && A.Side == static_cast<JawSide>(s));
				RB_CHECK(F.Start == A.TangentOnFacing);
				RB_CHECK(A.VirtualPoint == P.JawPoint[s]);
				RB_CHECK_NEAR(Length(F.End - F.Start), F.Length, 1e-15);
				RB_CHECK_NEAR(Length(F.End - P.JawPoint[s]), F.LengthFromVirtualPoint, 1e-15);
				RB_CHECK_NEAR(Length(F.Direction), 1.0, 1e-15);
				RB_CHECK_NEAR(Dot(F.PocketNormal, F.Direction), 0.0, 1e-15);
				// The pocket normal points toward the other jaw (into the opening).
				RB_CHECK(Dot(F.PocketNormal, P.JawPoint[1 - s] - P.JawPoint[s]) > 0.0);
				// The facing ends on the cushion-back line, CushionWidth behind the nose line.
				const double Depth = P.Kind == PocketKind::Corner && s == (P.Axis.x * P.Axis.y > 0.0 ? 0 : 1)
					? Abs(F.End.x) - G.HalfLength
					: Abs(F.End.y) - G.HalfWidth;
				RB_CHECK_NEAR(Depth, G.Spec.CushionWidth, 1e-15);
				RB_CHECK(F.TopHeight == G.Spec.CushionNoseHeight && F.Backdraft == G.Spec.Backdraft && F.Thickness == G.Spec.FacingThickness);
			}
		}
	}
}

RB_TEST(Geometry_PocketDropEdgeAndFrontArc)
{
	for (const TablePreset Preset : kAllPresets)
	{
		const TableSpec Spec = GetTableSpec(Preset);
		const TableGeometry G = Build(Spec);
		for (int k = 0; k < kPocketCount; ++k)
		{
			const PocketGeometry& P = G.Pockets[k];
			const PocketSpec& Ps = P.Kind == PocketKind::Corner ? Spec.Corner : Spec.Side;
			RB_CHECK(P.Id == static_cast<PocketId>(k));
			RB_CHECK(P.DropEdgeRadius == Ps.CaptureRadius + Spec.DropPointRadius); // a_d = r_p + r_d (collisions pitfall 19)
			RB_CHECK(P.DropRadius == Spec.DropPointRadius && P.CaptureRadius == Ps.CaptureRadius);
			RB_CHECK(P.WallTopZ == Spec.RailTopZ && P.LinerUndercut == Spec.LinerUndercut && P.Backdraft == Spec.Backdraft);
			RB_CHECK_NEAR(Length(P.Axis), 1.0, 1e-15);
			// C_cap = mouth midpoint + (shelf + r_p) axis: the front of the capture circle is on the slate cut.
			RB_CHECK(Near(P.CaptureCenter - P.Axis * (Ps.Shelf + Ps.CaptureRadius), P.MouthMid, 1e-15));
			RB_CHECK(Near(P.MouthMid, (P.JawPoint[0] + P.JawPoint[1]) * 0.5, 1e-15));
			RB_CHECK_NEAR(Length(P.JawPoint[1] - P.JawPoint[0]), Ps.Mouth, 1e-15);
			// Front arc: ends where the capture circle r_p meets the two facing lines (on the facings, not behind them)
			// or, for TABLE_7FT_78's side pockets (the r_p circle would meet the facing lines 0.3 mm behind the facing
			// ends), the cushion-back line; the table-side point of the circle inside it.
			RB_CHECK(P.FrontArcSweep > 0.0 && P.FrontArcSweep < kTwoPi);
			RB_CHECK(P.FrontArcFrom > -kPi && P.FrontArcFrom <= kPi);
			const double Ends[2] = {P.FrontArcFrom, P.FrontArcFrom + P.FrontArcSweep};
			bool OnFacing[2] = {false, false};
			int OnCushionBack = 0;
			for (const double Angle : Ends)
			{
				const Vec2 X = P.CaptureCenter + Vec2{Cos(Angle), Sin(Angle)} * P.CaptureRadius;
				for (int s = 0; s < 2; ++s)
				{
					const Facing& F = G.Facings[2 * k + s];
					if (Abs(Cross(F.Direction, X - P.JawPoint[s])) < 1e-12)
					{
						OnFacing[s] = true;
						RB_CHECK(Dot(X - P.JawPoint[s], F.Direction) <= F.LengthFromVirtualPoint); // on the facing, not behind it
					}
				}
				const bool Back = P.Kind == PocketKind::Side ? Near(Abs(X.y), G.HalfWidth + Spec.CushionWidth, 1e-12)
															 : (Near(Abs(X.x), G.HalfLength + Spec.CushionWidth, 1e-12) ||
																   Near(Abs(X.y), G.HalfWidth + Spec.CushionWidth, 1e-12));
				OnCushionBack += Back ? 1 : 0;
			}
			if (Preset == TablePreset::SevenFoot78 && P.Kind == PocketKind::Side)
			{
				RB_CHECK(OnCushionBack == 2 && !OnFacing[0] && !OnFacing[1]);
			}
			else
			{
				RB_CHECK(OnFacing[0] && OnFacing[1] && OnCushionBack == 0);
			}
			RB_CHECK(ArcContainsAngle(P.FrontArcFrom, P.FrontArcSweep, Atan2(-P.Axis.y, -P.Axis.x)));
			RB_CHECK(!ArcContainsAngle(P.FrontArcFrom, P.FrontArcSweep, Atan2(P.Axis.y, P.Axis.x)));
			RB_CHECK(P.FrontArcSweep < kPi); // the front arc is the table side only
		}
	}
}

RB_TEST(Geometry_FrontArcIsTheCaptureCircleInsideTheOpening)
{
	// Review fix (collisions 5.3: "drop edge (rim): the capture circle's front arc between the facings"; the hole
	// wall exists on the front arc only below the rim, elsewhere up to WallTopZ). The angular range must be where the
	// r_p circle (slate cut / liner) lies inside the pocket opening: a range taken from the larger a_d circle is ~4 deg
	// (9-ft) to ~9 deg (bar) narrower per end and leaves a phantom back wall standing in the open pocket mouth next to
	// each facing. Every point of the front half of the r_p circle inside the opening is on the front arc, every point
	// under the rail (behind a facing line or the cushion back) is not; and every a_d point a ball center on the shelf
	// can reach (at least s_f from both facing lines) is on the front arc too (PredictDropEdge).
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry G = Build(GetTableSpec(Preset));
		RB_CHECK(FrontArcViolations(G) == 0);
		// Presets: exactly the capture circle's arc (never widened): no r_p point under the rail is on it.
		for (int k = 0; k < kPocketCount; ++k)
		{
			const PocketGeometry& P = G.Pockets[k];
			Vec2 Opening[5];
			const int N = PocketOpening(G, k, Opening);
			for (int s = 0; s < 3600; ++s)
			{
				const double Angle = (static_cast<double>(s) + 0.5) * kTwoPi / 3600.0 - kPi;
				const Vec2 Wall = P.CaptureCenter + Vec2{Cos(Angle), Sin(Angle)} * P.CaptureRadius;
				if (!InConvex(Opening, N, Wall, -1e-9))
				{
					RB_CHECK(!ArcContainsAngle(P.FrontArcFrom, P.FrontArcSweep, Angle));
				}
			}
		}
		// The a_d circle meets the facing plan lines 4-9 deg inside the front arc on every preset (the extra a_d points
		// lie behind the facings); TABLE_9FT_PRO corner: r_p crossings at 172.61 / 277.39 deg (P3).
		if (Preset == TablePreset::NineFootPro)
		{
			const PocketGeometry& P = PocketOf(G, PocketId::FootLeft);
			RB_CHECK_NEAR(P.FrontArcFrom * kRadToDeg, 172.61, 0.01);
			RB_CHECK_NEAR(P.FrontArcSweep * kRadToDeg, 2.0 * (225.0 - 172.61), 0.02);
		}
	}
}

RB_TEST(Geometry_IsOverPocketOpening)
{
	const TableGeometry G = Build(kTableNineFootPro);
	for (int k = 0; k < kPocketCount; ++k)
	{
		const PocketGeometry& P = G.Pockets[k];
		RB_CHECK(IsOverPocketOpening(G, P.CaptureCenter));
		RB_CHECK(IsOverPocketOpening(G, P.MouthMid + P.Axis * 1e-4));  // just beyond the mouth line
		RB_CHECK(!IsOverPocketOpening(G, P.MouthMid - P.Axis * 1e-4)); // just in front of it (9-ft: a_d stays behind)
		RB_CHECK(IsOverPocketOpening(G, P.CaptureCenter - P.Axis * (P.DropEdgeRadius - 1e-6)));
	}
	RB_CHECK(!IsOverPocketOpening(G, G.Landmarks.HeadSpot));
	RB_CHECK(!IsOverPocketOpening(G, G.Landmarks.FootSpot));
	RB_CHECK(!IsOverPocketOpening(G, {0.0, 0.0}));
	RB_CHECK(!IsOverPocketOpening(G, {1.2, 0.6})); // near the corner but in front of the mouth line
	// Bar side pocket without shelf: the drop-edge circle reaches 4.76 mm in front of the mouth line.
	const TableGeometry Bar = Build(kTableSevenFootBar);
	const PocketGeometry& S = PocketOf(Bar, PocketId::SideLeft);
	RB_CHECK(IsOverPocketOpening(Bar, S.MouthMid - S.Axis * 0.004));
	RB_CHECK(!IsOverPocketOpening(Bar, S.MouthMid - S.Axis * 0.0055));
}

RB_TEST(Geometry_NoseOutline)
{
	const TableGeometry G = Build(kTableNineFootPro);
	std::vector<Vec2> Points(1000);
	const int N = BuildNoseOutline(G, 16, Points.data(), static_cast<int>(Points.size()));
	RB_REQUIRE(N == kPocketCount * (2 * 16 + 2));
	// Closed, counter-clockwise, and it encloses the playing area plus the pocket openings.
	const double Area = SignedArea(Points.data(), N);
	RB_CHECK(Area > 2.54 * 1.27);
	RB_CHECK(Area < 2.54 * 1.27 + 6 * 0.02);
	// Arc samples lie on their arcs; the outline starts at the head-rail end of P0's incoming arc.
	RB_CHECK(Points[0] == G.JawArcs[0].TangentOnNose);
	for (int i = 1; i < 15; ++i)
	{
		RB_CHECK_NEAR(Length(Points[i] - G.JawArcs[0].Center), G.JawArcs[0].Radius, 1e-15);
		// On the exposed side (toward the virtual jaw point), not the arc's complement inside the material.
		RB_CHECK(Length(Points[i] - G.JawArcs[0].VirtualPoint) < Length(G.JawArcs[0].Center - G.JawArcs[0].VirtualPoint));
		RB_CHECK(Length(Points[18 + i] - G.JawArcs[1].VirtualPoint) < Length(G.JawArcs[1].Center - G.JawArcs[1].VirtualPoint));
	}
	RB_CHECK(Points[15] == G.JawArcs[0].TangentOnFacing);
	RB_CHECK(Points[16] == G.Facings[0].End && Points[17] == G.Facings[1].End);
	RB_CHECK(Points[18] == G.JawArcs[1].TangentOnFacing && Points[33] == G.JawArcs[1].TangentOnNose);
	// Consecutive samples of an arc advance monotonically (no back-tracking).
	for (int i = 1; i + 1 < 16; ++i)
	{
		RB_CHECK(Cross(Points[i] - Points[i - 1], Points[i + 1] - Points[i]) <= 1e-18);
	}
	// Capacity handling.
	RB_CHECK(BuildNoseOutline(G, 16, Points.data(), N - 1) == -1);
	RB_CHECK(BuildNoseOutline(G, 1, Points.data(), 1000) == kPocketCount * (2 * 2 + 2));
	RB_CHECK(BuildNoseOutline(TableGeometry{}, 16, Points.data(), 1000) == 0);
}

RB_TEST(Geometry_NoseOutlineHugeSampleCount)
{
	// Review fix: the required point count was summed in int. SamplesPerArc near INT_MAX (or ~2e8: 12 arcs x 2e8 >
	// INT_MAX) overflowed it to a negative number, the capacity check passed and the sampler wrote hundreds of millions
	// of points into the caller's small buffer. The count is now 64-bit: a too-large request returns -1 and writes nothing.
	const TableGeometry G = Build(kTableNineFootPro);
	std::vector<Vec2> Points(64, Vec2{7.0, 7.0});
	for (const int Samples : {2147483647, 2147483646, 200000000, 178956971})
	{
		RB_CHECK(BuildNoseOutline(G, Samples, Points.data(), static_cast<int>(Points.size())) == -1);
		RB_CHECK(BuildNoseOutline(G, Samples, Points.data(), 2147483647) == -1);
	}
	for (const Vec2& P : Points)
	{
		RB_CHECK(P == (Vec2{7.0, 7.0}));
	}
}

RB_TEST(Geometry_InvalidSpecsAreRejected)
{
	TableGeometry G;
	TableSpec S = kTableNineFootPro;
	S.Length = -1.0;
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	RB_CHECK(G.Noses.IsEmpty() && G.RailTops.IsEmpty() && G.Spec.Length == -1.0);
	S = kTableNineFootPro;
	S.Width = std::nan("");
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	S = kTableNineFootPro;
	S.RailWidthTotal = 0.04; // narrower than the cushion
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	S = kTableNineFootPro;
	S.Corner.JawRadius = 0.5; // tangent offset beyond the facing
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	S = kTableNineFootPro;
	S.Side.Mouth = 2.4; // side pocket wider than the long rail
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	S = kTableNineFootPro;
	S.Corner.CutAngle = kPi; // facing along the rail
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	S = kTableNineFootPro;
	S.Corner.Mouth = 0.02; // facings cross before the cushion back
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	S = kTableNineFootPro;
	S.Corner.Shelf = 0.0;
	S.Corner.CaptureRadius = 0.4; // the hole reaches the jaw points
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	// Review fix: a hole lying entirely inside the pocket opening never reaches under the rail. It built before with a
	// "front arc" of 2 pi: no back wall anywhere, so a ball flying across the pocket passed through the (unmodelled)
	// rail faces behind the opening.
	S = kTableNineFootPro;
	S.Corner.Shelf = 0.01;
	S.Corner.CaptureRadius = 0.02;
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	S = kTableNineFootPro;
	S.Side.CaptureRadius = 0.02;
	RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	// A valid build after a failed one.
	RB_CHECK(BuildTableGeometry(kTableNineFootPro, G) == ErrorCode::Ok && G.Pockets.Size() == kPocketCount);
}

RB_TEST(Geometry_FacingAndLinerAnglesAreValidated)
{
	// Review fix: a back draft or liner undercut of +-90 deg (or beyond) built a table whose facing / liner face is
	// horizontal or flipped: FacingContactOffset divided by cos(beta_v) = 6e-17 (s_f = 5e14 m) or went negative, and the
	// tilted contact normals were meaningless. |beta_v|, |beta_l| < pi/2 is now required; every built table has a finite
	// s_f.
	TableGeometry G;
	for (const double Bad : {0.5 * kPi, -0.5 * kPi, 2.0, -2.0, kPi})
	{
		TableSpec S = kTableNineFootPro;
		S.Backdraft = Bad;
		RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
		S = kTableNineFootPro;
		S.LinerUndercut = Bad;
		RB_CHECK(BuildTableGeometry(S, G) == ErrorCode::InvalidTable);
	}
	// Vertical faces, the WPA range and a slight overcut still build.
	for (const double Good : {0.0, 12.0 * kDegToRad, 15.0 * kDegToRad, -5.0 * kDegToRad, 1.5})
	{
		TableSpec S = kTableNineFootPro;
		S.Backdraft = Good;
		S.LinerUndercut = Good;
		RB_REQUIRE(BuildTableGeometry(S, G) == ErrorCode::Ok);
		const double Sf = FacingContactOffset(kBallRadius, S.CushionNoseHeight, G.Pockets[0].Backdraft);
		RB_CHECK(IsFinite(Sf) && Sf > 0.0);
	}
}

RB_TEST(Geometry_PlanAnglesStayInTheDocumentedRange)
{
	// Review fix: parallel side facings (C = 90 deg exactly) gave a facing direction with a -0 component, and atan2 of the
	// resulting pocket normal (-1, -0) returned -pi: JawArc::AngleFrom = -pi, outside the documented (-pi, pi]. Every
	// plan angle (jaw arcs, front arcs) is now in (-pi, pi], for mirror-exact cut angles too.
	for (const double SideCut : {0.5 * kPi, 100.0 * kDegToRad, 104.0 * kDegToRad})
	{
		for (const double CornerCut : {0.75 * kPi, 142.0 * kDegToRad})
		{
			TableSpec S = kTableNineFootPro;
			S.Preset = TablePreset::Custom;
			S.Side.CutAngle = SideCut;
			S.Corner.CutAngle = CornerCut;
			const TableGeometry G = Build(S);
			RB_REQUIRE(G.JawArcs.Size() == kMaxJaws);
			for (const JawArc& A : G.JawArcs)
			{
				RB_CHECK(A.AngleFrom > -kPi && A.AngleFrom <= kPi);
				// The documented arc still runs between the two tangent points.
				const Vec2 E0 = A.Center + Vec2{Cos(A.AngleFrom), Sin(A.AngleFrom)} * A.Radius;
				const Vec2 E1 = A.Center + Vec2{Cos(A.AngleFrom + A.AngleSweep), Sin(A.AngleFrom + A.AngleSweep)} * A.Radius;
				RB_CHECK((Near(E0, A.TangentOnNose, 1e-15) && Near(E1, A.TangentOnFacing, 1e-15)) ||
					(Near(E0, A.TangentOnFacing, 1e-15) && Near(E1, A.TangentOnNose, 1e-15)));
			}
			for (const PocketGeometry& P : G.Pockets)
			{
				RB_CHECK(P.FrontArcFrom > -kPi && P.FrontArcFrom <= kPi);
			}
			RB_CHECK(FrontArcViolations(G) == 0);
		}
	}
}

RB_TEST(Geometry_BuildIsDeterministic)
{
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry A = Build(GetTableSpec(Preset));
		const TableGeometry B = Build(GetTableSpec(Preset));
		RB_CHECK(A.RailTops.Size() == B.RailTops.Size());
		for (int i = 0; i < A.RailTops.Size(); ++i)
		{
			for (int v = 0; v < A.RailTops[i].VertexCount; ++v)
			{
				RB_CHECK(A.RailTops[i].Vertices[v] == B.RailTops[i].Vertices[v]);
			}
		}
		for (int k = 0; k < kPocketCount; ++k)
		{
			RB_CHECK(A.Pockets[k].CaptureCenter == B.Pockets[k].CaptureCenter && A.Pockets[k].FrontArcFrom == B.Pockets[k].FrontArcFrom);
			RB_CHECK(A.JawArcs[2 * k].Center == B.JawArcs[2 * k].Center && A.Facings[2 * k + 1].End == B.Facings[2 * k + 1].End);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------
// Mirror symmetry (T-GEOM-6)
// ---------------------------------------------------------------------------------------------------------

RB_TEST(EQP_TGEOM6_MirrorSymmetry)
{
	constexpr double Tol = 1e-12;
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry G = Build(GetTableSpec(Preset));
		for (const double Sx : {-1.0, 1.0})
		{
			for (const double Sy : {-1.0, 1.0})
			{
				// Pockets: jaw points (as a set), capture center, axis, drop-edge front arc end points (as a set).
				for (const PocketGeometry& P : G.Pockets)
				{
					bool Found = false;
					for (const PocketGeometry& Q : G.Pockets)
					{
						const Vec2 J0 = Mirror(P.JawPoint[0], Sx, Sy);
						const Vec2 J1 = Mirror(P.JawPoint[1], Sx, Sy);
						const bool Jaws = (Near(J0, Q.JawPoint[0], Tol) && Near(J1, Q.JawPoint[1], Tol)) || (Near(J0, Q.JawPoint[1], Tol) && Near(J1, Q.JawPoint[0], Tol));
						const Vec2 A0 = Mirror(Vec2{Cos(P.FrontArcFrom), Sin(P.FrontArcFrom)}, Sx, Sy);
						const Vec2 A1 = Mirror(Vec2{Cos(P.FrontArcFrom + P.FrontArcSweep), Sin(P.FrontArcFrom + P.FrontArcSweep)}, Sx, Sy);
						const Vec2 B0{Cos(Q.FrontArcFrom), Sin(Q.FrontArcFrom)};
						const Vec2 B1{Cos(Q.FrontArcFrom + Q.FrontArcSweep), Sin(Q.FrontArcFrom + Q.FrontArcSweep)};
						const bool Arc = Near(P.FrontArcSweep, Q.FrontArcSweep, Tol) &&
							((Near(A0, B0, Tol) && Near(A1, B1, Tol)) || (Near(A0, B1, Tol) && Near(A1, B0, Tol)));
						Found = Found || (Jaws && Arc && Q.Kind == P.Kind && Near(Mirror(P.CaptureCenter, Sx, Sy), Q.CaptureCenter, Tol) &&
											 Near(Mirror(P.Axis, Sx, Sy), Q.Axis, Tol) && Q.Throat == P.Throat && Q.DropEdgeRadius == P.DropEdgeRadius);
					}
					RB_CHECK(Found);
				}
				// Jaw arcs and facings.
				for (int e = 0; e < G.JawArcs.Size(); ++e)
				{
					const JawArc& A = G.JawArcs[e];
					const Facing& F = G.Facings[e];
					bool FoundArc = false;
					bool FoundFacing = false;
					for (int f = 0; f < G.JawArcs.Size(); ++f)
					{
						const JawArc& B = G.JawArcs[f];
						FoundArc = FoundArc || (Near(Mirror(A.Center, Sx, Sy), B.Center, Tol) && Near(Mirror(A.TangentOnNose, Sx, Sy), B.TangentOnNose, Tol) &&
												   Near(Mirror(A.TangentOnFacing, Sx, Sy), B.TangentOnFacing, Tol) && A.Radius == B.Radius &&
												   Near(A.AngleSweep, B.AngleSweep, Tol));
						const Facing& H = G.Facings[f];
						FoundFacing = FoundFacing || (Near(Mirror(F.Start, Sx, Sy), H.Start, Tol) && Near(Mirror(F.End, Sx, Sy), H.End, Tol) &&
														 Near(Mirror(F.Direction, Sx, Sy), H.Direction, Tol) &&
														 Near(Mirror(F.PocketNormal, Sx, Sy), H.PocketNormal, Tol));
					}
					RB_CHECK(FoundArc);
					RB_CHECK(FoundFacing);
				}
				// Nose segments (as undirected segments).
				for (const NoseSegment& N : G.Noses)
				{
					bool Found = false;
					for (const NoseSegment& M : G.Noses)
					{
						const Vec2 A = Mirror(N.Start, Sx, Sy);
						const Vec2 B = Mirror(N.End, Sx, Sy);
						Found = Found || ((Near(A, M.Start, Tol) && Near(B, M.End, Tol)) || (Near(A, M.End, Tol) && Near(B, M.Start, Tol)));
					}
					RB_CHECK(Found);
				}
				// Rail-top polygons.
				for (const RailTopPolygon& Poly : G.RailTops)
				{
					RB_CHECK(HasMirroredPolygon(G, Poly, Sx, Sy));
				}
				// Sights.
				for (const Sight& S : G.Sights)
				{
					RB_CHECK(HasSight(G, Sx * S.Position.x, Sy * S.Position.y, Tol));
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------
// Rail tops (collisions 6.2): A-GEO-1, A-GEO-2
// ---------------------------------------------------------------------------------------------------------

RB_TEST(ARCH_GEO1_RailTopPolygonsCoverTheRailTopExactlyOnce)
{
	for (const TablePreset Preset : kAllPresets)
	{
		const TableGeometry G = Build(GetTableSpec(Preset));
		RB_CHECK(G.RailTops.Size() == 28);
		CheckPolygonStructure(G);
		const double Xo = G.OuterBoundary.Hi.x;
		const double Yo = G.OuterBoundary.Hi.y;
		// Whole rail top on a 3.7 mm grid (odd offsets keep the samples off the construction lines).
		RB_CHECK(CoverageViolations(G, -Xo + 0.0001234567, Xo, -Yo + 0.0002345678, Yo, 0.0037) == 0);
		// Dense around the canonical corner and side pocket (the other four are mirrors, T-GEOM-6).
		const double Hl = G.HalfLength;
		const double Hw = G.HalfWidth;
		RB_CHECK(CoverageViolations(G, Hl - 0.16 + 0.00001357, Xo, Hw - 0.16 + 0.00002468, Yo, 0.0009) == 0);
		RB_CHECK(CoverageViolations(G, -0.16 + 0.00003579, 0.16, Hw - 0.01 + 0.00001357, Yo, 0.0009) == 0);
		// Pocket surrounds: the cut discs are carried by the polygons around each pocket (and only by those).
		int Cuts[kPocketCount] = {};
		for (const RailTopPolygon& Poly : G.RailTops)
		{
			RB_CHECK(Poly.Pocket != PocketId::None);
			if (Poly.HasCut)
			{
				++Cuts[static_cast<int>(Poly.Pocket)];
			}
		}
		for (int k = 0; k < kPocketCount; ++k)
		{
			// The cut goes through the rail cap on both sides of the pocket (corner: long and end cap pieces, side:
			// the two cap pieces behind it); cushion tops and the corner square carry it where the disc reaches them.
			RB_CHECK(Cuts[k] >= 2);
		}
	}
}

RB_TEST(ARCH_GEO1_RailTopEdgesAndPlanes)
{
	const TableGeometry G = Build(kTableNineFootPro);
	const TableSpec& S = kTableNineFootPro;
	int Tops = 0;
	int Caps = 0;
	for (const RailTopPolygon& Poly : G.RailTops)
	{
		if (Poly.Kind == RailTopKind::CushionTop)
		{
			++Tops;
			// ~13 deg slope from the nose (h) to the cushion back (RailTopZ), normal toward the table.
			RB_CHECK_NEAR(Acos(Poly.PlaneNormal.z) * kRadToDeg, 12.98, 0.01);
			RB_CHECK(Poly.PlanePoint.z == S.CushionNoseHeight);
			RB_CHECK(Poly.Cushion != CushionId::None);
			const NoseSegment& Nose = G.Noses[static_cast<int>(Poly.Cushion)];
			RB_CHECK_NEAR(Dot(XY(Poly.PlaneNormal), Nose.InwardNormal), Length(XY(Poly.PlaneNormal)), 1e-15);
		}
		else
		{
			++Caps;
			RB_CHECK(Poly.PlaneNormal.x == 0.0 && Poly.PlaneNormal.y == 0.0 && Poly.PlaneNormal.z == 1.0 && Poly.PlanePoint.z == S.RailTopZ);
		}
		// Every straight edge of kind Facing borders a pocket opening: its midpoint is inside the pocket's opening
		// polygon's closure.
		for (int i = 0; i < Poly.VertexCount; ++i)
		{
			if (Poly.Edges[i] != RailEdgeKind::Facing)
			{
				continue;
			}
			const Vec2 Mid = (Poly.Vertices[i] + Poly.Vertices[(i + 1) % Poly.VertexCount]) * 0.5;
			Vec2 Opening[5];
			const int N = PocketOpening(G, static_cast<int>(Poly.Pocket), Opening);
			RB_CHECK(InConvex(Opening, N, Mid, -1e-12));
		}
	}
	RB_CHECK(Tops == 12 && Caps == 16);
	// Documented index order: canonical quadrant 0-6, then x-mirror, both, y-mirror.
	RB_CHECK(G.RailTops[0].Kind == RailTopKind::CushionTop && G.RailTops[0].Cushion == CushionId::LeftFoot && G.RailTops[0].Pocket == PocketId::SideLeft);
	RB_CHECK(G.RailTops[1].Cushion == CushionId::LeftFoot && G.RailTops[1].Pocket == PocketId::FootLeft);
	RB_CHECK(G.RailTops[2].Kind == RailTopKind::CushionTop && G.RailTops[2].Cushion == CushionId::Foot);
	RB_CHECK(G.RailTops[5].Kind == RailTopKind::RailCap && G.RailTops[5].Cushion == CushionId::None && G.RailTops[5].Pocket == PocketId::FootLeft);
	RB_CHECK(G.RailTops[7].Cushion == CushionId::LeftHead && G.RailTops[7].Pocket == PocketId::SideLeft);
	RB_CHECK(G.RailTops[8].Pocket == PocketId::HeadLeft && G.RailTops[15].Pocket == PocketId::HeadRight && G.RailTops[22].Pocket == PocketId::FootRight);
	RB_CHECK(G.RailTops[23].Cushion == CushionId::Foot && G.RailTops[21].Cushion == CushionId::RightFoot && G.RailTops[14].Cushion == CushionId::RightHead);
	// The profile lies on the polygon planes (renderer and physics share them): nose (0, h), cushion back
	// (CushionWidth, RailTopZ), outer edge (RailWidthTotal, RailTopZ).
	RB_REQUIRE(G.Profile.Points.Size() == 4);
	RB_CHECK(G.Profile.Points[G.Profile.NoseIndex] == (Vec2{0.0, S.CushionNoseHeight}));
	RB_CHECK(G.Profile.Points[G.Profile.CushionBackIndex] == (Vec2{S.CushionWidth, S.RailTopZ}));
	RB_CHECK(G.Profile.Points[3] == (Vec2{S.RailWidthTotal, S.RailTopZ}));
	RB_CHECK(G.Profile.Points[0].y == 0.0 && G.Profile.Points[0].x > 0.0);
	// A 52.5 mm ball on the cloth touching the nose clears the rubber face below it.
	const double R = 0.02625;
	const double Rc = ComputeCushionContact(R, S.CushionNoseHeight, 0.0, false).HorizontalOffset;
	const Vec2 Base = G.Profile.Points[0];
	const Vec2 Nose = G.Profile.Points[1];
	const Vec2 Center{-Rc, R};
	const Vec2 Dir = Normalized(Base - Nose);
	const double T = Clamp(Dot(Center - Nose, Dir), 0.0, Length(Base - Nose));
	RB_CHECK(Length(Center - (Nose + Dir * T)) >= R - 1e-12);
	// Plan extents.
	RB_CHECK(G.PlayingArea.Hi.x == 1.27 && G.PlayingArea.Lo.y == -0.635);
	RB_CHECK_NEAR(G.OuterBoundary.Hi.x, 1.27 + 0.1778, 1e-15);
	RB_CHECK_NEAR(G.OuterBoundary.Lo.y, -(0.635 + 0.1778), 1e-15);
}

RB_TEST(Geometry_RandomSpecsBuildExactlyOrAreRejected)
{
	// Robustness: plausible custom tables either build with an exact rail-top partition and a consistent front
	// arc, or are rejected with InvalidTable (never a partial geometry).
	Rng Stream(20260926);
	int Built = 0;
	for (int n = 0; n < 40; ++n)
	{
		TableSpec S = kTableNineFootPro;
		S.Preset = TablePreset::Custom;
		S.Width = Stream.NextUniform(0.9, 1.3);
		S.Length = 2.0 * S.Width;
		S.CushionWidth = Stream.NextUniform(0.045, 0.055);
		S.RailWidthTotal = Stream.NextUniform(0.1, 0.19);
		S.RailTopZ = Stream.NextUniform(0.040, 0.050);
		S.Corner = {Stream.NextUniform(0.10, 0.14), Stream.NextUniform(136.0, 145.0) * kDegToRad, Stream.NextUniform(0.0, 0.06),
			Stream.NextUniform(0.0, 0.022), Stream.NextUniform(0.055, 0.07)};
		S.Side = {Stream.NextUniform(0.11, 0.14), Stream.NextUniform(95.0, 106.0) * kDegToRad, Stream.NextUniform(0.0, 0.01),
			Stream.NextUniform(0.0, 0.01), Stream.NextUniform(0.058, 0.07)};
		S.DropPointRadius = Stream.NextUniform(0.003, 0.0065);
		TableGeometry G;
		const ErrorCode Code = BuildTableGeometry(S, G);
		RB_CHECK(Code == ErrorCode::Ok || Code == ErrorCode::InvalidTable);
		if (Code != ErrorCode::Ok)
		{
			RB_CHECK(G.RailTops.IsEmpty() && G.Pockets.IsEmpty() && G.Noses.IsEmpty());
			continue;
		}
		++Built;
		RB_CHECK(G.RailTops.Size() == 28);
		const double Xo = G.OuterBoundary.Hi.x;
		const double Yo = G.OuterBoundary.Hi.y;
		RB_CHECK(CoverageViolations(G, -Xo + 0.0001234567, Xo, -Yo + 0.0002345678, Yo, 0.0071) == 0);
		RB_CHECK(CoverageViolations(G, G.HalfLength - 0.12 + 0.00001357, Xo, G.HalfWidth - 0.12 + 0.00002468, Yo, 0.0023) == 0);
		for (int k = 0; k < kPocketCount; ++k)
		{
			const PocketGeometry& P = G.Pockets[k];
			RB_CHECK(P.FrontArcSweep > 0.0 && P.FrontArcSweep < kPi && ArcContainsAngle(P.FrontArcFrom, P.FrontArcSweep, Atan2(-P.Axis.y, -P.Axis.x)));
			RB_CHECK(FrontArcViolations(G) == 0);
			// Each end of the front arc lies on a facing line or on a cushion-back line, on the capture circle or (a
			// widened end) on the drop-edge circle.
			for (const double Angle : {P.FrontArcFrom, P.FrontArcFrom + P.FrontArcSweep})
			{
				bool OnBoundary = false;
				for (const double Radius : {P.CaptureRadius, P.DropEdgeRadius})
				{
					const Vec2 X = P.CaptureCenter + Vec2{Cos(Angle), Sin(Angle)} * Radius;
					for (int s = 0; s < 2; ++s)
					{
						const Facing& F = G.Facings[2 * k + s];
						OnBoundary = OnBoundary || Abs(Cross(F.Direction, X - P.JawPoint[s])) < 1e-12;
						const bool Long = Abs(F.End.y) > G.HalfWidth + 0.5 * S.CushionWidth;
						OnBoundary = OnBoundary || (Long ? Abs(Abs(X.y) - Abs(F.End.y)) < 1e-12 : Abs(Abs(X.x) - Abs(F.End.x)) < 1e-12);
					}
				}
				RB_CHECK(OnBoundary);
			}
		}
	}
	RB_CHECK(Built >= 36);
	std::printf("  [random specs] %d of 40 built, the rest rejected as InvalidTable\n", Built);
}

RB_TEST(Geometry_ExtremeSpecsBuildExactlyOrAreRejected)
{
	// Adversarial: far outside the presets (0.3-2 m wide, L/W 0.8-3, corner facings diverging by up to 40 deg or
	// converging by up to 40 deg, side facings from -30 to +35 deg, tiny and huge holes, sharp and very round jaws).
	// Every table either is rejected or builds an exact rail-top partition, positive nose / facing lengths, convex CCW
	// polygons and a front arc that is the capture circle inside the opening. (The review ran 3000 of these: 1488
	// built, none violated an invariant. Side facings converging by 37-51 deg into a hole wider than the pocket can
	// make the circle meet the opening in two separate arcs, which one front arc cannot describe: out of range.)
	Rng Stream(777);
	int Built = 0;
	int Bad = 0;
	constexpr int Tables = 250;
	for (int n = 0; n < Tables; ++n)
	{
		TableSpec S = kTableNineFootPro;
		S.Preset = TablePreset::Custom;
		S.Width = Stream.NextUniform(0.3, 2.0);
		S.Length = S.Width * Stream.NextUniform(0.8, 3.0);
		S.CushionNoseHeight = Stream.NextUniform(0.02, 0.05);
		S.CushionWidth = Stream.NextUniform(0.02, 0.08);
		S.RailWidthTotal = S.CushionWidth + Stream.NextUniform(0.001, 0.25);
		S.RailTopZ = Stream.NextUniform(0.03, 0.07);
		S.Corner = {Stream.NextUniform(0.05, 0.25), Stream.NextUniform(95.0, 175.0) * kDegToRad, Stream.NextUniform(0.0, 0.1),
			Stream.NextUniform(0.0, 0.03), Stream.NextUniform(0.02, 0.15)};
		S.Side = {Stream.NextUniform(0.05, 0.25), Stream.NextUniform(60.0, 125.0) * kDegToRad, Stream.NextUniform(0.0, 0.05),
			Stream.NextUniform(0.0, 0.03), Stream.NextUniform(0.02, 0.15)};
		S.DropPointRadius = Stream.NextUniform(0.0, 0.02);
		TableGeometry G;
		if (BuildTableGeometry(S, G) != ErrorCode::Ok)
		{
			continue;
		}
		++Built;
		const double Xo = G.OuterBoundary.Hi.x;
		const double Yo = G.OuterBoundary.Hi.y;
		int V = CoverageViolations(G, -Xo + 0.0001234567, Xo, -Yo + 0.0002345678, Yo, 0.0097);
		V += CoverageViolations(G, G.HalfLength - 0.3 + 0.00001357, Xo, G.HalfWidth - 0.3 + 0.00002468, Yo, 0.0031);
		V += CoverageViolations(G, -0.3 + 0.00003579, 0.3, G.HalfWidth - 0.05 + 0.00001357, Yo, 0.0031);
		bool Ok = V == 0 && G.RailTops.Size() == 28 && FrontArcViolations(G) == 0;
		for (int k = 0; k < kPocketCount; ++k)
		{
			const PocketGeometry& P = G.Pockets[k];
			Ok = Ok && P.FrontArcSweep > 0.0 && P.FrontArcSweep < kTwoPi && P.FrontArcFrom > -kPi && P.FrontArcFrom <= kPi;
			Ok = Ok && ArcContainsAngle(P.FrontArcFrom, P.FrontArcSweep, Atan2(-P.Axis.y, -P.Axis.x));
			for (int s = 0; s < 2; ++s)
			{
				Ok = Ok && G.Facings[2 * k + s].Length > 0.0;
			}
		}
		for (const NoseSegment& Nose : G.Noses)
		{
			Ok = Ok && Nose.Length > 0.0;
		}
		for (const RailTopPolygon& Poly : G.RailTops)
		{
			for (int i = 0; i < Poly.VertexCount; ++i)
			{
				const Vec2 A = Poly.Vertices[i];
				const Vec2 B = Poly.Vertices[(i + 1) % Poly.VertexCount];
				const Vec2 C = Poly.Vertices[(i + 2) % Poly.VertexCount];
				Ok = Ok && Cross(B - A, C - B) >= -1e-15;
			}
		}
		if (!Ok)
		{
			++Bad;
			std::printf("  [extreme] table %d violates an invariant (%d coverage violations)\n", n, V);
		}
	}
	std::printf("  [extreme specs] %d of %d built, the rest rejected as InvalidTable\n", Built, Tables);
	RB_CHECK(Bad == 0);
	RB_CHECK(Built >= Tables / 5);
}

RB_TEST(ARCH_GEO2_PocketlessTable)
{
	TableSpec Carom = kTableNineFootPro;
	Carom.Preset = TablePreset::Custom;
	Carom.HasPockets = false;
	const TableGeometry G = Build(Carom);
	RB_CHECK(G.Pockets.IsEmpty() && G.JawArcs.IsEmpty() && G.Facings.IsEmpty());
	RB_REQUIRE(G.Noses.Size() == kCushionCount);
	RB_CHECK(!G.Noses[1].Present && !G.Noses[4].Present);
	RB_CHECK(G.Noses[1].Cushion == CushionId::RightFoot && G.Noses[4].Cushion == CushionId::LeftHead);
	const NoseSegment& Right = G.Noses[0];
	const NoseSegment& Left = G.Noses[3];
	RB_CHECK(Right.Present && Right.Start == (Vec2{-1.27, -0.635}) && Right.End == (Vec2{1.27, -0.635}) && Right.Length == 2.54);
	RB_CHECK(Left.Present && Left.Start == (Vec2{1.27, 0.635}) && Left.End == (Vec2{-1.27, 0.635}) && Left.Length == 2.54);
	RB_CHECK(G.Noses[2].Present && G.Noses[2].Length == 1.27 && G.Noses[5].Present && G.Noses[5].Length == 1.27);
	RB_CHECK(G.Noses[5].Start == (Vec2{-1.27, 0.635}) && G.Noses[5].End == (Vec2{-1.27, -0.635}));
	// Closed rectangle of four cushions; no pocket opening anywhere.
	RB_CHECK(!IsOverPocketOpening(G, {1.27 - 1e-3, 0.635 - 1e-3}));
	RB_CHECK(!IsOverPocketOpening(G, {0.0, 0.635 - 1e-3}));
	Vec2 Outline[8];
	RB_CHECK(BuildNoseOutline(G, 16, Outline, 8) == 4);
	RB_CHECK_NEAR(SignedArea(Outline, 4), 2.54 * 1.27, 1e-12);
	// Rail tops: 20 polygons covering the whole rail frame exactly once, no cuts, C0 / C2 / C3 / C5 only.
	RB_CHECK(G.RailTops.Size() == 20);
	CheckPolygonStructure(G);
	for (const RailTopPolygon& Poly : G.RailTops)
	{
		RB_CHECK(!Poly.HasCut && Poly.Pocket == PocketId::None);
		RB_CHECK(Poly.Cushion != CushionId::RightFoot && Poly.Cushion != CushionId::LeftHead);
		RB_CHECK(HasMirroredPolygon(G, Poly, -1.0, 1.0) && HasMirroredPolygon(G, Poly, 1.0, -1.0));
	}
	const double Xo = G.OuterBoundary.Hi.x;
	const double Yo = G.OuterBoundary.Hi.y;
	RB_CHECK(CoverageViolations(G, -Xo + 0.0001234567, Xo, -Yo + 0.0002345678, Yo, 0.0031) == 0);
	for (const Sight& S : G.Sights)
	{
		RB_CHECK(G.Noses[static_cast<int>(S.NearestCushion)].Present);
	}
	RB_CHECK(G.Sights.Size() == 18);
}
