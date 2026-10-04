#pragma once

// The render core of ONE table voice (Docs/specs/audio.md 3.6, 5.1, 5.2), shared by the engine generator
// (URbImpactVoiceComponent's ISoundGenerator, audio render thread) and the offline renders of the tests and trailer stems:
//   * impacts: every event whose first written frame falls before the end of the next block (lookahead) is rendered COMPLETELY
//     into an overlap-add ring at its fractional device frame from the table's FShotAudioClock snapshot (sub-sample exact, and
//     every voice of a table shares the anchor: AU-T08 / AU-T21);
//   * continuous layers (rolling / sliding / gully) evaluated at the shot time of every output frame (frame-rate independent),
//     faded out while the clock is held or stopped (pause without a click);
//   * the plan-time presentation envelope of the table stem and the output gain (Pa -> digital) applied at the output;
//   * mapping changes: forward re-anchors (rate, hold / resume) keep what was rendered; a backward seek re-renders from the new
//     origin; a new plan starts from its clock origin;
//   * live one-shots (footsteps, loose-ball floor hits: pre-rendered PCM) and a live continuous layer (loose ball rolling).
// Deterministic: the same plan + anchor give bit-identical output for any block size (AU-T13). Single-threaded use (the owner's
// thread); no allocation after Initialize except when a live PCM arrives. Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbImpactSynth.h"
#include "RbAudio/RbNoiseSynth.h"
#include "RbAudio/RbShotAudioClock.h"

namespace RbAudio
{
	using FVoicePlanPtr = TSharedPtr<const FVoicePlan, ESPMode::ThreadSafe>;

	struct FVoiceStats
	{
		int32 RenderedImpacts = 0;   // impacts rendered into the ring (all plans)
		int32 LateImpacts = 0;       // impacts whose first frame was already past (partly lost)
		int32 SkippedImpacts = 0;    // impacts skipped by a forward seek
		double LastRenderMicros = 0.0;
		double MaxRenderMicros = 0.0;
		int64 LastBlockFrame = -1;
	};

	class RAWBREAKAUDIODSP_API FVoiceRenderer
	{
	public:
		static constexpr int32 DefaultRingFrames = 32768; // 0.68 s at 48 kHz: longest impact (bank tail) + lookahead

		void Initialize(double InSampleRate, int32 InBlockFrames, int32 RingFrames = DefaultRingFrames);
		double GetSampleRate() const { return SampleRate; }

		// Replaces the plan (null = only tails / live content). The next block starts the plan at its clock origin.
		void SetPlan(FVoicePlanPtr InPlan);
		const FVoicePlan* GetPlan() const { return Plan.Get(); }

		// Output gain of live content and of the ring when no plan is set (Pa -> digital), e.g. 1 / P_fs x bus offset.
		void SetLiveOutputGain(double Gain) { LiveOutputGain = Gain; }
		// Adds a pre-rendered one-shot [Pa at the voice's reference distance]; it starts DelayFrames after the next block start.
		void AddLivePcm(TConstArrayView<float> Pcm, int32 DelayFrames = 0);
		// Live continuous layer (loose ball rolling on the floor): target speed [m/s] and amplitude per m/s; smoothed per frame.
		void SetLiveContinuous(ENoiseKind Kind, double SpeedMps, double GainPerMps);

		// Renders one block whose first frame is device frame BlockStartFrame (Clock may be null: no plan content).
		void RenderBlock(const FShotAudioClock* Clock, int64 BlockStartFrame, TArrayView<float> Out);

		const FVoiceStats& GetStats() const { return Stats; }
		// Shot time of the last rendered impact (tests).
		double GetRenderedUpTo() const { return RenderedUpTo; }

	private:
		void ScheduleImpacts(const FShotClockSnapshot& Snap, int64 BlockStartFrame, int32 Frames);
		void RenderImpactIntoRing(const FImpactEvent& Event, double EventFrame, int64 BlockStartFrame);
		void EnsureNoise(ENoiseKind Kind);
		double ContinuousSample(const FShotClockSnapshot& Snap, double ShotTime, bool bActive);

		double SampleRate = 48000.0;
		int32 BlockFrames = 512;
		int32 RingMask = 0;
		TArray<float> Ring;               // device frame f lives at Ring[f & RingMask]
		int64 RingValidFrom = 0;          // frames < this are consumed (zero)
		TArray<float> Scratch;            // linear render target of one impact
		FImpactRenderer Renderer;

		FVoicePlanPtr Plan;
		bool bPlanStarted = false;        // NextImpact positioned for the current plan
		uint32 LastGeneration = 0;
		int32 NextImpact = 0;
		double RenderedUpTo = -1e30;
		int32 ContinuousCursor = 0;
		double LastPresentation = 1.0;
		double LastOutputGain = 1.0;
		double LiveOutputGain = 1.0;

		// Continuous layers (one noise source per kind, created on demand).
		FShapedNoise Noise[4];
		bool bNoiseReady[4] = {false, false, false, false};
		double ContinuousLevel[4] = {0.0, 0.0, 0.0, 0.0}; // smoothed amplitude per kind
		double ContinuousSmooth = 0.0;    // one-pole coefficient (5 ms)
		double HoldFade = 0.0;            // one-pole coefficient of the hold / stop fade (20 ms)

		// Live layers.
		struct FLivePcm
		{
			TArray<float> Samples;
			int64 StartFrame = INDEX_NONE; // assigned at the next block
			int32 DelayFrames = 0;
		};
		TArray<FLivePcm> PendingPcm;
		ENoiseKind LiveKind = ENoiseKind::RollingFloor;
		double LiveTargetSpeed = 0.0;
		double LiveGainPerMps = 0.0;
		double LiveSpeed = 0.0;
		double LiveTickPhase = 0.0;
		double LiveTickEnv = 0.0;

		FVoiceStats Stats;
	};
}
