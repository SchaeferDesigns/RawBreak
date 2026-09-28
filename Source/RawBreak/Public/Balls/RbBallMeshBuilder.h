#pragma once

// Ball render mesh (ue5-realism-plan 6.2, 6.7): a sphere of radius 1 (scaled per ball by its own radius),
// >= 64 segments around the equator (sagitta 0.034 mm at 64 on a 57.15 mm ball, T20); normals analytic from the
// centre; UV0 unused by the analytic material (the decal layout comes from the object-space position), UV1 an
// octahedral map for the later chalk/dirt render target (plan 6.2). Owner: UE-2.
//
// Topology: UV sphere with a single vertex at each pole, Segments columns (a multiple of 4) and Rings rows (even), so
// the equator and the meridians at 0 / 90 / 180 / 270 deg are mesh edges and every triangle lies inside ONE octant.
// That keeps the octahedral UV1 free of interpolation across its folds (lower hemisphere): each triangle is encoded
// with its own octant's formula, seam vertices get one UV1 element per octant.
//   UV0  equirectangular (u = longitude / 2 pi, v = polar angle / pi), seam at longitude 0, per-triangle pole elements
//   UV1  octahedral: p = n / |n|_1, lower hemisphere folded, uv = 0.5 p.xy + 0.5
//   tangent east (d/d longitude), bitangent south (d/d polar angle) = direction of increasing UV0
// Winding: GetTriNormal (UE convention, VectorUtil::Normal) points outward in UE space. The mesh is closed
// (Euler characteristic 2, every edge shared by two triangles) and every vertex lies exactly on the unit sphere.
// With the default 128 x 64 the largest deviation from the sphere (triangle interiors) is ~ 0.0172 mm on a
// 57.15 mm ball, half the T20 bound for 64 segments; triangles: 2 Segments (Rings - 1) = 16128.

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMesh3.h"

struct FRbBallMeshOptions
{
	int32 Segments = 128;   // around the equator (LOD0; plan 6.7 recommends 128); rounded up to a multiple of 4, >= 8
	int32 Rings = 64;       // pole to pole; rounded up to an even number, >= 4
};

namespace RbBallMeshBuilder
{
	// Unit sphere (radius 1 cm in UE units; the ball component scale = radius [cm]).
	RAWBREAK_API void BuildUnitSphere(const FRbBallMeshOptions& Options, UE::Geometry::FDynamicMesh3& Out);

	// The options BuildUnitSphere actually uses (rounding / minimum rules above).
	RAWBREAK_API FRbBallMeshOptions Sanitize(const FRbBallMeshOptions& Options);

	// Triangle count of BuildUnitSphere for Options (after Sanitize): 2 Segments (Rings - 1).
	RAWBREAK_API int32 TriangleCount(const FRbBallMeshOptions& Options);

	// Octahedral map of a unit direction into [0, 1]^2 (UV1). SignX / SignY (+-1) choose the fold of the lower
	// hemisphere for directions exactly on a fold line (x = 0 or y = 0 with z < 0; the -z pole is the corner
	// (SignX, SignY)); elsewhere they must equal the signs of the direction's x / y.
	RAWBREAK_API FVector2f OctahedralEncode(const FVector3d& UnitDirection, double SignX, double SignY);
	RAWBREAK_API FVector2f OctahedralEncode(const FVector3d& UnitDirection);
	// Inverse of OctahedralEncode (unit direction).
	RAWBREAK_API FVector3d OctahedralDecode(const FVector2f& UV);
}
