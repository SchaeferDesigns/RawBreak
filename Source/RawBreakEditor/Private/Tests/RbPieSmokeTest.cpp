// Functional smoke tests of the headless PIE path (Docs/ue-architecture.md 9.5, 10; M2 18.12):
//   RawBreak.Functional.PieSmoke            PIE on an engine map works under -NullRHI and the RAW BREAK world subsystems exist.
//   RawBreak.Functional.LevelSmoke.<Map>    every level the game can open (the title and every venue of RbTypes::MapFor) that
//                                           exists starts in PIE from its own World Settings: a venue runs ARbGameMode with a
//                                           possessed ARbPlayerCharacter, a started match on the player's table (URbTableSubsystem,
//                                           valid TableIndex set, player session consistent once M2-E registers sessions); the
//                                           title runs ARbTitleGameMode. One test per existing map, so a level that a package's
//                                           generator adds is smoke-tested at its merge without editing this file.
// Functional tests that need PIE live in this editor module (UnrealEd); pure unit tests live in the RawBreak module.
// Owner: UE-0 / M2-0 (architect).

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Core/RbAssetPaths.h"
#include "Core/RbTypes.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Player/RbPlayerCharacter.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"
#include "UI/Front/RbTitleGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRbCheckPieWorldCommand, FAutomationTestBase*, Test);

bool FRbCheckPieWorldCommand::Update()
{
	UWorld* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
	Test->TestNotNull(TEXT("PIE world is running"), World);
	if (World)
	{
		Test->TestTrue(TEXT("PIE world has begun play"), World->HasBegunPlay());
		Test->TestNotNull(TEXT("URbSimulationSubsystem exists in PIE"), World->GetSubsystem<URbSimulationSubsystem>());
		Test->TestNotNull(TEXT("URbReplaySubsystem exists in PIE"), World->GetSubsystem<URbReplaySubsystem>());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPieSmokeTest, "RawBreak.Functional.PieSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbPieSmokeTest::RunTest(const FString& Parameters)
{
	AutomationOpenMap(TEXT("/Engine/Maps/Entry"));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FRbCheckPieWorldCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// --- RawBreak.Functional.LevelSmoke -------------------------------------------------------------------------------------

namespace RbLevelSmoke
{
	// Every level the game opens by itself: the title (M2-D) and the venues of the title's venue select (RbTypes::MapFor).
	TArray<FString> PlayableMaps()
	{
		TArray<FString> Maps = {RbAssetPaths::TitleMap};
		const UEnum* Venues = StaticEnum<ERbVenue>();
		for (int32 Index = 0; Index < Venues->NumEnums() - 1; ++Index) // - the generated _MAX
		{
			Maps.AddUnique(RbTypes::MapFor(static_cast<ERbVenue>(Venues->GetValueByIndex(Index))));
		}
		return Maps;
	}

	bool IsVenueMap(const FString& Map)
	{
		return Map != RbAssetPaths::TitleMap;
	}
}

// Waits until the play world runs its game (venues: the match of the player's table has started; the title: the title game
// mode), then plays SettleSeconds more and checks the level's start-up contract.
class FRbLevelSmokeCommand : public IAutomationLatentCommand
{
public:
	FRbLevelSmokeCommand(FAutomationTestBase* InTest, const FString& InMap, double InTimeout, double InSettleSeconds)
		: Test(InTest), Map(InMap), Timeout(InTimeout), SettleSeconds(InSettleSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
		const bool bReady = World && World->HasBegunPlay() && IsStarted(World);
		if (!bReady)
		{
			if (GetCurrentRunTime() > Timeout)
			{
				Test->AddError(FString::Printf(TEXT("%s did not start its game in PIE within %.0f s (World Settings game mode, player start?)"), *Map, Timeout));
				return true;
			}
			return false;
		}
		if (ReadyTime < 0.0)
		{
			ReadyTime = GetCurrentRunTime();
			Test->AddInfo(FString::Printf(TEXT("%s started after %.1f s"), *Map, ReadyTime));
		}
		if (GetCurrentRunTime() - ReadyTime < SettleSeconds)
		{
			return false;
		}
		Check(World);
		return true;
	}

private:
	bool IsStarted(UWorld* World) const
	{
		if (!RbLevelSmoke::IsVenueMap(Map))
		{
			return World->GetAuthGameMode() != nullptr;
		}
		const ARbGameMode* Mode = Cast<ARbGameMode>(World->GetAuthGameMode());
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		return !Mode || (Director && Director->GetPhase() != ERbDirectorPhase::Idle);
	}

	void Check(UWorld* World)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		Test->TestNotNull(TEXT("a local player controller"), PC);
		if (!RbLevelSmoke::IsVenueMap(Map))
		{
			Test->TestTrue(TEXT("the title runs ARbTitleGameMode (World Settings)"), World->GetAuthGameMode() && World->GetAuthGameMode()->IsA<ARbTitleGameMode>());
			return;
		}

		const ARbGameMode* Mode = Cast<ARbGameMode>(World->GetAuthGameMode());
		if (!Test->TestNotNull(TEXT("the venue runs ARbGameMode (World Settings)"), Mode))
		{
			return;
		}
		Test->TestTrue(TEXT("the player possesses an ARbPlayerCharacter"), PC && Cast<ARbPlayerCharacter>(PC->GetPawn()) != nullptr);

		URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
		if (!Test->TestNotNull(TEXT("URbTableSubsystem"), Tables))
		{
			return;
		}
		FString Report;
		const bool bTablesValid = Tables->ValidateTables(Report);
		if (!Report.IsEmpty())
		{
			Test->AddInfo(Report);
		}
		Test->TestTrue(TEXT("tables valid (unique TableIndex, at least one table)"), bTablesValid);
		ARbTable* PlayerTable = Tables->GetPlayerTable();
		if (!Test->TestNotNull(TEXT("the player's table"), PlayerTable))
		{
			return;
		}
		Test->TestTrue(TEXT("the player's table has its physics context"), PlayerTable->HasContext());
		Test->TestTrue(TEXT("the match runs on the player's table"), Mode->GetTable() == PlayerTable);
		Test->TestNotNull(TEXT("the match's ball set"), Mode->GetBallSet());
		Test->TestNotNull(TEXT("the match's cue"), Mode->GetCue());

		// Sessions are registered by ARbGameMode from M2-E on (18.6.2); until then there are none. Once any session exists,
		// the player's session must exist and belong to the player's table.
		if (Tables->GetSessions().IsEmpty())
		{
			Test->AddInfo(TEXT("no table session registered (ARbGameMode session registration: M2-E)"));
		}
		else
		{
			const FRbTableSession* Session = Tables->GetPlayerSession();
			if (Test->TestNotNull(TEXT("the player's session"), Session))
			{
				Test->TestTrue(TEXT("the player's session plays the player's table"), Session->Table.Get() == PlayerTable);
				Test->TestTrue(TEXT("the player's session carries the match director"), Session->Director.Get() == Mode->GetDirector());
			}
		}
		Test->AddInfo(FString::Printf(TEXT("%s: %d table(s), player table index %d, director phase %d"), *Map, Tables->GetTables().Num(),
			PlayerTable->TableIndex, static_cast<int32>(Mode->GetDirector()->GetPhase())));
	}

	FAutomationTestBase* Test;
	FString Map;
	double Timeout;
	double SettleSeconds;
	double ReadyTime = -1.0;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRbLevelSmokeTest, "RawBreak.Functional.LevelSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void FRbLevelSmokeTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const FString& Map : RbLevelSmoke::PlayableMaps())
	{
		// Only generated levels: a package's map joins the list when its generator has run (rb_make_all.py).
		if (FPackageName::DoesPackageExist(Map))
		{
			OutBeautifiedNames.Add(FPackageName::GetShortName(Map));
			OutTestCommands.Add(Map);
		}
	}
}

bool FRbLevelSmokeTest::RunTest(const FString& Parameters)
{
	const FString Map = Parameters;
	if (!TestTrue(FString::Printf(TEXT("%s exists"), *Map), FPackageName::DoesPackageExist(Map)))
	{
		return false;
	}
	AutomationOpenMap(Map);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbLevelSmokeCommand(this, Map, 90.0, 2.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
