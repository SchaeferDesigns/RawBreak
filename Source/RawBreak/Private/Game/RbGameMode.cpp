#include "Game/RbGameMode.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Cue/RbCue.h"
#include "Game/RbTableSubsystem.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

#include "rb/Human/CueState.h"
#include "rb/Rules/Evaluate.h"

// Owner: UE-6b, M2-E (sessions). Option parsing, scene setup (one session per table of the level: ball set, cue, director,
// registered with URbTableSubsystem; the player's session wires the pawn's stroke component), match start, dev tools
// (rb.Match.*, the player's director); tests RawBreak.Unit.Match.Options, RawBreak.Functional.MatchFlow and
// RawBreak.Functional.MultiTable.

namespace
{
	TAutoConsoleVariable<int32> CVarRbDrawTableState(TEXT("rb.Match.DrawTableState"), 0,
		TEXT("Debug lines of the match director's table state: 1 = balls (core FRbTableState), playing surface, head string, spots and pocket openings."),
		ECVF_Cheat);

	bool ParseBool(const FString& Value, bool& Out)
	{
		if (Value == TEXT("1") || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("on"), ESearchCase::IgnoreCase))
		{
			Out = true;
			return true;
		}
		if (Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Out = false;
			return true;
		}
		return false;
	}

	bool ParseDouble(const FString& Value, double& Out)
	{
		if (Value.IsEmpty() || !Value.IsNumeric())
		{
			return false;
		}
		Out = FCString::Atod(*Value);
		return FMath::IsFinite(Out);
	}

	FColor BallColor(int32 Id)
	{
		switch (Id == 0 ? 0 : ((Id - 1) % 8) + 1)
		{
		case 0: return FColor(245, 245, 235);
		case 1: return FColor(255, 200, 0);
		case 2: return FColor(20, 70, 230);
		case 3: return FColor(225, 25, 25);
		case 4: return FColor(120, 40, 170);
		case 5: return FColor(255, 115, 0);
		case 6: return FColor(0, 150, 70);
		case 7: return FColor(130, 25, 25);
		default: return FColor(70, 70, 75); // 8 (lighter than black so it shows on a dark floor)
		}
	}

#if ENABLE_DRAW_DEBUG
	// Seven-segment digits drawn with debug lines in the table plane (readable from above, screen right = AxisX), so ball ids
	// show in headless captures without a HUD. Segments a..g as bits 0..6.
	void DrawDigits(const UWorld* World, const FVector& Center, int32 Number, double Height, const FVector& AxisX, const FVector& AxisY, const FColor& Color)
	{
		static const uint8 Masks[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
		const FString Text = FString::FromInt(FMath::Max(0, Number));
		const double W = 0.5 * Height;
		const double Gap = 0.3 * W;
		const double Total = Text.Len() * W + (Text.Len() - 1) * Gap;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const uint8 Mask = Masks[FMath::Clamp(static_cast<int32>(Text[i] - TEXT('0')), 0, 9)];
			const double X0 = -0.5 * Total + i * (W + Gap);
			const double H = 0.5 * Height;
			const auto P = [&](double X, double Y) { return Center + AxisX * (X0 + X) + AxisY * Y; };
			const FVector Pts[6] = {P(0, H), P(W, H), P(W, 0), P(W, -H), P(0, -H), P(0, 0)}; // TL TR MR BR BL ML
			const int32 Seg[7][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}, {5, 2}};   // a b c d e f g
			for (int32 S = 0; S < 7; ++S)
			{
				if (Mask & (1u << S))
				{
					DrawDebugLine(World, Pts[Seg[S][0]], Pts[Seg[S][1]], Color, false, -1.0f, SDPG_Foreground, 0.28f);
				}
			}
		}
	}
#endif

	URbMatchDirector* FindDirector(UWorld* World)
	{
		const ARbGameMode* GameMode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
		return GameMode ? GameMode->GetDirector() : nullptr;
	}

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

	void DumpDirector(const URbMatchDirector& Director)
	{
		const rb::rules::MatchState& S = Director.GetMatchState();
		const FRbLastShotSummary& Last = Director.GetLastShot();
		FString BallList;
		const FRbTableState& Table = Director.GetTableState();
		for (int32 Id = 0; Id < rb::rules::kRulesBallCount; ++Id)
		{
			if (Table.Balls[Id].InPlay)
			{
				BallList += FString::Printf(TEXT(" %d(%.4f,%.4f)"), Id, Table.Balls[Id].State.Position.x, Table.Balls[Id].State.Position.y);
			}
		}
		FString Pocketed;
		for (const int32 Ball : Last.Pocketed)
		{
			Pocketed += FString::Printf(TEXT(" %d"), Ball);
		}
		UE_LOG(LogRawBreak, Display, TEXT("RbMatch: phase=%s rack=%d shooter=%d wins=%d:%d fouls=%d:%d shot=%u inhand=%d last{fouls=%08x rule=%s pocketed=[%s] first=%d next=%d} balls=[%s]"),
			PhaseName(Director.GetPhase()), S.RackNumber, S.Game.Shooter, S.RackWins[0], S.RackWins[1], S.Game.Players[0].ConsecutiveFouls,
			S.Game.Players[1].ConsecutiveFouls, Director.GetMatchShotIndex(), Director.IsCueBallInHand() ? 1 : 0, Last.Fouls.Bits, *Last.RuleRef,
			*Pocketed, Last.FirstContactBall, static_cast<int32>(Last.Next), *BallList);
	}

	void PlaceCommand(const TArray<FString>& Args, UWorld* World)
	{
		URbMatchDirector* Director = FindDirector(World);
		double X = 0.0;
		double Y = 0.0;
		if (!Director || Args.Num() < 2 || !ParseDouble(Args[0], X) || !ParseDouble(Args[1], Y))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.Place <x m> <y m> (core table frame); needs a running ARbGameMode"));
			return;
		}
		const bool bPlaced = Director->PlaceCueBall(rb::Vec2(X, Y));
		UE_LOG(LogRawBreak, Display, TEXT("rb.Match.Place (%.4f, %.4f): %s"), X, Y, bPlaced ? TEXT("placed") : TEXT("refused"));
	}

	void StrikeCommand(const TArray<FString>& Args, UWorld* World)
	{
		URbMatchDirector* Director = FindDirector(World);
		double V[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
		bool bValid = Director && Args.Num() >= 2;
		for (int32 i = 0; bValid && i < Args.Num() && i < 5; ++i)
		{
			bValid = ParseDouble(Args[i], V[i]);
		}
		if (!bValid)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.Strike <speed m/s> <azimuth deg> [<elevation deg> <a> <b>]"));
			return;
		}
		const bool bOk = Director->SubmitScriptedStrike(V[0], FMath::DegreesToRadians(V[1]), FMath::DegreesToRadians(V[2]), V[3], V[4]);
		UE_LOG(LogRawBreak, Display, TEXT("rb.Match.Strike: %s"), bOk ? TEXT("submitted") : TEXT("refused"));
	}

	// Ball in hand behind the head string (if needed), then a scripted break at the apex (lowest) ball.
	void BreakCommand(const TArray<FString>& Args, UWorld* World)
	{
		URbMatchDirector* Director = FindDirector(World);
		double Speed = 9.0;
		if (!Director || (Args.Num() > 0 && !ParseDouble(Args[0], Speed)))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.Break [<speed m/s>]"));
			return;
		}
		const rb::rules::MatchConfig& Config = Director->GetMatchConfig();
		if (Director->GetPhase() == ERbDirectorPhase::AwaitPlacement)
		{
			Director->PlaceCueBall(rb::Vec2(Config.Table.HeadStringX - 0.12, 0.08));
		}
		const int32 Apex = rb::rules::LowestObjectBallAtStart(Director->GetMatchState().Game);
		const FRbTableState& Table = Director->GetTableState();
		if (Apex <= 0 || !Table.Balls[0].InPlay)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.Break: no cue ball / object ball"));
			return;
		}
		const rb::Vec3 Aim = Table.Balls[Apex].State.Position - Table.Balls[0].State.Position;
		const bool bOk = Director->SubmitScriptedStrike(Speed, FMath::Atan2(Aim.y, Aim.x), 0.0, 0.0, -0.1);
		UE_LOG(LogRawBreak, Display, TEXT("rb.Match.Break %.2f m/s at ball %d: %s"), Speed, Apex, bOk ? TEXT("submitted") : TEXT("refused"));
	}

	void RateCommand(const TArray<FString>& Args, UWorld* World)
	{
		URbMatchDirector* Director = FindDirector(World);
		double Rate = 1.0;
		if (!Director || Args.Num() < 1 || !ParseDouble(Args[0], Rate))
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.Rate <live playback rate, 0 = commit at once>"));
			return;
		}
		Director->SetLivePlaybackRate(static_cast<float>(Rate));
	}

	void ConfirmCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (URbMatchDirector* Director = FindDirector(World))
		{
			UE_LOG(LogRawBreak, Display, TEXT("rb.Match.Confirm: %s"), Director->Confirm() ? TEXT("done") : TEXT("nothing to confirm"));
		}
	}

	// Mid-rack layout: rb.Match.Layout <id> <x> <y> [<id> <x> <y> ...] (core table frame; the cue ball = id 0 in position).
	void LayoutCommand(const TArray<FString>& Args, UWorld* World)
	{
		URbMatchDirector* Director = FindDirector(World);
		if (!Director || Args.Num() < 3 || Args.Num() % 3 != 0)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.Layout <id> <x> <y> [...]"));
			return;
		}
		FRbTableState State = Director->GetTableState();
		for (rb::SimBall& Ball : State.Balls)
		{
			Ball.InPlay = false;
		}
		for (int32 i = 0; i + 2 < Args.Num(); i += 3)
		{
			double Id = 0.0;
			double X = 0.0;
			double Y = 0.0;
			if (!ParseDouble(Args[i], Id) || !ParseDouble(Args[i + 1], X) || !ParseDouble(Args[i + 2], Y) || Id < 0.0 || Id >= rb::kMaxBalls)
			{
				UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.Layout: bad triple %d"), i / 3);
				return;
			}
			rb::SimBall& Ball = State.Balls[static_cast<int32>(Id)];
			Ball.InPlay = true;
			Ball.State.Position = rb::Vec3(X, Y, Ball.Spec.Radius);
		}
		Director->SetTableStateForTest(State);
	}

	// Scripted strike aimed at a plan point: rb.Match.StrikeAt <speed m/s> <x> <y> [<b>].
	void StrikeAtCommand(const TArray<FString>& Args, UWorld* World)
	{
		URbMatchDirector* Director = FindDirector(World);
		double V[4] = {0.0, 0.0, 0.0, 0.0};
		bool bValid = Director && Args.Num() >= 3;
		for (int32 i = 0; bValid && i < Args.Num() && i < 4; ++i)
		{
			bValid = ParseDouble(Args[i], V[i]);
		}
		if (!bValid || !Director->GetTableState().Balls[0].InPlay)
		{
			UE_LOG(LogRawBreak, Warning, TEXT("rb.Match.StrikeAt <speed m/s> <x> <y> [<b>] (cue ball in play)"));
			return;
		}
		const rb::Vec3 Cue = Director->GetTableState().Balls[0].State.Position;
		const bool bOk = Director->SubmitScriptedStrike(V[0], FMath::Atan2(V[2] - Cue.y, V[1] - Cue.x), 0.0, 0.0, V[3]);
		UE_LOG(LogRawBreak, Display, TEXT("rb.Match.StrikeAt: %s"), bOk ? TEXT("submitted") : TEXT("refused"));
	}

	void DumpCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (const URbMatchDirector* Director = FindDirector(World))
		{
			DumpDirector(*Director);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GRbPlaceCommand(TEXT("rb.Match.Place"), TEXT("Ball in hand: place the cue ball at core table (x, y) [m]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaceCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GRbStrikeCommand(TEXT("rb.Match.Strike"), TEXT("Scripted strike: speed [m/s], azimuth [deg], elevation [deg], a, b."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StrikeCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GRbBreakCommand(TEXT("rb.Match.Break"), TEXT("Places the cue ball behind the head string if in hand and breaks at the apex ball [speed m/s]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BreakCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GRbRateCommand(TEXT("rb.Match.Rate"), TEXT("Live playback rate of the match director (0 = commit at once)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RateCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GRbConfirmCommand(TEXT("rb.Match.Confirm"), TEXT("Confirm: decision option / next rack / new match."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConfirmCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GRbLayoutCommand(TEXT("rb.Match.Layout"), TEXT("Mid-rack test layout: <id> <x> <y> triples (core table frame)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LayoutCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GRbStrikeAtCommand(TEXT("rb.Match.StrikeAt"), TEXT("Scripted strike at a plan point: speed [m/s], x, y [m], b."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StrikeAtCommand), ECVF_Cheat);
	FAutoConsoleCommandWithWorldAndArgs GRbDumpCommand(TEXT("rb.Match.Dump"), TEXT("Logs the match director state (RbMatch: ...)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpCommand), ECVF_Cheat);
}

namespace RbGameModeDebug
{
	// Debug lines of one session's table state (rb.Match.DrawTableState).
	void DrawSessionState(UWorld* World, const ARbTable* Table, const URbMatchDirector* Director);
}

ARbGameMode::ARbGameMode()
{
	DefaultPawnClass = ARbPlayerCharacter::StaticClass();
	PlayerControllerClass = ARbPlayerController::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

ARbGameMode* ARbGameMode::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
}

bool ARbGameMode::ParseMatchOptions(const FString& Options, FRbMatchSetup& InOut, float* OutPlaybackRate)
{
	bool bAllValid = true;
	const auto Get = [&Options](const TCHAR* Key, FString& Out) -> bool {
		if (!UGameplayStatics::HasOption(Options, Key))
		{
			return false;
		}
		Out = UGameplayStatics::ParseOption(Options, Key).TrimStartAndEnd();
		return true;
	};
	FString Value;
	double Number = 0.0;
	bool Flag = false;

	if (Get(TEXT("Mode"), Value))
	{
		if (Value.Equals(TEXT("Practice"), ESearchCase::IgnoreCase) || Value == TEXT("0"))
		{
			InOut.Mode = ERbMatchMode::Practice;
		}
		else if (Value.Equals(TEXT("HotSeat"), ESearchCase::IgnoreCase) || Value == TEXT("1"))
		{
			InOut.Mode = ERbMatchMode::HotSeat;
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Game"), Value))
	{
		if (Value.Equals(TEXT("NineBall"), ESearchCase::IgnoreCase) || Value == TEXT("9"))
		{
			InOut.Discipline = ERbDiscipline::NineBall;
		}
		else if (Value.Equals(TEXT("EightBall"), ESearchCase::IgnoreCase) || Value == TEXT("8"))
		{
			InOut.Discipline = ERbDiscipline::EightBall;
		}
		else if (Value.Equals(TEXT("TenBall"), ESearchCase::IgnoreCase) || Value == TEXT("10"))
		{
			InOut.Discipline = ERbDiscipline::TenBall;
		}
		else if (Value.Equals(TEXT("StraightPool"), ESearchCase::IgnoreCase) || Value == TEXT("14.1"))
		{
			InOut.Discipline = ERbDiscipline::StraightPool;
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Race"), Value))
	{
		if (ParseDouble(Value, Number) && Number >= 1.0 && Number <= 1000.0 && Number == FMath::FloorToDouble(Number))
		{
			InOut.RaceTo = static_cast<int32>(Number);
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Lag"), Value))
	{
		if (ParseBool(Value, Flag))
		{
			InOut.bLag = Flag;
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Seed"), Value))
	{
		if (!Value.IsEmpty() && Value.IsNumeric() && !Value.Contains(TEXT(".")))
		{
			InOut.Seed = FCString::Atoi64(*Value);
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Attr"), Value))
	{
		if (ParseDouble(Value, Number) && Number >= 0.0 && Number <= 100.0)
		{
			InOut.ShooterAttribute = Number;
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Pressure"), Value))
	{
		if (ParseBool(Value, Flag))
		{
			InOut.bPressure = Flag;
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Noise"), Value))
	{
		if (ParseDouble(Value, Number) && Number >= 0.0)
		{
			InOut.NoiseScale = Number;
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("Rate"), Value))
	{
		if (ParseDouble(Value, Number) && Number >= 0.0)
		{
			if (OutPlaybackRate)
			{
				*OutPlaybackRate = static_cast<float>(Number);
			}
		}
		else
		{
			bAllValid = false;
		}
	}
	if (Get(TEXT("P1"), Value) && !Value.IsEmpty())
	{
		InOut.Player1 = Value;
	}
	if (Get(TEXT("P2"), Value) && !Value.IsEmpty())
	{
		InOut.Player2 = Value;
	}
	return bAllValid;
}

void ARbGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	Setup = DefaultSetup;
	PlaybackRate = DefaultPlaybackRate;
	if (!ParseMatchOptions(Options, Setup, &PlaybackRate))
	{
		UE_LOG(LogRawBreak, Warning, TEXT("ARbGameMode: invalid match option in '%s' (ignored)"), *Options);
	}
}

void ARbGameMode::StartPlay()
{
	Super::StartPlay();
	SetupScene();
	for (const FRbGameModeSession& Session : Sessions)
	{
		URbMatchDirector* SessionDirector = Session.Director;
		if (!SessionDirector)
		{
			continue;
		}
		const bool bPlayer = SessionDirector == Director;
		const FRbMatchSetup SessionSetup = bPlayer ? Setup : MakeIdleSetup(Session.Table ? Session.Table->TableIndex : 0);
		SessionDirector->SetLivePlaybackRate(PlaybackRate);
		if (!SessionDirector->StartMatch(SessionSetup))
		{
			UE_LOG(LogRawBreak, Error, TEXT("ARbGameMode: the match of table %d could not start: %s"), Session.Table ? Session.Table->TableIndex : -1,
				*SessionDirector->GetLastError());
		}
	}
}

void ARbGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	URbTableSubsystem* Tables = URbTableSubsystem::Get(this);
	for (const FRbGameModeSession& Session : Sessions)
	{
		if (Session.Director)
		{
			Session.Director->Shutdown();
		}
		if (Tables && Session.Table)
		{
			Tables->UnregisterSession(Session.Table->TableIndex);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ARbGameMode::SetupScene()
{
	UWorld* World = GetWorld();
	URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
	if (!World || !Tables)
	{
		return;
	}
	// Every table of the level (18.6.2); a level without one gets a table at the origin (engine maps in the M1 tests).
	TArray<ARbTable*> LevelTables = Tables->GetTables();
	if (LevelTables.IsEmpty())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ARbTable* Spawned = World->SpawnActor<ARbTable>(ARbTable::StaticClass(), FTransform::Identity, Params))
		{
			LevelTables.Add(Spawned);
		}
	}
	FString Report;
	if (!Tables->ValidateTables(Report))
	{
		UE_LOG(LogRawBreak, Error, TEXT("ARbGameMode: the level's tables are invalid (sessions are keyed by TableIndex):\n%s"), *Report);
	}
	ARbTable* PlayerTable = Tables->GetPlayerTable();
	if (!PlayerTable && LevelTables.Num() > 0)
	{
		PlayerTable = LevelTables[0];
	}

	for (ARbTable* SessionTable : LevelTables)
	{
		const bool bPlayer = SessionTable == PlayerTable;
		const FRbGameModeSession Session = MakeSession(SessionTable, bPlayer);
		if (bPlayer)
		{
			Table = Session.Table;
			BallSet = Session.BallSet;
			Cue = Session.Cue;
			Director = Session.Director;
		}
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			WirePawn(PC->GetPawn());
		}
	}
}

FRbGameModeSession ARbGameMode::MakeSession(ARbTable* SessionTable, bool bPlayer)
{
	FRbGameModeSession Session;
	UWorld* World = GetWorld();
	URbTableSubsystem* Tables = URbTableSubsystem::Get(World);
	if (!World || !Tables || !SessionTable)
	{
		return Session;
	}
	if (FRbGameModeSession* Existing = Sessions.FindByPredicate([SessionTable](const FRbGameModeSession& S) { return S.Table == SessionTable; }))
	{
		return *Existing; // SetupScene again (idempotent)
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Session.Table = SessionTable;
	if (!SessionTable->HasContext())
	{
		SessionTable->RebuildTable();
	}
	// A ball set already initialised for this table (placed and bound by another system), else one placed in the level without a
	// table (dev maps, M1 behaviour), else a new one; one cue per table.
	Session.BallSet = Tables->FindBallSet(SessionTable);
	if (!Session.BallSet)
	{
		Session.BallSet = Tables->FindUnassignedBallSet();
	}
	if (!Session.BallSet)
	{
		Session.BallSet = World->SpawnActor<ARbBallSet>(ARbBallSet::StaticClass(), FTransform::Identity, Params);
	}
	Session.Cue = World->SpawnActor<ARbCue>(ARbCue::StaticClass(), FTransform::Identity, Params);
	if (SessionTable->HasContext())
	{
		if (Session.BallSet)
		{
			Session.BallSet->InitForTable(SessionTable);
		}
		if (Session.Cue)
		{
			const FRbShooterState DefaultShooter; // M1: both shooters play the same house cue
			Session.Cue->InitForTable(SessionTable, DefaultShooter.Cue, DefaultShooter.CueBody);
			Session.Cue->SetDrive(ERbCueDrive::Hidden);
		}
	}
	else
	{
		UE_LOG(LogRawBreak, Error, TEXT("ARbGameMode: table %s (TableIndex %d) has no valid table context"), *SessionTable->GetName(), SessionTable->TableIndex);
	}
	if (Session.BallSet && Session.Cue)
	{
		if (URbShotPlaybackComponent* Playback = Session.BallSet->GetPlayback())
		{
			Playback->SetCue(Session.Cue);
		}
	}
	const FName DirectorName = bPlayer ? FName(TEXT("MatchDirector")) : FName(*FString::Printf(TEXT("MatchDirector_Table%d"), SessionTable->TableIndex));
	Session.Director = NewObject<URbMatchDirector>(this, MakeUniqueObjectName(this, URbMatchDirector::StaticClass(), DirectorName));
	Session.Director->Initialize(SessionTable, Session.BallSet, Session.Cue, World->GetSubsystem<URbSimulationSubsystem>());
	Session.Director->SetRecordsReplays(bPlayer); // the replay history is the player's table only (M2)
	Sessions.Add(Session);

	FRbTableSession Registered;
	Registered.TableIndex = SessionTable->TableIndex;
	Registered.Table = SessionTable;
	Registered.BallSet = Session.BallSet.Get();
	Registered.Cue = Session.Cue.Get();
	Registered.Director = Session.Director.Get();
	Tables->RegisterSession(Registered);
	return Session;
}

FRbMatchSetup ARbGameMode::MakeIdleSetup(int32 TableIndex) const
{
	FRbMatchSetup Idle;
	Idle.Mode = ERbMatchMode::Practice;
	Idle.Discipline = ERbDiscipline::NineBall;
	Idle.Seed = Setup.Seed != 0 ? static_cast<int64>(HashCombineFast(GetTypeHash(Setup.Seed), GetTypeHash(TableIndex + 1)) | 1u) : 0;
	return Idle;
}

void ARbGameMode::WirePawn(APawn* Pawn)
{
	ARbPlayerCharacter* Character = Cast<ARbPlayerCharacter>(Pawn);
	URbStrokeComponent* Stroke = Character ? Character->GetStroke() : nullptr;
	if (!Stroke || !Director)
	{
		return;
	}
	Stroke->SetTable(Table);
	Stroke->SetCue(Cue);
	Director->SetStrokeComponent(Stroke); // one pawn for both hot-seat players
}

void ARbGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);
	WirePawn(NewPlayer ? NewPlayer->GetPawn() : nullptr);
}

void ARbGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (CVarRbDrawTableState.GetValueOnGameThread() > 0)
	{
		DrawTableStateDebug();
	}
}

void ARbGameMode::DrawTableStateDebug() const
{
	for (const FRbGameModeSession& Session : Sessions)
	{
		RbGameModeDebug::DrawSessionState(GetWorld(), Session.Table, Session.Director);
	}
}

void RbGameModeDebug::DrawSessionState(UWorld* World, const ARbTable* Table, const URbMatchDirector* Director)
{
#if ENABLE_DRAW_DEBUG
	if (!World || !Director || !Table || !Table->HasContext())
	{
		return;
	}
	const rb::rules::RulesTable& Rules = Table->GetContext().RulesTable;
	const auto At = [Table](double X, double Y, double Z = 0.0) { return Table->CoreToWorld(rb::Vec3(X, Y, Z)); };
	const FVector AxisX = Table->CoreDirectionToWorld(rb::Vec3(1.0, 0.0, 0.0));
	const FVector AxisY = Table->CoreDirectionToWorld(rb::Vec3(0.0, 1.0, 0.0));
	const uint8 Depth = SDPG_Foreground;
	const double HalfL = 0.5 * Rules.Length;
	const double HalfW = 0.5 * Rules.Width;

	// Playing surface (nose to nose), head string, spots.
	const FColor Surface(90, 200, 120);
	DrawDebugLine(World, At(-HalfL, -HalfW), At(HalfL, -HalfW), Surface, false, -1.0f, Depth, 0.4f);
	DrawDebugLine(World, At(HalfL, -HalfW), At(HalfL, HalfW), Surface, false, -1.0f, Depth, 0.4f);
	DrawDebugLine(World, At(HalfL, HalfW), At(-HalfL, HalfW), Surface, false, -1.0f, Depth, 0.4f);
	DrawDebugLine(World, At(-HalfL, HalfW), At(-HalfL, -HalfW), Surface, false, -1.0f, Depth, 0.4f);
	const bool bPlacing = Director->GetPhase() == ERbDirectorPhase::AwaitPlacement &&
		Director->GetConstraints().PlacementRegion == rb::rules::CueBallNext::InHandAboveHeadString;
	DrawDebugLine(World, At(Rules.HeadStringX, -HalfW), At(Rules.HeadStringX, HalfW), bPlacing ? FColor(255, 230, 60) : FColor(120, 160, 200), false,
		-1.0f, Depth, bPlacing ? 0.5f : 0.2f);
	for (const rb::Vec2& Spot : {Rules.HeadSpot, Rules.FootSpot, Rules.CenterSpot})
	{
		DrawDebugCircle(World, At(Spot.x, Spot.y), 0.8, 16, FColor(200, 200, 200), false, -1.0f, Depth, 0.2f, AxisX, AxisY, false);
	}
	// Pocket openings: mouth line between the virtual jaw points and the drop-edge circle.
	for (int32 P = 0; P < Rules.PocketCount; ++P)
	{
		const rb::rules::PocketOpening& Pocket = Rules.Pockets[P];
		DrawDebugLine(World, At(Pocket.JawPoint[0].x, Pocket.JawPoint[0].y), At(Pocket.JawPoint[1].x, Pocket.JawPoint[1].y), FColor(240, 120, 60), false, -1.0f,
			Depth, 0.3f);
		DrawDebugCircle(World, At(Pocket.CaptureCenter.x, Pocket.CaptureCenter.y), 100.0 * Pocket.DropEdgeRadius, 48, FColor(240, 120, 60), false, -1.0f, Depth,
			0.3f, AxisX, AxisY, false);
	}

	// Balls of the authoritative table state (what the next simulation starts from).
	const FRbTableState& State = Director->GetTableState();
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		const rb::SimBall& Ball = State.Balls[Id];
		if (!Ball.InPlay)
		{
			continue;
		}
		const FVector Center = At(Ball.State.Position.x, Ball.State.Position.y, Ball.State.Position.z);
		const double R = 100.0 * Ball.Spec.Radius;
		const FColor Color = BallColor(Id);
		for (int32 Ring = 0; Ring <= 9; ++Ring) // concentric rings fill the disc (0.02 R .. 0.99 R, no hole at the centre)
		{
			DrawDebugCircle(World, Center, R * (0.02 + 0.108 * Ring), 40, Color, false, -1.0f, Depth, 0.35f, AxisX, AxisY, false);
		}
		if (Id >= 9)
		{
			DrawDebugCircle(World, Center, R * 0.98, 40, FColor::White, false, -1.0f, Depth, 0.5f, AxisX, AxisY, false); // stripe ring
		}
		if (Id == 8)
		{
			DrawDebugCircle(World, Center, R * 0.4, 24, FColor::White, false, -1.0f, Depth, 0.3f, AxisX, AxisY, false);
		}
		const bool bLight = Id == 0 || Id == 1 || Id == 5 || Id == 9 || Id == 13;
		DrawDigits(World, Center + FVector(0.0, 0.0, 0.5), Id, R * 0.95, AxisX, AxisY, bLight ? FColor(10, 10, 10) : FColor::White);
	}
#endif
}
