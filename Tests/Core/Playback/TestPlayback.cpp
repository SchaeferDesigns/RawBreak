// Playback of hand-built ShotResults (Docs/architecture.md 8.10, ue5-realism-plan 5.7, prior-art 7.4 P6):
// A-PLAY-1 (orientation continuity at segment boundaries), A-PLAY-2 (cursor == random access bitwise; _Slow_ P6 timing),
// A-PLAY-3 (CueTipAt), sampling and segment evaluation (WP-7).

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Playback.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace playtest;
using rb::BallState;
using rb::MotionState;
using rb::Quat;
using rb::TrajectorySegment;
using rb::Vec3;

namespace
{
	// Four balls covering every kind of segment: sliding with draw and English into a roll whose spin dies mid-roll (grid
	// law with the spin-stop limit), a resting ball hit at 0.3 s through a 5 ms island (Sampled) into a plain roll
	// (closed form), a roll into a pocket (PocketFall flight, Terminal), a spinning ball.
	rb::ShotResult MakeShowcaseResult()
	{
		rb::ShotResult R;
		const Quat Q0 = rb::Normalized(Quat{0.8, 0.2, -0.4, 0.3});
		double Stop = 0.0;

		// Ball 0.
		{
			const BallState S = SurfaceState({-0.6, 0.1, kR}, {2.2, 0.3, 0.0}, {-5.0, -60.0, 18.0});
			Stop = std::fmax(Stop, AppendRollOut(R.Tracks[0], S, 0.0, Q0));
		}
		// Ball 1.
		{
			rb::BallTrack& T = R.Tracks[1];
			const BallState Rest = SurfaceState({0.3, -0.2, kR}, Vec3::Zero(), Vec3::Zero());
			TrajectorySegment Still = AnalyticSegment(rb::MakeSegment(Rest, 0.0, Spec(), Cloth(), 0.0, kG));
			Still.Orientation0 = rb::Normalized(Quat{0.1, 0.9, 0.3, -0.2});
			AppendSegment(T, Still);
			TrajectorySegment Island;
			Island.Kind = rb::SegmentKind::Sampled;
			Island.Motion.State = MotionState::Sliding;
			Island.Motion.T0 = 0.3;
			Island.Motion.Pos0 = Rest.Position;
			Island.Motion.Omega0 = {3.0, 20.0, 5.0};
			Island.EndPosition = Rest.Position + Vec3{0.004, 0.001, 0.0};
			AppendSegment(T, Island);
			const BallState Out = SurfaceState(Island.EndPosition, {0.8, 0.2, 0.0}, {-0.2 / kR, 0.8 / kR, 0.0});
			Stop = std::fmax(Stop, AppendRollOut(T, Out, 0.305, Quat::Identity()));
		}
		// Ball 2.
		{
			rb::BallTrack& T = R.Tracks[2];
			const BallState Roll = SurfaceState({1.0, 0.3, kR}, {0.0, 0.75, 0.0}, {-0.75 / kR, 0.0, 0.0});
			TrajectorySegment Rolling = AnalyticSegment(rb::MakeSegment(Roll, 0.0, Spec(), Cloth(), 0.0, kG));
			Rolling.Orientation0 = Quat::Identity();
			AppendSegment(T, Rolling);
			BallState Drop = rb::EvaluateSegment(Rolling.Motion, 0.4);
			Drop.Velocity.z = -0.3;
			Drop.State = MotionState::PocketFall;
			AppendSegment(T, AnalyticSegment(rb::MakeSegment(Drop, 0.4, Spec(), Cloth(), 0.0, kG)));
			TrajectorySegment Captured;
			Captured.Kind = rb::SegmentKind::Terminal;
			Captured.Motion.State = MotionState::Pocketed;
			Captured.Motion.T0 = 0.45;
			Captured.Motion.Pos0 = rb::EvaluateTrajectorySegment(T.Segments.back(), 0.45).Position;
			Captured.T1 = rb::kInfinity;
			AppendSegment(T, Captured);
		}
		// Ball 3.
		{
			const BallState Spin = SurfaceState({-0.2, 0.4, kR}, Vec3::Zero(), {0.0, 0.0, -12.0});
			Stop = std::fmax(Stop, AppendRollOut(R.Tracks[3], Spin, 0.0, Q0));
		}
		R.StopTime = Stop;
		R.Status = rb::SimStatus::Ok;
		return R;
	}

	constexpr int kShowcaseBalls = 4;
}

// A-PLAY-1: the orientation at T1 of every segment equals the next Orientation0 bitwise (random access, cursor, and the
// closing segment evaluated on its own), positions are continuous, and there is no visible jump just before T1.
RB_TEST(ARCH_PLAY1_OrientationAtSegmentEndEqualsNextOrientation0)
{
	const rb::ShotResult R = MakeShowcaseResult();
	int Boundaries = 0;
	int GridBoundaries = 0;
	for (int b = 0; b < kShowcaseBalls; ++b)
	{
		const std::vector<TrajectorySegment>& Segs = R.Tracks[b].Segments;
		RB_REQUIRE(Segs.size() >= 2);
		for (std::size_t k = 0; k + 1 < Segs.size(); ++k)
		{
			const TrajectorySegment& A = Segs[k];
			const TrajectorySegment& B = Segs[k + 1];
			RB_REQUIRE(SameBits(A.T1, B.Motion.T0));
			++Boundaries;
			GridBoundaries += rb::HasConstantRotationAxis(A) ? 0 : 1;

			Quat Q;
			RB_REQUIRE(rb::OrientationAt(R, b, A.T1, Q));
			RB_CHECK(SameBits(Q, B.Orientation0));

			// The closing segment alone (no successor): random access and a cursor that played it frame by frame.
			rb::ShotResult Lone;
			Lone.Tracks[0].Segments.push_back(A);
			RB_REQUIRE(rb::OrientationAt(Lone, 0, A.T1, Q));
			RB_CHECK(SameBits(Q, B.Orientation0));
			rb::PlaybackCursor Cursor;
			BallState State;
			for (double T = A.Motion.T0; T < A.T1; T += 0.0123)
			{
				RB_REQUIRE(rb::StateAtCursor(Lone, Cursor, 0, T, State, Q));
			}
			RB_REQUIRE(rb::StateAtCursor(Lone, Cursor, 0, A.T1, State, Q));
			RB_CHECK(SameBits(Q, B.Orientation0));

			// Continuous position, no visible jump.
			const BallState End = rb::EvaluateTrajectorySegment(A, A.T1);
			const BallState Next = rb::EvaluateTrajectorySegment(B, B.Motion.T0);
			RB_CHECK(rb::Length(End.Position - Next.Position) < 1e-12);
			const double Before = A.T1 - 1e-6 > A.Motion.T0 ? A.T1 - 1e-6 : A.Motion.T0;
			Quat Q1;
			RB_REQUIRE(rb::OrientationAt(R, b, Before, Q1));
			RB_CHECK(AngleBetween(Q1, B.Orientation0) < 2e-4); // |w| <= 200 rad/s over 1 us
		}
	}
	RB_CHECK(Boundaries >= 8);
	RB_CHECK(GridBoundaries >= 2);
}

// A-PLAY-2: per-frame cursor playback is bitwise equal to random access, also across backward jumps, segment
// boundaries hit exactly, times before the track and after its end; sampling agrees too.
RB_TEST(ARCH_PLAY2_CursorEqualsRandomAccessBitwise)
{
	const rb::ShotResult R = MakeShowcaseResult();
	std::vector<double> Times;
	for (double T = -0.05; T < R.StopTime + 0.3; T += 1.0 / 60.0)
	{
		Times.push_back(T);
	}
	// Exact boundaries and times just around them, in increasing order within each ball's sweep below.
	std::vector<double> Special;
	for (int b = 0; b < kShowcaseBalls; ++b)
	{
		for (const TrajectorySegment& S : R.Tracks[b].Segments)
		{
			Special.push_back(S.Motion.T0);
			Special.push_back(std::nextafter(S.Motion.T0, -1.0));
			Special.push_back(S.Motion.T0 + 0.0005);
		}
	}

	int Compared = 0;
	for (int b = 0; b < kShowcaseBalls; ++b)
	{
		rb::PlaybackCursor Cursor;
		std::vector<double> Sweep = Times;
		Sweep.insert(Sweep.end(), Special.begin(), Special.end());
		std::sort(Sweep.begin(), Sweep.end());
		for (const double T : Sweep)
		{
			BallState A;
			BallState B;
			Quat QA;
			Quat QB;
			RB_REQUIRE(rb::StateAtCursor(R, Cursor, b, T, A, QA));
			RB_REQUIRE(rb::StateAt(R, b, T, B));
			RB_REQUIRE(rb::OrientationAt(R, b, T, QB));
			RB_CHECK(SameBits(A, B));
			RB_CHECK(SameBits(QA, QB));
			++Compared;
		}
		// Backward jumps and random order.
		rb::Rng Rng(0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(b));
		for (int i = 0; i < 300; ++i)
		{
			const double T = Rng.NextUniform(-0.1, R.StopTime + 0.5);
			BallState A;
			BallState B;
			Quat QA;
			Quat QB;
			RB_REQUIRE(rb::StateAtCursor(R, Cursor, b, T, A, QA));
			RB_REQUIRE(rb::StateAt(R, b, T, B));
			RB_REQUIRE(rb::OrientationAt(R, b, T, QB));
			RB_CHECK(SameBits(A, B));
			RB_CHECK(SameBits(QA, QB));
			++Compared;
		}
		// A reset cursor starts over.
		rb::ResetCursor(Cursor);
		BallState A;
		BallState B;
		Quat QA;
		Quat QB;
		RB_REQUIRE(rb::StateAtCursor(R, Cursor, b, 0.5 * R.StopTime, A, QA));
		RB_REQUIRE(rb::StateAt(R, b, 0.5 * R.StopTime, B));
		RB_REQUIRE(rb::OrientationAt(R, b, 0.5 * R.StopTime, QB));
		RB_CHECK(SameBits(A, B));
		RB_CHECK(SameBits(QA, QB));
	}
	RB_CHECK(Compared > 1000);

	// Balls without a track.
	rb::PlaybackCursor Cursor;
	BallState S;
	Quat Q;
	RB_CHECK(!rb::StateAt(R, 7, 0.1, S));
	RB_CHECK(!rb::OrientationAt(R, 7, 0.1, Q));
	RB_CHECK(!rb::StateAtCursor(R, Cursor, 7, 0.1, S, Q));
	RB_CHECK(!rb::StateAt(R, -1, 0.1, S));
	RB_CHECK(!rb::StateAt(R, rb::kMaxBalls, 0.1, S));
}

// SampleTrajectory: [0, StopTime] every Dt plus every boundary and StopTime, strictly increasing, bitwise equal to
// random access; capacity overflow reported.
RB_TEST(Playback_SampleTrajectoryIncludesBoundariesAndStopTime)
{
	const rb::ShotResult R = MakeShowcaseResult();
	std::vector<rb::TrajectorySample> Out(100000);
	for (int b = 0; b < kShowcaseBalls; ++b)
	{
		const int N = rb::SampleTrajectory(R, b, 0.01, Out.data(), static_cast<int>(Out.size()));
		RB_REQUIRE(N > 2);
		RB_CHECK(Out[0].Time == 0.0);
		RB_CHECK(Out[static_cast<std::size_t>(N - 1)].Time == R.StopTime);
		for (int i = 1; i < N; ++i)
		{
			RB_CHECK(Out[static_cast<std::size_t>(i)].Time > Out[static_cast<std::size_t>(i - 1)].Time);
		}
		for (const TrajectorySegment& S : R.Tracks[b].Segments)
		{
			if (!(S.Motion.T0 > 0.0) || S.Motion.T0 > R.StopTime)
			{
				continue;
			}
			bool Found = false;
			for (int i = 0; i < N; ++i)
			{
				Found = Found || Out[static_cast<std::size_t>(i)].Time == S.Motion.T0;
			}
			RB_CHECK(Found);
		}
		for (int i = 0; i < N; ++i)
		{
			const rb::TrajectorySample& S = Out[static_cast<std::size_t>(i)];
			BallState State;
			Quat Q;
			RB_REQUIRE(rb::StateAt(R, b, S.Time, State));
			RB_REQUIRE(rb::OrientationAt(R, b, S.Time, Q));
			RB_CHECK(SameBits(S.Position, State.Position) && SameBits(S.Velocity, State.Velocity) && SameBits(S.Omega, State.Omega));
			RB_CHECK(S.State == State.State);
			RB_CHECK(SameBits(S.Orientation, Q));
		}
		// Boundaries only.
		const int NB = rb::SampleTrajectory(R, b, 0.0, Out.data(), static_cast<int>(Out.size()));
		RB_CHECK(NB >= 2 && NB <= static_cast<int>(R.Tracks[b].Segments.size()) + 1);
		// Too small.
		RB_CHECK(rb::SampleTrajectory(R, b, 0.01, Out.data(), 3) == -1);
	}
	RB_CHECK(rb::SampleTrajectory(R, 9, 0.01, Out.data(), static_cast<int>(Out.size())) == 0);
}

// EvaluateTrajectorySegment semantics of the three segment kinds.
RB_TEST(Playback_SegmentKindsEvaluate)
{
	TrajectorySegment Sampled;
	Sampled.Kind = rb::SegmentKind::Sampled;
	Sampled.Motion.State = MotionState::Rolling;
	Sampled.Motion.T0 = 1.0;
	Sampled.T1 = 1.004;
	Sampled.Motion.Pos0 = {0.1, 0.2, kR};
	Sampled.EndPosition = {0.102, 0.199, kR};
	Sampled.Motion.Omega0 = {1.0, 2.0, 3.0};
	const BallState Mid = rb::EvaluateTrajectorySegment(Sampled, 1.001);
	RB_CHECK_NEAR(Mid.Position.x, 0.1005, 1e-15);
	RB_CHECK_NEAR(Mid.Position.y, 0.19975, 1e-15);
	RB_CHECK_NEAR(Mid.Velocity.x, 0.5, 1e-12);
	RB_CHECK_NEAR(Mid.Velocity.y, -0.25, 1e-12);
	RB_CHECK(SameBits(Mid.Omega, Sampled.Motion.Omega0));
	RB_CHECK(Mid.State == MotionState::Rolling);
	RB_CHECK(SameBits(rb::EvaluateTrajectorySegment(Sampled, 0.5).Position, Sampled.Motion.Pos0));
	RB_CHECK(SameBits(rb::EvaluateTrajectorySegment(Sampled, 1.004).Position, Sampled.EndPosition));
	RB_CHECK(SameBits(rb::EvaluateTrajectorySegment(Sampled, 9.0).Position, Sampled.EndPosition));

	TrajectorySegment Terminal;
	Terminal.Kind = rb::SegmentKind::Terminal;
	Terminal.Motion.State = MotionState::OffTable;
	Terminal.Motion.T0 = 2.0;
	Terminal.Motion.Pos0 = {1.4, 0.2, 0.1};
	Terminal.Motion.Vel0 = {1.0, 0.0, 0.0};
	Terminal.T1 = rb::kInfinity;
	const BallState Off = rb::EvaluateTrajectorySegment(Terminal, 5.0);
	RB_CHECK(SameBits(Off.Position, Terminal.Motion.Pos0));
	RB_CHECK(SameBits(Off.Velocity, Vec3::Zero()));
	RB_CHECK(Off.State == MotionState::OffTable);

	// Analytic: EvaluateSegment at T - T0, clamped to [T0, T1].
	const BallState Roll = SurfaceState({0.0, 0.0, kR}, {1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 0.0});
	TrajectorySegment A = AnalyticSegment(rb::MakeSegment(Roll, 0.25, Spec(), Cloth(), 0.0, kG));
	A.T1 = 0.75;
	RB_CHECK(SameBits(rb::EvaluateTrajectorySegment(A, 0.5), rb::EvaluateSegment(A.Motion, 0.5 - 0.25)));
	RB_CHECK(SameBits(rb::EvaluateTrajectorySegment(A, 2.0), rb::EvaluateSegment(A.Motion, 0.75 - 0.25)));
	RB_CHECK(SameBits(rb::EvaluateTrajectorySegment(A, 0.0), rb::EvaluateSegment(A.Motion, 0.0)));
}

// A-PLAY-3: CueTipAt along analytic follow-through pieces (the tip dome of CueTipAsSegment), a re-contact piece and a
// sampled (island) piece; one path per strike.
RB_TEST(ARCH_PLAY3_CueTipAlongAnalyticAndSampledPieces)
{
	// A real strike: the dome touches the ball at the contact point at t = 0.
	rb::CueStrikeInput Input;
	Input.Speed = 3.0;
	Input.Azimuth = 0.3;
	Input.Elevation = 0.1;
	Input.OffsetA = 0.2;
	Input.OffsetB = -0.3;
	BallState Ball = SurfaceState({-0.6, 0.0, kR}, Vec3::Zero(), Vec3::Zero());
	const rb::NumericsConfig Numerics;
	const rb::StrikeResult Strike = rb::StrikeCueBall(Input, Ball, Spec(), Cloth(), rb::SlateParams{}, rb::PinchParams{}, kG, Numerics);
	RB_REQUIRE(Strike.Error == rb::ErrorCode::Ok);
	rb::CueTipPath Path = rb::MakeCueTipPath(Input, Strike, Ball.Position, kR);
	Path.Strike = 0;
	Path.StruckBall = 0;
	RB_REQUIRE(Path.StopTime > 0.05);

	rb::ShotResult R;
	rb::CueTipSegment First;
	First.Strike = 0;
	First.Path = Path;
	First.T1 = 0.02; // replaced by a re-contact at 20 ms
	R.CueTips.push_back(First);

	const rb::MotionSegment AsSeg = rb::CueTipAsSegment(Path);
	rb::CueTipSegment Second;
	Second.Strike = 0;
	Second.Path = Path;
	Second.Path.StartTime = 0.02;
	Second.Path.Start = rb::PositionAt(AsSeg, 0.02);
	Second.Path.Speed0 = 0.4 * Path.Speed0;
	Second.Path.StopTime = 0.02 + Second.Path.Speed0 / Path.Deceleration;
	Second.T1 = 0.03;
	R.CueTips.push_back(Second);

	rb::CueTipSegment Island;
	Island.Strike = 0;
	Island.Kind = rb::SegmentKind::Sampled;
	Island.Path = Second.Path;
	Island.Path.StartTime = 0.03;
	Island.Path.Start = rb::PositionAt(rb::CueTipAsSegment(Second.Path), 0.01);
	Island.EndPosition = Island.Path.Start + Path.Direction * 0.002;
	Island.T1 = 0.034;
	R.CueTips.push_back(Island);

	rb::CueTipPath LagPath = Path;
	LagPath.Strike = 1;
	LagPath.Start = {0.0, 0.3, kR};
	rb::CueTipSegment Lag;
	Lag.Strike = 1;
	Lag.Path = LagPath;
	Lag.T1 = LagPath.StopTime;
	R.CueTips.push_back(Lag);

	Vec3 Tip;
	Vec3 Dir;
	RB_REQUIRE(rb::CueTipAt(R, 0, 0.0, Tip, Dir));
	RB_CHECK(SameBits(Tip, Path.Start));
	RB_CHECK(SameBits(Dir, Path.Direction));
	RB_CHECK_NEAR(rb::Length(Tip - Ball.Position), kR + Input.Cue.TipDomeRadius, 1e-12);
	RB_CHECK(rb::CueTipAt(R, 0, -0.5, Tip, Dir) && SameBits(Tip, Path.Start)); // before the stroke: at the contact point

	// First piece: s = V' t - a t^2 / 2 along d.
	RB_REQUIRE(rb::CueTipAt(R, 0, 0.01, Tip, Dir));
	const double S1 = Path.Speed0 * 0.01 - 0.5 * Path.Deceleration * 0.01 * 0.01;
	RB_CHECK(rb::Length(Tip - (Path.Start + Path.Direction * S1)) < 1e-15);
	// Second piece (after the re-contact), continuous at 20 ms.
	RB_REQUIRE(rb::CueTipAt(R, 0, 0.02, Tip, Dir));
	RB_CHECK(SameBits(Tip, Second.Path.Start));
	RB_REQUIRE(rb::CueTipAt(R, 0, 0.025, Tip, Dir));
	const double S2 = Second.Path.Speed0 * 0.005 - 0.5 * Path.Deceleration * 0.005 * 0.005;
	RB_CHECK(rb::Length(Tip - (Second.Path.Start + Path.Direction * S2)) < 1e-15);
	// Sampled piece: linear.
	RB_REQUIRE(rb::CueTipAt(R, 0, 0.032, Tip, Dir));
	RB_CHECK(rb::Length(Tip - (Island.Path.Start + Path.Direction * 0.001)) < 1e-15);
	// After the last piece: where the island left it.
	RB_REQUIRE(rb::CueTipAt(R, 0, 5.0, Tip, Dir));
	RB_CHECK(SameBits(Tip, Island.EndPosition));

	// The lag's second cue: its own path, at rest after its stop time.
	RB_REQUIRE(rb::CueTipAt(R, 1, 10.0, Tip, Dir));
	const double Travel = Path.Speed0 * (Path.StopTime - Path.StartTime) - 0.5 * Path.Deceleration * (Path.StopTime - Path.StartTime) * (Path.StopTime - Path.StartTime);
	RB_CHECK(rb::Length(Tip - (LagPath.Start + Path.Direction * Travel)) < 1e-14);
	RB_CHECK_NEAR(Travel, Input.Cue.FollowThroughDistance, 1e-12); // the cue decelerates to rest over the follow-through
	RB_CHECK(!rb::CueTipAt(R, 2, 0.0, Tip, Dir));
}

// A-PLAY-2 (_Slow_, Release): prior-art 7.4 P6, analytic state evaluation <= 0.2 us per ball; per-frame cursor cost.
RB_TEST(ARCH_PLAY2_Slow_PlaybackTimingP6)
{
	rb::ShotResult R;
	double Stop = 0.0;
	rb::Rng Rng(77);
	for (int b = 0; b < 16; ++b)
	{
		const double V = Rng.NextUniform(0.3, 3.0);
		const double A = Rng.NextUniform(0.0, 2.0 * rb::kPi);
		const Vec3 Vel{V * std::cos(A), V * std::sin(A), 0.0};
		const Vec3 Omega{Rng.NextUniform(-60.0, 60.0), Rng.NextUniform(-60.0, 60.0), Rng.NextUniform(-20.0, 20.0)};
		Stop = std::fmax(Stop, AppendRollOut(R.Tracks[b], SurfaceState({0.0, 0.0, kR}, Vel, Omega), 0.0, Quat::Identity()));
	}
	R.StopTime = Stop;

	// Random access state (the P6 measure).
	std::vector<double> Times(20000);
	for (double& T : Times)
	{
		T = Rng.NextUniform(0.0, Stop);
	}
	double Sink = 0.0;
	auto Start = std::chrono::steady_clock::now();
	for (const double T : Times)
	{
		for (int b = 0; b < 16; ++b)
		{
			BallState S;
			rb::StateAt(R, b, T, S);
			Sink += S.Position.x;
		}
	}
	const double RandomNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - Start).count() / (16.0 * static_cast<double>(Times.size()));

	// Per-frame cursor playback (state + orientation) at 60 fps.
	rb::PlaybackCursor Cursor;
	int Frames = 0;
	Start = std::chrono::steady_clock::now();
	for (double T = 0.0; T <= Stop; T += 1.0 / 60.0)
	{
		for (int b = 0; b < 16; ++b)
		{
			BallState S;
			Quat Q;
			rb::StateAtCursor(R, Cursor, b, T, S, Q);
			Sink += Q.w;
		}
		++Frames;
	}
	const double CursorNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - Start).count() / (16.0 * Frames);

	// What the simulator pays per closed segment (the next Orientation0): the grid step of the law, per 1 ms of a sliding
	// segment, and a whole shot's tracks.
	const BallState Slide = SurfaceState({0.0, 0.0, kR}, {2.5, 0.4, 0.0}, {10.0, -80.0, 35.0});
	const TrajectorySegment Sliding = AnalyticSegment(rb::MakeSegment(Slide, 0.0, Spec(), Cloth(), 0.0, kG));
	Start = std::chrono::steady_clock::now();
	for (int i = 0; i < 200; ++i)
	{
		Sink += rb::SegmentOrientationAt(Quat::Identity(), Sliding, Sliding.Motion.TauEnd).w;
	}
	const double StepNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - Start).count() /
		(200.0 * Sliding.Motion.TauEnd / rb::kOrientationSubstep);
	int Segments = 0;
	Start = std::chrono::steady_clock::now();
	for (int b = 0; b < 16; ++b)
	{
		for (const TrajectorySegment& S : R.Tracks[b].Segments)
		{
			Sink += rb::SegmentOrientationAt(S.Orientation0, S, S.T1 - S.Motion.T0).x;
			++Segments;
		}
	}
	const double ChainUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - Start).count();
	std::printf("  playback: StateAt %.1f ns/ball, StateAtCursor (state + orientation, 60 fps) %.1f ns/ball-frame, grid step %.1f ns, "
				"orientation of all %d segment ends of 16 roll-outs %.1f us (sink %g)\n",
		RandomNs, CursorNs, StepNs, Segments, ChainUs, Sink);
#if defined(NDEBUG)
	RB_CHECK(RandomNs <= 200.0);
	RB_CHECK(CursorNs <= 200.0);
#endif
}
