#include "Balls/RbShotPlaybackComponent.h"

#include "Balls/RbBallSet.h"
#include "Core/RbCoords.h"
#include "Cue/RbCue.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/App.h"

// Owner: UE-2.

namespace
{
	bool IsCaptured(const rb::BallFinal& Final)
	{
		return Final.Status == rb::BallFinalStatus::Pocketed || Final.Status == rb::BallFinalStatus::OffTable;
	}

	// Same shown pose up to what no frame of continuous motion could hide: 1e-3 cm, ~3e-6 rad (q and -q are one rotation).
	bool SamePose(const FVector& LocationA, const FQuat& RotationA, const FVector& LocationB, const FQuat& RotationB)
	{
		return LocationA.Equals(LocationB, 1.0e-3) && FMath::Abs(RotationA | RotationB) >= 1.0 - 1.0e-12;
	}

	bool HasTipPath(const rb::ShotResult& Result, int32 Strike)
	{
		for (const rb::CueTipSegment& Piece : Result.CueTips)
		{
			if (Piece.Strike == Strike)
			{
				return true;
			}
		}
		return false;
	}
}

URbShotPlaybackComponent::URbShotPlaybackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = true; // to hold the clock while the world is paused (not to advance it)
	rb::ResetCursor(Cursor);
	FMemory::Memset(ShownCache, -1, sizeof(ShownCache));
}

double URbShotPlaybackComponent::ComputeFinishTime(const FRbShot& InShot, double InDropHideDelay)
{
	const rb::ShotResult& Result = InShot.Result;
	double End = FMath::IsFinite(Result.StopTime) ? FMath::Max(Result.StopTime, 0.0) : 0.0;
	for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
	{
		if ((Result.BallsInPlay >> Ball) & 1u)
		{
			const rb::BallFinal& Final = Result.Finals[Ball];
			if (IsCaptured(Final) && FMath::IsFinite(Final.Time))
			{
				End = FMath::Max(End, Final.Time + FMath::Max(InDropHideDelay, 0.0));
			}
		}
	}
	for (const rb::CueTipSegment& Piece : Result.CueTips)
	{
		if (Piece.Strike == 0 && FMath::IsFinite(Piece.T1))
		{
			End = FMath::Max(End, Piece.T1);
		}
	}
	return End;
}

void URbShotPlaybackComponent::SetClockSource(TFunction<double()> InClock)
{
	Clock = MoveTemp(InClock);
}

double URbShotPlaybackComponent::ClockNow() const
{
	return Clock ? Clock() : FApp::GetCurrentTime();
}

double URbShotPlaybackComponent::ClockShotTime(double Now) const
{
	return ClockOriginShotTime + (Now - ClockOrigin) * static_cast<double>(Rate);
}

double URbShotPlaybackComponent::ClampShotTime(double Time) const
{
	return FMath::Clamp(Time, 0.0, FinishTime);
}

int32 URbShotPlaybackComponent::FirstEventAtOrAfter(double T) const
{
	if (!Shot.IsValid())
	{
		return 0;
	}
	const std::vector<rb::ShotEvent>& Events = Shot->Result.Events;
	int32 Index = 0;
	while (Index < static_cast<int32>(Events.size()) && Events[Index].Time < T)
	{
		++Index;
	}
	return Index;
}

void URbShotPlaybackComponent::Play(const TSharedRef<const FRbShot>& InShot, bool bAnchorToContact, double StartShotTime, float InRate)
{
	Shot = InShot;
	rb::ResetCursor(Cursor);
	Rate = FMath::Max(0.0f, InRate);
	bPaused = false;
	EvaluatedMask = 0;
	FMemory::Memset(ShownCache, -1, sizeof(ShownCache));
	FinishTime = ComputeFinishTime(*InShot, DropHideDelay);

	const double Now = ClockNow();
	bLiveShot = bAnchorToContact;
	ClockOrigin = bAnchorToContact ? InShot->Request.ContactTime : Now;
	ClockOriginShotTime = bAnchorToContact ? 0.0 : ClampShotTime(StartShotTime);
	// Live: the shot started at the tip contact, possibly a frame ago - show the state of NOW, not t = 0 (review R-16).
	ShotTime = ClampShotTime(ClockShotTime(Now));
	NextEventIndex = FirstEventAtOrAfter(ClockOriginShotTime);

	// A ball that is not shown at the shot's pose at the clock origin (t = 0 of a live shot, StartShotTime of a replay) jumps:
	// its motion history is dropped after the first evaluation (a replay started from another table state, or from t0 > 0
	// while the table shows the start). A live shot starts where the table shows its balls (continuous). Random access at
	// the origin: t = 0 gives the input pose bitwise.
	FVector Locations[rb::kMaxBalls];
	FQuat Rotations[rb::kMaxBalls];
	uint32 Jumping = CaptureShownPoses(Locations, Rotations);
	for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
	{
		if (!((Jumping >> Ball) & 1u) || !((InShot->Result.BallsInPlay >> Ball) & 1u))
		{
			continue;
		}
		rb::BallState Origin;
		rb::Quat OriginOrientation;
		if (rb::StateAt(InShot->Result, Ball, ClockOriginShotTime, Origin) && rb::OrientationAt(InShot->Result, Ball, ClockOriginShotTime, OriginOrientation)
			&& SamePose(Locations[Ball], Rotations[Ball], FRbCoords::PositionToUE(Origin.Position), FRbCoords::OrientationToUE(OriginOrientation)))
		{
			Jumping &= ~(1u << Ball);
		}
	}

	// Balls that are not part of this shot are not shown while it plays.
	if (ARbBallSet* Balls = GetBallSet())
	{
		for (int32 Ball = 0; Ball < Balls->GetBallCount(); ++Ball)
		{
			if (!((InShot->Result.BallsInPlay >> Ball) & 1u))
			{
				SetBallShown(Balls, Ball, false);
			}
		}
	}
	if (ARbCue* CueActor = Cue.Get())
	{
		if (HasTipPath(InShot->Result, 0))
		{
			CueActor->SetDrive(ERbCueDrive::Playback);
		}
	}
	ApplyAt(ShotTime);
	ResetMovedBalls(Jumping, Locations, Rotations);
	// M2 (audio): the clock mapping before the first events fire (a listener may stop / replace this playback).
	OnPlaybackStarted.Broadcast(InShot, GetClockMapping());
	if (Shot.Get() != &InShot.Get())
	{
		return;
	}
	FireEventsUpTo(ShotTime);
}

FRbPlaybackClock URbShotPlaybackComponent::GetClockMapping() const
{
	FRbPlaybackClock Mapping;
	Mapping.OriginClock = ClockOrigin;
	Mapping.OriginShotTime = ClockOriginShotTime;
	Mapping.Rate = Rate;
	Mapping.bHeld = IsHeld();
	Mapping.bLive = bLiveShot;
	return Mapping;
}

void URbShotPlaybackComponent::BroadcastClockChanged()
{
	if (Shot.IsValid() && OnPlaybackClockChanged.IsBound())
	{
		OnPlaybackClockChanged.Broadcast(Shot.ToSharedRef(), GetClockMapping());
	}
}

void URbShotPlaybackComponent::Stop(bool bSnapToEnd)
{
	if (!Shot.IsValid())
	{
		return;
	}
	const TSharedRef<const FRbShot> Stopped = Shot.ToSharedRef();
	if (bSnapToEnd)
	{
		FVector Locations[rb::kMaxBalls];
		FQuat Rotations[rb::kMaxBalls];
		const uint32 Visible = CaptureShownPoses(Locations, Rotations);
		ApplyFinals(*Stopped);
		ResetMovedBalls(Visible, Locations, Rotations); // a snap from mid-shot is a jump
		ShotTime = FinishTime;
	}
	Shot.Reset();
	rb::ResetCursor(Cursor);
	bPaused = false;
}

void URbShotPlaybackComponent::SetRate(float NewRate)
{
	NewRate = FMath::Max(0.0f, NewRate);
	if (Shot.IsValid() && !IsHeld())
	{
		// Re-anchor at the current clock value: the shown time is continuous, only its speed changes.
		const double Now = ClockNow();
		ClockOriginShotTime = ClampShotTime(ClockShotTime(Now));
		ClockOrigin = Now;
	}
	const bool bChanged = NewRate != Rate;
	Rate = NewRate;
	if (bChanged)
	{
		BroadcastClockChanged();
	}
}

void URbShotPlaybackComponent::SetPaused(bool bPause)
{
	if (bPause != bPaused)
	{
		const bool bWasHeld = IsHeld();
		bPaused = bPause;
		OnHoldChanged(bWasHeld);
	}
}

void URbShotPlaybackComponent::SetWorldPaused(bool bPause)
{
	if (bPause != bWorldPaused)
	{
		const bool bWasHeld = IsHeld();
		bWorldPaused = bPause;
		OnHoldChanged(bWasHeld);
	}
}

void URbShotPlaybackComponent::OnHoldChanged(bool bWasHeld)
{
	const bool bHeld = IsHeld();
	if (bHeld == bWasHeld)
	{
		return;
	}
	const double Now = ClockNow();
	if (bHeld && Shot.IsValid())
	{
		// Freeze at the clock's current value (events up to it fire when the playback resumes).
		ShotTime = ClampShotTime(ClockShotTime(Now));
		ApplyAt(ShotTime);
	}
	// Holding or resuming: the clock continues from the shown time.
	ClockOrigin = Now;
	ClockOriginShotTime = ShotTime;
	BroadcastClockChanged();
}

void URbShotPlaybackComponent::SeekTo(double Time)
{
	if (!Shot.IsValid())
	{
		return;
	}
	ShotTime = ClampShotTime(Time);
	ClockOrigin = ClockNow();
	ClockOriginShotTime = ShotTime;
	NextEventIndex = FirstEventAtOrAfter(ShotTime);
	FVector Locations[rb::kMaxBalls];
	FQuat Rotations[rb::kMaxBalls];
	const uint32 Visible = CaptureShownPoses(Locations, Rotations);
	ApplyAt(ShotTime); // backward jumps re-seek the cursor automatically (rb::StateAtCursor)
	ResetMovedBalls(Visible, Locations, Rotations); // a seek is a jump: no streak across it
	BroadcastClockChanged();
}

void URbShotPlaybackComponent::SetCue(ARbCue* InCue)
{
	Cue = InCue;
}

void URbShotPlaybackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	SetWorldPaused(World != nullptr && World->IsPaused());
	Advance();
}

void URbShotPlaybackComponent::Advance()
{
	if (!Shot.IsValid() || IsHeld())
	{
		return;
	}
	const double Target = ClockShotTime(ClockNow());
	if (Target >= FinishTime)
	{
		ShotTime = FinishTime;
		ApplyAt(ShotTime);
		Finish();
		return;
	}
	ShotTime = ClampShotTime(Target);
	ApplyAt(ShotTime);
	FireEventsUpTo(ShotTime);
}

bool URbShotPlaybackComponent::FireEventsUpTo(double T)
{
	if (!Shot.IsValid())
	{
		return false;
	}
	const TSharedRef<const FRbShot> Current = Shot.ToSharedRef();
	const std::vector<rb::ShotEvent>& Events = Current->Result.Events;
	while (Shot.Get() == &Current.Get() && NextEventIndex < static_cast<int32>(Events.size()) && Events[NextEventIndex].Time <= T)
	{
		const int32 Index = NextEventIndex++;
		OnShotEvent.Broadcast(Current, Index);
	}
	return Shot.Get() == &Current.Get();
}

void URbShotPlaybackComponent::Finish()
{
	const TSharedRef<const FRbShot> Done = Shot.ToSharedRef();
	if (!FireEventsUpTo(rb::kInfinity))
	{
		return; // an event listener stopped or replaced this playback
	}
	ApplyFinals(*Done);
	Shot.Reset();
	rb::ResetCursor(Cursor);
	OnFinished.Broadcast(Done);
}

void URbShotPlaybackComponent::SetBallShown(ARbBallSet* Balls, int32 BallId, bool bShown)
{
	if (BallId < 0 || BallId >= rb::kMaxBalls)
	{
		return;
	}
	const int8 Value = bShown ? 1 : 0;
	if (ShownCache[BallId] != Value || Balls->IsBallVisible(BallId) != bShown)
	{
		Balls->SetBallVisible(BallId, bShown);
		ShownCache[BallId] = Value;
	}
}

void URbShotPlaybackComponent::ApplyAt(double Time)
{
	if (!Shot.IsValid())
	{
		return;
	}
	const rb::ShotResult& Result = Shot->Result;
	ARbBallSet* Balls = GetBallSet();
	EvaluatedMask = 0;
	for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
	{
		if (!((Result.BallsInPlay >> Ball) & 1u))
		{
			continue;
		}
		rb::BallState State;
		rb::Quat Orientation;
		if (!rb::StateAtCursor(Result, Cursor, Ball, Time, State, Orientation))
		{
			continue;
		}
		LastStates[Ball] = State;
		LastOrientations[Ball] = Orientation;
		EvaluatedMask |= 1u << Ball;
		if (!Balls || Ball >= Balls->GetBallCount())
		{
			continue;
		}
		const rb::BallFinal& Final = Result.Finals[Ball];
		const bool bGone = IsCaptured(Final) && Time >= Final.Time + static_cast<double>(DropHideDelay);
		if (!bGone)
		{
			Balls->SetBallCore(Ball, State.Position, Orientation);
			Balls->SetBallSpinCore(Ball, State.Omega);
		}
		SetBallShown(Balls, Ball, !bGone);
	}
	if (ARbCue* CueActor = Cue.Get())
	{
		rb::Vec3 Tip;
		rb::Vec3 Direction;
		if (rb::CueTipAt(Result, 0, Time, Tip, Direction))
		{
			CueActor->SetPoseCore(Tip, Direction);
		}
	}
}

void URbShotPlaybackComponent::ApplyFinals(const FRbShot& FinalShot)
{
	const rb::ShotResult& Result = FinalShot.Result;
	EvaluatedMask = 0;
	if (ARbBallSet* Balls = GetBallSet())
	{
		for (int32 Ball = 0; Ball < Balls->GetBallCount(); ++Ball)
		{
			const bool bInShot = Ball < rb::kMaxBalls && ((Result.BallsInPlay >> Ball) & 1u);
			const bool bOnTable = bInShot && Result.Finals[Ball].Status == rb::BallFinalStatus::OnTable;
			if (bOnTable)
			{
				Balls->SetBallCore(Ball, Result.Finals[Ball].State.Position, Result.Finals[Ball].Orientation);
				Balls->SetBallSpinCore(Ball, rb::Vec3(0.0, 0.0, 0.0));
			}
			SetBallShown(Balls, Ball, bOnTable);
		}
	}
	for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
	{
		if (((Result.BallsInPlay >> Ball) & 1u) && Result.Finals[Ball].Status != rb::BallFinalStatus::NotInPlay)
		{
			LastStates[Ball] = Result.Finals[Ball].State;
			LastOrientations[Ball] = Result.Finals[Ball].Orientation;
			EvaluatedMask |= 1u << Ball;
		}
	}
	if (ARbCue* CueActor = Cue.Get())
	{
		rb::Vec3 Tip;
		rb::Vec3 Direction;
		if (rb::CueTipAt(Result, 0, rb::kInfinity, Tip, Direction))
		{
			CueActor->SetPoseCore(Tip, Direction);
		}
	}
}

uint32 URbShotPlaybackComponent::CaptureShownPoses(FVector* OutLocations, FQuat* OutRotations) const
{
	uint32 Visible = 0;
	if (const ARbBallSet* Balls = GetBallSet())
	{
		const int32 Count = FMath::Min(Balls->GetBallCount(), rb::kMaxBalls);
		for (int32 Ball = 0; Ball < Count; ++Ball)
		{
			const UStaticMeshComponent* Component = Balls->GetBallComponent(Ball);
			if (Component && Balls->IsBallVisible(Ball))
			{
				OutLocations[Ball] = Component->GetRelativeLocation();
				OutRotations[Ball] = Balls->GetBallOrientationUE(Ball);
				Visible |= 1u << Ball;
			}
		}
	}
	return Visible;
}

void URbShotPlaybackComponent::ResetMovedBalls(uint32 VisibleBefore, const FVector* Locations, const FQuat* Rotations)
{
	ARbBallSet* Balls = GetBallSet();
	if (!Balls || VisibleBefore == 0)
	{
		return;
	}
	const int32 Count = FMath::Min(Balls->GetBallCount(), rb::kMaxBalls);
	for (int32 Ball = 0; Ball < Count; ++Ball)
	{
		// A ball that disappeared needs nothing; one that appeared starts without history (ARbBallSet::SetBallVisible drops it on hiding).
		const UStaticMeshComponent* Component = Balls->GetBallComponent(Ball);
		if (((VisibleBefore >> Ball) & 1u) && Component && Balls->IsBallVisible(Ball)
			&& !SamePose(Locations[Ball], Rotations[Ball], Component->GetRelativeLocation(), Balls->GetBallOrientationUE(Ball)))
		{
			Balls->ResetBallMotion(Ball);
		}
	}
}

bool URbShotPlaybackComponent::GetBallStateCore(int32 BallId, rb::BallState& OutState, rb::Quat& OutOrientation) const
{
	if (BallId < 0 || BallId >= rb::kMaxBalls || !((EvaluatedMask >> BallId) & 1u))
	{
		return false;
	}
	OutState = LastStates[BallId];
	OutOrientation = LastOrientations[BallId];
	return true;
}

ARbBallSet* URbShotPlaybackComponent::GetBallSet() const
{
	return Cast<ARbBallSet>(GetOwner());
}
