#pragma once

// Render / collision meshes of the table generated from rb::TableGeometry - the SAME data the physics uses
// (ue5-realism-plan 6.7, pitfall 16: never hand-modelled). Pure geometry code (no UObjects), so it runs in the
// game (UDynamicMeshComponent fallback), in the editor bake (UStaticMesh assets, RawBreakEditor) and in tests.
// Owner: UE-1. Docs/ue-architecture.md 5.3.
//
// Output frame: TABLE-LOCAL UE (FRbCoords: X = x, Y = -y, Z = z, centimetres; origin = bed centre on the cloth).
// The Y mirror flips handedness: every triangle is emitted with the winding that makes its normal point out of
// the solid IN UE SPACE (tests check it). Material ids: one mesh per ERbTablePart, a single material slot each.
//
// Parts and sources (architecture.md 13 item 2):
//   Bed          slate top at z = 0 inside the cushion-back outline, pocket cuts = PocketGeometry capture circles
//                (r_p) with the drop rounding r_d on the front arc; slate thickness from TableSpec
//   CushionCloth BuildNoseOutline(...) swept with CushionProfile (rubber face -> nose (0, h) -> cushion top ->
//                cushion back), facings as undercut planes (Backdraft), jaw arcs sampled with ArcSegments
//   RailCaps     RailTops polygons of kind RailCap (the physics' own convex polygons incl. pocket surrounds and
//                cut discs) at RailTopZ
//   Apron        outer rail faces from OuterBoundary down to the apron depth, plus the body skirt
//   PocketLiners hole walls: cylinders r_p around CaptureCenter from the rim down (front arc below -r_d, back
//                wall up to WallTopZ) + a closed cup/leather pocket below
//   Sights       18 sights (Sight::Position, SightDiameter) as thin discs on the caps
//   Legs         four legs / base blocks down to the floor (BedHeight)

#include "CoreMinimal.h"

#include "Core/RbTypes.h"
#include "DynamicMesh/DynamicMesh3.h"

#include "rb/Geometry/TableGeometry.h"

struct FRbTableMeshOptions
{
	int32 ArcSegments = 12;        // samples per jaw arc (BuildNoseOutline SamplesPerArc)
	int32 CircleSegments = 64;     // pocket cut / liner circles (full circle)
	int32 SightSegments = 24;
	double ApronDepthCm = 18.0;    // rail/apron height below the cloth (art, ESTIMATE)
	double LegSizeCm = 16.0;       // square legs (M1 art)
	bool bBuildLegs = true;
};

struct FRbTableMeshSet
{
	UE::Geometry::FDynamicMesh3 Parts[static_cast<int32>(ERbTablePart::Count)];

	UE::Geometry::FDynamicMesh3& Get(ERbTablePart Part) { return Parts[static_cast<int32>(Part)]; }
	const UE::Geometry::FDynamicMesh3& Get(ERbTablePart Part) const { return Parts[static_cast<int32>(Part)]; }
};

namespace RbTableMeshBuilder
{
	// Builds every part (normals, UV0 in world-scale cm / 100 so 1 UV unit = 1 m, tangents).
	// False with OutError if the geometry is not built (empty Noses) or a part fails.
	RAWBREAK_API bool BuildAll(const rb::TableGeometry& Geometry, const FRbTableMeshOptions& Options, FRbTableMeshSet& Out, FString& OutError);

	RAWBREAK_API bool BuildPart(const rb::TableGeometry& Geometry, ERbTablePart Part, const FRbTableMeshOptions& Options,
		UE::Geometry::FDynamicMesh3& Out, FString& OutError);
}
