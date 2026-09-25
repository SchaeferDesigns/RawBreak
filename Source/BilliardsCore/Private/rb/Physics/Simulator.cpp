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

	ErrorCode ValidatePhysicsParams(const PhysicsParams& /*Params*/)
	{
		// TODO(WP-6a): reject Origin == Unset, mu <= 0, e outside [0, 1], alpha_sp <= 0, g <= 0, non-positive step sizes,
		// capacities <= 0, Tsuji alpha that is neither < 0 (derive) nor finite (ROB-06).
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
		// TODO(WP-6a): event loop (architecture.md 8): validate (params, per-ball specs, table, overlaps, strikes),
		// resolve Tsuji alpha, strikes at t = 0, predict, pop / invalidate, dispatch (WP-6b hooks for islands,
		// pockets, landings, rail top), observers, tip slots, recording, guards, FinishShotRecord.
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
	}
}
