#include "RbAudio/RbImpactSynth.h"

#include "Misc/ScopeLock.h"

#include <cmath>

// Owner: M2-C. Port of click_synth.py runtime_render (ball part) and a time-domain form of piston_transfer (banks).

namespace RbAudio
{
	const TCHAR* ToString(EImpactKind Kind)
	{
		switch (Kind)
		{
		case EImpactKind::BallBall: return TEXT("BallBall");
		case EImpactKind::BallCushion: return TEXT("BallCushion");
		case EImpactKind::BallJaw: return TEXT("BallJaw");
		case EImpactKind::BallRailTop: return TEXT("BallRailTop");
		case EImpactKind::BallSlate: return TEXT("BallSlate");
		case EImpactKind::BallLiner: return TEXT("BallLiner");
		case EImpactKind::PocketDrop: return TEXT("PocketDrop");
		case EImpactKind::TrapClick: return TEXT("TrapClick");
		case EImpactKind::TipStrike: return TEXT("TipStrike");
		case EImpactKind::Miscue: return TEXT("Miscue");
		case EImpactKind::TipRecontact: return TEXT("TipRecontact");
		case EImpactKind::FloorHit: return TEXT("FloorHit");
		case EImpactKind::Footstep: return TEXT("Footstep");
		default: return TEXT("?");
		}
	}

	FReflection FReflection::Cloth(double SampleRate)
	{
		// bilinear([0.9], [1 / (2 pi fc), 1], fs): b = 0.9 wc / (2 fs + wc) [1, 1], a = [1, (wc - 2 fs) / (2 fs + wc)].
		const double Wc = 2.0 * Pi * ClothCornerHz;
		const double K = 2.0 * SampleRate;
		FReflection R;
		R.B0 = ClothReflection * Wc / (K + Wc);
		R.B1 = R.B0;
		R.A1 = (Wc - K) / (K + Wc);
		return R;
	}

	namespace
	{
		// click_synth.py (f Hz, modal mass kg, loss factor, area m^2); ESTIMATE, audio.md 3.5.
		const FModalMode RailBarBoxModes[] = {
			{82.0, 60.0, 0.15, 0.60}, {145.0, 15.0, 0.10, 0.25}, {240.0, 8.0, 0.08, 0.08}, {390.0, 8.0, 0.08, 0.08},
			{610.0, 6.0, 0.09, 0.06}, {950.0, 4.0, 0.10, 0.04}, {1500.0, 3.0, 0.12, 0.03}};
		const FModalMode RailProModes[] = {
			{95.0, 150.0, 0.18, 0.6}, {180.0, 20.0, 0.12, 0.2}, {300.0, 10.0, 0.08, 0.08}, {520.0, 8.0, 0.08, 0.06},
			{830.0, 6.0, 0.09, 0.04}, {1300.0, 4.0, 0.11, 0.03}};
		const FModalMode BedModes[] = {{95.0, 150.0, 0.15, 1.0}, {180.0, 60.0, 0.12, 0.5}, {310.0, 30.0, 0.10, 0.3}};
		const FModalMode PocketModes[] = {{160.0, 1.5, 0.25, 0.02}, {420.0, 0.8, 0.20, 0.01}, {900.0, 0.5, 0.25, 0.005}};
		const FModalMode CueModes[] = {{330.0, 0.25, 0.08, 2e-5}, {870.0, 0.20, 0.08, 1.5e-5}, {1520.0, 0.30, 0.06, 3e-4}, {2600.0, 0.15, 0.08, 1e-5}};
		const FModalMode CabinetModes[] = {{82.0, 60.0, 0.15, 0.60}, {145.0, 15.0, 0.10, 0.25}};
	}

	TConstArrayView<FModalMode> GetModalModes(EModalBank Bank, double& OutFc, bool& bOutBaffled)
	{
		bOutBaffled = true;
		switch (Bank)
		{
		case EModalBank::RailBarBox: OutFc = 400.0; return RailBarBoxModes;
		case EModalBank::RailPro: OutFc = 400.0; return RailProModes;
		case EModalBank::Bed: OutFc = 150.0; return BedModes;
		case EModalBank::Pocket: OutFc = 300.0; return PocketModes;
		case EModalBank::Cue: OutFc = 0.0; bOutBaffled = false; return CueModes;
		case EModalBank::Cabinet: OutFc = 400.0; return CabinetModes;
		default: OutFc = 0.0; return TConstArrayView<FModalMode>();
		}
	}

	FModalBankDesignPtr GetModalBankDesign(EModalBank Bank, double SampleRate)
	{
		if (Bank == EModalBank::None || Bank == EModalBank::Count)
		{
			return nullptr;
		}
		static FCriticalSection Lock;
		static TMap<int64, FModalBankDesignPtr> Cache;
		const int64 Key = FMath::RoundToInt64(SampleRate) * 16 + static_cast<int64>(Bank);
		FScopeLock Guard(&Lock);
		if (const FModalBankDesignPtr* Found = Cache.Find(Key))
		{
			return *Found;
		}
		TSharedPtr<FModalBankDesign, ESPMode::ThreadSafe> D = MakeShared<FModalBankDesign, ESPMode::ThreadSafe>();
		D->Bank = Bank;
		D->SampleRate = SampleRate;
		double Fc = 0.0;
		bool bBaffled = true;
		const TConstArrayView<FModalMode> Modes = GetModalModes(Bank, Fc, bBaffled);
		double LongestTail = 0.0;
		for (const FModalMode& M : Modes)
		{
			const double Wn = 2.0 * Pi * M.FrequencyHz;
			// acc / F = -(1/M) s^2 / (s^2 + eta wn s + wn^2); p = rho0 S acc / (2 pi) at 1 m (4 pi unbaffled).
			const double Radiation = Rho0 * M.Area / ((bBaffled ? 2.0 : 4.0) * Pi);
			FBiquad Q = FBiquad::FromAnalog(-Radiation / M.ModalMass, 0.0, 0.0, 1.0, M.LossFactor * Wn, Wn * Wn, SampleRate, M.FrequencyHz);
			D->Modes.Add(Q);
			const double Sigma = 0.5 * M.LossFactor * Wn;
			LongestTail = FMath::Max(LongestTail, std::log(1.0e4) / Sigma);
		}
		if (Fc > 0.0)
		{
			const double Wc = 2.0 * Pi * Fc;
			D->HighPass = FBiquad::FromAnalog(1.0, 0.0, 0.0, 1.0, 2.0 * Wc, Wc * Wc, SampleRate, Fc);
			D->bHighPass = true;
		}
		D->TailSamples = static_cast<int32>(std::ceil(LongestTail * SampleRate)) + 64;
		Cache.Add(Key, D);
		return D;
	}

	void FImpactRenderer::Initialize(double InSampleRate)
	{
		SampleRate = InSampleRate;
		DecimationFir(SampleRate);
		for (int32 B = 0; B < static_cast<int32>(EModalBank::Count); ++B)
		{
			Banks[B] = GetModalBankDesign(static_cast<EModalBank>(B), SampleRate);
		}
		ScratchG.Reserve(FBallKernels::MaxPulseSamples);
		ScratchKernel.Reserve(KernelTaps + FBallKernels::MaxPulseSamples);
		ScratchOut.Reserve(KernelTaps + FBallKernels::MaxPulseSamples + 16384);
	}

	void FImpactRenderer::ComputeForce(const FImpactEvent& Event, double Frac, TArray<double>& OutG) const
	{
		if (Event.Pulse == EPulseShape::Hertz && Event.Shape.IsValid())
		{
			RuntimeForceHertz(*Event.Shape, Event.ContactTime, Event.PeakForce, Frac, SampleRate, OutG);
		}
		else
		{
			RuntimeForceSine(Event.Impulse, Event.ContactTime, Frac, SampleRate, OutG);
		}
		if (OutG.Num() > FBallKernels::MaxPulseSamples)
		{
			OutG.SetNum(FBallKernels::MaxPulseSamples);
		}
	}

	void FImpactRenderer::Extent(const FImpactEvent& Event, int32& OutFirst, int32& OutEnd) const
	{
		const int32 PulseSamples = FMath::Min(static_cast<int32>(std::ceil(Event.ContactTime * SampleRate)) + 34, FBallKernels::MaxPulseSamples);
		int32 First = 0;
		int32 End = 0;
		bool bAny = false;
		if (Event.Kernels.IsValid())
		{
			for (int32 P = 0; P < Event.NumPaths; ++P)
			{
				const int32 Delay = static_cast<int32>(std::floor(Event.Paths[P].DelaySeconds * SampleRate));
				const int32 F = Delay - RuntimeLatency - 1;
				const int32 E = Delay - RuntimeLatency + PulseSamples + KernelTaps + 1;
				First = bAny ? FMath::Min(First, F) : F;
				End = bAny ? FMath::Max(End, E) : E;
				bAny = true;
			}
		}
		const FModalBankDesignPtr& Bank = Banks[static_cast<int32>(Event.Bank)];
		if (Bank.IsValid())
		{
			const int32 Delay = static_cast<int32>(std::floor(Event.BankDelaySeconds * SampleRate));
			const int32 F = Delay - DecimationTaps / 2 / DecimationFactor - 1;
			const int32 E = Delay + PulseSamples + Bank->TailSamples + 1;
			First = bAny ? FMath::Min(First, F) : F;
			End = bAny ? FMath::Max(End, E) : E;
			bAny = true;
		}
		OutFirst = First;
		OutEnd = End;
	}

	int32 FImpactRenderer::Render(const FImpactEvent& Event, double EventFrame, int64 BufferStartFrame, TArrayView<float> Out) const
	{
		return RenderImpl(Event, EventFrame, BufferStartFrame, Out);
	}

	int32 FImpactRenderer::Render(const FImpactEvent& Event, double EventFrame, int64 BufferStartFrame, TArrayView<double> Out) const
	{
		return RenderImpl(Event, EventFrame, BufferStartFrame, Out);
	}

	template <typename SampleType>
	int32 FImpactRenderer::RenderImpl(const FImpactEvent& Event, double EventFrame, int64 BufferStartFrame, TArrayView<SampleType> Out) const
	{
		int32 Written = 0;
		const int32 OutNum = Out.Num();
		auto AddSegment = [&](int64 FirstFrame, const TArray<double>& Seg, int32 Count)
		{
			const int64 Offset = FirstFrame - BufferStartFrame;
			const int32 Lo = static_cast<int32>(FMath::Clamp<int64>(-Offset, 0, Count));
			const int32 Hi = static_cast<int32>(FMath::Clamp<int64>(OutNum - Offset, 0, Count));
			for (int32 J = Lo; J < Hi; ++J)
			{
				Out[static_cast<int32>(Offset + J)] += static_cast<SampleType>(Seg[J]);
			}
			Written += FMath::Max(0, Hi - Lo);
		};
		auto ApplyLowPass = [&](TArray<double>& Seg, int32 Count)
		{
			if (Event.LowPassHz > 0.0)
			{
				FBiquad Lp = FBiquad::OnePoleLowPass(Event.LowPassHz, SampleRate);
				for (int32 J = 0; J < Count; ++J)
				{
					Seg[J] = Lp.Process(Seg[J]);
				}
			}
		};

		// --- ball radiation, per path (audio.md 3.6 steps 0-5) -------------------------------------------------------------
		if (Event.Kernels.IsValid() && Event.NumPaths > 0)
		{
			const FBallKernels& K = *Event.Kernels;
			for (int32 P = 0; P < FMath::Min(Event.NumPaths, 2); ++P)
			{
				const FRadiationPath& Path = Event.Paths[P];
				const double S = EventFrame + Path.DelaySeconds * SampleRate;
				const double IFloor = std::floor(S);
				const double Frac = S - IFloor;
				ComputeForce(Event, Frac, ScratchG);
				const int32 Lg = ScratchG.Num();
				if (Lg == 0)
				{
					continue;
				}
				const int32 Lk = KernelTaps + Lg - 1;
				ScratchKernel.SetNumUninitialized(Lk, EAllowShrinking::No);
				double* Kc = ScratchKernel.GetData();
				FMemory::Memzero(Kc, sizeof(double) * Lk);
				for (int32 N = 0; N <= 3; ++N)
				{
					if (!((K.OrderMask >> N) & 1u) || Path.Weights[N] == 0.0)
					{
						continue;
					}
					const double W = Path.Weights[N];
					const double* H = K.Orders[N].GetData();
					for (int32 M = 0; M < KernelTaps; ++M)
					{
						Kc[M] += W * H[M];
					}
				}
				const double Nf = Path.NearField * Path.Weights[1];
				if (Nf != 0.0)
				{
					const double* L1 = K.LeakyOrder1.GetData();
					for (int32 M = 0; M < Lk; ++M)
					{
						Kc[M] += Nf * L1[M];
					}
				}
				ScratchOut.SetNumUninitialized(Lk, EAllowShrinking::No);
				double* Sv = ScratchOut.GetData();
				FMemory::Memzero(Sv, sizeof(double) * Lk);
				const double* G = ScratchG.GetData();
				for (int32 M = 0; M < Lg; ++M)
				{
					const double Gm = G[M];
					if (Gm == 0.0)
					{
						continue;
					}
					double* Dst = Sv + M;
					const int32 Count = Lk - M;
					for (int32 Q = 0; Q < Count; ++Q)
					{
						Dst[Q] += Gm * Kc[Q];
					}
				}
				const double Gain = Path.Gain * Event.Gain;
				if (Path.bReflected)
				{
					const FReflection& R = Event.Reflection;
					double X1 = 0.0;
					double Y1 = 0.0;
					for (int32 J = 0; J < Lk; ++J)
					{
						const double X = Sv[J];
						const double Y = R.B0 * X + R.B1 * X1 - R.A1 * Y1;
						X1 = X;
						Y1 = Y;
						Sv[J] = Y * Gain;
					}
				}
				else
				{
					for (int32 J = 0; J < Lk; ++J)
					{
						Sv[J] *= Gain;
					}
				}
				ApplyLowPass(ScratchOut, Lk);
				AddSegment(static_cast<int64>(IFloor) - RuntimeLatency, ScratchOut, Lk);
			}
		}

		// --- structural bank ---------------------------------------------------------------------------------------------
		const FModalBankDesignPtr& BankPtr = Banks[static_cast<int32>(Event.Bank)];
		if (BankPtr.IsValid() && Event.BankGain != 0.0)
		{
			const FModalBankDesign& Bank = *BankPtr;
			const double S = EventFrame + Event.BankDelaySeconds * SampleRate;
			const double IFloor = std::floor(S);
			ComputeForce(Event, S - IFloor, ScratchG);
			const int32 Lg = ScratchG.Num();
			const int32 Count = Lg + Bank.TailSamples;
			ScratchOut.SetNumUninitialized(Count, EAllowShrinking::No);
			double* Sv = ScratchOut.GetData();
			FMemory::Memzero(Sv, sizeof(double) * Count);
			for (const FBiquad& ModeDesign : Bank.Modes)
			{
				FBiquad Mode = ModeDesign;
				for (int32 J = 0; J < Count; ++J)
				{
					Sv[J] += Mode.Process(J < Lg ? ScratchG[J] : 0.0);
				}
			}
			const double Gain = Event.BankGain * Event.Gain;
			if (Bank.bHighPass)
			{
				FBiquad Hp = Bank.HighPass;
				for (int32 J = 0; J < Count; ++J)
				{
					Sv[J] = Hp.Process(Sv[J]) * Gain;
				}
			}
			else
			{
				for (int32 J = 0; J < Count; ++J)
				{
					Sv[J] *= Gain;
				}
			}
			ApplyLowPass(ScratchOut, Count);
			AddSegment(static_cast<int64>(IFloor) - DecimationTaps / 2 / DecimationFactor, ScratchOut, Count);
		}
		return Written;
	}

	template int32 FImpactRenderer::RenderImpl<float>(const FImpactEvent&, double, int64, TArrayView<float>) const;
	template int32 FImpactRenderer::RenderImpl<double>(const FImpactEvent&, double, int64, TArrayView<double>) const;
}
