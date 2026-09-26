#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors principle 5, 4.1, 4.6 (HF-B04, HF-B05).
#include "rb/Human/Chores.h"

namespace rb::human
{
	double CoinChoreDuration(int /*Coins*/, const ChoreTiming& /*Timing*/)
	{
		// TODO(WP-11): Coins x 1 s + 2 s.
		return 0.0;
	}

	double BallClearDuration(int /*BallsLeft*/, const ChoreTiming& /*Timing*/)
	{
		// TODO(WP-11): linear in the balls left.
		return 0.0;
	}

	int PerformChalking(TipState& /*Tip*/, const ChalkCube& /*Cube*/, ChoreMode /*Mode*/, double /*ChalkHabit*/, double /*RitualSweep*/, int /*TwistsBeforeAbort*/,
		double& Duration, const TipParams& /*Params*/)
	{
		// TODO(WP-11): AutoChalkTwists / ritual twists, abort keeps the twists done (HF-B04, HF-B05).
		Duration = 0.0;
		return 0;
	}

	RackGapParams RackGapsForQuality(double /*Quality*/)
	{
		// TODO(WP-11): Mean = Jitter = 0.08 mm (1 - Q) + 0.005 mm Q (4.6).
		return kRackGapNone;
	}
}
