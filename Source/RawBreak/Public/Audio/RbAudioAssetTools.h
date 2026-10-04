#pragma once

// Helpers of the headless audio asset generator Tools/unreal/editor/rb_make_audio.py (Docs/ue-architecture.md 18.5) for what the
// editor Python API cannot reach: the sample data of a UAudioImpulseResponse (Synthesis plugin; set through reflection, so the
// RawBreak module needs no plugin dependency), consistent parent / child links of submixes, and WAV reading of the synthesised
// impulse responses (Tools/audio/ir_synth.py). Owner: M2-C.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "RbAudioAssetTools.generated.h"

class USoundSubmix;

UCLASS()
class RAWBREAK_API URbAudioAssetTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Fills an AudioImpulseResponse asset: interleaved samples, channel count, sample rate, normalisation volume [dB] (0 = the
	// samples as they are; the engine default is -24 dB). False if the object is not an impulse response.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Audio")
	static bool SetImpulseResponseData(UObject* ImpulseResponse, const TArray<float>& InterleavedSamples, int32 NumChannels, int32 SampleRate,
		float NormalizationVolumeDb);

	// Reads a WAV file (16 / 24 / 32-bit PCM or 32-bit float) as interleaved floats.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Audio")
	static bool ReadWavFile(const FString& Path, TArray<float>& OutInterleaved, int32& OutNumChannels, int32& OutSampleRate);

	// Makes Parent the parent of Child (removes Child from its previous parent's children, adds it to Parent's). Null: a root.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Audio")
	static void LinkSubmix(USoundSubmix* Child, USoundSubmix* Parent);

	// Number of samples and channels stored in an impulse response (checks of the generator).
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Audio")
	static int32 GetImpulseResponseNumSamples(UObject* ImpulseResponse, int32& OutNumChannels, int32& OutSampleRate);
};
