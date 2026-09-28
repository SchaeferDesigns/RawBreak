// RawBreak.Functional.M1Flow (Docs/ue-architecture.md 12 A4 / A8, 13 UE-8): PIE on the generated M1 map L_M1_TestRoom (not an
// engine map with a game-mode override): the map's own World Settings start ARbGameMode, which uses the level's table (the
// one the room and the lamp were built around), and the M1 loop runs on it:
//   level validator green in the play world (table, room, player start, look-dev cameras, lamp == physics lamp, E4 >= 520 lux)
//   -> the physics sees the rendered lamp (table context LampUndersideZ) -> pawn at the head end, look-dev cameras passive,
//   RAW BREAK user settings class -> practice 9-ball: ball in hand behind the head string through the stroke component's
//   placement event -> break through the stroke contact event (human layer -> simulation worker -> rules) -> shot committed and
//   recorded for replays -> 9 pocketed wins the rack -> Confirm racks again.
// Needs the generated map (Tools/unreal/editor/rb_make_test_room.py); live playback rate 0 (review R-15). Owner: UE-8.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

#include "Core/RbAssetPaths.h"
#include "Dev/RbLookDevCamera.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTestRoom.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "Settings/RbGameUserSettings.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbM1Flow
{
	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	ARbGameMode* GameMode()
	{
		UWorld* World = PlayWorld();
		return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
	}

	bool WaitForHandOff(const URbMatchDirector& Director, URbSimulationSubsystem* Simulation)
	{
		for (int32 Try = 0; Try < 200 && Director.GetPhase() == ERbDirectorPhase::Simulating; ++Try)
		{
			if (Simulation)
			{
				Simulation->TryCollect(0.05);
			}
		}
		return Director.GetPhase() != ERbDirectorPhase::Simulating;
	}
}

class FRbWaitForM1MatchCommand : public IAutomationLatentCommand
{
public:
	FRbWaitForM1MatchCommand(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* Mode = RbM1Flow::GameMode();
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (Director && Director->GetPhase() != ERbDirectorPhase::Idle)
		{
			return true;
		}
		if (GetCurrentRunTime() > Timeout)
		{
			Test->AddError(TEXT("PIE on L_M1_TestRoom did not start an ARbGameMode match (World Settings game mode?)"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Timeout;
};

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRbM1FlowCommand, FAutomationTestBase*, Test);

bool FRbM1FlowCommand::Update()
{
	using namespace RbM1Flow;
	UWorld* World = PlayWorld();
	ARbGameMode* Mode = GameMode();
	URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
	if (!Test->TestNotNull(TEXT("ARbGameMode from the map's World Settings"), Mode) || !Test->TestNotNull(TEXT("director"), Director))
	{
		return true;
	}

	// --- the level in play ---------------------------------------------------------------------------------------------
	bool bValid = false;
	const FString Report = ARbTestRoom::ValidateM1Level(World, bValid);
	Test->AddInfo(Report);
	Test->TestTrue(TEXT("M1 level validator green in the play world"), bValid);

	ARbTable* Table = Mode->GetTable();
	ARbTestRoom* Room = nullptr;
	for (TActorIterator<ARbTestRoom> It(World); It; ++It)
	{
		Room = *It;
	}
	if (!Test->TestTrue(TEXT("table with context"), Table && Table->HasContext()) || !Test->TestNotNull(TEXT("room"), Room))
	{
		return true;
	}
	int32 Tables = 0;
	for (TActorIterator<ARbTable> It(World); It; ++It)
	{
		++Tables;
	}
	Test->TestEqual(TEXT("the game mode uses the level's table (none spawned)"), Tables, 1);
	Test->TestNearlyEqual(TEXT("physics lamp = rendered lamp (R-14)"), Table->GetContext().Setup.LampUndersideZ, Room->GetLampUndersideHeightMeters(), 1e-12);
	const FRbLuxReport Lux = Room->ComputeLux(10.0, ERbLuxSources::Lamp);
	Test->TestTrue(FString::Printf(TEXT("E4 in play: >= 520 lux on bed and rails (min %.0f)"), Lux.TableMin()), Lux.TableMin() >= ARbTestRoom::WpaMinLux);
	Test->TestFalse(TEXT("lamp fixture visible in play"), Room->IsLampFixtureHiddenInGame());

	APlayerController* PC = World->GetFirstPlayerController();
	ARbPlayerCharacter* Character = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
	URbStrokeComponent* Stroke = Character ? Character->GetStroke() : nullptr;
	if (!Test->TestNotNull(TEXT("ARbPlayerCharacter with a stroke component"), Stroke))
	{
		return true;
	}
	const double HeadRail = -100.0 * (0.5 * Table->GetContext().Spec.Length + Table->GetContext().Spec.RailWidthTotal);
	Test->TestTrue(FString::Printf(TEXT("pawn starts at the head end (x %.0f < %.0f)"), Character->GetActorLocation().X, HeadRail),
		Character->GetActorLocation().X < HeadRail);
	Test->TestFalse(TEXT("look-dev cameras stay passive in play"), Cast<ARbLookDevCamera>(PC->GetViewTarget()) != nullptr);
	Test->TestNotNull(TEXT("RAW BREAK game user settings"), Cast<URbGameUserSettings>(GEngine->GetGameUserSettings()));

	// --- the M1 loop on this table -------------------------------------------------------------------------------------
	URbSimulationSubsystem* Simulation = World->GetSubsystem<URbSimulationSubsystem>();
	URbReplaySubsystem* Replay = World->GetSubsystem<URbReplaySubsystem>();
	if (!Test->TestNotNull(TEXT("simulation subsystem"), Simulation) || !Test->TestNotNull(TEXT("replay subsystem"), Replay))
	{
		return true;
	}
	Test->TestTrue(TEXT("practice 9-ball, break ball in hand"), Director->GetSetup().Mode == ERbMatchMode::Practice &&
		Director->GetPhase() == ERbDirectorPhase::AwaitPlacement && Stroke->GetPhase() == ERbStrokePhase::PlacingCueBall);
	Director->SetLivePlaybackRate(0.0f);
	const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
	const double R = Director->GetTableContext()->BallRadius(0);
	Stroke->OnCueBallPlaced.Broadcast(Table->CoreToWorld(rb::Vec3(Rules.HeadStringX - 0.10, 0.12, R)));
	if (!Test->TestTrue(TEXT("ball in hand placed behind the head string"), Director->GetPhase() == ERbDirectorPhase::AwaitStroke))
	{
		return true;
	}

	const int32 ShotsBefore = Replay->GetShotCount();
	const rb::Vec3 CueBall = Director->GetTableState().Balls[0].State.Position;
	const rb::Vec3 Apex = Director->GetTableState().Balls[1].State.Position;
	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = FMath::Atan2(Apex.y - CueBall.y, Apex.x - CueBall.x);
	Commit.Intended.Speed = 8.0;
	Commit.Intended.AxisOffsetB = -0.1;
	Commit.Intended.TimeDown = 4.0;
	Commit.Intended.ForwardStart = 3.7;
	Commit.ContactTime = FPlatformTime::Seconds();
	Commit.EyeTransform = Character->GetActorTransform();
	Stroke->OnStrokeContact.Broadcast(Commit);
	Test->TestTrue(TEXT("break handed off by the simulation worker"), WaitForHandOff(*Director, Simulation));
	const FRbLastShotSummary& Break = Director->GetLastShot();
	Test->AddInfo(FString::Printf(TEXT("break on the M1 table: %d pocketed, fouls %08x, sim %.2f ms, phase %d"), Break.Pocketed.Num(), Break.Fouls.Bits,
		Break.SimMilliseconds, static_cast<int32>(Director->GetPhase())));
	Test->TestTrue(TEXT("break committed through the rules"), Break.bValid && Director->GetMatchShotIndex() == 1);
	Test->TestTrue(TEXT("break recorded for replays"), Replay->GetShotCount() == ShotsBefore + 1);
	Test->TestTrue(TEXT("a replay is allowed between shots"), Director->IsReplayAllowed() || Director->GetPhase() == ERbDirectorPhase::RackOver);

	// Leave a decision / rack over, then pocket the 9 in the foot-right corner: the rack is won, Confirm racks again.
	for (int32 Try = 0; Try < 4 && Director->GetPhase() != ERbDirectorPhase::AwaitStroke && Director->GetPhase() != ERbDirectorPhase::AwaitPlacement; ++Try)
	{
		Director->Confirm();
	}
	const rb::PocketGeometry& Corner = Director->GetTableContext()->Geometry.Pockets[static_cast<int32>(rb::PocketId::FootRight)];
	const rb::Vec2 Nine = Corner.MouthMid - Corner.Axis * 0.3;
	FRbTableState State = Director->GetTableState();
	for (rb::SimBall& Ball : State.Balls)
	{
		Ball.InPlay = false;
	}
	const rb::Vec2 CuePos = Nine - Corner.Axis * 0.25;
	State.Balls[0].InPlay = true;
	State.Balls[0].State.Position = rb::Vec3(CuePos.x, CuePos.y, R);
	State.Balls[9].InPlay = true;
	State.Balls[9].State.Position = rb::Vec3(Nine.x, Nine.y, Director->GetTableContext()->BallRadius(9));
	Director->SetTableStateForTest(State);
	const int32 RackBefore = Director->GetMatchState().RackNumber;
	Test->TestTrue(TEXT("9 shot"), Director->SubmitScriptedStrike(2.0, FMath::Atan2(Corner.Axis.y, Corner.Axis.x), 0.0, 0.0, -0.35) &&
		WaitForHandOff(*Director, Simulation));
	Test->TestTrue(TEXT("9 pocketed wins the rack"), Director->GetPhase() == ERbDirectorPhase::RackOver);
	Test->TestTrue(TEXT("Confirm racks again"), Director->Confirm() && Director->GetMatchState().RackNumber == RackBefore + 1 &&
		Director->GetPhase() == ERbDirectorPhase::AwaitPlacement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbM1FlowTest, "RawBreak.Functional.M1Flow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbM1FlowTest::RunTest(const FString& Parameters)
{
	if (!FPackageName::DoesPackageExist(RbAssetPaths::M1TestRoomMap))
	{
		AddError(FString::Printf(TEXT("%s missing: run Tools/unreal/editor/rb_make_test_room.py"), RbAssetPaths::M1TestRoomMap));
		return false;
	}
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbWaitForM1MatchCommand(this, 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbM1FlowCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
