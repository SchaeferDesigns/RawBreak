#pragma once

// Minimal code-only info / debug overlay (Slate, no widget assets; Docs/ue-architecture.md 6.6). Decisions:
// no HUD by default, information is diegetic or glanced at; this overlay is the M1 stand-in for the diegetic
// scoreboard. It renders an FRbOverlayModel (plain text blocks) in a corner, small and unobtrusive. Owner: UE-7.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

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
};

class RAWBREAK_API SRbInfoOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbInfoOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Replaces the content (game thread).
	void SetModel(const FRbOverlayModel& InModel);

private:
	FText GetText() const;

	FRbOverlayModel Model;
};
