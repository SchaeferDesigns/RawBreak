#pragma once

// Cue clearance and minimum elevation (ue5-realism-plan 5.5, test T13; feeds rb::human::StrokeSituation::
// ElevationFloor / FloorBy / FloorBall). Analytic part (balls, cushions, rail caps) is pure core-frame math on the
// table context; the environment part (walls, lamp, furniture) is a UE capsule-chain sweep. Owner: UE-4.
//
// Cue pose at a trial elevation theta (the core's executed pose, human-factors 3.6 / rb::human::ExecuteStroke):
//  * the contact offsets (a, b) of ContactPoint are kept in the cue frame (rb::MakeCueFrame(theta, phi)) when theta changes:
//    q(theta) = a e_r + b e_u - c d, the contact point rises over the ball with the butt (a centre hit stays a centre hit);
//  * the axis runs through the tip DOME CENTRE C + (R_cb + r_dome) q (not through the contact point: off-centre hits put
//    the axis r_dome |(a, b)| further out), the body starts at the tip rim RimDepth = sqrt(r_dome^2 - (w_tip / 2)^2) ahead
//    of it and runs back along -d with r(s) = r_t + (r_b - r_t) s / L (CueBodyState taper);
//  * backswing: the cue slides back along its own axis by up to Backswing, so the swept body is the same line out to
//    s = L + Backswing with the envelope radius r(min(s, L)).
// Clearance tests (margin = Input.Margin):
//  * ball j: min over s of |X(s) - C_j| - r_env(s) >= R_j + margin (exact minimum for the linear taper, the plan's
//    closest-axis-point test refined; ball radii from the table context);
//  * rails: over every rail-top plane of rb::TableGeometry::RailTops (cushion tops and caps, pocket cut discs excluded) the
//    underside of the cue clears the plane by the margin everywhere over the polygon: the underside over a plan point at
//    lateral offset y from the axis is z_axis(s) - sqrt(r_env(s)^2 - y^2) / cos(theta) (plan 5.5's z(s) - r(s) / cos(theta)
//    at y = 0), minimised exactly over the part of the cue's plan footprint that lies over the polygon (so the side of a cue
//    crossing a rail at a shallow angle counts while the axis is still over the bed); pocket openings and the pocket holes
//    carry no rail;
//  * minimum elevation: coarse sweep of theta in 0.25 deg steps from the requested elevation upward, then bisection to
//    0.01 deg on the first clear interval (plan 5.5); the result is the clear end of the final bracket.
//  * environment: capsule chain along the swept body (tip to butt incl. the backswing), overlap query on the RbCueSweep trace
//    channel (RbAssetPaths::Collision::CueSweepChannel, M2-F; M1 used ECC_WorldDynamic) against blocking geometry: walls,
//    columns and furniture (RbVenueBlock, BlockAll) block, venue clutter (RbVenueProp), loose balls and pawns ignore the channel;
//    the table actor (its rails are analytic), pawns (no body in M1), balls and cues are skipped as before.

#include "CoreMinimal.h"

#include "Simulation/RbTableContext.h"

#include "rb/Human/CueState.h"
#include "rb/Human/Skill.h"
#include "rb/Math/Vec3.h"

class ARbTable;
class UWorld;

struct FRbCueClearanceInput
{
	rb::Vec3 ContactPoint;       // P on the cue-ball surface [m] (table frame), in the cue frame of ContactElevation
	double Azimuth = 0.0;        // phi [rad]
	double Elevation = 0.0;      // theta [rad] desired
	double CueLength = 1.4732;   // [m]
	double Backswing = 0.25;     // [m] max practice-stroke length included in the sweep
	double Margin = 0.001;       // [m]
	int32 CueBall = 0;
	const rb::Vec3* BallPositions = nullptr; // index = ball id, only balls with InPlay[i]
	const bool* InPlay = nullptr;
	int32 BallCount = 0;

	// --- additions (UE-4) ------------------------------------------------------------------------------
	// Elevation of the cue frame ContactPoint was computed in (the offsets (a, b) are read in that frame); < 0 = Elevation.
	// A caller that searches from Elevation = 0 (the absolute floor) with a contact point computed at its aim elevation MUST
	// pass that aim elevation here: otherwise a raised aim's centre hit reads as a top hit and the floor drops below an
	// obstacle ball (15.3 instead of 21.4 deg for a 15 deg aim over a ball 0.10 m behind).
	double ContactElevation = -1.0;
	double TipDomeRadius = 0.0106; // [m] r_dome (rb::human::TipState::DomeRadius)
	double TipWidth = 0.01275;     // [m] w_tip (rb::human::TipState::Width)
	double MaxElevation = 85.0 * UE_DOUBLE_PI / 180.0; // [rad] search limit
};

struct FRbCueClearanceResult
{
	double MinElevation = 0.0;   // [rad] lowest clear elevation >= the desired one (0.01 deg resolution)
	rb::human::FloorSource FloorBy = rb::human::FloorSource::None;
	int32 FloorBall = -1;
	bool bBlockedByEnvironment = false; // no elevation up to MaxElevation is clear of the environment

	// --- additions (UE-4) ------------------------------------------------------------------------------
	bool bBlocked = false;              // no elevation up to MaxElevation clears the balls / rails (MinElevation = MaxElevation)
	bool bRaisedByEnvironment = false;  // the environment sweep set MinElevation (FloorBy None)
};

// Clearance of the cue at one elevation: the smallest gap [m] (distance minus radii, taper and margin; < 0 = contact) and
// what sets it.
struct FRbCueGap
{
	double Gap = TNumericLimits<double>::Max();
	rb::human::FloorSource By = rb::human::FloorSource::None;
	int32 Ball = -1;
	double S = 0.0; // [m] body coordinate behind the tip rim where the gap is smallest
};

// The cue body at one elevation (core frame, metres): the axis of the swept body.
struct FRbCuePose
{
	rb::Vec3 DomeCenter;
	rb::Vec3 Rim;         // s = 0 of the body
	rb::Vec3 Direction;   // d, butt -> tip (unit)
	rb::Vec3 CueBallCenter;
	double CueBallRadius = 0.0;
	double OffsetA = 0.0; // contact offsets kept in the cue frame
	double OffsetB = 0.0;
};

namespace RbCueClearance
{
	// Analytic clearance vs balls (closest axis point, tapered radius r(s)) and vs cushion / rail heights.
	RAWBREAK_API FRbCueClearanceResult ComputeMinElevation(const FRbTableContext& Table, const rb::human::CueBodyState& Body,
		const FRbCueClearanceInput& Input);

	// Minimum theta >= Input.Elevation for a single obstacle ball (T13; bisection to 1e-9 rad): the cue pose of Input (cue
	// ball = BallPositions[CueBall] when given, else a centre-ball hit on a standard ball through ContactPoint), no rails.
	// Returns Input.MaxElevation when nothing up to it clears the ball.
	RAWBREAK_API double MinElevationForBall(const FRbCueClearanceInput& Input, const rb::Vec3& Obstacle, double ObstacleRadius,
		const rb::human::CueBodyState& Body);

	// Environment sweep (capsule chain along the cue incl. backswing) in the world of Table. True = clear.
	RAWBREAK_API bool SweepEnvironment(const UWorld* World, const ARbTable& Table, const FRbCueClearanceInput& Input, double Elevation,
		const rb::human::CueBodyState& Body);

	// --- additions (UE-4) ------------------------------------------------------------------------------

	// Analytic floor, then raised until the environment sweep is clear too (1 deg coarse steps, bisection to 0.01 deg on the
	// combined test). bBlockedByEnvironment: nothing up to MaxElevation is clear (MinElevation = MaxElevation).
	RAWBREAK_API FRbCueClearanceResult ComputeMinElevationWithEnvironment(const UWorld* World, const ARbTable& Table,
		const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input);

	// Short-cue situation (plan 5.5, equipment 8.2 / 8.3, HF-32): when the environment forces the cue above
	// MaxPlayableElevation (or blocks it), the longest bar short cue (52 / 48 / 36 in) shorter than Input.CueLength whose
	// combined floor is at most MaxPlayableElevation; 0 when the cue of Input is playable or no short cue helps.
	RAWBREAK_API double FindShortCueLength(const UWorld* World, const ARbTable& Table, const rb::human::CueBodyState& Body,
		const FRbCueClearanceInput& Input, double MaxPlayableElevation);

	// The swept body at Elevation (pose convention above). BallRadius = the cue ball's radius (used when BallPositions
	// has no cue ball: a centre-ball hit through ContactPoint).
	RAWBREAK_API FRbCuePose MakePose(const FRbCueClearanceInput& Input, double Elevation, double CueBallRadius);

	// Smallest gap of the pose at Elevation vs the balls and / or the rails of Table (Table may be null: balls only, with the
	// standard radius). Ball radii from Table.
	RAWBREAK_API FRbCueGap EvaluateGap(const FRbTableContext* Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input,
		double Elevation, bool bBalls = true, bool bRails = true);

	// Gap [m] of one ball against the swept body of Pose (exact minimum over s of the tapered, backswing-extended body).
	RAWBREAK_API double BallGap(const FRbCuePose& Pose, const rb::human::CueBodyState& Body, double CueLength, double Backswing,
		const rb::Vec3& Ball, double BallRadius, double* OutS = nullptr);

	// Radius of the swept body at s [m] behind the rim: r(min(s, L)) (0 <= s).
	RAWBREAK_API double EnvelopeRadius(const rb::human::CueBodyState& Body, double CueLength, double S);
}
