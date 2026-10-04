#include "UI/Screens/SRbPauseMenu.h"

#include "Game/RbMatchDirector.h"
#include "UI/Core/RbUiStyle.h"
#include "UI/RbOverlayComponent.h"
#include "UI/Screens/SRbConfirmDialog.h"
#include "UI/Widgets/SRbMenuWidgets.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Owner: M2-D. Pause menu (header).

#define LOCTEXT_NAMESPACE "RbPause"

using namespace RbUi;

FRbPauseInfo FRbPauseInfo::Build(const URbMatchDirector* Director)
{
	FRbPauseInfo Info;
	if (!Director || Director->GetPhase() == ERbDirectorPhase::Idle)
	{
		return Info;
	}
	// The same texts as the info overlay (one source for what the match says), without the last-shot block.
	const FRbOverlayModel Model = URbOverlayComponent::BuildModel(Director, FRbOverlayExtras());
	Info.bHasMatch = true;
	Info.Title = Model.Title;
	Info.Subtitle = Model.Subtitle;
	Info.Lines.Append(Model.MandatoryLines);
	Info.Lines.Append(Model.MatchLines);
	return Info;
}

FString FRbPauseInfo::ToDebugString() const
{
	FString Out = Title.ToString() + TEXT("\n") + Subtitle.ToString() + TEXT("\n");
	for (const FText& Line : Lines)
	{
		Out += Line.ToString() + TEXT("\n");
	}
	return Out;
}

void SRbPauseMenu::Construct(const FArguments& InArgs)
{
	InitScreen(ERbUiScreen::Pause, InArgs._Host.ToSharedRef());

	FText QuitReason;
	const bool bCanQuitToTitle = !Host->CanQuitToTitle || Host->CanQuitToTitle(QuitReason);
	Items.Add({LOCTEXT("Resume", "Resume"), true, [this]() { if (Host->Resume) { Host->Resume(); } }});
	Items.Add({LOCTEXT("Settings", "Settings"), true, [this]() { if (Host->OpenScreen) { Host->OpenScreen(ERbUiScreen::Settings); } }});
	Items.Add({LOCTEXT("QuitToTitle", "Quit to title"), bCanQuitToTitle, [this]() { AskQuitToTitle(); }});
	Items.Add({LOCTEXT("QuitToDesktop", "Quit to desktop"), true, [this]() { AskQuitToDesktop(); }});

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	TArray<FRbFocusItem> Focus;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		TSharedRef<SRbMenuItem> Widget = SNew(SRbMenuItem)
			.Label(Items[Index].Label)
			.Detail(Index == 2 && !bCanQuitToTitle ? QuitReason : FText::GetEmpty())
			.bEnabled(Items[Index].bEnabled)
			.IsFocused_Lambda([this, Index]() { return FocusIndex == Index; })
			.OnHovered_Lambda([this, Index]() { SetFocusIndex(Index, true); })
			.OnClicked_Lambda([this, Index]()
			{
				SetFocusIndex(Index, true);
				ActivateFocused();
			});
		List->AddSlot().AutoHeight()[Widget];
		Focus.Add({Widget, Items[Index].bEnabled});
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.BlurStrength(MenuBlurStrength)
			.bApplyAlphaToBlur(true)
		]
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(WhiteBrush())
			.ColorAndOpacity(Color(EColor::Ink900, 0.45f))
		]
		+ SOverlay::Slot()
		[
			SNew(SRbGradient)
			.Stops(TArray<FVector2D>{FVector2D(0.0, ScrimMenu), FVector2D(0.36, ScrimMenu), FVector2D(0.58, 0.0)})
		]
		+ SOverlay::Slot()
		.Padding(FMargin(154.0f, SafeY, SafeX, SafeY))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(560.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNullWidget::NullWidget
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(FocusBarWidth + 20.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Paused", "Paused"))
						.Font(Font(EFont::Display))
						.ColorAndOpacity(Color(EColor::Chalk100))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(FocusBarWidth + 22.0f, 8.0f, 0.0f, 0.0f))
					.HAlign(HAlign_Left)
					[
						SNew(SBox)
						.WidthOverride(72.0f)
						.HeightOverride(3.0f)
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity(Color(EColor::Amber400))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 36.0f, 0.0f, 0.0f))
					[
						List
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNullWidget::NullWidget
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SRbHintButton).Key(LOCTEXT("KeyEnter", "Enter")).Verb(LOCTEXT("Select", "Select"))
							.OnClicked_Lambda([this]() { ActivateFocused(); })
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SRbHintButton).Key(LOCTEXT("KeyEsc", "Esc")).Verb(LOCTEXT("ResumeHint", "Resume"))
							.OnClicked_Lambda([this]() { HandleBack(); })
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(600.0f)
				.Visibility_Lambda([this]() { return Info.bHasMatch ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
				[
					SNew(SBorder)
					.BorderImage(RoundedBrush())
					.BorderBackgroundColor(Color(EColor::Ink900, ScrimMenu))
					.Padding(FMargin(32.0f, 28.0f, 32.0f, 30.0f))
					[
						SAssignNew(InfoBox, SVerticalBox)
					]
				]
			]
		]
	];
	SetFocusItems(MoveTemp(Focus), 0);
	RefreshInfo();
}

void SRbPauseMenu::RefreshInfo()
{
	Info = FRbPauseInfo::Build(Host.IsValid() && Host->GetDirector ? Host->GetDirector() : nullptr);
	if (!InfoBox.IsValid())
	{
		return;
	}
	InfoBox->ClearChildren();
	if (!Info.bHasMatch)
	{
		return;
	}
	InfoBox->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.Text(Info.Title)
		.Font(Font(EFont::LabelCaps))
		.ColorAndOpacity(Color(EColor::Chalk300))
		.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
	];
	if (!Info.Subtitle.IsEmpty())
	{
		InfoBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(Info.Subtitle)
			.Font(Font(EFont::Title))
			.ColorAndOpacity(Color(EColor::Chalk100))
			.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
		];
	}
	InfoBox->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.0f, 16.0f, 0.0f, 12.0f))
	[
		SNew(SBox)
		.HeightOverride(1.0f)
		[
			SNew(SImage)
			.Image(WhiteBrush())
			.ColorAndOpacity(Color(EColor::Line600))
		]
	];
	for (const FText& Line : Info.Lines)
	{
		InfoBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(Line)
			.Font(Font(EFont::Body))
			.ColorAndOpacity(Color(EColor::Chalk100))
			.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
		];
	}
}

TArray<FString> SRbPauseMenu::GetItemLabels() const
{
	TArray<FString> Out;
	for (const FItem& Item : Items)
	{
		Out.Add(Item.Label.ToString());
	}
	return Out;
}

bool SRbPauseMenu::HandleBack()
{
	if (Host->Resume)
	{
		Host->Resume();
	}
	return true;
}

void SRbPauseMenu::ActivateFocused()
{
	if (Items.IsValidIndex(FocusIndex) && Items[FocusIndex].bEnabled)
	{
		const TFunction<void()> Action = Items[FocusIndex].Action;
		Action();
	}
}

void SRbPauseMenu::OnActivated()
{
	SRbScreen::OnActivated();
	RefreshInfo();
}

void SRbPauseMenu::AskQuitToTitle()
{
	if (!Host->PushScreen)
	{
		return;
	}
	const TSharedPtr<FRbUiHost> H = Host;
	Host->PushScreen(SNew(SRbConfirmDialog)
		.Host(Host)
		.Title(LOCTEXT("QuitTitleTitle", "Quit to the title?"))
		.Message(LOCTEXT("QuitTitleMessage", "The match will be lost."))
		.ConfirmLabel(LOCTEXT("QuitTitleConfirm", "Quit to title"))
		.CancelLabel(LOCTEXT("Cancel", "Cancel"))
		.OnConfirm_Lambda([H]() { if (H->QuitToTitle) { H->QuitToTitle(); } }));
}

void SRbPauseMenu::AskQuitToDesktop()
{
	if (!Host->PushScreen)
	{
		return;
	}
	const TSharedPtr<FRbUiHost> H = Host;
	Host->PushScreen(SNew(SRbConfirmDialog)
		.Host(Host)
		.Title(LOCTEXT("QuitDesktopTitle", "Quit to the desktop?"))
		.Message(LOCTEXT("QuitDesktopMessage", "The match will be lost."))
		.ConfirmLabel(LOCTEXT("QuitDesktopConfirm", "Quit to desktop"))
		.CancelLabel(LOCTEXT("Cancel", "Cancel"))
		.OnConfirm_Lambda([H]() { if (H->QuitToDesktop) { H->QuitToDesktop(); } }));
}

#undef LOCTEXT_NAMESPACE
