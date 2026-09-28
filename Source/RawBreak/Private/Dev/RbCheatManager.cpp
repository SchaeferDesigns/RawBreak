#include "Dev/RbCheatManager.h"

#include "RawBreak.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "Table/RbTable.h"
#include "UI/RbOverlayComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"

// Owner: UE-7. Every command through URbMatchDirector / URbStrokeComponent / URbReplaySubsystem / URbOverlayComponent; the
// queue and the RbDumpState format are described in the header. Tested by RawBreak.Functional.Replay (RbReplayTest.cpp).

namespace RbCheatUtil
{
	constexpr double QueueExpirySeconds = 60.0;
	constexpr double StrokeLeadSeconds = 0.25;      // first scripted sample after the get-down completes
	constexpr double StandBehindRailM = 0.35;       // pawn capsule (25 cm) + margin outside the table's outer boundary
	constexpr double MaxStandDistanceM = 1.40;      // < URbStrokeComponent::MaxReach (1.5 m)

	const TCHAR* PhaseName(ERbDirectorPhase Phase)
	{
		switch (Phase)
		{
		case ERbDirectorPhase::Idle: return TEXT("Idle");
		case ERbDirectorPhase::Lag: return TEXT("Lag");
		case ERbDirectorPhase::AwaitStroke: return TEXT("AwaitStroke");
		case ERbDirectorPhase::AwaitPlacement: return TEXT("AwaitPlacement");
		case ERbDirectorPhase::Simulating: return TEXT("Simulating");
		case ERbDirectorPhase::PlayingBack: return TEXT("PlayingBack");
		case ERbDirectorPhase::AwaitDecision: return TEXT("AwaitDecision");
		case ERbDirectorPhase::RackOver: return TEXT("RackOver");
		case ERbDirectorPhase::MatchOver: return TEXT("MatchOver");
		}
		return TEXT("?");
	}

	const TCHAR* DisciplineName(ERbDiscipline Discipline)
	{
		switch (Discipline)
		{
		case ERbDiscipline::NineBall: return TEXT("9-ball");
		case ERbDiscipline::EightBall: return TEXT("8-ball");
		case ERbDiscipline::TenBall: return TEXT("10-ball");
		case ERbDiscipline::StraightPool: return TEXT("14.1");
		}
		return TEXT("?");
	}

	const TCHAR* RegionName(rb::rules::CueBallNext Region)
	{
		switch (Region)
		{
		case rb::rules::CueBallNext::InPosition: return TEXT("InPosition");
		case rb::rules::CueBallNext::InHandAnywhere: return TEXT("InHandAnywhere");
		case rb::rules::CueBallNext::InHandAboveHeadString: return TEXT("InHandAboveHeadString");
		case rb::rules::CueBallNext::InHandBaulk: return TEXT("InHandBaulk");
		}
		return TEXT("?");
	}

	const TCHAR* OptionName(rb::rules::Option Option)
	{
		switch (Option)
		{
		case rb::rules::Option::AcceptTable: return TEXT("AcceptTable");
		case rb::rules::Option::BallInHandAboveHeadString: return TEXT("BallInHandAboveHeadString");
		case rb::rules::Option::RerackDeciderBreaks: return TEXT("RerackDeciderBreaks");
		case rb::rules::Option::RerackOffenderBreaks: return TEXT("RerackOffenderBreaks");
		case rb::rules::Option::Spot8ContinueFromPosition: return TEXT("Spot8ContinueFromPosition");
		case rb::rules::Option::Spot8BallInHandAboveHeadString: return TEXT("Spot8BallInHandAboveHeadString");
		case rb::rules::Option::AcceptTableNoPushOut: return TEXT("AcceptTableNoPushOut");
		case rb::rules::Option::HandBackPushOutAllowed: return TEXT("HandBackPushOutAllowed");
		case rb::rules::Option::ShootFromPosition: return TEXT("ShootFromPosition");
		case rb::rules::Option::PassBack: return TEXT("PassBack");
		case rb::rules::Option::RequireRebreak: return TEXT("RequireRebreak");
		}
		return TEXT("?");
	}

	const TCHAR* NextName(rb::rules::NextAction Next)
	{
		switch (Next)
		{
		case rb::rules::NextAction::Continue: return TEXT("Continue");
		case rb::rules::NextAction::Pass: return TEXT("Pass");
		case rb::rules::NextAction::AwaitDecision: return TEXT("AwaitDecision");
		case rb::rules::NextAction::RackWon: return TEXT("RackWon");
		case rb::rules::NextAction::MatchWon: return TEXT("MatchWon");
		case rb::rules::NextAction::RerackAndBreak: return TEXT("RerackAndBreak");
		}
		return TEXT("?");
	}

	// Where the pawn stands to address the ball at Center (core plan) with azimuth Phi: behind the ball on the aim line just
	// outside the table's outer boundary when that is within reach, else outside the nearest side of the table.
	rb::Vec2 StandingPoint(const rb::Aabb2& Outer, const rb::Vec2& Center, double Phi)
	{
		const rb::Vec2 Back(-FMath::Cos(Phi), -FMath::Sin(Phi));
		double Exit = TNumericLimits<double>::Max();
		if (Back.x > 1.0e-9)
		{
			Exit = FMath::Min(Exit, (Outer.Hi.x - Center.x) / Back.x);
		}
		else if (Back.x < -1.0e-9)
		{
			Exit = FMath::Min(Exit, (Outer.Lo.x - Center.x) / Back.x);
		}
		if (Back.y > 1.0e-9)
		{
			Exit = FMath::Min(Exit, (Outer.Hi.y - Center.y) / Back.y);
		}
		else if (Back.y < -1.0e-9)
		{
			Exit = FMath::Min(Exit, (Outer.Lo.y - Center.y) / Back.y);
		}
		const double Along = FMath::Max(0.0, Exit) + StandBehindRailM;
		if (Along <= MaxStandDistanceM)
		{
			return rb::Vec2(Center.x + Back.x * Along, Center.y + Back.y * Along);
		}
		// Nearest side of the table.
		const double ToLoX = Center.x - Outer.Lo.x;
		const double ToHiX = Outer.Hi.x - Center.x;
		const double ToLoY = Center.y - Outer.Lo.y;
		const double ToHiY = Outer.Hi.y - Center.y;
		const double Nearest = FMath::Min(FMath::Min(ToLoX, ToHiX), FMath::Min(ToLoY, ToHiY));
		if (Nearest == ToLoY)
		{
			return rb::Vec2(Center.x, Outer.Lo.y - StandBehindRailM);
		}
		if (Nearest == ToHiY)
		{
			return rb::Vec2(Center.x, Outer.Hi.y + StandBehindRailM);
		}
		if (Nearest == ToLoX)
		{
			return rb::Vec2(Outer.Lo.x - StandBehindRailM, Center.y);
		}
		return rb::Vec2(Outer.Hi.x + StandBehindRailM, Center.y);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------------------------------------------------

URbMatchDirector* URbCheatManager::GetDirector() const
{
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	return GameMode ? GameMode->GetDirector() : nullptr;
}

bool URbCheatManager::IsBusy() const
{
	const double Now = FPlatformTime::Seconds();
	if (Now < WaitUntil)
	{
		return true;
	}
	if (bStrokeInFlight && Now < StrokeDeadline && StrokeInFlight.IsValid())
	{
		return true;
	}
	if (const URbMatchDirector* Director = GetDirector())
	{
		const ERbDirectorPhase Phase = Director->GetPhase();
		return Phase == ERbDirectorPhase::Simulating || Phase == ERbDirectorPhase::PlayingBack;
	}
	return false;
}

void URbCheatManager::Enqueue(const TCHAR* Name, TFunction<void()> Run)
{
	ExpireStroke(); // a scripted stroke past its deadline is ended before anything else runs (Commit released, delegates unbound)
	if (Queue.Num() == 0 && !IsBusy())
	{
		Run();
		return;
	}
	FQueuedCommand& Command = Queue.AddDefaulted_GetRef();
	Command.Name = Name;
	Command.Run = MoveTemp(Run);
	Command.EnqueueTime = FPlatformTime::Seconds();
	UE_LOG(LogRawBreak, Display, TEXT("RbCheat: %s queued (%d waiting)"), Name, Queue.Num());
	EnsureQueueTimer();
}

void URbCheatManager::EnsureQueueTimer()
{
	UWorld* World = GetWorld();
	if (World && !World->GetTimerManager().IsTimerActive(QueueTimer))
	{
		World->GetTimerManager().SetTimer(QueueTimer, FTimerDelegate::CreateUObject(this, &URbCheatManager::DrainQueue), 0.02f, true);
	}
}

bool URbCheatManager::IsQueueTimerActive() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(QueueTimer);
}

void URbCheatManager::ExpireStroke()
{
	if (bStrokeInFlight && (FPlatformTime::Seconds() >= StrokeDeadline || !StrokeInFlight.IsValid()))
	{
		EndScriptedStroke(TEXT("timed out"));
	}
}

void URbCheatManager::DrainQueue()
{
	ExpireStroke();
	while (Queue.Num() > 0 && !IsBusy())
	{
		FQueuedCommand Command = MoveTemp(Queue[0]);
		Queue.RemoveAt(0);
		if (FPlatformTime::Seconds() - Command.EnqueueTime > RbCheatUtil::QueueExpirySeconds)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("RbCheat: %s expired in the queue"), *Command.Name);
			continue;
		}
		Command.Run();
	}
	// The timer also watches a scripted stroke's deadline: it stops only when nothing waits AND no stroke is in flight
	// (UE-7 review: clearing it with a stroke in flight left Commit held and the delegates bound after a stroke that never
	// reached the ball).
	if (Queue.Num() == 0 && !bStrokeInFlight)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(QueueTimer);
		}
	}
}

void URbCheatManager::StopReplayForMatchCommand()
{
	if (UWorld* World = GetWorld())
	{
		if (URbReplaySubsystem* Replay = World->GetSubsystem<URbReplaySubsystem>())
		{
			if (Replay->IsReplaying())
			{
				Replay->StopReplay();
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------------------------------

void URbCheatManager::RbStrike(float SpeedMps, float AzimuthDeg, float ElevationDeg, float OffsetA, float OffsetB)
{
	Enqueue(TEXT("RbStrike"), [this, SpeedMps, AzimuthDeg, ElevationDeg, OffsetA, OffsetB]() {
		StopReplayForMatchCommand();
		URbMatchDirector* Director = GetDirector();
		const bool bOk = Director && Director->SubmitScriptedStrike(SpeedMps, FMath::DegreesToRadians(AzimuthDeg), FMath::DegreesToRadians(ElevationDeg),
			OffsetA, OffsetB);
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbStrike %.3f m/s phi %.3f theta %.3f a %.3f b %.3f: %s"), SpeedMps, AzimuthDeg, ElevationDeg, OffsetA,
			OffsetB, bOk ? TEXT("submitted") : *FString::Printf(TEXT("refused (%s)"), Director ? *Director->GetLastError() : TEXT("no director")));
	});
}

void URbCheatManager::RbStroke(float SpeedMps, float AzimuthDeg)
{
	Enqueue(TEXT("RbStroke"), [this, SpeedMps, AzimuthDeg]() {
		StopReplayForMatchCommand();
		StartScriptedStroke(SpeedMps, AzimuthDeg);
	});
}

bool URbCheatManager::StartScriptedStroke(float SpeedMps, float AzimuthDeg)
{
	using namespace RbCheatUtil;
	URbMatchDirector* Director = GetDirector();
	URbStrokeComponent* Stroke = Director ? Director->GetStrokeComponent() : nullptr;
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	const ARbTable* Table = GameMode ? GameMode->GetTable() : nullptr;
	const auto Refuse = [SpeedMps, AzimuthDeg](const TCHAR* Why) {
		UE_LOG(LogRawBreak, Warning, TEXT("RbCheat: RbStroke %.3f m/s phi %.3f refused: %s"), SpeedMps, AzimuthDeg, Why);
		return false;
	};
	if (!Director || !Stroke || !Table || !Table->HasContext())
	{
		return Refuse(TEXT("no director / stroke component / table"));
	}
	if (!FMath::IsFinite(SpeedMps) || SpeedMps <= 0.0f || !FMath::IsFinite(AzimuthDeg))
	{
		return Refuse(TEXT("speed must be > 0"));
	}
	if (Director->GetPhase() != ERbDirectorPhase::AwaitStroke && Director->GetPhase() != ERbDirectorPhase::Lag)
	{
		return Refuse(Director->GetPhase() == ERbDirectorPhase::AwaitPlacement ? TEXT("ball in hand: RbPlaceCueBall first") : TEXT("no stroke expected now"));
	}
	const ERbStrokePhase Phase = Stroke->GetPhase();
	if (Phase != ERbStrokePhase::Walking && Phase != ERbStrokePhase::GettingDown && Phase != ERbStrokePhase::Down)
	{
		return Refuse(TEXT("the stroke component is not addressing a ball"));
	}

	// The struck ball (lag: the current lagger's) where the director armed the component.
	const int32 Struck = Director->GetPhase() == ERbDirectorPhase::Lag ? (Director->GetLagStroker() == 0 ? 0 : 1) : 0;
	const rb::Vec3 Ball = Director->GetTableState().Balls[Struck].State.Position;
	const double Phi = FMath::DegreesToRadians(static_cast<double>(AzimuthDeg));
	Stroke->SetAim(Phi, 0.0, 0.0, 0.0);

	// Step to the table: behind the ball, within reach (the stroke component only gets down near the cue ball).
	if (Phase == ERbStrokePhase::Walking)
	{
		if (APawn* Pawn = Cast<APawn>(Stroke->GetOwner()))
		{
			const rb::Vec2 Stand = StandingPoint(Table->GetContext().Geometry.OuterBoundary, rb::XY(Ball), Phi);
			const FVector Floor = Table->CoreToWorld(rb::Vec3(Stand.x, Stand.y, 0.0));
			const FVector Location(Floor.X, Floor.Y, Pawn->GetActorLocation().Z);
			const FVector Target = Table->CoreToWorld(Ball);
			const FRotator Facing = FRotator(0.0, (Target - Location).Rotation().Yaw, 0.0);
			Pawn->SetActorLocationAndRotation(Location, Facing, false, nullptr, ETeleportType::TeleportPhysics);
			if (AController* Controller = Pawn->GetController())
			{
				Controller->SetControlRotation(Facing);
			}
		}
		Stroke->RequestGetDownToggle();
	}
	if (Stroke->GetPhase() != ERbStrokePhase::GettingDown && Stroke->GetPhase() != ERbStrokePhase::Down)
	{
		return Refuse(TEXT("could not get down on the shot (out of reach?)"));
	}

	// A previous scripted stroke still tracked (a queued RbStroke cannot overlap one, but a direct call can): end it first,
	// so its delegate bindings never outlive it.
	EndScriptedStroke(TEXT("replaced"));

	// Live stroke: Commit held, the scripted hand samples start after the get-down (earlier samples would be dropped).
	Stroke->SetCommitHeld(true);
	const double Now = FPlatformTime::Seconds();
	const double Start = FMath::Max(Now, Stroke->GetDownSince()) + StrokeLeadSeconds;
	const TArray<FRbStrokeSample> Samples = Stroke->MakeScriptedStroke(SpeedMps, Start);
	if (Samples.Num() == 0)
	{
		Stroke->SetCommitHeld(false);
		return Refuse(TEXT("no scripted samples"));
	}
	Stroke->InjectStrokeSamples(Samples);

	StrokeInFlight = Stroke;
	bStrokeInFlight = true;
	StrokeDeadline = Now + StrokeTimeoutSeconds;
	ContactHandle = Stroke->OnStrokeContact.AddUObject(this, &URbCheatManager::OnScriptedStrokeContact);
	AbortHandle = Stroke->OnStrokeAborted.AddUObject(this, &URbCheatManager::OnScriptedStrokeAborted);
	EnsureQueueTimer(); // the queue timer also watches the stroke's timeout (it stays active until the stroke ends)
	UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbStroke %.3f m/s phi %.3f: %d samples from %.3f s after now (address %u)"), SpeedMps, AzimuthDeg,
		Samples.Num(), Start - Now, Stroke->GetAddressIndex());
	return true;
}

void URbCheatManager::OnScriptedStrokeContact(const FRbStrokeCommit& Commit)
{
	UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbStroke contact V=%.4f m/s phi=%.4f deg"), Commit.Intended.Speed,
		FMath::RadiansToDegrees(Commit.Intended.Azimuth));
	EndScriptedStroke(TEXT("contact"));
}

void URbCheatManager::OnScriptedStrokeAborted(bool bRampShown)
{
	EndScriptedStroke(bRampShown ? TEXT("aborted after the ramp") : TEXT("aborted"));
}

void URbCheatManager::EndScriptedStroke(const TCHAR* Why)
{
	if (!bStrokeInFlight)
	{
		return;
	}
	bStrokeInFlight = false;
	if (URbStrokeComponent* Stroke = StrokeInFlight.Get())
	{
		Stroke->OnStrokeContact.Remove(ContactHandle);
		Stroke->OnStrokeAborted.Remove(AbortHandle);
		// The player lets go of Commit (after contact nothing is live any more; the component already ended the stroke).
		Stroke->SetCommitHeld(false);
	}
	ContactHandle.Reset();
	AbortHandle.Reset();
	StrokeInFlight.Reset();
	UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbStroke %s"), Why);
}

void URbCheatManager::RbPlaceCueBall(float X, float Y)
{
	Enqueue(TEXT("RbPlaceCueBall"), [this, X, Y]() {
		StopReplayForMatchCommand();
		URbMatchDirector* Director = GetDirector();
		const bool bOk = Director && Director->PlaceCueBall(rb::Vec2(X, Y));
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbPlaceCueBall (%.4f, %.4f): %s"), X, Y, bOk ? TEXT("placed") : TEXT("refused"));
	});
}

void URbCheatManager::RbChoose(int32 OptionIndex)
{
	Enqueue(TEXT("RbChoose"), [this, OptionIndex]() {
		StopReplayForMatchCommand();
		URbMatchDirector* Director = GetDirector();
		bool bOk = false;
		if (Director && Director->GetPhase() == ERbDirectorPhase::AwaitDecision)
		{
			const auto& Options = Director->GetMatchState().PendingOutcome.Options;
			if (OptionIndex >= 0 && OptionIndex < static_cast<int32>(Options.Size()))
			{
				bOk = Director->ChooseOption(Options[OptionIndex]);
			}
		}
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbChoose %d: %s"), OptionIndex, bOk ? TEXT("applied") : TEXT("refused (no decision / no such option)"));
	});
}

void URbCheatManager::RbRerack()
{
	Enqueue(TEXT("RbRerack"), [this]() {
		StopReplayForMatchCommand();
		URbMatchDirector* Director = GetDirector();
		const bool bOk = Director && Director->RequestRerack();
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbRerack: %s"), bOk ? TEXT("racked") : TEXT("refused"));
	});
}

void URbCheatManager::RbNewMatch(int32 Mode)
{
	Enqueue(TEXT("RbNewMatch"), [this, Mode]() {
		StopReplayForMatchCommand();
		const ARbGameMode* GameMode = ARbGameMode::Get(this);
		URbMatchDirector* Director = GameMode ? GameMode->GetDirector() : nullptr;
		if (!Director)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("RbCheat: RbNewMatch: no director"));
			return;
		}
		// The setup the game started with (URL options, its seed - 0 draws a new random one), in the requested mode.
		FRbMatchSetup Setup = GameMode->GetStartSetup();
		Setup.Mode = Mode == 1 ? ERbMatchMode::HotSeat : ERbMatchMode::Practice;
		const bool bOk = Director->StartMatch(Setup);
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbNewMatch %s: %s"), Mode == 1 ? TEXT("hot-seat") : TEXT("practice"), bOk ? TEXT("started") : TEXT("refused"));
	});
}

void URbCheatManager::RbReplay(int32 View, float Rate, float StartTime)
{
	// Queued behind a live shot: a script can strike and replay in one batch (the subsystem itself refuses while it plays).
	Enqueue(TEXT("RbReplay"), [this, View, Rate, StartTime]() {
		UWorld* World = GetWorld();
		URbReplaySubsystem* Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
		const ERbReplayView ReplayView = static_cast<ERbReplayView>(FMath::Clamp(View, 0, 3));
		const bool bOk = Replay && Replay->PlayReplayFrom(0, ReplayView, FMath::Max(0.0f, Rate), FMath::Max(0.0f, StartTime));
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbReplay %s %.2fx from %.3f s: %s"), URbReplaySubsystem::ViewName(ReplayView), Rate, StartTime,
			bOk ? TEXT("playing") : TEXT("refused (no shot / live shot running)"));
	});
}

void URbCheatManager::RbStopReplay()
{
	StopReplayForMatchCommand();
}

void URbCheatManager::RbOverlay(int32 Mode)
{
	const ARbPlayerController* PC = Cast<ARbPlayerController>(GetOuterAPlayerController());
	URbOverlayComponent* Overlay = PC ? PC->GetOverlay() : nullptr;
	if (!Overlay)
	{
		UE_LOG(LogRawBreak, Warning, TEXT("RbCheat: RbOverlay: no overlay component"));
		return;
	}
	Overlay->SetPinned(Mode >= 1);
	Overlay->SetDebugShown(Mode >= 2);
	UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbOverlay %d: %s%s"), Mode, Mode >= 1 ? TEXT("pinned") : TEXT("hidden"), Mode >= 2 ? TEXT(" + debug") : TEXT(""));
}

void URbCheatManager::RbPlaybackRate(float Rate)
{
	URbMatchDirector* Director = GetDirector();
	if (!Director || !FMath::IsFinite(Rate))
	{
		UE_LOG(LogRawBreak, Warning, TEXT("RbCheat: RbPlaybackRate <rate>: needs a running match and a finite rate"));
		return;
	}
	Director->SetLivePlaybackRate(Rate);
	UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbPlaybackRate %.3f"), Director->GetLivePlaybackRate());
	DrainQueue(); // rate 0 may have committed the shot that held the queue
}

void URbCheatManager::RbDeclare(int32 Kind, int32 Ball, int32 Pocket)
{
	Enqueue(TEXT("RbDeclare"), [this, Kind, Ball, Pocket]() {
		StopReplayForMatchCommand();
		URbMatchDirector* Director = GetDirector();
		if (!Director)
		{
			return;
		}
		const rb::rules::ShotKind ShotKind = Kind == 1 ? rb::rules::ShotKind::PushOut
			: (Kind == 2 ? rb::rules::ShotKind::Safety : (Director->GetMatchState().Game.IsBreakShot ? rb::rules::ShotKind::Break : rb::rules::ShotKind::Normal));
		Director->SetShotKind(ShotKind);
		Director->SetCalledShot(Ball, Pocket);
		const rb::rules::ShotDeclaration& D = Director->GetDeclaration();
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbDeclare %d %d %d: kind %d, call %d in %d"), Kind, Ball, Pocket, static_cast<int32>(D.Kind),
			D.Called.Ball == rb::kNoBall ? -1 : static_cast<int32>(D.Called.Ball), D.Called.Pocket == rb::PocketId::None ? -1 : static_cast<int32>(D.Called.Pocket));
	});
}

void URbCheatManager::RbWait(float Seconds)
{
	Enqueue(TEXT("RbWait"), [this, Seconds]() {
		WaitUntil = FPlatformTime::Seconds() + FMath::Max(0.0f, Seconds);
		EnsureQueueTimer();
		UE_LOG(LogRawBreak, Display, TEXT("RbCheat: RbWait %.2f s"), Seconds);
	});
}

void URbCheatManager::RbDumpState()
{
	Enqueue(TEXT("RbDumpState"), [this]() { UE_LOG(LogRawBreak, Display, TEXT("%s"), *MakeStateLine()); });
}

FString URbCheatManager::MakeStateLine() const
{
	using namespace RbCheatUtil;
	const URbMatchDirector* Director = GetDirector();
	if (!Director)
	{
		return TEXT("RbState: phase=None");
	}
	const rb::rules::MatchState& S = Director->GetMatchState();
	const rb::rules::GameState& G = S.Game;
	const FRbLastShotSummary& Last = Director->GetLastShot();
	FString Options;
	if (Director->GetPhase() == ERbDirectorPhase::AwaitDecision)
	{
		for (int32 Index = 0; Index < static_cast<int32>(S.PendingOutcome.Options.Size()); ++Index)
		{
			Options += (Index > 0 ? TEXT(",") : TEXT(""));
			Options += OptionName(S.PendingOutcome.Options[Index]);
		}
	}
	FString Pocketed;
	for (const int32 Ball : Last.Pocketed)
	{
		Pocketed += (Pocketed.IsEmpty() ? TEXT("") : TEXT(","));
		Pocketed += FString::FromInt(Ball);
	}
	FString Balls;
	const FRbTableState& Table = Director->GetTableState();
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		if (Table.Balls[Id].InPlay)
		{
			Balls += FString::Printf(TEXT("%s%d(%.6f,%.6f)"), Balls.IsEmpty() ? TEXT("") : TEXT(" "), Id, Table.Balls[Id].State.Position.x,
				Table.Balls[Id].State.Position.y);
		}
	}
	int32 History = 0;
	bool bReplaying = false;
	if (const UWorld* World = GetWorld())
	{
		if (const URbReplaySubsystem* Replay = World->GetSubsystem<URbReplaySubsystem>())
		{
			History = Replay->GetShotCount();
			bReplaying = Replay->IsReplaying();
		}
	}
	const TSharedPtr<const FRbShot> LastShot = Director->GetLastCommittedShot();
	const bool bHuman = LastShot.IsValid() && LastShot->Request.Stroke.bHuman;
	return FString::Printf(TEXT("RbState: phase=%s mode=%s game=%s rack=%d shot=%u shooter=%d wins=%d:%d scores=%d:%d fouls=%d:%d inhand=%d placed=%d region=%s ")
		TEXT("decider=%d options=[%s] selected=%d replay=%d history=%d rate=%.3f last{valid=%d shooter=%d fouls=%08x enforced=%d rule=\"%s\" pocketed=[%s] ")
		TEXT("first=%d next=%s speed=%.4f human=%d sim=%.3f} balls=[%s]"),
		PhaseName(Director->GetPhase()), Director->GetSetup().Mode == ERbMatchMode::HotSeat ? TEXT("HotSeat") : TEXT("Practice"),
		DisciplineName(Director->GetSetup().Discipline), S.RackNumber, Director->GetMatchShotIndex(), Director->GetActivePlayer(), S.RackWins[0], S.RackWins[1],
		G.Players[0].Score, G.Players[1].Score, G.Players[0].ConsecutiveFouls, G.Players[1].ConsecutiveFouls, Director->IsCueBallInHand() ? 1 : 0,
		Director->IsCueBallPlaced() ? 1 : 0, RegionName(Director->GetConstraints().PlacementRegion),
		Director->GetPhase() == ERbDirectorPhase::AwaitDecision ? S.Decider : -1, *Options, Director->GetSelectedOption(), bReplaying ? 1 : 0, History,
		Director->GetLivePlaybackRate(), Last.bValid ? 1 : 0, Last.Shooter, Last.Fouls.Bits,
		Last.Enforced == rb::rules::Foul::Count ? -1 : static_cast<int32>(Last.Enforced), *Last.RuleRef, *Pocketed,
		Last.FirstContactBall >= 1 && Last.FirstContactBall < rb::rules::kRulesBallCount ? Last.FirstContactBall : -1, NextName(Last.Next), Last.CueSpeed,
		bHuman ? 1 : 0, Last.SimMilliseconds, *Balls);
}
