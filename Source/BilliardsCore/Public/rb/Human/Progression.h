#pragma once

// Progression, difficulty presets and the product-owner switches of the player model (human-factors 5.1-5.4 and
// the decisions of section 7). Every product decision is a ProductConfig value, so a later change of mind is a config
// change, never a code change; the defaults are the recorded decisions (all seven questions answered, v1.3).
// Owner: WP-11 (player model). Part of rb::human. XP values and costs TUNING (pacing target 5.2).

#include "rb/Config.h"
#include "rb/Human/Skill.h"

#include <cstdint>

namespace rb::human
{
	// ---------------------------------------------------------------------------------------------
	// Product-owner switches (human-factors 7)
	// ---------------------------------------------------------------------------------------------

	enum class AlcoholMode : std::uint8_t
	{
		CosmeticOnly, // Q2 DECIDED for V1 (default): drinks are props; StrokeIntoxication = 0, UE camera effects cosmetic only
		Mechanic,     // later option (the owner may want real drunkenness): StrokeIntoxication passes the game's level into
		              //   StrokeSituation::Intoxication, which ExecuteStroke maps onto pressure (one drink calms Nerve), drift
		              //   and tremor ("three hurt Steadiness"); HumanParams::Intoxication* placeholders. No core redesign needed.
	};

	enum class AttributeVisibility : std::uint8_t
	{
		Hidden,            // Q3 DECIDED (default): fully diegetic progression (steadier stroke, mentor remarks, notebook)
		LeagueCardAndMenu, // numbers on the league card and one menu page (kept as a switch only)
	};

	enum class MoneyGames : std::uint8_t
	{
		LeaguePrizeOnly,      // in-game cash only as league prize money (kept as a switch only, e.g. for a rating region)
		SideBetsAndHustling,  // Q6 DECIDED (default): side bets and hustling with IN-GAME cash (never bought with real money,
		                      //   never cashed out; a simulated-gambling descriptor is accepted); stakes via MoneyGameStakes
	};

	enum class ChoreSpeed : std::uint8_t
	{
		Full,    // 45-90 s overhead per 8-ball rack in the dive bar (TUNING)
		Brisk,   // 15-25 s
		Minimal, // < 8 s
	};

	struct ProductConfig
	{
		AlcoholMode Alcohol = AlcoholMode::CosmeticOnly;                 // Q2 (decided for V1; Mechanic = the later hook)
		AttributeVisibility Attributes = AttributeVisibility::Hidden;   // Q3 (decided): hidden, diegetic
		MoneyGames Money = MoneyGames::SideBetsAndHustling;              // Q6 (decided): side bets and hustling, in-game cash
		ChoreSpeed FirstVisitChores = ChoreSpeed::Full;                  // Q5 (decided): Full on the first visit of each venue ...
		ChoreSpeed ReturnVisitChores = ChoreSpeed::Brisk;                // ... Brisk afterwards (the player can change it any time)
		double HotSeatGuestAttribute = 50.0;                             // Q4 (decided): guests at 50 in every attribute ...
		bool HotSeatGuestEarnsXp = false;                                // ... without career XP
		double LowDeflectionMinSteadiness = 0.0;                         // Q7 (decided): no unlock gate; the LD shaft's higher
		                                                                 //   routine-miss rate at low Steadiness is disclosed
	};

	// Q5: Full on the first visit of a venue, Brisk afterwards (unless the player picked a setting).
	constexpr ChoreSpeed DefaultChoreSpeed(const ProductConfig& Config, bool FirstVisitOfVenue)
	{
		return FirstVisitOfVenue ? Config.FirstVisitChores : Config.ReturnVisitChores;
	}

	// Q4: the attributes of a hot-seat guest (and whether the guest earns career XP: Config.HotSeatGuestEarnsXp).
	constexpr ShooterAttributes HotSeatGuestAttributes(const ProductConfig& Config) { return UniformAttributes(Config.HotSeatGuestAttribute); }

	// Q3: whether the UI may show attribute numbers.
	constexpr bool ShowAttributeNumbers(const ProductConfig& Config) { return Config.Attributes == AttributeVisibility::LeagueCardAndMenu; }

	// Q6: whether side bets / hustling (money games with in-game cash) exist (default: yes); league prize money always does.
	constexpr bool MoneyGamesAllowed(const ProductConfig& Config) { return Config.Money == MoneyGames::SideBetsAndHustling; }

	// Q7: whether the low-deflection shaft is unlocked for these attributes (always true with the decided default 0).
	constexpr bool LowDeflectionShaftUnlocked(const ProductConfig& Config, const ShooterAttributes& Attributes)
	{
		return Attributes.Steadiness >= Config.LowDeflectionMinSteadiness;
	}

	// Q2 hook: the StrokeSituation::Intoxication ExecuteStroke sees. CosmeticOnly (V1) -> 0 whatever the level, so drinks never
	// change a stroke; Mechanic -> clamp(Level, 0, 1). Level = the game's intoxication estimate in [0, 1] (UG: from drinks and
	// in-game time, specified together with the mechanic). The same function for the AI (principle 4).
	constexpr double StrokeIntoxication(const ProductConfig& Config, double Level)
	{
		return Config.Alcohol == AlcoholMode::Mechanic ? Clamp(Level, 0.0, 1.0) : 0.0;
	}

	// ---------------------------------------------------------------------------------------------
	// XP (5.2)
	// ---------------------------------------------------------------------------------------------

	enum class XpSource : std::uint8_t
	{
		LongPot,       // Steadiness: 10 D, D = 2 deg / (pocket half-window in OB degrees), clamp [0.5, 5]
		LeaveQuality,  // Speed Control: 20 max(0, improvement)
		SpinShot,      // Spin Touch: 8 rho / 0.3 (rho >= 0.3, no miscue, intended draw/follow within 20 %)
		PowerShot,     // Bridge Stability: 5-15
		AwkwardShot,   // Stance: 10 d_s
		PressureMake,  // Nerve: 15 P (P >= 0.5, real matches only, never from drills)
		Drill,         // targeted, tiered; the first clear of a tier pays 10x
		Match,         // all attributes, small; losses pay 60 %
	};

	// XP for one event of a source; Value = D, improvement, rho, power 0-1, d_s or P per the table of 5.2; Drill: the tier's
	// award from the drill table (returned as is; the caller pays 10x on a first clear and 25 % after the daily soft cap);
	// Match: 1 = won (10 XP per attribute), 0 = lost (60 %: 6 XP). TUNING.
	RB_API double XpAward(XpSource Source, double Value);

	// Cost of the next point: 100 x 1.08^(x - 25) XP (25 -> 40: 2,715; 25 -> 85: 125,321 per attribute).
	RB_API double AttributePointCost(double Attribute);

	// Sum of AttributePointCost over the whole points from From to To.
	RB_API double XpToRaise(double From, double To);

	// Anti-grind: repeated near-identical shots (same binned geometry hash within 20 shots) pay 0.5^Repeats.
	RB_API double RepeatFactor(int RepeatsWithinWindow);

	// ---------------------------------------------------------------------------------------------
	// Difficulty presets and assists (5.4): the physics is fixed; presets change aids and the human-layer scale
	// ---------------------------------------------------------------------------------------------

	enum class DifficultyPreset : std::uint8_t
	{
		Pure,
		Real,     // default
		Assisted,
		Relaxed,
	};

	enum class AimLineAssist : std::uint8_t { Off, Short, Long };
	enum class TipRingAssist : std::uint8_t { Off, Faint, On };
	enum class BodyFoulDisplay : std::uint8_t { Live, Ghosted };

	struct AssistSettings
	{
		AimLineAssist AimLine = AimLineAssist::Off;
		TipRingAssist TipRing = TipRingAssist::Off;    // miscue warning at rho_max(mu at the contact zone)
		double SteeringGain = 0.25;                     // G_lat (UE 5.4; 0 = stroke fully straightened)
		double NoiseScale = 1.0;                        // HumanParams::NoiseScale
		PressureMode Pressure = PressureMode::On;
		bool AnyTipContactIsShot = false;               // Pure: yes; others: commit-hold
		BodyFoulDisplay BodyFouls = BodyFoulDisplay::Live; // same for both players
		bool ObviousCallAssist = true;                  // Call mode (RUL 4.5): Explicit (false) or ObviousAssist (true)
		bool StrokeReportAlways = false;                // false: Practice only
	};

	constexpr AssistSettings GetAssistSettings(DifficultyPreset Preset)
	{
		switch (Preset)
		{
		case DifficultyPreset::Pure: return {AimLineAssist::Off, TipRingAssist::Off, 0.25, 1.0, PressureMode::On, true, BodyFoulDisplay::Live, false, false};
		case DifficultyPreset::Assisted: return {AimLineAssist::Short, TipRingAssist::Faint, 0.10, 0.6, PressureMode::Subtle, false, BodyFoulDisplay::Ghosted, true, true};
		case DifficultyPreset::Relaxed: return {AimLineAssist::Long, TipRingAssist::On, 0.0, 0.3, PressureMode::Off, false, BodyFoulDisplay::Ghosted, true, true};
		case DifficultyPreset::Real: break;
		}
		return {AimLineAssist::Off, TipRingAssist::Off, 0.25, 1.0, PressureMode::On, false, BodyFoulDisplay::Live, true, false};
	}

	// Imperfections slider (principle 9): Sim / Scaled / Low / Off = NoiseScale 1 / 0.6 / 0.3 / 0 (the preset values).
	enum class ImperfectionSetting : std::uint8_t { Sim, Scaled, Low, Off };

	constexpr double NoiseScaleFor(ImperfectionSetting Setting)
	{
		switch (Setting)
		{
		case ImperfectionSetting::Scaled: return 0.6;
		case ImperfectionSetting::Low: return 0.3;
		case ImperfectionSetting::Off: return 0.0;
		case ImperfectionSetting::Sim: break;
		}
		return 1.0;
	}
}
