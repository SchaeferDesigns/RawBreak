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
// it was captured; the component hides a pocketed ball after DropHideDelay (the pocket-fall animation is a later package),
// a ball that left the table at its hand-off time (M2-E: URbLooseBallSubsystem continues a live shot's ball under engine
// physics; HideTimeOf), and leaves a ball that came to rest on the rail / frame where it is until the finals.
// The cue follows rb::CueTipAt of strike 0 while the tip path lasts (what the rules judged is what is shown).
// Events: each rb::ShotEvent whose time is passed fires OnShotEvent once, in log order (audio / VFX / chalk later).
//
// Details (UE-2):
//  * Clock source: the FRAME clock FApp::GetCurrentTime() (= FPlatformTime::Seconds() sampled once at the start of the
//    frame in normal play, the same time base as FRbShotRequest::ContactTime), so every object of a frame sees one time;
//    with a fixed / custom time step (benchmark runs, Movie Render Queue captures of the trailer) it advances by the
//    step, which makes replays frame-exact. SetClockSource replaces it (tests). ShotTime(now) = Origin + (now - ClockOrigin)
//    * Rate, clamped to [0, FinishTime].
//  * Rate >= 0 (negative rates are clamped to 0; scrub backwards with SeekTo). SetRate / SetPaused / SeekTo re-anchor the
//    clock at the current value, so the shown time never jumps.
//  * A paused WORLD (UWorld::IsPaused: pause command / menu, PIE pause) holds the playback like SetPaused (the component
//    ticks while paused to notice it), and it resumes where it stopped: the real clock running on during a pause never
//    makes the shot jump forward or finish at once. Time dilation still has no effect.
//  * Events fire when the playing clock passes them (Play fires those up to the anchored "now"); a seek does not fire the
//    events it jumps over, and events at or after the seek target fire again as the clock passes them.
//  * FinishTime = Result.StopTime, extended to capture time + DropHideDelay of the last captured ball and to the end of
//    the cue's tip path. The first tick at or after it fires the remaining events, shows Result.Finals exactly (snap:
//    on-table balls at their final position / orientation, captured and unused balls hidden), stops, and broadcasts
//    OnFinished(Shot) - never from inside Play / SeekTo / SetPaused, so a caller can set its own state after Play.
//  * Play makes the cue follow the tip path (ARbCue::SetDrive(Playback)) when the shot has one for strike 0; whoever
//    owns the cue afterwards (director, replay) sets the next drive.
//  * Stop never broadcasts OnFinished (a deliberate stop; the caller knows).
//  * Continuous playback never teleports (ETeleportType::None, motion vectors kept). DISCONTINUOUS re-placements drop the
//    motion history of the balls that jump (ARbBallSet::ResetBallMotion): SeekTo, Stop(true) and a Play whose balls are
//    not shown at the shot's pose at the clock origin (t = 0 live, StartShotTime for a replay: a replay started from another
//    table state or from t0 > 0; a live shot starts where the table shows its balls, so its first frame keeps the motion
//    vectors). No streak across a jump in motion blur / TSR.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Simulation/RbShot.h"

#include "rb/Physics/Playback.h"

#include "RbShotPlaybackComponent.generated.h"

class ARbBallSet;
class ARbCue;

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnPlaybackFinished, const TSharedRef<const FRbShot>& /*Shot*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnShotEvent, const TSharedRef<const FRbShot>& /*Shot*/, int32 /*EventIndex*/);

// M2 audio contract (Docs/ue-architecture.md 18.5, audio.md 5.1-5.2): the mapping from the playback clock to shot time, so
// the audio renderer can schedule every event sample-accurately (and follow slow motion, pause and seeks) without polling.
// While not held: ShotTime(c) = OriginShotTime + (c - OriginClock) * Rate, for clock values c in the component's clock domain
// (ClockNow(): FApp::GetCurrentTime() in normal play = FPlatformTime::Seconds() sampled at the frame start - the same time base
// as FRbShotRequest::ContactTime). Held (paused, world paused): ShotTime = OriginShotTime.
struct FRbPlaybackClock
{
	double OriginClock = 0.0;
	double OriginShotTime = 0.0;
	float Rate = 1.0f;
	bool bHeld = false;
	bool bLive = false; // anchored at the tip contact (a live shot), else a replay
};
DECLARE_MULTICAST_DELEGATE_TwoParams(FRbOnPlaybackClock, const TSharedRef<const FRbShot>& /*Shot*/, const FRbPlaybackClock& /*Clock*/);

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

	// M2 (architect; audio, 18.5): Play broadcasts OnPlaybackStarted after the first evaluation (before the first events fire);
	// SetRate, SetPaused, a world pause / resume and SeekTo broadcast OnPlaybackClockChanged with the new mapping.
	FRbOnPlaybackClock OnPlaybackStarted;
	FRbOnPlaybackClock OnPlaybackClockChanged;
	// The current mapping (valid while IsPlaying()).
	FRbPlaybackClock GetClockMapping() const;

	// UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- additions (UE-2) ----------------------------------------------------------------------------

	float GetRate() const { return Rate; }
	bool IsPaused() const { return bPaused; }
	// Paused by SetPaused or by a paused world (the clock is held).
	bool IsHeld() const { return bPaused || bWorldPaused; }

	// Shot time at which the playback finishes (see the file comment); 0 when not playing.
	double GetFinishTime() const { return FinishTime; }

	// FinishTime of Shot for a given drop-hide delay (pure).
	static double ComputeFinishTime(const FRbShot& Shot, double DropHideDelay);

	// M2-E (balls off the table, Docs/ue-architecture.md 18.6.1): shot time from which the table instance of a ball with this
	// final is hidden. Pocketed: capture + DropHideDelay (the pocket-fall animation is later). Off the table with reason Floor /
	// ExternalObjectRebound: the capture itself = the HAND-OFF time (a live shot continues the ball as an ARbLooseBall from
	// there, a replay shows the ball leaving and hides it at that moment). RestsOnRailOrFrame: never during the playback (the
	// ball stays where the core froze it on the rail / frame; the finals hide it). Balls on the table: never (+inf).
	static double HideTimeOf(const rb::BallFinal& Final, double DropHideDelay);

	// Core state of a ball as last shown (Play / SeekTo / tick): bitwise rb::StateAt / OrientationAt at GetShotTime().
	// False if the ball is not part of the playing shot.
	bool GetBallStateCore(int32 BallId, rb::BallState& OutState, rb::Quat& OutOrientation) const;

	// Index of the next event to fire (== Result.Events.size() when all fired).
	int32 GetNextEventIndex() const { return NextEventIndex; }

	// Clock source in seconds (default FApp::GetCurrentTime()); an empty function restores the default.
	void SetClockSource(TFunction<double()> InClock);
	double ClockNow() const;

	// What a tick does: advance ShotTime to the clock, show it, fire passed events, finish at FinishTime.
	void Advance();

protected:
	// Evaluates every ball (and the cue) at ShotTime and pushes the transforms to the ball set.
	void ApplyAt(double Time);

	ARbBallSet* GetBallSet() const;

private:
	// ShotTime the clock gives now (unclamped).
	double ClockShotTime(double Now) const;
	double ClampShotTime(double Time) const;
	// First event index with Time >= T.
	int32 FirstEventAtOrAfter(double T) const;
	// Fires the events up to T (inclusive); false if a listener stopped / replaced this playback.
	bool FireEventsUpTo(double T);
	// World pause hold (from the tick) and the common freeze / re-anchor of SetPaused and SetWorldPaused.
	void SetWorldPaused(bool bPause);
	void OnHoldChanged(bool bWasHeld);
	// Shows Result.Finals (on-table balls at their final pose, everything else hidden) and the cue at rest.
	void ApplyFinals(const FRbShot& FinalShot);
	void Finish();
	void SetBallShown(ARbBallSet* Balls, int32 BallId, bool bShown);

	// Table-local UE poses of the visible balls (bit mask of the visible ones).
	uint32 CaptureShownPoses(FVector* OutLocations, FQuat* OutRotations) const;
	// Drops the motion history of every ball in VisibleBefore that is still visible and no longer at its captured pose.
	void ResetMovedBalls(uint32 VisibleBefore, const FVector* Locations, const FQuat* Rotations);

	TSharedPtr<const FRbShot> Shot;
	rb::PlaybackCursor Cursor;
	double ShotTime = 0.0;
	double ClockOrigin = 0.0;   // clock value (ClockNow) at ShotTime = ClockOriginShotTime
	double ClockOriginShotTime = 0.0;
	float Rate = 1.0f;
	bool bPaused = false;
	bool bWorldPaused = false;              // the owning world was paused at the last tick
	bool bLiveShot = false;                 // Play anchored at the contact
	void BroadcastClockChanged();
	int32 NextEventIndex = 0;
	TWeakObjectPtr<ARbCue> Cue;

	double FinishTime = 0.0;
	TFunction<double()> Clock;
	uint32 EvaluatedMask = 0;               // balls whose LastStates / LastOrientations are valid
	rb::BallState LastStates[rb::kMaxBalls];
	rb::Quat LastOrientations[rb::kMaxBalls];
	int8 ShownCache[rb::kMaxBalls] = {};    // -1 unknown, 0 hidden, 1 shown (by this playback)
};
