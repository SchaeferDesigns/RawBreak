#pragma once

// World interactions of the first-person player that are not the stroke (Docs/ue-architecture.md 18.6): M2 = picking up a ball
// that left the table (decisions 2026-09-28, ui-ux 2.4 / 3.5 "[F] Pick up the ball"); later the coin slide, the cue rack, the
// triangle, chalk (HF-70..79 chores). Providers register an Offer + Interact pair; the pawn's Confirm (F / Enter) asks
// TryInteract FIRST (after the ball-in-hand placement, before the director's Confirm), so a gazed interactable within reach
// consumes the press. A provider decides itself whether the gaze ray and the eye position qualify (reach 1.2 m and gaze of
// ui-ux 3.5). The key-hint layer (M2-D) asks FindInteraction every frame for the prompt verb ("Pick up the ball").
// Owner: M2-E (dispatch implemented by the M2 architect step; ARbPlayerCharacter::HandleConfirm already calls TryInteract).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "RbInteractionSubsystem.generated.h"

class APawn;

// What the player looks at: the pawn, the eye (world) and the unit gaze direction (world).
struct FRbInteractionQuery
{
	APawn* Pawn = nullptr;
	FVector Eye = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
};

struct FRbInteractionProvider
{
	// True when this provider offers an interaction for the query now (possible AND meaningful, ui-ux 3.5); OutVerb = the
	// prompt's verb without the key ("Pick up the ball"). Must be cheap (called every frame by the key hints).
	TFunction<bool(const FRbInteractionQuery& /*Query*/, FText& /*OutVerb*/)> Offer;
	// Performs it; true = the press was consumed.
	TFunction<bool(const FRbInteractionQuery& /*Query*/)> Interact;
};

UCLASS()
class RAWBREAK_API URbInteractionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static URbInteractionSubsystem* Get(const UObject* WorldContext);

	// Registers a provider; the handle removes it again.
	FDelegateHandle AddProvider(FRbInteractionProvider Provider);
	void RemoveProvider(FDelegateHandle Handle);

	// The first provider (registration order) that offers an interaction; false = nothing to do here.
	bool FindInteraction(const FRbInteractionQuery& Query, FText& OutVerb) const;

	// Asks the providers in registration order (the first one that offers it performs it); true = consumed.
	bool TryInteract(const FRbInteractionQuery& Query);
	bool TryInteract(APawn& Pawn, const FVector& Eye, const FVector& Direction);

	int32 GetNumProviders() const { return Providers.Num(); }

private:
	TArray<TPair<FDelegateHandle, FRbInteractionProvider>> Providers;
};
