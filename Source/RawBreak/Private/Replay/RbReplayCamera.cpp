#include "Replay/RbReplayCamera.h"

#include "Balls/RbBallSet.h"
#include "Camera/RbCameraModel.h"
#include "Camera/RbCameraRigComponent.h"
#include "Core/RbCoords.h"
#include "Game/RbTableSubsystem.h"
#include "Table/RbTable.h"

#include "CineCameraComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// Owner: UE-7. View placements (header), lens per view, smooth follow.

const FName ARbReplayCamera::CameraTag(TEXT("RbReplayCamera"));

namespace RbReplayCameraView
{
	constexpr double OverheadMaxHeightM = 2.2;     // above the cloth, below any sane ceiling
	constexpr double OverheadLampClearanceM = 0.08; // below the lamp's underside
	constexpr double OverheadMarginScale = 1.08;
	constexpr double RailBackM = 1.8;               // behind the head rail's outer edge
	constexpr double RailHeightM = 0.55;
	constexpr double RailFovDeg = 30.0;
	constexpr double FollowSideM = 1.0;             // beside the right rail's outer edge
	constexpr double FollowHeightM = 0.85;
	constexpr double FollowFovDeg = 34.0;
	constexpr double FollowDollyShare = 0.5;        // the dolly follows half of the ball's travel along the table
	constexpr double FollowLookTau = 0.12;          // [s]
	constexpr double FollowDollyTau = 0.35;         // [s]
	constexpr double BroadcastAperture = 8.0;       // f-number of the broadcast views (deep focus)
	constexpr double DefaultSensorHeightMm = 13.365; // the cine camera's default 16:9 digital film height

	FTransform LookAtTransform(const FVector& Eye, const FVector& Target)
	{
		const FVector Forward = (Target - Eye).GetSafeNormal();
		return FTransform(FRotationMatrix::MakeFromXZ(Forward.IsNearlyZero() ? FVector::ForwardVector : Forward, FVector::UpVector).ToQuat(), Eye);
	}

	double Smoothing(double DeltaSeconds, double Tau)
	{
		return Tau > 0.0 ? 1.0 - FMath::Exp(-FMath::Max(0.0, DeltaSeconds) / Tau) : 1.0;
	}
}

ARbReplayCamera::ARbReplayCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	// After the playback (URbShotPlaybackComponent, TG_PrePhysics) has placed this frame's balls and before the player camera
	// manager reads the view (after the tick groups up to PostPhysics): Follow aims at the ball of THIS frame, not the last one.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Tags.AddUnique(CameraTag);
	SetActorEnableCollision(false);
}

FRbReplayViewPose ARbReplayCamera::ComputeViewPose(ERbReplayView InView, const ARbTable& InTable, const FTransform& ShooterView, const FVector& FollowPoint,
	double InShooterFovDeg, float Aspect, double OverheadHeightM)
{
	using namespace RbReplayCameraView;
	FRbReplayViewPose Pose;
	const double SafeAspect = Aspect > 0.1f ? static_cast<double>(Aspect) : 16.0 / 9.0;
	const rb::Aabb2 Outer = InTable.HasContext() ? InTable.GetContext().Geometry.OuterBoundary : rb::Aabb2{rb::Vec2(-1.42, -0.785), rb::Vec2(1.42, 0.785)};
	const auto At = [&InTable](double X, double Y, double Z) { return InTable.CoreToWorld(rb::Vec3(X, Y, Z)); };

	switch (InView)
	{
	case ERbReplayView::Shooter:
	{
		Pose.Transform = ShooterView;
		Pose.VerticalFovDeg = InShooterFovDeg;
		Pose.LookAt = ShooterView.GetLocation() + ShooterView.GetRotation().GetForwardVector() * 100.0;
		break;
	}
	case ERbReplayView::Overhead:
	{
		const double HalfX = FMath::Max(FMath::Abs(Outer.Lo.x), FMath::Abs(Outer.Hi.x)) * OverheadMarginScale;
		const double HalfY = FMath::Max(FMath::Abs(Outer.Lo.y), FMath::Abs(Outer.Hi.y)) * OverheadMarginScale;
		double Height = OverheadMaxHeightM;
		if (InTable.LampUndersideHeight > 0.0)
		{
			Height = FMath::Min(Height, InTable.LampUndersideHeight - OverheadLampClearanceM);
		}
		if (OverheadHeightM > 0.0)
		{
			Height = FMath::Min(Height, OverheadHeightM);
		}
		Height = FMath::Max(Height, 0.6);
		const double TanHalf = FMath::Max(HalfY, HalfX / SafeAspect) / Height;
		Pose.VerticalFovDeg = FMath::Clamp(FMath::RadiansToDegrees(2.0 * FMath::Atan(TanHalf)), 20.0, 110.0);
		const FVector Eye = At(0.0, 0.0, Height);
		// Straight down; screen right = core +x (the foot), so screen up = core +y (the head end is on the left).
		const FVector Down = -InTable.CoreDirectionToWorld(rb::Vec3(0.0, 0.0, 1.0));
		const FVector Right = InTable.CoreDirectionToWorld(rb::Vec3(1.0, 0.0, 0.0));
		Pose.Transform = FTransform(FRotationMatrix::MakeFromXY(Down, Right).ToQuat(), Eye);
		Pose.LookAt = At(0.0, 0.0, 0.0);
		break;
	}
	case ERbReplayView::Rail:
	{
		const FVector Eye = At(Outer.Lo.x - RailBackM, 0.0, RailHeightM);
		Pose.LookAt = At(0.15 * Outer.Hi.x, 0.0, 0.0);
		Pose.Transform = LookAtTransform(Eye, Pose.LookAt);
		Pose.VerticalFovDeg = RailFovDeg;
		break;
	}
	case ERbReplayView::Follow:
	{
		const rb::Vec3 Ball = InTable.WorldToCore(FollowPoint);
		const double DollyX = FMath::Clamp(FollowDollyShare * Ball.x, Outer.Lo.x, Outer.Hi.x);
		const FVector Eye = At(DollyX, Outer.Lo.y - FollowSideM, FollowHeightM);
		Pose.LookAt = FollowPoint;
		Pose.Transform = LookAtTransform(Eye, Pose.LookAt);
		Pose.VerticalFovDeg = FollowFovDeg;
		break;
	}
	}
	return Pose;
}

void ARbReplayCamera::SetView(ERbReplayView InView, const ARbTable* InTable, const FTransform& ShooterView)
{
	View = InView;
	Table = InTable;
	if (!InTable)
	{
		if (InView == ERbReplayView::Shooter)
		{
			SetActorTransform(ShooterView);
			ApplyLens(ShooterFovDeg, 100.0, false);
		}
		return;
	}
	if (InView == ERbReplayView::Follow)
	{
		PlaceFollow(0.0f, true);
		return;
	}
	FRbReplayViewPose Pose = ComputeViewPose(InView, *InTable, ShooterView, FVector::ZeroVector, ShooterFovDeg, ViewportAspect());
	FVector Eye = Pose.Transform.GetLocation();
	if (InView == ERbReplayView::Rail)
	{
		Eye = ClampToRoom(Pose.LookAt, Eye);
	}
	else if (InView == ERbReplayView::Overhead)
	{
		// A ceiling (or lamp geometry) lower than the lamp rule: stay under it and widen the lens to still fit the table.
		const FVector Clamped = ClampToRoom(Pose.LookAt, Eye);
		if (!Clamped.Equals(Eye, 0.01))
		{
			const double HeightM = InTable->WorldToCore(Clamped).z;
			Pose = ComputeViewPose(InView, *InTable, ShooterView, FVector::ZeroVector, ShooterFovDeg, ViewportAspect(), HeightM);
			Eye = Pose.Transform.GetLocation();
		}
	}
	SetActorLocationAndRotation(Eye, Pose.Transform.GetRotation());
	// Every replay view focuses deep (a replay shows the whole shot, not the eye's accommodation): the Shooter view on the
	// struck ball's aim line 1 m ahead of the ball, the others on their aim point.
	double FocusCm = FVector::Dist(Eye, Pose.LookAt);
	if (InView == ERbReplayView::Shooter && !ShooterFocusPoint.IsZero())
	{
		FocusCm = FVector::Dist(Eye, ShooterFocusPoint) + 100.0;
	}
	ApplyLens(Pose.VerticalFovDeg, FocusCm, InView != ERbReplayView::Shooter);
}

void ARbReplayCamera::SetFollowTarget(USceneComponent* Target)
{
	FollowTarget = Target;
	if (Target)
	{
		FollowLookAt = Target->GetComponentLocation();
		FollowBall = FollowLookAt;
	}
}

void ARbReplayCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (View == ERbReplayView::Follow)
	{
		PlaceFollow(DeltaSeconds, false);
	}
}

void ARbReplayCamera::PlaceFollow(float DeltaSeconds, bool bSnap)
{
	using namespace RbReplayCameraView;
	const ARbTable* TableActor = Table.Get();
	if (!TableActor)
	{
		return;
	}
	const USceneComponent* Target = FollowTarget.Get();
	// A captured (hidden) ball keeps the last point: the camera settles where the ball went down.
	const bool bTargetVisible = Target && Target->IsVisible();
	const FVector Ball = bTargetVisible ? Target->GetComponentLocation() : FollowBall;
	if (bSnap)
	{
		FollowLookAt = Ball;
		FollowBall = Ball;
	}
	else
	{
		FollowLookAt = FMath::Lerp(FollowLookAt, Ball, Smoothing(DeltaSeconds, FollowLookTau));
		FollowBall = FMath::Lerp(FollowBall, Ball, Smoothing(DeltaSeconds, FollowDollyTau));
	}
	// Dolly from the slowly smoothed ball, aim at the quickly smoothed one (tripod-smooth pan).
	const FRbReplayViewPose Dolly = ComputeViewPose(ERbReplayView::Follow, *TableActor, FTransform::Identity, FollowBall, ShooterFovDeg, ViewportAspect());
	const FVector Eye = ClampToRoom(FollowLookAt, Dolly.Transform.GetLocation());
	const FVector Forward = (FollowLookAt - Eye).GetSafeNormal();
	SetActorLocationAndRotation(Eye, FRotationMatrix::MakeFromXZ(Forward.IsNearlyZero() ? FVector::ForwardVector : Forward, FVector::UpVector).ToQuat());
	if (bSnap)
	{
		ApplyLens(FollowFovDeg, FVector::Dist(Eye, FollowLookAt), true);
	}
	else if (UCineCameraComponent* Cine = GetCineCameraComponent())
	{
		FCameraFocusSettings Focus = Cine->FocusSettings;
		Focus.ManualFocusDistance = static_cast<float>(FMath::Max(10.0, FVector::Dist(Eye, FollowLookAt)));
		Cine->SetFocusSettings(Focus);
	}
}

FVector ARbReplayCamera::ClampToRoom(const FVector& LookAt, const FVector& Eye) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Eye;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RbReplayCameraClamp), false, this);
	// Balls and the table are what the camera looks at, never what it hides behind.
	if (const ARbTable* TableActor = Table.Get())
	{
		Params.AddIgnoredActor(TableActor);
	}
	// Every table's balls (M2-E: through the table registry, never by iterating the world's ball sets). Called every tick of the
	// Follow view: the registered sessions (no allocation), plus the viewed table's ball set on a dev map without a session.
	if (const URbTableSubsystem* Tables = URbTableSubsystem::Get(World))
	{
		bool bViewedTableHasSession = false;
		for (const FRbTableSession& Session : Tables->GetSessions())
		{
			Params.AddIgnoredActor(Session.BallSet.Get());
			bViewedTableHasSession |= Session.Table.Get() == Table.Get();
		}
		if (!bViewedTableHasSession && Table.IsValid())
		{
			Params.AddIgnoredActor(Tables->FindBallSet(Table.Get()));
		}
	}
	const FVector Start = LookAt + FVector(0.0, 0.0, 10.0);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, Eye, ECC_Camera, Params) && Hit.bBlockingHit)
	{
		const FVector Back = (Start - Eye).GetSafeNormal();
		return Hit.Location + Back * 15.0;
	}
	return Eye;
}

float ARbReplayCamera::ViewportAspect() const
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

void ARbReplayCamera::ApplyLens(double VerticalFovDeg, double FocusDistanceCm, bool bBroadcast)
{
	using namespace RbReplayCameraView;
	UCineCameraComponent* Cine = GetCineCameraComponent();
	if (!Cine)
	{
		return;
	}
	const float Aspect = ViewportAspect();
	FRbCameraPresetParams Params = RbCameraModel::Defaults(bBroadcast ? ERbCameraPreset::Broadcast : ERbCameraPreset::Eyes);
	Params.VerticalFovDeg = VerticalFovDeg;
	// The game's camera model (UE-5b): filmback = viewport aspect, focal length from the vertical FOV, pupil, exposure, grain.
	URbCameraRigComponent::ApplyPresetToCamera(*Cine, Params, Aspect);
	Cine->bConstrainAspectRatio = false;
	// The rig's lens rule (R-06) directly, in case the preset left the lens elsewhere: V = 2 atan(h / 2f).
	if (!FMath::IsNearlyEqual(static_cast<double>(Cine->GetVerticalFieldOfView()), VerticalFovDeg, 0.05))
	{
		FCameraFilmbackSettings Filmback = Cine->Filmback;
		if (Filmback.SensorHeight <= 0.0f)
		{
			Filmback.SensorHeight = static_cast<float>(DefaultSensorHeightMm);
		}
		Filmback.SensorWidth = Filmback.SensorHeight * Aspect;
		Cine->SetFilmback(Filmback);
		const double Focal = Filmback.SensorHeight / (2.0 * FMath::Tan(FMath::DegreesToRadians(0.5 * VerticalFovDeg)));
		FCameraLensSettings Lens = Cine->LensSettings;
		Lens.MinFocalLength = FMath::Min(Lens.MinFocalLength, static_cast<float>(Focal));
		Lens.MaxFocalLength = FMath::Max(Lens.MaxFocalLength, static_cast<float>(Focal));
		Cine->SetLensSettings(Lens);
		Cine->SetCurrentFocalLength(static_cast<float>(Focal));
	}
	FCameraFocusSettings Focus = Cine->FocusSettings;
	Focus.FocusMethod = ECameraFocusMethod::Manual;
	Focus.ManualFocusDistance = static_cast<float>(FMath::Max(10.0, FocusDistanceCm));
	Cine->SetFocusSettings(Focus);
	// Deep focus for every replay view (a TV camera stopped down in a bright hall).
	FCameraLensSettings Lens = Cine->LensSettings;
	Lens.MaxFStop = FMath::Max(Lens.MaxFStop, static_cast<float>(BroadcastAperture));
	Cine->SetLensSettings(Lens);
	Cine->SetCurrentAperture(static_cast<float>(BroadcastAperture));
}
