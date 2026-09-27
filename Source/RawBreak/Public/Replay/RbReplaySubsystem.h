#pragma once

// Shot replay (Docs/ue-architecture.md 6.7; trailer-enabling tech, memory "Launch trailer"): keeps the last N
// immutable FRbShot records (their SimInput holds the pre-shot table, the Result the exact trajectories), and
// replays one through the ball set's playback component with its own clock (rate, pause, scrub) and a replay camera.
// A replay never changes the match: afterwards the balls return to the director's current FRbTableState. Replays
// use the STORED ShotResult (bitwise the live shot); re-simulating from the stored input is a determinism check,
// not the replay path. Owner: UE-7.
// Guards (review R-07): PlayReplay only when URbMatchDirector::IsReplayAllowed(); the playback component's OnFinished also
// fires for live shots, so OnReplayFinished acts only on ReplayShot while bReplaying (and the director ignores replays).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Simulation/RbShot.h"

#include "RbReplaySubsystem.generated.h"

class ARbReplayCamera;

UENUM(BlueprintType)
enum class ERbReplayView : uint8
{
	Shooter,   // the shooter's eye transform at contact (FRbShotRequest::ShooterView)
	Overhead,  // straight down over the bed centre
	Rail,      // low, behind the head rail, long lens (broadcast)
	Follow,    // tracks the cue ball
};

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

protected:
	void OnReplayFinished(const TSharedRef<const FRbShot>& Shot);

private:
	TArray<TSharedRef<const FRbShot>> History;
	bool bReplaying = false;
	TSharedPtr<const FRbShot> ReplayShot;
	ERbReplayView CurrentView = ERbReplayView::Shooter;
	TWeakObjectPtr<ARbReplayCamera> Camera;
	FDelegateHandle FinishedHandle;
};
