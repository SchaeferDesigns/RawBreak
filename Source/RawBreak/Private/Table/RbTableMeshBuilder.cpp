#include "Table/RbTableMeshBuilder.h"

#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"

#include "CompGeom/Delaunay2.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTangents.h"
#include "IndexTypes.h"
#include "VectorUtil.h"

// Owner: UE-1. Table geometry -> closed, UE-oriented FDynamicMesh3 parts (header: sources, frames, conventions).
//
// Construction in the CORE frame (metres, right-handed, +x foot, +y left, z up) with the physics' own numbers; the
// writer converts every point with the FRbCoords mirror and fixes each triangle's winding from the intended outward
// normal, so no construction code has to think about the handedness flip. Solids are assembled face by face (planar
// polygons triangulated by constrained Delaunay, ruled strips, sampled cylinders / tori); vertices are welded per
// solid (1 um), so a solid is closed exactly when its faces share their boundary samples.
//
// The helpers live in a NAMED namespace: the module builds as unity blobs, where anonymous namespaces of different files merge.

namespace RbTableMeshBuilderPrivate
{
	using namespace UE::Geometry;
	using FV2 = FVector2d;
	using FV3 = FVector3d;

	constexpr double kPi = UE_DOUBLE_PI;
	constexpr double kTwoPi = 2.0 * UE_DOUBLE_PI;
	constexpr double kWeldCellCm = 1e-3;  // weld grid cell [cm]
	constexpr double kWeldTolCm = 1e-4;   // 1 um: distinct features of the table are >= 0.1 mm apart

	// --- small 2D / 3D helpers (core frame) -----------------------------------------------------------------------

	FV2 V2(const rb::Vec2& V) { return FV2(V.x, V.y); }
	FV3 At(const FV2& P, double Z) { return FV3(P.X, P.Y, Z); }
	FV2 Flat(const FV3& P) { return FV2(P.X, P.Y); }
	double Dot2(const FV2& A, const FV2& B) { return A.X * B.X + A.Y * B.Y; }
	double Cross2(const FV2& A, const FV2& B) { return A.X * B.Y - A.Y * B.X; }
	double Len2(const FV2& A) { return FMath::Sqrt(Dot2(A, A)); }
	FV2 Norm2(const FV2& A) { const double L = Len2(A); return L > 0.0 ? A / L : FV2::ZeroVector; }
	FV2 PerpCcw(const FV2& V) { return FV2(-V.Y, V.X); }
	FV2 RightOf(const FV2& D) { return FV2(D.Y, -D.X); }
	FV2 Dir(double Angle) { return FV2(FMath::Cos(Angle), FMath::Sin(Angle)); }
	double AngleOf(const FV2& V) { return FMath::Atan2(V.Y, V.X); }

	// (-pi, pi]
	double WrapPi(double A)
	{
		double X = FMath::Fmod(A + kPi, kTwoPi);
		if (X <= 0.0)
		{
			X += kTwoPi;
		}
		return X - kPi;
	}

	// (0, 2 pi]
	double WrapPositive(double A)
	{
		double X = FMath::Fmod(A, kTwoPi);
		if (X <= 0.0)
		{
			X += kTwoPi;
		}
		return X;
	}

	double SignedArea(const TArray<FV2>& Loop)
	{
		double A = 0.0;
		for (int32 i = 0; i < Loop.Num(); ++i)
		{
			A += Cross2(Loop[i], Loop[(i + 1) % Loop.Num()]);
		}
		return 0.5 * A;
	}

	bool PointInPolygon(const TArray<FV2>& Poly, const FV2& P)
	{
		bool bInside = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const FV2& A = Poly[i];
			const FV2& B = Poly[j];
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	// Line P + t Dir (Dir unit) against the circle (C, R).
	bool LineCircle(const FV2& P, const FV2& D, const FV2& C, double R, double& T0, double& T1)
	{
		const FV2 F = P - C;
		const double B = Dot2(F, D);
		const double Cc = Dot2(F, F) - R * R;
		const double Disc = B * B - Cc;
		if (Disc < 0.0)
		{
			return false;
		}
		const double S = FMath::Sqrt(Disc);
		T0 = -B - S;
		T1 = -B + S;
		return true;
	}

	// Solves A1 . X = B1, A2 . X = B2.
	bool Solve2(const FV2& A1, double B1, const FV2& A2, double B2, FV2& Out)
	{
		const double Det = A1.X * A2.Y - A1.Y * A2.X;
		if (FMath::Abs(Det) < 1e-14)
		{
			return false;
		}
		Out = FV2((B1 * A2.Y - A1.Y * B2) / Det, (A1.X * B2 - B1 * A2.X) / Det);
		return true;
	}

	// Angles from From to To (either direction) including both ends and every "must" angle in between, plus the grid
	// Origin + i Step strictly inside, skipping grid angles closer than 0.25 Step to an included angle (no slivers).
	TArray<double> ArcParams(double From, double To, double Origin, double Step, TArrayView<const double> Must = {})
	{
		const double Lo = FMath::Min(From, To);
		const double Hi = FMath::Max(From, To);
		TArray<double> Fixed = {From, To};
		for (const double M : Must)
		{
			if (M > Lo && M < Hi)
			{
				Fixed.Add(M);
			}
		}
		TArray<double> Out = Fixed;
		const int64 I0 = FMath::CeilToInt64((Lo - Origin) / Step);
		const int64 I1 = FMath::FloorToInt64((Hi - Origin) / Step);
		for (int64 i = I0; i <= I1; ++i)
		{
			const double A = Origin + static_cast<double>(i) * Step;
			bool bNear = false;
			for (const double F : Fixed)
			{
				bNear = bNear || FMath::Abs(A - F) < 0.25 * Step;
			}
			if (!bNear && A > Lo && A < Hi)
			{
				Out.Add(A);
			}
		}
		Out.Sort();
		if (From > To)
		{
			Algo::Reverse(Out);
		}
		return Out;
	}

	// Bisection of a boolean predicate between In (true) and Out (false); returns the parameter on the In side.
	double Bisect(double In, double Out, TFunctionRef<bool(double)> Pred)
	{
		for (int32 i = 0; i < 80; ++i)
		{
			const double Mid = 0.5 * (In + Out);
			if (Pred(Mid))
			{
				In = Mid;
			}
			else
			{
				Out = Mid;
			}
		}
		return In;
	}

	// ============================================================================================================
	// Mesh writer: core-frame input, FRbCoords mirror, per-solid welding, UE-convention winding from the normals.
	// ============================================================================================================

	enum class EUVMode : uint8
	{
		Box,       // dominant-axis projection of the triangle normal (walls: V down)
		PlanarXY,  // (X, Y) / 100: grain along UE X (long rails)
		PlanarYX,  // (Y, X) / 100: grain along UE Y (end rails)
	};

	class FMeshWriter
	{
	public:
		explicit FMeshWriter(FDynamicMesh3& InMesh)
			: Mesh(InMesh)
		{
			Mesh.Clear();
			Mesh.EnableAttributes();
			Normals = Mesh.Attributes()->PrimaryNormals();
			UVs = Mesh.Attributes()->PrimaryUV();
		}

		// Starts a new solid: welding never joins vertices of different solids (touching solids stay manifold).
		void BeginSolid() { Grid.Reset(); }

		void SetUVMode(EUVMode Mode) { UVMode = Mode; }

		const FString& GetError() const { return Error; }
		bool Ok() const { return Error.IsEmpty(); }
		void Fail(const FString& Message)
		{
			if (Error.IsEmpty())
			{
				Error = Message;
			}
		}

		// Positions [m] and unit outward normals in the core frame. The winding is chosen so that UE's triangle normal
		// (VectorUtil::Normal) agrees with the intended normals; collapsed / zero-area triangles are skipped.
		void Tri(const FV3& A, const FV3& B, const FV3& C, const FV3& NA, const FV3& NB, const FV3& NC)
		{
			FV3 P[3] = {ToUE(A), ToUE(B), ToUE(C)};
			FV3 N[3] = {DirToUE(NA), DirToUE(NB), DirToUE(NC)};
			int32 V[3] = {Weld(P[0]), Weld(P[1]), Weld(P[2])};
			if (V[0] == V[1] || V[1] == V[2] || V[0] == V[2])
			{
				return;
			}
			P[0] = Mesh.GetVertex(V[0]);
			P[1] = Mesh.GetVertex(V[1]);
			P[2] = Mesh.GetVertex(V[2]);
			FV3 G = VectorUtil::NormalDirection(P[0], P[1], P[2]);
			const double Area2 = G.Size();
			if (Area2 < 1e-10)
			{
				return;
			}
			if (G.Dot(N[0] + N[1] + N[2]) < 0.0)
			{
				Swap(P[1], P[2]);
				Swap(N[1], N[2]);
				Swap(V[1], V[2]);
				G = -G;
			}
			const int32 Tid = Mesh.AppendTriangle(V[0], V[1], V[2]);
			if (Tid < 0)
			{
				Fail(FString::Printf(TEXT("non-manifold triangle (%d) at (%.4f, %.4f, %.4f) cm"), Tid, P[0].X, P[0].Y, P[0].Z));
				return;
			}
			const FV3 Face = G / Area2;
			Normals->SetTriangle(Tid, FIndex3i(NormalElement(V[0], N[0]), NormalElement(V[1], N[1]), NormalElement(V[2], N[2])));
			UVs->SetTriangle(Tid, FIndex3i(UVElement(V[0], ComputeUV(P[0], Face)), UVElement(V[1], ComputeUV(P[1], Face)),
				UVElement(V[2], ComputeUV(P[2], Face))));
		}

		void Quad(const FV3& A, const FV3& B, const FV3& C, const FV3& D, const FV3& NA, const FV3& NB, const FV3& NC, const FV3& ND)
		{
			// Split along the shorter diagonal (better for non-planar strips).
			if (FV3::DistSquared(A, C) <= FV3::DistSquared(B, D))
			{
				Tri(A, B, C, NA, NB, NC);
				Tri(A, C, D, NA, NC, ND);
			}
			else
			{
				Tri(A, B, D, NA, NB, ND);
				Tri(B, C, D, NB, NC, ND);
			}
		}

		// Band between two polylines A and B running the same way, advanced by their parameters (e.g. angles).
		void Zip(const TArray<FV3>& A, const TArray<FV3>& NA, const TArray<double>& TA, const TArray<FV3>& B, const TArray<FV3>& NB,
			const TArray<double>& TB)
		{
			const double Sign = (TA.Num() > 1 ? TA.Last() - TA[0] : (TB.Num() > 1 ? TB.Last() - TB[0] : 1.0)) >= 0.0 ? 1.0 : -1.0;
			int32 i = 0;
			int32 j = 0;
			while (i < A.Num() - 1 || j < B.Num() - 1)
			{
				bool bAdvanceA;
				if (i == A.Num() - 1)
				{
					bAdvanceA = false;
				}
				else if (j == B.Num() - 1)
				{
					bAdvanceA = true;
				}
				else
				{
					bAdvanceA = Sign * TA[i + 1] <= Sign * TB[j + 1];
				}
				if (bAdvanceA)
				{
					Tri(A[i], A[i + 1], B[j], NA[i], NA[i + 1], NB[j]);
					++i;
				}
				else
				{
					Tri(A[i], B[j + 1], B[j], NA[i], NB[j + 1], NB[j]);
					++j;
				}
			}
		}

		// Planar polygon (first loop = outer, others = holes) with one normal: constrained Delaunay, odd-winding fill.
		void Polygon(const TArray<TArray<FV3>>& Loops, const FV3& Normal)
		{
			const FV3 N = Normal.GetSafeNormal();
			FV3 U = FMath::Abs(N.Z) < 0.9 ? FV3(0.0, 0.0, 1.0).Cross(N) : FV3(1.0, 0.0, 0.0).Cross(N);
			U.Normalize();
			const FV3 V = N.Cross(U);
			TArray<FV2> Points;
			TArray<FIndex2i> Edges;
			TArray<FV3> Points3;
			for (const TArray<FV3>& Loop : Loops)
			{
				const int32 Base = Points.Num();
				const int32 Count = Loop.Num();
				if (Count < 3)
				{
					Fail(TEXT("polygon loop with fewer than 3 points"));
					return;
				}
				for (int32 i = 0; i < Count; ++i)
				{
					Points3.Add(Loop[i]);
					Points.Add(FV2(Loop[i].Dot(U), Loop[i].Dot(V)) * FRbCoords::CmPerMeter);
					Edges.Add(FIndex2i(Base + i, Base + (i + 1) % Count));
				}
			}
			FDelaunay2 Delaunay;
			if (!Delaunay.Triangulate(Points, Edges))
			{
				Fail(FString::Printf(TEXT("constrained Delaunay failed (%d points, first (%.5f, %.5f, %.5f) m)"), Points.Num(), Points3[0].X,
					Points3[0].Y, Points3[0].Z));
				return;
			}
			TArray<FIndex3i> Triangles;
			if (!Delaunay.GetFilledTriangles(Triangles, Edges, FDelaunay2::EFillMode::OddWinding))
			{
				Fail(TEXT("polygon fill failed"));
				return;
			}
			for (const FIndex3i& T : Triangles)
			{
				Tri(Points3[T.A], Points3[T.B], Points3[T.C], N, N, N);
			}
		}

		void Finish()
		{
			if (Ok() && Mesh.TriangleCount() > 0)
			{
				FMeshTangentsd::ComputeDefaultOverlayTangents(Mesh);
			}
		}

	private:
		static FV3 ToUE(const FV3& P) { return FV3(FRbCoords::CmPerMeter * P.X, -FRbCoords::CmPerMeter * P.Y, FRbCoords::CmPerMeter * P.Z); }
		static FV3 DirToUE(const FV3& D) { return FV3(D.X, -D.Y, D.Z).GetSafeNormal(); }

		int32 Weld(const FV3& P)
		{
			const FIntVector Key(FMath::FloorToInt32(P.X / kWeldCellCm), FMath::FloorToInt32(P.Y / kWeldCellCm), FMath::FloorToInt32(P.Z / kWeldCellCm));
			for (int32 Dz = -1; Dz <= 1; ++Dz)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						if (const TArray<int32>* Bucket = Grid.Find(Key + FIntVector(Dx, Dy, Dz)))
						{
							for (const int32 Vid : *Bucket)
							{
								if (FV3::DistSquared(Mesh.GetVertex(Vid), P) < kWeldTolCm * kWeldTolCm)
								{
									return Vid;
								}
							}
						}
					}
				}
			}
			const int32 Vid = Mesh.AppendVertex(P);
			Grid.FindOrAdd(Key).Add(Vid);
			return Vid;
		}

		int32 NormalElement(int32 Vid, const FV3& N)
		{
			if (VertexNormals.Num() <= Vid)
			{
				VertexNormals.SetNum(Vid + 1);
			}
			const FVector3f Nf(N);
			for (const TPair<FVector3f, int32>& E : VertexNormals[Vid])
			{
				if (FVector3f::DistSquared(E.Key, Nf) < 1e-10f)
				{
					return E.Value;
				}
			}
			const int32 Id = Normals->AppendElement(Nf);
			VertexNormals[Vid].Add({Nf, Id});
			return Id;
		}

		int32 UVElement(int32 Vid, const FVector2f& UV)
		{
			if (VertexUVs.Num() <= Vid)
			{
				VertexUVs.SetNum(Vid + 1);
			}
			for (const TPair<FVector2f, int32>& E : VertexUVs[Vid])
			{
				if (FVector2f::DistSquared(E.Key, UV) < 1e-12f)
				{
					return E.Value;
				}
			}
			const int32 Id = UVs->AppendElement(UV);
			VertexUVs[Vid].Add({UV, Id});
			return Id;
		}

		// 1 UV unit = 1 m (world-scale cm / 100).
		FVector2f ComputeUV(const FV3& P, const FV3& Face) const
		{
			const double S = 1.0 / FRbCoords::CmPerMeter;
			const FV3 A(FMath::Abs(Face.X), FMath::Abs(Face.Y), FMath::Abs(Face.Z));
			const bool bTop = A.Z >= A.X && A.Z >= A.Y;
			if (bTop && UVMode == EUVMode::PlanarYX)
			{
				return FVector2f(static_cast<float>(P.Y * S), static_cast<float>(P.X * S));
			}
			if (bTop)
			{
				return FVector2f(static_cast<float>(P.X * S), static_cast<float>(P.Y * S));
			}
			if (A.X >= A.Y)
			{
				return FVector2f(static_cast<float>(P.Y * S), static_cast<float>(-P.Z * S));
			}
			return FVector2f(static_cast<float>(P.X * S), static_cast<float>(-P.Z * S));
		}

		FDynamicMesh3& Mesh;
		FDynamicMeshNormalOverlay* Normals = nullptr;
		FDynamicMeshUVOverlay* UVs = nullptr;
		TMap<FIntVector, TArray<int32>> Grid;
		TArray<TArray<TPair<FVector3f, int32>, TInlineAllocator<4>>> VertexNormals;
		TArray<TArray<TPair<FVector2f, int32>, TInlineAllocator<4>>> VertexUVs;
		EUVMode UVMode = EUVMode::Box;
		FString Error;
	};

	// Closed 2D loop with per-edge smoothness for vertical walls (prisms).
	struct FWallLoop
	{
		TArray<FV2> P;
		TArray<FV2> PointNormal; // outward (from the solid) horizontal normals, used on smooth edges
		TArray<bool> EdgeSmooth; // edge i: P[i] -> P[i + 1]

		void Add(const FV2& Point, bool bSmoothToNext = false, const FV2& Normal = FV2::ZeroVector)
		{
			P.Add(Point);
			PointNormal.Add(Normal);
			EdgeSmooth.Add(bSmoothToNext);
		}
	};

	// Vertical walls of a loop from Z0 to Z1. bSolidOnLeft: the solid lies left of the loop direction. TopBand > 0 adds a
	// ring TopBand below Z1 whose upper strip carries the top (+z) normal: a rounded-edge look with exact geometry.
	void AddWalls(FMeshWriter& W, const FWallLoop& L, bool bSolidOnLeft, double Z0, double Z1, double TopBand = 0.0)
	{
		const int32 Num = L.P.Num();
		const FV3 Up(0.0, 0.0, 1.0);
		for (int32 i = 0; i < Num; ++i)
		{
			const FV2& A = L.P[i];
			const FV2& B = L.P[(i + 1) % Num];
			FV3 NA;
			FV3 NB;
			if (L.EdgeSmooth[i])
			{
				NA = At(L.PointNormal[i], 0.0);
				NB = At(L.PointNormal[(i + 1) % Num], 0.0);
			}
			else
			{
				const FV2 D = Norm2(B - A);
				const FV2 Out = bSolidOnLeft ? RightOf(D) : -RightOf(D);
				NA = NB = At(Out, 0.0);
			}
			if (TopBand > 0.0 && TopBand < Z1 - Z0)
			{
				const double Zb = Z1 - TopBand;
				W.Quad(At(A, Z0), At(B, Z0), At(B, Zb), At(A, Zb), NA, NB, NB, NA);
				W.Quad(At(A, Zb), At(B, Zb), At(B, Z1), At(A, Z1), NA, NB, Up, Up);
			}
			else
			{
				W.Quad(At(A, Z0), At(B, Z0), At(B, Z1), At(A, Z1), NA, NB, NB, NA);
			}
		}
	}

	TArray<FV3> Lift(const TArray<FV2>& Loop, double Z)
	{
		TArray<FV3> Out;
		Out.Reserve(Loop.Num());
		for (const FV2& P : Loop)
		{
			Out.Add(At(P, Z));
		}
		return Out;
	}

	// Horizontal face at Z (outer + holes), normal up or down.
	void AddFlat(FMeshWriter& W, const TArray<FV2>& Outer, const TArray<TArray<FV2>>& Holes, double Z, bool bUp)
	{
		TArray<TArray<FV3>> Loops;
		Loops.Add(Lift(Outer, Z));
		for (const TArray<FV2>& H : Holes)
		{
			Loops.Add(Lift(H, Z));
		}
		W.Polygon(Loops, FV3(0.0, 0.0, bUp ? 1.0 : -1.0));
	}

	// Closed box prism over a CCW rectangle.
	void AddBox(FMeshWriter& W, const FV2& Lo, const FV2& Hi, double Z0, double Z1)
	{
		W.BeginSolid();
		const TArray<FV2> Loop = {Lo, FV2(Hi.X, Lo.Y), Hi, FV2(Lo.X, Hi.Y)};
		AddFlat(W, Loop, {}, Z1, true);
		AddFlat(W, Loop, {}, Z0, false);
		FWallLoop L;
		for (const FV2& P : Loop)
		{
			L.Add(P);
		}
		AddWalls(W, L, true, Z0, Z1);
	}

	// ============================================================================================================
	// Layout: every shared computation (cushion ends with their pocket cuts, rectangle / circle arrangement)
	// ============================================================================================================

	struct FPocket
	{
		FV2 C;
		double Rp = 0.0;
		double Ad = 0.0;
		double Rd = 0.0;
		double ThetaRef = 0.0; // front angle (toward the table) about C
		FV2 Axis;
	};

	struct FCutColumn
	{
		double Theta = 0.0;
		FV3 Low;
		FV3 High;
	};

	struct FCushionFrame
	{
		FV2 NoseStart;
		FV2 D;
		FV2 N; // inward normal (toward the table)
		double H = 0.0;
		double Zc = 0.0;
		double Cw = 0.0;
		double K = 0.0; // cushion-top slope (Zc - h) / Cw
		double EFace = 0.0;
		FV3 TopNormal;
		FV3 FaceNormal;
		FV3 BackNormal;

		double Behind(const FV2& P) const { return -Dot2(P - NoseStart, N); }
		double TopZ(const FV2& P) const { return H + K * Behind(P); }
	};

	struct FJawEnd
	{
		int32 Pocket = -1;
		double SideSign = 0.0; // side of this end about the pocket centre (sign of phi)
		// facing
		FV2 S;
		FV2 F;
		FV2 Pn;
		double TanB = 0.0;
		FV3 FacingNormal;
		FV2 BottomLinePoint; // facing bottom line (z = 0): BottomLinePoint + t F
		// rulings nose -> facing
		TArray<FV3> Top;
		TArray<FV3> Band;
		TArray<FV3> Bottom;
		TArray<FV3> RulingNormal;
		FV2 ArcCenter;
		double JawRadius = 0.0;
		// facing top edge, cut through the cushion
		bool bCut = false;
		bool bBot = false;
		bool bFacingBand = false;
		double ThetaBot = 0.0;
		TArray<FCutColumn> Columns;
		FV3 E;
		FV3 EBand;
		TArray<FV3> FacingCurve;     // (EBand or E) down to the bottom-edge end at z = 0
		TArray<FV3> BackSide;        // back plane: z = 0 -> Zc
		TArray<FV3> TopBackChain;    // top face: E -> back point
		TArray<FV3> BottomBackChain; // bottom face: bottom-edge end -> back point
	};

	struct FCushion
	{
		FCushionFrame Frame;
		FJawEnd A; // start (outgoing jaw of pocket k)
		FJawEnd B; // end (incoming jaw of pocket k + 1)
	};

	// Arrangement of the cushion-back rectangle with the capture circles (r_p).
	struct FCross
	{
		int32 Pocket = -1;
		int32 Edge = -1;
		double T = 0.0;
		FV2 P;
		double Phi = 0.0; // WrapPi(theta - ThetaRef)
	};

	struct FRectPiece
	{
		int32 Start = -1; // crossing ids
		int32 End = -1;
		TArray<FV2> Points; // crossing, corners, crossing
	};

	struct FCircleArc
	{
		int32 Pocket = -1;
		int32 StartCross = -1;
		int32 EndCross = -1;
		double From = 0.0;  // phi of StartCross
		double Sweep = 0.0; // CCW
		bool bInside = false;
		bool bFront = false;
	};

	struct FLoopElement
	{
		bool bArc = false;
		int32 Index = -1;
		bool bForward = true; // arcs: CCW from StartCross
	};

	// Drop-edge structure of a pocket's front arc on the bed (the open, rounded range and its trims).
	struct FFrontSide
	{
		bool bTrimRect = true;  // trimmed by the rectangle edge (no plain range under a cushion)
		FV2 TrimPoint;          // trim line point
		FV2 TrimDir;            // trim line direction (unit)
		FV2 TrimNormal;         // horizontal normal toward the open side
		double PhiRect = 0.0;   // rectangle crossing
		double PhiRpTrim = 0.0; // r_p circle on the trim line (== PhiRect for rectangle trims)
		TArray<double> RingPhi; // ring k trim angle
	};

	struct FFrontArc
	{
		int32 Arc = -1;
		FFrontSide Side[2]; // 0 = phi < 0 (L), 1 = phi > 0 (R)
		TArray<TArray<double>> RingParams; // ring k angles (increasing phi)
	};

	// The cushion-back rectangle arranged with one circle per pocket (centre = CaptureCenter, radius per pocket).
	struct FArrangement
	{
		double Radius[rb::kPocketCount] = {};
		TArray<FCross> Crosses;
		TArray<FRectPiece> RectPieces;
		TArray<FCircleArc> Arcs;
		TArray<TArray<FLoopElement>> BedLoops; // rectangle minus the discs: [0] = main (with the front arcs), others = islands behind holes
		TArray<FLoopElement> UnionLoop;        // outline of the rectangle united with the discs
	};

	// Liner collar behind a hole (one per outside arc of the hole arrangement): the pocket's back wall where the capture
	// cylinder r_p cuts through the rail, lined from r_p out to r_p + LinerT; the rail ring is cut along its outer path.
	struct FCollarSide
	{
		FV2 Cross;           // the r_p circle's crossing with the cushion-back rectangle edge
		FV2 Anchor;          // where the collar begins on that edge: the r_p + LinerT crossing, or further out where an
		                     // undercut facing's bottom line meets the cushion back before the hole (the exposed stretch)
		bool bStrip = false; // the collar runs along the edge from Anchor (a strip LinerT thick) to the outer circle
		FV2 AnchorOut;       // Anchor moved out of the rectangle by LinerT (strip only)
		FV2 OnCircle;        // where the collar's outer boundary reaches the r_p + LinerT circle
		double Phi = 0.0;    // angle of OnCircle (unwrapped next to the hole arc's end)
	};

	struct FCollar
	{
		int32 Pocket = -1;
		int32 Arc = -1;                // outside arc of the hole arrangement
		FCollarSide Side[2];           // 0 = at the arc's StartCross, 1 = at its EndCross
		TArray<double> OuterParams;    // r_p + LinerT arc, Side[0].Phi .. Side[1].Phi (increasing)
		TArray<double> InnerParams;    // the hole arc (increasing)
		// Outer path Side[0] -> Side[1]: [Anchor, AnchorOut,] arc points [, AnchorOut, Anchor]; EdgeSmooth[i] = edge i -> i + 1
		// lies on the circle; Radial[i] = outward radial direction (away from the pocket centre) on arc points, else zero.
		TArray<FV2> OuterPath;
		TArray<FV2> Radial;
		TArray<bool> EdgeSmooth;
	};

	struct FLayout
	{
		const rb::TableGeometry* G = nullptr;
		FRbTableMeshOptions O;
		double H = 0.0, Zc = 0.0, T = 0.0, Cw = 0.0, Rw = 0.0, Hl = 0.0, Hw = 0.0, BedHeight = 0.0, NoseBand = 0.0, EFace = 0.0;
		double HlC = 0.0, HwC = 0.0, HlR = 0.0, HwR = 0.0;
		double Step = 0.0; // pocket circle grid step [rad]
		double ApronDepth = 0.0, CapT = 0.0, SkirtT = 0.0, SightDepth = 0.0, LinerT = 0.0, CapBand = 0.0;
		bool bCabinet = false;
		FPocket Pockets[rb::kPocketCount];
		FCushion Cushions[rb::kCushionCount];
		FV2 RectCorner[4];
		// The cushion-back rectangle with the capture circles r_p (bed, corner islands, fronts, collars).
		FArrangement Hole;
		TArray<FFrontArc> Fronts; // per pocket
		// Liner collars and, per hole crossing id, the collar side that starts there ((collar, side), -1 if none).
		TArray<FCollar> Collars;
		TArray<FIntPoint> CollarAtCross;
		// cap / body inner boundary (rectangle with the collars' outer paths) and the four mitre indices
		FWallLoop Union;
		int32 Mitre[4] = {-1, -1, -1, -1};

		FV2 CirclePoint(int32 Pocket, double Phi, double R) const { return Pockets[Pocket].C + Dir(Pockets[Pocket].ThetaRef + Phi) * R; }
		FV2 InwardFromCircle(int32 Pocket, double Phi) const { return -Dir(Pockets[Pocket].ThetaRef + Phi); } // toward C
		double CollarRadius(int32 Pocket) const { return Pockets[Pocket].Rp + LinerT; }
	};

	// --- cushion ends --------------------------------------------------------------------------------------------

	struct FCutEval
	{
		bool bInside = false;
		double Low = 0.0;
		double High = 0.0;
		double FaceZ = 0.0;
		double Behind = 0.0;
		bool bRubberBinding = false;
	};

	FCutEval EvalCut(const FCushionFrame& Fr, const FJawEnd& J, const FV2& Q)
	{
		FCutEval R;
		R.Behind = Fr.Behind(Q);
		R.High = Fr.TopZ(Q);
		const double Sf = Dot2(Q - J.S, J.Pn); // toward the opening
		R.FaceZ = J.TanB > 1e-12 ? Fr.H + Sf / J.TanB : (Sf > 0.0 ? 1e9 : -1e9);
		const double Rub = R.Behind < Fr.EFace ? Fr.H * (1.0 - R.Behind / Fr.EFace) : -1e9;
		R.Low = FMath::Max3(0.0, R.FaceZ, Rub);
		R.bRubberBinding = Rub > FMath::Max(0.0, R.FaceZ);
		R.bInside = R.Behind >= 0.0 && R.Behind <= Fr.Cw && R.High - R.Low > 0.0;
		return R;
	}

	bool BuildJawEnd(const FLayout& L, const FCushionFrame& Fr, int32 JawIndex, const TArray<FV2>& TopArc, FJawEnd& J, FString& Err)
	{
		const rb::TableGeometry& G = *L.G;
		const rb::Facing& Fa = G.Facings[JawIndex];
		const rb::JawArc& Arc = G.JawArcs[JawIndex];
		J.Pocket = JawIndex / 2;
		const FPocket& Pk = L.Pockets[J.Pocket];
		J.S = V2(Fa.Start);
		J.F = V2(Fa.Direction);
		J.Pn = V2(Fa.PocketNormal);
		J.TanB = FMath::Tan(Fa.Backdraft);
		J.FacingNormal = FV3(J.Pn.X * FMath::Cos(Fa.Backdraft), J.Pn.Y * FMath::Cos(Fa.Backdraft), -FMath::Sin(Fa.Backdraft));
		const double EFb = Fr.H * J.TanB;
		J.BottomLinePoint = J.S - J.Pn * EFb;
		J.ArcCenter = V2(Arc.Center);
		J.JawRadius = Arc.Radius;

		// Rulings: top arc at h (BuildNoseOutline samples), bottom arc of the same radius tangent to the face bottom line and
		// the facing bottom line, at the same angles; the blend between them is ruled.
		FV2 CenterB;
		if (!Solve2(Fr.N, Dot2(Fr.N, Fr.NoseStart) - (J.JawRadius + Fr.EFace), J.Pn, Dot2(J.Pn, J.S) - (J.JawRadius + EFb), CenterB))
		{
			Err = TEXT("jaw bottom centre: parallel lines");
			return false;
		}
		const int32 Num = TopArc.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			const FV3 Top = At(TopArc[i], Fr.H);
			FV3 Bottom;
			if (J.JawRadius > 0.0 && Num > 1)
			{
				Bottom = At(CenterB + Dir(AngleOf(TopArc[i] - J.ArcCenter)) * J.JawRadius, 0.0);
			}
			else
			{
				Bottom = At(CenterB, 0.0);
			}
			J.Top.Add(Top);
			J.Bottom.Add(Bottom);
			J.Band.Add(Top + (Bottom - Top).GetSafeNormal() * L.NoseBand);
			FV3 N;
			if (i == 0)
			{
				N = Fr.FaceNormal;
			}
			else if (i == Num - 1)
			{
				N = J.FacingNormal;
			}
			else
			{
				const FV2 Radial = TopArc[i] - J.ArcCenter;
				N = At(PerpCcw(Radial), 0.0).Cross(Bottom - Top).GetSafeNormal();
				if (N.Dot(At(Radial, 0.0)) < 0.0)
				{
					N = -N;
				}
			}
			J.RulingNormal.Add(N);
		}

		// Side of this end about the pocket centre (the facing end lies on it).
		const FV2 FacingEnd = V2(Fa.End);
		J.SideSign = WrapPi(AngleOf(FacingEnd - Pk.C) - Pk.ThetaRef) < 0.0 ? -1.0 : 1.0;

		// Band line on the facing: parallel to the top edge through the last ruling's band vertex.
		const FV3 JTop = J.Top.Last();
		const FV3 JBand = J.Band.Last();

		// --- pocket cut: the capture cylinder r_p through the cushion material ------------------------------------
		const int32 M = 2048;
		const double Dt = kTwoPi / M;
		auto Q = [&Pk](double Theta) { return Pk.C + Dir(Theta) * Pk.Rp; };
		auto Inside = [&](double Theta) { return EvalCut(Fr, J, Q(Theta)).bInside; };
		int32 StartIdx = -1;
		for (int32 i = 0; i < M; ++i)
		{
			if (!Inside(Pk.ThetaRef + i * Dt))
			{
				StartIdx = i;
				break;
			}
		}
		if (StartIdx < 0)
		{
			Err = TEXT("pocket circle entirely inside a cushion");
			return false;
		}
		int32 RunCount = 0;
		int32 RunFirst = -1;
		int32 RunLast = -1;
		bool bPrev = false;
		for (int32 k = 1; k <= M; ++k)
		{
			const int32 i = StartIdx + k;
			const bool bIn = Inside(Pk.ThetaRef + i * Dt);
			if (bIn && !bPrev)
			{
				++RunCount;
				RunFirst = i;
			}
			if (bIn)
			{
				RunLast = i;
			}
			bPrev = bIn;
		}
		if (RunCount > 1)
		{
			Err = FString::Printf(TEXT("pocket %d cuts cushion jaw %d in %d separate places"), J.Pocket, JawIndex, RunCount);
			return false;
		}
		J.bCut = RunCount == 1;

		const FV2 BackPoint = Fr.NoseStart - Fr.N * Fr.Cw;
		// Facing plane point at height z on the back plane (facing x back line).
		auto FacingOnBack = [&](double Z, FV3& Out) {
			FV2 P;
			if (!Solve2(Fr.N, Dot2(Fr.N, BackPoint), J.Pn, Dot2(J.Pn, J.S) + (Z - Fr.H) * J.TanB, P))
			{
				return false;
			}
			Out = At(P, Z);
			return true;
		};

		if (!J.bCut)
		{
			FV3 TopEnd;
			FV3 BottomEnd;
			if (!FacingOnBack(Fr.Zc, TopEnd) || !FacingOnBack(0.0, BottomEnd))
			{
				Err = TEXT("facing parallel to the cushion back");
				return false;
			}
			J.E = TopEnd;
			// Band point on the facing x back line.
			const FV3 Te = (J.E - JTop).GetSafeNormal();
			FV3 Mv = J.FacingNormal.Cross(Te).GetSafeNormal();
			if (Mv.Dot(JTop - JBand) < 0.0)
			{
				Mv = -Mv;
			}
			const double Den = (TopEnd - BottomEnd).Dot(Mv);
			const double U = FMath::Abs(Den) > 1e-12 ? (TopEnd - JBand).Dot(Mv) / Den : -1.0;
			J.bFacingBand = U > 1e-6 && U < 1.0 - 1e-6;
			if (J.bFacingBand)
			{
				J.EBand = TopEnd + (BottomEnd - TopEnd) * U;
				J.FacingCurve = {J.EBand, BottomEnd};
				J.BackSide = {BottomEnd, J.EBand, TopEnd};
			}
			else
			{
				J.FacingCurve = {J.E, BottomEnd};
				J.BackSide = {BottomEnd, TopEnd};
			}
			J.TopBackChain = {TopEnd};
			J.BottomBackChain = {BottomEnd};
			return true;
		}

		// Refine the run ends; classify them (facing top edge vs back line).
		const double ThA = Bisect(Pk.ThetaRef + RunFirst * Dt, Pk.ThetaRef + (RunFirst - 1) * Dt, Inside);
		const double ThB = Bisect(Pk.ThetaRef + RunLast * Dt, Pk.ThetaRef + (RunLast + 1) * Dt, Inside);
		auto IsBack = [&](double Theta) { return FMath::Abs(EvalCut(Fr, J, Q(Theta)).Behind - Fr.Cw) < 1e-8; };
		auto IsTop = [&](double Theta) {
			const FCutEval Ev = EvalCut(Fr, J, Q(Theta));
			return Ev.High - Ev.Low < 1e-8;
		};
		double ThTop;
		double ThBack;
		if (IsTop(ThA) && IsBack(ThB))
		{
			ThTop = ThA;
			ThBack = ThB;
		}
		else if (IsTop(ThB) && IsBack(ThA))
		{
			ThTop = ThB;
			ThBack = ThA;
		}
		else
		{
			Err = FString::Printf(TEXT("pocket %d: unexpected cut through cushion jaw %d"), J.Pocket, JawIndex);
			return false;
		}
		const double Sign = ThBack > ThTop ? 1.0 : -1.0;

		// theta_bot: where the cut's lower boundary leaves the facing and reaches the cloth (z = 0).
		auto OnFacing = [&](double Theta) { return EvalCut(Fr, J, Q(Theta)).FaceZ > 0.0; };
		const int32 Scan = 512;
		int32 FirstBot = -1;
		for (int32 s = 0; s <= Scan; ++s)
		{
			const double Th = ThTop + (ThBack - ThTop) * s / Scan;
			const FCutEval Ev = EvalCut(Fr, J, Q(Th));
			if (Ev.bRubberBinding && s > 0 && s < Scan)
			{
				Err = FString::Printf(TEXT("pocket %d cut reaches the rubber face of jaw %d"), J.Pocket, JawIndex);
				return false;
			}
			if (FirstBot < 0 && !OnFacing(Th))
			{
				FirstBot = s;
			}
			else if (FirstBot >= 0 && OnFacing(Th))
			{
				Err = FString::Printf(TEXT("pocket %d: cut boundary returns onto facing %d"), J.Pocket, JawIndex);
				return false;
			}
		}
		J.bBot = FirstBot > 0;
		if (J.bBot)
		{
			const double ThIn = ThTop + (ThBack - ThTop) * (FirstBot - 1) / Scan;
			const double ThOut = ThTop + (ThBack - ThTop) * FirstBot / Scan;
			J.ThetaBot = Bisect(ThOut, ThIn, [&](double Th) { return !OnFacing(Th); });
		}
		else if (FirstBot == 0)
		{
			Err = TEXT("cut starts below the facing");
			return false;
		}

		// Points on the cut's lower boundary.
		auto CurvePoint = [&](double Theta) {
			const FV2 P = Q(Theta);
			const FCutEval Ev = EvalCut(Fr, J, P);
			return At(P, Ev.Low);
		};
		J.E = At(Q(ThTop), Fr.TopZ(Q(ThTop)));

		// Band on the facing: crossing of the band line with the cut curve (still on the facing).
		const FV3 Te = (J.E - JTop).GetSafeNormal();
		FV3 Mv = J.FacingNormal.Cross(Te).GetSafeNormal();
		if (Mv.Dot(JTop - JBand) < 0.0)
		{
			Mv = -Mv;
		}
		auto BandSide = [&](double Theta) { return (CurvePoint(Theta) - JBand).Dot(Mv) > 0.0; };
		const double ThCurveEnd = J.bBot ? J.ThetaBot : ThBack;
		double ThBand = ThTop;
		J.bFacingBand = !BandSide(ThCurveEnd);
		if (J.bFacingBand)
		{
			ThBand = Bisect(ThTop, ThCurveEnd, BandSide);
			// Nudge onto the far side so the band point is strictly below the top edge.
			J.bFacingBand = FMath::Abs(ThBand - ThTop) > 1e-9;
		}

		// Columns: top point, band, grid, theta_bot, back.
		TArray<double> Must;
		if (J.bFacingBand)
		{
			Must.Add(ThBand);
		}
		if (J.bBot)
		{
			Must.Add(J.ThetaBot);
		}
		TArray<double> Params = ArcParams(ThTop, ThBack, Pk.ThetaRef, L.Step, Must);
		if (J.bFacingBand)
		{
			// No grid columns between the top point and the band (the band strip spans them).
			Params.RemoveAll([&](double Th) { return Sign * (Th - ThTop) > 0.0 && Sign * (Th - ThBand) < 0.0; });
		}
		for (const double Th : Params)
		{
			FCutColumn Col;
			Col.Theta = Th;
			const FV2 P = Q(Th);
			Col.High = At(P, Fr.TopZ(P));
			if (Th == ThTop)
			{
				Col.Low = Col.High = J.E;
			}
			else if (J.bBot && Sign * (Th - J.ThetaBot) >= 0.0)
			{
				Col.Low = At(P, 0.0);
			}
			else
			{
				Col.Low = CurvePoint(Th);
			}
			J.Columns.Add(Col);
		}
		if (J.bFacingBand)
		{
			J.EBand = J.Columns[1].Low;
		}

		// Facing curve from the band point (or E) down to the bottom-edge end.
		J.FacingCurve.Add(J.bFacingBand ? J.EBand : J.E);
		const int32 FirstCurveCol = J.bFacingBand ? 2 : 1;
		for (int32 c = FirstCurveCol; c < J.Columns.Num(); ++c)
		{
			const FCutColumn& Col = J.Columns[c];
			J.FacingCurve.Add(Col.Low);
			if (J.bBot && Col.Theta == J.ThetaBot)
			{
				break;
			}
		}
		const FCutColumn& BackCol = J.Columns.Last();
		J.TopBackChain.Reset();
		for (const FCutColumn& Col : J.Columns)
		{
			J.TopBackChain.Add(Col.High);
		}
		if (J.bBot)
		{
			bool bOn = false;
			for (const FCutColumn& Col : J.Columns)
			{
				bOn = bOn || Col.Theta == J.ThetaBot;
				if (bOn)
				{
					J.BottomBackChain.Add(Col.Low);
				}
			}
			J.BackSide = {BackCol.Low, BackCol.High};
		}
		else
		{
			FV3 BottomEnd;
			if (!FacingOnBack(0.0, BottomEnd))
			{
				Err = TEXT("facing parallel to the cushion back");
				return false;
			}
			J.FacingCurve.Add(BottomEnd);
			J.BottomBackChain = {BottomEnd};
			J.BackSide = {BottomEnd, BackCol.Low, BackCol.High};
		}
		return true;
	}

	// --- arrangement of the cushion-back rectangle and the capture circles ------------------------------------------

	// Arranges the cushion-back rectangle (L.RectCorner) with one circle per pocket of radius Ar.Radius[p].
	bool BuildArrangement(const FLayout& L, FArrangement& Ar, FString& Err)
	{
		double EdgeLen[4];
		double EdgeOffset[4];
		double Perimeter = 0.0;
		for (int32 e = 0; e < 4; ++e)
		{
			EdgeOffset[e] = Perimeter;
			EdgeLen[e] = Len2(L.RectCorner[(e + 1) % 4] - L.RectCorner[e]);
			Perimeter += EdgeLen[e];
		}
		auto PerimPoint = [&](double S) {
			S = FMath::Fmod(S, Perimeter);
			if (S < 0.0)
			{
				S += Perimeter;
			}
			for (int32 e = 3; e >= 0; --e)
			{
				if (S >= EdgeOffset[e])
				{
					return L.RectCorner[e] + Norm2(L.RectCorner[(e + 1) % 4] - L.RectCorner[e]) * (S - EdgeOffset[e]);
				}
			}
			return L.RectCorner[0];
		};

		for (int32 p = 0; p < rb::kPocketCount; ++p)
		{
			const FPocket& Pk = L.Pockets[p];
			for (int32 e = 0; e < 4; ++e)
			{
				const FV2 A = L.RectCorner[e];
				const FV2 D = Norm2(L.RectCorner[(e + 1) % 4] - A);
				double T0;
				double T1;
				if (!LineCircle(A, D, Pk.C, Ar.Radius[p], T0, T1))
				{
					continue;
				}
				for (const double T : {T0, T1})
				{
					if (T > 1e-9 && T < EdgeLen[e] - 1e-9)
					{
						FCross X;
						X.Pocket = p;
						X.Edge = e;
						X.T = T;
						X.P = A + D * T;
						X.Phi = WrapPi(AngleOf(X.P - Pk.C) - Pk.ThetaRef);
						Ar.Crosses.Add(X);
					}
				}
			}
		}
		Ar.Crosses.Sort([](const FCross& X, const FCross& Y) { return X.Edge != Y.Edge ? X.Edge < Y.Edge : X.T < Y.T; });
		const int32 K = Ar.Crosses.Num();
		if (K < 2)
		{
			Err = TEXT("pocket circles do not cross the cushion-back rectangle");
			return false;
		}

		// Rectangle pieces outside the discs.
		TArray<int32> PieceStartingAt;
		TArray<int32> PieceEndingAt;
		PieceStartingAt.Init(-1, K);
		PieceEndingAt.Init(-1, K);
		for (int32 k = 0; k < K; ++k)
		{
			const int32 Next = (k + 1) % K;
			const double S0 = EdgeOffset[Ar.Crosses[k].Edge] + Ar.Crosses[k].T;
			double S1 = EdgeOffset[Ar.Crosses[Next].Edge] + Ar.Crosses[Next].T;
			if (S1 <= S0)
			{
				S1 += Perimeter;
			}
			const FV2 Mid = PerimPoint(0.5 * (S0 + S1));
			bool bInDisc = false;
			for (int32 p = 0; p < rb::kPocketCount; ++p)
			{
				bInDisc = bInDisc || Len2(Mid - L.Pockets[p].C) < Ar.Radius[p];
			}
			if (bInDisc)
			{
				continue;
			}
			FRectPiece Piece;
			Piece.Start = k;
			Piece.End = Next;
			Piece.Points.Add(Ar.Crosses[k].P);
			for (int32 Lap = 0; Lap < 2; ++Lap)
			{
				for (int32 e = 0; e < 4; ++e)
				{
					const double Sc = EdgeOffset[e] + Lap * Perimeter;
					if (Sc > S0 + 1e-12 && Sc < S1 - 1e-12)
					{
						Piece.Points.Add(L.RectCorner[e]);
					}
				}
			}
			Piece.Points.Add(Ar.Crosses[Next].P);
			PieceStartingAt[k] = Ar.RectPieces.Num();
			PieceEndingAt[Next] = Ar.RectPieces.Num();
			Ar.RectPieces.Add(Piece);
		}

		// Circle arcs between consecutive crossings of each pocket.
		TArray<int32> InsideArcAt;
		TArray<int32> OutsideArcAt;
		InsideArcAt.Init(-1, K);
		OutsideArcAt.Init(-1, K);
		for (int32 p = 0; p < rb::kPocketCount; ++p)
		{
			TArray<int32> Ids;
			for (int32 k = 0; k < K; ++k)
			{
				if (Ar.Crosses[k].Pocket == p)
				{
					Ids.Add(k);
				}
			}
			Ids.Sort([&Ar](int32 X, int32 Y) { return Ar.Crosses[X].Phi < Ar.Crosses[Y].Phi; });
			for (int32 i = 0; i < Ids.Num(); ++i)
			{
				FCircleArc Arc;
				Arc.Pocket = p;
				Arc.StartCross = Ids[i];
				Arc.EndCross = Ids[(i + 1) % Ids.Num()];
				Arc.From = Ar.Crosses[Arc.StartCross].Phi;
				Arc.Sweep = WrapPositive(Ar.Crosses[Arc.EndCross].Phi - Arc.From);
				const FV2 Mid = L.CirclePoint(p, Arc.From + 0.5 * Arc.Sweep, Ar.Radius[p]);
				Arc.bInside = FMath::Abs(Mid.X) < L.HlC && FMath::Abs(Mid.Y) < L.HwC;
				Arc.bFront = Arc.bInside && Arc.From < 0.0 && Arc.From + Arc.Sweep > 0.0;
				TArray<int32>& Map = Arc.bInside ? InsideArcAt : OutsideArcAt;
				for (const int32 X : {Arc.StartCross, Arc.EndCross})
				{
					if (Map[X] >= 0)
					{
						Err = TEXT("ambiguous circle arcs at a crossing");
						return false;
					}
					Map[X] = Ar.Arcs.Num();
				}
				Ar.Arcs.Add(Arc);
			}
		}

		// Chain rectangle pieces with inside arcs (bed loops) and with outside arcs (union loop).
		auto Chain = [&](const TArray<int32>& ArcAt, TArray<TArray<FLoopElement>>& Out) {
			TArray<bool> Used;
			Used.Init(false, Ar.RectPieces.Num());
			for (int32 r = 0; r < Ar.RectPieces.Num(); ++r)
			{
				if (Used[r])
				{
					continue;
				}
				TArray<FLoopElement> Loop;
				int32 Piece = r;
				for (int32 Guard = 0; Guard < 64; ++Guard)
				{
					Used[Piece] = true;
					Loop.Add({false, Piece, true});
					const int32 X = Ar.RectPieces[Piece].End;
					const int32 ArcIndex = ArcAt[X];
					if (ArcIndex < 0)
					{
						return false;
					}
					const FCircleArc& Arc = Ar.Arcs[ArcIndex];
					const bool bForward = Arc.StartCross == X;
					Loop.Add({true, ArcIndex, bForward});
					const int32 Y = bForward ? Arc.EndCross : Arc.StartCross;
					Piece = PieceStartingAt[Y];
					if (Piece < 0)
					{
						return false;
					}
					if (Piece == r)
					{
						break;
					}
				}
				Out.Add(Loop);
			}
			return true;
		};
		TArray<TArray<FLoopElement>> UnionLoops;
		if (!Chain(InsideArcAt, Ar.BedLoops) || !Chain(OutsideArcAt, UnionLoops) || UnionLoops.Num() != 1)
		{
			Err = TEXT("could not chain the rectangle / pocket arrangement");
			return false;
		}
		Ar.UnionLoop = UnionLoops[0];
		// Main bed loop first (the one with the front arcs).
		Ar.BedLoops.Sort([&Ar](const TArray<FLoopElement>& X, const TArray<FLoopElement>& Y) {
			auto HasFront = [&Ar](const TArray<FLoopElement>& Loop) {
				for (const FLoopElement& E : Loop)
				{
					if (E.bArc && Ar.Arcs[E.Index].bFront)
					{
						return true;
					}
				}
				return false;
			};
			return HasFront(X) && !HasFront(Y);
		});
		int32 FrontCount = 0;
		for (const FCircleArc& Arc : Ar.Arcs)
		{
			FrontCount += Arc.bFront ? 1 : 0;
		}
		if (FrontCount != rb::kPocketCount)
		{
			Err = FString::Printf(TEXT("expected one front arc per pocket, found %d"), FrontCount);
			return false;
		}
		return true;
	}

	// Front arcs of the bed: open (rounded) range and its trims (facing bottom lines or rectangle edges).
	bool BuildFronts(FLayout& L, FString& Err)
	{
		L.Fronts.SetNum(rb::kPocketCount);
		const int32 Rings = FMath::Max(2, L.O.DropRoundingSegments);
		for (int32 a = 0; a < L.Hole.Arcs.Num(); ++a)
		{
			const FCircleArc& Arc = L.Hole.Arcs[a];
			if (!Arc.bFront)
			{
				continue;
			}
			const int32 p = Arc.Pocket;
			const FPocket& Pk = L.Pockets[p];
			FFrontArc& Fr = L.Fronts[p];
			Fr.Arc = a;
			const FJawEnd* Ends[2] = {&L.Cushions[(p + rb::kCushionCount - 1) % rb::kCushionCount].B, &L.Cushions[p].A};
			for (int32 s = 0; s < 2; ++s)
			{
				FFrontSide& Side = Fr.Side[s];
				const double SideSign = s == 0 ? -1.0 : 1.0;
				const FCross& X = L.Hole.Crosses[s == 0 ? Arc.StartCross : Arc.EndCross];
				Side.PhiRect = X.Phi;
				const FJawEnd* End = Ends[0]->SideSign == SideSign ? Ends[0] : (Ends[1]->SideSign == SideSign ? Ends[1] : nullptr);
				if (End == nullptr)
				{
					Err = FString::Printf(TEXT("pocket %d: no cushion end on side %d"), p, s);
					return false;
				}
				Side.bTrimRect = !End->bBot;
				if (Side.bTrimRect)
				{
					Side.TrimPoint = X.P;
					Side.TrimDir = Norm2(L.RectCorner[(X.Edge + 1) % 4] - L.RectCorner[X.Edge]);
					Side.TrimNormal = -RightOf(Side.TrimDir); // into the rectangle (left of the CCW edge)
				}
				else
				{
					Side.TrimPoint = End->BottomLinePoint;
					Side.TrimDir = End->F;
					Side.TrimNormal = End->Pn;
				}
				auto TrimPhi = [&](double R, double Near, double& OutPhi) {
					double T0;
					double T1;
					if (!LineCircle(Side.TrimPoint, Side.TrimDir, Pk.C, R, T0, T1))
					{
						return false;
					}
					const double P0 = WrapPi(AngleOf(Side.TrimPoint + Side.TrimDir * T0 - Pk.C) - Pk.ThetaRef);
					const double P1 = WrapPi(AngleOf(Side.TrimPoint + Side.TrimDir * T1 - Pk.C) - Pk.ThetaRef);
					const bool bOk0 = P0 * SideSign > 0.0;
					const bool bOk1 = P1 * SideSign > 0.0;
					if (bOk0 && (!bOk1 || FMath::Abs(P0 - Near) <= FMath::Abs(P1 - Near)))
					{
						OutPhi = P0;
					}
					else if (bOk1)
					{
						OutPhi = P1;
					}
					else
					{
						return false;
					}
					return true;
				};
				if (Side.bTrimRect)
				{
					Side.PhiRpTrim = Side.PhiRect;
				}
				else if (!TrimPhi(Pk.Rp, Side.PhiRect, Side.PhiRpTrim))
				{
					Err = FString::Printf(TEXT("pocket %d: r_p does not meet the facing bottom line"), p);
					return false;
				}
				if (SideSign * (Side.PhiRect - Side.PhiRpTrim) < -1e-12)
				{
					Err = FString::Printf(TEXT("pocket %d: plain range inverted"), p);
					return false;
				}
				Side.RingPhi.SetNum(Rings + 1);
				for (int32 k = 0; k <= Rings; ++k)
				{
					const double Psi = 0.5 * kPi * k / Rings;
					const double Rho = Pk.Ad - Pk.Rd * FMath::Sin(Psi);
					if (k == Rings)
					{
						Side.RingPhi[k] = Side.PhiRpTrim;
					}
					else if (!TrimPhi(Rho, Side.PhiRpTrim, Side.RingPhi[k]))
					{
						Err = FString::Printf(TEXT("pocket %d: rounding ring %d misses the trim"), p, k);
						return false;
					}
				}
			}
			Fr.RingParams.SetNum(Rings + 1);
			for (int32 k = 0; k <= Rings; ++k)
			{
				if (!(Fr.Side[0].RingPhi[k] < Fr.Side[1].RingPhi[k]))
				{
					Err = FString::Printf(TEXT("pocket %d: empty rounding ring %d"), p, k);
					return false;
				}
				Fr.RingParams[k] = ArcParams(Fr.Side[0].RingPhi[k], Fr.Side[1].RingPhi[k], 0.0, L.Step);
			}
		}
		return true;
	}

	// Liner collars behind the holes (see FCollar): per outside arc of the hole arrangement, the anchors on the rectangle
	// edges, the outer path along the r_p + LinerT circle and, where an undercut facing leaves a stretch of the cushion-back
	// line open next to the hole (bar tables: the facing's bottom line meets the cushion back before the r_p circle), a strip
	// along that stretch - so no rail wood shows inside a pocket.
	bool BuildCollarLayout(FLayout& L, FString& Err)
	{
		L.CollarAtCross.Init(FIntPoint(-1, -1), L.Hole.Crosses.Num());
		for (int32 a = 0; a < L.Hole.Arcs.Num(); ++a)
		{
			const FCircleArc& Arc = L.Hole.Arcs[a];
			if (Arc.bInside)
			{
				continue;
			}
			const int32 p = Arc.Pocket;
			const FPocket& Pk = L.Pockets[p];
			const double Rc = L.CollarRadius(p);
			FCollar Co;
			Co.Pocket = p;
			Co.Arc = a;
			for (int32 s = 0; s < 2; ++s)
			{
				const int32 CrossId = s == 0 ? Arc.StartCross : Arc.EndCross;
				const FCross& X = L.Hole.Crosses[CrossId];
				FCollarSide& Sd = Co.Side[s];
				Sd.Cross = X.P;
				const FV2 EdgeStart = L.RectCorner[X.Edge];
				const FV2 EdgeEnd = L.RectCorner[(X.Edge + 1) % 4];
				const FV2 D = Norm2(EdgeEnd - EdgeStart);
				const FV2 Outward = RightOf(D); // the rectangle is CCW: its inside is left of every edge
				const FV2 U = Dot2(X.P - Pk.C, D) >= 0.0 ? D : -D; // along the edge, away from the hole
				const double Room = Dot2((Dot2(U, D) > 0.0 ? EdgeEnd : EdgeStart) - X.P, U);
				double T0;
				double T1;
				if (!LineCircle(X.P, U, Pk.C, Rc, T0, T1) || !(T1 > 0.0) || !(T1 < Room))
				{
					Err = FString::Printf(TEXT("pocket %d: the liner collar does not fit on the cushion-back edge (LinerThicknessCm)"), p);
					return false;
				}
				Sd.Anchor = X.P + U * T1;
				Sd.OnCircle = Sd.Anchor;
				for (const FCushion& Cu : L.Cushions)
				{
					for (const FJawEnd* J : {&Cu.A, &Cu.B})
					{
						if (J->Pocket != p || J->bBot || J->BottomBackChain.Num() == 0)
						{
							continue;
						}
						const FV2 B = Flat(J->BottomBackChain[0]); // facing bottom line x cushion back (z = 0)
						const double Along = Dot2(B - X.P, U);
						if (FMath::Abs(Cross2(B - X.P, U)) > 1e-9 || !(Along > T1 + 1e-9))
						{
							continue;
						}
						if (!(Along < Room))
						{
							Err = FString::Printf(TEXT("pocket %d: open cushion-back stretch beyond the rectangle corner"), p);
							return false;
						}
						double Q0;
						double Q1;
						const FV2 Out = B + Outward * L.LinerT;
						if (!LineCircle(Out, -U, Pk.C, Rc, Q0, Q1) || !(Q0 > 0.0))
						{
							Err = FString::Printf(TEXT("pocket %d: collar strip misses the collar circle"), p);
							return false;
						}
						Sd.bStrip = true;
						Sd.Anchor = B;
						Sd.AnchorOut = Out;
						Sd.OnCircle = Out - U * Q0;
					}
				}
				L.CollarAtCross[CrossId] = FIntPoint(L.Collars.Num(), s);
			}
			const double PhiS = Arc.From;
			const double PhiE = Arc.From + Arc.Sweep;
			Co.Side[0].Phi = PhiS + WrapPi(AngleOf(Co.Side[0].OnCircle - Pk.C) - Pk.ThetaRef - PhiS);
			Co.Side[1].Phi = PhiE + WrapPi(AngleOf(Co.Side[1].OnCircle - Pk.C) - Pk.ThetaRef - PhiE);
			// (The collar arc is angularly wider than the hole arc where the centre lies inside the rectangle, narrower where
			// it lies beyond the cushion back - side pockets; either way its ends sit next to the hole arc's ends.)
			if (!(Co.Side[0].Phi < Co.Side[1].Phi) || FMath::Abs(Co.Side[0].Phi - PhiS) > 0.5 * kPi || FMath::Abs(Co.Side[1].Phi - PhiE) > 0.5 * kPi)
			{
				Err = FString::Printf(TEXT("pocket %d: collar arc does not follow the hole arc"), p);
				return false;
			}
			const double Must[] = {kPi, -kPi, 3.0 * kPi};
			Co.OuterParams = ArcParams(Co.Side[0].Phi, Co.Side[1].Phi, 0.0, L.Step, Must);
			Co.InnerParams = ArcParams(PhiS, PhiE, 0.0, L.Step);
			auto Push = [&Co](const FV2& P, const FV2& Radial, bool bSmoothToNext) {
				Co.OuterPath.Add(P);
				Co.Radial.Add(Radial);
				Co.EdgeSmooth.Add(bSmoothToNext);
			};
			if (Co.Side[0].bStrip)
			{
				Push(Co.Side[0].Anchor, FV2::ZeroVector, false);
				Push(Co.Side[0].AnchorOut, FV2::ZeroVector, false);
			}
			for (int32 i = 0; i < Co.OuterParams.Num(); ++i)
			{
				Push(L.CirclePoint(p, Co.OuterParams[i], Rc), -L.InwardFromCircle(p, Co.OuterParams[i]), i + 1 < Co.OuterParams.Num());
			}
			if (Co.Side[1].bStrip)
			{
				Co.EdgeSmooth.Last() = false;
				Push(Co.Side[1].AnchorOut, FV2::ZeroVector, false);
				Push(Co.Side[1].Anchor, FV2::ZeroVector, false);
			}
			Co.EdgeSmooth.Last() = false; // no edge after the last point
			L.Collars.Add(MoveTemp(Co));
		}
		return true;
	}

	// Cap / body inner boundary: the cushion-back rectangle with the collars' outer paths, and the four mitre points.
	bool BuildUnionLoop(FLayout& L, FString& Err)
	{
		FWallLoop& U = L.Union;
		auto AddPoint = [&U](const FV2& P, bool bSmoothToNext, const FV2& N) {
			if (U.P.Num() > 0 && FV2::DistSquared(U.P.Last(), P) < 1e-24)
			{
				// Shared point: the next edge decides smoothness; an arc's normal is kept for the arc edge ending here.
				U.EdgeSmooth.Last() = bSmoothToNext;
				if (!N.IsZero())
				{
					U.PointNormal.Last() = N;
				}
				return;
			}
			U.Add(P, bSmoothToNext, N);
		};
		auto AnchorAt = [&L](int32 CrossId, FV2& Out) {
			const FIntPoint At = L.CollarAtCross.IsValidIndex(CrossId) ? L.CollarAtCross[CrossId] : FIntPoint(-1, -1);
			if (At.X < 0)
			{
				return false;
			}
			Out = L.Collars[At.X].Side[At.Y].Anchor;
			return true;
		};
		for (const FLoopElement& E : L.Hole.UnionLoop)
		{
			if (!E.bArc)
			{
				const FRectPiece& Piece = L.Hole.RectPieces[E.Index];
				TArray<FV2> Points = Piece.Points;
				if (!AnchorAt(Piece.Start, Points[0]) || !AnchorAt(Piece.End, Points.Last()))
				{
					Err = TEXT("rail boundary: a rectangle piece without collars");
					return false;
				}
				for (const FV2& P : Points)
				{
					AddPoint(P, false, FV2::ZeroVector);
				}
				continue;
			}
			const FCollar* Co = L.Collars.FindByPredicate([&E](const FCollar& C) { return C.Arc == E.Index; });
			if (!Co)
			{
				Err = TEXT("rail boundary: an outside arc without a collar");
				return false;
			}
			// Outward from the rail solid = into the collar (toward the pocket centre).
			const int32 N = Co->OuterPath.Num();
			for (int32 k = 0; k < N; ++k)
			{
				const int32 i = E.bForward ? k : N - 1 - k;
				const bool bSmooth = E.bForward ? Co->EdgeSmooth[i] : (i > 0 && Co->EdgeSmooth[i - 1]);
				AddPoint(Co->OuterPath[i], k + 1 < N && bSmooth, -Co->Radial[i]);
			}
		}
		if (U.P.Num() > 1 && FV2::DistSquared(U.P.Last(), U.P[0]) < 1e-24)
		{
			if (U.PointNormal[0].IsZero())
			{
				U.PointNormal[0] = U.PointNormal.Last();
			}
			U.P.Pop();
			U.PointNormal.Pop();
			U.EdgeSmooth.Pop();
		}
		// Mitre points: the rectangle corner when it is free, else the corner collar's outermost point (phi = pi).
		for (int32 c = 0; c < 4; ++c)
		{
			const FV2 Qc = L.RectCorner[c];
			FV2 Target = Qc;
			for (int32 p = 0; p < rb::kPocketCount; ++p)
			{
				if (Len2(Qc - L.Pockets[p].C) < L.CollarRadius(p))
				{
					Target = L.CirclePoint(p, kPi, L.CollarRadius(p));
				}
			}
			for (int32 i = 0; i < U.P.Num(); ++i)
			{
				if (FV2::DistSquared(U.P[i], Target) < 1e-18)
				{
					L.Mitre[c] = i;
				}
			}
			if (L.Mitre[c] < 0)
			{
				Err = FString::Printf(TEXT("mitre point of corner %d not on the rail boundary"), c);
				return false;
			}
		}
		// The mitres must appear in CCW order around the loop (cyclically), so each rail piece walks one stretch.
		int32 Descents = 0;
		for (int32 c = 0; c < 4; ++c)
		{
			Descents += L.Mitre[(c + 1) % 4] < L.Mitre[c] ? 1 : 0;
		}
		if (Descents != 1)
		{
			Err = TEXT("rail mitres out of order");
			return false;
		}
		return true;
	}

	bool BuildLayout(const rb::TableGeometry& G, const FRbTableMeshOptions& Options, FLayout& L, FString& Err)
	{
		if (G.Noses.Size() != rb::kCushionCount)
		{
			Err = TEXT("table geometry is not built");
			return false;
		}
		if (!G.Spec.HasPockets || G.Pockets.Size() != rb::kPocketCount)
		{
			Err = TEXT("pocketless tables are not supported by the mesh builder");
			return false;
		}
		const rb::TableSpec& Spec = G.Spec;
		L.G = &G;
		L.O = Options;
		L.O.CircleSegments = FMath::Max(16, Options.CircleSegments + (Options.CircleSegments & 1));
		L.O.SightSegments = FMath::Max(8, Options.SightSegments);
		L.H = Spec.CushionNoseHeight;
		L.Zc = Spec.RailTopZ;
		L.T = Spec.SlateThickness;
		L.Cw = Spec.CushionWidth;
		L.Rw = Spec.RailWidthTotal;
		L.Hl = G.HalfLength;
		L.Hw = G.HalfWidth;
		L.HlC = L.Hl + L.Cw;
		L.HwC = L.Hw + L.Cw;
		L.HlR = L.Hl + L.Rw;
		L.HwR = L.Hw + L.Rw;
		L.BedHeight = Spec.BedHeight;
		L.NoseBand = Spec.CushionNoseProfileRadius > 0.0 ? Spec.CushionNoseProfileRadius : 0.001;
		L.Step = kTwoPi / L.O.CircleSegments;
		L.ApronDepth = Options.ApronDepthCm / FRbCoords::CmPerMeter;
		L.CapT = Options.CapThicknessCm / FRbCoords::CmPerMeter;
		L.SightDepth = Options.SightDepthCm / FRbCoords::CmPerMeter;
		L.LinerT = Options.LinerThicknessCm / FRbCoords::CmPerMeter;
		L.CapBand = Options.CapEdgeRoundingCm / FRbCoords::CmPerMeter;
		L.bCabinet = Options.BaseStyle == ERbTableBaseStyle::Cabinet ||
			(Options.BaseStyle == ERbTableBaseStyle::Auto && Spec.Cloth == rb::ClothPreset::NappedBar);
		if (G.Profile.Points.Size() < 3 || G.Profile.NoseIndex != 1 || G.Profile.CushionBackIndex != 2)
		{
			Err = TEXT("unexpected cushion profile");
			return false;
		}
		L.EFace = G.Profile.Points[0].x;
		if (!(L.EFace > 0.0) || !(L.T > 0.0) || !(L.CapT > 0.0) || !(L.CapT < L.Zc + L.T) || !(L.ApronDepth > L.T + 0.02) ||
			!(L.BedHeight > L.ApronDepth + 0.05))
		{
			Err = TEXT("invalid mesh options for this table");
			return false;
		}

		for (int32 p = 0; p < rb::kPocketCount; ++p)
		{
			const rb::PocketGeometry& Pg = G.Pockets[p];
			FPocket& Pk = L.Pockets[p];
			Pk.C = V2(Pg.CaptureCenter);
			Pk.Rp = Pg.CaptureRadius;
			Pk.Rd = Pg.DropRadius;
			Pk.Ad = Pg.DropEdgeRadius;
			Pk.Axis = V2(Pg.Axis);
			Pk.ThetaRef = AngleOf(-Pk.Axis);
			if (!(Pk.Rd > 0.0) || !(Pk.Rd < L.T))
			{
				Err = TEXT("drop rounding must be positive and thinner than the slate");
				return false;
			}
		}

		// Skirt thickness clears every pocket circle (the drop pockets hang inside the table body).
		double Clearance = 1e9;
		for (const FPocket& Pk : L.Pockets)
		{
			Clearance = FMath::Min(Clearance, FMath::Min(L.HlR - (FMath::Abs(Pk.C.X) + Pk.Rp), L.HwR - (FMath::Abs(Pk.C.Y) + Pk.Rp)));
		}
		L.SkirtT = FMath::Min(Options.SkirtThicknessCm / FRbCoords::CmPerMeter, Clearance - 0.005);
		if (L.SkirtT < 0.005)
		{
			Err = TEXT("pocket circles leave no room for the apron skirt");
			return false;
		}

		// Nose outline (the physics' own samples of the jaw arcs).
		const int32 PerArc = FMath::Max(2, Options.ArcSegments);
		TArray<rb::Vec2> Outline;
		Outline.SetNum(rb::kMaxJaws * (PerArc + 1) + 8);
		const int32 OutlineCount = rb::BuildNoseOutline(G, PerArc, Outline.GetData(), Outline.Num());
		if (OutlineCount <= 0)
		{
			Err = TEXT("BuildNoseOutline failed");
			return false;
		}
		TArray<FV2> IncomingArc[rb::kPocketCount];
		TArray<FV2> OutgoingArc[rb::kPocketCount];
		int32 Cursor = 0;
		for (int32 p = 0; p < rb::kPocketCount; ++p)
		{
			const int32 NIn = G.JawArcs[2 * p].Radius > 0.0 ? PerArc : 1;
			const int32 NOut = G.JawArcs[2 * p + 1].Radius > 0.0 ? PerArc : 1;
			for (int32 i = 0; i < NIn; ++i)
			{
				IncomingArc[p].Add(V2(Outline[Cursor++]));
			}
			Cursor += 2; // the two facing ends
			for (int32 i = 0; i < NOut; ++i)
			{
				OutgoingArc[p].Add(V2(Outline[Cursor++]));
			}
			Algo::Reverse(OutgoingArc[p]); // nose -> facing
		}
		if (Cursor != OutlineCount)
		{
			Err = TEXT("unexpected BuildNoseOutline layout");
			return false;
		}

		for (int32 c = 0; c < rb::kCushionCount; ++c)
		{
			const rb::NoseSegment& Nose = G.Noses[c];
			FCushion& Cu = L.Cushions[c];
			FCushionFrame& Fr = Cu.Frame;
			Fr.NoseStart = V2(Nose.Start);
			Fr.D = V2(Nose.Direction);
			Fr.N = V2(Nose.InwardNormal);
			Fr.H = L.H;
			Fr.Zc = L.Zc;
			Fr.Cw = L.Cw;
			Fr.K = (L.Zc - L.H) / L.Cw;
			Fr.EFace = L.EFace;
			Fr.TopNormal = FV3(Fr.K * Fr.N.X, Fr.K * Fr.N.Y, 1.0).GetSafeNormal();
			Fr.FaceNormal = FV3(Fr.N.X * L.H, Fr.N.Y * L.H, -L.EFace).GetSafeNormal();
			Fr.BackNormal = FV3(-Fr.N.X, -Fr.N.Y, 0.0);
			const int32 PB = (c + 1) % rb::kPocketCount;
			if (!BuildJawEnd(L, Fr, 2 * c + 1, OutgoingArc[c], Cu.A, Err) || !BuildJawEnd(L, Fr, 2 * PB, IncomingArc[PB], Cu.B, Err))
			{
				Err = FString::Printf(TEXT("cushion %d: %s"), c, *Err);
				return false;
			}
		}
		L.RectCorner[0] = FV2(-L.HlC, -L.HwC);
		L.RectCorner[1] = FV2(L.HlC, -L.HwC);
		L.RectCorner[2] = FV2(L.HlC, L.HwC);
		L.RectCorner[3] = FV2(-L.HlC, L.HwC);
		for (int32 p = 0; p < rb::kPocketCount; ++p)
		{
			L.Hole.Radius[p] = L.Pockets[p].Rp;
		}
		return BuildArrangement(L, L.Hole, Err) && BuildFronts(L, Err) && BuildCollarLayout(L, Err) && BuildUnionLoop(L, Err);
	}

	// ============================================================================================================
	// Parts
	// ============================================================================================================

	TArray<FV3> Reversed(TArray<FV3> In)
	{
		Algo::Reverse(In);
		return In;
	}

	void AppendUnique(TArray<FV3>& Out, const TArray<FV3>& In)
	{
		for (const FV3& P : In)
		{
			if (Out.Num() == 0 || FV3::DistSquared(Out.Last(), P) > 1e-20)
			{
				Out.Add(P);
			}
		}
	}

	void BuildCushion(FMeshWriter& W, const FLayout& L, const FCushion& Cu)
	{
		const FCushionFrame& Fr = Cu.Frame;
		const FJawEnd& A = Cu.A;
		const FJawEnd& B = Cu.B;
		W.BeginSolid();
		W.SetUVMode(EUVMode::Box);

		// Top (sloped plane through the nose line at h and the cushion back at RailTopZ).
		{
			TArray<FV3> Loop;
			AppendUnique(Loop, Reversed(A.TopBackChain));
			AppendUnique(Loop, Reversed(A.Top));
			AppendUnique(Loop, B.Top);
			AppendUnique(Loop, B.TopBackChain);
			W.Polygon({Loop}, Fr.TopNormal);
		}
		// Bottom (z = 0).
		{
			TArray<FV3> Loop;
			AppendUnique(Loop, Reversed(A.BottomBackChain));
			AppendUnique(Loop, Reversed(A.Bottom));
			AppendUnique(Loop, B.Bottom);
			AppendUnique(Loop, B.BottomBackChain);
			W.Polygon({Loop}, FV3(0.0, 0.0, -1.0));
		}
		// Back (vertical plane at the cushion back).
		{
			TArray<FV3> Loop;
			AppendUnique(Loop, A.BackSide);
			AppendUnique(Loop, Reversed(B.BackSide));
			W.Polygon({Loop}, Fr.BackNormal);
		}
		// Rubber face with the nose band (rounded-nose shading, exact geometry).
		W.Quad(A.Top[0], B.Top[0], B.Band[0], A.Band[0], Fr.TopNormal, Fr.TopNormal, Fr.FaceNormal, Fr.FaceNormal);
		W.Quad(A.Band[0], B.Band[0], B.Bottom[0], A.Bottom[0], Fr.FaceNormal, Fr.FaceNormal, Fr.FaceNormal, Fr.FaceNormal);

		for (const FJawEnd* J : {&A, &B})
		{
			// Jaw blend (ruled between the arc at h and the bottom arc).
			for (int32 i = 0; i + 1 < J->Top.Num(); ++i)
			{
				const FV3& N0 = J->RulingNormal[i];
				const FV3& N1 = J->RulingNormal[i + 1];
				W.Quad(J->Top[i], J->Top[i + 1], J->Band[i + 1], J->Band[i], Fr.TopNormal, Fr.TopNormal, N1, N0);
				W.Quad(J->Band[i], J->Band[i + 1], J->Bottom[i + 1], J->Bottom[i], N0, N1, N1, N0);
			}
			// Facing (undercut plane).
			const FV3& JTop = J->Top.Last();
			const FV3& JBand = J->Band.Last();
			const FV3& JBottom = J->Bottom.Last();
			TArray<FV3> Loop;
			if (J->bFacingBand)
			{
				W.Quad(JTop, J->E, J->EBand, JBand, Fr.TopNormal, Fr.TopNormal, J->FacingNormal, J->FacingNormal);
				Loop.Add(JBand);
				AppendUnique(Loop, J->FacingCurve);
				AppendUnique(Loop, {JBottom});
			}
			else
			{
				Loop.Add(JTop);
				AppendUnique(Loop, J->FacingCurve);
				AppendUnique(Loop, {JBottom, JBand});
			}
			W.Polygon({Loop}, J->FacingNormal);
			// Pocket cut (capture cylinder through the cushion).
			if (J->bCut)
			{
				const FPocket& Pk = L.Pockets[J->Pocket];
				for (int32 c = 0; c + 1 < J->Columns.Num(); ++c)
				{
					const FCutColumn& C0 = J->Columns[c];
					const FCutColumn& C1 = J->Columns[c + 1];
					const FV3 N0 = At(Norm2(Pk.C - Flat(C0.High)), 0.0);
					const FV3 N1 = At(Norm2(Pk.C - Flat(C1.High)), 0.0);
					if (c == 0)
					{
						W.Tri(C0.High, C1.Low, C1.High, N0, N1, N1);
					}
					else
					{
						W.Quad(C0.Low, C1.Low, C1.High, C0.High, N0, N1, N1, N0);
					}
				}
			}
		}
	}

	// r_p column wall segment between two columns given as vertical vertex lists (top -> bottom).
	void ColumnStrip(FMeshWriter& W, const FLayout& L, int32 Pocket, double Phi0, const TArray<double>& Z0, double Phi1, const TArray<double>& Z1)
	{
		const FPocket& Pk = L.Pockets[Pocket];
		const FV2 P0 = L.CirclePoint(Pocket, Phi0, Pk.Rp);
		const FV2 P1 = L.CirclePoint(Pocket, Phi1, Pk.Rp);
		const FV3 N0 = At(L.InwardFromCircle(Pocket, Phi0), 0.0);
		const FV3 N1 = At(L.InwardFromCircle(Pocket, Phi1), 0.0);
		TArray<FV3> A;
		TArray<FV3> NA;
		TArray<double> TA;
		TArray<FV3> B;
		TArray<FV3> NB;
		TArray<double> TB;
		for (const double Z : Z0)
		{
			A.Add(At(P0, Z));
			NA.Add(N0);
			TA.Add(-Z);
		}
		for (const double Z : Z1)
		{
			B.Add(At(P1, Z));
			NB.Add(N1);
			TB.Add(-Z);
		}
		W.Zip(A, NA, TA, B, NB, TB);
	}

	void BuildBed(FMeshWriter& W, const FLayout& L)
	{
		W.SetUVMode(EUVMode::Box);
		const int32 Rings = FMath::Max(2, L.O.DropRoundingSegments);
		auto RingRho = [&L, Rings](int32 Pocket, int32 k) {
			return L.Pockets[Pocket].Ad - L.Pockets[Pocket].Rd * FMath::Sin(0.5 * kPi * k / Rings);
		};
		auto RingZ = [&L, Rings](int32 Pocket, int32 k) { return -L.Pockets[Pocket].Rd + L.Pockets[Pocket].Rd * FMath::Cos(0.5 * kPi * k / Rings); };

		// Rectangle-trim ends of front arcs (keyed by crossing id): the torus section on the rectangle edge.
		TMap<int32, TArray<FV3>> RectTrimSection; // crossing -> ring 0 (z = 0) ... ring n (r_p, -r_d)
		for (int32 p = 0; p < rb::kPocketCount; ++p)
		{
			const FFrontArc& Fr = L.Fronts[p];
			const FCircleArc& Arc = L.Hole.Arcs[Fr.Arc];
			for (int32 s = 0; s < 2; ++s)
			{
				if (!Fr.Side[s].bTrimRect)
				{
					continue;
				}
				TArray<FV3> Section;
				for (int32 k = 0; k <= Rings; ++k)
				{
					Section.Add(At(L.CirclePoint(p, Fr.Side[s].RingPhi[k], RingRho(p, k)), RingZ(p, k)));
				}
				Section.Last() = At(L.Hole.Crosses[s == 0 ? Arc.StartCross : Arc.EndCross].P, -L.Pockets[p].Rd);
				RectTrimSection.Add(s == 0 ? Arc.StartCross : Arc.EndCross, Section);
			}
		}

		// Only the main loop: the slate islands behind bar-table holes are part of the corner inserts (BuildRailBlocks).
		for (int32 LoopIndex = 0; LoopIndex < FMath::Min(1, L.Hole.BedLoops.Num()); ++LoopIndex)
		{
			const TArray<FLoopElement>& Loop = L.Hole.BedLoops[LoopIndex];
			W.BeginSolid();
			TArray<FV3> TopLoop;
			TArray<FV3> BottomLoop;
			for (const FLoopElement& E : Loop)
			{
				if (!E.bArc)
				{
					const FRectPiece& Piece = L.Hole.RectPieces[E.Index];
					const TArray<FV3>* StartSec = RectTrimSection.Find(Piece.Start);
					const TArray<FV3>* EndSec = RectTrimSection.Find(Piece.End);
					TArray<FV3> Top = Lift(Piece.Points, 0.0);
					if (StartSec)
					{
						Top[0] = (*StartSec)[0];
					}
					if (EndSec)
					{
						Top.Last() = (*EndSec)[0];
					}
					AppendUnique(TopLoop, Top);
					AppendUnique(BottomLoop, Lift(Piece.Points, -L.T));
					// Outer walls (hidden behind the rail body), per straight segment.
					const int32 Segs = Piece.Points.Num() - 1;
					for (int32 i = 0; i < Segs; ++i)
					{
						const FV2 P0 = Piece.Points[i];
						const FV2 P1 = Piece.Points[i + 1];
						const FV3 N = At(RightOf(Norm2(P1 - P0)), 0.0);
						TArray<FV3> Wall;
						Wall.Add(At(P0, -L.T));
						Wall.Add(At(P1, -L.T));
						if (i == Segs - 1 && EndSec)
						{
							AppendUnique(Wall, Reversed(*EndSec));
						}
						else
						{
							Wall.Add(At(P1, 0.0));
						}
						if (i == 0 && StartSec)
						{
							AppendUnique(Wall, *StartSec);
						}
						else
						{
							Wall.Add(At(P0, 0.0));
						}
						W.Polygon({Wall}, N);
					}
					continue;
				}

				const FCircleArc& Arc = L.Hole.Arcs[E.Index];
				const int32 p = Arc.Pocket;
				const FPocket& Pk = L.Pockets[p];
				if (!Arc.bFront)
				{
					// Island arc (behind the hole): plain vertical cut.
					TArray<double> Params = ArcParams(Arc.From, Arc.From + Arc.Sweep, 0.0, L.Step);
					if (!E.bForward)
					{
						Algo::Reverse(Params);
					}
					for (int32 i = 0; i < Params.Num(); ++i)
					{
						const FV2 P = L.CirclePoint(p, Params[i], Pk.Rp);
						AppendUnique(TopLoop, {At(P, 0.0)});
						AppendUnique(BottomLoop, {At(P, -L.T)});
						if (i + 1 < Params.Num())
						{
							ColumnStrip(W, L, p, Params[i], {0.0, -L.T}, Params[i + 1], {0.0, -L.T});
						}
					}
					continue;
				}

				// Front arc: [plain L] [open, rounded] [plain R] in increasing phi.
				const FFrontArc& Fr = L.Fronts[p];
				const FFrontSide& SL = Fr.Side[0];
				const FFrontSide& SR = Fr.Side[1];
				struct FCol
				{
					double Phi;
					TArray<double> ZPlainSide; // column vertices used toward the plain neighbour
					TArray<double> ZOpenSide;  // ... toward the open neighbour
				};
				TArray<FCol> Cols;
				TArray<FV3> TopChain;
				TArray<double> PlainL = SL.bTrimRect ? TArray<double>() : ArcParams(SL.PhiRect, SL.PhiRpTrim, 0.0, L.Step);
				TArray<double> PlainR = SR.bTrimRect ? TArray<double>() : ArcParams(SR.PhiRpTrim, SR.PhiRect, 0.0, L.Step);
				const TArray<double>& Open = Fr.RingParams[Rings];
				for (int32 i = 0; i < PlainL.Num(); ++i)
				{
					TopChain.Add(At(L.CirclePoint(p, PlainL[i], Pk.Rp), 0.0));
					if (i + 1 < PlainL.Num())
					{
						Cols.Add({PlainL[i], {0.0, -L.T}, {0.0, -L.T}});
					}
				}
				for (const double Phi : Fr.RingParams[0])
				{
					TopChain.Add(At(L.CirclePoint(p, Phi, Pk.Ad), 0.0));
				}
				for (int32 i = 0; i < Open.Num(); ++i)
				{
					const bool bFirst = i == 0;
					const bool bLast = i + 1 == Open.Num();
					if ((bFirst && !SL.bTrimRect) || (bLast && !SR.bTrimRect))
					{
						Cols.Add({Open[i], {0.0, -Pk.Rd, -L.T}, {-Pk.Rd, -L.T}});
					}
					else
					{
						Cols.Add({Open[i], {-Pk.Rd, -L.T}, {-Pk.Rd, -L.T}});
					}
				}
				for (int32 i = 0; i < PlainR.Num(); ++i)
				{
					TopChain.Add(At(L.CirclePoint(p, PlainR[i], Pk.Rp), 0.0));
					if (i > 0)
					{
						Cols.Add({PlainR[i], {0.0, -L.T}, {0.0, -L.T}});
					}
				}
				// Walls at r_p between consecutive columns.
				const int32 OpenFirst = PlainL.Num() > 0 ? PlainL.Num() - 1 : 0;
				const int32 OpenLast = OpenFirst + Open.Num() - 1;
				for (int32 c = 0; c + 1 < Cols.Num(); ++c)
				{
					const bool bOpenStrip = c >= OpenFirst && c + 1 <= OpenLast;
					ColumnStrip(W, L, p, Cols[c].Phi, bOpenStrip ? Cols[c].ZOpenSide : Cols[c].ZPlainSide, Cols[c + 1].Phi,
						bOpenStrip ? Cols[c + 1].ZOpenSide : Cols[c + 1].ZPlainSide);
				}
				// Rounding (torus quarter) between the rings.
				for (int32 k = 0; k < Rings; ++k)
				{
					TArray<FV3> RA;
					TArray<FV3> NA;
					TArray<FV3> RB;
					TArray<FV3> NB;
					for (int32 Side = 0; Side < 2; ++Side)
					{
						const int32 Ring = k + Side;
						const double Psi = 0.5 * kPi * Ring / Rings;
						for (const double Phi : Fr.RingParams[Ring])
						{
							const FV2 Radial = Dir(Pk.ThetaRef + Phi);
							const FV3 P = At(Pk.C + Radial * RingRho(p, Ring), RingZ(p, Ring));
							const FV3 N = At(-Radial * FMath::Sin(Psi), FMath::Cos(Psi));
							(Side == 0 ? RA : RB).Add(P);
							(Side == 0 ? NA : NB).Add(N);
						}
					}
					W.Zip(RA, NA, Fr.RingParams[k], RB, NB, Fr.RingParams[k + 1]);
				}
				// Trim faces where the rounding meets a plain range under a cushion (vertical plane of the facing bottom line).
				for (int32 s = 0; s < 2; ++s)
				{
					const FFrontSide& Side = Fr.Side[s];
					if (Side.bTrimRect)
					{
						continue;
					}
					TArray<FV3> Face;
					for (int32 k = 0; k <= Rings; ++k)
					{
						Face.Add(At(L.CirclePoint(p, Side.RingPhi[k], RingRho(p, k)), RingZ(p, k)));
					}
					Face.Add(At(L.CirclePoint(p, Side.PhiRpTrim, Pk.Rp), 0.0));
					W.Polygon({Face}, At(Side.TrimNormal, 0.0));
				}
				// Loops.
				TArray<FV3> Bottom;
				TArray<double> AllPhi = PlainL;
				for (const double Phi : Open)
				{
					if (AllPhi.Num() == 0 || AllPhi.Last() != Phi)
					{
						AllPhi.Add(Phi);
					}
				}
				for (const double Phi : PlainR)
				{
					if (AllPhi.Last() != Phi)
					{
						AllPhi.Add(Phi);
					}
				}
				for (const double Phi : AllPhi)
				{
					Bottom.Add(At(L.CirclePoint(p, Phi, Pk.Rp), -L.T));
				}
				if (!E.bForward)
				{
					Algo::Reverse(TopChain);
					Algo::Reverse(Bottom);
				}
				AppendUnique(TopLoop, TopChain);
				AppendUnique(BottomLoop, Bottom);
			}
			if (TopLoop.Num() > 2 && FV3::DistSquared(TopLoop[0], TopLoop.Last()) < 1e-20)
			{
				TopLoop.Pop();
			}
			if (BottomLoop.Num() > 2 && FV3::DistSquared(BottomLoop[0], BottomLoop.Last()) < 1e-20)
			{
				BottomLoop.Pop();
			}
			W.Polygon({TopLoop}, FV3(0.0, 0.0, 1.0));
			W.Polygon({BottomLoop}, FV3(0.0, 0.0, -1.0));
		}
	}

	// Cap / body pieces between the mitres (piece c runs along the rail from outer corner c to c + 1).
	TArray<FV2> RailPiece(const FLayout& L, int32 c, const FV2 Outer[4])
	{
		const int32 N = L.Union.P.Num();
		TArray<FV2> Poly = {Outer[c], Outer[(c + 1) % 4]};
		const int32 From = L.Mitre[(c + 1) % 4];
		const int32 To = L.Mitre[c];
		int32 i = From;
		for (int32 Guard = 0; Guard <= N; ++Guard)
		{
			Poly.Add(L.Union.P[i]);
			if (i == To)
			{
				break;
			}
			i = (i - 1 + N) % N;
		}
		return Poly;
	}

	TArray<FV2> SightLoop(const FLayout& L, const rb::Sight& S)
	{
		TArray<FV2> Loop;
		const double R = 0.5 * L.G->Spec.SightDiameter;
		for (int32 j = 0; j < L.O.SightSegments; ++j)
		{
			Loop.Add(FV2(S.Position.x, S.Position.y) + Dir(kTwoPi * j / L.O.SightSegments) * R);
		}
		return Loop;
	}

	int32 SightRail(const FLayout& L, const rb::Sight& S)
	{
		if (S.OnLongRail)
		{
			return S.Position.y < 0.0 ? 0 : 2;
		}
		return S.Position.x > 0.0 ? 1 : 3;
	}

	void BuildRailRing(FMeshWriter& W, const FLayout& L, double Z0, double Z1, bool bSights, double TopBand)
	{
		const FV2 Outer[4] = {FV2(-L.HlR, -L.HwR), FV2(L.HlR, -L.HwR), FV2(L.HlR, L.HwR), FV2(-L.HlR, L.HwR)};
		W.BeginSolid();
		for (int32 c = 0; c < 4; ++c)
		{
			const TArray<FV2> Piece = RailPiece(L, c, Outer);
			TArray<TArray<FV2>> Holes;
			if (bSights)
			{
				for (const rb::Sight& S : L.G->Sights)
				{
					if (SightRail(L, S) == c)
					{
						Holes.Add(SightLoop(L, S));
					}
				}
			}
			W.SetUVMode((c % 2) == 0 ? EUVMode::PlanarXY : EUVMode::PlanarYX);
			AddFlat(W, Piece, Holes, Z1, true);
			AddFlat(W, Piece, Holes, Z0, false);
		}
		W.SetUVMode(EUVMode::Box);
		FWallLoop OuterLoop;
		for (const FV2& P : Outer)
		{
			OuterLoop.Add(P);
		}
		AddWalls(W, OuterLoop, true, Z0, Z1, TopBand);
		// The union loop runs CCW around the table: the rail solid lies to its right.
		AddWalls(W, L.Union, false, Z0, Z1);
		if (bSights)
		{
			for (const rb::Sight& S : L.G->Sights)
			{
				FWallLoop Hole;
				const FV2 Centre(S.Position.x, S.Position.y);
				for (const FV2& P : SightLoop(L, S))
				{
					Hole.Add(P, true, Norm2(Centre - P));
				}
				AddWalls(W, Hole, false, Z0, Z1); // CCW loop, solid outside (to its right)
			}
		}
	}

	// Corner inserts: the rectangle corners left free behind a hole (TABLE_7FT_BAR islands), from the slate bottom up to the
	// rail top; their wall at r_p is the back wall up to WallTopZ as in the physics. Part of the liner mesh (the rubber corner
	// pocket liners of coin-op tables), so the whole back wall of the pocket is liner.
	void BuildRailBlocks(FMeshWriter& W, const FLayout& L)
	{
		for (int32 LoopIndex = 1; LoopIndex < L.Hole.BedLoops.Num(); ++LoopIndex)
		{
			FWallLoop Loop;
			for (const FLoopElement& E : L.Hole.BedLoops[LoopIndex])
			{
				if (!E.bArc)
				{
					for (const FV2& P : L.Hole.RectPieces[E.Index].Points)
					{
						if (Loop.P.Num() == 0 || FV2::DistSquared(Loop.P.Last(), P) > 1e-24)
						{
							Loop.Add(P);
						}
					}
					continue;
				}
				const FCircleArc& Arc = L.Hole.Arcs[E.Index];
				TArray<double> Params = ArcParams(Arc.From, Arc.From + Arc.Sweep, 0.0, L.Step);
				if (!E.bForward)
				{
					Algo::Reverse(Params);
				}
				for (int32 i = 0; i < Params.Num(); ++i)
				{
					const FV2 P = L.CirclePoint(Arc.Pocket, Params[i], L.Pockets[Arc.Pocket].Rp);
					const FV2 N = L.InwardFromCircle(Arc.Pocket, Params[i]);
					if (Loop.P.Num() > 0 && FV2::DistSquared(Loop.P.Last(), P) < 1e-24)
					{
						Loop.EdgeSmooth.Last() = true;
						Loop.PointNormal.Last() = N;
						continue;
					}
					Loop.Add(P, i + 1 < Params.Num(), N);
				}
			}
			if (Loop.P.Num() > 2 && FV2::DistSquared(Loop.P[0], Loop.P.Last()) < 1e-24)
			{
				Loop.P.Pop();
				Loop.PointNormal.Pop();
				Loop.EdgeSmooth.Pop();
			}
			W.BeginSolid();
			W.SetUVMode(EUVMode::Box);
			AddFlat(W, Loop.P, {}, L.Zc, true);
			AddFlat(W, Loop.P, {}, -L.T, false);
			AddWalls(W, Loop, SignedArea(Loop.P) > 0.0, -L.T, L.Zc);
		}
	}

	// Liner collars (FCollar): the collar polygon = outer path (anchor, strip, r_p + LinerT arc, strip, anchor), the edge back
	// to the r_p crossing, the hole arc r_p reversed; extruded from the slate bottom up to the rail top. Its inner wall IS the
	// physics' back wall (cylinder r_p up to WallTopZ).
	void BuildCollars(FMeshWriter& W, const FLayout& L)
	{
		W.SetUVMode(EUVMode::Box);
		for (const FCollar& Co : L.Collars)
		{
			const int32 p = Co.Pocket;
			FWallLoop Loop;
			for (int32 i = 0; i < Co.OuterPath.Num(); ++i)
			{
				Loop.Add(Co.OuterPath[i], Co.EdgeSmooth[i], Co.Radial[i]); // outward from the collar = away from the centre
			}
			for (int32 i = Co.InnerParams.Num() - 1; i >= 0; --i)
			{
				Loop.Add(L.CirclePoint(p, Co.InnerParams[i], L.Hole.Radius[p]), i > 0, L.InwardFromCircle(p, Co.InnerParams[i]));
			}
			W.BeginSolid();
			AddFlat(W, Loop.P, {}, L.Zc, true);
			AddFlat(W, Loop.P, {}, -L.T, false);
			AddWalls(W, Loop, SignedArea(Loop.P) > 0.0, -L.T, L.Zc);
		}
	}

	void BuildSkirt(FMeshWriter& W, const FLayout& L)
	{
		const FV2 Outer[4] = {FV2(-L.HlR, -L.HwR), FV2(L.HlR, -L.HwR), FV2(L.HlR, L.HwR), FV2(-L.HlR, L.HwR)};
		const double Hi = L.HlR - L.SkirtT;
		const double Wi = L.HwR - L.SkirtT;
		const FV2 Inner[4] = {FV2(-Hi, -Wi), FV2(Hi, -Wi), FV2(Hi, Wi), FV2(-Hi, Wi)};
		const double Z0 = -L.ApronDepth;
		const double Z1 = -L.T;
		W.BeginSolid();
		for (int32 c = 0; c < 4; ++c)
		{
			const TArray<FV2> Piece = {Outer[c], Outer[(c + 1) % 4], Inner[(c + 1) % 4], Inner[c]};
			W.SetUVMode((c % 2) == 0 ? EUVMode::PlanarXY : EUVMode::PlanarYX);
			AddFlat(W, Piece, {}, Z1, true);
			AddFlat(W, Piece, {}, Z0, false);
		}
		W.SetUVMode(EUVMode::Box);
		FWallLoop O;
		FWallLoop I;
		for (int32 c = 0; c < 4; ++c)
		{
			O.Add(Outer[c]);
			I.Add(Inner[c]);
		}
		AddWalls(W, O, true, Z0, Z1);
		AddWalls(W, I, false, Z0, Z1);
	}

	void BuildSights(FMeshWriter& W, const FLayout& L)
	{
		W.SetUVMode(EUVMode::Box);
		const FV3 Up(0.0, 0.0, 1.0);
		const FV3 Down(0.0, 0.0, -1.0);
		for (const rb::Sight& S : L.G->Sights)
		{
			W.BeginSolid();
			const FV2 Centre(S.Position.x, S.Position.y);
			const TArray<FV2> Ring = SightLoop(L, S);
			const double Z1 = L.Zc;
			const double Z0 = L.Zc - L.SightDepth;
			const int32 N = Ring.Num();
			for (int32 j = 0; j < N; ++j)
			{
				const FV2& P0 = Ring[j];
				const FV2& P1 = Ring[(j + 1) % N];
				W.Tri(At(Centre, Z1), At(P0, Z1), At(P1, Z1), Up, Up, Up);
				W.Tri(At(Centre, Z0), At(P0, Z0), At(P1, Z0), Down, Down, Down);
				const FV3 N0 = At(Norm2(P0 - Centre), 0.0);
				const FV3 N1 = At(Norm2(P1 - Centre), 0.0);
				W.Quad(At(P0, Z0), At(P1, Z0), At(P1, Z1), At(P0, Z1), N0, N1, N1, N0);
			}
		}
	}

	// Drop pocket / cup: a revolved closed profile (outer r_p, wall LinerT) hanging from the slate bottom.
	void BuildLiners(FMeshWriter& W, const FLayout& L)
	{
		W.SetUVMode(EUVMode::Box);
		const int32 BowlSegs = 6;
		for (int32 p = 0; p < rb::kPocketCount; ++p)
		{
			const FPocket& Pk = L.Pockets[p];
			const double Ro = Pk.Rp;
			const double Ri = Pk.Rp - L.LinerT;
			const double Bottom = L.bCabinet ? -(L.T + 0.09) : -(L.ApronDepth - 0.02);
			const double Z1 = FMath::Min(-L.T - 0.005, Bottom + Ro);
			// Profile (rho, z) + normal (rho, z), as separate strips (hard edges at the rim).
			struct FProf
			{
				TArray<FV2> P;
				TArray<FV2> N;
			};
			TArray<FProf> Strips;
			{
				FProf Rim;
				Rim.P = {FV2(Ri, -L.T), FV2(Ro, -L.T)};
				Rim.N = {FV2(0.0, 1.0), FV2(0.0, 1.0)};
				Strips.Add(Rim);
				FProf Out;
				Out.P.Add(FV2(Ro, -L.T));
				Out.N.Add(FV2(1.0, 0.0));
				for (int32 k = 0; k <= BowlSegs; ++k)
				{
					const double A = 0.5 * kPi * k / BowlSegs;
					Out.P.Add(FV2(Ro * FMath::Cos(A), Z1 - Ro * FMath::Sin(A)));
					Out.N.Add(FV2(FMath::Cos(A), -FMath::Sin(A)));
				}
				Strips.Add(Out);
				FProf In;
				In.P.Add(FV2(Ri, -L.T));
				In.N.Add(FV2(-1.0, 0.0));
				for (int32 k = 0; k <= BowlSegs; ++k)
				{
					const double A = 0.5 * kPi * k / BowlSegs;
					In.P.Add(FV2(Ri * FMath::Cos(A), Z1 - Ri * FMath::Sin(A)));
					In.N.Add(FV2(-FMath::Cos(A), FMath::Sin(A)));
				}
				Strips.Add(In);
			}
			W.BeginSolid();
			const int32 Segs = L.O.CircleSegments;
			for (const FProf& S : Strips)
			{
				for (int32 j = 0; j < Segs; ++j)
				{
					const FV2 D0 = Dir(Pk.ThetaRef + L.Step * j);
					const FV2 D1 = Dir(Pk.ThetaRef + L.Step * (j + 1));
					for (int32 k = 0; k + 1 < S.P.Num(); ++k)
					{
						auto Pos = [&Pk](const FV2& Dirn, const FV2& Pr) { return At(Pk.C + Dirn * Pr.X, Pr.Y); };
						auto Nrm = [](const FV2& Dirn, const FV2& Nr) { return At(Dirn * Nr.X, Nr.Y).GetSafeNormal(); };
						W.Quad(Pos(D0, S.P[k]), Pos(D1, S.P[k]), Pos(D1, S.P[k + 1]), Pos(D0, S.P[k + 1]), Nrm(D0, S.N[k]), Nrm(D1, S.N[k]),
							Nrm(D1, S.N[k + 1]), Nrm(D0, S.N[k + 1]));
					}
				}
			}
		}
	}

	void BuildLegs(FMeshWriter& W, const FLayout& L)
	{
		W.SetUVMode(EUVMode::Box);
		const double Floor = -L.BedHeight;
		const double Top = -L.ApronDepth;
		const double Inset = L.SkirtT;
		if (L.bCabinet)
		{
			// Coin-op cabinet (recessed behind the skirt) on four leveller feet.
			const double FootH = 0.03;
			const double X = L.HlR - Inset;
			const double Y = L.HwR - Inset;
			AddBox(W, FV2(-X, -Y), FV2(X, Y), Floor + FootH, Top);
			const double F = 0.08;
			const double Fi = 0.04;
			for (int32 c = 0; c < 4; ++c)
			{
				const double Sx = (c == 1 || c == 2) ? 1.0 : -1.0;
				const double Sy = c >= 2 ? 1.0 : -1.0;
				const FV2 Centre(Sx * (X - Fi - 0.5 * F), Sy * (Y - Fi - 0.5 * F));
				AddBox(W, Centre - FV2(0.5 * F, 0.5 * F), Centre + FV2(0.5 * F, 0.5 * F), Floor, Floor + FootH);
			}
			return;
		}
		const double S = L.O.LegSizeCm / FRbCoords::CmPerMeter;
		const double LegInset = Inset + 0.01;
		for (int32 c = 0; c < 4; ++c)
		{
			const double Sx = (c == 1 || c == 2) ? 1.0 : -1.0;
			const double Sy = c >= 2 ? 1.0 : -1.0;
			const FV2 Centre(Sx * (L.HlR - LegInset - 0.5 * S), Sy * (L.HwR - LegInset - 0.5 * S));
			AddBox(W, Centre - FV2(0.5 * S, 0.5 * S), Centre + FV2(0.5 * S, 0.5 * S), Floor, Top);
		}
	}

	bool BuildFromLayout(const FLayout& L, ERbTablePart Part, FDynamicMesh3& Out, FString& OutError)
	{
		FMeshWriter W(Out);
		switch (Part)
		{
		case ERbTablePart::Bed:
			BuildBed(W, L);
			break;
		case ERbTablePart::CushionCloth:
			for (const FCushion& Cu : L.Cushions)
			{
				BuildCushion(W, L, Cu);
			}
			break;
		case ERbTablePart::RailCaps:
			BuildRailRing(W, L, L.Zc - L.CapT, L.Zc, true, L.CapBand);
			break;
		case ERbTablePart::Apron:
			BuildRailRing(W, L, -L.T, L.Zc - L.CapT, false, 0.0);
			BuildSkirt(W, L);
			break;
		case ERbTablePart::PocketLiners:
			BuildLiners(W, L);
			BuildCollars(W, L);
			BuildRailBlocks(W, L);
			break;
		case ERbTablePart::Sights:
			BuildSights(W, L);
			break;
		case ERbTablePart::Legs:
			if (L.O.bBuildLegs)
			{
				BuildLegs(W, L);
			}
			break;
		case ERbTablePart::Count:
			break;
		}
		W.Finish();
		if (!W.Ok())
		{
			OutError = FString::Printf(TEXT("%s: %s"), RbTypes::ToString(Part), *W.GetError());
			return false;
		}
		return true;
	}
} // namespace RbTableMeshBuilderPrivate

namespace RbTableMeshBuilder
{
	using namespace RbTableMeshBuilderPrivate;

	bool BuildAll(const rb::TableGeometry& Geometry, const FRbTableMeshOptions& Options, FRbTableMeshSet& Out, FString& OutError)
	{
		FLayout Layout;
		if (!BuildLayout(Geometry, Options, Layout, OutError))
		{
			for (UE::Geometry::FDynamicMesh3& Mesh : Out.Parts)
			{
				Mesh.Clear();
			}
			return false;
		}
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			if (!BuildFromLayout(Layout, static_cast<ERbTablePart>(PartIndex), Out.Parts[PartIndex], OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool BuildPart(const rb::TableGeometry& Geometry, ERbTablePart Part, const FRbTableMeshOptions& Options,
		UE::Geometry::FDynamicMesh3& Out, FString& OutError)
	{
		Out.Clear();
		FLayout Layout;
		if (!BuildLayout(Geometry, Options, Layout, OutError))
		{
			return false;
		}
		return BuildFromLayout(Layout, Part, Out, OutError);
	}

	FString GetPresetName(ERbTablePreset Preset)
	{
		const UEnum* Enum = StaticEnum<ERbTablePreset>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Preset)) : FString::Printf(TEXT("Preset%d"), static_cast<int32>(Preset));
	}

	FString GetBakedMeshPackagePath(ERbTablePreset Preset, ERbTablePart Part)
	{
		return FString::Printf(TEXT("%s/%s/SM_Table_%s"), RbAssetPaths::TableMeshDir, *GetPresetName(Preset), RbTypes::ToString(Part));
	}

	FString GetBakedMeshObjectPath(ERbTablePreset Preset, ERbTablePart Part)
	{
		return FString::Printf(TEXT("%s.SM_Table_%s"), *GetBakedMeshPackagePath(Preset, Part), RbTypes::ToString(Part));
	}

	bool PartHasCollision(ERbTablePart Part)
	{
		return Part != ERbTablePart::Sights;
	}

	bool PartUsesNanite(ERbTablePart Part)
	{
		return Part != ERbTablePart::Sights;
	}
}
