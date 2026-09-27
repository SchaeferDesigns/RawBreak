#pragma once

// Playback of a simulated shot: exact state at any time from the piecewise-analytic segments, ball
// orientation, monotone cursors for per-frame evaluation, cue tip positions, fixed-rate sampling
// (rbsim). ue5-realism-plan 5.7. Owner: WP-7 (output, playback & tools).
//
// ORIENTATION LAW (single definition, used by the simulator for TrajectorySegment::Orientation0 of the
// next segment AND by playback, so there is no jump at segment boundaries):
//  * The rotation vector of an interval [a, b] of a segment is the EXACT integral of the segment's own
//    angular-velocity laws (the ones EvaluateTrajectorySegment returns): Sliding w_h = Omega0_h + OmegaDotH tau;
//    Rolling w_h = z_hat x v(tau) / R (integral z_hat x (r(b) - r(a)) / R, the piece's velocity for a tilt chain
//    piece); w_z = OmegaZAt (linear decay clamped at OmegaZStopTau); Airborne / PocketFall / PocketPivot w = Omega0;
//    Sampled w = Motion.Omega0; Stationary, Pocketed, OffTable and Terminal segments do not rotate. Local time is
//    clamped to [0, TauEnd] like EvaluateSegment (Sampled: [0, T1 - T0]).
//  * Constant rotation axis -> exact closed form q(tau) = Exp(0.5 Theta(0, tau)) (x) q0: Stationary, Terminal,
//    Spinning (axis z), Airborne / PocketFall / PocketPivot (w constant), Sampled, Rolling with w_z = 0 on the whole
//    segment (Omega0.z == 0 and no spin rate) and !Motion.Tilt.Active (axis z_hat x v_hat, angle = distance / R).
//    The Rolling test is STRUCTURAL: a level Rolling segment's Accel2 is parallel to Vel0 by construction, and a
//    floating-point parallelism test (cross product == 0) would misclassify some level segments and change level
//    orientations bitwise.
//  * Otherwise (Sliding; Rolling with w_z != 0; every Rolling tilt chain piece (Motion.Tilt.Active),
//    human-factors 4.5.3, whose Accel2 is in general not parallel to Vel0): fixed grid tau_k = k Substep from the
//    segment start (Substep <= 1 ms, UE 5.7): q_{k+1} = Exp(0.5 Theta(tau_k, tau_k+1)) (x) q_k, renormalised;
//    q(tau) = Exp(0.5 Theta(tau_K, tau)) (x) q_K with K = floor(tau / Substep) (final partial step; K is lowered by
//    one if rounding would put tau_K after tau). A level Rolling segment whose spin stops inside it (w_z != 0 at the
//    start, OmegaZStopTau finite, no tilt) has a constant axis from the first grid point K0 at or after OmegaZStopTau
//    on: the grid stops at K0 and q(tau) = Exp(0.5 Theta(tau_K0, tau)) (x) q_K0 for tau >= tau_K0 (exact, so the
//    law stays exact while a long roll after the spin has died costs O(1) instead of O(duration / Substep)).
//  * An interval whose rotation vector is exactly zero leaves q unchanged bitwise (no renormalisation), so
//    SegmentOrientationAt(q0, S, 0) == q0 and a resting ball keeps its orientation bit for bit.
//  * Exp(0.5 Theta) is evaluated by the Taylor series of cos(a/2) and sin(a/2)/a for |Theta| < 0.25 rad (every grid
//    step below 250 rad/s; the omitted terms are < 1e-18, so it equals the Sin / Cos form to rounding, without
//    transcendental calls) and by FromRotationVector (rb/Math/Quat.h) above.
//  * Next Orientation0 = SegmentOrientationAt(prev.Orientation0, prev, prev.T1 - prev.Motion.T0): bitwise
//    equal to what playback shows at T1 (OrientationAt / StateAtCursor evaluate tau = T - T0 with the same doubles).
// Integration uses world-frame w with LEFT multiplication (rb/Math/Quat.h). Unreal converts q with
// q_UE = (qw, -qx, qy, -qz) in its own adapter.
//
// Cost: O(1) for constant-axis segments, O(tau / Substep) grid steps (about 20 ns each, Release) otherwise; the
// cursor makes per-frame playback O(frame interval / Substep) on grid segments and O(1) elsewhere.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Math/Quat.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ShotResult.h"

namespace rb
{
	inline constexpr double kOrientationSubstep = 1.0e-3; // [s] grid step of the orientation law

	// State of one segment at absolute time T (clamped to [T0, T1]). Analytic: EvaluateSegment(Motion, T - T0);
	// Sampled: linear from Motion.Pos0 at T0 to EndPosition at T1 (velocity = chord / duration, w = Motion.Omega0);
	// Terminal: frozen at Motion.Pos0 with zero velocity and spin. State = Motion.State.
	RB_API BallState EvaluateTrajectorySegment(const TrajectorySegment& Segment, double T);

	// True if the segment's rotation axis is constant (closed-form orientation, see the file comment).
	RB_API bool HasConstantRotationAxis(const TrajectorySegment& Segment);

	// Orientation at local time Tau of the segment, starting from Q0 at Tau = 0 (the orientation law).
	// O(1) for constant-axis segments, O(Tau / Substep) otherwise. Substep <= 0 (or not finite) uses kOrientationSubstep.
	RB_API Quat SegmentOrientationAt(const Quat& Q0, const TrajectorySegment& Segment, double Tau, double Substep = kOrientationSubstep);

	// Constant-omega integration with fixed sub-steps (ue5-realism-plan T17/T18): Steps times
	// q = Exp(0.5 Omega Dt) (x) q, renormalised.
	RB_API Quat IntegrateOrientationSteps(const Quat& Q0, const Vec3& Omega, double Dt, int Steps);

	// Random access (binary search in the ball's track: the last segment with Motion.T0 <= T, so a boundary time
	// belongs to the segment that starts there; times before the track clamp to its start, after it to its end);
	// false if the ball has no recorded track.
	RB_API bool StateAt(const ShotResult& Result, int Ball, double T, BallState& Out);
	RB_API bool OrientationAt(const ShotResult& Result, int Ball, double T, Quat& Out);

	// Per-frame playback with monotonically increasing T: O(1) amortised per ball (prior-art P6 <= 0.2 us)
	// because the cursor caches, per ball, the segment and the last orientation grid point reached; a
	// frame then integrates at most the grid steps since the previous frame plus one partial step.
	// Results are bitwise equal to StateAt / OrientationAt. A backward jump in T re-seeks that ball.
	// One cursor belongs to one ShotResult: ResetCursor before playing another result (or after the result changed).
	struct PlaybackCursor
	{
		int Segment[kMaxBalls] = {};
		int GridIndex[kMaxBalls] = {};    // grid point K reached in the current segment (grid segments only)
		Quat GridOrientation[kMaxBalls];  // orientation at that grid point
		double LastTime[kMaxBalls] = {};
		bool Valid[kMaxBalls] = {};       // false: the ball is (re-)seeked by binary search on its next evaluation
	};

	RB_API void ResetCursor(PlaybackCursor& Cursor);
	RB_API bool StateAtCursor(const ShotResult& Result, PlaybackCursor& Cursor, int Ball, double T, BallState& OutState, Quat& OutOrientation);

	// Cue tip dome center and cue direction of a strike at time T (from ShotResult::CueTips): the cue the
	// renderer animates is the one whose re-contacts the rules judged. The piece is the last one of the strike with
	// Path.StartTime <= T (the first one before it). Analytic pieces follow CueTipAsSegment (uniform deceleration,
	// at rest after Path.StopTime; a piece replaced at T1 is evaluated up to T1); Sampled pieces interpolate linearly
	// from Path.Start at Path.StartTime to EndPosition at T1. Direction = Path.Direction (butt -> tip). False if the
	// strike has no path (tips are recorded only with RecordOptions::Trajectories).
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

	// Samples [0, Result.StopTime] every Dt (t_k = k Dt; Dt <= 0: no regular samples) plus every segment boundary
	// inside the interval and StopTime itself (so corners and the final rest are exact), in increasing time without
	// duplicates; a boundary sample shows the segment that starts there. Returns the number written, or -1 if
	// Capacity is too small (0 if the ball has no track).
	RB_API int SampleTrajectory(const ShotResult& Result, int Ball, double Dt, TrajectorySample* Out, int Capacity);
}
