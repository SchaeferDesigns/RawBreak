#pragma once

// Diegetic ball in hand (playtest 2026-09-28 P2; ui-ux 9.6; Docs/ue-architecture.md 18.3). M1 moved an invisible target under the
// aim point and the player could only guess where the ball would land. Now a HAND carries the cue ball visibly over the cloth:
//   Carrying   the hand holds the ball HoverHeight above the cloth over the target (the real lamp shadow + a contact preview on
//              the cloth show where it will touch); the target follows the look point on the bed (analytic plane z = 0 of the
//              table frame), clamped to the reachable bed (the nose rectangle less a radius); AddFineAdjustCm nudges it (the
//              aim's fine factor); the hand moves with human lag (critically damped, sub-stepped, speed-limited: never a jump) and
//              a little tremor, and the ball never dips below HoverHeight while it is carried; over another ball (or the rail)
//              the hand lifts it clear, eased (a person never drags the ball through the others).
//   Lowering   Confirm (LMB / Enter / F) at a legal target: the hand sets the ball down (LowerSeconds, ~0.25 s; longer when the
//              lagging hand is still far away: lining up never exceeds MaxHandSpeed) on EXACTLY the previewed target (lining up from
//              where the lagging hand was, over - never through - a ball in between, over the rail when outside the bed); OnSetDown
//              fires when the ball touches the cloth (the stroke component then places it through the director,
//              URbMatchDirector::PlaceCueBall). The target is frozen from Confirm until the hand let go (SetTargetCore ignored).
//   Refused    at an illegal spot (IsLegal: overlaps a ball, off the bed, outside the kitchen when behind the head string) the hand
//              hesitates (a small lift and shake) and does not lower; OnRefused (a soft knock, M2-C) and the director's mandatory
//              line; the red outline exists only as an assist option (later). Moving the target carries on (the hesitation runs on
//              its own clock and fades out, a repeated Confirm during it does not restart it: never a jump).
//   Placed     the ball is on the cloth (the table's cue ball shows it); the hand lets go and withdraws, then Inactive.
// The table's cue ball is hidden while the hand carries it (it was picked up) and shown again if the carry is cancelled; after the
// placement the director shows it where it was set down. The same hand later carries a ball picked up from the floor (M2-E,
// BeginCarry) and becomes the M3 arm / hand (MetaHuman / mannequin); the API stays. Meshes (procedural stand-ins,
// URbAssetBakeLibrary::BakeHandCarryMesh via rb_make_player.py, to the contract of RbHandMesh below): the hand with the bare forearm
// and a rolled cuff, /Game/Generated/Player/SM_RbHand_Carry (RbAssetPaths::HandCarryMesh), at the carried ball, its yaw away from
// the carrying shoulder and its forearm pitched toward it; the shirt sleeve SM_RbArm_Carry stretched from the cuff to the shoulder
// (below and right of the eye, out of view), so the arm enters the frame like a real one. The carried ball uses the ball set's mesh
// and material instance of the ball; the contact preview is an engine plane with M_RbContactPreview. Every visual is optional (tests
// run without a world or without the generated assets). Owner: M2-F. Tests: RawBreak.Unit.BallInHand.* (F5),
// RawBreak.Functional.Feel.FeelFlow (F8).

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "rb/Math/Vec2.h"

#include "RbBallInHandComponent.generated.h"

class ARbBallSet;
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

// Geometry contract of the stand-in hand / arm meshes (RbAssetBake_Player.cpp builds them to these numbers; the component places
// them by them). Hand mesh frame [cm]: the centre of the 2 1/4 in ball it is modelled around at the origin, +X = where the fingers
// point (away from the player), +Z up; the forearm leaves the wrist toward -X rising ForearmElevationDeg, bare skin up to the rolled
// cuff that ends CuffEndCm from the wrist. Arm mesh frame: the sleeve along +X from 0 to ArmLengthCm (scaled to the cuff -> shoulder
// distance at run time).
namespace RbHandMesh
{
	inline const TCHAR* const ArmMeshPath = TEXT("/Game/Generated/Player/SM_RbArm_Carry");
	inline constexpr double ReferenceBallRadiusCm = 2.8575;
	inline constexpr double WristX = -6.0;
	inline constexpr double WristY = 0.3;
	inline constexpr double WristZ = ReferenceBallRadiusCm + 3.9;
	inline constexpr double ForearmElevationDeg = 30.0;
	inline constexpr double CuffEndCm = 19.0;
	inline constexpr double ArmLengthCm = 100.0;

	inline FVector Wrist() { return FVector(WristX, WristY, WristZ); }
	inline FVector ForearmDirection()
	{
		const double E = FMath::DegreesToRadians(ForearmElevationDeg);
		return FVector(-FMath::Cos(E), 0.0, FMath::Sin(E));
	}
	inline FVector CuffEnd() { return Wrist() + ForearmDirection() * CuffEndCm; }
}

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnBallSetDown, const rb::Vec2& /*PlanPositionCore*/);
// M2-F addition (M2-C: the soft knock of a refused placement): the target that was refused (core plan position).
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnBallSetDownRefused, const rb::Vec2& /*PlanPositionCore*/);

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbBallInHandComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	URbBallInHandComponent();

	// Starts carrying ball BallId (radius in m, the oversized bar cue ball included) over Table. IsLegal = the director's
	// CanPlaceCueBall (plan position, core table frame); unset = every spot on the bed is legal.
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
	// Fired when a set-down is refused (illegal target).
	FRbOnBallSetDownRefused OnRefused;

	// Height of the carried ball's bottom above the cloth [cm].
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float HoverHeightCm = 4.0f;

	// --- M2-F additions -----------------------------------------------------------------------------------------------------
	// Seconds from Confirm until the ball touches the cloth (at the least: a hand still far from the target takes longer).
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float LowerSeconds = 0.25f;
	// Human hand lag: critically damped follow of the target at this frequency [Hz], speed-limited [m/s].
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float FollowHz = 1.6f;
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float MaxHandSpeed = 1.5f;
	// Tremor of the carrying hand [mm] (horizontal; vertical only upward: the ball never dips below HoverHeight).
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float TremorMm = 0.6f;
	// Fine adjustment: cloth metres per cm of mouse travel (the pawn sets it from the fine aim factor).
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float FineMetersPerCm = 0.035f;
	// Largest fine offset from the look point [m].
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float MaxFineOffset = 0.15f;
	// The carrying hand's fingers turned inward from "away from the shoulder" [deg, - = to the left for the right hand]: the elbow
	// is out to the side, so the wrist does not hide the ball from the eye.
	UPROPERTY(EditAnywhere, Category = "RawBreak|BallInHand")
	float HandYawOffsetDeg = -40.0f;

	// Advances the hand by DeltaSeconds (TickComponent calls it; tests drive it directly).
	void TickCarry(double DeltaSeconds);

	bool HasTarget() const { return bHasTarget; }
	double GetBallRadius() const { return BallRadius; }
	int32 GetBallId() const { return BallId; }
	ARbTable* GetTable() const { return Table.Get(); }
	// Where the hand holds the ball now (core plan position, the lagged human hand incl. tremor) and the ball's bottom above the cloth
	// [cm].
	rb::Vec2 GetHandPlanCore() const { return ShownPlan; }
	double GetBallBottomCm() const { return ShownBottomCm; }
	// The target is legal (IsLegal) - evaluated every frame for tests and the debug block; the hand shows nothing of it.
	bool IsTargetLegal() const;
	// Seconds since the state was entered.
	double GetStateSeconds() const { return StateTime; }

	UStaticMeshComponent* GetHandMesh() const { return HandMesh; }
	UStaticMeshComponent* GetArmMesh() const { return ArmMesh; }
	UStaticMeshComponent* GetCarriedBallMesh() const { return BallMesh; }
	UStaticMeshComponent* GetPreviewMesh() const { return PreviewMesh; }

	// UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

protected:
	void SetState(ERbBallInHandState NewState);
	void RecomputeTarget();
	rb::Vec2 ClampToBed(const rb::Vec2& Plan) const;
	void EnsureVisuals();
	void UpdateVisuals();
	void HideVisuals();
	void RestoreTableBall();
	ARbBallSet* FindBallSet() const;
	FVector ShoulderWorld() const;
	// The lowest ball bottom [cm above the cloth] at Plan that clears every other shown ball by MarginCm.
	double ClearanceBottomCm(const rb::Vec2& Plan, double MarginCm) const;
	// Extra height of the ball bottom [cm] at Plan outside the bed (over the rail), 0 on the bed.
	double OutsideLiftCm(const rb::Vec2& Plan) const;
	// The hand's follow state starts at the carrying shoulder (at the target without a player).
	void InitHand();

	ERbBallInHandState State = ERbBallInHandState::Inactive;
	rb::Vec2 Target;
	TWeakObjectPtr<ARbTable> Table;
	int32 BallId = 0;
	double BallRadius = 0.028575;
	TFunction<bool(const rb::Vec2&)> IsLegal;

private:
	bool bHasTarget = false;
	rb::Vec2 LookTarget;          // the look point (before the fine offset and the clamp)
	rb::Vec2 FineOffset;          // accumulated fine adjustment [m]
	rb::Vec2 HandPlan;            // the hand's follow state (no tremor)
	rb::Vec2 HandVelocity;
	rb::Vec2 ShownPlan;           // incl. tremor
	double ShownBottomCm = 0.0;   // ball bottom above the cloth
	double ClearanceLiftCm = 0.0; // eased extra height over other balls
	bool bHandValid = false;
	double StepRemainder = 0.0;
	double Time = 0.0;
	double StateTime = 0.0;
	rb::Vec2 LowerFrom;           // shown ball plan position when the lowering began
	double LowerFromBottomCm = 0.0;
	double LowerDuration = 0.25;  // [s] of the running lowering: LowerSeconds, longer when the hand still has far to go (speed limit)
	rb::Vec2 RefusedAt;
	double RefuseTime = -1.0;     // seconds since the running hesitation began (< 0 = none); it finishes even if the target moves on
	bool bHidTableBall = false;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> HandMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ArmMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BallMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> PreviewMesh;
};
