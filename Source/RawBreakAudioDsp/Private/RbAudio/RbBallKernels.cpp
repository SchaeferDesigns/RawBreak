#include "RbAudio/RbBallKernels.h"

#include "Misc/ScopeLock.h"

#include <cmath>

// Owner: M2-C. Port of click_synth.py (lamb_roots, lamb_mode, sphere_modes, radiation_accel, band_taper, far_order_kernels).

namespace RbAudio
{
	double YoungFromHertz(double K, double R, double Nu)
	{
		const double EStar = 3.0 * K / (4.0 * std::sqrt(R / 2.0));
		return 2.0 * (1.0 - Nu * Nu) * EStar;
	}

	FBallMaterial PhenolicMaterial()
	{
		FBallMaterial M;
		M.Poisson = 0.35;
		M.YoungModulus = YoungFromHertz(CoreHertzK, StdBallRadius, M.Poisson);
		M.Loss = 0.015;
		return M;
	}

	FBallMaterial PolyesterMaterial()
	{
		FBallMaterial M;
		M.YoungModulus = 5.0e9;
		M.Poisson = 0.36;
		M.Loss = 0.030;
		return M;
	}

	namespace
	{
		void Lame(const FBallMaterial& Mat, double& OutLambda, double& OutMu)
		{
			OutLambda = Mat.YoungModulus * Mat.Poisson / ((1.0 + Mat.Poisson) * (1.0 - 2.0 * Mat.Poisson));
			OutMu = Mat.YoungModulus / (2.0 * (1.0 + Mat.Poisson));
		}

		void Jn3(int32 N, double X, double& J, double& Jp, double& Jpp)
		{
			J = SphJ(N, X);
			Jp = SphJDeriv(N, X);
			Jpp = -2.0 / X * Jp - (1.0 - N * (N + 1) / (X * X)) * J;
		}

		void LambRows(int32 N, double Omega, double A, double Lambda, double Mu, double Rho, double& SrrA, double& SrrB, double& SrtA, double& SrtB)
		{
			const double CL = std::sqrt((Lambda + 2.0 * Mu) / Rho);
			const double CT = std::sqrt(Mu / Rho);
			const double KL = Omega / CL;
			const double KT = Omega / CT;
			double F, Fp, Fpp;
			Jn3(N, KL * A, F, Fp, Fpp);
			Fp = Fp * KL;
			Fpp = Fpp * KL * KL;
			double G, Gp, Gpp;
			Jn3(N, KT * A, G, Gp, Gpp);
			Gp = Gp * KT;
			Gpp = Gpp * KT * KT;
			SrrA = -Lambda * KL * KL * F + 2.0 * Mu * Fpp;
			SrrB = 2.0 * Mu * N * (N + 1) * (Gp / A - G / (A * A));
			SrtA = 2.0 * Fp / A - 2.0 * F / (A * A);
			SrtB = Gpp + (N * (N + 1) - 2) * G / (A * A);
		}

		double LambDet(int32 N, double X, double A, double Lambda, double Mu, double Rho)
		{
			const double CT = std::sqrt(Mu / Rho);
			const double Omega = X * CT / A;
			double SrrA, SrrB, SrtA, SrtB;
			LambRows(N, Omega, A, Lambda, Mu, Rho, SrrA, SrrB, SrtA, SrtB);
			if (N == 0)
			{
				return SrrA;
			}
			return (SrrA * SrtB - SrrB * SrtA) / (Mu * Mu);
		}

		int SignOf(double V)
		{
			return V > 0.0 ? 1 : (V < 0.0 ? -1 : 0);
		}
	}

	TArray<double> LambRoots(int32 N, const FBallMaterial& Material, double Radius, double Density, double XMax, int32 Count)
	{
		double Lambda, Mu;
		Lame(Material, Lambda, Mu);
		constexpr int32 Samples = 6000;
		const double X0 = 0.3;
		const double Step = (XMax - X0) / (Samples - 1);
		TArray<double> Xs;
		TArray<double> Vals;
		Xs.SetNumUninitialized(Samples);
		Vals.SetNumUninitialized(Samples);
		for (int32 I = 0; I < Samples; ++I)
		{
			Xs[I] = I == Samples - 1 ? XMax : X0 + I * Step;
			Vals[I] = LambDet(N, Xs[I], Radius, Lambda, Mu, Density);
		}
		TArray<double> Roots;
		for (int32 I = 0; I < Samples - 1; ++I)
		{
			if (SignOf(Vals[I]) != SignOf(Vals[I + 1]))
			{
				Roots.Add(BrentQ([&](double X) { return LambDet(N, X, Radius, Lambda, Mu, Density); }, Xs[I], Xs[I + 1], 1e-12));
				if (Roots.Num() >= Count)
				{
					break;
				}
			}
		}
		return Roots;
	}

	FSphereMode LambMode(int32 N, double X, const FBallAcoustics& Ball, int32 RootIndex)
	{
		const double A = Ball.Radius;
		const double Rho = Ball.Density();
		double Lambda, Mu;
		Lame(Ball.Material, Lambda, Mu);
		const double CL = std::sqrt((Lambda + 2.0 * Mu) / Rho);
		const double CT = std::sqrt(Mu / Rho);
		const double Omega = X * CT / A;
		double SrrA, SrrB, SrtA, SrtB;
		LambRows(N, Omega, A, Lambda, Mu, Rho, SrrA, SrrB, SrtA, SrtB);
		double CA, CB;
		if (N == 0)
		{
			CA = 1.0;
			CB = 0.0;
		}
		else if (std::fabs(SrrA) + std::fabs(SrrB) > 1e-12 * (std::fabs(SrtA) + std::fabs(SrtB)) * Mu)
		{
			CA = SrrB;
			CB = -SrrA;
		}
		else
		{
			CA = SrtB;
			CB = -SrtA;
		}
		const double KL = Omega / CL;
		const double KT = Omega / CT;
		constexpr int32 Points = 4001;
		const double R0 = A * 1e-4;
		const double Step = (A - R0) / (Points - 1);
		TArray<double> Rs, Us, Vs;
		Rs.SetNumUninitialized(Points);
		Us.SetNumUninitialized(Points);
		Vs.SetNumUninitialized(Points);
		for (int32 I = 0; I < Points; ++I)
		{
			const double R = I == Points - 1 ? A : R0 + I * Step;
			const double F = SphJ(N, KL * R);
			const double Fp = KL * SphJDeriv(N, KL * R);
			const double G = SphJ(N, KT * R);
			const double Gp = KT * SphJDeriv(N, KT * R);
			Rs[I] = R;
			Us[I] = CA * Fp + CB * N * (N + 1) * G / R;
			Vs[I] = CA * F / R + CB * (G / R + Gp);
		}
		const double S = 1.0 / Us[Points - 1];
		double Integral = 0.0;
		double Prev = 0.0;
		for (int32 I = 0; I < Points; ++I)
		{
			const double U = Us[I] * S;
			const double V = Vs[I] * S;
			const double Integrand = (U * U * 2.0 / (2 * N + 1) + V * V * 2.0 * N * (N + 1) / (2 * N + 1)) * Rs[I] * Rs[I];
			if (I > 0)
			{
				Integral += 0.5 * (Integrand + Prev) * (Rs[I] - Rs[I - 1]);
			}
			Prev = Integrand;
		}
		FSphereMode Mode;
		Mode.Order = N;
		Mode.Root = RootIndex;
		Mode.X = X;
		Mode.FrequencyHz = Omega / (2.0 * Pi);
		Mode.ModalMass = Rho * 2.0 * Pi * Integral;
		return Mode;
	}

	TArray<FSphereMode> SphereModes(const FBallAcoustics& Ball, double FMax)
	{
		TArray<FSphereMode> Modes;
		for (int32 N = 0; N <= 3; ++N)
		{
			const TArray<double> Roots = LambRoots(N, Ball.Material, Ball.Radius, Ball.Density(), 14.0, 2);
			for (int32 I = 0; I < Roots.Num(); ++I)
			{
				const FSphereMode M = LambMode(N, Roots[I], Ball, I + 1);
				if (M.FrequencyHz <= FMax)
				{
					Modes.Add(M);
				}
			}
		}
		Modes.StableSort([](const FSphereMode& L, const FSphereMode& R) { return L.FrequencyHz < R.FrequencyHz; });
		return Modes;
	}

	FComplex RadiationAccel(int32 N, double FrequencyHz, double R, double A)
	{
		if (!(FrequencyHz > 0.0))
		{
			return FComplex(0.0, 0.0);
		}
		const double W = 2.0 * Pi * FrequencyHz;
		const double K = W / C0;
		return -Rho0 * C0 * SphH2(N, K * R) / (W * SphH2Deriv(N, K * A));
	}

	FComplex BandTaper(double F)
	{
		const FComplex S(0.0, F / 12.0);
		FComplex T = S * S / (S * S + std::sqrt(2.0) * S + 1.0);
		const double Hi0 = 20000.0;
		const double Hi1 = 23500.0;
		if (F > Hi0)
		{
			const double U = FMath::Clamp((F - Hi0) / (Hi1 - Hi0), 0.0, 1.0);
			T *= 0.5 + 0.5 * std::cos(Pi * U);
		}
		return T;
	}

	void ComputeFarOrderKernels(const FBallAcoustics& Ball, double SampleRate, bool bRigidOnly, TArray<double> OutOrders[4])
	{
		constexpr int32 NFft = 16384;
		const int32 Bins = NFft / 2 + 1;
		const double Zeta = Ball.Material.Loss / 2.0;
		const TArray<FSphereMode> Modes = bRigidOnly ? TArray<FSphereMode>() : SphereModes(Ball);
		const FComplex I(0.0, 1.0);
		TArray<FComplex> H;
		H.SetNumUninitialized(Bins);
		for (int32 N = 0; N <= 3; ++N)
		{
			// i^(n + 1)
			FComplex IPow(1.0, 0.0);
			for (int32 P = 0; P < N + 1; ++P)
			{
				IPow *= I;
			}
			for (int32 Bin = 0; Bin < Bins; ++Bin)
			{
				const double F = Bin * (SampleRate / NFft);
				if (!(F > 0.0))
				{
					H[Bin] = FComplex(0.0, 0.0);
					continue;
				}
				const double W = 2.0 * Pi * F;
				const double K = W / C0;
				const FComplex Hf = -Rho0 * C0 * C0 * IPow * std::exp(-I * (K * Ball.Radius)) / (W * W * SphH2Deriv(N, K * Ball.Radius));
				FComplex Acc(0.0, 0.0);
				if (N == 1)
				{
					Acc += -1.0 / Ball.Mass;
				}
				for (const FSphereMode& M : Modes)
				{
					if (M.Order == N)
					{
						const double Wn = 2.0 * Pi * M.FrequencyHz;
						Acc += W * W / (M.ModalMass * (Wn * Wn - W * W + 2.0 * I * Zeta * Wn * W));
					}
				}
				H[Bin] = Hf * Acc * BandTaper(F) * std::exp(-2.0 * I * Pi * F * static_cast<double>(KernelPreDelay) / SampleRate);
			}
			TArray<double> Impulse;
			InverseRealFft(H, NFft, Impulse);
			OutOrders[N].SetNumUninitialized(KernelTaps);
			for (int32 K = 0; K < KernelTaps; ++K)
			{
				OutOrders[N][K] = Impulse[K];
			}
		}
	}

	FBallKernelsPtr GetBallKernels(const FBallAcoustics& Ball, double SampleRate, bool bRigidOnly)
	{
		struct FEntry
		{
			FBallAcoustics Ball;
			double SampleRate;
			bool bRigidOnly;
			FBallKernelsPtr Kernels;
		};
		static FCriticalSection Lock;
		static TArray<FEntry> Cache;
		{
			FScopeLock Guard(&Lock);
			for (const FEntry& E : Cache)
			{
				if (E.Ball == Ball && E.SampleRate == SampleRate && E.bRigidOnly == bRigidOnly)
				{
					return E.Kernels;
				}
			}
		}
		TSharedPtr<FBallKernels, ESPMode::ThreadSafe> K = MakeShared<FBallKernels, ESPMode::ThreadSafe>();
		K->SampleRate = SampleRate;
		K->Ball = Ball;
		K->bRigidOnly = bRigidOnly;
		ComputeFarOrderKernels(Ball, SampleRate, bRigidOnly, K->Orders);
		for (int32 N = 0; N <= 3; ++N)
		{
			for (double V : K->Orders[N])
			{
				if (V != 0.0)
				{
					K->OrderMask |= 1u << N;
					break;
				}
			}
		}
		const double Leak = std::exp(-2.0 * Pi * NearFieldLeakHz / SampleRate);
		K->LeakyOrder1.SetNumUninitialized(KernelTaps + FBallKernels::MaxPulseSamples);
		double Acc = 0.0;
		for (int32 Q = 0; Q < K->LeakyOrder1.Num(); ++Q)
		{
			Acc = Acc * Leak + (Q < KernelTaps ? K->Orders[1][Q] : 0.0);
			K->LeakyOrder1[Q] = Acc;
		}
		FScopeLock Guard(&Lock);
		for (const FEntry& E : Cache)
		{
			if (E.Ball == Ball && E.SampleRate == SampleRate && E.bRigidOnly == bRigidOnly)
			{
				return E.Kernels;
			}
		}
		Cache.Add({Ball, SampleRate, bRigidOnly, K});
		return K;
	}
}
