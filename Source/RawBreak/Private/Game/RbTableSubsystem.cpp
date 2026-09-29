#include "Game/RbTableSubsystem.h"

#include "Balls/RbBallSet.h"
#include "Core/RbAssetPaths.h"
#include "Cue/RbCue.h"
#include "Game/RbMatchDirector.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

#include "Engine/World.h"
#include "EngineUtils.h"

// Owner: M2-E. Table queries (the architect step's) + sessions (registered by ARbGameMode). The ONLY file that iterates the
// world's tables, ball sets or cues (RawBreak.Unit.MultiTable.NoSingleTableLookups).

URbTableSubsystem* URbTableSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbTableSubsystem>() : nullptr;
}

TArray<ARbTable*> URbTableSubsystem::GetTables() const
{
	TArray<ARbTable*> Tables;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ARbTable> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Tables.Add(*It);
			}
		}
	}
	Tables.StableSort([](const ARbTable& A, const ARbTable& B) { return A.TableIndex < B.TableIndex; });
	return Tables;
}

ARbTable* URbTableSubsystem::FindTable(int32 TableIndex) const
{
	for (ARbTable* Table : GetTables())
	{
		if (Table->TableIndex == TableIndex)
		{
			return Table;
		}
	}
	return nullptr;
}

ARbTable* URbTableSubsystem::GetPlayerTable() const
{
	const TArray<ARbTable*> Tables = GetTables();
	for (ARbTable* Table : Tables)
	{
		if (Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable))
		{
			return Table;
		}
	}
	return Tables.Num() > 0 ? Tables[0] : nullptr;
}

ARbTable* URbTableSubsystem::FindNearestTable(const FVector& WorldPoint) const
{
	ARbTable* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (ARbTable* Table : GetTables())
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
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Table;
		}
	}
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
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ARbBallSet> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetTable() == Table)
			{
				return *It;
			}
		}
	}
	return nullptr;
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
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ARbBallSet> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetTable() == nullptr)
			{
				return *It;
			}
		}
	}
	return nullptr;
}

int32 URbTableSubsystem::CountPlayerTableTags() const
{
	int32 Count = 0;
	for (const ARbTable* Table : GetTables())
	{
		Count += Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable) ? 1 : 0;
	}
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
