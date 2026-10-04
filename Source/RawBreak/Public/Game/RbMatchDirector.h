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
//   after the shot: ApplyShotToEquipment, AdvanceNoiseHistory, ApplyShot (spotting), then ONE sync of FRbTableState from
//     MatchState.Game.Balls (status + plan position) and Result.Finals (orientation), R-12; next turn / decision
//     (ApplyOption) / rack over (Confirm: new rack) / match over (Confirm: new match), R-20
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
//
// Implementation notes (UE-6b):
//   * Practice: the rules still alternate their two players (a foul hands "the opponent" ball in hand), but both rule
//     players are the same human: every player-model operation (attributes, tip, noise stream) uses shooter slot 0
//     (GetShooter(0) == GetShooter(1)); RaceTo is unbounded, so a won rack racks again after Confirm.
//   * Auto-chalk (HF 4.1 A mode, [DD-CHALK]): PerformChalking with the shooter's cube and habit before EVERY shot, so every
//     visit starts chalked.
//   * Scripted strikes (cheats, tests) bypass the human layer: they reveal no per-shot draws and change no equipment.
//   * Without a URbSimulationSubsystem (world-free unit tests) the director simulates on the game thread with
//     URbSimulationSubsystem::RunShotBlocking and hands the shot to itself - the same simulation code, no worker.
//   * Lag (hot-seat, ?Lag=1): player 0 lags ball 0 (the cue ball), player 1 ball 1 from rb::rules::LagStartPositions;
//     the director collects both executed strokes and simulates ONE SimInput with two strikes; the lag winner breaks.
//   * ExecuteStroke uses MakeStrokeContext() + the commit's AddressIndex + the stroke component's aim ElevationFloor (what it
//     rendered with); FloorBy / FloorBall are not in FRbStrokeCommit yet (they only feed the F2 shaft-contact list).
//   * With a synchronous simulation (no subsystem) a stroke is committed and the next turn armed INSIDE the stroke
//     component's OnStrokeContact broadcast: the component must enter Watching before it broadcasts.
//
// Review fixes (UE-6b review):
//   * A human stroke that reached the ball but produced no committed shot (ExecuteStroke error, declaration refused, the
//     simulation service busy, InvalidInput from the simulator) showed the whole per-shot ramp: its draws are spent like an
//     aborted stroke with the ramp (HF-B13), so a retry never replays the draws the player has seen.
//   * The stroke component that made the contact stays in Contact / Watching while the shot simulates and plays back (6.2:
//     Contact -> Watching, GetDown stands up); the director only locks a component in any other phase (scripted strikes).
//   * SetShotKind / SetCalledShot refuse a declaration the rules would refuse (ValidateDeclaration without the placement), so a
//     bad declaration can never block every stroke of a turn.
//   * Calls: practice and hot-seat are casual, so the Explicit call mode of the WPA presets (8-ball, 10-ball, 14.1) becomes the
//     casual default ObviousAssist (rules.md 4.5); without it every shot after the break would need a call M1 cannot enter.
//   * Pressure hill (HF-15): either player needs one rack, also in a race to 1 (never in 14.1, which counts points).
//   * SetLivePlaybackRate applies to the live shot that is playing (0 = snap to its end and commit now).

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Core/RbTypes.h"
#include "Game/RbShooterState.h"
#include "Player/RbStrokeComponent.h"
#include "Simulation/RbShot.h"

#include "rb/Rules/Lag.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/ShotFacts.h"

#include "RbMatchDirector.generated.h"

class ARbBallSet;
class ARbCue;
class ARbTable;
class URbShotPlaybackComponent;
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
	// Every execution attribute of both shooters [0, 100]; < 0 = the neutral guest profile (HotSeatGuestAttributes, 50). ?Attr=
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") double ShooterAttribute = -1.0;
	// HF-15 pressure (PressureMode On / Off); hot-seat may switch it off for both. ?Pressure=0
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") bool bPressure = true;
	// HumanParams::NoiseScale (Imperfections slider Sim 1 / Scaled 0.6 / Low 0.3 / Off 0). ?Noise=
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") double NoiseScale = 1.0;
	// --- M3 (Docs/ue-architecture.md 19.3) ---
	// The AI opponent of a VsAi match (rules player 1). ?Opponent=
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") ERbAiProfile Opponent = ERbAiProfile::BarRegular;
	// How shots are called (M3-G's RbMatchRules::MakeMatchConfig maps it onto the rules' CallMode). ?Calls=
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Match") ERbCallPolicy Calls = ERbCallPolicy::Casual;
};

// M3 (19.3): who shoots for a rules player.
UENUM(BlueprintType)
enum class ERbShooterKind : uint8
{
	Human,
	Ai,
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
	bool bLag = false;               // the shot was the lag (LagOutcome in LagWinner)
	int32 LagWinner = -1;            // 0 / 1, -1 = re-lag
	rb::rules::NextAction Next = rb::rules::NextAction::Pass;
};

DECLARE_MULTICAST_DELEGATE(FRbOnMatchChanged);
// M3 (19.3): a declaration (call, push-out, safety) of the current shot was accepted (SetCalledShot / SetShotKind): the opponent
// acknowledges (M3-O), the overlay shows it (M3-G).
DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnDeclarationChanged, int32 /*RulesPlayer*/, const rb::rules::ShotDeclaration& /*Declaration*/);

UCLASS()
class RAWBREAK_API URbMatchDirector : public UObject
{
	GENERATED_BODY()

public:
	// Wires the director to the scene (all four required) and subscribes to the simulation / playback events.
	// Null actors are tolerated (world-free tests): no presentation, and without InSimulation the shot runs on the
	// game thread (URbSimulationSubsystem::RunShotBlocking).
	void Initialize(ARbTable* InTable, ARbBallSet* InBalls, ARbCue* InCue, URbSimulationSubsystem* InSimulation);

	// Uses this table context instead of the table actor's (world-free tests, tools). Read at StartMatch.
	void SetTableContext(TSharedPtr<const FRbTableContext> InContext) { ContextOverride = InContext; }

	// The pawn's stroke component (one for both hot-seat players): the director pushes BeginAddress / placement / lock /
	// FRbStrokeContext to it and listens to OnStrokeContact, OnCueBallPlaced and OnStrokeAborted. Null unbinds.
	void SetStrokeComponent(URbStrokeComponent* InStroke);
	URbStrokeComponent* GetStrokeComponent() const { return StrokeComponent.Get(); }

	// Unsubscribes from every event source (game mode EndPlay).
	void Shutdown();

	// Starts a match (Practice or HotSeat, M1: 9-ball) and racks the first rack.
	bool StartMatch(const FRbMatchSetup& Setup);

	// --- state for UI / pawn / tests ------------------------------------------------------------------
	ERbDirectorPhase GetPhase() const { return Phase; }
	const FRbMatchSetup& GetSetup() const { return Setup; }
	const rb::rules::MatchConfig& GetMatchConfig() const { return Config; }
	const rb::rules::MatchState& GetMatchState() const { return State; }
	rb::rules::ShotConstraints GetConstraints() const;
	int32 GetActivePlayer() const;
	// Player-model state of rules player 0 / 1 (practice: both are the one human, slot 0).
	const FRbShooterState& GetShooter(int32 Player) const { return Shooters[ShooterSlot(Player)]; }
	const FRbTableState& GetTableState() const { return TableState; }
	const FRbLastShotSummary& GetLastShot() const { return LastShot; }
	TSharedPtr<const FRbTableContext> GetTableContext() const { return TableContext; }
	TSharedPtr<const FRbShot> GetPendingShot() const { return PendingShot; }
	// The last committed shot (request incl. the human-layer record, result): debug block, tests.
	TSharedPtr<const FRbShot> GetLastCommittedShot() const { return LastCommittedShot; }
	const rb::rules::ShotDeclaration& GetDeclaration() const { return Declaration; }
	const rb::human::HumanParams& GetHumanParams() const { return HumanParams; }
	uint64 GetMatchSeed() const { return MatchSeed; }
	uint32 GetMatchShotIndex() const { return MatchShotIndex; }
	// Ball in hand for the current shot (AwaitPlacement, or AwaitStroke after the placement), the placed position.
	bool IsCueBallInHand() const;
	bool IsCueBallPlaced() const { return bCueBallPlaced; }
	rb::Vec2 GetPlacedCueBall() const { return PlacedCueBall; }
	// Lag: whose lag stroke is next (0 / 1), -1 outside the lag.
	int32 GetLagStroker() const { return Phase == ERbDirectorPhase::Lag ? LagStroker : -1; }
	// Index into GetMatchState().PendingOutcome.Options of the highlighted decision option (AwaitDecision).
	int32 GetSelectedOption() const { return SelectedOption; }
	// Last refused action / error for logs and the debug block.
	const FString& GetLastError() const { return LastError; }

	// --- player actions -------------------------------------------------------------------------------
	// Ball in hand: legality check and placement (table-frame plan position of the cue-ball centre).
	bool CanPlaceCueBall(const rb::Vec2& Position) const;
	bool PlaceCueBall(const rb::Vec2& Position);

	// Declaration for the next shot (calls where the discipline requires them; push-out / safety). Only while the shooter
	// places / addresses (AwaitPlacement / AwaitStroke, no replay); a declaration ValidateDeclaration would refuse (push-out out
	// of its window, safety in 9-ball, a call of a ball that is not on the table, ...) is refused and the previous one kept
	// (GetLastError). SetCalledShot(-1, -1) clears the call. The declaration resets to the neutral one at every new shot.
	void SetCalledShot(int32 Ball, int32 Pocket);
	void SetShotKind(rb::rules::ShotKind Kind);

	// The stroke reached the ball: execute (human layer), build the SimInput, submit. False if not allowed now.
	bool SubmitStroke(const FRbStrokeCommit& Commit);

	// Scripted strike without the human layer (cheats / tests): V [m/s], phi, theta [rad], contact offsets a, b.
	bool SubmitScriptedStrike(double Speed, double Azimuth, double Elevation, double OffsetA, double OffsetB);

	// AwaitDecision: the deciding player picks one of GetMatchState().PendingOutcome.Options.
	bool ChooseOption(rb::rules::Option Choice);

	// Confirm input (R-20): AwaitDecision -> the highlighted option; RackOver -> next rack; MatchOver -> new match with the
	// same setup (a random seed is drawn again when the setup's seed is 0). False if Confirm means nothing now.
	bool Confirm();

	// CycleOption input (Q / E): moves the highlighted decision option by Direction (wraps). False outside AwaitDecision.
	bool CycleOption(int32 Direction);

	// Re-rack the current rack (stalemate R 1.10 / 5.9: the rack's breaker breaks again, no score; RbRerack cheat).
	bool RequestRerack();

	// Test / cheat support: replaces the ball layout (core states), e.g. "9-ball alone on the table". The rules' GameState
	// follows: balls in play are OnTable at their plan position, racked balls not in play are Pocketed; the layout is a
	// mid-rack position (no break shot, no push-out); a cue ball in play is in position, one out of play is in hand
	// anywhere. The current turn is re-armed (AwaitStroke / AwaitPlacement). Only in AwaitStroke / AwaitPlacement.
	void SetTableStateForTest(const FRbTableState& NewState);

	// Live playback rate (1 = real time). 0 = no playback: commit right after the simulation (tests, RbPlaybackRate cheat,
	// ?Rate=0, rb.Match.Rate 0). While a live shot plays back the new rate applies to it at once (the playback re-anchors its
	// clock); 0 then snaps it to its end and commits it. A non-finite rate is ignored.
	void SetLivePlaybackRate(float Rate);
	float GetLivePlaybackRate() const { return LivePlaybackRate; }

	// SampleHand / ExecuteStroke inputs of the active shooter for the current shot (URbStrokeComponent::SetStrokeContext).
	FRbStrokeContext MakeStrokeContext() const;

	// A stroke ended without contact; bRampShown = it showed per-shot draws, which are spent (ShooterShotIndex++,
	// AdvanceNoiseHistory, HF-B13), then the context is pushed again.
	void OnStrokeAborted(bool bRampShown);

	// Replays may start only in AwaitStroke / AwaitPlacement / AwaitDecision / RackOver / MatchOver (not while a shot is
	// simulating or playing back); the replay subsystem locks the stroke component through the director.
	bool IsReplayAllowed() const;

	// Replay subsystem: true locks the stroke component; false shows FRbTableState again and re-arms the stroke component
	// for the current phase (without resetting its address count).
	void SetReplayActive(bool bActive);
	bool IsReplayActive() const { return bReplayActive; }

	FRbOnMatchChanged OnMatchChanged;

	// --- additions (M2-E: several tables per level, Docs/ue-architecture.md 18.6.2) -----------------------------------------

	// Committed shots go into URbReplaySubsystem's history (default on; ARbGameMode switches it off for every session but the
	// player's: the replay history is the player's table only in M2).
	void SetRecordsReplays(bool bRecord) { bRecordReplays = bRecord; }
	bool RecordsReplays() const { return bRecordReplays; }

	// The scene this director was initialised with (its table session).
	ARbTable* GetTable() const { return Table.Get(); }
	ARbBallSet* GetBallSet() const { return Balls.Get(); }
	ARbCue* GetCue() const { return Cue.Get(); }

	// --- M3 additions (Docs/ue-architecture.md 19.3 / 19.5; owner M3-O; FROZEN for M3-G and M3-H, additions allowed) --------------

	// Who shoots for a rules player: VsAi = rules player 1 is the AI (FRbMatchSetup::Opponent), everything else human.
	ERbShooterKind GetShooterKind(int32 Player) const;
	// The AI must act now (shoot, place, decide an option, lag, choose the breaker, request the spot).
	bool IsAiToAct() const;
	// SampleHand / ExecuteStroke inputs of a rules player (the AI's own attributes, equipment and noise stream).
	FRbStrokeContext MakeStrokeContextFor(int32 Player) const;
	// Rules 4.4 (ShotConstraints::MayRequestSpot): rb::rules::RequestSpot, sync, re-arm the turn. The human's Q (M3-G), the AI's
	// DecisionKind::RequestSpot (M3-O). False if not allowed now.
	bool RequestSpot();
	// MatchPhase::LagWinnerChooses: the lag winner names the breaker (rules player 0 / 1). False if not in that phase.
	bool ChooseBreaker(int32 Breaker);
	// One visible chalking of a rules player's tip (the body's twists, HF-22): Twists twists with the sweep quality [0, 1] (the habit
	// for automatic chalking). Returns the twists applied (0 if refused). The tip state changes only through this while a body chalks.
	int32 ChalkTip(int32 Player, int32 Twists, double Sweep);
	// true (default, M1 / M2 behaviour): PerformChalking at the start of every visit, instantly. A body that chalks visibly switches
	// it off (M3-H for the player, M3-O for the opponent).
	void SetInstantAutoChalk(bool bInstant) { bInstantAutoChalk = bInstant; }
	bool IsInstantAutoChalk() const { return bInstantAutoChalk; }

	FRbOnDeclarationChanged OnDeclarationChanged;

	// UObject
	virtual UWorld* GetWorld() const override;

protected:
	void RackNext();
	void BeginTurn();
	void OnShotSimulated(const TSharedRef<const FRbShot>& Shot);
	void OnPlaybackFinished(const TSharedRef<const FRbShot>& Shot);
	void CommitShot(const TSharedRef<const FRbShot>& Shot);
	void SetPhase(ERbDirectorPhase NewPhase);

private:
	// Shooter slot of a rules player (practice: always 0).
	int32 ShooterSlot(int32 Player) const;
	FRbShooterState& ActiveShooterState();
	const FRbShooterState& ActiveShooterState() const;
	int32 ActiveRulesPlayer() const;

	void InitShooters();
	rb::human::NoiseKey MakeNoiseKey(const FRbShooterState& Shooter) const;
	rb::human::StrokeSituation MakeSituation() const;
	rb::rules::ShotDeclaration NeutralDeclaration() const;
	// ValidateDeclaration of D for the current shot without the cue-ball placement (PlaceCueBall checks that).
	bool IsDeclarationAllowed(const rb::rules::ShotDeclaration& D) const;
	bool CanDeclareNow() const;
	rb::rules::CueBallNext PlacementRegion() const;
	bool CanShootNow() const;
	// Enters the director phase of the rules' MatchPhase after a commit / decision / re-rack (lag skipped unless enabled).
	void EnterRulesPhase();

	// The ONE sync of FRbTableState from the rules' GameState (+ the shot's finals / chalk marks), review R-12.
	void SyncTableState(const FRbShot* Shot, const rb::BallChalkMarks* Marks);
	void SetupLagTable();
	int32 LagBall(int32 Player) const { return Player == 0 ? 0 : 1; }
	rb::Vec3 StruckBallPosition() const;
	int32 StruckBallId() const { return Phase == ERbDirectorPhase::Lag ? LagBall(LagStroker) : 0; }

	// Human or scripted strike for the current shot (lag: for the current lag stroker). Record.bHuman selects the path.
	bool SubmitStrike(const rb::CueStrikeInput& Strike, FRbStrokeRecord&& Record, double ContactTime, const FTransform& ShooterView);
	bool SubmitRequest(FRbShotRequest&& Request);
	void BuildShotInput(FRbShotRequest& Request) const;
	void SpendRevealedDraws(FRbShooterState& Shooter);
	// A human stroke of rules player Player reached the ball but gave no committed shot: spend its draws (HF-B13) and push
	// the new context to the stroke component.
	void SpendRejectedStroke(int32 Player, bool bHuman);
	void ApplyEquipment(const FRbShot& Shot, rb::BallChalkMarks* Marks);
	void CommitLag(const TSharedRef<const FRbShot>& Shot, rb::BallChalkMarks* Marks);

	// Presentation / input wiring.
	void ShowTableState();
	void ArmStrokeForPhase(bool bNewTurn);
	void LockStroke();
	void AutoChalk(FRbShooterState& Shooter);
	void Refuse(const FString& Why);

	void HandleStrokeContact(const FRbStrokeCommit& Commit);
	void HandleCueBallPlaced(const FVector& WorldPosition);
	void HandleStrokeAborted(bool bRampShown);

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
	TSharedPtr<const FRbShot> LastCommittedShot;
	rb::rules::ShotFacts PendingFacts;
	rb::rules::ShotOutcome PendingOutcome;

	TSharedPtr<const FRbTableContext> TableContext;
	TSharedPtr<const FRbTableContext> ContextOverride;
	rb::human::HumanParams HumanParams;
	uint64 MatchSeed = 0;
	bool bRandomSeed = false;
	bool bCueBallPlaced = false;
	rb::Vec2 PlacedCueBall;
	rb::rules::ShotDeclaration PendingDeclaration; // the declaration the pending shot was validated with
	FRbLastShotSummary PendingSummary;
	bool bAwaitingSimulation = false;
	bool bPendingIsLag = false;
	uint32 SubmittedShotId = 0;
	uint32 LocalShotId = 0;                        // ids of game-thread shots without a subsystem
	int32 SelectedOption = 0;
	bool bReplayActive = false;
	bool bRecordReplays = true;
	bool bInstantAutoChalk = true; // M3 (19.3)
	FString LastError;

	// Lag
	int32 LagStroker = 0;
	rb::CueStrikeInput LagStrikes[2];
	FRbStrokeRecord LagRecords[2];
	rb::rules::LagResult PendingLag;

	TWeakObjectPtr<ARbTable> Table;
	TWeakObjectPtr<ARbBallSet> Balls;
	TWeakObjectPtr<ARbCue> Cue;
	TWeakObjectPtr<URbSimulationSubsystem> Simulation;
	TWeakObjectPtr<URbShotPlaybackComponent> Playback;
	TWeakObjectPtr<URbStrokeComponent> StrokeComponent;
	FDelegateHandle SimulatedHandle;
	FDelegateHandle PlaybackHandle;
	FDelegateHandle ContactHandle;
	FDelegateHandle PlacedHandle;
	FDelegateHandle AbortedHandle;
};
