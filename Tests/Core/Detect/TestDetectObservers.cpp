// Owner: WP-5 (event detection). Rules observers (architecture 8.7; rules 2.2 line crossings, F9 jump-over):
// A-DET-4 (PredictPlanDistanceCrossing for a jump over a ball) and A-DET-5 (PredictLineCrossings with IncludeFrom).

#include "rbtest.h"

#include "DetectTestUtil.h"

using namespace detecttest;
using rb::ContactPrediction;
using rb::LineCrossing;
using rb::MotionSegment;
using rb::TableLine;

namespace
{
	rb::TableLandmarks NineFoot()
	{
		rb::TableLandmarks L;
		L.HeadStringX = -0.635;
		L.FootStringX = 0.635;
		L.CenterStringX = 0.0;
		L.LongStringY = 0.0;
		L.BaulkX = -1.27 + 2.54 / 5.0;
		return L;
	}

	constexpr double kEps = 1e-6; // eps_line

	// Local time at which x(tau) = Target on a rolling / sliding segment moving along +-x (smaller root).
	double TimeToReach(const MotionSegment& S, double Target, int Axis = 0)
	{
		const double P = Axis == 0 ? S.Pos0.x : S.Pos0.y;
		const double V = Axis == 0 ? S.Vel0.x : S.Vel0.y;
		const double A = Axis == 0 ? S.Accel2.x : S.Accel2.y;
		const double c = P - Target;
		if (A == 0.0)
		{
			return -c / V;
		}
		const double Disc = V * V - 4.0 * A * c;
		const double r0 = (-V + std::sqrt(Disc)) / (2.0 * A);
		const double r1 = (-V - std::sqrt(Disc)) / (2.0 * A);
		const double Lo = rb::Min(r0, r1);
		return Lo >= 0.0 ? Lo : rb::Max(r0, r1);
	}
}

RB_TEST(ARCH_DET4_PlanDistanceCrossingForAJumpOverABall)
{
	// D-5b geometry: A jumps (2, 0, 1.5) from the origin over B resting at (0.4, 0): no 3D contact, but the plan distance
	// enters 2R at 0.171425 s and leaves it at 0.228575 s (-> BallJumpedOver(A, B), rules F9).
	const MotionSegment A = Airborne({0.0, 0.0, kR}, {2.0, 0.0, 1.5});
	const MotionSegment B = Stationary({0.4, 0.0, kR});
	RB_CHECK(!rb::PredictBallBall(A, kR, B, kR, rb::kInfinity, Numerics()).Found);
	const ContactPrediction In = rb::PredictPlanDistanceCrossing(A, kR, B, kR, true, rb::kInfinity, Numerics());
	const ContactPrediction Out = rb::PredictPlanDistanceCrossing(A, kR, B, kR, false, rb::kInfinity, Numerics());
	RB_REQUIRE(In.Found && Out.Found);
	RB_CHECK_NEAR(In.Time, (0.4 - 2.0 * kR) / 2.0, 1e-12);
	RB_CHECK_NEAR(Out.Time, (0.4 + 2.0 * kR) / 2.0, 1e-12);

	// From inside the overlap (segment re-anchored at 0.2 s): no further entering, the leaving is found.
	const MotionSegment Mid = Airborne(rb::PositionAt(A, 0.2), rb::VelocityAt(A, 0.2), kG, 0.2);
	RB_CHECK(!rb::PredictPlanDistanceCrossing(Mid, kR, B, kR, true, rb::kInfinity, Numerics()).Found);
	const ContactPrediction Out2 = rb::PredictPlanDistanceCrossing(Mid, kR, B, kR, false, rb::kInfinity, Numerics());
	RB_REQUIRE(Out2.Found);
	RB_CHECK_NEAR(Out2.Time, Out.Time, 1e-12);
	// The window ends at the landing: a far ball is never entered.
	RB_CHECK(!rb::PredictPlanDistanceCrossing(A, kR, Stationary({2.0, 0.0, kR}), kR, true, rb::kInfinity, Numerics()).Found);
	// TimeLimit cuts it.
	RB_CHECK(!rb::PredictPlanDistanceCrossing(A, kR, B, kR, false, 0.2, Numerics()).Found);
}

RB_TEST(ARCH_DET5_LineCrossingsSortedWithDirectionAndIncludeFrom)
{
	const rb::TableLandmarks L = NineFoot();
	LineCrossing Out[16];

	// Up the table from behind the baulk line: Baulk, HeadString, CenterString, FootString, all +1, in time order.
	const MotionSegment Roll = Rolling({-1.0, 0.1, kR}, {2.0, 0.0, 0.0}, kMuR, kG, 0.5);
	int N = rb::PredictLineCrossings(Roll, L, Roll.T0, rb::kInfinity, kEps, false, Out, 16);
	RB_REQUIRE(N == 4);
	const TableLine Expected[4] = {TableLine::Baulk, TableLine::HeadString, TableLine::CenterString, TableLine::FootString};
	const double Values[4] = {L.BaulkX, L.HeadStringX, L.CenterStringX, L.FootStringX};
	for (int i = 0; i < 4; ++i)
	{
		RB_CHECK(Out[i].Line == Expected[i]);
		RB_CHECK(Out[i].Direction == 1);
		RB_CHECK_NEAR(Out[i].Time, 0.5 + TimeToReach(Roll, Values[i] + kEps), 1e-12);
	}
	// Window (TimeFrom, TimeLimit] and capacity.
	RB_CHECK(rb::PredictLineCrossings(Roll, L, Out[1].Time, Out[2].Time, kEps, false, Out, 16) == 1);
	RB_CHECK(rb::PredictLineCrossings(Roll, L, Roll.T0, rb::kInfinity, kEps, false, Out, 2) == 2 && Out[1].Line == TableLine::HeadString);

	// Starting exactly on HeadString + eps and moving beyond: counted only with IncludeFrom (the observer of a segment that
	// an event replaced exactly at the crossing time, architecture 8.7).
	const MotionSegment OnLine = Rolling({L.HeadStringX + kEps, -0.2, kR}, {0.5, 0.0, 0.0}, kMuR, kG, 2.0);
	N = rb::PredictLineCrossings(OnLine, L, 2.0, 2.5, kEps, true, Out, 16);
	RB_REQUIRE(N >= 1);
	RB_CHECK(Out[0].Time == 2.0 && Out[0].Line == TableLine::HeadString && Out[0].Direction == 1);
	N = rb::PredictLineCrossings(OnLine, L, 2.0, 2.5, kEps, false, Out, 16);
	RB_CHECK(N == 0 || Out[0].Time > 2.0);

	// Down across the head string: -1 at HeadString - eps; then the long string (y) +1, then the baulk line -1.
	const MotionSegment Back = Rolling({-0.5, -0.1, kR}, {-1.0, 0.5, 0.0});
	N = rb::PredictLineCrossings(Back, L, 0.0, rb::kInfinity, kEps, false, Out, 16);
	RB_REQUIRE(N == 3);
	RB_CHECK(Out[0].Line == TableLine::HeadString && Out[0].Direction == -1);
	RB_CHECK_NEAR(Out[0].Time, TimeToReach(Back, L.HeadStringX - kEps), 1e-12);
	RB_CHECK(Out[1].Line == TableLine::LongString && Out[1].Direction == 1);
	RB_CHECK_NEAR(Out[1].Time, TimeToReach(Back, L.LongStringY + kEps, 1), 1e-12);
	RB_CHECK(Out[2].Line == TableLine::Baulk && Out[2].Direction == -1);

	// A drawn ball crossing the head string and coming back (curving / reversing segments, no velocity-sign culling).
	const MotionSegment Draw = Sliding({-0.66, 0.2, kR}, {0.5, 0.0, 0.0}, {0.0, -100.0, 0.0});
	N = rb::PredictLineCrossings(Draw, L, 0.0, rb::kInfinity, kEps, false, Out, 16);
	RB_REQUIRE(N == 2);
	RB_CHECK(Out[0].Line == TableLine::HeadString && Out[0].Direction == 1);
	RB_CHECK(Out[1].Line == TableLine::HeadString && Out[1].Direction == -1);
	RB_CHECK(Out[0].Time < Out[1].Time && Out[1].Time <= Draw.TauEnd);
}
