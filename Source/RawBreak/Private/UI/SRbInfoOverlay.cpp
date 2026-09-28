#include "UI/SRbInfoOverlay.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// Owner: UE-7. Corner panel of Docs/ue-architecture.md 6.6 (layout and colour rules in the header).

namespace RbOverlayWidget
{
	// Slate units (1080p = 1:1; the game viewport's DPI scale enlarges them on larger screens).
	constexpr float CornerMarginX = 30.0f;
	constexpr float CornerMarginY = 26.0f;
	constexpr float CardMinWidth = 340.0f;
	constexpr float CardMaxWidth = 560.0f;
	constexpr float CardGap = 8.0f;
	constexpr float BarWidth = 3.0f;
	constexpr float CardRadius = 6.0f;

	const FLinearColor CardColor(0.0f, 0.0f, 0.0f, 0.58f);

	FSlateFontInfo Font(const ANSICHAR* Face, float Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Face, Size);
	}

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
	}

	void AppendLines(FString& Out, const TCHAR* Prefix, const TArray<FText>& Lines)
	{
		for (const FText& Line : Lines)
		{
			Out += Prefix;
			Out += Line.ToString();
			Out += TEXT("\n");
		}
	}
}

FString FRbOverlayModel::ToDebugString() const
{
	FString Out;
	RbOverlayWidget::AppendLines(Out, TEXT("[M] "), MandatoryLines);
	if (!Title.IsEmpty())
	{
		Out += TEXT("[T] ") + Title.ToString() + TEXT("\n");
	}
	if (!Subtitle.IsEmpty())
	{
		Out += TEXT("[T] ") + Subtitle.ToString() + TEXT("\n");
	}
	RbOverlayWidget::AppendLines(Out, TEXT("[S] "), MatchLines);
	RbOverlayWidget::AppendLines(Out, TEXT("[L] "), LastShotLines);
	if (bShowDebug)
	{
		RbOverlayWidget::AppendLines(Out, TEXT("[D] "), DebugLines);
	}
	return Out;
}

FLinearColor SRbInfoOverlay::ToneColor(ERbOverlayTone Tone)
{
	using namespace RbOverlayWidget;
	switch (Tone)
	{
	case ERbOverlayTone::Dim: return Srgb(150, 156, 162);
	case ERbOverlayTone::Info: return Srgb(86, 180, 233);     // Okabe-Ito sky blue
	case ERbOverlayTone::Warning: return Srgb(230, 159, 0);   // Okabe-Ito orange
	case ERbOverlayTone::Good: return Srgb(0, 158, 115);      // Okabe-Ito bluish green
	case ERbOverlayTone::Normal: break;
	}
	return Srgb(236, 236, 230);
}

void SRbInfoOverlay::Construct(const FArguments& /*InArgs*/)
{
	using namespace RbOverlayWidget;
	CardBrush = FSlateRoundedBoxBrush(CardColor, CardRadius);
	BarBrush = FSlateColorBrush(FLinearColor::White);

	SetVisibility(EVisibility::HitTestInvisible);
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(CornerMarginX, CornerMarginY, 0.0f, 0.0f))
	[
		SNew(SBox)
		.MinDesiredWidth(CardMinWidth)
		.MaxDesiredWidth(CardMaxWidth)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(MandatoryCard, SBorder)
				.BorderImage(&CardBrush)
				.Padding(FMargin(12.0f, 8.0f, 14.0f, 8.0f))
				[
					SAssignNew(MandatoryBox, SVerticalBox)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, CardGap, 0.0f, 0.0f))
			[
				SAssignNew(FullCard, SBorder)
				.BorderImage(&CardBrush)
				.Padding(FMargin(14.0f, 10.0f, 16.0f, 12.0f))
				[
					SAssignNew(FullBox, SVerticalBox)
				]
			]
		]
	];
	UpdateVisibility();
}

void SRbInfoOverlay::SetModel(const FRbOverlayModel& InModel)
{
	FString Key = InModel.ToDebugString();
	for (const ERbOverlayTone Tone : InModel.MandatoryTones)
	{
		Key.AppendChar(TEXT('0') + static_cast<TCHAR>(Tone));
	}
	for (const ERbOverlayTone Tone : InModel.MatchTones)
	{
		Key.AppendChar(TEXT('0') + static_cast<TCHAR>(Tone));
	}
	for (const ERbOverlayTone Tone : InModel.LastShotTones)
	{
		Key.AppendChar(TEXT('0') + static_cast<TCHAR>(Tone));
	}
	Model = InModel;
	if (Key == ModelKey)
	{
		return;
	}
	ModelKey = MoveTemp(Key);
	RebuildMandatory();
	RebuildFull();
	UpdateVisibility();
}

void SRbInfoOverlay::SetFullOpacity(float InOpacity)
{
	const float Clamped = FMath::Clamp(InOpacity, 0.0f, 1.0f);
	if (Clamped == FullOpacity)
	{
		return;
	}
	FullOpacity = Clamped;
	UpdateVisibility();
}

bool SRbInfoOverlay::IsMandatoryCardVisible() const
{
	return MandatoryCard.IsValid() && MandatoryCard->GetVisibility() != EVisibility::Collapsed && MandatoryCard->GetVisibility() != EVisibility::Hidden;
}

bool SRbInfoOverlay::IsFullCardVisible() const
{
	return FullCard.IsValid() && FullCard->GetVisibility() != EVisibility::Collapsed && FullCard->GetVisibility() != EVisibility::Hidden &&
		FullCard->GetRenderOpacity() > 0.0f;
}

FText SRbInfoOverlay::GetText() const
{
	return FText::FromString(Model.ToDebugString());
}

namespace RbOverlayWidget
{
	// One line: accent bar (tone colour) + text. Warnings are bold (meaning never by colour alone).
	TSharedRef<SWidget> MakeLine(const FText& Text, ERbOverlayTone Tone, const FSlateFontInfo& BaseFont, const FSlateBrush* Bar, bool bShowBar)
	{
		FSlateFontInfo LineFont = BaseFont;
		if (Tone == ERbOverlayTone::Warning)
		{
			LineFont.TypefaceFontName = FName(TEXT("Bold"));
		}
		const FLinearColor Color = SRbInfoOverlay::ToneColor(Tone);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.0f, 2.0f, 8.0f, 2.0f))
			[
				SNew(SBox)
				.WidthOverride(BarWidth)
				[
					SNew(SImage)
					.Image(Bar)
					.ColorAndOpacity(bShowBar ? Color : FLinearColor::Transparent)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(LineFont)
				.ColorAndOpacity(FSlateColor(Color))
				.ShadowOffset(FVector2D(1.0, 1.0))
				.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
				.AutoWrapText(true)
			];
	}

	TSharedRef<SWidget> MakeSeparator(const FSlateBrush* Bar)
	{
		return SNew(SBox)
			.HeightOverride(1.0f)
			.Padding(FMargin(0.0f))
			[
				SNew(SImage)
				.Image(Bar)
				.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.18f))
			];
	}
}

void SRbInfoOverlay::RebuildMandatory()
{
	using namespace RbOverlayWidget;
	if (!MandatoryBox.IsValid())
	{
		return;
	}
	MandatoryBox->ClearChildren();
	const FSlateFontInfo LineFont = Font("Regular", 13);
	for (int32 Index = 0; Index < Model.MandatoryLines.Num(); ++Index)
	{
		MandatoryBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, Index == 0 ? 0.0f : 3.0f, 0.0f, 0.0f))
		[
			MakeLine(Model.MandatoryLines[Index], Model.MandatoryTone(Index), LineFont, &BarBrush, true)
		];
	}
}

void SRbInfoOverlay::RebuildFull()
{
	using namespace RbOverlayWidget;
	if (!FullBox.IsValid())
	{
		return;
	}
	FullBox->ClearChildren();
	const FSlateFontInfo TitleFont = Font("Bold", 15);
	const FSlateFontInfo SubtitleFont = Font("Regular", 13);
	const FSlateFontInfo LineFont = Font("Regular", 12);
	const FSlateFontInfo SectionFont = Font("Bold", 10);
	const FSlateFontInfo DebugFont = Font("Mono", 10);
	bool bFirst = true;
	const auto AddWidget = [this, &bFirst](const TSharedRef<SWidget>& Widget, float Gap) {
		FullBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, bFirst ? 0.0f : Gap, 0.0f, 0.0f))
		[
			Widget
		];
		bFirst = false;
	};
	const auto AddText = [&](const FText& Text, const FSlateFontInfo& InFont, ERbOverlayTone Tone, float Gap) {
		AddWidget(SNew(STextBlock)
			.Text(Text)
			.Font(InFont)
			.ColorAndOpacity(FSlateColor(ToneColor(Tone)))
			.ShadowOffset(FVector2D(1.0, 1.0))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
			.AutoWrapText(true), Gap);
	};

	if (!Model.Title.IsEmpty())
	{
		AddText(Model.Title, TitleFont, ERbOverlayTone::Normal, 0.0f);
	}
	if (!Model.Subtitle.IsEmpty())
	{
		AddText(Model.Subtitle, SubtitleFont, ERbOverlayTone::Normal, 2.0f);
	}
	for (int32 Index = 0; Index < Model.MatchLines.Num(); ++Index)
	{
		AddWidget(MakeLine(Model.MatchLines[Index], Model.MatchTone(Index), LineFont, &BarBrush, Model.MatchTone(Index) != ERbOverlayTone::Normal),
			Index == 0 ? 8.0f : 2.0f);
	}
	if (Model.LastShotLines.Num() > 0)
	{
		AddWidget(MakeSeparator(&BarBrush), 8.0f);
		for (int32 Index = 0; Index < Model.LastShotLines.Num(); ++Index)
		{
			const ERbOverlayTone Tone = Model.LastShotTone(Index);
			if (Index == 0 && Tone == ERbOverlayTone::Dim)
			{
				AddText(Model.LastShotLines[Index], SectionFont, Tone, 7.0f); // section header, aligned with the title
				continue;
			}
			AddWidget(MakeLine(Model.LastShotLines[Index], Tone, LineFont, &BarBrush, Tone == ERbOverlayTone::Warning || Tone == ERbOverlayTone::Good),
				Index == 0 ? 7.0f : 2.0f);
		}
	}
	if (Model.bShowDebug && Model.DebugLines.Num() > 0)
	{
		AddWidget(MakeSeparator(&BarBrush), 8.0f);
		for (int32 Index = 0; Index < Model.DebugLines.Num(); ++Index)
		{
			AddText(Model.DebugLines[Index], DebugFont, ERbOverlayTone::Dim, Index == 0 ? 7.0f : 1.0f);
		}
	}
}

void SRbInfoOverlay::UpdateVisibility()
{
	if (MandatoryCard.IsValid())
	{
		MandatoryCard->SetVisibility(Model.MandatoryLines.Num() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
	if (FullCard.IsValid())
	{
		const bool bHasContent = !Model.Title.IsEmpty() || Model.MatchLines.Num() > 0 || Model.LastShotLines.Num() > 0 ||
			(Model.bShowDebug && Model.DebugLines.Num() > 0);
		FullCard->SetVisibility(FullOpacity > 0.0f && bHasContent ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		FullCard->SetRenderOpacity(FullOpacity);
	}
}
