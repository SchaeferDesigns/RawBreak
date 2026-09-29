#pragma once

// Game mode of L_Title (M2 minimal front end, Docs/ue-architecture.md 18.4): no pawn, a dark room with one look-dev camera,
// shows SRbTitleScreen through URbUiSubsystem (ERbUiScreen::Title). Venue select opens RbTypes::MapFor(Venue) with
// "?Mode=Practice|HotSeat" (ARbGameMode options). The full 3D main menu ("Closing Time", ui-ux 6) replaces it later.
// Owner: M2-D (stub by the M2 architect step).

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "Core/RbTypes.h"

#include "RbTitleGameMode.generated.h"

UCLASS()
class RAWBREAK_API ARbTitleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARbTitleGameMode();

	// Travels to the venue's level with the match mode (TODO(M2-D): URL options, loading screen / fade).
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Menu")
	bool StartVenue(ERbVenue Venue, ERbMatchMode Mode);

	// The URL the title screen opens for a venue + mode (tests).
	static FString MakeVenueUrl(ERbVenue Venue, ERbMatchMode Mode);

	virtual void StartPlay() override;
};
