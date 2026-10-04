#pragma once

// Plain data of the table-audio DSP (Docs/specs/audio.md 3.6, 8.2, 8.3; Docs/ue-architecture.md 18.5). The plan builder of the
// RawBreak module (URbTableAudioComponent / RbAudioPlan, M2-C) turns an FRbShot (rb::ShotEvent list + ball tracks) into one
// FVoicePlan per voice on a worker; the voices' generators render it on the audio thread with the per-table FShotAudioClock.
// Everything a voice needs is resolved at plan time (pulse constants, kernels, listener weights, delays, gains), so the audio
// thread only renders. Nothing here knows UObjects, rb:: types or the audio device. Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbBallKernels.h"
#include "RbAudio/RbContactShape.h"

namespace RbAudio
{
	// What radiates (selects the contact law, the structural bank and the level law; audio.md 2.1-2.6).
	enum class EImpactKind : uint8
	{
		BallBall,     // AU-20/21: Hertz + Tsuji, both balls radiate (Lamb modes)
		BallCushion,  // AU-30: soft contact (T(1 m/s) ~2.5 ms) + rail / cabinet bank
		BallJaw,      // AU-31: facing knock
		BallRailTop,  // AU-32: rail cap
		BallSlate,    // AU-24: landing on the bed + bed bank
		BallLiner,    // AU-34/35: liner / gully boot hit
		PocketDrop,   // AU-35: coin-op drop into the gully (synthesised stand-in for the LIB layer in M2)
		TrapClick,    // AU-37: ball into the trap row (coin-op)
		TipStrike,    // AU-01: tip on the cue ball + cue body mode
		Miscue,       // AU-02: shortened pulse + scrape
		TipRecontact, // AU-03
		FloorHit,     // AU-25: a loose ball on the floor (M2-E impacts)
		Footstep,     // AU-65: synthesised heel / toe transient per surface (M2)
		Count,
	};
	RAWBREAKAUDIODSP_API const TCHAR* ToString(EImpactKind Kind);

	enum class EPulseShape : uint8
	{
		Hertz,  // the self-similar Hertz + Tsuji shape table of the restitution
		Sine15, // sin^1.5 of duration T and impulse J (tip, drops)
	};

	// Structural radiators (audio.md 3.5, ESTIMATE modal banks of click_synth.py).
	enum class EModalBank : uint8
	{
		None,
		RailBarBox, // coin-op bar box: hollow cabinet with the ball-return channel (boomier)
		RailPro,    // pro 9-ft table
		Bed,        // slate bed (landings)
		Pocket,     // pocket / gully boot
		Cue,        // cue body (tip strike), free field
		Cabinet,    // the bar box's two lowest modes (trap row inside the cabinet)
		Count,
	};

	// First-order reflection filter of an image path: y[k] = B0 x[k] + B1 x[k-1] - A1 y[k-1].
	struct FReflection
	{
		double B0 = 1.0;
		double B1 = 0.0;
		double A1 = 0.0;

		// Cloth on slate: bilinear transform of 0.9 / (1 + s / (2 pi 8 kHz)) (click_synth.py cloth_iir).
		RAWBREAKAUDIODSP_API static FReflection Cloth(double SampleRate);
		// A hard floor (VCT on concrete): a plain coefficient (ESTIMATE).
		static FReflection Rigid(double Coefficient)
		{
			FReflection R;
			R.B0 = Coefficient;
			return R;
		}
	};

	// One propagation path of a ball's radiation to the listener (direct, or the image in the cloth / floor plane).
	struct FRadiationPath
	{
		double Weights[4] = {0.0, 0.0, 0.0, 0.0}; // P_n(cos th) of the path (directivity)
		double NearField = 0.0;                   // c / (r fs): weight of the order-1 near-field term
		double Gain = 0.0;                        // distance gain of the path (1 / r, or referred to the voice's reference distance)
		double DelaySeconds = 0.0;                // propagation delay (r - a) / c
		bool bReflected = false;                  // filtered by FImpactEvent::Reflection
	};

	// One impact on one voice. Times in SHOT seconds (t = 0 at the first tip contact); the clock maps them to device frames.
	struct FImpactEvent
	{
		double ShotTime = 0.0;          // contact start [s]
		EImpactKind Kind = EImpactKind::BallBall;
		int32 SourceEvent = INDEX_NONE; // index of the rb::ShotEvent (coverage, logs); INDEX_NONE for derived impacts
		int32 ImpactId = INDEX_NONE;    // one id per physical impact (a ball-ball impact has two voice events with one id)

		// Pulse (resolved at plan time).
		EPulseShape Pulse = EPulseShape::Hertz;
		FContactShapePtr Shape;         // Hertz
		double ContactTime = 0.0;       // T [s]
		double PeakForce = 0.0;         // F_max [N] (Hertz)
		double Impulse = 0.0;           // J [N s] (Sine15)
		double NormalSpeed = 0.0;       // [m/s] (information)

		// Radiation of one ball (null Kernels: none on this voice).
		FBallKernelsPtr Kernels;
		FRadiationPath Paths[2];
		int32 NumPaths = 0;
		FReflection Reflection;

		// Structural bank driven by the same force (rail, bed, pocket, cue, cabinet).
		EModalBank Bank = EModalBank::None;
		double BankGain = 0.0;          // 1 / r (or referred to the voice's reference distance)
		double BankDelaySeconds = 0.0;

		double LowPassHz = 0.0;         // optional one-pole muffling (sources inside the cabinet), 0 = off
		double Gain = 1.0;              // overall gain
		double ListenerDistance = 1.0;  // plan-time distance of the source to the listener [m] (absolute-level renders)
		double LouderEarGain = 1.0;     // presentation only: the engine's pan gain of the louder ear (1 straight ahead ... sqrt 2 at the side)
		uint32 Seed = 0;                // variation seed (from the shot hash: replays render identically, AU-T13)
	};

	enum class ENoiseKind : uint8
	{
		RollingCloth, // AU-22
		SlidingCloth, // AU-23
		GullyRun,     // AU-36 (coin-op)
		RollingFloor, // AU-25 (loose ball)
	};

	// A continuous source over [StartTime, EndTime] with a linearly varying speed (piecewise from the ball tracks).
	struct FContinuousSegment
	{
		double StartTime = 0.0;  // shot seconds
		double EndTime = 0.0;
		double Speed0 = 0.0;     // [m/s] at StartTime
		double SpeedSlope = 0.0; // [m/s^2]
		ENoiseKind Kind = ENoiseKind::RollingCloth;
		double Gain = 1.0;       // amplitude per m/s [Pa at the voice's reference distance] (GullyRun: absolute amplitude)

		double SpeedAt(double T) const { return FMath::Max(0.0, Speed0 + SpeedSlope * (T - StartTime)); }
	};

	// Plan-time presentation envelope of a table (audio.md 4.2): linear gain on a uniform shot-time grid.
	struct FPresentationGain
	{
		double StartTime = 0.0;   // shot time of Gains[0]
		double Step = 0.001;      // [s]
		TArray<float> Gains;      // linear; beyond the ends: 1

		double Evaluate(double ShotTime) const
		{
			if (Gains.Num() == 0)
			{
				return 1.0;
			}
			const double X = (ShotTime - StartTime) / Step;
			if (!(X > 0.0))
			{
				return Gains[0];
			}
			const int32 I = static_cast<int32>(X);
			if (I >= Gains.Num() - 1)
			{
				return Gains.Last();
			}
			const double F = X - I;
			return Gains[I] + (Gains[I + 1] - Gains[I]) * F;
		}
	};
	using FPresentationGainPtr = TSharedPtr<const FPresentationGain, ESPMode::ThreadSafe>;

	// Everything one voice renders for one shot.
	struct FVoicePlan
	{
		uint64 ShotId = 0;
		uint64 Seed = 0;                       // noise seed of the continuous layers (shot hash x voice)
		TArray<FImpactEvent> Impacts;          // sorted by ShotTime
		TArray<FContinuousSegment> Continuous; // sorted by StartTime
		FPresentationGainPtr Presentation;     // table stem envelope (null: gain 1)
		double OutputGain = 1.0;               // 1 / P_fs(mode) x pan compensation: Pa -> digital
	};
}
