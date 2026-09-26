#pragma once

// equipment.md 11.1 constants (SI; angles in rad). Per-table values live in TableSpec presets.
// Owner: WP-2 (equipment & table geometry). Tags: [WPA-EQ] etc. per equipment.md.

#include "rb/Config.h"
#include "rb/Core/Constants.h"

namespace rb
{
	// --- Balls (equipment 6) -----------------------------------------------------------------------
	inline constexpr double kBallDiameter = 0.05715;          // [m] 2.25 in [WPA-EQ 16]
	inline constexpr double kBallRadius = 0.028575;           // [m]
	inline constexpr double kBallDiameterTol = 0.000127;      // [m] +- 0.005 in
	inline constexpr double kBallMass = 0.17009713875;        // [kg] 6 oz (pooltool default)
	inline constexpr double kBallMassMin = 0.156;             // [kg] WPA
	inline constexpr double kBallMassMax = 0.170;             // [kg] WPA
	inline constexpr double kBallInertia = 0.4 * kBallMass * kBallRadius * kBallRadius; // [kg m^2] (2/5) m R^2 = 5.5555809e-5, DERIVED
	                                                           // (exact expression, bitwise equal to MakeBallSpec / BallSpec{})
	inline constexpr double kBallDensity = 1740.4;            // [kg/m^3] DERIVED
	inline constexpr double kCueBallMagneticMass = 0.167;     // [kg] Valley bar boxes [DD-BALLW]
	inline constexpr double kBarObjectBallMassMean = 0.163;   // [kg]
	inline constexpr double kBarObjectBallMassMin = 0.155;    // [kg]
	inline constexpr double kBarObjectBallMassMax = 0.167;    // [kg]
	inline constexpr double kBarObjectBallMassSigma = 0.003;  // [kg] DIVE_BAR sampling (equipment 6.3)
	inline constexpr double kBarObjectBallDiameterMin = 0.05700; // [m] ESTIMATE (worn balls)
	inline constexpr double kCueBallOversizedDiameter = 0.060325;          // [m] 2 3/8 in
	inline constexpr double kCueBallOversizedMass = 0.2211;                // [kg] listed 7.8 oz
	// [kg] DERIVED: a phenolic ball of the standard density, m (D_ov / D)^3 = 0.2000508 (T-UNIT-3: 0.200051).
	inline constexpr double kCueBallOversizedMassEqualDensity =
		kBallMass * (kCueBallOversizedDiameter / kBallDiameter) * (kCueBallOversizedDiameter / kBallDiameter) * (kCueBallOversizedDiameter / kBallDiameter);
	// UK blackball pub pool (equipment 6.4, rules.md 12.4): 2 in object balls, 1 7/8 in cue ball. Masses are
	// not published there: DERIVED at the density of the standard ball, m (D_x / D)^3 (ESTIMATE, verify with a set).
	inline constexpr double kBlackballObjectBallDiameter = 0.0508;         // [m] 2 in
	inline constexpr double kBlackballCueBallDiameter = 0.047625;          // [m] 1 7/8 in
	inline constexpr double kBlackballObjectBallMass =                     // [kg] 0.1194647, ESTIMATE
		kBallMass * (kBlackballObjectBallDiameter / kBallDiameter) * (kBlackballObjectBallDiameter / kBallDiameter) * (kBlackballObjectBallDiameter / kBallDiameter);
	inline constexpr double kBlackballCueBallMass =                        // [kg] 0.0984358, ESTIMATE
		kBallMass * (kBlackballCueBallDiameter / kBallDiameter) * (kBlackballCueBallDiameter / kBallDiameter) * (kBlackballCueBallDiameter / kBallDiameter);
	inline constexpr double kSnookerBallDiameter = 0.0525;                 // [m] (equipment 6.4)
	inline constexpr double kSnookerBallMass = 0.140;                      // [kg] pooltool (equipment 6.4)

	// --- Cushions, pockets, sights (equipment 3-5) ---------------------------------------------------
	inline constexpr double kCushionNoseHeight = 0.03629025;     // [m] h = 63.5 % of D [WPA-EQ 7]
	inline constexpr double kCushionNoseHeightMin = 0.03571875;  // [m] 62.5 %
	inline constexpr double kCushionNoseHeightMax = 0.03686175;  // [m] 64.5 %
	// [rad] theta_c = asin((h - R)/R) = asin(0.27) = 15.66427 deg. (equipment 11.1 / T-CUSH-2 print 0.2733943 rad,
	// which contradicts their own 15.66427 deg and collisions C-G1 / verification log: asin(0.27) = 0.2733930.)
	inline constexpr double kCushionContactAngle = 0.27339303146747335;
	inline constexpr double kCushionContactOffsetXY = 0.027513733700867; // [m] R_c = R cos(theta_c) = 0.0275137 (DERIVED)
	inline constexpr double kCushionWidth = 0.0508;              // [m] nose to feather strip
	inline constexpr double kCushionNoseProfileRadius = 0.001;   // [m] ART ONLY (physics uses r_n = 0)
	inline constexpr double kFacingThicknessPro = 0.003175;      // [m]
	inline constexpr double kFacingThicknessBar = 0.00635;       // [m] ESTIMATE
	inline constexpr double kPocketBackdraftAngle = 0.20943951023931953; // [rad] 12 deg (undercut, INTERPRETATION)
	inline constexpr double kPocketDropPointRadius = 0.0047625;  // [m] r_d [BCA]
	inline constexpr double kPocketLinerUndercut = 0.20943951023931953; // [rad] beta_l 12 deg (collisions 5.3, ESTIMATE)
	inline constexpr double kSightInsetFromNose = 0.0936625;     // [m] 3 11/16 in
	inline constexpr double kSightDiameterRound = 0.0127;        // [m]
	inline constexpr double kThroatMeasureDepth = 0.0508;        // [m] throat measured 2 in behind the noses (TDF)

	// --- Racks (equipment 9) --------------------------------------------------------------------------
	inline constexpr double kRackRowSpacing = 0.86602540378443864676 * kBallDiameter;         // [m] (sqrt 3 / 2) D = 0.0494934
	inline constexpr double kRack15InnerSide = (4.0 + 1.7320508075688772935) * kBallDiameter; // [m] (4 + sqrt 3) D = 0.327587
	inline constexpr double kRack10InnerSide = (3.0 + 1.7320508075688772935) * kBallDiameter; // [m] (3 + sqrt 3) D = 0.270437
	inline constexpr double kRack9DiamondInnerSide = (2.0 + 1.1547005383792515290) * kBallDiameter; // [m] (2 + 2/sqrt 3) D = 0.180291
	inline constexpr double kRackTemplateMaxThickness = 0.00014; // [m] ignored by physics

	// --- Cues (equipment 8) ---------------------------------------------------------------------------
	inline constexpr double kCueLength = 1.4732;                 // [m] 58 in
	inline constexpr double kCueLengthMin = 1.016;               // [m] WPA 40 in
	inline constexpr double kCueMass = 0.5386;                   // [kg] 19 oz
	inline constexpr double kCueMassMax = 0.70874;               // [kg] WPA 25 oz
	inline constexpr double kCueTipDiameter = 0.01275;           // [m]
	inline constexpr double kCueTipDiameterMax = 0.014;          // [m]
	inline constexpr double kCueTipDomeRadiusNickel = 0.0106;    // [m] DERIVED from coin diameter
	inline constexpr double kCueTipDomeRadiusDime = 0.00896;     // [m]
	inline constexpr double kCueShaftLength = 0.7366;            // [m]
	inline constexpr double kCueFerruleLengthLowDeflection = 0.0127; // [m]
	inline constexpr double kCueFerruleLengthMax = 0.0254;       // [m]
	inline constexpr double kCueEndMassTypical = 0.005;          // [kg] "typical" (ratio ~30) [DD-ENDMASS]
	inline constexpr double kJumpCueLength = 1.016;              // [m]
	inline constexpr double kJumpCueMass = 0.2835;               // [kg] ~10 oz (Predator Air); physics preset uses 9 oz
	inline constexpr double kJumpCueTipDiameter = 0.01375;       // [m]
	inline constexpr double kHouseCueLengths[4] = {1.4478, 1.3208, 1.2192, 0.9144}; // [m] 57/52/48/36 in
	inline constexpr double kChalkCubeEdge = 0.022;              // [m] ESTIMATE

	// --- Lighting (equipment 10) ----------------------------------------------------------------------
	inline constexpr double kLightMinIlluminance = 520.0;        // [lux]
	inline constexpr double kLightGlareIlluminance = 5000.0;     // [lux]
	inline constexpr double kLightMinHeightMovable = 1.016;      // [m] above the bed
	inline constexpr double kLightMinHeightFixed = 1.65;         // [m]
	inline constexpr double kBarLightHeight = 0.84;              // [m] range 0.79-0.91
}
