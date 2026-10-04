#include "Settings/RbSettingsRegistry.h"

#include "RawBreak.h"
#include "Settings/RbGameUserSettings.h"
#include "UI/Core/RbUiStyle.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"

// Owner: M2-D. The rows of Docs/ue-architecture.md 18.4 (ranges and defaults there are frozen for M2; the defaults of the
// Controls / Camera / Audio rows are the RbSettingsTypes.h defaults). Tests: RawBreak.Unit.Settings.* (RbSettingsTests.cpp).

#define LOCTEXT_NAMESPACE "RbSettings"

namespace RbSettingsPrivate
{
	using FRow = FRbSettingDef;

	constexpr int32 NumPresetLevels = 5;

	// A number with exactly Count decimals, no grouping.
	FText Fixed(double Value, int32 Count)
	{
		return RbUi::Number(Value, Count); // EN number format (M2 is EN only; RbUi::NumberCulture)
	}

	FText Percent(double Value)
	{
		return FText::Format(LOCTEXT("PercentValue", "{0} %"), RbUi::Number(FMath::RoundToInt(100.0 * Value)));
	}

	FText Multiplier(double Value)
	{
		return FText::Format(LOCTEXT("MultiplierValue", "{0}×"), Fixed(Value, 2));
	}

	TArray<FText> LevelLabels()
	{
		return {LOCTEXT("LevelLow", "Low"), LOCTEXT("LevelMedium", "Medium"), LOCTEXT("LevelHigh", "High"), LOCTEXT("LevelEpic", "Epic"),
			LOCTEXT("LevelCinematic", "Cinematic")};
	}

	TArray<FText> OffOn()
	{
		return {LOCTEXT("Off", "Off"), LOCTEXT("On", "On")};
	}

	// A preset-driven quality option row (level 0..4 = the preset level).
	FRow QualityRow(const TCHAR* Id, ERbQualityOption Option, const FText& Label, const FText& Description, TArray<FText> Labels = LevelLabels(),
		ERbSettingApply Apply = ERbSettingApply::Live)
	{
		FRow Row;
		Row.Id = Id;
		Row.Page = ERbSettingsPage::Graphics;
		Row.Type = ERbSettingType::Enum;
		Row.Label = Label;
		Row.Description = Description;
		Row.EnumLabels = MoveTemp(Labels);
		Row.Min = 0.0;
		Row.Max = 4.0;
		Row.Step = 1.0;
		Row.bPresetDriven = true;
		Row.Apply = Apply;
		Row.Effect = ERbSettingEffect::QualityRows;
		for (int32 Level = 0; Level < NumPresetLevels; ++Level)
		{
			Row.PresetValues[Level] = Level;
		}
		Row.Get = [Option](const URbGameUserSettings& S) { return static_cast<double>(S.GetQualityOption(Option)); };
		Row.Set = [Option](URbGameUserSettings& S, double V) { S.SetQualityOption(Option, FMath::RoundToInt(V)); };
		return Row;
	}

	FRow FloatRow(const TCHAR* Id, ERbSettingsPage Page, const FText& Label, const FText& Description, double Min, double Max, double Step, double Default,
		TFunction<float&(URbGameUserSettings&)> Field, TFunction<FText(double)> Format, ERbSettingEffect Effect = ERbSettingEffect::Notify)
	{
		FRow Row;
		Row.Id = Id;
		Row.Page = Page;
		Row.Type = ERbSettingType::Float;
		Row.Label = Label;
		Row.Description = Description;
		Row.Min = Min;
		Row.Max = Max;
		Row.Step = Step;
		Row.Default = Default;
		Row.Effect = Effect;
		Row.Format = MoveTemp(Format);
		Row.Get = [Field](const URbGameUserSettings& S) { return static_cast<double>(Field(const_cast<URbGameUserSettings&>(S))); };
		Row.Set = [Field](URbGameUserSettings& S, double V) { Field(S) = static_cast<float>(V); };
		return Row;
	}

	FRow BoolRow(const TCHAR* Id, ERbSettingsPage Page, const FText& Label, const FText& Description, bool bDefault, TFunction<bool&(URbGameUserSettings&)> Field,
		ERbSettingEffect Effect = ERbSettingEffect::Notify)
	{
		FRow Row;
		Row.Id = Id;
		Row.Page = Page;
		Row.Type = ERbSettingType::Bool;
		Row.Label = Label;
		Row.Description = Description;
		Row.EnumLabels = OffOn();
		Row.Min = 0.0;
		Row.Max = 1.0;
		Row.Step = 1.0;
		Row.Default = bDefault ? 1.0 : 0.0;
		Row.Effect = Effect;
		Row.Get = [Field](const URbGameUserSettings& S) { return Field(const_cast<URbGameUserSettings&>(S)) ? 1.0 : 0.0; };
		Row.Set = [Field](URbGameUserSettings& S, double V) { Field(S) = V >= 0.5; };
		return Row;
	}

	FRow EnumRow(const TCHAR* Id, ERbSettingsPage Page, const FText& Label, const FText& Description, TArray<FText> Labels, double Default,
		TFunction<double(const URbGameUserSettings&)> Get, TFunction<void(URbGameUserSettings&, double)> Set, ERbSettingEffect Effect = ERbSettingEffect::Notify)
	{
		FRow Row;
		Row.Id = Id;
		Row.Page = Page;
		Row.Type = ERbSettingType::Enum;
		Row.Label = Label;
		Row.Description = Description;
		Row.EnumLabels = MoveTemp(Labels);
		Row.Min = 0.0;
		Row.Max = FMath::Max(0, Row.EnumLabels.Num() - 1);
		Row.Step = 1.0;
		Row.Default = Default;
		Row.Effect = Effect;
		Row.Get = MoveTemp(Get);
		Row.Set = MoveTemp(Set);
		return Row;
	}

	FText Degrees(double Value)
	{
		return FText::Format(LOCTEXT("DegreesValue", "{0}°"), RbUi::Number(FMath::RoundToInt(Value)));
	}

	// Width / height of the game viewport (the horizontal FOV the player sees); 16:9 without one (tests, tools).
	double ViewportAspect()
	{
		FVector2D Size = FVector2D::ZeroVector;
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(Size);
		}
		return Size.X > 0.0 && Size.Y > 0.0 ? Size.X / Size.Y : 16.0 / 9.0;
	}

	FText ResolutionText(const FIntPoint& Resolution)
	{
		return FText::Format(LOCTEXT("ResValue", "{0} × {1}"), RbUi::Number(Resolution.X), RbUi::Number(Resolution.Y));
	}

	TArray<FIntPoint> FallbackResolutions()
	{
		return {FIntPoint(1280, 720), FIntPoint(1280, 800), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(1920, 1200), FIntPoint(2560, 1440),
			FIntPoint(3440, 1440), FIntPoint(3840, 2160)};
	}

	// The RHI's fullscreen modes. The query walks every adapter output's display-mode list (DXGI GetDisplayModeList: milliseconds
	// per call), and the resolution row asks for the list several times per frame (value text, both arrows, the description's
	// default), so the answer is kept for a few seconds instead of being re-enumerated in every Slate attribute. Game thread only
	// (Slate attributes, the settings rows, tests).
	const TArray<FIntPoint>& SupportedResolutions()
	{
		static TArray<FIntPoint> Cached;
		static double CachedAt = -1.0e9;
		const double Now = FPlatformTime::Seconds();
		if (Now - CachedAt > 3.0)
		{
			CachedAt = Now;
			Cached.Reset();
			if (FApp::CanEverRender())
			{
				UKismetSystemLibrary::GetSupportedFullscreenResolutions(Cached);
			}
		}
		return Cached;
	}

	TArray<FRow> BuildRows()
	{
		TArray<FRow> Rows;

		// --- Graphics ------------------------------------------------------------------------------------------------------
		{
			FRow Preset;
			Preset.Id = TEXT("gfx.preset");
			Preset.Page = ERbSettingsPage::Graphics;
			Preset.Type = ERbSettingType::Enum;
			Preset.Label = LOCTEXT("PresetLabel", "Quality preset");
			Preset.Description = LOCTEXT("PresetDesc", "Fills in every quality row below. High is tuned for an RTX 3070-class graphics card; Epic and Cinematic are authored for the best possible image and are not capped to any PC. Changing a single row turns the preset into Custom.");
			for (int32 Index = 0; Index <= static_cast<int32>(ERbQualityPreset::Custom); ++Index)
			{
				Preset.EnumLabels.Add(FRbSettingsRegistry::PresetName(Index));
			}
			Preset.Min = 0.0;
			Preset.Max = static_cast<double>(ERbQualityPreset::Custom);
			Preset.Step = 1.0;
			Preset.Default = static_cast<double>(ERbQualityPreset::High);
			Preset.Effect = ERbSettingEffect::QualityRows;
			Preset.Get = [](const URbGameUserSettings& S) { return static_cast<double>(S.GetQualityPreset()); };
			Preset.Set = [](URbGameUserSettings& S, double V) { S.SelectQualityPreset(static_cast<ERbQualityPreset>(FMath::Clamp(FMath::RoundToInt(V), 0, 5))); };
			Rows.Add(MoveTemp(Preset));
		}
		Rows.Add(QualityRow(TEXT("gfx.globalIllumination"), ERbQualityOption::GlobalIllumination, LOCTEXT("GiLabel", "Global illumination"),
			LOCTEXT("GiDesc", "Lumen bounce light: the green glow the cloth throws onto the rails, the lamp light coming back from the walls and the ceiling."),
			{LOCTEXT("GiLiteLow", "Lumen lite (low)"), LOCTEXT("GiLite", "Lumen lite"), LOCTEXT("GiLumen", "Lumen"), LOCTEXT("GiHigh", "Lumen high"),
				LOCTEXT("GiCine", "Lumen cinematic")}));
		Rows.Add(QualityRow(TEXT("gfx.reflections"), ERbQualityOption::Reflections, LOCTEXT("ReflLabel", "Reflections"),
			LOCTEXT("ReflDesc", "Hit lighting mirrors the fully lit lamp, the room and the other balls in every ball and in the rails' lacquer. Screen space cannot show what is behind the camera."),
			{LOCTEXT("ReflSsr", "Screen space"), LOCTEXT("ReflCache", "Lumen surface cache"), LOCTEXT("ReflHitHalf", "Hit lighting, half res"),
				LOCTEXT("ReflHitFull", "Hit lighting, full res"), LOCTEXT("ReflHitCine", "Hit lighting, cinematic")}));
		Rows.Add(QualityRow(TEXT("gfx.shadows"), ERbQualityOption::Shadows, LOCTEXT("ShadowLabel", "Shadows"),
			LOCTEXT("ShadowDesc", "Resolution of the virtual shadow maps. The lamp's small, sharp shadow under every ball is how your eyes read its contact with the cloth.")));
		Rows.Add(QualityRow(TEXT("gfx.volumetricFog"), ERbQualityOption::VolumetricFog, LOCTEXT("FogLabel", "Volumetric haze"),
			LOCTEXT("FogDesc", "Haze in the lamp's light cone and the smoky air of a bar. Off removes the visible light cones."),
			{LOCTEXT("FogOff", "Off"), LOCTEXT("FogLow", "Low"), LOCTEXT("FogHigh", "High"), LOCTEXT("FogEpic", "Epic"), LOCTEXT("FogCine", "Cinematic")}));
		Rows.Add(QualityRow(TEXT("gfx.textures"), ERbQualityOption::Textures, LOCTEXT("TexLabel", "Textures"),
			LOCTEXT("TexDesc", "Texture detail and the size of the streaming pool in video memory. Lower it on graphics cards with less than 8 GB."),
			{LOCTEXT("Tex0", "Low (1000 MB)"), LOCTEXT("Tex1", "Medium (1500 MB)"), LOCTEXT("Tex2", "High (2500 MB)"), LOCTEXT("Tex3", "Epic (3500 MB)"),
				LOCTEXT("Tex4", "Cinematic (4500 MB)")}));
		Rows.Add(QualityRow(TEXT("gfx.effects"), ERbQualityOption::Effects, LOCTEXT("FxLabel", "Effects and materials"),
			LOCTEXT("FxDesc", "Material quality: the clear-coat haze of the balls, the fuzz of the cloth, chalk dust. A change recompiles materials and may stutter briefly."),
			LevelLabels(), ERbSettingApply::StuttersBriefly));
		Rows.Add(QualityRow(TEXT("gfx.antiAliasing"), ERbQualityOption::AntiAliasing, LOCTEXT("AaLabel", "Anti-aliasing"),
			LOCTEXT("AaDesc", "Temporal super resolution quality: smooth edges and stable thin lines - the cue, the rail sights, the pocket jaws.")));
		Rows.Add(QualityRow(TEXT("gfx.postProcess"), ERbQualityOption::PostProcess, LOCTEXT("PpLabel", "Post-processing"),
			LOCTEXT("PpDesc", "Quality of bloom around the lamp, depth of field and exposure.")));
		Rows.Add(QualityRow(TEXT("gfx.shading"), ERbQualityOption::Shading, LOCTEXT("ShadingLabel", "Shading"),
			LOCTEXT("ShadingDesc", "Shading detail: specular highlights and the light inside the balls.")));
		Rows.Add(QualityRow(TEXT("gfx.viewDistance"), ERbQualityOption::ViewDistance, LOCTEXT("ViewLabel", "View distance"),
			LOCTEXT("ViewDesc", "How far small objects keep their detail. The rooms are small, so the cost is low.")));
		Rows.Add(QualityRow(TEXT("gfx.foliage"), ERbQualityOption::Foliage, LOCTEXT("FoliageLabel", "Foliage"),
			LOCTEXT("FoliageDesc", "Density of plants. The current venues have none; it matters later, outside.")));
		{
			FRow Screen = FloatRow(TEXT("gfx.resolutionScale"), ERbSettingsPage::Graphics, LOCTEXT("ResScaleLabel", "Resolution scale"),
				LOCTEXT("ResScaleDesc", "Internal render resolution before temporal upscaling to your screen. 100 % renders natively; above 100 % supersamples."),
				25.0, 200.0, 1.0, 66.662, nullptr, [](double V) { return FText::Format(LOCTEXT("ResScaleValue", "{0} %"), RbUi::Number(FMath::RoundToInt(V))); },
				ERbSettingEffect::QualityRows);
			Screen.bPresetDriven = true;
			for (int32 Level = 0; Level < NumPresetLevels; ++Level)
			{
				Screen.PresetValues[Level] = URbGameUserSettings::PresetScreenPercentage(Level);
			}
			Screen.Get = [](const URbGameUserSettings& S) { return static_cast<double>(S.GetScreenPercentage()); };
			Screen.Set = [](URbGameUserSettings& S, double V) { S.SetScreenPercentage(static_cast<float>(V)); };
			Rows.Add(MoveTemp(Screen));
		}
		Rows.Add(BoolRow(TEXT("gfx.depthOfField"), ERbSettingsPage::Graphics, LOCTEXT("DofLabel", "Depth of field"),
			LOCTEXT("DofDesc", "The eye's focus: what is far from where you look goes slightly soft, as through a 4 mm pupil. Off keeps everything sharp."), true,
			[](URbGameUserSettings& S) -> bool& { return S.bDepthOfField; }));
		Rows.Add(FloatRow(TEXT("gfx.motionBlur"), ERbSettingsPage::Graphics, LOCTEXT("BlurLabel", "Motion blur"),
			LOCTEXT("BlurDesc", "Blur of fast balls and quick head turns, like a camera shutter. 0 % turns it off."), 0.0, 1.0, 0.05, 1.0,
			[](URbGameUserSettings& S) -> float& { return S.MotionBlurScale; }, &Percent));
		Rows.Add(FloatRow(TEXT("gfx.filmGrain"), ERbSettingsPage::Graphics, LOCTEXT("GrainLabel", "Film grain"),
			LOCTEXT("GrainDesc", "Fine, sensor-like noise in the dark parts of the image. 0 % turns it off."), 0.0, 1.0, 0.05, 1.0,
			[](URbGameUserSettings& S) -> float& { return S.GrainScale; }, &Percent));

		// --- Display -------------------------------------------------------------------------------------------------------
		{
			FRow Mode = EnumRow(TEXT("dsp.windowMode"), ERbSettingsPage::Display, LOCTEXT("WindowLabel", "Window mode"),
				LOCTEXT("WindowDesc", "Windowed fullscreen switches to other programs instantly. A new mode asks to be kept within 15 seconds, else it reverts."),
				{LOCTEXT("WinFull", "Fullscreen"), LOCTEXT("WinBorderless", "Windowed fullscreen"), LOCTEXT("WinWindowed", "Windowed")},
				static_cast<double>(EWindowMode::WindowedFullscreen),
				[](const URbGameUserSettings& S) { return static_cast<double>(S.GetFullscreenMode()); },
				[](URbGameUserSettings& S, double V) { S.SetFullscreenMode(EWindowMode::ConvertIntToWindowMode(FMath::Clamp(FMath::RoundToInt(V), 0, 2))); },
				ERbSettingEffect::Resolution);
			Mode.Apply = ERbSettingApply::ConfirmRevert15s;
			Rows.Add(MoveTemp(Mode));
		}
		{
			FRow Res;
			Res.Id = TEXT("dsp.resolution");
			Res.Page = ERbSettingsPage::Display;
			Res.Type = ERbSettingType::Enum;
			Res.Label = LOCTEXT("ResLabel", "Resolution");
			Res.Description = LOCTEXT("ResDesc", "Output resolution in fullscreen and windowed mode. A new resolution asks to be kept within 15 seconds, else it reverts.");
			Res.Apply = ERbSettingApply::ConfirmRevert15s;
			Res.Effect = ERbSettingEffect::Resolution;
			Res.Step = 1.0;
			Res.DynamicEnumLabels = [](const URbGameUserSettings& S)
			{
				TArray<FText> Labels;
				for (const FIntPoint& R : FRbSettingsRegistry::ResolutionList(S))
				{
					Labels.Add(ResolutionText(R));
				}
				return Labels;
			};
			Res.DynamicEnumCount = [](const URbGameUserSettings& S) { return FRbSettingsRegistry::ResolutionList(S).Num(); };
			Res.DynamicEnumLabel = [](const URbGameUserSettings& S, int32 Index)
			{
				const TArray<FIntPoint> List = FRbSettingsRegistry::ResolutionList(S);
				return List.IsValidIndex(Index) ? ResolutionText(List[Index]) : RbUi::Number(Index);
			};
			Res.Get = [](const URbGameUserSettings& S)
			{
				const TArray<FIntPoint> List = FRbSettingsRegistry::ResolutionList(S);
				return static_cast<double>(FMath::Max(0, List.IndexOfByKey(S.GetScreenResolution())));
			};
			Res.Set = [](URbGameUserSettings& S, double V)
			{
				const TArray<FIntPoint> List = FRbSettingsRegistry::ResolutionList(S);
				if (List.Num() > 0)
				{
					S.SetScreenResolution(List[FMath::Clamp(FMath::RoundToInt(V), 0, List.Num() - 1)]);
				}
			};
			Res.IsAvailable = [](const URbGameUserSettings& S, FText& OutReason)
			{
				if (S.GetFullscreenMode() == EWindowMode::WindowedFullscreen)
				{
					OutReason = LOCTEXT("ResBorderless", "Windowed fullscreen always uses the desktop resolution.");
					return false;
				}
				return true;
			};
			Rows.Add(MoveTemp(Res));
		}
		{
			TArray<FText> Labels;
			for (const float Cap : FRbSettingsRegistry::FrameCaps())
			{
				Labels.Add(Cap > 0.0f ? FText::Format(LOCTEXT("FpsValue", "{0} fps"), RbUi::Number(FMath::RoundToInt(Cap))) : LOCTEXT("FpsUnlimited", "Unlimited"));
			}
			Rows.Add(EnumRow(TEXT("dsp.frameCap"), ERbSettingsPage::Display, LOCTEXT("CapLabel", "Frame rate limit"),
				LOCTEXT("CapDesc", "Upper limit of the frame rate. The stroke keeps its own input clock at any frame rate."), MoveTemp(Labels),
				static_cast<double>(FRbSettingsRegistry::FrameCaps().Num() - 1),
				[](const URbGameUserSettings& S)
				{
					const TArray<float>& Caps = FRbSettingsRegistry::FrameCaps();
					const float Limit = S.GetFrameRateLimit();
					if (Limit <= 0.0f)
					{
						return static_cast<double>(Caps.Num() - 1);
					}
					int32 Best = 0;
					for (int32 Index = 0; Index < Caps.Num() - 1; ++Index)
					{
						if (FMath::Abs(Caps[Index] - Limit) < FMath::Abs(Caps[Best] - Limit))
						{
							Best = Index;
						}
					}
					return static_cast<double>(Best);
				},
				[](URbGameUserSettings& S, double V)
				{
					const TArray<float>& Caps = FRbSettingsRegistry::FrameCaps();
					S.SetFrameRateLimit(Caps[FMath::Clamp(FMath::RoundToInt(V), 0, Caps.Num() - 1)]);
				},
				ERbSettingEffect::NonResolution));
		}
		{
			FRow VSync;
			VSync.Id = TEXT("dsp.vsync");
			VSync.Page = ERbSettingsPage::Display;
			VSync.Type = ERbSettingType::Bool;
			VSync.Label = LOCTEXT("VSyncLabel", "V-sync");
			VSync.Description = LOCTEXT("VSyncDesc", "Synchronises the frames with the display: no tearing, slightly more input latency.");
			VSync.EnumLabels = OffOn();
			VSync.Min = 0.0;
			VSync.Max = 1.0;
			VSync.Step = 1.0;
			VSync.Default = 0.0;
			VSync.Effect = ERbSettingEffect::NonResolution;
			VSync.Get = [](const URbGameUserSettings& S) { return S.IsVSyncEnabled() ? 1.0 : 0.0; };
			VSync.Set = [](URbGameUserSettings& S, double V) { S.SetVSyncEnabled(V >= 0.5); };
			Rows.Add(MoveTemp(VSync));
		}

		// --- Camera --------------------------------------------------------------------------------------------------------
		Rows.Add(EnumRow(TEXT("cam.look"), ERbSettingsPage::Camera, LOCTEXT("LookLabel", "Look"),
			LOCTEXT("LookDesc", "Eyes: how you see it - a natural field of view, focus where you aim, stabilised like your own eyes. Headcam: a head-mounted action camera, every head motion shows."),
			{LOCTEXT("LookEyes", "Eyes"), LOCTEXT("LookHeadcam", "Headcam")}, 0.0,
			[](const URbGameUserSettings& S) { return S.CameraPreset == ERbCameraPreset::Headcam ? 1.0 : 0.0; },
			[](URbGameUserSettings& S, double V) { S.CameraPreset = V >= 0.5 ? ERbCameraPreset::Headcam : ERbCameraPreset::Eyes; }));
		Rows.Add(FloatRow(TEXT("cam.fov"), ERbSettingsPage::Camera, LOCTEXT("FovLabel", "Field of view"),
			LOCTEXT("FovDesc", "Vertical field of view (the horizontal one follows your screen's shape). 50° feels natural on a desktop monitor; wider shows more of the room and makes the table look smaller."),
			40.0, 75.0, 1.0, 50.0, [](URbGameUserSettings& S) -> float& { return S.VerticalFovDeg; },
			[](double V)
			{
				return FText::Format(LOCTEXT("FovValue", "{0}  ·  {1} wide"), Degrees(V), Degrees(FRbSettingsRegistry::HorizontalFovDeg(V, ViewportAspect())));
			}));
		Rows.Add(FloatRow(TEXT("cam.bodySway"), ERbSettingsPage::Camera, LOCTEXT("SwayLabel", "Body sway and breathing"),
			LOCTEXT("SwayDesc", "Breathing and the small sway of your body, standing and down on the shot."), 0.0, 1.0, 0.05, 1.0,
			[](URbGameUserSettings& S) -> float& { return S.Camera.BodySwayScale; }, &Percent));
		Rows.Add(FloatRow(TEXT("cam.headBob"), ERbSettingsPage::Camera, LOCTEXT("BobLabel", "Head bob"),
			LOCTEXT("BobDesc", "Head motion while you walk around the table."), 0.0, 1.0, 0.05, 1.0,
			[](URbGameUserSettings& S) -> float& { return S.HeadBobScale; }, &Percent));
		{
			FRow Shake = FloatRow(TEXT("cam.mountShake"), ERbSettingsPage::Camera, LOCTEXT("ShakeLabel", "Mount shake"),
				LOCTEXT("ShakeDesc", "Jitter of the head-mounted camera."), 0.0, 1.0, 0.05, 1.0,
				[](URbGameUserSettings& S) -> float& { return S.Camera.MountShakeScale; }, &Percent);
			Shake.IsAvailable = [](const URbGameUserSettings& S, FText& OutReason)
			{
				if (S.CameraPreset != ERbCameraPreset::Headcam)
				{
					OutReason = LOCTEXT("ShakeHeadcamOnly", "Headcam only.");
					return false;
				}
				return true;
			};
			Rows.Add(MoveTemp(Shake));
		}
		Rows.Add(EnumRow(TEXT("cam.posture"), ERbSettingsPage::Camera, LOCTEXT("PostureLabel", "Getting down"),
			LOCTEXT("PostureDesc", "How you get down on the shot and stand up again. Natural: a human movement, a little different every time. Quick: a short plain ease. Cut: instant."),
			{LOCTEXT("PostureNatural", "Natural"), LOCTEXT("PostureQuick", "Quick"), LOCTEXT("PostureCut", "Cut")}, 0.0,
			[](const URbGameUserSettings& S) { return static_cast<double>(S.Camera.PostureTransition); },
			[](URbGameUserSettings& S, double V) { S.Camera.PostureTransition = static_cast<ERbPostureTransition>(FMath::Clamp(FMath::RoundToInt(V), 0, 2)); }));
		{
			FRow Reduced;
			Reduced.Id = TEXT("cam.reducedMotion");
			Reduced.Page = ERbSettingsPage::Camera;
			Reduced.Type = ERbSettingType::Bool;
			Reduced.Label = LOCTEXT("ReducedLabel", "Reduced motion");
			Reduced.Description = LOCTEXT("ReducedDesc", "Sets head bob 0, body sway 30 %, mount shake 0, motion blur 0 and quick posture changes. The rows stay editable; switching it off restores your previous values.");
			Reduced.EnumLabels = OffOn();
			Reduced.Min = 0.0;
			Reduced.Max = 1.0;
			Reduced.Step = 1.0;
			Reduced.Default = 0.0;
			Reduced.Get = [](const URbGameUserSettings& S) { return S.bReducedMotion ? 1.0 : 0.0; };
			Reduced.Set = [](URbGameUserSettings& S, double V) { S.SetReducedMotion(V >= 0.5); };
			Rows.Add(MoveTemp(Reduced));
		}

		// --- Controls ------------------------------------------------------------------------------------------------------
		Rows.Add(FloatRow(TEXT("ctl.mouseDpi"), ERbSettingsPage::Controls, LOCTEXT("DpiLabel", "Mouse DPI"),
			LOCTEXT("DpiDesc", "Your mouse's resolution in counts per inch. Aim, look and stroke speeds are defined per centimetre of mouse travel, so set this to your mouse's real DPI."),
			200.0, 6400.0, 50.0, 800.0, [](URbGameUserSettings& S) -> float& { return S.MouseDpi; },
			[](double V) { return FText::Format(LOCTEXT("DpiValue", "{0} DPI"), RbUi::Number(FMath::RoundToInt(V))); }));
		{
			FRow Aim = FloatRow(TEXT("ctl.aimSpeed"), ERbSettingsPage::Controls, LOCTEXT("AimLabel", "Aim speed"),
				LOCTEXT("AimDesc", "How far the cue turns per centimetre of mouse travel while you are down on the shot. The value shows the mouse travel for a quarter turn."),
				0.1, 5.0, 0.05, 1.0, [](URbGameUserSettings& S) -> float& { return S.Controls.AimSensitivity; }, nullptr);
			Aim.Format = [](double V)
			{
				const double DegPerCm = FRbControlSettings().AimDegreesPerCm * V;
				const double Cm = DegPerCm > 0.0 ? 90.0 / DegPerCm : 0.0;
				return FText::Format(LOCTEXT("AimValue", "{0}  ·  90° per {1} cm"), Multiplier(V), Fixed(Cm, Cm < 10.0 ? 2 : 1));
			};
			Rows.Add(MoveTemp(Aim));
		}
		Rows.Add(FloatRow(TEXT("ctl.fineAim"), ERbSettingsPage::Controls, LOCTEXT("FineLabel", "Fine aim (Shift)"),
			LOCTEXT("FineDesc", "Aim speed while Shift is held, as a fraction of the normal aim speed."), 0.03, 0.5, 0.005, 0.075,
			[](URbGameUserSettings& S) -> float& { return S.Controls.FineAimFactor; },
			[](double V)
			{
				return FText::Format(LOCTEXT("FineValue", "{0}  ·  {1}× finer"), Fixed(V, 3),
					Fixed(V > 0.0 ? 1.0 / V : 0.0, 1));
			}));
		Rows.Add(FloatRow(TEXT("ctl.aimAcceleration"), ERbSettingsPage::Controls, LOCTEXT("AccelLabel", "Aim acceleration"),
			LOCTEXT("AccelDesc", "Off is linear. Higher values make slow hand motion finer and fast hand motion coarser."), 0.0, 1.0, 0.05, 0.0,
			[](URbGameUserSettings& S) -> float& { return S.Controls.AimAcceleration; },
			[](double V) { return V <= 0.0 ? LOCTEXT("AccelOff", "Off") : Percent(V); }));
		Rows.Add(FloatRow(TEXT("ctl.lookSensitivity"), ERbSettingsPage::Controls, LOCTEXT("LookSensLabel", "Look sensitivity"),
			LOCTEXT("LookSensDesc", "How far you look around per centimetre of mouse travel while standing."), 0.1, 5.0, 0.05, 1.0,
			[](URbGameUserSettings& S) -> float& { return S.Controls.LookSensitivity; }, &Multiplier));
		Rows.Add(BoolRow(TEXT("ctl.invertLookY"), ERbSettingsPage::Controls, LOCTEXT("InvertLabel", "Invert look up / down"),
			LOCTEXT("InvertDesc", "Moving the mouse forward looks down."), false, [](URbGameUserSettings& S) -> bool& { return S.Controls.bInvertLookY; }));
		Rows.Add(FloatRow(TEXT("ctl.strokeSensitivity"), ERbSettingsPage::Controls, LOCTEXT("StrokeLabel", "Stroke sensitivity"),
			LOCTEXT("StrokeDesc", "How far the cue moves per centimetre of mouse travel in the stroke, relative to your mouse's DPI."), 0.5, 2.0, 0.05, 1.0,
			[](URbGameUserSettings& S) -> float& { return S.Controls.StrokeSensitivity; }, &Percent));
		Rows.Add(BoolRow(TEXT("ctl.keyHints"), ERbSettingsPage::Controls, LOCTEXT("HintsLabel", "Key hints"),
			LOCTEXT("HintsDesc", "Small hints in the lower left for what you can do right now, for example [RMB] Get down. They fade after three seconds."), true,
			[](URbGameUserSettings& S) -> bool& { return S.bShowKeyHints; }));

		// --- Audio ---------------------------------------------------------------------------------------------------------
		const auto Volume = [&Rows](const TCHAR* Id, const FText& Label, const FText& Description, double Default, TFunction<float&(URbGameUserSettings&)> Field)
		{
			Rows.Add(FloatRow(Id, ERbSettingsPage::Audio, Label, Description, 0.0, 1.0, 0.05, Default, MoveTemp(Field), &Percent));
		};
		Volume(TEXT("aud.master"), LOCTEXT("MasterLabel", "Master"), LOCTEXT("MasterDesc", "The volume of everything."), 1.0,
			[](URbGameUserSettings& S) -> float& { return S.Volumes.Master; });
		Volume(TEXT("aud.music"), LOCTEXT("MusicLabel", "Music"), LOCTEXT("MusicDesc", "The jukebox and the menu music."), 0.7,
			[](URbGameUserSettings& S) -> float& { return S.Volumes.Music; });
		Volume(TEXT("aud.table"), LOCTEXT("TableLabel", "Table and balls"), LOCTEXT("TableDesc", "The cue tip, ball collisions, cushions, pockets and the ball return."), 1.0,
			[](URbGameUserSettings& S) -> float& { return S.Volumes.Table; });
		Volume(TEXT("aud.ambience"), LOCTEXT("AmbienceLabel", "Ambience"), LOCTEXT("AmbienceDesc", "Room tone, the other guests, the street outside."), 0.8,
			[](URbGameUserSettings& S) -> float& { return S.Volumes.Ambience; });
		Volume(TEXT("aud.voices"), LOCTEXT("VoicesLabel", "Voices"), LOCTEXT("VoicesDesc", "The people in the venue talking."), 1.0,
			[](URbGameUserSettings& S) -> float& { return S.Volumes.Voices; });
		Volume(TEXT("aud.interface"), LOCTEXT("InterfaceLabel", "Interface"), LOCTEXT("InterfaceDesc", "Menu sounds."), 0.6,
			[](URbGameUserSettings& S) -> float& { return S.Volumes.Interface; });
		return Rows;
	}

	URbGameUserSettings* GlobalSettings()
	{
		return URbGameUserSettings::Get();
	}

	FString ValueString(const FRbSettingDef& Row, const URbGameUserSettings& S)
	{
		const double V = FRbSettingsRegistry::GetValue(Row, S);
		return FString::Printf(TEXT("%-24s %-10g %s"), *Row.Id.ToString(), V, *FRbSettingsRegistry::FormatValue(Row, S, V).ToString());
	}

#if !UE_BUILD_SHIPPING
	FAutoConsoleCommand GRbSettingsDump(TEXT("rb.Settings.Dump"), TEXT("Log every settings row (id, value, text)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (const URbGameUserSettings* S = GlobalSettings())
			{
				UE_LOG(LogRawBreak, Display, TEXT("rb.Settings: preset %s"), *FRbSettingsRegistry::PresetLabel(*S).ToString());
				for (const FRbSettingDef& Row : FRbSettingsRegistry::Rows())
				{
					UE_LOG(LogRawBreak, Display, TEXT("rb.Settings: %s"), *ValueString(Row, *S));
				}
			}
		}));

	FAutoConsoleCommand GRbSettingsSet(TEXT("rb.Settings.Set"), TEXT("rb.Settings.Set <id> <value>: store and apply one settings row (not saved)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			URbGameUserSettings* S = GlobalSettings();
			if (!S || Args.Num() < 2 || !FRbSettingsRegistry::SetAndApply(FName(*Args[0]), *S, FCString::Atod(*Args[1])))
			{
				UE_LOG(LogRawBreak, Warning, TEXT("rb.Settings.Set <id> <value> (ids: rb.Settings.Dump)"));
				return;
			}
			UE_LOG(LogRawBreak, Display, TEXT("rb.Settings: %s"), *ValueString(*FRbSettingsRegistry::Find(FName(*Args[0])), *S));
		}));

	FAutoConsoleCommand GRbSettingsReset(TEXT("rb.Settings.Reset"), TEXT("rb.Settings.Reset [Graphics|Display|Camera|Controls|Audio|all]: restore defaults (not saved)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			URbGameUserSettings* S = GlobalSettings();
			if (!S)
			{
				return;
			}
			for (int32 Page = 0; Page < static_cast<int32>(ERbSettingsPage::Count); ++Page)
			{
				const FString Name = FRbSettingsRegistry::PageName(static_cast<ERbSettingsPage>(Page)).ToString();
				if (Args.Num() == 0 || Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase) || Args[0].Equals(Name, ESearchCase::IgnoreCase))
				{
					FRbSettingsRegistry::ResetPage(static_cast<ERbSettingsPage>(Page), *S);
				}
			}
		}));

	FAutoConsoleCommand GRbSettingsSave(TEXT("rb.Settings.Save"), TEXT("Save the user settings (GameUserSettings.ini)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (URbGameUserSettings* S = GlobalSettings())
			{
				S->SaveSettings();
				UE_LOG(LogRawBreak, Display, TEXT("rb.Settings: saved"));
			}
		}));
#endif
}

const TArray<FRbSettingDef>& FRbSettingsRegistry::Rows()
{
	static const TArray<FRbSettingDef> Built = RbSettingsPrivate::BuildRows();
	return Built;
}

const FRbSettingDef* FRbSettingsRegistry::Find(FName Id)
{
	return Rows().FindByPredicate([Id](const FRbSettingDef& Row) { return Row.Id == Id; });
}

TArray<const FRbSettingDef*> FRbSettingsRegistry::RowsOnPage(ERbSettingsPage Page)
{
	TArray<const FRbSettingDef*> Out;
	for (const FRbSettingDef& Row : Rows())
	{
		if (Row.Page == Page)
		{
			Out.Add(&Row);
		}
	}
	return Out;
}

double FRbSettingsRegistry::GetValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings)
{
	return Row.Get ? Row.Get(Settings) : 0.0;
}

int32 FRbSettingsRegistry::NumEnumValues(const FRbSettingDef& Row, const URbGameUserSettings& Settings)
{
	if (Row.Type == ERbSettingType::Bool)
	{
		return 2;
	}
	if (Row.Type != ERbSettingType::Enum)
	{
		return 0;
	}
	if (Row.DynamicEnumCount)
	{
		return Row.DynamicEnumCount(Settings); // no label is formatted to count them (the arrows ask every frame)
	}
	return Row.DynamicEnumLabels ? Row.DynamicEnumLabels(Settings).Num() : Row.EnumLabels.Num(); // no label copy for fixed rows
}

TArray<FText> FRbSettingsRegistry::EnumLabels(const FRbSettingDef& Row, const URbGameUserSettings& Settings)
{
	return Row.DynamicEnumLabels ? Row.DynamicEnumLabels(Settings) : Row.EnumLabels;
}

void FRbSettingsRegistry::SetValue(const FRbSettingDef& Row, URbGameUserSettings& Settings, double Value)
{
	if (!Row.Set)
	{
		return;
	}
	switch (Row.Type)
	{
	case ERbSettingType::Enum:
		Value = FMath::Clamp(static_cast<double>(FMath::RoundToInt(Value)), 0.0, static_cast<double>(FMath::Max(0, NumEnumValues(Row, Settings) - 1)));
		break;
	case ERbSettingType::Bool:
		Value = Value >= 0.5 ? 1.0 : 0.0;
		break;
	case ERbSettingType::Float:
		Value = FMath::Clamp(Value, Row.Min, Row.Max);
		break;
	case ERbSettingType::Action:
		return;
	}
	Row.Set(Settings, Value);
}

void FRbSettingsRegistry::Apply(const FRbSettingDef& Row, URbGameUserSettings& Settings)
{
	switch (Row.Effect)
	{
	case ERbSettingEffect::QualityRows:
		Settings.ApplyQualityRows();
		Settings.NotifySettingsChanged();
		break;
	case ERbSettingEffect::NonResolution:
		Settings.ApplyNonResolutionSettings(); // v-sync, frame cap (+ quality rows, notify)
		break;
	case ERbSettingEffect::Resolution:
		Settings.ApplyResolutionSettings(false);
		Settings.NotifySettingsChanged();
		break;
	case ERbSettingEffect::Notify:
		Settings.NotifySettingsChanged();
		break;
	}
}

bool FRbSettingsRegistry::SetAndApply(FName Id, URbGameUserSettings& Settings, double Value)
{
	const FRbSettingDef* Row = Find(Id);
	if (!Row)
	{
		return false;
	}
	SetValue(*Row, Settings, Value);
	Apply(*Row, Settings);
	return true;
}

double FRbSettingsRegistry::StepValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings, int32 Direction)
{
	const double Current = GetValue(Row, Settings);
	const int32 Dir = Direction > 0 ? 1 : (Direction < 0 ? -1 : 0);
	switch (Row.Type)
	{
	case ERbSettingType::Bool:
		return Dir == 0 ? Current : (Current >= 0.5 ? 0.0 : 1.0);
	case ERbSettingType::Enum:
		return FMath::Clamp(static_cast<double>(FMath::RoundToInt(Current) + Dir), 0.0, static_cast<double>(FMath::Max(0, NumEnumValues(Row, Settings) - 1)));
	case ERbSettingType::Float:
	{
		if (Row.Step <= 0.0 || Dir == 0)
		{
			return Current;
		}
		// Off-grid values (a preset's 66.662 %) go to the next grid value in the step's direction, never past it.
		const double Grid = (Current - Row.Min) / Row.Step;
		const double N = Dir > 0 ? FMath::FloorToDouble(Grid + 1e-6) + 1.0 : FMath::CeilToDouble(Grid - 1e-6) - 1.0;
		return FMath::Clamp(Row.Min + N * Row.Step, Row.Min, Row.Max);
	}
	case ERbSettingType::Action:
		break;
	}
	return Current;
}

FText FRbSettingsRegistry::FormatValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings, double Value)
{
	if (Row.Type == ERbSettingType::Enum || Row.Type == ERbSettingType::Bool)
	{
		const int32 Index = FMath::RoundToInt(Value);
		if (!Row.DynamicEnumLabels)
		{
			return Row.EnumLabels.IsValidIndex(Index) ? Row.EnumLabels[Index] : RbUi::Number(Index); // the value text of every frame: no copy
		}
		if (Row.DynamicEnumLabel)
		{
			return Row.DynamicEnumLabel(Settings, Index); // one label, not the whole list
		}
		const TArray<FText> Labels = Row.DynamicEnumLabels(Settings);
		return Labels.IsValidIndex(Index) ? Labels[Index] : RbUi::Number(Index);
	}
	if (Row.Format)
	{
		return Row.Format(Value);
	}
	return RbSettingsPrivate::Fixed(Value, 2);
}

bool FRbSettingsRegistry::IsAvailable(const FRbSettingDef& Row, const URbGameUserSettings& Settings, FText* OutReason)
{
	if (!Row.IsAvailable)
	{
		return true;
	}
	FText Reason;
	const bool bAvailable = Row.IsAvailable(Settings, Reason);
	if (OutReason)
	{
		*OutReason = Reason;
	}
	return bAvailable;
}

double FRbSettingsRegistry::DefaultValue(const FRbSettingDef& Row, const URbGameUserSettings& Settings)
{
	if (Row.bPresetDriven)
	{
		const int32 Level = URbGameUserSettings::PresetLevel(Settings.GetCustomBasePreset());
		return Row.PresetValues[FMath::Clamp(Level < 0 ? 2 : Level, 0, 4)];
	}
	if (Row.Id == TEXT("dsp.resolution"))
	{
		const FIntPoint Desktop = Settings.GetDesktopResolution();
		return static_cast<double>(FMath::Max(0, ResolutionList(Settings).IndexOfByKey(Desktop)));
	}
	return Row.Default;
}

void FRbSettingsRegistry::ResetPage(ERbSettingsPage Page, URbGameUserSettings& Settings)
{
	if (Page == ERbSettingsPage::Graphics)
	{
		Settings.SelectQualityPreset(ERbQualityPreset::High);
	}
	if (Page == ERbSettingsPage::Camera && Settings.bReducedMotion)
	{
		Settings.SetReducedMotion(false); // then every camera row goes to its default below
	}
	TSet<ERbSettingEffect> Effects;
	for (const FRbSettingDef* Row : RowsOnPage(Page))
	{
		if (Row->bPresetDriven || Row->Id == TEXT("gfx.preset") || Row->Apply == ERbSettingApply::ConfirmRevert15s)
		{
			continue; // the preset above; window mode / resolution ask to confirm on their own rows
		}
		SetValue(*Row, Settings, DefaultValue(*Row, Settings));
		Effects.Add(Row->Effect);
	}
	if (Page == ERbSettingsPage::Camera)
	{
		Settings.ReducedMotionBackup = FRbReducedMotionBackup();
	}
	if (Page == ERbSettingsPage::Graphics)
	{
		Effects.Add(ERbSettingEffect::QualityRows);
	}
	if (Effects.Contains(ERbSettingEffect::NonResolution))
	{
		Settings.ApplyNonResolutionSettings(); // includes the quality rows and the notification
	}
	else if (Effects.Contains(ERbSettingEffect::QualityRows))
	{
		Settings.ApplyQualityRows();
	}
	Settings.NotifySettingsChanged();
}

FText FRbSettingsRegistry::PageName(ERbSettingsPage Page)
{
	switch (Page)
	{
	case ERbSettingsPage::Graphics: return LOCTEXT("PageGraphics", "Graphics");
	case ERbSettingsPage::Display: return LOCTEXT("PageDisplay", "Display");
	case ERbSettingsPage::Camera: return LOCTEXT("PageCamera", "Camera");
	case ERbSettingsPage::Controls: return LOCTEXT("PageControls", "Controls");
	case ERbSettingsPage::Audio: return LOCTEXT("PageAudio", "Audio");
	case ERbSettingsPage::Count: break;
	}
	return FText::GetEmpty();
}

FText FRbSettingsRegistry::PresetName(int32 PresetIndex)
{
	switch (static_cast<ERbQualityPreset>(PresetIndex))
	{
	case ERbQualityPreset::Low: return LOCTEXT("PresetLow", "Low");
	case ERbQualityPreset::Medium: return LOCTEXT("PresetMedium", "Medium");
	case ERbQualityPreset::High: return LOCTEXT("PresetHigh", "High");
	case ERbQualityPreset::Epic: return LOCTEXT("PresetEpic", "Epic");
	case ERbQualityPreset::Cinematic: return LOCTEXT("PresetCinematic", "Cinematic");
	case ERbQualityPreset::Custom: return LOCTEXT("PresetCustom", "Custom");
	}
	return FText::GetEmpty();
}

FText FRbSettingsRegistry::PresetLabel(const URbGameUserSettings& Settings)
{
	if (Settings.GetQualityPreset() != ERbQualityPreset::Custom)
	{
		return PresetName(static_cast<int32>(Settings.GetQualityPreset()));
	}
	return FText::Format(LOCTEXT("PresetCustomBased", "Custom (based on {0})"), PresetName(static_cast<int32>(Settings.GetCustomBasePreset())));
}

TArray<FIntPoint> FRbSettingsRegistry::ResolutionList(const URbGameUserSettings& Settings)
{
	TArray<FIntPoint> List = RbSettingsPrivate::SupportedResolutions();
	if (List.Num() == 0)
	{
		List = RbSettingsPrivate::FallbackResolutions();
	}
	List.RemoveAll([](const FIntPoint& R) { return R.X < 1280 || R.Y < 720; }); // UX-T07: the layouts are checked down to 1280 x 720
	const FIntPoint Current = Settings.GetScreenResolution();
	if (Current.X > 0 && Current.Y > 0)
	{
		List.AddUnique(Current);
	}
	List.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	TArray<FIntPoint> Unique;
	for (const FIntPoint& R : List)
	{
		Unique.AddUnique(R);
	}
	return Unique;
}

const TArray<float>& FRbSettingsRegistry::FrameCaps()
{
	static const TArray<float> Caps = {30.0f, 60.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f, 0.0f};
	return Caps;
}

double FRbSettingsRegistry::HorizontalFovDeg(double VerticalDeg, double Aspect)
{
	return FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(0.5 * VerticalDeg)) * Aspect));
}

#undef LOCTEXT_NAMESPACE
