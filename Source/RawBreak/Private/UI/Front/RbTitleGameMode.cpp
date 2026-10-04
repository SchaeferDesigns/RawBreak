#include "UI/Front/RbTitleGameMode.h"

#include "RawBreak.h"
#include "UI/Core/RbUiSubsystem.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

// Owner: M2-D.

const FName ARbTitleGameMode::TitleCameraTag(TEXT("RbCam_Title"));

ARbTitleGameMode::ARbTitleGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

FString ARbTitleGameMode::MakeVenueUrl(ERbVenue Venue, ERbMatchMode Mode)
{
	return FString::Printf(TEXT("%s?Mode=%s"), RbTypes::MapFor(Venue), Mode == ERbMatchMode::HotSeat ? TEXT("HotSeat") : TEXT("Practice"));
}

bool ARbTitleGameMode::TravelToVenue(UObject* WorldContext, ERbVenue Venue, ERbMatchMode Mode)
{
	if (!WorldContext || !FPackageName::DoesPackageExist(RbTypes::MapFor(Venue)))
	{
		return false;
	}
	UGameplayStatics::OpenLevel(WorldContext, FName(RbTypes::MapFor(Venue)), true,
		FString::Printf(TEXT("Mode=%s"), Mode == ERbMatchMode::HotSeat ? TEXT("HotSeat") : TEXT("Practice")));
	return true;
}

bool ARbTitleGameMode::StartVenue(ERbVenue Venue, ERbMatchMode Mode)
{
	return TravelToVenue(this, Venue, Mode);
}

void ARbTitleGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// No Super: the title has no pawn to restart (RestartPlayer would only log that none can spawn).
	ShowTitle(NewPlayer);
}

void ARbTitleGameMode::StartPlay()
{
	Super::StartPlay();
	if (UWorld* World = GetWorld())
	{
		ShowTitle(World->GetFirstPlayerController());
	}
}

void ARbTitleGameMode::ShowTitle(APlayerController* Player)
{
	if (!Player || !Player->IsLocalController())
	{
		return;
	}
	ACameraActor* Camera = nullptr;
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TitleCameraTag))
		{
			Camera = *It;
			break;
		}
		if (!Camera)
		{
			Camera = *It; // any camera of the level, until the tagged one is found
		}
	}
	if (Camera)
	{
		Player->bAutoManageActiveCameraTarget = false;
		Player->SetViewTarget(Camera);
	}
	URbUiSubsystem* Ui = URbUiSubsystem::Get(Player);
	if (Ui && Ui->GetTopScreen() != ERbUiScreen::Title && !(Ui->IsMenuOpen() && Ui->GetStackIds().Contains(ERbUiScreen::Title)))
	{
		Ui->OpenScreen(ERbUiScreen::Title);
	}
}
