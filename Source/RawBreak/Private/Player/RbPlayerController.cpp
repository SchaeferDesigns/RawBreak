#include "Player/RbPlayerController.h"

#include "RawBreak.h"
#include "Dev/RbCheatManager.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Input/RbInputSetup.h"
#include "Modes/RbCallShotComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "UI/RbOverlayComponent.h"
#include "UI/Core/RbUiSubsystem.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputActionValue.h"

// Owner: UE-5b. Input setup + mapping context, the non-pawn actions (glance / overlay -> URbOverlayComponent, replay ->
// URbReplaySubsystem, options -> URbMatchDirector), mouse capture. Tests: RawBreak.Unit.Player.Controller* (RbCameraRigTests.cpp).

ARbPlayerController::ARbPlayerController()
{
	CheatClass = URbCheatManager::StaticClass();
	Overlay = CreateDefaultSubobject<URbOverlayComponent>(TEXT("Overlay"));
	CallShot = CreateDefaultSubobject<URbCallShotComponent>(TEXT("CallShot")); // M3 (19.3): behaviour M3-G
	bShowMouseCursor = false;
}

void ARbPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	RegisterMappingContext();
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
	// Esc (M2): the Pause action - leaves a replay, else opens / closes the pause menu (IA_Pause triggers while paused).
	Input->BindAction(InputSetup->Pause, ETriggerEvent::Triggered, this, &ARbPlayerController::OnPause);
	// M3 (19.3): the call component binds Call / Declare / CycleOption on this input component itself (M3-G).
	if (CallShot)
	{
		CallShot->BindInput(Input, InputSetup);
	}
	RegisterMappingContext(); // SetPlayer (the local player is set) may run after BeginPlay
}

bool ARbPlayerController::NeutraliseMouseAxes(UPlayerInput* Input)
{
	if (!Input)
	{
		return false;
	}
	// M2-F (P3): the Look action must carry raw counts. Enhanced Input applies the legacy axis properties of a mouse key (BaseInput.ini:
	// Sensitivity 0.07) as a hidden Scalar modifier when the mappings are rebuilt; DefaultInput.ini neutralises them, and this makes
	// sure (a user / plugin ini cannot bring the 0.07 back).
	bool bChanged = false;
	for (const FKey& Key : {EKeys::MouseX, EKeys::MouseY, EKeys::Mouse2D})
	{
		FInputAxisProperties Props;
		if (Input->GetAxisProperties(Key, Props) && (Props.Sensitivity != 1.0f || Props.DeadZone != 0.0f || Props.Exponent != 1.0f || Props.bInvert))
		{
			Props.Sensitivity = 1.0f;
			Props.DeadZone = 0.0f;
			Props.Exponent = 1.0f;
			Props.bInvert = false;
			Input->SetAxisProperties(Key, Props);
			bChanged = true;
		}
	}
	return bChanged;
}

void ARbPlayerController::RegisterMappingContext()
{
	if (!InputSetup || !InputSetup->Context || !IsLocalController())
	{
		return;
	}
	const bool bAxesChanged = NeutraliseMouseAxes(PlayerInput);
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (!Subsystem->HasMappingContext(InputSetup->Context))
		{
			Subsystem->AddMappingContext(InputSetup->Context, 0);
		}
		else if (bAxesChanged)
		{
			Subsystem->RequestRebuildControlMappings();
		}
	}
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

void ARbPlayerController::HandlePause()
{
	if (URbReplaySubsystem* Replay = FindReplay())
	{
		if (Replay->IsReplaying())
		{
			Replay->StopReplay();
			return;
		}
	}
	if (URbUiSubsystem* Ui = URbUiSubsystem::Get(this))
	{
		Ui->TogglePauseMenu();
	}
}

void ARbPlayerController::OnGlance(const FInputActionValue& Value) { HandleGlance(Value.Get<bool>()); }
void ARbPlayerController::OnToggleOverlay(const FInputActionValue& /*Value*/) { HandleToggleOverlay(); }
void ARbPlayerController::OnToggleDebug(const FInputActionValue& /*Value*/) { HandleToggleDebug(); }
void ARbPlayerController::OnReplay(const FInputActionValue& /*Value*/) { HandleReplay(); }
void ARbPlayerController::OnCycleOption(const FInputActionValue& Value) { HandleCycleOption(Value.Get<float>()); }
void ARbPlayerController::OnPause(const FInputActionValue& /*Value*/) { HandlePause(); }
