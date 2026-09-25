#include "rb/Core/FpGuard.h"
// Owner: WP-7 (output, playback & tools). Spec: ue5-realism-plan 5.7; prior-art 7.4 (P6); Docs/architecture.md 8.10.
#include "rb/Physics/Playback.h"

namespace rb
{
	BallState EvaluateTrajectorySegment(const TrajectorySegment& Segment, double /*T*/)
	{
		// TODO(WP-7): Analytic -> EvaluateSegment(Motion, T - T0); Sampled -> linear interpolation; Terminal -> frozen.
		BallState S;
		S.Position = Segment.Motion.Pos0;
		S.State = Segment.Motion.State;
		return S;
	}

	bool HasConstantRotationAxis(const TrajectorySegment& /*Segment*/)
	{
		// TODO(WP-7): Stationary, Terminal, Spinning, Airborne, PocketFall, Sampled, Rolling with w_z == 0 over the segment.
		return false;
	}

	Quat SegmentOrientationAt(const Quat& Q0, const TrajectorySegment& /*Segment*/, double /*Tau*/, double /*Substep*/)
	{
		// TODO(WP-7): the orientation law of Playback.h (closed form or fixed grid from the segment start).
		return Q0;
	}

	Quat IntegrateOrientationSteps(const Quat& Q0, const Vec3& /*Omega*/, double /*Dt*/, int /*Steps*/)
	{
		// TODO(WP-7): fixed-step constant-omega integration (UE plan T17, T18).
		return Q0;
	}

	bool StateAt(const ShotResult& /*Result*/, int /*Ball*/, double /*T*/, BallState& /*Out*/)
	{
		// TODO(WP-7): binary search over Tracks[Ball].Segments.
		return false;
	}

	bool OrientationAt(const ShotResult& /*Result*/, int /*Ball*/, double /*T*/, Quat& /*Out*/)
	{
		// TODO(WP-7): segment Orientation0 + SegmentOrientationAt.
		return false;
	}

	void ResetCursor(PlaybackCursor& Cursor)
	{
		Cursor = PlaybackCursor{};
	}

	bool StateAtCursor(const ShotResult& /*Result*/, PlaybackCursor& /*Cursor*/, int /*Ball*/, double /*T*/, BallState& /*OutState*/, Quat& /*OutOrientation*/)
	{
		// TODO(WP-7): monotone cursor advance with the cached grid orientation, O(1) amortised, bitwise equal to OrientationAt.
		return false;
	}

	bool CueTipAt(const ShotResult& /*Result*/, int /*Strike*/, double /*T*/, Vec3& /*TipCenter*/, Vec3& /*Direction*/)
	{
		// TODO(WP-7): evaluate ShotResult::CueTips (analytic / sampled pieces).
		return false;
	}

	int SampleTrajectory(const ShotResult& /*Result*/, int /*Ball*/, double /*Dt*/, TrajectorySample* /*Out*/, int /*Capacity*/)
	{
		// TODO(WP-7): fixed-rate samples plus segment boundaries.
		return 0;
	}
}
