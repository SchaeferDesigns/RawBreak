#pragma once

// Event-based shot simulator (Docs/architecture.md "Event loop"): input state + optional cue strikes
// -> ShotResult. Per-ball time bases, lazy-invalidated priority queue, strict tie-break order, Zeno
// guards, CLI island hand-off, pocket state machine, landing and rail-top routing, off-table detection.
// Owner: WP-6a (core loop: this header, the loop, recording, guards). Islands, pockets and rail-top
// routing are WP-6b behind the private interface Private/rb/Physics/SimInternal.h.
//
// Threading: a Simulator owns only its private workspace; the TableGeometry is read-only and may be
// shared. Independent Simulator objects can run concurrently on different threads (AI rollouts).
// No global mutable state, no allocation inside Run() once the ShotResult is reserved.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Tolerances.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Vec2.h"
#include "rb/Physics/BallBall.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Compliant.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Cushion.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/PocketDrop.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Slate.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>

namespace rb
{
	// Where a PhysicsParams value came from. Run() rejects Unset (InvalidInput, InputError =
	// InvalidParameter) so that no entry point silently simulates a table with another table's cloth
	// (single source of truth: TableSpec -> MakePhysicsParams).
	enum class ParamsOrigin : std::uint8_t
	{
		Unset,    // default-constructed: not allowed in Run()
		Table,    // MakePhysicsParams(TableSpec) (game, AI, rbsim), possibly with explicit overrides afterwards
		Explicit, // tests / calibration: every value pinned by the caller (VAL pitfall 9)
	};

	// Every physics parameter of a shot, with the verified spec defaults. Recorded with the shot
	// (replays / AI reproducibility). Tests pin their own values explicitly (Origin = Explicit).
	struct PhysicsParams
	{
		ParamsOrigin Origin = ParamsOrigin::Unset;
		double Gravity = kStandardGravity;   // g [m/s^2]
		ClothParams Cloth = kClothDefault;   // mu_s, mu_r, alpha_sp (A.9); from TableSpec::Cloth via MakePhysicsParams
		SlateParams Slate;                   // e_slate, h_min, N_max (C.5)
		PinchParams Pinch;                   // elevated-cue pinch schedule (B.8.2)
		BallBallParams BallBall;             // e_b, Alciatore friction (collisions 0.4)
		CushionParams Cushion;               // Mathavan default, mu_w, e_c law (4.8); k_f from TableSpec
		PocketContactParams PocketContacts;  // liner (from TableSpec), rim, rail top (5.3, 6.2)
		PocketModel Pockets = PocketModel::GeometricLevelA; // CaptureCircle only for XREF-01
		CliParams Cli;                       // Hertz/Tsuji island solver (3.9.3); TsujiAlpha < 0 = derived from e_b
		NumericsConfig Numerics;             // all tolerances and guards (rb/Core/Tolerances.h)
		TiltParams Tilt;                     // table tilt and nap (human-factors 4.5); level by default (HF-B10)
		bool ChalkCling = false;             // per-contact cling from SimBall::ChalkMarks (human-factors 4.3, V2); off: every
		                                     //   ball-ball contact uses BallBall.ClingFactor (= k_venue), as in collisions 2.1
	};

	// Condition of ONE physical table in a venue (persistent per venue seed, rb/Human/Venue.h): what differs between
	// two tables built from the same TableSpec preset. The default is a level table with clean balls.
	struct TableCondition
	{
		Vec2 Slope;              // TiltParams::Slope [1] (HF-50; dive bar 0.5-2.5 mm/m, pool hall <= 0.3, arena <= 0.1)
		double BallCling = 1.0;  // k_venue -> BallBallParams::ClingFactor (HF-41: 1.3 dive bar)
		bool ChalkCling = false; // HF-40 physics (V2) -> PhysicsParams::ChalkCling
	};

	// The ONLY constructor of table-dependent parameters (game, AI, rbsim): Origin = Table, Cloth =
	// ClothParamsFor(Spec.Cloth), Cushion.FacingRestitutionScale = Spec.FacingRestitutionScale,
	// PocketContacts.LinerRestitution / LinerFriction = Spec.Liner*, everything else the spec defaults
	// (level table, clean balls). Deterministic (pure function of the preset).
	RB_API PhysicsParams MakePhysicsParams(const TableSpec& Spec);

	// MakePhysicsParams(Spec) plus the venue table's condition: Tilt.Slope, BallBall.ClingFactor, ChalkCling.
	// MakePhysicsParams(Spec, TableCondition{}) == MakePhysicsParams(Spec) for every ParamTable key.
	RB_API PhysicsParams MakePhysicsParams(const TableSpec& Spec, const TableCondition& Condition);

	// Rejects non-physical parameters (prior-art 5.11, ROB-06): mu <= 0, e outside [0, 1], alpha_sp <= 0,
	// g <= 0, step sizes <= 0, capacities <= 0, Origin == Unset; tilt (human-factors 4.5.1, 4.5.6): non-finite tilt values,
	// (5/7) |Slope| + |NapPseudoSlope| > (1 - NapResistance) mu_r / 2 of the cloth (the k = 2/5 form of the TiltParams validity
	// rule; |Slope| <= 0.7 mu_r without nap), |Slope| > 0.7 mu_r of the rail cap (no nap there), Tolerance <= 0,
	// RefreshMaxInterval <= 0, NapResistance outside [0, 1), ChalkClingFactor or ClingFactor <= 0. Run additionally checks
	// the TiltParams rule with every ball's own k (InvalidInput).
	RB_API ErrorCode ValidatePhysicsParams(const PhysicsParams& Params);

	// What is logged into ShotResult (physics log, playback). The rules record (ShotRecord = true) is
	// independent of these switches: its observers (line crossings, freeze-leave, jump-over) and tip
	// contacts are always produced when ShotRecord is true (the AI and the referee must agree, RUL pitfall 17).
	struct RecordOptions
	{
		bool Trajectories = true;   // BallTrack segments + orientations + CueTips (playback). AI rollouts: false
		bool EventStates = true;    // ShotEvent::Pre/Post
		bool LogTransitions = true; // MotionTransition events in ShotResult::Events
		bool LogObservers = true;   // BallLineCross / BallJumpedOver events in ShotResult::Events
		bool ShotRecord = true;     // build ShotResult::Record (complete even if ShotResult::Events overflows)
	};

	struct SimBall
	{
		bool InPlay = false;        // simulated (on the table); false for pocketed / out-of-play / unused ids
		BallSpec Spec;              // per-ball R, m, I (InertiaFactor in (0, 2/3])
		BallState State;            // initial state; must be classified-consistent (at rest for a strike)
		Quat Orientation;           // initial orientation (playback; the chalk-mark cling with PhysicsParams::ChalkCling)
		BallChalkMarks ChalkMarks;  // body-frame chalk marks (rb/Human/BallMarks.h); read only with PhysicsParams::ChalkCling
	};

	// One cue stroke at t = 0. Normal shots have exactly one (Ball = the cue ball); the lag has two
	// (rules.md 4.1). Any in-play ball at rest may be struck (tests, rbsim --strike-ball).
	struct StrikeRequest
	{
		BallId Ball = static_cast<BallId>(kCueBallId);
		CueStrikeInput Input;
	};

	struct SimInput
	{
		const TableGeometry* Table = nullptr; // required; built once with BuildTableGeometry
		EnvironmentSpec Environment;          // lamp (off-table apex check)
		PhysicsParams Params;                 // MakePhysicsParams(Table->Spec) unless a test pins them
		SimBall Balls[kMaxBalls];             // index = ball id (0 = cue ball)
		FixedVector<StrikeRequest, kMaxStrikes> Strikes; // empty = no stroke (balls already moving)
		ShotContext Context;                  // game-layer facts for the rules record
		RecordOptions Record;
	};

	class Simulator
	{
	public:
		RB_API explicit Simulator(const ResultCapacity& InCapacity = ResultCapacity{});
		RB_API ~Simulator();
		RB_API Simulator(Simulator&& Other) noexcept;
		RB_API Simulator& operator=(Simulator&& Other) noexcept;
		Simulator(const Simulator&) = delete;
		Simulator& operator=(const Simulator&) = delete;

		// Simulates until every ball is stationary or terminal (or a guard fires). Result is reset and
		// reserved to this simulator's capacity if needed (the only allocation, before the loop starts).
		// Deterministic: identical Input -> bitwise identical Result.
		RB_API SimStatus Run(const SimInput& Input, ShotResult& Result);

	private:
		struct Workspace;          // private per-simulator state (Private/rb/Physics/SimInternal.h)
		Workspace* Ws = nullptr;   // owned; allocated once in the constructor
		ResultCapacity Caps;       // recording capacities used to reserve ShotResults
	};
}
