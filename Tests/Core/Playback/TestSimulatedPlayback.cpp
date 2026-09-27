// Integration of playback and the record builder with the real event loop (needs WP-6a / WP-6b): the simulator's
// recorded tracks obey the orientation law at every boundary (A-PLAY-1), cursor playback equals random access on them
// (A-PLAY-2), and the record built during Run equals the standalone rebuild from the complete log (A-REC-1) (WP-7).

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/Playback.h"
#include "rb/Physics/Simulator.h"
#include "rb/Shot/ShotRecordBuilder.h"

#include <memory>

using namespace playtest;
using rb::BallState;
using rb::Quat;
using rb::TrajectorySegment;

namespace
{
	struct Shot
	{
		rb::TableGeometry Geometry;
		rb::SimInput Input;
		rb::ShotResult Result;
	};

	// Draw with English into an object ball near the foot rail, a third ball in the way of the cue ball's return.
	std::unique_ptr<Shot> RunShot()
	{
		std::unique_ptr<Shot> S = std::make_unique<Shot>();
		rb::BuildTableGeometry(rb::kTableNineFootPro, S->Geometry);
		rb::SimInput& In = S->Input;
		In.Table = &S->Geometry;
		In.Params = rb::MakePhysicsParams(S->Geometry.Spec);
		const double Positions[3][2] = {{-0.6, 0.05}, {0.4, 0.12}, {-0.2, -0.3}};
		for (int b = 0; b < 3; ++b)
		{
			rb::SimBall& B = In.Balls[b];
			B.InPlay = true;
			B.Spec = rb::kStandardPoolBall;
			B.State.Position = {Positions[b][0], Positions[b][1], B.Spec.Radius};
			B.Orientation = rb::Normalized(Quat{1.0, 0.1 * b, -0.2, 0.05});
		}
		rb::StrikeRequest Strike;
		Strike.Ball = 0;
		Strike.Input.Speed = 3.5;
		Strike.Input.Azimuth = 0.07;
		Strike.Input.Elevation = 0.08;
		Strike.Input.OffsetA = 0.3;
		Strike.Input.OffsetB = -0.35;
		In.Strikes.PushBack(Strike);
		rb::Simulator Sim;
		Sim.Run(In, S->Result);
		return S;
	}
}

RB_TEST(Integ_ARCH_PLAY1_SimulatedTracksFollowTheOrientationLaw)
{
	const std::unique_ptr<Shot> S = RunShot();
	const rb::ShotResult& R = S->Result;
	RB_REQUIRE(R.Status == rb::SimStatus::Ok);
	int Boundaries = 0;
	for (int b = 0; b < 3; ++b)
	{
		const std::vector<TrajectorySegment>& Segs = R.Tracks[b].Segments;
		RB_REQUIRE(!Segs.empty());
		RB_CHECK(SameBits(Segs[0].Orientation0, S->Input.Balls[b].Orientation));
		for (std::size_t k = 0; k + 1 < Segs.size(); ++k)
		{
			const TrajectorySegment& A = Segs[k];
			const TrajectorySegment& B = Segs[k + 1];
			RB_CHECK(SameBits(A.T1, B.Motion.T0));
			RB_CHECK(SameBits(rb::SegmentOrientationAt(A.Orientation0, A, A.T1 - A.Motion.T0), B.Orientation0));
			const BallState End = rb::EvaluateTrajectorySegment(A, A.T1);
			const BallState Next = rb::EvaluateTrajectorySegment(B, B.Motion.T0);
			RB_CHECK(rb::Length(End.Position - Next.Position) < 1e-9);
			++Boundaries;
		}
		// Cursor playback equals random access on the simulator's tracks.
		rb::PlaybackCursor Cursor;
		for (double T = 0.0; T <= R.StopTime + 0.1; T += 1.0 / 120.0)
		{
			BallState A;
			BallState B;
			Quat QA;
			Quat QB;
			RB_REQUIRE(rb::StateAtCursor(R, Cursor, b, T, A, QA));
			RB_REQUIRE(rb::StateAt(R, b, T, B));
			RB_REQUIRE(rb::OrientationAt(R, b, T, QB));
			RB_CHECK(SameBits(A, B) && SameBits(QA, QB));
		}
		Quat Last;
		RB_REQUIRE(rb::OrientationAt(R, b, R.StopTime + 1.0, Last));
		RB_CHECK(AngleBetween(Last, R.Finals[b].Orientation) < 1e-12);
	}
	RB_CHECK(Boundaries >= 6);
	rb::Vec3 Tip;
	rb::Vec3 Dir;
	RB_CHECK(rb::CueTipAt(R, 0, 0.0, Tip, Dir));
}

RB_TEST(Integ_ARCH_REC1_RecordDuringRunEqualsStandaloneRebuild)
{
	const std::unique_ptr<Shot> S = RunShot();
	const rb::ShotResult& R = S->Result;
	RB_REQUIRE(R.Status == rb::SimStatus::Ok);
	RB_REQUIRE(!R.Diagnostics.EventLogOverflow);
	rb::ShotRecord Rebuilt;
	rb::BuildShotRecord(S->Input, R, Rebuilt);
	RB_CHECK(!Rebuilt.Truncated && !R.Record.Truncated);
	RB_REQUIRE(Rebuilt.Events.size() == R.Record.Events.size());
	for (std::size_t i = 0; i < Rebuilt.Events.size(); ++i)
	{
		const rb::RecordEvent& A = Rebuilt.Events[i];
		const rb::RecordEvent& B = R.Record.Events[i];
		RB_CHECK(A.Type == B.Type && A.A == B.A && A.B == B.B && SameBits(A.Time, B.Time) && A.Feature == B.Feature && A.Side == B.Side);
		RB_CHECK(SameBits(A.PositionA.x, B.PositionA.x) && SameBits(A.PositionA.y, B.PositionA.y));
	}
	RB_CHECK(Rebuilt.Stroke.TipContacts.Size() == R.Record.Stroke.TipContacts.Size());
	RB_REQUIRE(R.Record.Stroke.TipContacts.Size() >= 1);
	RB_CHECK(R.Record.Stroke.TipContacts[0].Ball == 0 && R.Record.Stroke.TipContacts[0].Start == 0.0);
	RB_CHECK(R.Record.End.StopTime == R.StopTime);
}
