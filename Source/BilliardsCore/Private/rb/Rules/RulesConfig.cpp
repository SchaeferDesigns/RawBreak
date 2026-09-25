#include "rb/Core/FpGuard.h"
// Owner: WP-8 (rules facts & evaluation). Spec: rules.md 12.
#include "rb/Rules/RulesConfig.h"

namespace rb::rules
{
	RulesConfig MakeRulesConfig(RulesPreset /*Preset*/)
	{
		// TODO(WP-8): per-preset flags (12.1 defaults; 8-ball ThreeFoulRule off; 12.2-12.6 variants).
		return RulesConfig{};
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
