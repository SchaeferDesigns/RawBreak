// M2-L table look-dev (Docs/ue-architecture.md 18.7): RawBreak.Unit.Table.Look.*
//   CapEdgeProfile     the outer top edge of the rail caps is a real quarter round of radius >= 3 mm with >= 8 segments (every
//                      vertex of the rounded band lies on the circle to 0.01 mm), the cap top stays RailTopZ
//   NoseRoll           the cushion nose roll's worst tessellation sagitta < 0.05 mm; the roll starts exactly on the nose line at
//                      z = h, never reaches in front of it, bulges at most NoseRollBulge x width above the physics' top plane
//   RubberStrip        the rubber lip lies behind the nose outline (outside the playing polygon), below the nose line, inside the
//                      rail outline, on every cushion
//   CastingClearance   coin-op castings: nothing inside the cushion-back rectangle (the jaws / facings stay free), >= 2 mm to every
//                      capture cylinder r_p in the rail height; the gully throats hang inside r_p below the slate
//   PartsPerStyle      the look-dev parts exist per base style (legs: no coin-op parts), every default part material path is
//                      generated (rb_make_materials.py) and allowed on Nanite
//   Deterministic      BuildAll twice gives identical meshes (idempotent regeneration: the bake of every run is the same asset)
//   TableViews         ARbLookDevCamera::ComputeTableViewPose: every view above the cloth and outside the table solids where it
//                      must be, looking at its target; the overhead view frames the whole table at the Eyes preset's 50 deg
// Tolerances: 0.01 mm = 1e-3 cm (the builder writes the profiles exactly; the welding grid is 1 um).

#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Dev/RbLookDevCamera.h"
#include "Table/RbTable.h"
#include "Table/RbTableMeshBuilder.h"
#include "Tests/RbTestFlags.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

#include "rb/Geometry/TableGeometry.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbTableLookTestsPrivate
{
	using namespace UE::Geometry;

	constexpr double kTolCm = 1e-3; // 0.01 mm

	const ERbTablePreset kLookPresets[] = {ERbTablePreset::NineFootPro, ERbTablePreset::SevenFootBar, ERbTablePreset::EightFootHome,
		ERbTablePreset::SevenFootTrue};

	struct FLookTable
	{
		rb::TableGeometry Geometry;
		FRbTableMeshOptions Options;
		FRbTableMeshSet Meshes;
		RbTableMeshBuilder::FLookDevMetrics Metrics;
	};

	const FLookTable* GetLook(FAutomationTestBase& Test, ERbTablePreset Preset)
	{
		static TMap<ERbTablePreset, TSharedPtr<FLookTable>> Cache;
		if (const TSharedPtr<FLookTable>* Found = Cache.Find(Preset))
		{
			return Found->Get();
		}
		TSharedPtr<FLookTable> Built = MakeShared<FLookTable>();
		const rb::ErrorCode Code = rb::BuildTableGeometry(rb::GetTableSpec(RbTypes::ToCore(Preset)), Built->Geometry);
		FString Error;
		if (!rb::Succeeded(Code) || !RbTableMeshBuilder::BuildAll(Built->Geometry, Built->Options, Built->Meshes, Error) ||
			!RbTableMeshBuilder::ComputeLookDevMetrics(Built->Geometry, Built->Options, Built->Metrics, Error))
		{
			Test.AddError(FString::Printf(TEXT("%s: build failed: %s"), *RbTableMeshBuilder::GetPresetName(Preset), *Error));
			return nullptr;
		}
		Cache.Add(Preset, Built);
		return Built.Get();
	}

	FString Name(ERbTablePreset Preset) { return RbTableMeshBuilder::GetPresetName(Preset); }

	// Signed plan distance of a point (UE cm, table-local) to the rounded-rectangle rail outline (positive outside).
	double OutlineDistanceCm(const FVector2d& P, double HalfXCm, double HalfYCm, double RadiusCm)
	{
		const FVector2d Q(FMath::Abs(P.X) - (HalfXCm - RadiusCm), FMath::Abs(P.Y) - (HalfYCm - RadiusCm));
		const FVector2d Outside(FMath::Max(Q.X, 0.0), FMath::Max(Q.Y, 0.0));
		return Outside.Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.0) - RadiusCm;
	}

	bool PointInPolygon(const TArray<FVector2d>& Poly, const FVector2d& P)
	{
		bool bInside = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const FVector2d& A = Poly[i];
			const FVector2d& B = Poly[j];
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	// The playing polygon at z = h in UE cm (BuildNoseOutline: nose lines, jaw arcs and facing ends around the bed).
	TArray<FVector2d> NoseOutlineCm(const rb::TableGeometry& G)
	{
		TArray<rb::Vec2> Outline;
		Outline.SetNum(rb::kMaxJaws * 13 + 8);
		const int32 Count = rb::BuildNoseOutline(G, 12, Outline.GetData(), Outline.Num());
		TArray<FVector2d> Out;
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector3d P = FRbCoords::PositionToUE(rb::Vec3(Outline[i].x, Outline[i].y, 0.0));
			Out.Add(FVector2d(P.X, P.Y));
		}
		return Out;
	}

	// Transient game world (actor tests).
	struct FLookWorld
	{
		UWorld* World = nullptr;
		FLookWorld()
		{
			const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RbTableLookWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FLookWorld()
		{
			World->EndPlay(EEndPlayReason::LevelTransition);
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			if (GEngine->GetWorldContextFromWorld(World))
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableLookCapEdge, "RawBreak.Unit.Table.Look.CapEdgeProfile", RB_UNIT_TEST_FLAGS)
bool FRbTableLookCapEdge::RunTest(const FString& Parameters)
{
	using namespace RbTableLookTestsPrivate;
	for (const ERbTablePreset Preset : kLookPresets)
	{
		const FLookTable* T = GetLook(*this, Preset);
		if (!T)
		{
			continue;
		}
		const rb::TableSpec& Spec = T->Geometry.Spec;
		const RbTableMeshBuilder::FLookDevMetrics& M = T->Metrics;
		TestTrue(Name(Preset) + TEXT(" cap edge radius >= 3 mm"), M.CapEdgeRadius >= 0.003 - 1e-12);
		TestTrue(Name(Preset) + TEXT(" cap edge >= 8 segments"), M.CapEdgeSegments >= 8);

		// Every rail-cap vertex within the edge band (inset <= r from the outline, z >= RailTopZ - r) lies on the quarter circle
		// centred r inside the outline and r below the cap top; the band has >= segments + 1 distinct profile samples.
		const double R = FRbCoords::CmPerMeter * M.CapEdgeRadius;
		const double Top = FRbCoords::CmPerMeter * Spec.RailTopZ;
		const double HalfX = FRbCoords::CmPerMeter * (T->Geometry.HalfLength + Spec.RailWidthTotal);
		const double HalfY = FRbCoords::CmPerMeter * (T->Geometry.HalfWidth + Spec.RailWidthTotal);
		const double Corner = FRbCoords::CmPerMeter * M.OuterCornerRadius;
		const FDynamicMesh3& Caps = T->Meshes.Get(ERbTablePart::RailCaps);
		int32 OnBand = 0;
		int32 OffCircle = 0;
		double MaxZ = -TNumericLimits<double>::Max();
		TSet<int64> Samples;
		for (const int32 Vid : Caps.VertexIndicesItr())
		{
			const FVector3d V = Caps.GetVertex(Vid);
			MaxZ = FMath::Max(MaxZ, V.Z);
			const double Inset = -OutlineDistanceCm(FVector2d(V.X, V.Y), HalfX, HalfY, Corner);
			if (Inset > R + kTolCm || V.Z < Top - R - kTolCm || Inset < -kTolCm)
			{
				continue;
			}
			++OnBand;
			const double Err = FMath::Abs(FVector2d(Inset - R, V.Z - (Top - R)).Size() - R);
			if (Err > kTolCm)
			{
				++OffCircle;
				if (OffCircle <= 3)
				{
					AddError(FString::Printf(TEXT("%s: cap edge vertex %s is %.4f cm off the quarter round (inset %.4f cm)"), *Name(Preset),
						*FVector(V).ToString(), Err, Inset));
				}
			}
			Samples.Add(FMath::RoundToInt64(V.Z * 1e4));
		}
		TestTrue(Name(Preset) + TEXT(" cap edge band has vertices"), OnBand > 0);
		TestEqual(Name(Preset) + TEXT(" cap edge vertices off the quarter round"), OffCircle, 0);
		TestTrue(Name(Preset) + FString::Printf(TEXT(" cap edge profile samples (%d) >= segments + 1"), Samples.Num()), Samples.Num() >= M.CapEdgeSegments + 1);
		TestNearlyEqual(Name(Preset) + TEXT(" cap top = RailTopZ"), MaxZ, Top, 1e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableLookNoseRoll, "RawBreak.Unit.Table.Look.NoseRoll", RB_UNIT_TEST_FLAGS)
bool FRbTableLookNoseRoll::RunTest(const FString& Parameters)
{
	using namespace RbTableLookTestsPrivate;
	for (const ERbTablePreset Preset : kLookPresets)
	{
		const FLookTable* T = GetLook(*this, Preset);
		if (!T)
		{
			continue;
		}
		const rb::TableSpec& Spec = T->Geometry.Spec;
		const RbTableMeshBuilder::FLookDevMetrics& M = T->Metrics;
		TestTrue(Name(Preset) + TEXT(" nose roll present"), M.NoseRollWidth > 0.0);
		TestTrue(Name(Preset) + FString::Printf(TEXT(" nose-profile sagitta %.5f mm < 0.05 mm"), 1000.0 * M.NoseRollMaxSagitta), M.NoseRollMaxSagitta < 5e-5);

		// Cushion vertices: none in front of the nose outline above the cloth band (the playing polygon is free up to the nose), and
		// on the straight noses the roll stays between the physics' top plane and plane + bulge (the real cushion top is convex).
		const TArray<FVector2d> Playing = NoseOutlineCm(T->Geometry);
		const FDynamicMesh3& Cushions = T->Meshes.Get(ERbTablePart::CushionCloth);
		const double H = FRbCoords::CmPerMeter * Spec.CushionNoseHeight;
		const double K = (Spec.RailTopZ - Spec.CushionNoseHeight) / Spec.CushionWidth;
		const double Width = FRbCoords::CmPerMeter * M.NoseRollWidth;
		const double Bulge = T->Options.NoseRollBulge * Width;
		int32 InFront = 0;
		int32 AbovePlane = 0;
		int32 BelowPlane = 0;
		int32 RollSamples = 0;
		// Straight long noses: the playing polygon's edges at |Y| = W / 2 (UE cm), as x ranges.
		const double HwCm = FRbCoords::CmPerMeter * T->Geometry.HalfWidth;
		TArray<FVector2d> Straight;
		for (int32 i = 0; i < Playing.Num(); ++i)
		{
			const FVector2d A = Playing[i];
			const FVector2d B = Playing[(i + 1) % Playing.Num()];
			if (FMath::Abs(FMath::Abs(A.Y) - HwCm) < 1e-6 && FMath::Abs(FMath::Abs(B.Y) - HwCm) < 1e-6 && FMath::Abs(A.X - B.X) > 1.0)
			{
				Straight.Add(FVector2d(FMath::Min(A.X, B.X), FMath::Max(A.X, B.X)));
			}
		}
		TestEqual(Name(Preset) + TEXT(" straight long noses"), Straight.Num(), 4);
		const double TanBackdraft = FMath::Tan(Spec.Backdraft);
		for (const int32 Vid : Cushions.VertexIndicesItr())
		{
			const FVector3d V = Cushions.GetVertex(Vid);
			// Above h the undercut facings lean over the pocket mouth by (z - h) tan(backdraft) (physics geometry, M1).
			const double Allowance = kTolCm + FMath::Max(0.0, V.Z - H) * TanBackdraft;
			if (V.Z > 0.2 && PointInPolygon(Playing, FVector2d(V.X, V.Y)))
			{
				// Allowed only on the nose line itself (the outline's own samples) within the weld tolerance.
				double Best = TNumericLimits<double>::Max();
				for (int32 i = 0; i < Playing.Num(); ++i)
				{
					const FVector2d A = Playing[i];
					const FVector2d B = Playing[(i + 1) % Playing.Num()];
					const FVector2d AB = B - A;
					const double Tp = FMath::Clamp((FVector2d(V.X, V.Y) - A).Dot(AB) / FMath::Max(AB.SquaredLength(), 1e-30), 0.0, 1.0);
					Best = FMath::Min(Best, (A + AB * Tp - FVector2d(V.X, V.Y)).Size());
				}
				if (Best > Allowance && ++InFront <= 3)
				{
					AddError(FString::Printf(TEXT("%s: cushion vertex %s is %.4f cm in front of the nose outline"), *Name(Preset), *FVector(V).ToString(), Best));
				}
			}
			// Straight part of the long noses (between the jaws' tangent points, the roll's taper stations included): behind-the-nose
			// distance u, the roll band u < width.
			bool bOnStraight = false;
			for (const FVector2d& Range : Straight)
			{
				bOnStraight = bOnStraight || (V.X > Range.X + kTolCm && V.X < Range.Y - kTolCm);
			}
			if (bOnStraight && V.Z > H - kTolCm)
			{
				const double U = FMath::Abs(V.Y) - HwCm;
				if (U > kTolCm && U < Width - kTolCm)
				{
					++RollSamples;
					const double PlaneZ = H + K * U;
					AbovePlane += V.Z > PlaneZ + Bulge + kTolCm ? 1 : 0;
					BelowPlane += V.Z < PlaneZ - kTolCm ? 1 : 0;
				}
			}
		}
		TestEqual(Name(Preset) + TEXT(" cushion vertices in front of the nose outline"), InFront, 0);
		TestTrue(Name(Preset) + TEXT(" roll samples on the long noses"), RollSamples > 0);
		TestEqual(Name(Preset) + TEXT(" roll vertices above plane + bulge"), AbovePlane, 0);
		TestEqual(Name(Preset) + TEXT(" roll vertices below the physics top plane"), BelowPlane, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableLookRubberStrip, "RawBreak.Unit.Table.Look.RubberStrip", RB_UNIT_TEST_FLAGS)
bool FRbTableLookRubberStrip::RunTest(const FString& Parameters)
{
	using namespace RbTableLookTestsPrivate;
	for (const ERbTablePreset Preset : kLookPresets)
	{
		const FLookTable* T = GetLook(*this, Preset);
		if (!T)
		{
			continue;
		}
		const rb::TableSpec& Spec = T->Geometry.Spec;
		const TArray<FVector2d> Playing = NoseOutlineCm(T->Geometry);
		const FDynamicMesh3& Strip = T->Meshes.Get(ERbTablePart::RubberStrip);
		const double H = FRbCoords::CmPerMeter * Spec.CushionNoseHeight;
		const double HalfX = FRbCoords::CmPerMeter * (T->Geometry.HalfLength + Spec.RailWidthTotal);
		const double HalfY = FRbCoords::CmPerMeter * (T->Geometry.HalfWidth + Spec.RailWidthTotal);
		int32 InPlaying = 0;
		int32 Above = 0;
		int32 Outside = 0;
		for (const int32 Vid : Strip.VertexIndicesItr())
		{
			const FVector3d V = Strip.GetVertex(Vid);
			InPlaying += PointInPolygon(Playing, FVector2d(V.X, V.Y)) ? 1 : 0;
			Above += (V.Z > FMath::Min(H, T->Options.RubberStripHeightCm) + kTolCm || V.Z < -kTolCm) ? 1 : 0;
			Outside += (FMath::Abs(V.X) > HalfX || FMath::Abs(V.Y) > HalfY) ? 1 : 0;
		}
		TestTrue(Name(Preset) + TEXT(" rubber strip has vertices"), Strip.VertexCount() > 0);
		TestEqual(Name(Preset) + TEXT(" rubber vertices inside the playing polygon (in front of the nose line)"), InPlaying, 0);
		TestEqual(Name(Preset) + TEXT(" rubber vertices above the strip height / nose line or below the cloth"), Above, 0);
		TestEqual(Name(Preset) + TEXT(" rubber vertices outside the rail outline"), Outside, 0);
		// One strip per cushion (6 closed hexahedra of 8 vertices).
		TestEqual(Name(Preset) + TEXT(" rubber strips"), Strip.VertexCount(), 6 * 8);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableLookCastingClearance, "RawBreak.Unit.Table.Look.CastingClearance", RB_UNIT_TEST_FLAGS)
bool FRbTableLookCastingClearance::RunTest(const FString& Parameters)
{
	using namespace RbTableLookTestsPrivate;
	int32 CabinetTables = 0;
	for (const ERbTablePreset Preset : kLookPresets)
	{
		const FLookTable* T = GetLook(*this, Preset);
		if (!T || T->Metrics.Style != ERbTableBaseStyle::Cabinet)
		{
			continue;
		}
		++CabinetTables;
		const rb::TableSpec& Spec = T->Geometry.Spec;
		const rb::TableGeometry& G = T->Geometry;
		const double HlC = FRbCoords::CmPerMeter * (G.HalfLength + Spec.CushionWidth);
		const double HwC = FRbCoords::CmPerMeter * (G.HalfWidth + Spec.CushionWidth);
		const double SlateBottom = -FRbCoords::CmPerMeter * Spec.SlateThickness;
		const FDynamicMesh3& Castings = T->Meshes.Get(ERbTablePart::Castings);
		int32 InRect = 0;
		int32 TooClose = 0;
		int32 RailVertices = 0;
		double MinClearanceCm = TNumericLimits<double>::Max();
		for (const int32 Vid : Castings.VertexIndicesItr())
		{
			const FVector3d V = Castings.GetVertex(Vid);
			if (V.Z < SlateBottom - kTolCm)
			{
				continue; // the trap / tray / return boxes in the cabinet
			}
			++RailVertices;
			InRect += (FMath::Abs(V.X) < HlC - kTolCm && FMath::Abs(V.Y) < HwC - kTolCm) ? 1 : 0;
			for (int32 p = 0; p < rb::kPocketCount; ++p)
			{
				const FVector3d C = FRbCoords::PositionToUE(rb::Vec3(G.Pockets[p].CaptureCenter.x, G.Pockets[p].CaptureCenter.y, 0.0));
				const double Clearance = FVector2d(V.X - C.X, V.Y - C.Y).Size() - FRbCoords::CmPerMeter * G.Pockets[p].CaptureRadius;
				MinClearanceCm = FMath::Min(MinClearanceCm, Clearance);
				TooClose += Clearance < 0.2 - kTolCm ? 1 : 0;
			}
		}
		TestTrue(Name(Preset) + TEXT(" castings in the rail height"), RailVertices > 0);
		TestEqual(Name(Preset) + TEXT(" casting vertices inside the cushion-back rectangle (jaws / facings)"), InRect, 0);
		TestEqual(Name(Preset) + FString::Printf(TEXT(" casting vertices < 2 mm from a capture cylinder (min %.3f mm)"), 10.0 * MinClearanceCm), TooClose, 0);

		// Gully throats: inside r_p, below the slate.
		const FDynamicMesh3& Throats = T->Meshes.Get(ERbTablePart::PocketBuckets);
		int32 Bad = 0;
		for (const int32 Vid : Throats.VertexIndicesItr())
		{
			const FVector3d V = Throats.GetVertex(Vid);
			bool bInside = false;
			for (int32 p = 0; p < rb::kPocketCount; ++p)
			{
				const FVector3d C = FRbCoords::PositionToUE(rb::Vec3(G.Pockets[p].CaptureCenter.x, G.Pockets[p].CaptureCenter.y, 0.0));
				bInside = bInside || FVector2d(V.X - C.X, V.Y - C.Y).Size() <= FRbCoords::CmPerMeter * G.Pockets[p].CaptureRadius + kTolCm;
			}
			Bad += (!bInside || V.Z > SlateBottom + kTolCm) ? 1 : 0;
		}
		TestEqual(Name(Preset) + TEXT(" gully-throat vertices outside r_p or above the slate bottom"), Bad, 0);
	}
	TestTrue(TEXT("coin-op tables checked"), CabinetTables >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableLookPartsPerStyle, "RawBreak.Unit.Table.Look.PartsPerStyle", RB_UNIT_TEST_FLAGS)
bool FRbTableLookPartsPerStyle::RunTest(const FString& Parameters)
{
	using namespace RbTableLookTestsPrivate;
	for (const ERbTablePreset Preset : kLookPresets)
	{
		const FLookTable* T = GetLook(*this, Preset);
		if (!T)
		{
			continue;
		}
		for (int32 Index = 0; Index < static_cast<int32>(ERbTablePart::Count); ++Index)
		{
			const ERbTablePart Part = static_cast<ERbTablePart>(Index);
			const bool bExpected = RbTableMeshBuilder::PartExpected(T->Metrics.Style, Part);
			const int32 Triangles = T->Meshes.Get(Part).TriangleCount();
			TestEqual(Name(Preset) + TEXT("/") + RbTypes::ToString(Part) + TEXT(" built exactly when expected"), Triangles > 0, bExpected);
		}
	}
	// The default material of every part of the two committed presets is generated, Substrate, and allowed on Nanite.
	for (const ERbTablePreset Preset : {ERbTablePreset::NineFootPro, ERbTablePreset::SevenFootBar})
	{
		const ERbTableBaseStyle Style = RbTableMeshBuilder::ResolveBaseStyle(rb::GetTableSpec(RbTypes::ToCore(Preset)), FRbTableMeshOptions());
		for (int32 Index = 0; Index < static_cast<int32>(ERbTablePart::Count); ++Index)
		{
			const ERbTablePart Part = static_cast<ERbTablePart>(Index);
			if (!RbTableMeshBuilder::PartExpected(Style, Part))
			{
				continue;
			}
			const FString Path = RbTableMeshBuilder::GetDefaultMaterialPath(Preset, Part);
			const FString Label = Name(Preset) + TEXT("/") + RbTypes::ToString(Part) + TEXT(" ") + Path;
			if (!TestFalse(Label + TEXT(" has a default material"), Path.IsEmpty()))
			{
				continue;
			}
			const FSoftObjectPath Soft(Path);
			if (!TestTrue(Label + TEXT(" is generated (rb_make_materials.py)"), FPackageName::DoesPackageExist(Soft.GetLongPackageName())))
			{
				continue;
			}
			const UMaterialInterface* Material = Cast<UMaterialInterface>(Soft.TryLoad());
			if (!TestNotNull(Label + TEXT(" loads"), Material))
			{
				continue;
			}
			const UMaterial* Base = Material->GetMaterial();
			TestTrue(Label + TEXT(" is Substrate"), Base && Base->HasSubstrateFrontMaterialConnected());
			TestTrue(Label + TEXT(" used with Nanite"), Base && Base->GetUsageByFlag(MATUSAGE_Nanite));
		}
	}
	// The dive-bar instances of RbAssetPaths (venue-dive-bar 6.4) exist.
	for (const TCHAR* Path : {RbAssetPaths::MatBallDiveBar, RbAssetPaths::MatClothBarGreen, RbAssetPaths::MatRailBlackLaminate})
	{
		TestTrue(FString(Path) + TEXT(" generated"), FPackageName::DoesPackageExist(Path));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableLookDeterministic, "RawBreak.Unit.Table.Look.Deterministic", RB_UNIT_TEST_FLAGS)
bool FRbTableLookDeterministic::RunTest(const FString& Parameters)
{
	using namespace RbTableLookTestsPrivate;
	for (const ERbTablePreset Preset : {ERbTablePreset::NineFootPro, ERbTablePreset::SevenFootBar})
	{
		rb::TableGeometry G;
		if (!TestTrue(TEXT("geometry"), rb::Succeeded(rb::BuildTableGeometry(rb::GetTableSpec(RbTypes::ToCore(Preset)), G))))
		{
			continue;
		}
		FRbTableMeshSet A;
		FRbTableMeshSet B;
		FString Error;
		if (!TestTrue(TEXT("build A"), RbTableMeshBuilder::BuildAll(G, FRbTableMeshOptions(), A, Error)) ||
			!TestTrue(TEXT("build B"), RbTableMeshBuilder::BuildAll(G, FRbTableMeshOptions(), B, Error)))
		{
			continue;
		}
		for (int32 Index = 0; Index < static_cast<int32>(ERbTablePart::Count); ++Index)
		{
			const FDynamicMesh3& MA = A.Parts[Index];
			const FDynamicMesh3& MB = B.Parts[Index];
			const FString Label = Name(Preset) + TEXT("/") + RbTypes::ToString(static_cast<ERbTablePart>(Index));
			if (!TestEqual(Label + TEXT(" vertex count"), MA.VertexCount(), MB.VertexCount()) ||
				!TestEqual(Label + TEXT(" triangle count"), MA.TriangleCount(), MB.TriangleCount()))
			{
				continue;
			}
			int32 Different = 0;
			for (const int32 Vid : MA.VertexIndicesItr())
			{
				Different += MA.GetVertex(Vid) == MB.GetVertex(Vid) ? 0 : 1;
			}
			for (const int32 Tid : MA.TriangleIndicesItr())
			{
				Different += MA.GetTriangle(Tid) == MB.GetTriangle(Tid) ? 0 : 1;
			}
			TestEqual(Label + TEXT(" bitwise identical"), Different, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbTableLookViews, "RawBreak.Unit.Table.Look.TableViews", RB_UNIT_TEST_FLAGS)
bool FRbTableLookViews::RunTest(const FString& Parameters)
{
	using namespace RbTableLookTestsPrivate;
	for (const ERbTablePreset Preset : {ERbTablePreset::NineFootPro, ERbTablePreset::SevenFootBar})
	{
		FLookWorld W;
		// Translated and yawed: the views follow the table frame.
		ARbTable* Table = W.World->SpawnActor<ARbTable>(FVector(120.0, -40.0, 0.0), FRotator(0.0, 30.0, 0.0));
		if (!TestNotNull(TEXT("table"), Table))
		{
			continue;
		}
		Table->Preset = Preset;
		Table->bUseBakedMeshes = false;
		Table->RebuildTable();
		if (!TestTrue(TEXT("context"), Table->HasContext()))
		{
			continue;
		}
		const rb::TableSpec& Spec = Table->GetContext().Spec;
		const double Hl = 0.5 * Spec.Length;
		const double Hw = 0.5 * Spec.Width;
		const double Rw = Spec.RailWidthTotal;
		const UEnum* Views = StaticEnum<ERbTableLookDevView>();
		for (int32 V = 0; V < Views->NumEnums() - 1; ++V)
		{
			const ERbTableLookDevView View = static_cast<ERbTableLookDevView>(Views->GetValueByIndex(V));
			const FRbTableViewPose Pose = ARbLookDevCamera::ComputeTableViewPose(*Table, View);
			const FString Label = Name(Preset) + TEXT(" ") + Views->GetNameStringByIndex(V);
			const rb::Vec3 Eye = Table->WorldToCore(Pose.Eye);
			// The camera looks at its focus point (forward axis) and never sits inside the table (below the cloth over the bed or
			// inside the rails' height over the rails).
			const FVector Forward = Pose.Rotation.Vector();
			TestTrue(Label + TEXT(" looks at its focus point"), Forward.Dot((Pose.FocusPoint - Pose.Eye).GetSafeNormal()) > 0.9999);
			const bool bOverTable = FMath::Abs(Eye.x) < Hl + Rw && FMath::Abs(Eye.y) < Hw + Rw;
			TestTrue(Label + TEXT(" eye above the cloth"), Eye.z > 0.005);
			if (bOverTable && (FMath::Abs(Eye.x) > Hl || FMath::Abs(Eye.y) > Hw))
			{
				TestTrue(Label + TEXT(" eye above the rails"), Eye.z > Spec.RailTopZ);
			}
			switch (View)
			{
			case ERbTableLookDevView::Overhead:
			{
				// The outer corners project inside a 50 deg (vertical) x 16:9 frame.
				const double TanV = FMath::Tan(FMath::DegreesToRadians(25.0));
				TestTrue(Label + TEXT(" frames the table width"), (Hw + Rw) / Eye.z < TanV);
				TestTrue(Label + TEXT(" frames the table length"), (Hl + Rw) / Eye.z < TanV * 16.0 / 9.0);
				TestNearlyEqual(Label + TEXT(" looks straight down"), Forward.Z, -1.0, 1e-6);
				break;
			}
			case ERbTableLookDevView::Standing:
				TestNearlyEqual(Label + TEXT(" standing eye 1.65 m above the floor"), Eye.z + Spec.BedHeight, 1.65, 1e-9);
				TestTrue(Label + TEXT(" behind the head rail"), Eye.x < -(Hl + Rw));
				break;
			case ERbTableLookDevView::CushionGrazing:
				TestNearlyEqual(Label + TEXT(" 3 cm above the cloth"), Eye.z, 0.03, 1e-9);
				break;
			case ERbTableLookDevView::RailCloseUp:
				TestTrue(Label + TEXT(" outside the rail"), Eye.y < -(Hw + Rw));
				break;
			case ERbTableLookDevView::PocketCloseUp:
				TestTrue(Label + TEXT(" over the table side of the foot-right pocket"), Eye.x > 0.0 && Eye.y < 0.0 && Eye.x < Hl && Eye.y > -Hw);
				break;
			case ERbTableLookDevView::FootEnd:
			{
				TestTrue(Label + TEXT(" in front of the foot end"), Eye.x > Hl + Rw + 0.5);
				TestNearlyEqual(Label + TEXT(" eye 1.15 m above the floor"), Eye.z + Spec.BedHeight, 1.15, 1e-9);
				// Looks down at the foot end's face, below the rail (the coin mechanism of the bar box sits 0.62 m above the floor).
				const rb::Vec3 Focus = Table->WorldToCore(Pose.FocusPoint);
				TestTrue(Label + TEXT(" focus on the foot-end face below the rail"), Focus.z + Spec.BedHeight < 0.70 && FMath::Abs(Focus.x - (Hl + Rw)) < 1e-6);
				break;
			}
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
