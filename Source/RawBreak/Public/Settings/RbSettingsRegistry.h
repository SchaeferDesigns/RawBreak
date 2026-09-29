#pragma once

// Registry of the settings rows (ui-ux 13.13, M2 subset of Docs/ue-architecture.md 18.4). The settings screen, presets,
// persistence and the tests all iterate this one list; adding an option is adding a row. Get / Set wrap URbGameUserSettings (the
// storage; engine rows through the UGameUserSettings setters, RAW BREAK quality rows through SetQualityOption, never direct
// cvar writes). Owner: M2-D (stub by the M2 architect step).

#include "CoreMinimal.h"

enum class ERbSettingsPage : uint8
{
	Graphics,
	Display,
	Camera,
	Controls,
	Audio,
};

enum class ERbSettingType : uint8
{
	Enum,
	Float,
	Bool,
	Action,
};

enum class ERbSettingApply : uint8
{
	Live,
	ConfirmRevert15s, // window mode, resolution
	StuttersBriefly,
	Restart,
};

struct FRbSettingDef
{
	FName Id;                        // "gfx.preset", "ctl.aimSensitivity", "aud.master" ...
	ERbSettingsPage Page = ERbSettingsPage::Graphics;
	ERbSettingType Type = ERbSettingType::Float;
	FText Label;
	FText Description;
	TArray<FText> EnumLabels;        // Enum rows
	double Min = 0.0, Max = 1.0, Step = 0.05;
	bool bPresetDriven = false;      // takes the quality preset's value when a preset is picked
	ERbSettingApply Apply = ERbSettingApply::Live;
	TFunction<double()> Get;         // enum index / value / 0-1 for bools
	TFunction<void(double)> Set;
};

class RAWBREAK_API FRbSettingsRegistry
{
public:
	// The rows (built once; TODO(M2-D): every row of 18.4).
	static const TArray<FRbSettingDef>& Rows();
	static const FRbSettingDef* Find(FName Id);
};
