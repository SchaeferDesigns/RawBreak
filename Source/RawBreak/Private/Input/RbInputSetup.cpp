#include "Input/RbInputSetup.h"

#include "InputAction.h"
#include "InputMappingContext.h"

// Owner: UE-5a. TODO(UE-5a): value types, triggers (pressed / held), modifiers (negate / swizzle for WASD and arrows),
// the key mapping of the header, tests (every action mapped, no key bound twice in one context).

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, FName(Name), RF_Transient);
		Action->ValueType = Type;
		return Action;
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
	// TODO(UE-5a): Context->MapKey(...) for every binding of the header.
	return Setup;
}
