#include "Dev/RbLookDevCamera.h"

#include "RawBreak.h"
#include "Camera/RbCameraRigComponent.h"
#include "Cue/RbCue.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTestRoom.h"
#include "Table/RbTable.h"

#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"

// Owner: UE-8.

namespace RbLookDevCameraPrivate
{
	// Filmback height of the R-06 rule (h fixed, w = h * aspect): the 16:9 crop of a 36 mm full-frame sensor (plan 4.5 example).
	constexpr float SensorHeightMm = 20.25f;
	constexpr double FocusTraceCm = 5000.0;
	constexpr double FocusFallbackCm = 1000.0;

	ARbTable* FindTable(const UWorld* World)
	{
		if (World)
		{
			for (TActorIterator<ARbTable> It(World); It; ++It)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

ARbLookDevCamera::ARbLookDevCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after the director / playback moved the balls this frame
	if (UCineCameraComponent* Cine = GetCineCameraComponent())
	{
		Cine->bConstrainAspectRatio = false; // MaintainYFOV: any viewport aspect, vertical FOV authored (plan 4.3)
	}
}

float ARbLookDevCamera::GetViewportAspect()
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0 && Size.Y > 0)
		{
			return static_cast<float>(Size.X) / static_cast<float>(Size.Y);
		}
	}
	return 16.0f / 9.0f;
}

bool ARbLookDevCamera::IsActiveViewTarget() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return false;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	return PC && PC->GetViewTarget() == this;
}

void ARbLookDevCamera::ApplyOptics(float Aspect)
{
	UCineCameraComponent* Cine = GetCineCameraComponent();
	if (!Cine || Aspect <= 0.0f)
	{
		return;
	}
	const FRbCameraPresetParams Params = RbCameraModel::Defaults(Preset);
	Cine->bConstrainAspectRatio = false;
	URbCameraRigComponent::ApplyPresetToCamera(*Cine, Params, Aspect);

	// Integration guard: the look-dev screenshots must show the preset's optics. If the rig's ApplyPresetToCamera left the lens
	// untouched (UE-5b not merged yet), apply the R-06 rule here: filmback aspect = viewport aspect with a fixed height,
	// f = h / (2 tan(V/2)), N = f / A (pupil). With the rig implemented the check passes and nothing below runs.
	const bool bFov = FMath::IsNearlyEqual(static_cast<double>(Cine->GetVerticalFieldOfView()), Params.VerticalFovDeg, 0.01);
	const bool bAspect = FMath::IsNearlyEqual(Cine->Filmback.SensorWidth / FMath::Max(Cine->Filmback.SensorHeight, 0.001f), Aspect, 1e-3f);
	if (bFov && bAspect)
	{
		AppliedAspect = Aspect;
		return;
	}
	using namespace RbLookDevCameraPrivate;
	FCameraLensSettings Lens = Cine->LensSettings;
	Lens.MinFocalLength = 1.0f;
	Lens.MaxFocalLength = 1000.0f;
	Lens.MinFStop = 0.5f;
	Lens.MaxFStop = 64.0f;
	Cine->SetLensSettings(Lens);
	FCameraFilmbackSettings Filmback = Cine->Filmback;
	Filmback.SensorHeight = SensorHeightMm;
	Filmback.SensorWidth = SensorHeightMm * Aspect;
	Filmback.SensorHorizontalOffset = 0.0f;
	Filmback.SensorVerticalOffset = 0.0f;
	Filmback.RecalcSensorAspectRatio();
	Cine->SetFilmback(Filmback);
	const double FocalMm = SensorHeightMm / (2.0 * FMath::Tan(FMath::DegreesToRadians(0.5 * Params.VerticalFovDeg)));
	Cine->SetCurrentFocalLength(static_cast<float>(FocalMm));
	Cine->SetCurrentAperture(static_cast<float>(FocalMm / FMath::Max(Params.ApertureDiameterMm, 0.1)));
	if (!bLoggedFallback)
	{
		UE_LOG(LogRawBreak, Display, TEXT("ARbLookDevCamera %s: rig optics not applied - look-dev lens: V %.2f deg at %.3f, f %.2f mm, N %.2f"), *GetName(),
			Params.VerticalFovDeg, Aspect, FocalMm, FocalMm / Params.ApertureDiameterMm);
		bLoggedFallback = true;
	}
	AppliedAspect = Aspect;
}

FRbChinOnCuePose ARbLookDevCamera::ComputeChinOnCuePose(const ARbTable& Table, const FRbCameraPresetParams& Params, const FVector2D& CueBall,
	const FVector2D& AimPoint, double ElevationDeg, double TipGapM, double BallRadiusM, double TipDomeRadiusM)
{
	// Core frame [m]: cue axis d = (cos th cos ph, cos th sin ph, -sin th) through the cue-ball centre (centre hit), tip dome
	// centre R + r_tip + gap behind the centre; eye e = P_axis(s_e) + h_c n_up + y_vc n_side (plan 4.2).
	const double Phi = FMath::Atan2(AimPoint.Y - CueBall.Y, AimPoint.X - CueBall.X);
	const double Theta = FMath::DegreesToRadians(ElevationDeg);
	const FVector3d D(FMath::Cos(Theta) * FMath::Cos(Phi), FMath::Cos(Theta) * FMath::Sin(Phi), -FMath::Sin(Theta));
	const FVector3d Up(FMath::Sin(Theta) * FMath::Cos(Phi), FMath::Sin(Theta) * FMath::Sin(Phi), FMath::Cos(Theta));
	const FVector3d Side(-FMath::Sin(Phi), FMath::Cos(Phi), 0.0);
	const FVector3d Center(CueBall.X, CueBall.Y, BallRadiusM);
	const FVector3d Tip = Center - D * (BallRadiusM + TipDomeRadiusM + TipGapM);
	const FVector3d Eye = Tip - D * Params.EyeBehindTipM + Up * Params.EyeAboveCueM + Side * Params.VisionCenterM;

	FRbChinOnCuePose Pose;
	Pose.TipDomeCore = Tip;
	Pose.DirectionCore = D;
	Pose.Eye = Table.CoreToWorld(rb::Vec3(Eye.X, Eye.Y, Eye.Z));
	Pose.AimPoint = Table.CoreToWorld(rb::Vec3(AimPoint.X, AimPoint.Y, BallRadiusM));
	Pose.Rotation = FRotationMatrix::MakeFromX(Pose.AimPoint - Pose.Eye).Rotator();
	Pose.Rotation.Roll = 0.0;
	return Pose;
}

void ARbLookDevCamera::UpdatePlacement(bool bActive)
{
	bHasFocusPoint = false;
	if (Placement != ERbLookDevPlacement::ChinOnCue)
	{
		return;
	}
	ARbTable* Table = RbLookDevCameraPrivate::FindTable(GetWorld());
	if (!Table || !Table->HasContext())
	{
		return;
	}
	const TSharedPtr<const FRbTableContext> Context = Table->GetContextPtr();
	FVector2D CueBall = CueBallCore;
	if (const ARbGameMode* Mode = ARbGameMode::Get(this))
	{
		if (const URbMatchDirector* Director = Mode->GetDirector())
		{
			const rb::SimBall& Ball = Director->GetTableState().Balls[0];
			if (Ball.InPlay)
			{
				CueBall = FVector2D(Ball.State.Position.x, Ball.State.Position.y);
			}
		}
	}
	ARbCue* Cue = nullptr;
	for (TActorIterator<ARbCue> It(GetWorld()); It; ++It)
	{
		Cue = *It;
		break;
	}
	const double TipDome = Cue ? Cue->GetCueSpec().TipDomeRadius : rb::kCuePlaying19oz.TipDomeRadius;
	const FRbChinOnCuePose Pose = ComputeChinOnCuePose(*Table, RbCameraModel::Defaults(Preset), CueBall, AimPointCore, CueElevationDeg, 0.01 * TipGapCm,
		Context->BallRadius(0), TipDome);
	SetActorLocationAndRotation(Pose.Eye, Pose.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	FocusPointWorld = Pose.AimPoint;
	bHasFocusPoint = true;
	if (bActive && bPoseCue && Cue)
	{
		Cue->SetPoseCore(rb::Vec3(Pose.TipDomeCore.X, Pose.TipDomeCore.Y, Pose.TipDomeCore.Z),
			rb::Vec3(Pose.DirectionCore.X, Pose.DirectionCore.Y, Pose.DirectionCore.Z));
		if (Cue->GetDrive() != ERbCueDrive::Input)
		{
			Cue->SetDrive(ERbCueDrive::Input);
		}
	}
}

void ARbLookDevCamera::UpdateFocus()
{
	UCineCameraComponent* Cine = GetCineCameraComponent();
	if (!Cine)
	{
		return;
	}
	double Distance = FocusDistanceCm;
	const FVector From = Cine->GetComponentLocation();
	if (Distance <= 0.0 && bHasFocusPoint)
	{
		Distance = FVector::Distance(From, FocusPointWorld);
	}
	else if (Distance <= 0.0)
	{
		Distance = RbLookDevCameraPrivate::FocusFallbackCm;
		if (UWorld* World = GetWorld())
		{
			FCollisionQueryParams Query(SCENE_QUERY_STAT(RbLookDevFocus), true, this);
			FHitResult Hit;
			const FVector To = From + Cine->GetForwardVector() * RbLookDevCameraPrivate::FocusTraceCm;
			if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Query))
			{
				Distance = FMath::Max(Hit.Distance, 1.0);
			}
		}
	}
	FCameraFocusSettings Focus = Cine->FocusSettings;
	Focus.FocusMethod = ECameraFocusMethod::Manual;
	Focus.ManualFocusDistance = static_cast<float>(Distance);
	Focus.bSmoothFocusChanges = false;
	Cine->SetFocusSettings(Focus);
}

void ARbLookDevCamera::SetLampFixtureHidden(bool bHide)
{
	for (TActorIterator<ARbTestRoom> It(GetWorld()); It; ++It)
	{
		It->SetLampFixtureHiddenInGame(bHide);
	}
	bFixtureHiddenByMe = bHide;
}

void ARbLookDevCamera::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Editor preview: the preset's lens at 16:9 and the chin-on-cue placement from the default cue ball.
	ApplyOptics(16.0f / 9.0f);
	UpdatePlacement(false);
	UpdateFocus();
}

void ARbLookDevCamera::BeginPlay()
{
	Super::BeginPlay();
	ApplyOptics(GetViewportAspect());
	UpdatePlacement(IsActiveViewTarget());
	UpdateFocus();
}

void ARbLookDevCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bFixtureHiddenByMe)
	{
		SetLampFixtureHidden(false);
	}
	Super::EndPlay(EndPlayReason);
}

void ARbLookDevCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return; // editor viewports tick cine cameras too: nothing to do there
	}
	const bool bActive = IsActiveViewTarget();
	if (bHideLampFixture && bActive != bFixtureHiddenByMe)
	{
		SetLampFixtureHidden(bActive);
	}
	if (!bActive)
	{
		return; // gameplay: the look-dev cameras in the M1 map stay passive
	}
	const float Aspect = GetViewportAspect();
	if (!FMath::IsNearlyEqual(Aspect, AppliedAspect, 1e-4f))
	{
		ApplyOptics(Aspect); // viewport resized: keep the filmback aspect = viewport aspect (R-06)
	}
	UpdatePlacement(true);
	UpdateFocus();
}
