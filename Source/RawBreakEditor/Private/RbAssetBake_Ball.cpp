// Ball bake (Owner: UE-2): RbBallMeshBuilder::BuildUnitSphere -> WriteStaticMesh(RbAssetPaths::BallMesh, M_RbBall when it
// exists, Nanite off - 128-segment LOD0 per plan 6.7, balls move every frame and are tiny on screen - no collision: Chaos
// never touches balls, plan pitfall 23). The mesh carries analytic normals and tangents, so the build keeps them.

#include "RbAssetBakeLibrary.h"

#include "Balls/RbBallMeshBuilder.h"
#include "Core/RbAssetPaths.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogRbBakeBall, Log, All);

bool URbAssetBakeLibrary::BakeBallMesh(int32 Segments, int32 Rings)
{
	FRbBallMeshOptions Options;
	Options.Segments = Segments;
	Options.Rings = Rings;
	const FRbBallMeshOptions Used = RbBallMeshBuilder::Sanitize(Options);
	if (Used.Segments != Segments || Used.Rings != Rings)
	{
		UE_LOG(LogRbBakeBall, Warning, TEXT("BakeBallMesh: %d x %d adjusted to %d x %d (segments a multiple of 4, rings even)"), Segments, Rings,
			Used.Segments, Used.Rings);
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	RbBallMeshBuilder::BuildUnitSphere(Used, Mesh);

	UMaterialInterface* Material = nullptr;
	if (FPackageName::DoesPackageExist(RbAssetPaths::MatBall))
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), RbAssetPaths::MatBall, *FPackageName::GetShortName(RbAssetPaths::MatBall));
		Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
	}

	FString Error;
	UStaticMesh* StaticMesh = WriteStaticMesh(Mesh, RbAssetPaths::BallMesh, Material, false, false, Error);
	if (!StaticMesh)
	{
		UE_LOG(LogRbBakeBall, Error, TEXT("BakeBallMesh: %s"), *Error);
		return false;
	}
	// Collision: no simple shapes are generated and the ball components disable collision (ARbBallSet).
	UE_LOG(LogRbBakeBall, Display, TEXT("BakeBallMesh: %s %d x %d, %d triangles, material %s"), RbAssetPaths::BallMesh, Used.Segments, Used.Rings,
		Mesh.TriangleCount(), Material ? *Material->GetPathName() : TEXT("(none yet)"));
	return true;
}
