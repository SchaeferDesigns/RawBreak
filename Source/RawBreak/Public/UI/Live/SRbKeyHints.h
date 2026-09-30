#pragma once

// Contextual key hints (M2 stand-in for the ui-ux 3.5 prompts; setting "Key hints", URbGameUserSettings::bShowKeyHints): one
// small plate in the lower left (3.5 % safe margin), "[key] Verb" pairs for what is possible NOW, faded out after 3 s without a
// context change, never over the table bed, hidden while a menu is open or a replay runs. Sources (read-only, public APIs of
// other packages; nothing is added to their files):
//   URbStrokeComponent::GetPhase (M2-F)          Walking: [RMB] Get down; Down: [LMB] Stroke  [Space] Commit  [Shift] Fine aim
//                                                [Ctrl] Settle  [Wheel] Elevation  [Arrows] English  [RMB] Stand up;
//                                                Watching: [RMB] Stand up
//   URbBallInHandComponent::GetState (M2-F)      Carrying: [LMB / F] Place the cue ball  [Shift] Fine; Refused: "Not here"
//   URbInteractionSubsystem::FindInteraction     [F] <verb> (e.g. "Pick up the ball", M2-E)
//   URbMatchDirector phase (the player's match)  decision: [Q / E] Choose  [Enter] Confirm; rack / match over: [Enter] Next rack
// The key labels come from the controller's mapping context (URbInputSetup), so a rebinding shows up. Built from
// FRbKeyHintsModel so tests check the text without Slate. The owner (URbOverlayComponent) runs the hold / fade. Owner: M2-D.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Game/RbMatchDirector.h"
#include "Player/RbBallInHandComponent.h"
#include "Player/RbStrokeComponent.h"

class APlayerController;
class SWrapBox;
class STextBlock;
class UInputAction;
class URbInputSetup;

struct FRbKeyHint
{
	FText Key;  // "RMB", "Space", "F"
	FText Verb; // "Get down"
};

// Everything the hints depend on (MakeContext reads it from a local player; tests fill it by hand).
struct RAWBREAK_API FRbKeyHintContext
{
	bool bShowKeyHints = true;
	bool bMenuOpen = false;
	bool bReplaying = false;
	bool bHasStroke = false;
	ERbStrokePhase StrokePhase = ERbStrokePhase::Locked;
	bool bHasBallInHand = false;
	ERbBallInHandState BallInHand = ERbBallInHandState::Inactive;
	bool bHasDirector = false;
	ERbDirectorPhase DirectorPhase = ERbDirectorPhase::Idle;
	bool bReplayAllowed = false;
	FText InteractionVerb;          // empty = nothing to interact with
	TMap<FName, FText> Keys;        // action name (GetDown, Stroke, Commit, FineAim, Settle, Elevation, TipOffset, Confirm,
	                                // CycleOption, Replay) -> key label; missing = the M1 default

	FText KeyFor(FName Action) const;
};

struct RAWBREAK_API FRbKeyHintsModel
{
	TArray<FRbKeyHint> Hints;
	FText Note; // a short line without a key ("Not here - move the ball")

	bool IsEmpty() const { return Hints.Num() == 0 && Note.IsEmpty(); }
	// "[RMB] Get down  [F] Pick up the ball | note" (tests, change detection).
	FString ToDebugString() const;

	// The hints for a local player's current context (empty when the setting is off or nothing applies).
	static FRbKeyHintsModel Build(const APlayerController* Controller);
	static FRbKeyHintsModel BuildFromContext(const FRbKeyHintContext& Context);
	static FRbKeyHintContext MakeContext(const APlayerController* Controller);

	// Short key names ("LMB", "Space", "Shift", "Wheel", "Arrows") and the keys an action is mapped to ("Enter / F").
	static FText KeyLabel(const FKey& Key);
	static FText KeysFor(const URbInputSetup* Setup, const UInputAction* Action);
};

class RAWBREAK_API SRbKeyHints : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbKeyHints) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetModel(const FRbKeyHintsModel& InModel);
	const FRbKeyHintsModel& GetModel() const { return Model; }

private:
	FRbKeyHintsModel Model;
	FString ModelKey;
	TSharedPtr<SWrapBox> Box;
	TSharedPtr<SWidget> Plate;
};
