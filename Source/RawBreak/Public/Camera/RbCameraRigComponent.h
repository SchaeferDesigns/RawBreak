#pragma once

// First-person camera rig (ue5-realism-plan 4.2-4.8, Docs/ue-architecture.md 6.3). Drives the character's
// UCineCameraComponent: eye placement (standing eye height; down on the shot e = P_axis(s_e) + h_c n_up + y_vc n_side
// from the cue axis), the get-down / stand-up transition, the authored VERTICAL FOV (MaintainYFOV), DoF focus on the
// aim target with an accommodation ease and the pupil-equivalent aperture, exposure / motion blur / grain from the
// preset, and later the procedural head motion layer. Owner: UE-5b.
//
// Vertical FOV on a UCineCameraComponent (review R-06, verified in UE 5.8 CameraStackTypes.cpp): with MaintainYFOV the engine
// derives the vertical FOV from the lens and the camera's OWN aspect ratio (the filmback), V = 2 atan(h_sensor / 2f), and
// the diaphragm DoF scales its circle of confusion by the sensor WIDTH. So the rig keeps the filmback aspect equal to the
// viewport aspect (h_sensor fixed, w_sensor = h_sensor * aspect, updated on viewport resize), sets f = h_sensor / (2 tan(V/2))
// and N = f / A (pupil A). The plan's f = w_sensor / (2 tan(H/2)) is the same number only under that condition.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Camera/RbCameraModel.h"

#include "RbCameraRigComponent.generated.h"

class UCineCameraComponent;

UENUM(BlueprintType)
enum class ERbCameraRigMode : uint8
{
	Standing,     // walking around the table: eye at StandingEyeHeight over the capsule
	DownOnShot,   // eye placed from the cue axis (plan 4.2)
	BallInHand,   // standing, looking down at the placement point
	External,     // another actor owns the view (replay camera) - the rig idles
};

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbCameraRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbCameraRigComponent();

	void SetCamera(UCineCameraComponent* InCamera);
	void SetPreset(ERbCameraPreset InPreset);
	ERbCameraPreset GetPreset() const { return Preset; }

	// Mode change with the eased transition (GetDownSeconds).
	void SetMode(ERbCameraRigMode InMode);
	ERbCameraRigMode GetMode() const { return Mode; }

	// Cue axis in WORLD space for DownOnShot placement: point on the cue-ball surface where the tip touches and
	// the butt -> tip unit direction.
	void SetCueAxisWorld(const FVector& ContactPoint, const FVector& Direction);

	// Focus target in world space (object ball on the aim line, else the cue ball).
	void SetFocusTargetWorld(const FVector& Target);

	// Applies the optics of Params (filmback at ViewportAspect, focal length from the vertical FOV, aperture = pupil,
	// exposure / motion blur / grain / vignette post-process) to any cine camera: the player rig AND the look-dev capture
	// cameras (ARbLookDevCamera), so acceptance screenshots show the game's camera model, not engine defaults (R-08).
	static void ApplyPresetToCamera(UCineCameraComponent& Camera, const FRbCameraPresetParams& Params, float ViewportAspect);

	// Current preset parameters (defaults or the data asset).
	const FRbCameraPresetParams& GetParams() const { return Params; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	TObjectPtr<URbCameraModel> ModelOverride;

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	void ApplyOptics();

	TWeakObjectPtr<UCineCameraComponent> Camera;
	ERbCameraPreset Preset = ERbCameraPreset::Eyes;
	ERbCameraRigMode Mode = ERbCameraRigMode::Standing;
	FRbCameraPresetParams Params;
	FVector CueContactWorld = FVector::ZeroVector;
	FVector CueDirectionWorld = FVector::ForwardVector;
	FVector FocusTargetWorld = FVector::ZeroVector;
	double TransitionAlpha = 1.0;
};
