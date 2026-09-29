#include "Audio/RbTableAudioComponent.h"

#include "Simulation/RbShot.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

// Owner: M2-C. Stub of the M2 architect step: binds the playback clock contract, builds no plans and creates no voices yet.

URbTableAudioComponent::URbTableAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Clock = MakeShared<RbAudio::FShotAudioClock, ESPMode::ThreadSafe>();
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
	}
	BoundPlayback = Playback;
	if (Playback)
	{
		StartedHandle = Playback->OnPlaybackStarted.AddUObject(this, &URbTableAudioComponent::HandlePlaybackStarted);
		ClockHandle = Playback->OnPlaybackClockChanged.AddUObject(this, &URbTableAudioComponent::HandlePlaybackClockChanged);
	}
}

void URbTableAudioComponent::BuildShotPlans(const FRbShot& /*Shot*/, const FRbTableContext& /*Context*/, const FTransform& /*TableToWorld*/,
	const FVector& /*ListenerWorld*/, ERbTableAudioTier /*Tier*/, TArray<RbAudio::FVoicePlan>& OutPlans)
{
	OutPlans.Reset(); // TODO(M2-C): events (audio.md 1.1, 2) -> voices (6.1), pulse parameters (3.2), gains, delays, noise segments
}

void URbTableAudioComponent::SetTier(ERbTableAudioTier NewTier)
{
	Tier = NewTier; // TODO(M2-C): create / destroy the tier's voices (audio.md 6.6)
}

void URbTableAudioComponent::HandlePlaybackStarted(const TSharedRef<const FRbShot>& Shot, const FRbPlaybackClock& Mapping)
{
	Clock->StartShot(static_cast<uint64>(Shot->ResultHash), Mapping.OriginClock, Mapping.OriginShotTime, Mapping.Rate, Mapping.bHeld, 0.0);
	// TODO(M2-C): build the plans on a worker and PushPlan them to the voices.
}

void URbTableAudioComponent::HandlePlaybackClockChanged(const TSharedRef<const FRbShot>& /*Shot*/, const FRbPlaybackClock& Mapping)
{
	Clock->SetMapping(Mapping.OriginClock, Mapping.OriginShotTime, Mapping.Rate, Mapping.bHeld);
}

void URbTableAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BindPlayback(nullptr);
	Clock->Stop();
	Super::EndPlay(EndPlayReason);
}
