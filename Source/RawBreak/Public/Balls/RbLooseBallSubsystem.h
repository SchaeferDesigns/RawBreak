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
//   Events     OnImpact per physics hit (audio AU-25, M2-C; captions later), OnReturned per returned ball.
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
//    exists, for UnreachableReturnSeconds).
//  * Pick-up: FindGazedBall = the loose ball (resting or slower than PickUpMaxSpeedCmS) whose centre is at most
//    GazeToleranceCm + R from the gaze ray, in front of the eye, within ReachCm plan distance and PickUpMaxDropCm below / 30 cm
//    above the eye, with a clear line of sight (Visibility); the nearest to the ray wins. A cue ball picked up while the
//    director waits for the placement goes into the pawn's URbBallInHandComponent (BeginCarry, legality = CanPlaceCueBall).
//  * Replays: URbReplaySubsystem calls SetReplayActive around a replay: withholding is suspended (the recorded shot shows every
//    ball) and the loose actors are hidden (the replay never shows a second copy); both return with the live table.
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

	// Replays: suspends withholding on every bound ball set and hides the loose actors (true), restores both (false).
	void SetReplayActive(bool bActive);
	bool IsReplayActive() const { return bReplayActive; }

	// Hand-offs on / off (and the console variable rb.LooseBall.Enable).
	void SetEnabled(bool bEnable) { bEnabled = bEnable; }
	bool IsEnabled() const;

	// Tunables (ui-ux 3.5 reach 1.2 m; ESTIMATE otherwise).
	double ReachCm = 120.0;
	double GazeToleranceCm = 12.0;
	double PickUpMaxSpeedCmS = 30.0;
	double PickUpMaxDropCm = 200.0;
	double UnreachableReturnSeconds = 20.0;
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
	// Removes the entry at Index, destroys its actor, un-withholds the table ball and broadcasts the return.
	void ReturnEntry(int32 Index, ERbLooseBallReturn Reason);
	void UpdateSessions();
	bool IsInReturnVolume(const ARbLooseBall& Ball) const;
	bool IsBelowKillZ(const ARbLooseBall& Ball) const;

	TArray<FEntry> Entries;
	TArray<FBinding> Bindings;
	TMap<int32, FTableWatch> Watches;
	FDelegateHandle ProviderHandle;
	ERbLooseBallReturn LastReturnReason = ERbLooseBallReturn::Manual;
	int32 ReturnCount = 0;
	bool bReplayActive = false;
	bool bEnabled = true;
};
