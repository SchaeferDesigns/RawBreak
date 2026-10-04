#pragma once

// Screen stack of the menus (Docs/ue-architecture.md 18.4, ui-ux 3.2-3.3, 12, 13). One per local player. M2 scope: the pause menu
// (Resume, Settings, Quit to title, Quit to desktop), the settings menu (presets Low..Cinematic + the individual rows of 18.4) and
// the minimal title / venue-select screen of L_Title (Test room / Dive bar x Practice / Hot-seat, Settings, Quit). Slate only
// (ui-ux 3.1): every screen is an SRbScreen added to the game viewport for this player (layer 60, dialogs 70; scaled only by the
// engine's DPI rule); no UMG, no widget assets.
//
// Push rules (ui-ux 3.3): a screen that pauses the game (the pause menu) pauses the world through the controller
// (APlayerController::SetPause) and switches the audio's pause mix (URbAudioSubsystem::SetPausedMix, audio.md 7.3) with it; the
// merged URbShotPlaybackComponent holds the live clock while the world is paused and resumes without a time jump (UX-T10). Input mode: menus UIOnly with a visible cursor (pressed keys flushed, so a held Stroke button
// never sticks); closing the last in-game screen restores GameOnly with the captured, hidden mouse the stroke's raw-input thread
// needs, and unpauses. Esc: ARbPlayerController::HandlePause -> TogglePauseMenu while no menu is open; inside a menu the screen
// handles Esc itself (it has the keyboard focus): back one screen, Resume on the pause root, a quit confirm on the title root.
// Travelling to another map drops the stack (world cleanup); ARbTitleGameMode opens the title on L_Title.
//
// Binding (UX-T25): the pause menu's match block, the overlay and the key hints show only the PLAYER's match:
// FindPlayerDirector = URbTableSubsystem::GetPlayerSession()'s director (else ARbGameMode::GetDirector(), the player session's
// accessor while no sessions are registered).
//
// Dev switch (captures, rbue.py capture --extra -RbUiScreen=<Screen>): Pause, Settings.<Page> (Graphics / Display / Camera /
// Controls / Audio), KeyHints (the hints never fade), KeyHints.Down (never fade; the player gets down on the shot: the RMB
// handler), Title (no clean first seconds), Title.Play (the venue list); opened once the world plays and the shader
// compiler has been idle for -RbUiScreenDelay seconds (default 2), and the screenshot then includes the UI.
// Owner: M2-D.

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "Core/RbTypes.h"

#include "RbUiSubsystem.generated.h"

class APlayerController;
class SRbScreen;
class UGameViewportClient;
class URbMatchDirector;
struct FRbUiHost;
enum class ERbSettingsPage : uint8;

UENUM(BlueprintType)
enum class ERbUiScreen : uint8
{
	None,
	Title,    // L_Title: venue select (Test room / Dive bar), Settings, Quit
	Pause,    // in play: Resume, Settings, Quit to title, Quit to desktop
	Settings, // pages Graphics / Display / Camera / Controls / Audio (M2 rows, 18.4)
	Dialog,   // a confirm dialog over another screen (M2-D)
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

	// Pushes a screen (Settings from the pause menu or the title screen). False if the screen cannot open now. Title replaces the
	// whole stack.
	bool OpenScreen(ERbUiScreen Screen);

	// Pops the top screen (Back). Closing the last in-game screen resumes the game.
	void CloseTopScreen();

	// Closes every screen and restores game input.
	void CloseAll();

	bool IsMenuOpen() const { return Stack.Num() > 0; }
	ERbUiScreen GetTopScreen() const;

	// --- M2-D ---------------------------------------------------------------------------------------------------------------
	bool OpenSettings(ERbSettingsPage Page);
	void PushScreen(const TSharedRef<SRbScreen>& Screen);
	TSharedPtr<SRbScreen> GetTopWidget() const;
	TArray<ERbUiScreen> GetStackIds() const;
	bool IsGamePausedByMenu() const { return bPausedByMenu; }
	// The host the screens act through (bound to this subsystem).
	TSharedRef<FRbUiHost> GetHost();

	// Actions (also used by the host).
	bool StartVenue(ERbVenue Venue, ERbMatchMode Mode);
	void QuitToTitle();
	void QuitToDesktop();
	static bool IsVenueAvailable(ERbVenue Venue, FText& OutReason);
	static bool CanQuitToTitle(FText& OutReason);

	// The player's match (UX-T25; see the header comment). nullptr without a match.
	static URbMatchDirector* FindPlayerDirector(const UObject* WorldContext);

	// Dev switch -RbUiScreen= (empty when absent) and its KeyHints mode.
	static const FString& GetDevScreen();
	static bool IsDevHoldKeyHints();

	// Tests: the title screen skips its clean first seconds. (Without it only the first title of a run has them: ui-ux 6.3 runs
	// them on every LAUNCH, so a return from a venue shows the list at once.)
	void SetSkipIntro(bool bSkip) { bSkipIntro = bSkip; }
	bool HasShownTitle() const { return bTitleShown; }

	// Tests / dev tools (UX-T09): the top screen handles a key given by its name ("Enter", "Escape", "Down", "E" ...) exactly like
	// a key press reaching the focused screen. False when no screen is open or the screen ignores the key.
	bool HandleMenuKey(FName KeyName);
	// The widget holding this player's Slate keyboard focus (nullptr without Slate or a local player).
	TSharedPtr<SWidget> GetSlateFocusedWidget() const;

	// USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	struct FEntry
	{
		ERbUiScreen Id = ERbUiScreen::None;
		TSharedPtr<SRbScreen> Widget;
	};
	TArray<FEntry> Stack;

private:
	APlayerController* GetController() const;
	UWorld* GetPlayerWorld() const;
	UGameViewportClient* GetViewport() const;
	void AddToViewport(const TSharedRef<SRbScreen>& Screen, int32 ZOrder);
	void RemoveFromViewport(const TSharedRef<SRbScreen>& Screen);
	void PopInternal();
	void ClearStack(bool bRestoreGame);
	void RestoreGame();
	// The pause menu's audio mix state (URbAudioSubsystem::SetPausedMix, audio.md 7.3) with the menu's world pause.
	void SetAudioPauseMix(bool bPaused) const;
	void ApplyMenuInput();
	void SetUserFocus(const TSharedPtr<SWidget>& Widget);
	bool TickUi(float DeltaTime);
	void TickDevScreen();
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void OnViewportBeginDraw();

	TSharedPtr<FRbUiHost> Host;
	TWeakObjectPtr<UWorld> StackWorld;
	bool bPausedByMenu = false;
	bool bSkipIntro = false;
	bool bTitleShown = false; // a title was shown in this run: the next ones skip the clean first seconds (ui-ux 6.3: per launch)
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle BeginDrawHandle;
	TWeakObjectPtr<UGameViewportClient> DrawHookViewport;
	bool bDevApplied = false;
	double DevIdleSince = -1.0;
};
