#pragma once

// Shot -> audio plan (Docs/specs/audio.md 1.1, 2, 3.6, 6.1, 6.6, 8.3; Docs/ue-architecture.md 18.5). Pure and worker-safe: turns a
// simulated shot (rb::ShotEvent log + ball tracks) of one table into one RbAudio::FVoicePlan per voice of the table's audio tier,
// with everything resolved: pulse constants (Hertz + Tsuji / sin^1.5), per-ball kernels, listener weights P_n(cos th), path
// delays and gains, structural banks, rolling / sliding segments, the coin-op gully runs and trap clicks, and the plan-time
// presentation envelope of the table stem (a mono render at the listener). Mirrors click_synth.py events_to_impacts (the event
// filters and ESTIMATE contact presets), so the dive-bar break covers the same 79 impacts + 3 gully runs (AU-T12).
//
// Voices of tier T0 (audio.md 6.1): 0..15 balls, 16..21 rails (CushionId), 22..27 pockets (PocketId), 28 table body (bed, gully,
// trap), 29 cue. T1: 4 quadrant voices; T2: 1 voice at the table centre. Geometry is the table's core frame (metres, cloth plane
// z = 0); the listener is given in the same frame. Owner: M2-C.

#include "CoreMinimal.h"

#include "Audio/RbAudioSettings.h"
#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbPresentation.h"

#include "rb/Math/Vec3.h"
#include "rb/Physics/ShotResult.h"

struct FRbTableContext;

struct FRbAudioPlanOptions
{
	ERbTableAudioTier Tier = ERbTableAudioTier::T0;
	double SampleRate = 48000.0;
	RbAudio::EDynamicRangeMode Mode = RbAudio::EDynamicRangeMode::Wide;
	double RefDistance = 0.25;          // voice output referred to this distance [m]
	double DirectivityFloorDb = -20.0;  // floor of |P_1| (<= -200: none, the golden form)
	double PanCompensation = 1.0;       // x the output (equal-power panning)
	bool bPresentation = true;          // compute the envelope (a mono render at the listener)
	uint64 ShotId = 1;                  // the clock id this plan plays under
	uint64 Seed = 0;                    // variation seed (shot hash): replays render identically
	// The listener's right-hand direction [core frame, unit; zero = unknown]. The presentation envelope then follows the louder ear:
	// the engine pans a source at azimuth th with equal power and the voices compensate a centred source to its physical level
	// (URbAudioSettings::PanCompensation), so one ear gets up to sqrt 2 (+3 dB) of the physical pressure for a source at the side.
	rb::Vec3 ListenerRightCore = rb::Vec3(0.0, 0.0, 0.0);
};

// Gain of the louder ear for a source at SourceCore heard at ListenerCore facing with RightCore (1 when RightCore is zero):
// sqrt(2) max(cos f, sin f) with the stereo pan fraction f = (th + 90 deg) / 180 deg x 90 deg of the azimuth th (front / back
// symmetric, as the engine's stereo channel map).
RAWBREAK_API double RbLouderEarPanGain(const rb::Vec3& SourceCore, const rb::Vec3& ListenerCore, const rb::Vec3& RightCore);

// One logical sound of the plan (for logs, coverage and the recorded-break onset check).
struct FRbAudioPlanImpact
{
	int32 ImpactId = INDEX_NONE;
	int32 SourceEvent = INDEX_NONE;
	RbAudio::EImpactKind Kind = RbAudio::EImpactKind::BallBall;
	double ShotTime = 0.0;
	double ArrivalTime = 0.0;           // shot time + propagation to the plan's listener
	double NormalSpeed = 0.0;
	int32 BallA = INDEX_NONE;
	int32 BallB = INDEX_NONE;
	double PeakPaAtListener = 0.0;      // from the mono render (0 without presentation)
};

struct FRbShotAudioPlan
{
	TArray<RbAudio::FVoicePlan> Voices;       // index = voice of the tier
	TArray<rb::Vec3> VoicePositionsCore;      // emitter of each voice [m, core frame] (balls: at the shot start)
	TArray<FRbAudioPlanImpact> Impacts;       // one per physical impact, sorted by time
	int32 NumGullyRuns = 0;
	int32 NumSkippedEvents = 0;               // pressing / too slow contacts (silent by design)
	double StemPeakPa = 0.0;                  // peak of the mono stem at the listener
	double StemPeakSpl = 0.0;
	double BuildMilliseconds = 0.0;
	double PresentationMilliseconds = 0.0;    // of which the mono render + envelope (audio.md 4.2)
	TArray<double> MonoStemPa;                // the mono render at the listener (only with bKeepMonoStem)
	double MonoStemStartTime = 0.0;           // shot time of MonoStemPa[0]
	RbAudio::FPresentationGainPtr Presentation;
};

class RAWBREAK_API FRbAudioPlanBuilder
{
public:
	static constexpr int32 NumBallVoices = 16;
	static constexpr int32 FirstRailVoice = 16;
	static constexpr int32 FirstPocketVoice = 22;
	static constexpr int32 BodyVoice = 28;
	static constexpr int32 CueVoice = 29;
	static constexpr int32 NumT0Voices = 30;

	static int32 NumVoices(ERbTableAudioTier Tier);
	static const TCHAR* VoiceName(ERbTableAudioTier Tier, int32 Voice);

	// Acoustic spec of a ball of the table's set (phenolic; radius and mass from the ball set).
	static RbAudio::FBallAcoustics BallAcoustics(const FRbTableContext& Context, int32 BallId);
	// Bar box (coin-op cabinet, gully and trap) vs pro table.
	static bool IsCoinOp(const FRbTableContext& Context);
	// Fixed emitter positions of a tier [m, core frame]: rails at their nose midpoints, pockets below their capture centres, the body
	// under the bed, the cue at the head spot (ball voices: the head spot until the first plan / ball positions take over).
	static void EmitterPositions(const FRbTableContext& Context, ERbTableAudioTier Tier, TArray<rb::Vec3>& Out);

	// Computes and caches everything a plan of this table needs that does not depend on the shot (the radiation kernels of every
	// ball of the set at the device rate, the contact shape tables of the restitutions in use), so the first shot's plan is ready
	// in milliseconds (a worker task started when the table's voices are created; thread-safe caches).
	static void Prewarm(const FRbTableContext& Context, double SampleRate);

	// Where the cue voice sounds for a shot: 0.35 m behind the struck ball along the stroke, 8 cm above it, at the first strike
	// (false without a strike). Cheap: the table audio places the cue voice with it before the plan's first sound.
	static bool CuePointCore(const rb::ShotResult& Result, rb::Vec3& OutCore);

	// Builds the plan. ListenerCore: the listener [m, core frame] at plan time.
	static void Build(const rb::ShotResult& Result, const FRbTableContext& Context, const rb::Vec3& ListenerCore, const FRbAudioPlanOptions& Options,
		FRbShotAudioPlan& Out, bool bKeepMonoStem = false);

	// Offline render of the plan's impacts at the listener: sum of every voice's impacts scaled back to absolute pressure [Pa]
	// (the presentation input, AU-T12 / T16 checks). StartTime: shot time of Out[0].
	static void RenderImpactsAtListener(const FRbShotAudioPlan& Plan, double SampleRate, double RefDistance, double StartTime, double Duration,
		TArray<double>& Out);
};
