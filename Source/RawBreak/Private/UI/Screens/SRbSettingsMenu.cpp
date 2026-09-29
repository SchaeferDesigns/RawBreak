#include "UI/Screens/SRbSettingsMenu.h"

#include "Widgets/Text/STextBlock.h"

// Owner: M2-D.

void SRbSettingsMenu::Construct(const FArguments& InArgs)
{
	// TODO(M2-D): layout, rows, focus, navigation (ui-ux 3.3, 12, 13).
	ChildSlot
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("Settings")))
	];
}
