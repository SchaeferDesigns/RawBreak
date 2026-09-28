// RawBreak.Functional.Replay (Docs/ue-architecture.md 12 A4 / A10, 13 UE-7): PIE with ARbGameMode (game mode override on the
// engine's Entry map, as RawBreak.Functional.MatchFlow) and the full scene: director, ball set + playback, cue, pawn with its
// stroke component, the controller's overlay component and the cheat manager. Every step goes through the cheat commands
// (PlayerController::ConsoleCommand) or the replay subsystem API:
//   cheats drive the director: RbPlaybackRate, RbPlaceCueBall, RbStrike (break), RbDumpState (grep-able line in the log),
//     RbOverlay (pinned + debug / hidden), RbReplay queued behind a live shot, RbStroke (human layer through the stroke component),
//     RbDeclare + RbChoose (push-out decision), RbRerack, RbNewMatch
//   replay: refused with no shot and while a live shot plays back; the queued RbReplay starts after the commit; the director is
//     locked (no stroke / placement / Confirm); the view is the replay camera (Overhead below the lamp, straight down); the
//     replay END STATE == the live END STATE bitwise (same stored result, never re-simulated); the last frame is held, then the
//     live table returns: no shot committed, history unchanged, balls at FRbTableState, view and stroke component back;
//     R-key cycling (Shooter -> Overhead), StopReplay, a frozen Follow view aimed at the cue ball.
//   UE-7 review: EVERY shown frame of the live shot and of its replay == the stored result at the shown shot time (rb::StateAt /
//     OrientationAt, bitwise; the end-state check alone compares Result.Finals with itself); every replay view jump and the
//     way back flag a camera cut; the replay camera ticks after the playback; the queue timer watches a scripted stroke's
//     deadline until contact, and a stroke that misses its deadline is ended (Commit released: no shot).
// Owner: UE-7.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbCoords.h"
#include "Cue/RbCue.h"
#include "Dev/RbCheatManager.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplayCamera.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"
#include "UI/RbOverlayComponent.h"

#include "rb/Physics/Playback.h"
#include "rb/Rules/TableRules.h"

#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbReplayFlow
{
	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	ARbGameMode* GameMode()
	{
		UWorld* World = PlayWorld();
		return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
	}

	// The simulation service may hand the shot off on a worker (UE-6a): collect it on the game thread.
	bool CollectShot(const URbMatchDirector& Director, URbSimulationSubsystem* Simulation)
	{
		for (int32 Try = 0; Try < 200 && Director.GetPhase() == ERbDirectorPhase::Simulating; ++Try)
		{
			if (Simulation)
			{
				Simulation->TryCollect(0.05);
			}
		}
		return Director.GetPhase() != ERbDirectorPhase::Simulating;
	}

	void Layout(URbMatchDirector& Director, std::initializer_list<TPair<int32, rb::Vec2>> Balls)
	{
		FRbTableState State = Director.GetTableState();
		for (rb::SimBall& Ball : State.Balls)
		{
			Ball.InPlay = false;
		}
		for (const TPair<int32, rb::Vec2>& At : Balls)
		{
			State.Balls[At.Key].InPlay = true;
			State.Balls[At.Key].State.Position = rb::Vec3(At.Value.x, At.Value.y, Director.GetTableContext()->BallRadius(At.Key));
		}
		Director.SetTableStateForTest(State);
	}

	void ToShotPhase(URbMatchDirector& Director)
	{
		for (int32 Try = 0; Try < 4 && Director.GetPhase() != ERbDirectorPhase::AwaitStroke && Director.GetPhase() != ERbDirectorPhase::AwaitPlacement; ++Try)
		{
			Director.Confirm();
		}
	}

	// The core states the playback showed when a shot finished (bitwise, per ball).
	struct FEndState
	{
		const FRbShot* Shot = nullptr;
		uint32 Valid = 0;
		rb::BallState States[rb::kMaxBalls];
		rb::Quat Orientations[rb::kMaxBalls];
	};

	FEndState Capture(const URbShotPlaybackComponent& Playback, const FRbShot& Shot)
	{
		FEndState End;
		End.Shot = &Shot;
		for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
		{
			if (Playback.GetBallStateCore(Ball, End.States[Ball], End.Orientations[Ball]))
			{
				End.Valid |= 1u << Ball;
			}
		}
		return End;
	}

	bool SameBits(const double* A, const double* B, int32 Count)
	{
		return FMemory::Memcmp(A, B, sizeof(double) * Count) == 0;
	}

	bool SameState(const rb::BallState& A, const rb::BallState& B)
	{
		const double Da[9] = {A.Position.x, A.Position.y, A.Position.z, A.Velocity.x, A.Velocity.y, A.Velocity.z, A.Omega.x, A.Omega.y, A.Omega.z};
		const double Db[9] = {B.Position.x, B.Position.y, B.Position.z, B.Velocity.x, B.Velocity.y, B.Velocity.z, B.Omega.x, B.Omega.y, B.Omega.z};
		return SameBits(Da, Db, 9) && A.State == B.State;
	}

	bool SameOrientation(const rb::Quat& A, const rb::Quat& B)
	{
		const double Da[4] = {A.w, A.x, A.y, A.z};
		const double Db[4] = {B.w, B.x, B.y, B.z};
		return SameBits(Da, Db, 4);
	}

	// Every ball the playback shows this frame == the stored result evaluated by random access at the same shot time
	// (rb::StateAt / OrientationAt), bitwise. Returns the number of balls compared; OutMismatch counts the differing ones.
	int32 CheckFrameAgainstResult(const URbShotPlaybackComponent& Playback, const FRbShot& Shot, int32& OutMismatch)
	{
		const double T = Playback.GetShotTime();
		int32 Compared = 0;
		for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
		{
			rb::BallState Shown;
			rb::Quat ShownQ;
			if (!Playback.GetBallStateCore(Ball, Shown, ShownQ))
			{
				continue;
			}
			rb::BallState Expect;
			rb::Quat ExpectQ;
			if (!rb::StateAt(Shot.Result, Ball, T, Expect) || !rb::OrientationAt(Shot.Result, Ball, T, ExpectQ))
			{
				continue;
			}
			++Compared;
			OutMismatch += SameState(Shown, Expect) && SameOrientation(ShownQ, ExpectQ) ? 0 : 1;
		}
		return Compared;
	}

	// Log lines of one console command (RbDumpState is grep-able).
	class FLineCapture : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			Lines.Add(V);
		}
		TArray<FString> Lines;
	};

	// The azimuth [deg] a scripted strike from the cue ball needs to hit Target (plan, core).
	float AzimuthDeg(const URbMatchDirector& Director, const rb::Vec2& Target)
	{
		const rb::Vec3 Cue = Director.GetTableState().Balls[0].State.Position;
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Target.y - Cue.y, Target.x - Cue.x)));
	}

	// Same conversion the cheat applies (float degrees -> radians), so a world-free director reproduces the PIE shot bitwise.
	double CheatRadians(float Degrees)
	{
		return FMath::DegreesToRadians(Degrees);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRbReplaySetGameModeCommand, UClass*, GameModeClass);

bool FRbReplaySetGameModeCommand::Update()
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (AWorldSettings* Settings = EditorWorld ? EditorWorld->GetWorldSettings() : nullptr)
	{
		Settings->DefaultGameMode = GameModeClass; // transient: the engine map is never saved
	}
	return true;
}

class FRbReplayWaitForMatchCommand : public IAutomationLatentCommand
{
public:
	FRbReplayWaitForMatchCommand(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* Mode = RbReplayFlow::GameMode();
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (Director && Director->GetPhase() != ERbDirectorPhase::Idle)
		{
			return true;
		}
		if (GetCurrentRunTime() > Timeout)
		{
			Test->AddError(TEXT("PIE did not start an ARbGameMode match"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Timeout;
};

// The whole flow as a stage machine (live playback and replays run in real time across frames).
class FRbReplayFlowCommand : public IAutomationLatentCommand
{
public:
	explicit FRbReplayFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual ~FRbReplayFlowCommand() override
	{
		Unbind();
	}

	virtual bool Update() override
	{
		if (!Resolve())
		{
			return true;
		}
		SampleFrame();
		bool bDone = false;
		switch (Stage)
		{
		case 0: bDone = StageCheatsAndBreak(); break;
		case 1: bDone = StageLiveShot(); break;
		case 2: bDone = StageWaitLiveCommitAndReplayStart(); break;
		case 3: bDone = StageWaitReplayEnd(); break;
		case 4: bDone = StageWaitRestore(); break;
		case 5: bDone = StageFollowCheck(); break;
		case 6: bDone = StageWaitStroke(); break;
		case 7: bDone = StageStrokeTimeout(); break;
		case 8: bDone = StageWaitStrokeTimeout(); break;
		case 9: bDone = StageDecisionRerackNewMatch(); break;
		default: bDone = true; break;
		}
		if (bDone)
		{
			Unbind();
			return true;
		}
		if (Stage != LastStage)
		{
			LastStage = Stage;
			StageStart = FPlatformTime::Seconds();
		}
		else if (FPlatformTime::Seconds() - StageStart > 20.0)
		{
			Test->AddError(FString::Printf(TEXT("stage %d timed out"), Stage));
			Unbind();
			return true;
		}
		return false;
	}

private:
	bool Resolve()
	{
		using namespace RbReplayFlow;
		World = PlayWorld();
		Mode = GameMode();
		Director = Mode ? Mode->GetDirector() : nullptr;
		PC = World ? Cast<ARbPlayerController>(World->GetFirstPlayerController()) : nullptr;
		Balls = Mode ? Mode->GetBallSet() : nullptr;
		Playback = Balls ? Balls->GetPlayback() : nullptr;
		Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
		Simulation = World ? World->GetSubsystem<URbSimulationSubsystem>() : nullptr;
		Character = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
		Stroke = Character ? Character->GetStroke() : nullptr;
		return Test->TestNotNull(TEXT("ARbGameMode"), Mode) && Test->TestNotNull(TEXT("director"), Director) && Test->TestNotNull(TEXT("ARbPlayerController"), PC) &&
			Test->TestNotNull(TEXT("playback"), Playback) && Test->TestNotNull(TEXT("replay subsystem"), Replay) &&
			Test->TestNotNull(TEXT("stroke component"), Stroke);
	}

	void Cmd(const TCHAR* Command)
	{
		PC->ConsoleCommand(Command);
	}

	// Every frame the live shot or its replay is on screen: the shown core states == the stored result at the shown shot time,
	// bitwise (the live playback and the replay evaluate the same trajectories; the end-state check alone compares
	// Result.Finals with itself). The finished / held end frame is not sampled (the playback no longer plays).
	void SampleFrame()
	{
		if (!LiveShot.IsValid() || !Playback->IsPlaying() || Playback->GetShot() != LiveShot)
		{
			return;
		}
		const bool bReplayFrame = Replay->IsReplaying() && Replay->GetReplayShot() == LiveShot;
		const double T = Playback->GetShotTime();
		double& LastT = bReplayFrame ? LastReplaySampleTime : LastLiveSampleTime;
		if (T == LastT)
		{
			return; // same shown frame (paused / frozen / no tick since the last sample)
		}
		LastT = T;
		int32 Mismatch = 0;
		const int32 Compared = RbReplayFlow::CheckFrameAgainstResult(*Playback, *LiveShot, Mismatch);
		if (Compared > 0)
		{
			(bReplayFrame ? ReplayFramesChecked : LiveFramesChecked) += 1;
		}
		if (Mismatch > 0)
		{
			FrameMismatches += Mismatch;
			Test->AddError(FString::Printf(TEXT("%s frame at t = %.9f s: %d ball(s) differ from the stored result"), bReplayFrame ? TEXT("replay") : TEXT("live"),
				T, Mismatch));
		}
	}

	void Unbind()
	{
		if (URbShotPlaybackComponent* Pb = BoundPlayback.Get())
		{
			Pb->OnFinished.Remove(FinishedHandle);
		}
		BoundPlayback.Reset();
		FinishedHandle.Reset();
	}

	// --- stage 0: cheats drive the director; the break --------------------------------------------------------------------
	bool StageCheatsAndBreak()
	{
		using namespace RbReplayFlow;
		URbCheatManager* Cheats = Cast<URbCheatManager>(PC->CheatManager);
		if (!Test->TestNotNull(TEXT("URbCheatManager (CheatClass of ARbPlayerController)"), Cheats))
		{
			return true;
		}
		Test->TestFalse(TEXT("no replay without a recorded shot"), Replay->PlayReplay(0, ERbReplayView::Shooter));
		Test->TestFalse(TEXT("not replaying"), Replay->IsReplaying());

		Cmd(TEXT("RbPlaybackRate 0"));
		Test->TestEqual(TEXT("RbPlaybackRate drives the director"), Director->GetLivePlaybackRate(), 0.0f);
		FRbMatchSetup Setup;
		Setup.Mode = ERbMatchMode::HotSeat;
		Setup.Seed = 11;
		Setup.RaceTo = 3;
		if (!Test->TestTrue(TEXT("hot-seat match"), Director->StartMatch(Setup)))
		{
			return true;
		}
		const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
		Cmd(TEXT("RbPlaceCueBall 0.3 0.1"));
		Test->TestEqual(TEXT("RbPlaceCueBall below the head string refused"), Director->GetPhase(), ERbDirectorPhase::AwaitPlacement);
		const float PlaceX = static_cast<float>(Rules.HeadStringX - 0.12);
		Cmd(*FString::Printf(TEXT("RbPlaceCueBall %.9g 0.08"), PlaceX));
		Test->TestEqual(TEXT("RbPlaceCueBall places"), Director->GetPhase(), ERbDirectorPhase::AwaitStroke);
		const int32 Apex = rb::rules::LowestObjectBallAtStart(Director->GetMatchState().Game);
		const float BreakAz = AzimuthDeg(*Director, rb::XY(Director->GetTableState().Balls[Apex].State.Position));
		Cmd(*FString::Printf(TEXT("RbStrike 9 %.9g 0 0 -0.1"), BreakAz));
		Test->TestTrue(TEXT("RbStrike submits"), Director->GetPhase() == ERbDirectorPhase::Simulating || Director->GetMatchShotIndex() == 1);
		Test->TestTrue(TEXT("hand-off"), CollectShot(*Director, Simulation));
		Test->TestEqual(TEXT("the break committed (rate 0)"), Director->GetMatchShotIndex(), 1u);
		Test->TestEqual(TEXT("recorded for replays"), Replay->GetShotCount(), 1);
		const TSharedPtr<const FRbShot> Break = Director->GetLastCommittedShot();
		Test->TestTrue(TEXT("scripted strike at the requested azimuth"), Break.IsValid() && !Break->Request.Stroke.bHuman &&
			Break->Request.Input.Strikes[0].Input.Azimuth == CheatRadians(BreakAz));

		// RbDumpState: one grep-able line.
		const FString Line = Cheats->MakeStateLine();
		Test->AddInfo(Line);
		Test->TestTrue(TEXT("state line format"), Line.StartsWith(TEXT("RbState: phase=")) && Line.Contains(TEXT(" mode=HotSeat ")) &&
			Line.Contains(TEXT(" shot=1 ")) && Line.Contains(TEXT(" history=1 ")) && Line.Contains(TEXT(" last{valid=1 ")) && Line.Contains(TEXT(" balls=[")));
		FLineCapture Captured;
		GLog->AddOutputDevice(&Captured);
		Cmd(TEXT("RbDumpState"));
		GLog->Flush();
		GLog->RemoveOutputDevice(&Captured);
		Test->TestTrue(TEXT("RbDumpState logs the line"), Captured.Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("RbState: phase=")); }));

		// RbOverlay drives the controller's overlay component (bound to the director; the break auto-glanced).
		URbOverlayComponent* Overlay = PC->GetOverlay();
		if (Test->TestNotNull(TEXT("overlay component"), Overlay))
		{
			Test->TestTrue(TEXT("overlay model from the director"), Overlay->GetModel().Title.ToString().Contains(TEXT("HOT-SEAT")));
			Test->TestTrue(TEXT("the committed break auto-glances"), Overlay->GetMode() == ERbOverlayMode::Glance && Overlay->GetAutoGlanceRemaining() > 0.0f);
			Cmd(TEXT("RbOverlay 2"));
			Test->TestTrue(TEXT("RbOverlay 2: pinned + debug"), Overlay->GetMode() == ERbOverlayMode::Pinned && Overlay->IsDebugShown() &&
				Overlay->GetModel().DebugLines.Num() > 0);
			Cmd(TEXT("RbOverlay 0"));
			Test->TestTrue(TEXT("RbOverlay 0: unpinned, no debug"), !Overlay->IsPinned() && !Overlay->IsDebugShown());
		}
		Stage = 1;
		return false;
	}

	// --- stage 1: a live shot at rate 4 (real playback); replays refused while it plays; RbReplay queued -------------------
	bool StageLiveShot()
	{
		using namespace RbReplayFlow;
		ToShotPhase(*Director);
		Layout(*Director, {{0, rb::Vec2(-0.6, 0.0)}, {1, rb::Vec2(0.3, 0.1)}, {9, rb::Vec2(0.6, -0.3)}});
		BoundPlayback = Playback;
		FinishedHandle = Playback->OnFinished.AddLambda([this](const TSharedRef<const FRbShot>& Shot) {
			if (URbShotPlaybackComponent* Pb = BoundPlayback.Get())
			{
				Ends.Add(RbReplayFlow::Capture(*Pb, Shot.Get()));
			}
		});
		Cmd(TEXT("RbPlaybackRate 4"));
		ShotsBefore = Replay->GetShotCount();
		IndexBefore = Director->GetMatchShotIndex();
		Cmd(*FString::Printf(TEXT("RbStrike 2 %.9g 0 0 0"), AzimuthDeg(*Director, rb::Vec2(0.3, 0.1))));
		Test->TestTrue(TEXT("hand-off"), CollectShot(*Director, Simulation));
		LiveShot = Director->GetPendingShot();
		if (!Test->TestTrue(TEXT("the live shot plays back"), Director->GetPhase() == ERbDirectorPhase::PlayingBack && LiveShot.IsValid() &&
			Playback->GetShot() == LiveShot))
		{
			return true;
		}
		Test->TestFalse(TEXT("PlayReplay refused while a live shot plays back"), Replay->PlayReplay(0, ERbReplayView::Overhead, 1.0f));
		Test->TestFalse(TEXT("... and nothing started"), Replay->IsReplaying() || Director->IsReplayActive());
		Test->TestTrue(TEXT("the live playback continues"), Playback->GetShot() == LiveShot);
		URbCheatManager* Cheats = Cast<URbCheatManager>(PC->CheatManager);
		Cmd(TEXT("RbReplay 1 6"));
		Test->TestTrue(TEXT("RbReplay waits behind the live shot"), Cheats && Cheats->GetQueuedCount() == 1 && !Replay->IsReplaying());
		Stage = 2;
		return false;
	}

	// --- stage 2: the live shot commits, the queued replay starts ----------------------------------------------------------
	bool StageWaitLiveCommitAndReplayStart()
	{
		using namespace RbReplayFlow;
		if (!Replay->IsReplaying())
		{
			return false;
		}
		Test->TestEqual(TEXT("the live shot committed once"), Director->GetMatchShotIndex(), IndexBefore + 1);
		Test->TestEqual(TEXT("recorded"), Replay->GetShotCount(), ShotsBefore + 1);
		Test->TestTrue(TEXT("the live end state was captured first"), Ends.Num() == 1 && Ends[0].Shot == LiveShot.Get());
		CommittedIndex = Director->GetMatchShotIndex();
		CommittedPhase = Director->GetPhase();
		CommittedShot = Director->GetLastCommittedShot();
		Test->TestTrue(TEXT("replaying the stored live shot (never re-simulated)"), Replay->GetReplayShot() == LiveShot && Playback->GetShot() == LiveShot);
		Test->TestEqual(TEXT("from the Overhead view"), Replay->GetView(), ERbReplayView::Overhead);
		Test->TestEqual(TEXT("at the requested rate"), Playback->GetRate(), 6.0f);
		Test->TestTrue(TEXT("director locked for the replay"), Director->IsReplayActive() && Stroke->GetPhase() == ERbStrokePhase::Locked);
		Test->TestFalse(TEXT("no stroke during a replay"), Director->SubmitScriptedStrike(1.0, 0.0, 0.0, 0.0, 0.0));
		Test->TestFalse(TEXT("no Confirm during a replay"), Director->Confirm());
		ARbReplayCamera* Camera = Replay->GetCamera();
		if (Test->TestNotNull(TEXT("replay camera"), Camera))
		{
			Test->TestTrue(TEXT("the player views through the replay camera"), PC->GetViewTarget() == Camera);
			const FVector Forward = Camera->GetActorForwardVector();
			const ARbTable* Table = Mode->GetTable();
			Test->TestTrue(TEXT("overhead: straight down"), Forward.Z < -0.999);
			const double HeightM = Table->WorldToCore(Camera->GetActorLocation()).z;
			Test->TestTrue(TEXT("overhead: below the lamp"), HeightM < Table->LampUndersideHeight && HeightM > 0.5);
			Test->TestTrue(TEXT("overhead: screen right = the foot rail"), Camera->GetActorRightVector().Equals(Table->CoreDirectionToWorld(rb::Vec3(1.0, 0.0, 0.0)), 1.0e-6));
		}
		Stage = 3;
		return false;
	}

	// --- stage 3: the replay's playback ends: bitwise == the live end; the last frame is held -------------------------------
	bool StageWaitReplayEnd()
	{
		using namespace RbReplayFlow;
		if (Ends.Num() < 2)
		{
			return false;
		}
		const FEndState& Live = Ends[0];
		const FEndState& Again = Ends[1];
		Test->TestTrue(TEXT("the replay ended on the same shot"), Again.Shot == LiveShot.Get());
		Test->TestEqual(TEXT("same balls evaluated"), Again.Valid, Live.Valid);
		int32 Mismatch = 0;
		int32 Compared = 0;
		for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
		{
			if ((Live.Valid >> Ball) & 1u)
			{
				++Compared;
				if (!SameState(Live.States[Ball], Again.States[Ball]) || !SameOrientation(Live.Orientations[Ball], Again.Orientations[Ball]))
				{
					++Mismatch;
					Test->AddError(FString::Printf(TEXT("ball %d: replay end != live end"), Ball));
				}
			}
		}
		Test->TestTrue(TEXT("balls compared"), Compared >= 3);
		Test->TestEqual(TEXT("replay end state == live end state (bitwise)"), Mismatch, 0);
		Test->TestTrue(TEXT("the last frame is held"), Replay->IsReplaying() && Replay->IsHoldingEnd());
		Test->TestEqual(TEXT("no commit from a finished replay (R-07)"), Director->GetMatchShotIndex(), CommittedIndex);
		Stage = 4;
		return false;
	}

	// --- stage 4: back to the live table; R cycling; StopReplay; a frozen Follow view -----------------------------------------
	bool StageWaitRestore()
	{
		using namespace RbReplayFlow;
		if (Replay->IsReplaying())
		{
			return false;
		}
		Test->TestTrue(TEXT("match unchanged by the replay"), Director->GetMatchShotIndex() == CommittedIndex && Director->GetPhase() == CommittedPhase &&
			Director->GetLastCommittedShot() == CommittedShot && Replay->GetShotCount() == ShotsBefore + 1);
		Test->TestFalse(TEXT("director unlocked"), Director->IsReplayActive());
		Test->TestTrue(TEXT("view back on the pawn"), PC->GetViewTarget() == PC->GetPawn());
		Test->TestTrue(TEXT("cue hidden again"), Mode->GetCue()->GetDrive() == ERbCueDrive::Hidden);
		const ERbStrokePhase Expected = Director->GetPhase() == ERbDirectorPhase::AwaitStroke ? ERbStrokePhase::Walking
			: (Director->GetPhase() == ERbDirectorPhase::AwaitPlacement ? ERbStrokePhase::PlacingCueBall : ERbStrokePhase::Locked);
		Test->TestEqual(TEXT("stroke component re-armed for the phase"), Stroke->GetPhase(), Expected);
		// The balls show the director's table state again.
		const FRbTableState& Table = Director->GetTableState();
		int32 Wrong = 0;
		for (int32 Ball = 0; Ball < Balls->GetBallCount() && Ball < rb::kMaxBalls; ++Ball)
		{
			const bool bShown = Balls->IsBallVisible(Ball);
			bool bOk = bShown == Table.Balls[Ball].InPlay;
			if (bOk && bShown)
			{
				const FVector Expect = FRbCoords::PositionToUE(Table.Balls[Ball].State.Position);
				bOk = Balls->GetBallComponent(Ball)->GetRelativeLocation().Equals(Expect, 1.0e-6);
			}
			Wrong += bOk ? 0 : 1;
		}
		Test->TestEqual(TEXT("balls at FRbTableState after the replay"), Wrong, 0);

		// Every shown frame of the live shot and of its replay was the stored result at the shown time (SampleFrame).
		Test->AddInfo(FString::Printf(TEXT("frames checked bitwise against the stored result: live %d, replay %d"), LiveFramesChecked, ReplayFramesChecked));
		Test->TestTrue(TEXT("live frames checked"), LiveFramesChecked >= 3);
		Test->TestTrue(TEXT("replay frames checked"), ReplayFramesChecked >= 3);
		Test->TestEqual(TEXT("every live and replay frame == the stored result (bitwise)"), FrameMismatches, 0);

		// R key: the last shot from the Shooter view, again = the next view (restart); StopReplay at once. Every view jump is a
		// camera cut for the renderer (TSR history / motion blur), also the way back to the pawn.
		APlayerCameraManager* CameraManager = PC->PlayerCameraManager;
		const auto CutAfter = [CameraManager](TFunctionRef<void()> Action) {
			if (!CameraManager)
			{
				return false;
			}
			CameraManager->bGameCameraCutThisFrame = false;
			Action();
			return CameraManager->bGameCameraCutThisFrame != 0;
		};
		ToShotPhase(*Director);
		bool bStarted = false;
		Test->TestTrue(TEXT("R: camera cut to the replay view"), CutAfter([&]() { bStarted = Replay->HandleReplayInput(); }));
		Test->TestTrue(TEXT("R: replay"), bStarted && Replay->IsReplaying() && Replay->GetView() == ERbReplayView::Shooter);
		Test->TestTrue(TEXT("R again: camera cut"), CutAfter([&]() { bStarted = Replay->HandleReplayInput(); }));
		Test->TestTrue(TEXT("R again: next view"), bStarted && Replay->GetView() == ERbReplayView::Overhead && Playback->GetShot() == Replay->GetReplayShot());
		Test->TestTrue(TEXT("CycleView: camera cut"), CutAfter([&]() { Replay->CycleView(); }));
		Test->TestEqual(TEXT("CycleView"), Replay->GetView(), ERbReplayView::Rail);
		if (ARbReplayCamera* Camera = Replay->GetCamera())
		{
			const ARbTable* TableActor = Mode->GetTable();
			const rb::Vec3 Eye = TableActor->WorldToCore(Camera->GetActorLocation());
			Test->TestTrue(TEXT("rail camera behind the head rail, above the cloth"), Eye.x < TableActor->GetContext().Geometry.OuterBoundary.Lo.x && Eye.z > 0.2);
			Test->TestTrue(TEXT("replay camera ticks after the playback placed the balls"), Camera->PrimaryActorTick.TickGroup == TG_PostPhysics);
		}
		Test->TestTrue(TEXT("RbStopReplay: camera cut back to the pawn"), CutAfter([&]() { Cmd(TEXT("RbStopReplay")); }));
		Test->TestTrue(TEXT("RbStopReplay restores at once"), !Replay->IsReplaying() && !Director->IsReplayActive() && PC->GetViewTarget() == PC->GetPawn());
		// Follow, frozen at 0.3 s (rate 0): the camera aims at the struck ball after a frame.
		Test->TestTrue(TEXT("frozen follow replay"), Replay->PlayReplayFrom(0, ERbReplayView::Follow, 0.0f, 0.3));
		Stage = 5;
		return false;
	}

	bool StageFollowCheck()
	{
		using namespace RbReplayFlow;
		if (++FollowFrames < 5)
		{
			return false;
		}
		ARbReplayCamera* Camera = Replay->GetCamera();
		const UStaticMeshComponent* Cue = Balls->GetBallComponent(0);
		if (Camera && Cue)
		{
			const FVector ToBall = (Cue->GetComponentLocation() - Camera->GetActorLocation()).GetSafeNormal();
			Test->TestTrue(TEXT("follow camera aims at the cue ball"), FVector::DotProduct(ToBall, Camera->GetActorForwardVector()) > 0.999);
			Test->TestTrue(TEXT("frozen at 0.3 s"), FMath::IsNearlyEqual(Playback->GetShotTime(), 0.3, 1.0e-9));
			Test->TestTrue(TEXT("the follow camera ticks during its replay"), Camera->IsActorTickEnabled());
		}
		Replay->StopReplay();
		if (Camera)
		{
			Test->TestFalse(TEXT("the replay camera idles during live play (no per-frame follow trace)"), Camera->IsActorTickEnabled());
		}

		// RbStroke: the human layer through the pawn's stroke component (get down, Commit, scripted hand samples).
		ToShotPhase(*Director);
		Cmd(TEXT("RbPlaybackRate 0"));
		Layout(*Director, {{0, rb::Vec2(-0.5, 0.0)}, {1, rb::Vec2(0.3, 0.05)}, {9, rb::Vec2(0.6, -0.3)}});
		IndexBefore = Director->GetMatchShotIndex();
		Cmd(TEXT("RbStroke 2 0"));
		Test->TestTrue(TEXT("RbStroke: the pawn got down on the shot"), Stroke->GetPhase() == ERbStrokePhase::GettingDown || Stroke->GetPhase() == ERbStrokePhase::Down);
		URbCheatManager* Cheats = Cast<URbCheatManager>(PC->CheatManager);
		Test->TestTrue(TEXT("RbStroke: a scripted stroke in flight"), Cheats && Cheats->IsStrokeInFlight());
		Stage = 6;
		return false;
	}

	bool StageWaitStroke()
	{
		using namespace RbReplayFlow;
		URbCheatManager* Cheats = Cast<URbCheatManager>(PC->CheatManager);
		if (Director->GetMatchShotIndex() == IndexBefore)
		{
			// The queue timer watches the stroke's deadline for as long as the stroke is in flight (it used to stop after 20 ms).
			if (Cheats && Cheats->IsStrokeInFlight() && !Cheats->IsQueueTimerActive())
			{
				++WatchdogGaps;
			}
			CollectShot(*Director, Simulation);
			return false;
		}
		Test->TestEqual(TEXT("the stroke's deadline watched until contact"), WatchdogGaps, 0);
		Test->TestTrue(TEXT("contact ends the scripted stroke"), Cheats && !Cheats->IsStrokeInFlight());
		const TSharedPtr<const FRbShot> Shot = Director->GetLastCommittedShot();
		if (Test->TestTrue(TEXT("RbStroke committed a human-layer shot"), Shot.IsValid() && Shot->Request.Stroke.bHuman))
		{
			const rb::human::IntendedStroke& Intended = Shot->Request.Stroke.Intended;
			Test->AddInfo(FString::Printf(TEXT("RbStroke: intended V %.6f m/s, phi %.9f rad, executed V %.6f"), Intended.Speed, Intended.Azimuth,
				Shot->Request.Stroke.Executed.Strike.Speed));
			Test->TestTrue(TEXT("intended speed = the scripted speed (1e-3 m/s)"), FMath::Abs(Intended.Speed - 2.0) < 1.0e-3);
			Test->TestTrue(TEXT("intended azimuth = the requested aim"), FMath::Abs(Intended.Azimuth) < 1.0e-9);
			Test->TestTrue(TEXT("scripted samples in the input log"), Shot->Request.Stroke.InputLog.Num() > 3);
		}
		Stage = 7;
		return false;
	}

	// --- stages 7-8: a scripted stroke that does not reach the ball in time is ended (Commit released): no shot ----------------
	bool StageStrokeTimeout()
	{
		using namespace RbReplayFlow;
		URbCheatManager* Cheats = Cast<URbCheatManager>(PC->CheatManager);
		if (!Test->TestNotNull(TEXT("cheat manager"), Cheats))
		{
			return true;
		}
		ToShotPhase(*Director);
		if (Director->GetPhase() == ERbDirectorPhase::AwaitPlacement)
		{
			Director->PlaceCueBall(rb::Vec2(Director->GetMatchConfig().Table.HeadStringX - 0.12, 0.0)); // legal in every region
		}
		Layout(*Director, {{0, rb::Vec2(-0.5, 0.0)}, {1, rb::Vec2(0.3, 0.05)}, {9, rb::Vec2(0.6, -0.3)}});
		if (!Test->TestEqual(TEXT("timeout case: a stroke is expected"), Director->GetPhase(), ERbDirectorPhase::AwaitStroke))
		{
			return true;
		}
		IndexBefore = Director->GetMatchShotIndex();
		// The scripted samples start 0.25 s after the get-down (>= 1 s): a 0.05 s deadline passes long before the cue moves.
		Cheats->StrokeTimeoutSeconds = 0.05;
		Cmd(TEXT("RbStroke 2 0"));
		Cheats->StrokeTimeoutSeconds = 10.0;
		Test->TestTrue(TEXT("timeout case: stroke in flight"), Cheats->IsStrokeInFlight());
		TimeoutStart = FPlatformTime::Seconds();
		Stage = 8;
		return false;
	}

	bool StageWaitStrokeTimeout()
	{
		URbCheatManager* Cheats = Cast<URbCheatManager>(PC->CheatManager);
		// Long enough for the get-down (GetDownSeconds) and the whole scripted stroke to run as a practice stroke.
		if (FPlatformTime::Seconds() - TimeoutStart < Stroke->GetDownSeconds + 2.5)
		{
			return false;
		}
		Test->TestTrue(TEXT("the timed-out stroke was ended by the queue timer"), Cheats && !Cheats->IsStrokeInFlight());
		Test->TestTrue(TEXT("the queue timer stops afterwards"), Cheats && !Cheats->IsQueueTimerActive());
		Test->TestEqual(TEXT("Commit released: the scripted samples ran as a practice stroke, no shot"), Director->GetMatchShotIndex(), IndexBefore);
		Test->TestEqual(TEXT("still the same turn"), Director->GetPhase(), ERbDirectorPhase::AwaitStroke);
		Stage = 9;
		return false;
	}

	// --- stage 9: push-out decision (RbDeclare, RbChoose), RbRerack, RbNewMatch ---------------------------------------------
	bool StageDecisionRerackNewMatch()
	{
		using namespace RbReplayFlow;
		// A seed with a legal break (push-out window) found world-free with this table's context: same scripted inputs, same
		// simulation (UE-6a determinism).
		const float PlaceX = static_cast<float>(Director->GetMatchConfig().Table.HeadStringX - 0.12);
		const float PlaceY = 0.08f;
		int64 FoundSeed = 0;
		float FoundAz = 0.0f;
		for (int64 Seed = 1; Seed <= 30 && FoundSeed == 0; ++Seed)
		{
			TStrongObjectPtr<URbMatchDirector> Probe(NewObject<URbMatchDirector>(GetTransientPackage()));
			Probe->Initialize(nullptr, nullptr, nullptr, nullptr);
			Probe->SetTableContext(Director->GetTableContext());
			Probe->SetLivePlaybackRate(0.0f);
			FRbMatchSetup Setup;
			Setup.Mode = ERbMatchMode::HotSeat;
			Setup.Seed = Seed;
			if (!Probe->StartMatch(Setup) || !Probe->PlaceCueBall(rb::Vec2(PlaceX, PlaceY)))
			{
				continue;
			}
			const int32 Apex = rb::rules::LowestObjectBallAtStart(Probe->GetMatchState().Game);
			const float Az = AzimuthDeg(*Probe, rb::XY(Probe->GetTableState().Balls[Apex].State.Position));
			Probe->SubmitScriptedStrike(9.0f, CheatRadians(Az), 0.0, 0.0, -0.1f);
			if (Probe->GetPhase() == ERbDirectorPhase::AwaitStroke && Probe->GetConstraints().PushOutAllowed)
			{
				FoundSeed = Seed;
				FoundAz = Az;
			}
		}
		if (Test->TestTrue(TEXT("a legal break among seeds 1..30"), FoundSeed != 0))
		{
			FRbMatchSetup Setup;
			Setup.Mode = ERbMatchMode::HotSeat;
			Setup.Seed = FoundSeed;
			Director->StartMatch(Setup);
			Cmd(*FString::Printf(TEXT("RbPlaceCueBall %.9g %.9g"), PlaceX, PlaceY));
			Cmd(*FString::Printf(TEXT("RbStrike 9 %.9g 0 0 -0.1"), FoundAz));
			CollectShot(*Director, Simulation);
			Test->TestTrue(TEXT("the same legal break in PIE"), Director->GetPhase() == ERbDirectorPhase::AwaitStroke && Director->GetConstraints().PushOutAllowed);
			const int32 Pusher = Director->GetMatchState().Game.Shooter;
			Cmd(TEXT("RbDeclare 1"));
			Test->TestTrue(TEXT("RbDeclare 1: push-out declared"), Director->GetDeclaration().Kind == rb::rules::ShotKind::PushOut);
			const rb::Vec3 Cue = Director->GetTableState().Balls[0].State.Position;
			const float PushAz = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Cue.y, -Cue.x)));
			Test->AddInfo(FString::Printf(TEXT("legal break: seed %lld, place %.9g %.9g, break azimuth %.9g deg, push-out azimuth %.9g deg"), FoundSeed, PlaceX,
				PlaceY, FoundAz, PushAz));
			Cmd(*FString::Printf(TEXT("RbStrike 0.35 %.9g 0 0 0"), PushAz));
			CollectShot(*Director, Simulation);
			if (Test->TestEqual(TEXT("push-out -> decision"), Director->GetPhase(), ERbDirectorPhase::AwaitDecision))
			{
				Cmd(TEXT("RbChoose 7"));
				Test->TestEqual(TEXT("RbChoose with no such option refused"), Director->GetPhase(), ERbDirectorPhase::AwaitDecision);
				Test->TestTrue(TEXT("option 1 = pass back"), Director->GetMatchState().PendingOutcome.Options.Size() == 2 &&
					Director->GetMatchState().PendingOutcome.Options[1] == rb::rules::Option::PassBack);
				Cmd(TEXT("RbChoose 1"));
				Test->TestTrue(TEXT("RbChoose 1: passed back, the pusher shoots"), Director->GetPhase() == ERbDirectorPhase::AwaitStroke &&
					Director->GetMatchState().Game.Shooter == Pusher);
			}
		}
		ToShotPhase(*Director);
		const int32 Rack = Director->GetMatchState().RackNumber;
		Cmd(TEXT("RbRerack"));
		Test->TestTrue(TEXT("RbRerack: a new rack"), Director->GetMatchState().RackNumber == Rack + 1 && Director->GetPhase() == ERbDirectorPhase::AwaitPlacement);
		Cmd(TEXT("RbNewMatch 0"));
		Test->TestTrue(TEXT("RbNewMatch 0: practice"), Director->GetSetup().Mode == ERbMatchMode::Practice && Director->GetMatchShotIndex() == 0 &&
			Director->GetPhase() == ERbDirectorPhase::AwaitPlacement);
		Cmd(TEXT("RbNewMatch 1"));
		Test->TestEqual(TEXT("RbNewMatch 1: hot-seat"), Director->GetSetup().Mode, ERbMatchMode::HotSeat);
		return true;
	}

	FAutomationTestBase* Test;
	int32 Stage = 0;
	int32 LastStage = -1;
	double StageStart = 0.0;
	int32 FollowFrames = 0;
	int32 WatchdogGaps = 0;
	double TimeoutStart = 0.0;
	int32 LiveFramesChecked = 0;
	int32 ReplayFramesChecked = 0;
	int32 FrameMismatches = 0;
	double LastLiveSampleTime = -1.0;
	double LastReplaySampleTime = -1.0;

	UWorld* World = nullptr;
	ARbGameMode* Mode = nullptr;
	URbMatchDirector* Director = nullptr;
	ARbPlayerController* PC = nullptr;
	ARbBallSet* Balls = nullptr;
	URbShotPlaybackComponent* Playback = nullptr;
	URbReplaySubsystem* Replay = nullptr;
	URbSimulationSubsystem* Simulation = nullptr;
	ARbPlayerCharacter* Character = nullptr;
	URbStrokeComponent* Stroke = nullptr;

	TWeakObjectPtr<URbShotPlaybackComponent> BoundPlayback;
	FDelegateHandle FinishedHandle;
	TArray<RbReplayFlow::FEndState> Ends;
	TSharedPtr<const FRbShot> LiveShot;
	TSharedPtr<const FRbShot> CommittedShot;
	int32 ShotsBefore = 0;
	uint32 IndexBefore = 0;
	uint32 CommittedIndex = 0;
	ERbDirectorPhase CommittedPhase = ERbDirectorPhase::Idle;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbReplayFunctionalTest, "RawBreak.Functional.Replay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbReplayFunctionalTest::RunTest(const FString& Parameters)
{
	AutomationOpenMap(TEXT("/Engine/Maps/Entry"));
	ADD_LATENT_AUTOMATION_COMMAND(FRbReplaySetGameModeCommand(ARbGameMode::StaticClass()));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbReplayWaitForMatchCommand(this, 20.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbReplayFlowCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FRbReplaySetGameModeCommand(nullptr));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
