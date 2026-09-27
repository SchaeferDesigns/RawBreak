// Common static-mesh writer of the bake library (Owner: UE-0, implemented).

#include "RbAssetBakeLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogRbBake, Log, All);

UStaticMesh* URbAssetBakeLibrary::WriteStaticMesh(const UE::Geometry::FDynamicMesh3& Mesh, const FString& PackagePath,
	UMaterialInterface* Material, bool bNanite, bool bComplexCollision, FString& OutError)
{
	if (Mesh.TriangleCount() == 0)
	{
		OutError = FString::Printf(TEXT("%s: mesh has no triangles"), *PackagePath);
		return nullptr;
	}
	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		OutError = FString::Printf(TEXT("%s: not a valid long package name"), *PackagePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
	UStaticMesh* StaticMesh = FindObject<UStaticMesh>(Package, *AssetName);
	const bool bCreated = StaticMesh == nullptr;
	if (bCreated)
	{
		StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}
	StaticMesh->PreEditChange(nullptr);

	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	const bool bHasTangents = Mesh.HasAttributes() && Mesh.Attributes()->HasTangentSpace();
	FDynamicMeshToMeshDescription Converter;
	Converter.Convert(&Mesh, Description, bHasTangents);

	StaticMesh->GetStaticMaterials().Reset();
	StaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material, FName(TEXT("Material")), FName(TEXT("Material"))));

	StaticMesh->SetNumSourceModels(0);
	FStaticMeshSourceModel& Source = StaticMesh->AddSourceModel();
	Source.BuildSettings.bRecomputeNormals = false;
	Source.BuildSettings.bRecomputeTangents = !bHasTangents;
	Source.BuildSettings.bGenerateLightmapUVs = false;
	Source.BuildSettings.bBuildReversedIndexBuffer = false;
	Source.BuildSettings.bGenerateDistanceFieldAsIfTwoSided = false;
	StaticMesh->CreateMeshDescription(0, MoveTemp(Description));
	StaticMesh->CommitMeshDescription(0);

	FMeshNaniteSettings Nanite = StaticMesh->GetNaniteSettings();
	Nanite.bEnabled = bNanite;
	if (bNanite)
	{
		// UE 5.8 ray tracing (r.RayTracing.Nanite.Mode = 0, RT proxies off by default) traces the Nanite FALLBACK
		// mesh, and complex-as-simple collision is cooked from it too. The Auto fallback decimates, so ball
		// reflections, Lumen HWRT hits and the pawn/cue collision would see a coarser table than the one rendered
		// (pocket jaws, cushion noses). Our meshes are small: keep every triangle (review R-02).
		Nanite.FallbackTarget = ENaniteFallbackTarget::PercentTriangles;
		Nanite.FallbackPercentTriangles = 1.0f;
	}
	StaticMesh->SetNaniteSettings(Nanite);

	StaticMesh->CreateBodySetup();
	if (UBodySetup* Body = StaticMesh->GetBodySetup())
	{
		Body->CollisionTraceFlag = bComplexCollision ? ECollisionTraceFlag::CTF_UseComplexAsSimple : ECollisionTraceFlag::CTF_UseDefault;
	}

	StaticMesh->Build(true);
	StaticMesh->PostEditChange();
	StaticMesh->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(StaticMesh);
	}

	if (!UEditorLoadingAndSavingUtils::SavePackages({Package}, false))
	{
		OutError = FString::Printf(TEXT("%s: save failed"), *PackagePath);
		return nullptr;
	}
	UE_LOG(LogRbBake, Display, TEXT("baked %s (%d triangles, Nanite %d)"), *PackagePath, Mesh.TriangleCount(), bNanite ? 1 : 0);
	return StaticMesh;
}

bool URbAssetBakeLibrary::BakeSelfTest(const FString& PackagePath)
{
	UE::Geometry::FGridBoxMeshGenerator Box;
	Box.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(50.0, 50.0, 50.0));
	Box.EdgeVertices = UE::Geometry::FIndex3i(2, 2, 2);
	Box.Generate();
	UE::Geometry::FDynamicMesh3 Mesh(&Box);

	FString Error;
	if (!WriteStaticMesh(Mesh, PackagePath, nullptr, false, true, Error))
	{
		UE_LOG(LogRbBake, Error, TEXT("BakeSelfTest: %s"), *Error);
		return false;
	}
	// The Nanite path too (review R-02): the fallback used by ray tracing / collision must keep every triangle.
	UStaticMesh* NaniteMesh = WriteStaticMesh(Mesh, PackagePath + TEXT("_Nanite"), nullptr, true, true, Error);
	if (!NaniteMesh)
	{
		UE_LOG(LogRbBake, Error, TEXT("BakeSelfTest (Nanite): %s"), *Error);
		return false;
	}
	const FMeshNaniteSettings& Nanite = NaniteMesh->GetNaniteSettings();
	if (!Nanite.bEnabled || Nanite.FallbackTarget != ENaniteFallbackTarget::PercentTriangles || Nanite.FallbackPercentTriangles != 1.0f)
	{
		UE_LOG(LogRbBake, Error, TEXT("BakeSelfTest: Nanite fallback is not full detail"));
		return false;
	}
	return true;
}
