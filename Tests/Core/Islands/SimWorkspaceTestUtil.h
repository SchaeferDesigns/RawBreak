#pragma once

// WP-6b hook-level tests: a hand-built private simulator workspace (Private/rb/Physics/SimInternal.h) around a Scene, set up like
// Simulator::Run does before its loop (reserved result, resolved parameters, per-ball closed-form segments, open track segments),
// so that StartIsland / AdvanceIsland / ProcessPocketEvent / ProcessRailTopEvent / RouteLanding can be driven directly. The loop's
// services (ReplaceSegment, EmitEvent, MakeTerminal) are WP-6a's: the tests only check what WP-6b itself maintains.

#include "IslandTestUtil.h"

#include "../../../Source/BilliardsCore/Private/rb/Physics/SimIslandInternal.h"

#include <memory>

namespace isltest
{
	inline std::unique_ptr<rb::sim::Workspace> MakeWorkspace(Scene& S)
	{
		std::unique_ptr<rb::sim::Workspace> Ws = std::make_unique<rb::sim::Workspace>();
		rb::ReserveShotResult(S.Result, rb::ResultCapacity{});
		rb::ResetShotResult(S.Result);
		Ws->Input = &S.Input;
		Ws->Result = &S.Result;
		Ws->Params = S.Input.Params;
		if (Ws->Params.Cli.TsujiAlpha < 0.0)
		{
			Ws->Params.Cli.TsujiAlpha = rb::TsujiAlphaForRestitution(Ws->Params.BallBall.Restitution);
		}
		Ws->Detection.NoseProfileRadius = Ws->Params.Cushion.NoseProfileRadius;
		Ws->Detection.PooltoolCompat = Ws->Params.Cushion.PooltoolCompat;
		Ws->Detection.Pockets = Ws->Params.Pockets;
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			rb::sim::BallSlot& Slot = Ws->Balls[b];
			const rb::SimBall& Ball = S.Input.Balls[b];
			Slot.InPlay = Ball.InPlay;
			if (!Ball.InPlay)
			{
				continue;
			}
			Slot.Seg = rb::MakeSegment(Ball.State, 0.0, Ball.Spec, Ws->Params.Cloth, 0.0, Ws->Params.Gravity, Ws->Params.Tilt);
			Slot.Orientation0 = Ball.Orientation;
			if (S.Input.Record.Trajectories)
			{
				rb::TrajectorySegment Open;
				Open.Motion = Slot.Seg;
				Open.T1 = rb::kInfinity;
				Open.Orientation0 = Slot.Orientation0;
				S.Result.Tracks[b].Segments.push_back(Open);
			}
		}
		return Ws;
	}
}
