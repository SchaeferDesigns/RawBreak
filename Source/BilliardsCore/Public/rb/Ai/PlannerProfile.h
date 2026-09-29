#pragma once

// What each AI profile knows and how widely and deeply it searches (Docs/architecture.md 7.6; human-factors 3.8, 5.5).
// The six profiles of human-factors 5.5 play on the same attributes and synthetic hand as the player (rb/Human/AiProfiles.h);
// this header adds the PLANNER side: candidate breadth, simulator aim correction, noisy samples, position depth, safety and
// kicking game, and the tendencies that make them feel different (a bar regular hits hard, a tourist does not care where the
// cue ball goes). All values are TUNING (fitted with the round robins of HF-B09 / A-AI-7).
// Owner: WP-12 (AI opponent). Part of rb::ai: may include rb/Human; nothing below rb/Ai includes it (root CMake guard).

#include "rb/Config.h"
#include "rb/Human/AiProfiles.h"
#include "rb/Human/Skill.h"

#include <cstdint>

namespace rb::ai
{
	struct PlannerProfile
	{
		human::AiProfileId Id = human::AiProfileId::Tourist;

		// Knowledge (human::AiKnowledge, 5.5).
		bool ModelsThrowAndSquirt = false; // false: plans on the simplified model (no ball-ball throw, no squirt, nominal balls)
		                                   //   and aims at the ghost ball; true: aim correction by the simulator
		bool KnowsTableSlope = false;      // plans on the real tilt (else on a level table)
		int PositionDepth = 0;             // 0 pots only; 1 next shot; 2 two-shot pattern; >= 3 second ply by simulation
		bool Safeties = false;             // safeties (8-ball: declared), and the safety branch of its own static value
		bool Kicks = false;                // one-rail kicks at legal balls
		bool Banks = false;                // one-rail bank pots
		bool Combinations = false;         // two-ball combinations
		bool Caroms = false;               // kisses (the first ball glances off another into the pocket) and cue-ball caroms
		bool PushOuts = false;             // uses the 9/10-ball push-out
		bool Sandbagger = false;           // local hustler: sandbags until money is down (SandbaggingProfile)

		// Search breadth and depth.
		int PotFamilies = 4;               // aim families (ball x pocket, banks, combinations) kept by static make chance
		int SpeedVariants = 2;             // 1..4 speeds of the ladder row of that length (multiples of the tip speed that just brings
		                                   //   the potted ball to the pocket: 1.6; 1.3, 2.2; 1.2, 1.8, 2.8; 1.1, 1.6, 2.3, 3.3)
		int SpinVariants = 1;              // 1..7 of the spin ladder (centre, follow, draw, stun, right, left, draw-right)
		int SafetyTargets = 0;             // legal balls examined for safeties
		int Placements = 3;                // ball-in-hand placements examined
		int AimIterations = 0;             // simulator aim-correction iterations (0 = ghost-ball aim)
		int NoisySamples = 0;              // K rollout samples of its own noise (0 = assumes perfect execution; HF 5.5)
		int NoisyCandidates = 0;           // M top screened candidates that get the K samples
		int SecondPlyCandidates = 0;       // top k candidates with a second-ply search (PositionDepth >= 3)
		int SecondPlyFamilies = 0;         // aim families per second-ply position
		int SimulationBudget = 2000;       // deterministic cap on the simulations of one decision (planned upper bound)

		// Tendencies (TUNING).
		double PerceivedAimSigma = 0.3e-2; // [rad] 1-sigma cue-ball direction error it assumes for its own make chances
		double RunoutRate = 0.35;          // q: probability to make the next ball and keep going from a typical position
		double SafetyQuality = 0.0;        // [0, 1] how much its safeties hurt the opponent
		double KickSkill = 0.3;            // [0, 1] probability of a legal contact when hooked (static model)
		double SafetyBias = 0.0;           // added to the score of safety / kick candidates (> 0 likes safeties)
		double SpeedBias = 1.0;            // multiplies planned speeds (bar regular: hits hard)
		double BreakSpeed = 6.0;           // [m/s] planned tip speed of the break (and 0.85 of it; capped at 10 m/s, other strokes at
		                                   //   7.5 m/s); the 14.1 opening safety break of a safety game is soft (1.8 / 2.4 m/s)
		double ChoiceTolerance = 0.0;      // chooses among candidates within this score of the best (seeded; human variety)
		double LagTarget = 0.1;            // [m] rest distance from the head cushion's nose the lag aims for (noisy profiles also try
		                                   //   0.5 / 1.6 / 2.4 times it and keep the best by P(win the lag))
	};

	// The planner profile of an HF 5.5 profile: knowledge from human::GetAiProfile(Id).Knowledge, breadth and tendencies from
	// the WP-12 table (Docs/architecture.md 7.6).
	RB_API PlannerProfile GetPlannerProfile(human::AiProfileId Id);

	// Same for a (possibly customised) human::AiProfile: knowledge from Profile.Knowledge, breadth and tendencies by Profile.Id.
	RB_API PlannerProfile MakePlannerProfile(const human::AiProfile& Profile);

	// The local hustler before money is down (5.5, Q6: a visible choice): the bar regular's knowledge and breadth (no safeties, no
	// noisy samples, depth 1) and a wider choice tolerance, so he "hides his speed"; his hand stays his own.
	RB_API PlannerProfile SandbaggingProfile(const PlannerProfile& Profile);

	// What the AI assumes about its opponent (the static evaluator's other player).
	struct OpponentModel
	{
		double AimSigma = 0.3e-2;    // [rad] 1-sigma cue-ball direction error
		double RunoutRate = 0.5;     // q
		double SafetyQuality = 0.3;  // [0, 1]
		double KickSkill = 0.5;      // [0, 1]
		bool PlaysSafeties = true;
	};

	// The opponent model of an AI profile (AI vs AI, hot-seat guests at 50 map to the league player).
	RB_API OpponentModel OpponentModelFor(human::AiProfileId Id);

	// A human opponent from his attributes (mean of the six, interpolated between the profiles' attribute levels 15 .. 90).
	RB_API OpponentModel OpponentModelFromAttributes(const human::ShooterAttributes& Attributes);
}
