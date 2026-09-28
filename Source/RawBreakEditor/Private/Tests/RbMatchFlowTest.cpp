// RawBreak.Functional.MatchFlow (Docs/ue-architecture.md 12 A4, 13 UE-6b): PIE with ARbGameMode (game mode override on the
// engine's Entry map, no project asset needed) and the complete wiring of 5.1 - table found / spawned with its context, ball
// set and cue attached to the cloth origin, the pawn's stroke component handed to the director, the simulation and replay
// subsystems of the play world. The match is driven through the stroke component's own events (OnCueBallPlaced,
// OnStrokeContact: the human layer path) and scripted strikes, at live playback rate 0 (review R-15), except the R-07 check
// that runs one shot at rate 1 into PlayingBack and fires the playback component's OnFinished by hand:
//   rack -> illegal placement refused -> placement -> break (ExecuteStroke -> subsystem -> rules) -> turn logic
//   scratch -> ball in hand to the opponent; 9 pocketed wins -> RackOver -> Confirm; a finished replay never commits (R-07);
//   a scripted strike locks a component that did not make the contact; a rate change reaches the live shot (0 = commit now).
// Owner: UE-6b.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"

#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Cue/RbCue.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#include "rb/Rules/Evaluate.h"

#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbMatchFlow
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
	bool WaitForHandOff(const URbMatchDirector& Director, URbSimulationSubsystem* Simulation)
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

	// Exactly these balls on the table (0 = cue ball, in position).
	void SetLayout(URbMatchDirector& Director, std::initializer_list<TPair<int32, rb::Vec2>> Layout)
	{
		FRbTableState State = Director.GetTableState();
		for (rb::SimBall& Ball : State.Balls)
		{
			Ball.InPlay = false;
		}
		for (const TPair<int32, rb::Vec2>& At : Layout)
		{
			State.Balls[At.Key].InPlay = true;
			State.Balls[At.Key].State.Position = rb::Vec3(At.Value.x, At.Value.y, Director.GetTableContext()->BallRadius(At.Key));
		}
		Director.SetTableStateForTest(State);
	}

	// Leaves a decision / rack over through Confirm until a shot can be set up.
	void ToShotPhase(URbMatchDirector& Director)
	{
		for (int32 Try = 0; Try < 4 && Director.GetPhase() != ERbDirectorPhase::AwaitStroke && Director.GetPhase() != ERbDirectorPhase::AwaitPlacement; ++Try)
		{
			Director.Confirm();
		}
	}

	bool TableStateInSync(const URbMatchDirector& Director)
	{
		const rb::rules::GameState& G = Director.GetMatchState().Game;
		const FRbTableState& T = Director.GetTableState();
		for (int32 Id = 1; Id < rb::rules::kRulesBallCount; ++Id)
		{
			const bool bOn = G.Balls[Id].Kind == rb::rules::BallStatusKind::OnTable;
			if (T.Balls[Id].InPlay != bOn || (bOn && (T.Balls[Id].State.Position.x != G.Balls[Id].Position.x || T.Balls[Id].State.Position.y != G.Balls[Id].Position.y)))
			{
				return false;
			}
		}
		return true;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRbSetGameModeOverrideCommand, UClass*, GameModeClass);

bool FRbSetGameModeOverrideCommand::Update()
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (AWorldSettings* Settings = EditorWorld ? EditorWorld->GetWorldSettings() : nullptr)
	{
		Settings->DefaultGameMode = GameModeClass; // transient: the engine map is never saved
	}
	return true;
}

class FRbWaitForMatchCommand : public IAutomationLatentCommand
{
public:
	FRbWaitForMatchCommand(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* GameMode = RbMatchFlow::GameMode();
		const URbMatchDirector* Director = GameMode ? GameMode->GetDirector() : nullptr;
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

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRbMatchFlowCommand, FAutomationTestBase*, Test);

bool FRbMatchFlowCommand::Update()
{
	using namespace RbMatchFlow;
	UWorld* World = PlayWorld();
	ARbGameMode* Mode = GameMode();
	URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
	if (!Test->TestNotNull(TEXT("ARbGameMode in PIE"), Mode) || !Test->TestNotNull(TEXT("director"), Director))
	{
		return true;
	}

	// --- scene setup (5.1) ---------------------------------------------------------------------------------------------
	ARbTable* Table = Mode->GetTable();
	ARbBallSet* Balls = Mode->GetBallSet();
	ARbCue* Cue = Mode->GetCue();
	if (!Test->TestTrue(TEXT("table with its context"), Table && Table->HasContext()) || !Test->TestNotNull(TEXT("ball set"), Balls) ||
		!Test->TestNotNull(TEXT("cue"), Cue))
	{
		return true;
	}
	Test->TestTrue(TEXT("director uses the table's context"), Director->GetTableContext() == Table->GetContextPtr());
	Test->TestTrue(TEXT("ball set initialised for the table"), Balls->GetTable() == Table &&
		Balls->GetRootComponent()->GetAttachParent() == Table->GetClothOrigin());
	Test->TestTrue(TEXT("cue attached to the cloth origin and hidden"), Cue->GetRootComponent()->GetAttachParent() == Table->GetClothOrigin() &&
		Cue->GetDrive() == ERbCueDrive::Hidden);
	APlayerController* PC = World->GetFirstPlayerController();
	ARbPlayerCharacter* Character = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
	URbStrokeComponent* Stroke = Character ? Character->GetStroke() : nullptr;
	if (!Test->TestNotNull(TEXT("ARbPlayerCharacter with a stroke component"), Stroke))
	{
		return true;
	}
	Test->TestTrue(TEXT("the pawn's stroke component is wired to the director"), Director->GetStrokeComponent() == Stroke);
	URbSimulationSubsystem* Simulation = World->GetSubsystem<URbSimulationSubsystem>();
	URbReplaySubsystem* Replay = World->GetSubsystem<URbReplaySubsystem>();
	URbShotPlaybackComponent* Playback = Balls->GetPlayback();
	if (!Test->TestNotNull(TEXT("simulation subsystem"), Simulation) || !Test->TestNotNull(TEXT("replay subsystem"), Replay) ||
		!Test->TestNotNull(TEXT("playback component"), Playback))
	{
		return true;
	}
	Test->TestTrue(TEXT("default match: practice 9-ball, break ball in hand"), Director->GetSetup().Mode == ERbMatchMode::Practice &&
		Director->GetPhase() == ERbDirectorPhase::AwaitPlacement && Stroke->GetPhase() == ERbStrokePhase::PlacingCueBall);

	// --- hot-seat match at playback rate 0 ---------------------------------------------------------------------------------
	Director->SetLivePlaybackRate(0.0f);
	FRbMatchSetup Setup;
	Setup.Mode = ERbMatchMode::HotSeat;
	Setup.Seed = 11;
	Setup.RaceTo = 3;
	if (!Test->TestTrue(TEXT("hot-seat match"), Director->StartMatch(Setup)))
	{
		return true;
	}
	const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
	const double R = Director->GetTableContext()->BallRadius(0);
	const int32 ShotsBefore = Replay->GetShotCount();

	// Ball in hand through the stroke component's event (world position -> core): illegal first, then legal.
	Stroke->OnCueBallPlaced.Broadcast(Table->CoreToWorld(rb::Vec3(0.3, 0.1, R)));
	Test->TestTrue(TEXT("placement below the head string refused"), Director->GetPhase() == ERbDirectorPhase::AwaitPlacement &&
		Stroke->GetPhase() == ERbStrokePhase::PlacingCueBall);
	Stroke->OnCueBallPlaced.Broadcast(Table->CoreToWorld(rb::Vec3(Rules.HeadStringX - 0.12, 0.08, R)));
	Test->TestTrue(TEXT("legal placement accepted -> AwaitStroke"), Director->GetPhase() == ERbDirectorPhase::AwaitStroke);
	Test->TestTrue(TEXT("placed where the world position says"), FMath::IsNearlyEqual(Director->GetPlacedCueBall().x, Rules.HeadStringX - 0.12, 1e-9) &&
		FMath::IsNearlyEqual(Director->GetPlacedCueBall().y, 0.08, 1e-9));
	Test->TestTrue(TEXT("stroke component addresses the placed cue ball"), Stroke->GetPhase() == ERbStrokePhase::Walking);

	// Break through the stroke component's contact event: human layer (guest profile) -> simulation subsystem -> rules.
	const rb::Vec3 CueBall = Director->GetTableState().Balls[0].State.Position;
	const rb::Vec3 Apex = Director->GetTableState().Balls[1].State.Position;
	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = FMath::Atan2(Apex.y - CueBall.y, Apex.x - CueBall.x);
	Commit.Intended.Speed = 9.0;
	Commit.Intended.AxisOffsetB = -0.1;
	Commit.Intended.TimeDown = 4.0;
	Commit.Intended.ForwardStart = 3.7;
	Commit.ContactTime = FPlatformTime::Seconds();
	Commit.EyeTransform = Character->GetActorTransform();
	Stroke->OnStrokeContact.Broadcast(Commit);
	Test->TestTrue(TEXT("hand-off"), WaitForHandOff(*Director, Simulation));
	const FRbLastShotSummary& Break = Director->GetLastShot();
	const TSharedPtr<const FRbShot> BreakShot = Director->GetLastCommittedShot();
	Test->AddInfo(FString::Printf(TEXT("break: next %d fouls %08x rule %s first %d, %d pocketed, sim %.2f ms, phase %d"), static_cast<int32>(Break.Next),
		Break.Fouls.Bits, *Break.RuleRef, Break.FirstContactBall, Break.Pocketed.Num(), Break.SimMilliseconds, static_cast<int32>(Director->GetPhase())));
	Test->TestTrue(TEXT("the break was simulated and committed"), Break.bValid && Director->GetMatchShotIndex() == 1 && BreakShot.IsValid());
	Test->TestTrue(TEXT("human-layer stroke with the shooter's view"), BreakShot.IsValid() && BreakShot->Request.Stroke.bHuman &&
		BreakShot->Request.Input.Context.InHand == rb::CueBallInHand::AboveHeadString);
	Test->TestTrue(TEXT("committed shot recorded for replays"), Replay->GetShotCount() == ShotsBefore + 1 && Replay->GetShot(0) == BreakShot);
	Test->TestTrue(TEXT("table state in sync with the rules"), TableStateInSync(*Director));
	const rb::rules::MatchState& State = Director->GetMatchState();
	switch (Break.Next)
	{
	case rb::rules::NextAction::Continue:
		Test->TestTrue(TEXT("breaker continues"), State.Game.Shooter == 0 && Director->GetPhase() == ERbDirectorPhase::AwaitStroke);
		break;
	case rb::rules::NextAction::Pass:
		Test->TestTrue(TEXT("opponent shoots (in hand after a foul)"), State.Game.Shooter == 1 &&
			Director->GetPhase() == (Break.Fouls.Bits ? ERbDirectorPhase::AwaitPlacement : ERbDirectorPhase::AwaitStroke));
		break;
	case rb::rules::NextAction::AwaitDecision:
		Test->TestTrue(TEXT("opponent decides"), State.Decider == 1 && Director->GetPhase() == ERbDirectorPhase::AwaitDecision);
		break;
	default:
		Test->TestTrue(TEXT("rack won on the break"), Director->GetPhase() == ERbDirectorPhase::RackOver);
		break;
	}
	const ERbStrokePhase Expected = Director->GetPhase() == ERbDirectorPhase::AwaitStroke ? ERbStrokePhase::Walking
		: (Director->GetPhase() == ERbDirectorPhase::AwaitPlacement ? ERbStrokePhase::PlacingCueBall : ERbStrokePhase::Locked);
	Test->TestTrue(TEXT("stroke component re-armed for the next shooter"), Stroke->GetPhase() == Expected);

	// Scratch -> ball in hand to the opponent.
	ToShotPhase(*Director);
	const rb::PocketGeometry& Corner = Director->GetTableContext()->Geometry.Pockets[static_cast<int32>(rb::PocketId::FootRight)];
	SetLayout(*Director, {{0, Corner.MouthMid - Corner.Axis * 0.3}, {1, rb::Vec2(-0.9, 0.4)}, {9, rb::Vec2(-0.7, 0.35)}});
	const int32 Scratcher = Director->GetMatchState().Game.Shooter;
	Test->TestTrue(TEXT("scratch shot"), Director->SubmitScriptedStrike(1.5, FMath::Atan2(Corner.Axis.y, Corner.Axis.x), 0.0, 0.0, 0.0) &&
		WaitForHandOff(*Director, Simulation));
	Test->TestTrue(TEXT("scratch: foul, ball in hand anywhere for the opponent"), Director->GetLastShot().Fouls.Has(rb::rules::Foul::CueBallScratch) &&
		Director->GetMatchState().Game.Shooter == 1 - Scratcher && Director->GetPhase() == ERbDirectorPhase::AwaitPlacement &&
		Director->GetConstraints().PlacementRegion == rb::rules::CueBallNext::InHandAnywhere && Stroke->GetPhase() == ERbStrokePhase::PlacingCueBall);
	Stroke->OnCueBallPlaced.Broadcast(Table->CoreToWorld(rb::Vec3(-0.9, 0.4, R)));
	Test->TestTrue(TEXT("placement on the 1 refused"), Director->GetPhase() == ERbDirectorPhase::AwaitPlacement);

	// The 9 pocketed wins the rack.
	const rb::Vec2 Nine = Corner.MouthMid - Corner.Axis * 0.3;
	SetLayout(*Director, {{0, Nine - Corner.Axis * 0.25}, {9, Nine}});
	const int32 Winner = Director->GetMatchState().Game.Shooter;
	const int32 WinsBefore = Director->GetMatchState().RackWins[Winner];
	Test->TestTrue(TEXT("9 shot"), Director->SubmitScriptedStrike(2.0, FMath::Atan2(Corner.Axis.y, Corner.Axis.x), 0.0, 0.0, -0.35) &&
		WaitForHandOff(*Director, Simulation));
	Test->TestTrue(TEXT("9 pocketed wins the rack -> RackOver, stroke locked"), Director->GetPhase() == ERbDirectorPhase::RackOver &&
		Director->GetMatchState().RackWins[Winner] == WinsBefore + 1 && Stroke->GetPhase() == ERbStrokePhase::Locked);
	Test->TestTrue(TEXT("Confirm racks again"), Director->Confirm() && Director->GetPhase() == ERbDirectorPhase::AwaitPlacement &&
		Director->GetMatchState().RackNumber == 2);

	// --- R-07: the playback component is shared by live shots and replays ---------------------------------------------------
	Director->SetLivePlaybackRate(1.0f);
	SetLayout(*Director, {{0, rb::Vec2(-0.6, 0.0)}, {1, rb::Vec2(0.3, 0.1)}, {9, rb::Vec2(0.6, -0.3)}});
	const uint32 ShotIndex = Director->GetMatchShotIndex();
	Test->TestTrue(TEXT("live shot at rate 1"), Director->SubmitScriptedStrike(1.0, 0.1, 0.0, 0.0, 0.0) && WaitForHandOff(*Director, Simulation));
	const TSharedPtr<const FRbShot> Pending = Director->GetPendingShot();
	Test->TestTrue(TEXT("PlayingBack, not yet committed"), Director->GetPhase() == ERbDirectorPhase::PlayingBack && Pending.IsValid() &&
		Director->GetMatchShotIndex() == ShotIndex && Cue->GetDrive() == ERbCueDrive::Playback);
	Test->TestTrue(TEXT("a scripted strike locks a component that did not make the contact"), Stroke->GetPhase() == ERbStrokePhase::Locked);
	Test->TestFalse(TEXT("no replay while a live shot plays back"), Director->IsReplayAllowed());
	const TSharedPtr<const FRbShot> Replayed = Replay->GetShot(0);
	if (Test->TestTrue(TEXT("a recorded shot to replay"), Replayed.IsValid() && Pending.IsValid() && Replayed != Pending))
	{
		Playback->OnFinished.Broadcast(Replayed.ToSharedRef());
		Test->TestTrue(TEXT("a finished replay never commits (R-07)"), Director->GetPhase() == ERbDirectorPhase::PlayingBack &&
			Director->GetMatchShotIndex() == ShotIndex && Director->GetPendingShot() == Pending);
		Playback->OnFinished.Broadcast(Pending.ToSharedRef());
		Test->TestTrue(TEXT("the pending live shot commits when its playback ends"), Director->GetPhase() != ERbDirectorPhase::PlayingBack &&
			Director->GetMatchShotIndex() == ShotIndex + 1 && Cue->GetDrive() == ERbCueDrive::Hidden);
		const ERbDirectorPhase After = Director->GetPhase();
		Playback->OnFinished.Broadcast(Pending.ToSharedRef());
		Test->TestTrue(TEXT("a second OnFinished of the same shot changes nothing"), Director->GetPhase() == After &&
			Director->GetMatchShotIndex() == ShotIndex + 1);
	}

	// --- a rate change reaches the live shot that is playing (review fix) ------------------------------------------------------
	ToShotPhase(*Director);
	SetLayout(*Director, {{0, rb::Vec2(-0.6, 0.0)}, {1, rb::Vec2(0.3, 0.1)}, {9, rb::Vec2(0.6, -0.3)}});
	const uint32 RateShotIndex = Director->GetMatchShotIndex();
	Test->TestTrue(TEXT("live shot at rate 1"), Director->SubmitScriptedStrike(1.0, 0.1, 0.0, 0.0, 0.0) && WaitForHandOff(*Director, Simulation));
	const TSharedPtr<const FRbShot> Live = Director->GetPendingShot();
	if (Test->TestTrue(TEXT("playing back"), Director->GetPhase() == ERbDirectorPhase::PlayingBack && Live.IsValid() && Playback->GetShot() == Live))
	{
		Director->SetLivePlaybackRate(0.5f);
		Test->TestTrue(TEXT("rate 0.5: the live shot keeps playing (slower)"), Director->GetPhase() == ERbDirectorPhase::PlayingBack &&
			Director->GetPendingShot() == Live && Playback->GetShot() == Live && Director->GetMatchShotIndex() == RateShotIndex);
		Director->SetLivePlaybackRate(0.0f);
		Test->TestTrue(TEXT("rate 0 during the playback: snapped to its end and committed"), Director->GetPhase() != ERbDirectorPhase::PlayingBack &&
			!Playback->IsPlaying() && Director->GetMatchShotIndex() == RateShotIndex + 1 && Director->GetLastCommittedShot() == Live &&
			Replay->GetShot(0) == Live);
		Test->TestTrue(TEXT("table state in sync after the early commit"), TableStateInSync(*Director));
		const ERbDirectorPhase After = Director->GetPhase();
		Playback->OnFinished.Broadcast(Live.ToSharedRef());
		Test->TestTrue(TEXT("a late OnFinished of that shot changes nothing"), Director->GetPhase() == After &&
			Director->GetMatchShotIndex() == RateShotIndex + 1);
	}
	Director->SetLivePlaybackRate(0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchFlowTest, "RawBreak.Functional.MatchFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbMatchFlowTest::RunTest(const FString& Parameters)
{
	AutomationOpenMap(TEXT("/Engine/Maps/Entry"));
	ADD_LATENT_AUTOMATION_COMMAND(FRbSetGameModeOverrideCommand(ARbGameMode::StaticClass()));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbWaitForMatchCommand(this, 20.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbMatchFlowCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FRbSetGameModeOverrideCommand(nullptr));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
