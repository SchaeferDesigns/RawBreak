#include "Audio/RbAudioCapture.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/World.h"
#include "Sound/SoundSubmix.h"

#include <cmath>

// Owner: M2-C.

TSharedPtr<FRbSubmixCapture, ESPMode::ThreadSafe> FRbSubmixCapture::Start(UWorld* World, USoundSubmix* Submix, double MaxSeconds, const FString& Name)
{
	FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
	if (!Device)
	{
		return nullptr;
	}
	TSharedRef<FRbSubmixCapture, ESPMode::ThreadSafe> Capture = MakeShared<FRbSubmixCapture, ESPMode::ThreadSafe>();
	Capture->Name = Name;
	Capture->DeviceId = Device->DeviceID;
	Capture->bMainSubmix = Submix == nullptr;
	USoundSubmix& Target = Submix ? *Submix : Device->GetMainSubmixObject();
	Capture->SubmixObject = &Target;
	Capture->MaxSamples = static_cast<int64>(FMath::Max(0.1, MaxSeconds) * Device->GetSampleRate() * 8.0);
	Device->RegisterSubmixBufferListener(Capture, Target);
	return Capture;
}

void FRbSubmixCapture::Stop()
{
	if (bStopped)
	{
		return;
	}
	bStopped = true;
	FAudioDeviceManager* Manager = FAudioDeviceManager::Get();
	FAudioDevice* Device = Manager ? Manager->GetAudioDeviceRaw(DeviceId) : nullptr;
	USoundSubmix* Target = SubmixObject.Get();
	if (Device && Target)
	{
		Device->UnregisterSubmixBufferListener(AsShared(), *Target);
	}
}

void FRbSubmixCapture::OnNewSubmixBuffer(const USoundSubmix* /*OwningSubmix*/, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate,
	double AudioClock)
{
	if (bStopped || NumChannels <= 0)
	{
		return;
	}
	const int32 Frames = NumSamples / NumChannels;
	const int64 Frame = static_cast<int64>(std::llround(AudioClock * SampleRate));
	FScopeLock Guard(&Lock);
	if (FirstFrame == INDEX_NONE)
	{
		FirstFrame = Frame;
		NextFrame = Frame;
		Channels = NumChannels;
		Rate = SampleRate;
	}
	if (NumChannels != Channels || Samples.Num() + NumSamples > MaxSamples)
	{
		return;
	}
	if (Frame != NextFrame)
	{
		// A discontinuity: pad the missing frames with zeros so indices stay device-frame exact (a gap backwards is ignored).
		++Gaps;
		if (Frame > NextFrame)
		{
			const int64 Missing = FMath::Min<int64>(Frame - NextFrame, (MaxSamples - Samples.Num()) / FMath::Max(1, Channels));
			Samples.AddZeroed(static_cast<int32>(Missing * Channels));
			NextFrame += Missing;
		}
	}
	Samples.Append(AudioData, NumSamples);
	NextFrame = Frame + Frames;
}

void FRbSubmixCapture::GetSamples(TArray<float>& OutInterleaved, int32& OutNumChannels, int32& OutSampleRate, int64& OutFirstFrame) const
{
	FScopeLock Guard(&Lock);
	OutInterleaved = Samples;
	OutNumChannels = Channels;
	OutSampleRate = Rate;
	OutFirstFrame = FirstFrame;
}

int64 FRbSubmixCapture::GetFramesCaptured() const
{
	FScopeLock Guard(&Lock);
	return Channels > 0 ? Samples.Num() / Channels : 0;
}

int32 FRbSubmixCapture::GetGaps() const
{
	FScopeLock Guard(&Lock);
	return Gaps;
}

int64 FRbSubmixCapture::GetFirstFrame() const
{
	FScopeLock Guard(&Lock);
	return FirstFrame;
}
