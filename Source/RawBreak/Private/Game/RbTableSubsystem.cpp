#include "Game/RbTableSubsystem.h"

#include "Balls/RbBallSet.h"
#include "Core/RbAssetPaths.h"
#include "Cue/RbCue.h"
#include "Game/RbMatchDirector.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "UObject/UObjectHash.h"

// Owner: M2-E. Table queries (the architect step's) + sessions (registered by ARbGameMode). The ONLY file that iterates the
// world's tables, ball sets or cues (RawBreak.Unit.MultiTable.NoSingleTableLookups).

namespace RbTableSubsystemPrivate
{
	// A live actor of World as TActorIterator filters by default: valid, no template, in one of the world's levels that is visible
	// (or being associated / disassociated).
	bool IsLiveActorOf(const AActor* Actor, const UWorld* World)
	{
		const ULevel* Level = (IsValid(Actor) && !Actor->IsTemplate()) ? Actor->GetLevel() : nullptr;
		return Level && Level->OwningWorld == World &&
			((Level->bIsVisible && !Level->bIsBeingRemoved) || Level->bIsAssociatingLevel || Level->bIsDisassociatingLevel);
	}

	// Visits every live actor of class T in World WITHOUT allocating (M2-E review): TActorIterator fills heap arrays on every use
	// (in the editor it copies every actor of the world and builds a set from them) and registers a spawn handler, while
	// GetPlayerTable / GetPlayerSession / FindTable / FindNearestTable run every frame for the player's context (overlay, key
	// hints), the audio LOD and the pick-up routing. Visit returns false to stop; it must not create or destroy UObjects (the
	// object hash is locked meanwhile). Iteration order: the object hash's (GetTables' stable sort and FindTable agree on it).
	template <typename T, typename FVisit>
	void ForEachLiveActor(const UWorld* World, FVisit&& Visit)
	{
		if (!World)
		{
			return;
		}
		bool bContinue = true;
		ForEachObjectOfClass(T::StaticClass(), [World, &Visit, &bContinue](UObject* Object)
		{
			T* Actor = static_cast<T*>(Object);
			if (bContinue && IsLiveActorOf(Actor, World))
			{
				bContinue = Visit(Actor);
			}
		}, true, RF_ClassDefaultObject | RF_ArchetypeObject, EInternalObjectFlags::Garbage);
	}
}

URbTableSubsystem* URbTableSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbTableSubsystem>() : nullptr;
}

TArray<ARbTable*> URbTableSubsystem::GetTables() const
{
	TArray<ARbTable*> Tables;
	RbTableSubsystemPrivate::ForEachLiveActor<ARbTable>(GetWorld(), [&Tables](ARbTable* Table)
	{
		Tables.Add(Table);
		return true;
	});
	Tables.StableSort([](const ARbTable& A, const ARbTable& B) { return A.TableIndex < B.TableIndex; });
	return Tables;
}

ARbTable* URbTableSubsystem::FindTable(int32 TableIndex) const
{
	// The first one in iteration order (GetTables' stable sort keeps it first among duplicates), without building the list.
	ARbTable* Found = nullptr;
	RbTableSubsystemPrivate::ForEachLiveActor<ARbTable>(GetWorld(), [TableIndex, &Found](ARbTable* Table)
	{
		Found = Table->TableIndex == TableIndex ? Table : nullptr;
		return Found == nullptr;
	});
	return Found;
}

ARbTable* URbTableSubsystem::GetPlayerTable() const
{
	// The first tagged table in TableIndex order, else the lowest index (ties: iteration order, like GetTables' stable sort) - one
	// pass without building / sorting a list: the player's context (overlay, key hints) asks every frame (M2-E review).
	ARbTable* Tagged = nullptr;
	ARbTable* Lowest = nullptr;
	RbTableSubsystemPrivate::ForEachLiveActor<ARbTable>(GetWorld(), [&Tagged, &Lowest](ARbTable* Table)
	{
		if (!Lowest || Table->TableIndex < Lowest->TableIndex)
		{
			Lowest = Table;
		}
		if (Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable) && (!Tagged || Table->TableIndex < Tagged->TableIndex))
		{
			Tagged = Table;
		}
		return true;
	});
	return Tagged ? Tagged : Lowest;
}

ARbTable* URbTableSubsystem::FindNearestTable(const FVector& WorldPoint) const
{
	ARbTable* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	RbTableSubsystemPrivate::ForEachLiveActor<ARbTable>(GetWorld(), [&WorldPoint, &Best, &BestDistance](ARbTable* Table)
	{
		double Distance = FVector::Dist(WorldPoint, Table->GetActorLocation());
		if (Table->HasContext())
		{
			// Distance to the outer boundary rectangle in the table frame (0 inside), in metres -> cm.
			const rb::TableSpec& Spec = Table->GetContext().Spec;
			const rb::Vec3 P = Table->WorldToCore(WorldPoint);
			const double HalfX = 0.5 * Spec.Length + Spec.RailWidthTotal;
			const double HalfY = 0.5 * Spec.Width + Spec.RailWidthTotal;
			const double Dx = FMath::Max(0.0, FMath::Abs(P.x) - HalfX);
			const double Dy = FMath::Max(0.0, FMath::Abs(P.y) - HalfY);
			Distance = 100.0 * FMath::Sqrt(Dx * Dx + Dy * Dy);
		}
		// Ties go to the lower TableIndex (the order GetTables lists them in).
		if (Distance < BestDistance || (Distance == BestDistance && Best && Table->TableIndex < Best->TableIndex))
		{
			BestDistance = Distance;
			Best = Table;
		}
		return true;
	});
	return Best;
}

ARbBallSet* URbTableSubsystem::FindBallSet(const ARbTable* Table) const
{
	if (!Table)
	{
		return nullptr;
	}
	if (const FRbTableSession* Session = FindSessionForTable(Table))
	{
		if (Session->BallSet.IsValid())
		{
			return Session->BallSet.Get();
		}
	}
	ARbBallSet* Found = nullptr;
	RbTableSubsystemPrivate::ForEachLiveActor<ARbBallSet>(GetWorld(), [Table, &Found](ARbBallSet* Balls)
	{
		Found = Balls->GetTable() == Table ? Balls : nullptr;
		return Found == nullptr;
	});
	return Found;
}

bool URbTableSubsystem::ValidateTables(FString& OutReport) const
{
	const TArray<ARbTable*> Tables = GetTables();
	bool bOk = Tables.Num() > 0;
	OutReport = FString::Printf(TEXT("tables: %d\n"), Tables.Num());
	if (Tables.IsEmpty())
	{
		OutReport += TEXT("FAIL no ARbTable in the level\n");
	}
	TSet<int32> Seen;
	for (const ARbTable* Table : Tables)
	{
		bool bDuplicate = false;
		Seen.Add(Table->TableIndex, &bDuplicate);
		if (bDuplicate || Table->TableIndex < 0)
		{
			bOk = false;
			OutReport += FString::Printf(TEXT("FAIL table %s: TableIndex %d duplicate or negative\n"), *Table->GetName(), Table->TableIndex);
		}
		else
		{
			OutReport += FString::Printf(TEXT("OK   table %s: TableIndex %d%s\n"), *Table->GetName(), Table->TableIndex,
				Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable) ? TEXT(" (player table)") : TEXT(""));
		}
	}
	const int32 Tagged = CountPlayerTableTags();
	if (Tagged > 1)
	{
		bOk = false;
		OutReport += FString::Printf(TEXT("FAIL %d tables tagged %s (at most one player table)\n"), Tagged, *RbAssetPaths::Tag::PlayerTable.ToString());
	}
	return bOk;
}

ARbBallSet* URbTableSubsystem::FindUnassignedBallSet() const
{
	ARbBallSet* Found = nullptr;
	RbTableSubsystemPrivate::ForEachLiveActor<ARbBallSet>(GetWorld(), [&Found](ARbBallSet* Balls)
	{
		Found = Balls->GetTable() == nullptr ? Balls : nullptr;
		return Found == nullptr;
	});
	return Found;
}

int32 URbTableSubsystem::CountPlayerTableTags() const
{
	int32 Count = 0;
	RbTableSubsystemPrivate::ForEachLiveActor<ARbTable>(GetWorld(), [&Count](const ARbTable* Table)
	{
		Count += Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable) ? 1 : 0;
		return true;
	});
	return Count;
}

const FRbTableSession* URbTableSubsystem::FindSessionForDirector(const URbMatchDirector* Director) const
{
	return Director ? Sessions.FindByPredicate([Director](const FRbTableSession& S) { return S.Director.Get() == Director; }) : nullptr;
}

URbMatchDirector* URbTableSubsystem::GetPlayerDirector() const
{
	const FRbTableSession* Session = GetPlayerSession();
	return Session ? Session->Director.Get() : nullptr;
}

ARbBallSet* URbTableSubsystem::GetPlayerBallSet() const
{
	const FRbTableSession* Session = GetPlayerSession();
	return Session ? Session->BallSet.Get() : nullptr;
}

void URbTableSubsystem::RegisterSession(const FRbTableSession& Session)
{
	Sessions.RemoveAll([&Session](const FRbTableSession& S) { return S.TableIndex == Session.TableIndex; });
	Sessions.Add(Session);
	OnSessionsChanged.Broadcast();
}

void URbTableSubsystem::UnregisterSession(int32 TableIndex)
{
	if (Sessions.RemoveAll([TableIndex](const FRbTableSession& S) { return S.TableIndex == TableIndex; }) > 0)
	{
		OnSessionsChanged.Broadcast();
	}
}

const FRbTableSession* URbTableSubsystem::FindSession(int32 TableIndex) const
{
	return Sessions.FindByPredicate([TableIndex](const FRbTableSession& S) { return S.TableIndex == TableIndex && S.IsValid(); });
}

const FRbTableSession* URbTableSubsystem::FindSessionForTable(const ARbTable* Table) const
{
	return Table ? Sessions.FindByPredicate([Table](const FRbTableSession& S) { return S.Table.Get() == Table; }) : nullptr;
}

const FRbTableSession* URbTableSubsystem::GetPlayerSession() const
{
	return FindSessionForTable(GetPlayerTable());
}
