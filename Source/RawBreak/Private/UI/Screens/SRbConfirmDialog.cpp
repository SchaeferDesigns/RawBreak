#include "UI/Screens/SRbConfirmDialog.h"

#include "UI/Core/RbUiStyle.h"
#include "UI/Widgets/SRbMenuWidgets.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Owner: M2-D.

#define LOCTEXT_NAMESPACE "RbConfirmDialog"

using namespace RbUi;

void SRbConfirmDialog::Construct(const FArguments& InArgs)
{
	InitScreen(ERbUiScreen::Dialog, InArgs._Host.ToSharedRef());
	OnConfirm = InArgs._OnConfirm;
	OnCancel = InArgs._OnCancel;
	Timeout = InArgs._TimeoutSeconds;
	Remaining = Timeout;

	TSharedRef<SVerticalBox> Buttons = SNew(SVerticalBox);
	TArray<FRbFocusItem> Items;
	const auto AddButton = [this, &Buttons, &Items](const FText& Label, bool bConfirm)
	{
		const int32 Index = Items.Num();
		TSharedRef<SRbMenuItem> Item = SNew(SRbMenuItem)
			.Label(Label)
			.Font(EFont::Body)
			.bPanel(true)
			.IsFocused_Lambda([this, Index]() { return FocusIndex == Index; })
			.OnHovered_Lambda([this, Index]() { SetFocusIndex(Index, true); })
			.OnClicked_Lambda([this, bConfirm]() { Answer(bConfirm); });
		Buttons->AddSlot().AutoHeight()[Item];
		Items.Add({Item, true});
	};
	AddButton(InArgs._ConfirmLabel, true);
	AddButton(InArgs._CancelLabel, false);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(WhiteBrush())
			.ColorAndOpacity(Color(EColor::Ink900, 0.6f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(720.0f)
			[
				SNew(SBorder)
				.BorderImage(RoundedBrush())
				.BorderBackgroundColor(Color(EColor::Ink800))
				.Padding(FMargin(40.0f, 32.0f, 40.0f, 28.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(InArgs._Title)
						.Font(Font(EFont::Title))
						.ColorAndOpacity(Color(EColor::Chalk100))
						.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(InArgs._Message)
						.Font(Font(EFont::Body))
						.ColorAndOpacity(Color(EColor::Chalk300))
						.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
						.Visibility(InArgs._Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(this, &SRbConfirmDialog::CountdownText)
						.Font(Font(EFont::Mono))
						.ColorAndOpacity(Color(EColor::Amber400))
						.Visibility(Timeout > 0.0f ? EVisibility::Visible : EVisibility::Collapsed)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 24.0f, 0.0f, 0.0f))
					[
						Buttons
					]
				]
			]
		]
	];
	SetFocusItems(MoveTemp(Items), InArgs._bFocusConfirm ? 0 : 1);
}

FText SRbConfirmDialog::CountdownText() const
{
	return FText::Format(LOCTEXT("Reverting", "Reverting in {0} s"), FText::AsNumber(FMath::CeilToInt(FMath::Max(0.0f, Remaining))));
}

bool SRbConfirmDialog::HandleBack()
{
	Answer(false);
	return true;
}

void SRbConfirmDialog::ActivateFocused()
{
	Answer(FocusIndex == 0);
}

bool SRbConfirmDialog::AdjustFocused(int32 Direction)
{
	MoveFocus(Direction);
	return true;
}

void SRbConfirmDialog::Answer(bool bConfirm)
{
	if (bAnswered)
	{
		return;
	}
	bAnswered = true;
	const TSharedRef<SRbConfirmDialog> Self = StaticCastSharedRef<SRbConfirmDialog>(AsShared()); // alive until the answer ran
	if (Host.IsValid() && Host->CloseTop)
	{
		Host->CloseTop();
	}
	(bConfirm ? OnConfirm : OnCancel).ExecuteIfBound();
}

void SRbConfirmDialog::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SRbScreen::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Timeout > 0.0f && !bAnswered)
	{
		Remaining -= FMath::Clamp(InDeltaTime, 0.0f, 0.25f);
		if (Remaining <= 0.0f)
		{
			Answer(false);
		}
	}
}

#undef LOCTEXT_NAMESPACE
