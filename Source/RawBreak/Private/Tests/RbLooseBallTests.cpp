// Balls off the table (M2-E, Docs/ue-architecture.md 18.6.1): RawBreak.Unit.LooseBall.*
//   HandOff_ExactCoreState        a simulated jump shot leaves the table (reason Floor); the loose ball starts at the event's core
//                                 state (position 0.1 mm, velocity / spin 1e-6 relative) on a translated + yawed table
//   RestsOnRailSpawnsNothing      RestsOnRailOrFrame hands nothing off; the playback's hide rule per final status
//   AwaitingReturn                the table instance stays hidden while its loose copy exists, whatever the presentation asks;
//                                 a return shows it only if the table state has it in play; replays suspend both; re-hand-off
//   LiveHandsOff_ReplaySpawnsNothing  the live playback hands off exactly once at the event; a replay of the same shot hides the
//                                 ball at the hand-off time and spawns nothing; the switch off spawns nothing
//   MatchUnchanged_ObjectBallAwaitsReturn  a director shot that sends the 9 off the table: the committed ResultHash and GameState
//                                 equal a run with the subsystem disabled; the respotted 9 awaits its return; a later shot returns it
//   CueBallInHand                 the cue ball off the table: pick-up by interaction (gaze, reach, verb) into the carrying hand,
//                                 the placement returns a loose cue ball, a new rack returns every loose ball
//   AutomaticReturns              kill Z, a return volume, reachable vs unreachable (UnreachableReturnSeconds)
// World tests without ticking: the physics never steps here (the bounce and the rest: RawBreak.Functional.LooseBall).
// Owner: M2-E.

#include "Balls/RbBallRackDemo.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbBallTestSupport.h"
#include "Balls/RbLooseBall.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Interaction/RbInteractionSubsystem.h"
#include "Player/RbBallInHandComponent.h"
#include "Tests/RbTestFlags.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/DefaultPawn.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "rb/Equipment/Cue.h"

#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbLooseBallTest
{
	// The cue ball near the +y side rail jumps (playing cue, 4 m/s, azimuth 90 deg, elevation 15 deg) and hops over the rail:
	// BallOffTable, reason Floor, ~0.15 s after the strike (rbsim --ball 0:0.2,0.3 --speed 4 --aim 90 --elevation 15).
	TSharedPtr<const FRbShot> SimulateJumpOff(const TSharedPtr<const FRbTableContext>& Context, FString& OutError)
	{
		rb::SimInput Input;
		RbShot::InitSimInput(*Context, Input);
		Input.Balls[0].InPlay = true;
		Input.Balls[0].State.Position = rb::Vec3(0.2, 0.3, Context->BallRadius(0));
		Input.Balls[0].Orientation = rb::Normalized(rb::Quat(0.9, 0.1, -0.3, 0.2));
		rb::CueStrikeInput Strike;
		Strike.Cue = rb::kCuePlaying19oz;
		Strike.Speed = 4.0;
		Strike.Azimuth = 0.5 * UE_DOUBLE_PI;
		Strike.Elevation = FMath::DegreesToRadians(15.0);
		return ARbBallRackDemo::SimulateStrike(Context, Input, Strike, OutError);
	}

	int32 FindOffTableEvent(const rb::ShotResult& Result, int32 Ball)
	{
		for (int32 Index = 0; Index < static_cast<int32>(Result.Events.size()); ++Index)
		{
			const rb::ShotEvent& Event = Result.Events[Index];
			if (Event.Type == rb::ShotEventType::BallOffTable && Event.A == Ball)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// A static floor slab / block (engine cube, BlockAll) with its top face at TopZ.
	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Center, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Center));
		if (Block && Cube)
		{
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Block->SetActorScale3D(SizeCm / 100.0);
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		}
		return Block;
	}

	// A ball at rest (no physics steps run in these tests): handed off with zero velocity and put to sleep.
	ARbLooseBall* DropResting(URbLooseBallSubsystem& Subsystem, ARbBallSet& Balls, int32 BallId, const FVector& World)
	{
		rb::BallState State;
		State.Position = Balls.GetTable()->WorldToCore(World);
		ARbLooseBall* Ball = Subsystem.HandOff(Balls, BallId, State, rb::Quat::Identity(), rb::OffTableReason::Floor, 0);
		if (Ball)
		{
			Ball->GetBodyComponent()->PutRigidBodyToSleep();
		}
		return Ball;
	}

	FString GameStateDigest(const rb::rules::MatchState& S)
	{
		const rb::rules::GameState& G = S.Game;
		FString Out = FString::Printf(TEXT("phase=%d rack=%d shooter=%d breaker=%d cue=%d break=%d push=%d open=%d free=%d visits=%d fouls=%d:%d score=%d:%d wins=%d:%d"),
			static_cast<int32>(S.Phase), S.RackNumber, G.Shooter, G.RackBreaker, static_cast<int32>(G.CueBall), G.IsBreakShot ? 1 : 0,
			G.PushOutAvailable ? 1 : 0, G.TableOpen ? 1 : 0, G.FreeShot ? 1 : 0, G.VisitsRemaining, G.Players[0].ConsecutiveFouls, G.Players[1].ConsecutiveFouls,
			G.Players[0].Score, G.Players[1].Score, S.RackWins[0], S.RackWins[1]);
		for (int32 Id = 0; Id < rb::rules::kRulesBallCount; ++Id)
		{
			const rb::rules::BallStatus& B = G.Balls[Id];
			Out += FString::Printf(TEXT(" %d:%d(%016llx,%016llx)"), Id, static_cast<int32>(B.Kind), std::bit_cast<uint64>(B.Position.x), std::bit_cast<uint64>(B.Position.y));
		}
		return Out;
	}

	// Plays the director's pending live shot to its end with a manual clock (the test world never ticks).
	void PlayPendingToEnd(URbMatchDirector& Director, URbShotPlaybackComponent& Playback, const TSharedRef<double>& Clock)
	{
		const TSharedPtr<const FRbShot> Shot = Director.GetPendingShot();
		if (!Shot.IsValid())
		{
			return;
		}
		const double Contact = Shot->Request.ContactTime;
		const double End = Playback.GetFinishTime() + 0.1;
		for (double T = 0.0; T <= End && Director.GetPendingShot() == Shot; T += 1.0 / 60.0)
		{
			*Clock = Contact + T;
			Playback.Advance();
		}
	}

	struct FDirectorScene
	{
		RbBallTest::FTableWithBalls Scene;
		URbMatchDirector* Director = nullptr;
		TSharedRef<double> Clock = MakeShared<double>(0.0);
	};

	// A director on the scene's table (no simulation service: RunShotBlocking), registered as the table's session, live playback
	// rate 1 with a manual clock.
	FDirectorScene MakeDirectorScene(UWorld* World, int64 Seed)
	{
		FDirectorScene Out;
		Out.Scene = RbBallTest::SpawnTableWithBalls(World);
		if (!Out.Scene.Table || !Out.Scene.Balls)
		{
			return Out;
		}
		Out.Director = NewObject<URbMatchDirector>(World);
		Out.Director->Initialize(Out.Scene.Table, Out.Scene.Balls, nullptr, nullptr);
		Out.Director->SetRecordsReplays(false);
		FRbMatchSetup Setup;
		Setup.Mode = ERbMatchMode::Practice;
		Setup.Seed = Seed;
		Out.Director->StartMatch(Setup);
		Out.Director->SetLivePlaybackRate(1.0f);
		*Out.Clock = FPlatformTime::Seconds();
		const TSharedRef<double> Clock = Out.Clock;
		Out.Scene.Balls->GetPlayback()->SetClockSource([Clock]() { return *Clock; });
		if (URbTableSubsystem* Tables = URbTableSubsystem::Get(World))
		{
			FRbTableSession Session;
			Session.TableIndex = Out.Scene.Table->TableIndex;
			Session.Table = Out.Scene.Table;
			Session.BallSet = Out.Scene.Balls;
			Session.Director = Out.Director;
			Tables->RegisterSession(Session);
		}
		return Out;
	}

	// Mid-rack layout: the listed balls at rest (core plan positions), everything else out of play.
	void SetLayout(URbMatchDirector& Director, std::initializer_list<TPair<int32, rb::Vec2>> Balls)
	{
		FRbTableState State = Director.GetTableState();
		for (rb::SimBall& Ball : State.Balls)
		{
			Ball.InPlay = false;
		}
		for (const TPair<int32, rb::Vec2>& Ball : Balls)
		{
			rb::SimBall& S = State.Balls[Ball.Key];
			S.InPlay = true;
			S.State = rb::BallState{};
			S.State.Position = rb::Vec3(Ball.Value.x, Ball.Value.y, S.Spec.Radius);
		}
		Director.SetTableStateForTest(State);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallHandOffExact, "RawBreak.Unit.LooseBall.HandOff_ExactCoreState", RB_UNIT_TEST_FLAGS)
bool FRbLooseBallHandOffExact::RunTest(const FString& Parameters)
{
	using namespace RbLooseBallTest;
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World); // translated, yaw 30 deg
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(TestWorld.World);
	if (!TestNotNull(TEXT("loose-ball subsystem in a game world"), Loose) || !TestTrue(TEXT("table + balls"), Scene.Table && Scene.Balls && Scene.Table->HasContext()))
	{
		return false;
	}
	TestTrue(TEXT("the ball set bound itself (InitForTable)"), Loose->IsBound(Scene.Balls));
	FString Error;
	const TSharedPtr<const FRbShot> Shot = SimulateJumpOff(Scene.Table->GetContextPtr(), Error);
	if (!TestTrue(FString::Printf(TEXT("jump shot simulated %s"), *Error), Shot.IsValid()))
	{
		return false;
	}
	const int32 EventIndex = FindOffTableEvent(Shot->Result, 0);
	if (!TestTrue(TEXT("the cue ball leaves the table"), EventIndex != INDEX_NONE))
	{
		return false;
	}
	const rb::ShotEvent& Event = Shot->Result.Events[EventIndex];
	TestEqual(TEXT("reason Floor"), static_cast<int32>(Event.Feature), static_cast<int32>(rb::OffTableReason::Floor));
	rb::BallState State;
	rb::Quat Orientation;
	TestTrue(TEXT("hand-off state"), URbLooseBallSubsystem::GetHandOffState(*Shot, EventIndex, State, Orientation));
	TestTrue(TEXT("hand-off state = the event's Pre[0] (bitwise)"), RbBallTest::SameBits(State, Event.Pre[0]));
	TestTrue(TEXT("hand-off orientation = the final orientation (bitwise)"), RbBallTest::SameBits(Orientation, Shot->Result.Finals[0].Orientation));
	TestTrue(TEXT("the ball moves outward at the hand-off"), State.Velocity.y > 0.1);
	AddInfo(FString::Printf(TEXT("hand-off at t=%.4f s: p=(%.4f, %.4f, %.4f) m v=(%.3f, %.3f, %.3f) m/s w=(%.2f, %.2f, %.2f) rad/s"), Event.Time,
		State.Position.x, State.Position.y, State.Position.z, State.Velocity.x, State.Velocity.y, State.Velocity.z, State.Omega.x, State.Omega.y, State.Omega.z));

	Scene.Balls->ShowSimBalls(Shot->Request.Input.Balls, rb::kMaxBalls);
	ARbLooseBall* Ball = Loose->HandOff(*Scene.Balls, 0, State, Orientation, rb::OffTableReason::Floor, Shot->Id);
	if (!TestNotNull(TEXT("loose ball spawned"), Ball))
	{
		return false;
	}

	// Expected world state, built independently of the table helpers: mirror y, x100, then the table's yaw and translation.
	const FTransform TableToWorld = Scene.Table->GetTableToWorld();
	const FVector ExpectedLocation = TableToWorld.TransformPosition(FVector(100.0 * State.Position.x, -100.0 * State.Position.y, 100.0 * State.Position.z));
	const FVector ExpectedVelocity = TableToWorld.GetRotation().RotateVector(FVector(100.0 * State.Velocity.x, -100.0 * State.Velocity.y, 100.0 * State.Velocity.z));
	const FVector ExpectedSpin = TableToWorld.GetRotation().RotateVector(FVector(-State.Omega.x, State.Omega.y, -State.Omega.z));
	const FQuat ExpectedRotation = TableToWorld.GetRotation() * FRbCoords::OrientationToUE(Orientation);

	const FVector Location = Ball->GetActorLocation();
	const FVector Velocity = Ball->GetLinearVelocity();
	const FVector Spin = Ball->GetAngularVelocity();
	AddInfo(FString::Printf(TEXT("position error %.3e cm, velocity error %.3e (relative), spin error %.3e (relative)"), (Location - ExpectedLocation).Size(),
		(Velocity - ExpectedVelocity).Size() / ExpectedVelocity.Size(), (Spin - ExpectedSpin).Size() / FMath::Max(ExpectedSpin.Size(), 1e-9)));
	TestTrue(TEXT("position at the event (0.1 mm)"), (Location - ExpectedLocation).Size() <= 0.01);
	TestTrue(TEXT("velocity at the event (1e-6 relative)"), (Velocity - ExpectedVelocity).Size() <= 1e-6 * ExpectedVelocity.Size());
	TestTrue(TEXT("spin at the event (1e-6 relative)"), (Spin - ExpectedSpin).Size() <= 1e-6 * FMath::Max(ExpectedSpin.Size(), 1e-3));
	TestTrue(TEXT("orientation at the event (1e-6 rad)"), Ball->GetActorQuat().AngularDistance(ExpectedRotation) <= 1e-6);

	// The actor: exact sphere, profile, CCD, simulating, the ball's mass and material, tag, keyed by (TableIndex, BallId).
	const USphereComponent* Body = Ball->GetBodyComponent();
	TestNearlyEqual(TEXT("sphere radius = the ball's radius [cm]"), static_cast<double>(Body->GetUnscaledSphereRadius()), Scene.Balls->GetBallRadiusCm(0), 1e-4);
	TestEqual(TEXT("collision profile RbLooseBall"), Body->GetCollisionProfileName(), RbAssetPaths::Collision::LooseBallProfile);
	TestTrue(TEXT("object type RbLooseBall"), Body->GetCollisionObjectType() == RbAssetPaths::Collision::LooseBallChannel);
	TestTrue(TEXT("ignores the pawn"), Body->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
	TestTrue(TEXT("ignores the cue sweep"), Body->GetCollisionResponseToChannel(RbAssetPaths::Collision::CueSweepChannel) == ECR_Ignore);
	TestTrue(TEXT("simulates physics"), Body->IsSimulatingPhysics());
	TestTrue(TEXT("CCD"), Body->BodyInstance.bUseCCD);
	TestNearlyEqual(TEXT("mass [kg]"), static_cast<double>(Body->GetMass()), Scene.Table->GetContext().Balls.Balls[0].Mass, 1e-4);
	TestTrue(TEXT("tagged RbLooseBall"), Ball->ActorHasTag(RbAssetPaths::Tag::LooseBall));
	TestEqual(TEXT("ball id"), Ball->GetBallId(), 0);
	TestEqual(TEXT("table index"), Ball->GetTableIndex(), Scene.Table->TableIndex);
	TestEqual(TEXT("hand-off shot id"), Ball->GetHandOffShotId(), Shot->Id);
	TestTrue(TEXT("the table's mesh"), Ball->GetBallComponent()->GetStaticMesh() == Scene.Balls->GetBallMesh());
	if (UMaterialInstanceDynamic* Mid = Ball->GetBallMaterial())
	{
		float Number = -1.0f;
		Mid->GetScalarParameterValue(FHashedMaterialParameterInfo(RbAssetPaths::Param::BallNumber), Number);
		TestEqual(TEXT("its own material instance with the ball's number"), Number, 0.0f);
		TestTrue(TEXT("not the table ball's instance"), Mid != Scene.Balls->GetBallMaterial(0));
	}
	TestNearlyEqual(TEXT("rendered radius [cm]"), Ball->GetBallComponent()->GetComponentScale().X * Scene.Balls->GetBallMesh()->GetBounds().BoxExtent.GetMax(),
		Scene.Balls->GetBallRadiusCm(0), 1e-6);

	// The table instance waits for the return.
	TestFalse(TEXT("table instance hidden"), Scene.Balls->IsBallVisible(0));
	TestTrue(TEXT("withheld"), Scene.Balls->IsBallWithheld(0));
	TestTrue(TEXT("awaiting return"), Loose->IsAwaitingReturn(Scene.Table->TableIndex, 0));
	TestTrue(TEXT("found by key"), Loose->FindLooseBall(Scene.Table->TableIndex, 0) == Ball);
	TestEqual(TEXT("one loose ball of this table"), Loose->GetLooseBalls(Scene.Table->TableIndex).Num(), 1);
	TestEqual(TEXT("none of another table"), Loose->GetLooseBalls(Scene.Table->TableIndex + 1).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallRestsOnRail, "RawBreak.Unit.LooseBall.RestsOnRailSpawnsNothing", RB_UNIT_TEST_FLAGS)
bool FRbLooseBallRestsOnRail::RunTest(const FString& Parameters)
{
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(TestWorld.World);
	if (!TestNotNull(TEXT("subsystem"), Loose) || !TestNotNull(TEXT("balls"), Scene.Balls))
	{
		return false;
	}
	rb::BallState OnCap;
	OnCap.Position = rb::Vec3(0.3, 0.70, 0.05); // resting on the rail cap
	TestNull(TEXT("RestsOnRailOrFrame spawns nothing"), Loose->HandOff(*Scene.Balls, 4, OnCap, rb::Quat::Identity(), rb::OffTableReason::RestsOnRailOrFrame, 0));
	TestFalse(TEXT("not withheld"), Scene.Balls->IsBallWithheld(4));
	TestEqual(TEXT("no loose ball"), Loose->GetNumLooseBalls(), 0);
	TestNotNull(TEXT("ExternalObjectRebound spawns"), Loose->HandOff(*Scene.Balls, 5, OnCap, rb::Quat::Identity(), rb::OffTableReason::ExternalObjectRebound, 0));

	// The playback's hide rule (URbShotPlaybackComponent::HideTimeOf).
	rb::BallFinal Final;
	Final.Time = 1.25;
	Final.Status = rb::BallFinalStatus::Pocketed;
	TestEqual(TEXT("pocketed: after the drop delay"), URbShotPlaybackComponent::HideTimeOf(Final, 0.15), 1.40);
	Final.Status = rb::BallFinalStatus::OffTable;
	Final.OffReason = rb::OffTableReason::Floor;
	TestEqual(TEXT("off the table (Floor): at the hand-off"), URbShotPlaybackComponent::HideTimeOf(Final, 0.15), 1.25);
	Final.OffReason = rb::OffTableReason::ExternalObjectRebound;
	TestEqual(TEXT("off the table (lamp): at the hand-off"), URbShotPlaybackComponent::HideTimeOf(Final, 0.15), 1.25);
	Final.OffReason = rb::OffTableReason::RestsOnRailOrFrame;
	TestTrue(TEXT("resting on the rail: never during the playback"), URbShotPlaybackComponent::HideTimeOf(Final, 0.15) > 1.0e30);
	Final.Status = rb::BallFinalStatus::OnTable;
	TestTrue(TEXT("on the table: never"), URbShotPlaybackComponent::HideTimeOf(Final, 0.15) > 1.0e30);

	// Rolling resistance per surface (ESTIMATE table).
	TestEqual(TEXT("VCT"), ARbLooseBall::RollingResistanceFor(RbAssetPaths::Surface::Vct), 0.020);
	TestEqual(TEXT("rubber mat"), ARbLooseBall::RollingResistanceFor(RbAssetPaths::Surface::Rubber), 0.080);
	TestEqual(TEXT("cloth"), ARbLooseBall::RollingResistanceFor(RbAssetPaths::Surface::Cloth), 0.010);
	TestEqual(TEXT("unknown"), ARbLooseBall::RollingResistanceFor(SurfaceType_Default), 0.020);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallAwaitingReturn, "RawBreak.Unit.LooseBall.AwaitingReturn", RB_UNIT_TEST_FLAGS)
bool FRbLooseBallAwaitingReturn::RunTest(const FString& Parameters)
{
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(TestWorld.World);
	if (!TestNotNull(TEXT("subsystem"), Loose) || !TestNotNull(TEXT("balls"), Scene.Balls))
	{
		return false;
	}
	const int32 TableIndex = Scene.Table->TableIndex;
	int32 Returned = 0;
	int32 LastReturned = -1;
	Loose->OnReturned.AddLambda([&Returned, &LastReturned](int32, int32 BallId) { ++Returned; LastReturned = BallId; });

	// The table state has the 5 back in play (respotted) while its loose copy lies on the floor.
	rb::SimBall Balls[rb::kMaxBalls];
	Balls[5].InPlay = true;
	Balls[5].State.Position = rb::Vec3(0.635, 0.0, Scene.Table->GetContext().BallRadius(5));
	Balls[6].InPlay = false;
	rb::BallState Floor;
	Floor.Position = rb::Vec3(0.5, 1.2, -0.70);
	ARbLooseBall* Five = Loose->HandOff(*Scene.Balls, 5, Floor);
	ARbLooseBall* Six = Loose->HandOff(*Scene.Balls, 6, Floor);
	if (!TestNotNull(TEXT("5 handed off"), Five) || !TestNotNull(TEXT("6 handed off"), Six))
	{
		return false;
	}
	Scene.Balls->ShowSimBalls(Balls, rb::kMaxBalls);
	TestFalse(TEXT("the respotted 5 stays hidden while it awaits its return"), Scene.Balls->IsBallVisible(5));
	TestTrue(TEXT("... although the table state shows it"), Scene.Balls->IsBallRequestedVisible(5));
	Scene.Balls->SetBallVisible(5, true);
	TestFalse(TEXT("SetBallVisible cannot show it either"), Scene.Balls->IsBallVisible(5));

	// Replays suspend withholding and hide the loose actors.
	Loose->SetReplayActive(true);
	TestTrue(TEXT("replay: the recorded shot shows the ball"), Scene.Balls->IsBallVisible(5));
	TestTrue(TEXT("replay: loose actor hidden"), Five->IsHidden());
	TestNull(TEXT("replay: no pick-up offered"), Loose->FindGazedBall(FRbInteractionQuery{}));
	Loose->SetReplayActive(false);
	TestFalse(TEXT("after the replay: withheld again"), Scene.Balls->IsBallVisible(5));
	TestFalse(TEXT("after the replay: loose actor visible"), Five->IsHidden());

	// Return: the 5 is in play -> visible at its table position; the 6 is not -> stays hidden.
	TestTrue(TEXT("return the 5"), Loose->ReturnBall(TableIndex, 5));
	TestEqual(TEXT("OnReturned once"), Returned, 1);
	TestEqual(TEXT("... for the 5"), LastReturned, 5);
	TestTrue(TEXT("the returned 5 shows on its spot"), Scene.Balls->IsBallVisible(5));
	TestTrue(TEXT("at the table state's position"), Scene.Balls->GetBallComponent(5)->GetRelativeLocation().Equals(FRbCoords::PositionToUE(Balls[5].State.Position), 1e-9));
	TestTrue(TEXT("the loose actor is gone"), !IsValid(Five) || Five->IsActorBeingDestroyed());
	TestFalse(TEXT("no longer awaiting"), Loose->IsAwaitingReturn(TableIndex, 5));
	TestTrue(TEXT("return the 6 (reason)"), Loose->ReturnBall(TableIndex, 6, ERbLooseBallReturn::PickUp) && Loose->GetLastReturnReason() == ERbLooseBallReturn::PickUp);
	TestFalse(TEXT("the returned 6 stays hidden (out of play)"), Scene.Balls->IsBallVisible(6));
	TestFalse(TEXT("nothing left to return"), Loose->ReturnBall(TableIndex, 6));
	TestEqual(TEXT("two returns"), Returned, 2);

	// The same ball leaving the table again replaces its earlier loose copy.
	ARbLooseBall* First = Loose->HandOff(*Scene.Balls, 7, Floor);
	ARbLooseBall* Second = Loose->HandOff(*Scene.Balls, 7, Floor);
	TestTrue(TEXT("second hand-off of the 7"), First && Second && First != Second);
	TestEqual(TEXT("replaced"), Loose->GetLastReturnReason(), ERbLooseBallReturn::Replaced);
	TestEqual(TEXT("one loose 7"), Loose->GetLooseBalls(TableIndex).Num(), 1);
	TestTrue(TEXT("still withheld"), Scene.Balls->IsBallWithheld(7));
	TestEqual(TEXT("ReturnAll"), Loose->ReturnAll(TableIndex), 1);
	TestFalse(TEXT("released"), Scene.Balls->IsBallWithheld(7));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallLiveVsReplay, "RawBreak.Unit.LooseBall.LiveHandsOff_ReplaySpawnsNothing", RB_UNIT_TEST_FLAGS)
bool FRbLooseBallLiveVsReplay::RunTest(const FString& Parameters)
{
	using namespace RbLooseBallTest;
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(TestWorld.World);
	URbShotPlaybackComponent* Playback = Scene.Balls ? Scene.Balls->GetPlayback() : nullptr;
	FString Error;
	const TSharedPtr<const FRbShot> Shot = Scene.Table ? SimulateJumpOff(Scene.Table->GetContextPtr(), Error) : nullptr;
	if (!TestNotNull(TEXT("subsystem"), Loose) || !TestNotNull(TEXT("playback"), Playback) || !TestTrue(TEXT("shot"), Shot.IsValid()))
	{
		return false;
	}
	const int32 EventIndex = FindOffTableEvent(Shot->Result, 0);
	if (!TestTrue(TEXT("off-table event"), EventIndex != INDEX_NONE))
	{
		return false;
	}
	const double EventTime = Shot->Result.Events[EventIndex].Time;
	const TSharedRef<double> Clock = MakeShared<double>(0.0);
	Playback->SetClockSource([Clock]() { return *Clock; });
	const double Contact = Shot->Request.ContactTime;

	// Plays the shot (live or replay) in 60 fps frames; returns the number of hand-offs and checks the visibility of the cue ball.
	const auto Run = [&](bool bLive) -> int32
	{
		int32 HandOffs = 0;
		const FDelegateHandle Handle = Loose->OnHandOff.AddLambda([&HandOffs](ARbLooseBall&) { ++HandOffs; });
		Scene.Balls->ShowSimBalls(Shot->Request.Input.Balls, rb::kMaxBalls);
		const double Origin = bLive ? Contact : 1000.0;
		*Clock = Origin;
		Playback->Play(Shot.ToSharedRef(), bLive, 0.0, 1.0f);
		bool bVisibleBefore = true;
		bool bHiddenAfter = true;
		for (int32 Frame = 1; Frame <= 120 && Playback->IsPlaying(); ++Frame)
		{
			*Clock = Origin + Frame / 60.0;
			Playback->Advance();
			const double T = Playback->IsPlaying() ? Playback->GetShotTime() : Playback->GetFinishTime();
			if (T < EventTime)
			{
				bVisibleBefore &= Scene.Balls->IsBallVisible(0);
			}
			else
			{
				bHiddenAfter &= !Scene.Balls->IsBallVisible(0);
			}
		}
		Playback->Stop(false);
		Loose->OnHandOff.Remove(Handle);
		TestTrue(FString::Printf(TEXT("%s: cue ball shown before the hand-off time"), bLive ? TEXT("live") : TEXT("replay")), bVisibleBefore);
		TestTrue(FString::Printf(TEXT("%s: cue ball hidden from the hand-off time on"), bLive ? TEXT("live") : TEXT("replay")), bHiddenAfter);
		return HandOffs;
	};

	TestEqual(TEXT("live playback: exactly one hand-off"), Run(true), 1);
	ARbLooseBall* Ball = Loose->FindLooseBall(Scene.Table->TableIndex, 0);
	if (TestNotNull(TEXT("the loose cue ball"), Ball))
	{
		// No physics step ran: the ball is still exactly where the event put it.
		const FVector Expected = Scene.Table->CoreToWorld(Shot->Result.Events[EventIndex].Pre[0].Position);
		TestTrue(TEXT("live hand-off at the event position (0.1 mm)"), (Ball->GetActorLocation() - Expected).Size() <= 0.01);
		TestEqual(TEXT("from this shot"), Ball->GetHandOffShotId(), Shot->Id);
	}
	Loose->ReturnAll(INDEX_NONE);
	TestEqual(TEXT("replay: no hand-off (no second loose actor)"), Run(false), 0);
	TestEqual(TEXT("replay: no loose ball"), Loose->GetNumLooseBalls(), 0);

	Loose->SetEnabled(false);
	TestEqual(TEXT("switched off: no hand-off"), Run(true), 0);
	TestEqual(TEXT("switched off: no loose ball"), Loose->GetNumLooseBalls(), 0);
	Loose->SetEnabled(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallMatchUnchanged, "RawBreak.Unit.LooseBall.MatchUnchanged_ObjectBallAwaitsReturn", RB_UNIT_TEST_FLAGS)
bool FRbLooseBallMatchUnchanged::RunTest(const FString& Parameters)
{
	using namespace RbLooseBallTest;
	// The 9 near the +y side rail, the cue ball 10 cm behind it; 9 m/s at 10 deg elevation drives the 9 over the rail (Floor) and
	// the cue ball stays (rbsim --ball 0:-0.3,0.4 --ball 9:-0.3,0.5 --speed 9 --aim 90 --elevation 10).
	const auto PlayRun = [this](bool bLooseBalls, uint64& OutHash, FString& OutState, bool& bOutAwaited) -> bool
	{
		RbBallTest::FTestWorld TestWorld;
		FDirectorScene D = MakeDirectorScene(TestWorld.World, 4242);
		URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(TestWorld.World);
		if (!D.Director || !Loose || D.Director->GetPhase() == ERbDirectorPhase::Idle)
		{
			AddError(TEXT("director scene"));
			return false;
		}
		Loose->SetEnabled(bLooseBalls);
		SetLayout(*D.Director, {{0, rb::Vec2(-0.3, 0.4)}, {9, rb::Vec2(-0.3, 0.5)}});
		if (!D.Director->SubmitScriptedStrike(9.0, 0.5 * UE_DOUBLE_PI, FMath::DegreesToRadians(10.0), 0.0, 0.0))
		{
			AddError(FString::Printf(TEXT("strike refused: %s"), *D.Director->GetLastError()));
			return false;
		}
		const TSharedPtr<const FRbShot> Shot = D.Director->GetPendingShot();
		TestTrue(TEXT("the 9 leaves the table (Floor)"), Shot.IsValid() && FindOffTableEvent(Shot->Result, 9) != INDEX_NONE &&
			Shot->Result.Finals[9].OffReason == rb::OffTableReason::Floor);
		PlayPendingToEnd(*D.Director, *D.Scene.Balls->GetPlayback(), D.Clock);
		const TSharedPtr<const FRbShot> Committed = D.Director->GetLastCommittedShot();
		if (!TestTrue(TEXT("shot committed"), Committed.IsValid() && D.Director->GetMatchShotIndex() == 1))
		{
			return false;
		}
		OutHash = Committed->ResultHash;
		OutState = GameStateDigest(D.Director->GetMatchState());
		const FRbTableState& Table = D.Director->GetTableState();
		bOutAwaited = Loose->IsAwaitingReturn(D.Scene.Table->TableIndex, 9);
		if (!bLooseBalls)
		{
			TestEqual(TEXT("disabled: no loose ball"), Loose->GetNumLooseBalls(), 0);
			TestTrue(TEXT("disabled: the respotted 9 shows at once"), Table.Balls[9].InPlay && D.Scene.Balls->IsBallVisible(9));
			return true;
		}
		// The foul put the 9 back on the table: its instance waits for the loose 9.
		TestTrue(TEXT("the committed state has the 9 back in play (spotted)"), Table.Balls[9].InPlay);
		TestTrue(TEXT("the 9 awaits its return"), bOutAwaited);
		TestFalse(TEXT("its table instance is hidden"), D.Scene.Balls->IsBallVisible(9));
		TestTrue(TEXT("the cue ball is shown (it stayed / is in hand)"), D.Scene.Balls->IsBallVisible(0) == Table.Balls[0].InPlay);
		Loose->UpdateAutomaticReturns(0.1);
		TestTrue(TEXT("nothing returns before the next shot"), Loose->IsAwaitingReturn(D.Scene.Table->TableIndex, 9));
		// Ball in hand after the foul: place it; the loose 9 still waits; the next shot returns it (Address).
		if (D.Director->GetPhase() == ERbDirectorPhase::AwaitPlacement)
		{
			TestTrue(TEXT("placed"), D.Director->PlaceCueBall(rb::Vec2(-0.9, -0.3)));
		}
		Loose->UpdateAutomaticReturns(0.1);
		TestTrue(TEXT("placing the cue ball does not return the 9"), Loose->IsAwaitingReturn(D.Scene.Table->TableIndex, 9));
		TestTrue(TEXT("next shot"), D.Director->SubmitScriptedStrike(0.5, 0.0, 0.0, 0.0, 0.0));
		Loose->UpdateAutomaticReturns(0.1);
		TestFalse(TEXT("the next shot returns the 9"), Loose->IsAwaitingReturn(D.Scene.Table->TableIndex, 9));
		TestEqual(TEXT("... as the address"), Loose->GetLastReturnReason(), ERbLooseBallReturn::Address);
		TestTrue(TEXT("the 9 shows on the table again"), D.Scene.Balls->IsBallVisible(9));
		return true;
	};

	uint64 HashOn = 0;
	uint64 HashOff = 0;
	FString StateOn;
	FString StateOff;
	bool bAwaitedOn = false;
	bool bAwaitedOff = true;
	if (!PlayRun(true, HashOn, StateOn, bAwaitedOn) || !PlayRun(false, HashOff, StateOff, bAwaitedOff))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("ResultHash %016llx / %016llx; state %s"), HashOn, HashOff, *StateOn));
	TestEqual(TEXT("ResultHash equals the run without loose balls"), HashOn, HashOff);
	TestEqual(TEXT("GameState equals the run without loose balls"), StateOn, StateOff);
	TestTrue(TEXT("only the run with loose balls awaited a return"), bAwaitedOn && !bAwaitedOff);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallCueBallInHand, "RawBreak.Unit.LooseBall.CueBallInHand", RB_UNIT_TEST_FLAGS)
bool FRbLooseBallCueBallInHand::RunTest(const FString& Parameters)
{
	using namespace RbLooseBallTest;
	RbBallTest::FTestWorld TestWorld;
	FDirectorScene D = MakeDirectorScene(TestWorld.World, 777);
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(TestWorld.World);
	URbInteractionSubsystem* Interaction = URbInteractionSubsystem::Get(TestWorld.World);
	if (!D.Director || !Loose || !Interaction)
	{
		AddError(TEXT("director scene / subsystems"));
		return false;
	}
	TestTrue(TEXT("the pick-up provider is registered"), Interaction->GetNumProviders() >= 1);
	const int32 TableIndex = D.Scene.Table->TableIndex;
	SetLayout(*D.Director, {{0, rb::Vec2(0.2, 0.3)}, {1, rb::Vec2(1.0, -0.3)}});
	TestTrue(TEXT("jump off"), D.Director->SubmitScriptedStrike(4.0, 0.5 * UE_DOUBLE_PI, FMath::DegreesToRadians(15.0), 0.0, 0.0));
	const TSharedPtr<const FRbShot> Shot = D.Director->GetPendingShot();
	TestTrue(TEXT("the cue ball leaves the table"), Shot.IsValid() && FindOffTableEvent(Shot->Result, 0) != INDEX_NONE);
	PlayPendingToEnd(*D.Director, *D.Scene.Balls->GetPlayback(), D.Clock);
	TestEqual(TEXT("cue ball off the table: foul, ball in hand"), D.Director->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	ARbLooseBall* Ball = Loose->FindLooseBall(TableIndex, 0);
	if (!TestNotNull(TEXT("loose cue ball"), Ball))
	{
		return false;
	}
	Loose->UpdateAutomaticReturns(0.1);
	TestTrue(TEXT("in hand, not placed: it stays on the floor"), Loose->IsAwaitingReturn(TableIndex, 0));

	// Gaze: a standing eye 60 cm (plan) beside the ball, 150 cm above it.
	const FVector Center = Ball->GetActorLocation();
	const FVector Outward = D.Scene.Table->CoreDirectionToWorld(rb::Vec3(0.0, 1.0, 0.0));
	FRbInteractionQuery Query;
	Query.Eye = Center + Outward * 60.0 + FVector(0.0, 0.0, 150.0);
	Query.Direction = (Center - Query.Eye).GetSafeNormal();
	FText Verb;
	TestFalse(TEXT("a ball still flying is not offered"), Interaction->FindInteraction(Query, Verb));
	Ball->GetBodyComponent()->PutRigidBodyToSleep();
	TestTrue(TEXT("a resting ball in reach and gazed at is offered"), Interaction->FindInteraction(Query, Verb));
	TestEqual(TEXT("verb"), Verb.ToString(), FString(TEXT("Pick up the ball")));
	FRbInteractionQuery Away = Query;
	Away.Direction = FQuat(FVector::UpVector, FMath::DegreesToRadians(40.0)).RotateVector(Query.Direction);
	TestNull(TEXT("not gazed at"), Loose->FindGazedBall(Away));
	FRbInteractionQuery Far = Query;
	Far.Eye = Center + Outward * 200.0 + FVector(0.0, 0.0, 150.0);
	Far.Direction = (Center - Far.Eye).GetSafeNormal();
	TestNull(TEXT("out of reach (2 m)"), Loose->FindGazedBall(Far));

	// Pick-up by a pawn with the carrying hand: the ball goes into the hand (M2-F's component carries and places it).
	ADefaultPawn* Pawn = TestWorld.World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), FTransform(Query.Eye));
	URbBallInHandComponent* Hand = Pawn ? NewObject<URbBallInHandComponent>(Pawn) : nullptr;
	if (Hand)
	{
		Hand->SetupAttachment(Pawn->GetRootComponent());
		Hand->RegisterComponent();
	}
	Query.Pawn = Pawn;
	TestTrue(TEXT("pick-up consumed the press"), Interaction->TryInteract(Query));
	TestFalse(TEXT("the loose cue ball is gone"), Loose->IsAwaitingReturn(TableIndex, 0));
	TestEqual(TEXT("reason PickUp"), Loose->GetLastReturnReason(), ERbLooseBallReturn::PickUp);
	TestTrue(TEXT("the hand carries the cue ball"), Hand && Hand->GetState() == ERbBallInHandState::Carrying);
	TestFalse(TEXT("in hand: not on the table"), D.Scene.Balls->IsBallVisible(0));
	if (Hand)
	{
		Hand->Cancel();
	}

	// A loose cue ball returns when the ball in hand is placed (the player put the ball down).
	ARbLooseBall* Again = DropResting(*Loose, *D.Scene.Balls, 0, Center);
	TestNotNull(TEXT("cue ball handed off again"), Again);
	Loose->UpdateAutomaticReturns(0.1);
	TestTrue(TEXT("still in hand"), Loose->IsAwaitingReturn(TableIndex, 0));
	TestTrue(TEXT("placed"), D.Director->PlaceCueBall(rb::Vec2(-0.2, 0.1)));
	Loose->UpdateAutomaticReturns(0.1);
	TestFalse(TEXT("placing returns the loose cue ball"), Loose->IsAwaitingReturn(TableIndex, 0));
	TestEqual(TEXT("reason CueBallPlaced"), Loose->GetLastReturnReason(), ERbLooseBallReturn::CueBallPlaced);
	TestTrue(TEXT("the placed cue ball shows"), D.Scene.Balls->IsBallVisible(0));

	// A new rack / match returns every loose ball of the table.
	DropResting(*Loose, *D.Scene.Balls, 3, Center);
	DropResting(*Loose, *D.Scene.Balls, 4, Center + FVector(10.0, 0.0, 0.0));
	Loose->UpdateAutomaticReturns(0.1);
	TestEqual(TEXT("two loose balls"), Loose->GetLooseBalls(TableIndex).Num(), 2);
	FRbMatchSetup Setup = D.Director->GetSetup();
	Setup.Seed = 778;
	D.Director->StartMatch(Setup);
	Loose->UpdateAutomaticReturns(0.1);
	TestEqual(TEXT("new match: every loose ball returned"), Loose->GetLooseBalls(TableIndex).Num(), 0);
	TestEqual(TEXT("reason NewRack"), Loose->GetLastReturnReason(), ERbLooseBallReturn::NewRack);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallAutomaticReturns, "RawBreak.Unit.LooseBall.AutomaticReturns", RB_UNIT_TEST_FLAGS)
bool FRbLooseBallAutomaticReturns::RunTest(const FString& Parameters)
{
	using namespace RbLooseBallTest;
	RbBallTest::FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(World);
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(World);
	if (!TestNotNull(TEXT("subsystem"), Loose) || !TestNotNull(TEXT("balls"), Scene.Balls))
	{
		return false;
	}
	const int32 TableIndex = Scene.Table->TableIndex;
	const double FloorZ = Scene.Table->GetActorLocation().Z;
	const double R = Scene.Balls->GetBallRadiusCm(2);
	// A 12 x 12 m floor with its top at the table's floor level.
	SpawnBlock(World, FVector(0.0, 0.0, FloorZ - 10.0), FVector(1200.0, 1200.0, 20.0));
	Loose->UnreachableReturnSeconds = 2.0;

	// Kill Z: far below the floor.
	DropResting(*Loose, *Scene.Balls, 2, FVector(0.0, 0.0, FloorZ - 450.0));
	Loose->UpdateAutomaticReturns(0.1);
	TestFalse(TEXT("below the floor: returned"), Loose->IsAwaitingReturn(TableIndex, 2));
	TestEqual(TEXT("reason KillZ"), Loose->GetLastReturnReason(), ERbLooseBallReturn::KillZ);

	// A return volume (profile RbBallReturn) on the floor, 3 m from the table.
	const FVector VolumeCenter(400.0, -350.0, FloorZ + 30.0);
	AActor* Volume = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(VolumeCenter));
	UBoxComponent* Box = Volume ? NewObject<UBoxComponent>(Volume) : nullptr;
	if (TestNotNull(TEXT("return volume"), Box))
	{
		Box->SetBoxExtent(FVector(40.0, 40.0, 30.0));
		Box->SetCollisionProfileName(RbAssetPaths::Collision::BallReturnProfile);
		Volume->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(VolumeCenter);
	}
	DropResting(*Loose, *Scene.Balls, 3, FVector(VolumeCenter.X, VolumeCenter.Y, FloorZ + R));
	Loose->UpdateAutomaticReturns(0.1);
	TestFalse(TEXT("resting in the return volume: returned"), Loose->IsAwaitingReturn(TableIndex, 3));
	TestEqual(TEXT("reason ReturnVolume"), Loose->GetLastReturnReason(), ERbLooseBallReturn::ReturnVolume);

	// Open floor: reachable, it stays however long it rests there.
	ARbLooseBall* Open = DropResting(*Loose, *Scene.Balls, 4, FVector(-450.0, 380.0, FloorZ + R));
	if (TestNotNull(TEXT("ball on the open floor"), Open))
	{
		TestTrue(TEXT("reachable on the open floor"), Loose->IsReachable(*Open));
	}
	// Under a low slab (a counter 3 x 3 m, underside 45 cm): no standing spot within reach sees it.
	const FVector Hidden(-450.0, -400.0, FloorZ + R);
	SpawnBlock(World, FVector(Hidden.X, Hidden.Y, FloorZ + 55.0), FVector(300.0, 300.0, 20.0));
	ARbLooseBall* Under = DropResting(*Loose, *Scene.Balls, 5, Hidden);
	if (TestNotNull(TEXT("ball under the counter"), Under))
	{
		TestFalse(TEXT("unreachable under the counter"), Loose->IsReachable(*Under));
	}
	for (int32 Step = 0; Step < 25; ++Step)
	{
		Loose->UpdateAutomaticReturns(0.1); // 2.5 s
	}
	TestTrue(TEXT("the reachable ball stays"), Loose->IsAwaitingReturn(TableIndex, 4));
	TestFalse(TEXT("the unreachable ball came back after UnreachableReturnSeconds"), Loose->IsAwaitingReturn(TableIndex, 5));
	TestEqual(TEXT("reason Unreachable"), Loose->GetLastReturnReason(), ERbLooseBallReturn::Unreachable);

	// An actor that vanished (FellOutOfWorld / unload) releases its table ball.
	if (Open)
	{
		Open->Destroy();
	}
	Loose->UpdateAutomaticReturns(0.1);
	TestFalse(TEXT("destroyed: released"), Scene.Balls->IsBallWithheld(4));
	TestEqual(TEXT("reason Destroyed"), Loose->GetLastReturnReason(), ERbLooseBallReturn::Destroyed);
	TestEqual(TEXT("nothing left"), Loose->GetNumLooseBalls(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
