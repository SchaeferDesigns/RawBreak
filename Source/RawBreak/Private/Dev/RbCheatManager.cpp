#include "Dev/RbCheatManager.h"

#include "RawBreak.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"

// Owner: UE-7. TODO(UE-7): implement every command through URbMatchDirector / URbReplaySubsystem /
// URbOverlayComponent; RbDumpState prints a stable, grep-able format ("RbState: phase=... shooter=... balls=...").

void URbCheatManager::RbStrike(float SpeedMps, float AzimuthDeg, float ElevationDeg, float OffsetA, float OffsetB)
{
	if (ARbGameMode* GameMode = ARbGameMode::Get(this))
	{
		if (URbMatchDirector* Director = GameMode->GetDirector())
		{
			Director->SubmitScriptedStrike(SpeedMps, FMath::DegreesToRadians(AzimuthDeg), FMath::DegreesToRadians(ElevationDeg), OffsetA, OffsetB);
		}
	}
}

void URbCheatManager::RbStroke(float /*SpeedMps*/, float /*AzimuthDeg*/) { /* TODO(UE-7) */ }
void URbCheatManager::RbPlaceCueBall(float /*X*/, float /*Y*/) { /* TODO(UE-7) */ }
void URbCheatManager::RbChoose(int32 /*OptionIndex*/) { /* TODO(UE-7) */ }
void URbCheatManager::RbRerack() { /* TODO(UE-7) */ }
void URbCheatManager::RbNewMatch(int32 /*Mode*/) { /* TODO(UE-7) */ }
void URbCheatManager::RbReplay(int32 /*View*/, float /*Rate*/) { /* TODO(UE-7) */ }
void URbCheatManager::RbOverlay(int32 /*Mode*/) { /* TODO(UE-7) */ }

void URbCheatManager::RbDumpState()
{
	UE_LOG(LogRawBreak, Display, TEXT("RbState: TODO(UE-7)"));
}
