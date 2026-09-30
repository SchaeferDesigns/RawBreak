#pragma once

// Player-facing setting groups added in M2 (Docs/ue-architecture.md 18.4, ui-ux 13.5-13.8, playtest 2026-09-28 P3). They are
// config structs of URbGameUserSettings (GameUserSettings.ini, section [/Script/RawBreak.RbGameUserSettings]).
//
// THE settings API between the M2 packages: M2-D owns the storage, validation, the settings menu and persistence; every other
// package only READS the values (URbGameUserSettings::Get()->Controls / ->Volumes) and re-reads them when
// URbGameUserSettings::OnSettingsChanged() fires. Field names are frozen for M2 (additions allowed, no renames); the defaults
// below are the playtest-derived values and belong to the reading package's acceptance (M2-F aim / look, M2-C volumes).

#include "CoreMinimal.h"

#include "RbSettingsTypes.generated.h"

// Mouse look / aim / stroke (ui-ux 13.6). Speeds are defined per CENTIMETRE of mouse travel so they do not depend on the mouse's
// DPI: counts -> cm with URbGameUserSettings::MouseDpi (2.54 / DPI cm per count). Consumer: M2-F (Input / Player / Camera).
USTRUCT(BlueprintType)
struct FRbControlSettings
{
	GENERATED_BODY()

	// Coarse aim down on the shot [deg of cue azimuth per cm of mouse travel] at AimSensitivity 1. Playtest P3: 90 deg in
	// 10-15 cm at normal mouse speed; 7.2 deg/cm = 90 deg per 12.5 cm (ui-ux 13.6: ~0.023 deg per count at 800 DPI).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	float AimDegreesPerCm = 7.2f;

	// Multiplier of the coarse aim (settings slider 0.1 - 5.0).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	float AimSensitivity = 1.0f;

	// Fine aim (Shift held) = coarse x this factor (0.03 - 0.5). Playtest P3: 10-20x slower; 0.075 = 13.3x.
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	float FineAimFactor = 0.075f;

	// Optional aim acceleration (0 = linear, 1 = the full curve M2-F defines: slow hand motion finer, fast motion coarser).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	float AimAcceleration = 0.0f;

	// Head / body look while standing [deg of view per cm of mouse travel] at LookSensitivity 1 (ui-ux 13.6: ~0.07 deg per
	// count at 800 DPI = 22 deg/cm).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	float LookDegreesPerCm = 22.0f;

	// Multiplier of the standing look (0.1 - 5.0).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	float LookSensitivity = 1.0f;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	bool bInvertLookY = false;

	// Stroke sensitivity (0.5 - 2.0): scales the hand metres per mouse count relative to MouseDpi (ui-ux 13.6).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Controls")
	float StrokeSensitivity = 1.0f;
};

// Get-down / stand-up transition style (ui-ux 13.5): Natural = the human-motion posture change of M2-F (0.8-1.5 s, seeded
// variation, overshoot and settle), Quick = a short plain ease (~0.4 s, no overshoot), Cut = instant. Reduced motion selects Quick.
UENUM(BlueprintType)
enum class ERbPostureTransition : uint8
{
	Natural,
	Quick,
	Cut,
};

// Camera / comfort rows added in M2 (ui-ux 13.5; ue5-realism-plan 4.8). The existing rows stay where they are in
// URbGameUserSettings (CameraPreset, VerticalFovDeg, HeadBobScale = walking bob, MotionBlurScale, GrainScale, bDepthOfField,
// bReducedMotion). Consumer: M2-F (URbCameraRigComponent / FRbHumanMotion).
USTRUCT(BlueprintType)
struct FRbCameraSettings
{
	GENERATED_BODY()

	// Breathing, postural sway and the small reactions of the human-motion layer (0..1; ui-ux 13.5 "Body sway & breathing").
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	float BodySwayScale = 1.0f;

	// Headcam mount jitter (0..1; Headcam only).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	float MountShakeScale = 1.0f;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Camera")
	ERbPostureTransition PostureTransition = ERbPostureTransition::Natural;
};

// --- M2-D additions (storage only; readers never need them) ------------------------------------------------------------

// The rows Reduced motion overrides (ui-ux 13.10, M2 subset) as they were before it was switched on, so switching it off
// restores exactly them (UX-T20). Persisted with the settings: a restart while Reduced motion is on still restores them later.
USTRUCT()
struct FRbReducedMotionBackup
{
	GENERATED_BODY()

	UPROPERTY(config) bool bValid = false;
	UPROPERTY(config) float HeadBobScale = 1.0f;
	UPROPERTY(config) float BodySwayScale = 1.0f;
	UPROPERTY(config) float MountShakeScale = 1.0f;
	UPROPERTY(config) float MotionBlurScale = 1.0f;
	UPROPERTY(config) ERbPostureTransition PostureTransition = ERbPostureTransition::Natural;
};

// The individual quality levels of a Custom mix (the UE scalability groups live in the engine's own [ScalabilityGroups] state,
// which the editor saves to a different ini): stored with the RAW BREAK settings so a Custom mix survives a restart as it was.
USTRUCT()
struct FRbCustomQualityLevels
{
	GENERATED_BODY()

	UPROPERTY(config) bool bValid = false;
	UPROPERTY(config) int32 ViewDistance = 2;
	UPROPERTY(config) int32 AntiAliasing = 2;
	UPROPERTY(config) int32 Shadows = 2;
	UPROPERTY(config) int32 GlobalIllumination = 2;
	UPROPERTY(config) int32 Reflections = 2;
	UPROPERTY(config) int32 PostProcess = 2;
	UPROPERTY(config) int32 Textures = 2;
	UPROPERTY(config) int32 Effects = 2;
	UPROPERTY(config) int32 Foliage = 2;
	UPROPERTY(config) int32 Shading = 2;
	UPROPERTY(config) float ScreenPercentage = 66.662f;
};

// Volumes 0..1 per category (ui-ux 13.8, audio.md 7.1 submixes). Consumer: M2-C (URbAudioSubsystem).
USTRUCT(BlueprintType)
struct FRbAudioVolumes
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Audio")
	float Master = 1.0f;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Audio")
	float Music = 0.7f;

	// Table & balls (the physics-driven sounds).
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Audio")
	float Table = 1.0f;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Audio")
	float Ambience = 0.8f;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Audio")
	float Voices = 1.0f;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Audio")
	float Interface = 0.6f;
};
