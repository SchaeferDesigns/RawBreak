#include "UI/Core/RbUiStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Styling/CoreStyle.h"

// Owner: M2-D. Tokens of ui-ux 4 (see the header). Tests: RawBreak.Unit.UI.Tokens*, RawBreak.Unit.UI.TextSize (RbUiTests.cpp).

namespace RbUi
{
	FColor Srgb(EColor Token)
	{
		switch (Token)
		{
		case EColor::Ink900: return FColor(0x0E, 0x0F, 0x10);
		case EColor::Ink800: return FColor(0x1A, 0x1C, 0x1E);
		case EColor::Ink700: return FColor(0x26, 0x29, 0x2C);
		case EColor::Line600: return FColor(0x3A, 0x3E, 0x42);
		case EColor::Chalk100: return FColor(0xED, 0xEB, 0xE6);
		case EColor::Chalk300: return FColor(0xB8, 0xB4, 0xAA);
		case EColor::Chalk500: return FColor(0x8A, 0x86, 0x7D);
		case EColor::Amber400: return FColor(0xFF, 0xB0, 0x2E);
		case EColor::SignalFoul: return FColor(0xFF, 0x5A, 0x4F);
		case EColor::SignalFoulText: return FColor(0xFF, 0x8A, 0x80);
		case EColor::SignalOk: return FColor(0x5B, 0xD9, 0x8A);
		case EColor::SignalInfo: return FColor(0x7F, 0xB8, 0xFF);
		case EColor::Count: break;
		}
		return FColor::White;
	}

	FLinearColor Color(EColor Token, float Alpha)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(Srgb(Token));
		C.A = Alpha;
		return C;
	}

	namespace
	{
		double Channel(uint8 V)
		{
			const double C = V / 255.0;
			return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4);
		}

		double Luminance(const FColor& C)
		{
			return 0.2126 * Channel(C.R) + 0.7152 * Channel(C.G) + 0.0722 * Channel(C.B);
		}
	}

	double ContrastRatio(const FColor& A, const FColor& B)
	{
		const double La = Luminance(A);
		const double Lb = Luminance(B);
		return (FMath::Max(La, Lb) + 0.05) / (FMath::Min(La, Lb) + 0.05);
	}

	FColor ScrimOverWhite(EColor Scrim, float Opacity)
	{
		const FLinearColor S = Color(Scrim);
		const FLinearColor Mixed = S * Opacity + FLinearColor::White * (1.0f - Opacity);
		return FLinearColor(Mixed.R, Mixed.G, Mixed.B, 1.0f).ToFColorSRGB();
	}

	namespace
	{
		struct FFontToken
		{
			const TCHAR* Name;
			const ANSICHAR* Face;
			float Points;
			int32 LetterSpacing; // 1/1000 em
			bool bUppercase;
		};

		const FFontToken& Token(EFont Font)
		{
			static const FFontToken Tokens[] = {
				{TEXT("Logo"), "Black", 67.5f, 180, true},              // 90 px em, ~64 px cap height
				{TEXT("Display"), "BoldCondensed", 42.0f, 0, false},    // 56 px
				{TEXT("Menu"), "Regular", 30.0f, 20, false},            // 40 px
				{TEXT("Title"), "BoldCondensed", 24.0f, 0, false},      // 32 px
				{TEXT("Body"), "Regular", 18.0f, 0, false},             // 24 px
				{TEXT("Label"), "Medium", 15.0f, 20, false},            // 20 px
				{TEXT("LabelCaps"), "BoldCondensed", 19.5f, 60, true},  // 26 px
				{TEXT("Caption"), "Regular", 15.0f, 0, false},          // 20 px
				{TEXT("Mono"), "Mono", 16.5f, 0, false},                // 22 px
				{TEXT("OverlayTitle"), "Bold", 16.0f, 0, false},        // 21.3 px
				{TEXT("OverlayLine"), "Regular", 13.0f, 0, false},      // 17.3 px em, body height ~20 px
				{TEXT("OverlaySmall"), "Bold", 12.0f, 20, false},       // 16 px em, body height ~18.75 px
				{TEXT("OverlayMono"), "Mono", 12.0f, 0, false},
			};
			static_assert(UE_ARRAY_COUNT(Tokens) == static_cast<int32>(EFont::Count), "one token per EFont");
			return Tokens[FMath::Clamp(static_cast<int32>(Font), 0, static_cast<int32>(EFont::Count) - 1)];
		}
	}

	FSlateFontInfo Font(EFont Token_)
	{
		const FFontToken& T = Token(Token_);
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(T.Face, T.Points);
		Info.LetterSpacing = T.LetterSpacing;
		return Info;
	}

	float FontPx(EFont Token_)
	{
		return Token(Token_).Points * 4.0f / 3.0f;
	}

	bool IsUppercase(EFont Token_)
	{
		return Token(Token_).bUppercase;
	}

	const TCHAR* FontName(EFont Token_)
	{
		return Token(Token_).Name;
	}

	const FSlateBrush* WhiteBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* RoundedBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 6.0f);
		return &Brush;
	}

	const FSlateBrush* KeycapBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, 5.0f, FLinearColor::White, 1.5f);
		return &Brush;
	}

	FCulturePtr NumberCulture()
	{
		const FCulturePtr English = FInternationalization::Get().GetCulture(TEXT("en"));
		return English.IsValid() ? English : FInternationalization::Get().GetCurrentCulture();
	}

	FText Number(double Value, int32 Decimals)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = FMath::Max(0, Decimals);
		Options.MaximumFractionalDigits = FMath::Max(0, Decimals);
		Options.UseGrouping = false;
		return FText::AsNumber(Value, &Options, NumberCulture());
	}
}
