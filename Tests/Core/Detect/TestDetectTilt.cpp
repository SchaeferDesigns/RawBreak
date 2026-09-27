// Owner: WP-5 (event detection). A-DET-6 (architecture 8.11, human-factors 4.5.3): a tilt chain piece is a general
// quadratic whose Accel2 is not parallel to Vel0; detection must not assume otherwise. Event times against a ball, a nose
// line and a drop edge equal brute-force sampling of the quadratic, and SweptBounds contain the piece.

#include "rbtest.h"

#include "DetectTestUtil.h"

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
	// A curving Rolling piece (the slope pulls it toward +y while it rolls along +x); the node is at TauEnd = 3 s.
	MotionSegment CurvingPiece()
	{
		MotionSegment S = Rolling({-0.5, -0.2, kR}, {0.6, 0.0, 0.0});
		S.Accel2 = {-0.02, 0.012, 0.0};
		S.TauEnd = 3.0;
		S.Tilt.Active = true;
		S.Tilt.EndsInRefresh = true;
		S.Tilt.K = kMuR * kG;
		S.Tilt.G = {0.0, 0.024};
		return S;
	}
}

RB_TEST(ARCH_DET6_CurvingTiltPieceAgainstBallNoseAndDropEdge)
{
	const MotionSegment S = CurvingPiece();
	RB_CHECK(rb::Abs(rb::Cross(rb::XY(S.Vel0), rb::XY(S.Accel2))) > 1e-3); // really not parallel

	// Ball placed 50 mm to the side of where the curve is at 1.5 s (a straight-line assumption would miss it).
	const MotionSegment B = Stationary(rb::PositionAt(S, 1.5) + Vec3{0.0, 0.05, 0.0});
	const ContactPrediction Ball = rb::PredictBallBall(S, kR, B, kR, rb::kInfinity, Numerics());
	RB_REQUIRE(Ball.Found);
	const double BallRef = BruteForceFirstContact([&](double Tau) { return rb::Length(rb::PositionAt(S, Tau) - B.Pos0) - 2.0 * kR; }, S.TauEnd);
	RB_CHECK_NEAR(Ball.Time, BallRef, 1e-9);
	// Without the curvature (Accel2 = 0) the same ball would be missed:
	MotionSegment Straight = S;
	Straight.Accel2 = rb::Vec3{};
	RB_CHECK(!rb::PredictBallBall(Straight, kR, B, kR, rb::kInfinity, Numerics()).Found);

	// Nose line along y = 0.0 + R_c (the curve rises from y = -0.2 by 0.012 tau^2 ... crosses it before 3 s).
	const double Rc = NoseContactOffset();
	const rb::NoseSegment Nose = MakeNose({1.2, -0.12 + Rc}, {-1.2, -0.12 + Rc}, {0.0, -1.0});
	const ContactPrediction Cushion = rb::PredictNoseOnCloth(S, kR, Nose, Rc, rb::kInfinity, Numerics());
	RB_REQUIRE(Cushion.Found);
	const double CushionRef = BruteForceFirstContact([&](double Tau) { return (-0.12 + Rc - rb::PositionAt(S, Tau).y) - Rc; }, S.TauEnd);
	RB_CHECK_NEAR(Cushion.Time, CushionRef, 1e-9);

	// Drop edge of a pocket whose a_d circle the curve enters (full front arc).
	rb::PocketGeometry P = CornerPocket();
	P.FrontArcFrom = -rb::kPi;
	P.FrontArcSweep = rb::kTwoPi;
	P.CaptureCenter = rb::XY(rb::PositionAt(S, 2.2)) + Vec2{0.02, 0.05};
	const ContactPrediction Drop = rb::PredictDropEdge(S, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(Drop.Found);
	const double DropRef =
		BruteForceFirstContact([&](double Tau) { return rb::Length(rb::XY(rb::PositionAt(S, Tau)) - P.CaptureCenter) - P.DropEdgeRadius; }, S.TauEnd);
	RB_CHECK_NEAR(Drop.Time, DropRef, 1e-9);

	// SweptBounds contain every point of the piece (and of every sub-window), and are tight.
	const rb::Aabb3 Box = rb::SweptBounds(S, 0.0, S.TauEnd);
	double MinY = 1e9, MaxY = -1e9, MaxX = -1e9;
	bool Inside = true;
	for (int i = 0; i <= 3000; ++i)
	{
		const double Tau = S.TauEnd * i / 3000.0;
		const Vec3 Pt = rb::PositionAt(S, Tau);
		Inside = Inside && Pt.x >= Box.Lo.x && Pt.x <= Box.Hi.x && Pt.y >= Box.Lo.y && Pt.y <= Box.Hi.y && Pt.z >= Box.Lo.z && Pt.z <= Box.Hi.z;
		MinY = rb::Min(MinY, Pt.y);
		MaxY = rb::Max(MaxY, Pt.y);
		MaxX = rb::Max(MaxX, Pt.x);
	}
	RB_CHECK(Inside);
	RB_CHECK_NEAR(Box.Hi.x, MaxX, 1e-9); // x would only turn back at tau = 15 s, after the node
	RB_CHECK_NEAR(Box.Lo.y, MinY, 1e-9);
	RB_CHECK_NEAR(Box.Hi.y, MaxY, 1e-9);
	const rb::Aabb3 Part = rb::SweptBounds(S, 1.0, 2.0);
	for (int i = 0; i <= 100; ++i)
	{
		const Vec3 Pt = rb::PositionAt(S, 1.0 + i / 100.0);
		RB_CHECK(Pt.x >= Part.Lo.x && Pt.x <= Part.Hi.x && Pt.y >= Part.Lo.y && Pt.y <= Part.Hi.y);
	}
	// Stopping under the tilt: an x-reversing piece (masse-like) keeps the analytic interior extreme.
	MotionSegment Reverse = S;
	Reverse.Accel2 = {-0.2, 0.012, 0.0}; // x peaks at tau = 1.5 s
	const rb::Aabb3 RBox = rb::SweptBounds(Reverse, 0.0, Reverse.TauEnd);
	RB_CHECK_NEAR(RBox.Hi.x, rb::PositionAt(Reverse, 1.5).x, 1e-11);
}
