#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 3.3, 3.4 (pressure), HF-16 (fatigue).
#include "rb/Human/Skill.h"

namespace rb::human
{
	double ComputePressure(const PressureInputs& Inputs, PressureMode Mode, const PressureWeights& Weights)
	{
		if (Mode == PressureMode::Off)
		{
			return 0.0;
		}
		// UnitOrZero instead of Clamp: identical for finite values, but a NaN (a shot-clock fraction elapsed / limit with a zero limit,
		// NaN stakes) counts as 0 instead of making P NaN.
		const double Stakes = UnitOrZero(Inputs.Stakes);
		const double GameBall = Inputs.GameBall ? 1.0 : 0.0;
		const double Hill = Inputs.Hill ? 1.0 : 0.0;
		const double Crowd = Clamp(static_cast<double>(Inputs.Watchers) / 10.0, 0.0, 1.0);
		const double Clock = UnitOrZero(Inputs.ShotClockFraction);
		const double Run = Clamp(static_cast<double>(Inputs.RunLength) / 8.0, 0.0, 1.0);
		const double P = UnitOrZero(Weights.Stakes * Stakes + Weights.GameBall * GameBall + Weights.Hill * Hill + Weights.Crowd * Crowd +
			Weights.Clock * Clock + Weights.Run * Run);
		return Mode == PressureMode::Subtle ? 0.5 * P : P;
	}

	double FatigueFromNight(double InGameHoursThisNight, bool Applies)
	{
		if (!Applies || !(InGameHoursThisNight > 2.0))
		{
			return 0.0;
		}
		return Clamp((InGameHoursThisNight - 2.0) / 2.0, 0.0, 1.0);
	}
}
