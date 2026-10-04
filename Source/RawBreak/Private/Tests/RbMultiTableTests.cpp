// Several tables per level (M2-E, Docs/ue-architecture.md 18.6.2): RawBreak.Unit.MultiTable.*
//   Registry               tables sorted by TableIndex, the player's table by tag (else the lowest index), FindTable,
//                          FindNearestTable, FindBallSet per table
//   ValidateTables         a duplicate / negative index, two player tags, no table -> FAIL with a report
//   Sessions               register / find (index, table, director) / player session / unregister, OnSessionsChanged
//   ShotMovesOnlyItsTable  two tables with their own ball sets and directors: a shot on the player's table moves only its balls;
//                          the other session is untouched; loose balls are keyed by (TableIndex, BallId); the venue seed hashes
//                          the index (every table of a hall has its own slope and balls)
//   PerTableState          (review) the world-wide ball-occlusion collection follows the player's table only; a replay suspends
//                          withholding / hides loose balls of the replayed table only
//   NoSingleTableLookups   grep test: "the first table found" (TActorIterator / TActorRange / TObjectIterator / GetAllActorsOfClass /
//                          GetActorOfClass / ForEachObjectOfClass over ARbTable, ARbBallSet, ARbCue) only inside RbTableSubsystem.cpp
// Owner: M2-E.

#include "Balls/RbBallSet.h"
#include "Balls/RbBallTestSupport.h"
#include "Balls/RbLooseBall.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Interaction/RbInteractionSubsystem.h"
#include "Table/RbTable.h"
#include "Tests/RbTestFlags.h"

#include "Components/StaticMeshComponent.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "rb/Rules/Evaluate.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbMultiTableTest
{
	ARbTable* SpawnTable(UWorld* World, int32 TableIndex, const FVector& Location, double YawDeg, ERbTablePreset Preset = ERbTablePreset::NineFootPro,
		bool bPlayerTag = false)
	{
		const FTransform Transform(FRotator(0.0, YawDeg, 0.0), Location);
		ARbTable* Table = World->SpawnActorDeferred<ARbTable>(ARbTable::StaticClass(), Transform);
		if (!Table)
		{
			return nullptr;
		}
		Table->Preset = Preset;
		Table->BallSet = Preset == ERbTablePreset::SevenFootBar ? ERbBallSetPreset::OldBarOversizedCue : ERbBallSetPreset::StandardPool;
		Table->TableIndex = TableIndex;
		if (bPlayerTag)
		{
			Table->Tags.Add(RbAssetPaths::Tag::PlayerTable);
		}
		Table->FinishSpawning(Transform);
		if (!Table->HasContext())
		{
			Table->RebuildTable();
		}
		return Table;
	}

	ARbBallSet* SpawnBallSet(UWorld* World, ARbTable* Table)
	{
		ARbBallSet* Balls = World->SpawnActor<ARbBallSet>();
		if (Balls)
		{
			Balls->InitForTable(Table);
		}
		return Balls;
	}

	// Every ball's shown pose (table-local UE) and visibility, bit for bit.
	TArray<FString> Poses(const ARbBallSet& Balls)
	{
		TArray<FString> Out;
		for (int32 Id = 0; Id < Balls.GetBallCount(); ++Id)
		{
			const UStaticMeshComponent* Ball = Balls.GetBallComponent(Id);
			const FVector P = Ball->GetRelativeLocation();
			const FQuat Q = Balls.GetBallOrientationUE(Id);
			Out.Add(FString::Printf(TEXT("%d %d %016llx %016llx %016llx %016llx %016llx %016llx %016llx"), Id, Balls.IsBallVisible(Id) ? 1 : 0,
				std::bit_cast<uint64>(P.X), std::bit_cast<uint64>(P.Y), std::bit_cast<uint64>(P.Z), std::bit_cast<uint64>(Q.X),
				std::bit_cast<uint64>(Q.Y), std::bit_cast<uint64>(Q.Z), std::bit_cast<uint64>(Q.W)));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMultiTableRegistry, "RawBreak.Unit.MultiTable.Registry", RB_UNIT_TEST_FLAGS)
bool FRbMultiTableRegistry::RunTest(const FString& Parameters)
{
	using namespace RbMultiTableTest;
	RbBallTest::FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
	if (!TestNotNull(TEXT("table subsystem"), Tables))
	{
		return false;
	}
	TestNull(TEXT("no table: no player table"), Tables->GetPlayerTable());
	ARbTable* Two = SpawnTable(World, 2, FVector(0.0, 600.0, 0.0), 0.0);
	ARbTable* Zero = SpawnTable(World, 0, FVector(0.0, 0.0, 0.0), 0.0);
	ARbTable* One = SpawnTable(World, 1, FVector(600.0, 0.0, 0.0), 90.0, ERbTablePreset::SevenFootBar);
	if (!Two || !Zero || !One)
	{
		AddError(TEXT("tables"));
		return false;
	}
	const TArray<ARbTable*> Sorted = Tables->GetTables();
	TestTrue(TEXT("sorted by TableIndex"), Sorted.Num() == 3 && Sorted[0] == Zero && Sorted[1] == One && Sorted[2] == Two);
	TestTrue(TEXT("FindTable"), Tables->FindTable(1) == One && Tables->FindTable(2) == Two && Tables->FindTable(7) == nullptr);
	TestTrue(TEXT("no tag: the lowest index is the player's"), Tables->GetPlayerTable() == Zero);
	Two->Tags.Add(RbAssetPaths::Tag::PlayerTable);
	TestTrue(TEXT("the tagged table is the player's"), Tables->GetPlayerTable() == Two);
	TestEqual(TEXT("one tag"), Tables->CountPlayerTableTags(), 1);
	FString Report;
	TestTrue(TEXT("valid"), Tables->ValidateTables(Report));
	AddInfo(Report);

	// Nearest by the playing field's outer boundary (not the actor origin): a point 20 cm beside table 1's long rail.
	const FVector BesideOne = One->CoreToWorld(rb::Vec3(0.0, 0.5 * One->GetContext().Spec.Width + One->GetContext().Spec.RailWidthTotal + 0.2, 0.0));
	TestTrue(TEXT("nearest table beside table 1"), Tables->FindNearestTable(BesideOne) == One);
	TestTrue(TEXT("nearest table over table 0's bed"), Tables->FindNearestTable(Zero->GetBedCenterWorld()) == Zero);

	// Ball sets per table (no sessions: the initialised ball set of that table).
	ARbBallSet* BallsZero = SpawnBallSet(World, Zero);
	ARbBallSet* BallsOne = SpawnBallSet(World, One);
	TestTrue(TEXT("FindBallSet per table"), Tables->FindBallSet(Zero) == BallsZero && Tables->FindBallSet(One) == BallsOne && Tables->FindBallSet(Two) == nullptr);
	TestEqual(TEXT("the 7-ft's ball set has the oversized cue ball"), BallsOne->GetBallRadiusCm(0), 100.0 * One->GetContext().BallRadius(0));

	// Review (M2-E): the queries iterate the object hash (no per-call allocation), so they must still see only THIS world's live
	// actors - another world's tables / ball sets (PIE beside the editor world, a second test world) and destroyed ones never.
	{
		RbBallTest::FTestWorld OtherWorld;
		ARbTable* Foreign = SpawnTable(OtherWorld.World, 5, FVector::ZeroVector, 0.0, ERbTablePreset::NineFootPro, true);
		ARbBallSet* ForeignBalls = OtherWorld.World->SpawnActor<ARbBallSet>(); // placed, no table yet (unassigned)
		ARbTable* Doomed = SpawnTable(World, 6, FVector(-900.0, 0.0, 0.0), 0.0);
		TestTrue(TEXT("scene of the other world"), Foreign && ForeignBalls && Doomed);
		TestTrue(TEXT("another world's table is not this world's"), Tables->FindTable(5) == nullptr && Tables->GetTables().Num() == 4);
		TestTrue(TEXT("another world's tagged table is not this world's player table"), Tables->GetPlayerTable() == Two);
		TestEqual(TEXT("another world's tag is not counted"), Tables->CountPlayerTableTags(), 1);
		TestNull(TEXT("another world's unassigned ball set is not this world's"), Tables->FindUnassignedBallSet());
		TestTrue(TEXT("the other world sees its own"), URbTableSubsystem::Get(OtherWorld.World)->GetPlayerTable() == Foreign &&
			URbTableSubsystem::Get(OtherWorld.World)->FindUnassignedBallSet() == ForeignBalls);
		TestTrue(TEXT("FindTable of a fresh table"), Doomed && Tables->FindTable(6) == Doomed);
		if (Doomed)
		{
			Doomed->Destroy();
		}
		TestTrue(TEXT("a destroyed table is gone"), Tables->FindTable(6) == nullptr && Tables->GetTables().Num() == 3);
		TestTrue(TEXT("nearest ignores the destroyed table"), Tables->FindNearestTable(FVector(-900.0, 0.0, 0.0)) == Zero);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMultiTableValidate, "RawBreak.Unit.MultiTable.ValidateTables", RB_UNIT_TEST_FLAGS)
bool FRbMultiTableValidate::RunTest(const FString& Parameters)
{
	using namespace RbMultiTableTest;
	RbBallTest::FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
	if (!TestNotNull(TEXT("table subsystem"), Tables))
	{
		return false;
	}
	FString Report;
	TestFalse(TEXT("no table: invalid"), Tables->ValidateTables(Report));
	SpawnTable(World, 0, FVector::ZeroVector, 0.0);
	ARbTable* Second = SpawnTable(World, 1, FVector(0.0, 500.0, 0.0), 0.0);
	TestTrue(TEXT("0 and 1: valid"), Tables->ValidateTables(Report));
	Second->TableIndex = 0;
	TestFalse(TEXT("a duplicate TableIndex is caught"), Tables->ValidateTables(Report));
	TestTrue(TEXT("the report names it"), Report.Contains(TEXT("duplicate")));
	AddInfo(Report);
	Second->TableIndex = -3;
	TestFalse(TEXT("a negative TableIndex is caught"), Tables->ValidateTables(Report));
	Second->TableIndex = 1;
	for (ARbTable* Table : Tables->GetTables())
	{
		Table->Tags.Add(RbAssetPaths::Tag::PlayerTable);
	}
	TestFalse(TEXT("two player tables are caught"), Tables->ValidateTables(Report));
	TestTrue(TEXT("the report names the tag"), Report.Contains(RbAssetPaths::Tag::PlayerTable.ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMultiTableSessions, "RawBreak.Unit.MultiTable.Sessions", RB_UNIT_TEST_FLAGS)
bool FRbMultiTableSessions::RunTest(const FString& Parameters)
{
	using namespace RbMultiTableTest;
	RbBallTest::FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
	ARbTable* Nine = SpawnTable(World, 0, FVector::ZeroVector, 0.0);
	ARbTable* Seven = SpawnTable(World, 1, FVector(0.0, 600.0, 0.0), 35.0, ERbTablePreset::SevenFootBar, true);
	if (!TestNotNull(TEXT("table subsystem"), Tables) || !Nine || !Seven)
	{
		return false;
	}
	int32 Changes = 0;
	Tables->OnSessionsChanged.AddLambda([&Changes]() { ++Changes; });
	TestNull(TEXT("no sessions yet"), Tables->GetPlayerSession());
	URbMatchDirector* DirectorNine = NewObject<URbMatchDirector>(World);
	URbMatchDirector* DirectorSeven = NewObject<URbMatchDirector>(World);
	ARbBallSet* BallsNine = SpawnBallSet(World, Nine);
	ARbBallSet* BallsSeven = SpawnBallSet(World, Seven);
	FRbTableSession A;
	A.TableIndex = 0;
	A.Table = Nine;
	A.BallSet = BallsNine;
	A.Director = DirectorNine;
	FRbTableSession B;
	B.TableIndex = 1;
	B.Table = Seven;
	B.BallSet = BallsSeven;
	B.Director = DirectorSeven;
	Tables->RegisterSession(A);
	Tables->RegisterSession(B);
	TestEqual(TEXT("two sessions"), Tables->GetSessions().Num(), 2);
	TestEqual(TEXT("two change events"), Changes, 2);
	TestTrue(TEXT("FindSession by index"), Tables->FindSession(1) && Tables->FindSession(1)->Director.Get() == DirectorSeven);
	TestTrue(TEXT("FindSessionForTable"), Tables->FindSessionForTable(Nine) && Tables->FindSessionForTable(Nine)->TableIndex == 0);
	TestTrue(TEXT("FindSessionForDirector"), Tables->FindSessionForDirector(DirectorSeven) && Tables->FindSessionForDirector(DirectorSeven)->Table.Get() == Seven);
	TestTrue(TEXT("the player session is the tagged 7-ft"), Tables->GetPlayerSession() && Tables->GetPlayerSession()->Table.Get() == Seven);
	TestTrue(TEXT("player director / ball set"), Tables->GetPlayerDirector() == DirectorSeven && Tables->GetPlayerBallSet() == BallsSeven);
	TestTrue(TEXT("FindBallSet prefers the session's"), Tables->FindBallSet(Seven) == BallsSeven);
	// Re-registering a table's session replaces it.
	URbMatchDirector* Replacement = NewObject<URbMatchDirector>(World);
	B.Director = Replacement;
	Tables->RegisterSession(B);
	TestEqual(TEXT("still two sessions"), Tables->GetSessions().Num(), 2);
	TestTrue(TEXT("replaced"), Tables->GetPlayerDirector() == Replacement);
	Tables->UnregisterSession(1);
	TestEqual(TEXT("one session left"), Tables->GetSessions().Num(), 1);
	TestNull(TEXT("the player table has no session now"), Tables->GetPlayerSession());
	TestEqual(TEXT("four change events"), Changes, 4);
	Tables->UnregisterSession(5);
	TestEqual(TEXT("unregistering an unknown index changes nothing"), Changes, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMultiTableShot, "RawBreak.Unit.MultiTable.ShotMovesOnlyItsTable", RB_UNIT_TEST_FLAGS)
bool FRbMultiTableShot::RunTest(const FString& Parameters)
{
	using namespace RbMultiTableTest;
	RbBallTest::FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(World);
	ARbTable* Nine = SpawnTable(World, 0, FVector::ZeroVector, 0.0);
	ARbTable* Seven = SpawnTable(World, 1, FVector(100.0, 520.0, 0.0), 90.0, ERbTablePreset::SevenFootBar, true);
	if (!Tables || !Loose || !Nine || !Seven)
	{
		AddError(TEXT("scene"));
		return false;
	}
	ARbTable* Scene[2] = {Nine, Seven};
	ARbBallSet* Balls[2] = {nullptr, nullptr};
	URbMatchDirector* Directors[2] = {nullptr, nullptr};
	for (int32 I = 0; I < 2; ++I)
	{
		Balls[I] = SpawnBallSet(World, Scene[I]);
		Directors[I] = NewObject<URbMatchDirector>(World);
		Directors[I]->Initialize(Scene[I], Balls[I], nullptr, nullptr);
		Directors[I]->SetLivePlaybackRate(0.0f);
		FRbMatchSetup Setup;
		Setup.Seed = 100 + I;
		TestTrue(FString::Printf(TEXT("match %d started"), I), Directors[I]->StartMatch(Setup));
		FRbTableSession Session;
		Session.TableIndex = Scene[I]->TableIndex;
		Session.Table = Scene[I];
		Session.BallSet = Balls[I];
		Session.Director = Directors[I];
		Tables->RegisterSession(Session);
	}
	TestTrue(TEXT("the player session is the tagged 7-ft"), Tables->GetPlayerDirector() == Directors[1]);
	TestTrue(TEXT("independent matches"), Directors[0]->GetMatchSeed() != Directors[1]->GetMatchSeed() &&
		Directors[0]->GetTableContext() != Directors[1]->GetTableContext());

	// Break on the player's table.
	const TArray<FString> OtherBefore = Poses(*Balls[0]);
	const TArray<FString> PlayerBefore = Poses(*Balls[1]);
	URbMatchDirector& Player = *Directors[1];
	const rb::rules::RulesTable& Rules = Player.GetMatchConfig().Table;
	TestTrue(TEXT("placed behind the head string"), Player.PlaceCueBall(rb::Vec2(Rules.HeadStringX - 0.10, 0.05)));
	const int32 Apex = rb::rules::LowestObjectBallAtStart(Player.GetMatchState().Game);
	const rb::Vec3 Aim = Player.GetTableState().Balls[Apex].State.Position - Player.GetTableState().Balls[0].State.Position;
	TestTrue(TEXT("break submitted"), Player.SubmitScriptedStrike(8.0, FMath::Atan2(Aim.y, Aim.x), 0.0, 0.0, -0.1));
	TestEqual(TEXT("the player's table shot"), static_cast<int32>(Player.GetMatchShotIndex()), 1);
	TestEqual(TEXT("the other table did not shoot"), static_cast<int32>(Directors[0]->GetMatchShotIndex()), 0);
	TestEqual(TEXT("the other table still waits for its break"), Directors[0]->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	TestTrue(TEXT("the other table's balls did not move (bitwise)"), Poses(*Balls[0]) == OtherBefore);
	TestFalse(TEXT("the player's balls moved"), Poses(*Balls[1]) == PlayerBefore);

	// Loose balls are keyed by (TableIndex, BallId): the 3 of each table leaves independently.
	rb::BallState Floor;
	Floor.Position = rb::Vec3(0.0, 1.2, -0.7);
	ARbLooseBall* LooseNine = Loose->HandOff(*Balls[0], 3, Floor);
	ARbLooseBall* LooseSeven = Loose->HandOff(*Balls[1], 3, Floor);
	TestTrue(TEXT("two loose 3s"), LooseNine && LooseSeven && LooseNine != LooseSeven);
	TestTrue(TEXT("found per table"), Loose->FindLooseBall(0, 3) == LooseNine && Loose->FindLooseBall(1, 3) == LooseSeven);
	TestTrue(TEXT("each at its own table's frame"), LooseNine && LooseSeven && !LooseNine->GetActorLocation().Equals(LooseSeven->GetActorLocation(), 1.0));
	TestEqual(TEXT("ReturnAll of table 0 returns only its ball"), Loose->ReturnAll(0), 1);
	TestTrue(TEXT("table 1's 3 still waits"), Loose->IsAwaitingReturn(1, 3) && Balls[1]->IsBallWithheld(3) && !Balls[0]->IsBallWithheld(3));
	Loose->ReturnAll(INDEX_NONE);

	// The venue seed hashes the table index (MakeVenueTableCondition / VenueBallSetSeed): every table of a hall has its own slope
	// and its own balls.
	for (ARbTable* Table : Scene)
	{
		Table->bUseVenueCondition = true;
		Table->VenueSeed = 20260928;
	}
	const FRbTableSetup SetupNine = Nine->MakeTableSetup();
	const FRbTableSetup SetupSeven = Seven->MakeTableSetup();
	TestTrue(TEXT("different ball-set seeds per table"), SetupNine.BallSetSeed != SetupSeven.BallSetSeed);
	TestTrue(TEXT("different slopes per table"), SetupNine.Condition.Slope.x != SetupSeven.Condition.Slope.x ||
		SetupNine.Condition.Slope.y != SetupSeven.Condition.Slope.y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMultiTablePerTableState, "RawBreak.Unit.MultiTable.PerTableState", RB_UNIT_TEST_FLAGS)
bool FRbMultiTablePerTableState::RunTest(const FString& Parameters)
{
	// Review (M2-E): state that exists once per world must follow the player's table, state that exists per table must stay per
	// table (18.6.2 rules 3 / 4):
	//  * MPC_RbBalls (the cloth's ball occlusion) is ONE collection: only the player's ball set writes it - an idle table racking
	//    after the player's (the player's table has the LOWER index, as in the dive bar) must not overwrite its entries;
	//  * a replay of the player's table suspends withholding and hides loose balls of THAT table only; the other table's loose
	//    balls stay visible, keep returning, and its withheld balls stay hidden.
	using namespace RbMultiTableTest;
	RbBallTest::FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(World);
	ARbTable* Player = SpawnTable(World, 0, FVector::ZeroVector, 0.0, ERbTablePreset::NineFootPro, true);
	ARbTable* Idle = SpawnTable(World, 1, FVector(100.0, 560.0, 0.0), 90.0, ERbTablePreset::SevenFootBar);
	ARbBallSet* PlayerBalls = Player ? SpawnBallSet(World, Player) : nullptr;
	ARbBallSet* IdleBalls = Idle ? SpawnBallSet(World, Idle) : nullptr;
	UMaterialParameterCollection* Mpc = RbBallTest::MakeBallMpc();
	if (!Loose || !PlayerBalls || !IdleBalls || !Mpc)
	{
		AddError(TEXT("scene"));
		return false;
	}
	TestTrue(TEXT("a ball set drives the collection by default (one table, tests, dev maps)"), PlayerBalls->DrivesOcclusion());
	IdleBalls->SetDrivesOcclusion(false); // what ARbGameMode does for every session but the player's
	PlayerBalls->SetOcclusionCollection(Mpc);
	IdleBalls->SetOcclusionCollection(Mpc);
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Mpc);
	if (!TestNotNull(TEXT("MPC instance"), Instance))
	{
		return false;
	}
	// Racks: the player's first, then the idle table's (StartPlay order = TableIndex order).
	const auto Rack = [](const ARbTable& Table, double Shift, rb::SimBall (&Out)[rb::kMaxBalls])
	{
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			Out[Id] = rb::SimBall{};
			Out[Id].InPlay = Id < 10;
			Out[Id].State.Position = rb::Vec3(-0.6 + 0.11 * Id, Shift - 0.03 * Id, Table.GetContext().BallRadius(Id));
		}
	};
	rb::SimBall PlayerRack[rb::kMaxBalls];
	rb::SimBall IdleRack[rb::kMaxBalls];
	Rack(*Player, 0.2, PlayerRack);
	Rack(*Idle, -0.1, IdleRack);
	PlayerBalls->ShowSimBalls(PlayerRack, rb::kMaxBalls);
	IdleBalls->ShowSimBalls(IdleRack, rb::kMaxBalls);
	const auto MpcHolds = [&](const ARbTable& Table, const rb::SimBall (&Rack)[rb::kMaxBalls]) -> bool
	{
		for (int32 Id = 0; Id < 10; ++Id)
		{
			FLinearColor Value;
			if (!Instance->GetVectorParameterValue(RbAssetPaths::Param::MpcBall(Id), Value) ||
				!FVector(Value.R, Value.G, Value.B).Equals(Table.CoreToWorld(Rack[Id].State.Position), 1e-2) || Value.A != 1.0f)
			{
				return false;
			}
		}
		return true;
	};
	TestTrue(TEXT("the collection holds the player's balls after the idle table racked"), MpcHolds(*Player, PlayerRack));
	IdleBalls->SetBallCore(3, rb::Vec3(0.3, 0.1, IdleRack[3].State.Position.z), rb::Quat::Identity());
	IdleBalls->SetBallVisible(4, false);
	TestTrue(TEXT("... and after the idle table moved / hid a ball"), MpcHolds(*Player, PlayerRack));
	// Switching the player to the other table (a later rebind): the new driver pushes its balls at once.
	PlayerBalls->SetDrivesOcclusion(false);
	IdleBalls->SetBallVisible(4, true);
	IdleBalls->SetBallCore(3, IdleRack[3].State.Position, rb::Quat::Identity());
	IdleBalls->SetDrivesOcclusion(true);
	TestTrue(TEXT("a new driver takes the collection over"), MpcHolds(*Idle, IdleRack));
	IdleBalls->SetDrivesOcclusion(false);
	PlayerBalls->SetDrivesOcclusion(true);
	TestTrue(TEXT("and gives it back"), MpcHolds(*Player, PlayerRack));

	// Replays are per table: a loose ball on each table, the idle table's 5 respotted (requested visible) and withheld.
	rb::BallState Floor;
	Floor.Position = rb::Vec3(0.0, 1.2, -0.7);
	rb::BallState Deep; // far below the floor: the kill-Z return
	Deep.Position = rb::Vec3(0.0, 1.2, -5.0);
	ARbLooseBall* PlayerLoose = Loose->HandOff(*PlayerBalls, 3, Deep);
	ARbLooseBall* IdleLoose = Loose->HandOff(*IdleBalls, 5, Floor);
	ARbLooseBall* IdleDeep = Loose->HandOff(*IdleBalls, 6, Deep);
	if (!TestTrue(TEXT("loose balls on both tables"), PlayerLoose && IdleLoose && IdleDeep))
	{
		return false;
	}
	Loose->SetReplayActive(true, Player->TableIndex);
	TestTrue(TEXT("a replay plays"), Loose->IsReplayActive() && Loose->IsReplayActive(Player->TableIndex) && !Loose->IsReplayActive(Idle->TableIndex));
	TestTrue(TEXT("the replayed table: withholding suspended, its loose ball hidden"), PlayerBalls->IsWithholdSuspended() && PlayerLoose->IsHidden());
	TestTrue(TEXT("the other table: withholding stays, its loose balls stay visible"), !IdleBalls->IsWithholdSuspended() && !IdleLoose->IsHidden() && !IdleDeep->IsHidden());
	TestFalse(TEXT("the other table's respotted 5 stays hidden"), IdleBalls->IsBallVisible(5));
	TestNull(TEXT("no pick-up offered during a replay"), Loose->FindGazedBall(FRbInteractionQuery{}));
	Loose->UpdateAutomaticReturns(0.1);
	TestFalse(TEXT("the other table keeps returning (kill Z)"), Loose->IsAwaitingReturn(Idle->TableIndex, 6));
	TestTrue(TEXT("the replayed table's loose ball waits for the live room"), Loose->IsAwaitingReturn(Player->TableIndex, 3));
	ARbLooseBall* During = Loose->HandOff(*PlayerBalls, 7, Floor);
	TestTrue(TEXT("a hand-off on the replayed table starts hidden"), During && During->IsHidden());
	Loose->SetReplayActive(false, INDEX_NONE);
	TestFalse(TEXT("replay over"), Loose->IsReplayActive());
	TestTrue(TEXT("the replayed table: withheld again, loose balls visible"), !PlayerBalls->IsWithholdSuspended() && !PlayerLoose->IsHidden() && During && !During->IsHidden());
	Loose->UpdateAutomaticReturns(0.1);
	TestFalse(TEXT("back live: the replayed table's kill-Z return"), Loose->IsAwaitingReturn(Player->TableIndex, 3));
	Loose->ReturnAll(INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMultiTableGrep, "RawBreak.Unit.MultiTable.NoSingleTableLookups", RB_UNIT_TEST_FLAGS)
bool FRbMultiTableGrep::RunTest(const FString& Parameters)
{
	// Rule 1 of 18.6.2: code that needs a table asks URbTableSubsystem for a specific one; iterating the world's tables, ball sets or
	// cues ("the first table found") is allowed only inside RbTableSubsystem.cpp.
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.cpp"), true, false, false);
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.h"), true, false, false);
	if (!TestTrue(FString::Printf(TEXT("the sources are there (%s: %d files)"), *Root, Files.Num()), Files.Num() > 50))
	{
		return false;
	}
	const FRegexPattern Pattern(TEXT("(TActorIterator|TActorRange|TObjectIterator|TObjectRange)\\s*<\\s*(const\\s+)?ARb(Table|BallSet|Cue)\\s*>|")
		TEXT("(GetAllActorsOfClass|GetActorOfClass|ForEachObjectOfClass|GetObjectsOfClass)\\s*\\([^;]*ARb(Table|BallSet|Cue)::StaticClass"));
	const FString Allowed = TEXT("RbTableSubsystem.cpp");
	// Files of other M2 packages with a lookup the M2-E report asks their owners to replace (removed here at the merge). Integration
	// round (integ/m2): RbTestRoom.cpp, RbCueDemo.cpp and RbPlayerCharacter.cpp no longer iterate; the look-dev camera's fallback
	// cue (a look-dev level without a session) is the one accepted lookup left.
	const TMap<FString, FString> Pending = {
		{TEXT("RbLookDevCamera.cpp"), TEXT("M2-L: ARbLookDevCamera fallback cue lookup (no session in a look-dev level), cached once")},
	};
	int32 Violations = 0;
	int32 PendingHits = 0;
	for (const FString& File : Files)
	{
		const FString Name = FPaths::GetCleanFilename(File);
		if (Name == Allowed)
		{
			continue;
		}
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *File))
		{
			continue;
		}
		for (int32 Line = 0; Line < Lines.Num(); ++Line)
		{
			const FString Trimmed = Lines[Line].TrimStart();
			if (Trimmed.StartsWith(TEXT("//")))
			{
				continue; // documentation
			}
			FRegexMatcher Matcher(Pattern, Lines[Line]);
			if (!Matcher.FindNext())
			{
				continue;
			}
			if (const FString* Request = Pending.Find(Name))
			{
				++PendingHits;
				AddInfo(FString::Printf(TEXT("pending request %s:%d (%s)"), *Name, Line + 1, **Request));
				continue;
			}
			++Violations;
			AddError(FString::Printf(TEXT("single-table lookup outside RbTableSubsystem.cpp: %s:%d: %s"), *File, Line + 1, *Trimmed));
		}
	}
	AddInfo(FString::Printf(TEXT("%d files scanned, %d violations, %d pending requests of other packages"), Files.Num(), Violations, PendingHits));
	return Violations == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
