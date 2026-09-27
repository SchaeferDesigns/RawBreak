#include "Player/RbPlayerController.h"

#include "Dev/RbCheatManager.h"
#include "Input/RbInputSetup.h"
#include "UI/RbOverlayComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputActionValue.h"

// Owner: UE-5b. TODO(UE-5b): bind glance / overlay / debug / replay / option actions (glance + overlay forward to
// URbOverlayComponent, replay to URbReplaySubsystem, options to URbMatchDirector), mouse capture, tests.

ARbPlayerController::ARbPlayerController()
{
	CheatClass = URbCheatManager::StaticClass();
	Overlay = CreateDefaultSubobject<URbOverlayComponent>(TEXT("Overlay"));
}

void ARbPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			if (InputSetup && InputSetup->Context)
			{
				Subsystem->AddMappingContext(InputSetup->Context, 0);
			}
		}
	}
}

void ARbPlayerController::SetupInputComponent()
{
	// The actions must exist before the pawn binds them (SetupPlayerInputComponent runs after possession).
	if (!InputSetup)
	{
		InputSetup = URbInputSetup::CreateDefault(this);
	}
	Super::SetupInputComponent();
	// TODO(UE-5b): BindAction for Glance, ToggleOverlay, ToggleDebug, Replay, CycleOption.
}

void ARbPlayerController::OnGlance(const FInputActionValue& /*Value*/) { /* TODO(UE-5b) */ }
void ARbPlayerController::OnToggleOverlay(const FInputActionValue& /*Value*/) { /* TODO(UE-5b) */ }
void ARbPlayerController::OnToggleDebug(const FInputActionValue& /*Value*/) { /* TODO(UE-5b) */ }
void ARbPlayerController::OnReplay(const FInputActionValue& /*Value*/) { /* TODO(UE-5b) */ }
void ARbPlayerController::OnCycleOption(const FInputActionValue& /*Value*/) { /* TODO(UE-5b) */ }
