// Owner: WP-2 (equipment & table geometry). equipment.md 6, 11, 13: T-UNIT-1..3, T-CUSH-1, T-WPA-1..3; ball-set
// presets (equipment 6.2-6.4, rules.md 12.4) and preset lookups.

#include "rbtest.h"

#include "rb/Core/Constants.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Math/Scalar.h"

using namespace rb;

// ---------------------------------------------------------------------------------------------------------
// equipment 13: units and constants
// ---------------------------------------------------------------------------------------------------------

RB_TEST(EQP_TUNIT1_BallRadius)
{
	RB_CHECK_NEAR(kBallRadius, 0.028575, 1e-12);
	RB_CHECK_NEAR(kBallDiameter, 2.25 * kInch, 1e-12);
	RB_CHECK(kStandardPoolBall.Radius == kBallRadius);
	RB_CHECK(BallSpec{}.Radius == kBallRadius);
}

RB_TEST(EQP_TUNIT2_BallInertia)
{
	RB_CHECK_NEAR(kBallInertia, 5.5555809e-5, 1e-11);
	RB_CHECK_NEAR(kBallMass, 6.0 * kOunce, 1e-15);
	// The same exact expression everywhere (architecture 7.2): bitwise equal.
	RB_CHECK(kBallInertia == kStandardPoolBall.Inertia);
	RB_CHECK(kBallInertia == BallSpec{}.Inertia);
	RB_CHECK_NEAR(InertiaFactor(kStandardPoolBall), 0.4, 1e-15);
}

RB_TEST(EQP_TUNIT3_OversizedCueBallMassAtPhenolicDensity)
{
	// Same density as the standard ball: m (D_ov / D)^3.
	RB_CHECK_NEAR(kCueBallOversizedMassEqualDensity, 0.200051, 1e-5);
	// From the rounded density constant 1740.4 kg/m^3 and the ball volume.
	const double R = 0.5 * kCueBallOversizedDiameter;
	RB_CHECK_NEAR(kBallDensity * (4.0 / 3.0) * kPi * R * R * R, 0.200051, 1e-5);
	RB_CHECK_NEAR(kBallMass / ((4.0 / 3.0) * kPi * kBallRadius * kBallRadius * kBallRadius), kBallDensity, 0.05);
	// The listed bar ball is heavier than a same-density ball ("221 g (7.8 oz)").
	RB_CHECK_NEAR(kCueBallOversizedMass, 7.8 * kOunce, 1e-4);
	static_assert(kCueBallOversizedMass > kCueBallOversizedMassEqualDensity, "the listed bar ball is denser");
	RB_CHECK(kOversizedCueBall.Radius == 0.5 * kCueBallOversizedDiameter && kOversizedCueBall.Mass == kCueBallOversizedMass);
}

RB_TEST(EQP_TCUSH1_NoseHeight)
{
	RB_CHECK_NEAR(0.635 * kBallDiameter, 0.03629025, 1e-9);
	RB_CHECK_NEAR(kCushionNoseHeight, 0.03629025, 1e-9);
	RB_CHECK_NEAR(kCushionNoseHeightMin, 0.625 * kBallDiameter, 1e-12);
	RB_CHECK_NEAR(kCushionNoseHeightMax, 0.645 * kBallDiameter, 1e-12);
	for (const TablePreset P : {TablePreset::NineFootPro, TablePreset::NineFootTight, TablePreset::EightFootPro, TablePreset::EightFootHome,
			 TablePreset::SevenFootBar, TablePreset::SevenFootTrue})
	{
		RB_CHECK_NEAR(GetTableSpec(P).CushionNoseHeight, 0.03629025, 1e-9);
	}
	RB_CHECK_NEAR(kTableSevenFoot78.CushionNoseHeight, 0.64 * kBallDiameter, 1e-12); // pooltool
}

RB_TEST(Equipment_DerivedConstantsAreExact)
{
	RB_CHECK_NEAR(kRackRowSpacing, 0.0494934, 1e-7);
	RB_CHECK_NEAR(kRack15InnerSide, 0.327587, 1e-6);
	RB_CHECK_NEAR(kRack10InnerSide, 0.270437, 1e-6);
	RB_CHECK_NEAR(kRack9DiamondInnerSide, 0.180291, 1e-6);
	RB_CHECK_NEAR(kCushionContactAngle, Asin((kCushionNoseHeight - kBallRadius) / kBallRadius), 1e-10);
	RB_CHECK_NEAR(kCushionContactOffsetXY, kBallRadius * Cos(kCushionContactAngle), 1e-7);
	// Blackball balls at the density of the standard ball (ESTIMATE).
	RB_CHECK_NEAR(kBlackballObjectBallMass, 0.1194647, 1e-7);
	RB_CHECK_NEAR(kBlackballCueBallMass, 0.0984358, 1e-7);
	RB_CHECK_NEAR(kBlackballObjectBallDiameter, 2.0 * kInch, 1e-12);
	RB_CHECK_NEAR(kBlackballCueBallDiameter, 1.875 * kInch, 1e-12);
}

// ---------------------------------------------------------------------------------------------------------
// equipment 13: WPA validation
// ---------------------------------------------------------------------------------------------------------

namespace
{
	std::uint32_t Bit(WpaCheck C) { return 1u << static_cast<unsigned>(C); }
}

RB_TEST(EQP_TWPA1_NineFootProPassesEverything)
{
	const WpaReport R = ValidateWpa(kTableNineFootPro);
	RB_CHECK(R.AllPassed());
	RB_CHECK(R.FailedMask == 0u);
	// The WPA 8-ft table passes too.
	RB_CHECK(ValidateWpa(kTableEightFootPro).AllPassed());
}

RB_TEST(EQP_TWPA2_SevenFootBarFailsExactly)
{
	const WpaReport R = ValidateWpa(kTableSevenFootBar);
	const std::uint32_t Expected = Bit(WpaCheck::PlayingSurfaceSize) | Bit(WpaCheck::CornerMouth) | Bit(WpaCheck::SideMouth) |
		Bit(WpaCheck::CornerCutAngle) | Bit(WpaCheck::SideCutAngle) | Bit(WpaCheck::CornerShelf);
	RB_CHECK(R.FailedMask == Expected);
	// The listed passes: bed height (0.743 >= 0.74295), rail width, nose height, side shelf.
	RB_CHECK(!R.Failed(WpaCheck::BedHeight));
	RB_CHECK(!R.Failed(WpaCheck::RailWidth));
	RB_CHECK(!R.Failed(WpaCheck::CushionNoseHeight));
	RB_CHECK(!R.Failed(WpaCheck::SideShelf));
	RB_CHECK(R.Failed(WpaCheck::CornerShelf));
}

RB_TEST(EQP_TWPA3_LowNoseFails)
{
	TableSpec Low = kTableNineFootPro;
	Low.CushionNoseHeight = 0.0355;
	const WpaReport R = ValidateWpa(Low);
	RB_CHECK(R.Failed(WpaCheck::CushionNoseHeight));
	RB_CHECK(R.FailedMask == Bit(WpaCheck::CushionNoseHeight));
	// Boundaries are inclusive; just outside fails.
	TableSpec Edge = kTableNineFootPro;
	Edge.CushionNoseHeight = kCushionNoseHeightMin;
	RB_CHECK(ValidateWpa(Edge).AllPassed());
	Edge.CushionNoseHeight = kCushionNoseHeightMin - 1e-6;
	RB_CHECK(ValidateWpa(Edge).FailedMask == Bit(WpaCheck::CushionNoseHeight));
}

RB_TEST(Equipment_WpaFieldByField)
{
	const auto FailsOnly = [](const TableSpec& S, WpaCheck C) { return ValidateWpa(S).FailedMask == Bit(C); };
	TableSpec S = kTableNineFootPro;
	S.Length = 2.54 + 0.004; // beyond +-1/8 in
	RB_CHECK(FailsOnly(S, WpaCheck::PlayingSurfaceSize));
	S = kTableNineFootPro;
	S.Length = 2.54 + 0.003; // inside +-1/8 in
	RB_CHECK(ValidateWpa(S).AllPassed());
	S = kTableNineFootPro;
	S.BedHeight = 0.79;
	RB_CHECK(FailsOnly(S, WpaCheck::BedHeight));
	S = kTableNineFootPro;
	S.RailWidthTotal = 0.2;
	RB_CHECK(FailsOnly(S, WpaCheck::RailWidth));
	S = kTableNineFootPro;
	S.Corner.Mouth = 0.117475; // 4.625 in: the upper limit passes
	RB_CHECK(ValidateWpa(S).AllPassed());
	S.Corner.Mouth = 0.118;
	RB_CHECK(FailsOnly(S, WpaCheck::CornerMouth));
	S = kTableNineFootPro;
	S.Side.Mouth = 0.131;
	RB_CHECK(FailsOnly(S, WpaCheck::SideMouth));
	S = kTableNineFootPro;
	S.Corner.CutAngle = 143.0 * kDegToRad; // 142 + 1 deg passes
	RB_CHECK(ValidateWpa(S).AllPassed());
	S.Corner.CutAngle = 143.1 * kDegToRad;
	RB_CHECK(FailsOnly(S, WpaCheck::CornerCutAngle));
	S = kTableNineFootPro;
	S.Side.CutAngle = 102.9 * kDegToRad;
	RB_CHECK(FailsOnly(S, WpaCheck::SideCutAngle));
	S = kTableNineFootPro;
	S.Side.Shelf = 0.01;
	RB_CHECK(FailsOnly(S, WpaCheck::SideShelf));
	S = kTableNineFootPro;
	S.Backdraft = 16.0 * kDegToRad;
	RB_CHECK(FailsOnly(S, WpaCheck::Backdraft));
	S = kTableNineFootPro;
	S.FacingThickness = 0.001;
	RB_CHECK(FailsOnly(S, WpaCheck::FacingThickness));
	S = kTableNineFootPro;
	S.BedHeight = std::nan("");
	RB_CHECK(FailsOnly(S, WpaCheck::BedHeight));
	// 9FT_TIGHT fails only its mouths.
	RB_CHECK(ValidateWpa(kTableNineFootTight).FailedMask == (Bit(WpaCheck::CornerMouth) | Bit(WpaCheck::SideMouth)));
}

// ---------------------------------------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------------------------------------

RB_TEST(Equipment_PresetLookups)
{
	RB_CHECK(GetTableSpec(TablePreset::NineFootPro).Preset == TablePreset::NineFootPro);
	RB_CHECK(GetTableSpec(TablePreset::Custom).Preset == TablePreset::NineFootPro);
	for (const TablePreset P : {TablePreset::NineFootTight, TablePreset::EightFootPro, TablePreset::EightFootHome, TablePreset::SevenFootBar,
			 TablePreset::SevenFoot78, TablePreset::SevenFootTrue})
	{
		RB_CHECK(GetTableSpec(P).Preset == P);
		RB_CHECK_NEAR(GetTableSpec(P).Length, 2.0 * GetTableSpec(P).Width, 1e-12); // L = 2 W (equipment 1)
	}
	// Bar tables: napped cloth, thicker softer facings (k_f 0.85).
	RB_CHECK(kTableSevenFootBar.Cloth == ClothPreset::NappedBar && kTableSevenFootBar.FacingRestitutionScale == 0.85);
	RB_CHECK(kTableSevenFootBar.Length == 80.0 * kInch && kTableSevenFootBar.Width == 40.0 * kInch);
	RB_CHECK(GetCueSpec(CuePreset::Playing19oz).Mass == kCuePlaying19oz.Mass);
	RB_CHECK(GetCueSpec(CuePreset::Break21oz).TipRestitution == kCueBreak21oz.TipRestitution);
	RB_CHECK(GetCueSpec(CuePreset::Jump9oz).JumpCue);
	RB_CHECK(GetCueSpec(CuePreset::House19oz).Length == kHouseCueLengths[0]);
}

RB_TEST(Equipment_SevenFoot78MatchesPooltoolPockets)
{
	// pooltool 0.6.0 PocketTableSpecs: corner center corner_pocket_depth = 0.0417 beyond the corner point along
	// the diagonal, side center side_pocket_depth = 0.0685 beyond the nose line (see TableSpec.h).
	const TableSpec& T = kTableSevenFoot78;
	const double MouthMidFromCorner = 0.5 * T.Corner.Mouth; // a / sqrt 2 with a = M / sqrt 2
	RB_CHECK_NEAR(T.Corner.Shelf + T.Corner.CaptureRadius - MouthMidFromCorner, 0.0417, 1e-12);
	RB_CHECK_NEAR(T.Side.Shelf + T.Side.CaptureRadius, 0.0685, 1e-12);
	RB_CHECK_NEAR(T.Corner.CutAngle - 0.75 * kPi, 5.3 * kDegToRad, 1e-12);
	RB_CHECK_NEAR(T.Side.CutAngle - 0.5 * kPi, 7.14 * kDegToRad, 1e-12);
}

// ---------------------------------------------------------------------------------------------------------
// Ball sets (equipment 6)
// ---------------------------------------------------------------------------------------------------------

RB_TEST(Equipment_BallSetStandardAndSnooker)
{
	BallSet Set;
	RB_REQUIRE(BuildBallSet(BallSetPreset::StandardPool, 7, Set) == ErrorCode::Ok);
	RB_CHECK(Set.Count == kPoolBallCount);
	for (int i = 0; i < Set.Count; ++i)
	{
		RB_CHECK(Set.Balls[i].Radius == kStandardPoolBall.Radius && Set.Balls[i].Mass == kStandardPoolBall.Mass &&
			Set.Balls[i].Inertia == kStandardPoolBall.Inertia);
	}
	RB_REQUIRE(BuildBallSet(BallSetPreset::Snooker, 7, Set) == ErrorCode::Ok);
	RB_CHECK(Set.Count == 22 && Set.Count <= kMaxBalls);
	for (int i = 0; i < Set.Count; ++i)
	{
		RB_CHECK(Set.Balls[i].Radius == 0.02625 && Set.Balls[i].Mass == 0.140);
		RB_CHECK_NEAR(InertiaFactor(Set.Balls[i]), 0.4, 1e-15);
	}
	RB_CHECK(BuildBallSet(static_cast<BallSetPreset>(99), 7, Set) == ErrorCode::InvalidArgument);
	RB_CHECK(Set.Count == 0);
}

RB_TEST(Equipment_BallSetDiveBarSampling)
{
	// Magnetic cue ball; object balls m ~ N(0.163, 0.003) clamped to [0.155, 0.167], D in [57.00, 57.15) mm.
	double MassSum = 0.0;
	int MassCount = 0;
	int Clamped = 0;
	for (std::uint64_t Seed = 0; Seed < 400; ++Seed)
	{
		BallSet Set;
		RB_REQUIRE(BuildBallSet(BallSetPreset::DiveBar, Seed, Set) == ErrorCode::Ok);
		RB_CHECK(Set.Count == kPoolBallCount);
		RB_CHECK(Set.Balls[0].Radius == kMagneticCueBall.Radius && Set.Balls[0].Mass == 0.167);
		for (int i = 1; i < Set.Count; ++i)
		{
			const BallSpec& B = Set.Balls[i];
			RB_CHECK(B.Mass >= 0.155 && B.Mass <= 0.167);
			RB_CHECK(B.Radius >= 0.5 * 0.05700 && B.Radius < 0.5 * 0.05715);
			RB_CHECK_NEAR(InertiaFactor(B), 0.4, 1e-15);
			MassSum += B.Mass;
			++MassCount;
			Clamped += (B.Mass == 0.155 || B.Mass == 0.167) ? 1 : 0;
		}
		// Deterministic per seed.
		BallSet Again;
		RB_REQUIRE(BuildBallSet(BallSetPreset::DiveBar, Seed, Again) == ErrorCode::Ok);
		for (int i = 0; i < Set.Count; ++i)
		{
			RB_CHECK(Set.Balls[i].Radius == Again.Balls[i].Radius && Set.Balls[i].Mass == Again.Balls[i].Mass &&
				Set.Balls[i].Inertia == Again.Balls[i].Inertia);
		}
	}
	// Clamp at -2.67 sigma (0.155) and +1.33 sigma (0.167): 0.4 % + 9.1 % of the balls; the clamped mean is
	// 0.163 - 0.003 x 0.041 = 0.16288 (6000 samples: standard error 4e-5).
	RB_CHECK_NEAR(MassSum / static_cast<double>(MassCount), 0.16288, 2e-4);
	RB_CHECK(Clamped > MassCount / 20 && Clamped < MassCount * 3 / 20);
	// Different seeds give different sets.
	BallSet A;
	BallSet B;
	RB_REQUIRE(BuildBallSet(BallSetPreset::DiveBar, 1, A) == ErrorCode::Ok);
	RB_REQUIRE(BuildBallSet(BallSetPreset::DiveBar, 2, B) == ErrorCode::Ok);
	RB_CHECK(A.Balls[1].Mass != B.Balls[1].Mass);
}

RB_TEST(Equipment_BallSetOldBarAndBlackball)
{
	BallSet Dive;
	BallSet Old;
	RB_REQUIRE(BuildBallSet(BallSetPreset::DiveBar, 42, Dive) == ErrorCode::Ok);
	RB_REQUIRE(BuildBallSet(BallSetPreset::OldBarOversizedCue, 42, Old) == ErrorCode::Ok);
	RB_CHECK(Old.Count == kPoolBallCount);
	RB_CHECK(Old.Balls[0].Radius == 0.0301625 && Old.Balls[0].Mass == 0.2211); // 60.325 mm, 7.8 oz
	for (int i = 1; i < kPoolBallCount; ++i) // same worn object balls for the same seed
	{
		RB_CHECK(Old.Balls[i].Radius == Dive.Balls[i].Radius && Old.Balls[i].Mass == Dive.Balls[i].Mass);
	}

	BallSet Black;
	RB_REQUIRE(BuildBallSet(BallSetPreset::Blackball, 42, Black) == ErrorCode::Ok);
	RB_CHECK(Black.Count == kPoolBallCount);
	RB_CHECK_NEAR(Black.Balls[0].Radius, 0.5 * 1.875 * kInch, 1e-12);
	RB_CHECK(Black.Balls[0].Mass == kBlackballCueBallMass);
	for (int i = 1; i < kPoolBallCount; ++i)
	{
		RB_CHECK_NEAR(Black.Balls[i].Radius, 0.0254, 1e-12);
		RB_CHECK(Black.Balls[i].Mass == kBlackballObjectBallMass);
		RB_CHECK_NEAR(InertiaFactor(Black.Balls[i]), 0.4, 1e-15);
	}
}
