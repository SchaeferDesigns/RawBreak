#pragma once

// Ball render mesh (ue5-realism-plan 6.2, 6.7): a sphere of radius 1 (scaled per ball by its own radius),
// >= 64 segments around the equator (sagitta 0.034 mm at 64 on a 57.15 mm ball, T20); normals analytic from the
// centre; UV0 unused by the analytic material (the decal layout comes from the object-space position), UV1 an
// octahedral map for the later chalk/dirt render target (plan 6.2). Owner: UE-2.

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMesh3.h"

struct FRbBallMeshOptions
{
	int32 Segments = 128;   // around the equator (LOD0; plan 6.7 recommends 128)
	int32 Rings = 64;       // pole to pole
};

namespace RbBallMeshBuilder
{
	// Unit sphere (radius 1 cm in UE units; the ball component scale = radius [cm]).
	RAWBREAK_API void BuildUnitSphere(const FRbBallMeshOptions& Options, UE::Geometry::FDynamicMesh3& Out);
}
