#include "Settings/RbGameUserSettings.h"

#include "RawBreak.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigUtilities.h"
#include "Scalability.h"

// Owner: UE-8.

namespace RbQualityPrivate
{
	constexpr int32 NumLevels = 5; // Low, Medium, High, Epic, Cinematic (UE: 0..3 + Cine)
	const FName RowTag(TEXT("RawBreakQualityRow"));

	// Plan 9.4 upscaler / internal resolution row (TSR now, DLSS later): Low 50-60 %, Medium DLSS Balanced, High DLSS Quality,
	// Epic DLSS Quality (DLAA = 100 as an individual choice), Cinematic DLAA. Used when the ini has no value.
	constexpr float DefaultScreenPercentage[NumLevels] = {55.0f, 58.328f, 66.662f, 66.662f, 100.0f};

	int32 ClampLevel(int32 Level) { return FMath::Clamp(Level, 0, NumLevels - 1); }

	// The engine group behind an option (nullptr: RAW BREAK row only).
	const TCHAR* EngineGroup(ERbQualityOption Option)
	{
		switch (Option)
		{
		case ERbQualityOption::ViewDistance: return TEXT("ViewDistanceQuality");
		case ERbQualityOption::AntiAliasing: return TEXT("AntiAliasingQuality");
		case ERbQualityOption::Shadows: return TEXT("ShadowQuality");
		case ERbQualityOption::GlobalIllumination: return TEXT("GlobalIlluminationQuality");
		case ERbQualityOption::Reflections: return TEXT("ReflectionQuality");
		case ERbQualityOption::PostProcess: return TEXT("PostProcessQuality");
		case ERbQualityOption::Textures: return TEXT("TextureQuality");
		case ERbQualityOption::Effects: return TEXT("EffectsQuality");
		case ERbQualityOption::Foliage: return TEXT("FoliageQuality");
		case ERbQualityOption::Shading: return TEXT("ShadingQuality");
		default: return nullptr;
		}
	}

	// Level field of an option's engine group (int32* / const int32*), nullptr for RAW BREAK-only options.
	template <class TLevels>
	auto GroupField(TLevels& Q, ERbQualityOption Option) -> decltype(&Q.ShadowQuality)
	{
		switch (Option)
		{
		case ERbQualityOption::ViewDistance: return &Q.ViewDistanceQuality;
		case ERbQualityOption::AntiAliasing: return &Q.AntiAliasingQuality;
		case ERbQualityOption::Shadows: return &Q.ShadowQuality;
		case ERbQualityOption::GlobalIllumination: return &Q.GlobalIlluminationQuality;
		case ERbQualityOption::Reflections: return &Q.ReflectionQuality;
		case ERbQualityOption::PostProcess: return &Q.PostProcessQuality;
		case ERbQualityOption::Textures: return &Q.TextureQuality;
		case ERbQualityOption::Effects: return &Q.EffectsQuality;
		case ERbQualityOption::Foliage: return &Q.FoliageQuality;
		case ERbQualityOption::Shading: return &Q.ShadingQuality;
		default: return nullptr;
		}
	}

	bool ParsePreset(const FString& Text, ERbQualityPreset& Out)
	{
		const UEnum* Enum = StaticEnum<ERbQualityPreset>();
		for (int32 Index = 0; Enum && Index < Enum->NumEnums() - 1; ++Index)
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase))
			{
				Out = static_cast<ERbQualityPreset>(Enum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	bool ParseOption(const FString& Text, ERbQualityOption& Out)
	{
		for (int32 Index = 0; Index < static_cast<int32>(ERbQualityOption::Count); ++Index)
		{
			if (Text.Equals(URbGameUserSettings::OptionName(static_cast<ERbQualityOption>(Index)), ESearchCase::IgnoreCase))
			{
				Out = static_cast<ERbQualityOption>(Index);
				return true;
			}
		}
		return false;
	}

	FString CVarText(const TCHAR* Name)
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetString() : FString(TEXT("-"));
	}

	void Dump()
	{
		const URbGameUserSettings* S = URbGameUserSettings::Get();
		if (!S)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Quality: the game user settings are not URbGameUserSettings"));
			return;
		}
		FString Options;
		for (int32 Index = 0; Index < static_cast<int32>(ERbQualityOption::Count); ++Index)
		{
			const ERbQualityOption Option = static_cast<ERbQualityOption>(Index);
			Options += FString::Printf(TEXT(" %s=%d"), URbGameUserSettings::OptionName(Option), S->GetQualityOption(Option));
		}
		UE_LOG(LogRawBreak, Display, TEXT("rb.Quality: preset %s, screen %.1f %%,%s | LightingMode %s HitLighting %s FinalGather %s LumenReflections %s pool %s"),
			*UEnum::GetValueAsString(S->GetQualityPreset()), S->GetScreenPercentage(), *Options, *CVarText(TEXT("r.Lumen.HardwareRayTracing.LightingMode")),
			*CVarText(TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed")), *CVarText(TEXT("r.Lumen.FinalGatherMethod")),
			*CVarText(TEXT("r.Lumen.Reflections.Allow")), *CVarText(TEXT("r.Streaming.PoolSize")));
	}

	// rb.Quality <preset> [save]: applies a preset (headless captures pass it with -ExecCmds; "save" also writes the user ini).
	FAutoConsoleCommand GRbQualityCommand(TEXT("rb.Quality"), TEXT("rb.Quality <Low|Medium|High|Epic|Cinematic|Custom> [save]: apply a RAW BREAK quality preset."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			URbGameUserSettings* S = URbGameUserSettings::Get();
			ERbQualityPreset Preset;
			if (!S || Args.Num() < 1 || !ParsePreset(Args[0], Preset))
			{
				UE_LOG(LogRawBreak, Warning, TEXT("rb.Quality <Low|Medium|High|Epic|Cinematic|Custom> [save]"));
				return;
			}
			S->ApplyQualityPreset(Preset);
			if (Args.Num() > 1 && Args[1].Equals(TEXT("save"), ESearchCase::IgnoreCase))
			{
				S->SaveSettings();
			}
			Dump();
		}));

	FAutoConsoleCommand GRbQualityOptionCommand(TEXT("rb.Quality.Option"), TEXT("rb.Quality.Option <option> <0-4>: set one quality option (preset -> Custom)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			URbGameUserSettings* S = URbGameUserSettings::Get();
			ERbQualityOption Option;
			if (!S || Args.Num() < 2 || !ParseOption(Args[0], Option))
			{
				UE_LOG(LogRawBreak, Warning, TEXT("rb.Quality.Option <ViewDistance|AntiAliasing|Shadows|GlobalIllumination|Reflections|PostProcess|Textures|Effects|Foliage|Shading|VolumetricFog> <0-4>"));
				return;
			}
			S->SetQualityOption(Option, FCString::Atoi(*Args[1]));
			S->ApplyQualityRows();
			Dump();
		}));

	FAutoConsoleCommand GRbQualityDumpCommand(TEXT("rb.Quality.Dump"), TEXT("Log the RAW BREAK quality preset, options and key renderer variables."),
		FConsoleCommandDelegate::CreateStatic(&Dump));
}

URbGameUserSettings::URbGameUserSettings()
{
}

URbGameUserSettings* URbGameUserSettings::Get()
{
	return GEngine ? Cast<URbGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

int32 URbGameUserSettings::PresetLevel(ERbQualityPreset Preset)
{
	switch (Preset)
	{
	case ERbQualityPreset::Low: return 0;
	case ERbQualityPreset::Medium: return 1;
	case ERbQualityPreset::High: return 2;
	case ERbQualityPreset::Epic: return 3;
	case ERbQualityPreset::Cinematic: return 4;
	default: return -1;
	}
}

float URbGameUserSettings::PresetScreenPercentage(int32 Level)
{
	Level = RbQualityPrivate::ClampLevel(Level);
	float Percent = RbQualityPrivate::DefaultScreenPercentage[Level];
	if (GConfig)
	{
		const FString Section = Scalability::GetScalabilitySectionString(TEXT("RawBreak.Preset"), Level, RbQualityPrivate::NumLevels);
		GConfig->GetFloat(*Section, TEXT("ScreenPercentage"), Percent, GScalabilityIni);
	}
	return FMath::Clamp(Percent, 25.0f, 200.0f);
}

const TCHAR* URbGameUserSettings::OptionName(ERbQualityOption Option)
{
	switch (Option)
	{
	case ERbQualityOption::ViewDistance: return TEXT("ViewDistance");
	case ERbQualityOption::AntiAliasing: return TEXT("AntiAliasing");
	case ERbQualityOption::Shadows: return TEXT("Shadows");
	case ERbQualityOption::GlobalIllumination: return TEXT("GlobalIllumination");
	case ERbQualityOption::Reflections: return TEXT("Reflections");
	case ERbQualityOption::PostProcess: return TEXT("PostProcess");
	case ERbQualityOption::Textures: return TEXT("Textures");
	case ERbQualityOption::Effects: return TEXT("Effects");
	case ERbQualityOption::Foliage: return TEXT("Foliage");
	case ERbQualityOption::Shading: return TEXT("Shading");
	case ERbQualityOption::VolumetricFog: return TEXT("VolumetricFog");
	default: return TEXT("?");
	}
}

FString URbGameUserSettings::RowSection(ERbQualityOption Option, int32 Level)
{
	const TCHAR* Group = RbQualityPrivate::EngineGroup(Option);
	const FString Base = FString(TEXT("RawBreak.")) + (Group ? Group : OptionName(Option));
	return Scalability::GetScalabilitySectionString(*Base, RbQualityPrivate::ClampLevel(Level), RbQualityPrivate::NumLevels);
}

TArray<FString> URbGameUserSettings::RowConsoleVariables()
{
	TArray<FString> Names;
	for (int32 Index = 0; Index < static_cast<int32>(ERbQualityOption::Count); ++Index)
	{
		for (int32 Level = 0; Level < RbQualityPrivate::NumLevels; ++Level)
		{
			const FString Section = RowSection(static_cast<ERbQualityOption>(Index), Level);
			if (GConfig && GConfig->DoesSectionExist(*Section, GScalabilityIni))
			{
				UE::ConfigUtilities::ForEachCVarInSectionFromIni(*Section, *GScalabilityIni,
					[&Names](IConsoleVariable*, const FString& Key, const FString&) { Names.AddUnique(Key); });
			}
		}
	}
	return Names;
}

void URbGameUserSettings::RemoveQualityRows()
{
	IConsoleManager::Get().UnsetAllConsoleVariablesWithTag(RbQualityPrivate::RowTag, ECVF_SetByGameOverride);
}

void URbGameUserSettings::FillFromPresetLevel(int32 Level)
{
	Level = RbQualityPrivate::ClampLevel(Level);
	ScalabilityQuality.SetFromSingleQualityLevel(Level); // every group at the preset level (4 = Cine: "Epic + extras")
	ScalabilityQuality.ResolutionQuality = PresetScreenPercentage(Level);
	VolumetricFogQuality = Level;
}

void URbGameUserSettings::ApplyQualityPreset(ERbQualityPreset Preset)
{
	QualityPreset = Preset;
	if (Preset != ERbQualityPreset::Custom)
	{
		FillFromPresetLevel(PresetLevel(Preset));
	}
	ApplyQualityRows();
}

void URbGameUserSettings::SetQualityOption(ERbQualityOption Option, int32 Level)
{
	Level = RbQualityPrivate::ClampLevel(Level);
	if (int32* Field = RbQualityPrivate::GroupField(ScalabilityQuality, Option))
	{
		*Field = Level;
	}
	else if (Option == ERbQualityOption::VolumetricFog)
	{
		VolumetricFogQuality = Level;
	}
	QualityPreset = ERbQualityPreset::Custom;
}

int32 URbGameUserSettings::GetQualityOption(ERbQualityOption Option) const
{
	if (const int32* Field = RbQualityPrivate::GroupField(ScalabilityQuality, Option))
	{
		return *Field;
	}
	return Option == ERbQualityOption::VolumetricFog ? VolumetricFogQuality : 0;
}

void URbGameUserSettings::SetScreenPercentage(float Percent)
{
	ScalabilityQuality.ResolutionQuality = FMath::Clamp(Percent, 25.0f, 200.0f);
	QualityPreset = ERbQualityPreset::Custom;
}

float URbGameUserSettings::GetScreenPercentage() const
{
	return ScalabilityQuality.ResolutionQuality;
}

void URbGameUserSettings::ApplyQualityRows()
{
	// Drop the previous rows first: a row that sets a variable only at some levels must not leave it behind at the others, and
	// the groups below must not run into the old rows (a SetByScalability write under a SetByGameOverride value is ignored
	// with a LogConsoleManager warning; the cvar history keeps it either way, so only the order of the log changes).
	RemoveQualityRows();
	if (bEnableScalabilitySettings)
	{
		Scalability::SetQualityLevels(ScalabilityQuality);
	}
	// RAW BREAK rows after the groups, one per option at that option's level (GameOverride: above project settings).
	for (int32 Index = 0; Index < static_cast<int32>(ERbQualityOption::Count); ++Index)
	{
		const ERbQualityOption Option = static_cast<ERbQualityOption>(Index);
		const FString Section = RowSection(Option, GetQualityOption(Option));
		if (GConfig && GConfig->DoesSectionExist(*Section, GScalabilityIni))
		{
			UE::ConfigUtilities::ApplyCVarSettingsFromIni(*Section, *GScalabilityIni, ECVF_SetByGameOverride, false, RbQualityPrivate::RowTag);
		}
	}
}

void URbGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	QualityPreset = ERbQualityPreset::High; // the M1 default (decisions: High = RTX 3070 Ti test tier)
	FillFromPresetLevel(PresetLevel(QualityPreset));
	CameraPreset = ERbCameraPreset::Eyes;
	VerticalFovDeg = 50.0f;
	MouseDpi = 800.0f;
	bHardcoreStroke = false;
	bReducedMotion = false;
	HeadBobScale = 1.0f;
	MotionBlurScale = 1.0f;
	GrainScale = 1.0f;
	bDepthOfField = true;
	Controls = FRbControlSettings();
	Camera = FRbCameraSettings();
	Volumes = FRbAudioVolumes();
	bShowKeyHints = true;
}

void URbGameUserSettings::LoadSettings(bool bForceReload)
{
	Super::LoadSettings(bForceReload); // ScalabilityQuality = the engine's loaded group state
	if (QualityPreset != ERbQualityPreset::Custom)
	{
		FillFromPresetLevel(PresetLevel(QualityPreset)); // a preset is authoritative for every option
	}
}

void URbGameUserSettings::ApplyNonResolutionSettings()
{
	if (QualityPreset != ERbQualityPreset::Custom)
	{
		FillFromPresetLevel(PresetLevel(QualityPreset));
	}
	RemoveQualityRows();                 // the engine's group update below must not collide with the previous rows
	Super::ApplyNonResolutionSettings(); // vsync, frame limit, groups once the engine is initialised, HDR
	ApplyQualityRows();                  // groups also at engine start (UGameEngine::Init) + the RAW BREAK rows
	NotifySettingsChanged();             // M2: readers (aim / look, camera, audio volumes) re-read
}

FSimpleMulticastDelegate& URbGameUserSettings::OnSettingsChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

void URbGameUserSettings::NotifySettingsChanged()
{
	OnSettingsChanged().Broadcast();
}

void URbGameUserSettings::ValidateSettings()
{
	Super::ValidateSettings();
	VolumetricFogQuality = RbQualityPrivate::ClampLevel(VolumetricFogQuality);
	VerticalFovDeg = FMath::Clamp(VerticalFovDeg, 40.0f, 75.0f);
	MouseDpi = FMath::Clamp(MouseDpi, 100.0f, 32000.0f);
	HeadBobScale = FMath::Clamp(HeadBobScale, 0.0f, 1.0f);
	MotionBlurScale = FMath::Clamp(MotionBlurScale, 0.0f, 1.0f);
	GrainScale = FMath::Clamp(GrainScale, 0.0f, 1.0f);
	// M2 rows (ui-ux 13.6 / 13.8 ranges).
	Controls.AimDegreesPerCm = FMath::Clamp(Controls.AimDegreesPerCm, 0.5f, 60.0f);
	Controls.AimSensitivity = FMath::Clamp(Controls.AimSensitivity, 0.1f, 5.0f);
	Controls.FineAimFactor = FMath::Clamp(Controls.FineAimFactor, 0.03f, 0.5f);
	Controls.AimAcceleration = FMath::Clamp(Controls.AimAcceleration, 0.0f, 1.0f);
	Controls.LookDegreesPerCm = FMath::Clamp(Controls.LookDegreesPerCm, 1.0f, 120.0f);
	Controls.LookSensitivity = FMath::Clamp(Controls.LookSensitivity, 0.1f, 5.0f);
	Controls.StrokeSensitivity = FMath::Clamp(Controls.StrokeSensitivity, 0.5f, 2.0f);
	Camera.BodySwayScale = FMath::Clamp(Camera.BodySwayScale, 0.0f, 1.0f);
	Camera.MountShakeScale = FMath::Clamp(Camera.MountShakeScale, 0.0f, 1.0f);
	for (float* Volume : {&Volumes.Master, &Volumes.Music, &Volumes.Table, &Volumes.Ambience, &Volumes.Voices, &Volumes.Interface})
	{
		*Volume = FMath::Clamp(*Volume, 0.0f, 1.0f);
	}
}
