#include "Game/RbGameMode.h"

#include "Balls/RbBallSet.h"
#include "Cue/RbCue.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

// Owner: UE-6b. TODO(UE-6b): option parsing, scene setup (find / spawn table, ball set, cue; wire the pawn's stroke
// component, the playback cue and the director), start the match; the M1 flow test RawBreak.Functional.M1Flow belongs to UE-8.

ARbGameMode::ARbGameMode()
{
	DefaultPawnClass = ARbPlayerCharacter::StaticClass();
	PlayerControllerClass = ARbPlayerController::StaticClass();
}

ARbGameMode* ARbGameMode::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
}

void ARbGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	Setup = DefaultSetup;
	// TODO(UE-6b): UGameplayStatics::ParseOption(Options, TEXT("Mode")) etc.
}

void ARbGameMode::StartPlay()
{
	Super::StartPlay();
	SetupScene();
	if (Director)
	{
		Director->StartMatch(Setup);
	}
}

void ARbGameMode::SetupScene()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ARbTable> It(World); It; ++It)
	{
		Table = *It;
		break;
	}
	// TODO(UE-6b): spawn the table if missing, the ball set and the cue; wire the pawn / playback.
	Director = NewObject<URbMatchDirector>(this, TEXT("MatchDirector"));
	Director->Initialize(Table, BallSet, Cue, World ? World->GetSubsystem<URbSimulationSubsystem>() : nullptr);
}
