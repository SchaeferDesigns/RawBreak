#include "rb/Core/FpGuard.h"
// Owner: WP-12 (AI opponent). The planner's static evaluator (Docs/architecture.md 7.6). All model constants TUNING.
#include "rb/Ai/PositionEval.h"

#include "rb/Math/Scalar.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/TableRules.h"

#include <initializer_list>

namespace rb::ai
{
	namespace
	{
		constexpr double kCornerFullAngle = 45.0 * kDegToRad;  // corner pockets: full window up to this approach angle ...
		constexpr double kCornerZeroAngle = 78.0 * kDegToRad;  // ... falling linearly to 0 here
		constexpr double kSideCosZero = 0.34;                  // side pockets: window ~ (cos(approach) - 0.34) / 0.66
		constexpr double kMinWindow = 0.004;                   // [m] smallest full window (tight pockets)
		constexpr double kAimDepth = 0.25;                     // aim point: mouth midpoint + 0.25 R along the axis
		constexpr double kPathTolerance = 0.001;               // [m] a gap below this counts as a touch
		constexpr double kMaxStaticCut = 85.0 * kDegToRad;
		constexpr double kSafetyLeave = 0.5;                   // the opponent's first-shot chance after a safety: 0.5 (1 - SafetyQuality) ...
		constexpr double kHookedLeave = 0.3;                   // ... + 0.3 (1 - visibility) when the mover is hooked himself
		constexpr double kSafetyFoul = 0.3;                    // the opponent's foul chance after a safety: 0.3 SafetyQuality (1 - KickSkill)
		constexpr double kTwoWayShare = 1.0;                   // share of the pot chance a safety keeps (two-way shots)
		constexpr int kMaxTurnBalls = 15;
		constexpr int kValueHorizon = 3;                       // balls the turn model looks ahead (players plan a few balls, not the rack)
		constexpr int kVisibilityRays = 5;
		constexpr int kMaxOptionDepth = 2;
		constexpr int kPointsHorizon = 8;                      // 14.1: points the mover races for (relative horizon, TUNING)
		constexpr double kPointsLead = 6.0;                    // 14.1: the lead's weight saturates at this many points (TUNING)

		bool HasBit(std::uint32_t Mask, int Bit) { return ((Mask >> Bit) & 1u) != 0u; }

		int ClampInt(int V, int Lo, int Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

		constexpr std::uint32_t Bit(int Ball) { return 1u << Ball; }

		Vec2 Rotate(const Vec2& V, double Angle)
		{
			const double C = Cos(Angle);
			const double S = Sin(Angle);
			return {V.x * C - V.y * S, V.x * S + V.y * C};
		}

		// The turn model (DERIVED; the rates TUNING): the player at the table makes each next ball with probability q (from a typical
		// position) and keeps shooting; a miss gives the other player the table at a typical position; potting the last ball wins.
		// Me(i, j) = P(the mover wins | he needs i balls, the other j), Other(j, i) the same for the other player to move. With
		// a = Me(i - 1, j), b = Other(j - 1, i) (1 when the count reaches 0) the two equations of one state solve to
		//   Me(i, j) = (q_M a + (1 - q_M) q_O (1 - b)) / (1 - (1 - q_M)(1 - q_O)),  Other(j, i) = (q_O b + (1 - q_O) q_M (1 - a)) / (...).
		// Rotation games (9/10-ball) share one count: both indices are the balls on the table.
		struct TurnModel
		{
			bool Shared = false;
			double MeTable[kMaxTurnBalls + 1][kMaxTurnBalls + 1] = {};    // [i][j]
			double OtherTable[kMaxTurnBalls + 1][kMaxTurnBalls + 1] = {}; // [j][i]

			static int Cap(int N) { return N < 0 ? 0 : (N > kMaxTurnBalls ? kMaxTurnBalls : N); }

			TurnModel(double QMe, double QOther, bool InShared) : Shared(InShared)
			{
				const double Den = 1.0 - (1.0 - QMe) * (1.0 - QOther);
				for (int k = 0; k <= kMaxTurnBalls; ++k)
				{
					MeTable[0][k] = 1.0;
					OtherTable[0][k] = 1.0;
				}
				for (int Sum = 2; Sum <= 2 * kMaxTurnBalls; ++Sum)
				{
					for (int i = 1; i <= kMaxTurnBalls; ++i)
					{
						const int j = Sum - i;
						if (j < 1 || j > kMaxTurnBalls)
						{
							continue;
						}
						const double A = MeTable[i - 1][Shared ? i - 1 : j];
						const double B = OtherTable[j - 1][Shared ? j - 1 : i];
						MeTable[i][j] = Den > 1e-12 ? (QMe * A + (1.0 - QMe) * QOther * (1.0 - B)) / Den : 0.5;
						OtherTable[j][i] = Den > 1e-12 ? (QOther * B + (1.0 - QOther) * QMe * (1.0 - A)) / Den : 0.5;
					}
				}
			}

			// The mover needs I, the other J (rotation: I == J, the balls on the table).
			double Me(int I, int J) const
			{
				if (I <= 0)
				{
					return 1.0;
				}
				return MeTable[Cap(I)][Cap(Shared ? I : J)];
			}

			double Other(int J, int I) const
			{
				if (J <= 0)
				{
					return 1.0;
				}
				return OtherTable[Cap(J)][Cap(Shared ? J : I)];
			}
		};

		bool RotationGame(rules::Discipline Game) { return Game == rules::Discipline::NineBall || Game == rules::Discipline::TenBall; }

		bool ThreeFoulsLose(const rules::RulesConfig& Config, rules::Discipline Game)
		{
			return Config.ThreeFoulRule && (RotationGame(Game) || Game == rules::Discipline::StraightPool);
		}

		int FullRackBalls(rules::Discipline Game)
		{
			switch (Game)
			{
			case rules::Discipline::NineBall: return 9;
			case rules::Discipline::TenBall: return 10;
			case rules::Discipline::StraightPool: return 14;
			case rules::Discipline::EightBall:
			case rules::Discipline::Blackball: break;
			}
			return 8;
		}

		// Best direct pot from cue position Cue on the legal balls of Legal (ghost-ball geometry, obstruction, pocket windows).
		struct BestPot
		{
			double Chance = 0.0;
			BallId Ball = kNoBall;
			PocketId Pocket = PocketId::None;
			Vec2 Ghost;
		};

		BestPot BestDirectPot(const EvalContext& Context, const BallLayout& Layout, const Vec2& Cue, double CueRadius, std::uint32_t Legal, double Sigma)
		{
			const TableGeometry& T = *Context.Table;
			BestPot Best;
			for (int b = 1; b < rules::kRulesBallCount; ++b)
			{
				if (!HasBit(Legal, b) || !HasBit(Layout.OnTable, b))
				{
					continue;
				}
				const Vec2 Object = Layout.Position[b];
				const double Ro = Layout.Radius[b];
				for (int p = 0; p < T.Pockets.Size(); ++p)
				{
					const PocketAim Aim = PocketAimFor(T, p, Object, Ro);
					if (!Aim.Valid)
					{
						continue;
					}
					const ShotGeometry Shot = CutGeometry(Cue, CueRadius, Object, Ro, Aim.Point, kMaxStaticCut);
					if (!Shot.Feasible)
					{
						continue;
					}
					const double Chance = PotChance(Shot, Aim.HalfWindow, CueRadius + Ro, Sigma);
					if (!(Chance > Best.Chance))
					{
						continue;
					}
					if (!PathClear(Layout, Cue, Shot.Ghost, CueRadius, Bit(0) | Bit(b)) || !PathClear(Layout, Object, Aim.Point, Ro, Bit(0) | Bit(b)))
					{
						continue;
					}
					Best.Chance = Chance;
					Best.Ball = static_cast<BallId>(b);
					Best.Pocket = static_cast<PocketId>(p);
					Best.Ghost = Shot.Ghost;
				}
			}
			return Best;
		}

		// Share of straight contact lines (full to thin on both sides) from Cue to any legal ball that no other ball blocks.
		double Visibility(const BallLayout& Layout, const Vec2& Cue, double CueRadius, std::uint32_t Legal)
		{
			double Best = 0.0;
			for (int b = 1; b < rules::kRulesBallCount; ++b)
			{
				if (!HasBit(Legal, b) || !HasBit(Layout.OnTable, b))
				{
					continue;
				}
				const Vec2 Object = Layout.Position[b];
				const double Sum = CueRadius + Layout.Radius[b];
				const Vec2 Dir = Normalized(Object - Cue);
				if (LengthSquared(Dir) == 0.0)
				{
					continue;
				}
				const Vec2 Side = PerpCcw(Dir);
				int Open = 0;
				for (int k = 0; k < kVisibilityRays; ++k)
				{
					const double F = -0.9 + 1.8 * static_cast<double>(k) / static_cast<double>(kVisibilityRays - 1);
					// Cue-ball centre at contact on the line Cue -> Object + Side F Sum (lateral offset F Sum from the object).
					const Vec2 Contact = Object + Side * (F * Sum) - Dir * Sqrt(Max(0.0, 1.0 - F * F)) * Sum;
					Open += PathClear(Layout, Cue, Contact, CueRadius, Bit(0) | Bit(b)) ? 1 : 0;
				}
				Best = Max(Best, static_cast<double>(Open) / kVisibilityRays);
			}
			return Best;
		}

		rules::GameState WithShooter(const rules::GameState& State, int Player)
		{
			rules::GameState G = State;
			G.Shooter = Player;
			return G;
		}

		std::uint32_t LegalMaskOf(const EvalContext& Context, const rules::GameState& State)
		{
			rules::ShotDeclaration Normal;
			Normal.Kind = rules::ShotKind::Normal;
			return rules::LegalFirstContactMask(Context.Match->Rules, State, Normal);
		}

		double StateValueAt(const EvalContext& Context, const rules::MatchState& State, int Player, int Depth);
	}

	double Erf(double X)
	{
		// Abramowitz-Stegun 7.1.26.
		const double S = X < 0.0 ? -1.0 : 1.0;
		const double A = Abs(X);
		const double T = 1.0 / (1.0 + 0.3275911 * A);
		const double Y = 1.0 - (((((1.061405429 * T - 1.453152027) * T) + 1.421413741) * T - 0.284496736) * T + 0.254829592) * T * Exp(-A * A);
		return S * Y;
	}

	PocketAim PocketAimFor(const TableGeometry& Table, int Pocket, const Vec2& Object, double Radius)
	{
		PocketAim Out;
		if (Pocket < 0 || Pocket >= Table.Pockets.Size())
		{
			return Out;
		}
		const PocketGeometry& P = Table.Pockets[Pocket];
		Out.Point = P.MouthMid + P.Axis * (kAimDepth * Radius);
		const Vec2 D = Out.Point - Object;
		const double Dist = Length(D);
		const double Cos = Dist > 1e-12 ? Clamp(Dot(D, P.Axis) / Dist, -1.0, 1.0) : 1.0;
		Out.Approach = Acos(Cos);
		const double H0 = Max(kMinWindow, 0.5 * P.Mouth - Radius);
		if (P.Kind == PocketKind::Corner)
		{
			Out.HalfWindow = Out.Approach <= kCornerFullAngle ? H0 : H0 * Max(0.0, (kCornerZeroAngle - Out.Approach) / (kCornerZeroAngle - kCornerFullAngle));
		}
		else
		{
			Out.HalfWindow = H0 * Max(0.0, (Cos - kSideCosZero) / (1.0 - kSideCosZero));
		}
		Out.Valid = Out.HalfWindow > 0.0;
		return Out;
	}

	BallLayout MakeBallLayout(const rules::GameState& State, const rules::RulesTable& Table)
	{
		BallLayout L;
		for (int b = 0; b < rules::kRulesBallCount; ++b)
		{
			if (State.Balls[b].Kind == rules::BallStatusKind::OnTable)
			{
				L.Position[b] = State.Balls[b].Position;
				L.Radius[b] = Table.BallRadius[b];
				L.OnTable |= Bit(b);
			}
		}
		return L;
	}

	bool PathClear(const BallLayout& Layout, const Vec2& A, const Vec2& B, double Radius, std::uint32_t ExcludeMask)
	{
		const Vec2 AB = B - A;
		const double Len2 = LengthSquared(AB);
		const std::uint32_t Check = Layout.OnTable & ~ExcludeMask;
		for (int j = 0; j < kMaxBalls; ++j)
		{
			if (!HasBit(Check, j))
			{
				continue;
			}
			const Vec2 P = Layout.Position[j];
			const double T = Len2 > 0.0 ? Clamp(Dot(P - A, AB) / Len2, 0.0, 1.0) : 0.0;
			const Vec2 Closest = A + AB * T;
			const double Reach = Radius + Layout.Radius[j] + kPathTolerance;
			if (LengthSquared(P - Closest) < Reach * Reach)
			{
				return false;
			}
		}
		return true;
	}

	ShotGeometry CutGeometry(const Vec2& Cue, double CueRadius, const Vec2& Object, double ObjectRadius, const Vec2& Target, double MaxCut)
	{
		ShotGeometry S;
		const Vec2 ToTarget = Target - Object;
		S.ObjectDistance = Length(ToTarget);
		if (!(S.ObjectDistance > 1e-9))
		{
			return S;
		}
		const Vec2 U = ToTarget / S.ObjectDistance;
		const double Sum = CueRadius + ObjectRadius;
		S.Ghost = Object - U * Sum;
		const Vec2 V = S.Ghost - Cue;
		S.CueDistance = Length(V);
		if (!(S.CueDistance > 1e-9) || LengthSquared(Object - Cue) <= Sum * Sum)
		{
			return S;
		}
		const Vec2 Dir = V / S.CueDistance;
		S.Cut = Atan2(Cross(Dir, U), Dot(Dir, U));
		S.Azimuth = Atan2(Dir.y, Dir.x);
		S.Feasible = Abs(S.Cut) <= MaxCut;
		return S;
	}

	double PotChance(const ShotGeometry& Shot, double HalfWindow, double RadiusSum, double AimSigma, double WindowScale)
	{
		if (!Shot.Feasible || !(HalfWindow > 0.0))
		{
			return 0.0;
		}
		const double W = Atan(HalfWindow * WindowScale / Max(Shot.ObjectDistance, 0.01));
		const double Allowed = W * RadiusSum * Cos(Shot.Cut) / Max(Shot.CueDistance, 0.05);
		if (!(AimSigma > 0.0))
		{
			return Allowed > 0.0 ? 1.0 : 0.0;
		}
		return Clamp(Erf(Allowed / (AimSigma * 1.4142135623730951)), 0.0, 1.0);
	}

	NextShotInfo BestNextShot(const EvalContext& Context, const rules::GameState& State, int Player)
	{
		NextShotInfo Info;
		if (Context.Table == nullptr || Context.Match == nullptr || Player < 0 || Player > 1)
		{
			return Info;
		}
		const rules::GameState G = WithShooter(State, Player);
		const rules::RulesTable& RT = Context.Match->Table;
		const EvalPlayer& Me = Context.Players[Player];
		const BallLayout Layout = MakeBallLayout(G, RT);
		const std::uint32_t Legal = LegalMaskOf(Context, G);
		const double Rc = RT.BallRadius[0];
		Info.InHand = G.CueBall != rules::CueBallNext::InPosition || G.Balls[0].Kind != rules::BallStatusKind::OnTable;

		BestPot Best;
		if (!Info.InHand)
		{
			Info.CuePosition = G.Balls[0].Position;
			Best = BestDirectPot(Context, Layout, Info.CuePosition, Rc, Legal, Me.AimSigma);
			Info.Visibility = Visibility(Layout, Info.CuePosition, Rc, Legal);
		}
		else
		{
			// Free placement: behind the ghost ball of every clear pot (straight and 20 deg off, 25 and 45 cm), plus a coarse grid of
			// the region (kitchen / baulk), each checked with CueBallPlacementLegal.
			Info.Visibility = 1.0;
			const TableGeometry& T = *Context.Table;
			const RulesTolerances& Tol = Context.Match->Rules.Tolerances;
			const auto TryPlacement = [&](const Vec2& P) {
				if (!rules::CueBallPlacementLegal(G, P, G.CueBall, RT, Tol))
				{
					return;
				}
				const BestPot From = BestDirectPot(Context, Layout, P, Rc, Legal, Me.AimSigma);
				if (From.Chance > Best.Chance || Best.Ball == kNoBall)
				{
					Best = From;
					Info.CuePosition = P;
				}
			};
			for (int b = 1; b < rules::kRulesBallCount; ++b)
			{
				if (!HasBit(Legal, b) || !HasBit(Layout.OnTable, b))
				{
					continue;
				}
				for (int p = 0; p < T.Pockets.Size(); ++p)
				{
					const PocketAim Aim = PocketAimFor(T, p, Layout.Position[b], Layout.Radius[b]);
					if (!Aim.Valid || !PathClear(Layout, Layout.Position[b], Aim.Point, Layout.Radius[b], Bit(0) | Bit(b)))
					{
						continue;
					}
					const Vec2 U = Normalized(Aim.Point - Layout.Position[b]);
					const Vec2 Ghost = Layout.Position[b] - U * (Rc + Layout.Radius[b]);
					for (const double Angle : {0.0, 20.0 * kDegToRad, -20.0 * kDegToRad})
					{
						for (const double Distance : {0.25, 0.45})
						{
							TryPlacement(Ghost - Rotate(U, Angle) * Distance);
						}
					}
				}
			}
			const double HalfL = T.HalfLength - Rc - 0.02;
			const double HalfW = T.HalfWidth - Rc - 0.02;
			const double XMax = G.CueBall == rules::CueBallNext::InHandAboveHeadString ? RT.HeadStringX - 0.02
				: (G.CueBall == rules::CueBallNext::InHandBaulk ? RT.BaulkX - 0.02 : HalfL);
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					const double X = -HalfL + (XMax + HalfL) * (0.2 + 0.3 * i);
					const double Y = HalfW * (-0.6 + 0.6 * j);
					TryPlacement({X, Y});
				}
			}
		}
		Info.PotChance = Best.Chance;
		Info.Ball = Best.Ball;
		Info.Pocket = Best.Pocket;

		if (Me.PositionDepth >= 2 && Best.Ball != kNoBall)
		{
			// Two-shot pattern: the best shot on the ball after it from the ghost-ball position (a proxy of where the cue ball stops).
			rules::GameState After = G;
			After.Balls[Best.Ball].Kind = rules::BallStatusKind::Pocketed;
			After.Balls[0].Kind = rules::BallStatusKind::OnTable;
			After.Balls[0].Position = Best.Ghost;
			After.CueBall = rules::CueBallNext::InPosition;
			After.IsBreakShot = false;
			if (G.Game == rules::Discipline::EightBall && After.Players[Player].Group == rules::BallGroup::None && Best.Ball != 8)
			{
				After.Players[Player].Group = rules::GroupOf(Best.Ball);
				After.Players[1 - Player].Group = After.Players[Player].Group == rules::BallGroup::Solids ? rules::BallGroup::Stripes : rules::BallGroup::Solids;
				After.TableOpen = false;
			}
			const std::uint32_t NextLegal = LegalMaskOf(Context, After);
			if ((NextLegal & ~Bit(0)) == 0u || rules::CountObjectBallsOnTable(After) == 0)
			{
				Info.Pattern = 1.0;
			}
			else
			{
				const BallLayout LayoutAfter = MakeBallLayout(After, RT);
				Info.Pattern = BestDirectPot(Context, LayoutAfter, Best.Ghost, Rc, NextLegal, Me.AimSigma).Chance;
			}
		}
		return Info;
	}

	int BallsToWin(const rules::GameState& State, int Player, const rules::RulesConfig& Config)
	{
		const int P = Player < 0 || Player > 1 ? 0 : Player;
		switch (State.Game)
		{
		case rules::Discipline::NineBall:
		case rules::Discipline::TenBall:
			return rules::CountObjectBallsOnTable(State);
		case rules::Discipline::StraightPool:
		{
			const int ToGo = Config.TargetPoints - State.Players[P].Score;
			return ToGo < 1 ? 1 : (ToGo > 14 ? 14 : ToGo);
		}
		case rules::Discipline::EightBall:
		case rules::Discipline::Blackball:
			break;
		}
		int Solids = 0;
		int Stripes = 0;
		for (int b = 1; b < rules::kRulesBallCount; ++b)
		{
			if (State.Balls[b].Kind == rules::BallStatusKind::OnTable)
			{
				Solids += rules::GroupOf(b) == rules::BallGroup::Solids ? 1 : 0;
				Stripes += rules::GroupOf(b) == rules::BallGroup::Stripes ? 1 : 0;
			}
		}
		switch (State.Players[P].Group)
		{
		case rules::BallGroup::Solids: return Solids + 1;
		case rules::BallGroup::Stripes: return Stripes + 1;
		case rules::BallGroup::None: break;
		}
		return (Solids < Stripes ? Solids : Stripes) + 1;
	}

	namespace
	{
		// 14.1 is a race to the target points, not a rack: the turn model on a RELATIVE horizon. The mover races for the next
		// min(points to go, kPointsHorizon) points, the other for as many more as the mover leads (fewer as he trails), the lead
		// squashed by L tanh(lead / 2L) (L = kPointsLead) and capped by the other's points to go; the other's count is real and the
		// turn model interpolated between its integer counts. So a foul's point, a run and the lead always keep a weight, while the
		// plain race to 100 would saturate the value (a stronger player's win certain, every decision worth the same).
		struct RaceCounts
		{
			int Me = 1;
			double Other = 1.0;
		};

		RaceCounts PointsRace(int ToGoMe, int ToGoOther)
		{
			RaceCounts C;
			C.Me = ClampInt(ToGoMe, 1, kPointsHorizon);
			const double X = static_cast<double>(ToGoOther - ToGoMe) / (2.0 * kPointsLead);
			const double Squashed = kPointsLead * (1.0 - 2.0 / (Exp(2.0 * Clamp(X, -20.0, 20.0)) + 1.0)); // L tanh(X)
			C.Other = Clamp(C.Me + Squashed, 1.0, Min(static_cast<double>(ToGoOther < 1 ? 1 : ToGoOther), static_cast<double>(kMaxTurnBalls - 1)));
			return C;
		}

		// Points a foul costs in 14.1 (the third consecutive foul: 1 + 15).
		int FoulPenalty(const rules::GameState& State, int Player)
		{
			return State.Players[Player].ConsecutiveFouls >= 2 ? 16 : 1;
		}

		// The value model of MoverWinProbability for a given next-shot summary.
		double ValueWithNext(const EvalContext& Context, const rules::GameState& State, const NextShotInfo& Next)
		{
			const rules::RulesConfig& Config = Context.Match->Rules;
			const int M = State.Shooter < 0 || State.Shooter > 1 ? 0 : State.Shooter;
			const int O = 1 - M;
			const EvalPlayer& Me = Context.Players[M];
			const EvalPlayer& Other = Context.Players[O];
			const bool Rotation = RotationGame(State.Game);
			const bool Points = State.Game == rules::Discipline::StraightPool;
			const int ToGoMe = Config.TargetPoints - State.Players[M].Score;
			const int ToGoOther = Config.TargetPoints - State.Players[O].Score;
			int Nm = 1;
			double No = 1.0;
			if (Points)
			{
				const RaceCounts C = PointsRace(ToGoMe, ToGoOther);
				Nm = C.Me;
				No = C.Other;
			}
			else
			{
				// The planning horizon: with many balls left the turn model's values flatten (a long exchange of short turns), so
				// neither keeping the table nor the next position would matter; players plan a few balls ahead (TUNING).
				const int NmRaw = BallsToWin(State, M, Config);
				const int NoRaw = BallsToWin(State, O, Config);
				Nm = NmRaw < 1 ? 1 : (NmRaw > kValueHorizon ? kValueHorizon : NmRaw);
				No = Rotation ? Nm : (NoRaw < 1 ? 1 : (NoRaw > kValueHorizon ? kValueHorizon : NoRaw));
			}
			const TurnModel Turns(Clamp(Me.RunoutRate, 0.0, 1.0), Clamp(Other.RunoutRate, 0.0, 1.0), Rotation);
			// The turn model at a real count of the other player (14.1; exact at integer counts).
			const auto MeV = [&](int I, double J) {
				const int J0 = static_cast<int>(Floor(J));
				const double F = J - J0;
				return F > 0.0 ? (1.0 - F) * Turns.Me(I, J0) + F * Turns.Me(I, J0 + 1) : Turns.Me(I, J0);
			};
			const auto OtherV = [&](double J, int I) {
				const int J0 = static_cast<int>(Floor(J));
				const double F = J - J0;
				return F > 0.0 ? (1.0 - F) * Turns.Other(J0, I) + F * Turns.Other(J0 + 1, I) : Turns.Other(J0, I);
			};
			// After the mover makes K balls in a row: the counts left (rotation games share them).
			const auto MeAfter = [&](int K) { return MeV(Nm - K, Rotation ? static_cast<double>(Nm - K) : No); };
			const auto OtherAfter = [&](int K) { return OtherV(Rotation ? static_cast<double>(Nm - K) : No, Nm - K); };

			const double P1 = Me.PositionDepth <= 0 ? Me.RunoutRate : Next.PotChance;
			double Made = 0.0;
			if (Nm <= 1)
			{
				Made = 1.0;
			}
			else if (Me.PositionDepth >= 2)
			{
				const double P2 = Next.Pattern;
				Made = P2 * MeAfter(2) + (1.0 - P2) * (1.0 - OtherAfter(1));
			}
			else
			{
				Made = MeAfter(1);
			}
			const double Attempt = P1 * Made + (1.0 - P1) * (1.0 - OtherAfter(0));

			// A player with the cue ball in hand makes his first shot with 0.5 + 0.5 q, then the turn model.
			const double PInHandOther = 0.5 + 0.5 * Clamp(Other.RunoutRate, 0.0, 1.0);
			const double OtherMadeOne = Rotation ? Turns.Other(Nm - 1, Nm - 1) : OtherV(No - 1.0, Nm);
			const double OtherInHand = PInHandOther * OtherMadeOne + (1.0 - PInHandOther) * (1.0 - MeV(Nm, No));
			const double PInHandMe = 0.5 + 0.5 * Clamp(Me.RunoutRate, 0.0, 1.0);
			const double MeInHand = PInHandMe * MeAfter(1) + (1.0 - PInHandMe) * (1.0 - OtherAfter(0));

			// Hooked: a kick is needed; a missed kick is a foul: ball in hand for the other and the third foul loses the rack in
			// 9/10-ball; in 14.1 the foul costs a point (the third 16) and the other takes the table in position.
			const double Foul = Next.InHand ? 0.0 : (1.0 - Next.Visibility) * (1.0 - Clamp(Me.KickSkill, 0.0, 1.0));
			double FoulValue = 0.0;
			if (Points)
			{
				const RaceCounts C = PointsRace(ToGoMe + FoulPenalty(State, M), ToGoOther);
				FoulValue = 1.0 - OtherV(C.Other, C.Me);
			}
			else
			{
				const bool ThirdFoulLoses = ThreeFoulsLose(Config, State.Game) && State.Players[M].ConsecutiveFouls >= 2;
				FoulValue = ThirdFoulLoses ? 0.0 : 1.0 - OtherInHand;
			}
			double Best = (1.0 - Foul) * Attempt + Foul * FoulValue;
			if (Me.PlaysSafeties)
			{
				// The other player after the mover's safety: a first shot of S, else the mover gets a typical position back; hooked, he
				// fouls now and then (his third foul loses the rack; in 14.1 it costs him points).
				const double S = Clamp(kSafetyLeave * (1.0 - Me.SafetyQuality) + kHookedLeave * (1.0 - Next.Visibility), 0.02, 0.95);
				const double OtherFoul = kSafetyFoul * Clamp(Me.SafetyQuality, 0.0, 1.0) * (1.0 - Clamp(Other.KickSkill, 0.0, 1.0));
				double OtherAfterFoul = 0.0;
				if (Points)
				{
					const RaceCounts C = PointsRace(ToGoMe, ToGoOther + FoulPenalty(State, O));
					OtherAfterFoul = 1.0 - MeV(C.Me, C.Other);
				}
				else
				{
					const bool OtherThirdFoulLoses = ThreeFoulsLose(Config, State.Game) && State.Players[O].ConsecutiveFouls >= 2;
					OtherAfterFoul = OtherThirdFoulLoses ? 0.0 : 1.0 - MeInHand;
				}
				const double OtherValue = (1.0 - OtherFoul) * (S * OtherMadeOne + (1.0 - S) * (1.0 - MeV(Nm, No))) + OtherFoul * OtherAfterFoul;
				// Two-way play: the better the shot the mover passes up, the more his safety can also pot or leave him something, so
				// the safety value keeps a gradient in p1 (otherwise every position below the safety threshold would be worth the same,
				// and a safety that leaves a 40 % shot would equal a hook).
				const double Safe = 1.0 - OtherValue;
				const double TwoWay = Safe + kTwoWayShare * P1 * Max(0.0, Made - Safe);
				Best = Max(Best, (1.0 - Foul) * TwoWay + Foul * FoulValue);
			}
			return Clamp(Best, 0.0, 1.0);
		}
	}

	double MoverWinProbability(const EvalContext& Context, const rules::GameState& State)
	{
		if (Context.Table == nullptr || Context.Match == nullptr)
		{
			return 0.5;
		}
		const int M = State.Shooter < 0 || State.Shooter > 1 ? 0 : State.Shooter;
		return ValueWithNext(Context, State, BestNextShot(Context, State, M));
	}

	double TypicalWinProbability(const EvalContext& Context, const rules::GameState& State)
	{
		if (Context.Table == nullptr || Context.Match == nullptr)
		{
			return 0.5;
		}
		const int M = State.Shooter < 0 || State.Shooter > 1 ? 0 : State.Shooter;
		NextShotInfo Typical;
		Typical.PotChance = Clamp(Context.Players[M].RunoutRate, 0.0, 1.0);
		Typical.Pattern = Typical.PotChance;
		Typical.Visibility = 1.0;
		return ValueWithNext(Context, State, Typical);
	}

	namespace
	{
		double StateValueAt(const EvalContext& Context, const rules::MatchState& State, int Player, int Depth)
		{
			switch (State.Phase)
			{
			case rules::MatchPhase::AwaitShot:
			{
				const double P = MoverWinProbability(Context, State.Game);
				return State.Game.Shooter == Player ? P : 1.0 - P;
			}
			case rules::MatchPhase::AwaitDecision:
			{
				const int Decider = State.Decider < 0 || State.Decider > 1 ? 0 : State.Decider;
				if (Depth >= kMaxOptionDepth || State.PendingOutcome.Options.Size() == 0)
				{
					return 0.5;
				}
				double Best = -1.0;
				for (const rules::Option Choice : State.PendingOutcome.Options)
				{
					rules::MatchState Copy = State;
					if (rules::ApplyOption(*Context.Match, Copy, Choice) != ErrorCode::Ok)
					{
						continue;
					}
					Best = Max(Best, StateValueAt(Context, Copy, Decider, Depth + 1));
				}
				if (Best < 0.0)
				{
					return 0.5;
				}
				return Decider == Player ? Best : 1.0 - Best;
			}
			case rules::MatchPhase::RackSetup:
			{
				const int Breaker = State.Game.RackBreaker < 0 || State.Game.RackBreaker > 1 ? 0 : State.Game.RackBreaker;
				const int N = FullRackBalls(Context.Match->Game) > kValueHorizon ? kValueHorizon : FullRackBalls(Context.Match->Game);
				const TurnModel Turns(Clamp(Context.Players[Breaker].RunoutRate, 0.0, 1.0), Clamp(Context.Players[1 - Breaker].RunoutRate, 0.0, 1.0),
					RotationGame(Context.Match->Game));
				const double P = Turns.Me(N, N);
				return Breaker == Player ? P : 1.0 - P;
			}
			case rules::MatchPhase::Setup:
			case rules::MatchPhase::Lag:
			case rules::MatchPhase::LagWinnerChooses:
			case rules::MatchPhase::RackOver:
			case rules::MatchPhase::MatchOver:
				break;
			}
			return 0.5;
		}
	}

	double StateValue(const EvalContext& Context, const rules::MatchState& State, int Player)
	{
		if (Context.Table == nullptr || Context.Match == nullptr)
		{
			return 0.5;
		}
		return StateValueAt(Context, State, Player, 0);
	}
}
