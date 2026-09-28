// Ball bake (Owner: UE-2). TODO(UE-2): RbBallMeshBuilder::BuildUnitSphere -> WriteStaticMesh(RbAssetPaths::BallMesh,
// M_RbBall, Nanite off - 128-segment LOD0 per plan 6.7 - no collision).

#include "RbAssetBakeLibrary.h"

bool URbAssetBakeLibrary::BakeBallMesh(int32 /*Segments*/, int32 /*Rings*/)
{
	return false; // TODO(UE-2)
}
