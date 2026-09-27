// Owner: WP-6a (simulator core loop). Adversarial review tests of the event loop (WP-6a review): the tip slot never predicts the
// past, input validation of orientations and chalk marks, pair observers against island members, the orientation of a ball leaving an
// island, the event cap with an exact-simultaneity group, the record's overflow point, no heap allocation inside Run, bitwise
// determinism under object-ball id permutations, extreme shots (termination, finite states, a time-ordered log), a ball resting on a
// pocket lip.

#include "rbtest.h"

#include "Simulator/SimTestUtil.h"

#include "../../../Source/BilliardsCore/Private/rb/Physics/SimInternal.h"

#include "rb/Core/Random.h"
#include "rb/Physics/Playback.h"

#include <cmath>
#include <limits>

#if defined(_MSC_VER) && defined(_DEBUG)
	#include <crtdbg.h>
	#define RB_SIMREVIEW_ALLOC_HOOK 1
#else
	#define RB_SIMREVIEW_ALLOC_HOOK 0
#endif

using namespace rb;
using namespace simtest;

namespace
{
	bool Finite(const BallState& S)
	{
		return std::isfinite(S.Position.x) && std::isfinite(S.Position.y) && std::isfinite(S.Position.z) && std::isfinite(S.Velocity.x) &&
			std::isfinite(S.Velocity.y) && std::isfinite(S.Velocity.z) && std::isfinite(S.Omega.x) && std::isfinite(S.Omega.y) && std::isfinite(S.Omega.z);
	}

	bool SameBits(const Vec3& A, const Vec3& B) { return Bits(A.x) == Bits(B.x) && Bits(A.y) == Bits(B.y) && Bits(A.z) == Bits(B.z); }

	bool SameBits(const BallState& A, const BallState& B)
	{
		return SameBits(A.Position, B.Position) && SameBits(A.Velocity, B.Velocity) && SameBits(A.Omega, B.Omega) && A.State == B.State;
	}

	// A loop workspace driven directly (the services of SimInternal.h), like TestSimServices.cpp.
	sim::Workspace& ReviewWorkspace(const SimInput& In, ShotResult& R)
	{
		static sim::Workspace Ws;
		Ws.Input = &In;
		Ws.Result = &R;
		Ws.Params = In.Params;
		Ws.Params.Cli.TsujiAlpha = 0.03689;
		Ws.Queue.Clear();
		Ws.Zeno.Clear();
		Ws.Island.Active = false;
		for (sim::BallSlot& Slot : Ws.Balls)
		{
			Slot = sim::BallSlot{};
		}
		for (sim::TipSlot& Tip : Ws.Tips)
		{
			Tip = sim::TipSlot{};
		}
		Ws.Now = 0.0;
		Ws.EventsProcessed = 0;
		Ws.Caps = ResultCapacity{};
		Ws.Cache = sim::LoopCache{};
		ReserveShotResult(R, Ws.Caps);
		ResetShotResult(R);
		return Ws;
	}

	// A ball of the workspace in event mode with its initial segment (t = 0).
	void Activate(sim::Workspace& Ws, const SimInput& In, int Ball)
	{
		Ws.Balls[Ball].InPlay = true;
		Ws.Balls[Ball].Orientation0 = In.Balls[Ball].Orientation;
		sim::loop::SetSegment(Ws, Ball, In.Balls[Ball].State, 0.0, true);
	}

	// B1-style layout (prior-art 7.5): cue ball plus 2..10 object balls at seeded free positions away from the pockets.
	int B1Layout(Rng& Random, const TableGeometry& T, Vec3* Pos)
	{
		const int Balls = 3 + static_cast<int>(Random.NextBelow(9));
		int Placed = 0;
		for (int Attempt = 0; Attempt < 2000 && Placed < Balls; ++Attempt)
		{
			const Vec3 P{Random.NextUniform(-T.HalfLength + 0.06, T.HalfLength - 0.06), Random.NextUniform(-T.HalfWidth + 0.06, T.HalfWidth - 0.06), kR};
			bool Free = true;
			for (int j = 0; j < Placed && Free; ++j)
			{
				Free = Length(P - Pos[j]) > 2.0 * kR + 0.005;
			}
			for (int p = 0; p < T.Pockets.Size() && Free; ++p)
			{
				Free = Length(XY(P) - T.Pockets[p].MouthMid) > 0.12;
			}
			if (Free)
			{
				Pos[Placed++] = P;
			}
		}
		return Placed;
	}

#if RB_SIMREVIEW_ALLOC_HOOK
	long& AllocationCount()
	{
		static long Count = 0;
		return Count;
	}

	int CountAllocations(int AllocType, void*, size_t, int, long, const unsigned char*, int)
	{
		if (AllocType == _HOOK_ALLOC || AllocType == _HOOK_REALLOC)
		{
			++AllocationCount();
		}
		return 1;
	}
#endif
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Tip slot: the search starts at Now. Found by the review: a tip re-contact that ResolveTipRecontact answered with no impulse (common
// right after an elevated follow stroke, when the hopping ball meets the descending tip) was found again by every later re-prediction
// of the unchanged path, i.e. queued BEFORE Now (event time running backwards, the no-impulse contact re-processed after every event
// of any other ball).
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_TipSlotNeverPredictsThePast)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 1, {0.3, 0.0, kR});
	ShotResult& R = ResultSlot();
	sim::Workspace& Ws = ReviewWorkspace(In, R);
	Activate(Ws, In, 1);
	sim::TipSlot& Tip = Ws.Tips[0];
	Tip.Path.StruckBall = 0;
	Tip.Path.Start = {0.0, 0.0, kR};
	Tip.Path.Direction = {1.0, 0.0, 0.0};
	Tip.Path.Speed0 = 1.0;
	Tip.Path.Deceleration = 0.5;
	Tip.Path.StartTime = 0.0;
	Tip.Path.StopTime = 2.0;
	Tip.Path.DomeRadius = kCuePlaying19oz.TipDomeRadius;
	Tip.Moving = true;
	Ws.Queue.Clear();

	// At t = 0 the tip reaches the ball at t_c (the path's own window).
	sim::loop::PredictTips(Ws, -1, 0.0);
	RB_REQUIRE(!Ws.Queue.IsEmpty());
	const double Contact = Ws.Queue.Top().Time;
	RB_REQUIRE(Contact > 0.2 && Contact < 0.3);

	// Later (the contact was processed without an impulse and another ball's event re-predicts the tip): nothing before Now.
	Ws.Queue.Clear();
	Ws.Now = Contact + 0.004;
	sim::loop::PredictTips(Ws, -1, 0.0);
	while (!Ws.Queue.IsEmpty())
	{
		RB_CHECK(Ws.Queue.Top().Time >= Ws.Now);
		Ws.Queue.Pop();
	}
}

// The same in a whole shot: an elevated follow stroke whose hopping cue ball meets the descending tip at t = 0 without an impulse
// (WP-1's re-contact model), and a far ball with its own events meanwhile. The two balls never interact, so the shot processes exactly
// the events of the two single-ball shots (before the fix the unchanged cue-tip path re-queued the t = 0 contact after every event of
// the other ball).
RB_TEST(Sim_Review_DisjointBallsDoNotReplayTipContacts)
{
	const TableGeometry& T = NineFoot();
	const StrikeRequest Stroke = Strike(0, 1.914741294123496, 0.0026904321146999366, 0.081169011598144353, -0.48624040165501614, 0.4960359092702078);
	const double Y = T.HalfWidth - ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset - 0.005;
	Simulator Sim;
	int Events[3] = {};
	for (int Run = 0; Run < 3; ++Run)
	{
		SimInput& In = NewInput(T, ColParams());
		if (Run != 1)
		{
			Place(In, 0, {-0.4, 0.0, kR});
			In.Strikes.PushBack(Stroke);
		}
		if (Run != 0)
		{
			// Into the side cushion within 5 ms, then transitions. (Integration round 2: at (0.9, Y) moving (0.3, 1.0) the ball met
			// the cue ball after its foot-rail rebound once the real landing physics ran; at the head end of the other side rail it
			// stays > 0.2 m from every point of the cue ball's path.)
			PlaceRolling(In, 1, {-0.9, -Y, kR}, {-0.1, -0.5, 0.0});
		}
		ShotResult& R = ResultSlot();
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		Events[Run] = R.Diagnostics.EventsProcessed;
		if (Run == 0)
		{
			RB_REQUIRE(R.Strikes[0].Result.State.State == MotionState::Airborne); // the hop that meets the tip
		}
	}
	RB_CHECK(Events[0] >= 1 && Events[1] >= 3);
	RB_CHECK(Events[2] == Events[0] + Events[1]);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Input validation: a non-finite orientation (playback, the cling) and non-finite chalk marks (read with ChalkCling) are rejected.
// Found by the review: a NaN orientation or mark strength made the contact's cling factor NaN, the impulse NaN, and ClassifyState then
// stopped both balls dead at the contact - status Ok with a wrong result.
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_NonFiniteOrientationAndChalkMarksRejected)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.ChalkCling = true;
	const auto Setup = [&]() -> SimInput&
	{
		SimInput& In = NewInput(T, P);
		PlaceRolling(In, 0, {-0.5, 0.0, kR}, {1.0, 0.0, 0.0});
		Place(In, 1, {-0.2, -0.01, kR});
		In.Balls[1].ChalkMarks.PushBack(ChalkMark{{-1.0, 0.0, 0.0}, 1.0, 2.5e-3});
		return In;
	};
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(Setup(), R) == SimStatus::Ok);
	RB_CHECK(Count(R, ShotEventType::BallBall) == 1 && R.Finals[1].State.Position.x > 0.0); // the object ball is driven on

	SimInput* In = &Setup();
	In->Balls[1].Orientation = Quat{std::nan(""), 0.0, 0.0, 0.0};
	RB_CHECK(Sim.Run(*In, R) == SimStatus::InvalidInput && R.Diagnostics.InputError == ErrorCode::InvalidState);
	In = &Setup();
	In->Balls[0].Orientation = Quat{0.0, 0.0, 0.0, 0.0};
	RB_CHECK(Sim.Run(*In, R) == SimStatus::InvalidInput && R.Diagnostics.InputError == ErrorCode::InvalidState);
	In = &Setup();
	In->Balls[1].ChalkMarks[0].Strength = std::nan("");
	RB_CHECK(Sim.Run(*In, R) == SimStatus::InvalidInput && R.Diagnostics.InputError == ErrorCode::InvalidArgument);
	In = &Setup();
	In->Balls[1].ChalkMarks[0].BodyDir.y = std::numeric_limits<double>::infinity();
	RB_CHECK(Sim.Run(*In, R) == SimStatus::InvalidInput && R.Diagnostics.InputError == ErrorCode::InvalidArgument);
	In = &Setup();
	In->Balls[1].ChalkMarks[0].Radius = std::nan("");
	RB_CHECK(Sim.Run(*In, R) == SimStatus::InvalidInput && R.Diagnostics.InputError == ErrorCode::InvalidArgument);
	// Marks are read only with ChalkCling: without it a bad mark is ignored (never read).
	In = &Setup();
	In->Params.ChalkCling = false;
	In->Balls[1].ChalkMarks[0].Strength = std::nan("");
	RB_CHECK(Sim.Run(*In, R) == SimStatus::Ok);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Pair observers of an event-mode ball against a partner inside an island are void. Found by the review: they were predicted against
// the partner's pre-island segment and still emitted (BallJumpedOver at a stale time, a ball freeze-leave clearing continuesInitialFreeze
// at a stale time).
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_PairObserversVoidWhilePartnerInIsland)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.3, 0.0, kR + 0.02}, {1.0, 0.0, 0.5});
	Place(In, 1, {0.0, 0.0, kR});
	Place(In, 2, {0.4, 0.2, kR}, {0.5, 0.0, 0.0});
	Place(In, 3, {0.4 + 2.0 * kR + 5e-5, 0.2, kR});
	ShotResult& R = ResultSlot();
	sim::Workspace& Ws = ReviewWorkspace(In, R);
	for (int b = 0; b < 4; ++b)
	{
		Activate(Ws, In, b);
	}
	// Ball 0 flies over ball 1 (plan overlap entered: a pending JumpLeave); ball 2 was frozen to ball 3 at t = 0.
	sim::BallSlot& Jumper = Ws.Balls[0];
	Jumper.Observers.Clear();
	Jumper.JumpPending = 1u << 1;
	Jumper.Observers.PushBack(sim::Observer{0.05, sim::ObserverKind::JumpLeave, 1, 0});
	sim::BallSlot& Leaver = Ws.Balls[2];
	Leaver.Observers.Clear();
	Leaver.InitialFreezeBalls = 1u << 3;
	Ws.Balls[3].InitialFreezeBalls = 1u << 2;
	Leaver.Observers.PushBack(sim::Observer{0.06, sim::ObserverKind::FreezeLeave, static_cast<std::uint8_t>(32 + 3), 0});
	for (int b = 0; b < kMaxBalls; ++b)
	{
		if (b != 0 && b != 2)
		{
			Ws.Balls[b].Observers.Clear();
		}
	}
	// The partners join an island (WP-6b's StartIsland): their event-mode segments are gone.
	Ws.Balls[1].InIsland = true;
	Ws.Balls[3].InIsland = true;
	sim::loop::EmitPending(Ws, 1.0, true);
	RB_CHECK(Count(R, ShotEventType::BallJumpedOver) == 0);
	RB_CHECK(Jumper.JumpPending == (1u << 1));             // still over the partner: resolved when it leaves the island
	RB_CHECK(Leaver.InitialFreezeBalls == (1u << 3));      // not left at a stale time
	RB_CHECK(Ws.Balls[3].InitialFreezeBalls == (1u << 2));

	// Without the island the same observers are emitted.
	Ws.Balls[1].InIsland = false;
	Ws.Balls[3].InIsland = false;
	Jumper.Observers.PushBack(sim::Observer{0.05, sim::ObserverKind::JumpLeave, 1, 0});
	Leaver.Observers.PushBack(sim::Observer{0.06, sim::ObserverKind::FreezeLeave, static_cast<std::uint8_t>(32 + 3), 0});
	sim::loop::EmitPending(Ws, 1.0, true);
	RB_CHECK(Count(R, ShotEventType::BallJumpedOver) == 1);
	RB_CHECK(Jumper.JumpPending == 0u && Leaver.InitialFreezeBalls == 0u && Ws.Balls[3].InitialFreezeBalls == 0u);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Leaving an island: the open Sampled segment is closed with Omega0 = RotationAccumulator / duration (BallSlot), so the orientation law
// rotates it by the accumulated rotation. Found by the review: the loop closed it with T1 and EndPosition only (Omega0 left as the
// island opened it), and without a recorded track it returned the orientation at SampleStart as the orientation at the exit.
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_IslandExitClosesSampledPieceWithAccumulatedRotation)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	PlaceRolling(In, 0, {-0.5, 0.0, kR}, {1.0, 0.0, 0.0});
	ShotResult& R = ResultSlot();
	sim::Workspace& Ws = ReviewWorkspace(In, R);
	Activate(Ws, In, 0);
	sim::BallSlot& Slot = Ws.Balls[0];
	// WP-6b at island start (t = 0.1): the analytic segment is closed and an open Sampled piece started; the ball is a member.
	const double Start = 0.1;
	const Quat Q0 = sim::loop::CloseOpenSegment(Ws, 0, Start, sim::BallStateAt(Ws, 0, Start).Position);
	TrajectorySegment Open;
	Open.Kind = SegmentKind::Sampled;
	Open.Motion.State = MotionState::Rolling;
	Open.Motion.T0 = Start;
	Open.Motion.Pos0 = sim::BallStateAt(Ws, 0, Start).Position;
	Open.T1 = kInfinity;
	Open.Orientation0 = Q0;
	R.Tracks[0].Segments.push_back(Open);
	Slot.InIsland = true;
	Slot.SampleStart = Start;
	Slot.Orientation0 = Q0;
	Slot.RotationAccumulator = {0.3, -0.2, 1.1}; // integral of w over the island stretch [rad]
	// The member leaves at t = 0.12 (ReplaceSegment while still InIsland).
	const double Exit = 0.12;
	BallState Leave;
	Leave.Position = Open.Motion.Pos0 + Vec3{0.02, 0.0, 0.0};
	Leave.Velocity = {0.8, 0.0, 0.0};
	Leave.Omega = RollingOmegaH(Leave.Velocity, kR);
	Leave.State = MotionState::Rolling;
	sim::ReplaceSegment(Ws, 0, Leave, Exit);
	const auto& Track = R.Tracks[0].Segments;
	RB_REQUIRE(Track.size() >= 3);
	const TrajectorySegment& Closed = Track[Track.size() - 2];
	RB_REQUIRE(Closed.Kind == SegmentKind::Sampled);
	RB_CHECK(Closed.T1 == Exit && Closed.EndPosition == Leave.Position);
	RB_CHECK(SameBits(Closed.Motion.Omega0, Slot.RotationAccumulator / (Exit - Start)));
	RB_CHECK(!Slot.InIsland);
	RB_CHECK(Track.back().Orientation0 == SegmentOrientationAt(Q0, Closed, Exit - Start)); // continuous at the boundary
}

// Without trajectories the orientation at the exit is the same law applied to Orientation0 (bitwise the recorded result): the marked
// ball's cling after an island and the Finals orientation (WP-7's orientation law).
RB_TEST(Integ_Sim_Review_IslandExitOrientationWithoutTrack)
{
	const TableGeometry& T = NineFoot();
	Quat Exits[2];
	for (int Recorded = 0; Recorded < 2; ++Recorded)
	{
		PhysicsParams P = ColParams();
		P.ChalkCling = true;
		SimInput& In = NewInput(T, P);
		In.Record.Trajectories = Recorded == 1;
		PlaceRolling(In, 0, {-0.5, 0.0, kR}, {1.0, 0.0, 0.0});
		In.Balls[0].ChalkMarks.PushBack(ChalkMark{{0.0, 0.0, 1.0}, 1.0, 2.5e-3}); // a marked ball: its orientation is kept
		ShotResult& R = ResultSlot();
		sim::Workspace& Ws = ReviewWorkspace(In, R);
		Activate(Ws, In, 0);
		sim::BallSlot& Slot = Ws.Balls[0];
		const double Start = 0.1;
		const Quat Q0 = sim::loop::CloseOpenSegment(Ws, 0, Start, sim::BallStateAt(Ws, 0, Start).Position);
		if (Recorded == 1)
		{
			TrajectorySegment Open;
			Open.Kind = SegmentKind::Sampled;
			Open.Motion.State = MotionState::Rolling;
			Open.Motion.T0 = Start;
			Open.Motion.Pos0 = sim::BallStateAt(Ws, 0, Start).Position;
			Open.T1 = kInfinity;
			Open.Orientation0 = Q0;
			R.Tracks[0].Segments.push_back(Open);
		}
		Slot.InIsland = true;
		Slot.SampleStart = Start;
		Slot.Orientation0 = Q0;
		Slot.RotationAccumulator = {0.3, -0.2, 1.1};
		BallState Leave;
		Leave.Position = {-0.38, 0.0, kR};
		Leave.State = MotionState::Stationary;
		sim::ReplaceSegment(Ws, 0, Leave, 0.12);
		Exits[Recorded] = Slot.Orientation0;
		RB_CHECK(!(Exits[Recorded] == Q0)); // rotated by the accumulated 1.2 rad
	}
	RB_CHECK(Exits[0] == Exits[1]);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Record positions: every record-relevant event carries the state of A (and of B when B is a ball) at the event, whatever
// RecordOptions::EventStates - WP-7's record takes PositionA / PositionB from them, and the rules use the last one as the FinalPosition
// of a pocketed, off-table or late-dropping ball (rules F13). Found by the review: tip-contact events carried no state at all, and
// line crossings and motion transitions only with EventStates (so the record depended on a logging switch; A-SIM-5).
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
	// Tip events with a target f (a 3 mm gap counts as frozen with eps_frozen = 5 mm; 80 deg off the stroke, so neither the cue ball
	// nor the tip touches it), rail contacts, line crossings and transitions.
	void RecordPositionShot(SimInput& In)
	{
		const double Azimuth = 1.2;
		const double Side = Azimuth - 80.0 * kDegToRad;
		const double Distance = 2.0 * kR + 3e-3;
		In.Context.FrozenTolerance = 5e-3;
		Place(In, 0, {-0.6, 0.0, kR});
		Place(In, 1, {-0.6 + Distance * std::cos(Side), Distance * std::sin(Side), kR});
		Place(In, 2, {0.7, -0.3, kR});
		In.Strikes.PushBack(Strike(0, 1.5, Azimuth));
	}
}

RB_TEST(Sim_Review_RecordRelevantEventsCarryPositions)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	RecordPositionShot(In);
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	int Checked[4] = {};
	for (const ShotEvent& E : R.Events)
	{
		const bool Tip = E.Type == ShotEventType::TipContactBegin || E.Type == ShotEventType::TipContactEnd;
		const bool Observer = E.Type == ShotEventType::BallLineCross || E.Type == ShotEventType::MotionTransition;
		if (!Tip && !Observer)
		{
			continue;
		}
		BallState OnTrack;
		RB_REQUIRE(TrackState(R, E.A, E.Time, OnTrack));
		RB_CHECK(Length(E.Pre[0].Position - OnTrack.Position) <= 1e-12);
		if (E.B != kNoBall)
		{
			BallState Other;
			RB_REQUIRE(TrackState(R, E.B, E.Time, Other));
			RB_CHECK(Length(E.Pre[1].Position - Other.Position) <= 1e-12);
			++Checked[3];
		}
		++Checked[E.Type == ShotEventType::TipContactBegin ? 0 : (E.Type == ShotEventType::TipContactEnd ? 1 : 2)];
	}
	RB_CHECK(Checked[0] == 1 && Checked[1] == 1 && Checked[2] >= 3 && Checked[3] == 2);
}

// The record's positions are identical with and without EventStates (WP-7's record builder).
RB_TEST(Integ_Sim_Review_RecordPositionsIndependentOfEventStates)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	ShotResult* Runs[2] = {&ResultSlot(0), &ResultSlot(1)};
	for (int Run = 0; Run < 2; ++Run)
	{
		SimInput& In = NewInput(T, ColParams());
		RecordPositionShot(In);
		In.Record.EventStates = Run == 0;
		RB_REQUIRE(Sim.Run(In, *Runs[Run]) == SimStatus::Ok);
	}
	const auto& A = Runs[0]->Record.Events;
	const auto& B = Runs[1]->Record.Events;
	RB_REQUIRE(A.size() == B.size() && A.size() > 6u);
	int Nonzero = 0;
	for (std::size_t k = 0; k < A.size(); ++k)
	{
		RB_CHECK(A[k].Type == B[k].Type && A[k].PositionA == B[k].PositionA && A[k].PositionB == B[k].PositionB);
		Nonzero += A[k].PositionA == Vec2{} ? 0 : 1;
	}
	RB_CHECK(Nonzero == static_cast<int>(A.size()));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Event cap with an exact-simultaneity group (guard 9): every member is a processed event, so a group that does not fit under
// MaxEvents is not handed off - the shot stops at the cap. Found by the review: the members were counted without the check, so a shot
// could end Ok with EventsProcessed > MaxEvents.
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_SimultaneityGroupRespectsEventCap)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.Numerics.MaxEvents = 1;
	SimInput& In = NewInput(T, P);
	PlaceRolling(In, 0, {-0.3, 0.2, kR}, {1.0, 0.0, 0.0});
	Place(In, 1, {0.0, 0.2, kR});
	PlaceRolling(In, 2, {0.3, 0.2, kR}, {-1.0, 0.0, 0.0});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Aborted);
	RB_CHECK(R.Diagnostics.EventsProcessed == 1);
	RB_CHECK(R.Diagnostics.IslandHandOffs == 0);
	for (int b = 0; b < 3; ++b)
	{
		RB_CHECK(R.Finals[b].State.State == MotionState::Stationary && R.Finals[b].Time == R.StopTime);
	}
	// With room for both members the group is handed off (A-SIM-6).
	In.Params.Numerics.MaxEvents = 2;
	Sim.Run(In, R);
	RB_CHECK(R.Diagnostics.IslandHandOffs >= 2);
	RB_CHECK(R.Diagnostics.EventsProcessed <= 2);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Members of an exact-simultaneity group (architecture 8.3, A-SIM-7): only entries that carry an impulse - ball-ball, tip, the cushion-
// like and rail-top contacts. Motion transitions, landings (end slot), tilt refreshes and region crossings never form or join a group.
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_SimultaneityGroupMembership)
{
	QueuedEvent E;
	E.Kind = QueuedEventKind::BallBall;
	RB_CHECK(sim::loop::IsImpulseContact(E));
	E.Kind = QueuedEventKind::TipContact;
	RB_CHECK(sim::loop::IsImpulseContact(E));
	E.Kind = QueuedEventKind::Transition;
	RB_CHECK(!sim::loop::IsImpulseContact(E));
	E.Kind = QueuedEventKind::TiltRefresh;
	E.FeatureKind = kTiltRefreshFeature;
	RB_CHECK(!sim::loop::IsImpulseContact(E));
	E.Kind = QueuedEventKind::TableFeature;
	const TableFeatureKind Contacts[] = {TableFeatureKind::NoseSegment, TableFeatureKind::JawArc, TableFeatureKind::FacingFace, TableFeatureKind::FacingTopEdge,
		TableFeatureKind::LinerWall, TableFeatureKind::RimTorus, TableFeatureKind::RailTop, TableFeatureKind::RailTopEdge};
	for (TableFeatureKind Kind : Contacts)
	{
		E.FeatureKind = static_cast<std::uint8_t>(Kind);
		RB_CHECK(sim::loop::IsImpulseContact(E));
	}
	const TableFeatureKind Regions[] = {TableFeatureKind::DropEdge, TableFeatureKind::CaptureDepth, TableFeatureKind::PocketExit, TableFeatureKind::CaptureCircle,
		TableFeatureKind::SupportExit, TableFeatureKind::OuterBoundary, TableFeatureKind::LampApex};
	for (TableFeatureKind Kind : Regions)
	{
		E.FeatureKind = static_cast<std::uint8_t>(Kind);
		RB_CHECK(!sim::loop::IsImpulseContact(E));
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The record's overflow point is the simulator's MaxRecordEvents, not the capacity a ShotResult was reserved with by another simulator
// (deterministic, like the event log and the tracks). Needs WP-7's record builder.
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Integ_Sim_Review_RecordOverflowAtSimulatorCapacity)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.8, 0.05, kR});
	Place(In, 1, {-0.2, -0.02, kR});
	Place(In, 2, {0.6, 0.3, kR});
	In.Strikes.PushBack(Strike(0, 2.5, std::atan2(-0.04, 0.6), 0.0, 0.1, 0.2));
	ShotResult& R = ResultSlot();
	Simulator Big;
	RB_REQUIRE(Big.Run(In, R) == SimStatus::Ok);
	RB_REQUIRE(R.Record.Events.size() > 4u && !R.Diagnostics.RecordOverflow);
	ResultCapacity Small;
	Small.MaxRecordEvents = 4;
	Simulator Tight(Small);
	RB_REQUIRE(Tight.Run(In, R) == SimStatus::Ok); // R keeps the large reserve of the first run
	RB_CHECK(R.Record.Events.size() == 4u);
	RB_CHECK(R.Record.Truncated && R.Diagnostics.RecordOverflow);
	ShotResult Fresh;
	RB_REQUIRE(Tight.Run(In, Fresh) == SimStatus::Ok); // reserved to exactly 4
	RB_CHECK(Fresh.Record.Events.size() == 4u && Fresh.Record.Truncated);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// No heap allocation inside Run once the ShotResult is reserved (architecture 2 / 12; Simulator.h): the debug CRT counts every
// allocation while a reserved result is re-simulated - strikes, tip re-contacts, ball-ball, cushions, tilt refreshes, observers,
// chalk cling, logging and tracks (MSVC Debug only; other builds run the same shots unchecked).
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_NoHeapAllocationInsideRun)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.Tilt.Slope = {0.0, 1.5e-3};
	P.ChalkCling = true;
	SimInput& In = NewInput(T, P);
	Place(In, 0, {-0.8, 0.05, kR});
	Place(In, 1, {-0.2, -0.02, kR});
	Place(In, 2, {0.6, 0.3, kR});
	Place(In, 3, {-0.2 + 2.0 * kR + 5e-5, -0.02, kR});
	In.Balls[1].ChalkMarks.PushBack(ChalkMark{{-1.0, 0.0, 0.0}, 1.0, 2.5e-3});
	In.Strikes.PushBack(Strike(0, 3.0, std::atan2(-0.07, 0.6), 0.1, 0.2, 0.3));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok); // reserves R (and warms up any lazy CRT state)
	RB_REQUIRE(R.Diagnostics.EventsProcessed > 10 && R.Diagnostics.TiltRefreshes > 0);
	const std::uint64_t Hash = ResultHash(R);
#if RB_SIMREVIEW_ALLOC_HOOK
	AllocationCount() = 0;
	const _CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(&CountAllocations);
	{
		const std::vector<double> Control(8, 1.0); // positive control: the hook sees heap allocations
		RB_CHECK(AllocationCount() >= 1 && Control.size() == 8u);
	}
	AllocationCount() = 0;
	const SimStatus Status = Sim.Run(In, R);
	In.Record.Trajectories = false;
	In.Record.EventStates = false;
	Sim.Run(In, R);
	const long Allocations = AllocationCount();
	_CrtSetAllocHook(Previous);
	RB_CHECK(Status == SimStatus::Ok);
	RB_CHECK(Allocations == 0);
	In.Record = RecordOptions{};
	Sim.Run(In, R);
#else
	Sim.Run(In, R);
#endif
	RB_CHECK(ResultHash(R) == Hash); // and bitwise deterministic
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Object-ball ids are labels: the same B1 layouts and strokes with the object-ball ids permuted give bitwise the same final states
// (event mode: equal-time events of disjoint balls commute, the strict tie-break key never decides physics; architecture 8.3, 11).
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_ObjectBallIdPermutationInvariance)
{
	const TableGeometry& T = NineFoot();
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kShots = 40;
#else
	constexpr int kShots = 300;
#endif
	int Mismatches = 0;
	int Contacts = 0;
	Simulator Sim;
	for (int s = 0; s < kShots; ++s)
	{
		Rng Random(777u + static_cast<std::uint64_t>(s));
		Vec3 Pos[16];
		const int Placed = B1Layout(Random, T, Pos);
		const StrikeRequest Stroke = Strike(0, Random.NextUniform(0.5, 6.0), Random.NextUniform(-3.1, 3.1), 0.0, Random.NextUniform(-0.5, 0.5),
			Random.NextUniform(-0.5, 0.5));
		int Perm[16];
		for (int k = 0; k < 16; ++k)
		{
			Perm[k] = k;
		}
		for (int k = Placed - 1; k > 1; --k)
		{
			const int j = 1 + static_cast<int>(Random.NextBelow(static_cast<std::uint32_t>(k)));
			const int Tmp = Perm[k];
			Perm[k] = Perm[j];
			Perm[j] = Tmp;
		}
		ShotResult* Runs[2] = {&ResultSlot(0), &ResultSlot(1)};
		for (int Run = 0; Run < 2; ++Run)
		{
			SimInput& In = NewInput(T, ValParams());
			for (int k = 0; k < Placed; ++k)
			{
				Place(In, Run == 0 ? k : Perm[k], Pos[k]);
			}
			In.Strikes.PushBack(Stroke);
			Sim.Run(In, *Runs[Run]);
		}
		Contacts += Count(*Runs[0], ShotEventType::BallBall);
		bool Same = Runs[0]->Status == Runs[1]->Status && Bits(Runs[0]->StopTime) == Bits(Runs[1]->StopTime);
		for (int k = 0; k < Placed; ++k)
		{
			const BallFinal& A = Runs[0]->Finals[k];
			const BallFinal& B = Runs[1]->Finals[Perm[k]];
			Same = Same && A.Status == B.Status && SameBits(A.State, B.State) && Bits(A.Time) == Bits(B.Time);
		}
		Mismatches += Same ? 0 : 1;
	}
	RB_CHECK(Contacts > kShots / 2);
	RB_CHECK(Mismatches == 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Extreme shots (seeded): up to 16 balls, strokes from 0.05 m/s to near kMaxCueSpeed with offsets at the miscue limit and elevations
// up to 80 deg (masse, jump cue), tilted tables, chalk cling, with and without tracks. Whatever the stubbed hooks do, every shot
// terminates with a finite state, a time-ordered log that never precedes the strike, and final times within StopTime.
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_ExtremeShotsTerminateFiniteAndOrdered)
{
	const TableGeometry& T = NineFoot();
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kShots = 150;
#else
	constexpr int kShots = 1500;
#endif
	int Run = 0;
	int Bad = 0;
	Simulator Sim;
	for (int s = 0; s < kShots; ++s)
	{
		Rng Random(31337u + static_cast<std::uint64_t>(s));
		PhysicsParams P = ValParams();
		if (Random.NextBelow(2) == 0)
		{
			P.Tilt.Slope = {Random.NextUniform(-2.5e-3, 2.5e-3), Random.NextUniform(-2.5e-3, 2.5e-3)};
		}
		P.ChalkCling = Random.NextBelow(2) == 0;
		SimInput& In = NewInput(T, P);
		In.Record.Trajectories = Random.NextBelow(2) == 0;
		const int Balls = 2 + static_cast<int>(Random.NextBelow(15));
		int Placed = 0;
		for (int Attempt = 0; Attempt < 4000 && Placed < Balls; ++Attempt)
		{
			const Vec3 Q{Random.NextUniform(-T.HalfLength + kR, T.HalfLength - kR), Random.NextUniform(-T.HalfWidth + kR, T.HalfWidth - kR), kR};
			bool Free = true;
			for (int j = 0; j < Placed && Free; ++j)
			{
				Free = Length(Q - In.Balls[j].State.Position) >= 2.0 * kR; // frozen pairs allowed
			}
			if (Free)
			{
				Place(In, Placed, Q);
				if (P.ChalkCling)
				{
					const Vec3 Dir{Random.NextUniform(-1.0, 1.0), Random.NextUniform(-1.0, 1.0), Random.NextUniform(-1.0, 1.0)};
					In.Balls[Placed].ChalkMarks.PushBack(ChalkMark{Normalized(Dir), 1.0, 2.5e-3});
				}
				++Placed;
			}
		}
		const double Elevation = Random.NextBelow(3) == 0 ? Random.NextUniform(0.0, 1.4) : 0.0;
		StrikeRequest Stroke = Strike(0, Random.NextUniform(0.05, 14.0), Random.NextUniform(-3.2, 3.2), Elevation, Random.NextUniform(-0.55, 0.55),
			Random.NextUniform(-0.55, 0.55));
		if (Elevation > 0.5)
		{
			Stroke.Input.Cue = kCueJump9oz;
		}
		if (ValidateCueStrike(Stroke.Input) != ErrorCode::Ok)
		{
			continue;
		}
		In.Strikes.PushBack(Stroke);
		ShotResult& R = ResultSlot();
		const SimStatus Status = Sim.Run(In, R);
		if (Status == SimStatus::InvalidInput)
		{
			continue;
		}
		++Run;
		bool Ok = std::isfinite(R.StopTime) && R.StopTime >= 0.0;
		double Last = 0.0;
		for (const ShotEvent& E : R.Events)
		{
			Ok = Ok && std::isfinite(E.Time) && E.Time >= Last && Finite(E.Pre[0]) && Finite(E.Pre[1]) && Finite(E.Post[0]) && Finite(E.Post[1]);
			Last = E.Time;
		}
		for (int b = 0; b < Placed; ++b)
		{
			Ok = Ok && Finite(R.Finals[b].State) && R.Finals[b].Time <= R.StopTime && R.Finals[b].Status != BallFinalStatus::NotInPlay;
		}
		Bad += Ok ? 0 : 1;
	}
	RB_CHECK(Run > kShots / 2);
	RB_CHECK(Bad == 0);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Max-speed break into a tight 15-ball rack (every neighbour exactly touching): the loop's group / hand-off / cap paths under the
// heaviest load terminate with finite states, a time-ordered log and a bitwise repeatable result, level and elevated with draw (with
// WP-6b's islands this is the real break; until then the hand-offs are no-ops).
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_MaxSpeedBreakTerminates)
{
	const TableGeometry& T = NineFoot();
	const double Row = std::sqrt(3.0) * kR;
	for (int Variant = 0; Variant < 2; ++Variant)
	{
		std::uint64_t Hashes[2] = {};
		for (int Repeat = 0; Repeat < 2; ++Repeat)
		{
			SimInput& In = NewInput(T, ValParams());
			int Id = 1;
			for (int r = 0; r < 5; ++r)
			{
				for (int k = 0; k <= r; ++k)
				{
					Place(In, Id++, {T.Landmarks.FootSpot.x + r * Row, (k - 0.5 * r) * 2.0 * kR, kR});
				}
			}
			Place(In, 0, {-T.HalfLength / 2.0, 0.0, kR});
			In.Strikes.PushBack(Variant == 0 ? Strike(0, kMaxCueSpeed, 0.0) : Strike(0, kMaxCueSpeed, 0.0, 5.0 * kDegToRad, 0.0, -0.4));
			Simulator Sim;
			ShotResult& R = ResultSlot();
			const SimStatus Status = Sim.Run(In, R);
			RB_REQUIRE(Status == SimStatus::Ok || Status == SimStatus::Aborted);
			RB_CHECK(R.Diagnostics.EventsProcessed <= In.Params.Numerics.MaxEvents);
			double Last = 0.0;
			bool Ordered = true;
			for (const ShotEvent& E : R.Events)
			{
				Ordered = Ordered && E.Time >= Last && Finite(E.Pre[0]) && Finite(E.Post[0]);
				Last = E.Time;
			}
			RB_CHECK(Ordered);
			for (int b = 0; b < 16; ++b)
			{
				RB_CHECK(Finite(R.Finals[b].State) && R.Finals[b].Time <= R.StopTime);
			}
			Hashes[Repeat] = ResultHash(R);
		}
		RB_CHECK(Hashes[0] == Hashes[1]);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// A ball resting exactly on a pocket lip (center on the drop-edge circle a_d, collisions 5.5 "hanging on the lip": within eps_touch it
// has not crossed) produces no event and stays on the table; a slow ball rolling along another pocket's axis that stops 0.5 mm short
// of its lip produces its rest transition only (no drop-edge event is queued).
// ---------------------------------------------------------------------------------------------------------------------------------
RB_TEST(Sim_Review_BallRestingOnPocketLipIsQuiet)
{
	const TableGeometry& T = NineFoot();
	const PhysicsParams P = ColParams();
	const PocketGeometry& Pocket = T.Pockets[static_cast<int>(PocketId::FootLeft)];
	const Vec2 Lip = Pocket.CaptureCenter - Pocket.Axis * Pocket.DropEdgeRadius;
	SimInput& In = NewInput(T, P);
	Place(In, 1, ToVec3(Lip, kR));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(R.Events.empty() && R.Diagnostics.EventsProcessed == 0);
	RB_CHECK(R.Finals[1].Status == BallFinalStatus::OnTable && R.Finals[1].State.Position == In.Balls[1].State.Position);

	const PocketGeometry& Side = T.Pockets[static_cast<int>(PocketId::SideRight)];
	const Vec2 SideLip = Side.CaptureCenter - Side.Axis * Side.DropEdgeRadius;
	const double Run = 0.2;
	const double Speed = std::sqrt(2.0 * P.Cloth.RollingResistance * P.Gravity * (Run - 5e-4));
	PlaceRolling(In, 2, ToVec3(SideLip - Side.Axis * Run, kR), ToVec3(Side.Axis * Speed));
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(R.Diagnostics.EventsProcessed == 1); // Rolling -> Stationary
	RB_CHECK(Count(R, ShotEventType::MotionTransition, 2) == 1);
	RB_CHECK_NEAR(Length(XY(R.Finals[2].State.Position) - Side.CaptureCenter), Side.DropEdgeRadius + 5e-4, 1e-9);
	RB_CHECK(R.Finals[1].State.Position == In.Balls[1].State.Position);
}
