#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.4 (HF-T10, HF-31).
#include "rb/Human/CueState.h"

namespace rb::human
{
	WarpEffect ComputeWarp(const CueBodyState& /*Cue*/, const CueSpec& /*Spec*/, const NoiseKey& /*Key*/, const HumanParams& /*Params*/)
	{
		// TODO(WP-11): gamma = 4 s_w s_e / L^2, chi from channel 10 unless WarpKnown (4.4, HF-T10).
		return {};
	}

	double NoticeableBow(double /*WarpCheckHabit*/)
	{
		// TODO(WP-11): 1 mm at habit 0 -> 0.3 mm at habit 1, linear (HF-31).
		return 1.0e-3;
	}

	bool AutoRollTestNotices(const CueBodyState& /*Cue*/, double /*WarpCheckHabit*/)
	{
		// TODO(WP-11): BowSag >= NoticeableBow(habit).
		return false;
	}
}
