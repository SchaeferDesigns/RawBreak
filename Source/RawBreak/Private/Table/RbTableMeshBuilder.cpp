#include "Table/RbTableMeshBuilder.h"

#include "Core/RbCoords.h"

// Owner: UE-1. TODO(UE-1): implement every part from the geometry (see the header for sources and frame),
// with outward normals in UE space, UVs, tangents; tests RawBreak.Unit.Table.* (nose line at h, pocket cut radius,
// 18 sights, closed bed outline, bounds = OuterBoundary, normals up on the cloth).

namespace RbTableMeshBuilder
{
	bool BuildAll(const rb::TableGeometry& Geometry, const FRbTableMeshOptions& Options, FRbTableMeshSet& Out, FString& OutError)
	{
		for (int32 PartIndex = 0; PartIndex < static_cast<int32>(ERbTablePart::Count); ++PartIndex)
		{
			if (!BuildPart(Geometry, static_cast<ERbTablePart>(PartIndex), Options, Out.Parts[PartIndex], OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool BuildPart(const rb::TableGeometry& Geometry, ERbTablePart /*Part*/, const FRbTableMeshOptions& /*Options*/,
		UE::Geometry::FDynamicMesh3& Out, FString& OutError)
	{
		Out.Clear();
		if (Geometry.Noses.Size() == 0)
		{
			OutError = TEXT("table geometry is not built");
			return false;
		}
		// TODO(UE-1): placeholder - an empty mesh per part.
		return true;
	}
}
