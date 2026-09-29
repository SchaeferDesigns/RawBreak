#include "Balls/RbLooseBallSubsystem.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBall.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Interaction/RbInteractionSubsystem.h"
#include "Player/RbBallInHandComponent.h"
#include "Player/RbStrokeComponent.h"
#include "Simulation/RbShot.h"
#include "Table/RbTable.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"

#include "rb/Physics/Playback.h"

// Owner: M2-E. Hand-off, awaiting return, pick-up, automatic returns, replays (rules in the header). Tests:
// RawBreak.Unit.LooseBall.* (RbLooseBallTests.cpp) and RawBreak.Functional.LooseBall (RbLooseBallFunctionalTest.cpp).

namespace RbLooseBallSubsystemPrivate
{
	TAutoConsoleVariable<int32> CVarLooseBallEnable(TEXT("rb.LooseBall.Enable"), 1,
		TEXT("Balls that leave the table continue under engine physics (1) or disappear at the hand-off time (0). Presentation only."),
		ECVF_Default);

	constexpr double ReachCheckInterval = 0.5;             // [s] between reachability checks of a resting ball
	const double ReachDistancesCm[] = {35.0, 70.0, 105.0};  // standing spots around the ball (plus its radius)
	constexpr int32 ReachDirections = 12;
	constexpr double StandMaxAboveBallCm = 30.0;            // the ball may lie at most this far below the standing floor

	void ListCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		URbLooseBallSubsystem* Subsystem = URbLooseBallSubsystem::Get(World);
		if (!Subsystem)
		{
			return;
		}
		for (ARbLooseBall* Ball : Subsystem->GetLooseBalls())
		{
			const FVector P = Ball->GetActorLocation();
			UE_LOG(LogRawBreak, Display, TEXT("RbLooseBall: table=%d ball=%d at (%.1f, %.1f, %.1f) cm speed=%.1f cm/s resting=%d impacts=%d reachable=%d"),
				Ball->GetTableIndex(), Ball->GetBallId(), P.X, P.Y, P.Z, Ball->GetLinearVelocity().Size(), Ball->IsResting() ? 1 : 0,
				Ball->GetImpactCount(), Subsystem->IsReachable(*Ball) ? 1 : 0);
		}
		UE_LOG(LogRawBreak, Display, TEXT("RbLooseBall: %d loose ball(s)"), Subsystem->GetNumLooseBalls());
	}

	void ReturnAllCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (URbLooseBallSubsystem* Subsystem = URbLooseBallSubsystem::Get(World))
		{
			UE_LOG(LogRawBreak, Display, TEXT("RbLooseBall: returned %d"), Subsystem->ReturnAll(INDEX_NONE, ERbLooseBallReturn::Manual));
		}
	}

	// rb.LooseBall.Drop <table index> <ball id> <x> <y> <z> [<vx> <vy> <vz>]: hands a ball off at a core state of that table.
	void DropCommand(const TArray<FString>& Args, UWorld* World)
	{
		URbLooseBallSubsystem* Subsystem = URbLooseBallSubsystem::Get(World);
		URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
		double V[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
		bool bValid = Subsystem && Tables && Args.Num() >= 5;
		for (int32 i = 0; bValid && i < Args.Num() && i < 8; ++i)
		{
			bValid = Args[i].IsNumeric();
			V[i] = FCString::Atod(*Args[i]);
		}
		ARbTable* Table = bValid ? Tables->FindTable(static_cast<int32>(V[0])) : nullptr;
		ARbBallSet* Balls = Table ? Tables->FindBallSet(Table) : nullptr;
		if (!Balls)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.LooseBall.Drop <table> <ball> <x> <y> <z> [<vx> <vy> <vz>] (core frame of that table, m, m/s)"));
			return;
		}
		rb::BallState State;
		State.Position = rb::Vec3(V[2], V[3], V[4]);
		State.Velocity = rb::Vec3(V[5], V[6], V[7]);
		const int32 BallId = static_cast<int32>(V[1]);
		const ARbLooseBall* Loose = Subsystem->HandOff(*Balls, BallId, State, rb::Quat::Identity(), rb::OffTableReason::Floor, 0);
		UE_LOG(LogRawBreak, Display, TEXT("rb.LooseBall.Drop: %s"), Loose ? TEXT("dropped") : TEXT("refused"));
	}

	FAutoConsoleCommandWithWorldAndArgs GListCommand(TEXT("rb.LooseBall.List"), TEXT("Logs every loose ball (RbLooseBall: ...)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GReturnAllCommand(TEXT("rb.LooseBall.ReturnAll"), TEXT("Returns every loose ball to its table."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ReturnAllCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GDropCommand(TEXT("rb.LooseBall.Drop"),
		TEXT("Hands a ball off at a core state: <table index> <ball id> <x> <y> <z> [<vx> <vy> <vz>] (m, m/s, that table's core frame)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DropCommand), ECVF_Cheat);
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------------------------------

URbLooseBallSubsystem* URbLooseBallSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbLooseBallSubsystem>() : nullptr;
}

bool URbLooseBallSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void URbLooseBallSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (URbInteractionSubsystem* Interaction = Collection.InitializeDependency<URbInteractionSubsystem>())
	{
		const TWeakObjectPtr<URbLooseBallSubsystem> WeakThis(this);
		FRbInteractionProvider Provider;
		Provider.Offer = [WeakThis](const FRbInteractionQuery& Query, FText& OutVerb)
		{
			const URbLooseBallSubsystem* Self = WeakThis.Get();
			if (!Self || !Self->FindGazedBall(Query))
			{
				return false;
			}
			OutVerb = PickUpVerb();
			return true;
		};
		Provider.Interact = [WeakThis](const FRbInteractionQuery& Query)
		{
			URbLooseBallSubsystem* Self = WeakThis.Get();
			ARbLooseBall* Ball = Self ? Self->FindGazedBall(Query) : nullptr;
			return Ball && Self->PickUp(*Ball, Query.Pawn);
		};
		ProviderHandle = Interaction->AddProvider(MoveTemp(Provider));
	}
}

void URbLooseBallSubsystem::Deinitialize()
{
	if (URbInteractionSubsystem* Interaction = URbInteractionSubsystem::Get(this))
	{
		Interaction->RemoveProvider(ProviderHandle);
	}
	ProviderHandle.Reset();
	for (const FBinding& Binding : Bindings)
	{
		if (ARbBallSet* Balls = Binding.BallSet.Get())
		{
			if (URbShotPlaybackComponent* Playback = Balls->GetPlayback())
			{
				Playback->OnShotEvent.Remove(Binding.EventHandle);
			}
		}
	}
	Bindings.Reset();
	Entries.Reset();
	Watches.Reset();
	Super::Deinitialize();
}

TStatId URbLooseBallSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URbLooseBallSubsystem, STATGROUP_Tickables);
}

void URbLooseBallSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateAutomaticReturns(DeltaTime);
}

bool URbLooseBallSubsystem::IsEnabled() const
{
	return bEnabled && RbLooseBallSubsystemPrivate::CVarLooseBallEnable.GetValueOnGameThread() != 0;
}

FText URbLooseBallSubsystem::PickUpVerb()
{
	return NSLOCTEXT("RawBreak", "PickUpTheBall", "Pick up the ball");
}

// ---------------------------------------------------------------------------------------------------------------------
// Binding and hand-off
// ---------------------------------------------------------------------------------------------------------------------

bool URbLooseBallSubsystem::IsBound(const ARbBallSet* BallSet) const
{
	return BallSet && Bindings.ContainsByPredicate([BallSet](const FBinding& B) { return B.BallSet.Get() == BallSet; });
}

void URbLooseBallSubsystem::BindBallSet(ARbBallSet* BallSet)
{
	if (!BallSet)
	{
		return;
	}
	if (!IsBound(BallSet))
	{
		FBinding Binding;
		Binding.BallSet = BallSet;
		if (URbShotPlaybackComponent* Playback = BallSet->GetPlayback())
		{
			Binding.EventHandle = Playback->OnShotEvent.AddUObject(this, &URbLooseBallSubsystem::HandleShotEvent, TWeakObjectPtr<ARbBallSet>(BallSet));
		}
		Bindings.Add(Binding);
	}
	// Re-initialised ball sets (InitForTable) keep their balls that lie off the table withheld.
	BallSet->SetWithholdSuspended(bReplayActive);
	for (const FEntry& Entry : Entries)
	{
		if (Entry.BallSet.Get() == BallSet && Entry.Ball.IsValid())
		{
			BallSet->SetBallWithheld(Entry.BallId, true);
		}
	}
}

void URbLooseBallSubsystem::UnbindBallSet(ARbBallSet* BallSet)
{
	for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
	{
		const FBinding& Binding = Bindings[Index];
		if (Binding.BallSet.Get() == BallSet || !Binding.BallSet.IsValid())
		{
			if (ARbBallSet* Balls = Binding.BallSet.Get())
			{
				if (URbShotPlaybackComponent* Playback = Balls->GetPlayback())
				{
					Playback->OnShotEvent.Remove(Binding.EventHandle);
				}
			}
			Bindings.RemoveAt(Index);
		}
	}
}

void URbLooseBallSubsystem::HandleShotEvent(const TSharedRef<const FRbShot>& Shot, int32 EventIndex, TWeakObjectPtr<ARbBallSet> WeakBalls)
{
	ARbBallSet* Balls = WeakBalls.Get();
	const std::vector<rb::ShotEvent>& Events = Shot->Result.Events;
	if (!Balls || EventIndex < 0 || EventIndex >= static_cast<int32>(Events.size()) || Events[EventIndex].Type != rb::ShotEventType::BallOffTable)
	{
		return;
	}
	// Live shots only: a replay shows the ball leaving and hides it at the hand-off time (URbShotPlaybackComponent::HideTimeOf).
	const URbShotPlaybackComponent* Playback = Balls->GetPlayback();
	if (!Playback || !Playback->IsPlaying() || !Playback->GetClockMapping().bLive)
	{
		return;
	}
	rb::BallState State;
	rb::Quat Orientation;
	if (!GetHandOffState(*Shot, EventIndex, State, Orientation))
	{
		return;
	}
	const rb::ShotEvent& Event = Events[EventIndex];
	HandOff(*Balls, Event.A, State, Orientation, static_cast<rb::OffTableReason>(Event.Feature), Shot->Id);
}

bool URbLooseBallSubsystem::GetHandOffState(const FRbShot& Shot, int32 EventIndex, rb::BallState& OutState, rb::Quat& OutOrientation)
{
	const rb::ShotResult& Result = Shot.Result;
	if (EventIndex < 0 || EventIndex >= static_cast<int32>(Result.Events.size()))
	{
		return false;
	}
	const rb::ShotEvent& Event = Result.Events[EventIndex];
	const int32 Ball = Event.A;
	if (Event.Type != rb::ShotEventType::BallOffTable || Ball < 0 || Ball >= rb::kMaxBalls)
	{
		return false;
	}
	// The orientation at the hand-off: the Terminal segment starts there (the final orientation of a ball off the table).
	const rb::BallFinal& Final = Result.Finals[Ball];
	if (Final.Status == rb::BallFinalStatus::OffTable)
	{
		OutOrientation = Final.Orientation;
	}
	else if (!rb::OrientationAt(Result, Ball, Event.Time, OutOrientation))
	{
		OutOrientation = rb::Quat::Identity();
	}
	if (Shot.Request.Input.Record.EventStates)
	{
		OutState = Event.Pre[0]; // the exact core state at the event (SimLoopEvents / SimRailTop)
		return true;
	}
	// Without event states: the end of the ball's last moving segment (the one before the Terminal segment).
	const std::vector<rb::TrajectorySegment>& Segments = Result.Tracks[Ball].Segments;
	for (int32 Index = static_cast<int32>(Segments.size()) - 1; Index >= 0; --Index)
	{
		if (Segments[Index].Kind != rb::SegmentKind::Terminal)
		{
			OutState = rb::EvaluateTrajectorySegment(Segments[Index], Event.Time);
			return true;
		}
	}
	return false;
}

ARbLooseBall* URbLooseBallSubsystem::HandOff(ARbBallSet& Balls, int32 BallId, const rb::BallState& CoreState)
{
	const rb::Quat Orientation = FRbCoords::OrientationToCore(Balls.GetBallOrientationUE(BallId));
	return HandOff(Balls, BallId, CoreState, Orientation, rb::OffTableReason::Floor, 0);
}

ARbLooseBall* URbLooseBallSubsystem::HandOff(ARbBallSet& Balls, int32 BallId, const rb::BallState& CoreState, const rb::Quat& Orientation,
	rb::OffTableReason Reason, uint32 ShotId)
{
	if (Reason == rb::OffTableReason::RestsOnRailOrFrame)
	{
		return nullptr; // no physics: the ball stays where the core froze it (the playback shows it there until the finals)
	}
	UWorld* World = GetWorld();
	ARbTable* Table = Balls.GetTable();
	if (!IsEnabled() || !World || !Table || !Table->HasContext() || BallId < 0 || BallId >= Balls.GetBallCount())
	{
		return nullptr;
	}
	const int32 TableIndex = Table->TableIndex;
	if (FEntry* Previous = FindEntry(TableIndex, BallId))
	{
		ReturnEntry(static_cast<int32>(Previous - Entries.GetData()), ERbLooseBallReturn::Replaced); // it left the table again
	}

	// Core (table frame) -> world: position, orientation, velocity (a vector) and spin (a pseudovector, FRbCoords).
	const FTransform TableToWorld = Table->GetTableToWorld();
	const FVector Location = Table->CoreToWorld(CoreState.Position);
	const FQuat Rotation = Table->CoreOrientationToWorld(Orientation);
	const FVector Velocity = TableToWorld.TransformVectorNoScale(FRbCoords::VelocityToUE(CoreState.Velocity));
	const FVector Spin = TableToWorld.TransformVectorNoScale(FRbCoords::AngularVelocityToUE(CoreState.Omega));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ARbLooseBall* Loose = World->SpawnActor<ARbLooseBall>(ARbLooseBall::StaticClass(), FTransform(Rotation, Location), Params);
	if (!Loose)
	{
		return nullptr;
	}
	const FRbTableContext& Context = Table->GetContext();
	const double MassKg = BallId < Context.Balls.Count ? Context.Balls.Balls[BallId].Mass : rb::kStandardPoolBall.Mass;
	Loose->SetHandOffShotId(ShotId);
	Loose->Launch(Table, BallId, Balls.GetBallMesh(), Balls.GetBallMaterial(BallId), Balls.GetBallRadiusCm(BallId), MassKg, Velocity, Spin);
	if (bReplayActive)
	{
		Loose->SetActorHiddenInGame(true);
	}
	Balls.SetBallWithheld(BallId, true); // the table instance waits for the return

	FEntry Entry;
	Entry.Ball = Loose;
	Entry.BallSet = &Balls;
	Entry.TableIndex = TableIndex;
	Entry.BallId = BallId;
	Entry.ShotId = ShotId;
	Entries.Add(Entry);
	UE_LOG(LogRawBreak, Log, TEXT("RbLooseBall: table %d ball %d handed off at (%.1f, %.1f, %.1f) cm, v (%.1f, %.1f, %.1f) cm/s (shot %u)"),
		TableIndex, BallId, Location.X, Location.Y, Location.Z, Velocity.X, Velocity.Y, Velocity.Z, ShotId);
	OnHandOff.Broadcast(*Loose);
	return Loose;
}

// ---------------------------------------------------------------------------------------------------------------------
// Registry and returns
// ---------------------------------------------------------------------------------------------------------------------

URbLooseBallSubsystem::FEntry* URbLooseBallSubsystem::FindEntry(int32 TableIndex, int32 BallId)
{
	return Entries.FindByPredicate([TableIndex, BallId](const FEntry& E) { return E.TableIndex == TableIndex && E.BallId == BallId && E.Ball.IsValid(); });
}

const URbLooseBallSubsystem::FEntry* URbLooseBallSubsystem::FindEntry(const ARbLooseBall& Ball) const
{
	return Entries.FindByPredicate([&Ball](const FEntry& E) { return E.Ball.Get() == &Ball; });
}

ARbLooseBall* URbLooseBallSubsystem::FindLooseBall(int32 TableIndex, int32 BallId) const
{
	for (const FEntry& Entry : Entries)
	{
		ARbLooseBall* Ball = Entry.Ball.Get();
		if (Ball && Entry.TableIndex == TableIndex && Entry.BallId == BallId)
		{
			return Ball;
		}
	}
	return nullptr;
}

TArray<ARbLooseBall*> URbLooseBallSubsystem::GetLooseBalls(int32 TableIndex) const
{
	TArray<ARbLooseBall*> Out;
	for (const FEntry& Entry : Entries)
	{
		ARbLooseBall* Ball = Entry.Ball.Get();
		if (Ball && (TableIndex == INDEX_NONE || Entry.TableIndex == TableIndex))
		{
			Out.Add(Ball);
		}
	}
	return Out;
}

int32 URbLooseBallSubsystem::GetNumLooseBalls() const
{
	return GetLooseBalls(INDEX_NONE).Num();
}

void URbLooseBallSubsystem::ReturnEntry(int32 Index, ERbLooseBallReturn Reason)
{
	if (!Entries.IsValidIndex(Index))
	{
		return;
	}
	const FEntry Entry = Entries[Index];
	Entries.RemoveAt(Index);
	if (ARbLooseBall* Ball = Entry.Ball.Get())
	{
		Ball->Destroy();
	}
	if (ARbBallSet* Balls = Entry.BallSet.Get())
	{
		// Visible again only if the committed table state has the ball in play (the presentation's last request).
		Balls->SetBallWithheld(Entry.BallId, false);
	}
	LastReturnReason = Reason;
	++ReturnCount;
	UE_LOG(LogRawBreak, Log, TEXT("RbLooseBall: table %d ball %d returned (%s)"), Entry.TableIndex, Entry.BallId,
		*StaticEnum<ERbLooseBallReturn>()->GetNameStringByValue(static_cast<int64>(Reason)));
	OnReturned.Broadcast(Entry.TableIndex, Entry.BallId);
	OnReturnedWithReason.Broadcast(Entry.TableIndex, Entry.BallId, Reason);
}

bool URbLooseBallSubsystem::ReturnBall(int32 TableIndex, int32 BallId)
{
	return ReturnBall(TableIndex, BallId, ERbLooseBallReturn::Manual);
}

bool URbLooseBallSubsystem::ReturnBall(int32 TableIndex, int32 BallId, ERbLooseBallReturn Reason)
{
	const int32 Index = Entries.IndexOfByPredicate([TableIndex, BallId](const FEntry& E) { return E.TableIndex == TableIndex && E.BallId == BallId; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const bool bExisted = Entries[Index].Ball.IsValid();
	ReturnEntry(Index, bExisted ? Reason : ERbLooseBallReturn::Destroyed);
	return bExisted;
}

int32 URbLooseBallSubsystem::ReturnAll(int32 TableIndex)
{
	return ReturnAll(TableIndex, ERbLooseBallReturn::Manual);
}

int32 URbLooseBallSubsystem::ReturnAll(int32 TableIndex, ERbLooseBallReturn Reason)
{
	int32 Count = 0;
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		if (TableIndex == INDEX_NONE || Entries[Index].TableIndex == TableIndex)
		{
			Count += Entries[Index].Ball.IsValid() ? 1 : 0;
			ReturnEntry(Index, Entries[Index].Ball.IsValid() ? Reason : ERbLooseBallReturn::Destroyed);
		}
	}
	return Count;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pick-up
// ---------------------------------------------------------------------------------------------------------------------

ARbLooseBall* URbLooseBallSubsystem::FindGazedBall(const FRbInteractionQuery& Query) const
{
	UWorld* World = GetWorld();
	const FVector Direction = Query.Direction.GetSafeNormal();
	if (!World || bReplayActive || Direction.IsNearlyZero())
	{
		return nullptr;
	}
	ARbLooseBall* Best = nullptr;
	double BestMiss = TNumericLimits<double>::Max();
	for (const FEntry& Entry : Entries)
	{
		ARbLooseBall* Ball = Entry.Ball.Get();
		if (!Ball || Ball->IsHidden() || (!Ball->IsResting() && Ball->GetLinearVelocity().Size() > PickUpMaxSpeedCmS))
		{
			continue;
		}
		const FVector Center = Ball->GetActorLocation();
		const FVector ToBall = Center - Query.Eye;
		const double Along = ToBall | Direction;
		const double Miss = (ToBall - Direction * Along).Size();
		if (Along <= 0.0 || Miss > GazeToleranceCm + Ball->GetRadiusCm() || FVector2D(ToBall.X, ToBall.Y).Size() > ReachCm ||
			ToBall.Z < -PickUpMaxDropCm || ToBall.Z > 30.0 || Miss >= BestMiss)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RbLooseBallGaze), false, Query.Pawn);
		for (const FEntry& Other : Entries)
		{
			Params.AddIgnoredActor(Other.Ball.Get());
		}
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Query.Eye, Center, ECC_Visibility, Params) && Hit.bBlockingHit &&
			Hit.Distance < ToBall.Size() - Ball->GetRadiusCm() - 1.0)
		{
			continue; // something is in the way
		}
		Best = Ball;
		BestMiss = Miss;
	}
	return Best;
}

bool URbLooseBallSubsystem::PickUp(ARbLooseBall& Ball, APawn* Pawn)
{
	const int32 TableIndex = Ball.GetTableIndex();
	const int32 BallId = Ball.GetBallId();
	ARbTable* Table = Ball.GetTable();
	const double RadiusM = FRbCoords::MetersPerCm * Ball.GetRadiusCm();
	const URbTableSubsystem* Tables = URbTableSubsystem::Get(this);
	const FRbTableSession* Session = Tables ? Tables->FindSession(TableIndex) : nullptr;
	URbMatchDirector* Director = Session ? Session->Director.Get() : nullptr;
	if (!ReturnBall(TableIndex, BallId, ERbLooseBallReturn::PickUp))
	{
		return false;
	}
	// The cue ball in hand goes into the carrying hand (M2-F's URbBallInHandComponent), which then places it.
	if (BallId == rb::kCueBallId && Pawn && Table && Director && Director->GetPhase() == ERbDirectorPhase::AwaitPlacement)
	{
		URbBallInHandComponent* Hand = Pawn->FindComponentByClass<URbBallInHandComponent>();
		if (Hand && (Hand->GetState() == ERbBallInHandState::Inactive || Hand->GetState() == ERbBallInHandState::Placed))
		{
			const TWeakObjectPtr<URbMatchDirector> WeakDirector(Director);
			Hand->BeginCarry(Table, BallId, RadiusM, [WeakDirector](const rb::Vec2& Plan)
			{
				const URbMatchDirector* D = WeakDirector.Get();
				return D && D->CanPlaceCueBall(Plan);
			});
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Automatic returns
// ---------------------------------------------------------------------------------------------------------------------

bool URbLooseBallSubsystem::IsBelowKillZ(const ARbLooseBall& Ball) const
{
	const double Z = Ball.GetActorLocation().Z;
	const UWorld* World = GetWorld();
	const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	if (Settings && Z < Settings->KillZ)
	{
		return true;
	}
	const ARbTable* Table = Ball.GetTable();
	return Table && Z < Table->GetActorLocation().Z - FallBelowFloorCm;
}

bool URbLooseBallSubsystem::IsInReturnVolume(const ARbLooseBall& Ball) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RbLooseBallReturnVolume), false, &Ball);
	World->OverlapMultiByChannel(Overlaps, Ball.GetActorLocation(), FQuat::Identity, RbAssetPaths::Collision::LooseBallChannel,
		FCollisionShape::MakeSphere(static_cast<float>(Ball.GetRadiusCm())), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UPrimitiveComponent* Component = Overlap.GetComponent();
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		if (Component && (Component->GetCollisionProfileName() == RbAssetPaths::Collision::BallReturnProfile ||
			Component->ComponentHasTag(RbAssetPaths::Tag::BallReturnVolume) || (Owner && Owner->ActorHasTag(RbAssetPaths::Tag::BallReturnVolume))))
		{
			return true;
		}
	}
	return false;
}

bool URbLooseBallSubsystem::IsReachable(const ARbLooseBall& Ball) const
{
	using namespace RbLooseBallSubsystemPrivate;
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	const FVector Center = Ball.GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RbLooseBallReach), false, &Ball);
	for (const FEntry& Entry : Entries)
	{
		Params.AddIgnoredActor(Entry.Ball.Get());
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			Params.AddIgnoredActor(PC->GetPawn());
		}
	}
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(static_cast<float>(PawnRadiusCm), static_cast<float>(PawnHalfHeightCm));
	for (const double Distance : ReachDistancesCm)
	{
		for (int32 Direction = 0; Direction < ReachDirections; ++Direction)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * (Direction + 0.5 * (Distance > 50.0 ? 1.0 : 0.0)) / ReachDirections;
			const double R = Distance + Ball.GetRadiusCm();
			const FVector Spot(Center.X + R * FMath::Cos(Angle), Center.Y + R * FMath::Sin(Angle), Center.Z);
			// The floor under the spot (whatever the pawn can stand on).
			FHitResult Floor;
			if (!World->LineTraceSingleByChannel(Floor, Spot + FVector(0.0, 0.0, 60.0), Spot - FVector(0.0, 0.0, PickUpMaxDropCm + 60.0), ECC_Pawn, Params) ||
				!Floor.bBlockingHit || Floor.ImpactNormal.Z < 0.7)
			{
				continue;
			}
			const double FloorZ = Floor.ImpactPoint.Z;
			if (Center.Z < FloorZ - StandMaxAboveBallCm || Center.Z > FloorZ + PickUpMaxDropCm)
			{
				continue;
			}
			// A pawn fits there ...
			const FVector Standing(Spot.X, Spot.Y, FloorZ + PawnHalfHeightCm + 2.0);
			if (World->OverlapBlockingTestByChannel(Standing, FQuat::Identity, ECC_Pawn, Capsule, Params))
			{
				continue;
			}
			// ... and sees the ball.
			const FVector Eye(Spot.X, Spot.Y, FloorZ + PawnEyeHeightCm);
			FHitResult Block;
			if (World->LineTraceSingleByChannel(Block, Eye, Center, ECC_Visibility, Params) && Block.bBlockingHit &&
				Block.Distance < (Center - Eye).Size() - Ball.GetRadiusCm() - 1.0)
			{
				continue;
			}
			return true;
		}
	}
	return false;
}

double URbLooseBallSubsystem::GetUnreachableSeconds(const ARbLooseBall& Ball) const
{
	const FEntry* Entry = FindEntry(Ball);
	return Entry ? Entry->UnreachableSeconds : 0.0;
}

void URbLooseBallSubsystem::UpdateSessions()
{
	const URbTableSubsystem* Tables = URbTableSubsystem::Get(this);
	if (!Tables)
	{
		return;
	}
	for (const FRbTableSession& Session : Tables->GetSessions())
	{
		const URbMatchDirector* Director = Session.Director.Get();
		if (!Director)
		{
			continue;
		}
		const int32 TableIndex = Session.TableIndex;
		FTableWatch& Watch = Watches.FindOrAdd(TableIndex);
		const FTableWatch Before = Watch;
		const ERbDirectorPhase Phase = Director->GetPhase();
		Watch.bValid = true;
		Watch.RackNumber = Director->GetMatchState().RackNumber;
		Watch.MatchSeed = Director->GetMatchSeed();
		Watch.MatchShotIndex = Director->GetMatchShotIndex();
		Watch.Phase = static_cast<uint8>(Phase);
		if (GetLooseBalls(TableIndex).IsEmpty())
		{
			continue;
		}
		if (Before.bValid && (Before.RackNumber != Watch.RackNumber || Before.MatchSeed != Watch.MatchSeed || Watch.MatchShotIndex < Before.MatchShotIndex))
		{
			ReturnAll(TableIndex, ERbLooseBallReturn::NewRack); // the balls are racked again (new rack or new match)
			continue;
		}
		const URbStrokeComponent* Stroke = Director->GetStrokeComponent();
		if (Stroke && (Stroke->GetPhase() == ERbStrokePhase::GettingDown || Stroke->GetPhase() == ERbStrokePhase::Down))
		{
			ReturnAll(TableIndex, ERbLooseBallReturn::Address); // the player addresses the next shot
			continue;
		}
		// A later shot than the one a ball left the table in was submitted (scripted strikes of cheats / tests have no get-down):
		// simulating, playing back or already committed (a rate-0 shot commits in the frame it was simulated).
		const TSharedPtr<const FRbShot> Pending = Director->GetPendingShot();
		const TSharedPtr<const FRbShot> Committed = Director->GetLastCommittedShot();
		const uint32 PendingId = Pending.IsValid() ? Pending->Id : 0u;
		for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
		{
			const FEntry& Entry = Entries[Index];
			if (Entry.TableIndex != TableIndex || !Entry.Ball.IsValid() || (Entry.ShotId != 0 && Entry.ShotId == PendingId))
			{
				continue;
			}
			const bool bLaterPending = PendingId != 0u;
			const bool bLaterCommitted = Entry.ShotId != 0 && Committed.IsValid() && Committed->Id > Entry.ShotId;
			if (Phase == ERbDirectorPhase::Simulating || bLaterPending || bLaterCommitted)
			{
				ReturnEntry(Index, ERbLooseBallReturn::Address);
			}
		}
		// The cue ball: placed on the table (ball in hand) or carried by the hand. Never for the shot that is still playing.
		const FEntry* Cue = FindEntry(TableIndex, rb::kCueBallId);
		if (!Cue || (Cue->ShotId != 0 && Cue->ShotId == PendingId) || Phase == ERbDirectorPhase::Simulating || Phase == ERbDirectorPhase::PlayingBack)
		{
			continue;
		}
		if (Director->GetTableState().Balls[rb::kCueBallId].InPlay)
		{
			ReturnBall(TableIndex, rb::kCueBallId, ERbLooseBallReturn::CueBallPlaced);
			continue;
		}
		const AActor* Pawn = Stroke ? Stroke->GetOwner() : nullptr;
		const URbBallInHandComponent* Hand = Pawn ? Pawn->FindComponentByClass<URbBallInHandComponent>() : nullptr;
		if (Hand && (Hand->GetState() == ERbBallInHandState::Carrying || Hand->GetState() == ERbBallInHandState::Lowering ||
			Hand->GetState() == ERbBallInHandState::Refused))
		{
			ReturnBall(TableIndex, rb::kCueBallId, ERbLooseBallReturn::Carried);
		}
	}
}

void URbLooseBallSubsystem::UpdateAutomaticReturns(double DeltaSeconds)
{
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		if (!Entries[Index].Ball.IsValid())
		{
			ReturnEntry(Index, ERbLooseBallReturn::Destroyed); // FellOutOfWorld, level unload
		}
	}
	if (bReplayActive || Entries.IsEmpty())
	{
		UpdateSessions(); // keeps the watches current (rack / phase changes during a replay are not "new")
		return;
	}
	UpdateSessions();
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Entries[Index];
		const ARbLooseBall* Ball = Entry.Ball.Get();
		if (!Ball)
		{
			continue;
		}
		if (IsBelowKillZ(*Ball))
		{
			ReturnEntry(Index, ERbLooseBallReturn::KillZ);
			continue;
		}
		if (!Ball->IsResting())
		{
			Entry.UnreachableSeconds = 0.0;
			Entry.ReachCheckIn = 0.0;
			continue;
		}
		if (IsInReturnVolume(*Ball))
		{
			ReturnEntry(Index, ERbLooseBallReturn::ReturnVolume);
			continue;
		}
		Entry.ReachCheckIn -= DeltaSeconds;
		if (Entry.ReachCheckIn <= 0.0)
		{
			Entry.bReachable = IsReachable(*Ball);
			Entry.ReachCheckIn = RbLooseBallSubsystemPrivate::ReachCheckInterval;
		}
		Entry.UnreachableSeconds = Entry.bReachable ? 0.0 : Entry.UnreachableSeconds + DeltaSeconds;
		if (Entry.UnreachableSeconds >= UnreachableReturnSeconds)
		{
			ReturnEntry(Index, ERbLooseBallReturn::Unreachable); // "the bartender brings it"
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Replays
// ---------------------------------------------------------------------------------------------------------------------

void URbLooseBallSubsystem::SetReplayActive(bool bActive)
{
	if (bReplayActive == bActive)
	{
		return;
	}
	bReplayActive = bActive;
	for (const FBinding& Binding : Bindings)
	{
		if (ARbBallSet* Balls = Binding.BallSet.Get())
		{
			Balls->SetWithholdSuspended(bActive);
		}
	}
	for (const FEntry& Entry : Entries)
	{
		if (ARbLooseBall* Ball = Entry.Ball.Get())
		{
			Ball->SetActorHiddenInGame(bActive);
		}
	}
}
