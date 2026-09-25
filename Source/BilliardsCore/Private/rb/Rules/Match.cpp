#include "rb/Core/FpGuard.h"
// Owner: WP-9 (rules table procedures & match). Spec: rules.md 4.2, 4.8, 4.10, 11, Reg 14/18/27.
#include "rb/Rules/Match.h"

namespace rb::rules
{
	void StartMatch(const MatchConfig& Config, MatchState& State)
	{
		// TODO(WP-9): players, seeded rack counter, scores, phase Lag.
		State = MatchState{};
		State.Game.Game = Config.Game;
		State.Phase = MatchPhase::Lag;
	}

	ErrorCode ApplyLagResult(const MatchConfig& /*Config*/, MatchState& /*State*/, const LagResult& /*Result*/)
	{
		// TODO(WP-9): Relag or LagWinnerChooses.
		return ErrorCode::NotImplemented;
	}

	ErrorCode ChooseBreaker(const MatchConfig& /*Config*/, MatchState& /*State*/, int /*Breaker*/)
	{
		// TODO(WP-9): LagWinnerChooses -> RackSetup.
		return ErrorCode::NotImplemented;
	}

	ErrorCode SetupRack(const MatchConfig& /*Config*/, MatchState& /*State*/, RackAssignment& OutRack)
	{
		// TODO(WP-9): GenerateRack with the next rack seed, reset per-rack counters (9/10-ball), AwaitShot.
		OutRack = RackAssignment{};
		return ErrorCode::NotImplemented;
	}

	ShotConstraints GetShotConstraints(const MatchConfig& /*Config*/, const MatchState& /*State*/)
	{
		// TODO(WP-9): 11.3 AwaitShot publication.
		return {};
	}

	ErrorCode ValidateDeclaration(const MatchConfig& /*Config*/, const MatchState& /*State*/, const ShotDeclaration& /*Declaration*/,
		const Vec2* /*PlacedCueBall*/)
	{
		// TODO(WP-9): pitfall 16 validation (N19, T11) incl. CueBallPlacementLegal for an in-hand cue ball.
		return ErrorCode::NotImplemented;
	}

	ErrorCode ApplyShot(const MatchConfig& /*Config*/, MatchState& /*State*/, const ShotOutcome& /*Outcome*/, const ShotFacts& /*Facts*/)
	{
		// TODO(WP-9): end of rules.md 10 (apply outcome) + stalemate counter + rack/match over.
		return ErrorCode::NotImplemented;
	}

	ErrorCode ApplyOption(const MatchConfig& /*Config*/, MatchState& /*State*/, Option /*Choice*/)
	{
		// TODO(WP-9): 11.3 option table.
		return ErrorCode::NotImplemented;
	}

	ErrorCode RequestSpot(const MatchConfig& /*Config*/, MatchState& /*State*/)
	{
		// TODO(WP-9): 4.4 spot request.
		return ErrorCode::NotImplemented;
	}

	ErrorCode DeclareStalemate(const MatchConfig& /*Config*/, MatchState& /*State*/)
	{
		// TODO(WP-9): 8/9/10: re-rack, original breaker; 14.1: new lag, scores kept.
		return ErrorCode::NotImplemented;
	}

	void Concede(MatchState& State, int Player)
	{
		// TODO(WP-9): bookkeeping (statistics); the opponent wins the match (R 1.12).
		State.Phase = MatchPhase::MatchOver;
		State.Winner = 1 - Player;
	}

	double ShotClockAllowed(const MatchConfig& /*Config*/, const MatchState& /*State*/)
	{
		// TODO(WP-9): 35 s (+ 25 s extension), <= 60 s after the break, 0 when disabled.
		return 0.0;
	}

	bool ShotClockExpired(const MatchConfig& /*Config*/, const MatchState& /*State*/, double /*Elapsed*/)
	{
		// TODO(WP-9): Elapsed > ShotClockAllowed when the clock is enabled.
		return false;
	}

	ErrorCode RequestShotClockExtension(const MatchConfig& /*Config*/, MatchState& /*State*/)
	{
		// TODO(WP-9): one extension per player per rack (C02).
		return ErrorCode::NotImplemented;
	}

	double ShotClockStartTime(const ShotEndSnapshot& PreviousShotEnd)
	{
		// TODO(WP-9): confirm against C03 (spinning balls delay the clock start).
		return PreviousShotEnd.StopTime;
	}
}
