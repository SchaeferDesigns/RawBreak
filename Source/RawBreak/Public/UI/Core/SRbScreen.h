#pragma once

// Base of every RAW BREAK menu screen (ui-ux 3.2 SRbScreen) and the host interface the screens act through (FRbUiHost). The
// screens never touch the world, the player controller or the settings singleton directly: every action goes through the host,
// which URbUiSubsystem implements for the game and the tests implement with recording doubles (UX-T09 without a viewport).
//
// Focus model (ui-ux 3.3, UX-T09): each screen owns an ordered list of focusable items (menu items, settings rows, dialog
// buttons) and exactly one focused index. Up / Down move it, Enter / Space activate, Left / Right change a row, Esc = Back
// (HandleBack), moving the mouse onto an item focuses it (a still cursor that a scroll or a new screen puts over an item does
// not: RbUi::IsPointerMotion), a click activates. The keys are handled by the screen root (they bubble up from the
// focused item), so Slate's own geometric navigation never runs; Tab is swallowed (it is Glance in play, UX-T26); an auto-repeated
// key only moves the focus or changes a row (IsRepeatableKey). The host
// mirrors the focused item into Slate's user focus when the screen lives in a window. Owner: M2-D.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Core/RbTypes.h"
#include "UI/Core/RbUiSubsystem.h"

class SRbScreen;
class URbGameUserSettings;
class URbMatchDirector;

// What a screen may ask of its surroundings (bound by URbUiSubsystem; test doubles record the calls).
struct FRbUiHost
{
	TFunction<void(ERbUiScreen)> OpenScreen;          // push a known screen (Settings from pause / title)
	TFunction<void(TSharedRef<SRbScreen>)> PushScreen; // push a widget (confirm dialogs)
	TFunction<void()> CloseTop;                        // pop the top screen (Back)
	TFunction<void()> Resume;                          // close every in-game screen, unpause
	TFunction<void()> QuitToTitle;
	TFunction<void()> QuitToDesktop;
	TFunction<bool(ERbVenue, ERbMatchMode)> StartVenue;
	TFunction<bool(ERbVenue, FText& /*OutReason*/)> IsVenueAvailable;
	TFunction<bool(FText& /*OutReason*/)> CanQuitToTitle;
	TFunction<URbGameUserSettings*()> GetSettings;
	TFunction<void()> SaveSettings;
	TFunction<const URbMatchDirector*()> GetDirector;  // the PLAYER's match (UX-T25); nullptr on the title
	TFunction<void(const TSharedPtr<SWidget>&)> SetFocus; // mirror an item into Slate's user focus (no-op without a window)
	bool bSkipIntro = false;                           // tests / captures: no clean-first-seconds delay on the title
	bool bReducedMotion = false;                       // screen pushes fade only (ui-ux 4.5)
};

// One focusable entry of a screen's focus list.
struct FRbFocusItem
{
	TSharedPtr<SWidget> Widget;
	bool bEnabled = true;
};

class RAWBREAK_API SRbScreen : public SCompoundWidget
{
public:
	virtual ~SRbScreen() override = default;

	ERbUiScreen GetScreenId() const { return ScreenId; }
	virtual bool PausesGame() const { return false; }
	virtual bool IsDialog() const { return false; }

	// Esc / the Back hint: default = close this screen. Returns true when handled.
	virtual bool HandleBack();

	// Keyboard: the screen's whole key map (tests call it directly; OnKeyDown forwards to it).
	virtual bool HandleKey(const FKey& Key, const FModifierKeysState& Modifiers);
	// Key released (hold-to-preview in the settings).
	virtual bool HandleKeyUp(const FKey& Key) { return false; }
	// Keys whose OS auto-repeat reaches HandleKey (focus moves, row changes). Every other repeat is swallowed by OnKeyDown: a held
	// Enter / Space / Esc acts once.
	static bool IsRepeatableKey(const FKey& Key);

	// Focus list.
	int32 GetFocusIndex() const { return FocusIndex; }
	int32 GetNumFocusItems() const { return FocusItems.Num(); }
	TSharedPtr<SWidget> GetFocusedWidget() const;
	TSharedPtr<SWidget> GetFocusItemWidget(int32 Index) const { return FocusItems.IsValidIndex(Index) ? FocusItems[Index].Widget : nullptr; }
	// Moves the focus to an item (skips nothing: disabled items can hold focus to show why they are disabled). bFromMouse: the
	// item is under the cursor already, so the screen does not scroll it (scrolling would move another item under the cursor).
	void SetFocusIndex(int32 Index, bool bFromMouse = false);
	bool IsFocusFromMouse() const { return bFocusFromMouse; }
	// Activates the focused item (Enter / click).
	virtual void ActivateFocused() {}
	// Left / Right on the focused item (settings rows); true when something changed.
	virtual bool AdjustFocused(int32 Direction) { return false; }

	// Called by the host after a push / when the screen becomes the top again, and when another screen covers it.
	virtual void OnActivated();
	virtual void OnDeactivated() {}

	// The screen's appear animation (push: 24 px slide + fade, 220 ms; reduced motion: fade 120 ms).
	float GetAppear() const { return Appear; }

	// SWidget
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; } // screens draw their own focus
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

protected:
	void InitScreen(ERbUiScreen InId, const TSharedRef<FRbUiHost>& InHost);
	// Rebuilds the focus list (screens call it after (re)building their items); keeps the index when possible.
	void SetFocusItems(TArray<FRbFocusItem> Items, int32 PreferredIndex = INDEX_NONE);
	// Moves the focus by Direction, skipping nothing, clamped (no wrap: the ends are felt).
	void MoveFocus(int32 Direction);
	virtual void OnFocusChanged() {}
	void MirrorFocus() const;

	ERbUiScreen ScreenId = ERbUiScreen::None;
	TSharedPtr<FRbUiHost> Host;
	TArray<FRbFocusItem> FocusItems;
	int32 FocusIndex = 0;
	bool bFocusFromMouse = false; // the last focus change came from the pointer (hover / click)
	float Appear = 1.0f;
	bool bAnimating = false;
};
