#pragma once

// Mouse -> aim / look response (playtest 2026-09-28 P3; ui-ux 13.6; Docs/ue-architecture.md 18.3).
//
// M1 root cause of "~1.9 m of mouse travel for 90 deg": the Look action delivers mouse counts x the engine's legacy Mouse2D axis
// sensitivity 0.07 (BaseInput.ini AxisConfig, plus bEnableMouseSmoothing), and the stroke component turned that into azimuth
// with AimSensitivity 0.0005 rad per unit -> 3.5e-5 rad (0.002 deg) per count: 90 deg = 44,900 counts = 1.43 m at 800 DPI
// (1.9 m at ~600 DPI). The aim therefore works in centimetres of mouse travel: counts -> cm with the calibrated DPI
// (URbGameUserSettings::MouseDpi), then the settings' deg/cm (FRbControlSettings), independent of engine axis scales.
//
//   coarse   dphi = AimDegreesPerCm x AimSensitivity x cm            (default 7.2 deg/cm = 90 deg per 12.5 cm)
//   fine     x FineAimFactor while Shift is held                      (default 0.075 = 13x slower)
//   accel    optional gain(hand speed) blended in by AimAcceleration  (1 at the reference speed; M2-F defines the curve:
//            monotone, slow hand motion finer, fast motion coarser, continuous, frame-rate independent)
// Pure functions; the input source must deliver counts per REPORT with their time (FRbRawMouseInput) or per frame with the frame
// time - never a smoothed value. Owner: M2-F (linear part implemented by the M2 architect step, acceleration TODO(M2-F)).

#include "CoreMinimal.h"

#include "Settings/RbSettingsTypes.h"

namespace RbAimResponse
{
	// Centimetres of mouse travel of Counts at Dpi counts per inch.
	RAWBREAK_API double CountsToCm(double Counts, double Dpi);

	// Acceleration gain at a hand speed [cm/s], blended by Acceleration (0 = 1 everywhere).
	RAWBREAK_API double AccelerationGain(double HandSpeedCmPerSecond, double Acceleration);

	// Azimuth change [rad] of the cue for a mouse delta of Counts over DeltaSeconds (> 0; used by the acceleration only).
	RAWBREAK_API double AimRadians(double Counts, double DeltaSeconds, bool bFine, double Dpi, const FRbControlSettings& Settings);

	// Standing look change [deg] for a mouse delta of Counts.
	RAWBREAK_API double LookDegrees(double Counts, double Dpi, const FRbControlSettings& Settings);
}
