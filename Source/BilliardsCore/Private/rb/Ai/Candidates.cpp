#include "rb/Core/FpGuard.h"
// Owner: WP-12 (AI opponent). Candidate generation of the planner (Docs/architecture.md 7.6): aim families (direct pots, one-rail
// banks, two-ball combinations, safeties, one-rail kicks, push-outs, the break, ball-in-hand placements) ranked by the static
// make chance and expanded into speed x spin variants with the declaration, the bridge, the elevation floor and the pressure.
// Pure functions of the frozen decision state: no simulation here.
#include "PlannerInternal.h"

#include "rb/Math/Scalar.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/TableRules.h"

#include <initializer_list>

namespace rb::ai
{
	namespace
	{
		constexpr double kMaxPotCut = 80.0 * kDegToRad;
		constexpr double kMaxComboCut = 55.0 * kDegToRad;
		constexpr double kMaxComboGap = 0.6;              // [m] first ball to the ghost of the second
		constexpr double kBankWindowScale = 0.45;         // a bank's window: the cushion's rebound adds its own error
		constexpr double kComboWindowScale = 0.7;
		constexpr double kBankSpeedFactor = 1.25;         // speed lost in the cushion
		constexpr double kBankEndMargin = 0.08;           // [m] the bounce point stays this far from the segment's ends
		constexpr double kMinPlanSpeed = 0.5;             // [m/s]
		constexpr double kMaxPlanSpeed = 7.5;             // [m/s]
		constexpr double kArrivalSpeed = 0.25;            // [m/s] the object ball should still have this at the pocket
		constexpr double kArrivalExtra = 0.15;            // [m] ... after this much more travel
		constexpr double kTipToRollingSpeed = 0.94;       // rolling cue-ball speed / tip speed for a centre hit (5/7 of 1.32)
		constexpr double kCueClearance = 0.009;           // [m] cue radius near the tip + margin (elevation floor)
		constexpr double kCueReach = 1.2;                 // [m] balls farther behind the cue ball do not lift the cue
		constexpr double kRailBridgeDistance = 0.16;      // [m] cushion behind the cue ball closer than this: rail bridge
		constexpr double kPlacementSpacing = 0.03;        // [m] ball-in-hand placements closer than this are duplicates
		constexpr double kMaxFallbackElevation = 70.0 * kDegToRad; // the last-resort stroke may jack the cue up this far
		constexpr double kCaromWindowScale = 0.6;         // kisses and caroms: the stun tangent line is only the plan
		constexpr double kCosMinCaromCut = 0.90630778703664994; // cos 25 deg: fuller glancing contacts deflect too little ...
		constexpr double kCosMaxCaromCut = 0.25881904510252074; // cos 75 deg: ... thinner ones keep too little control
		constexpr double kMaxKissGap = 0.6;               // [m] first ball's travel into the kiss
		constexpr double kMaxCaromGap = 1.0;              // [m] cue ball's travel from the first ball to the second
		constexpr double kFrozenGap = 0.002;              // [m] a gap below this is a frozen (dead) kiss

		// Tip-speed multipliers of the object ball's minimum speed, by SpeedVariants (1..4).
		constexpr double kSpeedLadder[4][4] = {{1.6, 0, 0, 0}, {1.3, 2.2, 0, 0}, {1.2, 1.8, 2.8, 0}, {1.1, 1.6, 2.3, 3.3}};

		struct Spin
		{
			double A;
			double B;
		};

		// Cue-axis offsets / R by SpinVariants (1..7): centre, follow, draw, stun, right, left, draw-right.
		constexpr Spin kSpinLadder[7] = {{0.0, 0.0}, {0.0, 0.45}, {0.0, -0.5}, {0.0, -0.2}, {0.3, 0.0}, {-0.3, 0.0}, {0.25, -0.35}};

		constexpr std::uint32_t Bit(int Ball) { return 1u << Ball; }
		bool HasBit(std::uint32_t Mask, int Ball) { return ((Mask >> Ball) & 1u) != 0u; }

		Vec2 Rotate(const Vec2& V, double Angle)
		{
			const double C = Cos(Angle);
			const double S = Sin(Angle);
			return {V.x * C - V.y * S, V.x * S + V.y * C};
		}

		bool RotationGame(rules::Discipline Game) { return Game == rules::Discipline::NineBall || Game == rules::Discipline::TenBall; }

		bool DisciplineCalls(rules::Discipline Game)
		{
			return Game == rules::Discipline::EightBall || Game == rules::Discipline::TenBall || Game == rules::Discipline::StraightPool;
		}

		// An aim family: one way to play a ball, before the speed / spin expansion.
		struct Family
		{
			ShotType Type = ShotType::Pot;
			BallId First = kNoBall;
			BallId Pot = kNoBall;
			PocketId Pocket = PocketId::None;
			BallId MeasureBall = kNoBall;       // kNoBall: First (measured after the cue ball's contact)
			BallId MeasureStriker = kCueBallId;
			Vec2 MeasureTarget;
			double Azimuth = 0.0;
			double Chance = 0.0;
			double MinSpeed = 1.0; // [m/s] tip speed that just brings the potted ball to the pocket
			bool GameBall = false;
		};

		// Everything generation needs about one origin.
		struct Scene
		{
			const PlanContext* Ctx = nullptr;
			const Origin* From = nullptr;
			int OriginIndex = 0;
			int Parent = -1;
			bool SecondPly = false;
			rules::ShotConstraints Constraints;
			std::uint32_t Legal = 0;
			std::uint32_t Useful = 0;       // balls whose pot keeps the table (combinations)
			bool InHand = false;
			double Rc = kDefaultBallRadius; // cue-ball radius
			double RollDecel = 0.1;         // mu_r g [m/s^2]
			double Restitution = 0.95;      // e_b
		};

		double RollStartSpeed(double EndSpeed, double Distance, double Decel) { return Sqrt(EndSpeed * EndSpeed + 2.0 * Decel * Max(0.0, Distance)); }

		// Speed the ball must leave a collision with to still roll Distance and arrive with kArrivalSpeed.
		double ArriveSpeed(double Distance, double Decel) { return RollStartSpeed(kArrivalSpeed, Distance + kArrivalExtra, Decel); }

		// Speed before a collision that gives the struck ball SpeedAfter along a line at Cut from the striker's path.
		double BeforeCollision(double SpeedAfter, double Cut, double Restitution)
		{
			return SpeedAfter / (Max(Cos(Cut), 0.25) * 0.5 * (1.0 + Restitution));
		}

		double TipSpeedFor(double BallSpeed) { return Clamp(BallSpeed / kTipToRollingSpeed, kMinPlanSpeed, kMaxPlanSpeed); }

		// Distance from P along the unit direction D to the nose-line rectangle |x| <= HalfLength, |y| <= HalfWidth.
		double RayToRectangle(const Vec2& P, const Vec2& D, double HalfLength, double HalfWidth)
		{
			double Best = kInfinity;
			if (D.x > 1e-12)
			{
				Best = Min(Best, (HalfLength - P.x) / D.x);
			}
			else if (D.x < -1e-12)
			{
				Best = Min(Best, (-HalfLength - P.x) / D.x);
			}
			if (D.y > 1e-12)
			{
				Best = Min(Best, (HalfWidth - P.y) / D.y);
			}
			else if (D.y < -1e-12)
			{
				Best = Min(Best, (-HalfWidth - P.y) / D.y);
			}
			return Max(0.0, Best);
		}

		// Bridge, lengths, elevation floor (rail behind the cue ball, balls the cue passes over) and pressure of a stroke.
		human::StrokeSituation MakeSituation(const Scene& S, const Vec2& Cue, double Azimuth, bool GameBall, double& Elevation)
		{
			const PlanContext& X = *S.Ctx;
			const TableGeometry& T = *X.Input.Table;
			human::StrokeSituation Out = X.Input.Situation;
			const Vec2 Back{-Cos(Azimuth), -Sin(Azimuth)};
			const double DistanceBack = RayToRectangle(Cue, Back, T.HalfLength, T.HalfWidth);
			double TanFloor = 0.0;
			Out.FloorBy = human::FloorSource::None;
			Out.FloorBall = kNoBall;
			if (DistanceBack < 0.4)
			{
				const double OverNose = (T.Spec.CushionNoseHeight + kCueClearance - S.Rc) / Max(DistanceBack, 0.01);
				const double OverRail = (T.Spec.RailTopZ + kCueClearance - S.Rc) / Max(DistanceBack + T.Spec.CushionWidth, 0.01);
				TanFloor = Max(0.0, Max(OverNose, OverRail));
				if (TanFloor > 0.0)
				{
					Out.FloorBy = human::FloorSource::Rail;
				}
			}
			const BallLayout& L = S.From->Layout;
			for (int j = 1; j < kMaxBalls; ++j)
			{
				if (!HasBit(L.OnTable, j))
				{
					continue;
				}
				const Vec2 Rel = L.Position[j] - Cue;
				const double Along = Dot(Rel, Back);
				if (Along <= 0.0 || Along > kCueReach)
				{
					continue;
				}
				if (Abs(Cross(Back, Rel)) < L.Radius[j] + kCueClearance)
				{
					const double Need = (2.0 * L.Radius[j] + kCueClearance - S.Rc) / Max(Along - L.Radius[j], 0.01);
					if (Need > TanFloor)
					{
						TanFloor = Need;
						Out.FloorBy = human::FloorSource::Ball;
						Out.FloorBall = static_cast<BallId>(j);
					}
				}
			}
			Out.ElevationFloor = Atan(TanFloor);
			if (DistanceBack < kRailBridgeDistance)
			{
				Out.Bridge = human::BridgeType::Rail;
				Out.BridgeLength = Clamp(DistanceBack + 0.03, 0.06, 0.2);
			}
			else if (Out.FloorBy == human::FloorSource::Ball)
			{
				Out.Bridge = human::BridgeType::Elevated;
				Out.BridgeLength = 0.2;
			}
			else
			{
				Out.Bridge = human::BridgeType::Closed;
				Out.BridgeLength = 0.2;
			}
			Out.BridgeToGrip = 0.8;
			human::PressureInputs P = X.Input.Pressure;
			P.GameBall = P.GameBall || GameBall;
			Out.Pressure = human::ComputePressure(P, X.Input.PressureMode);
			Elevation = Max(kMinElevation, Out.ElevationFloor + 1.0 * kDegToRad);
			return Out;
		}

		// The ball whose pot wins the rack (9 / 10 / 8).
		bool IsGameBall(const rules::GameState& G, int Ball)
		{
			switch (G.Game)
			{
			case rules::Discipline::NineBall: return Ball == 9;
			case rules::Discipline::TenBall: return Ball == 10;
			case rules::Discipline::EightBall:
			case rules::Discipline::Blackball: return Ball == 8;
			case rules::Discipline::StraightPool: break;
			}
			return false;
		}

		// Best pocket (by open window) for a declared ball, for calls that the discipline needs on safeties.
		PocketId CallPocketFor(const Scene& S, int Ball)
		{
			const TableGeometry& T = *S.Ctx->Input.Table;
			const BallLayout& L = S.From->Layout;
			PocketId Best = PocketId::None;
			double BestWindow = -1.0;
			for (int p = 0; p < T.Pockets.Size(); ++p)
			{
				const PocketAim Aim = PocketAimFor(T, p, L.Position[Ball], L.Radius[Ball]);
				if (Aim.HalfWindow > BestWindow)
				{
					BestWindow = Aim.HalfWindow;
					Best = static_cast<PocketId>(p);
				}
			}
			return Best;
		}

		bool MakeDeclaration(const Scene& S, ShotType Type, int First, int Pot, PocketId Pocket, bool Place, const Vec2& Cue, rules::ShotDeclaration& D)
		{
			const PlanContext& X = *S.Ctx;
			const rules::MatchState& M = S.From->State;
			D = rules::ShotDeclaration{};
			switch (Type)
			{
			case ShotType::Break:
				D.Kind = rules::ShotKind::Break;
				break;
			case ShotType::PushOut:
				D.Kind = rules::ShotKind::PushOut;
				break;
			case ShotType::Safety:
			case ShotType::Kick:
				D.Kind = S.Constraints.SafetyAllowed ? rules::ShotKind::Safety : rules::ShotKind::Normal;
				if (D.Kind == rules::ShotKind::Normal && S.Constraints.CallRequired && First != kNoBall)
				{
					D.Called.Ball = static_cast<BallId>(First);
					D.Called.Pocket = CallPocketFor(S, First);
				}
				break;
			case ShotType::Pot:
			case ShotType::Bank:
			case ShotType::Combination:
			case ShotType::Kiss:
			case ShotType::Carom:
				D.Kind = rules::ShotKind::Normal;
				if (DisciplineCalls(M.Game.Game) && !M.Game.IsBreakShot)
				{
					D.Called.Ball = static_cast<BallId>(Pot);
					D.Called.Pocket = Pocket;
				}
				break;
			}
			rules::CompleteDeclaration(X.Input.Match, M, D);
			return rules::ValidateDeclaration(X.Input.Match, M, D, Place ? &Cue : nullptr) == ErrorCode::Ok;
		}

		bool Full(const PlannerState& State) { return static_cast<int>(State.Candidates.size()) >= kMaxCandidates; }

		// Appends one candidate (false if it cannot be played: capacity, declaration, elevation).
		bool Emit(PlannerState& State, const Scene& S, ShotType Type, int First, int Pot, PocketId Pocket, const Vec2& Cue, bool Place, double Azimuth,
			double Speed, const Spin& Sp, const Vec2& MeasureTarget, double Chance, double MissValue, bool GameBall, double MaxElevation = kMaxElevation)
		{
			if (Full(State))
			{
				return false;
			}
			Candidate C;
			C.Type = Type;
			C.FirstBall = static_cast<BallId>(First);
			C.PotBall = static_cast<BallId>(Pot);
			C.MeasureBall = static_cast<BallId>(First);
			C.Pocket = Pocket;
			C.Origin = static_cast<std::uint8_t>(S.OriginIndex);
			C.Parent = static_cast<std::int16_t>(S.Parent);
			C.PlaceCueBall = Place;
			C.CueBall = Cue;
			C.MeasureTarget = MeasureTarget;
			C.PerceivedPot = Chance;
			C.MissValue = MissValue;
			C.KeepValue = KeepValueAt(*S.Ctx, *S.From, Cue);
			C.GameBall = GameBall;
			C.AimCorrect = S.Ctx->Profile.AimIterations > 0 && First != kNoBall && IsPotShot(Type);
			double Elevation = kMinElevation;
			C.Situation = MakeSituation(S, Cue, Azimuth, GameBall, Elevation);
			if (Elevation > MaxElevation)
			{
				return false;
			}
			C.Plan.Azimuth = Azimuth;
			C.Plan.Elevation = Elevation;
			C.Plan.AxisOffsetA = Sp.A;
			C.Plan.AxisOffsetB = Sp.B;
			C.Plan.Speed = Clamp(Speed, kMinPlanSpeed, kMaxPlanSpeed);
			C.Plan.Bridge = C.Situation.Bridge;
			if (!MakeDeclaration(S, Type, First, Pot, Pocket, Place, Cue, C.Declaration))
			{
				return false;
			}
			State.Candidates.push_back(C);
			return true;
		}

		// The two points G of the circle (Center, Radius) whose tangent passes through From ((From - G) perpendicular to
		// (Center - G)); false if From lies inside the circle.
		bool TangentPoints(const Vec2& From, const Vec2& Center, double Radius, Vec2 (&Out)[2])
		{
			const Vec2 W = From - Center;
			const double Distance = Length(W);
			if (!(Distance > Radius * (1.0 + 1e-9)))
			{
				return false;
			}
			const Vec2 Unit = W / Distance;
			const double Beta = Acos(Clamp(Radius / Distance, -1.0, 1.0));
			Out[0] = Center + Rotate(Unit, Beta) * Radius;
			Out[1] = Center + Rotate(Unit, -Beta) * Radius;
			return true;
		}

		double ChainChance(double AllowCue, double Sigma)
		{
			return Sigma > 0.0 ? Clamp(Erf(AllowCue / (Sigma * 1.4142135623730951)), 0.0, 1.0) : (AllowCue > 0.0 ? 1.0 : 0.0);
		}

		// Kisses (object-ball caroms): the legal ball t runs into ball k and leaves it along the tangent line into pocket p (stun
		// geometry: the planner's plan; the simulator's aim correction and rollouts judge the real, rolling kiss). A ball frozen
		// to k is a "dead" kiss when its tangent line points into the pocket's window.
		template <typename PushFn>
		void KissFamilies(const Scene& S, const Vec2& Cue, int t, int p, PushFn&& Push)
		{
			const PlanContext& X = *S.Ctx;
			const TableGeometry& T = *X.Input.Table;
			const BallLayout& L = S.From->Layout;
			const rules::GameState& G = S.From->State.Game;
			const double Sigma = X.Profile.PerceivedAimSigma;
			const Vec2 O = L.Position[t];
			const double Rt = L.Radius[t];
			const Vec2 AimPoint = T.Pockets[p].MouthMid + T.Pockets[p].Axis * (0.25 * Rt); // PocketAimFor's aim point
			for (int k = 1; k < rules::kRulesBallCount; ++k)
			{
				if (k == t || !HasBit(L.OnTable, k))
				{
					continue;
				}
				const Vec2 K = L.Position[k];
				const double Sum = Rt + L.Radius[k];
				const bool Frozen = Length(K - O) < Sum + kFrozenGap;
				Vec2 Kisses[2];
				int Count = 0;
				if (Frozen)
				{
					// The departure line is the tangent at the current contact (the sense toward the pocket, below).
					Kisses[0] = O;
					Count = 1;
				}
				else if (TangentPoints(AimPoint, K, Sum, Kisses))
				{
					Count = 2;
				}
				for (int i = 0; i < Count; ++i)
				{
					const Vec2 Gk = Kisses[i];
					const PocketAim Aim = PocketAimFor(T, p, Gk, Rt);
					if (!Aim.Valid)
					{
						continue;
					}
					const Vec2 N = Frozen ? Normalized(K - O) : (K - Gk) / Sum;
					Vec2 Out = Normalized(Aim.Point - Gk);
					double Lateral = 0.0; // frozen: how far the tangent line passes from the aim point [m]
					if (Frozen)
					{
						const Vec2 Tangent = Dot(Aim.Point - O, PerpCcw(N)) > 0.0 ? PerpCcw(N) : -PerpCcw(N);
						Lateral = Abs(Cross(Tangent, Aim.Point - O));
						if (Lateral >= Aim.HalfWindow * kCaromWindowScale)
						{
							continue;
						}
						Out = Tangent;
					}
					const double Gap = Frozen ? 0.0 : Length(Gk - O);
					if (Gap > kMaxKissGap)
					{
						continue;
					}
					// The first ball's direction into the kiss: toward the tangent point, or (frozen) between k and the tangent line.
					const Vec2 D = Frozen ? Normalized(N + Out) : (Gk - O) / Gap;
					const double CosA = Dot(D, N);
					if (CosA < kCosMaxCaromCut || CosA > kCosMinCaromCut || Dot(D, Out) <= 0.0)
					{
						continue;
					}
					const ShotGeometry First = CutGeometry(Cue, S.Rc, O, Rt, O + D * 0.5, kMaxComboCut);
					if (!First.Feasible || !PathClear(L, Cue, First.Ghost, S.Rc, Bit(0) | Bit(t)) ||
						(!Frozen && !PathClear(L, O, Gk, Rt, Bit(0) | Bit(t) | Bit(k))) || !PathClear(L, Gk, Aim.Point, Rt, Bit(0) | Bit(t) | Bit(k)))
					{
						continue;
					}
					const double Travel = Length(Aim.Point - Gk);
					const double SinA = Sqrt(Max(0.0, 1.0 - CosA * CosA));
					double AllowCue = 0.0;
					if (Frozen)
					{
						// A dead kiss: the departure line hardly depends on the cue ball's direction; the chance is the share of the
						// window the tangent line leaves, and the cue ball must still drive the ball between k and the line.
						AllowCue = (1.0 - Lateral / (Aim.HalfWindow * kCaromWindowScale)) * 0.3 * (S.Rc + Rt) * Cos(First.Cut) / Max(First.CueDistance, 0.05);
					}
					else
					{
						const double W2 = Atan(Aim.HalfWindow * kCaromWindowScale / Max(Travel, 0.01));
						const double AllowFirst = W2 * Sum * CosA / Max(Gap, 0.05);
						AllowCue = AllowFirst * (S.Rc + Rt) * Cos(First.Cut) / Max(First.CueDistance, 0.05);
					}
					Family F;
					F.Type = ShotType::Kiss;
					F.First = static_cast<BallId>(t);
					F.Pot = static_cast<BallId>(t);
					F.Pocket = static_cast<PocketId>(p);
					F.MeasureBall = static_cast<BallId>(t);
					F.MeasureStriker = static_cast<BallId>(k);
					F.MeasureTarget = Aim.Point;
					F.Azimuth = First.Azimuth;
					F.Chance = ChainChance(AllowCue, Sigma);
					const double KissSpeed = ArriveSpeed(Travel, S.RollDecel) / Max(SinA, 0.3);
					const double FirstSpeed = RollStartSpeed(KissSpeed, Gap, S.RollDecel);
					F.MinSpeed = TipSpeedFor(RollStartSpeed(BeforeCollision(FirstSpeed, First.Cut, S.Restitution), First.CueDistance, S.RollDecel));
					F.GameBall = IsGameBall(G, t);
					Push(F);
				}
			}
		}

		// Cue-ball caroms: the cue ball glances off the legal ball t, leaves it along the tangent line (stun geometry) and pockets
		// ball u in p (in 9-ball the classic "carom the 9 off the lowest ball").
		template <typename PushFn>
		void CaromFamilies(const Scene& S, const Vec2& Cue, int t, int p, PushFn&& Push)
		{
			const PlanContext& X = *S.Ctx;
			const TableGeometry& T = *X.Input.Table;
			const BallLayout& L = S.From->Layout;
			const rules::GameState& G = S.From->State.Game;
			const double Sigma = X.Profile.PerceivedAimSigma;
			const Vec2 O = L.Position[t];
			const double SumT = S.Rc + L.Radius[t];
			for (int u = 1; u < rules::kRulesBallCount; ++u)
			{
				if (u == t || !HasBit(S.Useful, u) || !HasBit(L.OnTable, u))
				{
					continue;
				}
				const Vec2 Ou = L.Position[u];
				const double Ru = L.Radius[u];
				const PocketAim Aim = PocketAimFor(T, p, Ou, Ru);
				if (!Aim.Valid || !PathClear(L, Ou, Aim.Point, Ru, Bit(0) | Bit(t) | Bit(u)))
				{
					continue;
				}
				const Vec2 Uu = Normalized(Aim.Point - Ou);
				const Vec2 GhostU = Ou - Uu * (S.Rc + Ru);
				Vec2 Contacts[2];
				if (!TangentPoints(GhostU, O, SumT, Contacts))
				{
					continue;
				}
				for (const Vec2& Pc : Contacts)
				{
					const double D0 = Length(Pc - Cue);
					const double D1 = Length(GhostU - Pc);
					if (D0 < 0.05 || D1 < 0.05 || D1 > kMaxCaromGap)
					{
						continue;
					}
					const Vec2 In = (Pc - Cue) / D0;
					const Vec2 N = (O - Pc) / SumT;
					const Vec2 Tau = (GhostU - Pc) / D1;
					const double CosA = Dot(In, N);
					const double CosCut2 = Dot(Tau, Uu);
					if (CosA < kCosMaxCaromCut || CosA > kCosMinCaromCut || Dot(In, Tau) <= 0.0 || CosCut2 < Cos(kMaxComboCut))
					{
						continue;
					}
					if (!PathClear(L, Cue, Pc, S.Rc, Bit(0) | Bit(t)) || !PathClear(L, Pc, GhostU, S.Rc, Bit(0) | Bit(t) | Bit(u)))
					{
						continue;
					}
					const double ObjectDistance = Length(Aim.Point - Ou);
					const double W2 = Atan(Aim.HalfWindow * kCaromWindowScale / Max(ObjectDistance, 0.01));
					const double AllowAfter = W2 * (S.Rc + Ru) * CosCut2 / Max(D1, 0.05);
					const double AllowCue = AllowAfter * SumT * CosA / Max(D0, 0.05);
					Family F;
					F.Type = ShotType::Carom;
					F.First = static_cast<BallId>(t);
					F.Pot = static_cast<BallId>(u);
					F.Pocket = static_cast<PocketId>(p);
					F.MeasureBall = static_cast<BallId>(u);
					F.MeasureStriker = static_cast<BallId>(kCueBallId);
					F.MeasureTarget = Aim.Point;
					F.Azimuth = Atan2(In.y, In.x);
					F.Chance = ChainChance(AllowCue, Sigma);
					const double SinA = Sqrt(Max(0.0, 1.0 - CosA * CosA));
					const double CueAtU = BeforeCollision(ArriveSpeed(ObjectDistance, S.RollDecel), Acos(Clamp(CosCut2, -1.0, 1.0)), S.Restitution);
					const double CueBeforeT = RollStartSpeed(CueAtU, D1, S.RollDecel) / Max(SinA, 0.3);
					F.MinSpeed = TipSpeedFor(RollStartSpeed(CueBeforeT, D0, S.RollDecel));
					F.GameBall = IsGameBall(G, u);
					Push(F);
				}
			}
		}

		// Direct pots, banks and combinations from the cue position Cue (appended to Families, capacity Capacity).
		int PotFamilies(const Scene& S, const Vec2& Cue, Family* Families, int Capacity)
		{
			const PlanContext& X = *S.Ctx;
			const PlannerProfile& P = X.Profile;
			const TableGeometry& T = *X.Input.Table;
			const BallLayout& L = S.From->Layout;
			const rules::GameState& G = S.From->State.Game;
			const double Sigma = P.PerceivedAimSigma;
			int Count = 0;
			// When the buffer is full, a better family replaces the worst one (the last of the lowest chance): the best Capacity
			// families survive whatever the generation order.
			const auto Push = [&](const Family& F) {
				if (!(F.Chance > 0.0))
				{
					return;
				}
				if (Count < Capacity)
				{
					Families[Count++] = F;
					return;
				}
				int Worst = 0;
				for (int i = 1; i < Count; ++i)
				{
					Worst = Families[i].Chance <= Families[Worst].Chance ? i : Worst;
				}
				if (F.Chance > Families[Worst].Chance)
				{
					Families[Worst] = F;
				}
			};
			for (int t = 1; t < rules::kRulesBallCount; ++t)
			{
				if (!HasBit(S.Legal, t) || !HasBit(L.OnTable, t))
				{
					continue;
				}
				const Vec2 O = L.Position[t];
				const double Rt = L.Radius[t];
				for (int p = 0; p < T.Pockets.Size(); ++p)
				{
					// Direct pot.
					const PocketAim Aim = PocketAimFor(T, p, O, Rt);
					if (Aim.Valid)
					{
						const ShotGeometry Shot = CutGeometry(Cue, S.Rc, O, Rt, Aim.Point, kMaxPotCut);
						if (Shot.Feasible && PathClear(L, Cue, Shot.Ghost, S.Rc, Bit(0) | Bit(t)) && PathClear(L, O, Aim.Point, Rt, Bit(0) | Bit(t)))
						{
							Family F;
							F.Type = ShotType::Pot;
							F.First = static_cast<BallId>(t);
							F.Pot = static_cast<BallId>(t);
							F.Pocket = static_cast<PocketId>(p);
							F.MeasureTarget = Aim.Point;
							F.Azimuth = Shot.Azimuth;
							F.Chance = PotChance(Shot, Aim.HalfWindow, S.Rc + Rt, Sigma);
							const double ObjectSpeed = ArriveSpeed(Shot.ObjectDistance, S.RollDecel);
							F.MinSpeed = TipSpeedFor(RollStartSpeed(BeforeCollision(ObjectSpeed, Shot.Cut, S.Restitution), Shot.CueDistance, S.RollDecel));
							F.GameBall = IsGameBall(G, t);
							Push(F);
						}
					}
					// One-rail banks: the aim point mirrored across the cushion's reflection line (nose line + R inward).
					if (P.Banks && !S.SecondPly)
					{
						for (int k = 0; k < T.Noses.Size(); ++k)
						{
							const NoseSegment& Nose = T.Noses[k];
							if (!Nose.Present)
							{
								continue;
							}
							const Vec2 L0 = Nose.Start + Nose.InwardNormal * Rt;
							const Vec2 N = Nose.InwardNormal;
							const Vec2 Target = T.Pockets[p].MouthMid + T.Pockets[p].Axis * (0.25 * Rt);
							if (Dot(Target - L0, N) < 0.1 || Dot(O - L0, N) <= 0.0)
							{
								continue; // pockets next to this cushion, or the ball behind the line
							}
							const Vec2 Mirror = Target - N * (2.0 * Dot(Target - L0, N));
							const double Den = Dot(Mirror - O, N);
							if (Abs(Den) < 1e-9)
							{
								continue;
							}
							const double Sg = Dot(L0 - O, N) / Den;
							if (!(Sg > 0.0 && Sg < 1.0))
							{
								continue;
							}
							const Vec2 Bounce = O + (Mirror - O) * Sg;
							const double Along = Dot(Bounce - Nose.Start, Nose.Direction);
							if (Along < kBankEndMargin || Along > Nose.Length - kBankEndMargin)
							{
								continue;
							}
							const PocketAim BankAim = PocketAimFor(T, p, Bounce, Rt);
							if (!BankAim.Valid)
							{
								continue;
							}
							ShotGeometry Shot = CutGeometry(Cue, S.Rc, O, Rt, Bounce, kMaxPotCut);
							if (!Shot.Feasible || !PathClear(L, Cue, Shot.Ghost, S.Rc, Bit(0) | Bit(t)) || !PathClear(L, O, Bounce, Rt, Bit(0) | Bit(t)) ||
								!PathClear(L, Bounce, BankAim.Point, Rt, Bit(0) | Bit(t)))
							{
								continue;
							}
							const double Travel = Shot.ObjectDistance + Length(BankAim.Point - Bounce);
							Shot.ObjectDistance = Travel;
							Family F;
							F.Type = ShotType::Bank;
							F.First = static_cast<BallId>(t);
							F.Pot = static_cast<BallId>(t);
							F.Pocket = static_cast<PocketId>(p);
							F.MeasureTarget = Bounce;
							F.Azimuth = Shot.Azimuth;
							F.Chance = PotChance(Shot, BankAim.HalfWindow, S.Rc + Rt, Sigma, kBankWindowScale);
							const double ObjectSpeed = kBankSpeedFactor * ArriveSpeed(Travel, S.RollDecel);
							F.MinSpeed = TipSpeedFor(RollStartSpeed(BeforeCollision(ObjectSpeed, Shot.Cut, S.Restitution), Shot.CueDistance, S.RollDecel));
							F.GameBall = IsGameBall(G, t);
							Push(F);
						}
					}
					// Two-ball combinations: t into u, u into p.
					if (P.Combinations && !S.SecondPly)
					{
						for (int u = 1; u < rules::kRulesBallCount; ++u)
						{
							if (u == t || !HasBit(S.Useful, u) || !HasBit(L.OnTable, u))
							{
								continue;
							}
							const Vec2 O2 = L.Position[u];
							const double R2 = L.Radius[u];
							const PocketAim Aim2 = PocketAimFor(T, p, O2, R2);
							if (!Aim2.Valid)
							{
								continue;
							}
							const ShotGeometry Second = CutGeometry(O, Rt, O2, R2, Aim2.Point, kMaxComboCut);
							if (!Second.Feasible || Second.CueDistance > kMaxComboGap)
							{
								continue;
							}
							const ShotGeometry First = CutGeometry(Cue, S.Rc, O, Rt, Second.Ghost, kMaxComboCut);
							if (!First.Feasible || !PathClear(L, Cue, First.Ghost, S.Rc, Bit(0) | Bit(t)) ||
								!PathClear(L, O, Second.Ghost, Rt, Bit(0) | Bit(t) | Bit(u)) || !PathClear(L, O2, Aim2.Point, R2, Bit(0) | Bit(t) | Bit(u)))
							{
								continue;
							}
							// Error chain: cue-ball direction -> first ball's direction -> second ball's direction (PositionEval.h PotChance).
							const double W2 = Atan(Aim2.HalfWindow * kComboWindowScale / Max(Second.ObjectDistance, 0.01));
							const double AllowFirst = W2 * (Rt + R2) * Cos(Second.Cut) / Max(Second.CueDistance, 0.05);
							const double AllowCue = AllowFirst * (S.Rc + Rt) * Cos(First.Cut) / Max(First.CueDistance, 0.05);
							Family F;
							F.Type = ShotType::Combination;
							F.First = static_cast<BallId>(t);
							F.Pot = static_cast<BallId>(u);
							F.Pocket = static_cast<PocketId>(p);
							F.MeasureTarget = Second.Ghost;
							F.Azimuth = First.Azimuth;
							F.Chance = Sigma > 0.0 ? Clamp(Erf(AllowCue / (Sigma * 1.4142135623730951)), 0.0, 1.0) : 1.0;
							const double SecondSpeed = ArriveSpeed(Second.ObjectDistance, S.RollDecel);
							const double FirstSpeed = RollStartSpeed(BeforeCollision(SecondSpeed, Second.Cut, S.Restitution), Second.CueDistance, S.RollDecel);
							F.MinSpeed = TipSpeedFor(RollStartSpeed(BeforeCollision(FirstSpeed, First.Cut, S.Restitution), First.CueDistance, S.RollDecel));
							F.GameBall = IsGameBall(G, u);
							Push(F);
						}
					}
					if (P.Caroms && !S.SecondPly)
					{
						KissFamilies(S, Cue, t, p, Push);
						CaromFamilies(S, Cue, t, p, Push);
					}
				}
			}
			// Best static chance first; ties keep generation order (insertion sort: stable, allocation-free).
			for (int i = 1; i < Count; ++i)
			{
				const Family F = Families[i];
				int j = i;
				while (j > 0 && Families[j - 1].Chance < F.Chance)
				{
					Families[j] = Families[j - 1];
					--j;
				}
				Families[j] = F;
			}
			return Count;
		}

		int ClampCount(int Value, int Lo, int Hi) { return Value < Lo ? Lo : (Value > Hi ? Hi : Value); }

		void ExpandFamily(PlannerState& State, const Scene& S, const Family& F, const Vec2& Cue, bool Place, double MissValue, int Speeds, int Spins)
		{
			const PlannerProfile& P = S.Ctx->Profile;
			const int Ns = ClampCount(Speeds, 1, 4);
			const int Nsp = ClampCount(Spins, 1, 7);
			for (int s = 0; s < Ns; ++s)
			{
				const double Speed = F.MinSpeed * kSpeedLadder[Ns - 1][s] * P.SpeedBias;
				for (int k = 0; k < Nsp; ++k)
				{
					if (Emit(State, S, F.Type, F.First, F.Pot, F.Pocket, Cue, Place, F.Azimuth, Speed, kSpinLadder[k], F.MeasureTarget, F.Chance, MissValue, F.GameBall) &&
						F.MeasureBall != kNoBall)
					{
						Candidate& C = State.Candidates.back();
						C.MeasureBall = F.MeasureBall;
						C.MeasureStriker = F.MeasureStriker;
					}
				}
			}
		}

		// Legal balls by distance from Cue (ties by id), at most Max.
		int LegalByDistance(const Scene& S, const Vec2& Cue, int* Out, int Capacity)
		{
			const BallLayout& L = S.From->Layout;
			int Count = 0;
			for (int b = 1; b < rules::kRulesBallCount; ++b)
			{
				if (!HasBit(S.Legal, b) || !HasBit(L.OnTable, b))
				{
					continue;
				}
				const double D = LengthSquared(L.Position[b] - Cue);
				int j = Count < Capacity ? Count : Capacity - 1;
				if (Count >= Capacity && D >= LengthSquared(L.Position[Out[Capacity - 1]] - Cue))
				{
					continue;
				}
				while (j > 0 && LengthSquared(L.Position[Out[j - 1]] - Cue) > D)
				{
					Out[j] = Out[j - 1];
					--j;
				}
				Out[j] = b;
				Count = Count < Capacity ? Count + 1 : Capacity;
			}
			return Count;
		}

		// Safeties at the nearest Targets legal balls: full, half and thin on both sides, soft and firm, centre and draw. Basic (profiles
		// without a safety game, when nothing can be potted): full and half hits, firm, centre ball ("just hit it").
		void SafetyCandidates(PlannerState& State, const Scene& S, const Vec2& Cue, bool Place, int Targets, bool Basic)
		{
			if (Targets <= 0)
			{
				return;
			}
			int Balls[rules::kRulesBallCount] = {};
			const int N = LegalByDistance(S, Cue, Balls, Targets < rules::kRulesBallCount ? Targets : rules::kRulesBallCount);
			const BallLayout& L = S.From->Layout;
			for (int i = 0; i < N; ++i)
			{
				const int t = Balls[i];
				const Vec2 O = L.Position[t];
				const double Sum = S.Rc + L.Radius[t];
				const Vec2 Dir = Normalized(O - Cue);
				if (LengthSquared(Dir) == 0.0)
				{
					continue;
				}
				const Vec2 Side = PerpCcw(Dir);
				const double Distance = Length(O - Cue);
				for (const double F : {0.0, 0.5, -0.5, 0.85, -0.85})
				{
					if (Basic && Abs(F) > 0.6)
					{
						continue;
					}
					const Vec2 AimPoint = O + Side * (F * Sum);
					const Vec2 Contact = AimPoint - Dir * (Sqrt(Max(0.0, 1.0 - F * F)) * Sum);
					if (!PathClear(L, Cue, Contact, S.Rc, Bit(0) | Bit(t)))
					{
						continue;
					}
					const Vec2 Aim = AimPoint - Cue;
					const double Azimuth = Atan2(Aim.y, Aim.x);
					for (const double Roll : {0.4, 1.6, 2.5})
					{
						if (Basic != (Roll > 2.0))
						{
							continue;
						}
						const double Speed = TipSpeedFor(RollStartSpeed(0.0, Distance + Roll, S.RollDecel));
						for (const Spin& Sp : {Spin{0.0, 0.0}, Spin{0.0, -0.4}})
						{
							if (Basic && Sp.B != 0.0)
							{
								continue;
							}
							Emit(State, S, ShotType::Safety, t, kNoBall, PocketId::None, Cue, Place, Azimuth, Speed, Sp, O, 1.0, 0.0, false);
						}
					}
				}
			}
		}

		void KickCandidates(PlannerState& State, const Scene& S, const Vec2& Cue, bool Place)
		{
			const TableGeometry& T = *S.Ctx->Input.Table;
			const BallLayout& L = S.From->Layout;
			int Balls[2] = {};
			const int N = LegalByDistance(S, Cue, Balls, 2);
			for (int i = 0; i < N; ++i)
			{
				const int t = Balls[i];
				const Vec2 O = L.Position[t];
				for (int k = 0; k < T.Noses.Size(); ++k)
				{
					const NoseSegment& Nose = T.Noses[k];
					if (!Nose.Present)
					{
						continue;
					}
					const Vec2 N0 = Nose.InwardNormal;
					const Vec2 L0 = Nose.Start + N0 * S.Rc;
					if (Dot(Cue - L0, N0) <= 0.0 || Dot(O - L0, N0) <= 0.0)
					{
						continue;
					}
					const Vec2 Mirror = O - N0 * (2.0 * Dot(O - L0, N0));
					const double Den = Dot(Mirror - Cue, N0);
					if (Abs(Den) < 1e-9)
					{
						continue;
					}
					const double Sg = Dot(L0 - Cue, N0) / Den;
					if (!(Sg > 0.0 && Sg < 1.0))
					{
						continue;
					}
					const Vec2 Bounce = Cue + (Mirror - Cue) * Sg;
					const double Along = Dot(Bounce - Nose.Start, Nose.Direction);
					if (Along < kBankEndMargin || Along > Nose.Length - kBankEndMargin)
					{
						continue;
					}
					const Vec2 In = Normalized(O - Bounce);
					const Vec2 Contact = O - In * (S.Rc + L.Radius[t]);
					if (!PathClear(L, Cue, Bounce, S.Rc, Bit(0)) || !PathClear(L, Bounce, Contact, S.Rc, Bit(0) | Bit(t)))
					{
						continue;
					}
					const Vec2 Aim = Bounce - Cue;
					const double Travel = Length(Aim) + Length(O - Bounce);
					for (const double Roll : {0.8, 1.8})
					{
						const double Speed = kBankSpeedFactor * TipSpeedFor(RollStartSpeed(0.0, Travel + Roll, S.RollDecel));
						Emit(State, S, ShotType::Kick, t, kNoBall, PocketId::None, Cue, Place, Atan2(Aim.y, Aim.x), Speed, Spin{0.0, 0.0}, O, 1.0, 0.0, false);
					}
				}
			}
		}

		void PushOutCandidates(PlannerState& State, const Scene& S, const Vec2& Cue)
		{
			for (int k = 0; k < 12; ++k)
			{
				const double Azimuth = (static_cast<double>(k) * 30.0 + 15.0) * kDegToRad;
				for (const double Roll : {0.35, 0.9})
				{
					const double Speed = TipSpeedFor(RollStartSpeed(0.0, Roll, S.RollDecel));
					Emit(State, S, ShotType::PushOut, kNoBall, kNoBall, PocketId::None, Cue, false, Azimuth, Speed, Spin{0.0, 0.0}, Cue, 1.0, 0.0, false);
				}
			}
		}

		void BreakCandidates(PlannerState& State, const Scene& S)
		{
			const PlanContext& X = *S.Ctx;
			const TableGeometry& T = *X.Input.Table;
			const rules::GameState& G = S.From->State.Game;
			const BallLayout& L = S.From->Layout;
			const rules::RulesTable& RT = X.Input.Match.Table;
			// The rack's front ball (smallest x; ties by id).
			int Apex = kNoBall;
			for (int b = 1; b < rules::kRulesBallCount; ++b)
			{
				if (HasBit(L.OnTable, b) && (Apex == kNoBall || L.Position[b].x < L.Position[Apex].x))
				{
					Apex = b;
				}
			}
			if (Apex == kNoBall)
			{
				return;
			}
			const bool InHand = S.InHand;
			const double X0 = G.CueBall == rules::CueBallNext::InHandBaulk ? RT.BaulkX - 0.05 : RT.HeadStringX - 0.05;
			const double Y = 0.5 * (T.HalfWidth - S.Rc);
			const double Spins = X.Profile.ModelsThrowAndSquirt ? -0.15 : 0.0;
			for (const double Yk : {0.0, Y, -Y})
			{
				const Vec2 Cue = InHand ? Vec2{X0, Yk} : G.Balls[0].Position;
				if (InHand && !rules::CueBallPlacementLegal(G, Cue, G.CueBall, RT, X.Input.Match.Rules.Tolerances))
				{
					continue;
				}
				const Vec2 Aim = L.Position[Apex] - Cue;
				for (const double Scale : {0.85, 1.0})
				{
					Emit(State, S, ShotType::Break, Apex, kNoBall, PocketId::None, Cue, InHand, Atan2(Aim.y, Aim.x), X.Profile.BreakSpeed * Scale, Spin{0.0, Spins},
						L.Position[Apex], 1.0, 0.0, false);
				}
				if (!InHand)
				{
					break;
				}
			}
		}

		struct Placement
		{
			Vec2 Cue;
			double Score = 0.0;
		};

		// Ball-in-hand placements: behind the ghost ball of every clear pot (0, +-15, +-30 deg off the line; 20, 35, 60 cm), legal
		// for the region, ranked by the static make chance with a small preference for a position angle of about 15 deg.
		int MakePlacements(const Scene& S, Placement* Out, int Capacity)
		{
			const PlanContext& X = *S.Ctx;
			const TableGeometry& T = *X.Input.Table;
			const BallLayout& L = S.From->Layout;
			const rules::GameState& G = S.From->State.Game;
			const rules::RulesTable& RT = X.Input.Match.Table;
			const RulesTolerances& Tol = X.Input.Match.Rules.Tolerances;
			const rules::CueBallNext Region = S.Constraints.PlacementRegion == rules::CueBallNext::InPosition ? rules::CueBallNext::InHandAnywhere
				: S.Constraints.PlacementRegion;
			int Count = 0;
			for (int t = 1; t < rules::kRulesBallCount; ++t)
			{
				if (!HasBit(S.Legal, t) || !HasBit(L.OnTable, t))
				{
					continue;
				}
				for (int p = 0; p < T.Pockets.Size(); ++p)
				{
					const PocketAim Aim = PocketAimFor(T, p, L.Position[t], L.Radius[t]);
					if (!Aim.Valid || !PathClear(L, L.Position[t], Aim.Point, L.Radius[t], Bit(0) | Bit(t)))
					{
						continue;
					}
					const Vec2 U = Normalized(Aim.Point - L.Position[t]);
					const Vec2 Ghost = L.Position[t] - U * (S.Rc + L.Radius[t]);
					for (const double AngleDeg : {15.0, -15.0, 0.0, 30.0, -30.0})
					{
						for (const double Distance : {0.2, 0.35, 0.6})
						{
							const Vec2 P = Ghost - Rotate(U, AngleDeg * kDegToRad) * Distance;
							if (!rules::CueBallPlacementLegal(G, P, Region, RT, Tol) || !PathClear(L, P, Ghost, S.Rc, Bit(0) | Bit(t)))
							{
								continue;
							}
							const ShotGeometry Shot = CutGeometry(P, S.Rc, L.Position[t], L.Radius[t], Aim.Point, kMaxPotCut);
							const double Chance = PotChance(Shot, Aim.HalfWindow, S.Rc + L.Radius[t], X.Profile.PerceivedAimSigma);
							const double Score = Chance * (1.0 - 0.1 * Abs(Abs(AngleDeg) - 15.0) / 15.0);
							bool Duplicate = false;
							for (int k = 0; k < Count && !Duplicate; ++k)
							{
								Duplicate = LengthSquared(Out[k].Cue - P) < kPlacementSpacing * kPlacementSpacing;
							}
							if (Duplicate)
							{
								continue;
							}
							// Insert by score (stable); drop the worst when full.
							int j = Count < Capacity ? Count : Capacity - 1;
							if (Count >= Capacity && Score <= Out[Capacity - 1].Score)
							{
								continue;
							}
							while (j > 0 && Out[j - 1].Score < Score)
							{
								Out[j] = Out[j - 1];
								--j;
							}
							Out[j].Cue = P;
							Out[j].Score = Score;
							Count = Count < Capacity ? Count + 1 : Capacity;
						}
					}
				}
			}
			if (Count == 0)
			{
				// Nothing to pot: the first legal point of a coarse grid of the region (for safeties and kicks).
				const double HalfL = T.HalfLength - S.Rc - 0.02;
				const double HalfW = T.HalfWidth - S.Rc - 0.02;
				const double XMax = Region == rules::CueBallNext::InHandAboveHeadString ? RT.HeadStringX - 0.02
					: (Region == rules::CueBallNext::InHandBaulk ? RT.BaulkX - 0.02 : HalfL);
				for (int i = 0; i < 5 && Count == 0; ++i)
				{
					for (int j = 0; j < 5 && Count == 0; ++j)
					{
						const Vec2 P{-HalfL + (XMax + HalfL) * (0.1 + 0.2 * i), HalfW * (-0.8 + 0.4 * j)};
						if (rules::CueBallPlacementLegal(G, P, Region, RT, Tol))
						{
							Out[0].Cue = P;
							Out[0].Score = 0.0;
							Count = 1;
						}
					}
				}
			}
			return Count;
		}

		// When nothing else was generated: hit the nearest legal ball full at a medium speed.
		void FallbackCandidate(PlannerState& State, const Scene& S, const Vec2& Cue, bool Place)
		{
			// The legal balls by distance (every ball if none is legal), full to thin on both sides, a jacked-up cue allowed (up to
			// kMaxFallbackElevation); the first stroke that can be declared is taken. Its value comes from the rollouts like any other.
			const BallLayout& L = S.From->Layout;
			Scene Any = S;
			if (Any.Legal == 0u)
			{
				Any.Legal = L.OnTable & ~Bit(0);
			}
			int Balls[rules::kRulesBallCount] = {};
			const int N = LegalByDistance(Any, Cue, Balls, rules::kRulesBallCount);
			for (int i = 0; i < N; ++i)
			{
				const int t = Balls[i];
				const Vec2 O = L.Position[t];
				const Vec2 Dir = Normalized(O - Cue);
				const double Sum = S.Rc + L.Radius[t];
				const double Speed = TipSpeedFor(RollStartSpeed(0.0, Length(O - Cue) + 2.5, S.RollDecel));
				for (const double F : {0.0, 0.5, -0.5, 0.85, -0.85})
				{
					const Vec2 Aim = O + PerpCcw(Dir) * (F * Sum) - Cue;
					if (Emit(State, S, ShotType::Safety, t, kNoBall, PocketId::None, Cue, Place, Atan2(Aim.y, Aim.x), Speed, Spin{0.0, 0.0}, O, 1.0, 0.0, false,
							kMaxFallbackElevation))
					{
						return;
					}
				}
			}
		}


		// Value x Scale rounded, at least 1 (0 stays 0).
		int Scaled(int Value, double Scale)
		{
			if (Value <= 0)
			{
				return 0;
			}
			const int S = static_cast<int>(Floor(Value * Scale + 0.5));
			return S < 1 ? 1 : S;
		}
	}

	double MissValueAt(const PlanContext& Context, const Origin& From, const Vec2& Cue)
	{
		// After a missed pot the balls lie somewhere else: the opponent comes to a TYPICAL table (the table as it is now would
		// flatter a hard shot: when the AI has nothing, neither has the opponent from the same cue-ball position).
		rules::GameState G = From.State.Game;
		G.Shooter = 1 - Context.Self;
		G.Balls[0].Kind = rules::BallStatusKind::OnTable;
		G.Balls[0].Position = Cue;
		G.CueBall = rules::CueBallNext::InPosition;
		G.IsBreakShot = false;
		G.PushOutAvailable = false;
		return 1.0 - TypicalWinProbability(Context.Eval, G);
	}

	double KeepValueAt(const PlanContext& Context, const Origin& From, const Vec2& Cue)
	{
		rules::GameState G = From.State.Game;
		G.Shooter = Context.Self;
		G.Balls[0].Kind = rules::BallStatusKind::OnTable;
		G.Balls[0].Position = Cue;
		G.CueBall = rules::CueBallNext::InPosition;
		G.IsBreakShot = false;
		G.PushOutAvailable = false;
		return TypicalWinProbability(Context.Eval, G);
	}

	int GenerateCandidates(PlannerState& State, int OriginIndex, int Parent)
	{
		const PlanContext& X = State.Ctx;
		const PlannerProfile& P = X.Profile;
		const int Before = static_cast<int>(State.Candidates.size());
		Scene S;
		S.Ctx = &X;
		S.From = &State.Origins[OriginIndex];
		S.OriginIndex = OriginIndex;
		S.Parent = Parent;
		S.SecondPly = OriginIndex > 0;
		const rules::MatchState& M = S.From->State;
		const rules::GameState& G = M.Game;
		S.Constraints = rules::GetShotConstraints(X.Input.Match, M);
		S.Legal = S.Constraints.LegalFirstContactMask;
		S.Useful = S.Legal;
		if (RotationGame(G.Game) || G.Game == rules::Discipline::StraightPool)
		{
			for (int b = 1; b < rules::kRulesBallCount; ++b)
			{
				S.Useful |= G.Balls[b].Kind == rules::BallStatusKind::OnTable ? Bit(b) : 0u;
			}
		}
		S.InHand = G.CueBall != rules::CueBallNext::InPosition || G.Balls[0].Kind != rules::BallStatusKind::OnTable;
		S.Rc = X.Specs[0].Radius;
		S.RollDecel = X.Planning.Cloth.RollingResistance * X.Planning.Gravity;
		S.Restitution = X.Planning.BallBall.Restitution;

		if (G.IsBreakShot)
		{
			BreakCandidates(State, S);
			if (static_cast<int>(State.Candidates.size()) == Before)
			{
				FallbackCandidate(State, S, S.InHand ? Vec2{X.Input.Match.Table.HeadStringX - 0.05, 0.0} : G.Balls[0].Position, S.InHand);
			}
			return static_cast<int>(State.Candidates.size()) - Before;
		}

		const int FamilyLimit = S.SecondPly ? ClampCount(P.SecondPlyFamilies, 1, 64) : Scaled(P.PotFamilies, X.Config.Breadth);
		const int Speeds = S.SecondPly ? (P.SpeedVariants < 2 ? P.SpeedVariants : 2) : P.SpeedVariants;
		const int Spins = S.SecondPly ? (P.SpinVariants < 3 ? P.SpinVariants : 3) : P.SpinVariants;
		constexpr int kFamilyCapacity = 256;
		Family Families[kFamilyCapacity];

		if (!S.InHand)
		{
			const Vec2 Cue = G.Balls[0].Position;
			const double Miss = MissValueAt(X, *S.From, Cue);
			const int N = PotFamilies(S, Cue, Families, kFamilyCapacity);
			const int Keep = N < FamilyLimit ? N : FamilyLimit;
			for (int f = 0; f < Keep; ++f)
			{
				ExpandFamily(State, S, Families[f], Cue, false, Miss, Speeds, Spins);
			}
			if (!S.SecondPly)
			{
				const int BeforeSafeties = static_cast<int>(State.Candidates.size());
				if (P.SafetyTargets > 0)
				{
					SafetyCandidates(State, S, Cue, false, Scaled(P.SafetyTargets, X.Config.Breadth), false);
				}
				else if (Keep == 0)
				{
					SafetyCandidates(State, S, Cue, false, 2, true);
				}
				// Hooked: a kicking game kicks whenever the pots are poor; without one, every profile still tries a one-rail kick when
				// no straight line reaches a legal ball (the only legal try, not a tactic; its quality is the profile's own model).
				const bool Hooked = Keep == 0 || Families[0].Chance < 0.25;
				const bool NoLine = Keep == 0 && static_cast<int>(State.Candidates.size()) == BeforeSafeties;
				if ((P.Kicks && Hooked) || NoLine)
				{
					KickCandidates(State, S, Cue, false);
				}
				if (S.Constraints.PushOutAllowed && P.PushOuts)
				{
					PushOutCandidates(State, S, Cue);
				}
			}
			if (static_cast<int>(State.Candidates.size()) == Before)
			{
				FallbackCandidate(State, S, Cue, false);
			}
		}
		else
		{
			Placement Placements[kMaxPlacements];
			const int Limit = S.SecondPly ? 4 : Scaled(P.Placements, X.Config.Breadth);
			const int N = MakePlacements(S, Placements, Limit < kMaxPlacements ? Limit : kMaxPlacements);
			State.Placements += S.SecondPly ? 0 : N;
			for (int i = 0; i < N; ++i)
			{
				const Vec2 Cue = Placements[i].Cue;
				const double Miss = MissValueAt(X, *S.From, Cue);
				const int F = PotFamilies(S, Cue, Families, kFamilyCapacity);
				// From a placement: its best families (the placement was made for one of them).
				const int Keep = F < 2 ? F : 2;
				for (int f = 0; f < Keep; ++f)
				{
					ExpandFamily(State, S, Families[f], Cue, true, Miss, Speeds, Spins);
				}
				if (i == 0 && !S.SecondPly && Placements[i].Score <= 0.0)
				{
					SafetyCandidates(State, S, Cue, true, P.SafetyTargets > 0 ? Scaled(P.SafetyTargets, X.Config.Breadth) : 2, P.SafetyTargets <= 0);
					if (P.Kicks)
					{
						KickCandidates(State, S, Cue, true);
					}
				}
			}
			if (static_cast<int>(State.Candidates.size()) == Before && N > 0)
			{
				FallbackCandidate(State, S, Placements[0].Cue, true);
			}
		}
		return static_cast<int>(State.Candidates.size()) - Before;
	}
}
