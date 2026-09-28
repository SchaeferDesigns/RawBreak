#include "Balls/RbBallSet.h"

#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbCoords.h"
#include "Table/RbTable.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"

// Owner: UE-2. TODO(UE-2): component creation per ball (mesh, scale = radius, MIDs with WPA colours and numbers),
// no-teleport updates, MPC writes, tests.

ARbBallSet::ARbBallSet()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Playback = CreateDefaultSubobject<URbShotPlaybackComponent>(TEXT("Playback"));
}

void ARbBallSet::InitForTable(ARbTable* InTable)
{
	Table = InTable;
	if (InTable)
	{
		AttachToComponent(InTable->GetClothOrigin(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	// TODO(UE-2): create BallComponents / BallMaterials for InTable->GetContext().Balls.
}

void ARbBallSet::SetBallCore(int32 BallId, const rb::Vec3& Position, const rb::Quat& Orientation)
{
	if (UStaticMeshComponent* Ball = GetBallComponent(BallId))
	{
		Ball->SetRelativeLocationAndRotation(FRbCoords::PositionToUE(Position), FRbCoords::OrientationToUE(Orientation), false, nullptr,
			ETeleportType::None);
	}
}

void ARbBallSet::SetBallSpinCore(int32 /*BallId*/, const rb::Vec3& /*Omega*/)
{
	// TODO(UE-2): ball-local omega -> BallOmegaLocal on the MID.
}

void ARbBallSet::SetBallVisible(int32 BallId, bool bVisible)
{
	if (UStaticMeshComponent* Ball = GetBallComponent(BallId))
	{
		Ball->SetVisibility(bVisible);
	}
}

bool ARbBallSet::IsBallVisible(int32 BallId) const
{
	const UStaticMeshComponent* Ball = GetBallComponent(BallId);
	return Ball && Ball->IsVisible();
}

void ARbBallSet::ShowSimBalls(const rb::SimBall* Balls, int32 Count)
{
	for (int32 Id = 0; Id < Count && Id < GetBallCount(); ++Id)
	{
		SetBallVisible(Id, Balls[Id].InPlay);
		if (Balls[Id].InPlay)
		{
			SetBallCore(Id, Balls[Id].State.Position, Balls[Id].Orientation);
		}
	}
}

UStaticMeshComponent* ARbBallSet::GetBallComponent(int32 BallId) const
{
	return BallComponents.IsValidIndex(BallId) ? BallComponents[BallId].Get() : nullptr;
}

void ARbBallSet::UpdateOcclusionParameters()
{
	// TODO(UE-2): MPC_RbBalls Ball00..Ball15 (RbAssetPaths::Param::MpcBall).
}
