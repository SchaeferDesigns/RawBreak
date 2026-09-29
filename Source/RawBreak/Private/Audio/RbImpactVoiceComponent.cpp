#include "Audio/RbImpactVoiceComponent.h"

#include "RbAudio/RbImpactSynth.h"

// Owner: M2-C. Stub of the M2 architect step: the generator renders silence and ignores its plan.

namespace RbAudioPrivate
{
	// The voice's generator on the audio render thread (no UObjects).
	class FRbImpactVoiceGenerator : public ISoundGenerator
	{
	public:
		FRbImpactVoiceGenerator(const FSoundGeneratorInitParams& InParams, FRbShotAudioClockPtr InClock)
			: NumChannels(FMath::Max(1, InParams.NumChannels))
			, NumFramesPerBlock(FMath::Max(1, InParams.AudioMixerNumOutputFrames))
			, SampleRate(InParams.SampleRate)
			, Clock(MoveTemp(InClock))
		{
			Renderer.Initialize(SampleRate);
		}

		void SetPlan(FRbVoicePlanPtr InPlan)
		{
			SynthCommand([this, InPlan]() { Plan = InPlan; });
		}

		// ISoundGenerator
		virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return NumFramesPerBlock * NumChannels; }
		virtual int32 GetNumChannels() const override { return NumChannels; }
		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			// TODO(M2-C): StartFrame from FAudioDevice::GetAudioClock (OnBeginGenerate), Clock->TryAnchor, render the due impacts
			// (overlap-add, sub-sample) and the continuous noise of the plan (audio.md 3.6, 5.1).
			FMemory::Memzero(OutAudio, sizeof(float) * NumSamples);
			return NumSamples;
		}

	private:
		int32 NumChannels = 1;
		int32 NumFramesPerBlock = 1024;
		double SampleRate = 48000.0;
		FRbShotAudioClockPtr Clock;
		FRbVoicePlanPtr Plan;
		RbAudio::FImpactRenderer Renderer;
	};
}

URbImpactVoiceComponent::URbImpactVoiceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1; // point source; the spatialiser pans it
	bAutoActivate = false;
}

void URbImpactVoiceComponent::PushPlan(FRbVoicePlanPtr Plan)
{
	PendingPlan = Plan;
	if (Generator.IsValid())
	{
		StaticCastSharedPtr<RbAudioPrivate::FRbImpactVoiceGenerator>(Generator)->SetPlan(MoveTemp(Plan));
	}
}

ISoundGeneratorPtr URbImpactVoiceComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	TSharedPtr<RbAudioPrivate::FRbImpactVoiceGenerator, ESPMode::ThreadSafe> NewGenerator =
		MakeShared<RbAudioPrivate::FRbImpactVoiceGenerator, ESPMode::ThreadSafe>(InParams, Clock);
	if (PendingPlan.IsValid())
	{
		NewGenerator->SetPlan(PendingPlan);
	}
	Generator = NewGenerator;
	return Generator;
}
