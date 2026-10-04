#pragma once

// Sample-exact capture of a submix output together with the DEVICE FRAME of every block (Docs/ue-architecture.md 18.5, audio.md
// 8.8): an ISubmixBufferListener whose OnNewSubmixBuffer receives each rendered block with the mixer's audio clock (the same clock
// the table voices anchor on), so a capture relates recording samples to device frames exactly. Used by the functional audio
// tests (AU-0 L_src, AU-T08, AU-T16, AU-T19, AU-T21, the recorded breaks) and by dev recordings. The capture is post submix
// effects and output volume. Owner: M2-C.

#include "CoreMinimal.h"
#include "AudioDeviceHandle.h"
#include "ISubmixBufferListener.h"

class USoundSubmix;
class UWorld;

class RAWBREAK_API FRbSubmixCapture : public ISubmixBufferListener
{
public:
	// Starts capturing Submix (null: the engine's main output submix) of the world's audio device, at most MaxSeconds. Null without
	// an audio device (-NoSound).
	static TSharedPtr<FRbSubmixCapture, ESPMode::ThreadSafe> Start(UWorld* World, USoundSubmix* Submix, double MaxSeconds, const FString& Name);

	// Stops listening (game thread; idempotent). The data stays readable.
	void Stop();

	// Copy of the capture so far (any thread). FirstFrame = device frame of OutInterleaved[0].
	void GetSamples(TArray<float>& OutInterleaved, int32& OutNumChannels, int32& OutSampleRate, int64& OutFirstFrame) const;
	int64 GetFramesCaptured() const;
	// Discontinuities (a block whose device frame did not follow the previous one: the submix was not rendered for a while).
	int32 GetGaps() const;
	int64 GetFirstFrame() const;

	// ISubmixBufferListener
	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate,
		double AudioClock) override;
	virtual const FString& GetListenerName() const override { return Name; }
	// Keeps the submix rendering while captured (no auto-disable in silence: the room-tone floor is part of the checks).
	virtual bool IsRenderingAudio() const override { return !bStopped; }

private:
	mutable FCriticalSection Lock;
	TArray<float> Samples;
	int32 Channels = 0;
	int32 Rate = 0;
	int64 FirstFrame = INDEX_NONE;
	int64 NextFrame = INDEX_NONE;
	int32 Gaps = 0;
	int64 MaxSamples = 0;
	FString Name;
	TWeakObjectPtr<USoundSubmix> SubmixObject;
	Audio::FDeviceId DeviceId = INDEX_NONE;
	bool bMainSubmix = false;
	TAtomic<bool> bStopped{false};
};
