#include "Balls/RbBallMeshBuilder.h"

// Owner: UE-2. TODO(UE-2): UV sphere (or cube sphere) with analytic normals, octahedral UV1, tangents; test the
// silhouette error bound and closedness.

namespace RbBallMeshBuilder
{
	void BuildUnitSphere(const FRbBallMeshOptions& /*Options*/, UE::Geometry::FDynamicMesh3& Out)
	{
		Out.Clear(); // TODO(UE-2)
	}
}
