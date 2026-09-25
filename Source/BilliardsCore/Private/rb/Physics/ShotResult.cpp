#include "rb/Core/FpGuard.h"
// Owner: WP-6a (simulator core loop). Recording-buffer management for ShotResult (allocation happens only here).
#include "rb/Physics/ShotResult.h"

namespace rb
{
	void ReserveShotResult(ShotResult& Result, const ResultCapacity& Capacity)
	{
		Result.Events.reserve(static_cast<std::size_t>(Capacity.MaxLoggedEvents > 0 ? Capacity.MaxLoggedEvents : 0));
		for (BallTrack& Track : Result.Tracks)
		{
			Track.Segments.reserve(static_cast<std::size_t>(Capacity.MaxSegmentsPerBall > 0 ? Capacity.MaxSegmentsPerBall : 0));
		}
		Result.Record.Events.reserve(static_cast<std::size_t>(Capacity.MaxRecordEvents > 0 ? Capacity.MaxRecordEvents : 0));
		Result.CueTips.reserve(static_cast<std::size_t>(Capacity.MaxCueTipSegments > 0 ? Capacity.MaxCueTipSegments : 0));
	}

	void ResetShotResult(ShotResult& Result)
	{
		Result.Status = SimStatus::NotImplemented;
		Result.StopTime = 0.0;
		Result.BallsInPlay = 0;
		Result.Strikes.Clear();
		Result.Events.clear();
		Result.CueTips.clear();
		for (BallTrack& Track : Result.Tracks)
		{
			Track.Segments.clear();
		}
		for (BallFinal& Final : Result.Finals)
		{
			Final = BallFinal{};
		}
		Result.Record.Start = ShotStartSnapshot{};
		Result.Record.Stroke = StrokeRecord{};
		Result.Record.Events.clear();
		Result.Record.End = ShotEndSnapshot{};
		Result.Record.Truncated = false;
		Result.Diagnostics = SimDiagnostics{};
	}
}
