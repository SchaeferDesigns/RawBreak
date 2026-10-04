#pragma once

// Audio v1 settings and shared types (Docs/specs/audio.md 4.2, 5.1, 6.6, 7.4, 8.2 URbAudioSettings; Docs/ue-architecture.md 18.5).
// Engine-side constants that are not player settings: presentation mode, latencies of the anchor formula, the voices' reference
// distance, directivity floor, tier distances, reverb sends. Defaults live here (C++); a [/Script/RawBreak.RbAudioSettings]
// section in DefaultGame.ini may override them later (architect). The player's volume sliders are FRbAudioVolumes (M2-D).
// Owner: M2-C.

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "RbAudioSettings.generated.h"

// Audio level of detail of one table (audio.md 6.6).
UENUM()
enum class ERbTableAudioTier : uint8
{
	T0,  // full synthesis, 30 voices (the player's table, or any table < 3 m)
	T1,  // 4 quadrant voices (3-10 m): order-1 kernels, no cloth image
	T2,  // 1 voice at the table centre (> 10 m): order-1 kernel, low-passed 6 kHz
	Off,
};

// Dynamic-range mode (audio.md 4.2; UIX 13.8 "Dynamic range"): which sound pressure maps to 0 dBFS.
UENUM()
enum class ERbDynamicRange : uint8
{
	Wide,   // headphones default (0 dBFS = 114 dB SPL, table knee 105 dB, 3:1)
	Normal, // speakers default (106 dB SPL, 97 dB, 4:1)
	Night,  // (100 dB SPL, 95 dB, 10:1)
};

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "RAW BREAK Audio"))
class RAWBREAK_API URbAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const URbAudioSettings* Get() { return GetDefault<URbAudioSettings>(); }

	// Presentation mode of every physical bus (a settings row of M2-D later; Wide until then).
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	ERbDynamicRange DynamicRange = ERbDynamicRange::Wide;

	// The voices output pressure referred to this distance [m]; their attenuation curve applies RefDistance / r (1 / r law,
	// clamped to 1 inside it). 0.25 m keeps the cue ball under the breaker's chin physical.
	UPROPERTY(config, EditAnywhere, Category = "Spatial")
	float RefDistanceMeters = 0.25f;

	// Range of the 1 / r attenuation curve [m] (beyond it: held at its last value; never culled inside a venue).
	UPROPERTY(config, EditAnywhere, Category = "Spatial")
	float AttenuationRangeMeters = 60.0f;

	// Gain that makes a positional mono voice straight ahead reach each ear at its physical level. The AU-0 gain probes measured the
	// engine's path (5.8.3, stereo device, PanningMethod=EqualPower of the audio block): -3.01 dB equal-power pan and -3.01 dB that
	// every source loses on its way into a submix, i.e. -6.02 dB per channel -> x 2 (1.0 disables).
	UPROPERTY(config, EditAnywhere, Category = "Spatial")
	float PanCompensation = 2.0f;

	// The same for non-spatialised multi-channel voices (the stereo room-tone bed): each channel of such a source reaches its submix
	// at -3.01 dB (AU-0 gain probes) -> x sqrt(2) (1.0 disables).
	UPROPERTY(config, EditAnywhere, Category = "Spatial")
	float NonSpatialCompensation = 1.41421356f;

	// Floor of the order-1 directivity weight [dB] (audio.md 3.6 DirectivityFloorDb; scattering by nearby balls and rails).
	UPROPERTY(config, EditAnywhere, Category = "Synthesis")
	float DirectivityFloorDb = -20.0f;

	// Anchor formula (audio.md 5.1): output latency of the device [s]: buffer frames x buffers / fs (512 x 2 / 48 kHz = 21.3 ms) + the
	// WASAPI device period (10 ms, AU-0 log) + the master limiter's lookahead (5 ms, DYN_RB_MasterLimiter).
	UPROPERTY(config, EditAnywhere, Category = "Timing")
	float OutputLatencySeconds = 0.036f;

	// Render + display latency in frames of the current frame rate (added to the user's A/V offset).
	UPROPERTY(config, EditAnywhere, Category = "Timing")
	float VisualLatencyFrames = 2.0f;

	// User A/V offset [s] (Settings, +-0.15 s; M2-D row later).
	UPROPERTY(config, EditAnywhere, Category = "Timing")
	float AvOffsetSeconds = 0.0f;

	// Minimum lead of a shot's anchor [device blocks] (5.1 LeadMin) plus a margin [frames] for the band-limit pre-ringing.
	UPROPERTY(config, EditAnywhere, Category = "Timing")
	int32 LeadMinBlocks = 1;

	UPROPERTY(config, EditAnywhere, Category = "Timing")
	int32 LeadMarginFrames = 64;

	// Tier distances [m] from the listener to the nearest rail (audio.md 6.6), hysteresis 1 m.
	UPROPERTY(config, EditAnywhere, Category = "Tiers")
	float TierT1Meters = 3.0f;

	UPROPERTY(config, EditAnywhere, Category = "Tiers")
	float TierT2Meters = 10.0f;

	// Reverb send of the physical voices (the send carries the RefDistance-referred signal; the IR is normalised for 1 m). AU-0's
	// calibration probe measured the engine's convolution path +1.4 dB hot (mean of both ears) -> 0.85.
	UPROPERTY(config, EditAnywhere, Category = "Reverb")
	float ReverbSendScale = 0.85f;

	// Pause mix (audio.md 7.3 CBM_RB_Pause): World low-pass cutoff [Hz], level [dB], attack / release [s].
	UPROPERTY(config, EditAnywhere, Category = "Mix")
	float PauseLowPassHz = 800.0f;

	UPROPERTY(config, EditAnywhere, Category = "Mix")
	float PauseLevelDb = -12.0f;

	UPROPERTY(config, EditAnywhere, Category = "Mix")
	float PauseAttackSeconds = 0.2f;

	UPROPERTY(config, EditAnywhere, Category = "Mix")
	float PauseReleaseSeconds = 0.3f;

	// Replay mix (7.3 CBM_RB_Replay): ambience level during replays [dB].
	UPROPERTY(config, EditAnywhere, Category = "Mix")
	float ReplayAmbienceDb = -10.0f;

	// Volume slider taper: gain = v^Exponent (2 = square law, 0.5 -> -12 dB).
	UPROPERTY(config, EditAnywhere, Category = "Mix")
	float VolumeTaperExponent = 2.0f;

	// Slider value -> linear gain (0 -> silence).
	static float VolumeToGain(float Slider, float Exponent = 2.0f)
	{
		const float V = FMath::Clamp(Slider, 0.0f, 1.0f);
		return V <= 0.0f ? 0.0f : FMath::Pow(V, Exponent);
	}
};
