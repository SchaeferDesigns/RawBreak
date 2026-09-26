#pragma once

// Cue tip state: chalk coverage per tip zone, tip shape and condition, chalk cubes, chalking (the revolver rule)
// and wear per hit (human-factors 4.1, 4.2; HF-21..HF-28). Friction at the actual contact point sets the miscue
// limit of the core (MOT B.4): the tip state never rolls a miscue, it only sets mu, the dome radius and e_tip that
// ExecuteStroke writes into the CueStrikeInput.
// Owner: WP-11 (player model). Part of rb::human. All state is plain data (career save, replay header); every
// function is deterministic.

#include "rb/Config.h"

#include <cstdint>

namespace rb::human
{
	// Zone 0 = centre disc; zones 1..6 = outer ring sectors centred at tip angles beta_k = 60 deg (k - 1), measured on the
	// tip face from +e_r toward +e_u, seen from behind the cue.
	inline constexpr int kTipZoneCount = 7;

	enum class TipHardness : std::uint8_t
	{
		Soft,   // n_c x1.15, glaze N 600, mushrooming x1.5
		Medium, // x1.0, N 400, x1
		Hard,   // x0.7, N 250, x0.4 [DD-HARD]
	};

	// Fictional chalk grades (4.1). Premium chalk never raises mu above mu_fresh; it only slows the decay.
	enum class ChalkGrade : std::uint8_t
	{
		RailRat,    // dried, cupped bar cube: n_c 10, coverage cap 0.7 (free in bars)
		OldBlue,    // standard: n_c 18 (start)
		Tensile,    // premium: n_c 30
		Glasshouse, // elite: n_c 45
	};

	struct ChalkGradeSpec
	{
		double HitsPerGrade = 18.0; // n_c [hits]
		double Cap = 1.0;           // coverage a fresh chalking can reach
	};

	constexpr ChalkGradeSpec ChalkGradeSpecFor(ChalkGrade Grade)
	{
		switch (Grade)
		{
		case ChalkGrade::RailRat: return {10.0, 0.7};
		case ChalkGrade::Tensile: return {30.0, 1.0};
		case ChalkGrade::Glasshouse: return {45.0, 1.0};
		case ChalkGrade::OldBlue: break;
		}
		return {18.0, 1.0};
	}

	// A chalk cube (HF-23): the bar cube is dried and cupped (cap 0.7); an own cube hollows over the career.
	struct ChalkCube
	{
		ChalkGrade Grade = ChalkGrade::OldBlue;
		double Hollow = 0.0;   // h [0, 1]: rim efficiency x(1 - 0.7 h)
		bool BarCube = false;  // coverage cap 0.7 (the RailRat grade's cap)
	};

	struct TipState
	{
		double Coverage[kTipZoneCount] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0}; // c_z [0, 1]
		ChalkGrade Chalk = ChalkGrade::OldBlue; // grade of the chalk on the tip (n_c of the decay)
		double DomeRadius = 0.0106;   // r_dome [m] (nickel 0.0106, dime 0.00896; house tips 0.012-0.020)
		double Width = 0.01275;       // w_tip [m]
		double Height = 0.006;        // [m] leather above the ferrule
		double Glaze = 0.0;           // G [0, 1] (HF-25)
		double Overhang = 0.0;        // o [m] mushroomed, chalk-free rim over the ferrule (HF-26)
		TipHardness Hardness = TipHardness::Medium;
		double Restitution = 0.73;    // e_tip of the tip itself [1] (leather 0.71-0.75, phenolic 0.85)
		bool Loose = false;           // loose / screw-on tip or cracked ferrule: e_tip - 0.07 (HF-28)
		std::uint32_t Hits = 0;
		std::uint32_t BreakInHits = 0; // hits left of a new tip's break-in glaze (Retip: kTipBreakInHits); when it reaches 0 the
		                               //   break-in glaze kNewTipGlaze is removed: G := max(0, (G - 0.3) / (1 - 0.3)), the glaze
		                               //   the per-hit law would have built from 0 (4.2 "glaze 0.3 until 50 hits")
	};

	// A new tip (4.2 retip): leather height 6 mm, a break-in glaze of 0.3 for its first 50 hits.
	inline constexpr double kNewTipHeight = 0.006;       // [m]
	inline constexpr double kNewTipGlaze = 0.3;          // [1]
	inline constexpr std::uint32_t kTipBreakInHits = 50;

	struct TipParams
	{
		double FreshFriction = 0.60;   // mu_fresh [MOT B.4]
		double BareFriction = 0.35;    // mu_bare (TUNING, no primary data)
		double RimFriction = 0.30;     // mu_rim on the overhang (EST)
		double FerruleFriction = 0.20; // (EST)
		double CenterTwistGain = 0.6;  // eta_0 (x(1 - 0.5 G))
		double RingTwistDrill = 0.12;  // eta_r = (0.12 + 0.33 H_chalk)(1 - 0.5 G)(1 - 0.7 hollow)
		double RingTwistSweep = 0.33;
		double HollowLoss = 0.7;
		double BarCubeCap = 0.7;
		double TwistCoverageStep = 0.15; // auto-chalking: 1 twist per 15 % missing
		double TwistSeconds = 0.4;       // [s] per twist, x(1 - 0.3 H_chalk)
		double TwistHabitSpeedup = 0.3;
		bool ConditionWear = false;       // per-hit tip CONDITION wear (dome flattening, glazing, mushrooming; HF-24 wear, HF-25,
		                                  //   HF-26: all V2 in the catalogue 2.2). V1 (false): the chalk coverage wears (HF-21, V1),
		                                  //   the condition stays as seeded / chored (house tips, scuff, shape, trim, retip). HF-T12
		                                  //   and the grade table of 4.1 are the V1 values (the chalk oracle has no glaze coupling)
		double DomeGrowthPerHit = 1.0e-6; // [m] x severity, cap MaxDomeRadius
		double MaxDomeRadius = 0.025;     // [m]
		double GlazeHitsMedium = 400.0;   // N_glaze (hard 250, soft 600)
		double GlazeHitsHard = 250.0;
		double GlazeHitsSoft = 600.0;
		double OverhangPerHit = 2.0e-7;   // [m] x severity x (soft 1.5 / medium 1 / hard 0.4)
		double LoosePenalty = 0.07;       // e_tip reduction (HF-28)
		double RetentionSoft = 1.15;      // h(hardness) of n_c_eff = n_c h (1 - 0.5 G): soft 1.15, medium 1, hard 0.7 [DD-HARD]
		double RetentionHard = 0.7;
		double GlazeRetentionLoss = 0.5;  // retention and twist efficiency x(1 - 0.5 G) (HF-25)
		double OverhangSoft = 1.5;        // mushrooming factor soft 1.5 / medium 1 / hard 0.4 (HF-26)
		double OverhangHard = 0.4;
		double ScuffGlazeFactor = 0.2;    // scuff: G *= 0.2
		double ScuffHeightLoss = 0.02e-3; // [m] scuff: height -= 0.02 mm
		double ShapeHeightLoss = 0.1e-3;  // [m] shape: height -= 0.1 mm
	};

	enum class TipZone : std::uint8_t
	{
		Dome,     // q <= 1: chalk map friction
		Overhang, // 1 < q <= 1 + o / (w/2): mu_rim
		Ferrule,  // beyond: mu_ferrule
	};

	// Contact of the tip at the executed contact-point offsets (a, b) (3.6, 4.1).
	struct TipContactPoint
	{
		double Q = 0.0;          // r_dome rho / (w_tip / 2): contact radius on the tip / usable tip radius
		double Beta = 0.0;       // atan2(-b, -a) [rad]: the tip side opposite the offset, seen from behind the cue
		TipZone Zone = TipZone::Dome;
		double Weights[kTipZoneCount] = {}; // omega_z of the lookup (4.1): wear goes to the same zones
		double Coverage = 1.0;   // c(q, beta)
		double Friction = 0.6;   // mu at the contact (Dome: mu_bare + (mu_fresh - mu_bare) c)
	};

	// 4.1 lookup: w_r = SmoothStep01((q - 0.2) / 0.3), s = beta / (pi/3) mod 6, k0 = floor(s), f = s - k0,
	// c = (1 - w_r) c_0 + w_r ((1 - f) c_{1+k0} + f c_{1+((k0+1) mod 6)}); zone and mu per 3.6.
	RB_API TipContactPoint LookupTipContact(const TipState& Tip, double OffsetA, double OffsetB, const TipParams& Params);

	// rho_edge = (w_tip / 2) / r_dome (HF-24): 0.601415 nickel, 0.354167 flat 18 mm (HF-T14).
	constexpr double TipEdgeLimit(const TipState& Tip) { return 0.5 * Tip.Width / Tip.DomeRadius; }

	// e_tip written into the strike: Restitution - LoosePenalty if Loose.
	constexpr double EffectiveTipRestitution(const TipState& Tip, const TipParams& Params) { return Tip.Loose ? Tip.Restitution - Params.LoosePenalty : Tip.Restitution; }

	// Hit severity (0.5 + 0.25 V / (1 m/s)) (0.6 + 2 rho^2) (Miscue ? 3 : 1): 1.005 at V = 2 m/s, rho = 0.45.
	RB_API double HitSeverity(double Speed, double Rho, bool Miscue);

	// Wear after one hit (4.1, 4.2): c_z *= exp(-omega_z severity / n_c_eff), n_c_eff = n_c(grade) h(hardness) (1 - 0.5 G);
	// with Params.ConditionWear (V2) also r_dome += 1e-6 m severity (cap), G += (1 - G) severity / N_glaze, o += 2e-7 m severity x
	// hardness; Hits + 1; a new tip's break-in glaze ends after kTipBreakInHits hits (BreakInHits).
	RB_API void ApplyTipWear(TipState& Tip, const TipContactPoint& Contact, double Severity, const TipParams& Params);

	// Coverage a fresh chalking can reach: the grade's cap, 0.7 for a bar cube.
	RB_API double ChalkCap(const ChalkCube& Cube, const TipParams& Params);

	// One twist (the revolver rule, 4.1): c_0 += max(0, cap - c_0) eta_0, ring zones c_z += max(0, cap - c_z) eta_r, with
	// Sweep = H_chalk (A/C/P modes) or the sweep coverage measured from the input (R mode, clamped to [0, 1], so a ritual
	// never beats the habit-1 result, principle 5). Sets Tip.Chalk = Cube.Grade.
	RB_API void ApplyChalkTwist(TipState& Tip, const ChalkCube& Cube, double Sweep, const TipParams& Params);

	// Automatic chalking before every shot (A/C/P): n_tw = ceil((cap - min_z c_z) / 0.15), 0 when nothing is missing
	// (HF-B04: 40 % missing -> 3 twists, 10 % -> 1).
	RB_API int AutoChalkTwists(const TipState& Tip, const ChalkCube& Cube, const TipParams& Params);

	// Seconds per twist: 0.4 (1 - 0.3 H_chalk).
	RB_API double TwistDuration(double ChalkHabit, const TipParams& Params);

	// Tip care chores (4.2): scuff (5-8 s): G *= 0.2, height -= 0.02 mm; shape (20-40 s): r_dome = target, height -= 0.1 mm;
	// trim: overhang 0; retip (career days): a new tip, height 6 mm, glaze 0.3 until 50 hits (BreakInHits; ApplyTipWear then
	// removes the 0.3 again, keeping the glaze gained meanwhile), no overhang, bare leather (coverage 0: the next chalking,
	// automatic before every shot in A / C / P mode, covers it), e_tip and chalk grade of the default TipState. Heights never
	// drop below 0.
	RB_API void ScuffTip(TipState& Tip);
	RB_API void ShapeTip(TipState& Tip, double TargetDomeRadius);
	RB_API void TrimTip(TipState& Tip);
	RB_API TipState Retip(TipHardness Hardness, double DomeRadius, double Width);
}
