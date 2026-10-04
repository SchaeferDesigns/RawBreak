// M2-D unit tests of the menus (Docs/ue-architecture.md 18.4; ui-ux 20.1). World-free except UX-T25; the screens run against a
// recording host (FRbUiHost double) and a transient URbGameUserSettings, so no test touches the editor's own settings file.
//   Unit.UI.Tokens          UX-T04 subset: every text token pair used by the screens >= 4.5:1 on its surface and over a white
//                           worst case behind its scrim; non-text marks >= 3:1
//   Unit.UI.TextSize        UX-T05: body height of every mixed-case style (the cap height of the uppercase ones) >= 18 px at
//                           1080p and >= 12 px at 1280x800 (font metrics through Slate's font measure)
//   Unit.UI.NoClipping      UX-T07: every screen and state laid out at the logical sizes of 1280x800, 1920x1080 and 3840x2160 (the
//                           DPI rule makes 4K the 1080p layout): no single-line text wider / taller than its allotted box, the
//                           content fits the screen
//   Unit.UI.DpiRule         UX-T26: layout scale = height / 1080 through the engine's UI settings; no screen carries its own DPI
//                           scaler; Tab never moves the focus
//   Unit.UI.NavigationKeys  UX-T09 keyboard only: title (Play -> venue -> mode -> StartVenue; Back at every level; Quit only with
//                           a confirm), pause (Resume, Settings, quits only through a confirm), settings (pages Q/E, rows, values,
//                           Back saves), dialog (Esc = cancel, timeout = cancel); one focused item after every step
//   Unit.UI.NavigationMouse UX-T09 mouse only: hover moves the focus, clicks activate, row arrows (clamped) / the stepper's value
//                           (= Enter: wraps, never a dead click) / slider / tabs / footer hints
//   Unit.UI.KeyRepeat       UX-T09: a held Enter / Esc acts once (no chain title -> venue -> travel, no resume from the Esc that
//                           opened the pause menu); a held arrow keeps moving the focus / changing the row
//   Unit.UI.DisplayRevert   ui-ux 13.2: a display change that is not kept reverts to the video mode from before - the resolution
//                           itself, also a window size that is listed only while it is the current one; the timeout reverts too
//   Unit.UI.KeyHints        FRbKeyHintsModel per context (stroke phases, ball in hand, decisions, interaction, setting off, menu,
//                           replay) and the overlay component's 3 s hold + fade
//   Unit.UI.TwoTables       UX-T25: two tables with two matches: the overlay, the pause block and the key-hint context bind to the
//                           player's table only; moving the player-table tag rebinds at the next refresh
//   Unit.UI.PointerFocus    UX-T09 keyboard + mouse together: a still cursor that a scroll / a new screen puts over an item never
//                           takes the focus (only real motion does); a pointer-focused row does not scroll; a slider drag ends when
//                           the capture is lost or the button is up; the Space preview ends when a dialog covers the screen; the
//                           resolution row's count / label fast path equals its full label list
// Owner: M2-D.

#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Settings/RbGameUserSettings.h"
#include "Settings/RbSettingsRegistry.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"
#include "Tests/RbTestFlags.h"
#include "UI/Core/RbDpiScalingRule.h"
#include "UI/Core/RbUiStyle.h"
#include "UI/Core/RbUiSubsystem.h"
#include "UI/Core/SRbScreen.h"
#include "UI/Live/SRbKeyHints.h"
#include "UI/RbOverlayComponent.h"
#include "UI/Screens/SRbConfirmDialog.h"
#include "UI/Screens/SRbPauseMenu.h"
#include "UI/Screens/SRbSettingsMenu.h"
#include "UI/Screens/SRbTitleScreen.h"
#include "UI/Widgets/SRbMenuWidgets.h"

#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SlateRenderer.h"
#include "Scalability.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbUiTest
{
	// A recording host: every action the screens take lands in Calls; pushed dialogs are kept on a small stack.
	struct FHost
	{
		TSharedRef<FRbUiHost> Host = MakeShared<FRbUiHost>();
		TArray<FString> Calls;
		TArray<TSharedRef<SRbScreen>> Pushed;
		TStrongObjectPtr<URbGameUserSettings> Settings;
		const URbMatchDirector* Director = nullptr;
		bool bVenueAvailable = true;

		FHost()
		{
			Settings.Reset(NewObject<URbGameUserSettings>(GetTransientPackage()));
			Settings->SetToDefaults();
			Host->OpenScreen = [this](ERbUiScreen Screen) { Calls.Add(FString::Printf(TEXT("Open:%s"), *UEnum::GetValueAsString(Screen))); };
			Host->PushScreen = [this](TSharedRef<SRbScreen> Screen) { Calls.Add(TEXT("Push")); Pushed.Add(Screen); };
			Host->CloseTop = [this]() { Calls.Add(TEXT("CloseTop")); if (Pushed.Num() > 0) { Pushed.Pop(); } };
			Host->Resume = [this]() { Calls.Add(TEXT("Resume")); };
			Host->QuitToTitle = [this]() { Calls.Add(TEXT("QuitToTitle")); };
			Host->QuitToDesktop = [this]() { Calls.Add(TEXT("QuitToDesktop")); };
			Host->StartVenue = [this](ERbVenue Venue, ERbMatchMode Mode)
			{
				Calls.Add(FString::Printf(TEXT("StartVenue:%s:%s"), *UEnum::GetValueAsString(Venue), *UEnum::GetValueAsString(Mode)));
				return true;
			};
			Host->IsVenueAvailable = [this](ERbVenue Venue, FText& Reason)
			{
				if (!bVenueAvailable && Venue == ERbVenue::DiveBar)
				{
					Reason = FText::FromString(TEXT("Not built yet"));
					return false;
				}
				return true;
			};
			Host->CanQuitToTitle = [](FText&) { return true; };
			Host->GetSettings = [this]() { return Settings.Get(); };
			Host->SaveSettings = [this]() { Calls.Add(TEXT("Save")); };
			Host->GetDirector = [this]() { return Director; };
			Host->SetFocus = [](const TSharedPtr<SWidget>&) {};
			Host->bSkipIntro = true;
		}

		bool Called(const FString& Call) const { return Calls.Contains(Call); }
		void Clear() { Calls.Reset(); }
		TSharedPtr<SRbConfirmDialog> TopDialog() const
		{
			return Pushed.Num() > 0 && Pushed.Last()->IsDialog() ? StaticCastSharedRef<SRbConfirmDialog>(Pushed.Last()) : TSharedPtr<SRbConfirmDialog>();
		}
	};

	bool Key(SRbScreen& Screen, const FKey& K)
	{
		return Screen.HandleKey(K, FModifierKeysState());
	}

	// Restores the editor's scalability state and removes the RAW BREAK rows when a test ends (quality rows are live).
	struct FScalabilityGuard
	{
		Scalability::FQualityLevels Saved = Scalability::GetQualityLevels();
		~FScalabilityGuard()
		{
			URbGameUserSettings::RemoveQualityRows();
			Scalability::SetQualityLevels(Saved, true);
		}
	};

	// A pointer event that arrives with real cursor motion (Delta since the last event; the hand moved the mouse there).
	FPointerEvent Mouse(const FVector2D& Position, const FKey& Button = EKeys::LeftMouseButton, const FVector2D& Delta = FVector2D(3.0, 1.0))
	{
		TSet<FKey> Pressed;
		Pressed.Add(Button);
		return FPointerEvent(0, Position, Position - Delta, Pressed, Button, 0.0f, FModifierKeysState());
	}

	// Slate's synthetic move under a still cursor (a list scrolled, a screen opened under it): zero delta, no button held.
	FPointerEvent StillCursor(const FVector2D& Position)
	{
		return FPointerEvent(0, Position, Position, TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
	}

	// The mouse moving with no button held.
	FPointerEvent Hover(const FVector2D& Position)
	{
		return FPointerEvent(0, Position, Position - FVector2D(3.0, 1.0), TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
	}

	void Click(const TSharedPtr<SWidget>& Widget, const FVector2D& Size, const FVector2D& At)
	{
		const FGeometry Geometry = FGeometry::MakeRoot(Size, FSlateLayoutTransform());
		Widget->OnMouseEnter(Geometry, Mouse(At));
		Widget->OnMouseButtonDown(Geometry, Mouse(At));
		Widget->OnMouseButtonUp(Geometry, Mouse(At));
	}

	// UX-T07: walks the arranged tree; a single-line text block wider / taller than its box is clipped.
	void CollectClipping(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, TArray<FString>& Out, int32 Depth = 0)
	{
		if (Depth > 64)
		{
			return;
		}
		if (Widget->GetType() == TEXT("STextBlock") && Widget->GetTag() != RbUi::WrapTag)
		{
			const FVector2D Desired = Widget->GetDesiredSize();
			const FVector2D Local = Geometry.GetLocalSize();
			if (Desired.X > Local.X + 0.5 || Desired.Y > Local.Y + 0.5)
			{
				Out.Add(FString::Printf(TEXT("%s: desired %.1f x %.1f > allotted %.1f x %.1f"),
					*StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString(), Desired.X, Desired.Y, Local.X, Local.Y));
			}
		}
		FArrangedChildren Arranged(EVisibility::Visible);
		Widget->ArrangeChildren(Geometry, Arranged);
		for (int32 Index = 0; Index < Arranged.Num(); ++Index)
		{
			CollectClipping(Arranged[Index].Widget, Arranged[Index].Geometry, Out, Depth + 1);
		}
	}

	TArray<FString> Clipping(const TSharedRef<SWidget>& Root, const FVector2D& Size)
	{
		// Twice: wrapped text sizes itself from the first arrangement.
		TArray<FString> Out;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			Out.Reset();
			Root->SlatePrepass(1.0f);
			CollectClipping(Root, FGeometry::MakeRoot(Size, FSlateLayoutTransform()), Out);
		}
		return Out;
	}

	bool ContainsType(const TSharedRef<SWidget>& Widget, const FName& Type, int32 Depth = 0)
	{
		if (Widget->GetType() == Type)
		{
			return true;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Depth < 64 && Index < Children->Num(); ++Index)
		{
			if (ContainsType(Children->GetChildAt(Index), Type, Depth + 1))
			{
				return true;
			}
		}
		return false;
	}

	bool SlateRuns()
	{
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr;
	}

	TStrongObjectPtr<URbMatchDirector> MakeDirector(FAutomationTestBase& Test, ERbMatchMode Mode, int64 Seed, const FString& P1 = TEXT("Player 1"))
	{
		FString Error;
		const TSharedPtr<const FRbTableContext> Table = FRbTableContext::Create(FRbTableSetup{}, Error);
		if (!Table.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("table context: %s"), *Error));
			return nullptr;
		}
		TStrongObjectPtr<URbMatchDirector> Director(NewObject<URbMatchDirector>(GetTransientPackage()));
		Director->Initialize(nullptr, nullptr, nullptr, nullptr);
		Director->SetTableContext(Table);
		Director->SetLivePlaybackRate(0.0f);
		FRbMatchSetup Setup;
		Setup.Mode = Mode;
		Setup.Seed = Seed;
		Setup.Player1 = P1;
		Setup.NoiseScale = 0.0;
		if (!Director->StartMatch(Setup))
		{
			Test.AddError(FString::Printf(TEXT("StartMatch failed: %s"), *Director->GetLastError()));
			return nullptr;
		}
		return Director;
	}

	// UGameUserSettings::ApplyResolutionSettings / ApplyNonResolutionSettings validate first, and ValidateSettings reloads (and may
	// even delete) the user-settings ini when the object's version is not current. A transient test object that APPLIES a display
	// row must carry the current version. The member pointer taken through a using-declaration is the legal way to reach the
	// protected UpdateVersion (FVersionAccess is never instantiated).
	struct FVersionAccess : public URbGameUserSettings
	{
		using UGameUserSettings::UpdateVersion;
	};

	void MakeVersionCurrent(URbGameUserSettings& Settings)
	{
		void (UGameUserSettings::*Update)() = &FVersionAccess::UpdateVersion;
		(Settings.*Update)();
	}

	// A key press as Slate delivers it (bRepeat: the OS auto-repeat of a held key).
	bool Press(SRbScreen& Screen, const FKey& K, bool bRepeat)
	{
		return Screen.OnKeyDown(FGeometry(), FKeyEvent(K, FModifierKeysState(), static_cast<uint32>(0), bRepeat, 0u, 0u)).IsEventHandled();
	}

	// The logical layout sizes of the three test resolutions (the engine's DPI rule divides by height / 1080).
	TArray<TPair<FString, FVector2D>> LayoutSizes()
	{
		TArray<TPair<FString, FVector2D>> Sizes;
		for (const FIntPoint& Res : {FIntPoint(1280, 800), FIntPoint(1920, 1080), FIntPoint(3840, 2160)})
		{
			const float Scale = URbDpiScalingRule::ScaleForViewport(Res);
			Sizes.Add({FString::Printf(TEXT("%dx%d"), Res.X, Res.Y), FVector2D(Res.X / Scale, Res.Y / Scale)});
		}
		return Sizes;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiTokens, "RawBreak.Unit.UI.Tokens", RB_UNIT_TEST_FLAGS)
bool FRbUiTokens::RunTest(const FString& Parameters)
{
	using namespace RbUi;
	const EColor Surfaces[] = {EColor::Ink900, EColor::Ink800, EColor::Ink700};
	const EColor Texts[] = {EColor::Chalk100, EColor::Chalk300, EColor::Amber400, EColor::SignalFoulText, EColor::SignalOk, EColor::SignalInfo};
	for (const EColor Surface : Surfaces)
	{
		for (const EColor Text : Texts)
		{
			const double Ratio = ContrastRatio(Srgb(Text), Srgb(Surface));
			TestTrue(FString::Printf(TEXT("text token %d on surface %d: %.2f >= 4.5"), static_cast<int32>(Text), static_cast<int32>(Surface), Ratio), Ratio >= 4.5);
		}
	}
	// scrim.menu (0.96) over a white worst case: every text token except the non-text signal.foul.
	const FColor Menu = ScrimOverWhite(EColor::Ink900, ScrimMenu);
	for (const EColor Text : Texts)
	{
		const double Ratio = ContrastRatio(Srgb(Text), Menu);
		TestTrue(FString::Printf(TEXT("text token %d on scrim.menu over white: %.2f >= 4.5"), static_cast<int32>(Text), Ratio), Ratio >= 4.5);
	}
	TestTrue(TEXT("signal.foul is not a text colour on scrim.menu (non-text only)"), ContrastRatio(Srgb(EColor::SignalFoul), Menu) < 4.5);
	// scrim.card (0.88): chalk.100 only; amber focus bars >= 3:1 (non-text).
	const FColor Card = ScrimOverWhite(EColor::Ink900, ScrimCard);
	TestTrue(TEXT("chalk.100 on scrim.card over white >= 4.5"), ContrastRatio(Srgb(EColor::Chalk100), Card) >= 4.5);
	TestTrue(TEXT("amber focus bar on scrim.card over white >= 3 (non-text)"), ContrastRatio(Srgb(EColor::Amber400), Card) >= 3.0);
	TestTrue(TEXT("amber is not a text colour on scrim.card"), ContrastRatio(Srgb(EColor::Amber400), Card) < 4.5);
	// The disabled grey stays readable on the menu panel (exempt from 4.5, still >= 3).
	TestTrue(TEXT("chalk.500 on ink.900 >= 3"), ContrastRatio(Srgb(EColor::Chalk500), Srgb(EColor::Ink900)) >= 3.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiTextSize, "RawBreak.Unit.UI.TextSize", RB_UNIT_TEST_FLAGS)
bool FRbUiTextSize::RunTest(const FString& Parameters)
{
	using namespace RbUi;
	const bool bMeasure = RbUiTest::SlateRuns();
	if (!bMeasure)
	{
		AddWarning(TEXT("Slate has no renderer here: body heights from the Roboto metrics (ascender 0.928 + descender 0.244 em)"));
	}
	const float SteamDeck = URbDpiScalingRule::ScaleForViewport(FIntPoint(1280, 800));
	for (int32 Index = 0; Index < static_cast<int32>(EFont::Count); ++Index)
	{
		const EFont Token = static_cast<EFont>(Index);
		const FSlateFontInfo Info = Font(Token);
		const float Em = FontPx(Token);
		float Height = 0.0f;
		if (IsUppercase(Token))
		{
			Height = 0.711f * Em; // Roboto cap height 1456 / 2048 em: an uppercase line measures its cap height (ui-ux 4.2)
		}
		else if (bMeasure)
		{
			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			Height = static_cast<float>(Measure->GetMaxCharacterHeight(Info, 1.0f));
		}
		else
		{
			Height = 1.172f * Em;
		}
		AddInfo(FString::Printf(TEXT("%s: %.1f pt = %.1f px em, measured height %.1f px at 1080p"), FontName(Token), Info.Size, Em, Height));
		TestTrue(FString::Printf(TEXT("%s >= 18 px at 1080p (%.1f)"), FontName(Token), Height), Height >= 18.0f);
		TestTrue(FString::Printf(TEXT("%s >= 12 px at 1280x800 (%.1f)"), FontName(Token), Height * SteamDeck), Height * SteamDeck >= 12.0f);
	}
	TestTrue(TEXT("key caps 30 px high (ui-ux 3.5)"), FMath::IsNearlyEqual(GlyphHeight, 30.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiNoClipping, "RawBreak.Unit.UI.NoClipping", RB_UNIT_TEST_FLAGS)
bool FRbUiNoClipping::RunTest(const FString& Parameters)
{
	if (!RbUiTest::SlateRuns())
	{
		AddWarning(TEXT("no Slate renderer (font measuring): run with --render for UX-T07"));
		return true;
	}
	RbUiTest::FScalabilityGuard Guard;
	RbUiTest::FHost H;
	TStrongObjectPtr<URbMatchDirector> Director = RbUiTest::MakeDirector(*this, ERbMatchMode::HotSeat, 42, TEXT("Alexandra Kowalczyk"));
	H.Director = Director.Get();

	// Every screen / state (EN).
	TArray<TPair<FString, TSharedRef<SWidget>>> Screens;
	{
		TSharedRef<SRbTitleScreen> Title = SNew(SRbTitleScreen).Host(H.Host);
		Screens.Add({TEXT("Title.Root"), Title});
		TSharedRef<SRbTitleScreen> Play = SNew(SRbTitleScreen).Host(H.Host);
		RbUiTest::Key(*Play, EKeys::Enter);
		Screens.Add({TEXT("Title.Play"), Play});
		TSharedRef<SRbTitleScreen> Venue = SNew(SRbTitleScreen).Host(H.Host);
		RbUiTest::Key(*Venue, EKeys::Enter);
		RbUiTest::Key(*Venue, EKeys::Enter);
		Screens.Add({TEXT("Title.Venue"), Venue});
		H.bVenueAvailable = false;
		TSharedRef<SRbTitleScreen> Missing = SNew(SRbTitleScreen).Host(H.Host);
		RbUiTest::Key(*Missing, EKeys::Enter);
		Screens.Add({TEXT("Title.Play (venue missing)"), Missing});
		H.bVenueAvailable = true;
	}
	Screens.Add({TEXT("Pause (hot-seat match)"), SNew(SRbPauseMenu).Host(H.Host)});
	for (int32 Page = 0; Page < static_cast<int32>(ERbSettingsPage::Count); ++Page)
	{
		Screens.Add({FString::Printf(TEXT("Settings.%s"), *FRbSettingsRegistry::PageName(static_cast<ERbSettingsPage>(Page)).ToString()),
			SNew(SRbSettingsMenu).Host(H.Host).InitialPage(static_cast<ERbSettingsPage>(Page))});
	}
	H.Settings->SelectQualityPreset(ERbQualityPreset::Cinematic);
	H.Settings->SetQualityOption(ERbQualityOption::Reflections, 1); // the longest preset label: "Custom (based on Cinematic)"
	H.Settings->CameraPreset = ERbCameraPreset::Headcam;
	Screens.Add({TEXT("Settings.Graphics (Custom)"), SNew(SRbSettingsMenu).Host(H.Host)});
	Screens.Add({TEXT("Dialog (display revert)"), SNew(SRbConfirmDialog).Host(H.Host).Title(FText::FromString(TEXT("Keep these display settings?")))
		.ConfirmLabel(FText::FromString(TEXT("Keep"))).CancelLabel(FText::FromString(TEXT("Revert"))).TimeoutSeconds(15.0f).bFocusConfirm(true)});
	Screens.Add({TEXT("Dialog (quit)"), SNew(SRbConfirmDialog).Host(H.Host).Title(FText::FromString(TEXT("Quit to the title?")))
		.Message(FText::FromString(TEXT("The match will be lost."))).ConfirmLabel(FText::FromString(TEXT("Quit to title"))).CancelLabel(FText::FromString(TEXT("Cancel")))});
	{
		TSharedRef<SRbKeyHints> Hints = SNew(SRbKeyHints);
		FRbKeyHintContext Down;
		Down.bHasStroke = true;
		Down.StrokePhase = ERbStrokePhase::Down;
		Hints->SetModel(FRbKeyHintsModel::BuildFromContext(Down));
		Screens.Add({TEXT("KeyHints (down)"), Hints});
	}

	for (const TPair<FString, FVector2D>& Size : RbUiTest::LayoutSizes())
	{
		for (const TPair<FString, TSharedRef<SWidget>>& Screen : Screens)
		{
			const TArray<FString> Problems = RbUiTest::Clipping(Screen.Value, Size.Value);
			for (const FString& Problem : Problems)
			{
				AddError(FString::Printf(TEXT("%s at %s (%.0f x %.0f units): %s"), *Screen.Key, *Size.Key, Size.Value.X, Size.Value.Y, *Problem));
			}
			const FVector2D Desired = Screen.Value->GetDesiredSize();
			TestTrue(FString::Printf(TEXT("%s fits %s (desired %.0f x %.0f)"), *Screen.Key, *Size.Key, Desired.X, Desired.Y),
				Desired.X <= Size.Value.X + 0.5 && Desired.Y <= Size.Value.Y + 0.5);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiDpiRule, "RawBreak.Unit.UI.DpiRule", RB_UNIT_TEST_FLAGS)
bool FRbUiDpiRule::RunTest(const FString& Parameters)
{
	const UUserInterfaceSettings* Ui = GetDefault<UUserInterfaceSettings>();
	for (const FIntPoint& Res : {FIntPoint(1280, 800), FIntPoint(1920, 1080), FIntPoint(3840, 2160), FIntPoint(2560, 1080)})
	{
		const float Expected = Res.Y / 1080.0f;
		TestNearlyEqual(FString::Printf(TEXT("rule %dx%d = height / 1080"), Res.X, Res.Y), URbDpiScalingRule::ScaleForViewport(Res), Expected, 1e-6f);
		TestNearlyEqual(FString::Printf(TEXT("engine UI scale %dx%d (custom rule registered)"), Res.X, Res.Y), Ui->GetDPIScaleBasedOnSize(Res), Expected, 1e-4f);
		// A 100-unit widget under the engine's scale measures 100 * height / 1080 px (the only scale, UX-T26).
		const FGeometry Root = FGeometry::MakeRoot(FVector2D(Res.X / Expected, Res.Y / Expected), FSlateLayoutTransform(Expected));
		const FGeometry Child = Root.MakeChild(FVector2D(100.0, 40.0), FSlateLayoutTransform());
		TestNearlyEqual(TEXT("100-unit widget in pixels"), static_cast<float>(Child.GetAbsoluteSize().X), 100.0f * Expected, 1e-3f);
	}
	TestNearlyEqual(TEXT("degenerate size"), URbDpiScalingRule::ScaleForViewport(FIntPoint(0, 0)), 1.0f, 1e-6f);

	// No screen scales itself (no SDPIScaler / SScaleBox inside), and Tab never moves the focus.
	RbUiTest::FHost H;
	TArray<TSharedRef<SRbScreen>> Screens = {SNew(SRbTitleScreen).Host(H.Host), SNew(SRbPauseMenu).Host(H.Host), SNew(SRbSettingsMenu).Host(H.Host),
		SNew(SRbConfirmDialog).Host(H.Host).Title(FText::FromString(TEXT("?"))).ConfirmLabel(FText::FromString(TEXT("Yes"))).CancelLabel(FText::FromString(TEXT("No")))};
	for (const TSharedRef<SRbScreen>& Screen : Screens)
	{
		const FString Name = UEnum::GetValueAsString(Screen->GetScreenId());
		TestFalse(FString::Printf(TEXT("%s has no own DPI scaler"), *Name), RbUiTest::ContainsType(Screen, TEXT("SDPIScaler")) ||
			RbUiTest::ContainsType(Screen, TEXT("SScaleBox")));
		const int32 Before = Screen->GetFocusIndex();
		TestTrue(FString::Printf(TEXT("%s swallows Tab"), *Name), RbUiTest::Key(*Screen, EKeys::Tab));
		TestEqual(FString::Printf(TEXT("%s: Tab keeps the focus"), *Name), Screen->GetFocusIndex(), Before);
		TestTrue(FString::Printf(TEXT("%s: Slate navigation is the screen's own"), *Name),
			Screen->OnNavigation(FGeometry(), FNavigationEvent(FModifierKeysState(), 0, EUINavigation::Next, ENavigationGenesis::Keyboard)).GetBoundaryRule() == EUINavigationRule::Stop);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiNavigationKeys, "RawBreak.Unit.UI.NavigationKeys", RB_UNIT_TEST_FLAGS)
bool FRbUiNavigationKeys::RunTest(const FString& Parameters)
{
	using RbUiTest::Key;
	RbUiTest::FScalabilityGuard Guard;
	RbUiTest::FHost H;
	const auto OneFocus = [this](const SRbScreen& Screen, const TCHAR* What)
	{
		TestTrue(FString::Printf(TEXT("%s: exactly one focused item"), What),
			Screen.GetNumFocusItems() > 0 && Screen.GetFocusIndex() >= 0 && Screen.GetFocusIndex() < Screen.GetNumFocusItems() && Screen.GetFocusedWidget().IsValid());
	};

	// --- title ------------------------------------------------------------------------------------------------------------
	TSharedRef<SRbTitleScreen> Title = SNew(SRbTitleScreen).Host(H.Host);
	TestTrue(TEXT("root: Play, Settings, Quit"), Title->GetItemLabels() == TArray<FString>({TEXT("Play"), TEXT("Settings"), TEXT("Quit")}));
	OneFocus(*Title, TEXT("title root"));
	TestEqual(TEXT("Play focused first"), Title->GetFocusIndex(), 0);
	Key(*Title, EKeys::Down);
	Key(*Title, EKeys::Enter);
	TestTrue(TEXT("Settings opens the settings screen"), H.Called(TEXT("Open:ERbUiScreen::Settings")));
	Key(*Title, EKeys::Down);
	Key(*Title, EKeys::Down);
	TestEqual(TEXT("focus stops at the last item (no wrap)"), Title->GetFocusIndex(), 2);
	H.Clear();
	Key(*Title, EKeys::Enter);
	TestTrue(TEXT("Quit asks first (a dialog, no quit)"), H.Called(TEXT("Push")) && !H.Called(TEXT("QuitToDesktop")));
	TSharedPtr<SRbConfirmDialog> Quit = H.TopDialog();
	if (TestTrue(TEXT("quit dialog"), Quit.IsValid()))
	{
		OneFocus(*Quit, TEXT("quit dialog"));
		TestEqual(TEXT("the safe answer (Cancel) is focused"), Quit->GetFocusIndex(), 1);
		Key(*Quit, EKeys::Enter);
		TestTrue(TEXT("Cancel closes the dialog, no quit"), H.Called(TEXT("CloseTop")) && !H.Called(TEXT("QuitToDesktop")));
	}
	H.Clear();
	Key(*Title, EKeys::Escape);
	TSharedPtr<SRbConfirmDialog> EscQuit = H.TopDialog();
	TestTrue(TEXT("Esc on the root asks to quit"), EscQuit.IsValid() && !H.Called(TEXT("QuitToDesktop")));
	if (EscQuit.IsValid())
	{
		Key(*EscQuit, EKeys::Escape);
		TestTrue(TEXT("Esc in the dialog = cancel"), !H.Called(TEXT("QuitToDesktop")) && EscQuit->IsAnswered());
		H.Clear();
		// Confirming quits.
		Key(*Title, EKeys::Enter);
		TSharedPtr<SRbConfirmDialog> Confirm = H.TopDialog();
		if (Confirm.IsValid())
		{
			Key(*Confirm, EKeys::Up);
			Key(*Confirm, EKeys::Enter);
			TestTrue(TEXT("Quit to desktop after the confirm"), H.Called(TEXT("QuitToDesktop")));
		}
	}
	// Play -> venue -> mode.
	H.Clear();
	Key(*Title, EKeys::Up);
	Key(*Title, EKeys::Up);
	Key(*Title, EKeys::Enter);
	TestTrue(TEXT("Play: the venues + Back"), Title->GetLevel() == SRbTitleScreen::ELevel::Play &&
		Title->GetItemLabels() == TArray<FString>({TEXT("The Low Bridge Tavern"), TEXT("Test room"), TEXT("Back")}));
	OneFocus(*Title, TEXT("title play"));
	Key(*Title, EKeys::Escape);
	TestTrue(TEXT("Back returns to the root"), Title->GetLevel() == SRbTitleScreen::ELevel::Root && Title->GetFocusIndex() == 0);
	Key(*Title, EKeys::Enter);
	Key(*Title, EKeys::Down);
	Key(*Title, EKeys::Enter);
	TestTrue(TEXT("Test room: Practice, Hot-seat, Back"), Title->GetLevel() == SRbTitleScreen::ELevel::Venue &&
		Title->GetSelectedVenue() == ERbVenue::TestRoom && Title->GetItemLabels() == TArray<FString>({TEXT("Practice"), TEXT("Hot-seat"), TEXT("Back")}));
	Key(*Title, EKeys::Escape);
	TestTrue(TEXT("Back from the modes returns to the venue list on the same venue"), Title->GetLevel() == SRbTitleScreen::ELevel::Play && Title->GetFocusIndex() == 1);
	Key(*Title, EKeys::Up);
	Key(*Title, EKeys::Enter);
	Key(*Title, EKeys::Down);
	Key(*Title, EKeys::SpaceBar);
	TestTrue(TEXT("Dive bar Hot-seat travels"), H.Called(TEXT("StartVenue:ERbVenue::DiveBar:ERbMatchMode::HotSeat")) && Title->IsLoading());
	TestTrue(TEXT("no input while loading"), RbUiTest::Key(*Title, EKeys::Escape) && Title->IsLoading());

	// A missing venue is disabled with its reason and cannot be chosen.
	H.Clear();
	H.bVenueAvailable = false;
	TSharedRef<SRbTitleScreen> Missing = SNew(SRbTitleScreen).Host(H.Host);
	Key(*Missing, EKeys::Enter);
	TestEqual(TEXT("the focus starts on the first AVAILABLE venue"), Missing->GetFocusIndex(), 1);
	Key(*Missing, EKeys::Up);
	Key(*Missing, EKeys::Enter);
	TestTrue(TEXT("a disabled venue does nothing"), Missing->GetLevel() == SRbTitleScreen::ELevel::Play && H.Calls.Num() == 0);
	H.bVenueAvailable = true;

	// Clean first seconds: the first input only reveals the list.
	RbUiTest::FHost Intro;
	Intro.Host->bSkipIntro = false;
	TSharedRef<SRbTitleScreen> Fresh = SNew(SRbTitleScreen).Host(Intro.Host);
	TestFalse(TEXT("intro: list hidden at start"), Fresh->IsIntroDone());
	Key(*Fresh, EKeys::Enter);
	TestTrue(TEXT("any key reveals the list and chooses nothing"), Fresh->IsIntroDone() && Fresh->GetLevel() == SRbTitleScreen::ELevel::Root);

	// --- pause ------------------------------------------------------------------------------------------------------------
	H.Clear();
	TSharedRef<SRbPauseMenu> Pause = SNew(SRbPauseMenu).Host(H.Host);
	TestTrue(TEXT("pause items"), Pause->GetItemLabels() == TArray<FString>({TEXT("Resume"), TEXT("Settings"), TEXT("Quit to title"), TEXT("Quit to desktop")}));
	TestTrue(TEXT("pause pauses the game"), Pause->PausesGame());
	OneFocus(*Pause, TEXT("pause"));
	Key(*Pause, EKeys::Escape);
	TestTrue(TEXT("Esc = Resume"), H.Called(TEXT("Resume")));
	H.Clear();
	Key(*Pause, EKeys::Enter);
	TestTrue(TEXT("Enter on Resume"), H.Called(TEXT("Resume")));
	Key(*Pause, EKeys::Down);
	Key(*Pause, EKeys::Enter);
	TestTrue(TEXT("Settings from the pause menu"), H.Called(TEXT("Open:ERbUiScreen::Settings")));
	for (const TPair<int32, FString>& Quit_ : {TPair<int32, FString>(2, TEXT("QuitToTitle")), TPair<int32, FString>(3, TEXT("QuitToDesktop"))})
	{
		H.Clear();
		Pause->SetFocusIndex(Quit_.Key);
		Key(*Pause, EKeys::Enter);
		TSharedPtr<SRbConfirmDialog> Dialog = H.TopDialog();
		TestTrue(FString::Printf(TEXT("%s asks first"), *Quit_.Value), Dialog.IsValid() && !H.Called(Quit_.Value));
		if (Dialog.IsValid())
		{
			Dialog->SetFocusIndex(0);
			Key(*Dialog, EKeys::Enter);
			TestTrue(FString::Printf(TEXT("%s after the confirm"), *Quit_.Value), H.Called(Quit_.Value));
		}
	}

	// --- settings ---------------------------------------------------------------------------------------------------------
	H.Clear();
	TSharedRef<SRbSettingsMenu> Settings = SNew(SRbSettingsMenu).Host(H.Host);
	OneFocus(*Settings, TEXT("settings"));
	TestTrue(TEXT("opens on Graphics, preset row first"), Settings->GetPage() == ERbSettingsPage::Graphics && Settings->GetFocusedRow() &&
		Settings->GetFocusedRow()->Id == TEXT("gfx.preset"));
	Key(*Settings, EKeys::Right);
	TestTrue(TEXT("Right: High -> Epic"), H.Settings->GetQualityPreset() == ERbQualityPreset::Epic);
	Key(*Settings, EKeys::Left);
	Key(*Settings, EKeys::Down);
	const FRbSettingDef* Gi = Settings->GetFocusedRow();
	TestTrue(TEXT("Down: the next row"), Gi && Gi->Id == TEXT("gfx.globalIllumination"));
	Key(*Settings, EKeys::Left);
	TestTrue(TEXT("a quality row -> Custom (based on High)"), H.Settings->GetQualityPreset() == ERbQualityPreset::Custom &&
		FRbSettingsRegistry::PresetLabel(*H.Settings).ToString() == TEXT("Custom (based on High)"));
	Key(*Settings, EKeys::E);
	TestTrue(TEXT("E: next page (Display)"), Settings->GetPage() == ERbSettingsPage::Display);
	OneFocus(*Settings, TEXT("settings display"));
	Key(*Settings, EKeys::E);
	TestTrue(TEXT("E: Camera"), Settings->GetPage() == ERbSettingsPage::Camera);
	Key(*Settings, EKeys::Down); // FOV
	const float Fov = H.Settings->VerticalFovDeg;
	Key(*Settings, EKeys::Right);
	TestNearlyEqual(TEXT("Right: FOV + 1 deg"), H.Settings->VerticalFovDeg, Fov + 1.0f, 1e-4f);
	Settings->SetFocusIndex(6); // Reduced motion
	Key(*Settings, EKeys::Enter);
	TestTrue(TEXT("Enter toggles Reduced motion"), H.Settings->bReducedMotion && FMath::IsNearlyZero(H.Settings->HeadBobScale));
	Key(*Settings, EKeys::Q);
	Key(*Settings, EKeys::Q);
	Key(*Settings, EKeys::Q);
	TestTrue(TEXT("Q three times: Audio (wraps)"), Settings->GetPage() == ERbSettingsPage::Audio);
	Key(*Settings, EKeys::R);
	TSharedPtr<SRbConfirmDialog> Reset = H.TopDialog();
	TestTrue(TEXT("R asks before resetting the page"), Reset.IsValid());
	if (Reset.IsValid())
	{
		H.Settings->Volumes.Music = 0.2f;
		Reset->SetFocusIndex(0);
		Key(*Reset, EKeys::Enter);
		TestNearlyEqual(TEXT("reset page restores the music default"), H.Settings->Volumes.Music, 0.7f, 1e-6f);
	}
	TestTrue(TEXT("hold Space previews"), Key(*Settings, EKeys::SpaceBar) && Settings->IsPreviewing());
	Settings->HandleKeyUp(EKeys::SpaceBar);
	TestFalse(TEXT("release ends the preview"), Settings->IsPreviewing());
	H.Clear();
	Key(*Settings, EKeys::Escape);
	TestTrue(TEXT("Back saves and closes"), H.Called(TEXT("Save")) && H.Called(TEXT("CloseTop")));

	// --- dialog timeout ----------------------------------------------------------------------------------------------------
	bool bCancelled = false;
	TSharedRef<SRbConfirmDialog> Revert = SNew(SRbConfirmDialog).Host(H.Host).Title(FText::FromString(TEXT("Keep?"))).ConfirmLabel(FText::FromString(TEXT("Keep")))
		.CancelLabel(FText::FromString(TEXT("Revert"))).TimeoutSeconds(15.0f).bFocusConfirm(true).OnCancel_Lambda([&bCancelled]() { bCancelled = true; });
	TestEqual(TEXT("keep is focused on the display dialog"), Revert->GetFocusIndex(), 0);
	for (int32 Frame = 0; Frame < 14 * 10; ++Frame)
	{
		Revert->Tick(FGeometry(), 0.0, 0.1f);
	}
	TestFalse(TEXT("no revert before 15 s"), bCancelled);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Revert->Tick(FGeometry(), 0.0, 0.1f);
	}
	TestTrue(TEXT("15 s without an answer reverts"), bCancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiNavigationMouse, "RawBreak.Unit.UI.NavigationMouse", RB_UNIT_TEST_FLAGS)
bool FRbUiNavigationMouse::RunTest(const FString& Parameters)
{
	using RbUiTest::Click;
	RbUiTest::FScalabilityGuard Guard;
	RbUiTest::FHost H;
	const FVector2D ItemSize(560.0, 64.0);

	// Title: click Play, click the Test room, click Practice.
	TSharedRef<SRbTitleScreen> Title = SNew(SRbTitleScreen).Host(H.Host);
	TSharedPtr<SWidget> Settings = Title->GetFocusItemWidget(1);
	Settings->OnMouseEnter(FGeometry::MakeRoot(ItemSize, FSlateLayoutTransform()), RbUiTest::Mouse(FVector2D(40.0, 30.0)));
	TestEqual(TEXT("hover moves the focus"), Title->GetFocusIndex(), 1);
	Click(Title->GetFocusItemWidget(0), ItemSize, FVector2D(40.0, 30.0));
	TestTrue(TEXT("click Play"), Title->GetLevel() == SRbTitleScreen::ELevel::Play);
	Click(Title->GetFocusItemWidget(1), ItemSize, FVector2D(40.0, 30.0));
	Click(Title->GetFocusItemWidget(0), ItemSize, FVector2D(40.0, 30.0));
	TestTrue(TEXT("click Test room -> Practice"), H.Called(TEXT("StartVenue:ERbVenue::TestRoom:ERbMatchMode::Practice")));
	TSharedRef<SRbTitleScreen> Back = SNew(SRbTitleScreen).Host(H.Host);
	Click(Back->GetFocusItemWidget(0), ItemSize, FVector2D(40.0, 30.0));
	Click(Back->GetFocusItemWidget(2), ItemSize, FVector2D(40.0, 30.0));
	TestTrue(TEXT("click Back"), Back->GetLevel() == SRbTitleScreen::ELevel::Root);
	H.Clear();
	Click(Back->GetFocusItemWidget(2), ItemSize, FVector2D(40.0, 30.0));
	TSharedPtr<SRbConfirmDialog> Quit = H.TopDialog();
	TestTrue(TEXT("click Quit asks first"), Quit.IsValid() && !H.Called(TEXT("QuitToDesktop")));
	if (Quit.IsValid())
	{
		Click(Quit->GetFocusItemWidget(0), ItemSize, FVector2D(40.0, 30.0));
		TestTrue(TEXT("click the confirm quits"), H.Called(TEXT("QuitToDesktop")));
	}
	// A right click does nothing.
	H.Clear();
	TSharedRef<SRbTitleScreen> Right = SNew(SRbTitleScreen).Host(H.Host);
	const FGeometry ItemGeometry = FGeometry::MakeRoot(ItemSize, FSlateLayoutTransform());
	Right->GetFocusItemWidget(0)->OnMouseButtonDown(ItemGeometry, RbUiTest::Mouse(FVector2D(40.0, 30.0), EKeys::RightMouseButton));
	Right->GetFocusItemWidget(0)->OnMouseButtonUp(ItemGeometry, RbUiTest::Mouse(FVector2D(40.0, 30.0), EKeys::RightMouseButton));
	TestTrue(TEXT("right click: nothing"), Right->GetLevel() == SRbTitleScreen::ELevel::Root);

	// Pause: click Resume.
	H.Clear();
	TSharedRef<SRbPauseMenu> Pause = SNew(SRbPauseMenu).Host(H.Host);
	Click(Pause->GetFocusItemWidget(0), ItemSize, FVector2D(40.0, 30.0));
	TestTrue(TEXT("click Resume"), H.Called(TEXT("Resume")));

	// Settings rows: arrows, value, slider, tabs, footer.
	TSharedRef<SRbSettingsMenu> Menu = SNew(SRbSettingsMenu).Host(H.Host).InitialPage(ERbSettingsPage::Camera);
	const FVector2D RowSize(1100.0, RbUi::RowHeight);
	const float ControlStart = RowSize.X - SRbOptionRow::ControlWidth + RbUi::FocusBarWidth;
	// cam.look (enum): click the right arrow -> Headcam; the left arrow -> Eyes; the value -> next.
	TSharedPtr<SRbOptionRow> Look = Menu->GetRowWidget(0);
	const float Inner = SRbOptionRow::ControlWidth - RbUi::FocusBarWidth - 12.0f;
	Click(Look, RowSize, FVector2D(ControlStart + Inner - 10.0f, 28.0));
	TestTrue(TEXT("click > : Headcam"), H.Settings->CameraPreset == ERbCameraPreset::Headcam);
	Click(Look, RowSize, FVector2D(ControlStart + 10.0f, 28.0));
	TestTrue(TEXT("click < : Eyes"), H.Settings->CameraPreset == ERbCameraPreset::Eyes);
	Click(Look, RowSize, FVector2D(ControlStart + 0.5f * Inner, 28.0));
	TestTrue(TEXT("click the value: next"), H.Settings->CameraPreset == ERbCameraPreset::Headcam);
	TestEqual(TEXT("the clicked row has the focus"), Menu->GetFocusIndex(), 0);
	Click(Look, RowSize, FVector2D(ControlStart + Inner - 10.0f, 28.0));
	TestTrue(TEXT("click > on the last value: stays (the arrows clamp)"), H.Settings->CameraPreset == ERbCameraPreset::Headcam);
	Click(Look, RowSize, FVector2D(ControlStart + 0.5f * Inner, 28.0));
	TestTrue(TEXT("click the last value: wraps to the first, like Enter (never a dead click)"), H.Settings->CameraPreset == ERbCameraPreset::Eyes);
	Click(Look, RowSize, FVector2D(ControlStart + 0.5f * Inner, 28.0));
	TestTrue(TEXT("click the value again: Headcam"), H.Settings->CameraPreset == ERbCameraPreset::Headcam);
	// cam.fov (float): click at 60 % of the track -> 40 + 0.6 * 35 = 61 deg; drag to the end -> 75.
	TSharedPtr<SRbOptionRow> Fov = Menu->GetRowWidget(1);
	const FGeometry RowGeometry = FGeometry::MakeRoot(RowSize, FSlateLayoutTransform());
	const FVector2D At60(ControlStart + 0.6f * SRbOptionRow::TrackWidth, 28.0);
	Fov->OnMouseEnter(RowGeometry, RbUiTest::Mouse(At60));
	Fov->OnMouseButtonDown(RowGeometry, RbUiTest::Mouse(At60));
	TestNearlyEqual(TEXT("slider click = that fraction (snapped)"), H.Settings->VerticalFovDeg, 61.0f, 1e-4f);
	Fov->OnMouseMove(RowGeometry, RbUiTest::Mouse(FVector2D(ControlStart + SRbOptionRow::TrackWidth + 50.0f, 28.0)));
	TestNearlyEqual(TEXT("drag past the end = max"), H.Settings->VerticalFovDeg, 75.0f, 1e-4f);
	Fov->OnMouseButtonUp(RowGeometry, RbUiTest::Mouse(FVector2D(ControlStart + SRbOptionRow::TrackWidth + 50.0f, 28.0)));
	Fov->OnMouseMove(RowGeometry, RbUiTest::Mouse(FVector2D(ControlStart, 28.0)));
	TestNearlyEqual(TEXT("no drag after the release"), H.Settings->VerticalFovDeg, 75.0f, 1e-4f);
	TestEqual(TEXT("zone of the label"), static_cast<int32>(Fov->ZoneAt(100.0f, RowSize.X)), static_cast<int32>(SRbOptionRow::EZone::Label));
	// Mount shake is Headcam only: a click on an unavailable row changes nothing.
	H.Settings->CameraPreset = ERbCameraPreset::Eyes;
	TSharedPtr<SRbOptionRow> Shake = Menu->GetRowWidget(4);
	const float Before = H.Settings->Camera.MountShakeScale;
	Click(Shake, RowSize, FVector2D(ControlStart + 0.2f * SRbOptionRow::TrackWidth, 28.0));
	TestTrue(TEXT("mount shake row is the Headcam-only row"), Shake->GetRow()->Id == TEXT("cam.mountShake"));
	TestNearlyEqual(TEXT("unavailable row unchanged"), H.Settings->Camera.MountShakeScale, Before, 1e-6f);
	// Tabs.
	Menu->GetTabWidget(ERbSettingsPage::Audio)->OnMouseButtonDown(FGeometry::MakeRoot(FVector2D(120.0, 40.0), FSlateLayoutTransform()),
		RbUiTest::Mouse(FVector2D(10.0, 10.0)));
	TestTrue(TEXT("click a tab"), Menu->GetPage() == ERbSettingsPage::Audio);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiKeyRepeat, "RawBreak.Unit.UI.KeyRepeat", RB_UNIT_TEST_FLAGS)
bool FRbUiKeyRepeat::RunTest(const FString& Parameters)
{
	using RbUiTest::Press;
	RbUiTest::FHost H;
	TestTrue(TEXT("arrows repeat"), SRbScreen::IsRepeatableKey(EKeys::Down) && SRbScreen::IsRepeatableKey(EKeys::Right) &&
		SRbScreen::IsRepeatableKey(EKeys::Gamepad_DPad_Up));
	TestFalse(TEXT("Enter / Space / Esc do not repeat"), SRbScreen::IsRepeatableKey(EKeys::Enter) || SRbScreen::IsRepeatableKey(EKeys::SpaceBar) ||
		SRbScreen::IsRepeatableKey(EKeys::Escape));

	// Title: a held Enter opens Play once and then stays there (no venue, no mode, no travel).
	TSharedRef<SRbTitleScreen> Title = SNew(SRbTitleScreen).Host(H.Host);
	TestTrue(TEXT("Enter handled"), Press(*Title, EKeys::Enter, false));
	TestTrue(TEXT("Enter: Play"), Title->GetLevel() == SRbTitleScreen::ELevel::Play);
	const int32 Venue = Title->GetFocusIndex();
	for (int32 Repeat = 0; Repeat < 20; ++Repeat)
	{
		TestTrue(TEXT("a repeated Enter is consumed"), Press(*Title, EKeys::Enter, true));
	}
	TestTrue(TEXT("holding Enter does not chain into a venue or a travel"), Title->GetLevel() == SRbTitleScreen::ELevel::Play &&
		Title->GetFocusIndex() == Venue && !Title->IsLoading() && !H.Called(TEXT("StartVenue:ERbVenue::DiveBar:ERbMatchMode::Practice")));
	// A held Down walks the list.
	Title->SetFocusIndex(0);
	Press(*Title, EKeys::Down, false);
	Press(*Title, EKeys::Down, true);
	TestEqual(TEXT("a held Down keeps moving the focus"), Title->GetFocusIndex(), 2);
	// Esc repeats do not step back through the levels either.
	Press(*Title, EKeys::Escape, false);
	TestTrue(TEXT("Esc: back to the root"), Title->GetLevel() == SRbTitleScreen::ELevel::Root);
	H.Clear();
	for (int32 Repeat = 0; Repeat < 10; ++Repeat)
	{
		Press(*Title, EKeys::Escape, true);
	}
	TestTrue(TEXT("a held Esc does not open the quit dialog"), !H.Called(TEXT("Push")) && Title->GetLevel() == SRbTitleScreen::ELevel::Root);

	// Pause: the Esc that opened the menu reaches it as repeats while it is held - they must not resume.
	TSharedRef<SRbPauseMenu> Pause = SNew(SRbPauseMenu).Host(H.Host);
	H.Clear();
	for (int32 Repeat = 0; Repeat < 10; ++Repeat)
	{
		Press(*Pause, EKeys::Escape, true);
	}
	TestFalse(TEXT("a held Esc does not resume"), H.Called(TEXT("Resume")));
	Press(*Pause, EKeys::Escape, false);
	TestTrue(TEXT("a new Esc press resumes"), H.Called(TEXT("Resume")));

	// Settings: a held Right keeps changing the focused row.
	TSharedRef<SRbSettingsMenu> Settings = SNew(SRbSettingsMenu).Host(H.Host).InitialPage(ERbSettingsPage::Camera);
	Settings->SetFocusIndex(1);
	TestTrue(TEXT("the FOV row"), Settings->GetFocusedRow() && Settings->GetFocusedRow()->Id == TEXT("cam.fov"));
	const float Fov = H.Settings->VerticalFovDeg;
	Press(*Settings, EKeys::Right, false);
	Press(*Settings, EKeys::Right, true);
	Press(*Settings, EKeys::Right, true);
	TestNearlyEqual(TEXT("a held Right: +1 deg per repeat"), H.Settings->VerticalFovDeg, Fov + 3.0f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiDisplayRevert, "RawBreak.Unit.UI.DisplayRevert", RB_UNIT_TEST_FLAGS)
bool FRbUiDisplayRevert::RunTest(const FString& Parameters)
{
	RbUiTest::FHost H;
	URbGameUserSettings* S = H.Settings.Get();
	RbUiTest::MakeVersionCurrent(*S); // the display rows apply (ApplyResolutionSettings validates the object first)
	// The applies write the engine's resolution request variables (the editor does not act on them); put them back afterwards.
	TArray<TPair<IConsoleVariable*, FString>> SavedCVars;
	for (const TCHAR* Name : {TEXT("r.SetRes"), TEXT("r.FullScreenMode")})
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			SavedCVars.Add({CVar, CVar->GetString()});
		}
	}
	ON_SCOPE_EXIT
	{
		for (const TPair<IConsoleVariable*, FString>& Entry : SavedCVars)
		{
			Entry.Key->SetWithCurrentPriority(*Entry.Value);
		}
	};

	// A window size that is no display mode: the resolution list holds it only while it is the current resolution.
	const FIntPoint Custom(1703, 958);
	S->SetFullscreenMode(EWindowMode::Windowed);
	S->SetScreenResolution(Custom);
	TestTrue(TEXT("the current custom size is listed"), FRbSettingsRegistry::ResolutionList(*S).Contains(Custom));
	TSharedRef<SRbSettingsMenu> Menu = SNew(SRbSettingsMenu).Host(H.Host).InitialPage(ERbSettingsPage::Display);
	Menu->SetFocusIndex(1);
	if (!TestTrue(TEXT("the resolution row (available in windowed mode)"), Menu->GetFocusedRow() && Menu->GetFocusedRow()->Id == TEXT("dsp.resolution") &&
		FRbSettingsRegistry::IsAvailable(*Menu->GetFocusedRow(), *S)))
	{
		return false;
	}
	RbUiTest::Key(*Menu, EKeys::Right);
	const FIntPoint Next = S->GetScreenResolution();
	AddInfo(FString::Printf(TEXT("Right: %d x %d -> %d x %d"), Custom.X, Custom.Y, Next.X, Next.Y));
	TestTrue(TEXT("Right: the next larger listed resolution"), Next != Custom && (Next.X > Custom.X || (Next.X == Custom.X && Next.Y > Custom.Y)));
	TestFalse(TEXT("the custom size left the list (the list indices moved)"), FRbSettingsRegistry::ResolutionList(*S).Contains(Custom));
	TSharedPtr<SRbConfirmDialog> Keep = H.TopDialog();
	if (TestTrue(TEXT("keep-or-revert dialog"), Keep.IsValid()))
	{
		TestEqual(TEXT("Keep is focused"), Keep->GetFocusIndex(), 0);
		RbUiTest::Key(*Keep, EKeys::Escape);
		TestTrue(FString::Printf(TEXT("Esc reverts to the custom size itself (got %d x %d)"), S->GetScreenResolution().X, S->GetScreenResolution().Y),
			S->GetScreenResolution() == Custom && S->GetFullscreenMode() == EWindowMode::Windowed);
	}

	// Window mode: Windowed -> Windowed fullscreen, no answer for 15 s -> back to windowed at the custom size.
	Menu->SetFocusIndex(0);
	RbUiTest::Key(*Menu, EKeys::Left);
	TestTrue(TEXT("Left: windowed fullscreen"), S->GetFullscreenMode() == EWindowMode::WindowedFullscreen);
	TSharedPtr<SRbConfirmDialog> Timeout = H.TopDialog();
	if (TestTrue(TEXT("keep-or-revert dialog (window mode)"), Timeout.IsValid()))
	{
		for (int32 Frame = 0; Frame < 16 * 10 && !Timeout->IsAnswered(); ++Frame)
		{
			Timeout->Tick(FGeometry(), 0.0, 0.1f);
		}
		TestTrue(TEXT("15 s without an answer: reverted to windowed at the custom size"), Timeout->IsAnswered() &&
			S->GetFullscreenMode() == EWindowMode::Windowed && S->GetScreenResolution() == Custom);
	}
	// Keep confirms the new mode.
	RbUiTest::Key(*Menu, EKeys::Left);
	TSharedPtr<SRbConfirmDialog> Confirm = H.TopDialog();
	if (TestTrue(TEXT("keep-or-revert dialog (keep)"), Confirm.IsValid()))
	{
		RbUiTest::Key(*Confirm, EKeys::Enter);
		TestTrue(TEXT("Keep: the new mode stays"), S->GetFullscreenMode() == EWindowMode::WindowedFullscreen && S->GetLastConfirmedFullscreenMode() == EWindowMode::WindowedFullscreen);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiKeyHints, "RawBreak.Unit.UI.KeyHints", RB_UNIT_TEST_FLAGS)
bool FRbUiKeyHints::RunTest(const FString& Parameters)
{
	const auto Text = [](const FRbKeyHintContext& C) { return FRbKeyHintsModel::BuildFromContext(C).ToDebugString(); };
	FRbKeyHintContext C;
	C.bHasStroke = true;
	C.bHasDirector = true;
	C.DirectorPhase = ERbDirectorPhase::AwaitStroke;
	C.StrokePhase = ERbStrokePhase::Walking;
	TestEqual(TEXT("walking"), Text(C), FString(TEXT("[RMB] Get down")));
	C.bReplayAllowed = true;
	TestEqual(TEXT("walking after a shot"), Text(C), FString(TEXT("[RMB] Get down  [R] Replay")));
	C.bReplayAllowed = false;
	C.InteractionVerb = FText::FromString(TEXT("Pick up the ball"));
	TestEqual(TEXT("an interaction comes first, on the confirm key"), Text(C), FString(TEXT("[F] Pick up the ball  [RMB] Get down")));
	C.InteractionVerb = FText::GetEmpty();
	C.StrokePhase = ERbStrokePhase::Down;
	TestEqual(TEXT("down on the shot"), Text(C),
		FString(TEXT("[LMB] Stroke  [Space] Commit  [Shift] Fine aim  [Ctrl] Settle  [Wheel] Elevation  [Arrows] English  [RMB] Stand up")));
	C.StrokePhase = ERbStrokePhase::Watching;
	C.DirectorPhase = ERbDirectorPhase::PlayingBack;
	TestEqual(TEXT("watching"), Text(C), FString(TEXT("[RMB] Stand up")));
	C.StrokePhase = ERbStrokePhase::Walking;
	TestEqual(TEXT("walking while the balls roll: no get down"), Text(C), FString());
	C.DirectorPhase = ERbDirectorPhase::AwaitPlacement;
	C.StrokePhase = ERbStrokePhase::PlacingCueBall;
	TestEqual(TEXT("ball in hand (M1, no hand)"), Text(C), FString(TEXT("[LMB / F] Place the cue ball")));
	C.bHasBallInHand = true;
	C.BallInHand = ERbBallInHandState::Carrying;
	TestEqual(TEXT("carrying"), Text(C), FString(TEXT("[LMB / F] Place the cue ball  [Shift] Fine")));
	C.BallInHand = ERbBallInHandState::Refused;
	TestTrue(TEXT("refused: a note, no key"), Text(C).StartsWith(TEXT("Not here")));
	C.BallInHand = ERbBallInHandState::Lowering;
	TestEqual(TEXT("lowering: nothing"), Text(C), FString());
	// Integration round (18.6.1): an empty hand while placing = the cue ball lies off the table; it is picked up first.
	C.BallInHand = ERbBallInHandState::Inactive;
	TestEqual(TEXT("empty hand: a note, no place key"), Text(C), FString(TEXT("Pick up the cue ball first")));
	C.DirectorPhase = ERbDirectorPhase::AwaitDecision;
	TestEqual(TEXT("decision"), Text(C), FString(TEXT("[Q / E] Choose  [Enter / F] Confirm")));
	C.DirectorPhase = ERbDirectorPhase::RackOver;
	TestEqual(TEXT("rack over"), Text(C), FString(TEXT("[Enter / F] Next rack")));
	C.DirectorPhase = ERbDirectorPhase::MatchOver;
	TestEqual(TEXT("match over"), Text(C), FString(TEXT("[Enter / F] New match")));
	C.Keys.Add(TEXT("CycleOption"), FText::FromString(TEXT("Z / C")));
	C.DirectorPhase = ERbDirectorPhase::AwaitDecision;
	TestEqual(TEXT("rebound keys show up"), Text(C), FString(TEXT("[Z / C] Choose  [Enter / F] Confirm")));
	C.bMenuOpen = true;
	TestEqual(TEXT("menu open: nothing"), Text(C), FString());
	C.bMenuOpen = false;
	C.bReplaying = true;
	TestEqual(TEXT("replay: nothing"), Text(C), FString());
	C.bReplaying = false;
	C.bShowKeyHints = false;
	TestEqual(TEXT("setting off: nothing"), Text(C), FString());
	TestEqual(TEXT("key label: LMB"), FRbKeyHintsModel::KeyLabel(EKeys::LeftMouseButton).ToString(), FString(TEXT("LMB")));
	TestEqual(TEXT("key label: arrows"), FRbKeyHintsModel::KeyLabel(EKeys::Left).ToString(), FString(TEXT("Arrows")));

	// Hold and fade on the overlay component: a new context shows its hints for 3 s, then they fade out (180 ms).
	TStrongObjectPtr<URbOverlayComponent> Overlay(NewObject<URbOverlayComponent>(GetTransientPackage()));
	FRbKeyHintContext Walk;
	Walk.bHasStroke = true;
	Walk.StrokePhase = ERbStrokePhase::Walking;
	const FRbKeyHintsModel WalkModel = FRbKeyHintsModel::BuildFromContext(Walk);
	Overlay->AdvanceKeyHints(WalkModel, 0.06f);
	TestNearlyEqual(TEXT("fading in"), Overlay->GetKeyHintOpacity(), 0.5f, 1e-3f);
	Overlay->AdvanceKeyHints(WalkModel, 0.1f);
	TestNearlyEqual(TEXT("in"), Overlay->GetKeyHintOpacity(), 1.0f, 1e-6f);
	for (int32 Step = 0; Step < 27; ++Step)
	{
		Overlay->AdvanceKeyHints(WalkModel, 0.1f);
	}
	TestNearlyEqual(TEXT("still shown at 2.86 s"), Overlay->GetKeyHintOpacity(), 1.0f, 1e-6f);
	Overlay->AdvanceKeyHints(WalkModel, 0.15f);
	TestNearlyEqual(TEXT("still shown at 3.01 s - 0.04 s"), Overlay->GetKeyHintOpacity(), 1.0f, 1e-6f);
	Overlay->AdvanceKeyHints(WalkModel, 0.14f);
	TestTrue(TEXT("fading out after 3 s"), Overlay->GetKeyHintOpacity() < 1.0f && Overlay->GetKeyHintOpacity() > 0.0f);
	Overlay->AdvanceKeyHints(WalkModel, 0.2f);
	TestNearlyEqual(TEXT("out"), Overlay->GetKeyHintOpacity(), 0.0f, 1e-6f);
	FRbKeyHintContext Down = Walk;
	Down.StrokePhase = ERbStrokePhase::Down;
	Overlay->AdvanceKeyHints(FRbKeyHintsModel::BuildFromContext(Down), 0.2f);
	TestNearlyEqual(TEXT("a context change shows them again"), Overlay->GetKeyHintOpacity(), 1.0f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiTwoTables, "RawBreak.Unit.UI.TwoTables", RB_UNIT_TEST_FLAGS)
bool FRbUiTwoTables::RunTest(const FString& Parameters)
{
	// UX-T25 on a two-table world (the M2-E dev level's layout: a 9-ft and a 7-ft table, the 7-ft is the player's).
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("RbUiTwoTablesWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->RemoveFromRoot();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	const auto SpawnTable = [World](int32 Index, ERbTablePreset Preset, const FVector& Location, double Yaw)
	{
		const FTransform Transform(FRotator(0.0, Yaw, 0.0), Location);
		ARbTable* Table = World->SpawnActorDeferred<ARbTable>(ARbTable::StaticClass(), Transform);
		Table->Preset = Preset;
		Table->TableIndex = Index;
		Table->FinishSpawning(Transform);
		return Table;
	};
	ARbTable* Nine = SpawnTable(0, ERbTablePreset::NineFootPro, FVector(0.0, 0.0, 0.0), 0.0);
	ARbTable* Seven = SpawnTable(1, ERbTablePreset::SevenFootBar, FVector(600.0, 300.0, 0.0), 35.0);
	Seven->Tags.Add(FName(TEXT("RbPlayerTable")));
	URbTableSubsystem* Tables = World->GetSubsystem<URbTableSubsystem>();
	if (!TestNotNull(TEXT("table subsystem"), Tables) || !TestTrue(TEXT("two tables"), Nine && Seven))
	{
		return false;
	}
	// Two matches: a hot-seat at the 9-ft (not the player's), the player's practice at the 7-ft.
	TStrongObjectPtr<URbMatchDirector> Other = RbUiTest::MakeDirector(*this, ERbMatchMode::HotSeat, 5, TEXT("Sonny"));
	TStrongObjectPtr<URbMatchDirector> Mine = RbUiTest::MakeDirector(*this, ERbMatchMode::Practice, 6);
	if (!Other.IsValid() || !Mine.IsValid())
	{
		return false;
	}
	FRbTableSession A;
	A.TableIndex = 0;
	A.Table = Nine;
	A.Director = Other.Get();
	FRbTableSession B;
	B.TableIndex = 1;
	B.Table = Seven;
	B.Director = Mine.Get();
	Tables->RegisterSession(A);
	Tables->RegisterSession(B);

	TestTrue(TEXT("the player's match = the tagged table's"), URbUiSubsystem::FindPlayerDirector(World) == Mine.Get());

	// The overlay on an actor of this world binds to the player's match only.
	AActor* Owner = World->SpawnActor<AActor>();
	URbOverlayComponent* Overlay = NewObject<URbOverlayComponent>(Owner);
	Overlay->Refresh();
	const FString Text = Overlay->GetModel().ToDebugString();
	AddInfo(Text);
	TestTrue(TEXT("overlay shows the player's practice"), Text.Contains(TEXT("PRACTICE")) && !Text.Contains(TEXT("Sonny")) && !Text.Contains(TEXT("HOT-SEAT")));
	// The pause block and the key-hint context use the same resolution.
	const FRbPauseInfo Info = FRbPauseInfo::Build(URbUiSubsystem::FindPlayerDirector(World));
	TestTrue(TEXT("pause block: the player's match"), Info.bHasMatch && Info.Title.ToString().Contains(TEXT("PRACTICE")) && !Info.ToDebugString().Contains(TEXT("Sonny")));

	// Switching the player's table rebinds at the next refresh (the overlay checks every tick).
	Seven->Tags.Remove(FName(TEXT("RbPlayerTable")));
	Nine->Tags.Add(FName(TEXT("RbPlayerTable")));
	TestTrue(TEXT("tag moved: the 9-ft match"), URbUiSubsystem::FindPlayerDirector(World) == Other.Get());
	Overlay->Refresh();
	TestTrue(TEXT("overlay rebinds to the hot-seat"), Overlay->GetModel().ToDebugString().Contains(TEXT("HOT-SEAT")));

	// A player table without a session: no match shown (never another table's).
	Tables->UnregisterSession(0);
	TestTrue(TEXT("no session at the player's table: nothing"), URbUiSubsystem::FindPlayerDirector(World) == nullptr);
	Overlay->SetDirector(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbUiPointerFocus, "RawBreak.Unit.UI.PointerFocus", RB_UNIT_TEST_FLAGS)
bool FRbUiPointerFocus::RunTest(const FString& Parameters)
{
	using RbUiTest::Key;
	RbUiTest::FScalabilityGuard Guard;
	RbUiTest::FHost H;
	const FVector2D RowSize(1100.0, RbUi::RowHeight);
	const FGeometry RowGeometry = FGeometry::MakeRoot(RowSize, FSlateLayoutTransform());
	const FVector2D OverLabel(300.0, 28.0);

	// --- a still cursor never takes the focus ------------------------------------------------------------------------------
	// The settings open from the pause menu by keyboard while the cursor rests mid-screen, over a row of the list.
	TSharedRef<SRbSettingsMenu> Menu = SNew(SRbSettingsMenu).Host(H.Host);
	Menu->GetRowWidget(6)->OnMouseEnter(RowGeometry, RbUiTest::StillCursor(OverLabel));
	TestEqual(TEXT("a new screen under a parked cursor: the first row keeps the focus"), Menu->GetFocusIndex(), 0);
	// Keyboard Down scrolls the list; the row that slides under the still cursor must not take the keyboard's focus.
	Key(*Menu, EKeys::Down);
	TestTrue(TEXT("Down: keyboard focus (scrolls its row into view)"), Menu->GetFocusIndex() == 1 && !Menu->IsFocusFromMouse());
	Menu->GetRowWidget(7)->OnMouseEnter(RowGeometry, RbUiTest::StillCursor(OverLabel));
	TestEqual(TEXT("a row scrolled under the still cursor does not steal the focus"), Menu->GetFocusIndex(), 1);
	// Moving the mouse onto a row focuses it - as pointer focus, which does not scroll (else the next row slides under the cursor,
	// takes the focus, scrolls ... to the end of the list).
	Menu->GetRowWidget(7)->OnMouseEnter(RowGeometry, RbUiTest::Mouse(OverLabel));
	TestTrue(TEXT("real motion focuses the row (pointer focus: no scroll)"), Menu->GetFocusIndex() == 7 && Menu->IsFocusFromMouse());
	Key(*Menu, EKeys::Down);
	TestTrue(TEXT("the next key is keyboard focus again"), Menu->GetFocusIndex() == 8 && !Menu->IsFocusFromMouse());
	// Title and pause items follow the same rule.
	const FGeometry ItemGeometry = FGeometry::MakeRoot(FVector2D(560.0, 64.0), FSlateLayoutTransform());
	TSharedRef<SRbTitleScreen> Title = SNew(SRbTitleScreen).Host(H.Host);
	Title->GetFocusItemWidget(2)->OnMouseEnter(ItemGeometry, RbUiTest::StillCursor(FVector2D(40.0, 30.0)));
	TestEqual(TEXT("title: a still cursor over Quit leaves Play focused"), Title->GetFocusIndex(), 0);
	TSharedRef<SRbPauseMenu> Pause = SNew(SRbPauseMenu).Host(H.Host);
	Pause->GetFocusItemWidget(3)->OnMouseEnter(ItemGeometry, RbUiTest::StillCursor(FVector2D(40.0, 30.0)));
	TestEqual(TEXT("pause: a still cursor over Quit to desktop leaves Resume focused"), Pause->GetFocusIndex(), 0);
	Pause->GetFocusItemWidget(3)->OnMouseEnter(ItemGeometry, RbUiTest::Mouse(FVector2D(40.0, 30.0)));
	TestEqual(TEXT("pause: moving onto it focuses it"), Pause->GetFocusIndex(), 3);

	// --- slider drags end when the button is gone -----------------------------------------------------------------------------
	TSharedRef<SRbSettingsMenu> Camera = SNew(SRbSettingsMenu).Host(H.Host).InitialPage(ERbSettingsPage::Camera);
	TSharedPtr<SRbOptionRow> Fov = Camera->GetRowWidget(1);
	if (!TestTrue(TEXT("the FOV row"), Fov.IsValid() && Fov->GetRow()->Id == TEXT("cam.fov")))
	{
		return false;
	}
	const float ControlStart = RowSize.X - SRbOptionRow::ControlWidth + RbUi::FocusBarWidth;
	const FVector2D At20(ControlStart + 0.2f * SRbOptionRow::TrackWidth, 28.0);
	const FVector2D At90(ControlStart + 0.9f * SRbOptionRow::TrackWidth, 28.0);
	Fov->OnMouseButtonDown(RowGeometry, RbUiTest::Mouse(At20));
	const float Grabbed = H.Settings->VerticalFovDeg;
	TestTrue(TEXT("press on the track: dragging at 20 % (47 deg)"), Fov->IsDragging() && FMath::IsNearlyEqual(Grabbed, 47.0f, 1e-4f));
	Fov->OnMouseCaptureLost(FCaptureLostEvent(0, 0));
	TestFalse(TEXT("capture lost (Alt-Tab, a dialog) ends the drag"), Fov->IsDragging());
	Fov->OnMouseMove(RowGeometry, RbUiTest::Hover(At90));
	TestNearlyEqual(TEXT("after the lost capture the bare cursor does not move the slider"), H.Settings->VerticalFovDeg, Grabbed, 1e-4f);
	Fov->OnMouseButtonDown(RowGeometry, RbUiTest::Mouse(At20));
	Fov->OnMouseMove(RowGeometry, RbUiTest::Hover(At90));
	TestTrue(TEXT("a move without the button ends a drag whose release went elsewhere"), !Fov->IsDragging() &&
		FMath::IsNearlyEqual(H.Settings->VerticalFovDeg, Grabbed, 1e-4f));
	const FVector2D PastEnd(ControlStart + SRbOptionRow::TrackWidth + 20.0f, 28.0);
	Fov->OnMouseButtonDown(RowGeometry, RbUiTest::Mouse(At20));
	Fov->OnMouseMove(RowGeometry, RbUiTest::Mouse(PastEnd));
	TestNearlyEqual(TEXT("a held drag still follows the cursor (past the end = 75 deg)"), H.Settings->VerticalFovDeg, 75.0f, 1e-4f);
	Fov->OnMouseButtonUp(RowGeometry, RbUiTest::Mouse(PastEnd));
	TestFalse(TEXT("the release ends it"), Fov->IsDragging());

	// --- the Space preview ends when a dialog covers the screen -----------------------------------------------------------------
	TestTrue(TEXT("hold Space: preview"), Key(*Camera, EKeys::SpaceBar) && Camera->IsPreviewing());
	Camera->OnDeactivated(); // the stack covers it (a confirm dialog); the Space release goes to the dialog
	TestFalse(TEXT("covered: the preview ends (the panel never stays invisible)"), Camera->IsPreviewing());

	// --- the resolution row's per-frame fast path equals the full list ----------------------------------------------------------
	const FRbSettingDef* Res = FRbSettingsRegistry::Find(TEXT("dsp.resolution"));
	if (TestNotNull(TEXT("resolution row"), Res))
	{
		const TArray<FText> Labels = FRbSettingsRegistry::EnumLabels(*Res, *H.Settings);
		TestTrue(TEXT("at least one resolution"), Labels.Num() > 0);
		TestEqual(TEXT("count without formatting = the label list"), FRbSettingsRegistry::NumEnumValues(*Res, *H.Settings), Labels.Num());
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("label %d without the list"), Index), FRbSettingsRegistry::FormatValue(*Res, *H.Settings, Index).ToString(),
				Labels[Index].ToString());
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
