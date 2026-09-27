#include "Math/RbCameraMath.h"

// Owner: UE-5b. TODO(UE-5b): implement and verify T2-T5, T8 (Private/Tests/RbCameraMathTests.cpp).

namespace RbCameraMath
{
	double VerticalFromHorizontalFovDeg(double HorizontalDeg, double AspectWidthOverHeight)
	{
		const double T = FMath::Tan(FMath::DegreesToRadians(0.5 * HorizontalDeg)) / AspectWidthOverHeight;
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(T));
	}

	double HorizontalFromVerticalFovDeg(double VerticalDeg, double AspectWidthOverHeight)
	{
		const double T = FMath::Tan(FMath::DegreesToRadians(0.5 * VerticalDeg)) * AspectWidthOverHeight;
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(T));
	}

	double NaturalMonitorFovDeg(double /*ScreenWidthMeters*/, double /*ViewDistanceMeters*/)
	{
		return 0.0; // TODO(UE-5b)
	}

	FDistortionFit DistortionOverscan(double BaseHorizontalDeg, double /*AspectWidthOverHeight*/, double /*K1*/, double /*K2*/)
	{
		FDistortionFit Fit;
		Fit.RenderHorizontalDeg = BaseHorizontalDeg; // TODO(UE-5b)
		Fit.EffectiveHorizontalDeg = BaseHorizontalDeg;
		return Fit;
	}

	double PixelsPerRadian(double WidthPixels, double HorizontalDeg)
	{
		return 0.5 * WidthPixels / FMath::Tan(FMath::DegreesToRadians(0.5 * HorizontalDeg));
	}

	double EyeBlurAngle(double /*PupilDiameterM*/, double /*FocusDistanceM*/, double /*ObjectDistanceM*/)
	{
		return 0.0; // TODO(UE-5b)
	}

	void PupilToCineLens(double /*SensorWidthMm*/, double /*HorizontalDeg*/, double /*PupilDiameterMm*/, double& OutFocalLengthMm, double& OutFStop)
	{
		OutFocalLengthMm = 0.0; // TODO(UE-5b)
		OutFStop = 0.0;
	}
}
