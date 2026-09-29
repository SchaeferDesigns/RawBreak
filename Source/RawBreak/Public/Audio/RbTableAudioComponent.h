#pragma once

// The audio of ONE table (Docs/specs/audio.md 6.1, 6.6, 8.2, 8.3; Docs/ue-architecture.md 18.5). Created at runtime by
// URbAudioSubsystem on every ARbTable of the world (no edit of ARbTable; several tables per level are the normal case):
//   * binds to the table's ball-set playback (URbTableSubsystem::FindBallSet): OnPlaybackStarted -> build the shot's voice
//     plans on a worker (events + tracks of FRbShot, listener geometry, seeded variation from the shot hash) -> PushPlan to the
//     voices + FShotAudioClock::StartShot; OnPlaybackClockChanged -> SetMapping (slow motion, pause, seek, world pause);
//     OnFinished -> the clock stops after the last tail;
//   * owns the table's voices: T0 (player's table or < 3 m) 16 ball + 6 rail + 6 pocket + body + cue = 30; T1 (3-10 m) 4
//     quadrant voices; T2 (> 10 m) 1 (audio.md 6.6). AU-0 may choose the 8-channel fallback of audio.md 5.1 instead of 30
//     point voices; the plan format stays the same;
//   * replays reuse the plan with the replay listener; replays of the player's table only (UI 2.4).
// Owner: M2-C (stub by the M2 architect step; TODO(M2-C)).

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Audio/RbImpactVoiceComponent.h"
#include "Balls/RbShotPlaybackComponent.h"

#include "RbTableAudioComponent.generated.h"

class ARbTable;
struct FRbTableContext;
struct FRbShot;

UENUM()
enum class ERbTableAudioTier : uint8
{
	T0, // full synthesis, 30 voices
	T1, // 4 quadrant voices
	T2, // 1 voice at the table centre
	Off,
};

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbTableAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbTableAudioComponent();

	// Binds to the playback of this table's ball set (idempotent; null unbinds).
	void BindPlayback(URbShotPlaybackComponent* Playback);

	// The voice plans of a shot for a listener (pure; worker-safe; tests). One plan per voice of the tier.
	static void BuildShotPlans(const FRbShot& Shot, const FRbTableContext& Context, const FTransform& TableToWorld,
		const FVector& ListenerWorld, ERbTableAudioTier Tier, TArray<RbAudio::FVoicePlan>& OutPlans);

	void SetTier(ERbTableAudioTier NewTier);
	ERbTableAudioTier GetTier() const { return Tier; }

	FRbShotAudioClockPtr GetClock() const { return Clock; }
	ARbTable* GetTable() const;

	// UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	void HandlePlaybackStarted(const TSharedRef<const FRbShot>& Shot, const FRbPlaybackClock& Mapping);
	void HandlePlaybackClockChanged(const TSharedRef<const FRbShot>& Shot, const FRbPlaybackClock& Mapping);

	UPROPERTY(Transient)
	TArray<TObjectPtr<URbImpactVoiceComponent>> Voices;

	TWeakObjectPtr<URbShotPlaybackComponent> BoundPlayback;
	FDelegateHandle StartedHandle;
	FDelegateHandle ClockHandle;
	FRbShotAudioClockPtr Clock;
	ERbTableAudioTier Tier = ERbTableAudioTier::Off;
};
