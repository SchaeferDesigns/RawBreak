#include "UI/Core/RbUiSubsystem.h"

#include "RawBreak.h"
#include "Audio/RbAudioSubsystem.h"
#include "Core/RbAssetPaths.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Player/RbPlayerCharacter.h"
#include "Settings/RbGameUserSettings.h"
#include "Settings/RbSettingsRegistry.h"
#include "UI/Core/SRbScreen.h"
#include "UI/Front/RbTitleGameMode.h"
#include "UI/Screens/SRbPauseMenu.h"
#include "UI/Screens/SRbSettingsMenu.h"
#include "UI/Screens/SRbTitleScreen.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Owner: M2-D. Screen stack, input modes, pause, travel and the dev switch (header).

#define LOCTEXT_NAMESPACE "RbUi"

namespace RbUiPrivate
{
	constexpr int32 ScreenLayer = 60; // ui-ux 2.3
	constexpr int32 DialogLayer = 70;

	FString ReadDevScreen()
	{
		FString Value;
		FParse::Value(FCommandLine::Get(), TEXT("RbUiScreen="), Value);
		return Value;
	}

	int32 RemainingShaderJobs()
	{
#if WITH_EDITOR
		return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
		return 0;
#endif
	}

	bool ParsePage(const FString& Name, ERbSettingsPage& Out)
	{
		for (int32 Index = 0; Index < static_cast<int32>(ERbSettingsPage::Count); ++Index)
		{
			if (FRbSettingsRegistry::PageName(static_cast<ERbSettingsPage>(Index)).ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				Out = static_cast<ERbSettingsPage>(Index);
				return true;
			}
		}
		return false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------
// Statics
// ---------------------------------------------------------------------------------------------------------------------------

URbUiSubsystem* URbUiSubsystem::Get(const APlayerController* Controller)
{
	const ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
	return Player ? Player->GetSubsystem<URbUiSubsystem>() : nullptr;
}

const FString& URbUiSubsystem::GetDevScreen()
{
	static const FString Screen = RbUiPrivate::ReadDevScreen();
	return Screen;
}

bool URbUiSubsystem::IsDevHoldKeyHints()
{
	return GetDevScreen().StartsWith(TEXT("KeyHints"), ESearchCase::IgnoreCase);
}

URbMatchDirector* URbUiSubsystem::FindPlayerDirector(const UObject* WorldContext)
{
	if (const URbTableSubsystem* Tables = URbTableSubsystem::Get(WorldContext))
	{
		if (const FRbTableSession* Session = Tables->GetPlayerSession())
		{
			if (Session->Director.IsValid())
			{
				return Session->Director.Get();
			}
		}
		if (Tables->GetSessions().Num() > 0)
		{
			return nullptr; // sessions exist, none for the player's table: the player has no match (never another table's)
		}
	}
	const ARbGameMode* Mode = ARbGameMode::Get(WorldContext);
	return Mode ? Mode->GetDirector() : nullptr;
}

bool URbUiSubsystem::IsVenueAvailable(ERbVenue Venue, FText& OutReason)
{
	if (FPackageName::DoesPackageExist(RbTypes::MapFor(Venue)))
	{
		return true;
	}
	OutReason = LOCTEXT("VenueMissing", "Not built yet");
	return false;
}

bool URbUiSubsystem::CanQuitToTitle(FText& OutReason)
{
	if (FPackageName::DoesPackageExist(RbAssetPaths::TitleMap))
	{
		return true;
	}
	OutReason = LOCTEXT("TitleMissing", "No title level");
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------------------------------------

void URbUiSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &URbUiSubsystem::TickUi));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &URbUiSubsystem::OnWorldCleanup);
}

void URbUiSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	if (UGameViewportClient* Viewport = DrawHookViewport.Get())
	{
		Viewport->OnBeginDraw().Remove(BeginDrawHandle);
	}
	ClearStack(false);
	Host.Reset();
	Super::Deinitialize();
}

void URbUiSubsystem::OnWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	if (World && World == StackWorld.Get())
	{
		ClearStack(false); // the map is going away (travel, end of PIE): its menus go with it
		URbAudioSubsystem* Audio = bPausedByMenu ? URbAudioSubsystem::Get(World) : nullptr;
		if (Audio)
		{
			Audio->SetPausedMix(false); // the pause mix follows the menu's pause, also when the map ends under it
		}
		bPausedByMenu = false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------------------------------------------------------

TSharedRef<FRbUiHost> URbUiSubsystem::GetHost()
{
	if (!Host.IsValid())
	{
		Host = MakeShared<FRbUiHost>();
		TWeakObjectPtr<URbUiSubsystem> Weak(this);
		Host->OpenScreen = [Weak](ERbUiScreen Screen) { if (URbUiSubsystem* Self = Weak.Get()) { Self->OpenScreen(Screen); } };
		Host->PushScreen = [Weak](TSharedRef<SRbScreen> Screen) { if (URbUiSubsystem* Self = Weak.Get()) { Self->PushScreen(Screen); } };
		Host->CloseTop = [Weak]() { if (URbUiSubsystem* Self = Weak.Get()) { Self->CloseTopScreen(); } };
		Host->Resume = [Weak]() { if (URbUiSubsystem* Self = Weak.Get()) { Self->CloseAll(); } };
		Host->QuitToTitle = [Weak]() { if (URbUiSubsystem* Self = Weak.Get()) { Self->QuitToTitle(); } };
		Host->QuitToDesktop = [Weak]() { if (URbUiSubsystem* Self = Weak.Get()) { Self->QuitToDesktop(); } };
		Host->StartVenue = [Weak](ERbVenue Venue, ERbMatchMode Mode) { URbUiSubsystem* Self = Weak.Get(); return Self && Self->StartVenue(Venue, Mode); };
		Host->IsVenueAvailable = [](ERbVenue Venue, FText& Reason) { return URbUiSubsystem::IsVenueAvailable(Venue, Reason); };
		Host->CanQuitToTitle = [](FText& Reason) { return URbUiSubsystem::CanQuitToTitle(Reason); };
		Host->GetSettings = []() { return URbGameUserSettings::Get(); };
		Host->SaveSettings = []() { if (URbGameUserSettings* S = URbGameUserSettings::Get()) { S->SaveSettings(); } };
		Host->GetDirector = [Weak]() -> const URbMatchDirector*
		{
			const URbUiSubsystem* Self = Weak.Get();
			return Self ? FindPlayerDirector(Self->GetPlayerWorld()) : nullptr;
		};
		Host->SetFocus = [Weak](const TSharedPtr<SWidget>& Widget) { if (URbUiSubsystem* Self = Weak.Get()) { Self->SetUserFocus(Widget); } };
	}
	const URbGameUserSettings* Settings = URbGameUserSettings::Get();
	Host->bReducedMotion = Settings && Settings->bReducedMotion;
	// The clean first seconds belong to the launch (ui-ux 6.3 "on every launch"): the title the player returns to from a venue
	// (Quit to title) shows its list at once. The local player - and this subsystem - live as long as the game instance.
	Host->bSkipIntro = bSkipIntro || bTitleShown || !GetDevScreen().IsEmpty();
	return Host.ToSharedRef();
}

// ---------------------------------------------------------------------------------------------------------------------------
// Stack
// ---------------------------------------------------------------------------------------------------------------------------

APlayerController* URbUiSubsystem::GetController() const
{
	const ULocalPlayer* Player = GetLocalPlayer();
	return Player ? Player->GetPlayerController(Player->GetWorld()) : nullptr;
}

UWorld* URbUiSubsystem::GetPlayerWorld() const
{
	const ULocalPlayer* Player = GetLocalPlayer();
	return Player ? Player->GetWorld() : nullptr;
}

UGameViewportClient* URbUiSubsystem::GetViewport() const
{
	const ULocalPlayer* Player = GetLocalPlayer();
	return Player ? Player->ViewportClient.Get() : nullptr;
}

ERbUiScreen URbUiSubsystem::GetTopScreen() const
{
	return Stack.Num() > 0 ? Stack.Last().Id : ERbUiScreen::None;
}

TSharedPtr<SRbScreen> URbUiSubsystem::GetTopWidget() const
{
	return Stack.Num() > 0 ? Stack.Last().Widget : nullptr;
}

TArray<ERbUiScreen> URbUiSubsystem::GetStackIds() const
{
	TArray<ERbUiScreen> Ids;
	for (const FEntry& Entry : Stack)
	{
		Ids.Add(Entry.Id);
	}
	return Ids;
}

void URbUiSubsystem::AddToViewport(const TSharedRef<SRbScreen>& Screen, int32 ZOrder)
{
	UGameViewportClient* Viewport = GetViewport();
	ULocalPlayer* Player = GetLocalPlayer();
	if (Viewport && Player && FApp::CanEverRender())
	{
		Viewport->AddViewportWidgetForPlayer(Player, Screen, ZOrder);
	}
}

void URbUiSubsystem::RemoveFromViewport(const TSharedRef<SRbScreen>& Screen)
{
	UGameViewportClient* Viewport = GetViewport();
	ULocalPlayer* Player = GetLocalPlayer();
	if (Viewport && Player)
	{
		Viewport->RemoveViewportWidgetForPlayer(Player, Screen);
	}
}

void URbUiSubsystem::PushScreen(const TSharedRef<SRbScreen>& Screen)
{
	UWorld* World = GetPlayerWorld();
	if (Stack.Num() > 0 && StackWorld.Get() != World)
	{
		ClearStack(false); // a stale stack of a previous map
	}
	StackWorld = World;
	if (Stack.Num() > 0)
	{
		const TSharedPtr<SRbScreen> Previous = Stack.Last().Widget;
		Previous->OnDeactivated();
		// A dialog keeps the screen below visible (it is modal over it); a new screen hides it.
		Previous->SetVisibility(Screen->IsDialog() ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
	Stack.Add({Screen->GetScreenId(), Screen});
	AddToViewport(Screen, (Screen->IsDialog() ? RbUiPrivate::DialogLayer : RbUiPrivate::ScreenLayer) + Stack.Num());
	Screen->SetVisibility(EVisibility::Visible);

	APlayerController* PC = GetController();
	if (Screen->PausesGame() && !bPausedByMenu && PC && World && !World->IsPaused())
	{
		bPausedByMenu = PC->SetPause(true);
		if (bPausedByMenu)
		{
			SetAudioPauseMix(true);
		}
	}
	ApplyMenuInput();
	Screen->OnActivated();
}

bool URbUiSubsystem::OpenScreen(ERbUiScreen Screen)
{
	const TSharedRef<FRbUiHost> H = GetHost();
	switch (Screen)
	{
	case ERbUiScreen::Title:
		ClearStack(false);
		PushScreen(SNew(SRbTitleScreen).Host(H));
		bTitleShown = true; // later titles of this run skip the clean first seconds (GetHost)
		return true;
	case ERbUiScreen::Pause:
	{
		if (Stack.Num() > 0 || !GetController() || !GetPlayerWorld())
		{
			return false; // only from live play (the title has its own root)
		}
		PushScreen(SNew(SRbPauseMenu).Host(H));
		return true;
	}
	case ERbUiScreen::Settings:
		return OpenSettings(ERbSettingsPage::Graphics);
	case ERbUiScreen::Dialog:
	case ERbUiScreen::None:
		break;
	}
	return false;
}

bool URbUiSubsystem::OpenSettings(ERbSettingsPage Page)
{
	PushScreen(SNew(SRbSettingsMenu).Host(GetHost()).InitialPage(Page));
	return true;
}

void URbUiSubsystem::TogglePauseMenu()
{
	if (Stack.Num() == 0)
	{
		OpenScreen(ERbUiScreen::Pause);
		return;
	}
	Stack.Last().Widget->HandleBack();
}

void URbUiSubsystem::PopInternal()
{
	if (Stack.Num() == 0)
	{
		return;
	}
	const FEntry Top = Stack.Pop();
	Top.Widget->OnDeactivated();
	RemoveFromViewport(Top.Widget.ToSharedRef());
}

void URbUiSubsystem::CloseTopScreen()
{
	if (Stack.Num() == 0)
	{
		return;
	}
	PopInternal();
	if (Stack.Num() == 0)
	{
		RestoreGame();
		return;
	}
	const TSharedPtr<SRbScreen> Top = Stack.Last().Widget;
	Top->SetVisibility(EVisibility::Visible);
	ApplyMenuInput();
	Top->OnActivated();
}

void URbUiSubsystem::CloseAll()
{
	if (Stack.Num() == 0)
	{
		return;
	}
	ClearStack(true);
}

void URbUiSubsystem::ClearStack(bool bRestoreGame)
{
	while (Stack.Num() > 0)
	{
		PopInternal();
	}
	if (bRestoreGame)
	{
		RestoreGame();
	}
}

void URbUiSubsystem::RestoreGame()
{
	APlayerController* PC = GetController();
	if (!PC)
	{
		bPausedByMenu = false;
		return;
	}
	if (bPausedByMenu)
	{
		PC->SetPause(false);
		bPausedByMenu = false;
		SetAudioPauseMix(false);
	}
	// First-person play: captured, hidden mouse (the stroke's raw-input thread relies on UE's high-precision mouse mode).
	FInputModeGameOnly Mode;
	Mode.SetConsumeCaptureMouseDown(true);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(false);
	PC->FlushPressedKeys();
}

void URbUiSubsystem::SetAudioPauseMix(bool bPaused) const
{
	// audio.md 7.3: the pause menu's mix (world low-pass, -12 dB) follows the menu's world pause (M2-C's public API).
	if (URbAudioSubsystem* Audio = URbAudioSubsystem::Get(GetPlayerWorld()))
	{
		Audio->SetPausedMix(bPaused);
	}
}

void URbUiSubsystem::ApplyMenuInput()
{
	APlayerController* PC = GetController();
	const TSharedPtr<SRbScreen> Top = GetTopWidget();
	if (!PC || !Top.IsValid())
	{
		return;
	}
	FInputModeUIOnly Mode;
	const TSharedPtr<SWidget> Focus = Top->GetFocusedWidget();
	Mode.SetWidgetToFocus(Focus.IsValid() ? Focus : TSharedPtr<SWidget>(Top));
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	PC->FlushPressedKeys();
}

void URbUiSubsystem::SetUserFocus(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	ULocalPlayer* Player = GetLocalPlayer();
	const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
	if (User.IsValid())
	{
		User->SetFocus(Widget.ToSharedRef(), EFocusCause::Navigation);
	}
}

bool URbUiSubsystem::HandleMenuKey(FName KeyName)
{
	const TSharedPtr<SRbScreen> Top = GetTopWidget();
	return Top.IsValid() && Top->HandleKey(FKey(KeyName), FModifierKeysState());
}

TSharedPtr<SWidget> URbUiSubsystem::GetSlateFocusedWidget() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}
	const ULocalPlayer* Player = GetLocalPlayer();
	const TSharedPtr<const FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
	return User.IsValid() ? User->GetFocusedWidget() : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------------------------------------

bool URbUiSubsystem::StartVenue(ERbVenue Venue, ERbMatchMode Mode)
{
	FText Reason;
	UWorld* World = GetPlayerWorld();
	if (!World || !IsVenueAvailable(Venue, Reason))
	{
		return false;
	}
	UE_LOG(LogRawBreak, Display, TEXT("RbUi: travelling to %s"), *ARbTitleGameMode::MakeVenueUrl(Venue, Mode));
	return ARbTitleGameMode::TravelToVenue(World, Venue, Mode);
}

void URbUiSubsystem::QuitToTitle()
{
	UWorld* World = GetPlayerWorld();
	FText Reason;
	if (!World || !CanQuitToTitle(Reason))
	{
		return;
	}
	if (URbGameUserSettings* Settings = URbGameUserSettings::Get())
	{
		Settings->SaveSettings();
	}
	ClearStack(true);
	UE_LOG(LogRawBreak, Display, TEXT("RbUi: quitting to the title"));
	UGameplayStatics::OpenLevel(World, FName(RbAssetPaths::TitleMap));
}

void URbUiSubsystem::QuitToDesktop()
{
	if (URbGameUserSettings* Settings = URbGameUserSettings::Get())
	{
		Settings->SaveSettings();
	}
	UWorld* World = GetPlayerWorld();
	UKismetSystemLibrary::QuitGame(World, GetController(), EQuitPreference::Quit, false);
}

// ---------------------------------------------------------------------------------------------------------------------------
// Tick: focus safety net, dev switch
// ---------------------------------------------------------------------------------------------------------------------------

bool URbUiSubsystem::TickUi(float DeltaTime)
{
	if (Stack.Num() > 0 && FSlateApplication::IsInitialized())
	{
		// A menu keeps the keyboard: when nothing (or the bare game viewport) holds the player's focus, give it back to the
		// focused item (a click that landed outside every widget, a window switch).
		ULocalPlayer* Player = GetLocalPlayer();
		const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
		UGameViewportClient* Viewport = GetViewport();
		if (User.IsValid())
		{
			const TSharedPtr<SWidget> Focused = User->GetFocusedWidget();
			const TSharedPtr<SWidget> ViewportWidget = Viewport ? StaticCastSharedPtr<SWidget>(Viewport->GetGameViewportWidget()) : nullptr;
			if (!Focused.IsValid() || Focused == ViewportWidget)
			{
				const TSharedPtr<SRbScreen> Top = GetTopWidget();
				SetUserFocus(Top->GetFocusedWidget().IsValid() ? Top->GetFocusedWidget() : TSharedPtr<SWidget>(Top));
			}
		}
	}
	TickDevScreen();
	return true;
}

void URbUiSubsystem::TickDevScreen()
{
	const FString& Dev = GetDevScreen();
	if (Dev.IsEmpty() || bDevApplied)
	{
		return;
	}
	UGameViewportClient* Viewport = GetViewport();
	if (Viewport && !DrawHookViewport.IsValid())
	{
		DrawHookViewport = Viewport;
		BeginDrawHandle = Viewport->OnBeginDraw().AddUObject(this, &URbUiSubsystem::OnViewportBeginDraw);
	}
	UWorld* World = GetPlayerWorld();
	APlayerController* PC = GetController();
	if (!World || !World->HasBegunPlay() || !PC)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (RbUiPrivate::RemainingShaderJobs() > 0)
	{
		DevIdleSince = -1.0;
		return;
	}
	if (DevIdleSince < 0.0)
	{
		DevIdleSince = Now;
	}
	float Delay = 2.0f;
	FParse::Value(FCommandLine::Get(), TEXT("RbUiScreenDelay="), Delay);
	if (Now - DevIdleSince < Delay)
	{
		return;
	}
	bDevApplied = true;
	UE_LOG(LogRawBreak, Display, TEXT("RbUi: dev screen %s"), *Dev);
	if (Dev.Equals(TEXT("Pause"), ESearchCase::IgnoreCase))
	{
		OpenScreen(ERbUiScreen::Pause);
	}
	else if (Dev.StartsWith(TEXT("Settings"), ESearchCase::IgnoreCase))
	{
		FString PageName;
		ERbSettingsPage Page = ERbSettingsPage::Graphics;
		if (Dev.Split(TEXT("."), nullptr, &PageName))
		{
			RbUiPrivate::ParsePage(PageName, Page);
		}
		if (Stack.Num() == 0)
		{
			OpenScreen(ERbUiScreen::Pause);
		}
		OpenSettings(Page);
	}
	else if (Dev.Equals(TEXT("KeyHints.Down"), ESearchCase::IgnoreCase))
	{
		// The player gets down on the shot (the RMB handler), so the capture shows the hints of the address.
		if (ARbPlayerCharacter* Character = Cast<ARbPlayerCharacter>(PC->GetPawn()))
		{
			Character->HandleGetDown();
		}
	}
	else if (Dev.Equals(TEXT("Title.Play"), ESearchCase::IgnoreCase))
	{
		if (const TSharedPtr<SRbScreen> Top = GetTopWidget())
		{
			if (Top->GetScreenId() == ERbUiScreen::Title)
			{
				Top->ActivateFocused(); // Play
			}
		}
	}
}

void URbUiSubsystem::OnViewportBeginDraw()
{
	// Dev captures: the headless capture requests its screenshot without UI; the menu screenshots need the UI in it.
	if (!GetDevScreen().IsEmpty() && FScreenshotRequest::IsScreenshotRequested() && !FScreenshotRequest::ShouldShowUI())
	{
		FScreenshotRequest::RequestScreenshot(true);
	}
}

#undef LOCTEXT_NAMESPACE
