// Owner: WP-11 (player model). Second adversarial review of rb::human (not spec IDs): non-finite chore measurements must never
// poison persistent state (principle 5), the rows of the 3.10 budget table that HF-S04..S06 do not pin, the DERIVED chalk
// equilibrium of 4.1 under automatic chalking, monotone situation multipliers (3.4), and randomized invariants of the executed
// stroke (always a strike the core accepts, the rendered pose is the executed one) and of the tip state (coverage, weights, mu,
// the ritual cap). Expected values: Tools/reference/human-factors/recompute_v12.py + budget.py (3.10 rows), equil.py (4.1).

#include "Human/HumanTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Human/Chores.h"
#include "rb/Human/CueState.h"
#include "rb/Human/Venue.h"
#include "rb/Physics/CueStrike.h"

#include <cmath>
#include <limits>

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

namespace
{
	bool CoverageFinite(const TipState& Tip)
	{
		for (const double C : Tip.Coverage)
		{
			if (!std::isfinite(C) || C < 0.0 || C > 1.0)
			{
				return false;
			}
		}
		return true;
	}

	bool SameCoverage(const TipState& A, const TipState& B)
	{
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			if (!SameBits(A.Coverage[z], B.Coverage[z]))
			{
				return false;
			}
		}
		return true;
	}
}

// The R-mode results come from measured UE motion (sweep coverage, push-and-lift rack quality) and the habits from the career
// save. A NaN measurement used to pass rb::Clamp unchanged (NaN compares false), so one bad sample made the tip's chalk
// coverage NaN (saved with the cue: every later ExecuteStroke with it InvalidArgument until a retip) and the rack gaps NaN
// (NaN ball positions in the rack). A non-finite unit value now counts as 0 (a drill / the loosest rack / habit 0); +-inf
// clamp to the ends of [0, 1] as before.
RB_TEST(Human_NonFiniteChoreInputsNeverPoisonTheState)
{
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	const TipParams Params;
	const ChalkCube Own;
	TipState Start;
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		Start.Coverage[z] = 0.3 + 0.05 * z;
	}

	// One twist with a NaN measured sweep: a drill (sweep 0), not a NaN tip.
	TipState Twisted = Start;
	ApplyChalkTwist(Twisted, Own, NaN, Params);
	TipState Drilled = Start;
	ApplyChalkTwist(Drilled, Own, 0.0, Params);
	RB_CHECK(CoverageFinite(Twisted) && SameCoverage(Twisted, Drilled));
	TipState PlusInf = Start;
	ApplyChalkTwist(PlusInf, Own, Inf, Params);
	TipState Swept = Start;
	ApplyChalkTwist(Swept, Own, 1.0, Params);
	RB_CHECK(SameCoverage(PlusInf, Swept));
	// A corrupted cube hollow or tip glaze does not spread into the coverage either.
	ChalkCube Hollowed = Own;
	Hollowed.Hollow = NaN;
	TipState FromHollow = Start;
	ApplyChalkTwist(FromHollow, Hollowed, 0.5, Params);
	RB_CHECK(CoverageFinite(FromHollow));
	TipState GlazeNaN = Start;
	GlazeNaN.Glaze = NaN;
	ApplyChalkTwist(GlazeNaN, Own, 0.5, Params);
	RB_CHECK(CoverageFinite(GlazeNaN));

	// The chore: R mode with a NaN sweep stays finite and capped at the habit-1 result; A mode with a NaN habit is habit 0.
	double Duration = 0.0;
	TipState Ritual = Start;
	PerformChalking(Ritual, Own, ChoreMode::Ritual, 0.5, NaN, -1, Duration, Params);
	TipState Habit1 = Start;
	PerformChalking(Habit1, Own, ChoreMode::Automatic, 1.0, 0.0, -1, Duration, Params);
	RB_CHECK(CoverageFinite(Ritual));
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		RB_CHECK(Ritual.Coverage[z] <= Habit1.Coverage[z]);
	}
	TipState AutoNaN = Start;
	PerformChalking(AutoNaN, Own, ChoreMode::Automatic, NaN, 0.0, -1, Duration, Params);
	RB_CHECK(std::isfinite(Duration) && Duration > 0.0);
	TipState Auto0 = Start;
	double Duration0 = 0.0;
	PerformChalking(Auto0, Own, ChoreMode::Automatic, 0.0, 0.0, -1, Duration0, Params);
	RB_CHECK(SameCoverage(AutoNaN, Auto0) && Duration == Duration0);
	RB_CHECK(TwistDuration(NaN, Params) == TwistDuration(0.0, Params));

	// Racks: a NaN quality or habit is the loosest valid rack, never NaN gaps; the ritual cap never returns NaN.
	const RackGapParams Gaps = RackGapsForQuality(NaN);
	RB_CHECK(std::isfinite(Gaps.Mean) && std::isfinite(Gaps.Jitter));
	RB_CHECK(Gaps.Mean == RackGapsForQuality(0.0).Mean && Gaps.Jitter == RackGapsForQuality(0.0).Jitter);
	RB_CHECK(RackGapsForQuality(Inf).Mean == RackGapsForQuality(1.0).Mean && RackGapsForQuality(-Inf).Mean == RackGapsForQuality(0.0).Mean);
	RB_CHECK(HabitualRackQuality(NaN) == 0.0 && CapRitualResult(NaN) == 0.0);
	RB_CHECK(HabitualRackQuality(Inf) == 1.0 && CapRitualResult(-Inf) == 0.0);
	static_assert(HabitualRackQuality(0.4) == 0.4 && CapRitualResult(1.4) == 1.0);
	// Warp check habit: a NaN habit notices from 1 mm (habit 0). A corrupted habit grows from 0, not straight to the maximum.
	RB_CHECK(NoticeableBow(NaN) == NoticeableBow(0.0));
	RB_CHECK(GrowHabit(NaN) == kHabitGainPerGoodExecution && GrowHabit(-3.0) == kHabitGainPerGoodExecution && GrowHabit(0.5) == 0.52);
}

// The rows of the 3.10 table that HF-S04..S06 do not pin (budget model, perfect input, 20 000 shots, keys of HF-S04): the
// elevated-bridge info row (16.9 %), B1 with the standard own cue (1.15 %), the LD shaft at Steadiness 40 (0.27 %, 5.3 / Q7),
// B1 at attributes 100 for every cue class (0.00 %, CB direction sigma 0.0115 deg with the house cue). Oracle counts
// (recompute_v12.py with budget.py): 3371, 229, 53, 0; tolerance +-2 counts (CRT), sigma +-1 in the last printed digit.
RB_TEST(Human_BudgetTableRowsOf310)
{
	struct Row
	{
		double Attribute;
		BridgeType Bridge;
		double Stance;
		double MassRatio;
		int Expected;
		double SigmaDeg; // oracle CB direction sigma [deg] (< 0: not checked)
	};
	const Row Rows[] = {
		{25.0, BridgeType::Elevated, 0.5, 15.0, 3371, 0.07154588080184467}, // info: elevated bridge, stance 0.5
		{25.0, BridgeType::Closed, 0.0, 20.0, 229, 0.03963271364328591},    // B1, standard own cue
		{40.0, BridgeType::Closed, 0.0, 40.0, 53, 0.03482593145632339},     // B1, LD shaft at Steadiness 40
		{100.0, BridgeType::Closed, 0.0, 15.0, 0, 0.0115435836143122},      // B1 at 100: house cue
		{100.0, BridgeType::Closed, 0.0, 20.0, 0, 0.0101796895599738},      //   standard
		{100.0, BridgeType::Closed, 0.0, 40.0, 0, 0.010144079920337537},    //   LD
	};
	for (const Row& R : Rows)
	{
		Setup S = MakeS0();
		S.Attributes = UniformAttributes(R.Attribute);
		S.Situation.Bridge = R.Bridge;
		S.Situation.StanceDifficulty = R.Stance;
		S.Intended.Speed = 1.5;
		NoiseHistory History = RebuildNoiseHistory(0xB00Du, 1u, 0u);
		int Misses = 0;
		double SumSq = 0.0;
		for (std::uint32_t i = 0; i < 20000u; ++i)
		{
			S.Key = MakeKey(0xB00Du, i / 20u, i, 1u, i);
			S.History = History;
			S.Intended.TimeDown = 1.5 + 2.5 * U01(HashKeys(99u, i));
			S.Intended.ForwardStart = S.Intended.TimeDown - 0.2;
			const ExecutedStroke X = Execute(S);
			const ContactOffsets C = BudgetContact(X, kR, S.Tip.DomeRadius);
			const double Error = X.Strike.Azimuth - S.Intended.Azimuth + BudgetSquirt(C.A, R.MassRatio);
			SumSq += Error * Error;
			Misses += BudgetPotMissed(Error, kR) ? 1 : 0;
			AdvanceNoiseHistory(History);
		}
		RB_CHECK(std::abs(Misses - R.Expected) <= 2);
		RB_CHECK_REL(std::sqrt(SumSq / 20000.0) * kRadToDeg, R.SigmaDeg, 1e-6);
	}
	// The printed digits of 3.10: 16.9 %, 1.15 %, 0.27 %, 0.0115 deg.
	RB_CHECK_NEAR(3371.0 / 200.0, 16.9, 0.0501);
	RB_CHECK_NEAR(229.0 / 200.0, 1.15, 0.00501);
	RB_CHECK_NEAR(53.0 / 200.0, 0.27, 0.00501); // 0.265 % rounds to 0.27 %
}

// 4.1 (DERIVED): right English at rho 0.45 every shot, V 2 m/s, automatic chalking before every shot (A mode). The struck rim
// zone settles at c 0.84 (rho_max 0.489) at habit 0 and 0.94 (0.505) at habit 1; with the bar cube (cap 0.7, hollow 0.8, RailRat
// n_c 10) at 0.60 (0.447) at habit 1 and around 0.44 (0.418) at habit 0, where the twist count alternates. Oracle: equil.py
// (full digits; like the spec's derivation it wears every hit with the grip severity).
RB_TEST(Human_ChalkEquilibriumUnderAutoChalking)
{
	struct Case
	{
		bool Bar;
		double Habit;
		double Coverage; // zone 4 before shot 200
		double RhoMax;
	};
	const Case Cases[] = {
		{false, 0.0, 0.842884135256206, 0.48908206587101394},
		{false, 1.0, 0.9377602409386372, 0.5045837708733386},
		{true, 1.0, 0.5970826227148724, 0.44669149080946247},
		{true, 0.0, 0.4375701058276972, 0.4174499505651107},
	};
	const TipParams Params;
	const double Severity = HitSeverity(2.0, 0.45, false);
	for (const Case& C : Cases)
	{
		ChalkCube Cube;
		if (C.Bar)
		{
			Cube = BarChalkCube();
			Cube.Hollow = 0.8;
		}
		const double Cap = ChalkCap(Cube, Params);
		TipState Tip;
		Tip.Chalk = Cube.Grade;
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			Tip.Coverage[z] = Cap;
		}
		double Before = 0.0;
		double MinLate = 1.0;
		double MaxLate = 0.0;
		TipContactPoint Contact;
		for (int Shot = 0; Shot < 200; ++Shot)
		{
			double Duration = 0.0;
			PerformChalking(Tip, Cube, ChoreMode::Automatic, C.Habit, 0.0, -1, Duration, Params);
			Contact = LookupTipContact(Tip, 0.45, 0.0, Params);
			Before = Contact.Coverage;
			if (Shot >= 20)
			{
				MinLate = std::fmin(MinLate, Before);
				MaxLate = std::fmax(MaxLate, Before);
			}
			ApplyTipWear(Tip, Contact, Severity, Params);
		}
		RB_CHECK_REL(Before, C.Coverage, 1e-12);
		RB_CHECK_REL(MiscueLimit(Contact.Friction), C.RhoMax, 1e-12);
		if (C.Bar && C.Habit == 0.0)
		{
			RB_CHECK(MinLate > 0.432 && MaxLate < 0.445); // alternating twist count: 0.432 .. 0.444
		}
		else
		{
			RB_CHECK(MaxLate - MinLate < 2e-4); // a steady state
		}
	}
}

// 3.4: every situation multiplier is exactly 1 in the neutral situation and grows monotonically with its input; the channels it
// feeds follow (more pressure, fatigue, sweat, stance difficulty, rush, jab, bridge slip, intoxication beyond I_c never make a
// stroke steadier; one drink only calms).
RB_TEST(Human_SituationMultipliersNeutralAndMonotone)
{
	const Setup S0 = MakeS0();
	const SituationFactors N = ComputeSituationFactors(S0.Intended, S0.Attributes, S0.Situation, S0.Params, 3.0, kR);
	RB_CHECK(N.Bridge == 1.0 && N.Slip == 1.0 && N.Stance == 1.0 && N.Head == 1.0 && N.Stick == 1.0 && N.Fatigue == 1.0 && N.OffHand == 1.0);
	RB_CHECK(N.Jab == 1.0 && N.Settle == 1.0 && N.PressureGain == 1.0 && N.IntoxicationDrift == 1.0 && N.IntoxicationTremor == 1.0);
	RB_CHECK(N.Rush == 1.0 && N.GripBias == 0.0 && N.Pressure == 0.0); // pause 0.4 s >= 0.2 s

	enum class Knob
	{
		Pressure,
		Fatigue,
		Sweat,
		Stance,
		Rush,
		Jab,
		Speed,
		Intoxication,
	};
	const Knob Knobs[] = {Knob::Pressure, Knob::Fatigue, Knob::Sweat, Knob::Stance, Knob::Rush, Knob::Jab, Knob::Speed, Knob::Intoxication};
	for (const Knob K : Knobs)
	{
		SituationFactors Previous{};
		for (int Step = 0; Step <= 20; ++Step)
		{
			const double X = Step / 20.0;
			Setup S = MakeS0();
			S.Situation.Pressure = 0.3; // so that the pressure-dependent terms are visible for every knob
			switch (K)
			{
			case Knob::Pressure: S.Situation.Pressure = X; break;
			case Knob::Fatigue: S.Situation.Fatigue = X; break;
			case Knob::Sweat: S.Situation.Sweat = X; break;
			case Knob::Stance: S.Situation.StanceDifficulty = X; break;
			case Knob::Rush: S.Intended.PauseDuration = 0.2 * (1.0 - X); break;
			case Knob::Jab: S.Intended.ContactAcceleration = -10.0 * X; break;
			case Knob::Speed: S.Intended.Speed = 2.0 + 10.0 * X; break; // past V_b = 6 m/s (closed bridge): slip
			case Knob::Intoxication: S.Situation.Intoxication = 0.25 + 0.75 * X; break; // beyond I_c
			}
			const SituationFactors F = ComputeSituationFactors(S.Intended, S.Attributes, S.Situation, S.Params, 3.0, kR);
			if (Step > 0)
			{
				RB_CHECK(F.DriftSigma >= Previous.DriftSigma && F.TremorSigma >= Previous.TremorSigma && F.ElevationSigma >= Previous.ElevationSigma);
				RB_CHECK(F.TipBSigma >= Previous.TipBSigma && F.PressureGain >= Previous.PressureGain && F.GripBias <= Previous.GripBias);
				RB_CHECK(F.TipASigma >= Previous.TipASigma);
				if (K != Knob::Speed)
				{
					RB_CHECK(F.SpeedSigma >= Previous.SpeedSigma); // the soft-shot factor falls with speed on purpose
				}
				RB_CHECK(F.SlipSpeed <= Previous.SlipSpeed);
			}
			Previous = F;
		}
	}
	// One drink (I <= I_c) only calms: the pressure terms fall, drift / tremor factors stay 1.
	Setup Calm = MakeS0();
	Calm.Situation.Pressure = 0.8;
	const SituationFactors Sober = ComputeSituationFactors(Calm.Intended, Calm.Attributes, Calm.Situation, Calm.Params, 3.0, kR);
	for (int Step = 1; Step <= 10; ++Step)
	{
		Calm.Situation.Intoxication = 0.025 * Step;
		const SituationFactors F = ComputeSituationFactors(Calm.Intended, Calm.Attributes, Calm.Situation, Calm.Params, 3.0, kR);
		RB_CHECK(F.PressureGain < Sober.PressureGain && F.GripBias > Sober.GripBias && F.IntoxicationDrift == 1.0 && F.IntoxicationTremor == 1.0);
	}
	// Bridges by m_br0 (closed 1.0 < open 1.2 < rail 1.4 < mechanical 1.8 < elevated 2.0), and a better Bridge Stability narrows
	// the gap but never below the closed bridge.
	const BridgeType Order[5] = {BridgeType::Closed, BridgeType::Open, BridgeType::Rail, BridgeType::Mechanical, BridgeType::Elevated};
	double Last = 0.0;
	for (const BridgeType B : Order)
	{
		Setup S = MakeS0();
		S.Situation.Bridge = B;
		const SituationFactors F = ComputeSituationFactors(S.Intended, S.Attributes, S.Situation, S.Params, 3.0, kR);
		RB_CHECK(F.DriftSigma > Last || B == BridgeType::Closed);
		Last = F.DriftSigma;
		S.Attributes.BridgeStability = 100.0;
		const SituationFactors Skilled = ComputeSituationFactors(S.Intended, S.Attributes, S.Situation, S.Params, 3.0, kR);
		RB_CHECK(Skilled.Bridge <= F.Bridge && Skilled.Bridge >= 1.0 && Skilled.SlipSpeed > F.SlipSpeed);
	}
}

// Randomized invariants of the executed stroke over wide, finite inputs (every situation field, every cue preset, tip shapes,
// bows, ball sizes, rollout and match keys, stale histories, NoiseScale 0-2, random channel masks, 0-15 other balls):
// Error Ok, a strike that the core's ValidateCueStrike accepts (MOT B.1), rho <= 0.9, V in [0, 12], the elevation in
// [0, pi/2), SampleHand at t_c with the full ramp is the executed pose bit for bit, and a second call is bitwise identical.
RB_TEST(Integ_Human_ExecutedStrikeAlwaysValidForTheCore)
{
	Rng R(0xF022u);
	int Failures = 0;
	for (int Case = 0; Case < 40000; ++Case)
	{
		Setup S = MakeS0();
		S.Intended.Azimuth = R.NextUniform(-10.0, 10.0);
		S.Intended.Elevation = R.NextUniform(-0.2, 1.7);
		S.Intended.AxisOffsetA = R.NextUniform(-1.5, 1.5);
		S.Intended.AxisOffsetB = R.NextUniform(-1.5, 1.5);
		S.Intended.Speed = R.NextUniform(-1.0, 20.0);
		S.Intended.TipVelocityRight = R.NextUniform(-1.0, 1.0);
		S.Intended.TipVelocityUp = R.NextUniform(-1.0, 1.0);
		S.Intended.TimeDown = R.NextUniform(0.0, 30.0);
		S.Intended.ForwardStart = S.Intended.TimeDown - R.NextUniform(0.1, 1.0);
		S.Intended.SettleStart = R.NextUniform(-1.0, 30.0);
		S.Intended.PauseDuration = R.NextUniform(-1.0, 2.0);
		S.Intended.ContactAcceleration = R.NextUniform(-30.0, 30.0);
		S.Intended.HeadMovedBeforeContact = R.NextDouble01() < 0.5;
		S.Attributes = {R.NextUniform(-20.0, 120.0), R.NextUniform(-20.0, 120.0), R.NextUniform(-20.0, 120.0), R.NextUniform(-20.0, 120.0),
			R.NextUniform(-20.0, 120.0), R.NextUniform(-20.0, 120.0)};
		S.Situation.Bridge = static_cast<BridgeType>(R.NextU64() % 5u);
		S.Situation.BridgeLength = R.NextUniform(0.0, 0.5);
		S.Situation.BridgeToGrip = R.NextUniform(0.05, 1.5);
		S.Situation.StanceDifficulty = R.NextUniform(-0.5, 1.5);
		S.Situation.Pressure = R.NextUniform(-0.5, 1.5);
		S.Situation.Fatigue = R.NextUniform(-0.5, 1.5);
		S.Situation.Sweat = R.NextUniform(-0.5, 1.5);
		S.Situation.Glove = R.NextDouble01() < 0.5;
		S.Situation.OffHand = R.NextDouble01() < 0.2;
		S.Situation.ElevationFloor = R.NextUniform(-0.1, 1.7);
		S.Situation.FloorBy = static_cast<FloorSource>(R.NextU64() % 3u);
		S.Situation.FloorBall = static_cast<BallId>(R.NextU64() % 16u);
		S.Situation.ShortCue = R.NextDouble01() < 0.2;
		S.Situation.Intoxication = R.NextUniform(-0.5, 1.5);
		S.Tip.DomeRadius = R.NextUniform(0.005, 0.03);
		S.Tip.Width = R.NextUniform(0.009, 0.014);
		for (double& C : S.Tip.Coverage)
		{
			C = R.NextDouble01();
		}
		S.Tip.Overhang = R.NextUniform(0.0, 1e-3);
		S.Tip.Glaze = R.NextDouble01();
		S.Tip.Restitution = R.NextUniform(0.6, 0.9);
		S.Tip.Loose = R.NextDouble01() < 0.3;
		S.CueBody.BowSag = R.NextUniform(-5e-3, 5e-3);
		S.CueBody.WarpKnown = R.NextDouble01() < 0.5;
		S.Cue = GetCueSpec(static_cast<CuePreset>(R.NextU64() % 4u));
		S.Ball = MakeBallSpec(R.NextUniform(0.025, 0.031), R.NextUniform(0.15, 0.23));
		S.BallPosition = {R.NextUniform(-1.0, 1.0), R.NextUniform(-0.5, 0.5), S.Ball.Radius};
		S.Key = MakeKey(R.NextU64(), static_cast<std::uint32_t>(R.NextU64() % 50u), static_cast<std::uint32_t>(R.NextU64() % 500u),
			static_cast<std::uint32_t>(R.NextU64()), static_cast<std::uint32_t>(R.NextU64() % 40u));
		S.Key.Purpose = R.NextDouble01() < 0.2 ? static_cast<std::uint32_t>(1u + R.NextU64() % 16u) : 0u;
		S.Key.AddressIndex = static_cast<std::uint32_t>(R.NextU64() % 3u);
		S.History = R.NextDouble01() < 0.5 ? RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), S.Key.ShooterShotIndex) : NoiseHistory{};
		S.Params.NoiseScale = R.NextUniform(0.0, 2.0);
		S.Params.ChannelMask = static_cast<std::uint32_t>(R.NextU64()) & 0x3FEu;
		S.Params.StreakGuard = R.NextDouble01() < 0.8;
		BallObstacle Others[15];
		const int OtherCount = static_cast<int>(R.NextU64() % 16u);
		for (int i = 0; i < OtherCount; ++i)
		{
			Others[i].Id = static_cast<BallId>(i + 1);
			Others[i].Radius = S.Ball.Radius;
			Others[i].Position = {S.BallPosition.x + R.NextUniform(-1.5, 1.5), S.BallPosition.y + R.NextUniform(-1.0, 1.0), S.Ball.Radius};
		}
		const ExecutedStroke X = Execute(S, Others, OtherCount);
		const ExecutedStroke Y = Execute(S, Others, OtherCount);
		const HandPose P = Sample(S, S.Intended.TimeDown);
		const bool Ok = X.Error == ErrorCode::Ok && ValidateCueStrike(X.Strike) == ErrorCode::Ok && X.Rho <= S.Params.OffsetClamp &&
			X.Strike.Speed >= 0.0 && X.Strike.Speed <= S.Params.MaxSpeed && X.Strike.Elevation >= 0.0 && X.Strike.Elevation < 0.5 * kPi &&
			SameBits(P.Azimuth, X.Strike.Azimuth) && SameBits(P.Elevation, X.Strike.Elevation) && SameBits(P.AxisOffsetA, X.AxisOffset.x) &&
			SameBits(P.AxisOffsetB, X.AxisOffset.y) && StrokeHash(X) == StrokeHash(Y) && SameStrike(X.Strike, Y.Strike) &&
			X.ShaftContactCandidates.Size() == Y.ShaftContactCandidates.Size() && X.DoubleHitRisk == Y.DoubleHitRisk && X.PushRisk == Y.PushRisk;
		Failures += Ok ? 0 : 1;
	}
	RB_CHECK(Failures == 0);
}

// Randomized invariants of the tip state under any sequence of hits (V2 condition wear on or off, miscues) and chores (every
// mode, aborts, sweeps outside [0, 1], every grade, bar cubes, hollow cubes): coverage and glaze stay in [0, 1], the lookup
// weights are non-negative and sum to 1, mu at any contact lies in [mu_ferrule, mu_fresh], and a ritual never beats the habit-1
// result of the same start (principle 5).
RB_TEST(Human_TipStateInvariantsUnderRandomWearAndChores)
{
	Rng R(0xF023u);
	int Failures = 0;
	for (int Case = 0; Case < 4000; ++Case)
	{
		TipState Tip;
		for (double& C : Tip.Coverage)
		{
			C = R.NextDouble01();
		}
		Tip.Glaze = R.NextDouble01();
		Tip.Hardness = static_cast<TipHardness>(R.NextU64() % 3u);
		Tip.Chalk = static_cast<ChalkGrade>(R.NextU64() % 4u);
		Tip.DomeRadius = R.NextUniform(0.008, 0.02);
		Tip.Width = R.NextUniform(0.011, 0.013);
		Tip.Overhang = R.NextUniform(0.0, 1e-3);
		ChalkCube Cube;
		Cube.Grade = static_cast<ChalkGrade>(R.NextU64() % 4u);
		Cube.Hollow = R.NextDouble01();
		Cube.BarCube = R.NextDouble01() < 0.3;
		TipParams P;
		P.ConditionWear = R.NextDouble01() < 0.5;
		for (int Step = 0; Step < 30; ++Step)
		{
			const double A = R.NextUniform(-0.9, 0.9);
			const double B = R.NextUniform(-0.9, 0.9);
			const TipContactPoint C = LookupTipContact(Tip, A, B, P);
			double Sum = 0.0;
			for (const double W : C.Weights)
			{
				Sum += W;
				Failures += W < 0.0 ? 1 : 0;
			}
			Failures += std::fabs(Sum - 1.0) > 1e-12 ? 1 : 0;
			Failures += (C.Friction < P.FerruleFriction || C.Friction > P.FreshFriction) ? 1 : 0;
			if (R.NextDouble01() < 0.7)
			{
				ApplyTipWear(Tip, C, HitSeverity(R.NextUniform(0.0, 12.0), std::hypot(A, B), R.NextDouble01() < 0.2), P);
			}
			else
			{
				const TipState Before = Tip;
				const ChoreMode Mode = static_cast<ChoreMode>(R.NextU64() % 4u);
				const int Twists = static_cast<int>(R.NextU64() % 12u) - 1;
				double Duration = 0.0;
				PerformChalking(Tip, Cube, Mode, R.NextDouble01(), R.NextUniform(-1.0, 3.0), Twists, Duration, P);
				Failures += !(Duration >= 0.0) ? 1 : 0;
				if (Mode == ChoreMode::Ritual)
				{
					TipState Habit1 = Before;
					PerformChalking(Habit1, Cube, ChoreMode::Automatic, 1.0, 0.0, -1, Duration, P);
					for (int z = 0; z < kTipZoneCount; ++z)
					{
						Failures += Tip.Coverage[z] > Habit1.Coverage[z] ? 1 : 0;
					}
				}
			}
			Failures += CoverageFinite(Tip) ? 0 : 1;
			Failures += (Tip.Glaze >= 0.0 && Tip.Glaze <= 1.0) ? 0 : 1;
			Failures += (Tip.DomeRadius > 0.0 && Tip.DomeRadius <= P.MaxDomeRadius) ? 0 : 1;
		}
	}
	RB_CHECK(Failures == 0);
}
