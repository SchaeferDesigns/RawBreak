#pragma once

// Scripted reference shots of the simulation service (tests, the ROB-10 check, the rb.SimDemo dev command).
// Owner: UE-6a.

#include "CoreMinimal.h"

#include "Simulation/RbShot.h"

namespace RbSimScenarios
{
	// The break9 scenario of ROB-10 (Docs/ue-architecture.md A5, Tools/rbsim/examples/break9.hash), rebuilt through the
	// SAME core calls as rbsim's BuildInput for
	//   rbsim --table 9ft-pro --rack 9ball --rack-gap wooden --seed 11 --cue break --ball 0:-0.735,0.12 --speed 9
	//         --aim -5.006 --offset 0,-0.1 --record
	// but with the table, physics, ball set and rules table from the game's own FRbTableContext and RbShot::InitSimInput:
	// ROB-10 proves that this UE path gives the standalone input (InputHash) and result (ResultHash) bit for bit.
	// Out must be a default-constructed request. False with OutError if a core call fails.
	RAWBREAK_API bool MakeBreak9(FRbShotRequest& Out, FString& OutError);

	// A short two-ball shot with other content than the break: cue ball at (-0.6, 0) with top-right english
	// (contact offsets 0.1, 0.2) at 3 m/s, 3 deg, playing cue, into ball 1 at (0.3, 0.05); 9-ft pro table.
	RAWBREAK_API bool MakeTwoBall(FRbShotRequest& Out, FString& OutError);

	// By name ("break9", "twoball"); false for an unknown name.
	RAWBREAK_API bool MakeByName(const FString& Name, FRbShotRequest& Out, FString& OutError);
}
