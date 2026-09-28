#include "Cue/RbCueMeshBuilder.h"

// Owner: UE-4. TODO(UE-4): lathe mesh with sections, normals, UVs (u along the axis in metres), tests (length,
// taper radii at tip / butt match CueBodyState, tip dome radius = CueSpec::TipDomeRadius).

namespace RbCueMeshBuilder
{
	void BuildCue(const rb::CueSpec& /*Cue*/, const rb::human::CueBodyState& /*Body*/, const FRbCueMeshOptions& /*Options*/,
		UE::Geometry::FDynamicMesh3& Out)
	{
		Out.Clear(); // TODO(UE-4)
	}
}
