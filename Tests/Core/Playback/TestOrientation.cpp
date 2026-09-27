// ue5-realism-plan 13 T17, T18 (orientation integration) and the orientation law of rb/Physics/Playback.h, incl.
// A-PLAY-4 (curving Rolling tilt pieces, human-factors 4.5.3) (WP-7).

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Math/Quat.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Playback.h"

#include <cmath>

using namespace playtest;
using rb::BallState;
using rb::MotionState;
using rb::Quat;
using rb::TrajectorySegment;
using rb::Vec3;

// T17: constant w = (0, 10, 0) rad/s for 0.1 s in 1 ms steps from the identity -> q = (0.877583, 0, 0.479426, 0), 1e-9.
RB_TEST(UE_T17_OrientationIntegration)
{
	const Quat Q = rb::IntegrateOrientationSteps(Quat::Identity(), {0.0, 10.0, 0.0}, 1.0e-3, 100);
	RB_CHECK_NEAR(Q.w, std::cos(0.5), 1e-9);
	RB_CHECK_NEAR(Q.x, 0.0, 1e-9);
	RB_CHECK_NEAR(Q.y, std::sin(0.5), 1e-9);
	RB_CHECK_NEAR(Q.z, 0.0, 1e-9);
	// The published digits (+-1 unit in the 6th decimal).
	RB_CHECK_NEAR(Q.w, 0.877583, 1e-6);
	RB_CHECK_NEAR(Q.y, 0.479426, 1e-6);

	// The same rotation through the orientation law of a constant-omega segment (closed form) and its grid.
	TrajectorySegment Flight;
	Flight.Kind = rb::SegmentKind::Analytic;
	Flight.Motion.State = MotionState::Airborne;
	Flight.Motion.TauEnd = 1.0;
	Flight.Motion.Omega0 = {0.0, 10.0, 0.0};
	const Quat Closed = rb::SegmentOrientationAt(Quat::Identity(), Flight, 0.1);
	RB_CHECK(rb::HasConstantRotationAxis(Flight));
	RB_CHECK_NEAR(Closed.w, std::cos(0.5), 1e-12);
	RB_CHECK_NEAR(Closed.y, std::sin(0.5), 1e-12);
	RB_CHECK(AngleBetween(Closed, Q) < 1e-9);
}

// T18: w = (0, 0, 2 pi) rad/s for 1 s in 1 ms steps -> q = +-identity, 1e-9.
RB_TEST(UE_T18_OrientationClosure)
{
	const Quat Q = rb::IntegrateOrientationSteps(Quat::Identity(), {0.0, 0.0, 2.0 * rb::kPi}, 1.0e-3, 1000);
	RB_CHECK_NEAR(std::fabs(Q.w), 1.0, 1e-9);
	RB_CHECK_NEAR(Q.x, 0.0, 1e-9);
	RB_CHECK_NEAR(Q.y, 0.0, 1e-9);
	RB_CHECK_NEAR(Q.z, 0.0, 1e-9);
	RB_CHECK(Q.w < 0.0); // one full turn of a spinor is -identity (the same rotation)
	RB_CHECK_NEAR(std::sqrt(rb::NormSquared(Q)), 1.0, 1e-15);

	// A Spinning segment with the same (constant) w: closed form, the same closure.
	TrajectorySegment Spin;
	Spin.Kind = rb::SegmentKind::Analytic;
	Spin.Motion.State = MotionState::Spinning;
	Spin.Motion.TauEnd = 10.0;
	Spin.Motion.Omega0 = {0.0, 0.0, 2.0 * rb::kPi};
	Spin.Motion.OmegaZStopTau = rb::kInfinity;
	const Quat S = rb::SegmentOrientationAt(Quat::Identity(), Spin, 1.0);
	RB_CHECK(AngleBetween(S, Quat::Identity()) < 1e-9);
}

// Structural classification of the orientation law (Playback.h): never a floating-point parallelism test.
RB_TEST(Playback_ConstantAxisClassificationIsStructural)
{
	TrajectorySegment S;
	S.Kind = rb::SegmentKind::Analytic;
	const MotionState Constant[] = {MotionState::Stationary, MotionState::Spinning, MotionState::Airborne, MotionState::PocketPivot,
		MotionState::PocketFall, MotionState::Pocketed, MotionState::OffTable};
	for (const MotionState State : Constant)
	{
		S.Motion.State = State;
		RB_CHECK(rb::HasConstantRotationAxis(S));
	}
	S.Motion.State = MotionState::Sliding;
	RB_CHECK(!rb::HasConstantRotationAxis(S));

	// Level rolling without spin: constant axis, even when rounding makes Accel2 not exactly parallel to Vel0.
	const BallState Roll = SurfaceState({0.0, 0.0, kR}, {0.7, 0.3, 0.0}, {-0.3 / kR, 0.7 / kR, 0.0});
	RB_REQUIRE(Roll.State == MotionState::Rolling);
	S.Motion = rb::MakeSegment(Roll, 0.0, Spec(), Cloth(), 0.0, kG);
	RB_CHECK(rb::HasConstantRotationAxis(S));
	S.Motion.Accel2.x += 1e-17; // perturbed direction: still the structural closed form
	RB_CHECK(rb::HasConstantRotationAxis(S));

	// Rolling with spin: grid.
	const BallState RollSpin = SurfaceState({0.0, 0.0, kR}, {1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 4.0});
	RB_REQUIRE(RollSpin.State == MotionState::Rolling);
	S.Motion = rb::MakeSegment(RollSpin, 0.0, Spec(), Cloth(), 0.0, kG);
	RB_CHECK(!rb::HasConstantRotationAxis(S));

	// Rolling tilt piece: grid (A-PLAY-4).
	rb::TiltParams Tilt;
	Tilt.Slope = {0.0, 2.0e-3};
	S.Motion = rb::MakeSegment(Roll, 0.0, Spec(), Cloth(), 0.0, kG, Tilt);
	RB_REQUIRE(S.Motion.Tilt.Active);
	RB_CHECK(!rb::HasConstantRotationAxis(S));

	S.Kind = rb::SegmentKind::Sampled;
	RB_CHECK(rb::HasConstantRotationAxis(S));
	S.Kind = rb::SegmentKind::Terminal;
	RB_CHECK(rb::HasConstantRotationAxis(S));
}

// tau = 0 and non-rotating segments return Q0 bitwise (no renormalisation), for every kind of segment.
RB_TEST(Playback_ZeroRotationKeepsOrientationBitwise)
{
	const Quat Q0 = rb::Normalized(Quat{0.3, -0.5, 0.2, 0.7});
	const BallState Slide = SurfaceState({0.0, 0.0, kR}, {2.0, 0.0, 0.0}, {0.0, -40.0, 15.0});
	RB_REQUIRE(Slide.State == MotionState::Sliding);
	TrajectorySegment S = AnalyticSegment(rb::MakeSegment(Slide, 0.0, Spec(), Cloth(), 0.0, kG));
	RB_CHECK(SameBits(rb::SegmentOrientationAt(Q0, S, 0.0), Q0));
	RB_CHECK(SameBits(rb::SegmentOrientationAt(Q0, S, -1.0), Q0));

	const BallState Rest = SurfaceState({0.3, 0.2, kR}, Vec3::Zero(), Vec3::Zero());
	TrajectorySegment Still = AnalyticSegment(rb::MakeSegment(Rest, 0.0, Spec(), Cloth(), 0.0, kG));
	RB_CHECK(SameBits(rb::SegmentOrientationAt(Q0, Still, 123.0), Q0));

	TrajectorySegment Terminal;
	Terminal.Kind = rb::SegmentKind::Terminal;
	Terminal.Motion.State = MotionState::Pocketed;
	Terminal.Motion.Omega0 = {5.0, 5.0, 5.0}; // ignored: terminal balls do not rotate
	RB_CHECK(SameBits(rb::SegmentOrientationAt(Q0, Terminal, 3.0), Q0));
}

// The law evaluates Exp(0.5 Theta) of small rotations (every 1 ms grid step below 250 rad/s) by its Taylor series: equal
// to the transcendental form (FromRotationVector) to rounding on both sides of the switch angle.
RB_TEST(Playback_SmallAngleSeriesEqualsTranscendentalForm)
{
	rb::Rng Rng(2024);
	TrajectorySegment Flight;
	Flight.Kind = rb::SegmentKind::Analytic;
	Flight.Motion.State = MotionState::Airborne;
	Flight.Motion.TauEnd = 2.0;
	double Worst = 0.0;
	for (int i = 0; i < 20000; ++i)
	{
		const double Angle = i < 10000 ? Rng.NextUniform(0.0, 0.25) : Rng.NextUniform(0.2499, 0.2501);
		const Vec3 Axis = rb::Normalized(Vec3{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)});
		Flight.Motion.Omega0 = Axis * Angle;
		const Quat Q0 = rb::Normalized(Quat{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)});
		const Quat Law = rb::SegmentOrientationAt(Q0, Flight, 1.0);
		const Quat Ref = rb::Normalized(rb::FromRotationVector(Axis * Angle) * Q0);
		Worst = std::fmax(Worst, std::fmax(std::fmax(std::fabs(Law.w - Ref.w), std::fabs(Law.x - Ref.x)), std::fmax(std::fabs(Law.y - Ref.y), std::fabs(Law.z - Ref.z))));
	}
	RB_CHECK_NEAR(Worst, 0.0, 1e-15);
}

// Closed forms are exact: level rolling rotates about z_hat x v_hat by distance / R; flight by w tau.
RB_TEST(Playback_RollingClosedFormIsDistanceOverRadius)
{
	const BallState Roll = SurfaceState({0.0, 0.0, kR}, {1.2, 0.0, 0.0}, {0.0, 1.2 / kR, 0.0});
	RB_REQUIRE(Roll.State == MotionState::Rolling);
	const TrajectorySegment S = AnalyticSegment(rb::MakeSegment(Roll, 0.0, Spec(), Cloth(), 0.0, kG));
	const double Tau = 0.8;
	const double Distance = rb::EvaluateSegment(S.Motion, Tau).Position.x - Roll.Position.x;
	const Quat Q = rb::SegmentOrientationAt(Quat::Identity(), S, Tau);
	const Quat Expected = rb::FromAxisAngle({0.0, 1.0, 0.0}, Distance / kR);
	RB_CHECK(AngleBetween(Q, Expected) < 1e-12);
	// A top point rotates toward +x (rolling toward +x).
	const Vec3 Top = rb::Rotate(Q, {0.0, 0.0, 1.0});
	RB_CHECK_NEAR(Top.x, std::sin(Distance / kR), 1e-12);
	// Beyond the end of the roll the ball does not turn any further (EvaluateSegment clamp).
	const Quat End = rb::SegmentOrientationAt(Quat::Identity(), S, S.Motion.TauEnd);
	RB_CHECK(SameBits(rb::SegmentOrientationAt(Quat::Identity(), S, S.Motion.TauEnd + 5.0), End));
	const double Total = rb::EvaluateSegment(S.Motion, S.Motion.TauEnd).Position.x - Roll.Position.x;
	RB_CHECK(AngleBetween(End, rb::FromAxisAngle({0.0, 1.0, 0.0}, Total / kR)) < 1e-11);
}

// The grid law (exact per-step integrals, 1 ms) against a fine brute-force integration of the same w laws: sliding with
// draw and side spin, rolling with spin (the grid stops at the spin stop, closed form after it).
RB_TEST(Playback_GridLawMatchesFineIntegration)
{
	const Quat Q0 = rb::Normalized(Quat{0.9, 0.1, -0.3, 0.2});
	{
		const BallState Slide = SurfaceState({0.0, 0.0, kR}, {2.5, 0.4, 0.0}, {10.0, -80.0, 35.0});
		RB_REQUIRE(Slide.State == MotionState::Sliding);
		const TrajectorySegment S = AnalyticSegment(rb::MakeSegment(Slide, 0.0, Spec(), Cloth(), 0.0, kG));
		RB_CHECK(!rb::HasConstantRotationAxis(S));
		for (const double Frac : {0.137, 0.5, 1.0})
		{
			const double Tau = Frac * S.Motion.TauEnd;
			const Quat Law = rb::SegmentOrientationAt(Q0, S, Tau);
			const Quat Ref = ReferenceOrientation(Q0, S, Tau, 200000);
			RB_CHECK(AngleBetween(Law, Ref) < 2e-4); // Magnus remainder of 1 ms steps (w x dw/dt terms), UE 5.7 grid
		}
	}
	{
		const BallState RollSpin = SurfaceState({0.0, 0.0, kR}, {0.8, -0.3, 0.0}, {0.3 / kR, 0.8 / kR, -6.0});
		RB_REQUIRE(RollSpin.State == MotionState::Rolling);
		const TrajectorySegment S = AnalyticSegment(rb::MakeSegment(RollSpin, 0.0, Spec(), Cloth(), 0.0, kG));
		RB_REQUIRE(S.Motion.OmegaZStopTau < S.Motion.TauEnd);
		for (const double Tau : {0.25, S.Motion.OmegaZStopTau, 0.5 * (S.Motion.OmegaZStopTau + S.Motion.TauEnd), S.Motion.TauEnd})
		{
			const Quat Law = rb::SegmentOrientationAt(Q0, S, Tau);
			const Quat Ref = ReferenceOrientation(Q0, S, Tau, 400000);
			RB_CHECK(AngleBetween(Law, Ref) < 1e-4);
		}
		// Substep refinement converges to the same rotation (the law is a consistent discretisation).
		const Quat Fine = rb::SegmentOrientationAt(Q0, S, S.Motion.TauEnd, 1.0e-5);
		RB_CHECK(AngleBetween(Fine, ReferenceOrientation(Q0, S, S.Motion.TauEnd, 400000)) < 1e-6);
	}
}

// A-PLAY-4: a curving Rolling tilt piece is not constant-axis; the orientation at every refresh boundary is bitwise
// continuous (the next piece starts from the law's value at the node, and playback shows exactly that value there).
RB_TEST(ARCH_PLAY4_CurvingTiltPieceOrientationContinuous)
{
	rb::TiltParams Tilt;
	Tilt.Slope = {0.4e-3, 2.0e-3}; // 2 mm/m across the roll: the path curves
	const BallState Start = SurfaceState({-0.8, 0.0, kR}, {1.4, 0.05, 0.0}, {0.0, 25.0, 3.0});
	RB_REQUIRE(Start.State == MotionState::Sliding);

	rb::ShotResult Result;
	const double Rest = AppendRollOut(Result.Tracks[0], Start, 0.0, Quat::Identity(), Tilt);
	Result.StopTime = Rest;
	const std::vector<TrajectorySegment>& Segs = Result.Tracks[0].Segments;
	int RollingPieces = 0;
	int Refreshes = 0;
	for (std::size_t k = 0; k + 1 < Segs.size(); ++k)
	{
		const TrajectorySegment& A = Segs[k];
		const TrajectorySegment& B = Segs[k + 1];
		if (A.Motion.State == MotionState::Rolling && A.Motion.Tilt.Active)
		{
			++RollingPieces;
			RB_CHECK(!rb::HasConstantRotationAxis(A));
			if (A.Motion.Tilt.EndsInRefresh)
			{
				// Accel2 not parallel to Vel0: the piece really curves.
				const double Cross = A.Motion.Vel0.x * A.Motion.Accel2.y - A.Motion.Vel0.y * A.Motion.Accel2.x;
				RB_CHECK(std::fabs(Cross) > 1e-9 * rb::Length(A.Motion.Vel0) * rb::Length(A.Motion.Accel2));
			}
		}
		if (A.Motion.Tilt.Active && A.Motion.Tilt.EndsInRefresh)
		{
			++Refreshes;
		}
		// Playback at the boundary shows the next Orientation0 exactly ...
		Quat AtBoundary;
		RB_REQUIRE(rb::OrientationAt(Result, 0, A.T1, AtBoundary));
		RB_CHECK(SameBits(AtBoundary, B.Orientation0));
		// ... and so does the closing piece evaluated at its own end (as a lone segment: no successor to switch to).
		rb::ShotResult Lone;
		Lone.Tracks[0].Segments.push_back(A);
		Quat LoneEnd;
		RB_REQUIRE(rb::OrientationAt(Lone, 0, A.T1, LoneEnd));
		RB_CHECK(SameBits(LoneEnd, B.Orientation0));
		rb::PlaybackCursor Cursor;
		BallState State;
		Quat ByCursor;
		for (double T = A.Motion.T0; T < A.T1; T += 1.0 / 60.0)
		{
			RB_REQUIRE(rb::StateAtCursor(Lone, Cursor, 0, T, State, ByCursor));
		}
		RB_REQUIRE(rb::StateAtCursor(Lone, Cursor, 0, A.T1, State, ByCursor));
		RB_CHECK(SameBits(ByCursor, B.Orientation0));
		// Positions continue across the node (the node is exact).
		const BallState End = rb::EvaluateTrajectorySegment(A, A.T1);
		const BallState Next = rb::EvaluateTrajectorySegment(B, B.Motion.T0);
		RB_CHECK(rb::Length(End.Position - Next.Position) < 1e-12);
	}
	RB_CHECK(RollingPieces >= 3);
	RB_CHECK(Refreshes >= 3);

	// The grid law on a curving piece matches a fine integration of the piece's w law: the remainder of exact-integral
	// 1 ms steps is the Magnus commutator term, about (Substep^2 / 12) * integral of |w x dw/dt| (1.4e-6 rad here).
	for (const TrajectorySegment& S : Segs)
	{
		if (S.Motion.State == MotionState::Rolling && S.Motion.Tilt.Active)
		{
			const double Tau = S.T1 - S.Motion.T0;
			RB_CHECK_NEAR(AngleBetween(rb::SegmentOrientationAt(S.Orientation0, S, Tau), ReferenceOrientation(S.Orientation0, S, Tau, 200000)), 0.0, 1e-5);
			RB_CHECK_NEAR(AngleBetween(rb::SegmentOrientationAt(S.Orientation0, S, Tau, 1.0e-4), ReferenceOrientation(S.Orientation0, S, Tau, 200000)), 0.0,
				1e-7);
			break;
		}
	}
}
