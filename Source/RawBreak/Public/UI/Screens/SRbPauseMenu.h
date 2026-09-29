#pragma once

// Pause menu (ui-ux 12): Resume, Settings, Quit to title, Quit to desktop (confirm). Over the live scene with a scrim; the world is paused.
// Owner: M2-D (stub by the M2 architect step; TODO(M2-D)).

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SRbPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbPauseMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
