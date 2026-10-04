#include "Player/RbPlayerCharacter.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Camera/RbCameraRigComponent.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Input/RbAimResponse.h"
#include "Input/RbInputSetup.h"
#include "Interaction/RbInteractionSubsystem.h"
#include "Player/RbBallInHandComponent.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Simulation/RbShot.h"
#include "Table/RbTable.h"

#include "CineCameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "EnhancedInputComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/IConsoleManager.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR && !UE_BUILD_SHIPPING
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

// Owner: UE-5b, M2-F. Pawn: movement settings, capsule / table collision, the input routing of the header (counts -> cm -> degrees,
// the look intent gate, the carrying hand), the watching reactions / flinch feed, dev console commands (rb.Player.*, non-shipping)
// that drive the pawn of a running game for headless captures. Tests: RawBreak.Unit.Player.*, RawBreak.Unit.Feel.*,
// RawBreak.Functional.Player.PawnBlockedByTable (Private/Tests/RbCameraRigTests.cpp, RbFeelTests.cpp).

namespace
{
	// A ball-ball impact below this approach speed makes no flinch; full loudness at kFlinchFullMps (the break's first hits).
	constexpr double kFlinchFromMps = 3.0;
	constexpr double kFlinchFullMps = 7.0;
}

ARbPlayerCharacter::ARbPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false; // the components tick (stroke: pre-physics, camera rig: post-physics)
	GetCapsuleComponent()->InitCapsuleSize(25.0f, 88.0f);

	Camera = CreateDefaultSubobject<UCineCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0, 0.0, 165.0 - 88.0)); // standing eye height over the capsule centre
	Camera->bUsePawnControlRotation = false; // the camera rig places the eye (down on the shot it is not the control rotation)

	CameraRig = CreateDefaultSubobject<URbCameraRigComponent>(TEXT("CameraRig"));
	Stroke = CreateDefaultSubobject<URbStrokeComponent>(TEXT("Stroke"));
	BallInHand = CreateDefaultSubobject<URbBallInHandComponent>(TEXT("BallInHand"));
	BallInHand->SetupAttachment(GetCapsuleComponent());

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = 600.0f;            // ~0.25 s to walking speed: a person, not a shooter avatar
	Move->BrakingDecelerationWalking = 700.0f;
	Move->GroundFriction = 8.0f;
	Move->MaxStepHeight = 20.0f;               // stairs-like steps only: never onto a rail or the apron
	Move->bOrientRotationToMovement = false;
	Move->NavAgentProps.bCanCrouch = false;
	Move->NavAgentProps.bCanJump = false;
	JumpMaxCount = 0;
}

void ARbPlayerCharacter::BeginPlay()
{
	// The rig needs its camera before its own BeginPlay places it for the first frame.
	CameraRig->SetCamera(Camera);
	CameraRig->FixationResolver = [WeakThis = TWeakObjectPtr<ARbPlayerCharacter>(this)](const FVector& Contact, const FVector& Dir, FVector& Out) {
		const ARbPlayerCharacter* Self = WeakThis.Get();
		return Self && Self->FindFixationPoint(Contact, Dir, Out);
	};
	CameraRig->ReactionResolver = [WeakThis = TWeakObjectPtr<ARbPlayerCharacter>(this)](FVector& Out) {
		const ARbPlayerCharacter* Self = WeakThis.Get();
		return Self && Self->FindReactionTarget(Out);
	};
	// Ball in hand: the director decides what is legal (the carrying hand hesitates at an illegal spot and never lowers there).
	Stroke->PlacementValidator = [WeakThis = TWeakObjectPtr<ARbPlayerCharacter>(this)](const rb::Vec2& Plan) {
		const ARbPlayerCharacter* Self = WeakThis.Get();
		const ARbGameMode* GameMode = Self ? ARbGameMode::Get(Self) : nullptr;
		const URbMatchDirector* Director = GameMode ? GameMode->GetDirector() : nullptr;
		return !Director || Director->CanPlaceCueBall(Plan);
	};
	// Pause (review): on resume the stroke component asks whether the Stroke button is physically down - a button held through the
	// pause must be released first, one let go during it must not swallow the next press. Unknown (no player controller / input
	// setup, tests) counts as held.
	Stroke->IsStrokeButtonDown = [WeakThis = TWeakObjectPtr<ARbPlayerCharacter>(this)]() {
		const ARbPlayerCharacter* Self = WeakThis.Get();
		const ARbPlayerController* PC = Self ? Cast<ARbPlayerController>(Self->GetController()) : nullptr;
		const URbInputSetup* Setup = PC ? PC->GetInputSetup() : nullptr;
		if (!Setup || !Setup->Context || !Setup->Stroke || !PC->PlayerInput)
		{
			return true;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Setup->Context->GetMappings())
		{
			if (Mapping.Action == Setup->Stroke && PC->IsInputKeyDown(Mapping.Key))
			{
				return true;
			}
		}
		return false;
	};
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	// The carrying hand places its arm from the eye: after the rig moved the camera this frame (both post-physics).
	BallInHand->AddTickPrerequisiteComponent(CameraRig);
	Stroke->GetDownSeconds = CameraRig->GetParams().GetDownSeconds; // without a running posture change (the rig's human get-down decides)
	ContactHandle = Stroke->OnStrokeContact.AddUObject(this, &ARbPlayerCharacter::OnStrokeContactForFeel);
}

void ARbPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stroke->OnStrokeContact.Remove(ContactHandle);
	ContactHandle.Reset();
	if (URbShotPlaybackComponent* Playback = BoundPlayback.Get())
	{
		Playback->OnShotEvent.Remove(ShotEventHandle);
	}
	ShotEventHandle.Reset();
	BoundPlayback.Reset();
	BoundBalls.Reset();
	CameraRig->ReactionResolver = nullptr;
	Stroke->IsStrokeButtonDown = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ARbPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	const ARbPlayerController* PC = Cast<ARbPlayerController>(GetController());
	const URbInputSetup* Setup = PC ? PC->GetInputSetup() : nullptr;
	if (!Input || !Setup)
	{
		UE_LOG(LogRawBreak, Warning, TEXT("ARbPlayerCharacter: no Enhanced Input component or input setup - the pawn gets no input"));
		return;
	}
	// Trigger conventions of URbInputSetup: axes and pressed actions fire Triggered; held actions Triggered (true) every frame
	// while held and Completed (false) on release.
	Input->BindAction(Setup->Move, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnMove);
	Input->BindAction(Setup->Look, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnLook);
	Input->BindAction(Setup->GetDown, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnGetDown);
	Input->BindAction(Setup->Elevation, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnElevation);
	Input->BindAction(Setup->TipOffset, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnTipOffset);
	Input->BindAction(Setup->Confirm, ETriggerEvent::Triggered, this, &ARbPlayerCharacter::OnConfirm);
	for (const ETriggerEvent Event : {ETriggerEvent::Triggered, ETriggerEvent::Completed})
	{
		Input->BindAction(Setup->Stroke, Event, this, &ARbPlayerCharacter::OnStroke);
		Input->BindAction(Setup->Commit, Event, this, &ARbPlayerCharacter::OnCommit);
		Input->BindAction(Setup->FineAim, Event, this, &ARbPlayerCharacter::OnFineAim);
		Input->BindAction(Setup->Settle, Event, this, &ARbPlayerCharacter::OnSettle);
	}
}

// ---------------------------------------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------------------------------------

bool ARbPlayerCharacter::CanWalk() const
{
	const ERbStrokePhase Phase = Stroke->GetPhase();
	if (Phase == ERbStrokePhase::GettingDown || Phase == ERbStrokePhase::Down)
	{
		return false;
	}
	const ERbCameraRigMode Mode = CameraRig->GetMode();
	return Mode == ERbCameraRigMode::Standing || Mode == ERbCameraRigMode::BallInHand;
}

void ARbPlayerCharacter::HandleMove(const FVector2D& Axis)
{
	if (!CanWalk())
	{
		return;
	}
	const FRotator Yaw(0.0, GetControlRotation().Yaw, 0.0);
	const FRotationMatrix Frame(Yaw);
	AddMovementInput(Frame.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(Frame.GetUnitAxis(EAxis::Y), Axis.X);
}

void ARbPlayerCharacter::HandleLook(const FVector2D& Counts, double DeltaSeconds)
{
	if (DeltaSeconds < 0.0)
	{
		// The hand speed of the optional aim acceleration is real time (not dilated game time).
		const UWorld* World = GetWorld();
		DeltaSeconds = World ? static_cast<double>(World->DeltaRealTimeSeconds) : 0.0;
	}
	const FRbControlSettings& Controls = Stroke->Controls;
	const double Dpi = Stroke->MouseDpi;
	const double InvertY = Controls.bInvertLookY ? -1.0 : 1.0;
	const ERbStrokePhase Phase = Stroke->GetPhase();
	const double Now = Stroke->GetClockNow();
	if (Phase == ERbStrokePhase::GettingDown || Phase == ERbStrokePhase::Down)
	{
		LookGate.Disarm();
		// Aiming (P3): X turns the cue about the cue ball (cm of mouse travel -> degrees); Y runs the eyes along the line (never while
		// the mouse is the stroke).
		Stroke->AddAimInput(Counts, bFineAim, DeltaSeconds);
		// The mouse is the stroke while a stroke source runs AND while the Stroke button is held without one yet (pressed during the
		// get-down: the stroke starts when Down begins) or must first be released (held through a pause): no eye pitch then (review).
		const bool bMouseIsStroke = Stroke->IsStrokeActive() || Stroke->IsStrokeHeld() || Stroke->IsStrokeReleaseRequired();
		if (!bMouseIsStroke)
		{
			const double Pitch = AimGazePitchScale * RbAimResponse::LookDegrees(Counts.Y, Dpi, Controls) * InvertY *
				(bFineAim ? static_cast<double>(Controls.FineAimFactor) : 1.0);
			CameraRig->AddGazeInput(0.0, Pitch);
		}
		return;
	}
	if (CameraRig->GetMode() == ERbCameraRigMode::DownOnShot)
	{
		// Still down after the contact (P1): the stroke's own motion, its follow-through and the hand coming to rest never turn the
		// head; a deliberate new move does, faded in (the look intent gate works in cm of travel and input timestamps).
		const FVector2D Cm(RbAimResponse::CountsToCm(Counts.X, Dpi), RbAimResponse::CountsToCm(Counts.Y, Dpi));
		const FVector2D Head = LookGate.Filter(Cm, Now);
		const double DegPerCm = static_cast<double>(Controls.LookDegreesPerCm) * static_cast<double>(Controls.LookSensitivity) * WatchLookScale;
		if (!Head.IsZero())
		{
			CameraRig->AddGazeInput(DegPerCm * Head.X, DegPerCm * Head.Y * InvertY);
		}
		return;
	}
	LookGate.Disarm();
	if (Phase == ERbStrokePhase::PlacingCueBall && bFineAim && BallInHand)
	{
		// Ball in hand with Shift: the eyes stay, the hand nudges the ball (the aim's fine factor).
		BallInHand->FineMetersPerCm = static_cast<float>(Controls.FineAimFactor * Controls.LookDegreesPerCm * Controls.LookSensitivity *
			UE_DOUBLE_PI / 180.0 * 1.2);
		BallInHand->AddFineAdjustCm(FVector2D(RbAimResponse::CountsToCm(Counts.X, Dpi), RbAimResponse::CountsToCm(Counts.Y, Dpi)));
		return;
	}
	// Standing look: degrees per cm of mouse travel, applied to the control rotation directly (no engine input scales).
	if (AController* PC = GetController())
	{
		FRotator Rotation = PC->GetControlRotation();
		Rotation.Yaw += RbAimResponse::LookDegrees(Counts.X, Dpi, Controls);
		Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch) + RbAimResponse::LookDegrees(Counts.Y, Dpi, Controls) * InvertY,
			-static_cast<double>(MaxLookPitchDeg), static_cast<double>(MaxLookPitchDeg));
		Rotation.Roll = 0.0;
		PC->SetControlRotation(Rotation);
	}
}

FVector2D ARbPlayerCharacter::LookCountsForDegrees(const FVector2D& Degrees) const
{
	const FRbControlSettings& Controls = Stroke->Controls;
	const double CmPerCount = RbAimResponse::CountsToCm(1.0, Stroke->MouseDpi);
	const double LookPerCount = CmPerCount * Controls.LookDegreesPerCm * Controls.LookSensitivity;
	const ERbStrokePhase Phase = Stroke->GetPhase();
	if (Phase == ERbStrokePhase::GettingDown || Phase == ERbStrokePhase::Down)
	{
		const double AimPerCount = FMath::RadiansToDegrees(RbAimResponse::RadiansPerCount(false, Stroke->MouseDpi, Controls));
		// + yaw (view right) = the aim turns right = mouse right (+X).
		return FVector2D(AimPerCount > 0.0 ? Degrees.X / AimPerCount : 0.0,
			LookPerCount > 0.0 ? Degrees.Y / (AimGazePitchScale * LookPerCount) : 0.0);
	}
	return LookPerCount > 0.0 ? Degrees / LookPerCount : FVector2D::ZeroVector;
}

bool ARbPlayerCharacter::InjectStrokeKey(bool bDown)
{
	ARbPlayerController* PC = Cast<ARbPlayerController>(GetController());
	const URbInputSetup* Setup = PC ? PC->GetInputSetup() : nullptr;
	if (!Setup || !Setup->Context || !Setup->Stroke || !PC->IsLocalController())
	{
		return false;
	}
	for (const FEnhancedActionKeyMapping& Mapping : Setup->Context->GetMappings())
	{
		if (Mapping.Action == Setup->Stroke)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(Mapping.Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.0f : 0.0f));
			return true;
		}
	}
	return false;
}

void ARbPlayerCharacter::HandleStroke(bool bHeld)
{
	bStrokeButtonHeld = bHeld;
	LookGate.SetStrokeHeld(bHeld, Stroke->GetClockNow());
	Stroke->SetStrokeHeld(bHeld);
}

void ARbPlayerCharacter::OnStrokeContactForFeel(const FRbStrokeCommit& Commit)
{
	// P1: the gate closes at the contact (the stroke's own time base); the button may still be held for the follow-through.
	LookGate.Arm(Commit.ContactTime);
	LookGate.SetStrokeHeld(bStrokeButtonHeld, Commit.ContactTime);
	BindShotEvents();
}

void ARbPlayerCharacter::BindShotEvents()
{
	// The player's ball set and playback, looked up once per contact: the reaction target is asked every frame while watching, and
	// the table lookup iterates the level's actors and builds an array (review: no per-frame lookups / allocations).
	const URbTableSubsystem* Tables = URbTableSubsystem::Get(this);
	ARbTable* Table = Tables ? Tables->GetPlayerTable() : nullptr;
	ARbBallSet* Balls = Tables && Table ? Tables->FindBallSet(Table) : nullptr;
	BoundBalls = Balls;
	URbShotPlaybackComponent* Playback = Balls ? Balls->GetPlayback() : nullptr;
	if (!Playback || Playback == BoundPlayback.Get())
	{
		return;
	}
	if (URbShotPlaybackComponent* Old = BoundPlayback.Get())
	{
		Old->OnShotEvent.Remove(ShotEventHandle);
	}
	ShotEventHandle = Playback->OnShotEvent.AddUObject(this, &ARbPlayerCharacter::OnShotEventForFeel);
	BoundPlayback = Playback;
}

void ARbPlayerCharacter::OnShotEventForFeel(const TSharedRef<const FRbShot>& Shot, int32 EventIndex)
{
	// A loud impact while the player watches his own shot (the break): the head flinches, scaled by the approach speed.
	const rb::ShotResult& Result = Shot->Result;
	if (!CameraRig->IsWatching() || EventIndex < 0 || EventIndex >= static_cast<int32>(Result.Events.size()))
	{
		return;
	}
	const rb::ShotEvent& Event = Result.Events[static_cast<size_t>(EventIndex)];
	if (Event.Type != rb::ShotEventType::BallBall)
	{
		return;
	}
	const double Loudness = FMath::Clamp((Event.NormalSpeed - kFlinchFromMps) / (kFlinchFullMps - kFlinchFromMps), 0.0, 1.0);
	if (Loudness > 0.0)
	{
		CameraRig->NotifyImpact(Loudness);
	}
}

bool ARbPlayerCharacter::FindReactionTarget(FVector& OutWorld) const
{
	// The ball set / playback of the shot this player watches (bound at the contact, BindShotEvents).
	const ARbBallSet* Balls = BoundBalls.Get();
	const URbShotPlaybackComponent* Playback = BoundPlayback.Get();
	if (!Playback || !Balls || Balls->GetPlayback() != Playback || !Playback->IsPlaying() || !Playback->GetShot().IsValid())
	{
		return false;
	}
	const UStaticMeshComponent* CueBall = Balls->GetBallComponent(0);
	if (!CueBall || !Balls->IsBallVisible(0))
	{
		return false;
	}
	// The cue ball, and once it hit its first object ball the point between both (the eyes split the attention).
	FVector Target = CueBall->GetComponentLocation();
	const double ShotTime = Playback->GetShotTime();
	for (const rb::ShotEvent& Event : Playback->GetShot()->Result.Events)
	{
		if (Event.Time > ShotTime)
		{
			break;
		}
		if (Event.Type == rb::ShotEventType::BallBall && (Event.A == 0 || Event.B == 0))
		{
			const int32 Other = Event.A == 0 ? Event.B : Event.A;
			if (const UStaticMeshComponent* Object = Balls->GetBallComponent(Other))
			{
				if (Balls->IsBallVisible(Other))
				{
					Target = 0.5 * (Target + Object->GetComponentLocation());
				}
			}
			break;
		}
	}
	OutWorld = Target;
	return true;
}

void ARbPlayerCharacter::HandleGetDown()
{
	if (Stroke->GetPhase() == ERbStrokePhase::Locked && CameraRig->GetMode() == ERbCameraRigMode::DownOnShot)
	{
		// Locked while still down (a scripted strike, a decision): the player may always stand up and look around.
		CameraRig->SetMode(ERbCameraRigMode::Standing);
		return;
	}
	Stroke->RequestGetDownToggle();
}

void ARbPlayerCharacter::HandleConfirm()
{
	if (Stroke->GetPhase() == ERbStrokePhase::PlacingCueBall)
	{
		// Ball in hand with an empty hand: the cue ball lies off the table (18.6.1) - a gazed pick-up takes the press first (the
		// picked-up cue ball goes into the carrying hand); otherwise Confirm sets the carried ball down (integration round).
		const URbBallInHandComponent* Hand = FindComponentByClass<URbBallInHandComponent>();
		URbInteractionSubsystem* Interaction = URbInteractionSubsystem::Get(this);
		const UCineCameraComponent* Eye = GetCamera();
		if (Hand && Hand->GetState() == ERbBallInHandState::Inactive && Interaction && Eye &&
			Interaction->TryInteract(*this, Eye->GetComponentLocation(), Eye->GetForwardVector()))
		{
			return;
		}
		Stroke->ConfirmPressed();
		return;
	}
	// M2 (architect, 18.6): world interactions first (pick up a ball that left the table, later the chores) - a gazed
	// interactable within reach consumes the press.
	if (URbInteractionSubsystem* Interaction = URbInteractionSubsystem::Get(this))
	{
		if (const UCineCameraComponent* Eye = GetCamera())
		{
			if (Interaction->TryInteract(*this, Eye->GetComponentLocation(), Eye->GetForwardVector()))
			{
				return;
			}
		}
	}
	// Decision option / next rack / new match (R-20).
	if (const ARbGameMode* GameMode = ARbGameMode::Get(this))
	{
		if (URbMatchDirector* Director = GameMode->GetDirector())
		{
			Director->Confirm();
		}
	}
}

bool ARbPlayerCharacter::FindFixationPoint(const FVector& ContactWorld, const FVector& DirectionWorld, FVector& OutWorld) const
{
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	const URbMatchDirector* Director = GameMode ? GameMode->GetDirector() : nullptr;
	const ARbTable* Table = GameMode ? GameMode->GetTable() : nullptr;
	if (!Director || !Table || !Table->HasContext())
	{
		return false;
	}
	// Plan line of the aim through the cue ball (core table frame).
	const rb::Vec3 D = Table->WorldDirectionToCore(DirectionWorld);
	const double Len = FMath::Sqrt(D.x * D.x + D.y * D.y);
	if (Len < 1.0e-9)
	{
		return false;
	}
	const double Dx = D.x / Len;
	const double Dy = D.y / Len;
	const FRbTableState& State = Director->GetTableState();
	const rb::SimBall& CueBall = State.Balls[0];
	const rb::Vec3 Cue = CueBall.InPlay ? CueBall.State.Position : Table->WorldToCore(ContactWorld);
	const double R = CueBall.Spec.Radius > 0.0 ? CueBall.Spec.Radius : 0.028575;

	// The first ball the cue ball would meet on the line.
	double Best = TNumericLimits<double>::Max();
	rb::Vec3 Target;
	for (int32 Id = 1; Id < rb::kMaxBalls; ++Id)
	{
		const rb::SimBall& Ball = State.Balls[Id];
		if (!Ball.InPlay)
		{
			continue;
		}
		const double Wx = Ball.State.Position.x - Cue.x;
		const double Wy = Ball.State.Position.y - Cue.y;
		const double Along = Wx * Dx + Wy * Dy;
		const double Perp = FMath::Abs(Dx * Wy - Dy * Wx);
		if (Along > 0.0 && Perp < R + Ball.Spec.Radius && Along < Best)
		{
			Best = Along;
			Target = Ball.State.Position;
		}
	}
	if (Best == TNumericLimits<double>::Max())
	{
		// No ball: where the cue ball's centre line meets the cushions (the nose rectangle less a radius).
		const rb::rules::RulesTable& Rules = Table->GetContext().RulesTable;
		const double HalfL = 0.5 * Rules.Length - R;
		const double HalfW = 0.5 * Rules.Width - R;
		const double Tx = Dx > 1.0e-9 ? (HalfL - Cue.x) / Dx : (Dx < -1.0e-9 ? (-HalfL - Cue.x) / Dx : TNumericLimits<double>::Max());
		const double Ty = Dy > 1.0e-9 ? (HalfW - Cue.y) / Dy : (Dy < -1.0e-9 ? (-HalfW - Cue.y) / Dy : TNumericLimits<double>::Max());
		const double T = FMath::Max(0.0, FMath::Min(Tx, Ty));
		Target = rb::Vec3(Cue.x + Dx * T, Cue.y + Dy * T, R);
	}
	OutWorld = Table->CoreToWorld(Target);
	return true;
}

void ARbPlayerCharacter::HandleSettle(bool bHeld)
{
	Stroke->SetSettleHeld(bHeld);
	CameraRig->SetSettleHeld(bHeld);
}

void ARbPlayerCharacter::OnMove(const FInputActionValue& Value) { HandleMove(Value.Get<FVector2D>()); }
void ARbPlayerCharacter::OnLook(const FInputActionValue& Value) { HandleLook(Value.Get<FVector2D>()); }
void ARbPlayerCharacter::OnGetDown(const FInputActionValue& /*Value*/) { HandleGetDown(); }
void ARbPlayerCharacter::OnStroke(const FInputActionValue& Value) { HandleStroke(Value.Get<bool>()); }
void ARbPlayerCharacter::OnCommit(const FInputActionValue& Value) { Stroke->SetCommitHeld(Value.Get<bool>()); }
void ARbPlayerCharacter::OnElevation(const FInputActionValue& Value) { Stroke->AddElevationInput(Value.Get<float>()); }
void ARbPlayerCharacter::OnTipOffset(const FInputActionValue& Value) { Stroke->AddTipOffsetInput(Value.Get<FVector2D>()); }
void ARbPlayerCharacter::OnFineAim(const FInputActionValue& Value) { bFineAim = Value.Get<bool>(); }
void ARbPlayerCharacter::OnSettle(const FInputActionValue& Value) { HandleSettle(Value.Get<bool>()); }
void ARbPlayerCharacter::OnConfirm(const FInputActionValue& /*Value*/) { HandleConfirm(); }

// ---------------------------------------------------------------------------------------------------------
// Dev console commands (headless captures: -ExecCmds="rb.Player.Teleport ..., rb.Player.GetDown")
// ---------------------------------------------------------------------------------------------------------

#if !UE_BUILD_SHIPPING
namespace RbPlayerDev
{
	ARbPlayerCharacter* FindPawn(UWorld* World)
	{
		return World ? Cast<ARbPlayerCharacter>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
	}

	ARbTable* FindTable(UWorld* World)
	{
		// The player's table (multi-table rule 18.6.2: no "first table found" outside the table subsystem).
		const URbTableSubsystem* Tables = World ? World->GetSubsystem<URbTableSubsystem>() : nullptr;
		return Tables ? Tables->GetPlayerTable() : nullptr;
	}

	bool ParseNumbers(const TArray<FString>& Args, int32 Required, TArray<double>& Out)
	{
		Out.Reset();
		for (const FString& Arg : Args)
		{
			if (Arg.IsEmpty() || !Arg.IsNumeric())
			{
				return false;
			}
			Out.Add(FCString::Atod(*Arg));
		}
		return Out.Num() >= Required;
	}

	const TCHAR* PhaseName(ERbStrokePhase Phase)
	{
		switch (Phase)
		{
		case ERbStrokePhase::Locked: return TEXT("Locked");
		case ERbStrokePhase::Walking: return TEXT("Walking");
		case ERbStrokePhase::GettingDown: return TEXT("GettingDown");
		case ERbStrokePhase::Down: return TEXT("Down");
		case ERbStrokePhase::Contact: return TEXT("Contact");
		case ERbStrokePhase::Watching: return TEXT("Watching");
		case ERbStrokePhase::PlacingCueBall: return TEXT("PlacingCueBall");
		}
		return TEXT("?");
	}

	const TCHAR* ModeName(ERbCameraRigMode Mode)
	{
		switch (Mode)
		{
		case ERbCameraRigMode::Standing: return TEXT("Standing");
		case ERbCameraRigMode::DownOnShot: return TEXT("DownOnShot");
		case ERbCameraRigMode::BallInHand: return TEXT("BallInHand");
		case ERbCameraRigMode::External: return TEXT("External");
		}
		return TEXT("?");
	}

	// rb.Player.Teleport <x m> <y m> <view azimuth deg> [<pitch deg>]: the pawn standing at a core plan point of the table frame,
	// looking along a core azimuth (counter-clockwise from +x, the foot).
	void Teleport(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		ARbTable* Table = FindTable(World);
		TArray<double> V;
		if (!Pawn || !Table || !ParseNumbers(Args, 3, V))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Teleport <x m> <y m> <azimuth deg> [<pitch deg>] (core table frame; needs the pawn and a table)"));
			return;
		}
		const double Az = FMath::DegreesToRadians(V[2]);
		const FVector Plan = Table->CoreToWorld(rb::Vec3(V[0], V[1], 0.0));
		const double FloorZ = Table->GetActorLocation().Z;
		const FVector Location(Plan.X, Plan.Y, FloorZ + Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0);
		Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		const double Yaw = Table->CoreDirectionToWorld(rb::Vec3(FMath::Cos(Az), FMath::Sin(Az), 0.0)).Rotation().Yaw;
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetControlRotation(FRotator(V.Num() > 3 ? V[3] : 0.0, Yaw, 0.0));
		}
		UE_LOG(LogRawBreak, Display, TEXT("rb.Player.Teleport: (%.3f, %.3f) m, azimuth %.1f deg -> world %s yaw %.1f"), V[0], V[1], V[2],
			*Location.ToString(), Yaw);
	}

	void GetDown(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (ARbPlayerCharacter* Pawn = FindPawn(World))
		{
			Pawn->HandleGetDown();
			UE_LOG(LogRawBreak, Display, TEXT("rb.Player.GetDown: stroke phase %s"), PhaseName(Pawn->GetStroke()->GetPhase()));
		}
	}

	// rb.Player.Aim <azimuth deg> [<elevation deg> [<a> <b>]] (core azimuth of the cue, axis offsets in R).
	void Aim(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		TArray<double> V;
		if (!Pawn || !ParseNumbers(Args, 1, V))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Aim <azimuth deg> [<elevation deg> [<a> <b>]]"));
			return;
		}
		V.SetNumZeroed(4);
		Pawn->GetStroke()->SetAim(FMath::DegreesToRadians(V[0]), FMath::DegreesToRadians(V[1]), V[2], V[3]);
	}

	// rb.Player.AimAt <x m> <y m> [<elevation deg>]: the cue aimed from the cue ball (director's table state) at a plan point.
	void AimAt(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		const ARbGameMode* GameMode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
		const URbMatchDirector* Director = GameMode ? GameMode->GetDirector() : nullptr;
		TArray<double> V;
		if (!Pawn || !Director || !ParseNumbers(Args, 2, V))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.AimAt <x m> <y m> [<elevation deg>] (needs ARbGameMode)"));
			return;
		}
		const rb::Vec3 Cue = Director->GetTableState().Balls[0].State.Position;
		const double Az = FMath::Atan2(V[1] - Cue.y, V[0] - Cue.x);
		Pawn->GetStroke()->SetAim(Az, FMath::DegreesToRadians(V.Num() > 2 ? V[2] : 0.0), 0.0, 0.0);
		UE_LOG(LogRawBreak, Display, TEXT("rb.Player.AimAt: azimuth %.3f deg from the cue ball (%.3f, %.3f)"), FMath::RadiansToDegrees(Az), Cue.x, Cue.y);
	}

	void Preset(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		if (!Pawn || Args.Num() < 1)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Preset Eyes|Headcam|Broadcast"));
			return;
		}
		const ERbCameraPreset P = Args[0].Equals(TEXT("Headcam"), ESearchCase::IgnoreCase) ? ERbCameraPreset::Headcam
			: Args[0].Equals(TEXT("Broadcast"), ESearchCase::IgnoreCase)                   ? ERbCameraPreset::Broadcast
																						   : ERbCameraPreset::Eyes;
		Pawn->GetCameraRig()->SetPreset(P);
	}

	void Fov(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		TArray<double> V;
		if (!Pawn || !ParseNumbers(Args, 1, V))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Fov <vertical deg> (Eyes; <= 0 = the preset's)"));
			return;
		}
		Pawn->GetCameraRig()->SetVerticalFovOverride(V[0]);
	}

	// rb.Player.Look <yaw deg> <pitch deg>: a look input in degrees, turned into mouse counts of the current routing and routed like
	// the mouse (standing look, aim while down, the gated head after the shot).
	void Look(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		TArray<double> V;
		if (!Pawn || !ParseNumbers(Args, 2, V))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Look <yaw deg> <pitch deg>"));
			return;
		}
		Pawn->HandleLook(Pawn->LookCountsForDegrees(FVector2D(V[0], V[1])));
	}

	// rb.Player.LookAt <x m> <y m> [<z m>]: the standing / ball-in-hand view turned to a core point of the table frame (the carried
	// ball follows the look point). Iterated: the lean of the ball-in-hand pose moves the eye with the view.
	void LookAt(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		ARbTable* Table = FindTable(World);
		TArray<double> V;
		if (!Pawn || !Table || !ParseNumbers(Args, 2, V) || !Pawn->GetController())
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.LookAt <x m> <y m> [<z m>] (core table frame; needs the pawn and a table)"));
			return;
		}
		const FVector Point = Table->CoreToWorld(rb::Vec3(V[0], V[1], V.Num() > 2 ? V[2] : 0.0));
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector Eye = Pawn->GetCameraRig()->GetBaseEyeTransform().GetLocation();
			const FRotator Rotation = (Point - Eye).Rotation();
			Pawn->GetController()->SetControlRotation(FRotator(Rotation.Pitch, Rotation.Yaw, 0.0));
			Pawn->GetCameraRig()->TickRig(0.0);
		}
	}

	// rb.Player.Confirm [<delay s>]: the Confirm input (ball in hand: set the ball down / refused; else interactions, the director).
	void Confirm(const TArray<FString>& Args, UWorld* World)
	{
		TArray<double> V;
		if (ParseNumbers(Args, 1, V) && V[0] > 0.0)
		{
			const TWeakObjectPtr<UWorld> WeakWorld(World);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld](float) {
				if (UWorld* W = WeakWorld.Get())
				{
					Confirm(TArray<FString>(), W);
				}
				return false;
			}), static_cast<float>(V[0]));
			return;
		}
		if (ARbPlayerCharacter* Pawn = FindPawn(World))
		{
			Pawn->HandleConfirm();
		}
	}

	void Settle(const TArray<FString>& Args, UWorld* World)
	{
		if (ARbPlayerCharacter* Pawn = FindPawn(World))
		{
			Pawn->HandleSettle(Args.Num() == 0 || Args[0] != TEXT("0"));
		}
	}

	// rb.Player.Stroke <tip speed m/s> [practice]: a scripted stroke through the pawn while down on the shot, played like a mouse
	// stroke: Commit (unless "practice") and the Stroke button held, the hand path of URbStrokeComponent::MakeScriptedStroke, the
	// button released 0.3 s after the contact (the follow-through with the button still held, the P1 trace of 18.3 F1).
	void Stroke(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		TArray<double> V;
		const bool bPractice = Args.Num() > 1 && Args[1].Equals(TEXT("practice"), ESearchCase::IgnoreCase);
		if (!Pawn || Args.Num() < 1 || !ParseNumbers({Args[0]}, 1, V) || V[0] <= 0.0)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Stroke <tip speed m/s> [practice] (down on the shot)"));
			return;
		}
		URbStrokeComponent* Component = Pawn->GetStroke();
		if (Component->GetPhase() != ERbStrokePhase::Down)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Stroke: not down on the shot (phase %s)"), PhaseName(Component->GetPhase()));
			return;
		}
		Component->SetCommitHeld(!bPractice);
		Pawn->HandleStroke(true);
		Component->InjectStrokeSamples(Component->MakeScriptedStroke(V[0], Component->GetClockNow() + 0.05));
		UE_LOG(LogRawBreak, Display, TEXT("rb.Player.Stroke: %.2f m/s %s"), V[0], bPractice ? TEXT("practice") : TEXT("shot"));
		const TWeakObjectPtr<ARbPlayerCharacter> Weak(Pawn);
		double SinceContact = -1.0;
		double Elapsed = 0.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, SinceContact, Elapsed](float Dt) mutable {
			ARbPlayerCharacter* P = Weak.Get();
			if (!P)
			{
				return false;
			}
			Elapsed += Dt;
			const ERbStrokePhase Phase = P->GetStroke()->GetPhase();
			if (SinceContact < 0.0 && (Phase == ERbStrokePhase::Contact || Phase == ERbStrokePhase::Watching || Phase == ERbStrokePhase::Locked))
			{
				SinceContact = 0.0;
			}
			else if (SinceContact >= 0.0)
			{
				SinceContact += Dt;
			}
			if (SinceContact >= 0.3 || Elapsed > 6.0)
			{
				P->HandleStroke(false);
				P->GetStroke()->SetCommitHeld(false);
				return false;
			}
			return true;
		}));
	}

	// rb.Player.WhenReady <seconds> <console command ...>: runs the command once the shader / asset compilers have been idle for 3
	// frames and <seconds> of game time have passed since - the same clock as the headless capture's warm-up (RbHeadlessCaptureSubsystem:
	// compilers idle, then WarmupSeconds), so a capture can show a moment of a timed action (the hesitation of a refused ball in
	// hand, the view right after the contact) whatever the shader compile took.
	void WhenReady(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2 || !Args[0].IsNumeric() || !World)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.WhenReady <seconds> <console command ...>"));
			return;
		}
		const double Delay = FCString::Atod(*Args[0]);
		const FString Command = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		int32 IdleFrames = 0;
		double Waited = -1.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Delay, Command, IdleFrames, Waited](float) mutable {
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return false;
			}
			if (Waited < 0.0)
			{
				int32 Remaining = 0;
#if WITH_EDITOR
				if (GShaderCompilingManager)
				{
					Remaining += GShaderCompilingManager->GetNumRemainingJobs();
				}
				Remaining += FAssetCompilingManager::Get().GetNumRemainingAssets();
#endif
				IdleFrames = Remaining > 0 ? 0 : IdleFrames + 1;
				if (IdleFrames >= 3)
				{
					Waited = 0.0;
				}
				return true;
			}
			Waited += W->GetDeltaSeconds();
			if (Waited < Delay)
			{
				return true;
			}
			UE_LOG(LogRawBreak, Display, TEXT("rb.Player.WhenReady: %s"), *Command);
			if (GEngine)
			{
				GEngine->Exec(W, *Command);
			}
			return false;
		}));
	}

	// rb.Player.BallInHand: logs the carrying hand (state, target, hand, legality) - captures and the feel checks.
	void BallInHandDump(const TArray<FString>& /*Args*/, UWorld* World)
	{
		const ARbPlayerCharacter* Pawn = FindPawn(World);
		const URbBallInHandComponent* Hand = Pawn ? Pawn->GetBallInHand() : nullptr;
		if (!Hand)
		{
			return;
		}
		static const TCHAR* const States[] = {TEXT("Inactive"), TEXT("Carrying"), TEXT("Lowering"), TEXT("Placed"), TEXT("Refused")};
		const rb::Vec2 Target = Hand->GetTargetCore();
		const rb::Vec2 At = Hand->GetHandPlanCore();
		UE_LOG(LogRawBreak, Display, TEXT("RbBallInHand: state=%s target=(%.4f, %.4f) hand=(%.4f, %.4f) bottom=%.2fcm legal=%d"),
			States[FMath::Clamp(static_cast<int32>(Hand->GetState()), 0, 4)], Target.x, Target.y, At.x, At.y, Hand->GetBallBottomCm(),
			Hand->IsTargetLegal() ? 1 : 0);
	}

	// rb.Player.Walk <forward> <right> <seconds>: holds a movement input (-1..1 per axis) for a while (captures of the head bob).
	void Walk(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		TArray<double> V;
		if (!Pawn || !ParseNumbers(Args, 3, V))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Walk <forward> <right> <seconds>"));
			return;
		}
		const TWeakObjectPtr<ARbPlayerCharacter> Weak(Pawn);
		const FVector2D Axis(V[1], V[0]);
		double Remaining = V[2];
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, Axis, Remaining](float Dt) mutable {
			ARbPlayerCharacter* P = Weak.Get();
			Remaining -= Dt;
			if (!P || Remaining <= 0.0)
			{
				return false;
			}
			P->HandleMove(Axis);
			return true;
		}));
	}

	// rb.Player.StandInCue 1|0: a two-part cylinder at the stroke component's rendered cue pose (tip dome centre, axis), for dev
	// captures while ARbCue has no mesh (UE-4). Shaft 0.7 m r 0.7 cm (maple), butt r 1.3 cm (dark), 1.47 m in total.
	void StandInCue(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		ARbTable* Table = FindTable(World);
		if (!Pawn || !Table || (Args.Num() > 0 && Args[0] == TEXT("0")))
		{
			return;
		}
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Cylinder || !Base)
		{
			return;
		}
		struct FPiece
		{
			double From, To, RadiusCm;
			FLinearColor Color;
		};
		const FPiece Pieces[2] = {{0.0, 0.70, 0.70, FLinearColor(0.55f, 0.40f, 0.24f)}, {0.70, 1.47, 1.30, FLinearColor(0.06f, 0.025f, 0.015f)}};
		TArray<TWeakObjectPtr<AStaticMeshActor>> Actors;
		for (const FPiece& Piece : Pieces)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
			UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cylinder);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Actor);
			Mid->SetVectorParameterValue(TEXT("Color"), Piece.Color);
			Mesh->SetMaterial(0, Mid);
			Actor->SetActorHiddenInGame(true);
			Actors.Add(Actor);
		}
		const TWeakObjectPtr<ARbPlayerCharacter> WeakPawn(Pawn);
		const TWeakObjectPtr<ARbTable> WeakTable(Table);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakPawn, WeakTable, Actors, Pieces](float) {
			ARbPlayerCharacter* P = WeakPawn.Get();
			const ARbTable* T = WeakTable.Get();
			if (!P || !T)
			{
				return false;
			}
			const ERbStrokePhase Phase = P->GetStroke()->GetPhase();
			const bool bShow = Phase == ERbStrokePhase::GettingDown || Phase == ERbStrokePhase::Down || Phase == ERbStrokePhase::Contact ||
				Phase == ERbStrokePhase::Watching;
			rb::Vec3 TipCore;
			rb::Vec3 DirCore;
			P->GetStroke()->GetCuePoseCore(TipCore, DirCore);
			const FVector Tip = T->CoreToWorld(TipCore);
			const FVector Dir = T->CoreDirectionToWorld(DirCore).GetSafeNormal();
			const FQuat Along = FRotationMatrix::MakeFromZ(Dir).ToQuat(); // the engine cylinder: 100 cm along Z, radius 50 cm
			for (int32 I = 0; I < Actors.Num(); ++I)
			{
				AStaticMeshActor* A = Actors[I].Get();
				if (!A)
				{
					return false;
				}
				const double Len = (Pieces[I].To - Pieces[I].From) * 100.0;
				const FVector Centre = Tip - Dir * (100.0 * Pieces[I].From + 0.5 * Len);
				A->SetActorTransform(FTransform(Along, Centre, FVector(Pieces[I].RadiusCm / 50.0, Pieces[I].RadiusCm / 50.0, Len / 100.0)));
				A->SetActorHiddenInGame(!bShow);
			}
			return true;
		}));
	}

	void Dump(const TArray<FString>& Args, UWorld* World)
	{
		// rb.Player.Dump [<delay s>]: with a delay the state is logged later; a negative delay logs every |delay| seconds (the last
		// line before a headless capture shows the state it captured).
		TArray<double> V;
		if (ParseNumbers(Args, 1, V) && V[0] != 0.0)
		{
			const TWeakObjectPtr<UWorld> WeakWorld(World);
			const bool bRepeat = V[0] < 0.0;
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([WeakWorld, bRepeat](float) {
					UWorld* W = WeakWorld.Get();
					if (W)
					{
						Dump(TArray<FString>(), W);
					}
					return bRepeat && W != nullptr;
				}),
				static_cast<float>(FMath::Abs(V[0])));
			return;
		}
		const ARbPlayerCharacter* Pawn = FindPawn(World);
		if (!Pawn)
		{
			return;
		}
		const URbCameraRigComponent* Rig = Pawn->GetCameraRig();
		const UCineCameraComponent* Cam = Pawn->GetCamera();
		const FRbAimState& Aim = Pawn->GetStroke()->GetAim();
		UE_LOG(LogRawBreak, Display,
			TEXT("RbPlayer: phase=%s mode=%s alpha=%.3f pawn=%s eye=%s rot=%s V=%.3f H=%.3f f=%.3fmm N=%.3f sensor=%.2fx%.2f focus=%.1fcm ")
				TEXT("EV=%.2f grain=%.4f gaze=(%.1f,%.1f) aim=(%.2f deg, %.2f deg) aspect=%.4f"),
			PhaseName(Pawn->GetStroke()->GetPhase()), ModeName(Rig->GetMode()), Rig->GetTransitionAlpha(), *Pawn->GetActorLocation().ToString(),
			*Cam->GetComponentLocation().ToString(), *Cam->GetComponentRotation().ToString(), Cam->GetVerticalFieldOfView(),
			Cam->GetHorizontalFieldOfView(), Cam->CurrentFocalLength, Cam->CurrentAperture, Cam->Filmback.SensorWidth, Cam->Filmback.SensorHeight,
			Rig->GetFocusDistanceCm(), Rig->GetCurrentEv100(), Rig->GetGrainIntensity(), Rig->GetGazeYaw(), Rig->GetGazePitch(),
			FMath::RadiansToDegrees(Aim.Azimuth), FMath::RadiansToDegrees(FMath::Max(Aim.Elevation, Aim.ElevationFloor)), Rig->GetViewportAspect());
		if (Rig->GetMode() == ERbCameraRigMode::DownOnShot)
		{
			FVector Fixation;
			const FTransform Eye = Rig->GetBaseEyeTransform();
			if (Pawn->FindFixationPoint(Eye.GetLocation(), Eye.GetRotation().GetForwardVector(), Fixation))
			{
				UE_LOG(LogRawBreak, Display, TEXT("RbPlayer: fixation %s, %.1f cm along the view"), *Fixation.ToString(),
					FVector::DotProduct(Fixation - Eye.GetLocation(), Eye.GetRotation().GetForwardVector()));
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GTeleport(TEXT("rb.Player.Teleport"), TEXT("Pawn at a core plan point <x m> <y m>, view azimuth <deg> [pitch deg]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Teleport), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GGetDown(TEXT("rb.Player.GetDown"), TEXT("Get down on the shot / stand up (the GetDown input)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GetDown), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GAim(TEXT("rb.Player.Aim"), TEXT("Aim of the address: <azimuth deg> [<elevation deg> [<a> <b>]]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Aim), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GAimAt(TEXT("rb.Player.AimAt"), TEXT("Aim from the cue ball at a plan point <x m> <y m> [<elevation deg>]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AimAt), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GPreset(TEXT("rb.Player.Preset"), TEXT("Camera preset Eyes|Headcam|Broadcast."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Preset), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GFov(TEXT("rb.Player.Fov"), TEXT("Vertical FOV override [deg] (Eyes)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Fov), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GLook(TEXT("rb.Player.Look"), TEXT("Look input <yaw deg> <pitch deg> (routed like the mouse)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Look), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GLookAt(TEXT("rb.Player.LookAt"), TEXT("View turned to a core point <x m> <y m> [<z m>] of the table frame."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LookAt), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GConfirm(TEXT("rb.Player.Confirm"), TEXT("The Confirm input [<delay s>] (ball in hand: set the ball down)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Confirm), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GSettle(TEXT("rb.Player.Settle"), TEXT("Settle held 1 / released 0."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Settle), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GStroke(TEXT("rb.Player.Stroke"), TEXT("Scripted stroke through the pawn <tip speed m/s> [practice] (down on the shot)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Stroke), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GWhenReady(TEXT("rb.Player.WhenReady"),
		TEXT("<seconds> <command ...>: runs the command that long after the shader / asset compilers went idle (capture timing)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WhenReady), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GBallInHand(TEXT("rb.Player.BallInHand"), TEXT("Logs the carrying hand (RbBallInHand: ...)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BallInHandDump), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GWalk(TEXT("rb.Player.Walk"), TEXT("Movement input <forward> <right> held for <seconds>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Walk), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GStandInCue(TEXT("rb.Player.StandInCue"), TEXT("1 = cylinder stand-in at the rendered cue pose (dev captures before UE-4)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StandInCue), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GDump(TEXT("rb.Player.Dump"), TEXT("Logs the pawn / camera rig state (RbPlayer: ...)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dump), ECVF_Cheat);
}
#endif
