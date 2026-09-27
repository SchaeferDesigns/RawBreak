#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.3 (HF-40). The cling itself is WP-3 (rb/Physics/BallBall.h).
#include "rb/Human/BallMarks.h"

#include "rb/Core/Ids.h"
#include "rb/Math/Scalar.h"

namespace rb::human
{
	namespace
	{
		// Path length of r(tau) = Pos0 + Vel0 tau + Accel2 tau^2 over [TauA, TauB]: composite Simpson on |v(tau)|, which is
		// smooth on each side of its minimum (the only kink, where v passes through 0).
		double SimpsonSpeed(const Vec3& Vel0, const Vec3& Accel, double TauA, double TauB)
		{
			constexpr int kPanels = 32; // even
			const double H = (TauB - TauA) / static_cast<double>(kPanels);
			if (!(H > 0.0))
			{
				return 0.0;
			}
			double Sum = 0.0;
			for (int i = 0; i <= kPanels; ++i)
			{
				const double Tau = TauA + H * static_cast<double>(i);
				const double Speed = Length(Vel0 + Accel * Tau);
				const double Weight = (i == 0 || i == kPanels) ? 1.0 : ((i & 1) ? 4.0 : 2.0);
				Sum += Weight * Speed;
			}
			return Sum * H / 3.0;
		}

		double QuadraticPathLength(const Vec3& Vel0, const Vec3& Accel2, double Duration)
		{
			if (!(Duration > 0.0) || !IsFinite(Duration))
			{
				return 0.0;
			}
			const Vec3 Accel = Accel2 * 2.0; // v(tau) = Vel0 + 2 Accel2 tau
			const double AccelSquared = LengthSquared(Accel);
			if (AccelSquared == 0.0)
			{
				return Length(Vel0) * Duration;
			}
			const double TauMin = -Dot(Vel0, Accel) / AccelSquared;
			if (TauMin > 0.0 && TauMin < Duration)
			{
				return SimpsonSpeed(Vel0, Accel, 0.0, TauMin) + SimpsonSpeed(Vel0, Accel, TauMin, Duration);
			}
			return SimpsonSpeed(Vel0, Accel, 0.0, Duration);
		}
	}

	void DepositChalkMark(BallChalkMarks& Marks, const Quat& Orientation, const Vec3& ContactDir, double TipCoverage, bool Miscue, const MarkParams& Params)
	{
		ChalkMark Mark;
		Mark.BodyDir = Normalized(Rotate(Conjugate(Orientation), ContactDir));
		Mark.Strength = Miscue ? 1.0 : Clamp(Params.BaseStrength + Params.CoverageStrength * Clamp(TipCoverage, 0.0, 1.0), 0.0, 1.0);
		Mark.Radius = Miscue ? Params.MiscueRadius : Params.Radius;
		if (Marks.IsFull())
		{
			// Replace the weakest (ties: the oldest, i.e. the lowest index); the list stays in deposit order.
			int Weakest = 0;
			for (int i = 1; i < Marks.Size(); ++i)
			{
				if (Marks[i].Strength < Marks[Weakest].Strength)
				{
					Weakest = i;
				}
			}
			Marks.RemoveAt(Weakest);
		}
		Marks.PushBack(Mark);
	}

	void FadeChalkMarks(BallChalkMarks& Marks, double SlideDistance, double RollDistance, const MarkParams& Params)
	{
		const double Factor = Exp(-Max(0.0, SlideDistance) / Params.SlideFadeLength - Max(0.0, RollDistance) / Params.RollFadeLength);
		for (int i = 0; i < Marks.Size();)
		{
			Marks[i].Strength *= Factor;
			if (Marks[i].Strength < Params.DropBelow)
			{
				Marks.RemoveAt(i);
			}
			else
			{
				++i;
			}
		}
	}

	TravelDistances ComputeTravelDistances(const ShotResult& Result, int Ball)
	{
		TravelDistances Distances;
		if (Ball < 0 || Ball >= kMaxBalls)
		{
			return Distances;
		}
		for (const TrajectorySegment& Segment : Result.Tracks[Ball].Segments)
		{
			const double Duration = Segment.T1 - Segment.Motion.T0;
			if (Segment.Kind == SegmentKind::Terminal || !(Duration > 0.0))
			{
				continue;
			}
			if (Segment.Kind == SegmentKind::Sampled)
			{
				Distances.Slide += Length(Segment.EndPosition - Segment.Motion.Pos0); // island / pivot pieces count as sliding
				continue;
			}
			if (Segment.Motion.State == MotionState::Sliding)
			{
				Distances.Slide += QuadraticPathLength(Segment.Motion.Vel0, Segment.Motion.Accel2, Duration);
			}
			else if (Segment.Motion.State == MotionState::Rolling)
			{
				Distances.Roll += QuadraticPathLength(Segment.Motion.Vel0, Segment.Motion.Accel2, Duration);
			}
		}
		return Distances;
	}
}
