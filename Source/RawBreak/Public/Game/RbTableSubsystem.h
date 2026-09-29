#pragma once

// Multi-table groundwork (decisions 2026-09-28; Docs/ue-architecture.md 18.6.2): the registry of the tables of a level and of
// the per-table "sessions" (table + ball set + cue + match director). Nothing on the Unreal side may assume one table or one
// match per level: code that needs "the" table asks for a specific one (by TableIndex, by actor, by position) or for the
// PLAYER's table (the table tagged RbAssetPaths::Tag::PlayerTable, else the lowest TableIndex).
//
//   * ARbTable::TableIndex is unique per level (0..N-1); ValidateTables reports duplicates (the level validators call it).
//   * ARbGameMode registers one FRbTableSession per table it runs a match on (M2: only the player's table plays; the other
//     tables stay idle until the AI regulars of V2 play there). ARbGameMode::GetDirector() / GetTable() / GetBallSet() /
//     GetCue() remain as the PLAYER session's accessors (UI, cheats, tests of M1 keep working).
//   * Per-table consumers key their state by TableIndex: loose balls (URbLooseBallSubsystem), table audio + shot clocks
//     (URbTableAudioComponent, one per table), replays (the player's table), later the score slates.
// Owner: M2-E. The table queries are implemented by the M2 architect step (actor iteration, no caching assumptions); the
// session registration is M2-E's (TODO(M2-E) in ARbGameMode).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "RbTableSubsystem.generated.h"

class ARbBallSet;
class ARbCue;
class ARbTable;
class URbMatchDirector;

// One table with its match (all weak: the actors / director own themselves).
struct FRbTableSession
{
	int32 TableIndex = INDEX_NONE;
	TWeakObjectPtr<ARbTable> Table;
	TWeakObjectPtr<ARbBallSet> BallSet;
	TWeakObjectPtr<ARbCue> Cue;
	TWeakObjectPtr<URbMatchDirector> Director;

	bool IsValid() const { return Table.IsValid(); }
};

DECLARE_MULTICAST_DELEGATE(FRbOnTableSessionsChanged);

UCLASS()
class RAWBREAK_API URbTableSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static URbTableSubsystem* Get(const UObject* WorldContext);

	// --- tables (every ARbTable of the world, sorted by TableIndex) ---------------------------------------------------
	TArray<ARbTable*> GetTables() const;
	ARbTable* FindTable(int32 TableIndex) const;
	// The table the local player plays at: tagged RbPlayerTable, else the lowest TableIndex; nullptr without tables.
	ARbTable* GetPlayerTable() const;
	// The table whose playing field (outer boundary) is nearest to a world point (NPC / audio LOD / pick-up routing).
	ARbTable* FindNearestTable(const FVector& WorldPoint) const;
	// The ball set spawned for a table (session first, else the world's ARbBallSet whose GetTable() is Table).
	ARbBallSet* FindBallSet(const ARbTable* Table) const;
	// False (with a report) when two tables share a TableIndex or none is valid.
	bool ValidateTables(FString& OutReport) const;

	// --- sessions (registered by ARbGameMode; M2-E) --------------------------------------------------------------------
	void RegisterSession(const FRbTableSession& Session);
	void UnregisterSession(int32 TableIndex);
	const FRbTableSession* FindSession(int32 TableIndex) const;
	const FRbTableSession* FindSessionForTable(const ARbTable* Table) const;
	const FRbTableSession* GetPlayerSession() const;
	const TArray<FRbTableSession>& GetSessions() const { return Sessions; }

	FRbOnTableSessionsChanged OnSessionsChanged;

private:
	TArray<FRbTableSession> Sessions;
};
