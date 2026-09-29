#pragma once

// One table voice (Docs/specs/audio.md 5.1, 6.1, 8.2; Docs/ue-architecture.md 18.5): a USynthComponent whose ISoundGenerator
// renders the physics-driven impacts and continuous noise of its emitter (a ball, a rail, a pocket, the table body, the cue)
// SAMPLE-ACCURATELY from the per-table FShotAudioClock: one callback per device block
// (GetDesiredNumSamplesToRenderPerCallback = AudioMixerNumOutputFrames x NumChannels), StartFrame from the device's audio clock
// in OnBeginGenerate, own frame counter, pitch 1, no Doppler, never virtualised or stolen (own concurrency). Voices run
// continuously while their table is in an audio LOD tier (an idle voice writes zeros); they are never started per shot.
// Owner: M2-C (stub by the M2 architect step; the generator writes silence; TODO(M2-C)).

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbShotAudioClock.h"

#include "RbImpactVoiceComponent.generated.h"

using FRbShotAudioClockPtr = TSharedPtr<RbAudio::FShotAudioClock, ESPMode::ThreadSafe>;
using FRbVoicePlanPtr = TSharedPtr<const RbAudio::FVoicePlan, ESPMode::ThreadSafe>;

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbImpactVoiceComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	URbImpactVoiceComponent(const FObjectInitializer& ObjectInitializer);

	// The table's clock (shared by all voices of one table). Set before Start().
	void SetClock(FRbShotAudioClockPtr InClock) { Clock = MoveTemp(InClock); }

	// Replaces the voice's plan (thread-safe hand-over to the generator). Null = silence.
	void PushPlan(FRbVoicePlanPtr Plan);

protected:
	// USynthComponent
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

	FRbShotAudioClockPtr Clock;
	FRbVoicePlanPtr PendingPlan;
	ISoundGeneratorPtr Generator;
};
