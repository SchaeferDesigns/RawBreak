#include "rb/Core/FpGuard.h"
// Owner: WP-12 (AI opponent). The planner's stages, deterministic reduction and choice (Docs/architecture.md 7.6).
#include "PlannerInternal.h"

#include "rb/Human/NoiseHash.h"
#include "rb/Math/Scalar.h"
#include "rb/Rules/Evaluate.h"

#include <cstdio>

namespace rb::ai
{
	namespace
	{
		constexpr std::uint64_t kChoiceChannel = 0xA1C0u; // HashKeys channel of the seeded choice among near-equal candidates
		constexpr int kMaxChoices = 8;
		constexpr double kRolloutTiltTolerance = 5e-4;    // [m] tilt chain tolerance of AI rollouts (architecture 5.2)

		ResultCapacity WorkerCapacity()
		{
			ResultCapacity C;
			C.MaxLoggedEvents = 512;     // EventStates for the aim measurement: the first contacts are enough
			C.MaxSegmentsPerBall = 0;    // no trajectories
			C.MaxRecordEvents = 4096;    // breaks
			C.MaxCueTipSegments = 8;
			return C;
		}

		bool IsPotType(ShotType T) { return IsPotShot(T); }
		bool IsSafetyType(ShotType T) { return T == ShotType::Safety || T == ShotType::Kick || T == ShotType::PushOut; }

		int ScaledCount(int Value, double Scale, int Cap)
		{
			if (Value <= 0)
			{
				return 0;
			}
			const int S = static_cast<int>(Floor(Value * Scale + 0.5));
			return S < 1 ? 1 : (S > Cap ? Cap : S);
		}

		// Better = higher score; ties: lower index.
		bool Better(const PlannerState& S, int A, int B)
		{
			const double Sa = S.Results[static_cast<std::size_t>(A)].Score;
			const double Sb = S.Results[static_cast<std::size_t>(B)].Score;
			return Sa > Sb || (Sa == Sb && A < B);
		}

		// The candidates the final choice may take: those with noisy samples when the noisy stage ran, else every candidate of the
		// decision's table.
		bool Eligible(const PlannerState& S, int Index)
		{
			if (Index >= S.DecisionCandidates)
			{
				return false;
			}
			return S.NoisyRows.empty() || S.Results[static_cast<std::size_t>(Index)].Noisy;
		}

		// Top Count eligible candidates (best first) into Out; returns how many.
		int TopCandidates(const PlannerState& S, int* Out, int Count, bool PotsOnly)
		{
			int N = 0;
			for (int c = 0; c < S.DecisionCandidates; ++c)
			{
				if (!Eligible(S, c) || (PotsOnly && !IsPotType(S.Candidates[static_cast<std::size_t>(c)].Type)))
				{
					continue;
				}
				if (N == Count && !Better(S, c, Out[N - 1]))
				{
					continue;
				}
				int j = N < Count ? N : Count - 1;
				while (j > 0 && Better(S, c, Out[j - 1]))
				{
					Out[j] = Out[j - 1];
					--j;
				}
				Out[j] = c;
				N = N < Count ? N + 1 : Count;
			}
			return N;
		}

		double FaultValueOf(const PlanContext& X, const rules::MatchState& M)
		{
			rules::GameState G = M.Game;
			G.Shooter = 1 - X.Self;
			G.Balls[0].Kind = rules::BallStatusKind::Pocketed;
			G.CueBall = rules::CueBallNext::InHandAnywhere;
			G.IsBreakShot = false;
			G.PushOutAvailable = false;
			return 1.0 - MoverWinProbability(X.Eval, G);
		}

		void SetOrigin(PlannerState& S, int Index, const rules::MatchState& M, int Parent)
		{
			Origin& O = S.Origins[Index];
			O.State = M;
			O.Layout = MakeBallLayout(M.Game, S.Ctx.Input.Match.Table);
			O.FaultValue = FaultValueOf(S.Ctx, M);
			O.Parent = Parent;
			O.StaticValue = StateValue(S.Ctx.Eval, M, S.Ctx.Self);
		}

		void QueueScreenJobs(PlannerState& S, int First, StageKind Kind)
		{
			S.Jobs.clear();
			for (int c = First; c < static_cast<int>(S.Candidates.size()); ++c)
			{
				Job J;
				J.Kind = Kind;
				J.Candidate = c;
				S.Jobs.push_back(J);
			}
			S.Results.resize(S.Candidates.size());
			S.Stage = Kind;
		}

		ConsideredShot Considered(const PlannerState& S, int c)
		{
			const Candidate& C = S.Candidates[static_cast<std::size_t>(c)];
			const CandidateResult& R = S.Results[static_cast<std::size_t>(c)];
			ConsideredShot Out;
			Out.Type = C.Type;
			Out.FirstBall = C.FirstBall;
			Out.PotBall = C.PotBall;
			Out.Pocket = C.Pocket;
			Out.Score = R.Score;
			Out.PotChance = R.Noisy ? R.PotShare : (IsPotType(C.Type) ? C.PerceivedPot : 0.0);
			Out.FoulChance = R.Noisy ? R.FoulShare : (R.Foul ? 1.0 : 0.0);
			Out.Speed = C.Plan.Speed;
			Out.SpinA = C.Plan.AxisOffsetA;
			Out.SpinB = C.Plan.AxisOffsetB;
			Out.Samples = R.Samples + 1;
			return Out;
		}

		void FinishDecision(PlannerState& S)
		{
			const PlanContext& X = S.Ctx;
			PlannedDecision& D = S.Decision;
			int Top[kMaxChoices] = {};
			const int N = TopCandidates(S, Top, kMaxChoices, false);
			if (N == 0)
			{
				D.Error = ErrorCode::InvalidState;
				D.Kind = DecisionKind::None;
				S.Stage = StageKind::Done;
				S.Finished = true;
				return;
			}
			int Chosen = Top[0];
			if (X.Profile.ChoiceTolerance > 0.0)
			{
				const double Floor0 = S.Results[static_cast<std::size_t>(Top[0])].Score - X.Profile.ChoiceTolerance;
				int Near = 1;
				while (Near < N && S.Results[static_cast<std::size_t>(Top[Near])].Score >= Floor0)
				{
					++Near;
				}
				const human::NoiseKey& K = X.Input.Key;
				const double U = human::U01(human::HashKeys(K.MatchSeed, static_cast<std::uint64_t>(K.RackIndex), static_cast<std::uint64_t>(K.ShotIndex),
					static_cast<std::uint64_t>(K.ShooterId), kChoiceChannel));
				const int Pick = static_cast<int>(U * Near);
				Chosen = Top[Pick < Near ? Pick : Near - 1];
			}
			const Candidate& C = S.Candidates[static_cast<std::size_t>(Chosen)];
			const CandidateResult& R = S.Results[static_cast<std::size_t>(Chosen)];
			D.Error = ErrorCode::Ok;
			D.Kind = DecisionKind::Stroke;
			D.Stroke = C.Plan;
			D.Stroke.Azimuth = R.Azimuth;
			D.Situation = C.Situation;
			D.Declaration = C.Declaration;
			D.PlaceCueBall = C.PlaceCueBall;
			D.CueBallPlacement = C.CueBall;
			D.Type = C.Type;
			D.TargetBall = C.FirstBall;
			D.PotBall = C.PotBall;
			D.Pocket = C.Pocket;
			D.ExpectedValue = Clamp(R.Score - (IsSafetyType(C.Type) ? X.Profile.SafetyBias : 0.0), 0.0, 1.0);
			D.PotChance = R.Noisy ? R.PotShare : (IsPotType(C.Type) ? C.PerceivedPot : 0.0);
			D.FoulChance = R.Noisy ? R.FoulShare : (R.Foul ? 1.0 : 0.0);
			PlanReasoning& Why = D.Reasoning;
			Why.Top.Clear();
			for (int i = 0; i < N && i < kReasoningShots; ++i)
			{
				Why.Top.PushBack(Considered(S, Top[i]));
			}
			Why.Candidates = static_cast<int>(S.Candidates.size());
			Why.NoisyCandidates = static_cast<int>(S.NoisyRows.size());
			Why.SecondPlyPositions = S.SecondPlyPositions;
			Why.Placements = S.Placements;
			Why.Sandbagging = X.Sandbagging;
			Why.ChoseSafety = IsSafetyType(C.Type) && C.Type != ShotType::PushOut;
			Why.ChosePushOut = C.Type == ShotType::PushOut;
			for (const CandidateResult& Each : S.Results)
			{
				Why.Simulations += Each.Simulations + Each.Samples;
			}
			Why.Simulations += S.SecondPlyPositions;
			S.Stage = StageKind::Done;
			S.Finished = true;
		}

		void DecideOption(PlannerState& S)
		{
			const PlanContext& X = S.Ctx;
			const rules::MatchState& M = X.Input.State;
			PlannedDecision& D = S.Decision;
			double Best = -1.0;
			for (const rules::Option Choice : M.PendingOutcome.Options)
			{
				rules::MatchState Copy = M;
				if (rules::ApplyOption(X.Input.Match, Copy, Choice) != ErrorCode::Ok)
				{
					continue;
				}
				const double V = StateValue(X.Eval, Copy, X.Self);
				if (V > Best)
				{
					Best = V;
					D.Choice = Choice;
				}
			}
			D.Error = Best >= 0.0 ? ErrorCode::Ok : ErrorCode::InvalidState;
			D.Kind = Best >= 0.0 ? DecisionKind::Option : DecisionKind::None;
			D.ExpectedValue = Best >= 0.0 ? Best : 0.0;
			D.Reasoning.TableValue = StateValue(X.Eval, M, X.Self);
			S.Stage = StageKind::Done;
			S.Finished = true;
		}

		// The lag winner chooses the first breaker: the break is worth having in 8/9/10-ball and Blackball (the breaker's typical
		// value of the rack model); in 14.1 the opening break is a safety break that opens the rack for the other player, so the
		// AI lets the other player break.
		void DecideBreaker(PlannerState& S)
		{
			const PlanContext& X = S.Ctx;
			PlannedDecision& D = S.Decision;
			const int Breaker = X.Input.Match.Game == rules::Discipline::StraightPool ? 1 - X.Self : X.Self;
			rules::MatchState Copy = X.Input.State;
			const bool Ok = rules::ChooseBreaker(X.Input.Match, Copy, Breaker) == ErrorCode::Ok;
			D.Error = Ok ? ErrorCode::Ok : ErrorCode::InvalidState;
			D.Kind = Ok ? DecisionKind::ChooseBreaker : DecisionKind::None;
			D.Breaker = Ok ? Breaker : -1;
			D.ExpectedValue = Ok ? StateValue(X.Eval, Copy, X.Self) : 0.0;
			S.Stage = StageKind::Done;
			S.Finished = true;
		}

		// Second ply: the noise-free end states of the top candidates in which the AI keeps the table get their own next-shot
		// search. Returns true if jobs were queued.
		bool StartSecondPly(PlannerState& S, WorkerState& Serial)
		{
			const int K = S.Ctx.SecondPly;
			if (K <= 0)
			{
				return false;
			}
			int Top[kMaxSecondPly] = {};
			const int N = TopCandidates(S, Top, K < kMaxSecondPly ? K : kMaxSecondPly, true);
			const int First = static_cast<int>(S.Candidates.size());
			for (int i = 0; i < N && S.OriginCount < kMaxOrigins; ++i)
			{
				const int c = Top[i];
				const Candidate& C = S.Candidates[static_cast<std::size_t>(c)];
				rules::MatchState End;
				const RolloutOutcome Out = Rollout(S, C, S.Results[static_cast<std::size_t>(c)].Azimuth, kNoiseFreeSample, false, Serial, &End);
				++S.SecondPlyPositions;
				if (!Out.Kept || End.Phase != rules::MatchPhase::AwaitShot || End.Game.Shooter != S.Ctx.Self)
				{
					continue;
				}
				const int O = S.OriginCount++;
				SetOrigin(S, O, End, c);
				GenerateCandidates(S, O, c);
			}
			if (static_cast<int>(S.Candidates.size()) == First)
			{
				return false;
			}
			QueueScreenJobs(S, First, StageKind::SecondPly);
			return true;
		}

		void FinishSecondPly(PlannerState& S)
		{
			for (int o = 1; o < S.OriginCount; ++o)
			{
				const Origin& O = S.Origins[o];
				double Next = -1.0;
				for (int c = S.DecisionCandidates; c < static_cast<int>(S.Candidates.size()); ++c)
				{
					if (S.Candidates[static_cast<std::size_t>(c)].Origin == o)
					{
						Next = Max(Next, S.Results[static_cast<std::size_t>(c)].Score);
					}
				}
				if (Next < 0.0 || O.Parent < 0)
				{
					continue;
				}
				CandidateResult& P = S.Results[static_cast<std::size_t>(O.Parent)];
				const Candidate& PC = S.Candidates[static_cast<std::size_t>(O.Parent)];
				// The share of the parent's score that assumed the static value of a kept table.
				const double Weight = P.Noisy ? P.KeptShare : (P.Potted && !P.Foul ? PC.PerceivedPot : (P.Kept ? 1.0 : 0.0));
				P.Score += Weight * (Next - O.StaticValue);
			}
		}

		// Mean value, pot, foul and kept shares of every noisy row, summed in sample order (the same for any thread count).
		void ReduceNoisy(PlannerState& S)
		{
			const int K = S.Ctx.Samples;
			for (int r = 0; r < static_cast<int>(S.NoisyRows.size()); ++r)
			{
				const int c = S.NoisyRows[static_cast<std::size_t>(r)];
				const SampleResult* Row = &S.SampleTable[static_cast<std::size_t>(r) * kMaxSamples];
				double Sum = 0.0;
				int Potted = 0;
				int Fouls = 0;
				int Kept = 0;
				for (int s = 0; s < K; ++s)
				{
					Sum += Row[s].Value;
					Potted += Row[s].Potted ? 1 : 0;
					Fouls += Row[s].Foul ? 1 : 0;
					Kept += Row[s].Kept ? 1 : 0;
				}
				CandidateResult& R = S.Results[static_cast<std::size_t>(c)];
				const double InvK = 1.0 / K;
				R.Noisy = true;
				R.Samples = K;
				R.Score = Sum * InvK + (IsSafetyType(S.Candidates[static_cast<std::size_t>(c)].Type) ? S.Ctx.Profile.SafetyBias : 0.0);
				R.PotShare = Potted * InvK;
				R.FoulShare = Fouls * InvK;
				R.KeptShare = Kept * InvK;
			}
		}

		bool NextAfterCandidates(PlannerState& S, WorkerState& Serial)
		{
			if (StartSecondPly(S, Serial))
			{
				return true;
			}
			FinishDecision(S);
			return false;
		}

		// Planned simulations of the decision (upper bound) and the deterministic budget trim: second ply first, then the noisy set,
		// then the candidates from the end of the list.
		void ApplyBudget(PlannerState& S)
		{
			PlanContext& X = S.Ctx;
			const int Budget = X.Config.SimulationBudget > 0 ? X.Config.SimulationBudget : X.Profile.SimulationBudget;
			if (Budget <= 0)
			{
				return;
			}
			const auto ScreenCost = [&](int Count) {
				int Sum = 0;
				for (int c = 0; c < Count; ++c)
				{
					Sum += 1 + (S.Candidates[static_cast<std::size_t>(c)].AimCorrect ? X.Profile.AimIterations : 0);
				}
				return Sum;
			};
			const int PerPly = X.Profile.SecondPlyFamilies * 2 * 3 * (1 + X.Profile.AimIterations) + 1;
			int Count = static_cast<int>(S.Candidates.size());
			const auto Total = [&] { return ScreenCost(Count) + X.NoisyCandidates * X.Samples + X.SecondPly * PerPly; };
			while (Total() > Budget && X.SecondPly > 0)
			{
				--X.SecondPly;
			}
			while (Total() > Budget && X.NoisyCandidates > 1)
			{
				--X.NoisyCandidates;
			}
			while (Total() > Budget && Count > 1)
			{
				--Count;
			}
			S.Candidates.resize(static_cast<std::size_t>(Count));
		}
	}

	PlannerState::PlannerState()
	{
		Candidates.reserve(kMaxCandidates);
		Results.reserve(kMaxCandidates);
		SampleTable.resize(static_cast<std::size_t>(kMaxNoisyCandidates) * kMaxSamples);
		NoisyRows.reserve(kMaxNoisyCandidates);
		Jobs.reserve(kMaxCandidates);
	}

	WorkerState::WorkerState() : Sim(WorkerCapacity())
	{
		ReserveShotResult(Result, WorkerCapacity());
	}

	// ------------------------------------------------------------------------------------------------------------------------
	// PlannerWorker / PlannerScratch
	// ------------------------------------------------------------------------------------------------------------------------

	PlannerWorker::PlannerWorker() : State(new WorkerState()) {}

	PlannerWorker::~PlannerWorker() { delete State; }

	PlannerWorker::PlannerWorker(PlannerWorker&& Other) noexcept : State(Other.State) { Other.State = nullptr; }

	PlannerWorker& PlannerWorker::operator=(PlannerWorker&& Other) noexcept
	{
		if (this != &Other)
		{
			delete State;
			State = Other.State;
			Other.State = nullptr;
		}
		return *this;
	}

	PlannerScratch::PlannerScratch() : State(new PlannerState()) {}

	PlannerScratch::~PlannerScratch() { delete State; }

	PlannerScratch::PlannerScratch(PlannerScratch&& Other) noexcept : State(Other.State), Serial(static_cast<PlannerWorker&&>(Other.Serial))
	{
		Other.State = nullptr;
	}

	PlannerScratch& PlannerScratch::operator=(PlannerScratch&& Other) noexcept
	{
		if (this != &Other)
		{
			delete State;
			State = Other.State;
			Other.State = nullptr;
			Serial = static_cast<PlannerWorker&&>(Other.Serial);
		}
		return *this;
	}

	PlannerWorker& PlannerScratch::SerialWorker() { return Serial; }

	ErrorCode PlannerScratch::Begin(const PlannerInput& Input, const PlannerConfig& Config)
	{
		PlannerState& S = *State;
		S.Candidates.clear();
		S.Results.clear();
		S.NoisyRows.clear();
		S.Jobs.clear();
		S.OriginCount = 0;
		S.DecisionCandidates = 0;
		S.Placements = 0;
		S.SecondPlyPositions = 0;
		S.Decision = PlannedDecision{};
		S.Stage = StageKind::Idle;
		S.Finished = true;

		PlanContext& X = S.Ctx;
		X.Input = Input;
		X.Config = Config;
		X.Self = Input.Self;
		if (Input.Table == nullptr || Input.Self < 0 || Input.Self > 1 || Input.State.Game.Game != Input.Match.Game)
		{
			S.Decision.Error = ErrorCode::InvalidArgument;
			return S.Decision.Error;
		}

		// Effective profile: the hustler sandbags until money is down (5.5, Q6); config scaling.
		X.Profile = Input.Planner;
		X.Sandbagging = Input.Planner.Sandbagger && Input.Money == human::MoneyGames::SideBetsAndHustling && !Input.MoneyDown;
		if (X.Sandbagging)
		{
			X.Profile = SandbaggingProfile(Input.Planner);
		}
		X.Samples = ScaledCount(X.Profile.NoisySamples, Config.Samples, kMaxSamples);
		X.NoisyCandidates = X.Samples > 0 ? ScaledCount(X.Profile.NoisyCandidates, Config.Samples, kMaxNoisyCandidates) : 0;
		X.SecondPly = Config.SecondPly && X.Profile.PositionDepth >= 3 ? (X.Profile.SecondPlyCandidates < kMaxSecondPly ? X.Profile.SecondPlyCandidates : kMaxSecondPly) : 0;

		// The planning model (HF 3.8, 5.5): the level table unless it knows the slope, the AI rollout tilt tolerance, no chalk-mark
		// cling; the simplified model has no ball-ball throw and nominal ball masses (and no squirt, Rollout).
		X.Planning = Input.Physics;
		if (!X.Profile.KnowsTableSlope)
		{
			X.Planning.Tilt.Slope = Vec2{};
			X.Planning.Tilt.NapPseudoSlope = Vec2{};
		}
		X.Planning.Tilt.Tolerance = Max(X.Planning.Tilt.Tolerance, kRolloutTiltTolerance);
		X.Planning.ChalkCling = false;
		if (!X.Profile.ModelsThrowAndSquirt)
		{
			X.Planning.BallBall.Friction = BallBallFrictionModel::None;
		}
		for (int b = 0; b < kMaxBalls; ++b)
		{
			X.Specs[b] = X.Profile.ModelsThrowAndSquirt ? Input.Balls[b] : MakeBallSpec(Input.Balls[b].Radius, kDefaultBallMass);
		}
		X.Attributes = Input.Character.Profile.Attributes;

		// Both players of the static evaluator.
		X.Eval.Table = Input.Table;
		X.Eval.Match = &X.Input.Match;
		EvalPlayer& Me = X.Eval.Players[X.Self];
		Me.AimSigma = X.Profile.PerceivedAimSigma;
		Me.RunoutRate = X.Profile.RunoutRate;
		Me.SafetyQuality = X.Profile.SafetyQuality;
		Me.KickSkill = X.Profile.KickSkill;
		Me.PlaysSafeties = X.Profile.Safeties;
		Me.PositionDepth = X.Profile.PositionDepth < 2 ? X.Profile.PositionDepth : 2;
		EvalPlayer& Other = X.Eval.Players[1 - X.Self];
		Other.AimSigma = Input.Opponent.AimSigma;
		Other.RunoutRate = Input.Opponent.RunoutRate;
		Other.SafetyQuality = Input.Opponent.SafetyQuality;
		Other.KickSkill = Input.Opponent.KickSkill;
		Other.PlaysSafeties = Input.Opponent.PlaysSafeties;
		Other.PositionDepth = 1;

		const rules::MatchState& M = X.Input.State;
		if (M.Phase == rules::MatchPhase::AwaitDecision && M.Decider == X.Self)
		{
			DecideOption(S);
			return S.Decision.Error;
		}
		if (M.Phase == rules::MatchPhase::Lag)
		{
			// Both players lag at once: the AI plans its own ball (a short serial search on the calling thread).
			PlanLag(S, *Serial.Internal());
			return S.Decision.Error;
		}
		if (M.Phase == rules::MatchPhase::LagWinnerChooses && M.Decider == X.Self)
		{
			DecideBreaker(S);
			return S.Decision.Error;
		}
		if (M.Phase != rules::MatchPhase::AwaitShot || M.Game.Shooter != X.Self)
		{
			S.Decision.Error = ErrorCode::InvalidState;
			return S.Decision.Error;
		}

		S.Finished = false;
		S.OriginCount = 1;
		SetOrigin(S, 0, M, -1);
		S.Decision.Reasoning.TableValue = S.Origins[0].StaticValue;
		S.Decision.Reasoning.BestPotChance = BestNextShot(X.Eval, M.Game, X.Self).PotChance;
		GenerateCandidates(S, 0, -1);
		ApplyBudget(S);
		S.DecisionCandidates = static_cast<int>(S.Candidates.size());
		if (S.DecisionCandidates == 0)
		{
			S.Decision.Error = ErrorCode::InvalidState;
			S.Finished = true;
			return S.Decision.Error;
		}
		QueueScreenJobs(S, 0, StageKind::Screen);
		return ErrorCode::Ok;
	}

	int PlannerScratch::JobCount() const { return State->Finished ? 0 : static_cast<int>(State->Jobs.size()); }

	void PlannerScratch::RunJob(int JobIndex, PlannerWorker& Worker)
	{
		PlannerState& S = *State;
		if (S.Finished || JobIndex < 0 || JobIndex >= static_cast<int>(S.Jobs.size()))
		{
			return;
		}
		const Job& J = S.Jobs[static_cast<std::size_t>(JobIndex)];
		WorkerState& W = *Worker.Internal();
		switch (J.Kind)
		{
		case StageKind::Screen:
		case StageKind::SecondPly:
			ScreenCandidate(S, J.Candidate, W, S.Results[static_cast<std::size_t>(J.Candidate)]);
			break;
		case StageKind::Noisy:
		{
			const Candidate& C = S.Candidates[static_cast<std::size_t>(J.Candidate)];
			const double Azimuth = S.Results[static_cast<std::size_t>(J.Candidate)].Azimuth;
			SampleResult* Row = &S.SampleTable[static_cast<std::size_t>(J.Slot) * kMaxSamples];
			for (int s = 0; s < S.Ctx.Samples; ++s)
			{
				const RolloutOutcome Out = Rollout(S, C, Azimuth, s, false, W, nullptr);
				Row[s].Value = Out.Value;
				Row[s].Potted = Out.Potted;
				Row[s].Foul = Out.Foul;
				Row[s].Kept = Out.Kept;
				Row[s].Valid = true;
			}
			break;
		}
		case StageKind::Idle:
		case StageKind::Done:
			break;
		}
	}

	bool PlannerScratch::Advance()
	{
		PlannerState& S = *State;
		if (S.Finished)
		{
			return false;
		}
		WorkerState& SerialState = *Serial.Internal();
		switch (S.Stage)
		{
		case StageKind::Screen:
		{
			const int M = S.Ctx.NoisyCandidates;
			if (S.Ctx.Samples > 0 && M > 0)
			{
				int Top[kMaxNoisyCandidates] = {};
				// Every decision candidate is eligible before the noisy stage (NoisyRows is empty).
				const int N = TopCandidates(S, Top, M < kMaxNoisyCandidates ? M : kMaxNoisyCandidates, false);
				S.NoisyRows.clear();
				S.Jobs.clear();
				for (int r = 0; r < N; ++r)
				{
					S.NoisyRows.push_back(Top[r]);
					Job J;
					J.Kind = StageKind::Noisy;
					J.Candidate = Top[r];
					J.Slot = r;
					S.Jobs.push_back(J);
				}
				S.Stage = StageKind::Noisy;
				return true;
			}
			return NextAfterCandidates(S, SerialState);
		}
		case StageKind::Noisy:
			ReduceNoisy(S);
			return NextAfterCandidates(S, SerialState);
		case StageKind::SecondPly:
			FinishSecondPly(S);
			FinishDecision(S);
			return false;
		case StageKind::Idle:
		case StageKind::Done:
			break;
		}
		FinishDecision(S);
		return false;
	}

	void PlannerScratch::FinishNow()
	{
		PlannerState& S = *State;
		if (S.Finished)
		{
			return;
		}
		switch (S.Stage)
		{
		case StageKind::Noisy:
			ReduceNoisy(S);
			break;
		case StageKind::SecondPly:
			FinishSecondPly(S);
			break;
		case StageKind::Screen:
		case StageKind::Idle:
		case StageKind::Done:
			break;
		}
		FinishDecision(S);
	}

	bool PlannerScratch::Finished() const { return State->Finished; }

	PlannerProgress PlannerScratch::Progress() const
	{
		const PlannerState& S = *State;
		PlannerProgress P;
		switch (S.Stage)
		{
		case StageKind::Idle: P.Stage = PlannerStage::Idle; break;
		case StageKind::Screen: P.Stage = PlannerStage::Screening; break;
		case StageKind::Noisy: P.Stage = PlannerStage::NoisySamples; break;
		case StageKind::SecondPly: P.Stage = PlannerStage::SecondPly; break;
		case StageKind::Done: P.Stage = PlannerStage::Done; break;
		}
		P.Jobs = S.Finished ? 0 : static_cast<int>(S.Jobs.size());
		P.Candidates = static_cast<int>(S.Candidates.size());
		// Screening results exist once the screening stage was reduced (the stage that runs next is a later one, or the end).
		const bool Screened = S.Stage == StageKind::Noisy || S.Stage == StageKind::SecondPly || (S.Stage == StageKind::Done && S.DecisionCandidates > 0);
		if (Screened)
		{
			int Top[1] = {};
			if (TopCandidates(S, Top, 1, false) == 1)
			{
				const Candidate& C = S.Candidates[static_cast<std::size_t>(Top[0])];
				P.HasBest = true;
				P.Best = Considered(S, Top[0]);
				P.BestCueBall = C.CueBall;
				P.BestAzimuth = S.Results[static_cast<std::size_t>(Top[0])].Azimuth;
			}
		}
		return P;
	}

	const PlannedDecision& PlannerScratch::Decision() const { return State->Decision; }

	PlannedDecision PlanShot(const PlannerInput& Input, const PlannerConfig& Config, PlannerScratch& Scratch)
	{
		Scratch.Begin(Input, Config);
		while (!Scratch.Finished())
		{
			const int N = Scratch.JobCount();
			for (int j = 0; j < N; ++j)
			{
				Scratch.RunJob(j, Scratch.SerialWorker());
			}
			Scratch.Advance();
		}
		return Scratch.Decision();
	}

	const char* ShotTypeName(ShotType Type)
	{
		switch (Type)
		{
		case ShotType::Pot: return "pot";
		case ShotType::Bank: return "bank";
		case ShotType::Combination: return "combination";
		case ShotType::Kick: return "kick";
		case ShotType::Safety: return "safety";
		case ShotType::Break: return "break";
		case ShotType::PushOut: return "push-out";
		case ShotType::Kiss: return "kiss";
		case ShotType::Carom: return "carom";
		case ShotType::Lag: return "lag";
		}
		return "?";
	}

	int FormatReasoning(const PlannedDecision& D, char* Buffer, int Size)
	{
		if (Buffer == nullptr || Size <= 0)
		{
			return 0;
		}
		int N = 0;
		if (D.Kind == DecisionKind::Option)
		{
			N = std::snprintf(Buffer, static_cast<std::size_t>(Size), "option %d: P(win) %.2f", static_cast<int>(D.Choice), D.ExpectedValue);
		}
		else if (D.Kind == DecisionKind::ChooseBreaker)
		{
			N = std::snprintf(Buffer, static_cast<std::size_t>(Size), "player %d breaks: P(win) %.2f", D.Breaker, D.ExpectedValue);
		}
		else if (D.Kind == DecisionKind::Stroke && D.Type == ShotType::Lag)
		{
			N = std::snprintf(Buffer, static_cast<std::size_t>(Size), "lag from (%.3f, %.3f), V %.2f m/s to %.3f m from the head cushion: P(win) %.2f; %d simulations",
				D.CueBallPlacement.x, D.CueBallPlacement.y, D.Stroke.Speed, D.LagDistance, D.ExpectedValue, D.Reasoning.Simulations);
		}
		else if (D.Kind == DecisionKind::Stroke)
		{
			N = std::snprintf(Buffer, static_cast<std::size_t>(Size),
				"%s ball %d -> %d pocket %d%s, V %.2f m/s, A %.2f B %.2f: P(win) %.2f, pot %.2f, foul %.2f; %d candidates, %d simulations%s",
				ShotTypeName(D.Type), static_cast<int>(D.TargetBall), static_cast<int>(D.PotBall), static_cast<int>(D.Pocket),
				D.PlaceCueBall ? " (ball in hand)" : "", D.Stroke.Speed, D.Stroke.AxisOffsetA, D.Stroke.AxisOffsetB, D.ExpectedValue, D.PotChance,
				D.FoulChance, D.Reasoning.Candidates, D.Reasoning.Simulations, D.Reasoning.Sandbagging ? " (sandbagging)" : "");
		}
		else
		{
			N = std::snprintf(Buffer, static_cast<std::size_t>(Size), "no decision (error %d)", static_cast<int>(D.Error));
		}
		return N < 0 ? 0 : (N >= Size ? Size - 1 : N);
	}
}
