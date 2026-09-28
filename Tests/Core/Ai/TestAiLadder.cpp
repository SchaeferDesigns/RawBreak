// Owner: WP-12 (AI opponent). Strength of the profiles over many racks with the full core (Docs/architecture.md 7.6):
//   A-AI-7  strength ladder: every profile beats the next weaker one (9-ball, 9-ft pro table, alternating breaks), the touring pro
//           beats the tourist clearly; win rates, run-out and break-and-run rates, pot rates, fouls, safeties, push-outs and
//           decision times are printed;
//   HF-B09  round robin of all six profiles (9-ball): ratings fitted by maximum likelihood (Bradley-Terry) on the logarithmic
//           scale of human-factors 5.5 (+100 points = 2:1 in games) reproduce 2:1 game odds per 100 SPEC rating points within
//           +-5 % (INTERPRETATION: the least-squares slope s of the fitted against the spec ratings gives odds 2^s per 100 spec
//           points in [1.9, 2.1]); the order of the fitted ratings equals the spec's; per-pair measured and predicted rates are
//           printed;
//   A-AI-10 (8-ball half) 8-ball racks on the 7-ft bar table are played to the end with valid declarations (calls) only.
// Racks run in parallel (one planner scratch and referee per thread; each rack is independent and seeded, so the results do not
// depend on the thread count). Every rack is a fresh match: seed = hash(pair, rack), breaker alternates with the rack index.

#include "Ai/AiTestUtil.h"

#include <cmath>
#include <cstdlib>
#include <mutex>

using namespace rb;

namespace
{
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr bool kRelease = false;
#else
	constexpr bool kRelease = true;
#endif

	struct PairResult
	{
		human::AiProfileId A = human::AiProfileId::Tourist;
		human::AiProfileId B = human::AiProfileId::Tourist;
		aitest::Stats Stats[2];
		int Stalled = 0;
		int Racks = 0;
	};

	// An integer from the environment (experiments: RB_AI_RACKS racks per pair, RB_AI_PAIR one pair; the gates assume the defaults).
	int EnvInt(const char* Name, int Default)
	{
#if defined(_MSC_VER)
		char* Value = nullptr;
		std::size_t Length = 0;
		if (_dupenv_s(&Value, &Length, Name) == 0 && Value != nullptr)
		{
			const int N = std::atoi(Value);
			std::free(Value);
			return N;
		}
		return Default;
#else
		const char* Value = std::getenv(Name);
		return Value != nullptr ? std::atoi(Value) : Default;
#endif
	}

	int RacksPerPair(int Default)
	{
		const int N = EnvInt("RB_AI_RACKS", Default);
		return N > 0 ? N : Default;
	}

	int Threads()
	{
		const unsigned Hw = std::thread::hardware_concurrency();
		return Hw >= 4 ? static_cast<int>(Hw) - 2 : 1;
	}

	void Merge(aitest::Stats& To, const aitest::Stats& From)
	{
		To.Racks += From.Racks;
		To.RacksWon += From.RacksWon;
		To.BreakAndRuns += From.BreakAndRuns;
		To.RunOuts += From.RunOuts;
		To.Shots += From.Shots;
		To.PotAttempts += From.PotAttempts;
		To.PotsMade += From.PotsMade;
		To.Safeties += From.Safeties;
		To.PushOuts += From.PushOuts;
		To.Fouls += From.Fouls;
		To.Breaks += From.Breaks;
		To.BreakPots += From.BreakPots;
		To.InvalidDeclarations += From.InvalidDeclarations;
		To.PlannerErrors += From.PlannerErrors;
		To.SimulationErrors += From.SimulationErrors;
		To.DecisionSeconds += From.DecisionSeconds;
		To.Decisions += From.Decisions;
		To.Simulations += From.Simulations;
	}

	// Plays Racks racks of every pair (A = player 0, B = player 1) on Threads threads.
	void PlayPairs(std::vector<PairResult>& Pairs, int Racks, rules::Discipline Game, const TableSpec& Spec, std::uint64_t Seed)
	{
		const TableGeometry& T = simtest::Table(Spec);
		const PhysicsParams Physics = MakePhysicsParams(Spec);
		const int Total = static_cast<int>(Pairs.size()) * Racks;
		std::atomic<int> Next{0};
		std::mutex Lock;
		const auto Work = [&]() {
			auto Scratch = std::make_unique<ai::PlannerScratch>();
			auto Ref = std::make_unique<aitest::Referee>();
			for (int k = Next.fetch_add(1); k < Total; k = Next.fetch_add(1))
			{
				PairResult& P = Pairs[static_cast<std::size_t>(k / Racks)];
				const int r = k % Racks;
				const std::uint64_t RackSeed = human::HashKeys(Seed, static_cast<std::uint64_t>(k / Racks), static_cast<std::uint64_t>(r));
				const rules::MatchConfig C = aitest::MakeMatch(Game, T, RackSeed);
				aitest::Player Players[2] = {aitest::MakePlayer(P.A, 1, RackSeed), aitest::MakePlayer(P.B, 2, RackSeed + 1)};
				const ai::OpponentModel Models[2] = {ai::OpponentModelFor(P.A), ai::OpponentModelFor(P.B)};
				aitest::Stats St[2];
				rules::MatchState S = aitest::StartMatchState(C, r % 2);
				std::uint32_t ShotIndex = 0;
				const aitest::RackResult R = aitest::PlayRack(T, Physics, C, S, Players, Models, St, *Scratch, *Ref, ShotIndex, ai::PlannerConfig{});
				std::lock_guard<std::mutex> Guard(Lock);
				Merge(P.Stats[0], St[0]);
				Merge(P.Stats[1], St[1]);
				P.Racks++;
				P.Stalled += R.Winner < 0 ? 1 : 0;
			}
		};
		std::vector<std::thread> Pool;
		const int N = aitest::TraceOn() ? 1 : Threads();
		for (int w = 0; w < N; ++w)
		{
			Pool.emplace_back(Work);
		}
		for (std::thread& Th : Pool)
		{
			Th.join();
		}
	}

	double WinRate(const PairResult& P, int Side)
	{
		const int Decided = P.Stats[0].RacksWon + P.Stats[1].RacksWon;
		return Decided > 0 ? static_cast<double>(P.Stats[Side].RacksWon) / Decided : 0.5;
	}

	void PrintPair(const PairResult& P)
	{
		std::printf("  %-18s vs %-18s: %4d racks (%d stalled), win %5.1f %% / %5.1f %%\n", aitest::ProfileName(P.A), aitest::ProfileName(P.B), P.Racks, P.Stalled,
			100.0 * WinRate(P, 0), 100.0 * WinRate(P, 1));
		for (int s = 0; s < 2; ++s)
		{
			const aitest::Stats& S = P.Stats[s];
			std::printf("      %-18s run-outs %5.1f %% of racks, break-and-runs %5.1f %%, pots %5.1f %% of %d, fouls/rack %.2f, safeties/rack %.2f, push-outs %d, "
						"break pots %.0f %%, %.1f ms and %.0f sims per decision%s\n",
				aitest::ProfileName(s == 0 ? P.A : P.B), 100.0 * S.RunOuts / (S.Racks > 0 ? S.Racks : 1), 100.0 * S.BreakAndRuns / (S.Racks > 0 ? S.Racks : 1),
				100.0 * S.PotsMade / (S.PotAttempts > 0 ? S.PotAttempts : 1), S.PotAttempts, static_cast<double>(S.Fouls) / (S.Racks > 0 ? S.Racks : 1),
				static_cast<double>(S.Safeties) / (S.Racks > 0 ? S.Racks : 1), S.PushOuts, 100.0 * S.BreakPots / (S.Breaks > 0 ? S.Breaks : 1),
				1e3 * S.DecisionSeconds / (S.Decisions > 0 ? S.Decisions : 1), static_cast<double>(S.Simulations) / (S.Decisions > 0 ? S.Decisions : 1),
				S.InvalidDeclarations + S.PlannerErrors + S.SimulationErrors > 0 ? " ERRORS" : "");
			if (S.InvalidDeclarations + S.PlannerErrors + S.SimulationErrors > 0)
			{
				std::printf("        invalid declarations %d, planner errors %d, simulation errors %d\n", S.InvalidDeclarations, S.PlannerErrors, S.SimulationErrors);
			}
		}
	}

	bool Clean(const PairResult& P)
	{
		for (const aitest::Stats& S : P.Stats)
		{
			if (S.InvalidDeclarations + S.PlannerErrors + S.SimulationErrors > 0)
			{
				return false;
			}
		}
		return true;
	}
}

// A-AI-7: the strength ladder (adjacent profiles and the extremes), 9-ball on the 9-ft pro table.
RB_TEST(Integ_ARCH_AI7_Slow_StrengthLadder)
{
	const int Racks = RacksPerPair(kRelease ? 200 : 2);
	std::vector<PairResult> Pairs;
	for (int i = 0; i + 1 < human::kAiProfileCount; ++i)
	{
		PairResult P;
		P.A = aitest::kProfiles[i + 1]; // the stronger one is player 0
		P.B = aitest::kProfiles[i];
		Pairs.push_back(P);
	}
	PairResult Extremes;
	Extremes.A = human::AiProfileId::TouringPro;
	Extremes.B = human::AiProfileId::Tourist;
	Pairs.push_back(Extremes);
	const int Only = EnvInt("RB_AI_PAIR", -1); // experiments: one pair only
	if (Only >= 0 && Only < static_cast<int>(Pairs.size()))
	{
		const PairResult Keep = Pairs[static_cast<std::size_t>(Only)];
		Pairs.assign(1, Keep);
	}
	const auto T0 = std::chrono::steady_clock::now();
	PlayPairs(Pairs, Racks, rules::Discipline::NineBall, kTableNineFootPro, 0x1ADDE7u);
	std::printf("  A-AI-7 strength ladder, 9-ball, %d racks per pair, %d threads, %.1f s\n", Racks, Threads(),
		std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count());
	for (const PairResult& P : Pairs)
	{
		PrintPair(P);
		RB_CHECK(Clean(P));
		RB_CHECK(P.Stalled * 20 <= P.Racks);
		if (kRelease)
		{
			const bool Extreme = P.A == human::AiProfileId::TouringPro && P.B == human::AiProfileId::Tourist;
			RB_CHECK(WinRate(P, 0) >= (Extreme ? 0.85 : 0.55));
		}
	}
}

// A-AI-10 (8-ball half): 8-ball racks on the 7-ft bar table between the league player, the road player, the touring pro and the
// bar regular: every rack is played to the end (or the shot cap), every declaration is valid (called shots: an own-group ball or
// the 8 with its pocket; declared safeties), no planner or simulation error; the stronger profile wins more.
RB_TEST(Integ_ARCH_AI10_Slow_EightBallRacks)
{
	const int Racks = RacksPerPair(kRelease ? 60 : 2);
	std::vector<PairResult> Pairs;
	for (const human::AiProfileId Id : {human::AiProfileId::LeaguePlayer, human::AiProfileId::RoadPlayer, human::AiProfileId::TouringPro})
	{
		PairResult P;
		P.A = Id;
		P.B = human::AiProfileId::BarRegular;
		Pairs.push_back(P);
	}
	const auto T0 = std::chrono::steady_clock::now();
	PlayPairs(Pairs, Racks, rules::Discipline::EightBall, kTableSevenFootBar, 0x8BA11u);
	std::printf("  A-AI-10 8-ball on the 7-ft bar table, %d racks per pair, %.1f s\n", Racks, std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count());
	for (const PairResult& P : Pairs)
	{
		PrintPair(P);
		RB_CHECK(Clean(P));
		RB_CHECK(P.Stalled * 10 <= P.Racks);
		if (kRelease)
		{
			RB_CHECK(WinRate(P, 0) > 0.5);
		}
	}
}

// HF-B09: the round robin of the six profiles and the rating fit.
RB_TEST(Integ_HF_B09_Slow_RoundRobinRatings)
{
	const int Racks = RacksPerPair(kRelease ? 200 : 1);
	std::vector<PairResult> Pairs;
	for (int j = 1; j < human::kAiProfileCount; ++j)
	{
		for (int i = 0; i < j; ++i)
		{
			PairResult P;
			P.A = aitest::kProfiles[j];
			P.B = aitest::kProfiles[i];
			Pairs.push_back(P);
		}
	}
	const auto T0 = std::chrono::steady_clock::now();
	PlayPairs(Pairs, Racks, rules::Discipline::NineBall, kTableNineFootPro, 0xB09B09u);
	std::printf("  HF-B09 round robin, 9-ball, %d racks per pair (%zu pairs), %.1f s\n", Racks, Pairs.size(),
		std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count());

	// Wins[i][k]: racks profile i won against profile k.
	constexpr int N = human::kAiProfileCount;
	double Wins[N][N] = {};
	for (const PairResult& P : Pairs)
	{
		const int A = static_cast<int>(P.A);
		const int B = static_cast<int>(P.B);
		Wins[A][B] += P.Stats[0].RacksWon;
		Wins[B][A] += P.Stats[1].RacksWon;
		RB_CHECK(Clean(P));
	}
	// Bradley-Terry by the MM iteration: gamma_i = W_i / sum_k n_ik / (gamma_i + gamma_k); rating = 100 log2(gamma).
	double Gamma[N];
	for (double& G : Gamma)
	{
		G = 1.0;
	}
	for (int Iteration = 0; Iteration < 5000; ++Iteration)
	{
		double Next[N];
		for (int i = 0; i < N; ++i)
		{
			double W = 0.0;
			double Den = 0.0;
			for (int k = 0; k < N; ++k)
			{
				if (k != i)
				{
					W += Wins[i][k];
					Den += (Wins[i][k] + Wins[k][i]) / (Gamma[i] + Gamma[k]);
				}
			}
			Next[i] = Den > 0.0 ? Max(W, 0.5) / Den : Gamma[i]; // +0.5 guards a profile that never won
		}
		double LogMean = 0.0;
		for (int i = 0; i < N; ++i)
		{
			LogMean += std::log(Next[i]) / N;
		}
		for (int i = 0; i < N; ++i)
		{
			Gamma[i] = Next[i] / std::exp(LogMean);
		}
	}
	double Spec[N];
	double SpecMean = 0.0;
	for (int i = 0; i < N; ++i)
	{
		Spec[i] = human::GetAiProfile(aitest::kProfiles[i]).Rating;
		SpecMean += Spec[i] / N;
	}
	double Fit[N];
	double FitMean = 0.0;
	for (int i = 0; i < N; ++i)
	{
		Fit[i] = 100.0 * std::log2(Gamma[i]);
		FitMean += Fit[i] / N;
	}
	double Sxy = 0.0;
	double Sxx = 0.0;
	for (int i = 0; i < N; ++i)
	{
		Fit[i] += SpecMean - FitMean;
		Sxy += (Spec[i] - SpecMean) * (Fit[i] - SpecMean);
		Sxx += (Spec[i] - SpecMean) * (Spec[i] - SpecMean);
	}
	const double Slope = Sxy / Sxx;
	const double OddsPer100 = std::exp2(Slope);
	std::printf("  %-18s  spec rating  fitted rating\n", "profile");
	bool Ordered = true;
	for (int i = 0; i < N; ++i)
	{
		std::printf("  %-18s  %11.0f  %13.0f\n", aitest::ProfileName(aitest::kProfiles[i]), Spec[i], Fit[i]);
		Ordered = Ordered && (i == 0 || Fit[i] > Fit[i - 1]);
	}
	std::printf("  %-18s vs %-18s  measured  spec-predicted  fit-predicted\n", "profile", "profile");
	for (const PairResult& P : Pairs)
	{
		const int A = static_cast<int>(P.A);
		const int B = static_cast<int>(P.B);
		const double PredictedSpec = 1.0 / (1.0 + std::exp2(-(Spec[A] - Spec[B]) / 100.0));
		const double PredictedFit = 1.0 / (1.0 + std::exp2(-(Fit[A] - Fit[B]) / 100.0));
		std::printf("  %-18s vs %-18s  %6.1f %%  %11.1f %%  %11.1f %%\n", aitest::ProfileName(P.A), aitest::ProfileName(P.B), 100.0 * WinRate(P, 0),
			100.0 * PredictedSpec, 100.0 * PredictedFit);
	}
	std::printf("  HF-B09 slope of the fitted against the spec ratings %.3f: %.2f : 1 game odds per 100 spec points (2 : 1 +- 5 %%: [1.90, 2.10]) %s; "
				"order %s\n", Slope, OddsPer100, OddsPer100 >= 1.9 && OddsPer100 <= 2.1 ? "PASS" : "FAIL", Ordered ? "as specified" : "DIFFERS");
	if (kRelease)
	{
		RB_CHECK(Ordered);
		RB_CHECK(OddsPer100 >= 1.9 && OddsPer100 <= 2.1);
	}
}

// A-AI-10 (other disciplines): 10-ball (every pot called), Blackball (ball in hand in baulk, free shots) and 14.1 continuous
// (called shots, continuation racks) are played by the road player against the league player without an invalid declaration,
// a planner error or a simulation error. 14.1 is played for a capped number of shots per "rack" (the driver's shot cap).
RB_TEST(Integ_ARCH_AI10_Slow_OtherDisciplines)
{
	const int Racks = RacksPerPair(kRelease ? 20 : 1);
	for (const rules::Discipline Game : {rules::Discipline::TenBall, rules::Discipline::Blackball, rules::Discipline::StraightPool})
	{
		std::vector<PairResult> Pairs(1);
		Pairs[0].A = human::AiProfileId::RoadPlayer;
		Pairs[0].B = human::AiProfileId::LeaguePlayer;
		PlayPairs(Pairs, Racks, Game, kTableNineFootPro, 0x07E4u + static_cast<std::uint64_t>(Game));
		std::printf("  A-AI-10 discipline %d:\n", static_cast<int>(Game));
		PrintPair(Pairs[0]);
		RB_CHECK(Clean(Pairs[0]));
	}
}
