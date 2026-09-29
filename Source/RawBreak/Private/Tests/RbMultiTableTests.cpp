// Several tables per level (M2-E, Docs/ue-architecture.md 18.6.2): RawBreak.Unit.MultiTable.*
//   Registry               tables sorted by TableIndex, the player's table by tag (else the lowest index), FindTable,
//                          FindNearestTable, FindBallSet per table
//   ValidateTables         a duplicate / negative index, two player tags, no table -> FAIL with a report
//   Sessions               register / find (index, table, director) / player session / unregister, OnSessionsChanged
//   ShotMovesOnlyItsTable  two tables with their own ball sets and directors: a shot on the player's table moves only its balls;
//                          the other session is untouched; loose balls are keyed by (TableIndex, BallId); the venue seed hashes
//                          the index (every table of a hall has its own slope and balls)
//   NoSingleTableLookups   grep test: "the first table found" (TActorIterator / TActorRange / GetAllActorsOfClass / GetActorOfClass
//                          over ARbTable, ARbBallSet, ARbCue) only inside RbTableSubsystem.cpp
// Owner: M2-E.

#include "Balls/RbBallSet.h"
#include "Balls/RbBallTestSupport.h"
#include "Balls/RbLooseBall.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Table/RbTable.h"
#include "Tests/RbTestFlags.h"

#include "Components/StaticMeshComponent.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
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
	const FRegexPattern Pattern(TEXT("(TActorIterator|TActorRange)\\s*<\\s*(const\\s+)?ARb(Table|BallSet|Cue)\\s*>|(GetAllActorsOfClass|GetActorOfClass)\\s*\\([^;]*ARb(Table|BallSet|Cue)::StaticClass"));
	const FString Allowed = TEXT("RbTableSubsystem.cpp");
	// Files of other M2 packages with a lookup the M2-E report asks their owners to replace (removed here at the merge).
	const TMap<FString, FString> Pending = {
		{TEXT("RbTestRoom.cpp"), TEXT("M2-L: ARbTestRoom::FindTable / the validator -> URbTableSubsystem::GetPlayerTable / GetTables")},
		{TEXT("RbLookDevCamera.cpp"), TEXT("M2-L: ARbLookDevCamera table / cue lookup -> URbTableSubsystem::GetPlayerTable, the player session's cue")},
		{TEXT("RbCueDemo.cpp"), TEXT("M2-F: ARbCueDemo table lookup -> URbTableSubsystem::GetPlayerTable")},
		{TEXT("RbPlayerCharacter.cpp"), TEXT("M2-F: ARbPlayerCharacter table fallback -> URbTableSubsystem::GetPlayerTable")},
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
