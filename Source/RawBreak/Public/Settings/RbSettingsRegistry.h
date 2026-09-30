#pragma once

// Registry of the settings rows (ui-ux 13.13, M2 subset of Docs/ue-architecture.md 18.4). The settings screen, presets,
// persistence and the tests all iterate this one list; adding an option is adding a row. Get / Set wrap URbGameUserSettings (the
// storage; engine rows through the UGameUserSettings setters, RAW BREAK quality rows through SetQualityOption, never direct
// cvar writes). Owner: M2-D.
//
// Model: every row's value is a double - the enum index, the value of a range, 0 / 1 for a toggle. SetValue stores (snapped to
// the row's step, clamped to its range) and never touches the renderer; Apply performs the row's live effect:
//   QualityRows      URbGameUserSettings::ApplyQualityRows (engine groups, then the RAW BREAK [RawBreak.*] rows) + notify
//   NonResolution    ApplyNonResolutionSettings (v-sync, frame cap; it re-applies the quality rows and notifies)
//   Resolution       ApplyResolutionSettings (window mode, resolution; the settings menu asks to keep them within 15 s)
//   Notify           URbGameUserSettings::NotifySettingsChanged (camera, controls, audio, key hints: readers re-read)
// Presets: the quality rows and the resolution scale are preset-driven (PresetValues, Low..Cinematic); changing one of them
// turns the preset into "Custom (based on <preset>)". Every other row has a Default and is never touched by a preset.
// Persistence: everything lives in URbGameUserSettings (GameUserSettings.ini); the menu saves when it closes.
// Dev console (non-shipping): rb.Settings.Dump, rb.Settings.Set <id> <value>, rb.Settings.Reset [page|all], rb.Settings.Save.

#include "CoreMinimal.h"

class URbGameUserSettings;

enum class ERbSettingsPage : uint8
{
	Graphics,
	Display,
	Camera,
	Controls,
	Audio,
	Count,
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

// What applying a stored change does (see the header comment).
enum class ERbSettingEffect : uint8
{
	Notify,
	QualityRows,
	NonResolution,
	Resolution,
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
	// The stored value (enum index / value / 0-1 for bools) of a settings object, and the store (no apply; SetValue snaps and
	// clamps before calling it).
	TFunction<double(const URbGameUserSettings&)> Get;
	TFunction<void(URbGameUserSettings&, double)> Set;

	// --- M2-D additions -----------------------------------------------------------------------------------------------------
	ERbSettingEffect Effect = ERbSettingEffect::Notify;
	double Default = 0.0;            // rows that are not preset-driven
	double PresetValues[5] = {};     // preset-driven rows: Low, Medium, High, Epic, Cinematic
	// Value text of Float rows ("50 deg", "80 %", "1.00x (90 deg per 12.5 cm)"); Enum / Bool rows use their labels.
	TFunction<FText(double)> Format;
	// Enum labels that depend on the machine (the resolution list); overrides EnumLabels when set.
	TFunction<TArray<FText>(const URbGameUserSettings&)> DynamicEnumLabels;
	// False (with the reason shown in the description panel) when the row cannot be changed now ("Headcam only").
	TFunction<bool(const URbGameUserSettings&, FText& /*OutReason*/)> IsAvailable;
};

class RAWBREAK_API FRbSettingsRegistry
{
public:
	// Every row of Docs/ue-architecture.md 18.4, in page order (built once).
	static const TArray<FRbSettingDef>& Rows();
	static const FRbSettingDef* Find(FName Id);
	static TArray<const FRbSettingDef*> RowsOnPage(ERbSettingsPage Page);

	static double GetValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings);
	// Stores Value (snapped to the step, clamped to the range; enums wrap nothing - they clamp). No renderer / delegate work.
	static void SetValue(const FRbSettingDef& Row, URbGameUserSettings& Settings, double Value);
	// The row's live effect (see the header comment).
	static void Apply(const FRbSettingDef& Row, URbGameUserSettings& Settings);
	static bool SetAndApply(FName Id, URbGameUserSettings& Settings, double Value);
	// One step up / down (enums: next / previous value, clamped; toggles flip). Returns the new value.
	static double StepValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings, int32 Direction);

	static int32 NumEnumValues(const FRbSettingDef& Row, const URbGameUserSettings& Settings);
	static TArray<FText> EnumLabels(const FRbSettingDef& Row, const URbGameUserSettings& Settings);
	static FText FormatValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings, double Value);
	static bool IsAvailable(const FRbSettingDef& Row, const URbGameUserSettings& Settings, FText* OutReason = nullptr);

	// Default of a row: the preset's value for preset-driven rows (a Custom mix: its base preset), else Default.
	static double DefaultValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings);
	// Restores every row of a page to its default and applies the page (Graphics = the High preset + the comfort defaults;
	// Display skips window mode and resolution, which ask to confirm on their own rows).
	static void ResetPage(ERbSettingsPage Page, URbGameUserSettings& Settings);

	static FText PageName(ERbSettingsPage Page);
	static FText PresetName(int32 PresetIndex);
	// "High" / "Custom (based on High)".
	static FText PresetLabel(const URbGameUserSettings& Settings);

	// Display rows (machine dependent): the supported resolutions (+ the current one), ascending.
	static TArray<FIntPoint> ResolutionList(const URbGameUserSettings& Settings);
	// The frame caps of the Display page (0 = unlimited, last).
	static const TArray<float>& FrameCaps();
	// Horizontal FOV [deg] of a vertical FOV at an aspect (width / height).
	static double HorizontalFovDeg(double VerticalDeg, double Aspect);
};
