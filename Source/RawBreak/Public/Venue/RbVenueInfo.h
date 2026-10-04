#pragma once

// The identity and state of a venue level (Docs/ue-architecture.md 18.8; venue-dive-bar 4.6, 6.2, 13.8). One per venue level,
// placed by the level generator (rb_make_divebar.py, M2-A) and tagged RbAssetPaths::Tag::VenueInfo; the M1 test room has
// none (ERbVenue::TestRoom is assumed). Readers: audio (room tone, reverb and zones per venue, M2-C), the UI (title / pause
// venue name, M2-D), the menu scene later (AfterHours). Tables carry their own venue inputs (ARbTable::VenueSeed / VenueKind /
// TableIndex; several tables per venue).
//
// Lighting states (venue-dive-bar 4.6): Open (default), LightsUp (closing time, troffers on), AfterHours (the main menu, later).
// The lights live in always-loaded lighting sublevels of the venue map (L_DiveBar_Light_Open: the night rig, _LightsUp: the
// troffers, _AfterHours: its state stub) so the three states exist at load; a state change is a per-light intensity RAMP of
// >= 0.8 s (smoothstep, RbVenueLighting::Ramp01), never a one-frame step, driven by this actor from Lights (the level
// generator's copy of Art/DiveBar/lights.json: state factors, animations, neon proxy flux). No light changes faster than 3 Hz
// (photosensitivity, VDB-T8): the animations of RbVenueLighting are smooth and counted by the validator.
// Also: the venue post-process baseline (Eyes exposure law of RbCameraModel for views without a RAW BREAK camera, white balance
// on the venue's key light), the slow ceiling fan (actors tagged RbDB_Fan), the plan capture (the ceiling actors tagged
// RbDB_Ceiling are hidden while a camera tagged RbCam_DB_V10 is the view target), the EV report of the captures (-RbEvLog or a
// capture run: "RbVenue EV" log lines with the adapted exposure, VDB-T2) and the level validator.
// Command line: -RbLightingState=Open|LightsUp|AfterHours sets the initial state (captures V12); the console command
// rb.Venue.LightingState <State> [seconds] ramps to a state at run time (look-dev, VDB-T12 transition captures).
// Owner: M2-A.

#include "CoreMinimal.h"
#include "GameFramework/Info.h"

#include "Core/RbTypes.h"
#include "Venue/RbVenueLighting.h"

#include "RbVenueInfo.generated.h"

class UCurveFloat;
class ULightComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UTexture2D;
class FRbVenueViewExtension;

UENUM(BlueprintType)
enum class ERbLightingState : uint8
{
	Open,
	LightsUp,
	AfterHours,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnLightingStateChanged, ERbLightingState /*NewState*/);

// Lens of one capture camera that differs from its preset (venue-dive-bar 12.3 trailer hooks: 26 mm phone, 100 mm macro f/2.8, ...;
// ui-ux 6.3 menu stations: vertical FOV 30 / 40 deg). The look-dev camera applies its preset at BeginPlay / resize; the venue info
// re-applies these on top while that camera is the view target. 0 = keep the preset's value.
USTRUCT(BlueprintType)
struct RAWBREAK_API FRbVenueCameraOptics
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	FName CameraTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float VerticalFovDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float HorizontalFovDeg = 0.0f;

	// Full-frame equivalent focal length (36 mm wide frame) [mm].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float FocalLength35mm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue")
	float FStop = 0.0f;
};

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

	// --- lighting (generator: Art/DiveBar/lights.json) -------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Lighting")
	TArray<FRbVenueLight> Lights;

	// The 4.4 lamp model of the table lamp (VDB-T1): bulb flux / efficiency / cut-off; the bulbs are the lights of group
	// LampGroup (positions from the placed light actors, so the probe measures the level, not the spec).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Lighting")
	FName LampGroup = TEXT("table_lamp");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Lighting")
	FName LampReflectorGroup = TEXT("table_lamp_reflector");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Lighting")
	float LampEfficiency = 0.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Lighting")
	float LampCutoffDeg = 50.2f;

	// Switches the lighting state with a ramp (>= RbVenueLighting::MinRampSeconds per light; RampSeconds overrides the lights'
	// own ramp time when longer).
	void SetLightingState(ERbLightingState NewState, float RampSeconds = 0.8f);
	ERbLightingState GetLightingState() const { return LightingState; }

	// Current state factor of a light (ramp included, animation excluded); -1 for an unknown id.
	float GetLightStateFactor(FName Id) const;
	bool IsRamping() const;

	// Advances the ramps / animations by DeltaSeconds and applies them to the bound light components (Tick calls it; tests drive it).
	void UpdateLighting(float DeltaSeconds);

	// Binds Lights to the light components of the world (tag RbVenueLighting::LightTag(Id)); returns the number bound.
	int32 BindLights();

	// Look-dev: an extra intensity factor for every light of a group ("all" = every light; console rb.Venue.GroupScale <group> <f>,
	// e.g. in a capture's -ExecCmds to see what a group contributes). 1 = the rig as generated. Not saved.
	void SetGroupScale(FName Group, float Scale);
	float GetGroupScale(FName Group) const;

	FRbOnLightingStateChanged OnLightingStateChanged;

	// --- look ------------------------------------------------------------------------------------------------------------------

	// White balance [K]: the eye adapts toward the venue's key light (2700 K tungsten-look LEDs) but keeps much of its warmth at night
	// (incomplete chromatic adaptation at low luminance): 4200 K leaves the 2700 K lamp amber and the 2200 K pendants orange; 3300 K
	// (v1) rendered the room cold once the venue post-process actually applied (M2-A look-dev, captures V01 / V02).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Look")
	float WhiteTemp = 4200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Look")
	float WhiteTint = 0.0f;

	// Ceiling fan speed (E24: 40 rpm, 4 blades -> 2.7 Hz blade pass < 3 Hz; the blades cast no shadow).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Look")
	float FanRpm = 40.0f;

	// Night look: the exposure compensation curve of RbVenueLighting::NightCompensationKeys on the venue post-process (incomplete
	// mesopic adaptation; the cameras do not override the curve, so it holds for the player's eyes and the capture cameras).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Look")
	bool bNightLook = true;

	UPostProcessComponent* GetPostProcess() const { return PostProcess; }

	// --- VDB-T1 rendered white card (capture_divebar.py --lux; console rb.Venue.LuxProbe <x> <y> <z>) -------------------------------

	// The hidden lux rig of the level (generator: a 8 x 8 cm Lambertian card of albedo 0.80 as the root, emissive reference cards of
	// known lit-equivalent luminance and a manual-exposure camera tagged RbCam_DB_Lux, all tagged LuxRigTag): moves the card onto
	// the core point (table frame [m], z above the cloth), shows the rig and switches every light group except the lamp's off
	// (lamp-only, Lumen GI on). Returns false without a rig / table.
	bool ShowLuxProbe(const FVector3d& CorePoint);

	static const FName LuxRigTag;    // RbDB_LuxRig (every rig actor)
	static const FName LuxCardTag;   // RbDB_LuxCard (the root: the white card)

	// --- capture cameras (generator: layout.json cameras) ------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Venue|Cameras")
	TArray<FRbVenueCameraOptics> CameraOptics;

	// Focal length [mm] on a filmback of SensorWidthMm x SensorHeightMm that gives the entry's field of view (0 = no override).
	static double FocalLengthFor(const FRbVenueCameraOptics& Optics, double SensorWidthMm, double SensorHeightMm);

	// --- validation / reports ----------------------------------------------------------------------------------------------------

	// Level validator of a venue level (called by rb_make_divebar.py and rb_make_all.py; VDB-T10, VDB-T11, VDB-T12 parts):
	// returns the report (one line per check, "OK" / "FAIL"), bOutOk = every check passed.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Venue", meta = (WorldContext = "WorldContextObject"))
	static FString ValidateVenueLevel(const UObject* WorldContextObject, bool& bOutOk);

	// VDB-T1 lux report of the level's table lamp (lamp only): the analytic 4.4 model on the placed bulbs and the direct light of the
	// placed UE lights of the lamp groups (UE's photometric model, occlusion traced), every 4.4 point with its band, bed + rail
	// minimum / maximum / uniformity, WPA "failed by design"; plus the all-lights values (informational).
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Venue", meta = (WorldContext = "WorldContextObject"))
	static FString ComputeLuxReport(const UObject* WorldContextObject, bool& bOutOk);

	// AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// Tags of the generated level (rb_make_divebar.py).
	static const FName CeilingTag;   // == RbAssetPaths::Tag::DiveBarCeiling
	static const FName FanTag;       // RbDB_Fan
	static const FName GeometryTag;  // RbDB_Geo (every generated architecture / greybox actor)

protected:
	struct FBoundLight
	{
		TWeakObjectPtr<ULightComponent> Component;
		FLinearColor BaseColor = FLinearColor::White;
		FRotator BaseRotation = FRotator::ZeroRotator;
		FRbLightRamp Ramp;
		bool bVisible = true;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> EmissiveMids;
	};

	void ApplyPostProcess();
	void ApplyCameraOptics();
	void ApplyLight(int32 Index, double Time);
	void UpdateCeilingVisibility();
	void LogExposure(double Now);

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Venue")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MeterMask;

	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> NightCurve;

	ERbLightingState LightingState = ERbLightingState::Open;
	TArray<FBoundLight> Bound; // index = Lights index
	TMap<FName, float> GroupScales;
	TArray<TWeakObjectPtr<AActor>> FanActors;
	TArray<TWeakObjectPtr<AActor>> CeilingActors;
	double LightTime = 0.0;
	bool bCeilingHidden = false;
	bool bEvLog = false;
	double LastEvLog = -1.0;
	TSharedPtr<FRbVenueViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
