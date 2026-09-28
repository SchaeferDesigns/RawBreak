// Stroke-input mathematics: ue5-realism-plan T9-T12 plus the gain inverse, the cue integrator and the scripted stroke
// generator the component tests rely on. Owner: UE-5a.

#include "Math/RbStrokeMath.h"
#include "Tests/RbTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// x(t) = 0.5 t^2 sampled at 1 kHz on [T0, T1] (times as k / 1000, the nearest doubles).
	TArray<FRbStrokeSample> ParabolaSamples(int32 K0, int32 K1)
	{
		TArray<FRbStrokeSample> Samples;
		for (int32 k = K0; k <= K1; ++k)
		{
			FRbStrokeSample S;
			S.Time = k / 1000.0;
			S.Position = 0.5 * S.Time * S.Time;
			Samples.Add(S);
		}
		return Samples;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeMathT9, "RawBreak.Unit.StrokeMath.T9_VelocityEstimator", RB_UNIT_TEST_FLAGS)
bool FRbStrokeMathT9::RunTest(const FString& Parameters)
{
	// T9: x(t) = 0.5 t^2 sampled at 1 kHz, window [0.48, 0.50] s -> quadratic fit v(0.50) = 0.5000; linear-fit slope 0.4900.
	const TArray<FRbStrokeSample> Samples = ParabolaSamples(0, 600);
	double V = 0.0;
	TestTrue(TEXT("quadratic fit succeeds"), RbStrokeMath::QuadraticFitVelocity(Samples.GetData(), Samples.Num(), 0.5, 0.02, V));
	TestNearlyEqual(TEXT("T9 quadratic v(0.50)"), V, 0.5, 1e-6);
	double VLin = 0.0;
	TestTrue(TEXT("linear fit succeeds"), RbStrokeMath::LinearFitVelocity(Samples.GetData(), Samples.Num(), 0.5, 0.02, VLin));
	TestNearlyEqual(TEXT("T9 linear slope (half-window lag)"), VLin, 0.49, 1e-6);

	// Acceleration of the general fit, evaluation after the window (extrapolation) and QPC-sized times.
	double A = 0.0;
	TestTrue(TEXT("general fit"), RbStrokeMath::QuadraticFit(Samples.GetData(), Samples.Num(), 0.505, 0.48, 0.50, V, A));
	TestNearlyEqual(TEXT("v at 0.505 (extrapolated)"), V, 0.505, 1e-6);
	TestNearlyEqual(TEXT("a = 1"), A, 1.0, 1e-6);
	TArray<FRbStrokeSample> Shifted = Samples;
	const double Offset = 123456.789; // ~34 h of uptime: centring on T must avoid the cancellation of t^2
	for (FRbStrokeSample& S : Shifted)
	{
		const double Local = S.Time;
		S.Time = Offset + Local;
		S.Position = 0.5 * Local * Local;
	}
	TestTrue(TEXT("fit with large times"), RbStrokeMath::QuadraticFitVelocity(Shifted.GetData(), Shifted.Num(), Offset + 0.5, 0.02, V));
	TestNearlyEqual(TEXT("v(0.50) with large times"), V, 0.5, 1e-6);

	// Fewer than 3 samples in the window: no fit.
	const TArray<FRbStrokeSample> Two = ParabolaSamples(499, 500);
	TestFalse(TEXT("2 samples -> false"), RbStrokeMath::QuadraticFitVelocity(Two.GetData(), Two.Num(), 0.5, 0.02, V));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeMathT10, "RawBreak.Unit.StrokeMath.T10_GainCurve", RB_UNIT_TEST_FLAGS)
bool FRbStrokeMathT10::RunTest(const FString& Parameters)
{
	// T10: v_m = 0.3 / 1.25 / 2.0 / 3.0 m/s, plan 5.4 defaults -> v_tip = 0.600 / 4.375 / 10.000 / 12.000 (clamped).
	const FRbStrokeGain Gain;
	TestNearlyEqual(TEXT("0.3 m/s"), RbStrokeMath::CueSpeedFromHandSpeed(0.3, Gain), 0.6, 1e-6);
	TestNearlyEqual(TEXT("1.25 m/s"), RbStrokeMath::CueSpeedFromHandSpeed(1.25, Gain), 4.375, 1e-6);
	TestNearlyEqual(TEXT("2.0 m/s"), RbStrokeMath::CueSpeedFromHandSpeed(2.0, Gain), 10.0, 1e-6);
	TestNearlyEqual(TEXT("3.0 m/s (clamped)"), RbStrokeMath::CueSpeedFromHandSpeed(3.0, Gain), 12.0, 1e-6);
	TestNearlyEqual(TEXT("odd: -1.25 m/s"), RbStrokeMath::CueSpeedFromHandSpeed(-1.25, Gain), -4.375, 1e-6);
	TestNearlyEqual(TEXT("G at the knee"), RbStrokeMath::Gain(0.5, Gain), 2.0, 1e-12);
	TestNearlyEqual(TEXT("G at saturation"), RbStrokeMath::Gain(2.0, Gain), 5.0, 1e-12);
	TestNearlyEqual(TEXT("counts to metres: 800 counts at 800 dpi = 1 inch"), RbStrokeMath::CountsToMeters(800.0, 800.0), 0.0254, 1e-15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeMathT11, "RawBreak.Unit.StrokeMath.T11_Steering", RB_UNIT_TEST_FLAGS)
bool FRbStrokeMathT11::RunTest(const FString& Parameters)
{
	// T11: y_g = 10 mm, L_bg = 0.8 m, L_bt = 0.2 m -> d_psi = 0.7162 deg, tip shift = -2.500 mm.
	double Yaw = 0.0;
	double Shift = 0.0;
	RbStrokeMath::Steering(0.010, 0.8, 0.2, Yaw, Shift);
	TestNearlyEqual(TEXT("d_psi [deg]"), FMath::RadiansToDegrees(Yaw), 0.7162, 1e-3);
	TestNearlyEqual(TEXT("tip shift [mm]"), Shift * 1000.0, -2.5, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeMathT12, "RawBreak.Unit.StrokeMath.T12_BridgeHeight", RB_UNIT_TEST_FLAGS)
bool FRbStrokeMathT12::RunTest(const FString& Parameters)
{
	// T12: theta = 5 deg, L_b = 0.20 m, centre hit -> z_bridge = 48.497 mm.
	const double Z = RbStrokeMath::BridgeHeight(0.028575, FMath::DegreesToRadians(5.0), 0.20);
	TestNearlyEqual(TEXT("z_bridge [mm]"), Z * 1000.0, 48.497, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeMathGainInverse, "RawBreak.Unit.StrokeMath.GainInverseAndTravel", RB_UNIT_TEST_FLAGS)
bool FRbStrokeMathGainInverse::RunTest(const FString& Parameters)
{
	const FRbStrokeGain Gain;
	for (const double V : {0.05, 0.5, 0.999, 1.0, 1.5, 4.375, 7.0, 9.99, 10.0, 11.0, 12.0})
	{
		const double Hand = RbStrokeMath::HandSpeedForCueSpeed(V, Gain);
		TestNearlyEqual(*FString::Printf(TEXT("inverse round trip %.3f"), V), RbStrokeMath::CueSpeedFromHandSpeed(Hand, Gain), V, 1e-12);
		TestNearlyEqual(*FString::Printf(TEXT("inverse is odd %.3f"), V), RbStrokeMath::HandSpeedForCueSpeed(-V, Gain), -Hand, 1e-15);
	}
	// Derivative vs a central difference (inside each branch), 0 under the clamp.
	for (const double Hand : {0.2, 0.9, 1.7, 2.2})
	{
		const double H = 1e-6;
		const double Numeric = (RbStrokeMath::CueSpeedFromHandSpeed(Hand + H, Gain) - RbStrokeMath::CueSpeedFromHandSpeed(Hand - H, Gain)) / (2.0 * H);
		TestNearlyEqual(*FString::Printf(TEXT("dv_c/dv_m at %.2f"), Hand), RbStrokeMath::CueSpeedDerivative(Hand, Gain), Numeric, 1e-5);
	}
	TestEqual(TEXT("dv_c/dv_m under the clamp"), RbStrokeMath::CueSpeedDerivative(3.0, Gain), 0.0);
	// Travel integral vs trapezoid integration of the clamped curve.
	for (const double Hand : {0.3, 1.2, 2.0, 2.7})
	{
		const int32 N = 200000;
		double Sum = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			const double U0 = Hand * i / N;
			const double U1 = Hand * (i + 1) / N;
			Sum += 0.5 * (RbStrokeMath::CueSpeedFromHandSpeed(U0, Gain) + RbStrokeMath::CueSpeedFromHandSpeed(U1, Gain)) * (U1 - U0);
		}
		TestNearlyEqual(*FString::Printf(TEXT("travel integral at %.2f"), Hand), RbStrokeMath::CueTravelIntegral(Hand, Gain), Sum, 1e-8);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeMathIntegrator, "RawBreak.Unit.StrokeMath.IntegratorAndCrossing", RB_UNIT_TEST_FLAGS)
bool FRbStrokeMathIntegrator::RunTest(const FString& Parameters)
{
	const FRbStrokeGain Gain;
	// Slow uniform hand motion (G0 region): the cue moves exactly G0 times the hand.
	FRbCueIntegrator Integrator;
	Integrator.Reset(-0.05);
	TArray<FRbStrokeSample> Cue;
	for (int32 k = 0; k <= 100; ++k)
	{
		FRbStrokeSample S;
		S.Time = 10.0 + k * 0.001;
		S.Position = 0.2 * k * 0.001; // 0.2 m/s
		const FRbCueStepResult R = Integrator.Step(S, Gain, true, 0.004, 0.3);
		TestEqual(TEXT("first sample is the reference"), R.bReference, k == 0);
		FRbStrokeSample C;
		C.Time = S.Time;
		C.Position = Integrator.GetX();
		Cue.Add(C);
		if (R.bCrossed)
		{
			// x_c = -0.05 + 0.4 (t - 10) -> crossing at t = 10.125 s: not reached in 0.1 s.
			AddError(TEXT("unexpected crossing"));
		}
	}
	TestNearlyEqual(TEXT("x_c after 0.1 s at 0.2 m/s hand (G0 = 2)"), Integrator.GetX(), -0.05 + 0.04, 1e-12);

	// Crossing inside a step: linear interpolation, identical to FindContactCrossing.
	FRbCueIntegrator Live;
	Live.Reset(-0.001);
	Live.Step({1.0, 0.0, 0.0}, Gain, true, 0.004, 0.3);
	const FRbCueStepResult Cross = Live.Step({1.001, 0.001, 0.0}, Gain, true, 0.004, 0.3); // 1 m/s hand -> 3 m/s cue, dx = 3 mm
	TestTrue(TEXT("crossed"), Cross.bCrossed);
	TestNearlyEqual(TEXT("crossing time"), Cross.CrossingTime, 1.0 + 0.001 / 3.0, 1e-12);
	const FRbStrokeSample Path[2] = {{1.0, -0.001, 0.0}, {1.001, 0.002, 0.0}};
	double T = 0.0;
	TestTrue(TEXT("FindContactCrossing"), RbStrokeMath::FindContactCrossing(Path, 2, T));
	TestNearlyEqual(TEXT("same interpolation"), T, Cross.CrossingTime, 1e-12);

	// Practice stroke: stops at -StopShort and never crosses; the back limit holds too.
	FRbCueIntegrator Practice;
	Practice.Reset(-0.001);
	Practice.Step({1.0, 0.0, 0.0}, Gain, false, 0.004, 0.3);
	const FRbCueStepResult Stop = Practice.Step({1.001, 0.002, 0.0}, Gain, false, 0.004, 0.3);
	TestFalse(TEXT("practice never crosses"), Stop.bCrossed);
	TestEqual(TEXT("already ahead of the stop: stays"), Practice.GetX(), -0.001);
	FRbCueIntegrator Back;
	Back.Reset(-0.02);
	Back.Step({1.0, 0.0, 0.0}, Gain, false, 0.004, 0.3);
	Back.Step({1.1, 0.01, 0.0}, Gain, false, 0.004, 0.3);
	TestEqual(TEXT("front limit = -StopShort"), Back.GetX(), -0.004);
	Back.Step({1.5, -0.5, 0.0}, Gain, false, 0.004, 0.3);
	TestEqual(TEXT("back limit = -MaxBackswing"), Back.GetX(), -0.3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeMathScripted, "RawBreak.Unit.StrokeMath.ScriptedStroke", RB_UNIT_TEST_FLAGS)
bool FRbStrokeMathScripted::RunTest(const FString& Parameters)
{
	const FRbStrokeGain Gain;
	for (const double V : {0.3, 1.0, 2.0, 4.0, 8.0, 12.0})
	{
		FRbScriptedStroke P;
		P.TipSpeed = V;
		P.StartTime = 2000.0;
		TArray<FRbStrokeSample> Samples;
		double Tc = 0.0;
		double TFwd = 0.0;
		if (!TestTrue(*FString::Printf(TEXT("scripted stroke %.1f m/s built"), V), RbStrokeMath::MakeScriptedStroke(P, Gain, Samples, &Tc, &TFwd)))
		{
			continue;
		}
		// The component's own path: integrate, find the crossing, fit at the crossing.
		FRbCueIntegrator Integrator;
		Integrator.Reset(P.StartCueDisplacement);
		int32 CrossIndex = -1;
		double Crossing = 0.0;
		for (int32 i = 0; i < Samples.Num(); ++i)
		{
			const FRbCueStepResult R = Integrator.Step(Samples[i], Gain, true, 0.004, P.MaxBackswing);
			if (R.bCrossed)
			{
				CrossIndex = i;
				Crossing = R.CrossingTime;
				break;
			}
		}
		if (!TestTrue(TEXT("crosses"), CrossIndex > 0))
		{
			continue;
		}
		TestEqual(TEXT("reported contact time"), Crossing, Tc);
		double Hand = 0.0;
		double Acc = 0.0;
		TestTrue(TEXT("fit"), RbStrokeMath::QuadraticFit(Samples.GetData(), CrossIndex + 1, Crossing, Crossing - 0.02, Samples[CrossIndex].Time, Hand, Acc));
		TestNearlyEqual(*FString::Printf(TEXT("tip speed at the crossing %.1f"), V), RbStrokeMath::CueSpeedFromHandSpeed(Hand, Gain), V, 1e-9);
		TestTrue(TEXT("forward stroke starts after the pause"), TFwd > P.StartTime + P.BackswingTime && Tc > TFwd);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
