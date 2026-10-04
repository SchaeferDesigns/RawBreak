#include "Ai/RbAiPlannerService.h"

#include "Engine/World.h"

// Owner: M3-O (Docs/ue-architecture.md 19.5). Plan-step stub: the service refuses every request; PlanBlocking (the serial reference
// of O1) is complete. Tests: RawBreak.Unit.Ai.* (M3-O).

URbAiPlannerService* URbAiPlannerService::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbAiPlannerService>() : nullptr;
}

uint32 URbAiPlannerService::RequestDecision(FRbAiRequest&& Request)
{
	(void)Request;
	return 0; // TODO(M3-O): Begin / RunJob / Advance on background UE::Tasks, progress snapshots, the wall-clock FinishNow escape
}

bool URbAiPlannerService::GetProgress(FRbAiProgress& Out) const
{
	(void)Out;
	return false; // TODO(M3-O)
}

void URbAiPlannerService::Cancel()
{
	ActiveRequest = 0; // TODO(M3-O): stop handing out jobs, wait for the running ones, drop the result
}

void URbAiPlannerService::SetWorkerCount(int32 Count)
{
	WorkerCount = Count;
}

rb::ai::PlannedDecision URbAiPlannerService::PlanBlocking(const FRbAiRequest& Request)
{
	rb::ai::PlannerScratch Scratch;
	return rb::ai::PlanShot(Request.Input, Request.Config, Scratch);
}

void URbAiPlannerService::Deinitialize()
{
	Cancel();
	Super::Deinitialize();
}

void URbAiPlannerService::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// TODO(M3-O): hand a finished decision to the game thread (OnDecisionReady), publish progress, the wall-clock escape.
}

bool URbAiPlannerService::IsTickable() const
{
	return ActiveRequest != 0;
}

TStatId URbAiPlannerService::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URbAiPlannerService, STATGROUP_Tickables);
}
