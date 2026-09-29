// RawBreak.Functional.LooseBall (M2-E, Docs/ue-architecture.md 18.6.1): balls off the table IN THE GAME, on the M2-E dev level
// /Game/Dev/M2E/L_TwoTables (rb_dev_m2e.py: its World Settings start ARbGameMode; the 7-ft bar table is the player's, VCT floor):
//   1. a shot planned by simulating candidate layouts / strikes on the director's own table state sends the 9 over the side rail
//      (BallOffTable, reason Floor) while the cue ball stays; played first with the loose-ball subsystem OFF, then from the same
//      match seed and layout with it ON (live playback rate 1): the committed ResultHash and GameState are equal;
//   2. the live playback hands the 9 off at the exact core state of the event (position 0.1 mm, velocity / spin 1e-6 relative,
//      orientation 1e-6 rad); it bounces on the VCT floor (hits from below, a bounce above its resting height, OnImpact with
//      the VCT surface, OnRolling) and comes to rest on the floor; the foul spotted the 9 but its table instance stays hidden
//      (awaiting return);
//   3. a replay of the shot shows the 9 leaving, hides it from the hand-off time on and spawns nothing (the loose actor is
//      hidden meanwhile); back live, the loose 9 shows again and the table 9 is withheld again;
//   4. ball in hand placed: the 9 still waits; the pawn steps up to it and looks at it, the key-hint query offers "Pick up the
//      ball", the pawn's Confirm (HandleConfirm, the F key) picks it up -> the 9 shows on its spot;
//   5. automatic returns under engine physics in the level: a ball dropped into the RbBallReturn corner (ReturnVolume), one
//      below the kill Z (KillZ), one under the 9-ft table (Unreachable after UnreachableReturnSeconds), one on the open floor
//      that the next shot returns (Address), one returned by a new match (NewRack).
// Needs the dev level (python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2e.py). Owner: M2-E.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBall.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Interaction/RbInteractionSubsystem.h"
#include "Player/RbPlayerCharacter.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbShot.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#include "rb/Physics/ShotResult.h"

#include <bit>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbLooseBallFlow
{
	const TCHAR* const DevMap = TEXT("/Game/Dev/M2E/L_TwoTables");
	constexpr int32 kNine = 9;
	constexpr int64 kSeed = 20260929;
	constexpr double kStageTimeout = 45.0;

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	ARbGameMode* GameMode()
	{
		UWorld* World = PlayWorld();
		return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
	}

	int32 FindOffTableEvent(const rb::ShotResult& Result, int32 Ball)
	{
		for (int32 Index = 0; Index < static_cast<int32>(Result.Events.size()); ++Index)
		{
			const rb::ShotEvent& Event = Result.Events[Index];
			if (Event.Type == rb::ShotEventType::BallOffTable && Event.A == Ball)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// The scripted strike's simulation on the director's current table state (built like URbMatchDirector::BuildShotInput).
	TSharedRef<FRbShot> Simulate(const URbMatchDirector& Director, double Speed, double Azimuth, double Elevation)
	{
		FRbShotRequest Request;
		Request.Table = Director.GetTableContext();
		RbShot::InitSimInput(*Request.Table, Request.Input);
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			Request.Input.Balls[Id] = Director.GetTableState().Balls[Id];
		}
		Request.Input.Context.FrozenTolerance = Director.GetMatchConfig().Rules.Tolerances.Frozen;
		rb::StrikeRequest Strike;
		Strike.Ball = static_cast<rb::BallId>(rb::kCueBallId);
		Strike.Input.Speed = Speed;
		Strike.Input.Azimuth = Azimuth;
		Strike.Input.Elevation = Elevation;
		Strike.Input.Cue = Director.GetShooter(Director.GetActivePlayer()).Cue;
		Request.Input.Strikes.PushBack(Strike);
		return URbSimulationSubsystem::RunShotBlocking(MoveTemp(Request));
	}

	// Only the cue ball and the 9 on the table, at rest.
	FRbTableState MakeLayout(const URbMatchDirector& Director, const rb::Vec2& Cue, const rb::Vec2& Nine)
	{
		FRbTableState State = Director.GetTableState();
		for (rb::SimBall& Ball : State.Balls)
		{
			Ball.InPlay = false;
		}
		const auto Put = [&State](int32 Id, const rb::Vec2& P)
		{
			rb::SimBall& Ball = State.Balls[Id];
			Ball.InPlay = true;
			Ball.State = rb::BallState{};
			Ball.State.Position = rb::Vec3(P.x, P.y, Ball.Spec.Radius);
		};
		Put(rb::kCueBallId, Cue);
		Put(kNine, Nine);
		return State;
	}

	FString GameStateDigest(const rb::rules::MatchState& S)
	{
		const rb::rules::GameState& G = S.Game;
		FString Out = FString::Printf(TEXT("phase=%d rack=%d shooter=%d cue=%d break=%d push=%d open=%d free=%d fouls=%d:%d score=%d:%d"),
			static_cast<int32>(S.Phase), S.RackNumber, G.Shooter, static_cast<int32>(G.CueBall), G.IsBreakShot ? 1 : 0, G.PushOutAvailable ? 1 : 0,
			G.TableOpen ? 1 : 0, G.FreeShot ? 1 : 0, G.Players[0].ConsecutiveFouls, G.Players[1].ConsecutiveFouls, G.Players[0].Score, G.Players[1].Score);
		for (int32 Id = 0; Id < rb::rules::kRulesBallCount; ++Id)
		{
			const rb::rules::BallStatus& B = G.Balls[Id];
			Out += FString::Printf(TEXT(" %d:%d(%016llx,%016llx)"), Id, static_cast<int32>(B.Kind), std::bit_cast<uint64>(B.Position.x), std::bit_cast<uint64>(B.Position.y));
		}
		return Out;
	}

	// What the loose-ball subsystem reported (its delegates; shared with the lambdas, so a vanished command never dangles).
	struct FObserved
	{
		int32 HandOffs = 0;
		bool bHandOffMeasured = false;
		double PositionErrorCm = -1.0;
		double VelocityErrorRel = -1.0;
		double SpinErrorRel = -1.0;
		double RotationErrorRad = -1.0;
		double HandOffSpeedCmS = 0.0;
		int32 Impacts = 0;
		int32 VctImpacts = 0;
		double MaxImpactSpeed = 0.0;
		int32 VctRolling = 0;
		TArray<TPair<int32, ERbLooseBallReturn>> Returns; // (ball id, reason)
	};

	// The expected world state of a hand-off (independent of the table helpers: mirror y, x100, the table's rotation /
	// translation) vs the spawned actor, recorded in the hand-off itself (before any physics step).
	void MeasureHandOff(FObserved& Obs, ARbLooseBall& Ball, const URbMatchDirector* Director)
	{
		const TSharedPtr<const FRbShot> Shot = Director ? Director->GetPendingShot() : nullptr;
		const ARbTable* Table = Ball.GetTable();
		const int32 EventIndex = Shot.IsValid() ? FindOffTableEvent(Shot->Result, Ball.GetBallId()) : INDEX_NONE;
		if (!Table || EventIndex == INDEX_NONE || Ball.GetHandOffShotId() != Shot->Id)
		{
			return;
		}
		const rb::BallState& S = Shot->Result.Events[EventIndex].Pre[0];
		const rb::Quat& Orientation = Shot->Result.Finals[Ball.GetBallId()].Orientation;
		const FTransform TableToWorld = Table->GetTableToWorld();
		const FVector Location = TableToWorld.TransformPosition(FVector(100.0 * S.Position.x, -100.0 * S.Position.y, 100.0 * S.Position.z));
		const FVector Velocity = TableToWorld.GetRotation().RotateVector(FVector(100.0 * S.Velocity.x, -100.0 * S.Velocity.y, 100.0 * S.Velocity.z));
		const FVector Spin = TableToWorld.GetRotation().RotateVector(FVector(-S.Omega.x, S.Omega.y, -S.Omega.z));
		const FQuat Rotation = TableToWorld.GetRotation() * FRbCoords::OrientationToUE(Orientation);
		Obs.PositionErrorCm = (Ball.GetActorLocation() - Location).Size();
		Obs.VelocityErrorRel = (Ball.GetLinearVelocity() - Velocity).Size() / FMath::Max(Velocity.Size(), 1.0e-9);
		Obs.SpinErrorRel = (Ball.GetAngularVelocity() - Spin).Size() / FMath::Max(Spin.Size(), 1.0e-3);
		Obs.RotationErrorRad = Ball.GetActorQuat().AngularDistance(Rotation);
		Obs.HandOffSpeedCmS = Velocity.Size();
		Obs.bHandOffMeasured = true;
	}
}

class FRbLooseBallFlowWaitForMatch : public IAutomationLatentCommand
{
public:
	FRbLooseBallFlowWaitForMatch(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* Mode = RbLooseBallFlow::GameMode();
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (Director && Director->GetPhase() != ERbDirectorPhase::Idle)
		{
			return true;
		}
		if (GetCurrentRunTime() > Timeout)
		{
			Test->AddError(TEXT("PIE on L_TwoTables did not start an ARbGameMode match (World Settings game mode?)"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Timeout;
};

// The flow as a stage machine: shots, the ball's flight, the replay and the returns run in real time across frames.
class FRbLooseBallFlowCommand : public IAutomationLatentCommand
{
public:
	explicit FRbLooseBallFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual ~FRbLooseBallFlowCommand() override
	{
		Unbind();
	}

	virtual bool Update() override
	{
		const ARbGameMode* Current = RbLooseBallFlow::GameMode();
		if (!Current || !Current->GetDirector() || Current->GetDirector()->GetPhase() == ERbDirectorPhase::Idle)
		{
			// Between two PIE sessions (see below): wait for the next one.
			if (GetCurrentRunTime() > 300.0)
			{
				Test->AddError(TEXT("no PIE match"));
				return Finish(false);
			}
			return false;
		}
		if (!Resolve())
		{
			return Finish(false);
		}
		// The automation map load may start a PIE session that FStartPIECommand then replaces: start over on a new play world.
		if (Stage != EStage::Setup && SetupWorld.Get() != RbLooseBallFlow::PlayWorld())
		{
			if (++Restarts > 2)
			{
				Test->AddError(TEXT("the play world keeps changing"));
				return Finish(false);
			}
			Test->AddInfo(TEXT("new PIE world: starting over"));
			Unbind();
			Obs = MakeShared<RbLooseBallFlow::FObserved>();
			Stage = EStage::Setup;
		}
		bool bDone = false;
		switch (Stage)
		{
		case EStage::Setup: bDone = StageSetup(); break;
		case EStage::Plan: bDone = StagePlan(); break;
		case EStage::RunOff: bDone = StageRun(false); break;
		case EStage::RunOn: bDone = StageRun(true); break;
		case EStage::Rest: bDone = StageRest(); break;
		case EStage::Replay: bDone = StageReplay(); break;
		case EStage::Place: bDone = StagePlace(); break;
		case EStage::PickUp: bDone = StagePickUp(); break;
		case EStage::ReturnVolume: bDone = StageReturnVolume(); break;
		case EStage::KillZ: bDone = StageKillZ(); break;
		case EStage::Unreachable: bDone = StageUnreachable(); break;
		case EStage::Address: bDone = StageAddress(); break;
		case EStage::NewRack: bDone = StageNewRack(); break;
		case EStage::Finish: return Finish(true);
		}
		if (bDone)
		{
			return Finish(false);
		}
		if (Stage != LastStage)
		{
			LastStage = Stage;
			StageStart = FPlatformTime::Seconds();
			bStageStarted = false;
		}
		else if (FPlatformTime::Seconds() - StageStart > RbLooseBallFlow::kStageTimeout)
		{
			Test->AddError(FString::Printf(TEXT("stage %d timed out (phase %d, loose balls %d)"), static_cast<int32>(Stage),
				static_cast<int32>(Director->GetPhase()), Loose->GetNumLooseBalls()));
			return Finish(false);
		}
		return false;
	}

private:
	enum class EStage : uint8
	{
		Setup,
		Plan,
		RunOff,
		RunOn,
		Rest,
		Replay,
		Place,
		PickUp,
		ReturnVolume,
		KillZ,
		Unreachable,
		Address,
		NewRack,
		Finish,
	};

	bool Resolve()
	{
		UWorld* World = RbLooseBallFlow::PlayWorld();
		Mode = RbLooseBallFlow::GameMode();
		Director = Mode ? Mode->GetDirector() : nullptr;
		Table = Mode ? Mode->GetTable() : nullptr;
		Balls = Mode ? Mode->GetBallSet() : nullptr;
		Playback = Balls ? Balls->GetPlayback() : nullptr;
		Tables = URbTableSubsystem::Get(World);
		Loose = URbLooseBallSubsystem::Get(World);
		Interaction = URbInteractionSubsystem::Get(World);
		Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
		PC = World ? World->GetFirstPlayerController() : nullptr;
		Character = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
		return Test->TestNotNull(TEXT("ARbGameMode"), Mode) && Test->TestNotNull(TEXT("director"), Director) &&
			Test->TestTrue(TEXT("player table with context"), Table && Table->HasContext()) && Test->TestNotNull(TEXT("ball set"), Balls) &&
			Test->TestNotNull(TEXT("playback"), Playback) && Test->TestNotNull(TEXT("table subsystem"), Tables) &&
			Test->TestNotNull(TEXT("loose-ball subsystem"), Loose) && Test->TestNotNull(TEXT("interaction subsystem"), Interaction) &&
			Test->TestNotNull(TEXT("replay subsystem"), Replay) && Test->TestNotNull(TEXT("ARbPlayerCharacter"), Character);
	}

	void Bind()
	{
		Unbind();
		BoundLoose = Loose;
		const TSharedRef<RbLooseBallFlow::FObserved> Shared = Obs;
		const TWeakObjectPtr<URbMatchDirector> WeakDirector(Director);
		HandOffHandle = Loose->OnHandOff.AddLambda([Shared, WeakDirector](ARbLooseBall& Ball)
		{
			++Shared->HandOffs;
			if (Ball.GetBallId() == RbLooseBallFlow::kNine && !Shared->bHandOffMeasured)
			{
				RbLooseBallFlow::MeasureHandOff(*Shared, Ball, WeakDirector.Get());
			}
		});
		ImpactHandle = Loose->OnImpact.AddLambda([Shared](const FRbLooseBallImpact& Impact)
		{
			if (Impact.BallId == RbLooseBallFlow::kNine)
			{
				++Shared->Impacts;
				Shared->VctImpacts += Impact.Surface == RbAssetPaths::Surface::Vct ? 1 : 0;
				Shared->MaxImpactSpeed = FMath::Max(Shared->MaxImpactSpeed, Impact.NormalSpeed);
			}
		});
		RollingHandle = Loose->OnRolling.AddLambda([Shared](const FRbLooseBallRolling& Rolling)
		{
			Shared->VctRolling += (Rolling.BallId == RbLooseBallFlow::kNine && Rolling.Surface == RbAssetPaths::Surface::Vct) ? 1 : 0;
		});
		ReturnHandle = Loose->OnReturnedWithReason.AddLambda([Shared](int32 /*TableIndex*/, int32 BallId, ERbLooseBallReturn Reason)
		{
			Shared->Returns.Emplace(BallId, Reason);
		});
	}

	void Unbind()
	{
		if (URbLooseBallSubsystem* Subsystem = BoundLoose.Get())
		{
			Subsystem->OnHandOff.Remove(HandOffHandle);
			Subsystem->OnImpact.Remove(ImpactHandle);
			Subsystem->OnRolling.Remove(RollingHandle);
			Subsystem->OnReturnedWithReason.Remove(ReturnHandle);
		}
		BoundLoose.Reset();
	}

	bool Finish(bool bCompleted)
	{
		if (Loose)
		{
			Loose->SetEnabled(true);
			Loose->UnreachableReturnSeconds = DefaultUnreachableSeconds;
		}
		Unbind();
		if (bCompleted)
		{
			Test->AddInfo(TEXT("loose-ball flow complete"));
		}
		return true;
	}

	bool IsSettled() const
	{
		const ERbDirectorPhase Phase = Director->GetPhase();
		return Phase != ERbDirectorPhase::Simulating && Phase != ERbDirectorPhase::PlayingBack && !Replay->IsReplaying();
	}

	FRbMatchSetup MatchSetup(int64 Seed) const
	{
		FRbMatchSetup Setup = Director->GetSetup();
		Setup.Mode = ERbMatchMode::Practice;
		Setup.Seed = Seed;
		return Setup;
	}

	// The floor under a world point (the loose-ball channel, what the balls roll on).
	double FloorZ(const FVector& Above) const
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RbLooseBallFlowFloor), false);
		for (ARbLooseBall* Ball : Loose->GetLooseBalls())
		{
			Params.AddIgnoredActor(Ball);
		}
		UWorld* World = Director->GetWorld();
		if (World && World->LineTraceSingleByChannel(Hit, Above + FVector(0.0, 0.0, 5.0), Above - FVector(0.0, 0.0, 500.0),
			RbAssetPaths::Collision::LooseBallChannel, Params) && Hit.bBlockingHit)
		{
			return Hit.ImpactPoint.Z;
		}
		return Table->GetActorLocation().Z;
	}

	// Hands ball Id of the player's table off at a world point / velocity (a core state of that table).
	ARbLooseBall* Drop(int32 Id, const FVector& WorldPoint, const FVector& VelocityCmS = FVector::ZeroVector)
	{
		rb::BallState State;
		State.Position = Table->WorldToCore(WorldPoint);
		const rb::Vec3 V = Table->WorldDirectionToCore(VelocityCmS);
		State.Velocity = rb::Vec3(FRbCoords::MetersPerCm * V.x, FRbCoords::MetersPerCm * V.y, FRbCoords::MetersPerCm * V.z);
		return Loose->HandOff(*Balls, Id, State, rb::Quat::Identity(), rb::OffTableReason::Floor, 0);
	}

	bool LastReturnWas(int32 Id, ERbLooseBallReturn Reason) const
	{
		return Obs->Returns.Num() > 0 && Obs->Returns.Last().Key == Id && Obs->Returns.Last().Value == Reason;
	}

	// --- 0. setup -----------------------------------------------------------------------------------------------------------

	bool StageSetup()
	{
		SetupWorld = RbLooseBallFlow::PlayWorld();
		const FRbTableSession* Session = Tables->GetPlayerSession();
		if (!Test->TestTrue(TEXT("the player session is the game mode's (tagged 7-ft)"), Session && Session->Director.Get() == Director &&
			Session->Table.Get() == Table && Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable)))
		{
			return true;
		}
		Test->TestTrue(TEXT("the player's ball set is bound to the loose-ball subsystem"), Loose->IsBound(Balls));
		DefaultUnreachableSeconds = Loose->UnreachableReturnSeconds;
		Loose->UnreachableReturnSeconds = 1.0e9; // nothing returns by itself until the Unreachable stage
		Loose->ReturnAll(INDEX_NONE, ERbLooseBallReturn::Manual);
		Loose->SetEnabled(true);
		Bind();
		Director->SetLivePlaybackRate(1.0f);
		if (!Test->TestTrue(TEXT("practice match with a fixed seed"), Director->StartMatch(MatchSetup(RbLooseBallFlow::kSeed))))
		{
			return true;
		}
		Stage = EStage::Plan;
		return false;
	}

	// --- 1. plan: the 9 over the -y side rail (open floor toward the 9-ft), the cue ball stays --------------------------------

	// The cue ball drives the 9 into the near cushion at speed with some elevation; the 9 jumps and leaves over a side rail. Ranked
	// by preference: the cue ball stays and the 9 leaves over the -y rail (world -X in L_TwoTables: open floor between the tables),
	// the cue ball stays (any side rail), the cue ball leaves too (Floor; its own loose copy is returned by the ball-in-hand
	// placement) with the 9 over the -y rail, then any of those.
	bool StagePlan()
	{
		using namespace RbLooseBallFlow;
		const FRbTableContext& Context = *Director->GetTableContext();
		const double HalfW = 0.5 * Context.Spec.Width;
		int32 Tried = 0;
		constexpr int32 kRanks = 4;
		FPlan Best[kRanks];
		FString BestInfo[kRanks];
		TMap<FString, int32> Outcomes; // what the candidates did (diagnostics)
		for (const double Side : {1.0, -1.0})
		{
			const double Azimuth = Side * 0.5 * UE_DOUBLE_PI;
			for (const double FromRail : {0.20, 0.235, 0.27, 0.31})
			{
				for (const double Along : {0.45, -0.45, 0.30})
				{
					const rb::Vec2 Cue(Along, Side * (HalfW - FromRail));
					const rb::Vec2 Nine(Along, Side * (HalfW - FromRail + 0.10));
					Director->SetTableStateForTest(MakeLayout(*Director, Cue, Nine));
					if (Director->GetPhase() != ERbDirectorPhase::AwaitStroke)
					{
						Test->AddError(FString::Printf(TEXT("layout refused: %s"), *Director->GetLastError()));
						return true;
					}
					for (const double Speed : {10.0, 9.0, 11.0, 8.0, 12.0, 7.0, 6.0, 5.0})
					{
						for (const double ElevationDeg : {10.0, 12.0, 14.0, 8.0, 16.0, 6.0, 4.0})
						{
							++Tried;
							const double Elevation = FMath::DegreesToRadians(ElevationDeg);
							const TSharedRef<FRbShot> Shot = Simulate(*Director, Speed, Azimuth, Elevation);
							const rb::ShotResult& R = Shot->Result;
							const rb::BallFinal& CueFinal = R.Finals[rb::kCueBallId];
							const rb::BallFinal& NineFinal = R.Finals[kNine];
							++Outcomes.FindOrAdd(FString::Printf(TEXT("9 %d/%d, cue %d/%d"), static_cast<int32>(NineFinal.Status),
								static_cast<int32>(NineFinal.OffReason), static_cast<int32>(CueFinal.Status), static_cast<int32>(CueFinal.OffReason)));
							const int32 Off = FindOffTableEvent(R, kNine);
							const bool bCueStays = CueFinal.Status == rb::BallFinalStatus::OnTable;
							const bool bCueOffFloor = CueFinal.Status == rb::BallFinalStatus::OffTable && CueFinal.OffReason == rb::OffTableReason::Floor;
							if (Off == INDEX_NONE || NineFinal.Status != rb::BallFinalStatus::OffTable || NineFinal.OffReason != rb::OffTableReason::Floor ||
								!(bCueStays || bCueOffFloor))
							{
								continue;
							}
							bool bTipRecontact = false;
							for (const rb::ShotEvent& Event : R.Events)
							{
								bTipRecontact |= Event.Type == rb::ShotEventType::TipRecontact;
							}
							const rb::BallState& At = R.Events[Off].Pre[0];
							const bool bOverSideRail = FMath::Abs(At.Position.y) > HalfW && FMath::Abs(At.Position.x) < 0.5 * Context.Spec.Length;
							if (bTipRecontact || !bOverSideRail || At.Position.z > 0.6)
							{
								continue; // a double hit, off at an end / corner, or a lob far above the bed
							}
							const bool bTowardOpenFloor = At.Velocity.y < -0.05;
							const int32 Rank = bCueStays ? (bTowardOpenFloor ? 0 : 1) : (bTowardOpenFloor ? 2 : 3);
							if (Best[Rank].bValid)
							{
								continue;
							}
							Best[Rank].Layout = Director->GetTableState();
							Best[Rank].Speed = Speed;
							Best[Rank].Azimuth = Azimuth;
							Best[Rank].Elevation = Elevation;
							Best[Rank].bValid = true;
							BestInfo[Rank] = FString::Printf(TEXT("cue (%.3f, %.3f), 9 (%.3f, %.3f), %.1f m/s at azimuth %.0f / elevation %.0f deg; the 9 leaves at t=%.3f s, p=(%.3f, %.3f, %.3f) m, v=(%.2f, %.2f, %.2f) m/s; the cue ball %s"),
								Cue.x, Cue.y, Nine.x, Nine.y, Speed, FMath::RadiansToDegrees(Azimuth), ElevationDeg, R.Events[Off].Time, At.Position.x, At.Position.y,
								At.Position.z, At.Velocity.x, At.Velocity.y, At.Velocity.z, bCueStays ? TEXT("stays") : TEXT("leaves too"));
							if (Rank == 0)
							{
								break;
							}
						}
						if (Best[0].bValid)
						{
							break;
						}
					}
					if (Best[0].bValid)
					{
						break;
					}
				}
				if (Best[0].bValid)
				{
					break;
				}
			}
			if (Best[0].bValid)
			{
				break;
			}
		}
		FString Summary;
		for (const TPair<FString, int32>& Outcome : Outcomes)
		{
			Summary += FString::Printf(TEXT(" [%s: %d]"), *Outcome.Key, Outcome.Value);
		}
		Test->AddInfo(FString::Printf(TEXT("planner: %d candidates, final status / off reason (9, cue):%s"), Tried, *Summary));
		for (int32 Rank = 0; Rank < kRanks; ++Rank)
		{
			if (Best[Rank].bValid)
			{
				Plan = Best[Rank];
				Test->AddInfo(FString::Printf(TEXT("plan (rank %d): %s"), Rank, *BestInfo[Rank]));
				Stage = EStage::RunOff;
				return false;
			}
		}
		Test->AddError(FString::Printf(TEXT("no strike sends the 9 off the table (Floor) (%d candidates)"), Tried));
		return true;
	}

	// --- 2. the shot without and with loose balls -------------------------------------------------------------------------

	bool StageRun(bool bLooseBalls)
	{
		using namespace RbLooseBallFlow;
		const TCHAR* Label = bLooseBalls ? TEXT("loose balls on") : TEXT("loose balls off");
		if (!bStageStarted)
		{
			Loose->SetEnabled(bLooseBalls);
			Obs->HandOffs = 0;
			if (!Test->TestTrue(FString::Printf(TEXT("%s: match restarted (same seed)"), Label), Director->StartMatch(MatchSetup(kSeed))))
			{
				return true;
			}
			Director->SetTableStateForTest(Plan.Layout);
			if (!Test->TestTrue(FString::Printf(TEXT("%s: strike submitted"), Label),
				Director->SubmitScriptedStrike(Plan.Speed, Plan.Azimuth, Plan.Elevation, 0.0, 0.0)))
			{
				Test->AddError(Director->GetLastError());
				return true;
			}
			bStageStarted = true;
			bSawPlayback = false;
			return false;
		}
		if (Director->GetPhase() == ERbDirectorPhase::PlayingBack)
		{
			bSawPlayback = true;
			// Before its hand-off the 9 has no loose copy and shows on the table; from it on it has one and the table 9 is hidden.
			const TSharedPtr<const FRbShot> Shot = Director->GetPendingShot();
			if (bLooseBalls && Shot.IsValid() && Playback->IsPlaying() && Playback->GetShot() == Shot)
			{
				const double HandOffTime = Shot->Result.Finals[kNine].Time;
				const double T = Playback->GetShotTime();
				const bool bLoose9 = Loose->FindLooseBall(Table->TableIndex, kNine) != nullptr;
				if (T < HandOffTime - 0.02)
				{
					bBeforeOk &= !bLoose9 && Balls->IsBallVisible(kNine);
				}
				else if (T > HandOffTime + 0.02)
				{
					bAfterOk &= bLoose9 && !Balls->IsBallVisible(kNine);
				}
			}
		}
		if (!IsSettled())
		{
			return false;
		}
		const TSharedPtr<const FRbShot> Committed = Director->GetLastCommittedShot();
		if (!Test->TestTrue(FString::Printf(TEXT("%s: shot committed"), Label), Committed.IsValid() && Director->GetMatchShotIndex() == 1))
		{
			return true;
		}
		Test->TestTrue(FString::Printf(TEXT("%s: the live playback ran"), Label), bSawPlayback);
		Test->TestTrue(FString::Printf(TEXT("%s: the 9 left the table (Floor)"), Label), Committed->Result.Finals[kNine].Status == rb::BallFinalStatus::OffTable &&
			Committed->Result.Finals[kNine].OffReason == rb::OffTableReason::Floor);
		const FRbTableState& State = Director->GetTableState();
		Test->TestTrue(FString::Printf(TEXT("%s: the foul spotted the 9 (committed state)"), Label), State.Balls[kNine].InPlay);
		if (!bLooseBalls)
		{
			HashOff = Committed->ResultHash;
			DigestOff = GameStateDigest(Director->GetMatchState());
			Test->TestEqual(TEXT("off: no hand-off"), Obs->HandOffs, 0);
			Test->TestEqual(TEXT("off: no loose ball"), Loose->GetNumLooseBalls(), 0);
			Test->TestTrue(TEXT("off: the spotted 9 shows at once"), Balls->IsBallVisible(kNine));
			bBeforeOk = true;
			bAfterOk = true;
			Stage = EStage::RunOn;
			return false;
		}
		// The rules never looked at the loose ball.
		Test->AddInfo(FString::Printf(TEXT("ResultHash %016llx (off %016llx), fouls %08x, phase %d"), Committed->ResultHash, HashOff,
			Director->GetLastShot().Fouls.Bits, static_cast<int32>(Director->GetPhase())));
		Test->TestEqual(TEXT("ResultHash equals the run without loose balls"), Committed->ResultHash, HashOff);
		Test->TestEqual(TEXT("GameState equals the run without loose balls"), GameStateDigest(Director->GetMatchState()), DigestOff);
		// The hand-off: one per ball that left the table (the 9, possibly the cue ball), the 9 at the event's core state.
		int32 LeftTheTable = 0;
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::BallFinal& Final = Committed->Result.Finals[Id];
			LeftTheTable += (Final.Status == rb::BallFinalStatus::OffTable && Final.OffReason != rb::OffTableReason::RestsOnRailOrFrame) ? 1 : 0;
		}
		Test->TestEqual(TEXT("one hand-off per ball that left the table"), Obs->HandOffs, LeftTheTable);
		Test->TestEqual(TEXT("one loose ball per ball that left the table"), Loose->GetNumLooseBalls(), LeftTheTable);
		Test->AddInfo(FString::Printf(TEXT("hand-off errors: position %.3e cm, velocity %.3e, spin %.3e (relative), orientation %.3e rad; speed %.1f cm/s"),
			Obs->PositionErrorCm, Obs->VelocityErrorRel, Obs->SpinErrorRel, Obs->RotationErrorRad, Obs->HandOffSpeedCmS));
		Test->TestTrue(TEXT("hand-off measured"), Obs->bHandOffMeasured);
		Test->TestTrue(TEXT("hand-off position = the event's (0.1 mm)"), Obs->PositionErrorCm >= 0.0 && Obs->PositionErrorCm <= 0.01);
		Test->TestTrue(TEXT("hand-off velocity = the event's (1e-6 relative)"), Obs->VelocityErrorRel >= 0.0 && Obs->VelocityErrorRel <= 1.0e-6);
		Test->TestTrue(TEXT("hand-off spin = the event's (1e-6 relative)"), Obs->SpinErrorRel >= 0.0 && Obs->SpinErrorRel <= 1.0e-6);
		Test->TestTrue(TEXT("hand-off orientation = the event's (1e-6 rad)"), Obs->RotationErrorRad >= 0.0 && Obs->RotationErrorRad <= 1.0e-6);
		Test->TestTrue(TEXT("during the playback: the 9 shown before its hand-off, its loose copy and the table 9 hidden after it"), bBeforeOk && bAfterOk);
		Loose9 = Loose->FindLooseBall(Table->TableIndex, kNine);
		Test->TestNotNull(TEXT("the loose 9"), Loose9.Get());
		Test->TestTrue(TEXT("awaiting return"), Loose->IsAwaitingReturn(Table->TableIndex, kNine));
		Test->TestFalse(TEXT("the spotted 9's table instance stays hidden"), Balls->IsBallVisible(kNine));
		Test->TestTrue(TEXT("... although the table state shows it"), Balls->IsBallRequestedVisible(kNine));
		Test->TestTrue(TEXT("the cue ball shows iff in play"), Balls->IsBallVisible(rb::kCueBallId) == State.Balls[rb::kCueBallId].InPlay);
		Stage = Loose9.IsValid() ? EStage::Rest : EStage::Finish;
		return !Loose9.IsValid();
	}

	// --- 3. bounce and rest on the floor ------------------------------------------------------------------------------------

	bool StageRest()
	{
		ARbLooseBall* Ball = Loose9.Get();
		if (!Test->TestNotNull(TEXT("the loose 9 still exists"), Ball))
		{
			return true;
		}
		if (!Ball->IsResting())
		{
			return false;
		}
		const FVector P = Ball->GetActorLocation();
		const double Floor = FloorZ(P);
		const double R = Ball->GetRadiusCm();
		Test->AddInfo(FString::Printf(TEXT("the 9 rests at (%.1f, %.1f, %.2f) cm after %.1f s: floor %.2f, %d hits (%d from below, %d on VCT), max approach %.2f m/s, bounce apex %.2f cm above rest, %d rolling samples on VCT"),
			P.X, P.Y, P.Z, FPlatformTime::Seconds() - StageStart, Floor, Ball->GetImpactCount(), Ball->GetFloorImpactCount(), Obs->VctImpacts,
			Obs->MaxImpactSpeed, Ball->GetMaxZAfterFirstFloorImpact() - (Floor + R), Obs->VctRolling));
		Test->TestTrue(TEXT("it hit the floor"), Ball->GetFloorImpactCount() >= 1);
		Test->TestTrue(TEXT("it bounced (rose >= 5 mm above its resting height after the first floor hit)"), Ball->GetMaxZAfterFirstFloorImpact() >= Floor + R + 0.5);
		Test->TestTrue(TEXT("OnImpact reported hits on the VCT floor"), Obs->VctImpacts >= 1);
		Test->TestTrue(TEXT("OnRolling reported rolling on VCT"), Obs->VctRolling >= 1);
		Test->TestTrue(TEXT("at rest ON the floor (centre = floor + R within 3 mm)"), FMath::Abs(P.Z - (Floor + R)) <= 0.3);
		Test->TestTrue(TEXT("grounded on the VCT floor"), Ball->IsGrounded() && Ball->GetGroundSurface() == RbAssetPaths::Surface::Vct);
		Test->TestTrue(TEXT("resting off the table (outside its outer boundary)"), [this, &P]()
		{
			const rb::Vec3 C = Table->WorldToCore(P);
			const rb::TableSpec& Spec = Table->GetContext().Spec;
			return FMath::Abs(C.x) > 0.5 * Spec.Length + Spec.RailWidthTotal || FMath::Abs(C.y) > 0.5 * Spec.Width + Spec.RailWidthTotal;
		}());
		Test->TestTrue(TEXT("still awaiting return, table 9 hidden"), Loose->IsAwaitingReturn(Table->TableIndex, RbLooseBallFlow::kNine) &&
			!Balls->IsBallVisible(RbLooseBallFlow::kNine));
		Stage = EStage::Replay;
		return false;
	}

	// --- 4. replay: the 9 leaves, is hidden at the hand-off time, nothing spawns -----------------------------------------

	bool StageReplay()
	{
		using namespace RbLooseBallFlow;
		ARbLooseBall* Ball = Loose9.Get();
		if (!Test->TestNotNull(TEXT("the loose 9"), Ball))
		{
			return true;
		}
		if (!bStageStarted)
		{
			ReplayHandOffs = Obs->HandOffs;
			ReplayLooseCount = Loose->GetNumLooseBalls();
			const TSharedPtr<const FRbShot> Last = Replay->GetShot(0);
			if (!Test->TestTrue(TEXT("the shot is in the replay history"), Last.IsValid() && Last == Director->GetLastCommittedShot()) ||
				!Test->TestTrue(TEXT("replay started"), Replay->PlayReplayFrom(0, ERbReplayView::Shooter, 1.0f, 0.0)))
			{
				return true;
			}
			ReplayShot = Last;
			bStageStarted = true;
			bBeforeOk = true;
			bAfterOk = true;
			bSawBefore = false;
			bSawAfter = false;
			return false;
		}
		if (Replay->IsReplaying())
		{
			bAfterOk &= Loose->GetNumLooseBalls() == ReplayLooseCount && Ball->IsHidden() && Obs->HandOffs == ReplayHandOffs;
			if (Playback->IsPlaying() && Playback->GetShot() == ReplayShot)
			{
				const double HandOffTime = ReplayShot->Result.Finals[kNine].Time;
				const double T = Playback->GetShotTime();
				if (T < HandOffTime - 0.02)
				{
					bSawBefore = true;
					bBeforeOk &= Balls->IsBallVisible(kNine);
				}
				else if (T > HandOffTime + 0.02)
				{
					bSawAfter = true;
					bAfterOk &= !Balls->IsBallVisible(kNine);
					if (T > HandOffTime + 0.5)
					{
						Replay->StopReplay(); // seen enough: back to the live table
					}
				}
			}
			return false;
		}
		Test->TestTrue(TEXT("replay: frames before and after the hand-off time"), bSawBefore && bSawAfter);
		Test->TestTrue(TEXT("replay: the 9 shows while it flies (withholding suspended)"), bBeforeOk);
		Test->TestTrue(TEXT("replay: hidden from the hand-off time on, the loose actor hidden, no second loose actor, no hand-off"), bAfterOk);
		Test->TestEqual(TEXT("replay over: the same loose balls"), Loose->GetNumLooseBalls(), ReplayLooseCount);
		Test->TestFalse(TEXT("replay over: the loose 9 shows again"), Ball->IsHidden());
		Test->TestTrue(TEXT("replay over: the table 9 withheld again"), Balls->IsBallWithheld(kNine) && !Balls->IsBallVisible(kNine));
		Stage = EStage::Place;
		return false;
	}

	// --- 5. ball in hand placed; the 9 still waits --------------------------------------------------------------------------

	bool StagePlace()
	{
		if (!bStageStarted)
		{
			if (!Test->TestEqual(TEXT("ball in hand after the foul"), Director->GetPhase(), ERbDirectorPhase::AwaitPlacement))
			{
				return true;
			}
			bLooseCueBall = Loose->IsAwaitingReturn(Table->TableIndex, rb::kCueBallId);
			bool bPlaced = false;
			for (const rb::Vec2& Spot : {rb::Vec2(-0.45, 0.15), rb::Vec2(-0.35, -0.10), rb::Vec2(0.30, 0.20), rb::Vec2(0.0, 0.0)})
			{
				if (Director->CanPlaceCueBall(Spot) && Director->PlaceCueBall(Spot))
				{
					bPlaced = true;
					break;
				}
			}
			if (!Test->TestTrue(TEXT("ball in hand placed"), bPlaced && Director->GetPhase() == ERbDirectorPhase::AwaitStroke))
			{
				return true;
			}
			bStageStarted = true;
			return false;
		}
		if (FPlatformTime::Seconds() - StageStart < 0.3)
		{
			return false; // a few ticks of the automatic returns
		}
		Test->TestTrue(TEXT("placing the cue ball does not return the 9"), Loose->IsAwaitingReturn(Table->TableIndex, RbLooseBallFlow::kNine));
		if (bLooseCueBall)
		{
			// The cue ball had left the table too: the placement returned its loose copy.
			Test->TestFalse(TEXT("the placement returned the loose cue ball"), Loose->IsAwaitingReturn(Table->TableIndex, rb::kCueBallId));
			Test->TestTrue(TEXT("... as CueBallPlaced"), Obs->Returns.ContainsByPredicate([](const TPair<int32, ERbLooseBallReturn>& R)
			{
				return R.Key == rb::kCueBallId && R.Value == ERbLooseBallReturn::CueBallPlaced;
			}));
			Test->TestTrue(TEXT("the placed cue ball shows"), Balls->IsBallVisible(rb::kCueBallId));
		}
		Stage = EStage::PickUp;
		return false;
	}

	// --- 6. the pawn steps up, looks at the ball and presses Confirm ------------------------------------------------------

	bool StagePickUp()
	{
		using namespace RbLooseBallFlow;
		ARbLooseBall* Ball = Loose9.Get();
		if (!Test->TestNotNull(TEXT("the loose 9"), Ball))
		{
			return true;
		}
		const FVector Center = Ball->GetActorLocation();
		if (!bStageStarted)
		{
			// A standing spot 70 cm (plan) from the ball where the capsule fits, preferring the side away from the table.
			UWorld* World = Director->GetWorld();
			const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
			const double HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			const double Floor = FloorZ(Center);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RbLooseBallFlowStand), false, Character);
			const FVector Away = (Center - Table->GetActorLocation()).GetSafeNormal2D();
			bool bFound = false;
			FVector Stand = FVector::ZeroVector;
			for (int32 Step = 0; Step < 16 && !bFound; ++Step)
			{
				const double Angle = UE_DOUBLE_PI * (Step % 2 == 0 ? 1.0 : -1.0) * ((Step + 1) / 2) / 8.0;
				const FVector Dir = FQuat(FVector::UpVector, Angle).RotateVector(Away.IsNearlyZero() ? FVector::ForwardVector : Away);
				Stand = FVector(Center.X, Center.Y, Floor + HalfHeight + 2.0) + Dir * 70.0;
				bFound = !World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight), Params);
			}
			if (!Test->TestTrue(TEXT("a standing spot beside the ball"), bFound))
			{
				return true;
			}
			Character->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			bStageStarted = true;
			AimFrames = 0;
			return false;
		}
		// Look at the ball (the camera rig follows the control rotation while standing); a few steady frames.
		FVector Eye;
		FRotator View;
		PC->GetPlayerViewPoint(Eye, View);
		const FVector ToBall = (Center - Eye).GetSafeNormal();
		PC->SetControlRotation(ToBall.Rotation());
		const double ErrorDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(View.Vector() | ToBall, -1.0, 1.0)));
		AimFrames = ErrorDeg < 1.5 ? AimFrames + 1 : 0;
		if (AimFrames < 5)
		{
			return false;
		}
		const FVector PlanOffset = FVector(Center.X - Eye.X, Center.Y - Eye.Y, 0.0);
		Test->AddInfo(FString::Printf(TEXT("the pawn looks at the 9 from %.0f cm (plan), %.0f cm above it, view error %.2f deg"), PlanOffset.Size(),
			Eye.Z - Center.Z, ErrorDeg));
		FRbInteractionQuery Query;
		Query.Pawn = Character;
		Query.Eye = Eye;
		Query.Direction = View.Vector();
		FText Verb;
		Test->TestTrue(TEXT("the key hints are offered the pick-up"), Interaction->FindInteraction(Query, Verb));
		Test->TestEqual(TEXT("verb"), Verb.ToString(), URbLooseBallSubsystem::PickUpVerb().ToString());
		const ERbDirectorPhase PhaseBefore = Director->GetPhase();
		Character->HandleConfirm(); // the F key
		Test->TestFalse(TEXT("Confirm picked the 9 up"), Loose->IsAwaitingReturn(Table->TableIndex, kNine));
		Test->TestTrue(TEXT("... as a pick-up"), LastReturnWas(kNine, ERbLooseBallReturn::PickUp));
		Test->TestEqual(TEXT("the press went to the pick-up, not to the director"), Director->GetPhase(), PhaseBefore);
		const FRbTableState& State = Director->GetTableState();
		const UStaticMeshComponent* Nine = Balls->GetBallComponent(kNine);
		Test->TestTrue(TEXT("the 9 shows on its spot on the table"), Balls->IsBallVisible(kNine) && Nine &&
			FVector::Dist(Nine->GetComponentLocation(), Table->CoreToWorld(State.Balls[kNine].State.Position)) < 1.0e-3);
		Stage = EStage::ReturnVolume;
		return false;
	}

	// --- 7. automatic returns under engine physics -------------------------------------------------------------------------

	bool StageReturnVolume()
	{
		constexpr int32 Id = 3;
		if (!bStageStarted)
		{
			AActor* Volume = nullptr;
			for (TActorIterator<AActor> It(Director->GetWorld()); It; ++It)
			{
				if (It->ActorHasTag(RbAssetPaths::Tag::BallReturnVolume))
				{
					Volume = *It;
					break;
				}
			}
			if (!Test->TestNotNull(TEXT("the level's RbBallReturn volume"), Volume))
			{
				return true;
			}
			const FVector C = Volume->GetActorLocation();
			// Dropped 25 cm above the floor with a little roll: it bounces, rolls and rests inside the volume.
			if (!Test->TestNotNull(TEXT("ball dropped into the return corner"), Drop(Id, FVector(C.X, C.Y, FloorZ(C) + 25.0), FVector(8.0, 5.0, 0.0))))
			{
				return true;
			}
			bStageStarted = true;
			return false;
		}
		if (Loose->IsAwaitingReturn(Table->TableIndex, Id))
		{
			return false;
		}
		Test->TestTrue(TEXT("resting in the return volume: returned"), LastReturnWas(Id, ERbLooseBallReturn::ReturnVolume));
		Test->TestFalse(TEXT("released (not in play: stays hidden)"), Balls->IsBallWithheld(Id) || Balls->IsBallVisible(Id));
		Stage = EStage::KillZ;
		return false;
	}

	bool StageKillZ()
	{
		constexpr int32 Id = 4;
		if (!bStageStarted)
		{
			const FVector Below = Table->GetActorLocation() - FVector(0.0, 0.0, 450.0);
			if (!Test->TestNotNull(TEXT("ball dropped below the floor"), Drop(Id, Below)))
			{
				return true;
			}
			bStageStarted = true;
			return false;
		}
		if (Loose->IsAwaitingReturn(Table->TableIndex, Id))
		{
			return false;
		}
		Test->TestTrue(TEXT("below the kill Z: returned"), LastReturnWas(Id, ERbLooseBallReturn::KillZ));
		Stage = EStage::Unreachable;
		return false;
	}

	bool StageUnreachable()
	{
		constexpr int32 Id = 5;
		if (!bStageStarted)
		{
			// Under the middle of the other (9-ft) table: no standing spot within reach sees it.
			const ARbTable* Other = nullptr;
			for (const ARbTable* Candidate : Tables->GetTables())
			{
				Other = Candidate != Table ? Candidate : Other;
			}
			if (!Test->TestNotNull(TEXT("the other table"), Other))
			{
				return true;
			}
			const FVector Under = Other->GetBedCenterWorld();
			const FVector Floor(Under.X, Under.Y, FloorZ(FVector(Under.X, Under.Y, Other->GetActorLocation().Z + 30.0)));
			ARbLooseBall* Ball = Drop(Id, Floor + FVector(0.0, 0.0, Balls->GetBallRadiusCm(Id) + 0.5));
			if (!Test->TestNotNull(TEXT("ball dropped under the 9-ft table"), Ball))
			{
				return true;
			}
			Loose->UnreachableReturnSeconds = 2.0;
			bStageStarted = true;
			return false;
		}
		const ARbLooseBall* Ball = Loose->FindLooseBall(Table->TableIndex, Id);
		if (Ball)
		{
			if (Ball->IsResting() && !bCheckedReach)
			{
				bCheckedReach = true;
				Test->TestFalse(TEXT("under the table: unreachable"), Loose->IsReachable(*Ball));
			}
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("the ball under the table came back after %.1f s"), FPlatformTime::Seconds() - StageStart));
		Test->TestTrue(TEXT("unreachable for UnreachableReturnSeconds: returned"), LastReturnWas(Id, ERbLooseBallReturn::Unreachable));
		Loose->UnreachableReturnSeconds = DefaultUnreachableSeconds;
		Stage = EStage::Address;
		return false;
	}

	bool StageAddress()
	{
		constexpr int32 Id = 6;
		const rb::TableSpec& Spec = Table->GetContext().Spec;
		if (!bStageStarted)
		{
			// On the open floor 60 cm beyond the head rail: reachable, it waits for the next shot.
			const FVector Beyond = Table->CoreToWorld(rb::Vec3(-(0.5 * Spec.Length + Spec.RailWidthTotal + 0.6), 0.0, 0.0));
			const double Floor = FloorZ(FVector(Beyond.X, Beyond.Y, Table->GetActorLocation().Z + 30.0));
			if (!Test->TestNotNull(TEXT("ball dropped on the open floor"), Drop(Id, FVector(Beyond.X, Beyond.Y, Floor + Balls->GetBallRadiusCm(Id) + 0.5))))
			{
				return true;
			}
			bStageStarted = true;
			bStruck = false;
			return false;
		}
		if (!bStruck)
		{
			const ARbLooseBall* Ball = Loose->FindLooseBall(Table->TableIndex, Id);
			if (!Ball || !Ball->IsResting() || FPlatformTime::Seconds() - StageStart < 1.0)
			{
				Test->TestNotNull(TEXT("the ball on the open floor waits"), Ball);
				return Ball == nullptr;
			}
			Test->TestTrue(TEXT("on the open floor: reachable"), Loose->IsReachable(*Ball));
			if (!Test->TestTrue(TEXT("next shot submitted"), Director->SubmitScriptedStrike(0.6, 0.0, 0.0, 0.0, 0.0)))
			{
				Test->AddError(Director->GetLastError());
				return true;
			}
			bStruck = true;
			return false;
		}
		if (Loose->IsAwaitingReturn(Table->TableIndex, Id))
		{
			return false;
		}
		Test->TestTrue(TEXT("the next shot returned it (address)"), LastReturnWas(Id, ERbLooseBallReturn::Address));
		Stage = EStage::NewRack;
		return false;
	}

	bool StageNewRack()
	{
		constexpr int32 Id = 7;
		if (!bStageStarted)
		{
			if (!IsSettled())
			{
				return false;
			}
			const FVector Beside = Table->CoreToWorld(rb::Vec3(0.0, -(0.5 * Table->GetContext().Spec.Width + Table->GetContext().Spec.RailWidthTotal + 0.5), 0.0));
			const double Floor = FloorZ(FVector(Beside.X, Beside.Y, Table->GetActorLocation().Z + 30.0));
			if (!Test->TestNotNull(TEXT("ball dropped beside the table"), Drop(Id, FVector(Beside.X, Beside.Y, Floor + Balls->GetBallRadiusCm(Id) + 0.5))))
			{
				return true;
			}
			ShotIndexBefore = Director->GetMatchShotIndex();
			bStageStarted = true;
			bRestarted = false;
			return false;
		}
		if (!bRestarted)
		{
			if (FPlatformTime::Seconds() - StageStart < 0.5)
			{
				return false;
			}
			Test->TestTrue(TEXT("it waits before the new match"), Loose->IsAwaitingReturn(Table->TableIndex, Id));
			// The same seed again (rack 1 -> rack 1): the reset shot index marks the new match.
			Test->TestTrue(TEXT("new match (same seed)"), ShotIndexBefore > 0 && Director->StartMatch(MatchSetup(RbLooseBallFlow::kSeed)));
			bRestarted = true;
			return false;
		}
		if (Loose->IsAwaitingReturn(Table->TableIndex, Id))
		{
			return false;
		}
		Test->TestTrue(TEXT("the new match returned it"), LastReturnWas(Id, ERbLooseBallReturn::NewRack));
		Test->TestEqual(TEXT("no loose ball left"), Loose->GetNumLooseBalls(), 0);
		FString Reasons;
		for (const TPair<int32, ERbLooseBallReturn>& Return : Obs->Returns)
		{
			Reasons += FString::Printf(TEXT(" %d:%s"), Return.Key, *StaticEnum<ERbLooseBallReturn>()->GetNameStringByValue(static_cast<int64>(Return.Value)));
		}
		Test->AddInfo(FString::Printf(TEXT("returns:%s"), *Reasons));
		Stage = EStage::Finish;
		return false;
	}

	struct FPlan
	{
		bool bValid = false;
		FRbTableState Layout;
		double Speed = 0.0;
		double Azimuth = 0.0;
		double Elevation = 0.0;
	};

	FAutomationTestBase* Test;
	int32 Restarts = 0;
	TWeakObjectPtr<UWorld> SetupWorld;
	EStage Stage = EStage::Setup;
	EStage LastStage = EStage::Finish;
	double StageStart = 0.0;
	bool bStageStarted = false;

	ARbGameMode* Mode = nullptr;
	URbMatchDirector* Director = nullptr;
	ARbTable* Table = nullptr;
	ARbBallSet* Balls = nullptr;
	URbShotPlaybackComponent* Playback = nullptr;
	URbTableSubsystem* Tables = nullptr;
	URbLooseBallSubsystem* Loose = nullptr;
	URbInteractionSubsystem* Interaction = nullptr;
	URbReplaySubsystem* Replay = nullptr;
	APlayerController* PC = nullptr;
	ARbPlayerCharacter* Character = nullptr;

	TSharedRef<RbLooseBallFlow::FObserved> Obs = MakeShared<RbLooseBallFlow::FObserved>();
	TWeakObjectPtr<URbLooseBallSubsystem> BoundLoose;
	FDelegateHandle HandOffHandle;
	FDelegateHandle ImpactHandle;
	FDelegateHandle RollingHandle;
	FDelegateHandle ReturnHandle;

	FPlan Plan;
	uint64 HashOff = 0;
	FString DigestOff;
	bool bSawPlayback = false;
	bool bBeforeOk = true;
	bool bAfterOk = true;
	bool bSawBefore = false;
	bool bSawAfter = false;
	TWeakObjectPtr<ARbLooseBall> Loose9;
	TSharedPtr<const FRbShot> ReplayShot;
	int32 ReplayHandOffs = 0;
	int32 ReplayLooseCount = 0;
	int32 AimFrames = 0;
	bool bLooseCueBall = false;
	bool bCheckedReach = false;
	bool bStruck = false;
	bool bRestarted = false;
	uint32 ShotIndexBefore = 0;
	double DefaultUnreachableSeconds = 20.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLooseBallFunctionalTest, "RawBreak.Functional.LooseBall", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbLooseBallFunctionalTest::RunTest(const FString& Parameters)
{
	if (!FPackageName::DoesPackageExist(RbLooseBallFlow::DevMap))
	{
		AddError(FString::Printf(TEXT("%s missing: run python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2e.py"), RbLooseBallFlow::DevMap));
		return false;
	}
	AutomationOpenMap(RbLooseBallFlow::DevMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbLooseBallFlowWaitForMatch(this, 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbLooseBallFlowCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
