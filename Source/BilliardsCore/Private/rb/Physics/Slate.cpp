#include "rb/Core/FpGuard.h"
// Owner: WP-1 (motion, slate & cue strike). Spec: physics-motion-and-cue C.3, C.4; physics-collisions 2.4 step 6.
#include "rb/Physics/Slate.h"

namespace rb
{
	SlateImpactResult ResolveSlateImpact(const Vec3& Velocity, const Vec3& Omega, const BallSpec& /*Spec*/, double /*Restitution*/,
		double /*SlidingFriction*/, const SlateParams& /*Slate*/, int /*BounceIndex*/, double /*Gravity*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-1): C.3 stick/slip impulse with k = InertiaFactor(Spec) (w_z unchanged) + C.4 v_z_min / N_max guard.
		SlateImpactResult Result;
		Result.Velocity = Velocity;
		Result.Omega = Omega;
		return Result;
	}

	void ApplyTableReaction(BallState& /*S*/, bool /*WasOnSurface*/, const BallSpec& /*Spec*/, const ClothParams& /*Surface*/,
		const SlateParams& /*Slate*/, double /*Gravity*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-1): collisions 2.4 step 6 (downward v_z -> ResolveSlateImpact, small upward v_z -> 0).
	}
}
