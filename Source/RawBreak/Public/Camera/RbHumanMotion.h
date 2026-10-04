#pragma once

// The camera IS the eyes (playtest 2026-09-28 P5; ue5-realism-plan 4.8; Docs/ue-architecture.md 18.3): the procedural human
// head / body motion layer of the first-person view. It replaces M1's mechanical smoothstep dolly between the standing and the
// down-on-the-shot eye with a HUMAN movement, and adds the continuous micro-motion of a living head. M1's FRbHeadMotion (walking
// bob, breathing, postural sway) is folded into the continuous layer.
//
// POSTURE CHANGES (GetDown, StandUp, LeanOver, StraightenUp) - an analytic function of the time since the start, so the path does
// not depend on the frame rate; the target (ToEye) may move and is re-read every frame:
//   hip hinge      the head travels on an arc: getting down, the horizontal part leads the vertical one by ArcLead (the torso
//                  pivots at the hip: forward first, then down); standing up the vertical part leads (up first, then back);
//   weight shift   a sideways bump toward the bridge-hand side (left for a right-hander) that returns to 0 at the end, plus a
//                  small seeded wobble, so no two paths are the same;
//   head lead      the view rotation runs HeadLead (80-150 ms) ahead of the translation's leading part;
//   timing         minimum-jerk progress per channel (rotation, leading, lagging translation: the same duration, staggered by
//                  HeadLead and ArcLead, so each starts and ends with zero velocity - no pop, no kink); getting down is slower
//                  (0.90-1.05 s) with a long settle; standing up is faster at the start (time-warped progress, 0.85-1.05 s);
//   overshoot      the vertical motion overshoots the final eye height by 4-12 mm (getting down: below it) and settles as a damped
//                  oscillator (2.2-3.0 Hz, zeta 0.55-0.7) that starts at the overshoot peak with zero velocity (C1 path);
//   seed           every parameter varies per movement from Seed (hash of match seed, shooter shot index, address index, change
//                  counter): never two identical get-downs, bitwise reproducible.
//   Style Quick = a plain 0.4 s smoothstep (no arc, no overshoot); Cut = instant (settings PostureTransition, Reduced motion).
//
// CONTINUOUS LAYER (Step, integrated phases; springs sub-stepped at a fixed internal rate -> frame-rate independent):
//   breathing      0.22-0.30 Hz and 1.0-1.6x deeper as StrokeSituation::Pressure goes 0 -> 1 (inside the 0.2-0.33 Hz band);
//                  head amplitude standing 2.5 mm, down 1.5 mm (FRbHeadMotionParams);
//   postural sway  a band-limited (0.1-0.5 Hz) sum of seeded sines per axis, 2D RMS ~4.5 mm standing, ~1 mm down;
//   walking        vertical bob at the step rate f = 1.4 + 0.45 v Hz (4.5 cm p-p at 1.4 m/s, head lowest at the heel strike),
//                  lateral sway at the stride rate toward the stance foot, a footstep (bFootstep, bLeftFoot) at every heel strike
//                  while walking (the rig fires URbCameraRigComponent::OnFootstep);
//   tremor         a pressure-driven head tremor share (~8.7 Hz, 0.15 mm at full pressure);
//   settle         breathing, sway and tremor x (1 - SettleReduction x SettleAlpha): -70 % once the Settle has run;
//   reactions      watching the shot: the head turns FollowFraction of the way toward the reaction target (cue ball / first object
//                  ball, angles given by the rig) with a latency of 150-200 ms and a critically damped smooth pursuit (no jump,
//                  never mouse-driven: P1 stays true), leaning a little toward it; a loud impact (NotifyImpact, the break) gives a
//                  flinch: the head goes back a few mm and pitches up a fraction of a degree, peaking after 65 ms;
//   presets        Eyes: rotation from bob / breathing / sway is not produced (the rig keeps the gaze on the fixation point, VOR),
//                  continuous translation x HeadTranslationScale (0.3); Headcam: full translation, nod / roll with the bob and
//                  sway (no VOR) + mount jitter x MountShakeScale; reactions and the flinch are deliberate and never stabilised;
//   comfort        bob x HeadBobScale, breathing / sway / tremor / reactions / flinch x BodySwayScale, everything x MotionScale
//                  (0 with Reduced motion: all zero).
// The motion layer moves only the eye: the cue pose stays SampleHand (what you see is what hits).
//
// Pure maths (no UObjects): the rig (URbCameraRigComponent) owns one instance and composes it with the mode poses. Deterministic:
// a function of the inputs, the seeds and the integrated time. Owner: M2-F. Tests: RawBreak.Unit.HumanMotion.* (F6, F7).

#include "CoreMinimal.h"

#include "Camera/RbCameraModel.h"
#include "Settings/RbSettingsTypes.h"

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
	// --- M2-F additions ------------------------------------------------------------------------------
	double HeadBobScale = 1.0;    // settings HeadBobScale (walking bob)
	double BodySwayScale = 1.0;   // settings FRbCameraSettings::BodySwayScale (breathing, sway, tremor, reactions)
	double MountShakeScale = 1.0; // settings FRbCameraSettings::MountShakeScale (Headcam mount jitter)
	// Reaction target while watching: its direction relative to the base view [deg] (+ yaw = right, + pitch = up), measured from
	// the direction it had when watching began (so the reaction starts at 0). Ignored unless bWatching && bHasReactionTarget.
	bool bHasReactionTarget = false;
	FVector2D ReactionAnglesDeg = FVector2D::ZeroVector;
};

// Output: added to the rig's mode pose.
struct FRbHumanMotionSample
{
	FVector Offset = FVector::ZeroVector;     // head translation in the yaw-only view frame [cm]: X forward, Y right, Z up
	FRotator Rotation = FRotator::ZeroRotator; // head rotation not removed by the stabilisation (Headcam, reactions)
	bool bFootstep = false;                   // a foot touched the ground during this step
	bool bLeftFoot = false;
	// --- M2-F additions (tests, dev dump) --------------------------------------------------------------
	FVector StabilisedOffset = FVector::ZeroVector; // the part of Offset the Eyes compensate by VOR (bob, breathing, sway, tremor)
	FRotator Reaction = FRotator::ZeroRotator;      // the reaction + flinch part of Rotation
	double BreathRateHz = 0.0;
	double BreathDepthScale = 0.0;                  // x the preset's breathing amplitude (pressure, settle, comfort)
};

// The seeded parameters of one posture change (tests, the trace plots).
struct FRbPostureParams
{
	ERbPostureChange Change = ERbPostureChange::GetDown;
	ERbPostureTransition Style = ERbPostureTransition::Natural;
	double MainSeconds = 1.0;      // minimum-jerk main movement
	double WarpExponent = 1.0;     // < 1: faster start (standing up)
	double ArcLeadSeconds = 0.0;   // the leading translation part runs ahead of the other one
	double HeadLeadSeconds = 0.0;  // the view rotation runs ahead of the leading translation part
	double OvershootCm = 0.0;      // beyond the final eye height in the direction of the vertical motion
	double SettleHz = 2.5;
	double SettleZeta = 0.6;
	double WeightShiftCm = 0.0;    // toward the bridge-hand side (left, - view-right) at mid-movement
	double WobbleCm = 0.0;         // seeded lateral wobble
	double WobblePhase = 0.0;
	double TotalSeconds = 1.0;     // incl. the settle (the envelope below 0.5 mm)
};

class RAWBREAK_API FRbHumanMotion
{
public:
	// Starts a posture change between two eye poses (world). Seed = hash of (match seed, shooter shot index, address index,
	// posture-change counter): the same seed gives the same movement, a different one a slightly different movement.
	void BeginPostureChange(ERbPostureChange Change, const FTransform& FromEye, const FTransform& ToEye, uint64 Seed,
		ERbPostureTransition Style = ERbPostureTransition::Natural);

	// Eye pose of the running posture change at Seconds since its start (the target may move: ToEye is re-read every frame).
	// Returns false when the change (incl. its settle) has finished; OutEye is then ToEye.
	bool EvaluatePostureChange(double Seconds, const FTransform& ToEye, FTransform& OutEye) const;

	// Duration of the running change (incl. the settle) [s].
	double GetPostureChangeSeconds() const { return ChangeSeconds; }
	// Time at which the eye first arrives at the target pose (end of the main movement; the settle continues) [s]. The stroke
	// component's Down phase starts then.
	double GetPostureArrivalSeconds() const { return Posture.Style == ERbPostureTransition::Natural ? Posture.MainSeconds : ChangeSeconds; }
	const FRbPostureParams& GetPostureParams() const { return Posture; }

	// The seeded parameters a posture change with this seed and style would use (pure).
	static FRbPostureParams MakePostureParams(ERbPostureChange Change, uint64 Seed, ERbPostureTransition Style);

	// Progress functions of a posture change at Seconds (0 -> 1, the vertical may overshoot): horizontal translation, vertical
	// translation (fraction of the vertical distance, without the overshoot), rotation. Exposed for the tests and plots.
	static void EvaluateProgress(const FRbPostureParams& P, double Seconds, double& OutHorizontal, double& OutVertical, double& OutRotation,
		double& OutOvershootFraction);

	// Continuous layer.
	FRbHumanMotionSample Step(const FRbHumanMotionInputs& Inputs, const FRbCameraPresetParams& Params);

	// A loud impact heard by the player (Loudness 0..1: the break ~1, a soft click ~0), from the playback's shot events.
	void NotifyImpact(double Loudness);

	// Clears the reaction state (a new watch starts at the current view).
	void ResetReaction();

	void Reset();

	// Walking bob peak-to-peak [cm] at a speed (plan 4.8 table: 3 cm at 0.8 m/s, 4.5 cm at 1.4 m/s, linear to 0 at rest).
	static double BobPeakToPeakCm(double SpeedMps, const FRbHeadMotionParams& Params);
	// Breathing rate [Hz] and depth factor at a pressure (0..1).
	static double BreathRateHz(double Pressure, const FRbHeadMotionParams& Params);
	static double BreathDepthFactor(double Pressure);

	double GetTime() const { return Time; }
	double GetStepPhase() const { return StepPhase; }
	double GetSmoothedSpeed() const { return SmoothedSpeed; }

private:
	void StepReaction(double Dt, const FRbHumanMotionInputs& Inputs, const FRbHeadMotionParams& P);

	// Posture change.
	ERbPostureChange Change = ERbPostureChange::GetDown;
	FRbPostureParams Posture;
	FTransform ChangeFrom;
	uint64 ChangeSeed = 0;
	double ChangeSeconds = 0.0;

	// Continuous layer.
	double Time = 0.0;
	double StepPhase = 0.0;      // integrated step phase [rad] (heel strike at multiples of 2 pi)
	int64 StepCount = 0;         // heel strikes so far (left / right alternate)
	double SmoothedSpeed = 0.0;  // [m/s] the gait follows the walking speed with a 0.25 s lag
	double BreathPhase = 0.0;    // integrated [rad]
	bool bStarted = false;
	double DownBlend = 0.0;      // 0 standing .. 1 down, eased (amplitudes never jump)
	double SmoothedPressure = 0.0;
	double PendingFlinch = 0.0;  // loudness of an impact not yet started
	double FlinchStart = -1.0;   // Time of the running flinch (< 0 = none)
	double FlinchAmplitude = 0.0;

	// Reaction: delayed target angles and a critically damped pursuit (yaw, pitch) [deg, deg/s].
	struct FReactionSample
	{
		double Time = 0.0;
		FVector2D Angles = FVector2D::ZeroVector;
	};
	TArray<FReactionSample> ReactionHistory;
	FVector2D ReactionAngle = FVector2D::ZeroVector;
	FVector2D ReactionVelocity = FVector2D::ZeroVector;
	double ReactionStepRemainder = 0.0;
	bool bWasWatching = false;
};
