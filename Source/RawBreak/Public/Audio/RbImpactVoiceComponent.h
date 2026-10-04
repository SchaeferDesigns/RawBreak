#pragma once

// One table / foley voice (Docs/specs/audio.md 5.1, 6.1, 8.2; Docs/ue-architecture.md 18.5): a USynthComponent whose
// ISoundGenerator runs an RbAudio::FVoiceRenderer SAMPLE-ACCURATELY from the per-table FShotAudioClock: one callback per device
// block (GetDesiredNumSamplesToRenderPerCallback = AudioMixerNumOutputFrames x NumChannels), the block's device frame read from
// the device's audio clock inside the callback (FAudioDevice::GetAudioClock: the start of the block being rendered; AU-0), pitch
// 1, no Doppler, bIsUISound (keeps rendering while the game is paused: the pause mix filters it, the held clock silences the
// table), bAlwaysPlay. Voices run continuously while their table is in an audio tier (an idle voice writes zeros).
//
// Commands (plans, live one-shots, live rolling, gains) go through a thread-safe queue shared with the generator, so the plan
// builder's worker can hand plans over without the game thread. Attenuation: the voice output is pressure referred to
// RefDistance; the component's custom attenuation curve applies RefDistance / r (the 1 / r law; clamped to 1 inside it), the
// reverb send is taken PRE distance attenuation (the diffuse field does not fall with distance).
// Owner: M2-C.

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Containers/MpscQueue.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbShotAudioClock.h"
#include "RbAudio/RbVoiceRenderer.h"

#include <atomic>

#include "RbImpactVoiceComponent.generated.h"

class FAudioDevice;
class USoundSubmixBase;

using FRbShotAudioClockPtr = TSharedPtr<RbAudio::FShotAudioClock, ESPMode::ThreadSafe>;
using FRbVoicePlanPtr = TSharedPtr<const RbAudio::FVoicePlan, ESPMode::ThreadSafe>;

// Per-block render times of several voices (T19): every generator of a profiled table adds (block frame, microseconds) twice: the
// CPU time its thread spent in the callback (Windows: QueryThreadCycleTime; independent of preemption by other processes on a
// busy machine) and the wall time (includes preemption). Create it on the game thread (the constructor calibrates the cycle clock).
struct RAWBREAK_API FRbAudioRenderProfile
{
	FRbAudioRenderProfile();
	void Add(int64 BlockFrame, double CpuMicros, double WallMicros);
	void Reset();
	// Largest summed render time of one device block over all voices (CPU or wall), and the block it happened in.
	double MaxBlockMicros(int64* OutBlockFrame = nullptr, bool bWall = false) const;
	int32 NumSamples() const;

	// CPU time of the calling thread [us] (wall time where the platform has no thread cycle counter).
	static double ThreadCpuMicros();

private:
	mutable FCriticalSection Lock;
	TMap<int64, double> PerBlock;
	TMap<int64, double> PerBlockWall;
	int32 Count = 0;
};
using FRbAudioRenderProfilePtr = TSharedPtr<FRbAudioRenderProfile, ESPMode::ThreadSafe>;

// State shared between the component (game thread / plan worker) and its generator (audio render thread).
struct RAWBREAK_API FRbVoiceShared
{
	TMpscQueue<TUniqueFunction<void(RbAudio::FVoiceRenderer&)>> Commands;
	// The last plan handed over: a generator created later (the mixer re-created the source) adopts it, so a shot keeps sounding.
	FCriticalSection PlanLock;
	FRbVoicePlanPtr LatestPlan;
	FRbShotAudioClockPtr Clock;
	FRbAudioRenderProfilePtr Profile;
	// Timing constants of the anchor (audio.md 5.1).
	double OutputLatencySeconds = 0.031;
	int32 LeadMinBlocks = 1;
	int32 LeadMarginFrames = 64;
	// Stats (written by the generator).
	std::atomic<int32> RenderedImpacts{0};
	std::atomic<int32> LateImpacts{0};
	std::atomic<int64> LastBlockFrame{-1};
	std::atomic<int64> BlocksRendered{0};
	std::atomic<double> MaxRenderMicros{0.0};
	std::atomic<int32> BlockFrames{0};
	std::atomic<double> SampleRate{0.0};
	std::atomic<int32> GeneratorsAlive{0}; // live generators (a new one can start before the old one is destroyed)
	// Output channel of a non-spatialised multi-channel test voice (AU-0: two voices on the left / right channel); -1 = every
	// channel gets the mono signal.
	int32 ChannelMask = -1;
};
using FRbVoiceSharedPtr = TSharedPtr<FRbVoiceShared, ESPMode::ThreadSafe>;

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbImpactVoiceComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	URbImpactVoiceComponent(const FObjectInitializer& ObjectInitializer);

	// Routing / attenuation before Start (game thread). Positional voices attenuate with RefDistance / r out to RangeMeters.
	void ConfigureVoice(bool bPositional, USoundSubmixBase* BaseSubmix, USoundSubmixBase* ReverbSubmix, float ReverbSendLevel, double RefDistanceMeters,
		double RangeMeters);

	// Scales the reverb send by the volume of the voice's own bus (game thread, any time): the reverb submix is shared by all
	// buses, so a volume slider that only turned its bus's dry submix down would leave the sound's reverb return at full level.
	void SetReverbSendGain(float Gain);
	float GetReverbSendLevel() const { return ReverbSendBase * ReverbSendGain; }

	// The table's clock (shared by all voices of one table). Set before Start().
	void SetClock(FRbShotAudioClockPtr InClock) { Shared->Clock = MoveTemp(InClock); }
	void SetProfile(FRbAudioRenderProfilePtr InProfile) { Shared->Profile = MoveTemp(InProfile); }

	// Thread-safe hand-over (any thread): replaces the voice's plan (null = only tails / live content).
	void PushPlan(FRbVoicePlanPtr Plan);
	static void PushPlan(const FRbVoiceSharedPtr& Shared, FRbVoicePlanPtr Plan);
	// Live content (game thread): a pre-rendered one-shot [Pa at RefDistance], the live rolling layer, the live output gain.
	void AddLivePcm(TArray<float>&& Pcm, int32 DelayFrames = 0);
	void SetLiveContinuous(RbAudio::ENoiseKind Kind, double SpeedMps, double GainPerMps);
	void SetLiveOutputGain(double Gain);

	// AU-0 / AU-T08 / AU-T21 test routing (before Start): a non-spatialised stereo voice that writes only to OutputChannel.
	void SetTestChannel(int32 OutputChannel);

	FRbVoiceSharedPtr GetShared() const { return Shared; }
	// True once the audio mixer created the generator (the voice renders).
	bool IsRendering() const { return Shared->GeneratorsAlive.load() > 0; }

	// Starts the voice and keeps it rendering while silent (the synth's sound plays when silent: never virtualised, so its
	// generator and its state survive quiet passages, an unfocused window and the pause; audio.md 7.4).
	void StartVoice();

	// UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	// USynthComponent
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

	FRbVoiceSharedPtr Shared;

	// The configured reverb send (ConfigureVoice) and the bus-volume gain on it (SetReverbSendGain).
	UPROPERTY(Transient)
	TObjectPtr<USoundSubmixBase> ReverbSendSubmix;
	float ReverbSendBase = 0.0f;
	float ReverbSendGain = 1.0f;
};

// Builds the 1 / r attenuation of the voices: RefDistance / r for r >= RefDistance, 1 inside, out to Range (log-spaced keys).
RAWBREAK_API void RbMakeInverseDistanceAttenuation(struct FSoundAttenuationSettings& Out, double RefDistanceMeters, double RangeMeters);
