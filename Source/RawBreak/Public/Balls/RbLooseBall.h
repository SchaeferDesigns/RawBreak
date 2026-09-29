#pragma once

// A ball that left its table (decisions 2026-09-28; Docs/ue-architecture.md 18.6.1), under ENGINE physics: it bounces on the
// floor, rolls under stools, knocks against furniture. Pure presentation: the rules already decided the foul and the respot
// (the core's BallOffTable event, rb::OffTableReason), nothing here feeds back into the core, replays or the match state.
// Spawned by URbLooseBallSubsystem at the hand-off; carries the ball's own mesh / material instance (numbers visible), radius
// and mass; collision profile RbAssetPaths::Collision::LooseBallProfile (blocks world geometry, ignores pawns and the cue
// sweep), physical material RbAssetPaths::PhysMatBall, CCD on. Tagged RbAssetPaths::Tag::LooseBall.
// Owner: M2-E.
//
// Details (M2-E):
//  * Components: Body (USphereComponent, the root, radius = the ball's own radius) simulates; Ball (the ball set's mesh, scaled
//    to the radius, no collision) is its visual child. The sphere is exact for every ball (the oversized bar cue ball too) and
//    independent of the baked mesh asset.
//  * Launch sets the core state of the hand-off: the actor transform (position, orientation) is the caller's, then mass
//    (inertia 2/5 m R^2 from the sphere), linear [cm/s] and angular [rad/s] world velocities. Read back unchanged before the
//    first physics step (RawBreak.Unit.LooseBall.HandOff_ExactCoreState).
//  * Contact materials: PM_RbBall multiplies (friction 1.0, restitution 0.975, combine mode Multiply wins over the default
//    Average), so the SURFACE's physical material gives the pair its friction / restitution (VCT 0.5 / 0.35 etc.,
//    rb_make_physics.py; venue-dive-bar 13.5).
//  * Rolling resistance (Chaos has none): while the ball rests on a surface (a probe below the centre on the RbLooseBall
//    channel), its tangential velocity and its spin shrink by the same factor each tick, i.e. a constant deceleration
//    mu_r * g that keeps a rolling ball rolling (RollingResistanceFor, ESTIMATE per surface), and the spin about the normal
//    decays (pivoting friction).
//  * Rest: IsResting when the body sleeps, or it moved slower than RestSpeedCmS for RestHoldSeconds; then it is put to sleep.
//  * Events (URbLooseBallSubsystem, audio AU-25): every physics hit with an approach speed >= MinImpactSpeedMps -> OnImpact
//    (normal impulse [N s], approach speed [m/s] from the velocity before the step, surface type of the other material);
//    every tick while it rolls on a surface -> OnRolling.
//  * Rotation smear: its own material instance (a copy of the ball set's) gets BallOmegaLocal from the physics spin.

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "GameFramework/Actor.h"

#include "RbLooseBall.generated.h"

class ARbTable;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class RAWBREAK_API ARbLooseBall : public AActor
{
	GENERATED_BODY()

public:
	ARbLooseBall();

	// Starts the ball under physics at its current actor transform with world velocities [cm/s] and [rad/s].
	void Launch(ARbTable* InTable, int32 InBallId, UStaticMesh* Mesh, UMaterialInterface* Material, double RadiusCm, double MassKg,
		const FVector& LinearVelocityCmS, const FVector& AngularVelocityRadS);

	int32 GetBallId() const { return BallId; }
	int32 GetTableIndex() const { return TableIndex; }
	ARbTable* GetTable() const { return Table.Get(); }
	double GetRadiusCm() const { return RadiusCm; }

	// At rest (the physics body sleeps or moves slower than 1 cm/s for 0.5 s).
	bool IsResting() const;

	UStaticMeshComponent* GetBallComponent() const { return Ball; }

	// --- additions (M2-E) --------------------------------------------------------------------------------------------

	// The physics body (root).
	USphereComponent* GetBodyComponent() const { return Body; }
	double GetMassKg() const { return MassKg; }
	FVector GetLinearVelocity() const;      // world [cm/s]
	FVector GetAngularVelocity() const;     // world [rad/s]
	// Seconds the ball has been slower than RestSpeedCmS (0 while it moves).
	double GetRestSeconds() const { return RestSeconds; }
	// The last tick found a supporting surface under the ball (its surface type, e.g. RbAssetPaths::Surface::Vct).
	bool IsGrounded() const { return bGrounded; }
	EPhysicalSurface GetGroundSurface() const { return GroundSurface; }
	// Physics hits reported so far (all / from below: normal within 60 deg of up) and the largest approach speed [m/s].
	int32 GetImpactCount() const { return ImpactCount; }
	int32 GetFloorImpactCount() const { return FloorImpactCount; }
	double GetMaxImpactSpeed() const { return MaxImpactSpeed; }
	// Highest centre Z [world cm] after the first hit from below (a bounce rises above the resting height).
	double GetMaxZAfterFirstFloorImpact() const { return MaxZAfterFloorImpact; }
	UMaterialInstanceDynamic* GetBallMaterial() const { return BallMaterial; }

	// Shot whose hand-off spawned this ball (FRbShot::Id; 0 = a direct HandOff call).
	uint32 GetHandOffShotId() const { return HandOffShotId; }
	void SetHandOffShotId(uint32 Id) { HandOffShotId = Id; }

	// Rolling-resistance coefficient mu_r of a pool ball on a surface (ESTIMATE until measured: VCT / concrete 0.02 with grout
	// and grit, wood 0.015, rubber mat 0.08, cloth 0.010 as the core's table cloth, anything else 0.02).
	static double RollingResistanceFor(EPhysicalSurface Surface);

	// What a tick does (rolling resistance, rest detection, events, smear); tests call it without ticking the world.
	void UpdateMotion(double DeltaSeconds);

	UPROPERTY(EditAnywhere, Category = "RawBreak|Balls")
	double RestSpeedCmS = 1.0;

	UPROPERTY(EditAnywhere, Category = "RawBreak|Balls")
	double RestHoldSeconds = 0.5;

	// Physics hits slower than this [m/s] along the normal are not reported (continuous rolling contact).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Balls")
	double MinImpactSpeedMps = 0.05;

	// Time constant [s] of the decay of the spin about the contact normal on a surface (pivoting friction, ESTIMATE).
	UPROPERTY(EditAnywhere, Category = "RawBreak|Balls")
	double PivotSpinDecaySeconds = 0.6;

	// AActor
	virtual void Tick(float DeltaSeconds) override;

protected:
	UFUNCTION()
	void OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse,
		const FHitResult& Hit);

	// Probes the surface under the ball (bGrounded, GroundNormal, GroundSurface).
	void UpdateGround();

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Balls")
	TObjectPtr<USphereComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Balls")
	TObjectPtr<UStaticMeshComponent> Ball;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BallMaterial;

	TWeakObjectPtr<ARbTable> Table;
	int32 BallId = INDEX_NONE;
	int32 TableIndex = INDEX_NONE;
	double RadiusCm = 2.8575;
	double MassKg = 0.17;
	uint32 HandOffShotId = 0;

	FVector PreStepVelocity = FVector::ZeroVector; // velocity before the coming physics step (approach speeds of hits)
	double RestSeconds = 0.0;
	bool bGrounded = false;
	FVector GroundNormal = FVector::UpVector;
	EPhysicalSurface GroundSurface = SurfaceType_Default;
	int32 ImpactCount = 0;
	int32 FloorImpactCount = 0;
	double MaxImpactSpeed = 0.0;
	double MaxZAfterFloorImpact = -UE_BIG_NUMBER;
};
