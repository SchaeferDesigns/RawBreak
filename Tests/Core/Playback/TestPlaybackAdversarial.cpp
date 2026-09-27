// WP-7 review: adversarial playback tests (Docs/architecture.md 8.10, ue5-realism-plan 5.7): non-finite frame times, extreme
// shots (masse spin above the series switch of the orientation law, a jump shot into a pocket, a ball leaving the table),
// bitwise determinism, and no heap allocation in the playback / record hot paths (Debug CRT hook).

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Playback.h"
#include "rb/Physics/Simulator.h"
#include "rb/Shot/ShotRecordBuilder.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define RB_PLAYBACK_TEST_ALLOC_HOOK 1
#else
#define RB_PLAYBACK_TEST_ALLOC_HOOK 0
#endif

using namespace playtest;
using rb::BallState;
using rb::MotionState;
using rb::Quat;
using rb::TrajectorySegment;
using rb::Vec3;

namespace
{
#if RB_PLAYBACK_TEST_ALLOC_HOOK
	int g_PlaybackTestAllocations = 0;
	int CountAllocations(int AllocType, void*, size_t, int, long, const unsigned char*, int)
	{
		if (AllocType == _HOOK_ALLOC || AllocType == _HOOK_REALLOC)
		{
			++g_PlaybackTestAllocations;
		}
		return 1;
	}
#endif

	constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

	// Ball 0: masse (|w| up to 420 rad/s, so every 1 ms grid step turns more than the 0.25 rad series switch) sliding into a
	// spinning roll. Ball 1: rolling, jumps (Airborne) at 0.2 s, lands, rolls on, hops into a pocket (PocketFall) and is
	// captured (Terminal). Ball 2: flies off the table (Airborne -> OffTable Terminal). Ball 3 at rest (never rotates).
	rb::ShotResult MakeExtremeResult()
	{
		rb::ShotResult R;
		const rb::NumericsConfig Numerics;
		double Stop = 0.0;

		// Ball 0: masse.
		{
			const BallState S = SurfaceState({-0.5, 0.2, kR}, {0.9, -0.2, 0.0}, {250.0, -330.0, 120.0});
			RB_CHECK(S.State == MotionState::Sliding);
			Stop = std::fmax(Stop, AppendRollOut(R.Tracks[0], S, 0.0, rb::Normalized(Quat{0.2, 0.7, -0.1, 0.4})));
		}
		// Ball 1: jump shot into a pocket.
		{
			rb::BallTrack& T = R.Tracks[1];
			const BallState Roll = SurfaceState({0.0, -0.3, kR}, {1.5, 0.0, 0.0}, {0.0, 1.5 / kR, 8.0});
			TrajectorySegment First = AnalyticSegment(rb::MakeSegment(Roll, 0.0, Spec(), Cloth(), 0.0, kG));
			First.Orientation0 = Quat::Identity();
			AppendSegment(T, First);
			BallState Hop = rb::EvaluateSegment(First.Motion, 0.2);
			Hop.Velocity.z = 0.9;
			Hop.State = MotionState::Airborne;
			TrajectorySegment Flight = AnalyticSegment(rb::MakeSegment(Hop, 0.2, Spec(), Cloth(), 0.0, kG));
			AppendSegment(T, Flight);
			RB_CHECK(Flight.Motion.TauEnd > 0.05 && Flight.Motion.TauEnd < 1.0);
			BallState Landed = rb::SegmentEndState(Flight.Motion, Numerics);
			Landed.Velocity.z = 0.0;
			Landed.Position.z = kR;
			Landed.Omega = {-0.1, 1.2 * Landed.Velocity.x / kR, 6.0};
			rb::ClassifyState(Landed, kR, 0.0, Numerics);
			const double TLand = 0.2 + Flight.Motion.TauEnd;
			TrajectorySegment After = AnalyticSegment(rb::MakeSegment(Landed, TLand, Spec(), Cloth(), 0.0, kG));
			AppendSegment(T, After);
			BallState Drop = rb::EvaluateTrajectorySegment(After, TLand + 0.15);
			Drop.Velocity.z = -0.4;
			Drop.State = MotionState::PocketFall;
			AppendSegment(T, AnalyticSegment(rb::MakeSegment(Drop, TLand + 0.15, Spec(), Cloth(), 0.0, kG)));
			TrajectorySegment Captured;
			Captured.Kind = rb::SegmentKind::Terminal;
			Captured.Motion.State = MotionState::Pocketed;
			Captured.Motion.T0 = TLand + 0.21;
			Captured.Motion.Pos0 = rb::EvaluateTrajectorySegment(T.Segments.back(), Captured.Motion.T0).Position;
			Captured.T1 = rb::kInfinity;
			AppendSegment(T, Captured);
			Stop = std::fmax(Stop, Captured.Motion.T0);
		}
		// Ball 2: off the table.
		{
			rb::BallTrack& T = R.Tracks[2];
			const BallState Up = SurfaceState({1.1, 0.5, kR}, {2.0, 1.0, 1.8}, {-30.0, 60.0, -15.0});
			RB_CHECK(Up.State == MotionState::Airborne);
			TrajectorySegment Flight = AnalyticSegment(rb::MakeSegment(Up, 0.0, Spec(), Cloth(), 0.0, kG));
			Flight.Orientation0 = rb::Normalized(Quat{-0.3, 0.1, 0.9, 0.2});
			AppendSegment(T, Flight);
			TrajectorySegment Gone;
			Gone.Kind = rb::SegmentKind::Terminal;
			Gone.Motion.State = MotionState::OffTable;
			Gone.Motion.T0 = 0.12;
			Gone.Motion.Pos0 = rb::EvaluateTrajectorySegment(Flight, 0.12).Position;
			Gone.T1 = rb::kInfinity;
			AppendSegment(T, Gone);
		}
		// Ball 3: at rest.
		{
			TrajectorySegment Still = AnalyticSegment(rb::MakeSegment(SurfaceState({-0.2, -0.4, kR}, Vec3::Zero(), Vec3::Zero()), 0.0, Spec(), Cloth(), 0.0, kG));
			Still.Orientation0 = rb::Normalized(Quat{0.5, -0.5, 0.5, 0.5});
			AppendSegment(R.Tracks[3], Still);
		}
		R.StopTime = Stop;
		R.Status = rb::SimStatus::Ok;
		return R;
	}

	constexpr int kExtremeBalls = 4;
}

// A NaN frame time (e.g. a division by zero in the renderer) must not poison the cursor: the next frames, including a
// backward jump, are again bitwise equal to random access (the cursor used to keep NaN as its last time and then never
// re-seeked, evaluating the cached later segment at earlier times).
RB_TEST(Playback_Adv_CursorRecoversFromNonFiniteTime)
{
	const rb::ShotResult R = MakeExtremeResult();
	for (int b = 0; b < kExtremeBalls; ++b)
	{
		const std::vector<TrajectorySegment>& Segs = R.Tracks[b].Segments;
		const double Late = Segs.size() > 1 ? Segs.back().Motion.T0 + 0.01 : 0.9;
		const double Times[] = {0.0, Late, kNaN, 0.05, rb::kInfinity, 0.03, -rb::kInfinity, kNaN, kNaN, Late, 0.01};
		rb::PlaybackCursor Cursor;
		for (const double T : Times)
		{
			BallState A;
			BallState B;
			Quat QA;
			Quat QB;
			RB_REQUIRE(rb::StateAtCursor(R, Cursor, b, T, A, QA));
			RB_REQUIRE(rb::StateAt(R, b, T, B));
			RB_REQUIRE(rb::OrientationAt(R, b, T, QB));
			RB_CHECK(SameBits(QA, QB));
			RB_CHECK(std::isfinite(QA.w) && std::fabs(rb::NormSquared(QA) - 1.0) < 1e-12);
			if (!std::isnan(T))
			{
				RB_CHECK(SameBits(A, B));
			}
		}
	}
	// "Show the final table" at T = +inf (or any T whose square overflows): a resting ball is where it rests, not at NaN
	// (EvaluateSegment of the open last Stationary segment computed Pos0 + 0 * inf).
	for (const double T : {rb::kInfinity, 1.0e200})
	{
		BallState Final;
		RB_REQUIRE(rb::StateAt(R, 3, T, Final));
		RB_CHECK(SameBits(Final.Position, R.Tracks[3].Segments[0].Motion.Pos0) && SameBits(Final.Velocity, Vec3::Zero()));
		RB_REQUIRE(rb::StateAt(R, 0, T, Final));
		RB_CHECK(Final.State == MotionState::Stationary && std::isfinite(Final.Position.x) && std::isfinite(Final.Position.y));
		RB_CHECK(SameBits(Final, rb::EvaluateTrajectorySegment(R.Tracks[0].Segments.back(), R.StopTime + 1.0)));
	}
	// Non-finite times in the other entry points: clamped, never a crash or a non-finite orientation.
	Vec3 Tip;
	Vec3 Dir;
	rb::ShotResult WithTip = R;
	rb::CueTipSegment Piece;
	Piece.Path.Start = {-0.6, 0.0, 0.04};
	Piece.Path.Direction = {1.0, 0.0, 0.0};
	Piece.Path.Speed0 = 1.0;
	Piece.Path.Deceleration = 4.0;
	Piece.Path.StopTime = 0.25;
	Piece.T1 = 0.25;
	WithTip.CueTips.push_back(Piece);
	RB_CHECK(rb::CueTipAt(WithTip, 0, kNaN, Tip, Dir) && SameBits(Tip, Piece.Path.Start));
	RB_CHECK(rb::CueTipAt(WithTip, 0, rb::kInfinity, Tip, Dir) && std::isfinite(Tip.x));
	std::vector<rb::TrajectorySample> Out(4096);
	rb::ShotResult NanStop = R;
	NanStop.StopTime = kNaN;
	RB_CHECK(rb::SampleTrajectory(NanStop, 0, 0.01, Out.data(), static_cast<int>(Out.size())) == 1); // only t = 0
	const int BoundariesOnly = rb::SampleTrajectory(R, 0, 0.0, Out.data(), static_cast<int>(Out.size())); // no regular samples
	RB_CHECK(BoundariesOnly >= 2 && BoundariesOnly <= static_cast<int>(R.Tracks[0].Segments.size()) + 1);
	RB_CHECK(rb::SampleTrajectory(R, 0, kNaN, Out.data(), static_cast<int>(Out.size())) == BoundariesOnly);
	RB_CHECK(rb::SampleTrajectory(R, 0, -1.0, Out.data(), static_cast<int>(Out.size())) == BoundariesOnly);
	RB_CHECK(rb::SampleTrajectory(R, 0, rb::kInfinity, Out.data(), static_cast<int>(Out.size())) == BoundariesOnly);
}

// Extreme shots: masse spin (every grid step above the series switch), a jump shot into a pocket and a ball leaving the
// table keep A-PLAY-1 (bitwise orientation continuity at every boundary), A-PLAY-2 (cursor == random access), unit
// quaternions, a resting ball's orientation bit for bit, and determinism (a second evaluation is bitwise identical).
RB_TEST(Playback_Adv_ExtremeShotsKeepTheOrientationContract)
{
	const rb::ShotResult R = MakeExtremeResult();
	const rb::ShotResult Again = MakeExtremeResult();
	for (int b = 0; b < kExtremeBalls; ++b)
	{
		const std::vector<TrajectorySegment>& Segs = R.Tracks[b].Segments;
		for (std::size_t k = 0; k + 1 < Segs.size(); ++k)
		{
			Quat Q;
			RB_REQUIRE(rb::OrientationAt(R, b, Segs[k].T1, Q));
			RB_CHECK(SameBits(Q, Segs[k + 1].Orientation0));
			RB_CHECK(rb::Length(rb::EvaluateTrajectorySegment(Segs[k], Segs[k].T1).Position - rb::EvaluateTrajectorySegment(Segs[k + 1], Segs[k + 1].Motion.T0).Position) < 1e-12);
		}
		rb::PlaybackCursor Cursor;
		for (double T = 0.0; T <= R.StopTime + 0.5; T += 1.0 / 144.0)
		{
			BallState A;
			BallState B;
			Quat QA;
			Quat QB;
			Quat QC;
			RB_REQUIRE(rb::StateAtCursor(R, Cursor, b, T, A, QA));
			RB_REQUIRE(rb::StateAt(R, b, T, B));
			RB_REQUIRE(rb::OrientationAt(R, b, T, QB));
			RB_REQUIRE(rb::OrientationAt(Again, b, T, QC));
			RB_CHECK(SameBits(A, B) && SameBits(QA, QB) && SameBits(QB, QC));
			RB_CHECK(std::fabs(rb::NormSquared(QA) - 1.0) < 1e-13);
		}
	}
	// The masse really exercises large grid steps, and the law converges to the w laws it integrates.
	const TrajectorySegment& Masse = R.Tracks[0].Segments[0];
	RB_REQUIRE(Masse.Motion.State == MotionState::Sliding);
	const BallState Start = rb::EvaluateTrajectorySegment(Masse, 0.0);
	RB_CHECK(rb::Length(Start.Omega) * rb::kOrientationSubstep > 0.25);
	const double Tau = Masse.T1 - Masse.Motion.T0;
	const Quat Reference = ReferenceOrientation(Masse.Orientation0, Masse, Tau, 400000);
	// Exact-integral steps leave the Magnus remainder, about (h^2 / 12) * integral of |w x dw/dt| for a step h.
	double Commutator = 0.0;
	const int Panels = 20000;
	for (int k = 0; k < Panels; ++k)
	{
		const double T = (static_cast<double>(k) + 0.5) * Tau / Panels;
		const Vec3 W = rb::EvaluateTrajectorySegment(Masse, T).Omega;
		const Vec3 Dw = (rb::EvaluateTrajectorySegment(Masse, T + 1e-6).Omega - rb::EvaluateTrajectorySegment(Masse, T - 1e-6).Omega) / 2e-6;
		Commutator += rb::Length(rb::Cross(W, Dw)) * Tau / Panels;
	}
	const double ErrFine = AngleBetween(rb::SegmentOrientationAt(Masse.Orientation0, Masse, Tau, 1.0e-5), Reference);
	const double ErrLaw = AngleBetween(rb::SegmentOrientationAt(Masse.Orientation0, Masse, Tau), Reference);
	RB_CHECK(ErrFine <= 1.0e-10 / 12.0 * Commutator + 1e-7);
	RB_CHECK(ErrLaw <= 1.0e-6 / 12.0 * Commutator + 1e-7);
	RB_CHECK(ErrLaw < 2e-2); // about 1 deg over the whole masse slide: invisible
	std::printf("  masse: slide %.3f s, |w0| %.0f rad/s, law error %.2e rad (h 1 ms), %.2e rad (h 10 us), commutator bound %.2e / %.2e\n", Tau,
		rb::Length(Start.Omega), ErrLaw, ErrFine, 1.0e-6 / 12.0 * Commutator, 1.0e-10 / 12.0 * Commutator);
	// Airborne / PocketFall are closed forms about the constant w; the terminal and the resting ball never turn.
	for (const TrajectorySegment& S : R.Tracks[1].Segments)
	{
		if (S.Motion.State == MotionState::Airborne || S.Motion.State == MotionState::PocketFall)
		{
			RB_CHECK(rb::HasConstantRotationAxis(S));
		}
	}
	Quat Rest;
	RB_REQUIRE(rb::OrientationAt(R, 3, 123.0, Rest));
	RB_CHECK(SameBits(Rest, R.Tracks[3].Segments[0].Orientation0));
	Quat Gone;
	Quat GoneLater;
	RB_REQUIRE(rb::OrientationAt(R, 2, 0.12, Gone) && rb::OrientationAt(R, 2, 50.0, GoneLater));
	RB_CHECK(SameBits(Gone, GoneLater));
	BallState Off;
	RB_REQUIRE(rb::StateAt(R, 2, 50.0, Off));
	RB_CHECK(Off.State == MotionState::OffTable && SameBits(Off.Velocity, Vec3::Zero()));
}

// Code rule / VAL P4: no heap allocation in the per-frame playback path and in what the event loop calls per event
// (orientation law, record appends, frozen predicate) or once per shot (Begin / Finish). MSVC Debug CRT hook; other builds
// only run the calls.
RB_TEST(Playback_Adv_NoHeapAllocationInHotPaths)
{
	const rb::ShotResult R = MakeExtremeResult();
	const std::unique_ptr<rb::TableGeometry> G = std::make_unique<rb::TableGeometry>();
	RB_REQUIRE(rb::BuildTableGeometry(rb::kTableNineFootPro, *G) == rb::ErrorCode::Ok);
	const std::unique_ptr<rb::SimInput> In = std::make_unique<rb::SimInput>();
	In->Table = G.get();
	In->Params = rb::MakePhysicsParams(G->Spec);
	for (int b = 0; b < 3; ++b)
	{
		In->Balls[b].InPlay = true;
		In->Balls[b].State.Position = {-0.3 + 0.2 * b, 0.1, In->Balls[b].Spec.Radius};
	}
	In->Strikes.PushBack(rb::StrikeRequest{});
	const std::unique_ptr<rb::ShotResult> Sim = std::make_unique<rb::ShotResult>();
	rb::ReserveShotResult(*Sim, rb::ResultCapacity{});
	rb::ResetShotResult(*Sim);
	Sim->Status = rb::SimStatus::Ok;
	Sim->StopTime = 1.0;
	for (int b = 0; b < 3; ++b)
	{
		Sim->Finals[b].Status = rb::BallFinalStatus::OnTable;
		Sim->Finals[b].State = In->Balls[b].State;
	}
	rb::ShotEvent Events[4];
	Events[0].Type = rb::ShotEventType::TipContactBegin;
	Events[0].Feature = 0;
	Events[0].A = 0;
	Events[1] = Events[0];
	Events[1].Type = rb::ShotEventType::TipContactEnd;
	Events[1].Time = 1.0e-3;
	Events[2].Type = rb::ShotEventType::BallBall;
	Events[2].Time = 0.3;
	Events[2].A = 0;
	Events[2].B = 1;
	Events[3].Type = rb::ShotEventType::MotionTransition;
	Events[3].Time = 0.2;
	Events[3].A = 0;
	std::vector<rb::TrajectorySample> Samples(8192);
	double Radii[3] = {0.028575, 0.028575, 0.028575};

	double Sink = 0.0;
#if RB_PLAYBACK_TEST_ALLOC_HOOK
	g_PlaybackTestAllocations = 0;
	const _CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(&CountAllocations);
#endif
	rb::PlaybackCursor Cursor;
	for (int b = 0; b < kExtremeBalls; ++b)
	{
		for (double T = -0.1; T <= R.StopTime + 0.2; T += 1.0 / 60.0)
		{
			BallState S;
			Quat Q;
			rb::StateAtCursor(R, Cursor, b, T, S, Q);
			rb::StateAt(R, b, 0.7 * T, S);
			rb::OrientationAt(R, b, 0.7 * T, Q);
			Sink += S.Position.x + Q.w;
		}
		for (const TrajectorySegment& S : R.Tracks[b].Segments)
		{
			Sink += rb::SegmentOrientationAt(S.Orientation0, S, S.T1 - S.Motion.T0).x;
		}
		Sink += static_cast<double>(rb::SampleTrajectory(R, b, 0.01, Samples.data(), static_cast<int>(Samples.size())));
	}
	Vec3 Tip;
	Vec3 Dir;
	rb::CueTipAt(R, 0, 0.1, Tip, Dir);
	rb::BeginShotRecord(*In, Sim->Record);
	for (const rb::ShotEvent& E : Events)
	{
		rb::AppendRecordEvent(E, Sim->Record);
	}
	rb::FinishShotRecord(*In, *Sim, Sim->Record);
	Sink += static_cast<double>(rb::FrozenRailFeatures(*G, In->Params.Cushion, {1.2, 0.0}, 0.028575, 1e-4));
	const rb::rules::RulesTable Table = rb::BuildRulesTable(*G, 0.028575, Radii, 3);
	Sink += Table.HeadStringX;
#if RB_PLAYBACK_TEST_ALLOC_HOOK
	_CrtSetAllocHook(Previous);
	RB_CHECK(g_PlaybackTestAllocations == 0);
#endif
	RB_CHECK(std::isfinite(Sink));
	RB_CHECK(Sim->Record.Events.size() == 4u && Sim->Record.Stroke.TipContacts.Size() == 1);
}
