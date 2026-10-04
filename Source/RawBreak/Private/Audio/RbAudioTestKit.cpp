#include "Audio/RbAudioTestKit.h"

#include "Audio/RbAudioSettings.h"

#include "GameFramework/Actor.h"
#include "Sound/SoundSubmix.h"

#include "RbAudio/RbAudioAnalysis.h"
#include "RbAudio/RbAudioMath.h"
#include "RbAudio/RbImpactSynth.h"
#include "RbAudio/RbPresentation.h"
#include "RbAudio/RbShotAudioClock.h"

#include <cmath>

// Owner: M2-C.

namespace RbAudioTestKit
{
	void DetectOnsets(TConstArrayView<double> Mono, double SampleRate, TArray<FOnset>& Out, double HighPassHz, double RiseDb, double RiseWindowMs, double MinGapMs,
		double AbsFloor)
	{
		TArray<RbAudio::FOnset> Onsets;
		RbAudio::DetectOnsets(Mono, SampleRate, Onsets, HighPassHz, RiseDb, RiseWindowMs, MinGapMs, AbsFloor);
		Out.Reset();
		for (const RbAudio::FOnset& O : Onsets)
		{
			Out.Add({O.Sample, O.LevelDb});
		}
	}

	double GroupDelaySamples(TConstArrayView<double> A, TConstArrayView<double> B, double SampleRate, double FLo, double FHi)
	{
		return RbAudio::GroupDelaySamples(A, B, SampleRate, FLo, FHi);
	}

	double TruePeakDbtp(TConstArrayView<float> Interleaved, int32 NumChannels)
	{
		return RbAudio::TruePeakDbtp(Interleaved, NumChannels);
	}

	double RmsDb(TConstArrayView<double> X, int32 Begin, int32 End)
	{
		return RbAudio::RmsDb(X, Begin, End);
	}

	double SchroederRt60(TConstArrayView<double> X, int32 Begin, int32 End, double SampleRate, double DecayDb)
	{
		return RbAudio::SchroederRt60(X, Begin, End, SampleRate, DecayDb);
	}

	double LaeqDb(TConstArrayView<double> Digital, double SampleRate, double FullScalePa)
	{
		TArray<double> Pa;
		Pa.SetNumUninitialized(Digital.Num());
		for (int32 I = 0; I < Digital.Num(); ++I)
		{
			Pa[I] = Digital[I] * FullScalePa;
		}
		return RbAudio::LaeqDb(Pa, SampleRate);
	}

	double SpectralCentroidHz(TConstArrayView<double> X, double SampleRate)
	{
		int32 N = 1;
		while (N < X.Num())
		{
			N <<= 1;
		}
		TArray<RbAudio::FComplex> Buf;
		Buf.SetNumZeroed(N);
		for (int32 I = 0; I < X.Num(); ++I)
		{
			Buf[I] = RbAudio::FComplex(X[I], 0.0);
		}
		RbAudio::Fft(Buf, false);
		double Num = 0.0;
		double Den = 0.0;
		for (int32 K = 1; K <= N / 2; ++K)
		{
			const double F = K * SampleRate / N;
			if (F < 20.0 || F > 20000.0)
			{
				continue;
			}
			const double P = std::norm(Buf[K]);
			Num += F * P;
			Den += P;
		}
		return Den > 0.0 ? Num / Den : 0.0;
	}

	double BandRmsDb(TConstArrayView<double> X, double SampleRate, double LoHz, double HiHz, int32 Begin, int32 End)
	{
		RbAudio::FBiquad H1 = RbAudio::FBiquad::ButterHighPass2(LoHz, SampleRate);
		RbAudio::FBiquad H2 = H1;
		RbAudio::FBiquad L1 = RbAudio::FBiquad::ButterLowPass2(HiHz, SampleRate);
		RbAudio::FBiquad L2 = L1;
		TArray<double> Y;
		Y.SetNumUninitialized(X.Num());
		for (int32 I = 0; I < X.Num(); ++I)
		{
			Y[I] = L2.Process(L1.Process(H2.Process(H1.Process(X[I]))));
		}
		return RbAudio::RmsDb(Y, Begin, End);
	}

	bool WriteWav(const FString& Path, TConstArrayView<float> Interleaved, int32 NumChannels, int32 SampleRate, bool bFloat32)
	{
		return RbAudio::WriteWavFile(Path, Interleaved, NumChannels, SampleRate, bFloat32);
	}

	void ExtractChannel(TConstArrayView<float> Interleaved, int32 NumChannels, int32 Channel, TArray<double>& Out)
	{
		RbAudio::ExtractChannel(Interleaved, NumChannels, Channel, Out);
	}

	const TCHAR* ImpactKindName(uint8 Kind)
	{
		return RbAudio::ToString(static_cast<RbAudio::EImpactKind>(Kind));
	}

	FRbShotAudioClockPtr MakeClock()
	{
		return MakeShared<RbAudio::FShotAudioClock, ESPMode::ThreadSafe>();
	}

	void StartShot(const FRbShotAudioClockPtr& Clock, uint64 ShotId, double OriginClock, double OriginShotTime, double Rate, double VisualLatency)
	{
		if (Clock.IsValid())
		{
			Clock->StartShot(ShotId, OriginClock, OriginShotTime, Rate, false, VisualLatency);
		}
	}

	FClockState ReadClock(const FRbShotAudioClockPtr& Clock)
	{
		FClockState S;
		if (!Clock.IsValid())
		{
			return S;
		}
		const RbAudio::FShotClockSnapshot Snap = Clock->Snapshot();
		S.ShotId = Snap.ShotId;
		S.Generation = Snap.Generation;
		S.bAnchored = Snap.bAnchored;
		S.bRunning = Snap.bRunning;
		S.bHeld = Snap.bHeld;
		S.AnchorFrame = Snap.AnchorFrame;
		S.OriginShotTime = Snap.OriginShotTime;
		S.Rate = Snap.Rate;
		S.LastLeadFrames = Clock->GetLastLeadFrames();
		return S;
	}

	namespace
	{
		RbAudio::FImpactEvent ClickEvent(double SampleRate)
		{
			using namespace RbAudio;
			FBallAcoustics Std;
			Std.Material = PhenolicMaterial();
			const double L[3] = {-1.0, 0.0, Std.Radius}; // on the line of centres, 1 m behind, at the ball's height
			TArray<FImpactEvent> Events;
			MakeBallBallRuntimeEvents(1.0, Std, Std, L, SampleRate, Events);
			FImpactEvent E = Events[0];
			E.NumPaths = 1;              // direct path only
			E.Paths[0].DelaySeconds = 0.0; // no propagation: the event frame is the contact start
			return E;
		}
	}

	FRbVoicePlanPtr MakeClickPlan(uint64 ShotId, TConstArrayView<double> ShotTimes, double SampleRate, double OutputGain)
	{
		TSharedPtr<RbAudio::FVoicePlan, ESPMode::ThreadSafe> Plan = MakeShared<RbAudio::FVoicePlan, ESPMode::ThreadSafe>();
		Plan->ShotId = ShotId;
		Plan->Seed = ShotId;
		Plan->OutputGain = OutputGain;
		const RbAudio::FImpactEvent Click = ClickEvent(SampleRate);
		for (const double T : ShotTimes)
		{
			RbAudio::FImpactEvent E = Click;
			E.ShotTime = T;
			Plan->Impacts.Add(E);
		}
		return Plan;
	}

	void RenderClick(double EventFrame, int32 NumOut, double SampleRate, TArray<double>& Out)
	{
		Out.SetNumZeroed(NumOut);
		RbAudio::FImpactRenderer Renderer(SampleRate);
		Renderer.Render(ClickEvent(SampleRate), EventFrame, 0, TArrayView<double>(Out));
	}

	URbImpactVoiceComponent* SpawnTestVoice(AActor* Owner, USoundSubmix* Submix, int32 Channel, const FRbShotAudioClockPtr& Clock, FName Name,
		USoundSubmix* ReverbSubmix, float ReverbSend, double RefDistance)
	{
		if (!Owner)
		{
			return nullptr;
		}
		URbImpactVoiceComponent* V = NewObject<URbImpactVoiceComponent>(Owner, Name, RF_Transient);
		V->SetupAttachment(Owner->GetRootComponent());
		V->ConfigureVoice(Channel == -1, Submix, ReverbSubmix, ReverbSend, RefDistance, 60.0);
		if (Channel >= 0)
		{
			V->SetTestChannel(Channel);
		}
		V->SetClock(Clock);
		V->RegisterComponent();
		V->StartVoice();
		return V;
	}

	FString DescribeVoice(const URbImpactVoiceComponent* Voice)
	{
		if (!Voice)
		{
			return TEXT("(null voice)");
		}
		return FString::Printf(TEXT("%s: registered %d, active %d, playing %d, generator %d, blocks %lld, last block frame %lld"), *Voice->GetName(),
			Voice->IsRegistered() ? 1 : 0, Voice->IsActive() ? 1 : 0, Voice->IsPlaying() ? 1 : 0, Voice->IsRendering() ? 1 : 0,
			Voice->GetShared()->BlocksRendered.load(), Voice->GetShared()->LastBlockFrame.load());
	}

	double WavChannelEnergy(const FString& Path, int32 Channel)
	{
		TArray<float> X;
		int32 Channels = 0;
		int32 Rate = 0;
		if (!RbAudio::ReadWavFile(Path, X, Channels, Rate) || Channel < 0 || Channel >= Channels)
		{
			return -1.0;
		}
		double E = 0.0;
		for (int32 I = Channel; I < X.Num(); I += Channels)
		{
			E += static_cast<double>(X[I]) * X[I];
		}
		return E;
	}

	double FullScalePa()
	{
		return RbAudio::GetPresentationMode(static_cast<RbAudio::EDynamicRangeMode>(URbAudioSettings::Get()->DynamicRange)).FullScalePa();
	}
}
