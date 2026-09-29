#pragma once

// User settings (Docs/ue-architecture.md 8.1): the RAW BREAK quality preset on top of UE scalability, camera preset and
// comfort options. QUALITY FIRST (ue5-realism-plan 9.1, decisions 2026-09-27): a preset only fills in defaults for every
// individually adjustable option; Epic / Cinematic are authored for the best image and never capped to the dev PC (the
// RTX 3070 Ti is the High test tier). Registered through [/Script/Engine.Engine] GameUserSettingsClassName. Owner: UE-8.
//
// How a preset plugs in (plan 9.4):
//   1. the UE scalability groups (sg.*): every group at the preset level, Low 0 .. Cinematic 4 (= UE's Cine level, "Epic +
//      extras"); Config/DefaultScalability.ini overrides the engine's group sections where the plan differs (Lumen Lite on
//      Low, VSM shadows kept on Low, HWRT surface-cache reflections on Medium, texture pools 1000-4500 MB, 16x anisotropy
//      for the grazing cloth);
//   2. the resolution (upscaler input) from [RawBreak.Preset@<level>] ScreenPercentage (TSR now, DLSS later);
//   3. the RAW BREAK rows [RawBreak.<Option>@<level>] of DefaultScalability.ini, applied at ECVF_SetByGameOverride after the
//      groups: they follow each option's own level (so a Custom mix stays consistent) and outrank project settings, e.g.
//      r.Lumen.HardwareRayTracing.LightingMode (2 = hit-lit reflections from High up, DefaultEngine.ini) is lowered on Low /
//      Medium (review R-03), and the engine's GI group, which forbids hit lighting below Epic, cannot switch it off on High.
// Every option is individually adjustable (SetQualityOption -> preset Custom); Custom leaves the individual values alone.
// Engine groups persist through the engine ([ScalabilityGroups] of GameUserSettings.ini), everything else through the
// config properties below. Console: rb.Quality <Low|Medium|High|Epic|Cinematic|Custom> [save], rb.Quality.Option <option>
// <0-4>, rb.Quality.Dump.

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"

#include "Core/RbTypes.h"
#include "Settings/RbSettingsTypes.h"

#include "RbGameUserSettings.generated.h"

// The individually adjustable quality options: the UE scalability groups plus RAW BREAK's own rows.
UENUM(BlueprintType)
enum class ERbQualityOption : uint8
{
	ViewDistance,
	AntiAliasing,
	Shadows,
	GlobalIllumination, // Lumen Lite (Low / Medium) .. Lumen HWRT max (Cinematic)
	Reflections,        // SSR (Low), HWRT surface cache (Medium), HWRT hit lighting (High+)
	PostProcess,
	Textures,           // texture pool 1000 .. 4500 MB, anisotropy
	Effects,            // material quality (ball haze / SSS lobes via Quality Switch)
	Foliage,
	Shading,
	VolumetricFog,      // RAW BREAK row: off / low / on / on / high
	Count UMETA(Hidden)
};

UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class RAWBREAK_API URbGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	URbGameUserSettings();

	static URbGameUserSettings* Get();

	// Applies the preset: fills every option from it (Custom keeps the individual values) and applies the groups and the RAW
	// BREAK rows at once (not saved: call SaveSettings).
	void ApplyQualityPreset(ERbQualityPreset Preset);

	ERbQualityPreset GetQualityPreset() const { return QualityPreset; }

	// Individual options, level 0..4 (Low..Cinematic). Setting one makes the preset Custom; ApplyQualityRows applies them.
	void SetQualityOption(ERbQualityOption Option, int32 Level);
	int32 GetQualityOption(ERbQualityOption Option) const;

	// Internal resolution [% of the output] the upscaler starts from (25..200); makes the preset Custom.
	void SetScreenPercentage(float Percent);
	float GetScreenPercentage() const;

	// Applies the current options: the UE scalability groups, then the RAW BREAK rows (ECVF_SetByGameOverride).
	void ApplyQualityRows();

	// Low 0, Medium 1, High 2, Epic 3, Cinematic 4 (Custom: -1).
	static int32 PresetLevel(ERbQualityPreset Preset);

	// Screen percentage of a preset level ([RawBreak.Preset@<level>] ScreenPercentage; plan 9.4 upscaler row as fallback).
	static float PresetScreenPercentage(int32 Level);

	// Ini section of a RAW BREAK row: RawBreak.<Group>@<level> (level 4 = @Cine, like the engine's groups).
	static FString RowSection(ERbQualityOption Option, int32 Level);
	static const TCHAR* OptionName(ERbQualityOption Option);

	// Every console variable any RAW BREAK row sets.
	static TArray<FString> RowConsoleVariables();

	// Removes the RAW BREAK rows (unsets their ECVF_SetByGameOverride values; the scalability / project values return).
	static void RemoveQualityRows();

	// UGameUserSettings
	virtual void SetToDefaults() override;
	virtual void LoadSettings(bool bForceReload = false) override;
	virtual void ApplyNonResolutionSettings() override;
	virtual void ValidateSettings() override;

	UPROPERTY(config) ERbQualityPreset QualityPreset = ERbQualityPreset::High;
	UPROPERTY(config) int32 VolumetricFogQuality = 2; // RAW BREAK row level (0..4)

	UPROPERTY(config) ERbCameraPreset CameraPreset = ERbCameraPreset::Eyes;
	UPROPERTY(config) float VerticalFovDeg = 50.0f;      // 40-75 (plan 4.3 / 4.9)
	UPROPERTY(config) float MouseDpi = 800.0f;           // stroke mapping (plan 5.4)
	UPROPERTY(config) bool bHardcoreStroke = false;      // any tip contact is a shot (plan 14 Q2)
	UPROPERTY(config) bool bReducedMotion = false;       // comfort master switch (plan 4.8)
	UPROPERTY(config) float HeadBobScale = 1.0f;
	UPROPERTY(config) float MotionBlurScale = 1.0f;
	UPROPERTY(config) float GrainScale = 1.0f;
	UPROPERTY(config) bool bDepthOfField = true;

	// --- M2 additions (Docs/ue-architecture.md 18.4; the settings API: owner M2-D, readers M2-F / M2-C) ------------------
	// Engine rows used by the M2 settings menu: window mode / resolution (UGameUserSettings::FullscreenMode, ResolutionSizeX/Y,
	// SetScreenResolution + ApplyResolutionSettings), frame cap (FrameRateLimit), v-sync.
	UPROPERTY(config) FRbControlSettings Controls;
	UPROPERTY(config) FRbCameraSettings Camera;
	UPROPERTY(config) FRbAudioVolumes Volumes;
	// Contextual key hints (get down, stroke, place the cue ball ...; the M2 stand-in for ui-ux 3.5 prompts).
	UPROPERTY(config) bool bShowKeyHints = true;

	// Broadcast after ApplyNonResolutionSettings (ApplySettings, startup) and by the settings menu after every live change.
	// Readers re-read the fields above; never cache them across a broadcast.
	static FSimpleMulticastDelegate& OnSettingsChanged();
	void NotifySettingsChanged();

protected:
	// Fills ScalabilityQuality and the RAW BREAK option levels from a preset level.
	void FillFromPresetLevel(int32 Level);
};
