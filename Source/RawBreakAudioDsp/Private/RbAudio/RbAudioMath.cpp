#include "RbAudio/RbAudioMath.h"

#include "Misc/ScopeLock.h"

#include <cmath>

// Owner: M2-C. Ports of the numpy / scipy routines Tools/audio/click_synth.py uses (see the header).

namespace RbAudio
{
	double Legendre(int32 N, double X)
	{
		switch (N)
		{
		case 0: return 1.0;
		case 1: return X;
		case 2: return 0.5 * (3.0 * X * X - 1.0);
		case 3: return 0.5 * (5.0 * X * X * X - 3.0 * X);
		default: break;
		}
		// Bonnet recursion for completeness.
		double P0 = 1.0;
		double P1 = X;
		for (int32 K = 1; K < N; ++K)
		{
			const double P2 = ((2.0 * K + 1.0) * X * P1 - K * P0) / (K + 1.0);
			P0 = P1;
			P1 = P2;
		}
		return P1;
	}

	namespace
	{
		// j_n(x) by its power series: x^n / (2n+1)!! * sum_k (-x^2/2)^k / (k! (2n+3)(2n+5)...(2n+2k+1)).
		double SphJSeries(int32 N, double X)
		{
			double DoubleFactorial = 1.0;
			for (int32 K = 1; K <= 2 * N + 1; K += 2)
			{
				DoubleFactorial *= K;
			}
			const double Lead = std::pow(X, N) / DoubleFactorial;
			const double Z = -0.5 * X * X;
			double Term = 1.0;
			double Sum = 1.0;
			for (int32 K = 1; K < 60; ++K)
			{
				Term *= Z / (K * (2.0 * N + 2.0 * K + 1.0));
				Sum += Term;
				if (std::fabs(Term) < 1e-18 * std::fabs(Sum))
				{
					break;
				}
			}
			return Lead * Sum;
		}
	}

	double SphJ(int32 N, double X)
	{
		if (N < 0)
		{
			return std::nan("");
		}
		if (X == 0.0)
		{
			return N == 0 ? 1.0 : 0.0;
		}
		if (N > 0 && static_cast<double>(N) >= X)
		{
			return SphJSeries(N, X);
		}
		double S0 = std::sin(X) / X;
		if (N == 0)
		{
			return S0;
		}
		double S1 = (S0 - std::cos(X)) / X;
		if (N == 1)
		{
			return S1;
		}
		double Sn = S1;
		for (int32 Idx = 0; Idx < N - 1; ++Idx)
		{
			Sn = (2 * Idx + 3) * S1 / X - S0;
			S0 = S1;
			S1 = Sn;
		}
		return Sn;
	}

	double SphY(int32 N, double X)
	{
		if (N < 0)
		{
			return std::nan("");
		}
		if (X == 0.0)
		{
			return -std::numeric_limits<double>::infinity();
		}
		double S0 = -std::cos(X) / X;
		if (N == 0)
		{
			return S0;
		}
		double S1 = (S0 - std::sin(X)) / X;
		if (N == 1)
		{
			return S1;
		}
		double Sn = S1;
		for (int32 Idx = 0; Idx < N - 1; ++Idx)
		{
			Sn = (2 * Idx + 3) * S1 / X - S0;
			S0 = S1;
			S1 = Sn;
			if (std::isinf(Sn))
			{
				return Sn;
			}
		}
		return Sn;
	}

	double SphJDeriv(int32 N, double X)
	{
		if (N == 0)
		{
			return -SphJ(1, X);
		}
		if (X == 0.0)
		{
			return N == 1 ? 1.0 / 3.0 : 0.0;
		}
		return SphJ(N - 1, X) - (N + 1) * SphJ(N, X) / X;
	}

	double SphYDeriv(int32 N, double X)
	{
		if (N == 0)
		{
			return -SphY(1, X);
		}
		return SphY(N - 1, X) - (N + 1) * SphY(N, X) / X;
	}

	FComplex SphH2(int32 N, double X)
	{
		return FComplex(SphJ(N, X), -SphY(N, X));
	}

	FComplex SphH2Deriv(int32 N, double X)
	{
		return FComplex(SphJDeriv(N, X), -SphYDeriv(N, X));
	}

	double BesselI0(double X)
	{
		// Power series: sum_k ((x/2)^k / k!)^2; converges for every x used here (Kaiser beta <= 20).
		const double Half = 0.5 * X;
		double Term = 1.0;
		double Sum = 1.0;
		for (int32 K = 1; K < 300; ++K)
		{
			Term *= (Half / K) * (Half / K);
			Sum += Term;
			if (Term < 1e-17 * Sum)
			{
				break;
			}
		}
		return Sum;
	}

	double BrentQ(TFunctionRef<double(double)> F, double XA, double XB, double XTol, double RTol, int32 MaxIterations)
	{
		double XPre = XA;
		double XCur = XB;
		double XBlk = 0.0;
		double FPre = F(XPre);
		double FCur = F(XCur);
		double FBlk = 0.0;
		double SPre = 0.0;
		double SCur = 0.0;
		if (FPre == 0.0)
		{
			return XPre;
		}
		if (FCur == 0.0)
		{
			return XCur;
		}
		for (int32 I = 0; I < MaxIterations; ++I)
		{
			if (FPre != 0.0 && FCur != 0.0 && (std::signbit(FPre) != std::signbit(FCur)))
			{
				XBlk = XPre;
				FBlk = FPre;
				SPre = SCur = XCur - XPre;
			}
			if (std::fabs(FBlk) < std::fabs(FCur))
			{
				XPre = XCur;
				XCur = XBlk;
				XBlk = XPre;
				FPre = FCur;
				FCur = FBlk;
				FBlk = FPre;
			}
			const double Delta = (XTol + RTol * std::fabs(XCur)) / 2.0;
			const double SBis = (XBlk - XCur) / 2.0;
			if (FCur == 0.0 || std::fabs(SBis) < Delta)
			{
				return XCur;
			}
			if (std::fabs(SPre) > Delta && std::fabs(FCur) < std::fabs(FPre))
			{
				double STry;
				if (XPre == XBlk)
				{
					STry = -FCur * (XCur - XPre) / (FCur - FPre); // interpolate
				}
				else
				{
					const double DPre = (FPre - FCur) / (XPre - XCur); // extrapolate
					const double DBlk = (FBlk - FCur) / (XBlk - XCur);
					STry = -FCur * (FBlk * DBlk - FPre * DPre) / (DBlk * DPre * (FBlk - FPre));
				}
				if (2.0 * std::fabs(STry) < FMath::Min(std::fabs(SPre), 3.0 * std::fabs(SBis) - Delta))
				{
					SPre = SCur; // good short step
					SCur = STry;
				}
				else
				{
					SPre = SBis; // bisect
					SCur = SBis;
				}
			}
			else
			{
				SPre = SBis;
				SCur = SBis;
			}
			XPre = XCur;
			FPre = FCur;
			if (std::fabs(SCur) > Delta)
			{
				XCur += SCur;
			}
			else
			{
				XCur += (SBis > 0.0 ? Delta : -Delta);
			}
			FCur = F(XCur);
		}
		return XCur;
	}

	void Fft(TArrayView<FComplex> Data, bool bInverse)
	{
		const int32 N = Data.Num();
		check(N > 0 && FMath::IsPowerOfTwo(N));
		// Bit reversal.
		for (int32 I = 1, J = 0; I < N; ++I)
		{
			int32 Bit = N >> 1;
			for (; J & Bit; Bit >>= 1)
			{
				J ^= Bit;
			}
			J ^= Bit;
			if (I < J)
			{
				Swap(Data[I], Data[J]);
			}
		}
		const double Sign = bInverse ? 1.0 : -1.0;
		for (int32 Len = 2; Len <= N; Len <<= 1)
		{
			const int32 HalfLen = Len >> 1;
			const double Step = 2.0 * Pi / Len;
			for (int32 K = 0; K < HalfLen; ++K)
			{
				const double Angle = Step * K;
				const FComplex W(std::cos(Angle), Sign * std::sin(Angle));
				for (int32 Start = 0; Start < N; Start += Len)
				{
					const FComplex U = Data[Start + K];
					const FComplex V = Data[Start + K + HalfLen] * W;
					Data[Start + K] = U + V;
					Data[Start + K + HalfLen] = U - V;
				}
			}
		}
	}

	void InverseRealFft(TConstArrayView<FComplex> Half, int32 N, TArray<double>& Out)
	{
		check(Half.Num() == N / 2 + 1);
		TArray<FComplex> Full;
		Full.SetNumZeroed(N);
		for (int32 K = 0; K <= N / 2; ++K)
		{
			Full[K] = Half[K];
		}
		Full[0] = FComplex(Half[0].real(), 0.0);
		Full[N / 2] = FComplex(Half[N / 2].real(), 0.0);
		for (int32 K = 1; K < N / 2; ++K)
		{
			Full[N - K] = std::conj(Half[K]);
		}
		Fft(Full, true);
		Out.SetNumUninitialized(N);
		for (int32 K = 0; K < N; ++K)
		{
			Out[K] = Full[K].real() / N;
		}
	}

	void KaiserLowPass(int32 Taps, double CutoffHz, double SampleRate, double Beta, TArray<double>& Out)
	{
		const double Nyquist = 0.5 * SampleRate;
		const double Right = CutoffHz / Nyquist;
		const double Alpha = 0.5 * (Taps - 1);
		const double I0Beta = BesselI0(Beta);
		Out.SetNumUninitialized(Taps);
		double Sum = 0.0;
		for (int32 N = 0; N < Taps; ++N)
		{
			const double M = N - Alpha;
			const double X = Right * M;
			const double Sinc = X == 0.0 ? 1.0 : std::sin(Pi * X) / (Pi * X);
			const double R = (N - Alpha) / Alpha;
			const double Window = BesselI0(Beta * std::sqrt(1.0 - R * R)) / I0Beta;
			Out[N] = Right * Sinc * Window;
			Sum += Out[N];
		}
		for (double& H : Out)
		{
			H /= Sum;
		}
	}

	const TArray<double>& DecimationFir(double SampleRate)
	{
		static FCriticalSection Lock;
		static TMap<int64, TArray<double>> Cache;
		const int64 Key = FMath::RoundToInt64(SampleRate * 1000.0);
		FScopeLock Guard(&Lock);
		if (const TArray<double>* Found = Cache.Find(Key))
		{
			return *Found;
		}
		TArray<double> Taps;
		KaiserLowPass(DecimationTaps, 0.5 * SampleRate, SampleRate * DecimationFactor, DecimationKaiserBeta, Taps);
		return Cache.Add(Key, MoveTemp(Taps));
	}

	FBiquad FBiquad::FromAnalog(double Sb0, double Sb1, double Sb2, double Sa0, double Sa1, double Sa2, double SampleRate, double PrewarpHz)
	{
		double K = 2.0 * SampleRate;
		if (PrewarpHz > 0.0)
		{
			const double W = 2.0 * Pi * PrewarpHz;
			K = W / std::tan(W / (2.0 * SampleRate));
		}
		const double K2 = K * K;
		const double N0 = Sb0 * K2 + Sb1 * K + Sb2;
		const double N1 = -2.0 * Sb0 * K2 + 2.0 * Sb2;
		const double N2 = Sb0 * K2 - Sb1 * K + Sb2;
		const double D0 = Sa0 * K2 + Sa1 * K + Sa2;
		const double D1 = -2.0 * Sa0 * K2 + 2.0 * Sa2;
		const double D2 = Sa0 * K2 - Sa1 * K + Sa2;
		FBiquad Q;
		Q.B0 = N0 / D0;
		Q.B1 = N1 / D0;
		Q.B2 = N2 / D0;
		Q.A1 = D1 / D0;
		Q.A2 = D2 / D0;
		return Q;
	}

	FBiquad FBiquad::ButterLowPass2(double CutoffHz, double SampleRate)
	{
		const double W = 2.0 * Pi * CutoffHz;
		return FromAnalog(0.0, 0.0, W * W, 1.0, std::sqrt(2.0) * W, W * W, SampleRate, CutoffHz);
	}

	FBiquad FBiquad::ButterHighPass2(double CutoffHz, double SampleRate)
	{
		const double W = 2.0 * Pi * CutoffHz;
		return FromAnalog(1.0, 0.0, 0.0, 1.0, std::sqrt(2.0) * W, W * W, SampleRate, CutoffHz);
	}

	void FBiquad::ButterBandPass2(double LoHz, double HiHz, double SampleRate, FBiquad& OutA, FBiquad& OutB)
	{
		OutA = ButterHighPass2(LoHz, SampleRate);
		OutB = ButterLowPass2(FMath::Min(HiHz, 0.45 * SampleRate), SampleRate);
	}

	FBiquad FBiquad::OnePoleLowPass(double CutoffHz, double SampleRate)
	{
		const double W = 2.0 * Pi * CutoffHz;
		const double K = W / std::tan(W / (2.0 * SampleRate));
		FBiquad Q;
		Q.B0 = W / (W + K);
		Q.B1 = Q.B0;
		Q.B2 = 0.0;
		Q.A1 = (W - K) / (W + K);
		Q.A2 = 0.0;
		return Q;
	}

	void FNoise::Reseed(uint64 Seed)
	{
		// splitmix64 of the seed (never 0).
		uint64 Z = Seed + 0x9E3779B97F4A7C15ull;
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		Z = Z ^ (Z >> 31);
		State = Z != 0 ? Z : 0x9E3779B97F4A7C15ull;
	}

	uint64 HashMix(uint64 A, uint64 B)
	{
		uint64 H = 1469598103934665603ull;
		for (int32 I = 0; I < 8; ++I)
		{
			H = (H ^ ((A >> (8 * I)) & 0xFFu)) * 1099511628211ull;
		}
		for (int32 I = 0; I < 8; ++I)
		{
			H = (H ^ ((B >> (8 * I)) & 0xFFu)) * 1099511628211ull;
		}
		return H;
	}
}
