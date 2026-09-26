#pragma once

// AI opponents on the player's model (human-factors 3.8, 5.5; principle 4): the planner's stroke goes through a
// SYNTHETIC HAND that adds the profile's input flaws, then through the same ExecuteStroke, CueStrikeInput,
// simulator and rules as the player. The AI never builds a CueStrikeInput itself (HF-B07): SyntheticHand returns
// an IntendedStroke. The AI reads only what the player could see (no future seeds; rollout keys draw without the
// streak history, rb/Human/NoiseHash.h).
// Owner: WP-11 (player model). Part of rb::human. Profile values TUNING, fitted with rbsim round-robins (HF-B09).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"

#include <cstdint>

namespace rb::human
{
	// Input flaws of the synthetic hand (3.8).
	struct SyntheticHandParams
	{
		double AimSigma = 0.0;          // sigma_aim [rad] (channel 20)
		double AimBiasMax = 0.0;        // |bias_aim| <= this [rad]: constant per character (vision centre), sign and size seeded
		double SteerSigma = 0.0;        // sigma_steer [m] grip steering through the pivot (channel 21)
		double SpeedSigma = 0.0;        // sigma_spd [1] relative (channel 22)
		double PauseMean = 0.5;         // [s] T_pause = PauseMean (0.7 + 0.6 U23)
		double JabProbability = 0.0;    // a_c = -5 m/s^2 when U24 < p_jab
		double HeadMoveProbability = 0.0; // HeadMoved when U26 < p_head
		bool UsesSettle = false;        // t_s = t_c - 2 s when P > 0.4
	};

	// What the planner knows (5.5; the planner itself is outside the core).
	struct AiKnowledge
	{
		bool AssumesPerfectExecution = true; // weak AIs plan without their own noise
		bool ModelsThrowAndSquirt = false;   // false: simplified model (no throw, squirt, swerve, nominal masses)
		bool KnowsTableSlope = false;        // plans on the real tilt (else on a level table)
		int PlanDepth = 1;                   // balls ahead
		int SelfNoiseSamples = 0;            // K rollout samples of its own noise ("percentage play"), rollout keys Purpose 1..K
		bool Safeties = false;
		bool SandbagsUntilMoney = false;     // local hustler (money games are in by default, Q6; off with MoneyGames::LeaguePrizeOnly)
	};

	enum class AiProfileId : std::uint8_t
	{
		Tourist,
		BarRegular,
		LeaguePlayer,
		LocalHustler,
		RoadPlayer,
		TouringPro,
	};
	inline constexpr int kAiProfileCount = 6;

	struct AiProfile
	{
		AiProfileId Id = AiProfileId::Tourist;
		double Rating = 250.0;          // fictional logarithmic scale: +100 points = 2:1 in games
		ShooterAttributes Attributes;
		SyntheticHandParams Hand;
		ShooterHabits Habits;           // ChalkSweep (H_chalk), Rack (H_rack), WarpCheck (0/1)
		double MoneyGameRackHabit = -1.0; // H_rack in money games (hustler 0.2); < 0 = same as Habits.Rack
		AiKnowledge Knowledge;
	};

	// The 5.5 table (attributes St/SC/ST/BS/Sta/N, synthetic hand, habits, knowledge).
	RB_API AiProfile GetAiProfile(AiProfileId Id);

	// A concrete opponent: a profile plus the character seed of its constant aim bias.
	struct AiCharacter
	{
		AiProfile Profile;
		std::uint64_t CharacterSeed = 0;
	};

	// bias_aim = AimBiasMax (2 U01(HashKeys(CharacterSeed, 20, 1)) - 1): constant per character.
	RB_API double CharacterAimBias(const AiCharacter& Character);

	// Planner output (5.5).
	struct PlannedStroke
	{
		double Azimuth = 0.0;    // phi_p [rad]
		double Elevation = 0.0;  // theta_p [rad]
		double AxisOffsetA = 0.0; // A_p [1]
		double AxisOffsetB = 0.0; // B_p [1]
		double Speed = 0.0;      // V_p [m/s]
		BridgeType Bridge = BridgeType::Closed;
	};

	// 3.8: phi_i = phi_p + bias_aim + sigma_aim eps20; y_s = sigma_steer eps21, phi_i += y_s / L_bg,
	// A_i = A_p - y_s L_bc / (L_bg R); v_r,i = -(y_s V_p / 0.15 m) L_b / L_bg (animation only); B_i = B_p; theta_i = theta_p;
	// V_i = V_p (1 + sigma_spd eps22); T_pause = mean (0.7 + 0.6 U23); a_c = U24 < p_jab ? -5 : 0; t_c = 1.5 + 1.5 U25 s;
	// t_s = (UsesSettle and P > 0.4) ? t_c - 2 s : -1; HeadMoved = U26 < p_head; t_fwd = t_c - max(0.1 s, 2 L_stroke / V_p)
	// (a uniformly accelerated final stroke of L_stroke = 0.15 m, animation only; V_p <= 0 -> t_c - 0.1 s). eps20-22 are
	// streak-guarded like 3.2 (DrawPerShot with History, a cache rebuilt on mismatch, and Params.StreakGuard; plain for
	// rollout keys); U23-U26 = PlainUniform. ExecuteStroke then runs with the AI's own attributes and a StrokeSituation built
	// like the player's (pressure incl. MoneyGameStakes, fatigue, Intoxication through StrokeIntoxication: principle 4).
	RB_API IntendedStroke SyntheticHand(const PlannedStroke& Plan, const AiCharacter& Character, const StrokeSituation& Situation, double BallRadius,
		const NoiseKey& Key, const NoiseHistory& History, const HumanParams& Params);
}
