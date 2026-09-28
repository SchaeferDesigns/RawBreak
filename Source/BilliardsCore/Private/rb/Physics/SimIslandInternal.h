#pragma once

// WP-6b PRIVATE helpers (islands, pockets & rail-top routing; Docs/architecture.md 8.8, 8.9, 8.11). Shared by
// SimIsland*.cpp, SimPocket*.cpp, SimRailTop*.cpp and the WP-6b tests (Tests/Core/Islands, Tests/Core/PocketFlow).
// Not part of any contract: the loop (WP-6a) only sees the hooks of SimInternal.h.
//
// Conventions WP-6b relies on (the private loop <-> 6b interface, documented in SimInternal.h terms):
//  * Tracks (RecordOptions::Trajectories): Result->Tracks[i].Segments.back() is the ball's OPEN segment (T1 = kInfinity while
//    open) and BallSlot::Orientation0 its start orientation. ReplaceSegment(t) closes back() (T1 = t) and continues the
//    orientation with SegmentOrientationAt(back().Orientation0, back(), t - back().Motion.T0), which is exact for the
//    Sampled segments 6b leaves open (islands, pivots). For the same continuation from BallSlot::Seg (Trajectories off,
//    ChalkCling), 6b sets Seg to the constant-spin equivalent of the open Sampled piece before calling ReplaceSegment.
//  * ReplaceSegment derives the support from BallSlot::Context (Support == RailCap: SupportZ = Spec.RailTopZ with
//    RailCapSurface(Params.PocketContacts); otherwise SupportZ = 0 with Params.Cloth) and, for State == PocketPivot,
//    uses PivotDetectionProxy(BallSlot::Pivot) (6b sets Pivot first). ClassifyState keeps pocket states, so a caller never
//    passes a surface / airborne state for a ball inside a pocket (6b sets PocketFall explicitly).
//  * The end slot of a PocketPivot segment (Seg.TauEnd = T_p) is dispatched as ProcessPocketEvent(Ball, {None, pocket, 0}).
//  * ReplaceSegment re-predicts the tip slots of every moving, non-island cue (a tip path that 6b changed when the tip left an
//    island is re-predicted with the first member that leaves).
//  * MakeTerminal logs nothing: 6b emits BallPocketed / BallOffTable itself before calling it.
//  * The Zeno detector is the loop's (architecture 9 guard 5): the loop logs ZenoGuard (and counts ZenoTriggers) when it hands a
//    Zeno seed to StartIsland; 6b does not log it again (IslandSeed::Zeno only documents the seed).

#include "SimInternal.h"

#include "rb/Geometry/TableGeometry.h"

#include <cstdint>

namespace rb::sim
{
	// ---------------------------------------------------------------------------------------------
	// Island features (architecture 8.8, review item 26): bounded IslandFeatures from the table geometry.
	// ---------------------------------------------------------------------------------------------

	// A table element becomes at most two island features (a straight rail-top edge whose middle lies over the pocket cut
	// disc, a cut rim whose arc on the polygon is split in two, or a facing's back-end edge, WP-10: its lower and upper
	// segments). The second piece carries SourceSub | kSecondPieceBit.
	inline constexpr int kMaxFeaturePieces = 2;
	inline constexpr std::uint8_t kSecondPieceBit = 0x10; // edge indices are < kMaxRailTopVertices (8); kCutRimEdge = 0xFE

	// Converts a table feature (rb/Physics/Detect.h) into bounded island features with the contact parameters of
	// PhysicsParams: noses and jaw arcs (e_c law, mu_w), facing faces and top edges (k_f e_c, mu_f), rail-top planes, their
	// physical straight edges (RailTopEdgePieces) and the cut rim of a FLAT rail-top polygon (constant e_rt, mu_rt, rail-top
	// rolling). Returns the number written (0: not a contact feature of islands: drop edges, capture circles, region events,
	// the cut rim of a sloped cushion top, whose members leave the island over the cut instead).
	int MakeIslandFeatures(const TableFeatureRef& Ref, const TableGeometry& Table, const PhysicsParams& Params, IslandFeature Out[kMaxFeaturePieces]);

	// The table feature an island feature was made from (the piece bit removed).
	TableFeatureRef SourceOf(const IslandFeature& Feature);

	// Signed gap [m] between a sphere (center P, radius R) and an island feature, with the solver's contact geometry
	// (rb/Physics/Compliant.h); kInfinity where the feature cannot touch the sphere (outside its valid region). Normal (optional):
	// the unit contact normal from the feature toward the center.
	double IslandFeatureGap(const IslandFeature& Feature, const Vec3& P, double R, Vec3* Normal = nullptr);

	// ---------------------------------------------------------------------------------------------
	// Landing routing and pocket geometry (collisions 5.3, 6.1)
	// ---------------------------------------------------------------------------------------------
	enum class LandingSurface : std::uint8_t
	{
		PlayingSurface, // inside the nose-line rectangle: slate impact (motion C.3 / C.4)
		Shelf,          // pocket opening beyond the mouth line, outside the drop-edge circle: slate impact
		PocketHole,     // inside a capture circle, or in the rounded annulus r_p < rho < a_d on the front arc: PocketFall
		OverRail,       // behind a nose line: a rail-top / cushion event must have fired first (missed event)
	};

	// Where an airborne ball whose center reaches z = R at plan point P comes down (collisions 6.1, in order: pocket hole,
	// playing surface, shelf, rail). Pocket = the pocket for PocketHole / Shelf, -1 otherwise. CaptureCircle pockets
	// (XREF-01) use the capture circle r_p only.
	LandingSurface ClassifyLandingPoint(const TableGeometry& Table, PocketModel Model, const Vec2& P, int& Pocket);

	// True if plan point P lies on the front (table-side) arc sector of the pocket (angle about the capture center within
	// [FrontArcFrom, FrontArcFrom + FrontArcSweep]).
	bool OnFrontArc(const PocketGeometry& Pocket, const Vec2& P);

	// Pocket whose drop-edge circle a_d (GeometricLevelA) or capture circle r_p (CaptureCircle) contains P strictly, -1 if none.
	int PocketContaining(const TableGeometry& Table, PocketModel Model, const Vec2& P);

	// Wall-like contacts inside a pocket (liner / back wall, the rim torus where it is steeper than 45 deg; collisions 5.3) of a ball
	// running around the inside of the hole (DECISION, WP-6b review): Level A has no sustained contact there (the pocket interior is no
	// island feature), so a ball sliding along the curved wall is a chain of grazing micro-contacts. With the separation of a grazing
	// contact (e_l v_n, at least v_rest) the straight flight re-meets the wall after 2 v_n rho / v_t^2, i.e. every 2 atan(v_n / v_t) of
	// arc: thousands of BallLiner events for one pocketed ball (1.9e5 per second at v_t = 5 m/s; a jump or a fast ball along a jaw
	// reached the 20 000 event cap). The ball therefore leaves every wall-like contact at least kPocketWallMinExitAngle into the hole
	// from the tangent of its circle about the pocket axis: the path around the wall becomes a polygon with at most
	// 2 pi / (2 kPocketWallMinExitAngle) = 36 contacts per turn, 0.13 mm inside the circle at most (rho (1 - cos 5 deg)). This is the
	// continuum limit of the sustained contact: the normal impulse per turn is 2 pi m v_t as for the continuous normal force
	// m v_t^2 / rho (so the Coulomb losses of the GRI friction match), and the normal velocity returns with no energy loss (the
	// horizontal velocity is turned, its length kept).
	inline constexpr double kPocketWallMinExitAngle = 5.0 * kDegToRad;

	// The horizontal velocity of a ball at Position leaving a wall-like contact inside Pocket, turned (its length kept, Velocity.z
	// unchanged) so that it points into the hole by kPocketWallMinExitAngle from the tangent of the circle about the pocket axis through
	// the center, if it is closer to that tangent than the angle (either side); otherwise Velocity unchanged (a ball thrown back
	// across the hole or out of it keeps its direction).
	Vec3 TurnOffPocketWall(const PocketGeometry& Pocket, const Vec3& Position, const Vec3& Velocity);

	// ---------------------------------------------------------------------------------------------
	// Rail top (collisions 6.2)
	// ---------------------------------------------------------------------------------------------

	// Index of the rail-top polygon whose plan region contains P (outside its cut disc; boundary within Slack counts), of the
	// given kind unless AnyKind; -1 if none. Ties (P on a shared seam) go to the polygon the plan direction Ahead points
	// into (a zero Ahead: the lower index).
	int FindRailTopPolygon(const TableGeometry& Table, const Vec2& P, bool AnyKind, RailTopKind Kind, const Vec2& Ahead, double Slack);

	// Height of the plane of a rail-top polygon at plan point P [m].
	double RailTopHeightAt(const RailTopPolygon& Polygon, const Vec2& P);

	// True if a rebound with velocity V off a contact with unit normal Normal (from the contact to the center) rises less than
	// MinHeight along the normal against gravity (the collisions 6.2 "bounce height < h_min" test). Contacts whose normal does not
	// point up (Normal.z <= 0.2) never count as low bounces.
	bool IsLowRebound(const Vec3& V, const Vec3& Normal, double Gravity, double MinHeight);

	// ---------------------------------------------------------------------------------------------
	// Island internals shared by the pocket and rail-top routing
	// ---------------------------------------------------------------------------------------------

	// StartIsland with an explicit state for the seed ball BallA (the post-impact state of a rail-top contact; the other
	// members come from BallStateForEvent as in StartIsland).
	void StartIslandWithState(Workspace& Ws, const IslandSeed& Seed, double Time, const BallState& SeedState);

	// Pocket entry over the drop edge (collisions 5.4) of a ball whose center is on (or, leaving an island, just inside) the
	// drop-edge circle: BallPocketEnter, then PocketPivot (pivot macro-step) or PocketFall (immediate leave).
	void EnterPocketOverDropEdge(Workspace& Ws, int Ball, const BallState& State, int Pocket, double Time);

	// Pooltool circle pocket (PocketModel::CaptureCircle, XREF-01): BallPocketEnter + BallPocketed at once.
	void CaptureInCircle(Workspace& Ws, int Ball, const BallState& State, int Pocket, double Time);

	// Ball comes to rest on the rail (flat cap or slope) -> BallOffTable(RestsOnRailOrFrame) + terminal (WPA 2.6).
	void RestOnRail(Workspace& Ws, int Ball, const BallState& State, double Time);

	// Continues a ball on the flat rail cap: surface segment with SupportZ = RailTopZ on polygon Polygon.
	void ContinueOnCap(Workspace& Ws, int Ball, BallState State, int Polygon, double Time);

	// ---------------------------------------------------------------------------------------------
	// Events and recording helpers (architecture 8.7, 8.10)
	// ---------------------------------------------------------------------------------------------

	// A ShotEvent of one ball with Pre / Post = State (callers overwrite what changes).
	ShotEvent MakeBallEvent(ShotEventType Type, double Time, int Ball, const BallState& State);

	// The struck ball of a strike touched a rail-like element (cushion, jaw, rail top, liner): TipSlot::StruckTouchedOther.
	void NoteRailContact(Workspace& Ws, int Ball);

	// Emits the pending observers of an event-mode ball with Time < Before (line crossings, jump-over) and clears its list
	// (a ball leaving event mode for an island). FreezeLeave observers clear their freeze bits.
	void FlushObservers(Workspace& Ws, int Ball, double Before);

	// Leaves the analytic segment of a ball at time T (the open track segment is closed at T) and opens a Sampled track
	// segment (island member / pivot) starting from State. Returns nothing; maintains BallSlot::Orientation0 / Sample*.
	void BeginSampledTrack(Workspace& Ws, int Ball, const BallState& State, double T);

	// Updates the open Sampled segment with the body state at T (EndPosition, mean spin) and cuts a new sample when linear
	// interpolation would deviate by more than SampleTolerance, after SampleMaxInterval, or when Force (contact begin).
	void UpdateSampledTrack(Workspace& Ws, int Ball, const Vec3& Position, const Vec3& Velocity, const Vec3& Omega, double Dt, double T, bool Force);

	// Closes the open Sampled piece at T (EndPosition = Position) and prepares BallSlot::Seg / Orientation0 so that
	// ReplaceSegment continues the orientation law exactly from it.
	void EndSampledTrack(Workspace& Ws, int Ball, const Vec3& Position, double T);

	// Pivot playback: replaces the analytic proxy segment of the pivot [Pivot.T0, T] by adaptive Sampled pieces of the true
	// path (EvaluatePivot), leaving the last one open for ReplaceSegment.
	void SamplePivotTrack(Workspace& Ws, int Ball, double T);

	// Removes the Zeno histories of every pair of the given balls and of their ball-feature slots (architecture 8.8
	// re-entry guard: cleared at island start and exit).
	void ClearZenoHistories(Workspace& Ws, std::uint32_t Balls);
}
