#pragma once

// Dev / look-dev driver for the ball set (UE-2): placed in a dev map, it racks a full ball set on the level's table at
// BeginPlay through the same core calls the match uses (rules GenerateRack with micro-gaps, cue ball on the head spot)
// and can simulate a break on the game thread and play it back - optionally frozen at a shot time, so a headless capture
// shows balls in motion. Also the quickest way to see playback, orientation and radii in the real renderer without the
// match flow (Tools/unreal/editor/rb_dev_ue2.py, Docs/images/dev/UE-2). Not used by the game. Owner: UE-2.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Core/RbTypes.h"
#include "Simulation/RbShot.h"

#include "RbBallRackDemo.generated.h"

class ARbBallSet;
class ARbTable;
class UMaterialInterface;

UCLASS()
class RAWBREAK_API ARbBallRackDemo : public AActor
{
	GENERATED_BODY()

public:
	ARbBallRackDemo();

	// Table to rack on; none = the first ARbTable of the level, else one is spawned at this actor's location.
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	TObjectPtr<ARbTable> Table;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	ERbDiscipline Discipline = ERbDiscipline::EightBall;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	int32 RackSeed = 7;

	// Seeded random ball orientations (a real rack; false = identity, all number circles facing +x / -x).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	bool bRandomOrientations = true;

	// Cue ball = head spot + this offset [m] in the core plan frame (+x foot, +y left).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	FVector2D CueBallOffset = FVector2D::ZeroVector;

	// Passed to the ball set (dev material while UE-3's M_RbBall does not exist).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	TSoftObjectPtr<UMaterialInterface> BallMaterialOverride;

	// Simulate a break at BeginPlay and play it back.
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	bool bPlayBreak = false;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float BreakSpeed = 9.0f; // tip speed [m/s]

	// Added to the azimuth of the line cue ball -> foot spot [deg].
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float BreakAimOffsetDeg = 0.0f;

	// Contact-point offsets / R (a right english, b follow).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	FVector2D BreakTipOffset = FVector2D(0.0, -0.1);

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float PlaybackRate = 1.0f;

	// >= 0: seek the break to this shot time [s] and pause there (captures of a moving shot).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float FreezeAtShotTime = -1.0f;

	ARbBallSet* GetBallSet() const { return BallSet; }
	TSharedPtr<const FRbShot> GetShot() const { return Shot; }

	// The demo rack as simulator input: InitSimInput + GenerateRack (wooden-rack micro-gaps) + cue ball on the head
	// spot (+ offset). False with OutError on a core error.
	static bool BuildRackInput(const TSharedPtr<const FRbTableContext>& Context, ERbDiscipline Discipline, int32 Seed, bool bRandomOrientations,
		const FVector2D& CueOffset, rb::SimInput& Out, FString& OutError);

	// Simulates Input with one break strike on the game thread (dev only; a break takes ~ms) into a compact FRbShot.
	static TSharedPtr<const FRbShot> SimulateStrike(const TSharedPtr<const FRbTableContext>& Context, const rb::SimInput& Input,
		const rb::CueStrikeInput& Strike, FString& OutError);

	virtual void BeginPlay() override;

protected:
	UPROPERTY(Transient)
	TObjectPtr<ARbBallSet> BallSet;

	TSharedPtr<const FRbShot> Shot;
};
