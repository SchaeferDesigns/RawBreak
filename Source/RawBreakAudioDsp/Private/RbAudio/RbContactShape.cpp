#include "RbAudio/RbContactShape.h"

#include "RbAudio/RbAudioMath.h"

#include "Misc/ScopeLock.h"

#include <cmath>

// Owner: M2-C. Line-by-line port of Tools/audio/click_synth.py (same operation order, so the golden data of
// Tools/audio/out/ref/golden_runtime_48k.json is reproduced to rounding; AU-T01..T04, AU-T11).

namespace RbAudio
{
	FHertzIntegration IntegrateHertz(double V, double MStar, double K, double Alpha, int32 Sub, double MaxTime, bool bKeepForces)
	{
		const double Eta = Alpha * std::sqrt(MStar * K);
		const double H = 1.0 / (HertzGridRate * Sub);
		auto Acc = [&](double D, double DD) -> double
		{
			if (D <= 0.0)
			{
				return 0.0;
			}
			const double F = K * std::pow(D, 1.5) + Eta * std::pow(D, 0.25) * DD;
			return F > 0.0 ? -F / MStar : 0.0;
		};
		auto Force = [&](double D, double DD) -> double
		{
			if (D <= 0.0)
			{
				return 0.0;
			}
			const double F = K * std::pow(D, 1.5) + Eta * std::pow(D, 0.25) * DD;
			return F > 0.0 ? F : 0.0;
		};

		FHertzIntegration Out;
		double D = 0.0;
		double DD = V;
		double Sum = 0.0;
		double Peak = 0.0;
		if (bKeepForces)
		{
			Out.Forces.Add(0.0);
		}
		int64 Step = 0;
		const int64 MaxSteps = static_cast<int64>(MaxTime / H);
		while (Step < MaxSteps)
		{
			const double K1D = DD;
			const double K1V = Acc(D, DD);
			const double K2D = DD + 0.5 * H * K1V;
			const double K2V = Acc(D + 0.5 * H * K1D, DD + 0.5 * H * K1V);
			const double K3D = DD + 0.5 * H * K2V;
			const double K3V = Acc(D + 0.5 * H * K2D, DD + 0.5 * H * K2V);
			const double K4D = DD + H * K3V;
			const double K4V = Acc(D + H * K3D, DD + H * K3V);
			D += H / 6.0 * (K1D + 2 * K2D + 2 * K3D + K4D);
			DD += H / 6.0 * (K1V + 2 * K2V + 2 * K3V + K4V);
			++Step;
			if (Step % Sub == 0)
			{
				const double F = Force(D, DD);
				Sum += F;
				Peak = FMath::Max(Peak, F);
				if (bKeepForces)
				{
					Out.Forces.Add(F);
				}
			}
			if (D <= 0.0 && DD < 0.0)
			{
				break;
			}
		}
		if (bKeepForces)
		{
			Out.Forces.Add(0.0);
		}
		Out.Duration = static_cast<double>(Step) * H;
		Out.ExitSpeed = -DD;
		Out.PeakForce = Peak;
		Out.Impulse = Sum / HertzGridRate;
		return Out;
	}

	double AlphaForRestitution(double E)
	{
		if (E >= 0.9999)
		{
			return 0.0;
		}
		const double MStar = StdBallMass / 2.0;
		return BrentQ([&](double Alpha) { return IntegrateHertz(1.0, MStar, CoreHertzK, Alpha).ExitSpeed - E; }, 1e-6, 1.5, 1e-7);
	}

	double FContactShape::Eval(double U) const
	{
		const int32 N = Shape.Num();
		if (N < 2 || !(U >= 0.0) || U > 1.0)
		{
			return 0.0;
		}
		const double Step = 1.0 / (N - 1);
		if (U == 1.0)
		{
			return Shape[N - 1];
		}
		int32 J = FMath::Clamp(static_cast<int32>(std::floor(U * (N - 1))), 0, N - 2);
		// numpy's binary search: xp[j] <= u < xp[j + 1] with xp[j] = j * step.
		while (J > 0 && J * Step > U)
		{
			--J;
		}
		while (J < N - 2 && (J + 1) * Step <= U)
		{
			++J;
		}
		const double X0 = J * Step;
		if (X0 == U)
		{
			return Shape[J];
		}
		const double X1 = (J + 1) == N - 1 ? 1.0 : (J + 1) * Step;
		const double Slope = (Shape[J + 1] - Shape[J]) / (X1 - X0);
		return Slope * (U - X0) + Shape[J];
	}

	FContactShape ComputeContactShape(double E, int32 Points, double H)
	{
		FContactShape Out;
		Out.Restitution = E;
		const double Alpha = AlphaForRestitution(E);
		Out.Alpha = Alpha;
		auto Acc = [Alpha](double X, double XD) -> double
		{
			if (X <= 0.0)
			{
				return 0.0;
			}
			const double Fo = std::pow(X, 1.5) + Alpha * std::pow(X, 0.25) * XD;
			return Fo > 0.0 ? -Fo : 0.0;
		};
		double X = 0.0;
		double XD = 1.0;
		double S = 0.0;
		TArray<double> Ss;
		TArray<double> Ff;
		Ss.Reserve(static_cast<int32>(4.0 / H) + 16);
		Ff.Reserve(static_cast<int32>(4.0 / H) + 16);
		Ss.Add(0.0);
		Ff.Add(0.0);
		for (int32 Guard = 0; Guard < 100000000; ++Guard)
		{
			const double K1X = XD;
			const double K1V = Acc(X, XD);
			const double K2X = XD + 0.5 * H * K1V;
			const double K2V = Acc(X + 0.5 * H * K1X, XD + 0.5 * H * K1V);
			const double K3X = XD + 0.5 * H * K2V;
			const double K3V = Acc(X + 0.5 * H * K2X, XD + 0.5 * H * K2V);
			const double K4X = XD + H * K3V;
			const double K4V = Acc(X + H * K3X, XD + H * K3V);
			X += H / 6 * (K1X + 2 * K2X + 2 * K3X + K4X);
			XD += H / 6 * (K1V + 2 * K2V + 2 * K3V + K4V);
			S += H;
			Ss.Add(S);
			Ff.Add(X > 0.0 ? FMath::Max(0.0, std::pow(X, 1.5) + Alpha * std::pow(X, 0.25) * XD) : 0.0);
			if (X <= 0.0 && XD < 0.0)
			{
				break;
			}
		}
		const double Tau = Ss.Last();
		double Phi = 0.0;
		for (double F : Ff)
		{
			Phi = FMath::Max(Phi, F);
		}
		Out.Tau = Tau;
		Out.Phi = Phi;

		// grid = numpy.linspace(0, tau, n); shape = numpy.interp(grid, ss, ff) / phi.
		Out.Shape.SetNumUninitialized(Points);
		const double GridStep = Tau / (Points - 1);
		int32 J = 0;
		const int32 Last = Ss.Num() - 1;
		for (int32 I = 0; I < Points; ++I)
		{
			const double G = I == Points - 1 ? Tau : I * GridStep;
			double Value;
			if (G >= Ss[Last])
			{
				Value = Ff[Last];
			}
			else
			{
				while (J < Last - 1 && Ss[J + 1] <= G)
				{
					++J;
				}
				if (Ss[J] == G)
				{
					Value = Ff[J];
				}
				else
				{
					const double Slope = (Ff[J + 1] - Ff[J]) / (Ss[J + 1] - Ss[J]);
					Value = Slope * (G - Ss[J]) + Ff[J];
				}
			}
			Out.Shape[I] = Value / Phi;
		}
		// numpy.trapezoid(shape, grid / tau)
		double Area = 0.0;
		for (int32 I = 1; I < Points; ++I)
		{
			const double U0 = (I - 1 == Points - 1 ? Tau : (I - 1) * GridStep) / Tau;
			const double U1 = (I == Points - 1 ? Tau : I * GridStep) / Tau;
			Area += 0.5 * (Out.Shape[I] + Out.Shape[I - 1]) * (U1 - U0);
		}
		Out.Area = Area;
		return Out;
	}

	double QuantizeRestitution(double E)
	{
		E = FMath::Clamp(E, 0.05, 1.0);
		if (FMath::Abs(E - BallRestitution) < 1e-9)
		{
			return BallRestitution;
		}
		return FMath::RoundToDouble(E * 100.0) / 100.0;
	}

	FContactShapePtr GetContactShape(double E)
	{
		static FCriticalSection Lock;
		static TMap<int32, FContactShapePtr> Cache;
		const double Q = QuantizeRestitution(E);
		const int32 Key = FMath::RoundToInt32(Q * 10000.0);
		{
			FScopeLock Guard(&Lock);
			if (const FContactShapePtr* Found = Cache.Find(Key))
			{
				return *Found;
			}
		}
		// Computed outside the lock (two threads may both compute the same shape once; the first one stays).
		const double Step = Q == BallRestitution ? 2e-5 : 1e-4;
		FContactShapePtr Shape = MakeShared<FContactShape, ESPMode::ThreadSafe>(ComputeContactShape(Q, 129, Step));
		FScopeLock Guard(&Lock);
		if (const FContactShapePtr* Found = Cache.Find(Key))
		{
			return *Found;
		}
		Cache.Add(Key, Shape);
		return Shape;
	}

	FContactPulse HertzPulse(const FContactShape& Shape, double V, double MStar, double K)
	{
		FContactPulse Pulse;
		if (!(V > 0.0) || !(MStar > 0.0) || !(K > 0.0))
		{
			return Pulse;
		}
		const double T0 = std::pow(MStar * MStar / (K * K * V), 0.2);
		const double D0 = std::pow(MStar * V * V / K, 0.4);
		Pulse.Duration = Shape.Tau * T0;
		Pulse.PeakForce = Shape.Phi * K * std::pow(D0, 1.5);
		Pulse.Impulse = (1.0 + Shape.Restitution) * MStar * V;
		return Pulse;
	}

	FContactPulse HertzPulse(double V, double MStar, double K, double E)
	{
		return HertzPulse(*GetContactShape(E), V, MStar, K);
	}

	double StiffnessForContactTime(double T1, double MStar, double V)
	{
		return std::sqrt(MStar * MStar / (V * std::pow(T1 / 3.2181, 5.0)));
	}

	double CushionRestitution(double V)
	{
		return FMath::Clamp(0.97 - 0.035 * FMath::Max(0.0, V - 1.0), 0.60, 0.97);
	}

	double HertzStiffness(double R1, double E1, double Nu1, double R2, double E2, double Nu2)
	{
		const double RStar = R1 * R2 / (R1 + R2);
		const double Inv = (1.0 - Nu1 * Nu1) / E1 + (1.0 - Nu2 * Nu2) / E2;
		return 4.0 / 3.0 * std::sqrt(RStar) / Inv;
	}

	double Sin15Area()
	{
		static const double Area = std::tgamma(1.25) / (std::sqrt(Pi) * std::tgamma(1.75));
		return Area;
	}

	namespace
	{
		// Decimates F4 (4 x fs) with the FIR: G[k] = sum_m h[m] F4[4k - m], k = 0 .. (len(F4) + 128 + 3) / 4 - 1.
		void Decimate(const TArray<double>& F4, double SampleRate, TArray<double>& OutG)
		{
			const TArray<double>& Fir = DecimationFir(SampleRate);
			const int32 J = F4.Num();
			const int32 Taps = Fir.Num();
			const int32 Count = (J + Taps - 1 + DecimationFactor - 1) / DecimationFactor;
			OutG.SetNumUninitialized(Count);
			for (int32 K = 0; K < Count; ++K)
			{
				const int32 N = DecimationFactor * K;
				double Sum = 0.0;
				const int32 MLo = FMath::Max(0, N - (J - 1));
				const int32 MHi = FMath::Min(Taps - 1, N);
				for (int32 M = MLo; M <= MHi; ++M)
				{
					Sum += Fir[M] * F4[N - M];
				}
				OutG[K] = Sum;
			}
		}
	}

	void RuntimeForceHertz(const FContactShape& Shape, double T, double FMax, double Frac, double SampleRate, TArray<double>& OutG)
	{
		const int32 N48 = static_cast<int32>(std::ceil(T * SampleRate + Frac)) + 1;
		const int32 J = DecimationFactor * N48;
		// Per-thread scratch: the audio render thread renders impacts without allocating once it has grown.
		thread_local TArray<double> F4;
		F4.SetNumUninitialized(J, EAllowShrinking::No);
		FMemory::Memzero(F4.GetData(), sizeof(double) * J);
		const double Hi = SampleRate * DecimationFactor;
		for (int32 I = 0; I < J; ++I)
		{
			const double U = (static_cast<double>(I) / Hi - Frac / SampleRate) / T;
			if (U >= 0.0 && U <= 1.0)
			{
				F4[I] = FMax * Shape.Eval(U);
			}
		}
		Decimate(F4, SampleRate, OutG);
	}

	void RuntimeForceSine(double Impulse, double T, double Frac, double SampleRate, TArray<double>& OutG)
	{
		const int32 N48 = static_cast<int32>(std::ceil(T * SampleRate + Frac)) + 1;
		const int32 J = DecimationFactor * N48;
		// Per-thread scratch: the audio render thread renders impacts without allocating once it has grown.
		thread_local TArray<double> F4;
		F4.SetNumUninitialized(J, EAllowShrinking::No);
		FMemory::Memzero(F4.GetData(), sizeof(double) * J);
		const double Hi = SampleRate * DecimationFactor;
		const double Amp = Impulse / (T * Sin15Area());
		for (int32 I = 0; I < J; ++I)
		{
			const double U = (static_cast<double>(I) / Hi - Frac / SampleRate) / T;
			if (U >= 0.0 && U <= 1.0)
			{
				F4[I] = Amp * std::pow(std::sin(Pi * U), 1.5);
			}
		}
		Decimate(F4, SampleRate, OutG);
	}
}
