#include "Player/RbPlayerController.h"

#include "RawBreak.h"
#include "Dev/RbCheatManager.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Input/RbInputSetup.h"
#include "Replay/RbReplaySubsystem.h"
#include "UI/RbOverlayComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputActionValue.h"

// Owner: UE-5b. Input setup + mapping context, the non-pawn actions (glance / overlay -> URbOverlayComponent, replay ->
// URbReplaySubsystem, options -> URbMatchDirector), mouse capture. Tests: RawBreak.Unit.Player.Controller* (RbCameraRigTests.cpp).

ARbPlayerController::ARbPlayerController()
{
	CheatClass = URbCheatManager::StaticClass();
	Overlay = CreateDefaultSubobject<URbOverlayComponent>(TEXT("Overlay"));
	bShowMouseCursor = false;
}

void ARbPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (InputSetup && InputSetup->Context && !Subsystem->HasMappingContext(InputSetup->Context))
		{
			Subsystem->AddMappingContext(InputSetup->Context, 0);
		}
	}
	// First-person mouse: captured and hidden (the stroke's raw-input thread relies on UE's high-precision mouse mode).
	FInputModeGameOnly Mode;
	Mode.SetConsumeCaptureMouseDown(true);
	SetInputMode(Mode);
	bShowMouseCursor = false;
}

void ARbPlayerController::SetupInputComponent()
{
	// The actions must exist before the pawn binds them (SetupPlayerInputComponent runs after possession).
	if (!InputSetup)
	{
		InputSetup = URbInputSetup::CreateDefault(this);
	}
	Super::SetupInputComponent();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogRawBreak, Warning, TEXT("ARbPlayerController: the input component is not an UEnhancedInputComponent (DefaultInput.ini)"));
		return;
	}
	Input->BindAction(InputSetup->Glance, ETriggerEvent::Triggered, this, &ARbPlayerController::OnGlance);
	Input->BindAction(InputSetup->Glance, ETriggerEvent::Completed, this, &ARbPlayerController::OnGlance);
	Input->BindAction(InputSetup->ToggleOverlay, ETriggerEvent::Triggered, this, &ARbPlayerController::OnToggleOverlay);
	Input->BindAction(InputSetup->ToggleDebug, ETriggerEvent::Triggered, this, &ARbPlayerController::OnToggleDebug);
	Input->BindAction(InputSetup->Replay, ETriggerEvent::Triggered, this, &ARbPlayerController::OnReplay);
	Input->BindAction(InputSetup->CycleOption, ETriggerEvent::Triggered, this, &ARbPlayerController::OnCycleOption);
	// Esc leaves a replay: a legacy key binding outside the mapping context (UEnhancedPlayerInput still evaluates the legacy key
	// bindings after the actions). UEnhancedInputComponent hides the legacy helpers, so bind through the base class. A "Back" action
	// in URbInputSetup (UE-5a) would replace this.
	static_cast<UInputComponent*>(Input)->BindKey(EKeys::Escape, IE_Pressed, this, &ARbPlayerController::HandleReplayBack);
}

URbMatchDirector* ARbPlayerController::FindDirector() const
{
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	return GameMode ? GameMode->GetDirector() : nullptr;
}

URbReplaySubsystem* ARbPlayerController::FindReplay() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
}

void ARbPlayerController::HandleGlance(bool bHeld)
{
	if (Overlay)
	{
		Overlay->SetGlanceHeld(bHeld);
	}
}

void ARbPlayerController::HandleToggleOverlay()
{
	if (Overlay)
	{
		Overlay->TogglePinned();
	}
}

void ARbPlayerController::HandleToggleDebug()
{
	if (Overlay)
	{
		Overlay->ToggleDebug();
	}
}

bool ARbPlayerController::HandleReplay()
{
	URbReplaySubsystem* Replay = FindReplay();
	if (!Replay)
	{
		return false;
	}
	if (Replay->IsReplaying())
	{
		Replay->CycleView(); // R again: the next view
		return true;
	}
	const URbMatchDirector* Director = FindDirector();
	if (!Director || !Director->IsReplayAllowed() || Replay->GetShotCount() == 0)
	{
		return false; // never while a live shot simulates or plays back (R-07)
	}
	return Replay->PlayReplay(0, ERbReplayView::Shooter, 1.0f);
}

void ARbPlayerController::HandleReplayBack()
{
	if (URbReplaySubsystem* Replay = FindReplay())
	{
		if (Replay->IsReplaying())
		{
			Replay->StopReplay();
		}
	}
}

bool ARbPlayerController::HandleCycleOption(float Direction)
{
	URbMatchDirector* Director = FindDirector();
	if (!Director || FMath::IsNearlyZero(Direction))
	{
		return false;
	}
	return Director->CycleOption(Direction > 0.0f ? 1 : -1);
}

void ARbPlayerController::OnGlance(const FInputActionValue& Value) { HandleGlance(Value.Get<bool>()); }
void ARbPlayerController::OnToggleOverlay(const FInputActionValue& /*Value*/) { HandleToggleOverlay(); }
void ARbPlayerController::OnToggleDebug(const FInputActionValue& /*Value*/) { HandleToggleDebug(); }
void ARbPlayerController::OnReplay(const FInputActionValue& /*Value*/) { HandleReplay(); }
void ARbPlayerController::OnCycleOption(const FInputActionValue& Value) { HandleCycleOption(Value.Get<float>()); }
