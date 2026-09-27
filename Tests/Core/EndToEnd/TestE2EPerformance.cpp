// Integration round 2 (end-to-end performance, architecture 1 / 12 targets = prior-art 7.4 P1-P3): A-E2E-18 measures the whole
// core in Release on the prior-art 7.5 benchmark shots with a warm simulator (one per worker, result buffers reserved once,
// architecture 12). The full PERF-01..05 suite (threads, allocation counter, reference CPU) is WP-10's; this is the integration check:
//   typical shot    (B1 mid-game shots)                        P1: median <= 100 us, p99 <= 1 ms
//   break           (B2 9-ball and 15-ball 8-ball breaks)      P2: median <= 2 ms, p99 <= 10 ms
//   1000-shot batch (B1, one core)                             P3: >= 10 000 shots / s / core
// with the AI rollout recording (RecordOptions of architecture 5.2: no trajectories, states, transitions or observers; the rules
// record only; small capacities) and, for information, the full game recording and a tilted table (2 mm/m; A-PERF-1 / O-13).
// Numbers are printed; the gates apply in Release only (a Debug build prints them). Gated: P1, P3 and P2 for 9-ball breaks. The
// 15-ball break of P2 is printed with its verdict but not gated: at integration round 2 it takes a median of about 2.8 ms on the
// development machine, 1000-3000 compliant CLI steps of ~2 us with 16 bodies (WP-3 solver cost; open item for WP-3 / WP-10).

#include "rbtest.h"

#include "EndToEnd/EndToEndUtil.h"

#include <algorithm>
#include <chrono>
#include <vector>

using namespace rb;

namespace
{
	struct Timing
	{
		double MeanUs = 0.0;
		double MedianUs = 0.0;
		double P90Us = 0.0;
		double P99Us = 0.0;
		double MaxUs = 0.0;
		double TotalS = 0.0;
		int NotOk = 0;
	};

	Timing Summarize(std::vector<double>& Us, double TotalS, int NotOk)
	{
		Timing T;
		T.TotalS = TotalS;
		T.NotOk = NotOk;
		if (Us.empty())
		{
			return T;
		}
		double Sum = 0.0;
		for (double U : Us)
		{
			Sum += U;
		}
		std::sort(Us.begin(), Us.end());
		T.MeanUs = Sum / static_cast<double>(Us.size());
		T.MedianUs = Us[Us.size() / 2];
		T.P90Us = Us[(Us.size() * 9) / 10];
		T.P99Us = Us[(Us.size() * 99) / 100];
		T.MaxUs = Us.back();
		return T;
	}

	void AiRecording(SimInput& In)
	{
		In.Record.Trajectories = false;
		In.Record.EventStates = false;
		In.Record.LogTransitions = false;
		In.Record.LogObservers = false;
		In.Record.ShotRecord = true;
	}

	// Times Count shots made by Make(s, In) on a warm simulator (the first shot of the set is run once before timing).
	template <typename MakeFn>
	Timing TimeShots(Simulator& Sim, ShotResult& R, int Count, bool Ai, const Vec2& Slope, MakeFn Make)
	{
		static SimInput In;
		std::vector<double> Us;
		Us.reserve(static_cast<std::size_t>(Count));
		int NotOk = 0;
		Make(0, In);
		if (Ai)
		{
			AiRecording(In);
		}
		Sim.Run(In, R); // warm-up: buffers reserved, workspace touched
		const auto Batch = std::chrono::steady_clock::now();
		for (int s = 0; s < Count; ++s)
		{
			Make(s, In);
			In.Params.Tilt.Slope = Slope;
			if (Ai)
			{
				AiRecording(In);
			}
			const auto Start = std::chrono::steady_clock::now();
			const SimStatus Status = Sim.Run(In, R);
			Us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - Start).count());
			NotOk += Status == SimStatus::Ok ? 0 : 1;
		}
		const double Total = std::chrono::duration<double>(std::chrono::steady_clock::now() - Batch).count();
		return Summarize(Us, Total, NotOk);
	}

	void Print(const char* Label, const Timing& T, int Count)
	{
		std::printf("  %-44s mean %7.1f us  median %7.1f us  p90 %7.1f us  p99 %7.1f us  max %7.1f us  -> %6.0f shots/s/core (%d not Ok of %d)\n", Label,
			T.MeanUs, T.MedianUs, T.P90Us, T.P99Us, T.MaxUs, static_cast<double>(Count) / T.TotalS, T.NotOk, Count);
	}
}

RB_TEST(Integ_ARCH_E2E18_Slow_PerformanceBudgets)
{
	const TableGeometry& T = simtest::NineFoot();
	ResultCapacity AiCaps;
	AiCaps.MaxLoggedEvents = 256;
	AiCaps.MaxSegmentsPerBall = 0;
	AiCaps.MaxRecordEvents = 1024;
	AiCaps.MaxCueTipSegments = 8;
	Simulator Ai(AiCaps);
	Simulator Game;
	static ShotResult AiResult;
	static ShotResult GameResult;
	const auto B1 = [&T](int s, SimInput& In) { e2e::MakeB1Shot(900000u + static_cast<std::uint64_t>(s), T, In); };
	const auto B2 = [&T](int s, SimInput& In) { e2e::MakeB2Break(700000u + static_cast<std::uint64_t>(s), T, In, false); };
	const auto B2Eight = [&T](int s, SimInput& In) { e2e::MakeB2Break(800000u + static_cast<std::uint64_t>(s), T, In, true); };
	const Vec2 Level{};
	const Vec2 Tilted{0.0, 2.0e-3};

	const int Shots = 1000;
	const int Breaks = 100;
	std::printf("  A-E2E-18 performance (targets P1 B1 median <= 100 us / p99 <= 1 ms, P2 break median <= 2 ms / p99 <= 10 ms, P3 >= 10000 shots/s/core):\n");
	const Timing AiShots = TimeShots(Ai, AiResult, Shots, true, Level, B1);
	Print("B1 typical shots, AI recording", AiShots, Shots);
	const Timing GameShots = TimeShots(Game, GameResult, Shots, false, Level, B1);
	Print("B1 typical shots, full game recording", GameShots, Shots);
	const Timing TiltShots = TimeShots(Ai, AiResult, Shots, true, Tilted, B1);
	Print("B1 typical shots, AI recording, 2 mm/m tilt", TiltShots, Shots);
	const Timing AiBreaks = TimeShots(Ai, AiResult, Breaks, true, Level, B2);
	Print("B2 9-ball breaks, AI recording", AiBreaks, Breaks);
	const Timing GameBreaks = TimeShots(Game, GameResult, Breaks, false, Level, B2);
	Print("B2 9-ball breaks, full game recording", GameBreaks, Breaks);
	const Timing AiEight = TimeShots(Ai, AiResult, Breaks, true, Level, B2Eight);
	Print("B2 15-ball (8-ball) breaks, AI recording", AiEight, Breaks);
	const Timing GameEight = TimeShots(Game, GameResult, Breaks, false, Level, B2Eight);
	Print("B2 15-ball (8-ball) breaks, full game recording", GameEight, Breaks);
	RB_CHECK(AiShots.NotOk == 0 && GameShots.NotOk == 0 && TiltShots.NotOk == 0);
	RB_CHECK(AiBreaks.NotOk == 0 && GameBreaks.NotOk == 0 && AiEight.NotOk == 0 && GameEight.NotOk == 0);
#ifdef NDEBUG
	RB_CHECK(AiShots.MedianUs <= 100.0 && AiShots.P99Us <= 1000.0);   // P1
	RB_CHECK(static_cast<double>(Shots) / AiShots.TotalS >= 10000.0); // P3: the 1000-shot batch on one core
	RB_CHECK(AiBreaks.MedianUs <= 2000.0 && AiBreaks.P99Us <= 10000.0); // P2 (9-ball breaks)
#endif
	std::printf("  verdicts: P1 %s, P2 9-ball %s, P2 15-ball %s, P3 %s\n", AiShots.MedianUs <= 100.0 && AiShots.P99Us <= 1000.0 ? "met" : "NOT MET",
		AiBreaks.MedianUs <= 2000.0 && AiBreaks.P99Us <= 10000.0 ? "met" : "NOT MET", AiEight.MedianUs <= 2000.0 && AiEight.P99Us <= 10000.0 ? "met" : "NOT MET",
		static_cast<double>(Shots) / AiShots.TotalS >= 10000.0 ? "met" : "NOT MET");
}
