#pragma once

// Procedural head motion layer of the first-person camera (ue5-realism-plan 4.8). M1 has no body, so the layer moves the
// camera itself (later it drives the head bone of the full-body character so shadows and reflections agree, plan 5.1).
//
//   walking bob     vertical at the step rate f = f0 + k v (~2 Hz at 1.4 m/s), 3 cm p-p at 0.8 m/s, 4.5 cm at 1.4 m/s
//   walking sway    lateral at the stride rate f / 2, 2.5 cm p-p at 1.4 m/s
//   breathing       0.25 Hz, head ~2.5 mm standing, ~1.5 mm down on the shot
//   postural sway   0.1-0.5 Hz band (two octaves of Perlin noise), ~4.5 mm standing, ~1 mm down (bridge hand)
//   settle          breathing and sway reduced by 70 % over 1.5 s while the Settle input is held (HF-06)
//   Headcam only    head nod / roll with the bob and sway (no vestibulo-ocular reflex) + 1-4 Hz mount jitter
// Every translation is scaled by the preset's HeadTranslationScale (Eyes 0.3) and the comfort scale (URbGameUserSettings
// HeadBobScale, 0 with Reduced motion). Deterministic: a function of the accumulated time and the integrated step phase (a
// speed change never makes the bob jump). Owner: UE-5b.

#include "CoreMinimal.h"

#include "Camera/RbCameraModel.h"

struct FRbHeadMotionSample
{
	// Head translation in the yaw-only view frame: X forward, Y right, Z up [cm].
	FVector Offset = FVector::ZeroVector;
	// Extra head rotation (Headcam: nod, roll, mount jitter); zero for gaze-stabilised presets.
	FRotator Rotation = FRotator::ZeroRotator;
};

class RAWBREAK_API FRbHeadMotion
{
public:
	// Advances the clock by Dt [s] at horizontal walking speed SpeedMps [m/s]; bDown = down on the shot (smaller breathing /
	// sway, no walking); SettleAlpha in [0, 1] = how far the settle has progressed; MotionScale = comfort scale (0 = off).
	FRbHeadMotionSample Step(double Dt, double SpeedMps, bool bDown, double SettleAlpha, const FRbCameraPresetParams& Params, double MotionScale);

	void Reset();
	double GetTime() const { return Time; }
	double GetStepPhase() const { return StepPhase; }

	// Walking bob peak-to-peak [cm] at a speed (plan 4.8 table: 3 cm at 0.8 m/s, 4.5 cm at 1.4 m/s, linear to 0 at rest).
	static double BobPeakToPeakCm(double SpeedMps, const FRbHeadMotionParams& Params);

private:
	double Time = 0.0;
	double StepPhase = 0.0; // [rad] of the step cycle (the stride is half of it)
	double SmoothedSpeed = 0.0; // [m/s] the gait follows the walking speed with a 0.25 s lag
};
