// Match director tests (Docs/ue-architecture.md 13, UE-6b): headless match flows through the real rules and the real
// simulator, world-free (no actors, no simulation subsystem: the director simulates on the game thread with
// URbSimulationSubsystem::RunShotBlocking) and at live playback rate 0 (commit right after the simulation, review R-15).
// Scripted strikes / scripted IntendedStrokes only (no UE-2 playback, no UE-5a input). Owner: UE-6b.
//
//   Unit.Match.StartRack             9-ball rack, break ball in hand above the head string, Assisted input mode, guest profile
//   Unit.Match.PlacementRules        illegal placements refused (region, overlap, pocket opening, off table), legal accepted
//   Unit.Match.BreakTurnLogic        rack -> scripted break -> the director follows the evaluated outcome, table state in sync
//   Unit.Match.ScratchBallInHand     scratch -> foul, ball in hand anywhere for the opponent (hot-seat)
//   Unit.Match.SpottingSync          9 pocketed on a foul -> spotted: GameState and TableState agree (review R-12)
//   Unit.Match.NinePocketedWins      the 9 wins the rack -> RackOver -> Confirm -> next rack ... race over -> MatchOver -> new match
//   Unit.Match.PracticeFlow          practice: no race end, Confirm racks again, both rules players are the one shooter (slot 0)
//   Unit.Match.ThreeFoulsHotSeat     two-foul warning, the third consecutive foul loses the rack
//   Unit.Match.NoiseScaleZero        ExecuteStroke path with NoiseScale 0 == the intended stroke (HF-T08 through the director)
//   Unit.Match.AbortedStroke         an abort that showed the ramp spends the per-shot draws (HF-B13), one without does not
//   Unit.Match.AutoChalk             the tip wears on a human stroke and is auto-chalked before the next shot
//   Unit.Match.ReplayAllowed         IsReplayAllowed per director phase (incl. Simulating)
//   Unit.Match.AssistedInputMode     rules InputMode::Assisted, NonTipContacts empty (review R-11)
//   Unit.Match.PushOutDecision       legal break -> push-out -> AwaitDecision -> CycleOption / Confirm
//   Unit.Match.Lag                   hot-seat lag: two strokes, ONE simulation with two strikes, the winner breaks
//   Unit.Match.Rerack                stalemate re-rack keeps the rack's breaker
//   Unit.Match.Options               ARbGameMode URL options

#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Simulation/RbTableContext.h"
#include "Tests/RbTestFlags.h"

#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "rb/Human/Chores.h"
#include "rb/Human/HumanModel.h"
#include "rb/Human/Progression.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/TableRules.h"

#include <initializer_list>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbMatchTestUtil
{
	using FBallAt = TPair<int32, rb::Vec2>;

	TStrongObjectPtr<URbMatchDirector> MakeDirector(FAutomationTestBase& Test, ERbMatchMode Mode, int64 Seed, int32 RaceTo = 5, bool bLag = false,
		double NoiseScale = 1.0)
	{
		FString Error;
		const TSharedPtr<const FRbTableContext> Table = FRbTableContext::Create(FRbTableSetup{}, Error);
		if (!Table.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("table context: %s"), *Error));
			return nullptr;
		}
		TStrongObjectPtr<URbMatchDirector> Director(NewObject<URbMatchDirector>(GetTransientPackage()));
		Director->Initialize(nullptr, nullptr, nullptr, nullptr);
		Director->SetTableContext(Table);
		Director->SetLivePlaybackRate(0.0f);
		FRbMatchSetup Setup;
		Setup.Mode = Mode;
		Setup.Seed = Seed;
		Setup.RaceTo = RaceTo;
		Setup.bLag = bLag;
		Setup.NoiseScale = NoiseScale;
		if (!Director->StartMatch(Setup))
		{
			Test.AddError(FString::Printf(TEXT("StartMatch failed: %s"), *Director->GetLastError()));
			return nullptr;
		}
		return Director;
	}

	double Radius(const URbMatchDirector& D, int32 Id) { return D.GetTableContext()->BallRadius(Id); }

	const rb::PocketGeometry& Pocket(const URbMatchDirector& D, rb::PocketId Id)
	{
		return D.GetTableContext()->Geometry.Pockets[static_cast<int32>(Id)];
	}

	// Mid-rack layout: exactly these balls on the table (cue ball = id 0 in position).
	void SetLayout(URbMatchDirector& D, std::initializer_list<FBallAt> Layout)
	{
		FRbTableState S = D.GetTableState();
		for (rb::SimBall& B : S.Balls)
		{
			B.InPlay = false;
		}
		for (const FBallAt& At : Layout)
		{
			rb::SimBall& B = S.Balls[At.Key];
			B.InPlay = true;
			B.State.Position = rb::Vec3(At.Value.x, At.Value.y, Radius(D, At.Key));
		}
		D.SetTableStateForTest(S);
	}

	// Scripted strike of the cue ball toward a plan point.
	bool ShootAt(URbMatchDirector& D, const rb::Vec2& Target, double Speed, double OffsetB = 0.0)
	{
		const rb::Vec3 Cue = D.GetTableState().Balls[0].State.Position;
		return D.SubmitScriptedStrike(Speed, FMath::Atan2(Target.y - Cue.y, Target.x - Cue.x), 0.0, 0.0, OffsetB);
	}

	bool ShootDirection(URbMatchDirector& D, const rb::Vec2& Direction, double Speed, double OffsetB = 0.0)
	{
		return D.SubmitScriptedStrike(Speed, FMath::Atan2(Direction.y, Direction.x), 0.0, 0.0, OffsetB);
	}

	// R-12: the table state is the physics mirror of the rules' GameState (status and plan position, z = R, at rest).
	void CheckTableStateSync(FAutomationTestBase& Test, const URbMatchDirector& D, const FString& What)
	{
		const rb::rules::GameState& G = D.GetMatchState().Game;
		const FRbTableState& T = D.GetTableState();
		int32 Mismatches = 0;
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::SimBall& B = T.Balls[Id];
			bool bExpected = Id < rb::rules::kRulesBallCount && G.Balls[Id].Kind == rb::rules::BallStatusKind::OnTable;
			rb::Vec2 Expected = Id < rb::rules::kRulesBallCount ? G.Balls[Id].Position : rb::Vec2{};
			if (Id == 0 && D.IsCueBallInHand())
			{
				bExpected = D.IsCueBallPlaced();
				Expected = D.GetPlacedCueBall();
			}
			bool bOk = B.InPlay == bExpected && B.Spec.Radius == D.GetTableContext()->BallRadius(Id);
			if (bOk && bExpected)
			{
				bOk = B.State.Position.x == Expected.x && B.State.Position.y == Expected.y && B.State.Position.z == B.Spec.Radius &&
					B.State.State == rb::MotionState::Stationary && rb::LengthSquared(B.State.Velocity) == 0.0 && rb::LengthSquared(B.State.Omega) == 0.0;
			}
			if (!bOk)
			{
				++Mismatches;
				Test.AddError(FString::Printf(TEXT("%s: ball %d table state (in play %d at %.6f, %.6f, %.6f) != rules (%d at %.6f, %.6f)"), *What, Id,
					B.InPlay ? 1 : 0, B.State.Position.x, B.State.Position.y, B.State.Position.z, bExpected ? 1 : 0, Expected.x, Expected.y));
			}
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: table state in sync with the rules"), *What), Mismatches, 0);
	}

	FString Describe(const URbMatchDirector& D)
	{
		const FRbLastShotSummary& L = D.GetLastShot();
		FString Pocketed;
		for (const int32 B : L.Pocketed)
		{
			Pocketed += FString::Printf(TEXT(" %d"), B);
		}
		return FString::Printf(TEXT("phase %d shooter %d next %d fouls %08x rule %s first %d pocketed [%s] wins %d:%d fouls %d:%d"),
			static_cast<int32>(D.GetPhase()), D.GetMatchState().Game.Shooter, static_cast<int32>(L.Next), L.Fouls.Bits, *L.RuleRef, L.FirstContactBall,
			*Pocketed, D.GetMatchState().RackWins[0], D.GetMatchState().RackWins[1], D.GetMatchState().Game.Players[0].ConsecutiveFouls,
			D.GetMatchState().Game.Players[1].ConsecutiveFouls);
	}

	// The director phase that must follow a committed shot (for a rules phase and the cue-ball state).
	ERbDirectorPhase ExpectedPhase(const URbMatchDirector& D)
	{
		switch (D.GetMatchState().Phase)
		{
		case rb::rules::MatchPhase::AwaitShot: return D.IsCueBallInHand() ? ERbDirectorPhase::AwaitPlacement : ERbDirectorPhase::AwaitStroke;
		case rb::rules::MatchPhase::AwaitDecision: return ERbDirectorPhase::AwaitDecision;
		case rb::rules::MatchPhase::RackOver: return ERbDirectorPhase::RackOver;
		case rb::rules::MatchPhase::MatchOver: return ERbDirectorPhase::MatchOver;
		default: return ERbDirectorPhase::Idle;
		}
	}

	// Break: ball in hand behind the head string, full hit on the apex ball.
	bool Break(FAutomationTestBase& Test, URbMatchDirector& D, double Speed = 9.0)
	{
		const rb::rules::RulesTable& Rules = D.GetMatchConfig().Table;
		if (!Test.TestTrue(TEXT("break: cue ball placed behind the head string"), D.PlaceCueBall(rb::Vec2(Rules.HeadStringX - 0.12, 0.08))))
		{
			return false;
		}
		const int32 Apex = rb::rules::LowestObjectBallAtStart(D.GetMatchState().Game);
		return Test.TestTrue(TEXT("break submitted"), ShootAt(D, rb::XY(D.GetTableState().Balls[Apex].State.Position), Speed, -0.1));
	}

	bool SameStrike(const rb::CueStrikeInput& A, const rb::CueStrikeInput& B)
	{
		return A.Speed == B.Speed && A.Elevation == B.Elevation && A.Azimuth == B.Azimuth && A.OffsetA == B.OffsetA && A.OffsetB == B.OffsetB &&
			A.Cue.TipFriction == B.Cue.TipFriction && A.Cue.TipDomeRadius == B.Cue.TipDomeRadius && A.TipTouchesCloth == B.TipTouchesCloth;
	}
}

using namespace RbMatchTestUtil;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchStartRack, "RawBreak.Unit.Match.StartRack", RB_UNIT_TEST_FLAGS)
bool FRbMatchStartRack::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 11);
	if (!D.IsValid())
	{
		return false;
	}
	const rb::rules::MatchState& S = D->GetMatchState();
	TestEqual(TEXT("break: ball in hand -> AwaitPlacement"), D->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	TestTrue(TEXT("rules in AwaitShot on the break"), S.Phase == rb::rules::MatchPhase::AwaitShot && S.Game.IsBreakShot && S.RackNumber == 1);
	TestTrue(TEXT("player 1 breaks the first rack"), S.Game.Shooter == 0 && S.Game.RackBreaker == 0);
	TestTrue(TEXT("placement region above the head string"), D->GetConstraints().PlacementRegion == rb::rules::CueBallNext::InHandAboveHeadString);
	TestTrue(TEXT("cue ball in hand"), D->IsCueBallInHand() && !D->IsCueBallPlaced() && !D->GetTableState().Balls[0].InPlay);
	TestTrue(TEXT("Assisted input mode (R-11)"), D->GetMatchConfig().Rules.Input == rb::rules::InputMode::Assisted);
	TestTrue(TEXT("9-ball"), D->GetMatchConfig().Game == rb::rules::Discipline::NineBall);
	TestEqual(TEXT("race"), D->GetMatchConfig().RaceTo, 5);
	TestEqual(TEXT("seed kept"), D->GetMatchSeed(), static_cast<uint64>(11));
	int32 Racked = 0;
	double ApexX = 1e9;
	int32 Apex = -1;
	for (int32 Id = 1; Id < rb::rules::kRulesBallCount; ++Id)
	{
		const bool bOn = S.Game.Balls[Id].Kind == rb::rules::BallStatusKind::OnTable;
		TestEqual(*FString::Printf(TEXT("ball %d racked iff 1..9"), Id), bOn, Id <= 9);
		if (bOn)
		{
			++Racked;
			if (S.Game.Balls[Id].Position.x < ApexX)
			{
				ApexX = S.Game.Balls[Id].Position.x;
				Apex = Id;
			}
		}
	}
	TestEqual(TEXT("9 balls racked"), Racked, 9);
	TestEqual(TEXT("the 1 at the apex"), Apex, 1);
	TestTrue(TEXT("the 9 on the foot spot"), S.Game.Balls[9].Position.x == D->GetMatchConfig().Table.FootSpot.x && S.Game.Balls[9].Position.y == 0.0);
	CheckTableStateSync(*this, *D, TEXT("rack"));

	for (int32 P = 0; P < 2; ++P)
	{
		const FRbShooterState& Shooter = D->GetShooter(P);
		TestEqual(TEXT("guest profile 50 (HF Q4)"), Shooter.Attributes.Steadiness, 50.0);
		TestEqual(TEXT("guest profile 50 (Nerve)"), Shooter.Attributes.Nerve, 50.0);
		TestTrue(TEXT("noise history empty for the match stream"),
			Shooter.ShooterShotIndex == 0 && Shooter.History.NextIndex == 0 && Shooter.History.MatchSeed == 11 && Shooter.History.Shooter == static_cast<uint64>(P));
	}
	TestEqual(TEXT("hot-seat names"), D->GetShooter(1).Name, FString(TEXT("Player 2")));

	// The stroke context the stroke component gets (R-04): the active shooter, the match key, friendly stakes.
	const FRbStrokeContext Context = D->MakeStrokeContext();
	TestTrue(TEXT("context key"), Context.Key.MatchSeed == 11 && Context.Key.ShooterId == 0 && Context.Key.ShotIndex == 0 && Context.Key.RackIndex == 0 &&
		Context.Key.Purpose == 0);
	TestTrue(TEXT("context pressure: friendly stakes only (0.35 x 0.3)"), FMath::IsNearlyEqual(Context.Situation.Pressure, 0.35 * 0.3, 1e-12));
	TestTrue(TEXT("context fatigue / intoxication 0"), Context.Situation.Fatigue == 0.0 && Context.Situation.Intoxication == 0.0);
	TestEqual(TEXT("context cue ball spec"), Context.CueBall.Radius, D->GetTableContext()->BallRadius(0));

	// Practice: one shooter slot, unbounded race.
	TStrongObjectPtr<URbMatchDirector> P = MakeDirector(*this, ERbMatchMode::Practice, 5);
	if (P.IsValid())
	{
		TestTrue(TEXT("practice: a won rack racks again (no race)"), P->GetMatchConfig().RaceTo > 1000000);
		TestTrue(TEXT("practice: both rules players are the same shooter"), &P->GetShooter(0) == &P->GetShooter(1));
		TestTrue(TEXT("practice stakes 0"), P->MakeStrokeContext().Situation.Pressure == 0.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchPlacementRules, "RawBreak.Unit.Match.PlacementRules", RB_UNIT_TEST_FLAGS)
bool FRbMatchPlacementRules::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 3);
	if (!D.IsValid())
	{
		return false;
	}
	const rb::rules::RulesTable& Rules = D->GetMatchConfig().Table;
	const double R = Radius(*D, 0);
	const double HalfL = 0.5 * Rules.Length;
	const double HalfW = 0.5 * Rules.Width;

	TestFalse(TEXT("no stroke before the placement"), D->SubmitScriptedStrike(3.0, 0.0, 0.0, 0.0, 0.0));
	// Break: strictly above (behind) the head string.
	TestFalse(TEXT("break: below the head string refused"), D->PlaceCueBall(rb::Vec2(0.0, 0.1)));
	TestFalse(TEXT("break: on the head string refused"), D->PlaceCueBall(rb::Vec2(Rules.HeadStringX, 0.1)));
	TestFalse(TEXT("off the table refused"), D->PlaceCueBall(rb::Vec2(-HalfL + 0.5 * R, 0.0)));
	TestFalse(TEXT("into the cushion refused"), D->PlaceCueBall(rb::Vec2(-1.0, HalfW - 0.5 * R)));
	const rb::PocketGeometry& Corner = Pocket(*D, rb::PocketId::HeadLeft);
	TestFalse(TEXT("over a pocket opening refused"), D->PlaceCueBall(Corner.MouthMid + Corner.Axis * 0.005));
	TestFalse(TEXT("NaN refused"), D->PlaceCueBall(rb::Vec2(std::numeric_limits<double>::quiet_NaN(), 0.0)));
	TestEqual(TEXT("still placing"), D->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	TestTrue(TEXT("refused placement leaves the cue ball out"), !D->GetTableState().Balls[0].InPlay);

	const rb::Vec2 Legal(Rules.HeadStringX - 0.2, 0.1);
	TestTrue(TEXT("legal placement accepted"), D->PlaceCueBall(Legal));
	TestEqual(TEXT("-> AwaitStroke"), D->GetPhase(), ERbDirectorPhase::AwaitStroke);
	TestTrue(TEXT("placed cue ball in the table state (z = R)"), D->GetTableState().Balls[0].InPlay && D->GetTableState().Balls[0].State.Position.x == Legal.x &&
		D->GetTableState().Balls[0].State.Position.z == R);
	CheckTableStateSync(*this, *D, TEXT("placed"));
	TestTrue(TEXT("re-placing while still in hand"), D->PlaceCueBall(rb::Vec2(Rules.HeadStringX - 0.3, -0.2)));
	TestFalse(TEXT("re-placing below the string still refused"), D->PlaceCueBall(rb::Vec2(Rules.HeadStringX + 0.05, -0.2)));

	// Ball in hand anywhere: overlaps with object balls are refused, touching is legal.
	SetLayout(*D, {{1, rb::Vec2(0.3, 0.2)}, {9, rb::Vec2(0.6, -0.3)}});
	TestEqual(TEXT("layout without the cue ball -> ball in hand"), D->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	TestTrue(TEXT("in hand anywhere"), D->GetConstraints().PlacementRegion == rb::rules::CueBallNext::InHandAnywhere);
	const double Contact = R + Radius(*D, 1);
	TestFalse(TEXT("overlapping the 1 refused"), D->PlaceCueBall(rb::Vec2(0.3 + Contact - 0.001, 0.2)));
	TestTrue(TEXT("anywhere: below the head string is fine"), D->CanPlaceCueBall(rb::Vec2(0.0, 0.0)));
	TestTrue(TEXT("touching the 1 is legal"), D->PlaceCueBall(rb::Vec2(0.3 + Contact + 1e-9, 0.2)));
	CheckTableStateSync(*this, *D, TEXT("placed in hand anywhere"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchBreakTurnLogic, "RawBreak.Unit.Match.BreakTurnLogic", RB_UNIT_TEST_FLAGS)
bool FRbMatchBreakTurnLogic::RunTest(const FString& Parameters)
{
	for (const int64 Seed : {11ll, 12ll, 13ll})
	{
		TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, Seed);
		if (!D.IsValid() || !Break(*this, *D))
		{
			return false;
		}
		const FRbLastShotSummary& Last = D->GetLastShot();
		const rb::rules::MatchState& S = D->GetMatchState();
		AddInfo(FString::Printf(TEXT("seed %lld break: %s, sim %.2f ms"), Seed, *Describe(*D), Last.SimMilliseconds));
		TestTrue(TEXT("shot committed"), Last.bValid && D->GetMatchShotIndex() == 1 && D->GetLastCommittedShot().IsValid());
		TestEqual(TEXT("breaker shot"), Last.Shooter, 0);
		TestEqual(TEXT("first contact: the 1"), Last.FirstContactBall, 1);
		TestFalse(TEXT("the break is no longer pending"), S.Game.IsBreakShot);
		TestEqual(TEXT("director phase follows the rules"), D->GetPhase(), ExpectedPhase(*D));
		switch (Last.Next)
		{
		case rb::rules::NextAction::Continue:
			TestTrue(TEXT("pocketed without a foul: the breaker continues, push-out available"),
				!Last.Fouls.Bits && S.Game.Shooter == 0 && D->GetConstraints().PushOutAllowed && !D->IsCueBallInHand());
			break;
		case rb::rules::NextAction::Pass:
			TestEqual(TEXT("pass: the opponent shoots"), S.Game.Shooter, 1);
			TestEqual(TEXT("a foul gives ball in hand, a miss the position"), D->IsCueBallInHand(), Last.Fouls.Bits != 0);
			break;
		case rb::rules::NextAction::AwaitDecision:
			TestTrue(TEXT("three-ball rule: the opponent decides"), S.Decider == 1 && S.PendingOutcome.Options.Size() == 2);
			break;
		case rb::rules::NextAction::RackWon:
			TestTrue(TEXT("9 on the break wins"), S.RackWins[0] == 1);
			break;
		default:
			AddError(TEXT("unexpected outcome of a 9-ball break"));
			break;
		}
		for (const int32 Ball : Last.Pocketed)
		{
			if (Ball != 0 && Ball != 9)
			{
				TestFalse(*FString::Printf(TEXT("pocketed ball %d is off the table"), Ball), D->GetTableState().Balls[Ball].InPlay);
			}
		}
		CheckTableStateSync(*this, *D, FString::Printf(TEXT("after the break (seed %lld)"), Seed));
		// The next stroke context: shot 1 of the match, the incoming shooter.
		TestEqual(TEXT("next key: shot 1"), D->MakeStrokeContext().Key.ShotIndex, 1u);
		TestTrue(TEXT("replays allowed after the commit"), D->IsReplayAllowed());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchScratchBallInHand, "RawBreak.Unit.Match.ScratchBallInHand", RB_UNIT_TEST_FLAGS)
bool FRbMatchScratchBallInHand::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 21);
	if (!D.IsValid())
	{
		return false;
	}
	const rb::PocketGeometry& P = Pocket(*D, rb::PocketId::FootRight);
	SetLayout(*D, {{0, P.MouthMid - P.Axis * 0.3}, {1, rb::Vec2(-0.9, 0.4)}, {9, rb::Vec2(-0.7, 0.35)}});
	TestEqual(TEXT("cue ball in position"), D->GetPhase(), ERbDirectorPhase::AwaitStroke);
	TestTrue(TEXT("straight into the corner"), ShootDirection(*D, P.Axis, 1.5));
	const FRbLastShotSummary& Last = D->GetLastShot();
	AddInfo(Describe(*D));
	TestTrue(TEXT("scratch foul"), Last.Fouls.Has(rb::rules::Foul::CueBallScratch));
	TestTrue(TEXT("cue ball pocketed"), Last.Pocketed.Contains(0));
	TestEqual(TEXT("ball in hand for the opponent"), D->GetMatchState().Game.Shooter, 1);
	TestEqual(TEXT("-> AwaitPlacement"), D->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	TestTrue(TEXT("anywhere"), D->GetConstraints().PlacementRegion == rb::rules::CueBallNext::InHandAnywhere);
	TestEqual(TEXT("foul counted for player 1"), D->GetMatchState().Game.Players[0].ConsecutiveFouls, 1);
	TestFalse(TEXT("the cue ball is out of the table state until placed"), D->GetTableState().Balls[0].InPlay);
	CheckTableStateSync(*this, *D, TEXT("after the scratch"));
	TestTrue(TEXT("the opponent places below the head string"), D->PlaceCueBall(rb::Vec2(0.2, -0.1)));
	TestEqual(TEXT("opponent addresses"), D->GetPhase(), ERbDirectorPhase::AwaitStroke);
	TestEqual(TEXT("the opponent's stroke context"), D->MakeStrokeContext().Key.ShooterId, 1u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchSpottingSync, "RawBreak.Unit.Match.SpottingSync", RB_UNIT_TEST_FLAGS)
bool FRbMatchSpottingSync::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 31);
	if (!D.IsValid())
	{
		return false;
	}
	// The 9 in the jaws of the foot-right corner, the cue ball behind it on the pocket axis; the 1 is the lowest ball, so hitting
	// the 9 first is a foul (wrong ball first) and the pocketed 9 is spotted (R 5.6).
	const rb::PocketGeometry& P = Pocket(*D, rb::PocketId::FootRight);
	const rb::Vec2 Nine = P.MouthMid - P.Axis * 0.08;
	SetLayout(*D, {{0, Nine - P.Axis * 0.35}, {1, rb::Vec2(-0.9, 0.4)}, {9, Nine}});
	TestTrue(TEXT("stroke into the 9"), ShootDirection(*D, P.Axis, 1.6, -0.3));
	const FRbLastShotSummary& Last = D->GetLastShot();
	AddInfo(Describe(*D));
	TestTrue(TEXT("the 9 was pocketed (physics)"), Last.Pocketed.Contains(9));
	TestTrue(TEXT("wrong ball first"), Last.Fouls.Has(rb::rules::Foul::WrongBallFirst));
	const rb::rules::GameState& G = D->GetMatchState().Game;
	const FRbTableState& T = D->GetTableState();
	TestTrue(TEXT("the 9 is back on the table in the rules"), G.Balls[9].Kind == rb::rules::BallStatusKind::OnTable);
	TestTrue(TEXT("spotted on the foot spot (free)"), G.Balls[9].Position.x == D->GetMatchConfig().Table.FootSpot.x && G.Balls[9].Position.y == 0.0);
	TestTrue(TEXT("TableState: the 9 in play at the spotted position"), T.Balls[9].InPlay && T.Balls[9].State.Position.x == G.Balls[9].Position.x &&
		T.Balls[9].State.Position.y == G.Balls[9].Position.y && T.Balls[9].State.Position.z == Radius(*D, 9));
	CheckTableStateSync(*this, *D, TEXT("after spotting"));

	// The next simulation starts from the spotted 9: the opponent places and shoots, the input carries the 9 at the spot.
	TestTrue(TEXT("opponent places"), D->PlaceCueBall(rb::Vec2(-0.5, 0.3)));
	TestTrue(TEXT("opponent shoots at the 1"), ShootAt(*D, rb::Vec2(-0.9, 0.4), 1.0));
	const TSharedPtr<const FRbShot> Next = D->GetLastCommittedShot();
	TestTrue(TEXT("the spotted 9 is part of the next SimInput"), Next.IsValid() && Next->Request.Input.Balls[9].InPlay &&
		Next->Request.Input.Balls[9].State.Position.x == D->GetMatchConfig().Table.FootSpot.x);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchNinePocketedWins, "RawBreak.Unit.Match.NinePocketedWins", RB_UNIT_TEST_FLAGS)
bool FRbMatchNinePocketedWins::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 41, 2);
	if (!D.IsValid())
	{
		return false;
	}
	const rb::PocketGeometry& P = Pocket(*D, rb::PocketId::FootRight);
	const rb::Vec2 Nine = P.MouthMid - P.Axis * 0.3;
	const auto PotTheNine = [&](int32 Shooter) {
		SetLayout(*D, {{0, Nine - P.Axis * 0.25}, {9, Nine}});
		TestEqual(TEXT("shooter"), D->GetMatchState().Game.Shooter, Shooter);
		TestTrue(TEXT("stun into the 9"), ShootDirection(*D, P.Axis, 2.0, -0.35));
		AddInfo(Describe(*D));
		TestTrue(TEXT("the 9 pocketed without a foul"), D->GetLastShot().Pocketed.Contains(9) && D->GetLastShot().Fouls.Bits == 0);
	};

	PotTheNine(0);
	TestTrue(TEXT("rack won"), D->GetLastShot().Next == rb::rules::NextAction::RackWon);
	TestEqual(TEXT("-> RackOver"), D->GetPhase(), ERbDirectorPhase::RackOver);
	TestEqual(TEXT("1 : 0"), D->GetMatchState().RackWins[0], 1);
	TestTrue(TEXT("replay allowed in RackOver"), D->IsReplayAllowed());
	TestFalse(TEXT("no stroke in RackOver"), D->SubmitScriptedStrike(2.0, 0.0, 0.0, 0.0, 0.0));
	TestTrue(TEXT("Confirm racks the next rack (R-20)"), D->Confirm());
	TestEqual(TEXT("rack 2"), D->GetMatchState().RackNumber, 2);
	TestEqual(TEXT("alternate break: player 2"), D->GetMatchState().Game.Shooter, 1);
	TestEqual(TEXT("break ball in hand"), D->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	CheckTableStateSync(*this, *D, TEXT("rack 2"));
	TestEqual(TEXT("new rack index in the noise key"), D->MakeStrokeContext().Key.RackIndex, 1u);

	PotTheNine(1);
	TestEqual(TEXT("1 : 1"), D->GetMatchState().RackWins[1], 1);
	TestTrue(TEXT("Confirm: rack 3"), D->Confirm() && D->GetMatchState().RackNumber == 3 && D->GetMatchState().Game.Shooter == 0);

	PotTheNine(0);
	TestTrue(TEXT("match won"), D->GetPhase() == ERbDirectorPhase::MatchOver && D->GetMatchState().Winner == 0 && D->GetMatchState().RackWins[0] == 2);
	TestTrue(TEXT("replay allowed in MatchOver"), D->IsReplayAllowed());
	TestTrue(TEXT("Confirm starts a new match"), D->Confirm());
	TestTrue(TEXT("new match: 0 : 0, rack 1, same seed"), D->GetMatchState().RackWins[0] == 0 && D->GetMatchState().RackWins[1] == 0 &&
		D->GetMatchState().RackNumber == 1 && D->GetMatchSeed() == 41 && D->GetPhase() == ERbDirectorPhase::AwaitPlacement && D->GetMatchShotIndex() == 0);
	TestFalse(TEXT("nothing to confirm while placing"), D->Confirm());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchPracticeFlow, "RawBreak.Unit.Match.PracticeFlow", RB_UNIT_TEST_FLAGS)
bool FRbMatchPracticeFlow::RunTest(const FString& Parameters)
{
	// Race 1 in the setup: practice ignores it, a won rack never ends the match (7.1).
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::Practice, 131, 1);
	if (!D.IsValid())
	{
		return false;
	}
	const rb::PocketGeometry& P = Pocket(*D, rb::PocketId::FootRight);
	const rb::Vec2 Nine = P.MouthMid - P.Axis * 0.3;
	SetLayout(*D, {{0, Nine - P.Axis * 0.25}, {9, Nine}});
	TestTrue(TEXT("stun into the 9"), ShootDirection(*D, P.Axis, 2.0, -0.35));
	AddInfo(Describe(*D));
	TestTrue(TEXT("rack won"), D->GetLastShot().Next == rb::rules::NextAction::RackWon && D->GetMatchState().RackWins[0] == 1);
	TestEqual(TEXT("practice: RackOver, never MatchOver"), D->GetPhase(), ERbDirectorPhase::RackOver);
	TestTrue(TEXT("Confirm racks again"), D->Confirm() && D->GetMatchState().RackNumber == 2 && D->GetPhase() == ERbDirectorPhase::AwaitPlacement);
	CheckTableStateSync(*this, *D, TEXT("practice rack 2"));

	// The rules alternate the break (rules player 2 breaks rack 2), but both rules players are the one human: shooter slot 0.
	TestEqual(TEXT("rules player 2 breaks rack 2"), D->GetActivePlayer(), 1);
	TestTrue(TEXT("rules player 2 maps to the one shooter"), &D->GetShooter(1) == &D->GetShooter(0) && D->GetShooter(1).Name == TEXT("Player 1"));
	const FRbStrokeContext Context = D->MakeStrokeContext();
	TestTrue(TEXT("the one shooter's noise stream, rack 2"), Context.Key.ShooterId == 0 && Context.Key.RackIndex == 1 && Context.Key.ShotIndex == 1);
	TestEqual(TEXT("practice stakes"), Context.Situation.Pressure, 0.0);
	TestTrue(TEXT("place behind the head string"), D->PlaceCueBall(rb::Vec2(D->GetMatchConfig().Table.HeadStringX - 0.12, 0.08)));
	const uint32 Before = D->GetShooter(0).ShooterShotIndex;
	TestEqual(TEXT("scripted strikes reveal no draws"), Before, 0u);
	const rb::Vec3 CueBall = D->GetTableState().Balls[0].State.Position;
	const rb::Vec3 Apex = D->GetTableState().Balls[1].State.Position;
	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = FMath::Atan2(Apex.y - CueBall.y, Apex.x - CueBall.x);
	Commit.Intended.AxisOffsetB = -0.1;
	Commit.Intended.Speed = 8.0;
	TestTrue(TEXT("human break stroke of rules player 2"), D->SubmitStroke(Commit));
	const TSharedPtr<const FRbShot> Shot = D->GetLastCommittedShot();
	TestTrue(TEXT("the stroke used the one shooter's key"), Shot.IsValid() && Shot->Request.Stroke.bHuman && Shot->Request.Shooter == 1 &&
		Shot->Request.Stroke.Key.ShooterId == 0 && Shot->Request.Stroke.Key.ShooterShotIndex == Before);
	TestEqual(TEXT("slot 0 revealed the draw"), D->GetShooter(0).ShooterShotIndex, Before + 1);
	TestTrue(TEXT("its tip took the hit"), D->GetShooter(1).Tip.Hits == 1);
	TestEqual(TEXT("director phase follows the rules"), D->GetPhase(), ExpectedPhase(*D));
	CheckTableStateSync(*this, *D, TEXT("after the practice break"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchThreeFoulsHotSeat, "RawBreak.Unit.Match.ThreeFoulsHotSeat", RB_UNIT_TEST_FLAGS)
bool FRbMatchThreeFoulsHotSeat::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 51, 3);
	if (!D.IsValid())
	{
		return false;
	}
	SetLayout(*D, {{0, rb::Vec2(-0.8, -0.3)}, {1, rb::Vec2(0.6, 0.3)}, {9, rb::Vec2(0.7, -0.3)}});
	// Every shot: a soft stroke into the head cushion that touches no ball = foul (R 3.3); the fouls of both
	// players alternate, player 1's third consecutive foul loses the rack (R 5.8).
	const auto Foul = [&](int32 Shooter, int32 FoulsBefore, bool bPlace) {
		TestEqual(TEXT("shooter"), D->GetMatchState().Game.Shooter, Shooter);
		TestEqual(TEXT("two-foul warning shown iff on two fouls (Reg 8)"), D->GetConstraints().ThreeFoulWarning, FoulsBefore == 2);
		if (bPlace)
		{
			TestTrue(TEXT("ball in hand placement"), D->PlaceCueBall(rb::Vec2(-0.8, Shooter == 0 ? -0.3 : 0.3)));
		}
		TestTrue(TEXT("foul stroke"), D->SubmitScriptedStrike(0.4, PI, 0.0, 0.0, 0.0));
		AddInfo(Describe(*D));
		TestTrue(TEXT("no contact is a foul (R 3.3: no ball contacted, no rail after contact)"), D->GetLastShot().Fouls.Has(rb::rules::Foul::NoRailAfterContact));
	};
	Foul(0, 0, false);
	TestEqual(TEXT("player 1 on one foul"), D->GetMatchState().Game.Players[0].ConsecutiveFouls, 1);
	Foul(1, 0, true);
	Foul(0, 1, true);
	TestEqual(TEXT("player 1 on two fouls"), D->GetMatchState().Game.Players[0].ConsecutiveFouls, 2);
	Foul(1, 1, true);
	Foul(0, 2, true);
	TestTrue(TEXT("third consecutive foul"), D->GetLastShot().Fouls.Has(rb::rules::Foul::ThreeConsecutiveFouls));
	TestTrue(TEXT("player 1 loses the rack"), D->GetLastShot().Next == rb::rules::NextAction::RackWon && D->GetMatchState().RackWins[1] == 1 &&
		D->GetMatchState().RackWins[0] == 0);
	TestEqual(TEXT("-> RackOver"), D->GetPhase(), ERbDirectorPhase::RackOver);
	TestTrue(TEXT("Confirm: next rack resets the counters"), D->Confirm() && D->GetMatchState().Game.Players[0].ConsecutiveFouls == 0 &&
		D->GetMatchState().Game.Players[1].ConsecutiveFouls == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchNoiseScaleZero, "RawBreak.Unit.Match.NoiseScaleZero", RB_UNIT_TEST_FLAGS)
bool FRbMatchNoiseScaleZero::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::Practice, 61, 5, false, 0.0);
	if (!D.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("NoiseScale 0 from the setup"), D->GetHumanParams().NoiseScale, 0.0);
	SetLayout(*D, {{0, rb::Vec2(-0.6, 0.1)}, {1, rb::Vec2(0.2, 0.3)}, {9, rb::Vec2(0.5, -0.3)}});
	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = 0.23;
	Commit.Intended.Elevation = 4.0 * rb::kDegToRad;
	Commit.Intended.AxisOffsetA = 0.2;
	Commit.Intended.AxisOffsetB = -0.3;
	Commit.Intended.Speed = 2.5;
	Commit.Intended.TimeDown = 3.0;
	Commit.Intended.ForwardStart = 2.6;
	Commit.AddressIndex = 2;
	Commit.InputLog.Add(FRbStrokeSample{1.0, -0.1});
	Commit.InputLog.Add(FRbStrokeSample{1.1, 0.0});
	const FRbStrokeContext Before = D->MakeStrokeContext();
	TestTrue(TEXT("stroke submitted"), D->SubmitStroke(Commit));
	const TSharedPtr<const FRbShot> Shot = D->GetLastCommittedShot();
	if (!TestTrue(TEXT("shot committed"), Shot.IsValid()))
	{
		return false;
	}
	const FRbStrokeRecord& Record = Shot->Request.Stroke;
	const rb::CueStrikeInput& Strike = Record.Executed.Strike;
	TestTrue(TEXT("human-layer record"), Record.bHuman && Record.Executed.Error == rb::ErrorCode::Ok && Record.InputLog.Num() == 2);
	TestTrue(TEXT("azimuth == intended (bitwise)"), Strike.Azimuth == Commit.Intended.Azimuth);
	TestTrue(TEXT("elevation == intended (bitwise)"), Strike.Elevation == Commit.Intended.Elevation);
	TestTrue(TEXT("speed == intended (bitwise)"), Strike.Speed == Commit.Intended.Speed);
	TestTrue(TEXT("axis offsets == intended (bitwise)"), Record.Executed.AxisOffset.x == 0.2 && Record.Executed.AxisOffset.y == -0.3);
	const rb::Vec2 Contact = rb::AimToContactOffset(rb::Vec2(0.2, -0.3), Radius(*D, 0), Before.Tip.DomeRadius);
	TestTrue(TEXT("contact offsets = AimToContactOffset(intended)"), FMath::IsNearlyEqual(Strike.OffsetA, Contact.x, 1e-15) &&
		FMath::IsNearlyEqual(Strike.OffsetB, Contact.y, 1e-15));
	TestTrue(TEXT("the simulator got exactly the executed strike"), Shot->Request.Input.Strikes.Size() == 1 && Shot->Request.Input.Strikes[0].Ball == 0 &&
		SameStrike(Shot->Request.Input.Strikes[0].Input, Strike));
	TestTrue(TEXT("noise key of the shot"), Record.Key.MatchSeed == 61 && Record.Key.ShotIndex == 0 && Record.Key.ShooterShotIndex == 0 &&
		Record.Key.AddressIndex == 2 && Record.Key.ShooterId == 0);
	TestTrue(TEXT("ExecuteStroke with the director's context reproduces the record"), [&] {
		const rb::human::ExecutedStroke Again = rb::human::ExecuteStroke(Commit.Intended, Before.Attributes, Before.Situation, Before.Tip, Before.CueBody,
			Before.Cue, Before.CueBall, Shot->Request.Input.Balls[0].State.Position, nullptr, 0, Record.Key, Before.History, Before.Params);
		return SameStrike(Again.Strike, Strike);
	}());
	const FRbShooterState& Shooter = D->GetShooter(0);
	TestTrue(TEXT("the tip contact revealed one per-shot draw"), Shooter.ShooterShotIndex == 1 && Shooter.History.NextIndex == 1);
	TestTrue(TEXT("executed speed shown in the summary"), D->GetLastShot().CueSpeed == 2.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchAbortedStroke, "RawBreak.Unit.Match.AbortedStroke", RB_UNIT_TEST_FLAGS)
bool FRbMatchAbortedStroke::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 71);
	if (!D.IsValid())
	{
		return false;
	}
	D->OnStrokeAborted(true);
	TestEqual(TEXT("no stroke possible while placing: nothing spent"), D->GetShooter(0).ShooterShotIndex, 0u);
	TestTrue(TEXT("place"), D->PlaceCueBall(rb::Vec2(D->GetMatchConfig().Table.HeadStringX - 0.2, 0.0)));
	D->OnStrokeAborted(false);
	TestEqual(TEXT("abort before the ramp: nothing spent"), D->GetShooter(0).ShooterShotIndex, 0u);
	D->OnStrokeAborted(true);
	const FRbShooterState& S = D->GetShooter(0);
	TestEqual(TEXT("abort with the ramp: ShooterShotIndex++ (HF-B13)"), S.ShooterShotIndex, 1u);
	TestEqual(TEXT("history advanced"), S.History.NextIndex, 1u);
	const rb::human::NoiseHistory Canonical = rb::human::RebuildNoiseHistory(71, 0, 1);
	bool bSame = Canonical.NextIndex == S.History.NextIndex && Canonical.MatchSeed == S.History.MatchSeed;
	for (int32 C = 0; C < rb::human::kStreakChannelCount; ++C)
	{
		bSame = bSame && Canonical.Channels[C].Count == S.History.Channels[C].Count &&
			FMemory::Memcmp(Canonical.Channels[C].Eighths, S.History.Channels[C].Eighths, sizeof(Canonical.Channels[C].Eighths)) == 0;
	}
	TestTrue(TEXT("history == RebuildNoiseHistory (cache stays canonical)"), bSame);
	TestEqual(TEXT("the pushed context uses the next draw"), D->MakeStrokeContext().Key.ShooterShotIndex, 1u);
	TestEqual(TEXT("the opponent is untouched"), D->GetShooter(1).ShooterShotIndex, 0u);

	// The shot after the abort draws index 1 and then moves on to 2.
	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = 0.0;
	Commit.Intended.Speed = 6.0;
	TestTrue(TEXT("stroke"), D->SubmitStroke(Commit));
	TestTrue(TEXT("the shot used draw 1"), D->GetLastCommittedShot().IsValid() && D->GetLastCommittedShot()->Request.Stroke.Key.ShooterShotIndex == 1);
	TestEqual(TEXT("after the shot: 2"), D->GetShooter(0).ShooterShotIndex, 2u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchAutoChalk, "RawBreak.Unit.Match.AutoChalk", RB_UNIT_TEST_FLAGS)
bool FRbMatchAutoChalk::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::Practice, 81);
	if (!D.IsValid())
	{
		return false;
	}
	SetLayout(*D, {{0, rb::Vec2(-0.6, 0.1)}, {1, rb::Vec2(0.2, 0.3)}, {9, rb::Vec2(0.5, -0.3)}});
	const rb::human::TipState Fresh = D->GetShooter(0).Tip;
	TestEqual(TEXT("a full tip needs no twist"), D->GetShooter(0).LastChalkTwists, 0);
	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = 0.5;
	Commit.Intended.AxisOffsetA = 0.45; // right English: wears a rim zone
	Commit.Intended.Speed = 3.0;
	TestTrue(TEXT("stroke"), D->SubmitStroke(Commit));
	const TSharedPtr<const FRbShot> Shot = D->GetLastCommittedShot();
	if (!TestTrue(TEXT("shot"), Shot.IsValid()))
	{
		return false;
	}
	// Expected: the wear of this hit (ApplyShotToEquipment on the real result), then the automatic chalking of the next shot.
	rb::human::TipState Worn = Fresh;
	rb::human::ApplyShotToEquipment(Shot->Request.Stroke.Executed, Shot->Result, 0, Shot->Request.Input.Balls[0].Orientation, Worn, nullptr);
	rb::human::TipState Chalked = Worn;
	double Seconds = 0.0;
	const int32 Twists = rb::human::PerformChalking(Chalked, rb::human::ChalkCube{}, rb::human::ChoreMode::Automatic, 0.0, 0.0, -1, Seconds);
	const rb::human::TipState& Tip = D->GetShooter(0).Tip;
	bool bWorn = false;
	bool bSameAsExpected = Tip.Hits == 1 && Twists >= 1;
	for (int32 Z = 0; Z < rb::human::kTipZoneCount; ++Z)
	{
		bWorn = bWorn || Worn.Coverage[Z] < Fresh.Coverage[Z];
		bSameAsExpected = bSameAsExpected && Tip.Coverage[Z] == Chalked.Coverage[Z];
	}
	TestTrue(TEXT("the hit wore the tip"), bWorn);
	TestTrue(TEXT("auto-chalk before the next shot == PerformChalking(worn tip) (bitwise)"), bSameAsExpected);
	TestEqual(TEXT("twists recorded"), D->GetShooter(0).LastChalkTwists, Twists);
	TestTrue(TEXT("the next stroke context carries the chalked tip"), D->MakeStrokeContext().Tip.Coverage[0] == Tip.Coverage[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchReplayAllowed, "RawBreak.Unit.Match.ReplayAllowed", RB_UNIT_TEST_FLAGS)
bool FRbMatchReplayAllowed::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> Idle(NewObject<URbMatchDirector>(GetTransientPackage()));
	TestFalse(TEXT("Idle"), Idle->IsReplayAllowed());

	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 91);
	if (!D.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("AwaitPlacement"), D->GetPhase() == ERbDirectorPhase::AwaitPlacement && D->IsReplayAllowed());
	D->PlaceCueBall(rb::Vec2(D->GetMatchConfig().Table.HeadStringX - 0.2, 0.0));
	TestTrue(TEXT("AwaitStroke"), D->GetPhase() == ERbDirectorPhase::AwaitStroke && D->IsReplayAllowed());

	TMap<ERbDirectorPhase, bool> Seen;
	const FDelegateHandle Handle = D->OnMatchChanged.AddLambda([&Seen, Director = D.Get()] { Seen.Add(Director->GetPhase(), Director->IsReplayAllowed()); });
	TestTrue(TEXT("break"), ShootAt(*D, rb::XY(D->GetTableState().Balls[1].State.Position), 9.0, -0.1));
	D->OnMatchChanged.Remove(Handle);
	TestTrue(TEXT("passed through Simulating"), Seen.Contains(ERbDirectorPhase::Simulating));
	TestFalse(TEXT("Simulating: no replay"), Seen.FindRef(ERbDirectorPhase::Simulating));

	// A replay locks the table: no placement, stroke or option while it runs; afterwards the turn continues.
	D->SetReplayActive(true);
	TestFalse(TEXT("no stroke during a replay"), D->SubmitScriptedStrike(1.0, 0.0, 0.0, 0.0, 0.0));
	TestFalse(TEXT("no placement during a replay"), D->PlaceCueBall(rb::Vec2(0.0, 0.0)));
	D->SetReplayActive(false);
	TestTrue(TEXT("after the replay the phase is unchanged"), D->GetPhase() == ExpectedPhase(*D));

	// Lag (hot-seat ?Lag=1): no replay while the players lag.
	TStrongObjectPtr<URbMatchDirector> L = MakeDirector(*this, ERbMatchMode::HotSeat, 92, 5, true);
	if (L.IsValid())
	{
		TestTrue(TEXT("Lag: no replay"), L->GetPhase() == ERbDirectorPhase::Lag && !L->IsReplayAllowed());
		// A new match started during the lag leaves no lag state behind (break ball in hand, rack in the table state).
		FRbMatchSetup Setup;
		Setup.Mode = ERbMatchMode::HotSeat;
		Setup.Seed = 93;
		TestTrue(TEXT("new match from the lag"), L->StartMatch(Setup));
		TestTrue(TEXT("-> AwaitPlacement with the cue ball in hand"), L->GetPhase() == ERbDirectorPhase::AwaitPlacement && L->IsCueBallInHand() &&
			L->GetLagStroker() == -1 && !L->GetTableState().Balls[0].InPlay);
		CheckTableStateSync(*this, *L, TEXT("new match after the lag"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchAssistedInputMode, "RawBreak.Unit.Match.AssistedInputMode", RB_UNIT_TEST_FLAGS)
bool FRbMatchAssistedInputMode::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 101, 5, false, 0.0);
	if (!D.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("InputMode::Assisted"), D->GetMatchConfig().Rules.Input == rb::rules::InputMode::Assisted);
	// In Sim mode an illegal placement would be accepted and evaluated as foul 3.10; Assisted refuses it before the stroke.
	rb::rules::MatchConfig SimConfig = D->GetMatchConfig();
	SimConfig.Rules.Input = rb::rules::InputMode::Sim;
	const rb::Vec2 Illegal(0.3, 0.0);
	rb::rules::ShotDeclaration Break;
	Break.Kind = rb::rules::ShotKind::Break;
	TestTrue(TEXT("Sim mode would accept the illegal placement"),
		rb::rules::ValidateDeclaration(SimConfig, D->GetMatchState(), Break, &Illegal) == rb::ErrorCode::Ok);
	TestFalse(TEXT("Assisted: refused"), D->PlaceCueBall(Illegal));

	// A stroke with the butt low next to a ball: the human layer may list shaft-contact candidates, the rules never see them.
	SetLayout(*D, {{0, rb::Vec2(-0.3, 0.0)}, {1, rb::Vec2(-0.3 - 2.0 * Radius(*D, 0) - 0.01, 0.0)}, {9, rb::Vec2(0.5, 0.3)}});
	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = 0.0; // the 1 lies right behind the cue ball, under the cue
	Commit.Intended.Speed = 1.0;
	TestTrue(TEXT("stroke"), D->SubmitStroke(Commit));
	const TSharedPtr<const FRbShot> Shot = D->GetLastCommittedShot();
	TestTrue(TEXT("NonTipContacts stay empty (R-11)"), Shot.IsValid() && Shot->Request.Input.Context.NonTipContacts.Size() == 0);
	AddInfo(FString::Printf(TEXT("shaft-contact candidates of the executed pose: %d"),
		Shot.IsValid() ? Shot->Request.Stroke.Executed.ShaftContactCandidates.Size() : -1));
	TestFalse(TEXT("no touched-ball foul from the body-less player"), D->GetLastShot().Fouls.Has(rb::rules::Foul::TouchedBall));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchPushOutDecision, "RawBreak.Unit.Match.PushOutDecision", RB_UNIT_TEST_FLAGS)
bool FRbMatchPushOutDecision::RunTest(const FString& Parameters)
{
	// The first seed whose scripted break is legal (no foul, three-ball rule met, the 9 not down) - deterministic.
	TStrongObjectPtr<URbMatchDirector> D;
	for (int64 Seed = 1; Seed <= 30 && !D.IsValid(); ++Seed)
	{
		TStrongObjectPtr<URbMatchDirector> Candidate = MakeDirector(*this, ERbMatchMode::HotSeat, Seed);
		if (Candidate.IsValid() && Break(*this, *Candidate) && Candidate->GetPhase() == ERbDirectorPhase::AwaitStroke &&
			Candidate->GetConstraints().PushOutAllowed)
		{
			AddInfo(FString::Printf(TEXT("legal break with seed %lld: %s"), Seed, *Describe(*Candidate)));
			D = Candidate;
		}
	}
	if (!TestTrue(TEXT("a legal break among seeds 1..30"), D.IsValid()))
	{
		return false;
	}
	const int32 Pusher = D->GetMatchState().Game.Shooter;
	D->SetShotKind(rb::rules::ShotKind::PushOut);
	const rb::Vec3 Cue = D->GetTableState().Balls[0].State.Position;
	TestTrue(TEXT("soft push-out toward the table centre"), D->SubmitScriptedStrike(0.35, FMath::Atan2(-Cue.y, -Cue.x), 0.0, 0.0, 0.0));
	AddInfo(Describe(*D));
	if (!TestEqual(TEXT("foul-free push-out -> the other player decides"), D->GetPhase(), ERbDirectorPhase::AwaitDecision))
	{
		return false;
	}
	const rb::rules::MatchState& S = D->GetMatchState();
	TestEqual(TEXT("decider"), S.Decider, 1 - Pusher);
	TestTrue(TEXT("options: shoot / pass back"), S.PendingOutcome.Options.Size() == 2 && S.PendingOutcome.Options[0] == rb::rules::Option::ShootFromPosition &&
		S.PendingOutcome.Options[1] == rb::rules::Option::PassBack);
	TestTrue(TEXT("no stroke while deciding"), !D->SubmitScriptedStrike(1.0, 0.0, 0.0, 0.0, 0.0));
	TestTrue(TEXT("CycleOption"), D->CycleOption(1) && D->GetSelectedOption() == 1);
	TestTrue(TEXT("CycleOption wraps"), D->CycleOption(1) && D->GetSelectedOption() == 0 && D->CycleOption(-1) && D->GetSelectedOption() == 1);
	TestTrue(TEXT("Confirm = pass back"), D->Confirm());
	TestEqual(TEXT("the pusher must shoot"), D->GetMatchState().Game.Shooter, Pusher);
	TestEqual(TEXT("from position"), D->GetPhase(), ERbDirectorPhase::AwaitStroke);
	TestFalse(TEXT("no second push-out"), D->GetConstraints().PushOutAllowed);
	CheckTableStateSync(*this, *D, TEXT("after the decision"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchLag, "RawBreak.Unit.Match.Lag", RB_UNIT_TEST_FLAGS)
bool FRbMatchLag::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 111, 5, true, 0.0);
	if (!D.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("-> Lag"), D->GetPhase(), ERbDirectorPhase::Lag);
	TestEqual(TEXT("player 1 lags first"), D->GetLagStroker(), 0);
	rb::Vec2 First;
	rb::Vec2 Second;
	rb::rules::LagStartPositions(D->GetMatchConfig().Table, First, Second);
	const FRbTableState& T = D->GetTableState();
	TestTrue(TEXT("lag balls at the start positions"), T.Balls[0].InPlay && T.Balls[1].InPlay && T.Balls[0].State.Position.x == First.x &&
		T.Balls[1].State.Position.y == Second.y);
	TestEqual(TEXT("the first lagger's context"), D->MakeStrokeContext().Key.ShooterId, 0u);

	FRbStrokeCommit Commit;
	Commit.Intended.Azimuth = 0.0;
	Commit.Intended.Speed = 1.9;
	TestTrue(TEXT("player 1 lags"), D->SubmitStroke(Commit));
	TestTrue(TEXT("still lagging, player 2 next"), D->GetPhase() == ERbDirectorPhase::Lag && D->GetLagStroker() == 1 && D->GetMatchShotIndex() == 0);
	TestEqual(TEXT("the second lagger's context"), D->MakeStrokeContext().Key.ShooterId, 1u);
	Commit.Intended.Speed = 1.5;
	TestTrue(TEXT("player 2 lags"), D->SubmitStroke(Commit));

	const TSharedPtr<const FRbShot> Shot = D->GetLastCommittedShot();
	if (!TestTrue(TEXT("lag simulated"), Shot.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("ONE SimInput with both strikes at t = 0"), Shot->Request.Input.Strikes.Size() == 2 && Shot->Request.Input.Strikes[0].Ball == 0 &&
		Shot->Request.Input.Strikes[1].Ball == 1);
	TestTrue(TEXT("NoiseScale 0: the strikes are the intended strokes"), Shot->Request.Input.Strikes[0].Input.Speed == 1.9 &&
		Shot->Request.Input.Strikes[1].Input.Speed == 1.5);
	TestTrue(TEXT("both laggers revealed their draws"), D->GetShooter(0).ShooterShotIndex == 1 && D->GetShooter(1).ShooterShotIndex == 1);
	const FRbLastShotSummary& Last = D->GetLastShot();
	AddInfo(FString::Printf(TEXT("lag winner %d, phase %d"), Last.LagWinner, static_cast<int32>(D->GetPhase())));
	TestTrue(TEXT("lag summary"), Last.bLag);
	if (Last.LagWinner >= 0)
	{
		TestTrue(TEXT("the winner breaks"), D->GetMatchState().Game.Shooter == Last.LagWinner && D->GetMatchState().RackNumber == 1 &&
			D->GetPhase() == ERbDirectorPhase::AwaitPlacement && D->GetMatchState().FirstBreaker == Last.LagWinner);
		CheckTableStateSync(*this, *D, TEXT("rack after the lag"));
	}
	else
	{
		TestTrue(TEXT("re-lag"), D->GetPhase() == ERbDirectorPhase::Lag && D->GetLagStroker() == 0 && D->GetMatchShotIndex() == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchRerack, "RawBreak.Unit.Match.Rerack", RB_UNIT_TEST_FLAGS)
bool FRbMatchRerack::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbMatchDirector> D = MakeDirector(*this, ERbMatchMode::HotSeat, 121);
	if (!D.IsValid() || !Break(*this, *D))
	{
		return false;
	}
	if (D->GetPhase() == ERbDirectorPhase::RackOver)
	{
		TestTrue(TEXT("re-rack in RackOver = next rack"), D->RequestRerack());
	}
	else
	{
		const int32 Wins = D->GetMatchState().RackWins[0] + D->GetMatchState().RackWins[1];
		TestTrue(TEXT("stalemate re-rack"), D->RequestRerack());
		TestTrue(TEXT("rack 2, no score, the rack's breaker breaks again"), D->GetMatchState().RackNumber == 2 &&
			D->GetMatchState().RackWins[0] + D->GetMatchState().RackWins[1] == Wins && D->GetMatchState().Game.Shooter == 0);
	}
	TestEqual(TEXT("break ball in hand"), D->GetPhase(), ERbDirectorPhase::AwaitPlacement);
	CheckTableStateSync(*this, *D, TEXT("re-racked"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbMatchOptions, "RawBreak.Unit.Match.Options", RB_UNIT_TEST_FLAGS)
bool FRbMatchOptions::RunTest(const FString& Parameters)
{
	FRbMatchSetup Setup;
	float Rate = 1.0f;
	TestTrue(TEXT("all valid"), ARbGameMode::ParseMatchOptions(
		TEXT("?Mode=HotSeat?Game=NineBall?Race=7?Lag=1?Seed=4242?Attr=80?Pressure=0?Noise=0.3?Rate=0?P1=Ann?P2=Bob"), Setup, &Rate));
	TestTrue(TEXT("mode"), Setup.Mode == ERbMatchMode::HotSeat);
	TestTrue(TEXT("game"), Setup.Discipline == ERbDiscipline::NineBall);
	TestEqual(TEXT("race"), Setup.RaceTo, 7);
	TestTrue(TEXT("lag"), Setup.bLag);
	TestEqual(TEXT("seed"), Setup.Seed, static_cast<int64>(4242));
	TestEqual(TEXT("attributes"), Setup.ShooterAttribute, 80.0);
	TestFalse(TEXT("pressure off"), Setup.bPressure);
	TestEqual(TEXT("noise"), Setup.NoiseScale, 0.3);
	TestEqual(TEXT("rate"), Rate, 0.0f);
	TestTrue(TEXT("names"), Setup.Player1 == TEXT("Ann") && Setup.Player2 == TEXT("Bob"));

	FRbMatchSetup Other;
	TestTrue(TEXT("aliases"), ARbGameMode::ParseMatchOptions(TEXT("?Mode=0?Game=10?Lag=false"), Other));
	TestTrue(TEXT("practice / 10-ball"), Other.Mode == ERbMatchMode::Practice && Other.Discipline == ERbDiscipline::TenBall && !Other.bLag);
	FRbMatchSetup Bad;
	TestFalse(TEXT("invalid values reported"), ARbGameMode::ParseMatchOptions(TEXT("?Mode=Solo?Race=0?Attr=150?Seed=1.5"), Bad));
	TestTrue(TEXT("invalid values leave the defaults"), Bad.Mode == ERbMatchMode::Practice && Bad.RaceTo == 5 && Bad.ShooterAttribute < 0.0 && Bad.Seed == 0);

	// ?Attr= reaches the shooters, ?Pressure=0 the situation.
	FString Error;
	TStrongObjectPtr<URbMatchDirector> D(NewObject<URbMatchDirector>(GetTransientPackage()));
	D->SetTableContext(FRbTableContext::Create(FRbTableSetup{}, Error));
	D->SetLivePlaybackRate(0.0f);
	TestTrue(TEXT("start"), D->StartMatch(Setup));
	TestTrue(TEXT("attributes 80"), D->GetShooter(0).Attributes.Steadiness == 80.0 && D->GetShooter(1).Attributes.SpinTouch == 80.0);
	TestEqual(TEXT("pressure off"), D->MakeStrokeContext().Situation.Pressure, 0.0);
	TestEqual(TEXT("noise scale"), D->GetHumanParams().NoiseScale, 0.3);
	TestEqual(TEXT("lag"), D->GetPhase(), ERbDirectorPhase::Lag);
	TestEqual(TEXT("player names"), D->GetShooter(1).Name, FString(TEXT("Bob")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
