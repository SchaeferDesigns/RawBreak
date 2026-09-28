#include "Replay/RbReplaySubsystem.h"

#include "Replay/RbReplayCamera.h"

// Owner: UE-7. TODO(UE-7): history, replay start (lock input via the director phase, show Request.Input balls, play
// with bAnchorToContact = false), camera view target switching and blend, restore the live table + view at the end,
// tests (replay end state == live end state bitwise; history cap).

void URbReplaySubsystem::RecordShot(const TSharedRef<const FRbShot>& Shot)
{
	History.Add(Shot);
	while (History.Num() > MaxShots)
	{
		History.RemoveAt(0);
	}
}

TSharedPtr<const FRbShot> URbReplaySubsystem::GetShot(int32 IndexFromLast) const
{
	const int32 Index = History.Num() - 1 - IndexFromLast;
	return History.IsValidIndex(Index) ? TSharedPtr<const FRbShot>(History[Index]) : nullptr;
}

bool URbReplaySubsystem::PlayReplay(int32 /*IndexFromLast*/, ERbReplayView View, float /*Rate*/)
{
	CurrentView = View;
	return false; // TODO(UE-7)
}

void URbReplaySubsystem::StopReplay()
{
	bReplaying = false; // TODO(UE-7): restore the live table + view
	ReplayShot.Reset();
}

void URbReplaySubsystem::CycleView()
{
	CurrentView = static_cast<ERbReplayView>((static_cast<uint8>(CurrentView) + 1) % 4); // TODO(UE-7): apply
}

void URbReplaySubsystem::OnReplayFinished(const TSharedRef<const FRbShot>& Shot)
{
	// OnFinished also fires for live shots on the same playback component (review R-07).
	if (!bReplaying || !ReplayShot.IsValid() || ReplayShot.Get() != &Shot.Get())
	{
		return;
	}
	StopReplay();
}
