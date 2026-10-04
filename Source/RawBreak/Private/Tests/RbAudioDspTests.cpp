// Offline tests of the table-audio DSP core RawBreakAudioDsp (Docs/specs/audio.md 14, Docs/ue-architecture.md 18.5):
//   AU-T01..T07   the contact law, shape table, Lamb solver, radiation limit and causality (the checks of click_synth.py --selftest)
//   AU-T08        sub-sample scheduling: impact pairs 0.5 / 0.5104 / 0.1042 ms apart -> onsets exactly (t2 - t1) fs apart, exact
//                 and runtime renderer (0.01 sample)
//   AU-T09        level law of the click (+7.45 dB from 1 to 2 m/s, 24.3 dB / decade) and the prototype's level table
//   AU-T10        runtime renderer vs the exact render (1/6-octave rms error <= 0.05 dB on-axis, <= 0.3 dB side-on)
//   AU-T11        golden vectors of Tools/audio/out/ref/golden_runtime_48k.json (residual <= -100 dB) and the port of the kernel
//                 design, the shape table, the decimation FIR and the cloth IIR
//   clock / voice the per-table shot clock (anchor formula, generations, holds) and the voice renderer (block-size independence,
//                 seeks, rate, a hold fades without a click)
//   presentation  static curve caps, BS.1770 loudness, true peak; noise / ambience / footstep synthesis levels and determinism.
// No audio device, no world. Owner: M2-C.

#include "Tests/RbTestFlags.h"

#include "RbAudio/RbAudioAnalysis.h"
#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbAudioMath.h"
#include "RbAudio/RbBallKernels.h"
#include "RbAudio/RbContactShape.h"
#include "RbAudio/RbImpactSynth.h"
#include "RbAudio/RbNoiseSynth.h"
#include "RbAudio/RbPresentation.h"
#include "RbAudio/RbShotAudioClock.h"
#include "RbAudio/RbVoiceRenderer.h"

#include "RbAudio/RbJsonLite.h"

#include "Misc/Paths.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbAudioDspTests
{
	using namespace RbAudio;

	FString RefPath(const TCHAR* File)
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/audio/out/ref"), File);
	}

	bool LoadJson(const FString& Path, FJsonLite& Out)
	{
		return FJsonLite::ParseFile(Path, Out);
	}

	// Residual energy of Test against Ref [dB] (10 log10 sum (t - r)^2 / sum r^2).
	double ResidualDb(TConstArrayView<double> Ref, TConstArrayView<double> Test)
	{
		double E = 0.0;
		double R = 0.0;
		const int32 N = FMath::Min(Ref.Num(), Test.Num());
		for (int32 I = 0; I < N; ++I)
		{
			E += (Test[I] - Ref[I]) * (Test[I] - Ref[I]);
			R += Ref[I] * Ref[I];
		}
		for (int32 I = N; I < Ref.Num(); ++I)
		{
			E += Ref[I] * Ref[I];
			R += Ref[I] * Ref[I];
		}
		for (int32 I = N; I < Test.Num(); ++I)
		{
			E += Test[I] * Test[I];
		}
		if (E == 0.0)
		{
			return -300.0; // identical (also both all zero: e.g. an order without modes below 40 kHz)
		}
		return 10.0 * std::log10(E / FMath::Max(R, 1e-300));
	}

	double PeakDbSpl(TConstArrayView<double> P)
	{
		double Peak = 0.0;
		for (double V : P)
		{
			Peak = FMath::Max(Peak, FMath::Abs(V));
		}
		return PaToDbSpl(FMath::Max(Peak, 1e-12));
	}

	FBallAcoustics StdBall()
	{
		FBallAcoustics B;
		B.Radius = StdBallRadius;
		B.Mass = StdBallMass;
		B.Material = PhenolicMaterial();
		return B;
	}

	FBallAcoustics BallFromKey(const FString& Key)
	{
		FBallAcoustics B = StdBall();
		if (Key == TEXT("oversized_cb"))
		{
			B.Radius = 0.0301625;
			B.Mass = 0.2211;
		}
		else if (Key == TEXT("bar_ob"))
		{
			B.Radius = 0.02855;
			B.Mass = 0.163;
		}
		return B;
	}

	// Runtime render of events whose contact starts StartSeconds after Out[0] (the golden / selftest form).
	void RenderRuntime(const TArray<FImpactEvent>& Events, double StartSeconds, int32 NumOut, double SampleRate, TArray<double>& Out)
	{
		Out.SetNumZeroed(NumOut);
		FImpactRenderer Renderer(SampleRate);
		for (const FImpactEvent& E : Events)
		{
			Renderer.Render(E, StartSeconds * SampleRate, 0, TArrayView<double>(Out));
		}
	}

	const double Shooter[3] = {-1.00, 0.03, 0.30};
	const double Observer[3] = {0.00, 1.20, 0.85};


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT01, "RawBreak.Unit.Audio.Dsp.AU_T01_TsujiAlpha", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT01::RunTest(const FString& Parameters)
{
	const double Alpha = AlphaForRestitution(0.95);
	TestTrue(FString::Printf(TEXT("alpha_T(0.95) = %.6f (want 0.036893 +- 5e-4; core table 0.036915)"), Alpha), FMath::Abs(Alpha - 0.036893) <= 5e-4);
	TestTrue(TEXT("alpha_T(1) = 0 (undamped)"), AlphaForRestitution(1.0) == 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT02, "RawBreak.Unit.Audio.Dsp.AU_T02_ContactTimeUndamped", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT02::RunTest(const FString& Parameters)
{
	const double Speeds[] = {0.5, 1.0, 5.0, 10.0};
	const double WantUs[] = {378.0, 329.0, 238.0, 207.0};
	for (int32 I = 0; I < 4; ++I)
	{
		const FHertzIntegration H = IntegrateHertz(Speeds[I], StdBallMass / 2.0, CoreHertzK, 0.0);
		TestTrue(FString::Printf(TEXT("undamped contact time at %.1f m/s = %.2f us (want %.0f +- 1.5, physics 3.9.3)"), Speeds[I], H.Duration * 1e6, WantUs[I]),
			FMath::Abs(H.Duration * 1e6 - WantUs[I]) <= 1.5);
	}
	const FHertzIntegration Damped = IntegrateHertz(1.0, StdBallMass / 2.0, CoreHertzK, AlphaForRestitution(0.95));
	TestTrue(FString::Printf(TEXT("peak force at 1 m/s, damped = %.1f N (want 940 +- 25)"), Damped.PeakForce), FMath::Abs(Damped.PeakForce - 940.0) <= 25.0);
	const FHertzIntegration At3 = IntegrateHertz(3.0, StdBallMass / 2.0, CoreHertzK, AlphaForRestitution(0.95));
	TestTrue(FString::Printf(TEXT("restitution achieved at 3 m/s = %.5f (want 0.95 +- 2e-3)"), At3.ExitSpeed / 3.0), FMath::Abs(At3.ExitSpeed / 3.0 - 0.95) <= 2e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT03, "RawBreak.Unit.Audio.Dsp.AU_T03_ShapeTableVsIntegration", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT03::RunTest(const FString& Parameters)
{
	const FContactShapePtr Shape = GetContactShape(0.95);
	for (const double V : {0.3, 3.0, 12.0})
	{
		const FContactPulse P = HertzPulse(*Shape, V, StdBallMass / 2.0, CoreHertzK);
		const FHertzIntegration H = IntegrateHertz(V, StdBallMass / 2.0, CoreHertzK, Shape->Alpha);
		// Impulse of the shape-table pulse on the 768 kHz grid (click_synth.py runtime_pulse).
		const int32 N = FMath::Max(4, static_cast<int32>(std::ceil(P.Duration * HertzGridRate)));
		double Sum = 0.0;
		for (int32 J = 0; J <= N; ++J)
		{
			Sum += P.PeakForce * Shape->Eval(J / HertzGridRate / P.Duration);
		}
		const double Impulse = Sum / HertzGridRate;
		TestTrue(FString::Printf(TEXT("%.1f m/s: F_max ratio %.6f (want 1 +- 2e-3)"), V, P.PeakForce / H.PeakForce), FMath::Abs(P.PeakForce / H.PeakForce - 1.0) <= 2e-3);
		TestTrue(FString::Printf(TEXT("%.1f m/s: impulse ratio %.6f (want 1 +- 5e-3)"), V, Impulse / H.Impulse), FMath::Abs(Impulse / H.Impulse - 1.0) <= 5e-3);
		TestTrue(FString::Printf(TEXT("%.1f m/s: J = (1 + e) m* v within 0.5 %% of the integration (%.6g vs %.6g)"), V, P.Impulse, H.Impulse),
			FMath::Abs(P.Impulse / H.Impulse - 1.0) <= 5e-3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT04, "RawBreak.Unit.Audio.Dsp.AU_T04_UniversalConstants", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT04::RunTest(const FString& Parameters)
{
	const FContactShape Undamped = ComputeContactShape(1.0);
	TestTrue(FString::Printf(TEXT("undamped tau = %.5f (want 3.2181 +- 2e-3)"), Undamped.Tau), FMath::Abs(Undamped.Tau - 3.2181) <= 2e-3);
	TestTrue(FString::Printf(TEXT("undamped phi = %.5f (want 1.14326 +- 1e-3)"), Undamped.Phi), FMath::Abs(Undamped.Phi - 1.14326) <= 1e-3);
	const FContactShapePtr Shape = GetContactShape(0.95);
	TestTrue(FString::Printf(TEXT("e 0.95 tau = %.5f (want 3.23516)"), Shape->Tau), FMath::Abs(Shape->Tau - 3.23516) <= 1e-4);
	TestTrue(FString::Printf(TEXT("e 0.95 phi = %.5f (want 1.10912)"), Shape->Phi), FMath::Abs(Shape->Phi - 1.10912) <= 1e-4);
	// The runtime shape table is the one of hertz_shape_e095.json.
	FJsonLite Json;
	if (TestTrue(TEXT("hertz_shape_e095.json readable"), LoadJson(RefPath(TEXT("hertz_shape_e095.json")), Json)))
	{
		const TArray<double> Ref = Json[TEXT("shape")].AsNumbers();
		TestEqual(TEXT("129 shape points"), Shape->Shape.Num(), Ref.Num());
		double MaxErr = 0.0;
		for (int32 I = 0; I < FMath::Min(Ref.Num(), Shape->Shape.Num()); ++I)
		{
			MaxErr = FMath::Max(MaxErr, FMath::Abs(Ref[I] - Shape->Shape[I]));
		}
		TestTrue(FString::Printf(TEXT("shape table == prototype (max error %.3g)"), MaxErr), MaxErr <= 1e-9);
		TestTrue(TEXT("tau == prototype"), FMath::Abs(Json[TEXT("tau")].AsNumber() - Shape->Tau) <= 1e-9);
		TestTrue(TEXT("phi == prototype"), FMath::Abs(Json[TEXT("phi")].AsNumber() - Shape->Phi) <= 1e-9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT05, "RawBreak.Unit.Audio.Dsp.AU_T05_LambSolver", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT05::RunTest(const FString& Parameters)
{
	FBallMaterial M025;
	M025.YoungModulus = 1e10;
	M025.Poisson = 0.25;
	M025.Loss = 0.0;
	const double R = 0.03;
	const double Rho = 0.2 / (4.0 / 3.0 * Pi * R * R * R);
	const TArray<double> Roots = LambRoots(2, M025, R, Rho, 14.0, 1);
	if (TestTrue(TEXT("a root of order 2"), Roots.Num() == 1))
	{
		TestTrue(FString::Printf(TEXT("Lamb n = 2 fundamental, nu 0.25: k_T a = %.4f (want 2.640 +- 0.01)"), Roots[0]), FMath::Abs(Roots[0] - 2.640) <= 0.01);
	}
	// audio.md 3.4 table: the n = 2 fundamentals of the phenolic balls.
	auto N2Hz = [](const FBallAcoustics& B)
	{
		for (const FSphereMode& M : SphereModes(B))
		{
			if (M.Order == 2 && M.Root == 1)
			{
				return M.FrequencyHz;
			}
		}
		return 0.0;
	};
	const double StdHz = N2Hz(StdBall());
	const double OverHz = N2Hz(BallFromKey(TEXT("oversized_cb")));
	TestTrue(FString::Printf(TEXT("standard phenolic n = 2 at %.0f Hz (want 20.3 kHz)"), StdHz), FMath::Abs(StdHz - 20300.0) <= 100.0);
	TestTrue(FString::Printf(TEXT("oversized cue ball n = 2 at %.0f Hz (want 18.3 kHz)"), OverHz), FMath::Abs(OverHz - 18300.0) <= 100.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT06, "RawBreak.Unit.Audio.Dsp.AU_T06_RadiationIncompressible", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT06::RunTest(const FString& Parameters)
{
	const FComplex H = RadiationAccel(1, 1.0, 0.1, StdBallRadius);
	const double Want = Rho0 * StdBallRadius * StdBallRadius * StdBallRadius / (2.0 * 0.01);
	TestTrue(FString::Printf(TEXT("order-1 radiation at 1 Hz, r 0.1 m: %.6g / rho0 a^3 / (2 r^2) = %.5f (want 1 +- 0.01)"), H.real(), H.real() / Want),
		FMath::Abs(H.real() / Want - 1.0) <= 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT07, "RawBreak.Unit.Audio.Dsp.AU_T07_Causality", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT07::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	const double L[3] = {-1.0, 0.0, 0.3};
	const FExactImpact Imp = MakeBallBallExactImpact(2.0, StdBall(), StdBall());
	TArray<double> P;
	RenderExactImpact(Imp, L, 0.004, 16384, Fs, P);
	const double Dx = L[0] - Imp.Bodies[0].Center[0];
	const double Dz = L[2] - Imp.Bodies[0].Center[2];
	const double R = std::sqrt(Dx * Dx + L[1] * L[1] + Dz * Dz);
	const double TArr = 0.004 + (R - StdBallRadius) / C0;
	const int32 IArr = static_cast<int32>((TArr - 0.0002) * Fs);
	double Early = 0.0, Total = 0.0;
	for (int32 I = 0; I < P.Num(); ++I)
	{
		Total += P[I] * P[I];
		Early += I < IArr ? P[I] * P[I] : 0.0;
	}
	TestTrue(FString::Printf(TEXT("exact render: energy earlier than (r - a)/c - 0.2 ms = %.3g of the total (want < 1e-4)"), Early / Total), Early / Total < 1e-4);
	// The runtime renderer: its band-limit pre-ringing (<= 48 samples) stays in the same bound.
	TArray<FImpactEvent> Events;
	MakeBallBallRuntimeEvents(2.0, StdBall(), StdBall(), L, Fs, Events);
	TArray<double> Q;
	RenderRuntime(Events, 0.004, 16384, Fs, Q);
	Early = Total = 0.0;
	for (int32 I = 0; I < Q.Num(); ++I)
	{
		Total += Q[I] * Q[I];
		Early += I < IArr ? Q[I] * Q[I] : 0.0;
	}
	TestTrue(FString::Printf(TEXT("runtime render: early energy %.3g of the total (want < 1e-4)"), Early / Total), Early / Total < 1e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT08, "RawBreak.Unit.Audio.Dsp.AU_T08_SubSampleScheduling", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT08::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	const double L[3] = {-1.0, 0.0, 0.3};
	const FExactImpact Imp = MakeBallBallExactImpact(2.0, StdBall(), StdBall());
	TArray<FImpactEvent> Events;
	MakeBallBallRuntimeEvents(2.0, StdBall(), StdBall(), L, Fs, Events);
	const double Pairs[3][2] = {{0.010, 0.0105}, {0.0100073, 0.0105177}, {0.0100104, 0.0101146}};
	for (const auto& Pair : Pairs)
	{
		const double Want = (Pair[1] - Pair[0]) * Fs;
		TArray<double> A, B;
		RenderExactImpact(Imp, L, Pair[0], 16384, Fs, A);
		RenderExactImpact(Imp, L, Pair[1], 16384, Fs, B);
		const double Exact = GroupDelaySamples(A, B, Fs);
		TestTrue(FString::Printf(TEXT("exact render: onsets %.4f / %.4f ms -> %.4f samples (want %.4f +- 0.01)"), Pair[0] * 1e3, Pair[1] * 1e3, Exact, Want),
			FMath::Abs(Exact - Want) <= 0.01);
		RenderRuntime(Events, Pair[0], 16384, Fs, A);
		RenderRuntime(Events, Pair[1], 16384, Fs, B);
		const double Runtime = GroupDelaySamples(A, B, Fs);
		TestTrue(FString::Printf(TEXT("runtime render: onsets %.4f / %.4f ms -> %.4f samples (want %.4f +- 0.01)"), Pair[0] * 1e3, Pair[1] * 1e3, Runtime, Want),
			FMath::Abs(Runtime - Want) <= 0.01);
	}
	// The engine case of AU-T08 in two voices of one table clock: 0.5104 ms -> 24.4992 samples, rendered through two voice renderers.
	TSharedRef<FShotAudioClock> Clock = MakeShared<FShotAudioClock>();
	Clock->StartShotAnchored(7, 4096, 0.0, 1.0, Fs);
	FVoiceRenderer V1, V2;
	V1.Initialize(Fs, 512);
	V2.Initialize(Fs, 512);
	auto PlanWith = [&](double T)
	{
		TSharedPtr<FVoicePlan, ESPMode::ThreadSafe> Plan = MakeShared<FVoicePlan, ESPMode::ThreadSafe>();
		Plan->ShotId = 7;
		FImpactEvent E = Events[0];
		E.ShotTime = T;
		Plan->Impacts.Add(E);
		Plan->OutputGain = 1.0;
		return Plan;
	};
	V1.SetPlan(PlanWith(0.0100));
	V2.SetPlan(PlanWith(0.0100 + 0.5104e-3));
	TArray<double> O1, O2;
	TArray<float> Block;
	Block.SetNumZeroed(512);
	for (int64 F = 0; F < 16384; F += 512)
	{
		V1.RenderBlock(&Clock.Get(), F, Block);
		for (float S : Block)
		{
			O1.Add(S);
		}
		V2.RenderBlock(&Clock.Get(), F, Block);
		for (float S : Block)
		{
			O2.Add(S);
		}
	}
	const double Voices = GroupDelaySamples(O1, O2, Fs);
	TestTrue(FString::Printf(TEXT("two voices of one clock, 0.5104 ms apart -> %.4f samples (want 24.4992 +- 0.01)"), Voices), FMath::Abs(Voices - 24.4992) <= 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT09, "RawBreak.Unit.Audio.Dsp.AU_T09_LevelLaw", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT09::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	const double Speeds[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 12.0};
	const double TableDb[] = {90.0, 98.1, 105.7, 113.2, 120.3, 127.1, 131.0}; // audio.md 3.7, peak dB SPL at the shooter
	double Peaks[7];
	double Sx = 0.0, Sy = 0.0, Sxx = 0.0, Sxy = 0.0;
	for (int32 I = 0; I < 7; ++I)
	{
		TArray<double> P;
		RenderExactImpact(MakeBallBallExactImpact(Speeds[I], StdBall(), StdBall()), Shooter, 0.002, 16384, Fs, P);
		Peaks[I] = PeakDbSpl(P);
		TestTrue(FString::Printf(TEXT("%.2f m/s: peak %.2f dB SPL at the shooter (prototype table %.1f +- 0.2)"), Speeds[I], Peaks[I], TableDb[I]),
			FMath::Abs(Peaks[I] - TableDb[I]) <= 0.2);
		const double X = std::log10(Speeds[I]);
		Sx += X;
		Sy += Peaks[I];
		Sxx += X * X;
		Sxy += X * Peaks[I];
	}
	const double Slope = (7.0 * Sxy - Sx * Sy) / (7.0 * Sxx - Sx * Sx);
	TestTrue(FString::Printf(TEXT("2 vs 1 m/s: %+.3f dB (want +7.45 +- 0.1)"), Peaks[3] - Peaks[2]), FMath::Abs(Peaks[3] - Peaks[2] - 7.45) <= 0.1);
	TestTrue(FString::Printf(TEXT("level law %.2f dB / decade over 0.25-12 m/s (want 24.3 +- 0.3)"), Slope), FMath::Abs(Slope - 24.3) <= 0.3);
	// The runtime renderer follows the same law (what the game plays).
	TArray<FImpactEvent> E1, E2;
	MakeBallBallRuntimeEvents(1.0, StdBall(), StdBall(), Shooter, Fs, E1);
	MakeBallBallRuntimeEvents(2.0, StdBall(), StdBall(), Shooter, Fs, E2);
	TArray<double> R1, R2;
	RenderRuntime(E1, 0.002, 16384, Fs, R1);
	RenderRuntime(E2, 0.002, 16384, Fs, R2);
	TestTrue(FString::Printf(TEXT("runtime renderer 2 vs 1 m/s: %+.3f dB (want +7.45 +- 0.15)"), PeakDbSpl(R2) - PeakDbSpl(R1)),
		FMath::Abs(PeakDbSpl(R2) - PeakDbSpl(R1) - 7.45) <= 0.15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT10, "RawBreak.Unit.Audio.Dsp.AU_T10_RuntimeVsExact", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT10::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	for (const double V : {0.25, 1.0, 4.0, 12.0})
	{
		for (int32 L = 0; L < 2; ++L)
		{
			const double* Listener = L == 0 ? Shooter : Observer;
			TArray<double> Ex, Rt;
			RenderExactImpact(MakeBallBallExactImpact(V, StdBall(), StdBall()), Listener, 0.002, 16384, Fs, Ex);
			TArray<FImpactEvent> Events;
			MakeBallBallRuntimeEvents(V, StdBall(), StdBall(), Listener, Fs, Events);
			RenderRuntime(Events, 0.002, 16384, Fs, Rt);
			// click_synth.py compares the first 0.25 s (+ 2 ms); both renders here are 16384 samples.
			double Max = 0.0, Rms = 0.0;
			BandErrorDb(TArrayView<double>(Ex.GetData(), 12096), TArrayView<double>(Rt.GetData(), 12096), Fs, Max, Rms);
			const double Tol = L == 0 ? 0.05 : 0.3;
			TestTrue(FString::Printf(TEXT("%.2f m/s %s: 1/6-oct rms error %.3f dB, max %.2f dB (want rms <= %.2f)"), V, L == 0 ? TEXT("shooter (on-axis)")
				: TEXT("observer (side-on)"), Rms, Max, Tol), Rms <= Tol);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspT11, "RawBreak.Unit.Audio.Dsp.AU_T11_GoldenVectors", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspT11::RunTest(const FString& Parameters)
{
	FJsonLite Golden;
	if (!TestTrue(TEXT("golden_runtime_48k.json readable"), LoadJson(RefPath(TEXT("golden_runtime_48k.json")), Golden)))
	{
		return false;
	}
	const double Fs = Golden[TEXT("fs")].AsNumber();
	TestTrue(TEXT("c0 == port"), FMath::Abs(Golden[TEXT("c0")].AsNumber() - C0) < 1e-12);
	TestTrue(TEXT("rho0 == port"), FMath::Abs(Golden[TEXT("rho0")].AsNumber() - Rho0) < 1e-12);
	TestEqual(TEXT("latency 48 samples"), static_cast<int32>(Golden[TEXT("latency_samples")].AsNumber()), RuntimeLatency);
	TestTrue(TEXT("near-field leak == port"), FMath::Abs(Golden[TEXT("near_field_leak")].AsNumber() - std::exp(-2.0 * Pi * NearFieldLeakHz / Fs)) < 1e-15);
	// Decimation FIR (scipy.signal.firwin, Kaiser 8).
	const TArray<double> Taps = Golden[TEXT("decimation")][TEXT("taps")].AsNumbers();
	const TArray<double>& Fir = DecimationFir(Fs);
	TestEqual(TEXT("129 taps"), Fir.Num(), Taps.Num());
	TestTrue(FString::Printf(TEXT("decimation FIR residual %.1f dB (want <= -150)"), ResidualDb(Taps, Fir)), ResidualDb(Taps, Fir) <= -150.0);
	// Cloth IIR.
	const TArray<double> B = Golden[TEXT("cloth_iir")][TEXT("b")].AsNumbers();
	const TArray<double> A = Golden[TEXT("cloth_iir")][TEXT("a")].AsNumbers();
	const FReflection R = FReflection::Cloth(Fs);
	TestTrue(TEXT("cloth IIR b0, b1, a1 == scipy.signal.bilinear"), B.Num() == 2 && A.Num() == 2 && FMath::Abs(B[0] - R.B0) < 1e-14
		&& FMath::Abs(B[1] - R.B1) < 1e-14 && FMath::Abs(A[1] - R.A1) < 1e-14);

	// Kernel design against the three kernel files.
	for (const TCHAR* Key : {TEXT("std"), TEXT("oversized_cb"), TEXT("bar_ob")})
	{
		FJsonLite KJson;
		if (!TestTrue(FString::Printf(TEXT("ball_kernels_%s_48k.json readable"), Key),
			LoadJson(RefPath(*FString::Printf(TEXT("ball_kernels_%s_48k.json"), Key)), KJson)))
		{
			continue;
		}
		FBallAcoustics Ball = BallFromKey(Key);
		Ball.Radius = KJson[TEXT("radius_m")].AsNumber();
		Ball.Mass = KJson[TEXT("mass_kg")].AsNumber();
		const FBallKernelsPtr K = GetBallKernels(Ball, Fs, false);
		for (int32 N = 0; N <= 3; ++N)
		{
			const TArray<double> Ref = KJson[TEXT("orders")][FString::FromInt(N)].AsNumbers();
			const double Res = ResidualDb(Ref, K->Orders[N]);
			TestTrue(FString::Printf(TEXT("kernel %s order %d: residual %.1f dB (want <= -100)"), Key, N, Res), Res <= -100.0);
		}
	}

	// The three golden cases of runtime_render.
	const FJsonLite& Cases = Golden[TEXT("cases")];
	TestEqual(TEXT("three golden cases"), Cases.Num(), 3);
	for (const FJsonLite& Case : Cases.Items)
	{
		const FString Name = Case[TEXT("name")].String;
		const double TStart = Case[TEXT("t_start_s")].AsNumber();
		const int32 NOut = static_cast<int32>(Case[TEXT("n_out")].AsNumber());
		const double Vn = Case[TEXT("v_n")].AsNumber();
		const double MStar = Case[TEXT("m_star")].AsNumber();
		const double K = Case[TEXT("K")].AsNumber();
		const double E = Case[TEXT("e")].AsNumber();
		const TArray<double> ListenerArr = Case[TEXT("listener_m")].AsNumbers();
		const double L[3] = {ListenerArr[0], ListenerArr[1], ListenerArr[2]};
		const FContactShapePtr Shape = GetContactShape(E);
		const FContactPulse Pulse = HertzPulse(*Shape, Vn, MStar, K);
		const double WantT = Case[TEXT("contact_time_s")].AsNumber();
		const double WantF = Case[TEXT("fmax_n")].AsNumber();
		TestTrue(FString::Printf(TEXT("%s: contact time %.12g == %.12g"), *Name, Pulse.Duration, WantT), FMath::Abs(Pulse.Duration / WantT - 1.0) < 1e-12);
		TestTrue(FString::Printf(TEXT("%s: F_max %.12g == %.12g"), *Name, Pulse.PeakForce, WantF), FMath::Abs(Pulse.PeakForce / WantF - 1.0) < 1e-12);
		// Step 1 on its own: the force with the fraction 0.25.
		TArray<double> G;
		RuntimeForceHertz(*Shape, Pulse.Duration, Pulse.PeakForce, 0.25, Fs, G);
		const double ResG = ResidualDb(Case[TEXT("force_frac_0.25")].AsNumbers(), G);
		TestTrue(FString::Printf(TEXT("%s: force (frac 0.25) residual %.1f dB (want <= -100)"), *Name, ResG), ResG <= -100.0);
		// The whole render.
		TArray<FImpactEvent> Events;
		for (const FJsonLite& Body : Case[TEXT("bodies")].Items)
		{
			FBallAcoustics Ball = BallFromKey(Body[TEXT("ball")].String);
			Ball.Radius = Body[TEXT("R")].AsNumber();
			Ball.Mass = Body[TEXT("m")].AsNumber();
			const TArray<double> C = Body[TEXT("center_m")].AsNumbers();
			const TArray<double> Ax = Body[TEXT("axis")].AsNumbers();
			FImpactEvent Ev;
			Ev.Pulse = EPulseShape::Hertz;
			Ev.Shape = Shape;
			Ev.ContactTime = Pulse.Duration;
			Ev.PeakForce = Pulse.PeakForce;
			Ev.Kernels = GetBallKernels(Ball, Fs, false);
			Ev.Reflection = FReflection::Cloth(Fs);
			for (int32 P = 0; P < 2; ++P)
			{
				const double Cz = P == 0 ? C[2] : -C[2];
				const double Az = P == 0 ? Ax[2] : -Ax[2];
				const double D[3] = {L[0] - C[0], L[1] - C[1], L[2] - Cz};
				const double Rr = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
				const double Cs = (D[0] * Ax[0] + D[1] * Ax[1] + D[2] * Az) / Rr;
				FRadiationPath& Path = Ev.Paths[P];
				for (int32 N = 0; N <= 3; ++N)
				{
					Path.Weights[N] = Legendre(N, Cs);
				}
				Path.NearField = C0 / (Rr * Fs);
				Path.Gain = 1.0 / Rr;
				Path.DelaySeconds = (Rr - Ball.Radius) / C0;
				Path.bReflected = P == 1;
			}
			Ev.NumPaths = C[2] > 0.0 ? 2 : 1;
			Events.Add(Ev);
		}
		TArray<double> Out;
		RenderRuntime(Events, TStart, NOut, Fs, Out);
		const double Res = ResidualDb(Case[TEXT("p_pa")].AsNumbers(), Out);
		TestTrue(FString::Printf(TEXT("%s: runtime render residual %.1f dB (want <= -100, AU-T11)"), *Name, Res), Res <= -100.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspClock, "RawBreak.Unit.Audio.Dsp.ShotClock_Anchor", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspClock::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	FShotAudioClock Clock;
	double Frame = 0.0;
	TestFalse(TEXT("stopped clock maps nothing"), Clock.ShotTimeToDeviceFrame(0.0, Frame));
	// A live shot: the contact 20 ms in the past -> the lead is negative -> the anchor sits LeadMin after the block (5.1 item 5).
	Clock.StartShot(1, 100.0, 0.0, 1.0, false, 0.033);
	TestFalse(TEXT("not anchored before the first callback"), Clock.ShotTimeToDeviceFrame(0.0, Frame));
	Clock.TryAnchor(48000, 100.0 + 0.020 + 0.033 + 0.5, 0.031, 576, Fs);
	const FShotClockSnapshot S = Clock.Snapshot();
	TestTrue(TEXT("anchored"), S.bAnchored);
	TestEqual(TEXT("late plan: anchor = block + LeadMin"), S.AnchorFrame, static_cast<int64>(48000 + 576));
	// A second callback (another voice) never re-anchors the same generation.
	Clock.TryAnchor(48512, 100.6, 0.031, 576, Fs);
	TestEqual(TEXT("one anchor per generation"), Clock.Snapshot().AnchorFrame, static_cast<int64>(48000 + 576));
	// Ahead of time: F0 = block + round((origin + visual - (now + output)) fs).
	Clock.StartShot(2, 200.0, 0.0, 1.0, false, 0.040);
	Clock.TryAnchor(96000, 199.9, 0.031, 576, Fs);
	const int64 Expect = 96000 + std::llround((200.0 + 0.040 - (199.9 + 0.031)) * Fs);
	TestEqual(TEXT("anchor from the platform clock"), Clock.Snapshot().AnchorFrame, Expect);
	TestTrue(TEXT("shot time 0.5 s -> anchor + 24000"), Clock.ShotTimeToDeviceFrame(0.5, Frame) && FMath::IsNearlyEqual(Frame, Expect + 24000.0, 1e-6));
	// Slow motion x 0.25 from shot time 1.0: spacing x 4.
	Clock.SetMapping(210.0, 1.0, 0.25, false);
	Clock.TryAnchor(200000, 209.0, 0.031, 576, Fs);
	const FShotClockSnapshot S2 = Clock.Snapshot();
	double F1 = 0.0, F2 = 0.0;
	TestTrue(TEXT("re-anchored after SetMapping"), S2.bAnchored && S2.Generation != S.Generation);
	TestTrue(TEXT("rate 0.25: 10 ms of shot time = 40 ms of audio"), Clock.ShotTimeToDeviceFrame(1.01, F2) && Clock.ShotTimeToDeviceFrame(1.0, F1)
		&& FMath::IsNearlyEqual(F2 - F1, 0.04 * Fs, 1e-6));
	// Held: nothing maps, the hold time is reported.
	Clock.SetMapping(211.0, 1.2, 0.25, true);
	TestFalse(TEXT("held clock maps nothing"), Clock.ShotTimeToDeviceFrame(1.3, Frame));
	TestTrue(TEXT("held at 1.2"), FMath::IsNearlyEqual(Clock.Snapshot().DeviceFrameToShotTime(123.0), 1.2));
	Clock.Stop();
	TestFalse(TEXT("stopped"), Clock.Snapshot().bRunning);
	// Offline anchoring (tests, trailer stems).
	Clock.StartShotAnchored(9, 1000, 0.0, 1.0, Fs);
	TestTrue(TEXT("offline anchor"), Clock.ShotTimeToDeviceFrame(0.001, Frame) && FMath::IsNearlyEqual(Frame, 1048.0, 1e-9));
	return true;
}

	// A small plan: a few clicks, a cushion with its bank, rolling, a gully run, and the presentation.
	TSharedPtr<FVoicePlan, ESPMode::ThreadSafe> MakeTestPlan(double Fs, uint64 ShotId)
	{
		TSharedPtr<FVoicePlan, ESPMode::ThreadSafe> Plan = MakeShared<FVoicePlan, ESPMode::ThreadSafe>();
		Plan->ShotId = ShotId;
		Plan->Seed = 1234;
		Plan->OutputGain = 1.0 / 10.0;
		TArray<FImpactEvent> Events;
		MakeBallBallRuntimeEvents(3.0, StdBall(), StdBall(), Shooter, Fs, Events);
		for (int32 I = 0; I < 6; ++I)
		{
			FImpactEvent E = Events[I % 2];
			E.ShotTime = 0.0137 * I + 0.0000213 * I * I;
			if (I == 3)
			{
				E.Bank = EModalBank::RailBarBox;
				E.BankGain = 1.0;
				E.BankDelaySeconds = 0.003;
			}
			Plan->Impacts.Add(E);
		}
		FContinuousSegment Roll;
		Roll.StartTime = 0.0;
		Roll.EndTime = 0.3;
		Roll.Speed0 = 2.0;
		Roll.SpeedSlope = -5.0;
		Roll.Gain = RollingRmsPerMps * 4.0;
		Plan->Continuous.Add(Roll);
		FContinuousSegment Gully;
		Gully.StartTime = 0.05;
		Gully.EndTime = 0.25;
		Gully.Kind = ENoiseKind::GullyRun;
		Gully.Gain = GullyRms * 4.0;
		Plan->Continuous.Add(Gully);
		return Plan;
	}

	void RenderPlan(const TSharedPtr<FVoicePlan, ESPMode::ThreadSafe>& Plan, FShotAudioClock& Clock, int32 BlockFrames, int64 FirstFrame, int32 Frames,
		double Fs, TArray<float>& Out, TFunctionRef<void(int64)> BeforeBlock)
	{
		FVoiceRenderer R;
		R.Initialize(Fs, BlockFrames);
		R.SetPlan(Plan);
		Out.Reset();
		TArray<float> Block;
		for (int64 F = FirstFrame; F < FirstFrame + Frames; F += BlockFrames)
		{
			BeforeBlock(F);
			Block.SetNumZeroed(BlockFrames);
			R.RenderBlock(&Clock, F, Block);
			Out.Append(Block);
		}
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspVoiceBlocks, "RawBreak.Unit.Audio.Dsp.VoiceRenderer_BlockSizeIndependent", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspVoiceBlocks::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	const TSharedPtr<FVoicePlan, ESPMode::ThreadSafe> Plan = MakeTestPlan(Fs, 5);
	TArray<float> Ref;
	for (const int32 Block : {512, 256, 1024, 480, 64})
	{
		FShotAudioClock Clock;
		Clock.StartShotAnchored(5, 10000, 0.0, 1.0, Fs);
		TArray<float> Out;
		RenderPlan(Plan, Clock, Block, 7680, 30720, Fs, Out, [](int64) {});
		Out.SetNum(30720);
		if (Ref.Num() == 0)
		{
			Ref = Out;
			double Peak = 0.0;
			for (float S : Out)
			{
				Peak = FMath::Max(Peak, static_cast<double>(FMath::Abs(S)));
			}
			TestTrue(TEXT("the plan sounds"), Peak > 1e-4);
			continue;
		}
		bool bEqual = true;
		for (int32 I = 0; I < Ref.Num() && bEqual; ++I)
		{
			bEqual = Ref[I] == Out[I];
		}
		TestTrue(FString::Printf(TEXT("%d-frame blocks render bit-identically to 512 (AU-T13)"), Block), bEqual);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspVoiceHold, "RawBreak.Unit.Audio.Dsp.VoiceRenderer_HoldRateSeek", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspVoiceHold::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	const TSharedPtr<FVoicePlan, ESPMode::ThreadSafe> Plan = MakeTestPlan(Fs, 6);
	// Hold at shot time ~0.1 s for 0.2 s, then resume (a pause or a world pause): the continuous layers fade out and in without a
	// step (no click), and nothing new sounds while held.
	TSharedPtr<FVoicePlan, ESPMode::ThreadSafe> Cont = MakeShared<FVoicePlan, ESPMode::ThreadSafe>(*Plan);
	Cont->Impacts.Reset();
	Cont->Continuous.SetNum(1);
	Cont->Continuous[0].StartTime = 0.0;
	Cont->Continuous[0].EndTime = 5.0;
	Cont->Continuous[0].Speed0 = 1.5;
	Cont->Continuous[0].SpeedSlope = 0.0;
	FShotAudioClock Clock;
	Clock.StartShotAnchored(6, 0, 0.0, 1.0, Fs);
	TArray<float> Held;
	const int64 HoldAt = 5120;                       // a block start, shot time 0.10667 s
	const int64 ResumeAt = HoldAt + 9728;            // 0.2 s later (block aligned)
	RenderPlan(Cont, Clock, 512, 0, 48000, Fs, Held, [&](int64 F)
	{
		if (F == HoldAt)
		{
			Clock.SetMapping(0.0, static_cast<double>(HoldAt) / Fs, 1.0, true);
		}
		if (F == ResumeAt)
		{
			Clock.StartShotAnchored(6, F, static_cast<double>(HoldAt) / Fs, 1.0, Fs);
		}
	});
	auto MaxStep = [&Held](int64 From, int64 To)
	{
		double M = 0.0;
		for (int64 I = FMath::Max<int64>(1, From); I < FMath::Min<int64>(To, Held.Num()); ++I)
		{
			M = FMath::Max(M, static_cast<double>(FMath::Abs(Held[I] - Held[I - 1])));
		}
		return M;
	};
	const double Steady = MaxStep(HoldAt - 4096, HoldAt - 256);
	const double AtHold = MaxStep(HoldAt - 256, HoldAt + 2048);
	const double AtResume = MaxStep(ResumeAt - 256, ResumeAt + 2048);
	TestTrue(FString::Printf(TEXT("hold: largest step %.3g <= 1.2 x the running noise's %.3g (no click)"), AtHold, Steady), AtHold <= 1.2 * Steady);
	TestTrue(FString::Printf(TEXT("resume: largest step %.3g <= 1.2 x the running noise's %.3g (no click)"), AtResume, Steady), AtResume <= 1.2 * Steady);
	auto Peak = [&Held](int64 From, int64 To)
	{
		double M = 0.0;
		for (int64 I = From; I < FMath::Min<int64>(To, Held.Num()); ++I)
		{
			M = FMath::Max(M, static_cast<double>(FMath::Abs(Held[I])));
		}
		return M;
	};
	const double Running = Peak(HoldAt - 4096, HoldAt);
	const double QuietPeak = Peak(HoldAt + 5760, ResumeAt);
	TestTrue(FString::Printf(TEXT("held: the table is silent (%.3g, running %.3g: < 1 %%)"), QuietPeak, Running), QuietPeak < 0.01 * Running);
	const double After = Peak(ResumeAt + 2400, ResumeAt + 6496);
	TestTrue(FString::Printf(TEXT("resumed: the layer sounds again at its level (%.3g vs %.3g)"), After, Running), After > 0.5 * Running && After < 2.0 * Running);

	// Rate 0.5 (film style): impacts twice as far apart, each at natural pitch (identical waveform).
	FShotAudioClock Slow;
	Slow.StartShotAnchored(6, 0, 0.0, 0.5, Fs);
	TSharedPtr<FVoicePlan, ESPMode::ThreadSafe> Two = MakeShared<FVoicePlan, ESPMode::ThreadSafe>(*Plan);
	Two->Continuous.Reset();
	Two->Impacts.SetNum(2);
	Two->Impacts[0].Bank = EModalBank::None;
	Two->Impacts[1] = Two->Impacts[0]; // the same ball and path (only the time differs)
	Two->Impacts[0].ShotTime = 0.02;
	Two->Impacts[1].ShotTime = 0.07;
	TArray<float> SlowOut;
	RenderPlan(Two, Slow, 512, 0, 16384, Fs, SlowOut, [](int64) {});
	TArray<double> A, B;
	for (int32 I = 0; I < 16384; ++I)
	{
		const double T = I / Fs;
		A.Add(T < 0.1 ? SlowOut[I] : 0.0);
		B.Add(T >= 0.1 ? SlowOut[I] : 0.0);
	}
	const double Sep = GroupDelaySamples(A, B, Fs);
	TestTrue(FString::Printf(TEXT("rate 0.5: 50 ms of shot time -> %.3f samples (want 4800 +- 0.05)"), Sep), FMath::Abs(Sep - 4800.0) <= 0.05);

	// A backward seek renders again from the new origin; a forward seek skips the impacts it jumps over.
	FShotAudioClock Seek;
	Seek.StartShotAnchored(6, 0, 0.0, 1.0, Fs);
	FVoiceRenderer R;
	R.Initialize(Fs, 512);
	R.SetPlan(Plan);
	TArray<float> Block;
	Block.SetNumZeroed(512);
	for (int64 F = 0; F < 9600; F += 512)
	{
		R.RenderBlock(&Seek, F, Block);
	}
	const int32 Rendered = R.GetStats().RenderedImpacts;
	Seek.StartShotAnchored(6, 9728, 0.0, 1.0, Fs); // seek back to 0 (same plan)
	for (int64 F = 9728; F < 20000; F += 512)
	{
		R.RenderBlock(&Seek, F, Block);
	}
	TestTrue(FString::Printf(TEXT("backward seek renders the impacts again (%d -> %d)"), Rendered, R.GetStats().RenderedImpacts),
		R.GetStats().RenderedImpacts > Rendered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspPresentation, "RawBreak.Unit.Audio.Dsp.Presentation_Meters", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspPresentation::RunTest(const FString& Parameters)
{
	// The static curve keeps the loudest summed break peak (127 dB SPL) below -1 dBFS in every mode (click_synth.py selftest).
	for (const EDynamicRangeMode Mode : {EDynamicRangeMode::Wide, EDynamicRangeMode::Normal, EDynamicRangeMode::Night})
	{
		const FPresentationMode M = GetPresentationMode(Mode);
		const double Dbfs = 127.0 + M.GainDb(127.0) - M.FullScaleSpl;
		TestTrue(FString::Printf(TEXT("%s: 127 dB SPL break peak -> %+.2f dBFS (<= -1)"), ToString(Mode), Dbfs), Dbfs <= -1.0 + 1e-9);
	}
	// BS.1770: a 997 Hz sine at -20 dBFS peak in one channel reads -23.01 LUFS.
	TArray<float> Sine;
	for (int32 I = 0; I < 5 * 48000; ++I)
	{
		Sine.Add(static_cast<float>(0.1 * std::sin(2.0 * Pi * 997.0 * I / 48000.0)));
	}
	const double Lufs = LoudnessLufs(Sine, 1, 48000.0);
	TestTrue(FString::Printf(TEXT("BS.1770 loudness %.3f LUFS (want -23.01 +- 0.05)"), Lufs), FMath::Abs(Lufs + 23.01) <= 0.05);
	const double Tp = TruePeakDbtp(Sine, 1);
	TestTrue(FString::Printf(TEXT("true peak of the sine %.3f dBTP (want -20 +- 0.05)"), Tp), FMath::Abs(Tp + 20.0) <= 0.05);
	// The envelope: a click cluster far above the knee is reduced, a quiet click is untouched, the gain is smooth.
	TArray<double> Stem;
	Stem.SetNumZeroed(48000);
	Stem[4800] = 20.0;  // 120 dB SPL
	Stem[24000] = 0.02; // 60 dB SPL
	const FPresentationGainPtr Env = ComputePresentationEnvelope(Stem, 48000.0, 0.0, GetPresentationMode(EDynamicRangeMode::Wide));
	// The 1 ms smoothing of the dB gain against the 60 ms release of the peak envelope: the deepest gain sits just after the peak.
	double GMinDb = 0.0;
	for (int32 I = -30; I <= 100; ++I)
	{
		GMinDb = FMath::Min(GMinDb, GainToDb(Env->Evaluate(0.1 + I * 1e-4)));
	}
	const double GQuiet = Env->Evaluate(0.5);
	TestTrue(FString::Printf(TEXT("Wide: a 120 dB peak is presented with %.2f dB (static curve -10.0 = -(120 - 105) (1 - 1/3); 1 ms smoothing vs 60 ms release)"),
		GMinDb), GMinDb >= -10.05 && GMinDb <= -9.5);
	TestTrue(FString::Printf(TEXT("the gain at the peak itself is already %.2f dB (>= 90 %% of the reduction)"), GainToDb(Env->Evaluate(0.1))),
		GainToDb(Env->Evaluate(0.1)) <= -9.0);
	TestTrue(TEXT("a quiet click keeps gain 1"), FMath::Abs(GQuiet - 1.0) < 1e-6);
	TestTrue(TEXT("the gain is already down 3 ms before the peak (lookahead)"), Env->Evaluate(0.1 - 0.0025) < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioDspNoise, "RawBreak.Unit.Audio.Dsp.NoiseAmbienceFootsteps", RB_UNIT_TEST_FLAGS)
bool FRbAudioDspNoise::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	// Shaped noise: unit RMS for every kind, deterministic per seed.
	for (const ENoiseKind Kind : {ENoiseKind::RollingCloth, ENoiseKind::SlidingCloth, ENoiseKind::GullyRun, ENoiseKind::RollingFloor})
	{
		FShapedNoise A, B;
		A.Initialize(Kind, Fs, 99);
		B.Initialize(Kind, Fs, 99);
		double Sum = 0.0;
		bool bSame = true;
		for (int32 I = 0; I < 96000; ++I)
		{
			const double X = A.Next();
			bSame &= X == B.Next();
			Sum += I > 4800 ? X * X : 0.0;
		}
		const double Rms = std::sqrt(Sum / (96000 - 4801));
		TestTrue(FString::Printf(TEXT("noise kind %d: rms %.3f (want 1 +- 0.1)"), static_cast<int32>(Kind), Rms), FMath::Abs(Rms - 1.0) <= 0.1);
		TestTrue(TEXT("same seed, same noise"), bSame);
	}
	// Ambience layers hit their A-weighted level at 1 m (calibrated) and are deterministic.
	for (const EAmbienceLayer Layer : {EAmbienceLayer::HvacBed, EAmbienceLayer::HvacDiffuser, EAmbienceLayer::Compressor, EAmbienceLayer::NeonHum})
	{
		FAmbienceLayerDesc Desc;
		Desc.Layer = Layer;
		Desc.LevelDbA = 42.0;
		Desc.Seed = 77;
		Desc.bStartOn = true;
		Desc.CycleOnSeconds[0] = Desc.CycleOnSeconds[1] = 1000.0;
		FAmbienceSynth Synth;
		Synth.Initialize(Desc, Fs);
		const int32 Ch = Desc.NumChannels();
		TArray<float> Out;
		Out.SetNumZeroed(4 * 48000 * Ch);
		Synth.Render(Out.GetData(), 4 * 48000);
		double Level = 0.0;
		for (int32 C = 0; C < Ch; ++C)
		{
			TArray<double> Mono;
			for (int32 I = 24000; I < 4 * 48000; ++I)
			{
				Mono.Add(Out[I * Ch + C]);
			}
			Level += std::pow(10.0, LaeqDb(Mono, Fs) / 10.0);
		}
		Level = 10.0 * std::log10(Level / Ch);
		TestTrue(FString::Printf(TEXT("%s: LAeq %.2f dB(A) at 1 m (want 42 +- 1)"), ToString(Layer), Level), FMath::Abs(Level - 42.0) <= 1.0);
		FAmbienceSynth Again;
		Again.Initialize(Desc, Fs, Synth.GetCalibration());
		TArray<float> Out2;
		Out2.SetNumZeroed(4800 * Ch);
		Again.Render(Out2.GetData(), 4800);
		bool bSame = true;
		for (int32 I = 0; I < 4800 * Ch; ++I)
		{
			bSame &= Out2[I] == Out[I];
		}
		TestTrue(FString::Printf(TEXT("%s: deterministic"), ToString(Layer)), bSame);
	}
	// Compressor duty cycle: off -> on with a start clunk.
	{
		FAmbienceLayerDesc Desc;
		Desc.Layer = EAmbienceLayer::Compressor;
		Desc.LevelDbA = 50.0;
		Desc.Seed = 3;
		Desc.bStartOn = false;
		Desc.CycleOffSeconds[0] = Desc.CycleOffSeconds[1] = 1.0;
		Desc.CycleOnSeconds[0] = Desc.CycleOnSeconds[1] = 1.0;
		FAmbienceSynth Synth;
		Synth.Initialize(Desc, Fs);
		TArray<float> Out;
		Out.SetNumZeroed(3 * 48000);
		const bool bStart = Synth.IsCompressorOn();
		int32 Toggles = 0;
		bool bLast = bStart;
		for (int32 B = 0; B < 3 * 48000 / 480; ++B)
		{
			Synth.Render(Out.GetData() + B * 480, 480);
			Toggles += Synth.IsCompressorOn() != bLast ? 1 : 0;
			bLast = Synth.IsCompressorOn();
		}
		TestFalse(TEXT("the compressor starts off"), bStart);
		TestTrue(FString::Printf(TEXT("duty cycle toggles within 3 s (%d)"), Toggles), Toggles >= 1);
	}
	// Footsteps: the heel peak at 1 m follows the surface's nominal level, seeds vary, equal seeds repeat.
	for (const EFloorSurface Surface : {EFloorSurface::Vct, EFloorSurface::Concrete, EFloorSurface::Rubber, EFloorSurface::Wood})
	{
		FFootstepParams P;
		P.Surface = Surface;
		P.SpeedMps = 1.4;
		P.bOwnSteps = false;
		P.Seed = 11;
		TArray<float> A, B, C;
		FFootstepSynth::Render(P, Fs, A);
		FFootstepSynth::Render(P, Fs, B);
		P.Seed = 12;
		FFootstepSynth::Render(P, Fs, C);
		double Peak = 0.0;
		bool bSame = A.Num() == B.Num();
		bool bDiffer = false;
		for (int32 I = 0; I < A.Num(); ++I)
		{
			Peak = FMath::Max(Peak, static_cast<double>(FMath::Abs(A[I])));
			bSame &= A[I] == B[I];
			bDiffer |= I < C.Num() && A[I] != C[I];
		}
		const double Nominal = FFootstepSynth::NominalHeelPeakPa(Surface);
		TestTrue(FString::Printf(TEXT("%s step peak %.4f Pa at 1 m (nominal %.4f +- 15 %%)"), ToString(Surface), Peak, Nominal),
			Peak >= 0.84 * Nominal && Peak <= 1.16 * Nominal);
		TestTrue(TEXT("equal seeds repeat"), bSame);
		TestTrue(TEXT("different seeds differ (never two identical steps)"), bDiffer);
	}
	return true;
}

} // namespace RbAudioDspTests

#endif // WITH_DEV_AUTOMATION_TESTS
