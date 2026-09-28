#pragma once

// Bridge between the game and rb::rules + rb::human (Docs/ue-architecture.md 5.2, 7; architecture.md 13 items 4, 8,
// 11). Owns the match (MatchConfig / MatchState), the authoritative FRbTableState and the shooters, and runs the
// shot pipeline:
//
//   rack (SetupRack -> FRbTableState -> ARbBallSet) -> AwaitShot: GetShotConstraints -> UI / stroke component
//   ball in hand: PlaceCueBall (CueBallPlacementLegal / ValidateDeclaration with the placed position)
//   stroke contact (FRbStrokeCommit) -> rb::human::ExecuteStroke (attributes, situation, TipState, CueBodyState,
//     other balls, NoiseKey + NoiseHistory) -> SimInput (FRbTableState, strikes, ShotContext) -> URbSimulationSubsystem
//   OnShotSimulated -> live playback (ARbBallSet) + rules on the game thread: DeriveShotFacts(Result.Record) ->
//     EvaluateShot -> ApplyShot (committed when the playback finishes, so the UI never spoils the outcome)
//   after the shot: ApplyShotToEquipment, AdvanceNoiseHistory, FRbTableState from Result.Finals, spotting,
//     next turn / decision (ApplyOption) / rack over (new rack) / match over
//
// Practice = one human shooting every turn with the rules running; HotSeat = two humans alternating (the same pawn).
// Lag optional (two strikes in one SimInput, EvaluateLag). M1 runs rules InputMode::Assisted (no F10 / 3.4 / 3.10 fouls
// from the body-less player; illegal placements are refused by ValidateDeclaration, rules.md 16 item 22). Owner: UE-6b.
//
// Review additions (Docs/ue-architecture.md 16):
//   * The ball set's playback component is shared by live shots and replays: OnPlaybackFinished commits ONLY the
//     director's own PendingShot in phase PlayingBack (a finished replay must never commit a shot twice, R-07).
//   * SetLivePlaybackRate(0) commits a shot as soon as it is simulated (headless tests without a ball set or real-time
//     waiting; a 9-ball break plays 11 s). Tests of UE-6b therefore never depend on UE-2's playback (R-15).
//   * MakeStrokeContext / OnStrokeAborted: the SampleHand contract of URbStrokeComponent (what you see is what hits, R-04).

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Core/RbTypes.h"
#include "Game/RbShooterState.h"
#include "Player/RbStrokeComponent.h"
#include "Simulation/RbShot.h"

#include "rb/Rules/Match.h"
#include "rb/Rules/ShotFacts.h"

#include "RbMatchDirector.generated.h"

class ARbBallSet;
class ARbCue;
class ARbTable;
class URbSimulationSubsystem;

// Game-side phase (finer than rb::rules::MatchPhase: the simulation / playback / placement steps).
UENUM(BlueprintType)
enum class ERbDirectorPhase : uint8
{
	Idle,            // no match
	Lag,             // both players lag (optional)
	AwaitStroke,     // shooter may address the ball (cue ball in position)
	AwaitPlacement,  // ball in hand: shooter places the cue ball first
	Simulating,      // stroke submitted, waiting for the worker
	PlayingBack,     // live playback of the shot
	AwaitDecision,   // a player chooses an option (accept table, push-out hand back, re-rack, ...)
	RackOver,        // short pause, then the next rack
	MatchOver,
};

USTRUCT(BlueprintType)
struct FRbMatchSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") ERbMatchMode Mode = ERbMatchMode::Practice;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") ERbDiscipline Discipline = ERbDiscipline::NineBall;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") int32 RaceTo = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") bool bLag = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") int64 Seed = 0;  // 0 = random at start (stored for replays)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") FString Player1 = TEXT("Player 1");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") FString Player2 = TEXT("Player 2");
};

// What the overlay shows about the last shot (plain data, filled after rules evaluation).
struct FRbLastShotSummary
{
	bool bValid = false;
	int32 Shooter = -1;
	rb::rules::FoulSet Fouls;
	rb::rules::Foul Enforced = rb::rules::Foul::Count;
	FString RuleRef;
	TArray<int32> Pocketed;          // ball ids in pocketing order
	int32 FirstContactBall = -1;
	double CueSpeed = 0.0;           // executed tip speed [m/s]
	double SimMilliseconds = 0.0;
	bool bPredictedMiscue = false;   // from the human layer
	bool bMiscue = false;            // from the physics
};

DECLARE_MULTICAST_DELEGATE(FRbOnMatchChanged);

UCLASS()
class RAWBREAK_API URbMatchDirector : public UObject
{
	GENERATED_BODY()

public:
	// Wires the director to the scene (all four required) and subscribes to the simulation / playback events.
	void Initialize(ARbTable* InTable, ARbBallSet* InBalls, ARbCue* InCue, URbSimulationSubsystem* InSimulation);

	// Starts a match (Practice or HotSeat, M1: 9-ball) and racks the first rack.
	bool StartMatch(const FRbMatchSetup& Setup);

	// --- state for UI / pawn / tests ------------------------------------------------------------------
	ERbDirectorPhase GetPhase() const { return Phase; }
	const FRbMatchSetup& GetSetup() const { return Setup; }
	const rb::rules::MatchConfig& GetMatchConfig() const { return Config; }
	const rb::rules::MatchState& GetMatchState() const { return State; }
	rb::rules::ShotConstraints GetConstraints() const;
	int32 GetActivePlayer() const;
	const FRbShooterState& GetShooter(int32 Player) const { return Shooters[FMath::Clamp(Player, 0, 1)]; }
	const FRbTableState& GetTableState() const { return TableState; }
	const FRbLastShotSummary& GetLastShot() const { return LastShot; }

	// --- player actions -------------------------------------------------------------------------------
	// Ball in hand: legality check and placement (table-frame plan position of the cue-ball centre).
	bool CanPlaceCueBall(const rb::Vec2& Position) const;
	bool PlaceCueBall(const rb::Vec2& Position);

	// Declaration for the next shot (calls where the discipline requires them; push-out / safety).
	void SetCalledShot(int32 Ball, int32 Pocket);
	void SetShotKind(rb::rules::ShotKind Kind);

	// The stroke reached the ball: execute (human layer), build the SimInput, submit. False if not allowed now.
	bool SubmitStroke(const FRbStrokeCommit& Commit);

	// Scripted strike without the human layer (cheats / tests): V [m/s], phi, theta [rad], contact offsets a, b.
	bool SubmitScriptedStrike(double Speed, double Azimuth, double Elevation, double OffsetA, double OffsetB);

	// AwaitDecision: the deciding player picks one of GetMatchState().PendingOutcome.Options.
	bool ChooseOption(rb::rules::Option Choice);

	// Test / cheat support: replaces the ball layout (core states), e.g. "9-ball alone on the table".
	void SetTableStateForTest(const FRbTableState& NewState);

	// Live playback rate (1 = real time). 0 = no playback: commit right after the simulation (tests, RbFastForward).
	void SetLivePlaybackRate(float Rate) { LivePlaybackRate = FMath::Max(0.0f, Rate); }
	float GetLivePlaybackRate() const { return LivePlaybackRate; }

	// SampleHand / ExecuteStroke inputs of the active shooter for the current shot (URbStrokeComponent::SetStrokeContext).
	FRbStrokeContext MakeStrokeContext() const;

	// A stroke ended without contact; bRampShown = it showed per-shot draws, which are spent (ShooterShotIndex++,
	// AdvanceNoiseHistory, HF-B13), then the context is pushed again.
	void OnStrokeAborted(bool bRampShown);

	// Replays may start only in AwaitStroke / AwaitPlacement / AwaitDecision / RackOver / MatchOver (not while a shot is
	// simulating or playing back); the replay subsystem locks the stroke component through the director.
	bool IsReplayAllowed() const;

	FRbOnMatchChanged OnMatchChanged;

protected:
	void RackNext();
	void BeginTurn();
	void OnShotSimulated(const TSharedRef<const FRbShot>& Shot);
	void OnPlaybackFinished(const TSharedRef<const FRbShot>& Shot);
	void CommitShot(const TSharedRef<const FRbShot>& Shot);
	void SetPhase(ERbDirectorPhase NewPhase);

private:
	ERbDirectorPhase Phase = ERbDirectorPhase::Idle;
	FRbMatchSetup Setup;
	rb::rules::MatchConfig Config;
	rb::rules::MatchState State;
	rb::rules::ShotDeclaration Declaration;
	FRbTableState TableState;
	FRbShooterState Shooters[2];
	FRbLastShotSummary LastShot;
	uint32 MatchShotIndex = 0;
	float LivePlaybackRate = 1.0f;
	TSharedPtr<const FRbShot> PendingShot;       // simulated, playing back, not yet committed
	rb::rules::ShotFacts PendingFacts;
	rb::rules::ShotOutcome PendingOutcome;

	TWeakObjectPtr<ARbTable> Table;
	TWeakObjectPtr<ARbBallSet> Balls;
	TWeakObjectPtr<ARbCue> Cue;
	TWeakObjectPtr<URbSimulationSubsystem> Simulation;
	FDelegateHandle SimulatedHandle;
	FDelegateHandle PlaybackHandle;
};
