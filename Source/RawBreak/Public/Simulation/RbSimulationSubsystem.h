#pragma once

// Simulation service of a world (Docs/ue-architecture.md 5.2, 7): runs rb::Simulator::Run on a worker thread
// (UE::Tasks) and hands the immutable FRbShot back to the game thread. No UObject is touched off the game thread;
// the worker only sees the request (plain data + TSharedPtr<const FRbTableContext>) and a Simulator it owns
// exclusively for the duration of the task. Owner: UE-6a.
//
// Hand-off: SubmitShot at the tip contact (pawn tick, TG_PrePhysics) -> the subsystem's Tick (end of the world
// tick, after TG_PostPhysics) waits up to CollectBudgetSeconds for the task (a break takes < 2 ms, plan 9.1), so the
// result is normally broadcast in the SAME frame; otherwise it is polled (no wait) every later frame. The bounded wait
// happens once per shot, at the FIRST Tick after its submission: the submit frame, or the next frame for a shot
// submitted after this frame's Tick already ran (from an OnShotSimulated handler, a later tick group, a console
// command). A task that no worker has started yet when the wait begins is retracted and run in place on the game
// thread (UE::Tasks Wait), so a saturated worker pool never delays the hand-off beyond the simulation time itself.
// Playback is anchored at the contact time, so a late hand-off never changes what is shown, only when.
//
// Lifetime: one shot in flight at a time (SubmitShot refuses while busy). Deinitialize (world teardown) and
// CancelInFlight wait for the in-flight task - Simulator::Run cannot be interrupted, but it is bounded by its
// MaxEvents / time-horizon guards and takes milliseconds - and drop its result without broadcasting. A table rebuilt
// while a shot is in flight is harmless: the request holds its own TSharedPtr<const FRbTableContext>.
//
// AI (later): a pool of Simulators (one per worker) sharing the table context, RecordOptions for rollouts
// (architecture.md 5.2); the same subsystem owns them.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"

#include "Simulation/RbShot.h"

#include "rb/Physics/Simulator.h"

#include "RbSimulationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnShotSimulated, const TSharedRef<const FRbShot>& /*Shot*/);

// Counters of the service (F2 debug block, performance acceptance A6).
struct FRbSimulationStats
{
	int32 Submitted = 0;             // shots launched on a worker
	int32 HandedOff = 0;             // shots broadcast through OnShotSimulated
	int32 SameFrameHandOffs = 0;     // ... in the frame they were submitted in (HandOffFrame == SubmitFrame)
	int32 RanOnWorker = 0;           // ... whose Simulator::Run executed off the game thread
	int32 Discarded = 0;             // shots dropped by CancelInFlight / Deinitialize
	int32 Refused = 0;               // SubmitShot calls refused (busy or invalid request)
	double LastSimMilliseconds = 0.0;
	double MaxSimMilliseconds = 0.0;
	double TotalSimMilliseconds = 0.0;
};

UCLASS()
class RAWBREAK_API URbSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	URbSimulationSubsystem();

	// Launches the shot on a worker. Returns the shot id (> 0), or 0 if a shot is still in flight or the request
	// is invalid (no table context); a refused request is left untouched. Game thread only.
	uint32 SubmitShot(FRbShotRequest&& Request);

	// True while a submitted shot has not been handed off yet.
	bool IsBusy() const;

	// True while a shot is in flight AND its simulation has finished (TryCollect(0) hands it off without waiting).
	bool IsResultReady() const;

	// Waits up to MaxWaitSeconds for the in-flight shot and broadcasts OnShotSimulated if it finished. MaxWaitSeconds
	// <= 0 only polls (never blocks, never runs the task in place). Returns true if a shot was handed off. Called by
	// Tick; tests may call it directly. Game thread only.
	bool TryCollect(double MaxWaitSeconds);

	// Waits for the in-flight shot (if any) and drops it: no OnShotSimulated, GetLastShot unchanged, the service is
	// free again. For a match reset / new rack while a shot simulates. Game thread only.
	void CancelInFlight();

	// Game-thread synchronous run (tests, tools, the ROB-10 check) with its own Simulator. Does not broadcast.
	// The result is bitwise identical to the worker path (deterministic core, ROB-10 standalone half).
	static TSharedRef<FRbShot> RunShotBlocking(FRbShotRequest&& Request);

	TSharedPtr<const FRbShot> GetLastShot() const { return LastShot; }

	const FRbSimulationStats& GetStats() const { return Stats; }

	// Fired on the game thread once per submitted shot, in submission order.
	FRbOnShotSimulated OnShotSimulated;

	// Wait budget inside Tick for a same-frame hand-off [s] (only at the first Tick after the submission, normally in
	// the frame of the submission).
	double CollectBudgetSeconds = 0.004;

	// Creates the pooled Simulator / result and runs the break9 reference shot once on the game thread (discarded: no
	// broadcast, no stats), so the first real shot of a world does not pay the first-run costs - the ~6 MB result reservation
	// and its first touch, cold code. Without it the first break of a packaged game took 5.5 ms and handed off a frame late
	// (M1 integration, A6). Called at OnWorldBeginPlay of game worlds; idempotent; refused while a shot is in flight.
	void Prewarm();
	bool IsPrewarmed() const { return bPrewarmed; }

	// UTickableWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void BeginDestroy() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	// Worker body: runs the simulation into the pooled reserved result, then compacts it into Shot and hashes it.
	static void Simulate(rb::Simulator& Sim, rb::ShotResult& Work, FRbShot& Shot);

	// Blocks until the in-flight task finished and drops its result (Deinitialize, CancelInFlight, BeginDestroy).
	void DiscardInFlight(const TCHAR* Reason);

	TUniquePtr<rb::Simulator> Simulator;           // used by at most one task at a time; created on the first SubmitShot
	TUniquePtr<rb::ShotResult> WorkResult;          // reserved by the first Run (ReserveShotResult), reused across shots
	UE::Tasks::TTask<TSharedPtr<FRbShot>> InFlight; // valid while a shot is in flight
	bool bInFlight = false;
	bool bPrewarmed = false;
	bool bCollectWaitPending = false;               // the in-flight shot has not had its bounded Tick wait yet
	uint32 InFlightId = 0;
	uint32 NextShotId = 1;
	TSharedPtr<const FRbShot> LastShot;
	FRbSimulationStats Stats;
};
