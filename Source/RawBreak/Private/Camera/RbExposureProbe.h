#pragma once

// Reads the adapted exposure of the player's view back to the game thread (ue5-realism-plan 4.7: film grain coupled to the
// CURRENT exposure). The renderer reads its eye-adaptation buffer back every frame for pre-exposure; SetupView runs on the game
// thread while the view is set up (ULocalPlayer::CalcSceneView), so the value is read there, for the views of one actor only,
// and never touched off the game thread. Owner: UE-5b.

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FRbExposureProbe : public FWorldSceneViewExtension
{
public:
	FRbExposureProbe(const FAutoRegister& AutoRegister, UWorld* InWorld, const AActor* InViewActor);

	// ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	// Linear exposure multiplier of the last rendered frame of the actor's view (0 = none yet).
	float GetLastExposure() const { return LastExposure; }

private:
	uint32 ViewActorId = 0;
	float LastExposure = 0.0f;
};
