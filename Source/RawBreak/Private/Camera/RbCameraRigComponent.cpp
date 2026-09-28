#include "Camera/RbCameraRigComponent.h"

#include "RbExposureProbe.h"
#include "Math/RbCameraMath.h"
#include "Settings/RbGameUserSettings.h"

#include "CineCameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "SceneViewExtension.h"
#include "UObject/Package.h"

// Owner: UE-5b. Eye placement + transition, vertical FOV through the filmback, pupil DoF with the accommodation ease, exposure /
// motion blur / grain / vignette / CA / bloom from the preset, head motion, comfort settings (header). Tests:
// RawBreak.Unit.Camera.* (Private/Tests/RbCameraRigTests.cpp).

namespace
{
	constexpr double kCmPerM = 100.0;
	constexpr double kMaxGazeYawDeg = 70.0;
	constexpr double kMaxGazePitchDeg = 35.0;
	constexpr double kCueBallFrameMarginDeg = 6.0;  // the cue ball stays this far inside the frame when looking down the line
	constexpr double kMinFocusCm = 5.0;             // lens minimum focus distance (50 mm)
	constexpr double kMaxFocusCm = 3000.0;          // "infinity" of the focus trace
	constexpr double kAxisSnapCm = 20.0;            // a new address far from the last one snaps the smoothed axis

	FVector HorizontalUnit(const FVector& V, const FVector& Fallback = FVector::ForwardVector)
	{
		const FVector H(V.X, V.Y, 0.0);
		return H.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? H.GetUnsafeNormal() : Fallback;
	}

	double SmoothStep(double A)
	{
		A = FMath::Clamp(A, 0.0, 1.0);
		return A * A * (3.0 - 2.0 * A);
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
		P.VerticalFovDeg = FMath::Clamp(VerticalFovOverride, 30.0, 90.0);
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
	SetComfort(bReduced ? 0.0 : Settings->HeadBobScale, bReduced ? 0.0 : Settings->MotionBlurScale, Settings->GrainScale, Settings->bDepthOfField);
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
		GazeYawDeg = 0.0;
		GazePitchDeg = 0.0;
		bSmoothedAxisValid = false;
	}
	if (InMode != ERbCameraRigMode::DownOnShot)
	{
		bSettleHeld = false;
	}
	TransitionSeconds = TransitionSecondsFor(FromMode, InMode);
	if (!bHasPose || TransitionSeconds <= 0.0 || InMode == ERbCameraRigMode::External)
	{
		TransitionAlpha = 1.0;
		return;
	}
	FromEye = BlendedEye;
	FromRot = BlendedRot;
	TransitionAlpha = 0.0;
}

double URbCameraRigComponent::TransitionSecondsFor(ERbCameraRigMode From, ERbCameraRigMode To) const
{
	if (From == ERbCameraRigMode::External || To == ERbCameraRigMode::External)
	{
		return 0.0; // camera cut
	}
	if (From == ERbCameraRigMode::DownOnShot || To == ERbCameraRigMode::DownOnShot)
	{
		return Params.GetDownSeconds;
	}
	return Params.BallInHandSeconds;
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

	// 1. Pose of the mode, blended from the pose at the last mode change.
	FVector Eye;
	FQuat Rot;
	ComputeModePose(Mode, Eye, Rot);
	if (TransitionAlpha < 1.0 && bHasPose)
	{
		TransitionAlpha = TransitionSeconds > 0.0 ? FMath::Min(1.0, TransitionAlpha + Dt / TransitionSeconds) : 1.0;
		const double A = SmoothStep(TransitionAlpha);
		Eye = FMath::Lerp(FromEye, Eye, A);
		Rot = FQuat::Slerp(FromRot, Rot, A).GetNormalized();
	}
	else
	{
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

	// 3. Head motion layer (plan 4.8): translation in the yaw-only frame; Eyes keep the fixation point (VOR), Headcam nods.
	const bool bDown = Mode == ERbCameraRigMode::DownOnShot;
	const double SettleRate = Params.HeadMotion.SettleSeconds > 0.0 ? Dt / Params.HeadMotion.SettleSeconds : 1.0;
	SettleAlpha = FMath::Clamp(SettleAlpha + (bSettleHeld && bDown ? SettleRate : -SettleRate), 0.0, 1.0);
	const AActor* Owner = GetOwner();
	const double Speed = (Owner && !bDown) ? Owner->GetVelocity().Size2D() / kCmPerM : 0.0;
	HeadSample = HeadMotion.Step(Dt, Speed, bDown, SettleAlpha, Params, MotionScale);
	const FQuat YawOnly = FRotator(0.0, Rot.Rotator().Yaw, 0.0).Quaternion();
	const FVector FinalEye = Eye + YawOnly.RotateVector(HeadSample.Offset);
	FQuat FinalRot = Rot;
	if (Params.bStabiliseGaze)
	{
		const FVector Fixation = Eye + Rot.GetForwardVector() * FMath::Clamp(FocusCm > 0.0 ? FocusCm : 200.0, 30.0, 2000.0);
		const FVector Dir = Fixation - FinalEye;
		if (!Dir.IsNearlyZero())
		{
			FRotator View = Dir.Rotation();
			View.Roll = Rot.Rotator().Roll;
			FinalRot = View.Quaternion();
		}
	}
	else
	{
		FinalRot = Rot * HeadSample.Rotation.Quaternion();
	}
	Cam->SetWorldLocationAndRotation(FinalEye, FinalRot);

	// 4. Exposure-coupled grain.
	UpdateGrain();
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
			View.Pitch = FMath::Clamp(View.Pitch + GazePitchDeg, -89.0, 89.0);
			View.Yaw += GazeYawDeg;
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

double URbCameraRigComponent::Ev100FromExposure(double Exposure, double Compensation)
{
	return Exposure > 0.0 ? Compensation - FMath::Log2(1.2 * Exposure) : -100.0;
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
	if (Mode == ERbCameraRigMode::DownOnShot && bSmoothedAxisValid)
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
	PP.BloomMethod = EBloomMethod::BM_SOG;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = static_cast<float>(Params.BloomIntensity);
}
