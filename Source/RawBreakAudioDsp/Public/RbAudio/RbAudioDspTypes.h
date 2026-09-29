#pragma once

// Plain data of the table-audio DSP (Docs/specs/audio.md 3.6, 8.2, 8.3; Docs/ue-architecture.md 18.5). The plan builder of the
// RawBreak module (URbTableAudioComponent, M2-C) turns an FRbShot (rb::ShotEvent list + ball tracks) into one FVoicePlan per
// voice on a worker; the voices' generators render it on the audio thread with the per-table FShotAudioClock. Nothing here
// knows UObjects, rb:: types or the audio device.

#include "CoreMinimal.h"

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
	};

	// One impact on one voice. Times in SHOT seconds (t = 0 at the first tip contact); the clock maps them to device frames.
	struct FImpactEvent
	{
		double ShotTime = 0.0;         // [s]
		EImpactKind Kind = EImpactKind::BallBall;
		double NormalSpeed = 0.0;      // [m/s] approach speed along the contact normal
		double EffectiveMass = 0.0;    // m* [kg]
		double Stiffness = 0.0;        // Hertz K [N/m^1.5]; soft contacts: 0 and ContactTime1ms instead
		double ContactTime1ms = 0.0;   // [s] contact time at 1 m/s for soft contacts (cushion 2.5e-3, ESTIMATE)
		double Restitution = 0.95;
		double Gain = 1.0;             // listener-dependent linear gain (directivity, distance), referred to 1 m
		double DelaySeconds = 0.0;     // propagation delay (r - a) / c [s]
		int32 BankIndex = INDEX_NONE;  // structural modal bank (rail, bed, pocket, cue) or none
		uint32 Seed = 0;               // variation seed (from the shot hash: replays render identically, AU-T13)
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
		double Gain = 1.0;
	};

	// Everything one voice renders for one shot.
	struct FVoicePlan
	{
		uint64 ShotId = 0;
		TArray<FImpactEvent> Impacts;          // sorted by ShotTime
		TArray<FContinuousSegment> Continuous; // sorted by StartTime
	};
}
