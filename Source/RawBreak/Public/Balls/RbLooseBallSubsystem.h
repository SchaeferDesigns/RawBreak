#pragma once

// Balls off the table (decisions 2026-09-28; ui-ux 2.4; venue-dive-bar 13.5; Docs/ue-architecture.md 18.6.1).
//
//   Hand-off   a LIVE shot's playback fires rb::ShotEventType::BallOffTable (OnShotEvent of the table's
//              URbShotPlaybackComponent): the ball set hides that ball and an ARbLooseBall continues its motion under engine
//              physics from the core state at the event (position, velocity, spin; FRbCoords -> world). OffTableReason Floor /
//              ExternalObjectRebound: physics; RestsOnRailOrFrame: the ball stays where the core froze it (resting on the cap).
//              Replays show the ball leaving and hide it at the hand-off time (no second loose actor).
//   Rules      untouched: the director already committed the foul and the respot / ball in hand (RUL 4.7); while a loose
//              ball exists for a ball that the committed table state has back in play, the table's instance stays hidden
//              ("awaiting return") - the rules never wait for it.
//   Return     (simple for M2) pick-up by the player: a URbInteractionSubsystem provider offers "Pick up the ball" when a
//              resting-or-slow loose ball is gazed within 1.2 m; the loose actor is removed and the table instance shown (the
//              cue ball in hand goes into M2-F's carrying hand). Automatic return when the next address starts (BeginAddress
//              of the player's table), when the ball comes to rest in an RbBallReturn volume, falls below the kill Z or rests
//              unreachable for 20 s ("the bartender brings it"; a voice line later).
//   Events     OnImpact per physics hit (audio AU-25, M2-C; captions later), OnReturned per returned ball. A loose ball that is
//              hidden (a replay of its table plays) reports no OnImpact / OnRolling: the replay neither shows nor sounds it.
// Keyed by (TableIndex, BallId): several tables per level work (18.6.2).
// Owner: M2-E.
//
// Details (M2-E):
//  * Binding: every ARbBallSet binds itself in InitForTable (and unbinds in EndPlay), so every table of a level hands off.
//    A shot event is a hand-off only while the playback's clock is LIVE (FRbPlaybackClock::bLive); the state comes from the
//    event's Pre[0] (RecordOptions::EventStates, always on in the game), else from the ball's last segment before the
//    Terminal one; the orientation is the final (= hand-off) orientation. The playback itself hides the table instance from
//    the hand-off time on (URbShotPlaybackComponent::HideTimeOf), live and in replays.
//  * Awaiting return = withheld (ARbBallSet::SetBallWithheld): the ball set keeps the table instance hidden while its loose copy
//    exists, whatever the director shows; a return un-withholds it (visible again only if the committed state has it in play).
//  * Automatic returns, checked every tick (UpdateAutomaticReturns) per registered table session (URbTableSubsystem):
//      Address        the session's stroke component gets down (GettingDown / Down), or the director submits the next shot
//                     (phase Simulating: scripted strikes of cheats and tests have no get-down)
//      CueBallPlaced  a loose cue ball of an already committed shot while the director's table state has the cue ball in play
//                     (the player placed the ball in hand)
//      Carried        the pawn's URbBallInHandComponent carries the cue ball (M2-F's hand holds it)
//      NewRack        the session's rack number or match seed changed, or its match shot index dropped (a new match, even
//                     with the same seed): the balls are racked
//    and per loose ball: ReturnVolume (resting inside a component with the RbBallReturn profile or tag), KillZ (below the
//    world's kill Z or FallBelowFloorCm under its table's floor), Unreachable (resting where no standing spot within reach
//    exists, for UnreachableReturnSeconds), OnTable (M2-E review: lying on ANY table - bed, rail, pocket - for
//    OnTableReturnSeconds: an ExternalObjectRebound ball is handed off at its apex under the lamp and falls back onto the bed,
//    a Floor ball may bounce off furniture onto a rail; left there it would roll through the table's balls, which have no
//    physics, and lie where the player aims the next shot while its table instance - respotted - stays hidden). The scene
//    queries of a resting ball (return volume, reach) run on its first resting tick and then every 0.5 s. A table that
//    replays keeps its loose balls as they are until the replay ends.
//  * A returned loose CUE ball while its director waits for the placement (any reason but CueBallPlaced / Carried / Replaced
//    / NewRack) goes into the hand as a picked-up one does: the picker's pawn, else the pawn of the session's stroke component
//    (M2-E review: the "bartender" returns of a cue ball the player cannot reach would otherwise leave nothing to place when
//    the carrying hand does not pick up a cue ball that lies loose).
//  * Pick-up: FindGazedBall = the loose ball (resting or slower than PickUpMaxSpeedCmS) whose centre is at most
//    GazeToleranceCm + R from the gaze ray, in front of the eye, within ReachCm plan distance and PickUpMaxDropCm below / 30 cm
//    above the eye, with a clear line of sight (Visibility); the nearest to the ray wins. A cue ball picked up while the
//    director waits for the placement goes into the pawn's URbBallInHandComponent (BeginCarry, legality = CanPlaceCueBall).
//  * Replays: URbReplaySubsystem calls SetReplayActive around a replay of the player's table: that table's withholding is
//    suspended (the recorded shot shows every ball) and its loose actors are hidden (the replay never shows a second copy); both
//    return with the live table. Other tables keep their loose balls and withheld balls (per table, 18.6.2 rule 3); no pick-up
//    is offered while any replay plays.
//  * Switch: console variable rb.LooseBall.Enable (1) and SetEnabled (tests): off = no hand-off (the ball disappears at the
//    hand-off time), everything else unchanged - the rules and hashes never depend on it.
//  * Dev commands: rb.LooseBall.List, rb.LooseBall.ReturnAll, rb.LooseBall.Drop <table> <id> <x> <y> <z> [<vx> <vy> <vz>]
//    (a ball handed off at a core state of that table; captures and debugging).

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Subsystems/WorldSubsystem.h"

#include "rb/Math/Quat.h"
#include "rb/Physics/BallState.h"
#include "rb/Shot/ShotRecord.h"

#include "RbLooseBallSubsystem.generated.h"

class APawn;
class ARbBallSet;
class ARbLooseBall;
struct FRbInteractionQuery;
struct FRbShot;

// One physics contact of a loose ball (audio AU-25: v_n = NormalImpulse / mass drives the synthesised floor hit).
struct FRbLooseBallImpact
{
	int32 TableIndex = INDEX_NONE;
	int32 BallId = INDEX_NONE;
	FVector WorldLocation = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	double NormalImpulse = 0.0;  // [N s]
	double NormalSpeed = 0.0;    // [m/s] approach speed along the normal
	double MassKg = 0.17;
	EPhysicalSurface Surface = SurfaceType_Default;
};

// A loose ball rolling (audio: rolling noise on the floor surface), sampled per frame for every moving loose ball.
struct FRbLooseBallRolling
{
	int32 TableIndex = INDEX_NONE;
	int32 BallId = INDEX_NONE;
	FVector WorldLocation = FVector::ZeroVector;
	double SpeedMps = 0.0;
	EPhysicalSurface Surface = SurfaceType_Default;
};

// Why a loose ball went back (M2-E addition; GetLastReturnReason, OnReturnedWithReason).
UENUM(BlueprintType)
enum class ERbLooseBallReturn : uint8
{
	PickUp,        // the player picked it up (interaction)
	Address,       // the next address of its table started (get down / next shot submitted)
	CueBallPlaced, // the ball in hand was placed on the table
	Carried,       // the carrying hand holds the cue ball
	NewRack,       // a new rack / match on its table
	ReturnVolume,  // came to rest in an RbBallReturn volume
	KillZ,         // fell below the kill Z / far below its table's floor
	Unreachable,   // rested out of reach for UnreachableReturnSeconds ("the bartender brings it")
	Replaced,      // the same ball left the table again
	Manual,        // ReturnBall / ReturnAll / console
	Destroyed,     // the actor vanished (FellOutOfWorld, level unload)
	OnTable,       // came back onto a table (lamp rebound, a bounce off furniture): the referee takes it off (M2-E review)
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnLooseBallImpact, const FRbLooseBallImpact& /*Impact*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnLooseBallRolling, const FRbLooseBallRolling& /*Rolling*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnLooseBallReturned, int32 /*TableIndex*/, int32 /*BallId*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRbOnLooseBallReturnedWithReason, int32 /*TableIndex*/, int32 /*BallId*/, ERbLooseBallReturn /*Reason*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnLooseBallHandOff, ARbLooseBall& /*Ball*/);

UCLASS()
class RAWBREAK_API URbLooseBallSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static URbLooseBallSubsystem* Get(const UObject* WorldContext);

	// Listens to the ball set's playback (live shots hand off, replays only hide). Idempotent.
	void BindBallSet(ARbBallSet* BallSet);
	void UnbindBallSet(ARbBallSet* BallSet);

	// The hand-off itself (tests call it directly): hides the ball in Balls and spawns its loose actor from the core state
	// (table frame) at the event. Returns nullptr when the ball stays on the rail (RestsOnRailOrFrame) or on failure.
	ARbLooseBall* HandOff(ARbBallSet& Balls, int32 BallId, const rb::BallState& CoreState);

	ARbLooseBall* FindLooseBall(int32 TableIndex, int32 BallId) const;
	TArray<ARbLooseBall*> GetLooseBalls(int32 TableIndex = INDEX_NONE) const;
	// A ball of that table waits for its return (the table instance stays hidden).
	bool IsAwaitingReturn(int32 TableIndex, int32 BallId) const { return FindLooseBall(TableIndex, BallId) != nullptr; }

	// Removes the loose actor and shows the table instance again (pick-up or automatic return). False if none existed.
	bool ReturnBall(int32 TableIndex, int32 BallId);
	int32 ReturnAll(int32 TableIndex);

	FRbOnLooseBallImpact OnImpact;
	FRbOnLooseBallRolling OnRolling;
	FRbOnLooseBallReturned OnReturned;

	// --- additions (M2-E) --------------------------------------------------------------------------------------------

	// The full hand-off: Reason from the event (RestsOnRailOrFrame spawns nothing), the ball's orientation at the hand-off
	// (core, table frame) and the shot it came from (FRbShot::Id; 0 = none).
	ARbLooseBall* HandOff(ARbBallSet& Balls, int32 BallId, const rb::BallState& CoreState, const rb::Quat& Orientation,
		rb::OffTableReason Reason, uint32 ShotId = 0);

	// Core state and orientation of the ball of event EventIndex (a BallOffTable event) at the hand-off. False if the event is
	// not a BallOffTable event or the ball has no track.
	static bool GetHandOffState(const FRbShot& Shot, int32 EventIndex, rb::BallState& OutState, rb::Quat& OutOrientation);

	bool IsBound(const ARbBallSet* BallSet) const;
	int32 GetNumLooseBalls() const;

	bool ReturnBall(int32 TableIndex, int32 BallId, ERbLooseBallReturn Reason);
	int32 ReturnAll(int32 TableIndex, ERbLooseBallReturn Reason);
	ERbLooseBallReturn GetLastReturnReason() const { return LastReturnReason; }
	int32 GetReturnCount() const { return ReturnCount; }

	// Pick-up (interaction provider): the gazed loose ball for Query (nullptr = none) and the pick-up itself.
	ARbLooseBall* FindGazedBall(const FRbInteractionQuery& Query) const;
	bool PickUp(ARbLooseBall& Ball, APawn* Pawn);
	static FText PickUpVerb();

	// The automatic returns of one tick (Tick calls it; tests call it with a chosen step).
	void UpdateAutomaticReturns(double DeltaSeconds);
	// A standing spot for a pawn exists within ReachCm of the ball, with a line of sight to it (see the file comment).
	bool IsReachable(const ARbLooseBall& Ball) const;
	// Seconds the ball has rested out of reach (0 while reachable or moving).
	double GetUnreachableSeconds(const ARbLooseBall& Ball) const;

	// Replays: suspends withholding on the replayed table's ball set and hides that table's loose actors (true), restores both
	// (false). Per table (18.6.2 rule 3: the other tables' loose balls and withheld balls are not part of the player's replay);
	// TableIndex INDEX_NONE = every table (false with INDEX_NONE ends every replay state).
	void SetReplayActive(bool bActive, int32 TableIndex = INDEX_NONE);
	// Any table replays (INDEX_NONE) / that table replays.
	bool IsReplayActive(int32 TableIndex = INDEX_NONE) const;

	// Hand-offs on / off (and the console variable rb.LooseBall.Enable).
	void SetEnabled(bool bEnable) { bEnabled = bEnable; }
	bool IsEnabled() const;

	// Tunables (ui-ux 3.5 reach 1.2 m; ESTIMATE otherwise).
	double ReachCm = 120.0;
	double GazeToleranceCm = 12.0;
	double PickUpMaxSpeedCmS = 30.0;
	double PickUpMaxDropCm = 200.0;
	double UnreachableReturnSeconds = 20.0;
	double OnTableReturnSeconds = 0.3; // lying on a table this long (accumulated while grounded on one): returned
	double FallBelowFloorCm = 300.0;
	double PawnRadiusCm = 25.0;
	double PawnHalfHeightCm = 88.0;
	double PawnEyeHeightCm = 150.0;

	FRbOnLooseBallReturnedWithReason OnReturnedWithReason;
	FRbOnLooseBallHandOff OnHandOff;

	// UWorldSubsystem / FTickableGameObject
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FEntry
	{
		TWeakObjectPtr<ARbLooseBall> Ball;
		TWeakObjectPtr<ARbBallSet> BallSet;
		int32 TableIndex = INDEX_NONE;
		int32 BallId = INDEX_NONE;
		uint32 ShotId = 0;
		double UnreachableSeconds = 0.0;
		double OnTableSeconds = 0.0; // grounded on a table (OnTable return)
		double ReachCheckIn = 0.0;   // seconds until the next reachability check
		bool bReachable = true;
	};

	struct FBinding
	{
		TWeakObjectPtr<ARbBallSet> BallSet;
		FDelegateHandle EventHandle;
	};

	struct FTableWatch
	{
		bool bValid = false;
		int32 RackNumber = 0;
		uint64 MatchSeed = 0;
		uint32 MatchShotIndex = 0; // drops on a new match (StartMatch resets it, even with the same seed and rack number)
		uint8 Phase = 0;           // ERbDirectorPhase
	};

	void HandleShotEvent(const TSharedRef<const FRbShot>& Shot, int32 EventIndex, TWeakObjectPtr<ARbBallSet> WeakBalls);
	FEntry* FindEntry(int32 TableIndex, int32 BallId);
	const FEntry* FindEntry(const ARbLooseBall& Ball) const;
	// A live loose ball of that table (no allocation: UpdateSessions asks it every tick).
	bool HasLooseBalls(int32 TableIndex) const;
	// Removes the entry at Index, destroys its actor, un-withholds the table ball (unless another live loose copy of the same
	// ball exists), hands a returned cue ball to the placing player (GiveCueBallToHand) and broadcasts the return. Pawn: who
	// picked it up (nullptr = an automatic return: the session's player).
	void ReturnEntry(int32 Index, ERbLooseBallReturn Reason, APawn* Pawn = nullptr);
	// The cue ball of TableIndex into the carrying hand (URbBallInHandComponent::BeginCarry, legality = CanPlaceCueBall) while
	// that table's director waits for the placement and the hand holds nothing. Pawn nullptr: the session's stroke component's.
	void GiveCueBallToHand(int32 TableIndex, double RadiusCm, APawn* Pawn);
	void UpdateSessions();
	bool IsInReturnVolume(const ARbLooseBall& Ball) const;
	bool IsBelowKillZ(const ARbLooseBall& Ball) const;
	// Replay state of one table (SetReplayActive) and its application to the bound ball sets / loose actors.
	bool IsTableInReplay(int32 TableIndex) const { return bReplayAllTables || ReplayTables.Contains(TableIndex); }
	void ApplyReplayState();
	static int32 TableIndexOf(const ARbBallSet* BallSet);

	TArray<FEntry> Entries;
	TArray<FBinding> Bindings;
	TMap<int32, FTableWatch> Watches;
	FDelegateHandle ProviderHandle;
	ERbLooseBallReturn LastReturnReason = ERbLooseBallReturn::Manual;
	int32 ReturnCount = 0;
	TArray<int32, TInlineAllocator<4>> ReplayTables; // tables whose replay plays (M2: the player's)
	bool bReplayAllTables = false;
	bool bEnabled = true;
};
