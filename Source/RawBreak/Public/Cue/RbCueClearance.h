#pragma once

// Cue clearance and minimum elevation (ue5-realism-plan 5.5, test T13; feeds rb::human::StrokeSituation::
// ElevationFloor / FloorBy / FloorBall). Analytic part (balls, cushions, rail caps) is pure core-frame math on the
// table context; the environment part (walls, lamp, furniture) is a UE capsule-chain sweep. Owner: UE-4.

#include "CoreMinimal.h"

#include "Simulation/RbTableContext.h"

#include "rb/Human/CueState.h"
#include "rb/Human/Skill.h"
#include "rb/Math/Vec3.h"

class ARbTable;
class UWorld;

struct FRbCueClearanceInput
{
	rb::Vec3 ContactPoint;       // P on the cue-ball surface [m] (table frame)
	double Azimuth = 0.0;        // phi [rad]
	double Elevation = 0.0;      // theta [rad] desired
	double CueLength = 1.4732;   // [m]
	double Backswing = 0.25;     // [m] max practice-stroke length included in the sweep
	double Margin = 0.001;       // [m]
	int32 CueBall = 0;
	const rb::Vec3* BallPositions = nullptr; // index = ball id, only balls with InPlay[i]
	const bool* InPlay = nullptr;
	int32 BallCount = 0;
};

struct FRbCueClearanceResult
{
	double MinElevation = 0.0;   // [rad] lowest clear elevation >= the desired one (0.01 deg resolution)
	rb::human::FloorSource FloorBy = rb::human::FloorSource::None;
	int32 FloorBall = -1;
	bool bBlockedByEnvironment = false; // no elevation up to MaxElevation is clear of the environment
};

namespace RbCueClearance
{
	// Analytic clearance vs balls (closest axis point, tapered radius r(s)) and vs cushion / rail heights.
	RAWBREAK_API FRbCueClearanceResult ComputeMinElevation(const FRbTableContext& Table, const rb::human::CueBodyState& Body,
		const FRbCueClearanceInput& Input);

	// Minimum theta for a single obstacle ball (T13 closed form by fixed-point iteration).
	RAWBREAK_API double MinElevationForBall(const rb::Vec3& ContactPoint, double Azimuth, const rb::Vec3& Obstacle, double ObstacleRadius,
		const rb::human::CueBodyState& Body, double CueLength, double Margin);

	// Environment sweep (capsule chain along the cue incl. backswing) in the world of Table. True = clear.
	RAWBREAK_API bool SweepEnvironment(const UWorld* World, const ARbTable& Table, const FRbCueClearanceInput& Input, double Elevation,
		const rb::human::CueBodyState& Body);
}
