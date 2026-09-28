// RawBreak.Functional.M1Rack (M1 integration): a complete 9-ball practice rack played headlessly IN THE GAME on the generated
// M1 map L_M1_TestRoom (its World Settings start ARbGameMode) - every shot through the cheat console commands
// (PlayerController::ConsoleCommand, the same path as -ExecCmds), real-time live playback, rules and replays:
//   RbPlaybackRate 4 -> RbPlaceCueBall behind the head string -> RbStroke break (human layer through the pawn's stroke component:
//   get down, Commit, scripted hand samples, ExecuteStroke) -> shots planned by simulating candidate strikes on the director's
//   own table state (RunShotBlocking, the input the director builds) and played with RbStrike -> one deliberate foul (a soft
//   stroke that touches no ball): ball in hand anywhere for the incoming shooter, mandatory overlay line, an illegal placement
//   (on top of a ball) refused, RbPlaceCueBall for a straight-in, then an RbStroke (human layer) -> ... -> the 9 pocketed
//   legally wins the rack (RackOver, NextAction RackWon).
// After every shot: the playback ran over several frames and ended on the committed table state (every ball in play at
// FRbTableState within 1e-3 cm, pocketed balls hidden); the RbDumpState line is logged. At the end: replays of the last
// shot from the Shooter and the Overhead view (A10) return to the live table, and Confirm racks again.
// A break that ends the rack at once (9 on the break) is followed by the next rack, so the checked rack always has several
// shots. Owner: M1 integration (Docs/ue-architecture.md 12, A4 / A10).

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbAssetPaths.h"
#include "Dev/RbCheatManager.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbShot.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"
#include "UI/RbOverlayComponent.h"

#include "rb/Physics/ShotResult.h"

#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbM1Rack
{
	constexpr int32 kMaxShots = 45;
	constexpr int32 kMaxRacks = 3;
	constexpr int32 kLastObjectBall = 9;
	constexpr float kLiveRate = 4.0f;

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	ARbGameMode* GameMode()
	{
		UWorld* World = PlayWorld();
		return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
	}

	// A strike as the cheat sends it: floats, degrees (RbStrike converts with FMath::DegreesToRadians(float)).
	struct FStrike
	{
		float Speed = 0.0f;
		float PhiDeg = 0.0f;
		float B = 0.0f;
	};

	struct FPlan
	{
		bool bValid = false;
		FStrike Strike;
		int32 Target = -1;
		int32 Pocket = -1;
		int32 Width = 0;      // consecutive successful 0.1 deg azimuth samples around the chosen one
		double CutDeg = 0.0;
	};

	// The scripted strike's simulation on a table state, built like URbMatchDirector::BuildShotInput + SubmitStrike.
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
		Strike.Input.Elevation = FMath::DegreesToRadians(0.0f);
		Strike.Input.OffsetA = 0.0f;
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

	// The tip caught the cue ball again (double hit, R 3.7): the planner avoids such strikes.
	bool HasTipRecontact(const rb::ShotResult& Result)
	{
		for (const rb::ShotEvent& Event : Result.Events)
		{
			if (Event.Type == rb::ShotEventType::TipRecontact)
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

	// Legal pot of Target: the cue ball hits it first, it drops, the cue ball stays on the table.
	bool Pots(const rb::ShotResult& Result, int32 Target)
	{
		return Result.Status == rb::SimStatus::Ok && FirstCueBallContact(Result) == Target && Result.Finals[Target].Status == rb::BallFinalStatus::Pocketed &&
			Result.Finals[rb::kCueBallId].Status == rb::BallFinalStatus::OnTable && !HasTipRecontact(Result);
	}

	double SegmentDistance(const rb::Vec2& P, const rb::Vec2& A, const rb::Vec2& B)
	{
		const rb::Vec2 AB = B - A;
		const double L2 = rb::LengthSquared(AB);
		const double T = L2 > 0.0 ? FMath::Clamp(rb::Dot(P - A, AB) / L2, 0.0, 1.0) : 0.0;
		return rb::Length(P - (A + AB * T));
	}

	// No ball in play (other than the two ignored ids) within Clearance of the segment A-B.
	bool PathClear(const FRbTableState& Table, const rb::Vec2& A, const rb::Vec2& B, double Clearance, int32 IgnoreA, int32 IgnoreB)
	{
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			if (Id != IgnoreA && Id != IgnoreB && Table.Balls[Id].InPlay && SegmentDistance(rb::XY(Table.Balls[Id].State.Position), A, B) < Clearance)
			{
				return false;
			}
		}
		return true;
	}

	// Plans a pot of the lowest ball from the cue ball's position: for every pocket with a clear ghost-ball line, azimuths
	// +- Span around the ghost-ball aim in 0.1 deg steps at a few speeds / draw offsets are simulated; the plan is the middle of
	// the widest run of legal pots (widest window = most robust shot). OnlyPocket >= 0 restricts the pockets.
	FPlan PlanPot(const URbMatchDirector& Director, const FRbTableState& Table, int32 OnlyPocket = -1, bool bQuick = false)
	{
		FPlan Best;
		const int32 Target = LowestBall(Table);
		if (Target < 0 || !Table.Balls[rb::kCueBallId].InPlay)
		{
			return Best;
		}
		const FRbTableContext& Context = *Director.GetTableContext();
		const double R = Context.BallRadius(Target);
		const rb::Vec2 Cue = rb::XY(Table.Balls[rb::kCueBallId].State.Position);
		const rb::Vec2 Object = rb::XY(Table.Balls[Target].State.Position);
		const int32 Span = bQuick ? 15 : 30;
		const TArray<float> Speeds = bQuick ? TArray<float>{2.0f, 2.8f} : TArray<float>{1.8f, 2.6f, 3.4f};
		for (int32 P = 0; P < static_cast<int32>(Context.Geometry.Pockets.Size()); ++P)
		{
			if (OnlyPocket >= 0 && P != OnlyPocket)
			{
				continue;
			}
			const rb::PocketGeometry& Pocket = Context.Geometry.Pockets[P];
			const rb::Vec2 U = rb::Normalized(Pocket.MouthMid - Object);
			const rb::Vec2 Ghost = Object - U * (2.0 * R);
			const rb::Vec2 ToGhost = Ghost - Cue;
			if (rb::Length(ToGhost) < 1.0e-3)
			{
				continue;
			}
			const double CutDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(rb::Dot(rb::Normalized(ToGhost), U), -1.0, 1.0)));
			if (CutDeg > 70.0 || !PathClear(Table, Object, Pocket.MouthMid, 2.0 * R, rb::kCueBallId, Target) ||
				!PathClear(Table, Cue, Ghost, 2.0 * R, rb::kCueBallId, Target))
			{
				continue;
			}
			const double GhostDeg = FMath::RadiansToDegrees(FMath::Atan2(ToGhost.y, ToGhost.x));
			for (const float Speed : Speeds)
			{
				for (const float B : {-0.3f, 0.0f})
				{
					int32 Run = 0;
					int32 BestRun = 0;
					int32 BestEnd = 0;
					for (int32 K = -Span; K <= Span; ++K)
					{
						const FStrike S{Speed, static_cast<float>(GhostDeg + 0.1 * K), B};
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
					if (BestRun > Best.Width)
					{
						Best.bValid = true;
						Best.Width = BestRun;
						Best.Strike = FStrike{Speed, static_cast<float>(GhostDeg + 0.1 * (BestEnd - (BestRun - 1) / 2)), B};
						Best.Target = Target;
						Best.Pocket = P;
						Best.CutDeg = CutDeg;
					}
				}
			}
		}
		return Best;
	}

	// Any legal pot (first contact = the lowest ball, some object ball drops, the cue ball stays): full-circle scan, the fallback
	// when no ghost-ball line is open.
	FPlan PlanAnyPot(const URbMatchDirector& Director, const FRbTableState& Table)
	{
		FPlan Plan;
		const int32 Target = LowestBall(Table);
		for (const float Speed : {2.5f, 4.0f})
		{
			for (int32 Deg = 0; Deg < 360; ++Deg)
			{
				const FStrike S{Speed, static_cast<float>(Deg), -0.2f};
				const TSharedRef<FRbShot> Shot = Simulate(Director, Table, S);
				const rb::ShotResult& Result = Shot->Result;
				if (Result.Status != rb::SimStatus::Ok || FirstCueBallContact(Result) != Target ||
					Result.Finals[rb::kCueBallId].Status != rb::BallFinalStatus::OnTable || HasTipRecontact(Result))
				{
					continue;
				}
				for (int32 Id = 1; Id < rb::kMaxBalls; ++Id)
				{
					if (Table.Balls[Id].InPlay && Result.Finals[Id].Status == rb::BallFinalStatus::Pocketed)
					{
						Plan.bValid = true;
						Plan.Strike = S;
						Plan.Target = Target;
						Plan.Width = 1;
						return Plan;
					}
				}
			}
		}
		return Plan;
	}

	// A deliberate foul: a soft stroke that touches no ball (and does not scratch).
	bool PlanFoul(const URbMatchDirector& Director, const FRbTableState& Table, FStrike& Out)
	{
		for (const float Speed : {0.8f, 0.6f})
		{
			for (int32 Deg = 0; Deg < 360; Deg += 5)
			{
				const FStrike S{Speed, static_cast<float>(Deg), 0.0f};
				const TSharedRef<FRbShot> Shot = Simulate(Director, Table, S);
				if (Shot->Result.Status == rb::SimStatus::Ok && FirstCueBallContact(Shot->Result) < 0 &&
					Shot->Result.Finals[rb::kCueBallId].Status == rb::BallFinalStatus::OnTable && !HasTipRecontact(Shot->Result))
				{
					Out = S;
					return true;
				}
			}
		}
		return false;
	}

	// Ball in hand: a straight-in placement behind the lowest ball, checked by planning the pot from there.
	bool PlanPlacement(const URbMatchDirector& Director, const FRbTableState& Table, rb::Vec2& OutPlace, FPlan& OutPlan)
	{
		const int32 Target = LowestBall(Table);
		if (Target < 0)
		{
			return false;
		}
		const FRbTableContext& Context = *Director.GetTableContext();
		const double R = Context.BallRadius(Target);
		const rb::Vec2 Object = rb::XY(Table.Balls[Target].State.Position);
		TArray<int32> Pockets;
		for (int32 P = 0; P < static_cast<int32>(Context.Geometry.Pockets.Size()); ++P)
		{
			Pockets.Add(P);
		}
		Pockets.Sort([&](int32 A, int32 B) {
			return rb::Length(Context.Geometry.Pockets[A].MouthMid - Object) < rb::Length(Context.Geometry.Pockets[B].MouthMid - Object);
		});
		for (const int32 P : Pockets)
		{
			const rb::Vec2 Mouth = Context.Geometry.Pockets[P].MouthMid;
			if (!PathClear(Table, Object, Mouth, 2.0 * R, rb::kCueBallId, Target))
			{
				continue;
			}
			const rb::Vec2 U = rb::Normalized(Mouth - Object);
			for (const double Gap : {0.12, 0.20, 0.30, 0.45})
			{
				const rb::Vec2 Exact = Object - U * (2.0 * R + Gap);
				const rb::Vec2 Place(static_cast<float>(Exact.x), static_cast<float>(Exact.y)); // RbPlaceCueBall takes floats
				if (!Director.CanPlaceCueBall(Place) || !PathClear(Table, Place, Object, 2.0 * R, rb::kCueBallId, Target))
				{
					continue;
				}
				FRbTableState Hypothesis = Table;
				rb::SimBall& CueBall = Hypothesis.Balls[rb::kCueBallId];
				CueBall.InPlay = true;
				CueBall.State = rb::BallState();
				CueBall.State.Position = rb::Vec3(Place.x, Place.y, CueBall.Spec.Radius);
				CueBall.State.State = rb::MotionState::Stationary;
				const FPlan Plan = PlanPot(Director, Hypothesis, P, true);
				if (Plan.bValid && Plan.Width >= 3)
				{
					OutPlace = Place;
					OutPlan = Plan;
					return true;
				}
			}
		}
		return false;
	}

	FString StrikeCommand(const FStrike& S)
	{
		return FString::Printf(TEXT("RbStrike %.9g %.9g 0 0 %.9g"), S.Speed, S.PhiDeg, S.B);
	}

	FString Pocketed(const FRbLastShotSummary& Last)
	{
		FString Out;
		for (const int32 Ball : Last.Pocketed)
		{
			Out += FString::Printf(TEXT("%s%d"), Out.IsEmpty() ? TEXT("") : TEXT(","), Ball);
		}
		return Out.IsEmpty() ? TEXT("-") : Out;
	}
}

class FRbM1RackWaitForMatchCommand : public IAutomationLatentCommand
{
public:
	FRbM1RackWaitForMatchCommand(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* Mode = RbM1Rack::GameMode();
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (Director && Director->GetPhase() != ERbDirectorPhase::Idle)
		{
			return true;
		}
		if (GetCurrentRunTime() > Timeout)
		{
			Test->AddError(TEXT("PIE on L_M1_TestRoom did not start an ARbGameMode match"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Timeout;
};

// The rack as a stage machine: shots, playback and replays run in real time across frames.
class FRbM1RackCommand : public IAutomationLatentCommand
{
public:
	explicit FRbM1RackCommand(FAutomationTestBase* InTest, int32 InRestarts = 0) : Test(InTest), Restarts(InRestarts) {}

	virtual bool Update() override
	{
		const ARbGameMode* Current = RbM1Rack::GameMode();
		if (!Current || !Current->GetDirector() || Current->GetDirector()->GetPhase() == ERbDirectorPhase::Idle)
		{
			// Between two PIE sessions (see below): wait for the next one.
			if (GetCurrentRunTime() > 600.0)
			{
				Test->AddError(TEXT("no PIE match"));
				return true;
			}
			return false;
		}
		if (!Resolve())
		{
			return true;
		}
		// The automation map load may start a PIE session that FStartPIECommand then replaces: set up again on a new play world.
		if (Stage != EStage::Setup && SetupWorld.Get() != RbM1Rack::PlayWorld())
		{
			if (++Restarts > 2)
			{
				Test->AddError(TEXT("the play world keeps changing"));
				return true;
			}
			Test->AddInfo(TEXT("new PIE world: setting the match up again"));
			*this = FRbM1RackCommand(Test, Restarts);
			return false;
		}
		ObservePlayback();
		bool bDone = false;
		switch (Stage)
		{
		case EStage::Setup: bDone = StageSetup(); break;
		case EStage::Break: bDone = StageBreak(); break;
		case EStage::WaitShot: bDone = StageWaitShot(); break;
		case EStage::NextShot: bDone = StageNextShot(); break;
		case EStage::ReplayShooter: bDone = StageReplay(ERbReplayView::Shooter, EStage::ReplayOverhead); break;
		case EStage::ReplayOverhead: bDone = StageReplay(ERbReplayView::Overhead, EStage::Finish); break;
		case EStage::Finish: bDone = StageFinish(); break;
		}
		if (bDone)
		{
			return true;
		}
		if (Stage != LastStage)
		{
			LastStage = Stage;
			StageStart = FPlatformTime::Seconds();
		}
		else if (FPlatformTime::Seconds() - StageStart > 60.0)
		{
			Test->AddError(FString::Printf(TEXT("stage %d timed out (%s)"), static_cast<int32>(Stage), Cheats ? *Cheats->MakeStateLine() : TEXT("")));
			return true;
		}
		return false;
	}

private:
	enum class EStage : uint8
	{
		Setup,
		Break,
		WaitShot,
		NextShot,
		ReplayShooter,
		ReplayOverhead,
		Finish,
	};

	bool Resolve()
	{
		using namespace RbM1Rack;
		UWorld* World = PlayWorld();
		Mode = GameMode();
		Director = Mode ? Mode->GetDirector() : nullptr;
		PC = World ? Cast<ARbPlayerController>(World->GetFirstPlayerController()) : nullptr;
		Cheats = PC ? Cast<URbCheatManager>(PC->CheatManager) : nullptr;
		Table = Mode ? Mode->GetTable() : nullptr;
		Balls = Mode ? Mode->GetBallSet() : nullptr;
		Playback = Balls ? Balls->GetPlayback() : nullptr;
		Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
		const ARbPlayerCharacter* Character = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
		Stroke = Character ? Character->GetStroke() : nullptr;
		return Test->TestNotNull(TEXT("ARbGameMode"), Mode) && Test->TestNotNull(TEXT("director"), Director) &&
			Test->TestNotNull(TEXT("URbCheatManager"), Cheats) && Test->TestNotNull(TEXT("table"), Table) && Test->TestNotNull(TEXT("playback"), Playback) &&
			Test->TestNotNull(TEXT("replay subsystem"), Replay) && Test->TestNotNull(TEXT("stroke component"), Stroke);
	}

	void Cmd(const FString& Command)
	{
		Test->AddInfo(FString::Printf(TEXT("> %s"), *Command));
		PC->ConsoleCommand(Command);
	}

	bool IsSettled() const
	{
		const ERbDirectorPhase Phase = Director->GetPhase();
		return Phase != ERbDirectorPhase::Simulating && Phase != ERbDirectorPhase::PlayingBack && !Cheats->IsStrokeInFlight() &&
			Cheats->GetQueuedCount() == 0 && !Replay->IsReplaying();
	}

	// Frames the live playback showed of the pending shot (the playback ran in real time, not snapped to its end).
	void ObservePlayback()
	{
		if (Director->GetPhase() == ERbDirectorPhase::PlayingBack && Playback->IsPlaying() && Playback->GetShot() == Director->GetPendingShot() &&
			Playback->GetShotTime() != LastPlaybackTime)
		{
			LastPlaybackTime = Playback->GetShotTime();
			++PlaybackFrames;
		}
	}

	// The committed table state is on screen: every ball in play at its FRbTableState position, every other ball hidden.
	void CheckTableShown(const TCHAR* When)
	{
		const FRbTableState& State = Director->GetTableState();
		int32 Wrong = 0;
		double Worst = 0.0;
		for (int32 Id = 0; Id <= RbM1Rack::kLastObjectBall; ++Id)
		{
			const UStaticMeshComponent* Ball = Balls->GetBallComponent(Id);
			if (!Ball)
			{
				++Wrong;
				continue;
			}
			if (State.Balls[Id].InPlay)
			{
				const double Error = FVector::Dist(Ball->GetComponentLocation(), Table->CoreToWorld(State.Balls[Id].State.Position));
				Worst = FMath::Max(Worst, Error);
				Wrong += (Error > 1.0e-3 || !Balls->IsBallVisible(Id)) ? 1 : 0;
			}
			else
			{
				Wrong += Balls->IsBallVisible(Id) ? 1 : 0;
			}
		}
		Test->TestEqual(*FString::Printf(TEXT("%s: the ball set shows the committed table state (worst %.2g cm)"), When, Worst), Wrong, 0);
	}

	void Shoot(const FString& Command, const TCHAR* Kind)
	{
		ShotIndexBefore = Director->GetMatchShotIndex();
		PlaybackFrames = 0;
		LastPlaybackTime = -1.0;
		ShotKind = Kind;
		ShotSent = FPlatformTime::Seconds();
		Cmd(Command);
		Stage = EStage::WaitShot;
	}

	// --- setup: live playback in real time (x4), a fixed seed -------------------------------------------------------------
	bool StageSetup()
	{
		SetupWorld = RbM1Rack::PlayWorld();
		FRbMatchSetup Setup = Mode->GetStartSetup();
		Setup.Mode = ERbMatchMode::Practice;
		Setup.Discipline = ERbDiscipline::NineBall;
		Setup.Seed = 20260928;
		if (!Test->TestTrue(TEXT("9-ball practice match"), Director->StartMatch(Setup)))
		{
			return true;
		}
		Cmd(FString::Printf(TEXT("RbPlaybackRate %g"), RbM1Rack::kLiveRate));
		Test->TestEqual(TEXT("RbPlaybackRate drives the director"), Director->GetLivePlaybackRate(), RbM1Rack::kLiveRate);
		Stage = EStage::Break;
		return false;
	}

	// --- break: ball in hand behind the head string, RbStroke (human layer) at the apex ------------------------------------
	bool StageBreak()
	{
		if (!IsSettled())
		{
			return false;
		}
		if (!Test->TestEqual(TEXT("break: ball in hand"), Director->GetPhase(), ERbDirectorPhase::AwaitPlacement))
		{
			return true;
		}
		++Racks;
		ShotsThisRack = 0;
		const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
		Cmd(FString::Printf(TEXT("RbPlaceCueBall %.9g 0.12"), static_cast<float>(Rules.HeadStringX - 0.10)));
		if (!Test->TestEqual(TEXT("break: cue ball placed behind the head string"), Director->GetPhase(), ERbDirectorPhase::AwaitStroke))
		{
			return true;
		}
		const rb::Vec3 Cue = Director->GetTableState().Balls[0].State.Position;
		const rb::Vec3 Apex = Director->GetTableState().Balls[1].State.Position;
		const float Phi = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Apex.y - Cue.y, Apex.x - Cue.x)));
		Shoot(FString::Printf(TEXT("RbStroke 8.5 %.9g"), Phi), TEXT("break (RbStroke, human layer)"));
		return false;
	}

	// --- one shot: submitted, simulated on the worker, played back in real time, committed through the rules -----------------
	bool StageWaitShot()
	{
		if (!IsSettled())
		{
			return false;
		}
		if (Director->GetMatchShotIndex() == ShotIndexBefore)
		{
			if (FPlatformTime::Seconds() - ShotSent < 1.0)
			{
				return false;
			}
			if (bFallbackStrike)
			{
				// The human-layer stroke was refused (e.g. the pawn cannot reach the cue ball from outside the table): the same
				// plan as a scripted strike.
				Test->AddInfo(FString::Printf(TEXT("%s refused (%s): scripted strike instead"), *ShotKind, *Director->GetLastError()));
				bFallbackStrike = false;
				bExpectHuman = false;
				bPlanned = true;
				Shoot(RbM1Rack::StrikeCommand(FallbackStrike), TEXT("RbStrike (fallback of a refused RbStroke)"));
				return false;
			}
			Test->AddError(FString::Printf(TEXT("%s: no shot committed (%s)"), *ShotKind, *Director->GetLastError()));
			return true;
		}
		bFallbackStrike = false;
		++Shots;
		++ShotsThisRack;
		const FRbLastShotSummary& Last = Director->GetLastShot();
		const TSharedPtr<const FRbShot> Shot = Director->GetLastCommittedShot();
		const FString State = Cheats->MakeStateLine();
		Test->AddInfo(FString::Printf(TEXT("shot %d (rack %d #%d) %s: pocketed [%s], fouls %08x, next %d, speed %.3f m/s, sim %.2f ms, %d playback frames"), Shots, Racks,
			ShotsThisRack, *ShotKind, *RbM1Rack::Pocketed(Last), Last.Fouls.Bits, static_cast<int32>(Last.Next), Last.CueSpeed, Last.SimMilliseconds, PlaybackFrames));
		Test->AddInfo(State);
		Cmd(TEXT("RbDumpState"));
		Test->TestTrue(*FString::Printf(TEXT("shot %d: committed through the rules"), Shots), Last.bValid && Shot.IsValid());
		Test->TestTrue(*FString::Printf(TEXT("shot %d: played back in real time over several frames (%d)"), Shots, PlaybackFrames), PlaybackFrames >= 3);
		CheckTableShown(*FString::Printf(TEXT("shot %d"), Shots));
		Test->TestEqual(*FString::Printf(TEXT("shot %d: recorded for replays"), Shots), Replay->GetShotCount(), FMath::Min(Shots, 32));
		if (bExpectHuman)
		{
			Test->TestTrue(*FString::Printf(TEXT("shot %d: human layer (ExecuteStroke)"), Shots), Shot.IsValid() && Shot->Request.Stroke.bHuman);
			bExpectHuman = false;
		}
		if (bPlanned && Shot.IsValid())
		{
			// A scripted strike is simulated exactly as planned (same input -> same result).
			const bool bPotted = Shot->Result.Finals[PlannedTarget].Status == rb::BallFinalStatus::Pocketed;
			Test->TestTrue(*FString::Printf(TEXT("shot %d: the planned pot of the %d dropped"), Shots, PlannedTarget), bPotted);
			Test->TestEqual(*FString::Printf(TEXT("shot %d: the planned pot is legal (no foul)"), Shots), Last.Fouls.Bits, 0u);
			bPlanned = false;
		}
		// A6: the simulation hands the shot to the game thread in the frame of the stroke (same-frame hand-off). Counted for the
		// strokes through the stroke component: their contact happens inside the world tick like a player's; an RbStrike sent by
		// this latent command runs after the world tick, so its hand-off is always the next frame's.
		if (Shot.IsValid() && Shot->Request.Stroke.bHuman)
		{
			++HumanShots;
			SameFrameHandOffs += Shot->HandOffFrame == Shot->SubmitFrame ? 1 : 0;
			if (ShotsThisRack == 1)
			{
				BreakSimMs.Add(Shot->SimMilliseconds);
			}
		}
		BallsPocketed += Last.Pocketed.Num();
		if (Last.Fouls.Bits != 0)
		{
			++Fouls;
		}
		if (bFoulShot)
		{
			bFoulShot = false;
			CheckFoulHandsBallInHand();
		}
		Stage = EStage::NextShot;
		return false;
	}

	void CheckFoulHandsBallInHand()
	{
		const FRbLastShotSummary& Last = Director->GetLastShot();
		Test->TestTrue(TEXT("deliberate foul: a foul was called"), Last.Fouls.Bits != 0 && Last.Enforced != rb::rules::Foul::Count);
		Test->AddInfo(FString::Printf(TEXT("foul enforced %d, rule \"%s\""), static_cast<int32>(Last.Enforced), *Last.RuleRef));
		Test->TestEqual(TEXT("deliberate foul: ball in hand for the incoming shooter"), Director->GetPhase(), ERbDirectorPhase::AwaitPlacement);
		Test->TestTrue(TEXT("deliberate foul: cue ball in hand anywhere"), Director->IsCueBallInHand() &&
			Director->GetConstraints().PlacementRegion == rb::rules::CueBallNext::InHandAnywhere);
		Test->TestEqual(TEXT("deliberate foul: the fouler's consecutive fouls"), Director->GetMatchState().Game.Players[FoulShooter].ConsecutiveFouls, 1);
		if (URbOverlayComponent* Overlay = PC->GetOverlay())
		{
			Overlay->Refresh();
			FString Lines;
			for (const FText& Line : Overlay->GetModel().MandatoryLines)
			{
				Lines += Line.ToString() + TEXT(" | ");
			}
			Test->AddInfo(FString::Printf(TEXT("mandatory overlay lines: %s"), *Lines));
			Test->TestTrue(TEXT("deliberate foul: the mandatory overlay lines are on screen (ball in hand)"), Overlay->AreMandatoryLinesShown());
		}
		bFoulChecked = true;
		bPlaceStraightInForHuman = true;
	}

	// --- the next shot of the rack ----------------------------------------------------------------------------------------
	bool StageNextShot()
	{
		using namespace RbM1Rack;
		if (!IsSettled())
		{
			return false;
		}
		if (Shots >= kMaxShots)
		{
			Test->AddError(FString::Printf(TEXT("no rack won after %d shots"), Shots));
			return true;
		}
		const FRbTableState& State = Director->GetTableState();
		switch (Director->GetPhase())
		{
		case ERbDirectorPhase::RackOver:
		{
			const FRbLastShotSummary& Last = Director->GetLastShot();
			Test->AddInfo(FString::Printf(TEXT("rack %d over after %d shot(s): next %d, pocketed on the last shot [%s]"), Racks, ShotsThisRack,
				static_cast<int32>(Last.Next), *Pocketed(Last)));
			if (ShotsThisRack >= 4 && bFoulChecked)
			{
				Test->TestTrue(TEXT("the rack is won"), Last.Next == rb::rules::NextAction::RackWon || Last.Next == rb::rules::NextAction::MatchWon);
				Test->TestTrue(TEXT("the 9 is off the table"), !State.Balls[kLastObjectBall].InPlay);
				Test->TestTrue(TEXT("the 9 dropped on the last shot"), Last.Pocketed.Contains(kLastObjectBall));
				Stage = EStage::ReplayShooter;
				return false;
			}
			if (Racks >= kMaxRacks)
			{
				Test->AddError(TEXT("no rack with several shots and a foul"));
				return true;
			}
			const int32 RackBefore = Director->GetMatchState().RackNumber;
			Test->TestTrue(TEXT("Confirm racks again"), Director->Confirm() && Director->GetMatchState().RackNumber == RackBefore + 1);
			Stage = EStage::Break;
			return false;
		}
		case ERbDirectorPhase::AwaitDecision:
			Cmd(TEXT("RbChoose 0"));
			return false;
		case ERbDirectorPhase::AwaitPlacement:
		{
			const int32 Target = LowestBall(State);
			if (!bIllegalPlacementChecked && Target > 0)
			{
				// On top of the lowest ball: refused by the rules (Assisted input mode, R-11), still in hand.
				const rb::Vec3 On = State.Balls[Target].State.Position;
				Cmd(FString::Printf(TEXT("RbPlaceCueBall %.9g %.9g"), static_cast<float>(On.x), static_cast<float>(On.y)));
				Test->TestTrue(TEXT("illegal placement (on a ball) refused"), Director->GetPhase() == ERbDirectorPhase::AwaitPlacement && !Director->IsCueBallPlaced());
				bIllegalPlacementChecked = true;
			}
			rb::Vec2 Place;
			FPlan Plan;
			if (PlanPlacement(*Director, State, Place, Plan))
			{
				Test->AddInfo(FString::Printf(TEXT("ball in hand: straight-in on the %d into pocket %d from (%.3f, %.3f)"), Plan.Target, Plan.Pocket, Place.x, Place.y));
			}
			else
			{
				// No straight-in: anywhere legal in the kitchen area of the head (the next shot plans from there).
				const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
				Place = rb::Vec2(static_cast<float>(Rules.HeadStringX - 0.20), 0.0f);
				Test->AddInfo(TEXT("ball in hand: no straight-in found, placing at the head"));
			}
			Cmd(FString::Printf(TEXT("RbPlaceCueBall %.9g %.9g"), Place.x, Place.y));
			Test->TestEqual(TEXT("ball in hand placed"), Director->GetPhase(), ERbDirectorPhase::AwaitStroke);
			return false;
		}
		case ERbDirectorPhase::AwaitStroke:
			break;
		default:
			return false;
		}

		// Cue ball in position (or just placed).
		if (!bFoulDone && ShotsThisRack >= 2 && LowestBall(State) != kLastObjectBall)
		{
			FStrike Foul;
			if (RbM1Rack::PlanFoul(*Director, State, Foul))
			{
				bFoulDone = true;
				bFoulShot = true;
				FoulShooter = Director->GetActivePlayer();
				Shoot(RbM1Rack::StrikeCommand(Foul), TEXT("deliberate foul (touches no ball)"));
				return false;
			}
		}
		FPlan Plan = PlanPot(*Director, State);
		if (Plan.bValid && ((bPlaceStraightInForHuman && Plan.Width >= 5) || Plan.Width >= 8))
		{
			// The shot after the ball in hand, and every shot with a wide window, goes through the human layer (RbStroke: speed +
			// azimuth, centre ball; the guest shooter's noise may miss it).
			bPlaceStraightInForHuman = false;
			bExpectHuman = true;
			bFallbackStrike = true;
			FallbackStrike = Plan.Strike;
			PlannedTarget = Plan.Target;
			Shoot(FString::Printf(TEXT("RbStroke %.9g %.9g"), Plan.Strike.Speed, Plan.Strike.PhiDeg),
				*FString::Printf(TEXT("RbStroke at the %d (human layer, planned window %.1f deg)"), Plan.Target, 0.1 * Plan.Width));
			return false;
		}
		if (!Plan.bValid)
		{
			Plan = PlanAnyPot(*Director, State);
		}
		if (Plan.bValid)
		{
			bPlanned = true;
			PlannedTarget = Plan.Target;
			Shoot(StrikeCommand(Plan.Strike), *FString::Printf(TEXT("RbStrike pot of the %d (pocket %d, cut %.0f deg, window %.1f deg)"), Plan.Target, Plan.Pocket,
				Plan.CutDeg, 0.1 * Plan.Width));
			return false;
		}
		// Nothing drops: hit the lowest ball full at medium pace.
		const int32 Target = LowestBall(State);
		const rb::Vec3 Cue = State.Balls[0].State.Position;
		const rb::Vec3 Object = State.Balls[FMath::Max(Target, 1)].State.Position;
		const FStrike Direct{3.0f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Object.y - Cue.y, Object.x - Cue.x))), 0.0f};
		Shoot(StrikeCommand(Direct), *FString::Printf(TEXT("RbStrike safety at the %d (no pot found)"), Target));
		return false;
	}

	// --- A10: replays of the winning shot from the Shooter and the Overhead view return to the live table ------------------
	bool StageReplay(ERbReplayView View, EStage Next)
	{
		if (!bReplayStarted)
		{
			if (Cheats->GetQueuedCount() > 0 || Replay->IsReplaying())
			{
				return false;
			}
			ReplayShots = Replay->GetShotCount();
			ReplayShotIndex = Director->GetMatchShotIndex();
			Cmd(FString::Printf(TEXT("RbReplay %d 4"), static_cast<int32>(View)));
			const bool bStarted = Replay->IsReplaying() && Replay->GetView() == View;
			if (!Test->TestTrue(*FString::Printf(TEXT("replay from view %s started"), URbReplaySubsystem::ViewName(View)), bStarted))
			{
				return true;
			}
			Test->TestTrue(TEXT("the director is locked during the replay"), Director->IsReplayActive());
			bReplayStarted = true;
			return false;
		}
		if (Replay->IsReplaying())
		{
			return false;
		}
		bReplayStarted = false;
		Test->TestFalse(TEXT("replay over: the director is live again"), Director->IsReplayActive());
		Test->TestEqual(TEXT("replay over: no shot committed"), Director->GetMatchShotIndex(), ReplayShotIndex);
		Test->TestEqual(TEXT("replay over: history unchanged"), Replay->GetShotCount(), ReplayShots);
		Test->TestEqual(TEXT("replay over: still between racks"), Director->GetPhase(), ERbDirectorPhase::RackOver);
		Test->TestTrue(TEXT("replay over: the view is back on the pawn"), PC->GetViewTarget() == PC->GetPawn());
		CheckTableShown(*FString::Printf(TEXT("after the %s replay"), URbReplaySubsystem::ViewName(View)));
		Stage = Next;
		return false;
	}

	bool StageFinish()
	{
		Test->AddInfo(FString::Printf(TEXT("M1 rack: %d rack(s), %d shots, %d balls pocketed, %d foul shot(s)"), Racks, Shots, BallsPocketed, Fouls));
		FString Breaks;
		for (const double Ms : BreakSimMs)
		{
			Breaks += FString::Printf(TEXT(" %.2f"), Ms);
		}
		Test->AddInfo(FString::Printf(TEXT("A6: same-frame hand-off %d / %d strokes through the stroke component (%.0f %%), break simulation [ms]:%s"),
			SameFrameHandOffs, HumanShots, 100.0 * SameFrameHandOffs / FMath::Max(1, HumanShots), *Breaks));
		Test->TestTrue(TEXT("A6: several strokes through the stroke component"), HumanShots >= 3);
		Test->TestTrue(TEXT("A6: hand-off in the contact frame for >= 95 % of the strokes"), SameFrameHandOffs * 100 >= 95 * HumanShots);
		Test->TestTrue(TEXT("several shots in the won rack"), ShotsThisRack >= 4);
		Test->TestTrue(TEXT("a foul with ball in hand was played"), bFoulChecked);
		Test->TestTrue(TEXT("an illegal placement was refused"), bIllegalPlacementChecked);
		const int32 RackBefore = Director->GetMatchState().RackNumber;
		Test->TestTrue(TEXT("Confirm racks again after the won rack"), Director->Confirm() && Director->GetMatchState().RackNumber == RackBefore + 1 &&
			Director->GetPhase() == ERbDirectorPhase::AwaitPlacement);
		CheckTableShown(TEXT("new rack"));
		return true;
	}

	FAutomationTestBase* Test;
	int32 Restarts = 0;
	TWeakObjectPtr<UWorld> SetupWorld;
	EStage Stage = EStage::Setup;
	EStage LastStage = EStage::Setup;
	double StageStart = FPlatformTime::Seconds();

	ARbGameMode* Mode = nullptr;
	URbMatchDirector* Director = nullptr;
	ARbPlayerController* PC = nullptr;
	URbCheatManager* Cheats = nullptr;
	ARbTable* Table = nullptr;
	ARbBallSet* Balls = nullptr;
	URbShotPlaybackComponent* Playback = nullptr;
	URbReplaySubsystem* Replay = nullptr;
	URbStrokeComponent* Stroke = nullptr;

	uint32 ShotIndexBefore = 0;
	double ShotSent = 0.0;
	FString ShotKind;
	int32 PlaybackFrames = 0;
	double LastPlaybackTime = -1.0;
	int32 Shots = 0;
	int32 ShotsThisRack = 0;
	int32 Racks = 0;
	int32 BallsPocketed = 0;
	int32 Fouls = 0;
	int32 SameFrameHandOffs = 0;
	int32 HumanShots = 0;
	bool bFallbackStrike = false;
	RbM1Rack::FStrike FallbackStrike;
	TArray<double> BreakSimMs;
	bool bPlanned = false;
	int32 PlannedTarget = -1;
	bool bExpectHuman = false;
	bool bFoulDone = false;
	bool bFoulShot = false;
	bool bFoulChecked = false;
	int32 FoulShooter = 0;
	bool bIllegalPlacementChecked = false;
	bool bPlaceStraightInForHuman = false;
	bool bReplayStarted = false;
	int32 ReplayShots = 0;
	uint32 ReplayShotIndex = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbM1RackTest, "RawBreak.Functional.M1Rack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbM1RackTest::RunTest(const FString& Parameters)
{
	if (!FPackageName::DoesPackageExist(RbAssetPaths::M1TestRoomMap))
	{
		AddError(FString::Printf(TEXT("%s missing: run Tools/unreal/editor/rb_make_test_room.py"), RbAssetPaths::M1TestRoomMap));
		return false;
	}
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbM1RackWaitForMatchCommand(this, 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbM1RackCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
