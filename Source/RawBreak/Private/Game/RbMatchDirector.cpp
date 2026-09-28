#include "Game/RbMatchDirector.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Cue/RbCue.h"
#include "Player/RbStrokeComponent.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"

#include "rb/Human/HumanModel.h"
#include "rb/Human/Progression.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/TableRules.h"

// Owner: UE-6b. TODO(UE-6b): the whole pipeline of the header (rack, turns, placement, ExecuteStroke, SimInput,
// submit, rules evaluation, commit after playback, equipment update, decisions, rack over, match over, lag), tests
// RawBreak.Unit.Match.* (headless, no rendering): scripted break -> turn logic, scratch -> ball in hand to the
// opponent, illegal placement rejected, 9 on the break wins, three fouls in hot-seat.

void URbMatchDirector::Initialize(ARbTable* InTable, ARbBallSet* InBalls, ARbCue* InCue, URbSimulationSubsystem* InSimulation)
{
	Table = InTable;
	Balls = InBalls;
	Cue = InCue;
	Simulation = InSimulation;
	if (InSimulation)
	{
		SimulatedHandle = InSimulation->OnShotSimulated.AddUObject(this, &URbMatchDirector::OnShotSimulated);
	}
	if (InBalls && InBalls->GetPlayback())
	{
		PlaybackHandle = InBalls->GetPlayback()->OnFinished.AddUObject(this, &URbMatchDirector::OnPlaybackFinished);
	}
}

bool URbMatchDirector::StartMatch(const FRbMatchSetup& InSetup)
{
	Setup = InSetup;
	// TODO(UE-6b): MatchConfig from the discipline (MakeRulesConfig), RulesTable from the table context, seed,
	// shooters (HotSeatGuestAttributes), StartMatch / lag / ChooseBreaker, first rack.
	SetPhase(ERbDirectorPhase::Idle);
	return false;
}

rb::rules::ShotConstraints URbMatchDirector::GetConstraints() const
{
	return rb::rules::GetShotConstraints(Config, State);
}

int32 URbMatchDirector::GetActivePlayer() const
{
	return State.Game.Shooter;
}

bool URbMatchDirector::CanPlaceCueBall(const rb::Vec2& /*Position*/) const { return false; /* TODO(UE-6b) */ }
bool URbMatchDirector::PlaceCueBall(const rb::Vec2& /*Position*/) { return false; /* TODO(UE-6b) */ }

void URbMatchDirector::SetCalledShot(int32 Ball, int32 Pocket)
{
	Declaration.Called.Ball = static_cast<rb::BallId>(Ball);
	Declaration.Called.Pocket = static_cast<rb::PocketId>(Pocket);
}

void URbMatchDirector::SetShotKind(rb::rules::ShotKind Kind)
{
	Declaration.Kind = Kind;
}

bool URbMatchDirector::SubmitStroke(const FRbStrokeCommit& /*Commit*/) { return false; /* TODO(UE-6b) */ }

bool URbMatchDirector::SubmitScriptedStrike(double /*Speed*/, double /*Azimuth*/, double /*Elevation*/, double /*OffsetA*/, double /*OffsetB*/)
{
	return false; // TODO(UE-6b)
}

bool URbMatchDirector::ChooseOption(rb::rules::Option /*Choice*/) { return false; /* TODO(UE-6b) */ }

void URbMatchDirector::SetTableStateForTest(const FRbTableState& NewState)
{
	TableState = NewState;
	if (ARbBallSet* BallSet = Balls.Get())
	{
		BallSet->ShowSimBalls(TableState.Balls, rb::kMaxBalls);
	}
}

void URbMatchDirector::RackNext() { /* TODO(UE-6b) */ }
void URbMatchDirector::BeginTurn() { /* TODO(UE-6b) */ }
void URbMatchDirector::OnShotSimulated(const TSharedRef<const FRbShot>& /*Shot*/) { /* TODO(UE-6b) */ }
void URbMatchDirector::OnPlaybackFinished(const TSharedRef<const FRbShot>& Shot)
{
	// The playback component also plays replays: only our own pending live shot is committed (review R-07).
	if (Phase != ERbDirectorPhase::PlayingBack || !PendingShot.IsValid() || PendingShot.Get() != &Shot.Get())
	{
		return;
	}
	CommitShot(Shot);
}
void URbMatchDirector::CommitShot(const TSharedRef<const FRbShot>& /*Shot*/) { /* TODO(UE-6b) */ }

FRbStrokeContext URbMatchDirector::MakeStrokeContext() const
{
	return FRbStrokeContext{}; // TODO(UE-6b): active shooter's attributes, situation, tip, cue body, cue, CB spec, key, history
}

void URbMatchDirector::OnStrokeAborted(bool /*bRampShown*/)
{
	// TODO(UE-6b): bRampShown -> ShooterShotIndex++, AdvanceNoiseHistory, push MakeStrokeContext() to the stroke component.
}

bool URbMatchDirector::IsReplayAllowed() const
{
	return Phase != ERbDirectorPhase::Simulating && Phase != ERbDirectorPhase::PlayingBack && Phase != ERbDirectorPhase::Idle;
}

void URbMatchDirector::SetPhase(ERbDirectorPhase NewPhase)
{
	Phase = NewPhase;
	OnMatchChanged.Broadcast();
}
