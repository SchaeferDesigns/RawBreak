#pragma once

// Reference shots of the audio tests and recorded checks (Docs/ue-architecture.md 18.5, audio.md 3.7 / 14 AU-T12, AU-T16).
//   DiveBarBreak8   the dive-bar 8-ball break of click_synth.py (7-ft bar box, oversized 221 g cue ball, house cue, 8 m/s, sloppy
//                   rack, seed 13), rebuilt through the same core calls as
//                     rbsim --table 7ft-bar --balls oldbar --rack 8ball --rack-gap sloppy --seed 13 --cue house --ball 0:-0.62,0.10
//                           --speed 8 --aim -5.07 --offset 0,-0.05 --record
//                   Tools/audio/out/ref/divebar_break8.hash holds rbsim's input / result hashes of it (the AU-T12 event log).
//   TestRoomBreak9  the ROB-10 break9 scenario (9-ft pro table), via RbSimScenarios.
//   Breaker's ears  the listener of the prototype's break renders: 0.55 m behind the cue ball along the aim, eye 0.36 m above the
//                   cloth (ears +-8.75 cm to the sides).
// Owner: M2-C.

#include "CoreMinimal.h"

#include "Simulation/RbShot.h"

namespace RbAudioScenarios
{
	RAWBREAK_API bool MakeDiveBarBreak8(FRbShotRequest& Out, FString& OutError);
	RAWBREAK_API bool MakeTestRoomBreak9(FRbShotRequest& Out, FString& OutError);

	// Head of the breaker [m, core frame] for the first strike of Request (and the unit vector to the left ear).
	RAWBREAK_API bool BreakerHead(const FRbShotRequest& Request, rb::Vec3& OutHead, rb::Vec3& OutLeft);

	// Runs the request synchronously (rb::Simulator; the RbShot hashes filled) into a new shot.
	RAWBREAK_API TSharedPtr<FRbShot> Simulate(FRbShotRequest&& Request, FString& OutError);
}
