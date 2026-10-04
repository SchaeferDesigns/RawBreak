#pragma once

// Live (unscheduled) physical one-shots of audio v1 (Docs/specs/audio.md 2.2 AU-25, 2.6 AU-65; Docs/ue-architecture.md 18.5):
// sounds whose cause is not a pre-simulated shot but a game-thread event (a loose ball hitting the floor under engine physics, a
// footstep of the walking player). They are rendered at once on the game thread into pressure PCM referred to the voice's
// reference distance and handed to a positional voice (URbImpactVoiceComponent::AddLivePcm); the engine's 1 / r attenuation and
// panning place them. Pure (no UObjects), unit-tested in RawBreak.Unit.Audio.Live.*. Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbNoiseSynth.h"

struct FRbTableContext;

// A loose ball hitting the floor (or a wall / stool base: any hard plane).
struct FRbFloorHitParams
{
	double NormalSpeed = 1.0;                     // approach speed along the plane normal [m/s]
	double BallRadius = 0.028575;                 // [m]
	double BallMass = 0.170097;                   // [kg]
	RbAudio::EFloorSurface Surface = RbAudio::EFloorSurface::Vct;
	FVector PlaneNormal = FVector::UpVector;      // world, unit (points away from the floor into the room)
	FVector ContactPointCm = FVector::ZeroVector; // world
	FVector ListenerCm = FVector(0.0, 0.0, 160.0);
	double RefDistance = 0.25;                    // voice reference distance [m]
	uint64 Seed = 1;
};

namespace RbAudioLive
{
	// ESTIMATE contact of a ball on a floor surface (audio.md 2.2 AU-25: VCT on concrete T(1 m/s) 0.30 ms, e 0.55, no structure bank).
	RAWBREAK_API void FloorContact(RbAudio::EFloorSurface Surface, double& OutContactTime1, double& OutRestitution, double& OutReflection);

	// The acoustic ball of a loose ball (radius, mass, material): the ball of the table's set (exactly the plan builder's
	// FRbAudioPlanBuilder::BallAcoustics, whose radiation kernels the table's voices prewarm on a worker) when the table and the ball
	// are known, else a standard ball. Never the physics body's mass: the kernel cache is keyed by the exact values, so any other
	// mass (0.17, a float-rounded body mass) designs a new kernel set on the game thread (~30 ms per value) and grows the cache.
	RAWBREAK_API RbAudio::FBallAcoustics LooseBallAcoustics(const FRbTableContext* Context, int32 BallId);

	// Shot-independent caches of the live sounds (floor contact shapes of every surface, the standard ball's kernels) for a device
	// rate; called by FRbAudioPlanBuilder::Prewarm on its worker, so a loose ball's first floor hit computes nothing on the game thread.
	RAWBREAK_API void Prewarm(double SampleRate);

	// The impact event of a floor hit (ball radiation along the contact axis, direct path + the image in the floor plane), gains
	// referred to RefDistance, delays relative to the contact.
	RAWBREAK_API RbAudio::FImpactEvent MakeFloorHitEvent(const FRbFloorHitParams& Params, double SampleRate);

	// Renders a floor hit [Pa at RefDistance] into Out (resized; the contact starts PreFrames after Out[0]). False below 3 mm/s.
	RAWBREAK_API bool RenderFloorHit(const FRbFloorHitParams& Params, double SampleRate, TArray<float>& Out, int32 PreFrames = 64);

	// A footstep [Pa at RefDistance] (RbAudio::FFootstepSynth, referred from 1 m to RefDistance).
	RAWBREAK_API void RenderFootstep(const RbAudio::FFootstepParams& Params, double RefDistance, double SampleRate, TArray<float>& Out);
}
