#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 3.3, 3.4 (pressure), HF-16 (fatigue).
#include "rb/Human/Skill.h"

namespace rb::human
{
	double ComputePressure(const PressureInputs& /*Inputs*/, PressureMode /*Mode*/, const PressureWeights& /*Weights*/)
	{
		// TODO(WP-11): weighted sum of 3.4, clamp [0, 1], Subtle x0.5, Off 0.
		return 0.0;
	}

	double FatigueFromNight(double /*InGameHoursThisNight*/, bool /*Applies*/)
	{
		// TODO(WP-11): clamp((hours - 2) / 2, 0, 1), 0 when not applicable (HF-16).
		return 0.0;
	}
}
