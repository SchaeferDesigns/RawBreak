#pragma once

// Per-table shot audio clock (Docs/specs/audio.md 5.1-5.2; Docs/ue-architecture.md 18.5). ONE per table, shared by every voice
// of that table, so events land at exact relative device frames across voices (AU-T08, AU-T21).
//
//   Game thread   StartShot(...) from URbShotPlaybackComponent::OnPlaybackStarted (FRbPlaybackClock: OriginClock =
//                 FPlatformTime-domain seconds of shot time OriginShotTime, Rate, held, live); SetMapping(...) from
//                 OnPlaybackClockChanged (slow motion, pause, seek, world pause); Stop().
//   Audio thread  the FIRST voice callback after StartShot anchors the shot in device frames (TryAnchor):
//                 F0 = BlockFrame + max(LeadMin, round((OriginClock + VisualLatency - (PlatformNow + OutputLatency)) * fs));
//                 every voice then converts a shot time to its own frame index with ShotTimeToDeviceFrame (fraction kept).
// Owner: M2-C (stub by the M2 architect step: the mapping maths; TODO(M2-C) lock-free publication and AU-0 constants).

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

namespace RbAudio
{
	class RAWBREAKAUDIODSP_API FShotAudioClock
	{
	public:
		// A shot starts (live or replay). Times in the playback clock domain (FPlatformTime seconds sampled at frame start).
		void StartShot(uint64 ShotId, double OriginClock, double OriginShotTime, double Rate, bool bHeld, double VisualLatencySeconds);
		// A new mapping of the running shot (rate / pause / seek); the anchor is re-derived at the next callback.
		void SetMapping(double OriginClock, double OriginShotTime, double Rate, bool bHeld);
		void Stop();

		// Audio thread, once per device block of any voice of this table.
		void TryAnchor(int64 BlockFrame, double PlatformNow, double OutputLatencySeconds, int64 LeadMinFrames, double SampleRate);

		// Device frame (fractional) at which shot time ShotTime is heard; false while stopped, held or not yet anchored.
		bool ShotTimeToDeviceFrame(double ShotTime, double& OutDeviceFrame) const;

		uint64 GetShotId() const;
		bool IsAnchored() const;

	private:
		mutable FCriticalSection Lock; // TODO(M2-C): lock-free (seqlock / atomics) before the AU-0 sign-off
		uint64 ShotId = 0;
		double OriginClock = 0.0;
		double OriginShotTime = 0.0;
		double Rate = 1.0;
		double VisualLatency = 0.0;
		double SampleRate = 48000.0;
		double AnchorFrame = 0.0;     // device frame of OriginShotTime
		bool bRunning = false;
		bool bHeld = false;
		bool bAnchored = false;
	};
}
