#pragma once

// The camera IS the eyes (playtest 2026-09-28 P5; ue5-realism-plan 4.8; Docs/ue-architecture.md 18.3): the procedural human
// head / body motion layer of the first-person view. It replaces M1's mechanical smoothstep dolly between the standing and the
// down-on-the-shot eye with a HUMAN movement, and adds the continuous micro-motion of a living head.
//
//   Posture changes     get down / stand up / lean over for the ball in hand as a body movement: hip hinge (the head travels
//                       forward-down on an arc, not on the straight line), weight shift toward the bridge-hand side, the head
//                       leading the body (rotation ~80-150 ms ahead of the translation), asymmetric timing (getting down slower
//                       with a long settle; standing up faster at the start), a small overshoot below the final eye height and a
//                       damped settle (2-3 Hz, zeta ~0.6); every parameter varies a little from one movement to the next, SEEDED
//                       (never two identical get-downs, reproducible in tests and replays).
//   Continuous layer    breathing (0.2-0.33 Hz, faster and deeper under pressure), postural sway (0.1-0.5 Hz; standing ~4.5 mm,
//                       down ~1 mm with the bridge hand as the third support), walking bob / sway with gaze stabilisation and a
//                       footstep event per foot contact (URbCameraRigComponent::OnFootstep -> audio), a pressure-driven head
//                       tremor share; Settle (HF-06) calms breathing and sway by ~70 % over 1-2 s.
//   Reactions           after the contact: the head leans / turns slightly to follow the cue ball and the object ball with human
//                       latency (smooth pursuit after ~150-200 ms, never a mouse-driven jump - P1 stays true); a small flinch
//                       (50-80 ms, a few mm back, a fraction of a degree) on a loud impact (the break), scaled by the loudness.
//   Presets             Eyes: vestibulo-ocular stabilisation (head rotation from bob / breathing removed, the view stays on the
//                       gaze target; translation x HeadTranslationScale 0.3 for the continuous layer; posture changes keep their
//                       full, human trajectory). Headcam: nothing stabilised (full translation and rotation) + mount jitter.
//   Human factors       inputs from the stroke context: StrokeSituation::Pressure (HF-15) raises breathing rate / depth and
//                       tremor; Settle lowers them; fatigue / intoxication hooks (0 in M2).
//   Comfort             everything x MotionScale (URbGameUserSettings HeadBobScale; 0 with Reduced motion): posture changes then
//                       fall back to a short plain ease (no overshoot, no sway).
//
// Pure maths (no UObjects): the rig (URbCameraRigComponent) owns one instance and composes it with the mode poses. Deterministic:
// a function of the inputs, the seeds and the integrated time; frame-rate independent (sub-stepped springs).
// Owner: M2-F (stub by the M2 architect step; TODO(M2-F) marks the parts to implement). FRbHeadMotion (M1) is folded into it.

#include "CoreMinimal.h"

#include "Camera/RbCameraModel.h"

enum class ERbPostureChange : uint8
{
	GetDown,      // standing -> down on the shot
	StandUp,      // down -> standing
	LeanOver,     // standing -> ball-in-hand lean over the table
	StraightenUp, // ball-in-hand lean -> standing
};

// Per-frame inputs of the continuous layer.
struct FRbHumanMotionInputs
{
	double DeltaSeconds = 0.0;
	double WalkSpeedMps = 0.0;   // horizontal pawn speed
	bool bDown = false;          // down on the shot (bridge hand on the cloth)
	bool bWatching = false;      // after the contact, still down
	double Pressure = 0.0;       // StrokeSituation::Pressure of the active stroke context (0 = practice, 1 = full pressure)
	double SettleAlpha = 0.0;    // 0..1 progress of the Settle (HF-06)
	double MotionScale = 1.0;    // comfort (0 = off)
	ERbCameraPreset Preset = ERbCameraPreset::Eyes;
	FVector GazeTargetWorld = FVector::ZeroVector; // what the eyes fixate (stabilisation); zero = none
	bool bHasGazeTarget = false;
};

// Output: added to the rig's mode pose.
struct FRbHumanMotionSample
{
	FVector Offset = FVector::ZeroVector;     // head translation in the yaw-only view frame [cm]: X forward, Y right, Z up
	FRotator Rotation = FRotator::ZeroRotator; // head rotation not removed by the stabilisation (Headcam, reactions)
	bool bFootstep = false;                   // a foot touched the ground during this step
	bool bLeftFoot = false;
};

class RAWBREAK_API FRbHumanMotion
{
public:
	// Starts a posture change between two eye poses (world). Seed = hash of (match seed, shooter shot index, address index,
	// posture-change counter): the same seed gives the same movement, a different one a slightly different movement.
	void BeginPostureChange(ERbPostureChange Change, const FTransform& FromEye, const FTransform& ToEye, uint64 Seed);

	// Eye pose of the running posture change at Seconds since its start (the target may move: ToEye is re-read every frame).
	// Returns false when the change (incl. its settle) has finished; OutEye is then ToEye.
	bool EvaluatePostureChange(double Seconds, const FTransform& ToEye, FTransform& OutEye) const;

	// Duration of the running change (incl. the settle) [s].
	double GetPostureChangeSeconds() const { return ChangeSeconds; }

	// Continuous layer.
	FRbHumanMotionSample Step(const FRbHumanMotionInputs& Inputs, const FRbCameraPresetParams& Params);

	// A loud impact heard by the player (Loudness 0..1: the break ~1, a soft click ~0), from the playback's shot events.
	void NotifyImpact(double Loudness);

	void Reset();

private:
	ERbPostureChange Change = ERbPostureChange::GetDown;
	FTransform ChangeFrom;
	uint64 ChangeSeed = 0;
	double ChangeSeconds = 0.0;
	double Time = 0.0;
	double PendingFlinch = 0.0;
};
