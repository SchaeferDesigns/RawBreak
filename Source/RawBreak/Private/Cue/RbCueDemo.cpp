#include "Cue/RbCueDemo.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Cue/RbCue.h"
#include "Player/RbStrokeComponent.h"
#include "Table/RbTable.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

#include "rb/Equipment/Cue.h"
#include "rb/Human/HumanModel.h"
#include "rb/Physics/CueStrike.h"

// Owner: UE-4 (dev tool).

// File-local helpers in a named namespace (unity builds share one translation unit between files).
namespace RbCueDemoPrivate
{
	const TCHAR* FloorName(rb::human::FloorSource Source)
	{
		switch (Source)
		{
		case rb::human::FloorSource::Rail: return TEXT("rail");
		case rb::human::FloorSource::Ball: return TEXT("ball");
		case rb::human::FloorSource::None: break;
		}
		return TEXT("none");
	}
}

ARbCueDemo::ARbCueDemo()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ARbCueDemo::BeginPlay()
{
	Super::BeginPlay();
	BuildScene();
}

void ARbCueDemo::BuildScene()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Table)
	{
		for (TActorIterator<ARbTable> It(World); It; ++It)
		{
			Table = *It;
			break;
		}
	}
	if (!Table)
	{
		UE_LOG(LogRawBreak, Error, TEXT("RbCueDemo: no ARbTable in the level"));
		return;
	}
	if (!Table->HasContext())
	{
		Table->RebuildTable();
	}
	if (!Table->HasContext())
	{
		UE_LOG(LogRawBreak, Error, TEXT("RbCueDemo: the table has no context"));
		return;
	}
	for (ARbCue* Cue : Cues)
	{
		if (Cue)
		{
			Cue->Destroy();
		}
	}
	Cues.Reset();
	if (!BallSet)
	{
		BallSet = World->SpawnActor<ARbBallSet>();
	}
	if (BallSet)
	{
		BallSet->BallMaterialOverride = BallMaterialOverride;
		BallSet->InitForTable(Table);
		for (int32 Id = 0; Id < BallSet->GetBallCount(); ++Id)
		{
			BallSet->SetBallVisible(Id, false);
		}
	}
	if (Scene == ERbCueDemoScene::Lineup)
	{
		BuildLineup();
	}
	else
	{
		BuildAddress();
	}
}

void ARbCueDemo::BuildAddress()
{
	const FRbTableContext& Context = Table->GetContext();
	const double R = Context.BallRadius(0);
	const rb::Vec3 Center(CueBall.X, CueBall.Y, R);
	rb::Vec3 Positions[rb::kMaxBalls];
	bool InPlay[rb::kMaxBalls] = {};
	Positions[0] = Center;
	InPlay[0] = true;
	if (BallSet)
	{
		BallSet->SetBallCore(0, Center, rb::Quat::Identity());
		BallSet->SetBallVisible(0, true);
	}
	for (int32 Index = 0; Index < ObjectBalls.Num() && Index + 1 < rb::kMaxBalls; ++Index)
	{
		const int32 Id = Index + 1;
		Positions[Id] = rb::Vec3(ObjectBalls[Index].X, ObjectBalls[Index].Y, Context.BallRadius(Id));
		InPlay[Id] = true;
		if (BallSet && Id < BallSet->GetBallCount())
		{
			BallSet->SetBallCore(Id, Positions[Id], rb::Quat::Identity());
			BallSet->SetBallVisible(Id, true);
		}
	}

	rb::CueSpec Spec = rb::GetCueSpec(RbTypes::ToCore(CuePreset));
	if (CueLengthOverrideCm > 0.0f)
	{
		Spec.Length = 0.01 * CueLengthOverrideCm;
	}
	const rb::human::CueBodyState Body;
	const rb::human::TipState Tip;
	const double Azimuth = FMath::DegreesToRadians(static_cast<double>(AzimuthDeg));
	const double Requested = FMath::DegreesToRadians(static_cast<double>(ElevationDeg));

	// The clearance input exactly as the stroke component builds it (contact offsets from the axis offsets, clamped).
	rb::Vec2 Offset = rb::AimToContactOffset(rb::Vec2(TipOffset.X, TipOffset.Y), R, Tip.DomeRadius);
	const rb::human::HumanParams Params;
	const double Rho = FMath::Sqrt(Offset.x * Offset.x + Offset.y * Offset.y);
	if (Rho > Params.OffsetClamp && Rho > 0.0)
	{
		Offset = rb::Vec2(Offset.x * Params.OffsetClamp / Rho, Offset.y * Params.OffsetClamp / Rho);
	}
	FRbCueClearanceInput Input;
	Input.ContactPoint = Center + rb::CueContactPoint(rb::MakeCueFrame(Requested, Azimuth), Offset.x, Offset.y, R);
	Input.ContactElevation = Requested;
	Input.Azimuth = Azimuth;
	Input.Elevation = Requested;
	Input.CueLength = Spec.Length;
	Input.Backswing = 0.01 * BackswingCm;
	Input.TipDomeRadius = Tip.DomeRadius;
	Input.TipWidth = Tip.Width;
	Input.CueBall = 0;
	Input.BallPositions = Positions;
	Input.InPlay = InPlay;
	Input.BallCount = rb::kMaxBalls;
	Floor = RbCueClearance::ComputeMinElevationWithEnvironment(GetWorld(), *Table, Body, Input);
	ShownElevation = bClearanceFloor ? FMath::Max(Requested, Floor.MinElevation) : Requested;

	ARbCue* Cue = GetWorld()->SpawnActor<ARbCue>();
	if (!Cue)
	{
		return;
	}
	Cue->MaterialOverride = CueMaterialOverride;
	Cue->InitForTable(Table, Spec, Body);
	rb::human::HandPose Pose;
	Pose.Azimuth = Azimuth;
	Pose.Elevation = ShownElevation;
	Pose.AxisOffsetA = TipOffset.X;
	Pose.AxisOffsetB = TipOffset.Y;
	rb::Vec3 DomeCenter;
	rb::Vec3 Direction;
	URbStrokeComponent::ComputeCuePoseCore(Pose, Center, R, Tip.DomeRadius, Params.OffsetClamp, -0.01 * TipBackCm, DomeCenter, Direction);
	Cue->SetPoseCore(DomeCenter, Direction);
	Cue->SetDrive(ERbCueDrive::Input);
	Cues.Add(Cue);
	UE_LOG(LogRawBreak, Display,
		TEXT("RbCueDemo: %s cue (%.2f cm), azimuth %.2f deg, requested %.2f deg, floor %.3f deg by %s%s%s, shown %.3f deg, baked mesh %d, dome centre (%.4f, %.4f, %.4f) m"),
		*StaticEnum<ERbCuePreset>()->GetNameStringByValue(static_cast<int64>(CuePreset)), 100.0 * Spec.Length, AzimuthDeg, ElevationDeg,
		FMath::RadiansToDegrees(Floor.MinElevation), RbCueDemoPrivate::FloorName(Floor.FloorBy), Floor.bRaisedByEnvironment ? TEXT(" (raised by the environment)") : TEXT(""),
		Floor.bBlockedByEnvironment ? TEXT(" (BLOCKED by the environment)") : TEXT(""), FMath::RadiansToDegrees(ShownElevation),
		Cue->IsUsingBakedMesh() ? 1 : 0, DomeCenter.x, DomeCenter.y, DomeCenter.z);
}

void ARbCueDemo::BuildLineup()
{
	// Every preset lying on the bed along +x (tips toward the foot), LineupSpacingCm apart across the table; the taper makes a
	// lying cue pitch down toward the tip, so the pose rests the bottom generator on the cloth.
	static constexpr ERbCuePreset Presets[] = {ERbCuePreset::Playing19oz, ERbCuePreset::Break21oz, ERbCuePreset::Jump9oz, ERbCuePreset::House19oz};
	const rb::human::CueBodyState Body;
	int32 Index = 0;
	for (const ERbCuePreset Preset : Presets)
	{
		const rb::CueSpec Spec = rb::GetCueSpec(RbTypes::ToCore(Preset));
		ARbCue* Cue = GetWorld()->SpawnActor<ARbCue>();
		if (!Cue)
		{
			continue;
		}
		Cue->MaterialOverride = CueMaterialOverride;
		Cue->InitForTable(Table, Spec, Body);
		const double Slope = (Body.ButtRadius - Body.TipRadius) / Spec.Length;
		const double Pitch = FMath::Atan(Slope); // butt raised by the taper half angle
		const rb::Vec3 Direction = rb::MakeCueFrame(Pitch, 0.0).Axis;
		// Bottom generator on the cloth: the rim's axis point sits r_t / cos(pitch) above z = 0.
		const double RimDepth = 0.01 * RbCueMeshBuilder::RimDepthCm(Spec);
		const double Y = CueBall.Y + 0.01 * LineupSpacingCm * (static_cast<double>(Index) - 1.5);
		const rb::Vec3 Rim(CueBall.X, Y, Body.TipRadius / FMath::Cos(Pitch) + 1e-5);
		Cue->SetPoseCore(Rim - Direction * RimDepth, Direction);
		Cue->SetDrive(ERbCueDrive::Input);
		Cues.Add(Cue);
		++Index;
	}
	UE_LOG(LogRawBreak, Display, TEXT("RbCueDemo: lineup of %d cue presets"), Cues.Num());
}
