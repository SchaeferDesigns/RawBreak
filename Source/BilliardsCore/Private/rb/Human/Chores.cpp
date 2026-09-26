#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors principle 5, 4.1, 4.6 (HF-B04, HF-B05).
#include "rb/Human/Chores.h"

#include "rb/Math/Scalar.h"

namespace rb::human
{
	double CoinChoreDuration(int Coins, const ChoreTiming& Timing)
	{
		return Coins > 0 ? static_cast<double>(Coins) * Timing.CoinSeconds + Timing.SlideSeconds : 0.0;
	}

	double BallClearDuration(int BallsLeft, const ChoreTiming& Timing)
	{
		// 1.5-3 s per ball (HF-71): the middle of the range per leftover ball, linear in the balls left (HF-B04).
		return BallsLeft > 0 ? static_cast<double>(BallsLeft) * (0.5 * (Timing.BallClearMin + Timing.BallClearMax)) : 0.0;
	}

	int PerformChalking(TipState& Tip, const ChalkCube& Cube, ChoreMode Mode, double ChalkHabit, double RitualSweep, int TwistsBeforeAbort,
		double& Duration, const TipParams& Params)
	{
		// A / C / P: the habitual result (AutoChalkTwists twists with the habit's sweep), whatever the real-time speed; R: the
		// player's own twists (TwistsBeforeAbort; < 0 = the automatic count) with the measured sweep, capped at the habit-1 result
		// by ApplyChalkTwist's clamp. Aborting keeps exactly the twists done (HF-B04).
		const bool Ritual = Mode == ChoreMode::Ritual;
		const int Planned = AutoChalkTwists(Tip, Cube, Params);
		int Twists = Planned;
		if (Ritual)
		{
			Twists = TwistsBeforeAbort >= 0 ? TwistsBeforeAbort : Planned;
		}
		else if (TwistsBeforeAbort >= 0 && TwistsBeforeAbort < Planned)
		{
			Twists = TwistsBeforeAbort;
		}
		const double Sweep = Ritual ? Clamp(RitualSweep, 0.0, 1.0) : Clamp(ChalkHabit, 0.0, 1.0);
		// The habit-1 result (principle 5): the automatic chalking of a maxed habit from the same start. A ritual is capped at it
		// zone by zone, so extra twists (or drilling the centre) never beat the habit once it is maxed.
		TipState Habit1 = Tip;
		if (Ritual)
		{
			for (int i = 0; i < Planned; ++i)
			{
				ApplyChalkTwist(Habit1, Cube, 1.0, Params);
			}
		}
		for (int i = 0; i < Twists; ++i)
		{
			ApplyChalkTwist(Tip, Cube, Sweep, Params);
		}
		if (Ritual)
		{
			for (int z = 0; z < kTipZoneCount; ++z)
			{
				Tip.Coverage[z] = Min(Tip.Coverage[z], Habit1.Coverage[z]);
			}
		}
		switch (Mode)
		{
		case ChoreMode::Parallel: Duration = 0.0; break;                                // done while the opponent shoots
		case ChoreMode::Cut: Duration = Twists > 0 ? ChoreTiming{}.CutSeconds : 0.0; break; // 1.5 s cut
		case ChoreMode::Automatic:
		case ChoreMode::Ritual: Duration = static_cast<double>(Twists) * TwistDuration(ChalkHabit, Params); break; // R: the game's clock rules
		}
		return Twists;
	}

	RackGapParams RackGapsForQuality(double Quality)
	{
		const double Q = Clamp(Quality, 0.0, 1.0);
		const double Gap = 0.08e-3 * (1.0 - Q) + 0.005e-3 * Q;
		RackGapParams Params = kRackGapNone;
		Params.Mean = Gap;
		Params.Jitter = Gap;
		return Params;
	}
}
