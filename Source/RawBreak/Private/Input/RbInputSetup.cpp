#include "Input/RbInputSetup.h"

#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

// Owner: UE-5a. Runtime Enhanced Input (Docs/ue-architecture.md 6.5): value types, triggers (pressed / held / pulse),
// modifiers (swizzle / negate for WASD and the arrows), the key mapping of the header. Tests: RawBreak.Unit.Input.*
// (every action mapped, no key bound twice in the context).
//
// Trigger conventions for the pawn / controller bindings (UE-5b):
//   held actions (Stroke, Commit, FineAim, Settle, Glance, Call): no trigger = implicit "Down": Triggered every frame while held,
//     Completed on release -> bind Triggered (value true) and Completed (value false);
//   pressed actions (GetDown, ToggleOverlay, ToggleDebug, Replay, Confirm, CycleOption, Declare, Chalk): UInputTriggerPressed ->
//     bind Triggered (fires once per press);
//   TipOffset: UInputTriggerPulse (a step on press, then repeated every 0.12 s while held) -> bind Triggered, one step each;
//   axes (Move, Look, Elevation): no trigger -> bind Triggered (Look = raw Mouse2D counts, + Y = mouse moved forward).

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, FName(Name), RF_Transient);
		Action->ValueType = Type;
		return Action;
	}

	UInputModifierNegate* MakeNegate(UObject* Outer)
	{
		return NewObject<UInputModifierNegate>(Outer, NAME_None, RF_Transient);
	}

	UInputModifierSwizzleAxis* MakeSwizzleYXZ(UObject* Outer)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer, NAME_None, RF_Transient);
		Swizzle->Order = EInputAxisSwizzle::YXZ; // a key's X value goes to Y (forward / up)
		return Swizzle;
	}

	void MapPressed(UInputMappingContext* Context, UInputAction* Action, const FKey& Key, UObject* Outer, bool bNegate = false)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Triggers.Add(NewObject<UInputTriggerPressed>(Outer, NAME_None, RF_Transient));
		if (bNegate)
		{
			Mapping.Modifiers.Add(MakeNegate(Outer));
		}
	}

	// A 2D direction key: bForwardAxis -> value on Y (swizzle), bNegative -> negated.
	FEnhancedActionKeyMapping& MapDirection(UInputMappingContext* Context, UInputAction* Action, const FKey& Key, UObject* Outer, bool bForwardAxis,
		bool bNegative)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bForwardAxis)
		{
			Mapping.Modifiers.Add(MakeSwizzleYXZ(Outer));
		}
		if (bNegative)
		{
			Mapping.Modifiers.Add(MakeNegate(Outer));
		}
		return Mapping;
	}
}

URbInputSetup* URbInputSetup::CreateDefault(UObject* Outer)
{
	URbInputSetup* Setup = NewObject<URbInputSetup>(Outer, NAME_None, RF_Transient);
	Setup->Context = NewObject<UInputMappingContext>(Setup, TEXT("IMC_RbDefault"), RF_Transient);
	Setup->Move = MakeAction(Setup, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	Setup->Look = MakeAction(Setup, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	Setup->GetDown = MakeAction(Setup, TEXT("IA_GetDown"), EInputActionValueType::Boolean);
	Setup->Stroke = MakeAction(Setup, TEXT("IA_Stroke"), EInputActionValueType::Boolean);
	Setup->Commit = MakeAction(Setup, TEXT("IA_Commit"), EInputActionValueType::Boolean);
	Setup->Elevation = MakeAction(Setup, TEXT("IA_Elevation"), EInputActionValueType::Axis1D);
	Setup->TipOffset = MakeAction(Setup, TEXT("IA_TipOffset"), EInputActionValueType::Axis2D);
	Setup->FineAim = MakeAction(Setup, TEXT("IA_FineAim"), EInputActionValueType::Boolean);
	Setup->Settle = MakeAction(Setup, TEXT("IA_Settle"), EInputActionValueType::Boolean);
	Setup->Glance = MakeAction(Setup, TEXT("IA_Glance"), EInputActionValueType::Boolean);
	Setup->ToggleOverlay = MakeAction(Setup, TEXT("IA_ToggleOverlay"), EInputActionValueType::Boolean);
	Setup->ToggleDebug = MakeAction(Setup, TEXT("IA_ToggleDebug"), EInputActionValueType::Boolean);
	Setup->Replay = MakeAction(Setup, TEXT("IA_Replay"), EInputActionValueType::Boolean);
	Setup->Confirm = MakeAction(Setup, TEXT("IA_Confirm"), EInputActionValueType::Boolean);
	Setup->CycleOption = MakeAction(Setup, TEXT("IA_CycleOption"), EInputActionValueType::Axis1D);
	Setup->Pause = MakeAction(Setup, TEXT("IA_Pause"), EInputActionValueType::Boolean);
	Setup->Pause->bTriggerWhenPaused = true; // M2: the pause menu closes with the same key
	Setup->Call = MakeAction(Setup, TEXT("IA_Call"), EInputActionValueType::Boolean);       // M3 (19.3)
	Setup->Declare = MakeAction(Setup, TEXT("IA_Declare"), EInputActionValueType::Boolean); // M3
	Setup->Chalk = MakeAction(Setup, TEXT("IA_Chalk"), EInputActionValueType::Boolean);     // M3

	UInputMappingContext* C = Setup->Context;

	// Move: W / S forward / back (Y), D / A right / left (X).
	MapDirection(C, Setup->Move, EKeys::W, Setup, true, false);
	MapDirection(C, Setup->Move, EKeys::S, Setup, true, true);
	MapDirection(C, Setup->Move, EKeys::D, Setup, false, false);
	MapDirection(C, Setup->Move, EKeys::A, Setup, false, true);

	// Look: raw 2D mouse delta (counts per frame). Aim while down, placement point while in hand (routing: pawn / stroke).
	C->MapKey(Setup->Look, EKeys::Mouse2D);

	// Get down / stand up (toggle), stroke mode (held), commit (held).
	MapPressed(C, Setup->GetDown, EKeys::RightMouseButton, Setup);
	C->MapKey(Setup->Stroke, EKeys::LeftMouseButton);
	C->MapKey(Setup->Commit, EKeys::SpaceBar);

	// Elevation: mouse wheel (+1 per notch away from the user = butt up).
	C->MapKey(Setup->Elevation, EKeys::MouseWheelAxis);

	// Tip offset: arrows, one step per press, repeated while held.
	auto MapTip = [&](const FKey& Key, bool bUp, bool bNegative)
	{
		FEnhancedActionKeyMapping& Mapping = MapDirection(C, Setup->TipOffset, Key, Setup, bUp, bNegative);
		UInputTriggerPulse* Pulse = NewObject<UInputTriggerPulse>(Setup, NAME_None, RF_Transient);
		Pulse->bTriggerOnStart = true;
		Pulse->Interval = 0.12f;
		Mapping.Triggers.Add(Pulse);
	};
	MapTip(EKeys::Up, true, false);
	MapTip(EKeys::Down, true, true);
	MapTip(EKeys::Right, false, false);
	MapTip(EKeys::Left, false, true);

	// Held modifiers.
	C->MapKey(Setup->FineAim, EKeys::LeftShift);
	C->MapKey(Setup->Settle, EKeys::LeftControl);
	C->MapKey(Setup->Glance, EKeys::Tab);

	// Pressed actions.
	MapPressed(C, Setup->ToggleOverlay, EKeys::F1, Setup);
	MapPressed(C, Setup->ToggleDebug, EKeys::F2, Setup);
	MapPressed(C, Setup->Replay, EKeys::R, Setup);
	MapPressed(C, Setup->Confirm, EKeys::Enter, Setup);
	MapPressed(C, Setup->Confirm, EKeys::F, Setup);
	MapPressed(C, Setup->CycleOption, EKeys::Q, Setup, true); // -1
	MapPressed(C, Setup->CycleOption, EKeys::E, Setup);       // +1
	MapPressed(C, Setup->Pause, EKeys::Escape, Setup);         // M2: replay back / pause menu

	// M3 (Docs/ue-architecture.md 19.3; ui-ux 9.4 / 9.5 / 9.12): the shot call (held), the push-out / safety declaration, chalking.
	C->MapKey(Setup->Call, EKeys::C);
	MapPressed(C, Setup->Declare, EKeys::X, Setup);
	MapPressed(C, Setup->Chalk, EKeys::G, Setup);
	return Setup;
}

TArray<UInputAction*> URbInputSetup::GetAllActions() const
{
	return {Move, Look, GetDown, Stroke, Commit, Elevation, TipOffset, FineAim, Settle, Glance, ToggleOverlay, ToggleDebug, Replay, Confirm, CycleOption, Pause,
		Call, Declare, Chalk};
}
