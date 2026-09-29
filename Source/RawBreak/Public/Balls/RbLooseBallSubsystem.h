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
// Owner: M2-E (stub by the M2 architect step; TODO(M2-E)).

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Subsystems/WorldSubsystem.h"

#include "rb/Physics/BallState.h"

#include "RbLooseBallSubsystem.generated.h"

class ARbBallSet;
class ARbLooseBall;

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

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnLooseBallImpact, const FRbLooseBallImpact& /*Impact*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnLooseBallRolling, const FRbLooseBallRolling& /*Rolling*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnLooseBallReturned, int32 /*TableIndex*/, int32 /*BallId*/);

UCLASS()
class RAWBREAK_API URbLooseBallSubsystem : public UWorldSubsystem
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

	// UWorldSubsystem
	virtual void Deinitialize() override;

private:
	TArray<TWeakObjectPtr<ARbLooseBall>> LooseBalls;
	TArray<TWeakObjectPtr<ARbBallSet>> BoundBallSets;
};
