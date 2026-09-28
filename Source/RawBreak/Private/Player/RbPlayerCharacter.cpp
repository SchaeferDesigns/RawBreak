#include "Player/RbPlayerCharacter.h"

#include "RawBreak.h"
#include "Camera/RbCameraRigComponent.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Input/RbInputSetup.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
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
#include "HAL/IConsoleManager.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

// Owner: UE-5b. Pawn: movement settings, capsule / table collision, the input routing of the header, dev console commands
// (rb.Player.*, non-shipping) that drive the pawn of a running game for headless captures. Tests: RawBreak.Unit.Player.*,
// RawBreak.Functional.Player.PawnBlockedByTable (Private/Tests/RbCameraRigTests.cpp).

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
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	Stroke->GetDownSeconds = CameraRig->GetParams().GetDownSeconds; // the stroke's Down phase starts when the eye arrives
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

void ARbPlayerCharacter::HandleLook(const FVector2D& Axis)
{
	const ERbStrokePhase Phase = Stroke->GetPhase();
	if (Phase == ERbStrokePhase::GettingDown || Phase == ERbStrokePhase::Down)
	{
		// Aiming: X turns the cue about the cue ball; Y runs the eyes along the line (never while the mouse is the stroke).
		Stroke->AddAimInput(Axis, bFineAim);
		if (!Stroke->IsStrokeActive())
		{
			CameraRig->AddGazeInput(0.0, 0.5 * GazeDegreesPerLookUnit * Axis.Y * (bFineAim ? Stroke->FineAimScale : 1.0));
		}
		return;
	}
	if (CameraRig->GetMode() == ERbCameraRigMode::DownOnShot)
	{
		// Still down after the contact (or locked while down): the head follows the balls.
		CameraRig->AddGazeInput(GazeDegreesPerLookUnit * Axis.X, GazeDegreesPerLookUnit * Axis.Y);
		return;
	}
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
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
		Stroke->ConfirmPressed();
		return;
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
void ARbPlayerCharacter::OnStroke(const FInputActionValue& Value) { Stroke->SetStrokeHeld(Value.Get<bool>()); }
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
		if (const ARbGameMode* GameMode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr)
		{
			if (GameMode->GetTable())
			{
				return GameMode->GetTable();
			}
		}
		for (TActorIterator<ARbTable> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
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

	// rb.Player.Look <yaw deg> <pitch deg>: a look input in degrees, routed like the mouse (aim while down, head after the shot).
	void Look(const TArray<FString>& Args, UWorld* World)
	{
		ARbPlayerCharacter* Pawn = FindPawn(World);
		TArray<double> V;
		if (!Pawn || !ParseNumbers(Args, 2, V))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Player.Look <yaw deg> <pitch deg>"));
			return;
		}
		const double Scale = FMath::Max(1.0e-6, static_cast<double>(Pawn->GazeDegreesPerLookUnit));
		Pawn->HandleLook(FVector2D(V[0] / Scale, V[1] / Scale));
	}

	void Settle(const TArray<FString>& Args, UWorld* World)
	{
		if (ARbPlayerCharacter* Pawn = FindPawn(World))
		{
			Pawn->HandleSettle(Args.Num() == 0 || Args[0] != TEXT("0"));
		}
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
	FAutoConsoleCommandWithWorldAndArgs GSettle(TEXT("rb.Player.Settle"), TEXT("Settle held 1 / released 0."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Settle), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GWalk(TEXT("rb.Player.Walk"), TEXT("Movement input <forward> <right> held for <seconds>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Walk), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GStandInCue(TEXT("rb.Player.StandInCue"), TEXT("1 = cylinder stand-in at the rendered cue pose (dev captures before UE-4)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StandInCue), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GDump(TEXT("rb.Player.Dump"), TEXT("Logs the pawn / camera rig state (RbPlayer: ...)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dump), ECVF_Cheat);
}
#endif
