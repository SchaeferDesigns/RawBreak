#pragma once

// Pause menu (ui-ux 12, M2 subset): Resume, Settings, Quit to title (confirm: the match is lost), Quit to desktop (confirm).
// Over the live scene, blurred, with an ink gradient on the left (menu column at scrim.menu opacity); on the right the match
// block of the PLAYER's match only (UX-T25: FRbPauseInfo from the host's director = the player session's): discipline and mode,
// score / rack, the mandatory lines (fouls, ball in hand, pending decision). The world is paused while it is open (the playback
// holds without a time jump, UX-T10). Esc = Resume. Owner: M2-D.

#include "CoreMinimal.h"

#include "UI/Core/SRbScreen.h"

class STextBlock;
class SVerticalBox;
class URbMatchDirector;

// The match block of the pause menu (built without Slate; tests check it).
struct RAWBREAK_API FRbPauseInfo
{
	FText Title;                 // "9-BALL  ·  HOT-SEAT"
	FText Subtitle;              // score line / rack number
	TArray<FText> Lines;         // mandatory lines first, then the match lines (shooter, fouls, ...)
	bool bHasMatch = false;

	static FRbPauseInfo Build(const URbMatchDirector* Director);
	FString ToDebugString() const;
};

class RAWBREAK_API SRbPauseMenu : public SRbScreen
{
public:
	SLATE_BEGIN_ARGS(SRbPauseMenu) {}
		SLATE_ARGUMENT(TSharedPtr<FRbUiHost>, Host)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool PausesGame() const override { return true; }
	virtual bool HandleBack() override;
	virtual void ActivateFocused() override;
	virtual void OnActivated() override;

	TArray<FString> GetItemLabels() const;
	const FRbPauseInfo& GetInfo() const { return Info; }
	void RefreshInfo();

private:
	void AskQuitToTitle();
	void AskQuitToDesktop();

	struct FItem
	{
		FText Label;
		bool bEnabled = true;
		TFunction<void()> Action;
	};
	TArray<FItem> Items;
	FRbPauseInfo Info;
	TSharedPtr<SVerticalBox> InfoBox;
};
