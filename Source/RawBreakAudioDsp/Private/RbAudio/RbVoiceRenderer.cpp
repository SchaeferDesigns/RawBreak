#include "RbAudio/RbVoiceRenderer.h"

#include "HAL/PlatformTime.h"

#include <cmath>

// Owner: M2-C.

namespace RbAudio
{
	namespace
	{
		int32 KindIndex(ENoiseKind Kind)
		{
			return FMath::Clamp(static_cast<int32>(Kind), 0, 3);
		}
	}

	void FVoiceRenderer::Initialize(double InSampleRate, int32 InBlockFrames, int32 RingFrames)
	{
		SampleRate = InSampleRate;
		BlockFrames = FMath::Max(16, InBlockFrames);
		const int32 Size = FMath::RoundUpToPowerOfTwo(FMath::Max(RingFrames, 4 * BlockFrames));
		Ring.Reset();
		Ring.SetNumZeroed(Size);
		RingMask = Size - 1;
		Scratch.Reset();
		Scratch.SetNumZeroed(Size);
		RingValidFrom = 0;
		Renderer.Initialize(SampleRate);
		ContinuousSmooth = 1.0 - std::exp(-1.0 / (0.005 * SampleRate));
		HoldFade = 1.0 - std::exp(-1.0 / (0.020 * SampleRate));
		Plan.Reset();
		bPlanStarted = false;
		NextImpact = 0;
		Stats = FVoiceStats();
	}

	void FVoiceRenderer::SetPlan(FVoicePlanPtr InPlan)
	{
		Plan = MoveTemp(InPlan);
		bPlanStarted = false;
		NextImpact = 0;
		ContinuousCursor = 0;
		RenderedUpTo = -1e30;
		if (Plan.IsValid())
		{
			for (const FContinuousSegment& S : Plan->Continuous)
			{
				EnsureNoise(S.Kind);
			}
		}
	}

	void FVoiceRenderer::EnsureNoise(ENoiseKind Kind)
	{
		const int32 K = KindIndex(Kind);
		if (!bNoiseReady[K])
		{
			const uint64 Seed = Plan.IsValid() ? HashMix(Plan->Seed, static_cast<uint64>(K) + 1) : static_cast<uint64>(K) + 17;
			Noise[K].Initialize(Kind, SampleRate, Seed);
			bNoiseReady[K] = true;
		}
	}

	void FVoiceRenderer::AddLivePcm(TConstArrayView<float> Pcm, int32 DelayFrames)
	{
		FLivePcm& L = PendingPcm.AddDefaulted_GetRef();
		L.Samples = TArray<float>(Pcm.GetData(), Pcm.Num());
		L.DelayFrames = FMath::Max(0, DelayFrames);
	}

	void FVoiceRenderer::SetLiveContinuous(ENoiseKind Kind, double SpeedMps, double GainPerMps)
	{
		LiveKind = Kind;
		LiveTargetSpeed = FMath::Max(0.0, SpeedMps);
		LiveGainPerMps = GainPerMps;
		if (LiveTargetSpeed > 0.0)
		{
			EnsureNoise(Kind);
		}
	}

	void FVoiceRenderer::RenderImpactIntoRing(const FImpactEvent& Event, double EventFrame, int64 BlockStartFrame)
	{
		int32 First = 0;
		int32 End = 0;
		Renderer.Extent(Event, First, End);
		const int64 Lo = static_cast<int64>(std::floor(EventFrame)) + First;
		const int64 Hi = static_cast<int64>(std::floor(EventFrame)) + End;
		const int64 RingSize = static_cast<int64>(Ring.Num());
		const int64 ClipLo = FMath::Max(Lo, BlockStartFrame);
		const int64 ClipHi = FMath::Min(Hi, BlockStartFrame + RingSize);
		if (Lo < BlockStartFrame)
		{
			++Stats.LateImpacts;
		}
		if (ClipHi <= ClipLo)
		{
			return;
		}
		const int32 Count = static_cast<int32>(ClipHi - ClipLo);
		FMemory::Memzero(Scratch.GetData(), sizeof(float) * Count);
		Renderer.Render(Event, EventFrame, ClipLo, TArrayView<float>(Scratch.GetData(), Count));
		for (int32 J = 0; J < Count; ++J)
		{
			Ring[static_cast<int32>((ClipLo + J) & RingMask)] += Scratch[J];
		}
		++Stats.RenderedImpacts;
	}

	void FVoiceRenderer::ScheduleImpacts(const FShotClockSnapshot& Snap, int64 BlockStartFrame, int32 Frames)
	{
		const TArray<FImpactEvent>& Impacts = Plan->Impacts;
		const double Horizon = static_cast<double>(BlockStartFrame + Frames + BlockFrames);
		while (NextImpact < Impacts.Num())
		{
			const FImpactEvent& E = Impacts[NextImpact];
			double Frame = 0.0;
			if (!Snap.ShotTimeToDeviceFrame(E.ShotTime, Frame))
			{
				return;
			}
			int32 First = 0;
			int32 End = 0;
			Renderer.Extent(E, First, End);
			if (Frame + First >= Horizon)
			{
				return;
			}
			RenderImpactIntoRing(E, Frame, BlockStartFrame);
			RenderedUpTo = E.ShotTime;
			++NextImpact;
		}
	}

	double FVoiceRenderer::ContinuousSample(const FShotClockSnapshot& Snap, double ShotTime, bool bActive)
	{
		double Target[4] = {0.0, 0.0, 0.0, 0.0};
		const bool bPlaying = bActive && Snap.IsPlaying();
		if (bPlaying && Plan.IsValid() && Plan->Continuous.Num() > 0)
		{
			const TArray<FContinuousSegment>& Segs = Plan->Continuous;
			while (ContinuousCursor < Segs.Num() && Segs[ContinuousCursor].EndTime < ShotTime && ContinuousCursor < Segs.Num())
			{
				// Only skip a segment that no later segment overlaps backwards (segments are sorted by start).
				++ContinuousCursor;
			}
			for (int32 I = FMath::Max(0, ContinuousCursor - 4); I < Segs.Num(); ++I)
			{
				const FContinuousSegment& S = Segs[I];
				if (S.StartTime > ShotTime)
				{
					break;
				}
				if (ShotTime > S.EndTime)
				{
					continue;
				}
				const int32 K = KindIndex(S.Kind);
				if (S.Kind == ENoiseKind::GullyRun)
				{
					const double Local = ShotTime - S.StartTime;
					Target[K] += S.Gain * GullyBumps(Local) * GullyEnvelope(Local, S.EndTime - S.StartTime);
				}
				else
				{
					Target[K] += S.Gain * S.SpeedAt(ShotTime);
				}
			}
		}
		const double Coef = bPlaying ? ContinuousSmooth : HoldFade;
		double Sum = 0.0;
		for (int32 K = 0; K < 4; ++K)
		{
			ContinuousLevel[K] += (Target[K] - ContinuousLevel[K]) * Coef;
			if (ContinuousLevel[K] > 1e-12 && bNoiseReady[K])
			{
				Sum += ContinuousLevel[K] * Noise[K].Next();
			}
			else if (Target[K] == 0.0)
			{
				ContinuousLevel[K] = 0.0;
			}
		}
		return Sum;
	}

	void FVoiceRenderer::RenderBlock(const FShotAudioClock* Clock, int64 BlockStartFrame, TArrayView<float> Out)
	{
		const uint64 Start = FPlatformTime::Cycles64();
		const int32 Frames = Out.Num();
		const int64 RingSize = static_cast<int64>(Ring.Num());

		// Frames skipped since the last block (a source that was not rendered): their ring content is stale.
		if (BlockStartFrame > RingValidFrom)
		{
			const int64 Gap = FMath::Min(BlockStartFrame - RingValidFrom, RingSize);
			for (int64 F = BlockStartFrame - Gap; F < BlockStartFrame; ++F)
			{
				Ring[static_cast<int32>(F & RingMask)] = 0.0f;
			}
		}

		FShotClockSnapshot Snap;
		if (Clock)
		{
			Snap = Clock->Snapshot();
		}
		const bool bPlanActive = Plan.IsValid() && Clock && Snap.ShotId == Plan->ShotId && Snap.bRunning && Snap.bAnchored;
		if (bPlanActive)
		{
			if (!bPlanStarted)
			{
				NextImpact = 0;
				while (NextImpact < Plan->Impacts.Num() && Plan->Impacts[NextImpact].ShotTime < Snap.OriginShotTime - 1e-9)
				{
					++NextImpact;
				}
				// A plan adopted while its shot already plays (a re-created source): impacts whose onset is past are skipped, never
				// replayed at once. A new shot's anchor lies ahead of the block (LeadMin), so nothing is skipped then.
				double OnsetFrame = 0.0;
				while (NextImpact < Plan->Impacts.Num() && Snap.ShotTimeToDeviceFrame(Plan->Impacts[NextImpact].ShotTime, OnsetFrame)
					&& OnsetFrame < static_cast<double>(BlockStartFrame))
				{
					++NextImpact;
					++Stats.SkippedImpacts;
				}
				RenderedUpTo = Snap.OriginShotTime - 1e-9;
				ContinuousCursor = 0;
				bPlanStarted = true;
				LastGeneration = Snap.Generation;
			}
			else if (Snap.Generation != LastGeneration)
			{
				int32 FirstAtOrigin = 0;
				while (FirstAtOrigin < Plan->Impacts.Num() && Plan->Impacts[FirstAtOrigin].ShotTime < Snap.OriginShotTime - 1e-9)
				{
					++FirstAtOrigin;
				}
				if (Snap.OriginShotTime < RenderedUpTo - 0.05)
				{
					NextImpact = FirstAtOrigin; // backward seek: render again from the new origin
					RenderedUpTo = Snap.OriginShotTime - 1e-9;
				}
				else if (FirstAtOrigin > NextImpact)
				{
					Stats.SkippedImpacts += FirstAtOrigin - NextImpact; // forward seek
					NextImpact = FirstAtOrigin;
				}
				ContinuousCursor = 0;
				LastGeneration = Snap.Generation;
			}
			if (Snap.IsPlaying())
			{
				ScheduleImpacts(Snap, BlockStartFrame, Frames);
			}
			LastOutputGain = Plan->OutputGain;
		}

		// Live one-shots start at the next block (+ their delay).
		for (int32 I = 0; I < PendingPcm.Num(); ++I)
		{
			FLivePcm& L = PendingPcm[I];
			const int64 First = BlockStartFrame + L.DelayFrames;
			const int32 Count = static_cast<int32>(FMath::Min<int64>(L.Samples.Num(), BlockStartFrame + RingSize - First));
			for (int32 J = 0; J < Count; ++J)
			{
				Ring[static_cast<int32>((First + J) & RingMask)] += L.Samples[J];
			}
		}
		PendingPcm.Reset();

		const FPresentationGain* Presentation = bPlanActive ? Plan->Presentation.Get() : nullptr;
		const double Gain = bPlanActive ? Plan->OutputGain : (Plan.IsValid() ? LastOutputGain : LiveOutputGain);
		const bool bLive = LiveTargetSpeed > 0.0 || LiveSpeed > 1e-6;
		const double LiveSmooth = 1.0 - std::exp(-1.0 / (0.030 * SampleRate));
		const double TickDecay = std::exp(-1.0 / (0.003 * SampleRate));
		const int32 LiveK = KindIndex(LiveKind);
		for (int32 I = 0; I < Frames; ++I)
		{
			const int64 F = BlockStartFrame + I;
			const int32 Slot = static_cast<int32>(F & RingMask);
			const double ShotTime = bPlanActive ? Snap.DeviceFrameToShotTime(static_cast<double>(F)) : 0.0;
			double G = LastPresentation;
			if (Presentation)
			{
				G = Presentation->Evaluate(ShotTime);
				LastPresentation = G;
			}
			double Sample = static_cast<double>(Ring[Slot]) * G;
			Ring[Slot] = 0.0f;
			Sample += ContinuousSample(Snap, ShotTime, bPlanActive);
			Sample *= Gain;
			if (bLive && bNoiseReady[LiveK])
			{
				LiveSpeed += (LiveTargetSpeed - LiveSpeed) * LiveSmooth;
				LiveTickPhase += LiveSpeed / TileJointSpacing / SampleRate;
				if (LiveTickPhase >= 1.0)
				{
					LiveTickPhase -= 1.0;
					LiveTickEnv = 1.0;
				}
				LiveTickEnv *= TickDecay;
				Sample += LiveOutputGain * LiveSpeed * LiveGainPerMps * (1.0 + 1.5 * LiveTickEnv) * Noise[LiveK].Next();
			}
			Out[I] = static_cast<float>(Sample);
		}
		RingValidFrom = BlockStartFrame + Frames;

		const double Micros = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start) * 1000.0;
		Stats.LastRenderMicros = Micros;
		Stats.MaxRenderMicros = FMath::Max(Stats.MaxRenderMicros, Micros);
		Stats.LastBlockFrame = BlockStartFrame;
	}
}
