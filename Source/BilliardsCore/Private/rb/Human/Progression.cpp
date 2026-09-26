#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 5.1-5.4, section 7 (product-owner switches).
#include "rb/Human/Progression.h"

namespace rb::human
{
	double XpAward(XpSource /*Source*/, double /*Value*/)
	{
		// TODO(WP-11): the 5.2 table.
		return 0.0;
	}

	double AttributePointCost(double /*Attribute*/)
	{
		// TODO(WP-11): 100 x 1.08^(x - 25).
		return 0.0;
	}

	double XpToRaise(double /*From*/, double /*To*/)
	{
		// TODO(WP-11): sum of AttributePointCost.
		return 0.0;
	}

	double RepeatFactor(int /*RepeatsWithinWindow*/)
	{
		// TODO(WP-11): 0.5^Repeats (anti-grind, 5.2).
		return 1.0;
	}
}
