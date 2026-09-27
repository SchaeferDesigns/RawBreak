#pragma once

// Camera and render mathematics of ue5-realism-plan 4.3-4.7, 6.1, 6.2, 6.5, 6.7 (tests T1-T8, T19-T22).
// Pure functions on doubles (degrees where the name says so, SI otherwise). Owner: UE-5b (camera: T2-T5, T8)
// and UE-3 (render: T1, T6, T7, T19-T22) - the functions are split by owner below; each owner implements its
// block in Private/Math/RbCameraMath_<Owner>.cpp.

#include "CoreMinimal.h"

namespace RbCameraMath
{
	// ---- UE-5b: projection and depth of field (RbCameraMath_Camera.cpp) ----------------------------------

	// tan(V/2) = tan(H/2) * (H_px / W_px): FOVs in degrees (T2, T3).
	RAWBREAK_API double VerticalFromHorizontalFovDeg(double HorizontalDeg, double AspectWidthOverHeight);
	RAWBREAK_API double HorizontalFromVerticalFovDeg(double VerticalDeg, double AspectWidthOverHeight);

	// H_nat = 2 atan(W_screen / (2 D_view)) [deg] (T4).
	RAWBREAK_API double NaturalMonitorFovDeg(double ScreenWidthMeters, double ViewDistanceMeters);

	// Headcam barrel distortion (Brown-Conrady, plan 4.3): overscan s_over and the render / effective FOVs (T5).
	struct FDistortionFit
	{
		double Overscan = 1.0;
		double RenderHorizontalDeg = 0.0;
		double EffectiveHorizontalDeg = 0.0;
	};
	RAWBREAK_API FDistortionFit DistortionOverscan(double BaseHorizontalDeg, double AspectWidthOverHeight, double K1, double K2);

	// Pixels per radian at the image centre: (W_px / 2) / tan(H/2).
	RAWBREAK_API double PixelsPerRadian(double WidthPixels, double HorizontalDeg);

	// Eye DoF blur angle beta = A |1/s - 1/d| [rad] (T8).
	RAWBREAK_API double EyeBlurAngle(double PupilDiameterM, double FocusDistanceM, double ObjectDistanceM);

	// Cine-camera thin-lens equivalent of a pupil: f = w_sensor / (2 tan(H/2)) [mm], N = f / A.
	RAWBREAK_API void PupilToCineLens(double SensorWidthMm, double HorizontalDeg, double PupilDiameterMm, double& OutFocalLengthMm, double& OutFStop);

	// ---- UE-3: exposure, surfaces, occlusion, lighting (RbCameraMath_Render.cpp) --------------------------

	// F0 = ((n - 1) / (n + 1))^2; legacy Specular = F0 / 0.08 (T1).
	RAWBREAK_API double FresnelF0FromIor(double Ior);

	// L = rho E / pi [cd/m^2]; EV100 = log2(8 L) (T6).
	RAWBREAK_API double LuminanceFromIlluminance(double IlluminanceLux, double Albedo);
	RAWBREAK_API double Ev100FromLuminance(double Luminance);

	// grain = clamp(g0 2^(max(0, EV_ref - EV) / 2), 0, g_max) (T7).
	RAWBREAK_API double ExposureCoupledGrain(double G0, double EvRef, double Ev, double GMax);

	// Occ(rho) = R^3 / (rho^2 + R^2)^(3/2) (T19).
	RAWBREAK_API double BallOcclusion(double Rho, double Radius);

	// Silhouette sagitta error e = R (1 - cos(pi / N)) (T20).
	RAWBREAK_API double SphereTessellationError(double Radius, int32 Segments);

	// Illuminance at a bed point from downward point sources: E = sum I_i h_i / d_i^3 [lux] (T21); I0 = Phi / pi (T22).
	struct FPointLamp
	{
		FVector3d Position; // [m] (z = height above the bed)
		double IntensityCd = 0.0;
	};
	RAWBREAK_API double IlluminanceAt(const FVector3d& BedPoint, const FPointLamp* Lamps, int32 Count);
	RAWBREAK_API double LambertianIntensityFromFlux(double FluxLumen);
}
