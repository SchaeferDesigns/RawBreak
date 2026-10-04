#include "RbAudio/RbShotAudioClock.h"

#include <cmath>

// Owner: M2-C.

namespace RbAudio
{
	void FShotAudioClock::Publish(const FMapping& M)
	{
		const uint32 S = Seq.load(std::memory_order_relaxed);
		Seq.store(S + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		MShotId.store(M.ShotId, std::memory_order_relaxed);
		MGeneration.store(M.Generation, std::memory_order_relaxed);
		MOriginClock.store(M.OriginClock, std::memory_order_relaxed);
		MOriginShotTime.store(M.OriginShotTime, std::memory_order_relaxed);
		MRate.store(M.Rate, std::memory_order_relaxed);
		MVisualLatency.store(M.VisualLatency, std::memory_order_relaxed);
		MRunning.store(M.bRunning, std::memory_order_relaxed);
		MHeld.store(M.bHeld, std::memory_order_relaxed);
		Seq.store(S + 2, std::memory_order_release);
	}

	FShotAudioClock::FMapping FShotAudioClock::ReadMapping() const
	{
		FMapping M;
		for (;;)
		{
			const uint32 S1 = Seq.load(std::memory_order_acquire);
			M.ShotId = MShotId.load(std::memory_order_relaxed);
			M.Generation = MGeneration.load(std::memory_order_relaxed);
			M.OriginClock = MOriginClock.load(std::memory_order_relaxed);
			M.OriginShotTime = MOriginShotTime.load(std::memory_order_relaxed);
			M.Rate = MRate.load(std::memory_order_relaxed);
			M.VisualLatency = MVisualLatency.load(std::memory_order_relaxed);
			M.bRunning = MRunning.load(std::memory_order_relaxed);
			M.bHeld = MHeld.load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			const uint32 S2 = Seq.load(std::memory_order_relaxed);
			if (S1 == S2 && (S1 & 1u) == 0)
			{
				return M;
			}
		}
	}

	void FShotAudioClock::StartShot(uint64 InShotId, double InOriginClock, double InOriginShotTime, double InRate, bool bInHeld, double VisualLatencySeconds)
	{
		WriterMapping.ShotId = InShotId;
		WriterMapping.Generation = ++WriterGeneration;
		WriterMapping.OriginClock = InOriginClock;
		WriterMapping.OriginShotTime = InOriginShotTime;
		WriterMapping.Rate = FMath::Max(0.0, InRate);
		WriterMapping.VisualLatency = VisualLatencySeconds;
		WriterMapping.bRunning = true;
		WriterMapping.bHeld = bInHeld;
		Publish(WriterMapping);
	}

	void FShotAudioClock::SetMapping(double InOriginClock, double InOriginShotTime, double InRate, bool bInHeld)
	{
		WriterMapping.Generation = ++WriterGeneration;
		WriterMapping.OriginClock = InOriginClock;
		WriterMapping.OriginShotTime = InOriginShotTime;
		WriterMapping.Rate = FMath::Max(0.0, InRate);
		WriterMapping.bHeld = bInHeld;
		Publish(WriterMapping);
	}

	void FShotAudioClock::Stop()
	{
		WriterMapping.Generation = ++WriterGeneration;
		WriterMapping.bRunning = false;
		Publish(WriterMapping);
	}

	void FShotAudioClock::StartShotAnchored(uint64 InShotId, int64 AnchorFrame, double InOriginShotTime, double InRate, double SampleRate)
	{
		WriterMapping.ShotId = InShotId;
		WriterMapping.Generation = ++WriterGeneration;
		WriterMapping.OriginClock = 0.0;
		WriterMapping.OriginShotTime = InOriginShotTime;
		WriterMapping.Rate = FMath::Max(0.0, InRate);
		WriterMapping.VisualLatency = 0.0;
		WriterMapping.bRunning = true;
		WriterMapping.bHeld = false;
		AnchorSampleRate.store(SampleRate, std::memory_order_relaxed);
		Anchor.store(Pack(WriterMapping.Generation, AnchorFrame), std::memory_order_release);
		Publish(WriterMapping);
	}

	void FShotAudioClock::TryAnchor(int64 BlockFrame, double PlatformNow, double OutputLatencySeconds, int64 LeadMinFrames, double SampleRate)
	{
		if (SampleRate <= 0.0)
		{
			return;
		}
		const FMapping M = ReadMapping();
		if (!M.bRunning || M.bHeld)
		{
			return;
		}
		uint64 Current = Anchor.load(std::memory_order_acquire);
		const uint64 Gen = uint64(M.Generation) & GenerationMask;
		if ((Current & ValidBit) && ((Current >> FrameBits) & GenerationMask) == Gen)
		{
			return;
		}
		const double LeadSeconds = M.OriginClock + M.VisualLatency - (PlatformNow + OutputLatencySeconds);
		const int64 Lead = static_cast<int64>(std::llround(LeadSeconds * SampleRate));
		const int64 Frame = BlockFrame + FMath::Max(LeadMinFrames, Lead);
		AnchorSampleRate.store(SampleRate, std::memory_order_relaxed);
		if (Anchor.compare_exchange_strong(Current, Pack(M.Generation, Frame), std::memory_order_acq_rel))
		{
			LastLead.store(Lead - LeadMinFrames, std::memory_order_relaxed);
		}
	}

	FShotClockSnapshot FShotAudioClock::Snapshot() const
	{
		const FMapping M = ReadMapping();
		FShotClockSnapshot S;
		S.ShotId = M.ShotId;
		S.Generation = M.Generation;
		S.bRunning = M.bRunning;
		S.bHeld = M.bHeld;
		S.OriginShotTime = M.OriginShotTime;
		S.Rate = M.Rate;
		S.SampleRate = AnchorSampleRate.load(std::memory_order_relaxed);
		const uint64 A = Anchor.load(std::memory_order_acquire);
		if ((A & ValidBit) && ((A >> FrameBits) & GenerationMask) == (uint64(M.Generation) & GenerationMask))
		{
			S.bAnchored = true;
			S.AnchorFrame = static_cast<int64>(A & FrameMask);
		}
		return S;
	}

	bool FShotAudioClock::ShotTimeToDeviceFrame(double ShotTime, double& OutDeviceFrame) const
	{
		return Snapshot().ShotTimeToDeviceFrame(ShotTime, OutDeviceFrame);
	}

	uint64 FShotAudioClock::GetShotId() const
	{
		return MShotId.load(std::memory_order_acquire);
	}

	bool FShotAudioClock::IsAnchored() const
	{
		return Snapshot().bAnchored;
	}

	uint32 FShotAudioClock::GetGeneration() const
	{
		return MGeneration.load(std::memory_order_acquire);
	}
}
