#include "Dev/RbLookDevCamera.h"

#include "Camera/RbCameraModel.h"
#include "Camera/RbCameraRigComponent.h"

#include "CineCameraComponent.h"

// Owner: UE-8. TODO(UE-8): viewport aspect from the game viewport (capture resolution), focus distance (trace when 0).

ARbLookDevCamera::ARbLookDevCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void ARbLookDevCamera::BeginPlay()
{
	Super::BeginPlay();
	if (UCineCameraComponent* Cine = GetCineCameraComponent())
	{
		URbCameraRigComponent::ApplyPresetToCamera(*Cine, RbCameraModel::Defaults(Preset), 16.0f / 9.0f);
	}
}
