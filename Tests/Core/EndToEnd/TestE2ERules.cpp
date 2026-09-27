// Integration round 2 (end-to-end): the rules and the player model consume real physics output.
//   A-E2E-15 an 8-ball break through the match state machine: SetupRack (WP-9) -> ValidateDeclaration -> Simulator::Run ->
//            ShotRecord (WP-7) -> DeriveShotFacts -> EvaluateShot (WP-8) -> ApplyShot (WP-9), every fact checked against the
//            simulator's own finals (pocketed balls and pockets, first contact, the cue ball) and the next game state against the
//            physics positions;
//   A-E2E-16 9-ball shots on a hand-built game state: a legal pot of the lowest ball continues, hitting the 9 first is a wrong-ball
//            foul with ball in hand, a 1-9 combination wins the rack (rules.md 10.4 / R 5);
//   A-E2E-17 the player model (human-factors 3): ExecuteStroke -> CueStrikeInput -> Run, deterministic; a noisy miss is diagnosed by
//            the 3.9 loop (re-runs with one change each) within 3 s, and the after-shot equipment update runs on the real result.

#include "rbtest.h"

#include "EndToEnd/EndToEndUtil.h"
#include "Human/HumanTestUtil.h"

#include "rb/Equipment/Cue.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Shot/ShotRecordBuilder.h"

#include <chrono>

using namespace rb;

namespace
{
	constexpr double kR = simtest::kR;

	const TableGeometry& NineFoot() { return simtest::Table(kTableNineFootPro); }

	rules::RulesTable RulesTableOf(const TableGeometry& T)
	{
		double Radii[kMaxBalls];
		for (double& R : Radii)
		{
			R = kR;
		}
		return BuildRulesTable(T, kR, Radii, kMaxBalls);
	}

	SimInput& NewShot(const TableGeometry& T)
	{
		static SimInput In;
		In = SimInput{};
		In.Table = &T;
		In.Params = MakePhysicsParams(T.Spec);
		return In;
	}

	ShotResult& Result()
	{
		static ShotResult R;
		return R;
	}

	// The simulator's pocketed balls (bit per ball) and each one's pocket.
	std::uint32_t PocketedMask(const ShotResult& R, PocketId* Pockets)
	{
		std::uint32_t Mask = 0;
		for (int b = 0; b < rules::kRulesBallCount; ++b)
		{
			if (R.Finals[b].Status == BallFinalStatus::Pocketed)
			{
				Mask |= 1u << b;
				Pockets[b] = R.Finals[b].Pocket;
			}
		}
		return Mask;
	}

	// The facts agree with the physics finals: pocketed set and pockets (object balls and the cue ball), nothing off the table.
	void CheckFactsMatchFinals(const rules::ShotFacts& Facts, const ShotResult& R)
	{
		PocketId Pockets[kMaxBalls] = {};
		const std::uint32_t Physics = PocketedMask(R, Pockets);
		std::uint32_t Rules = 0;
		for (const rules::PocketedBall& P : Facts.Pocketed)
		{
			Rules |= 1u << P.Ball;
			RB_CHECK(P.Pocket == Pockets[P.Ball]);
		}
		RB_CHECK(Rules == Physics);
		RB_CHECK(Facts.CueBallPocketed == ((Physics & 1u) != 0u));
		RB_CHECK(!Facts.RecordTruncated);
		for (int b = 0; b < rules::kRulesBallCount; ++b)
		{
			if (R.Finals[b].Status == BallFinalStatus::OnTable)
			{
				RB_CHECK(Facts.Balls[b].EndStatus == BallEndStatus::OnTable);
				RB_CHECK(Facts.Balls[b].FinalPosition.x == R.Finals[b].State.Position.x && Facts.Balls[b].FinalPosition.y == R.Finals[b].State.Position.y);
			}
		}
	}

	rules::GameState NineBallState()
	{
		rules::GameState G;
		G.Game = rules::Discipline::NineBall;
		G.Shooter = 0;
		G.RackBreaker = 1;
		G.IsBreakShot = false;
		G.PushOutAvailable = false;
		G.CueBall = rules::CueBallNext::InPosition;
		for (int b = 0; b <= 9; ++b)
		{
			G.Balls[b].Kind = rules::BallStatusKind::Pocketed;
		}
		return G;
	}

	void OnTable(rules::GameState& G, const SimInput& In, int Ball)
	{
		G.Balls[Ball].Kind = rules::BallStatusKind::OnTable;
		G.Balls[Ball].Position = XY(In.Balls[Ball].State.Position);
	}
}

// A-E2E-15: an 8-ball break through the match flow with the real physics.
RB_TEST(Integ_ARCH_E2E15_EightBallBreakThroughTheRules)
{
	const TableGeometry& T = NineFoot();
	rules::MatchConfig C;
	C.Game = rules::Discipline::EightBall;
	C.RaceTo = 3;
	C.Rules = rules::MakeRulesConfig(rules::RulesPreset::Wpa8Ball);
	C.Table = RulesTableOf(T);
	C.Seed = 4242;
	C.RackGaps = kRackGapWoodenRack;
	rules::MatchState S;
	rules::StartMatch(C, S);
	rules::LagResult Lag;
	Lag.Outcome = rules::LagOutcome::FirstWins;
	RB_REQUIRE(rules::ApplyLagResult(C, S, Lag) == ErrorCode::Ok);
	RB_REQUIRE(rules::ChooseBreaker(C, S, 0) == ErrorCode::Ok);
	rules::RackAssignment Rack;
	RB_REQUIRE(rules::SetupRack(C, S, Rack) == ErrorCode::Ok);
	RB_REQUIRE(S.Phase == rules::MatchPhase::AwaitShot && S.Game.IsBreakShot && S.Game.Shooter == 0);

	rules::ShotDeclaration D;
	D.Kind = rules::ShotKind::Break;
	rules::CompleteDeclaration(C, S, D);
	const Vec2 Placed{C.Table.HeadStringX - 0.12, 0.08};
	RB_REQUIRE(rules::ValidateDeclaration(C, S, D, &Placed) == ErrorCode::Ok);

	// The physics input is the game state: racked balls where WP-9 put them, the cue ball where the player placed it.
	SimInput& In = NewShot(T);
	for (int b = 1; b < rules::kRulesBallCount; ++b)
	{
		if (S.Game.Balls[b].Kind == rules::BallStatusKind::OnTable)
		{
			simtest::Place(In, b, ToVec3(S.Game.Balls[b].Position, kR));
		}
	}
	simtest::Place(In, 0, ToVec3(Placed, kR));
	const int Apex = Rack.BallAtSite[0];
	RB_REQUIRE(Apex >= 1 && Apex <= 15);
	const Vec2 Aim = S.Game.Balls[Apex].Position - Placed;
	StrikeRequest Break = simtest::Strike(0, 9.5, std::atan2(Aim.y, Aim.x), 0.0, 0.0, -0.1);
	Break.Input.Cue = kCueBreak21oz;
	In.Strikes.PushBack(Break);
	In.Context.InHand = CueBallInHand::AboveHeadString;
	In.Context.PlacedPosition = Placed;
	ShotResult& R = Result();
	Simulator Sim;
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const e2e::ShotInvariants I = e2e::CheckInvariants(R, In);
	RB_CHECK(I.AtRest && I.Overlap <= 1e-6 && I.CushionPenetration <= 1e-6 && I.EnergyRise <= 1e-9 && I.Clean);

	rules::ShotFacts Facts;
	rules::DeriveShotFacts(R.Record, C.Table, RulesTolerances{}, kInfinity, Facts);
	CheckFactsMatchFinals(Facts, R);
	RB_CHECK(Facts.EarliestContact == Apex);
	RB_CHECK(Facts.CueBallPlacementLegal);
	const rules::ShotOutcome O = rules::EvaluateShot(C.Rules, C.Table, S.Game, D, Facts);
	std::printf("  8-ball break: %d pocketed, %d object balls to a rail, cue ball %s; outcome next %d shooter %d, fouls %08x, enforced %d (%s)\n",
		Facts.Pocketed.Size(), Facts.NumObjectBallsDrivenToRail, Facts.CueBallPocketed ? "scratched" : "on the table", static_cast<int>(O.Next), O.NextShooter,
		O.Detected.Bits, static_cast<int>(O.Enforced), O.RuleRef);
	RB_CHECK(Facts.NumObjectBallsDrivenToRail >= 4 || Facts.AnyObjectBallPocketed);
	RB_CHECK(!O.Detected.Has(rules::Foul::BreakTooFewRails));
	const bool EightDown = Facts.IsPocketed(8);
	if (!EightDown)
	{
		if (Facts.CueBallPocketed)
		{
			RB_CHECK(O.AnyFoul && O.Detected.Has(rules::Foul::CueBallScratch) && O.NextShooter == 1);
		}
		else if (Facts.AnyObjectBallPocketed)
		{
			RB_CHECK(!O.AnyFoul && O.Next == rules::NextAction::Continue && O.NextShooter == 0); // the breaker continues, table open
		}
		else
		{
			RB_CHECK(!O.AnyFoul && O.Next == rules::NextAction::Pass && O.NextShooter == 1);
		}
	}
	RB_REQUIRE(rules::ApplyShot(C, S, O, Facts) == ErrorCode::Ok);
	// The next game state carries the physics: pocketed balls are gone, the others lie where the simulation left them.
	if (!EightDown && S.Phase == rules::MatchPhase::AwaitShot)
	{
		for (int b = 1; b < rules::kRulesBallCount; ++b)
		{
			if (R.Finals[b].Status == BallFinalStatus::Pocketed)
			{
				RB_CHECK(S.Game.Balls[b].Kind == rules::BallStatusKind::Pocketed);
			}
			else if (R.Finals[b].Status == BallFinalStatus::OnTable)
			{
				RB_CHECK(S.Game.Balls[b].Kind == rules::BallStatusKind::OnTable);
				RB_CHECK(S.Game.Balls[b].Position.x == R.Finals[b].State.Position.x && S.Game.Balls[b].Position.y == R.Finals[b].State.Position.y);
			}
		}
		RB_CHECK(!S.Game.IsBreakShot && S.Game.TableOpen);
	}
}

// A-E2E-16: three 9-ball shots with the real physics on a hand-built state (balls 1 and 9 left, shooter 0, cue ball in position).
RB_TEST(Integ_ARCH_E2E16_NineBallShotsThroughTheRules)
{
	const TableGeometry& T = NineFoot();
	const rules::RulesTable Table = RulesTableOf(T);
	const rules::RulesConfig Config = rules::MakeRulesConfig(rules::RulesPreset::Wpa9Ball);
	const PocketGeometry& P = T.Pockets[static_cast<int>(PocketId::FootRight)];
	Simulator Sim;
	ShotResult& R = Result();
	rules::ShotDeclaration D; // 9-ball: no call

	// (a) A stun shot pots the 1 straight into the foot-right corner: legal, the shooter continues.
	{
		SimInput& In = NewShot(T);
		e2e::SetupStunShot(In, 0, 1, P.MouthMid - P.Axis * 0.4, P.Axis, 1.8, -0.3);
		simtest::Place(In, 9, {-0.7, 0.35, kR});
		rules::GameState G = NineBallState();
		OnTable(G, In, 0);
		OnTable(G, In, 1);
		OnTable(G, In, 9);
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		rules::ShotFacts Facts;
		rules::DeriveShotFacts(R.Record, Table, RulesTolerances{}, kInfinity, Facts);
		CheckFactsMatchFinals(Facts, R);
		const rules::ShotOutcome O = rules::EvaluateShot(Config, Table, G, D, Facts);
		RB_CHECK(Facts.EarliestContact == 1 && Facts.IsPocketed(1) && Facts.PocketOf(1) == PocketId::FootRight);
		RB_CHECK(!O.AnyFoul && O.Next == rules::NextAction::Continue && O.NextShooter == 0);
	}
	// (b) The cue ball hits the 9 first while the 1 is the lowest ball: wrong ball first, ball in hand for the opponent.
	{
		SimInput& In = NewShot(T);
		e2e::SetupStunShot(In, 0, 9, {0.1, 0.2}, {1.0, 0.0}, 1.2, -0.2);
		simtest::Place(In, 1, {-0.9, -0.4, kR});
		rules::GameState G = NineBallState();
		OnTable(G, In, 0);
		OnTable(G, In, 1);
		OnTable(G, In, 9);
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		rules::ShotFacts Facts;
		rules::DeriveShotFacts(R.Record, Table, RulesTolerances{}, kInfinity, Facts);
		CheckFactsMatchFinals(Facts, R);
		const rules::ShotOutcome O = rules::EvaluateShot(Config, Table, G, D, Facts);
		RB_CHECK(Facts.EarliestContact == 9);
		RB_CHECK(O.AnyFoul && O.Detected.Has(rules::Foul::WrongBallFirst));
		RB_CHECK(O.Next == rules::NextAction::Pass && O.NextShooter == 1 && O.NextCueBall == rules::CueBallNext::InHandAnywhere);
	}
	// (c) A 1-9 combination: the cue ball stuns into the 1, the 1 drives the 9 into the corner: the 9 legally pocketed wins the rack.
	{
		SimInput& In = NewShot(T);
		const Vec2 Nine = P.MouthMid - P.Axis * 0.3;
		const Vec2 One = Nine - P.Axis * (2.0 * kR + 0.06);
		e2e::SetupStunShot(In, 0, 1, One, P.Axis, 2.0, -0.3);
		simtest::Place(In, 9, ToVec3(Nine, kR));
		rules::GameState G = NineBallState();
		OnTable(G, In, 0);
		OnTable(G, In, 1);
		OnTable(G, In, 9);
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		rules::ShotFacts Facts;
		rules::DeriveShotFacts(R.Record, Table, RulesTolerances{}, kInfinity, Facts);
		CheckFactsMatchFinals(Facts, R);
		const rules::ShotOutcome O = rules::EvaluateShot(Config, Table, G, D, Facts);
		std::printf("  9-ball combination: 9 %s, 1 %s, outcome next %d winner %d\n", Facts.IsPocketed(9) ? "pocketed" : "not pocketed",
			Facts.IsPocketed(1) ? "pocketed" : "on the table", static_cast<int>(O.Next), O.Winner);
		RB_CHECK(Facts.EarliestContact == 1 && Facts.IsPocketed(9) && !Facts.CueBallPocketed);
		RB_CHECK(!O.AnyFoul && (O.Next == rules::NextAction::RackWon || O.Next == rules::NextAction::MatchWon) && O.Winner == 0);
	}
}

// A-E2E-17: the player model drives the physics (human-factors 3.1, 3.9; HF-B01 / HF-B06 on an object-ball shot).
RB_TEST(Integ_ARCH_E2E17_PlayerStrokeToDiagnosis)
{
	using namespace rb::human;
	using namespace rb::human::testhelp;
	constexpr double Radius = simtest::kR;
	const TableGeometry& T = NineFoot();
	const PocketGeometry& P = T.Pockets[static_cast<int>(PocketId::FootRight)];
	const Vec2 Object = P.MouthMid - P.Axis * 0.45;
	const Vec2 Cue{-0.4, 0.15};
	const Vec2 Ghost = Object - P.Axis * (2.0 * Radius); // full-ball aim point for a pot along the pocket axis (cut ~ 22 deg)
	Setup Base = MakeS0();
	Base.BallPosition = ToVec3(Cue, Radius);
	Base.Intended.Azimuth = std::atan2(Ghost.y - Cue.y, Ghost.x - Cue.x);
	Base.Intended.Elevation = 2.0 * kDegToRad;
	Base.Intended.Speed = 1.6;
	Base.Intended.AxisOffsetB = -0.2;
	const PhysicsParams Physics = MakePhysicsParams(T.Spec);
	Simulator Sim;
	ShotResult& R = Result();

	const auto Pots = [&](const Setup& S, const PhysicsParams& Params, std::uint64_t* Hash) -> bool {
		const ExecutedStroke X = Execute(S);
		RB_CHECK(X.Error == ErrorCode::Ok);
		SimInput& In = NewShot(T);
		In.Params = Params;
		simtest::Place(In, 0, S.BallPosition);
		simtest::Place(In, 1, ToVec3(Object, Radius));
		StrikeRequest Request;
		Request.Ball = 0;
		Request.Input = X.Strike;
		In.Strikes.PushBack(Request);
		In.Record.Trajectories = false;
		if (Sim.Run(In, R) != SimStatus::Ok)
		{
			return false;
		}
		if (Hash != nullptr)
		{
			*Hash = simtest::ResultHash(R);
		}
		return R.Finals[1].Status == BallFinalStatus::Pocketed && R.Finals[0].Status == BallFinalStatus::OnTable;
	};

	// Without the human layer the stroke pots the ball; the execution and the shot are bitwise reproducible (HF-B01).
	Setup Clean = Base;
	Clean.Params.NoiseScale = 0.0;
	std::uint64_t H1 = 0;
	std::uint64_t H2 = 0;
	RB_REQUIRE(Pots(Clean, Physics, &H1));
	RB_CHECK(Pots(Clean, Physics, &H2) && H1 == H2);
	RB_CHECK(SameStrike(Execute(Clean).Strike, Execute(Clean).Strike));

	// A shaky shooter under pressure: the first of the per-shot draws that misses (deterministic in the key).
	Setup Shaky = Base;
	Shaky.Params.NoiseScale = 1.0;
	Shaky.Attributes = UniformAttributes(0.0);
	Shaky.Situation.Pressure = 1.0;
	Shaky.Situation.Bridge = BridgeType::Elevated;
	Shaky.Situation.StanceDifficulty = 1.0;
	int Missed = -1;
	for (std::uint32_t Shot = 0; Shot < 40 && Missed < 0; ++Shot)
	{
		Shaky.Key = MakeKey(0xE2Eu, 0u, Shot, 1u, Shot);
		Shaky.History = RebuildNoiseHistory(Shaky.Key.MatchSeed, ShooterKey(Shaky.Key), Shaky.Key.ShooterShotIndex);
		Missed = Pots(Shaky, Physics, nullptr) ? -1 : static_cast<int>(Shot);
	}
	RB_REQUIRE(Missed >= 0);

	// The diagnosis (3.9): one change per re-run against the real shot, the first that pots names the cause; the last step turns
	// every human channel off, which is the clean stroke - so a cause other than "input" is found, within 3 s (HF-B06).
	const auto Start = std::chrono::steady_clock::now();
	MissCause Cause = MissCause::Input;
	for (int i = 0; i < kDiagnosisStepCount; ++i)
	{
		const DiagnosisStep Step = DiagnosisStepAt(i);
		Setup Rerun = Shaky;
		PhysicsParams Params = Physics;
		ApplyDiagnosisStep(Step, Rerun.Params, Rerun.Tip, Rerun.CueBody);
		ApplyDiagnosisStep(Step, Params, nullptr);
		if (Pots(Rerun, Params, nullptr))
		{
			Cause = Step.Cause;
			break;
		}
	}
	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	std::printf("  player model: miss at shot %d, diagnosis cause %d in %.3f s\n", Missed, static_cast<int>(Cause), Seconds);
	RB_CHECK(Cause != MissCause::Input && Cause != MissCause::Equipment && Cause != MissCause::Table); // fresh chalk, straight cue, level table
	RB_CHECK(Seconds < 3.0);

	// After the shot: the equipment update runs on the real result (tip wear from the core's strike, HF 4.1).
	const ExecutedStroke Real = Execute(Shaky);
	RB_REQUIRE(Pots(Shaky, Physics, nullptr) == false);
	TipState Tip = Shaky.Tip;
	ApplyShotToEquipment(Real, R, 0, Quat{}, Tip, nullptr);
	double Before = 0.0;
	double After = 0.0;
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		Before += Shaky.Tip.Coverage[z];
		After += Tip.Coverage[z];
	}
	RB_CHECK(After <= Before); // chalk only wears off in a stroke
}
