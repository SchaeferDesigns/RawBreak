#include "RbAudio/RbNoiseSynth.h"

#include "RbAudio/RbPresentation.h"

#include <atomic>
#include <cmath>

// Owner: M2-C. Every level / frequency here is an ESTIMATE of audio.md 2 (to be fitted against recordings, 3.8).

namespace RbAudio
{
	namespace
	{
		void BandFor(ENoiseKind Kind, double& OutLo, double& OutHi)
		{
			switch (Kind)
			{
			case ENoiseKind::SlidingCloth: OutLo = 1500.0; OutHi = 6000.0; break;
			case ENoiseKind::GullyRun: OutLo = 120.0; OutHi = 900.0; break;
			case ENoiseKind::RollingFloor: OutLo = 200.0; OutHi = 3000.0; break;
			case ENoiseKind::RollingCloth:
			default: OutLo = 60.0; OutHi = 700.0; break;
			}
		}

		double UnitRmsNorm(const FBiquad& InA, const FBiquad& InB, double SampleRate)
		{
			FBiquad A = InA;
			FBiquad B = InB;
			double Energy = 0.0;
			const int32 N = static_cast<int32>(SampleRate);
			for (int32 I = 0; I < N; ++I)
			{
				const double Y = B.Process(A.Process(I == 0 ? 1.0 : 0.0));
				Energy += Y * Y;
			}
			return Energy > 0.0 ? 1.0 / std::sqrt(Energy) : 1.0;
		}

		// The normalisation depends only on (kind, rate) but costs a second of filtering: a voice adopting a plan on the audio thread
		// must not pay it (AU-T19). Lock-free per (kind, common device rate); the plan builder prewarms it on its worker.
		constexpr double CachedRates[] = {16000.0, 22050.0, 24000.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
		constexpr int32 NumCachedRates = UE_ARRAY_COUNT(CachedRates);
		std::atomic<double> GNormCache[4][NumCachedRates];

		double CachedUnitRmsNorm(ENoiseKind Kind, const FBiquad& A, const FBiquad& B, double SampleRate)
		{
			const int32 K = FMath::Clamp(static_cast<int32>(Kind), 0, 3);
			for (int32 R = 0; R < NumCachedRates; ++R)
			{
				if (CachedRates[R] == SampleRate)
				{
					double Norm = GNormCache[K][R].load(std::memory_order_relaxed);
					if (Norm <= 0.0)
					{
						Norm = UnitRmsNorm(A, B, SampleRate); // deterministic: concurrent fills store the same value
						GNormCache[K][R].store(Norm, std::memory_order_relaxed);
					}
					return Norm;
				}
			}
			return UnitRmsNorm(A, B, SampleRate);
		}

		// Two-pole resonator with unit peak gain at F0: H(s) = (w0 / Q) s / (s^2 + (w0 / Q) s + w0^2), Q from the T60.
		FBiquad Resonator(double F0, double T60, double SampleRate)
		{
			const double W0 = 2.0 * Pi * F0;
			const double Sigma = 6.907755 / FMath::Max(T60, 1e-4);
			const double Q = FMath::Max(0.3, W0 / (2.0 * Sigma));
			return FBiquad::FromAnalog(0.0, W0 / Q, 0.0, 1.0, W0 / Q, W0 * W0, SampleRate, F0);
		}

		double PinkStep(double* B, double White)
		{
			B[0] = 0.99886 * B[0] + White * 0.0555179;
			B[1] = 0.99332 * B[1] + White * 0.0750759;
			B[2] = 0.96900 * B[2] + White * 0.1538520;
			B[3] = 0.86650 * B[3] + White * 0.3104856;
			B[4] = 0.55000 * B[4] + White * 0.5329522;
			B[5] = -0.7616 * B[5] - White * 0.0168980;
			const double Pink = B[0] + B[1] + B[2] + B[3] + B[4] + B[5] + B[6] + White * 0.5362;
			B[6] = White * 0.115926;
			return Pink * 0.11;
		}
	}

	void FShapedNoise::Initialize(ENoiseKind InKind, double SampleRate, uint64 Seed)
	{
		Kind = InKind;
		Rng.Reseed(Seed);
		double Lo, Hi;
		BandFor(Kind, Lo, Hi);
		FBiquad::ButterBandPass2(Lo, Hi, SampleRate, A, B);
		Norm = CachedUnitRmsNorm(Kind, A, B, SampleRate);
	}

	void FShapedNoise::Prewarm(double SampleRate)
	{
		for (const ENoiseKind Kind : {ENoiseKind::RollingCloth, ENoiseKind::SlidingCloth, ENoiseKind::GullyRun, ENoiseKind::RollingFloor})
		{
			FShapedNoise Noise;
			Noise.Initialize(Kind, SampleRate, 1);
		}
	}

	double GullyBumps(double T)
	{
		const double S = std::sin(2.0 * Pi * (0.8 / 0.06) * T);
		return 1.0 + 0.6 * std::pow(FMath::Max(0.0, S), 8.0);
	}

	double GullyEnvelope(double T, double Duration)
	{
		if (T < 0.0 || T > Duration)
		{
			return 0.0;
		}
		return FMath::Min(1.0, T / 0.05) * FMath::Min(1.0, (Duration - T) / 0.08);
	}

	const TCHAR* ToString(EFloorSurface Surface)
	{
		switch (Surface)
		{
		case EFloorSurface::Concrete: return TEXT("Concrete");
		case EFloorSurface::Vct: return TEXT("Vct");
		case EFloorSurface::Rubber: return TEXT("Rubber");
		case EFloorSurface::Wood: return TEXT("Wood");
		default: return TEXT("?");
		}
	}

	double FFootstepSynth::NominalHeelPeakPa(EFloorSurface Surface)
	{
		switch (Surface)
		{
		case EFloorSurface::Concrete: return 0.075;  // ~71.5 dB SPL peak at 1 m
		case EFloorSurface::Rubber: return 0.022;    // dull, ~61 dB
		case EFloorSurface::Wood: return 0.065;
		case EFloorSurface::Vct:
		default: return 0.060;                       // ~69.5 dB
		}
	}

	void FFootstepSynth::Render(const FFootstepParams& P, double SampleRate, TArray<float>& Out)
	{
		struct FMode
		{
			double F;
			double T60;
			double Gain;
		};
		static const FMode Concrete[] = {{95.0, 0.06, 1.0}, {820.0, 0.020, 0.60}, {2300.0, 0.012, 0.45}, {5200.0, 0.008, 0.25}};
		static const FMode Vct[] = {{110.0, 0.07, 1.0}, {640.0, 0.025, 0.55}, {1850.0, 0.015, 0.40}, {4300.0, 0.009, 0.20}};
		static const FMode Rubber[] = {{80.0, 0.05, 1.0}, {380.0, 0.02, 0.25}};
		static const FMode Wood[] = {{140.0, 0.09, 1.0}, {430.0, 0.05, 0.70}, {1200.0, 0.02, 0.35}, {3100.0, 0.010, 0.15}};
		TConstArrayView<FMode> Modes;
		double HeelT = 0.004;
		double ScuffLevel = 0.10;
		switch (P.Surface)
		{
		case EFloorSurface::Concrete: Modes = Concrete; HeelT = 0.003; ScuffLevel = 0.12; break;
		case EFloorSurface::Rubber: Modes = Rubber; HeelT = 0.009; ScuffLevel = 0.0; break;
		case EFloorSurface::Wood: Modes = Wood; HeelT = 0.005; ScuffLevel = 0.08; break;
		case EFloorSurface::Vct:
		default: Modes = Vct; HeelT = 0.004; ScuffLevel = 0.10; break;
		}
		FNoise Rng(HashMix(P.Seed, static_cast<uint64>(P.Surface) * 7919u + (P.bLeftFoot ? 1u : 0u)));
		const int32 N = static_cast<int32>(0.35 * SampleRate);
		TArray<double> Force;
		Force.SetNumZeroed(N);
		auto AddPulse = [&](double Start, double T, double Amp)
		{
			const int32 I0 = static_cast<int32>(Start * SampleRate);
			const int32 Len = FMath::Max(2, static_cast<int32>(T * SampleRate));
			for (int32 I = 0; I < Len && I0 + I < N; ++I)
			{
				Force[I0 + I] += Amp * std::pow(std::sin(Pi * (I + 0.5) / Len), 1.5);
			}
		};
		const double Heel = 0.004 + Rng.Uniform(0.0, 0.003);
		const double HeelDur = HeelT * Rng.Uniform(0.85, 1.15);
		const double ToeDelay = Rng.Uniform(0.085, 0.14);
		AddPulse(Heel, HeelDur, 1.0);
		AddPulse(Heel + ToeDelay, 0.7 * HeelDur, 0.45 * Rng.Uniform(0.8, 1.2));

		TArray<double> Y;
		Y.SetNumZeroed(N);
		for (const FMode& M : Modes)
		{
			FBiquad R = Resonator(M.F * Rng.Uniform(0.93, 1.07), M.T60 * Rng.Uniform(0.85, 1.15), SampleRate);
			const double G = M.Gain * Rng.Uniform(0.8, 1.2);
			for (int32 I = 0; I < N; ++I)
			{
				Y[I] += G * R.Process(Force[I]);
			}
		}
		if (ScuffLevel > 0.0)
		{
			// Sole scuff: band noise 0.9-6 kHz decaying over 20-35 ms after the heel (and a shorter one at the toe).
			FBiquad A, B;
			FBiquad::ButterBandPass2(900.0, 6000.0, SampleRate, A, B);
			const double Tau = Rng.Uniform(0.020, 0.035) / 3.0;
			for (int32 I = 0; I < N; ++I)
			{
				const double T = I / SampleRate;
				double Env = 0.0;
				if (T >= Heel)
				{
					Env += std::exp(-(T - Heel) / Tau);
				}
				if (T >= Heel + ToeDelay)
				{
					Env += 0.5 * std::exp(-(T - Heel - ToeDelay) / (0.6 * Tau));
				}
				Y[I] += ScuffLevel * Env * B.Process(A.Process(Rng.Gauss()));
			}
		}
		if (P.bOwnSteps)
		{
			FBiquad Lp = FBiquad::ButterLowPass2(200.0, SampleRate);
			for (int32 I = 0; I < N; ++I)
			{
				Y[I] += 0.41 * Lp.Process(Y[I]);
			}
		}
		double Peak = 0.0;
		for (double V : Y)
		{
			Peak = FMath::Max(Peak, FMath::Abs(V));
		}
		const double Target = NominalHeelPeakPa(P.Surface) * std::pow(FMath::Clamp(P.SpeedMps, 0.2, 3.0) / 1.4, 0.8) * Rng.Uniform(0.85, 1.15);
		const double Scale = Peak > 0.0 ? Target / Peak : 0.0;
		Out.SetNumUninitialized(N);
		for (int32 I = 0; I < N; ++I)
		{
			Out[I] = static_cast<float>(Y[I] * Scale);
		}
	}

	const TCHAR* ToString(EAmbienceLayer Layer)
	{
		switch (Layer)
		{
		case EAmbienceLayer::HvacBed: return TEXT("HvacBed");
		case EAmbienceLayer::HvacDiffuser: return TEXT("HvacDiffuser");
		case EAmbienceLayer::Compressor: return TEXT("Compressor");
		case EAmbienceLayer::NeonHum: return TEXT("NeonHum");
		default: return TEXT("?");
		}
	}

	void FAmbienceSynth::Initialize(const FAmbienceLayerDesc& InDesc, double InSampleRate, double KnownCalibration)
	{
		Desc = InDesc;
		SampleRate = InSampleRate;
		// Calibration pass: 3 s of the steady state at Calibration 1, A-weighted, then everything is reset.
		auto Reset = [this]()
		{
			FramesRendered = 0;
			Rng.Reseed(HashMix(Desc.Seed, 0xA11CEull));
			RngB.Reseed(HashMix(Desc.Seed, 0xB0B0ull));
			FMemory::Memzero(Pink, sizeof(Pink));
			Brown[0] = Brown[1] = 0.0;
			Drift = DriftTarget = DriftRate = 0.0;
			NextDriftFrame = 0;
			MotorPhase = 0.0;
			SizzleEnv = 0.0;
			TransientSeconds = -1.0;
			RunLevel = 1.0;
			bOn = true;
			StateEndSeconds = 1e30;
			for (int32 Ch = 0; Ch < 2; ++Ch)
			{
				for (FBiquad& Q : Band[Ch])
				{
					Q.Reset();
				}
			}
			switch (Desc.Layer)
			{
			case EAmbienceLayer::HvacBed:
				for (int32 Ch = 0; Ch < 2; ++Ch)
				{
					Band[Ch][0] = FBiquad::ButterLowPass2(1400.0, SampleRate);    // air
					Band[Ch][1] = FBiquad::ButterHighPass2(22.0, SampleRate);     // rumble band
					Band[Ch][2] = FBiquad::ButterLowPass2(140.0, SampleRate);
				}
				break;
			case EAmbienceLayer::HvacDiffuser:
				Band[0][0] = FBiquad::ButterHighPass2(350.0, SampleRate);
				Band[0][1] = FBiquad::ButterLowPass2(5000.0, SampleRate);
				break;
			case EAmbienceLayer::Compressor:
				Band[0][0] = FBiquad::ButterHighPass2(250.0, SampleRate);
				Band[0][1] = FBiquad::ButterLowPass2(2500.0, SampleRate);
				break;
			case EAmbienceLayer::NeonHum:
				Band[0][0] = FBiquad::ButterHighPass2(3000.0, SampleRate);
				Band[0][1] = FBiquad::ButterLowPass2(9000.0, SampleRate);
				break;
			}
			// Harmonic families.
			NumHarmonics = 0;
			FNoise HRng(HashMix(Desc.Seed, 0x4A12ull));
			if (Desc.Layer == EAmbienceLayer::Compressor || Desc.Layer == EAmbienceLayer::NeonHum)
			{
				const bool bNeon = Desc.Layer == EAmbienceLayer::NeonHum;
				const double F0 = bNeon ? 2.0 * Desc.MainsHz : Desc.MainsHz;
				static const double CompressorGains[] = {1.0, 0.8, 0.55, 0.4, 0.28, 0.2, 0.12, 0.08};
				const int32 Count = bNeon ? FMath::Min(MaxHarmonics, static_cast<int32>(3200.0 / F0)) : 8;
				for (int32 H = 0; H < Count; ++H)
				{
					const double F = F0 * (H + 1);
					const double W = 2.0 * Pi * F / SampleRate;
					const double Phase = HRng.Uniform(0.0, 2.0 * Pi);
					HRe[H] = std::cos(Phase);
					HIm[H] = std::sin(Phase);
					HCos[H] = std::cos(W);
					HSin[H] = std::sin(W);
					if (bNeon)
					{
						// Magnetostriction buzz: 1/k^1.1 with the low odd members (120 / 360 Hz) emphasised.
						HGain[H] = std::pow(H + 1.0, -1.1) * HRng.Uniform(0.5, 1.5) * ((H % 2 == 0) ? 1.3 : 1.0);
					}
					else
					{
						HGain[H] = CompressorGains[H] * HRng.Uniform(0.8, 1.2);
					}
					++NumHarmonics;
				}
			}
		};
		Reset();
		Calibration = 1.0;
		const int32 Channels = Desc.NumChannels();
		if (KnownCalibration > 0.0)
		{
			Calibration = KnownCalibration;
		}
		else
		{
		const int32 CalFrames = static_cast<int32>(3.0 * SampleRate);
		TArray<float> Tmp;
		Tmp.SetNumUninitialized(CalFrames * Channels);
		Render(Tmp.GetData(), CalFrames);
		TArray<double> Mono;
		Mono.SetNumUninitialized(CalFrames);
		double LevelSum = 0.0;
		for (int32 Ch = 0; Ch < Channels; ++Ch)
		{
			for (int32 I = 0; I < CalFrames; ++I)
			{
				Mono[I] = Tmp[I * Channels + Ch];
			}
			// Skip the first 0.5 s (filter settling).
			const int32 Skip = static_cast<int32>(0.5 * SampleRate);
			LevelSum += std::pow(10.0, LaeqDb(TConstArrayView<double>(Mono.GetData() + Skip, CalFrames - Skip), SampleRate) / 10.0);
		}
		const double Measured = 10.0 * std::log10(LevelSum / Channels);
		Calibration = std::pow(10.0, (Desc.LevelDbA - Measured) / 20.0);
		}
		Reset();
		// Compressor duty cycle: seeded start state and a random elapsed part of the first state.
		if (Desc.Layer == EAmbienceLayer::Compressor)
		{
			FNoise CRng(HashMix(Desc.Seed, 0xC0C0ull));
			bOn = Desc.bStartOn;
			const double Length = bOn ? CRng.Uniform(Desc.CycleOnSeconds[0], Desc.CycleOnSeconds[1])
				: CRng.Uniform(Desc.CycleOffSeconds[0], Desc.CycleOffSeconds[1]);
			StateEndSeconds = Length * CRng.Uniform(0.2, 1.0);
			RunLevel = bOn ? 1.0 : 0.0;
		}
	}

	void FAmbienceSynth::AdvanceFrame()
	{
		++FramesRendered;
	}

	double FAmbienceSynth::RawSample(int32 Channel)
	{
		switch (Desc.Layer)
		{
		case EAmbienceLayer::HvacBed:
		{
			FNoise& R = Channel == 0 ? Rng : RngB;
			const double White = R.Gauss();
			const double Air = Band[Channel][0].Process(PinkStep(Pink[Channel], White));
			Brown[Channel] = 0.995 * Brown[Channel] + 0.05 * R.Gauss();
			const double Rumble = Band[Channel][2].Process(Band[Channel][1].Process(Brown[Channel]));
			return (Air + 1.6 * Rumble) * std::pow(10.0, Drift / 20.0);
		}
		case EAmbienceLayer::HvacDiffuser:
		{
			const double White = Rng.Gauss();
			const double Hiss = Band[0][1].Process(Band[0][0].Process(PinkStep(Pink[0], White)));
			return Hiss * std::pow(10.0, Drift / 20.0);
		}
		case EAmbienceLayer::Compressor:
		{
			double Hum = 0.0;
			for (int32 H = 0; H < NumHarmonics; ++H)
			{
				Hum += HGain[H] * HIm[H];
			}
			const double Motor = 1.0 + 0.35 * std::sin(MotorPhase);
			const double Mech = Band[0][1].Process(Band[0][0].Process(Rng.Gauss())) * Motor;
			double Out = RunLevel * (0.5 * Hum + 0.8 * Mech);
			if (TransientSeconds >= 0.0)
			{
				// Start / stop clunk: a 65 Hz thump plus a rattle burst.
				const double T = TransientSeconds;
				const double Level = bTransientIsStart ? 3.0 : 1.8;
				Out += Level * (std::exp(-T / 0.06) * std::sin(2.0 * Pi * 65.0 * T) + 0.6 * std::exp(-T / 0.04) * RngB.Gauss());
			}
			return Out;
		}
		case EAmbienceLayer::NeonHum:
		{
			double Hum = 0.0;
			for (int32 H = 0; H < NumHarmonics; ++H)
			{
				Hum += HGain[H] * HIm[H];
			}
			// Soft saturation of the transformer buzz, then the faint electrode sizzle (sparse crackles).
			Hum = std::tanh(1.4 * Hum) / 1.4;
			const double Sizzle = SizzleEnv * Band[0][1].Process(Band[0][0].Process(Rng.Gauss()));
			return Hum * std::pow(10.0, Drift / 20.0) + 0.25 * Sizzle;
		}
		default:
			return 0.0;
		}
	}

	void FAmbienceSynth::Render(float* Out, int32 Frames)
	{
		const int32 Channels = Desc.NumChannels();
		const double Dt = 1.0 / SampleRate;
		for (int32 I = 0; I < Frames; ++I)
		{
			// Slow drift (+-1 dB, new target every 20-60 s; flicker of the neon: faster, smaller).
			if (FramesRendered >= NextDriftFrame)
			{
				const bool bNeon = Desc.Layer == EAmbienceLayer::NeonHum;
				DriftTarget = bNeon ? RngB.Uniform(-0.4, 0.4) : RngB.Uniform(-1.0, 1.0);
				const double Interval = bNeon ? RngB.Uniform(0.5, 2.0) : RngB.Uniform(20.0, 60.0);
				DriftRate = (DriftTarget - Drift) / (Interval * SampleRate);
				NextDriftFrame = FramesRendered + static_cast<int64>(Interval * SampleRate);
			}
			Drift += DriftRate;

			if (Desc.Layer == EAmbienceLayer::Compressor)
			{
				const double Now = static_cast<double>(FramesRendered) * Dt;
				if (Now >= StateEndSeconds)
				{
					FNoise CRng(HashMix(Desc.Seed, static_cast<uint64>(FramesRendered)));
					bOn = !bOn;
					StateEndSeconds = Now + (bOn ? CRng.Uniform(Desc.CycleOnSeconds[0], Desc.CycleOnSeconds[1])
						: CRng.Uniform(Desc.CycleOffSeconds[0], Desc.CycleOffSeconds[1]));
					TransientSeconds = 0.0;
					bTransientIsStart = bOn;
				}
				const double Target = bOn ? 1.0 : 0.0;
				const double RampSeconds = bOn ? 0.8 : 1.2;
				RunLevel += FMath::Clamp(Target - RunLevel, -Dt / RampSeconds, Dt / RampSeconds);
				if (TransientSeconds >= 0.0)
				{
					TransientSeconds += Dt;
					if (TransientSeconds > 0.4)
					{
						TransientSeconds = -1.0;
					}
				}
				MotorPhase += 2.0 * Pi * 29.5 * Dt;
				if (MotorPhase > 2.0 * Pi)
				{
					MotorPhase -= 2.0 * Pi;
				}
			}
			if (Desc.Layer == EAmbienceLayer::NeonHum)
			{
				// Poisson crackles, ~4 per second, 1.5 ms decay.
				SizzleEnv *= std::exp(-Dt / 0.0015);
				if (RngB.Uniform() < 4.0 * Dt)
				{
					SizzleEnv = RngB.Uniform(0.3, 1.0);
				}
			}
			for (int32 Ch = 0; Ch < Channels; ++Ch)
			{
				Out[I * Channels + Ch] = static_cast<float>(Calibration * RawSample(Ch));
			}
			// Rotate the harmonic phasors.
			for (int32 H = 0; H < NumHarmonics; ++H)
			{
				const double Re = HRe[H] * HCos[H] - HIm[H] * HSin[H];
				const double Im = HRe[H] * HSin[H] + HIm[H] * HCos[H];
				HRe[H] = Re;
				HIm[H] = Im;
			}
			AdvanceFrame();
		}
		// Renormalise the phasors (rounding drift).
		for (int32 H = 0; H < NumHarmonics; ++H)
		{
			const double Mag = std::sqrt(HRe[H] * HRe[H] + HIm[H] * HIm[H]);
			if (Mag > 0.0)
			{
				HRe[H] /= Mag;
				HIm[H] /= Mag;
			}
		}
	}
}
