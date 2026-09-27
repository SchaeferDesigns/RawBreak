#include "Player/RbPlayerCharacter.h"

#include "Camera/RbCameraRigComponent.h"
#include "Input/RbInputSetup.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"

#include "CineCameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"

// Owner: UE-5b. TODO(UE-5b): bind every action of URbInputSetup, route walking look vs aiming look by stroke phase,
// movement settings, capsule / table collision, tests (functional: walk around the table without entering it).

ARbPlayerCharacter::ARbPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(25.0f, 88.0f);

	Camera = CreateDefaultSubobject<UCineCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0, 0.0, 165.0 - 88.0)); // standing eye height over the capsule centre
	Camera->bUsePawnControlRotation = true;

	CameraRig = CreateDefaultSubobject<URbCameraRigComponent>(TEXT("CameraRig"));
	Stroke = CreateDefaultSubobject<URbStrokeComponent>(TEXT("Stroke"));

	bUseControllerRotationYaw = true;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void ARbPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	CameraRig->SetCamera(Camera);
}

void ARbPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	const ARbPlayerController* PC = Cast<ARbPlayerController>(GetController());
	const URbInputSetup* Setup = PC ? PC->GetInputSetup() : nullptr;
	if (!Input || !Setup)
	{
		return;
	}
	Input->BindAction(Setup->Move, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnMove);
	Input->BindAction(Setup->Look, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnLook);
	// TODO(UE-5b): GetDown, Stroke, Commit, Elevation, TipOffset, FineAim, Settle, Confirm.
}

void ARbPlayerCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), Axis.Y);
	AddMovementInput(GetActorRightVector(), Axis.X);
}

void ARbPlayerCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void ARbPlayerCharacter::OnGetDown(const FInputActionValue& /*Value*/) { Stroke->RequestGetDownToggle(); }
void ARbPlayerCharacter::OnStroke(const FInputActionValue& Value) { Stroke->SetStrokeHeld(Value.Get<bool>()); }
void ARbPlayerCharacter::OnCommit(const FInputActionValue& Value) { Stroke->SetCommitHeld(Value.Get<bool>()); }
void ARbPlayerCharacter::OnElevation(const FInputActionValue& Value) { Stroke->AddElevationInput(Value.Get<float>()); }
void ARbPlayerCharacter::OnTipOffset(const FInputActionValue& Value) { Stroke->AddTipOffsetInput(Value.Get<FVector2D>()); }
void ARbPlayerCharacter::OnFineAim(const FInputActionValue& Value) { bFineAim = Value.Get<bool>(); }
void ARbPlayerCharacter::OnSettle(const FInputActionValue& Value) { Stroke->SetSettleHeld(Value.Get<bool>()); }
void ARbPlayerCharacter::OnConfirm(const FInputActionValue& /*Value*/) { Stroke->ConfirmPressed(); }
