#pragma once

// Headless screenshot capture for the pipeline (Docs/ue-architecture.md 9.4). Active only when the process was
// started with -RBCapture=<absolute .png path> (normally through Tools/unreal/rbue.py capture):
//
//   UnrealEditor-Cmd.exe RawBreak.uproject <map> -game -RenderOffscreen -ResX=1920 -ResY=1080 -ForceRes
//       -RBCapture=C:/.../shot.png [-RBCaptureCamera=<tag or name>] [-RBCaptureWarmup=<frames>]
//       [-RBCaptureWarmupSeconds=<game seconds>] [-RBCaptureTimeout=<seconds>] [-RBCaptureNoQuit]
//
// Sequence: wait for the world to begin play -> view through the camera actor whose tag or name matches
// -RBCaptureCamera (optional; the player pawn is hidden then) -> wait until the shader and asset compilers are idle -> render at least
// Warmup frames AND WarmupSeconds of game time (auto exposure adapts in EV per SECOND - 0.7 EV/s down for the Eyes preset - so a
// frame count alone under-converges on a fast GPU; Lumen, TSR history, virtual texture / Nanite streaming) -> one screenshot via
// UGameViewportClient's capture delegate -> PNG at the given path -> request exit. On timeout it logs an error and
// exits with the log line "RbCapture: FAILED". -ForceRes is required: without it the engine clamps a windowed
// resolution to the desktop work area (1920x1080 on a 1080p monitor becomes a smaller "convenient" size).
// Owner: UE-0 (implemented).

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "RbHeadlessCaptureSubsystem.generated.h"

UCLASS()
class RAWBREAK_API URbHeadlessCaptureSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	// Conditional + IsTickable: the class default object is an FTickableGameObject too and must never tick.
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return bArmed; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }

private:
	enum class EStage : uint8
	{
		WaitWorld,
		WaitCompile,
		Warmup,
		Requested,
		Done,
	};

	bool ApplyCamera(UWorld* World);
	int32 RemainingCompileJobs() const;
	void OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors);
	void Finish(bool bSuccess, const FString& Message);

	bool bArmed = false; // set in Initialize (never on the CDO)
	FString OutputPath;
	FString CameraName;
	int32 WarmupFrames = 90;
	double WarmupSeconds = 4.0;    // minimum game time in the Warmup stage (review R-09)
	double WarmupElapsed = 0.0;
	double TimeoutSeconds = 1800.0;
	bool bQuitWhenDone = true;

	EStage Stage = EStage::WaitWorld;
	int32 FramesInStage = 0;
	double StartTime = 0.0;
	double LastProgressLog = 0.0;
	bool bCameraApplied = false;
	FDelegateHandle ScreenshotHandle;
};
