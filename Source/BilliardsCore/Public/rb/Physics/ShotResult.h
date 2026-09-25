#pragma once

// Simulation output: (1) ordered event log with participants, time, pre/post states, impact speeds
// and impulses (audio/VFX/debug), (2) per-ball piecewise-analytic trajectory segments for playback
// (evaluate with rb/Physics/Playback.h) including segment start orientations, plus the cue tip paths
// (cue animation consistent with the detected double hits), (3) the rules-facing ShotRecord from which
// rb::rules derives ShotFacts. Plain data; containers are reserved ONCE outside the event loop
// (ReserveShotResult) and reused across shots without reallocation.
// Owner: WP-6a (simulator core loop). Consumers: WP-7 (playback, record builder, rbsim), Unreal, AI.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>
#include <vector>

namespace rb
{
	enum class ShotEventType : std::uint8_t
	{
		CueStrike,          // A = struck ball, t = 0; Feature = strike index; Normal = p_hat; NormalSpeed = V (tip); NormalImpulse = J; Value = V'
		TipRecontact,       // follow-through tip impulse on A (event mode; A = struck or any other ball); Feature = strike index
		TipContactBegin,    // tip contact interval starts on A (t = 0 for the strike, TipRecontact, positive island tip force);
		                    //   Feature = strike index; B = f (frozen ball the stroke goes into, rules.md F7); Value = gap A-f;
		                    //   SubFeature = 1 if A touched a ball other than f or a rail before (0 otherwise)
		TipContactEnd,      // tip contact interval ends on A; same fields
		BallBall,           // A < B; Normal = n_hat (A -> B); NormalSpeed = v_n; impulses; CutAngle (if one is the cue ball)
		BallCushion,        // A; Feature = CushionId (nose line)
		BallJaw,            // A; Feature = PocketId; SubFeature = JawSide | (element << 4), element 0 arc, 1 facing face, 2 facing edge
		BallRailTop,        // A; Feature = CushionId (0xFF pocket surround); SubFeature = RailTopKind; Value = polygon index
		BallSlate,          // A hit the slate/shelf from the air; NormalSpeed = -v_z; SubFeature = bounce index
		BallAirborne,       // A left the cloth (strike hop, rail, ball-ball); Value = apex center height [m]
		BallLand,           // A classified back on the cloth; Value = max center height of the sequence [m]
		BallPocketEnter,    // A crossed the drop edge of pocket Feature; Value = normal speed over the edge
		BallPocketRim,      // A hit the rounded rim from inside (torus)
		BallLiner,          // A hit the liner / hole wall
		BallPocketExit,     // A rattled back out over the table
		BallPocketed,       // A captured (z <= -R) in pocket Feature
		BallOffTable,       // A; Feature = OffTableReason
		BallExternalContact,// A; Feature = ExternalObject (Lamp)
		MotionTransition,   // A; From -> To (sliding -> rolling -> spinning -> stationary)
		BallLineCross,      // A; Feature = TableLine; SubFeature = direction (0 = +, 1 = -) (observer)
		BallJumpedOver,     // A passed over B while airborne without touching it (observer, rules F9)
		IslandBegin,        // CLI island started; A = lowest member id; Value = member count
		IslandRigid,        // island switched Compliant -> Rigid (sustained contact or CompliantMaxDuration); Value = compliant duration [s]
		IslandEnd,          // CLI island ended; Value = island duration [s]
		ZenoGuard,          // pair (A, B or feature) moved into a CLI island by the Zeno detector
		Diagnostic,         // overlap / missed event / snap residual / capacity; Value = magnitude
	};

	namespace ShotEventFlags
	{
		inline constexpr std::uint8_t Pressing = 1u << 0;               // resolved in a CLI island at zero normal speed
		inline constexpr std::uint8_t AtStart = 1u << 1;                // contact at tau = 0 (touching and approaching)
		inline constexpr std::uint8_t FromIsland = 1u << 2;             // first-touch record emitted by a CLI island
		inline constexpr std::uint8_t ContinuesInitialFreeze = 1u << 3; // rules.md 3.3
		inline constexpr std::uint8_t Stick = 1u << 4;                  // stick branch of the impulse model
		inline constexpr std::uint8_t Resting = 1u << 5;                // micro-impact with e = 0 (v_n < v_rest)
		inline constexpr std::uint8_t Miscue = 1u << 6;                 // CueStrike / TipRecontact
		inline constexpr std::uint8_t Airborne = 1u << 7;               // resolved with the airborne (GRI) model
	}

	struct ShotEvent
	{
		double Time = 0.0;              // absolute [s], t = 0 at the first tip contact
		ShotEventType Type = ShotEventType::Diagnostic;
		std::uint8_t Flags = 0;         // ShotEventFlags
		BallId A = kNoBall;
		BallId B = kNoBall;
		std::uint8_t Feature = 0xFF;    // see ShotEventType
		std::uint8_t SubFeature = 0xFF;
		MotionState From = MotionState::Stationary; // MotionTransition
		MotionState To = MotionState::Stationary;
		Vec3 Normal;                    // contact normal (unit): ball-ball A -> B; fixed contacts k_hat (contact -> center)
		double NormalSpeed = 0.0;       // approach speed along Normal [m/s] (audio level/timbre)
		double NormalImpulse = 0.0;     // [N s]
		double TangentImpulse = 0.0;    // [N s]
		double CutAngle = 0.0;          // [rad]
		double Value = 0.0;             // event-specific scalar (see ShotEventType)
		BallState Pre[2];               // states of A (index 0) and B (index 1) just before (RecordOptions::EventStates)
		BallState Post[2];              // ... and just after the event
	};

	enum class SegmentKind : std::uint8_t
	{
		Analytic, // closed-form MotionSegment (Motion.Pos0 + Vel0 tau + Accel2 tau^2, spin laws of Motion.h)
		Sampled,  // CLI island or pocket pivot: linear from Motion.Pos0 (T0) to EndPosition (T1), w = Motion.Omega0
		Terminal, // pocketed / off table: frozen at Motion.Pos0 from T0 on (the renderer animates the gully / fall)
	};

	struct TrajectorySegment
	{
		MotionSegment Motion;   // Motion.T0 = segment start; Motion.State = motion state during the segment
		double T1 = 0.0;        // segment end [s] (+inf for the last Stationary/Terminal segment)
		SegmentKind Kind = SegmentKind::Analytic;
		Vec3 EndPosition;       // Sampled only
		Quat Orientation0;      // ball orientation at T0 (integrated by the simulator; no drift across segments)
	};

	struct BallTrack
	{
		std::vector<TrajectorySegment> Segments; // time ordered, contiguous: Segments[k].T1 == Segments[k+1].Motion.T0
	};

	// One piece of a cue tip's path (renderer: the cue follows the tip dome center; the rest of the cue is
	// rigid along Path.Direction). Analytic pieces use CueTipPath (uniform deceleration); Sampled pieces
	// (tip inside a CLI island) interpolate linearly from Path.Start at Path.StartTime to EndPosition at T1.
	struct CueTipSegment
	{
		int Strike = 0;
		CueTipPath Path;
		double T1 = 0.0;        // end of this piece [s] (the last piece ends at Path.StopTime)
		SegmentKind Kind = SegmentKind::Analytic;
		Vec3 EndPosition;       // Sampled only
	};

	enum class BallFinalStatus : std::uint8_t
	{
		NotInPlay,
		OnTable,
		Pocketed,
		OffTable,
	};

	struct BallFinal
	{
		BallFinalStatus Status = BallFinalStatus::NotInPlay;
		BallState State;        // final state (at rest on the table, or where it was removed)
		Quat Orientation;       // final orientation (carry over into the next shot's SimBall::Orientation)
		PocketId Pocket = PocketId::None;
		OffTableReason OffReason = OffTableReason::Floor;
		double Time = 0.0;      // time it came to rest / was pocketed / left the table [s]
	};

	enum class SimStatus : std::uint8_t
	{
		Ok,
		InvalidInput,   // see Diagnostics.InputError; nothing simulated
		Aborted,        // MaxEvents exceeded: all balls stopped where they were (collisions 7.3 guard 4)
		HorizonReached, // TimeHorizon reached with balls still moving (safety net)
		NotImplemented, // stub
	};

	struct SimDiagnostics
	{
		ErrorCode InputError = ErrorCode::Ok;
		int EventsProcessed = 0;     // popped valid events (counted against MaxEvents)
		int StaleEventsSkipped = 0;  // lazy-deletion discards
		int Predictions = 0;         // root isolations performed
		int Islands = 0;
		int IslandSteps = 0;         // compliant + rigid steps of all islands (budget NumericsConfig::MaxIslandSteps)
		int IslandRigidSwitches = 0; // islands switched to Rigid mode (sustained contacts, CompliantMaxDuration, rail top)
		bool IslandBudgetExceeded = false; // MaxIslandSteps reached: all balls stopped, SimStatus::Aborted
		int ZenoTriggers = 0;
		int PressingContacts = 0;
		int OverlapWarnings = 0;     // overlap > OverlapGuard detected (should be 0; ROB-11)
		int MissedEvents = 0;        // e.g. landing over a rail (collisions 6.1 step 3)
		int FeatureJoins = 0;        // table features added to running islands (architecture 8.8)
		bool IslandCapacityExceeded = false; // island body / feature / contact list full (Diagnostic event logged)
		bool EventLogOverflow = false;
		bool TrajectoryOverflow = false;
		bool CueTipOverflow = false;
		bool RecordOverflow = false;
	};

	struct StrikeOutcome
	{
		BallId Ball = kNoBall;   // struck ball
		StrikeResult Result;     // strike details at t = 0
	};

	struct ShotResult
	{
		SimStatus Status = SimStatus::NotImplemented;
		double StopTime = 0.0;           // tStop [s]
		std::uint32_t BallsInPlay = 0;   // bit per ball id simulated
		FixedVector<StrikeOutcome, kMaxStrikes> Strikes; // one per SimInput::Strikes entry, same order
		std::vector<ShotEvent> Events;   // time ordered by (Time, tier, ids) - the processing order
		BallTrack Tracks[kMaxBalls];     // index = ball id
		std::vector<CueTipSegment> CueTips; // all strikes, time ordered per strike
		BallFinal Finals[kMaxBalls];     // index = ball id
		ShotRecord Record;               // rules-facing record, built during Run (Begin/AppendRecordEvent/Finish, ShotRecordBuilder.h)
		SimDiagnostics Diagnostics;
	};

	// Capacities of the recording buffers. Reserved once; on overflow the simulation continues
	// correctly and the matching *Overflow diagnostic is set (never an allocation in the loop).
	struct ResultCapacity
	{
		int MaxLoggedEvents = 4096;     // ShotResult::Events (a 15-ball break logs < 1000)
		int MaxSegmentsPerBall = 512;   // BallTrack::Segments (islands / pivots are sampled adaptively, NumericsConfig::SampleTolerance;
		                                //   a 50 ms rigid island needs < 100 samples per member)
		int MaxRecordEvents = 4096;     // ShotRecord::Events
		int MaxCueTipSegments = 64;     // ShotResult::CueTips
	};

	// Reserves all vectors to capacity (allocates; call outside the event loop).
	RB_API void ReserveShotResult(ShotResult& Result, const ResultCapacity& Capacity);

	// Clears all content but keeps the reserved capacity.
	RB_API void ResetShotResult(ShotResult& Result);
}
