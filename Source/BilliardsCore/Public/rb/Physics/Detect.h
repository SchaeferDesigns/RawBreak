#pragma once

// Event detection: earliest contact times of closed-form segments with other balls and with every
// table feature (physics-collisions 3, 4.10, 5.3, 6; prior-art 5.5, 5.7, 5.10).
// Owner: WP-5 (event detection).
//
// Contract for every Predict* function:
//  - Inputs are MotionSegments with their OWN time bases (T0); pairs are re-expanded to the common
//    origin t_ref = max(T0_a, T0_b) FROM THE STORED SEGMENT ORIGINS, never cumulatively (3.1).
//  - The search window is [t_ref, min(T0 + TauEnd of every segment involved, TimeLimit)]; roots
//    outside it are never reported (R8 "ghost collisions", ROB-13).
//  - Roots are isolated on the time-scaled polynomial with rb::SolveInInterval (derivative isolation +
//    safeguarded Newton; no closed-form quartic, no complex-root thresholds).
//  - Only APPROACHING crossings are accepted (gap' < 0). A pair touching at the start
//    (|gap| <= ContactTol) and approaching yields Time = t_ref with ContactFlags::AtStart; touching with
//    zero normal speed (|gap'| <= ApproachSpeedTol) and gap'' < 0 yields AtStart | Pressing (3.6, 4.10);
//    touching and separating yields no event; grazing double roots (|gap_min| <= eps_f) are misses.
//  - Overlap beyond OverlapGuard sets ContactFlags::Overlap (state corrupt: log, never move balls).
//  - Returned times are ABSOLUTE [s].

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Tolerances.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Aabb.h"
#include "rb/Math/Polynomial.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Cushion.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/PocketDrop.h"

#include <cstdint>

namespace rb
{
	namespace ContactFlags
	{
		inline constexpr std::uint8_t AtStart = 1u << 0;  // contact at the window start (touching and approaching)
		inline constexpr std::uint8_t Pressing = 1u << 1; // touching, zero normal speed, accelerating together -> CLI island
		inline constexpr std::uint8_t Overlap = 1u << 2;  // overlap > OverlapGuard at the window start (diagnostic)
	}

	struct ContactPrediction
	{
		bool Found = false;
		double Time = kInfinity;  // absolute [s]
		std::uint8_t Flags = 0;   // ContactFlags
	};

	// Model options that change contact distances or pocket handling (filled by the simulator from
	// PhysicsParams: CushionParams and PhysicsParams::Pockets).
	struct DetectOptions
	{
		double NoseProfileRadius = 0.0; // r_n (physics 0)
		bool PooltoolCompat = false;    // nose contact distance R instead of R_c
		PocketModel Pockets = PocketModel::GeometricLevelA; // CaptureCircle: pooltool circle pockets (XREF-01 only)
	};

	// ---------------------------------------------------------------------------------------------
	// Ball-ball (3.1-3.6, 7.4): |dC + dB tau + dA tau^2|^2 - (R1 + R2)^2, full 3D (airborne balls).
	// ---------------------------------------------------------------------------------------------

	// Gap polynomial f(tau), tau = t - RefTime, coefficients a0..a4 (3.2). Exposed for tests
	// (D-6 degenerate degrees, D-12 f''(0)) and for the tip re-contact.
	RB_API Polynomial BallBallGapPolynomial(const MotionSegment& A, double RadiusA, const MotionSegment& B, double RadiusB, double RefTime);

	RB_API ContactPrediction PredictBallBall(const MotionSegment& A, double RadiusA, const MotionSegment& B, double RadiusB, double TimeLimit,
		const NumericsConfig& Numerics);

	// Conservative broad phase (3.5, prior-art 5.10): swept AABB of the parabola over [TauFrom, TauTo]
	// (per-axis extremes are analytic); never culls by velocity direction (masse balls reverse).
	RB_API Aabb3 SweptBounds(const MotionSegment& Seg, double TauFrom, double TauTo);

	// ---------------------------------------------------------------------------------------------
	// Cushion noses and jaws (4.10). ContactOffset = R_c (or R in pooltoolCompat) from
	// ComputeCushionContact; airborne variants use the true 3D distance to the line / arc.
	// ---------------------------------------------------------------------------------------------
	RB_API ContactPrediction PredictNoseOnCloth(const MotionSegment& Seg, double Radius, const NoseSegment& Nose, double ContactOffset, double TimeLimit,
		const NumericsConfig& Numerics);
	RB_API ContactPrediction PredictNoseAirborne(const MotionSegment& Seg, double Radius, const NoseSegment& Nose, double NoseProfileRadius, double TimeLimit,
		const NumericsConfig& Numerics);
	RB_API ContactPrediction PredictJawArcOnCloth(const MotionSegment& Seg, double Radius, const JawArc& Arc, double ContactOffset, double TimeLimit,
		const NumericsConfig& Numerics);
	// Sphere approximation of the arc (|p - O| = R + r_j, quartic; error ~0.13 mm) or exact degree 8.
	RB_API ContactPrediction PredictJawArcAirborne(const MotionSegment& Seg, double Radius, const JawArc& Arc, double TimeLimit, const NumericsConfig& Numerics);

	// ---------------------------------------------------------------------------------------------
	// Pocket elements (5.3). Facing on the shelf: center-to-plan-line distance s_f (FacingContactOffset).
	// ---------------------------------------------------------------------------------------------
	RB_API ContactPrediction PredictFacingOnShelf(const MotionSegment& Seg, double Radius, const Facing& Face, double ContactOffset, double TimeLimit,
		const NumericsConfig& Numerics);
	RB_API ContactPrediction PredictFacingAirborne(const MotionSegment& Seg, double Radius, const Facing& Face, double TimeLimit, const NumericsConfig& Numerics);
	RB_API ContactPrediction PredictFacingTopEdge(const MotionSegment& Seg, double Radius, const Facing& Face, double TimeLimit, const NumericsConfig& Numerics);
	// Center reaches horizontal distance a_d from C_cap, moving inward, within the front arc (DropEdge).
	RB_API ContactPrediction PredictDropEdge(const MotionSegment& Seg, double Radius, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics);
	// Hole wall / liner / back wall from inside: horizontal distance r_p - R moving outward. The wall exists
	// on the FRONT arc only for contact points z < -r_d and on the rest of the circle up to WallTopZ
	// (RailTopZ): a ball flying across the pocket above the cloth meets the back wall (8.9).
	RB_API ContactPrediction PredictLinerWall(const MotionSegment& Seg, double Radius, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics);
	// Rounded rim from inside (torus: major a_d, minor r_d, center height -r_d): degree 8 in tau.
	RB_API ContactPrediction PredictRimTorus(const MotionSegment& Seg, double Radius, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics);
	// pooltool circle pocket (PocketModel::CaptureCircle, XREF-01): the CENTER enters the circle
	// (CaptureCenter, CaptureRadius) -> BallPocketEnter + BallPocketed at once (collisions 5.2).
	RB_API ContactPrediction PredictCaptureCircle(const MotionSegment& Seg, const PocketGeometry& Pocket, double TimeLimit, const NumericsConfig& Numerics);
	// z(tau) = -R: the ball is entirely below the slate top -> BallPocketed.
	RB_API ContactPrediction PredictCaptureDepth(const MotionSegment& Seg, double Radius, double TimeLimit, const NumericsConfig& Numerics);
	// Center back outside the drop-edge circle with z > R ("rattled out", BallPocketExit).
	RB_API ContactPrediction PredictPocketExit(const MotionSegment& Seg, double Radius, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics);

	// ---------------------------------------------------------------------------------------------
	// Slate, rail top, leaving the table (C.2, 6.1-6.3)
	// ---------------------------------------------------------------------------------------------
	// Landing (z = R, later root). Owned by the END slot only (Airborne TauEnd); PredictTableEvent never
	// returns SlateLanding, so there is exactly one live landing entry per ball.
	RB_API ContactPrediction PredictSlateLanding(const MotionSegment& Seg, double Radius, double TimeLimit);
	// Plane contact n . (p - x0) = R (quadratic), accepted only where the contact point (center minus R n)
	// lies over the polygon and outside its cut disc.
	RB_API ContactPrediction PredictRailTop(const MotionSegment& Seg, double Radius, const RailTopPolygon& Polygon, double TimeLimit,
		const NumericsConfig& Numerics);
	// Convex edges of a rail-top polygon hit by an airborne ball: straight edges of kind CushionBack /
	// OuterEdge / Facing as the airborne nose (quartic); the pocket-cut rim (circle r_p at z = RailTopZ)
	// as a circle contact (degree 8, same solver as the rim torus). Edge = straight edge index, or
	// kCutRimEdge for the cut rim.
	inline constexpr int kCutRimEdge = 0xFE;
	RB_API ContactPrediction PredictRailTopEdge(const MotionSegment& Seg, double Radius, const RailTopPolygon& Polygon, int Edge, double TimeLimit,
		const NumericsConfig& Numerics);
	// A ball rolling / sliding ON the flat rail cap (Seg.SupportZ = RailTopZ): first time its CENTER leaves
	// the polygon across a straight edge or enters the cut disc. EdgeOut = edge index (see
	// RailTopPolygon::Edges for what lies beyond; Seam = continue on the neighbouring polygon) or kCutRimEdge.
	RB_API ContactPrediction PredictSupportExit(const MotionSegment& Seg, const RailTopPolygon& Polygon, double TimeLimit, int& EdgeOut);
	RB_API ContactPrediction PredictOuterBoundary(const MotionSegment& Seg, const Aabb2& Outer, double TimeLimit);
	// Analytic apex z_max = z0 + v_z0^2 / (2g) at tau = v_z0 / g; event at the apex if z_max + R >= lamp
	// underside and the apex lies over the lamp footprint (6.3).
	RB_API ContactPrediction PredictLampApex(const MotionSegment& Seg, double Radius, const EnvironmentSpec& Environment, double Gravity, double TimeLimit);

	// ---------------------------------------------------------------------------------------------
	// Rules observers (no state change): center crosses a string / spot line by more than Eps.
	// ---------------------------------------------------------------------------------------------
	struct LineCrossing
	{
		double Time = 0.0;              // absolute [s]
		TableLine Line = TableLine::HeadString;
		std::int8_t Direction = 0;      // +1 toward +x (+y for LongString), -1 the other way
	};

	// All crossings in (TimeFrom, TimeLimit] sorted by (Time, Line). Returns the count (<= Capacity).
	// IncludeFrom = true searches [TimeFrom, ...]: a segment that starts exactly on line +- Eps and moves
	// beyond counts (the observer at the time of an event that replaced the segment, architecture 8.7).
	RB_API int PredictLineCrossings(const MotionSegment& Seg, const TableLandmarks& Landmarks, double TimeFrom, double TimeLimit, double Eps, bool IncludeFrom,
		LineCrossing* Out, int Capacity);

	// Jump-over observer (rules F9 JumpedOver, Blackball): first time in the window at which the PLAN
	// (horizontal) center distance of A and B crosses RadiusA + RadiusB, entering (Entering = true) or
	// leaving. The simulator evaluates it only while A is airborne and emits BallJumpedOver(A, B) when an
	// entered plan overlap is left without a BallBall(A, B) contact in between.
	RB_API ContactPrediction PredictPlanDistanceCrossing(const MotionSegment& A, double RadiusA, const MotionSegment& B, double RadiusB, bool Entering,
		double TimeLimit, const NumericsConfig& Numerics);

	// ---------------------------------------------------------------------------------------------
	// Table dispatcher used by the simulator: earliest event of ONE ball against the table.
	// ---------------------------------------------------------------------------------------------
	enum class TableFeatureKind : std::uint8_t
	{
		None,
		NoseSegment,   // Index = CushionId
		JawArc,        // Index = 2 * pocket + side
		FacingFace,    // Index = 2 * pocket + side
		FacingTopEdge, // Index = 2 * pocket + side
		DropEdge,      // Index = PocketId
		LinerWall,     // Index = PocketId
		RimTorus,      // Index = PocketId
		CaptureDepth,  // Index = PocketId
		PocketExit,    // Index = PocketId
		SlateLanding,  // Index = 0 (end slot only; never returned by PredictTableEvent)
		RailTop,       // Index = rail-top polygon (plane contact of an airborne ball)
		RailTopEdge,   // Index = rail-top polygon, SubIndex = edge or kCutRimEdge
		SupportExit,   // Index = rail-top polygon, SubIndex = edge or kCutRimEdge (ball on the flat cap leaves it)
		CaptureCircle, // Index = PocketId (PocketModel::CaptureCircle only)
		OuterBoundary, // Index = 0 (-> BallOffTable Floor)
		LampApex,      // Index = 0 (-> BallExternalContact Lamp + BallOffTable ExternalObjectRebound)
	};

	struct TableFeatureRef
	{
		TableFeatureKind Kind = TableFeatureKind::None;
		std::uint8_t Index = 0;
		std::uint8_t SubIndex = 0;
	};

	enum class SupportKind : std::uint8_t
	{
		Cloth,   // cloth / slate / shelf (z = 0)
		RailCap, // flat rail cap (z = RailTopZ); SupportPolygon = RailTopPolygon index
	};

	// Where the ball is, for choosing the applicable features (the simulator's per-ball context).
	// There is no "over the rail" flag: applicability of every feature is decided by its own geometric
	// validity region along the segment (nose lines: table side n_c . q >= 0; rail-top planes: contact
	// point over the polygon; pocket walls: swept bounds reach the a_d circle; ...).
	struct BallTableContext
	{
		PocketId Pocket = PocketId::None;     // pocket the ball is in (PocketPivot/PocketFall) or entering
		SupportKind Support = SupportKind::Cloth; // surface states only
		std::uint8_t SupportPolygon = 0;      // RailCap: current RailTopPolygon index
	};

	struct FeaturePrediction
	{
		ContactPrediction Contact;
		TableFeatureRef Feature;
	};

	// Earliest applicable feature event (ties by (Kind, Index, SubIndex) ascending). Applicability by state
	// AND position (every candidate is first culled by SweptBounds against the feature's bounds):
	//  * surface state on the cloth: noses, jaw arcs, facing faces (shelf), drop edges (GeometricLevelA)
	//    or capture circles (CaptureCircle);
	//  * surface state on the flat rail cap: SupportExit of the current polygon, OuterBoundary;
	//  * Airborne: airborne nose / jaw-arc / facing face / facing top edge variants, rail-top planes and
	//    rail-top edges, OuterBoundary, LampApex, and for EVERY pocket whose a_d cylinder the swept bounds
	//    reach: the rim torus and the liner / back wall (height rules of PredictLinerWall). NOT the
	//    landing (end slot);
	//  * PocketPivot (Seg = PivotDetectionProxy): facing faces and top edges and jaw arcs of Context.Pocket
	//    (airborne predictors); other balls are pair slots;
	//  * PocketFall: facings, arcs, liner, rim torus, capture depth, pocket exit of Context.Pocket.
	RB_API FeaturePrediction PredictTableEvent(const MotionSegment& Seg, const BallSpec& Spec, const BallTableContext& Context, const TableGeometry& Table,
		const EnvironmentSpec& Environment, const DetectOptions& Options, double Gravity, double TimeLimit, const NumericsConfig& Numerics);

	// Island feature joining (architecture 8.8): every table element whose bounds intersect Region
	// (the island bodies' reach over the next steps), in (Kind, Index, SubIndex) order. Returns the
	// count written (<= Capacity); sets Overflow if more exist.
	RB_API int QueryTableFeatures(const Aabb3& Region, const TableGeometry& Table, const DetectOptions& Options, TableFeatureRef* Out, int Capacity,
		bool& Overflow);

	// Contact frame for the resolution dispatcher (ResolveFixedContact) at the event state.
	RB_API FixedContact MakeFixedContact(const TableFeatureRef& Feature, const TableGeometry& Table, const BallState& Ball, const BallSpec& Spec,
		const DetectOptions& Options);
}
