#pragma once

// Settings (ui-ux 13, M2 rows of Docs/ue-architecture.md 18.4): pages Graphics (quality preset Low..Cinematic + the individual
// quality rows, resolution scale, depth of field, motion blur, grain), Display (window mode and resolution with the 15 s
// keep-or-revert, frame cap, v-sync), Camera (look, FOV, comfort, reduced motion), Controls (mouse DPI, aim / look / stroke
// sensitivity, fine-aim factor, acceleration, invert, key hints), Audio (volumes). Rows come from FRbSettingsRegistry; every
// change is live (stored, then applied); the settings are saved when the screen closes.
// Keyboard: Up / Down rows, Left / Right change, Enter = next value / toggle, Q / E pages, R reset the page (confirm), hold Space
// = preview (the panel fades out to show the live view), Esc back. Mouse: tabs, arrows, sliders (click / drag), footer hints.
// Owner: M2-D.

#include "CoreMinimal.h"

#include "Settings/RbSettingsRegistry.h"
#include "UI/Core/SRbScreen.h"

class SScrollBox;
class SVerticalBox;
class SRbOptionRow;

class RAWBREAK_API SRbSettingsMenu : public SRbScreen
{
public:
	SLATE_BEGIN_ARGS(SRbSettingsMenu)
		: _InitialPage(ERbSettingsPage::Graphics)
		{}
		SLATE_ARGUMENT(TSharedPtr<FRbUiHost>, Host)
		SLATE_ARGUMENT(ERbSettingsPage, InitialPage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	ERbSettingsPage GetPage() const { return Page; }
	void SetPage(ERbSettingsPage InPage);
	const FRbSettingDef* GetFocusedRow() const;
	TSharedPtr<SRbOptionRow> GetRowWidget(int32 Index) const;
	TSharedPtr<SWidget> GetTabWidget(ERbSettingsPage InPage) const { return TabWidgets.IsValidIndex(static_cast<int32>(InPage)) ? TabWidgets[static_cast<int32>(InPage)] : nullptr; }
	bool IsDirty() const { return bDirty; }
	bool IsPreviewing() const { return bPreview; }

	// Stores and applies a new value for a row (the display rows ask to keep it within 15 s). Public for the tests.
	void ChangeRow(const FRbSettingDef& Row, double NewValue);

	virtual bool HandleBack() override;
	virtual bool HandleKey(const FKey& Key, const FModifierKeysState& Modifiers) override;
	virtual bool HandleKeyUp(const FKey& Key) override;
	virtual void ActivateFocused() override;
	virtual bool AdjustFocused(int32 Direction) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

protected:
	virtual void OnFocusChanged() override;

private:
	void BuildRows();
	void AskResetPage();
	URbGameUserSettings* Settings() const;
	FText DescriptionTitle() const;
	FText DescriptionBody() const;
	FText DescriptionMeta() const;
	FText DescriptionReason() const;

	ERbSettingsPage Page = ERbSettingsPage::Graphics;
	TArray<const FRbSettingDef*> PageRows;
	TArray<TSharedPtr<SRbOptionRow>> RowWidgets;
	TArray<TSharedPtr<SWidget>> TabWidgets;
	TSharedPtr<SScrollBox> Scroll;
	TSharedPtr<SWidget> Panel;
	bool bDirty = false;
	bool bPreview = false;
	float PreviewAlpha = 1.0f;
};
