#pragma once

// Settings (ui-ux 13, M2 rows of Docs/ue-architecture.md 18.4): pages Graphics (quality preset Low..Cinematic + the individual quality rows), Display (window mode, resolution with the 15 s revert, frame cap, v-sync), Camera (look Eyes / Headcam, FOV, comfort), Controls (mouse DPI, aim / look / stroke sensitivity, fine-aim factor, acceleration, invert, key hints), Audio (volumes). Rows come from URbSettingsRegistry.
// Owner: M2-D (stub by the M2 architect step; TODO(M2-D)).

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SRbSettingsMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbSettingsMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
