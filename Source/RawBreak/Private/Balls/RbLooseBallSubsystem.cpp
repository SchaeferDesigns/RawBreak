#include "Balls/RbLooseBallSubsystem.h"

#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBall.h"
#include "Table/RbTable.h"

#include "Engine/World.h"

// Owner: M2-E. Stub of the M2 architect step: registry and return only; the playback binding, the physics hand-off, the
// pick-up provider (URbInteractionSubsystem) and the automatic returns are TODO(M2-E).

URbLooseBallSubsystem* URbLooseBallSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbLooseBallSubsystem>() : nullptr;
}

void URbLooseBallSubsystem::BindBallSet(ARbBallSet* BallSet)
{
	if (BallSet && !BoundBallSets.Contains(BallSet))
	{
		BoundBallSets.Add(BallSet);
		// TODO(M2-E): BallSet->GetPlayback()->OnShotEvent (BallOffTable, live shots only: FRbPlaybackClock::bLive).
	}
}

void URbLooseBallSubsystem::UnbindBallSet(ARbBallSet* BallSet)
{
	BoundBallSets.Remove(BallSet);
}

ARbLooseBall* URbLooseBallSubsystem::HandOff(ARbBallSet& /*Balls*/, int32 /*BallId*/, const rb::BallState& /*CoreState*/)
{
	return nullptr; // TODO(M2-E): hide the table ball, spawn ARbLooseBall, Launch with FRbCoords velocities.
}

ARbLooseBall* URbLooseBallSubsystem::FindLooseBall(int32 TableIndex, int32 BallId) const
{
	for (const TWeakObjectPtr<ARbLooseBall>& Weak : LooseBalls)
	{
		ARbLooseBall* Ball = Weak.Get();
		if (Ball && Ball->GetTableIndex() == TableIndex && Ball->GetBallId() == BallId)
		{
			return Ball;
		}
	}
	return nullptr;
}

TArray<ARbLooseBall*> URbLooseBallSubsystem::GetLooseBalls(int32 TableIndex) const
{
	TArray<ARbLooseBall*> Out;
	for (const TWeakObjectPtr<ARbLooseBall>& Weak : LooseBalls)
	{
		ARbLooseBall* Ball = Weak.Get();
		if (Ball && (TableIndex == INDEX_NONE || Ball->GetTableIndex() == TableIndex))
		{
			Out.Add(Ball);
		}
	}
	return Out;
}

bool URbLooseBallSubsystem::ReturnBall(int32 TableIndex, int32 BallId)
{
	ARbLooseBall* Ball = FindLooseBall(TableIndex, BallId);
	if (!Ball)
	{
		return false;
	}
	LooseBalls.Remove(Ball);
	Ball->Destroy();
	// TODO(M2-E): show the table instance again (ARbBallSet::SetBallVisible) when the committed table state has it in play.
	OnReturned.Broadcast(TableIndex, BallId);
	return true;
}

int32 URbLooseBallSubsystem::ReturnAll(int32 TableIndex)
{
	int32 Count = 0;
	for (ARbLooseBall* Ball : GetLooseBalls(TableIndex))
	{
		Count += ReturnBall(Ball->GetTableIndex(), Ball->GetBallId()) ? 1 : 0;
	}
	return Count;
}

void URbLooseBallSubsystem::Deinitialize()
{
	LooseBalls.Reset();
	BoundBallSets.Reset();
	Super::Deinitialize();
}
