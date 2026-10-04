#include "Dev/RbLookDevCamera.h"

#include "RawBreak.h"
#include "Camera/RbCameraRigComponent.h"
#include "Cue/RbCue.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Game/RbTestRoom.h"
#include "Table/RbTable.h"

#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"

// Owner: M2-L (UE-8 in M1).

namespace RbLookDevCameraPrivate
{
	// Filmback height of the R-06 rule (h fixed, w = h * aspect): the 16:9 crop of a 36 mm full-frame sensor (plan 4.5 example).
	constexpr float SensorHeightMm = 20.25f;
	constexpr double FocusTraceCm = 5000.0;
	constexpr double FocusFallbackCm = 1000.0;
	// Standing eye height above the floor [m] (camera model: the Eyes preset's standing eye, plan 4.2).
	constexpr double StandingEyeM = 1.65;
	// Eye height of the foot-end view above the floor [m] (bent over the coin slide).
	constexpr double FootEndEyeM = 1.15;
}

ARbTable* ARbLookDevCamera::ResolveTable() const
{
	// Cached while the table lives in this camera's world and the index is unchanged (URbTableSubsystem's queries collect the level's
	// tables into an array: not once per frame).
	if (ARbTable* Cached = CachedTable.Get(); Cached && CachedTableIndex == TableIndex && Cached->GetWorld() == GetWorld() &&
		(TableIndex < 0 || Cached->TableIndex == TableIndex))
	{
		return Cached;
	}
	const URbTableSubsystem* Tables = URbTableSubsystem::Get(this);
	ARbTable* Table = Tables ? (TableIndex >= 0 ? Tables->FindTable(TableIndex) : Tables->GetPlayerTable()) : nullptr;
	CachedTable = Table;
	CachedTableIndex = TableIndex;
	return Table;
}

FRbTableViewPose ARbLookDevCamera::ComputeTableViewPose(const ARbTable& Table, ERbTableLookDevView View)
{
	// Core table frame [m]: +x foot, +y left seen from the head end, z up from the cloth. Every view is defined by an eye and a
	// look-at point in that frame, derived from the TableSpec (no hard-coded table numbers).
	const rb::TableSpec& Spec = Table.GetContext().Spec;
	const double Hl = 0.5 * Spec.Length;
	const double Hw = 0.5 * Spec.Width;
	const double Rw = Spec.RailWidthTotal;
	const double Zc = Spec.RailTopZ;
	FVector3d Eye = FVector3d::ZeroVector;
	FVector3d Target = FVector3d::UnitX();
	switch (View)
	{
	case ERbTableLookDevView::Overhead:
	{
		// Straight down; the outer width plus 10 cm fills the 50 deg vertical field (image up = core +y, right = foot).
		const double Height = (Hw + Rw + 0.10) / FMath::Tan(FMath::DegreesToRadians(25.0));
		FRbTableViewPose Pose;
		Pose.Eye = Table.CoreToWorld(rb::Vec3(0.0, 0.0, Height));
		Pose.FocusPoint = Table.CoreToWorld(rb::Vec3(0.0, 0.0, 0.0));
		Pose.Rotation = FRotator(-90.0, Table.GetActorRotation().Yaw - 90.0, 0.0);
		return Pose;
	}
	case ERbTableLookDevView::Standing:
		// A player walking up to the head end: 0.55 m behind the head rail's outer edge, a little left of the long string.
		Eye = FVector3d(-(Hl + Rw + 0.55), 0.18, RbLookDevCameraPrivate::StandingEyeM - Spec.BedHeight);
		Target = FVector3d(0.5 * Hl, 0.0, 0.0);
		break;
	case ERbTableLookDevView::PocketCloseUp:
	{
		// Foot-right corner pocket (P2) from the table side along its axis, leaning over it: 0.40 m in plan, 0.26 m above the
		// cloth, looking at the capture centre just below the cloth (jaws, facings, the drop and the pocket hardware).
		const rb::PocketGeometry& Pocket = Table.GetContext().Geometry.Pockets[static_cast<int32>(rb::PocketId::FootRight)];
		const FVector3d C(Pocket.CaptureCenter.x, Pocket.CaptureCenter.y, 0.0);
		const FVector3d Axis(Pocket.Axis.x, Pocket.Axis.y, 0.0); // into the pocket
		Eye = C - Axis.GetSafeNormal() * 0.40 + FVector3d(0.0, 0.0, 0.26);
		Target = C + FVector3d(0.0, 0.0, -0.01);
		break;
	}
	case ERbTableLookDevView::CushionGrazing:
		// 3 cm above the cloth, 9 cm off the right long cushion's nose, looking along it toward the foot: the nose line recedes, the
		// cloth shows its sheen at grazing angles.
		Eye = FVector3d(-0.62 * Hl, -(Hw - 0.09), 0.03);
		Target = FVector3d(0.35 * Hl, -(Hw - 0.01), 0.022);
		break;
	case ERbTableLookDevView::RailCloseUp:
		// From outside the right long rail, a little above it: the rounded cap edge, a sight, the cushion behind and the apron /
		// cabinet below.
		Eye = FVector3d(0.10 * Hl, -(Hw + Rw + 0.30), Zc + 0.16);
		Target = FVector3d(0.30 * Hl, -(Hw + 0.55 * Rw), Zc - 0.02);
		break;
	case ERbTableLookDevView::FootEnd:
		// A player bending down to the foot end (eye 1.15 m above the floor, 0.85 m in front of it, a little to the right): the foot
		// rail and, on the coin-op cabinet, the coin mechanism, the trap window, the ball tray and the cue-ball return.
		Eye = FVector3d(Hl + Rw + 0.85, -0.30, RbLookDevCameraPrivate::FootEndEyeM - Spec.BedHeight);
		Target = FVector3d(Hl + Rw, 0.05, 0.55 - Spec.BedHeight);
		break;
	}
	FRbTableViewPose Pose;
	Pose.Eye = Table.CoreToWorld(rb::Vec3(Eye.X, Eye.Y, Eye.Z));
	Pose.FocusPoint = Table.CoreToWorld(rb::Vec3(Target.X, Target.Y, Target.Z));
	Pose.Rotation = FRotationMatrix::MakeFromX(Pose.FocusPoint - Pose.Eye).Rotator();
	Pose.Rotation.Roll = 0.0;
	return Pose;
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
	// Look-dev exposure calibration on top of the preset's compensation (ApplyPresetToCamera resets it every call, so no accumulation).
	Cine->PostProcessSettings.AutoExposureBias = static_cast<float>(Params.ExposureCompensation) + ExposureBiasEv;

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
	if (Placement == ERbLookDevPlacement::Fixed)
	{
		return;
	}
	ARbTable* Table = ResolveTable();
	if (!Table || !Table->HasContext())
	{
		return;
	}
	if (Placement == ERbLookDevPlacement::TableView)
	{
		const FRbTableViewPose Pose = ComputeTableViewPose(*Table, TableView);
		SetActorLocationAndRotation(Pose.Eye, Pose.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		FocusPointWorld = Pose.FocusPoint;
		bHasFocusPoint = true;
		return;
	}
	const TSharedPtr<const FRbTableContext> Context = Table->GetContextPtr();
	// The director and cue of the table's session (multi-table rule, 18.6.2); without a registered session the game mode's
	// (player) director and the level's first cue (M1 levels, a table without a match).
	const URbMatchDirector* Director = nullptr;
	ARbCue* Cue = nullptr;
	if (const URbTableSubsystem* Tables = URbTableSubsystem::Get(this))
	{
		if (const FRbTableSession* Session = Tables->FindSessionForTable(Table))
		{
			Director = Session->Director.Get();
			Cue = Session->Cue.Get();
		}
	}
	if (!Director)
	{
		const ARbGameMode* Mode = ARbGameMode::Get(this);
		Director = Mode && Mode->GetTable() == Table ? Mode->GetDirector() : nullptr;
	}
	if (!Cue)
	{
		// The level's first cue, looked up once (an actor iterator collects the class's objects into an array: not every frame).
		if (!CachedCue.IsValid() || CachedCue->GetWorld() != GetWorld())
		{
			CachedCue = nullptr;
			for (TActorIterator<ARbCue> It(GetWorld()); It; ++It)
			{
				CachedCue = *It;
				break;
			}
		}
		Cue = CachedCue.Get();
	}
	FVector2D CueBall = CueBallCore;
	if (Director)
	{
		const rb::SimBall& Ball = Director->GetTableState().Balls[0];
		if (Ball.InPlay)
		{
			CueBall = FVector2D(Ball.State.Position.x, Ball.State.Position.y);
		}
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
