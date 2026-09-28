#pragma once

// Minimal code-only info / debug overlay (Slate, no widget assets; Docs/ue-architecture.md 6.6). Decisions:
// no HUD by default, information is diegetic or glanced at; this overlay is the M1 stand-in for the diegetic
// scoreboard. It renders an FRbOverlayModel (plain text blocks) in a corner, small and unobtrusive. Owner: UE-7.
//
// Layout (UE-7): one column in the top-left corner (safe margin), two cards on a subtle translucent background:
//   mandatory card   the MandatoryLines, shown in EVERY mode whenever there is one (rules obligations, review R-05) - it
//                    never moves, so the full card fading in below it cannot make it jump
//   full card        title, subtitle (score), match lines, last shot (its first line, in the Dim tone, is the section header),
//                    debug block (F2, monospace); faded by SetFullOpacity (glance, pinned, post-shot auto-glance) and collapsed
//                    while invisible
// Sizes are Slate units: the game viewport's DPI scale (the engine's DPI curve on the shortest side, 1.0 at 1080p, 2.0 at
// 2160p) keeps it readable from 1080p to 4K. Colour-blind safe (plan 12.22): tones use the Okabe-Ito palette (orange warnings,
// sky-blue prompts) and never carry meaning alone - warnings are also bold and worded ("FOUL", "TWO FOULS"), every line has a
// leading accent bar, and balls are always named by NUMBER, never by colour. Hit-test invisible: it never takes the mouse.

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class SVerticalBox;

// Tone of one overlay line (colour + weight of the text and its accent bar).
enum class ERbOverlayTone : uint8
{
	Normal,   // plain information
	Dim,      // section headers, secondary facts
	Info,     // prompts that wait for the player (ball in hand, decision, confirm, replay)
	Warning,  // fouls and the two-foul warning (Reg 8)
	Good,     // pocketed / rack won (positive result)
};

// What the overlay shows (filled by URbOverlayComponent from the director).
struct FRbOverlayModel
{
	FText Title;                 // e.g. "9-BALL  Practice"  /  "Player 1  3 : 2  Player 2  (race to 5)"
	TArray<FText> MatchLines;    // shooter, fouls (two-foul warning), called ball, ball in hand, decision prompt
	TArray<FText> LastShotLines; // pocketed, fouls, first contact, rule reference
	TArray<FText> DebugLines;    // physics facts: tip speed, miscue, sim time, event count, raw-input timestamp mode (F2)
	// Shown even in Hidden mode (review R-05, rules.md 16 item 12 "always display the foul counter - the display is the
	// mandatory warning [Reg 8]"): the shooter's consecutive fouls while > 0 (with the two-foul warning), a pending decision
	// and its options, and ball in hand for the incoming shooter. One compact line each; the M1 stand-in for the diegetic
	// scoreboard / referee call.
	TArray<FText> MandatoryLines;
	bool bShowDebug = false;

	// --- additions (UE-7) ----------------------------------------------------------------------------------
	FText Subtitle;                          // score / race line under the title (hot-seat), rack number
	// Tones parallel to the line arrays above (a missing entry = Normal; mandatory lines default to Warning).
	TArray<ERbOverlayTone> MatchTones;
	TArray<ERbOverlayTone> LastShotTones;
	TArray<ERbOverlayTone> MandatoryTones;

	// Every text of the model in display order, one line each ("[M] " / "[T] " / "[S] " / "[L] " / "[D] " prefixes for
	// mandatory / title / match / last shot / debug): tests, logs and the change detection of SetModel.
	FString ToDebugString() const;

	ERbOverlayTone MandatoryTone(int32 Index) const { return MandatoryTones.IsValidIndex(Index) ? MandatoryTones[Index] : ERbOverlayTone::Warning; }
	ERbOverlayTone MatchTone(int32 Index) const { return MatchTones.IsValidIndex(Index) ? MatchTones[Index] : ERbOverlayTone::Normal; }
	ERbOverlayTone LastShotTone(int32 Index) const { return LastShotTones.IsValidIndex(Index) ? LastShotTones[Index] : ERbOverlayTone::Normal; }
};

class RAWBREAK_API SRbInfoOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbInfoOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Replaces the content (game thread). Rebuilds the text widgets only when the text changed.
	void SetModel(const FRbOverlayModel& InModel);

	// --- additions (UE-7) ----------------------------------------------------------------------------------

	// Opacity of the full card [0, 1] (glance fade); 0 collapses it. The mandatory card is not affected.
	void SetFullOpacity(float InOpacity);
	float GetFullOpacity() const { return FullOpacity; }

	const FRbOverlayModel& GetModel() const { return Model; }

	// What is on screen (tests): the mandatory card whenever the model has mandatory lines, the full card while its
	// opacity is above zero.
	bool IsMandatoryCardVisible() const;
	bool IsFullCardVisible() const;

	// Colour of a tone (linear, Okabe-Ito sRGB values).
	static FLinearColor ToneColor(ERbOverlayTone Tone);

private:
	FText GetText() const;
	void RebuildMandatory();
	void RebuildFull();
	void UpdateVisibility();

	FRbOverlayModel Model;
	FString ModelKey;            // ToDebugString of the shown model (+ debug flag): skip identical rebuilds
	float FullOpacity = 0.0f;

	TSharedPtr<SBorder> MandatoryCard;
	TSharedPtr<SVerticalBox> MandatoryBox;
	TSharedPtr<SBorder> FullCard;
	TSharedPtr<SVerticalBox> FullBox;
	FSlateBrush CardBrush;
	FSlateBrush BarBrush;
};
