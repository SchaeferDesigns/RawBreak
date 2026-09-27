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
	uint32 Id = 0;                           // > 0, unique per world
	FRbShotRequest Request;
	rb::ShotResult Result;                   // compact (vectors sized to their content, not to ResultCapacity)
	double SimMilliseconds = 0.0;            // wall time of Simulator::Run on the worker
	uint64 ResultHash = 0;                   // RbShot::ResultHash (ROB-10 UE half)
};

namespace RbShot
{
	// Hash of status, stop time, the serialized event log, the finals and two diagnostics counters: bit-identical
	// definition to ResultHash in Tests/Core/Simulator/SimTestUtil.h (ROB-10 compares UE module vs standalone).
	RAWBREAK_API uint64 ResultHash(const rb::ShotResult& Result);

	// Hash of every simulator input field (table spec id, params, balls incl. orientation and marks, strikes, context),
	// same definition as rbsim --hash (UE-6a adds the flag). ROB-10 (review R-13): the UE test rebuilds the break9 SCENARIO
	// through the same core calls rbsim makes, compares InputHash first (scenario drift) and then ResultHash against the
	// values rbsim printed, committed in Tools/rbsim/examples/break9.hash. (Tools/rbsim/examples/break9.json is a rounded
	// output file - 6 significant digits - and can never reproduce a bitwise input.)
	RAWBREAK_API uint64 InputHash(const rb::SimInput& Input);

	// Copies Source into Out with every vector sized to its content (a reserved ShotResult is ~5.8 MB; the
	// compact copy of a break is a few hundred KB, so replay history stays small).
	RAWBREAK_API void CopyCompact(const rb::ShotResult& Source, rb::ShotResult& Out);

	// Initial SimInput for a table: Table pointer, Environment, Physics, RecordOptions for playback + rules
	// (everything on), no balls in play, no strikes.
	RAWBREAK_API void InitSimInput(const FRbTableContext& Table, rb::SimInput& Out);
}
