#pragma once

// Owner: WP-9 (rules table procedures & match). Helpers for the Rules/Table and Rules/Match tests:
// hand-built tables, states, racks and outcomes (rules.md 17 conventions: 9-ft table, R = 0.028575 m).

#include "rb/Math/Vec2.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/TableRules.h"

#include <initializer_list>

// Nested in rb::rules so that unqualified names resolve without using-directives in a header.
namespace rb::rules::testhelp
{
	inline constexpr double kR = 0.028575;
	inline constexpr double kSqrt3R = 1.7320508075688772935 * kR; // 0.049493352 m

	inline RulesTable NineFootTable() { return MakeRulesTable(2.54, 1.27, kR); }

	// 9-ft table with hand-made pocket openings (corner mouth 4.5 in, side mouth 5 in; jaw points on the
	// nose lines, JawPoint[0] = Incoming jaw, [1] = Outgoing, rules.md 2.3 numbering). Drop-edge circles:
	// corner radius 0.060 m centered 0.02 m out along the axis, side radius 0.070 m centered 0.03 m behind
	// the nose line (so they reach 0.04 m into the playing surface).
	inline RulesTable NineFootTableWithPockets()
	{
		RulesTable T = NineFootTable();
		const double L2 = 0.5 * T.Length;
		const double W2 = 0.5 * T.Width;
		const double A = 0.1143 / 1.4142135623730951; // corner jaw offset along each nose line
		const double S = 0.0635;                      // half side mouth
		const double D = 0.7071067811865476;
		T.PocketCount = kPocketCount;
		T.Pockets[0] = {{Vec2{-L2, -W2 + A}, Vec2{-L2 + A, -W2}}, Vec2{-D, -D}, Vec2{-L2 - 0.02 * D, -W2 - 0.02 * D}, 0.060};
		T.Pockets[1] = {{Vec2{-S, -W2}, Vec2{S, -W2}}, Vec2{0.0, -1.0}, Vec2{0.0, -W2 - 0.03}, 0.070};
		T.Pockets[2] = {{Vec2{L2 - A, -W2}, Vec2{L2, -W2 + A}}, Vec2{D, -D}, Vec2{L2 + 0.02 * D, -W2 - 0.02 * D}, 0.060};
		T.Pockets[3] = {{Vec2{L2, W2 - A}, Vec2{L2 - A, W2}}, Vec2{D, D}, Vec2{L2 + 0.02 * D, W2 + 0.02 * D}, 0.060};
		T.Pockets[4] = {{Vec2{S, W2}, Vec2{-S, W2}}, Vec2{0.0, 1.0}, Vec2{0.0, W2 + 0.03}, 0.070};
		T.Pockets[5] = {{Vec2{-L2 + A, W2}, Vec2{-L2, W2 - A}}, Vec2{-D, D}, Vec2{-L2 - 0.02 * D, W2 + 0.02 * D}, 0.060};
		return T;
	}

	inline GameState EmptyState(Discipline Game)
	{
		GameState S;
		S.Game = Game;
		for (BallStatus& B : S.Balls)
		{
			B = BallStatus{};
		}
		return S;
	}

	inline void Place(GameState& S, int Ball, double X, double Y)
	{
		S.Balls[Ball].Kind = BallStatusKind::OnTable;
		S.Balls[Ball].Position = {X, Y};
	}

	// rules.md 5.1 frozen lattice: center(r, k) = (x_A + r sqrt3 R, (2k - n_r + 1) R).
	inline Vec2 LatticeSite(int Row, int Index, int RowCount, double ApexX) { return {ApexX + Row * kSqrt3R, (2 * Index - RowCount + 1) * kR}; }

	// A frozen hand-built rack of the balls First..Last on the 15-ball triangle (apex on the foot spot),
	// sites filled in lattice order, the apex left empty when ApexEmpty. Positions only matter for tests
	// that look at them; match-flow tests only need "racked".
	inline RackAssignment HandRack(int First, int Last, bool ApexEmpty = false)
	{
		RackAssignment Rack;
		int Ball = First;
		int Site = 0;
		for (int Row = 0; Row < 5; ++Row)
		{
			for (int k = 0; k <= Row; ++k, ++Site)
			{
				if ((ApexEmpty && Site == 0) || Ball > Last)
				{
					continue;
				}
				Rack.BallAtSite[Site] = static_cast<BallId>(Ball);
				Rack.Racked[Ball] = true;
				Rack.Position[Ball] = LatticeSite(Row, k, Row + 1, 0.635);
				++Ball;
			}
		}
		Rack.SiteCount = 15;
		return Rack;
	}

	inline int LastBallOf(Discipline Game)
	{
		switch (Game)
		{
		case Discipline::NineBall: return 9;
		case Discipline::TenBall: return 10;
		default: return 15;
		}
	}

	inline MatchConfig MakeMatchConfig(Discipline Game, int RaceTo = 7)
	{
		MatchConfig C;
		C.Game = Game;
		C.RaceTo = RaceTo;
		C.Table = NineFootTable();
		C.RackGaps = kRackGapNone;
		C.Seed = 1234;
		return C;
	}

	// Setup -> lag won by Winner (0 = First) -> Winner lets Breaker break -> first rack (hand-built) -> AwaitShot.
	inline bool StartToFirstBreak(const MatchConfig& Config, MatchState& State, int Breaker, int LagWinner = 0)
	{
		StartMatch(Config, State);
		LagResult Lag;
		Lag.Outcome = LagWinner == 0 ? LagOutcome::FirstWins : LagOutcome::SecondWins;
		if (ApplyLagResult(Config, State, Lag) != ErrorCode::Ok) return false;
		if (ChooseBreaker(Config, State, Breaker) != ErrorCode::Ok) return false;
		return SetupRackWith(Config, State, HandRack(1, LastBallOf(Config.Game))) == ErrorCode::Ok;
	}

	inline ShotOutcome ContinueOutcome(int Shooter)
	{
		ShotOutcome O;
		O.Next = NextAction::Continue;
		O.NextShooter = Shooter;
		O.NextCueBall = CueBallNext::InPosition;
		return O;
	}

	inline ShotOutcome PassOutcome(int NextShooter, CueBallNext Cue = CueBallNext::InPosition)
	{
		ShotOutcome O;
		O.Next = NextAction::Pass;
		O.NextShooter = NextShooter;
		O.NextCueBall = Cue;
		return O;
	}

	inline ShotOutcome DecideOutcome(int Decider, std::initializer_list<Option> Options)
	{
		ShotOutcome O;
		O.Next = NextAction::AwaitDecision;
		O.NextShooter = Decider;
		for (const Option Opt : Options)
		{
			O.Options.PushBack(Opt);
		}
		return O;
	}

	inline ShotOutcome RackWonOutcome(int Winner)
	{
		ShotOutcome O;
		O.Next = NextAction::RackWon;
		O.Winner = Winner;
		return O;
	}

	// Hand-built facts: Ball comes to rest on the table at (X, Y).
	inline void FactsRestAt(ShotFacts& F, int Ball, double X, double Y)
	{
		F.Balls[Ball].EndStatus = BallEndStatus::OnTable;
		F.Balls[Ball].FinalPosition = {X, Y};
	}

	// Hand-built facts: Ball pocketed in P (F4 list, per-ball summary, CueBallPocketed / AnyObjectBallPocketed).
	inline void FactsPocketed(ShotFacts& F, int Ball, PocketId P)
	{
		PocketedBall B;
		B.Ball = static_cast<BallId>(Ball);
		B.Pocket = P;
		F.Pocketed.PushBack(B);
		F.Balls[Ball].Pocketed = true;
		F.Balls[Ball].Pocket = P;
		F.Balls[Ball].EndStatus = BallEndStatus::Pocketed;
		if (Ball == kCueBallId)
		{
			F.CueBallPocketed = true;
		}
		else
		{
			F.AnyObjectBallPocketed = true;
		}
	}

	// Hand-built facts: object ball Ball driven off the table (F5).
	inline void FactsOffTable(ShotFacts& F, int Ball)
	{
		F.ObjectBallsOffTable |= 1u << static_cast<unsigned>(Ball);
		F.Balls[Ball].OffTable = true;
		F.Balls[Ball].EndStatus = BallEndStatus::OffTable;
	}

	// A standard foul outcome in the shape of rules.md 10.4 / 10.5: turn to the opponent, cue ball in hand
	// anywhere, the shooter's counter = FoulsAfter.
	inline ShotOutcome StandardFoul(int Shooter, int FoulsAfter, Foul Kind = Foul::WrongBallFirst, CueBallNext Cue = CueBallNext::InHandAnywhere)
	{
		ShotOutcome O = PassOutcome(1 - Shooter, Cue);
		O.AnyFoul = true;
		O.Detected.Add(Kind);
		O.Enforced = Kind;
		O.FoulsAfter[Shooter] = FoulsAfter;
		return O;
	}

	// Plays the current rack to its end with Winner winning it (one shot), then sets up the next rack by hand.
	inline bool WinRack(const MatchConfig& Config, MatchState& State, int Winner)
	{
		const ShotFacts NoFacts;
		if (ApplyShot(Config, State, RackWonOutcome(Winner), NoFacts) != ErrorCode::Ok) return false;
		if (State.Phase == MatchPhase::MatchOver) return true;
		return SetupRackWith(Config, State, HandRack(1, LastBallOf(Config.Game))) == ErrorCode::Ok;
	}
}
