#pragma once

// Game mode of the playable table room (Docs/ue-architecture.md 5.1, 7). Finds the level's ARbTable (or spawns one
// at the origin), spawns ARbBallSet and ARbCue, creates the URbMatchDirector and starts the match. URL options
// (also usable from the command line after the map name):
//   ?Mode=Practice|HotSeat  ?Game=NineBall|EightBall|TenBall|StraightPool  ?Race=5  ?Lag=0|1  ?Seed=<n>
//   ?Attr=<0..100> (both shooters' attributes; default the neutral guest profile 50)  ?Pressure=0|1  ?Noise=<NoiseScale>
//   ?Rate=<live playback rate, 0 = commit at once>  ?P1=<name>  ?P2=<name>
// Default pawn ARbPlayerCharacter, controller ARbPlayerController. Owner: UE-6b.
//
// Wiring (5.1): the pawn's stroke component gets the table and the cue and is handed to the director (also for pawns that
// are restarted later, FinishRestartPlayer); the ball set's playback component drives the cue after contact.
// Dev tools (non-shipping): console variable rb.Match.DrawTableState 1 draws the director's FRbTableState, the playing
// surface, strings and pocket openings as debug lines; console commands rb.Match.Place <x> <y>, rb.Match.Strike <V> <phi deg>
// [<theta deg> <a> <b>], rb.Match.StrikeAt <V> <x> <y> [<b>], rb.Match.Break [<V>] (places the cue ball behind the head string
// and breaks at the apex ball), rb.Match.Layout <id> <x> <y> ..., rb.Match.Rate <r>, rb.Match.Confirm and rb.Match.Dump drive
// the director of the running game (headless captures). The official cheat interface stays URbCheatManager (UE-7).

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

	// Live playback rate the director starts with (?Rate=; 0 = commit right after the simulation).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match")
	float DefaultPlaybackRate = 1.0f;

	// Applies the URL options above to InOut (unknown keys are ignored). False if a recognised option had an invalid value
	// (that option is left unchanged). OutPlaybackRate receives ?Rate= when present.
	static bool ParseMatchOptions(const FString& Options, FRbMatchSetup& InOut, float* OutPlaybackRate = nullptr);

	// The setup parsed in InitGame (what StartPlay started).
	const FRbMatchSetup& GetStartSetup() const { return Setup; }

	// AGameModeBase
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	// Finds or spawns the scene actors and wires pawn / cue / director.
	void SetupScene();

	// Hands a (re)spawned player pawn's stroke component to the table, the cue and the director.
	void WirePawn(APawn* Pawn);

	// Debug lines of the director's table state (rb.Match.DrawTableState).
	void DrawTableStateDebug() const;

	virtual void FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation) override;

	UPROPERTY(Transient) TObjectPtr<URbMatchDirector> Director;
	UPROPERTY(Transient) TObjectPtr<ARbTable> Table;
	UPROPERTY(Transient) TObjectPtr<ARbBallSet> BallSet;
	UPROPERTY(Transient) TObjectPtr<ARbCue> Cue;

	FRbMatchSetup Setup;
	float PlaybackRate = 1.0f;
};
