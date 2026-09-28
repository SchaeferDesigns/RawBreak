#include "Game/RbMatchDirector.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Cue/RbCue.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"

#include "rb/Human/BallMarks.h"
#include "rb/Human/Chores.h"
#include "rb/Human/HumanModel.h"
#include "rb/Human/Progression.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/TableRules.h"

// Owner: UE-6b. The match bridge of Docs/ue-architecture.md 5.2 / 7: rack, turns, ball in hand (Assisted input mode, R-11),
// ExecuteStroke with the stroke component's context (R-04), SimInput, submit, rules evaluation on the game thread, commit
// after the playback (R-07 guard; rate 0 commits at once, R-15), the ONE table-state sync (R-12), equipment, noise
// history, auto-chalk, decisions, rack over / match over via Confirm (R-20), optional lag. Tests: RawBreak.Unit.Match.*
// (RbMatchTests.cpp, world-free) and RawBreak.Functional.MatchFlow (RbMatchFlowTest.cpp, PIE with ARbGameMode).

namespace
{
	constexpr int32 kRulesBalls = rb::rules::kRulesBallCount;

	bool IsInHandRegion(rb::rules::CueBallNext Next)
	{
		return Next != rb::rules::CueBallNext::InPosition;
	}

	rb::CueBallInHand ToInHand(rb::rules::CueBallNext Next)
	{
		switch (Next)
		{
		case rb::rules::CueBallNext::InHandAnywhere: return rb::CueBallInHand::Anywhere;
		case rb::rules::CueBallNext::InHandAboveHeadString: return rb::CueBallInHand::AboveHeadString;
		case rb::rules::CueBallNext::InHandBaulk: return rb::CueBallInHand::Baulk;
		case rb::rules::CueBallNext::InPosition: break;
		}
		return rb::CueBallInHand::No;
	}

	uint64 DrawRandomSeed()
	{
		const uint64 Seed = rb::human::HashKeys(FPlatformTime::Cycles64(), static_cast<uint64>(FDateTime::UtcNow().GetTicks()));
		return Seed != 0 ? Seed : 1;
	}

	// A ball at rest on the cloth at plan position P (z = R).
	rb::BallState RestingAt(const rb::Vec2& P, double Radius)
	{
		rb::BallState S;
		S.Position = rb::Vec3(P.x, P.y, Radius);
		S.State = rb::MotionState::Stationary;
		return S;
	}

	bool IsFiniteVec2(const rb::Vec2& P)
	{
		return FMath::IsFinite(P.x) && FMath::IsFinite(P.y);
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
}

// ---------------------------------------------------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------------------------------------------------

void URbMatchDirector::Initialize(ARbTable* InTable, ARbBallSet* InBalls, ARbCue* InCue, URbSimulationSubsystem* InSimulation)
{
	Table = InTable;
	Balls = InBalls;
	Cue = InCue;

	if (URbSimulationSubsystem* Old = Simulation.Get())
	{
		Old->OnShotSimulated.Remove(SimulatedHandle);
	}
	SimulatedHandle.Reset();
	Simulation = InSimulation;
	if (InSimulation)
	{
		SimulatedHandle = InSimulation->OnShotSimulated.AddUObject(this, &URbMatchDirector::OnShotSimulated);
	}

	if (URbShotPlaybackComponent* Old = Playback.Get())
	{
		Old->OnFinished.Remove(PlaybackHandle);
	}
	PlaybackHandle.Reset();
	Playback = InBalls ? InBalls->GetPlayback() : nullptr;
	if (URbShotPlaybackComponent* NewPlayback = Playback.Get())
	{
		PlaybackHandle = NewPlayback->OnFinished.AddUObject(this, &URbMatchDirector::OnPlaybackFinished);
	}
}

void URbMatchDirector::SetStrokeComponent(URbStrokeComponent* InStroke)
{
	if (URbStrokeComponent* Old = StrokeComponent.Get())
	{
		if (Old == InStroke)
		{
			return;
		}
		Old->OnStrokeContact.Remove(ContactHandle);
		Old->OnCueBallPlaced.Remove(PlacedHandle);
		Old->OnStrokeAborted.Remove(AbortedHandle);
	}
	ContactHandle.Reset();
	PlacedHandle.Reset();
	AbortedHandle.Reset();
	StrokeComponent = InStroke;
	if (!InStroke)
	{
		return;
	}
	ContactHandle = InStroke->OnStrokeContact.AddUObject(this, &URbMatchDirector::HandleStrokeContact);
	PlacedHandle = InStroke->OnCueBallPlaced.AddUObject(this, &URbMatchDirector::HandleCueBallPlaced);
	AbortedHandle = InStroke->OnStrokeAborted.AddUObject(this, &URbMatchDirector::HandleStrokeAborted);
	ArmStrokeForPhase(true);
}

void URbMatchDirector::Shutdown()
{
	SetStrokeComponent(nullptr);
	if (URbSimulationSubsystem* Sim = Simulation.Get())
	{
		Sim->OnShotSimulated.Remove(SimulatedHandle);
	}
	if (URbShotPlaybackComponent* Pb = Playback.Get())
	{
		Pb->OnFinished.Remove(PlaybackHandle);
	}
	SimulatedHandle.Reset();
	PlaybackHandle.Reset();
	bAwaitingSimulation = false;
	PendingShot.Reset();
}

UWorld* URbMatchDirector::GetWorld() const
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}
	if (const ARbTable* T = Table.Get())
	{
		return T->GetWorld();
	}
	if (const URbSimulationSubsystem* Sim = Simulation.Get())
	{
		return Sim->GetWorld();
	}
	const UObject* Outer = GetOuter();
	return Outer ? Outer->GetWorld() : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Match start, rack, turns
// ---------------------------------------------------------------------------------------------------------------------

bool URbMatchDirector::StartMatch(const FRbMatchSetup& InSetup)
{
	LastError.Reset();
	// A match restarted mid-shot drops the shot in flight (its hand-off / playback end is ignored). The shot is forgotten
	// BEFORE the playback stops, so nothing the stop might broadcast can commit it into the new match.
	const bool bWasPlayingBack = Phase == ERbDirectorPhase::PlayingBack;
	bAwaitingSimulation = false;
	SubmittedShotId = 0;
	PendingShot.Reset();
	Phase = ERbDirectorPhase::Idle; // no stale Lag / placement state while the new match is set up (broadcast by EnterRulesPhase)
	if (bWasPlayingBack)
	{
		if (URbShotPlaybackComponent* Pb = Playback.Get())
		{
			Pb->Stop(false);
		}
	}

	TableContext = ContextOverride.IsValid() ? ContextOverride : nullptr;
	if (!TableContext.IsValid())
	{
		const ARbTable* T = Table.Get();
		TableContext = (T && T->HasContext()) ? T->GetContextPtr() : nullptr;
	}
	if (!TableContext.IsValid())
	{
		Refuse(TEXT("StartMatch: no table context"));
		SetPhase(ERbDirectorPhase::Idle);
		return false;
	}

	Setup = InSetup;
	bRandomSeed = InSetup.Seed == 0;
	MatchSeed = bRandomSeed ? DrawRandomSeed() : static_cast<uint64>(InSetup.Seed);
	Setup.Seed = static_cast<int64>(MatchSeed); // stored for replays

	HumanParams = rb::human::HumanParams{};
	HumanParams.NoiseScale = FMath::Max(0.0, Setup.NoiseScale);

	const bool bPractice = Setup.Mode == ERbMatchMode::Practice;
	const int32 Race = FMath::Max(1, Setup.RaceTo);
	Config = rb::rules::MatchConfig{};
	Config.Game = RbTypes::ToCore(Setup.Discipline);
	Config.Rules = rb::rules::MakeRulesConfig(RbTypes::RulesPresetFor(Setup.Discipline));
	Config.Rules.Input = rb::rules::InputMode::Assisted; // M1: no body / bridge hand (rules.md 16 item 22, review R-11)
	// Practice and hot-seat are casual play: calls use the casual default ObviousAssist (rules.md 4.5) instead of the ranked
	// Explicit of the WPA presets (8-ball, 10-ball, 14.1). M1 has no call input, and Explicit would refuse every shot after the
	// break that carries no call (CallRequired); explicit calls made through SetCalledShot are still honoured.
	if (Config.Rules.Calls == rb::rules::CallMode::Explicit)
	{
		Config.Rules.Calls = rb::rules::CallMode::ObviousAssist;
	}
	Config.RaceTo = bPractice ? TNumericLimits<int32>::Max() : Race; // practice: a won rack racks again
	if (Config.Game == rb::rules::Discipline::StraightPool)
	{
		Config.TargetPoints = bPractice ? TNumericLimits<int32>::Max() : Race; // ?Race= counts points in 14.1
		Config.Rules.TargetPoints = Config.TargetPoints;
	}
	Config.Seed = MatchSeed;
	Config.RackGaps = rb::kRackGapWoodenRack;
	Config.Table = TableContext->RulesTable;

	State = rb::rules::MatchState{};
	rb::rules::StartMatch(Config, State);
	Declaration = rb::rules::ShotDeclaration{};
	MatchShotIndex = 0;
	LastShot = FRbLastShotSummary{};
	LastCommittedShot.Reset();
	PendingSummary = FRbLastShotSummary{};
	bCueBallPlaced = false;
	PlacedCueBall = rb::Vec2{};
	SelectedOption = 0;
	LagStroker = 0;
	bPendingIsLag = false;

	TableState = FRbTableState{};
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		TableState.Balls[Id].Spec = Id < TableContext->Balls.Count ? TableContext->Balls.Balls[Id] : rb::BallSpec{};
	}
	InitShooters();
	if (ARbCue* CueActor = Cue.Get())
	{
		CueActor->SetDrive(ERbCueDrive::Hidden);
	}

	UE_LOG(LogRawBreak, Log, TEXT("RbMatchDirector: %s %s, race %d, lag %d, seed %llu, attribute %.0f, noise %.2f"),
		bPractice ? TEXT("practice") : TEXT("hot-seat"), DisciplineName(Setup.Discipline), Race, Setup.bLag ? 1 : 0, MatchSeed,
		Shooters[0].Attributes.Steadiness, HumanParams.NoiseScale);

	EnterRulesPhase();
	return Phase != ERbDirectorPhase::Idle;
}

void URbMatchDirector::InitShooters()
{
	const rb::human::ProductConfig Product;
	const rb::human::ShooterAttributes Attributes = Setup.ShooterAttribute >= 0.0
		? rb::human::UniformAttributes(FMath::Clamp(Setup.ShooterAttribute, 0.0, 100.0))
		: rb::human::HotSeatGuestAttributes(Product);
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		FRbShooterState& S = Shooters[Slot];
		S = FRbShooterState{};
		S.Name = Slot == 0 ? Setup.Player1 : Setup.Player2;
		S.Attributes = Attributes;
		S.ShooterId = static_cast<uint32>(Slot);
		// Canonical empty history of the match stream (Purpose 0: ShooterKey == ShooterId).
		S.History = rb::human::RebuildNoiseHistory(MatchSeed, static_cast<uint64>(S.ShooterId), 0);
	}
}

void URbMatchDirector::EnterRulesPhase()
{
	switch (State.Phase)
	{
	case rb::rules::MatchPhase::Setup:
		rb::rules::StartMatch(Config, State); // -> Lag
		EnterRulesPhase();
		return;
	case rb::rules::MatchPhase::Lag:
		if (Setup.bLag && Setup.Mode == ERbMatchMode::HotSeat)
		{
			LagStroker = 0;
			SetupLagTable();
			ShowTableState();
			SetPhase(ERbDirectorPhase::Lag);
			ArmStrokeForPhase(true);
			return;
		}
		{
			// No lag (practice, or hot-seat without ?Lag=1): player 0 breaks the first rack; after a 14.1 stalemate the rack's
			// breaker breaks again.
			const int32 Breaker = State.RackNumber > 0 ? FMath::Clamp(State.Game.RackBreaker, 0, 1) : 0;
			rb::rules::LagResult Skip;
			Skip.Outcome = Breaker == 0 ? rb::rules::LagOutcome::FirstWins : rb::rules::LagOutcome::SecondWins;
			rb::rules::ApplyLagResult(Config, State, Skip);
			rb::rules::ChooseBreaker(Config, State, Breaker);
		}
		RackNext();
		return;
	case rb::rules::MatchPhase::LagWinnerChooses:
		// M1: the lag winner breaks (no choice UI).
		rb::rules::ChooseBreaker(Config, State, FMath::Clamp(State.LagWinner, 0, 1));
		RackNext();
		return;
	case rb::rules::MatchPhase::RackSetup:
		RackNext();
		return;
	case rb::rules::MatchPhase::AwaitShot:
		BeginTurn();
		return;
	case rb::rules::MatchPhase::AwaitDecision:
		SelectedOption = 0;
		SetPhase(ERbDirectorPhase::AwaitDecision);
		ArmStrokeForPhase(false);
		return;
	case rb::rules::MatchPhase::RackOver:
		SetPhase(ERbDirectorPhase::RackOver);
		ArmStrokeForPhase(false);
		return;
	case rb::rules::MatchPhase::MatchOver:
		SetPhase(ERbDirectorPhase::MatchOver);
		ArmStrokeForPhase(false);
		return;
	}
}

void URbMatchDirector::RackNext()
{
	rb::rules::RackAssignment Rack;
	const rb::ErrorCode Error = rb::rules::SetupRack(Config, State, Rack);
	if (!rb::Succeeded(Error))
	{
		Refuse(FString::Printf(TEXT("SetupRack failed: %hs"), rb::ToString(Error)));
		SetPhase(ERbDirectorPhase::Idle);
		return;
	}
	SyncTableState(nullptr, nullptr);
	ShowTableState();
	BeginTurn();
}

void URbMatchDirector::BeginTurn()
{
	if (State.Phase != rb::rules::MatchPhase::AwaitShot)
	{
		EnterRulesPhase();
		return;
	}
	Declaration = NeutralDeclaration();
	bCueBallPlaced = false;
	PlacedCueBall = rb::Vec2{};
	SelectedOption = 0;
	AutoChalk(ActiveShooterState());
	const bool bInHand = IsCueBallInHand();
	if (bInHand)
	{
		TableState.Balls[rb::kCueBallId].InPlay = false; // out of play until placed (R-12)
		TableState.Balls[rb::kCueBallId].State = rb::BallState{};
	}
	ShowTableState();
	SetPhase(bInHand ? ERbDirectorPhase::AwaitPlacement : ERbDirectorPhase::AwaitStroke);
	ArmStrokeForPhase(true);
}

// ---------------------------------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------------------------------

rb::rules::ShotConstraints URbMatchDirector::GetConstraints() const
{
	return rb::rules::GetShotConstraints(Config, State);
}

int32 URbMatchDirector::GetActivePlayer() const
{
	return Phase == ERbDirectorPhase::Lag ? FMath::Clamp(LagStroker, 0, 1) : State.Game.Shooter;
}

int32 URbMatchDirector::ShooterSlot(int32 Player) const
{
	return Setup.Mode == ERbMatchMode::Practice ? 0 : FMath::Clamp(Player, 0, 1);
}

int32 URbMatchDirector::ActiveRulesPlayer() const
{
	return GetActivePlayer();
}

FRbShooterState& URbMatchDirector::ActiveShooterState()
{
	return Shooters[ShooterSlot(ActiveRulesPlayer())];
}

const FRbShooterState& URbMatchDirector::ActiveShooterState() const
{
	return Shooters[ShooterSlot(ActiveRulesPlayer())];
}

bool URbMatchDirector::IsCueBallInHand() const
{
	if (State.Phase != rb::rules::MatchPhase::AwaitShot) // (the lag is rules phase Lag)
	{
		return false;
	}
	const rb::rules::GameState& G = State.Game;
	return IsInHandRegion(G.CueBall) || G.Balls[rb::kCueBallId].Kind != rb::rules::BallStatusKind::OnTable;
}

rb::rules::CueBallNext URbMatchDirector::PlacementRegion() const
{
	const rb::rules::GameState& G = State.Game;
	if (G.FreeShot)
	{
		return rb::rules::CueBallNext::InHandBaulk;
	}
	return IsInHandRegion(G.CueBall) ? G.CueBall : rb::rules::CueBallNext::InHandAnywhere;
}

rb::rules::ShotDeclaration URbMatchDirector::NeutralDeclaration() const
{
	rb::rules::ShotDeclaration D;
	D.Kind = State.Game.IsBreakShot ? rb::rules::ShotKind::Break : rb::rules::ShotKind::Normal;
	return D;
}

bool URbMatchDirector::CanShootNow() const
{
	if (bReplayActive || !TableContext.IsValid())
	{
		return false;
	}
	if (Phase == ERbDirectorPhase::Lag)
	{
		return LagStroker >= 0 && LagStroker < 2;
	}
	return Phase == ERbDirectorPhase::AwaitStroke && (!IsCueBallInHand() || bCueBallPlaced) &&
		TableState.Balls[rb::kCueBallId].InPlay;
}

bool URbMatchDirector::IsReplayAllowed() const
{
	switch (Phase)
	{
	case ERbDirectorPhase::AwaitStroke:
	case ERbDirectorPhase::AwaitPlacement:
	case ERbDirectorPhase::AwaitDecision:
	case ERbDirectorPhase::RackOver:
	case ERbDirectorPhase::MatchOver:
		return true;
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Player model inputs
// ---------------------------------------------------------------------------------------------------------------------

rb::human::NoiseKey URbMatchDirector::MakeNoiseKey(const FRbShooterState& Shooter) const
{
	rb::human::NoiseKey Key;
	Key.MatchSeed = MatchSeed;
	Key.RackIndex = State.RackNumber > 0 ? static_cast<uint32>(State.RackNumber - 1) : 0u;
	Key.ShotIndex = MatchShotIndex;
	Key.ShooterId = Shooter.ShooterId;
	Key.ShooterShotIndex = Shooter.ShooterShotIndex;
	Key.CuePickupIndex = Shooter.CuePickupIndex;
	Key.Purpose = 0;
	Key.AddressIndex = 0; // the stroke component counts the get-downs of the shot
	return Key;
}

rb::human::StrokeSituation URbMatchDirector::MakeSituation() const
{
	rb::human::StrokeSituation Situation; // closed bridge 0.20 m, grip 0.80 m (no body in M1)
	const rb::rules::GameState& G = State.Game;
	const bool bHotSeat = Setup.Mode == ERbMatchMode::HotSeat;

	rb::human::PressureInputs Inputs;
	Inputs.Stakes = bHotSeat ? rb::human::kStakesFriendly : rb::human::kStakesPractice;
	if (Phase != ERbDirectorPhase::Lag && State.Phase == rb::rules::MatchPhase::AwaitShot)
	{
		const int32 Lowest = rb::rules::LowestObjectBallAtStart(G);
		switch (Config.Game)
		{
		case rb::rules::Discipline::NineBall: Inputs.GameBall = Lowest == 9; break;
		case rb::rules::Discipline::TenBall: Inputs.GameBall = Lowest == 10; break;
		case rb::rules::Discipline::EightBall:
		case rb::rules::Discipline::Blackball:
			Inputs.GameBall = !G.TableOpen && G.Shooter >= 0 && G.Shooter <= 1 && rb::rules::GroupCleared(G, G.Players[G.Shooter].Group);
			break;
		case rb::rules::Discipline::StraightPool: break;
		}
	}
	// Hill (HF 3.4): either player needs one rack - in a race to 1 both do from the first rack on. 14.1 counts points, not
	// racks (RackWins stay 0), and practice has no race.
	Inputs.Hill = bHotSeat && Config.Game != rb::rules::Discipline::StraightPool &&
		(State.RackWins[0] >= Config.RaceTo - 1 || State.RackWins[1] >= Config.RaceTo - 1);
	Situation.Pressure = rb::human::ComputePressure(Inputs, Setup.bPressure ? rb::human::PressureMode::On : rb::human::PressureMode::Off);
	Situation.Fatigue = 0.0;      // HF-16: off in practice and hot-seat
	Situation.Intoxication = 0.0; // V1: cosmetic only
	return Situation;
}

FRbStrokeContext URbMatchDirector::MakeStrokeContext() const
{
	FRbStrokeContext Context;
	const FRbShooterState& S = ActiveShooterState();
	Context.Attributes = S.Attributes;
	Context.Situation = MakeSituation();
	Context.Tip = S.Tip;
	Context.CueBody = S.CueBody;
	Context.Cue = S.Cue;
	Context.CueBall = TableState.Balls[StruckBallId()].Spec;
	Context.Key = MakeNoiseKey(S);
	Context.History = S.History;
	Context.Params = HumanParams;
	return Context;
}

rb::Vec3 URbMatchDirector::StruckBallPosition() const
{
	return TableState.Balls[StruckBallId()].State.Position;
}

void URbMatchDirector::SpendRevealedDraws(FRbShooterState& Shooter)
{
	// The history is a cache of RebuildNoiseHistory: keep it in step with the index before advancing both.
	if (Shooter.History.MatchSeed != MatchSeed || Shooter.History.NextIndex != Shooter.ShooterShotIndex)
	{
		Shooter.History = rb::human::RebuildNoiseHistory(MatchSeed, static_cast<uint64>(Shooter.ShooterId), Shooter.ShooterShotIndex);
	}
	rb::human::AdvanceNoiseHistory(Shooter.History);
	++Shooter.ShooterShotIndex;
}

void URbMatchDirector::SpendRejectedStroke(int32 Player, bool bHuman)
{
	if (!bHuman)
	{
		return; // scripted strikes reveal no draws
	}
	// The stroke reached the ball, so the component showed the whole per-shot ramp: those draws are spent (HF-B13), exactly as
	// for an aborted stroke with the ramp, and the retry gets the next ones.
	SpendRevealedDraws(Shooters[ShooterSlot(Player)]);
	if (URbStrokeComponent* Stroke = StrokeComponent.Get())
	{
		Stroke->SetStrokeContext(MakeStrokeContext());
	}
}

void URbMatchDirector::AutoChalk(FRbShooterState& Shooter)
{
	double Duration = 0.0;
	Shooter.LastChalkTwists = rb::human::PerformChalking(Shooter.Tip, Shooter.Cube, rb::human::ChoreMode::Automatic, Shooter.Habits.ChalkSweep, 0.0, -1,
		Duration);
}

void URbMatchDirector::OnStrokeAborted(bool bRampShown)
{
	if (Phase != ERbDirectorPhase::AwaitStroke && Phase != ERbDirectorPhase::Lag)
	{
		return;
	}
	if (bRampShown)
	{
		SpendRevealedDraws(ActiveShooterState()); // HF-B13: the draws the ramp showed are spent
	}
	if (URbStrokeComponent* Stroke = StrokeComponent.Get())
	{
		Stroke->SetStrokeContext(MakeStrokeContext());
	}
	OnMatchChanged.Broadcast();
}

// ---------------------------------------------------------------------------------------------------------------------
// Ball in hand and declarations
// ---------------------------------------------------------------------------------------------------------------------

bool URbMatchDirector::CanPlaceCueBall(const rb::Vec2& Position) const
{
	if (bReplayActive || !TableContext.IsValid() || !IsFiniteVec2(Position))
	{
		return false;
	}
	const bool bPlacing = Phase == ERbDirectorPhase::AwaitPlacement || (Phase == ERbDirectorPhase::AwaitStroke && IsCueBallInHand());
	if (!bPlacing)
	{
		return false;
	}
	// Assisted input mode: an illegal placement is refused here, never evaluated as foul 3.10 (R-11).
	if (!rb::rules::CueBallPlacementLegal(State.Game, Position, PlacementRegion(), Config.Table, Config.Rules.Tolerances))
	{
		return false;
	}
	return rb::rules::ValidateDeclaration(Config, State, NeutralDeclaration(), &Position) == rb::ErrorCode::Ok;
}

bool URbMatchDirector::PlaceCueBall(const rb::Vec2& Position)
{
	if (!CanPlaceCueBall(Position))
	{
		Refuse(FString::Printf(TEXT("cue ball placement (%.4f, %.4f) refused in phase %d"), Position.x, Position.y, static_cast<int32>(Phase)));
		return false;
	}
	PlacedCueBall = Position;
	bCueBallPlaced = true;
	rb::SimBall& CueBall = TableState.Balls[rb::kCueBallId];
	CueBall.InPlay = true;
	CueBall.State = RestingAt(Position, CueBall.Spec.Radius);
	ShowTableState();
	SetPhase(ERbDirectorPhase::AwaitStroke);
	ArmStrokeForPhase(true);
	return true;
}

bool URbMatchDirector::CanDeclareNow() const
{
	return !bReplayActive && TableContext.IsValid() && State.Phase == rb::rules::MatchPhase::AwaitShot &&
		(Phase == ERbDirectorPhase::AwaitStroke || Phase == ERbDirectorPhase::AwaitPlacement);
}

bool URbMatchDirector::IsDeclarationAllowed(const rb::rules::ShotDeclaration& D) const
{
	// ValidateDeclaration checks the kind, the call and the claim, and - for a cue ball in hand - the placement. The placement
	// is PlaceCueBall's job (and is checked again at the stroke), so the probe plays the cue ball from position.
	rb::rules::MatchState Probe = State;
	Probe.Game.CueBall = rb::rules::CueBallNext::InPosition;
	Probe.Game.FreeShot = false;
	rb::rules::ShotDeclaration Completed = D;
	rb::rules::CompleteDeclaration(Config, Probe, Completed);
	return rb::rules::ValidateDeclaration(Config, Probe, Completed, nullptr) == rb::ErrorCode::Ok;
}

void URbMatchDirector::SetCalledShot(int32 Ball, int32 Pocket)
{
	if (!CanDeclareNow())
	{
		Refuse(FString::Printf(TEXT("call refused in phase %d"), static_cast<int32>(Phase)));
		return;
	}
	// Negative = no call; anything else must be a ball / pocket id (never wrapped by the narrowing casts).
	if (Ball >= rb::rules::kRulesBallCount || Pocket >= rb::kPocketCount)
	{
		Refuse(FString::Printf(TEXT("call of ball %d in pocket %d refused: no such ball / pocket"), Ball, Pocket));
		return;
	}
	rb::rules::ShotDeclaration D = Declaration;
	D.Called.Ball = Ball < 0 ? rb::kNoBall : static_cast<rb::BallId>(Ball);
	D.Called.Pocket = Pocket < 0 ? rb::PocketId::None : static_cast<rb::PocketId>(Pocket);
	if (!IsDeclarationAllowed(D))
	{
		Refuse(FString::Printf(TEXT("call of ball %d in pocket %d refused by the rules"), Ball, Pocket));
		return;
	}
	Declaration = D;
	OnMatchChanged.Broadcast();
}

void URbMatchDirector::SetShotKind(rb::rules::ShotKind Kind)
{
	if (!CanDeclareNow())
	{
		Refuse(FString::Printf(TEXT("shot kind refused in phase %d"), static_cast<int32>(Phase)));
		return;
	}
	rb::rules::ShotDeclaration D = Declaration;
	D.Kind = Kind;
	if (!IsDeclarationAllowed(D))
	{
		Refuse(FString::Printf(TEXT("shot kind %d refused by the rules (push-out / safety not allowed now)"), static_cast<int32>(Kind)));
		return;
	}
	Declaration = D;
	OnMatchChanged.Broadcast();
}

// ---------------------------------------------------------------------------------------------------------------------
// Strokes -> simulation
// ---------------------------------------------------------------------------------------------------------------------

bool URbMatchDirector::SubmitStroke(const FRbStrokeCommit& Commit)
{
	if (!CanShootNow())
	{
		Refuse(FString::Printf(TEXT("stroke refused in phase %d"), static_cast<int32>(Phase)));
		return false;
	}
	// The SAME context the stroke component rendered with (R-04): director state + its address count + its elevation floor.
	FRbStrokeContext Context = MakeStrokeContext();
	Context.Key.AddressIndex = Commit.AddressIndex;
	if (const URbStrokeComponent* Stroke = StrokeComponent.Get())
	{
		Context.Situation.ElevationFloor = Stroke->GetAim().ElevationFloor;
	}

	const int32 Struck = StruckBallId();
	rb::human::BallObstacle Obstacles[rb::kMaxBalls];
	int32 ObstacleCount = 0;
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		const rb::SimBall& B = TableState.Balls[Id];
		if (Id != Struck && B.InPlay)
		{
			rb::human::BallObstacle& O = Obstacles[ObstacleCount++];
			O.Id = static_cast<rb::BallId>(Id);
			O.Position = B.State.Position;
			O.Radius = B.Spec.Radius;
		}
	}

	const rb::human::ExecutedStroke Executed = rb::human::ExecuteStroke(Commit.Intended, Context.Attributes, Context.Situation, Context.Tip,
		Context.CueBody, Context.Cue, Context.CueBall, TableState.Balls[Struck].State.Position, Obstacles, ObstacleCount, Context.Key, Context.History,
		Context.Params);
	if (Executed.Error != rb::ErrorCode::Ok)
	{
		Refuse(FString::Printf(TEXT("ExecuteStroke failed: %hs"), rb::ToString(Executed.Error)));
		SpendRejectedStroke(GetActivePlayer(), true);
		return false;
	}

	FRbStrokeRecord Record;
	Record.bHuman = true;
	Record.Intended = Commit.Intended;
	Record.Executed = Executed;
	Record.Key = Context.Key;
	Record.InputLog = Commit.InputLog;
	const double ContactTime = Commit.ContactTime > 0.0 ? Commit.ContactTime : FPlatformTime::Seconds();
	return SubmitStrike(Executed.Strike, MoveTemp(Record), ContactTime, Commit.EyeTransform);
}

bool URbMatchDirector::SubmitScriptedStrike(double Speed, double Azimuth, double Elevation, double OffsetA, double OffsetB)
{
	if (!CanShootNow())
	{
		Refuse(FString::Printf(TEXT("scripted strike refused in phase %d"), static_cast<int32>(Phase)));
		return false;
	}
	rb::CueStrikeInput Strike;
	Strike.Speed = Speed;
	Strike.Azimuth = Azimuth;
	Strike.Elevation = Elevation;
	Strike.OffsetA = OffsetA;
	Strike.OffsetB = OffsetB;
	Strike.Cue = ActiveShooterState().Cue;
	const rb::ErrorCode Valid = rb::ValidateCueStrike(Strike);
	if (!rb::Succeeded(Valid))
	{
		Refuse(FString::Printf(TEXT("scripted strike invalid: %hs"), rb::ToString(Valid)));
		return false;
	}
	FTransform View = FTransform::Identity;
	if (const URbStrokeComponent* Stroke = StrokeComponent.Get())
	{
		if (const APawn* Pawn = Cast<APawn>(Stroke->GetOwner()))
		{
			FVector Location;
			FRotator Rotation;
			Pawn->GetActorEyesViewPoint(Location, Rotation);
			View = FTransform(Rotation, Location);
		}
	}
	return SubmitStrike(Strike, FRbStrokeRecord{}, FPlatformTime::Seconds(), View);
}

void URbMatchDirector::BuildShotInput(FRbShotRequest& Request) const
{
	Request.Table = TableContext;
	RbShot::InitSimInput(*TableContext, Request.Input);
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		Request.Input.Balls[Id] = TableState.Balls[Id];
	}
	Request.Input.Context.FrozenTolerance = Config.Rules.Tolerances.Frozen;
	// Assisted input mode: no body / bridge-hand contacts exist, NonTipContacts stay empty (R-11).
	Request.MatchShotIndex = MatchShotIndex;
}

bool URbMatchDirector::SubmitStrike(const rb::CueStrikeInput& Strike, FRbStrokeRecord&& Record, double ContactTime, const FTransform& ShooterView)
{
	if (Phase == ERbDirectorPhase::Lag)
	{
		const int32 Player = FMath::Clamp(LagStroker, 0, 1);
		LagStrikes[Player] = Strike;
		LagRecords[Player] = MoveTemp(Record);
		++LagStroker;
		if (LagStroker < 2)
		{
			ArmStrokeForPhase(true); // the second player lags
			OnMatchChanged.Broadcast();
			return true;
		}
		// Both lag strokes known: ONE simulation with both strikes at t = 0 (rules.md 4.1, architecture.md 13 item 4).
		FRbShotRequest Request;
		BuildShotInput(Request);
		for (int32 P = 0; P < 2; ++P)
		{
			rb::StrikeRequest S;
			S.Ball = static_cast<rb::BallId>(LagBall(P));
			S.Input = LagStrikes[P];
			Request.Input.Strikes.PushBack(S);
		}
		Request.Stroke = LagRecords[0];
		Request.Shooter = 0;
		Request.ContactTime = ContactTime;
		Request.ShooterView = ShooterView;
		bPendingIsLag = true;
		return SubmitRequest(MoveTemp(Request));
	}

	rb::rules::ShotDeclaration D = Declaration;
	rb::rules::CompleteDeclaration(Config, State, D);
	const bool bInHand = IsCueBallInHand();
	const rb::ErrorCode Valid = rb::rules::ValidateDeclaration(Config, State, D, bInHand ? &PlacedCueBall : nullptr);
	if (!rb::Succeeded(Valid))
	{
		Refuse(FString::Printf(TEXT("declaration refused: %hs"), rb::ToString(Valid)));
		SpendRejectedStroke(State.Game.Shooter, Record.bHuman);
		return false;
	}
	PendingDeclaration = D;

	FRbShotRequest Request;
	BuildShotInput(Request);
	rb::StrikeRequest S;
	S.Ball = static_cast<rb::BallId>(rb::kCueBallId);
	S.Input = Strike;
	Request.Input.Strikes.PushBack(S);
	if (bInHand)
	{
		Request.Input.Context.InHand = ToInHand(PlacementRegion());
		Request.Input.Context.PlacedPosition = PlacedCueBall;
	}
	Request.Stroke = MoveTemp(Record);
	Request.Shooter = State.Game.Shooter;
	Request.ContactTime = ContactTime;
	Request.ShooterView = ShooterView;
	bPendingIsLag = false;
	return SubmitRequest(MoveTemp(Request));
}

bool URbMatchDirector::SubmitRequest(FRbShotRequest&& Request)
{
	const ERbDirectorPhase Before = Phase;
	// The stroke that triggered this submission (lag: the second lagger's; the first one's strike stays valid).
	const bool bLagRequest = bPendingIsLag;
	const int32 Stroker = bLagRequest ? 1 : Request.Shooter;
	const bool bHumanStroke = bLagRequest ? LagRecords[1].bHuman : Request.Stroke.bHuman;
	bAwaitingSimulation = true;
	SubmittedShotId = 0;
	// A component that made the contact is in Contact and enters Watching after its broadcast (6.2: the player may stand up
	// and watch); it cannot stroke again from there, and the director re-arms it at the commit. Any other phase (a scripted
	// strike while the player walks or is down) is locked so no second stroke reaches the director during the shot.
	if (URbStrokeComponent* Stroke = StrokeComponent.Get())
	{
		if (Stroke->GetPhase() != ERbStrokePhase::Contact && Stroke->GetPhase() != ERbStrokePhase::Watching)
		{
			Stroke->SetLocked(true);
		}
	}
	SetPhase(ERbDirectorPhase::Simulating);

	if (URbSimulationSubsystem* Sim = Simulation.Get())
	{
		// The hand-off may arrive inside SubmitShot (synchronous service) or in the subsystem's Tick (worker).
		const uint32 Id = Sim->SubmitShot(MoveTemp(Request));
		if (Id == 0)
		{
			if (bAwaitingSimulation)
			{
				bAwaitingSimulation = false;
				if (bLagRequest)
				{
					LagStroker = 1; // the second lag stroke has to be played again
				}
				SetPhase(Before);
				Refuse(TEXT("the simulation refused the shot (busy or invalid request)"));
				SpendRejectedStroke(Stroker, bHumanStroke);
				ArmStrokeForPhase(true);
			}
			return false;
		}
		if (bAwaitingSimulation)
		{
			SubmittedShotId = Id;
		}
		return true;
	}

	// No simulation service (world-free tests): the same simulation on the game thread.
	TSharedRef<FRbShot> Shot = URbSimulationSubsystem::RunShotBlocking(MoveTemp(Request));
	Shot->Id = ++LocalShotId;
	OnShotSimulated(Shot);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hand-off, rules evaluation, playback, commit
// ---------------------------------------------------------------------------------------------------------------------

void URbMatchDirector::OnShotSimulated(const TSharedRef<const FRbShot>& Shot)
{
	if (!bAwaitingSimulation || Phase != ERbDirectorPhase::Simulating)
	{
		return;
	}
	if ((SubmittedShotId != 0 && Shot->Id != SubmittedShotId) || Shot->Request.MatchShotIndex != MatchShotIndex ||
		Shot->Request.Table != TableContext)
	{
		return; // somebody else's shot
	}
	bAwaitingSimulation = false;
	SubmittedShotId = 0;

	const rb::ShotResult& Result = Shot->Result;
	if (Result.Status == rb::SimStatus::InvalidInput || Result.Status == rb::SimStatus::NotImplemented)
	{
		Refuse(FString::Printf(TEXT("simulation rejected the shot: status %d, input error %hs"), static_cast<int32>(Result.Status),
			rb::ToString(Result.Diagnostics.InputError)));
		// Every stroke of the rejected shot showed its full ramp: spent (HF-B13); the shot is played again with new draws.
		if (bPendingIsLag)
		{
			bPendingIsLag = false;
			LagStroker = 0; // both players lag again
			SetPhase(ERbDirectorPhase::Lag);
			SpendRejectedStroke(0, LagRecords[0].bHuman);
			SpendRejectedStroke(1, LagRecords[1].bHuman);
		}
		else
		{
			SetPhase(ERbDirectorPhase::AwaitStroke);
			SpendRejectedStroke(Shot->Request.Shooter, Shot->Request.Stroke.bHuman);
		}
		ArmStrokeForPhase(true);
		return;
	}
	if (Result.Status != rb::SimStatus::Ok)
	{
		UE_LOG(LogRawBreak, Warning, TEXT("RbMatchDirector: shot %u ended with status %d (record truncated: %d); evaluated as it is"), Shot->Id,
			static_cast<int32>(Result.Status), Result.Record.Truncated ? 1 : 0);
	}

	PendingShot = Shot;
	PendingSummary = FRbLastShotSummary{};
	PendingSummary.bValid = true;
	PendingSummary.SimMilliseconds = Shot->SimMilliseconds;
	PendingSummary.CueSpeed = Shot->Request.Input.Strikes.Size() > 0 ? Shot->Request.Input.Strikes[0].Input.Speed : 0.0;
	PendingSummary.bPredictedMiscue = Shot->Request.Stroke.bHuman && Shot->Request.Stroke.Executed.PredictedMiscue;
	PendingSummary.bMiscue = Result.Strikes.Size() > 0 && Result.Strikes[0].Result.Miscue;

	const rb::RulesTolerances& Tolerances = Config.Rules.Tolerances;
	if (bPendingIsLag)
	{
		const rb::rules::LagBallFacts First = rb::rules::DeriveLagBallFacts(Result.Record, LagBall(0), Config.Table, Tolerances);
		const rb::rules::LagBallFacts Second = rb::rules::DeriveLagBallFacts(Result.Record, LagBall(1), Config.Table, Tolerances);
		PendingLag = rb::rules::EvaluateLag(First, Second, Tolerances);
		PendingSummary.bLag = true;
		PendingSummary.LagWinner = PendingLag.Outcome == rb::rules::LagOutcome::FirstWins ? 0
			: (PendingLag.Outcome == rb::rules::LagOutcome::SecondWins ? 1 : -1);
		PendingSummary.RuleRef = TEXT("R 1.2");
	}
	else
	{
		// Rules immediately, shown only at the commit (the UI never spoils the outcome).
		rb::rules::DeriveShotFacts(Result.Record, Config.Table, Tolerances, rb::kInfinity, PendingFacts);
		PendingOutcome = rb::rules::EvaluateShot(Config.Rules, Config.Table, State.Game, PendingDeclaration, PendingFacts);
		PendingSummary.Shooter = State.Game.Shooter;
		PendingSummary.Fouls = PendingOutcome.Detected;
		PendingSummary.Enforced = PendingOutcome.Enforced;
		PendingSummary.RuleRef = UTF8_TO_TCHAR(PendingOutcome.RuleRef ? PendingOutcome.RuleRef : "");
		PendingSummary.Next = PendingOutcome.Next;
		for (const rb::rules::PocketedBall& P : PendingFacts.Pocketed)
		{
			PendingSummary.Pocketed.Add(P.Ball);
		}
		PendingSummary.FirstContactBall =
			rb::rules::ResolveFirstContact(PendingFacts, rb::rules::LegalFirstContactMask(Config.Rules, State.Game, PendingDeclaration));
	}

	URbShotPlaybackComponent* Pb = Playback.Get();
	if (LivePlaybackRate <= 0.0f || !Pb)
	{
		CommitShot(Shot); // headless: no playback (R-15)
		return;
	}
	SetPhase(ERbDirectorPhase::PlayingBack);
	if (ARbCue* CueActor = Cue.Get())
	{
		CueActor->SetDrive(ERbCueDrive::Playback);
	}
	Pb->Play(Shot, true, 0.0, LivePlaybackRate);
}

void URbMatchDirector::SetLivePlaybackRate(float Rate)
{
	if (!FMath::IsFinite(Rate))
	{
		return;
	}
	LivePlaybackRate = FMath::Max(0.0f, Rate);
	if (Phase != ERbDirectorPhase::PlayingBack || !PendingShot.IsValid())
	{
		return; // applies to the next live shot
	}
	// The live shot that is playing: same rate change for it (the playback re-anchors its clock, no time jump), or at 0 its
	// end state at once and the commit (what a rate-0 shot does right after the simulation).
	URbShotPlaybackComponent* Pb = Playback.Get();
	const bool bOurs = Pb && Pb->GetShot() == PendingShot;
	if (LivePlaybackRate > 0.0f)
	{
		if (bOurs)
		{
			Pb->SetRate(LivePlaybackRate);
		}
		return;
	}
	if (bOurs)
	{
		Pb->Stop(true);
	}
	CommitShot(PendingShot.ToSharedRef());
}

void URbMatchDirector::OnPlaybackFinished(const TSharedRef<const FRbShot>& Shot)
{
	// The playback component also plays replays: only our own pending live shot is committed (review R-07).
	if (Phase != ERbDirectorPhase::PlayingBack || !PendingShot.IsValid() || PendingShot.Get() != &Shot.Get())
	{
		return;
	}
	CommitShot(Shot);
}

void URbMatchDirector::ApplyEquipment(const FRbShot& Shot, rb::BallChalkMarks* Marks)
{
	const FRbStrokeRecord& Stroke = Shot.Request.Stroke;
	if (!Stroke.bHuman || Shot.Request.Input.Strikes.Size() == 0)
	{
		return; // scripted strikes bypass the human layer: no tip wear, no marks, no draws
	}
	FRbShooterState& Shooter = Shooters[ShooterSlot(Shot.Request.Shooter)];
	const int32 Struck = FMath::Clamp(static_cast<int32>(Shot.Request.Input.Strikes[0].Ball), 0, rb::kMaxBalls - 1);
	rb::human::ApplyShotToEquipment(Stroke.Executed, Shot.Result, 0, Shot.Request.Input.Balls[Struck].Orientation, Shooter.Tip, Marks);
	SpendRevealedDraws(Shooter);
}

void URbMatchDirector::CommitShot(const TSharedRef<const FRbShot>& Shot)
{
	if (!PendingShot.IsValid() || PendingShot.Get() != &Shot.Get())
	{
		return;
	}
	const TSharedRef<const FRbShot> Committed = Shot; // keeps the shot alive while PendingShot is reset

	rb::BallChalkMarks Marks[rb::kMaxBalls];
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		Marks[Id] = Committed->Request.Input.Balls[Id].ChalkMarks;
	}

	const bool bLag = bPendingIsLag;
	if (bLag)
	{
		CommitLag(Committed, Marks);
	}
	else
	{
		ApplyEquipment(*Committed, Marks);
		const rb::ErrorCode Applied = rb::rules::ApplyShot(Config, State, PendingOutcome, PendingFacts);
		if (!rb::Succeeded(Applied))
		{
			UE_LOG(LogRawBreak, Error, TEXT("RbMatchDirector: ApplyShot failed (%hs); the rules state is unchanged"), rb::ToString(Applied));
		}
		SyncTableState(&Committed.Get(), Marks);
	}

	++MatchShotIndex;
	LastShot = PendingSummary;
	LastCommittedShot = Committed;
	PendingShot.Reset();
	bPendingIsLag = false;
	Declaration = NeutralDeclaration(); // the declaration belonged to this shot (a decision / rack over follows without BeginTurn)
	if (ARbCue* CueActor = Cue.Get())
	{
		CueActor->SetDrive(ERbCueDrive::Hidden);
	}
	if (UWorld* World = GetWorld())
	{
		if (URbReplaySubsystem* Replay = World->GetSubsystem<URbReplaySubsystem>())
		{
			Replay->RecordShot(Committed);
		}
	}
	ShowTableState();

	if (bLag && State.Phase == rb::rules::MatchPhase::Lag)
	{
		// Re-lag: new strokes (new shot index, new keys).
		LagStroker = 0;
		SetupLagTable();
		ShowTableState();
		SetPhase(ERbDirectorPhase::Lag);
		ArmStrokeForPhase(true);
		return;
	}
	EnterRulesPhase();
}

void URbMatchDirector::CommitLag(const TSharedRef<const FRbShot>& Shot, rb::BallChalkMarks* Marks)
{
	// One equipment update per strike in strike order (the fading runs in the call for the last strike).
	for (int32 P = 0; P < 2; ++P)
	{
		const FRbStrokeRecord& Stroke = LagRecords[P];
		if (!Stroke.bHuman)
		{
			continue;
		}
		FRbShooterState& Shooter = Shooters[ShooterSlot(P)];
		rb::human::ApplyShotToEquipment(Stroke.Executed, Shot->Result, P, Shot->Request.Input.Balls[LagBall(P)].Orientation, Shooter.Tip, Marks);
		SpendRevealedDraws(Shooter);
	}
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		TableState.Balls[Id].ChalkMarks = Marks[Id];
		if (Shot->Result.Finals[Id].Status != rb::BallFinalStatus::NotInPlay)
		{
			TableState.Balls[Id].Orientation = Shot->Result.Finals[Id].Orientation;
		}
	}
	const rb::ErrorCode Applied = rb::rules::ApplyLagResult(Config, State, PendingLag);
	if (!rb::Succeeded(Applied))
	{
		UE_LOG(LogRawBreak, Error, TEXT("RbMatchDirector: ApplyLagResult failed (%hs)"), rb::ToString(Applied));
	}
	UE_LOG(LogRawBreak, Log, TEXT("RbMatchDirector: lag %s (distances %.4f / %.4f m, bad %d / %d)"),
		PendingLag.Outcome == rb::rules::LagOutcome::Relag ? TEXT("re-lag") : (PendingLag.Outcome == rb::rules::LagOutcome::FirstWins ? TEXT("won by player 1") : TEXT("won by player 2")),
		PendingLag.First.Distance, PendingLag.Second.Distance, PendingLag.First.Bad ? 1 : 0, PendingLag.Second.Bad ? 1 : 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Table state (R-12)
// ---------------------------------------------------------------------------------------------------------------------

void URbMatchDirector::SyncTableState(const FRbShot* Shot, const rb::BallChalkMarks* Marks)
{
	if (!TableContext.IsValid())
	{
		return;
	}
	const FRbTableContext& Context = *TableContext;
	const rb::rules::GameState& G = State.Game;
	// The cue ball is out of play while it is off the table or in hand for the next shot (placed later).
	const bool bCueOut = G.Balls[rb::kCueBallId].Kind != rb::rules::BallStatusKind::OnTable ||
		(State.Phase == rb::rules::MatchPhase::AwaitShot && IsInHandRegion(G.CueBall));
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		rb::SimBall& B = TableState.Balls[Id];
		B.Spec = Id < Context.Balls.Count ? Context.Balls.Balls[Id] : rb::BallSpec{};
		if (Marks)
		{
			B.ChalkMarks = Marks[Id];
		}
		if (Shot && Shot->Result.Finals[Id].Status != rb::BallFinalStatus::NotInPlay)
		{
			B.Orientation = Shot->Result.Finals[Id].Orientation;
		}
		bool bOnTable = Id < kRulesBalls && G.Balls[Id].Kind == rb::rules::BallStatusKind::OnTable;
		if (Id == rb::kCueBallId && bCueOut)
		{
			bOnTable = false;
		}
		B.InPlay = bOnTable;
		// Status and plan position from the rules (spotted balls included), z = R, at rest.
		B.State = bOnTable ? RestingAt(G.Balls[Id].Position, B.Spec.Radius) : rb::BallState{};
	}
	bCueBallPlaced = false;
}

void URbMatchDirector::SetupLagTable()
{
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		TableState.Balls[Id].InPlay = false;
		TableState.Balls[Id].State = rb::BallState{};
	}
	rb::Vec2 Positions[2];
	rb::rules::LagStartPositions(Config.Table, Positions[0], Positions[1]);
	for (int32 P = 0; P < 2; ++P)
	{
		rb::SimBall& B = TableState.Balls[LagBall(P)];
		B.InPlay = true;
		B.State = RestingAt(Positions[P], B.Spec.Radius);
	}
}

void URbMatchDirector::SetTableStateForTest(const FRbTableState& NewState)
{
	if (!TableContext.IsValid() || (Phase != ERbDirectorPhase::AwaitStroke && Phase != ERbDirectorPhase::AwaitPlacement))
	{
		Refuse(FString::Printf(TEXT("SetTableStateForTest refused in phase %d"), static_cast<int32>(Phase)));
		return;
	}
	const FRbTableContext& Context = *TableContext;
	rb::rules::GameState& G = State.Game;
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		rb::SimBall& B = TableState.Balls[Id];
		const rb::SimBall& N = NewState.Balls[Id];
		B.Spec = Id < Context.Balls.Count ? Context.Balls.Balls[Id] : rb::BallSpec{};
		B.InPlay = N.InPlay && Id < Context.Balls.Count;
		B.Orientation = N.Orientation;
		B.ChalkMarks = N.ChalkMarks;
		B.State = B.InPlay ? RestingAt(rb::XY(N.State.Position), B.Spec.Radius) : rb::BallState{};
		if (Id < kRulesBalls)
		{
			rb::rules::BallStatus& S = G.Balls[Id];
			if (B.InPlay)
			{
				S.Kind = rb::rules::BallStatusKind::OnTable;
				S.Position = rb::XY(N.State.Position);
			}
			else if (S.Kind == rb::rules::BallStatusKind::OnTable)
			{
				S.Kind = rb::rules::BallStatusKind::Pocketed;
			}
		}
	}
	G.IsBreakShot = false;
	G.PushOutAvailable = false;
	G.FreeShot = false;
	G.CueBall = G.Balls[rb::kCueBallId].Kind == rb::rules::BallStatusKind::OnTable ? rb::rules::CueBallNext::InPosition
		: rb::rules::CueBallNext::InHandAnywhere;
	BeginTurn();
}

// ---------------------------------------------------------------------------------------------------------------------
// Decisions, rack over, match over
// ---------------------------------------------------------------------------------------------------------------------

bool URbMatchDirector::ChooseOption(rb::rules::Option Choice)
{
	if (Phase != ERbDirectorPhase::AwaitDecision || bReplayActive)
	{
		Refuse(FString::Printf(TEXT("option refused in phase %d"), static_cast<int32>(Phase)));
		return false;
	}
	const rb::ErrorCode Applied = rb::rules::ApplyOption(Config, State, Choice);
	if (!rb::Succeeded(Applied))
	{
		Refuse(FString::Printf(TEXT("option %d refused: %hs"), static_cast<int32>(Choice), rb::ToString(Applied)));
		return false;
	}
	SelectedOption = 0;
	if (State.Phase == rb::rules::MatchPhase::AwaitShot)
	{
		SyncTableState(nullptr, nullptr); // spotted 8 / cue ball now in hand
		ShowTableState();
	}
	EnterRulesPhase();
	return true;
}

bool URbMatchDirector::CycleOption(int32 Direction)
{
	const int32 Count = State.PendingOutcome.Options.Size();
	if (Phase != ERbDirectorPhase::AwaitDecision || Count <= 0)
	{
		return false;
	}
	SelectedOption = ((SelectedOption + Direction) % Count + Count) % Count;
	OnMatchChanged.Broadcast();
	return true;
}

bool URbMatchDirector::Confirm()
{
	if (bReplayActive)
	{
		return false;
	}
	switch (Phase)
	{
	case ERbDirectorPhase::AwaitDecision:
	{
		const int32 Count = State.PendingOutcome.Options.Size();
		if (Count <= 0)
		{
			return false;
		}
		return ChooseOption(State.PendingOutcome.Options[FMath::Clamp(SelectedOption, 0, Count - 1)]);
	}
	case ERbDirectorPhase::RackOver:
		RackNext();
		return true;
	case ERbDirectorPhase::MatchOver:
	{
		FRbMatchSetup Next = Setup;
		if (bRandomSeed)
		{
			Next.Seed = 0; // a new random match
		}
		return StartMatch(Next);
	}
	default:
		return false;
	}
}

bool URbMatchDirector::RequestRerack()
{
	if (bReplayActive)
	{
		return false;
	}
	if (Phase == ERbDirectorPhase::RackOver)
	{
		RackNext();
		return true;
	}
	if (Phase != ERbDirectorPhase::AwaitStroke && Phase != ERbDirectorPhase::AwaitPlacement && Phase != ERbDirectorPhase::AwaitDecision)
	{
		Refuse(FString::Printf(TEXT("re-rack refused in phase %d"), static_cast<int32>(Phase)));
		return false;
	}
	const rb::ErrorCode Declared = rb::rules::DeclareStalemate(Config, State);
	if (!rb::Succeeded(Declared))
	{
		Refuse(FString::Printf(TEXT("re-rack refused: %hs"), rb::ToString(Declared)));
		return false;
	}
	EnterRulesPhase();
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Presentation and the stroke component
// ---------------------------------------------------------------------------------------------------------------------

void URbMatchDirector::ShowTableState()
{
	if (ARbBallSet* BallSet = Balls.Get())
	{
		BallSet->ShowSimBalls(TableState.Balls, rb::kMaxBalls);
	}
}

void URbMatchDirector::LockStroke()
{
	if (URbStrokeComponent* Stroke = StrokeComponent.Get())
	{
		Stroke->SetLocked(true);
	}
}

void URbMatchDirector::ArmStrokeForPhase(bool bNewTurn)
{
	URbStrokeComponent* Stroke = StrokeComponent.Get();
	if (!Stroke)
	{
		return;
	}
	if (bReplayActive || !TableContext.IsValid())
	{
		Stroke->SetLocked(true);
		return;
	}
	switch (Phase)
	{
	case ERbDirectorPhase::AwaitPlacement:
		Stroke->SetStrokeContext(MakeStrokeContext());
		Stroke->BeginCueBallPlacement();
		break;
	case ERbDirectorPhase::AwaitStroke:
	case ERbDirectorPhase::Lag:
	{
		const int32 Struck = StruckBallId();
		Stroke->SetStrokeContext(MakeStrokeContext());
		if (bNewTurn)
		{
			Stroke->BeginAddress(TableState.Balls[Struck].State.Position, TableState.Balls[Struck].Spec.Radius);
		}
		else
		{
			Stroke->SetLocked(false);
		}
		break;
	}
	default:
		Stroke->SetLocked(true);
		break;
	}
}

void URbMatchDirector::SetReplayActive(bool bActive)
{
	if (bReplayActive == bActive)
	{
		return;
	}
	bReplayActive = bActive;
	if (bActive)
	{
		LockStroke();
	}
	else
	{
		ShowTableState();
		ArmStrokeForPhase(false);
	}
	OnMatchChanged.Broadcast();
}

void URbMatchDirector::HandleStrokeContact(const FRbStrokeCommit& Commit)
{
	if (!SubmitStroke(Commit) && (Phase == ERbDirectorPhase::AwaitStroke || Phase == ERbDirectorPhase::Lag))
	{
		ArmStrokeForPhase(true); // refused: the component addresses the ball again
	}
}

void URbMatchDirector::HandleCueBallPlaced(const FVector& WorldPosition)
{
	const ARbTable* T = Table.Get();
	if (!T)
	{
		return;
	}
	if (!PlaceCueBall(rb::XY(T->WorldToCore(WorldPosition))) && Phase == ERbDirectorPhase::AwaitPlacement)
	{
		ArmStrokeForPhase(false); // refused: keep placing
	}
}

void URbMatchDirector::HandleStrokeAborted(bool bRampShown)
{
	OnStrokeAborted(bRampShown);
}

void URbMatchDirector::Refuse(const FString& Why)
{
	LastError = Why;
	UE_LOG(LogRawBreak, Warning, TEXT("RbMatchDirector: %s"), *Why);
}

void URbMatchDirector::SetPhase(ERbDirectorPhase NewPhase)
{
	Phase = NewPhase;
	OnMatchChanged.Broadcast();
}
