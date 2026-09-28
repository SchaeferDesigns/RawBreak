// Owner: WP-12 (AI opponent). The planner's static evaluator and profiles without the simulator (Docs/architecture.md 7.6):
//   A-AI-8  pocket windows, ghost-ball geometry, make chance monotone in distance / cut / sigma, obstruction, next-shot quality,
//           hooked positions and the three-foul rule in the value, ball in hand, option decisions, ball counts per discipline, the
//           erf funnel; the profile table (breadth and samples grow with skill, knowledge from human-factors 5.5, sandbagging,
//           opponent models) and the measured cue-ball direction spread of every profile's hand + human layer against the
//           planner's perceived aim sigma.

#include "Ai/AiTestUtil.h"

#include "rb/Physics/CueStrike.h"

#include <cmath>

using namespace rb;
using aitest::kR;

namespace
{
	const TableGeometry& NineFoot() { return simtest::Table(kTableNineFootPro); }

	ai::EvalContext Context(const rules::MatchConfig& C, human::AiProfileId Me, human::AiProfileId Other)
	{
		ai::EvalContext X;
		X.Table = &NineFoot();
		X.Match = &C;
		const ai::OpponentModel A = ai::OpponentModelFor(Me);
		const ai::OpponentModel B = ai::OpponentModelFor(Other);
		X.Players[0] = ai::EvalPlayer{A.AimSigma, A.RunoutRate, A.SafetyQuality, A.KickSkill, A.PlaysSafeties, 2};
		X.Players[1] = ai::EvalPlayer{B.AimSigma, B.RunoutRate, B.SafetyQuality, B.KickSkill, B.PlaysSafeties, 1};
		return X;
	}
}

RB_TEST(ARCH_AI8_ErfAndPocketWindows)
{
	for (double X = -3.0; X <= 3.0; X += 0.125)
	{
		RB_CHECK_NEAR(ai::Erf(X), std::erf(X), 2e-7);
	}
	const TableGeometry& T = NineFoot();
	for (int p = 0; p < T.Pockets.Size(); ++p)
	{
		const PocketGeometry& P = T.Pockets[p];
		// Straight in along the axis: the full window; beyond the limit: none.
		const ai::PocketAim Straight = ai::PocketAimFor(T, p, P.MouthMid - P.Axis * 0.5, kR);
		RB_CHECK(Straight.Valid && Straight.Approach < 1e-9);
		RB_CHECK_NEAR(Straight.HalfWindow, Max(0.004, 0.5 * P.Mouth - kR), 1e-12);
		const Vec2 Side = PerpCcw(P.Axis);
		const ai::PocketAim Steep = ai::PocketAimFor(T, p, P.MouthMid - P.Axis * 0.05 + Side * 0.6, kR);
		RB_CHECK(!Steep.Valid);
		// The window shrinks as the approach steepens.
		double Last = kInfinity;
		for (double Angle = 0.0; Angle <= 80.0; Angle += 10.0)
		{
			const double A = Angle * kDegToRad;
			const Vec2 From = P.MouthMid - (P.Axis * Cos(A) + Side * Sin(A)) * 0.5;
			const double W = ai::PocketAimFor(T, p, From, kR).HalfWindow;
			RB_CHECK(W <= Last + 1e-12);
			Last = W;
		}
	}
}

RB_TEST(ARCH_AI8_PotChanceMonotoneAndObstruction)
{
	const Vec2 Target{1.2, 0.0};
	const auto Chance = [&](double D1, double CutDeg, double Sigma) {
		const Vec2 Object{0.6, 0.0};
		const Vec2 U{1.0, 0.0};
		const Vec2 Ghost = Object - U * (2.0 * kR);
		const double A = CutDeg * kDegToRad;
		const Vec2 Cue = Ghost - Vec2{Cos(A), Sin(A)} * D1;
		const ai::ShotGeometry S = ai::CutGeometry(Cue, kR, Object, kR, Target, 85.0 * kDegToRad);
		RB_CHECK(S.Feasible);
		RB_CHECK_NEAR(Abs(S.Cut), A, 1e-9);
		RB_CHECK_NEAR(S.CueDistance, D1, 1e-9);
		return ai::PotChance(S, 0.028, 2.0 * kR, Sigma);
	};
	for (const double Sigma : {0.0005, 0.002, 0.007})
	{
		RB_CHECK(Chance(0.3, 10.0, Sigma) >= Chance(0.8, 10.0, Sigma));
		RB_CHECK(Chance(0.8, 10.0, Sigma) >= Chance(1.5, 10.0, Sigma));
		RB_CHECK(Chance(0.8, 0.0, Sigma) >= Chance(0.8, 40.0, Sigma));
		RB_CHECK(Chance(0.8, 40.0, Sigma) >= Chance(0.8, 75.0, Sigma));
	}
	RB_CHECK(Chance(1.0, 30.0, 0.0005) > Chance(1.0, 30.0, 0.002));
	RB_CHECK(Chance(1.0, 30.0, 0.002) > Chance(1.0, 30.0, 0.007));
	// Beyond the cut limit: infeasible.
	const ai::ShotGeometry Thin = ai::CutGeometry({0.6, 0.5}, kR, {0.6, 0.0}, kR, {1.2, 0.0}, 80.0 * kDegToRad);
	RB_CHECK(!Thin.Feasible);

	// Obstruction: a ball on the line blocks, one beside it does not.
	ai::BallLayout L;
	L.OnTable = 1u << 5;
	L.Position[5] = {0.3, 0.0};
	L.Radius[5] = kR;
	RB_CHECK(!ai::PathClear(L, {0.0, 0.0}, {0.6, 0.0}, kR, 0u));
	RB_CHECK(ai::PathClear(L, {0.0, 0.0}, {0.6, 0.0}, kR, 1u << 5));
	L.Position[5] = {0.3, 2.0 * kR + 0.002};
	RB_CHECK(ai::PathClear(L, {0.0, 0.0}, {0.6, 0.0}, kR, 0u));
	L.Position[5] = {0.3, 2.0 * kR - 0.002};
	RB_CHECK(!ai::PathClear(L, {0.0, 0.0}, {0.6, 0.0}, kR, 0u));
}

RB_TEST(ARCH_AI8_NextShotAndValues)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 7);
	ai::EvalContext X = Context(C, human::AiProfileId::LeaguePlayer, human::AiProfileId::LeaguePlayer);
	X.Players[0].PositionDepth = 1; // the next shot only (the two-shot pattern would compare the balls after it as well)
	const PocketGeometry& P3 = T.Pockets[3];
	const Vec2 Hanger = P3.MouthMid - P3.Axis * (kR + 0.02);
	// A hanger with the cue ball 0.5 m behind it is better than the same ball from 2 m at a 60 deg cut.
	const rules::MatchState Easy = aitest::ScenarioState(C, 0, {{1, Hanger}, {9, {-0.5, 0.3}}}, Hanger - P3.Axis * 0.5);
	const Vec2 Far = Hanger - Vec2{2.0, -0.3};
	const rules::MatchState Hard = aitest::ScenarioState(C, 0, {{1, {0.3, -0.2}}, {9, {-0.5, 0.3}}}, Far);
	const ai::NextShotInfo E = ai::BestNextShot(X, Easy.Game, 0);
	const ai::NextShotInfo H = ai::BestNextShot(X, Hard.Game, 0);
	std::printf("  A-AI-8 next shot: hanger %.3f (ball %d pocket %d), long cut %.3f; visibility %.2f / %.2f\n", E.PotChance, E.Ball, static_cast<int>(E.Pocket),
		H.PotChance, E.Visibility, H.Visibility);
	RB_CHECK(E.Ball == 1 && E.Pocket == PocketId::FootLeft && E.PotChance > 0.95);
	RB_CHECK(E.PotChance > H.PotChance);
	const double VEasy = ai::MoverWinProbability(X, Easy.Game);
	const double VHard = ai::MoverWinProbability(X, Hard.Game);
	RB_CHECK(VEasy > VHard);
	RB_CHECK(ai::StateValue(X, Easy, 0) == VEasy && ai::StateValue(X, Easy, 1) == 1.0 - VEasy);

	// Hooked: the 1 behind the 2 and the 3 (no straight line to any part of it).
	const Vec2 One{0.9, 0.0};
	const rules::MatchState Hooked = aitest::ScenarioState(C, 0, {{1, One}, {2, {0.9 - 2.0 * kR - 0.003, 2.0 * kR}}, {3, {0.9 - 2.0 * kR - 0.003, -2.0 * kR}},
		{4, {0.9 - 4.0 * kR - 0.006, 0.0}}, {9, {-0.9, 0.4}}}, {-0.5, 0.0});
	const ai::NextShotInfo K = ai::BestNextShot(X, Hooked.Game, 0);
	std::printf("  A-AI-8 hooked: visibility %.2f, pot %.3f\n", K.Visibility, K.PotChance);
	RB_CHECK(K.Visibility == 0.0 && K.PotChance == 0.0);
	// Three-foul rule (9-ball): hooked on two fouls is worse than hooked on none.
	rules::MatchState TwoFouls = Hooked;
	TwoFouls.Game.Players[0].ConsecutiveFouls = 2;
	const double V0 = ai::MoverWinProbability(X, Hooked.Game);
	const double V2 = ai::MoverWinProbability(X, TwoFouls.Game);
	std::printf("  A-AI-8 hooked value: %.3f with no foul, %.3f on two fouls\n", V0, V2);
	RB_CHECK(V2 < V0);

	// Ball in hand beats the cue ball where it lies on the hard table.
	rules::MatchState InHand = Hard;
	InHand.Game.Balls[0].Kind = rules::BallStatusKind::Pocketed;
	InHand.Game.CueBall = rules::CueBallNext::InHandAnywhere;
	const ai::NextShotInfo B = ai::BestNextShot(X, InHand.Game, 0);
	RB_CHECK(B.InHand && B.PotChance > H.PotChance && B.PotChance > 0.9);
	RB_CHECK(rules::CueBallPlacementLegal(InHand.Game, B.CuePosition, rules::CueBallNext::InHandAnywhere, C.Table, C.Rules.Tolerances));
	RB_CHECK(ai::MoverWinProbability(X, InHand.Game) > VHard);

	// An option decision: the decider takes the side the evaluator prefers (push-out response).
	rules::MatchState Push = Hard;
	Push.Phase = rules::MatchPhase::AwaitDecision;
	Push.Decider = 1;
	Push.PendingOutcome.Options.PushBack(rules::Option::ShootFromPosition);
	Push.PendingOutcome.Options.PushBack(rules::Option::PassBack);
	const double Shoot = [&] {
		rules::MatchState S = Push;
		rules::ApplyOption(C, S, rules::Option::ShootFromPosition);
		return ai::StateValue(X, S, 1);
	}();
	const double Pass = [&] {
		rules::MatchState S = Push;
		rules::ApplyOption(C, S, rules::Option::PassBack);
		return ai::StateValue(X, S, 1);
	}();
	RB_CHECK_NEAR(ai::StateValue(X, Push, 1), Max(Shoot, Pass), 1e-15);
}

// 14.1 is a points race: the value grows with the lead, a foul's point always costs value (leading or trailing by far, never
// saturated), the third foul (16 points) costs more, and the mover's value stays inside (0, 1) at a 60-point lead.
RB_TEST(ARCH_AI8_StraightPoolPointsRace)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::StraightPool, T, 3);
	const ai::EvalContext X = Context(C, human::AiProfileId::RoadPlayer, human::AiProfileId::LeaguePlayer);
	rules::MatchState S = aitest::ScenarioState(C, 0, {{1, {0.3, 0.2}}, {2, {0.6, -0.3}}, {3, {-0.4, 0.35}}, {4, {0.9, 0.1}}}, {-0.2, 0.0});
	const auto Value = [&](int Mine, int Theirs, int Fouls) {
		rules::MatchState V = S;
		V.Game.Players[0].Score = Mine;
		V.Game.Players[1].Score = Theirs;
		V.Game.Players[0].ConsecutiveFouls = Fouls;
		return ai::MoverWinProbability(X, V.Game);
	};
	double Last = -1.0;
	for (int Lead = -60; Lead <= 60; Lead += 5)
	{
		const double V = Value(40 + (Lead > 0 ? Lead : 0), 40 + (Lead < 0 ? -Lead : 0), 0);
		RB_CHECK(V > Last && V > 0.0 && V < 1.0);
		Last = V;
	}
	for (const int Lead : {-40, -10, 0, 10, 40})
	{
		const int Mine = 50 + (Lead > 0 ? Lead : 0);
		const int Theirs = 50 + (Lead < 0 ? -Lead : 0);
		const double Clean = Value(Mine, Theirs, 0);
		const double OnePoint = Value(Mine - 1, Theirs, 0);
		const double Sixteen = Value(Mine - 16, Theirs, 0);
		std::printf("  A-AI-8 14.1 lead %+3d: P(win) %.4f, a point less %.4f, 16 points less %.4f\n", Lead, Clean, OnePoint, Sixteen);
		RB_CHECK(OnePoint < Clean && Sixteen < OnePoint);
	}
	// Near the target the real counts decide: 1 point to go with the ball on is nearly won.
	RB_CHECK(Value(99, 60, 0) > Value(90, 60, 0));
}

RB_TEST(ARCH_AI8_BallsToWin)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C9 = aitest::MakeMatch(rules::Discipline::NineBall, T, 1);
	const rules::MatchState S9 = aitest::ScenarioState(C9, 0, {{3, {0.1, 0.1}}, {5, {0.2, 0.1}}, {9, {0.3, 0.1}}}, {-0.5, 0.0});
	RB_CHECK(ai::BallsToWin(S9.Game, 0, C9.Rules) == 3 && ai::BallsToWin(S9.Game, 1, C9.Rules) == 3);
	const rules::MatchConfig C8 = aitest::MakeMatch(rules::Discipline::EightBall, T, 1);
	rules::MatchState S8 = aitest::ScenarioState(C8, 0, {{1, {0.1, 0.1}}, {2, {0.2, 0.1}}, {8, {0.3, 0.1}}, {9, {0.4, 0.1}}, {10, {0.5, 0.1}}, {11, {0.6, 0.1}}},
		{-0.5, 0.0});
	RB_CHECK(ai::BallsToWin(S8.Game, 0, C8.Rules) == 3); // open table: the smaller group + the 8
	S8.Game.TableOpen = false;
	S8.Game.Players[0].Group = rules::BallGroup::Stripes;
	S8.Game.Players[1].Group = rules::BallGroup::Solids;
	RB_CHECK(ai::BallsToWin(S8.Game, 0, C8.Rules) == 4 && ai::BallsToWin(S8.Game, 1, C8.Rules) == 3);
}

RB_TEST(ARCH_AI8_ProfilesGrowWithSkill)
{
	for (int i = 0; i + 1 < human::kAiProfileCount; ++i)
	{
		const ai::PlannerProfile A = ai::GetPlannerProfile(aitest::kProfiles[i]);
		const ai::PlannerProfile B = ai::GetPlannerProfile(aitest::kProfiles[i + 1]);
		RB_CHECK(B.PotFamilies >= A.PotFamilies && B.SpinVariants >= A.SpinVariants && B.Placements >= A.Placements);
		RB_CHECK(B.NoisySamples >= A.NoisySamples && B.PositionDepth >= A.PositionDepth && B.SimulationBudget > A.SimulationBudget);
		RB_CHECK(B.PerceivedAimSigma < A.PerceivedAimSigma && B.RunoutRate > A.RunoutRate && B.ChoiceTolerance <= A.ChoiceTolerance);
		const human::AiKnowledge& K = human::GetAiProfile(aitest::kProfiles[i]).Knowledge;
		RB_CHECK(A.ModelsThrowAndSquirt == K.ModelsThrowAndSquirt && A.KnowsTableSlope == K.KnowsTableSlope && A.Safeties == K.Safeties);
		RB_CHECK(A.NoisySamples == (K.AssumesPerfectExecution ? 0 : K.SelfNoiseSamples));
	}
	// HF 5.5: road player K = 8, touring pro K = 16; the hustler sandbags until money is down.
	RB_CHECK(ai::GetPlannerProfile(human::AiProfileId::RoadPlayer).NoisySamples == 8);
	RB_CHECK(ai::GetPlannerProfile(human::AiProfileId::TouringPro).NoisySamples == 16);
	const ai::PlannerProfile Hustler = ai::GetPlannerProfile(human::AiProfileId::LocalHustler);
	RB_CHECK(Hustler.Sandbagger);
	const ai::PlannerProfile Hidden = ai::SandbaggingProfile(Hustler);
	const ai::PlannerProfile Bar = ai::GetPlannerProfile(human::AiProfileId::BarRegular);
	RB_CHECK(Hidden.Id == human::AiProfileId::LocalHustler && Hidden.PotFamilies == Bar.PotFamilies && !Hidden.Safeties && Hidden.NoisySamples == 0);
	// Opponent models: attributes of a profile map close to that profile.
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const ai::OpponentModel M = ai::OpponentModelFor(Id);
		const ai::OpponentModel A = ai::OpponentModelFromAttributes(human::GetAiProfile(Id).Attributes);
		RB_CHECK_NEAR(A.RunoutRate, M.RunoutRate, 0.02);
	}
	RB_CHECK(ai::OpponentModelFromAttributes(human::UniformAttributes(50.0)).PlaysSafeties);
	RB_CHECK(!ai::OpponentModelFromAttributes(human::UniformAttributes(25.0)).PlaysSafeties);
}

// The cue-ball direction spread each profile really produces (synthetic hand + human layer + squirt of the executed offsets, 4000
// match-key strokes of a medium centre-ball shot, closed bridge, no pressure) against the planner's perceived aim sigma.
RB_TEST(ARCH_AI8_ProfileAimSpread)
{
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		aitest::Player P = aitest::MakePlayer(Id, 1, 99);
		human::PlannedStroke Plan;
		Plan.Azimuth = 0.3;
		Plan.Elevation = 3.0 * kDegToRad;
		Plan.Speed = 2.0;
		human::StrokeSituation Situation;
		double Sum = 0.0;
		double Sum2 = 0.0;
		const int N = 4000;
		const double MassRatio = kDefaultBallMass / P.Cue.EndMass;
		const double K = InertiaFactor(MakeBallSpec(kR, kDefaultBallMass));
		for (int i = 0; i < N; ++i)
		{
			const human::NoiseKey Key = aitest::KeyFor(0xC0FFEEu, static_cast<std::uint32_t>(i / 20), static_cast<std::uint32_t>(i), P);
			const human::IntendedStroke I = human::SyntheticHand(Plan, P.Character, Situation, kR, Key, P.History, P.Human);
			const human::ExecutedStroke X = human::ExecuteStroke(I, P.Character.Profile.Attributes, Situation, P.Tip, P.CueBody, P.Cue, MakeBallSpec(kR, kDefaultBallMass),
				{0.0, 0.0, kR}, nullptr, 0, Key, P.History, P.Human);
			human::AdvanceNoiseHistory(P.History);
			const double Direction = X.Strike.Azimuth + SquirtAngle(X.Strike.OffsetA, MassRatio, K) - Plan.Azimuth;
			Sum += Direction;
			Sum2 += Direction * Direction;
		}
		const double Mean = Sum / N;
		const double Sd = Sqrt(Max(0.0, Sum2 / N - Mean * Mean));
		const double Rms = Sqrt(Sum2 / N);
		const double Perceived = ai::GetPlannerProfile(Id).PerceivedAimSigma;
		std::printf("  A-AI-8 %-18s cue-ball direction: bias %+.4f deg, sd %.4f deg, rms %.4f deg; perceived sigma %.4f deg\n", aitest::ProfileName(Id),
			Mean * kRadToDeg, Sd * kRadToDeg, Rms * kRadToDeg, Perceived * kRadToDeg);
		RB_CHECK(Rms > 0.0);
	}
}
