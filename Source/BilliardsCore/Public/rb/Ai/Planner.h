#pragma once

// The AI opponent's shot planner (Docs/architecture.md 7.6): a deterministic Monte-Carlo search over candidate strokes that
// uses the REAL simulator and the REAL rules. Stages: candidate generation (ghost-ball pots, banks, combinations, kicks,
// safeties, break, push-out, ball-in-hand placements, called-shot declarations, speed / spin / elevation variants) ->
// screening (throw- / squirt-aware aim correction by the simulator, then a noise-free rollout) -> noisy evaluation (K
// samples of the AI's own hand and human layer, rollout keys) -> second ply for the strongest profiles -> choice.
// Every rollout's end state is judged by rules::EvaluateShot / ApplyShot and valued with the static evaluator of
// rb/Ai/PositionEval.h (the AI's probability to win the rack). The match start is covered as well: MatchPhase::Lag -> a lag
// stroke from the AI's lag position; LagWinnerChooses -> the breaker; AwaitDecision -> an option.
//
// The planner decides WHAT to play. The game executes it like the player's stroke (human-factors principle 4):
//   human::SyntheticHand(Decision.Stroke, Character, Decision.Situation, R, match key, history) -> human::ExecuteStroke ->
//   Simulator::Run -> rules. The planner never builds a CueStrikeInput (HF-B07): its own rollouts use the same SyntheticHand /
//   ExecuteStroke with rollout keys (human::RolloutKey, plain draws that never read the streak history, human-factors 3.2).
//
// Threads (thread-pool agnostic, for the Unreal task graph): a PlannerScratch holds one decision; a PlannerWorker (one per
// thread) holds a Simulator and the per-rollout buffers. Protocol:
//   Scratch.Begin(Input, Config);
//   while (!Scratch.Finished()) {
//       for every j < Scratch.JobCount(), on any threads, in any order: Scratch.RunJob(j, WorkerOfThatThread);   // concurrent OK
//       join;  Scratch.Advance();                                                                                  // one thread
//   }
//   const PlannedDecision& D = Scratch.Decision();
// Concurrent RunJob calls with DISTINCT job indices are safe (each job writes only its own result slot and reads only the
// stage's frozen data); Begin / Advance / Decision must not overlap with RunJob. The decision is bitwise identical for any
// thread count and job order (A-AI-1). PlanShot runs the protocol on the calling thread.
// Budget: deterministic (a simulation count, PlannerProfile::SimulationBudget), never wall time.
// Memory: every buffer is reserved in the constructors; nothing is allocated per candidate or per rollout (A-AI-9).
// Owner: WP-12 (AI opponent). Part of rb::ai.

#include "rb/Ai/PlannerProfile.h"
#include "rb/Ai/PositionEval.h"
#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Equipment/Cue.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Human/AiProfiles.h"
#include "rb/Human/CueState.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Progression.h"
#include "rb/Human/Skill.h"
#include "rb/Human/TipState.h"
#include "rb/Math/Vec2.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/RulesTypes.h"

#include <cstdint>

namespace rb::ai
{
	enum class ShotType : std::uint8_t
	{
		Pot,         // direct pot of the first ball
		Bank,        // one-rail bank of the first ball
		Combination, // first ball into a second one that drops
		Kick,        // one rail before the first (legal) ball
		Safety,      // no pot intended (8-ball: declared safety)
		Break,
		PushOut,     // 9/10-ball push-out
		Kiss,        // the first ball glances off a second ball into the pocket (object-ball carom)
		Carom,       // the cue ball glances off the first ball into a second one that drops (cue-ball carom)
		Lag,         // the lag for the first break (MatchPhase::Lag): from the AI's lag position to the foot rail and back
	};

	// The shot types that intend to pocket a ball (PotBall).
	constexpr bool IsPotShot(ShotType Type)
	{
		return Type == ShotType::Pot || Type == ShotType::Bank || Type == ShotType::Combination || Type == ShotType::Kiss || Type == ShotType::Carom;
	}

	enum class DecisionKind : std::uint8_t
	{
		None,   // error, see PlannedDecision::Error
		Stroke,        // play Stroke with Declaration (and the placement; the lag: from CueBallPlacement)
		Option,        // pick Choice among State.PendingOutcome.Options
		ChooseBreaker, // the lag winner chooses the first breaker (rules::ChooseBreaker(Config, State, Breaker))
	};

	struct PlannerInput
	{
		const TableGeometry* Table = nullptr;  // required, shared read-only (BuildTableGeometry)
		PhysicsParams Physics;                 // the referee's parameters of this table (MakePhysicsParams(Spec, Condition))
		BallSpec Balls[kMaxBalls];             // the ball set, index = ball id (default: standard balls)
		rules::MatchConfig Match;              // discipline, rules config, rules table (per-ball radii, pocket openings)
		rules::MatchState State;               // AwaitShot with Self to shoot, or AwaitDecision with Self to decide
		int Self = 0;                          // rules player (team) index of the AI
		human::AiCharacter Character;          // profile (attributes, synthetic hand, knowledge) + character seed
		PlannerProfile Planner;                // normally GetPlannerProfile(Character.Profile.Id)
		OpponentModel Opponent;                // what the AI assumes about the other player
		human::TipState Tip;                   // the AI's equipment (as the game keeps it)
		human::CueBodyState CueBody;
		CueSpec Cue = kCuePlaying19oz;
		human::HumanParams Human;
		human::StrokeSituation Situation;      // base situation (fatigue, sweat, glove, off hand, intoxication); Bridge, lengths,
		                                       //   ElevationFloor / FloorBy and Pressure are set per candidate by the planner
		human::PressureInputs Pressure;        // the game's pressure inputs; GameBall is set per candidate
		human::PressureMode PressureMode = human::PressureMode::On;
		human::NoiseKey Key;                   // the REAL shot's key (Purpose 0): rollouts use RolloutKey(Key, s) only; the seeded
		                                       //   choice among near-equal candidates hashes MatchSeed / RackIndex / ShotIndex / ShooterId
		human::MoneyGames Money = human::MoneyGames::SideBetsAndHustling; // Q6 product switch (LeaguePrizeOnly: no sandbagging)
		bool MoneyDown = false;                // a money game with the bet placed: the hustler stops sandbagging
	};

	struct PlannerConfig
	{
		double Breadth = 1.0;       // scales the profile's families, placements and safety targets (each stays >= 1)
		double Samples = 1.0;       // scales NoisySamples and NoisyCandidates (each stays >= 1 when the profile has them)
		int SimulationBudget = 0;   // > 0 overrides PlannerProfile::SimulationBudget
		bool SecondPly = true;      // false skips the second ply (tests)
	};

	// One of the best candidates, for the debug overlay and the UI's "the opponent studies the table".
	struct ConsideredShot
	{
		ShotType Type = ShotType::Pot;
		BallId FirstBall = kNoBall;      // intended first object ball
		BallId PotBall = kNoBall;        // intended pocketed ball (kNoBall: safety / kick / push-out / break)
		PocketId Pocket = PocketId::None;
		double Score = 0.0;              // estimated P(win the rack) [0, 1] (+ the profile's safety bias)
		double PotChance = 0.0;          // noisy samples: share of rollouts that pocketed PotBall; else the perceived chance
		double FoulChance = 0.0;         // noisy samples: share of rollouts that fouled; else the noise-free rollout's foul (0 / 1)
		double Speed = 0.0;              // planned tip speed [m/s]
		double SpinA = 0.0;              // planned cue-axis offsets / R
		double SpinB = 0.0;
		int Samples = 0;                 // rollouts that valued it (screening + noisy)
	};

	inline constexpr int kReasoningShots = 5;

	struct PlanReasoning
	{
		FixedVector<ConsideredShot, kReasoningShots> Top; // best first (the chosen one is Top[0] unless ChoiceTolerance picked another)
		int Candidates = 0;         // generated candidates (all stages)
		int Simulations = 0;        // simulator runs of this decision
		int NoisyCandidates = 0;    // candidates with noisy samples
		int SecondPlyPositions = 0; // positions searched in the second ply
		int Placements = 0;         // ball-in-hand placements examined
		double TableValue = 0.0;    // static P(win) of the table before the decision, for the AI
		double BestPotChance = 0.0; // static make chance of the best direct pot before the decision
		bool Sandbagging = false;   // the hustler hid his speed
		bool ChoseSafety = false;
		bool ChosePushOut = false;
	};

	struct PlannedDecision
	{
		ErrorCode Error = ErrorCode::NotImplemented; // Ok, InvalidArgument (no table, bad input), InvalidState (not the AI's turn)
		DecisionKind Kind = DecisionKind::None;
		human::PlannedStroke Stroke;                 // for human::SyntheticHand (Kind == Stroke)
		human::StrokeSituation Situation;            // the situation the planner assumed (bridge, lengths, elevation floor, pressure);
		                                             //   the game may refine it (IK) but should keep the bridge and floor
		rules::ShotDeclaration Declaration;          // kind, call (validated with rules::ValidateDeclaration)
		bool PlaceCueBall = false;                   // cue ball in hand: place it at CueBallPlacement
		Vec2 CueBallPlacement;                       // [m]
		rules::Option Choice = rules::Option::AcceptTable; // Kind == Option
		int Breaker = -1;                            // Kind == ChooseBreaker: the rules player who breaks
		ShotType Type = ShotType::Pot;
		BallId TargetBall = kNoBall;                 // first object ball
		BallId PotBall = kNoBall;                    // intended pocketed ball
		PocketId Pocket = PocketId::None;
		double ExpectedValue = 0.0;                  // estimated P(the AI wins the rack) after this decision (the lag: P(win the lag))
		double LagDistance = 0.0;                    // the lag: planned rest distance from the head cushion's nose [m]
		double PotChance = 0.0;
		double FoulChance = 0.0;
		PlanReasoning Reasoning;
	};

	enum class PlannerStage : std::uint8_t
	{
		Idle,        // no decision started
		Screening,   // aim correction + noise-free rollout per candidate
		NoisySamples,// K samples of the AI's own hand per top candidate
		SecondPly,   // next-shot searches of the best end states
		Done,
	};

	struct PlannerProgress
	{
		PlannerStage Stage = PlannerStage::Idle;
		int Jobs = 0;               // jobs of the stage that runs next
		int Candidates = 0;         // candidates generated so far
		bool HasBest = false;
		ConsideredShot Best;        // the best screened / sampled candidate so far (HasBest)
		Vec2 BestCueBall;           // where the cue ball starts for it (placement or its position) [m]
		double BestAzimuth = 0.0;   // its planned aim [rad]
	};

	struct PlannerState;  // Private/rb/Ai/PlannerInternal.h
	struct WorkerState;

	// Per-thread rollout buffers: a Simulator (AI recording capacities), a ShotResult, a SimInput, rules facts and state copies.
	// Allocated once in the constructor (about 0.5 MB); reused across decisions. Never shared between threads at the same time.
	class PlannerWorker
	{
	public:
		RB_API PlannerWorker();
		RB_API ~PlannerWorker();
		RB_API PlannerWorker(PlannerWorker&& Other) noexcept;
		RB_API PlannerWorker& operator=(PlannerWorker&& Other) noexcept;
		PlannerWorker(const PlannerWorker&) = delete;
		PlannerWorker& operator=(const PlannerWorker&) = delete;

		WorkerState* Internal() const { return State; }

	private:
		WorkerState* State = nullptr; // owned
	};

	// One decision: the input copy, candidates, per-sample results and stage bookkeeping (buffers reserved once, about 3 MB), and
	// one PlannerWorker for PlanShot and for the few serial simulations of Advance (second-ply positions).
	class PlannerScratch
	{
	public:
		RB_API PlannerScratch();
		RB_API ~PlannerScratch();
		RB_API PlannerScratch(PlannerScratch&& Other) noexcept;
		RB_API PlannerScratch& operator=(PlannerScratch&& Other) noexcept;
		PlannerScratch(const PlannerScratch&) = delete;
		PlannerScratch& operator=(const PlannerScratch&) = delete;

		// Starts a decision (serial generation stage). On an input error or when nothing needs a simulation (an option decision)
		// the planner is Finished at once. Returns the decision's error code.
		RB_API ErrorCode Begin(const PlannerInput& Input, const PlannerConfig& Config);

		// Jobs of the current stage (0 when Finished).
		RB_API int JobCount() const;

		// Runs job Job of the current stage on Worker. Thread-safe for distinct Job indices (see the header comment).
		RB_API void RunJob(int Job, PlannerWorker& Worker);

		// Reduces the finished stage (in index order) and prepares the next one; single thread, after every job ran. Returns true
		// while the planner is not Finished.
		RB_API bool Advance();

		// Instead of Advance: reduces the finished stage and decides with what is known so far (no further stages). For a caller
		// that runs out of wall time (the Unreal task graph under load). Deterministic for the stage it is called after; a
		// wall-clock trigger makes the decision depend on the machine, so the default protocol never uses it.
		RB_API void FinishNow();

		RB_API bool Finished() const;

		// The decision (valid once Finished).
		RB_API const PlannedDecision& Decision() const;

		// The worker PlanShot uses (callers of the staged protocol may use it for one of their threads).
		RB_API PlannerWorker& SerialWorker();

		PlannerState* Internal() const { return State; }

		// Progress for the UI (between Advance calls, never concurrently with RunJob): the stage that runs next and the best
		// shot known so far (false before the screening stage is reduced). The avatar can look at it while the AI "studies
		// the table"; the final decision may differ.
		RB_API PlannerProgress Progress() const;

	private:
		PlannerState* State = nullptr; // owned
		PlannerWorker Serial;
	};

	// The whole protocol on the calling thread (Begin, RunJob over every job, Advance, ... ; Decision()).
	RB_API PlannedDecision PlanShot(const PlannerInput& Input, const PlannerConfig& Config, PlannerScratch& Scratch);

	// A one-line English summary of the decision for logs and the debug overlay (the UI builds its own localised text from
	// PlanReasoning). Writes at most Size - 1 characters and a terminating zero; returns the characters written.
	RB_API int FormatReasoning(const PlannedDecision& Decision, char* Buffer, int Size);

	// Name of a shot type ("pot", "bank", ...).
	RB_API const char* ShotTypeName(ShotType Type);
}
