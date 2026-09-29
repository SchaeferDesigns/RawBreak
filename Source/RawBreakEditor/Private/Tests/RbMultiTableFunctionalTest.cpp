// RawBreak.Functional.MultiTable (M2-E, Docs/ue-architecture.md 18.6.2): several tables per level IN THE GAME, on the M2-E dev
// level /Game/Dev/M2E/L_TwoTables (a 9-ft pro table, TableIndex 0, and a 7-ft bar table, TableIndex 1, tagged RbPlayerTable,
// different yaws; its World Settings start ARbGameMode):
//   * the level's tables validate (unique indices, one player tag); ARbGameMode registered one session per table (table, ball
//     set, cue, director - none shared); the player's session is the tagged 7-ft and IS the game mode's M1 accessors; the pawn's
//     stroke component drives only the player's director; only the player's director records replays;
//   * the idle 9-ft plays its own practice rack (its own seed), rendered on its own table; both ball sets hand off loose balls;
//   * a break on the player's table (live playback) moves only its balls: the other table's balls, director state and phase stay
//     bit for bit; the replay history holds the player's shot only;
//   * loose balls are keyed by (TableIndex, BallId): the 3 of each table leaves beside its own table and returns alone;
//   * ValidateTables catches a duplicate TableIndex placed at run time; the player table falls back to the lowest index without
//     the tag.
// Needs the dev level (python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2e.py). Owner: M2-E.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBall.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Cue/RbCue.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Player/RbPlayerCharacter.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbShot.h"
#include "Table/RbTable.h"

#include "rb/Rules/Evaluate.h"

#include <bit>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbMultiTableFlow
{
	const TCHAR* const DevMap = TEXT("/Game/Dev/M2E/L_TwoTables");

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	ARbGameMode* GameMode()
	{
		UWorld* World = PlayWorld();
		return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
	}

	// Every ball's shown pose (world) and visibility, bit for bit.
	FString Poses(const ARbBallSet& Balls)
	{
		FString Out;
		for (int32 Id = 0; Id < Balls.GetBallCount(); ++Id)
		{
			const UStaticMeshComponent* Ball = Balls.GetBallComponent(Id);
			const FVector P = Ball ? Ball->GetComponentLocation() : FVector::ZeroVector;
			const FQuat Q = Ball ? Ball->GetComponentQuat() : FQuat::Identity;
			Out += FString::Printf(TEXT("%d:%d %016llx %016llx %016llx %016llx %016llx %016llx %016llx\n"), Id, Balls.IsBallVisible(Id) ? 1 : 0,
				std::bit_cast<uint64>(P.X), std::bit_cast<uint64>(P.Y), std::bit_cast<uint64>(P.Z), std::bit_cast<uint64>(Q.X),
				std::bit_cast<uint64>(Q.Y), std::bit_cast<uint64>(Q.Z), std::bit_cast<uint64>(Q.W));
		}
		return Out;
	}

	// The director's table state and match progress, bit for bit.
	FString DirectorDigest(const URbMatchDirector& Director)
	{
		FString Out = FString::Printf(TEXT("phase=%d shot=%u rack=%d seed=%llu"), static_cast<int32>(Director.GetPhase()), Director.GetMatchShotIndex(),
			Director.GetMatchState().RackNumber, Director.GetMatchSeed());
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::SimBall& Ball = Director.GetTableState().Balls[Id];
			if (Ball.InPlay)
			{
				Out += FString::Printf(TEXT(" %d(%016llx,%016llx)"), Id, std::bit_cast<uint64>(Ball.State.Position.x), std::bit_cast<uint64>(Ball.State.Position.y));
			}
		}
		return Out;
	}

	// The shown balls of a ball set lie on its own table's bed (outer boundary of the playing field).
	int32 CountBallsOnTable(const ARbBallSet& Balls, const ARbTable& Table)
	{
		const rb::TableSpec& Spec = Table.GetContext().Spec;
		int32 Count = 0;
		for (int32 Id = 0; Id < Balls.GetBallCount(); ++Id)
		{
			const UStaticMeshComponent* Ball = Balls.GetBallComponent(Id);
			if (Ball && Balls.IsBallVisible(Id))
			{
				const rb::Vec3 P = Table.WorldToCore(Ball->GetComponentLocation());
				Count += (FMath::Abs(P.x) < 0.5 * Spec.Length && FMath::Abs(P.y) < 0.5 * Spec.Width && FMath::Abs(P.z - Table.GetContext().BallRadius(Id)) < 1.0e-3) ? 1 : 0;
			}
		}
		return Count;
	}
}

class FRbMultiTableWaitForMatch : public IAutomationLatentCommand
{
public:
	FRbMultiTableWaitForMatch(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* Mode = RbMultiTableFlow::GameMode();
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (Director && Director->GetPhase() != ERbDirectorPhase::Idle)
		{
			return true;
		}
		if (GetCurrentRunTime() > Timeout)
		{
			Test->AddError(TEXT("PIE on L_TwoTables did not start an ARbGameMode match (World Settings game mode?)"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Timeout;
};

class FRbMultiTableFlowCommand : public IAutomationLatentCommand
{
public:
	explicit FRbMultiTableFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		const ARbGameMode* Current = RbMultiTableFlow::GameMode();
		if (!Current || !Current->GetDirector() || Current->GetDirector()->GetPhase() == ERbDirectorPhase::Idle)
		{
			if (GetCurrentRunTime() > 300.0)
			{
				Test->AddError(TEXT("no PIE match"));
				return true;
			}
			return false;
		}
		if (!Resolve())
		{
			return true;
		}
		if (Stage != EStage::Setup && SetupWorld.Get() != RbMultiTableFlow::PlayWorld())
		{
			if (++Restarts > 2)
			{
				Test->AddError(TEXT("the play world keeps changing"));
				return true;
			}
			Test->AddInfo(TEXT("new PIE world: starting over"));
			Stage = EStage::Setup;
		}
		bool bDone = false;
		switch (Stage)
		{
		case EStage::Setup: bDone = StageSetup(); break;
		case EStage::Break: bDone = StageBreak(); break;
		case EStage::LooseBalls: bDone = StageLooseBalls(); break;
		case EStage::Validate: bDone = StageValidate(); break;
		case EStage::Finish: return true;
		}
		if (bDone)
		{
			return true;
		}
		if (Stage != LastStage)
		{
			LastStage = Stage;
			StageStart = FPlatformTime::Seconds();
			bStageStarted = false;
		}
		else if (FPlatformTime::Seconds() - StageStart > 45.0)
		{
			Test->AddError(FString::Printf(TEXT("stage %d timed out"), static_cast<int32>(Stage)));
			return true;
		}
		return false;
	}

private:
	enum class EStage : uint8
	{
		Setup,
		Break,
		LooseBalls,
		Validate,
		Finish,
	};

	bool Resolve()
	{
		UWorld* World = RbMultiTableFlow::PlayWorld();
		Mode = RbMultiTableFlow::GameMode();
		Tables = URbTableSubsystem::Get(World);
		Loose = URbLooseBallSubsystem::Get(World);
		Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		Character = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
		return Test->TestNotNull(TEXT("ARbGameMode"), Mode) && Test->TestNotNull(TEXT("table subsystem"), Tables) &&
			Test->TestNotNull(TEXT("loose-ball subsystem"), Loose) && Test->TestNotNull(TEXT("replay subsystem"), Replay) &&
			Test->TestNotNull(TEXT("ARbPlayerCharacter"), Character);
	}

	// --- the level's sessions ------------------------------------------------------------------------------------------------

	bool StageSetup()
	{
		SetupWorld = RbMultiTableFlow::PlayWorld();
		FString Report;
		const bool bValid = Tables->ValidateTables(Report);
		Test->AddInfo(Report);
		Test->TestTrue(TEXT("the level's tables validate"), bValid);
		const TArray<ARbTable*> Level = Tables->GetTables();
		if (!Test->TestEqual(TEXT("two tables"), Level.Num(), 2) || !Test->TestEqual(TEXT("one session per table"), Tables->GetSessions().Num(), 2))
		{
			return true;
		}
		const FRbTableSession* Player = Tables->GetPlayerSession();
		const FRbTableSession* Idle = Tables->FindSession(0);
		if (!Test->TestTrue(TEXT("player session = the tagged 7-ft (TableIndex 1)"), Player && Player->TableIndex == 1 && Player->Table.IsValid() &&
			Player->Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable) && Player->Table->Preset == ERbTablePreset::SevenFootBar) ||
			!Test->TestTrue(TEXT("the 9-ft's session"), Idle && Idle->Table.IsValid() && Idle->Table->Preset == ERbTablePreset::NineFootPro))
		{
			return true;
		}
		PlayerTable = Player->Table.Get();
		OtherTable = Idle->Table.Get();
		PlayerDirector = Player->Director.Get();
		OtherDirector = Idle->Director.Get();
		PlayerBalls = Player->BallSet.Get();
		OtherBalls = Idle->BallSet.Get();
		if (!PlayerDirector || !OtherDirector || !PlayerBalls || !OtherBalls || !Player->Cue.IsValid() || !Idle->Cue.IsValid())
		{
			Test->AddError(TEXT("a session without its director / ball set / cue"));
			return true;
		}
		Test->TestTrue(TEXT("the player session IS the game mode's M1 accessors"), Mode->GetDirector() == PlayerDirector &&
			Mode->GetTable() == PlayerTable && Mode->GetBallSet() == PlayerBalls && Mode->GetCue() == Player->Cue.Get());
		Test->TestTrue(TEXT("nothing shared between the sessions"), PlayerDirector != OtherDirector && PlayerBalls != OtherBalls &&
			Player->Cue.Get() != Idle->Cue.Get() && PlayerDirector->GetTableContext() != OtherDirector->GetTableContext());
		Test->TestTrue(TEXT("each session's actors belong to its table"), PlayerBalls->GetTable() == PlayerTable && OtherBalls->GetTable() == OtherTable &&
			PlayerDirector->GetTable() == PlayerTable && OtherDirector->GetTable() == OtherTable && OtherDirector->GetCue() == Idle->Cue.Get());
		Test->TestTrue(TEXT("the pawn's stroke drives only the player's director"), PlayerDirector->GetStrokeComponent() == Character->GetStroke() &&
			OtherDirector->GetStrokeComponent() == nullptr);
		Test->TestTrue(TEXT("only the player's director records replays"), PlayerDirector->RecordsReplays() && !OtherDirector->RecordsReplays());
		Test->TestTrue(TEXT("both ball sets hand off loose balls"), Loose->IsBound(PlayerBalls) && Loose->IsBound(OtherBalls));
		Test->TestTrue(TEXT("FindBallSet per table"), Tables->FindBallSet(PlayerTable) == PlayerBalls && Tables->FindBallSet(OtherTable) == OtherBalls);

		// The idle 9-ft plays its own practice rack, on its own table, with its own seed.
		Test->TestTrue(TEXT("the idle table's match runs (practice, rack shown)"), OtherDirector->GetPhase() != ERbDirectorPhase::Idle &&
			OtherDirector->GetSetup().Mode == ERbMatchMode::Practice);
		Test->TestTrue(TEXT("its own seed"), OtherDirector->GetMatchSeed() != PlayerDirector->GetMatchSeed());
		const int32 OtherShown = RbMultiTableFlow::CountBallsOnTable(*OtherBalls, *OtherTable);
		const int32 PlayerShown = RbMultiTableFlow::CountBallsOnTable(*PlayerBalls, *PlayerTable);
		Test->AddInfo(FString::Printf(TEXT("balls shown on their own bed: 9-ft %d, 7-ft %d"), OtherShown, PlayerShown));
		Test->TestTrue(TEXT("the 9-ft's rack is rendered on the 9-ft"), OtherShown >= 9);
		Test->TestTrue(TEXT("the 7-ft's rack is rendered on the 7-ft"), PlayerShown >= 9);
		Stage = EStage::Break;
		return false;
	}

	// --- a break on the player's table moves only its balls ---------------------------------------------------------------

	bool StageBreak()
	{
		if (!bStageStarted)
		{
			OtherPosesBefore = RbMultiTableFlow::Poses(*OtherBalls);
			OtherDigestBefore = RbMultiTableFlow::DirectorDigest(*OtherDirector);
			PlayerPosesBefore = RbMultiTableFlow::Poses(*PlayerBalls);
			ShotsBefore = Replay->GetShotCount();
			PlayerDirector->SetLivePlaybackRate(4.0f);
			if (PlayerDirector->GetPhase() == ERbDirectorPhase::AwaitPlacement)
			{
				const rb::rules::RulesTable& Rules = PlayerDirector->GetMatchConfig().Table;
				Test->TestTrue(TEXT("ball in hand placed behind the head string"), PlayerDirector->PlaceCueBall(rb::Vec2(Rules.HeadStringX - 0.10, 0.05)));
			}
			const int32 Apex = rb::rules::LowestObjectBallAtStart(PlayerDirector->GetMatchState().Game);
			const rb::Vec3 Aim = PlayerDirector->GetTableState().Balls[Apex].State.Position - PlayerDirector->GetTableState().Balls[0].State.Position;
			if (!Test->TestTrue(TEXT("break submitted on the player's table"), PlayerDirector->SubmitScriptedStrike(8.0, FMath::Atan2(Aim.y, Aim.x), 0.0, 0.0, -0.1)))
			{
				Test->AddError(PlayerDirector->GetLastError());
				return true;
			}
			bStageStarted = true;
			return false;
		}
		const ERbDirectorPhase Phase = PlayerDirector->GetPhase();
		if (Phase == ERbDirectorPhase::Simulating || Phase == ERbDirectorPhase::PlayingBack)
		{
			bSawPlayback |= Phase == ERbDirectorPhase::PlayingBack;
			bOtherStill &= RbMultiTableFlow::Poses(*OtherBalls) == OtherPosesBefore;
			return false;
		}
		Test->TestTrue(TEXT("the break played back live"), bSawPlayback);
		Test->TestEqual(TEXT("the player's table shot"), PlayerDirector->GetMatchShotIndex(), 1u);
		Test->TestFalse(TEXT("the player's balls moved"), RbMultiTableFlow::Poses(*PlayerBalls) == PlayerPosesBefore);
		Test->TestTrue(TEXT("the other table's balls never moved during the playback (bitwise)"), bOtherStill);
		Test->TestEqual(TEXT("the other table's balls after the shot (bitwise)"), RbMultiTableFlow::Poses(*OtherBalls), OtherPosesBefore);
		Test->TestEqual(TEXT("the other director's state and phase (bitwise)"), RbMultiTableFlow::DirectorDigest(*OtherDirector), OtherDigestBefore);
		const TSharedPtr<const FRbShot> Recorded = Replay->GetShot(0);
		Test->TestTrue(TEXT("the replay history got the player's shot only"), Replay->GetShotCount() == ShotsBefore + 1 && Recorded.IsValid() &&
			Recorded == PlayerDirector->GetLastCommittedShot() && Recorded->Request.Table == PlayerDirector->GetTableContext());
		Stage = EStage::LooseBalls;
		return false;
	}

	// --- loose balls keyed by (TableIndex, BallId) ------------------------------------------------------------------------

	bool StageLooseBalls()
	{
		constexpr int32 Id = 3;
		if (!bStageStarted)
		{
			ARbTable* Scene[2] = {OtherTable, PlayerTable};
			ARbBallSet* Sets[2] = {OtherBalls, PlayerBalls};
			for (int32 I = 0; I < 2; ++I)
			{
				// 40 cm beside the +x end rail of its own table, a few centimetres above the floor.
				const rb::TableSpec& Spec = Scene[I]->GetContext().Spec;
				rb::BallState State;
				State.Position = rb::Vec3(0.5 * Spec.Length + Spec.RailWidthTotal + 0.40, 0.0, -Scene[I]->GetContext().BedHeight() + 0.05);
				LooseByTable[I] = Loose->HandOff(*Sets[I], Id, State, rb::Quat::Identity(), rb::OffTableReason::Floor, 0);
			}
			Test->TestTrue(TEXT("the 3 of each table handed off"), LooseByTable[0].IsValid() && LooseByTable[1].IsValid() && LooseByTable[0] != LooseByTable[1]);
			Test->TestTrue(TEXT("found per table"), Loose->FindLooseBall(0, Id) == LooseByTable[0].Get() && Loose->FindLooseBall(1, Id) == LooseByTable[1].Get());
			Test->TestTrue(TEXT("each belongs to its table"), LooseByTable[0].IsValid() && LooseByTable[1].IsValid() &&
				LooseByTable[0]->GetTable() == OtherTable && LooseByTable[1]->GetTable() == PlayerTable);
			Test->TestTrue(TEXT("each withholds only its own table's 3"), OtherBalls->IsBallWithheld(Id) && PlayerBalls->IsBallWithheld(Id));
			bStageStarted = true;
			return false;
		}
		if (FPlatformTime::Seconds() - StageStart < 1.0)
		{
			return false; // they fall to the floor under engine physics
		}
		const ARbLooseBall* A = LooseByTable[0].Get();
		const ARbLooseBall* B = LooseByTable[1].Get();
		Test->TestTrue(TEXT("both came down beside their own tables"), A && B && Tables->FindNearestTable(A->GetActorLocation()) == OtherTable &&
			Tables->FindNearestTable(B->GetActorLocation()) == PlayerTable);
		Test->TestEqual(TEXT("ReturnAll of table 0 returns only its ball"), Loose->ReturnAll(0, ERbLooseBallReturn::Manual), 1);
		Test->TestTrue(TEXT("the 7-ft's 3 still waits"), Loose->IsAwaitingReturn(1, Id) && PlayerBalls->IsBallWithheld(Id) && !OtherBalls->IsBallWithheld(Id));
		Test->TestEqual(TEXT("then the 7-ft's"), Loose->ReturnAll(1, ERbLooseBallReturn::Manual), 1);
		Test->TestEqual(TEXT("no loose ball left"), Loose->GetNumLooseBalls(), 0);
		Stage = EStage::Validate;
		return false;
	}

	// --- validation at run time and the player-table fallback -------------------------------------------------------------

	bool StageValidate()
	{
		UWorld* World = RbMultiTableFlow::PlayWorld();
		const FTransform Far(FRotator::ZeroRotator, FVector(0.0, -2000.0, 0.0));
		ARbTable* Duplicate = World->SpawnActorDeferred<ARbTable>(ARbTable::StaticClass(), Far);
		if (!Test->TestNotNull(TEXT("a third table"), Duplicate))
		{
			return true;
		}
		Duplicate->TableIndex = 0;
		Duplicate->bUseBakedMeshes = true;
		Duplicate->FinishSpawning(Far);
		FString Report;
		Test->TestFalse(TEXT("a duplicate TableIndex is caught"), Tables->ValidateTables(Report));
		Test->TestTrue(TEXT("the report names it"), Report.Contains(TEXT("duplicate")));
		Test->AddInfo(Report);
		Duplicate->Destroy();
		Test->TestTrue(TEXT("valid again without it"), Tables->ValidateTables(Report));

		PlayerTable->Tags.Remove(RbAssetPaths::Tag::PlayerTable);
		Test->TestTrue(TEXT("without the tag the lowest TableIndex is the player's"), Tables->GetPlayerTable() == OtherTable);
		PlayerTable->Tags.Add(RbAssetPaths::Tag::PlayerTable);
		Test->TestTrue(TEXT("the tag wins again"), Tables->GetPlayerTable() == PlayerTable && Tables->GetPlayerDirector() == PlayerDirector);
		Stage = EStage::Finish;
		return false;
	}

	FAutomationTestBase* Test;
	int32 Restarts = 0;
	TWeakObjectPtr<UWorld> SetupWorld;
	EStage Stage = EStage::Setup;
	EStage LastStage = EStage::Finish;
	double StageStart = 0.0;
	bool bStageStarted = false;

	ARbGameMode* Mode = nullptr;
	URbTableSubsystem* Tables = nullptr;
	URbLooseBallSubsystem* Loose = nullptr;
	URbReplaySubsystem* Replay = nullptr;
	ARbPlayerCharacter* Character = nullptr;

	ARbTable* PlayerTable = nullptr;
	ARbTable* OtherTable = nullptr;
	URbMatchDirector* PlayerDirector = nullptr;
	URbMatchDirector* OtherDirector = nullptr;
	ARbBallSet* PlayerBalls = nullptr;
	ARbBallSet* OtherBalls = nullptr;

	FString OtherPosesBefore;
	FString OtherDigestBefore;
	FString PlayerPosesBefore;
	int32 ShotsBefore = 0;
	bool bSawPlayback = false;
	bool bOtherStill = true;
	TWeakObjectPtr<ARbLooseBall> LooseByTable[2];
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMultiTableFunctionalTest, "RawBreak.Functional.MultiTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbMultiTableFunctionalTest::RunTest(const FString& Parameters)
{
	if (!FPackageName::DoesPackageExist(RbMultiTableFlow::DevMap))
	{
		AddError(FString::Printf(TEXT("%s missing: run python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2e.py"), RbMultiTableFlow::DevMap));
		return false;
	}
	AutomationOpenMap(RbMultiTableFlow::DevMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbMultiTableWaitForMatch(this, 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbMultiTableFlowCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
