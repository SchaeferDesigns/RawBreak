#pragma once

// The identity and state of a venue level (Docs/ue-architecture.md 18.8; venue-dive-bar 4.6, 6.2, 13.8). One per venue level,
// placed by the level generator (rb_make_divebar.py, M2-A) and tagged RbAssetPaths::Tag::VenueInfo; the M1 test room has
// none (ERbVenue::TestRoom is assumed). Readers: audio (room tone, reverb and zones per venue, M2-C), the UI (title / pause
// venue name, M2-D), the menu scene later (AfterHours). Tables carry their own venue inputs (ARbTable::VenueSeed / VenueKind /
// TableIndex; several tables per venue).
//
// Lighting states (venue-dive-bar 4.6): Open (default), LightsUp (closing time, troffers on), AfterHours (the main menu, later).
// Each state is a lighting sublevel of the venue map (L_DiveBar_Light_<State>); every change is a ramp of >= 0.8 s per light,
// never a one-frame step, and no light changes faster than 3 Hz (photosensitivity, VDB-T8).
// Owner: M2-A (stub by the M2 architect step; TODO(M2-A)).

#include "CoreMinimal.h"
#include "GameFramework/Info.h"

#include "Core/RbTypes.h"

#include "RbVenueInfo.generated.h"

UENUM(BlueprintType)
enum class ERbLightingState : uint8
{
	Open,
	LightsUp,
	AfterHours,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnLightingStateChanged, ERbLightingState /*NewState*/);

UCLASS()
class RAWBREAK_API ARbVenueInfo : public AInfo
{
	GENERATED_BODY()

public:
	ARbVenueInfo();

	// The level's venue info (nullptr in levels without one, e.g. the M1 test room).
	static ARbVenueInfo* Find(const UObject* WorldContext);
	// The level's venue: its ARbVenueInfo's, else TestRoom.
	static ERbVenue GetVenue(const UObject* WorldContext);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Venue")
	ERbVenue Venue = ERbVenue::TestRoom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Venue")
	ERbVenueKind VenueKind = ERbVenueKind::DiveBar;

	// The venue seed the level generator gave its tables (ARbTable::VenueSeed; seeded roll-off, venue-dive-bar 3.2).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Venue")
	int64 VenueSeed = 0;

	// Venue-wide wear (MPC_DB_Venue.Age: dive bar 0.80, venue-dive-bar 6.2). The material generator writes the MPC; this copy is
	// informational (validator, UI).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Venue")
	float Age = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Venue")
	ERbLightingState InitialLightingState = ERbLightingState::Open;

	// Switches the lighting state with a ramp (>= 0.8 s). TODO(M2-A): sublevel streaming + per-light intensity ramps.
	void SetLightingState(ERbLightingState NewState, float RampSeconds = 0.8f);
	ERbLightingState GetLightingState() const { return LightingState; }

	FRbOnLightingStateChanged OnLightingStateChanged;

	// Level validator of a venue level (called by rb_make_divebar.py and rb_make_all.py; VDB-T10, VDB-T11, VDB-T12 parts):
	// returns the report (one line per check, "OK" / "FAIL"), bOutOk = every check passed.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Venue", meta = (WorldContext = "WorldContextObject"))
	static FString ValidateVenueLevel(const UObject* WorldContextObject, bool& bOutOk);

	// AActor
	virtual void BeginPlay() override;

protected:
	ERbLightingState LightingState = ERbLightingState::Open;
};
