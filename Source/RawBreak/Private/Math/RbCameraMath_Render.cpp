#include "Math/RbCameraMath.h"

// Render mathematics of ue5-realism-plan 4.4 (exposure), 4.7 (grain), 6.0/6.2 (F0), 6.1 (lamp illuminance),
// 6.5 (analytic ball occlusion) and 6.7 (tessellation). Owner: UE-3; verified by RawBreak.Unit.Render.* (T1, T6, T7,
// T19-T22 in Private/Tests/RbRenderMathTests.cpp). The shader side of T19 is Shaders/Private/RbBallOcclusion.ush.

namespace RbCameraMath
{
	namespace
	{
		// Reflected-light meter calibration (ISO 2720): EV100 = log2(L S / K), S = 100, K = 12.5 -> log2(8 L).
		constexpr double MeterIso = 100.0;
		constexpr double MeterK = 12.5;
	}

	double FresnelF0FromIor(double Ior)
	{
		// Normal-incidence Fresnel reflectance of a dielectric in air (plan 6.2: phenolic n = 1.57 -> 0.049).
		const double R = (Ior - 1.0) / (Ior + 1.0);
		return R * R;
	}

	double LuminanceFromIlluminance(double IlluminanceLux, double Albedo)
	{
		// Lambertian surface: L = rho E / pi [cd/m^2].
		return Albedo * IlluminanceLux / UE_DOUBLE_PI;
	}

	double Ev100FromLuminance(double Luminance)
	{
		// log2 of a non-positive luminance is undefined: report the darkest representable value instead of NaN.
		if (!(Luminance > 0.0))
		{
			return -TNumericLimits<double>::Max();
		}
		return FMath::Log2(Luminance * MeterIso / MeterK);
	}

	double ExposureCoupledGrain(double G0, double EvRef, double Ev, double GMax)
	{
		// Sensor gain doubles per EV of under-exposure, shot-noise std ~ sqrt(gain): 2^(dEV / 2) (plan 4.7).
		const double Under = FMath::Max(0.0, EvRef - Ev);
		return FMath::Clamp(G0 * FMath::Pow(2.0, 0.5 * Under), 0.0, GMax);
	}

	double BallOcclusion(double Rho, double Radius)
	{
		// Cosine-weighted occlusion of a sphere resting on the plane (plan 6.5): R^3 / (rho^2 + R^2)^(3/2).
		const double D2 = Rho * Rho + Radius * Radius;
		return D2 > 0.0 ? Radius * Radius * Radius / (D2 * FMath::Sqrt(D2)) : 0.0;
	}

	double SphereTessellationError(double Radius, int32 Segments)
	{
		// Sagitta of one great-circle segment: R (1 - cos(pi / N)); fewer than 3 segments is no polygon.
		if (Segments < 3)
		{
			return Radius;
		}
		return Radius * (1.0 - FMath::Cos(UE_DOUBLE_PI / static_cast<double>(Segments)));
	}

	double IlluminanceAt(const FVector3d& BedPoint, const FPointLamp* Lamps, int32 Count)
	{
		// Horizontal illuminance from downward point sources (plan 6.1): E = sum I_i h_i / d_i^3, h_i = height of the
		// source above the point. A source at or below the point's plane adds nothing (it cannot light a face-up bed).
		double E = 0.0;
		for (int32 I = 0; Lamps && I < Count; ++I)
		{
			const FVector3d D = Lamps[I].Position - BedPoint;
			const double H = D.Z;
			if (H <= 0.0)
			{
				continue;
			}
			const double Dist = D.Length();
			E += Lamps[I].IntensityCd * H / (Dist * Dist * Dist);
		}
		return E;
	}

	double LambertianIntensityFromFlux(double FluxLumen)
	{
		// Lambertian emitter into the lower hemisphere: Phi = pi I0 (plan 6.1).
		return FluxLumen / UE_DOUBLE_PI;
	}
}
