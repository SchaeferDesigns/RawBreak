#pragma once

// Contextual key hints (M2 stand-in for the ui-ux 3.5 prompts; setting "Key hints", URbGameUserSettings::bShowKeyHints): one
// small line in the lower left, "[glyph] Verb" pairs for what is possible NOW, faded out after 3 s without a context change,
// never over the table bed. Sources (read-only, public APIs of other packages):
//   URbStrokeComponent::GetPhase (M2-F)          Walking: [RMB] Get down; Down: [LMB] Stroke  [Space] Commit  [Shift] Fine aim
//                                                [Ctrl] Settle  [Wheel] Elevation  [Arrows] English  [RMB] Stand up
//   URbBallInHandComponent::GetState (M2-F)      Carrying: [LMB / F] Set down  [Shift] Fine; Refused: "Not here"
//   URbInteractionSubsystem::FindInteraction     [F] <verb> (e.g. "Pick up the ball", M2-E)
//   URbMatchDirector phase / decision (M2-E)     [Q / E] Choose  [F] Confirm
// Built from FRbKeyHintsModel so tests check the text without Slate. Owner: M2-D (stub by the M2 architect step; TODO(M2-D)).

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class APlayerController;

struct FRbKeyHint
{
	FText Key;  // "RMB", "Space", "F"
	FText Verb; // "Get down"
};

struct FRbKeyHintsModel
{
	TArray<FRbKeyHint> Hints;

	// The hints for a local player's current context (empty when the setting is off or nothing applies).
	static FRbKeyHintsModel Build(const APlayerController* Controller);
};

class RAWBREAK_API SRbKeyHints : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbKeyHints) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetModel(const FRbKeyHintsModel& InModel);

private:
	FRbKeyHintsModel Model;
};
