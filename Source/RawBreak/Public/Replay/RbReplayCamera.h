#pragma once

// Replay camera (Docs/ue-architecture.md 6.7): a cine camera the replay subsystem positions per ERbReplayView
// (Broadcast preset of ue5-realism-plan 4.1: long lens 25-40 deg, tripod-smooth). Later the base of the trailer
// camera kit (handheld noise, dolly, slow motion via the playback rate). Owner: UE-7.

#include "CoreMinimal.h"
#include "CineCameraActor.h"

#include "Replay/RbReplaySubsystem.h"

#include "RbReplayCamera.generated.h"

class ARbTable;

UCLASS()
class RAWBREAK_API ARbReplayCamera : public ACineCameraActor
{
	GENERATED_BODY()

public:
	ARbReplayCamera(const FObjectInitializer& ObjectInitializer);

	// Places the camera for View (Shooter uses ShooterView; Follow tracks FollowTarget each tick).
	void SetView(ERbReplayView View, const ARbTable* Table, const FTransform& ShooterView);
	void SetFollowTarget(USceneComponent* Target);

	virtual void Tick(float DeltaSeconds) override;

protected:
	ERbReplayView View = ERbReplayView::Shooter;
	TWeakObjectPtr<USceneComponent> FollowTarget;
};
