#pragma once

// RulesConfig switches (rules.md 12.1, WPA defaults) and the variant presets (12.2-12.6).
// Owner: WP-8 (rules facts & evaluation).

#include "rb/Config.h"
#include "rb/Core/Tolerances.h"
#include "rb/Rules/RulesTypes.h"

#include <cstdint>

namespace rb::rules
{
	enum class FoulScope : std::uint8_t { AllBall, CueBallOnly };
	enum class CallMode : std::uint8_t { Explicit, ObviousAssist, EightOnly, None };
	enum class ScoopPolicy : std::uint8_t { WpaMiscue, Foul };
	enum class BreakOrder : std::uint8_t { Alternate, WinnerBreaks, LoserBreaks };
	enum class FoulCueBallMode : std::uint8_t { InHandAnywhere, InHandBehindHeadString, InHandBaulk, FreeShotPlusVisit, TwoVisits, InPosition };
	enum class EightOnBreakRule : std::uint8_t { SpotOrRebreakOption, Win, Rerack };
	enum class EightOnBreakWithFoulRule : std::uint8_t { OpponentOption, Lose, Rerack };
	enum class BreakAssignsGroupRule : std::uint8_t { Off, IfOnlyOneGroupPocketed };
	enum class BreakFirstContactRule : std::uint8_t { Any, HeadBallOrSecondRow };
	enum class NineBallRackRule : std::uint8_t { NineOnSpot, OneOnSpot };
	enum class ThreeBallRuleScope : std::uint8_t { Reg16Combined, OnlyIfNothingPocketed };
	enum class TenOnlyBallMoment : std::uint8_t { ShotStart, AtPocketing, EarlyTenWins };
	enum class JumpShotRule : std::uint8_t { Legal, Illegal };
	enum class InputMode : std::uint8_t { Assisted, Sim }; // Sim: avatar/cue colliders live (fouls 3.4/3.6/3.10 possible)

	struct RulesConfig
	{
		FoulScope Fouls = FoulScope::AllBall;
		CallMode Calls = CallMode::Explicit;               // ranked default; casual preset uses ObviousAssist
		ScoopPolicy Scoop = ScoopPolicy::WpaMiscue;
		bool ThreeFoulRule = true;                         // 8-ball presets: false (R 3.13)
		BreakOrder Breaks = BreakOrder::Alternate;
		FoulCueBallMode FoulCueBall = FoulCueBallMode::InHandAnywhere;
		bool RailAfterContactRequired = true;
		EightOnBreakRule EightOnBreak = EightOnBreakRule::SpotOrRebreakOption;
		EightOnBreakWithFoulRule EightOnBreakWithFoul = EightOnBreakWithFoulRule::OpponentOption;
		bool ScratchWhileShootingEightLoses = false;
		bool LastPocketRule = false;
		BreakAssignsGroupRule BreakAssignsGroup = BreakAssignsGroupRule::Off;
		bool OpenTableEightFirstFoul = true;
		bool OpenTableEightGroupGoneException = true;      // 2025 R 4.4 exception (false = 2016 LEGACY)
		bool SpotJumpedObjectBalls = false;
		BreakFirstContactRule BreakFirstContact = BreakFirstContactRule::Any;
		NineBallRackRule NineBallRack = NineBallRackRule::NineOnSpot;
		bool ThreeBallRule = true;
		ThreeBallRuleScope ThreeBallScope = ThreeBallRuleScope::Reg16Combined;
		bool ThreeBallRuleReach = false;                   // LEGACY: balls merely reaching the head string count
		TenOnlyBallMoment TenOnlyBall = TenOnlyBallMoment::ShotStart;
		int TargetPoints = 100;                            // 14.1
		JumpShotRule JumpShots = JumpShotRule::Legal;
		bool UseRackTemplate = false;
		InputMode Input = InputMode::Assisted;
		int StalemateInnings = 8;                          // N_stall (DERIVED game heuristic)
		RulesTolerances Tolerances;
	};

	enum class RulesPreset : std::uint8_t
	{
		Wpa8Ball,
		Wpa9Ball,
		Wpa10Ball,
		Wpa14_1,
		WpaBlackball,
		WpaLegacy9Ball, // 12.2
		Apa8Ball,       // 12.3 (verify against the current APA manual before shipping)
		BarHouse8Ball,  // 12.6 house-rule toggles
	};

	RB_API RulesConfig MakeRulesConfig(RulesPreset Preset);
	RB_API Discipline DisciplineOf(RulesPreset Preset);
}
