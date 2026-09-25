#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 6.
#include "rb/Equipment/BallSets.h"

namespace rb
{
	ErrorCode BuildBallSet(BallSetPreset /*Preset*/, std::uint64_t /*Seed*/, BallSet& Out)
	{
		// TODO(WP-2): presets StandardPool / DiveBar (seeded N(0.163, 0.003) clamp) / OldBarOversizedCue / Snooker.
		Out = BallSet{};
		return ErrorCode::NotImplemented;
	}
}
