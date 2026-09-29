#include "Dev/RbHeadlessCaptureSubsystem.h"

#include "RawBreak.h"

#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/RbCameraRigComponent.h"
#include "ContentStreaming.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderTimer.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

// Owner: UE-0 / M2-0 (implemented; pipeline infrastructure).

// Reads the adapted exposure of the game view back on the game thread (the renderer reads its eye-adaptation buffer back every
// frame for pre-exposure; SetupView runs on the game thread in ULocalPlayer::CalcSceneView after the final post-process settings
// are resolved, so the effective exposure bias is known there too). Scene captures, reflection captures and planar reflections
// are ignored.
class FRbCaptureExposureProbe : public FWorldSceneViewExtension
{
public:
	FRbCaptureExposureProbe(const FAutoRegister& AutoRegister, UWorld* InWorld)
		: FWorldSceneViewExtension(AutoRegister, InWorld)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
		if (!IsInGameThread() || !InView.bIsGameView || InView.bIsSceneCapture || InView.bIsReflectionCapture || InView.bIsPlanarReflection)
		{
			return;
		}
		const float Exposure = InView.GetLastEyeAdaptationExposure();
		if (Exposure > 0.0f && FMath::IsFinite(Exposure))
		{
			LastExposure = Exposure;
			LastBias = InView.FinalPostProcessSettings.AutoExposureBias;
		}
	}

	float LastExposure = 0.0f;
	float LastBias = 0.0f;
};

namespace
{
	// Frames after the world began play before a named capture camera that was not found fails the run (level cameras exist at
	// BeginPlay; the grace covers sublevels that stream in during the first frames).
	constexpr int32 kCameraGraceFrames = 60;
	// A camera switch snaps the eye adaptation to the new view: the cut flag is raised on the switch frame and the next frames,
	// because the new view target reaches the camera cache only at the next camera update.
	constexpr int32 kCameraCutFrames = 3;

	FString JsonNumber(double Value)
	{
		return FMath::IsFinite(Value) ? FString::Printf(TEXT("%.4f"), Value) : TEXT("null");
	}

	FString JsonString(const FString& Value)
	{
		FString Out = Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
		return TEXT("\"") + Out + TEXT("\"");
	}

	FString JsonStats(const FRbPerfSeriesStats& S)
	{
		return FString::Printf(TEXT("{\"count\": %d, \"mean\": %s, \"median\": %s, \"p95\": %s, \"p99\": %s, \"min\": %s, \"max\": %s}"), S.Count,
			*JsonNumber(S.Mean), *JsonNumber(S.Median), *JsonNumber(S.P95), *JsonNumber(S.P99), *JsonNumber(S.Min), *JsonNumber(S.Max));
	}

	FString JsonSamples(const TArray<double>& Samples)
	{
		TArray<FString> Parts;
		Parts.Reserve(Samples.Num());
		for (const double Value : Samples)
		{
			Parts.Add(FString::Printf(TEXT("%.3f"), Value));
		}
		return TEXT("[") + FString::Join(Parts, TEXT(", ")) + TEXT("]");
	}
}

// --- pure helpers ------------------------------------------------------------------------------------------------------------

TArray<FString> URbHeadlessCaptureSubsystem::ParseList(const FString& List, const TCHAR* Delimiters)
{
	TArray<FString> Out;
	FString Current;
	auto Flush = [&Out, &Current]()
	{
		Current.TrimStartAndEndInline();
		if (!Current.IsEmpty())
		{
			Out.Add(Current);
		}
		Current.Reset();
	};
	for (const TCHAR Ch : List)
	{
		if (FCString::Strchr(Delimiters, Ch) != nullptr)
		{
			Flush();
		}
		else if (Ch != TEXT('"'))
		{
			Current.AppendChar(Ch);
		}
	}
	Flush();
	return Out;
}

FString URbHeadlessCaptureSubsystem::ResolveOutputPath(const FString& Pattern, const FString& Camera, int32 NumCameras)
{
	FString Safe = Camera.IsEmpty() ? FString(TEXT("player")) : Camera;
	for (TCHAR& Ch : Safe)
	{
		if (FCString::Strchr(TEXT("\\/:*?\"<>| "), Ch) != nullptr)
		{
			Ch = TEXT('_');
		}
	}
	if (Pattern.Contains(TEXT("{camera}")))
	{
		return Pattern.Replace(TEXT("{camera}"), *Safe);
	}
	if (NumCameras <= 1)
	{
		return Pattern;
	}
	const FString Dir = FPaths::GetPath(Pattern);
	const FString Stem = FPaths::GetBaseFilename(Pattern);
	FString Ext = FPaths::GetExtension(Pattern, true);
	if (Ext.IsEmpty())
	{
		Ext = TEXT(".png");
	}
	const FString File = Stem + TEXT("_") + Safe + Ext;
	return Dir.IsEmpty() ? File : FPaths::Combine(Dir, File);
}

FRbPerfSeriesStats URbHeadlessCaptureSubsystem::ComputeStats(TArray<double> Samples)
{
	FRbPerfSeriesStats S;
	S.Count = Samples.Num();
	if (S.Count == 0)
	{
		return S;
	}
	Samples.Sort();
	double Sum = 0.0;
	for (const double Value : Samples)
	{
		Sum += Value;
	}
	S.Mean = Sum / S.Count;
	S.Min = Samples[0];
	S.Max = Samples.Last();
	S.Median = (S.Count % 2 == 1) ? Samples[S.Count / 2] : 0.5 * (Samples[S.Count / 2 - 1] + Samples[S.Count / 2]);
	// Nearest-rank percentile: the smallest sample with at least p % of the samples at or below it.
	auto Percentile = [&Samples](double P)
	{
		const int32 Rank = FMath::Clamp(FMath::CeilToInt(P / 100.0 * Samples.Num()), 1, Samples.Num());
		return Samples[Rank - 1];
	};
	S.P95 = Percentile(95.0);
	S.P99 = Percentile(99.0);
	return S;
}

// --- subsystem ---------------------------------------------------------------------------------------------------------------

bool URbHeadlessCaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Path;
	const TCHAR* Cmd = FCommandLine::Get();
	return (FParse::Value(Cmd, TEXT("RBCapture="), Path, false) && !Path.IsEmpty()) || (FParse::Value(Cmd, TEXT("RBPerf="), Path, false) && !Path.IsEmpty());
}

void URbHeadlessCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();

	FString Pattern;
	bCapture = FParse::Value(Cmd, TEXT("RBCapture="), Pattern, false) && !Pattern.IsEmpty();
	FString CameraList;
	FParse::Value(Cmd, TEXT("RBCaptureCamera="), CameraList, false);
	const TArray<FString> Cameras = ParseList(CameraList);
	if (bCapture)
	{
		Pattern = FPaths::ConvertRelativePathToFull(Pattern);
		if (Cameras.IsEmpty())
		{
			Views.Add({FString(), ResolveOutputPath(Pattern, FString(), 1)});
		}
		for (const FString& Camera : Cameras)
		{
			Views.Add({Camera, ResolveOutputPath(Pattern, Camera, Cameras.Num())});
		}
	}

	FString HideList;
	FParse::Value(Cmd, TEXT("RBCaptureHideTags="), HideList, false);
	for (const FString& Tag : ParseList(HideList))
	{
		HideTags.Add(FName(*Tag));
	}
	bShowUI = FParse::Param(Cmd, TEXT("RBCaptureShowUI"));

	FParse::Value(Cmd, TEXT("RBCaptureWarmup="), WarmupFrames);
	FParse::Value(Cmd, TEXT("RBCaptureWarmupSeconds="), WarmupSeconds);
	FParse::Value(Cmd, TEXT("RBCaptureTimeout="), TimeoutSeconds);
	bQuitWhenDone = !FParse::Param(Cmd, TEXT("RBCaptureNoQuit"));
	WarmupFrames = FMath::Max(1, WarmupFrames);
	WarmupSeconds = FMath::Max(0.0, WarmupSeconds);

	if (FParse::Value(Cmd, TEXT("RBPerf="), PerfPath, false) && !PerfPath.IsEmpty())
	{
		PerfPath = FPaths::ConvertRelativePathToFull(PerfPath);
		FParse::Value(Cmd, TEXT("RBPerfFrames="), PerfFrames);
		PerfFrames = FMath::Max(1, PerfFrames);
		FString ExecList;
		FParse::Value(Cmd, TEXT("RBPerfExec="), ExecList, false);
		PerfExec = ParseList(ExecList, TEXT(";"));
		if (!bCapture)
		{
			// Perf only: record through the (last) named camera, else the player's own view; no screenshot.
			Views.Add({Cameras.IsEmpty() ? FString() : Cameras.Last(), FString()});
		}
	}

	StartTime = FPlatformTime::Seconds();
	LastProgressLog = StartTime;
	bArmed = true;
	for (const FView& View : Views)
	{
		UE_LOG(LogRawBreak, Display, TEXT("RbCapture: armed view '%s' -> %s"), View.Camera.IsEmpty() ? TEXT("player") : *View.Camera,
			View.OutputPath.IsEmpty() ? TEXT("(perf)") : *View.OutputPath);
	}
	UE_LOG(LogRawBreak, Display, TEXT("RbCapture: warmup %d frames / %.1f s per view, UI %s, hidden tags '%s'%s"), WarmupFrames, WarmupSeconds,
		bShowUI ? TEXT("on") : TEXT("off"), *HideList, PerfPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", perf %d frames -> %s"), PerfFrames, *PerfPath));
}

void URbHeadlessCaptureSubsystem::Deinitialize()
{
	bArmed = false;
	if (ScreenshotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
		ScreenshotHandle.Reset();
	}
	ExposureProbe.Reset();
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

ACameraActor* URbHeadlessCaptureSubsystem::FindCamera(UWorld* World, const FString& Name) const
{
	const FName Tag(*Name);
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		ACameraActor* Camera = *It;
		bool bMatch = Camera->Tags.Contains(Tag) || Camera->GetName() == Name;
#if WITH_EDITOR
		bMatch = bMatch || Camera->GetActorLabel() == Name;
#endif
		if (bMatch)
		{
			return Camera;
		}
	}
	return nullptr;
}

void URbHeadlessCaptureSubsystem::ApplyHideTags(UWorld* World)
{
	if (HideTags.IsEmpty())
	{
		return;
	}
	int32 Hidden = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		for (const FName& Tag : HideTags)
		{
			if (Actor->Tags.Contains(Tag))
			{
				if (!Actor->IsHidden())
				{
					Actor->SetActorHiddenInGame(true);
					++Hidden;
				}
				break;
			}
		}
	}
	if (Hidden > 0)
	{
		UE_LOG(LogRawBreak, Display, TEXT("RbCapture: hid %d actor(s) with the tags of -RBCaptureHideTags"), Hidden);
	}
}

bool URbHeadlessCaptureSubsystem::ApplyCamera(UWorld* World)
{
	const FString Name = Views.IsValidIndex(ViewIndex) ? Views[ViewIndex].Camera : FString();
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;
	// The first frames of a view are camera cuts: the eye adaptation snaps to the new view (PostProcessEyeAdaptation: bCameraCut
	// -> ForceTarget) and the temporal history restarts, so a view never inherits the previous view's exposure.
	auto Cut = [this, PC]()
	{
		if (CutFramesLeft > 0 && PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
			--CutFramesLeft;
		}
	};
	if (Name.IsEmpty())
	{
		Cut();
		return true;
	}
	if (!PC)
	{
		return false;
	}
	ACameraActor* Camera = FindCamera(World, Name);
	if (!Camera)
	{
		return false;
	}
	Cut();
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

void URbHeadlessCaptureSubsystem::BeginView(int32 Index)
{
	ViewIndex = Index;
	bCameraApplied = false;
	FramesWithoutCamera = 0;
	FramesInStage = 0;
	IdleFrames = 0;
	CutFramesLeft = kCameraCutFrames;
	WarmupElapsed = 0.0;
	ViewStartTime = FPlatformTime::Seconds();
	Stage = EStage::WaitCompile;
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
		Finish(false, FString::Printf(TEXT("timeout after %.0f s in stage %d (view %d)"), Now - StartTime, static_cast<int32>(Stage), ViewIndex));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || !World->HasBegunPlay() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	++FramesInStage;

	// Every view waits for its camera: a named camera that does not show up within the grace frames fails the run.
	auto CameraReady = [this, World]()
	{
		if (ApplyCamera(World))
		{
			return true;
		}
		if (++FramesWithoutCamera > kCameraGraceFrames)
		{
			Finish(false, FString::Printf(TEXT("no CameraActor with tag, name or label '%s'"), *Views[ViewIndex].Camera));
		}
		return false;
	};

	switch (Stage)
	{
	case EStage::WaitWorld:
		ApplyHideTags(World);
		if (!ExposureProbe.IsValid())
		{
			ExposureProbe = FSceneViewExtensions::NewExtension<FRbCaptureExposureProbe>(World);
		}
		if (Views.IsEmpty())
		{
			Finish(false, TEXT("nothing to do (no -RBCapture / -RBPerf view)"));
			break;
		}
		BeginView(0);
		break;

	case EStage::WaitCompile:
	{
		if (!CameraReady())
		{
			break;
		}
		const int32 Remaining = RemainingCompileJobs();
		if (Remaining > 0)
		{
			IdleFrames = 0;
			if (Now - LastProgressLog > 10.0)
			{
				UE_LOG(LogRawBreak, Display, TEXT("RbCapture: waiting for %d shader / asset compile jobs"), Remaining);
				LastProgressLog = Now;
			}
		}
		else if (++IdleFrames >= 3 && CutFramesLeft == 0)
		{
			ApplyHideTags(World);
			IStreamingManager::Get().StreamAllResources(5.0f);
			Stage = EStage::Warmup;
			FramesInStage = 0;
			WarmupElapsed = 0.0;
			UE_LOG(LogRawBreak, Display, TEXT("RbCapture: compilers idle after %.1f s, warming up view '%s' for %d frames / %.1f s"), Now - StartTime,
				Views[ViewIndex].Camera.IsEmpty() ? TEXT("player") : *Views[ViewIndex].Camera, WarmupFrames, WarmupSeconds);
		}
		break;
	}

	case EStage::Warmup:
		CameraReady();
		WarmupElapsed += DeltaTime;
		if (RemainingCompileJobs() > 0)
		{
			Stage = EStage::WaitCompile; // something new started compiling (streamed-in material)
			FramesInStage = 0;
			IdleFrames = 0;
		}
		else if (FramesInStage >= WarmupFrames && WarmupElapsed >= WarmupSeconds)
		{
			if (!Views[ViewIndex].OutputPath.IsEmpty())
			{
				ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &URbHeadlessCaptureSubsystem::OnScreenshot);
				FScreenshotRequest::RequestScreenshot(bShowUI);
				Stage = EStage::Requested;
				FramesInStage = 0;
			}
			else
			{
				BeginPerf(World);
			}
		}
		break;

	case EStage::Requested:
		CameraReady();
		if (bScreenshotSaved)
		{
			bScreenshotSaved = false;
			if (ViewIndex + 1 < Views.Num())
			{
				BeginView(ViewIndex + 1);
			}
			else if (!PerfPath.IsEmpty())
			{
				BeginPerf(World); // through the last capture view, already warm
			}
			else
			{
				Finish(true, FString::Printf(TEXT("%d view(s)"), Views.Num()));
			}
		}
		else if (FramesInStage > 120)
		{
			Finish(false, TEXT("screenshot request was not processed within 120 frames"));
		}
		break;

	case EStage::PerfRecord:
		CameraReady();
		RecordPerfFrame();
		if (PerfFrameMs.Num() >= PerfFrames)
		{
			FString Error;
			if (WritePerfReport(Error))
			{
				Finish(true, FString::Printf(TEXT("perf %d frames -> %s"), PerfFrameMs.Num(), *PerfPath));
			}
			else
			{
				Finish(false, Error);
			}
		}
		break;

	case EStage::Done:
		break;
	}
}

void URbHeadlessCaptureSubsystem::BeginPerf(UWorld* World)
{
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;
	for (const FString& Command : PerfExec)
	{
		UE_LOG(LogRawBreak, Display, TEXT("RbCapture: perf exec '%s'"), *Command);
		if (PC)
		{
			PC->ConsoleCommand(Command, true);
		}
		else if (GEngine)
		{
			GEngine->Exec(World, *Command);
		}
	}
	for (TArray<double>* Series : {&PerfFrameMs, &PerfGameMs, &PerfRenderMs, &PerfRhiMs, &PerfGpuMs})
	{
		Series->Reset();
		Series->Reserve(PerfFrames);
	}
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		PerfWidth = Size.X;
		PerfHeight = Size.Y;
	}
	LastPerfFrameTime = FPlatformTime::Seconds();
	Stage = EStage::PerfRecord;
	FramesInStage = 0;
	UE_LOG(LogRawBreak, Display, TEXT("RbCapture: recording %d frames at %dx%d"), PerfFrames, PerfWidth, PerfHeight);
}

void URbHeadlessCaptureSubsystem::RecordPerfFrame()
{
	const double Now = FPlatformTime::Seconds();
	PerfFrameMs.Add((Now - LastPerfFrameTime) * 1000.0);
	LastPerfFrameTime = Now;
	// The engine's `stat unit` values of the last drawn frame (set in FViewport::Draw; idle / wait time excluded).
	PerfGameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	PerfRenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	PerfRhiMs.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));
	PerfGpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
}

bool URbHeadlessCaptureSubsystem::WritePerfReport(FString& OutError) const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	int32 Over16 = 0;
	int32 Over33 = 0;
	for (const double Ms : PerfFrameMs)
	{
		Over16 += Ms > 1000.0 / 60.0 ? 1 : 0;
		Over33 += Ms > 1000.0 / 30.0 ? 1 : 0;
	}
	auto CVarString = [](const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetString() : FString(TEXT("?"));
	};
	const FString Camera = Views.IsValidIndex(ViewIndex) ? Views[ViewIndex].Camera : FString();
	TArray<FString> Exec;
	for (const FString& Command : PerfExec)
	{
		Exec.Add(JsonString(Command));
	}

	FString Json = TEXT("{\n");
	Json += FString::Printf(TEXT(" \"map\": %s,\n"), *JsonString(World ? World->GetOutermost()->GetName() : FString()));
	Json += FString::Printf(TEXT(" \"camera\": %s,\n"), *JsonString(Camera.IsEmpty() ? TEXT("player") : Camera));
	Json += FString::Printf(TEXT(" \"width\": %d,\n \"height\": %d,\n"), PerfWidth, PerfHeight);
	Json += FString::Printf(TEXT(" \"screen_percentage\": %s,\n"), *JsonString(CVarString(TEXT("r.ScreenPercentage"))));
	Json += FString::Printf(TEXT(" \"anti_aliasing\": %s,\n"), *JsonString(CVarString(TEXT("r.AntiAliasingMethod"))));
	Json += FString::Printf(TEXT(" \"rhi\": %s,\n \"adapter\": %s,\n"), *JsonString(GDynamicRHI ? GDynamicRHI->GetName() : TEXT("?")), *JsonString(GRHIAdapterName));
	Json += FString::Printf(TEXT(" \"exec\": [%s],\n"), *FString::Join(Exec, TEXT(", ")));
	Json += FString::Printf(TEXT(" \"frames\": %d,\n"), PerfFrameMs.Num());
	Json += FString::Printf(TEXT(" \"frames_over_16_7ms\": %d,\n \"frames_over_33_3ms\": %d,\n"), Over16, Over33);
	Json += FString::Printf(TEXT(" \"frame_ms\": %s,\n"), *JsonStats(ComputeStats(PerfFrameMs)));
	Json += FString::Printf(TEXT(" \"game_ms\": %s,\n"), *JsonStats(ComputeStats(PerfGameMs)));
	Json += FString::Printf(TEXT(" \"render_ms\": %s,\n"), *JsonStats(ComputeStats(PerfRenderMs)));
	Json += FString::Printf(TEXT(" \"rhi_ms\": %s,\n"), *JsonStats(ComputeStats(PerfRhiMs)));
	Json += FString::Printf(TEXT(" \"gpu_ms\": %s,\n"), *JsonStats(ComputeStats(PerfGpuMs)));
	Json += TEXT(" \"samples\": {\n");
	Json += FString::Printf(TEXT("  \"frame_ms\": %s,\n"), *JsonSamples(PerfFrameMs));
	Json += FString::Printf(TEXT("  \"game_ms\": %s,\n"), *JsonSamples(PerfGameMs));
	Json += FString::Printf(TEXT("  \"render_ms\": %s,\n"), *JsonSamples(PerfRenderMs));
	Json += FString::Printf(TEXT("  \"rhi_ms\": %s,\n"), *JsonSamples(PerfRhiMs));
	Json += FString::Printf(TEXT("  \"gpu_ms\": %s\n"), *JsonSamples(PerfGpuMs));
	Json += TEXT(" }\n}\n");

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PerfPath), true);
	if (!FFileHelper::SaveStringToFile(Json, *PerfPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *PerfPath);
		return false;
	}
	const FRbPerfSeriesStats Gpu = ComputeStats(PerfGpuMs);
	const FRbPerfSeriesStats Game = ComputeStats(PerfGameMs);
	const FRbPerfSeriesStats Frame = ComputeStats(PerfFrameMs);
	UE_LOG(LogRawBreak, Display, TEXT("RbCapture: perf frame mean %.2f p95 %.2f max %.2f ms | game mean %.2f p95 %.2f max %.2f ms | gpu mean %.2f p95 %.2f ms"),
		Frame.Mean, Frame.P95, Frame.Max, Game.Mean, Game.P95, Game.Max, Gpu.Mean, Gpu.P95);
	return true;
}

void URbHeadlessCaptureSubsystem::LogExposure(const FString& Camera) const
{
	const TCHAR* Name = Camera.IsEmpty() ? TEXT("player") : *Camera;
	if (!ExposureProbe.IsValid() || ExposureProbe->LastExposure <= 0.0f)
	{
		UE_LOG(LogRawBreak, Display, TEXT("RbCapture: EV100 %s unknown (no eye-adaptation read-back)"), Name);
		return;
	}
	const double Ev100 = URbCameraRigComponent::Ev100FromExposure(ExposureProbe->LastExposure, ExposureProbe->LastBias);
	UE_LOG(LogRawBreak, Display, TEXT("RbCapture: EV100 %s %.2f (exposure %.6g, bias %.2f)"), Name, Ev100, ExposureProbe->LastExposure,
		ExposureProbe->LastBias);
}

void URbHeadlessCaptureSubsystem::OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	ScreenshotHandle.Reset();
	if (Stage != EStage::Requested || !Views.IsValidIndex(ViewIndex))
	{
		return;
	}
	if (Colors.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		Finish(false, TEXT("empty screenshot"));
		return;
	}
	const FString& OutputPath = Views[ViewIndex].OutputPath;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	const FImageView Image(Colors.GetData(), Width, Height);
	if (!FImageUtils::SaveImageByExtension(*OutputPath, Image))
	{
		Finish(false, FString::Printf(TEXT("could not write %s"), *OutputPath));
		return;
	}
	UE_LOG(LogRawBreak, Display, TEXT("RbCapture: saved %dx%d -> %s (view %.1f s)"), Width, Height, *OutputPath, FPlatformTime::Seconds() - ViewStartTime);
	LogExposure(Views[ViewIndex].Camera);
	bScreenshotSaved = true; // the next Tick moves on (never switch view targets inside the viewport's draw)
}

void URbHeadlessCaptureSubsystem::Finish(bool bSuccess, const FString& Message)
{
	if (Stage == EStage::Done)
	{
		return;
	}
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
