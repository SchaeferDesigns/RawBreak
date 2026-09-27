#include "Replay/RbReplayCamera.h"

#include "Table/RbTable.h"

// Owner: UE-7. TODO(UE-7): view placements per table size, lens per view, smooth follow.

ARbReplayCamera::ARbReplayCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARbReplayCamera::SetView(ERbReplayView InView, const ARbTable* /*Table*/, const FTransform& ShooterView)
{
	View = InView;
	if (InView == ERbReplayView::Shooter)
	{
		SetActorTransform(ShooterView);
	}
	// TODO(UE-7): Overhead / Rail / Follow placements.
}

void ARbReplayCamera::SetFollowTarget(USceneComponent* Target)
{
	FollowTarget = Target;
}

void ARbReplayCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// TODO(UE-7): follow.
}
