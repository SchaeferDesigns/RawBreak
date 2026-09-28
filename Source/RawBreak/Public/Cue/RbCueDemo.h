#pragma once

// Dev / look-dev driver for the cue (UE-4): placed in a dev map, it shows at BeginPlay either one cue at address - the pose of
// the game (URbStrokeComponent::ComputeCuePoseCore: aim, tip offsets, tip back from the contact point) with the elevation
// raised to the clearance floor of RbCueClearance (balls, rails and the environment sweep, exactly as the stroke component
// floors it) - or every cue preset lying on the bed side by side. Balls: the cue ball and the object balls of the scene
// through an ARbBallSet. Used by Tools/unreal/editor/rb_dev_ue4.py (Docs/images/dev/UE-4); not used by the game. Owner: UE-4.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Core/RbTypes.h"
#include "Cue/RbCueClearance.h"

#include "RbCueDemo.generated.h"

class ARbBallSet;
class ARbCue;
class ARbTable;
class UMaterialInterface;

UENUM(BlueprintType)
enum class ERbCueDemoScene : uint8
{
	Address, // one cue at the cue ball, elevation floored by RbCueClearance
	Lineup,  // every cue preset lying on the bed (mesh review)
};

UCLASS()
class RAWBREAK_API ARbCueDemo : public AActor
{
	GENERATED_BODY()

public:
	ARbCueDemo();

	// Table of the scene; none = the first ARbTable of the level.
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	TObjectPtr<ARbTable> Table;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	ERbCueDemoScene Scene = ERbCueDemoScene::Address;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	ERbCuePreset CuePreset = ERbCuePreset::Playing19oz;

	// > 0: the preset cue cut to this length [cm] (a bar short cue: 132.08 / 121.92 / 91.44), shown by the runtime mesh.
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float CueLengthOverrideCm = 0.0f;

	// Cue-ball centre in the core plan frame [m] (+x foot, +y left).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	FVector2D CueBall = FVector2D(-0.6, 0.1);

	// Object balls (ids 1, 2, ...) in the core plan frame [m].
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	TArray<FVector2D> ObjectBalls;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float AzimuthDeg = 0.0f;

	// Requested elevation; the shown one is max(requested, floor) when bClearanceFloor.
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float ElevationDeg = 0.0f;

	// Cue-AXIS offsets / R (A right english, B follow) like the stroke component's aim.
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	FVector2D TipOffset = FVector2D::ZeroVector;

	// Tip behind the contact point along the axis [cm] (0 = touching the ball).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float TipBackCm = 0.5f;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	bool bClearanceFloor = true;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float BackswingCm = 30.0f;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	float LineupSpacingCm = 10.0f;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	TSoftObjectPtr<UMaterialInterface> CueMaterialOverride;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Demo")
	TSoftObjectPtr<UMaterialInterface> BallMaterialOverride;

	// Result of the Address scene (logged): the floor and the shown elevation [deg].
	const FRbCueClearanceResult& GetFloor() const { return Floor; }
	double GetShownElevation() const { return ShownElevation; }
	const TArray<TObjectPtr<ARbCue>>& GetCues() const { return Cues; }
	ARbBallSet* GetBallSet() const { return BallSet; }

	// Builds the scene now (BeginPlay does it; tests call it on a world without BeginPlay).
	void BuildScene();

	virtual void BeginPlay() override;

protected:
	void BuildAddress();
	void BuildLineup();

	UPROPERTY(Transient)
	TObjectPtr<ARbBallSet> BallSet;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ARbCue>> Cues;

	FRbCueClearanceResult Floor;
	double ShownElevation = 0.0;
};
