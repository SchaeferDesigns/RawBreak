#pragma once

// Camera model parameters (ue5-realism-plan 4.1-4.9, parameter table 4.9). One struct per preset; the defaults
// are compiled in (RbCameraModel::Defaults) so M1 needs no data asset, and URbCameraModel lets look-dev override
// them later as a data asset. Owner: UE-5b.
//
// Presets (plan 4.1):
//   Eyes (default)  human vision: V = 50 deg authored (79.3 deg horizontal at 16:9), pupil 4 mm DoF, slow adaptation
//                   (1.5 / 0.7 EV/s), very light grain, head translation x0.3 with the rotation stabilised on the gaze target
//   Headcam         head-mounted action camera: base H0 = 90 deg at 16:9 (V = 58.7 deg) + barrel distortion k1 0.12 / k2 0.02
//                   (post-process after the upscaler = post-M1; until then the base projection without overscan), 1.1 mm
//                   aperture (nearly everything sharp), fast AE (3.0 / 2.0), exposure-coupled noise, CA, 180 deg shutter,
//                   full head motion + mount jitter
//   Broadcast       replay cameras (UE-7): long lens, tripod-smooth, clean sensor; not part of table 4.9 (values ESTIMATE)
// Every effect has an off switch (URbGameUserSettings comfort options, applied by URbCameraRigComponent).

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "Core/RbTypes.h"

#include "RbCameraModel.generated.h"

// Procedural head motion (plan 4.8): physiology, the same for every preset (the preset scales the translation and decides
// whether the rotation is gaze-stabilised). Peak-to-peak / amplitude values of the head, before HeadTranslationScale.
USTRUCT(BlueprintType)
struct FRbHeadMotionParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walking") double BobPeakToPeakCm = 4.5;       // vertical at 1.4 m/s (3 cm at 0.8 m/s)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walking") double BobPeakToPeakSlowCm = 3.0;   // vertical at 0.8 m/s
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walking") double SwayPeakToPeakCm = 2.5;      // lateral at 1.4 m/s (stride = half the step rate)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walking") double StepRateAtRestHz = 1.4;      // step rate f = f0 + k v
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walking") double StepRatePerMps = 0.45;       // (2.03 Hz at 1.4 m/s)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing") double BreathRateHz = 0.25;       // 15 breaths / min
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing") double BreathStandingMm = 2.5;    // head amplitude standing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breathing") double BreathDownMm = 1.5;        // down on the shot
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway") double PosturalSwayStandingMm = 4.5;   // 0.1-0.5 Hz band
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway") double PosturalSwayDownMm = 1.0;       // bridge hand = third support
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settle") double SettleReduction = 0.7;        // breathing + sway -70 % ...
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settle") double SettleSeconds = 1.5;          // ... over 1-2 s (HF-06)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Headcam") double HeadPitchPerCmBob = 0.12;    // [deg / cm] head nod with the bob (no VOR)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Headcam") double HeadRollPerCmSway = 0.25;    // [deg / cm] roll with the stride sway
};

USTRUCT(BlueprintType)
struct FRbCameraPresetParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double VerticalFovDeg = 50.0;      // authored vertical FOV
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double DistortionK1 = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double DistortionK2 = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double NearClipCm = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double ApertureDiameterMm = 4.0;      // pupil A (DoF)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double FocusEaseSeconds = 0.2;       // accommodation latency
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double ShutterAngleDeg = 108.0;      // motion blur amount = angle / 360
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double MotionBlurMaxPercent = 5.0;   // of the screen width (plan 4.6)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double ChromaticAberration = 0.0;    // scene fringe (Headcam 0.3-0.6)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double BloomIntensity = 0.25;        // lamp glare / veiling glare
	// Plan 4.4: the Eyes see the lamp glare through a convolution bloom (FFT, the engine's default kernel) at low intensity; the
	// Headcam / Broadcast lenses use the standard (sum of gaussians) bloom.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") bool bConvolutionBloom = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double MinEv100 = 2.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double MaxEv100 = 11.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double AdaptSpeedUp = 1.5;         // [EV/s] dark -> bright
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double AdaptSpeedDown = 0.7;       // [EV/s] bright -> dark (slow)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double ExposureCompensation = 0.25;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double MeteringSigma = 0.35;       // centre-weighted mask: gaussian sigma (image half-size = 1)
	// Histogram window the adaptation averages: the eye adapts to what it fixates - the lit table - and lets the dim room go dark,
	// so the Eyes average the brighter half (a camera averages more of the frame).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double HistogramLowPercent = 50.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double HistogramHighPercent = 90.0; // < 100: no pumping on the lamp (pitfall 8)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double LocalExposureHighlightContrast = 0.8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double LocalExposureShadowContrast = 0.9;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor") double GrainG0 = 0.015;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor") double GrainMax = 0.06;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor") double GrainEvRef = 8.0;             // grain doubles per 2 EV below this
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor") double Vignette = 0.1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") double HeadTranslationScale = 0.3;   // Eyes 0.3, Headcam 1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") bool bStabiliseGaze = true;          // Eyes: rotation keeps the gaze target (VOR)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") double MountJitterDeg = 0.0;         // Headcam 1-4 Hz, 0.05-0.15 deg
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") FRbHeadMotionParams HeadMotion;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double EyeBehindTipM = 0.45;      // s_e (plan 4.2)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double EyeAboveCueM = 0.10;       // h_c
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double VisionCenterM = 0.0;       // y_vc (calibrated), + = right of the cue
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double StandingEyeHeightM = 1.65;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double GetDownSeconds = 1.0;      // transition 0.8-1.5 s
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double BallInHandSeconds = 0.5;   // lean over the table to place
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double BallInHandLeanM = 0.20;    // forward and down while placing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double CueAxisSmoothingSeconds = 0.08; // the head follows the aim, not the tremor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double LookAheadM = 1.0;          // gaze point on the aim line without an object ball
};

UCLASS(BlueprintType)
class RAWBREAK_API URbCameraModel : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	URbCameraModel();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Camera")
	FRbCameraPresetParams Eyes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Camera")
	FRbCameraPresetParams Headcam;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Camera")
	FRbCameraPresetParams Broadcast;

	const FRbCameraPresetParams& Get(ERbCameraPreset Preset) const;
};

namespace RbCameraModel
{
	// Compiled-in defaults of plan 4.9 (Broadcast = replay cameras, long lens).
	RAWBREAK_API FRbCameraPresetParams Defaults(ERbCameraPreset Preset);

	// Render overscan s_over of the preset's distortion at a viewport aspect (plan 4.3: 1.0 for Eyes, 1.1926 for the Headcam at
	// 16:9). Only the distortion post-process (post-M1) renders with it; the M1 projection is the base one.
	RAWBREAK_API double Overscan(const FRbCameraPresetParams& Params, double AspectWidthOverHeight);

	// Sensor height of every RAW BREAK cine camera [mm]: the 36 mm wide 16:9 back of plan 4.5 (f = 21.71 mm for V = 50 deg). The
	// width follows the viewport aspect (review R-06).
	inline constexpr double SensorHeightMm = 20.25;
}
