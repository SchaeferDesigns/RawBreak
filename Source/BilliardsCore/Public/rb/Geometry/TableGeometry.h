#pragma once

// Exact table geometry built from a TableSpec (equipment 3, 4, 5.3; physics-collisions 4.1, 5.1-5.3,
// 6.2). This is the SINGLE source of truth consumed by the physics (event detection and contact
// frames) AND by the Unreal mesh generation (cloth outline, cushion profile, pocket cuts, diamonds,
// spots/strings). The render mesh must never be modelled by eye (ue5-realism-plan 6.7, pitfall 16).
// Owner: WP-2 (equipment & table geometry).
//
// Everything is in the core frame (metres, +x foot, +y left, +z up, cloth z = 0). Plan-view
// elements are 2D at the height stated per element. One corner and one side pocket are constructed
// and mirrored (equipment 12.8); index conventions are documented per container.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Math/Aabb.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"

#include <cstdint>

namespace rb
{
	inline constexpr int kMaxNoseSegments = kCushionCount; // 6 entries always (index = CushionId; a pocketless table marks C1, C4 absent)
	inline constexpr int kMaxPockets = kPocketCount;       // 6
	inline constexpr int kMaxJaws = 2 * kMaxPockets;       // 12
	inline constexpr int kMaxRailTopPolygons = 48;         // 6 cushion tops + 6 cap strips + pocket surrounds (>= 6, several pieces each)
	inline constexpr int kMaxRailTopVertices = 8;
	inline constexpr int kMaxSights = 18;
	inline constexpr int kMaxProfilePoints = 16;

	// Cushion nose line at height h, ending at the tangent points of the rounded jaws (equipment 5.3).
	// Pocketless tables: C0 / C3 span the whole long rails and C1 / C4 have Present = false (Ids.h).
	struct NoseSegment
	{
		bool Present = true;
		CushionId Cushion = CushionId::None;
		Vec2 Start;          // CCW order around the table (Start -> End)
		Vec2 End;
		Vec2 Direction;      // unit (End - Start)
		Vec2 InwardNormal;   // unit, horizontal, points into the playing area (n_c, collisions 4.1)
		double Length = 0.0; // [m]
		double Height = 0.0; // h [m]
	};

	// Rounded jaw point: circle of radius r_j at height h, tangent to the nose line and to the facing.
	// The exposed arc runs between the two tangent points on the side away from the material; seen from Center
	// they lie along the outward normals of the nose (NoseSegment::InwardNormal) and of the facing
	// (Facing::PocketNormal), so AngleSweep = pi - C. r_j = 0 gives a sharp jaw (Center = tangent points =
	// VirtualPoint).
	struct JawArc
	{
		PocketId Pocket = PocketId::None;
		JawSide Side = JawSide::Incoming;
		Vec2 Center;          // r_j / sin(C/2) from the virtual point along the bisector, inside the material
		double Radius = 0.0;  // r_j [m]
		double Height = 0.0;  // h [m]
		double AngleFrom = 0.0;  // plan angle (atan2 about Center, in (-pi, pi]) where the exposed arc starts [rad]
		double AngleSweep = 0.0; // CCW sweep of the exposed arc, pi - C > 0 [rad]
		Vec2 TangentOnNose;   // r_j / tan(C/2) from the virtual point along the nose line
		Vec2 TangentOnFacing; // same distance along the facing line
		Vec2 VirtualPoint;    // WPA jaw point (nose line x facing line)
	};

	// Pocket facing: undercut plane through its plan line at height h, tilted by the back draft.
	struct Facing
	{
		PocketId Pocket = PocketId::None;
		JawSide Side = JawSide::Incoming;
		Vec2 Start;           // plan line at z = h: jaw-arc tangent point
		Vec2 End;             // plan line at z = h: where the facing meets the cushion back (exactly on the
		                      // cushion-back line, CushionWidth behind the nose line)
		Vec2 Direction;       // unit (End - Start), into the pocket
		double Length = 0.0;  // |End - Start| [m]
		double LengthFromVirtualPoint = 0.0; // virtual jaw point -> cushion back (CushionWidth / sin phi), collisions P-1
		Vec2 PocketNormal;    // horizontal unit normal of the plan line pointing into the pocket opening
		double TopHeight = 0.0;  // h [m]
		double Backdraft = 0.0;  // beta_v [rad]: face normal = PocketNormal tilted DOWN by beta_v
		double Thickness = 0.0;  // hard-rubber facing thickness [m] (art)
	};

	struct PocketGeometry
	{
		PocketId Id = PocketId::None;
		PocketKind Kind = PocketKind::Corner;
		Vec2 JawPoint[2];         // virtual jaw points, index = JawSide
		Vec2 MouthMid;            // midpoint of the mouth line
		Vec2 Axis;                // unit pocket axis, pointing OUT of the table
		double Mouth = 0.0;       // [m]
		double CutAngle = 0.0;    // C [rad]
		double FacingAngle = 0.0; // phi = pi - C [rad]
		double Convergence = 0.0; // beta per side: C - 3pi/4 (corner) or C - pi/2 (side) [rad]
		double Shelf = 0.0;       // [m]
		double Throat = 0.0;      // throat width 2 in (kThroatMeasureDepth) behind the noses [m]
		Vec2 CaptureCenter;       // C_cap = MouthMid + (Shelf + r_p) Axis
		double CaptureRadius = 0.0;  // r_p: hole wall / liner cylinder [m]
		double DropRadius = 0.0;     // r_d: slate-edge rounding [m]
		double DropEdgeRadius = 0.0; // a_d = r_p + r_d: drop-edge trigger / pivot axis radius [m] (collisions pitfall 19)
		// Front (table-side) arc, "the capture circle's front arc between the facings" (collisions 5.3): the angular
		// range about CaptureCenter where the r_p circle (slate cut / liner) lies inside the pocket opening (on the
		// pocket side of both facing plan lines and in front of the cushion-back lines), through its point nearest the
		// table (CaptureCenter - r_p Axis). Its ends are where the r_p circle meets the facing plan lines, or a
		// cushion-back line where the wall reaches the cushion back first (TABLE_7FT_78 side pockets); the rest of the
		// circle lies under the rail (back wall up to WallTopZ). The same angles bound the rim / drop edge at a_d: on
		// every preset the a_d circle meets the facing lines 4-9 deg inside the r_p crossings (the a_d points beyond
		// lie behind a facing, where no ball center on the shelf can be). A custom hole centred far behind a
		// cushion-back line can let the a_d circle reach the opening at wider angles; the arc is then widened to the
		// a_d circle's, so every drop-edge point in the opening is on the arc.
		double FrontArcFrom = 0.0;   // plan angle about CaptureCenter where the front (table-side) arc starts, in (-pi, pi] [rad]
		double FrontArcSweep = 0.0;  // CCW sweep of the front arc between the facings, > 0 (< pi on every preset) [rad]
		double LinerUndercut = 0.0;  // beta_l [rad]
		double Backdraft = 0.0;      // beta_v [rad]
		// Hole wall / liner (collisions 5.3): cylinder r_p around CaptureCenter. On the FRONT arc (between the
		// facings, FrontArcFrom/Sweep) it exists only below the rim, z < -r_d; on the rest of the circle (the
		// back wall behind the pocket, cut through the rail) it rises up to WallTopZ = RailTopZ, where the rail
		// cap (RailTopPolygon with a pocket cut) begins. Airborne balls crossing the pocket meet it (8.9).
		double WallTopZ = 0.0;       // [m]
	};

	// Rail-top surfaces (collisions 6.2; equipment 4.2 art profile). The union of all polygons covers the
	// whole rail top from the nose lines to the outer rail edge INCLUDING the corner and side pocket
	// surrounds (the areas around the pocket cuts), so a ball landing anywhere on the rails meets a surface.
	enum class RailTopKind : std::uint8_t
	{
		CushionTop, // sloped plane from the nose line (z = h) to the cushion back (z = RailTopZ), ~13 deg
		RailCap,    // flat plane z = RailTopZ from the cushion back to the outer rail edge (incl. pocket surrounds)
	};

	// What lies beyond one polygon edge (event routing for balls moving ON the rail top).
	enum class RailEdgeKind : std::uint8_t
	{
		Seam,        // same surface continues in the neighbouring polygon (no physical edge; context update only)
		Nose,        // cushion-top edge above the nose line: beyond it is the table (ball drops back over the nose)
		CushionBack, // ridge between the sloped cushion top and the flat cap (convex edge, EdgeLine island feature)
		OuterEdge,   // outer rail edge: beyond it is the floor (OffTable Floor once the center passes the outer boundary)
		Facing,      // cushion-top edge above a pocket facing: beyond it is the pocket opening
	};

	// Convex plan polygon (CCW) on a plane, optionally minus a disc (the pocket cut through the rail):
	// the surface exists over { p in polygon } minus { |p - CutCenter| < CutRadius }.
	//
	// Decomposition built by BuildTableGeometry (A-GEO-1: the polygons cover every rail-top point between the
	// nose lines and the outer boundary exactly once, except the pocket openings and the cut discs). The rail
	// top excludes each pocket opening: the region between the facing plan lines from the mouth line back to the
	// cushion-back line(s) (a corner opening ends at the cushion-back corner). Pocket tables: 28 polygons, the
	// canonical quadrant x >= 0, y >= 0 in the order
	//   0 CushionTop C3 half next to P4: nose (virtual jaw point -> seam), seam, cushion back, facing
	//   1 CushionTop C3 half next to P3 (the left-rail cushion is split at the midpoint x_s of its virtual jaw
	//     points, so every piece meets one pocket)
	//   2 CushionTop C2 half above y = 0
	//   3 RailCap behind P4, x in [0, x_s]      4 RailCap toward P3, x in [x_s, L/2 + CushionWidth]
	//   5 RailCap corner square behind P3 (Cushion None)      6 RailCap foot strip above y = 0
	// followed by its mirrors x -> -x (7-13), both (14-20) and y -> -y (21-27) (equipment 12.8; CCW order kept).
	// Pocketless tables: 20 polygons (5 per quadrant: the two cushion tops meeting in a mitred valley on the
	// corner diagonal, long cap, corner square, end cap). Conventions:
	//  * Nose edges run through the virtual jaw points (the few mm^2 between a jaw arc and its virtual corner
	//    belong to the cushion top); Facing edges lie on the facing plan lines at height h (the undercut face's
	//    top edge is up to (RailTopZ - h) tan(beta_v) = 2.5 mm further into the opening; neglected) or, on a
	//    cap piece, on the stretch of the cushion-back line that borders an opening. Collinear split vertices
	//    (180 deg) separate edges of different kinds.
	//  * Cushion tops: plane through the nose line at h and the cushion-back line at RailTopZ (~13 deg);
	//    PlanePoint on the nose line. Caps: z = RailTopZ, normal +z.
	//  * Pocket: the pocket whose surround the piece belongs to (every piece of a pocket table has one: the
	//    pocket at its end of the rail); HasCut when that pocket's capture disc (liner cylinder r_p through the
	//    rail) meets the piece (BuildTableGeometry rejects a table where a piece would meet another pocket's
	//    disc). Cushion: the rail the piece belongs to (None for the corner squares).
	struct RailTopPolygon
	{
		RailTopKind Kind = RailTopKind::CushionTop;
		CushionId Cushion = CushionId::None; // rail the polygon belongs to (None for a pure pocket surround)
		PocketId Pocket = PocketId::None;    // pocket whose surround / cut this polygon borders (None otherwise)
		Vec3 PlanePoint;                     // a point on the plane [m]
		Vec3 PlaneNormal;                    // unit normal pointing away from the rail material (up / toward the table)
		int VertexCount = 0;                 // 3 .. kMaxRailTopVertices
		Vec2 Vertices[kMaxRailTopVertices];  // CCW plan vertices
		RailEdgeKind Edges[kMaxRailTopVertices] = {}; // edge i runs from Vertices[i] to Vertices[(i + 1) % VertexCount]
		bool HasCut = false;                 // pocket cut through the rail (liner cylinder r_p up to RailTopZ)
		Vec2 CutCenter;                      // = the pocket's CaptureCenter [m]
		double CutRadius = 0.0;              // = the pocket's CaptureRadius r_p [m]
	};

	struct Sight
	{
		Vec3 Position;        // center at z = RailTopZ [m]
		Vec2 NoseLinePoint;   // projected diamond on the cushion nose line (aiming systems)
		CushionId NearestCushion = CushionId::None;
		std::uint8_t Index = 0; // 1..7 along long rails (4 = side pocket, absent), 1..3 along end rails
		bool OnLongRail = false;
	};

	// Strings and spots (equipment 3.1, rules.md 2.2).
	struct TableLandmarks
	{
		double HeadStringX = 0.0;   // -L/4
		double FootStringX = 0.0;   // +L/4
		double CenterStringX = 0.0; // 0
		double LongStringY = 0.0;   // 0
		double BaulkX = 0.0;        // -L/2 + L/5 (Blackball)
		Vec2 HeadSpot;
		Vec2 FootSpot;
		Vec2 CenterSpot;
		double DiamondSpacing = 0.0; // L/8 (= W/4)
	};

	// Cushion + rail cross-section for mesh generation, in (d, z): d = distance behind the nose line
	// (into the rail) [m], z = height above the cloth [m]. Ordered from the cloth under the nose, up the
	// rubber face to the nose, along the cushion top to the cushion back, along the rail cap to the outer
	// edge. ESTIMATE shape (equipment 4.2 art profile); the physics uses only the nose line and the
	// RailTopPolygon planes, which lie on this polyline.
	struct CushionProfile
	{
		FixedVector<Vec2, kMaxProfilePoints> Points;
		int NoseIndex = -1;        // index of the nose point (0, h)
		int CushionBackIndex = -1; // index of (CushionWidth, RailTopZ)
	};

	struct TableGeometry
	{
		TableSpec Spec;
		double HalfLength = 0.0;
		double HalfWidth = 0.0;
		FixedVector<NoseSegment, kMaxNoseSegments> Noses;   // index = CushionId (always 6 entries; see NoseSegment::Present)
		FixedVector<JawArc, kMaxJaws> JawArcs;              // index = 2 * pocket + side (empty without pockets)
		FixedVector<Facing, kMaxJaws> Facings;              // index = 2 * pocket + side
		FixedVector<PocketGeometry, kMaxPockets> Pockets;   // index = PocketId
		FixedVector<RailTopPolygon, kMaxRailTopPolygons> RailTops; // physics rail-top surfaces AND the renderer's rail-top mesh
		FixedVector<Sight, kMaxSights> Sights;
		TableLandmarks Landmarks;
		CushionProfile Profile;
		Aabb2 PlayingArea;    // nose-line rectangle |x| <= L/2, |y| <= W/2
		Aabb2 OuterBoundary;  // |x| <= L/2 + RailWidthTotal, |y| <= W/2 + RailWidthTotal (leaving it = Floor)
	};

	// Scene objects that can stop a flying ball (physics-collisions 6.3). Not part of the table.
	struct EnvironmentSpec
	{
		double LampUndersideZ = kInfinity; // lamp underside height above the cloth [m]; WPA >= 1.016 (movable), bar 0.84
		Aabb2 LampFootprint{{-kInfinity, -kInfinity}, {kInfinity, kInfinity}}; // plan area covered by the lamp
	};

	// Ball-dependent cushion contact geometry (collisions 4.1, equipment 4.1, T-CUSH-2..5, C-G1).
	struct CushionContactGeometry
	{
		double SinTheta = 0.0;          // (h - R) / (R + r_n)
		double CosTheta = 1.0;
		double Theta = 0.0;             // theta_c [rad]
		double HorizontalOffset = 0.0;  // R_c = sqrt((R + r_n)^2 - (h - R)^2); = R in pooltoolCompat mode
	};

	// Builds everything above from the spec (one corner and one side pocket constructed with sign parameters,
	// so the table is exactly mirror-symmetric; T-GEOM-6). Sights: 18, counter-clockwise from the right rail's
	// head end (index 4 of the long rails = side pocket, absent also on pocketless tables). Profile: rubber
	// face bottom (0.4 CushionWidth, 0), nose (0, h), cushion back (CushionWidth, RailTopZ), outer edge
	// (RailWidthTotal, RailTopZ). Landmarks use the expressions of rules::MakeRulesTable (bitwise equal).
	// Returns ErrorCode::InvalidTable (Out = {Spec}, everything else empty) for a non-finite field,
	// L, W, h, CushionWidth, RailTopZ <= 0, RailWidthTotal <= CushionWidth, |Backdraft| or |LinerUndercut| >= pi/2
	// (a horizontal or flipped face), a cut angle outside (0, pi), jaw
	// rounding longer than a facing or nose, facings that meet before the cushion back, a drop-edge circle
	// that reaches a jaw point, a capture circle whose front point lies behind a facing or the rail or that lies
	// entirely inside the pocket opening (no back wall under the rail), or a cut disc that would reach a second
	// pocket's rail pieces.
	RB_API ErrorCode BuildTableGeometry(const TableSpec& Spec, TableGeometry& Out);

	// theta_c, R_c for a ball of radius R against a nose at height h with profile radius r_n (physics: 0).
	// |h - R| > R + r_n is clamped (sin = +-1, R_c = 0). PooltoolCompat: HorizontalOffset = R (equipment 12.2).
	// A non-finite input or R + r_n <= 0 returns the default (sin 0, cos 1, theta 0, offset 0).
	RB_API CushionContactGeometry ComputeCushionContact(double BallRadius, double NoseHeight, double NoseProfileRadius, bool PooltoolCompat);

	// Horizontal center-to-facing-plan-line distance at contact for a ball ON THE SHELF:
	// s_f = (R - (h - R) sin(beta_v)) / cos(beta_v)   (27.573 mm for 12 deg; collisions 5.3).
	RB_API double FacingContactOffset(double BallRadius, double NoseHeight, double Backdraft);

	// Throat width at depth t behind the noses (equipment 5.3): corner M - sqrt(2) t (cot phi - 1),
	// side M - 2 t tan(beta). T-POCKET-2..4.
	RB_API double CornerThroat(double Mouth, double CutAngle, double Depth);
	RB_API double SideThroat(double Mouth, double CutAngle, double Depth);

	// True if a ball center at P would lie over a pocket opening (beyond a mouth line or inside a
	// drop-edge circle) - used for cue-ball placement legality (rules F11). Strictly inside the a_d circle, or
	// beyond the mouth line (Dot(P - JawPoint[0], Axis) > 0) with its projection onto the mouth between the
	// jaw points: the same predicate as rules::OverPocketOpening on BuildRulesTable's pocket openings.
	RB_API bool IsOverPocketOpening(const TableGeometry& Geometry, const Vec2& P);

	// Closed CCW plan outline of the cushion noses at z = h (nose segments, jaw arcs sampled with
	// SamplesPerArc points, facings to the cushion back) for mesh generation. Returns the number of
	// points written, or -1 if Capacity is too small (0 for an unbuilt geometry). Per pocket P0..P5: the
	// incoming jaw arc from its nose tangent point to its facing tangent point (max(2, SamplesPerArc) points
	// incl. both ends; 1 point for a sharp jaw), the incoming facing's End, the outgoing facing's End, the
	// outgoing jaw arc from facing to nose; the nose segments join consecutive pockets and the last point joins
	// the first (not repeated). Pocketless: the 4 corners of the nose rectangle.
	RB_API int BuildNoseOutline(const TableGeometry& Geometry, int SamplesPerArc, Vec2* Out, int Capacity);

	// equipment T-GEOM-4: strictly above (toward the head rail); the string itself is not "above".
	constexpr bool IsAboveHeadString(double X, const TableLandmarks& Landmarks) { return X < Landmarks.HeadStringX; }
}
