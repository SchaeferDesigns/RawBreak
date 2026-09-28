// Owner: WP-12 (AI opponent). A-AI-1: the planner's decision is bitwise identical for 1 and N threads, for shuffled job orders,
// for fresh and reused scratches and workers, and for repeated runs (Docs/architecture.md 7.6: a job's result is a pure function
// of the stage data and its index; reductions run in index order). Every profile, on real mid-rack positions (9-ball after a
// seeded break and a few shots, the break itself, ball in hand, a push-out window, 8-ball with groups).

#include "Ai/AiTestUtil.h"

#include <cstring>

using namespace rb;
using aitest::kR;

namespace
{
	const TableGeometry& NineFoot() { return simtest::Table(kTableNineFootPro); }

	const PhysicsParams& NineFootParams()
	{
		static const PhysicsParams P = MakePhysicsParams(kTableNineFootPro);
		return P;
	}

	std::uint64_t Bits(double X)
	{
		std::uint64_t U = 0;
		std::memcpy(&U, &X, sizeof(U));
		return U;
	}

	bool SameShot(const ai::ConsideredShot& A, const ai::ConsideredShot& B)
	{
		return A.Type == B.Type && A.FirstBall == B.FirstBall && A.PotBall == B.PotBall && A.Pocket == B.Pocket && Bits(A.Score) == Bits(B.Score) &&
			Bits(A.PotChance) == Bits(B.PotChance) && Bits(A.FoulChance) == Bits(B.FoulChance) && Bits(A.Speed) == Bits(B.Speed) && A.Samples == B.Samples;
	}

	bool SameDecision(const ai::PlannedDecision& A, const ai::PlannedDecision& B)
	{
		bool Same = A.Error == B.Error && A.Kind == B.Kind && A.Type == B.Type && A.TargetBall == B.TargetBall && A.PotBall == B.PotBall && A.Pocket == B.Pocket &&
			A.Choice == B.Choice && A.PlaceCueBall == B.PlaceCueBall && Bits(A.CueBallPlacement.x) == Bits(B.CueBallPlacement.x) &&
			Bits(A.CueBallPlacement.y) == Bits(B.CueBallPlacement.y) && Bits(A.Stroke.Azimuth) == Bits(B.Stroke.Azimuth) &&
			Bits(A.Stroke.Elevation) == Bits(B.Stroke.Elevation) && Bits(A.Stroke.AxisOffsetA) == Bits(B.Stroke.AxisOffsetA) &&
			Bits(A.Stroke.AxisOffsetB) == Bits(B.Stroke.AxisOffsetB) && Bits(A.Stroke.Speed) == Bits(B.Stroke.Speed) && A.Stroke.Bridge == B.Stroke.Bridge &&
			Bits(A.Situation.Pressure) == Bits(B.Situation.Pressure) && Bits(A.Situation.ElevationFloor) == Bits(B.Situation.ElevationFloor) &&
			A.Declaration.Kind == B.Declaration.Kind && A.Declaration.Called.Ball == B.Declaration.Called.Ball &&
			A.Declaration.Called.Pocket == B.Declaration.Called.Pocket && Bits(A.ExpectedValue) == Bits(B.ExpectedValue) &&
			Bits(A.PotChance) == Bits(B.PotChance) && Bits(A.FoulChance) == Bits(B.FoulChance) && A.Reasoning.Candidates == B.Reasoning.Candidates &&
			A.Reasoning.Simulations == B.Reasoning.Simulations && A.Reasoning.NoisyCandidates == B.Reasoning.NoisyCandidates &&
			A.Reasoning.SecondPlyPositions == B.Reasoning.SecondPlyPositions && A.Reasoning.Top.Size() == B.Reasoning.Top.Size();
		for (int i = 0; Same && i < A.Reasoning.Top.Size(); ++i)
		{
			Same = SameShot(A.Reasoning.Top[i], B.Reasoning.Top[i]);
		}
		return Same;
	}

	struct Position
	{
		const char* Name;
		rules::MatchConfig Config;
		rules::MatchState State;
	};

	// Real positions: a seeded 9-ball rack is broken and played for a few shots by two road players (the referee on the real
	// table); the states before every shot are collected.
	void CollectNineBall(std::vector<Position>& Out, int Count)
	{
		const TableGeometry& T = NineFoot();
		static ai::PlannerScratch Scratch;
		static aitest::Referee Ref;
		const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 0xD37u);
		rules::MatchState S = aitest::StartMatchState(C, 0);
		rules::RackAssignment Rack;
		rules::SetupRack(C, S, Rack);
		aitest::Player Players[2] = {aitest::MakePlayer(human::AiProfileId::RoadPlayer, 1, 5), aitest::MakePlayer(human::AiProfileId::RoadPlayer, 2, 6)};
		std::uint32_t Shot = 0;
		while (static_cast<int>(Out.size()) < Count && S.Phase == rules::MatchPhase::AwaitShot)
		{
			Out.push_back({S.Game.IsBreakShot ? "9-ball break" : "9-ball mid-rack", C, S});
			const int Me = S.Game.Shooter;
			const human::NoiseKey Key = aitest::KeyFor(C.Seed, 1, Shot, Players[Me]);
			ai::PlannerConfig Fast;
			Fast.Samples = 0.25;
			const ai::PlannedDecision D = ai::PlanShot(
				aitest::MakeInput(T, NineFootParams(), C, S, Me, Players[Me], ai::OpponentModelFor(human::AiProfileId::RoadPlayer), Key), Fast, Scratch);
			if (D.Kind != ai::DecisionKind::Stroke)
			{
				break;
			}
			aitest::ExecuteDecision(Ref, D, Players[Me], T, NineFootParams(), C, S, Key);
			++Shot;
			while (S.Phase == rules::MatchPhase::AwaitDecision)
			{
				rules::ApplyOption(C, S, S.PendingOutcome.Options[0]);
			}
		}
	}

	std::vector<Position> Positions()
	{
		std::vector<Position> Out;
		CollectNineBall(Out, 4);
		const TableGeometry& T = NineFoot();
		const rules::MatchConfig C9 = aitest::MakeMatch(rules::Discipline::NineBall, T, 11);
		Out.push_back({"9-ball ball in hand", C9,
			aitest::ScenarioState(C9, 1, {{2, {0.4, 0.2}}, {4, {-0.3, -0.35}}, {6, {0.9, 0.45}}, {9, {1.0, -0.2}}}, {}, true)});
		rules::MatchState Push = aitest::ScenarioState(C9, 0, {{1, {0.9, 0.0}}, {2, {0.9 - 2.0 * kR - 0.003, 2.0 * kR}}, {3, {0.9 - 2.0 * kR - 0.003, -2.0 * kR}},
			{5, {0.2, 0.4}}, {9, {-0.9, 0.4}}}, {-0.5, 0.05});
		Push.Game.PushOutAvailable = true;
		Out.push_back({"9-ball push-out window", C9, Push});
		const rules::MatchConfig C8 = aitest::MakeMatch(rules::Discipline::EightBall, T, 12);
		rules::MatchState Eight = aitest::ScenarioState(C8, 0,
			{{2, {0.5, 0.3}}, {3, {-0.6, -0.4}}, {7, {0.9, -0.2}}, {8, {0.2, 0.0}}, {10, {1.0, 0.5}}, {12, {-0.2, 0.45}}, {15, {0.7, -0.5}}}, {-0.8, 0.1});
		Eight.Game.TableOpen = false;
		Eight.Game.Players[0].Group = rules::BallGroup::Solids;
		Eight.Game.Players[1].Group = rules::BallGroup::Stripes;
		Out.push_back({"8-ball groups", C8, Eight});
		rules::MatchState Lag;
		rules::StartMatch(C9, Lag);
		Out.push_back({"9-ball lag", C9, Lag});
		return Out;
	}
}

RB_TEST(Integ_ARCH_AI1_DecisionsIdenticalForAnyThreadCount)
{
	const std::vector<Position> Set = Positions();
	RB_REQUIRE(Set.size() >= 5);
	static ai::PlannerScratch Serial;
	static ai::PlannerScratch Parallel;
	std::vector<ai::PlannerWorker> Workers(4);
	int Compared = 0;
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		aitest::Player Me = aitest::MakePlayer(Id, 1, 77);
		for (std::size_t k = 0; k < Set.size(); ++k)
		{
			const Position& P = Set[k];
			const int Self = P.State.Game.Shooter;
			const human::NoiseKey Key = aitest::KeyFor(0x5EEDu + k, 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannerInput In = aitest::MakeInput(NineFoot(), NineFootParams(), P.Config, P.State, Self, Me, ai::OpponentModelFor(Id), Key);
			const ai::PlannerConfig Config;
			const ai::PlannedDecision A = ai::PlanShot(In, Config, Serial);
			const ai::PlannedDecision B = aitest::PlanParallel(Parallel, Workers, 4, In, Config, 1);
			const ai::PlannedDecision C = aitest::PlanParallel(Parallel, Workers, 3, In, Config, 0x9E37u + k);
			ai::PlannerScratch Fresh; // a fresh scratch and worker after the others ran
			const ai::PlannedDecision D = ai::PlanShot(In, Config, Fresh);
			const ai::PlannedDecision E = ai::PlanShot(In, Config, Serial); // repeated on the reused scratch
			RB_CHECK(A.Error == ErrorCode::Ok && A.Kind == ai::DecisionKind::Stroke);
			const bool Same = SameDecision(A, B) && SameDecision(A, C) && SameDecision(A, D) && SameDecision(A, E);
			if (!Same)
			{
				char Text[400];
				ai::FormatReasoning(A, Text, static_cast<int>(sizeof(Text)));
				std::printf("  A-AI-1 MISMATCH %s, %s: %s\n", aitest::ProfileName(Id), P.Name, Text);
				ai::FormatReasoning(B, Text, static_cast<int>(sizeof(Text)));
				std::printf("         4 threads: %s\n", Text);
			}
			RB_CHECK(Same);
			++Compared;
		}
	}
	std::printf("  A-AI-1 %d decisions (6 profiles x %zu positions) bitwise identical: serial, 4 and 3 threads with shuffled jobs, fresh and "
				"reused scratch\n", Compared, Set.size());
}

// FinishNow after any stage (the wall-clock escape of the Unreal side) gives a valid stroke, the same one on every run, and after
// the last stage the same one as the full protocol.
RB_TEST(Integ_ARCH_AI1_FinishNowAfterAnyStage)
{
	const std::vector<Position> Set = Positions();
	aitest::Player Me = aitest::MakePlayer(human::AiProfileId::TouringPro, 1, 91);
	static ai::PlannerScratch Planner;
	for (std::size_t k = 0; k < Set.size(); ++k)
	{
		const Position& P = Set[k];
		const human::NoiseKey Key = aitest::KeyFor(0xF1u + k, 1, static_cast<std::uint32_t>(k), Me);
		const ai::PlannerInput In =
			aitest::MakeInput(NineFoot(), NineFootParams(), P.Config, P.State, P.State.Game.Shooter, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), Key);
		const ai::PlannedDecision Full = ai::PlanShot(In, ai::PlannerConfig{}, Planner);
		for (int Stages = 1; Stages <= 3; ++Stages)
		{
			ai::PlannedDecision Early[2];
			for (ai::PlannedDecision& D : Early)
			{
				Planner.Begin(In, ai::PlannerConfig{});
				for (int Stage = 0; Stage < Stages && !Planner.Finished(); ++Stage)
				{
					for (int j = 0; j < Planner.JobCount(); ++j)
					{
						Planner.RunJob(j, Planner.SerialWorker());
					}
					if (Stage + 1 < Stages)
					{
						Planner.Advance();
					}
				}
				Planner.FinishNow();
				RB_CHECK(Planner.Finished());
				D = Planner.Decision();
			}
			RB_CHECK(Early[0].Kind == ai::DecisionKind::Stroke && SameDecision(Early[0], Early[1]));
			if (Stages == 3)
			{
				RB_CHECK(SameDecision(Early[0], Full));
			}
		}
	}
}
