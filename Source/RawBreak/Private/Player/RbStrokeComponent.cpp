#include "Player/RbStrokeComponent.h"

#include "Cue/RbCue.h"
#include "Input/RbRawMouseInput.h"
#include "Table/RbTable.h"

#include "HAL/PlatformTime.h"

// Owner: UE-5a. TODO(UE-5a): phase machine, aim / elevation (clearance floor via RbCueClearance) / tip offsets, raw
// sample integration with the gain curve, practice stop-short, contact detection + quadratic-fit speed, IntendedStroke
// (TimeDown, ForwardStart, SettleStart, PauseDuration, ContactAcceleration, HeadMovedBeforeContact), SampleHand-driven
// cue pose while down + OnStrokeAborted(RampShown) (review R-04), scripted samples, ball-in-hand placement; tests
// RawBreak.Unit.Stroke.* (scripted stroke of known speed -> Intended.Speed within 1e-3 m/s, independent of frame rate).

URbStrokeComponent::URbStrokeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URbStrokeComponent::SetTable(ARbTable* InTable) { Table = InTable; }
void URbStrokeComponent::SetCue(ARbCue* InCue) { Cue = InCue; }

void URbStrokeComponent::BeginAddress(const rb::Vec3& InCueBallPosition, double InCueBallRadius)
{
	CueBallPosition = InCueBallPosition;
	CueBallRadius = InCueBallRadius;
	AddressIndex = 0;
	SetPhase(ERbStrokePhase::Walking);
}

void URbStrokeComponent::SetLocked(bool bLocked)
{
	if (bLocked)
	{
		SetPhase(ERbStrokePhase::Locked);
	}
	else if (Phase == ERbStrokePhase::Locked)
	{
		SetPhase(ERbStrokePhase::Walking);
	}
}

void URbStrokeComponent::SetStrokeContext(const FRbStrokeContext& InContext) { Context = InContext; }
void URbStrokeComponent::SetSettleHeld(bool /*bHeld*/) { /* TODO(UE-5a): SettleSince while down */ }

void URbStrokeComponent::BeginCueBallPlacement() { SetPhase(ERbStrokePhase::PlacingCueBall); }
void URbStrokeComponent::RequestGetDownToggle() { /* TODO(UE-5a) */ }
void URbStrokeComponent::AddAimInput(const FVector2D& /*LookDelta*/, bool /*bFine*/) { /* TODO(UE-5a) */ }
void URbStrokeComponent::AddElevationInput(float /*Steps*/) { /* TODO(UE-5a) */ }
void URbStrokeComponent::AddTipOffsetInput(const FVector2D& /*Delta*/) { /* TODO(UE-5a) */ }
void URbStrokeComponent::SetStrokeHeld(bool bHeld) { bStrokeHeld = bHeld; }
void URbStrokeComponent::SetCommitHeld(bool bHeld) { bCommitHeld = bHeld; }
void URbStrokeComponent::ConfirmPressed() { /* TODO(UE-5a) */ }

void URbStrokeComponent::InjectStrokeSamples(const TArray<FRbStrokeSample>& Samples)
{
	PendingScripted.Append(Samples);
}

void URbStrokeComponent::BeginPlay()
{
	Super::BeginPlay();
	RawMouse = FRbRawMouseInput::Create();
}

void URbStrokeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ProcessStrokeSamples(FPlatformTime::Seconds());
	UpdatePresentation();
}

void URbStrokeComponent::SetPhase(ERbStrokePhase NewPhase)
{
	Phase = NewPhase;
}

void URbStrokeComponent::ProcessStrokeSamples(double /*Now*/)
{
	// TODO(UE-5a)
}

void URbStrokeComponent::UpdatePresentation()
{
	// TODO(UE-5a): ARbCue::SetPoseCore from the address + CueDisplacement (+ SampleHand pose via the director later).
}
