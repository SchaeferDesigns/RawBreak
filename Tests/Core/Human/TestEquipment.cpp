// Owner: WP-11 (player model). human-factors 4.1-4.4, 4.6, 5.2 (tip, chalk, marks, venue, chores, progression): HF-T12..T14,
// HF-B03, HF-B04, HF-B05, HF-B11. Expected values: Tools/reference/human-factors/chalk.py, equil.py.

#include "Human/HumanTestUtil.h"

#include "rb/Human/BallMarks.h"
#include "rb/Human/Chores.h"
#include "rb/Human/Progression.h"
#include "rb/Human/Venue.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/ShotResult.h"

#include <cmath>

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

namespace
{
	double RhoMax(double Mu) { return Mu / std::sqrt(1.0 + Mu * Mu); }

	TipState WithCoverage(const double (&Coverage)[kTipZoneCount])
	{
		TipState Tip;
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			Tip.Coverage[z] = Coverage[z];
		}
		return Tip;
	}
}

RB_TEST(HF_T12_ChalkDecay)
{
	TipState Tip; // standard grade (OldBlue, n_c 18), fresh, medium hardness
	const TipParams Params;
	const TipContactPoint Contact = LookupTipContact(Tip, 0.45, 0.0, Params); // pure right English
	RB_CHECK_NEAR(Contact.Q, 0.748235294, 1e-9);
	RB_CHECK_NEAR(std::fabs(Contact.Beta), kPi, 1e-15);
	RB_CHECK(Contact.Zone == TipZone::Dome);
	RB_CHECK(Contact.Weights[4] == 1.0);
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		RB_CHECK(z == 4 || Contact.Weights[z] == 0.0);
	}
	const double Severity = HitSeverity(2.0, 0.45, false);
	RB_CHECK_NEAR(Severity, 1.005, 1e-15);
	double Coverage[10] = {};
	bool Miscued[11] = {};
	for (int Hit = 1; Hit <= 10; ++Hit)
	{
		const TipContactPoint Now = LookupTipContact(Tip, 0.45, 0.0, Params);
		Miscued[Hit] = 0.45 > RhoMax(Now.Friction);
		ApplyTipWear(Tip, Now, Severity, Params);
		if (Hit <= 9)
		{
			Coverage[Hit] = Tip.Coverage[4];
		}
	}
	RB_CHECK_REL(Coverage[8], 0.639757125, 1e-9);
	RB_CHECK_REL(Coverage[8], 0.6397571251117244, 1e-12);
	RB_CHECK_REL(RhoMax(0.35 + 0.25 * Coverage[8]), 0.454283156, 1e-9);
	RB_CHECK_REL(Coverage[9], 0.605016227, 1e-9);
	RB_CHECK_REL(RhoMax(0.35 + 0.25 * Coverage[9]), 0.448110250, 1e-9);
	for (int Hit = 1; Hit <= 9; ++Hit)
	{
		RB_CHECK(!Miscued[Hit]);
	}
	RB_CHECK(Miscued[10]);
	RB_CHECK(Tip.Coverage[0] == 1.0 && Tip.Coverage[1] == 1.0); // only the struck zone wears
	RB_CHECK(Tip.Hits == 10u);
	// Grade table (4.1): first miscue at rho 0.45, V 2 m/s, no re-chalk: 3rd / 10th / 16th / 23rd hit.
	const ChalkGrade Grades[4] = {ChalkGrade::RailRat, ChalkGrade::OldBlue, ChalkGrade::Tensile, ChalkGrade::Glasshouse};
	const int FirstMiscue[4] = {3, 10, 16, 23};
	for (int g = 0; g < 4; ++g)
	{
		TipState T;
		T.Chalk = Grades[g];
		const double Cap = ChalkGradeSpecFor(Grades[g]).Cap;
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			T.Coverage[z] = Cap;
		}
		int Hit = 1;
		while (!(0.45 > RhoMax(LookupTipContact(T, 0.45, 0.0, Params).Friction)) && Hit < 100)
		{
			ApplyTipWear(T, LookupTipContact(T, 0.45, 0.0, Params), Severity, Params);
			++Hit;
		}
		RB_CHECK(Hit == FirstMiscue[g]);
	}
}

RB_TEST(HF_T13_Chalking)
{
	const double Start[kTipZoneCount] = {0.5, 0.2, 0.9, 0.9, 0.9, 0.9, 0.9};
	const ChalkCube Own; // OldBlue, cap 1, no hollow
	const TipParams Params;
	TipState Drill = WithCoverage(Start);
	TipState Sweep = WithCoverage(Start);
	for (int i = 0; i < 3; ++i)
	{
		ApplyChalkTwist(Drill, Own, 0.0, Params);
		ApplyChalkTwist(Sweep, Own, 1.0, Params);
	}
	RB_CHECK_NEAR(Drill.Coverage[0], 0.968, 1e-12);
	RB_CHECK_NEAR(Drill.Coverage[1], 0.454822, 5e-7);
	RB_CHECK_NEAR(Sweep.Coverage[0], 0.968, 1e-12);
	RB_CHECK_NEAR(Sweep.Coverage[1], 0.866900, 5e-7);
	for (int z = 2; z < kTipZoneCount; ++z)
	{
		RB_CHECK_NEAR(Drill.Coverage[z], 0.931853, 5e-7);
		RB_CHECK_NEAR(Sweep.Coverage[z], 0.983363, 5e-7);
	}
	// Bar cube: cap 0.7, hollow 0.8, H_chalk 1, one twist; no zone decreases.
	ChalkCube Bar = BarChalkCube();
	Bar.Hollow = 0.8;
	TipState BarTip = WithCoverage(Start);
	ApplyChalkTwist(BarTip, Bar, 1.0, Params);
	RB_CHECK_NEAR(BarTip.Coverage[0], 0.62, 1e-12);
	RB_CHECK_NEAR(BarTip.Coverage[1], 0.299, 1e-12);
	for (int z = 2; z < kTipZoneCount; ++z)
	{
		RB_CHECK(BarTip.Coverage[z] == 0.9);
	}
	TipState BarDrill = WithCoverage(Start);
	ApplyChalkTwist(BarDrill, Bar, 0.0, Params);
	RB_CHECK_NEAR(BarDrill.Coverage[1], 0.2264, 1e-12);
	RB_CHECK(ChalkCap(Bar, Params) == 0.7 && ChalkCap(Own, Params) == 1.0);
	RB_CHECK(BarTip.Chalk == ChalkGrade::RailRat);
	// A ritual sweep beyond 1 is capped at the habit-1 result (principle 5).
	TipState Over = WithCoverage(Start);
	ApplyChalkTwist(Over, Own, 3.0, Params);
	TipState One = WithCoverage(Start);
	ApplyChalkTwist(One, Own, 1.0, Params);
	RB_CHECK(Over.Coverage[1] == One.Coverage[1]);
}

RB_TEST(HF_T14_TipEdgeLimit)
{
	const double Width[4] = {12.75e-3, 12.75e-3, 12.75e-3, 11e-3};
	const double Dome[4] = {10.6e-3, 8.96e-3, 18e-3, 18e-3};
	const double Expected[4] = {0.601415, 0.711496, 0.354167, 0.305556};
	for (int i = 0; i < 4; ++i)
	{
		TipState Tip;
		Tip.Width = Width[i];
		Tip.DomeRadius = Dome[i];
		RB_CHECK_NEAR(TipEdgeLimit(Tip), Expected[i], 5e-7);
		// The contact reaches the rim exactly at rho_edge (q = 1): Dome below, Ferrule beyond (no overhang).
		RB_CHECK(LookupTipContact(Tip, 0.999 * TipEdgeLimit(Tip), 0.0, TipParams{}).Zone == TipZone::Dome);
		RB_CHECK(LookupTipContact(Tip, 1.001 * TipEdgeLimit(Tip), 0.0, TipParams{}).Zone == TipZone::Ferrule);
	}
	// A mushroomed rim: Overhang zone with mu_rim.
	TipState Mushroom;
	Mushroom.Overhang = 0.5e-3;
	const TipContactPoint Rim = LookupTipContact(Mushroom, 1.05 * TipEdgeLimit(Mushroom), 0.0, TipParams{});
	RB_CHECK(Rim.Zone == TipZone::Overhang && Rim.Friction == 0.30);
	// R / (R + r_dome) falls from 0.729 (nickel) to 0.614 (18 mm).
	RB_CHECK_NEAR(kR / (kR + 0.0106), 0.729, 5e-4);
	RB_CHECK_NEAR(kR / (kR + 0.018), 0.614, 5e-4);
	RB_CHECK_NEAR(EffectiveTipRestitution(Mushroom, TipParams{}), 0.73, 1e-15);
	Mushroom.Loose = true;
	RB_CHECK_NEAR(EffectiveTipRestitution(Mushroom, TipParams{}), 0.66, 1e-15);
}

RB_TEST(Human_TipWearAndCareChores)
{
	const TipParams Params;
	TipState Tip;
	const TipContactPoint Center = LookupTipContact(Tip, 0.0, 0.0, Params);
	RB_CHECK(Center.Weights[0] == 1.0 && Center.Friction == 0.6);
	// A lookup between two sectors blends them: beta = -90 deg (tip side below the axis for a follow hit b > 0).
	const TipContactPoint Up = LookupTipContact(Tip, 0.0, 0.45, Params);
	RB_CHECK_NEAR(Up.Beta, -0.5 * kPi, 1e-15);
	RB_CHECK_NEAR(Up.Weights[5] + Up.Weights[6], 1.0, 1e-15); // s = 4.5: sectors 5 and 6 half each
	RB_CHECK_NEAR(Up.Weights[5], 0.5, 1e-12);
	double Sum = 0.0;
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		Sum += Up.Weights[z];
	}
	RB_CHECK_NEAR(Sum, 1.0, 1e-15);
	// Wear per hit: dome growth, glaze, overhang by hardness; a miscue triples the severity.
	RB_CHECK_NEAR(HitSeverity(2.0, 0.45, true), 3.015, 1e-12);
	// V1 default: the chalk wears, the tip condition does not (HF-24 wear, HF-25, HF-26 are V2).
	TipState V1;
	ApplyTipWear(V1, Up, 1.0, Params);
	RB_CHECK(V1.DomeRadius == 0.0106 && V1.Glaze == 0.0 && V1.Overhang == 0.0 && V1.Hits == 1u && V1.Coverage[5] < 1.0);
	TipParams Wear = Params;
	Wear.ConditionWear = true;
	TipState Hard;
	Hard.Hardness = TipHardness::Hard;
	TipState Soft;
	Soft.Hardness = TipHardness::Soft;
	ApplyTipWear(Hard, Up, 1.0, Wear);
	ApplyTipWear(Soft, Up, 1.0, Wear);
	RB_CHECK_NEAR(Hard.DomeRadius, 0.0106 + 1e-6, 1e-15);
	RB_CHECK_NEAR(Hard.Glaze, 1.0 / 250.0, 1e-15);
	RB_CHECK_NEAR(Soft.Glaze, 1.0 / 600.0, 1e-15);
	RB_CHECK_NEAR(Hard.Overhang, 0.4 * 2e-7, 1e-20);
	RB_CHECK_NEAR(Soft.Overhang, 1.5 * 2e-7, 1e-20);
	RB_CHECK(Soft.Coverage[5] > Hard.Coverage[5]); // soft tips hold chalk longer (n_c x1.15 vs x0.7)
	// Care chores (4.2).
	TipState Glazed;
	Glazed.Glaze = 0.5;
	Glazed.Overhang = 0.3e-3;
	ScuffTip(Glazed);
	RB_CHECK_NEAR(Glazed.Glaze, 0.1, 1e-15);
	RB_CHECK_NEAR(Glazed.Height, 0.006 - 0.02e-3, 1e-15);
	ShapeTip(Glazed, 0.0106);
	RB_CHECK(Glazed.DomeRadius == 0.0106);
	RB_CHECK_NEAR(Glazed.Height, 0.006 - 0.12e-3, 1e-15);
	TrimTip(Glazed);
	RB_CHECK(Glazed.Overhang == 0.0);
	// Retip: height 6 mm, glaze 0.3 for 50 hits, then the break-in glaze is gone again.
	TipState New = Retip(TipHardness::Medium, 0.0106, 0.01275);
	RB_CHECK(New.Height == 0.006 && New.Glaze == 0.3 && New.BreakInHits == 50u && New.Hits == 0u && New.Coverage[0] == 0.0);
	TipState NewV1 = New;
	for (int i = 0; i < 50; ++i)
	{
		ApplyTipWear(New, Center, 1.0, Wear);
		ApplyTipWear(NewV1, Center, 1.0, Params);
		RB_CHECK(i == 49 || NewV1.Glaze == 0.3);
	}
	TipState Reference; // the same 50 hits from glaze 0
	for (int i = 0; i < 50; ++i)
	{
		ApplyTipWear(Reference, Center, 1.0, Wear);
	}
	RB_CHECK(New.BreakInHits == 0u && NewV1.BreakInHits == 0u);
	RB_CHECK_NEAR(New.Glaze, Reference.Glaze, 1e-14);
	RB_CHECK(NewV1.Glaze == 0.0);
	// The dome flattens up to the cap (V2).
	TipState Flat;
	Flat.DomeRadius = 0.0249995;
	ApplyTipWear(Flat, Center, 1.0, Wear);
	RB_CHECK(Flat.DomeRadius == Wear.MaxDomeRadius);
}

// HF-B03: a miscue happens only when the core's criterion fires; the tip wear after the shot reads the core's flag, never
// the prediction of ExecuteStroke.
RB_TEST(HF_B03_MiscueFlagComesFromTheCore)
{
	Setup S = MakeS0();
	const ExecutedStroke X = Execute(S);
	RB_REQUIRE(X.Error == ErrorCode::Ok);
	RB_CHECK(!X.PredictedMiscue);
	ShotResult Result;
	StrikeOutcome Outcome;
	Outcome.Ball = 0;
	Outcome.Result.Miscue = true; // the core decided "miscue" although nothing predicted it
	Result.Strikes.PushBack(Outcome);
	TipState CoreMiscue = S.Tip;
	ApplyShotToEquipment(X, Result, 0, Quat{}, CoreMiscue, nullptr);
	Result.Strikes[0].Result.Miscue = false;
	TipState CoreGrip = S.Tip;
	ApplyShotToEquipment(X, Result, 0, Quat{}, CoreGrip, nullptr);
	// The centre zone wears with the core's severity: ln c(miscue) = 3 ln c(grip).
	RB_CHECK(X.Contact.Weights[0] == 1.0);
	RB_CHECK_REL(std::log(CoreMiscue.Coverage[0]), 3.0 * std::log(CoreGrip.Coverage[0]), 1e-12);
	RB_CHECK(CoreGrip.Coverage[0] < 1.0);
	RB_CHECK(CoreMiscue.Hits == 1u && CoreGrip.Hits == 1u);
	// A strike index outside the shot changes nothing.
	TipState Untouched = S.Tip;
	ApplyShotToEquipment(X, Result, 3, Quat{}, Untouched, nullptr);
	RB_CHECK(Untouched.Hits == 0u && Untouched.DomeRadius == S.Tip.DomeRadius);
}

RB_TEST(Integ_HF_B03_PredictionAgreesWithTheCoreCriterion)
{
	// For executed strokes across the offset range, StrikeCueBall's miscue equals rho > mu / sqrt(1 + mu^2) with the strike's
	// friction; the human layer adds no other path.
	Setup S = MakeS0();
	for (int i = 0; i < 40; ++i)
	{
		Setup V = S;
		V.Intended.AxisOffsetA = 0.02 * i;
		V.Intended.AxisOffsetB = -0.01 * i;
		V.Key.ShooterShotIndex = static_cast<std::uint32_t>(i);
		V.Tip.Coverage[4] = 0.3; // a worn zone on the right-English side
		const ExecutedStroke X = Execute(V);
		BallState Ball;
		Ball.Position = V.BallPosition;
		const StrikeResult Core = StrikeCueBall(X.Strike, Ball, V.Ball, kClothDefault, SlateParams{}, PinchParams{}, kStandardGravity, NumericsConfig{});
		RB_REQUIRE(Core.Error == ErrorCode::Ok);
		RB_CHECK(Core.Miscue == X.PredictedMiscue);
		RB_CHECK_NEAR(Core.MiscueLimit, X.MiscueLimit, 1e-15);
	}
}

// HF-B04: the revolver rule; chores cost their physical steps.
RB_TEST(HF_B04_RevolverRuleAndChoreDurations)
{
	const TipParams Params;
	const ChalkCube Own;
	TipState Missing40;
	Missing40.Coverage[3] = 0.6;
	RB_CHECK(AutoChalkTwists(Missing40, Own, Params) == 3);
	TipState Missing10;
	Missing10.Coverage[5] = 0.9;
	RB_CHECK(AutoChalkTwists(Missing10, Own, Params) == 1);
	RB_CHECK(AutoChalkTwists(TipState{}, Own, Params) == 0);
	TipState Missing45;
	Missing45.Coverage[1] = 0.55;
	RB_CHECK(AutoChalkTwists(Missing45, Own, Params) == 3); // exactly 3 steps: not rounded up to 4
	// The bar cube's cap: nothing missing below 0.7, no useless twists.
	TipState AtBarCap;
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		AtBarCap.Coverage[z] = 0.7;
	}
	RB_CHECK(AutoChalkTwists(AtBarCap, BarChalkCube(), Params) == 0);
	// 3 twists at habit 0: about 1.2 s.
	double Duration = 0.0;
	TipState Chalked = Missing40;
	RB_CHECK(PerformChalking(Chalked, Own, ChoreMode::Automatic, 0.0, 0.0, -1, Duration, Params) == 3);
	RB_CHECK_NEAR(Duration, 1.2, 1e-12);
	RB_CHECK_NEAR(TwistDuration(1.0, Params), 0.28, 1e-15);
	// Aborting after 2 of 5 twists keeps exactly 2 twists of coverage.
	TipState Bare;
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		Bare.Coverage[z] = 0.25;
	}
	RB_CHECK(AutoChalkTwists(Bare, Own, Params) == 5);
	TipState Aborted = Bare;
	RB_CHECK(PerformChalking(Aborted, Own, ChoreMode::Automatic, 0.4, 0.0, 2, Duration, Params) == 2);
	TipState TwoTwists = Bare;
	ApplyChalkTwist(TwoTwists, Own, 0.4, Params);
	ApplyChalkTwist(TwoTwists, Own, 0.4, Params);
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		RB_CHECK(Aborted.Coverage[z] == TwoTwists.Coverage[z]);
	}
	RB_CHECK_NEAR(Duration, 2.0 * 0.4 * (1.0 - 0.3 * 0.4), 1e-15);
	// Ball clearing grows linearly with the balls left; coins one by one.
	RB_CHECK(BallClearDuration(0) == 0.0);
	RB_CHECK_NEAR(BallClearDuration(6) - BallClearDuration(5), BallClearDuration(1), 1e-12);
	RB_CHECK_NEAR(BallClearDuration(10), 10.0 * BallClearDuration(1), 1e-12);
	RB_CHECK(BallClearDuration(1) >= 1.5 && BallClearDuration(1) <= 3.0);
	RB_CHECK(CoinChoreDuration(4) == 6.0 && CoinChoreDuration(8) == 10.0 && CoinChoreDuration(0) == 0.0);
}

// HF-B05: a skipped chore (A / C / P) gives the habitual result bit-exactly; a ritual can match or beat it, never beyond the
// habit-1 result.
RB_TEST(HF_B05_HabitualResultAndRitualCap)
{
	const TipParams Params;
	const ChalkCube Own;
	TipState Start;
	Start.Coverage[2] = 0.3;
	Start.Coverage[0] = 0.5;
	const double Habit = 0.35;
	TipState A = Start;
	TipState C = Start;
	TipState P = Start;
	double DA = 0.0;
	double DC = 0.0;
	double DP = 0.0;
	PerformChalking(A, Own, ChoreMode::Automatic, Habit, 0.0, -1, DA, Params);
	PerformChalking(C, Own, ChoreMode::Cut, Habit, 0.9, -1, DC, Params);
	PerformChalking(P, Own, ChoreMode::Parallel, Habit, 0.0, -1, DP, Params);
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		RB_CHECK(SameBits(A.Coverage[z], C.Coverage[z]) && SameBits(A.Coverage[z], P.Coverage[z]));
	}
	RB_CHECK(DC == 1.5 && DP == 0.0 && DA > DC);
	// Ritual: the same twists with the measured sweep; better sweep -> better rim, capped at sweep 1.
	TipState Good = Start;
	TipState Perfect = Start;
	TipState Beyond = Start;
	double D = 0.0;
	const int Twists = AutoChalkTwists(Start, Own, Params);
	PerformChalking(Good, Own, ChoreMode::Ritual, Habit, 0.8, Twists, D, Params);
	PerformChalking(Perfect, Own, ChoreMode::Ritual, Habit, 1.0, Twists, D, Params);
	PerformChalking(Beyond, Own, ChoreMode::Ritual, Habit, 7.0, Twists, D, Params);
	RB_CHECK(Good.Coverage[2] > A.Coverage[2]);
	RB_CHECK(Perfect.Coverage[2] >= Good.Coverage[2]);
	RB_CHECK(SameBits(Beyond.Coverage[2], Perfect.Coverage[2]));
	// Racks: the habit's quality, a ritual capped at the habit-1 result; quality -> micro-gaps (4.6).
	RB_CHECK(HabitualRackQuality(0.4) == 0.4 && HabitualRackQuality(-1.0) == 0.0 && HabitualRackQuality(2.0) == 1.0);
	RB_CHECK(CapRitualResult(1.4) == 1.0 && CapRitualResult(0.7) == 0.7);
	const RackGapParams Sloppy = RackGapsForQuality(0.0);
	const RackGapParams Tight = RackGapsForQuality(1.0);
	RB_CHECK_NEAR(Sloppy.Mean, 0.08e-3, 1e-18);
	RB_CHECK_NEAR(Tight.Mean, 0.005e-3, 1e-18);
	RB_CHECK(Sloppy.Jitter == Sloppy.Mean && Tight.Jitter == Tight.Mean && Sloppy.OutlierProbability == 0.0);
	RB_CHECK_NEAR(RackGapsForQuality(0.2).Mean, 0.08e-3 * 0.8 + 0.005e-3 * 0.2, 1e-18); // the hustler's money rack
	RB_CHECK(RackGapsForQuality(1.5).Mean == Tight.Mean);
	RB_CHECK(GrowHabit(0.99) == 1.0);
	RB_CHECK_NEAR(GrowHabit(0.5), 0.52, 1e-15);
}

RB_TEST(Human_ChalkMarksDepositFadeWipe)
{
	const MarkParams Params;
	BallChalkMarks Marks;
	// Deposit in the body frame: a ball turned 90 deg about z stores the world +x contact as body -y.
	const Quat Turned = FromAxisAngle({0.0, 0.0, 1.0}, 0.5 * kPi);
	DepositChalkMark(Marks, Turned, {1.0, 0.0, 0.0}, 1.0, false, Params);
	RB_REQUIRE(Marks.Size() == 1);
	RB_CHECK_NEAR(Marks[0].BodyDir.x, 0.0, 1e-15);
	RB_CHECK_NEAR(Marks[0].BodyDir.y, -1.0, 1e-15);
	RB_CHECK_NEAR(Marks[0].Strength, 1.0, 1e-15);
	RB_CHECK(Marks[0].Radius == 2.5e-3);
	const Vec3 World = Rotate(Turned, Marks[0].BodyDir);
	RB_CHECK_NEAR(World.x, 1.0, 1e-15);
	DepositChalkMark(Marks, Quat{}, {0.0, 1.0, 0.0}, 0.0, false, Params);
	RB_CHECK_NEAR(Marks[1].Strength, 0.3, 1e-15); // 0.3 + 0.7 c
	DepositChalkMark(Marks, Quat{}, {0.0, 0.0, 1.0}, 0.2, true, Params);
	RB_CHECK(Marks[2].Strength == 1.0 && Marks[2].Radius == 4.0e-3); // miscue
	// Full list: the weakest is replaced (ties: the oldest), the list stays in deposit order.
	for (int i = 3; i < kMaxChalkMarks; ++i)
	{
		DepositChalkMark(Marks, Quat{}, {1.0, 0.0, 0.0}, 0.5, false, Params);
	}
	RB_REQUIRE(Marks.IsFull());
	DepositChalkMark(Marks, Quat{}, {-1.0, 0.0, 0.0}, 1.0, false, Params);
	RB_CHECK(Marks.Size() == kMaxChalkMarks);
	RB_CHECK_NEAR(Marks[1].Strength, 1.0, 1e-15); // the 0.3 mark (index 1) is gone, the miscue mark moved up
	RB_CHECK(Marks[1].Radius == 4.0e-3);
	RB_CHECK_NEAR(Marks[kMaxChalkMarks - 1].BodyDir.x, -1.0, 1e-15);
	// Fade: exp(-d_slide / 0.5 m - d_roll / 20 m); marks below 0.05 are dropped.
	BallChalkMarks Fading;
	DepositChalkMark(Fading, Quat{}, {1.0, 0.0, 0.0}, 1.0, false, Params);
	DepositChalkMark(Fading, Quat{}, {0.0, 1.0, 0.0}, 0.0, false, Params);
	FadeChalkMarks(Fading, 0.25, 10.0, Params); // exp(-0.5 - 0.5) = exp(-1)
	RB_REQUIRE(Fading.Size() == 2);
	RB_CHECK_NEAR(Fading[0].Strength, std::exp(-1.0), 1e-15);
	RB_CHECK_NEAR(Fading[1].Strength, 0.3 * std::exp(-1.0), 1e-15);
	FadeChalkMarks(Fading, 0.5, 0.0, Params); // 0.110 x 0.368 = 0.041 < 0.05: dropped, the other stays at 0.135
	RB_CHECK(Fading.Size() == 1);
	RB_CHECK_NEAR(Fading[0].Strength, std::exp(-2.0), 1e-15);
	WipeChalkMarks(Fading);
	RB_CHECK(Fading.IsEmpty());
}

RB_TEST(Human_TravelDistancesFromTracks)
{
	ShotResult Result;
	TrajectorySegment Slide;
	Slide.Motion.State = MotionState::Sliding;
	Slide.Motion.T0 = 0.0;
	Slide.Motion.Vel0 = {2.0, 0.0, 0.0};
	Slide.Motion.Accel2 = {-0.5, 0.25, 0.0}; // curving (masse-like) slide
	Slide.T1 = 0.4;
	TrajectorySegment Roll;
	Roll.Motion.State = MotionState::Rolling;
	Roll.Motion.T0 = 0.4;
	Roll.Motion.Vel0 = {1.0, 0.0, 0.0};
	Roll.Motion.Accel2 = {-0.05, 0.0, 0.0};
	Roll.T1 = 0.4 + 10.0; // rolls to rest: 1 / (2 x 0.05) = 10 s
	TrajectorySegment Island;
	Island.Kind = SegmentKind::Sampled;
	Island.Motion.T0 = 10.4;
	Island.Motion.Pos0 = {0.0, 0.0, 0.0};
	Island.EndPosition = {0.003, 0.004, 0.0};
	Island.T1 = 10.5;
	TrajectorySegment Rest;
	Rest.Motion.State = MotionState::Stationary;
	Rest.Motion.T0 = 10.5;
	Rest.T1 = kInfinity;
	Result.Tracks[3].Segments = {Slide, Roll, Island, Rest};
	const TravelDistances D = ComputeTravelDistances(Result, 3);
	// Reference slide length by a fine midpoint sum.
	double Reference = 0.0;
	const int N = 200000;
	for (int i = 0; i < N; ++i)
	{
		const double Tau = (i + 0.5) * 0.4 / N;
		Reference += std::hypot(2.0 - 1.0 * Tau, 0.5 * Tau) * 0.4 / N;
	}
	RB_CHECK_NEAR(D.Slide, Reference + 0.005, 1e-9);
	RB_CHECK_NEAR(D.Roll, 5.0, 1e-9); // v0^2 / (2 a) = 1 / 0.2
	const TravelDistances None = ComputeTravelDistances(Result, 4);
	RB_CHECK(None.Slide == 0.0 && None.Roll == 0.0);
	RB_CHECK(ComputeTravelDistances(Result, -1).Slide == 0.0 && ComputeTravelDistances(Result, kMaxBalls).Roll == 0.0);
	// A draw shot passes through v = 0 inside the slide (the speed kink): split at the minimum.
	ShotResult Draw;
	TrajectorySegment Back;
	Back.Motion.State = MotionState::Sliding;
	Back.Motion.Vel0 = {1.0, 0.0, 0.0};
	Back.Motion.Accel2 = {-1.0, 0.0, 0.0}; // v = 1 - 2 tau: forward 0.25 m until 0.5 s, back 0.25 m until 1 s
	Back.T1 = 1.0;
	Draw.Tracks[0].Segments = {Back};
	RB_CHECK_NEAR(ComputeTravelDistances(Draw, 0).Slide, 0.5, 1e-12);
}

RB_TEST(Integ_Human_ApplyShotDepositsAtTheContactPoint)
{
	// The mark goes where the tip touched: centre ball -> the back of the ball (-d).
	Setup S = MakeS0();
	S.Params.NoiseScale = 0.0;
	S.Intended.Elevation = 0.0;
	const ExecutedStroke X = Execute(S);
	ShotResult Result;
	StrikeOutcome Outcome;
	Outcome.Ball = 0;
	Result.Strikes.PushBack(Outcome);
	Result.BallsInPlay = 1u;
	BallChalkMarks Marks[kMaxBalls];
	TipState Tip = S.Tip;
	ApplyShotToEquipment(X, Result, 0, Quat{}, Tip, Marks);
	RB_REQUIRE(Marks[0].Size() == 1);
	RB_CHECK_NEAR(Marks[0][0].BodyDir.x, -1.0, 1e-12);
	RB_CHECK_NEAR(Marks[0][0].Strength, 1.0, 1e-15);
}

RB_TEST(Human_ApplyShotFadesOncePerShot)
{
	// Fading runs in the call for the last strike only, for every ball in play, by its travel.
	Setup S = MakeS0();
	const ExecutedStroke X = Execute(S);
	ShotResult Result;
	StrikeOutcome Outcome;
	Outcome.Ball = 0;
	Result.Strikes.PushBack(Outcome);
	Outcome.Ball = 1;
	Result.Strikes.PushBack(Outcome); // the lag: two strikes
	Result.BallsInPlay = (1u << 0) | (1u << 1) | (1u << 2);
	TrajectorySegment Roll;
	Roll.Motion.State = MotionState::Rolling;
	Roll.Motion.Vel0 = {1.0, 0.0, 0.0};
	Roll.T1 = 2.0; // 2 m of rolling
	Result.Tracks[2].Segments = {Roll};
	BallChalkMarks Marks[kMaxBalls];
	DepositChalkMark(Marks[2], Quat{}, {1.0, 0.0, 0.0}, 1.0, false, MarkParams{});
	TipState Tip = S.Tip;
	ApplyShotToEquipment(X, Result, 0, Quat{}, Tip, Marks);
	RB_CHECK(Marks[2][0].Strength == 1.0); // not yet
	ApplyShotToEquipment(X, Result, 1, Quat{}, Tip, Marks);
	RB_CHECK_NEAR(Marks[2][0].Strength, std::exp(-2.0 / 20.0), 1e-15);
	RB_CHECK(Marks[0].Size() == 1 && Marks[1].Size() == 1 && Tip.Hits == 2u);
}

// HF-B11: the same venue seed gives the same house-cue defects, ball set and table slope on every visit.
RB_TEST(HF_B11_VenueStateIsSeeded)
{
	for (std::uint64_t Seed = 1; Seed <= 40; ++Seed)
	{
		for (int Table = 0; Table < 3; ++Table)
		{
			const Vec2 A = SeedTableSlope(Seed, Table, VenueKind::DiveBar, false);
			const Vec2 B = SeedTableSlope(Seed, Table, VenueKind::DiveBar, false);
			RB_CHECK(SameBits(A.x, B.x) && SameBits(A.y, B.y));
			const double Magnitude = std::hypot(A.x, A.y);
			RB_CHECK(Magnitude >= 0.5e-3 - 1e-15 && Magnitude <= 2.5e-3 + 1e-15);
			RB_CHECK(std::hypot(SeedTableSlope(Seed, Table, VenueKind::DiveBar, true).x, SeedTableSlope(Seed, Table, VenueKind::DiveBar, true).y) <=
				1.0e-3 + 1e-15);
			const Vec2 Hall = SeedTableSlope(Seed, Table, VenueKind::PoolHall, false);
			RB_CHECK(std::hypot(Hall.x, Hall.y) <= 0.3e-3 + 1e-15);
			const Vec2 Arena = SeedTableSlope(Seed, Table, VenueKind::Arena, false);
			RB_CHECK(std::hypot(Arena.x, Arena.y) <= 0.1e-3 + 1e-15);
			const TableCondition Condition = MakeVenueTableCondition(Seed, Table, VenueKind::DiveBar, false, true);
			RB_CHECK(SameBits(Condition.Slope.x, A.x) && Condition.BallCling == 1.3 && Condition.ChalkCling);
			RB_CHECK(VenueBallSetSeed(Seed, Table) == VenueBallSetSeed(Seed, Table));
		}
		RB_CHECK(VenueBallSetSeed(Seed, 0) != VenueBallSetSeed(Seed, 1));
		RB_CHECK(SeedTableSlope(Seed, 0, VenueKind::DiveBar, false).x != SeedTableSlope(Seed + 100, 0, VenueKind::DiveBar, false).x);
	}
	RB_CHECK(VenueBallCling(VenueKind::PoolHall) == 1.0 && VenueBallCling(VenueKind::Arena) == 1.0);
	// House cues: the same defects on every visit; the bar "replacing" a cue (next generation) changes them.
	int Bows[3] = {};
	int Loose = 0;
	int Short = 0;
	const int Cues = 2000;
	for (int Slot = 0; Slot < Cues; ++Slot)
	{
		const HouseCue A = SeedHouseCue(77u, Slot, 0u);
		const HouseCue B = SeedHouseCue(77u, Slot, 0u);
		RB_CHECK(SameBits(A.Body.BowSag, B.Body.BowSag) && SameBits(A.Tip.DomeRadius, B.Tip.DomeRadius) && SameBits(A.Spec.Mass, B.Spec.Mass) &&
			A.Tip.Loose == B.Tip.Loose && A.Short == B.Short);
		RB_CHECK(!A.Body.WarpKnown);
		const double Oz = A.Spec.Mass / kOunce;
		RB_CHECK(std::fabs(Oz - std::round(Oz)) < 1e-9 && Oz >= 18.0 - 1e-9 && Oz <= 21.0 + 1e-9);
		RB_CHECK(A.Tip.Width >= 11e-3 && A.Tip.Width <= 13e-3 && A.Tip.DomeRadius >= 12e-3 && A.Tip.DomeRadius <= 20e-3);
		RB_CHECK(A.Tip.Glaze >= 0.3 && A.Tip.Glaze <= 0.9 && A.Tip.Overhang >= 0.0 && A.Tip.Overhang <= 1e-3);
		RB_CHECK(A.Tip.Restitution >= 0.68 && A.Tip.Restitution <= 0.72);
		RB_CHECK(A.Spec.EndMass == kCueHouse19oz.EndMass); // m / m_e = 15
		// TipState is authoritative; the CueSpec view agrees.
		RB_CHECK(A.Spec.TipDomeRadius == A.Tip.DomeRadius && A.Spec.TipRestitution == EffectiveTipRestitution(A.Tip, TipParams{}));
		RB_CHECK(A.Short ? (A.Spec.Length == 48.0 * kInch || A.Spec.Length == 52.0 * kInch) : std::fabs(A.Spec.Length - 57.0 * kInch) < 1e-12);
		Bows[A.Body.BowSag < 0.5e-3 ? 0 : (A.Body.BowSag < 2e-3 ? 1 : 2)] += 1;
		Loose += A.Tip.Loose ? 1 : 0;
		Short += A.Short ? 1 : 0;
	}
	RB_CHECK(std::abs(Bows[0] - 1200) < 90 && std::abs(Bows[1] - 600) < 75 && std::abs(Bows[2] - 200) < 45); // 60 / 30 / 10 %
	RB_CHECK(std::abs(Loose - 200) < 45);
	RB_CHECK(Short > 150 && Short < 350);
	RB_CHECK(SeedHouseCue(77u, 3, 1u).Body.BowSag != SeedHouseCue(77u, 3, 0u).Body.BowSag);
	RB_CHECK(BarChalkCube().Grade == ChalkGrade::RailRat && BarChalkCube().BarCube);
}

RB_TEST(Human_ProgressionXpAndCosts)
{
	// Cost of the next point 100 x 1.08^(x - 25): 25 -> 40: 2,715; 25 -> 50: 7,311; 25 -> 85: 125,321; 25 -> 90: 184,725 XP.
	RB_CHECK_NEAR(AttributePointCost(25.0), 100.0, 1e-12);
	RB_CHECK_NEAR(XpToRaise(25.0, 40.0), 2715.0, 0.5);
	RB_CHECK_NEAR(XpToRaise(25.0, 50.0), 7311.0, 0.5);
	RB_CHECK_NEAR(XpToRaise(25.0, 85.0), 125321.0, 0.5);
	RB_CHECK_NEAR(XpToRaise(25.0, 90.0), 184725.0, 0.5);
	RB_CHECK(XpToRaise(40.0, 25.0) == 0.0);
	// The cost doubles about every 9 points.
	RB_CHECK_NEAR(AttributePointCost(34.0) / AttributePointCost(25.0), 2.0, 0.01);
	RB_CHECK(RepeatFactor(0) == 1.0 && RepeatFactor(1) == 0.5 && RepeatFactor(3) == 0.125);
	RB_CHECK(XpAward(XpSource::LongPot, 2.0) == 20.0 && XpAward(XpSource::LongPot, 9.0) == 50.0 && XpAward(XpSource::LongPot, 0.1) == 5.0);
	RB_CHECK(XpAward(XpSource::LeaveQuality, 0.5) == 10.0 && XpAward(XpSource::LeaveQuality, -1.0) == 0.0);
	RB_CHECK_NEAR(XpAward(XpSource::SpinShot, 0.45), 12.0, 1e-12);
	RB_CHECK(XpAward(XpSource::SpinShot, 0.2) == 0.0);
	RB_CHECK(XpAward(XpSource::PowerShot, 0.0) == 5.0 && XpAward(XpSource::PowerShot, 1.0) == 15.0);
	RB_CHECK(XpAward(XpSource::AwkwardShot, 0.5) == 5.0);
	RB_CHECK(XpAward(XpSource::PressureMake, 0.4) == 0.0 && XpAward(XpSource::PressureMake, 0.8) == 12.0);
	RB_CHECK(XpAward(XpSource::Match, 1.0) == 10.0 && XpAward(XpSource::Match, 0.0) == 6.0); // losses pay 60 %
	RB_CHECK(XpAward(XpSource::Drill, 25.0) == 25.0);
}
