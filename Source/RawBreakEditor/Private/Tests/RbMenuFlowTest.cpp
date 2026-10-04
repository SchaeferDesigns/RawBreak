// M2-D functional menu tests (PIE; Docs/ue-architecture.md 18.4, ui-ux 20.1). Run with --render for the real Slate focus checks
// (UX-T09); without a renderer the screens are not added to the viewport and only the stack / keys / input modes are checked.
//   RawBreak.Functional.MenuFlow       L_Title -> Dive bar Hot-seat (the test room while L_DiveBar is not built: a warning)
//                                      -> pause (Esc path: ARbPlayerController::HandlePause) -> settings -> Camera page ->
//                                      FOV + 5 deg -> back (saved) -> resume -> pause -> Quit to title (confirm) -> the title
//                                      again (its list at once: the clean first seconds belong to the launch, ui-ux 6.3); then
//                                      every other venue x mode through the title (Practice / Hot-seat in both
//                                      venues) and back through the pause menu. Checks the screen stack, one focused item after
//                                      every push / pop, the input modes (menus: UI only with a cursor; play: game only,
//                                      hidden captured mouse), the world pause, the player's match in the pause block, the
//                                      title camera, the ?Mode= of every travel.
//   RawBreak.Functional.PauseMidShot   UX-T10 on L_M1_TestRoom: a live break is playing -> the pause menu opens: the playback
//                                      clock holds (no advance in ~1 s of real time), the director does not commit, the
//                                      audio's pause mix is on (URbAudioSubsystem::IsPausedMix, off again after the resume); resume:
//                                      no time jump (the clock re-anchors at the held shot time; the shown time advances only
//                                      by the real time since the resume x rate, so a render hitch is fine, a skip over the
//                                      pause is not), the director commits only after the resumed playback finished, the
//                                      committed shot's ResultHash is unchanged.
// The editor's own user settings are restored at the end (the flow changes the FOV and saves). Owner: M2-D.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"

#include "Audio/RbAudioSubsystem.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Camera/RbCameraRigComponent.h"
#include "Core/RbAssetPaths.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "Settings/RbGameUserSettings.h"
#include "Settings/RbSettingsRegistry.h"
#include "Simulation/RbShot.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"
#include "UI/Core/RbUiSubsystem.h"
#include "UI/Core/SRbScreen.h"
#include "UI/Front/RbTitleGameMode.h"
#include "UI/Screens/SRbConfirmDialog.h"
#include "UI/Screens/SRbPauseMenu.h"
#include "UI/Screens/SRbSettingsMenu.h"
#include "UI/Screens/SRbTitleScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbMenuFlow
{
	// The PIE world (it changes with every travel; GEditor->PlayWorld is not relied on).
	UWorld* PieWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	APlayerController* Controller()
	{
		UWorld* World = PieWorld();
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	URbUiSubsystem* Ui()
	{
		return URbUiSubsystem::Get(Controller());
	}

	bool IsMap(const UWorld* World, const TCHAR* PackagePath)
	{
		return World && World->GetOutermost() && FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) ==
			FPackageName::GetShortName(PackagePath);
	}

	// A key press on the top screen, by the key's name (the editor module does not link InputCore: URbUiSubsystem builds the FKey).
	bool Key(const TCHAR* KeyName)
	{
		URbUiSubsystem* Subsystem = Ui();
		return Subsystem && Subsystem->HandleMenuKey(FName(KeyName));
	}

	// UX-T09 in the real viewport: exactly one focused item, and it holds the player's Slate focus.
	void CheckFocus(FAutomationTestBase& Test, const TCHAR* What)
	{
		URbUiSubsystem* Subsystem = Ui();
		const TSharedPtr<SRbScreen> Top = Subsystem ? Subsystem->GetTopWidget() : nullptr;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: a screen is open"), What), Top.IsValid()))
		{
			return;
		}
		Test.TestTrue(FString::Printf(TEXT("%s: exactly one focused item"), What), Top->GetNumFocusItems() > 0 && Top->GetFocusedWidget().IsValid());
		if (FApp::CanEverRender())
		{
			Test.TestTrue(FString::Printf(TEXT("%s: the focused item holds the Slate focus"), What), Subsystem->GetSlateFocusedWidget() == Top->GetFocusedWidget());
		}
	}

	void CheckMenuInput(FAutomationTestBase& Test, const TCHAR* What)
	{
		const APlayerController* PC = Controller();
		Test.TestTrue(FString::Printf(TEXT("%s: menu input (cursor shown)"), What), PC && PC->ShouldShowMouseCursor());
	}

	void CheckGameInput(FAutomationTestBase& Test, const TCHAR* What)
	{
		const APlayerController* PC = Controller();
		const URbUiSubsystem* Subsystem = Ui();
		Test.TestTrue(FString::Printf(TEXT("%s: no menu open"), What), Subsystem && !Subsystem->IsMenuOpen());
		Test.TestTrue(FString::Printf(TEXT("%s: game input (hidden mouse)"), What), PC && !PC->ShouldShowMouseCursor());
		Test.TestFalse(FString::Printf(TEXT("%s: the world runs"), What), PieWorld() && PieWorld()->IsPaused());
	}

	// One trip through the title: pick a venue and a mode (bFull: the 18.4 flow in the venue), then back to the title.
	struct FLeg
	{
		ERbVenue Venue = ERbVenue::TestRoom;
		ERbMatchMode Mode = ERbMatchMode::Practice;
		bool bFull = false;
	};
}

class FRbMenuFlowCommand : public IAutomationLatentCommand
{
public:
	FRbMenuFlowCommand(FAutomationTestBase* InTest, TArray<RbMenuFlow::FLeg> InLegs) : Test(InTest), Legs(MoveTemp(InLegs)) {}

	virtual bool Update() override
	{
		using namespace RbMenuFlow;
		if (Step == EStep::Done)
		{
			return true;
		}
		if (FPlatformTime::Seconds() - StepStart > StepTimeout)
		{
			Test->AddError(FString::Printf(TEXT("menu flow: step %d of leg %d timed out"), static_cast<int32>(Step), LegIndex));
			Finish();
			return true;
		}
		switch (Step)
		{
		case EStep::WaitTitle: return WaitTitle();
		case EStep::ChooseVenue: return ChooseVenue();
		case EStep::WaitVenue: return WaitVenue();
		case EStep::InVenue: return InVenue();
		case EStep::Done: break;
		}
		return true;
	}

private:
	enum class EStep : uint8
	{
		WaitTitle,
		ChooseVenue,
		WaitVenue,
		InVenue,
		Done,
	};

	void Next(EStep InStep, double Timeout = 60.0)
	{
		Step = InStep;
		StepStart = FPlatformTime::Seconds();
		StepTimeout = Timeout;
		Frames = 0;
	}

	void Finish()
	{
		// Restore the editor's own settings (the flow changed the FOV and saved them).
		if (URbGameUserSettings* S = URbGameUserSettings::Get(); S && OriginalFov > 0.0f)
		{
			FRbSettingsRegistry::SetAndApply(TEXT("cam.fov"), *S, OriginalFov);
			S->SaveSettings();
		}
		Step = EStep::Done;
	}

	bool WaitTitle()
	{
		using namespace RbMenuFlow;
		UWorld* World = PieWorld();
		URbUiSubsystem* Subsystem = Ui();
		if (!IsMap(World, RbAssetPaths::TitleMap) || !Subsystem || Subsystem->GetTopScreen() != ERbUiScreen::Title || ++Frames < 3)
		{
			return false;
		}
		const APlayerController* PC = Controller();
		Test->TestTrue(TEXT("title: ARbTitleGameMode"), Cast<ARbTitleGameMode>(World->GetAuthGameMode()) != nullptr);
		Test->TestTrue(TEXT("title: the level's title camera is the view"), PC && PC->GetViewTarget() && PC->GetViewTarget()->ActorHasTag(ARbTitleGameMode::TitleCameraTag));
		Test->TestTrue(TEXT("title: no pawn"), PC && PC->GetPawn() == nullptr);
		Test->TestEqual(TEXT("title: one screen"), Subsystem->GetStackIds().Num(), 1);
		CheckMenuInput(*Test, TEXT("title"));
		const TSharedPtr<SRbTitleScreen> Title = StaticCastSharedPtr<SRbTitleScreen>(Subsystem->GetTopWidget());
		if (LegIndex > 0)
		{
			// The clean first seconds belong to the launch (ui-ux 6.3): back from a venue, the list is there at once.
			Test->TestTrue(TEXT("title after Quit to title: the list shows at once (no second intro)"), Title->IsIntroDone());
		}
		if (!Title->IsIntroDone())
		{
			Key(TEXT("Escape")); // any key shows the list at once and chooses nothing
			Test->TestTrue(TEXT("title: the first key only reveals the list"), Title->IsIntroDone() && Title->GetLevel() == SRbTitleScreen::ELevel::Root &&
				Subsystem->GetStackIds().Num() == 1);
		}
		CheckFocus(*Test, TEXT("title root"));
		if (!Legs.IsValidIndex(LegIndex))
		{
			Finish();
			return true;
		}
		Next(EStep::ChooseVenue);
		return false;
	}

	bool ChooseVenue()
	{
		using namespace RbMenuFlow;
		const FLeg& Leg = Legs[LegIndex];
		URbUiSubsystem* Subsystem = Ui();
		const TSharedPtr<SRbTitleScreen> Title = Subsystem ? StaticCastSharedPtr<SRbTitleScreen>(Subsystem->GetTopWidget()) : nullptr;
		if (!Test->TestTrue(TEXT("title screen on top"), Title.IsValid() && Subsystem->GetTopScreen() == ERbUiScreen::Title))
		{
			Finish();
			return true;
		}
		Title->SetFocusIndex(0);
		Key(TEXT("Enter")); // Play
		Test->TestTrue(TEXT("Play: the venue list"), Title->GetLevel() == SRbTitleScreen::ELevel::Play);
		CheckFocus(*Test, TEXT("venue list"));
		// The list: The Low Bridge Tavern, Test room, Back.
		const int32 VenueIndex = Leg.Venue == ERbVenue::DiveBar ? 0 : 1;
		while (Title->GetFocusIndex() < VenueIndex) { Key(TEXT("Down")); }
		while (Title->GetFocusIndex() > VenueIndex) { Key(TEXT("Up")); }
		Key(TEXT("Enter"));
		Test->TestTrue(FString::Printf(TEXT("%s: the modes"), *SRbTitleScreen::VenueName(Leg.Venue).ToString()),
			Title->GetLevel() == SRbTitleScreen::ELevel::Venue && Title->GetSelectedVenue() == Leg.Venue);
		if (Leg.Mode == ERbMatchMode::HotSeat)
		{
			Key(TEXT("Down"));
		}
		Key(TEXT("Enter")); // travels
		Test->TestTrue(TEXT("the title shows the travel line"), Title->IsLoading());
		Next(EStep::WaitVenue, 90.0);
		return false;
	}

	bool WaitVenue()
	{
		using namespace RbMenuFlow;
		const FLeg& Leg = Legs[LegIndex];
		UWorld* World = PieWorld();
		const ARbGameMode* Mode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (!IsMap(World, RbTypes::MapFor(Leg.Venue)) || !Director || Director->GetPhase() == ERbDirectorPhase::Idle || ++Frames < 5)
		{
			return false;
		}
		const FString Name = FString::Printf(TEXT("%s %s"), *SRbTitleScreen::VenueName(Leg.Venue).ToString(), *UEnum::GetValueAsString(Leg.Mode));
		Test->AddInfo(FString::Printf(TEXT("travelled to %s"), *Name));
		Test->TestTrue(FString::Printf(TEXT("%s: ?Mode= reached the match"), *Name), Director->GetSetup().Mode == Leg.Mode);
		Test->TestTrue(FString::Printf(TEXT("%s: the player's match is the pause block's"), *Name), URbUiSubsystem::FindPlayerDirector(World) == Director);
		CheckGameInput(*Test, *Name);
		Next(EStep::InVenue, 60.0);
		SubStep = 0;
		return false;
	}

	bool InVenue()
	{
		using namespace RbMenuFlow;
		const FLeg& Leg = Legs[LegIndex];
		UWorld* World = PieWorld();
		ARbPlayerController* PC = Cast<ARbPlayerController>(Controller());
		URbUiSubsystem* Subsystem = Ui();
		URbGameUserSettings* Settings = URbGameUserSettings::Get();
		if (!Test->TestTrue(TEXT("venue: RAW BREAK controller, UI subsystem, settings"), PC && Subsystem && Settings))
		{
			Finish();
			return true;
		}
		if (++Frames < 3)
		{
			return false; // let each step's input mode / focus settle for a few frames
		}
		Frames = 0;
		switch (SubStep++)
		{
		case 0:
		{
			PC->HandlePause(); // Esc
			Test->TestTrue(TEXT("Esc opens the pause menu"), Subsystem->GetTopScreen() == ERbUiScreen::Pause);
			Test->TestTrue(TEXT("the pause menu pauses the world"), World->IsPaused() && Subsystem->IsGamePausedByMenu());
			const TSharedPtr<SRbPauseMenu> Pause = StaticCastSharedPtr<SRbPauseMenu>(Subsystem->GetTopWidget());
			Test->TestTrue(TEXT("pause block: the player's match"), Pause.IsValid() && Pause->GetInfo().bHasMatch &&
				Pause->GetInfo().Title.ToString().Contains(Leg.Mode == ERbMatchMode::HotSeat ? TEXT("HOT-SEAT") : TEXT("PRACTICE")));
			return false;
		}
		case 1:
			CheckFocus(*Test, TEXT("pause"));
			CheckMenuInput(*Test, TEXT("pause"));
			if (!Leg.bFull)
			{
				SubStep = 10; // straight to Quit to title
				return false;
			}
			Key(TEXT("Down"));
			Key(TEXT("Enter")); // Settings
			Test->TestTrue(TEXT("Settings from the pause menu"), Subsystem->GetStackIds() == TArray<ERbUiScreen>({ERbUiScreen::Pause, ERbUiScreen::Settings}));
			return false;
		case 2:
		{
			CheckFocus(*Test, TEXT("settings"));
			const TSharedPtr<SRbSettingsMenu> Menu = StaticCastSharedPtr<SRbSettingsMenu>(Subsystem->GetTopWidget());
			Key(TEXT("E"));
			Key(TEXT("E"));
			Test->TestTrue(TEXT("E E: the Camera page"), Menu->GetPage() == ERbSettingsPage::Camera);
			Key(TEXT("Down"));
			Test->TestTrue(TEXT("the FOV row"), Menu->GetFocusedRow() && Menu->GetFocusedRow()->Id == TEXT("cam.fov"));
			OriginalFov = Settings->VerticalFovDeg;
			const float Target = OriginalFov <= 70.0f ? OriginalFov + 5.0f : OriginalFov - 5.0f;
			int32 Broadcasts = 0;
			const FDelegateHandle Handle = URbGameUserSettings::OnSettingsChanged().AddLambda([&Broadcasts]() { ++Broadcasts; });
			for (int32 Press = 0; Press < 5; ++Press)
			{
				Key(Target > OriginalFov ? TEXT("Right") : TEXT("Left"));
			}
			URbGameUserSettings::OnSettingsChanged().Remove(Handle);
			NewFov = Settings->VerticalFovDeg;
			Test->TestNearlyEqual(TEXT("5 presses: FOV +- 5 deg"), NewFov, Target, 1e-4f);
			Test->TestEqual(TEXT("every change notifies the readers"), Broadcasts, 5);
			Test->TestTrue(TEXT("the menu has unsaved changes"), Menu->IsDirty());
			return false;
		}
		case 3:
			Key(TEXT("Escape")); // back: saves
			Test->TestTrue(TEXT("Back returns to the pause menu"), Subsystem->GetTopScreen() == ERbUiScreen::Pause && World->IsPaused());
			CheckFocus(*Test, TEXT("pause after settings"));
			return false;
		case 4:
		{
			Key(TEXT("Escape")); // resume
			CheckGameInput(*Test, TEXT("resumed"));
			// The camera rig re-reads the FOV on OnSettingsChanged (M2-F); without it the new value applies at the next level.
			const ARbPlayerCharacter* Character = Cast<ARbPlayerCharacter>(PC->GetPawn());
			const URbCameraRigComponent* Rig = Character ? Character->GetCameraRig() : nullptr;
			if (Rig && Settings->CameraPreset == ERbCameraPreset::Eyes)
			{
				const double RigFov = Rig->GetEffectiveParams().VerticalFovDeg;
				if (!FMath::IsNearlyEqual(RigFov, static_cast<double>(NewFov), 0.01))
				{
					Test->AddWarning(FString::Printf(TEXT("the camera rig still shows %.1f deg (the setting is %.1f): it re-reads VerticalFovDeg only at BeginPlay "
						"until M2-F listens to OnSettingsChanged"), RigFov, NewFov));
				}
				else
				{
					Test->AddInfo(TEXT("the camera rig follows the new FOV"));
				}
			}
			return false;
		}
		case 5:
			PC->HandlePause();
			Test->TestTrue(TEXT("pause again"), Subsystem->GetTopScreen() == ERbUiScreen::Pause);
			SubStep = 10;
			return false;
		case 10:
		{
			// Quit to title: asks first; Cancel keeps playing, Confirm leaves.
			const TSharedPtr<SRbScreen> Pause = Subsystem->GetTopWidget();
			Pause->SetFocusIndex(2);
			Key(TEXT("Enter"));
			Test->TestTrue(TEXT("Quit to title asks first"), Subsystem->GetTopScreen() == ERbUiScreen::Dialog && IsMap(World, RbTypes::MapFor(Leg.Venue)));
			CheckFocus(*Test, TEXT("quit dialog"));
			const TSharedPtr<SRbScreen> Dialog = Subsystem->GetTopWidget();
			Test->TestEqual(TEXT("the safe answer is focused"), Dialog->GetFocusIndex(), 1);
			if (Leg.bFull)
			{
				Key(TEXT("Escape"));
				Test->TestTrue(TEXT("Esc in the dialog: back to the pause menu"), Subsystem->GetTopScreen() == ERbUiScreen::Pause);
				Pause->SetFocusIndex(2);
				Key(TEXT("Enter"));
			}
			return false;
		}
		case 11:
			if (Leg.bFull)
			{
				CheckFocus(*Test, TEXT("quit dialog again"));
			}
			Key(TEXT("Up"));
			Key(TEXT("Enter")); // confirm: travel to the title
			++LegIndex;
			Next(EStep::WaitTitle, 90.0);
			return false;
		default:
			break;
		}
		return false;
	}

	FAutomationTestBase* Test;
	TArray<RbMenuFlow::FLeg> Legs;
	int32 LegIndex = 0;
	EStep Step = EStep::WaitTitle;
	int32 SubStep = 0;
	int32 Frames = 0;
	double StepStart = FPlatformTime::Seconds();
	double StepTimeout = 90.0;
	float OriginalFov = -1.0f;
	float NewFov = -1.0f;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMenuFlowTest, "RawBreak.Functional.MenuFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbMenuFlowTest::RunTest(const FString& Parameters)
{
	using RbMenuFlow::FLeg;
	if (!FPackageName::DoesPackageExist(RbAssetPaths::TitleMap))
	{
		AddError(FString::Printf(TEXT("%s missing: run Tools/unreal/editor/rb_make_title.py"), RbAssetPaths::TitleMap));
		return false;
	}
	const bool bDiveBar = FPackageName::DoesPackageExist(RbAssetPaths::DiveBarMap);
	if (!bDiveBar)
	{
		AddWarning(FString::Printf(TEXT("%s is not built yet (M2-A): the flow runs the test room only"), RbAssetPaths::DiveBarMap));
	}
	TArray<FLeg> Legs;
	Legs.Add({bDiveBar ? ERbVenue::DiveBar : ERbVenue::TestRoom, ERbMatchMode::HotSeat, true}); // the 18.4 flow
	Legs.Add({ERbVenue::TestRoom, ERbMatchMode::Practice, false});
	if (bDiveBar)
	{
		Legs.Add({ERbVenue::DiveBar, ERbMatchMode::Practice, false});
		Legs.Add({ERbVenue::TestRoom, ERbMatchMode::HotSeat, false});
	}
	AutomationOpenMap(RbAssetPaths::TitleMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbMenuFlowCommand(this, MoveTemp(Legs)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------
// UX-T10: pause mid-shot.
// ---------------------------------------------------------------------------------------------------------------------------

class FRbPauseMidShotCommand : public IAutomationLatentCommand
{
public:
	explicit FRbPauseMidShotCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace RbMenuFlow;
		UWorld* World = PieWorld();
		ARbGameMode* Mode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
		URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		const double Now = FPlatformTime::Seconds();
		if (Start < 0.0)
		{
			Start = Now;
		}
		if (Now - Start > 120.0)
		{
			Test->AddError(FString::Printf(TEXT("pause mid-shot: stage %d timed out"), Stage));
			return true;
		}
		if (!Director || Director->GetPhase() == ERbDirectorPhase::Idle)
		{
			return false;
		}
		URbShotPlaybackComponent* Playback = Mode->GetBallSet() ? Mode->GetBallSet()->GetPlayback() : nullptr;
		URbUiSubsystem* Subsystem = Ui();
		switch (Stage)
		{
		case 0:
		{
			// Ball in hand behind the head string, then a live break through the pawn's stroke component (the M1Rack path: the
			// cheat commands queue, the human layer strokes, the worker simulates, the playback runs in real time at rate 2).
			APlayerController* PC = Controller();
			if (!Test->TestNotNull(TEXT("controller"), PC) || !Test->TestNotNull(TEXT("playback"), Playback) || !Test->TestNotNull(TEXT("UI subsystem"), Subsystem))
			{
				return true;
			}
			if (Director->GetPhase() != ERbDirectorPhase::AwaitPlacement || Now - Start < 1.0)
			{
				return false; // the pawn and the scene settle first
			}
			const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
			const rb::Vec3 Apex = Director->GetTableState().Balls[1].State.Position;
			const double CueX = Rules.HeadStringX - 0.10;
			const double Phi = FMath::RadiansToDegrees(FMath::Atan2(Apex.y - 0.12, Apex.x - CueX));
			ShotsBefore = Director->GetMatchShotIndex();
			PC->ConsoleCommand(TEXT("RbPlaybackRate 2"));
			PC->ConsoleCommand(FString::Printf(TEXT("RbPlaceCueBall %.9g 0.12"), CueX));
			PC->ConsoleCommand(FString::Printf(TEXT("RbStroke 8.5 %.9g"), Phi));
			Mark = Now;
			Stage = 10;
			return false;
		}
		case 10:
			if (Director->GetPhase() != ERbDirectorPhase::PlayingBack || !Playback->IsPlaying())
			{
				if (Director->GetMatchShotIndex() != ShotsBefore)
				{
					Test->AddError(TEXT("the break committed without a live playback"));
					return true;
				}
				return Now - Mark > 30.0 ? (Test->AddError(TEXT("the break never started playing")), true) : false;
			}
			Shot = Playback->GetShot();
			PlayedHash = Shot.IsValid() ? Shot->ResultHash : 0;
			Test->TestTrue(TEXT("the shot carries its result hash"), PlayedHash != 0 && PlayedHash == RbShot::ResultHash(Shot->Result));
			Test->AddInfo(FString::Printf(TEXT("playing: shot time %.3f of %.3f s, rate %.2f"), Playback->GetShotTime(), Shot->Result.StopTime,
				Director->GetLivePlaybackRate()));
			Stage = 1;
			return false;
		case 1:
			if (Playback->GetShotTime() < 0.5)
			{
				if (Now - LastLog > 1.0)
				{
					LastLog = Now;
					Test->AddInfo(FString::Printf(TEXT("waiting: shot time %.3f, playing %d, held %d, phase %d, world paused %d, frame %llu"), Playback->GetShotTime(),
						Playback->IsPlaying() ? 1 : 0, Playback->IsHeld() ? 1 : 0, static_cast<int32>(Director->GetPhase()), World->IsPaused() ? 1 : 0,
						static_cast<unsigned long long>(GFrameCounter)));
				}
				if (!Playback->IsPlaying() || Director->GetPhase() != ERbDirectorPhase::PlayingBack)
				{
					Test->AddError(TEXT("the live playback ended before the pause"));
					return true;
				}
				return false;
			}
			Subsystem->TogglePauseMenu(); // Esc
			Test->TestTrue(TEXT("mid-shot: the pause menu is open and the world paused"), Subsystem->GetTopScreen() == ERbUiScreen::Pause && World->IsPaused());
			if (const URbAudioSubsystem* Audio = URbAudioSubsystem::Get(World))
			{
				Test->TestTrue(TEXT("paused: the audio's pause mix is on (audio.md 7.3)"), Audio->IsPausedMix());
			}
			else
			{
				Test->AddError(TEXT("no URbAudioSubsystem in the PIE world"));
			}
			Stage = 2;
			Mark = Now;
			return false;
		case 2:
			// The playback notices the world pause at its next tick.
			if (!Playback->IsHeld())
			{
				return Now - Mark > 5.0 ? (Test->AddError(TEXT("the playback never held under the world pause")), true) : false;
			}
			HeldTime = Playback->GetShotTime();
			Mark = Now;
			Stage = 3;
			return false;
		case 3:
			// About a second of real time in the pause: the clock holds, nothing commits.
			Test->TestEqual(TEXT("paused: the shot time holds"), Playback->GetShotTime(), HeldTime);
			if (Now - Mark < 1.0)
			{
				return false;
			}
			Test->TestTrue(TEXT("paused: the director waits in PlayingBack"), Director->GetPhase() == ERbDirectorPhase::PlayingBack &&
				Director->GetMatchShotIndex() == ShotsBefore);
			PausedSeconds = Now - Mark;
			Test->AddInfo(FString::Printf(TEXT("held at shot time %.3f s for %.2f s of real time"), HeldTime, PausedSeconds));
			Subsystem->TogglePauseMenu(); // Esc on the pause root = Resume
			Test->TestTrue(TEXT("resumed"), !Subsystem->IsMenuOpen() && !World->IsPaused());
			if (const URbAudioSubsystem* Audio = URbAudioSubsystem::Get(World))
			{
				Test->TestFalse(TEXT("resumed: the audio's pause mix is off"), Audio->IsPausedMix());
			}
			Mark = Now;
			ResumeClock = FApp::GetCurrentTime(); // the playback's frame clock (RbShotPlaybackComponent.h)
			Stage = 4;
			return false;
		case 4:
		{
			if (Playback->IsHeld())
			{
				return Now - Mark > 5.0 ? (Test->AddError(TEXT("the playback never resumed")), true) : false;
			}
			if (Playback->GetShotTime() <= HeldTime)
			{
				return false; // the first tick after the resume re-anchors the clock
			}
			// No time jump: the clock re-anchored at the held shot time when the world resumed, so the shown time advanced only by
			// the real time since the resume (x rate), never by the paused second(s). A long first frame (a render hitch) is
			// allowed; a jump over the pause (rate x PausedSeconds) is not.
			const FRbPlaybackClock Mapping = Playback->GetClockMapping();
			const double Advance = Playback->GetShotTime() - HeldTime;
			const double SinceResume = FApp::GetCurrentTime() - ResumeClock;
			const double Rate = static_cast<double>(Mapping.Rate);
			Test->TestTrue(FString::Printf(TEXT("resume: the clock re-anchored at the held time (origin shot time %.4f, held %.4f; origin clock %.4f >= resume %.4f)"),
				Mapping.OriginShotTime, HeldTime, Mapping.OriginClock, ResumeClock),
				FMath::IsNearlyEqual(Mapping.OriginShotTime, HeldTime, 1e-6) && Mapping.OriginClock >= ResumeClock - 1e-9);
			Test->TestTrue(FString::Printf(TEXT("resume: no time jump (%.3f -> %.3f s: +%.3f s of shot time in %.3f s of real time at rate %.2f; the pause was %.2f s)"),
				HeldTime, Playback->GetShotTime(), Advance, SinceResume, Rate, PausedSeconds),
				Advance <= Rate * SinceResume + 1e-6 && Advance < Rate * PausedSeconds);
			Stage = 5;
			return false;
		}
		case 5:
			if (Director->GetPhase() == ERbDirectorPhase::PlayingBack)
			{
				Test->TestTrue(TEXT("no commit while the playback runs"), Director->GetMatchShotIndex() == ShotsBefore);
				return false;
			}
		{
			// Committed after the resumed playback finished.
			Test->TestFalse(TEXT("the playback had finished when the director committed"), Playback->IsPlaying() && Playback->GetShot() == Shot);
			Test->TestEqual(TEXT("one shot committed"), static_cast<int64>(Director->GetMatchShotIndex()), static_cast<int64>(ShotsBefore) + 1);
			const URbReplaySubsystem* Replay = World->GetSubsystem<URbReplaySubsystem>();
			const TSharedPtr<const FRbShot> Recorded = Replay ? Replay->GetShot(0) : nullptr;
			Test->TestTrue(TEXT("the committed shot is the paused one"), Recorded.IsValid() && Recorded == Shot);
			Test->TestTrue(TEXT("ResultHash unchanged"), Recorded.IsValid() && Recorded->ResultHash == PlayedHash && RbShot::ResultHash(Recorded->Result) == PlayedHash);
			Test->AddInfo(FString::Printf(TEXT("ResultHash %016llx, committed in phase %d"), PlayedHash, static_cast<int32>(Director->GetPhase())));
			return true;
		}
		default:
			return true;
		}
	}

private:
	FAutomationTestBase* Test;
	int32 Stage = 0;
	double Start = -1.0;
	double Mark = 0.0;
	double HeldTime = 0.0;
	double PausedSeconds = 0.0;
	double ResumeClock = 0.0;
	double LastLog = 0.0;
	uint32 ShotsBefore = 0;
	uint64 PlayedHash = 0;
	TSharedPtr<const FRbShot> Shot;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPauseMidShotTest, "RawBreak.Functional.PauseMidShot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbPauseMidShotTest::RunTest(const FString& Parameters)
{
	if (!FPackageName::DoesPackageExist(RbAssetPaths::M1TestRoomMap))
	{
		AddError(FString::Printf(TEXT("%s missing: run Tools/unreal/editor/rb_make_test_room.py"), RbAssetPaths::M1TestRoomMap));
		return false;
	}
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbPauseMidShotCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
