#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.8 (bounded island features, review item 26),
// 8.9; physics-collisions 3.9.2 (features joining an island), 5.3, 6.1, 6.2.
// This file: table feature -> IslandFeature conversion, the solver's feature gap, landing / pocket / rail-top geometry.
#include "SimIslandInternal.h"

#include "rb/Math/Scalar.h"

namespace rb::sim
{
	namespace
	{
		// Constant restitution law e (fixed-e elements: rail top 0.5; collisions 6.2).
		CushionRestitutionLaw ConstantLaw(double E)
		{
			CushionRestitutionLaw Law;
			Law.Max = E;
			Law.Min = E;
			Law.Slope = 0.0;
			Law.Knee = 1.0;
			return Law;
		}

		IslandFeature BaseFeature(const TableFeatureRef& Ref)
		{
			IslandFeature F;
			F.SourceKind = static_cast<std::uint8_t>(Ref.Kind);
			F.SourceIndex = Ref.Index;
			F.SourceSub = Ref.SubIndex;
			return F;
		}

		void RailTopContactParams(IslandFeature& F, const PhysicsParams& Params)
		{
			F.RailFeature = 0xFF;
			F.Restitution = ConstantLaw(Params.PocketContacts.RailTopRestitution);
			F.RestitutionScale = 1.0;
			F.Friction = Params.PocketContacts.RailTopFriction;
			F.RollingResistance = Params.PocketContacts.RailTopRollingResistance;
			F.SpinDeceleration = Params.PocketContacts.RailTopSpinDeceleration;
		}

		bool InsideConvex(const Vec2* Vertices, int Count, const Vec2& P, double Slack)
		{
			if (Count < 3)
			{
				return false;
			}
			for (int i = 0; i < Count; ++i)
			{
				const Vec2& V0 = Vertices[i];
				const Vec2 E = Vertices[i + 1 < Count ? i + 1 : 0] - V0;
				const double L = Length(E);
				if (Cross(E, P - V0) < -Slack * L)
				{
					return false;
				}
			}
			return true;
		}

		// Angle intervals [From, To) within [0, 2 pi) on which the cut circle (Center, Radius) lies inside the convex polygon.
		struct ArcSet
		{
			double From[8] = {};
			double To[8] = {};
			int Count = 0;
		};

		// Intersects Set with the arc [Start, Start + Sweep) (mod 2 pi), Sweep in [0, 2 pi].
		void IntersectArc(ArcSet& Set, double Start, double Sweep)
		{
			if (Sweep >= kTwoPi)
			{
				return;
			}
			const double A = Start - kTwoPi * Floor(Start / kTwoPi);
			const double B = A + Sweep;
			const double Pieces[2][2] = {{A, Min(B, kTwoPi)}, {0.0, B - kTwoPi}};
			const int PieceCount = B > kTwoPi ? 2 : 1;
			ArcSet Out;
			for (int i = 0; i < Set.Count; ++i)
			{
				for (int k = 0; k < PieceCount; ++k)
				{
					const double Lo = Max(Set.From[i], Pieces[k][0]);
					const double Hi = Min(Set.To[i], Pieces[k][1]);
					if (Hi > Lo && Out.Count < 8)
					{
						Out.From[Out.Count] = Lo;
						Out.To[Out.Count] = Hi;
						++Out.Count;
					}
				}
			}
			Set = Out;
		}

		// Cut rim of a flat rail-top polygon: the arcs of the circle r_p about CutCenter that lie on the polygon (at most two,
		// the longest kept), as JawCircle features at the plane height.
		int CutRimFeatures(const RailTopPolygon& Poly, const IslandFeature& Base, IslandFeature Out[kMaxFeaturePieces])
		{
			const double Radius = Poly.CutRadius;
			if (!Poly.HasCut || !(Radius > 0.0) || Poly.VertexCount < 3)
			{
				return 0;
			}
			ArcSet Set;
			Set.From[0] = 0.0;
			Set.To[0] = kTwoPi;
			Set.Count = 1;
			for (int i = 0; i < Poly.VertexCount && Set.Count > 0; ++i)
			{
				const Vec2& V0 = Poly.Vertices[i];
				const Vec2 E = Poly.Vertices[(i + 1) % Poly.VertexCount] - V0;
				const double L = Length(E);
				if (!(L > 0.0))
				{
					continue;
				}
				// Inside: Cross(E, C - V0) + r |E| sin(phi - theta_E) >= 0, i.e. sin(phi - theta_E) >= -K.
				const double K = Cross(E, Poly.CutCenter - V0) / (Radius * L);
				if (K >= 1.0)
				{
					continue; // the whole circle is on the inner side of this edge
				}
				if (K < -1.0)
				{
					Set.Count = 0; // the whole circle is outside
					break;
				}
				const double ThetaE = Atan2(E.y, E.x);
				const double Delta = Asin(K);
				IntersectArc(Set, ThetaE - Delta, kPi + 2.0 * Delta);
			}
			if (Set.Count == 0)
			{
				return 0;
			}
			// Join the pieces that meet across 0 = 2 pi.
			int First = -1;
			int Last = -1;
			for (int i = 0; i < Set.Count; ++i)
			{
				if (Set.From[i] == 0.0)
				{
					First = i;
				}
				if (Set.To[i] == kTwoPi)
				{
					Last = i;
				}
			}
			if (First >= 0 && Last >= 0 && First != Last)
			{
				Set.From[First] = Set.From[Last] - kTwoPi;
				Set.To[Last] = Set.From[Last]; // emptied below
			}
			// Keep the (at most two) longest non-empty arcs, in angle order of their start for determinism.
			int Best[2] = {-1, -1};
			for (int i = 0; i < Set.Count; ++i)
			{
				const double Len = Set.To[i] - Set.From[i];
				if (!(Len > 0.0))
				{
					continue;
				}
				if (Best[0] < 0 || Len > Set.To[Best[0]] - Set.From[Best[0]])
				{
					Best[1] = Best[0];
					Best[0] = i;
				}
				else if (Best[1] < 0 || Len > Set.To[Best[1]] - Set.From[Best[1]])
				{
					Best[1] = i;
				}
			}
			int Count = 0;
			for (int k = 0; k < 2; ++k)
			{
				if (Best[k] < 0)
				{
					continue;
				}
				IslandFeature F = Base;
				F.Kind = IslandFeatureKind::JawCircle;
				F.Point = ToVec3(Poly.CutCenter, Poly.PlanePoint.z);
				F.Radius = Radius;
				F.AngleFrom = Set.From[Best[k]];
				F.AngleSweep = Set.To[Best[k]] - Set.From[Best[k]];
				if (Count == 1)
				{
					F.SourceSub = static_cast<std::uint8_t>(F.SourceSub - 1); // 0xFD: the second rim arc (0xFE | bit would overflow)
				}
				Out[Count++] = F;
			}
			return Count;
		}
	}

	int MakeIslandFeatures(const TableFeatureRef& Ref, const TableGeometry& Table, const PhysicsParams& Params, IslandFeature Out[kMaxFeaturePieces])
	{
		const int Index = Ref.Index;
		IslandFeature F = BaseFeature(Ref);
		switch (Ref.Kind)
		{
		case TableFeatureKind::NoseSegment:
		{
			if (Index >= Table.Noses.Size() || !Table.Noses[Index].Present)
			{
				return 0;
			}
			const NoseSegment& Nose = Table.Noses[Index];
			F.Kind = IslandFeatureKind::EdgeLine;
			F.RailFeature = static_cast<std::uint8_t>(RailFeatureOfCushion(static_cast<CushionId>(Index)));
			F.Point = ToVec3(Nose.Start, Nose.Height);
			F.Direction = ToVec3(Nose.Direction);
			F.Normal = ToVec3(Nose.InwardNormal);
			F.Length = Nose.Length;
			F.Radius = Params.Cushion.NoseProfileRadius;
			F.Restitution = Params.Cushion.Restitution;
			F.Friction = Params.Cushion.Friction;
			Out[0] = F;
			return 1;
		}
		case TableFeatureKind::JawArc:
		{
			if (Index >= Table.JawArcs.Size())
			{
				return 0;
			}
			const JawArc& Arc = Table.JawArcs[Index];
			F.Kind = IslandFeatureKind::JawCircle;
			F.RailFeature = static_cast<std::uint8_t>(RailFeatureOfJaw(Arc.Pocket, Arc.Side));
			F.Point = ToVec3(Arc.Center, Arc.Height);
			F.Radius = Arc.Radius;
			F.AngleFrom = Arc.AngleFrom;
			F.AngleSweep = Arc.AngleSweep;
			F.Restitution = Params.Cushion.Restitution;
			F.Friction = Params.Cushion.Friction;
			Out[0] = F;
			return 1;
		}
		case TableFeatureKind::FacingFace:
		case TableFeatureKind::FacingTopEdge:
		{
			if (Index >= Table.Facings.Size())
			{
				return 0;
			}
			const Facing& Face = Table.Facings[Index];
			F.RailFeature = static_cast<std::uint8_t>(RailFeatureOfJaw(Face.Pocket, Face.Side));
			F.Point = ToVec3(Face.Start, Face.TopHeight);
			F.Direction = ToVec3(Face.Direction);
			F.Length = Face.Length;
			F.Restitution = Params.Cushion.Restitution;
			F.RestitutionScale = Params.Cushion.FacingRestitutionScale;
			F.Friction = Params.Cushion.FacingFriction;
			if (Ref.Kind == TableFeatureKind::FacingFace)
			{
				// Undercut face (collisions 5.3): the plan line at h, normal into the pocket tilted DOWN by beta_v; from the
				// shelf up to the top edge.
				const double CosB = Cos(Face.Backdraft);
				const double SinB = Sin(Face.Backdraft);
				F.Kind = IslandFeatureKind::FacingPlane;
				F.Normal = Vec3{Face.PocketNormal.x * CosB, Face.PocketNormal.y * CosB, -SinB};
				F.ZMin = 0.0;
				F.ZMax = Face.TopHeight;
			}
			else if (Ref.SubIndex == kFacingBottomEdge)
			{
				// The facing's bottom edge (WP-10, Detect.h PredictFacingBottomEdge): one EdgeLine at z = 0 along the facing.
				F.Kind = IslandFeatureKind::EdgeLine;
				F.Point = FacingBottomEdgeStart(Face);
				F.Normal = -Vec3{Face.PocketNormal.x * Sin(Face.Backdraft), Face.PocketNormal.y * Sin(Face.Backdraft), Cos(Face.Backdraft)};
				F.Radius = 0.0;
			}
			else if (Ref.SubIndex == kFacingEndEdge)
			{
				// The facing's back-end edge (WP-10, Detect.h PredictFacingEndEdge): two EdgeLines (their end caps are points, so the
				// joint is closed), the lower one in the face plane from the slate to (End, h), the upper one vertical to the cushion
				// top. The second piece carries SourceSub | kSecondPieceBit (HasFeature finds the pair by the first).
				const FacingEndEdge E = MakeFacingEndEdge(Face, FacingEndEdgeTop(Table, Face));
				F.Kind = IslandFeatureKind::EdgeLine;
				F.Point = E.Lower;
				F.Direction = E.LowerDirection;
				F.Length = E.LowerLength;
				F.Normal = ToVec3(Face.Direction);
				F.Radius = 0.0;
				Out[0] = F;
				if (!(E.UpperLength > 0.0))
				{
					return 1;
				}
				F.Point = E.Joint;
				F.Direction = Vec3::UnitZ();
				F.Length = E.UpperLength;
				F.SourceSub = static_cast<std::uint8_t>(kFacingEndEdge | kSecondPieceBit);
				Out[1] = F;
				return 2;
			}
			else
			{
				F.Kind = IslandFeatureKind::EdgeLine;
				F.Normal = ToVec3(Face.PocketNormal);
				F.Radius = 0.0;
			}
			Out[0] = F;
			return 1;
		}
		case TableFeatureKind::RailTop:
		{
			if (Index >= Table.RailTops.Size())
			{
				return 0;
			}
			const RailTopPolygon& Poly = Table.RailTops[Index];
			if (Poly.VertexCount < 3 || Poly.VertexCount > kMaxIslandPlaneVertices)
			{
				return 0;
			}
			F.Kind = IslandFeatureKind::Plane;
			F.Point = Poly.PlanePoint;
			F.Direction = Poly.PlaneNormal;
			F.Normal = Poly.PlaneNormal;
			F.VertexCount = Poly.VertexCount;
			for (int i = 0; i < Poly.VertexCount; ++i)
			{
				F.Vertices[i] = Poly.Vertices[i];
			}
			F.HasCut = Poly.HasCut;
			F.CutCenter = Poly.CutCenter;
			F.CutRadius = Poly.CutRadius;
			RailTopContactParams(F, Params);
			Out[0] = F;
			return 1;
		}
		case TableFeatureKind::RailTopEdge:
		{
			if (Index >= Table.RailTops.Size())
			{
				return 0;
			}
			const RailTopPolygon& Poly = Table.RailTops[Index];
			RailTopContactParams(F, Params);
			if (Ref.SubIndex == kCutRimEdge)
			{
				// Only the flat cap's rim is a horizontal circle; a member reaching the cut of a sloped top leaves the island.
				const bool Flat = Poly.PlaneNormal.x == 0.0 && Poly.PlaneNormal.y == 0.0;
				return Flat ? CutRimFeatures(Poly, F, Out) : 0;
			}
			const int Edge = Ref.SubIndex;
			if (Edge >= Poly.VertexCount)
			{
				return 0;
			}
			Vec3 From[2];
			Vec3 To[2];
			const int Pieces = RailTopEdgePieces(Table, Index, Edge, From, To);
			const Vec2 E = Poly.Vertices[(Edge + 1) % Poly.VertexCount] - Poly.Vertices[Edge];
			const Vec2 Outward = Normalized(Vec2{E.y, -E.x}); // CCW polygon: the interior is on the left
			int Count = 0;
			for (int k = 0; k < Pieces && k < kMaxFeaturePieces; ++k)
			{
				const Vec3 D = To[k] - From[k];
				const double L = Length(D);
				if (!(L > 0.0))
				{
					continue;
				}
				IslandFeature Piece = F;
				Piece.Kind = IslandFeatureKind::EdgeLine;
				Piece.Point = From[k];
				Piece.Direction = D / L;
				Piece.Normal = ToVec3(Outward);
				Piece.Length = L;
				Piece.Radius = 0.0;
				if (Count == 1)
				{
					Piece.SourceSub = static_cast<std::uint8_t>(Piece.SourceSub | kSecondPieceBit);
				}
				Out[Count++] = Piece;
			}
			return Count;
		}
		default:
			return 0; // drop edges, capture circles, pocket interiors and region events are not island contact features
		}
	}

	TableFeatureRef SourceOf(const IslandFeature& Feature)
	{
		TableFeatureRef Ref;
		Ref.Kind = static_cast<TableFeatureKind>(Feature.SourceKind);
		Ref.Index = Feature.SourceIndex;
		Ref.SubIndex = Feature.SourceSub;
		if (Ref.Kind == TableFeatureKind::FacingTopEdge)
		{
			Ref.SubIndex = static_cast<std::uint8_t>(Ref.SubIndex & ~kSecondPieceBit); // the end edge's upper piece (WP-10)
		}
		if (Ref.Kind == TableFeatureKind::RailTopEdge)
		{
			if (Ref.SubIndex == kCutRimEdge - 1)
			{
				Ref.SubIndex = kCutRimEdge;
			}
			else if (Ref.SubIndex != kCutRimEdge)
			{
				Ref.SubIndex = static_cast<std::uint8_t>(Ref.SubIndex & ~kSecondPieceBit);
			}
		}
		return Ref;
	}

	double IslandFeatureGap(const IslandFeature& F, const Vec3& P, double R, Vec3* Normal)
	{
		Vec3 Unused;
		Vec3& N = Normal != nullptr ? *Normal : Unused;
		switch (F.Kind)
		{
		case IslandFeatureKind::EdgeLine:
		{
			const double T = Clamp(Dot(P - F.Point, F.Direction), 0.0, F.Length);
			const Vec3 D = P - (F.Point + F.Direction * T);
			const double Dist = Length(D);
			N = Dist > 0.0 ? D / Dist : F.Normal;
			return Dist - (R + F.Radius);
		}
		case IslandFeatureKind::JawCircle:
		{
			const double Hx = P.x - F.Point.x;
			const double Hy = P.y - F.Point.y;
			const double Rho = Sqrt(Hx * Hx + Hy * Hy);
			if (!(Rho > 0.0))
			{
				return kInfinity;
			}
			double Rel = Atan2(Hy, Hx) - F.AngleFrom;
			Rel -= kTwoPi * Floor(Rel / kTwoPi);
			if (Rel > F.AngleSweep)
			{
				return kInfinity;
			}
			const Vec3 Radial{Hx / Rho, Hy / Rho, 0.0};
			const Vec3 D = P - (F.Point + Radial * F.Radius);
			const double Dist = Length(D);
			N = Dist > 0.0 ? D / Dist : Radial;
			return Dist - R;
		}
		case IslandFeatureKind::FacingPlane:
		{
			const double Sigma = Dot(P - F.Point, F.Normal);
			if (!(Sigma > 0.0))
			{
				return kInfinity;
			}
			const Vec3 Q = P - F.Normal * Sigma;
			const double T = Dot(Q - F.Point, F.Direction);
			if (T < 0.0 || T > F.Length || Q.z < F.ZMin || Q.z > F.ZMax)
			{
				return kInfinity;
			}
			N = F.Normal;
			return Sigma - R;
		}
		case IslandFeatureKind::Plane:
		{
			const double Sigma = Dot(P - F.Point, F.Direction);
			if (!(Sigma > 0.0))
			{
				return kInfinity;
			}
			const Vec3 Q = P - F.Direction * Sigma;
			if (!InsideConvex(F.Vertices, F.VertexCount, XY(Q), 0.0))
			{
				return kInfinity;
			}
			if (F.HasCut && LengthSquared(XY(Q) - F.CutCenter) < F.CutRadius * F.CutRadius)
			{
				return kInfinity;
			}
			N = F.Direction;
			return Sigma - R;
		}
		}
		return kInfinity;
	}

	// ---------------------------------------------------------------------------------------------
	// Landing / pocket geometry
	// ---------------------------------------------------------------------------------------------

	bool OnFrontArc(const PocketGeometry& Pocket, const Vec2& P)
	{
		const Vec2 H = P - Pocket.CaptureCenter;
		if (!(LengthSquared(H) > 0.0))
		{
			return true;
		}
		double Rel = Atan2(H.y, H.x) - Pocket.FrontArcFrom;
		Rel -= kTwoPi * Floor(Rel / kTwoPi);
		return Rel <= Pocket.FrontArcSweep;
	}

	int PocketContaining(const TableGeometry& Table, PocketModel Model, const Vec2& P)
	{
		for (int p = 0; p < Table.Pockets.Size(); ++p)
		{
			const PocketGeometry& Pocket = Table.Pockets[p];
			const double Radius = Model == PocketModel::CaptureCircle ? Pocket.CaptureRadius : Pocket.DropEdgeRadius;
			if (LengthSquared(P - Pocket.CaptureCenter) < Radius * Radius)
			{
				return p;
			}
		}
		return -1;
	}

	Vec3 TurnOffPocketWall(const PocketGeometry& Pocket, const Vec3& Position, const Vec3& Velocity)
	{
		const Vec2 Radial = XY(Position) - Pocket.CaptureCenter;
		const double Rho = Length(Radial);
		const Vec2 Horizontal = XY(Velocity);
		const double Speed = Length(Horizontal);
		if (!(Rho > 0.0) || !(Speed > 0.0))
		{
			return Velocity;
		}
		const Vec2 Out = Radial / Rho;      // from the pocket axis to the center
		const Vec2 Tangent{-Out.y, Out.x};  // the circle's tangent through the center
		const double Inward = -Dot(Horizontal, Out);
		const double Limit = Speed * Sin(kPocketWallMinExitAngle);
		if (!(Inward < Limit && Inward > -Limit))
		{
			return Velocity;
		}
		const double Along = Dot(Horizontal, Tangent) < 0.0 ? -1.0 : 1.0;
		const Vec2 Turned = Tangent * (Along * Speed * Cos(kPocketWallMinExitAngle)) - Out * Limit;
		return Vec3{Turned.x, Turned.y, Velocity.z};
	}

	LandingSurface ClassifyLandingPoint(const TableGeometry& Table, PocketModel Model, const Vec2& P, int& Pocket)
	{
		Pocket = -1;
		for (int p = 0; p < Table.Pockets.Size(); ++p)
		{
			const PocketGeometry& G = Table.Pockets[p];
			const double Rho2 = LengthSquared(P - G.CaptureCenter);
			if (Model == PocketModel::CaptureCircle)
			{
				if (Rho2 < G.CaptureRadius * G.CaptureRadius)
				{
					Pocket = p;
					return LandingSurface::PocketHole;
				}
				continue;
			}
			// Inside the capture circle there is no slate; in the rounded annulus r_p < rho < a_d on the front arc the ball
			// meets the rim below z = R (collisions 6.1 step 1): both continue in PocketFall.
			if (Rho2 < G.CaptureRadius * G.CaptureRadius || (Rho2 < G.DropEdgeRadius * G.DropEdgeRadius && OnFrontArc(G, P)))
			{
				Pocket = p;
				return LandingSurface::PocketHole;
			}
		}
		if (Table.PlayingArea.Contains(P))
		{
			return LandingSurface::PlayingSurface;
		}
		// Outside the nose rectangle the rail-top polygons cover everything up to the outer boundary except the pocket openings
		// and the cut discs (A-GEO-1): a covered point lies under the rail top.
		if (FindRailTopPolygon(Table, P, true, RailTopKind::CushionTop, Vec2{}, 0.0) < 0 && IsOverPocketOpening(Table, P))
		{
			return LandingSurface::Shelf;
		}
		return LandingSurface::OverRail;
	}

	// ---------------------------------------------------------------------------------------------
	// Rail top
	// ---------------------------------------------------------------------------------------------

	int FindRailTopPolygon(const TableGeometry& Table, const Vec2& P, bool AnyKind, RailTopKind Kind, const Vec2& Ahead, double Slack)
	{
		const bool HasAhead = LengthSquared(Ahead) > 0.0;
		int First = -1;
		for (int i = 0; i < Table.RailTops.Size(); ++i)
		{
			const RailTopPolygon& Poly = Table.RailTops[i];
			if (!AnyKind && Poly.Kind != Kind)
			{
				continue;
			}
			if (!InsideConvex(Poly.Vertices, Poly.VertexCount, P, Slack))
			{
				continue;
			}
			if (Poly.HasCut && LengthSquared(P - Poly.CutCenter) < Square(Poly.CutRadius - Slack))
			{
				continue;
			}
			if (First < 0)
			{
				First = i;
			}
			// P could be on a seam shared by two polygons: the one the motion points into wins.
			if (!HasAhead || InsideConvex(Poly.Vertices, Poly.VertexCount, P + Ahead, 0.0))
			{
				return i;
			}
		}
		return First;
	}

	double RailTopHeightAt(const RailTopPolygon& Polygon, const Vec2& P)
	{
		const Vec3& N = Polygon.PlaneNormal;
		const Vec3& X0 = Polygon.PlanePoint;
		if (!(Abs(N.z) > 0.0))
		{
			return X0.z;
		}
		return X0.z - (N.x * (P.x - X0.x) + N.y * (P.y - X0.y)) / N.z;
	}

	bool IsLowRebound(const Vec3& V, const Vec3& Normal, double Gravity, double MinHeight)
	{
		if (!(Normal.z > 0.2))
		{
			return false;
		}
		const double Vn = Dot(V, Normal);
		if (!(Vn > 0.0))
		{
			return true; // no rebound at all (resting contact)
		}
		return Vn * Vn < 2.0 * Gravity * Normal.z * MinHeight;
	}
}
