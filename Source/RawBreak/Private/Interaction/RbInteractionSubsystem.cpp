#include "Interaction/RbInteractionSubsystem.h"

#include "Engine/World.h"

// Owner: M2-E. The dispatch is complete; M2-E registers the loose-ball pick-up provider (URbLooseBallSubsystem).

URbInteractionSubsystem* URbInteractionSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbInteractionSubsystem>() : nullptr;
}

FDelegateHandle URbInteractionSubsystem::AddProvider(FRbInteractionProvider Provider)
{
	const FDelegateHandle Handle(FDelegateHandle::GenerateNewHandle);
	Providers.Emplace(Handle, MoveTemp(Provider));
	return Handle;
}

void URbInteractionSubsystem::RemoveProvider(FDelegateHandle Handle)
{
	Providers.RemoveAll([Handle](const TPair<FDelegateHandle, FRbInteractionProvider>& Entry) { return Entry.Key == Handle; });
}

bool URbInteractionSubsystem::FindInteraction(const FRbInteractionQuery& Query, FText& OutVerb) const
{
	for (const TPair<FDelegateHandle, FRbInteractionProvider>& Entry : Providers)
	{
		if (Entry.Value.Offer && Entry.Value.Offer(Query, OutVerb))
		{
			return true;
		}
	}
	return false;
}

bool URbInteractionSubsystem::TryInteract(const FRbInteractionQuery& Query)
{
	// Copy: a provider may add / remove providers while it runs.
	const TArray<TPair<FDelegateHandle, FRbInteractionProvider>> Snapshot = Providers;
	for (const TPair<FDelegateHandle, FRbInteractionProvider>& Entry : Snapshot)
	{
		FText Verb;
		if (Entry.Value.Offer && Entry.Value.Interact && Entry.Value.Offer(Query, Verb) && Entry.Value.Interact(Query))
		{
			return true;
		}
	}
	return false;
}

bool URbInteractionSubsystem::TryInteract(APawn& Pawn, const FVector& Eye, const FVector& Direction)
{
	FRbInteractionQuery Query;
	Query.Pawn = &Pawn;
	Query.Eye = Eye;
	Query.Direction = Direction;
	return TryInteract(Query);
}
