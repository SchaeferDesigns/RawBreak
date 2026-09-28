#pragma once

// Camera model parameters (ue5-realism-plan 4.1-4.9, parameter table 4.9). One struct per preset; the defaults
// are compiled in (RbCameraModel::Defaults) so M1 needs no data asset, and URbCameraModel lets look-dev override
// them later as a data asset. Owner: UE-5b.

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "Core/RbTypes.h"

#include "RbCameraModel.generated.h"

USTRUCT(BlueprintType)
struct FRbCameraPresetParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double VerticalFovDeg = 50.0;      // authored vertical FOV
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double DistortionK1 = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double DistortionK2 = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection") double NearClipCm = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double ApertureDiameterMm = 4.0;      // pupil A (DoF)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double FocusEaseSeconds = 0.2;       // accommodation latency
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optics") double ShutterAngleDeg = 108.0;      // motion blur amount = angle / 360
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double MinEv100 = 2.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double MaxEv100 = 11.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double AdaptSpeedUp = 1.5;         // [EV/s]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double AdaptSpeedDown = 0.7;       // [EV/s]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure") double ExposureCompensation = 0.25;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor") double GrainG0 = 0.015;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor") double GrainMax = 0.06;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor") double Vignette = 0.1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion") double HeadTranslationScale = 0.3;   // Eyes 0.3, Headcam 1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double EyeBehindTipM = 0.45;      // s_e (plan 4.2)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double EyeAboveCueM = 0.10;       // h_c
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double VisionCenterM = 0.0;       // y_vc (calibrated)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double StandingEyeHeightM = 1.65;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement") double GetDownSeconds = 1.0;      // transition 0.8-1.5 s
};

UCLASS(BlueprintType)
class RAWBREAK_API URbCameraModel : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Camera")
	FRbCameraPresetParams Eyes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Camera")
	FRbCameraPresetParams Headcam;

	const FRbCameraPresetParams& Get(ERbCameraPreset Preset) const;
};

namespace RbCameraModel
{
	// Compiled-in defaults of plan 4.9 (Broadcast = replay cameras, long lens).
	RAWBREAK_API FRbCameraPresetParams Defaults(ERbCameraPreset Preset);
}
