#include "Balls/RbShotPlaybackComponent.h"

#include "Balls/RbBallSet.h"
#include "Cue/RbCue.h"

#include "HAL/PlatformTime.h"

// Owner: UE-2. TODO(UE-2): clock (anchor / replay / rate / pause / seek), per-ball StateAtCursor -> ARbBallSet,
// Terminal handling, cue tip path, event firing, finish; tests RawBreak.Unit.Playback.* (matches rb::StateAt after the
// adapter to 1e-6 cm, monotone cursor = random access bitwise, no teleport).

URbShotPlaybackComponent::URbShotPlaybackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	rb::ResetCursor(Cursor);
}

void URbShotPlaybackComponent::Play(const TSharedRef<const FRbShot>& InShot, bool bAnchorToContact, double StartShotTime, float InRate)
{
	Shot = InShot;
	rb::ResetCursor(Cursor);
	Rate = InRate;
	bPaused = false;
	NextEventIndex = 0;
	const double Now = FPlatformTime::Seconds();
	ClockOrigin = bAnchorToContact ? InShot->Request.ContactTime : Now;
	ClockOriginShotTime = bAnchorToContact ? 0.0 : StartShotTime;
	// Live: the shot started at the tip contact, possibly a frame ago - show the state of NOW, not t = 0 (review R-16).
	ShotTime = ClockOriginShotTime + (Now - ClockOrigin) * Rate;
	ApplyAt(ShotTime);
}

void URbShotPlaybackComponent::Stop(bool /*bSnapToEnd*/)
{
	Shot.Reset(); // TODO(UE-2): snap to Result.Finals
}

void URbShotPlaybackComponent::SetRate(float NewRate)
{
	Rate = NewRate; // TODO(UE-2): re-anchor the clock so the time does not jump
}

void URbShotPlaybackComponent::SetPaused(bool bPause)
{
	bPaused = bPause; // TODO(UE-2): re-anchor on resume
}

void URbShotPlaybackComponent::SeekTo(double Time)
{
	ShotTime = Time; // TODO(UE-2): re-anchor, re-seek the cursor (backward jumps re-seek automatically)
	ApplyAt(ShotTime);
}

void URbShotPlaybackComponent::SetCue(ARbCue* InCue)
{
	Cue = InCue;
}

void URbShotPlaybackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Shot.IsValid() || bPaused)
	{
		return;
	}
	ShotTime = ClockOriginShotTime + (FPlatformTime::Seconds() - ClockOrigin) * Rate;
	ApplyAt(ShotTime);
	// TODO(UE-2): fire OnShotEvent for passed events, OnFinished at Result.StopTime (+ DropHideDelay).
}

void URbShotPlaybackComponent::ApplyAt(double /*Time*/)
{
	// TODO(UE-2): for each ball in Result.BallsInPlay: rb::StateAtCursor -> ARbBallSet::SetBallCore / SetBallSpinCore;
	// cue: rb::CueTipAt(Result, 0, Time, ...) -> ARbCue::SetPoseCore.
}

ARbBallSet* URbShotPlaybackComponent::GetBallSet() const
{
	return Cast<ARbBallSet>(GetOwner());
}
