// Owner: WP-6a (simulator core loop). The services of the private loop <-> island / pocket interface (SimInternal.h) that WP-6b
// builds on, exercised directly on a workspace: ReplaceSegment with a PocketPivot state (the pivot from the ball's pocket context,
// its end slot, adaptive Sampled recording when it is closed, orientation continuity), EmitEvent's logging switches, MakeTerminal.

#include "rbtest.h"

#include "Simulator/SimTestUtil.h"

#include "../../../Source/BilliardsCore/Private/rb/Physics/SimInternal.h"

#include "rb/Physics/PocketDrop.h"

#include <cmath>

using namespace rb;
using namespace simtest;

namespace
{
	bool SameStateBits(const BallState& A, const BallState& B)
	{
		return A.Position == B.Position && A.Velocity == B.Velocity && A.Omega == B.Omega && A.State == B.State;
	}

	sim::Workspace& FreshWorkspace(const SimInput& In, ShotResult& R)
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
		Ws.Caps = ResultCapacity{};
		Ws.Cache = sim::LoopCache{};
		ReserveShotResult(R, Ws.Caps);
		ResetShotResult(R);
		return Ws;
	}
}

RB_TEST(Sim_ServicesPivotReplaceAndTerminal)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	const PocketGeometry& Pocket = T.Pockets[static_cast<int>(PocketId::FootLeft)];
	// Ball 0 rolling along the pocket axis, its center on the drop-edge circle (a_d) at t = 0.5 s.
	const Vec2 Edge = Pocket.CaptureCenter - Pocket.Axis * Pocket.DropEdgeRadius;
	const Vec3 Velocity = ToVec3(Pocket.Axis * 0.2);
	Place(In, 0, ToVec3(Edge, kR) - Velocity * 0.1);
	ShotResult& R = ResultSlot();
	sim::Workspace& Ws = FreshWorkspace(In, R);
	sim::BallSlot& Slot = Ws.Balls[0];
	Slot.InPlay = true;
	BallState Start = In.Balls[0].State;
	sim::loop::SetSegment(Ws, 0, Start, 0.0, true);
	RB_REQUIRE(R.Tracks[0].Segments.size() == 1);

	// The pocket state machine (WP-6b) hands the ball over with the pocket in its context and a PocketPivot state.
	const double T0 = 0.5;
	Ws.Now = T0;
	Slot.Context.Pocket = PocketId::FootLeft;
	BallState AtEdge;
	AtEdge.Position = ToVec3(Edge, kR);
	AtEdge.Velocity = Velocity;
	AtEdge.Omega = RollingOmegaH(Velocity, kR);
	AtEdge.State = MotionState::PocketPivot;
	const std::uint32_t Version = Slot.Version;
	sim::ReplaceSegment(Ws, 0, AtEdge, T0);
	RB_CHECK(Slot.Version == Version + 1);
	RB_REQUIRE(Slot.Seg.State == MotionState::PocketPivot);
	const PivotPath& Pivot = Slot.Pivot;
	RB_REQUIRE(!Pivot.Result.Immediate && Pivot.Result.Duration > 0.05);
	RB_CHECK(Pivot.T0 == T0 && Pivot.Pocket == PocketId::FootLeft);
	RB_CHECK(R.Tracks[0].Segments[0].T1 == T0);
	RB_CHECK(R.Tracks[0].Segments.back().Kind == SegmentKind::Sampled && R.Tracks[0].Segments.back().T1 == kInfinity);
	const double Mid = T0 + 0.5 * Pivot.Result.Duration;
	const BallState OnPath = EvaluatePivot(Pivot, Mid - T0);
	RB_CHECK(SameStateBits(sim::BallStateAt(Ws, 0, Mid), OnPath));
	// Its end slot (pivot end, tier Transition) is queued with the new version.
	bool EndQueued = false;
	while (!Ws.Queue.IsEmpty())
	{
		const QueuedEvent E = Ws.Queue.Top();
		Ws.Queue.Pop();
		EndQueued = EndQueued || (E.Kind == QueuedEventKind::Transition && E.BallA == 0 && E.VersionA == Slot.Version && E.Time == T0 + Pivot.Result.Duration);
	}
	RB_CHECK(EndQueued);

	// Pivot end: PocketFall from the leave state; the pivot becomes adaptive Sampled pieces on the pivot path.
	const double T1 = T0 + Pivot.Result.Duration;
	Ws.Now = T1;
	const BallState Leave = PivotLeaveState(Pivot);
	const PivotPath Saved = Pivot;
	const Vec3 PivotStart = EvaluatePivot(Pivot, 0.0).Position;
	sim::ReplaceSegment(Ws, 0, Leave, T1);
	RB_CHECK(Slot.Seg.State == MotionState::PocketFall && Slot.Seg.T0 == T1);
	const auto& Track = R.Tracks[0].Segments;
	RB_REQUIRE(Track.size() >= 4);
	double Previous = T0;
	Vec3 PreviousEnd = PivotStart;
	int Pieces = 0;
	for (std::size_t k = 1; k + 1 < Track.size(); ++k)
	{
		const TrajectorySegment& S = Track[k];
		RB_CHECK(S.Kind == SegmentKind::Sampled && S.Motion.State == MotionState::PocketPivot);
		RB_CHECK(S.Motion.T0 == Previous && S.T1 > S.Motion.T0);
		RB_CHECK(S.T1 - S.Motion.T0 <= NumericsConfig{}.SampleMaxInterval + 1e-15);
		RB_CHECK(Length(S.Motion.Pos0 - PreviousEnd) == 0.0);
		const double Middle = 0.5 * (S.Motion.T0 + S.T1);
		const Vec3 Chord = (S.Motion.Pos0 + S.EndPosition) * 0.5;
		RB_CHECK(Length(Chord - EvaluatePivot(Saved, Middle - T0).Position) <= NumericsConfig{}.SampleTolerance * 1.01);
		Previous = S.T1;
		PreviousEnd = S.EndPosition;
		++Pieces;
	}
	RB_CHECK(Pieces >= 10);
	RB_CHECK(Previous == T1);
	RB_CHECK(Length(PreviousEnd - Leave.Position) <= 1e-12);
	RB_CHECK(Track.back().Kind == SegmentKind::Analytic && Track.back().Motion.T0 == T1);
	RB_CHECK(Slot.Orientation0 == Track.back().Orientation0);

	// EmitEvent: MotionTransition only with LogTransitions; always the physics diagnostics.
	ShotEvent Transition;
	Transition.Type = ShotEventType::MotionTransition;
	Transition.Time = T1;
	Transition.A = 0;
	SimInput& Quiet = InputSlot();
	Quiet.Record.LogTransitions = false;
	const std::size_t Logged = R.Events.size();
	sim::EmitEvent(Ws, Transition);
	RB_CHECK(R.Events.size() == Logged);
	Quiet.Record.LogTransitions = true;
	sim::EmitEvent(Ws, Transition);
	RB_CHECK(R.Events.size() == Logged + 1);

	// Capture: MakeTerminal writes the Terminal segment and the Finals entry and removes the ball from detection.
	const double T2 = T1 + 0.1;
	Ws.Now = T2;
	sim::MakeTerminal(Ws, 0, MotionState::Pocketed, T2, PocketId::FootLeft, OffTableReason::Floor);
	RB_CHECK(Slot.Seg.State == MotionState::Pocketed && !sim::loop::IsLive(Ws, 0));
	RB_CHECK(R.Tracks[0].Segments.back().Kind == SegmentKind::Terminal && R.Tracks[0].Segments.back().Motion.T0 == T2);
	RB_CHECK(R.Finals[0].Status == BallFinalStatus::Pocketed && R.Finals[0].Pocket == PocketId::FootLeft && R.Finals[0].Time == T2);
	RB_CHECK(R.Tracks[0].Segments[R.Tracks[0].Segments.size() - 2].T1 == T2);
}
