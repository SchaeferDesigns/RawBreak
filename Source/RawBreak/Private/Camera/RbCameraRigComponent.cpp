#include "Camera/RbCameraRigComponent.h"

#include "CineCameraComponent.h"

// Owner: UE-5b. TODO(UE-5b): eye placement + transition, FOV (vertical authored), DoF (pupil -> cine lens, focus
// ease), exposure / motion blur / grain post-process settings from Params, head motion hooks, tests (placement math,
// FOV at 16:9 / 21:9, T8 blur).

URbCameraRigComponent::URbCameraRigComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	Params = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
}

void URbCameraRigComponent::SetCamera(UCineCameraComponent* InCamera)
{
	Camera = InCamera;
	ApplyOptics();
}

void URbCameraRigComponent::SetPreset(ERbCameraPreset InPreset)
{
	Preset = InPreset;
	Params = ModelOverride ? ModelOverride->Get(InPreset) : RbCameraModel::Defaults(InPreset);
	ApplyOptics();
}

void URbCameraRigComponent::SetMode(ERbCameraRigMode InMode)
{
	if (Mode != InMode)
	{
		Mode = InMode;
		TransitionAlpha = 0.0;
	}
}

void URbCameraRigComponent::SetCueAxisWorld(const FVector& ContactPoint, const FVector& Direction)
{
	CueContactWorld = ContactPoint;
	CueDirectionWorld = Direction.GetSafeNormal();
}

void URbCameraRigComponent::SetFocusTargetWorld(const FVector& Target)
{
	FocusTargetWorld = Target;
}

void URbCameraRigComponent::BeginPlay()
{
	Super::BeginPlay();
	SetPreset(Preset);
}

void URbCameraRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// TODO(UE-5b): place the camera per Mode (standing / down on the shot / ball in hand) with the eased transition.
}

void URbCameraRigComponent::ApplyPresetToCamera(UCineCameraComponent& /*Camera*/, const FRbCameraPresetParams& /*Params*/, float /*ViewportAspect*/)
{
	// TODO(UE-5b): filmback (h fixed, w = h * aspect), f from the vertical FOV, N = f / A, post-process settings.
}

void URbCameraRigComponent::ApplyOptics()
{
	// TODO(UE-5b): FOV, aperture / focus, post-process (exposure, motion blur, grain, vignette) from Params.
}
