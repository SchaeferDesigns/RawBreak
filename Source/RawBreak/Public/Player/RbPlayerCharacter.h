#pragma once

// The first-person player (Docs/ue-architecture.md 6.1). M1: capsule + CharacterMovement to walk around the table
// (the table blocks the pawn), a UCineCameraComponent driven by URbCameraRigComponent, the stroke state machine
// URbStrokeComponent, and (M2-F) the carrying hand of the diegetic ball in hand, URbBallInHandComponent. No body yet
// (MetaHuman comes later from the user; plan 5.1: world-space body, camera in a head socket; the carrying hand is the M2 stand-in).
// Hot-seat: both players use this one pawn; the director switches the active player. Owner: UE-5b, M2-F.
//
// Input routing (actions of the controller's URbInputSetup, bindings per its trigger conventions). The Look action carries RAW
// mouse counts (M2-F, P3: DefaultInput.ini + ARbPlayerController neutralise the engine's axis scale); every speed is per cm of mouse
// travel through RbAimResponse with the stroke component's MouseDpi / Controls (URbGameUserSettings):
//   Move            walking only while the eye is up (rig Standing / BallInHand); never while down on the shot
//   Look            walking: control rotation, LookDegreesPerCm x LookSensitivity (invert Y);
//                   ball in hand: the same (the carried ball follows the look point); with FineAim (Shift) the view stays and
//                   the hand nudges the ball (URbBallInHandComponent::AddFineAdjustCm, the aim's fine factor)
//                   getting down / down: the stroke component's aim (azimuth, 7.2 deg/cm; Shift x FineAimFactor) and, while no
//                   stroke source is active, the eyes' pitch along the line (rig gaze, half the look speed); a held Stroke makes
//                   the mouse the stroke (also pressed during the get-down, before the stroke starts at Down, and a button held
//                   through a pause that must be released first: no aim, no eye pitch)
//                   contact / watching while down: head look (rig gaze yaw + pitch) THROUGH THE LOOK INTENT GATE (P1): nothing
//                   while the Stroke button is held, a quiet period after the contact / release, then only a deliberate move
//                   beyond the dead zone opens it with a fade-in (FRbLookIntentGate); the rig's head follower smooths it
//                   locked while down without a contact (scripted strike, decision): head look as before
//   GetDown         stroke component toggle (get down / stand up; stand up after the shot = a human StandUp); locked while down:
//                   stand up
//   Stroke, Commit, Settle, FineAim   held: Triggered = true, Completed = false (Settle also drives the rig's settle; Stroke also
//                   feeds the look gate)
//   Elevation, TipOffset             stroke component
//   Confirm         ball in hand: set the ball down (stroke component -> carrying hand); otherwise interactions, then the director's
//                   Confirm (decision / next rack / new match)
// Watching the shot the rig's reactions follow the playing cue ball / first object ball (ReactionResolver) and a loud impact
// (a ball-ball event of the playing shot above ~3 m/s) makes the head flinch.
// Movement: 1.4 m/s walk with a human acceleration, no jumping, no crouching; the capsule (r 25 cm, half height 88 cm) is
// blocked by the table's complex-as-simple collision and a 20 cm step height keeps it from stepping onto the rails.

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "Player/RbLookIntentGate.h"

#include "RbPlayerCharacter.generated.h"

class ARbBallSet;
class UCineCameraComponent;
class UInputComponent;
class URbBallInHandComponent;
class URbCameraRigComponent;
class URbInputSetup;
class URbShotPlaybackComponent;
class URbStrokeComponent;
struct FInputActionValue;
struct FRbShot;
struct FRbStrokeCommit;

UCLASS()
class RAWBREAK_API ARbPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARbPlayerCharacter();

	UCineCameraComponent* GetCamera() const { return Camera; }
	URbCameraRigComponent* GetCameraRig() const { return CameraRig; }
	URbStrokeComponent* GetStroke() const { return Stroke; }
	URbBallInHandComponent* GetBallInHand() const { return BallInHand; }
	const FRbLookIntentGate& GetLookGate() const { return LookGate; }
	FRbLookIntentGate& GetLookGate() { return LookGate; }

	// Walking speed [cm/s] (1.4 m/s normal walk).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Player")
	float WalkSpeed = 140.0f;

	// The eyes' pitch along the line while aiming, relative to the look speed (0.5 = half).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Player")
	float AimGazePitchScale = 0.5f;

	// The head look while still down after the contact (through the look intent gate), relative to the look speed: bent over the
	// table the head turns slower (0.3 x 22 = 6.6 deg/cm, about the aim speed), and the rig's head follows it heavily (P1: calm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Player")
	float WatchLookScale = 0.3f;

	// Limits of the standing view pitch [deg].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Player")
	float MaxLookPitchDeg = 85.0f;

	// True while the player may walk (the eye is up; down on the shot the feet stay planted).
	bool CanWalk() const;

	// Input routing (the Enhanced Input handlers call these; tests and dev tools call them directly). HandleLook takes RAW mouse
	// counts (+X right, +Y forward / up) of one frame; DeltaSeconds = that frame's time (< 0: the world's delta; only the optional
	// aim acceleration uses it).
	void HandleMove(const FVector2D& Axis);
	void HandleLook(const FVector2D& Counts, double DeltaSeconds = -1.0);
	void HandleGetDown();
	void HandleConfirm();
	void HandleSettle(bool bHeld);
	void HandleStroke(bool bHeld);
	void HandleFineAim(bool bHeld) { bFineAim = bHeld; }
	// Tests / dev tools: presses or releases the Stroke action's key through the player controller's input, so Enhanced Input sends
	// its real events (Triggered while held, the synthetic Completed on a paused frame, Started again on resume). False without a
	// local player controller or input setup.
	bool InjectStrokeKey(bool bDown);

	// Mouse counts that turn the view / aim by Degrees in the current routing (dev commands, tests): X = yaw (aim azimuth while
	// down), Y = pitch.
	FVector2D LookCountsForDegrees(const FVector2D& Degrees) const;

	// The reaction target while watching the playing shot: between the cue ball and the first object ball it hit (world).
	bool FindReactionTarget(FVector& OutWorld) const;

	// Fixation point of the eyes down on the shot (the rig's FixationResolver): the first ball of the director's table state on the
	// cue ball's line, else where the line meets the cushions. False without a match. World space.
	bool FindFixationPoint(const FVector& ContactWorld, const FVector& DirectionWorld, FVector& OutWorld) const;

	// ACharacter
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	// Enhanced Input handlers (actions from the controller's URbInputSetup).
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnGetDown(const FInputActionValue& Value);
	void OnStroke(const FInputActionValue& Value);
	void OnCommit(const FInputActionValue& Value);
	void OnElevation(const FInputActionValue& Value);
	void OnTipOffset(const FInputActionValue& Value);
	void OnFineAim(const FInputActionValue& Value);
	void OnSettle(const FInputActionValue& Value);
	void OnConfirm(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Player")
	TObjectPtr<UCineCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Player")
	TObjectPtr<URbCameraRigComponent> CameraRig;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Player")
	TObjectPtr<URbStrokeComponent> Stroke;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Player")
	TObjectPtr<URbBallInHandComponent> BallInHand;

	void OnStrokeContactForFeel(const FRbStrokeCommit& Commit);
	void BindShotEvents();
	void OnShotEventForFeel(const TSharedRef<const FRbShot>& Shot, int32 EventIndex);

	bool bFineAim = false;
	bool bStrokeButtonHeld = false;
	FRbLookIntentGate LookGate;
	FDelegateHandle ContactHandle;
	FDelegateHandle ShotEventHandle;
	TWeakObjectPtr<URbShotPlaybackComponent> BoundPlayback;
	TWeakObjectPtr<ARbBallSet> BoundBalls; // the player's ball set of the watched shot (bound at the contact)
};
