#pragma once

// Render / collision meshes of the table generated from rb::TableGeometry - the SAME data the physics uses
// (ue5-realism-plan 6.7, pitfall 16: never hand-modelled). Pure geometry code (no UObjects), so it runs in the
// game (UDynamicMeshComponent fallback), in the editor bake (UStaticMesh assets, RawBreakEditor) and in tests.
// Owner: UE-1. Docs/ue-architecture.md 5.3.
//
// Output frame: TABLE-LOCAL UE (FRbCoords: X = x, Y = -y, Z = z, centimetres; origin = bed centre on the cloth).
// The Y mirror flips handedness: every triangle is emitted with the winding that makes its normal point out of
// the solid IN UE SPACE (UE::Geometry::VectorUtil::Normal convention; tests check it). Material ids: one mesh per
// ERbTablePart, a single material slot each.
//
// Every part is a set of CLOSED, consistently oriented solids (no boundary edges; watertight per solid), so mesh
// distance fields, Lumen cards and complex collision see proper volumes. Hidden contact faces between parts
// (e.g. a cushion's bottom on the bed) are coplanar back to back and never visible.
//
// Parts and sources (architecture.md 13 item 2; the physics surfaces are reproduced exactly up to the 1 um vertex weld,
// RawBreak.Unit.Table.* checks them to 0.01 mm):
//   Bed          slate (z = 0 .. -SlateThickness) inside the cushion-back rectangle minus the pocket capture circles
//                (r_p); on each pocket's open front arc the slate edge is rounded with r_d (torus from a_d = r_p + r_d
//                at z = 0 down to r_p at z = -r_d), under the cushions the cut is a plain vertical wall at r_p.
//                Corners of the rectangle that the capture circle leaves free behind the hole (TABLE_7FT_BAR) belong to
//                the liner part's corner inserts.
//   CushionCloth one solid per NoseSegment: the exact CushionProfile (rubber face from (0.4 CushionWidth, 0) to the
//                nose (0, h), cushion top plane to the cushion back (CushionWidth, RailTopZ)); the nose line and the
//                jaw arcs are exactly BuildNoseOutline(ArcSegments) at z = h; jaw arcs are ruled blends down to the
//                matching arcs of the face / facing bottom lines; facings are the physics' undercut planes
//                (Backdraft); the cushion is cut by the capture cylinder r_p where the pocket reaches under it.
//                The CushionNoseProfileRadius rounding (art) is a normal band on the walls below the nose chain
//                (geometry stays on the physics profile, shading is rounded).
//   RailCaps     the rail-top cap slab (z = RailTopZ - CapThickness .. RailTopZ) between the cushion-back rectangle and
//                the outer boundary, minus the liner collars' outer circles (r_p + LinerThickness), with flush holes for
//                the sights; split into the 4 rails by corner mitres (UV grain along each rail).
//   Apron        the rail body under the cap (down to the slate bottom, cut like the caps) and the apron skirt (down to
//                ApronDepthCm) along the outer boundary.
//   PocketLiners per pocket: the liner collar = the pocket's back wall behind the hole where the capture cylinder cuts
//                through the rail (annulus r_p .. r_p + LinerThickness outside the cushion-back rectangle, slate bottom up
//                to RailTopZ = WallTopZ; its inner wall is the physics' back wall at exactly r_p), extended as a strip
//                along the cushion-back line where an undercut facing leaves a stretch of it open next to the hole (bar
//                tables), so no rail wood shows inside a pocket; corner inserts filling the rectangle corners left free
//                behind a hole (TABLE_7FT_BAR, slate bottom up to WallTopZ); a leather drop pocket (pro tables) or rubber
//                cup (bar tables) of outer radius r_p hanging below the slate. The hole walls inside the rectangle (slate
//                below the drop rounding, cushion cuts) are cloth-covered and belong to the Bed / CushionCloth solids.
//   Sights       18 flush discs (Sight::Position, SightDiameter) inlaid in the caps.
//   Legs         four square legs (pro / home tables) or a coin-op cabinet with feet (bar tables) down to the floor
//                (z = -BedHeight).

#include "CoreMinimal.h"

#include "Core/RbTypes.h"
#include "DynamicMesh/DynamicMesh3.h"

#include "rb/Geometry/TableGeometry.h"

enum class ERbTableBaseStyle : uint8
{
	Auto,    // Cabinet for bar-box tables (napped bar cloth), Legs otherwise
	Legs,
	Cabinet,
};

struct FRbTableMeshOptions
{
	int32 ArcSegments = 12;          // samples per jaw arc (BuildNoseOutline SamplesPerArc)
	int32 CircleSegments = 128;      // pocket circles (full circle, even): sagitta 0.019 mm at r_p = 62 mm
	int32 SightSegments = 32;
	int32 DropRoundingSegments = 8;  // quarter circle of the slate drop rounding r_d
	double ApronDepthCm = 18.0;      // rail/apron height below the cloth (art, ESTIMATE)
	double LegSizeCm = 16.0;         // square legs (M1 art)
	bool bBuildLegs = true;
	ERbTableBaseStyle BaseStyle = ERbTableBaseStyle::Auto;
	double CapThicknessCm = 2.0;     // wooden rail cap slab
	double SkirtThicknessCm = 2.5;   // apron skirt board (reduced automatically to clear the pocket circles)
	double SightDepthCm = 0.3;       // inlay depth of the flush sights
	double LinerThicknessCm = 0.3;   // leather / rubber pocket wall and liner collar (the rails are cut at r_p + this)
	double CapEdgeRoundingCm = 0.4;  // normal band of the rounded outer rail-cap edge
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
	// False with OutError if the geometry is not built (empty Noses), is pocketless (not supported: every ERbTablePreset
	// has pockets) or a part fails.
	RAWBREAK_API bool BuildAll(const rb::TableGeometry& Geometry, const FRbTableMeshOptions& Options, FRbTableMeshSet& Out, FString& OutError);

	RAWBREAK_API bool BuildPart(const rb::TableGeometry& Geometry, ERbTablePart Part, const FRbTableMeshOptions& Options,
		UE::Geometry::FDynamicMesh3& Out, FString& OutError);

	// --- baked assets (single source of the naming for ARbTable and URbAssetBakeLibrary::BakeTableMeshes) -------------

	// "NineFootPro", "SevenFootBar", ... (the ERbTablePreset enumerator name).
	RAWBREAK_API FString GetPresetName(ERbTablePreset Preset);

	// Long package name <RbAssetPaths::TableMeshDir>/<Preset>/SM_Table_<Part>.
	RAWBREAK_API FString GetBakedMeshPackagePath(ERbTablePreset Preset, ERbTablePart Part);

	// Object path <package>.SM_Table_<Part> (for soft references / LoadObject).
	RAWBREAK_API FString GetBakedMeshObjectPath(ERbTablePreset Preset, ERbTablePart Part);

	// Bake / runtime policy per part: complex-as-simple collision (pawn, cue sweeps, traces; every part, so the collision surface is
	// the rendered one) and Nanite (not on the thin sights).
	RAWBREAK_API bool PartHasCollision(ERbTablePart Part);
	RAWBREAK_API bool PartUsesNanite(ERbTablePart Part);
}
