#include "RbAudio/RbShotAudioClock.h"

#include "Misc/ScopeLock.h"

// Owner: M2-C.

namespace RbAudio
{
	void FShotAudioClock::StartShot(uint64 InShotId, double InOriginClock, double InOriginShotTime, double InRate, bool bInHeld,
		double VisualLatencySeconds)
	{
		FScopeLock Guard(&Lock);
		ShotId = InShotId;
		OriginClock = InOriginClock;
		OriginShotTime = InOriginShotTime;
		Rate = InRate;
		bHeld = bInHeld;
		VisualLatency = VisualLatencySeconds;
		bRunning = true;
		bAnchored = false;
	}

	void FShotAudioClock::SetMapping(double InOriginClock, double InOriginShotTime, double InRate, bool bInHeld)
	{
		FScopeLock Guard(&Lock);
		OriginClock = InOriginClock;
		OriginShotTime = InOriginShotTime;
		Rate = InRate;
		bHeld = bInHeld;
		bAnchored = false;
	}

	void FShotAudioClock::Stop()
	{
		FScopeLock Guard(&Lock);
		bRunning = false;
		bAnchored = false;
	}

	void FShotAudioClock::TryAnchor(int64 BlockFrame, double PlatformNow, double OutputLatencySeconds, int64 LeadMinFrames, double InSampleRate)
	{
		FScopeLock Guard(&Lock);
		if (!bRunning || bHeld || bAnchored || InSampleRate <= 0.0)
		{
			return;
		}
		SampleRate = InSampleRate;
		const double Lead = FMath::RoundToDouble((OriginClock + VisualLatency - (PlatformNow + OutputLatencySeconds)) * SampleRate);
		AnchorFrame = static_cast<double>(BlockFrame) + FMath::Max(static_cast<double>(LeadMinFrames), Lead);
		bAnchored = true;
	}

	bool FShotAudioClock::ShotTimeToDeviceFrame(double ShotTime, double& OutDeviceFrame) const
	{
		FScopeLock Guard(&Lock);
		if (!bRunning || bHeld || !bAnchored || Rate <= 0.0)
		{
			return false;
		}
		// Film-style slow motion (audio.md 5.2): event spacing scales with 1 / Rate, each impact keeps its natural duration.
		OutDeviceFrame = AnchorFrame + (ShotTime - OriginShotTime) / Rate * SampleRate;
		return true;
	}

	uint64 FShotAudioClock::GetShotId() const
	{
		FScopeLock Guard(&Lock);
		return ShotId;
	}

	bool FShotAudioClock::IsAnchored() const
	{
		FScopeLock Guard(&Lock);
		return bAnchored;
	}
}
