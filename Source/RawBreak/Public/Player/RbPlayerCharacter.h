#pragma once

// The first-person player (Docs/ue-architecture.md 6.1). M1: capsule + CharacterMovement to walk around the table
// (the table blocks the pawn), a UCineCameraComponent driven by URbCameraRigComponent, the stroke state machine
// URbStrokeComponent. NO body / hands yet (MetaHuman comes later from the user; plan 5.1: world-space body, camera
// in a head socket). Hot-seat: both players use this one pawn; the director switches the active player. Owner: UE-5b.
//
// Input routing (actions of the controller's URbInputSetup, bindings per its trigger conventions):
//   Move            walking only while the eye is up (rig Standing / BallInHand); never while down on the shot
//   Look            walking / ball in hand: control rotation (the placement point follows the view)
//                   getting down / down: the stroke component's aim (azimuth; FineAim x0.2) and, while no stroke source is
//                   active, the eyes' pitch along the line (rig gaze); a held Stroke makes the mouse the stroke
//                   contact / watching / locked while down: head look (rig gaze yaw + pitch) to follow the balls
//   GetDown         stroke component toggle (get down / stand up; stand up after the shot); locked while down: stand up
//   Stroke, Commit, Settle, FineAim   held: Triggered = true, Completed = false (Settle also drives the rig's settle)
//   Elevation, TipOffset             stroke component
//   Confirm         ball in hand: place (stroke component); otherwise the director's Confirm (decision / next rack / new match)
// Movement: 1.4 m/s walk with a human acceleration, no jumping, no crouching; the capsule (r 25 cm, half height 88 cm) is
// blocked by the table's complex-as-simple collision and a 20 cm step height keeps it from stepping onto the rails.

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "RbPlayerCharacter.generated.h"

class UCineCameraComponent;
class UInputComponent;
class URbCameraRigComponent;
class URbInputSetup;
class URbStrokeComponent;
struct FInputActionValue;

UCLASS()
class RAWBREAK_API ARbPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARbPlayerCharacter();

	UCineCameraComponent* GetCamera() const { return Camera; }
	URbCameraRigComponent* GetCameraRig() const { return CameraRig; }
	URbStrokeComponent* GetStroke() const { return Stroke; }

	// Walking speed [cm/s] (1.4 m/s normal walk).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Player")
	float WalkSpeed = 140.0f;

	// Degrees of head / eye rotation per unit of the Look action (the controller's legacy look scale: mouse counts x 0.07 x 2.5).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Player")
	float GazeDegreesPerLookUnit = 2.5f;

	// True while the player may walk (the eye is up; down on the shot the feet stay planted).
	bool CanWalk() const;

	// Input routing (the Enhanced Input handlers call these; tests and dev tools call them directly).
	void HandleMove(const FVector2D& Axis);
	void HandleLook(const FVector2D& Axis);
	void HandleGetDown();
	void HandleConfirm();
	void HandleSettle(bool bHeld);

	// Fixation point of the eyes down on the shot (the rig's FixationResolver): the first ball of the director's table state on the
	// cue ball's line, else where the line meets the cushions. False without a match. World space.
	bool FindFixationPoint(const FVector& ContactWorld, const FVector& DirectionWorld, FVector& OutWorld) const;

	// ACharacter
	virtual void BeginPlay() override;
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

	bool bFineAim = false;
};
