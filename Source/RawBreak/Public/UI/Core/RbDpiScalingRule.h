#pragma once

// UI layout scale = viewport height / 1080 (ui-ux 3.2 FRbUiScale, contract C2). Registered in Config/DefaultEngine.ini
// ([/Script/Engine.UserInterfaceSettings] UIScaleRule=Custom, CustomScalingRuleClass=/Script/RawBreak.RbDpiScalingRule), so every
// screen-space Slate widget (menus, overlay) is authored at 1080p and scaled by the engine's SDPIScaler only (never twice).
// Owner: M2-D (implemented by the M2 architect step).

#include "CoreMinimal.h"
#include "Engine/DPICustomScalingRule.h"

#include "RbDpiScalingRule.generated.h"

UCLASS()
class RAWBREAK_API URbDpiScalingRule : public UDPICustomScalingRule
{
	GENERATED_BODY()

public:
	virtual float GetDPIScaleBasedOnSize(FIntPoint Size) const override;

	// The rule as a pure function (tests): Height / 1080, 1 for a degenerate size.
	static float ScaleForViewport(FIntPoint Size);
};
