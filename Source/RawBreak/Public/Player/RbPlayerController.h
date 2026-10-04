#pragma once

// Player controller (Docs/ue-architecture.md 6.4): creates the runtime input setup and registers its mapping
// context, owns the info overlay (URbOverlayComponent) and handles the non-pawn actions (glance, overlay toggles,
// replay, decision options). One controller for both hot-seat players. Cheat manager URbCheatManager for the
// headless functional tests (rb.* commands via -ExecCmds). Owner: UE-5b (overlay component: UE-7).
//
// Non-pawn actions:
//   Glance (Tab held)     overlay glance while held (Triggered = held, Completed = released)
//   ToggleOverlay (F1)    pin / unpin the overlay;  ToggleDebug (F2): physics debug block
//   Replay (R)            replay the last shot from the shooter's view (only when URbMatchDirector::IsReplayAllowed); while a
//                         replay runs: next view.
//   Pause (Esc)           M2: stops a running replay, else URbUiSubsystem::TogglePauseMenu (M2-D). Replaces M1's legacy Esc key
//                         binding (the Pause action of URbInputSetup triggers while the game is paused).
//   CycleOption (Q / E)   the director's highlighted decision option -1 / +1
// Mouse: captured, hidden, game-only input (the raw-input thread of the stroke needs the high-precision mouse mode); the mouse axes
// are neutral (M2-F, P3: raw counts in the Look action, NeutraliseMouseAxes). Pausing: the pause menu (M2-D) uses SetPause /
// SetInputMode from its own code; the stroke component notices the paused world and drops a held stroke (18.2).

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "RbPlayerController.generated.h"

class URbInputSetup;
class URbMatchDirector;
class URbOverlayComponent;
class URbReplaySubsystem;
class UPlayerInput;
struct FInputActionValue;

UCLASS()
class RAWBREAK_API ARbPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARbPlayerController();

	const URbInputSetup* GetInputSetup() const { return InputSetup; }
	URbOverlayComponent* GetOverlay() const { return Overlay; }

	// Action handlers (the Enhanced Input bindings call these; tests and dev tools call them directly).
	void HandleGlance(bool bHeld);
	void HandleToggleOverlay();
	void HandleToggleDebug();
	// Returns true if a replay started or its view changed.
	bool HandleReplay();
	void HandleReplayBack();
	// Returns true if the director moved its highlighted option.
	bool HandleCycleOption(float Direction);
	// Esc: replay back, else the pause menu (M2).
	void HandlePause();

	// M2-F (P3): sets the mouse axes of Input to neutral (sensitivity 1, no dead zone / exponent / invert), so the Look action carries
	// raw counts. True if something changed. RegisterMappingContext calls it before the mapping context is added.
	static bool NeutraliseMouseAxes(UPlayerInput* Input);

	// APlayerController
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

protected:
	void OnGlance(const FInputActionValue& Value);
	void OnToggleOverlay(const FInputActionValue& Value);
	void OnToggleDebug(const FInputActionValue& Value);
	void OnReplay(const FInputActionValue& Value);
	void OnCycleOption(const FInputActionValue& Value);
	void OnPause(const FInputActionValue& Value);

	// Adds the input setup's mapping context to the local player's Enhanced Input subsystem (once). Called from BeginPlay AND
	// SetupInputComponent: a controller spawned into a world that already began play runs BeginPlay before SetPlayer ->
	// InitInputSystem creates the setup, and must still get its mapping context.
	void RegisterMappingContext();

	URbMatchDirector* FindDirector() const;
	URbReplaySubsystem* FindReplay() const;

	UPROPERTY(Transient)
	TObjectPtr<URbInputSetup> InputSetup;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|UI")
	TObjectPtr<URbOverlayComponent> Overlay;
};
