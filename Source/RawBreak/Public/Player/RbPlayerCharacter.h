#pragma once

// The first-person player (Docs/ue-architecture.md 6.1). M1: capsule + CharacterMovement to walk around the table
// (the table blocks the pawn), a UCineCameraComponent driven by URbCameraRigComponent, the stroke state machine
// URbStrokeComponent. NO body / hands yet (MetaHuman comes later from the user; plan 5.1: world-space body, camera
// in a head socket). Hot-seat: both players use this one pawn; the director switches the active player. Owner: UE-5b.

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
