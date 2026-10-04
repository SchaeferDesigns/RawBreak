// Table geometry -> meshes (UE-1, Docs/ue-architecture.md 5.3 / 13): RawBreak.Unit.Table.*
//   ClosedBoundaries   every part of every preset is a set of closed, consistently oriented solids
//   OutwardNormals     every solid has positive volume with UE's triangle-normal convention (outward in UE space) and
//                      the shading normals agree with the faces
//   NoseLine           the cushion nose line / jaw arcs at z = h are exactly BuildNoseOutline (0.01 mm); the facings lie in the
//                      physics' undercut facing planes
//   PocketCuts         capture cylinder r_p through bed, cushions, caps and apron; slate drop rounding r_d (torus)
//   Sights             18 flush sights at Sight::Position with SightDiameter, caps drilled for them
//   BedAndBounds       bed top z = 0, slate thickness, part bounds = cushion-back rectangle / OuterBoundary (+ apron)
//   PhysicsSurfaces    the top surfaces seen from above = cloth plane over the playing area, RailTopPolygon planes on the rails
//   BakedMatchesRuntime  the baked SM_Table_* assets carry exactly the runtime meshes (triangles, vertices = no geometry drift,
//                      bounds, Nanite 100 %)
//   Frames             CoreToWorld / WorldToCore / directions / orientations / azimuth round trips (translated + yawed); a
//                      scaled / tilted placement is reset to upright and unscaled
//   Collision          every part collides complex-as-simple (baked AND runtime components); the collision triangles are the
//                      render triangles: ray casts hit cloth, cushion top, rail caps, a sight and the apron on the physics surfaces
//   TransientParts     part components are transient, rebuilt at BeginPlay; a saved + reloaded level stores no meshes
// Tolerances: 0.01 mm = 1e-3 cm for geometry (the builder reproduces the physics surfaces to 1e-6 cm).

#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Table/RbTable.h"
#include "Table/RbTableMeshBuilder.h"
#include "Tests/RbTestFlags.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Selections/MeshConnectedComponents.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "VectorUtil.h"

#if WITH_EDITORONLY_DATA
#include "MeshDescription.h"
#endif

#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Quat.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbTableTestsPrivate
{
	using namespace UE::Geometry;

	constexpr double kTolCm = 1e-3; // 0.01 mm

	const ERbTablePreset kAllPresets[] = {ERbTablePreset::NineFootPro, ERbTablePreset::NineFootTight, ERbTablePreset::EightFootPro,
		ERbTablePreset::EightFootHome, ERbTablePreset::SevenFootBar, ERbTablePreset::SevenFoot78, ERbTablePreset::SevenFootTrue};

	// Presets baked and committed by Tools/unreal/editor/rb_bake_table.py (M1 room + the dive-bar table).
	const ERbTablePreset kBakedPresets[] = {ERbTablePreset::NineFootPro, ERbTablePreset::SevenFootBar};

	struct FBuiltTable
	{
		rb::TableGeometry Geometry;
		FRbTableMeshOptions Options;
		FRbTableMeshSet Meshes;
	};

	// Builds (once per test run) the geometry and every part of a preset with the default options (= runtime and bake).
	const FBuiltTable* GetBuilt(FAutomationTestBase& Test, ERbTablePreset Preset)
	{
		static TMap<ERbTablePreset, TSharedPtr<FBuiltTable>> Cache;
		if (const TSharedPtr<FBuiltTable>* Found = Cache.Find(Preset))
		{
			return Found->Get();
		}
		TSharedPtr<FBuiltTable> Built = MakeShared<FBuiltTable>();
		const rb::TableSpec Spec = rb::GetTableSpec(RbTypes::ToCore(Preset));
		const rb::ErrorCode Code = rb::BuildTableGeometry(Spec, Built->Geometry);
		if (!rb::Succeeded(Code))
		{
			Test.AddError(FString::Printf(TEXT("%s: BuildTableGeometry failed (%hs)"), *RbTableMeshBuilder::GetPresetName(Preset), rb::ToString(Code)));
			return nullptr;
		}
		FString Error;
		if (!RbTableMeshBuilder::BuildAll(Built->Geometry, Built->Options, Built->Meshes, Error))
		{
			Test.AddError(FString::Printf(TEXT("%s: BuildAll failed: %s"), *RbTableMeshBuilder::GetPresetName(Preset), *Error));
			return nullptr;
		}
		Cache.Add(Preset, Built);
		return Built.Get();
	}

	FString Label(ERbTablePreset Preset, ERbTablePart Part)
	{
		return FString::Printf(TEXT("%s/%s"), *RbTableMeshBuilder::GetPresetName(Preset), RbTypes::ToString(Part));
	}

	FVector3d ToUECm(const rb::Vec2& P, double ZMeters) { return FRbCoords::PositionToUE(rb::Vec3(P.x, P.y, ZMeters)); }

	double PlanDist(const FVector3d& A, const FVector3d& B) { return FVector2d(A.X - B.X, A.Y - B.Y).Size(); }

	double NearestVertexDistance(const FDynamicMesh3& Mesh, const FVector3d& P)
	{
		double Best = TNumericLimits<double>::Max();
		for (const int32 Vid : Mesh.VertexIndicesItr())
		{
			Best = FMath::Min(Best, FVector3d::Dist(Mesh.GetVertex(Vid), P));
		}
		return Best;
	}

	int32 FindVertex(const FDynamicMesh3& Mesh, const FVector3d& P, double Tol)
	{
		int32 Best = IndexConstants::InvalidID;
		double BestDist = Tol;
		for (const int32 Vid : Mesh.VertexIndicesItr())
		{
			const double D = FVector3d::Dist(Mesh.GetVertex(Vid), P);
			if (D <= BestDist)
			{
				BestDist = D;
				Best = Vid;
			}
		}
		return Best;
	}

	// True if the mesh vertices A and B are joined by a chain of mesh edges whose interior vertices all lie on the segment A-B (within
	// Tol, strictly advancing from A to B): a straight physics segment reproduced exactly, possibly split by collinear vertices (M2-L:
	// the nose roll's stations split the straight nose line; every vertex stays on it).
	bool IsCollinearEdgeChain(const FDynamicMesh3& Mesh, int32 A, int32 B, double Tol)
	{
		if (A < 0 || B < 0)
		{
			return false;
		}
		const FVector3d PA = Mesh.GetVertex(A);
		const FVector3d AB = Mesh.GetVertex(B) - PA;
		const double Len2 = AB.SquaredLength();
		if (Len2 < 1e-20)
		{
			return false;
		}
		int32 Cur = A;
		double CurT = 0.0;
		for (int32 Guard = 0; Guard < 4096 && Cur != B; ++Guard)
		{
			int32 Next = IndexConstants::InvalidID;
			double NextT = TNumericLimits<double>::Max();
			for (const int32 Nb : Mesh.VtxVerticesItr(Cur))
			{
				if (Nb == B)
				{
					Next = B;
					break;
				}
				const FVector3d P = Mesh.GetVertex(Nb);
				const double T = (P - PA).Dot(AB) / Len2;
				if (T > CurT + 1e-12 && T < 1.0 && (PA + AB * T - P).Size() <= Tol && T < NextT)
				{
					Next = Nb;
					NextT = T;
				}
			}
			if (Next == IndexConstants::InvalidID)
			{
				return false;
			}
			Cur = Next;
			CurT = NextT;
		}
		return Cur == B;
	}

	// Plan distance of P to the closed polyline Loop (UE cm).
	double DistToLoop(const TArray<FVector2d>& Loop, const FVector2d& P)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i < Loop.Num(); ++i)
		{
			const FVector2d A = Loop[i];
			const FVector2d B = Loop[(i + 1) % Loop.Num()];
			const FVector2d AB = B - A;
			const double T = FMath::Clamp((P - A).Dot(AB) / FMath::Max(AB.SquaredLength(), 1e-30), 0.0, 1.0);
			Best = FMath::Min(Best, (A + AB * T - P).Size());
		}
		return Best;
	}

	// Signed volume of a set of triangles with UE's triangle-normal convention (VectorUtil::NormalDirection): positive
	// when the triangles of a closed solid face outward.
	double SignedVolume(const FDynamicMesh3& Mesh, const TArray<int32>& Triangles)
	{
		double Volume = 0.0;
		for (const int32 Tid : Triangles)
		{
			FVector3d A;
			FVector3d B;
			FVector3d C;
			Mesh.GetTriVertices(Tid, A, B, C);
			Volume += A.Dot(VectorUtil::NormalDirection(A, B, C));
		}
		return Volume / 6.0;
	}

	FAxisAlignedBox3d Bounds(const FDynamicMesh3& Mesh) { return Mesh.GetBounds(true); }

	// Transient game world for actor tests (pattern of CQTest's FActorTestSpawner).
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RbTableTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FTestWorld() { Destroy(World); }

		static void Destroy(UWorld* InWorld)
		{
			if (!InWorld)
			{
				return;
			}
			InWorld->EndPlay(EEndPlayReason::LevelTransition); // routes EndPlay to begun actors (no-op if play never began)
			GEngine->ShutdownWorldNetDriver(InWorld);
			InWorld->DestroyWorld(true);
			InWorld->SetPhysicsScene(nullptr);
			if (GEngine->GetWorldContextFromWorld(InWorld))
			{
				GEngine->DestroyWorldContext(InWorld);
			}
			InWorld->RemoveFromRoot();
		}
	};

	// M2-L: parts differ per base style (the coin-op parts are empty on a legs table).
	ERbTableBaseStyle StyleOf(ERbTablePreset Preset)
	{
		return RbTableMeshBuilder::ResolveBaseStyle(rb::GetTableSpec(RbTypes::ToCore(Preset)), FRbTableMeshOptions());
	}

	bool Expected(ERbTablePreset Preset, ERbTablePart Part) { return RbTableMeshBuilder::PartExpected(StyleOf(Preset), Part); }

	int32 ExpectedPartCount(ERbTablePreset Preset)
	{
		int32 Count = 0;
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			Count += Expected(Preset, static_cast<ERbTablePart>(PartIndex)) ? 1 : 0;
		}
		return Count;
	}

	int32 CountPartComponents(const AActor* Actor)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
		int32 Count = 0;
		for (const UPrimitiveComponent* Component : Components)
		{
			Count += (Component && Component->ComponentHasTag(ARbTable::PartComponentTag)) ? 1 : 0;
		}
		return Count;
	}
}

// --- mesh topology ----------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableClosedBoundaries, "RawBreak.Unit.Table.ClosedBoundaries", RB_UNIT_TEST_FLAGS)
bool FRbTableClosedBoundaries::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			const ERbTablePart Part = static_cast<ERbTablePart>(PartIndex);
			const FDynamicMesh3& Mesh = Built->Meshes.Get(Part);
			const FString Name = Label(Preset, Part);
			if (!Expected(Preset, Part))
			{
				TestEqual(Name + TEXT(" is empty in this base style"), Mesh.TriangleCount(), 0);
				continue;
			}
			TestTrue(Name + TEXT(" has triangles"), Mesh.TriangleCount() > 0);
			TestTrue(Name + TEXT(" is valid"), Mesh.CheckValidity(FDynamicMesh3::FValidityOptions(), EValidityCheckFailMode::ReturnOnly));
			int32 BoundaryEdges = 0;
			int32 Inconsistent = 0;
			for (const int32 Eid : Mesh.EdgeIndicesItr())
			{
				const FDynamicMesh3::FEdge Edge = Mesh.GetEdge(Eid);
				if (Edge.Tri.B == IndexConstants::InvalidID)
				{
					++BoundaryEdges;
					continue;
				}
				// A consistently oriented surface traverses every interior edge once in each direction.
				auto Forward = [&Mesh, &Edge](int32 Tid) {
					const FIndex3i T = Mesh.GetTriangle(Tid);
					for (int32 j = 0; j < 3; ++j)
					{
						if (T[j] == Edge.Vert.A && T[(j + 1) % 3] == Edge.Vert.B)
						{
							return true;
						}
					}
					return false;
				};
				Inconsistent += Forward(Edge.Tri.A) == Forward(Edge.Tri.B) ? 1 : 0;
			}
			TestEqual(Name + TEXT(" boundary edges"), BoundaryEdges, 0);
			TestTrue(Name + TEXT(" IsClosed"), Mesh.IsClosed());
			TestEqual(Name + TEXT(" inconsistently oriented edges"), Inconsistent, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableOutwardNormals, "RawBreak.Unit.Table.OutwardNormals", RB_UNIT_TEST_FLAGS)
bool FRbTableOutwardNormals::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			const ERbTablePart Part = static_cast<ERbTablePart>(PartIndex);
			const FDynamicMesh3& Mesh = Built->Meshes.Get(Part);
			const FString Name = Label(Preset, Part);
			if (!Expected(Preset, Part))
			{
				continue;
			}

			FMeshConnectedComponents Components(&Mesh);
			Components.FindConnectedTriangles();
			int32 Inverted = 0;
			for (int32 c = 0; c < Components.Num(); ++c)
			{
				const double Volume = SignedVolume(Mesh, Components[c].Indices);
				if (!(Volume > 1e-3)) // cm^3; the smallest solid (a sight inlay) has ~0.4 cm^3
				{
					++Inverted;
					FAxisAlignedBox3d Box = FAxisAlignedBox3d::Empty();
					for (const int32 Tid : Components[c].Indices)
					{
						const FIndex3i T = Mesh.GetTriangle(Tid);
						Box.Contain(Mesh.GetVertex(T.A));
						Box.Contain(Mesh.GetVertex(T.B));
						Box.Contain(Mesh.GetVertex(T.C));
					}
					AddError(FString::Printf(TEXT("%s: solid %d has volume %.6g cm^3 (inward or degenerate), bounds %s .. %s"), *Name, c, Volume,
						*FVector(Box.Min).ToString(), *FVector(Box.Max).ToString()));
				}
			}
			TestEqual(Name + TEXT(" solids with outward orientation"), Components.Num() - Inverted, Components.Num());

			// Shading normals point to the same side as the faces (UE convention).
			const FDynamicMeshNormalOverlay* Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
			if (!TestNotNull(Name + TEXT(" normal overlay"), Normals))
			{
				continue;
			}
			int32 Disagree = 0;
			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				FVector3d A;
				FVector3d B;
				FVector3d C;
				Mesh.GetTriVertices(Tid, A, B, C);
				const FVector3d Face = VectorUtil::Normal(A, B, C);
				FVector3f N0;
				FVector3f N1;
				FVector3f N2;
				Normals->GetTriElements(Tid, N0, N1, N2);
				const bool bAgree = FVector3d(N0 + N1 + N2).Dot(Face) > 0.0;
				if (!bAgree && Disagree < 3)
				{
					AddError(FString::Printf(TEXT("%s: triangle %d (%s, %s, %s) face %s shading %s"), *Name, Tid, *FVector(A).ToString(), *FVector(B).ToString(),
						*FVector(C).ToString(), *FVector(Face).ToString(), *FVector(N0 + N1 + N2).ToString()));
				}
				Disagree += bAgree ? 0 : 1;
			}
			TestEqual(Name + TEXT(" triangles whose shading normals oppose the face"), Disagree, 0);
			TestTrue(Name + TEXT(" has UVs and tangents"), Mesh.Attributes()->NumUVLayers() > 0 && Mesh.Attributes()->HasTangentSpace());
		}

		// Cross-check against the physics frame: the bed's top faces point to +Z (core +z).
		const FDynamicMesh3& Bed = Built->Meshes.Get(ERbTablePart::Bed);
		double UpArea = 0.0;
		for (const int32 Tid : Bed.TriangleIndicesItr())
		{
			FVector3d A;
			FVector3d B;
			FVector3d C;
			Bed.GetTriVertices(Tid, A, B, C);
			if (FMath::Abs(A.Z) < 1e-9 && FMath::Abs(B.Z) < 1e-9 && FMath::Abs(C.Z) < 1e-9)
			{
				const FVector3d N = VectorUtil::NormalDirection(A, B, C);
				TestTrue(Label(Preset, ERbTablePart::Bed) + TEXT(" top triangle faces +Z"), N.Z > 0.0);
				UpArea += 0.5 * N.Size();
			}
		}
		// Bed top >= playing area minus the pocket openings (> 95 % of L x W).
		const double PlayArea = 4.0 * Built->Geometry.HalfLength * Built->Geometry.HalfWidth * 1e4;
		TestTrue(Label(Preset, ERbTablePart::Bed) + TEXT(" top area covers the playing area"), UpArea > 0.95 * PlayArea);
	}
	return true;
}

// --- physics surfaces ---------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableNoseLine, "RawBreak.Unit.Table.NoseLine", RB_UNIT_TEST_FLAGS)
bool FRbTableNoseLine::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		const rb::TableGeometry& G = Built->Geometry;
		const FDynamicMesh3& Mesh = Built->Meshes.Get(ERbTablePart::CushionCloth);
		const FString Name = Label(Preset, ERbTablePart::CushionCloth);
		const double H = G.Spec.CushionNoseHeight;
		const double ZhCm = FRbCoords::CmPerMeter * H;

		TArray<rb::Vec2> Outline;
		Outline.SetNum(rb::kMaxJaws * (Built->Options.ArcSegments + 2) + 8);
		const int32 Count = rb::BuildNoseOutline(G, Built->Options.ArcSegments, Outline.GetData(), Outline.Num());
		if (!TestTrue(Name + TEXT(" BuildNoseOutline"), Count > 0))
		{
			continue;
		}
		Outline.SetNum(Count);

		// Which outline points are facing ends (at the cushion back): per pocket [arc in][end in][end out][arc out].
		TArray<bool> IsFacingEnd;
		IsFacingEnd.Init(false, Count);
		{
			int32 Cursor = 0;
			for (int32 p = 0; p < rb::kPocketCount; ++p)
			{
				Cursor += G.JawArcs[2 * p].Radius > 0.0 ? FMath::Max(2, Built->Options.ArcSegments) : 1;
				IsFacingEnd[Cursor++] = true;
				IsFacingEnd[Cursor++] = true;
				Cursor += G.JawArcs[2 * p + 1].Radius > 0.0 ? FMath::Max(2, Built->Options.ArcSegments) : 1;
			}
			TestEqual(Name + TEXT(" outline layout"), Cursor, Count);
		}

		// (1) every nose / jaw-arc point of the outline is a mesh vertex at z = h; consecutive ones are joined by an edge.
		TArray<int32> Vids;
		Vids.Init(IndexConstants::InvalidID, Count);
		int32 Missing = 0;
		for (int32 i = 0; i < Count; ++i)
		{
			if (!IsFacingEnd[i])
			{
				Vids[i] = FindVertex(Mesh, ToUECm(Outline[i], H), kTolCm);
				if (Vids[i] == IndexConstants::InvalidID)
				{
					++Missing;
					AddError(FString::Printf(TEXT("%s: outline point %d (%.6f, %.6f) m has no vertex at z = h (nearest %.6f cm)"), *Name, i, Outline[i].x,
						Outline[i].y, NearestVertexDistance(Mesh, ToUECm(Outline[i], H))));
				}
			}
		}
		TestEqual(Name + TEXT(" outline points without a vertex"), Missing, 0);
		int32 MissingEdges = 0;
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 j = (i + 1) % Count;
			if (Vids[i] != IndexConstants::InvalidID && Vids[j] != IndexConstants::InvalidID && Mesh.FindEdge(Vids[i], Vids[j]) == IndexConstants::InvalidID
				&& !IsCollinearEdgeChain(Mesh, Vids[i], Vids[j], kTolCm))
			{
				++MissingEdges;
			}
		}
		TestEqual(Name + TEXT(" nose-line segments without a mesh edge"), MissingEdges, 0);

		// (2) every cushion vertex at z = h lies on the nose outline (plan, 0.01 mm), and no cushion vertex is above the
		// nose height inside the playing area.
		TArray<FVector2d> Loop;
		for (const rb::Vec2& P : Outline)
		{
			const FVector3d U = ToUECm(P, H);
			Loop.Add(FVector2d(U.X, U.Y));
		}
		int32 OffLine = 0;
		int32 AtH = 0;
		for (const int32 Vid : Mesh.VertexIndicesItr())
		{
			const FVector3d V = Mesh.GetVertex(Vid);
			if (FMath::Abs(V.Z - ZhCm) < 1e-6)
			{
				++AtH;
				const double D = DistToLoop(Loop, FVector2d(V.X, V.Y));
				if (D > kTolCm)
				{
					++OffLine;
					AddError(FString::Printf(TEXT("%s: vertex at z = h off the nose outline by %.6f cm at (%.4f, %.4f)"), *Name, D, V.X, V.Y));
				}
			}
		}
		TestTrue(Name + TEXT(" has vertices at z = h"), AtH >= Count - 12);
		TestEqual(Name + TEXT(" vertices at z = h off the outline"), OffLine, 0);

		// (3) the nose segments themselves (NoseSegment::Start / End at h) are edges of the mesh (or a chain of collinear edges on
		// the segment: M2-L's nose-roll stations), and the cushion never rises above the cushion-top plane / RailTopZ.
		for (const rb::NoseSegment& Nose : G.Noses)
		{
			const int32 A = FindVertex(Mesh, ToUECm(Nose.Start, H), kTolCm);
			const int32 B = FindVertex(Mesh, ToUECm(Nose.End, H), kTolCm);
			TestTrue(Name + TEXT(" nose segment is a mesh edge chain"),
				A >= 0 && B >= 0 && (Mesh.FindEdge(A, B) != IndexConstants::InvalidID || IsCollinearEdgeChain(Mesh, A, B, kTolCm)));
		}
		const FAxisAlignedBox3d Box = Bounds(Mesh);
		TestNearlyEqual(Name + TEXT(" top = RailTopZ"), Box.Max.Z, FRbCoords::CmPerMeter * G.Spec.RailTopZ, 1e-6);
		TestNearlyEqual(Name + TEXT(" bottom on the cloth"), Box.Min.Z, 0.0, 1e-6);

		// (4) the rendered facings ARE the physics' undercut facing planes: every cushion triangle facing along a Facing's
		// normal (PocketNormal tilted down by the back draft) near that facing lies in its plane (through the plan line at h).
		for (const rb::Facing& Fa : G.Facings)
		{
			const rb::Vec3 N(Fa.PocketNormal.x * FMath::Cos(Fa.Backdraft), Fa.PocketNormal.y * FMath::Cos(Fa.Backdraft), -FMath::Sin(Fa.Backdraft));
			const rb::Vec3 S(Fa.Start.x, Fa.Start.y, H);
			const FVector3d MidUE = ToUECm((Fa.Start + Fa.End) * 0.5, 0.5 * H);
			double FacingArea = 0.0;
			int32 OffPlane = 0;
			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				FVector3d A;
				FVector3d B;
				FVector3d C;
				Mesh.GetTriVertices(Tid, A, B, C);
				const FVector3d Centroid = (A + B + C) / 3.0;
				const rb::Vec3 Normal = FRbCoords::DirectionToCore(VectorUtil::Normal(A, B, C));
				if (FVector3d::Dist(Centroid, MidUE) > 10.0 || Normal.x * N.x + Normal.y * N.y + Normal.z * N.z < 1.0 - 1e-9)
				{
					continue;
				}
				FacingArea += VectorUtil::Area(A, B, C);
				for (const FVector3d& V : {A, B, C})
				{
					const rb::Vec3 P = FRbCoords::PositionToCore(V);
					OffPlane += FMath::Abs((P.x - S.x) * N.x + (P.y - S.y) * N.y + (P.z - S.z) * N.z) * FRbCoords::CmPerMeter > kTolCm ? 1 : 0;
				}
			}
			const FString FacingName = Name + FString::Printf(TEXT(" facing of pocket %d side %d"), static_cast<int32>(Fa.Pocket), static_cast<int32>(Fa.Side));
			TestTrue(FacingName + TEXT(" is rendered"), FacingArea > 0.5); // cm^2
			TestEqual(FacingName + TEXT(" vertices off the physics facing plane"), OffPlane, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTablePocketCuts, "RawBreak.Unit.Table.PocketCuts", RB_UNIT_TEST_FLAGS)
bool FRbTablePocketCuts::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		const rb::TableGeometry& G = Built->Geometry;
		const double T = FRbCoords::CmPerMeter * G.Spec.SlateThickness;
		const double HlC = FRbCoords::CmPerMeter * (G.HalfLength + G.Spec.CushionWidth);
		const double HwC = FRbCoords::CmPerMeter * (G.HalfWidth + G.Spec.CushionWidth);
		int32 OpenStretches = 0;
		for (const rb::PocketGeometry& Pg : G.Pockets)
		{
			const FString PocketName = FString::Printf(TEXT("%s pocket %d"), *RbTableMeshBuilder::GetPresetName(Preset), static_cast<int32>(Pg.Id));
			const FVector3d C = ToUECm(Pg.CaptureCenter, 0.0);
			const double Rp = FRbCoords::CmPerMeter * Pg.CaptureRadius;
			const double Rd = FRbCoords::CmPerMeter * Pg.DropRadius;
			const double Ad = FRbCoords::CmPerMeter * Pg.DropEdgeRadius;
			TestNearlyEqual(PocketName + TEXT(" a_d = r_p + r_d"), Ad, Rp + Rd, 1e-9);

			// Nothing of the table reaches inside the capture cylinder r_p (the hole through slate, cushions, collar and rails):
			// the bed, the cushions and the liner collar are cut at r_p, the rails behind the collar at r_p + LinerThickness (M2-L:
			// on the coin-op table the castings replace the rails around the pockets; the caps / rail body end before them).
			const double Rc = Rp + RbTableMeshBuilder::ResolveLinerThicknessCm(G.Spec, Built->Options);
			const bool bCabinet = StyleOf(Preset) == ERbTableBaseStyle::Cabinet;
			for (const ERbTablePart Part : {ERbTablePart::Bed, ERbTablePart::CushionCloth, ERbTablePart::RailCaps, ERbTablePart::Apron, ERbTablePart::PocketLiners,
					 ERbTablePart::Castings, ERbTablePart::RubberStrip})
			{
				if (!Expected(Preset, Part))
				{
					continue;
				}
				const bool bRail = Part == ERbTablePart::RailCaps || Part == ERbTablePart::Apron || Part == ERbTablePart::Castings;
				const bool bMustBeCut = Part != ERbTablePart::CushionCloth && Part != ERbTablePart::RubberStrip && !(bCabinet && bRail && Part != ERbTablePart::Castings);
				const double CutRadius = bRail ? Rc : Rp;
				const FDynamicMesh3& Mesh = Built->Meshes.Get(Part);
				double MinRho = TNumericLimits<double>::Max();
				int32 OnCylinder = 0;
				int32 OnCylinderAtTop = 0;
				for (const int32 Vid : Mesh.VertexIndicesItr())
				{
					const FVector3d V = Mesh.GetVertex(Vid);
					// Below the slate the apron skirt clears the holes and the drop pockets hang inside r_p (rim at the slate bottom).
					if (V.Z > (Part == ERbTablePart::PocketLiners ? -T + 1e-6 : -T - 1e-6))
					{
						const double Rho = PlanDist(V, C);
						MinRho = FMath::Min(MinRho, Rho);
						const bool bOn = FMath::Abs(Rho - CutRadius) < kTolCm;
						OnCylinder += bOn ? 1 : 0;
						OnCylinderAtTop += (bOn && FMath::Abs(V.Z - FRbCoords::CmPerMeter * Pg.WallTopZ) < 1e-6) ? 1 : 0;
					}
				}
				const FString Name = PocketName + TEXT(" ") + RbTypes::ToString(Part);
				TestTrue(Name + TEXT(" stays outside its cut cylinder"), MinRho > CutRadius - kTolCm);
				if (bMustBeCut)
				{
					TestTrue(Name + TEXT(" is cut by its cylinder"), OnCylinder >= 8);
				}
				if (Part == ERbTablePart::PocketLiners)
				{
					// The physics' back wall: cylinder r_p up to WallTopZ = RailTopZ, formed by the liner collar.
					TestNearlyEqual(PocketName + TEXT(" WallTopZ = RailTopZ"), Pg.WallTopZ, G.Spec.RailTopZ, 1e-12);
					TestTrue(PocketName + TEXT(" back wall at r_p reaches WallTopZ"), OnCylinderAtTop >= 4);
				}
			}

			// Slate drop rounding: every bed vertex closer than a_d lies on the r_p wall or on the torus (tube radius r_d about
			// the a_d circle at z = -r_d); the front point of the rim at a_d / z = 0, the wall start at r_p / z = -r_d and the
			// 45 deg point of the rounding are vertices.
			const FDynamicMesh3& Bed = Built->Meshes.Get(ERbTablePart::Bed);
			int32 Stray = 0;
			int32 OnTorus = 0;
			for (const int32 Vid : Bed.VertexIndicesItr())
			{
				const FVector3d V = Bed.GetVertex(Vid);
				const double Rho = PlanDist(V, C);
				const bool bOnRectangle = FMath::Abs(FMath::Abs(V.X) - HlC) < 1e-6 || FMath::Abs(FMath::Abs(V.Y) - HwC) < 1e-6;
				if (Rho > Ad - kTolCm || bOnRectangle) // corners of the slate islands behind a hole lie on the cushion-back rectangle
				{
					continue;
				}
				const bool bWall = FMath::Abs(Rho - Rp) < kTolCm;
				const double TubeDist = FMath::Abs(FVector2d(Rho - Ad, V.Z + Rd).Size() - Rd);
				const bool bTorus = V.Z <= kTolCm && V.Z >= -Rd - kTolCm && Rho <= Ad + kTolCm && TubeDist < kTolCm;
				OnTorus += (bTorus && !bWall) ? 1 : 0;
				if (!bWall && !bTorus)
				{
					++Stray;
					AddError(FString::Printf(TEXT("%s: bed vertex at rho %.5f cm, z %.5f cm is neither on the r_p wall nor on the r_d rounding"), *PocketName,
						Rho, V.Z));
				}
			}
			TestEqual(PocketName + TEXT(" stray bed vertices near the hole"), Stray, 0);
			TestTrue(PocketName + TEXT(" rounding vertices"), OnTorus >= 16);
			const FVector3d Inward = FRbCoords::DirectionToUE(rb::Vec3(-Pg.Axis.x, -Pg.Axis.y, 0.0));
			TestTrue(PocketName + TEXT(" rim front point at a_d, z = 0"), FindVertex(Bed, C + Inward * Ad, kTolCm) >= 0);
			TestTrue(PocketName + TEXT(" wall start at r_p, z = -r_d"), FindVertex(Bed, C + Inward * Rp + FVector3d(0.0, 0.0, -Rd), kTolCm) >= 0);
			const double S45 = FMath::Sin(0.25 * UE_DOUBLE_PI);
			TestTrue(PocketName + TEXT(" rounding at 45 deg"),
				FindVertex(Bed, C + Inward * (Ad - Rd * S45) + FVector3d(0.0, 0.0, -Rd + Rd * S45), kTolCm) >= 0);

			// No rail wood inside the pocket: where an undercut facing's bottom line (z = 0) meets the cushion-back rectangle
			// outside the collar ring, the stretch of the cushion-back line from there to the hole is lined (liner vertex at
			// the stretch's end, rail cut behind it: no cap / apron vertex on the open stretch).
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const rb::Facing& Fa = G.Facings[2 * static_cast<int32>(Pg.Id) + Side];
				const rb::Vec2 Bottom0 = Fa.Start - Fa.PocketNormal * (G.Spec.CushionNoseHeight * FMath::Tan(Fa.Backdraft));
				const FVector3d P0 = ToUECm(Bottom0, 0.0);
				const FVector3d Dir = FRbCoords::DirectionToUE(rb::Vec3(Fa.Direction.x, Fa.Direction.y, 0.0));
				double THit = TNumericLimits<double>::Max();
				for (const double Bound : {HlC, -HlC})
				{
					if (FMath::Abs(Dir.X) > 1e-12)
					{
						const double T0 = (Bound - P0.X) / Dir.X;
						THit = (T0 > 0.0 && FMath::Abs(P0.Y + T0 * Dir.Y) <= HwC + 1e-9) ? FMath::Min(THit, T0) : THit;
					}
				}
				for (const double Bound : {HwC, -HwC})
				{
					if (FMath::Abs(Dir.Y) > 1e-12)
					{
						const double T0 = (Bound - P0.Y) / Dir.Y;
						THit = (T0 > 0.0 && FMath::Abs(P0.X + T0 * Dir.X) <= HlC + 1e-9) ? FMath::Min(THit, T0) : THit;
					}
				}
				const FVector3d B = P0 + Dir * THit;
				if (!(PlanDist(B, C) > Rc + kTolCm))
				{
					continue; // the facing reaches the cushion back inside the hole or the collar ring: nothing open
				}
				++OpenStretches;
				const FString Name = PocketName + FString::Printf(TEXT(" facing %d"), Side);
				TestTrue(Name + TEXT(": liner strip starts where the facing meets the cushion back"),
					FindVertex(Built->Meshes.Get(ERbTablePart::PocketLiners), FVector3d(B.X, B.Y, FRbCoords::CmPerMeter * G.Spec.RailTopZ), kTolCm) >= 0);
				// Along the cushion-back line from B toward the hole, up to the r_p circle.
				const bool bOnX = FMath::Abs(FMath::Abs(B.X) - HlC) < 1e-6;
				const FVector3d Along = bOnX ? FVector3d(0.0, C.Y > B.Y ? 1.0 : -1.0, 0.0) : FVector3d(C.X > B.X ? 1.0 : -1.0, 0.0, 0.0);
				const double Dist = bOnX ? FMath::Abs(C.X - B.X) : FMath::Abs(C.Y - B.Y);
				const double ToHole = FMath::Abs(((C - B).Dot(Along))) - FMath::Sqrt(FMath::Max(0.0, Rp * Rp - Dist * Dist));
				int32 Exposed = 0;
				for (const ERbTablePart Rail : {ERbTablePart::RailCaps, ERbTablePart::Apron, ERbTablePart::Castings})
				{
					const FDynamicMesh3& Mesh = Built->Meshes.Get(Rail);
					for (const int32 Vid : Mesh.VertexIndicesItr())
					{
						const FVector3d V = Mesh.GetVertex(Vid);
						const double Off = bOnX ? FMath::Abs(V.X - B.X) : FMath::Abs(V.Y - B.Y);
						const double TV = (V - B).Dot(Along);
						Exposed += (Off < 1e-6 && TV > kTolCm && TV < ToHole + kTolCm) ? 1 : 0;
					}
				}
				TestEqual(Name + TEXT(": rail vertices on the open cushion-back stretch"), Exposed, 0);
			}

			// Drop pocket (M2-L: the PocketBuckets part): outer radius r_p, hanging from the slate bottom; liner collar (PocketLiners)
			// outer radius r_p + LinerThickness up to the rail top.
			double MaxRhoBelow = 0.0;
			double MinZ = TNumericLimits<double>::Max();
			double MaxZ = -TNumericLimits<double>::Max();
			const FDynamicMesh3& Buckets = Built->Meshes.Get(ERbTablePart::PocketBuckets);
			int32 BucketAbove = 0;
			for (const int32 Vid : Buckets.VertexIndicesItr())
			{
				const FVector3d V = Buckets.GetVertex(Vid);
				if (PlanDist(V, C) < Rc + 1.0)
				{
					MaxRhoBelow = FMath::Max(MaxRhoBelow, PlanDist(V, C));
					MinZ = FMath::Min(MinZ, V.Z);
					BucketAbove += V.Z > -T + 1e-6 ? 1 : 0;
				}
			}
			TestEqual(PocketName + TEXT(" bucket vertices above the slate bottom"), BucketAbove, 0);
			const FDynamicMesh3& Liners = Built->Meshes.Get(ERbTablePart::PocketLiners);
			for (const int32 Vid : Liners.VertexIndicesItr())
			{
				const FVector3d V = Liners.GetVertex(Vid);
				if (PlanDist(V, C) < Rc + 1.0)
				{
					MaxZ = FMath::Max(MaxZ, V.Z);
				}
			}
			TestNearlyEqual(PocketName + TEXT(" drop pocket outer radius r_p"), MaxRhoBelow, Rp, kTolCm);
			TestTrue(PocketName + TEXT(" drop pocket hangs below the slate"), MinZ < -T - 5.0);
			TestNearlyEqual(PocketName + TEXT(" liner collar up to the rail top"), MaxZ, FRbCoords::CmPerMeter * G.Spec.RailTopZ, 1e-6);
		}
		if (Preset == ERbTablePreset::SevenFootBar)
		{
			// The bar table's undercut corner facings meet the cushion back ~1.6 mm outside the collar ring: strips needed.
			TestTrue(TEXT("SevenFootBar has open cushion-back stretches (collar strips exercised)"), OpenStretches > 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableSights, "RawBreak.Unit.Table.Sights", RB_UNIT_TEST_FLAGS)
bool FRbTableSights::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		const rb::TableGeometry& G = Built->Geometry;
		const FString Name = Label(Preset, ERbTablePart::Sights);
		const FDynamicMesh3& Mesh = Built->Meshes.Get(ERbTablePart::Sights);
		const double Radius = 0.5 * FRbCoords::CmPerMeter * G.Spec.SightDiameter;
		const double TopZ = FRbCoords::CmPerMeter * G.Spec.RailTopZ;
		TestEqual(Name + TEXT(" core sights"), static_cast<int32>(G.Sights.Size()), 18);

		FMeshConnectedComponents Components(&Mesh);
		Components.FindConnectedTriangles();
		TestEqual(Name + TEXT(" sight solids"), Components.Num(), 18);

		TArray<bool> Matched;
		Matched.Init(false, G.Sights.Size());
		for (int32 c = 0; c < Components.Num(); ++c)
		{
			// Centre = mean of the rim vertices of the top face.
			FVector3d Sum = FVector3d::Zero();
			int32 N = 0;
			double MaxZ = -TNumericLimits<double>::Max();
			TSet<int32> Verts;
			for (const int32 Tid : Components[c].Indices)
			{
				const FIndex3i Tri = Mesh.GetTriangle(Tid);
				Verts.Add(Tri.A);
				Verts.Add(Tri.B);
				Verts.Add(Tri.C);
			}
			for (const int32 Vid : Verts)
			{
				MaxZ = FMath::Max(MaxZ, Mesh.GetVertex(Vid).Z);
			}
			FVector3d Centre = FVector3d::Zero();
			for (const int32 Vid : Verts)
			{
				Sum += Mesh.GetVertex(Vid);
				++N;
			}
			Centre = Sum / FMath::Max(N, 1);
			int32 Best = -1;
			double BestDist = TNumericLimits<double>::Max();
			for (int32 s = 0; s < G.Sights.Size(); ++s)
			{
				const double D = PlanDist(Centre, FRbCoords::PositionToUE(G.Sights[s].Position));
				if (D < BestDist)
				{
					BestDist = D;
					Best = s;
				}
			}
			TestTrue(Name + FString::Printf(TEXT(" solid %d centred on a Sight::Position"), c), BestDist < kTolCm);
			TestNearlyEqual(Name + TEXT(" flush with the rail top"), MaxZ, TopZ, 1e-6);
			if (Best >= 0)
			{
				TestFalse(Name + TEXT(" each sight matched once"), Matched[Best]);
				Matched[Best] = true;
				TestNearlyEqual(Name + TEXT(" sight z = RailTopZ"), FRbCoords::CmPerMeter * G.Sights[Best].Position.z, TopZ, 1e-9);
				const FVector3d P = FRbCoords::PositionToUE(G.Sights[Best].Position);
				double MaxR = 0.0;
				for (const int32 Vid : Verts)
				{
					MaxR = FMath::Max(MaxR, PlanDist(Mesh.GetVertex(Vid), P));
				}
				TestNearlyEqual(Name + TEXT(" sight radius = SightDiameter / 2"), MaxR, Radius, kTolCm);
			}
		}

		// The caps are drilled for every sight (rim vertices at the sight radius on the cap top).
		const FDynamicMesh3& Caps = Built->Meshes.Get(ERbTablePart::RailCaps);
		for (const rb::Sight& S : G.Sights)
		{
			const FVector3d P = FRbCoords::PositionToUE(S.Position);
			int32 Rim = 0;
			for (const int32 Vid : Caps.VertexIndicesItr())
			{
				const FVector3d V = Caps.GetVertex(Vid);
				Rim += (FMath::Abs(V.Z - TopZ) < 1e-6 && FMath::Abs(PlanDist(V, P) - Radius) < kTolCm) ? 1 : 0;
			}
			TestEqual(Label(Preset, ERbTablePart::RailCaps) + TEXT(" sight hole rim vertices"), Rim, Built->Options.SightSegments);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableBedAndBounds, "RawBreak.Unit.Table.BedAndBounds", RB_UNIT_TEST_FLAGS)
bool FRbTableBedAndBounds::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		const rb::TableGeometry& G = Built->Geometry;
		const rb::TableSpec& Spec = G.Spec;
		const double Cm = FRbCoords::CmPerMeter;
		const FString PresetName = RbTableMeshBuilder::GetPresetName(Preset);

		// Bed: top exactly on the cloth plane z = 0, slate thickness, footprint = the cushion-back rectangle.
		const FAxisAlignedBox3d Bed = Bounds(Built->Meshes.Get(ERbTablePart::Bed));
		TestEqual(PresetName + TEXT(" bed top z = 0"), Bed.Max.Z, 0.0);
		TestNearlyEqual(PresetName + TEXT(" bed bottom = -SlateThickness"), Bed.Min.Z, -Cm * Spec.SlateThickness, 1e-9);
		const double HlC = Cm * (G.HalfLength + Spec.CushionWidth);
		const double HwC = Cm * (G.HalfWidth + Spec.CushionWidth);
		TestTrue(PresetName + TEXT(" bed inside the cushion-back rectangle"),
			Bed.Max.X <= HlC + 1e-6 && Bed.Min.X >= -HlC - 1e-6 && Bed.Max.Y <= HwC + 1e-6 && Bed.Min.Y >= -HwC - 1e-6);
		TestNearlyEqual(PresetName + TEXT(" bed reaches the cushion back (side pockets)"), Bed.Max.Y, HwC, 1e-6);

		// Cushions: between the cloth and the rail top, inside the cushion-back rectangle; nose line at |x| = L/2.
		const FAxisAlignedBox3d Cushions = Bounds(Built->Meshes.Get(ERbTablePart::CushionCloth));
		TestTrue(PresetName + TEXT(" cushions inside the cushion-back rectangle"),
			Cushions.Max.X <= HlC + 1e-6 && Cushions.Min.X >= -HlC - 1e-6 && Cushions.Max.Y <= HwC + 1e-6 && Cushions.Min.Y >= -HwC - 1e-6);

		// Rails (caps + body): exactly the physics' OuterBoundary in plan, cap top at RailTopZ.
		const FAxisAlignedBox3d Caps = Bounds(Built->Meshes.Get(ERbTablePart::RailCaps));
		const FAxisAlignedBox3d Apron = Bounds(Built->Meshes.Get(ERbTablePart::Apron));
		const FVector3d OuterLo = ToUECm(G.OuterBoundary.Lo, 0.0);
		const FVector3d OuterHi = ToUECm(G.OuterBoundary.Hi, 0.0);
		const double OMinX = FMath::Min(OuterLo.X, OuterHi.X);
		const double OMaxX = FMath::Max(OuterLo.X, OuterHi.X);
		const double OMinY = FMath::Min(OuterLo.Y, OuterHi.Y);
		const double OMaxY = FMath::Max(OuterLo.Y, OuterHi.Y);
		// (M2-L: the rail body of the legs style sits 1.5 mm behind the cap edge - the cap overhangs it.)
		for (const TPair<const TCHAR*, FAxisAlignedBox3d>& Box : {TPair<const TCHAR*, FAxisAlignedBox3d>(TEXT("caps"), Caps),
				 TPair<const TCHAR*, FAxisAlignedBox3d>(TEXT("apron"), Apron)})
		{
			const FString Name = PresetName + TEXT(" ") + Box.Key;
			const double Tol = FCString::Strcmp(Box.Key, TEXT("caps")) == 0 ? 1e-6 : 0.2;
			TestNearlyEqual(Name + TEXT(" min X = OuterBoundary"), Box.Value.Min.X, OMinX, Tol);
			TestNearlyEqual(Name + TEXT(" max X = OuterBoundary"), Box.Value.Max.X, OMaxX, Tol);
			TestNearlyEqual(Name + TEXT(" min Y = OuterBoundary"), Box.Value.Min.Y, OMinY, Tol);
			TestNearlyEqual(Name + TEXT(" max Y = OuterBoundary"), Box.Value.Max.Y, OMaxY, Tol);
			TestTrue(Name + TEXT(" not beyond OuterBoundary"), Box.Value.Min.X >= OMinX - 1e-6 && Box.Value.Max.X <= OMaxX + 1e-6 &&
				Box.Value.Min.Y >= OMinY - 1e-6 && Box.Value.Max.Y <= OMaxY + 1e-6);
		}
		const bool bCabinet = StyleOf(Preset) == ERbTableBaseStyle::Cabinet;
		TestNearlyEqual(PresetName + TEXT(" cap top = RailTopZ"), Caps.Max.Z, Cm * Spec.RailTopZ, 1e-9);
		TestNearlyEqual(PresetName + TEXT(" cap slab thickness"), Caps.Min.Z, Cm * Spec.RailTopZ - Built->Options.CapThicknessCm, 1e-6);
		TestNearlyEqual(PresetName + TEXT(" apron top meets the cap"), Apron.Max.Z, Cm * Spec.RailTopZ - Built->Options.CapThicknessCm, 1e-6);
		// Legs style: the rail body and the apron skirt down to ApronDepth; coin-op: the rail body sits on the cabinet at the slate bottom.
		TestNearlyEqual(PresetName + TEXT(" apron depth"), Apron.Min.Z, bCabinet ? -Cm * Spec.SlateThickness : -Built->Options.ApronDepthCm, 1e-6);

		// Legs / pedestals stand on their levelers on the floor (z = -BedHeight) under the apron / cabinet; nothing of the table leaves
		// OuterBoundary except the coin-op's foot-end hardware (coin slide, trap window frame, return ring) and the coin door on the
		// wall side (venue-dive-bar 3.1: the slide protrudes 0.06 m).
		const FAxisAlignedBox3d Legs = Bounds(Built->Meshes.Get(ERbTablePart::Legs));
		const FAxisAlignedBox3d Hardware = Bounds(Built->Meshes.Get(ERbTablePart::Hardware));
		TestNearlyEqual(PresetName + TEXT(" levelers on the floor"), FMath::Min(Legs.Min.Z, Hardware.Min.Z), -Cm * Spec.BedHeight, 1e-6);
		TestTrue(PresetName + TEXT(" legs on their levelers"), Legs.Min.Z > -Cm * Spec.BedHeight + 0.5 && Legs.Min.Z < -Cm * Spec.BedHeight + 3.0);
		TestNearlyEqual(PresetName + TEXT(" legs meet the apron / cabinet"), Legs.Max.Z, bCabinet ? -Cm * Spec.BedHeight + 30.0 : -Built->Options.ApronDepthCm, 1e-6);
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			const ERbTablePart Part = static_cast<ERbTablePart>(PartIndex);
			if (!Expected(Preset, Part))
			{
				continue;
			}
			const FAxisAlignedBox3d Box = Bounds(Built->Meshes.Parts[PartIndex]);
			const bool bHardware = bCabinet && (Part == ERbTablePart::Hardware || Part == ERbTablePart::Castings);
			const double FootOut = bHardware ? 8.0 : 1e-6;  // cm beyond the foot end (+x): slide 6 cm + plate + knob
			const double SideOut = bHardware ? 1.0 : 1e-6;  // cm beyond the wall side (core -y = UE +Y)
			TestTrue(Label(Preset, Part) + TEXT(" inside OuterBoundary"),
				Box.Min.X >= OMinX - 1e-6 && Box.Max.X <= OMaxX + FootOut && Box.Min.Y >= OMinY - 1e-6 && Box.Max.Y <= OMaxY + SideOut &&
					Box.Max.Z <= Cm * Spec.RailTopZ + 1e-6 && Box.Min.Z >= -Cm * Spec.BedHeight - 1e-6);
		}
	}
	return true;
}

// The rendered surfaces seen from above ARE the physics surfaces: the highest mesh surface over every point of the playing
// area (not over a pocket opening) is the cloth plane z = 0, and over every rail-top polygon of the physics (cushion tops,
// caps, pocket surrounds) it is that polygon's plane - the surfaces a ball rolls on, lands on or leaves the table over.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTablePhysicsSurfaces, "RawBreak.Unit.Table.PhysicsSurfaces", RB_UNIT_TEST_FLAGS)
bool FRbTablePhysicsSurfaces::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		const rb::TableGeometry& G = Built->Geometry;
		const FString PresetName = RbTableMeshBuilder::GetPresetName(Preset);
		TArray<TUniquePtr<FDynamicMeshAABBTree3>> Trees;
		// (M2-L: the coin-op's castings are the rail top around its pockets.)
		for (const ERbTablePart Part : {ERbTablePart::Bed, ERbTablePart::CushionCloth, ERbTablePart::RailCaps, ERbTablePart::PocketLiners, ERbTablePart::Sights,
				 ERbTablePart::Castings})
		{
			if (Built->Meshes.Get(Part).TriangleCount() > 0)
			{
				Trees.Add(MakeUnique<FDynamicMeshAABBTree3>(&Built->Meshes.Get(Part), true));
			}
		}
		// Height [cm] of the highest rendered surface over a plan point (core metres).
		auto TopZ = [&Trees](const rb::Vec2& P, double& OutZ) {
			const FVector3d Origin = ToUECm(P, 2.0);
			const FRay3d Ray(Origin, FVector3d(0.0, 0.0, -1.0));
			bool bHit = false;
			OutZ = -TNumericLimits<double>::Max();
			for (const TUniquePtr<FDynamicMeshAABBTree3>& Tree : Trees)
			{
				double T = 0.0;
				int32 Tid = IndexConstants::InvalidID;
				if (Tree->FindNearestHitTriangle(Ray, T, Tid))
				{
					OutZ = FMath::Max(OutZ, Origin.Z - T);
					bHit = true;
				}
			}
			return bHit;
		};

		// (1) Playing area: the cloth plane.
		int32 Samples = 0;
		int32 Bad = 0;
		const int32 Nx = 64;
		const int32 Ny = 32;
		for (int32 i = 0; i < Nx; ++i)
		{
			for (int32 j = 0; j < Ny; ++j)
			{
				const rb::Vec2 P(-G.HalfLength + (i + 0.5) * 2.0 * G.HalfLength / Nx, -G.HalfWidth + (j + 0.5) * 2.0 * G.HalfWidth / Ny);
				if (rb::IsOverPocketOpening(G, P))
				{
					continue;
				}
				++Samples;
				double Z = 0.0;
				if (!TopZ(P, Z) || FMath::Abs(Z) > 1e-6)
				{
					++Bad;
				}
			}
		}
		TestTrue(PresetName + TEXT(" playing-area samples"), Samples > Nx * Ny * 9 / 10);
		TestEqual(PresetName + TEXT(" playing-area points whose top surface is not the cloth at z = 0"), Bad, 0);

		// (2) Rail tops: every physics polygon's plane (centroid and halfway to each vertex, away from the pocket cut discs).
		int32 RailSamples = 0;
		int32 RailBad = 0;
		for (const rb::RailTopPolygon& Poly : G.RailTops)
		{
			rb::Vec2 Centroid(0.0, 0.0);
			for (int32 k = 0; k < Poly.VertexCount; ++k)
			{
				Centroid += Poly.Vertices[k];
			}
			Centroid = Centroid / static_cast<double>(Poly.VertexCount);
			TArray<rb::Vec2> Points = {Centroid};
			for (int32 k = 0; k < Poly.VertexCount; ++k)
			{
				Points.Add((Centroid + Poly.Vertices[k]) * 0.5);
			}
			for (const rb::Vec2& S : Points)
			{
				if (Poly.HasCut && rb::Length(S - Poly.CutCenter) < Poly.CutRadius + 0.002)
				{
					continue;
				}
				const double PlaneZ = Poly.PlanePoint.z -
					(Poly.PlaneNormal.x * (S.x - Poly.PlanePoint.x) + Poly.PlaneNormal.y * (S.y - Poly.PlanePoint.y)) / Poly.PlaneNormal.z;
				double Z = 0.0;
				++RailSamples;
				if (!TopZ(S, Z) || FMath::Abs(Z - FRbCoords::CmPerMeter * PlaneZ) > kTolCm)
				{
					++RailBad;
					AddError(FString::Printf(TEXT("%s: rail top at (%.4f, %.4f) m is %.5f cm, physics plane %.5f cm (%s polygon)"), *PresetName, S.x, S.y, Z,
						FRbCoords::CmPerMeter * PlaneZ, Poly.Kind == rb::RailTopKind::CushionTop ? TEXT("cushion-top") : TEXT("cap")));
				}
			}
		}
		TestTrue(PresetName + TEXT(" rail-top samples"), RailSamples > 3 * static_cast<int32>(G.RailTops.Size()));
		TestEqual(PresetName + TEXT(" rail-top points off the physics planes"), RailBad, 0);
	}
	return true;
}

// --- baked assets ---------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableBakedMatchesRuntime, "RawBreak.Unit.Table.BakedMatchesRuntime", RB_UNIT_TEST_FLAGS)
bool FRbTableBakedMatchesRuntime::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	TestEqual(TEXT("baked package path"), RbTableMeshBuilder::GetBakedMeshPackagePath(ERbTablePreset::NineFootPro, ERbTablePart::Bed),
		FString(TEXT("/Game/Generated/Tables/NineFootPro/SM_Table_Bed")));
	TestEqual(TEXT("baked object path"), RbTableMeshBuilder::GetBakedMeshObjectPath(ERbTablePreset::SevenFootBar, ERbTablePart::CushionCloth),
		FString(TEXT("/Game/Generated/Tables/SevenFootBar/SM_Table_CushionCloth.SM_Table_CushionCloth")));

	int32 Checked = 0;
	for (const ERbTablePreset Preset : kAllPresets)
	{
		bool bRequired = false;
		for (const ERbTablePreset Baked : kBakedPresets)
		{
			bRequired = bRequired || Baked == Preset;
		}
		const FBuiltTable* Built = GetBuilt(*this, Preset);
		if (!Built)
		{
			continue;
		}
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			const ERbTablePart Part = static_cast<ERbTablePart>(PartIndex);
			const FString Name = Label(Preset, Part);
			const FString PackagePath = RbTableMeshBuilder::GetBakedMeshPackagePath(Preset, Part);
			if (!Expected(Preset, Part))
			{
				// M2-L: parts of the other base style are not baked (rb_bake_table.py deletes stale ones).
				TestFalse(Name + TEXT(" has no baked asset"), FPackageName::DoesPackageExist(PackagePath));
				continue;
			}
			if (!FPackageName::DoesPackageExist(PackagePath))
			{
				if (bRequired)
				{
					AddError(FString::Printf(TEXT("%s: baked asset %s missing (run Tools/unreal/editor/rb_bake_table.py)"), *Name, *PackagePath));
				}
				continue;
			}
			const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *RbTableMeshBuilder::GetBakedMeshObjectPath(Preset, Part));
			if (!TestNotNull(Name + TEXT(" loads"), Mesh))
			{
				continue;
			}
			++Checked;
			const FDynamicMesh3& Runtime = Built->Meshes.Get(Part);
			TestEqual(Name + TEXT(" render triangles == runtime"), Mesh->GetNumTriangles(0), Runtime.TriangleCount());
#if WITH_EDITORONLY_DATA
			const FMeshDescription* Source = Mesh->GetMeshDescription(0);
			TestTrue(Name + TEXT(" source triangles == runtime"), Source && Source->Triangles().Num() == Runtime.TriangleCount());
			if (Source)
			{
				// Geometry drift (plan pitfall 16): a bake from an older TableSpec / builder can keep every triangle count and even
				// the bounds (e.g. a changed mouth width moves only the jaws). Every baked vertex must be a current runtime vertex.
				TestEqual(Name + TEXT(" source vertices == runtime"), Source->Vertices().Num(), Runtime.VertexCount());
				TMap<FIntVector, TArray<int32>> Cells;
				const double Cell = 0.01; // cm
				auto CellOf = [Cell](const FVector3d& P) {
					return FIntVector(FMath::FloorToInt32(P.X / Cell), FMath::FloorToInt32(P.Y / Cell), FMath::FloorToInt32(P.Z / Cell));
				};
				for (const int32 Vid : Runtime.VertexIndicesItr())
				{
					Cells.FindOrAdd(CellOf(Runtime.GetVertex(Vid))).Add(Vid);
				}
				const TVertexAttributesConstRef<FVector3f> Positions = Source->GetVertexPositions();
				int32 Drifted = 0;
				double WorstDrift = 0.0;
				for (const FVertexID VertexID : Source->Vertices().GetElementIDs())
				{
					const FVector3d P(Positions[VertexID]);
					double Best = TNumericLimits<double>::Max();
					const FIntVector Key = CellOf(P);
					for (int32 Dz = -1; Dz <= 1; ++Dz)
					{
						for (int32 Dy = -1; Dy <= 1; ++Dy)
						{
							for (int32 Dx = -1; Dx <= 1; ++Dx)
							{
								if (const TArray<int32>* Bucket = Cells.Find(Key + FIntVector(Dx, Dy, Dz)))
								{
									for (const int32 Vid : *Bucket)
									{
										Best = FMath::Min(Best, FVector3d::Dist(Runtime.GetVertex(Vid), P));
									}
								}
							}
						}
					}
					Drifted += Best > kTolCm ? 1 : 0;
					WorstDrift = FMath::Max(WorstDrift, FMath::Min(Best, 1e9));
				}
				if (Drifted > 0)
				{
					AddError(FString::Printf(TEXT("%s: %d baked vertices are not runtime vertices (worst %.6g cm) - re-run rb_bake_table.py"), *Name, Drifted, WorstDrift));
				}
			}
#endif
			const FAxisAlignedBox3d RuntimeBox = Bounds(Runtime);
			const FBox BakedBox = Mesh->GetBoundingBox();
			TestTrue(Name + TEXT(" bounds == runtime"), FVector3d::Dist(BakedBox.Min, RuntimeBox.Min) < kTolCm && FVector3d::Dist(BakedBox.Max, RuntimeBox.Max) < kTolCm);
#if WITH_EDITORONLY_DATA
			const FMeshNaniteSettings& Nanite = Mesh->GetNaniteSettings();
			TestEqual(Name + TEXT(" Nanite per part policy"), Nanite.bEnabled, RbTableMeshBuilder::PartUsesNanite(Part));
			if (Nanite.bEnabled)
			{
				// Review R-02: ray tracing and collision use the fallback, which must keep every triangle.
				TestTrue(Name + TEXT(" Nanite fallback keeps 100 % of the triangles"),
					Nanite.FallbackTarget == ENaniteFallbackTarget::PercentTriangles && Nanite.FallbackPercentTriangles == 1.0f);
			}
#endif
			const UBodySetup* Body = Mesh->GetBodySetup();
			if (RbTableMeshBuilder::PartHasCollision(Part))
			{
				TestTrue(Name + TEXT(" complex-as-simple collision"), Body && Body->CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
			}
		}
	}
	int32 ExpectedBaked = 0;
	for (const ERbTablePreset Preset : kBakedPresets)
	{
		ExpectedBaked += ExpectedPartCount(Preset);
	}
	TestTrue(TEXT("baked meshes checked"), Checked >= ExpectedBaked);
	return true;
}

// --- actor: frames and transient parts ------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableFrames, "RawBreak.Unit.Table.Frames", RB_UNIT_TEST_FLAGS)
bool FRbTableFrames::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	FTestWorld TestWorld;
	const FVector Location(123.4, -567.8, 9.1);
	const FRotator Rotation(0.0, 37.0, 0.0);
	ARbTable* Table = TestWorld.World->SpawnActor<ARbTable>(Location, Rotation);
	if (!TestNotNull(TEXT("table spawned"), Table) || !TestTrue(TEXT("context built at construction"), Table->HasContext()))
	{
		return false;
	}
	const FRbTableContext& Ctx = Table->GetContext();
	const double BedCm = FRbCoords::CmPerMeter * Ctx.BedHeight();
	const FTransform Actor = Table->GetActorTransform();

	// ClothOrigin = bed centre on the cloth: BedHeight above the actor (floor), same yaw, scale 1.
	const FTransform TableToWorld = Table->GetTableToWorld();
	TestTrue(TEXT("cloth origin location"), TableToWorld.GetLocation().Equals(Location + FVector(0.0, 0.0, BedCm), 1e-9));
	TestTrue(TEXT("cloth origin rotation"), TableToWorld.GetRotation().Equals(Rotation.Quaternion(), 1e-12));
	TestTrue(TEXT("cloth origin scale 1"), TableToWorld.GetScale3D().Equals(FVector::OneVector, 1e-12));
	TestTrue(TEXT("bed centre"), Table->GetBedCenterWorld().Equals(Location + FVector(0.0, 0.0, BedCm), 1e-9));

	// Axes: core +x (foot) = actor forward, core +y (left) = -actor right, core +z = up.
	TestTrue(TEXT("core +x -> forward"), Table->CoreDirectionToWorld(rb::Vec3(1.0, 0.0, 0.0)).Equals(Table->GetActorForwardVector(), 1e-12));
	TestTrue(TEXT("core +y -> -right"), Table->CoreDirectionToWorld(rb::Vec3(0.0, 1.0, 0.0)).Equals(-Table->GetActorRightVector(), 1e-12));
	TestTrue(TEXT("core +z -> up"), Table->CoreDirectionToWorld(rb::Vec3(0.0, 0.0, 1.0)).Equals(FVector::UpVector, 1e-12));

	FRandomStream Random(20260928);
	for (int32 i = 0; i < 200; ++i)
	{
		const rb::Vec3 P(Random.FRandRange(-2.0, 2.0), Random.FRandRange(-1.0, 1.0), Random.FRandRange(-0.8, 1.2));
		const FVector W = Table->CoreToWorld(P);
		const FVector Expected = Actor.TransformPosition(FRbCoords::PositionToUE(P) + FVector(0.0, 0.0, BedCm));
		if (!W.Equals(Expected, 1e-9))
		{
			AddError(FString::Printf(TEXT("CoreToWorld mismatch at sample %d"), i));
			break;
		}
		const rb::Vec3 Back = Table->WorldToCore(W);
		if (FMath::Abs(Back.x - P.x) > 1e-12 || FMath::Abs(Back.y - P.y) > 1e-12 || FMath::Abs(Back.z - P.z) > 1e-12)
		{
			AddError(FString::Printf(TEXT("WorldToCore(CoreToWorld(p)) != p at sample %d"), i));
			break;
		}
		const rb::Vec3 D = rb::Normalized(P);
		const rb::Vec3 DBack = Table->WorldDirectionToCore(Table->CoreDirectionToWorld(D));
		if (FMath::Abs(DBack.x - D.x) > 1e-12 || FMath::Abs(DBack.y - D.y) > 1e-12 || FMath::Abs(DBack.z - D.z) > 1e-12)
		{
			AddError(FString::Printf(TEXT("direction round trip failed at sample %d"), i));
			break;
		}
		// Orientation: round trip, and the mirror commutes with the rotation in the world (q v mapped == q_W (v mapped)).
		const rb::Quat Q = rb::Normalized(rb::Quat(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0),
			Random.FRandRange(-1.0, 1.0)));
		const rb::Quat QBack = Table->WorldOrientationToCore(Table->CoreOrientationToWorld(Q));
		const double Sign = (QBack.w * Q.w + QBack.x * Q.x + QBack.y * Q.y + QBack.z * Q.z) < 0.0 ? -1.0 : 1.0;
		if (FMath::Abs(Sign * QBack.w - Q.w) > 1e-12 || FMath::Abs(Sign * QBack.x - Q.x) > 1e-12 || FMath::Abs(Sign * QBack.y - Q.y) > 1e-12 ||
			FMath::Abs(Sign * QBack.z - Q.z) > 1e-12)
		{
			AddError(FString::Printf(TEXT("orientation round trip failed at sample %d"), i));
			break;
		}
		// The world orientation maps ball-local vectors (UE axes, identity = table axes) into the world.
		const FVector Rotated = Table->CoreOrientationToWorld(Q).RotateVector(FRbCoords::DirectionToUE(D));
		if (!Rotated.Equals(Table->CoreDirectionToWorld(rb::Rotate(Q, D)), 1e-12))
		{
			AddError(FString::Printf(TEXT("world orientation does not rotate like the core at sample %d"), i));
			break;
		}
		// Azimuth of a cue direction (butt raised) in the world.
		const double Phi = Random.FRandRange(-UE_DOUBLE_PI + 1e-6, UE_DOUBLE_PI);
		const double Theta = Random.FRandRange(0.0, 1.2);
		const rb::Vec3 Cue(FMath::Cos(Theta) * FMath::Cos(Phi), FMath::Cos(Theta) * FMath::Sin(Phi), -FMath::Sin(Theta));
		const double PhiBack = Table->WorldDirectionToAzimuth(Table->CoreDirectionToWorld(Cue));
		if (FMath::Abs(FMath::UnwindRadians(PhiBack - Phi)) > 1e-12)
		{
			AddError(FString::Printf(TEXT("azimuth round trip failed at sample %d (%.15f vs %.15f)"), i, PhiBack, Phi));
			break;
		}
	}

	// The part meshes sit in the same frame: the bed's top face is at the world cloth height, and a foot-side cushion
	// nose point maps to where the core puts it.
	const UPrimitiveComponent* BedComponent = Table->GetPartComponent(ERbTablePart::Bed);
	if (TestNotNull(TEXT("bed component"), BedComponent))
	{
		const FBoxSphereBounds B = BedComponent->Bounds;
		TestNearlyEqual(TEXT("bed top at the world cloth height"), B.Origin.Z + B.BoxExtent.Z, Location.Z + BedCm, 1e-3);
		TestTrue(TEXT("bed attached to ClothOrigin"), BedComponent->GetAttachParent() == Table->GetClothOrigin());
	}

	// The frame is translation + yaw, scale 1: a scaled / tilted placement is reset (FRbCoords maps centimetres 1:1, the physics
	// has no tilted table), so CoreToWorld stays exact.
	AddExpectedMessage(TEXT("a table is placed upright and unscaled"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	const FTransform Bad(FRotator(10.0, 37.0, -5.0), Location, FVector(2.0, 2.0, 0.5));
	ARbTable* Tilted = TestWorld.World->SpawnActor<ARbTable>(ARbTable::StaticClass(), Bad);
	if (TestNotNull(TEXT("tilted + scaled table spawned"), Tilted))
	{
		const FTransform Fixed = Tilted->GetTableToWorld();
		TestTrue(TEXT("reset to scale 1"), Fixed.GetScale3D().Equals(FVector::OneVector, 1e-9));
		TestTrue(TEXT("reset to yaw only"), Fixed.GetRotation().Equals(Rotation.Quaternion(), 1e-9));
		TestTrue(TEXT("cloth origin BedHeight above the actor"), Fixed.GetLocation().Equals(Location + FVector(0.0, 0.0, BedCm), 1e-6));
		TestTrue(TEXT("core +z -> world up"), Tilted->CoreDirectionToWorld(rb::Vec3(0.0, 0.0, 1.0)).Equals(FVector::UpVector, 1e-9));
	}
	return true;
}

// Collision of the part components (pawn walking, cue sweeps, traces), for the baked static meshes AND the runtime dynamic
// meshes, in a translated + yawed table: every part with collision blocks with complex-as-simple collision (so SIMPLE
// queries - a character capsule, a cue sweep - use the triangles), and the triangle data the physics cooks from
// (IInterface_CollisionDataProvider) is the render mesh: ray casts against it hit the cloth, cushion tops, rail caps and
// the apron exactly where the physics surfaces are. (The data is checked directly: a never-ticking test world creates the
// bodies of freshly loaded meshes late, which says nothing about the table.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableCollision, "RawBreak.Unit.Table.Collision", RB_UNIT_TEST_FLAGS)
bool FRbTableCollision::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	for (const bool bBaked : {true, false})
	{
		for (const ERbTablePreset Preset : kBakedPresets)
		{
			const FString Name = RbTableMeshBuilder::GetPresetName(Preset) + (bBaked ? TEXT(" baked") : TEXT(" runtime"));
			FTestWorld TestWorld;
			ARbTable* Table = TestWorld.World->SpawnActor<ARbTable>(FVector(-210.0, 75.0, 3.0), FRotator(0.0, -23.0, 0.0));
			if (!TestNotNull(Name + TEXT(" table spawned"), Table))
			{
				continue;
			}
			Table->Preset = Preset;
			Table->bUseBakedMeshes = bBaked;
			Table->RebuildTable();
			if (!TestTrue(Name + TEXT(" context"), Table->HasContext()))
			{
				continue;
			}
			TestEqual(Name + TEXT(" part kind"), Table->IsPartBaked(ERbTablePart::Bed), bBaked);
			const FBuiltTable* Built = GetBuilt(*this, Preset);
			const rb::TableGeometry& G = Table->GetContext().Geometry;
			const rb::TableSpec& Spec = G.Spec;
			const double Cm = FRbCoords::CmPerMeter;

			// World-space collision triangles of every part with collision.
			struct FCollisionTri
			{
				FVector A, B, C;
				ERbTablePart Part;
			};
			TArray<FCollisionTri> Tris;
			for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
			{
				const ERbTablePart Part = static_cast<ERbTablePart>(PartIndex);
				UPrimitiveComponent* Component = Table->GetPartComponent(Part);
				const FString PartName = Name + TEXT(" ") + RbTypes::ToString(Part);
				if (!Expected(Preset, Part))
				{
					TestNull(PartName + TEXT(" has no component in this base style"), Component);
					continue;
				}
				if (!TestNotNull(PartName + TEXT(" component"), Component))
				{
					continue;
				}
				const bool bCollision = RbTableMeshBuilder::PartHasCollision(Part);
				TestEqual(PartName + TEXT(" query and physics collision"), Component->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics, bCollision);
				if (!bCollision)
				{
					continue;
				}
				TestEqual(PartName + TEXT(" blocks pawns"), Component->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
				UBodySetup* Body = Component->GetBodySetup();
				TestTrue(PartName + TEXT(" complex-as-simple body"), Body && Body->CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
				IInterface_CollisionDataProvider* Provider = nullptr;
				int32 RenderTriangles = 0;
				if (UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component))
				{
					Provider = Static->GetStaticMesh();
					RenderTriangles = Static->GetStaticMesh() ? Static->GetStaticMesh()->GetNumTriangles(0) : 0;
				}
				else if (UDynamicMeshComponent* Dynamic = Cast<UDynamicMeshComponent>(Component))
				{
					Provider = Dynamic;
					RenderTriangles = Dynamic->GetMesh()->TriangleCount();
					// The runtime body is cooked synchronously when the mesh is set: its triangle mesh must exist.
					TestTrue(PartName + TEXT(" cooked triangle mesh"), Body && Body->TriMeshGeometries.Num() > 0);
				}
				if (!TestTrue(PartName + TEXT(" provides complex collision"), Provider && Provider->ContainsPhysicsTriMeshData(true)))
				{
					continue;
				}
				FTriMeshCollisionData Data;
				TestTrue(PartName + TEXT(" collision triangles"), Provider->GetPhysicsTriMeshData(&Data, true));
				// The physics drops (or its cooker cleans away) degenerate slivers - the few thin ears of the finely sampled jaw
				// arcs (< 0.002 mm^2); every other render triangle is a collision triangle.
				const FDynamicMesh3* Source = Built ? &Built->Meshes.Get(Part) : nullptr;
				int32 SourceSolid = 0;
				if (Source)
				{
					for (const int32 Tid : Source->TriangleIndicesItr())
					{
						FVector3d A;
						FVector3d B;
						FVector3d C;
						Source->GetTriVertices(Tid, A, B, C);
						SourceSolid += FVector3f::CrossProduct(FVector3f(A - B), FVector3f(A - C)).SizeSquared() >= UE_SMALL_NUMBER ? 1 : 0;
					}
					TestEqual(PartName + TEXT(" render triangles == builder"), RenderTriangles, Source->TriangleCount());
				}
				int32 DataSolid = 0;
				for (const FTriIndices& T : Data.Indices)
				{
					DataSolid += FVector3f::CrossProduct(Data.Vertices[T.v0] - Data.Vertices[T.v1], Data.Vertices[T.v0] - Data.Vertices[T.v2]).SizeSquared() >= UE_SMALL_NUMBER ? 1 : 0;
				}
				TestEqual(PartName + TEXT(" collision triangles == render triangles (without slivers)"), DataSolid, SourceSolid);
				const FTransform ToWorld = Component->GetComponentTransform();
				for (const FTriIndices& T : Data.Indices)
				{
					Tris.Add({ToWorld.TransformPosition(FVector(Data.Vertices[T.v0])), ToWorld.TransformPosition(FVector(Data.Vertices[T.v1])),
						ToWorld.TransformPosition(FVector(Data.Vertices[T.v2])), Part});
				}
			}

			// Nearest ray hit (Moeller-Trumbore) over the collision triangles.
			auto RayCast = [&Tris](const FVector& Start, const FVector& End, FVector& OutPoint, ERbTablePart& OutPart) {
				const FVector D = End - Start;
				double Best = 1.0 + 1e-9;
				for (const FCollisionTri& T : Tris)
				{
					const FVector E1 = T.B - T.A;
					const FVector E2 = T.C - T.A;
					const FVector P = D.Cross(E2);
					const double Det = E1.Dot(P);
					if (FMath::Abs(Det) < 1e-12)
					{
						continue;
					}
					const FVector S = Start - T.A;
					const double U = S.Dot(P) / Det;
					const FVector Q = S.Cross(E1);
					const double V = D.Dot(Q) / Det;
					const double Tt = E2.Dot(Q) / Det;
					if (U >= -1e-9 && V >= -1e-9 && U + V <= 1.0 + 1e-9 && Tt >= 0.0 && Tt < Best)
					{
						Best = Tt;
						OutPart = T.Part;
					}
				}
				OutPoint = Start + D * Best;
				return Best <= 1.0;
			};
			struct FProbe
			{
				const TCHAR* What;
				rb::Vec3 From;
				rb::Vec3 To;
				double Expected; // expected core coordinate [m] (z, or x for the horizontal probe)
				bool bHorizontal;
				ERbTablePart Part;
			};
			const double Hl = G.HalfLength;
			const double Hw = G.HalfWidth;
			const double K = (Spec.RailTopZ - Spec.CushionNoseHeight) / Spec.CushionWidth;
			const double Mid = 0.5 * (Spec.CushionWidth + Spec.RailWidthTotal); // rail cap, between the cushion back and the outer edge
			// M2-L: the side faces below the rails are the apron skirt behind the rail outline (legs style) or the coin-op cabinet's
			// boards 2 mm behind it; the probe at z = -8 cm hits them at y = 5 cm (clear of the foot end's coin mechanism).
			RbTableMeshBuilder::FLookDevMetrics Metrics;
			FString MetricsError;
			RbTableMeshBuilder::ComputeLookDevMetrics(G, FRbTableMeshOptions(), Metrics, MetricsError);
			const bool bCabinet = Metrics.Style == ERbTableBaseStyle::Cabinet;
			const double OuterX = G.OuterBoundary.Hi.x - (bCabinet ? 0.002 : Metrics.SkirtInset);
			const rb::Vec3 Sight = G.Sights[2].Position;
			const FProbe Probes[] = {
				{TEXT("bed centre"), rb::Vec3(0.0, 0.0, 0.5), rb::Vec3(0.0, 0.0, -0.5), 0.0, false, ERbTablePart::Bed},
				{TEXT("bed near the foot spot"), rb::Vec3(0.5 * Hl, -0.3 * Hw, 0.5), rb::Vec3(0.5 * Hl, -0.3 * Hw, -0.5), 0.0, false, ERbTablePart::Bed},
				{TEXT("cushion top"), rb::Vec3(0.25 * Hl, Hw + 0.5 * Spec.CushionWidth, 0.5), rb::Vec3(0.25 * Hl, Hw + 0.5 * Spec.CushionWidth, -0.5),
					Spec.CushionNoseHeight + K * 0.5 * Spec.CushionWidth, false, ERbTablePart::CushionCloth},
				{TEXT("rail cap"), rb::Vec3(-0.3 * Hl, -Hw - Mid, 0.5), rb::Vec3(-0.3 * Hl, -Hw - Mid, -0.5), Spec.RailTopZ, false, ERbTablePart::RailCaps},
				{TEXT("end rail cap"), rb::Vec3(Hl + Mid, 0.1 * Hw, 0.5), rb::Vec3(Hl + Mid, 0.1 * Hw, -0.5), Spec.RailTopZ, false, ERbTablePart::RailCaps},
				{TEXT("apron from outside"), rb::Vec3(OuterX + 1.0, bCabinet ? 0.3 : 0.05, -0.08), rb::Vec3(0.0, bCabinet ? 0.3 : 0.05, -0.08), OuterX, true,
					bCabinet ? ERbTablePart::Cabinet : ERbTablePart::Apron},
				{TEXT("sight (flush, the cap is drilled under it)"), rb::Vec3(Sight.x + 0.001, Sight.y + 0.0015, 0.5), rb::Vec3(Sight.x + 0.001, Sight.y + 0.0015, -0.5),
					Spec.RailTopZ, false, ERbTablePart::Sights},
			};
			for (const FProbe& Probe : Probes)
			{
				FVector Point;
				ERbTablePart Part = ERbTablePart::Count;
				if (!TestTrue(Name + TEXT(" collision under the ") + Probe.What, RayCast(Table->CoreToWorld(Probe.From), Table->CoreToWorld(Probe.To), Point, Part)))
				{
					continue;
				}
				const rb::Vec3 Core = Table->WorldToCore(Point);
				TestNearlyEqual(Name + TEXT(" collision surface at the ") + Probe.What, Cm * (Probe.bHorizontal ? Core.x : Core.z), Cm * Probe.Expected, 1e-3);
				TestEqual(Name + TEXT(" collision part at the ") + Probe.What, static_cast<int32>(Part), static_cast<int32>(Probe.Part));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableTransientParts, "RawBreak.Unit.Table.TransientParts", RB_UNIT_TEST_FLAGS)
bool FRbTableTransientParts::RunTest(const FString& Parameters)
{
	using namespace RbTableTestsPrivate;
	// 1) Spawned table: one tagged, transient part component per part, attached to ClothOrigin; a preset change rebuilds.
	{
		FTestWorld TestWorld;
		ARbTable* Table = TestWorld.World->SpawnActor<ARbTable>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("table spawned"), Table))
		{
			return false;
		}
		TestEqual(TEXT("tagged part components"), CountPartComponents(Table), ExpectedPartCount(Table->Preset));
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			const ERbTablePart Part = static_cast<ERbTablePart>(PartIndex);
			const UPrimitiveComponent* Component = Table->GetPartComponent(Part);
			if (!Expected(Table->Preset, Part))
			{
				continue;
			}
			if (!TestNotNull(FString(TEXT("part component ")) + RbTypes::ToString(Part), Component))
			{
				continue;
			}
			TestTrue(FString(TEXT("transient ")) + RbTypes::ToString(Part), Component->HasAnyFlags(RF_Transient));
			TestTrue(FString(TEXT("attached ")) + RbTypes::ToString(Part), Component->GetAttachParent() == Table->GetClothOrigin());
			TestFalse(FString(TEXT("not an instance component ")) + RbTypes::ToString(Part), Table->GetInstanceComponents().Contains(Component));
			const bool bCollision = RbTableMeshBuilder::PartHasCollision(Part);
			TestEqual(FString(TEXT("collision ")) + RbTypes::ToString(Part), Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision, bCollision);
		}
		const double NineFootX = Table->GetPartComponent(ERbTablePart::RailCaps)->Bounds.BoxExtent.X;
		Table->Preset = ERbTablePreset::SevenFootBar;
		Table->RebuildTable();
		TestEqual(TEXT("tagged part components after a rebuild"), CountPartComponents(Table), ExpectedPartCount(ERbTablePreset::SevenFootBar));
		TestEqual(TEXT("context follows the preset"), static_cast<int32>(Table->GetContext().Spec.Preset), static_cast<int32>(rb::TablePreset::SevenFootBar));
		const double SevenFootX = Table->GetPartComponent(ERbTablePart::RailCaps)->Bounds.BoxExtent.X;
		TestNearlyEqual(TEXT("rails follow the preset"), NineFootX - SevenFootX,
			0.5 * FRbCoords::CmPerMeter * (rb::kTableNineFootPro.Length - rb::kTableSevenFootBar.Length) +
				FRbCoords::CmPerMeter * (rb::kTableNineFootPro.RailWidthTotal - rb::kTableSevenFootBar.RailWidthTotal),
			1e-3);
	}

	// 2) BeginPlay rebuilds whatever construction did not build (a -game / PIE load runs no construction script): a preset
	// changed behind the actor's back is picked up when play begins.
	{
		FTestWorld TestWorld;
		ARbTable* Table = TestWorld.World->SpawnActor<ARbTable>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (TestNotNull(TEXT("table spawned"), Table))
		{
			Table->Preset = ERbTablePreset::SevenFootTrue; // no construction, no RebuildTable
			TestEqual(TEXT("context still the old preset"), static_cast<int32>(Table->GetContext().Spec.Preset), static_cast<int32>(rb::TablePreset::NineFootPro));
			TestWorld.World->GetWorldSettings()->NotifyBeginPlay();
			TestTrue(TEXT("BeginPlay dispatched"), Table->HasActorBegunPlay());
			TestEqual(TEXT("context rebuilt at BeginPlay"), static_cast<int32>(Table->GetContext().Spec.Preset), static_cast<int32>(rb::TablePreset::SevenFootTrue));
			TestEqual(TEXT("parts rebuilt at BeginPlay"), CountPartComponents(Table), ExpectedPartCount(ERbTablePreset::SevenFootTrue));
		}
	}

#if WITH_EDITOR
	// 3) Reload: save a level with a table, check the file, load it back under another name. The file must not contain the
	// part components, and whatever part components the loaded actor has were built after the load (never loaded).
	const FString Stamp = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString SavedName = FString::Printf(TEXT("/Temp/RbUE1/L_TableSave_%s"), *Stamp);
	const FString LoadName = FString::Printf(TEXT("/Temp/RbUE1/L_TableLoad_%s"), *Stamp);
	const FString SavedFile = FPackageName::LongPackageNameToFilename(SavedName, FPackageName::GetMapPackageExtension());
	const FString LoadFile = FPackageName::LongPackageNameToFilename(LoadName, FPackageName::GetMapPackageExtension());
	{
		UPackage* Package = CreatePackage(*SavedName);
		UWorld* World = UWorld::CreateWorld(EWorldType::Inactive, false, FName(*FPackageName::GetShortName(SavedName)), Package);
		World->SetFlags(RF_Public | RF_Standalone);
		ARbTable* Table = World->SpawnActor<ARbTable>(FVector(10.0, 20.0, 0.0), FRotator(0.0, 90.0, 0.0));
		TestTrue(TEXT("parts exist before saving"), Table && CountPartComponents(Table) == ExpectedPartCount(Table->Preset));
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, World, *SavedFile, SaveArgs);
		TestTrue(TEXT("level saved"), bSaved);
		World->DestroyWorld(false);
		World->ClearFlags(RF_Public | RF_Standalone);
		Package->ClearFlags(RF_Standalone);
		if (!bSaved)
		{
			return false;
		}
	}
	// The part components' names would be in the package's name table if any had been saved.
	auto FileContains = [](const TArray<uint8>& Bytes, const FAnsiStringView Needle) {
		for (int32 i = 0; i + Needle.Len() <= Bytes.Num(); ++i)
		{
			if (FMemory::Memcmp(Bytes.GetData() + i, Needle.GetData(), Needle.Len()) == 0)
			{
				return true;
			}
		}
		return false;
	};
	TArray<uint8> Bytes;
	if (TestTrue(TEXT("saved file readable"), FFileHelper::LoadFileToArray(Bytes, *SavedFile)))
	{
		TestFalse(TEXT("saved level stores no part component"), FileContains(Bytes, "RbTablePart_"));
		TestTrue(TEXT("saved level stores the table actor"), FileContains(Bytes, "RbTable"));
	}
	TestTrue(TEXT("copy for reload"), IFileManager::Get().Copy(*LoadFile, *SavedFile) == COPY_OK);
	{
		UPackage* Loaded = LoadPackage(nullptr, *LoadName, LOAD_None);
		UWorld* World = Loaded ? UWorld::FindWorldInPackage(Loaded) : nullptr;
		if (TestNotNull(TEXT("reloaded world"), World))
		{
			ARbTable* Table = nullptr;
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				Table = Table ? Table : Cast<ARbTable>(Actor);
			}
			if (TestNotNull(TEXT("reloaded table"), Table))
			{
				TestTrue(TEXT("table actor came from the file"), Table->HasAnyFlags(RF_WasLoaded));
				TInlineComponentArray<UPrimitiveComponent*> Components(Table);
				for (const UPrimitiveComponent* Component : Components)
				{
					if (Component && Component->ComponentHasTag(ARbTable::PartComponentTag))
					{
						TestFalse(TEXT("part component was not loaded from the file"), Component->HasAnyFlags(RF_WasLoaded));
						TestTrue(TEXT("part component is transient"), Component->HasAnyFlags(RF_Transient));
					}
				}
				// The editor initialises loaded worlds and runs construction (-game / PIE do not; case 2 covers BeginPlay).
				if (!Table->HasContext())
				{
					Table->RebuildTable();
				}
				TestEqual(TEXT("reloaded table has its parts"), CountPartComponents(Table), ExpectedPartCount(Table->Preset));
				TestTrue(TEXT("reloaded placement kept"),
					Table->GetBedCenterWorld().Equals(FVector(10.0, 20.0, FRbCoords::CmPerMeter * Table->GetContext().BedHeight()), 1e-9));
			}
			if (World->bIsWorldInitialized)
			{
				World->DestroyWorld(false);
			}
			World->ClearFlags(RF_Public | RF_Standalone);
		}
		if (Loaded)
		{
			Loaded->ClearFlags(RF_Standalone);
			ResetLoaders(Loaded); // release the file handle
		}
	}
	IFileManager::Get().Delete(*SavedFile, false, false, true);
	IFileManager::Get().Delete(*LoadFile, false, false, true);
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(SavedFile), false, false);
#endif // WITH_EDITOR
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
