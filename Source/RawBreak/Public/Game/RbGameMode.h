#pragma once

// Game mode of the playable table room (Docs/ue-architecture.md 5.1, 7). Finds the level's ARbTable (or spawns one
// at the origin), spawns ARbBallSet and ARbCue, creates the URbMatchDirector and starts the match. URL options
// (also usable from the command line after the map name):
//   ?Mode=Practice|HotSeat  ?Game=NineBall|EightBall|TenBall|StraightPool  ?Race=5  ?Lag=0|1  ?Seed=<n>
// Default pawn ARbPlayerCharacter, controller ARbPlayerController. Owner: UE-6b.

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "Game/RbMatchDirector.h"

#include "RbGameMode.generated.h"

class ARbBallSet;
class ARbCue;
class ARbTable;

UCLASS()
class RAWBREAK_API ARbGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARbGameMode();

	static ARbGameMode* Get(const UObject* WorldContext);

	URbMatchDirector* GetDirector() const { return Director; }
	ARbTable* GetTable() const { return Table; }
	ARbBallSet* GetBallSet() const { return BallSet; }
	ARbCue* GetCue() const { return Cue; }

	// Match started in StartPlay (options above override these defaults).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match")
	FRbMatchSetup DefaultSetup;

	// AGameModeBase
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;

protected:
	// Finds or spawns the scene actors and wires pawn / cue / director.
	void SetupScene();

	UPROPERTY(Transient) TObjectPtr<URbMatchDirector> Director;
	UPROPERTY(Transient) TObjectPtr<ARbTable> Table;
	UPROPERTY(Transient) TObjectPtr<ARbBallSet> BallSet;
	UPROPERTY(Transient) TObjectPtr<ARbCue> Cue;

	FRbMatchSetup Setup;
};
