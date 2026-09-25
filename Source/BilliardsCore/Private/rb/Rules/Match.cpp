#include "rb/Core/FpGuard.h"
// Owner: WP-9 (rules table procedures & match). Spec: rules.md 4.2, 4.8, 4.10, 11, Reg 14/18/27.
#include "rb/Rules/Match.h"

#include "rb/Core/Random.h"
#include "rb/Math/Scalar.h"

namespace rb::rules
{
	namespace
	{
		constexpr BallId kEightBall = 8;

		constexpr bool IsPlayer(int Player) { return Player == 0 || Player == 1; }
		constexpr bool IsNineOrTen(Discipline Game) { return Game == Discipline::NineBall || Game == Discipline::TenBall; }
		constexpr bool CallsShots(Discipline Game)
		{
			return Game == Discipline::EightBall || Game == Discipline::TenBall || Game == Discipline::StraightPool;
		}
		constexpr bool SafetyCallAllowed(Discipline Game) { return Game == Discipline::EightBall || Game == Discipline::StraightPool; }
		constexpr bool HasThreeFoulRule(Discipline Game)
		{
			return Game == Discipline::NineBall || Game == Discipline::TenBall || Game == Discipline::StraightPool;
		}
		constexpr bool IsInHand(CueBallNext Cue) { return Cue != CueBallNext::InPosition; }

		constexpr BallGroup OtherGroup(BallGroup Group)
		{
			return Group == BallGroup::Solids ? BallGroup::Stripes : (Group == BallGroup::Stripes ? BallGroup::Solids : BallGroup::None);
		}

		constexpr std::uint32_t BallBit(int Ball) { return 1u << static_cast<unsigned>(Ball); }

		// Rack seed = hash(Seed, RackCounter) (deterministic, stored implicitly by the replay's root seed).
		constexpr std::uint64_t RackSeed(std::uint64_t Seed, std::uint64_t Counter)
		{
			std::uint64_t Mix = Seed ^ (0xD1B54A32D192ED03ull * (Counter + 1u));
			return SplitMix64Next(Mix);
		}

		constexpr CueBallNext BreakCueBall(Discipline Game)
		{
			return Game == Discipline::Blackball ? CueBallNext::InHandBaulk : CueBallNext::InHandAboveHeadString;
		}

		// Doubles (Reg 27): the member of Team who breaks the team's next rack (team breakers alternate,
		// member 0 breaks first).
		int NextTeamBreakMember(const MatchState& S, int Team) { return S.TeamBreaker[Team] < 0 ? 0 : 1 - S.TeamBreaker[Team]; }

		// Doubles: the member of Team who broke the team's current rack breaks again (re-racks of the same game).
		int SameTeamBreakMember(const MatchState& S, int Team) { return S.TeamBreaker[Team] < 0 ? S.ActiveMember[Team] : S.TeamBreaker[Team]; }

		int NextBreaker(BreakOrder Order, int PreviousBreaker, int RackWinner)
		{
			switch (Order)
			{
			case BreakOrder::Alternate: return 1 - PreviousBreaker;
			case BreakOrder::WinnerBreaks: return RackWinner;
			case BreakOrder::LoserBreaks: return 1 - RackWinner;
			}
			return 1 - PreviousBreaker;
		}

		void ResetStalemate(MatchState& S)
		{
			S.InningsWithoutProgress = 0;
			S.StalemateWarning = false;
			S.InningHadProgress = false;
		}

		// The inning of Team ends: stalemate heuristic (4.8) and, in Doubles, the team's next inning is
		// played by the partner (Reg 27: pass-backs therefore go to the partner).
		void EndInning(const MatchConfig& Config, MatchState& S, int Team)
		{
			if (S.InningHadProgress)
			{
				S.InningsWithoutProgress = 0;
			}
			else
			{
				++S.InningsWithoutProgress;
			}
			S.InningHadProgress = false;
			S.StalemateWarning = Config.Rules.StalemateInnings > 0 && S.InningsWithoutProgress >= Config.Rules.StalemateInnings;
			if (Config.Doubles)
			{
				S.ActiveMember[Team] = 1 - S.ActiveMember[Team];
			}
		}

		void EnterRackSetup(MatchState& S, int Breaker, MatchPhase Phase)
		{
			S.Phase = Phase;
			S.Game.RackBreaker = Breaker;
			S.Game.Shooter = Breaker;
			S.Decider = -1;
			S.PendingOutcome = ShotOutcome{};
			S.ShotAfterBreak = false;
			S.Clock.ExtensionActive = false;
			ResetStalemate(S);
		}

		void RackOver(const MatchConfig& Config, MatchState& S, int Winner)
		{
			++S.RackWins[Winner];
			bool MatchIsOver = false;
			if (Config.RacksPerSet > 0)
			{
				if (S.RackWins[Winner] >= Config.RacksPerSet)
				{
					++S.SetWins[Winner];
					S.RackWins[0] = 0;
					S.RackWins[1] = 0;
				}
				MatchIsOver = S.SetWins[Winner] >= (Config.SetsToWin > 0 ? Config.SetsToWin : 1);
			}
			else
			{
				MatchIsOver = S.RackWins[Winner] >= Config.RaceTo;
			}
			if (MatchIsOver)
			{
				S.Phase = MatchPhase::MatchOver;
				S.Winner = Winner;
				S.Decider = -1;
				S.PendingOutcome = ShotOutcome{};
				return;
			}
			const int Breaker = NextBreaker(Config.Rules.Breaks, S.Game.RackBreaker, Winner);
			EnterRackSetup(S, Breaker, MatchPhase::RackOver);
			if (Config.Doubles)
			{
				S.ActiveMember[Breaker] = NextTeamBreakMember(S, Breaker);
			}
		}

		// Ball status after the shot (F4 pocketed, F5 off table, final positions); balls without end
		// information keep their state.
		void ApplyFinalBallStatus(GameState& G, const ShotFacts& Facts)
		{
			BallStatus& Cue = G.Balls[kCueBallId];
			if (Facts.CueBallPocketed || Facts.CueBallOffTable)
			{
				Cue.Kind = BallStatusKind::Pocketed; // in hand for the next shot
			}
			else if (Facts.Balls[kCueBallId].EndStatus == BallEndStatus::OnTable)
			{
				Cue.Kind = BallStatusKind::OnTable;
				Cue.Position = Facts.Balls[kCueBallId].FinalPosition;
			}

			for (int b = 1; b < kRulesBallCount; ++b)
			{
				BallStatus& Ball = G.Balls[b];
				if (Ball.Kind != BallStatusKind::OnTable)
				{
					continue;
				}
				if (Facts.IsPocketed(b))
				{
					Ball.Kind = BallStatusKind::Pocketed; // incl. supported-over-pocket balls (F4)
				}
				else if (Facts.IsOffTable(b))
				{
					Ball.Kind = BallStatusKind::OutOfPlay;
				}
				else if (Facts.Balls[b].EndStatus == BallEndStatus::OnTable)
				{
					Ball.Position = Facts.Balls[b].FinalPosition;
				}
			}
		}

		// A racked ball overlaps a ball that stays on the table (OnTable in G and not part of Rack), per-ball
		// radii, overlap tolerance eps_overlap.
		bool RackOverlapsTableBall(const GameState& G, const RackAssignment& Rack, const RulesTable& Table, const RulesTolerances& Tolerances)
		{
			for (int j = 0; j < kRulesBallCount; ++j)
			{
				if (G.Balls[j].Kind != BallStatusKind::OnTable || Rack.Racked[j])
				{
					continue;
				}
				for (int b = 1; b < kRulesBallCount; ++b)
				{
					if (!Rack.Racked[b])
					{
						continue;
					}
					const double MinDist = Table.BallRadius[b] + Table.BallRadius[j] - Tolerances.PlacementOverlap;
					if (LengthSquared(Rack.Position[b] - G.Balls[j].Position) < MinDist * MinDist)
					{
						return true;
					}
				}
			}
			return false;
		}

		// 14.1 continuation rack (9.5) after spotting: rack the balls, move the 15th ball / the cue ball.
		ErrorCode ExecuteRackCommand(const MatchConfig& Config, MatchState& S, const RackCommand& Command)
		{
			if (Command.Kind == RackCommandKind::None)
			{
				return ErrorCode::Ok;
			}
			GameState& G = S.Game;
			const RulesTable& Table = Config.Table;

			int Fifteenth = Command.FifteenthBall;
			if (Fifteenth <= 0 || Fifteenth >= kRulesBallCount || G.Balls[Fifteenth].Kind != BallStatusKind::OnTable)
			{
				Fifteenth = -1;
				for (int b = 1; b < kRulesBallCount; ++b)
				{
					if (G.Balls[b].Kind == BallStatusKind::OnTable)
					{
						Fifteenth = b;
					}
				}
			}

			const bool Full = Command.Kind == RackCommandKind::Rerack15;
			std::uint32_t Mask = 0u;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if (Full || G.Balls[b].Kind != BallStatusKind::OnTable)
				{
					Mask |= BallBit(b);
				}
			}

			// The 15th ball and the cue ball go to their places first: they do not depend on the rack, and
			// the rack must not overlap whatever stays on the table.
			if (!Full && Fifteenth > 0)
			{
				if (Command.FifteenthBallPlacement == PlacementCommand::ToHeadSpot)
				{
					G.Balls[Fifteenth].Position = Table.HeadSpot;
				}
				else if (Command.FifteenthBallPlacement == PlacementCommand::ToCenterSpot)
				{
					G.Balls[Fifteenth].Position = Table.CenterSpot;
				}
			}

			BallStatus& Cue = G.Balls[kCueBallId];
			switch (Command.CueBallPlacement)
			{
			case PlacementCommand::ToHeadSpot:
				Cue.Kind = BallStatusKind::OnTable;
				Cue.Position = Table.HeadSpot;
				G.CueBall = CueBallNext::InPosition;
				break;
			case PlacementCommand::ToCenterSpot:
				Cue.Kind = BallStatusKind::OnTable;
				Cue.Position = Table.CenterSpot;
				G.CueBall = CueBallNext::InPosition;
				break;
			case PlacementCommand::InHandAboveHeadString:
				Cue.Kind = BallStatusKind::Pocketed; // picked up: in hand above the head string
				G.CueBall = CueBallNext::InHandAboveHeadString;
				break;
			case PlacementCommand::Keep:
			case PlacementCommand::IntoRack:
				break;
			}

			// Micro-gaps widen the rack slightly beyond the tight 15-ball outline (5.2) that decided "does not
			// interfere", so a ball kept just outside the outline could be overlapped. The tight rack never
			// reaches a ball outside the outline: in that case rack again without gaps (same seed, so the
			// same balls on the same sites).
			const std::uint64_t Seed = RackSeed(Config.Seed, S.RackCounter);
			RackAssignment Rack;
			ErrorCode Generated = GenerateStraightPoolRack(Table, Seed, !Full, Mask, Config.RackGaps, Rack);
			if (Succeeded(Generated) && RackOverlapsTableBall(G, Rack, Table, Config.Rules.Tolerances))
			{
				Generated = GenerateStraightPoolRack(Table, Seed, !Full, Mask, kRackGapNone, Rack);
			}
			if (!Succeeded(Generated))
			{
				return Generated;
			}
			++S.RackCounter;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if (Rack.Racked[b])
				{
					G.Balls[b].Kind = BallStatusKind::OnTable;
					G.Balls[b].Position = Rack.Position[b];
				}
			}

			// A new rack of 15 balls: one shot-clock extension per player again (INTERPRETATION, 4.10).
			S.Clock.ExtensionsUsed[0] = 0;
			S.Clock.ExtensionsUsed[1] = 0;
			return ErrorCode::Ok;
		}

		// 8-ball call legality (6.3, 6.4) at shot start.
		bool EightBallCallLegal(const MatchConfig& Config, const GameState& G, const ShotDeclaration& Declaration)
		{
			const int Ball = Declaration.Called.Ball;
			if (G.TableOpen)
			{
				// The 8 on an open table only with a (valid, separately checked) claim of a cleared group.
				return Ball != kEightBall || (Config.Rules.OpenTableEightGroupGoneException && Declaration.ClaimedClearedGroup != BallGroup::None);
			}
			const BallGroup Mine = G.Players[G.Shooter].Group;
			if (GroupCleared(G, Mine))
			{
				return Ball == kEightBall;
			}
			return GroupOf(Ball) == Mine;
		}
	}

	void StartMatch(const MatchConfig& Config, MatchState& State)
	{
		State = MatchState{};
		State.Game.Game = Config.Game;
		State.Game.Shooter = 0;
		State.Game.RackBreaker = 0;
		State.Game.CueBall = BreakCueBall(Config.Game);
		State.TeamBreaker[0] = -1;
		State.TeamBreaker[1] = -1;
		State.Phase = MatchPhase::Lag;
	}

	ErrorCode ApplyLagResult(const MatchConfig& /*Config*/, MatchState& State, const LagResult& Result)
	{
		if (State.Phase != MatchPhase::Lag)
		{
			return ErrorCode::InvalidState;
		}
		switch (Result.Outcome)
		{
		case LagOutcome::Relag: return ErrorCode::Ok;
		case LagOutcome::FirstWins: State.LagWinner = 0; break;
		case LagOutcome::SecondWins: State.LagWinner = 1; break;
		}
		State.Decider = State.LagWinner;
		State.Phase = MatchPhase::LagWinnerChooses;
		return ErrorCode::Ok;
	}

	ErrorCode ChooseBreaker(const MatchConfig& Config, MatchState& State, int Breaker)
	{
		if (State.Phase != MatchPhase::LagWinnerChooses)
		{
			return ErrorCode::InvalidState;
		}
		if (!IsPlayer(Breaker))
		{
			return ErrorCode::InvalidArgument;
		}
		if (State.FirstBreaker < 0)
		{
			State.FirstBreaker = Breaker;
		}
		EnterRackSetup(State, Breaker, MatchPhase::RackSetup);
		if (Config.Doubles)
		{
			State.ActiveMember[Breaker] = NextTeamBreakMember(State, Breaker);
		}
		return ErrorCode::Ok;
	}

	ErrorCode SetupRack(const MatchConfig& Config, MatchState& State, RackAssignment& OutRack)
	{
		if (State.Phase != MatchPhase::RackSetup && State.Phase != MatchPhase::RackOver)
		{
			OutRack = RackAssignment{};
			return ErrorCode::InvalidState;
		}
		const ErrorCode Generated =
			GenerateRack(Config.Game, Config.Rules, Config.Table, RackSeed(Config.Seed, State.RackCounter), false, Config.RackGaps, OutRack);
		if (!Succeeded(Generated))
		{
			return Generated;
		}
		const ErrorCode Entered = SetupRackWith(Config, State, OutRack);
		if (Succeeded(Entered))
		{
			++State.RackCounter;
		}
		return Entered;
	}

	ErrorCode SetupRackWith(const MatchConfig& Config, MatchState& State, const RackAssignment& Rack)
	{
		if (State.Phase != MatchPhase::RackSetup && State.Phase != MatchPhase::RackOver)
		{
			return ErrorCode::InvalidState;
		}
		GameState& G = State.Game;
		const int Breaker = IsPlayer(G.RackBreaker) ? G.RackBreaker : 0;

		for (int b = 0; b < kRulesBallCount; ++b)
		{
			G.Balls[b] = BallStatus{};
		}
		for (int b = 1; b < kRulesBallCount; ++b)
		{
			if (Rack.Racked[b])
			{
				G.Balls[b].Kind = BallStatusKind::OnTable;
				G.Balls[b].Position = Rack.Position[b];
			}
		}
		G.Balls[kCueBallId].Kind = BallStatusKind::Pocketed; // in hand, not yet placed
		G.Balls[kCueBallId].Position = Config.Table.HeadSpot;

		G.Game = Config.Game;
		G.Shooter = Breaker;
		G.RackBreaker = Breaker;
		G.CueBall = BreakCueBall(Config.Game);
		G.IsBreakShot = true;
		G.PushOutAvailable = false;
		G.TableOpen = true;
		G.FreeShot = false;
		G.VisitsRemaining = 0;
		for (PlayerState& P : G.Players)
		{
			P.Group = BallGroup::None;
			if (Config.Game != Discipline::StraightPool)
			{
				P.ConsecutiveFouls = 0; // fouls count only within one rack (R 3.13); 14.1 keeps them
			}
		}

		State.Clock = ShotClockState{};
		State.ShotAfterBreak = false;
		State.Decider = -1;
		State.PendingOutcome = ShotOutcome{};
		ResetStalemate(State);
		if (Config.Doubles)
		{
			State.TeamBreaker[Breaker] = State.ActiveMember[Breaker];
		}
		++State.RackNumber;
		State.Phase = MatchPhase::AwaitShot;
		return ErrorCode::Ok;
	}

	ShotConstraints GetShotConstraints(const MatchConfig& Config, const MatchState& State)
	{
		ShotConstraints C;
		if (State.Phase != MatchPhase::AwaitShot)
		{
			return C;
		}
		const GameState& G = State.Game;
		const Discipline Game = Config.Game;
		const int Me = G.Shooter;
		const bool CueOnTable = G.Balls[kCueBallId].Kind == BallStatusKind::OnTable;

		C.PlacementRegion = G.CueBall;
		C.PlacementChoices = CueBallChoiceBit(G.CueBall);
		C.FreeShot = G.FreeShot;
		if (G.FreeShot) // Blackball free shot: from where it lies or in hand in baulk
		{
			C.PlacementChoices = static_cast<std::uint8_t>(C.PlacementChoices | CueBallChoiceBit(CueBallNext::InHandBaulk));
			if (CueOnTable)
			{
				C.PlacementChoices = static_cast<std::uint8_t>(C.PlacementChoices | CueBallChoiceBit(CueBallNext::InPosition));
			}
			C.PlacementRegion = CueOnTable ? CueBallNext::InPosition : CueBallNext::InHandBaulk;
		}
		C.VisitsRemaining = G.VisitsRemaining;
		C.Member = (Config.Doubles && IsPlayer(Me)) ? State.ActiveMember[Me] : 0;

		const bool OnEight = Game == Discipline::EightBall && !G.TableOpen && IsPlayer(Me) && GroupCleared(G, G.Players[Me].Group);
		C.CallRequired = CallsShots(Game) && !G.IsBreakShot &&
			(Config.Rules.Calls == CallMode::Explicit || (Config.Rules.Calls == CallMode::EightOnly && OnEight));
		C.SafetyAllowed = SafetyCallAllowed(Game) && !G.IsBreakShot;
		C.PushOutAllowed = IsNineOrTen(Game) && G.PushOutAvailable && !G.IsBreakShot;

		const ShotDeclaration Plain;
		C.LegalFirstContactMask = LegalFirstContactMask(Config.Rules, G, Plain);
		C.MayRequestSpot = SpotRequestCandidate(G, C.LegalFirstContactMask, Config.Table, Config.Rules.Tolerances) >= 0;
		C.MayClaimClearedGroup = Game == Discipline::EightBall && G.TableOpen && !G.IsBreakShot && Config.Rules.OpenTableEightGroupGoneException &&
			(GroupCleared(G, BallGroup::Solids) || GroupCleared(G, BallGroup::Stripes));
		C.ThreeFoulWarning = Config.Rules.ThreeFoulRule && HasThreeFoulRule(Game) && IsPlayer(Me) && G.Players[Me].ConsecutiveFouls >= 2;
		C.ShotClockAllowed = ShotClockAllowed(Config, State);
		return C;
	}

	void CompleteDeclaration(const MatchConfig& Config, const MatchState& State, ShotDeclaration& Declaration)
	{
		const GameState& G = State.Game;
		if (Config.Game != Discipline::EightBall || State.Phase != MatchPhase::AwaitShot || !G.TableOpen || G.IsBreakShot ||
			!Config.Rules.OpenTableEightGroupGoneException || Declaration.Called.Ball != kEightBall ||
			Declaration.ClaimedClearedGroup != BallGroup::None)
		{
			return;
		}
		if (GroupCleared(G, BallGroup::Solids))
		{
			Declaration.ClaimedClearedGroup = BallGroup::Solids;
		}
		else if (GroupCleared(G, BallGroup::Stripes))
		{
			Declaration.ClaimedClearedGroup = BallGroup::Stripes;
		}
	}

	ErrorCode ValidateDeclaration(const MatchConfig& Config, const MatchState& State, const ShotDeclaration& Declaration, const Vec2* PlacedCueBall)
	{
		if (State.Phase != MatchPhase::AwaitShot)
		{
			return ErrorCode::InvalidState;
		}
		const GameState& G = State.Game;
		const Discipline Game = Config.Game;
		if (!IsPlayer(G.Shooter))
		{
			return ErrorCode::InvalidState;
		}

		// Shot kind (pitfalls 9, 10, 16; N19, T11).
		switch (Declaration.Kind)
		{
		case ShotKind::Break:
			if (!G.IsBreakShot)
			{
				return ErrorCode::InvalidDeclaration;
			}
			break;
		case ShotKind::Normal:
			break; // on a break shot a Normal declaration is the break
		case ShotKind::PushOut:
			if (G.IsBreakShot || !IsNineOrTen(Game) || !G.PushOutAvailable)
			{
				return ErrorCode::InvalidDeclaration;
			}
			break;
		case ShotKind::Safety:
			if (G.IsBreakShot || !SafetyCallAllowed(Game))
			{
				return ErrorCode::InvalidDeclaration;
			}
			break;
		}

		// Claimed cleared group (8-ball open table, R 4.4).
		if (Declaration.ClaimedClearedGroup != BallGroup::None)
		{
			if (Game != Discipline::EightBall || !G.TableOpen || G.IsBreakShot || !Config.Rules.OpenTableEightGroupGoneException ||
				!GroupCleared(G, Declaration.ClaimedClearedGroup))
			{
				return ErrorCode::InvalidDeclaration;
			}
			// A claim means "I play the 8 on this shot" (the 8 becomes the only legal first ball): it cannot
			// go with a call of another ball.
			if (Declaration.Kind == ShotKind::Normal && Declaration.Called.Ball != kNoBall && Declaration.Called.Ball != kEightBall)
			{
				return ErrorCode::InvalidDeclaration;
			}
		}

		// Calls (4.5): 9-ball and Blackball have none; the 8/10-ball break has none.
		const bool CallMatters = CallsShots(Game) && !(G.IsBreakShot && Game != Discipline::StraightPool) &&
			Declaration.Kind != ShotKind::Safety && Declaration.Kind != ShotKind::PushOut;
		if (CallMatters)
		{
			const int Ball = Declaration.Called.Ball;
			const bool HasBall = Ball != kNoBall;
			const bool HasPocket = Declaration.Called.Pocket != PocketId::None;
			if (HasBall != HasPocket)
			{
				return ErrorCode::InvalidDeclaration;
			}
			if (HasBall)
			{
				if (Ball < 1 || Ball >= kRulesBallCount || G.Balls[Ball].Kind != BallStatusKind::OnTable ||
					static_cast<int>(Declaration.Called.Pocket) >= kPocketCount)
				{
					return ErrorCode::InvalidDeclaration;
				}
				if (Game == Discipline::EightBall && !G.IsBreakShot && !EightBallCallLegal(Config, G, Declaration))
				{
					return ErrorCode::InvalidDeclaration;
				}
			}
			else if (!G.IsBreakShot)
			{
				const ShotConstraints C = GetShotConstraints(Config, State);
				if (C.CallRequired)
				{
					return ErrorCode::InvalidDeclaration;
				}
			}
		}

		// Cue-ball placement (F11, 16.16).
		const bool CueOnTable = G.Balls[kCueBallId].Kind == BallStatusKind::OnTable;
		CueBallNext Region = G.CueBall;
		bool MayPlayFromPosition = !IsInHand(G.CueBall);
		if (G.FreeShot) // Blackball free shot: from where it lies or in hand in baulk
		{
			MayPlayFromPosition = CueOnTable;
			Region = CueBallNext::InHandBaulk;
		}
		if (PlacedCueBall == nullptr)
		{
			if (!MayPlayFromPosition)
			{
				return ErrorCode::InvalidDeclaration;
			}
		}
		else
		{
			if (!IsInHand(Region))
			{
				return ErrorCode::InvalidDeclaration; // the cue ball is played from where it lies
			}
			if (Config.Rules.Input != InputMode::Sim &&
				!CueBallPlacementLegal(G, *PlacedCueBall, Region, Config.Table, Config.Rules.Tolerances))
			{
				return ErrorCode::InvalidDeclaration;
			}
		}
		return ErrorCode::Ok;
	}

	ErrorCode ApplyShot(const MatchConfig& Config, MatchState& State, const ShotOutcome& Outcome, const ShotFacts& Facts)
	{
		if (State.Phase != MatchPhase::AwaitShot || !IsPlayer(State.Game.Shooter))
		{
			return ErrorCode::InvalidState;
		}
		MatchState S = State; // committed only on success
		GameState& G = S.Game;
		const int Me = G.Shooter;
		const int Opp = 1 - Me;
		const bool WasBreak = G.IsBreakShot;

		// Scores, the shooter's consecutive-foul counter, groups (pitfall 24: assigned after evaluation).
		G.Players[0].Score += Outcome.ScoreDelta[0];
		G.Players[1].Score += Outcome.ScoreDelta[1];
		G.Players[Me].ConsecutiveFouls = Outcome.FoulsAfter[Me];
		if (Outcome.AssignShooterGroup != BallGroup::None)
		{
			G.Players[Me].Group = Outcome.AssignShooterGroup;
			G.Players[Opp].Group = OtherGroup(Outcome.AssignShooterGroup);
			G.TableOpen = false;
		}

		// Balls at the end of the shot, then spotting (4.3) in the evaluator's order.
		ApplyFinalBallStatus(G, Facts);
		SpotBalls(G, Outcome.BallsToSpot.Data(), Outcome.BallsToSpot.Size(), Config.Table, Config.Rules.Tolerances);

		// Next-shot flags.
		G.IsBreakShot = false;
		G.PushOutAvailable = Outcome.NextPushOutAvailable; // exactly the next shot (pitfall 9)
		G.FreeShot = Outcome.NextFreeShot;
		G.VisitsRemaining = Outcome.NextVisits;
		G.CueBall = Outcome.NextCueBall;
		S.ShotAfterBreak = WasBreak;
		S.Clock.ExtensionActive = false;
		S.InningHadProgress = S.InningHadProgress || Facts.AnyObjectBallPocketed || Outcome.AnyFoul;

		// 14.1 continuation rack (after spotting, pitfall 11), only while the rack stays in play. The
		// Rerack15 of a RerackAndBreak outcome (three-foul penalty, rules.md 10.6) is the NEW rack that
		// SetupRack builds for the opening break; executing it here as well would rack twice (and consume
		// a rack seed for a rack that is thrown away).
		const bool RackStaysInPlay =
			Outcome.Next == NextAction::Continue || Outcome.Next == NextAction::Pass || Outcome.Next == NextAction::AwaitDecision;
		if (RackStaysInPlay)
		{
			const ErrorCode Racked = ExecuteRackCommand(Config, S, Outcome.Rack);
			if (!Succeeded(Racked))
			{
				return Racked;
			}
		}

		const int Next = IsPlayer(Outcome.NextShooter) ? Outcome.NextShooter : -1;
		switch (Outcome.Next)
		{
		case NextAction::Continue:
			G.Shooter = Next >= 0 ? Next : Me;
			if (G.Shooter != Me)
			{
				EndInning(Config, S, Me);
			}
			S.Phase = MatchPhase::AwaitShot;
			break;
		case NextAction::Pass:
			G.Shooter = Next >= 0 ? Next : Opp;
			if (G.Shooter != Me)
			{
				EndInning(Config, S, Me);
			}
			S.Phase = MatchPhase::AwaitShot;
			break;
		case NextAction::AwaitDecision:
			S.Decider = Next >= 0 ? Next : Opp;
			S.PendingOutcome = Outcome;
			if (S.Decider != Me)
			{
				EndInning(Config, S, Me);
			}
			S.Phase = MatchPhase::AwaitDecision;
			break;
		case NextAction::RackWon:
			if (!IsPlayer(Outcome.Winner))
			{
				return ErrorCode::InvalidArgument;
			}
			RackOver(Config, S, Outcome.Winner);
			break;
		case NextAction::MatchWon:
			S.Winner = IsPlayer(Outcome.Winner) ? Outcome.Winner : Me;
			S.Decider = -1;
			S.Phase = MatchPhase::MatchOver;
			break;
		case NextAction::RerackAndBreak:
		{
			// 14.1 three-foul penalty (offender breaks), Blackball black on the break, 8-ball variants.
			const int Breaker = Next >= 0 ? Next : Me;
			EnterRackSetup(S, Breaker, MatchPhase::RackSetup);
			if (Config.Doubles && Breaker != Me)
			{
				S.ActiveMember[Breaker] = NextTeamBreakMember(S, Breaker);
			}
			break;
		}
		}

		State = S;
		return ErrorCode::Ok;
	}

	ErrorCode ApplyOption(const MatchConfig& Config, MatchState& State, Option Choice)
	{
		if (State.Phase != MatchPhase::AwaitDecision || !IsPlayer(State.Decider))
		{
			return ErrorCode::InvalidState;
		}
		bool Offered = false;
		for (const Option O : State.PendingOutcome.Options)
		{
			Offered = Offered || O == Choice;
		}
		if (!Offered)
		{
			return ErrorCode::InvalidOption;
		}

		MatchState S = State;
		GameState& G = S.Game;
		const int Decider = S.Decider;
		const int Other = 1 - Decider;
		const ShotOutcome& Pending = State.PendingOutcome;
		const RulesTolerances& Tolerances = Config.Rules.Tolerances;

		const auto Shoot = [&](int Shooter, CueBallNext Cue, bool PushOut) {
			G.Shooter = Shooter;
			G.CueBall = Cue;
			G.PushOutAvailable = PushOut;
			G.IsBreakShot = false;
			S.Phase = MatchPhase::AwaitShot;
			S.Decider = -1;
			S.PendingOutcome = ShotOutcome{};
		};
		const auto Rerack = [&](int Breaker, bool SameMember) {
			if (Config.Doubles)
			{
				S.ActiveMember[Breaker] = SameMember ? SameTeamBreakMember(S, Breaker) : S.ActiveMember[Breaker];
			}
			EnterRackSetup(S, Breaker, MatchPhase::RackSetup);
		};

		switch (Choice)
		{
		case Option::AcceptTable: Shoot(Decider, Pending.CueBallIfAccepted, false); break;
		case Option::BallInHandAboveHeadString: Shoot(Decider, CueBallNext::InHandAboveHeadString, false); break;
		case Option::RerackDeciderBreaks: Rerack(Decider, false); break;
		case Option::RerackOffenderBreaks: Rerack(Other, true); break;
		case Option::Spot8ContinueFromPosition:
			SpotBalls(G, &kEightBall, 1, Config.Table, Tolerances);
			Shoot(Decider, CueBallNext::InPosition, false);
			break;
		case Option::Spot8BallInHandAboveHeadString:
			SpotBalls(G, &kEightBall, 1, Config.Table, Tolerances);
			Shoot(Decider, CueBallNext::InHandAboveHeadString, false);
			break;
		case Option::AcceptTableNoPushOut: Shoot(Decider, CueBallNext::InPosition, false); break;
		case Option::HandBackPushOutAllowed: Shoot(Other, CueBallNext::InPosition, true); break;
		case Option::ShootFromPosition: Shoot(Decider, CueBallNext::InPosition, false); break;
		case Option::PassBack: Shoot(Other, CueBallNext::InPosition, false); break;
		case Option::RequireRebreak: Rerack(Other, true); break;
		}
		S.Clock.ExtensionActive = false;
		State = S;
		return ErrorCode::Ok;
	}

	ErrorCode RequestSpot(const MatchConfig& Config, MatchState& State)
	{
		if (State.Phase != MatchPhase::AwaitShot)
		{
			return ErrorCode::InvalidState;
		}
		GameState& G = State.Game;
		const ShotDeclaration Plain;
		const std::uint32_t Legal = LegalFirstContactMask(Config.Rules, G, Plain);
		const int Ball = SpotRequestCandidate(G, Legal, Config.Table, Config.Rules.Tolerances);
		if (Ball < 0)
		{
			return ErrorCode::InvalidOption;
		}
		const BallId Spotted = static_cast<BallId>(Ball);
		SpotBalls(G, &Spotted, 1, Config.Table, Config.Rules.Tolerances);
		return ErrorCode::Ok;
	}

	ErrorCode DeclareStalemate(const MatchConfig& Config, MatchState& State)
	{
		if (State.Phase != MatchPhase::AwaitShot && State.Phase != MatchPhase::AwaitDecision)
		{
			return ErrorCode::InvalidState;
		}
		if (Config.Game == Discipline::StraightPool)
		{
			// R 7.12: new lag, the winner decides who takes an opening break; scores (and foul counters,
			// rules.md 14 #17) carried over.
			State.Phase = MatchPhase::Lag;
			State.LagWinner = -1;
			State.Decider = -1;
			State.PendingOutcome = ShotOutcome{};
			State.ShotAfterBreak = false;
			State.Clock.ExtensionActive = false;
			ResetStalemate(State);
			return ErrorCode::Ok;
		}
		// R 4.11, 5.9, 6.11: the original breaker of the rack breaks again, no score change.
		const int Breaker = IsPlayer(State.Game.RackBreaker) ? State.Game.RackBreaker : 0;
		if (Config.Doubles)
		{
			State.ActiveMember[Breaker] = SameTeamBreakMember(State, Breaker);
		}
		EnterRackSetup(State, Breaker, MatchPhase::RackSetup);
		return ErrorCode::Ok;
	}

	void Concede(MatchState& State, int Player)
	{
		// R 1.12: conceding loses the match. A finished match keeps its result.
		if (State.Phase == MatchPhase::MatchOver || !IsPlayer(Player))
		{
			return;
		}
		State.Phase = MatchPhase::MatchOver;
		State.Winner = 1 - Player;
		State.Decider = -1;
		State.PendingOutcome = ShotOutcome{};
	}

	double ShotClockAllowed(const MatchConfig& Config, const MatchState& State)
	{
		const ShotClockConfig& Clock = Config.Clock;
		if (!Clock.Enabled)
		{
			return 0.0;
		}
		double Allowed = Clock.ShotTime;
		if (State.ShotAfterBreak)
		{
			Allowed = Max(Allowed, Min(Clock.AfterBreakMax, 60.0)); // never more than 60 s (Reg 18)
		}
		if (State.Clock.ExtensionActive)
		{
			Allowed += Clock.ExtensionTime;
		}
		return Allowed;
	}

	bool ShotClockExpired(const MatchConfig& Config, const MatchState& State, double Elapsed)
	{
		return Config.Clock.Enabled && Elapsed > ShotClockAllowed(Config, State);
	}

	ErrorCode RequestShotClockExtension(const MatchConfig& Config, MatchState& State)
	{
		const int Me = State.Game.Shooter;
		if (!Config.Clock.Enabled || State.Phase != MatchPhase::AwaitShot || !IsPlayer(Me) || State.Clock.ExtensionActive ||
			State.Clock.ExtensionsUsed[Me] >= Config.Clock.ExtensionsPerRack)
		{
			return ErrorCode::InvalidOption;
		}
		State.Clock.ExtensionActive = true;
		++State.Clock.ExtensionsUsed[Me];
		return ErrorCode::Ok;
	}

	double ShotClockStartTime(const ShotEndSnapshot& PreviousShotEnd)
	{
		// StopTime is the instant the last ball stopped moving AND spinning (R 2.19, Reg 18), so a ball
		// still spinning in place delays the clock start (C03).
		return PreviousShotEnd.StopTime;
	}
}
