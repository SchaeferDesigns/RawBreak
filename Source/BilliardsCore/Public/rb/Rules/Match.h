#pragma once

// Match state machine (race to N / 14.1 target points), declaration validation, applying outcomes
// and decisions, stalemate, concession, shot clock (rules.md 4.2, 4.8, 4.10, 11).
// Owner: WP-9 (rules table procedures & match).
//
//   Setup -> Lag -> LagWinnerChooses -> RackSetup -> AwaitShot <-> AwaitDecision
//        AwaitShot --ApplyShot--> AwaitShot | AwaitDecision | RackOver -> RackSetup | MatchOver
//   14.1 stalemate -> Lag (scores kept); 8/9/10 stalemate -> RackSetup(RackBreaker); Concede -> MatchOver

#include "rb/Config.h"
#include "rb/Core/Error.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Rules/TableRules.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>

namespace rb::rules
{
	enum class MatchPhase : std::uint8_t
	{
		Setup,
		Lag,
		LagWinnerChooses,
		RackSetup,
		AwaitShot,
		AwaitDecision,
		RackOver,
		MatchOver,
	};

	struct ShotClockConfig
	{
		bool Enabled = false;          // off by default (CONFIG)
		double ShotTime = 35.0;        // [s]
		double WarningAt = 10.0;       // [s] remaining
		double ExtensionTime = 25.0;   // [s]
		int ExtensionsPerRack = 1;     // per player (14.1: per 15 balls, INTERPRETATION)
		double AfterBreakMax = 60.0;   // [s] upper bound for the shot after the opening break
	};

	struct ShotClockState
	{
		bool ExtensionActive = false;  // requested for the current shot
		int ExtensionsUsed[2] = {0, 0};// in the current rack
	};

	struct MatchConfig
	{
		Discipline Game = Discipline::NineBall;
		int RaceTo = 7;                // racks (8/9/10-ball)
		int TargetPoints = 100;        // 14.1 (informational: the evaluator ends the match with Rules.TargetPoints; keep both equal)
		int RacksPerSet = 0;           // 0 = no sets
		int SetsToWin = 0;
		RulesConfig Rules;
		ShotClockConfig Clock;
		bool Doubles = false;          // Reg 27 alternation (team = player index; MatchState::ActiveMember tracks the member)
		std::uint64_t Seed = 0;        // rack RNG root seed (stored in the replay)
		RackGapParams RackGaps = kRackGapWoodenRack;
		RulesTable Table;
	};

	struct MatchState
	{
		MatchPhase Phase = MatchPhase::Setup;
		GameState Game;
		int RackWins[2] = {0, 0};
		int SetWins[2] = {0, 0};
		int RackNumber = 0;
		int FirstBreaker = -1;
		int LagWinner = -1;
		int Decider = -1;              // player choosing in AwaitDecision
		ShotOutcome PendingOutcome;    // outcome whose Options are pending
		int Winner = -1;               // MatchOver
		std::uint64_t RackCounter = 0; // rack seed = hash(Seed, RackCounter)
		ShotClockState Clock;
		int InningsWithoutProgress = 0;// stalemate heuristic (4.8)
		bool StalemateWarning = false;
		int ActiveMember[2] = {0, 0};  // Doubles: member (0/1) of each team due to shoot next (Reg 27; pass-backs go to the partner)
		int TeamBreaker[2] = {0, 0};   // Doubles: member who broke the team's last rack (team breakers alternate; -1 = none yet)
		bool InningHadProgress = false;// stalemate heuristic: an object ball was pocketed or a foul occurred in the current inning
		bool ShotAfterBreak = false;   // the next shot is the first shot after a break (shot clock AfterBreakMax)
	};

	// Bit mask of CueBallNext values (bit = 1u << value).
	constexpr std::uint8_t CueBallChoiceBit(CueBallNext Region) { return static_cast<std::uint8_t>(1u << static_cast<unsigned>(Region)); }

	// Published to the UI / AI in AwaitShot (11.3).
	struct ShotConstraints
	{
		CueBallNext PlacementRegion = CueBallNext::InPosition; // default choice
		std::uint8_t PlacementChoices = 0;     // all allowed choices (CueBallChoiceBit): e.g. the Blackball free shot
		                                       //   allows InPosition | InHandBaulk
		bool FreeShot = false;                 // Blackball free shot (3.2 suspended)
		int VisitsRemaining = 0;               // TwoVisits / FreeShotPlusVisit
		int Member = 0;                        // Doubles: team member due to shoot
		bool CallRequired = false;
		bool SafetyAllowed = false;
		bool PushOutAllowed = false;
		bool MayRequestSpot = false;
		bool MayClaimClearedGroup = false;
		std::uint32_t LegalFirstContactMask = 0; // hint
		bool ThreeFoulWarning = false;           // shooter is on two fouls (display = mandatory warning, Reg 8)
		double ShotClockAllowed = 0.0;           // [s], 0 = no clock
	};

	// Setup -> Lag. Player (team) 0 lags with LagResult::First, player 1 with LagResult::Second.
	RB_API void StartMatch(const MatchConfig& Config, MatchState& State);
	// Lag: Relag stays in Lag; a winner -> LagWinnerChooses (Decider = LagWinner).
	RB_API ErrorCode ApplyLagResult(const MatchConfig& Config, MatchState& State, const LagResult& Result);
	// LagWinnerChooses -> RackSetup with Breaker as the rack breaker (first rack, or 14.1 after a stalemate).
	RB_API ErrorCode ChooseBreaker(const MatchConfig& Config, MatchState& State, int Breaker);

	// Builds the next rack (GenerateRack with the next rack seed) and enters AwaitShot with the break.
	// Allowed in RackSetup and RackOver. On error the state is unchanged.
	RB_API ErrorCode SetupRack(const MatchConfig& Config, MatchState& State, RackAssignment& OutRack);

	// Same state transition as SetupRack with a given rack (replays / networked play with an authoritative
	// rack, practice layouts, tests): racked balls OnTable at Rack.Position, the others NotUsed, cue ball in
	// hand (above HS; Blackball: baulk; its status is Pocketed = "not on the table" until the shot places it,
	// the same representation ApplyShot uses after a scratch), break shot, table open, 8/9/10-ball/Blackball foul counters reset
	// (14.1 keeps them), shot-clock extensions reset. Consumes no rack seed.
	RB_API ErrorCode SetupRackWith(const MatchConfig& Config, MatchState& State, const RackAssignment& Rack);

	RB_API ShotConstraints GetShotConstraints(const MatchConfig& Config, const MatchState& State);

	// 8-ball open table (rules.md 6.3): calling the 8 auto-sets the claim of a completely cleared group
	// (Solids if both are gone). Call before ValidateDeclaration; other declarations are left unchanged.
	RB_API void CompleteDeclaration(const MatchConfig& Config, const MatchState& State, ShotDeclaration& Declaration);

	// Before the stroke: call present/legal for the discipline, push-out only in its window, no safety
	// in 10-ball (T11) / 9-ball, claimed group really cleared (and a claim only with a call of the 8, a
	// safety or no explicit call), and - when the cue ball is in hand -
	// the placement PlacedCueBall (non-null required then) with CueBallPlacementLegal (region, overlaps
	// with per-ball radii, not over a pocket opening; 16.16). A cue ball in position must not be placed
	// (PlacedCueBall == nullptr); the Blackball free shot allows both. In InputMode::Sim an illegal placement
	// is not rejected (it becomes foul 3.10 in the evaluation).
	RB_API ErrorCode ValidateDeclaration(const MatchConfig& Config, const MatchState& State, const ShotDeclaration& Declaration, const Vec2* PlacedCueBall);

	// Applies an evaluated shot (end of rules.md 10): scores, foul counters, groups, removal of
	// supported balls, spotting (4.3), rack commands (9.5), CB state, push-out window, turn / decision /
	// rack over / match over, stalemate counter. Final ball positions come from Facts.Balls[].FinalPosition.
	// Ball status per ball OnTable at shot start (and the cue ball): pocketed (F4 list) -> Pocketed,
	// off table (F5) -> OutOfPlay (a cue ball off the table -> Pocketed = in hand), EndStatus OnTable ->
	// FinalPosition, no end information -> unchanged. Only the SHOOTER's foul counter is taken from
	// Outcome.FoulsAfter (the opponent's cannot change on this shot). RackWon -> RackOver (or MatchOver),
	// RerackAndBreak -> RackSetup. Outcome.Rack is executed only while the rack stays in play (Continue,
	// Pass, AwaitDecision); the Rerack15 of a RerackAndBreak outcome (14.1 three-foul penalty) is the new
	// rack that SetupRack builds. A continuation rack whose micro-gaps (Config.RackGaps) would overlap a
	// ball that stays on the table (15th ball / cue ball just outside the outline) is racked without gaps.
	// On error (e.g. a continuation rack that cannot be generated) the state is unchanged.
	RB_API ErrorCode ApplyShot(const MatchConfig& Config, MatchState& State, const ShotOutcome& Outcome, const ShotFacts& Facts);

	// AwaitDecision: the deciding player picks one of State.PendingOutcome.Options (11.3 table).
	RB_API ErrorCode ApplyOption(const MatchConfig& Config, MatchState& State, Option Choice);

	// 4.4 spot request by the shooter in AwaitShot (legal balls: LegalFirstContactMask); InvalidOption if
	// not available.
	RB_API ErrorCode RequestSpot(const MatchConfig& Config, MatchState& State);

	// 8/9/10-ball, Blackball: RackSetup with the original breaker of the rack, no score change; 14.1: Lag
	// (scores and foul counters kept, rules.md 14 #17). Allowed in AwaitShot and AwaitDecision.
	RB_API ErrorCode DeclareStalemate(const MatchConfig& Config, MatchState& State);
	// R 1.12: Player (0/1) concedes -> MatchOver, the opponent wins. Ignored once the match is over or for
	// an invalid player.
	RB_API void Concede(MatchState& State, int Player);

	// Shot clock (4.10): allowed time for the current shot, expiry test, extension request. The clock
	// starts when every ball has stopped moving AND spinning, i.e. at ShotEndSnapshot::StopTime (C03).
	// Allowed time: ShotTime, for the first shot after a break Max(ShotTime, Min(AfterBreakMax, 60 s))
	// (INTERPRETATION of Reg 18), plus ExtensionTime while an extension is active; 0 = no clock.
	// One extension per player per rack (14.1: per 15-ball rack, also reset by continuation racks).
	RB_API double ShotClockAllowed(const MatchConfig& Config, const MatchState& State);
	RB_API bool ShotClockExpired(const MatchConfig& Config, const MatchState& State, double Elapsed);
	RB_API ErrorCode RequestShotClockExtension(const MatchConfig& Config, MatchState& State);
	RB_API double ShotClockStartTime(const ShotEndSnapshot& PreviousShotEnd);
}
