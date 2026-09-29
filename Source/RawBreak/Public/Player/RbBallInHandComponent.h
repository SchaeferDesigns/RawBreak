#pragma once

// Diegetic ball in hand (playtest 2026-09-28 P2; ui-ux 9.6; Docs/ue-architecture.md 18.3). M1 moved an invisible target under the
// aim point and the player could only guess where the ball would land. Now a HAND carries the cue ball visibly over the cloth:
//   Carrying   the hand holds the ball HoverHeight above the cloth exactly over the target (the real lamp shadow + a contact
//              preview under it show where it will touch); the target follows the look point on the bed (analytic plane z = 0 of
//              the table frame), clamped to the reachable bed; fine adjustment with Shift (the aim's fine factor); the hand moves
//              with human lag and a little tremor (the human-motion layer), never teleports.
//   Lowering   Confirm (LMB / Enter / F): the hand sets the ball down (~0.25 s) exactly on the previewed spot; the placement goes
//              to the director (URbMatchDirector::PlaceCueBall) only when the ball touches the cloth.
//   Refused    at an illegal spot (overlaps a ball, off the bed, outside the kitchen when behind the head string) the hand
//              hesitates and does not lower; a soft knock sound (M2-C) + the mandatory line; the red outline exists only as an
//              assist option (Assisted / Relaxed, later).
// The same hand later carries a ball picked up from the floor (M2-E) and becomes the M3 arm / hand (MetaHuman / mannequin); the
// API stays. Mesh: /Game/Generated/Player/SM_RbHand_Carry (RbAssetPaths::HandCarryMesh, a procedural stand-in made by M2-F's
// generator); the carried ball uses the table's ball mesh and the ball set's material instance of the cue ball.
// Owner: M2-F (stub by the M2 architect step; TODO(M2-F)).

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "rb/Math/Vec2.h"

#include "RbBallInHandComponent.generated.h"

class ARbTable;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ERbBallInHandState : uint8
{
	Inactive,
	Carrying,
	Lowering,
	Placed,
	Refused,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnBallSetDown, const rb::Vec2& /*PlanPositionCore*/);

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbBallInHandComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	URbBallInHandComponent();

	// Starts carrying ball BallId (radius in m, the oversized bar cue ball included) over Table. IsLegal = the director's
	// CanPlaceCueBall (plan position, core table frame).
	void BeginCarry(ARbTable* Table, int32 BallId, double BallRadius, TFunction<bool(const rb::Vec2&)> IsLegal);

	// The target under the look point (core plan position) and a fine adjustment [cm of mouse travel -> m on the cloth].
	void SetTargetCore(const rb::Vec2& Plan);
	void AddFineAdjustCm(const FVector2D& DeltaCm);

	// Confirm: lowers the ball at a legal target (true) or refuses (false, state Refused until the target moves).
	bool RequestSetDown();
	void Cancel();

	ERbBallInHandState GetState() const { return State; }
	rb::Vec2 GetTargetCore() const { return Target; }
	// Carried ball centre (world) as shown this frame.
	FVector GetBallWorld() const;

	// Fired when the lowered ball touches the cloth (the stroke component then calls the director's PlaceCueBall).
	FRbOnBallSetDown OnSetDown;

	// Height of the carried ball's bottom above the cloth [cm].
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float HoverHeightCm = 4.0f;

protected:
	ERbBallInHandState State = ERbBallInHandState::Inactive;
	rb::Vec2 Target;
	TWeakObjectPtr<ARbTable> Table;
	int32 BallId = 0;
	double BallRadius = 0.028575;
	TFunction<bool(const rb::Vec2&)> IsLegal;
};
