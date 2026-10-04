#pragma once

// The AI planner on worker threads (Docs/ue-architecture.md 19.5; architecture.md 7.6). One rb::ai::PlannerScratch per running
// decision and N rb::ai::PlannerWorkers (N = clamp(worker threads - 1, 1, 6)), allocated on the first request and reused. The
// PlannerScratch protocol on UE::Tasks with BACKGROUND priority: Begin on a worker task, per stage JobCount() jobs over the N workers
// (job j on worker j mod N), join, Advance on a worker task; after every Advance a PlannerProgress snapshot is published for the game
// thread. Wall-clock escape: after MaxComputeSeconds (> 0) no further jobs are handed out, the running ones finish and FinishNow
// decides (non-deterministic, logged, never used in tests). The game thread never waits: OnDecisionReady fires in the tick after the
// decision finished. Deinitialize / Cancel stop handing out jobs and wait for the running ones (bounded by one job).
// Determinism: without the escape the decision is bitwise the serial rb::ai::PlanShot for any worker count (O1).
// Owner: M3-O. Plan-step stub: refuses every request (TODO(M3-O)); PlanBlocking is complete (the serial reference).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Simulation/RbTableContext.h"

#include "rb/Ai/Planner.h"

#include "RbAiPlannerService.generated.h"

// One decision request (filled on the game thread from the director: state copies, the AI's character and equipment).
struct FRbAiRequest
{
	int32 TableIndex = 0;                          // the session (multi-table ready)
	TSharedPtr<const FRbTableContext> Table;       // keeps Input.Table (the geometry) alive while the decision runs
	rb::ai::PlannerInput Input;
	rb::ai::PlannerConfig Config;
	double MaxComputeSeconds = 0.0;                // wall-clock escape (> 0: FinishNow after this long; 0 = off, deterministic)
};

// The progress snapshot of the running decision (published between stages).
struct FRbAiProgress
{
	uint32 RequestId = 0;
	rb::ai::PlannerProgress Progress;
	double ElapsedSeconds = 0.0;
	bool bFinishedEarly = false;                   // the wall-clock escape fired
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnAiDecisionReady, uint32 /*RequestId*/, const rb::ai::PlannedDecision& /*Decision*/);

UCLASS()
class RAWBREAK_API URbAiPlannerService : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static URbAiPlannerService* Get(const UObject* WorldContext);

	// Starts a decision on the workers. Returns its id (> 0), or 0 when refused (busy, invalid input).
	uint32 RequestDecision(FRbAiRequest&& Request);
	bool IsBusy() const { return ActiveRequest != 0; }
	// The latest snapshot of the running decision; false when none runs or no stage finished yet.
	bool GetProgress(FRbAiProgress& Out) const;
	// Drops the running decision (nothing is delivered for it); stops handing out jobs and waits for the running ones.
	void Cancel();

	// Worker count (tests: 1 / 3 / 6); <= 0 = automatic. Only between decisions.
	void SetWorkerCount(int32 Count);
	int32 GetWorkerCount() const { return WorkerCount; }

	// The serial reference (rb::ai::PlanShot on the calling thread with a fresh scratch): tests (O1), tools.
	static rb::ai::PlannedDecision PlanBlocking(const FRbAiRequest& Request);

	FRbOnAiDecisionReady OnDecisionReady;

	// UTickableWorldSubsystem
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

protected:
	uint32 ActiveRequest = 0;
	uint32 NextRequestId = 1;
	int32 WorkerCount = 0;
};
