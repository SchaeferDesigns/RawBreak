#pragma once

// Player controller (Docs/ue-architecture.md 6.4): creates the runtime input setup and registers its mapping
// context, owns the info overlay (URbOverlayComponent) and handles the non-pawn actions (glance, overlay toggles,
// replay, decision options). One controller for both hot-seat players. Cheat manager URbCheatManager for the
// headless functional tests (rb.* commands via -ExecCmds). Owner: UE-5b (overlay component: UE-7).

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "RbPlayerController.generated.h"

class URbInputSetup;
class URbOverlayComponent;
struct FInputActionValue;

UCLASS()
class RAWBREAK_API ARbPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARbPlayerController();

	const URbInputSetup* GetInputSetup() const { return InputSetup; }
	URbOverlayComponent* GetOverlay() const { return Overlay; }

	// APlayerController
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

protected:
	void OnGlance(const FInputActionValue& Value);
	void OnToggleOverlay(const FInputActionValue& Value);
	void OnToggleDebug(const FInputActionValue& Value);
	void OnReplay(const FInputActionValue& Value);
	void OnCycleOption(const FInputActionValue& Value);

	UPROPERTY(Transient)
	TObjectPtr<URbInputSetup> InputSetup;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|UI")
	TObjectPtr<URbOverlayComponent> Overlay;
};
