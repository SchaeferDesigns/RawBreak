#include "Simulation/RbSimulationSubsystem.h"

#include "RawBreak.h"

#include "Simulation/RbSimScenarios.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"

// Owner: UE-6a. Tests: Private/Tests/RbSimulationTests.cpp (RawBreak.Unit.Simulation.*).

namespace RbSimulationSubsystemPrivate
{
	const TCHAR* StatusName(rb::SimStatus Status)
	{
		switch (Status)
		{
		case rb::SimStatus::Ok: return TEXT("Ok");
		case rb::SimStatus::InvalidInput: return TEXT("InvalidInput");
		case rb::SimStatus::Aborted: return TEXT("Aborted");
		case rb::SimStatus::HorizonReached: return TEXT("HorizonReached");
		case rb::SimStatus::NotImplemented: return TEXT("NotImplemented");
		}
		return TEXT("?");
	}
}

URbSimulationSubsystem::URbSimulationSubsystem() = default;

void URbSimulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.IsGameWorld())
	{
		Prewarm();
	}
}

void URbSimulationSubsystem::Prewarm()
{
	check(IsInGameThread());
	if (bPrewarmed || bInFlight)
	{
		return;
	}
	FRbShotRequest Request;
	FString Error;
	if (!RbSimScenarios::MakeBreak9(Request, Error))
	{
		UE_LOG(LogRawBreak, Warning, TEXT("RbSimulation: prewarm skipped (%s)"), *Error);
		return;
	}
	if (!Simulator.IsValid())
	{
		Simulator = MakeUnique<rb::Simulator>();
		WorkResult = MakeUnique<rb::ShotResult>();
	}
	FRbShot Shot;
	Shot.Request = MoveTemp(Request);
	Shot.Request.Input.Table = &Shot.Request.Table->Geometry;
	const double Start = FPlatformTime::Seconds();
	Simulate(*Simulator, *WorkResult, Shot);
	bPrewarmed = true;
	UE_LOG(LogRawBreak, Log, TEXT("RbSimulation: prewarmed with the break9 reference shot (%.2f ms simulation, %.2f ms total)"), Shot.SimMilliseconds,
		1000.0 * (FPlatformTime::Seconds() - Start));
}

uint32 URbSimulationSubsystem::SubmitShot(FRbShotRequest&& Request)
{
	check(IsInGameThread());
	if (bInFlight)
	{
		++Stats.Refused;
		UE_LOG(LogRawBreak, Warning, TEXT("SubmitShot refused: shot %u is still in flight"), InFlightId);
		return 0;
	}
	if (!Request.Table.IsValid())
	{
		++Stats.Refused;
		UE_LOG(LogRawBreak, Warning, TEXT("SubmitShot refused: the request has no table context"));
		return 0;
	}
	if (!Simulator.IsValid())
	{
		// Lazily: editor worlds get this subsystem too and never simulate. The first Run reserves WorkResult (~6 MB).
		Simulator = MakeUnique<rb::Simulator>();
		WorkResult = MakeUnique<rb::ShotResult>();
	}

	TSharedPtr<FRbShot> Shot = MakeShared<FRbShot>();
	Shot->Request = MoveTemp(Request);
	Shot->Request.Input.Table = &Shot->Request.Table->Geometry;
	Shot->Id = NextShotId++;
	Shot->SubmitFrame = GFrameCounter;

	// The worker gets plain pointers to the pooled Simulator / result (alive until the task is waited for: TryCollect,
	// CancelInFlight, Deinitialize, BeginDestroy) and the shot it exclusively owns until completion. Never `this`.
	rb::Simulator* Sim = Simulator.Get();
	rb::ShotResult* Work = WorkResult.Get();
	InFlight = UE::Tasks::Launch(TEXT("RbSimulateShot"),
		[Sim, Work, Shot]()
		{
			Simulate(*Sim, *Work, *Shot);
			return Shot;
		},
		UE::Tasks::ETaskPriority::High);
	bInFlight = true;
	bCollectWaitPending = true;
	InFlightId = Shot->Id;
	++Stats.Submitted;
	return Shot->Id;
}

bool URbSimulationSubsystem::IsBusy() const
{
	return bInFlight;
}

bool URbSimulationSubsystem::IsResultReady() const
{
	return bInFlight && InFlight.IsCompleted();
}

bool URbSimulationSubsystem::TryCollect(double MaxWaitSeconds)
{
	check(IsInGameThread());
	if (!bInFlight)
	{
		return false;
	}
	const bool bDone = MaxWaitSeconds > 0.0 ? InFlight.Wait(FTimespan::FromSeconds(MaxWaitSeconds)) : InFlight.IsCompleted();
	if (!bDone)
	{
		return false;
	}

	TSharedPtr<FRbShot> Shot = InFlight.GetResult();
	// Free the service BEFORE broadcasting: a listener may submit the next shot from its handler.
	InFlight = UE::Tasks::TTask<TSharedPtr<FRbShot>>();
	bInFlight = false;
	bCollectWaitPending = false;
	InFlightId = 0;
	check(Shot.IsValid());

	Shot->HandOffFrame = GFrameCounter;
	const bool bSameFrame = Shot->HandOffFrame == Shot->SubmitFrame;
	++Stats.HandedOff;
	Stats.SameFrameHandOffs += bSameFrame ? 1 : 0;
	Stats.RanOnWorker += Shot->bSimulatedOnWorker ? 1 : 0;
	Stats.LastSimMilliseconds = Shot->SimMilliseconds;
	Stats.MaxSimMilliseconds = FMath::Max(Stats.MaxSimMilliseconds, Shot->SimMilliseconds);
	Stats.TotalSimMilliseconds += Shot->SimMilliseconds;

	using RbSimulationSubsystemPrivate::StatusName;
	const rb::ShotResult& R = Shot->Result;
	if (R.Status == rb::SimStatus::Ok)
	{
		UE_LOG(LogRawBreak, Log, TEXT("Shot %u simulated in %.3f ms %s (%s, %d events, stop %.3f s, %.0f KB), hand-off %s (+%llu frames)"), Shot->Id,
			Shot->SimMilliseconds, Shot->bSimulatedOnWorker ? TEXT("on a worker") : TEXT("in place"), StatusName(R.Status),
			R.Diagnostics.EventsProcessed, R.StopTime, static_cast<double>(RbShot::FootprintBytes(*Shot)) / 1024.0,
			bSameFrame ? TEXT("in the submit frame") : TEXT("late"), Shot->HandOffFrame - Shot->SubmitFrame);
	}
	else
	{
		UE_LOG(LogRawBreak, Warning, TEXT("Shot %u: simulation status %s (input error %hs, %d events, stop %.3f s)"), Shot->Id, StatusName(R.Status),
			rb::ToString(R.Diagnostics.InputError), R.Diagnostics.EventsProcessed, R.StopTime);
	}

	const TSharedRef<const FRbShot> Done = Shot.ToSharedRef();
	LastShot = Done;
	OnShotSimulated.Broadcast(Done);
	return true;
}

void URbSimulationSubsystem::CancelInFlight()
{
	check(IsInGameThread());
	DiscardInFlight(TEXT("cancelled"));
}

void URbSimulationSubsystem::DiscardInFlight(const TCHAR* Reason)
{
	if (!bInFlight)
	{
		return;
	}
	InFlight.Wait(); // bounded: Simulator::Run stops at its MaxEvents / time-horizon guards
	InFlight = UE::Tasks::TTask<TSharedPtr<FRbShot>>();
	UE_LOG(LogRawBreak, Log, TEXT("Shot %u dropped without hand-off (%s)"), InFlightId, Reason);
	bInFlight = false;
	bCollectWaitPending = false;
	InFlightId = 0;
	++Stats.Discarded;
}

TSharedRef<FRbShot> URbSimulationSubsystem::RunShotBlocking(FRbShotRequest&& Request)
{
	TSharedRef<FRbShot> Shot = MakeShared<FRbShot>();
	Shot->Request = MoveTemp(Request);
	Shot->SubmitFrame = GFrameCounter;
	Shot->HandOffFrame = GFrameCounter;
	if (Shot->Request.Table.IsValid())
	{
		Shot->Request.Input.Table = &Shot->Request.Table->Geometry;
		rb::Simulator Sim;
		TUniquePtr<rb::ShotResult> Work = MakeUnique<rb::ShotResult>(); // large when reserved: heap, not stack
		Simulate(Sim, *Work, *Shot);
	}
	else
	{
		UE_LOG(LogRawBreak, Warning, TEXT("RunShotBlocking: the request has no table context"));
		Shot->Result.Status = rb::SimStatus::InvalidInput;
		Shot->Result.Diagnostics.InputError = rb::ErrorCode::InvalidArgument;
		Shot->InputHash = RbShot::InputHash(Shot->Request.Input);
		Shot->ResultHash = RbShot::ResultHash(Shot->Result);
	}
	return Shot;
}

void URbSimulationSubsystem::Simulate(rb::Simulator& Sim, rb::ShotResult& Work, FRbShot& Shot)
{
	Shot.bSimulatedOnWorker = !IsInGameThread();
	const double Start = FPlatformTime::Seconds();
	Sim.Run(Shot.Request.Input, Work);
	Shot.SimMilliseconds = 1000.0 * (FPlatformTime::Seconds() - Start);
	RbShot::CopyCompact(Work, Shot.Result);
	Shot.InputHash = RbShot::InputHash(Shot.Request.Input);
	Shot.ResultHash = RbShot::ResultHash(Shot.Result);
}

void URbSimulationSubsystem::Deinitialize()
{
	// World teardown with a shot in flight: wait for the worker before the Simulator / result it writes are destroyed,
	// and never broadcast into listeners that are being torn down.
	DiscardInFlight(TEXT("world teardown"));
	OnShotSimulated.Clear();
	LastShot.Reset();
	Simulator.Reset();
	WorkResult.Reset();
	bPrewarmed = false;
	Super::Deinitialize();
}

void URbSimulationSubsystem::BeginDestroy()
{
	// Deinitialize already waited; this only guards a subsystem destroyed without it (the worker must never outlive
	// the Simulator it writes into).
	DiscardInFlight(TEXT("destroyed"));
	Super::BeginDestroy();
}

void URbSimulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bInFlight)
	{
		// Wait (bounded) once per shot, at the first Tick after its submission: the submit frame, or the next frame for a
		// shot submitted after this Tick already ran (OnShotSimulated handler, later tick group, console command). The
		// wait also retracts a task no worker has started yet and runs it in place, so such a shot is never left to a
		// saturated worker pool. Afterwards poll without blocking the game thread. The flag is cleared BEFORE collecting:
		// a listener may submit the next shot from its OnShotSimulated handler, and that shot gets its own wait.
		const double Budget = bCollectWaitPending ? CollectBudgetSeconds : 0.0;
		bCollectWaitPending = false;
		TryCollect(Budget);
	}
}

bool URbSimulationSubsystem::IsTickable() const
{
	return bInFlight;
}

TStatId URbSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URbSimulationSubsystem, STATGROUP_Tickables);
}
