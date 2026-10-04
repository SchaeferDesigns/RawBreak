// RawBreak.Functional.M2Integration.{DiveBar,TestRoom} (integration round M2, Docs/ue-architecture.md 18.10 / 18.12): the merged
// packages together in the two playable venues (PIE, the level's own ARbGameMode, the player's table, live playback x4):
//   1. a complete 9-ball practice rack through the match director: the break, shots planned by simulating candidate strikes on the
//      director's table state (scripted strikes, no human layer: the rack is about the venue, the rules and the presentation), ball in
//      hand after fouls, decisions confirmed, until a rack is won (several racks if one is lost on fouls);
//   2. the audio of every shot (M2-C on M2-E's / M1's playback): the table's audio plan of each live shot is built once, and every
//      physical impact event of the shot (cue strike, tip recontact, ball-ball, cushion / jaw / rail top, slate, liner / pocket rim,
//      pocketed) is either scheduled as a sound of the plan (FRbAudioPlanImpact::SourceEvent) or counted as silent by design
//      (NumSkippedEvents: pressing / too slow contacts); no sound refers to an event that is not an impact;
//   3. a ball off the table (M2-E): a layout + strike planned on the director (the 9 jumps a side rail, the cue ball stays) - the 9 is
//      handed to engine physics, lands, comes to rest grounded on the venue's floor outside the table, and every engine hit of it is
//      sounded (URbAudioSubsystem floor hits = OnImpact count); the foul spots the 9 on the foot spot / long string; ball in hand
//      placed and the next shot addressed returns the loose 9 (Address) and the table shows the spotted 9 again.
// Needs an audio device: run with `rbue.py test --sound` (as the whole suite since the M2-C merge). Owner: M2-0 (integration).

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

#include "Audio/RbAudioPlan.h"
#include "Audio/RbAudioSubsystem.h"
#include "Audio/RbTableAudioComponent.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBall.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Core/RbAssetPaths.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbShot.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

#include "rb/Physics/ShotResult.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbM2IntegrationTest
{
	constexpr int32 kNine = 9;
	constexpr int32 kMaxShots = 70;
	constexpr int32 kMaxRacks = 3;
	constexpr float kLiveRate = 4.0f;
	constexpr double kStageTimeout = 90.0;

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	ARbGameMode* GameMode()
	{
		UWorld* World = PlayWorld();
		return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
	}

	struct FStrike
	{
		double Speed = 0.0;
		double PhiDeg = 0.0;
		double ElevationDeg = 0.0;
		double B = 0.0;
	};

	// The scripted strike's simulation on a table state (built like URbMatchDirector::BuildShotInput).
	TSharedRef<FRbShot> Simulate(const URbMatchDirector& Director, const FRbTableState& Table, const FStrike& S)
	{
		FRbShotRequest Request;
		Request.Table = Director.GetTableContext();
		RbShot::InitSimInput(*Request.Table, Request.Input);
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			Request.Input.Balls[Id] = Table.Balls[Id];
		}
		Request.Input.Context.FrozenTolerance = Director.GetMatchConfig().Rules.Tolerances.Frozen;
		rb::StrikeRequest Strike;
		Strike.Ball = static_cast<rb::BallId>(rb::kCueBallId);
		Strike.Input.Speed = S.Speed;
		Strike.Input.Azimuth = FMath::DegreesToRadians(S.PhiDeg);
		Strike.Input.Elevation = FMath::DegreesToRadians(S.ElevationDeg);
		Strike.Input.OffsetA = 0.0;
		Strike.Input.OffsetB = S.B;
		Strike.Input.Cue = Director.GetShooter(Director.GetActivePlayer()).Cue;
		Request.Input.Strikes.PushBack(Strike);
		return URbSimulationSubsystem::RunShotBlocking(MoveTemp(Request));
	}

	int32 FirstCueBallContact(const rb::ShotResult& Result)
	{
		for (const rb::ShotEvent& Event : Result.Events)
		{
			if (Event.Type == rb::ShotEventType::BallBall && (Event.A == rb::kCueBallId || Event.B == rb::kCueBallId))
			{
				return Event.A == rb::kCueBallId ? Event.B : Event.A;
			}
		}
		return -1;
	}

	bool HasEvent(const rb::ShotResult& Result, rb::ShotEventType Type)
	{
		for (const rb::ShotEvent& Event : Result.Events)
		{
			if (Event.Type == Type)
			{
				return true;
			}
		}
		return false;
	}

	bool LeavesTable(const rb::ShotResult& Result)
	{
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			if (Result.Finals[Id].Status == rb::BallFinalStatus::OffTable)
			{
				return true;
			}
		}
		return false;
	}

	int32 LowestBall(const FRbTableState& Table)
	{
		for (int32 Id = 1; Id < rb::kMaxBalls; ++Id)
		{
			if (Table.Balls[Id].InPlay)
			{
				return Id;
			}
		}
		return -1;
	}

	// A clean pot: the lowest ball first, it drops, the cue ball stays, no double hit, nothing leaves the table.
	bool Pots(const rb::ShotResult& Result, int32 Target)
	{
		return Result.Status == rb::SimStatus::Ok && FirstCueBallContact(Result) == Target && Result.Finals[Target].Status == rb::BallFinalStatus::Pocketed &&
			Result.Finals[rb::kCueBallId].Status == rb::BallFinalStatus::OnTable && !HasEvent(Result, rb::ShotEventType::TipRecontact) && !LeavesTable(Result);
	}

	// The widest window of angles (0.1 deg steps around the ghost-ball line of each pocket) that pots the lowest ball.
	bool PlanPot(const URbMatchDirector& Director, const FRbTableState& Table, FStrike& Out, int32& OutWidth)
	{
		const int32 Target = LowestBall(Table);
		if (Target < 0 || !Table.Balls[rb::kCueBallId].InPlay)
		{
			return false;
		}
		const FRbTableContext& Context = *Director.GetTableContext();
		const double R = Context.BallRadius(Target);
		const double RCue = Context.BallRadius(rb::kCueBallId);
		const rb::Vec2 Cue = rb::XY(Table.Balls[rb::kCueBallId].State.Position);
		const rb::Vec2 Object = rb::XY(Table.Balls[Target].State.Position);
		OutWidth = 0;
		for (int32 P = 0; P < static_cast<int32>(Context.Geometry.Pockets.Size()); ++P)
		{
			const rb::Vec2 U = rb::Normalized(Context.Geometry.Pockets[P].MouthMid - Object);
			const rb::Vec2 Ghost = Object - U * (R + RCue);
			const rb::Vec2 ToGhost = Ghost - Cue;
			if (rb::Length(ToGhost) < 1.0e-3)
			{
				continue;
			}
			const double CutDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(rb::Dot(rb::Normalized(ToGhost), U), -1.0, 1.0)));
			if (CutDeg > 70.0)
			{
				continue;
			}
			const double GhostDeg = FMath::RadiansToDegrees(FMath::Atan2(ToGhost.y, ToGhost.x));
			for (const double Speed : {2.0, 3.0})
			{
				for (const double B : {-0.3, 0.0})
				{
					int32 Run = 0, BestRun = 0, BestEnd = 0;
					for (int32 K = -20; K <= 20; ++K)
					{
						const FStrike S{Speed, GhostDeg + 0.1 * K, 0.0, B};
						if (Pots(Simulate(Director, Table, S)->Result, Target))
						{
							if (++Run > BestRun)
							{
								BestRun = Run;
								BestEnd = K;
							}
						}
						else
						{
							Run = 0;
						}
					}
					if (BestRun > OutWidth)
					{
						OutWidth = BestRun;
						Out = FStrike{Speed, GhostDeg + 0.1 * (BestEnd - (BestRun - 1) / 2), 0.0, B};
					}
				}
			}
		}
		return OutWidth > 0;
	}

	// Any strike that pockets something legally (first contact the lowest ball, the cue ball stays): a coarse 360 deg sweep.
	bool PlanAnyPot(const URbMatchDirector& Director, const FRbTableState& Table, FStrike& Out)
	{
		const int32 Target = LowestBall(Table);
		for (const double Speed : {2.5, 4.0})
		{
			for (int32 Deg = 0; Deg < 360; ++Deg)
			{
				const FStrike S{Speed, static_cast<double>(Deg), 0.0, -0.2};
				const rb::ShotResult& Result = Simulate(Director, Table, S)->Result;
				if (Result.Status != rb::SimStatus::Ok || FirstCueBallContact(Result) != Target || Result.Finals[rb::kCueBallId].Status != rb::BallFinalStatus::OnTable ||
					HasEvent(Result, rb::ShotEventType::TipRecontact) || LeavesTable(Result))
				{
					continue;
				}
				for (int32 Id = 1; Id < rb::kMaxBalls; ++Id)
				{
					if (Table.Balls[Id].InPlay && Result.Finals[Id].Status == rb::BallFinalStatus::Pocketed)
					{
						Out = S;
						return true;
					}
				}
			}
		}
		return false;
	}

	// Ball in hand: a spot behind the lowest ball on the line to a pocket (a straight-in that pots), else the first legal spot of a grid.
	bool PlanPlacement(const URbMatchDirector& Director, const FRbTableState& Table, rb::Vec2& OutPlace)
	{
		const FRbTableContext& Context = *Director.GetTableContext();
		const int32 Target = LowestBall(Table);
		if (Target > 0)
		{
			const double R = Context.BallRadius(Target);
			const double RCue = Context.BallRadius(rb::kCueBallId);
			const rb::Vec2 Object = rb::XY(Table.Balls[Target].State.Position);
			for (int32 P = 0; P < static_cast<int32>(Context.Geometry.Pockets.Size()); ++P)
			{
				const rb::Vec2 U = rb::Normalized(Context.Geometry.Pockets[P].MouthMid - Object);
				for (const double Gap : {0.15, 0.25, 0.35})
				{
					const rb::Vec2 Exact = Object - U * (R + RCue + Gap);
					const rb::Vec2 Place(static_cast<float>(Exact.x), static_cast<float>(Exact.y));
					if (!Director.CanPlaceCueBall(Place))
					{
						continue;
					}
					FRbTableState Hypothesis = Table;
					rb::SimBall& CueBall = Hypothesis.Balls[rb::kCueBallId];
					CueBall.InPlay = true;
					CueBall.State = rb::BallState();
					CueBall.State.Position = rb::Vec3(Place.x, Place.y, CueBall.Spec.Radius);
					FStrike Strike;
					int32 Width = 0;
					if (PlanPot(Director, Hypothesis, Strike, Width) && Width >= 3)
					{
						OutPlace = Place;
						return true;
					}
				}
			}
		}
		const double HalfL = 0.5 * Context.Spec.Length, HalfW = 0.5 * Context.Spec.Width;
		for (double X = -0.6; X <= 0.61; X += 0.2)
		{
			for (double Y = -0.6; Y <= 0.61; Y += 0.3)
			{
				const rb::Vec2 Place(static_cast<float>(X * HalfL), static_cast<float>(Y * HalfW));
				if (Director.CanPlaceCueBall(Place))
				{
					OutPlace = Place;
					return true;
				}
			}
		}
		return false;
	}

	// Event types that are physical impacts (audio.md 1.1 / RbAudioPlan.cpp): each one sounds or is silent by design.
	bool IsImpactEvent(const rb::ShotEvent& E)
	{
		const bool bA = E.A >= 0 && E.A < rb::kMaxBalls;
		switch (E.Type)
		{
		case rb::ShotEventType::CueStrike:
			return bA;
		case rb::ShotEventType::TipRecontact:
			return bA && FMath::Abs(E.NormalImpulse) > 0.0;
		case rb::ShotEventType::BallBall:
			return bA && E.B >= 0 && E.B < rb::kMaxBalls;
		case rb::ShotEventType::BallCushion:
		case rb::ShotEventType::BallJaw:
		case rb::ShotEventType::BallRailTop:
		case rb::ShotEventType::BallSlate:
		case rb::ShotEventType::BallLiner:
		case rb::ShotEventType::BallPocketRim:
		case rb::ShotEventType::BallPocketed:
			return bA;
		default:
			return false;
		}
	}

	// Totals over the run (logged at the end).
	struct FAudioTotals
	{
		int32 Shots = 0;
		int32 Events = 0;
		int32 Sounded = 0;
		int32 Silent = 0;
		int32 External = 0; // lamp contacts: no plan sound in audio v1 (logged)
	};
}

class FRbM2IntegrationWaitForMatch : public IAutomationLatentCommand
{
public:
	FRbM2IntegrationWaitForMatch(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* Mode = RbM2IntegrationTest::GameMode();
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

class FRbM2IntegrationCommand : public IAutomationLatentCommand
{
public:
	FRbM2IntegrationCommand(FAutomationTestBase* InTest, const TCHAR* InVenue, int64 InSeed, EPhysicalSurface InFloorSurface)
		: Test(InTest), Venue(InVenue), Seed(InSeed), FloorSurface(InFloorSurface) {}

	virtual ~FRbM2IntegrationCommand() override
	{
		Unbind();
	}

	virtual bool Update() override
	{
		const ARbGameMode* Current = RbM2IntegrationTest::GameMode();
		if (!Current || !Current->GetDirector() || Current->GetDirector()->GetPhase() == ERbDirectorPhase::Idle)
		{
			if (GetCurrentRunTime() > 300.0)
			{
				Test->AddError(TEXT("no PIE match"));
				return Finish();
			}
			return false;
		}
		if (!Resolve())
		{
			return Finish();
		}
		if (Stage != EStage::Setup && SetupWorld.Get() != RbM2IntegrationTest::PlayWorld())
		{
			if (++Restarts > 2)
			{
				Test->AddError(TEXT("the play world keeps changing"));
				return Finish();
			}
			Test->AddInfo(TEXT("new PIE world: starting over"));
			Unbind();
			Stage = EStage::Setup;
		}
		bool bDone = false;
		switch (Stage)
		{
		case EStage::Setup: bDone = StageSetup(); break;
		case EStage::Rack: bDone = StageRack(); break;
		case EStage::WaitShot: bDone = StageWaitShot(); break;
		case EStage::OffTablePlan: bDone = StageOffTablePlan(); break;
		case EStage::OffTableShot: bDone = StageOffTableShot(); break;
		case EStage::OffTableRest: bDone = StageOffTableRest(); break;
		case EStage::Respot: bDone = StageRespot(); break;
		case EStage::Finish:
			LogTotals();
			return Finish();
		}
		if (bDone)
		{
			return Finish();
		}
		if (Stage != LastStage)
		{
			LastStage = Stage;
			StageStart = FPlatformTime::Seconds();
			bStageStarted = false;
		}
		else if (FPlatformTime::Seconds() - StageStart > RbM2IntegrationTest::kStageTimeout)
		{
			Test->AddError(FString::Printf(TEXT("%s: stage %d timed out (phase %d, loose balls %d)"), Venue, static_cast<int32>(Stage),
				static_cast<int32>(Director->GetPhase()), Loose->GetNumLooseBalls()));
			return Finish();
		}
		return false;
	}

private:
	enum class EStage : uint8
	{
		Setup,
		Rack,
		WaitShot,
		OffTablePlan,
		OffTableShot,
		OffTableRest,
		Respot,
		Finish,
	};

	bool Resolve()
	{
		UWorld* World = RbM2IntegrationTest::PlayWorld();
		Mode = RbM2IntegrationTest::GameMode();
		Director = Mode ? Mode->GetDirector() : nullptr;
		Table = Mode ? Mode->GetTable() : nullptr;
		Balls = Mode ? Mode->GetBallSet() : nullptr;
		Loose = URbLooseBallSubsystem::Get(World);
		Audio = URbAudioSubsystem::Get(World);
		Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
		return Test->TestNotNull(TEXT("ARbGameMode"), Mode) && Test->TestNotNull(TEXT("director"), Director) &&
			Test->TestTrue(TEXT("player table with context"), Table && Table->HasContext()) && Test->TestNotNull(TEXT("ball set"), Balls) &&
			Test->TestNotNull(TEXT("loose-ball subsystem"), Loose) && Test->TestNotNull(TEXT("audio subsystem"), Audio) &&
			Test->TestNotNull(TEXT("replay subsystem"), Replay);
	}

	void Bind()
	{
		Unbind();
		BoundLoose = Loose;
		const TSharedRef<int32> Impacts = LooseImpacts;
		ImpactHandle = Loose->OnImpact.AddLambda([Impacts](const FRbLooseBallImpact&) { ++*Impacts; });
		const TSharedRef<TArray<TPair<int32, ERbLooseBallReturn>>> Shared = Returns;
		ReturnHandle = Loose->OnReturnedWithReason.AddLambda([Shared](int32, int32 BallId, ERbLooseBallReturn Reason) { Shared->Emplace(BallId, Reason); });
	}

	void Unbind()
	{
		if (URbLooseBallSubsystem* Subsystem = BoundLoose.Get())
		{
			Subsystem->OnImpact.Remove(ImpactHandle);
			Subsystem->OnReturnedWithReason.Remove(ReturnHandle);
		}
		BoundLoose.Reset();
	}

	bool Finish()
	{
		if (URbLooseBallSubsystem* Subsystem = BoundLoose.Get())
		{
			Subsystem->UnreachableReturnSeconds = DefaultUnreachableSeconds;
		}
		Unbind();
		return true;
	}

	bool IsSettled() const
	{
		const ERbDirectorPhase Phase = Director->GetPhase();
		return Phase != ERbDirectorPhase::Simulating && Phase != ERbDirectorPhase::PlayingBack && !Replay->IsReplaying();
	}

	URbTableAudioComponent* TableAudio() const
	{
		for (URbTableAudioComponent* Component : Audio->GetTableAudio())
		{
			if (Component && Component->GetTable() == Table)
			{
				return Component;
			}
		}
		return nullptr;
	}

	// The floor under a world point on the loose-ball channel (what loose balls roll on), ignoring the loose balls.
	bool FloorUnder(const FVector& Above, FHitResult& OutHit) const
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RbM2IntegrationFloor), false);
		Params.bReturnPhysicalMaterial = true;
		for (ARbLooseBall* Ball : Loose->GetLooseBalls())
		{
			Params.AddIgnoredActor(Ball);
		}
		UWorld* World = Director->GetWorld();
		return World && World->LineTraceSingleByChannel(OutHit, Above + FVector(0.0, 0.0, 5.0), Above - FVector(0.0, 0.0, 500.0),
			RbAssetPaths::Collision::LooseBallChannel, Params) && OutHit.bBlockingHit;
	}

	void Submit(const RbM2IntegrationTest::FStrike& S, const FString& Kind)
	{
		ShotIndexBefore = Director->GetMatchShotIndex();
		URbTableAudioComponent* Component = TableAudio();
		PlansBefore = Component ? Component->GetPlansBuilt() : 0;
		ShotKind = Kind;
		ShotSent = FPlatformTime::Seconds();
		if (!Director->SubmitScriptedStrike(S.Speed, FMath::DegreesToRadians(S.PhiDeg), FMath::DegreesToRadians(S.ElevationDeg), 0.0, S.B))
		{
			Test->AddError(FString::Printf(TEXT("%s: %s refused: %s"), Venue, *Kind, *Director->GetLastError()));
			bSubmitFailed = true;
		}
	}

	// Every impact event of the last committed shot is sounded by the table's plan or silent by design.
	bool CheckShotAudio(const TCHAR* Label)
	{
		using namespace RbM2IntegrationTest;
		URbTableAudioComponent* Component = TableAudio();
		if (!Test->TestNotNull(*FString::Printf(TEXT("%s: the player's table has its audio component"), Label), Component))
		{
			return false;
		}
		if (Component->IsPlanPending())
		{
			return false; // the worker is still building it
		}
		const TSharedPtr<const FRbShot> Shot = Director->GetLastCommittedShot();
		const TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> Plan = Component->GetLastPlan();
		Test->TestEqual(*FString::Printf(TEXT("%s: one audio plan for the live shot"), Label), Component->GetPlansBuilt(), PlansBefore + 1);
		if (!Test->TestTrue(*FString::Printf(TEXT("%s: shot and plan"), Label), Shot.IsValid() && Plan.IsValid()))
		{
			return true;
		}
		const rb::ShotResult& Result = Shot->Result;
		const int32 NumEvents = static_cast<int32>(Result.Events.size());
		TSet<int32> Sounded;
		int32 BadSource = 0;
		for (const FRbAudioPlanImpact& Impact : Plan->Impacts)
		{
			if (Impact.SourceEvent == INDEX_NONE)
			{
				continue; // a consequence without its own event (the coin-op trap click after a gully run)
			}
			Sounded.Add(Impact.SourceEvent);
			BadSource += (Impact.SourceEvent < 0 || Impact.SourceEvent >= NumEvents || !IsImpactEvent(Result.Events[Impact.SourceEvent])) ? 1 : 0;
		}
		int32 Events = 0, Missing = 0, External = 0;
		bool bStrikeSounds = false;
		for (int32 Index = 0; Index < NumEvents; ++Index)
		{
			const rb::ShotEvent& E = Result.Events[Index];
			External += E.Type == rb::ShotEventType::BallExternalContact ? 1 : 0;
			bStrikeSounds |= E.Type == rb::ShotEventType::CueStrike && Sounded.Contains(Index);
			if (IsImpactEvent(E))
			{
				++Events;
				Missing += Sounded.Contains(Index) ? 0 : 1;
			}
		}
		Test->TestEqual(*FString::Printf(TEXT("%s: every impact event sounds or is silent by design (%d events, %d sounded, %d silent)"), Label, Events,
			Events - Missing, Plan->NumSkippedEvents), Missing, Plan->NumSkippedEvents);
		Test->TestEqual(*FString::Printf(TEXT("%s: every planned sound refers to an impact event"), Label), BadSource, 0);
		Test->TestTrue(*FString::Printf(TEXT("%s: the cue strike sounds"), Label), bStrikeSounds);
		++Totals.Shots;
		Totals.Events += Events;
		Totals.Sounded += Events - Missing;
		Totals.Silent += Missing;
		Totals.External += External;
		return true;
	}

	void LogTotals()
	{
		Test->AddInfo(FString::Printf(TEXT("%s: audio over %d shots: %d impact events, %d sounded, %d silent by design, %d lamp contacts; loose-ball hits %d, floor hits played %d"),
			Venue, Totals.Shots, Totals.Events, Totals.Sounded, Totals.Silent, Totals.External, *LooseImpacts, Audio ? Audio->GetFloorHitsPlayed() - FloorHitsBefore : -1));
	}

	// --- setup --------------------------------------------------------------------------------------------------------------
	bool StageSetup()
	{
		SetupWorld = RbM2IntegrationTest::PlayWorld();
		const URbTableSubsystem* Tables = URbTableSubsystem::Get(SetupWorld.Get());
		Test->TestTrue(*FString::Printf(TEXT("%s: the player's table is the session table"), Venue), Tables && Tables->GetPlayerTable() == Table);
		if (!Test->TestTrue(*FString::Printf(TEXT("%s: an audio device (run with --sound)"), Venue), Audio->HasAudio()))
		{
			return true;
		}
		DefaultUnreachableSeconds = Loose->UnreachableReturnSeconds;
		Loose->UnreachableReturnSeconds = 1.0e9; // the off-table ball waits for its return by the next shot
		Loose->ReturnAll(INDEX_NONE, ERbLooseBallReturn::Manual);
		Loose->SetEnabled(true);
		Bind();
		*LooseImpacts = 0;
		Returns->Reset();
		FloorHitsBefore = Audio->GetFloorHitsPlayed();
		Director->SetLivePlaybackRate(RbM2IntegrationTest::kLiveRate);
		FRbMatchSetup Setup = Director->GetSetup();
		Setup.Mode = ERbMatchMode::Practice;
		Setup.Discipline = ERbDiscipline::NineBall;
		Setup.bLag = false;
		Setup.Seed = Seed;
		if (!Test->TestTrue(*FString::Printf(TEXT("%s: 9-ball practice match"), Venue), Director->StartMatch(Setup)))
		{
			return true;
		}
		Test->AddInfo(FString::Printf(TEXT("%s: table %s, ball radius cue %.2f / object %.2f mm"), Venue, *Table->GetName(),
			1000.0 * Director->GetTableContext()->BallRadius(rb::kCueBallId), 1000.0 * Director->GetTableContext()->BallRadius(1)));
		Racks = 1;
		Stage = EStage::Rack;
		return false;
	}

	// --- 1. the rack ----------------------------------------------------------------------------------------------------------
	bool StageRack()
	{
		using namespace RbM2IntegrationTest;
		if (!IsSettled())
		{
			return false;
		}
		if (Shots >= kMaxShots)
		{
			Test->AddError(FString::Printf(TEXT("%s: no rack won after %d shots"), Venue, Shots));
			return true;
		}
		const FRbTableState& State = Director->GetTableState();
		const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
		const bool bBreak = Director->GetMatchState().Game.IsBreakShot;
		switch (Director->GetPhase())
		{
		case ERbDirectorPhase::RackOver:
		case ERbDirectorPhase::MatchOver:
		{
			const FRbLastShotSummary& Last = Director->GetLastShot();
			const bool bWon = Last.Next == rb::rules::NextAction::RackWon || Last.Next == rb::rules::NextAction::MatchWon;
			Test->AddInfo(FString::Printf(TEXT("%s: rack %d over after %d shot(s) in total: next %d, the 9 %s"), Venue, Racks, Shots, static_cast<int32>(Last.Next),
				State.Balls[kNine].InPlay ? TEXT("on the table") : TEXT("down")));
			if (bWon)
			{
				Test->TestFalse(*FString::Printf(TEXT("%s: the rack is won with the 9 down"), Venue), State.Balls[kNine].InPlay);
				Test->TestTrue(*FString::Printf(TEXT("%s: the 9 dropped on the last shot"), Venue), Last.Pocketed.Contains(kNine));
				bRackWon = true;
				Stage = EStage::OffTablePlan;
				return false;
			}
			if (++Racks > kMaxRacks)
			{
				Test->AddError(FString::Printf(TEXT("%s: no rack won in %d racks"), Venue, kMaxRacks));
				return true;
			}
			Test->TestTrue(*FString::Printf(TEXT("%s: Confirm racks again"), Venue), Director->Confirm());
			return false;
		}
		case ERbDirectorPhase::AwaitDecision:
			Test->TestTrue(*FString::Printf(TEXT("%s: decision confirmed"), Venue), Director->Confirm());
			return false;
		case ERbDirectorPhase::AwaitPlacement:
		{
			rb::Vec2 Place(static_cast<float>(Rules.HeadStringX - 0.10), 0.12f);
			if (!bBreak && !PlanPlacement(*Director, State, Place))
			{
				Test->AddError(FString::Printf(TEXT("%s: no legal ball-in-hand spot"), Venue));
				return true;
			}
			if (!Test->TestTrue(*FString::Printf(TEXT("%s: cue ball placed at (%.3f, %.3f)"), Venue, Place.x, Place.y), Director->PlaceCueBall(Place)))
			{
				Test->AddError(Director->GetLastError());
				return true;
			}
			return false;
		}
		case ERbDirectorPhase::AwaitStroke:
			break;
		default:
			return false;
		}
		if (bBreak)
		{
			const rb::Vec3 Cue = State.Balls[rb::kCueBallId].State.Position;
			const rb::Vec3 Apex = State.Balls[1].State.Position;
			Submit(FStrike{8.0, FMath::RadiansToDegrees(FMath::Atan2(Apex.y - Cue.y, Apex.x - Cue.x)), 0.0, 0.0}, TEXT("break"));
		}
		else
		{
			FStrike Strike;
			int32 Width = 0;
			if (PlanPot(*Director, State, Strike, Width))
			{
				Submit(Strike, FString::Printf(TEXT("pot of the %d (window %.1f deg, b %.1f)"), LowestBall(State), 0.1 * Width, Strike.B));
			}
			else if (PlanAnyPot(*Director, State, Strike))
			{
				Submit(Strike, FString::Printf(TEXT("any pot (lowest %d)"), LowestBall(State)));
			}
			else
			{
				const int32 Target = FMath::Max(LowestBall(State), 1);
				const rb::Vec3 Cue = State.Balls[rb::kCueBallId].State.Position;
				const rb::Vec3 Object = State.Balls[Target].State.Position;
				Submit(FStrike{3.0, FMath::RadiansToDegrees(FMath::Atan2(Object.y - Cue.y, Object.x - Cue.x)), 0.0, 0.0}, FString::Printf(TEXT("safety at the %d"), Target));
			}
		}
		if (bSubmitFailed)
		{
			return true;
		}
		NextStage = EStage::Rack;
		Stage = EStage::WaitShot;
		return false;
	}

	bool StageWaitShot()
	{
		if (!IsSettled())
		{
			return false;
		}
		if (Director->GetMatchShotIndex() == ShotIndexBefore)
		{
			if (FPlatformTime::Seconds() - ShotSent < 2.0)
			{
				return false;
			}
			Test->AddError(FString::Printf(TEXT("%s: %s: no shot committed (%s)"), Venue, *ShotKind, *Director->GetLastError()));
			return true;
		}
		const FString Label = FString::Printf(TEXT("%s shot %d (%s)"), Venue, Shots + 1, *ShotKind);
		if (!CheckShotAudio(*Label))
		{
			if (FPlatformTime::Seconds() - ShotSent > 30.0)
			{
				Test->AddError(FString::Printf(TEXT("%s: the audio plan never finished"), *Label));
				return true;
			}
			return false;
		}
		++Shots;
		const FRbLastShotSummary& Last = Director->GetLastShot();
		FString Pocketed;
		for (const int32 Ball : Last.Pocketed)
		{
			Pocketed += FString::Printf(TEXT("%s%d"), Pocketed.IsEmpty() ? TEXT("") : TEXT(","), Ball);
		}
		Test->AddInfo(FString::Printf(TEXT("%s: pocketed [%s], fouls %08x, next %d"), *Label, Pocketed.IsEmpty() ? TEXT("-") : *Pocketed, Last.Fouls.Bits,
			static_cast<int32>(Last.Next)));
		Stage = NextStage;
		return false;
	}

	// --- 3. a ball off the table ------------------------------------------------------------------------------------------------
	// Only the cue ball and the 9: the cue ball drives the 9 into the near side cushion with some elevation and the 9 jumps the rail.
	bool StageOffTablePlan()
	{
		using namespace RbM2IntegrationTest;
		if (!IsSettled())
		{
			return false;
		}
		if (Director->GetPhase() == ERbDirectorPhase::RackOver || Director->GetPhase() == ERbDirectorPhase::MatchOver)
		{
			Director->Confirm();
			return false;
		}
		if (Director->GetPhase() != ERbDirectorPhase::AwaitPlacement && Director->GetPhase() != ERbDirectorPhase::AwaitStroke)
		{
			return false;
		}
		const FRbTableContext& Context = *Director->GetTableContext();
		const double HalfW = 0.5 * Context.Spec.Width;
		const double HalfL = 0.5 * Context.Spec.Length;
		int32 Tried = 0;
		for (const double Side : {-1.0, 1.0})
		{
			for (const double FromRail : {0.20, 0.235, 0.27, 0.31})
			{
				for (const double Along : {0.45, -0.45, 0.30})
				{
					FRbTableState Layout = Director->GetTableState();
					for (rb::SimBall& Ball : Layout.Balls)
					{
						Ball.InPlay = false;
					}
					const auto Put = [&Layout](int32 Id, const rb::Vec2& P)
					{
						rb::SimBall& Ball = Layout.Balls[Id];
						Ball.InPlay = true;
						Ball.State = rb::BallState{};
						Ball.State.Position = rb::Vec3(P.x, P.y, Ball.Spec.Radius);
					};
					Put(rb::kCueBallId, rb::Vec2(Along * HalfL / 1.27, Side * (HalfW - FromRail)));
					Put(kNine, rb::Vec2(Along * HalfL / 1.27, Side * (HalfW - FromRail + 0.10)));
					for (const double Speed : {10.0, 9.0, 11.0, 8.0, 12.0, 7.0})
					{
						for (const double ElevationDeg : {10.0, 12.0, 14.0, 8.0, 16.0, 6.0})
						{
							++Tried;
							const FStrike S{Speed, Side * 90.0, ElevationDeg, 0.0};
							const TSharedRef<FRbShot> Shot = Simulate(*Director, Layout, S);
							const rb::ShotResult& R = Shot->Result;
							const rb::BallFinal& NineFinal = R.Finals[kNine];
							if (R.Status != rb::SimStatus::Ok || NineFinal.Status != rb::BallFinalStatus::OffTable || NineFinal.OffReason != rb::OffTableReason::Floor ||
								R.Finals[rb::kCueBallId].Status != rb::BallFinalStatus::OnTable || HasEvent(R, rb::ShotEventType::TipRecontact))
							{
								continue;
							}
							int32 Off = INDEX_NONE;
							for (int32 Index = 0; Index < static_cast<int32>(R.Events.size()); ++Index)
							{
								if (R.Events[Index].Type == rb::ShotEventType::BallOffTable && R.Events[Index].A == kNine)
								{
									Off = Index;
									break;
								}
							}
							if (Off == INDEX_NONE)
							{
								continue;
							}
							const rb::BallState& At = R.Events[Off].Pre[0];
							if (FMath::Abs(At.Position.y) <= HalfW || FMath::Abs(At.Position.x) >= HalfL || At.Position.z > 0.6)
							{
								continue; // off at an end / corner, or a lob far above the bed
							}
							OffLayout = Layout;
							OffStrike = S;
							Test->AddInfo(FString::Printf(TEXT("%s: off-table plan after %d candidates: cue (%.3f, %.3f), 9 (%.3f, %.3f), %.0f m/s, elevation %.0f deg; the 9 leaves at t=%.3f s over the %s side rail"),
								Venue, Tried, Layout.Balls[0].State.Position.x, Layout.Balls[0].State.Position.y, Layout.Balls[kNine].State.Position.x,
								Layout.Balls[kNine].State.Position.y, Speed, ElevationDeg, R.Events[Off].Time, Side < 0.0 ? TEXT("-y") : TEXT("+y")));
							Stage = EStage::OffTableShot;
							return false;
						}
					}
				}
			}
		}
		Test->AddError(FString::Printf(TEXT("%s: no strike sends the 9 off the table (%d candidates)"), Venue, Tried));
		return true;
	}

	bool StageOffTableShot()
	{
		using namespace RbM2IntegrationTest;
		if (!bStageStarted)
		{
			Director->SetTableStateForTest(OffLayout);
			if (!Test->TestEqual(*FString::Printf(TEXT("%s: off-table layout set"), Venue), Director->GetPhase(), ERbDirectorPhase::AwaitStroke))
			{
				Test->AddError(Director->GetLastError());
				return true;
			}
			*LooseImpacts = 0;
			FloorHitsBefore = Audio->GetFloorHitsPlayed();
			Submit(OffStrike, TEXT("the 9 over the side rail"));
			if (bSubmitFailed)
			{
				return true;
			}
			bStageStarted = true;
			return false;
		}
		if (!IsSettled())
		{
			return false;
		}
		const FString Label = FString::Printf(TEXT("%s off-table shot"), Venue);
		if (Director->GetMatchShotIndex() == ShotIndexBefore || !CheckShotAudio(*Label))
		{
			return false;
		}
		const TSharedPtr<const FRbShot> Shot = Director->GetLastCommittedShot();
		Test->TestTrue(*FString::Printf(TEXT("%s: the 9 left the table (Floor)"), Venue), Shot.IsValid() && Shot->Result.Finals[kNine].Status == rb::BallFinalStatus::OffTable &&
			Shot->Result.Finals[kNine].OffReason == rb::OffTableReason::Floor);
		const FRbTableState& State = Director->GetTableState();
		const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
		const rb::Vec3 Nine = State.Balls[kNine].State.Position;
		Test->TestTrue(*FString::Printf(TEXT("%s: the foul spotted the 9 (committed state at (%.4f, %.4f), foot spot (%.4f, %.4f))"), Venue, Nine.x, Nine.y,
			Rules.FootSpot.x, Rules.FootSpot.y), State.Balls[kNine].InPlay && FMath::Abs(Nine.y - Rules.FootSpot.y) < 1.0e-3 && Nine.x >= Rules.FootSpot.x - 1.0e-3);
		Test->TestTrue(*FString::Printf(TEXT("%s: a foul was called"), Venue), Director->GetLastShot().Fouls.Bits != 0);
		Loose9 = Loose->FindLooseBall(Table->TableIndex, kNine);
		if (!Test->TestNotNull(*FString::Printf(TEXT("%s: the loose 9 (engine physics)"), Venue), Loose9.Get()))
		{
			return true;
		}
		Test->TestFalse(*FString::Printf(TEXT("%s: the spotted 9 waits hidden while its loose copy is out"), Venue), Balls->IsBallVisible(kNine));
		Stage = EStage::OffTableRest;
		return false;
	}

	bool StageOffTableRest()
	{
		ARbLooseBall* Ball = Loose9.Get();
		if (!Test->TestNotNull(*FString::Printf(TEXT("%s: the loose 9 still exists"), Venue), Ball))
		{
			return true;
		}
		if (!Ball->IsResting())
		{
			return false;
		}
		const FVector P = Ball->GetActorLocation();
		FHitResult Floor;
		const bool bFloor = FloorUnder(P, Floor);
		const AActor* Ground = Ball->GetGroundActor();
		Test->AddInfo(FString::Printf(TEXT("%s: the 9 rests at (%.1f, %.1f, %.2f) cm after %.1f s on %s (SurfaceType%d), %d hits (%d on the floor), max approach %.2f m/s"),
			Venue, P.X, P.Y, P.Z, FPlatformTime::Seconds() - StageStart, Ground ? *Ground->GetName() : TEXT("-"),
			static_cast<int32>(Ball->GetGroundSurface()), Ball->GetImpactCount(), Ball->GetFloorImpactCount(), Ball->GetMaxImpactSpeed()));
		Test->TestTrue(*FString::Printf(TEXT("%s: the 9 hit the floor"), Venue), Ball->GetFloorImpactCount() >= 1);
		Test->TestTrue(*FString::Printf(TEXT("%s: grounded on the venue floor's surface (SurfaceType%d)"), Venue, static_cast<int32>(FloorSurface)),
			Ball->IsGrounded() && Ball->GetGroundSurface() == FloorSurface);
		Test->TestTrue(*FString::Printf(TEXT("%s: at rest on the surface below it (centre = surface + R within 3 mm)"), Venue),
			bFloor && FMath::Abs(P.Z - (Floor.ImpactPoint.Z + Ball->GetRadiusCm())) <= 0.3);
		Test->TestTrue(*FString::Printf(TEXT("%s: below the table's bed (on the floor, not on the table or a ledge)"), Venue), P.Z < Table->GetActorLocation().Z + 20.0);
		const rb::Vec3 C = Table->WorldToCore(P);
		const rb::TableSpec& Spec = Table->GetContext().Spec;
		Test->TestTrue(*FString::Printf(TEXT("%s: outside the table"), Venue), FMath::Abs(C.x) > 0.5 * Spec.Length + Spec.RailWidthTotal ||
			FMath::Abs(C.y) > 0.5 * Spec.Width + Spec.RailWidthTotal);
		Test->TestTrue(*FString::Printf(TEXT("%s: OnImpact reported the 9's engine hits (%d)"), Venue, *LooseImpacts), *LooseImpacts >= 1);
		Test->TestEqual(*FString::Printf(TEXT("%s: every loose-ball hit was sounded (floor hits played)"), Venue), Audio->GetFloorHitsPlayed() - FloorHitsBefore, *LooseImpacts);
		Test->TestTrue(*FString::Printf(TEXT("%s: still awaiting its return"), Venue), Loose->IsAwaitingReturn(Table->TableIndex, RbM2IntegrationTest::kNine));
		Stage = EStage::Respot;
		return false;
	}

	// Ball in hand after the foul, then the next shot is addressed: the loose 9 goes back, the table shows the spotted 9.
	bool StageRespot()
	{
		using namespace RbM2IntegrationTest;
		if (!bStageStarted)
		{
			if (Director->GetPhase() == ERbDirectorPhase::AwaitPlacement)
			{
				rb::Vec2 Place;
				if (!Test->TestTrue(*FString::Printf(TEXT("%s: ball in hand after the foul"), Venue), PlanPlacement(*Director, Director->GetTableState(), Place) &&
					Director->PlaceCueBall(Place)))
				{
					Test->AddError(Director->GetLastError());
					return true;
				}
			}
			if (!Test->TestEqual(*FString::Printf(TEXT("%s: ready to shoot"), Venue), Director->GetPhase(), ERbDirectorPhase::AwaitStroke))
			{
				return true;
			}
			Test->TestTrue(*FString::Printf(TEXT("%s: placing the cue ball does not return the 9"), Venue), Loose->IsAwaitingReturn(Table->TableIndex, kNine));
			RespotState = Director->GetTableState();
			// A soft strike away from the 9 (the next shot addresses the table: the loose 9 returns before it is played).
			const rb::Vec3 Cue = RespotState.Balls[rb::kCueBallId].State.Position;
			const rb::Vec3 Spot = RespotState.Balls[kNine].State.Position;
			const double Away = FMath::RadiansToDegrees(FMath::Atan2(Cue.y - Spot.y, Cue.x - Spot.x));
			Submit(FStrike{0.4, Away, 0.0, 0.0}, TEXT("the next shot (address)"));
			if (bSubmitFailed)
			{
				return true;
			}
			bStageStarted = true;
			bSawReturn = false;
			return false;
		}
		if (!bSawReturn)
		{
			if (Loose->IsAwaitingReturn(Table->TableIndex, kNine))
			{
				return false;
			}
			bSawReturn = true;
			Test->TestTrue(*FString::Printf(TEXT("%s: the next shot returned the loose 9 (Address)"), Venue), Returns->ContainsByPredicate([](const TPair<int32, ERbLooseBallReturn>& R)
			{
				return R.Key == RbM2IntegrationTest::kNine && R.Value == ERbLooseBallReturn::Address;
			}));
		}
		if (!IsSettled() || Director->GetMatchShotIndex() == ShotIndexBefore)
		{
			return false;
		}
		const FRbTableState& State = Director->GetTableState();
		Test->TestTrue(*FString::Printf(TEXT("%s: the 9 is on the table again"), Venue), State.Balls[kNine].InPlay);
		Test->TestTrue(*FString::Printf(TEXT("%s: the table shows the 9"), Venue), Balls->IsBallVisible(kNine));
		if (const UStaticMeshComponent* Nine = Balls->GetBallComponent(kNine))
		{
			const double Error = FVector::Dist(Nine->GetComponentLocation(), Table->CoreToWorld(State.Balls[kNine].State.Position));
			Test->TestTrue(*FString::Printf(TEXT("%s: ... where the table state has it (%.3g cm)"), Venue, Error), Error < 1.0e-2);
		}
		Test->TestEqual(*FString::Printf(TEXT("%s: no loose ball left"), Venue), Loose->GetNumLooseBalls(), 0);
		Stage = EStage::Finish;
		return false;
	}

	FAutomationTestBase* Test;
	const TCHAR* Venue;
	int64 Seed;
	EPhysicalSurface FloorSurface;
	int32 Restarts = 0;
	TWeakObjectPtr<UWorld> SetupWorld;
	EStage Stage = EStage::Setup;
	EStage LastStage = EStage::Finish;
	EStage NextStage = EStage::Rack;
	double StageStart = 0.0;
	bool bStageStarted = false;

	ARbGameMode* Mode = nullptr;
	URbMatchDirector* Director = nullptr;
	ARbTable* Table = nullptr;
	ARbBallSet* Balls = nullptr;
	URbLooseBallSubsystem* Loose = nullptr;
	URbAudioSubsystem* Audio = nullptr;
	URbReplaySubsystem* Replay = nullptr;

	TWeakObjectPtr<URbLooseBallSubsystem> BoundLoose;
	FDelegateHandle ImpactHandle;
	FDelegateHandle ReturnHandle;
	TSharedRef<int32> LooseImpacts = MakeShared<int32>(0);
	TSharedRef<TArray<TPair<int32, ERbLooseBallReturn>>> Returns = MakeShared<TArray<TPair<int32, ERbLooseBallReturn>>>();
	double DefaultUnreachableSeconds = 20.0;
	int32 FloorHitsBefore = 0;

	uint32 ShotIndexBefore = 0;
	int32 PlansBefore = 0;
	FString ShotKind;
	double ShotSent = 0.0;
	bool bSubmitFailed = false;
	int32 Shots = 0;
	int32 Racks = 0;
	bool bRackWon = false;
	RbM2IntegrationTest::FAudioTotals Totals;

	FRbTableState OffLayout;
	RbM2IntegrationTest::FStrike OffStrike;
	TWeakObjectPtr<ARbLooseBall> Loose9;
	FRbTableState RespotState;
	bool bSawReturn = false;
};

namespace RbM2IntegrationTest
{
	bool Run(FAutomationTestBase& Test, const TCHAR* Map, const TCHAR* Venue, int64 Seed, EPhysicalSurface FloorSurface)
	{
		if (!FPackageName::DoesPackageExist(Map))
		{
			Test.AddError(FString::Printf(TEXT("%s missing (generate it with rb_make_all.py)"), Map));
			return false;
		}
		AutomationOpenMap(Map);
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FRbM2IntegrationWaitForMatch(&Test, 60.0));
		ADD_LATENT_AUTOMATION_COMMAND(FRbM2IntegrationCommand(&Test, Venue, Seed, FloorSurface));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbM2IntegrationDiveBarTest, "RawBreak.Functional.M2Integration.DiveBar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbM2IntegrationDiveBarTest::RunTest(const FString& Parameters)
{
	// The dive bar's floor is vinyl tile (PM_RbSurface_Vct, 18.6.1), the test room's concrete.
	return RbM2IntegrationTest::Run(*this, RbAssetPaths::DiveBarMap, TEXT("dive bar"), 20261004, RbAssetPaths::Surface::Vct);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbM2IntegrationTestRoomTest, "RawBreak.Functional.M2Integration.TestRoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbM2IntegrationTestRoomTest::RunTest(const FString& Parameters)
{
	return RbM2IntegrationTest::Run(*this, RbAssetPaths::M1TestRoomMap, TEXT("test room"), 20261005, RbAssetPaths::Surface::Concrete);
}

#endif // WITH_DEV_AUTOMATION_TESTS
