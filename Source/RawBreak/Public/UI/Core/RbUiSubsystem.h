#pragma once

// Screen stack of the menus (Docs/ue-architecture.md 18.4, ui-ux 3.2-3.3, 12, 13). One per local player. M2 scope: the pause menu
// (Resume, Settings, Quit), the settings menu (presets Low..Cinematic + the individual rows of 18.4) and the minimal title /
// venue-select screen of L_Title (Test room / Dive bar / Settings / Quit). Slate only (ui-ux 3.1): every screen is an
// SCompoundWidget added to the game viewport; no UMG, no widget assets.
//
// Push rules (ui-ux 3.3): a screen that pauses the game calls SetGamePaused(true) in single-player / hot-seat; the merged
// URbShotPlaybackComponent holds the live clock while the world is paused and resumes without a time jump (UX-T10). Input
// mode: menus UIOnly with a visible cursor; closing the last screen restores GameOnly with the captured, hidden mouse the
// stroke's raw-input thread needs (ARbPlayerController::BeginPlay). Esc: ARbPlayerController::HandlePause -> TogglePauseMenu
// (open / back / resume); the menus also handle Esc themselves while they have keyboard focus.
// Owner: M2-D (stub by the M2 architect step; TODO(M2-D) marks the parts to implement).

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "RbUiSubsystem.generated.h"

class APlayerController;

UENUM(BlueprintType)
enum class ERbUiScreen : uint8
{
	None,
	Title,    // L_Title: venue select (Test room / Dive bar), Settings, Quit
	Pause,    // in play: Resume, Settings, Quit to title, Quit to desktop
	Settings, // pages Graphics / Display / Camera / Controls / Audio (M2 rows, 18.4)
};

UCLASS()
class RAWBREAK_API URbUiSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// The subsystem of a controller's local player (nullptr for remote / no local player).
	static URbUiSubsystem* Get(const APlayerController* Controller);

	// Esc in play: opens the pause menu (pauses the game); in a menu: back one screen; on the pause root: resume.
	void TogglePauseMenu();

	// Pushes a screen (Settings from the pause menu or the title screen). False if the screen cannot open now.
	bool OpenScreen(ERbUiScreen Screen);

	// Pops the top screen (Back). Closing the last in-game screen resumes the game.
	void CloseTopScreen();

	// Closes every screen and restores game input.
	void CloseAll();

	bool IsMenuOpen() const { return TopScreen != ERbUiScreen::None; }
	ERbUiScreen GetTopScreen() const { return TopScreen; }

protected:
	ERbUiScreen TopScreen = ERbUiScreen::None; // TODO(M2-D): the real stack of SRbScreen widgets
};
