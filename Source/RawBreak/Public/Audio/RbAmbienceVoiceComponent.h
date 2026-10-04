#pragma once

// One synthesised room-tone layer of a venue (Docs/specs/audio.md 2.7, 12.1; venue-dive-bar 10): an HVAC bed (stereo, not
// spatialised), a ceiling diffuser, a cooler compressor or a neon transformer (mono, positional, 1 / r attenuation like the table
// voices). The generator runs RbAudio::FAmbienceSynth (calibrated to its A-weighted level at 1 m) and multiplies by the output gain
// (1 / P_fs of the dynamic-range mode x the ambience bus offset; the Ambience volume slider is the submix's). Owner: M2-C.

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"

#include "RbAudio/RbNoiseSynth.h"

#include <atomic>

#include "RbAmbienceVoiceComponent.generated.h"

class USoundSubmixBase;

struct FRbAmbienceShared
{
	std::atomic<double> OutputGain{1.0};
	std::atomic<bool> bCompressorOn{false};
	std::atomic<int32> GeneratorsAlive{0}; // live generators (a new one can start before the old one is destroyed)
	std::atomic<int64> FramesRendered{0};
};
using FRbAmbienceSharedPtr = TSharedPtr<FRbAmbienceShared, ESPMode::ThreadSafe>;

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbAmbienceVoiceComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	URbAmbienceVoiceComponent(const FObjectInitializer& ObjectInitializer);

	// Before Start (game thread). Pressure output referred to RefDistanceMeters for positional layers.
	void ConfigureLayer(const RbAudio::FAmbienceLayerDesc& InDesc, USoundSubmixBase* BaseSubmix, USoundSubmixBase* ReverbSubmix, float ReverbSendLevel,
		double RefDistanceMeters, double RangeMeters);
	// Pa -> digital (1 / P_fs x bus offset); any thread.
	void SetOutputGain(double Gain) { Shared->OutputGain.store(Gain); }
	// Scales the reverb send by the Ambience bus volume incl. the replay dip (game thread; see URbImpactVoiceComponent).
	void SetReverbSendGain(float Gain);
	float GetReverbSendLevel() const { return ReverbSendBase * ReverbSendGain; }

	const RbAudio::FAmbienceLayerDesc& GetLayerDesc() const { return Desc; }
	// Level calibration of a layer (cached per layer / seed / level / rate; game thread).
	static double CalibrationFor(const RbAudio::FAmbienceLayerDesc& InDesc, double SampleRate);
	bool IsRendering() const { return Shared->GeneratorsAlive.load() > 0; }
	FRbAmbienceSharedPtr GetShared() const { return Shared; }

	// Starts the voice and keeps it rendering while silent (the synth's sound plays when silent: never virtualised, so its
	// generator and its state survive quiet passages, an unfocused window and the pause; audio.md 7.4).
	void StartVoice();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

	RbAudio::FAmbienceLayerDesc Desc;
	double RefDistance = 0.25;
	double Calibration = 0.0;       // computed on the game thread at ConfigureLayer (the audio thread never calibrates)
	double CalibrationRate = 48000.0;
	FRbAmbienceSharedPtr Shared;

	UPROPERTY(Transient)
	TObjectPtr<USoundSubmixBase> ReverbSendSubmix;
	float ReverbSendBase = 0.0f;
	float ReverbSendGain = 1.0f;
};
