#pragma once

// Simulation service of a world (Docs/ue-architecture.md 5.2, 7): runs rb::Simulator::Run on a worker thread
// (UE::Tasks) and hands the immutable FRbShot back to the game thread. No UObject is touched off the game thread;
// the worker only sees the request (plain data + TSharedPtr<const FRbTableContext>) and a Simulator it owns
// exclusively for the duration of the task. Owner: UE-6a.
//
// Hand-off: SubmitShot at the tip contact (pawn tick, TG_PrePhysics) -> the subsystem's Tick (end of the world
// tick) waits up to CollectBudgetSeconds for the task (a break takes < 2 ms, plan 9.1), so the result is normally
// broadcast in the SAME frame; otherwise it is polled every frame. Playback is anchored at the contact time, so
// a late hand-off never changes what is shown, only when.
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

UCLASS()
class RAWBREAK_API URbSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	URbSimulationSubsystem();

	// Launches the shot on a worker. Returns the shot id (> 0), or 0 if a shot is still in flight or the request
	// is invalid (no table context). Game thread only.
	uint32 SubmitShot(FRbShotRequest&& Request);

	// True while a submitted shot has not been handed off yet.
	bool IsBusy() const;

	// Waits up to MaxWaitSeconds for the in-flight shot and broadcasts OnShotSimulated if it finished.
	// Returns true if a shot was handed off. Called by Tick; tests may call it directly.
	bool TryCollect(double MaxWaitSeconds);

	// Game-thread synchronous run (tests, tools, the ROB-10 check). Does not broadcast.
	static TSharedRef<FRbShot> RunShotBlocking(FRbShotRequest&& Request);

	TSharedPtr<const FRbShot> GetLastShot() const { return LastShot; }

	// Fired on the game thread once per submitted shot, in submission order.
	FRbOnShotSimulated OnShotSimulated;

	// Wait budget inside Tick for a same-frame hand-off [s].
	double CollectBudgetSeconds = 0.004;

	// UTickableWorldSubsystem
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	// Worker body: runs the simulation into the pooled reserved result, then compacts it into Shot.
	static void Simulate(rb::Simulator& Sim, rb::ShotResult& Work, FRbShot& Shot);

	TUniquePtr<rb::Simulator> Simulator;           // used by at most one task at a time
	TUniquePtr<rb::ShotResult> WorkResult;          // reserved once (ReserveShotResult), reused across shots
	UE::Tasks::TTask<TSharedPtr<FRbShot>> InFlight; // valid while a shot is in flight
	bool bInFlight = false;
	uint32 NextShotId = 1;
	TSharedPtr<const FRbShot> LastShot;
};
