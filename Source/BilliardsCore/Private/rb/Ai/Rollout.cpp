#include "rb/Core/FpGuard.h"
// Owner: WP-12 (AI opponent). One planner rollout (Docs/architecture.md 7.6): the planned stroke through the synthetic hand (noisy
// samples, rollout keys) or perfect execution (NoiseScale 0), human::ExecuteStroke, Simulator::Run on the planning model, then
// rules::DeriveShotFacts / EvaluateShot / ApplyShot and the static value of the end state. Also the throw- / squirt-aware aim
// correction by the simulator and the screening score. The AI builds strikes only through ExecuteStroke (HF-B07).
#include "PlannerInternal.h"

#include "rb/Human/AiProfiles.h"
#include "rb/Human/HumanModel.h"
#include "rb/Math/Scalar.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/ShotFacts.h"

namespace rb::ai
{
	namespace
	{
		// Purpose sample index of the noise-free rollouts' key: a rollout key (never the match stream), never read (NoiseScale 0).
		constexpr std::uint32_t kNoiseFreeKeySample = 0xFFFFu;
		constexpr double kMaxAimStep = 2.0 * kDegToRad;    // largest aim change of one correction step
		constexpr double kSlopeStep = 1.0e-5;              // [rad] finite difference of the geometric slope

		human::IntendedStroke PerfectStroke(const human::PlannedStroke& Plan)
		{
			human::IntendedStroke I;
			I.Azimuth = Plan.Azimuth;
			I.Elevation = Plan.Elevation;
			I.AxisOffsetA = Plan.AxisOffsetA;
			I.AxisOffsetB = Plan.AxisOffsetB;
			I.Speed = Plan.Speed;
			I.TimeDown = 2.0;
			I.ForwardStart = 1.8;
			I.SettleStart = -1.0;
			I.PauseDuration = 0.5;
			I.ContactAcceleration = 0.0;
			I.HeadMovedBeforeContact = false;
			return I;
		}

		CueBallInHand InHandOf(rules::CueBallNext Region)
		{
			switch (Region)
			{
			case rules::CueBallNext::InHandAnywhere: return CueBallInHand::Anywhere;
			case rules::CueBallNext::InHandAboveHeadString: return CueBallInHand::AboveHeadString;
			case rules::CueBallNext::InHandBaulk: return CueBallInHand::Baulk;
			case rules::CueBallNext::InPosition: break;
			}
			return CueBallInHand::Anywhere;
		}

		double WrapAngle(double A)
		{
			while (A > kPi)
			{
				A -= 2.0 * kPi;
			}
			while (A < -kPi)
			{
				A += 2.0 * kPi;
			}
			return A;
		}

		// A ball moving from From along the unit direction U first touches Object (plan view, exact ghost-ball geometry, no
		// throw): its centre at the contact and the unit line of centres (the struck ball's direction); false if it misses.
		bool GeometricContact(const Vec2& From, const Vec2& U, const Vec2& Object, double RadiusSum, Vec2& Contact, Vec2& Normal)
		{
			const Vec2 Rel = Object - From;
			const double Along = Dot(Rel, U);
			const double Perp = Cross(U, Rel);
			if (Along <= 0.0 || Abs(Perp) >= RadiusSum)
			{
				return false;
			}
			const double S = Along - Sqrt(RadiusSum * RadiusSum - Perp * Perp);
			Contact = From + U * S;
			Normal = (Object - Contact) / RadiusSum;
			return true;
		}

		// Unit direction a ball leaves a stun contact with (the tangent line: its incoming direction U minus the part along the
		// line of centres N); false for a full hit.
		bool TangentLine(const Vec2& U, const Vec2& N, Vec2& Out)
		{
			const Vec2 T = U - N * Dot(U, N);
			const double L = Length(T);
			if (L < 1e-9)
			{
				return false;
			}
			Out = T / L;
			return true;
		}

		// Direction [rad] of the measured ball after its measured contact when the cue ball leaves along Azimuth (the plan-view
		// chain of the candidate's type: ghost-ball contacts, straight paths, stun tangent lines; no throw, no curve).
		bool GeometricDirection(const PlannerState& State, const Candidate& C, double Azimuth, double& Direction)
		{
			const Origin& O = State.Origins[C.Origin];
			const BallLayout& L = O.Layout;
			const int First = C.FirstBall;
			if (First == kNoBall || ((L.OnTable >> First) & 1u) == 0u)
			{
				return false;
			}
			const double Rc = State.Ctx.Specs[0].Radius;
			const Vec2 U{Cos(Azimuth), Sin(Azimuth)};
			Vec2 CueContact;
			Vec2 FirstDir;
			if (!GeometricContact(C.CueBall, U, L.Position[First], Rc + L.Radius[First], CueContact, FirstDir))
			{
				return false;
			}
			Vec2 Out = FirstDir;
			if (C.Type == ShotType::Kiss)
			{
				// The first ball runs into the kissed ball and leaves along the tangent line.
				const int K = C.MeasureStriker;
				Vec2 Contact;
				Vec2 Normal;
				if (K == kNoBall || ((L.OnTable >> K) & 1u) == 0u ||
					!GeometricContact(L.Position[First], FirstDir, L.Position[K], L.Radius[First] + L.Radius[K], Contact, Normal) || !TangentLine(FirstDir, Normal, Out))
				{
					return false;
				}
			}
			else if (C.Type == ShotType::Carom)
			{
				// The cue ball leaves the first ball along the tangent line into the measured ball.
				const int B = C.MeasureBall;
				Vec2 Tangent;
				Vec2 Contact;
				if (B == kNoBall || ((L.OnTable >> B) & 1u) == 0u || !TangentLine(U, FirstDir, Tangent) ||
					!GeometricContact(CueContact, Tangent, L.Position[B], Rc + L.Radius[B], Contact, Out))
				{
					return false;
				}
			}
			Direction = Atan2(Out.y, Out.x);
			return true;
		}

		// d(direction of the measured ball) / d(azimuth) from the exact geometry (central difference; one-sided at the edges).
		double GeometricSlope(const PlannerState& State, const Candidate& C, double Azimuth)
		{
			double Plus = 0.0;
			double Minus = 0.0;
			double Mid = 0.0;
			const bool HasPlus = GeometricDirection(State, C, Azimuth + kSlopeStep, Plus);
			const bool HasMinus = GeometricDirection(State, C, Azimuth - kSlopeStep, Minus);
			const bool HasMid = GeometricDirection(State, C, Azimuth, Mid);
			if (HasPlus && HasMinus)
			{
				return WrapAngle(Plus - Minus) / (2.0 * kSlopeStep);
			}
			if (HasPlus && HasMid)
			{
				return WrapAngle(Plus - Mid) / kSlopeStep;
			}
			if (HasMinus && HasMid)
			{
				return WrapAngle(Mid - Minus) / kSlopeStep;
			}
			return 0.0;
		}

		void BuildInput(const PlanContext& X, const rules::GameState& G, const Candidate& C, bool Measure, SimInput& In)
		{
			In.Table = X.Input.Table;
			In.Environment = EnvironmentSpec{};
			In.Params = X.Planning;
			for (int b = 0; b < kMaxBalls; ++b)
			{
				SimBall& Ball = In.Balls[b];
				Ball.InPlay = false;
				Ball.ChalkMarks.Clear();
				Ball.Orientation = Quat{};
				Ball.State = BallState{};
				Ball.Spec = X.Specs[b];
				const bool OnTable = b < rules::kRulesBallCount && G.Balls[b].Kind == rules::BallStatusKind::OnTable;
				if (b == 0 || OnTable)
				{
					const Vec2 P = b == 0 ? C.CueBall : G.Balls[b].Position;
					Ball.InPlay = true;
					Ball.State.Position = ToVec3(P, X.Specs[b].Radius);
					Ball.State.State = MotionState::Stationary;
				}
			}
			In.Strikes.Clear();
			In.Context = ShotContext{};
			if (C.PlaceCueBall)
			{
				In.Context.InHand = InHandOf(G.CueBall);
				In.Context.PlacedPosition = C.CueBall;
			}
			In.Record.Trajectories = false;
			In.Record.EventStates = Measure;
			In.Record.LogTransitions = false;
			In.Record.LogObservers = false;
			In.Record.ShotRecord = true;
		}

		// The measured contact (the first one between MeasureBall and MeasureStriker, after the cue ball's first contact, which must
		// be FirstBall): the measured ball's post-contact direction against the wanted one.
		void Measure(const ShotResult& R, const Candidate& C, RolloutOutcome& Out)
		{
			bool CueTouched = false;
			for (const ShotEvent& E : R.Events)
			{
				if (E.Type != ShotEventType::BallBall)
				{
					continue;
				}
				const bool CueEvent = E.A == kCueBallId || E.B == kCueBallId;
				if (!CueTouched)
				{
					if (!CueEvent)
					{
						continue; // e.g. the rack's balls settling before the cue ball arrives
					}
					if ((E.A == kCueBallId ? E.B : E.A) != C.FirstBall)
					{
						return; // the cue ball touched another ball first
					}
					CueTouched = true;
					if (!(C.MeasureStriker == kCueBallId && C.MeasureBall == C.FirstBall))
					{
						continue;
					}
				}
				else if (!((E.A == C.MeasureBall && E.B == C.MeasureStriker) || (E.B == C.MeasureBall && E.A == C.MeasureStriker)))
				{
					continue;
				}
				const BallState& Post = E.A == C.MeasureBall ? E.Post[0] : E.Post[1];
				const Vec2 V = XY(Post.Velocity);
				const Vec2 Want = C.MeasureTarget - XY(Post.Position);
				if (LengthSquared(V) < 1e-12 || LengthSquared(Want) < 1e-12)
				{
					return;
				}
				Out.Contact = true;
				Out.DirectionError = WrapAngle(Atan2(V.y, V.x) - Atan2(Want.y, Want.x));
				return;
			}
		}
	}

	RolloutOutcome Rollout(const PlannerState& State, const Candidate& C, double Azimuth, int Sample, bool MeasureAim, WorkerState& W, rules::MatchState* EndState)
	{
		const PlanContext& X = State.Ctx;
		const Origin& O = State.Origins[C.Origin];
		const rules::GameState& G = O.State.Game;
		RolloutOutcome Out;
		Out.Value = O.FaultValue;
		Out.Foul = true;

		human::PlannedStroke Plan = C.Plan;
		Plan.Azimuth = Azimuth;
		human::HumanParams Human = X.Input.Human;
		human::CueBodyState Body = X.Input.CueBody;
		human::NoiseKey Key;
		human::IntendedStroke Intended;
		if (Sample < 0)
		{
			// Perfect execution in the AI's own model: no human layer, no hand flaws, a straight cue.
			Key = human::RolloutKey(X.Input.Key, kNoiseFreeKeySample);
			Human.NoiseScale = 0.0;
			Body.BowSag = 0.0;
			Intended = PerfectStroke(Plan);
		}
		else
		{
			Key = human::RolloutKey(X.Input.Key, static_cast<std::uint32_t>(Sample));
			Intended = human::SyntheticHand(Plan, X.Input.Character, C.Situation, X.Specs[0].Radius, Key, human::NoiseHistory{}, Human);
		}
		int Obstacles = 0;
		for (int b = 1; b < rules::kRulesBallCount; ++b)
		{
			if (G.Balls[b].Kind == rules::BallStatusKind::OnTable)
			{
				human::BallObstacle& Ob = W.Obstacles[Obstacles++];
				Ob.Id = static_cast<BallId>(b);
				Ob.Position = ToVec3(G.Balls[b].Position, X.Specs[b].Radius);
				Ob.Radius = X.Specs[b].Radius;
			}
		}
		const human::ExecutedStroke Executed = human::ExecuteStroke(Intended, X.Attributes, C.Situation, X.Input.Tip, Body, X.Input.Cue, X.Specs[0],
			ToVec3(C.CueBall, X.Specs[0].Radius), W.Obstacles, Obstacles, Key, human::NoiseHistory{}, Human);
		if (Executed.Error != ErrorCode::Ok)
		{
			return Out;
		}

		BuildInput(X, G, C, MeasureAim, W.In);
		StrikeRequest Request;
		Request.Ball = static_cast<BallId>(kCueBallId);
		Request.Input = Executed.Strike;
		Request.Input.SquirtEnabled = Request.Input.SquirtEnabled && X.Profile.ModelsThrowAndSquirt; // the simplified model has no squirt
		W.In.Strikes.PushBack(Request);
		Out.Status = W.Sim.Run(W.In, W.Result);
		if (Out.Status != SimStatus::Ok || W.Result.Record.Truncated)
		{
			return Out;
		}
		if (MeasureAim)
		{
			Measure(W.Result, C, Out);
		}

		const rules::MatchConfig& M = X.Input.Match;
		rules::DeriveShotFacts(W.Result.Record, M.Table, M.Rules.Tolerances, kInfinity, W.Facts);
		const rules::ShotOutcome Outcome = rules::EvaluateShot(M.Rules, M.Table, G, C.Declaration, W.Facts);
		Out.Foul = Outcome.AnyFoul;
		Out.Potted = C.PotBall != kNoBall && W.Facts.IsPocketed(C.PotBall) && (C.Pocket == PocketId::None || W.Facts.PocketOf(C.PotBall) == C.Pocket);
		if (Outcome.Next == rules::NextAction::RackWon || Outcome.Next == rules::NextAction::MatchWon)
		{
			Out.Value = Outcome.Winner == X.Self ? 1.0 : 0.0;
			Out.Won = Outcome.Winner == X.Self;
			Out.Kept = false;
			return Out;
		}
		W.After = O.State;
		if (rules::ApplyShot(M, W.After, Outcome, W.Facts) != ErrorCode::Ok)
		{
			Out.Value = O.FaultValue;
			Out.Foul = true;
			return Out;
		}
		Out.Value = StateValue(X.Eval, W.After, X.Self);
		Out.Kept = W.After.Phase == rules::MatchPhase::AwaitShot && W.After.Game.Shooter == X.Self;
		if (EndState != nullptr)
		{
			*EndState = W.After;
		}
		return Out;
	}

	double ScreenScore(const PlanContext& Context, const Candidate& C, const CandidateResult& R)
	{
		// A noise-free rack win the stroke did not plan (the 9 dropping on a safety, a bank that caroms the 9 in) is luck of one
		// exact stroke: it counts like keeping the table.
		const bool Planned = C.GameBall && R.Potted;
		const double Value = R.Won && !Planned ? Min(R.Value, C.KeepValue) : R.Value;
		switch (C.Type)
		{
		case ShotType::Pot:
		case ShotType::Bank:
		case ShotType::Combination:
		case ShotType::Kiss:
		case ShotType::Carom:
			// It works in the AI's mind's eye: weigh it by the make chance it believes in ("percentage intuition"); a stroke that
			// does not even work noise-free is at best a miss.
			return R.Potted && !R.Foul ? C.PerceivedPot * Value + (1.0 - C.PerceivedPot) * C.MissValue : Min(Value, C.MissValue);
		case ShotType::Safety:
		case ShotType::Kick:
		case ShotType::PushOut:
			return Value + Context.Profile.SafetyBias;
		case ShotType::Break:
			break;
		}
		return Value;
	}

	void ScreenCandidate(const PlannerState& State, int Index, WorkerState& W, CandidateResult& Out)
	{
		const PlanContext& X = State.Ctx;
		const Candidate& C = State.Candidates[static_cast<std::size_t>(Index)];
		Out = CandidateResult{};
		const int Iterations = C.AimCorrect ? X.Profile.AimIterations : 0;
		double Phi = C.Plan.Azimuth;
		RolloutOutcome Current = Rollout(State, C, Phi, kNoiseFreeSample, Iterations > 0, W, nullptr);
		int Simulations = 1;
		double BestPhi = Phi;
		RolloutOutcome Best = Current;
		if (Iterations > 0)
		{
			const double Geometric = GeometricSlope(State, C, Phi);
			double PrevPhi = Phi;
			double PrevError = Current.DirectionError;
			for (int it = 0; it < Iterations; ++it)
			{
				if (!Current.Contact || Abs(Current.DirectionError) < kAimTolerance || Geometric == 0.0)
				{
					break;
				}
				double Slope = Geometric;
				if (it > 0 && Abs(Phi - PrevPhi) > 1e-12)
				{
					const double Secant = (Current.DirectionError - PrevError) / (Phi - PrevPhi);
					if (Secant * Geometric > 0.0 && Abs(Secant) > 0.1 * Abs(Geometric))
					{
						Slope = Secant;
					}
				}
				const double Step = Clamp(-Current.DirectionError / Slope, -kMaxAimStep, kMaxAimStep);
				PrevPhi = Phi;
				PrevError = Current.DirectionError;
				Phi += Step;
				Current = Rollout(State, C, Phi, kNoiseFreeSample, true, W, nullptr);
				++Simulations;
				if (Current.Contact && (!Best.Contact || Abs(Current.DirectionError) < Abs(Best.DirectionError)))
				{
					Best = Current;
					BestPhi = Phi;
				}
			}
		}
		Out.Azimuth = BestPhi;
		Out.Value = Best.Value;
		Out.Potted = Best.Potted;
		Out.Won = Best.Won;
		Out.Foul = Best.Foul;
		Out.Kept = Best.Kept;
		Out.AimError = Best.DirectionError;
		Out.Simulations = Simulations;
		Out.KeptShare = Best.Kept ? 1.0 : 0.0;
		Out.PotShare = Best.Potted ? 1.0 : 0.0;
		Out.FoulShare = Best.Foul ? 1.0 : 0.0;
		Out.Score = ScreenScore(X, C, Out);
	}

	// ------------------------------------------------------------------------------------------------------------------------
	// The lag
	// ------------------------------------------------------------------------------------------------------------------------

	namespace
	{
		constexpr double kLagMinSpeed = 0.4;        // [m/s] tip speed scan of the lag ...
		constexpr double kLagMaxSpeed = 3.0;        // ...
		constexpr double kLagScanStep = 0.1;        // [m/s]
		constexpr int kLagBisections = 8;
		constexpr double kOpponentLag = 0.15;       // [m] mean rest distance of the other lag ball (exponential; TUNING)
		constexpr double kLagTargetScales[4] = {1.0, 0.5, 1.6, 2.4};

		struct LagTry
		{
			double Distance = kInfinity; // rest distance from the head cushion's nose [m]
			bool Bad = true;
			bool Short = false;          // did not reach the foot cushion
			bool Long = false;           // touched the head cushion or rests past its nose
			bool Valid = false;          // simulated
		};

		// One lag stroke of the candidate at Speed: perfect execution (Sample < 0) or a noisy sample of the AI's own hand.
		LagTry LagRollout(const PlannerState& State, const Candidate& C, double Speed, int Sample, WorkerState& W)
		{
			const PlanContext& X = State.Ctx;
			LagTry Out;
			human::PlannedStroke Plan = C.Plan;
			Plan.Speed = Speed;
			human::HumanParams Human = X.Input.Human;
			human::CueBodyState Body = X.Input.CueBody;
			human::NoiseKey Key;
			human::IntendedStroke Intended;
			if (Sample < 0)
			{
				Key = human::RolloutKey(X.Input.Key, kNoiseFreeKeySample);
				Human.NoiseScale = 0.0;
				Body.BowSag = 0.0;
				Intended = PerfectStroke(Plan);
			}
			else
			{
				Key = human::RolloutKey(X.Input.Key, static_cast<std::uint32_t>(Sample));
				Intended = human::SyntheticHand(Plan, X.Input.Character, C.Situation, X.Specs[0].Radius, Key, human::NoiseHistory{}, Human);
			}
			const human::ExecutedStroke Executed = human::ExecuteStroke(Intended, X.Attributes, C.Situation, X.Input.Tip, Body, X.Input.Cue, X.Specs[0],
				ToVec3(C.CueBall, X.Specs[0].Radius), W.Obstacles, 0, Key, human::NoiseHistory{}, Human);
			if (Executed.Error != ErrorCode::Ok)
			{
				return Out;
			}
			SimInput& In = W.In;
			In.Table = X.Input.Table;
			In.Environment = EnvironmentSpec{};
			In.Params = X.Planning;
			for (int b = 0; b < kMaxBalls; ++b)
			{
				SimBall& Ball = In.Balls[b];
				Ball.InPlay = b == 0;
				Ball.ChalkMarks.Clear();
				Ball.Orientation = Quat{};
				Ball.State = BallState{};
				Ball.Spec = X.Specs[b];
				if (b == 0)
				{
					Ball.State.Position = ToVec3(C.CueBall, X.Specs[0].Radius);
					Ball.State.State = MotionState::Stationary;
				}
			}
			In.Strikes.Clear();
			In.Context = ShotContext{};
			In.Record.Trajectories = false;
			In.Record.EventStates = false;
			In.Record.LogTransitions = false;
			In.Record.LogObservers = false;
			In.Record.ShotRecord = true;
			StrikeRequest LagRequest;
			LagRequest.Ball = static_cast<BallId>(kCueBallId);
			LagRequest.Input = Executed.Strike;
			LagRequest.Input.SquirtEnabled = LagRequest.Input.SquirtEnabled && X.Profile.ModelsThrowAndSquirt;
			In.Strikes.PushBack(LagRequest);
			if (W.Sim.Run(In, W.Result) != SimStatus::Ok || W.Result.Record.Truncated)
			{
				return Out;
			}
			const rules::MatchConfig& M = X.Input.Match;
			const rules::LagBallFacts F = rules::DeriveLagBallFacts(W.Result.Record, 0, M.Table, M.Rules.Tolerances);
			Out.Valid = true;
			Out.Bad = F.Bad;
			Out.Distance = F.PocketedOrOffTable ? kInfinity : F.Distance;
			Out.Short = F.FootCushionContacts == 0 && !F.PocketedOrOffTable;
			Out.Long = F.PastHeadCushionNose || F.FootCushionContacts > 1;
			for (const RecordEvent& E : W.Result.Record.Events)
			{
				Out.Long = Out.Long ||
					(E.A == 0 && E.Type == RecordEventType::BallCushion && E.Feature == static_cast<std::uint8_t>(CushionId::Head));
			}
			return Out;
		}

		// The speed whose noise-free lag rests at Target (the last scanned speed that is still short of it, refined by bisection);
		// Simulations counts the runs. Returns a negative speed when no speed brings the ball back.
		double LagSpeedFor(const PlannerState& State, const Candidate& C, double Target, WorkerState& W, int& Simulations, LagTry& Found)
		{
			double Lo = -1.0; // good and resting beyond Target (or short)
			double Hi = -1.0; // at or inside Target, long or bad
			LagTry AtLo;
			for (double V = kLagMinSpeed; V <= kLagMaxSpeed + 1e-9; V += kLagScanStep)
			{
				const LagTry T = LagRollout(State, C, V, kNoiseFreeSample, W);
				++Simulations;
				if (!T.Valid)
				{
					continue;
				}
				if (T.Short || (!T.Bad && T.Distance > Target))
				{
					Lo = V;
					AtLo = T;
					continue;
				}
				Hi = V;
				break;
			}
			if (Lo < 0.0 || Hi < 0.0)
			{
				Found = AtLo;
				return Lo;
			}
			for (int i = 0; i < kLagBisections; ++i)
			{
				const double Mid = 0.5 * (Lo + Hi);
				const LagTry T = LagRollout(State, C, Mid, kNoiseFreeSample, W);
				++Simulations;
				if (T.Valid && (T.Short || (!T.Bad && T.Distance > Target)))
				{
					Lo = Mid;
					AtLo = T;
				}
				else
				{
					Hi = Mid;
				}
			}
			Found = AtLo;
			return AtLo.Short ? Hi : Lo;
		}
	}

	void PlanLag(PlannerState& State, WorkerState& W)
	{
		PlanContext& X = State.Ctx;
		PlannedDecision& D = State.Decision;
		Vec2 First;
		Vec2 Second;
		rules::LagStartPositions(X.Input.Match.Table, First, Second);
		Candidate C;
		C.Type = ShotType::Lag;
		C.CueBall = X.Self == 0 ? First : Second;
		C.PlaceCueBall = true;
		C.Situation = X.Input.Situation;
		C.Situation.Bridge = human::BridgeType::Closed;
		C.Situation.BridgeLength = 0.2;
		C.Situation.BridgeToGrip = 0.8;
		C.Situation.ElevationFloor = 0.0;
		C.Situation.FloorBy = human::FloorSource::None;
		C.Situation.FloorBall = kNoBall;
		C.Situation.Pressure = human::ComputePressure(X.Input.Pressure, X.Input.PressureMode);
		C.Plan.Azimuth = 0.0; // straight up the table to the foot cushion
		C.Plan.Elevation = kMinElevation;
		C.Plan.Bridge = human::BridgeType::Closed;
		C.Declaration = rules::ShotDeclaration{};
		// The profile's target; noisy profiles weigh targets by P(win the lag) against an exponential opponent over their samples.
		const int Targets = X.Samples > 0 ? 4 : 1;
		int Simulations = 0;
		double BestScore = -1.0;
		double BestSpeed = -1.0;
		double BestDistance = 0.0;
		for (int t = 0; t < Targets; ++t)
		{
			const double Target = X.Profile.LagTarget * kLagTargetScales[t];
			LagTry Found;
			const double Speed = LagSpeedFor(State, C, Target, W, Simulations, Found);
			if (Speed <= 0.0)
			{
				continue;
			}
			double Score = Found.Valid && !Found.Bad ? Exp(-Found.Distance / kOpponentLag) : 0.0;
			if (X.Samples > 0)
			{
				double Sum = 0.0;
				for (int k = 0; k < X.Samples; ++k)
				{
					const LagTry T = LagRollout(State, C, Speed, k, W);
					++Simulations;
					Sum += T.Valid && !T.Bad ? Exp(-T.Distance / kOpponentLag) : 0.0;
				}
				Score = Sum / X.Samples;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestSpeed = Speed;
				BestDistance = Found.Distance;
			}
		}
		if (BestSpeed <= 0.0)
		{
			// No speed brought the ball back cleanly on the planning model: a medium lag.
			BestSpeed = 1.0;
			BestScore = 0.0;
			BestDistance = 0.0;
		}
		D.Error = ErrorCode::Ok;
		D.Kind = DecisionKind::Stroke;
		D.Type = ShotType::Lag;
		D.Stroke = C.Plan;
		D.Stroke.Speed = BestSpeed;
		D.Situation = C.Situation;
		D.Declaration = C.Declaration;
		D.PlaceCueBall = true;
		D.CueBallPlacement = C.CueBall;
		D.ExpectedValue = Clamp(BestScore, 0.0, 1.0);
		D.LagDistance = BestDistance;
		D.Reasoning.Candidates = Targets;
		D.Reasoning.Simulations = Simulations;
		D.Reasoning.Sandbagging = X.Sandbagging;
	}
}
