#pragma once

// Editor-only bake of the procedural geometry into UStaticMesh assets (Docs/ue-architecture.md 5.3, 9.3). Called
// from editor Python in the headless commandlet, e.g.
//   unreal.RbAssetBakeLibrary.bake_table_meshes(unreal.RbTablePreset.NINE_FOOT_PRO, True)
// Baked meshes get Nanite, mesh distance fields and Lumen cards (quality: ue5-realism-plan 6.x, 9.1); the runtime
// UDynamicMeshComponent path shows the same FDynamicMesh3 when no bake exists. Everything is regenerated from
// rb::TableSpec / CueSpec, so the assets are caches of code, never hand-edited.
// Owners: the common writer UE-0 (RbAssetBake_Common.cpp), BakeTableMeshes UE-1 (RbAssetBake_Table.cpp),
// BakeBallMesh UE-2 (RbAssetBake_Ball.cpp), BakeCueMesh UE-4 (RbAssetBake_Cue.cpp).

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "Core/RbTypes.h"
#include "DynamicMesh/DynamicMesh3.h"

#include "RbAssetBakeLibrary.generated.h"

class UMaterialInterface;
class UStaticMesh;

UCLASS()
class RAWBREAKEDITOR_API URbAssetBakeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Every ERbTablePart of Preset -> <RbAssetPaths::TableMeshDir>/<Preset>/SM_Table_<Part>. Returns the number of
	// assets written, -1 on error (logged).
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Bake")
	static int32 BakeTableMeshes(ERbTablePreset Preset, bool bNanite);

	// Unit ball sphere -> RbAssetPaths::BallMesh.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Bake")
	static bool BakeBallMesh(int32 Segments, int32 Rings);

	// Procedural cue -> <RbAssetPaths::CueMeshDir>/SM_Cue_<Preset>.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Bake")
	static bool BakeCueMesh(ERbCuePreset Preset);

	// M2-F (Docs/ue-architecture.md 18.3): the procedural stand-in hand that carries the cue ball (diegetic ball in hand) ->
	// RbAssetPaths::HandCarryMesh. Implemented in RbAssetBake_Player.cpp (owner M2-F; stub until then).
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Bake")
	static bool BakeHandCarryMesh();

	// Pipeline self-test: a 1 m box through the whole bake path to PackagePath (e.g. /Game/Dev/PipelineProof/SM_BakeTest),
	// and a Nanite copy to PackagePath + "_Nanite" whose fallback must keep 100 % of the triangles (ray tracing / collision).
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Bake")
	static bool BakeSelfTest(const FString& PackagePath);

	// Common writer: FDynamicMesh3 (UE cm, with normals / UVs / optional tangents) -> saved UStaticMesh at PackagePath
	// (long package name). Creates or overwrites; material slot 0 = Material (may be null); complex-as-simple collision
	// when requested. Returns the mesh or nullptr with OutError.
	static UStaticMesh* WriteStaticMesh(const UE::Geometry::FDynamicMesh3& Mesh, const FString& PackagePath, UMaterialInterface* Material,
		bool bNanite, bool bComplexCollision, FString& OutError);
};
