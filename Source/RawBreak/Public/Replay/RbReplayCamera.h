#pragma once

// Replay camera (Docs/ue-architecture.md 6.7): a cine camera the replay subsystem positions per ERbReplayView
// (Broadcast preset of ue5-realism-plan 4.1: long lens 25-40 deg, tripod-smooth). Later the base of the trailer
// camera kit (handheld noise, dolly, slow motion via the playback rate). Owner: UE-7.
//
// Views (UE-7; everything from the table's own geometry, so any table size / placement works):
//   Shooter   the stored eye transform at contact (FRbShotRequest::ShooterView) with the player's vertical FOV (Eyes 50 deg);
//             the subsystem passes a virtual shooter eye for scripted strikes (behind the cue ball on the strike's axis)
//   Overhead  straight down over the bed centre, screen right = the foot rail (head end on the left), BELOW the lamp
//             (ARbTable::LampUndersideHeight - 8 cm, at most 2.2 m, under any geometry a trace finds above the table) with the
//             vertical FOV that fits the whole table (+8 %); under a 1 m lamp that is a wide lens (~85 deg)
//   Rail      broadcast rail camera 1.8 m behind the head rail, 0.55 m above the cloth, 30 deg vertical, aimed down the table
//   Follow    tripod on a dolly beside the right side rail: 0.85 m above the cloth, pans after the cue ball and dollies with
//             it (critically damped: look-at 0.12 s, dolly 0.35 s), 34 deg vertical
// A camera position inside a wall is pulled in front of it (trace from the aim point, ECC_Camera, balls / table ignored).
// Lens: filmback aspect = viewport aspect, f = h_sensor / (2 tan(V/2)) (the rig's rule, review R-06); the optics of the
// preset come from URbCameraRigComponent::ApplyPresetToCamera (Eyes for Shooter, Broadcast for the others). Every view focuses
// manually at f/8 (deep focus, a TV camera in a bright hall): a replay shows the whole shot, not the eye's accommodation.

#include "CoreMinimal.h"
#include "CineCameraActor.h"

#include "Replay/RbReplaySubsystem.h"

#include "RbReplayCamera.generated.h"

class ARbTable;

// Placement of one view (world): camera transform, vertical FOV, point the camera aims / focuses at.
struct FRbReplayViewPose
{
	FTransform Transform;
	double VerticalFovDeg = 40.0;
	FVector LookAt = FVector::ZeroVector;
};

UCLASS()
class RAWBREAK_API ARbReplayCamera : public ACineCameraActor
{
	GENERATED_BODY()

public:
	ARbReplayCamera(const FObjectInitializer& ObjectInitializer);

	// Places the camera for View (Shooter uses ShooterView; Follow tracks FollowTarget each tick).
	void SetView(ERbReplayView View, const ARbTable* Table, const FTransform& ShooterView);
	void SetFollowTarget(USceneComponent* Target);

	virtual void Tick(float DeltaSeconds) override;

	// --- additions (UE-7) ----------------------------------------------------------------------------------

	// The pose of View for a table (pure geometry, no traces). FollowPoint = the tracked ball (world) for Follow.
	// OverheadHeightM > 0 caps the overhead camera's height above the cloth (a ceiling found by a trace).
	static FRbReplayViewPose ComputeViewPose(ERbReplayView View, const ARbTable& Table, const FTransform& ShooterView, const FVector& FollowPoint,
		double ShooterFovDeg, float Aspect, double OverheadHeightM = 0.0);

	ERbReplayView GetView() const { return View; }

	// Vertical FOV [deg] the Shooter view uses (the player's camera preset; the subsystem sets it from the pawn's rig).
	double ShooterFovDeg = 50.0;

	// Where the Shooter view focuses (world; the cue ball at the stroke). Zero = 1 m in front of the camera.
	FVector ShooterFocusPoint = FVector::ZeroVector;

	// Tag of every replay camera (rbue.py capture --camera RbReplayCamera views through it).
	static const FName CameraTag;

protected:
	// Lens for a vertical FOV and focus distance; bBroadcast = Broadcast preset + deep focus, else the Eyes preset.
	void ApplyLens(double VerticalFovDeg, double FocusDistanceCm, bool bBroadcast);
	float ViewportAspect() const;
	// Pulls Eye in front of the first blocking surface between LookAt and Eye.
	FVector ClampToRoom(const FVector& LookAt, const FVector& Eye) const;
	void PlaceFollow(float DeltaSeconds, bool bSnap);

	ERbReplayView View = ERbReplayView::Shooter;
	TWeakObjectPtr<USceneComponent> FollowTarget;
	TWeakObjectPtr<const ARbTable> Table;
	FVector FollowLookAt = FVector::ZeroVector;   // smoothed aim point
	FVector FollowBall = FVector::ZeroVector;     // smoothed dolly position of the ball
};
