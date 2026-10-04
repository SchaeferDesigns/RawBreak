#pragma once

// Game mode of L_Title (M2 minimal front end, Docs/ue-architecture.md 18.4): no pawn, the level's title camera (an
// ARbLookDevCamera tagged RbCam_Title, Eyes preset) as the view, SRbTitleScreen through URbUiSubsystem (ERbUiScreen::Title, UI-only
// input with a cursor). Venue select opens RbTypes::MapFor(Venue) with "?Mode=Practice|HotSeat" (ARbGameMode options). The full
// 3D main menu ("Closing Time", ui-ux 6) replaces it later. Owner: M2-D.

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

	// Tag of the title camera placed by rb_make_title.py.
	static const FName TitleCameraTag;

	// Travels to the venue's level with the match mode. False when the venue's level does not exist yet.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Menu")
	bool StartVenue(ERbVenue Venue, ERbMatchMode Mode);

	// The URL the title screen opens for a venue + mode (tests).
	static FString MakeVenueUrl(ERbVenue Venue, ERbMatchMode Mode);
	// The travel itself (also from a title screen shown outside L_Title).
	static bool TravelToVenue(UObject* WorldContext, ERbVenue Venue, ERbMatchMode Mode);

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void StartPlay() override;

protected:
	// The level's title camera as the player's view, the title screen on the player's viewport.
	void ShowTitle(APlayerController* Player);
};
