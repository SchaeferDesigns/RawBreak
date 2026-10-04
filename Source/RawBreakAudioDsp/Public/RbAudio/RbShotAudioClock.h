#pragma once

// Per-table shot audio clock (Docs/specs/audio.md 5.1-5.2; Docs/ue-architecture.md 18.5). ONE per table, shared by every voice
// of that table, so events land at exact relative device frames across voices (AU-T08, AU-T21).
//
//   Game thread   StartShot(...) from URbShotPlaybackComponent::OnPlaybackStarted (FRbPlaybackClock: OriginClock =
//                 FPlatformTime-domain seconds of shot time OriginShotTime, Rate, held, live); SetMapping(...) from
//                 OnPlaybackClockChanged (slow motion, pause, seek, world pause); Stop(). Every call starts a new generation.
//   Audio thread  the FIRST voice callback of a generation anchors it in device frames (TryAnchor; lock-free, one winner):
//                 F0 = BlockFrame + max(LeadMin, round((OriginClock + VisualLatency - (PlatformNow + OutputLatency)) * fs));
//                 every voice then converts a shot time to its own (fractional) device frame with the snapshot's mapping:
//                 frame(t) = F0 + (t - OriginShotTime) / Rate * fs (film-style slow motion: spacing x 1 / Rate, natural pitch).
// Lock-free: the mapping is published with a sequence lock (single writer), the anchor with a compare-exchange on a packed word
// (generation + frame). Offline rendering (tests, trailer stems) anchors directly with StartShotAnchored. Owner: M2-C.

#include "CoreMinimal.h"

#include <atomic>

namespace RbAudio
{
	struct FShotClockSnapshot
	{
		uint64 ShotId = 0;
		uint32 Generation = 0;
		bool bRunning = false;
		bool bHeld = false;
		bool bAnchored = false;
		double OriginShotTime = 0.0;
		double Rate = 1.0;
		double SampleRate = 48000.0;
		int64 AnchorFrame = 0; // device frame of OriginShotTime

		// Playing (anchored, running, not held, rate > 0).
		bool IsPlaying() const { return bRunning && bAnchored && !bHeld && Rate > 0.0; }
		// Device frame (fractional) at which shot time T is heard; false while not playing.
		bool ShotTimeToDeviceFrame(double T, double& OutFrame) const
		{
			if (!IsPlaying())
			{
				return false;
			}
			OutFrame = static_cast<double>(AnchorFrame) + (T - OriginShotTime) / Rate * SampleRate;
			return true;
		}
		// Shot time heard at device frame F (held: the hold time).
		double DeviceFrameToShotTime(double F) const
		{
			if (!bAnchored || bHeld)
			{
				return OriginShotTime;
			}
			return OriginShotTime + (F - static_cast<double>(AnchorFrame)) / SampleRate * Rate;
		}
	};

	class RAWBREAKAUDIODSP_API FShotAudioClock
	{
	public:
		// A shot starts (live or replay). Times in the playback clock domain (FPlatformTime seconds sampled at frame start).
		void StartShot(uint64 ShotId, double OriginClock, double OriginShotTime, double Rate, bool bHeld, double VisualLatencySeconds);
		// A new mapping of the running shot (rate / pause / seek); the anchor is re-derived at the next callback.
		void SetMapping(double OriginClock, double OriginShotTime, double Rate, bool bHeld);
		void Stop();
		// Offline / tests: starts a shot already anchored at device frame AnchorFrame (no platform time involved).
		void StartShotAnchored(uint64 ShotId, int64 AnchorFrame, double OriginShotTime, double Rate, double SampleRate);

		// Audio thread, once per device block of any voice of this table (idempotent per generation).
		void TryAnchor(int64 BlockFrame, double PlatformNow, double OutputLatencySeconds, int64 LeadMinFrames, double SampleRate);

		// Consistent view of mapping + anchor (any thread).
		FShotClockSnapshot Snapshot() const;

		// Device frame (fractional) at which shot time ShotTime is heard; false while stopped, held or not yet anchored.
		bool ShotTimeToDeviceFrame(double ShotTime, double& OutDeviceFrame) const;

		uint64 GetShotId() const;
		bool IsAnchored() const;
		uint32 GetGeneration() const;
		// Frames the last anchor was shifted by the LeadMin clamp (late plan / hand-off; diagnostics, AU-0).
		int64 GetLastLeadFrames() const { return LastLead.load(std::memory_order_relaxed); }

	private:
		struct FMapping
		{
			uint64 ShotId = 0;
			uint32 Generation = 0;
			double OriginClock = 0.0;
			double OriginShotTime = 0.0;
			double Rate = 1.0;
			double VisualLatency = 0.0;
			bool bRunning = false;
			bool bHeld = false;
		};
		void Publish(const FMapping& M);
		FMapping ReadMapping() const;

		static constexpr int32 FrameBits = 44;
		static constexpr uint64 FrameMask = (uint64(1) << FrameBits) - 1;
		static constexpr uint64 GenerationMask = (uint64(1) << 19) - 1;
		static constexpr uint64 ValidBit = uint64(1) << 63;
		static uint64 Pack(uint32 Generation, int64 Frame) { return ValidBit | ((uint64(Generation) & GenerationMask) << FrameBits) | (uint64(Frame) & FrameMask); }

		// Sequence-locked mapping (written by one thread; each field atomic so readers never see torn values).
		std::atomic<uint32> Seq{0};
		std::atomic<uint64> MShotId{0};
		std::atomic<uint32> MGeneration{0};
		std::atomic<double> MOriginClock{0.0};
		std::atomic<double> MOriginShotTime{0.0};
		std::atomic<double> MRate{1.0};
		std::atomic<double> MVisualLatency{0.0};
		std::atomic<bool> MRunning{false};
		std::atomic<bool> MHeld{false};

		std::atomic<uint64> Anchor{0};
		std::atomic<double> AnchorSampleRate{48000.0};
		std::atomic<int64> LastLead{0};
		uint32 WriterGeneration = 0; // writer side only
		FMapping WriterMapping;      // writer side only
	};
}
