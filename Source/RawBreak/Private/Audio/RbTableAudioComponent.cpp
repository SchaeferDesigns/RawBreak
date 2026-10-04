#include "Audio/RbTableAudioComponent.h"

#include "Balls/RbBallSet.h"
#include "Core/RbCoords.h"
#include "Simulation/RbShot.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

#include "AudioDevice.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Tasks/Task.h"

// Owner: M2-C.

URbTableAudioComponent::URbTableAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after the playback moved the balls this frame
	Clock = MakeShared<RbAudio::FShotAudioClock, ESPMode::ThreadSafe>();
	PlanShared = MakeShared<FPlanShared, ESPMode::ThreadSafe>();
}

ARbTable* URbTableAudioComponent::GetTable() const
{
	return Cast<ARbTable>(GetOwner());
}

void URbTableAudioComponent::BindPlayback(URbShotPlaybackComponent* Playback)
{
	if (BoundPlayback.Get() == Playback)
	{
		return;
	}
	if (URbShotPlaybackComponent* Old = BoundPlayback.Get())
	{
		Old->OnPlaybackStarted.Remove(StartedHandle);
		Old->OnPlaybackClockChanged.Remove(ClockHandle);
		Old->OnFinished.Remove(FinishedHandle);
	}
	BoundPlayback = Playback;
	if (Playback)
	{
		StartedHandle = Playback->OnPlaybackStarted.AddUObject(this, &URbTableAudioComponent::HandlePlaybackStarted);
		ClockHandle = Playback->OnPlaybackClockChanged.AddUObject(this, &URbTableAudioComponent::HandlePlaybackClockChanged);
		FinishedHandle = Playback->OnFinished.AddUObject(this, &URbTableAudioComponent::HandlePlaybackFinished);
	}
}

void URbTableAudioComponent::BuildShotPlans(const FRbShot& Shot, const FRbTableContext& Context, const FTransform& TableToWorld, const FVector& InListenerWorld,
	ERbTableAudioTier InTier, TArray<RbAudio::FVoicePlan>& OutPlans)
{
	const FVector LocalCm = TableToWorld.InverseTransformPosition(InListenerWorld);
	const rb::Vec3 ListenerCore = FRbCoords::PositionToCore(LocalCm);
	FRbAudioPlanOptions Options;
	Options.Tier = InTier;
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	Options.RefDistance = Settings->RefDistanceMeters;
	Options.DirectivityFloorDb = Settings->DirectivityFloorDb;
	Options.PanCompensation = Settings->PanCompensation;
	Options.Mode = static_cast<RbAudio::EDynamicRangeMode>(Settings->DynamicRange);
	Options.Seed = Shot.ResultHash;
	FRbShotAudioPlan Plan;
	FRbAudioPlanBuilder::Build(Shot.Result, Context, ListenerCore, Options, Plan);
	OutPlans = MoveTemp(Plan.Voices);
}

void URbTableAudioComponent::SetRouting(const FRbTableAudioRouting& InRouting)
{
	Routing = InRouting;
}

void URbTableAudioComponent::SetReverbSendGain(float Gain)
{
	Routing.ReverbSendGain = Gain;
	for (URbImpactVoiceComponent* Voice : Voices)
	{
		if (Voice)
		{
			Voice->SetReverbSendGain(Gain);
		}
	}
}

void URbTableAudioComponent::SetProfile(FRbAudioRenderProfilePtr InProfile)
{
	Profile = MoveTemp(InProfile);
	for (URbImpactVoiceComponent* Voice : Voices)
	{
		if (Voice)
		{
			Voice->SetProfile(Profile);
		}
	}
}

void URbTableAudioComponent::SetTier(ERbTableAudioTier NewTier)
{
	PendingTier = NewTier;
	const bool bSounding = IsPlanPending() || bClockRunning || (BoundPlayback.IsValid() && BoundPlayback->IsPlaying());
	// A tier whose voices could not be created yet (the table's context appears in its BeginPlay, which can run after the audio
	// subsystem's OnWorldBeginPlay) is completed as soon as the context exists (TickComponent retries).
	const bool bComplete = Voices.Num() == FRbAudioPlanBuilder::NumVoices(NewTier);
	if ((NewTier == Tier && bComplete) || (bSounding && Voices.Num() > 0))
	{
		return;
	}
	DestroyVoices();
	Tier = NewTier;
	CreateVoices();
}

void URbTableAudioComponent::CreateVoices()
{
	ARbTable* Table = GetTable();
	const int32 Count = FRbAudioPlanBuilder::NumVoices(Tier);
	if (!Table || !Table->HasContext() || Count == 0)
	{
		return;
	}
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	FRbAudioPlanBuilder::EmitterPositions(Table->GetContext(), Tier, EmitterPositionsCore);
	// The shot-independent parts of the plans (kernels of every ball, shape tables) on a worker now, not in the first shot's plan.
	{
		double Rate = 48000.0;
		if (const UWorld* World = GetWorld())
		{
			if (const FAudioDevice* Device = World->GetAudioDeviceRaw())
			{
				Rate = Device->GetSampleRate();
			}
		}
		UE::Tasks::Launch(TEXT("RbAudioPrewarm"), [Context = Table->GetContextPtr(), Rate]()
		{
			if (Context.IsValid())
			{
				FRbAudioPlanBuilder::Prewarm(*Context, Rate);
			}
		}, LowLevelTasks::ETaskPriority::BackgroundNormal);
	}
	for (int32 V = 0; V < Count; ++V)
	{
		const FName Name = MakeUniqueObjectName(Table, URbImpactVoiceComponent::StaticClass(),
			FName(*FString::Printf(TEXT("RbVoice_T%d_%s"), Table->TableIndex, FRbAudioPlanBuilder::VoiceName(Tier, V))));
		URbImpactVoiceComponent* Voice = NewObject<URbImpactVoiceComponent>(Table, Name, RF_Transient);
		Voice->SetupAttachment(Table->GetRootComponent());
		Voice->ConfigureVoice(true, Routing.TableSubmix, Routing.ReverbSubmix, Routing.ReverbSend, Settings->RefDistanceMeters, Settings->AttenuationRangeMeters);
		Voice->SetReverbSendGain(Routing.ReverbSendGain);
		Voice->SetClock(Clock);
		Voice->SetProfile(Profile);
		FRbVoiceSharedPtr Shared = Voice->GetShared();
		Shared->OutputLatencySeconds = Settings->OutputLatencySeconds;
		Shared->LeadMinBlocks = Settings->LeadMinBlocks;
		Shared->LeadMarginFrames = Settings->LeadMarginFrames;
		Voice->RegisterComponent();
		Voice->SetWorldLocation(Table->CoreToWorld(EmitterPositionsCore[V]));
		Voice->StartVoice();
		Voices.Add(Voice);
	}
}

void URbTableAudioComponent::DestroyVoices()
{
	for (URbImpactVoiceComponent* Voice : Voices)
	{
		if (Voice)
		{
			Voice->Stop();
			Voice->DestroyComponent();
		}
	}
	Voices.Reset();
}

FVector URbTableAudioComponent::ListenerWorld() const
{
	if (ListenerOverride.IsSet())
	{
		return ListenerOverride.GetValue();
	}
	if (const UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector Location, Front, Right;
			PC->GetAudioListenerPosition(Location, Front, Right);
			return Location;
		}
	}
	return FVector::ZeroVector;
}

double URbTableAudioComponent::VisualLatencySeconds() const
{
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	const double Frame = FMath::Clamp(FApp::GetDeltaTime(), 1.0 / 240.0, 0.1);
	return Settings->VisualLatencyFrames * Frame + Settings->AvOffsetSeconds;
}

bool URbTableAudioComponent::IsPlanPending() const
{
	FScopeLock Guard(&PlanShared->Lock);
	return PlanShared->bPending;
}

int32 URbTableAudioComponent::GetPlansBuilt() const
{
	FScopeLock Guard(&PlanShared->Lock);
	return PlanShared->PlansBuilt;
}

TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> URbTableAudioComponent::GetLastPlan() const
{
	FScopeLock Guard(&PlanShared->Lock);
	return PlanShared->LastPlan;
}

void URbTableAudioComponent::HandlePlaybackStarted(const TSharedRef<const FRbShot>& Shot, const FRbPlaybackClock& Mapping)
{
	ARbTable* Table = GetTable();
	const TSharedPtr<const FRbTableContext> Context = Shot->Request.Table.IsValid() ? Shot->Request.Table : (Table && Table->HasContext() ? Table->GetContextPtr() : nullptr);
	if (!Table || !Context.IsValid() || Voices.Num() == 0)
	{
		return;
	}
	const uint64 Serial = ++PlaySerial;
	bFinished = false;
	bLastLive = Mapping.bLive;
	LastMapping = Mapping;
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	FRbAudioPlanOptions Options;
	Options.Tier = Tier;
	Options.SampleRate = 48000.0;
	if (const UWorld* World = GetWorld())
	{
		if (const FAudioDevice* Device = World->GetAudioDeviceRaw())
		{
			Options.SampleRate = Device->GetSampleRate();
		}
	}
	Options.Mode = static_cast<RbAudio::EDynamicRangeMode>(Settings->DynamicRange);
	Options.RefDistance = Settings->RefDistanceMeters;
	Options.DirectivityFloorDb = Settings->DirectivityFloorDb;
	Options.PanCompensation = Settings->PanCompensation;
	Options.ShotId = Serial;
	Options.Seed = Shot->ResultHash;
	// The emitters of the new shot before its first sound: the worker pushes the plan and starts the clock, and the tip strike can
	// render before the next game-thread tick moves the voices (UpdateVoicePositions): the cue voice at the strike, the ball voices
	// at their balls (the playback shows the shot's first pose now).
	if (Tier == ERbTableAudioTier::T0 && Voices.IsValidIndex(FRbAudioPlanBuilder::CueVoice) && Voices[FRbAudioPlanBuilder::CueVoice])
	{
		rb::Vec3 CueCore;
		if (FRbAudioPlanBuilder::CuePointCore(Shot->Result, CueCore))
		{
			Voices[FRbAudioPlanBuilder::CueVoice]->SetWorldLocation(Table->CoreToWorld(CueCore));
			if (EmitterPositionsCore.IsValidIndex(FRbAudioPlanBuilder::CueVoice))
			{
				EmitterPositionsCore[FRbAudioPlanBuilder::CueVoice] = CueCore;
			}
		}
	}
	UpdateVoicePositions();
	const FVector ListenerAt = ListenerWorld();
	const rb::Vec3 ListenerCore = Table->WorldToCore(ListenerAt);
	if (const UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector Location, Front, Right;
			PC->GetAudioListenerPosition(Location, Front, Right);
			const rb::Vec3 R = Table->WorldToCore(ListenerAt + Right.GetSafeNormal() * 100.0) - ListenerCore;
			const double L = rb::Length(R);
			Options.ListenerRightCore = L > 1e-6 ? R / L : rb::Vec3(0.0, 0.0, 0.0);
		}
	}
	TArray<FRbVoiceSharedPtr> VoiceShared;
	for (URbImpactVoiceComponent* Voice : Voices)
	{
		VoiceShared.Add(Voice ? Voice->GetShared() : nullptr);
	}
	{
		FScopeLock Guard(&PlanShared->Lock);
		PlanShared->bPending = true;
		PlanShared->PendingSerial = Serial;
		PlanShared->LatestMapping = Mapping;
		PlanShared->VisualLatency = VisualLatencySeconds();
		PlanShared->bStopped = false;
	}
	TSharedPtr<FPlanShared, ESPMode::ThreadSafe> Shared = PlanShared;
	FRbShotAudioClockPtr ClockPtr = Clock;
	auto Task = [Shared, ClockPtr, VoiceShared, Shot, Context, ListenerCore, Options, Serial]()
	{
		TSharedPtr<FRbShotAudioPlan, ESPMode::ThreadSafe> Plan = MakeShared<FRbShotAudioPlan, ESPMode::ThreadSafe>();
		FRbAudioPlanBuilder::Build(Shot->Result, *Context, ListenerCore, Options, *Plan);
		{
			FScopeLock Guard(&Shared->Lock);
			if (Shared->PendingSerial != Serial)
			{
				return; // a newer shot replaced this one
			}
		}
		// Plans first, then the clock (a voice that already sees the new shot id waits for its plan: AU-0 ordering).
		for (int32 V = 0; V < VoiceShared.Num() && V < Plan->Voices.Num(); ++V)
		{
			URbImpactVoiceComponent::PushPlan(VoiceShared[V], MakeShared<const RbAudio::FVoicePlan, ESPMode::ThreadSafe>(Plan->Voices[V]));
		}
		FScopeLock Guard(&Shared->Lock);
		if (Shared->PendingSerial != Serial)
		{
			return;
		}
		if (!Shared->bStopped)
		{
			const FRbPlaybackClock& M = Shared->LatestMapping;
			ClockPtr->StartShot(Serial, M.OriginClock, M.OriginShotTime, M.Rate, M.bHeld, Shared->VisualLatency);
		}
		Shared->bPending = false;
		Shared->LastPlan = Plan;
		++Shared->PlansBuilt;
	};
	if (bSynchronousPlans)
	{
		Task();
	}
	else
	{
		UE::Tasks::Launch(TEXT("RbAudioPlan"), MoveTemp(Task), LowLevelTasks::ETaskPriority::High);
	}
	bClockRunning = true;
}

void URbTableAudioComponent::HandlePlaybackFinished(const TSharedRef<const FRbShot>& /*Shot*/)
{
	if (!bClockRunning)
	{
		return;
	}
	bFinished = true;
	FinishedShotTime = BoundPlayback.IsValid() ? BoundPlayback->GetShotTime() : 0.0;
	FinishedRealSeconds = FPlatformTime::Seconds();
}

double URbTableAudioComponent::PlanEndShotTime(const FRbShotAudioPlan& Plan)
{
	constexpr double ImpactTail = 0.6; // the longest structural tail (bar-box rail bank, -80 dB) and the propagation
	double End = -1.0;
	for (const RbAudio::FVoicePlan& Voice : Plan.Voices)
	{
		if (Voice.Impacts.Num() > 0)
		{
			End = FMath::Max(End, Voice.Impacts.Last().ShotTime + ImpactTail);
		}
		for (const RbAudio::FContinuousSegment& Segment : Voice.Continuous)
		{
			End = FMath::Max(End, Segment.EndTime);
		}
	}
	return End;
}

bool URbTableAudioComponent::IsPlayingReplay() const
{
	return !bLastLive && BoundPlayback.IsValid() && BoundPlayback->IsPlaying();
}

void URbTableAudioComponent::HandlePlaybackClockChanged(const TSharedRef<const FRbShot>& /*Shot*/, const FRbPlaybackClock& Mapping)
{
	LastMapping = Mapping;
	FScopeLock Guard(&PlanShared->Lock);
	if (PlanShared->bPending)
	{
		PlanShared->LatestMapping = Mapping;
	}
	else
	{
		Clock->SetMapping(Mapping.OriginClock, Mapping.OriginShotTime, Mapping.Rate, Mapping.bHeld);
	}
}

void URbTableAudioComponent::UpdateVoicePositions()
{
	ARbTable* Table = GetTable();
	if (!Table || !Table->HasContext())
	{
		return;
	}
	// A new plan: its emitters (the cue voice sits where the strike happened).
	TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> Last = GetLastPlan();
	if (Last.IsValid() && Last != AppliedPlan)
	{
		AppliedPlan = Last;
		if (Last->VoicePositionsCore.Num() == Voices.Num())
		{
			EmitterPositionsCore = Last->VoicePositionsCore;
			for (int32 V = 0; V < Voices.Num(); ++V)
			{
				if (Voices[V])
				{
					Voices[V]->SetWorldLocation(Table->CoreToWorld(EmitterPositionsCore[V]));
				}
			}
		}
	}
	// Ball voices follow their balls (T0).
	if (Tier == ERbTableAudioTier::T0)
	{
		const ARbBallSet* Balls = BoundPlayback.IsValid() ? Cast<ARbBallSet>(BoundPlayback->GetOwner()) : nullptr;
		if (Balls)
		{
			const int32 Count = FMath::Min(Balls->GetBallCount(), FRbAudioPlanBuilder::NumBallVoices);
			for (int32 B = 0; B < Count && B < Voices.Num(); ++B)
			{
				const UStaticMeshComponent* Mesh = Balls->GetBallComponent(B);
				if (Mesh && Voices[B] && Balls->IsBallVisible(B))
				{
					Voices[B]->SetWorldLocation(Mesh->GetComponentLocation());
				}
			}
		}
	}
}

void URbTableAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateVoicePositions();
	// A playback that was stopped: stop the clock (no new events; the rendered tails ring out). A playback that finished: the clock
	// runs on until the plan's tail (gully runs, trap clicks) has played at the playback's rate.
	const bool bPlaying = BoundPlayback.IsValid() && BoundPlayback->IsPlaying();
	if (bClockRunning && !bPlaying)
	{
		bool bStop = true;
		if (bFinished)
		{
			const TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> Plan = GetLastPlan();
			const double End = Plan.IsValid() ? PlanEndShotTime(*Plan) : -1.0;
			const double Rate = FMath::Max(static_cast<double>(LastMapping.Rate), 1e-3);
			bStop = LastMapping.bHeld || End <= FinishedShotTime || FPlatformTime::Seconds() - FinishedRealSeconds > (End - FinishedShotTime) / Rate + 0.1;
		}
		if (bStop)
		{
			FScopeLock Guard(&PlanShared->Lock);
			if (!PlanShared->bPending)
			{
				Clock->Stop();
				bClockRunning = false;
				bFinished = false;
			}
		}
	}
	const bool bIncomplete = Voices.Num() != FRbAudioPlanBuilder::NumVoices(PendingTier);
	if ((PendingTier != Tier || bIncomplete) && !bPlaying && !bClockRunning && !IsPlanPending())
	{
		const ARbTable* Table = GetTable();
		if (PendingTier != Tier || (Table && Table->HasContext()))
		{
			SetTier(PendingTier);
		}
	}
}

void URbTableAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BindPlayback(nullptr);
	{
		FScopeLock Guard(&PlanShared->Lock);
		PlanShared->PendingSerial = 0;
		PlanShared->bPending = false;
		Clock->Stop();
	}
	DestroyVoices();
	Super::EndPlay(EndPlayReason);
}
