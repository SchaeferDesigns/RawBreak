#pragma once

// Stroke-input mathematics (ue5-realism-plan 5.3-5.4, tests T9-T12). Pure functions on doubles: no UObjects,
// no engine state, frame-rate independent (pitfall 19). Owner: UE-5a.

#include "CoreMinimal.h"

// One raw input report projected on the stroke axis.
struct FRbStrokeSample
{
	double Time = 0.0;     // [s] QPC-based (FPlatformTime::Seconds domain)
	double Position = 0.0; // hand position along the stroke axis [m] (+ = forward, toward the ball)
};

// Nonlinear hand -> cue gain curve G(|v_m|) of plan 5.4.
struct FRbStrokeGain
{
	double G0 = 2.0;        // gain at low speed
	double VKnee = 0.5;     // [m/s]
	double GMax = 5.0;
	double VSat = 2.0;      // [m/s]
	double VTipMax = 12.0;  // [m/s] clamp of the resulting tip speed (core accepts up to rb::kMaxCueSpeed)
};

namespace RbStrokeMath
{
	// counts / DPI * 0.0254 [m].
	RAWBREAK_API double CountsToMeters(double Counts, double Dpi);

	// G(|v|) of plan 5.4 (piecewise linear between the knee and saturation).
	RAWBREAK_API double Gain(double HandSpeed, const FRbStrokeGain& Params);

	// Cue speed from the hand speed: G(|v_m|) v_m, clamped to +-VTipMax (T10).
	RAWBREAK_API double CueSpeedFromHandSpeed(double HandSpeed, const FRbStrokeGain& Params);

	// Velocity at time T from a quadratic least-squares fit over the samples in [T - Window, T] (Savitzky-Golay
	// derivative evaluated AT T, not at the window centre; T9). Returns false with fewer than 3 samples.
	RAWBREAK_API bool QuadraticFitVelocity(const FRbStrokeSample* Samples, int32 Count, double T, double Window, double& OutVelocity);

	// The same with a linear fit (documents the half-window lag, T9).
	RAWBREAK_API bool LinearFitVelocity(const FRbStrokeSample* Samples, int32 Count, double T, double Window, double& OutVelocity);

	// Time at which the cue position x_c(t) crosses 0 moving forward between two samples (linear inside the step).
	RAWBREAK_API bool FindContactCrossing(const FRbStrokeSample* CuePositions, int32 Count, double& OutTime);

	// Steering (plan 5.4): yaw error d_psi = y_g / L_bg [rad] and tip side shift d_a = -d_psi L_bt [m] (T11).
	RAWBREAK_API void Steering(double GripLateral, double BridgeToGrip, double BridgeToTip, double& OutYawError, double& OutTipShift);

	// Cue height at the bridge for a centre-ball hit (plan 5.3): z_P = R + R sin(theta), z_bridge = z_P + L_b sin(theta) [m] (T12).
	RAWBREAK_API double BridgeHeight(double BallRadius, double Elevation, double BridgeLength);
}
