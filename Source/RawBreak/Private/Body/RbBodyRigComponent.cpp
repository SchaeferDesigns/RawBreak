#include "Body/RbBodyRigComponent.h"

#include "Cue/RbCue.h"
#include "Table/RbTable.h"

// Owner: M3-H (Docs/ue-architecture.md 19.4). Plan-step stub: stores the inputs, never poses anything, never starts a timed activity
// (the chores and gestures return false), so the M2 game runs unchanged until M3-H implements the rig. Tests: RawBreak.Unit.Body.*,
// RawBreak.Functional.Body.* (M3-H).

URbBodyRigComponent::URbBodyRigComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // TODO(M3-H): tick while a body is shown
}

void URbBodyRigComponent::SetView(ERbBodyView InView)
{
	View = InView; // TODO(M3-H): owner-no-see head / neck in first person, shadows and reflections kept (19.4)
}

void URbBodyRigComponent::SetAppearance(const FRbBodyAppearance& InAppearance)
{
	Appearance = InAppearance; // TODO(M3-H): mesh (Manny / Quinn) and the body material parameters
}

void URbBodyRigComponent::SetHandedness(ERbHandedness InHandedness)
{
	Handedness = InHandedness;
}

void URbBodyRigComponent::SetTable(ARbTable* InTable)
{
	Table = InTable;
}

void URbBodyRigComponent::SetCue(ARbCue* InCue)
{
	Cue = InCue;
}

void URbBodyRigComponent::SetEyeTransform(const FTransform& InEyeWorld, ERbCameraPreset Preset)
{
	EyeWorld = InEyeWorld; // TODO(M3-H): the head follows the eye, the torso follows the head (19.4)
	EyePreset = Preset;
}

void URbBodyRigComponent::SetLookTarget(const FVector& TargetWorld, bool bActive)
{
	LookTarget = TargetWorld;
	bHasLookTarget = bActive;
}

void URbBodyRigComponent::BeginStance(const FRbStanceRequest& Request)
{
	// TODO(M3-H): GettingDown -> Down with the bridge plan; OnStanceReady when the bridge hand is planted.
	Stance = Request;
	bInStance = true;
}

void URbBodyRigComponent::EndStance()
{
	// TODO(M3-H): StandingUp -> Idle.
	bInStance = false;
}

void URbBodyRigComponent::SetHumanState(const FRbBodyHumanState& State)
{
	HumanState = State;
}

bool URbBodyRigComponent::PlayChalk(int32 Twists, AActor* ChalkCube)
{
	(void)Twists;
	(void)ChalkCube;
	return false; // TODO(M3-H): pick up the cube, OnChalkTwist per twist, put it back (19.4, H8)
}

bool URbBodyRigComponent::PlayWipeHands()
{
	return false; // TODO(M3-H): HF-17
}

bool URbBodyRigComponent::PlayRollCue()
{
	return false; // TODO(M3-H): HF-30 / HF-31 roll test
}

bool URbBodyRigComponent::PlayGesture(ERbBodyGesture Gesture, const FVector& TargetWorld)
{
	(void)Gesture;
	(void)TargetWorld;
	return false; // TODO(M3-H)
}

void URbBodyRigComponent::SetPointTarget(const FVector& TargetWorld, bool bActive)
{
	PointTarget = TargetWorld; // TODO(M3-H): the cue tip points at it
	bPointing = bActive;
}

void URbBodyRigComponent::SetCarriedBall(const FVector& BallCentreWorld, double RadiusCm, bool bInCarrying)
{
	CarriedBall = BallCentreWorld; // TODO(M3-H): the right hand holds the ball
	CarriedBallRadiusCm = RadiusCm;
	bCarrying = bInCarrying;
}

bool URbBodyRigComponent::IsBusy() const
{
	switch (Activity)
	{
	case ERbBodyActivity::GettingDown:
	case ERbBodyActivity::StandingUp:
	case ERbBodyActivity::Chalking:
	case ERbBodyActivity::WipingHands:
	case ERbBodyActivity::RollingCue:
	case ERbBodyActivity::Gesture:
		return true;
	default:
		return false;
	}
}

bool URbBodyRigComponent::GetBridgeSeat(FVector& OutWorld) const
{
	(void)OutWorld;
	return false; // TODO(M3-H)
}

bool URbBodyRigComponent::GetGripPoint(FVector& OutWorld) const
{
	(void)OutWorld;
	return false; // TODO(M3-H)
}

void URbBodyRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// TODO(M3-H): solve and apply the pose (19.4).
}
