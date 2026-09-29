#pragma once

// PRIVATE interface of the AI planner (WP-12): the decision state behind PlannerScratch, the per-thread WorkerState behind
// PlannerWorker, and the functions of the translation units Candidates.cpp (generation), Rollout.cpp (one rollout through the
// synthetic hand, ExecuteStroke, the simulator and the rules) and Planner.cpp (stages, reduction, choice).
// Owner: WP-12 (AI opponent).

#include "rb/Ai/Planner.h"
#include "rb/Ai/PositionEval.h"
#include "rb/Human/HumanModel.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/ShotFacts.h"

#include <cstdint>
#include <vector>

namespace rb::ai
{
	inline constexpr int kMaxCandidates = 2048;        // all stages of one decision
	inline constexpr int kMaxSamples = 32;             // noisy samples per candidate
	inline constexpr int kMaxNoisyCandidates = 64;     // candidates with noisy samples
	inline constexpr int kMaxSecondPly = 8;            // second-ply positions
	inline constexpr int kMaxOrigins = 1 + kMaxSecondPly;
	inline constexpr int kMaxPlacements = 32;          // ball-in-hand placements per origin
	inline constexpr int kNoiseFreeSample = -1;        // Rollout: perfect execution
	inline constexpr double kAimTolerance = 1.75e-4;   // [rad] (0.01 deg) aim correction stops below this direction error
	inline constexpr double kMinElevation = 2.5 * kDegToRad; // planned elevation of an ordinary stroke
	inline constexpr double kMaxElevation = 40.0 * kDegToRad; // candidates that need more (a jump / masse) are dropped
	// Score penalty per unit foul chance (TUNING): the static evaluator values the opponent's ball in hand like an easy shot in
	// position (its make chance saturates for good players), so a deliberate foul would tie with a legal leave; a player does
	// not give away ball in hand for nothing. Larger than the league player's choice tolerance; the same for every candidate
	// when all of them foul (hooked), so it only breaks ties between fouls and legal shots.
	inline constexpr double kPlannedFoulPenalty = 0.05;

	enum class StageKind : std::uint8_t
	{
		Idle,
		Screen,    // aim correction + noise-free rollout per candidate
		Noisy,     // K samples per selected candidate
		SecondPly, // screening of the next-shot candidates of the second-ply positions
		Done,
	};

	struct Candidate
	{
		ShotType Type = ShotType::Pot;
		BallId FirstBall = kNoBall;     // intended first object ball (kNoBall: push-out without contact)
		BallId PotBall = kNoBall;       // intended pocketed ball
		BallId MeasureBall = kNoBall;   // aim correction: the ball whose post-contact direction is measured ...
		BallId MeasureStriker = kCueBallId; // ... after its first contact with this ball (kiss: the second ball; carom: the cue
		                                //   ball's second contact); the cue ball's first contact must be FirstBall
		PocketId Pocket = PocketId::None;
		std::uint8_t Origin = 0;        // rollout origin (0 = the decision's table, 1.. second-ply positions)
		std::int16_t Parent = -1;       // second ply: the decision candidate this next shot continues
		bool PlaceCueBall = false;
		bool AimCorrect = false;        // run the simulator aim correction
		Vec2 CueBall;                   // start of the cue ball (placement or where it lies) [m]
		Vec2 MeasureTarget;             // the measured ball should travel toward this point [m]
		human::PlannedStroke Plan;      // Azimuth = the geometric aim
		rules::ShotDeclaration Declaration;
		human::StrokeSituation Situation; // bridge, lengths, elevation floor, pressure
		double PerceivedPot = 1.0;      // static make chance with the profile's own sigma (1 for non-pots)
		double MissValue = 0.0;         // static value for the AI when the intended pot fails (the opponent at a typical table)
		double KeepValue = 0.0;         // static value for the AI when it keeps the table at a typical table (caps a lucky
		                                //   noise-free rack win: the 9 dropping on a safety is not a plan)
		bool GameBall = false;          // PotBall wins the rack (9 / 10 / 8)
	};

	struct CandidateResult
	{
		double Azimuth = 0.0;     // aim after the correction [rad]
		double Value = 0.0;       // value of the noise-free rollout
		double Score = 0.0;       // screening score, replaced by the noisy mean and the second-ply correction
		double PotShare = 0.0;    // noisy: share of samples that pocketed PotBall
		double FoulShare = 0.0;   // noisy: share of fouls
		double KeptShare = 0.0;   // share of rollouts after which the AI keeps the table
		double AimError = 0.0;    // last measured direction error [rad]
		int Simulations = 0;
		int Samples = 0;          // noisy samples
		bool Potted = false;      // noise-free rollout pocketed PotBall
		bool Won = false;         // noise-free rollout won the rack
		bool Foul = false;        // noise-free rollout fouled
		bool Kept = false;        // noise-free rollout: the AI keeps the table
		bool Noisy = false;       // has noisy samples
		bool Screened = false;    // its screening job ran (FinishNow may end a stage whose jobs did not all run)
	};

	struct SampleResult
	{
		double Value = 0.0;
		bool Potted = false;
		bool Foul = false;
		bool Kept = false;
		bool Valid = false;       // written by this decision's noisy job (reset when the noisy stage is queued)
	};

	struct Job
	{
		StageKind Kind = StageKind::Screen;
		int Candidate = 0;
		int Slot = 0;             // Noisy: row of the sample table
	};

	struct Origin
	{
		rules::MatchState State;
		BallLayout Layout;
		double FaultValue = 0.0;  // value for the AI after a foul (the opponent with the cue ball in hand)
		int Parent = -1;          // second ply: the decision candidate
		double StaticValue = 0.0; // StateValue of State for the AI (second ply: the estimate the rollouts used)
	};

	// Frozen per decision (read by every job).
	struct PlanContext
	{
		PlannerInput Input;
		PlannerConfig Config;
		PlannerProfile Profile;             // effective (sandbagging, config scaling)
		PhysicsParams Planning;             // the planning model's physics
		BallSpec Specs[kMaxBalls];          // the ball specs the planner assumes
		EvalContext Eval;                   // Table + &Input.Match, both players
		human::ShooterAttributes Attributes;
		int Self = 0;
		int Samples = 0;                    // K
		int NoisyCandidates = 0;            // M
		int SecondPly = 0;                  // k
		bool Sandbagging = false;
	};

	struct PlannerState
	{
		PlanContext Ctx;
		StageKind Stage = StageKind::Idle;
		bool Finished = true;
		std::vector<Candidate> Candidates;      // reserved kMaxCandidates
		std::vector<CandidateResult> Results;   // reserved kMaxCandidates
		std::vector<SampleResult> SampleTable;  // kMaxNoisyCandidates x kMaxSamples
		std::vector<int> NoisyRows;             // candidate of each noisy row
		int NoisyReduced = 0;                   // noisy rows that got samples (reduced); > 0: only they may be chosen
		std::vector<Job> Jobs;                  // reserved kMaxCandidates
		Origin Origins[kMaxOrigins];
		int OriginCount = 0;
		int DecisionCandidates = 0;             // candidates of origin 0 (the second ply appends after them)
		int Placements = 0;
		int SecondPlyPositions = 0;
		PlannedDecision Decision;

		PlannerState();
	};

	struct WorkerState
	{
		Simulator Sim;
		ShotResult Result;
		SimInput In;
		rules::ShotFacts Facts;
		rules::MatchState After;
		human::BallObstacle Obstacles[kMaxBalls];

		WorkerState();
	};

	// One rollout's result.
	struct RolloutOutcome
	{
		double Value = 0.0;           // P(the AI wins the rack) after the shot
		bool Potted = false;          // the intended ball dropped
		bool Foul = false;
		bool Kept = false;            // the AI keeps the table
		bool Won = false;             // the AI won the rack
		bool Contact = false;         // Measure: the cue ball's first ball contact was MeasureBall
		double DirectionError = 0.0;  // Measure: measured minus wanted direction of MeasureBall after the contact [rad]
		SimStatus Status = SimStatus::NotImplemented;
	};

	// Candidates.cpp: appends the candidates of origin OriginIndex (0: the decision's table; > 0: a second-ply position, pots only,
	// Parent = the decision candidate). Returns the number appended.
	int GenerateCandidates(PlannerState& State, int OriginIndex, int Parent);

	// Candidates.cpp: static value for the AI when the opponent comes to a typical table (after a missed pot).
	double MissValueAt(const PlanContext& Context, const Origin& From, const Vec2& Cue);

	// Candidates.cpp: static value for the AI when it keeps a typical table.
	double KeepValueAt(const PlanContext& Context, const Origin& From, const Vec2& Cue);

	// Rollout.cpp: one rollout of Candidate at Azimuth. Sample = kNoiseFreeSample (perfect execution) or a noisy sample index
	// (RolloutKey(Key, Sample)). Measure: event states on, the direction error of MeasureBall is measured. EndState != nullptr
	// receives the rules state after the shot (ApplyShot).
	RolloutOutcome Rollout(const PlannerState& State, const Candidate& C, double Azimuth, int Sample, bool Measure, WorkerState& Worker,
		rules::MatchState* EndState);

	// Rollout.cpp: aim correction (Candidate::AimCorrect, PlannerProfile::AimIterations) and the noise-free rollout; fills Out.
	void ScreenCandidate(const PlannerState& State, int Index, WorkerState& Worker, CandidateResult& Out);

	// Rollout.cpp: the screening score of a screened candidate.
	double ScreenScore(const PlanContext& Context, const Candidate& C, const CandidateResult& R);

	// Rollout.cpp: the lag (MatchPhase::Lag, the AI's lag position from rules::LagStartPositions): the stroke speed that brings the
	// ball to rest at the profile's target distance from the head cushion's nose (noise-free search on the planning model); noisy
	// profiles also try other targets and keep the best by P(win the lag) over K samples of their own hand. Fills the decision.
	void PlanLag(PlannerState& State, WorkerState& Worker);
}
