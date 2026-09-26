#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.3 (HF-40). The cling itself is WP-3 (rb/Physics/BallBall.h).
#include "rb/Human/BallMarks.h"

namespace rb::human
{
	void DepositChalkMark(BallChalkMarks& /*Marks*/, const Quat& /*Orientation*/, const Vec3& /*ContactDir*/, double /*TipCoverage*/, bool /*Miscue*/,
		const MarkParams& /*Params*/)
	{
		// TODO(WP-11): body-frame direction, strength / radius, replace the weakest when full (4.3).
	}

	void FadeChalkMarks(BallChalkMarks& /*Marks*/, double /*SlideDistance*/, double /*RollDistance*/, const MarkParams& /*Params*/)
	{
		// TODO(WP-11): Strength *= exp(-d_slide / 0.5 m - d_roll / 20 m); drop < 0.05 (4.3).
	}

	TravelDistances ComputeTravelDistances(const ShotResult& /*Result*/, int /*Ball*/)
	{
		// TODO(WP-11): path lengths of Sliding / Rolling (and Sampled) segments of the ball's track.
		return {};
	}
}
