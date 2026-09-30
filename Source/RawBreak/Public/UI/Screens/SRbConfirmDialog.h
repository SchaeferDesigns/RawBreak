#pragma once

// Modal confirm dialog (ui-ux 2.3 layer 70): quitting (to the title, to the desktop) and the display confirm-or-revert
// ("Keep these display settings? Reverting in 15 s"). Esc / Back = the cancel answer; a timeout = the cancel answer. The dialog
// closes itself first, then runs the answer. Owner: M2-D.

#include "CoreMinimal.h"

#include "UI/Core/SRbScreen.h"

class SRbMenuItem;
class STextBlock;

class RAWBREAK_API SRbConfirmDialog : public SRbScreen
{
public:
	SLATE_BEGIN_ARGS(SRbConfirmDialog)
		: _TimeoutSeconds(0.0f)
		, _bFocusConfirm(false)
		{}
		SLATE_ARGUMENT(TSharedPtr<FRbUiHost>, Host)
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(FText, Message)
		SLATE_ARGUMENT(FText, ConfirmLabel)
		SLATE_ARGUMENT(FText, CancelLabel)
		SLATE_ARGUMENT(float, TimeoutSeconds)   // > 0: the cancel answer runs by itself after this many seconds
		SLATE_ARGUMENT(bool, bFocusConfirm)     // the safe answer is focused by default (cancel), except for "keep"
		SLATE_EVENT(FSimpleDelegate, OnConfirm)
		SLATE_EVENT(FSimpleDelegate, OnCancel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool IsDialog() const override { return true; }
	virtual bool HandleBack() override;
	virtual void ActivateFocused() override;
	virtual bool AdjustFocused(int32 Direction) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void Answer(bool bConfirm);
	float GetRemainingSeconds() const { return Remaining; }
	bool IsAnswered() const { return bAnswered; }

private:
	FText CountdownText() const;

	FSimpleDelegate OnConfirm;
	FSimpleDelegate OnCancel;
	float Timeout = 0.0f;
	float Remaining = 0.0f;
	bool bAnswered = false;
};
