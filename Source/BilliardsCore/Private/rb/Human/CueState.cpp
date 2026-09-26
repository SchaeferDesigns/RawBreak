#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.4 (HF-T10, HF-31).
#include "rb/Human/CueState.h"

#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"

namespace rb::human
{
	WarpEffect ComputeWarp(const CueBodyState& Cue, const CueSpec& Spec, const NoiseKey& Key, const HumanParams& Params)
	{
		WarpEffect Warp;
		Warp.Roll = Cue.WarpKnown
			? 0.0
			: kTwoPi * U01(HashKeys(Key.MatchSeed, ShooterKey(Key), static_cast<std::uint64_t>(NoiseChannel::WarpRoll), static_cast<std::uint64_t>(Key.CuePickupIndex)));
		if (Cue.BowSag == 0.0 || !(Spec.Length > 0.0))
		{
			return Warp; // straight cue: no error at all (exact zeros, HF-T08)
		}
		// Chord of the sighted part (tip to the point under the eyes) against the tip tangent: gamma = kappa s_e / 2 = 4 s_w s_e / L^2.
		Warp.Gamma = 4.0 * Cue.BowSag * Params.WarpSightLength / (Spec.Length * Spec.Length);
		if (Cue.WarpKnown)
		{
			Warp.AzimuthError = 0.0;           // bow held up: the whole error goes into the elevation
			Warp.ElevationError = Warp.Gamma;
			return Warp;
		}
		Warp.AzimuthError = Warp.Gamma * Sin(Warp.Roll);
		Warp.ElevationError = Warp.Gamma * Cos(Warp.Roll);
		return Warp;
	}

	double NoticeableBow(double WarpCheckHabit)
	{
		return 1.0e-3 + (0.3e-3 - 1.0e-3) * Clamp(WarpCheckHabit, 0.0, 1.0);
	}

	bool AutoRollTestNotices(const CueBodyState& Cue, double WarpCheckHabit)
	{
		return Abs(Cue.BowSag) >= NoticeableBow(WarpCheckHabit);
	}
}
