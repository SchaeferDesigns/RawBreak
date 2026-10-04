#include "UI/Widgets/SRbMenuWidgets.h"

#include "Settings/RbGameUserSettings.h"
#include "Settings/RbSettingsRegistry.h"

#include "Rendering/DrawElements.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Owner: M2-D. Building blocks of the menus (header).

#define LOCTEXT_NAMESPACE "RbMenuWidgets"

using namespace RbUi;

bool RbUi::IsPointerMotion(const FPointerEvent& MouseEvent)
{
	const FVector2f Delta = MouseEvent.GetCursorDelta();
	return !Delta.IsNearlyZero(0.01f);
}

// ---------------------------------------------------------------------------------------------------------------------------
// SRbGradient
// ---------------------------------------------------------------------------------------------------------------------------

void SRbGradient::Construct(const FArguments& InArgs)
{
	Stops = InArgs._Stops;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SRbGradient::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (Stops.Num() < 2)
	{
		return LayerId;
	}
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	TArray<FSlateGradientStop> Gradient;
	for (const FVector2D& Stop : Stops)
	{
		Gradient.Add(FSlateGradientStop(FVector2D(Stop.X * Size.X, 0.0), Color(EColor::Ink900, static_cast<float>(Stop.Y) * InWidgetStyle.GetColorAndOpacityTint().A)));
	}
	FSlateDrawElement::MakeGradient(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), MoveTemp(Gradient), Orient_Vertical);
	return LayerId + 1;
}

// ---------------------------------------------------------------------------------------------------------------------------
// SRbKeycap
// ---------------------------------------------------------------------------------------------------------------------------

void SRbKeycap::Construct(const FArguments& InArgs)
{
	const TAttribute<FSlateColor> Tint = InArgs._Color.IsSet() ? InArgs._Color : TAttribute<FSlateColor>(FSlateColor(Color(EColor::Chalk100)));
	ChildSlot
	[
		SNew(SBox)
		.HeightOverride(GlyphHeight)
		.MinDesiredWidth(GlyphHeight)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(KeycapBrush())
			.BorderBackgroundColor(Tint)
			.Padding(FMargin(8.0f, 0.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(InArgs._Key)
				.Font(Font(EFont::Label))
				.ColorAndOpacity(Tint)
			]
		]
	];
}

// ---------------------------------------------------------------------------------------------------------------------------
// SRbMenuItem
// ---------------------------------------------------------------------------------------------------------------------------

void SRbMenuItem::Construct(const FArguments& InArgs)
{
	IsFocused = InArgs._IsFocused;
	OnHovered = InArgs._OnHovered;
	OnClicked = InArgs._OnClicked;
	bEnabled = InArgs._bEnabled;
	const bool bPanel = InArgs._bPanel;
	const EFont LabelFont = InArgs._Font;
	const TAttribute<FText> Detail = InArgs._Detail;

	TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(FocusBarWidth)
			[
				SNew(SImage)
				.Image(WhiteBrush())
				.ColorAndOpacity_Lambda([this]() { return IsFocused.Get(false) ? Color(EColor::Amber400) : FLinearColor::Transparent; })
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(20.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(InArgs._Label)
			.Font(Font(LabelFont))
			.ColorAndOpacity(this, &SRbMenuItem::LabelColor)
		];
	if (Detail.IsSet())
	{
		Line->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(FMargin(24.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(Detail)
			.Font(Font(EFont::Body))
			.ColorAndOpacity_Lambda([this]() { return Color(bEnabled ? EColor::Chalk300 : EColor::Chalk500); })
		];
	}
	const float Height = LabelFont == EFont::Menu ? 64.0f : RowHeight;
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(WhiteBrush())
		.BorderBackgroundColor_Lambda([this, bPanel]() { return bPanel && IsFocused.Get(false) ? Color(EColor::Ink700) : FLinearColor::Transparent; })
		.Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
		[
			SNew(SBox)
			.MinDesiredHeight(Height)
			[
				Line
			]
		]
	];
}

FSlateColor SRbMenuItem::LabelColor() const
{
	if (!bEnabled)
	{
		return Color(EColor::Chalk500);
	}
	return Color(IsFocused.Get(false) ? EColor::Chalk100 : EColor::Chalk300);
}

void SRbMenuItem::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsPointerMotion(MouseEvent))
	{
		OnHovered.ExecuteIfBound();
	}
}

FReply SRbMenuItem::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	OnHovered.ExecuteIfBound();
	return FReply::Handled();
}

FReply SRbMenuItem::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	OnClicked.ExecuteIfBound();
	return FReply::Handled();
}

void SRbMenuItem::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bPressed = false;
}

// ---------------------------------------------------------------------------------------------------------------------------
// SRbHintButton
// ---------------------------------------------------------------------------------------------------------------------------

void SRbHintButton::Construct(const FArguments& InArgs)
{
	OnClicked = InArgs._OnClicked;
	ChildSlot
	.Padding(FMargin(0.0f, 0.0f, 28.0f, 0.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SRbKeycap).Key(InArgs._Key).Color_Lambda([this]() { return FSlateColor(VerbColor()); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(10.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(InArgs._Verb)
			.Font(Font(EFont::Label))
			.ColorAndOpacity(this, &SRbHintButton::VerbColor)
		]
	];
}

FSlateColor SRbHintButton::VerbColor() const
{
	return Color(IsHovered() ? EColor::Amber400 : EColor::Chalk300);
}

FReply SRbHintButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled();
}

FReply SRbHintButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	OnClicked.ExecuteIfBound();
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------------------------------
// SRbTab
// ---------------------------------------------------------------------------------------------------------------------------

void SRbTab::Construct(const FArguments& InArgs)
{
	IsActive = InArgs._IsActive;
	OnClicked = InArgs._OnClicked;
	ChildSlot
	.Padding(FMargin(0.0f, 0.0f, 40.0f, 0.0f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InArgs._Label)
			.Font(Font(EFont::Title))
			.ColorAndOpacity_Lambda([this]()
			{
				return Color(IsActive.Get(false) ? EColor::Chalk100 : (IsHovered() ? EColor::Amber400 : EColor::Chalk300));
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
		[
			SNew(SBox)
			.HeightOverride(3.0f)
			[
				SNew(SImage)
				.Image(WhiteBrush())
				.ColorAndOpacity_Lambda([this]() { return IsActive.Get(false) ? Color(EColor::Amber400) : FLinearColor::Transparent; })
			]
		]
	];
}

FReply SRbTab::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	OnClicked.ExecuteIfBound();
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------------------------------
// SRbSliderBar
// ---------------------------------------------------------------------------------------------------------------------------

void SRbSliderBar::Construct(const FArguments& InArgs)
{
	Fraction = InArgs._Fraction;
	DefaultFraction = InArgs._DefaultFraction;
	IsBarEnabled = InArgs._BarEnabled;
	SetVisibility(EVisibility::HitTestInvisible);
}

FVector2D SRbSliderBar::ComputeDesiredSize(float) const
{
	return FVector2D(SRbOptionRow::TrackWidth, 28.0);
}

int32 SRbSliderBar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float F = FMath::Clamp(Fraction.Get(0.0f), 0.0f, 1.0f);
	const float D = FMath::Clamp(DefaultFraction.Get(-1.0f), -1.0f, 1.0f);
	const bool bOn = IsBarEnabled.Get(true);
	const float Tint = InWidgetStyle.GetColorAndOpacityTint().A;
	const float TrackH = 4.0f;
	const float Y = 0.5f * static_cast<float>(Size.Y) - 0.5f * TrackH;
	const auto Box = [&](float X0, float Y0, float W, float H, const FLinearColor& C, int32 Layer)
	{
		FLinearColor Tinted = C;
		Tinted.A *= Tint;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X0, Y0))),
			RoundedBrush(), ESlateDrawEffect::None, Tinted);
	};
	const float W = static_cast<float>(Size.X);
	Box(0.0f, Y, W, TrackH, Color(EColor::Line600), LayerId);
	Box(0.0f, Y, F * W, TrackH, Color(bOn ? EColor::Amber400 : EColor::Chalk500), LayerId + 1);
	if (D >= 0.0f)
	{
		Box(D * W - 1.0f, Y - 6.0f, 2.0f, TrackH + 12.0f, Color(EColor::Chalk500), LayerId + 1);
	}
	const float Thumb = 16.0f;
	Box(F * W - 0.5f * Thumb, 0.5f * static_cast<float>(Size.Y) - 0.5f * Thumb, Thumb, Thumb, Color(bOn ? EColor::Chalk100 : EColor::Chalk500), LayerId + 2);
	return LayerId + 3;
}

// ---------------------------------------------------------------------------------------------------------------------------
// SRbOptionRow
// ---------------------------------------------------------------------------------------------------------------------------

void SRbOptionRow::Construct(const FArguments& InArgs)
{
	Row = InArgs._Row;
	GetSettings = InArgs._GetSettings;
	IsFocused = InArgs._IsFocused;
	OnHovered = InArgs._OnHovered;
	OnStep = InArgs._OnStep;
	OnSetFraction = InArgs._OnSetFraction;
	OnActivate = InArgs._OnActivate;
	check(Row);

	TSharedRef<SWidget> Control = SNullWidget::NullWidget;
	if (Row->Type == ERbSettingType::Float)
	{
		Control = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(TrackWidth)
				.HeightOverride(28.0f)
				[
					SNew(SRbSliderBar)
					.Fraction(this, &SRbOptionRow::ValueFraction)
					.DefaultFraction(this, &SRbOptionRow::DefaultFraction)
					.BarEnabled(this, &SRbOptionRow::IsRowEnabled)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(TrackGap, 0.0f, 0.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SRbOptionRow::GetValueText)
				.Font(Font(EFont::Body))
				.ColorAndOpacity_Lambda([this]() { return TextColor(false); })
			];
	}
	else
	{
		const auto Arrow = [this](const TCHAR* Glyph, int32 Direction)
		{
			return SNew(SBox)
				.WidthOverride(ArrowWidth)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Glyph))
					.Font(Font(EFont::Title))
					.ColorAndOpacity_Lambda([this, Direction]()
					{
						if (!IsRowEnabled())
						{
							return FSlateColor(FLinearColor::Transparent);
						}
						const URbGameUserSettings* S = GetSettings ? GetSettings() : nullptr;
						const double V = S ? FRbSettingsRegistry::GetValue(*Row, *S) : 0.0;
						const int32 Count = S ? FRbSettingsRegistry::NumEnumValues(*Row, *S) : 0;
						const bool bCan = Row->Type == ERbSettingType::Bool || (Direction < 0 ? V > 0.5 : V < Count - 1.5);
						return FSlateColor(Color(bCan ? (IsFocused.Get(false) ? EColor::Amber400 : EColor::Chalk300) : EColor::Line600));
					})
				];
		};
		Control = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				Arrow(TEXT("‹"), -1)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SRbOptionRow::GetValueText)
				.Font(Font(EFont::Body))
				.ColorAndOpacity_Lambda([this]() { return TextColor(false); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				Arrow(TEXT("›"), 1)
			];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(WhiteBrush())
		.BorderBackgroundColor_Lambda([this]() { return IsFocused.Get(false) ? Color(EColor::Ink700) : FLinearColor::Transparent; })
		.Padding(0.0f)
		[
			SNew(SBox)
			.HeightOverride(RowHeight)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(FocusBarWidth)
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity_Lambda([this]() { return IsFocused.Get(false) ? Color(EColor::Amber400) : FLinearColor::Transparent; })
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(20.0f, 0.0f, 24.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(Row->Label)
					.Font(Font(EFont::Body))
					.ColorAndOpacity_Lambda([this]() { return TextColor(true); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(ControlWidth - FocusBarWidth)
					.Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
					[
						Control
					]
				]
			]
		]
	];
}

FText SRbOptionRow::GetValueText() const
{
	const URbGameUserSettings* S = GetSettings ? GetSettings() : nullptr;
	return S ? FRbSettingsRegistry::FormatValue(*Row, *S, FRbSettingsRegistry::GetValue(*Row, *S)) : FText::GetEmpty();
}

bool SRbOptionRow::IsRowEnabled() const
{
	const URbGameUserSettings* S = GetSettings ? GetSettings() : nullptr;
	return S && FRbSettingsRegistry::IsAvailable(*Row, *S);
}

FSlateColor SRbOptionRow::TextColor(bool bLabel) const
{
	if (!IsRowEnabled())
	{
		return Color(EColor::Chalk500);
	}
	return Color(bLabel && !IsFocused.Get(false) ? EColor::Chalk300 : EColor::Chalk100);
}

float SRbOptionRow::ValueFraction() const
{
	const URbGameUserSettings* S = GetSettings ? GetSettings() : nullptr;
	if (!S || Row->Max <= Row->Min)
	{
		return 0.0f;
	}
	return static_cast<float>((FRbSettingsRegistry::GetValue(*Row, *S) - Row->Min) / (Row->Max - Row->Min));
}

float SRbOptionRow::DefaultFraction() const
{
	const URbGameUserSettings* S = GetSettings ? GetSettings() : nullptr;
	if (!S || Row->Max <= Row->Min)
	{
		return -1.0f;
	}
	return static_cast<float>((FRbSettingsRegistry::DefaultValue(*Row, *S) - Row->Min) / (Row->Max - Row->Min));
}

SRbOptionRow::EZone SRbOptionRow::ZoneAt(float LocalX, float Width) const
{
	const float ControlStart = Width - ControlWidth + FocusBarWidth;
	if (LocalX < ControlStart)
	{
		return EZone::Label;
	}
	const float X = LocalX - ControlStart;
	if (Row->Type == ERbSettingType::Float)
	{
		return X <= TrackWidth + 0.5f * TrackGap ? EZone::Track : EZone::Value;
	}
	if (X < ArrowWidth)
	{
		return EZone::Decrement;
	}
	const float ControlInner = ControlWidth - FocusBarWidth - 12.0f;
	if (X > ControlInner - ArrowWidth && X <= ControlInner)
	{
		return EZone::Increment;
	}
	return EZone::Value;
}

float SRbOptionRow::TrackFraction(float LocalX, float Width)
{
	const float ControlStart = Width - ControlWidth + FocusBarWidth;
	return FMath::Clamp((LocalX - ControlStart) / TrackWidth, 0.0f, 1.0f);
}

void SRbOptionRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsPointerMotion(MouseEvent))
	{
		OnHovered.ExecuteIfBound();
	}
}

FReply SRbOptionRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	OnHovered.ExecuteIfBound();
	if (!IsRowEnabled())
	{
		return FReply::Handled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float Width = static_cast<float>(MyGeometry.GetLocalSize().X);
	switch (ZoneAt(static_cast<float>(Local.X), Width))
	{
	case EZone::Decrement:
		OnStep.ExecuteIfBound(-1);
		return FReply::Handled();
	case EZone::Increment:
		OnStep.ExecuteIfBound(1);
		return FReply::Handled();
	case EZone::Value:
		if (Row->Type != ERbSettingType::Float)
		{
			// The value cycles like Enter (wrapping): clicking "Windowed" moves on to "Fullscreen" instead of doing nothing.
			if (OnActivate.IsBound())
			{
				OnActivate.Execute();
			}
			else
			{
				OnStep.ExecuteIfBound(1);
			}
		}
		return FReply::Handled();
	case EZone::Track:
		bDragging = true;
		OnSetFraction.ExecuteIfBound(TrackFraction(static_cast<float>(Local.X), Width));
		return FReply::Handled().CaptureMouse(SharedThis(this));
	case EZone::Label:
		break;
	}
	return FReply::Handled();
}

FReply SRbOptionRow::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		bDragging = false; // the release went elsewhere: the bare cursor never moves the slider
		return FReply::Handled().ReleaseMouseCapture();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	OnSetFraction.ExecuteIfBound(TrackFraction(static_cast<float>(Local.X), static_cast<float>(MyGeometry.GetLocalSize().X)));
	return FReply::Handled();
}

FReply SRbOptionRow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SRbOptionRow::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
	bDragging = false; // Alt-Tab / a dialog took the mouse mid-drag: the next bare hover must not move the slider
}

#undef LOCTEXT_NAMESPACE
