#pragma once

// Owns the info overlay on the local player's viewport and its visibility (Docs/ue-architecture.md 6.6):
//   Hidden (default)  nothing on screen - no HUD by default (decisions)
//   Glance            while the glance key is held: the overlay fades in, fades out on release (the "glance" concept:
//                     a quick look at the scoreboard; later a diegetic chalkboard / scoreboard in the venue)
//   Pinned            F1 toggles it permanently on
//   Debug block       F2 adds the physics facts of the last shot
//   Mandatory lines   shown in EVERY mode (rules obligations, review R-05): foul counter while > 0 incl. the two-foul warning,
//                     pending decision + options, ball in hand; after each shot a 4 s auto-glance shows the result
//                     (enforced foul + rule reference, rules.md 16 item 2)
// Refreshes its model from URbMatchDirector on OnMatchChanged. Lives on ARbPlayerController. Owner: UE-7; since M2 M2-D.
//
// M2-D additions:
//  * The director is the PLAYER's match only (URbUiSubsystem::FindPlayerDirector = the player session's, UX-T25); a different
//    player table rebinds within one tick.
//  * Key hints (SRbKeyHints, layer 15, lower left): rebuilt every 0.1 s from FRbKeyHintsModel::Build; shown for KeyHintHold
//    (3 s) after every context change, then faded out; hidden while a menu is open, the world is paused or a replay runs, and
//    when the setting is off. -RbUiScreen=KeyHints holds them (captures).
//  * Text sizes follow the UI tokens (RbUi::EFont::Overlay*; UX-T05: >= 18 px body height at 1080p).
//
// Details (UE-7):
//  * Mode: Pinned while pinned; else Glance while the glance key is held OR the post-shot auto-glance runs; else Hidden.
//    The full card fades toward its target (FadeInSeconds / FadeOutSeconds, real time, also while the world is paused);
//    the mandatory card never fades.
//  * Auto-glance: a newly committed shot (the director's GetLastCommittedShot changed) starts AutoGlanceSeconds of Glance.
//  * Mandatory lines beyond R-05: RackOver / MatchOver ("Enter: next rack / new match") - without them a player in Hidden
//    mode would face a table that waits for an input nobody told them about - and the replay tag while a replay runs.
//  * The widget exists only for a LOCAL player controller with a game viewport and a renderer (never under -nullrhi); the
//    model, the mode and the fade run without it (tests).
//  * The director is found lazily (it is created by ARbGameMode::StartPlay after the controller's BeginPlay); tests inject
//    one with SetDirector.
//  * Dev: console variable rb.Overlay.InScreenshots 1 makes screenshots requested WITHOUT UI (the headless capture,
//    rbue.py capture) include the overlay: when the viewport begins to draw (after every tick, before it processes the
//    request) the request is turned into a Slate window capture. For the UE-7 dev screenshots.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Replay/RbReplaySubsystem.h"
#include "UI/Live/SRbKeyHints.h"
#include "UI/SRbInfoOverlay.h"

#include "RbOverlayComponent.generated.h"

class SRbInfoOverlay;
class SRbKeyHints;
class URbMatchDirector;
class UGameViewportClient;

UENUM(BlueprintType)
enum class ERbOverlayMode : uint8
{
	Hidden,
	Glance,
	Pinned,
};

// Inputs of the model that do not come from the director.
struct FRbOverlayExtras
{
	bool bDebug = false;
	bool bReplaying = false;
	ERbReplayView ReplayView = ERbReplayView::Shooter;
	float ReplayRate = 1.0f;
	int32 ReplayIndexFromLast = 0;
	int32 RawInputMode = -1;      // F2: -1 no stroke component, 0 reconstructed report times, 1 true per-report timestamps (6.2.1)
	int32 LastRackWinner = -1;    // rules player who won the last rack (tracked by the component), -1 = derive from the last shot
};

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbOverlayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbOverlayComponent();

	void SetGlanceHeld(bool bHeld);
	void TogglePinned();
	void ToggleDebug();
	ERbOverlayMode GetMode() const { return Mode; }
	bool IsDebugShown() const { return bDebug; }

	// Rebuilds the model from the director (called on OnMatchChanged and after each shot).
	void Refresh();

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- additions (UE-7) ----------------------------------------------------------------------------------

	// The model for a director state (pure; nullptr = no match). Text rules in the .cpp header.
	static FRbOverlayModel BuildModel(const URbMatchDirector* Director, const FRbOverlayExtras& Extras);

	// Explicit mode (cheat RbOverlay): Hidden clears the pin (a held glance key still glances), Pinned pins.
	void SetPinned(bool bPin);
	void SetDebugShown(bool bShow);
	bool IsPinned() const { return bPinned; }
	bool IsGlanceHeld() const { return bGlanceHeld; }

	// Starts the post-shot auto-glance (Refresh does it for every newly committed shot).
	void StartAutoGlance();
	float GetAutoGlanceRemaining() const { return AutoGlanceRemaining; }

	// Full card opacity now / its target (1 while Pinned or Glance, else 0).
	float GetFullOpacity() const { return Opacity; }
	float GetTargetOpacity() const { return Mode == ERbOverlayMode::Hidden ? 0.0f : 1.0f; }
	// Mandatory lines are on screen (whatever the mode) whenever the model has any.
	bool AreMandatoryLinesShown() const { return Model.MandatoryLines.Num() > 0; }

	// Advances the fades and the auto-glance by DeltaSeconds (the tick calls it with real time; tests call it directly).
	void AdvanceOverlay(float DeltaSeconds);

	const FRbOverlayModel& GetModel() const { return Model; }
	TSharedPtr<SRbInfoOverlay> GetWidget() const { return Widget; }

	// Uses this director instead of the game mode's (world-free tests) and subscribes to it; nullptr unbinds.
	void SetDirector(URbMatchDirector* InDirector);

	UPROPERTY(EditAnywhere, Category = "RawBreak|UI")
	float AutoGlanceSeconds = 4.0f;

	UPROPERTY(EditAnywhere, Category = "RawBreak|UI")
	float FadeInSeconds = 0.12f;

	UPROPERTY(EditAnywhere, Category = "RawBreak|UI")
	float FadeOutSeconds = 0.35f;

	// --- key hints (M2-D) ----------------------------------------------------------------------------------------------------
	const FRbKeyHintsModel& GetKeyHints() const { return HintModel; }
	float GetKeyHintOpacity() const { return HintOpacity; }
	// Feeds a key-hint model and advances the hold / fade by DeltaSeconds (the tick does it from the controller; tests directly).
	void AdvanceKeyHints(const FRbKeyHintsModel& Model, float DeltaSeconds);
	TSharedPtr<SRbKeyHints> GetKeyHintsWidget() const { return HintsWidget; }

protected:
	URbMatchDirector* FindDirector() const;
	void UpdateVisibility();

	ERbOverlayMode Mode = ERbOverlayMode::Hidden;
	bool bGlanceHeld = false;
	bool bDebug = false;
	float Opacity = 0.0f;
	TSharedPtr<SRbInfoOverlay> Widget;
	FDelegateHandle MatchChangedHandle;

private:
	// Subscribes to the director once it exists (lazy: the game mode creates it after BeginPlay).
	void EnsureDirectorBound();
	void BindDirector(URbMatchDirector* InDirector);
	void UnbindDirector();
	void OnMatchChanged();
	void OnReplayChanged();
	FRbOverlayExtras MakeExtras() const;
	void CreateWidget();
	void RemoveWidget();
	void PushToWidget();
	void OnViewportBeginDraw();

	bool bPinned = false;
	float AutoGlanceRemaining = 0.0f;
	float RefreshCountdown = 0.0f;
	FRbOverlayModel Model;
	TWeakObjectPtr<URbMatchDirector> Director;
	bool bDirectorOverride = false;
	TWeakPtr<const FRbShot> LastSeenShot; // the director's last committed shot at the last refresh (auto-glance trigger)
	int32 SeenRackWins[2] = {0, 0};       // rack wins at the last refresh: the one that grew won the last rack
	int32 LastRackWinner = -1;
	TWeakObjectPtr<URbReplaySubsystem> ReplaySubsystem;
	FDelegateHandle ReplayChangedHandle;
	TWeakObjectPtr<UGameViewportClient> WidgetViewport;
	FDelegateHandle BeginDrawHandle;

	// Key hints (M2-D).
	TSharedPtr<SRbKeyHints> HintsWidget;
	FRbKeyHintsModel HintModel;
	FString HintKey;
	float HintHold = 0.0f;
	float HintOpacity = 0.0f;
	float HintRefreshCountdown = 0.0f;
};
