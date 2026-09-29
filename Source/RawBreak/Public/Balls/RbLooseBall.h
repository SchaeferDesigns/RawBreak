#pragma once

// A ball that left its table (decisions 2026-09-28; Docs/ue-architecture.md 18.6.1), under ENGINE physics: it bounces on the
// floor, rolls under stools, knocks against furniture. Pure presentation: the rules already decided the foul and the respot
// (the core's BallOffTable event, rb::OffTableReason), nothing here feeds back into the core, replays or the match state.
// Spawned by URbLooseBallSubsystem at the hand-off; carries the ball's own mesh / material instance (numbers visible), radius
// and mass; collision profile RbAssetPaths::Collision::LooseBallProfile (blocks world geometry, ignores pawns and the cue
// sweep), physical material RbAssetPaths::PhysMatBall, CCD on. Tagged RbAssetPaths::Tag::LooseBall.
// Owner: M2-E (stub by the M2 architect step; TODO(M2-E)).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "RbLooseBall.generated.h"

class ARbTable;
class UMaterialInterface;
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

protected:
	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Balls")
	TObjectPtr<UStaticMeshComponent> Ball;

	TWeakObjectPtr<ARbTable> Table;
	int32 BallId = INDEX_NONE;
	int32 TableIndex = INDEX_NONE;
	double RadiusCm = 2.8575;
};
