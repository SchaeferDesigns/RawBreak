#include "RbAudio/RbAudioAnalysis.h"

#include "RbAudio/RbContactShape.h"
#include "RbAudio/RbImpactSynth.h"

#include "Algo/BinarySearch.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include <cmath>

// Owner: M2-C. Ports of click_synth.py (render_impact_segment, body_transfer, piston_transfer, ball_ball_impact, onset_separation,
// band_error_db, smooth_octave) for the offline tests; the analysis helpers of the recorded checks.

namespace RbAudio
{
	namespace
	{
		double Dot3(const double A[3], const double B[3]) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
		double Norm3(const double A[3]) { return std::sqrt(Dot3(A, A)); }

		int32 NextPow2(int32 N)
		{
			int32 P = 1;
			while (P < N)
			{
				P <<= 1;
			}
			return P;
		}

		// click_synth.py body_transfer: p(f) / F(f) at the listener for one ball (direct + cloth image).
		FComplex BodyTransfer(double F, const FExactBody& Body, const TArray<FSphereMode>& Modes, const double L[3], bool bImage)
		{
			if (!(F > 0.0))
			{
				return FComplex(0.0, 0.0);
			}
			const double A = Body.Ball.Radius;
			const double M = Body.Ball.Mass;
			const double W = 2.0 * Pi * F;
			const double Zeta = Body.Ball.Material.Loss / 2.0;
			FComplex G(0.0, 0.0);
			for (int32 Src = 0; Src < ((bImage && Body.Center[2] > 0.0) ? 2 : 1); ++Src)
			{
				double C[3] = {Body.Center[0], Body.Center[1], Body.Center[2]};
				double Ax[3] = {Body.Axis[0], Body.Axis[1], Body.Axis[2]};
				FComplex Refl(1.0, 0.0);
				if (Src == 1)
				{
					C[2] = -C[2];
					Ax[2] = -Ax[2];
					Refl = ClothReflection / FComplex(1.0, F / ClothCornerHz);
				}
				const double D[3] = {L[0] - C[0], L[1] - C[1], L[2] - C[2]};
				const double R = Norm3(D);
				const double Cs = Dot3(D, Ax) / R;
				FComplex Gs = Cs * RadiationAccel(1, F, R, A) * (-1.0 / M);
				if (Body.bModes)
				{
					for (const FSphereMode& Md : Modes)
					{
						const double Wn = 2.0 * Pi * Md.FrequencyHz;
						const FComplex Acc = W * W / (Md.ModalMass * FComplex(Wn * Wn - W * W, 2.0 * Zeta * Wn * W));
						Gs += Legendre(Md.Order, Cs) * RadiationAccel(Md.Order, F, R, A) * Acc;
					}
				}
				G += Gs * Refl;
			}
			return G;
		}

		// click_synth.py piston_transfer.
		FComplex PistonTransfer(double F, const FExactPiston& Piston, const double L[3])
		{
			double Fc = 0.0;
			bool bBaffled = true;
			const TConstArrayView<FModalMode> Modes = GetModalModes(Piston.Bank, Fc, bBaffled);
			const double D[3] = {L[0] - Piston.Position[0], L[1] - Piston.Position[1], L[2] - Piston.Position[2]};
			const double R = Norm3(D);
			const double W = 2.0 * Pi * F;
			const double K = W / C0;
			FComplex G(0.0, 0.0);
			for (const FModalMode& Md : Modes)
			{
				const double Wn = 2.0 * Pi * Md.FrequencyHz;
				const FComplex Acc = W * W / (Md.ModalMass * FComplex(Wn * Wn - W * W, Md.LossFactor * Wn * W));
				G += Rho0 * Md.Area * Acc / ((bBaffled ? 2.0 : 4.0) * Pi * R);
			}
			if (Fc > 0.0)
			{
				G *= 1.0 / (1.0 + std::pow(Fc / FMath::Max(F, 1e-3), 2.0));
			}
			return Piston.Gain * G * std::exp(FComplex(0.0, -K * R));
		}

		// 1/Frac-octave moving average of a power spectrum on its own (uniform) grid (click_synth.py smooth_octave).
		void SmoothOctave(const TArray<double>& F, const TArray<double>& Mag2, int32 Frac, TArray<double>& Out)
		{
			const int32 N = F.Num();
			TArray<double> Cs;
			Cs.SetNumZeroed(N + 1);
			for (int32 I = 0; I < N; ++I)
			{
				Cs[I + 1] = Cs[I] + Mag2[I];
			}
			Out.SetNumUninitialized(N);
			const double Lo = std::pow(2.0, -0.5 / Frac);
			const double Hi = std::pow(2.0, 0.5 / Frac);
			for (int32 I = 0; I < N; ++I)
			{
				// searchsorted left of F * Lo, right of F * Hi.
				const double FLo = F[I] * Lo;
				const double FHi = F[I] * Hi;
				int32 A = Algo::LowerBound(F, FLo);
				int32 B = Algo::UpperBound(F, FHi);
				B = FMath::Max(B, A + 1);
				Out[I] = (Cs[FMath::Min(B, N)] - Cs[A]) / (B - A);
			}
		}

		void PowerSpectrum(TConstArrayView<double> X, int32 N, TArray<double>& OutMag2)
		{
			TArray<FComplex> Buf;
			Buf.SetNumZeroed(N);
			for (int32 I = 0; I < FMath::Min(N, X.Num()); ++I)
			{
				Buf[I] = FComplex(X[I], 0.0);
			}
			Fft(Buf, false);
			OutMag2.SetNumUninitialized(N / 2 + 1);
			for (int32 K = 0; K <= N / 2; ++K)
			{
				OutMag2[K] = std::norm(Buf[K]);
			}
		}
	}

	FExactImpact MakeBallBallExactImpact(double V, const FBallAcoustics& B1, const FBallAcoustics& B2, double Restitution)
	{
		const double MStar = B1.Mass * B2.Mass / (B1.Mass + B2.Mass);
		const double S = B1.Radius + B2.Radius;
		const double D = 2.0 * std::sqrt(B1.Radius * B2.Radius);
		const double C1[3] = {-B1.Radius * D / S, 0.0, B1.Radius};
		const double C2[3] = {B2.Radius * D / S, 0.0, B2.Radius};
		const double N[3] = {(C2[0] - C1[0]) / S, (C2[1] - C1[1]) / S, (C2[2] - C1[2]) / S};
		const double K = HertzStiffness(B1.Radius, B1.Material.YoungModulus, B1.Material.Poisson, B2.Radius, B2.Material.YoungModulus, B2.Material.Poisson);
		FExactImpact Imp;
		Imp.Force = IntegrateHertz(V * N[0], MStar, K, AlphaForRestitution(Restitution), 4, 0.05, true).Forces;
		FExactBody& A = Imp.Bodies.AddDefaulted_GetRef();
		A.Ball = B1;
		FExactBody& B = Imp.Bodies.AddDefaulted_GetRef();
		B.Ball = B2;
		for (int32 I = 0; I < 3; ++I)
		{
			A.Center[I] = C1[I];
			A.Axis[I] = N[I];
			B.Center[I] = C2[I];
			B.Axis[I] = -N[I];
		}
		return Imp;
	}

	void MakeBallBallRuntimeEvents(double V, const FBallAcoustics& B1, const FBallAcoustics& B2, const double L[3], double SampleRate,
		TArray<FImpactEvent>& OutEvents, double Restitution)
	{
		const FExactImpact Exact = MakeBallBallExactImpact(V, B1, B2, Restitution);
		const double MStar = B1.Mass * B2.Mass / (B1.Mass + B2.Mass);
		const double K = HertzStiffness(B1.Radius, B1.Material.YoungModulus, B1.Material.Poisson, B2.Radius, B2.Material.YoungModulus, B2.Material.Poisson);
		const double Vn = V * Exact.Bodies[0].Axis[0];
		const FContactShapePtr Shape = GetContactShape(Restitution);
		const FContactPulse Pulse = HertzPulse(*Shape, Vn, MStar, K);
		OutEvents.Reset();
		for (const FExactBody& Body : Exact.Bodies)
		{
			FImpactEvent E;
			E.Kind = EImpactKind::BallBall;
			E.Pulse = EPulseShape::Hertz;
			E.Shape = Shape;
			E.ContactTime = Pulse.Duration;
			E.PeakForce = Pulse.PeakForce;
			E.Impulse = Pulse.Impulse;
			E.NormalSpeed = Vn;
			E.Kernels = GetBallKernels(Body.Ball, SampleRate, false);
			const double A = Body.Ball.Radius;
			for (int32 P = 0; P < 2; ++P)
			{
				double C[3] = {Body.Center[0], Body.Center[1], P == 0 ? Body.Center[2] : -Body.Center[2]};
				double Ax[3] = {Body.Axis[0], Body.Axis[1], P == 0 ? Body.Axis[2] : -Body.Axis[2]};
				const double D[3] = {L[0] - C[0], L[1] - C[1], L[2] - C[2]};
				const double R = Norm3(D);
				const double Cs = Dot3(D, Ax) / R;
				FRadiationPath& Path = E.Paths[P];
				for (int32 Order = 0; Order <= 3; ++Order)
				{
					Path.Weights[Order] = Legendre(Order, Cs);
				}
				Path.NearField = C0 / (R * SampleRate);
				Path.Gain = 1.0 / R;
				Path.DelaySeconds = (R - A) / C0;
				Path.bReflected = P == 1;
				if (P == 0)
				{
					E.ListenerDistance = R;
				}
			}
			E.NumPaths = Body.Center[2] > 0.0 ? 2 : 1;
			E.Reflection = FReflection::Cloth(SampleRate);
			OutEvents.Add(E);
		}
	}

	void RenderExactImpact(const FExactImpact& Impact, const double L[3], double StartSeconds, int32 NumOut, double SampleRate, TArray<double>& Out)
	{
		check(FMath::IsPowerOfTwo(NumOut));
		Out.SetNumZeroed(NumOut);
		const int32 Bins = NumOut / 2 + 1;
		const double Hi = HertzGridRate * (SampleRate / 48000.0);
		// Pulse spectrum (the force DFT at the output bins) x the band taper.
		TArray<FComplex> Spec;
		Spec.SetNumZeroed(Bins);
		TArray<TArray<FSphereMode>> Modes;
		for (const FExactBody& B : Impact.Bodies)
		{
			Modes.Add(B.bModes ? SphereModes(B.Ball) : TArray<FSphereMode>());
		}
		const double Start = StartSeconds * SampleRate;
		const double I0 = std::floor(Start);
		const double Frac = Start - I0;
		for (int32 K = 0; K < Bins; ++K)
		{
			const double F = K * SampleRate / NumOut;
			FComplex Fw(0.0, 0.0);
			const FComplex Step = std::exp(FComplex(0.0, -2.0 * Pi * F / Hi));
			FComplex Ph(1.0, 0.0);
			for (int32 J = 0; J < Impact.Force.Num(); ++J)
			{
				Fw += Impact.Force[J] * Ph;
				Ph *= Step;
			}
			Fw = Fw / Hi * BandTaper(F);
			FComplex G(0.0, 0.0);
			for (int32 B = 0; B < Impact.Bodies.Num(); ++B)
			{
				G += BodyTransfer(F, Impact.Bodies[B], Modes[B], L, Impact.bImage);
			}
			for (const FExactPiston& P : Impact.Pistons)
			{
				G += PistonTransfer(F, P, L);
			}
			if (Impact.LowPassHz > 0.0)
			{
				G = G / FComplex(1.0, F / Impact.LowPassHz);
			}
			Spec[K] = Fw * G * std::exp(FComplex(0.0, -2.0 * Pi * F * Frac / SampleRate));
		}
		TArray<double> Y;
		InverseRealFft(Spec, NumOut, Y);
		const int32 Base = static_cast<int32>(I0);
		for (int32 K = 0; K < NumOut; ++K)
		{
			const int32 Idx = Base + K;
			if (Idx >= 0 && Idx < NumOut)
			{
				Out[Idx] += Y[K] * SampleRate;
			}
		}
	}

	double GroupDelaySamples(TConstArrayView<double> A, TConstArrayView<double> B, double SampleRate, double FLo, double FHi)
	{
		const int32 N = NextPow2(FMath::Max(A.Num(), B.Num()));
		TArray<FComplex> Fa, Fb;
		Fa.SetNumZeroed(N);
		Fb.SetNumZeroed(N);
		for (int32 I = 0; I < A.Num(); ++I)
		{
			Fa[I] = FComplex(A[I], 0.0);
		}
		for (int32 I = 0; I < B.Num(); ++I)
		{
			Fb[I] = FComplex(B[I], 0.0);
		}
		Fft(Fa, false);
		Fft(Fb, false);
		TArray<double> X, Y;
		double Prev = 0.0;
		double Offset = 0.0;
		bool bFirst = true;
		for (int32 K = 0; K <= N / 2; ++K)
		{
			const double F = K * SampleRate / N;
			if (!(F > FLo && F < FHi))
			{
				continue;
			}
			double Ph = std::arg(Fb[K] * std::conj(Fa[K]));
			if (!bFirst)
			{
				double D = Ph + Offset - Prev;
				while (D > Pi)
				{
					Offset -= 2.0 * Pi;
					D -= 2.0 * Pi;
				}
				while (D < -Pi)
				{
					Offset += 2.0 * Pi;
					D += 2.0 * Pi;
				}
			}
			Ph += Offset;
			Prev = Ph;
			bFirst = false;
			X.Add(2.0 * Pi * F);
			Y.Add(Ph);
		}
		const int32 M = X.Num();
		if (M < 2)
		{
			return 0.0;
		}
		double Sx = 0.0, Sy = 0.0, Sxx = 0.0, Sxy = 0.0;
		for (int32 I = 0; I < M; ++I)
		{
			Sx += X[I];
			Sy += Y[I];
			Sxx += X[I] * X[I];
			Sxy += X[I] * Y[I];
		}
		const double Slope = (M * Sxy - Sx * Sy) / (M * Sxx - Sx * Sx);
		return -Slope * SampleRate;
	}

	void BandErrorDb(TConstArrayView<double> Ref, TConstArrayView<double> Test, double SampleRate, double& OutMax, double& OutRms, double WithinDb)
	{
		const int32 N = NextPow2(FMath::Max(1, 2 * Ref.Num()));
		TArray<double> A, B;
		PowerSpectrum(Ref, N, A);
		PowerSpectrum(Test, N, B);
		TArray<double> F, Ma, Mb;
		for (int32 K = 0; K <= N / 2; ++K)
		{
			const double Fk = K * SampleRate / N;
			if (Fk >= 100.0 && Fk <= 16000.0)
			{
				F.Add(Fk);
				Ma.Add(A[K]);
				Mb.Add(B[K]);
			}
		}
		TArray<double> Sa, Sb;
		SmoothOctave(F, Ma, 6, Sa);
		SmoothOctave(F, Mb, 6, Sb);
		double MaxDa = -1e300;
		TArray<double> Da, Db;
		Da.SetNumUninitialized(F.Num());
		Db.SetNumUninitialized(F.Num());
		for (int32 I = 0; I < F.Num(); ++I)
		{
			Da[I] = 10.0 * std::log10(Sa[I] + 1e-300);
			Db[I] = 10.0 * std::log10(Sb[I] + 1e-300);
			MaxDa = FMath::Max(MaxDa, Da[I]);
		}
		double SumW = 0.0;
		for (int32 I = 0; I < F.Num(); ++I)
		{
			if (Da[I] >= MaxDa - WithinDb)
			{
				SumW += Sa[I];
			}
		}
		double Max = 0.0;
		double Acc = 0.0;
		for (int32 I = 0; I < F.Num(); ++I)
		{
			if (Da[I] >= MaxDa - WithinDb)
			{
				const double D = FMath::Abs(Da[I] - Db[I]);
				Max = FMath::Max(Max, D);
				Acc += (Sa[I] / SumW) * D * D;
			}
		}
		OutMax = Max;
		OutRms = std::sqrt(Acc);
	}

	void DetectOnsets(TConstArrayView<double> Mono, double SampleRate, TArray<FOnset>& Out, double HighPassHz, double RiseDb, double RiseWindowMs,
		double MinGapMs, double AbsFloor)
	{
		Out.Reset();
		const int32 N = Mono.Num();
		if (N == 0)
		{
			return;
		}
		TArray<double> X;
		X.SetNumUninitialized(N);
		if (HighPassHz > 0.0)
		{
			FBiquad Hp = FBiquad::ButterHighPass2(HighPassHz, SampleRate);
			FBiquad Hp2 = Hp;
			for (int32 I = 0; I < N; ++I)
			{
				X[I] = Hp2.Process(Hp.Process(Mono[I]));
			}
		}
		else
		{
			for (int32 I = 0; I < N; ++I)
			{
				X[I] = Mono[I];
			}
		}
		// 0.25 ms RMS envelope (trailing window).
		const int32 Win = FMath::Max(2, static_cast<int32>(0.00025 * SampleRate));
		TArray<double> Env;
		Env.SetNumUninitialized(N);
		double Sum = 0.0;
		for (int32 I = 0; I < N; ++I)
		{
			Sum += X[I] * X[I];
			if (I >= Win)
			{
				Sum -= X[I - Win] * X[I - Win];
			}
			Env[I] = std::sqrt(FMath::Max(Sum, 0.0) / Win);
		}
		const int32 Back = FMath::Max(1, static_cast<int32>(RiseWindowMs * 1e-3 * SampleRate));
		const int32 Gap = FMath::Max(1, static_cast<int32>(MinGapMs * 1e-3 * SampleRate));
		const double Ratio = std::pow(10.0, RiseDb / 20.0);
		int32 Last = -Gap - 1;
		for (int32 I = Back; I < N; ++I)
		{
			if (I - Last < Gap || Env[I] <= AbsFloor || Env[I] < Ratio * FMath::Max(Env[I - Back], AbsFloor))
			{
				continue;
			}
			// The rise starts at the first sample of the envelope window that carries a quarter of the new level.
			int32 S = I;
			for (int32 J = FMath::Max(0, I - Win + 1); J <= I; ++J)
			{
				if (std::fabs(X[J]) > 0.25 * Env[I])
				{
					S = J;
					break;
				}
			}
			double Peak = 0.0;
			for (int32 J = I; J < FMath::Min(N, I + Gap); ++J)
			{
				Peak = FMath::Max(Peak, Env[J]);
			}
			FOnset& O = Out.AddDefaulted_GetRef();
			O.Sample = static_cast<double>(S);
			O.LevelDb = 20.0 * std::log10(FMath::Max(Peak, 1e-300));
			Last = I;
		}
	}

	double RmsDb(TConstArrayView<double> X, int32 Begin, int32 End)
	{
		Begin = FMath::Clamp(Begin, 0, X.Num());
		End = FMath::Clamp(End, Begin, X.Num());
		if (End <= Begin)
		{
			return -300.0;
		}
		double Sum = 0.0;
		for (int32 I = Begin; I < End; ++I)
		{
			Sum += X[I] * X[I];
		}
		return 10.0 * std::log10(FMath::Max(Sum / (End - Begin), 1e-300));
	}

	double SchroederRt60(TConstArrayView<double> X, int32 Begin, int32 End, double SampleRate, double DecayDb)
	{
		Begin = FMath::Clamp(Begin, 0, X.Num());
		End = FMath::Clamp(End, Begin, X.Num());
		const int32 N = End - Begin;
		if (N < 16)
		{
			return 0.0;
		}
		TArray<double> E;
		E.SetNumUninitialized(N);
		double Acc = 0.0;
		for (int32 I = N - 1; I >= 0; --I)
		{
			Acc += X[Begin + I] * X[Begin + I];
			E[I] = Acc;
		}
		if (!(E[0] > 0.0))
		{
			return 0.0;
		}
		int32 T5 = -1;
		int32 T2 = -1;
		for (int32 I = 0; I < N; ++I)
		{
			const double L = 10.0 * std::log10(FMath::Max(E[I] / E[0], 1e-300));
			if (T5 < 0 && L <= -5.0)
			{
				T5 = I;
			}
			if (T2 < 0 && L <= -5.0 - DecayDb)
			{
				T2 = I;
				break;
			}
		}
		if (T5 < 0 || T2 <= T5)
		{
			return 0.0;
		}
		return 60.0 / DecayDb * (T2 - T5) / SampleRate;
	}

	bool WriteWavFile(const FString& Path, TConstArrayView<float> Interleaved, int32 NumChannels, int32 SampleRate, bool bFloat32)
	{
		if (NumChannels <= 0)
		{
			return false;
		}
		const int32 Bytes = bFloat32 ? 4 : 3;
		const uint32 DataSize = static_cast<uint32>(Interleaved.Num()) * Bytes;
		TArray<uint8> File;
		File.Reserve(44 + DataSize);
		auto Put32 = [&File](uint32 V) { File.Add(V & 0xFF); File.Add((V >> 8) & 0xFF); File.Add((V >> 16) & 0xFF); File.Add((V >> 24) & 0xFF); };
		auto Put16 = [&File](uint16 V) { File.Add(V & 0xFF); File.Add((V >> 8) & 0xFF); };
		auto PutTag = [&File](const char* Tag) { for (int32 I = 0; I < 4; ++I) { File.Add(static_cast<uint8>(Tag[I])); } };
		PutTag("RIFF");
		Put32(36 + DataSize);
		PutTag("WAVE");
		PutTag("fmt ");
		Put32(16);
		Put16(bFloat32 ? 3 : 1);
		Put16(static_cast<uint16>(NumChannels));
		Put32(static_cast<uint32>(SampleRate));
		Put32(static_cast<uint32>(SampleRate * NumChannels * Bytes));
		Put16(static_cast<uint16>(NumChannels * Bytes));
		Put16(static_cast<uint16>(8 * Bytes));
		PutTag("data");
		Put32(DataSize);
		for (float V : Interleaved)
		{
			if (bFloat32)
			{
				uint32 Bits;
				FMemory::Memcpy(&Bits, &V, 4);
				Put32(Bits);
			}
			else
			{
				const int32 Q = FMath::Clamp(static_cast<int32>(std::lround(static_cast<double>(V) * 8388607.0)), -8388608, 8388607);
				const uint32 U = static_cast<uint32>(Q);
				File.Add(U & 0xFF);
				File.Add((U >> 8) & 0xFF);
				File.Add((U >> 16) & 0xFF);
			}
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveArrayToFile(File, *Path);
	}

	bool ReadWavFile(const FString& Path, TArray<float>& Out, int32& OutChannels, int32& OutRate)
	{
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *Path) || File.Num() < 44)
		{
			return false;
		}
		auto Get32 = [&File](int32 At) { return uint32(File[At]) | (uint32(File[At + 1]) << 8) | (uint32(File[At + 2]) << 16) | (uint32(File[At + 3]) << 24); };
		auto Get16 = [&File](int32 At) { return uint16(File[At]) | (uint16(File[At + 1]) << 8); };
		if (FMemory::Memcmp(File.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(File.GetData() + 8, "WAVE", 4) != 0)
		{
			return false;
		}
		int32 At = 12;
		int32 Format = 0, Channels = 0, Rate = 0, Bits = 0;
		while (At + 8 <= File.Num())
		{
			const uint32 Size = Get32(At + 4);
			if (FMemory::Memcmp(File.GetData() + At, "fmt ", 4) == 0)
			{
				Format = Get16(At + 8);
				Channels = Get16(At + 10);
				Rate = static_cast<int32>(Get32(At + 12));
				Bits = Get16(At + 22);
			}
			else if (FMemory::Memcmp(File.GetData() + At, "data", 4) == 0)
			{
				const int32 Bytes = Bits / 8;
				if (Channels <= 0 || Bytes <= 0)
				{
					return false;
				}
				const int32 Count = static_cast<int32>(FMath::Min<int64>(Size, File.Num() - At - 8) / Bytes);
				Out.SetNumUninitialized(Count);
				const uint8* P = File.GetData() + At + 8;
				for (int32 I = 0; I < Count; ++I, P += Bytes)
				{
					if (Format == 3 && Bytes == 4)
					{
						FMemory::Memcpy(&Out[I], P, 4);
					}
					else if (Bytes == 2)
					{
						Out[I] = static_cast<int16>(uint16(P[0]) | (uint16(P[1]) << 8)) / 32768.0f;
					}
					else if (Bytes == 3)
					{
						int32 V = int32(P[0]) | (int32(P[1]) << 8) | (int32(P[2]) << 16);
						if (V & 0x800000)
						{
							V |= ~0xFFFFFF;
						}
						Out[I] = V / 8388608.0f;
					}
					else if (Bytes == 4)
					{
						const int32 V = int32(uint32(P[0]) | (uint32(P[1]) << 8) | (uint32(P[2]) << 16) | (uint32(P[3]) << 24));
						Out[I] = static_cast<float>(V / 2147483648.0);
					}
				}
				OutChannels = Channels;
				OutRate = Rate;
				return true;
			}
			At += 8 + static_cast<int32>(Size) + (Size & 1);
		}
		return false;
	}

	void ExtractChannel(TConstArrayView<float> Interleaved, int32 NumChannels, int32 Channel, TArray<double>& Out)
	{
		const int32 Frames = NumChannels > 0 ? Interleaved.Num() / NumChannels : 0;
		Out.SetNumUninitialized(Frames);
		for (int32 I = 0; I < Frames; ++I)
		{
			if (Channel >= 0 && Channel < NumChannels)
			{
				Out[I] = Interleaved[I * NumChannels + Channel];
			}
			else
			{
				double S = 0.0;
				for (int32 C = 0; C < NumChannels; ++C)
				{
					S += Interleaved[I * NumChannels + C];
				}
				Out[I] = S / NumChannels;
			}
		}
	}
}
