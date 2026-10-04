#pragma once

// The body rig (Docs/ue-architecture.md 19.4): one full body at real scale per person - the first-person player's own body and the
// AI opponent's in third person - on the Epic template mannequin (Manny / Quinn, UE EULA; MetaHuman later behind this API), posed by
// a pure C++ solver (stance, head, two-bone arm IK, authored hand poses; no Control Rig graphs). Contract (19.2 / 19.3):
//   * The body FOLLOWS: the eye (the camera rig's human motion: the head leads, the body follows) and the cue pose (SampleHand + the
//     cue displacement: what you see is what hits). It never writes the cue pose while down or the eye.
//   * The bridge hand is planted at the seat of the physics cue axis (L_b from the tip) on the cloth or the rail cap; no body part
//     below the cloth, inside a rail or closer than 5 mm to a ball (HF-60).
//   * Chalking changes the tip state only through the visible twists: OnChalkTwist per twist, the owner calls
//     URbMatchDirector::ChalkTip for it.
// API FROZEN for M3-O (the opponent) and M3-G (SetPointTarget); additions allowed. Owner: M3-H. Stub of the plan step: stores the
// inputs and idles (TODO(M3-H)); every call is safe without a mesh, a table or a cue (tests, headless).

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "Body/RbBodyTypes.h"
#include "Camera/RbCameraRigComponent.h"
#include "Core/RbTypes.h"

#include "RbBodyRigComponent.generated.h"

class ARbCue;
class ARbTable;

DECLARE_MULTICAST_DELEGATE(FRbOnBodyStanceReady);
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnBodyActivityFinished, ERbBodyActivity /*Activity*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnBodyChalkTwist, int32 /*TwistIndex*/);

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbBodyRigComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	URbBodyRigComponent();

	// --- setup -----------------------------------------------------------------------------------------------------------------
	void SetView(ERbBodyView InView);
	ERbBodyView GetView() const { return View; }
	void SetAppearance(const FRbBodyAppearance& InAppearance);
	const FRbBodyAppearance& GetAppearance() const { return Appearance; }
	void SetHandedness(ERbHandedness InHandedness);
	ERbHandedness GetHandedness() const { return Handedness; }
	// The table (cloth plane, rails, balls: finger contacts and clearances) and the cue in the hands.
	void SetTable(ARbTable* InTable);
	void SetCue(ARbCue* InCue);
	ARbTable* GetTable() const { return Table.Get(); }
	ARbCue* GetCue() const { return Cue.Get(); }

	// --- head ------------------------------------------------------------------------------------------------------------------
	// First person: the camera rig's eye this frame (world). The head is placed so the camera sits at the preset's eye point (Eyes:
	// between the eyes; Headcam: the mount on the head); the torso follows the head with a human lag.
	void SetEyeTransform(const FTransform& EyeWorld, ERbCameraPreset Preset);
	// Third person: where the head looks (studying the table, watching a shot).
	void SetLookTarget(const FVector& TargetWorld, bool bActive);

	// --- the shot --------------------------------------------------------------------------------------------------------------
	// Into the stance for one address (GettingDown -> Down; OnStanceReady when the bridge hand is planted).
	void BeginStance(const FRbStanceRequest& Request);
	// Out of the stance (StandingUp -> Idle).
	void EndStance();
	bool IsInStance() const { return bInStance; }
	const FRbStanceRequest& GetStance() const { return Stance; }
	// The hands' human state (every frame while down).
	void SetHumanState(const FRbBodyHumanState& State);
	const FRbBodyHumanState& GetHumanState() const { return HumanState; }

	// --- chores and gestures (timed; OnActivityFinished at the end) -----------------------------------------------------------
	// Picks up the chalk cube (nullptr: the nearest RbChalkCube-tagged actor, else a cube spawned on the nearest rail), twists Twists
	// times (OnChalkTwist per twist), puts it back. False if the body is busy or Twists <= 0.
	bool PlayChalk(int32 Twists, AActor* ChalkCube = nullptr);
	bool PlayWipeHands();
	bool PlayRollCue();
	bool PlayGesture(ERbBodyGesture Gesture, const FVector& TargetWorld = FVector::ZeroVector);
	// The cue tip points at a world point while active (the diegetic call: M3-G sets it while C is held).
	void SetPointTarget(const FVector& TargetWorld, bool bActive);
	// The right hand holds a ball (centre, radius) while bCarrying (ball in hand, a picked-up ball).
	void SetCarriedBall(const FVector& BallCentreWorld, double RadiusCm, bool bCarrying);

	// --- state -----------------------------------------------------------------------------------------------------------------
	ERbBodyActivity GetActivity() const { return Activity; }
	bool IsBusy() const;
	// Where the cue sits in the bridge / where the grip hand holds it (world). False when not in a stance.
	bool GetBridgeSeat(FVector& OutWorld) const;
	bool GetGripPoint(FVector& OutWorld) const;

	FRbOnBodyStanceReady OnStanceReady;
	FRbOnBodyActivityFinished OnActivityFinished;
	FRbOnBodyChalkTwist OnChalkTwist;
	FRbOnFootstep OnFootstep; // third-person locomotion (the opponent's footsteps; the player's come from the camera rig)

	// UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	ERbBodyView View = ERbBodyView::FirstPerson;
	FRbBodyAppearance Appearance;
	ERbHandedness Handedness = ERbHandedness::Right;
	ERbBodyActivity Activity = ERbBodyActivity::Idle;
	FRbStanceRequest Stance;
	FRbBodyHumanState HumanState;
	bool bInStance = false;
	FTransform EyeWorld = FTransform::Identity;
	ERbCameraPreset EyePreset = ERbCameraPreset::Eyes;
	FVector LookTarget = FVector::ZeroVector;
	bool bHasLookTarget = false;
	FVector PointTarget = FVector::ZeroVector;
	bool bPointing = false;
	FVector CarriedBall = FVector::ZeroVector;
	double CarriedBallRadiusCm = 0.0;
	bool bCarrying = false;
	TWeakObjectPtr<ARbTable> Table;
	TWeakObjectPtr<ARbCue> Cue;
};
