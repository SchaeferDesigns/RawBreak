#include "Cue/RbCueClearance.h"

#include "Table/RbTable.h"

// Owner: UE-4. TODO(UE-4): coarse 0.25 deg sweep + bisection to 0.01 deg (plan 5.5), rails from the cushion
// profile / rail-top heights, environment capsule sweep, tests T13 + rail cases.

namespace RbCueClearance
{
	FRbCueClearanceResult ComputeMinElevation(const FRbTableContext& /*Table*/, const rb::human::CueBodyState& /*Body*/,
		const FRbCueClearanceInput& Input)
	{
		FRbCueClearanceResult Result;
		Result.MinElevation = Input.Elevation; // TODO(UE-4)
		return Result;
	}

	double MinElevationForBall(const rb::Vec3& /*ContactPoint*/, double /*Azimuth*/, const rb::Vec3& /*Obstacle*/, double /*ObstacleRadius*/,
		const rb::human::CueBodyState& /*Body*/, double /*CueLength*/, double /*Margin*/)
	{
		return 0.0; // TODO(UE-4)
	}

	bool SweepEnvironment(const UWorld* /*World*/, const ARbTable& /*Table*/, const FRbCueClearanceInput& /*Input*/, double /*Elevation*/,
		const rb::human::CueBodyState& /*Body*/)
	{
		return true; // TODO(UE-4)
	}
}
