#include "Audio/RbImpactVoiceComponent.h"

#include "Audio/RbAudioLog.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundSubmixSend.h"

#include <cmath>

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <intrin.h>
#include <realtimeapiset.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// Owner: M2-C.

namespace RbAudioProfilePrivate
{
	// TSC ticks per microsecond: QueryThreadCycleTime counts the thread's CPU time in TSC ticks (invariant TSC). Calibrated once
	// against the performance counter over 20 ms (the TSC keeps counting while the calibrating thread sleeps).
	double TscPerMicro()
	{
#if PLATFORM_WINDOWS
		static const double Value = []()
		{
			LARGE_INTEGER Freq, Q0, Q1;
			QueryPerformanceFrequency(&Freq);
			QueryPerformanceCounter(&Q0);
			const uint64 C0 = __rdtsc();
			FPlatformProcess::Sleep(0.02f);
			const uint64 C1 = __rdtsc();
			QueryPerformanceCounter(&Q1);
			const double Micros = static_cast<double>(Q1.QuadPart - Q0.QuadPart) * 1e6 / static_cast<double>(Freq.QuadPart);
			return Micros > 0.0 && C1 > C0 ? static_cast<double>(C1 - C0) / Micros : 3000.0;
		}();
		return Value;
#else
		return 1.0;
#endif
	}
}

FRbAudioRenderProfile::FRbAudioRenderProfile()
{
	RbAudioProfilePrivate::TscPerMicro(); // calibrate here (game thread), never in the audio callback
}

double FRbAudioRenderProfile::ThreadCpuMicros()
{
#if PLATFORM_WINDOWS
	ULONG64 Cycles = 0;
	if (QueryThreadCycleTime(GetCurrentThread(), &Cycles))
	{
		return static_cast<double>(Cycles) / RbAudioProfilePrivate::TscPerMicro();
	}
#endif
	return FPlatformTime::Seconds() * 1e6;
}

void FRbAudioRenderProfile::Add(int64 BlockFrame, double CpuMicros, double WallMicros)
{
	FScopeLock Guard(&Lock);
	PerBlock.FindOrAdd(BlockFrame) += CpuMicros;
	PerBlockWall.FindOrAdd(BlockFrame) += WallMicros;
	++Count;
}

void FRbAudioRenderProfile::Reset()
{
	FScopeLock Guard(&Lock);
	PerBlock.Reset();
	PerBlockWall.Reset();
	Count = 0;
}

double FRbAudioRenderProfile::MaxBlockMicros(int64* OutBlockFrame, bool bWall) const
{
	FScopeLock Guard(&Lock);
	double Max = 0.0;
	int64 At = -1;
	for (const TPair<int64, double>& P : bWall ? PerBlockWall : PerBlock)
	{
		if (P.Value > Max)
		{
			Max = P.Value;
			At = P.Key;
		}
	}
	if (OutBlockFrame)
	{
		*OutBlockFrame = At;
	}
	return Max;
}

int32 FRbAudioRenderProfile::NumSamples() const
{
	FScopeLock Guard(&Lock);
	return Count;
}

namespace RbAudioVoicePrivate
{
	// The voice's generator on the audio render thread (no UObjects).
	class FRbImpactVoiceGenerator : public ISoundGenerator
	{
	public:
		FRbImpactVoiceGenerator(const FSoundGeneratorInitParams& InParams, FRbVoiceSharedPtr InShared, FAudioDevice* InDevice)
			: NumChannels(FMath::Max(1, InParams.NumChannels))
			, BlockFrames(FMath::Max(1, InParams.AudioMixerNumOutputFrames))
			, SampleRate(InParams.SampleRate > 0.0f ? InParams.SampleRate : 48000.0)
			, Shared(MoveTemp(InShared))
			, Device(InDevice)
		{
			Renderer.Initialize(SampleRate, BlockFrames);
			{
				FScopeLock Guard(&Shared->PlanLock);
				if (Shared->LatestPlan.IsValid())
				{
					Renderer.SetPlan(Shared->LatestPlan); // impacts already past are skipped when the plan starts
				}
			}
			Mono.SetNumZeroed(BlockFrames);
			Shared->BlockFrames.store(BlockFrames);
			Shared->SampleRate.store(SampleRate);
			Shared->GeneratorsAlive.fetch_add(1);
		}

		virtual ~FRbImpactVoiceGenerator() override
		{
			Shared->GeneratorsAlive.fetch_sub(1);
		}

		// ISoundGenerator
		virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return BlockFrames * NumChannels; }
		virtual int32 GetNumChannels() const override { return NumChannels; }
		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const uint64 Start = FPlatformTime::Cycles64();
			FRbAudioRenderProfile* Profile = Shared->Profile.Get();
			const double CpuStart = Profile ? FRbAudioRenderProfile::ThreadCpuMicros() : 0.0;
			while (TOptional<TUniqueFunction<void(RbAudio::FVoiceRenderer&)>> Command = Shared->Commands.Dequeue())
			{
				(*Command)(Renderer);
			}
			// Per-frame live parameters (atomics, no queue: see FRbVoiceShared).
			const uint32 Serial = Shared->LiveSerial.load(std::memory_order_acquire);
			if (Serial != AppliedLiveSerial)
			{
				AppliedLiveSerial = Serial;
				Renderer.SetLiveContinuous(static_cast<RbAudio::ENoiseKind>(Shared->LiveKind.load(std::memory_order_relaxed)),
					Shared->LiveSpeedMps.load(std::memory_order_relaxed), Shared->LiveGainPerMps.load(std::memory_order_relaxed));
			}
			const double LiveGain = Shared->LiveOutputGain.load(std::memory_order_relaxed);
			if (LiveGain >= 0.0 && LiveGain != AppliedLiveGain)
			{
				AppliedLiveGain = LiveGain;
				Renderer.SetLiveOutputGain(LiveGain);
			}
			const int32 Frames = NumSamples / NumChannels;
			if (Mono.Num() < Frames)
			{
				Mono.SetNumZeroed(Frames);
			}
			// The device frame of this block: FAudioDevice::GetAudioClock() is the start of the block being rendered (the mixer
			// advances it only after all sources rendered; audio.md 5.1, verified in AU-0).
			const int64 BlockFrame = Device ? static_cast<int64>(std::llround(Device->GetAudioClock() * SampleRate)) : OwnFrame;
			OwnFrame = BlockFrame + Frames;
			RbAudio::FShotAudioClock* Clock = Shared->Clock.Get();
			if (Clock)
			{
				const int64 LeadMin = static_cast<int64>(Shared->LeadMinBlocks) * BlockFrames + Shared->LeadMarginFrames;
				Clock->TryAnchor(BlockFrame, FPlatformTime::Seconds(), Shared->OutputLatencySeconds, LeadMin, SampleRate);
			}
			Renderer.RenderBlock(Clock, BlockFrame, TArrayView<float>(Mono.GetData(), Frames));
			if (NumChannels == 1)
			{
				FMemory::Memcpy(OutAudio, Mono.GetData(), sizeof(float) * Frames);
			}
			else if (Shared->ChannelMask >= 0)
			{
				const int32 Only = FMath::Min(Shared->ChannelMask, NumChannels - 1);
				for (int32 I = 0; I < Frames; ++I)
				{
					for (int32 C = 0; C < NumChannels; ++C)
					{
						OutAudio[I * NumChannels + C] = C == Only ? Mono[I] : 0.0f;
					}
				}
			}
			else
			{
				for (int32 I = 0; I < Frames; ++I)
				{
					for (int32 C = 0; C < NumChannels; ++C)
					{
						OutAudio[I * NumChannels + C] = Mono[I];
					}
				}
			}
			const RbAudio::FVoiceStats& Stats = Renderer.GetStats();
			Shared->RenderedImpacts.store(Stats.RenderedImpacts);
			Shared->LateImpacts.store(Stats.LateImpacts);
			Shared->LastBlockFrame.store(BlockFrame);
			Shared->BlocksRendered.fetch_add(1);
			const double Micros = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start) * 1000.0;
			if (Micros > Shared->MaxRenderMicros.load())
			{
				Shared->MaxRenderMicros.store(Micros);
			}
			if (Profile)
			{
				Profile->Add(BlockFrame, FRbAudioRenderProfile::ThreadCpuMicros() - CpuStart, Micros);
			}
			return NumSamples;
		}

	private:
		int32 NumChannels = 1;
		int32 BlockFrames = 512;
		double SampleRate = 48000.0;
		FRbVoiceSharedPtr Shared;
		FAudioDevice* Device = nullptr;
		int64 OwnFrame = 0;
		uint32 AppliedLiveSerial = 0;
		double AppliedLiveGain = -1.0;
		RbAudio::FVoiceRenderer Renderer;
		TArray<float> Mono;
	};
}

void RbMakeInverseDistanceAttenuation(FSoundAttenuationSettings& Out, double RefDistanceMeters, double RangeMeters)
{
	Out.bAttenuate = true;
	Out.bSpatialize = true;
	Out.bAttenuateWithLPF = false;
	Out.bEnableListenerFocus = false;
	Out.bEnableOcclusion = false;
	Out.bEnableReverbSend = false;
	Out.bEnablePriorityAttenuation = false;
	Out.bEnableSubmixSends = false;
	Out.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
	Out.AttenuationShape = EAttenuationShape::Sphere;
	Out.AttenuationShapeExtents = FVector::ZeroVector;
	const double RefCm = FMath::Max(1.0, 100.0 * RefDistanceMeters);
	const double RangeCm = FMath::Max(RefCm * 2.0, 100.0 * RangeMeters);
	Out.FalloffDistance = static_cast<float>(RangeCm);
	FRichCurve& Curve = Out.CustomAttenuationCurve.EditorCurveData;
	Curve.Reset();
	// gain(x) = min(1, RefCm / (x RangeCm)) with x = d / Range; linear keys every 5 % in distance (error < 0.003 dB).
	const double X1 = RefCm / RangeCm;
	auto AddKey = [&Curve](double X, double Y)
	{
		const FKeyHandle Handle = Curve.AddKey(static_cast<float>(X), static_cast<float>(Y));
		Curve.SetKeyInterpMode(Handle, RCIM_Linear);
	};
	AddKey(0.0, 1.0);
	for (double X = X1; X < 1.0; X *= 1.05)
	{
		AddKey(X, FMath::Min(1.0, X1 / X));
	}
	AddKey(1.0, X1);
}

URbImpactVoiceComponent::URbImpactVoiceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1; // point source; the spatialiser pans it
	bAutoActivate = false;
	bIsUISound = true;        // keeps rendering in the paused world: the pause mix filters it, the held clock silences the table
	bAlwaysPlay = true;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	Shared = MakeShared<FRbVoiceShared, ESPMode::ThreadSafe>();
	RbMakeInverseDistanceAttenuation(AttenuationOverrides, 0.25, 60.0);
}

void URbImpactVoiceComponent::ConfigureVoice(bool bPositional, USoundSubmixBase* BaseSubmix, USoundSubmixBase* ReverbSubmix, float ReverbSendLevel,
	double RefDistanceMeters, double RangeMeters)
{
	bAllowSpatialization = bPositional;
	bOverrideAttenuation = true;
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

void URbImpactVoiceComponent::SetReverbSendGain(float Gain)
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
	// The component's send list is what the next Start() hands to the synth's sound; a playing voice takes the level through
	// the active sound's send override (same submix: only the level changes, the pre-attenuation stage stays).
	for (FSoundSubmixSendInfo& Send : SoundSubmixSends)
	{
		if (Send.SoundSubmix == ReverbSendSubmix)
		{
			Send.SendLevel = ReverbSendBase * ReverbSendGain;
		}
	}
	SetSubmixSend(ReverbSendSubmix, ReverbSendBase * ReverbSendGain);
}

void URbImpactVoiceComponent::SetTestChannel(int32 OutputChannel)
{
	Shared->ChannelMask = OutputChannel;
	NumChannels = 2;
	bAllowSpatialization = false;
	AttenuationOverrides.bAttenuate = false;
	AttenuationOverrides.bSpatialize = false;
}

void URbImpactVoiceComponent::PushPlan(const FRbVoiceSharedPtr& InShared, FRbVoicePlanPtr Plan)
{
	if (InShared.IsValid())
	{
		{
			FScopeLock Guard(&InShared->PlanLock);
			InShared->LatestPlan = Plan;
		}
		InShared->Commands.Enqueue([Plan](RbAudio::FVoiceRenderer& R) { R.SetPlan(Plan); });
	}
}

void URbImpactVoiceComponent::PushPlan(FRbVoicePlanPtr Plan)
{
	PushPlan(Shared, MoveTemp(Plan));
}

void URbImpactVoiceComponent::AddLivePcm(TArray<float>&& Pcm, int32 DelayFrames)
{
	// The closure owns the buffer; the renderer takes it over (no copy on the render thread).
	Shared->Commands.Enqueue([Samples = MoveTemp(Pcm), DelayFrames](RbAudio::FVoiceRenderer& R) mutable { R.AddLivePcm(MoveTemp(Samples), DelayFrames); });
}

void URbImpactVoiceComponent::SetLiveContinuous(RbAudio::ENoiseKind Kind, double SpeedMps, double GainPerMps)
{
	// Called every frame per rolling loose ball: atomics, no queue command (review M2-C: no per-frame allocation).
	const bool bSame = Shared->LiveKind.load(std::memory_order_relaxed) == static_cast<uint8>(Kind)
		&& Shared->LiveSpeedMps.load(std::memory_order_relaxed) == SpeedMps && Shared->LiveGainPerMps.load(std::memory_order_relaxed) == GainPerMps;
	if (bSame && Shared->LiveSerial.load(std::memory_order_relaxed) != 0)
	{
		return;
	}
	Shared->LiveKind.store(static_cast<uint8>(Kind), std::memory_order_relaxed);
	Shared->LiveSpeedMps.store(SpeedMps, std::memory_order_relaxed);
	Shared->LiveGainPerMps.store(GainPerMps, std::memory_order_relaxed);
	Shared->LiveSerial.fetch_add(1, std::memory_order_release);
}

void URbImpactVoiceComponent::SetLiveOutputGain(double Gain)
{
	Shared->LiveOutputGain.store(FMath::Max(0.0, Gain), std::memory_order_relaxed);
}

void URbImpactVoiceComponent::StartVoice()
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

void URbImpactVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}

ISoundGeneratorPtr URbImpactVoiceComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	FAudioDevice* Device = nullptr;
	if (FAudioDeviceManager* Manager = FAudioDeviceManager::Get())
	{
		Device = Manager->GetAudioDeviceRaw(InParams.AudioDeviceID);
	}
	UE_LOG(LogRbAudio, Log, TEXT("%s: generator %d ch, %d frames per callback, %.0f Hz, device %s"), *GetName(), InParams.NumChannels,
		InParams.AudioMixerNumOutputFrames, InParams.SampleRate, Device ? TEXT("found") : TEXT("MISSING"));
	return MakeShared<RbAudioVoicePrivate::FRbImpactVoiceGenerator, ESPMode::ThreadSafe>(InParams, Shared, Device);
}
