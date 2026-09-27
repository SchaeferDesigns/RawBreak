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
// Refreshes its model from URbMatchDirector on OnMatchChanged. Lives on ARbPlayerController. Owner: UE-7.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "RbOverlayComponent.generated.h"

class SRbInfoOverlay;
class URbMatchDirector;

UENUM(BlueprintType)
enum class ERbOverlayMode : uint8
{
	Hidden,
	Glance,
	Pinned,
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

protected:
	URbMatchDirector* FindDirector() const;
	void UpdateVisibility();

	ERbOverlayMode Mode = ERbOverlayMode::Hidden;
	bool bGlanceHeld = false;
	bool bDebug = false;
	float Opacity = 0.0f;
	TSharedPtr<SRbInfoOverlay> Widget;
	FDelegateHandle MatchChangedHandle;
};
