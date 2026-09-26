#pragma once

// Playback of a simulated shot: exact state at any time from the piecewise-analytic segments, ball
// orientation, monotone cursors for per-frame evaluation, cue tip positions, fixed-rate sampling
// (rbsim). ue5-realism-plan 5.7. Owner: WP-7 (output, playback & tools).
//
// ORIENTATION LAW (single definition, used by the simulator for TrajectorySegment::Orientation0 of the
// next segment AND by playback, so there is no jump at segment boundaries):
//  * Constant rotation axis -> exact closed form q(tau) = Exp(0.5 Theta(tau)) (x) q0, Theta = integral
//    of w over [0, tau]: Stationary, Terminal (Theta = 0), Spinning (axis z), Airborne / PocketFall
//    (w constant), Sampled (w = Motion.Omega0 constant), Rolling with w_z = 0 on the whole segment and
//    !Motion.Tilt.Active (axis z_hat x v_hat, angle = distance / R). The Rolling test is STRUCTURAL: a level
//    Rolling segment's Accel2 is parallel to Vel0 by construction, and a floating-point parallelism test
//    (cross product == 0) would misclassify some level segments and change level orientations bitwise.
//  * Otherwise (Sliding; Rolling with w_z != 0; every Rolling tilt chain piece (Motion.Tilt.Active),
//    human-factors 4.5.3, whose Accel2 is in general not parallel to Vel0): fixed grid tau_k = k Substep from the segment start
//    (Substep <= 1 ms, UE 5.7): q_{k+1} = Exp(0.5 Theta_k) (x) q_k with Theta_k = the EXACT integral of
//    the piecewise-linear w laws over [tau_k, tau_k+1], renormalised; q(tau) = Exp(0.5 integral of w
//    over [tau_K, tau]) (x) q_K, K = floor(tau / Substep) (final partial step).
//  * Next Orientation0 = SegmentOrientationAt(prev.Orientation0, prev, prev.T1 - prev.Motion.T0): bitwise
//    equal to what playback shows at T1.
// Integration uses world-frame w with LEFT multiplication (rb/Math/Quat.h). Unreal converts q with
// q_UE = (qw, -qx, qy, -qz) in its own adapter.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Math/Quat.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ShotResult.h"

namespace rb
{
	inline constexpr double kOrientationSubstep = 1.0e-3; // [s] grid step of the orientation law

	// State of one segment at absolute time T (clamped to [T0, T1]).
	RB_API BallState EvaluateTrajectorySegment(const TrajectorySegment& Segment, double T);

	// True if the segment's rotation axis is constant (closed-form orientation, see the file comment).
	RB_API bool HasConstantRotationAxis(const TrajectorySegment& Segment);

	// Orientation at local time Tau of the segment, starting from Q0 at Tau = 0 (the orientation law).
	// O(1) for constant-axis segments, O(Tau / Substep) otherwise.
	RB_API Quat SegmentOrientationAt(const Quat& Q0, const TrajectorySegment& Segment, double Tau, double Substep = kOrientationSubstep);

	// Constant-omega integration with fixed sub-steps (ue5-realism-plan T17/T18): Steps times
	// q = Exp(0.5 Omega Dt) (x) q, renormalised.
	RB_API Quat IntegrateOrientationSteps(const Quat& Q0, const Vec3& Omega, double Dt, int Steps);

	// Random access (binary search in the ball's track); false if the ball has no recorded track.
	RB_API bool StateAt(const ShotResult& Result, int Ball, double T, BallState& Out);
	RB_API bool OrientationAt(const ShotResult& Result, int Ball, double T, Quat& Out);

	// Per-frame playback with monotonically increasing T: O(1) amortised per ball (prior-art P6 <= 0.2 us)
	// because the cursor caches, per ball, the segment and the last orientation grid point reached; a
	// frame then integrates at most the grid steps since the previous frame plus one partial step.
	// Results are bitwise equal to StateAt / OrientationAt. A backward jump in T re-seeks that ball.
	struct PlaybackCursor
	{
		int Segment[kMaxBalls] = {};
		int GridIndex[kMaxBalls] = {};    // grid point K reached in the current segment (grid segments only)
		Quat GridOrientation[kMaxBalls];  // orientation at that grid point
		double LastTime[kMaxBalls] = {};
	};

	RB_API void ResetCursor(PlaybackCursor& Cursor);
	RB_API bool StateAtCursor(const ShotResult& Result, PlaybackCursor& Cursor, int Ball, double T, BallState& OutState, Quat& OutOrientation);

	// Cue tip dome center and cue direction of a strike at time T (from ShotResult::CueTips): the cue the
	// renderer animates is the one whose re-contacts the rules judged. False if the strike has no path
	// (tips are recorded only with RecordOptions::Trajectories).
	RB_API bool CueTipAt(const ShotResult& Result, int Strike, double T, Vec3& TipCenter, Vec3& Direction);

	struct TrajectorySample
	{
		double Time = 0.0;
		Vec3 Position;
		Vec3 Velocity;
		Vec3 Omega;
		Quat Orientation;
		MotionState State = MotionState::Stationary;
	};

	// Samples [0, Result.StopTime] every Dt plus every segment boundary (so corners are exact).
	// Returns the number written, or -1 if Capacity is too small.
	RB_API int SampleTrajectory(const ShotResult& Result, int Ball, double Dt, TrajectorySample* Out, int Capacity);
}
