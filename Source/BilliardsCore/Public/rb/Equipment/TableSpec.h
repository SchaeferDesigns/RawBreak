#pragma once

// Table presets (equipment 1, 2, 4, 5, 11.2) and WPA validation. The TableSpec is the SINGLE source of
// truth: BuildTableGeometry (rb/Geometry/TableGeometry.h) turns it into the exact physics geometry and
// everything the renderer needs; nothing else may hard-code table numbers.
// Owner: WP-2 (equipment & table geometry).
//
// All lengths nose-to-nose (WPA). Angles in radians. Values tagged ESTIMATE/VERIFY in equipment.md
// are marked the same way here.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Ids.h"

#include <cstdint>

namespace rb
{
	struct PocketSpec
	{
		double Mouth = 0.0;         // point-to-point distance between the VIRTUAL jaw points [m]
		double CutAngle = 0.0;      // C: interior material angle between nose line and facing [rad]
		                            //    (142 deg corner / 104 deg side WPA); phi = pi - C, beta = C - 3pi/4 (corner), C - pi/2 (side)
		double Shelf = 0.0;         // mouth-line midpoint to the vertical slate cut, incl. bevel [m]
		double JawRadius = 0.0;     // r_j: plan radius of the rounded jaw point [m] (ESTIMATE)
		double CaptureRadius = 0.0; // r_p: radius of the capture cylinder / hole wall [m]
	};

	struct TableSpec
	{
		TablePreset Preset = TablePreset::Custom;
		const char* Name = "Custom";
		double Length = 2.54;               // L: playing length, nose to nose [m]
		double Width = 1.27;                // W: playing width [m]
		double BedHeight = 0.765;           // cloth above the floor [m] (camera / avatar only)
		double CushionNoseHeight = 0.03629025; // h above the cloth [m]
		double CushionWidth = 0.0508;       // nose to feather strip [m]
		double CushionNoseProfileRadius = 0.001; // [m] ART ONLY; physics uses CushionParams::NoseProfileRadius (0)
		double RailWidthTotal = 0.1778;     // nose to outer rail edge [m]
		double RailTopZ = 0.048;            // rail cap above the cloth [m] (ESTIMATE)
		double SlateThickness = 0.0254;     // [m]
		double SightInset = 0.0936625;      // sight centers behind the nose line [m]
		double SightDiameter = 0.0127;      // [m]
		PocketSpec Corner;
		PocketSpec Side;
		double Backdraft = 12.0 * kDegToRad;   // beta_v: facing undercut from vertical [rad] (INTERPRETATION: undercut)
		double DropPointRadius = 0.0047625;    // r_d: slate-edge rounding at the drop [m]
		double FacingThickness = 0.003175;     // [m]
		double LinerUndercut = 12.0 * kDegToRad; // beta_l: liner wall undercut [rad] (ESTIMATE, collisions 5.3)
		bool HasPockets = true;             // false: closed rectangle of 4 cushions (carom, robustness tests); see CushionId
		ClothPreset Cloth = ClothPreset::Default;
		// Table-dependent contact parameters (single source of truth: rb::MakePhysicsParams copies them into
		// PhysicsParams; nothing else may hold per-table physics values):
		double FacingRestitutionScale = 1.0; // k_f: e_f = k_f e_c(v_perp) (collisions 5.5): 1.0 hard thin facings, 0.85 bar (ESTIMATE)
		double LinerRestitution = 0.3;      // e_l leather/rubber liner and back wall (collisions 5.3, ESTIMATE; "spits out" knob)
		double LinerFriction = 0.3;         // mu_l (ESTIMATE)
	};

	// --- Presets (equipment 11.2) -------------------------------------------------------------------
	inline constexpr TableSpec kTableNineFootPro{TablePreset::NineFootPro, "TABLE_9FT_PRO", 2.54, 1.27, 0.765, 0.03629025, 0.0508, 0.001, 0.1778, 0.048,
		0.0254, 0.0936625, 0.0127, {0.1143, 142.0 * kDegToRad, 0.041275, 0.004, 0.062}, {0.127, 104.0 * kDegToRad, 0.0047625, 0.004, 0.0645},
		12.0 * kDegToRad, 0.0047625, 0.003175, 12.0 * kDegToRad, true, ClothPreset::WorstedFast};

	inline constexpr TableSpec kTableNineFootTight{TablePreset::NineFootTight, "TABLE_9FT_TIGHT", 2.54, 1.27, 0.765, 0.03629025, 0.0508, 0.001, 0.1778,
		0.048, 0.0254, 0.0936625, 0.0127, {0.10795, 142.0 * kDegToRad, 0.0381, 0.004, 0.062}, {0.12065, 104.0 * kDegToRad, 0.003175, 0.004, 0.0645},
		12.0 * kDegToRad, 0.0047625, 0.003175, 12.0 * kDegToRad, true, ClothPreset::WorstedFast};

	// WPA 8-ft (92 x 46 in); rail width and pockets as 9FT_PRO (ESTIMATE).
	inline constexpr TableSpec kTableEightFootPro{TablePreset::EightFootPro, "TABLE_8FT_PRO", 2.3368, 1.1684, 0.765, 0.03629025, 0.0508, 0.001, 0.1778,
		0.048, 0.0254, 0.0936625, 0.0127, {0.1143, 142.0 * kDegToRad, 0.041275, 0.004, 0.062}, {0.127, 104.0 * kDegToRad, 0.0047625, 0.004, 0.0645},
		12.0 * kDegToRad, 0.0047625, 0.003175, 12.0 * kDegToRad, true, ClothPreset::WorstedFast};

	// Home 8-ft (88 x 44 in); pockets = older BCA "standard" mouths 4 7/8 in / 5 3/8 in (ESTIMATE).
	inline constexpr TableSpec kTableEightFootHome{TablePreset::EightFootHome, "TABLE_8FT_HOME", 2.2352, 1.1176, 0.765, 0.03629025, 0.0508, 0.001, 0.1651,
		0.048, 0.0254, 0.0936625, 0.0127, {0.123825, 142.0 * kDegToRad, 0.041275, 0.004, 0.062}, {0.136525, 104.0 * kDegToRad, 0.0047625, 0.004, 0.0645},
		12.0 * kDegToRad, 0.0047625, 0.003175, 12.0 * kDegToRad, true, ClothPreset::Default};

	// US coin-op bar box (80 x 40 in), dive-bar default. Cut angles and shelves ESTIMATE (equipment 5.2).
	inline constexpr TableSpec kTableSevenFootBar{TablePreset::SevenFootBar, "TABLE_7FT_BAR", 2.032, 1.016, 0.743, 0.03629025, 0.0508, 0.001, 0.1651, 0.048,
		0.0254, 0.0936625, 0.0127, {0.123825, 138.0 * kDegToRad, 0.00635, 0.006, 0.062}, {0.12065, 100.0 * kDegToRad, 0.0, 0.006, 0.0645},
		12.0 * kDegToRad, 0.0047625, 0.00635, 12.0 * kDegToRad, true, ClothPreset::NappedBar, 0.85, 0.3, 0.3};

	// pooltool default table (78 x 39 in, PocketTableSpecs = SEVEN_FOOT_SHOWOOD) for cross-validation only.
	// Verified (WP-2) against pooltool 0.6.0 objects/table/specs.py + layout.py: l = 1.9812, w = l/2, cushion
	// width 0.0508, cushion height 0.64 D, corner mouth 0.118 between the virtual points (jaw points at
	// pw cos(pi/4) along each rail), corner_pocket_angle 5.3 deg (C = 140.3 deg), side mouth 0.137,
	// side_pocket_angle 7.14 deg (C = 97.14 deg), jaw radii 20.95 / 7.95 mm (tangent offsets r_j / tan(C/2) as
	// pooltool's dc / ds), capture radii 0.062 / 0.0645. pooltool places the capture circles by depth: corner
	// center corner_pocket_depth = 0.0417 beyond the corner point along the diagonal, side center
	// side_pocket_depth = 0.0685 beyond the nose line; with C_cap = mouth midpoint + (shelf + r_p) axis this is
	// corner shelf = 0.0417 + 0.118 / 2 - 0.062 = 0.0387 and side shelf = 0.0685 - 0.0645 = 0.004 (so the
	// CaptureCircle pockets coincide with pooltool's). Differences that remain (by design): pooltool's side
	// facings use the slope sin(angle) instead of tan(angle) (equipment 12.3, 0.06 deg), rail width, bed height,
	// rail-top and drop values (pooltool has no rail top or drop edge).
	inline constexpr TableSpec kTableSevenFoot78{TablePreset::SevenFoot78, "TABLE_7FT_78", 1.9812, 0.9906, 0.765, 0.036576, 0.0508, 0.001, 0.1651, 0.048,
		0.0254, 0.0936625, 0.0127, {0.118, 140.3 * kDegToRad, 0.0387, 0.02095, 0.062}, {0.137, 97.14 * kDegToRad, 0.004, 0.00795, 0.0645},
		12.0 * kDegToRad, 0.0047625, 0.003175, 12.0 * kDegToRad, true, ClothPreset::Default};

	// "True" 7-ft (76 x 38 in) with bar pockets (ESTIMATE).
	inline constexpr TableSpec kTableSevenFootTrue{TablePreset::SevenFootTrue, "TABLE_7FT_TRUE", 1.9304, 0.9652, 0.743, 0.03629025, 0.0508, 0.001, 0.1651,
		0.048, 0.0254, 0.0936625, 0.0127, {0.123825, 138.0 * kDegToRad, 0.00635, 0.006, 0.062}, {0.12065, 100.0 * kDegToRad, 0.0, 0.006, 0.0645},
		12.0 * kDegToRad, 0.0047625, 0.00635, 12.0 * kDegToRad, true, ClothPreset::NappedBar, 0.85, 0.3, 0.3};

	constexpr TableSpec GetTableSpec(TablePreset Preset)
	{
		switch (Preset)
		{
		case TablePreset::NineFootTight: return kTableNineFootTight;
		case TablePreset::EightFootPro: return kTableEightFootPro;
		case TablePreset::EightFootHome: return kTableEightFootHome;
		case TablePreset::SevenFootBar: return kTableSevenFootBar;
		case TablePreset::SevenFoot78: return kTableSevenFoot78;
		case TablePreset::SevenFootTrue: return kTableSevenFootTrue;
		case TablePreset::NineFootPro:
		case TablePreset::Custom: break;
		}
		return kTableNineFootPro;
	}

	// --- WPA validation (equipment 11 sketch, tests T-WPA-1..3) ---------------------------------------
	// Every range is closed (the WPA limits themselves pass) with a 1e-9 m / 1e-9 rad allowance for the
	// decimal -> binary rounding of the limits; a non-finite field fails its check.
	enum class WpaCheck : std::uint8_t
	{
		PlayingSurfaceSize, // 9-ft (100 x 50 in) or 8-ft (92 x 46 in), +- 1/8 in on each dimension
		BedHeight,          // 29.25-31 in
		RailWidth,          // 4-7.5 in (RailWidthTotal)
		CushionNoseHeight,  // 62.5-64.5 % of the standard D = 57.15 mm
		CornerMouth,        // 4.5-4.625 in
		SideMouth,          // 5-5.125 in
		CornerCutAngle,     // 142 +- 1 deg
		SideCutAngle,       // 104 +- 1 deg
		CornerShelf,        // 1-2.25 in
		SideShelf,          // 0-0.375 in
		Backdraft,          // 12-15 deg
		FacingThickness,    // 1/16-1/4 in
		Count
	};

	struct WpaReport
	{
		std::uint32_t FailedMask = 0; // bit per WpaCheck

		constexpr bool Failed(WpaCheck Check) const { return ((FailedMask >> static_cast<unsigned>(Check)) & 1u) != 0u; }
		constexpr bool AllPassed() const { return FailedMask == 0; }
	};

	// Per-field pass/fail against WPA Recommended Equipment Specifications (ball diameter = standard).
	RB_API WpaReport ValidateWpa(const TableSpec& Spec);
}
