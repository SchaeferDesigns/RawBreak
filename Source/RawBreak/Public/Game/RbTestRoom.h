#pragma once

// The M1 test room (Docs/ue-architecture.md 8.3): a clean, neutral closed room generated in C++ around the level's
// ARbTable - floor, four walls and ceiling (engine cube meshes scaled, Static mobility: distance fields, Lumen cards, HWRT
// geometry), and a WPA tournament table lamp in PHYSICAL units (ue5-realism-plan 6.1):
//   * a canopy over the table with LampSections.X x LampSections.Y diffuser sections; every section is a rect light in
//     LUMENS with its real source size (never 0: plan pitfall 10) and 4000 K, with barn doors that model the canopy's
//     egg-crate louvres (the visible louvre walls are built to the same angle and length);
//   * the fixture underside (louvre bottoms) LampHeightAboveBed above the cloth (WPA >= 1.016 m); the level generator
//     writes it into ARbTable::LampUndersideHeight (single source, review R-14);
//   * >= 520 lux on bed and rails (E4), checked by the analytic lux probe below (every rect / point / spot light of the
//     room, UE's own barn-door clipping, Lambertian emitters, the attenuation-radius window);
//   * emissive diffusers hidden from ray tracing, Lumen and shadows, so the ball highlight comes from the analytic light
//     only (no double highlights, plan pitfall 9); the housing stays visible in reflections (plan 6.6);
//   * a dim ambient: ceiling panels giving ~50 lux on the floor (WPA: >= 50 lux elsewhere in the venue);
//   * an unbound post-process baseline: Eyes-preset exposure law (EV100 range, adaptation speeds, compensation from
//     RbCameraModel::Defaults(Eyes), so the room and the camera model share one source), local exposure, low bloom, white
//     balance on the lamp's colour temperature (the eye adapts to the illuminant).
// The bed height and the table footprint come from the level's ARbTable (TableSpec, R-14); without a table the room uses
// BedHeight and a 9-ft footprint. Every generated component is transient (tag GeneratedComponentTag): OnConstruction builds
// them in the editor, BeginPlay rebuilds them in PIE / -game, so a saved level never stores a stale room. Owner: UE-8.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "RbTestRoom.generated.h"

class ARbTable;
class ULocalLightComponent;
class UMaterialInterface;
class UPostProcessComponent;
class URectLightComponent;
class UStaticMeshComponent;

// Which of the room's lights the lux probe evaluates.
enum class ERbLuxSources : uint8
{
	All,
	Lamp,    // the table lamp only (the WPA check must pass without help from the ambient light)
	Ambient, // the ceiling panels only (the ~50 lux room level)
};

// Result of the lux probe (E4) on a regular grid: bed = playing surface nose to nose at z = 0, rails = the rail caps from the
// nose line to the outer rail edge at RailTopZ, floor = the room floor outside the table footprint (walkway).
struct RAWBREAK_API FRbLuxReport
{
	double BedMin = 0.0, BedMax = 0.0, BedAvg = 0.0;
	double RailMin = 0.0, RailMax = 0.0, RailAvg = 0.0;
	double FloorMin = 0.0, FloorMax = 0.0, FloorAvg = 0.0;
	int32 BedPoints = 0, RailPoints = 0, FloorPoints = 0;

	double TableMin() const { return FMath::Min(BedMin, RailMin); }
	double TableMax() const { return FMath::Max(BedMax, RailMax); }
	// Max / min over bed and rails (WPA: the centre must not get noticeably more than rails and corners).
	double TableUniformity() const { return TableMin() > 0.0 ? TableMax() / TableMin() : 0.0; }
	FString ToString() const;
};

UCLASS()
class RAWBREAK_API ARbTestRoom : public AActor
{
	GENERATED_BODY()

public:
	ARbTestRoom();

	// --- room shell ---------------------------------------------------------------------------------------------------

	// Inner room size [cm] (x along the table, y across, z floor to ceiling): 9-ft table + >= 1.52 m clearance on every side
	// (WPA playing area) -> 6.0 x 4.8 m, 2.8 m high.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	FVector InnerSize = FVector(600.0, 480.0, 280.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	double WallThickness = 20.0;

	// Linear albedo of the fallback surfaces (used while UE-3's M_RbRoomWall / M_RbRoomFloor do not exist): neutral mid-grey
	// walls, dark neutral floor, light ceiling (clean test room, no colour cast on the table, the lit table stands out).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	FLinearColor WallAlbedo = FLinearColor(0.26f, 0.26f, 0.25f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	FLinearColor FloorAlbedo = FLinearColor(0.10f, 0.095f, 0.09f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	FLinearColor CeilingAlbedo = FLinearColor(0.50f, 0.50f, 0.49f);

	// Bed height above the floor [cm] when the level has no ARbTable (otherwise TableSpec::BedHeight, R-14).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	double BedHeight = 76.5;

	// Optional surface materials (M2-L look-dev rooms, e.g. a dark room around the bar table); empty = the generated M_RbRoomWall /
	// M_RbRoomFloor (or the fallback albedos above).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	TSoftObjectPtr<UMaterialInterface> WallMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Room")
	TSoftObjectPtr<UMaterialInterface> FloorMaterialOverride;

	// --- table lamp (WPA, physical units) -----------------------------------------------------------------------------

	// Fixture underside (louvre bottoms) above the cloth [cm]; WPA >= 101.6 cm for a lamp that can be moved aside.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampHeightAboveBed = 101.6;

	// Diffuser sections: one per (column, row), centres [cm] in the table frame (x along the table, y across), source size
	// [cm] (never 0). The canopy covers the table's outline (about 3.2 x 1.7 m for the 9-ft table).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	TArray<double> LampColumnX = {-125.0, -45.0, 45.0, 125.0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	TArray<double> LampRowY = {-57.0, 57.0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	FVector2D LampSectionSize = FVector2D(50.0, 30.0);

	// Luminous flux per section of each column [lm] (missing entries = the last one): the end columns are brighter so the
	// rail corners reach the WPA minimum without over-lighting the centre (bed and rails 610..880 lux, max / min 1.42).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	TArray<double> LampColumnLumens = {2000.0, 950.0, 950.0, 2000.0};

	// Egg-crate louvres around every section = the rect light's barn doors: angle from the emission axis [deg] and length [cm].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampLouvreAngleDeg = 30.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampLouvreLength = 12.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampTemperatureK = 4000.0;

	// Contact shadows on the key lamp for the ball-cloth contact (plan 6.5: 0.02-0.05).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Lamp")
	double LampContactShadowLength = 0.03;

	// --- ambient ------------------------------------------------------------------------------------------------------

	// Ceiling panels (count X x Y, pitch [cm], square source size [cm], flux [lm]): ~50 lux on the room floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Ambient")
	FIntPoint AmbientPanels = FIntPoint(3, 2);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Ambient")
	FVector2D AmbientPanelPitch = FVector2D(200.0, 320.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Ambient")
	double AmbientPanelSize = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Ambient")
	double AmbientPanelLumens = 600.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Ambient")
	double AmbientTemperatureK = 4000.0;

	// --- post-process baseline ------------------------------------------------------------------------------------------

	// Local exposure highlight / shadow contrast (plan 4.4 Eyes: 0.8 / 0.9) and bloom intensity (low: lamp glare only).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Post")
	float LocalExposureHighlightContrast = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Post")
	float LocalExposureShadowContrast = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Post")
	float BloomIntensity = 0.15f;

	// White balance [K] of the post-process baseline; <= 0 = the lamp's colour temperature + 200 K (the eye adapted to the illuminant,
	// M1). A warm bar lamp keeps its warmth with a higher value (M2-L look-dev room).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Post")
	double WhiteBalanceTempK = 0.0;

	// --- API ----------------------------------------------------------------------------------------------------------

	// Rebuilds every generated component from the properties and the level's table.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Room")
	void RebuildRoom();

	// The table the room is built around (first ARbTable of the world), nullptr if none.
	ARbTable* FindTable() const;

	// Bed height above the floor [cm] the room was built for (TableSpec when a table exists, else BedHeight).
	double GetBedHeightUsed() const { return BedHeightUsed; }

	// Fixture underside above the cloth [m] (= ARbTable::LampUndersideHeight of the level).
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Room")
	double GetLampUndersideHeightMeters() const { return LampHeightAboveBed / 100.0; }

	// Height of the lamp's emitting plane (diffusers) above the cloth [cm]: underside + louvre depth.
	double GetLampEmitterHeight() const;

	const TArray<TObjectPtr<URectLightComponent>>& GetLampLights() const { return LampLights; }
	const TArray<TObjectPtr<URectLightComponent>>& GetAmbientLights() const { return AmbientLights; }
	UPostProcessComponent* GetPostProcess() const { return PostProcess; }

	// Shows / hides the lamp fixture meshes (housing, louvres, diffusers, rods) in game views; the lights stay on. Used by
	// the overhead look-dev camera, which looks at the table from above the lamp.
	void SetLampFixtureHiddenInGame(bool bHide);
	bool IsLampFixtureHiddenInGame() const { return bFixtureHidden; }

	// Lux probe (plan 6.1, E4) over bed, rails and floor on a GridCm grid (the floor on a 4x coarser one) from the room's
	// lights. Table frame from the level's table (or a 9-ft footprint at the room origin).
	FRbLuxReport ComputeLux(double GridCm = 5.0, ERbLuxSources Sources = ERbLuxSources::All) const;

	// Illuminance [lux] at a world point on a surface with the given unit normal from one light: rect lights as Lambertian
	// emitters with UE's barn-door clipping (RectLight.ush GetRect), point / spot lights as point sources (spot cone falloff on
	// UE's clamped cone angles); the inverse-square window of the attenuation radius; lights in Lumens / Candelas / Nits (point /
	// spot Nits over the capsule area, like UE); other units (EV, unitless) and exponent falloff -> 0.
	static double IlluminanceFromLight(const ULocalLightComponent& Light, const FVector& WorldPoint, const FVector& Normal);

	// The M1 level validator (ue-architecture 13 UE-8): one table at the origin, one room, a PlayerStart at the head end facing
	// the table, the four ARbLookDevCameras (RbAssetPaths::CaptureCamera tags), World Settings GameMode = ARbGameMode, table
	// LampUndersideHeight == room lamp, room bed height == TableSpec, lux probe >= 520 on bed and rails and ~50 lux ambient.
	// Returns the report (one line per check, "OK" / "FAIL", plus metrics); bOutOk = every check passed.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Room", meta = (WorldContext = "WorldContextObject"))
	static FString ValidateM1Level(const UObject* WorldContextObject, bool& bOutOk);

	// WPA tournament minimum on bed and rails [lux] and the ambient target band on the floor (E4, A8).
	static constexpr double WpaMinLux = 520.0;
	static constexpr double AmbientMinLux = 35.0;
	static constexpr double AmbientMaxLux = 80.0;

	// Tag of every generated (transient) component.
	static const FName GeneratedComponentTag;

	// AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

protected:
	void DestroyGeneratedComponents();

	UStaticMeshComponent* AddBox(const FName& Name, const FVector& Center, const FVector& Size, const FRotator& Rotation, UMaterialInterface* Material,
		bool bFixture);
	UStaticMeshComponent* AddMesh(const FName& Name, UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, bool bFixture);
	URectLightComponent* AddRectLight(const FName& Name, const FVector& Location, const FRotator& Rotation, const FVector2D& SourceSize, double Lumens,
		double TemperatureK, double BarnAngleDeg, double BarnLength);

	void BuildShell();
	void BuildLamp();
	void BuildAmbient();
	void ApplyPostProcess();

	// Materials: the generated ones (UE-3) when they exist, else transient fallback instances.
	UMaterialInterface* SurfaceMaterial(const TCHAR* GeneratedPath, const FLinearColor& FallbackAlbedo, float FallbackRoughness);
	UMaterialInterface* EmissiveMaterial(double LuminanceNits);

	// Table frame used by the lamp / probe: world transform of the bed centre on the cloth (translation + yaw).
	FTransform GetTableFrame() const;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Room")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Room")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Surfaces; // floor, ceiling, walls

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FixtureMeshes; // lamp housing, louvres, diffusers, rods, ambient panels

	UPROPERTY(Transient)
	TArray<TObjectPtr<URectLightComponent>> LampLights;

	UPROPERTY(Transient)
	TArray<TObjectPtr<URectLightComponent>> AmbientLights;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> TransientMaterials; // fallback MIDs (kept alive with the components)

	// Centre-weighted metering mask of the auto exposure (plan 4.4), generated at BeginPlay (game worlds only, never saved).
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> MeterMask;

	double BedHeightUsed = 76.5;
	bool bFixtureHidden = false;
};
