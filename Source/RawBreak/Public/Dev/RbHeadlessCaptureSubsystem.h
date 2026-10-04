#pragma once

// Headless screenshots and frame-time recordings for the pipeline (Docs/ue-architecture.md 9.4, M2 additions 18.9). Active only
// when the process was started with -RBCapture=<absolute .png path> and / or -RBPerf=<absolute .json path> (normally through
// Tools/unreal/rbue.py capture / perf):
//
//   UnrealEditor-Cmd.exe RawBreak.uproject <map> -game -RenderOffscreen -ResX=1920 -ResY=1080 -ForceRes
//       -RBCapture=C:/.../shot.png [-RBCaptureCamera=<tag or name>[,<tag or name>...]] [-RBCaptureWarmup=<frames>]
//       [-RBCaptureWarmupSeconds=<game seconds>] [-RBCaptureTimeout=<seconds>] [-RBCaptureNoQuit]
//       [-RBCaptureShowUI] [-RBCaptureHideTags=<tag>[,<tag>...]]
//       [-RBPerf=C:/.../perf.json [-RBPerfFrames=<n>] [-RBPerfExec="<console command>[;<console command>...]"]]
//
// Sequence: wait for the world to begin play -> hide the actors carrying one of -RBCaptureHideTags (e.g. RbDB_Ceiling for the
// dive bar's plan view V10) -> for every camera of -RBCaptureCamera (or once through the player's own view when none is given):
// view through the camera actor whose tag, name or label matches (labels exist in editor builds only: a packaged build, --exe,
// finds cameras by tag or object name; the player pawn is hidden then; a camera cut snaps the eye
// adaptation to the new view, so every view starts from its own exposure) -> wait until the shader and asset compilers are idle ->
// a second camera cut (the exposure snaps to the image with the final shaders, not to a default-material frame of a cold DDC) ->
// render at least Warmup frames AND WarmupSeconds of game time (auto exposure adapts in EV per SECOND - 0.7 EV/s down for the
// Eyes preset - so a frame count alone under-converges on a fast GPU; Lumen, TSR history, virtual texture / Nanite streaming) ->
// one screenshot via UGameViewportClient's capture delegate (scene only, or with the Slate UI of the viewport - overlay, menus,
// key hints - under -RBCaptureShowUI) -> PNG -> the log line "RbCapture: EV100 <camera> <value>" with the adapted exposure of
// that view (scene EV100 = exposure bias - log2(LuminanceMax x exposure), the camera rig's formula; venue-dive-bar VDB-T2).
// Several cameras in one process: -RBCapture may contain the token {camera} (replaced by the camera's tag / name); without it,
// the files are named <stem>_<camera>.png. A named camera that does not exist fails the run (no silent player-view fallback).
//
// -RBPerf (M2-A9 performance log): after the captures (or after its own warm-up when there are none) the console commands of
// -RBPerfExec run once (e.g. "rb.Match.Break 9" so the break plays during the recording), then PerfFrames frames are recorded:
// frame time, game thread, render thread, RHI thread (cycles of FViewport::Draw, like `stat unit`) and GPU
// (RHIGetGPUFrameCycles) -> a JSON with mean / median / p95 / p99 / max per series, hitch counts, the raw samples and the
// scalability groups (sg.*) in effect at the end of the recording. The view is the last capture camera, else the player's.
//
// On timeout or any failure it logs an error and exits with the log line "RbCapture: FAILED". -ForceRes is required: without it
// the engine clamps a windowed resolution to the desktop work area (1920x1080 on a 1080p monitor becomes a smaller size).
// Owner: UE-0 / M2-0 (architect; implemented).

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "RbHeadlessCaptureSubsystem.generated.h"

class ACameraActor;
class FRbCaptureExposureProbe;

// Summary statistics of one recorded series (milliseconds), see URbHeadlessCaptureSubsystem::ComputeStats.
struct FRbPerfSeriesStats
{
	int32 Count = 0;
	double Mean = 0.0;
	double Median = 0.0;
	double P95 = 0.0;
	double P99 = 0.0;
	double Min = 0.0;
	double Max = 0.0;
};

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

	// --- pure helpers (unit-tested: RawBreak.Unit.Pipeline.*) ---------------------------------------------------------------
	// Splits a comma / semicolon separated list, trims every entry, drops empty ones (order kept, duplicates kept).
	static TArray<FString> ParseList(const FString& List, const TCHAR* Delimiters = TEXT(",;"));
	// Output file of one camera: Pattern with {camera} replaced; without the token and with more than one camera
	// <dir>/<stem>_<camera>.<ext>; otherwise Pattern unchanged. Characters that are invalid in file names become '_'.
	static FString ResolveOutputPath(const FString& Pattern, const FString& Camera, int32 NumCameras);
	// Mean / median / nearest-rank percentiles / min / max of Samples (empty -> all zero).
	static FRbPerfSeriesStats ComputeStats(TArray<double> Samples);

private:
	enum class EStage : uint8
	{
		WaitWorld,
		WaitCompile,
		Warmup,
		Requested,
		PerfRecord,
		Done,
	};

	// The views of the run: one entry per capture camera ("" = the player's own view).
	struct FView
	{
		FString Camera;
		FString OutputPath;
	};

	bool ApplyCamera(UWorld* World);
	ACameraActor* FindCamera(UWorld* World, const FString& Name) const;
	void ApplyHideTags(UWorld* World);
	int32 RemainingCompileJobs() const;
	void BeginView(int32 Index);
	void BeginPerf(UWorld* World);
	void RecordPerfFrame();
	bool WritePerfReport(FString& OutError) const;
	void LogExposure(const FString& Camera) const;
	void OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors);
	void Finish(bool bSuccess, const FString& Message);

	bool bArmed = false; // set in Initialize (never on the CDO)
	bool bCapture = false;
	bool bShowUI = false;
	TArray<FView> Views;
	TArray<FName> HideTags;
	int32 ViewIndex = 0;
	int32 WarmupFrames = 90;
	double WarmupSeconds = 4.0;    // minimum game time in the Warmup stage (review R-09)
	double WarmupElapsed = 0.0;
	double TimeoutSeconds = 1800.0;
	bool bQuitWhenDone = true;

	// Perf recording (M2-A9).
	FString PerfPath;
	int32 PerfFrames = 600;
	TArray<FString> PerfExec;
	double LastPerfFrameTime = 0.0;
	TArray<double> PerfFrameMs;
	TArray<double> PerfGameMs;
	TArray<double> PerfRenderMs;
	TArray<double> PerfRhiMs;
	TArray<double> PerfGpuMs;
	int32 PerfWidth = 0;
	int32 PerfHeight = 0;

	EStage Stage = EStage::WaitWorld;
	int32 FramesInStage = 0;
	int32 IdleFrames = 0;           // consecutive frames without compile jobs (WaitCompile)
	int32 FramesWithoutCamera = 0;
	int32 CutFramesLeft = 0;        // camera-cut frames still to raise for the current view
	bool bScreenshotSaved = false;  // set by OnScreenshot, consumed by the next Tick
	double StartTime = 0.0;
	double ViewStartTime = 0.0;
	double LastProgressLog = 0.0;
	bool bCameraApplied = false;
	// The current view's camera once found: every later frame (incl. the whole perf recording) reuses it instead of iterating the
	// world's cameras and building name strings per frame (review: no per-frame search / allocation inside the measured window).
	TWeakObjectPtr<ACameraActor> ViewCamera;
	FDelegateHandle ScreenshotHandle;
	TSharedPtr<FRbCaptureExposureProbe, ESPMode::ThreadSafe> ExposureProbe;
};
