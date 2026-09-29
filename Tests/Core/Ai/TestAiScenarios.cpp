// Owner: WP-12 (AI opponent). Sanity scenarios of the planner with the full core (Docs/architecture.md 7.6); every shot is executed
// by the referee exactly like the game would (synthetic hand with the match key -> ExecuteStroke -> the real table -> rules):
//   A-AI-2  an easy hanging ball is pocketed by every profile but the tourist in >= 80 % of real executions;
//   A-AI-3  with no makeable shot the profiles that play safeties choose a safety that leaves the opponent without a good shot;
//   A-AI-4  push-out: a legal push-out when the table is bad after the break, none with an easy shot; the response is an offered
//           option;
//   A-AI-5  ball in hand: a legal placement that leaves a short, easy shot, made in >= 80 % of real executions;
//   A-AI-9  no heap allocation in the planner once its scratch and workers exist (Debug: CRT allocation hook);
//   A-AI-10 declarations: every decision passes rules::ValidateDeclaration; 8-ball calls an own-group ball (the 8 when the group
//           is cleared), 10-ball calls every pot;
//   A-AI-12 kisses and caroms: a cue-ball carom that wins the rack and a frozen dead kiss are chosen and made by the profiles
//           with a combination game;
//   hustler: sandbags until money is down (human-factors 5.5, Q6), not with MoneyGames::LeaguePrizeOnly.

#include "Ai/AiTestUtil.h"

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

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

	Vec2 Hanger(const TableGeometry& T, int Pocket, double Gap)
	{
		const PocketGeometry& P = T.Pockets[Pocket];
		return P.MouthMid - P.Axis * (kR + Gap);
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
	constexpr int kTrials = 4;
	constexpr int kLagTrials = 6;
#else
	constexpr bool kRelease = true;
	constexpr int kTrials = 30;
	constexpr int kLagTrials = 40;
#endif

	bool PlaysSafeties(human::AiProfileId Id) { return ai::GetPlannerProfile(Id).Safeties; }

	struct TrialResult
	{
		int Made = 0;                // trials whose shot pocketed Ball without a foul
		int Trials = 0;
		int ValidDeclarations = 0;
		int Fouls = 0;
		int Safeties = 0;            // safety or kick chosen
		int Defensive = 0;           // safety, kick, or a two-way pot the planner gives <= 20 %
		int PushOuts = 0;
		int Kisses = 0;              // kiss (object-ball carom) chosen
		int Caroms = 0;              // cue-ball carom chosen
		int Placements = 0;          // ball in hand: placed
		int LegalPlacements = 0;
		int ShortEasyPlacements = 0; // ball in hand: the planned pot from the placement is <= 0.75 m with a cut <= 35 deg
		int TurnPassed = 0;          // the opponent shoots next
		double OpponentChance = 0.0; // sum over TurnPassed of the opponent's static best-shot chance
		int Decisions = 0;           // AwaitDecision after the shot
		int ValidOptions = 0;        // ... answered with an offered option by the opponent's planner
		int CalledOwnGroup = 0;      // 8-ball: pots called with an own-group ball (or the 8 when cleared) and the planned pocket
		int CalledPots = 0;          // pots with a call
		int Pots = 0;                // pot decisions
	};

	// Plans and executes Count times from State (keys differ per trial).
	TrialResult Trials(const rules::MatchConfig& C, const rules::MatchState& State, human::AiProfileId Id, int Ball, int Count, bool Print,
		human::AiProfileId Opponent = human::AiProfileId::LeaguePlayer)
	{
		TrialResult Out;
		for (int k = 0; k < Count; ++k)
		{
			aitest::Player Me = aitest::MakePlayer(Id, 1, 1000u + static_cast<std::uint64_t>(k));
			rules::MatchState S = State;
			const int Self = S.Game.Shooter;
			const human::NoiseKey Key = aitest::KeyFor(0xA15EEDu + static_cast<std::uint64_t>(k) * 7919u, 1, static_cast<std::uint32_t>(k), Me);
			const ai::PlannerInput In = aitest::MakeInput(NineFoot(), NineFootParams(), C, S, Self, Me, ai::OpponentModelFor(Opponent), Key);
			const ai::PlannedDecision D = ai::PlanShot(In, ai::PlannerConfig{}, Scratch());
			if (D.Kind != ai::DecisionKind::Stroke)
			{
				continue;
			}
			if (Print && k == 0)
			{
				char Text[320];
				ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
				std::printf("  %-18s %s\n", aitest::ProfileName(Id), Text);
				if (aitest::TraceOn())
				{
					for (const ai::ConsideredShot& T : D.Reasoning.Top)
					{
						std::printf("      top: %-11s first %2d pot %2d pocket %3d score %.3f pot %.2f foul %.2f V %.2f A %.2f B %.2f (%d samples)\n", ai::ShotTypeName(T.Type),
							T.FirstBall, T.PotBall, static_cast<int>(T.Pocket), T.Score, T.PotChance, T.FoulChance, T.Speed, T.SpinA, T.SpinB, T.Samples);
					}
				}
			}
			if (D.PlaceCueBall)
			{
				++Out.Placements;
				Out.LegalPlacements += rules::CueBallPlacementLegal(S.Game, D.CueBallPlacement, S.Game.CueBall, C.Table, C.Rules.Tolerances) ? 1 : 0;
				if (D.PotBall != kNoBall && D.Pocket != PocketId::None)
				{
					const Vec2 O = S.Game.Balls[D.PotBall].Position;
					const ai::PocketAim Aim = ai::PocketAimFor(NineFoot(), static_cast<int>(D.Pocket), O, kR);
					const ai::ShotGeometry G = ai::CutGeometry(D.CueBallPlacement, kR, O, kR, Aim.Point, 89.0 * kDegToRad);
					Out.ShortEasyPlacements += G.Feasible && G.CueDistance <= 0.75 && Abs(G.Cut) <= 35.0 * kDegToRad ? 1 : 0;
				}
			}
			Out.Safeties += D.Type == ai::ShotType::Safety || D.Type == ai::ShotType::Kick ? 1 : 0;
			Out.Defensive += D.Type == ai::ShotType::Safety || D.Type == ai::ShotType::Kick || D.PotChance <= 0.2 ? 1 : 0;
			Out.PushOuts += D.Type == ai::ShotType::PushOut ? 1 : 0;
			Out.Kisses += D.Type == ai::ShotType::Kiss ? 1 : 0;
			Out.Caroms += D.Type == ai::ShotType::Carom ? 1 : 0;
			if (ai::IsPotShot(D.Type))
			{
				++Out.Pots;
				if (D.Declaration.Called.Ball != kNoBall)
				{
					++Out.CalledPots;
					const rules::BallGroup Mine = S.Game.Players[Self].Group;
					const bool Cleared = Mine != rules::BallGroup::None && rules::GroupCleared(S.Game, Mine);
					const bool Own = Cleared ? D.Declaration.Called.Ball == 8 : rules::GroupOf(D.Declaration.Called.Ball) == Mine;
					Out.CalledOwnGroup += Own && D.Declaration.Called.Pocket == D.Pocket && D.Declaration.Called.Ball == D.PotBall ? 1 : 0;
				}
			}
			const aitest::Executed X = aitest::ExecuteDecision(Ref(), D, Me, NineFoot(), NineFootParams(), C, S, Key);
			++Out.Trials;
			Out.ValidDeclarations += X.DeclarationValid ? 1 : 0;
			Out.Fouls += X.Outcome.AnyFoul ? 1 : 0;
			Out.Made += Ball > 0 && Ref().Facts.IsPocketed(Ball) && !X.Outcome.AnyFoul ? 1 : 0;
			if (S.Phase == rules::MatchPhase::AwaitShot && S.Game.Shooter != Self)
			{
				++Out.TurnPassed;
				ai::EvalContext E;
				E.Table = &NineFoot();
				E.Match = &C;
				Out.OpponentChance += ai::BestNextShot(E, S.Game, S.Game.Shooter).PotChance;
			}
			if (S.Phase == rules::MatchPhase::AwaitDecision)
			{
				++Out.Decisions;
				aitest::Player Other = aitest::MakePlayer(Opponent, 2, 2000u + static_cast<std::uint64_t>(k));
				const human::NoiseKey OtherKey = aitest::KeyFor(Key.MatchSeed, 1, Key.ShotIndex + 1, Other);
				const ai::PlannedDecision R = ai::PlanShot(
					aitest::MakeInput(NineFoot(), NineFootParams(), C, S, S.Decider, Other, ai::OpponentModelFor(Id), OtherKey), ai::PlannerConfig{}, Scratch());
				bool Offered = false;
				for (const rules::Option O : S.PendingOutcome.Options)
				{
					Offered = Offered || O == R.Choice;
				}
				Out.ValidOptions += R.Kind == ai::DecisionKind::Option && Offered && rules::ApplyOption(C, S, R.Choice) == ErrorCode::Ok ? 1 : 0;
			}
		}
		return Out;
	}

	double Share(int Count, int Of) { return Of > 0 ? static_cast<double>(Count) / Of : 0.0; }
}

// A-AI-2: an easy hanging ball (2 cm from the foot-left corner's mouth, cue ball 0.8 m away, cut about 20 deg).
RB_TEST(Integ_ARCH_AI2_HangingBallIsPocketed)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 42);
	const Vec2 One = Hanger(T, 3, 0.02);
	const Vec2 Axis = T.Pockets[3].Axis;
	const Vec2 Cue = One - Vec2{Axis.x * 0.9 + Axis.y * 0.3, Axis.y * 0.9 - Axis.x * 0.3} * 0.8;
	const rules::MatchState S = aitest::ScenarioState(C, 0, {{1, One}, {9, Vec2{-0.6, 0.2}}}, Cue);
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const TrialResult R = Trials(C, S, Id, 1, kTrials, true);
		const double Rate = Share(R.Made, R.Trials);
		std::printf("  A-AI-2 %-18s hanging 1-ball made %2d / %2d (%.0f %%)\n", aitest::ProfileName(Id), R.Made, R.Trials, 100.0 * Rate);
		RB_CHECK(R.Trials == kTrials && R.ValidDeclarations == R.Trials);
		RB_CHECK(Rate >= (Id == human::AiProfileId::Tourist ? 0.5 : 0.8));
	}
}

// A-AI-3: the 1-ball frozen to the middle of the foot rail, the cue ball at the head end: no direct pot or combination is
// possible (every cut > 80 deg); only the frozen-ball bank off the foot rail to a head corner, a long shot nobody makes. The
// profiles with a safety game play defensively (a safety, a kick, or that bank as a two-way shot they give <= 20 %) and leave
// the opponent with a poor shot; the others just hit it.
RB_TEST(Integ_ARCH_AI3_SafetyWhenNothingIsMakeable)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 43);
	const rules::MatchState S = aitest::ScenarioState(C, 0,
		{{1, {T.HalfLength - kR - 0.0005, 0.0}}, {5, {-0.2, -0.4}}, {7, {0.6, 0.45}}, {9, {0.2, 0.4}}}, {-0.9, 0.05});
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const TrialResult R = Trials(C, S, Id, 0, kTrials, true);
		const double Opp = R.TurnPassed > 0 ? R.OpponentChance / R.TurnPassed : 0.0;
		std::printf("  A-AI-3 %-18s defensive %2d / %2d (explicit safeties or kicks %2d), fouls %2d, opponent's next-shot chance %.2f (over %d)\n",
			aitest::ProfileName(Id), R.Defensive, R.Trials, R.Safeties, R.Fouls, Opp, R.TurnPassed);
		RB_CHECK(R.Trials == kTrials && R.ValidDeclarations == R.Trials);
		if (PlaysSafeties(Id))
		{
			RB_CHECK(Share(R.Defensive, R.Trials) >= 0.8);
			if (kRelease)
			{
				RB_CHECK(Share(R.Fouls, R.Trials) <= 0.3);
				RB_CHECK(Opp <= 0.5);
			}
		}
	}
}

// A-AI-4: right after the break with the 1 hooked behind the 2, 3 and 4 the profiles that use push-outs push (a legal push-out
// declaration), and the opponent answers with an offered option; with a hanging 1-ball nobody pushes.
RB_TEST(Integ_ARCH_AI4_PushOut)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 44);
	rules::MatchState Bad = aitest::ScenarioState(C, 0, {{1, {0.9, 0.0}}, {2, {0.9 - 2.0 * kR - 0.003, 2.0 * kR}}, {3, {0.9 - 2.0 * kR - 0.003, -2.0 * kR}},
		{4, {0.9 - 4.0 * kR - 0.006, 0.0}}, {6, {0.1, -0.45}}, {9, {-0.9, 0.4}}}, {-0.5, 0.0});
	Bad.Game.PushOutAvailable = true;
	rules::MatchState Good = aitest::ScenarioState(C, 0, {{1, Hanger(T, 3, 0.02)}, {6, {0.1, -0.45}}, {9, {-0.9, 0.4}}}, {0.6, 0.2});
	Good.Game.PushOutAvailable = true;
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const bool Pushes = ai::GetPlannerProfile(Id).PushOuts;
		const TrialResult R = Trials(C, Bad, Id, 0, kTrials, true);
		const TrialResult E = Trials(C, Good, Id, 1, kTrials, false);
		std::printf("  A-AI-4 %-18s hooked: push-outs %2d / %2d, answered with an offered option %d / %d; hanger: push-outs %d, made %d / %d\n",
			aitest::ProfileName(Id), R.PushOuts, R.Trials, R.ValidOptions, R.Decisions, E.PushOuts, E.Made, E.Trials);
		RB_CHECK(R.ValidDeclarations == R.Trials && E.ValidDeclarations == E.Trials);
		RB_CHECK(R.ValidOptions == R.Decisions);
		RB_CHECK(E.PushOuts == 0);
		if (Pushes)
		{
			RB_CHECK(Share(R.PushOuts, R.Trials) >= 0.7);
			RB_CHECK(R.Decisions >= R.PushOuts - R.Fouls);
		}
		else
		{
			RB_CHECK(R.PushOuts == 0);
		}
	}
}

// A-AI-5: cue ball in hand anywhere: the placement is legal and leaves a short, easy pot of the lowest ball, made in >= 80 % of
// real executions (tourist and bar regular >= 50 %).
RB_TEST(Integ_ARCH_AI5_BallInHandPlacement)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 45);
	const rules::MatchState S = aitest::ScenarioState(C, 0, {{2, {0.4, 0.2}}, {4, {-0.3, -0.35}}, {6, {0.9, 0.45}}, {9, {1.0, -0.2}}}, {}, true);
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const TrialResult R = Trials(C, S, Id, 2, kTrials, true);
		const double Rate = Share(R.Made, R.Trials);
		std::printf("  A-AI-5 %-18s placed %2d (legal %2d, short and easy %2d), 2-ball made %2d / %2d (%.0f %%)\n", aitest::ProfileName(Id), R.Placements,
			R.LegalPlacements, R.ShortEasyPlacements, R.Made, R.Trials, 100.0 * Rate);
		RB_CHECK(R.Trials == kTrials && R.Placements == R.Trials && R.LegalPlacements == R.Placements && R.ValidDeclarations == R.Trials);
		RB_CHECK(Share(R.ShortEasyPlacements, R.Placements) >= 0.8);
		const bool Weak = Id == human::AiProfileId::Tourist || Id == human::AiProfileId::BarRegular;
		RB_CHECK(Rate >= (Weak ? 0.5 : 0.8));
	}
}

// A-AI-12: kisses and caroms (the profiles with a combination game). (a) Cue-ball carom: the 9 hangs in the foot-left corner,
// the 1 is 45 cm in front of it, off the line; the 1 has no pot and no combination (the 9 and the 5 block its lines, every
// other pocket is a long, thin cut), but a thin hit on the 1 sends the cue ball along the tangent line into the 9: the rack. (b) Dead kiss: the
// 1 is frozen to the 5 with the tangent line through the foot-left corner's window, 50 cm away; the 5 blocks the direct pot.
RB_TEST(Integ_ARCH_AI12_KissesAndCaroms)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 50);
	const PocketGeometry& P3 = T.Pockets[3];
	const auto Rotate = [](const Vec2& V, double Deg) {
		const double A = Deg * kDegToRad;
		return Vec2{V.x * Cos(A) - V.y * Sin(A), V.x * Sin(A) + V.y * Cos(A)};
	};
	// (a) The carom.
	const Vec2 Nine = Hanger(T, 3, 0.02);
	const Vec2 Ghost9 = Nine - P3.Axis * (2.0 * kR);
	const Vec2 Tau = Rotate(P3.Axis, -10.0);                    // the cue ball's line into the 9 (10 deg cut)
	const Vec2 Contact = Ghost9 - Tau * 0.45;                    // cue-ball centre at the contact with the 1
	const Vec2 N = -PerpCcw(Tau);                                // line of centres cue ball -> 1
	const Vec2 One = Contact + N * (2.0 * kR);
	const Vec2 In = N * 0.5 + Tau * 0.8660254037844386;          // 60 deg cut on the 1
	const Vec2 Cue = Contact - In * 0.7;
	const Vec2 Blocker = One + Tau * 0.22 + N * kR;              // on the 1's line to the 9, clear of the cue ball's line
	const rules::MatchState Carom = aitest::ScenarioState(C, 0, {{1, One}, {5, Blocker}, {9, Nine}}, Cue);
	// (b) The dead kiss.
	const Vec2 Aim = P3.MouthMid + P3.Axis * (0.25 * kR);
	const Vec2 Line = Rotate(P3.Axis, 15.0);                     // the tangent line into the corner
	const Vec2 KissOne = Aim - Line * 0.5;
	const Vec2 Five = KissOne + PerpCcw(Line) * (2.0 * kR);     // frozen to the 1
	const Vec2 Drive = Normalized(PerpCcw(Line) + Line);         // the 1 driven between the 5 and the tangent line ...
	const Vec2 KissCue = KissOne - Rotate(Drive, -20.0) * 0.6;   // ... with a 20 deg cut
	const rules::MatchState Kiss = aitest::ScenarioState(C, 0, {{1, KissOne}, {5, Five}, {9, {-0.8, -0.3}}}, KissCue);
	std::printf("  A-AI-12 carom: 1 at (%.3f, %.3f), 9 at (%.3f, %.3f), cue (%.3f, %.3f); kiss: 1 at (%.3f, %.3f), 5 at (%.3f, %.3f), cue (%.3f, %.3f)\n",
		One.x, One.y, Nine.x, Nine.y, Cue.x, Cue.y, KissOne.x, KissOne.y, Five.x, Five.y, KissCue.x, KissCue.y);
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const TrialResult A = Trials(C, Carom, Id, 9, kTrials, true);
		const TrialResult B = Trials(C, Kiss, Id, 1, kTrials, true);
		std::printf("  A-AI-12 %-18s carom chosen %2d / %2d, 9 made %2d; kiss chosen %2d / %2d, 1 made %2d\n", aitest::ProfileName(Id), A.Caroms, A.Trials, A.Made,
			B.Kisses, B.Trials, B.Made);
		RB_CHECK(A.ValidDeclarations == A.Trials && B.ValidDeclarations == B.Trials);
		if (ai::GetPlannerProfile(Id).Caroms)
		{
			RB_CHECK(Share(A.Caroms, A.Trials) >= 0.8);
			RB_CHECK(Share(B.Kisses, B.Trials) >= 0.8);
			if (kRelease)
			{
				RB_CHECK(Share(A.Made, A.Trials) >= 0.6);
				RB_CHECK(Share(B.Made, B.Trials) >= 0.6);
			}
		}
		else
		{
			RB_CHECK(A.Caroms == 0 && B.Kisses == 0);
		}
	}
}

// A-AI-10 (scenario half): 8-ball with groups (the AI has the solids): pots are called with a solid and the planned pocket; with
// the solids cleared the 8 is called; 10-ball calls every pot. The referee validates every declaration.
RB_TEST(Integ_ARCH_AI10_CalledShots)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C8 = aitest::MakeMatch(rules::Discipline::EightBall, T, 46);
	rules::MatchState Groups = aitest::ScenarioState(C8, 0,
		{{2, {0.5, 0.3}}, {3, {-0.6, -0.4}}, {7, {0.9, -0.2}}, {8, {0.2, 0.0}}, {10, {1.0, 0.5}}, {12, {-0.2, 0.45}}, {15, {0.7, -0.5}}}, {-0.8, 0.1});
	Groups.Game.TableOpen = false;
	Groups.Game.Players[0].Group = rules::BallGroup::Solids;
	Groups.Game.Players[1].Group = rules::BallGroup::Stripes;
	rules::MatchState OnEight = aitest::ScenarioState(C8, 0, {{8, {0.8, 0.25}}, {10, {1.0, 0.5}}, {12, {-0.2, 0.45}}, {15, {0.7, -0.5}}}, {0.3, 0.1});
	OnEight.Game.TableOpen = false;
	OnEight.Game.Players[0].Group = rules::BallGroup::Solids;
	OnEight.Game.Players[1].Group = rules::BallGroup::Stripes;
	const rules::MatchConfig C10 = aitest::MakeMatch(rules::Discipline::TenBall, T, 47);
	const rules::MatchState Ten = aitest::ScenarioState(C10, 0, {{3, {0.5, 0.2}}, {5, {-0.4, -0.3}}, {10, {1.0, -0.4}}}, {-0.2, 0.1});
	const int N = kTrials < 6 ? kTrials : 6;
	for (const human::AiProfileId Id : aitest::kProfiles)
	{
		const TrialResult A = Trials(C8, Groups, Id, 0, N, false);
		const TrialResult B = Trials(C8, OnEight, Id, 8, N, false);
		const TrialResult D = Trials(C10, Ten, Id, 3, N, false);
		std::printf("  A-AI-10 %-18s 8-ball groups: %d / %d pots called with an own ball; on the 8: %d / %d (8 made %d); 10-ball: %d / %d pots called\n",
			aitest::ProfileName(Id), A.CalledOwnGroup, A.Pots, B.CalledOwnGroup, B.Pots, B.Made, D.CalledPots, D.Pots);
		RB_CHECK(A.ValidDeclarations == A.Trials && B.ValidDeclarations == B.Trials && D.ValidDeclarations == D.Trials);
		RB_CHECK(A.CalledOwnGroup == A.Pots && A.CalledPots == A.Pots);
		RB_CHECK(B.CalledOwnGroup == B.Pots && B.CalledPots == B.Pots);
		RB_CHECK(D.CalledPots == D.Pots);
	}
}

// Hustler (human-factors 5.5, Q6): sandbags until money is down (the bar regular's knowledge, a visible choice), plays his real
// game with the money down, and never sandbags when money games are off (MoneyGames::LeaguePrizeOnly).
// A-AI-13: the match start. (a) The lag (rules 4.1): every profile plans a lag from its lag position; the referee executes both
// players' lags in one simulation (synthetic hand with the match key -> ExecuteStroke, two strikes at t = 0) and the rules judge
// them: a good lag for every profile but the tourist in >= 80 % of real executions, the stronger profiles rest closer to the head
// cushion, and the touring pro wins most lags against the tourist. (b) The lag winner breaks in 9-ball and lets the other player
// break the 14.1 opening (a safety break).
RB_TEST(Integ_ARCH_AI13_LagAndBreakerChoice)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 51);
	rules::MatchState S;
	rules::StartMatch(C, S);
	RB_REQUIRE(S.Phase == rules::MatchPhase::Lag);
	Vec2 First;
	Vec2 Second;
	rules::LagStartPositions(C.Table, First, Second);
	const int Trials = kLagTrials;
	double MeanDistance[human::kAiProfileCount] = {};
	for (int p = 0; p < human::kAiProfileCount; ++p)
	{
		const human::AiProfileId Id = aitest::kProfiles[p];
		int Good = 0;
		int Wins = 0;
		int Decided = 0;
		double Sum = 0.0;
		for (int k = 0; k < Trials; ++k)
		{
			aitest::Player Players[2] = {aitest::MakePlayer(Id, 1, 3000u + static_cast<std::uint64_t>(k)),
				aitest::MakePlayer(human::AiProfileId::Tourist, 2, 4000u + static_cast<std::uint64_t>(k))};
			ai::PlannedDecision D[2];
			human::NoiseKey Keys[2];
			for (int Side = 0; Side < 2; ++Side)
			{
				Keys[Side] = aitest::KeyFor(0x1A6u + static_cast<std::uint64_t>(k), 0, 0, Players[Side]);
				D[Side] = ai::PlanShot(aitest::MakeInput(T, NineFootParams(), C, S, Side, Players[Side], ai::OpponentModelFor(human::AiProfileId::Tourist), Keys[Side]),
					ai::PlannerConfig{}, Scratch());
				RB_CHECK(D[Side].Error == ErrorCode::Ok && D[Side].Kind == ai::DecisionKind::Stroke && D[Side].Type == ai::ShotType::Lag && D[Side].PlaceCueBall);
				const Vec2 Want = Side == 0 ? First : Second;
				RB_CHECK(D[Side].CueBallPlacement.x == Want.x && D[Side].CueBallPlacement.y == Want.y);
			}
			if (k == 0)
			{
				char Text[320];
				ai::FormatReasoning(D[0], Text, static_cast<int>(sizeof(Text)));
				std::printf("  %-18s %s\n", aitest::ProfileName(Id), Text);
			}
			// The referee: both lag strokes through the hand and ExecuteStroke, one simulation.
			SimInput& In = Ref().In;
			In = SimInput{};
			In.Table = &T;
			In.Params = NineFootParams();
			for (int Side = 0; Side < 2; ++Side)
			{
				const Vec2 At = D[Side].CueBallPlacement;
				simtest::Place(In, Side, ToVec3(At, kR));
				aitest::Player& P = Players[Side];
				const human::IntendedStroke I = human::SyntheticHand(D[Side].Stroke, P.Character, D[Side].Situation, kR, Keys[Side], P.History, P.Human);
				const human::ExecutedStroke X = human::ExecuteStroke(I, P.Character.Profile.Attributes, D[Side].Situation, P.Tip, P.CueBody, P.Cue,
					MakeBallSpec(kR, kDefaultBallMass), ToVec3(At, kR), nullptr, 0, Keys[Side], P.History, P.Human);
				RB_CHECK(X.Error == ErrorCode::Ok);
				StrikeRequest R;
				R.Ball = static_cast<BallId>(Side);
				R.Input = X.Strike;
				In.Strikes.PushBack(R);
			}
			In.Record.Trajectories = false;
			In.Record.ShotRecord = true;
			RB_REQUIRE(Ref().Sim.Run(In, Ref().Result) == SimStatus::Ok);
			double Radii[kMaxBalls] = {};
			Radii[0] = kR;
			Radii[1] = kR;
			const rules::RulesTable Lagging = BuildRulesTable(T, kR, Radii, kMaxBalls);
			const rules::LagBallFacts A = rules::DeriveLagBallFacts(Ref().Result.Record, 0, Lagging, C.Rules.Tolerances);
			const rules::LagBallFacts B = rules::DeriveLagBallFacts(Ref().Result.Record, 1, Lagging, C.Rules.Tolerances);
			const rules::LagResult L = rules::EvaluateLag(A, B, C.Rules.Tolerances);
			Good += A.Bad ? 0 : 1;
			Sum += A.Bad ? 0.0 : A.Distance;
			Decided += L.Outcome == rules::LagOutcome::Relag ? 0 : 1;
			Wins += L.Outcome == rules::LagOutcome::FirstWins ? 1 : 0;
		}
		MeanDistance[p] = Good > 0 ? Sum / Good : kInfinity;
		std::printf("  A-AI-13 %-18s good lags %2d / %2d, mean rest distance %.3f m, won %2d of %2d decided lags against the tourist\n", aitest::ProfileName(Id), Good,
			Trials, MeanDistance[p], Wins, Decided);
		RB_CHECK(Share(Good, Trials) >= (Id == human::AiProfileId::Tourist ? 0.5 : 0.8));
		if (kRelease)
		{
			RB_CHECK(Id != human::AiProfileId::TouringPro || Share(Wins, Decided) >= 0.7);
		}
	}
	if (kRelease)
	{
		RB_CHECK(MeanDistance[human::kAiProfileCount - 1] < MeanDistance[0]);
		RB_CHECK(MeanDistance[human::kAiProfileCount - 1] < MeanDistance[1]);
	}

	// (b) The lag winner's choice.
	for (const rules::Discipline Game : {rules::Discipline::NineBall, rules::Discipline::EightBall, rules::Discipline::StraightPool})
	{
		const rules::MatchConfig G = aitest::MakeMatch(Game, T, 52);
		rules::MatchState W;
		rules::StartMatch(G, W);
		rules::LagResult Lag;
		Lag.Outcome = rules::LagOutcome::SecondWins;
		RB_REQUIRE(rules::ApplyLagResult(G, W, Lag) == ErrorCode::Ok && W.Phase == rules::MatchPhase::LagWinnerChooses && W.Decider == 1);
		aitest::Player Me = aitest::MakePlayer(human::AiProfileId::LeaguePlayer, 2, 52);
		const ai::PlannedDecision D =
			ai::PlanShot(aitest::MakeInput(T, NineFootParams(), G, W, 1, Me, ai::OpponentModelFor(human::AiProfileId::LeaguePlayer), aitest::KeyFor(52, 0, 0, Me)),
				ai::PlannerConfig{}, Scratch());
		char Text[200];
		ai::FormatReasoning(D, Text, static_cast<int>(sizeof(Text)));
		std::printf("  A-AI-13 discipline %d, the lag winner (player 1) decides: %s\n", static_cast<int>(Game), Text);
		RB_CHECK(D.Error == ErrorCode::Ok && D.Kind == ai::DecisionKind::ChooseBreaker);
		RB_CHECK(D.Breaker == (Game == rules::Discipline::StraightPool ? 0 : 1));
		RB_CHECK(rules::ChooseBreaker(G, W, D.Breaker) == ErrorCode::Ok && W.Phase == rules::MatchPhase::RackSetup);
	}
}

// Progress for the UI: idle before a decision; after the screening stage is reduced, the best shot so far is a pot of the
// position's legal ball with a valid start; after the decision the stage is Done.
RB_TEST(Integ_ARCH_AI13_ProgressForTheUi)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 53);
	const rules::MatchState S = aitest::ScenarioState(C, 0, {{1, Hanger(T, 3, 0.03)}, {5, {-0.3, -0.35}}, {9, {1.0, -0.2}}}, {0.4, 0.2});
	aitest::Player Me = aitest::MakePlayer(human::AiProfileId::TouringPro, 1, 53);
	ai::PlannerScratch Planner;
	RB_CHECK(Planner.Progress().Stage == ai::PlannerStage::Idle && !Planner.Progress().HasBest);
	Planner.Begin(aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(53, 1, 1, Me)),
		ai::PlannerConfig{});
	const ai::PlannerProgress P0 = Planner.Progress();
	RB_CHECK(P0.Stage == ai::PlannerStage::Screening && P0.Jobs == Planner.JobCount() && P0.Candidates > 0 && !P0.HasBest);
	int Stages = 0;
	bool SawBest = false;
	while (!Planner.Finished())
	{
		for (int j = 0; j < Planner.JobCount(); ++j)
		{
			Planner.RunJob(j, Planner.SerialWorker());
		}
		Planner.Advance();
		++Stages;
		const ai::PlannerProgress P = Planner.Progress();
		if (P.HasBest)
		{
			SawBest = true;
			RB_CHECK(P.Best.FirstBall == 1 && Length(P.BestCueBall - Vec2{0.4, 0.2}) < 1e-12);
		}
	}
	RB_CHECK(Planner.Progress().Stage == ai::PlannerStage::Done && SawBest && Stages >= 2);
	std::printf("  A-AI-13 progress: %d stages, best shot visible after screening\n", Stages);
}

RB_TEST(Integ_ARCH_AI_HustlerSandbagsUntilMoneyIsDown)
{
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 48);
	const rules::MatchState S = aitest::ScenarioState(C, 0, {{1, {0.5, 0.2}}, {3, {-0.4, -0.3}}, {6, {0.8, 0.4}}, {9, {1.0, -0.4}}}, {-0.2, 0.1});
	aitest::Player Me = aitest::MakePlayer(human::AiProfileId::LocalHustler, 1, 48);
	const human::NoiseKey Key = aitest::KeyFor(0x4057u, 1, 3, Me);
	ai::PlannerInput In = aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(human::AiProfileId::BarRegular), Key);
	In.MoneyDown = false;
	const ai::PlannedDecision Hidden = ai::PlanShot(In, ai::PlannerConfig{}, Scratch());
	In.MoneyDown = true;
	const ai::PlannedDecision Real = ai::PlanShot(In, ai::PlannerConfig{}, Scratch());
	In.MoneyDown = false;
	In.Money = human::MoneyGames::LeaguePrizeOnly;
	const ai::PlannedDecision League = ai::PlanShot(In, ai::PlannerConfig{}, Scratch());
	std::printf("  hustler: sandbagging %d (%d candidates), money down %d (%d candidates), league prizes only %d\n", Hidden.Reasoning.Sandbagging ? 1 : 0,
		Hidden.Reasoning.Candidates, Real.Reasoning.Sandbagging ? 1 : 0, Real.Reasoning.Candidates, League.Reasoning.Sandbagging ? 1 : 0);
	RB_CHECK(Hidden.Kind == ai::DecisionKind::Stroke && Real.Kind == ai::DecisionKind::Stroke);
	RB_CHECK(Hidden.Reasoning.Sandbagging && !Real.Reasoning.Sandbagging && !League.Reasoning.Sandbagging);
	RB_CHECK(Hidden.Reasoning.Candidates < Real.Reasoning.Candidates);
}

#if defined(_MSC_VER) && defined(_DEBUG)
namespace
{
	long GAllocations = 0;
	bool GArmed = false;

	int CountingHook(int Type, void*, std::size_t, int, long, const unsigned char*, int)
	{
		if (GArmed && (Type == _HOOK_ALLOC || Type == _HOOK_REALLOC))
		{
			++GAllocations;
		}
		return 1;
	}
}
#endif

// A-AI-9: once a PlannerScratch (and its worker) exists, a whole decision - generation, screening with aim correction, noisy
// samples, second ply, reduction - allocates nothing (Debug: the CRT allocation hook counts every heap allocation of this thread
// while armed; Release builds skip the count).
RB_TEST(Integ_ARCH_AI9_NoAllocationInTheHotLoop)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	const TableGeometry& T = NineFoot();
	const rules::MatchConfig C = aitest::MakeMatch(rules::Discipline::NineBall, T, 49);
	const rules::MatchState S = aitest::ScenarioState(C, 0, {{1, {0.5, 0.2}}, {3, {-0.4, -0.3}}, {6, {0.8, 0.4}}, {9, {1.0, -0.4}}}, {-0.2, 0.1});
	const rules::MatchState Hand = aitest::ScenarioState(C, 0, {{2, {0.4, 0.2}}, {4, {-0.3, -0.35}}, {9, {1.0, -0.2}}}, {}, true);
	aitest::Player Me = aitest::MakePlayer(human::AiProfileId::TouringPro, 1, 49);
	static ai::PlannerScratch Planner;
	ai::PlannerConfig Config;
	Config.Samples = 0.25; // Debug speed: fewer samples, same code paths
	const ai::PlannerInput A = aitest::MakeInput(T, NineFootParams(), C, S, 0, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(1, 1, 1, Me));
	const ai::PlannerInput B = aitest::MakeInput(T, NineFootParams(), C, Hand, 0, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(1, 1, 2, Me));
	rules::MatchState LagState;
	rules::StartMatch(C, LagState);
	const ai::PlannerInput L = aitest::MakeInput(T, NineFootParams(), C, LagState, 1, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(1, 0, 0, Me));
	// WP-12 review: the 14.1 opening (safety) break and ball in hand behind the head string (8-ball) take their own paths.
	const rules::MatchConfig C14 = aitest::MakeMatch(rules::Discipline::StraightPool, T, 49);
	rules::MatchState Opening = aitest::StartMatchState(C14, 0);
	rules::RackAssignment Rack;
	rules::SetupRack(C14, Opening, Rack);
	const ai::PlannerInput O = aitest::MakeInput(T, NineFootParams(), C14, Opening, 0, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(1, 1, 3, Me));
	const rules::MatchConfig C8 = aitest::MakeMatch(rules::Discipline::EightBall, T, 49);
	rules::MatchState Kitchen = aitest::ScenarioState(C8, 0, {{1, {C8.Table.HeadStringX - 0.3, 0.3}}, {3, {0.7, 0.4}}, {8, {0.9, -0.2}}, {9, {0.2, 0.1}}}, {}, true,
		rules::CueBallNext::InHandAboveHeadString);
	Kitchen.Game.TableOpen = false;
	Kitchen.Game.Players[0].Group = rules::BallGroup::Solids;
	Kitchen.Game.Players[1].Group = rules::BallGroup::Stripes;
	const ai::PlannerInput K = aitest::MakeInput(T, NineFootParams(), C8, Kitchen, 0, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(1, 1, 4, Me));
	// WP-12 review: the spot request (14.1, every ball behind the head string) and an option (the 14.1 breaking-foul options after
	// a soft opening break played by the referee before the count is armed).
	const rules::MatchState Spot = aitest::ScenarioState(C14, 0, {{5, {C14.Table.HeadStringX - 0.25, 0.3}}, {11, {C14.Table.HeadStringX - 0.5, -0.35}}}, {}, true,
		rules::CueBallNext::InHandAboveHeadString);
	const ai::PlannerInput P = aitest::MakeInput(T, NineFootParams(), C14, Spot, 0, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(1, 1, 5, Me));
	rules::MatchState Foul = Opening;
	{
		static aitest::Referee Ref;
		aitest::Player Breaker = aitest::MakePlayer(human::AiProfileId::Tourist, 2, 49);
		Breaker.Human.NoiseScale = 0.0;
		const human::NoiseKey BreakKey = aitest::KeyFor(1, 1, 6, Breaker);
		ai::PlannedDecision Soft = ai::PlanShot(
			aitest::MakeInput(T, NineFootParams(), C14, Foul, 0, Breaker, ai::OpponentModelFor(human::AiProfileId::Tourist), BreakKey), ai::PlannerConfig{}, Planner);
		Soft.Stroke.Speed = 0.9;
		aitest::ExecuteDecision(Ref, Soft, Breaker, T, NineFootParams(), C14, Foul, BreakKey);
	}
	RB_REQUIRE(Foul.Phase == rules::MatchPhase::AwaitDecision && Foul.Decider == 1);
	const ai::PlannerInput D = aitest::MakeInput(T, NineFootParams(), C14, Foul, 1, Me, ai::OpponentModelFor(human::AiProfileId::TouringPro), aitest::KeyFor(1, 1, 7, Me));
	ai::PlanShot(A, Config, Planner); // warm-up (nothing should be left to reserve)
	_CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(CountingHook);
	GAllocations = 0;
	for (const ai::PlannerInput* In : {&A, &B, &L, &O, &K, &P, &D})
	{
		GArmed = true;
		Planner.Begin(*In, Config);
		while (!Planner.Finished())
		{
			for (int j = 0; j < Planner.JobCount(); ++j)
			{
				Planner.RunJob(j, Planner.SerialWorker());
			}
			Planner.Advance();
		}
		GArmed = false;
		const ai::DecisionKind Want = In == &P ? ai::DecisionKind::RequestSpot : (In == &D ? ai::DecisionKind::Option : ai::DecisionKind::Stroke);
		RB_CHECK(Planner.Decision().Error == ErrorCode::Ok && Planner.Decision().Kind == Want);
	}
	_CrtSetAllocHook(Previous);
	std::printf("  A-AI-9 heap allocations during seven warm decisions (touring pro: a shot, ball in hand, the lag, the 14.1 opening break, ball in "
				"hand behind the head string, a spot request, an option): %ld (0)\n", GAllocations);
	RB_CHECK(GAllocations == 0);
#else
	std::printf("  A-AI-9 needs the Debug CRT allocation hook: skipped in this build\n");
#endif
}
