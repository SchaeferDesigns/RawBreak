#pragma once

// Shot replay (Docs/ue-architecture.md 6.7; trailer-enabling tech, memory "Launch trailer"): keeps the last N
// immutable FRbShot records (their SimInput holds the pre-shot table, the Result the exact trajectories), and
// replays one through the ball set's playback component with its own clock (rate, pause, scrub) and a replay camera.
// A replay never changes the match: afterwards the balls return to the director's current FRbTableState. Replays
// use the STORED ShotResult (bitwise the live shot); re-simulating from the stored input is a determinism check,
// not the replay path. Owner: UE-7.
// Guards (review R-07): PlayReplay only when URbMatchDirector::IsReplayAllowed(); the playback component's OnFinished also
// fires for live shots, so OnReplayFinished acts only on ReplayShot while bReplaying (and the director ignores replays).
//
// Details (UE-7):
//  * Start: URbMatchDirector::SetReplayActive(true) (locks the stroke component; the director refuses strokes, placements,
//    declarations and Confirm meanwhile), the ball set shows Request.Input's balls (a jump: motion history dropped), the
//    playback plays the stored shot from StartShotTime with its own clock (bAnchorToContact = false), the cue follows the
//    stored tip path, the local player views through ARbReplayCamera (a cut, broadcast style) and the pawn's camera rig idles
//    (ERbCameraRigMode::External).
//  * End: the replay's OnFinished holds the final frame EndHoldSeconds (world timer), then restores: cue hidden, rig mode and
//    view target back, SetReplayActive(false) (the director shows FRbTableState again and re-arms the stroke component).
//    StopReplay restores at once. Nothing here writes match state; ARbGameMode's director is optional (dev maps without a
//    match: the balls then stay at the replay's end).
//  * R key (HandleReplayInput): the last shot from the Shooter view at 1x; while a replay runs, the same shot again from the
//    next view (Shooter -> Overhead -> Rail -> Follow). Rate 0 freezes the replay at its start time (captures).
//  * The history keeps the newest MaxShots shots (32; a break's compact result is a few hundred KB, RbShot::FootprintBytes).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Engine/TimerHandle.h"
#include "Simulation/RbShot.h"

#include "RbReplaySubsystem.generated.h"

class AActor;
class APlayerController;
class ARbBallSet;
class ARbReplayCamera;
class ARbTable;
class URbMatchDirector;
class URbShotPlaybackComponent;

UENUM(BlueprintType)
enum class ERbReplayView : uint8
{
	Shooter,   // the shooter's eye transform at contact (FRbShotRequest::ShooterView)
	Overhead,  // straight down over the bed centre
	Rail,      // low, behind the head rail, long lens (broadcast)
	Follow,    // tracks the cue ball
};

DECLARE_MULTICAST_DELEGATE(FRbOnReplayChanged);

UCLASS()
class RAWBREAK_API URbReplaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Adds a shot to the history (called by the director when a shot is committed). Oldest dropped beyond MaxShots.
	void RecordShot(const TSharedRef<const FRbShot>& Shot);

	int32 GetShotCount() const { return History.Num(); }
	TSharedPtr<const FRbShot> GetShot(int32 IndexFromLast) const;

	// Plays shot IndexFromLast (0 = last) with View at Rate. False if no shot or a live shot is playing.
	bool PlayReplay(int32 IndexFromLast, ERbReplayView View, float Rate = 1.0f);
	void StopReplay();
	bool IsReplaying() const { return bReplaying; }
	void CycleView();

	int32 MaxShots = 32;

	// --- additions (UE-7) ----------------------------------------------------------------------------------

	// Starts at StartShotTime [s] of the shot (0 = the stroke); with Rate 0 the replay stays frozen there (captures).
	bool PlayReplayFrom(int32 IndexFromLast, ERbReplayView View, float Rate, double StartShotTime);

	// R key: not replaying -> PlayReplay(0, Shooter, 1); replaying -> the same shot again from the next view.
	bool HandleReplayInput();

	ERbReplayView GetView() const { return CurrentView; }
	float GetRate() const { return ReplayRate; }
	// Slow motion / freeze of the running replay (the playback re-anchors, no time jump).
	void SetRate(float Rate);
	// Scrubs the running replay to ShotTime (a jump: motion history dropped).
	void SeekReplay(double ShotTime);
	int32 GetReplayIndex() const { return bReplaying ? ReplayIndex : -1; }
	TSharedPtr<const FRbShot> GetReplayShot() const { return ReplayShot; }
	// The replay's playback ended and its last frame is held (EndHoldSeconds) before the live table returns.
	bool IsHoldingEnd() const { return bHolding; }
	ARbReplayCamera* GetCamera() const { return Camera.Get(); }

	// Seconds the final frame stays on screen before the live table returns (0 = at once).
	double EndHoldSeconds = 1.0;

	// Replay started / view changed / ended (overlay).
	FRbOnReplayChanged OnReplayChanged;

	static const TCHAR* ViewName(ERbReplayView View);

	// UWorldSubsystem
	virtual void Deinitialize() override;

protected:
	void OnReplayFinished(const TSharedRef<const FRbShot>& Shot);

private:
	URbMatchDirector* FindDirector() const;
	ARbBallSet* FindBallSet() const;
	ARbTable* FindTable() const;
	APlayerController* FindLocalController() const;
	ARbReplayCamera* EnsureCamera();
	void BindPlayback(URbShotPlaybackComponent* Playback);
	// Places the replay camera for CurrentView and makes it the local player's view target.
	void ApplyView();
	void EndHold();
	// Back to the live table: cue hidden, view target and rig mode restored, the director re-arms.
	void RestoreLive();

	TArray<TSharedRef<const FRbShot>> History;
	bool bReplaying = false;
	TSharedPtr<const FRbShot> ReplayShot;
	ERbReplayView CurrentView = ERbReplayView::Shooter;
	TWeakObjectPtr<ARbReplayCamera> Camera;
	FDelegateHandle FinishedHandle;

	TWeakObjectPtr<URbShotPlaybackComponent> BoundPlayback;
	TWeakObjectPtr<AActor> SavedViewTarget;
	TWeakObjectPtr<APlayerController> ViewController;
	uint8 SavedRigMode = 0;               // ERbCameraRigMode of the pawn's rig before the replay
	bool bRigModeSaved = false;
	bool bHolding = false;
	int32 ReplayIndex = 0;
	float ReplayRate = 1.0f;
	FTimerHandle HoldTimer;
};
