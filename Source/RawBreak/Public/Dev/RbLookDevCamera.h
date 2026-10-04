#pragma once

// Look-dev / acceptance capture camera (Docs/ue-architecture.md 12 A7, review R-08). A cine camera actor that applies a
// RAW BREAK camera preset (default Eyes: vertical FOV 50 deg, pupil DoF, EV100 exposure law, shutter, grain) through
// URbCameraRigComponent::ApplyPresetToCamera with the real viewport aspect (re-applied when the viewport is resized). The
// level generators place these (tags RbAssetPaths::CaptureCamera::*) instead of plain ACameraActors, so headless screenshots
// judge the look through the game's own camera model. URbHeadlessCaptureSubsystem finds it like any ACameraActor (tag or
// name). Owner: UE-8.
//
// Focus: FocusDistanceCm > 0 = manual; 0 = the first blocking hit along the view axis (traced every frame), or the aim point
// in the ChinOnCue placement (the eye focuses on the object ball, plan 4.5).
// Placement ChinOnCue: the eye is placed like the rig's DownOnShot (plan 4.2: e = P_axis(s_e) + h_c n_up + y_vc n_side with
// the preset's s_e / h_c / y_vc) from a cue axis: the director's cue ball (the capture places it, else CueBallCore) aimed at
// AimPointCore with CueElevationDeg; while this camera is the view target it also poses the level's ARbCue at address along
// that axis (bPoseCue), so the "cue under the chin" view exists before the pawn's own get-down path is capturable.
// bHideLampFixture: while this camera is the view target the test room's lamp housing is hidden (the overhead view looks at
// the table from above the lamp); the lights stay on.
// Placement TableView (M2-L, Docs/ue-architecture.md 18.7): one of the standard table look-dev views (overhead, standing at the
// head end, pocket close-up, cushion grazing, rail close-up) computed from the table's own geometry, so the same camera works on
// every preset and after every TableSpec change (ComputeTableViewPose).
// Table: TableIndex >= 0 = the level's table with that index (URbTableSubsystem), else the player's table.
// Look-dev only: nothing happens unless this camera is the local player's view target, so the cameras can stay in the M1 map.
// Owner: M2-L (UE-8 in M1).

#include "CoreMinimal.h"
#include "CineCameraActor.h"

#include "Camera/RbCameraModel.h"
#include "Core/RbTypes.h"

#include "RbLookDevCamera.generated.h"

class ARbTable;

UENUM(BlueprintType)
enum class ERbLookDevPlacement : uint8
{
	Fixed,     // the placed transform
	ChinOnCue, // eye from the cue axis (plan 4.2), cue posed at address
	TableView, // M2-L: a standard table view (ERbTableLookDevView) from the table geometry
};

// Standard table look-dev views of M2-L (Docs/ue-architecture.md 18.7; the chin-on-cue view is the ChinOnCue placement).
UENUM(BlueprintType)
enum class ERbTableLookDevView : uint8
{
	Overhead,       // straight down over the bed centre, the whole table in frame (image right = foot)
	Standing,       // standing eye (1.65 m above the floor) behind the head rail, looking at the foot spot
	PocketCloseUp,  // the foot-left corner pocket from the table side: jaws, facings, liner, drop, hardware
	CushionGrazing, // 3 cm above the cloth along the right long cushion: nose roll, rubber, cloth sheen at grazing angles
	RailCloseUp,    // from outside, above the right long rail: cap edge, sights, rail body, apron / cabinet below
	FootEnd,        // bent over the foot end (eye 1.15 m above the floor): foot rail, coin mechanism, trap window, ball tray, return
};

// Result of a table view placement (world space, cm).
struct FRbTableViewPose
{
	FVector Eye = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector FocusPoint = FVector::ZeroVector;
};

// Result of the chin-on-cue placement (world space, cm).
struct FRbChinOnCuePose
{
	FVector Eye = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector AimPoint = FVector::ZeroVector;   // object-ball centre the eye looks at (world)
	FVector3d TipDomeCore = FVector3d::ZeroVector; // cue pose in core terms [m] (ARbCue::SetPoseCore)
	FVector3d DirectionCore = FVector3d::UnitX();
};

UCLASS()
class RAWBREAK_API ARbLookDevCamera : public ACineCameraActor
{
	GENERATED_BODY()

public:
	ARbLookDevCamera(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	ERbCameraPreset Preset = ERbCameraPreset::Eyes;

	// Focus distance [cm] (0 = focus on the first blocking hit along the view axis; ChinOnCue: on the aim point).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	float FocusDistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	ERbLookDevPlacement Placement = ERbLookDevPlacement::Fixed;

	// ChinOnCue: cue-ball centre on the cloth [m, core table frame] used when the director has no cue ball in play.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	FVector2D CueBallCore = FVector2D(-0.60, 0.10);

	// ChinOnCue: aim point on the cloth [m, core table frame] (the object ball the eye focuses on).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	FVector2D AimPointCore = FVector2D(0.635, 0.0);

	// ChinOnCue: cue elevation above the cloth [deg] and the tip dome's gap to the cue ball at address [cm].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	float CueElevationDeg = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	float TipGapCm = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	bool bPoseCue = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	bool bHideLampFixture = false;

	// TableView placement: which view (M2-L).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	ERbTableLookDevView TableView = ERbTableLookDevView::Standing;

	// The table this camera looks at: >= 0 = the table with that TableIndex, else the player's table (URbTableSubsystem).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	int32 TableIndex = -1;

	// Look-dev exposure calibration [EV] added to the preset's exposure compensation (M2-L): a look-dev room darker than the venue it
	// stands in for adapts lower than the venue's target band (venue-dive-bar 4.5); the bias restores that band for the captures.
	// 0 = the preset's exposure law unchanged (M1 cameras).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	float ExposureBiasEv = 0.0f;

	// Chin-on-cue eye and cue pose (plan 4.2) for a cue ball / aim point in the core table frame [m]; Params gives s_e, h_c, y_vc.
	static FRbChinOnCuePose ComputeChinOnCuePose(const ARbTable& Table, const FRbCameraPresetParams& Params, const FVector2D& CueBall,
		const FVector2D& AimPoint, double ElevationDeg, double TipGapM, double BallRadiusM, double TipDomeRadiusM);

	// Eye, rotation and focus point of a standard table view (M2-L), from the table's TableSpec / geometry and its frame.
	static FRbTableViewPose ComputeTableViewPose(const ARbTable& Table, ERbTableLookDevView View);

	// The table this camera looks at (TableIndex, else the player's table), nullptr without one.
	ARbTable* ResolveTable() const;

	// Aspect ratio (width / height) of the game viewport; 16:9 without one (editor preview, commandlets).
	static float GetViewportAspect();

	// True while this camera is the first local player's view target.
	bool IsActiveViewTarget() const;

	// Applies the preset's optics at Aspect (URbCameraRigComponent::ApplyPresetToCamera; see the .cpp for the integration
	// guard). Called at construction (16:9), BeginPlay and on viewport resizes.
	void ApplyOptics(float Aspect);

	// AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	void UpdatePlacement(bool bActive);
	void UpdateFocus();
	void SetLampFixtureHidden(bool bHide);

	float AppliedAspect = 0.0f;
	bool bFixtureHiddenByMe = false;
	bool bLoggedFallback = false;
	FVector FocusPointWorld = FVector::ZeroVector; // ChinOnCue: the aim point
	bool bHasFocusPoint = false;
};
