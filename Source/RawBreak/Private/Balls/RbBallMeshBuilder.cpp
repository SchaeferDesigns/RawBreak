#include "Balls/RbBallMeshBuilder.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"

// Owner: UE-2.

namespace
{
	using UE::Geometry::FDynamicMesh3;
	using UE::Geometry::FIndex3i;

	uint32 FloatBits(float V)
	{
		uint32 B = 0;
		FMemory::Memcpy(&B, &V, sizeof(B));
		return B;
	}

	using FElementKey = TTuple<int32, uint32, uint32, uint32>;

	// Overlay elements shared between triangles when (vertex, value) are identical, split otherwise (seams, poles, folds).
	template <typename OverlayType, typename ValueType, int32 N>
	struct TElementCache
	{
		OverlayType* Overlay = nullptr;
		TMap<FElementKey, int32> Map;

		int32 Get(int32 Vertex, const ValueType& Value)
		{
			uint32 Third = 0;
			if constexpr (N > 2)
			{
				Third = FloatBits(Value[2]);
			}
			const FElementKey Key(Vertex, FloatBits(Value[0]), FloatBits(Value[1]), Third);
			if (const int32* Found = Map.Find(Key))
			{
				return *Found;
			}
			const int32 Element = Overlay->AppendElement(Value);
			Map.Add(Key, Element);
			return Element;
		}
	};

	// Unit direction on ring I (polar angle pi I / Rings) and column J (longitude 2 pi J / Segments), with the exact
	// values on the equator, the quarter meridians and a mirror-symmetric southern hemisphere (bitwise z(i) = -z(R - i)).
	void RingDirection(int32 I, int32 J, int32 Segments, int32 Rings, double& SinTheta, double& CosTheta, double& CosPhi, double& SinPhi)
	{
		const int32 Mirror = Rings - I;
		const int32 Upper = FMath::Min(I, Mirror);
		const double Theta = UE_DOUBLE_PI * static_cast<double>(Upper) / static_cast<double>(Rings);
		SinTheta = 2 * Upper == Rings ? 1.0 : FMath::Sin(Theta);
		CosTheta = 2 * Upper == Rings ? 0.0 : FMath::Cos(Theta);
		if (Mirror < I)
		{
			CosTheta = -CosTheta;
		}
		const int32 Wrapped = ((J % Segments) + Segments) % Segments;
		const int32 Quarter = Segments / 4;
		if (Wrapped % Quarter == 0)
		{
			static constexpr double QuarterCos[4] = {1.0, 0.0, -1.0, 0.0};
			static constexpr double QuarterSin[4] = {0.0, 1.0, 0.0, -1.0};
			CosPhi = QuarterCos[Wrapped / Quarter];
			SinPhi = QuarterSin[Wrapped / Quarter];
		}
		else
		{
			const double Phi = UE_DOUBLE_TWO_PI * static_cast<double>(Wrapped) / static_cast<double>(Segments);
			CosPhi = FMath::Cos(Phi);
			SinPhi = FMath::Sin(Phi);
		}
	}
}

namespace RbBallMeshBuilder
{
	FRbBallMeshOptions Sanitize(const FRbBallMeshOptions& Options)
	{
		FRbBallMeshOptions Out;
		Out.Segments = FMath::Max(8, (FMath::Max(Options.Segments, 1) + 3) / 4 * 4);
		Out.Rings = FMath::Max(4, (FMath::Max(Options.Rings, 1) + 1) / 2 * 2);
		return Out;
	}

	int32 TriangleCount(const FRbBallMeshOptions& Options)
	{
		const FRbBallMeshOptions O = Sanitize(Options);
		return 2 * O.Segments * (O.Rings - 1);
	}

	FVector2f OctahedralEncode(const FVector3d& N, double SignX, double SignY)
	{
		const double L1 = FMath::Abs(N.X) + FMath::Abs(N.Y) + FMath::Abs(N.Z);
		double PX = N.X / L1;
		double PY = N.Y / L1;
		if (N.Z < 0.0)
		{
			const double FX = (1.0 - FMath::Abs(PY)) * (SignX < 0.0 ? -1.0 : 1.0);
			const double FY = (1.0 - FMath::Abs(PX)) * (SignY < 0.0 ? -1.0 : 1.0);
			PX = FX;
			PY = FY;
		}
		return FVector2f(static_cast<float>(0.5 * PX + 0.5), static_cast<float>(0.5 * PY + 0.5));
	}

	FVector2f OctahedralEncode(const FVector3d& N)
	{
		return OctahedralEncode(N, N.X < 0.0 ? -1.0 : 1.0, N.Y < 0.0 ? -1.0 : 1.0);
	}

	FVector3d OctahedralDecode(const FVector2f& UV)
	{
		double PX = 2.0 * UV.X - 1.0;
		double PY = 2.0 * UV.Y - 1.0;
		const double Z = 1.0 - FMath::Abs(PX) - FMath::Abs(PY);
		if (Z < 0.0)
		{
			const double FX = (1.0 - FMath::Abs(PY)) * (PX < 0.0 ? -1.0 : 1.0);
			const double FY = (1.0 - FMath::Abs(PX)) * (PY < 0.0 ? -1.0 : 1.0);
			PX = FX;
			PY = FY;
		}
		return FVector3d(PX, PY, Z).GetSafeNormal();
	}

	void BuildUnitSphere(const FRbBallMeshOptions& Options, FDynamicMesh3& Out)
	{
		const FRbBallMeshOptions O = Sanitize(Options);
		const int32 S = O.Segments;
		const int32 R = O.Rings;
		const int32 Quarter = S / 4;

		Out.Clear();
		Out.EnableAttributes();
		UE::Geometry::FDynamicMeshAttributeSet* Attributes = Out.Attributes();
		Attributes->SetNumUVLayers(2);
		Attributes->EnableTangents();

		// Vertices: north pole, rings 1 .. R-1 (S each), south pole.
		const int32 North = Out.AppendVertex(FVector3d(0.0, 0.0, 1.0));
		for (int32 I = 1; I < R; ++I)
		{
			for (int32 J = 0; J < S; ++J)
			{
				double ST, CT, CP, SP;
				RingDirection(I, J, S, R, ST, CT, CP, SP);
				Out.AppendVertex(FVector3d(ST * CP, ST * SP, CT));
			}
		}
		const int32 South = Out.AppendVertex(FVector3d(0.0, 0.0, -1.0));
		auto RingVertex = [S](int32 I, int32 J) { return 1 + (I - 1) * S + (J % S); };

		TElementCache<UE::Geometry::FDynamicMeshNormalOverlay, FVector3f, 3> Normals{Attributes->PrimaryNormals()};
		TElementCache<UE::Geometry::FDynamicMeshNormalOverlay, FVector3f, 3> Tangents{Attributes->PrimaryTangents()};
		TElementCache<UE::Geometry::FDynamicMeshNormalOverlay, FVector3f, 3> Bitangents{Attributes->PrimaryBiTangents()};
		TElementCache<UE::Geometry::FDynamicMeshUVOverlay, FVector2f, 2> UV0{Attributes->GetUVLayer(0)};
		TElementCache<UE::Geometry::FDynamicMeshUVOverlay, FVector2f, 2> UV1{Attributes->GetUVLayer(1)};

		// One corner of a triangle: ring I (0 = north pole, R = south pole), unwrapped column J (0 .. S), the triangle's
		// column (pole corners use its mid longitude) and the octant signs of the triangle.
		struct FCorner
		{
			int32 Vertex;
			int32 N, T, B, U0, U1;
		};
		auto MakeCorner = [&](int32 I, int32 J, int32 TriColumn, double SignX, double SignY) -> FCorner
		{
			FCorner C;
			double ST, CT, CP, SP;
			const bool bPole = I == 0 || I == R;
			if (bPole)
			{
				const double PhiMid = UE_DOUBLE_TWO_PI * (static_cast<double>(TriColumn) + 0.5) / static_cast<double>(S);
				CP = FMath::Cos(PhiMid);
				SP = FMath::Sin(PhiMid);
				ST = 0.0;
				CT = I == 0 ? 1.0 : -1.0;
				C.Vertex = I == 0 ? North : South;
			}
			else
			{
				RingDirection(I, J, S, R, ST, CT, CP, SP);
				C.Vertex = RingVertex(I, J);
			}
			const FVector3d P = Out.GetVertex(C.Vertex);
			const FVector3f Tangent(static_cast<float>(-SP), static_cast<float>(CP), 0.0f);
			const FVector3f Bitangent(static_cast<float>(CP * CT), static_cast<float>(SP * CT), static_cast<float>(-ST));
			const float U = bPole ? static_cast<float>((static_cast<double>(TriColumn) + 0.5) / static_cast<double>(S))
				: static_cast<float>(static_cast<double>(J) / static_cast<double>(S));
			const float V = static_cast<float>(static_cast<double>(I) / static_cast<double>(R));
			C.N = Normals.Get(C.Vertex, FVector3f(P));
			C.T = Tangents.Get(C.Vertex, Tangent);
			C.B = Bitangents.Get(C.Vertex, Bitangent);
			C.U0 = UV0.Get(C.Vertex, FVector2f(U, V));
			C.U1 = UV1.Get(C.Vertex, OctahedralEncode(P, SignX, SignY));
			return C;
		};
		auto AddTriangle = [&](const FCorner& A, const FCorner& B, const FCorner& C)
		{
			const int32 Tri = Out.AppendTriangle(A.Vertex, B.Vertex, C.Vertex);
			check(Tri >= 0);
			Normals.Overlay->SetTriangle(Tri, FIndex3i(A.N, B.N, C.N));
			Tangents.Overlay->SetTriangle(Tri, FIndex3i(A.T, B.T, C.T));
			Bitangents.Overlay->SetTriangle(Tri, FIndex3i(A.B, B.B, C.B));
			UV0.Overlay->SetTriangle(Tri, FIndex3i(A.U0, B.U0, C.U0));
			UV1.Overlay->SetTriangle(Tri, FIndex3i(A.U1, B.U1, C.U1));
		};

		for (int32 J = 0; J < S; ++J)
		{
			// Octant of every triangle in this column (the quarter meridians are column boundaries).
			const int32 Q = J / Quarter;
			const double SX = (Q == 0 || Q == 3) ? 1.0 : -1.0;
			const double SY = (Q == 0 || Q == 1) ? 1.0 : -1.0;

			// North cap: (pole, (1, j+1), (1, j)) - outward in UE's winding (VectorUtil::Normal).
			AddTriangle(MakeCorner(0, J, J, SX, SY), MakeCorner(1, J + 1, J, SX, SY), MakeCorner(1, J, J, SX, SY));
			// Bands between ring I (upper) and I + 1: (A, B, C) and (B, D, C) with A = (I, j), B = (I, j+1), C = (I+1, j), D = (I+1, j+1).
			for (int32 I = 1; I + 1 < R; ++I)
			{
				const FCorner A = MakeCorner(I, J, J, SX, SY);
				const FCorner B = MakeCorner(I, J + 1, J, SX, SY);
				const FCorner C = MakeCorner(I + 1, J, J, SX, SY);
				const FCorner D = MakeCorner(I + 1, J + 1, J, SX, SY);
				AddTriangle(A, B, C);
				AddTriangle(B, D, C);
			}
			// South cap: ((R-1, j), (R-1, j+1), pole).
			AddTriangle(MakeCorner(R - 1, J, J, SX, SY), MakeCorner(R - 1, J + 1, J, SX, SY), MakeCorner(R, J, J, SX, SY));
		}
	}
}
