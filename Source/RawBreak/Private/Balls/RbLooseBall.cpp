#include "Balls/RbLooseBall.h"

#include "Core/RbAssetPaths.h"
#include "Table/RbTable.h"

#include "Components/StaticMeshComponent.h"

// Owner: M2-E. Stub of the M2 architect step: a static mesh component that is shown where it was spawned; the physics set-up
// (profile, physical material, CCD, mass, damping, velocities, hit notifies -> URbLooseBallSubsystem::OnImpact) is TODO(M2-E).

ARbLooseBall::ARbLooseBall()
{
	PrimaryActorTick.bCanEverTick = false;
	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ball"));
	SetRootComponent(Ball);
	Ball->SetMobility(EComponentMobility::Movable);
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision); // TODO(M2-E): LooseBallProfile + simulate physics
	Tags.Add(RbAssetPaths::Tag::LooseBall);
}

void ARbLooseBall::Launch(ARbTable* InTable, int32 InBallId, UStaticMesh* Mesh, UMaterialInterface* Material, double InRadiusCm,
	double /*MassKg*/, const FVector& /*LinearVelocityCmS*/, const FVector& /*AngularVelocityRadS*/)
{
	Table = InTable;
	TableIndex = InTable ? InTable->TableIndex : INDEX_NONE;
	BallId = InBallId;
	RadiusCm = InRadiusCm;
	Ball->SetStaticMesh(Mesh);
	if (Material)
	{
		Ball->SetMaterial(0, Material);
	}
	// The generated ball mesh is a unit sphere of radius 1 cm (RbAssetPaths::BallMesh), scaled per ball.
	Ball->SetWorldScale3D(FVector(RadiusCm));
	// TODO(M2-E): SetSimulatePhysics, mass override, SetPhysicsLinearVelocity / SetPhysicsAngularVelocityInRadians, CCD.
}

bool ARbLooseBall::IsResting() const
{
	return !Ball->IsSimulatingPhysics() || !Ball->IsAnyRigidBodyAwake();
}
