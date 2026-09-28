#include "Camera/RbHeadMotion.h"

// Owner: UE-5b. Procedural head motion of ue5-realism-plan 4.8 (header). Tests: RawBreak.Unit.Camera.HeadMotion_*.

namespace
{
	constexpr double kSlowSpeed = 0.8; // [m/s] of the 3 cm bob (Hirasaki et al. 1999)
	constexpr double kFastSpeed = 1.4; // [m/s] of the 4.5 cm bob and the 2.5 cm sway
	constexpr double kGaitLagSeconds = 0.25;

	// Band-limited noise in about [-1, 1]: two octaves of UE's gradient noise (one lattice cell per unit of X).
	double Noise(double X, double Seed)
	{
		const double A = FMath::PerlinNoise1D(static_cast<float>(X + Seed));
		const double B = FMath::PerlinNoise1D(static_cast<float>(2.07 * X + Seed + 17.3));
		return FMath::Clamp(1.6 * (0.7 * A + 0.3 * B), -1.0, 1.0);
	}
}

void FRbHeadMotion::Reset()
{
	Time = 0.0;
	StepPhase = 0.0;
	SmoothedSpeed = 0.0;
}

double FRbHeadMotion::BobPeakToPeakCm(double SpeedMps, const FRbHeadMotionParams& P)
{
	if (SpeedMps <= 0.0)
	{
		return 0.0;
	}
	if (SpeedMps < kSlowSpeed)
	{
		return P.BobPeakToPeakSlowCm * SpeedMps / kSlowSpeed;
	}
	const double PP = P.BobPeakToPeakSlowCm + (P.BobPeakToPeakCm - P.BobPeakToPeakSlowCm) * (SpeedMps - kSlowSpeed) / (kFastSpeed - kSlowSpeed);
	return FMath::Min(PP, 1.5 * P.BobPeakToPeakCm);
}

FRbHeadMotionSample FRbHeadMotion::Step(double Dt, double SpeedMps, bool bDown, double SettleAlpha, const FRbCameraPresetParams& Params,
	double MotionScale)
{
	const FRbHeadMotionParams& P = Params.HeadMotion;
	Dt = FMath::Max(0.0, Dt);
	Time += Dt;
	// The gait follows the speed with a short lag (a step-change of the input speed never makes the bob jump).
	const double TargetSpeed = bDown ? 0.0 : FMath::Max(0.0, SpeedMps);
	SmoothedSpeed = TargetSpeed + (SmoothedSpeed - TargetSpeed) * FMath::Exp(-Dt / kGaitLagSeconds);
	const double Speed = SmoothedSpeed;
	// The step phase is integrated (a speed change changes the rate, never the phase).
	StepPhase = FMath::Fmod(StepPhase + UE_DOUBLE_TWO_PI * (P.StepRateAtRestHz + P.StepRatePerMps * Speed) * Dt, 2.0 * UE_DOUBLE_TWO_PI);

	FRbHeadMotionSample Out;
	if (MotionScale <= 0.0)
	{
		return Out;
	}

	// Walking: vertical bob at the step rate, lateral sway at the stride rate (half the step rate).
	const double BobCm = 0.5 * BobPeakToPeakCm(Speed, P) * FMath::Sin(StepPhase);
	const double SwayCm = 0.5 * P.SwayPeakToPeakCm * FMath::Min(Speed / kFastSpeed, 1.5) * FMath::Sin(0.5 * StepPhase);

	// Breathing and postural sway, reduced while settled (exhale and hold, HF-06).
	const double Settle = 1.0 - P.SettleReduction * FMath::Clamp(SettleAlpha, 0.0, 1.0);
	const double BreathCm = 0.1 * (bDown ? P.BreathDownMm : P.BreathStandingMm) * Settle;
	const double Breath = FMath::Sin(UE_DOUBLE_TWO_PI * P.BreathRateHz * Time);
	const double PosturalCm = 0.1 * (bDown ? P.PosturalSwayDownMm : P.PosturalSwayStandingMm) * Settle;
	const double SwayX = Noise(0.23 * Time, 3.1);  // ~0.23 Hz fundamental + one octave: the 0.1-0.5 Hz band
	const double SwayY = Noise(0.19 * Time, 41.7);

	const double Translation = Params.HeadTranslationScale * MotionScale;
	Out.Offset.X = Translation * (0.3 * BreathCm * FMath::Sin(UE_DOUBLE_TWO_PI * P.BreathRateHz * Time + 0.5) + PosturalCm * SwayX);
	Out.Offset.Y = Translation * (SwayCm + PosturalCm * SwayY);
	Out.Offset.Z = Translation * (BobCm + BreathCm * Breath);

	if (!Params.bStabiliseGaze)
	{
		// A head-mounted camera nods with the bob and rolls with the stride; the mount adds a little 1-4 Hz jitter.
		const double Jitter = Params.MountJitterDeg * MotionScale;
		Out.Rotation.Pitch = -P.HeadPitchPerCmBob * BobCm * MotionScale + Jitter * Noise(2.3 * Time, 7.9);
		Out.Rotation.Yaw = Jitter * Noise(1.7 * Time, 13.1);
		Out.Rotation.Roll = P.HeadRollPerCmSway * SwayCm * MotionScale + Jitter * Noise(3.1 * Time, 29.5);
	}
	return Out;
}
