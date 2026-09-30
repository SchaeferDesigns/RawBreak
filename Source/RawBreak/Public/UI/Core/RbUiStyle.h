#pragma once

// Visual tokens of the flat UI (ui-ux 4, "after-hours documentary": chalk-white type on smoke-black, one amber accent), the
// single source for every RAW BREAK Slate screen and the M1 overlay. Sizes are Slate units at 1080p: the engine's DPI rule
// (URbDpiScalingRule, height / 1080) is the ONLY scale applied to screen-space widgets (ui-ux 3.2, UX-T26) - nothing here
// multiplies by the viewport again.
//
// Fonts: the engine's Roboto family (Engine/Content/Slate/Fonts, shipped with the engine, no extra licence) stands in for
// IBM Plex until the font fetch of ui-ux 3.7 exists. FSlateFontInfo::Size is in points at 96 DPI: px = pt * 4 / 3 (the table of
// ui-ux 4.2 lists px at 1080p). Minimum text size (XAG 101 / UX-T05): the body height (ascender - descender) of every mixed-case
// style >= 18 px at 1080p; uppercase only in LabelCaps (26 px em -> ~18.5 px cap height). Owner: M2-D.

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"

namespace RbUi
{
	// --- colour tokens (ui-ux 4.1; sRGB hex -> linear) --------------------------------------------------------------------
	enum class EColor : uint8
	{
		Ink900,         // #0E0F10 base surface, scrims
		Ink800,         // #1A1C1E raised panel
		Ink700,         // #26292C focused row background
		Line600,        // #3A3E42 dividers
		Chalk100,       // #EDEBE6 primary text
		Chalk300,       // #B8B4AA secondary text
		Chalk500,       // #8A867D disabled
		Amber400,       // #FFB02E focus bar, selection, slider fill
		SignalFoul,     // #FF5A4F non-text marks only
		SignalFoulText, // #FF8A80 foul text on opaque surfaces / scrim.menu
		SignalOk,       // #5BD98A
		SignalInfo,     // #7FB8FF
		Count,
	};

	RAWBREAK_API FColor Srgb(EColor Token);
	RAWBREAK_API FLinearColor Color(EColor Token, float Alpha = 1.0f);

	// Scrim opacities of ink.900 (ui-ux 4.1): menus 0.96 (+ background blur 16), cards 0.88.
	inline constexpr float ScrimMenu = 0.96f;
	inline constexpr float ScrimCard = 0.88f;
	inline constexpr float MenuBlurStrength = 16.0f;

	// WCAG 2.x contrast ratio of two opaque sRGB colours, and the colour a scrim of Opacity over pure white shows (the worst case
	// behind a menu, blended in linear light).
	RAWBREAK_API double ContrastRatio(const FColor& A, const FColor& B);
	RAWBREAK_API FColor ScrimOverWhite(EColor Scrim, float Opacity);

	// --- typography tokens (ui-ux 4.2) -------------------------------------------------------------------------------------
	enum class EFont : uint8
	{
		Logo,          // RAW BREAK logotype (Black, 64 px cap height)
		Display,       // screen titles, 56 px
		Menu,          // main-menu / pause items, 40 px
		Title,         // section titles, tabs, card headers, 32 px
		Body,          // rows, descriptions, dialogs, 24 px
		Label,         // tags, column heads, key hints, 20 px
		LabelCaps,     // UPPERCASE labels (style), 26 px
		Caption,       // minimum text, 20 px
		Mono,          // numbers, values, 22 px
		OverlayTitle,  // info overlay (M1 card): title
		OverlayLine,   // info overlay: lines, mandatory lines
		OverlaySmall,  // info overlay: section header (bold)
		OverlayMono,   // info overlay: F2 debug block
		Count,
	};

	RAWBREAK_API FSlateFontInfo Font(EFont Token);
	// em size [px at 1080p] of a token (Size * 4 / 3).
	RAWBREAK_API float FontPx(EFont Token);
	// True for the tokens drawn in uppercase (their measured height is the cap height, UX-T05).
	RAWBREAK_API bool IsUppercase(EFont Token);
	RAWBREAK_API const TCHAR* FontName(EFont Token);

	// --- spacing (ui-ux 4.3; Slate units at 1080p) -------------------------------------------------------------------------
	inline constexpr float SafeX = 96.0f;       // 5 % title-safe margin (menus)
	inline constexpr float SafeY = 54.0f;
	inline constexpr float LiveSafeX = 67.0f;   // 3.5 % (live elements: key hints)
	inline constexpr float LiveSafeY = 38.0f;
	inline constexpr float RowHeight = 56.0f;
	inline constexpr float FocusBarWidth = 4.0f;
	inline constexpr float DescriptionWidth = 440.0f;
	inline constexpr float ContentMaxWidth = 1280.0f;
	inline constexpr float GlyphHeight = 30.0f; // key caps (ui-ux 3.5)

	// --- motion (ui-ux 4.5; seconds) ---------------------------------------------------------------------------------------
	inline constexpr float FadeIn = 0.12f;
	inline constexpr float FadeOut = 0.18f;
	inline constexpr float Push = 0.22f;
	inline constexpr float PushSlidePx = 24.0f;
	inline constexpr float PushReduced = 0.12f; // Reduced motion: fade only
	inline constexpr float TitleIntroDelay = 2.5f; // clean first seconds (ui-ux 6.3)
	inline constexpr float TitleIntroFade = 0.4f;
	inline constexpr float KeyHintHold = 3.0f;

	// --- brushes (static, resource-free) -----------------------------------------------------------------------------------
	RAWBREAK_API const FSlateBrush* WhiteBrush();
	RAWBREAK_API const FSlateBrush* RoundedBrush(); // white, 6 px corner radius
	RAWBREAK_API const FSlateBrush* KeycapBrush();  // outlined rounded box (1.5 px chalk outline, transparent fill)

	// Tag of the text blocks that wrap (AutoWrapText): the no-clipping check (UX-T07) measures only single-line text against its
	// allotted width.
	inline const FName WrapTag(TEXT("RbWrap"));

	// Ease-out cubic (0..1).
	inline float EaseOut(float T) { T = FMath::Clamp(T, 0.0f, 1.0f); const float U = 1.0f - T; return 1.0f - U * U * U; }

	// The culture numbers in the UI are formatted with: the text language's. M2 is EN only (Docs/ue-architecture.md 18.0), so
	// numbers read "12.5 cm" next to English words also on a German Windows (the OS locale would give "12,5"); the language
	// setting of ui-ux 13.11 replaces it later. Falls back to the current culture when "en" is unknown.
	RAWBREAK_API FCulturePtr NumberCulture();
	// FText::AsNumber in NumberCulture() with exactly Decimals fractional digits and no grouping.
	RAWBREAK_API FText Number(double Value, int32 Decimals = 0);
}
