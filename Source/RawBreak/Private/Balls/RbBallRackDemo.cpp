#include "Balls/RbBallRackDemo.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Table/RbTable.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"

#include "rb/Equipment/Cue.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/TableRules.h"

// Owner: UE-2 (dev tool).

namespace
{
	// Uniform random rotation (Shoemake).
	rb::Quat RandomOrientation(FRandomStream& Random)
	{
		const double U1 = Random.GetFraction();
		const double U2 = Random.GetFraction();
		const double U3 = Random.GetFraction();
		const double A = FMath::Sqrt(1.0 - U1);
		const double B = FMath::Sqrt(U1);
		return rb::Normalized(rb::Quat(B * FMath::Cos(UE_DOUBLE_TWO_PI * U3), A * FMath::Sin(UE_DOUBLE_TWO_PI * U2),
			A * FMath::Cos(UE_DOUBLE_TWO_PI * U2), B * FMath::Sin(UE_DOUBLE_TWO_PI * U3)));
	}
}

ARbBallRackDemo::ARbBallRackDemo()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ARbBallRackDemo::BuildRackInput(const TSharedPtr<const FRbTableContext>& Context, ERbDiscipline InDiscipline, int32 Seed,
	bool bInRandomOrientations, const FVector2D& CueOffset, rb::SimInput& Out, FString& OutError)
{
	if (!Context.IsValid())
	{
		OutError = TEXT("no table context");
		return false;
	}
	RbShot::InitSimInput(*Context, Out);
	const rb::rules::RulesConfig Config = rb::rules::MakeRulesConfig(RbTypes::RulesPresetFor(InDiscipline));
	rb::rules::RackAssignment Rack;
	const rb::ErrorCode Error = rb::rules::GenerateRack(RbTypes::ToCore(InDiscipline), Config, Context->RulesTable, static_cast<uint64>(Seed), false,
		rb::kRackGapWoodenRack, Rack);
	if (!rb::Succeeded(Error))
	{
		OutError = FString::Printf(TEXT("GenerateRack failed: %hs"), rb::ToString(Error));
		return false;
	}

	FRandomStream Random(Seed);
	auto Place = [&](int32 Id, const rb::Vec2& P)
	{
		rb::SimBall& Ball = Out.Balls[Id];
		Ball.InPlay = true;
		Ball.State = rb::BallState{};
		Ball.State.Position = rb::Vec3(P.x, P.y, Ball.Spec.Radius);
		Ball.Orientation = bInRandomOrientations ? RandomOrientation(Random) : rb::Quat::Identity();
	};
	for (int32 Id = 1; Id < rb::rules::kRulesBallCount && Id < Context->Balls.Count; ++Id)
	{
		if (Rack.Racked[Id])
		{
			Place(Id, Rack.Position[Id]);
		}
	}
	const rb::Vec2 Head = Context->RulesTable.HeadSpot;
	Place(rb::kCueBallId, rb::Vec2(Head.x + CueOffset.X, Head.y + CueOffset.Y));
	return true;
}

TSharedPtr<const FRbShot> ARbBallRackDemo::SimulateStrike(const TSharedPtr<const FRbTableContext>& Context, const rb::SimInput& Input,
	const rb::CueStrikeInput& Strike, FString& OutError)
{
	if (!Context.IsValid())
	{
		OutError = TEXT("no table context");
		return nullptr;
	}
	static uint32 NextDemoShotId = 0x40000000u; // far from the simulation subsystem's ids

	const TSharedRef<FRbShot> NewShot = MakeShared<FRbShot>();
	NewShot->Id = ++NextDemoShotId;
	NewShot->Request.Table = Context;
	NewShot->Request.Input = Input;
	NewShot->Request.Input.Table = &Context->Geometry;
	NewShot->Request.Input.Strikes.Clear();
	rb::StrikeRequest Request;
	Request.Ball = static_cast<rb::BallId>(rb::kCueBallId);
	Request.Input = Strike;
	NewShot->Request.Input.Strikes.PushBack(Request);

	TUniquePtr<rb::Simulator> Simulator = MakeUnique<rb::Simulator>();
	TUniquePtr<rb::ShotResult> Full = MakeUnique<rb::ShotResult>();
	const double Start = FPlatformTime::Seconds();
	const rb::SimStatus Status = Simulator->Run(NewShot->Request.Input, *Full);
	NewShot->SimMilliseconds = 1000.0 * (FPlatformTime::Seconds() - Start);
	if (Status != rb::SimStatus::Ok)
	{
		OutError = FString::Printf(TEXT("Simulator::Run status %d (input error %hs)"), static_cast<int32>(Status), rb::ToString(Full->Diagnostics.InputError));
		return nullptr;
	}
	RbShot::CopyCompact(*Full, NewShot->Result);
	NewShot->ResultHash = RbShot::ResultHash(NewShot->Result);
	NewShot->Request.ContactTime = FPlatformTime::Seconds();
	return NewShot;
}

void ARbBallRackDemo::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	ARbTable* UseTable = Table;
	if (!UseTable)
	{
		for (TActorIterator<ARbTable> It(World); It; ++It)
		{
			UseTable = *It;
			break;
		}
	}
	if (!UseTable)
	{
		UseTable = World->SpawnActor<ARbTable>(GetActorLocation(), GetActorRotation());
	}
	if (UseTable && !UseTable->HasContext())
	{
		UseTable->RebuildTable();
	}
	if (!UseTable || !UseTable->HasContext())
	{
		UE_LOG(LogRawBreak, Error, TEXT("ARbBallRackDemo %s: no table"), *GetName());
		return;
	}

	BallSet = World->SpawnActor<ARbBallSet>();
	BallSet->BallMaterialOverride = BallMaterialOverride;
	BallSet->InitForTable(UseTable);

	rb::SimInput Input;
	FString Error;
	if (!BuildRackInput(UseTable->GetContextPtr(), Discipline, RackSeed, bRandomOrientations, CueBallOffset, Input, Error))
	{
		UE_LOG(LogRawBreak, Error, TEXT("ARbBallRackDemo %s: %s"), *GetName(), *Error);
		return;
	}
	BallSet->ShowSimBalls(Input.Balls, rb::kMaxBalls);
	UE_LOG(LogRawBreak, Display, TEXT("ARbBallRackDemo: racked %d balls (discipline %d, seed %d)"), BallSet->GetBallCount(),
		static_cast<int32>(Discipline), RackSeed);

	if (!bPlayBreak)
	{
		return;
	}
	const rb::Vec3 CueBall = Input.Balls[rb::kCueBallId].State.Position;
	const rb::Vec2 Foot = UseTable->GetContext().RulesTable.FootSpot;
	rb::CueStrikeInput Strike;
	Strike.Cue = rb::kCueBreak21oz;
	Strike.Speed = BreakSpeed;
	Strike.Azimuth = FMath::Atan2(Foot.y - CueBall.y, Foot.x - CueBall.x) + FMath::DegreesToRadians(static_cast<double>(BreakAimOffsetDeg));
	Strike.OffsetA = BreakTipOffset.X;
	Strike.OffsetB = BreakTipOffset.Y;
	Shot = SimulateStrike(UseTable->GetContextPtr(), Input, Strike, Error);
	if (!Shot.IsValid())
	{
		UE_LOG(LogRawBreak, Error, TEXT("ARbBallRackDemo %s: %s"), *GetName(), *Error);
		return;
	}
	UE_LOG(LogRawBreak, Display, TEXT("ARbBallRackDemo: break simulated in %.2f ms, stop time %.3f s, %d events"), Shot->SimMilliseconds,
		Shot->Result.StopTime, static_cast<int32>(Shot->Result.Events.size()));
	URbShotPlaybackComponent* Playback = BallSet->GetPlayback();
	Playback->Play(Shot.ToSharedRef(), false, 0.0, PlaybackRate);
	if (FreezeAtShotTime >= 0.0f)
	{
		Playback->SeekTo(FreezeAtShotTime);
		Playback->SetPaused(true);
	}
}
