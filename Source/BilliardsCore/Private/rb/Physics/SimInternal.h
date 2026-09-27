#pragma once

// PRIVATE interface between the simulator core loop and the island / pocket / rail-top routing
// (Docs/architecture.md 8, 17). Not part of the public API; never include it from a public header.
//
//   WP-6a (owner of this header): Simulator.cpp, SimLoop*.cpp - validation, strikes, queue, slots,
//         versions, per-ball time bases, event dispatch, transitions, ball-ball / cushion impulses,
//         observers, tip slots, recording, guards (event cap, horizon, island budget).
//   WP-6b: SimIsland*.cpp, SimPocket*.cpp, SimRailTop*.cpp - island start / stepping / joining (balls AND
//         table features) / member exits / rigid switch / tips inside islands / observers inside
//         islands / sampled recording; pocket state machine; landing routing; rail-top routing.
// Changes to this header need sign-off from both packages.
//
// Contract details of the WP-6a implementation (v1 of the loop; every item below is ADDITIVE to the frozen v1.2 header, no
// declaration of the frozen part changed):
//  * Tracks. With RecordOptions::Trajectories, Result->Tracks[b].Segments.back() is the ball's OPEN segment (T1 = +inf) while
//    the ball is in event mode; its Orientation0 is the orientation at its Motion.T0. BallSlot::Orientation0 is the
//    orientation at Seg.T0 in event mode (maintained with trajectories, and for every marked ball with ChalkCling); inside an
//    island it is the orientation at SampleStart (WP-6b). ReplaceSegment / MakeTerminal close the open track segment at Time:
//    an open Analytic segment gets T1 = Time, an open Sampled segment (left open by an island) gets T1 = Time and
//    EndPosition = the new state's position; a segment that is already closed (T1 < +inf, e.g. the last Sampled piece an island
//    wrote) is kept and the next Orientation0 continues from its T1. Next Orientation0 = SegmentOrientationAt of the closed
//    segment (rb/Physics/Playback.h), so segment boundaries stay bitwise continuous.
//  * Leaving an island (review addition). Call ReplaceSegment / MakeTerminal for a leaving member while its BallSlot::InIsland is
//    still true: the loop then closes the open Sampled segment with Motion.Omega0 = RotationAccumulator / (Time - its T0), and
//    without a recorded track (Trajectories off: a marked ball with ChalkCling, the Finals orientation) it advances
//    Orientation0 (the orientation at SampleStart) by the Sampled law with RotationAccumulator / (Time - SampleStart) - bitwise
//    the recorded result. So the island keeps SampleStart / RotationAccumulator for every member (BallSlot) and needs no
//    orientation bookkeeping at the exit.
//  * AdvanceIsland (review addition): return right after a step in which a member left the island (ReplaceSegment /
//    MakeTerminal), so that the loop processes the leaving ball's new events and observers in time order before the next step
//    (8.8 "before each step, all valid non-island events with Time <= step end are processed first"); Ws.Now is the time of the
//    step being processed, never later than the Time passed to ReplaceSegment (the tip slots are searched from Ws.Now on).
//  * ReplaceSegment(Ws, b, S, t) returns the ball to event mode (InIsland = false), bumps its version and re-predicts all its
//    slots (end, table, pairs, and every moving tip). It does NOT log BallAirborne / BallLand / MotionTransition (callers do).
//    S.State == PocketPivot builds the pivot from Balls[b].Context.Pocket (MakePivotPath at Time; an immediate pivot becomes
//    PocketFall with PivotLeaveState); the pivot is recorded as adaptive Sampled pieces when it is closed. The support plane
//    (SupportZ, friction) of the new segment follows Balls[b].Context.Support (RailCap: TableSpec::RailTopZ and
//    RailCapSurface; otherwise the cloth).
//  * MakeTerminal does not log BallPocketed / BallOffTable (callers do); it closes the track, writes the Terminal segment and
//    the Finals entry, and removes the ball from detection. It takes the position from the island body when the ball is still
//    an island member.
//  * The end slot of a PocketPivot segment (pivot end, tier Transition) is dispatched as
//    ProcessPocketEvent(Ws, b, TableFeatureRef{TableFeatureKind::None, pocket, 0}, t).
//  * LinerWall and RimTorus table events go to ProcessPocketEvent first; if it returns false the loop resolves them itself as
//    GRI contacts (architecture 8.5 "cushion-like" row: BallLiner / BallPocketRim). DropEdge, CaptureDepth, PocketExit,
//    CaptureCircle and the pivot end are WP-6b's only (not consumed = nothing happens). RailTop, RailTopEdge and SupportExit
//    go to ProcessRailTopEvent only; the loop itself turns a Stationary transition on the flat cap into
//    OffTable(RestsOnRailOrFrame) (architecture 8.5 Transition row).
//  * Exactly simultaneous IMPULSE contacts (ball-ball, tip, and the cushion-like / rail-top table contacts) sharing a ball form
//    one group (architecture 8.3): the loop calls StartIsland once per group member, in queue-key order, at the same Time (the
//    first call starts the island, the following ones must merge into it). Region events (drop edge, capture, pocket exit,
//    support exit, boundary, lamp) carry no impulse and never form a group, like the transition tier.
//  * Every StartIsland call made by the loop increments SimDiagnostics::IslandHandOffs (and PressingContacts for pressing
//    seeds, ZenoTriggers for Zeno seeds); SimDiagnostics::Islands (new islands) and IslandSteps are WP-6b's (the loop copies
//    Workspace::IslandStepsUsed into IslandSteps if that is larger).
//  * Tip slots (review addition) are searched from Ws.Now on, never from the path's older StartTime: a re-contact that
//    ResolveTipRecontact answered with no impulse is not found again by a later re-prediction (the tip passes on). Tip slots are
//    re-predicted by ReplaceSegment / MakeTerminal (every moving tip that is not InIsland), so an island that releases a tip
//    (TipSlot::InIsland = false, new Path) does so before its last member's ReplaceSegment.
//  * The strike's own tip contact (integration round 2, cross-package fix WP-1 / WP-6a). StrikeCueBall resolves the whole
//    tip-ball interaction of the stroke at t = 0 incl. the slate reaction (sequential for jump cues, pinched for masse; MOT B.8.1,
//    B.8.3: "handled inside the strike"). The follow-through dome starts touching the struck ball, and after an elevated stroke
//    the slate rebound drives the ball back into it at once: a TipRecontact at t = 0 would resolve the same interaction twice (it
//    took a 4 m/s, 50 deg jump shot from the 21 cm hop of MOT T-B14 down to 4 cm). While TipSlot::StrikeContactOpen, the tip slot
//    therefore searches the struck ball only from the time the dome and the ball separate (first up-crossing of their gap); the
//    flag clears once they are seen apart, or at a genuine later re-contact (a double hit).
//  * Pair observers (jump-over, ball freeze-leave) of an event-mode ball against a partner inside an island are void (never
//    emitted); the partner's return to event mode recomputes them. The island evaluates its members' own crossings (8.7).
//  * An open event-mode tip contact (TipSlot::ContactBall != kNoBall, pending TipContactEnd at TipSlot::ContactEnd) whose tip
//    becomes an island participant (TipSlot::InIsland) is continued and closed by the island (TipContactEnd, ContactBall :=
//    kNoBall); the loop emits pending ends only for tips that are not InIsland.
//  * Record positions (review addition): every record-relevant event (IsRecordRelevant) passed to EmitEvent carries Pre[0] = A's
//    state at the event (and Pre[1] = B's when B is a ball) WHATEVER RecordOptions::EventStates - the record takes PositionA /
//    PositionB from them (the rules' FinalPosition of a pocketed, off-table or late-dropping ball is the last record position,
//    rules F13). EmitEvent strips the states from the physics-log copy when EventStates is off. This holds for WP-6b's events too
//    (BallLand, BallPocketEnter / Rim / Exit / Pocketed, BallLiner, BallRailTop, island records, tip events of islands).
//  * EmitEvent maintains the loop's bookkeeping from the events themselves, so island records count as well: jump-over
//    contacts (BallBall of a pending plan overlap) and TipSlot::StruckTouchedOther (a BallBall of the struck ball with a ball
//    other than FrozenTarget, or a BallCushion / BallJaw / BallRailTop / BallLiner of the struck ball).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/FixedVector.h"
#include "rb/Math/Aabb.h"
#include "rb/Math/Quat.h"
#include "rb/Physics/Compliant.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Detect.h"
#include "rb/Physics/EventQueue.h"
#include "rb/Physics/PocketDrop.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"

#include <cstdint>

namespace rb::sim
{
	inline constexpr int kMaxObserversPerBall = 16;
	inline constexpr int kZenoHistoryLength = 8;
	inline constexpr int kMaxZenoEntries = 96;

	// Observer = rules-only crossing on the current segment (no state change, architecture 8.7).
	enum class ObserverKind : std::uint8_t
	{
		LineCross,   // Index = TableLine, Direction = +-1
		FreezeLeave, // Index = rail feature or 32 + ball: the ball left an initial freeze by LeaveDistance
		JumpEnter,   // Index = other ball: plan overlap entered while airborne
		JumpLeave,   // Index = other ball: plan overlap left (emit BallJumpedOver unless a contact happened)
	};

	struct Observer
	{
		double Time = 0.0;
		ObserverKind Kind = ObserverKind::LineCross;
		std::uint8_t Index = 0;
		std::int8_t Direction = 0;
	};

	// Per-ball workspace (architecture 8.1).
	struct BallSlot
	{
		bool InPlay = false;
		bool InIsland = false;              // integrated by the island (no analytic predictions)
		MotionSegment Seg;                  // current closed-form segment (own time base Seg.T0)
		std::uint32_t Version = 0;          // bumped whenever Seg is replaced; queued entries carry it
		BallTableContext Context;           // pocket, support surface / polygon
		PivotPath Pivot;                    // PocketPivot only
		int BounceIndex = 0;                // current airborne sequence (N_max)
		double SequenceMaxZ = 0.0;          // max center height of the airborne sequence (BallLand)
		std::uint32_t InitialFreezeRails = 0; // rail features frozen at t = 0 and not yet left by LeaveDistance
		std::uint32_t InitialFreezeBalls = 0; // balls frozen at t = 0 and not yet left by LeaveDistance
		std::uint32_t JumpPending = 0;      // plan overlaps entered while airborne (bit = other ball)
		std::uint32_t ContactSinceJump = 0; // BallBall contacts during those overlaps
		FixedVector<Observer, kMaxObserversPerBall> Observers; // pending observers of Seg, time ordered
		Quat Orientation0;                  // orientation at Seg.T0 (orientation law, rb/Physics/Playback.h); maintained for
		                                    //   every ball with chalk marks when PhysicsParams::ChalkCling, whatever the RecordOptions
		double SampleStart = 0.0;           // island / pivot: start of the open Sampled segment
		Vec3 SamplePosition;                // ... its start position
		Vec3 RotationAccumulator;           // ... integral of w since SampleStart (Sampled Omega0 = this / duration)
	};

	// Zeno detector (guard 5): contact times of one ball-ball pair or ball-feature slot.
	struct ZenoEntry
	{
		std::uint8_t BallA = 0;
		std::uint8_t BallB = kNoBallSlot;   // kNoBallSlot for ball-feature slots
		std::uint8_t FeatureKind = 0;
		std::uint8_t FeatureIndex = 0;
		std::uint8_t FeatureSub = 0;
		int Count = 0;
		double Times[kZenoHistoryLength] = {};
	};

	// Follow-through cue tip of one strike (architecture 8.6).
	struct TipSlot
	{
		bool Moving = false;                // until Path.StopTime
		bool InIsland = false;              // integrated by the island (IslandTip)
		CueTipPath Path;
		std::uint32_t Version = 0;          // bumped when Path changes (tip slot entries carry it)
		BallId ContactBall = kNoBall;       // open tip contact interval (TipContactBegin logged)
		BallId FrozenTarget = kNoBall;      // f: frozen ball the stroke goes into (rules F7)
		bool StruckTouchedOther = false;    // struck ball touched a ball other than f or a rail
		int OpenCueTipSegment = -1;         // index into Result.CueTips of the open piece
		double ContactEnd = 0.0;            // (v1 addition) event mode: time of the pending TipContactEnd of ContactBall [s]
		bool StrikeContactOpen = false;     // (integration round 2) the dome still touches / overlaps the struck ball since the strike:
		                                    //   that interaction (incl. the slate reaction and the pinch) is StrikeCueBall's (MOT B.8.3),
		                                    //   so the tip slot searches the struck ball only from their separation on
	};

	// Island bookkeeping (architecture 8.8).
	struct IslandState
	{
		bool Active = false;
		double StartTime = 0.0;
		double RigidSince = -1.0;           // < 0 while compliant
		CompliantIsland Solver;
		IslandRecordList StepRecords;
	};

	// (v1 addition) WP-6a loop caches; not used by WP-6b.
	struct LoopCache
	{
		Aabb3 Box[kMaxBalls];                   // swept bounds of Seg over [T0, T0 + TauEnd] (pair broad phase)
		std::uint32_t BoxVersion[kMaxBalls] = {};
		bool BoxValid[kMaxBalls] = {};
		bool TrackFull[kMaxBalls] = {};         // the ball's track hit MaxSegmentsPerBall (TrajectoryOverflow)
	};

	struct Workspace
	{
		const SimInput* Input = nullptr;
		ShotResult* Result = nullptr;
		PhysicsParams Params;               // validated copy; Cli.TsujiAlpha resolved (>= 0)
		DetectOptions Detection;            // from Params (pooltool compat, nose radius, pocket model)
		EventHeap<kEventHeapCapacity> Queue;
		BallSlot Balls[kMaxBalls];
		TipSlot Tips[kMaxStrikes];
		FixedVector<ZenoEntry, kMaxZenoEntries> Zeno;
		IslandState Island;
		double Now = 0.0;                   // time of the event / island step being processed
		int EventsProcessed = 0;
		int IslandStepsUsed = 0;            // against NumericsConfig::MaxIslandSteps
		std::uint32_t RecordSequence = 0;
		ResultCapacity Caps;                // (v1 addition) recording capacities of this simulator (deterministic overflow points)
		LoopCache Cache;                    // (v1 addition) WP-6a only
	};

	// Seed of an island: the event that triggered it (ball-ball, ball-feature, pressing, Zeno, landing
	// or contact on the sloped rail top, frozen strike) at time Time.
	struct IslandSeed
	{
		int BallA = -1;
		int BallB = -1;                     // -1 for a ball-feature seed
		TableFeatureRef Feature;            // ball-feature seeds
		bool Pressing = false;
		bool Zeno = false;
		bool RailTop = false;               // start directly in Rigid mode (collisions 6.2)
	};

	// ---------------------------------------------------------------------------------------------
	// Services of the core loop (WP-6a), used by WP-6b
	// ---------------------------------------------------------------------------------------------

	// Closes the ball's open trajectory segment at Time and starts a new analytic segment from State
	// (classified by the caller), bumps the version, recomputes observers and re-predicts all its slots.
	void ReplaceSegment(Workspace& Ws, int Ball, const BallState& State, double Time);

	// Appends to ShotResult::Events (subject to the logging switches / capacity) and, if record-relevant,
	// to the ShotRecord (always, when RecordOptions::ShotRecord).
	void EmitEvent(Workspace& Ws, const ShotEvent& Event);

	// Terminal states (Pocketed / OffTable): final segment, Finals entry, removal from detection.
	void MakeTerminal(Workspace& Ws, int Ball, MotionState Terminal, double Time, PocketId Pocket, OffTableReason Reason);

	// Current state of a ball in event mode at absolute time Time (segment state: detection, approach tests,
	// observers, recording).
	BallState BallStateAt(const Workspace& Ws, int Ball, double Time);

	// State that RESOLVES an event of the ball at Time (every resolver: ball-ball, cushion, pocket, landing, rail top,
	// tip; and the initial body state of a ball that starts or joins an island, StartIsland / AdvanceIsland):
	// position from the segment, velocity and spin from the exact pursuit state inside a tilt chain piece
	// (EvaluateSegmentForEvent, human-factors 4.5.3); equal to BallStateAt on a level table. A contact that approaches
	// by the segment velocity but whose exact normal speed is <= ApproachSpeedTol is a pressing contact (island).
	BallState BallStateForEvent(const Workspace& Ws, int Ball, double Time);

	// ---------------------------------------------------------------------------------------------
	// Hooks implemented by WP-6b
	// ---------------------------------------------------------------------------------------------

	// Island (8.8): BFS with delta_cl over balls AND table features (QueryTableFeatures), members leave
	// event mode; tips of moving cues join as IslandTip when they can reach a member. Member states come from
	// BallStateForEvent; on a tilted table the island gets SetInPlaneGravity(InPlaneGravity(Params.Tilt, g)) (8.11).
	void StartIsland(Workspace& Ws, const IslandSeed& Seed, double Time);

	// Steps the active island up to (not beyond) UntilTime: per step joining of balls and table
	// features, member exits (drop edge, cloth region, rail-top exits), records -> events, observers of
	// members, sampled recording, rigid switch, exit, budget. Returns false if the step budget is exhausted.
	bool AdvanceIsland(Workspace& Ws, double UntilTime);

	// Time of the next island step (kInfinity if no island is active).
	double NextIslandStepTime(const Workspace& Ws);

	// Pocket state machine (8.9): DropEdge, liner, rim torus, capture depth, pocket exit, capture circle,
	// pivot end; returns true if the event was consumed.
	bool ProcessPocketEvent(Workspace& Ws, int Ball, const TableFeatureRef& Feature, double Time);

	// Landing routing (collisions 6.1) at the end of an Airborne segment.
	void RouteLanding(Workspace& Ws, int Ball, double Time);

	// Rail-top routing (collisions 6.2): RailTop / RailTopEdge contacts, SupportExit of the flat cap,
	// rest on the cap -> OffTable(RestsOnRailOrFrame).
	bool ProcessRailTopEvent(Workspace& Ws, int Ball, const TableFeatureRef& Feature, double Time);

	// ---------------------------------------------------------------------------------------------
	// (v1 addition) WP-6a loop internals, shared by Simulator.cpp and SimLoop*.cpp. Not for WP-6b.
	// ---------------------------------------------------------------------------------------------
	namespace loop
	{
		// Ball in play and not terminal.
		bool IsLive(const Workspace& Ws, int Ball);
		// Orientation is integrated for this ball (Trajectories, or a marked ball with ChalkCling).
		bool TracksOrientation(const Workspace& Ws, int Ball);
		// Support surface of the ball's context: friction parameters and plane height.
		ClothParams SupportSurface(const Workspace& Ws, int Ball);
		double SupportHeight(const Workspace& Ws, int Ball);
		// Position at Time from the island body (member), the pivot, or the segment.
		Vec3 CurrentPosition(const Workspace& Ws, int Ball, double Time);
		// (review addition) The same for the whole state (island member: the body's position, velocity and spin).
		BallState CurrentState(const Workspace& Ws, int Ball, double Time);

		// Segment replacement WITHOUT re-prediction (version bump, track, observers from Time with IncludeFrom). Initial
		// segments (t = 0) pass Initial = true: no track segment is closed, Orientation0 comes from the input.
		void SetSegment(Workspace& Ws, int Ball, const BallState& State, double Time, bool Initial);
		// Re-predicts every slot of the balls in Changed (bit = ball), the pair slots against every other live ball (each
		// pair once), the pair observers of other balls referencing them, and every moving tip.
		void PredictBalls(Workspace& Ws, std::uint32_t Changed);
		// Re-predicts the tip slots of moving tips. ExcludeBall / ExcludeTime: a tip contact on that ball at exactly that
		// time is not queued again (a tip contact that produced no impulse).
		void PredictTips(Workspace& Ws, int ExcludeBall, double ExcludeTime);

		// Queue validity of an entry (versions, island membership, terminal balls, moving tips).
		bool IsValid(const Workspace& Ws, const QueuedEvent& Event);
		// Push with compaction when full.
		void Push(Workspace& Ws, const QueuedEvent& Event);

		// Earliest pending observer or tip-contact end (kInfinity if none).
		double EarliestPending(const Workspace& Ws);
		// Emits pending observers and tip-contact ends with time < Time (Inclusive: <= Time) in time order.
		void EmitPending(Workspace& Ws, double Time, bool Inclusive);
		// Emits the pending TipContactEnd of a strike at TipSlot::ContactEnd and closes the interval (ContactBall := kNoBall).
		void EmitTipContactEnd(Workspace& Ws, int Strike);

		// Recomputes the ball's own observers (line crossings, rail freeze-leave, pair observers it owns) from From.
		void ComputeObservers(Workspace& Ws, int Ball, double From, bool IncludeFrom);
		// Removes and recomputes the pair observers owned by Owner with partner Partner (jump-over, ball freeze-leave).
		void RecomputePairObservers(Workspace& Ws, int Owner, int Partner, double From);

		// Closes the open track segment of the ball at Time (see the file comment) and returns the orientation at Time.
		Quat CloseOpenSegment(Workspace& Ws, int Ball, double Time, const Vec3& EndPosition);

		// Cue strikes at t = 0 (architecture 8.6): strike outcomes, CueStrike / TipContactBegin / BallAirborne events, tip
		// paths. States[b] is updated to the post-strike state.
		void ProcessStrikes(Workspace& Ws, BallState* States);
		// Dispatch of one valid queued event (architecture 8.5).
		void ProcessEvent(Workspace& Ws, const QueuedEvent& Event);
		// Island seed of a queued contact entry (group hand-off of exactly simultaneous events, architecture 8.3).
		IslandSeed SeedOf(const QueuedEvent& Event);
		// Hands a seed to StartIsland (diagnostics counters).
		void HandOffToIsland(Workspace& Ws, const IslandSeed& Seed, double Time);
		// True for queued entries that carry an impulse (group members of 8.3).
		bool IsImpulseContact(const QueuedEvent& Event);

		// Stops every live ball where it is at Time (event cap, horizon, island budget) and ends every tip and the island.
		void StopAllBalls(Workspace& Ws, double Time);

		// Plan gap [m] of a ball center P (radius R) to a cushion-like table feature for the island test and the freeze sets:
		// nose line (R_c), jaw arc (r_j + R_c), facing face on the shelf (s_f), within the feature's extent; airborne balls use the
		// 3-D distance to the nose / jaw edge minus R + r_n. kInfinity for every other feature, outside the extent, or for a jaw arc
		// more than 5 cm away (no caller compares with more than a few mm).
		double FeatureGap(const Workspace& Ws, const TableFeatureRef& Feature, const Vec3& P, double R);
	}
}
