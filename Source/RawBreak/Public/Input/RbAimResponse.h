#pragma once

// Mouse -> aim / look response (playtest 2026-09-28 P3; ui-ux 13.6; Docs/ue-architecture.md 18.3).
//
// M1 root cause of "~1.9 m of mouse travel for 90 deg": the Look action delivered mouse counts x the engine's legacy Mouse2D axis
// sensitivity 0.07 (BaseInput.ini AxisConfig, which Enhanced Input turns into a hidden Scalar modifier on every mouse mapping,
// EnhancedInputSubsystemInterface.cpp ApplyAxisPropertyModifiers), and the stroke component turned that into azimuth with
// 0.0005 rad per unit -> 3.5e-5 rad (0.002 deg) per count: 90 deg = 44,900 counts = 1.43 m at 800 DPI (1.9 m at ~600 DPI). The
// aim therefore works in centimetres of mouse travel: counts -> cm with the calibrated DPI (URbGameUserSettings::MouseDpi), then
// the settings' deg/cm (FRbControlSettings), independent of engine axis scales (DefaultInput.ini neutralises the axis config and
// ARbPlayerController re-asserts it at runtime, so the Look action carries raw counts).
//
//   coarse   dphi = AimDegreesPerCm x AimSensitivity x cm            (default 7.2 deg/cm = 90 deg per 12.5 cm)
//   fine     x FineAimFactor while Shift is held                      (default 0.075 = 13.3x slower)
//   accel    x gain(hand speed) blended in by AimAcceleration         (0 = linear)
//
// Acceleration curve (M2-F): g(v) = clamp((v / v_ref)^p, g_min, g_max) with v_ref = ReferenceSpeedCmPerSecond (10 cm/s, a
// deliberate aiming move), p = 0.6, g_min = 0.25 (slow hand = up to 4x finer), g_max = 3 (fast flick = up to 3x coarser);
// gain = 1 + a (g(v) - 1). Monotone non-decreasing and continuous in v, gain 1 at v_ref for every a, gain 1 everywhere at a = 0.
// v = |cm| / dt of the delta, so the same steady hand speed gives the same gain at any frame rate. The LINEAR path (a = 0) is exact:
// callers that need bitwise frame-split independence accumulate the integer counts and apply RadiansPerCount once (see
// URbStrokeComponent::AddAimInput).
// Standing look: LookDegreesPerCm 22 x LookSensitivity (invert Y is the caller's).
// Pure functions. Owner: M2-F.

#include "CoreMinimal.h"

#include "Settings/RbSettingsTypes.h"

namespace RbAimResponse
{
	inline constexpr double ReferenceSpeedCmPerSecond = 10.0;
	inline constexpr double AccelerationExponent = 0.6;
	inline constexpr double MinAccelerationGain = 0.25;
	inline constexpr double MaxAccelerationGain = 3.0;

	// Centimetres of mouse travel of Counts at Dpi counts per inch.
	RAWBREAK_API double CountsToCm(double Counts, double Dpi);

	// Acceleration gain at a hand speed [cm/s], blended by Acceleration (0 = 1 everywhere).
	RAWBREAK_API double AccelerationGain(double HandSpeedCmPerSecond, double Acceleration);

	// Linear azimuth change of ONE count [rad] (coarse, or fine with bFine): the exact factor of the linear path.
	RAWBREAK_API double RadiansPerCount(bool bFine, double Dpi, const FRbControlSettings& Settings);

	// Azimuth change [rad] of the cue for a mouse delta of Counts over DeltaSeconds (> 0; used by the acceleration only).
	RAWBREAK_API double AimRadians(double Counts, double DeltaSeconds, bool bFine, double Dpi, const FRbControlSettings& Settings);

	// Standing look change [deg] for a mouse delta of Counts.
	RAWBREAK_API double LookDegrees(double Counts, double Dpi, const FRbControlSettings& Settings);

	// Mouse travel [cm] for Degrees of coarse aim (the settings row "90 deg per x cm").
	RAWBREAK_API double CmForAimDegrees(double Degrees, const FRbControlSettings& Settings);
}
