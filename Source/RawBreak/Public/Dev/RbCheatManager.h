#pragma once

// Development commands (Docs/ue-architecture.md 9.5): drive a match without a mouse, for headless functional tests
// (-ExecCmds="RbStrike 8 0 0 0 0") and for debugging. Available in non-shipping builds (the controller sets CheatClass;
// standalone games allow cheats). Owner: UE-7.
//
// Details (UE-7):
//  * Every command drives the real game objects: URbMatchDirector (the match), the pawn's URbStrokeComponent (RbStroke: the
//    human layer), URbReplaySubsystem, the controller's URbOverlayComponent. Nothing bypasses the rules.
//  * Sequencing for scripts: a match command issued while a live shot simulates / plays back, a scripted stroke is still on its
//    way to the ball, or an RbWait runs, is QUEUED and runs in order once that is over (a world timer drains the queue). So one
//    -ExecCmds batch can play several shots: "RbPlaybackRate 0, RbPlaceCueBall -0.8 0, RbStrike 3 0, RbReplay 1 0.25". A command
//    issued while nothing is pending runs at once. Queued commands expire after 60 s. RbPlaybackRate, RbOverlay and
//    RbStopReplay never wait. A match command stops a running replay first.
//  * Log lines "RbCheat: <command> ..." report the outcome; RbDumpState prints ONE grep-able line:
//      RbState: phase=<ERbDirectorPhase> mode=<Practice|HotSeat> game=<9-ball..> rack=<n> shot=<match shot index> shooter=<p>
//      wins=<a>:<b> scores=<a>:<b> fouls=<a>:<b> inhand=<0|1> placed=<0|1> region=<CueBallNext> decider=<p> options=[<Option>,..]
//      selected=<i> replay=<0|1> history=<n> rate=<live playback rate> last{valid=.. shooter=.. fouls=<hex> enforced=<Foul|-1>
//      rule="<ref>" pocketed=[..] first=<ball|-1> next=<NextAction> speed=<m/s> human=<0|1> sim=<ms>} balls=[<id>(<x>,<y>) ..]

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"

#include "Engine/TimerHandle.h"

#include "RbCheatManager.generated.h"

class URbMatchDirector;
class URbStrokeComponent;
struct FRbStrokeCommit;

UCLASS()
class RAWBREAK_API URbCheatManager : public UCheatManager
{
	GENERATED_BODY()

public:
	// Scripted strike (no human layer): tip speed [m/s], azimuth / elevation [deg], contact offsets a, b [-1, 1].
	UFUNCTION(Exec) void RbStrike(float SpeedMps, float AzimuthDeg, float ElevationDeg = 0.0f, float OffsetA = 0.0f, float OffsetB = 0.0f);

	// Human-layer stroke: scripted hand samples of a stroke that reaches SpeedMps at contact (tests the stroke path).
	// The pawn steps to the table behind the cue ball (within reach), aims at AzimuthDeg (core azimuth), gets down, holds
	// Commit and feeds URbStrokeComponent::MakeScriptedStroke through InjectStrokeSamples - the same path as the mouse.
	UFUNCTION(Exec) void RbStroke(float SpeedMps, float AzimuthDeg);

	// Ball in hand: place the cue ball at table coordinates [m] (core frame).
	UFUNCTION(Exec) void RbPlaceCueBall(float X, float Y);

	// Decision: pick option index i of the pending outcome.
	UFUNCTION(Exec) void RbChoose(int32 OptionIndex);

	// New rack / new match (Mode 0 practice, 1 hot-seat).
	UFUNCTION(Exec) void RbRerack();
	UFUNCTION(Exec) void RbNewMatch(int32 Mode);

	// Replay the last shot (view 0..3, rate). StartTime [s] of the shot to start from; Rate 0 freezes there (captures).
	UFUNCTION(Exec) void RbReplay(int32 View = 0, float Rate = 1.0f, float StartTime = 0.0f);

	// Overlay mode 0 hidden, 1 pinned, 2 pinned + debug.
	UFUNCTION(Exec) void RbOverlay(int32 Mode);

	// Logs the match state, the table state and the last shot summary (LogRawBreak) - tests grep it.
	UFUNCTION(Exec) void RbDumpState();

	// --- additions (UE-7) ----------------------------------------------------------------------------------

	// Live playback rate of the director (1 = real time; 0 = commit right after the simulation, headless tests).
	UFUNCTION(Exec) void RbPlaybackRate(float Rate);

	// Ends a running replay (back to the live table).
	UFUNCTION(Exec) void RbStopReplay();

	// Declaration of the next shot: Kind 0 normal, 1 push-out, 2 safety; Ball / Pocket (rb::PocketId 0..5) call a shot, -1 = none.
	UFUNCTION(Exec) void RbDeclare(int32 Kind, int32 Ball = -1, int32 Pocket = -1);

	// Queued pause of Seconds before the next queued command (scripts: let a shot settle before a capture state).
	UFUNCTION(Exec) void RbWait(float Seconds);

	// The line RbDumpState logs (tests).
	FString MakeStateLine() const;

	// Commands waiting in the queue (tests).
	int32 GetQueuedCount() const { return Queue.Num(); }

private:
	struct FQueuedCommand
	{
		FString Name;
		TFunction<void()> Run;
		double EnqueueTime = 0.0;
	};

	URbMatchDirector* GetDirector() const;
	// A live shot simulates / plays back, a scripted stroke is on its way, or an RbWait runs.
	bool IsBusy() const;
	// Runs Run now when nothing is pending, else queues it (Name for the log).
	void Enqueue(const TCHAR* Name, TFunction<void()> Run);
	void DrainQueue();
	void StopReplayForMatchCommand();

	bool StartScriptedStroke(float SpeedMps, float AzimuthDeg);
	void OnScriptedStrokeContact(const FRbStrokeCommit& Commit);
	void OnScriptedStrokeAborted(bool bRampShown);
	void EndScriptedStroke(const TCHAR* Why);

	TArray<FQueuedCommand> Queue;
	FTimerHandle QueueTimer;
	double WaitUntil = 0.0;
	bool bStrokeInFlight = false;
	double StrokeDeadline = 0.0;
	TWeakObjectPtr<URbStrokeComponent> StrokeInFlight;
	FDelegateHandle ContactHandle;
	FDelegateHandle AbortHandle;
};
