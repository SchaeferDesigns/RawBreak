#pragma once

// The M1 test room (Docs/ue-architecture.md 8.4): a simple but clean closed room generated in C++ around the
// table - floor, four walls, ceiling (engine cube meshes scaled, so they are static meshes with distance fields and
// Lumen cards), neutral materials, and the table lamp in PHYSICAL units (ue5-realism-plan 6.1: rect lights with a
// real source size, >= 520 lux on bed and rails for a tournament table, lamp >= 1.016 m above the bed, emissive
// diffuser hidden from ray-traced reflections to avoid double highlights, pitfall 9). A dim ambient ceiling light
// keeps the room at ~50 lux (WPA minimum elsewhere). Everything regenerates from the properties. Owner: UE-8.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "RbTestRoom.generated.h"

class URectLightComponent;
class UStaticMeshComponent;

UCLASS()
class RAWBREAK_API ARbTestRoom : public AActor
{
	GENERATED_BODY()

public:
	ARbTestRoom();

	// Inner room size [cm] (x along the table, y across, z floor to ceiling). Default: 9-ft table + 1.52 m
	// clearance on every side (WPA playing area) -> about 6.0 x 4.8 x 2.8 m.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	FVector InnerSize = FVector(600.0, 480.0, 280.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	double WallThickness = 20.0;

	// Lamp: shade opening centred over the bed, underside height above the cloth [cm], light per section.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampHeightAboveBed = 101.6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	int32 LampSections = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	FVector2D LampSectionSize = FVector2D(50.0, 40.0); // source size [cm] (never 0: plan pitfall 10)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampSectionLumens = 3000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampTemperatureK = 4000.0;

	// Bed height above the floor [cm] (the lamp is placed relative to it). Single source (review R-14): RebuildRoom takes
	// it from the level's ARbTable (TableSpec::BedHeight) when one exists, and the level generator writes the lamp
	// underside height into ARbTable::LampUndersideHeight (the physics' off-table apex check sees the same lamp).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	double BedHeight = 76.5;

	UFUNCTION(BlueprintCallable, Category = "RawBreak|Room")
	void RebuildRoom();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Room")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Surfaces; // floor, ceiling, 4 walls, lamp housing

	UPROPERTY(Transient)
	TArray<TObjectPtr<URectLightComponent>> LampLights;
};
