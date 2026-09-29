#pragma once

// Owner: WP-12 (AI opponent). Shared helpers of the planner tests (Tests/Core/Ai): tables and match configurations, scenario
// states, planner inputs for the HF 5.5 profiles, the REFEREE that executes a planned decision exactly like the game (synthetic
// hand with the match key and the shooter's streak history -> ExecuteStroke -> Simulator::Run on the real table -> rules), a
// multi-threaded driver of the staged planner protocol (std::thread, shuffled job order) and a rack / match player with
// statistics for the strength ladder and the round robins.

#include "rbtest.h"

#include "Simulator/SimTestUtil.h"

#include "rb/Ai/Planner.h"
#include "rb/Ai/PlannerProfile.h"
#include "rb/Ai/PositionEval.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Random.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Human/AiProfiles.h"
#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Rules/TableRules.h"
#include "rb/Shot/ShotRecordBuilder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace aitest
{
	using namespace rb;

	inline constexpr double kR = simtest::kR;

	inline const char* ProfileName(human::AiProfileId Id)
	{
		switch (Id)
		{
		case human::AiProfileId::Tourist: return "Tourist";
		case human::AiProfileId::BarRegular: return "Bar regular";
		case human::AiProfileId::LeaguePlayer: return "League player";
		case human::AiProfileId::LocalHustler: return "Hustler";
		case human::AiProfileId::RoadPlayer: return "Pro (road player)";
		case human::AiProfileId::TouringPro: return "Touring pro";
		}
		return "?";
	}

	inline constexpr human::AiProfileId kProfiles[human::kAiProfileCount] = {human::AiProfileId::Tourist, human::AiProfileId::BarRegular,
		human::AiProfileId::LeaguePlayer, human::AiProfileId::LocalHustler, human::AiProfileId::RoadPlayer, human::AiProfileId::TouringPro};

	inline rules::RulesTable RulesTableOf(const TableGeometry& T)
	{
		double Radii[kMaxBalls];
		for (double& R : Radii)
		{
			R = kR;
		}
		return BuildRulesTable(T, kR, Radii, kMaxBalls);
	}

	// The WPA preset of a discipline (the tests' default rules).
	inline rules::RulesPreset WpaPreset(rules::Discipline Game)
	{
		switch (Game)
		{
		case rules::Discipline::EightBall: return rules::RulesPreset::Wpa8Ball;
		case rules::Discipline::TenBall: return rules::RulesPreset::Wpa10Ball;
		case rules::Discipline::StraightPool: return rules::RulesPreset::Wpa14_1;
		case rules::Discipline::Blackball: return rules::RulesPreset::WpaBlackball;
		case rules::Discipline::NineBall: break;
		}
		return rules::RulesPreset::Wpa9Ball;
	}

	// A match of Preset's discipline and rules (house rules: BarHouse8Ball, Apa8Ball, ...).
	inline rules::MatchConfig MakeMatchWith(rules::RulesPreset Preset, const TableGeometry& T, std::uint64_t Seed)
	{
		rules::MatchConfig C;
		C.Game = rules::DisciplineOf(Preset);
		C.Rules = rules::MakeRulesConfig(Preset);
		C.RaceTo = 100000; // racks are counted by the driver
		C.Table = RulesTableOf(T);
		C.Seed = Seed;
		C.RackGaps = kRackGapWoodenRack;
		return C;
	}

	inline rules::MatchConfig MakeMatch(rules::Discipline Game, const TableGeometry& T, std::uint64_t Seed)
	{
		return MakeMatchWith(WpaPreset(Game), T, Seed);
	}

	// A mid-rack AwaitShot state: the listed object balls on the table, the others pocketed; cue ball at Cue (or in hand in
	// Region when InHand).
	inline rules::MatchState ScenarioState(const rules::MatchConfig& C, int Shooter, std::initializer_list<std::pair<int, Vec2>> Balls, const Vec2& Cue,
		bool InHand = false, rules::CueBallNext Region = rules::CueBallNext::InHandAnywhere)
	{
		rules::MatchState S;
		S.Phase = rules::MatchPhase::AwaitShot;
		S.RackNumber = 1;
		rules::GameState& G = S.Game;
		G.Game = C.Game;
		G.Shooter = Shooter;
		G.RackBreaker = 1 - Shooter;
		G.IsBreakShot = false;
		G.PushOutAvailable = false;
		G.TableOpen = C.Game == rules::Discipline::EightBall;
		const int Count = C.Game == rules::Discipline::NineBall ? 10 : (C.Game == rules::Discipline::TenBall ? 11 : 16);
		for (int b = 0; b < rules::kRulesBallCount; ++b)
		{
			G.Balls[b].Kind = b < Count ? rules::BallStatusKind::Pocketed : rules::BallStatusKind::NotUsed;
		}
		for (const auto& B : Balls)
		{
			G.Balls[B.first].Kind = rules::BallStatusKind::OnTable;
			G.Balls[B.first].Position = B.second;
		}
		if (InHand)
		{
			G.Balls[0].Kind = rules::BallStatusKind::Pocketed;
			G.CueBall = Region;
		}
		else
		{
			G.Balls[0].Kind = rules::BallStatusKind::OnTable;
			G.Balls[0].Position = Cue;
			G.CueBall = rules::CueBallNext::InPosition;
		}
		return S;
	}

	// One AI player with its equipment and match-stream noise state.
	struct Player
	{
		human::AiCharacter Character;
		ai::PlannerProfile Planner;
		human::TipState Tip;
		human::CueBodyState CueBody;
		CueSpec Cue = kCuePlaying19oz;
		human::HumanParams Human;
		human::NoiseHistory History;
		std::uint32_t ShooterId = 1;
	};

	inline Player MakePlayer(human::AiProfileId Id, std::uint32_t ShooterId, std::uint64_t Seed)
	{
		Player P;
		P.Character.Profile = human::GetAiProfile(Id);
		P.Character.CharacterSeed = Seed ^ (0x9E3779B97F4A7C15ull * ShooterId);
		P.Planner = ai::GetPlannerProfile(Id);
		P.ShooterId = ShooterId;
		return P;
	}

	inline human::NoiseKey KeyFor(std::uint64_t MatchSeed, std::uint32_t Rack, std::uint32_t Shot, const Player& P)
	{
		human::NoiseKey Key;
		Key.MatchSeed = MatchSeed;
		Key.RackIndex = Rack;
		Key.ShotIndex = Shot;
		Key.ShooterId = P.ShooterId;
		Key.ShooterShotIndex = P.History.NextIndex;
		return Key;
	}

	inline ai::PlannerInput MakeInput(const TableGeometry& T, const PhysicsParams& Physics, const rules::MatchConfig& C, const rules::MatchState& S, int Self,
		const Player& Me, const ai::OpponentModel& Opponent, const human::NoiseKey& Key)
	{
		ai::PlannerInput In;
		In.Table = &T;
		In.Physics = Physics;
		for (BallSpec& B : In.Balls)
		{
			B = MakeBallSpec(kR, kDefaultBallMass);
		}
		In.Match = C;
		In.State = S;
		In.Self = Self;
		In.Character = Me.Character;
		In.Planner = Me.Planner;
		In.Opponent = Opponent;
		In.Tip = Me.Tip;
		In.CueBody = Me.CueBody;
		In.Cue = Me.Cue;
		In.Human = Me.Human;
		In.Key = Key;
		In.MoneyDown = true;
		return In;
	}

	// Runs the staged protocol on Threads std::threads; the job order is shuffled with ShuffleSeed (0: in order), and a job goes
	// to whichever worker takes it next (an atomic counter), so the worker of a job differs between runs.
	inline ai::PlannedDecision PlanParallel(ai::PlannerScratch& Scratch, std::vector<ai::PlannerWorker>& Workers, int Threads, const ai::PlannerInput& Input,
		const ai::PlannerConfig& Config, std::uint64_t ShuffleSeed)
	{
		Scratch.Begin(Input, Config);
		std::vector<int> Order;
		while (!Scratch.Finished())
		{
			const int N = Scratch.JobCount();
			Order.resize(static_cast<std::size_t>(N));
			for (int j = 0; j < N; ++j)
			{
				Order[static_cast<std::size_t>(j)] = j;
			}
			if (ShuffleSeed != 0 && N > 1)
			{
				Rng Random(ShuffleSeed + static_cast<std::uint64_t>(N));
				for (int j = N - 1; j > 0; --j)
				{
					std::swap(Order[static_cast<std::size_t>(j)], Order[Random.NextBelow(static_cast<std::uint32_t>(j + 1))]);
				}
			}
			std::atomic<int> Next{0};
			const auto Work = [&](int w) {
				for (int k = Next.fetch_add(1); k < N; k = Next.fetch_add(1))
				{
					Scratch.RunJob(Order[static_cast<std::size_t>(k)], Workers[static_cast<std::size_t>(w)]);
				}
			};
			if (Threads <= 1)
			{
				Work(0);
			}
			else
			{
				std::vector<std::thread> Pool;
				for (int w = 0; w < Threads; ++w)
				{
					Pool.emplace_back(Work, w);
				}
				for (std::thread& Th : Pool)
				{
					Th.join();
				}
			}
			Scratch.Advance();
		}
		return Scratch.Decision();
	}

	// The game side: executes a planned stroke like the player's (principle 4) on the REAL table and applies the rules.
	struct Referee
	{
		Simulator Sim;
		ShotResult Result;
		SimInput In;
		rules::ShotFacts Facts;

		Referee() : Sim(Capacity()) { ReserveShotResult(Result, Capacity()); }

		static ResultCapacity Capacity()
		{
			ResultCapacity C;
			C.MaxLoggedEvents = 1024;
			C.MaxSegmentsPerBall = 0;
			C.MaxRecordEvents = 8192;
			C.MaxCueTipSegments = 16;
			return C;
		}
	};

	struct Executed
	{
		rules::ShotOutcome Outcome;
		SimStatus Status = SimStatus::NotImplemented;
		bool DeclarationValid = false;
		bool PottedIntended = false;
		int Pocketed = 0; // object balls pocketed
	};

	inline rules::CueBallNext RegionOf(const rules::GameState& G) { return G.CueBall; }

	inline Executed ExecuteDecision(Referee& Ref, const ai::PlannedDecision& D, Player& P, const TableGeometry& T, const PhysicsParams& Physics,
		const rules::MatchConfig& C, rules::MatchState& S, const human::NoiseKey& Key)
	{
		Executed Out;
		const rules::GameState& G = S.Game;
		const Vec2* Placement = D.PlaceCueBall ? &D.CueBallPlacement : nullptr;
		Out.DeclarationValid = rules::ValidateDeclaration(C, S, D.Declaration, Placement) == ErrorCode::Ok;
		const Vec2 Cue = D.PlaceCueBall ? D.CueBallPlacement : G.Balls[0].Position;
		human::BallObstacle Obstacles[kMaxBalls];
		int N = 0;
		for (int b = 1; b < rules::kRulesBallCount; ++b)
		{
			if (G.Balls[b].Kind == rules::BallStatusKind::OnTable)
			{
				Obstacles[N].Id = static_cast<BallId>(b);
				Obstacles[N].Position = ToVec3(G.Balls[b].Position, kR);
				Obstacles[N].Radius = kR;
				++N;
			}
		}
		const human::IntendedStroke I = human::SyntheticHand(D.Stroke, P.Character, D.Situation, kR, Key, P.History, P.Human);
		const human::ExecutedStroke X = human::ExecuteStroke(I, P.Character.Profile.Attributes, D.Situation, P.Tip, P.CueBody, P.Cue, MakeBallSpec(kR, kDefaultBallMass),
			ToVec3(Cue, kR), Obstacles, N, Key, P.History, P.Human);
		human::AdvanceNoiseHistory(P.History);
		SimInput& In = Ref.In;
		In = SimInput{};
		In.Table = &T;
		In.Params = Physics;
		for (int b = 0; b < rules::kRulesBallCount; ++b)
		{
			if (b == 0 || G.Balls[b].Kind == rules::BallStatusKind::OnTable)
			{
				simtest::Place(In, b, ToVec3(b == 0 ? Cue : G.Balls[b].Position, kR));
			}
		}
		StrikeRequest R;
		R.Ball = 0;
		R.Input = X.Strike;
		In.Strikes.PushBack(R);
		if (D.PlaceCueBall)
		{
			In.Context.InHand = G.CueBall == rules::CueBallNext::InHandAboveHeadString ? CueBallInHand::AboveHeadString
				: (G.CueBall == rules::CueBallNext::InHandBaulk ? CueBallInHand::Baulk : CueBallInHand::Anywhere);
			In.Context.PlacedPosition = Cue;
		}
		In.Record.Trajectories = false;
		In.Record.EventStates = false;
		In.Record.LogTransitions = false;
		In.Record.LogObservers = false;
		In.Record.ShotRecord = true;
		Out.Status = X.Error == ErrorCode::Ok ? Ref.Sim.Run(In, Ref.Result) : SimStatus::InvalidInput;
		if (Out.Status != SimStatus::Ok)
		{
			// Not expected; the planner's shot could not be simulated: count it as a foul-free miss (turn passes).
			return Out;
		}
		rules::DeriveShotFacts(Ref.Result.Record, C.Table, C.Rules.Tolerances, kInfinity, Ref.Facts);
		Out.Outcome = rules::EvaluateShot(C.Rules, C.Table, G, D.Declaration, Ref.Facts);
		Out.PottedIntended = D.PotBall != kNoBall && Ref.Facts.IsPocketed(D.PotBall);
		for (const rules::PocketedBall& Pb : Ref.Facts.Pocketed)
		{
			Out.Pocketed += Pb.Ball != 0 ? 1 : 0;
		}
		rules::ApplyShot(C, S, Out.Outcome, Ref.Facts);
		return Out;
	}

	// Statistics of one player over racks.
	struct Stats
	{
		int Racks = 0;
		int RacksWon = 0;
		int BreakAndRuns = 0;      // won the rack in the break inning
		int RunOuts = 0;           // won the rack in one inning that started with >= 5 object balls on the table (incl. break-and-runs)
		int Shots = 0;
		int PotAttempts = 0;
		int PotsMade = 0;          // intended ball pocketed
		int Safeties = 0;
		int PushOuts = 0;
		int SpotRequests = 0;      // rules 4.4 spot requests (RequestSpot decisions)
		int Fouls = 0;
		int Breaks = 0;
		int BreakPots = 0;         // breaks that pocketed a ball
		int InvalidDeclarations = 0;
		int PlannerErrors = 0;
		int SimulationErrors = 0;
		double DecisionSeconds = 0.0;
		int Decisions = 0;
		long long Simulations = 0;
	};

	struct RackResult
	{
		int Winner = -1;  // -1: stalled / aborted
		int Shots = 0;
	};

	// RB_AI_TRACE=1 prints every decision of PlayRack (diagnosis of experiments).
	inline bool TraceOn()
	{
#if defined(_MSC_VER)
		char* Value = nullptr;
		std::size_t Length = 0;
		const bool On = _dupenv_s(&Value, &Length, "RB_AI_TRACE") == 0 && Value != nullptr && Value[0] == '1';
		std::free(Value);
		return On;
#else
		const char* Value = std::getenv("RB_AI_TRACE");
		return Value != nullptr && Value[0] == '1';
#endif
	}

	// Plays one rack from RackSetup / RackOver (the rack's breaker is S.Game.RackBreaker) to its end. Planner decisions on the
	// calling thread (PlanShot) unless Workers is non-null.
	inline RackResult PlayRack(const TableGeometry& T, const PhysicsParams& Physics, const rules::MatchConfig& C, rules::MatchState& S, Player* Players,
		const ai::OpponentModel* Models, Stats* St, ai::PlannerScratch& Scratch, Referee& Ref, std::uint32_t& ShotIndex, const ai::PlannerConfig& Config,
		int MaxShots = 150)
	{
		RackResult Out;
		rules::RackAssignment Rack;
		if (rules::SetupRack(C, S, Rack) != ErrorCode::Ok)
		{
			return Out;
		}
		const std::uint32_t RackIndex = static_cast<std::uint32_t>(S.RackNumber);
		const int Breaker = S.Game.Shooter;
		St[0].Racks++;
		St[1].Racks++;
		int InningShooter = -1;
		int InningStartBalls = 0;
		bool BreakInning = true;
		const bool Trace = TraceOn();
		while (Out.Shots < MaxShots)
		{
			if (S.Phase == rules::MatchPhase::AwaitDecision)
			{
				const int Decider = S.Decider;
				const human::NoiseKey Key = KeyFor(C.Seed, RackIndex, ShotIndex, Players[Decider]);
				const ai::PlannerInput In = MakeInput(T, Physics, C, S, Decider, Players[Decider], Models[1 - Decider], Key);
				const ai::PlannedDecision D = ai::PlanShot(In, Config, Scratch);
				if (Trace)
				{
					char Text[400];
					ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
					std::printf("    [rack %u] P%d decides: %s\n", RackIndex, Decider, Text);
				}
				if (D.Kind != ai::DecisionKind::Option || rules::ApplyOption(C, S, D.Choice) != ErrorCode::Ok)
				{
					St[Decider].PlannerErrors++;
					rules::ApplyOption(C, S, S.PendingOutcome.Options[0]);
				}
				continue;
			}
			if (S.Phase != rules::MatchPhase::AwaitShot)
			{
				break;
			}
			const int Me = S.Game.Shooter;
			if (Me != InningShooter)
			{
				InningShooter = Me;
				InningStartBalls = rules::CountObjectBallsOnTable(S.Game);
				BreakInning = S.Game.IsBreakShot;
			}
			const bool WasBreak = S.Game.IsBreakShot;
			const human::NoiseKey Key = KeyFor(C.Seed, RackIndex, ShotIndex, Players[Me]);
			const ai::PlannerInput In = MakeInput(T, Physics, C, S, Me, Players[Me], Models[1 - Me], Key);
			const auto T0 = std::chrono::steady_clock::now();
			const ai::PlannedDecision D = ai::PlanShot(In, Config, Scratch);
			St[Me].DecisionSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
			St[Me].Decisions++;
			St[Me].Simulations += D.Reasoning.Simulations;
			if (D.Kind == ai::DecisionKind::RequestSpot)
			{
				// Rules 4.4: the game spots the ball and asks the planner again.
				if (Trace)
				{
					char Text[400];
					ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
					std::printf("    [rack %u] P%d %s\n", RackIndex, Me, Text);
				}
				St[Me].SpotRequests++;
				if (rules::RequestSpot(C, S) != ErrorCode::Ok)
				{
					St[Me].PlannerErrors++;
					break;
				}
				continue;
			}
			if (D.Kind != ai::DecisionKind::Stroke)
			{
				if (Trace)
				{
					std::printf("    [rack %u] P%d planner error %d (phase %d, cue ball %d, in hand %d)\n", RackIndex, Me, static_cast<int>(D.Error),
						static_cast<int>(S.Phase), static_cast<int>(S.Game.Balls[0].Kind), static_cast<int>(S.Game.CueBall));
				}
				St[Me].PlannerErrors++;
				break;
			}
			const int BallsBefore = rules::CountObjectBallsOnTable(S.Game);
			const rules::GameState Before = S.Game;
			const Executed X = ExecuteDecision(Ref, D, Players[Me], T, Physics, C, S, Key);
			if (Trace)
			{
				char Text[400];
				ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
				std::printf("    [rack %u shot %u] P%d (%d balls) %s\n        -> %s, %d pocketed, foul %d (%s), next %d\n", RackIndex, ShotIndex, Me, BallsBefore, Text,
					X.PottedIntended ? "MADE" : "missed", X.Pocketed, X.Outcome.AnyFoul ? 1 : 0, X.Outcome.RuleRef, static_cast<int>(X.Outcome.Next));
				if (D.FoulChance >= 1.0)
				{
					std::printf("        planned foul; balls before:");
					for (int b = 0; b < rules::kRulesBallCount; ++b)
					{
						if (Before.Balls[b].Kind == rules::BallStatusKind::OnTable)
						{
							std::printf(" %d(%.3f,%.3f)", b, Before.Balls[b].Position.x, Before.Balls[b].Position.y);
						}
					}
					std::printf(" cue ball next %d\n", static_cast<int>(Before.CueBall));
				}
				for (const ai::ConsideredShot& Top : D.Reasoning.Top)
				{
					std::printf("        top: %-11s first %2d pot %2d pocket %3d score %.3f pot %.2f foul %.2f V %.2f A %.2f B %.2f (%d samples)\n",
						ai::ShotTypeName(Top.Type), Top.FirstBall, Top.PotBall, static_cast<int>(Top.Pocket), Top.Score, Top.PotChance, Top.FoulChance, Top.Speed, Top.SpinA,
						Top.SpinB, Top.Samples);
				}
			}
			++ShotIndex;
			++Out.Shots;
			St[Me].Shots++;
			St[Me].InvalidDeclarations += X.DeclarationValid ? 0 : 1;
			if (X.Status != SimStatus::Ok)
			{
				St[Me].SimulationErrors++;
				break;
			}
			St[Me].Fouls += X.Outcome.AnyFoul ? 1 : 0;
			if (WasBreak)
			{
				St[Me].Breaks++;
				St[Me].BreakPots += X.Pocketed > 0 ? 1 : 0;
			}
			else if (ai::IsPotShot(D.Type))
			{
				St[Me].PotAttempts++;
				St[Me].PotsMade += X.PottedIntended ? 1 : 0;
			}
			else if (D.Type == ai::ShotType::PushOut)
			{
				St[Me].PushOuts++;
			}
			else
			{
				St[Me].Safeties++;
			}
			if (X.Outcome.Next == rules::NextAction::RackWon || X.Outcome.Next == rules::NextAction::MatchWon)
			{
				Out.Winner = X.Outcome.Winner;
				if (Out.Winner == Me)
				{
					St[Me].BreakAndRuns += BreakInning && Me == Breaker ? 1 : 0;
					St[Me].RunOuts += InningStartBalls >= 5 ? 1 : 0;
				}
				break;
			}
			if (S.Phase == rules::MatchPhase::RackSetup)
			{
				break; // re-rack (8 on the break options, stalemate): no winner
			}
		}
		if (Out.Winner >= 0)
		{
			St[Out.Winner].RacksWon++;
		}
		return Out;
	}

	// Starts a match state for Game with player Breaker breaking the first rack.
	inline rules::MatchState StartMatchState(const rules::MatchConfig& C, int Breaker)
	{
		rules::MatchState S;
		rules::StartMatch(C, S);
		rules::LagResult Lag;
		Lag.Outcome = rules::LagOutcome::FirstWins;
		rules::ApplyLagResult(C, S, Lag);
		rules::ChooseBreaker(C, S, Breaker);
		return S;
	}

	// After a rack (RackOver) or a stall: the next rack with alternating breaks.
	inline void NextRack(const rules::MatchConfig& C, rules::MatchState& S, int NextBreaker)
	{
		if (S.Phase == rules::MatchPhase::AwaitShot || S.Phase == rules::MatchPhase::AwaitDecision)
		{
			rules::DeclareStalemate(C, S); // a stalled rack (shot cap or an error): re-rack, the driver counts no winner
		}
		S.Game.RackBreaker = NextBreaker;
		S.Game.Shooter = NextBreaker;
	}
}
