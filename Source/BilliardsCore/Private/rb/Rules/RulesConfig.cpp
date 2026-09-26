#include "rb/Core/FpGuard.h"
// Owner: WP-8 (rules facts & evaluation). Spec: rules.md 12.
#include "rb/Rules/RulesConfig.h"

namespace rb::rules
{
	RulesConfig MakeRulesConfig(RulesPreset Preset)
	{
		// Every preset starts from the WPA defaults of rules.md 12.1 (bold values) and changes only
		// what its section lists.
		RulesConfig C;
		switch (Preset)
		{
		case RulesPreset::Wpa8Ball:
			C.Calls = CallMode::Explicit;
			C.ThreeFoulRule = false; // R 3.13: 8-ball has no three-foul rule
			break;

		case RulesPreset::Wpa9Ball:
			C.Calls = CallMode::None; // 9-ball has no calls: every legally pocketed ball counts (R 5.5)
			C.ThreeFoulRule = true;
			break;

		case RulesPreset::Wpa10Ball:
			C.Calls = CallMode::Explicit; // no safety call (validator)
			C.ThreeFoulRule = true;
			break;

		case RulesPreset::Wpa14_1:
			C.Calls = CallMode::Explicit;
			C.ThreeFoulRule = true;
			C.TargetPoints = 100;
			break;

		case RulesPreset::WpaBlackball:
			// 12.4: shots are not called, foul = free shot (evaluator) with the cue ball in position or in
			// hand in baulk, jumping over a ball is a foul, balls driven off the table are spotted.
			C.Calls = CallMode::None;
			C.ThreeFoulRule = false;
			C.FoulCueBall = FoulCueBallMode::InHandBaulk;
			C.JumpShots = JumpShotRule::Illegal;
			C.SpotJumpedObjectBalls = true;
			C.OpenTableEightGroupGoneException = false;
			break;

		case RulesPreset::WpaLegacy9Ball:
			// 12.2: 1 on the foot spot, three-ball rule counts balls that merely reach the head string.
			C.Calls = CallMode::None;
			C.ThreeFoulRule = true;
			C.NineBallRack = NineBallRackRule::OneOnSpot;
			C.ThreeBallRuleReach = true;
			break;

		case RulesPreset::Apa8Ball:
			// 12.3 (verify against the current APA manual before shipping).
			C.Calls = CallMode::EightOnly;
			C.ThreeFoulRule = false;
			C.Fouls = FoulScope::CueBallOnly;
			C.Scoop = ScoopPolicy::Foul;
			C.EightOnBreak = EightOnBreakRule::Win;
			C.EightOnBreakWithFoul = EightOnBreakWithFoulRule::Lose;
			C.ScratchWhileShootingEightLoses = true;
			C.BreakAssignsGroup = BreakAssignsGroupRule::IfOnlyOneGroupPocketed;
			C.SpotJumpedObjectBalls = true;
			C.BreakFirstContact = BreakFirstContactRule::HeadBallOrSecondRow;
			C.OpenTableEightGroupGoneException = false; // WPA 2025 exception only
			break;

		case RulesPreset::BarHouse8Ball:
			// 12.6 house-rule toggles (each individually selectable in the lobby).
			C.Calls = CallMode::EightOnly;
			C.ThreeFoulRule = false;
			C.FoulCueBall = FoulCueBallMode::InHandBehindHeadString;
			C.RailAfterContactRequired = false;
			C.EightOnBreak = EightOnBreakRule::Win;
			C.EightOnBreakWithFoul = EightOnBreakWithFoulRule::Lose;
			C.ScratchWhileShootingEightLoses = true;
			C.LastPocketRule = true;
			C.BreakAssignsGroup = BreakAssignsGroupRule::IfOnlyOneGroupPocketed;
			C.SpotJumpedObjectBalls = true;
			C.OpenTableEightGroupGoneException = false;
			break;
		}
		return C;
	}

	Discipline DisciplineOf(RulesPreset Preset)
	{
		switch (Preset)
		{
		case RulesPreset::Wpa8Ball:
		case RulesPreset::Apa8Ball:
		case RulesPreset::BarHouse8Ball: return Discipline::EightBall;
		case RulesPreset::Wpa9Ball:
		case RulesPreset::WpaLegacy9Ball: return Discipline::NineBall;
		case RulesPreset::Wpa10Ball: return Discipline::TenBall;
		case RulesPreset::Wpa14_1: return Discipline::StraightPool;
		case RulesPreset::WpaBlackball: return Discipline::Blackball;
		}
		return Discipline::NineBall;
	}
}
