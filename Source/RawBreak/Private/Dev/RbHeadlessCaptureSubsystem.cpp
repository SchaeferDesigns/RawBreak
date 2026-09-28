#include "Dev/RbHeadlessCaptureSubsystem.h"

#include "RawBreak.h"

#include "Camera/CameraActor.h"
#include "ContentStreaming.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

// Owner: UE-0 (implemented; pipeline infrastructure).

bool URbHeadlessCaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Path;
	return FParse::Value(FCommandLine::Get(), TEXT("RBCapture="), Path) && !Path.IsEmpty();
}

void URbHeadlessCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("RBCapture="), OutputPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);
	FParse::Value(Cmd, TEXT("RBCaptureCamera="), CameraName);
	FParse::Value(Cmd, TEXT("RBCaptureWarmup="), WarmupFrames);
	FParse::Value(Cmd, TEXT("RBCaptureWarmupSeconds="), WarmupSeconds);
	FParse::Value(Cmd, TEXT("RBCaptureTimeout="), TimeoutSeconds);
	bQuitWhenDone = !FParse::Param(Cmd, TEXT("RBCaptureNoQuit"));
	WarmupFrames = FMath::Max(1, WarmupFrames);
	WarmupSeconds = FMath::Max(0.0, WarmupSeconds);
	StartTime = FPlatformTime::Seconds();
	LastProgressLog = StartTime;
	bArmed = true;
	UE_LOG(LogRawBreak, Display, TEXT("RbCapture: armed -> %s (camera '%s', warmup %d frames / %.1f s)"), *OutputPath, *CameraName, WarmupFrames,
		WarmupSeconds);
}

void URbHeadlessCaptureSubsystem::Deinitialize()
{
	bArmed = false;
	if (ScreenshotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
		ScreenshotHandle.Reset();
	}
	Super::Deinitialize();
}

TStatId URbHeadlessCaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URbHeadlessCaptureSubsystem, STATGROUP_Tickables);
}

int32 URbHeadlessCaptureSubsystem::RemainingCompileJobs() const
{
	int32 Remaining = 0;
#if WITH_EDITOR
	if (GShaderCompilingManager)
	{
		Remaining += GShaderCompilingManager->GetNumRemainingJobs();
	}
	Remaining += FAssetCompilingManager::Get().GetNumRemainingAssets();
#endif
	return Remaining;
}

bool URbHeadlessCaptureSubsystem::ApplyCamera(UWorld* World)
{
	if (CameraName.IsEmpty())
	{
		return true;
	}
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;
	if (!PC)
	{
		return false;
	}
	const FName Tag(*CameraName);
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		ACameraActor* Camera = *It;
		bool bMatch = Camera->Tags.Contains(Tag) || Camera->GetName() == CameraName;
#if WITH_EDITOR
		bMatch = bMatch || Camera->GetActorLabel() == CameraName;
#endif
		if (bMatch)
		{
			// Look-dev views never show the (body-less) player pawn or its shadow.
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->SetActorHiddenInGame(true);
			}
			PC->bAutoManageActiveCameraTarget = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
			if (!bCameraApplied)
			{
				UE_LOG(LogRawBreak, Display, TEXT("RbCapture: viewing through %s"), *Camera->GetName());
				bCameraApplied = true;
			}
			return true;
		}
	}
	if (!bCameraApplied)
	{
		UE_LOG(LogRawBreak, Warning, TEXT("RbCapture: no CameraActor with tag or name '%s' - using the player view"), *CameraName);
		bCameraApplied = true;
	}
	return true;
}

void URbHeadlessCaptureSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartTime > TimeoutSeconds)
	{
		Finish(false, FString::Printf(TEXT("timeout after %.0f s in stage %d"), Now - StartTime, static_cast<int32>(Stage)));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || !World->HasBegunPlay() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	++FramesInStage;

	switch (Stage)
	{
	case EStage::WaitWorld:
		if (ApplyCamera(World))
		{
			Stage = EStage::WaitCompile;
			FramesInStage = 0;
		}
		break;

	case EStage::WaitCompile:
	{
		ApplyCamera(World);
		const int32 Remaining = RemainingCompileJobs();
		if (Remaining > 0)
		{
			FramesInStage = 0;
			if (Now - LastProgressLog > 10.0)
			{
				UE_LOG(LogRawBreak, Display, TEXT("RbCapture: waiting for %d shader / asset compile jobs"), Remaining);
				LastProgressLog = Now;
			}
		}
		else if (FramesInStage >= 3)
		{
			IStreamingManager::Get().StreamAllResources(5.0f);
			Stage = EStage::Warmup;
			FramesInStage = 0;
			UE_LOG(LogRawBreak, Display, TEXT("RbCapture: compilers idle after %.1f s, warming up %d frames"), Now - StartTime, WarmupFrames);
		}
		break;
	}

	case EStage::Warmup:
		ApplyCamera(World);
		WarmupElapsed += DeltaTime;
		if (RemainingCompileJobs() > 0)
		{
			Stage = EStage::WaitCompile; // something new started compiling (streamed-in material)
			FramesInStage = 0;
		}
		else if (FramesInStage >= WarmupFrames && WarmupElapsed >= WarmupSeconds)
		{
			ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &URbHeadlessCaptureSubsystem::OnScreenshot);
			FScreenshotRequest::RequestScreenshot(false);
			Stage = EStage::Requested;
			FramesInStage = 0;
		}
		break;

	case EStage::Requested:
		if (FramesInStage > 120)
		{
			Finish(false, TEXT("screenshot request was not processed within 120 frames"));
		}
		break;

	case EStage::Done:
		break;
	}
}

void URbHeadlessCaptureSubsystem::OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	ScreenshotHandle.Reset();
	if (Colors.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		Finish(false, TEXT("empty screenshot"));
		return;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	const FImageView Image(Colors.GetData(), Width, Height);
	if (!FImageUtils::SaveImageByExtension(*OutputPath, Image))
	{
		Finish(false, FString::Printf(TEXT("could not write %s"), *OutputPath));
		return;
	}
	Finish(true, FString::Printf(TEXT("%dx%d -> %s"), Width, Height, *OutputPath));
}

void URbHeadlessCaptureSubsystem::Finish(bool bSuccess, const FString& Message)
{
	Stage = EStage::Done;
	if (bSuccess)
	{
		UE_LOG(LogRawBreak, Display, TEXT("RbCapture: OK %s (%.1f s)"), *Message, FPlatformTime::Seconds() - StartTime);
	}
	else
	{
		UE_LOG(LogRawBreak, Error, TEXT("RbCapture: FAILED %s"), *Message);
	}
	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExit(false, TEXT("RbHeadlessCapture"));
	}
}
