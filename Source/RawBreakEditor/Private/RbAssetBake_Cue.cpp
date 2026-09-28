// Cue bake (Owner: UE-4): RbCueMeshBuilder::BuildCue(GetCueSpec(preset), CueBodyState{}) ->
// WriteStaticMesh(<RbAssetPaths::CueMeshDir>/SM_Cue_<Preset>, M_RbCue when it exists, Nanite off, no collision).
// Nanite off: a thin, moving lathe of ~5 k triangles (Nanite gains nothing and its fallback would be what ray tracing sees);
// no collision: the cue never touches the world through Chaos (RbCueClearance is analytic + an environment query).
// The mesh keeps its builder normals (smooth sections, hard edges at the rim / end disc), UV0 (s in metres, azimuth),
// UV1 (section, position in the section) and the per-section vertex colours; the build computes tangents.

#include "RbAssetBakeLibrary.h"

#include "Core/RbAssetPaths.h"
#include "Core/RbTypes.h"
#include "Cue/RbCue.h"
#include "Cue/RbCueMeshBuilder.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogRbBakeCue, Log, All);

bool URbAssetBakeLibrary::BakeCueMesh(ERbCuePreset Preset)
{
	const rb::CueSpec Spec = rb::GetCueSpec(RbTypes::ToCore(Preset));
	const rb::human::CueBodyState Body; // the default body: ARbCue uses the bake only for it (ARbCue::IsDefaultBody)
	UE::Geometry::FDynamicMesh3 Mesh;
	RbCueMeshBuilder::BuildCue(Spec, Body, FRbCueMeshOptions(), Mesh);

	UMaterialInterface* Material = nullptr;
	if (FPackageName::DoesPackageExist(RbAssetPaths::MatCue))
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), RbAssetPaths::MatCue, *FPackageName::GetShortName(RbAssetPaths::MatCue));
		Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
	}

	const FString Package = ARbCue::BakedMeshPackage(Preset);
	FString Error;
	UStaticMesh* StaticMesh = WriteStaticMesh(Mesh, Package, Material, false, false, Error);
	if (!StaticMesh)
	{
		UE_LOG(LogRbBakeCue, Error, TEXT("BakeCueMesh: %s"), *Error);
		return false;
	}
	UE_LOG(LogRbBakeCue, Display, TEXT("BakeCueMesh: %s, length %.3f cm, %d triangles, material %s"), *Package, 100.0 * Spec.Length,
		Mesh.TriangleCount(), Material ? *Material->GetPathName() : TEXT("(none yet)"));
	return true;
}
