// Owner: WP-12 (AI opponent). Adversarial tests of the planner (WP-12 review, Docs/architecture.md 7.6); every shot is executed by
// the referee like the game would (Ai/AiTestUtil.h):
//   A-AI-14 FinishNow on partial stages (the Unreal side's wall-clock escape): right after Begin, after part of the screening
//           jobs, after part of the noisy jobs on a scratch that decided another position before: a valid stroke, bitwise the
//           same as on a fresh scratch (no stale result of an earlier decision counts); Progress shows the screening's best
//           while the noisy stage runs;
//   A-AI-15 fuzzed positions of all five disciplines (random ball sets, balls frozen to cushions and to each other, the cue ball
//           in hand in every region or in position, open tables and groups, foul counts, push-out windows, free shots): every
//           profile returns a stroke with a valid declaration and a finite plan that the referee simulates;
//   A-AI-16 rules 3.11: with the cue ball in hand behind the head string no profile plays a ball above the head string directly
//           (a sure foul); it plays the ball below the string from the kitchen;
//   A-AI-17 the nearest legal balls hidden: a profile without a safety game hits the visible legal ball instead of kicking at the
//           hidden ones; the league player and the hustler do not foul on purpose when a legal safety is worth as much;
//   A-AI-18 14.1: the three-foul penalty's re-rack is valued with its 16 points (not as a fresh rack);
//   A-AI-19 14.1 opening break: the profiles with a safety game play a safety break (soft, the other player has a poor first
//           shot, few breaking fouls), the others break hard;
//   A-AI-20 the deterministic simulation budget caps a decision; the break candidates are distinct and follow the profile's
//           break speed (road player and touring pro above the 7.5 m/s cap of the other strokes);
//   A-AI-21 push-out response: every profile shoots after a push-out that leaves an easy shot and passes back one that leaves
//           the ball hidden;
//   A-AI-22 rules 4.4: in hand behind the head string with every legal ball behind it (14.1 after a scratch, 8-ball with the own
//           group in the kitchen) every profile asks for the spot (RequestSpot, the ball nearest the head string) instead of a
//           sure foul, then plays the spotted ball legally from the kitchen; the static value of such a position is that of the
//           spotted one;
//   A-AI-23 three-foul rule in action (9-ball, the AI on two fouls, the 1 hidden behind a wall): no profile plans a sure foul,
//           every profile kicks, the kicking games make a legal contact in most executions;
//   A-AI-24 two planners on two threads at once (two AI players deciding concurrently, each with its own scratch and worker)
//           decide bitwise the same as one after the other: no shared mutable state between planner instances;
//   A-AI-25 8-ball last-pocket rule (bar house rules): on the 8, no profile plans or calls it into any pocket but the one of its
//           group's last ball, even when another pocket is easier; the static evaluator's next shot uses that pocket too;
//   A-AI-26 14.1 match ball (both players one point short): the pot that wins the match is a planned win (not capped at the value
//           of keeping the table like a lucky noise-free win), played with the game-ball pressure;
//   A-AI-27 options of every rule set reached by real soft breaks: every profile picks an offered option the rules accept, and
//           the decision that follows it is planned without an error.

#include "Ai/AiTestUtil.h"

#include <cmath>
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

	ai::PlannerScratch& Scratch()
	{
		static ai::PlannerScratch S;
		return S;
	}

	aitest::Referee& Ref()
	{
		static aitest::Referee R;
		return R;
	}

#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr bool kRelease = false;
	constexpr int kTrials = 3;
	constexpr int kFuzzCases = 2;
#else
	constexpr bool kRelease = true;
	constexpr int kTrials = 10;
	constexpr int kFuzzCases = 12;
#endif

	bool Finite(double X) { return std::isfinite(X); }

	std::uint64_t Bits(double X)
	{
		std::uint64_t U = 0;
		std::memcpy(&U, &X, sizeof(U));
		return U;
	}

	bool SameDecision(const ai::PlannedDecision& A, const ai::PlannedDecision& B)
	{
		return A.Error == B.Error && A.Kind == B.Kind && A.Type == B.Type && A.TargetBall == B.TargetBall && A.PotBall == B.PotBall && A.Pocket == B.Pocket &&
			A.PlaceCueBall == B.PlaceCueBall && Bits(A.CueBallPlacement.x) == Bits(B.CueBallPlacement.x) && Bits(A.CueBallPlacement.y) == Bits(B.CueBallPlacement.y) &&
			Bits(A.Stroke.Azimuth) == Bits(B.Stroke.Azimuth) && Bits(A.Stroke.Speed) == Bits(B.Stroke.Speed) && Bits(A.Stroke.AxisOffsetA) == Bits(B.Stroke.AxisOffsetA) &&
			Bits(A.Stroke.AxisOffsetB) == Bits(B.Stroke.AxisOffsetB) && Bits(A.ExpectedValue) == Bits(B.ExpectedValue) && Bits(A.PotChance) == Bits(B.PotChance) &&
			Bits(A.FoulChance) == Bits(B.FoulChance) && A.Reasoning.Simulations == B.Reasoning.Simulations;
	}

	void RunStage(ai::PlannerScratch& P, int Step)
	{
		for (int j = 0; j < P.JobCount(); j += Step)
		{
			P.RunJob(j, P.SerialWorker());
		}
	}

	struct Fuzz
	{
		rules::MatchConfig C;
		rules::MatchState S;
	};

	// A random spot on the bed (not over a pocket opening, no overlap); now and then frozen to a cushion or to a ball placed before.
	Vec2 Spot(Rng& R, const TableGeometry& T, const rules::MatchConfig& C, const Vec2* Taken, int Count)
	{
		const double Xm = T.HalfLength - kR;
		const double Ym = T.HalfWidth - kR;
		for (int Try = 0; Try < 2000; ++Try)
		{
			Vec2 P{R.NextUniform(-Xm + 0.001, Xm - 0.001), R.NextUniform(-Ym + 0.001, Ym - 0.001)};
			const double Mode = R.NextDouble01();
			if (Mode < 0.15)
			{
				P = R.NextDouble01() < 0.5 ? Vec2{P.x, P.y > 0.0 ? Ym - 1e-5 : -Ym + 1e-5} : Vec2{P.x > 0.0 ? Xm - 1e-5 : -Xm + 1e-5, P.y};
			}
			else if (Mode < 0.3 && Count > 0)
			{
				const double A = R.NextUniform(-kPi, kPi);
				P = Taken[R.NextBelow(static_cast<std::uint32_t>(Count))] + Vec2{Cos(A), Sin(A)} * (2.0 * kR + 1e-5);
			}
			if (Abs(P.x) > Xm || Abs(P.y) > Ym || rules::OverPocketOpening(P, C.Table))
			{
				continue;
			}
			bool Clear = true;
			for (int i = 0; i < Count && Clear; ++i)
			{
				Clear = LengthSquared(P - Taken[i]) >= (2.0 * kR) * (2.0 * kR);
			}
			if (Clear)
			{
				return P;
			}
		}
		return {0.0, 0.0};
	}

	Fuzz MakeFuzz(rules::Discipline Game, std::uint64_t Seed)
	{
		const TableGeometry& T = NineFoot();
		Fuzz F;
		F.C = aitest::MakeMatch(Game, T, Seed);
		Rng R(Seed);
		rules::MatchState& S = F.S;
		S.Phase = rules::MatchPhase::AwaitShot;
		S.RackNumber = 1;
		rules::GameState& G = S.Game;
		G.Game = Game;
		G.Shooter = 0;
		G.RackBreaker = 1;
		G.IsBreakShot = false;
		const int Count = Game == rules::Discipline::NineBall ? 10 : (Game == rules::Discipline::TenBall ? 11 : 16);
		for (int b = 0; b < rules::kRulesBallCount; ++b)
		{
			G.Balls[b].Kind = b < Count ? rules::BallStatusKind::Pocketed : rules::BallStatusKind::NotUsed;
		}
		Vec2 Taken[rules::kRulesBallCount];
		int N = 0;
		const bool EightLike = Game == rules::Discipline::EightBall || Game == rules::Discipline::Blackball;
		for (int b = 1; b < Count; ++b)
		{
			const bool Must = (Game == rules::Discipline::NineBall && b == 9) || (Game == rules::Discipline::TenBall && b == 10) || (EightLike && b == 8) ||
				(Game == rules::Discipline::StraightPool && b <= 2);
			if (Must || R.NextDouble01() < 0.4)
			{
				const Vec2 P = Spot(R, T, F.C, Taken, N);
				Taken[N++] = P;
				G.Balls[b].Kind = rules::BallStatusKind::OnTable;
				G.Balls[b].Position = P;
			}
		}
		const bool InHand = R.NextDouble01() < 0.3;
		if (InHand)
		{
			// The cue ball in hand: pocketed, or (after a foul that left it on the table) still at its last position.
			G.Balls[0].Kind = R.NextDouble01() < 0.5 ? rules::BallStatusKind::Pocketed : rules::BallStatusKind::OnTable;
			G.Balls[0].Position = Spot(R, T, F.C, Taken, N);
			switch (Game)
			{
			case rules::Discipline::EightBall:
				G.CueBall = R.NextDouble01() < 0.5 ? rules::CueBallNext::InHandAboveHeadString : rules::CueBallNext::InHandAnywhere;
				break;
			case rules::Discipline::StraightPool:
				G.CueBall = rules::CueBallNext::InHandAboveHeadString;
				break;
			case rules::Discipline::Blackball:
				G.CueBall = rules::CueBallNext::InHandBaulk;
				G.Balls[0].Kind = rules::BallStatusKind::Pocketed;
				G.FreeShot = true;
				break;
			case rules::Discipline::NineBall:
			case rules::Discipline::TenBall:
				G.CueBall = rules::CueBallNext::InHandAnywhere;
				break;
			}
		}
		else
		{
			G.Balls[0].Kind = rules::BallStatusKind::OnTable;
			G.Balls[0].Position = Spot(R, T, F.C, Taken, N);
			G.CueBall = rules::CueBallNext::InPosition;
			G.FreeShot = Game == rules::Discipline::Blackball && R.NextDouble01() < 0.3;
		}
		G.PushOutAvailable = (Game == rules::Discipline::NineBall || Game == rules::Discipline::TenBall) && !InHand && R.NextDouble01() < 0.2;
		G.TableOpen = EightLike && R.NextDouble01() < 0.35;
		if (EightLike && !G.TableOpen)
		{
			const bool Solids = R.NextDouble01() < 0.5;
			G.Players[0].Group = Solids ? rules::BallGroup::Solids : rules::BallGroup::Stripes;
			G.Players[1].Group = Solids ? rules::BallGroup::Stripes : rules::BallGroup::Solids;
		}
		for (rules::PlayerState& P : G.Players)
		{
			P.ConsecutiveFouls = static_cast<int>(R.NextBelow(3));
			P.Score = Game == rules::Discipline::StraightPool ? static_cast<int>(R.NextBelow(95)) : 0;
		}
		return F;
	}

	double Share(int Count, int Of) { return Of > 0 ? static_cast<double>(Count) / Of : 0.0; }
}

// A-AI-14: FinishNow after partial stages.
RB_TEST(Integ_ARCH_AI14_FinishNowOnPartialStages)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 53);
	const rules::MatchState S = aitest::ScenarioState(C, 0, {{1, {0.5, 0.2}}, {3, {-0.4, -0.3}}, {6, {0.8, 0.4}}, {9, {1.0, -0.4}}}, {-0.2, 0.1});
	const rules::MatchState Other = aitest::ScenarioState(C, 0, {{2, {0.4, 0.2}}, {4, {-0.3, -0.35}}, {9, {1.0, -0.2}}}, {-0.6, 0.3});
	aitest::Player Me = aitest::MakePlayer(human::AiProfileId::TouringPro, 1, 53);
	const ai::OpponentModel Opp = ai::OpponentModelFor(human::AiProfileId::TouringPro);
	const ai::PlannerInput A = aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, Opp, aitest::KeyFor(53, 1, 1, Me));
	const ai::PlannerInput B = aitest::MakeInput(T, NineFootParams(), C, Other, 0, Me, Opp, aitest::KeyFor(53, 1, 2, Me));
	const Vec2 ToOne = S.Game.Balls[1].Position - S.Game.Balls[0].Position;
	const double OneAzimuth = Atan2(ToOne.y, ToOne.x);
	// Stage 0: FinishNow right after Begin; 1: after every other screening job; 2: after every other noisy job.
	for (int Stage = 0; Stage < 3; ++Stage)
	{
		ai::PlannedDecision Out[2];
		for (int Run = 0; Run < 2; ++Run)
		{
			ai::PlannerScratch Fresh;
			ai::PlannerScratch& P = Run == 0 ? Fresh : Scratch();
			if (Run == 1)
			{
				ai::PlanShot(B, ai::PlannerConfig{}, P); // leaves its results and samples in the reused scratch
			}
			P.Begin(A, ai::PlannerConfig{});
			if (Stage >= 1)
			{
				RunStage(P, Stage == 1 ? 2 : 1);
			}
			if (Stage == 2)
			{
				P.Advance();
				const ai::PlannerProgress Pr = P.Progress();
				RB_CHECK(Pr.Stage == ai::PlannerStage::NoisySamples && Pr.HasBest && Pr.Best.FirstBall == 1);
				RunStage(P, 2);
			}
			P.FinishNow();
			RB_CHECK(P.Finished());
			Out[Run] = P.Decision();
		}
		const ai::PlannedDecision& D = Out[0];
		char Text[400];
		ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
		std::printf("  A-AI-14 FinishNow after %s: %s\n", Stage == 0 ? "Begin" : (Stage == 1 ? "half the screening" : "half the noisy samples"), Text);
		RB_CHECK(D.Error == ErrorCode::Ok && D.Kind == ai::DecisionKind::Stroke && D.TargetBall == 1);
		RB_CHECK(Finite(D.Stroke.Azimuth) && Abs(D.Stroke.Azimuth - OneAzimuth) < 5.0 * kDegToRad);
		RB_CHECK(Stage == 0 ? D.Reasoning.Simulations == 0 : D.Reasoning.Simulations > 0);
		RB_CHECK(SameDecision(Out[0], Out[1]));
	}
}

// A-AI-15: fuzzed positions of every discipline.
RB_TEST(Integ_ARCH_AI15_FuzzedPositionsGiveLegalStrokes)
{
	const TableGeometry& T = NineFoot();
	int Decisions = 0;
	int Errors = 0;
	int Invalid = 0;
	int SimErrors = 0;
	int NonFinite = 0;
	int SpotRequests = 0;
	int Fouls[human::kAiProfileCount] = {};
	int Plays[human::kAiProfileCount] = {};
	for (const rules::Discipline Game : {rules::Discipline::NineBall, rules::Discipline::TenBall, rules::Discipline::EightBall, rules::Discipline::Blackball,
			 rules::Discipline::StraightPool})
	{
		for (int k = 0; k < kFuzzCases; ++k)
		{
			const Fuzz F = MakeFuzz(Game, 0xF022u + 97u * static_cast<std::uint64_t>(k) + 7919u * static_cast<std::uint64_t>(Game));
			for (int p = 0; p < human::kAiProfileCount; ++p)
			{
				aitest::Player Me = aitest::MakePlayer(aitest::kProfiles[p], 1, 11u + static_cast<std::uint64_t>(k));
				const human::NoiseKey Key = aitest::KeyFor(F.C.Seed, 1, static_cast<std::uint32_t>(k), Me);
				ai::PlannerConfig Config;
				Config.Samples = 0.25;
				rules::MatchState S = F.S;
				ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), F.C, S, 0, Me, ai::OpponentModelFor(aitest::kProfiles[p]), Key), Config,
					Scratch());
				if (D.Kind == ai::DecisionKind::RequestSpot)
				{
					// Rules 4.4 (every legal ball behind the head string): the game spots the ball and asks again.
					++SpotRequests;
					const bool Spotted = rules::RequestSpot(F.C, S) == ErrorCode::Ok;
					RB_CHECK(Spotted && S.Game.Balls[D.TargetBall].Kind == rules::BallStatusKind::OnTable &&
						!rules::AboveHeadString(S.Game.Balls[D.TargetBall].Position, F.C.Table, F.C.Rules.Tolerances.Line));
					D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), F.C, S, 0, Me, ai::OpponentModelFor(aitest::kProfiles[p]), Key), Config, Scratch());
				}
				++Decisions;
				if (D.Error != ErrorCode::Ok || D.Kind != ai::DecisionKind::Stroke)
				{
					++Errors;
					std::printf("  A-AI-15 ERROR discipline %d case %d %s: error %d kind %d\n", static_cast<int>(Game), k, aitest::ProfileName(aitest::kProfiles[p]),
						static_cast<int>(D.Error), static_cast<int>(D.Kind));
					continue;
				}
				const bool Ok = Finite(D.Stroke.Azimuth) && Finite(D.Stroke.Elevation) && Finite(D.Stroke.Speed) && Finite(D.Stroke.AxisOffsetA) &&
					Finite(D.Stroke.AxisOffsetB) && D.Stroke.Speed > 0.0 && D.Stroke.Speed <= 12.0 && D.Stroke.Elevation >= 0.0 && D.Stroke.Elevation < 0.5 * kPi &&
					Finite(D.ExpectedValue);
				NonFinite += Ok ? 0 : 1;
				const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), F.C, S, Key);
				if (!X.DeclarationValid)
				{
					++Invalid;
					std::printf("  A-AI-15 INVALID discipline %d case %d %s: %s\n", static_cast<int>(Game), k, aitest::ProfileName(aitest::kProfiles[p]), ai::ShotTypeName(D.Type));
				}
				SimErrors += X.Status == SimStatus::Ok ? 0 : 1;
				Fouls[p] += X.Outcome.AnyFoul ? 1 : 0;
				Plays[p]++;
			}
		}
	}
	std::printf("  A-AI-15 %d fuzzed decisions (%d after a spot request): %d planner errors, %d invalid declarations, %d simulation errors, %d bad strokes; fouls:",
		Decisions, SpotRequests, Errors, Invalid, SimErrors, NonFinite);
	for (int p = 0; p < human::kAiProfileCount; ++p)
	{
		std::printf(" %d/%d", Fouls[p], Plays[p]);
	}
	std::printf("\n");
	RB_CHECK(Errors == 0 && Invalid == 0 && SimErrors == 0 && NonFinite == 0);
}

// A-AI-16: ball in hand behind the head string (8-ball after a break scratch): two solids above the head string, one below.
RB_TEST(Integ_ARCH_AI16_ShootsOutOfTheKitchen)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::EightBall, T, 61);
	const double Hs = C.Table.HeadStringX;
	rules::MatchState K = aitest::ScenarioState(C, 0, {{1, {Hs - 0.3, 0.3}}, {2, {Hs - 0.4, -0.35}}, {3, {0.7, 0.4}}, {8, {0.9, -0.2}}, {9, {0.2, 0.1}}}, {}, true,
		rules::CueBallNext::InHandAboveHeadString);
	K.Game.TableOpen = false;
	K.Game.Players[0].Group = rules::BallGroup::Solids;
	K.Game.Players[1].Group = rules::BallGroup::Stripes;
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		int Direct = 0;
		int Fouls = 0;
		int Made = 0;
		for (int k = 0; k < kTrials; ++k)
		{
			aitest::Player Me = aitest::MakePlayer(Id, 1, 100u + static_cast<std::uint64_t>(k));
			rules::MatchState S = K;
			const human::NoiseKey Key = aitest::KeyFor(0x4B1u + static_cast<std::uint64_t>(k), 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke);
			RB_CHECK(D.PlaceCueBall && rules::AboveHeadString(D.CueBallPlacement, C.Table, C.Rules.Tolerances.Line));
			Direct += D.Type != ai::ShotType::Kick && (D.TargetBall == 1 || D.TargetBall == 2) ? 1 : 0;
			const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), C, S, Key);
			RB_CHECK(X.DeclarationValid);
			Fouls += X.Outcome.AnyFoul ? 1 : 0;
			Made += X.PottedIntended && !X.Outcome.AnyFoul ? 1 : 0;
		}
		std::printf("  A-AI-16 %-18s in hand in the kitchen: direct shots at a ball above the head string %d / %d, fouls %d, pots made %d\n", aitest::ProfileName(Id), Direct,
			kTrials, Fouls, Made);
		RB_CHECK(Direct == 0);
		RB_CHECK(Share(Fouls, kTrials) <= 0.3);
	}
}

// A-AI-17: the two nearest solids each hide behind a wall of two stripes; the 3 is visible (frozen to the foot rail, no pot).
RB_TEST(Integ_ARCH_AI17_HiddenNearestBallsAndNoDeliberateFoul)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::EightBall, T, 62);
	rules::MatchState H = aitest::ScenarioState(C, 0,
		{{1, {0.0, 0.4}}, {9, {kR + 0.0005, 0.2}}, {10, {-kR - 0.0005, 0.2}}, {2, {0.0, -0.4}}, {11, {kR + 0.0005, -0.2}}, {12, {-kR - 0.0005, -0.2}},
			{3, {T.HalfLength - kR - 1e-5, 0.05}}, {8, {-1.0, -0.5}}},
		{0.0, 0.0});
	H.Game.TableOpen = false;
	H.Game.Players[0].Group = rules::BallGroup::Solids;
	H.Game.Players[1].Group = rules::BallGroup::Stripes;
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const ai::PlannerProfile P = ai::GetPlannerProfile(Id);
		int AtThree = 0;
		int Planned = 0;
		int Fouls = 0;
		for (int k = 0; k < kTrials; ++k)
		{
			aitest::Player Me = aitest::MakePlayer(Id, 1, 200u + static_cast<std::uint64_t>(k));
			rules::MatchState S = H;
			const human::NoiseKey Key = aitest::KeyFor(0x4B2u + static_cast<std::uint64_t>(k), 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke);
			AtThree += D.TargetBall == 3 ? 1 : 0;
			Planned += D.FoulChance >= 1.0 ? 1 : 0;
			const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), C, S, Key);
			Fouls += X.Outcome.AnyFoul ? 1 : 0;
		}
		std::printf("  A-AI-17 %-18s hooked on the nearest balls: plays the visible 3 %d / %d, planned sure fouls %d, fouls %d\n", aitest::ProfileName(Id), AtThree, kTrials,
			Planned, Fouls);
		RB_CHECK(Planned == 0);
		if (!P.Safeties)
		{
			RB_CHECK(AtThree == kTrials); // "just hit it": the ball it can see
		}
		if (kRelease)
		{
			RB_CHECK(Share(Fouls, kTrials) <= 0.3);
		}
	}
}

// A-AI-18: 14.1, both players on 50 points, the AI (player 0) on two fouls. A third foul costs 16 points and re-racks with the
// offender breaking (RackSetup); an ordinary foul costs one point and passes the table. The third foul must be valued clearly
// below the ordinary one (a rack model of RackSetup gave the breaker's fresh-rack value, about 0.5, and made a third foul cheap).
RB_TEST(ARCH_AI18_StraightPoolThirdFoulKeepsItsPoints)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::StraightPool, T, 63);
	rules::MatchState Normal = aitest::ScenarioState(C, 1, {{1, {0.5, 0.2}}, {3, {-0.4, -0.3}}, {6, {0.8, 0.4}}, {9, {1.0, -0.4}}, {12, {0.2, -0.45}}}, {-0.2, 0.1});
	Normal.Game.Players[0].Score = 49;
	Normal.Game.Players[1].Score = 50;
	rules::MatchState Third = Normal;
	Third.Phase = rules::MatchPhase::RackSetup;
	Third.Game.RackBreaker = 0;
	Third.Game.Players[0].Score = 50 - 16;
	ai::EvalContext E;
	E.Table = &T;
	E.Match = &C;
	for (const human::AiProfileId Id : {human::AiProfileId::LeaguePlayer, human::AiProfileId::TouringPro})
	{
		const ai::OpponentModel M = ai::OpponentModelFor(Id);
		for (ai::EvalPlayer& P : E.Players)
		{
			P.AimSigma = M.AimSigma;
			P.RunoutRate = M.RunoutRate;
			P.SafetyQuality = M.SafetyQuality;
			P.KickSkill = M.KickSkill;
			P.PlaysSafeties = M.PlaysSafeties;
		}
		const double AfterFoul = ai::StateValue(E, Normal, 0);
		const double AfterThird = ai::StateValue(E, Third, 0);
		std::printf("  A-AI-18 %-18s 14.1 value after a foul %.3f, after the third foul (-16, re-rack) %.3f\n", aitest::ProfileName(Id), AfterFoul, AfterThird);
		RB_CHECK(AfterThird < AfterFoul - 0.05 && AfterThird < 0.25);
	}
}

// A-AI-19: the 14.1 opening break.
RB_TEST(Integ_ARCH_AI19_Slow_StraightPoolSafetyBreak)
{
	const TableGeometry& T = NineFoot();
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const ai::PlannerProfile P = ai::GetPlannerProfile(Id);
		int Soft = 0;
		int Fouls = 0;
		int Passed = 0;
		double Opp = 0.0;
		for (int k = 0; k < kTrials; ++k)
		{
			const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::StraightPool, T, 700u + static_cast<std::uint64_t>(k));
			rules::MatchState S = aitest::StartMatchState(C, 0);
			rules::RackAssignment Rack;
			RB_REQUIRE(rules::SetupRack(C, S, Rack) == ErrorCode::Ok && S.Game.IsBreakShot);
			aitest::Player Me = aitest::MakePlayer(Id, 1, 9u + static_cast<std::uint64_t>(k));
			const human::NoiseKey Key = aitest::KeyFor(C.Seed, 1, 0, Me);
			const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke && D.Type == ai::ShotType::Break);
			Soft += D.Stroke.Speed <= 3.0 ? 1 : 0;
			const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), C, S, Key);
			RB_CHECK(X.DeclarationValid);
			Fouls += X.Outcome.AnyFoul ? 1 : 0;
			if (S.Phase == rules::MatchPhase::AwaitShot && S.Game.Shooter == 1)
			{
				ai::EvalContext E;
				E.Table = &T;
				E.Match = &C;
				Opp += ai::BestNextShot(E, S.Game, 1).PotChance;
				++Passed;
			}
		}
		const double Chance = Passed > 0 ? Opp / Passed : 1.0;
		std::printf("  A-AI-19 %-18s 14.1 opening break: safety breaks %d / %d, breaking fouls %d, the other player's first-shot chance %.2f (over %d)\n",
			aitest::ProfileName(Id), Soft, kTrials, Fouls, Chance, Passed);
		if (P.Safeties)
		{
			RB_CHECK(Soft == kTrials);
			if (kRelease)
			{
				const bool Strong = P.NoisySamples > 0;
				RB_CHECK(Share(Fouls, kTrials) <= (Strong ? 0.2 : 0.4));
				RB_CHECK(Chance <= (Strong ? 0.3 : 0.5));
			}
		}
		else
		{
			RB_CHECK(Soft == 0);
		}
	}
}

// A-AI-20: the budget and the break candidates.
RB_TEST(Integ_ARCH_AI20_BudgetAndBreakCandidates)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 64);
	aitest::Player Pro = aitest::MakePlayer(human::AiProfileId::TouringPro, 1, 64);
	const rules::MatchState Positions[3] = {
		aitest::ScenarioState(C, 0, {{1, {0.5, 0.2}}, {3, {-0.4, -0.3}}, {6, {0.8, 0.4}}, {9, {1.0, -0.4}}}, {-0.2, 0.1}),
		aitest::ScenarioState(C, 0, {{2, {0.4, 0.2}}, {4, {-0.3, -0.35}}, {9, {1.0, -0.2}}}, {}, true),
		aitest::ScenarioState(C, 0, {{1, {T.HalfLength - kR - 0.0005, 0.0}}, {5, {-0.2, -0.4}}, {7, {0.6, 0.45}}, {9, {0.2, 0.4}}}, {-0.9, 0.05}),
	};
	for (const int Budget : {5, 60, 400})
	{
		for (const rules::MatchState& S : Positions)
		{
			ai::PlannerConfig Config;
			Config.SimulationBudget = Budget;
			const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Pro, ai::OpponentModelFor(human::AiProfileId::TouringPro),
														   aitest::KeyFor(64, 1, static_cast<std::uint32_t>(Budget), Pro)),
				Config, Scratch());
			RB_CHECK(D.Kind == ai::DecisionKind::Stroke && D.Reasoning.Simulations <= Budget);
		}
	}
	std::printf("  A-AI-20 the touring pro's decisions stay within simulation budgets of 5, 60 and 400\n");
	// Config scales out of range (a NaN from a settings file) are rejected; zero scales still plan (each count stays >= 1).
	{
		const ai::PlannerInput In = aitest::MakeInput(T, NineFootParams(), C, Positions[0], 0, Pro, ai::OpponentModelFor(human::AiProfileId::TouringPro),
			aitest::KeyFor(64, 1, 7, Pro));
		ai::PlannerConfig Bad;
		Bad.Breadth = std::nan("");
		RB_CHECK(ai::PlanShot(In, Bad, Scratch()).Error == ErrorCode::InvalidArgument);
		Bad = ai::PlannerConfig{};
		Bad.Samples = -1.0;
		RB_CHECK(ai::PlanShot(In, Bad, Scratch()).Error == ErrorCode::InvalidArgument);
		ai::PlannerConfig Zero;
		Zero.Breadth = 0.0;
		Zero.Samples = 0.0;
		const ai::PlannedDecision D = ai::PlanShot(In, Zero, Scratch());
		RB_CHECK(D.Error == ErrorCode::Ok && D.Kind == ai::DecisionKind::Stroke);
	}
	// The 9-ball break of every profile: BreakSpeed or 0.85 of it, both speeds among the best considered breaks.
	rules::MatchState Break = aitest::StartMatchState(C, 0);
	rules::RackAssignment Rack;
	RB_REQUIRE(rules::SetupRack(C, Break, Rack) == ErrorCode::Ok);
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		aitest::Player Me = aitest::MakePlayer(Id, 1, 65);
		const ai::PlannerProfile P = ai::GetPlannerProfile(Id);
		const ai::PlannedDecision D =
			ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, Break, 0, Me, ai::OpponentModelFor(Id), aitest::KeyFor(65, 1, 0, Me)), ai::PlannerConfig{}, Scratch());
		RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke && D.Type == ai::ShotType::Break);
		const double Hard = P.BreakSpeed;
		RB_CHECK(Abs(D.Stroke.Speed - Hard) < 1e-12 || Abs(D.Stroke.Speed - 0.85 * Hard) < 1e-12);
		bool SawHard = false;
		bool SawSoft = false;
		for (const ai::ConsideredShot& Top : D.Reasoning.Top)
		{
			SawHard = SawHard || Abs(Top.Speed - Hard) < 1e-12;
			SawSoft = SawSoft || Abs(Top.Speed - 0.85 * Hard) < 1e-12;
		}
		std::printf("  A-AI-20 %-18s 9-ball break at %.2f m/s (break speed %.2f m/s)\n", aitest::ProfileName(Id), D.Stroke.Speed, P.BreakSpeed);
		RB_CHECK(SawHard && SawSoft);
	}
}

// A-AI-21: the answer to a push-out. Player 0 pushes (a soft roll into open space); the AI (player 1) decides.
RB_TEST(Integ_ARCH_AI21_PushOutResponse)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 66);
	const PocketGeometry& P3 = T.Pockets[3];
	const Vec2 Hanger = P3.MouthMid - P3.Axis * (kR + 0.02);
	// Easy: the 1 hangs in a corner, the cue ball 60 cm behind it. Hidden: the 1 behind a wall of the 2 and the 3.
	rules::MatchState Easy = aitest::ScenarioState(C, 0, {{1, Hanger}, {6, {0.1, -0.45}}, {9, {-0.9, 0.4}}}, Hanger - P3.Axis * 0.6);
	rules::MatchState Hidden = aitest::ScenarioState(C, 0, {{1, {0.9, 0.0}}, {2, {0.6, kR + 0.0005}}, {3, {0.6, -kR - 0.0005}}, {6, {0.1, -0.45}}, {9, {-0.9, 0.4}}},
		{0.1, 0.0});
	for (int Which = 0; Which < 2; ++Which)
	{
		rules::MatchState S = Which == 0 ? Easy : Hidden;
		S.Game.PushOutAvailable = true;
		// The push: 0.5 m/s straight toward the head rail (away from every ball), declared as a push-out.
		ai::PlannedDecision Push;
		Push.Error = ErrorCode::Ok;
		Push.Kind = ai::DecisionKind::Stroke;
		Push.Type = ai::ShotType::PushOut;
		Push.Stroke.Azimuth = kPi;
		Push.Stroke.Elevation = 2.5 * kDegToRad;
		Push.Stroke.Speed = 0.5;
		Push.Declaration.Kind = rules::ShotKind::PushOut;
		Push.Situation.Bridge = human::BridgeType::Closed;
		Push.Situation.BridgeLength = 0.2;
		Push.Situation.BridgeToGrip = 0.8;
		aitest::Player Pusher = aitest::MakePlayer(human::AiProfileId::TouringPro, 1, 66);
		Pusher.Human.NoiseScale = 0.0;
		const aitest::Executed X = aitest::ExecuteDecision(Ref(), Push, Pusher, T, NineFootParams(), C, S, aitest::KeyFor(66, 1, 0, Pusher));
		RB_REQUIRE(X.DeclarationValid && !X.Outcome.AnyFoul && S.Phase == rules::MatchPhase::AwaitDecision && S.Decider == 1);
		for (const human::AiProfileId Id : aitest::kProfiles)
		{
			aitest::Player Me = aitest::MakePlayer(Id, 2, 67);
			const ai::PlannedDecision D =
				ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 1, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(66, 1, 1, Me)),
					ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(D.Kind == ai::DecisionKind::Option);
			const rules::Option Want = Which == 0 ? rules::Option::ShootFromPosition : rules::Option::PassBack;
			if (D.Choice != Want)
			{
				std::printf("  A-AI-21 %-18s after the %s push-out chose option %d (P(win) %.2f)\n", aitest::ProfileName(Id), Which == 0 ? "easy" : "hidden",
					static_cast<int>(D.Choice), D.ExpectedValue);
			}
			RB_CHECK(D.Choice == Want);
		}
	}
	std::printf("  A-AI-21 every profile shoots after an easy push-out and passes back a hidden one\n");
}

// A-AI-22: the spot request (rules 4.4). (a) 14.1 after a scratch: the 5 and the 11 lie behind the head string, the cue ball is in
// hand behind it. (b) 8-ball, solids: the 1 and the 2 lie behind the head string, the stripes and the 8 below it.
RB_TEST(Integ_ARCH_AI22_SpotRequestFromTheKitchen)
{
	const TableGeometry& T = NineFoot();
	for (const rules::Discipline Game : {rules::Discipline::StraightPool, rules::Discipline::EightBall})
	{
		const rules::MatchConfig C = aitest::MakeMatch(Game, T, 67);
		const double Hs = C.Table.HeadStringX;
		const bool Eight = Game == rules::Discipline::EightBall;
		rules::MatchState K = Eight ? aitest::ScenarioState(C, 0, {{1, {Hs - 0.25, 0.3}}, {2, {Hs - 0.5, -0.35}}, {8, {0.9, -0.2}}, {10, {0.6, 0.4}}, {12, {0.2, -0.1}}},
										  {}, true, rules::CueBallNext::InHandAboveHeadString)
									: aitest::ScenarioState(C, 0, {{5, {Hs - 0.25, 0.3}}, {11, {Hs - 0.5, -0.35}}}, {}, true, rules::CueBallNext::InHandAboveHeadString);
		if (Eight)
		{
			K.Game.TableOpen = false;
			K.Game.Players[0].Group = rules::BallGroup::Solids;
			K.Game.Players[1].Group = rules::BallGroup::Stripes;
		}
		const int Nearest = Eight ? 1 : 5;
		RB_REQUIRE(rules::GetShotConstraints(C, K).MayRequestSpot);
		// The static value of the kitchen position is that of the spotted one (both players' view).
		rules::MatchState Spotted = K;
		RB_REQUIRE(rules::RequestSpot(C, Spotted) == ErrorCode::Ok);
		ai::EvalContext E;
		E.Table = &T;
		E.Match = &C;
		RB_CHECK(ai::StateValue(E, K, 0) == ai::StateValue(E, Spotted, 0) && ai::StateValue(E, K, 1) == ai::StateValue(E, Spotted, 1));
		for (const human::AiProfileId Id : aitest::kProfiles)
		{
			int Requests = 0;
			int Fouls = 0;
			int Legal = 0;
			for (int k = 0; k < kTrials; ++k)
			{
				aitest::Player Me = aitest::MakePlayer(Id, 1, 300u + static_cast<std::uint64_t>(k));
				rules::MatchState S = K;
				const human::NoiseKey Key = aitest::KeyFor(0x4B3u + static_cast<std::uint64_t>(k), 1, static_cast<std::uint32_t>(k), Me);
				const ai::PlannedDecision R = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
				if (R.Kind != ai::DecisionKind::RequestSpot || R.Error != ErrorCode::Ok || R.TargetBall != Nearest)
				{
					continue;
				}
				++Requests;
				RB_REQUIRE(rules::RequestSpot(C, S) == ErrorCode::Ok);
				const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
				RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke);
				RB_CHECK(D.PlaceCueBall && rules::CueBallPlacementLegal(S.Game, D.CueBallPlacement, S.Game.CueBall, C.Table, C.Rules.Tolerances));
				const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), C, S, Key);
				RB_CHECK(X.DeclarationValid && X.Status == SimStatus::Ok);
				Fouls += X.Outcome.AnyFoul ? 1 : 0;
				Legal += X.DeclarationValid && !X.Outcome.AnyFoul ? 1 : 0;
				if (k == 0)
				{
					char Text[400];
					ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
					std::printf("  A-AI-22 %-18s after the spot: %s -> %s\n", aitest::ProfileName(Id), Text, X.Outcome.AnyFoul ? X.Outcome.RuleRef : "legal");
				}
			}
			std::printf("  A-AI-22 %-18s discipline %d, every legal ball in the kitchen: spot requests %d / %d, then fouls %d, legal shots %d\n", aitest::ProfileName(Id),
				static_cast<int>(Game), Requests, kTrials, Fouls, Legal);
			RB_CHECK(Requests == kTrials);
			if (kRelease)
			{
				RB_CHECK(Share(Fouls, kTrials) <= 0.3);
			}
		}
	}
}

// A-AI-23: 9-ball, the AI (player 0) on two fouls: the 1 hides behind a wall of the 2 and the 3 frozen together across the line
// halfway (no straight line to it); a one-rail kick off the side rail reaches it. A third foul loses the rack (rules 3.13): nobody
// plans a sure foul, everybody kicks.
RB_TEST(Integ_ARCH_AI23_ThreeFoulRuleKick)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 68);
	const Vec2 Cue{0.0, -0.1};
	const Vec2 One{0.5, 0.35};
	const Vec2 Mid = (Cue + One) * 0.5;
	const Vec2 Across = PerpCcw(Normalized(One - Cue)) * (kR + 0.0005);
	rules::MatchState H = aitest::ScenarioState(C, 0, {{1, One}, {2, Mid + Across}, {3, Mid - Across}, {9, {-0.9, -0.4}}}, Cue);
	H.Game.Players[0].ConsecutiveFouls = 2;
	RB_REQUIRE(rules::GetShotConstraints(C, H).ThreeFoulWarning);
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const ai::PlannerProfile P = ai::GetPlannerProfile(Id);
		int Planned = 0;
		int Kicks = 0;
		int Fouls = 0;
		int Lost = 0;
		for (int k = 0; k < kTrials; ++k)
		{
			aitest::Player Me = aitest::MakePlayer(Id, 1, 400u + static_cast<std::uint64_t>(k));
			rules::MatchState S = H;
			const human::NoiseKey Key = aitest::KeyFor(0x4B4u + static_cast<std::uint64_t>(k), 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke);
			Planned += D.FoulChance >= 1.0 ? 1 : 0;
			Kicks += D.Type == ai::ShotType::Kick ? 1 : 0;
			const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), C, S, Key);
			RB_CHECK(X.DeclarationValid);
			Fouls += X.Outcome.AnyFoul ? 1 : 0;
			Lost += (X.Outcome.Next == rules::NextAction::RackWon || X.Outcome.Next == rules::NextAction::MatchWon) && X.Outcome.Winner == 1 ? 1 : 0;
		}
		std::printf("  A-AI-23 %-18s on two fouls, hooked: kicks %d / %d, planned sure fouls %d, fouls (the rack lost) %d (%d)\n", aitest::ProfileName(Id), Kicks,
			kTrials, Planned, Fouls, Lost);
		RB_CHECK(Planned == 0);
		RB_CHECK(Kicks == kTrials);
		RB_CHECK(Lost == Fouls);
		if (kRelease)
		{
			RB_CHECK(!P.Kicks || Share(Fouls, kTrials) <= 0.4);
		}
	}
}

// A-AI-25: the 8-ball last-pocket variant of the bar house rules (BarHouse8Ball, rules 12.6): the AI is on the 8 and its last solid
// dropped in pocket X; the easiest pot of the 8 goes into another pocket Y, which would lose the rack. No profile plans or calls
// the 8 anywhere but X, the static evaluator's best next shot uses X, and no rack is lost by the rule.
RB_TEST(Integ_ARCH_AI25_LastPocketRule)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatchWith(rules::RulesPreset::BarHouse8Ball, T, 70);
	RB_REQUIRE(C.Rules.LastPocketRule);
	// The 8 in the foot half, off the long string: the side pocket and the foot corner on the far long rail both take it with a
	// medium cut from a cue ball between it and the near rail. The cue ball is chosen on a small grid (by the tourist's aim
	// spread) so that the second most makeable pocket X stays makeable and the most makeable one Y is clearly easier.
	const Vec2 Eight{0.6, -0.3};
	const double Sigma = ai::GetPlannerProfile(human::AiProfileId::Tourist).PerceivedAimSigma;
	Vec2 Cue;
	int Y = -1;
	int X = -1;
	double ChanceY = 0.0;
	double ChanceX = 0.0;
	for (const double Cx : {0.45, 0.55, 0.65, 0.75})
	{
		for (const double Cy : {-0.5, -0.56})
		{
			const Vec2 Try{Cx, Cy};
			int Best = -1;
			int Next = -1;
			double ChanceBest = 0.0;
			double ChanceNext = 0.0;
			for (int p = 0; p < T.Pockets.Size(); ++p)
			{
				const ai::PocketAim Aim = ai::PocketAimFor(T, p, Eight, kR);
				const double Chance =
					Aim.Valid ? ai::PotChance(ai::CutGeometry(Try, kR, Eight, kR, Aim.Point, 80.0 * kDegToRad), Aim.HalfWindow, 2.0 * kR, Sigma) : 0.0;
				if (Chance > ChanceBest)
				{
					Next = Best;
					ChanceNext = ChanceBest;
					Best = p;
					ChanceBest = Chance;
				}
				else if (Chance > ChanceNext)
				{
					Next = p;
					ChanceNext = Chance;
				}
			}
			// Keep the cue ball with the largest lead of the easiest pocket while the second one stays makeable.
			if (Next >= 0 && ChanceNext >= 0.15 && (X < 0 || ChanceBest - ChanceNext > ChanceY - ChanceX))
			{
				Cue = Try;
				Y = Best;
				X = Next;
				ChanceY = ChanceBest;
				ChanceX = ChanceNext;
			}
		}
	}
	RB_REQUIRE(Y >= 0 && X >= 0 && ChanceX >= 0.15 && ChanceY >= ChanceX + 0.1);
	rules::MatchState H = aitest::ScenarioState(C, 0, {{8, Eight}, {10, {-0.9, 0.4}}, {13, {-1.0, -0.45}}}, Cue);
	H.Game.TableOpen = false;
	H.Game.Players[0].Group = rules::BallGroup::Solids;
	H.Game.Players[1].Group = rules::BallGroup::Stripes;
	H.Game.LastGroupBallPocket[static_cast<int>(rules::BallGroup::Solids)] = static_cast<PocketId>(X);
	ai::EvalContext E;
	E.Table = &T;
	E.Match = &C;
	const ai::NextShotInfo Next = ai::BestNextShot(E, H.Game, 0);
	std::printf("  A-AI-25 the 8: easiest pocket %d (%.2f), the last solid's pocket %d (%.2f); the evaluator's best next shot: ball %d pocket %d (%.2f)\n", Y, ChanceY,
		X, ChanceX, static_cast<int>(Next.Ball), static_cast<int>(Next.Pocket), Next.PotChance);
	RB_CHECK(Next.Ball == 8 && Next.Pocket == static_cast<PocketId>(X));
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		int Wrong = 0;
		int AtX = 0;
		int LostByRule = 0;
		int Won = 0;
		for (int k = 0; k < kTrials; ++k)
		{
			aitest::Player Me = aitest::MakePlayer(Id, 1, 500u + static_cast<std::uint64_t>(k));
			rules::MatchState S = H;
			const human::NoiseKey Key = aitest::KeyFor(0x4B5u + static_cast<std::uint64_t>(k), 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke);
			const bool PotsEight = D.PotBall == 8 || D.Declaration.Called.Ball == 8;
			Wrong += PotsEight && (D.Pocket != static_cast<PocketId>(X) || D.Declaration.Called.Pocket != static_cast<PocketId>(X)) ? 1 : 0;
			AtX += PotsEight && D.Pocket == static_cast<PocketId>(X) ? 1 : 0;
			const aitest::Executed Out = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), C, S, Key);
			RB_CHECK(Out.DeclarationValid);
			LostByRule += std::strcmp(Out.Outcome.RuleRef, "variant: last-pocket rule") == 0 ? 1 : 0;
			Won += (Out.Outcome.Next == rules::NextAction::RackWon || Out.Outcome.Next == rules::NextAction::MatchWon) && Out.Outcome.Winner == 0 ? 1 : 0;
		}
		std::printf("  A-AI-25 %-18s on the 8 (last-pocket rule): the 8 at pocket %d %d / %d, elsewhere %d, racks won %d, lost by the rule %d\n", aitest::ProfileName(Id),
			X, AtX, kTrials, Wrong, Won, LostByRule);
		RB_CHECK(Wrong == 0);
		RB_CHECK(LostByRule <= 1);
	}
}

// A-AI-24: two AI players decide at the same time on two threads (each with its own scratch; the Unreal side may plan the next
// shot of an AI-vs-AI match or a hint while another decision runs): bitwise the same decisions as one after the other.
RB_TEST(Integ_ARCH_AI24_ConcurrentPlannersAreIndependent)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 69);
	const rules::MatchState Positions[3] = {
		aitest::ScenarioState(C, 0, {{1, {0.5, 0.2}}, {3, {-0.4, -0.3}}, {6, {0.8, 0.4}}, {9, {1.0, -0.4}}}, {-0.2, 0.1}),
		aitest::ScenarioState(C, 0, {{2, {0.4, 0.2}}, {4, {-0.3, -0.35}}, {9, {1.0, -0.2}}}, {}, true),
		aitest::ScenarioState(C, 0, {{1, {T.HalfLength - kR - 0.0005, 0.0}}, {5, {-0.2, -0.4}}, {7, {0.6, 0.45}}, {9, {0.2, 0.4}}}, {-0.9, 0.05}),
	};
	const human::AiProfileId Ids[2] = {human::AiProfileId::TouringPro, human::AiProfileId::LocalHustler};
	constexpr int kRuns = 3;
	std::vector<ai::PlannerInput> Inputs[2];
	for (int t = 0; t < 2; ++t)
	{
		aitest::Player Me = aitest::MakePlayer(Ids[t], 1 + static_cast<std::uint32_t>(t), 69u + static_cast<std::uint64_t>(t));
		for (int r = 0; r < kRuns; ++r)
		{
			for (int p = 0; p < 3; ++p)
			{
				Inputs[t].push_back(aitest::MakeInput(T, NineFootParams(), C, Positions[p], 0, Me, ai::OpponentModelFor(Ids[1 - t]),
					aitest::KeyFor(69, 1, static_cast<std::uint32_t>(3 * r + p), Me)));
			}
		}
	}
	ai::PlannerConfig Config;
	Config.Samples = 0.5;
	std::vector<ai::PlannedDecision> Serial[2];
	for (int t = 0; t < 2; ++t)
	{
		for (const ai::PlannerInput& In : Inputs[t])
		{
			Serial[t].push_back(ai::PlanShot(In, Config, Scratch()));
		}
	}
	std::vector<ai::PlannedDecision> Concurrent[2];
	std::vector<std::thread> Pool;
	for (int t = 0; t < 2; ++t)
	{
		Pool.emplace_back([&, t]() {
			auto Own = std::make_unique<ai::PlannerScratch>();
			for (const ai::PlannerInput& In : Inputs[t])
			{
				Concurrent[t].push_back(ai::PlanShot(In, Config, *Own));
			}
		});
	}
	for (std::thread& Th : Pool)
	{
		Th.join();
	}
	int Same = 0;
	for (int t = 0; t < 2; ++t)
	{
		for (std::size_t i = 0; i < Serial[t].size(); ++i)
		{
			Same += SameDecision(Serial[t][i], Concurrent[t][i]) ? 1 : 0;
		}
	}
	std::printf("  A-AI-24 two planners on two threads: %d of %d decisions bitwise the same as serial\n", Same, 2 * 3 * kRuns);
	RB_CHECK(Same == 2 * 3 * kRuns);
}

// A-AI-26: 14.1, both players one point from the match (TargetPoints - 1). A hanging ball in a corner (a 25 deg cut, so that
// even the tourist's rolling centre-ball hit does not follow it in) is the match ball: its pot wins the match (R 7.4), so the
// noise-free win is planned, not luck, and must not be capped at the value of keeping the table (the planner valued it like a
// kept table, about 0.6 for the tourist). Every profile pots a ball (any pot wins), expects to win and plays it with the game-ball
// pressure.
RB_TEST(Integ_ARCH_AI26_StraightPoolMatchBall)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::StraightPool, T, 71);
	const PocketGeometry& P3 = T.Pockets[3];
	const Vec2 Hanger = P3.MouthMid - P3.Axis * (kR + 0.02);
	const double Cut = 25.0 * kDegToRad;
	const Vec2 Line{P3.Axis.x * Cos(Cut) - P3.Axis.y * Sin(Cut), P3.Axis.x * Sin(Cut) + P3.Axis.y * Cos(Cut)};
	rules::MatchState Base = aitest::ScenarioState(C, 0, {{7, Hanger}, {2, {-0.9, 0.4}}, {11, {-1.0, -0.45}}, {13, {0.2, -0.45}}}, Hanger - Line * 0.5);
	rules::MatchState Match = Base;
	Match.Game.Players[0].Score = C.Rules.TargetPoints - 1;
	Match.Game.Players[1].Score = C.Rules.TargetPoints - 1;
	Base.Game.Players[0].Score = C.Rules.TargetPoints / 2;
	Base.Game.Players[1].Score = C.Rules.TargetPoints / 2;
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		int Pots = 0;
		int Won = 0;
		int Pressured = 0;
		double MinValue = 1.0;
		for (int k = 0; k < kTrials; ++k)
		{
			aitest::Player Me = aitest::MakePlayer(Id, 1, 600u + static_cast<std::uint64_t>(k));
			const human::NoiseKey Key = aitest::KeyFor(0x4B6u + static_cast<std::uint64_t>(k), 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannedDecision Mid =
				ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, Base, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
			rules::MatchState S = Match;
			const ai::PlannedDecision D = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(Id), Key), ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(D.Kind == ai::DecisionKind::Stroke && Mid.Kind == ai::DecisionKind::Stroke);
			if (k == 0)
			{
				char Text[400];
				ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
				std::printf("  A-AI-26 %-18s %s\n", aitest::ProfileName(Id), Text);
			}
			Pots += ai::IsPotShot(D.Type) ? 1 : 0;
			MinValue = Min(MinValue, D.ExpectedValue);
			Pressured += D.Situation.Pressure > Mid.Situation.Pressure ? 1 : 0;
			const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, T, NineFootParams(), C, S, Key);
			RB_CHECK(X.DeclarationValid);
			Won += X.Outcome.Next == rules::NextAction::MatchWon && X.Outcome.Winner == 0 ? 1 : 0;
		}
		std::printf("  A-AI-26 %-18s 14.1 match ball (both on %d): pots %d / %d, expected P(win) >= %.3f, game-ball pressure %d / %d, matches won %d\n",
			aitest::ProfileName(Id), C.Rules.TargetPoints - 1, Pots, kTrials, MinValue, Pressured, kTrials, Won);
		RB_CHECK(Pots == kTrials);
		RB_CHECK(MinValue >= 0.9);
		RB_CHECK(Pressured == kTrials);
		RB_CHECK(Share(Won, kTrials) >= 0.8);
	}
}

namespace
{
	// The decision the game asks for next, for the player the rules name (a shot, an option, a spot request or the next break);
	// true when the planner answers it without an error.
	bool PlansNext(const TableGeometry& T, const rules::MatchConfig& C, rules::MatchState S, human::AiProfileId Id, std::uint32_t Shot)
	{
		if (S.Phase == rules::MatchPhase::RackSetup)
		{
			rules::RackAssignment Rack;
			if (rules::SetupRack(C, S, Rack) != ErrorCode::Ok)
			{
				return false;
			}
		}
		if (S.Phase == rules::MatchPhase::RackOver || S.Phase == rules::MatchPhase::MatchOver)
		{
			return true;
		}
		const int Self = S.Phase == rules::MatchPhase::AwaitDecision ? S.Decider : S.Game.Shooter;
		if (Self < 0 || Self > 1 || (S.Phase != rules::MatchPhase::AwaitShot && S.Phase != rules::MatchPhase::AwaitDecision))
		{
			return false;
		}
		aitest::Player Me = aitest::MakePlayer(Id, 1 + static_cast<std::uint32_t>(Self), 700u + Shot);
		ai::PlannerConfig Config;
		Config.Samples = 0.25;
		const ai::PlannedDecision D =
			ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, Self, Me, ai::OpponentModelFor(Id), aitest::KeyFor(C.Seed, 1, Shot, Me)), Config, Scratch());
		if (D.Error != ErrorCode::Ok)
		{
			return false;
		}
		rules::MatchState Copy = S;
		switch (D.Kind)
		{
		case ai::DecisionKind::Stroke:
			return rules::ValidateDeclaration(C, S, D.Declaration, D.PlaceCueBall ? &D.CueBallPlacement : nullptr) == ErrorCode::Ok;
		case ai::DecisionKind::Option:
			return rules::ApplyOption(C, Copy, D.Choice) == ErrorCode::Ok;
		case ai::DecisionKind::RequestSpot:
			return rules::RequestSpot(C, Copy) == ErrorCode::Ok;
		case ai::DecisionKind::ChooseBreaker:
		case ai::DecisionKind::None:
			break;
		}
		return false;
	}
}

// A-AI-27: the options of every rule set, reached by real soft breaks (illegal breaks, 14.1 breaking fouls, the three-ball rule of
// the legacy 9-ball rules, 10-ball wrongful pockets, ...): every profile picks an offered option that the rules accept, and the
// decision the game asks for after it (the shot from position, the re-break, a further option) is planned without an error.
RB_TEST(Integ_ARCH_AI27_OptionsAfterSoftBreaks)
{
	const TableGeometry& T = NineFoot();
	const rules::RulesPreset Presets[] = {rules::RulesPreset::Wpa8Ball, rules::RulesPreset::Wpa9Ball, rules::RulesPreset::Wpa10Ball, rules::RulesPreset::Wpa14_1,
		rules::RulesPreset::WpaBlackball, rules::RulesPreset::WpaLegacy9Ball, rules::RulesPreset::Apa8Ball, rules::RulesPreset::BarHouse8Ball};
	int Situations = 0;
	int Decisions = 0;
	int Bad = 0;
	int BadNext = 0;
	int Offers[16] = {};
	for (const rules::RulesPreset Preset : Presets)
	{
		for (const double Speed : {0.9, 1.6, 2.6, 4.0})
		{
			const rules::MatchConfig C =
				aitest::MakeMatchWith(Preset, T, 800u + 16u * static_cast<std::uint64_t>(Preset) + static_cast<std::uint64_t>(Speed * 10.0));
			rules::MatchState S = aitest::StartMatchState(C, 0);
			rules::RackAssignment Rack;
			RB_REQUIRE(rules::SetupRack(C, S, Rack) == ErrorCode::Ok && S.Game.IsBreakShot);
			// The tourist's break plan (placement, aim at the rack), played soft and without noise.
			aitest::Player Breaker = aitest::MakePlayer(human::AiProfileId::Tourist, 1, 800);
			Breaker.Human.NoiseScale = 0.0;
			const human::NoiseKey Key = aitest::KeyFor(C.Seed, 1, 0, Breaker);
			ai::PlannedDecision Break = ai::PlanShot(
				aitest::MakeInput(T, NineFootParams(), C, S, 0, Breaker, ai::OpponentModelFor(human::AiProfileId::Tourist), Key), ai::PlannerConfig{}, Scratch());
			RB_REQUIRE(Break.Kind == ai::DecisionKind::Stroke && Break.Type == ai::ShotType::Break);
			Break.Stroke.Speed = Speed;
			const aitest::Executed X = aitest::ExecuteDecision(Ref(), Break, Breaker, T, NineFootParams(), C, S, Key);
			RB_REQUIRE(X.DeclarationValid && X.Status == SimStatus::Ok);
			if (S.Phase != rules::MatchPhase::AwaitDecision)
			{
				continue;
			}
			++Situations;
			for (const rules::Option O : S.PendingOutcome.Options)
			{
				Offers[static_cast<int>(O) & 15]++;
			}
			std::printf("  A-AI-27 preset %d, break %.1f m/s (%s): options", static_cast<int>(Preset), Speed, S.PendingOutcome.RuleRef);
			for (const rules::Option O : S.PendingOutcome.Options)
			{
				std::printf(" %d", static_cast<int>(O));
			}
			std::printf("; chosen by the six profiles:");
			for (const human::AiProfileId Id : aitest::kProfiles)
			{
				aitest::Player Me = aitest::MakePlayer(Id, 2, 801);
				const ai::PlannedDecision D = ai::PlanShot(
					aitest::MakeInput(T, NineFootParams(), C, S, S.Decider, Me, ai::OpponentModelFor(human::AiProfileId::Tourist), aitest::KeyFor(C.Seed, 1, 1, Me)),
					ai::PlannerConfig{}, Scratch());
				std::printf(" %d (%.2f)", static_cast<int>(D.Choice), D.ExpectedValue);
			}
			std::printf("\n");
			for (const human::AiProfileId Id : aitest::kProfiles)
			{
				aitest::Player Me = aitest::MakePlayer(Id, 2, 801);
				const ai::PlannedDecision D = ai::PlanShot(
					aitest::MakeInput(T, NineFootParams(), C, S, S.Decider, Me, ai::OpponentModelFor(human::AiProfileId::Tourist), aitest::KeyFor(C.Seed, 1, 1, Me)),
					ai::PlannerConfig{}, Scratch());
				++Decisions;
				bool Offered = false;
				for (const rules::Option O : S.PendingOutcome.Options)
				{
					Offered = Offered || O == D.Choice;
				}
				rules::MatchState After = S;
				const bool Ok = D.Error == ErrorCode::Ok && D.Kind == ai::DecisionKind::Option && Offered && rules::ApplyOption(C, After, D.Choice) == ErrorCode::Ok &&
					Finite(D.ExpectedValue) && D.ExpectedValue >= 0.0 && D.ExpectedValue <= 1.0;
				if (!Ok)
				{
					++Bad;
					std::printf("  A-AI-27 preset %d, break %.1f m/s, %s: error %d, kind %d, option %d\n", static_cast<int>(Preset), Speed, aitest::ProfileName(Id),
						static_cast<int>(D.Error), static_cast<int>(D.Kind), static_cast<int>(D.Choice));
					continue;
				}
				if (!PlansNext(T, C, After, Id, 2))
				{
					++BadNext;
					std::printf("  A-AI-27 preset %d, break %.1f m/s, %s: no plan after option %d (phase %d)\n", static_cast<int>(Preset), Speed, aitest::ProfileName(Id),
						static_cast<int>(D.Choice), static_cast<int>(After.Phase));
				}
			}
		}
	}
	std::printf("  A-AI-27 %d option situations after soft breaks (%d decisions): %d invalid choices, %d unplannable continuations; options offered:", Situations,
		Decisions, Bad, BadNext);
	for (int k = 0; k < 16; ++k)
	{
		if (Offers[k] > 0)
		{
			std::printf(" %d x%d", k, Offers[k]);
		}
	}
	std::printf("\n");
	RB_CHECK(Situations >= 4);
	RB_CHECK(Bad == 0 && BadNext == 0);
}
