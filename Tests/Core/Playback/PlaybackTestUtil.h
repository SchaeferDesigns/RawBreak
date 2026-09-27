#pragma once

// Shared helpers of the WP-7 tests (Tests/Core/Playback, Tests/Core/ShotRecord): hand-built ShotResults whose tracks are
// chained exactly like the simulator chains them (architecture.md 8.10: the next Orientation0 is the orientation law of
// the closed segment at its end), bitwise comparisons and a brute-force reference integrator for the orientation.

#include "rbtest.h"

#include "rb/Core/Constants.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Quat.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Playback.h"
#include "rb/Physics/ShotResult.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace playtest
{
	// Motion-spec parameters (architecture.md 18: m = 0.170, g = 9.80665).
	inline constexpr double kG = 9.80665;
	inline constexpr double kR = 0.028575;
	inline constexpr double kM = 0.170;

	inline rb::BallSpec Spec() { return rb::MakeBallSpec(kR, kM); }
	inline rb::ClothParams Cloth() { return {0.20, 0.010, 10.0}; }

	inline std::uint64_t Bits(double X) { return std::bit_cast<std::uint64_t>(X); }
	inline bool SameBits(double A, double B) { return Bits(A) == Bits(B); }
	inline bool SameBits(const rb::Vec3& A, const rb::Vec3& B) { return SameBits(A.x, B.x) && SameBits(A.y, B.y) && SameBits(A.z, B.z); }
	inline bool SameBits(const rb::Quat& A, const rb::Quat& B) { return SameBits(A.w, B.w) && SameBits(A.x, B.x) && SameBits(A.y, B.y) && SameBits(A.z, B.z); }
	inline bool SameBits(const rb::BallState& A, const rb::BallState& B)
	{
		return A.State == B.State && SameBits(A.Position, B.Position) && SameBits(A.Velocity, B.Velocity) && SameBits(A.Omega, B.Omega);
	}

	// Rotation angle between two unit quaternions [rad] (q and -q are the same rotation).
	inline double AngleBetween(const rb::Quat& A, const rb::Quat& B)
	{
		const double D = std::fabs(A.w * B.w + A.x * B.x + A.y * B.y + A.z * B.z);
		return 2.0 * std::acos(D > 1.0 ? 1.0 : D);
	}

	inline rb::BallState SurfaceState(const rb::Vec3& Position, const rb::Vec3& Velocity, const rb::Vec3& Omega)
	{
		rb::BallState S;
		S.Position = Position;
		S.Velocity = Velocity;
		S.Omega = Omega;
		const rb::NumericsConfig Numerics;
		rb::ClassifyState(S, kR, 0.0, Numerics);
		return S;
	}

	// Appends a segment the way the simulator records one (architecture.md 8.10): the open segment is closed at the new
	// segment's T0 and the new Orientation0 is the orientation law of the closed segment at its end. The first segment
	// keeps the Orientation0 it carries.
	inline void AppendSegment(rb::BallTrack& Track, const rb::TrajectorySegment& Segment)
	{
		rb::TrajectorySegment Next = Segment;
		if (!Track.Segments.empty())
		{
			rb::TrajectorySegment& Prev = Track.Segments.back();
			Prev.T1 = Next.Motion.T0;
			Next.Orientation0 = rb::SegmentOrientationAt(Prev.Orientation0, Prev, Prev.T1 - Prev.Motion.T0);
		}
		Track.Segments.push_back(Next);
	}

	inline rb::TrajectorySegment AnalyticSegment(const rb::MotionSegment& Motion)
	{
		rb::TrajectorySegment S;
		S.Kind = rb::SegmentKind::Analytic;
		S.Motion = Motion;
		S.T1 = rb::kInfinity;
		return S;
	}

	// One ball on an empty (level or tilted) table from state S at T0 until it rests; segments / tilt chain pieces follow
	// SegmentEndState exactly like the event loop's Transition and TiltRefresh processing. Returns the rest time.
	inline double AppendRollOut(rb::BallTrack& Track, rb::BallState S, double T0, const rb::Quat& Q0, const rb::TiltParams& Tilt = {},
		int MaxSegments = 400)
	{
		const rb::NumericsConfig Numerics;
		double T = T0;
		for (int i = 0; i < MaxSegments; ++i)
		{
			rb::TrajectorySegment Seg = AnalyticSegment(rb::MakeSegment(S, T, Spec(), Cloth(), 0.0, kG, Tilt));
			if (Track.Segments.empty())
			{
				Seg.Orientation0 = Q0;
			}
			AppendSegment(Track, Seg);
			if (!(Seg.Motion.TauEnd < rb::kInfinity))
			{
				break;
			}
			S = rb::SegmentEndState(Seg.Motion, Numerics);
			T = T + Seg.Motion.TauEnd;
		}
		return T;
	}

	// Brute-force reference for the orientation of a segment: midpoint rule on the segment's w laws with N sub-steps
	// (w from EvaluateTrajectorySegment, the same laws the orientation law integrates exactly per step).
	inline rb::Quat ReferenceOrientation(const rb::Quat& Q0, const rb::TrajectorySegment& S, double Tau, int N)
	{
		rb::Quat Q = Q0;
		const double H = Tau / static_cast<double>(N);
		for (int k = 0; k < N; ++k)
		{
			const double Mid = S.Motion.T0 + (static_cast<double>(k) + 0.5) * H;
			const rb::BallState State = rb::EvaluateTrajectorySegment(S, Mid);
			Q = rb::IntegrateConstantOmega(Q, State.Omega, H);
		}
		return Q;
	}
}
