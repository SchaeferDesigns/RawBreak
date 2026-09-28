#pragma once

// First-person camera rig (ue5-realism-plan 4.2-4.8, Docs/ue-architecture.md 6.3). Drives the character's
// UCineCameraComponent: eye placement (standing eye height; down on the shot e = P_axis(s_e) + h_c n_up + y_vc n_side
// from the cue axis), the get-down / stand-up transition, the authored VERTICAL FOV (MaintainYFOV), DoF focus on the
// aim target with an accommodation ease and the pupil-equivalent aperture, exposure / motion blur / grain from the
// preset, and the procedural head motion layer (FRbHeadMotion). Owner: UE-5b.
//
// Vertical FOV on a UCineCameraComponent (review R-06, verified in UE 5.8 CameraStackTypes.cpp): with MaintainYFOV the engine
// derives the vertical FOV from the lens and the camera's OWN aspect ratio (the filmback), V = 2 atan(h_sensor / 2f), and
// the diaphragm DoF scales its circle of confusion by the sensor WIDTH. So the rig keeps the filmback aspect equal to the
// viewport aspect (h_sensor fixed, w_sensor = h_sensor * aspect, updated on viewport resize), sets f = h_sensor / (2 tan(V/2))
// and N = f / A (pupil A). The plan's f = w_sensor / (2 tan(H/2)) is the same number only under that condition.
// bConstrainAspectRatio is off: the projection takes the MaintainYFOV path (V from the filmback, Hor+ on wider screens), so even
// the frame between a resize and the filmback update has the right vertical FOV and never shows black bars.
//
// Placement per mode (all world space, computed every tick in TG_PostPhysics after the character moved):
//   Standing     eye StandingEyeHeight above the capsule bottom, view = control rotation
//   BallInHand   leaning over the table: BallInHandLean forward and down, view = control rotation
//   DownOnShot   eye from the (smoothed) cue axis the stroke component reports: P_axis(s) = C - s u, n_up = unit vector
//                perpendicular to u in the vertical plane of u, n_side = horizontal unit vector to the right of u. View yaw = cue
//                yaw, pitch toward the gaze point (the focus target = object ball on the aim line, else LookAhead along the line),
//                clamped so the cue ball stays in the frame; look input adds gaze offsets (pitch while aiming, yaw + pitch while
//                watching the shot). The control rotation follows the view, so standing up keeps looking where the player looked.
//   External     another actor owns the view (replay camera): the rig idles
// Mode changes blend from the last pose to the (moving) target with a smoothstep over GetDownSeconds (BallInHandSeconds for
// the placement lean). The cue axis is low-passed (CueAxisSmoothingSeconds): the head follows the aim and the slow drift, not
// the 8-12 Hz hand tremor of SampleHand.
//
// Optics every tick: filmback = viewport aspect, focus = accommodation ease in dioptres (exp. time constant FocusEaseSeconds)
// toward the focus target (down) or the first hit along the view axis (standing), film grain from the CURRENT adapted exposure
// (FRbExposureProbe reads the renderer's eye adaptation back on the game thread) through RbCameraMath::ExposureCoupledGrain.
// Comfort (URbGameUserSettings, bApplyUserSettings): camera preset, vertical FOV (Eyes), head bob scale, reduced motion (no
// head motion, no motion blur), motion blur / grain scales, DoF on / off.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Camera/RbCameraModel.h"
#include "Camera/RbHeadMotion.h"

#include "RbCameraRigComponent.generated.h"

class FRbExposureProbe;
class UCineCameraComponent;
class UTexture2D;

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
	UCineCameraComponent* GetCamera() const { return Camera.Get(); }
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

	// Fallback fixation point down on the shot when the reported focus target is not ahead of the cue ball (no object ball known on
	// the line): given the contact point and the butt -> tip direction (world), returns the point the eyes fixate (the first ball on
	// the line, else where the line meets the cushion). The pawn provides it from the match state; unset = LookAhead along the line.
	TFunction<bool(const FVector& /*ContactPoint*/, const FVector& /*Direction*/, FVector& /*OutPoint*/)> FixationResolver;

	// Applies the optics of Params (filmback at ViewportAspect, focal length from the vertical FOV, aperture = pupil,
	// exposure / motion blur / grain / vignette post-process) to any cine camera: the player rig AND the look-dev capture
	// cameras (ARbLookDevCamera), so acceptance screenshots show the game's camera model, not engine defaults (R-08).
	static void ApplyPresetToCamera(UCineCameraComponent& Camera, const FRbCameraPresetParams& Params, float ViewportAspect);

	// Current preset parameters (defaults or the data asset).
	const FRbCameraPresetParams& GetParams() const { return Params; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	TObjectPtr<URbCameraModel> ModelOverride;

	// Read URbGameUserSettings at BeginPlay (camera preset, FOV, comfort). Tests switch it off for deterministic defaults.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	bool bApplyUserSettings = true;

	// --- input and settings ------------------------------------------------------------------------------

	// Vertical FOV [deg] replacing the preset's (settings slider 40-75, Eyes only); <= 0 = the preset's own.
	void SetVerticalFovOverride(double VerticalDeg);
	// Eye / head rotation from look input while down [deg]: pitch while aiming (the eyes run along the line), yaw and pitch while
	// watching the shot. Reset by every new get-down. Clamped to +-70 deg yaw, +-35 deg pitch.
	void AddGazeInput(double YawDeg, double PitchDeg);
	// Settle (HF-06 exhale and hold): breathing and sway fade by SettleReduction over SettleSeconds while held down on the shot.
	void SetSettleHeld(bool bHeld);
	// Comfort scales (plan 4.8 / 9.4): head motion (0 = off), motion blur, grain, DoF.
	void SetComfort(double InMotionScale, double InMotionBlurScale, double InGrainScale, bool bInDepthOfField);
	// Re-reads URbGameUserSettings (preset, FOV, comfort).
	void ApplyUserSettings();
	// Viewport aspect W / H the filmback follows; > 0 overrides the owning player's viewport (tests, tools).
	void SetViewportAspectOverride(double Aspect);
	double GetViewportAspect() const { return ViewportAspect; }

	// Advances the rig by DeltaSeconds (TickComponent calls it; tests drive it directly).
	void TickRig(double DeltaSeconds);

	// --- state (tests, dev dump) ---------------------------------------------------------------------------

	// Eye pose of the mode incl. the transition, before the head motion layer.
	FTransform GetBaseEyeTransform() const { return FTransform(BlendedRot, BlendedEye); }
	double GetTransitionAlpha() const { return TransitionAlpha; }
	double GetFocusDistanceCm() const { return FocusCm; }
	// EV100 of the last rendered frame of the player's view (< -50 = unknown: no view rendered yet).
	double GetCurrentEv100() const { return CurrentEv100; }
	double GetGrainIntensity() const { return GrainIntensity; }
	double GetGazeYaw() const { return GazeYawDeg; }
	double GetGazePitch() const { return GazePitchDeg; }
	double GetSettleAlpha() const { return SettleAlpha; }
	const FRbHeadMotion& GetHeadMotion() const { return HeadMotion; }
	const FRbHeadMotionSample& GetHeadMotionSample() const { return HeadSample; }
	// The parameters in effect (preset + FOV override).
	FRbCameraPresetParams GetEffectiveParams() const;

	// --- pure geometry / optics (plan 4.2, 4.4, 4.5) ---------------------------------------------------------

	// e = P_axis(s_e) + h_c n_up + y_vc n_side for the cue axis through ContactPoint with butt -> tip Direction (world, cm).
	static FVector ComputeDownOnShotEye(const FVector& ContactPoint, const FVector& Direction, const FRbCameraPresetParams& Params);
	// View of the eye down on the shot: yaw of the cue, pitch toward GazePoint measured in the vertical plane of the cue, no roll.
	static FQuat ComputeDownOnShotView(const FVector& Eye, const FVector& Direction, const FVector& GazePoint);
	// Accommodation: exponential ease in dioptres (1 / distance), exact for any frame split at a constant target.
	static double EaseFocusCm(double CurrentCm, double TargetCm, double DeltaSeconds, double TauSeconds);
	// EV100 of a linear eye-adaptation exposure (UE: exposure = 2^compensation / (1.2 * 2^EV100)).
	static double Ev100FromExposure(double Exposure, double Compensation);
	// Centre-weighted metering mask (gaussian of Sigma in image half-sizes, 5 % floor), one transient texture per sigma.
	static UTexture2D* GetMeteringMask(double Sigma);

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

private:
	void UpdateViewportAspect();
	void ComputeModePose(ERbCameraRigMode ForMode, FVector& OutEye, FQuat& OutRot) const;
	FVector StandingEye() const;
	FQuat ControlQuat() const;
	double TransitionSecondsFor(ERbCameraRigMode From, ERbCameraRigMode To) const;
	void UpdateCueAxisSmoothing(double DeltaSeconds);
	FVector DownFixation() const;
	double TraceFocusCm(const FVector& Eye, const FVector& Forward) const;
	void UpdateFocus(double DeltaSeconds, const FVector& Eye, const FQuat& Rot);
	void UpdateGrain();
	void SyncControlRotation(const FQuat& Rot) const;

	double VerticalFovOverride = 0.0;
	double ViewportAspectOverride = 0.0;
	double ViewportAspect = 16.0 / 9.0;
	bool bOpticsApplied = false;

	// Cue axis: raw (stroke component) and smoothed (what the eye follows).
	bool bHasCueAxis = false;
	bool bSmoothedAxisValid = false;
	FVector SmoothedContact = FVector::ZeroVector;
	FVector SmoothedDirection = FVector::ForwardVector;
	bool bHasFocusTarget = false;

	// Transition and last pose (before head motion).
	ERbCameraRigMode FromMode = ERbCameraRigMode::Standing;
	double TransitionSeconds = 1.0;
	bool bHasPose = false;
	FVector FromEye = FVector::ZeroVector;
	FQuat FromRot = FQuat::Identity;
	FVector BlendedEye = FVector::ZeroVector;
	FQuat BlendedRot = FQuat::Identity;

	double GazeYawDeg = 0.0;
	double GazePitchDeg = 0.0;
	bool bSettleHeld = false;
	double SettleAlpha = 0.0;

	double FocusCm = 0.0;           // eased focus distance (0 = not yet set)
	double CurrentEv100 = -100.0;
	double GrainIntensity = 0.0;

	double MotionScale = 1.0;       // comfort: head motion
	double MotionBlurScale = 1.0;
	double GrainScale = 1.0;
	bool bDepthOfField = true;

	FRbHeadMotion HeadMotion;
	FRbHeadMotionSample HeadSample;
	TSharedPtr<FRbExposureProbe, ESPMode::ThreadSafe> ExposureProbe;
};
