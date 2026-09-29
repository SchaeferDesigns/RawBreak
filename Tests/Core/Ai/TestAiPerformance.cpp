// Owner: WP-12 (AI opponent). A-AI-6: decision time per profile (Release; prior-art 7.3: an AI decision within about 1.5 s of wall
// time on 6 worker threads). Real positions: seeded 9-ball racks played by two road players (breaks, mid-rack, ball in hand,
// push-out windows), 8-ball racks on the 7-ft bar table and 14.1 racks (the opening safety break and full-rack positions, added by
// the WP-12 review); every profile decides each position on 1 thread (PlanShot) and on 6
// threads (the staged protocol on std::threads). Median / p90 / max wall time, simulations and candidates per decision are printed;
// the touring pro's worst decision on 6 threads must stay within the 1.5 s budget (Release). The simulation count is the
// deterministic budget (never wall time), so these times vary with the machine only.

#include "Ai/AiTestUtil.h"

using namespace rb;

namespace
{
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr bool kRelease = false;
	constexpr int kPositions = 3;
#else
	constexpr bool kRelease = true;
	constexpr int kPositions = 24;
#endif

	struct Position
	{
		rules::MatchConfig Config;
		rules::MatchState State;
		const TableGeometry* Table = nullptr;
		PhysicsParams Physics;
	};

	void Collect(std::vector<Position>& Out, rules::Discipline Game, const TableSpec& Spec, std::uint64_t Seed, int Count)
	{
		const TableGeometry& T = simtest::Table(Spec);
		const PhysicsParams Physics = MakePhysicsParams(Spec);
		auto Scratch = std::make_unique<ai::PlannerScratch>();
		auto Ref = std::make_unique<aitest::Referee>();
		int Added = 0;
		for (std::uint64_t Rack = 0; Added < Count && Rack < 20; ++Rack)
		{
			const rules::MatchConfig C = aitest::MakeMatch(Game, T, Seed + Rack);
			rules::MatchState S = aitest::StartMatchState(C, static_cast<int>(Rack % 2));
			rules::RackAssignment Assignment;
			rules::SetupRack(C, S, Assignment);
			aitest::Player Players[2] = {aitest::MakePlayer(human::AiProfileId::RoadPlayer, 1, Seed + Rack), aitest::MakePlayer(human::AiProfileId::RoadPlayer, 2, Seed + Rack + 7)};
			std::uint32_t Shot = 0;
			while (Added < Count && Shot < 40)
			{
				while (S.Phase == rules::MatchPhase::AwaitDecision)
				{
					rules::ApplyOption(C, S, S.PendingOutcome.Options[0]);
				}
				if (S.Phase != rules::MatchPhase::AwaitShot)
				{
					break;
				}
				Out.push_back({C, S, &T, Physics});
				++Added;
				const int Me = S.Game.Shooter;
				const human::NoiseKey Key = aitest::KeyFor(C.Seed, 1, Shot, Players[Me]);
				ai::PlannerConfig Fast;
				Fast.Samples = 0.25;
				const ai::PlannedDecision D =
					ai::PlanShot(aitest::MakeInput(T, Physics, C, S, Me, Players[Me], ai::OpponentModelFor(human::AiProfileId::RoadPlayer), Key), Fast, *Scratch);
				if (D.Kind != ai::DecisionKind::Stroke)
				{
					break;
				}
				aitest::ExecuteDecision(*Ref, D, Players[Me], T, Physics, C, S, Key);
				++Shot;
			}
		}
	}

	double Percentile(std::vector<double> V, double P)
	{
		if (V.empty())
		{
			return 0.0;
		}
		std::sort(V.begin(), V.end());
		const std::size_t I = static_cast<std::size_t>(P * static_cast<double>(V.size() - 1) + 0.5);
		return V[I < V.size() ? I : V.size() - 1];
	}
}

RB_TEST(Integ_ARCH_AI6_Slow_DecisionTimePerProfile)
{
	std::vector<Position> Set;
	Collect(Set, rules::Discipline::NineBall, kTableNineFootPro, 0xA16u, kPositions);
	Collect(Set, rules::Discipline::EightBall, kTableSevenFootBar, 0xA18u, kPositions / 2);
	// WP-12 review: 14.1 with up to 15 balls on the table (the opening safety break and the first shots of a rack) are the most
	// expensive decisions (more families, safeties and costlier rollouts).
	Collect(Set, rules::Discipline::StraightPool, kTableNineFootPro, 0xA14u, kPositions / 4);
	RB_REQUIRE(!Set.empty());
	auto Scratch = std::make_unique<ai::PlannerScratch>();
	std::vector<ai::PlannerWorker> Workers(6);
	std::printf("  A-AI-6 %zu positions (9-ball and 14.1 on the 9-ft pro table, 8-ball on the 7-ft bar table), %u hardware threads\n", Set.size(),
		std::thread::hardware_concurrency());
	std::printf("  %-18s | 1 thread: median   p90     max [ms] | 6 threads: median   p90     max [ms] | sims/decision  candidates\n", "profile");
	// The six profiles as specified, and the touring pro widened toward the prior-art search shape (PlannerConfig: twice the
	// breadth, K = 32 samples on the top 64, a larger budget): the headroom of the 1.5 s budget.
	struct Row
	{
		human::AiProfileId Id;
		ai::PlannerConfig Config;
		const char* Label;
	};
	std::vector<Row> Rows;
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		Rows.push_back({Id, ai::PlannerConfig{}, aitest::ProfileName(Id)});
	}
	ai::PlannerConfig Wide;
	Wide.Breadth = 2.0;
	Wide.Samples = 2.0;
	Wide.SimulationBudget = 30000;
	Rows.push_back({human::AiProfileId::TouringPro, Wide, "Touring pro, wide"});
	for (const Row& Each : Rows)
	{
		const human::AiProfileId Id = Each.Id;
		aitest::Player Me = aitest::MakePlayer(Id, 1, 6);
		std::vector<double> One;
		std::vector<double> Six;
		double Sims = 0.0;
		double Candidates = 0.0;
		for (std::size_t k = 0; k < Set.size(); ++k)
		{
			const Position& P = Set[k];
			const int Self = P.State.Game.Shooter;
			const human::NoiseKey Key = aitest::KeyFor(0x71A3u + k, 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannerInput In = aitest::MakeInput(*P.Table, P.Physics, P.Config, P.State, Self, Me, ai::OpponentModelFor(Id), Key);
			const auto T0 = std::chrono::steady_clock::now();
			const ai::PlannedDecision A = ai::PlanShot(In, Each.Config, *Scratch);
			const auto T1 = std::chrono::steady_clock::now();
			const ai::PlannedDecision B = aitest::PlanParallel(*Scratch, Workers, 6, In, Each.Config, 0);
			const auto T2 = std::chrono::steady_clock::now();
			RB_CHECK(A.Kind == ai::DecisionKind::Stroke && B.Kind == ai::DecisionKind::Stroke && A.Reasoning.Simulations == B.Reasoning.Simulations);
			One.push_back(1e3 * std::chrono::duration<double>(T1 - T0).count());
			Six.push_back(1e3 * std::chrono::duration<double>(T2 - T1).count());
			Sims += A.Reasoning.Simulations;
			Candidates += A.Reasoning.Candidates;
		}
		const double N = static_cast<double>(Set.size());
		std::printf("  %-18s |          %7.1f %7.1f %7.1f       |           %7.1f %7.1f %7.1f       | %8.0f %10.0f\n", Each.Label,
			Percentile(One, 0.5), Percentile(One, 0.9), Percentile(One, 1.0), Percentile(Six, 0.5), Percentile(Six, 0.9), Percentile(Six, 1.0), Sims / N,
			Candidates / N);
		if (kRelease)
		{
			RB_CHECK(Percentile(Six, 1.0) <= 1500.0); // prior-art 7.3: about 1.5 s on 6 threads
		}
	}
}
