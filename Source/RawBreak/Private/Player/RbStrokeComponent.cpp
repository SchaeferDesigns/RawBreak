#include "Player/RbStrokeComponent.h"

// Owner: UE-5a, M2-F. Stroke state machine (Docs/ue-architecture.md 6.2, 18.3; the header documents every rule).

#include "RawBreak.h"
#include "Camera/RbCameraRigComponent.h"
#include "Core/RbCoords.h"
#include "Cue/RbCue.h"
#include "Cue/RbCueClearance.h"
#include "Input/RbAimResponse.h"
#include "Input/RbRawMouseInput.h"
#include "Player/RbBallInHandComponent.h"
#include "Settings/RbGameUserSettings.h"
#include "Table/RbTable.h"

#include "Engine/World.h"
#include "rb/Human/NoiseHash.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#include "rb/Physics/CueStrike.h"

namespace
{
	// ForwardStart of a stroke without a committed forward stroke: far in the future, so SampleHand shows no ramp.
	constexpr double kNoRampOffset = 1.0e6;
	constexpr double kDefaultPause = 0.4; // IntendedStroke::PauseDuration default (no final stroke yet)

	double WrapAngle(double A)
	{
		return FMath::UnwindRadians(A);
	}
}

URbStrokeComponent::URbStrokeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = true; // only to notice a pause: a held stroke is dropped (TickComponent)
}

void URbStrokeComponent::SetTable(ARbTable* InTable)
{
	Table = InTable;
	bFloorDirty = true;
}

void URbStrokeComponent::SetCue(ARbCue* InCue)
{
	Cue = InCue;
}

double URbStrokeComponent::ClockNow() const
{
	return ClockOverride ? ClockOverride() : FPlatformTime::Seconds();
}

bool URbStrokeComponent::HasTrueTimestamps() const
{
	return RawMouse.IsValid() && RawMouse->HasTrueTimestamps();
}

double URbStrokeComponent::StrokeMetersPerCount() const
{
	// Stroke sensitivity scales the hand metres per count relative to the calibrated DPI (ui-ux 13.6); 1 = the M1 mapping, exactly.
	const double Sensitivity = FMath::Clamp(static_cast<double>(Controls.StrokeSensitivity), 0.1, 10.0);
	return RbStrokeMath::CountsToMeters(1.0, MouseDpi) * Sensitivity;
}

void URbStrokeComponent::RebaseAim()
{
	AimBase = Aim.Azimuth;
	AimCoarseCounts = 0.0;
	AimFineCounts = 0.0;
	AimAccelRadians = 0.0;
}

void URbStrokeComponent::ApplyUserSettings()
{
	if (const URbGameUserSettings* Settings = URbGameUserSettings::Get())
	{
		MouseDpi = FMath::Max(1.0, static_cast<double>(Settings->MouseDpi));
		Controls = Settings->Controls;
		bHardcore = Settings->bHardcoreStroke;
	}
	RebaseAim(); // the new factors apply from the current azimuth on
}

void URbStrokeComponent::PushRigContext()
{
	if (URbCameraRigComponent* Rig = FindRig())
	{
		// Never two identical get-downs, reproducibly: the posture seed of this address (the rig adds its change counter).
		Rig->SetMotionSeed(rb::human::HashKeys(Context.Key.MatchSeed, rb::human::ShooterKey(Context.Key), static_cast<uint64>(Context.Key.ShooterShotIndex),
			static_cast<uint64>(AddressIndex)));
		Rig->SetPressure(Context.Situation.Pressure);
	}
}

URbBallInHandComponent* URbStrokeComponent::FindBallInHand() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<URbBallInHandComponent>() : nullptr;
}

bool URbStrokeComponent::GetPlacementTargetCore(rb::Vec2& OutPlan) const
{
	if (Phase != ERbStrokePhase::PlacingCueBall)
	{
		return false;
	}
	if (const URbBallInHandComponent* Hand = FindBallInHand())
	{
		if (Hand->GetState() != ERbBallInHandState::Inactive && Hand->HasTarget())
		{
			OutPlan = Hand->GetTargetCore();
			return true;
		}
	}
	if (!bHasPlacement)
	{
		return false;
	}
	const ARbTable* TableActor = Table.Get();
	const rb::Vec3 Core = TableActor ? TableActor->WorldToCore(PlacementWorld) : FRbCoords::PositionToCore(PlacementWorld);
	OutPlan = rb::Vec2(Core.x, Core.y);
	return true;
}

void URbStrokeComponent::HandleBallSetDown(const rb::Vec2& PlanCore)
{
	// The carried ball touched the cloth at exactly the previewed spot: that is the placement (the director validates it again).
	if (Phase != ERbStrokePhase::PlacingCueBall)
	{
		return;
	}
	const URbBallInHandComponent* Hand = FindBallInHand();
	const double R = Hand ? Hand->GetBallRadius() : CueBallRadius;
	PlacementWorld = CoreToWorld(rb::Vec3(PlanCore.x, PlanCore.y, R));
	bHasPlacement = true;
	OnCueBallPlaced.Broadcast(PlacementWorld);
}

// ---------------------------------------------------------------------------------------------------------
// Director -> component
// ---------------------------------------------------------------------------------------------------------

void URbStrokeComponent::BeginAddress(const rb::Vec3& InCueBallPosition, double InCueBallRadius)
{
	EndStroke();
	CueBallPosition = InCueBallPosition;
	CueBallRadius = InCueBallRadius;
	AddressIndex = 0;
	Context.Key.AddressIndex = 0;
	ResetAddress();
	Aim.Elevation = 0.0;
	Aim.AxisOffsetA = 0.0;
	Aim.AxisOffsetB = 0.0;
	RebaseAim();
	bAimSetExplicitly = false;
	SettleSince = -1.0;
	bFloorDirty = true;
	bHasPlacement = false;
	PushRigContext();
	SetPhase(ERbStrokePhase::Walking);
}

void URbStrokeComponent::SetStrokeContext(const FRbStrokeContext& InContext)
{
	Context = InContext;
	Context.Key.AddressIndex = AddressIndex;
	Aim.BridgeLength = Context.Situation.BridgeLength;
	// The floor fields are the component's (RbCueClearance); keep the current ones until the next update.
	Context.Situation.ElevationFloor = Aim.ElevationFloor;
	bFloorDirty = true;
	PushRigContext();
}

void URbStrokeComponent::SetLocked(bool bLocked)
{
	if (bLocked)
	{
		if (Phase != ERbStrokePhase::Locked)
		{
			EndStroke();
			SetPhase(ERbStrokePhase::Locked);
		}
	}
	else if (Phase == ERbStrokePhase::Locked)
	{
		SetPhase(ERbStrokePhase::Walking);
	}
}

void URbStrokeComponent::BeginCueBallPlacement()
{
	EndStroke();
	bHasPlacement = false;
	SetPhase(ERbStrokePhase::PlacingCueBall);
	// The hand picks the cue ball up (again after a refused placement): carried over the look point until Confirm sets it down.
	if (URbBallInHandComponent* Hand = FindBallInHand())
	{
		if (Hand->GetState() != ERbBallInHandState::Carrying && Hand->GetState() != ERbBallInHandState::Refused)
		{
			const double R = Context.CueBall.Radius > 0.0 ? Context.CueBall.Radius : CueBallRadius;
			Hand->BeginCarry(Table.Get(), 0, R, PlacementValidator);
		}
		if (!SetDownHandle.IsValid())
		{
			SetDownHandle = Hand->OnSetDown.AddUObject(this, &URbStrokeComponent::HandleBallSetDown);
		}
		UpdatePlacementPoint();
	}
}

// ---------------------------------------------------------------------------------------------------------
// Input -> component
// ---------------------------------------------------------------------------------------------------------

void URbStrokeComponent::RequestGetDownToggle()
{
	const double T = ClockNow();
	switch (Phase)
	{
	case ERbStrokePhase::Walking:
		if (!IsCueBallInReach())
		{
			return;
		}
		if (!bAimSetExplicitly)
		{
			InitAimFromView();
		}
		RebaseAim();
		ResetAddress();
		CueDisplacement = -AddressDistance;
		DownSince = T + GetDownSeconds;
		LastTickTime = T;
		SettleSince = -1.0;
		bFloorDirty = true;
		PushRigContext();
		SetPhase(ERbStrokePhase::GettingDown);
		// Down starts when the eye arrives: the rig's human get-down (seeded duration; Quick 0.4 s, Cut 0); no rig = GetDownSeconds.
		if (const URbCameraRigComponent* Rig = FindRig())
		{
			DownSince = T + Rig->GetPostureArrivalSeconds();
		}
		UpdatePresentation();
		break;
	case ERbStrokePhase::GettingDown:
	case ERbStrokePhase::Down:
		ProcessStrokeSamples(T);
		if (Phase != ERbStrokePhase::Down && Phase != ERbStrokePhase::GettingDown)
		{
			return; // the pending samples reached the ball
		}
		EndStroke();
		++AddressIndex; // standing up and getting down again gives new drift / tremor processes (HF 3.2)
		Context.Key.AddressIndex = AddressIndex;
		SettleSince = -1.0;
		PushRigContext();
		SetPhase(ERbStrokePhase::Walking);
		break;
	case ERbStrokePhase::Watching:
		if (URbCameraRigComponent* Rig = FindRig())
		{
			Rig->SetMode(ERbCameraRigMode::Standing); // stand up and watch; the director unlocks the next shot
		}
		break;
	default:
		break;
	}
}

void URbStrokeComponent::AddAimInput(const FVector2D& LookCounts, bool bFine, double DeltaSeconds)
{
	if (Phase != ERbStrokePhase::Down && Phase != ERbStrokePhase::GettingDown)
	{
		return;
	}
	if (bStrokeHeld || bForwardPhase)
	{
		// The aim is frozen during a stroke. Look input that the mouse stroke does not explain (not the Stroke button's
		// own mouse motion) after ForwardStart is a head movement (HF-10).
		if (bCommittedStroke && !bStrokeHeld)
		{
			HeadMoveAccum += (FMath::Abs(LookCounts.X) + FMath::Abs(LookCounts.Y)) * RbAimResponse::RadiansPerCount(bFine, MouseDpi, Controls);
		}
		return;
	}
	// P3: counts -> cm -> degrees (RbAimResponse). The linear path sums the integer counts and applies the factor once, so any
	// frame split of the same counts gives bitwise the same azimuth; the acceleration (optional) accumulates radians.
	if (Controls.AimAcceleration > 0.0f)
	{
		AimAccelRadians += RbAimResponse::AimRadians(LookCounts.X, DeltaSeconds, bFine, MouseDpi, Controls);
	}
	else if (bFine)
	{
		AimFineCounts += LookCounts.X;
	}
	else
	{
		AimCoarseCounts += LookCounts.X;
	}
	// Mouse right (+X) turns the aim clockwise seen from above: the core azimuth (counter-clockwise, +y = left) decreases.
	const double Turn = AimCoarseCounts * RbAimResponse::RadiansPerCount(false, MouseDpi, Controls) +
		AimFineCounts * RbAimResponse::RadiansPerCount(true, MouseDpi, Controls) + AimAccelRadians;
	Aim.Azimuth = WrapAngle(AimBase - Turn);
	bFloorDirty = true;
}

void URbStrokeComponent::AddElevationInput(float Steps)
{
	if ((Phase != ERbStrokePhase::Down && Phase != ERbStrokePhase::GettingDown) || bForwardPhase)
	{
		return;
	}
	Aim.Elevation = FMath::Clamp(Aim.Elevation + Steps * ElevationStep, 0.0, MaxElevation);
	bFloorDirty = true;
}

void URbStrokeComponent::AddTipOffsetInput(const FVector2D& Delta)
{
	if ((Phase != ERbStrokePhase::Down && Phase != ERbStrokePhase::GettingDown) || bForwardPhase)
	{
		return;
	}
	double A = Aim.AxisOffsetA + Delta.X * TipOffsetStep;
	double B = Aim.AxisOffsetB + Delta.Y * TipOffsetStep;
	const double Rho = FMath::Sqrt(A * A + B * B);
	if (Rho > MaxTipOffset && Rho > 0.0)
	{
		A *= MaxTipOffset / Rho;
		B *= MaxTipOffset / Rho;
	}
	Aim.AxisOffsetA = A;
	Aim.AxisOffsetB = B;
	bFloorDirty = true;
}

void URbStrokeComponent::SetStrokeHeld(bool bHeld)
{
	const double T = ClockNow();
	if (bHeld)
	{
		if (bStrokeHeld || bStrokeNeedsRelease)
		{
			return; // Triggered repeats while held
		}
		if (Phase == ERbStrokePhase::PlacingCueBall)
		{
			bStrokeNeedsRelease = true; // left mouse is not bound twice: in hand it confirms (R-17)
			ConfirmPressed();
			return;
		}
		bStrokeHeld = true;
		StrokeHeldSince = T;
		if (Phase == ERbStrokePhase::Down)
		{
			if (!bScriptedStroke)
			{
				BeginStroke(true, FMath::Max(T, DownSince)); // older reports (look input) are filtered by StrokeHeldSince
			}
			else
			{
				TakeOverExhaustedScriptedStroke(FMath::Max(T, DownSince)); // a used-up scripted stream never blocks the button
			}
		}
		return;
	}
	bStrokeNeedsRelease = false;
	if (!bStrokeHeld)
	{
		return;
	}
	if (Phase == ERbStrokePhase::Down)
	{
		ProcessStrokeSamples(T); // the samples up to the release still count
	}
	bStrokeHeld = false;
	if (bStrokeActive && !bScriptedStroke)
	{
		EndStroke();
	}
}

void URbStrokeComponent::SetCommitHeld(bool bHeld)
{
	if (bHeld == bCommitHeld)
	{
		return;
	}
	const double T = ClockNow();
	if (Phase == ERbStrokePhase::Down)
	{
		ProcessStrokeSamples(T); // samples before the change keep the old state
	}
	bCommitHeld = bHeld;
	if (!IsLive())
	{
		AbortCommittedStroke();
	}
}

void URbStrokeComponent::SetSettleHeld(bool bHeld)
{
	if (!bHeld)
	{
		SettleSince = -1.0; // breathing again ends the settle (HF-06 "exhale and hold")
		return;
	}
	if (Phase == ERbStrokePhase::Down && SettleSince < 0.0)
	{
		SettleSince = ClockNow();
	}
}

void URbStrokeComponent::ConfirmPressed()
{
	if (Phase != ERbStrokePhase::PlacingCueBall)
	{
		return;
	}
	UpdatePlacementPoint();
	if (URbBallInHandComponent* Hand = FindBallInHand())
	{
		if (Hand->GetState() != ERbBallInHandState::Inactive)
		{
			// The hand sets the ball down on exactly the previewed spot (OnSetDown -> OnCueBallPlaced when it touches the cloth), or
			// hesitates at an illegal spot (Refused: nothing is placed).
			Hand->RequestSetDown();
			return;
		}
	}
	const FVector World = bHasPlacement ? PlacementWorld : CoreToWorld(CueBallPosition);
	OnCueBallPlaced.Broadcast(World);
}

void URbStrokeComponent::NotifyWorldPaused(bool bPaused)
{
	if (bPaused == bWorldPaused)
	{
		return;
	}
	bWorldPaused = bPaused;
	if (!bPaused)
	{
		return;
	}
	// Pausing drops a held stroke (18.2): a committed one aborts (its shown ramp is spent), the scripted samples still to come are
	// discarded (their times pass during the pause), and a Stroke button still held on resume must be released first.
	const bool bWasHeld = bStrokeHeld;
	if (Phase == ERbStrokePhase::Down || Phase == ERbStrokePhase::GettingDown)
	{
		EndStroke();
	}
	bStrokeHeld = false;
	bStrokeNeedsRelease = bStrokeNeedsRelease || bWasHeld;
	PendingScripted.Reset();
	PendingScriptedStarts.Reset();
	bScriptedRestart = false;
	if (RawMouse.IsValid() && RawMouse->IsActive())
	{
		RawMouse->Reset();
	}
	UE_LOG(LogRawBreak, Log, TEXT("RbStroke: world paused - the held stroke was dropped (button held: %d)"), bWasHeld ? 1 : 0);
}

void URbStrokeComponent::InjectStrokeSamples(const TArray<FRbStrokeSample>& Samples, bool bNewStroke)
{
	if (Samples.Num() == 0)
	{
		return;
	}
	const int32 First = PendingScripted.Num();
	PendingScripted.Append(Samples);
	PendingScriptedStarts.AddZeroed(Samples.Num());
	PendingScriptedStarts[First] = bNewStroke;
}

TArray<FRbStrokeSample> URbStrokeComponent::MakeScriptedStroke(double TipSpeed, double StartTime) const
{
	FRbScriptedStroke Params;
	Params.TipSpeed = TipSpeed;
	Params.StartTime = StartTime;
	Params.StartCueDisplacement = FMath::Min(CueDisplacement, -0.001);
	Params.MaxBackswing = MaxBackswing;
	Params.ForwardTravel = FMath::Min(0.20, 0.8 * MaxBackswing);
	TArray<FRbStrokeSample> Samples;
	RbStrokeMath::MakeScriptedStroke(Params, Gain, Samples);
	return Samples;
}

void URbStrokeComponent::SetAim(double Azimuth, double Elevation, double AxisOffsetA, double AxisOffsetB)
{
	Aim.Azimuth = WrapAngle(Azimuth);
	RebaseAim();
	Aim.Elevation = FMath::Clamp(Elevation, 0.0, MaxElevation);
	Aim.AxisOffsetA = AxisOffsetA;
	Aim.AxisOffsetB = AxisOffsetB;
	bAimSetExplicitly = true;
	bFloorDirty = true;
}

// ---------------------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------------------

void URbStrokeComponent::BeginPlay()
{
	Super::BeginPlay();
	RawMouse = FRbRawMouseInput::Create();
	if (bApplyUserSettings)
	{
		ApplyUserSettings();
		SettingsHandle = URbGameUserSettings::OnSettingsChanged().AddWeakLambda(this, [this]() { ApplyUserSettings(); });
	}
}

void URbStrokeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SettingsHandle.IsValid())
	{
		URbGameUserSettings::OnSettingsChanged().Remove(SettingsHandle);
		SettingsHandle.Reset();
	}
	if (URbBallInHandComponent* Hand = FindBallInHand())
	{
		Hand->OnSetDown.Remove(SetDownHandle);
	}
	SetDownHandle.Reset();
	RawMouse.Reset();
	Super::EndPlay(EndPlayReason);
}

void URbStrokeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	const bool bPaused = World && World->IsPaused();
	NotifyWorldPaused(bPaused);
	if (bPaused)
	{
		// The raw reports of the pause (menu mouse moves) never reach a stroke.
		if (RawMouse.IsValid() && RawMouse->IsActive())
		{
			RawMouse->Reset();
		}
		return;
	}
	TickStroke(ClockNow());
}

void URbStrokeComponent::TickStroke(double NowSeconds)
{
	LastTickTime = NowSeconds;
	if (Phase == ERbStrokePhase::GettingDown && NowSeconds >= DownSince)
	{
		SetPhase(ERbStrokePhase::Down);
		if (bStrokeHeld && !bStrokeActive && !bStrokeNeedsRelease)
		{
			BeginStroke(true, DownSince); // Stroke held through the get-down: reports before DownSince are filtered
		}
	}
	if (Phase == ERbStrokePhase::PlacingCueBall)
	{
		UpdatePlacementPoint();
	}
	ProcessStrokeSamples(NowSeconds);
	UpdatePresentation();
}

void URbStrokeComponent::SetPhase(ERbStrokePhase NewPhase)
{
	const ERbStrokePhase Old = Phase;
	Phase = NewPhase;
	if (Old == NewPhase)
	{
		return;
	}
	URbCameraRigComponent* Rig = FindRig();
	ARbCue* CueActor = Cue.Get();
	if (Rig)
	{
		Rig->SetWatching(NewPhase == ERbStrokePhase::Contact || NewPhase == ERbStrokePhase::Watching);
	}
	if (Old == ERbStrokePhase::PlacingCueBall)
	{
		// Leaving the placement without setting the ball down (locked, a new address, a replay): the hand lets go of it.
		if (URbBallInHandComponent* Hand = FindBallInHand())
		{
			if (Hand->GetState() == ERbBallInHandState::Carrying || Hand->GetState() == ERbBallInHandState::Refused ||
				Hand->GetState() == ERbBallInHandState::Lowering)
			{
				Hand->Cancel();
			}
		}
	}
	switch (NewPhase)
	{
	case ERbStrokePhase::GettingDown:
		if (Rig)
		{
			Rig->SetMode(ERbCameraRigMode::DownOnShot);
		}
		if (CueActor)
		{
			CueActor->SetDrive(ERbCueDrive::Input);
		}
		break;
	case ERbStrokePhase::Walking:
	case ERbStrokePhase::PlacingCueBall:
		if (Rig)
		{
			Rig->SetMode(NewPhase == ERbStrokePhase::Walking ? ERbCameraRigMode::Standing : ERbCameraRigMode::BallInHand);
		}
		if (CueActor && CueActor->GetDrive() == ERbCueDrive::Input)
		{
			CueActor->SetDrive(ERbCueDrive::Hidden);
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Samples
// ---------------------------------------------------------------------------------------------------------

void URbStrokeComponent::ResetAddress()
{
	HandSamples.Reset();
	CueSamples.Reset();
	StrokeStartIndex = 0;
	RawCountsX = 0;
	RawCountsY = 0;
	bStrokeActive = false;
	bScriptedStroke = false;
	bForwardPhase = false;
	bCommittedStroke = false;
	bRampShownLatched = false;
	HeadMoveAccum = 0.0;
	PauseDuration = kDefaultPause;
	HandPose = rb::human::HandPose();
}

void URbStrokeComponent::BeginStroke(bool bRawSource, double StartTime)
{
	bStrokeActive = true;
	bScriptedStroke = !bRawSource;
	StrokeStartIndex = HandSamples.Num();
	Integrator.Reset(CueDisplacement);
	bForwardPhase = false;
	BackMostX = CueDisplacement;
	BackMostIndex = StrokeStartIndex;
	ForwardPeakX = CueDisplacement;
	bCommittedStroke = false;
	bRampShownLatched = false;
	HeadMoveAccum = 0.0;
	PauseDuration = kDefaultPause;
	if (bRawSource)
	{
		// Reference sample at the press: the first report after it already moves the cue.
		const double MetersPerCount = StrokeMetersPerCount();
		FRbStrokeSample Reference;
		Reference.Time = StartTime;
		Reference.Position = -static_cast<double>(RawCountsY) * MetersPerCount;
		Reference.Lateral = static_cast<double>(RawCountsX) * MetersPerCount;
		ProcessHandSample(Reference);
	}
}

void URbStrokeComponent::EndStroke()
{
	AbortCommittedStroke();
	bStrokeActive = false;
	bScriptedStroke = false;
	bForwardPhase = false;
}

bool URbStrokeComponent::TakeOverExhaustedScriptedStroke(double StartTime)
{
	if (!bStrokeHeld || bStrokeNeedsRelease || Phase != ERbStrokePhase::Down || !bStrokeActive || !bScriptedStroke || PendingScripted.Num() > 0)
	{
		return false;
	}
	// The raw stroke starts where the scripted one stopped at the earliest (its reports stay time ordered after it).
	double Start = FMath::Max(StartTime, DownSince);
	if (HandSamples.Num() > StrokeStartIndex)
	{
		Start = FMath::Max(Start, HandSamples.Last().Time);
	}
	EndStroke(); // the used-up scripted stroke ends without contact (a committed one aborts)
	if (Phase != ERbStrokePhase::Down)
	{
		return false; // an abort listener locked / moved the component
	}
	BeginStroke(true, Start);
	return true;
}

void URbStrokeComponent::ProcessStrokeSamples(double NowSeconds)
{
	// Drained reports in a reused buffer (no per-frame allocation). Moved out for the call, so a delegate listener that calls
	// back into the component (OnStrokeAborted / OnStrokeContact -> SetCommitHeld ...) gets its own buffer.
	TArray<FRbRawMouseReport> Reports = MoveTemp(ReportScratch);
	Reports.Reset();
	ON_SCOPE_EXIT
	{
		Reports.Reset();
		ReportScratch = MoveTemp(Reports);
	};
	if (RawMouse.IsValid() && RawMouse->IsActive())
	{
		RawMouse->Drain(Reports); // always drained: outside a stroke the reports are look input only
	}
	if (Phase != ERbStrokePhase::Down)
	{
		return; // scripted samples wait for Down (the ones older than DownSince are dropped then)
	}

	// Scripted samples whose time has come, in time order. The first sample of each InjectStrokeSamples call (bNewStroke)
	// begins a new stroke with its own hand origin.
	int32 Consumed = 0;
	bool bContact = false;
	while (Consumed < PendingScripted.Num() && PendingScripted[Consumed].Time <= NowSeconds)
	{
		const FRbStrokeSample Sample = PendingScripted[Consumed];
		bScriptedRestart |= PendingScriptedStarts[Consumed];
		++Consumed;
		if (Sample.Time < DownSince)
		{
			continue;
		}
		if (!bStrokeActive || !bScriptedStroke || bScriptedRestart)
		{
			if (bStrokeActive)
			{
				EndStroke();
			}
			bScriptedRestart = false;
			BeginStroke(false, Sample.Time);
		}
		else if (HandSamples.Num() > StrokeStartIndex && Sample.Time < HandSamples.Last().Time)
		{
			continue; // older than the stroke's last sample: the integrator and the fits need time order
		}
		if (!ProcessHandSample(Sample))
		{
			bContact = true;
			break;
		}
	}
	if (bContact)
	{
		PendingScripted.Reset();
		PendingScriptedStarts.Reset();
		bScriptedRestart = false;
		return;
	}
	if (Consumed > 0)
	{
		PendingScripted.RemoveAt(0, Consumed, EAllowShrinking::No);
		PendingScriptedStarts.RemoveAt(0, Consumed, EAllowShrinking::No);
	}

	// Stroke button held while a scripted stream ran out: the button's raw reports drive the cue from now on.
	TakeOverExhaustedScriptedStroke(StrokeHeldSince);

	// Raw reports of the Stroke button (true timestamps from the input thread, or the reconstructed fallback times).
	if (bStrokeActive && !bScriptedStroke && bStrokeHeld && Phase == ERbStrokePhase::Down)
	{
		const double MetersPerCount = StrokeMetersPerCount();
		for (const FRbRawMouseReport& Report : Reports)
		{
			if (Report.Time < StrokeHeldSince || Report.Time < DownSince ||
				(HandSamples.Num() > StrokeStartIndex && Report.Time < HandSamples.Last().Time))
			{
				continue;
			}
			RawCountsX += Report.DeltaX;
			RawCountsY += Report.DeltaY;
			FRbStrokeSample Sample;
			Sample.Time = Report.Time;
			Sample.Position = -static_cast<double>(RawCountsY) * MetersPerCount; // + Y = toward the user = backward
			Sample.Lateral = static_cast<double>(RawCountsX) * MetersPerCount;
			if (!ProcessHandSample(Sample))
			{
				break;
			}
		}
	}
}

bool URbStrokeComponent::ProcessHandSample(const FRbStrokeSample& Sample)
{
	HandSamples.Add(Sample);
	const int32 Index = HandSamples.Num() - 1;
	const FRbCueStepResult Step = Integrator.Step(Sample, Gain, IsLive(), PracticeStopShort, MaxBackswing);
	FRbStrokeSample CueSample;
	CueSample.Time = Sample.Time;
	CueSample.Position = Integrator.GetX();
	CueSamples.Add(CueSample);
	if (Step.bCrossed)
	{
		OnCrossing(Step.CrossingTime, Index);
		return false;
	}
	CueDisplacement = Integrator.GetX();
	if (Step.bReference)
	{
		BackMostX = CueDisplacement;
		BackMostIndex = Index;
		ForwardPeakX = CueDisplacement;
		return true;
	}
	TrackForwardStroke(Index);
	return true;
}

void URbStrokeComponent::TrackForwardStroke(int32 Index)
{
	const double X = CueSamples[Index].Position;
	const double Time = CueSamples[Index].Time;
	if (!bForwardPhase)
	{
		if (X <= BackMostX)
		{
			BackMostX = X;
			BackMostIndex = Index;
			return;
		}
		if (X > BackMostX + ForwardHysteresis)
		{
			// Pause at the back (HF-08): the samples within PauseTolerance of the back-most point. A still mouse sends no
			// reports, so the silence counts; the departure is at most one report interval before the first sample
			// beyond the band.
			const double Band = BackMostX + PauseTolerance;
			int32 First = BackMostIndex;
			while (First - 1 >= StrokeStartIndex && CueSamples[First - 1].Position <= Band)
			{
				--First;
			}
			int32 Last = BackMostIndex;
			while (Last + 1 < Index && CueSamples[Last + 1].Position <= Band)
			{
				++Last;
			}
			const double Departure = FMath::Max(CueSamples[Last].Time, CueSamples[Last + 1].Time - MaxReportInterval);
			PauseDuration = FMath::Max(0.0, Departure - CueSamples[First].Time);
			ForwardMotionStart = Departure;
			ForwardMotionIndex = Last;
			bForwardPhase = true;
			ForwardPeakX = X;
			if (IsLive())
			{
				StartCommittedStroke(ForwardMotionStart, ForwardMotionIndex);
			}
		}
		return;
	}
	ForwardPeakX = FMath::Max(ForwardPeakX, X);
	if (IsLive() && !bCommittedStroke)
	{
		StartCommittedStroke(Time, Index); // Commit pressed during the forward stroke
	}
	else if (!IsLive() && bCommittedStroke)
	{
		AbortCommittedStroke();
	}
	if (X < ForwardPeakX - ReverseHysteresis)
	{
		// The forward stroke turned back without reaching the ball: a new backswing begins.
		AbortCommittedStroke();
		bForwardPhase = false;
		BackMostX = X;
		BackMostIndex = Index;
	}
}

void URbStrokeComponent::StartCommittedStroke(double StartTime, int32 StartIndex)
{
	bCommittedStroke = true;
	ForwardStart = StartTime;
	ForwardStartIndex = StartIndex;
	bRampShownLatched = false;
	HeadMoveAccum = 0.0;
}

void URbStrokeComponent::AbortCommittedStroke()
{
	if (!bCommittedStroke)
	{
		return;
	}
	const bool bShown = bRampShownLatched;
	bCommittedStroke = false;
	bRampShownLatched = false;
	HeadMoveAccum = 0.0;
	UE_LOG(LogRawBreak, Verbose, TEXT("RbStroke: committed stroke aborted (ramp shown: %d)"), bShown ? 1 : 0);
	OnStrokeAborted.Broadcast(bShown); // the director spends the draws (HF-B13) and pushes a new context
}

rb::human::IntendedStroke URbStrokeComponent::BuildIntended(double AbsoluteTime, double Speed, double Acceleration, double LateralNow,
	double LateralVelocity) const
{
	rb::human::IntendedStroke I;
	const double Lb = Context.Situation.BridgeLength;
	const double Lbg = Context.Situation.BridgeToGrip;
	const double R = Context.CueBall.Radius;
	double Yaw = 0.0;
	double Shift = 0.0;
	double SwoopRight = 0.0;
	if (bCommittedStroke && HandSamples.IsValidIndex(ForwardStartIndex) && R > 0.0 && Lbg > 0.0)
	{
		// Steering through the bridge pivot (plan 5.4, human-factors 3.5): lever L_b + R to the ball centre.
		const double GripLateral = SteeringGain * (LateralNow - HandSamples[ForwardStartIndex].Lateral);
		RbStrokeMath::Steering(GripLateral, Lbg, Lb + R, Yaw, Shift);
		SwoopRight = -SteeringGain * LateralVelocity * Lb / Lbg;
	}
	I.Azimuth = Aim.Azimuth + Yaw;
	I.Elevation = FMath::Max(Aim.Elevation, Aim.ElevationFloor); // the butt rises automatically over an obstacle (plan 5.5)
	I.AxisOffsetA = Aim.AxisOffsetA + (R > 0.0 ? Shift / R : 0.0);
	I.AxisOffsetB = Aim.AxisOffsetB;
	I.Speed = Speed;
	I.TipVelocityRight = SwoopRight;
	I.TipVelocityUp = 0.0;
	I.TimeDown = AbsoluteTime - DownSince;
	I.ForwardStart = bCommittedStroke ? ForwardStart - DownSince : I.TimeDown + kNoRampOffset;
	I.SettleStart = SettleSince >= 0.0 ? SettleSince - DownSince : -1.0;
	I.PauseDuration = bForwardPhase || bCommittedStroke ? PauseDuration : kDefaultPause;
	I.ContactAcceleration = Acceleration;
	I.HeadMovedBeforeContact = HeadMoveAccum > HeadMoveThreshold;
	return I;
}

rb::human::HandPose URbStrokeComponent::SampleHandAt(const rb::human::IntendedStroke& Intended, double TimeSinceDown) const
{
	return rb::human::SampleHand(Intended, Context.Attributes, Context.Situation, Context.CueBody, Context.Cue, Context.CueBall, Context.Key,
		Context.History, Context.Params, TimeSinceDown);
}

void URbStrokeComponent::OnCrossing(double CrossingTime, int32 CrossingIndex)
{
	// Tip speed at contact: quadratic fit of the hand samples of this stroke over [t_c - window, crossing sample],
	// evaluated AT t_c, mapped through the gain curve (T9, T10). At least 3 samples (slow report rates widen the window).
	const FRbStrokeSample* Stroke = HandSamples.GetData() + StrokeStartIndex;
	const int32 Count = CrossingIndex - StrokeStartIndex + 1;
	const double WindowEnd = HandSamples[CrossingIndex].Time;
	double WindowStart = CrossingTime - FitWindowSeconds;
	int32 InWindow = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		InWindow += Stroke[i].Time >= WindowStart - 1.0e-9 ? 1 : 0;
	}
	if (InWindow < 3 && Count >= 3)
	{
		WindowStart = Stroke[Count - 3].Time;
	}
	double HandVelocity = 0.0;
	double HandAcceleration = 0.0;
	if (!RbStrokeMath::QuadraticFit(Stroke, Count, CrossingTime, WindowStart, WindowEnd, HandVelocity, HandAcceleration))
	{
		// Two samples only: the secant of the crossing step.
		const FRbStrokeSample& P0 = HandSamples[CrossingIndex - 1];
		const FRbStrokeSample& P1 = HandSamples[CrossingIndex];
		HandVelocity = (P1.Position - P0.Position) / FMath::Max(P1.Time - P0.Time, FRbCueIntegrator::MinStep);
		HandAcceleration = 0.0;
	}
	const double Speed = FMath::Clamp(RbStrokeMath::CueSpeedFromHandSpeed(HandVelocity, Gain), 0.0, Gain.VTipMax);
	const double Acceleration = RbStrokeMath::CueSpeedDerivative(HandVelocity, Gain) * HandAcceleration;

	// Steering at the crossing: lateral position interpolated inside the step, lateral velocity from the same fit.
	const FRbStrokeSample& P0 = HandSamples[CrossingIndex - 1];
	const FRbStrokeSample& P1 = HandSamples[CrossingIndex];
	const double Frac = P1.Time > P0.Time ? (CrossingTime - P0.Time) / (P1.Time - P0.Time) : 1.0;
	const double LateralAtContact = P0.Lateral + (P1.Lateral - P0.Lateral) * Frac;
	double LateralVelocity = 0.0;
	double LateralAcceleration = 0.0;
	RbStrokeMath::QuadraticFitLateral(Stroke, Count, CrossingTime, WindowStart, WindowEnd, LateralVelocity, LateralAcceleration);

	if (bFloorDirty)
	{
		UpdateElevationFloor(); // the committed context carries the floor of the aim at contact
	}
	if (!bCommittedStroke)
	{
		// The whole stroke was shorter than the forward hysteresis (tip started next to the ball): it began at its first sample.
		StartCommittedStroke(Stroke[0].Time, StrokeStartIndex);
	}
	if (!bForwardPhase)
	{
		PauseDuration = kDefaultPause;
		bForwardPhase = true;
	}

	FRbStrokeCommit Commit;
	Commit.Intended = BuildIntended(CrossingTime, Speed, Acceleration, LateralAtContact, LateralVelocity);
	Commit.InputLog.Append(HandSamples.GetData(), CrossingIndex + 1);
	Commit.ContactTime = CrossingTime;
	Commit.AddressIndex = AddressIndex;
	Commit.EyeTransform = GetEyeTransform();
	Commit.Context = Context;
	Commit.Context.Key.AddressIndex = AddressIndex;
	Commit.bTrueTimestamps = !bScriptedStroke && HasTrueTimestamps();
	// What you see is what hits: the pose on screen at contact is SampleHand of the final stroke at t_c.
	HandPose = SampleHandAt(Commit.Intended, Commit.Intended.TimeDown);
	Commit.ContactPose = HandPose;

	// The stroke ended with contact (no abort).
	bCommittedStroke = false;
	bRampShownLatched = false;
	bStrokeActive = false;
	bScriptedStroke = false;
	bForwardPhase = false;
	CueDisplacement = 0.0;
	ComputeCuePoseCore(HandPose, CueBallPosition, CueBallRadius, Context.Tip.DomeRadius, Context.Params.OffsetClamp, 0.0, CuePoseTip, CuePoseDir);
	PushPresentation();

	LastCommit = MoveTemp(Commit);
	UE_LOG(LogRawBreak, Log, TEXT("RbStroke: contact V=%.4f m/s phi=%.4f deg theta=%.3f deg A=%.4f B=%.4f t_c=%.3f s (address %u, %s timestamps)"),
		LastCommit.Intended.Speed, FMath::RadiansToDegrees(LastCommit.Intended.Azimuth), FMath::RadiansToDegrees(LastCommit.Intended.Elevation),
		LastCommit.Intended.AxisOffsetA, LastCommit.Intended.AxisOffsetB, LastCommit.Intended.TimeDown, AddressIndex,
		LastCommit.bTrueTimestamps ? TEXT("true") : TEXT("scripted/reconstructed"));
	SetPhase(ERbStrokePhase::Contact);
	OnStrokeContact.Broadcast(LastCommit);
	if (Phase == ERbStrokePhase::Contact)
	{
		SetPhase(ERbStrokePhase::Watching); // the listener may already have locked the component
	}
}

// ---------------------------------------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------------------------------------

void URbStrokeComponent::ComputeCuePoseCore(const rb::human::HandPose& Pose, const rb::Vec3& BallCenter, double BallRadius, double DomeRadius,
	double OffsetClamp, double CueDisplacementX, rb::Vec3& OutTipDomeCenter, rb::Vec3& OutDirection)
{
	// The executed pose of ExecuteStroke (human-factors 3.6): axis offsets -> contact offsets with the current dome radius,
	// rho clamp, dome centre on the ball at the contact point.
	const rb::CueFrame Frame = rb::MakeCueFrame(Pose.Elevation, Pose.Azimuth);
	const rb::Vec2 Contact = rb::AimToContactOffset(rb::Vec2(Pose.AxisOffsetA, Pose.AxisOffsetB), BallRadius, DomeRadius);
	double A = Contact.x;
	double B = Contact.y;
	const double Rho = FMath::Sqrt(A * A + B * B);
	if (Rho > OffsetClamp && Rho > 0.0)
	{
		A = A * OffsetClamp / Rho;
		B = B * OffsetClamp / Rho;
	}
	const rb::Vec3 ContactDir = rb::CueContactPoint(Frame, A, B, 1.0);
	const rb::Vec3 DomeAtContact = BallCenter + ContactDir * (BallRadius + DomeRadius);
	OutTipDomeCenter = DomeAtContact + Frame.Axis * CueDisplacementX;
	OutDirection = Frame.Axis;
}

void URbStrokeComponent::UpdatePresentation()
{
	if (Phase != ERbStrokePhase::GettingDown && Phase != ERbStrokePhase::Down)
	{
		return;
	}
	if (bFloorDirty)
	{
		UpdateElevationFloor();
	}
	// Time since down (0 during the get-down transition: the pose stands still until the processes start).
	const double T = FMath::Max(0.0, LastTickTime - DownSince);
	double Speed = 0.0;
	double LateralNow = 0.0;
	if (bStrokeActive && HandSamples.Num() > StrokeStartIndex)
	{
		const FRbStrokeSample& Latest = HandSamples.Last();
		LateralNow = Latest.Lateral;
		if (bForwardPhase)
		{
			// Every frame: only the samples of the window (binary search, the stroke is time ordered), not the whole stroke.
			const FRbStrokeSample* Stroke = HandSamples.GetData() + StrokeStartIndex;
			const int32 Count = HandSamples.Num() - StrokeStartIndex;
			const double WindowStart = Latest.Time - FitWindowSeconds;
			const int32 First = RbStrokeMath::FirstSampleInWindow(Stroke, Count, WindowStart);
			double V = 0.0;
			double Acc = 0.0;
			if (RbStrokeMath::QuadraticFit(Stroke + First, Count - First, Latest.Time, WindowStart, Latest.Time, V, Acc))
			{
				Speed = FMath::Clamp(RbStrokeMath::CueSpeedFromHandSpeed(V, Gain), 0.0, Gain.VTipMax);
			}
		}
	}
	const rb::human::IntendedStroke Provisional = BuildIntended(DownSince + T, Speed, 0.0, LateralNow, 0.0);
	HandPose = SampleHandAt(Provisional, T);
	if (bCommittedStroke && HandPose.RampShown)
	{
		bRampShownLatched = true; // this frame shows part of the per-shot draws: an abort now spends them (HF-B13)
	}
	ComputeCuePoseCore(HandPose, CueBallPosition, CueBallRadius, Context.Tip.DomeRadius, Context.Params.OffsetClamp, CueDisplacement,
		CuePoseTip, CuePoseDir);
	PushPresentation();
}

void URbStrokeComponent::PushPresentation()
{
	if (ARbCue* CueActor = Cue.Get())
	{
		if (CueActor->GetDrive() != ERbCueDrive::Input)
		{
			CueActor->SetDrive(ERbCueDrive::Input);
		}
		CueActor->SetPoseCore(CuePoseTip, CuePoseDir);
	}
	URbCameraRigComponent* Rig = FindRig();
	if (!Rig)
	{
		return;
	}
	// Contact point on the ball surface of the rendered pose (the dome touches the ball there at x_c = 0).
	const double R = CueBallRadius;
	const double Dome = Context.Tip.DomeRadius;
	const rb::Vec3 DomeAtContact = CuePoseTip - CuePoseDir * CueDisplacement;
	const rb::Vec3 ContactPoint = CueBallPosition + (DomeAtContact - CueBallPosition) * (R / (R + Dome));
	Rig->SetCueAxisWorld(CoreToWorld(ContactPoint), CoreDirectionToWorld(CuePoseDir));

	// Focus: the first ball on the aim line (plan corridor), else the cue ball.
	rb::Vec3 Focus = CueBallPosition;
	double Best = TNumericLimits<double>::Max();
	const double Cx = FMath::Cos(Aim.Azimuth);
	const double Cy = FMath::Sin(Aim.Azimuth);
	for (const rb::human::BallObstacle& Ball : Context.OtherBalls)
	{
		const double Wx = Ball.Position.x - CueBallPosition.x;
		const double Wy = Ball.Position.y - CueBallPosition.y;
		const double Along = Wx * Cx + Wy * Cy;
		const double Perp = FMath::Abs(Cx * Wy - Cy * Wx);
		if (Along > 0.0 && Perp < R + Ball.Radius && Along < Best)
		{
			Best = Along;
			Focus = Ball.Position;
		}
	}
	Rig->SetFocusTargetWorld(CoreToWorld(Focus));
}

// ---------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------

void URbStrokeComponent::UpdateElevationFloor()
{
	bFloorDirty = false;
	double Floor = 0.0;
	rb::human::FloorSource FloorBy = rb::human::FloorSource::None;
	rb::BallId FloorBall = rb::kNoBall;
	ARbTable* TableActor = Table.Get();
	if (TableActor && TableActor->HasContext())
	{
		rb::Vec3 Positions[rb::kMaxBalls];
		bool InPlay[rb::kMaxBalls] = {};
		Positions[0] = CueBallPosition;
		InPlay[0] = true;
		for (const rb::human::BallObstacle& Ball : Context.OtherBalls)
		{
			if (Ball.Id > 0 && Ball.Id < rb::kMaxBalls)
			{
				Positions[Ball.Id] = Ball.Position;
				InPlay[Ball.Id] = true;
			}
		}
		const double R = CueBallRadius;
		const rb::CueFrame Frame = rb::MakeCueFrame(Aim.Elevation, Aim.Azimuth);
		rb::Vec2 Offset = rb::AimToContactOffset(rb::Vec2(Aim.AxisOffsetA, Aim.AxisOffsetB), R, Context.Tip.DomeRadius);
		const double Rho = FMath::Sqrt(Offset.x * Offset.x + Offset.y * Offset.y);
		if (Rho > Context.Params.OffsetClamp && Rho > 0.0)
		{
			Offset = rb::Vec2(Offset.x * Context.Params.OffsetClamp / Rho, Offset.y * Context.Params.OffsetClamp / Rho);
		}
		FRbCueClearanceInput Input;
		Input.ContactPoint = CueBallPosition + rb::CueContactPoint(Frame, Offset.x, Offset.y, R);
		Input.Azimuth = Aim.Azimuth;
		Input.Elevation = 0.0; // the absolute floor: lowest clear elevation (the requested one may lie above it)
		// The contact offsets above were computed in the cue frame of the aim elevation: the search from 0 must keep them in that
		// frame, or a raised centre hit reads as a top hit and the floor drops below an obstacle ball (UE-4 review, integration).
		Input.ContactElevation = Aim.Elevation;
		Input.TipDomeRadius = Context.Tip.DomeRadius;
		Input.TipWidth = Context.Tip.Width;
		Input.CueLength = Context.Cue.Length;
		Input.Backswing = MaxBackswing;
		Input.MaxElevation = MaxElevation;
		Input.CueBall = 0;
		Input.BallPositions = Positions;
		Input.InPlay = InPlay;
		Input.BallCount = rb::kMaxBalls;
		// Balls and rails analytically, then raised until the environment sweep (walls, lamp, furniture) is clear too (bisection
		// to 0.01 deg on the combined test).
		const UWorld* World = GetWorld();
		const FRbCueClearanceResult Result = World
			? RbCueClearance::ComputeMinElevationWithEnvironment(World, *TableActor, Context.CueBody, Input)
			: RbCueClearance::ComputeMinElevation(TableActor->GetContext(), Context.CueBody, Input);
		Floor = FMath::Clamp(Result.MinElevation, 0.0, MaxElevation);
		FloorBy = Result.FloorBy;
		FloorBall = static_cast<rb::BallId>(Result.FloorBall);
	}
	Aim.ElevationFloor = Floor;
	Context.Situation.ElevationFloor = Floor;
	Context.Situation.FloorBy = FloorBy;
	Context.Situation.FloorBall = FloorBall;
}

void URbStrokeComponent::UpdatePlacementPoint()
{
	const AActor* Owner = GetOwner();
	const UCameraComponent* Camera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!Camera)
	{
		return;
	}
	// The view ray hits the bed plane (z = 0 of the table frame); the ball centre sits R above it. The ray of the rig's base eye
	// (the posture without breathing / sway), so the target does not breathe; the hand adds its own human lag and tremor.
	FVector Origin = Camera->GetComponentLocation();
	FVector Dir = Camera->GetForwardVector();
	if (const URbCameraRigComponent* Rig = FindRig())
	{
		if (Rig->GetMode() == ERbCameraRigMode::BallInHand || Rig->GetMode() == ERbCameraRigMode::Standing)
		{
			const FTransform Base = Rig->GetBaseEyeTransform();
			Origin = Base.GetLocation();
			Dir = Base.GetRotation().GetForwardVector();
		}
	}
	const ARbTable* TableActor = Table.Get();
	const double PlaneZ = TableActor && TableActor->GetClothOrigin() ? TableActor->GetClothOrigin()->GetComponentLocation().Z : 0.0;
	if (Dir.Z > -1.0e-3)
	{
		return; // looking level or up: keep the previous point
	}
	const double Distance = (PlaneZ - Origin.Z) / Dir.Z;
	if (Distance <= 0.0 || Distance > 2000.0)
	{
		return;
	}
	FVector Point = Origin + Dir * Distance + FVector(0.0, 0.0, CueBallRadius * FRbCoords::CmPerMeter);
	if (URbBallInHandComponent* Hand = FindBallInHand())
	{
		if (Hand->GetState() == ERbBallInHandState::Carrying || Hand->GetState() == ERbBallInHandState::Refused)
		{
			const rb::Vec3 Core = TableActor ? TableActor->WorldToCore(Point) : FRbCoords::PositionToCore(Point);
			Hand->SetTargetCore(rb::Vec2(Core.x, Core.y));
			const rb::Vec2 Target = Hand->GetTargetCore(); // clamped to the reachable bed, incl. the fine adjustment
			Point = CoreToWorld(rb::Vec3(Target.x, Target.y, Hand->GetBallRadius()));
		}
		else if (Hand->GetState() == ERbBallInHandState::Lowering || Hand->GetState() == ERbBallInHandState::Placed)
		{
			return; // the ball is on its way down / down: the target stays where it was confirmed
		}
	}
	if (!bHasPlacement || !Point.Equals(PlacementWorld, 0.01))
	{
		PlacementWorld = Point;
		bHasPlacement = true;
		OnPlacementPointChanged.Broadcast(PlacementWorld);
	}
}

bool URbStrokeComponent::IsCueBallInReach() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return true; // tests / tools without a pawn
	}
	const FVector Ball = CoreToWorld(CueBallPosition);
	return FVector::Dist2D(Owner->GetActorLocation(), Ball) <= MaxReach * FRbCoords::CmPerMeter;
}

void URbStrokeComponent::InitAimFromView()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return;
	}
	FVector Dir = Pawn->GetViewRotation().Vector();
	Dir.Z = 0.0;
	if (!Dir.Normalize())
	{
		return;
	}
	const ARbTable* TableActor = Table.Get();
	Aim.Azimuth = TableActor ? TableActor->WorldDirectionToAzimuth(Dir) : FRbCoords::AzimuthFromUEDirection(Dir);
}

FTransform URbStrokeComponent::GetEyeTransform() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (const UCameraComponent* Camera = Owner->FindComponentByClass<UCameraComponent>())
		{
			return Camera->GetComponentTransform();
		}
		return Owner->GetActorTransform();
	}
	return FTransform::Identity;
}

URbCameraRigComponent* URbStrokeComponent::FindRig() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<URbCameraRigComponent>() : nullptr;
}

FVector URbStrokeComponent::CoreToWorld(const rb::Vec3& P) const
{
	const ARbTable* TableActor = Table.Get();
	return TableActor ? TableActor->CoreToWorld(P) : FRbCoords::PositionToUE(P);
}

FVector URbStrokeComponent::CoreDirectionToWorld(const rb::Vec3& D) const
{
	const ARbTable* TableActor = Table.Get();
	return TableActor ? TableActor->CoreDirectionToWorld(D) : FRbCoords::DirectionToUE(D);
}

// ---------------------------------------------------------------------------------------------------------
// Dev tool
// ---------------------------------------------------------------------------------------------------------

TArray<FTransform> URbStrokeComponent::DevRecordStrokePoses(float TipSpeed, float FrameRate, float NoiseScale, float HoldSeconds, bool bCommit,
	int32 Seed, TArray<float>& OutTimes)
{
	TArray<FTransform> Poses;
	OutTimes.Reset();
	URbStrokeComponent* Stroke = NewObject<URbStrokeComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	double Clock = 1000.0;
	Stroke->ClockOverride = [&Clock]() { return Clock; };

	FRbStrokeContext Context;
	Context.Attributes = rb::human::UniformAttributes(25.0);
	Context.Situation.Pressure = 0.3;
	Context.Key.MatchSeed = static_cast<uint64>(FMath::Max(Seed, 0)) + 1;
	Context.Key.ShooterId = 1;
	Context.Params.NoiseScale = FMath::Max(0.0f, NoiseScale);
	const double R = Context.CueBall.Radius;
	Stroke->SetStrokeContext(Context);
	Stroke->BeginAddress(rb::Vec3(0.0, 0.0, R), R);
	Stroke->RequestGetDownToggle();
	const double Start = Clock;
	Clock = Stroke->GetDownSince();
	Stroke->TickStroke(Clock);
	Stroke->SetCommitHeld(bCommit);
	Stroke->InjectStrokeSamples(Stroke->MakeScriptedStroke(TipSpeed, Clock + FMath::Max(0.0f, HoldSeconds)));

	const double Dt = 1.0 / FMath::Max(1.0f, FrameRate);
	for (int32 Frame = 0; Frame < 20000; ++Frame)
	{
		rb::Vec3 Tip;
		rb::Vec3 Dir;
		Stroke->GetCuePoseCore(Tip, Dir);
		Poses.Add(FTransform(FRotationMatrix::MakeFromX(FRbCoords::DirectionToUE(Dir)).ToQuat(), FRbCoords::PositionToUE(Tip)));
		OutTimes.Add(static_cast<float>(Clock - Start));
		if (Stroke->GetPhase() == ERbStrokePhase::Watching || (Stroke->PendingScripted.Num() == 0 && Clock > Stroke->GetDownSince() + 0.3))
		{
			break;
		}
		Clock += Dt;
		Stroke->TickStroke(Clock);
	}
	Stroke->ClockOverride = nullptr;
	Stroke->MarkAsGarbage();
	return Poses;
}
