#pragma once

// Stroke-input mathematics (ue5-realism-plan 5.3-5.4, tests T9-T12). Pure functions on doubles: no UObjects,
// no engine state, frame-rate independent (pitfall 19). Owner: UE-5a.
//
// Frame-rate independence (UE-5a acceptance): every quantity of the stroke is a function of the TIMESTAMPED samples
// alone. The cue displacement is integrated sample by sample (FRbCueIntegrator, one step per sample in time order), the
// crossing of the ball surface is interpolated inside the sample step, and the tip speed at contact is a quadratic
// least-squares fit over the samples of a time window, evaluated at the crossing time. How the samples are split into
// frames never enters, so the IntendedStroke is bitwise identical at 30 / 60 / 144 fps.

#include "CoreMinimal.h"

// One stroke-input sample: hand position along the stroke axis (and sideways, for steering) at a report time.
struct FRbStrokeSample
{
	double Time = 0.0;     // [s] QPC-based (FPlatformTime::Seconds domain)
	double Position = 0.0; // hand position along the stroke axis [m] (+ = forward, toward the ball)
	double Lateral = 0.0;  // hand position sideways [m] (+ = shooter's right): steering input of plan 5.4
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

// Result of one integration step of the cue displacement.
struct FRbCueStepResult
{
	double CueDelta = 0.0;     // [m] change of x_c in this step (after the limits)
	bool bCrossed = false;     // x_c crossed 0 moving forward inside this step (only when the limit allows it)
	double CrossingTime = 0.0; // [s] linear interpolation of the crossing inside the step
	bool bHitFrontLimit = false; // stopped at the front limit (practice stroke stop-short)
	bool bHitBackLimit = false;  // stopped at the back limit (max backswing)
	bool bReference = false;     // first sample of the stroke: it only set the hand origin
};

// Integrates the cue displacement x_c (0 = tip touching the ball, < 0 = behind it) from hand samples: per sample step,
// v_m = dx_m / dt, v_c = G(|v_m|) v_m clamped to +-VTipMax, dx_c = v_c dt (plan 5.4). The component and the scripted
// stroke generator use this one class, so a scripted stroke crosses exactly where the component detects it.
class RAWBREAK_API FRbCueIntegrator
{
public:
	// Starts a stroke at cue displacement X (no reference sample yet: the first sample only sets the hand origin).
	void Reset(double X);

	// Adds a sample. bLive: the stroke is a shot (Commit held / Hardcore): no front limit, a forward crossing of 0 is
	// reported (x_c keeps the value after the step; the caller ends the stroke). Otherwise x_c stops at -StopShort
	// (practice stroke, plan 5.4). x_c never moves behind -MaxBackswing. The limits only stop motion toward them.
	FRbCueStepResult Step(const FRbStrokeSample& Sample, const FRbStrokeGain& Gain, bool bLive, double StopShort, double MaxBackswing);

	double GetX() const { return X; }
	bool HasReference() const { return bHasPrevious; }
	const FRbStrokeSample& GetPrevious() const { return Previous; }

	// Smallest time step used for the velocity (coalesced reports with equal time stamps) [s].
	static constexpr double MinStep = 1.0e-4;

private:
	double X = 0.0;
	bool bHasPrevious = false;
	FRbStrokeSample Previous;
};

// Parameters of a scripted (synthetic) stroke: a slow backswing, a still pause at the back (a still mouse sends no
// reports), then a uniformly accelerated forward hand stroke that crosses the ball with the requested tip speed.
struct FRbScriptedStroke
{
	double TipSpeed = 2.0;              // [m/s] tip speed at the crossing (after the gain curve)
	double StartTime = 0.0;             // [s] time of the first sample (the hand origin)
	double StartCueDisplacement = -0.03;// [m] x_c of the cue when the stroke starts
	double SampleRate = 1000.0;         // [Hz] report rate
	double BackswingTime = 0.5;         // [s]
	double Pause = 0.3;                 // [s] still hand at the back of the stroke (IntendedStroke::PauseDuration)
	double ForwardTravel = 0.20;        // [m] cue travel of the final forward stroke (sets the hand acceleration)
	double FollowThrough = 0.05;        // [s] of samples after the crossing
	double MaxBackswing = 0.30;         // [m] back limit of x_c (as in the component)
	double LateralDrift = 0.0;          // [m] sideways hand travel spread uniformly over the forward stroke (steering)
};

namespace RbStrokeMath
{
	// counts / DPI * 0.0254 [m].
	RAWBREAK_API double CountsToMeters(double Counts, double Dpi);

	// G(|v|) of plan 5.4 (piecewise linear between the knee and saturation).
	RAWBREAK_API double Gain(double HandSpeed, const FRbStrokeGain& Params);

	// Cue speed from the hand speed: G(|v_m|) v_m, clamped to +-VTipMax (T10).
	RAWBREAK_API double CueSpeedFromHandSpeed(double HandSpeed, const FRbStrokeGain& Params);

	// Inverse of CueSpeedFromHandSpeed for |CueSpeed| <= VTipMax (odd function; the clamp is not inverted).
	RAWBREAK_API double HandSpeedForCueSpeed(double CueSpeed, const FRbStrokeGain& Params);

	// d v_c / d v_m at HandSpeed: G + |v| G'(|v|) inside the curve, 0 where the tip-speed clamp bites.
	RAWBREAK_API double CueSpeedDerivative(double HandSpeed, const FRbStrokeGain& Params);

	// Cue travel needed to reach HandSpeed from rest with a uniformly accelerated hand of acceleration 1 m/s^2:
	// S(v) = integral_0^v G(u) u du (the travel for acceleration a is S(v) / a).
	RAWBREAK_API double CueTravelIntegral(double HandSpeed, const FRbStrokeGain& Params);

	// Velocity at time T from a quadratic least-squares fit over the samples in [T - Window, T] (Savitzky-Golay
	// derivative evaluated AT T, not at the window centre; T9). Returns false with fewer than 3 samples.
	RAWBREAK_API bool QuadraticFitVelocity(const FRbStrokeSample* Samples, int32 Count, double T, double Window, double& OutVelocity);

	// General form: quadratic least-squares fit of Position over the samples with Time in [WindowStart, WindowEnd]
	// (inclusive, 1 ns slack), evaluated at T: velocity and acceleration. Times are centred on T and scaled by the window
	// before the fit (no cancellation with QPC-sized times). Returns false with fewer than 3 samples or a singular fit.
	RAWBREAK_API bool QuadraticFit(const FRbStrokeSample* Samples, int32 Count, double T, double WindowStart, double WindowEnd,
		double& OutVelocity, double& OutAcceleration);

	// The same fit on the Lateral coordinate (steering swoop at contact).
	RAWBREAK_API bool QuadraticFitLateral(const FRbStrokeSample* Samples, int32 Count, double T, double WindowStart, double WindowEnd,
		double& OutVelocity, double& OutAcceleration);

	// The same with a linear fit (documents the half-window lag, T9). Returns false with fewer than 2 samples.
	RAWBREAK_API bool LinearFitVelocity(const FRbStrokeSample* Samples, int32 Count, double T, double Window, double& OutVelocity);

	// Time at which the cue position x_c(t) crosses 0 moving forward between two samples (linear inside the step).
	// CuePositions[i].Position = x_c at CuePositions[i].Time. Returns the FIRST forward crossing.
	RAWBREAK_API bool FindContactCrossing(const FRbStrokeSample* CuePositions, int32 Count, double& OutTime);

	// Steering (plan 5.4): yaw error d_psi = y_g / L_bg [rad] and tip side shift d_a = -d_psi L_bt [m] (T11).
	RAWBREAK_API void Steering(double GripLateral, double BridgeToGrip, double BridgeToTip, double& OutYawError, double& OutTipShift);

	// Cue height at the bridge for a centre-ball hit (plan 5.3): z_P = R + R sin(theta), z_bridge = z_P + L_b sin(theta) [m] (T12).
	RAWBREAK_API double BridgeHeight(double BallRadius, double Elevation, double BridgeLength);

	// Builds a scripted stroke (tests, RbStroke cheat, dev captures): hand samples at Params.SampleRate whose final forward
	// stroke crosses the ball at Params.TipSpeed. The backswing length is solved (bisection) with FRbCueIntegrator, so the
	// crossing happens exactly at the hand speed HandSpeedForCueSpeed(TipSpeed) of the uniformly accelerated forward
	// stroke, and the quadratic fit at the crossing (exact for quadratic data) returns TipSpeed. OutContactTime = the
	// crossing time, OutForwardStart = the start of the forward stroke (end of the pause). False if the stroke does not
	// fit inside MaxBackswing.
	RAWBREAK_API bool MakeScriptedStroke(const FRbScriptedStroke& Params, const FRbStrokeGain& Gain, TArray<FRbStrokeSample>& OutSamples,
		double* OutContactTime = nullptr, double* OutForwardStart = nullptr);
}
