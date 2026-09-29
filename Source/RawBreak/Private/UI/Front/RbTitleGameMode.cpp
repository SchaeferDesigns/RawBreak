#include "UI/Front/RbTitleGameMode.h"

#include "RawBreak.h"

#include "Kismet/GameplayStatics.h"

// Owner: M2-D.

ARbTitleGameMode::ARbTitleGameMode()
{
	DefaultPawnClass = nullptr;
}

FString ARbTitleGameMode::MakeVenueUrl(ERbVenue Venue, ERbMatchMode Mode)
{
	return FString::Printf(TEXT("%s?Mode=%s"), RbTypes::MapFor(Venue), Mode == ERbMatchMode::HotSeat ? TEXT("HotSeat") : TEXT("Practice"));
}

bool ARbTitleGameMode::StartVenue(ERbVenue Venue, ERbMatchMode Mode)
{
	// TODO(M2-D): refuse a venue whose level does not exist yet (disabled entry), fade, loading screen.
	UGameplayStatics::OpenLevel(this, FName(*MakeVenueUrl(Venue, Mode)));
	return true;
}

void ARbTitleGameMode::StartPlay()
{
	Super::StartPlay();
	// TODO(M2-D): URbUiSubsystem::OpenScreen(ERbUiScreen::Title) for the first local player, UIOnly input.
}
