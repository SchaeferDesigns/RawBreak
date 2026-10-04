#pragma once

// Types of the body rig API (Docs/ue-architecture.md 19.3 / 19.4). FROZEN for M3-O (the opponent's body) and M3-G (pointing);
// additions allowed, renames / signature changes only with the consumers' sign-off. Plain data, no UObjects. Owner: M3-H.

#include "CoreMinimal.h"

#include "Core/RbTypes.h"

#include "RbBodyTypes.generated.h"

// First person: the player's own body; the head and neck are hidden from the owner's camera but still cast shadows and appear in
// reflections (ue5-realism-plan 5.1). Third person: everything visible (the AI opponent).
UENUM(BlueprintType)
enum class ERbBodyView : uint8
{
	FirstPerson,
	ThirdPerson,
};

UENUM(BlueprintType)
enum class ERbHandedness : uint8
{
	Right, // right-handed shooter: left bridge hand, right grip hand
	Left,
};

// What the body is doing (GetActivity; OnActivityFinished reports the end of the timed ones).
UENUM(BlueprintType)
enum class ERbBodyActivity : uint8
{
	Idle,         // standing, the cue held (ARbCue drive "Body", M3-H)
	Walking,      // locomotion from the owner's velocity
	GettingDown,  // into the stance (BeginStance)
	Down,         // in the stance: bridge planted, grip on the butt; the cue pose (SampleHand + displacement) drives the grip
	StandingUp,   // EndStance
	Chalking,     // PlayChalk
	WipingHands,  // PlayWipeHands (HF-17)
	RollingCue,   // PlayRollCue (HF-30 / HF-31 roll test)
	CarryingBall, // SetCarriedBall (ball in hand, a picked-up ball)
	Gesture,      // PlayGesture
	Pointing,     // SetPointTarget active (the diegetic call)
};

UENUM(BlueprintType)
enum class ERbBodyGesture : uint8
{
	Nod,       // acknowledges a call / a decision
	PointAt,   // points the cue tip at TargetWorld (the AI's own call)
	ShakeHead,
	WaveAside, // "mind stepping back?" (HF-77)
	Shrug,
};

// The body's look (the opponent's variant from M3-O's roster; the player's own body).
struct FRbBodyAppearance
{
	bool bQuinn = false;                                          // the template's Quinn instead of Manny
	FLinearColor Shirt = FLinearColor(0.025f, 0.025f, 0.03f);     // plain dark long-sleeve shirt (linear)
	FLinearColor Trousers = FLinearColor(0.03f, 0.04f, 0.07f);    // dark jeans
	FLinearColor Skin = FLinearColor(0.45f, 0.30f, 0.22f);
	double HeightCm = 178.0;
};

// One address: the shot in WORLD space and the stance fields of the stroke situation (what ExecuteStroke uses).
struct FRbStanceRequest
{
	FVector CueBallWorld = FVector::ZeroVector;           // cue-ball centre
	double BallRadiusCm = 2.8575;
	FVector AimDirectionWorld = FVector::ForwardVector;  // horizontal aim (butt -> tip)
	double ElevationDeg = 0.0;
	ERbBridgeType Bridge = ERbBridgeType::Closed;        // the player's from the bridge solver, the AI's from the planner
	double BridgeLengthCm = 20.0;                        // L_b, bridge to tip (StrokeSituation::BridgeLength)
	double BridgeToGripCm = 80.0;                        // L_bg (StrokeSituation::BridgeToGrip)
	ERbHandedness Handedness = ERbHandedness::Right;
	uint64 Seed = 0;                                     // stance variation per address (bitwise reproducible)
};

// The human state the hands show (human-factors HF-05 / 12 / 14 / 17), refreshed every frame while down.
struct FRbBodyHumanState
{
	double Pressure = 0.0;      // StrokeSituation::Pressure [0, 1]
	double TremorRightCm = 0.0; // HandPose tremor at the tip (the bridge hand shows its share)
	double TremorUpCm = 0.0;
	double GripTension = 0.0;   // [0, 1] white knuckles (HandPose per-shot grip tension)
	double BridgeSlip = 0.0;    // [0, 1] creeping bridge fingers on a stroke above the slip speed V_b
	double Sweat = 0.0;         // [0, 1] the urge to wipe the hands
};
