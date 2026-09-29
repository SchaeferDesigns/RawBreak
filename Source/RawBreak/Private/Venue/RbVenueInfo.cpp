#include "Venue/RbVenueInfo.h"

#include "Core/RbAssetPaths.h"

#include "Engine/World.h"
#include "EngineUtils.h"

// Owner: M2-A. Stub of the M2 architect step: identity queries work; lighting states and the validator are TODO(M2-A).

ARbVenueInfo::ARbVenueInfo()
{
	Tags.Add(RbAssetPaths::Tag::VenueInfo);
}

ARbVenueInfo* ARbVenueInfo::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ARbVenueInfo> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

ERbVenue ARbVenueInfo::GetVenue(const UObject* WorldContext)
{
	const ARbVenueInfo* Info = Find(WorldContext);
	return Info ? Info->Venue : ERbVenue::TestRoom;
}

void ARbVenueInfo::BeginPlay()
{
	Super::BeginPlay();
	LightingState = InitialLightingState;
}

void ARbVenueInfo::SetLightingState(ERbLightingState NewState, float /*RampSeconds*/)
{
	// TODO(M2-A): load / show the state's lighting sublevel and ramp every light over >= 0.8 s (venue-dive-bar 4.6).
	if (NewState != LightingState)
	{
		LightingState = NewState;
		OnLightingStateChanged.Broadcast(NewState);
	}
}

FString ARbVenueInfo::ValidateVenueLevel(const UObject* WorldContextObject, bool& bOutOk)
{
	// TODO(M2-A): VDB-T10 (table preset / ball set / placement / lamp height + footprint / bed height), VDB-T11 (light flags),
	// cameras RbCam_DB_* / RbCam_Menu_*, PlayerStart, GameMode, collision profiles, URbTableSubsystem::ValidateTables.
	const ARbVenueInfo* Info = Find(WorldContextObject);
	bOutOk = Info != nullptr;
	return bOutOk ? TEXT("OK venue info present (full validator TODO(M2-A))\n") : TEXT("FAIL no ARbVenueInfo in the level\n");
}
