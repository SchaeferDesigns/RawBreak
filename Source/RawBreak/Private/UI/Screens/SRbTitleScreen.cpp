#include "UI/Screens/SRbTitleScreen.h"

#include "UI/Core/RbUiStyle.h"
#include "UI/Screens/SRbConfirmDialog.h"
#include "UI/Widgets/SRbMenuWidgets.h"

#include "Misc/ConfigCacheIni.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Owner: M2-D. Title / venue select (header).

#define LOCTEXT_NAMESPACE "RbTitle"

using namespace RbUi;

namespace RbTitlePrivate
{
	constexpr float ListLeft = 154.0f; // 8 % of 1920 (ui-ux 6.3)

	FText VersionText()
	{
		FString Version(TEXT("0.1.0"));
		if (GConfig)
		{
			GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
		}
		return FText::Format(LOCTEXT("Version", "v{0}  ·  EN"), FText::FromString(Version));
	}
}

float SRbTitleScreen::RbUiIntroEnd()
{
	return TitleIntroDelay + TitleIntroFade;
}

FText SRbTitleScreen::VenueName(ERbVenue InVenue)
{
	return InVenue == ERbVenue::DiveBar ? LOCTEXT("VenueDiveBar", "The Low Bridge Tavern") : LOCTEXT("VenueTestRoom", "Test room");
}

FText SRbTitleScreen::VenueDetail(ERbVenue InVenue)
{
	return InVenue == ERbVenue::DiveBar ? LOCTEXT("VenueDiveBarDetail", "Dive bar · 7-ft coin-op table")
		: LOCTEXT("VenueTestRoomDetail", "9-ft table under a tournament lamp");
}

void SRbTitleScreen::Construct(const FArguments& InArgs)
{
	InitScreen(ERbUiScreen::Title, InArgs._Host.ToSharedRef());
	IntroTime = Host->bSkipIntro ? RbUiIntroEnd() : 0.0f;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(Gradient, SRbGradient)
			.Stops(TArray<FVector2D>{FVector2D(0.0, 0.86), FVector2D(0.28, 0.78), FVector2D(0.50, 0.0)})
		]
		+ SOverlay::Slot()
		.Padding(FMargin(RbTitlePrivate::ListLeft, SafeY, SafeX, SafeY))
		[
			SAssignNew(Content, SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Logotype", "RAW BREAK"))
				.Font(Font(EFont::Logo))
				.ColorAndOpacity(Color(EColor::Chalk100))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(4.0f, 10.0f, 0.0f, 0.0f))
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
			.Padding(FMargin(0.0f, 52.0f, 0.0f, 0.0f))
			[
				SNew(SBox)
				.MinDesiredHeight(36.0f)
				.Padding(FMargin(FocusBarWidth + 20.0f, 0.0f, 0.0f, 0.0f))
				.VAlign(VAlign_Bottom)
				[
					SNew(STextBlock)
					.Text(this, &SRbTitleScreen::HeaderText)
					.Font(Font(EFont::LabelCaps))
					.TransformPolicy(ETextTransformPolicy::ToUpper)
					.ColorAndOpacity(Color(EColor::Chalk300))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
			[
				SAssignNew(List, SVerticalBox)
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
					SNew(SRbHintButton).Key(LOCTEXT("KeyEsc", "Esc")).Verb(LOCTEXT("Back", "Back"))
					.OnClicked_Lambda([this]() { HandleBack(); })
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(RbTitlePrivate::VersionText())
					.Font(Font(EFont::Caption))
					.ColorAndOpacity(Color(EColor::Chalk500))
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(RbTitlePrivate::ListLeft, 0.0f, 0.0f, SafeY + 64.0f))
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return LoadingText; })
			.Font(Font(EFont::Body))
			.ColorAndOpacity(Color(EColor::Chalk100))
			.Visibility_Lambda([this]() { return bLoading ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		]
	];
	ShowLevel(ELevel::Root);
	UpdateIntroOpacity();
}

void SRbTitleScreen::UpdateIntroOpacity()
{
	const float Opacity = ContentOpacity();
	if (Content.IsValid())
	{
		Content->SetRenderOpacity(Opacity);
	}
	if (Gradient.IsValid())
	{
		Gradient->SetRenderOpacity(0.35f + 0.65f * Opacity);
	}
}

float SRbTitleScreen::ContentOpacity() const
{
	return FMath::Clamp((IntroTime - TitleIntroDelay) / TitleIntroFade, 0.0f, 1.0f);
}

FText SRbTitleScreen::HeaderText() const
{
	switch (Level)
	{
	case ELevel::Play: return LOCTEXT("HeaderPlay", "Play");
	case ELevel::Venue: return VenueName(Venue);
	case ELevel::Root: break;
	}
	return FText::GetEmpty();
}

TArray<FString> SRbTitleScreen::GetItemLabels() const
{
	TArray<FString> Out;
	for (const FItem& Item : Items)
	{
		Out.Add(Item.Label.ToString());
	}
	return Out;
}

void SRbTitleScreen::ShowLevel(ELevel InLevel, int32 InFocusIndex)
{
	Level = InLevel;
	Items.Reset();
	switch (Level)
	{
	case ELevel::Root:
		Items.Add({LOCTEXT("Play", "Play"), FText::GetEmpty(), true, [this]() { ShowLevel(ELevel::Play); }});
		Items.Add({LOCTEXT("Settings", "Settings"), FText::GetEmpty(), true, [this]() { if (Host->OpenScreen) { Host->OpenScreen(ERbUiScreen::Settings); } }});
		Items.Add({LOCTEXT("Quit", "Quit"), FText::GetEmpty(), true, [this]() { AskQuit(); }});
		break;
	case ELevel::Play:
		for (const ERbVenue Candidate : {ERbVenue::DiveBar, ERbVenue::TestRoom})
		{
			FText Reason;
			const bool bAvailable = !Host->IsVenueAvailable || Host->IsVenueAvailable(Candidate, Reason);
			Items.Add({VenueName(Candidate), bAvailable ? VenueDetail(Candidate) : Reason, bAvailable, [this, Candidate]()
			{
				Venue = Candidate;
				ShowLevel(ELevel::Venue);
			}});
		}
		Items.Add({LOCTEXT("Back", "Back"), FText::GetEmpty(), true, [this]() { HandleBack(); }});
		break;
	case ELevel::Venue:
		for (const ERbMatchMode Mode : {ERbMatchMode::Practice, ERbMatchMode::HotSeat})
		{
			Items.Add({Mode == ERbMatchMode::Practice ? LOCTEXT("Practice", "Practice") : LOCTEXT("HotSeat", "Hot-seat"),
				Mode == ERbMatchMode::Practice ? LOCTEXT("PracticeDetail", "Shoot racks on your own; the rules still run")
					: LOCTEXT("HotSeatDetail", "Two players at one PC, taking turns"), true, [this, Mode]()
			{
				if (bLoading || !Host->StartVenue)
				{
					return;
				}
				if (Host->StartVenue(Venue, Mode))
				{
					bLoading = true;
					LoadingText = FText::Format(LOCTEXT("Heading", "Heading to {0}…"), VenueName(Venue));
				}
			}});
		}
		Items.Add({LOCTEXT("Back", "Back"), FText::GetEmpty(), true, [this]() { HandleBack(); }});
		break;
	}

	TArray<FRbFocusItem> Focus;
	if (List.IsValid())
	{
		List->ClearChildren();
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			const FItem& Item = Items[Index];
			TSharedRef<SRbMenuItem> Widget = SNew(SRbMenuItem)
				.Label(Item.Label)
				.Detail(Item.Detail)
				.bEnabled(Item.bEnabled)
				.IsFocused_Lambda([this, Index]() { return FocusIndex == Index; })
				.OnHovered_Lambda([this, Index]() { if (IsIntroDone()) { SetFocusIndex(Index, true); } })
				.OnClicked_Lambda([this, Index]()
				{
					if (!IsIntroDone())
					{
						SkipIntro();
						return;
					}
					SetFocusIndex(Index, true);
					ActivateFocused();
				});
			List->AddSlot().AutoHeight().HAlign(HAlign_Left)[Widget];
			Focus.Add({Widget, Item.bEnabled});
		}
	}
	SetFocusItems(MoveTemp(Focus), InFocusIndex);
}

void SRbTitleScreen::ActivateFocused()
{
	if (bLoading || !Items.IsValidIndex(FocusIndex) || !Items[FocusIndex].bEnabled)
	{
		return;
	}
	const TFunction<void()> Action = Items[FocusIndex].Action; // the action may rebuild Items
	Action();
}

bool SRbTitleScreen::HandleBack()
{
	if (bLoading)
	{
		return true;
	}
	switch (Level)
	{
	case ELevel::Venue:
		ShowLevel(ELevel::Play, Venue == ERbVenue::DiveBar ? 0 : 1);
		return true;
	case ELevel::Play:
		ShowLevel(ELevel::Root, 0);
		return true;
	case ELevel::Root:
		AskQuit();
		return true;
	}
	return true;
}

void SRbTitleScreen::AskQuit()
{
	if (!Host->PushScreen)
	{
		return;
	}
	const TSharedPtr<FRbUiHost> H = Host;
	Host->PushScreen(SNew(SRbConfirmDialog)
		.Host(Host)
		.Title(LOCTEXT("QuitTitle", "Quit RAW BREAK?"))
		.Message(FText::GetEmpty())
		.ConfirmLabel(LOCTEXT("QuitConfirm", "Quit to desktop"))
		.CancelLabel(LOCTEXT("QuitCancel", "Cancel"))
		.OnConfirm_Lambda([H]() { if (H->QuitToDesktop) { H->QuitToDesktop(); } }));
}

void SRbTitleScreen::SkipIntro()
{
	IntroTime = FMath::Max(IntroTime, RbUiIntroEnd());
	UpdateIntroOpacity();
}

bool SRbTitleScreen::HandleKey(const FKey& Key, const FModifierKeysState& Modifiers)
{
	if (!IsIntroDone())
	{
		SkipIntro(); // any input shows the list at once, and is consumed (nothing is chosen blind)
		return true;
	}
	return SRbScreen::HandleKey(Key, Modifiers);
}

FReply SRbTitleScreen::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SkipIntro();
	return SRbScreen::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SRbTitleScreen::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!IsIntroDone() && MouseEvent.GetCursorDelta().SizeSquared() > 64.0)
	{
		SkipIntro();
	}
	return FReply::Unhandled();
}

void SRbTitleScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SRbScreen::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	IntroTime += FMath::Clamp(InDeltaTime, 0.0f, 0.1f);
	UpdateIntroOpacity();
}

#undef LOCTEXT_NAMESPACE
