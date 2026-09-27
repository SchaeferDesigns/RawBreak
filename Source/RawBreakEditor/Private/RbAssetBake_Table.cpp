// Table bake (Owner: UE-1). TODO(UE-1): FRbTableContext for Preset -> RbTableMeshBuilder::BuildAll -> WriteStaticMesh
// per part to <RbAssetPaths::TableMeshDir>/<Preset>/SM_Table_<Part> with the part's default material (RbAssetPaths),
// Nanite on the rails / apron / legs (not on thin sights), complex collision on bed, cushions, caps, apron.

#include "RbAssetBakeLibrary.h"

int32 URbAssetBakeLibrary::BakeTableMeshes(ERbTablePreset /*Preset*/, bool /*bNanite*/)
{
	return -1; // TODO(UE-1)
}
