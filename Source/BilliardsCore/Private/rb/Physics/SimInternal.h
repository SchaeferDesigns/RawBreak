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

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/FixedVector.h"
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
}
