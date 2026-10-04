#include "RbAudio/RbPresentation.h"

#include <cmath>
#include <limits>

// Owner: M2-C.

namespace RbAudio
{
	FPresentationMode GetPresentationMode(EDynamicRangeMode Mode)
	{
		FPresentationMode M;
		switch (Mode)
		{
		case EDynamicRangeMode::Normal:
			M.FullScaleSpl = 106.0;
			M.KneeSpl = 97.0;
			M.Ratio = 4.0;
			M.AmbienceOffsetDb = -2.0;
			M.VoiceAddressedDb = 4.0;
			break;
		case EDynamicRangeMode::Night:
			M.FullScaleSpl = 100.0;
			M.KneeSpl = 95.0;
			M.Ratio = 10.0;
			M.AmbienceOffsetDb = -4.0;
			M.VoiceAddressedDb = 6.0;
			break;
		case EDynamicRangeMode::Wide:
		default:
			break;
		}
		return M;
	}

	const TCHAR* ToString(EDynamicRangeMode Mode)
	{
		switch (Mode)
		{
		case EDynamicRangeMode::Normal: return TEXT("Normal");
		case EDynamicRangeMode::Night: return TEXT("Night");
		default: return TEXT("Wide");
		}
	}

	void PresentationGainPerSample(TConstArrayView<double> PeakPa, double SampleRate, const FPresentationMode& Mode, TArray<double>& OutGain)
	{
		const int32 N = PeakPa.Num();
		OutGain.SetNumUninitialized(N);
		if (N == 0)
		{
			return;
		}
		int32 La = static_cast<int32>(FMath::RoundToDouble(PresentationLookaheadSeconds * SampleRate));
		La += La % 2;
		// Sliding maximum over [k, k + La] (zero beyond the end) with a monotone deque.
		TArray<double> Env;
		Env.SetNumUninitialized(N);
		TArray<int32> Deque;
		Deque.Reserve(La + 2);
		int32 Head = 0;
		int32 Next = 0;
		for (int32 K = 0; K < N; ++K)
		{
			const int32 WindowEnd = FMath::Min(N - 1, K + La);
			while (Next <= WindowEnd)
			{
				const double V = FMath::Abs(PeakPa[Next]);
				while (Deque.Num() > Head && FMath::Abs(PeakPa[Deque.Last()]) <= V)
				{
					Deque.Pop(EAllowShrinking::No);
				}
				Deque.Add(Next);
				++Next;
			}
			while (Deque[Head] < K)
			{
				++Head;
			}
			Env[K] = FMath::Abs(PeakPa[Deque[Head]]);
			if (Head > 4096)
			{
				Deque.RemoveAt(0, Head, EAllowShrinking::No);
				Head = 0;
			}
		}
		const double Rel = std::exp(-1.0 / (PresentationReleaseSeconds * SampleRate));
		const double Sm = 1.0 - std::exp(-1.0 / (PresentationSmoothSeconds * SampleRate));
		// The knee as a pressure: below it (the envelope's peak-hold decays, it never needs the log) the static curve is 0 dB, and a
		// smoothed gain that has settled back to 0 dB (|g| < 1e-9 dB) is exactly unity. Saves a log10 and a pow per sample for the
		// quiet majority of a shot (the plan must be ready in milliseconds, audio.md 8.3).
		const double KneePa = PRef * FMath::Pow(10.0, Mode.KneeSpl / 20.0);
		double Acc = 0.0;
		double Smoothed = 0.0;
		for (int32 K = 0; K < N; ++K)
		{
			Acc = FMath::Max(Env[K], Acc * Rel);
			double GainDb = 0.0;
			if (Acc > KneePa)
			{
				const double Over = 20.0 * std::log10(Acc / PRef) - Mode.KneeSpl;
				GainDb = Over > 0.0 ? -Over * (1.0 - 1.0 / Mode.Ratio) : 0.0;
			}
			Smoothed = Sm * GainDb + (1.0 - Sm) * Smoothed;
			if (GainDb == 0.0 && Smoothed > -1e-9)
			{
				Smoothed = 0.0;
				OutGain[K] = 1.0;
			}
			else
			{
				OutGain[K] = std::pow(10.0, Smoothed / 20.0);
			}
		}
	}

	FPresentationGainPtr ComputePresentationEnvelope(TConstArrayView<double> PeakPa, double SampleRate, double StartShotTime, const FPresentationMode& Mode,
		double Step)
	{
		TArray<double> PerSample;
		PresentationGainPerSample(PeakPa, SampleRate, Mode, PerSample);
		TSharedPtr<FPresentationGain, ESPMode::ThreadSafe> Env = MakeShared<FPresentationGain, ESPMode::ThreadSafe>();
		Env->StartTime = StartShotTime;
		Env->Step = Step;
		const int32 PerCell = FMath::Max(1, static_cast<int32>(FMath::RoundToDouble(Step * SampleRate)));
		const int32 Cells = (PerSample.Num() + PerCell - 1) / PerCell;
		Env->Gains.SetNumUninitialized(Cells + 1);
		for (int32 C = 0; C < Cells; ++C)
		{
			double Min = 1.0;
			const int32 End = FMath::Min(PerSample.Num(), (C + 1) * PerCell);
			for (int32 K = C * PerCell; K < End; ++K)
			{
				Min = FMath::Min(Min, PerSample[K]);
			}
			Env->Gains[C] = static_cast<float>(Min);
		}
		Env->Gains[Cells] = 1.0f;
		return Env;
	}

	FAWeighting::FAWeighting(double SampleRate)
	{
		const double W1 = 2.0 * Pi * 20.598997;
		const double W2 = 2.0 * Pi * 107.65265;
		const double W3 = 2.0 * Pi * 737.86223;
		const double W4 = 2.0 * Pi * 12194.217;
		Sections[0] = FBiquad::FromAnalog(1.0, 0.0, 0.0, 1.0, 2.0 * W1, W1 * W1, SampleRate, 0.0);
		Sections[1] = FBiquad::FromAnalog(1.0, 0.0, 0.0, 1.0, W2 + W3, W2 * W3, SampleRate, 0.0);
		Sections[2] = FBiquad::FromAnalog(0.0, 0.0, W4 * W4, 1.0, 2.0 * W4, W4 * W4, SampleRate, 0.0);
		// Normalise to exactly 0 dB at 1 kHz.
		const FComplex Z = std::polar(1.0, 2.0 * Pi * 1000.0 / SampleRate);
		FComplex H(1.0, 0.0);
		for (const FBiquad& Q : Sections)
		{
			const FComplex Zi = 1.0 / Z;
			H *= (Q.B0 + Q.B1 * Zi + Q.B2 * Zi * Zi) / (1.0 + Q.A1 * Zi + Q.A2 * Zi * Zi);
		}
		Gain = 1.0 / std::abs(H);
	}

	double LaeqDb(TConstArrayView<double> Pa, double SampleRate)
	{
		if (Pa.Num() == 0)
		{
			return -300.0;
		}
		FAWeighting A(SampleRate);
		double Sum = 0.0;
		for (double X : Pa)
		{
			const double Y = A.Process(X);
			Sum += Y * Y;
		}
		return 10.0 * std::log10(FMath::Max(Sum / Pa.Num(), 1e-300) / (PRef * PRef));
	}

	double LoudnessLufs(TConstArrayView<float> Interleaved, int32 NumChannels, double SampleRate)
	{
		if (NumChannels <= 0)
		{
			return -std::numeric_limits<double>::infinity();
		}
		const int32 Frames = Interleaved.Num() / NumChannels;
		FBiquad Pre;
		FBiquad Rlb;
		if (FMath::IsNearlyEqual(SampleRate, 48000.0))
		{
			Pre.B0 = 1.53512485958697; Pre.B1 = -2.69169618940638; Pre.B2 = 1.19839281085285;
			Pre.A1 = -1.69065929318241; Pre.A2 = 0.73248077421585;
			Rlb.B0 = 1.0; Rlb.B1 = -2.0; Rlb.B2 = 1.0;
			Rlb.A1 = -1.99004745483398; Rlb.A2 = 0.99007225036621;
		}
		else
		{
			double F0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
			double K = std::tan(Pi * F0 / SampleRate);
			const double Vh = std::pow(10.0, G / 20.0);
			const double Vb = std::pow(Vh, 0.4996667741545416);
			double A0 = 1.0 + K / Q + K * K;
			Pre.B0 = (Vh + Vb * K / Q + K * K) / A0; Pre.B1 = 2.0 * (K * K - Vh) / A0; Pre.B2 = (Vh - Vb * K / Q + K * K) / A0;
			Pre.A1 = 2.0 * (K * K - 1.0) / A0; Pre.A2 = (1.0 - K / Q + K * K) / A0;
			F0 = 38.13547087602444; Q = 0.5003270373238773;
			K = std::tan(Pi * F0 / SampleRate);
			A0 = 1.0 + K / Q + K * K;
			Rlb.B0 = 1.0; Rlb.B1 = -2.0; Rlb.B2 = 1.0;
			Rlb.A1 = 2.0 * (K * K - 1.0) / A0; Rlb.A2 = (1.0 - K / Q + K * K) / A0;
		}
		TArray<double> Sq;
		Sq.SetNumZeroed(Frames);
		for (int32 Ch = 0; Ch < NumChannels; ++Ch)
		{
			FBiquad P = Pre;
			FBiquad R = Rlb;
			for (int32 I = 0; I < Frames; ++I)
			{
				const double Y = R.Process(P.Process(Interleaved[I * NumChannels + Ch]));
				Sq[I] += Y * Y;
			}
		}
		const int32 Block = static_cast<int32>(0.4 * SampleRate);
		const int32 Hop = static_cast<int32>(0.1 * SampleRate);
		if (Frames < Block)
		{
			return -std::numeric_limits<double>::infinity();
		}
		TArray<double> Z;
		// Each block summed exactly (no running-sum drift): 400 ms blocks, 100 ms hop.
		for (int32 Start = 0; Start + Block <= Frames; Start += Hop)
		{
			double Sum = 0.0;
			for (int32 I = Start; I < Start + Block; ++I)
			{
				Sum += Sq[I];
			}
			Z.Add(Sum / Block);
		}
		TArray<double> Gated;
		for (double V : Z)
		{
			if (-0.691 + 10.0 * std::log10(V + 1e-300) > -70.0)
			{
				Gated.Add(V);
			}
		}
		if (Gated.Num() == 0)
		{
			return -std::numeric_limits<double>::infinity();
		}
		double Mean = 0.0;
		for (double V : Gated)
		{
			Mean += V;
		}
		Mean /= Gated.Num();
		const double Relative = -0.691 + 10.0 * std::log10(Mean) - 10.0;
		double Sum2 = 0.0;
		int32 Count2 = 0;
		for (double V : Z)
		{
			const double L = -0.691 + 10.0 * std::log10(V + 1e-300);
			if (L > -70.0 && L > Relative)
			{
				Sum2 += V;
				++Count2;
			}
		}
		return Count2 > 0 ? -0.691 + 10.0 * std::log10(Sum2 / Count2) : -std::numeric_limits<double>::infinity();
	}

	double TruePeakDbtp(TConstArrayView<float> Interleaved, int32 NumChannels)
	{
		if (NumChannels <= 0 || Interleaved.Num() < NumChannels)
		{
			return -300.0;
		}
		constexpr int32 Up = 4;
		constexpr int32 Taps = 193;
		// Thread-safe one-time design (a function-local static's initialiser runs once; the meters may run on several threads).
		static const TArray<double> Fir = []()
		{
			TArray<double> F;
			KaiserLowPass(Taps, 0.5 * 48000.0 * 0.98, 48000.0 * Up, 8.0, F);
			for (double& H : F)
			{
				H *= Up;
			}
			return F;
		}();
		const int32 Frames = Interleaved.Num() / NumChannels;
		double Peak = 0.0;
		const int32 Half = Taps / 2;
		for (int32 Ch = 0; Ch < NumChannels; ++Ch)
		{
			for (int32 I = 0; I < Frames; ++I)
			{
				Peak = FMath::Max(Peak, static_cast<double>(FMath::Abs(Interleaved[I * NumChannels + Ch])));
			}
			// Upsampled sample at fine index u = 4 i + p: sum_k h[u - 4 k + Half] x[k].
			for (int32 I = 0; I < Frames; ++I)
			{
				for (int32 P = 1; P < Up; ++P)
				{
					const int32 U = Up * I + P;
					double Sum = 0.0;
					const int32 KLo = FMath::Max(0, (U + Half - (Taps - 1) + Up - 1) / Up);
					const int32 KHi = FMath::Min(Frames - 1, (U + Half) / Up);
					for (int32 K = KLo; K <= KHi; ++K)
					{
						Sum += Fir[U - Up * K + Half] * Interleaved[K * NumChannels + Ch];
					}
					Peak = FMath::Max(Peak, FMath::Abs(Sum));
				}
			}
		}
		return 20.0 * std::log10(FMath::Max(Peak, 1e-15));
	}

	double SamplePeakDbfs(TConstArrayView<float> Interleaved)
	{
		double Peak = 0.0;
		for (float V : Interleaved)
		{
			Peak = FMath::Max(Peak, static_cast<double>(FMath::Abs(V)));
		}
		return 20.0 * std::log10(FMath::Max(Peak, 1e-15));
	}
}
