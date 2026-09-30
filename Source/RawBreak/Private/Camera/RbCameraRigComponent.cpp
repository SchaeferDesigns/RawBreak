#include "Camera/RbCameraRigComponent.h"

#include "RbExposureProbe.h"
#include "Math/RbCameraMath.h"
#include "Settings/RbGameUserSettings.h"

#include "rb/Human/NoiseHash.h"

#include "CineCameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SceneViewExtension.h"
#include "UObject/Package.h"

// Owner: UE-5b, M2-F. Eye placement + human posture changes, vertical FOV through the filmback, pupil DoF with the accommodation
// ease, exposure / motion blur / grain / vignette / CA / bloom from the preset, the human motion layer (FRbHumanMotion: breathing,
// sway, walking bob + footsteps, reactions, flinch), gaze follower, comfort settings (header). Tests: RawBreak.Unit.Camera.*
// (Private/Tests/RbCameraRigTests.cpp), RawBreak.Unit.HumanMotion.* / RawBreak.Unit.Feel.* (M2-F).

namespace
{
	constexpr double kCmPerM = 100.0;
	constexpr double kMaxGazeYawDeg = 70.0;
	constexpr double kMaxGazePitchDeg = 35.0;
	constexpr double kCueBallFrameMarginDeg = 6.0;  // the cue ball stays this far inside the frame when looking down the line
	constexpr double kMinFocusCm = 5.0;             // lens minimum focus distance (50 mm)
	constexpr double kMaxFocusCm = 3000.0;          // "infinity" of the focus trace
	constexpr double kAxisSnapCm = 20.0;            // a new address far from the last one snaps the smoothed axis
	constexpr double kGazeOnLineDeg = 1.5;          // gaze offsets below this still fixate the line's fixation point
	constexpr double kGazeStep = 1.0 / 480.0;       // gaze follower sub-step (frame-rate independent)
	constexpr double kFootSideCm = 10.0;            // a foot lands this far left / right of the pawn's centre line

	FVector HorizontalUnit(const FVector& V, const FVector& Fallback = FVector::ForwardVector)
	{
		const FVector H(V.X, V.Y, 0.0);
		return H.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? H.GetUnsafeNormal() : Fallback;
	}

	// One transient mask texture per sigma, kept alive by the cameras that reference it (weak cache).
	TMap<int32, TWeakObjectPtr<UTexture2D>>& MaskCache()
	{
		static TMap<int32, TWeakObjectPtr<UTexture2D>> Cache;
		return Cache;
	}
}

URbCameraRigComponent::URbCameraRigComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after the character moved and the stroke component pushed the cue axis
	Params = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
}

// ---------------------------------------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------------------------------------

void URbCameraRigComponent::SetCamera(UCineCameraComponent* InCamera)
{
	Camera = InCamera;
	if (InCamera)
	{
		// The rig owns the camera's world transform (no pawn control rotation: it would overwrite the view down on the shot).
		InCamera->bUsePawnControlRotation = false;
		InCamera->SetUsingAbsoluteLocation(true);
		InCamera->SetUsingAbsoluteRotation(true);
	}
	bOpticsApplied = false;
	ApplyOptics();
}

void URbCameraRigComponent::SetPreset(ERbCameraPreset InPreset)
{
	Preset = InPreset;
	Params = ModelOverride ? ModelOverride->Get(InPreset) : RbCameraModel::Defaults(InPreset);
	ApplyOptics();
}

FRbCameraPresetParams URbCameraRigComponent::GetEffectiveParams() const
{
	FRbCameraPresetParams P = Params;
	if (VerticalFovOverride > 0.0 && Preset == ERbCameraPreset::Eyes)
	{
		P.VerticalFovDeg = FMath::Clamp(VerticalFovOverride, MinVerticalFovDeg, MaxVerticalFovDeg);
	}
	return P;
}

void URbCameraRigComponent::SetVerticalFovOverride(double VerticalDeg)
{
	VerticalFovOverride = VerticalDeg;
	ApplyOptics();
}

void URbCameraRigComponent::SetComfort(double InMotionScale, double InMotionBlurScale, double InGrainScale, bool bInDepthOfField)
{
	MotionScale = FMath::Max(0.0, InMotionScale);
	MotionBlurScale = FMath::Max(0.0, InMotionBlurScale);
	GrainScale = FMath::Max(0.0, InGrainScale);
	bDepthOfField = bInDepthOfField;
	ApplyOptics();
}

void URbCameraRigComponent::SetMotionComfort(double InHeadBobScale, double InBodySwayScale, double InMountShakeScale, ERbPostureTransition InTransition)
{
	HeadBobScale = FMath::Clamp(InHeadBobScale, 0.0, 1.0);
	BodySwayScale = FMath::Clamp(InBodySwayScale, 0.0, 1.0);
	MountShakeScale = FMath::Clamp(InMountShakeScale, 0.0, 1.0);
	PostureTransition = InTransition;
}

void URbCameraRigComponent::ApplyUserSettings()
{
	const URbGameUserSettings* Settings = URbGameUserSettings::Get();
	if (!Settings)
	{
		return;
	}
	Preset = Settings->CameraPreset == ERbCameraPreset::Broadcast ? ERbCameraPreset::Eyes : Settings->CameraPreset; // Broadcast = replays only
	Params = ModelOverride ? ModelOverride->Get(Preset) : RbCameraModel::Defaults(Preset);
	VerticalFovOverride = Settings->VerticalFovDeg;
	const bool bReduced = Settings->bReducedMotion;
	// Reduced motion: no continuous human motion, posture changes as a short plain ease (Cut stays a cut), no motion blur.
	const ERbPostureTransition Transition = bReduced && Settings->Camera.PostureTransition == ERbPostureTransition::Natural
		? ERbPostureTransition::Quick : Settings->Camera.PostureTransition;
	SetMotionComfort(Settings->HeadBobScale, Settings->Camera.BodySwayScale, Settings->Camera.MountShakeScale, Transition);
	SetComfort(bReduced ? 0.0 : 1.0, bReduced ? 0.0 : Settings->MotionBlurScale, Settings->GrainScale, Settings->bDepthOfField);
}

void URbCameraRigComponent::SetViewportAspectOverride(double Aspect)
{
	ViewportAspectOverride = Aspect;
	UpdateViewportAspect();
}

void URbCameraRigComponent::SetMode(ERbCameraRigMode InMode)
{
	if (Mode == InMode)
	{
		return;
	}
	FromMode = Mode;
	Mode = InMode;
	if (InMode == ERbCameraRigMode::DownOnShot)
	{
		// A new address: the eyes look straight down the new line, the smoothed axis restarts at the new one.
		GazeYawDeg = GazePitchDeg = 0.0;
		ViewGazeYawDeg = ViewGazePitchDeg = 0.0;
		GazeVelocity = FVector2D::ZeroVector;
		bSmoothedAxisValid = false;
	}
	if (InMode != ERbCameraRigMode::DownOnShot)
	{
		bSettleHeld = false;
		SetWatching(false);
	}
	if (!bHasPose || InMode == ERbCameraRigMode::External || FromMode == ERbCameraRigMode::External)
	{
		// The first pose, or another actor owns / owned the view: a cut.
		bPostureRunning = false;
		TransitionAlpha = 1.0;
		return;
	}
	// The human movement between the two postures, seeded per change (never two identical get-downs).
	ERbPostureChange Change = ERbPostureChange::GetDown;
	if (InMode == ERbCameraRigMode::DownOnShot)
	{
		Change = ERbPostureChange::GetDown;
	}
	else if (FromMode == ERbCameraRigMode::DownOnShot)
	{
		Change = ERbPostureChange::StandUp;
	}
	else
	{
		Change = InMode == ERbCameraRigMode::BallInHand ? ERbPostureChange::LeanOver : ERbPostureChange::StraightenUp;
	}
	++PostureCounter;
	const uint64 Seed = rb::human::HashKeys(MotionSeed, static_cast<uint64>(PostureCounter), static_cast<uint64>(Change));
	HumanMotion.BeginPostureChange(Change, FTransform(BlendedRot, BlendedEye), FTransform(BlendedRot, BlendedEye), Seed, PostureTransition);
	PostureTime = 0.0;
	bPostureRunning = HumanMotion.GetPostureChangeSeconds() > 0.0;
	TransitionAlpha = bPostureRunning ? 0.0 : 1.0;
}

double URbCameraRigComponent::GetPostureArrivalSeconds() const
{
	return bPostureRunning ? HumanMotion.GetPostureArrivalSeconds() : 0.0;
}

void URbCameraRigComponent::SetWatching(bool bInWatching)
{
	if (bWatching == bInWatching)
	{
		return;
	}
	bWatching = bInWatching;
	bReactionOriginValid = false;
	bHasReactionTarget = false;
	// No hard reset of the reaction: when the watching ends (standing up, the next address) the head's pursuit relaxes back from
	// where it is (critically damped, FRbHumanMotion::StepReaction) - zeroing it here would pop the view by the reaction angle.
}

void URbCameraRigComponent::SetReactionTargetWorld(const FVector& Target)
{
	ReactionTargetWorld = Target;
	bHasReactionTarget = true;
}

void URbCameraRigComponent::SetCueAxisWorld(const FVector& ContactPoint, const FVector& Direction)
{
	CueContactWorld = ContactPoint;
	CueDirectionWorld = Direction.GetSafeNormal();
	if (CueDirectionWorld.IsNearlyZero())
	{
		CueDirectionWorld = FVector::ForwardVector;
	}
	bHasCueAxis = true;
}

void URbCameraRigComponent::SetFocusTargetWorld(const FVector& Target)
{
	FocusTargetWorld = Target;
	bHasFocusTarget = true;
}

void URbCameraRigComponent::AddGazeInput(double YawDeg, double PitchDeg)
{
	GazeYawDeg = FMath::Clamp(GazeYawDeg + YawDeg, -kMaxGazeYawDeg, kMaxGazeYawDeg);
	GazePitchDeg = FMath::Clamp(GazePitchDeg + PitchDeg, -kMaxGazePitchDeg, kMaxGazePitchDeg);
}

void URbCameraRigComponent::SetSettleHeld(bool bHeld)
{
	bSettleHeld = bHeld && Mode == ERbCameraRigMode::DownOnShot;
}

// ---------------------------------------------------------------------------------------------------------
// Lifetime and tick
// ---------------------------------------------------------------------------------------------------------

void URbCameraRigComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bApplyUserSettings)
	{
		ApplyUserSettings();
		// The settings menu applies live: re-read on every change (never cache across a broadcast, 18.4).
		SettingsHandle = URbGameUserSettings::OnSettingsChanged().AddWeakLambda(this, [this]() { ApplyUserSettings(); });
	}
	SetPreset(Preset);
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && World->GetGameViewport() && GEngine)
	{
		ExposureProbe = FSceneViewExtensions::NewExtension<FRbExposureProbe>(World, GetOwner());
	}
	TickRig(0.0); // place the camera before the first frame renders
}

void URbCameraRigComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SettingsHandle.IsValid())
	{
		URbGameUserSettings::OnSettingsChanged().Remove(SettingsHandle);
		SettingsHandle.Reset();
	}
	ExposureProbe.Reset(); // unregisters the view extension
	Super::EndPlay(EndPlayReason);
}

void URbCameraRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickRig(DeltaTime);
}

void URbCameraRigComponent::TickRig(double DeltaSeconds)
{
	UCineCameraComponent* Cam = Camera.Get();
	if (!Cam || Mode == ERbCameraRigMode::External)
	{
		return;
	}
	const double Dt = FMath::Max(0.0, DeltaSeconds);
	UpdateViewportAspect();
	UpdateCueAxisSmoothing(Dt);
	UpdateGazeFollower(Dt);

	// 1. Pose of the mode; a running posture change carries the eye there along its human path (the target may move).
	FVector Eye;
	FQuat Rot;
	ComputeModePose(Mode, Eye, Rot);
	if (bPostureRunning && bHasPose)
	{
		PostureTime += Dt;
		FTransform Out;
		bPostureRunning = HumanMotion.EvaluatePostureChange(PostureTime, FTransform(Rot, Eye), Out);
		Eye = Out.GetLocation();
		Rot = Out.GetRotation();
		const double Total = HumanMotion.GetPostureChangeSeconds();
		TransitionAlpha = bPostureRunning && Total > 0.0 ? FMath::Clamp(PostureTime / Total, 0.0, 1.0) : 1.0;
	}
	else
	{
		bPostureRunning = false;
		TransitionAlpha = 1.0;
	}
	BlendedEye = Eye;
	BlendedRot = Rot;
	bHasPose = true;
	if (Mode == ERbCameraRigMode::DownOnShot)
	{
		SyncControlRotation(Rot);
	}

	// 2. Focus (dioptre ease) on the base view; it is also the fixation distance of the gaze stabilisation.
	UpdateFocus(Dt, Eye, Rot);

	// 3. Human motion layer (plan 4.8, P5): translation in the yaw-only frame; Eyes keep the fixation point (VOR) and add the
	// deliberate reactions on top, Headcam nods / rolls.
	const bool bDown = Mode == ERbCameraRigMode::DownOnShot;
	const double SettleRate = Params.HeadMotion.SettleSeconds > 0.0 ? Dt / Params.HeadMotion.SettleSeconds : 1.0;
	SettleAlpha = FMath::Clamp(SettleAlpha + (bSettleHeld && bDown ? SettleRate : -SettleRate), 0.0, 1.0);
	const AActor* Owner = GetOwner();
	FRbHumanMotionInputs In;
	In.DeltaSeconds = Dt;
	In.WalkSpeedMps = (Owner && !bDown) ? Owner->GetVelocity().Size2D() / kCmPerM : 0.0;
	In.bDown = bDown;
	In.bWatching = bDown && bWatching;
	In.Pressure = Pressure;
	In.SettleAlpha = SettleAlpha;
	In.MotionScale = MotionScale;
	In.Preset = Preset;
	In.HeadBobScale = HeadBobScale;
	In.BodySwayScale = BodySwayScale;
	In.MountShakeScale = MountShakeScale;
	if (In.bWatching && ReactionResolver)
	{
		FVector Resolved;
		if (ReactionResolver(Resolved))
		{
			SetReactionTargetWorld(Resolved);
		}
	}
	if (In.bWatching && bHasReactionTarget)
	{
		const FVector2D Angles = ReactionAnglesDeg(Eye, Rot);
		if (!bReactionOriginValid)
		{
			ReactionOrigin = Angles;
			bReactionOriginValid = true;
		}
		In.bHasReactionTarget = true;
		In.ReactionAnglesDeg = Angles - ReactionOrigin;
	}
	HumanSample = HumanMotion.Step(In, Params);
	const FQuat YawOnly = FRotator(0.0, Rot.Rotator().Yaw, 0.0).Quaternion();
	const FVector FinalEye = Eye + YawOnly.RotateVector(HumanSample.Offset);
	FQuat FinalRot = Rot;
	FixationWorld = Eye + Rot.GetForwardVector() * FMath::Clamp(FocusCm > 0.0 ? FocusCm : 200.0, 30.0, 2000.0);
	if (Params.bStabiliseGaze)
	{
		const FVector Dir = FixationWorld - FinalEye;
		if (!Dir.IsNearlyZero())
		{
			FRotator View = Dir.Rotation();
			View.Roll = Rot.Rotator().Roll;
			FinalRot = View.Quaternion();
		}
		FinalRot = FinalRot * HumanSample.Reaction.Quaternion();
	}
	else
	{
		FinalRot = Rot * HumanSample.Rotation.Quaternion();
	}
	Cam->SetWorldLocationAndRotation(FinalEye, FinalRot);

	// 4. Footsteps (audio, M2-C): under the foot that struck, on the floor below the capsule.
	if (HumanSample.bFootstep && OnFootstep.IsBound() && Owner)
	{
		FVector Floor = Owner->GetActorLocation();
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Owner->GetRootComponent()))
		{
			Floor.Z -= Capsule->GetScaledCapsuleHalfHeight();
		}
		const FVector Right = YawOnly.GetRightVector();
		FRbFootstep Step;
		Step.bLeftFoot = HumanSample.bLeftFoot;
		Step.WorldLocation = Floor + Right * (Step.bLeftFoot ? -kFootSideCm : kFootSideCm);
		Step.SpeedMps = static_cast<float>(HumanMotion.GetSmoothedSpeed());
		OnFootstep.Broadcast(Step);
	}

	// 5. Exposure-coupled grain.
	UpdateGrain();
}

void URbCameraRigComponent::UpdateGazeFollower(double DeltaSeconds)
{
	// The head turns toward the gaze targets like a mass on a critically damped spring (no jump, the same path at any frame rate).
	const double Omega = UE_DOUBLE_TWO_PI * FMath::Max(0.1, bWatching ? WatchGazeFollowHz : GazeFollowHz);
	const FVector2D Target(GazeYawDeg, GazePitchDeg);
	FVector2D View(ViewGazeYawDeg, ViewGazePitchDeg);
	GazeStepRemainder += DeltaSeconds;
	while (GazeStepRemainder >= kGazeStep)
	{
		GazeStepRemainder -= kGazeStep;
		const FVector2D Accel = (Target - View) * (Omega * Omega) - GazeVelocity * (2.0 * Omega);
		GazeVelocity += Accel * kGazeStep;
		View += GazeVelocity * kGazeStep;
	}
	if (FVector2D::DistSquared(View, Target) < 1.0e-12 && GazeVelocity.SizeSquared() < 1.0e-10)
	{
		View = Target;
		GazeVelocity = FVector2D::ZeroVector;
	}
	ViewGazeYawDeg = View.X;
	ViewGazePitchDeg = View.Y;
}

FVector2D URbCameraRigComponent::ReactionAnglesDeg(const FVector& Eye, const FQuat& Rot) const
{
	// Direction of the reaction target in the base view's frame: yaw right, pitch up.
	const FVector Local = Rot.UnrotateVector(ReactionTargetWorld - Eye);
	if (Local.X <= 1.0)
	{
		return bReactionOriginValid ? ReactionOrigin : FVector2D::ZeroVector; // behind the head: no pursuit
	}
	return FVector2D(FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)),
		FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FMath::Sqrt(Local.X * Local.X + Local.Y * Local.Y))));
}

// ---------------------------------------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------------------------------------

FVector URbCameraRigComponent::ComputeDownOnShotEye(const FVector& ContactPoint, const FVector& Direction, const FRbCameraPresetParams& P)
{
	const FVector U = Direction.GetSafeNormal();
	// n_up: perpendicular to u in the vertical plane containing u (pointing up); n_side: horizontal, to the right of u.
	FVector Up = FVector::UpVector - FVector::DotProduct(FVector::UpVector, U) * U;
	Up = Up.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? Up.GetUnsafeNormal() : FVector::ForwardVector;
	FVector Side = FVector::CrossProduct(FVector::UpVector, U);
	Side = Side.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? Side.GetUnsafeNormal() : FVector::RightVector;
	const FVector PAxis = ContactPoint - U * (P.EyeBehindTipM * kCmPerM);
	return PAxis + Up * (P.EyeAboveCueM * kCmPerM) + Side * (P.VisionCenterM * kCmPerM);
}

FQuat URbCameraRigComponent::ComputeDownOnShotView(const FVector& Eye, const FVector& Direction, const FVector& GazePoint)
{
	const FVector UH = HorizontalUnit(Direction);
	const FVector ToGaze = GazePoint - Eye;
	const double Along = FMath::Max(FVector::DotProduct(ToGaze, UH), 1.0);
	const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(ToGaze.Z, Along));
	return FRotator(Pitch, UH.Rotation().Yaw, 0.0).Quaternion();
}

FVector URbCameraRigComponent::StandingEye() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector(0.0, 0.0, Params.StandingEyeHeightM * kCmPerM);
	}
	FVector Base = Owner->GetActorLocation();
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Owner->GetRootComponent()))
	{
		Base.Z -= Capsule->GetScaledCapsuleHalfHeight(); // the floor under the capsule
	}
	return Base + FVector(0.0, 0.0, Params.StandingEyeHeightM * kCmPerM);
}

FQuat URbCameraRigComponent::ControlQuat() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Pawn->GetViewRotation().Quaternion();
	}
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorQuat() : FQuat::Identity;
}

void URbCameraRigComponent::ComputeModePose(ERbCameraRigMode ForMode, FVector& OutEye, FQuat& OutRot) const
{
	switch (ForMode)
	{
	case ERbCameraRigMode::DownOnShot:
		if (bSmoothedAxisValid)
		{
			const FVector U = SmoothedDirection;
			const FVector UH = HorizontalUnit(U);
			OutEye = ComputeDownOnShotEye(SmoothedContact, U, Params);
			FRotator View = ComputeDownOnShotView(OutEye, U, DownFixation()).Rotator();
			// Keep the cue ball in the frame (V/2 minus a margin below the view axis).
			const FRotator ToBall = ComputeDownOnShotView(OutEye, U, SmoothedContact).Rotator();
			const double HalfV = 0.5 * GetEffectiveParams().VerticalFovDeg;
			View.Pitch = FMath::Min(View.Pitch, ToBall.Pitch + HalfV - kCueBallFrameMarginDeg);
			View.Pitch = FMath::Clamp(View.Pitch + ViewGazePitchDeg, -89.0, 89.0);
			View.Yaw += ViewGazeYawDeg;
			OutRot = View.Quaternion();
			return;
		}
		// No cue axis yet: hold the last pose.
		OutEye = bHasPose ? BlendedEye : StandingEye();
		OutRot = bHasPose ? BlendedRot : ControlQuat();
		return;

	case ERbCameraRigMode::BallInHand:
	{
		OutRot = ControlQuat();
		const FVector Forward = HorizontalUnit(OutRot.GetForwardVector());
		const double Lean = Params.BallInHandLeanM * kCmPerM;
		OutEye = StandingEye() + Forward * Lean - FVector::UpVector * Lean;
		return;
	}

	case ERbCameraRigMode::Standing:
	case ERbCameraRigMode::External:
	default:
		OutEye = StandingEye();
		OutRot = ControlQuat();
		return;
	}
}

FVector URbCameraRigComponent::DownFixation() const
{
	// The object ball on the line (the stroke component's focus target) when it lies ahead of the cue ball, else the pawn's
	// resolver (first ball on the line / the cushion), else a point LookAhead down the line at the height of the contact.
	const FVector UH = HorizontalUnit(SmoothedDirection);
	if (bHasFocusTarget && FVector::DotProduct(FocusTargetWorld - SmoothedContact, UH) > 10.0)
	{
		return FocusTargetWorld;
	}
	FVector Resolved;
	if (FixationResolver && FixationResolver(SmoothedContact, SmoothedDirection, Resolved) && FVector::DotProduct(Resolved - SmoothedContact, UH) > 10.0)
	{
		return Resolved;
	}
	return SmoothedContact + UH * (Params.LookAheadM * kCmPerM);
}

void URbCameraRigComponent::UpdateCueAxisSmoothing(double DeltaSeconds)
{
	if (!bHasCueAxis)
	{
		return;
	}
	if (!bSmoothedAxisValid || FVector::Dist(SmoothedContact, CueContactWorld) > kAxisSnapCm)
	{
		SmoothedContact = CueContactWorld;
		SmoothedDirection = CueDirectionWorld;
		bSmoothedAxisValid = true;
		return;
	}
	const double Tau = Params.CueAxisSmoothingSeconds;
	const double A = Tau > 0.0 ? 1.0 - FMath::Exp(-DeltaSeconds / Tau) : 1.0;
	SmoothedContact = FMath::Lerp(SmoothedContact, CueContactWorld, A);
	SmoothedDirection = FMath::Lerp(SmoothedDirection, CueDirectionWorld, A).GetSafeNormal();
	if (SmoothedDirection.IsNearlyZero())
	{
		SmoothedDirection = CueDirectionWorld;
	}
}

void URbCameraRigComponent::SyncControlRotation(const FQuat& Rot) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AController* Controller = Pawn ? Pawn->GetController() : nullptr)
	{
		FRotator R = Rot.Rotator();
		R.Roll = 0.0;
		Controller->SetControlRotation(R);
	}
}

// ---------------------------------------------------------------------------------------------------------
// Optics
// ---------------------------------------------------------------------------------------------------------

double URbCameraRigComponent::EaseFocusCm(double CurrentCm, double TargetCm, double DeltaSeconds, double TauSeconds)
{
	TargetCm = FMath::Clamp(TargetCm, kMinFocusCm, kMaxFocusCm);
	if (CurrentCm <= 0.0 || TauSeconds <= 0.0)
	{
		return TargetCm;
	}
	// Accommodation changes the eye's refractive power: ease linearly in dioptres, exponentially in time.
	const double D0 = 1.0 / CurrentCm;
	const double D1 = 1.0 / TargetCm;
	const double D = D1 + (D0 - D1) * FMath::Exp(-FMath::Max(0.0, DeltaSeconds) / TauSeconds);
	return 1.0 / D;
}

double URbCameraRigComponent::Ev100FromExposure(double Exposure, double Compensation, double LuminanceMax)
{
	// Eye adaptation (UE 5.8): the average luminance maps to 0.18 of the white point L_white = LuminanceMax 2^EV100, and the stored
	// exposure is 2^compensation / L_white.
	return (Exposure > 0.0 && LuminanceMax > 0.0) ? Compensation - FMath::Log2(LuminanceMax * Exposure) : -100.0;
}

double URbCameraRigComponent::EyeAdaptationLuminanceMax()
{
	// Mirrors the renderer's LuminanceMaxFromLensAttenuation (PostProcessEyeAdaptation.cpp, private to the Renderer module).
	static IConsoleVariable* const Extended = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
	static IConsoleVariable* const LensAttenuation = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.LensAttenuation"));
	if (!Extended || Extended->GetInt() != 1)
	{
		return 1.0;
	}
	constexpr double kIsoSaturationSpeedConstant = 0.78; // ISO 12232:2006
	const double Q = LensAttenuation ? static_cast<double>(LensAttenuation->GetFloat()) : 0.78;
	return kIsoSaturationSpeedConstant / FMath::Max(Q, 0.01);
}

double URbCameraRigComponent::TraceFocusCm(const FVector& Eye, const FVector& Forward) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return kMaxFocusCm;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RbCameraFocus), true, GetOwner());
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Eye, Eye + Forward * kMaxFocusCm, ECC_Visibility, Query))
	{
		return FMath::Max(Hit.Distance, kMinFocusCm);
	}
	return kMaxFocusCm;
}

void URbCameraRigComponent::UpdateFocus(double DeltaSeconds, const FVector& Eye, const FQuat& Rot)
{
	const FVector Forward = Rot.GetForwardVector();
	double Target = kMaxFocusCm;
	// Down on the shot the eyes rest on the fixation point (the object ball on the line) until the gaze input turns them away: then
	// they focus on what they look at, like standing. (The fixation plane alone would collapse toward the eye when the head turns:
	// cos 60 deg halves the distance and blurs the whole scene while the player watches the balls after the shot.)
	const bool bGazeOnLine = FMath::Square(GazeYawDeg) + FMath::Square(GazePitchDeg) <= FMath::Square(kGazeOnLineDeg);
	if (Mode == ERbCameraRigMode::DownOnShot && bSmoothedAxisValid && bGazeOnLine)
	{
		Target = FVector::DotProduct(DownFixation() - Eye, Forward); // focus plane through the fixation point (thin lens)
	}
	else
	{
		Target = TraceFocusCm(Eye, Forward);
	}
	FocusCm = EaseFocusCm(FocusCm, Target, DeltaSeconds, Params.FocusEaseSeconds);
	if (UCineCameraComponent* Cam = Camera.Get())
	{
		Cam->FocusSettings.ManualFocusDistance = static_cast<float>(FocusCm);
	}
}

void URbCameraRigComponent::UpdateGrain()
{
	UCineCameraComponent* Cam = Camera.Get();
	if (!Cam)
	{
		return;
	}
	const double Exposure = ExposureProbe.IsValid() ? ExposureProbe->GetLastExposure() : 0.0;
	CurrentEv100 = Ev100FromExposure(Exposure, Params.ExposureCompensation);
	const double Base = Exposure > 0.0 ? RbCameraMath::ExposureCoupledGrain(Params.GrainG0, Params.GrainEvRef, CurrentEv100, Params.GrainMax)
									   : Params.GrainG0;
	GrainIntensity = Base * GrainScale;
	Cam->PostProcessSettings.bOverride_FilmGrainIntensity = true;
	Cam->PostProcessSettings.FilmGrainIntensity = static_cast<float>(GrainIntensity);
}

void URbCameraRigComponent::UpdateViewportAspect()
{
	double Aspect = ViewportAspectOverride;
	if (Aspect <= 0.0)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		int32 X = 0;
		int32 Y = 0;
		if (PC)
		{
			PC->GetViewportSize(X, Y);
		}
		Aspect = (X > 0 && Y > 0) ? static_cast<double>(X) / static_cast<double>(Y) : ViewportAspect;
	}
	if (Aspect > 0.0 && (!bOpticsApplied || !FMath::IsNearlyEqual(Aspect, ViewportAspect, 1.0e-4)))
	{
		ViewportAspect = Aspect;
		ApplyOptics();
	}
}

void URbCameraRigComponent::ApplyOptics()
{
	UCineCameraComponent* Cam = Camera.Get();
	if (!Cam)
	{
		return;
	}
	const FRbCameraPresetParams P = GetEffectiveParams();
	ApplyPresetToCamera(*Cam, P, static_cast<float>(ViewportAspect));
	// Comfort scales on top of the preset.
	FPostProcessSettings& PP = Cam->PostProcessSettings;
	PP.MotionBlurAmount = static_cast<float>(P.ShutterAngleDeg / 360.0 * MotionBlurScale);
	PP.FilmGrainIntensity = static_cast<float>(GrainIntensity > 0.0 ? GrainIntensity : P.GrainG0 * GrainScale);
	FCameraFocusSettings Focus = Cam->FocusSettings;
	Focus.FocusMethod = bDepthOfField ? ECameraFocusMethod::Manual : ECameraFocusMethod::Disable;
	if (FocusCm > 0.0)
	{
		Focus.ManualFocusDistance = static_cast<float>(FocusCm);
	}
	Cam->SetFocusSettings(Focus);
	bOpticsApplied = true;
}

UTexture2D* URbCameraRigComponent::GetMeteringMask(double Sigma)
{
	Sigma = FMath::Clamp(Sigma, 0.05, 10.0);
	const int32 Key = FMath::RoundToInt32(Sigma * 1000.0);
	if (const TWeakObjectPtr<UTexture2D>* Cached = MaskCache().Find(Key))
	{
		if (UTexture2D* Texture = Cached->Get())
		{
			return Texture;
		}
	}
	constexpr int32 N = 64;
	constexpr double Floor = 0.05; // the periphery still counts a little
	UTexture2D* Texture = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(),
		*FString::Printf(TEXT("T_RbMeterMask_%d"), Key)));
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const double U = (X + 0.5) / N * 2.0 - 1.0; // image half-sizes, 0 at the centre
			const double V = (Y + 0.5) / N * 2.0 - 1.0;
			const double W = Floor + (1.0 - Floor) * FMath::Exp(-(U * U + V * V) / (2.0 * Sigma * Sigma));
			const uint8 Byte = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(W * 255.0), 0, 255));
			Pixels[Y * N + X] = FColor(Byte, Byte, Byte, 255);
		}
	}
	Mip.BulkData.Unlock();
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->UpdateResource();
	MaskCache().Add(Key, Texture);
	return Texture;
}

void URbCameraRigComponent::ApplyPresetToCamera(UCineCameraComponent& Camera, const FRbCameraPresetParams& Params, float ViewportAspect)
{
	const double Aspect = ViewportAspect > 0.0f ? ViewportAspect : 16.0 / 9.0;

	// Lens limits wide enough for every preset (the component clamps f and N into them).
	FCameraLensSettings Lens = Camera.LensSettings;
	Lens.MinFocalLength = 1.0f;
	Lens.MaxFocalLength = 2000.0f;
	Lens.MinFStop = 1.0f;
	Lens.MaxFStop = 64.0f;
	Lens.MinimumFocusDistance = 10.0f * static_cast<float>(kMinFocusCm); // [mm]
	Lens.SqueezeFactor = 1.0f;
	Lens.DiaphragmBladeCount = 16; // a round pupil
	Camera.SetLensSettings(Lens);

	FPlateCropSettings Crop;
	Crop.AspectRatio = 0.0f;
	Camera.SetCropSettings(Crop);

	// Filmback = viewport aspect with a fixed height (R-06): V = 2 atan(h / 2f) in the MaintainYFOV projection.
	FCameraFilmbackSettings Filmback = Camera.Filmback;
	Filmback.SensorHeight = static_cast<float>(RbCameraModel::SensorHeightMm);
	Filmback.SensorWidth = static_cast<float>(RbCameraModel::SensorHeightMm * Aspect);
	Filmback.SensorHorizontalOffset = 0.0f;
	Filmback.SensorVerticalOffset = 0.0f;
	Camera.SetFilmback(Filmback);
	Camera.bConstrainAspectRatio = false;
	// The vertical FOV is authored: the camera carries MaintainYFOV itself (FMinimalViewInfo::AspectRatioAxisConstraint overrides the
	// local player's config value), so no engine / user setting (MaintainXFOV, MajorAxisFOV) can turn 21:9 into a vertical crop.
	Camera.bOverrideAspectRatioAxisConstraint = true;
	Camera.SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV);
	Camera.Overscan = 0.0f; // the Headcam distortion pass (and its overscan) is post-M1

	// Lens from the vertical FOV and the pupil: f = h / (2 tan(V/2)) (= w / (2 tan(H/2))), N = f / A.
	double FocalMm = 0.0;
	double FStop = 0.0;
	RbCameraMath::PupilToCineLens(RbCameraModel::SensorHeightMm * Aspect, RbCameraMath::HorizontalFromVerticalFovDeg(Params.VerticalFovDeg, Aspect),
		Params.ApertureDiameterMm, FocalMm, FStop);
	Camera.SetCurrentFocalLength(static_cast<float>(FocalMm));
	Camera.SetCurrentAperture(static_cast<float>(FStop));
	Camera.SetCustomNearClippingPlane(static_cast<float>(Params.NearClipCm));
	Camera.bOverride_CustomNearClippingPlane = true;

	FCameraFocusSettings Focus = Camera.FocusSettings;
	if (Focus.FocusMethod == ECameraFocusMethod::DoNotOverride)
	{
		Focus.FocusMethod = ECameraFocusMethod::Manual;
	}
	Focus.bSmoothFocusChanges = false; // the rig eases the accommodation itself
	Camera.SetFocusSettings(Focus);
	Camera.PostProcessBlendWeight = 1.0f;

	FPostProcessSettings& PP = Camera.PostProcessSettings;
	// Exposure (plan 4.4): histogram auto exposure in EV100 (ExtendDefaultLuminanceRange), centre-weighted metering, the preset's
	// adaptation speeds; physical camera exposure off (the aperture is the DoF pupil, not an exposure control).
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	PP.bOverride_AutoExposureMinBrightness = true;
	PP.AutoExposureMinBrightness = static_cast<float>(Params.MinEv100);
	PP.bOverride_AutoExposureMaxBrightness = true;
	PP.AutoExposureMaxBrightness = static_cast<float>(Params.MaxEv100);
	PP.bOverride_AutoExposureSpeedUp = true;
	PP.AutoExposureSpeedUp = static_cast<float>(Params.AdaptSpeedUp);
	PP.bOverride_AutoExposureSpeedDown = true;
	PP.AutoExposureSpeedDown = static_cast<float>(Params.AdaptSpeedDown);
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = static_cast<float>(Params.ExposureCompensation);
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = 0;
	PP.bOverride_AutoExposureLowPercent = true;
	PP.AutoExposureLowPercent = static_cast<float>(Params.HistogramLowPercent);
	PP.bOverride_AutoExposureHighPercent = true;
	PP.AutoExposureHighPercent = static_cast<float>(Params.HistogramHighPercent);
	if (UTexture2D* Mask = GetMeteringMask(Params.MeteringSigma))
	{
		PP.bOverride_AutoExposureMeterMask = true;
		PP.AutoExposureMeterMask = Mask;
	}
	PP.bOverride_LocalExposureHighlightContrastScale = true;
	PP.LocalExposureHighlightContrastScale = static_cast<float>(Params.LocalExposureHighlightContrast);
	PP.bOverride_LocalExposureShadowContrastScale = true;
	PP.LocalExposureShadowContrastScale = static_cast<float>(Params.LocalExposureShadowContrast);

	// Shutter (plan 4.6): amount = shutter angle / 360 of the real frame time, max 5 % of the screen width.
	PP.bOverride_MotionBlurAmount = true;
	PP.MotionBlurAmount = static_cast<float>(Params.ShutterAngleDeg / 360.0);
	PP.bOverride_MotionBlurMax = true;
	PP.MotionBlurMax = static_cast<float>(Params.MotionBlurMaxPercent);
	PP.bOverride_MotionBlurTargetFPS = true;
	PP.MotionBlurTargetFPS = 0;

	// Sensor (plan 4.7): grain (the rig couples it to the current exposure every frame; g0 = the grain at EV_ref and above) with
	// more grain in the shadows, vignette, lateral CA (Headcam), bloom.
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = static_cast<float>(Params.GrainG0);
	PP.bOverride_FilmGrainIntensityShadows = true;
	PP.FilmGrainIntensityShadows = 1.0f;
	PP.bOverride_FilmGrainIntensityMidtones = true;
	PP.FilmGrainIntensityMidtones = 0.6f;
	PP.bOverride_FilmGrainIntensityHighlights = true;
	PP.FilmGrainIntensityHighlights = 0.3f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = static_cast<float>(Params.Vignette);
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = static_cast<float>(Params.ChromaticAberration);
	PP.bOverride_BloomMethod = true;
	PP.BloomMethod = Params.bConvolutionBloom ? EBloomMethod::BM_FFT : EBloomMethod::BM_SOG; // plan 4.4: Eyes convolution, cameras standard
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = static_cast<float>(Params.BloomIntensity);
}
