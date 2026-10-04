#pragma once

// The diegetic shot call, declarations and the spot request (Docs/ue-architecture.md 19.6; ui-ux 9.4, 9.5, 9.6 item 4). On the
// player controller (created by ARbPlayerController since the M3 plan step, which calls BindInput from SetupInputComponent):
//   Call (C held)      standing: the ball nearest the view ray inside a 3 deg cone is selected, then a pocket inside a 6 deg cone;
//                      releasing C on a pocket calls URbMatchDirector::SetCalledShot(ball, pocket), elsewhere cancels; while held the
//                      player's body points the cue tip at the selection (URbBodyRigComponent::SetPointTarget, M3-H). 9-ball: "No
//                      calls in 9-ball" once.
//   Declare (X)        standing: toggles the one declaration the rules allow now (push-out / safety, GetShotConstraints).
//   CycleOption (Q/E)  down: cycles the pocket of the ball the cue aims at (calls required); in hand with MayRequestSpot: Q asks for
//                      the spot (URbMatchDirector::RequestSpot). The controller's own CycleOption binding (decision options) stays.
// Owner: M3-G. Plan-step stub: binds nothing, selects nothing (TODO(M3-G)). Tests: RawBreak.Unit.Modes.*, RawBreak.Functional.Modes.*.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "RbCallShotComponent.generated.h"

class UEnhancedInputComponent;
class URbInputSetup;

UENUM(BlueprintType)
enum class ERbCallState : uint8
{
	Idle,
	SelectingBall,   // C held, no ball under the gaze yet
	SelectingPocket, // C held, a ball selected
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnShotCalled, int32 /*Ball*/, int32 /*Pocket*/);

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbCallShotComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbCallShotComponent();

	// Binds Call (Triggered / Completed), Declare and CycleOption of Setup on the controller's input component.
	void BindInput(UEnhancedInputComponent* Input, const URbInputSetup* Setup);

	// Input routing (the bindings call these; tests call them directly).
	void HandleCall(bool bHeld);
	void HandleDeclare();
	void HandleCycle(float Direction);

	ERbCallState GetState() const { return State; }
	// The current selection (-1 = none): rules ball id / rb::PocketId index.
	int32 GetSelectedBall() const { return SelectedBall; }
	int32 GetSelectedPocket() const { return SelectedPocket; }

	// A call was made (after the director accepted it).
	FRbOnShotCalled OnShotCalled;

protected:
	ERbCallState State = ERbCallState::Idle;
	int32 SelectedBall = -1;
	int32 SelectedPocket = -1;
};
