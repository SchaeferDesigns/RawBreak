// Quality presets and user settings (Docs/ue-architecture.md 8.1, 13 UE-8; ue5-realism-plan 9.4). Owner: UE-8; since M2 M2-D (the
// registry tests at the end of the file).
//   RawBreak.Unit.Settings.PresetLevels     every preset fills every option (Low 0 .. Cinematic 4), screen percentages, Custom
//   RawBreak.Unit.Settings.Rows_R03         applying a preset sets the renderer: hit-lit reflections from High up (review R-03),
//                                           surface cache on Low / Medium, Lumen Lite on Low, shadows on Low, texture pools, row clean-up
//   RawBreak.Unit.Settings.Persistence      the config properties survive a save / load round trip; Custom keeps its options
//   RawBreak.Unit.Settings.DefaultsHigh     a fresh settings object is High (M1 default) with the Eyes camera
// The renderer tests change global console variables of the editor process and restore them at the end.

#include "Settings/RbGameUserSettings.h"
#include "Settings/RbSettingsRegistry.h"
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
	TestTrue(TEXT("Medium: VSM page pool for the room's 14 shadowed lights (>= 1024 pages)"), CVarInt(TEXT("r.Shadow.Virtual.MaxPhysicalPages")) >= 1024);

	S->ApplyQualityPreset(ERbQualityPreset::Low);
	TestEqual(TEXT("Low: surface cache"), CVarInt(LightingMode), 0);
	TestEqual(TEXT("Low: SSR (no Lumen reflections)"), CVarInt(TEXT("r.Lumen.Reflections.Allow")), 0);
	TestEqual(TEXT("Low: Lumen Lite GI on (engine: off)"), CVarInt(TEXT("r.Lumen.DiffuseIndirect.Allow")), 1);
	TestEqual(TEXT("Low: irradiance field gather"), CVarInt(TEXT("r.Lumen.FinalGatherMethod")), 0);
	TestEqual(TEXT("Low: volumetric fog off"), CVarInt(TEXT("r.VolumetricFog")), 0);
	TestEqual(TEXT("Low: texture pool 1000 MB"), CVarInt(TEXT("r.Streaming.PoolSize")), 1000);
	// Plan 9.4 Low = VSM at a lower resolution, not the engine's Low (r.ShadowQuality 0 = no dynamic shadows: floating balls).
	TestTrue(TEXT("Low: dynamic shadows on (plan 9.4)"), CVarInt(TEXT("r.ShadowQuality")) > 0);
	TestEqual(TEXT("Low: local-light VSM one level coarser"), CVarInt(TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal")), 1);
	TestTrue(TEXT("Low: VSM page pool for the room's 14 shadowed lights (>= 1024 pages)"), CVarInt(TEXT("r.Shadow.Virtual.MaxPhysicalPages")) >= 1024);
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

// ---------------------------------------------------------------------------------------------------------------------------
// M2-D (Docs/ue-architecture.md 18.4): the settings registry.
//   RawBreak.Unit.Settings.Registry      UX-T01: the 18.4 rows (ids, EN labels / descriptions / values, ranges and defaults),
//                                        default == fresh value, Set -> Get round trip of every value, save -> reload of the
//                                        ini with every row changed (Custom mix and Reduced motion backup included)
//   RawBreak.Unit.Settings.Presets       UX-T02: each preset sets every preset-driven row; one row -> "Custom (based on ...)";
//                                        High reads back hit-lit reflections, a GI-only change leaves them; Epic / Cinematic
//                                        material quality 1; Low has SSR; presets never touch the other rows
//   RawBreak.Unit.Settings.ReducedMotion UX-T20: switching it on sets exactly its rows, off restores exactly the previous values
//                                        (also after a save / reload); the rows stay editable
// ---------------------------------------------------------------------------------------------------------------------------

namespace RbSettingsTestsPrivate
{
	// Every row's stored value.
	TMap<FName, double> Snapshot(const URbGameUserSettings& S)
	{
		TMap<FName, double> Out;
		for (const FRbSettingDef& Row : FRbSettingsRegistry::Rows())
		{
			Out.Add(Row.Id, FRbSettingsRegistry::GetValue(Row, S));
		}
		return Out;
	}

	// Ids whose value differs between two snapshots (relative tolerance for floats).
	TArray<FName> Changed(const TMap<FName, double>& A, const TMap<FName, double>& B)
	{
		TArray<FName> Out;
		for (const TPair<FName, double>& Entry : A)
		{
			const double* Other = B.Find(Entry.Key);
			if (!Other || !FMath::IsNearlyEqual(Entry.Value, *Other, 1e-5 * FMath::Max(1.0, FMath::Abs(Entry.Value))))
			{
				Out.Add(Entry.Key);
			}
		}
		return Out;
	}

	FString Join(const TArray<FName>& Ids)
	{
		TArray<FString> Names;
		for (const FName& Id : Ids)
		{
			Names.Add(Id.ToString());
		}
		Names.Sort();
		return FString::Join(Names, TEXT(", "));
	}

	// Candidate values of a row for the round trip: every enum / toggle value, else min, max, default and a snapped mid value.
	TArray<double> Candidates(const FRbSettingDef& Row, const URbGameUserSettings& S)
	{
		TArray<double> Out;
		if (Row.Type == ERbSettingType::Enum || Row.Type == ERbSettingType::Bool)
		{
			for (int32 Index = 0; Index < FRbSettingsRegistry::NumEnumValues(Row, S); ++Index)
			{
				Out.Add(Index);
			}
			return Out;
		}
		const double Mid = Row.Min + FMath::RoundToDouble(0.37 * (Row.Max - Row.Min) / Row.Step) * Row.Step;
		Out = {Row.Min, Row.Max, FRbSettingsRegistry::DefaultValue(Row, S), Mid};
		return Out;
	}

	// A value different from the current one (the save / reload check changes every row).
	double OtherValue(const FRbSettingDef& Row, const URbGameUserSettings& S)
	{
		const double Current = FRbSettingsRegistry::GetValue(Row, S);
		if (Row.Type == ERbSettingType::Enum || Row.Type == ERbSettingType::Bool)
		{
			const int32 Count = FMath::Max(1, FRbSettingsRegistry::NumEnumValues(Row, S));
			return (FMath::RoundToInt(Current) + 1) % Count;
		}
		const double Mid = Row.Min + FMath::RoundToDouble(0.37 * (Row.Max - Row.Min) / Row.Step) * Row.Step;
		return FMath::IsNearlyEqual(Mid, Current, 1e-6) ? Row.Min + FMath::RoundToDouble(0.71 * (Row.Max - Row.Min) / Row.Step) * Row.Step : Mid;
	}

	FString TempIni(const TCHAR* Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RbTests"), Name));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSettingsRegistryTest, "RawBreak.Unit.Settings.Registry", RB_UNIT_TEST_FLAGS)
bool FRbSettingsRegistryTest::RunTest(const FString& Parameters)
{
	using namespace RbSettingsTestsPrivate;
	FScalabilityGuard Guard;

	// --- the rows of 18.4, in page order ------------------------------------------------------------------------------
	const TArray<FString> Expected = {
		TEXT("gfx.preset"), TEXT("gfx.globalIllumination"), TEXT("gfx.reflections"), TEXT("gfx.shadows"), TEXT("gfx.volumetricFog"), TEXT("gfx.textures"),
		TEXT("gfx.effects"), TEXT("gfx.antiAliasing"), TEXT("gfx.postProcess"), TEXT("gfx.shading"), TEXT("gfx.viewDistance"), TEXT("gfx.foliage"),
		TEXT("gfx.resolutionScale"), TEXT("gfx.depthOfField"), TEXT("gfx.motionBlur"), TEXT("gfx.filmGrain"),
		TEXT("dsp.windowMode"), TEXT("dsp.resolution"), TEXT("dsp.frameCap"), TEXT("dsp.vsync"),
		TEXT("cam.look"), TEXT("cam.fov"), TEXT("cam.bodySway"), TEXT("cam.headBob"), TEXT("cam.mountShake"), TEXT("cam.posture"), TEXT("cam.reducedMotion"),
		TEXT("ctl.mouseDpi"), TEXT("ctl.aimSpeed"), TEXT("ctl.fineAim"), TEXT("ctl.aimAcceleration"), TEXT("ctl.lookSensitivity"), TEXT("ctl.invertLookY"),
		TEXT("ctl.strokeSensitivity"), TEXT("ctl.keyHints"),
		TEXT("aud.master"), TEXT("aud.music"), TEXT("aud.table"), TEXT("aud.ambience"), TEXT("aud.voices"), TEXT("aud.interface")};
	TArray<FString> Ids;
	for (const FRbSettingDef& Row : FRbSettingsRegistry::Rows())
	{
		Ids.Add(Row.Id.ToString());
	}
	TestTrue(FString::Printf(TEXT("the 18.4 rows in page order (%d rows: %s)"), Ids.Num(), *FString::Join(Ids, TEXT(" "))), Ids == Expected);
	const TCHAR* Prefix[] = {TEXT("gfx."), TEXT("dsp."), TEXT("cam."), TEXT("ctl."), TEXT("aud.")};
	int32 QualityRows = 0;
	for (const FRbSettingDef& Row : FRbSettingsRegistry::Rows())
	{
		const FString Id = Row.Id.ToString();
		TestTrue(FString::Printf(TEXT("%s: on its page"), *Id), Id.StartsWith(Prefix[static_cast<int32>(Row.Page)]));
		TestNotNull(FString::Printf(TEXT("%s: Find"), *Id), FRbSettingsRegistry::Find(Row.Id));
		TestFalse(FString::Printf(TEXT("%s: EN label"), *Id), Row.Label.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s: EN description (a sentence)"), *Id), Row.Description.ToString().Len() >= 12);
		TestTrue(FString::Printf(TEXT("%s: getter and setter"), *Id), static_cast<bool>(Row.Get) && static_cast<bool>(Row.Set));
		TestTrue(FString::Printf(TEXT("%s: range"), *Id), Row.Min < Row.Max && Row.Step > 0.0);
		if (Row.Type == ERbSettingType::Enum && !Row.DynamicEnumLabels)
		{
			TestEqual(FString::Printf(TEXT("%s: one label per value"), *Id), Row.EnumLabels.Num(), FMath::RoundToInt(Row.Max) + 1);
		}
		if (Row.Type == ERbSettingType::Bool)
		{
			TestEqual(FString::Printf(TEXT("%s: Off / On"), *Id), Row.EnumLabels.Num(), 2);
		}
		if (Row.bPresetDriven)
		{
			for (int32 Level = 0; Level < 5; ++Level)
			{
				TestTrue(FString::Printf(TEXT("%s: preset value %d in range"), *Id, Level), Row.PresetValues[Level] >= Row.Min && Row.PresetValues[Level] <= Row.Max);
			}
			QualityRows += Row.Effect == ERbSettingEffect::QualityRows ? 1 : 0;
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s: default in range"), *Id), Row.Default >= Row.Min && Row.Default <= Row.Max);
		}
		for (const TCHAR* Letter : {TEXT("ä"), TEXT("ö"), TEXT("ü"), TEXT("ß")})
		{
			TestFalse(FString::Printf(TEXT("%s: EN only (M2)"), *Id), Row.Label.ToString().Contains(Letter) || Row.Description.ToString().Contains(Letter));
		}
	}
	TestEqual(TEXT("the eleven quality options + resolution scale are preset-driven"), QualityRows, 12);

	// --- defaults (18.4 table; RbSettingsTypes.h) ----------------------------------------------------------------------
	URbGameUserSettings* Fresh = NewSettings();
	const TMap<FName, double> Defaults = {
		{TEXT("gfx.preset"), 2.0}, {TEXT("gfx.resolutionScale"), 66.662}, {TEXT("gfx.depthOfField"), 1.0}, {TEXT("gfx.motionBlur"), 1.0}, {TEXT("gfx.filmGrain"), 1.0},
		{TEXT("dsp.windowMode"), static_cast<double>(EWindowMode::WindowedFullscreen)}, {TEXT("dsp.frameCap"), 7.0}, {TEXT("dsp.vsync"), 0.0},
		{TEXT("cam.look"), 0.0}, {TEXT("cam.fov"), 50.0}, {TEXT("cam.bodySway"), 1.0}, {TEXT("cam.headBob"), 1.0}, {TEXT("cam.mountShake"), 1.0},
		{TEXT("cam.posture"), 0.0}, {TEXT("cam.reducedMotion"), 0.0},
		{TEXT("ctl.mouseDpi"), 800.0}, {TEXT("ctl.aimSpeed"), 1.0}, {TEXT("ctl.fineAim"), 0.075}, {TEXT("ctl.aimAcceleration"), 0.0}, {TEXT("ctl.lookSensitivity"), 1.0},
		{TEXT("ctl.invertLookY"), 0.0}, {TEXT("ctl.strokeSensitivity"), 1.0}, {TEXT("ctl.keyHints"), 1.0},
		{TEXT("aud.master"), 1.0}, {TEXT("aud.music"), 0.7}, {TEXT("aud.table"), 1.0}, {TEXT("aud.ambience"), 0.8}, {TEXT("aud.voices"), 1.0}, {TEXT("aud.interface"), 0.6}};
	for (const TPair<FName, double>& Default : Defaults)
	{
		const FRbSettingDef* Row = FRbSettingsRegistry::Find(Default.Key);
		if (!TestNotNull(*Default.Key.ToString(), Row))
		{
			continue;
		}
		TestNearlyEqual(FString::Printf(TEXT("%s: default (18.4)"), *Default.Key.ToString()), FRbSettingsRegistry::DefaultValue(*Row, *Fresh), Default.Value, 1e-3);
		TestNearlyEqual(FString::Printf(TEXT("%s: a fresh object holds its default"), *Default.Key.ToString()), FRbSettingsRegistry::GetValue(*Row, *Fresh), Default.Value, 1e-3);
	}
	for (const FRbSettingDef* Row : FRbSettingsRegistry::RowsOnPage(ERbSettingsPage::Graphics))
	{
		if (Row->bPresetDriven)
		{
			TestNearlyEqual(FString::Printf(TEXT("%s: fresh = the High preset's value"), *Row->Id.ToString()), FRbSettingsRegistry::GetValue(*Row, *Fresh), Row->PresetValues[2], 1e-3);
		}
	}
	// Ranges of 18.4 (frozen for M2).
	const TMap<FName, FVector2D> Ranges = {{TEXT("cam.fov"), FVector2D(40.0, 75.0)}, {TEXT("ctl.mouseDpi"), FVector2D(200.0, 6400.0)},
		{TEXT("ctl.aimSpeed"), FVector2D(0.1, 5.0)}, {TEXT("ctl.fineAim"), FVector2D(0.03, 0.5)}, {TEXT("ctl.aimAcceleration"), FVector2D(0.0, 1.0)},
		{TEXT("ctl.lookSensitivity"), FVector2D(0.1, 5.0)}, {TEXT("ctl.strokeSensitivity"), FVector2D(0.5, 2.0)}, {TEXT("gfx.resolutionScale"), FVector2D(25.0, 200.0)},
		{TEXT("aud.master"), FVector2D(0.0, 1.0)}, {TEXT("cam.headBob"), FVector2D(0.0, 1.0)}};
	for (const TPair<FName, FVector2D>& Range : Ranges)
	{
		const FRbSettingDef* Row = FRbSettingsRegistry::Find(Range.Key);
		TestTrue(FString::Printf(TEXT("%s: range %g..%g"), *Range.Key.ToString(), Range.Value.X, Range.Value.Y),
			Row && FMath::IsNearlyEqual(Row->Min, Range.Value.X) && FMath::IsNearlyEqual(Row->Max, Range.Value.Y));
	}
	TestTrue(TEXT("frame caps 30 60 90 120 144 165 240 unlimited"), FRbSettingsRegistry::FrameCaps() == TArray<float>({30.0f, 60.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f, 0.0f}));
	TestEqual(TEXT("preset label High"), FRbSettingsRegistry::PresetLabel(*Fresh).ToString(), FString(TEXT("High")));
	// Value texts in EN number format (M2 is EN only, also on a German Windows).
	TestEqual(TEXT("aim speed text"), FRbSettingsRegistry::FormatValue(*FRbSettingsRegistry::Find(TEXT("ctl.aimSpeed")), *Fresh, 1.0).ToString(),
		FString(TEXT("1.00×  ·  90° per 12.5 cm")));
	TestEqual(TEXT("FOV text"), FRbSettingsRegistry::FormatValue(*FRbSettingsRegistry::Find(TEXT("cam.fov")), *Fresh, 50.0).ToString(), FString(TEXT("50°  ·  79° wide")));
	TestEqual(TEXT("volume text"), FRbSettingsRegistry::FormatValue(*FRbSettingsRegistry::Find(TEXT("aud.music")), *Fresh, 0.7).ToString(), FString(TEXT("70 %")));

	// --- Set -> Get round trip of every value --------------------------------------------------------------------------
	for (const FRbSettingDef& Row : FRbSettingsRegistry::Rows())
	{
		URbGameUserSettings* S = NewSettings();
		for (const double Value : Candidates(Row, *S))
		{
			FRbSettingsRegistry::SetValue(Row, *S, Value);
			const double Back = FRbSettingsRegistry::GetValue(Row, *S);
			TestTrue(FString::Printf(TEXT("%s: Set %g -> Get %g"), *Row.Id.ToString(), Value, Back), FMath::IsNearlyEqual(Back, Value, 1e-4 * FMath::Max(1.0, FMath::Abs(Value))));
			TestFalse(FString::Printf(TEXT("%s: value text for %g"), *Row.Id.ToString(), Value), FRbSettingsRegistry::FormatValue(Row, *S, Back).IsEmpty());
		}
		// Out-of-range values clamp.
		FRbSettingsRegistry::SetValue(Row, *S, Row.Max + 1000.0);
		TestTrue(FString::Printf(TEXT("%s: clamps above"), *Row.Id.ToString()), FRbSettingsRegistry::GetValue(Row, *S) <= Row.Max + 1e-6 ||
			(Row.DynamicEnumLabels && FRbSettingsRegistry::GetValue(Row, *S) < FRbSettingsRegistry::NumEnumValues(Row, *S)));
		// Step: one step up from the minimum never passes the maximum and never stands still below it.
		FRbSettingsRegistry::SetValue(Row, *S, Row.Min);
		const double Up = FRbSettingsRegistry::StepValue(Row, *S, 1);
		TestTrue(FString::Printf(TEXT("%s: one step up moves"), *Row.Id.ToString()), Up > Row.Min || FRbSettingsRegistry::NumEnumValues(Row, *S) == 1);
	}

	// --- change notifications ------------------------------------------------------------------------------------------
	{
		URbGameUserSettings* S = NewSettings();
		int32 Broadcasts = 0;
		const FDelegateHandle Handle = URbGameUserSettings::OnSettingsChanged().AddLambda([&Broadcasts]() { ++Broadcasts; });
		FRbSettingsRegistry::SetAndApply(TEXT("cam.fov"), *S, 62.0);
		FRbSettingsRegistry::SetAndApply(TEXT("aud.music"), *S, 0.25);
		FRbSettingsRegistry::SetAndApply(TEXT("gfx.shadows"), *S, 1.0);
		URbGameUserSettings::OnSettingsChanged().Remove(Handle);
		TestEqual(TEXT("one OnSettingsChanged per applied row (readers re-read)"), Broadcasts, 3);
		TestFalse(TEXT("an unknown id is refused"), FRbSettingsRegistry::SetAndApply(TEXT("gfx.nope"), *S, 1.0));
	}

	// --- save -> reload with every row changed (the process-restart path: LoadConfig + LoadSettings' fix-ups) -------------
	const FString Ini = TempIni(TEXT("RbSettingsRegistry.ini"));
	IFileManager::Get().Delete(*Ini, false, true, true);
	URbGameUserSettings* A = NewSettings();
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("gfx.preset")), *A, static_cast<double>(ERbQualityPreset::Epic));
	for (const FRbSettingDef& Row : FRbSettingsRegistry::Rows())
	{
		if (Row.Id != TEXT("gfx.preset") && Row.Id != TEXT("cam.reducedMotion"))
		{
			FRbSettingsRegistry::SetValue(Row, *A, OtherValue(Row, *A));
		}
	}
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("cam.reducedMotion")), *A, 1.0); // last: its backup holds the changed rows
	const TMap<FName, double> Saved = Snapshot(*A);
	TestTrue(TEXT("every row changed"), Changed(Saved, Snapshot(*NewSettings())).Num() >= Saved.Num() - 3); // resolution / mount shake may equal
	TestEqual(TEXT("label of the saved mix"), FRbSettingsRegistry::PresetLabel(*A).ToString(), FString(TEXT("Custom (based on Epic)")));
	A->SaveConfig(CPF_Config, *Ini);
	GConfig->Flush(false, Ini);

	URbGameUserSettings* B = NewObject<URbGameUserSettings>(GetTransientPackage());
	B->SetToDefaults();
	B->LoadConfig(URbGameUserSettings::StaticClass(), *Ini);
	B->RestoreQualityAfterLoad(); // (no ValidateSettings: with an old Version it would reload / wipe the editor's own user settings)
	const TArray<FName> Lost = Changed(Saved, Snapshot(*B));
	TestTrue(FString::Printf(TEXT("every row survives save -> reload (differs: %s)"), *Join(Lost)), Lost.Num() == 0);
	TestEqual(TEXT("reloaded label"), FRbSettingsRegistry::PresetLabel(*B).ToString(), FString(TEXT("Custom (based on Epic)")));
	// Reduced motion's backup survives too: switching it off after the restart restores the changed rows, not the defaults.
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("cam.reducedMotion")), *A, 0.0);
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("cam.reducedMotion")), *B, 0.0);
	const TArray<FName> BackupLost = Changed(Snapshot(*A), Snapshot(*B));
	TestTrue(FString::Printf(TEXT("Reduced motion off after a reload restores the saved rows (differs: %s)"), *Join(BackupLost)), BackupLost.Num() == 0);
	// A named preset reloads as that preset (every option re-derived from it).
	URbGameUserSettings* C = NewSettings();
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("gfx.preset")), *C, static_cast<double>(ERbQualityPreset::Low));
	C->SaveConfig(CPF_Config, *Ini);
	URbGameUserSettings* D = NewObject<URbGameUserSettings>(GetTransientPackage());
	D->SetToDefaults();
	D->LoadConfig(URbGameUserSettings::StaticClass(), *Ini);
	D->RestoreQualityAfterLoad();
	const TArray<FName> LowLost = Changed(Snapshot(*C), Snapshot(*D));
	TestTrue(FString::Printf(TEXT("Low survives save -> reload (differs: %s)"), *Join(LowLost)), LowLost.Num() == 0);
	GConfig->Remove(Ini);
	IFileManager::Get().Delete(*Ini, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSettingsPresetsTest, "RawBreak.Unit.Settings.Presets", RB_UNIT_TEST_FLAGS)
bool FRbSettingsPresetsTest::RunTest(const FString& Parameters)
{
	using namespace RbSettingsTestsPrivate;
	FScalabilityGuard Guard;
	URbGameUserSettings* S = NewSettings();
	const FRbSettingDef* Preset = FRbSettingsRegistry::Find(TEXT("gfx.preset"));
	if (!TestNotNull(TEXT("preset row"), Preset))
	{
		return false;
	}
	// Non-preset rows the presets must never touch.
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("cam.fov")), *S, 61.0);
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("ctl.mouseDpi")), *S, 1600.0);
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("aud.music")), *S, 0.3);
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("gfx.motionBlur")), *S, 0.4);
	const TCHAR* LightingMode = TEXT("r.Lumen.HardwareRayTracing.LightingMode");
	const TCHAR* HitLighting = TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed");

	for (int32 Level = 0; Level < 5; ++Level)
	{
		FRbSettingsRegistry::SetAndApply(TEXT("gfx.preset"), *S, Level);
		const FString Name = FRbSettingsRegistry::PresetName(Level).ToString();
		TestEqual(FString::Printf(TEXT("%s: label"), *Name), FRbSettingsRegistry::PresetLabel(*S).ToString(), Name);
		for (const FRbSettingDef* Row : FRbSettingsRegistry::RowsOnPage(ERbSettingsPage::Graphics))
		{
			if (Row->bPresetDriven)
			{
				TestNearlyEqual(FString::Printf(TEXT("%s: %s = the preset's value"), *Name, *Row->Id.ToString()), FRbSettingsRegistry::GetValue(*Row, *S),
					Row->PresetValues[Level], 1e-3);
				TestNearlyEqual(FString::Printf(TEXT("%s: %s default = the preset's value"), *Name, *Row->Id.ToString()), FRbSettingsRegistry::DefaultValue(*Row, *S),
					Row->PresetValues[Level], 1e-3);
			}
		}
		TestEqual(FString::Printf(TEXT("%s: sg.ShadowQuality"), *Name), CVarInt(TEXT("sg.ShadowQuality")), Level);
		TestEqual(FString::Printf(TEXT("%s: sg.GlobalIlluminationQuality"), *Name), CVarInt(TEXT("sg.GlobalIlluminationQuality")), Level);
		TestEqual(FString::Printf(TEXT("%s: hit-lit reflections from High up"), *Name), CVarInt(LightingMode), Level >= 2 ? 2 : 0);
		TestEqual(FString::Printf(TEXT("%s: hit lighting allowed from High up"), *Name), CVarInt(HitLighting), Level >= 2 ? 1 : 0);
		if (Level >= 3)
		{
			TestEqual(FString::Printf(TEXT("%s: r.MaterialQualityLevel 1 (the High pins UE-3 authored)"), *Name), CVarInt(TEXT("r.MaterialQualityLevel")), 1);
		}
		if (Level == 0)
		{
			TestTrue(TEXT("Low: screen-space reflections on (plan 9.4; engine Low has none)"), CVarInt(TEXT("r.SSR.Quality")) > 0);
			TestEqual(TEXT("Low: no Lumen reflections"), CVarInt(TEXT("r.Lumen.Reflections.Allow")), 0);
		}
		// One row changed -> Custom (based on this preset); picking the preset again overwrites it.
		const double Shadows = FRbSettingsRegistry::GetValue(*FRbSettingsRegistry::Find(TEXT("gfx.shadows")), *S);
		FRbSettingsRegistry::SetAndApply(TEXT("gfx.shadows"), *S, Level == 0 ? 1.0 : 0.0);
		TestEqual(FString::Printf(TEXT("%s: one row -> Custom"), *Name), FRbSettingsRegistry::PresetLabel(*S).ToString(),
			FString::Printf(TEXT("Custom (based on %s)"), *Name));
		FRbSettingsRegistry::SetAndApply(TEXT("gfx.preset"), *S, Level);
		TestNearlyEqual(FString::Printf(TEXT("%s: picking the preset again overwrites the row"), *Name),
			FRbSettingsRegistry::GetValue(*FRbSettingsRegistry::Find(TEXT("gfx.shadows")), *S), Shadows, 1e-6);
	}

	// High, then only Global illumination changes: the RAW BREAK rows are applied after the group again (ui-ux 0.12).
	FRbSettingsRegistry::SetAndApply(TEXT("gfx.preset"), *S, static_cast<double>(ERbQualityPreset::High));
	TestEqual(TEXT("High: LightingMode 2"), CVarInt(LightingMode), 2);
	TestEqual(TEXT("High: HitLighting.Allowed 1"), CVarInt(HitLighting), 1);
	for (const double Gi : {1.0, 3.0, 0.0})
	{
		FRbSettingsRegistry::SetAndApply(TEXT("gfx.globalIllumination"), *S, Gi);
		TestEqual(FString::Printf(TEXT("GI %g only: LightingMode stays 2"), Gi), CVarInt(LightingMode), 2);
		TestEqual(FString::Printf(TEXT("GI %g only: HitLighting.Allowed stays 1"), Gi), CVarInt(HitLighting), 1);
		TestEqual(FString::Printf(TEXT("GI %g: sg.GlobalIlluminationQuality follows"), Gi), CVarInt(TEXT("sg.GlobalIlluminationQuality")), FMath::RoundToInt(Gi));
	}
	TestEqual(TEXT("GI changes -> Custom (based on High)"), FRbSettingsRegistry::PresetLabel(*S).ToString(), FString(TEXT("Custom (based on High)")));
	// The resolution scale is preset-driven too.
	FRbSettingsRegistry::SetAndApply(TEXT("gfx.resolutionScale"), *S, 100.0);
	TestNearlyEqual(TEXT("resolution scale row -> r.ScreenPercentage"), CVarFloat(TEXT("r.ScreenPercentage")), 100.0f, 1e-3f);
	// Reset page (Graphics) = the High preset.
	FRbSettingsRegistry::ResetPage(ERbSettingsPage::Graphics, *S);
	TestEqual(TEXT("reset Graphics = High"), FRbSettingsRegistry::PresetLabel(*S).ToString(), FString(TEXT("High")));
	TestNearlyEqual(TEXT("reset Graphics: motion blur default"), S->MotionBlurScale, 1.0f, 1e-6f);

	// Presets never touch the rows of the other pages.
	TestNearlyEqual(TEXT("FOV untouched by presets"), S->VerticalFovDeg, 61.0f, 1e-6f);
	TestNearlyEqual(TEXT("DPI untouched by presets"), S->MouseDpi, 1600.0f, 1e-6f);
	TestNearlyEqual(TEXT("music untouched by presets"), S->Volumes.Music, 0.3f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSettingsReducedMotionTest, "RawBreak.Unit.Settings.ReducedMotion", RB_UNIT_TEST_FLAGS)
bool FRbSettingsReducedMotionTest::RunTest(const FString& Parameters)
{
	using namespace RbSettingsTestsPrivate;
	URbGameUserSettings* S = NewSettings();
	const auto Set = [S](const TCHAR* Id, double Value) { FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(Id), *S, Value); };
	Set(TEXT("cam.look"), 1.0);         // Headcam (mount shake available)
	Set(TEXT("cam.headBob"), 0.6);
	Set(TEXT("cam.bodySway"), 0.8);
	Set(TEXT("cam.mountShake"), 0.5);
	Set(TEXT("gfx.motionBlur"), 0.7);
	Set(TEXT("cam.posture"), static_cast<double>(ERbPostureTransition::Cut));
	Set(TEXT("cam.fov"), 57.0);
	const TMap<FName, double> Before = Snapshot(*S);

	Set(TEXT("cam.reducedMotion"), 1.0);
	const TMap<FName, double> On = Snapshot(*S);
	const TArray<FName> Rows = Changed(Before, On);
	TestEqual(TEXT("Reduced motion sets exactly its rows (13.10, M2 subset)"), Join(Rows),
		FString(TEXT("cam.bodySway, cam.headBob, cam.mountShake, cam.posture, cam.reducedMotion, gfx.motionBlur")));
	TestNearlyEqual(TEXT("head bob 0"), On[TEXT("cam.headBob")], 0.0, 1e-9);
	TestNearlyEqual(TEXT("body sway 30 %"), On[TEXT("cam.bodySway")], 0.3, 1e-6);
	TestNearlyEqual(TEXT("mount shake 0"), On[TEXT("cam.mountShake")], 0.0, 1e-9);
	TestNearlyEqual(TEXT("motion blur 0"), On[TEXT("gfx.motionBlur")], 0.0, 1e-9);
	TestNearlyEqual(TEXT("posture Quick"), On[TEXT("cam.posture")], static_cast<double>(ERbPostureTransition::Quick), 1e-9);
	Set(TEXT("cam.reducedMotion"), 1.0);
	TestTrue(TEXT("switching it on twice changes nothing"), Changed(On, Snapshot(*S)).Num() == 0);

	// The rows stay editable while it is on; switching it off restores exactly the previous values.
	Set(TEXT("cam.headBob"), 0.2);
	TestNearlyEqual(TEXT("editable while on"), S->HeadBobScale, 0.2f, 1e-6f);
	Set(TEXT("cam.reducedMotion"), 0.0);
	const TArray<FName> NotRestored = Changed(Before, Snapshot(*S));
	TestTrue(FString::Printf(TEXT("off restores every previous value (differs: %s)"), *Join(NotRestored)), NotRestored.Num() == 0);
	Set(TEXT("cam.reducedMotion"), 0.0);
	TestTrue(TEXT("switching it off twice changes nothing"), Changed(Before, Snapshot(*S)).Num() == 0);

	// Through a restart: on -> save -> reload -> off restores the values from before.
	const FString Ini = TempIni(TEXT("RbSettingsReducedMotion.ini"));
	IFileManager::Get().Delete(*Ini, false, true, true);
	Set(TEXT("cam.reducedMotion"), 1.0);
	S->SaveConfig(CPF_Config, *Ini);
	GConfig->Flush(false, Ini);
	URbGameUserSettings* B = NewObject<URbGameUserSettings>(GetTransientPackage());
	B->SetToDefaults();
	B->LoadConfig(URbGameUserSettings::StaticClass(), *Ini);
	B->RestoreQualityAfterLoad();
	TestTrue(TEXT("reloaded: on"), B->bReducedMotion && FMath::IsNearlyZero(B->HeadBobScale));
	FRbSettingsRegistry::SetValue(*FRbSettingsRegistry::Find(TEXT("cam.reducedMotion")), *B, 0.0);
	const TArray<FName> AfterRestart = Changed(Before, Snapshot(*B));
	TestTrue(FString::Printf(TEXT("off after a restart restores the previous values (differs: %s)"), *Join(AfterRestart)), AfterRestart.Num() == 0);
	GConfig->Remove(Ini);
	IFileManager::Get().Delete(*Ini, false, true, true);

	// Reset page (Camera) with Reduced motion on: every camera row to its default, no stale backup.
	Set(TEXT("cam.reducedMotion"), 1.0);
	FRbSettingsRegistry::ResetPage(ERbSettingsPage::Camera, *S);
	TestTrue(TEXT("reset Camera: reduced motion off, head bob 1, no backup"), !S->bReducedMotion && FMath::IsNearlyEqual(S->HeadBobScale, 1.0f) &&
		!S->ReducedMotionBackup.bValid);
	TestNearlyEqual(TEXT("reset Camera leaves the Graphics page (motion blur) alone"), S->MotionBlurScale, 0.7f, 1e-6f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
