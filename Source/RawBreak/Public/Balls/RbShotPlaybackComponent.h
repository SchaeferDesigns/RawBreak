#pragma once

// Plays a simulated shot on the rendered balls (ue5-realism-plan 5.7, architecture.md 13 item 6,
// Docs/ue-architecture.md 5.5). RENDER TIME IS DECOUPLED FROM PHYSICS TIME: every frame evaluates the exact
// state at the playback clock ShotTime with rb::StateAtCursor (monotone, O(1) per ball; orientation law of
// rb/Physics/Playback.h), so slow motion, pause, scrubbing and replays are free and bitwise consistent.
// Owner: UE-2.
//
// Clock: live shots are anchored at the tip contact, ShotTime = (FPlatformTime::Seconds() - ContactTime) * Rate,
// so a late hand-off never shifts the motion; replays start at StartShotTime with their own rate. The clock is
// NOT affected by global time dilation. Pocketed / off-table balls: the Terminal segment freezes the ball where
// it was captured; the component hides it after DropHideDelay (the pocket-fall animation is a later package).
// The cue follows rb::CueTipAt of strike 0 while the tip path lasts (what the rules judged is what is shown).
// Events: each rb::ShotEvent whose time is passed fires OnShotEvent once, in log order (audio / VFX / chalk later).

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Simulation/RbShot.h"

#include "rb/Physics/Playback.h"

#include "RbShotPlaybackComponent.generated.h"

class ARbBallSet;
class ARbCue;

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnPlaybackFinished, const TSharedRef<const FRbShot>& /*Shot*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnShotEvent, const TSharedRef<const FRbShot>& /*Shot*/, int32 /*EventIndex*/);

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbShotPlaybackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbShotPlaybackComponent();

	// Starts playing Shot. Live: bAnchorToContact = true (clock from Shot->Request.ContactTime). Replay: false,
	// clock starts at StartShotTime now.
	void Play(const TSharedRef<const FRbShot>& Shot, bool bAnchorToContact, double StartShotTime = 0.0, float Rate = 1.0f);

	// Stops; bSnapToEnd shows the final state (Result.Finals) immediately.
	void Stop(bool bSnapToEnd);

	void SetRate(float NewRate);
	void SetPaused(bool bPause);
	void SeekTo(double ShotTime);

	bool IsPlaying() const { return Shot.IsValid(); }
	double GetShotTime() const { return ShotTime; }
	TSharedPtr<const FRbShot> GetShot() const { return Shot; }

	// Optional cue animated from the tip path (set by the game mode).
	void SetCue(ARbCue* InCue);

	// Seconds a captured ball stays visible at its capture point before it is hidden.
	UPROPERTY(EditAnywhere, Category = "RawBreak|Playback")
	float DropHideDelay = 0.15f;

	FRbOnPlaybackFinished OnFinished;
	FRbOnShotEvent OnShotEvent;

	// UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	// Evaluates every ball (and the cue) at ShotTime and pushes the transforms to the ball set.
	void ApplyAt(double Time);

	ARbBallSet* GetBallSet() const;

private:
	TSharedPtr<const FRbShot> Shot;
	rb::PlaybackCursor Cursor;
	double ShotTime = 0.0;
	double ClockOrigin = 0.0;   // FPlatformTime::Seconds() at ShotTime = ClockOriginShotTime
	double ClockOriginShotTime = 0.0;
	float Rate = 1.0f;
	bool bPaused = false;
	int32 NextEventIndex = 0;
	TWeakObjectPtr<ARbCue> Cue;
};
