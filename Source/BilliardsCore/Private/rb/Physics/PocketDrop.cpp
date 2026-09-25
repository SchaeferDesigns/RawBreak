#include "rb/Core/FpGuard.h"
// Owner: WP-4 (cushion, facing & pocket-edge resolution). Spec: physics-collisions 5.4.
#include "rb/Physics/PocketDrop.h"

namespace rb
{
	PivotResult ComputePivot(double /*V0*/, double /*Rho*/, double /*InertiaK*/, double /*Gravity*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-4): leave angle, T_p via asinh substitution + 16-panel Simpson, leave speed (P-2).
		return {};
	}

	PivotPath MakePivotPath(const BallState& /*AtDropEdge*/, double T0, const PocketGeometry& Pocket, const BallSpec& Spec, double /*Gravity*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-4): axis point at a_d / z = -r_d below the crossing, n_e, tangential speed, ComputePivot.
		PivotPath Path;
		Path.Pocket = Pocket.Id;
		Path.T0 = T0;
		Path.Radius = Spec.Radius;
		return Path;
	}

	BallState EvaluatePivot(const PivotPath& /*Path*/, double /*Tau*/)
	{
		// TODO(WP-4): exact circular pivot state at tau.
		BallState S;
		S.State = MotionState::PocketPivot;
		return S;
	}

	BallState PivotLeaveState(const PivotPath& /*Path*/)
	{
		// TODO(WP-4): state at psi_leave (5.4 "state at leave").
		BallState S;
		S.State = MotionState::PocketFall;
		return S;
	}

	MotionSegment PivotDetectionProxy(const PivotPath& Path)
	{
		// TODO(WP-4): quadratic proxy through start and leave positions.
		MotionSegment Seg;
		Seg.State = MotionState::PocketPivot;
		Seg.T0 = Path.T0;
		Seg.Radius = Path.Radius;
		return Seg;
	}
}
