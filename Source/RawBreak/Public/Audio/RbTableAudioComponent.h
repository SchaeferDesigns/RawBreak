#pragma once

// The audio of ONE table (Docs/specs/audio.md 6.1, 6.6, 8.2, 8.3; Docs/ue-architecture.md 18.5). Created at runtime by
// URbAudioSubsystem on every ARbTable of the world (no edit of ARbTable; several tables per level are the normal case):
//   * binds to the table's ball-set playback: OnPlaybackStarted -> the shot's voice plans are built on a worker (events + tracks,
//     listener geometry at that moment, variation seeded from the shot hash so replays render identically), handed to the voices
//     and the table's FShotAudioClock is started with the playback's mapping (the worker does both, no frame of latency);
//     OnPlaybackClockChanged -> SetMapping (slow motion, pause, seek, world pause); a playback that is stopped (Stop, a new Play)
//     stops the clock at once (the rendered tails ring out); a playback that FINISHES (OnFinished: the balls came to rest) keeps the
//     clock running until the plan's own tail has played: the coin-op gully runs and trap clicks of the last pocketed balls last up
//     to 3 s beyond the shot's stop time;
//   * owns the table's voices by tier: T0 (player's table or < 3 m) 16 ball + 6 rail + 6 pocket + body + cue = 30; T1 (3-10 m) 4
//     quadrant voices; T2 (> 10 m) 1 (audio.md 6.6). Ball voices follow their balls every frame; the others sit at their emitters;
//   * plus ONE reverb-feed voice per table (any tier, when the venue has a reverb): non-spatialised, send-only (no base submix
//     output), rendering FRbShotAudioPlan::ReverbFeed into the venue reverb, so the room is excited by the radiated power of each
//     sound and not by the listener's directional signal (audio.md 3.6 / 6.4). The table voices carry no reverb send;
//   * replays reuse the same path with the replay listener (the camera of that moment).
// Owner: M2-C.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Audio/RbAudioPlan.h"
#include "Audio/RbAudioSettings.h"
#include "Audio/RbImpactVoiceComponent.h"
#include "Balls/RbShotPlaybackComponent.h"

#include "RbTableAudioComponent.generated.h"

class ARbBallSet;
class ARbTable;
class USoundSubmixBase;
struct FRbShot;

// Routing of the table's voices (from the audio subsystem).
struct FRbTableAudioRouting
{
	USoundSubmixBase* TableSubmix = nullptr;
	USoundSubmixBase* ReverbSubmix = nullptr;
	float ReverbSend = 0.25f;
	float ReverbSendGain = 1.0f; // the Table bus volume on the reverb send (URbAudioSubsystem::ApplyVolumes)
};

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbTableAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbTableAudioComponent();

	// Binds to the playback of this table's ball set (idempotent; null unbinds).
	void BindPlayback(URbShotPlaybackComponent* Playback);
	URbShotPlaybackComponent* GetBoundPlayback() const { return BoundPlayback.Get(); }

	// The voice plans of a shot for a listener (pure; worker-safe; tests). ListenerWorld in world space.
	static void BuildShotPlans(const FRbShot& Shot, const FRbTableContext& Context, const FTransform& TableToWorld, const FVector& ListenerWorld,
		ERbTableAudioTier Tier, TArray<RbAudio::FVoicePlan>& OutPlans);

	// Creates / replaces the voices of a tier (only while no shot is sounding; otherwise the change waits).
	void SetTier(ERbTableAudioTier NewTier);
	ERbTableAudioTier GetTier() const { return Tier; }
	// New routing (submixes, reverb). Voices take it when they are created; existing voices are created again as soon as no shot is
	// sounding (a venue change during a shot).
	void SetRouting(const FRbTableAudioRouting& InRouting);
	// The Table bus volume on the voices' reverb send (now and for voices created later).
	void SetReverbSendGain(float Gain);

	FRbShotAudioClockPtr GetClock() const { return Clock; }
	ARbTable* GetTable() const;
	const TArray<TObjectPtr<URbImpactVoiceComponent>>& GetVoices() const { return Voices; }
	// The send-only voice that feeds the venue reverb with the table's radiated power (null without a reverb submix).
	URbImpactVoiceComponent* GetReverbFeedVoice() const { return ReverbFeedVoice.Get(); }

	// Listener used for the next plans (world); unset = the local player's audio listener.
	void SetListenerOverride(TOptional<FVector> InListener) { ListenerOverride = InListener; }
	// Build plans on the game thread (tests: deterministic ordering); default: a worker task.
	void SetSynchronousPlans(bool bSynchronous) { bSynchronousPlans = bSynchronous; }
	// Per-block render times of this table's voices (T19); null disables.
	void SetProfile(FRbAudioRenderProfilePtr InProfile);

	// The plan of the last shot (after the worker finished; game thread).
	TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> GetLastPlan() const;
	int32 GetPlansBuilt() const;
	bool IsPlanPending() const;
	// Clock id of the last started shot (one per Play).
	uint64 GetPlaySerial() const { return PlaySerial; }
	// A replay (not a live shot) is playing on this table (the replay mix of URbAudioSubsystem).
	bool IsPlayingReplay() const;
	// The last playback mapping this table's clock follows (tests).
	const FRbPlaybackClock& GetLastMapping() const { return LastMapping; }

	// UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	void HandlePlaybackStarted(const TSharedRef<const FRbShot>& Shot, const FRbPlaybackClock& Mapping);
	void HandlePlaybackClockChanged(const TSharedRef<const FRbShot>& Shot, const FRbPlaybackClock& Mapping);
	void HandlePlaybackFinished(const TSharedRef<const FRbShot>& Shot);
	// Shot time up to which the last plan has sound (last impact + its tail, last continuous segment); -1 without a plan.
	static double PlanEndShotTime(const FRbShotAudioPlan& Plan);
	void CreateVoices();
	void DestroyVoices();
	void UpdateVoicePositions();
	FVector ListenerWorld() const;
	double VisualLatencySeconds() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<URbImpactVoiceComponent>> Voices;

	UPROPERTY(Transient)
	TObjectPtr<URbImpactVoiceComponent> ReverbFeedVoice;
	// SetRouting changed the routing while the table was sounding: the voices are created again once it is quiet.
	bool bRoutingDirty = false;

	TWeakObjectPtr<URbShotPlaybackComponent> BoundPlayback;
	FDelegateHandle StartedHandle;
	FDelegateHandle ClockHandle;
	FDelegateHandle FinishedHandle;
	bool bFinished = false;          // the bound playback finished (OnFinished) and the plan's tail plays on
	double FinishedShotTime = 0.0;   // shot time of the finish
	double FinishedRealSeconds = 0.0;
	FRbShotAudioClockPtr Clock;
	ERbTableAudioTier Tier = ERbTableAudioTier::Off;
	ERbTableAudioTier PendingTier = ERbTableAudioTier::Off;
	FRbTableAudioRouting Routing;
	TOptional<FVector> ListenerOverride;
	bool bSynchronousPlans = false;
	FRbAudioRenderProfilePtr Profile;
	uint64 PlaySerial = 0;
	bool bClockRunning = false;
	bool bLastLive = true;
	FRbPlaybackClock LastMapping;
	TArray<rb::Vec3> EmitterPositionsCore;

	// Shared with the plan worker (the clock is written under this lock, so it always has one writer at a time).
	struct FPlanShared
	{
		FCriticalSection Lock;
		bool bPending = false;
		uint64 PendingSerial = 0;
		FRbPlaybackClock LatestMapping;
		double VisualLatency = 0.0;
		bool bStopped = false;
		TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> LastPlan;
		int32 PlansBuilt = 0;
	};
	TSharedPtr<FPlanShared, ESPMode::ThreadSafe> PlanShared;
	TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> AppliedPlan; // the plan whose emitters the voices use
};
