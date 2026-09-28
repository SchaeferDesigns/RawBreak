#include "Replay/RbReplaySubsystem.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Camera/RbCameraRigComponent.h"
#include "Cue/RbCue.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Replay/RbReplayCamera.h"
#include "Table/RbTable.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

// Owner: UE-7. History, replay start / end, camera views, restore of the live table (rules in the header). Tests:
// RawBreak.Unit.Replay.* (RbOverlayTests.cpp) and RawBreak.Functional.Replay (RbReplayTest.cpp: replay end state == live end
// state bitwise, refused while a live shot plays, the match is never changed).

namespace RbReplayUtil
{
	constexpr double VirtualEyeBackM = 0.50;   // virtual shooter eye: behind the struck ball on the strike's azimuth
	constexpr double VirtualEyeHeightM = 0.15; // above the ball centre
	constexpr double VirtualAimAheadM = 0.60;  // aimed at the cloth this far ahead of the ball

	ERbReplayView NextView(ERbReplayView View)
	{
		return static_cast<ERbReplayView>((static_cast<uint8>(View) + 1) % 4);
	}
}

const TCHAR* URbReplaySubsystem::ViewName(ERbReplayView View)
{
	switch (View)
	{
	case ERbReplayView::Shooter: return TEXT("Shooter");
	case ERbReplayView::Overhead: return TEXT("Overhead");
	case ERbReplayView::Rail: return TEXT("Rail");
	case ERbReplayView::Follow: return TEXT("Follow");
	}
	return TEXT("?");
}

void URbReplaySubsystem::RecordShot(const TSharedRef<const FRbShot>& Shot)
{
	History.Add(Shot);
	const int32 Cap = FMath::Max(1, MaxShots);
	if (History.Num() > Cap)
	{
		History.RemoveAt(0, History.Num() - Cap);
	}
}

TSharedPtr<const FRbShot> URbReplaySubsystem::GetShot(int32 IndexFromLast) const
{
	const int32 Index = History.Num() - 1 - IndexFromLast;
	return IndexFromLast >= 0 && History.IsValidIndex(Index) ? TSharedPtr<const FRbShot>(History[Index]) : nullptr;
}

bool URbReplaySubsystem::PlayReplay(int32 IndexFromLast, ERbReplayView View, float Rate)
{
	return PlayReplayFrom(IndexFromLast, View, Rate, 0.0);
}

bool URbReplaySubsystem::PlayReplayFrom(int32 IndexFromLast, ERbReplayView View, float Rate, double StartShotTime)
{
	const TSharedPtr<const FRbShot> Shot = GetShot(IndexFromLast);
	if (!Shot.IsValid() || !FMath::IsFinite(Rate) || !FMath::IsFinite(StartShotTime))
	{
		return false;
	}
	// Never while a live shot simulates or plays back (review R-07; the director decides per phase).
	URbMatchDirector* Director = FindDirector();
	if (Director && !Director->IsReplayAllowed())
	{
		UE_LOG(LogRawBreak, Log, TEXT("RbReplay: refused in director phase %d"), static_cast<int32>(Director->GetPhase()));
		return false;
	}
	ARbBallSet* Balls = FindBallSet();
	URbShotPlaybackComponent* Playback = Balls ? Balls->GetPlayback() : nullptr;
	if (!Playback)
	{
		return false;
	}
	// Somebody else's playback (a live shot on a dev map without a director): not ours to interrupt.
	if (Playback->IsPlaying() && !(bReplaying && Playback->GetShot() == ReplayShot))
	{
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldTimer);
	}
	bHolding = false;
	bReplaying = true;
	ReplayShot = Shot;
	ReplayIndex = IndexFromLast;
	CurrentView = View;
	ReplayRate = FMath::Max(0.0f, Rate);
	if (Director && !Director->IsReplayActive())
	{
		Director->SetReplayActive(true); // locks the stroke component; the director refuses every match input meanwhile
	}
	BindPlayback(Playback);
	if (Playback->IsPlaying())
	{
		Playback->Stop(false); // our previous replay (restart / next view)
	}
	// The pre-shot table of the stored input, then the stored result with its own clock (never re-simulated).
	Balls->ShowSimBalls(Shot->Request.Input.Balls, rb::kMaxBalls);
	Playback->Play(Shot.ToSharedRef(), false, FMath::Max(0.0, StartShotTime), ReplayRate);
	ApplyView();
	UE_LOG(LogRawBreak, Log, TEXT("RbReplay: shot %u (index %d) from the %s view at %.2fx from %.3f s"), Shot->Id, IndexFromLast, ViewName(View), ReplayRate,
		StartShotTime);
	OnReplayChanged.Broadcast();
	return true;
}

bool URbReplaySubsystem::HandleReplayInput()
{
	if (!bReplaying)
	{
		return PlayReplay(0, ERbReplayView::Shooter, 1.0f);
	}
	return PlayReplayFrom(ReplayIndex, RbReplayUtil::NextView(CurrentView), ReplayRate > 0.0f ? ReplayRate : 1.0f, 0.0);
}

void URbReplaySubsystem::StopReplay()
{
	if (!bReplaying)
	{
		ReplayShot.Reset();
		return;
	}
	RestoreLive();
}

void URbReplaySubsystem::CycleView()
{
	CurrentView = RbReplayUtil::NextView(CurrentView);
	if (bReplaying)
	{
		ApplyView();
		OnReplayChanged.Broadcast();
	}
}

void URbReplaySubsystem::SetRate(float Rate)
{
	if (!FMath::IsFinite(Rate))
	{
		return;
	}
	ReplayRate = FMath::Max(0.0f, Rate);
	URbShotPlaybackComponent* Playback = BoundPlayback.Get();
	if (bReplaying && Playback && Playback->GetShot() == ReplayShot)
	{
		Playback->SetRate(ReplayRate);
	}
	OnReplayChanged.Broadcast();
}

void URbReplaySubsystem::SeekReplay(double ShotTime)
{
	if (!bReplaying || !FMath::IsFinite(ShotTime))
	{
		return;
	}
	URbShotPlaybackComponent* Playback = BoundPlayback.Get();
	if (Playback && Playback->GetShot() == ReplayShot)
	{
		Playback->SeekTo(ShotTime);
		return;
	}
	// Holding the end frame: play again from there.
	PlayReplayFrom(ReplayIndex, CurrentView, ReplayRate, ShotTime);
}

void URbReplaySubsystem::OnReplayFinished(const TSharedRef<const FRbShot>& Shot)
{
	// OnFinished also fires for live shots on the same playback component (review R-07).
	if (!bReplaying || !ReplayShot.IsValid() || ReplayShot.Get() != &Shot.Get())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (EndHoldSeconds > 0.0 && World)
	{
		bHolding = true;
		World->GetTimerManager().SetTimer(HoldTimer, FTimerDelegate::CreateUObject(this, &URbReplaySubsystem::EndHold),
			static_cast<float>(EndHoldSeconds), false);
		OnReplayChanged.Broadcast();
		return;
	}
	StopReplay();
}

void URbReplaySubsystem::EndHold()
{
	if (bReplaying && bHolding)
	{
		RestoreLive();
	}
}

void URbReplaySubsystem::RestoreLive()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldTimer);
	}
	bHolding = false;
	if (URbShotPlaybackComponent* Playback = BoundPlayback.Get())
	{
		if (Playback->IsPlaying() && Playback->GetShot() == ReplayShot)
		{
			Playback->Stop(false);
		}
	}
	// The cue followed the stored tip path: back to hidden (the stroke component shows it again when the player gets down).
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	if (ARbCue* Cue = GameMode ? GameMode->GetCue() : nullptr)
	{
		if (Cue->GetDrive() == ERbCueDrive::Playback)
		{
			Cue->SetDrive(ERbCueDrive::Hidden);
		}
	}
	// View and camera rig back to the player.
	if (APlayerController* PC = ViewController.Get())
	{
		APawn* Pawn = PC->GetPawn();
		if (bRigModeSaved && Pawn)
		{
			if (URbCameraRigComponent* Rig = Pawn->FindComponentByClass<URbCameraRigComponent>())
			{
				Rig->SetMode(static_cast<ERbCameraRigMode>(SavedRigMode));
			}
		}
		AActor* Back = SavedViewTarget.IsValid() ? SavedViewTarget.Get() : static_cast<AActor*>(Pawn);
		if (Back && PC->GetViewTarget() != Back)
		{
			PC->SetViewTarget(Back);
		}
	}
	ViewController.Reset();
	SavedViewTarget.Reset();
	bRigModeSaved = false;

	const TSharedPtr<const FRbShot> Ended = ReplayShot;
	bReplaying = false;
	ReplayShot.Reset();
	// The director shows its FRbTableState again and re-arms the stroke component for its (unchanged) phase.
	if (URbMatchDirector* Director = FindDirector())
	{
		if (Director->IsReplayActive())
		{
			Director->SetReplayActive(false);
		}
	}
	UE_LOG(LogRawBreak, Log, TEXT("RbReplay: ended (shot %u), live table restored"), Ended.IsValid() ? Ended->Id : 0u);
	OnReplayChanged.Broadcast();
}

void URbReplaySubsystem::ApplyView()
{
	ARbReplayCamera* Cam = EnsureCamera();
	if (!Cam || !ReplayShot.IsValid())
	{
		return;
	}
	const FRbShot& Shot = *ReplayShot;
	ARbTable* Table = FindTable();
	ARbBallSet* Balls = FindBallSet();
	APlayerController* PC = FindLocalController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	// The struck ball (cue ball; a lag has two - the first) and where it lay at the stroke.
	int32 Struck = 0;
	if (Shot.Request.Input.Strikes.Size() > 0)
	{
		Struck = FMath::Clamp(static_cast<int32>(Shot.Request.Input.Strikes[0].Ball), 0, rb::kMaxBalls - 1);
	}
	const rb::Vec3 StruckAt = Shot.Request.Input.Balls[Struck].State.Position;

	// Shooter view: the stored eye of a human stroke, else a virtual eye behind the ball on the strike's axis.
	FTransform ShooterView = Shot.Request.ShooterView;
	const bool bStoredEye = Shot.Request.Stroke.bHuman && !ShooterView.Equals(FTransform::Identity, 1.0e-3);
	if (!bStoredEye && Table && Shot.Request.Input.Strikes.Size() > 0)
	{
		using namespace RbReplayUtil;
		const double Phi = Shot.Request.Input.Strikes[0].Input.Azimuth;
		const rb::Vec3 Along(FMath::Cos(Phi), FMath::Sin(Phi), 0.0);
		const FVector Eye = Table->CoreToWorld(StruckAt - Along * VirtualEyeBackM + rb::Vec3(0.0, 0.0, VirtualEyeHeightM));
		const FVector Aim = Table->CoreToWorld(rb::Vec3(StruckAt.x, StruckAt.y, 0.0) + Along * VirtualAimAheadM);
		ShooterView = FTransform(FRotationMatrix::MakeFromXZ((Aim - Eye).GetSafeNormal(), FVector::UpVector).ToQuat(), Eye);
	}
	Cam->ShooterFocusPoint = Table ? Table->CoreToWorld(StruckAt) : FVector::ZeroVector;
	Cam->ShooterFovDeg = RbCameraModel::Defaults(ERbCameraPreset::Eyes).VerticalFovDeg;
	if (const URbCameraRigComponent* PawnRig = Pawn ? Pawn->FindComponentByClass<URbCameraRigComponent>() : nullptr)
	{
		Cam->ShooterFovDeg = PawnRig->GetParams().VerticalFovDeg; // the player's own preset
	}
	Cam->SetFollowTarget(Balls ? Balls->GetBallComponent(Struck) : nullptr);
	Cam->SetView(CurrentView, Table, ShooterView);

	if (!PC)
	{
		return;
	}
	if (!ViewController.IsValid())
	{
		// First view of this replay session: remember what to return to.
		ViewController = PC;
		AActor* Current = PC->GetViewTarget();
		SavedViewTarget = Current != Cam ? Current : nullptr;
		if (URbCameraRigComponent* Rig = Pawn ? Pawn->FindComponentByClass<URbCameraRigComponent>() : nullptr)
		{
			SavedRigMode = static_cast<uint8>(Rig->GetMode());
			bRigModeSaved = true;
			Rig->SetMode(ERbCameraRigMode::External);
		}
	}
	if (PC->GetViewTarget() != Cam)
	{
		PC->SetViewTarget(Cam); // a cut, broadcast style
	}
}

ARbReplayCamera* URbReplaySubsystem::EnsureCamera()
{
	if (ARbReplayCamera* Existing = Camera.Get())
	{
		return Existing;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ARbReplayCamera* Spawned = World->SpawnActor<ARbReplayCamera>(ARbReplayCamera::StaticClass(), FTransform::Identity, Params);
	Camera = Spawned;
	return Spawned;
}

void URbReplaySubsystem::BindPlayback(URbShotPlaybackComponent* Playback)
{
	if (BoundPlayback.Get() == Playback)
	{
		return;
	}
	if (URbShotPlaybackComponent* Old = BoundPlayback.Get())
	{
		Old->OnFinished.Remove(FinishedHandle);
	}
	FinishedHandle.Reset();
	BoundPlayback = Playback;
	if (Playback)
	{
		FinishedHandle = Playback->OnFinished.AddUObject(this, &URbReplaySubsystem::OnReplayFinished);
	}
}

URbMatchDirector* URbReplaySubsystem::FindDirector() const
{
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	return GameMode ? GameMode->GetDirector() : nullptr;
}

ARbBallSet* URbReplaySubsystem::FindBallSet() const
{
	if (const ARbGameMode* GameMode = ARbGameMode::Get(this))
	{
		if (ARbBallSet* Balls = GameMode->GetBallSet())
		{
			return Balls;
		}
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ARbBallSet> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ARbTable* URbReplaySubsystem::FindTable() const
{
	if (const ARbGameMode* GameMode = ARbGameMode::Get(this))
	{
		if (ARbTable* Table = GameMode->GetTable())
		{
			return Table;
		}
	}
	if (const ARbBallSet* Balls = FindBallSet())
	{
		if (ARbTable* Table = Balls->GetTable())
		{
			return Table;
		}
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ARbTable> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

APlayerController* URbReplaySubsystem::FindLocalController() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController())
		{
			return PC;
		}
	}
	return nullptr;
}

void URbReplaySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldTimer);
	}
	if (URbShotPlaybackComponent* Playback = BoundPlayback.Get())
	{
		Playback->OnFinished.Remove(FinishedHandle);
	}
	FinishedHandle.Reset();
	BoundPlayback.Reset();
	bReplaying = false;
	bHolding = false;
	ReplayShot.Reset();
	History.Reset();
	Super::Deinitialize();
}
