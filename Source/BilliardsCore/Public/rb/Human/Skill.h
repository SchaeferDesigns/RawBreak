#pragma once

// Execution attributes, habits, the human-layer parameters with their skill scaling, and the per-shot situation
// inputs (bridge, stance, pressure, fatigue, sweat) of the player model (human-factors 3.3, 3.4, 5.1; HF-01..HF-19).
// Owner: WP-11 (player model). Part of rb::human (never included by the event loop or the rules).
//
// Skill never straightens, smooths or rescales the player's input (principle 2); it only shrinks the thin human
// layer: every noise channel follows sigma(x) = sigma_25 L(x; rho), L(x; rho) = rho^((clamp(x, 0, 100) - 25) / 75)
// (1 at x = 25, rho at x = 100, > 1 below 25 for weak AIs), never zero (principle 6). All values TUNING, fitted to
// the routine-shot budget of 3.10 (release test HF-B02).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Scalar.h"

#include <cstdint>

namespace rb::human
{
	// ---------------------------------------------------------------------------------------------
	// Attributes and habits (5.1)
	// ---------------------------------------------------------------------------------------------

	// Six execution attributes on [0, 100], one noise family each. A new career starts at 25; a touring pro sits
	// at 85-95; skill never decays. Hot-seat guests use 50 in every attribute (Q4, rb/Human/Progression.h).
	struct ShooterAttributes
	{
		double Steadiness = 25.0;      // grip-hand drift (HF-01)
		double SpeedControl = 25.0;    // speed scatter (HF-03)
		double SpinTouch = 25.0;       // tip placement, unintended elevation (HF-02, HF-04)
		double BridgeStability = 25.0; // bridge factor, slip threshold (HF-12)
		double Stance = 25.0;          // awkward-stance factor (HF-13)
		double Nerve = 25.0;           // pressure gain, flinch, grip tension (HF-05, HF-14)
	};

	constexpr ShooterAttributes UniformAttributes(double Value) { return {Value, Value, Value, Value, Value, Value}; }

	// Habits in [0, 1]: grow by +0.02 per good ritual (R-mode) execution, saturating at 1 (5.1), and then show as
	// automatic behaviour. A skipped chore produces the habitual result (principle 5, rb/Human/Chores.h).
	struct ShooterHabits
	{
		double ChalkSweep = 0.0;   // H_chalk: sweep vs drill (4.1)
		double AutoRechalk = 0.0;  // re-chalk after spin shots
		double BallWipe = 0.0;     // wipes chalk marks at ball in hand (HF-40)
		double WarpCheck = 0.0;    // roll test notices smaller bows: 1 mm at 0 -> 0.3 mm at 1 (HF-31)
		double SleeveTuck = 0.0;   // HF-60
		double Rack = 0.0;         // H_rack: rack quality Q in A/C mode (4.6)
	};

	inline constexpr double kHabitGainPerGoodExecution = 0.02;

	// +0.02 per good ritual execution, saturating at 1.
	constexpr double GrowHabit(double Habit) { return Min(1.0, Habit + kHabitGainPerGoodExecution); }

	// L(x; rho) = rho^((clamp(x, 0, 100) - 25) / 75).
	inline double SkillScale(double Attribute, double Rho) { return Pow(Rho, (Clamp(Attribute, 0.0, 100.0) - 25.0) / 75.0); }

	// ---------------------------------------------------------------------------------------------
	// Bridges (HF-12)
	// ---------------------------------------------------------------------------------------------

	enum class BridgeType : std::uint8_t
	{
		Closed,
		Open,
		Rail,
		Elevated,
		Mechanical,
	};

	struct BridgeSpec
	{
		double BaseFactor = 1.0; // m_br0 [1] (drift and elevation factor at Bridge Stability 25)
		double SlipSpeed = 6.0;  // V_b0 [m/s] above which the bridge slips
	};

	constexpr BridgeSpec BridgeSpecFor(BridgeType Bridge)
	{
		switch (Bridge)
		{
		case BridgeType::Open: return {1.2, 4.0};
		case BridgeType::Rail: return {1.4, 3.5};
		case BridgeType::Elevated: return {2.0, 2.5};
		case BridgeType::Mechanical: return {1.8, 3.0};
		case BridgeType::Closed: break;
		}
		return {1.0, 6.0};
	}

	// ---------------------------------------------------------------------------------------------
	// Human-layer parameters (3.3 table and the constants of 3.4-3.7). All TUNING.
	// ---------------------------------------------------------------------------------------------
	struct HumanParams
	{
		// Switches
		double NoiseScale = 1.0;            // NS: Imperfections Sim 1 / Scaled 0.6 / Low 0.3 / Off 0 (5.4); not applied to warp
		std::uint32_t ChannelMask = 0;      // ChannelBit(c) set = channel c off (diagnosis re-runs, 3.9)
		bool StreakGuard = true;            // Q1 streak guard of the per-shot draws (false = plain independent draws)
		double WarpSightLength = 0.45;      // s_e [m] tip-to-eye distance along the cue (UE 4.2), warp coupling k_w = s_e / L (4.4)
		RulesTolerances Rules;              // Frozen, FrozenEnvelope, GrazeAngle for DoubleHitRisk / PushRisk (3.6, RUL F7/F8)

		// Channel sigmas at attribute 25 and their rho (value at 100 / value at 25)
		double DriftSigma = 0.9e-3;         // sigma_drift [m] grip lateral (Steadiness)
		double DriftRho = 0.2;
		double DriftVerticalRatio = 0.5;    // vertical drift = 0.5 x lateral
		double TremorSigma = 0.03e-3;       // sigma_tr [m] at the tip, at rest (Nerve acts via the pressure gain)
		double TipASigma = 0.20e-3;         // sigma_A [m] lateral tip placement (Spin Touch)
		double TipARho = 0.25;
		double TipBSigma = 1.5e-3;          // sigma_B [m] vertical tip placement (Spin Touch)
		double TipBRho = 0.2;
		double OffsetKappa = 0.06;          // kappa_off [1] of |A_i| R, |B_i| R ("English costs accuracy")
		double OffsetKappaRho = 0.25;
		double ElevationSigma = 0.4 * kDegToRad; // sigma_theta [rad] (Spin Touch)
		double ElevationRho = 0.25;
		double SpeedSigma = 0.05;           // s_V [1] relative (Speed Control)
		double SpeedRho = 0.3;
		double SoftShotSpeed = 1.0;         // [m/s] soft-shot factor 1 + SoftShotGain max(0, 1 - V_i / SoftShotSpeed)
		double SoftShotGain = 0.5;
		double FlinchLoss = 0.08;           // phi_fl [1] speed loss at P = 1 (Nerve)
		double PressureGainMax = 4.0;       // g_max - 1: g = 1 + P (g_max - 1) L_N
		double GripDrop = 1.5e-3;           // b_grip [m] tip drop at P = 1 (Nerve)
		double NerveRho = 0.125;            // rho of phi_fl, g_max - 1 and b_grip
		double DriftPressureExponent = 1.0 / 3.0; // drift x g^(1/3) (refit 9.2 item 3); tremor x g; speed x g^(1/2)
		double SpeedPressureExponent = 0.5;

		// Situation multipliers (3.4)
		double BridgeRho = 0.4;             // m_br = 1 + (m_br0 - 1) L(Bridge Stability; 0.4)
		double SlipSpeedSkillGain = 0.6;    // V_b = V_b0 (1 + 0.6 (x - 25)/75) ...
		double GloveSlipSpeed = 1.15;       // ... x1.15 with a glove ...
		double SweatSlipSpeed = 0.3;        // ... x(1 - 0.3 S)
		double SlipGain = 0.5;              // m_slip = 1 + 0.5 max(0, V_i - V_b) / V_b
		double StanceRho = 0.2;             // m_st = 1 + d_s L(Stance; 0.2)
		double HeadMoveFactor = 1.5;        // m_head
		double SweatDrift = 0.5;            // m_stick = 1 + 0.5 S (Glove ? 0.2 : 1)
		double GloveSweat = 0.2;
		double FatigueGain = 0.2;           // m_fat = 1 + 0.2 F
		double OffHandFactor = 2.5;         // m_off
		double RushGain = 0.3;              // m_rush = 1 + 0.3 clamp(1 - T_pause / 0.2 s, 0, 1)
		double RushPause = 0.2;             // [s]
		double JabGain = 0.5;               // m_jab = 1 + 0.5 clamp(-a_c / 10 m/s^2, 0, 1)
		double JabAcceleration = 10.0;      // [m/s^2]
		double SettleInTime = 0.8;          // E(t) = 1 + exp(-t / 0.8 s) + min(0.5, 0.03 max(0, t - 10 s))
		double LongHoldStart = 10.0;        // [s]
		double LongHoldRate = 0.03;         // [1/s]
		double LongHoldMax = 0.5;
		double SettleRamp = 1.2;            // k_set: 1 -> 0.3 over 1.2 s (SmoothStep01), ...
		double SettleHold = 4.0;            // ... hold 4 s, ...
		double SettleRelease = 1.5;         // ... then 0.3 -> 1.15 over 1.5 s
		double SettleFloor = 0.3;
		double SettleAfter = 1.15;

		// Execution and contact (3.5-3.7)
		double RampDuration = 0.1;          // [s] per-shot channels ramp in over 0.1 s from t_fwd (rendering only, 3.7)
		double MaxSpeed = 12.0;             // [m/s] V_x clamp (UE input clamp, MOT B.9)
		double OffsetClamp = 0.90;          // rho clamp of the executed contact offset (stays below kCueOffsetValidLimit)
		double ShortCueSpeedCap = 0.8;      // HF-32: V_i capped at 0.8 x MaxSpeed with a short cue

		// Intoxication hook (Q2, HF-20, 3.4): reached only through StrokeSituation::Intoxication, which is 0 unless
		// ProductConfig::Alcohol == AlcoholMode::Mechanic (StrokeIntoxication, rb/Human/Progression.h). V1 ships alcohol
		// cosmetic only, so these PLACEHOLDERS are never active; with I = 0 every factor is exactly 1 (bitwise no effect).
		double IntoxicationCalmLevel = 0.25; // I_c: up to here alcohol only calms ("one beer calms Nerve")
		double IntoxicationCalm = 0.5;       // c_calm: P_x = P (1 - c_calm min(1, I / I_c)) (pressure gain, flinch, grip drop)
		double IntoxicationDriftGain = 1.0;  // k_dr: drift x(1 + k_dr max(0, I - I_c) / (1 - I_c)) ("three hurt Steadiness")
		double IntoxicationTremorGain = 1.0; // k_tr: tremor x(1 + k_tr max(0, I - I_c) / (1 - I_c))
	};

	// ---------------------------------------------------------------------------------------------
	// Situation (3.1, 3.4)
	// ---------------------------------------------------------------------------------------------

	enum class FloorSource : std::uint8_t
	{
		None,
		Rail, // the rail under the butt sets the lowest elevation
		Ball, // a ball under the butt / shaft sets it (FloorBall)
	};

	struct StrokeSituation
	{
		BridgeType Bridge = BridgeType::Closed;
		double BridgeLength = 0.20;    // L_b [m] bridge to tip (UE 5.3/5.4)
		double BridgeToGrip = 0.80;    // L_bg [m]
		double StanceDifficulty = 0.0; // d_s [0, 1] from the IK solver (0 comfortable, 1 max stretch / one foot)
		double Pressure = 0.0;         // P [0, 1] (ComputePressure)
		double Fatigue = 0.0;          // F [0, 1] (FatigueFromNight)
		double Sweat = 0.0;            // S [0, 1]
		bool Glove = false;
		bool OffHand = false;          // HF-19
		double ElevationFloor = 0.0;   // [rad] lowest elevation the cue can take (UE 5.5)
		FloorSource FloorBy = FloorSource::None;
		BallId FloorBall = kNoBall;    // FloorSource::Ball
		bool ShortCue = false;         // HF-32 (48 / 52 in cue near a wall)
		double Intoxication = 0.0;     // I [0, 1] (Q2 hook, HF-20): StrokeIntoxication(ProductConfig, level) = 0 while alcohol is
		                               //   cosmetic (V1); ExecuteStroke maps it onto pressure (calm), drift and tremor (3.4);
		                               //   0 = no effect, bit for bit (HumanParams::Intoxication*)
	};

	// ---------------------------------------------------------------------------------------------
	// Pressure (HF-15, 3.4) and fatigue (HF-16)
	// ---------------------------------------------------------------------------------------------

	inline constexpr double kStakesPractice = 0.0;
	inline constexpr double kStakesFriendly = 0.3;
	inline constexpr double kStakesMoneyOrLeague = 0.6; // league match, or a money game with a small bet (MoneyGameStakes)
	inline constexpr double kStakesFinal = 1.0;

	// Stakes of a money game (Q6 decided: side bets and hustling with in-game cash, 3.4): 0.6 up to a bet of 10 % of the
	// shooter's cash on hand before the bet, rising linearly to 1 (a final) at 50 % or more; Cash <= 0 (all in) -> 1.
	// The same rule for the AI with its own bankroll (a rich hustler feels less). TUNING.
	constexpr double MoneyGameStakes(double Bet, double Cash)
	{
		return Cash <= 0.0 ? kStakesFinal
			: kStakesMoneyOrLeague + (kStakesFinal - kStakesMoneyOrLeague) * Clamp((Bet / Cash - 0.1) / 0.4, 0.0, 1.0);
	}

	struct PressureInputs
	{
		double Stakes = kStakesPractice; // 0 practice, 0.3 friendly, 0.6 league, MoneyGameStakes for a money game, 1 final
		bool GameBall = false;           // this shot can win the rack
		bool Hill = false;               // either player needs one rack
		int Watchers = 0;                // crowd = min(1, watchers / 10)
		double ShotClockFraction = 0.0;  // elapsed / limit while a shot clock runs, else 0
		int RunLength = 0;               // run = min(1, run length / 8)
	};

	struct PressureWeights
	{
		double Stakes = 0.35;
		double GameBall = 0.25;
		double Hill = 0.15;
		double Crowd = 0.10;
		double Clock = 0.10;
		double Run = 0.05;
	};

	// Pressure option (principle 9, 5.4): On, Subtle (P x 0.5), Off (0; also hot-seat can switch it off for both).
	enum class PressureMode : std::uint8_t
	{
		On,
		Subtle,
		Off,
	};

	// P = clamp(0.35 stakes + 0.25 gameBall + 0.15 hill + 0.10 crowd + 0.10 clock + 0.05 run, 0, 1), then the mode.
	// The same formula for the AI (3.4).
	RB_API double ComputePressure(const PressureInputs& Inputs, PressureMode Mode, const PressureWeights& Weights = PressureWeights{});

	// F = clamp((in-game hours of this night - 2) / 2, 0, 1); 0 when !Applies (Practice, hot-seat). Never real-world
	// session time (HF-16).
	RB_API double FatigueFromNight(double InGameHoursThisNight, bool Applies);
}
