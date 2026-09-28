// Camera maths of ue5-realism-plan 4.3 / 4.5 against the numbers of plan section 13 (tests T2-T5, T8; UE-5b acceptance,
// Docs/ue-architecture.md 13). Owner: UE-5b.

#include "Math/RbCameraMath.h"
#include "Tests/RbTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbCameraMathTests
{
	constexpr double Aspect169 = 16.0 / 9.0;
	constexpr double Aspect219 = 64.0 / 27.0; // "21:9" of the plan (2560 x 1080)
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraMathT2, "RawBreak.Unit.CameraMath.T2_VerticalFromHorizontal", RB_UNIT_TEST_FLAGS)
bool FRbCameraMathT2::RunTest(const FString& Parameters)
{
	using namespace RbCameraMathTests;
	// T2: H = 80 deg at 16:9 -> V = 50.534 deg (0.01 deg).
	TestEqual(TEXT("T2 V(H = 80, 16:9)"), RbCameraMath::VerticalFromHorizontalFovDeg(80.0, Aspect169), 50.534, 0.01);
	// Round trip and the square case.
	TestEqual(TEXT("round trip"), RbCameraMath::HorizontalFromVerticalFovDeg(RbCameraMath::VerticalFromHorizontalFovDeg(80.0, Aspect169), Aspect169), 80.0, 1e-9);
	TestEqual(TEXT("square aspect"), RbCameraMath::VerticalFromHorizontalFovDeg(73.0, 1.0), 73.0, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraMathT3, "RawBreak.Unit.CameraMath.T3_HorizontalFromVertical", RB_UNIT_TEST_FLAGS)
bool FRbCameraMathT3::RunTest(const FString& Parameters)
{
	using namespace RbCameraMathTests;
	// T3: V = 50 deg -> H = 79.317 deg at 16:9, 95.728 deg at 64:27 (0.01 deg).
	TestEqual(TEXT("T3 H(V = 50, 16:9)"), RbCameraMath::HorizontalFromVerticalFovDeg(50.0, Aspect169), 79.317, 0.01);
	TestEqual(TEXT("T3 H(V = 50, 64:27)"), RbCameraMath::HorizontalFromVerticalFovDeg(50.0, Aspect219), 95.728, 0.01);
	// The Headcam's base: H0 = 90 deg at 16:9 is V = 58.7 deg (plan 4.9).
	TestEqual(TEXT("Headcam V(H0 = 90, 16:9)"), RbCameraMath::VerticalFromHorizontalFovDeg(90.0, Aspect169), 58.7, 0.02);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraMathT4, "RawBreak.Unit.CameraMath.T4_NaturalMonitorFov", RB_UNIT_TEST_FLAGS)
bool FRbCameraMathT4::RunTest(const FString& Parameters)
{
	// T4: 27" 16:9 monitor, 0.59773 m wide, seen from 0.70 m -> 46.24 deg (0.02 deg).
	TestEqual(TEXT("T4 27 in at 0.7 m"), RbCameraMath::NaturalMonitorFovDeg(0.59773, 0.70), 46.24, 0.02);
	TestEqual(TEXT("screen as wide as twice the distance -> 90 deg"), RbCameraMath::NaturalMonitorFovDeg(1.4, 0.70), 90.0, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraMathT5, "RawBreak.Unit.CameraMath.T5_DistortionOverscan", RB_UNIT_TEST_FLAGS)
bool FRbCameraMathT5::RunTest(const FString& Parameters)
{
	using namespace RbCameraMathTests;
	// T5: H0 = 90 deg, 16:9, k1 = 0.12, k2 = 0.02 -> s_over = 1.19263 (1e-4), render H = 100.04 deg, effective H = 97.49 deg (0.02).
	const RbCameraMath::FDistortionFit Fit = RbCameraMath::DistortionOverscan(90.0, Aspect169, 0.12, 0.02);
	TestEqual(TEXT("T5 overscan"), Fit.Overscan, 1.19263, 1e-4);
	TestEqual(TEXT("T5 render H"), Fit.RenderHorizontalDeg, 100.04, 0.02);
	TestEqual(TEXT("T5 effective H"), Fit.EffectiveHorizontalDeg, 97.49, 0.02);
	// No distortion: no overscan, render = effective = base.
	const RbCameraMath::FDistortionFit None = RbCameraMath::DistortionOverscan(79.317, Aspect169, 0.0, 0.0);
	TestEqual(TEXT("k = 0: overscan 1"), None.Overscan, 1.0, 1e-12);
	TestEqual(TEXT("k = 0: render = base"), None.RenderHorizontalDeg, 79.317, 1e-9);
	TestEqual(TEXT("k = 0: effective = base"), None.EffectiveHorizontalDeg, 79.317, 1e-9);
	// A wider screen has less vertical extent: its corners need less overscan.
	TestTrue(TEXT("21:9 overscan < 16:9 overscan"), RbCameraMath::DistortionOverscan(90.0, Aspect219, 0.12, 0.02).Overscan < Fit.Overscan);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraMathT8, "RawBreak.Unit.CameraMath.T8_EyeDofBlur", RB_UNIT_TEST_FLAGS)
bool FRbCameraMathT8::RunTest(const FString& Parameters)
{
	using namespace RbCameraMathTests;
	// T8: A = 4 mm, focus 1.5 m, shaft at 0.25 m, 2560 px wide, V = 50 deg at 16:9 -> beta = 0.013333 rad, p = 1544.04 px/rad,
	// 20.59 px (0.05 px).
	const double Beta = RbCameraMath::EyeBlurAngle(0.004, 1.5, 0.25);
	TestEqual(TEXT("T8 beta"), Beta, 0.013333, 1e-6);
	const double H = RbCameraMath::HorizontalFromVerticalFovDeg(50.0, Aspect169);
	const double P = RbCameraMath::PixelsPerRadian(2560.0, H);
	TestEqual(TEXT("T8 pixels per radian"), P, 1544.04, 0.01);
	TestEqual(TEXT("T8 blur [px]"), Beta * P, 20.59, 0.05);
	TestEqual(TEXT("in focus: no blur"), RbCameraMath::EyeBlurAngle(0.004, 1.5, 1.5), 0.0, 1e-15);
	TestEqual(TEXT("symmetric in dioptres"), RbCameraMath::EyeBlurAngle(0.004, 1.0, 0.5), RbCameraMath::EyeBlurAngle(0.004, 0.5, 1.0), 1e-15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraMathCineLens, "RawBreak.Unit.CameraMath.PupilToCineLens", RB_UNIT_TEST_FLAGS)
bool FRbCameraMathCineLens::RunTest(const FString& Parameters)
{
	using namespace RbCameraMathTests;
	// Plan 4.5: w = 36 mm, H = 79.32 deg -> f = 21.71 mm, N = 5.43 for A = 4 mm.
	double F = 0.0;
	double N = 0.0;
	RbCameraMath::PupilToCineLens(36.0, 79.32, 4.0, F, N);
	TestEqual(TEXT("plan 4.5 focal length"), F, 21.71, 0.01);
	TestEqual(TEXT("plan 4.5 f-stop"), N, 5.43, 0.01);
	// The aperture diameter f / N is the pupil, whatever the sensor.
	RbCameraMath::PupilToCineLens(48.0, RbCameraMath::HorizontalFromVerticalFovDeg(50.0, Aspect219), 4.0, F, N);
	TestEqual(TEXT("aperture diameter = pupil"), F / N, 4.0, 1e-12);
	// A cine camera's angular blur (f / N) |1/s - 1/d| equals the eye's beta (thin lens, T8 geometry).
	TestEqual(TEXT("cine blur = eye blur"), (F / N) * 1.0e-3 * FMath::Abs(1.0 / 1.5 - 1.0 / 0.25), RbCameraMath::EyeBlurAngle(0.004, 1.5, 0.25), 1e-12);
	// Keeping the filmback height and widening it with the aspect keeps f (the vertical FOV is authored).
	double F169 = 0.0;
	double N169 = 0.0;
	RbCameraMath::PupilToCineLens(20.25 * Aspect169, RbCameraMath::HorizontalFromVerticalFovDeg(50.0, Aspect169), 4.0, F169, N169);
	double F219 = 0.0;
	double N219 = 0.0;
	RbCameraMath::PupilToCineLens(20.25 * Aspect219, RbCameraMath::HorizontalFromVerticalFovDeg(50.0, Aspect219), 4.0, F219, N219);
	TestEqual(TEXT("f independent of the aspect"), F219, F169, 1e-9);
	TestEqual(TEXT("N independent of the aspect"), N219, N169, 1e-9);
	return true;
}

#endif
