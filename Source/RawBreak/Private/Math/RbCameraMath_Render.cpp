#include "Math/RbCameraMath.h"

// Owner: UE-3. TODO(UE-3): implement and verify T1, T6, T7, T19-T22 (Private/Tests/RbRenderMathTests.cpp).

namespace RbCameraMath
{
	double FresnelF0FromIor(double Ior)
	{
		const double R = (Ior - 1.0) / (Ior + 1.0);
		return R * R;
	}

	double LuminanceFromIlluminance(double /*IlluminanceLux*/, double /*Albedo*/)
	{
		return 0.0; // TODO(UE-3)
	}

	double Ev100FromLuminance(double /*Luminance*/)
	{
		return 0.0; // TODO(UE-3)
	}

	double ExposureCoupledGrain(double /*G0*/, double /*EvRef*/, double /*Ev*/, double /*GMax*/)
	{
		return 0.0; // TODO(UE-3)
	}

	double BallOcclusion(double Rho, double Radius)
	{
		const double D2 = Rho * Rho + Radius * Radius;
		return D2 > 0.0 ? Radius * Radius * Radius / (D2 * FMath::Sqrt(D2)) : 0.0;
	}

	double SphereTessellationError(double /*Radius*/, int32 /*Segments*/)
	{
		return 0.0; // TODO(UE-3)
	}

	double IlluminanceAt(const FVector3d& /*BedPoint*/, const FPointLamp* /*Lamps*/, int32 /*Count*/)
	{
		return 0.0; // TODO(UE-3)
	}

	double LambertianIntensityFromFlux(double /*FluxLumen*/)
	{
		return 0.0; // TODO(UE-3)
	}
}
