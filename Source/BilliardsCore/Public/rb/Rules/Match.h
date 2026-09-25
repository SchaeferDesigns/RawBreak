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
		int TargetPoints = 100;        // 14.1
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
		int TeamBreaker[2] = {0, 0};   // Doubles: member who broke the team's last rack (team breakers alternate)
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

	RB_API void StartMatch(const MatchConfig& Config, MatchState& State);
	RB_API ErrorCode ApplyLagResult(const MatchConfig& Config, MatchState& State, const LagResult& Result);
	RB_API ErrorCode ChooseBreaker(const MatchConfig& Config, MatchState& State, int Breaker);

	// Builds the next rack (GenerateRack with the next rack seed) and enters AwaitShot with the break.
	RB_API ErrorCode SetupRack(const MatchConfig& Config, MatchState& State, RackAssignment& OutRack);

	RB_API ShotConstraints GetShotConstraints(const MatchConfig& Config, const MatchState& State);

	// Before the stroke: call present/legal for the discipline, push-out only in its window, no safety
	// in 10-ball (T11) / 9-ball, claimed group really cleared, and - when the cue ball is in hand -
	// the placement PlacedCueBall (non-null required then) with CueBallPlacementLegal (region, overlaps
	// with per-ball radii, not over a pocket opening; 16.16).
	RB_API ErrorCode ValidateDeclaration(const MatchConfig& Config, const MatchState& State, const ShotDeclaration& Declaration, const Vec2* PlacedCueBall);

	// Applies an evaluated shot (end of rules.md 10): scores, foul counters, groups, removal of
	// supported balls, spotting (4.3), rack commands (9.5), CB state, push-out window, turn / decision /
	// rack over / match over, stalemate counter. Final ball positions come from Facts.Balls[].FinalPosition.
	RB_API ErrorCode ApplyShot(const MatchConfig& Config, MatchState& State, const ShotOutcome& Outcome, const ShotFacts& Facts);

	// AwaitDecision: the deciding player picks one of State.PendingOutcome.Options (11.3 table).
	RB_API ErrorCode ApplyOption(const MatchConfig& Config, MatchState& State, Option Choice);

	// 4.4 spot request by the shooter in AwaitShot.
	RB_API ErrorCode RequestSpot(const MatchConfig& Config, MatchState& State);

	RB_API ErrorCode DeclareStalemate(const MatchConfig& Config, MatchState& State);
	RB_API void Concede(MatchState& State, int Player);

	// Shot clock (4.10): allowed time for the current shot, expiry test, extension request. The clock
	// starts when every ball has stopped moving AND spinning, i.e. at ShotEndSnapshot::StopTime (C03).
	RB_API double ShotClockAllowed(const MatchConfig& Config, const MatchState& State);
	RB_API bool ShotClockExpired(const MatchConfig& Config, const MatchState& State, double Elapsed);
	RB_API ErrorCode RequestShotClockExtension(const MatchConfig& Config, MatchState& State);
	RB_API double ShotClockStartTime(const ShotEndSnapshot& PreviousShotEnd);
}
