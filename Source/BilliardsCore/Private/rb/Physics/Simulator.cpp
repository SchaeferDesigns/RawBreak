#include "rb/Core/FpGuard.h"
// Owner: WP-6a (simulator core loop). Spec: Docs/architecture.md "Event loop"; physics-collisions 1, 3.6-3.9, 7;
// physics-motion-and-cue C.2-C.4, implementation notes 1-10; prior-art 5.
#include "rb/Physics/Simulator.h"

#include "SimInternal.h"

namespace rb
{
	// Private per-simulator state (allocated once in the constructor, reused by every Run).
	struct Simulator::Workspace
	{
		sim::Workspace Data;
	};

	PhysicsParams MakePhysicsParams(const TableSpec& Spec)
	{
		PhysicsParams Params;
		Params.Origin = ParamsOrigin::Table;
		Params.Cloth = ClothParamsFor(Spec.Cloth);
		Params.Cushion.FacingRestitutionScale = Spec.FacingRestitutionScale;
		Params.PocketContacts.LinerRestitution = Spec.LinerRestitution;
		Params.PocketContacts.LinerFriction = Spec.LinerFriction;
		return Params;
	}

	PhysicsParams MakePhysicsParams(const TableSpec& Spec, const TableCondition& Condition)
	{
		PhysicsParams Params = MakePhysicsParams(Spec);
		Params.Tilt.Slope = Condition.Slope;
		Params.BallBall.ClingFactor = Condition.BallCling;
		Params.ChalkCling = Condition.ChalkCling;
		return Params;
	}

	ErrorCode ValidatePhysicsParams(const PhysicsParams& /*Params*/)
	{
		// TODO(WP-6a): reject Origin == Unset, mu <= 0, e outside [0, 1], alpha_sp <= 0, g <= 0, non-positive step sizes,
		// capacities <= 0, Tsuji alpha that is neither < 0 (derive) nor finite (ROB-06); tilt (human-factors 4.5.1, 4.5.6;
		// Simulator.h): non-finite tilt values, (5/7) |Slope| + |NapPseudoSlope| > (1 - NapResistance) mu_r / 2 (cloth),
		// |Slope| > 0.7 mu_r (rail cap), Tolerance / RefreshMaxInterval <= 0, NapResistance outside [0, 1),
		// ClingFactor / ChalkClingFactor <= 0.
		return ErrorCode::NotImplemented;
	}

	Simulator::Simulator(const ResultCapacity& InCapacity)
		: Ws(new Workspace())
		, Caps(InCapacity)
	{
	}

	Simulator::~Simulator()
	{
		delete Ws;
	}

	Simulator::Simulator(Simulator&& Other) noexcept
		: Ws(Other.Ws)
		, Caps(Other.Caps)
	{
		Other.Ws = nullptr;
	}

	Simulator& Simulator::operator=(Simulator&& Other) noexcept
	{
		if (this != &Other)
		{
			delete Ws;
			Ws = Other.Ws;
			Caps = Other.Caps;
			Other.Ws = nullptr;
		}
		return *this;
	}

	SimStatus Simulator::Run(const SimInput& /*Input*/, ShotResult& Result)
	{
		// TODO(WP-6a): event loop (architecture.md 8): validate (params, per-ball specs incl. the TiltParams validity rule with
		// each ball's k, table, overlaps, strikes),
		// resolve Tsuji alpha, strikes at t = 0, predict, pop / invalidate, dispatch (WP-6b hooks for islands,
		// pockets, landings, rail top), observers, tip slots, recording, guards, FinishShotRecord; tilted table
		// (architecture.md 8.11): MakeSegment(..., Params.Tilt), TiltRefresh, exact re-anchor of events inside chain
		// pieces (BallStateForEvent); ChalkCling: orientation of marked balls, per-contact ContactClingFactor; islands get
		// CompliantIsland::SetInPlaneGravity(InPlaneGravity(Params.Tilt, g)) (WP-6b).
		ReserveShotResult(Result, Caps);
		ResetShotResult(Result);
		Result.Status = SimStatus::NotImplemented;
		return Result.Status;
	}

	namespace sim
	{
		void ReplaceSegment(Workspace& /*Ws*/, int /*Ball*/, const BallState& /*State*/, double /*Time*/)
		{
			// TODO(WP-6a): close the open trajectory segment (orientation law), MakeSegment, version bump, observers, re-predict.
		}

		void EmitEvent(Workspace& /*Ws*/, const ShotEvent& /*Event*/)
		{
			// TODO(WP-6a): physics log (switches, capacity) + AppendRecordEvent when RecordOptions::ShotRecord.
		}

		void MakeTerminal(Workspace& /*Ws*/, int /*Ball*/, MotionState /*Terminal*/, double /*Time*/, PocketId /*Pocket*/, OffTableReason /*Reason*/)
		{
			// TODO(WP-6a): terminal segment, Finals entry, remove from detection.
		}

		BallState BallStateAt(const Workspace& Ws, int Ball, double Time)
		{
			// TODO(WP-6a): EvaluateSegment / EvaluatePivot of the ball's current segment.
			const MotionSegment& Seg = Ws.Balls[Ball].Seg;
			return EvaluateSegment(Seg, Time - Seg.T0);
		}

		BallState BallStateForEvent(const Workspace& Ws, int Ball, double Time)
		{
			// TODO(WP-6a): EvaluateSegmentForEvent (exact pursuit velocity inside tilt chain pieces) / EvaluatePivot.
			const MotionSegment& Seg = Ws.Balls[Ball].Seg;
			return EvaluateSegmentForEvent(Seg, Time - Seg.T0);
		}
	}
}
