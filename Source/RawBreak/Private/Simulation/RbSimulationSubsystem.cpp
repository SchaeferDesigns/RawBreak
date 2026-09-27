#include "Simulation/RbSimulationSubsystem.h"

#include "RawBreak.h"

#include "HAL/PlatformTime.h"

// Owner: UE-6a. TODO(UE-6a): worker launch, collect, compact copy, timing, error logging, tests
// (RawBreak.Unit.Simulation.*: same-frame hand-off, determinism vs RunShotBlocking, ROB-10 hash).

URbSimulationSubsystem::URbSimulationSubsystem() = default;

uint32 URbSimulationSubsystem::SubmitShot(FRbShotRequest&& Request)
{
	if (bInFlight || !Request.Table.IsValid())
	{
		return 0;
	}
	// TODO(UE-6a): launch Simulate() with UE::Tasks::Launch on a worker; keep the task in InFlight.
	TSharedRef<FRbShot> Shot = RunShotBlocking(MoveTemp(Request));
	Shot->Id = NextShotId++;
	LastShot = Shot;
	OnShotSimulated.Broadcast(Shot);
	return Shot->Id;
}

bool URbSimulationSubsystem::IsBusy() const
{
	return bInFlight;
}

bool URbSimulationSubsystem::TryCollect(double /*MaxWaitSeconds*/)
{
	return false; // TODO(UE-6a)
}

TSharedRef<FRbShot> URbSimulationSubsystem::RunShotBlocking(FRbShotRequest&& Request)
{
	TSharedRef<FRbShot> Shot = MakeShared<FRbShot>();
	Shot->Request = MoveTemp(Request);
	if (Shot->Request.Table.IsValid())
	{
		Shot->Request.Input.Table = &Shot->Request.Table->Geometry;
		rb::Simulator Sim;
		rb::ShotResult Work;
		Simulate(Sim, Work, *Shot);
	}
	return Shot;
}

void URbSimulationSubsystem::Simulate(rb::Simulator& Sim, rb::ShotResult& Work, FRbShot& Shot)
{
	const double Start = FPlatformTime::Seconds();
	Sim.Run(Shot.Request.Input, Work);
	Shot.SimMilliseconds = 1000.0 * (FPlatformTime::Seconds() - Start);
	RbShot::CopyCompact(Work, Shot.Result);
	Shot.ResultHash = RbShot::ResultHash(Shot.Result);
}

void URbSimulationSubsystem::Deinitialize()
{
	// TODO(UE-6a): wait for an in-flight task before the Simulator is destroyed.
	Super::Deinitialize();
}

void URbSimulationSubsystem::Tick(float /*DeltaTime*/)
{
	if (bInFlight)
	{
		TryCollect(CollectBudgetSeconds);
	}
}

TStatId URbSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URbSimulationSubsystem, STATGROUP_Tickables);
}
