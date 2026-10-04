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
//   Legs         four legs with plinths (pro / home tables) or the coin-op cabinet's four pedestal legs (bar tables) down to
//                the levelers.
//
// M2-L look-dev (Docs/ue-architecture.md 18.7): detail only where the physics does not look.
//   * The rail outline is a rounded rectangle (OuterBoundary with plan-corner radius OuterCornerRadiusCm); the rails are built
//     as zones between cuts (9-ft: four mitred rails; 7-ft: caps between the castings), each a closed solid. The outer top
//     edge of the caps is a REAL quarter round (CapEdgeRadiusCm, EdgeSegments), the caps' top plane stays RailTopZ.
//   * Cushion nose roll: behind the exact nose line (z = h, BuildNoseOutline) the cloth rolls over the rubber nose onto the
//     cushion-top plane within NoseRollWidthCm (vertical tangent at the nose line, a small convex bulge, tangent onto the
//     top plane; the real cushion top is convex above the physics' planar approximation). It never reaches in front of the
//     nose outline, tapers to zero along the jaw arcs so the facings (physics planes) are untouched.
//   * RubberStrip: the black rubber lip along the bottom of each cushion face (z <= RubberStripHeightCm, behind the nose line).
//   * PocketBuckets: 9-ft leather drop pockets (flange + bulged bucket), 7-ft gully throats; PocketLiners keeps the rubber
//     liner collars (the physics' back wall r_p) and the corner inserts.
//   * Coin-op (BaseStyle Cabinet, venue-dive-bar 3.1 "HALVERSON Stallion 7"): corner and side castings replace the rail
//     around the pockets (tops flush with RailTopZ, openings = the liner collars, >= 2 mm to the capture cylinder r_p), the
//     cabinet box of boards (Z 0.30 m .. the rail underside), aluminium trim, pedestal legs with screw levelers, the coin
//     mechanism with its 6-quarter slide on the foot end, the coin door on the wall side (core -y), the ball-trap window,
//     the ball tray and the cue-ball return cup in the foot end.

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
	double ApronDepthCm = 18.0;      // rail/apron height below the cloth (art, ESTIMATE; legs style)
	double LegSizeCm = 15.0;         // square legs (legs style)
	bool bBuildLegs = true;
	ERbTableBaseStyle BaseStyle = ERbTableBaseStyle::Auto;
	double CapThicknessCm = 2.0;     // wooden rail cap slab
	double SkirtThicknessCm = 2.5;   // apron skirt board (reduced automatically to clear the pocket circles)
	double SightDepthCm = 0.3;       // inlay depth of the flush sights
	double LinerThicknessCm = -1.0;  // leather / rubber pocket wall and liner collar (the rails are cut at r_p + this); <= 0 = the base
	                                 // style's default (M2-L: legs 0.8 = the leather lip that rings a pro table's pocket on the rail
	                                 // top; cabinet 0.3 = the coin-op's rubber liner inside the casting, whose corner islands leave no
	                                 // more room). ResolveLinerThicknessCm.
	double CapEdgeRoundingCm = 0.4;  // M1 (unused since M2: the cap edge is real geometry, CapEdgeRadiusCm)

	// --- M2-L look-dev (<= 0 = the base style's default) -----------------------------------------------------------------
	double OuterCornerRadiusCm = -1.0; // plan-corner radius of the rail outline (legs 2.0, cabinet 4.0)
	double CapEdgeRadiusCm = -1.0;     // real quarter round of the outer rail-cap edge (legs 0.8, cabinet 0.5; >= 0.3)
	int32 EdgeSegments = 8;            // segments of every rounded profile edge (>= 8)
	int32 CornerSegments = 12;         // segments of a plan-corner arc (even: the mitre lies on its 45 deg sample)
	double NoseRollWidthCm = 0.8;      // cushion nose roll width behind the straight nose line (0.6 x the jaw radius along the jaws)
	double NoseRollBulge = 0.25;       // max bulge of the roll above the straight blend, as a fraction of its width
	int32 NoseRollSegments = 12;
	double NoseRollTaperCm = 3.0;      // the straight roll widens from the jaw width to NoseRollWidthCm over this length
	double RubberStripHeightCm = 0.45; // rubber lip at the bottom of the cushion face
	double RubberStripThicknessCm = 0.08;
	double CornerCastingLengthCm = 24.0; // coin-op corner castings: along each rail from the outer corner (past the pocket collar)
	double SideCastingLengthCm = 26.0;   // coin-op side castings: total length along the long rail
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

	// --- M2-L -----------------------------------------------------------------------------------------------------------------

	// The base style the options resolve to for a table (Auto: Cabinet for napped bar cloth).
	RAWBREAK_API ERbTableBaseStyle ResolveBaseStyle(const rb::TableSpec& Spec, const FRbTableMeshOptions& Options);

	// The liner thickness [cm] the options resolve to for a table (LinerThicknessCm, or the base style's default when <= 0).
	RAWBREAK_API double ResolveLinerThicknessCm(const rb::TableSpec& Spec, const FRbTableMeshOptions& Options);

	// The plan-corner radius [cm] of the rail outline / coin-op cabinet the options resolve to (OuterCornerRadiusCm, or the base
	// style's default when <= 0).
	RAWBREAK_API double ResolveOuterCornerRadiusCm(const rb::TableSpec& Spec, const FRbTableMeshOptions& Options);

	// Table-dimension parameters of the table-family materials (Shaders/Private/RbTableLook.ush places the wear from the table-local
	// position and needs these; rb_make_materials.py bakes the committed presets' values into the generated materials). Names and
	// values for a table: scalars HalfLength / HalfWidth / CushionWidth / FaceBase [m] (cloth), CornerRadiusCm / BedHeightCm and the
	// vector HalfOuterCm (rg) [cm] (laminate). ARbTable gives a table whose values differ from its material's a dynamic instance.
	struct FMaterialTableParameters
	{
		TArray<TPair<FName, double>, TInlineAllocator<8>> Scalars;
		TArray<TPair<FName, FVector2D>, TInlineAllocator<2>> Vectors;
	};
	RAWBREAK_API FMaterialTableParameters GetMaterialTableParameters(const rb::TableGeometry& Geometry, const FRbTableMeshOptions& Options);

	// Whether BuildAll produces triangles for a part in a base style (coin-op parts are empty on a legs table).
	RAWBREAK_API bool PartExpected(ERbTableBaseStyle Style, ERbTablePart Part);

	// Object path of the default material of a part for a preset (generated by Tools/unreal/editor/rb_make_materials.py; the
	// dive-bar instances of venue-dive-bar 6.4 for the coin-op preset). ARbTable uses it for parts without an override, the bake
	// assigns it to the baked asset.
	RAWBREAK_API FString GetDefaultMaterialPath(ERbTablePreset Preset, ERbTablePart Part);

	// Resolved geometry parameters (tests, look-dev): rail outline corner radius, cap edge radius, nose roll width, the cushion
	// roll's worst tessellation sagitta [m] and the coin-op cut positions (corner cut |x| / |y| and side half-length, core m).
	struct FLookDevMetrics
	{
		ERbTableBaseStyle Style = ERbTableBaseStyle::Legs;
		double OuterCornerRadius = 0.0;
		double CapEdgeRadius = 0.0;
		int32 CapEdgeSegments = 0;
		double NoseRollWidth = 0.0;
		double NoseRollMaxSagitta = 0.0;
		double CornerCutX = 0.0;
		double CornerCutY = 0.0;
		double SideCutHalf = 0.0;
		double SkirtInset = 0.0;
	};
	RAWBREAK_API bool ComputeLookDevMetrics(const rb::TableGeometry& Geometry, const FRbTableMeshOptions& Options, FLookDevMetrics& Out, FString& OutError);
}
