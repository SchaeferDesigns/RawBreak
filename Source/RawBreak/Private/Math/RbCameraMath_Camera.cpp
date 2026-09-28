#include "Math/RbCameraMath.h"

// Owner: UE-5b. Projection and depth-of-field maths of ue5-realism-plan 4.3 / 4.5 (tests T2-T5, T8 in
// Private/Tests/RbCameraMathTests.cpp). Pure functions on doubles: degrees where the name says so, SI otherwise.

namespace RbCameraMath
{
	namespace
	{
		double TanHalfDeg(double Deg)
		{
			return FMath::Tan(FMath::DegreesToRadians(0.5 * Deg));
		}

		double FullAngleDeg(double TanHalf)
		{
			return FMath::RadiansToDegrees(2.0 * FMath::Atan(TanHalf));
		}
	}

	// tan(V/2) = tan(H/2) * (H_px / W_px) (T2).
	double VerticalFromHorizontalFovDeg(double HorizontalDeg, double AspectWidthOverHeight)
	{
		return FullAngleDeg(TanHalfDeg(HorizontalDeg) / AspectWidthOverHeight);
	}

	// tan(H/2) = tan(V/2) * (W_px / H_px) (T3).
	double HorizontalFromVerticalFovDeg(double VerticalDeg, double AspectWidthOverHeight)
	{
		return FullAngleDeg(TanHalfDeg(VerticalDeg) * AspectWidthOverHeight);
	}

	// H_nat = 2 atan(W_screen / (2 D_view)) (T4).
	double NaturalMonitorFovDeg(double ScreenWidthMeters, double ViewDistanceMeters)
	{
		return FullAngleDeg(0.5 * ScreenWidthMeters / ViewDistanceMeters);
	}

	// Brown-Conrady barrel distortion of the Headcam (plan 4.3, T5). Coordinates in units of the base camera's horizontal
	// half-width (x = 1 at the left / right edge), so the image corner sits at rho_corner = sqrt(1 + (H_px / W_px)^2):
	//   s_over = 1 + k1 rho_c^2 + k2 rho_c^4                render tan(H/2) = s_over tan(H0/2)   (corners have source data)
	//   H_eff  = 2 atan((1 + k1 + k2) tan(H0/2))           the content visible at the left / right edge (rho = 1)
	FDistortionFit DistortionOverscan(double BaseHorizontalDeg, double AspectWidthOverHeight, double K1, double K2)
	{
		const double InvAspect = 1.0 / AspectWidthOverHeight;
		const double RhoCorner2 = 1.0 + InvAspect * InvAspect;
		const double TanHalf = TanHalfDeg(BaseHorizontalDeg);
		FDistortionFit Fit;
		Fit.Overscan = 1.0 + K1 * RhoCorner2 + K2 * RhoCorner2 * RhoCorner2;
		Fit.RenderHorizontalDeg = FullAngleDeg(Fit.Overscan * TanHalf);
		Fit.EffectiveHorizontalDeg = FullAngleDeg((1.0 + K1 + K2) * TanHalf);
		return Fit;
	}

	// p = (W_px / 2) / tan(H/2) (plan 4.3).
	double PixelsPerRadian(double WidthPixels, double HorizontalDeg)
	{
		return 0.5 * WidthPixels / TanHalfDeg(HorizontalDeg);
	}

	// beta = A |1/s - 1/d| (thin lens, small angles; plan 4.5, T8).
	double EyeBlurAngle(double PupilDiameterM, double FocusDistanceM, double ObjectDistanceM)
	{
		return PupilDiameterM * FMath::Abs(1.0 / FocusDistanceM - 1.0 / ObjectDistanceM);
	}

	// Thin-lens camera with the pupil as its entrance pupil: aperture diameter f / N = A (plan 4.5). The angular blur of a cine
	// camera is (f / N) |1/s - 1/d| (CoC on the sensor A f |1/s - 1/d| over the focal length), identical to the eye's beta.
	void PupilToCineLens(double SensorWidthMm, double HorizontalDeg, double PupilDiameterMm, double& OutFocalLengthMm, double& OutFStop)
	{
		OutFocalLengthMm = 0.5 * SensorWidthMm / TanHalfDeg(HorizontalDeg);
		OutFStop = PupilDiameterMm > 0.0 ? OutFocalLengthMm / PupilDiameterMm : 0.0;
	}
}
