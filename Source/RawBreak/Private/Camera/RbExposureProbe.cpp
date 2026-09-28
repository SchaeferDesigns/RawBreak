#include "RbExposureProbe.h"

#include "GameFramework/Actor.h"
#include "SceneView.h"

// Owner: UE-5b.

FRbExposureProbe::FRbExposureProbe(const FAutoRegister& AutoRegister, UWorld* InWorld, const AActor* InViewActor)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
	, ViewActorId(InViewActor ? InViewActor->GetUniqueID() : 0)
{
}

void FRbExposureProbe::SetupView(FSceneViewFamily& /*InViewFamily*/, FSceneView& InView)
{
	if (!IsInGameThread() || (ViewActorId != 0 && InView.ViewActor.ActorUniqueId != ViewActorId))
	{
		return;
	}
	const float Exposure = InView.GetLastEyeAdaptationExposure();
	if (Exposure > 0.0f && FMath::IsFinite(Exposure))
	{
		LastExposure = Exposure;
	}
}
