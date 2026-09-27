#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 5.1-5.4, section 7 (product-owner switches).
#include "rb/Human/Progression.h"

#include "rb/Math/Scalar.h"

namespace rb::human
{
	double XpAward(XpSource Source, double Value)
	{
		if (!IsFinite(Value))
		{
			return 0.0;
		}
		switch (Source)
		{
		case XpSource::LongPot: return 10.0 * Clamp(Value, 0.5, 5.0);                        // 10 D, D clamped [0.5, 5]
		case XpSource::LeaveQuality: return 20.0 * Max(0.0, Value);                          // 20 max(0, improvement)
		case XpSource::SpinShot: return Value >= 0.3 ? 8.0 * Min(Value, 1.0) / 0.3 : 0.0;    // 8 rho / 0.3 for rho >= 0.3
		case XpSource::PowerShot: return 5.0 + 10.0 * Clamp(Value, 0.0, 1.0);               // 5-15 by power in [0, 1]
		case XpSource::AwkwardShot: return 10.0 * Clamp(Value, 0.0, 1.0);                   // 10 d_s
		case XpSource::PressureMake: return Value >= 0.5 ? 15.0 * Min(Value, 1.0) : 0.0;    // 15 P, only at P >= 0.5
		case XpSource::Drill: return Max(0.0, Value);                                       // the tier's award (the drill table's
		                                                                                    //   data; the caller pays 10x on a first clear)
		case XpSource::Match: return Value >= 0.5 ? 10.0 : 6.0;                             // Value 1 = won, 0 = lost (60 %)
		}
		return 0.0;
	}

	double AttributePointCost(double Attribute)
	{
		return 100.0 * Pow(1.08, Clamp(Attribute, 0.0, 100.0) - 25.0);
	}

	double XpToRaise(double From, double To)
	{
		double Sum = 0.0;
		// Whole points From, From + 1, ... below To (To - From rounded to the nearest whole number of points).
		const double Points = Floor(To - From + 0.5);
		if (!(Points > 0.0))
		{
			return 0.0;
		}
		const int Count = static_cast<int>(Min(Points, 200.0));
		for (int i = 0; i < Count; ++i)
		{
			Sum += AttributePointCost(From + static_cast<double>(i));
		}
		return Sum;
	}

	double RepeatFactor(int RepeatsWithinWindow)
	{
		double Factor = 1.0;
		for (int i = 0; i < RepeatsWithinWindow && i < 64; ++i)
		{
			Factor *= 0.5;
		}
		return Factor;
	}
}
