#pragma once

// Small Slate building blocks of the RAW BREAK menus (ui-ux 3.2 widget list, M2 subset). All visual state comes from
// attributes; none of them decides anything - the owning SRbScreen keeps the focus list and performs the actions. Mouse input is
// handled by the item itself (hover -> OnHovered, press + release inside -> OnClicked; the settings row maps the click position
// to its arrows / slider), so the screens are mouse-only operable (UX-T09). Owner: M2-D.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

#include "UI/Core/RbUiStyle.h"

struct FRbSettingDef;
class URbGameUserSettings;

namespace RbUi
{
	// True when a pointer event carries real cursor motion. Slate also sends OnMouseEnter when a widget moves under a still cursor
	// (a list scrolls, a screen opens under the cursor: synthetic moves with zero delta); hover-to-focus only follows the hand, so a
	// keyboard user's focus is never pulled to whatever the parked cursor happens to cover.
	RAWBREAK_API bool IsPointerMotion(const FPointerEvent& MouseEvent);
}

// A horizontal gradient of ink.900 (the title's and the pause menu's left-side scrims): Stops = (x fraction, opacity).
class RAWBREAK_API SRbGradient : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRbGradient) {}
		SLATE_ARGUMENT(TArray<FVector2D>, Stops)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

private:
	TArray<FVector2D> Stops;
};

// A key cap "[F]" (ui-ux 3.5: 30 px high at 1080p, label >= 18 px) - outlined, chalk text.
class RAWBREAK_API SRbKeycap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbKeycap) {}
		SLATE_ATTRIBUTE(FText, Key)
		SLATE_ATTRIBUTE(FSlateColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

// A menu entry: focus bar + label (+ detail). Focus = the 4 px amber bar and chalk.100 (ui-ux 6.3); unfocused chalk.300;
// disabled chalk.500 (still focusable to show why). Hover moves the focus only when the cursor itself moves (IsPointerMotion).
class RAWBREAK_API SRbMenuItem : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbMenuItem)
		: _Font(RbUi::EFont::Menu)
		, _bEnabled(true)
		, _bPanel(false)
		{}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(FText, Detail)
		SLATE_ARGUMENT(RbUi::EFont, Font)
		SLATE_ARGUMENT(bool, bEnabled)
		SLATE_ARGUMENT(bool, bPanel)      // dialog buttons: ink.700 background while focused
		SLATE_ATTRIBUTE(bool, IsFocused)
		SLATE_EVENT(FSimpleDelegate, OnHovered)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	bool IsItemEnabled() const { return bEnabled; }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	// The focus is drawn by the item itself (amber bar), never Slate's keyboard focus rectangle.
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	FSlateColor LabelColor() const;
	TAttribute<bool> IsFocused;
	FSimpleDelegate OnHovered;
	FSimpleDelegate OnClicked;
	bool bEnabled = true;
	bool bPressed = false;
};

// A clickable footer hint "[Esc] Back" (mouse-only path to every keyboard action of a screen).
class RAWBREAK_API SRbHintButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbHintButton) {}
		SLATE_ARGUMENT(FText, Key)
		SLATE_ATTRIBUTE(FText, Verb)       // may follow the screen's state (the title's Esc: Back / Quit)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FSlateColor VerbColor() const;
	FSimpleDelegate OnClicked;
	bool bPressed = false;
};

// A tab of the settings page strip: title text, amber underline while active.
class RAWBREAK_API SRbTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbTab) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ATTRIBUTE(bool, IsActive)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TAttribute<bool> IsActive;
	FSimpleDelegate OnClicked;
};

// The slider bar of a range row: track (line.600), fill (amber), default tick, thumb.
class RAWBREAK_API SRbSliderBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRbSliderBar) {}
		SLATE_ATTRIBUTE(float, Fraction)
		SLATE_ATTRIBUTE(float, DefaultFraction)
		SLATE_ATTRIBUTE(bool, BarEnabled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

private:
	TAttribute<float> Fraction;
	TAttribute<float> DefaultFraction;
	TAttribute<bool> IsBarEnabled;
};

DECLARE_DELEGATE_OneParam(FRbOnRowStep, int32 /*Direction*/);
DECLARE_DELEGATE_OneParam(FRbOnRowSetFraction, float /*Fraction 0..1*/);

// One settings row (ui-ux 13.1 SRbOptionRow): label | control. Enum / toggle rows: "<  value  >" (click an arrow = one step,
// clamped; a click on the value = OnActivate, the screen's Enter: the next value, wrapping - else the last value of a list would be
// a dead click); range rows: slider bar (click / drag = that fraction) + value text. The row maps the pointer to its
// zones from its own geometry (the control column is the rightmost ControlWidth units), so tests can click it without a window.
// A drag ends on the button release, on a move without the left button and when the mouse capture is lost (Alt-Tab, a dialog):
// a lost release never leaves the slider following the bare cursor.
class RAWBREAK_API SRbOptionRow : public SCompoundWidget
{
public:
	static constexpr float ControlWidth = 640.0f;
	static constexpr float ArrowWidth = 48.0f;
	static constexpr float TrackWidth = 240.0f;
	static constexpr float TrackGap = 24.0f;

	SLATE_BEGIN_ARGS(SRbOptionRow) {}
		SLATE_ARGUMENT(const FRbSettingDef*, Row)
		SLATE_ARGUMENT(TFunction<URbGameUserSettings*()>, GetSettings)
		SLATE_ATTRIBUTE(bool, IsFocused)
		SLATE_EVENT(FSimpleDelegate, OnHovered)
		SLATE_EVENT(FRbOnRowStep, OnStep)
		SLATE_EVENT(FRbOnRowSetFraction, OnSetFraction)
		SLATE_EVENT(FSimpleDelegate, OnActivate) // a click on an enum / toggle value (unbound: one step up)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	const FRbSettingDef* GetRow() const { return Row; }

	// Where a local x position falls (tests use it to aim their clicks).
	enum class EZone : uint8 { Label, Decrement, Value, Increment, Track };
	EZone ZoneAt(float LocalX, float Width) const;
	// Fraction of the slider track at a local x.
	static float TrackFraction(float LocalX, float Width);

	FText GetValueText() const;
	bool IsRowEnabled() const;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	bool IsDragging() const { return bDragging; }

private:
	float ValueFraction() const;
	float DefaultFraction() const;
	FSlateColor TextColor(bool bLabel) const;

	const FRbSettingDef* Row = nullptr;
	TFunction<URbGameUserSettings*()> GetSettings;
	TAttribute<bool> IsFocused;
	FSimpleDelegate OnHovered;
	FRbOnRowStep OnStep;
	FRbOnRowSetFraction OnSetFraction;
	FSimpleDelegate OnActivate;
	bool bDragging = false;
};
