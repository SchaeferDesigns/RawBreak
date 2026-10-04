#include "UI/Screens/SRbSettingsMenu.h"

#include "Settings/RbGameUserSettings.h"
#include "UI/Core/RbUiStyle.h"
#include "UI/Screens/SRbConfirmDialog.h"
#include "UI/Widgets/SRbMenuWidgets.h"

#include "Layout/WidgetPath.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Owner: M2-D. Settings screen (header).

#define LOCTEXT_NAMESPACE "RbSettingsMenu"

using namespace RbUi;

namespace
{
	// The row list asks for at most 13 rows (it gets the whole height between the tabs and the footer: ~13.8 rows at 1080p).
	constexpr float ListMaxDesiredHeight = 13.0f * RowHeight;
}

void SRbSettingsMenu::Construct(const FArguments& InArgs)
{
	InitScreen(ERbUiScreen::Settings, InArgs._Host.ToSharedRef());
	Page = InArgs._InitialPage;

	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < static_cast<int32>(ERbSettingsPage::Count); ++Index)
	{
		const ERbSettingsPage TabPage = static_cast<ERbSettingsPage>(Index);
		TSharedRef<SRbTab> Tab = SNew(SRbTab)
			.Label(FRbSettingsRegistry::PageName(TabPage))
			.IsActive_Lambda([this, TabPage]() { return Page == TabPage; })
			.OnClicked_Lambda([this, TabPage]() { SetPage(TabPage); });
		TabWidgets.Add(Tab);
		Tabs->AddSlot()
		.AutoWidth()
		[
			Tab
		];
	}

	const auto Hint = [this](const FText& Key, const FText& Verb, TFunction<void()> Action)
	{
		return SNew(SRbHintButton).Key(Key).Verb(Verb).OnClicked_Lambda([Action]() { if (Action) { Action(); } });
	};

	ChildSlot
	[
		SAssignNew(Panel, SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.BlurStrength(MenuBlurStrength)
		]
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(WhiteBrush())
			.ColorAndOpacity(Color(EColor::Ink900, ScrimMenu))
		]
		+ SOverlay::Slot()
		.Padding(FMargin(SafeX, SafeY, SafeX, SafeY))
		[
			SNew(SVerticalBox)
			// Header: title + preset
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Settings", "Settings"))
					.Font(Font(EFont::Display))
					.ColorAndOpacity(Color(EColor::Chalk100))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const URbGameUserSettings* S = Settings();
						return S ? FText::Format(LOCTEXT("PresetHeader", "Preset: {0}"), FRbSettingsRegistry::PresetLabel(*S)) : FText::GetEmpty();
					})
					.Font(Font(EFont::Label))
					.ColorAndOpacity(Color(EColor::Chalk300))
				]
			]
			// Tabs + divider
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 18.0f, 0.0f, 0.0f))
			[
				Tabs
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(1.0f)
				[
					SNew(SImage)
					.Image(WhiteBrush())
					.ColorAndOpacity(Color(EColor::Line600))
				]
			]
			// Rows | description
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(FMargin(0.0f, 20.0f, 0.0f, 12.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SBox)
					.MaxDesiredWidth(ContentMaxWidth)
					.MaxDesiredHeight(ListMaxDesiredHeight) // longer pages scroll; the slot fills the space it gets anyway
					[
						SAssignNew(Scroll, SScrollBox)
						.ScrollBarThickness(FVector2D(4.0, 4.0))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(40.0f, 0.0f, 32.0f, 0.0f))
				[
					SNew(SBox)
					.WidthOverride(1.0f)
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity(Color(EColor::Line600))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(DescriptionWidth)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(this, &SRbSettingsMenu::DescriptionTitle)
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
							.Text(this, &SRbSettingsMenu::DescriptionBody)
							.Font(Font(EFont::Body))
							.ColorAndOpacity(Color(EColor::Chalk300))
							.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
						[
							SNew(STextBlock)
							.Text(this, &SRbSettingsMenu::DescriptionReason)
							.Font(Font(EFont::Body))
							.ColorAndOpacity(Color(EColor::Amber400))
							.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
							.Visibility_Lambda([this]() { return DescriptionReason().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
						[
							SNew(STextBlock)
							.Text(this, &SRbSettingsMenu::DescriptionMeta)
							.Font(Font(EFont::Label))
							.ColorAndOpacity(Color(EColor::Chalk300))
							.AutoWrapText(true)
						.Tag(RbUi::WrapTag)
						]
					]
				]
			]
			// Footer
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Hint(LOCTEXT("KeyEnter", "Enter"), LOCTEXT("Change", "Change"), [this]() { ActivateFocused(); })]
				+ SHorizontalBox::Slot().AutoWidth()[Hint(LOCTEXT("KeyQE", "Q / E"), LOCTEXT("Page", "Page"), [this]()
				{
					SetPage(static_cast<ERbSettingsPage>((static_cast<int32>(Page) + 1) % static_cast<int32>(ERbSettingsPage::Count)));
				})]
				+ SHorizontalBox::Slot().AutoWidth()[Hint(LOCTEXT("KeyR", "R"), LOCTEXT("ResetPage", "Reset page"), [this]() { AskResetPage(); })]
				+ SHorizontalBox::Slot().AutoWidth()[Hint(LOCTEXT("KeySpace", "Space"), LOCTEXT("Preview", "Hold to preview"), nullptr)]
				+ SHorizontalBox::Slot().AutoWidth()[Hint(LOCTEXT("KeyEsc", "Esc"), LOCTEXT("Back", "Back"), [this]() { HandleBack(); })]
			]
		]
	];
	BuildRows();
}

URbGameUserSettings* SRbSettingsMenu::Settings() const
{
	return Host.IsValid() && Host->GetSettings ? Host->GetSettings() : nullptr;
}

void SRbSettingsMenu::SetPage(ERbSettingsPage InPage)
{
	if (InPage == Page && RowWidgets.Num() > 0)
	{
		return;
	}
	Page = InPage;
	BuildRows();
}

void SRbSettingsMenu::BuildRows()
{
	PageRows = FRbSettingsRegistry::RowsOnPage(Page);
	RowWidgets.Reset();
	TArray<FRbFocusItem> Focus;
	if (Scroll.IsValid())
	{
		Scroll->ClearChildren();
	}
	const TSharedPtr<FRbUiHost> H = Host;
	for (int32 Index = 0; Index < PageRows.Num(); ++Index)
	{
		const FRbSettingDef* Row = PageRows[Index];
		TSharedRef<SRbOptionRow> Widget = SNew(SRbOptionRow)
			.Row(Row)
			.GetSettings([H]() { return H.IsValid() && H->GetSettings ? H->GetSettings() : nullptr; })
			.IsFocused_Lambda([this, Index]() { return FocusIndex == Index; })
			.OnHovered_Lambda([this, Index]() { SetFocusIndex(Index, true); })
			.OnStep_Lambda([this, Row](int32 Direction)
			{
				if (const URbGameUserSettings* S = Settings())
				{
					ChangeRow(*Row, FRbSettingsRegistry::StepValue(*Row, *S, Direction));
				}
			})
			.OnSetFraction_Lambda([this, Row](float Fraction)
			{
				const double Raw = Row->Min + Fraction * (Row->Max - Row->Min);
				const double Snapped = Row->Step > 0.0 ? Row->Min + FMath::RoundToDouble((Raw - Row->Min) / Row->Step) * Row->Step : Raw;
				ChangeRow(*Row, FMath::Clamp(Snapped, Row->Min, Row->Max));
			})
			.OnActivate_Lambda([this, Index]()
			{
				SetFocusIndex(Index, true);
				ActivateFocused(); // a click on the value = Enter (the next value, wrapping)
			});
		if (Scroll.IsValid())
		{
			Scroll->AddSlot()[Widget];
		}
		RowWidgets.Add(Widget);
		Focus.Add({Widget, true});
	}
	SetFocusItems(MoveTemp(Focus), 0);
	if (Scroll.IsValid())
	{
		Scroll->ScrollToStart();
	}
}

const FRbSettingDef* SRbSettingsMenu::GetFocusedRow() const
{
	return PageRows.IsValidIndex(FocusIndex) ? PageRows[FocusIndex] : nullptr;
}

TSharedPtr<SRbOptionRow> SRbSettingsMenu::GetRowWidget(int32 Index) const
{
	return RowWidgets.IsValidIndex(Index) ? RowWidgets[Index] : nullptr;
}

void SRbSettingsMenu::OnFocusChanged()
{
	// Keyboard focus scrolls its row into view. A row focused by the pointer is under the cursor already: scrolling it would put
	// the next row under the still cursor, which then takes the focus and scrolls again (the list ran to its end).
	if (!bFocusFromMouse && Scroll.IsValid() && RowWidgets.IsValidIndex(FocusIndex) && RowWidgets[FocusIndex].IsValid())
	{
		Scroll->ScrollDescendantIntoView(RowWidgets[FocusIndex], false, EDescendantScrollDestination::IntoView, RowHeight);
	}
}

void SRbSettingsMenu::ChangeRow(const FRbSettingDef& Row, double NewValue)
{
	URbGameUserSettings* S = Settings();
	if (!S || !FRbSettingsRegistry::IsAvailable(Row, *S))
	{
		return;
	}
	const double Old = FRbSettingsRegistry::GetValue(Row, *S);
	if (FMath::IsNearlyEqual(Old, NewValue, 1e-9))
	{
		return;
	}
	// The video mode before the change (the revert target of the display rows). Stored as the mode itself, never as the row's
	// value: the resolution row's value is an index into a list that contains the CURRENT resolution, so the indices shift when
	// the resolution changes and the old index can name a different resolution afterwards.
	const EWindowMode::Type OldWindowMode = S->GetFullscreenMode();
	const FIntPoint OldResolution = S->GetScreenResolution();
	FRbSettingsRegistry::SetValue(Row, *S, NewValue);
	FRbSettingsRegistry::Apply(Row, *S);
	bDirty = true;
	if (Row.Apply != ERbSettingApply::ConfirmRevert15s || !Host->PushScreen)
	{
		return;
	}
	// Window mode / resolution: keep within 15 s or revert (ui-ux 13.2).
	const TSharedPtr<FRbUiHost> H = Host;
	Host->PushScreen(SNew(SRbConfirmDialog)
		.Host(Host)
		.Title(LOCTEXT("KeepTitle", "Keep these display settings?"))
		.Message(FText::GetEmpty())
		.ConfirmLabel(LOCTEXT("Keep", "Keep"))
		.CancelLabel(LOCTEXT("Revert", "Revert"))
		.TimeoutSeconds(15.0f)
		.bFocusConfirm(true)
		.OnConfirm_Lambda([H]()
		{
			if (URbGameUserSettings* Current = H->GetSettings ? H->GetSettings() : nullptr)
			{
				Current->ConfirmVideoMode();
			}
		})
		.OnCancel_Lambda([H, OldWindowMode, OldResolution]()
		{
			if (URbGameUserSettings* Current = H->GetSettings ? H->GetSettings() : nullptr)
			{
				RevertVideoMode(*Current, OldWindowMode, OldResolution);
			}
		}));
}

void SRbSettingsMenu::RevertVideoMode(URbGameUserSettings& Settings, EWindowMode::Type WindowMode, const FIntPoint& Resolution)
{
	Settings.SetFullscreenMode(WindowMode);
	if (Resolution.X > 0 && Resolution.Y > 0)
	{
		Settings.SetScreenResolution(Resolution);
	}
	Settings.ApplyResolutionSettings(false);
	Settings.NotifySettingsChanged();
}

void SRbSettingsMenu::ActivateFocused()
{
	const FRbSettingDef* Row = GetFocusedRow();
	URbGameUserSettings* S = Settings();
	if (!Row || !S)
	{
		return;
	}
	if (Row->Type == ERbSettingType::Bool)
	{
		ChangeRow(*Row, FRbSettingsRegistry::StepValue(*Row, *S, 1));
	}
	else if (Row->Type == ERbSettingType::Enum)
	{
		// Enter = the next value, wrapping (a stepper you can cycle with one key).
		const int32 Count = FRbSettingsRegistry::NumEnumValues(*Row, *S);
		const int32 Next = Count > 0 ? (FMath::RoundToInt(FRbSettingsRegistry::GetValue(*Row, *S)) + 1) % Count : 0;
		ChangeRow(*Row, Next);
	}
}

bool SRbSettingsMenu::AdjustFocused(int32 Direction)
{
	const FRbSettingDef* Row = GetFocusedRow();
	URbGameUserSettings* S = Settings();
	if (!Row || !S)
	{
		return false;
	}
	const double Before = FRbSettingsRegistry::GetValue(*Row, *S);
	ChangeRow(*Row, FRbSettingsRegistry::StepValue(*Row, *S, Direction));
	return !FMath::IsNearlyEqual(Before, FRbSettingsRegistry::GetValue(*Row, *S), 1e-9);
}

bool SRbSettingsMenu::HandleBack()
{
	if (bDirty && Host->SaveSettings)
	{
		Host->SaveSettings(); // saved when leaving the screen (ui-ux 13.2)
	}
	bDirty = false;
	return SRbScreen::HandleBack();
}

void SRbSettingsMenu::AskResetPage()
{
	if (!Host->PushScreen)
	{
		return;
	}
	const TSharedPtr<FRbUiHost> H = Host;
	const ERbSettingsPage ResetPage = Page;
	TWeakPtr<SRbSettingsMenu> Weak = StaticCastSharedRef<SRbSettingsMenu>(AsShared());
	Host->PushScreen(SNew(SRbConfirmDialog)
		.Host(Host)
		.Title(FText::Format(LOCTEXT("ResetTitle", "Reset the {0} page to its defaults?"), FRbSettingsRegistry::PageName(ResetPage)))
		.Message(ResetPage == ERbSettingsPage::Display ? LOCTEXT("ResetDisplayNote", "Window mode and resolution keep their values.") : FText::GetEmpty())
		.ConfirmLabel(LOCTEXT("Reset", "Reset"))
		.CancelLabel(LOCTEXT("Cancel", "Cancel"))
		.OnConfirm_Lambda([H, ResetPage, Weak]()
		{
			if (URbGameUserSettings* S = H->GetSettings ? H->GetSettings() : nullptr)
			{
				FRbSettingsRegistry::ResetPage(ResetPage, *S);
			}
			if (const TSharedPtr<SRbSettingsMenu> Menu = Weak.Pin())
			{
				Menu->bDirty = true;
			}
		}));
}

bool SRbSettingsMenu::HandleKey(const FKey& Key, const FModifierKeysState& Modifiers)
{
	const int32 Pages = static_cast<int32>(ERbSettingsPage::Count);
	if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::PageUp)
	{
		SetPage(static_cast<ERbSettingsPage>((static_cast<int32>(Page) + Pages - 1) % Pages));
		return true;
	}
	if (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder || Key == EKeys::PageDown)
	{
		SetPage(static_cast<ERbSettingsPage>((static_cast<int32>(Page) + 1) % Pages));
		return true;
	}
	if (Key == EKeys::R || Key == EKeys::Gamepad_FaceButton_Left)
	{
		AskResetPage();
		return true;
	}
	if (Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Top)
	{
		bPreview = true; // hold to preview (ui-ux 13.1)
		return true;
	}
	return SRbScreen::HandleKey(Key, Modifiers);
}

bool SRbSettingsMenu::HandleKeyUp(const FKey& Key)
{
	if (Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Top)
	{
		bPreview = false;
		return true;
	}
	return false;
}

void SRbSettingsMenu::OnDeactivated()
{
	SRbScreen::OnDeactivated();
	bPreview = false; // a dialog took the keys: the Space release would never reach this screen (the panel stayed invisible)
}

void SRbSettingsMenu::OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	SRbScreen::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);
	if (bPreview && !NewWidgetPath.ContainsWidget(this))
	{
		bPreview = false; // the keyboard left the screen (window switch) while Space was held
	}
}

void SRbSettingsMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SRbScreen::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float Target = bPreview ? 0.0f : 1.0f;
	const float Dt = FMath::Clamp(InDeltaTime, 0.0f, 0.1f);
	PreviewAlpha = PreviewAlpha < Target ? FMath::Min(Target, PreviewAlpha + Dt / FadeIn) : FMath::Max(Target, PreviewAlpha - Dt / FadeIn);
	if (Panel.IsValid())
	{
		Panel->SetRenderOpacity(PreviewAlpha);
	}
}

FText SRbSettingsMenu::DescriptionTitle() const
{
	const FRbSettingDef* Row = GetFocusedRow();
	return Row ? Row->Label : FText::GetEmpty();
}

FText SRbSettingsMenu::DescriptionBody() const
{
	const FRbSettingDef* Row = GetFocusedRow();
	return Row ? Row->Description : FText::GetEmpty();
}

FText SRbSettingsMenu::DescriptionReason() const
{
	const FRbSettingDef* Row = GetFocusedRow();
	const URbGameUserSettings* S = Settings();
	FText Reason;
	if (Row && S && !FRbSettingsRegistry::IsAvailable(*Row, *S, &Reason))
	{
		return Reason;
	}
	return FText::GetEmpty();
}

FText SRbSettingsMenu::DescriptionMeta() const
{
	const FRbSettingDef* Row = GetFocusedRow();
	const URbGameUserSettings* S = Settings();
	if (!Row || !S)
	{
		return FText::GetEmpty();
	}
	const FText Default = FRbSettingsRegistry::FormatValue(*Row, *S, FRbSettingsRegistry::DefaultValue(*Row, *S));
	FText Apply;
	switch (Row->Apply)
	{
	case ERbSettingApply::Live: Apply = LOCTEXT("ApplyLive", "Applies immediately."); break;
	case ERbSettingApply::ConfirmRevert15s: Apply = LOCTEXT("ApplyConfirm", "Applies immediately; keep it within 15 seconds or it reverts."); break;
	case ERbSettingApply::StuttersBriefly: Apply = LOCTEXT("ApplyStutter", "Applies immediately; may stutter briefly."); break;
	case ERbSettingApply::Restart: Apply = LOCTEXT("ApplyRestart", "Applies after a restart."); break;
	}
	const FText Source = Row->bPresetDriven ? FText::Format(LOCTEXT("PresetDefault", "{0} preset: {1}"),
		FRbSettingsRegistry::PresetName(static_cast<int32>(S->GetCustomBasePreset())), Default) : FText::Format(LOCTEXT("Default", "Default: {0}"), Default);
	return FText::Format(LOCTEXT("Meta", "{0}\n{1}"), Source, Apply);
}

#undef LOCTEXT_NAMESPACE
