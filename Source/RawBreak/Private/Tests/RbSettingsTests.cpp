// Quality presets and user settings (Docs/ue-architecture.md 8.1, 13 UE-8; ue5-realism-plan 9.4). Owner: UE-8.
//   RawBreak.Unit.Settings.PresetLevels     every preset fills every option (Low 0 .. Cinematic 4), screen percentages, Custom
//   RawBreak.Unit.Settings.Rows_R03         applying a preset sets the renderer: hit-lit reflections from High up (review R-03),
//                                           surface cache on Low / Medium, Lumen Lite on Low, texture pools, row clean-up
//   RawBreak.Unit.Settings.Persistence      the config properties survive a save / load round trip; Custom keeps its options
//   RawBreak.Unit.Settings.DefaultsHigh     a fresh settings object is High (M1 default) with the Eyes camera
// The renderer tests change global console variables of the editor process and restore them at the end.

#include "Settings/RbGameUserSettings.h"
#include "Tests/RbTestFlags.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Scalability.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbSettingsTestsPrivate
{
	int32 CVarInt(const TCHAR* Name)
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetInt() : INT32_MIN;
	}

	float CVarFloat(const TCHAR* Name)
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetFloat() : -1.0f;
	}

	// Restores the editor's scalability state and removes the RAW BREAK rows when a test ends.
	struct FScalabilityGuard
	{
		Scalability::FQualityLevels Saved = Scalability::GetQualityLevels();
		~FScalabilityGuard()
		{
			URbGameUserSettings::RemoveQualityRows();
			Scalability::SetQualityLevels(Saved, true);
		}
	};

	URbGameUserSettings* NewSettings()
	{
		URbGameUserSettings* S = NewObject<URbGameUserSettings>(GetTransientPackage());
		S->SetToDefaults();
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSettingsPresetLevels, "RawBreak.Unit.Settings.PresetLevels", RB_UNIT_TEST_FLAGS)
bool FRbSettingsPresetLevels::RunTest(const FString& Parameters)
{
	using namespace RbSettingsTestsPrivate;
	FScalabilityGuard Guard;
	URbGameUserSettings* S = NewSettings();
	const ERbQualityPreset Presets[] = {ERbQualityPreset::Low, ERbQualityPreset::Medium, ERbQualityPreset::High, ERbQualityPreset::Epic, ERbQualityPreset::Cinematic};
	const float Screen[] = {55.0f, 58.328f, 66.662f, 66.662f, 100.0f}; // plan 9.4 upscaler row
	for (int32 Level = 0; Level < 5; ++Level)
	{
		TestEqual(TEXT("preset level"), URbGameUserSettings::PresetLevel(Presets[Level]), Level);
		S->ApplyQualityPreset(Presets[Level]);
		TestTrue(TEXT("preset stored"), S->GetQualityPreset() == Presets[Level]);
		for (int32 Option = 0; Option < static_cast<int32>(ERbQualityOption::Count); ++Option)
		{
			TestEqual(FString::Printf(TEXT("%s at the preset level %d"), URbGameUserSettings::OptionName(static_cast<ERbQualityOption>(Option)), Level),
				S->GetQualityOption(static_cast<ERbQualityOption>(Option)), Level);
		}
		TestNearlyEqual(TEXT("screen percentage of the preset (DefaultScalability.ini)"), S->GetScreenPercentage(), Screen[Level], 1e-3f);
		TestEqual(TEXT("row section naming (Cinematic = @Cine)"), URbGameUserSettings::RowSection(ERbQualityOption::Reflections, Level),
			Level == 4 ? FString(TEXT("RawBreak.ReflectionQuality@Cine")) : FString::Printf(TEXT("RawBreak.ReflectionQuality@%d"), Level));
	}
	TestEqual(TEXT("Custom has no level"), URbGameUserSettings::PresetLevel(ERbQualityPreset::Custom), -1);

	// Individual options: setting one makes the preset Custom, and Custom keeps every individual value.
	S->ApplyQualityPreset(ERbQualityPreset::High);
	S->SetQualityOption(ERbQualityOption::Shadows, 0);
	S->SetQualityOption(ERbQualityOption::VolumetricFog, 4);
	TestTrue(TEXT("an individual option -> Custom"), S->GetQualityPreset() == ERbQualityPreset::Custom);
	S->ApplyQualityPreset(ERbQualityPreset::Custom);
	TestEqual(TEXT("Custom keeps shadows 0"), S->GetQualityOption(ERbQualityOption::Shadows), 0);
	TestEqual(TEXT("Custom keeps fog 4"), S->GetQualityOption(ERbQualityOption::VolumetricFog), 4);
	TestEqual(TEXT("Custom keeps reflections 2"), S->GetQualityOption(ERbQualityOption::Reflections), 2);
	S->SetScreenPercentage(100.0f);
	TestNearlyEqual(TEXT("DLAA-style 100 % as an individual choice"), S->GetScreenPercentage(), 100.0f, 1e-4f);
	S->SetQualityOption(ERbQualityOption::Textures, 9);
	TestEqual(TEXT("levels clamp to 0..4"), S->GetQualityOption(ERbQualityOption::Textures), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSettingsRows, "RawBreak.Unit.Settings.Rows_R03", RB_UNIT_TEST_FLAGS)
bool FRbSettingsRows::RunTest(const FString& Parameters)
{
	using namespace RbSettingsTestsPrivate;
	FScalabilityGuard Guard;
	const int32 WarningsAtStart = ExecutionInfo.GetWarningTotal();
	URbGameUserSettings* S = NewSettings();
	const TCHAR* LightingMode = TEXT("r.Lumen.HardwareRayTracing.LightingMode");
	const TCHAR* HitLighting = TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed");
	TestTrue(TEXT("the rows name the hit-lighting variables"), URbGameUserSettings::RowConsoleVariables().Contains(LightingMode) &&
		URbGameUserSettings::RowConsoleVariables().Contains(HitLighting));

	S->ApplyQualityPreset(ERbQualityPreset::High);
	TestEqual(TEXT("High: sg.ReflectionQuality 2"), CVarInt(TEXT("sg.ReflectionQuality")), 2);
	TestEqual(TEXT("High: Hit Lighting for Reflections (R-03)"), CVarInt(LightingMode), 2);
	TestEqual(TEXT("High: hit lighting allowed although the engine GI group forbids it below Epic"), CVarInt(HitLighting), 1);
	TestEqual(TEXT("High: HWRT Lumen reflections"), CVarInt(TEXT("r.Lumen.Reflections.Allow")), 1);
	TestEqual(TEXT("High: screen probe gather (HWRT Lumen GI)"), CVarInt(TEXT("r.Lumen.FinalGatherMethod")), 1);
	TestEqual(TEXT("High: texture pool 2500 MB"), CVarInt(TEXT("r.Streaming.PoolSize")), 2500);
	TestEqual(TEXT("High: 16x anisotropy (grazing cloth)"), CVarInt(TEXT("r.MaxAnisotropy")), 16);
	TestEqual(TEXT("High: volumetric fog on"), CVarInt(TEXT("r.VolumetricFog")), 1);
	TestNearlyEqual(TEXT("High: DLSS-Quality internal resolution"), CVarFloat(TEXT("r.ScreenPercentage")), 66.662f, 1e-3f);

	S->ApplyQualityPreset(ERbQualityPreset::Medium);
	TestEqual(TEXT("Medium: surface-cache lighting (lowered, R-03)"), CVarInt(LightingMode), 0);
	TestEqual(TEXT("Medium: no hit lighting"), CVarInt(HitLighting), 0);
	TestEqual(TEXT("Medium: HWRT Lumen reflections (plan 9.4, engine: SSR)"), CVarInt(TEXT("r.Lumen.Reflections.Allow")), 1);
	TestEqual(TEXT("Medium: Lumen Lite (irradiance field gather)"), CVarInt(TEXT("r.Lumen.FinalGatherMethod")), 0);
	TestEqual(TEXT("Medium: texture pool 1500 MB"), CVarInt(TEXT("r.Streaming.PoolSize")), 1500);

	S->ApplyQualityPreset(ERbQualityPreset::Low);
	TestEqual(TEXT("Low: surface cache"), CVarInt(LightingMode), 0);
	TestEqual(TEXT("Low: SSR (no Lumen reflections)"), CVarInt(TEXT("r.Lumen.Reflections.Allow")), 0);
	TestEqual(TEXT("Low: Lumen Lite GI on (engine: off)"), CVarInt(TEXT("r.Lumen.DiffuseIndirect.Allow")), 1);
	TestEqual(TEXT("Low: irradiance field gather"), CVarInt(TEXT("r.Lumen.FinalGatherMethod")), 0);
	TestEqual(TEXT("Low: volumetric fog off"), CVarInt(TEXT("r.VolumetricFog")), 0);
	TestEqual(TEXT("Low: texture pool 1000 MB"), CVarInt(TEXT("r.Streaming.PoolSize")), 1000);
	TestNearlyEqual(TEXT("Low: ~55 % internal resolution"), CVarFloat(TEXT("r.ScreenPercentage")), 55.0f, 1e-3f);

	S->ApplyQualityPreset(ERbQualityPreset::Epic);
	TestEqual(TEXT("Epic: hit lighting"), CVarInt(LightingMode), 2);
	TestEqual(TEXT("Epic: texture pool 3500 MB (not capped to the 8 GB dev GPU)"), CVarInt(TEXT("r.Streaming.PoolSize")), 3500);

	const float HighLocalBias = [&]()
	{
		S->ApplyQualityPreset(ERbQualityPreset::High);
		return CVarFloat(TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal"));
	}();
	S->ApplyQualityPreset(ERbQualityPreset::Cinematic);
	TestEqual(TEXT("Cinematic: sg level 4"), CVarInt(TEXT("sg.ShadowQuality")), 4);
	TestEqual(TEXT("Cinematic: hit lighting"), CVarInt(LightingMode), 2);
	TestEqual(TEXT("Cinematic: texture pool 4500 MB"), CVarInt(TEXT("r.Streaming.PoolSize")), 4500);
	TestNearlyEqual(TEXT("Cinematic: sharper key-lamp shadow pages"), CVarFloat(TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal")), -0.5f, 1e-4f);
	TestNearlyEqual(TEXT("Cinematic: native internal resolution (DLAA)"), CVarFloat(TEXT("r.ScreenPercentage")), 100.0f, 1e-3f);
	S->ApplyQualityPreset(ERbQualityPreset::High);
	TestNearlyEqual(TEXT("a row set only at Cinematic does not stick when going back to High"), CVarFloat(TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal")),
		HighLocalBias, 1e-4f);

	// A Custom mix: the reflection row follows the reflection option, not the preset.
	S->SetQualityOption(ERbQualityOption::GlobalIllumination, 1);
	S->SetQualityOption(ERbQualityOption::Reflections, 3);
	S->ApplyQualityRows();
	TestEqual(TEXT("Custom GI Medium + reflections Epic: hit lighting stays allowed"), CVarInt(HitLighting), 1);
	TestEqual(TEXT("Custom GI Medium + reflections Epic: LightingMode 2"), CVarInt(LightingMode), 2);
	S->SetQualityOption(ERbQualityOption::Reflections, 1);
	S->ApplyQualityRows();
	TestEqual(TEXT("Custom reflections Medium: LightingMode 0"), CVarInt(LightingMode), 0);

	// Switching presets never writes a group value under a live row (LogConsoleManager "was ignored as it is lower priority").
	TestEqual(TEXT("preset switches log no console-variable priority warnings"), ExecutionInfo.GetWarningTotal(), WarningsAtStart);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSettingsPersistence, "RawBreak.Unit.Settings.Persistence", RB_UNIT_TEST_FLAGS)
bool FRbSettingsPersistence::RunTest(const FString& Parameters)
{
	using namespace RbSettingsTestsPrivate;
	FScalabilityGuard Guard;
	const FString Ini = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RbTests"), TEXT("RbSettingsPersistence.ini")));
	IFileManager::Get().Delete(*Ini, false, true, true);

	URbGameUserSettings* A = NewSettings();
	A->ApplyQualityPreset(ERbQualityPreset::Epic);
	A->SetQualityOption(ERbQualityOption::VolumetricFog, 1); // -> Custom
	A->CameraPreset = ERbCameraPreset::Headcam;
	A->VerticalFovDeg = 62.5f;
	A->MouseDpi = 1600.0f;
	A->bHardcoreStroke = true;
	A->bReducedMotion = true;
	A->HeadBobScale = 0.25f;
	A->MotionBlurScale = 0.5f;
	A->GrainScale = 0.0f;
	A->bDepthOfField = false;
	A->SaveConfig(CPF_Config, *Ini);
	GConfig->Flush(false, Ini);
	TestTrue(TEXT("settings file written"), IFileManager::Get().FileExists(*Ini));

	URbGameUserSettings* B = NewObject<URbGameUserSettings>(GetTransientPackage());
	B->SetToDefaults();
	B->LoadConfig(URbGameUserSettings::StaticClass(), *Ini);
	TestTrue(TEXT("preset Custom persists"), B->QualityPreset == ERbQualityPreset::Custom);
	TestEqual(TEXT("fog option persists"), B->VolumetricFogQuality, 1);
	TestTrue(TEXT("camera preset persists"), B->CameraPreset == ERbCameraPreset::Headcam);
	TestNearlyEqual(TEXT("FOV persists"), B->VerticalFovDeg, 62.5f, 1e-6f);
	TestNearlyEqual(TEXT("DPI persists"), B->MouseDpi, 1600.0f, 1e-6f);
	TestTrue(TEXT("stroke / comfort flags persist"), B->bHardcoreStroke && B->bReducedMotion && !B->bDepthOfField);
	TestNearlyEqual(TEXT("head bob persists"), B->HeadBobScale, 0.25f, 1e-6f);
	TestNearlyEqual(TEXT("motion blur persists"), B->MotionBlurScale, 0.5f, 1e-6f);
	TestNearlyEqual(TEXT("grain persists"), B->GrainScale, 0.0f, 1e-6f);

	// A named preset loaded from the file is authoritative for every option (LoadSettings / ApplyNonResolutionSettings re-derive
	// the options from it; the same path as ApplyQualityPreset).
	URbGameUserSettings* C = NewSettings();
	C->ApplyQualityPreset(ERbQualityPreset::Low);
	C->SaveConfig(CPF_Config, *Ini);
	C->LoadConfig(URbGameUserSettings::StaticClass(), *Ini);
	C->ApplyQualityPreset(C->GetQualityPreset());
	TestEqual(TEXT("Low round trip: reflections 0"), C->GetQualityOption(ERbQualityOption::Reflections), 0);
	TestEqual(TEXT("Low round trip: fog 0"), C->VolumetricFogQuality, 0);

	// Validation clamps out-of-range values.
	B->VerticalFovDeg = 120.0f;
	B->VolumetricFogQuality = 7;
	B->ValidateSettings();
	TestNearlyEqual(TEXT("FOV clamped to 40..75"), B->VerticalFovDeg, 75.0f, 1e-6f);
	TestEqual(TEXT("fog level clamped"), B->VolumetricFogQuality, 4);

	GConfig->Remove(Ini);
	IFileManager::Get().Delete(*Ini, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSettingsDefaults, "RawBreak.Unit.Settings.DefaultsHigh", RB_UNIT_TEST_FLAGS)
bool FRbSettingsDefaults::RunTest(const FString& Parameters)
{
	using namespace RbSettingsTestsPrivate;
	URbGameUserSettings* S = RbSettingsTestsPrivate::NewSettings();
	TestTrue(TEXT("M1 default preset High (3070 Ti test tier)"), S->GetQualityPreset() == ERbQualityPreset::High);
	TestEqual(TEXT("every option at High"), S->GetQualityOption(ERbQualityOption::Reflections), 2);
	TestNearlyEqual(TEXT("DLSS-Quality internal resolution"), S->GetScreenPercentage(), 66.662f, 1e-3f);
	TestTrue(TEXT("Eyes camera, V = 50 deg"), S->CameraPreset == ERbCameraPreset::Eyes && FMath::IsNearlyEqual(S->VerticalFovDeg, 50.0f));
	TestTrue(TEXT("registered as the engine's game user settings class"),
		GetDefault<UEngine>()->GameUserSettingsClassName.ToString() == TEXT("/Script/RawBreak.RbGameUserSettings"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
