#include "Modes/RbCallShotComponent.h"

#include "Input/RbInputSetup.h"

#include "EnhancedInputComponent.h"

// Owner: M3-G (Docs/ue-architecture.md 19.6). Plan-step stub: no bindings, no selection; the controller already creates the component
// and calls BindInput, so M3-G only fills this file.

URbCallShotComponent::URbCallShotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // TODO(M3-G): tick while C is held (gaze selection)
}

void URbCallShotComponent::BindInput(UEnhancedInputComponent* Input, const URbInputSetup* Setup)
{
	(void)Input;
	(void)Setup;
	// TODO(M3-G): bind Setup->Call (Triggered / Completed), Setup->Declare, Setup->CycleOption.
}

void URbCallShotComponent::HandleCall(bool bHeld)
{
	(void)bHeld;
	// TODO(M3-G): gaze selection (3 deg ball cone, 6 deg pocket cone), SetCalledShot on release over a pocket, the body's point target.
}

void URbCallShotComponent::HandleDeclare()
{
	// TODO(M3-G): toggle push-out / safety per GetShotConstraints (URbMatchDirector::SetShotKind).
}

void URbCallShotComponent::HandleCycle(float Direction)
{
	(void)Direction;
	// TODO(M3-G): down: cycle the aimed ball's pocket; in hand with MayRequestSpot: Q = URbMatchDirector::RequestSpot.
}
