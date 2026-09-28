#pragma once

// Look-dev / acceptance capture camera (Docs/ue-architecture.md 12 A7, review R-08). A cine camera actor that applies a
// RAW BREAK camera preset (default Eyes: vertical FOV 50 deg, pupil DoF, EV100 exposure law, shutter, grain) through
// URbCameraRigComponent::ApplyPresetToCamera at BeginPlay, with the real viewport aspect. The level generators place these
// (tags RbAssetPaths::CaptureCamera::*) instead of plain ACameraActors, so headless screenshots judge the look through
// the game's own camera model. URbHeadlessCaptureSubsystem finds it like any ACameraActor (tag or name). Owner: UE-8.

#include "CoreMinimal.h"
#include "CineCameraActor.h"

#include "Core/RbTypes.h"

#include "RbLookDevCamera.generated.h"

UCLASS()
class RAWBREAK_API ARbLookDevCamera : public ACineCameraActor
{
	GENERATED_BODY()

public:
	ARbLookDevCamera(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	ERbCameraPreset Preset = ERbCameraPreset::Eyes;

	// Focus distance [cm] (0 = focus on the first blocking hit along the view axis).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	float FocusDistanceCm = 0.0f;

	virtual void BeginPlay() override;
};
