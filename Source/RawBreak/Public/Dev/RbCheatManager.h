#pragma once

// Development commands (Docs/ue-architecture.md 9.5): drive a match without a mouse, for headless functional tests
// (-ExecCmds="RbStrike 8 0 0 0 0") and for debugging. Available in non-shipping builds (the controller sets CheatClass;
// standalone games allow cheats). Owner: UE-7.

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"

#include "RbCheatManager.generated.h"

UCLASS()
class RAWBREAK_API URbCheatManager : public UCheatManager
{
	GENERATED_BODY()

public:
	// Scripted strike (no human layer): tip speed [m/s], azimuth / elevation [deg], contact offsets a, b [-1, 1].
	UFUNCTION(Exec) void RbStrike(float SpeedMps, float AzimuthDeg, float ElevationDeg, float OffsetA, float OffsetB);

	// Human-layer stroke: scripted hand samples of a stroke that reaches SpeedMps at contact (tests the stroke path).
	UFUNCTION(Exec) void RbStroke(float SpeedMps, float AzimuthDeg);

	// Ball in hand: place the cue ball at table coordinates [m] (core frame).
	UFUNCTION(Exec) void RbPlaceCueBall(float X, float Y);

	// Decision: pick option index i of the pending outcome.
	UFUNCTION(Exec) void RbChoose(int32 OptionIndex);

	// New rack / new match (Mode 0 practice, 1 hot-seat).
	UFUNCTION(Exec) void RbRerack();
	UFUNCTION(Exec) void RbNewMatch(int32 Mode);

	// Replay the last shot (view 0..3, rate).
	UFUNCTION(Exec) void RbReplay(int32 View, float Rate);

	// Overlay mode 0 hidden, 1 pinned, 2 pinned + debug.
	UFUNCTION(Exec) void RbOverlay(int32 Mode);

	// Logs the match state, the table state and the last shot summary (LogRawBreak) - tests grep it.
	UFUNCTION(Exec) void RbDumpState();
};
