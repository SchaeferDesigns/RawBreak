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
//                         replay runs: next view. Esc (a legacy key binding of this controller, not in the mapping context, so
//                         no key is bound twice there) stops the replay.
//   CycleOption (Q / E)   the director's highlighted decision option -1 / +1
// Mouse: captured, hidden, game-only input (the raw-input thread of the stroke needs the high-precision mouse mode).

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "RbPlayerController.generated.h"

class URbInputSetup;
class URbMatchDirector;
class URbOverlayComponent;
class URbReplaySubsystem;
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

	// APlayerController
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

protected:
	void OnGlance(const FInputActionValue& Value);
	void OnToggleOverlay(const FInputActionValue& Value);
	void OnToggleDebug(const FInputActionValue& Value);
	void OnReplay(const FInputActionValue& Value);
	void OnCycleOption(const FInputActionValue& Value);

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
