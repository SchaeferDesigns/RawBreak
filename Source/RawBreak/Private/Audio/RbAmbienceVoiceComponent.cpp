#include "Audio/RbAmbienceVoiceComponent.h"

#include "Audio/RbImpactVoiceComponent.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Misc/ScopeLock.h"
#include "Engine/World.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundSubmixSend.h"

// Owner: M2-C.

namespace RbAmbiencePrivate
{
	class FRbAmbienceGenerator : public ISoundGenerator
	{
	public:
		FRbAmbienceGenerator(const FSoundGeneratorInitParams& InParams, const RbAudio::FAmbienceLayerDesc& Desc, double InRefScale, double Calibration,
			FRbAmbienceSharedPtr InShared)
			: NumChannels(FMath::Max(1, InParams.NumChannels))
			, BlockFrames(FMath::Max(1, InParams.AudioMixerNumOutputFrames))
			, RefScale(InRefScale)
			, Shared(MoveTemp(InShared))
		{
			Synth.Initialize(Desc, InParams.SampleRate > 0.0f ? InParams.SampleRate : 48000.0, Calibration);
			SynthChannels = Desc.NumChannels();
			Buffer.SetNumZeroed(BlockFrames * SynthChannels);
			Shared->GeneratorsAlive.fetch_add(1);
		}
		virtual ~FRbAmbienceGenerator() override { Shared->GeneratorsAlive.fetch_sub(1); }

		virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return BlockFrames * NumChannels; }
		virtual int32 GetNumChannels() const override { return NumChannels; }
		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / NumChannels;
			if (Buffer.Num() < Frames * SynthChannels)
			{
				Buffer.SetNumZeroed(Frames * SynthChannels);
			}
			Synth.Render(Buffer.GetData(), Frames);
			const float Gain = static_cast<float>(Shared->OutputGain.load() * RefScale);
			for (int32 I = 0; I < Frames; ++I)
			{
				for (int32 C = 0; C < NumChannels; ++C)
				{
					OutAudio[I * NumChannels + C] = Gain * Buffer[I * SynthChannels + FMath::Min(C, SynthChannels - 1)];
				}
			}
			Shared->bCompressorOn.store(Synth.IsCompressorOn());
			Shared->FramesRendered.fetch_add(Frames);
			return NumSamples;
		}

	private:
		int32 NumChannels = 1;
		int32 BlockFrames = 512;
		int32 SynthChannels = 1;
		double RefScale = 1.0;
		FRbAmbienceSharedPtr Shared;
		RbAudio::FAmbienceSynth Synth;
		TArray<float> Buffer;
	};
}

URbAmbienceVoiceComponent::URbAmbienceVoiceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1;
	bAutoActivate = false;
	bIsUISound = true; // plays on in the paused world (the pause mix filters it)
	bAlwaysPlay = true;
	bStopWhenOwnerDestroyed = true;
	bOverrideAttenuation = true;
	Shared = MakeShared<FRbAmbienceShared, ESPMode::ThreadSafe>();
}

void URbAmbienceVoiceComponent::ConfigureLayer(const RbAudio::FAmbienceLayerDesc& InDesc, USoundSubmixBase* BaseSubmix, USoundSubmixBase* ReverbSubmix,
	float ReverbSendLevel, double RefDistanceMeters, double RangeMeters)
{
	Desc = InDesc;
	RefDistance = RefDistanceMeters;
	NumChannels = Desc.NumChannels();
	CalibrationRate = 48000.0;
	if (const UWorld* World = GetWorld())
	{
		if (const FAudioDevice* Device = World->GetAudioDeviceRaw())
		{
			CalibrationRate = Device->GetSampleRate();
		}
	}
	Calibration = CalibrationFor(Desc, CalibrationRate);
	const bool bPositional = NumChannels == 1;
	bAllowSpatialization = bPositional;
	RbMakeInverseDistanceAttenuation(AttenuationOverrides, RefDistanceMeters, RangeMeters);
	if (!bPositional)
	{
		AttenuationOverrides.bAttenuate = false;
		AttenuationOverrides.bSpatialize = false;
	}
	SoundSubmix = BaseSubmix;
	bEnableBaseSubmix = true;
	SoundSubmixSends.Reset();
	ReverbSendSubmix = nullptr;
	ReverbSendBase = 0.0f;
	if (ReverbSubmix && ReverbSendLevel > 0.0f)
	{
		ReverbSendSubmix = ReverbSubmix;
		ReverbSendBase = ReverbSendLevel;
		FSoundSubmixSendInfo Send;
		Send.SoundSubmix = ReverbSubmix;
		Send.SendLevelControlMethod = ESendLevelControlMethod::Manual;
		Send.SendLevel = ReverbSendLevel * ReverbSendGain;
		Send.SendStage = ESubmixSendStage::PreDistanceAttenuation;
		SoundSubmixSends.Add(Send);
		bEnableSubmixSends = true;
	}
}

void URbAmbienceVoiceComponent::SetReverbSendGain(float Gain)
{
	Gain = FMath::Max(Gain, 0.0f);
	if (Gain == ReverbSendGain)
	{
		return;
	}
	ReverbSendGain = Gain;
	if (!ReverbSendSubmix)
	{
		return;
	}
	for (FSoundSubmixSendInfo& Send : SoundSubmixSends)
	{
		if (Send.SoundSubmix == ReverbSendSubmix)
		{
			Send.SendLevel = ReverbSendBase * ReverbSendGain;
		}
	}
	SetSubmixSend(ReverbSendSubmix, ReverbSendBase * ReverbSendGain);
}

double URbAmbienceVoiceComponent::CalibrationFor(const RbAudio::FAmbienceLayerDesc& InDesc, double SampleRate)
{
	// One calibration pass per layer / seed / level / rate and session, on the game thread (the audio thread never calibrates).
	static FCriticalSection Lock;
	static TMap<FString, double> Cache;
	const FString Key = FString::Printf(TEXT("%d_%llu_%.3f_%.1f_%.1f"), static_cast<int32>(InDesc.Layer), InDesc.Seed, InDesc.LevelDbA, InDesc.MainsHz,
		SampleRate);
	{
		FScopeLock Guard(&Lock);
		if (const double* Found = Cache.Find(Key))
		{
			return *Found;
		}
	}
	RbAudio::FAmbienceSynth Synth;
	Synth.Initialize(InDesc, SampleRate);
	FScopeLock Guard(&Lock);
	Cache.Add(Key, Synth.GetCalibration());
	return Synth.GetCalibration();
}

void URbAmbienceVoiceComponent::StartVoice()
{
	Start();
	if (UAudioComponent* Audio = GetAudioComponent())
	{
		if (USoundBase* Sound = Audio->Sound)
		{
			Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
		}
	}
}

void URbAmbienceVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}

ISoundGeneratorPtr URbAmbienceVoiceComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	// Positional layers: the synth is calibrated at 1 m; the voice outputs it referred to RefDistance (x 1 m / RefDistance).
	const double RefScale = NumChannels == 1 ? 1.0 / FMath::Max(0.01, RefDistance) : 1.0;
	const double Rate = InParams.SampleRate > 0.0f ? InParams.SampleRate : 48000.0;
	return MakeShared<RbAmbiencePrivate::FRbAmbienceGenerator, ESPMode::ThreadSafe>(InParams, Desc, RefScale,
		FMath::IsNearlyEqual(Rate, CalibrationRate) ? Calibration : 0.0, Shared);
}
