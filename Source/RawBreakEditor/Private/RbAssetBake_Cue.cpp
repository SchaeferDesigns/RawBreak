// Cue bake (Owner: UE-4). TODO(UE-4): RbCueMeshBuilder::BuildCue(GetCueSpec(preset), CueBodyState{}) ->
// WriteStaticMesh(<RbAssetPaths::CueMeshDir>/SM_Cue_<Preset>, M_RbCue, Nanite off, no collision).

#include "RbAssetBakeLibrary.h"

bool URbAssetBakeLibrary::BakeCueMesh(ERbCuePreset /*Preset*/)
{
	return false; // TODO(UE-4)
}
