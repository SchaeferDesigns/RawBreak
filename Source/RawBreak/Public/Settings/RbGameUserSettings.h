#pragma once

// User settings (Docs/ue-architecture.md 8.1): the RAW BREAK quality preset on top of UE scalability, camera preset
// and comfort options. QUALITY FIRST (ue5-realism-plan 9.1, decisions 2026-09-27): the preset only fills in defaults
// for every individually adjustable option; Epic / Cinematic are never capped to the dev PC. Registered through
// [/Script/Engine.Engine] GameUserSettingsClassName. M1: storage + apply of the camera preset; the full preset
// matrix of plan 9.4 is a later package (UE-8 prepares the hook). Owner: UE-8.

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"

#include "Core/RbTypes.h"

#include "RbGameUserSettings.generated.h"

UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class RAWBREAK_API URbGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static URbGameUserSettings* Get();

	// Applies the preset: UE scalability groups (SetOverallScalabilityLevel for Low..Epic, Cinematic = 4 + extras)
	// plus RAW BREAK's own rows of plan 9.4 (reflections mode, ball SSS/haze, texture pool, hero texture size, path
	// tracer availability). Custom leaves the individual values alone.
	void ApplyQualityPreset(ERbQualityPreset Preset);

	UPROPERTY(config) ERbQualityPreset QualityPreset = ERbQualityPreset::High;
	UPROPERTY(config) ERbCameraPreset CameraPreset = ERbCameraPreset::Eyes;
	UPROPERTY(config) float VerticalFovDeg = 50.0f;      // 40-75 (plan 4.3)
	UPROPERTY(config) float MouseDpi = 800.0f;           // stroke mapping (plan 5.4)
	UPROPERTY(config) bool bHardcoreStroke = false;      // any tip contact is a shot (plan 14 Q2)
	UPROPERTY(config) bool bReducedMotion = false;       // comfort master switch (plan 4.8)
	UPROPERTY(config) float HeadBobScale = 1.0f;
	UPROPERTY(config) float MotionBlurScale = 1.0f;
	UPROPERTY(config) float GrainScale = 1.0f;
	UPROPERTY(config) bool bDepthOfField = true;
};
