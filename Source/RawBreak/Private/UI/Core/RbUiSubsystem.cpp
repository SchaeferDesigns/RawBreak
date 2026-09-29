#include "UI/Core/RbUiSubsystem.h"

#include "RawBreak.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

// Owner: M2-D. Stub of the M2 architect step: the API compiles and does nothing yet (Esc outside a replay is a no-op, as in M1).

URbUiSubsystem* URbUiSubsystem::Get(const APlayerController* Controller)
{
	const ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
	return Player ? Player->GetSubsystem<URbUiSubsystem>() : nullptr;
}

void URbUiSubsystem::TogglePauseMenu()
{
	// TODO(M2-D): push SRbPauseMenu (pause the game, UIOnly input, cursor) / pop it (resume, GameOnly, captured mouse).
	UE_LOG(LogRawBreak, Verbose, TEXT("URbUiSubsystem::TogglePauseMenu: not implemented yet (M2-D)"));
}

bool URbUiSubsystem::OpenScreen(ERbUiScreen Screen)
{
	// TODO(M2-D): screen stack (SRbTitleScreen, SRbPauseMenu, SRbSettingsMenu).
	return Screen == ERbUiScreen::None;
}

void URbUiSubsystem::CloseTopScreen()
{
	// TODO(M2-D)
	TopScreen = ERbUiScreen::None;
}

void URbUiSubsystem::CloseAll()
{
	// TODO(M2-D)
	TopScreen = ERbUiScreen::None;
}
