#pragma once

// One shot as the game sees it (Docs/ue-architecture.md 5.2): the complete simulator input, the human-layer
// record of the stroke (replay header, architecture.md 5.3 / 13 item 11) and the compact result. Plain C++ (no
// UObject): built on the game thread, simulated on a worker, then IMMUTABLE and shared as TSharedRef<const FRbShot>
// by playback, rules evaluation and replay. Owner: UE-6a.

#include "CoreMinimal.h"

#include "Math/RbStrokeMath.h"
#include "Simulation/RbTableContext.h"

#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"

// What the human layer produced for the strike (absent for scripted / cheat shots).
struct FRbStrokeRecord
{
	bool bHuman = false;                     // Executed came from rb::human::ExecuteStroke
	rb::human::IntendedStroke Intended;      // from the stroke input (URbStrokeComponent)
	rb::human::ExecutedStroke Executed;      // what the physics got (Executed.Strike = Input.Strikes[0].Input)
	rb::human::NoiseKey Key;                 // noise key of the draw
	TArray<FRbStrokeSample> InputLog;        // raw stroke samples (re-execution HF-B01; replays use Executed.Strike)
};

struct FRbShotRequest
{
	TSharedPtr<const FRbTableContext> Table; // required
	rb::SimInput Input;                      // Input.Table is (re)pointed at Table->Geometry by the subsystem
	FRbStrokeRecord Stroke;
	int32 Shooter = 0;                       // player index (hot-seat) - informational
	uint32 MatchShotIndex = 0;               // shot of the match (NoiseKey::ShotIndex)
	double ContactTime = 0.0;                // FPlatformTime::Seconds() at the tip contact: playback clock anchor
	FTransform ShooterView;                  // world camera transform at contact ("shooter" replay camera)
};

struct FRbShot
{
	uint32 Id = 0;                           // > 0, unique per world (RunShotBlocking: 0)
	FRbShotRequest Request;
	rb::ShotResult Result;                   // compact (vectors sized to their content, not to ResultCapacity)
	double SimMilliseconds = 0.0;            // wall time of Simulator::Run (worker, or the game thread for RunShotBlocking)
	uint64 ResultHash = 0;                   // RbShot::ResultHash(Result) (ROB-10 UE half)
	uint64 InputHash = 0;                    // RbShot::InputHash(Request.Input): replay header / determinism checks
	bool bSimulatedOnWorker = false;         // Run executed off the game thread (false: RunShotBlocking, or a collect wait
	                                         //   that retracted the not yet started task and ran it in place)
	uint64 SubmitFrame = 0;                  // GFrameCounter at SubmitShot
	uint64 HandOffFrame = 0;                 // GFrameCounter when OnShotSimulated fired (== SubmitFrame: same-frame hand-off)
};

namespace RbShot
{
	// Hash of status, stop time, the serialized event log (incl. pre/post states), the finals and two diagnostics
	// counters: bit-identical definition to ResultHash in Tests/Core/Simulator/SimTestUtil.h, which rbsim --hash uses
	// directly (ROB-10 compares UE module vs standalone). FNV-1a over the bytes of 64-bit words, doubles by bit pattern.
	RAWBREAK_API uint64 ResultHash(const rb::ShotResult& Result);

	// Hash of every simulator input field, same definition (field order) as InputHash in Tools/rbsim/Main.cpp
	// (rbsim --hash). ROB-10 (review R-13): the UE test rebuilds the break9 SCENARIO through the same core calls rbsim
	// makes, compares InputHash first (scenario drift) and then ResultHash against the values rbsim printed, committed
	// in Tools/rbsim/examples/break9.hash. (Tools/rbsim/examples/break9.json is a rounded output file - 6 significant
	// digits - and can never reproduce a bitwise input.) Field order, same Mix as ResultHash:
	//   1. table: 0 without a table, else 1, TableSpec::Preset and every TableSpec number in declaration order (Length ..
	//      SightDiameter, Corner / Side pocket {Mouth, CutAngle, Shelf, JawRadius, CaptureRadius}, Backdraft ..
	//      LinerUndercut, HasPockets, Cloth, FacingRestitutionScale, LinerRestitution, LinerFriction); the geometry is
	//      BuildTableGeometry(spec) and therefore covered
	//   2. environment: LampUndersideZ, LampFootprint Lo.x Lo.y Hi.x Hi.y
	//   3. params: PhysicsParamCount(), then every rb/Physics/ParamTable.h key's value in table order (the complete
	//      replay contract of rbsim --dump-input / --in)
	//   4. every IN-PLAY ball in id order: id, Radius, Mass, Inertia, state (position, velocity, omega, motion state),
	//      orientation w x y z, chalk-mark count, per mark BodyDir x y z, Strength, Radius
	//   5. strikes: count, per strike Ball, Speed, Elevation, Azimuth, OffsetA, OffsetB, LambdaOverride, SquirtEnabled,
	//      TipTouchesCloth, cue {Mass, EndMass, TipRestitution, TipFriction, TipFrictionKinetic, TipDomeRadius,
	//      TipDiameter, Length, ContactTime, FollowThroughDistance, JumpCue}
	//   6. context: InHand, PlacedPosition x y, TemplatePresent, ShotClockElapsed, FootOnFloor, FrozenTolerance,
	//      non-tip contact count, per contact Ball, Source, Time
	//   7. record options: Trajectories, EventStates, LogTransitions, LogObservers, ShotRecord
	// Balls that are not in play are skipped (the simulator never reads them). Adding a ParamTable key changes every hash:
	// regenerate break9.hash with rbsim --hash then (command in the header of that file).
	RAWBREAK_API uint64 InputHash(const rb::SimInput& Input);

	// Copies Source into Out with every vector sized to its content (a reserved ShotResult is ~5.8 MB; the
	// compact copy of a break is a few hundred KB, so replay history stays small). Afterwards capacity() == size()
	// for Events, CueTips, every Tracks[i].Segments and Record.Events.
	RAWBREAK_API void CopyCompact(const rb::ShotResult& Source, rb::ShotResult& Out);

	// Heap bytes held by the result's vectors (by capacity).
	RAWBREAK_API SIZE_T HeapBytes(const rb::ShotResult& Result);

	// Memory of one shot: sizeof(FRbShot) + HeapBytes(Result) + the stroke input log's allocation (replay history
	// budget: < 1 MB for a break, ue-architecture 6.7).
	RAWBREAK_API SIZE_T FootprintBytes(const FRbShot& Shot);

	// Initial SimInput for a table: Table pointer, Environment, Physics, RecordOptions for playback + rules
	// (everything on), no balls in play (the ball specs of the set are filled in), no strikes, default context.
	RAWBREAK_API void InitSimInput(const FRbTableContext& Table, rb::SimInput& Out);
}
