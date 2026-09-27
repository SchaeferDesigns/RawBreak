// Owner: WP-11 (player model). Third adversarial review of rb::human (not spec IDs): the AI's Settle on pressure shots
// (3.8 t_s = t_c - 2 s collided with the "t_s < 0 = no Settle" sentinel of IntendedStroke), NaN-safe pressure inputs (3.4: a
// 0 / 0 shot-clock fraction or a NaN bet made P NaN and every stroke InvalidArgument), corrupted chalk coverage that chalking
// never repaired (and a float -> int conversion of +inf twists), non-finite shaping targets, and executed strikes outside the
// core's contract (rho clamp >= kCueOffsetValidLimit, speed clamp > kMaxCueSpeed, e_tip outside [0, 1]) for which ExecuteStroke
// reported Ok although MOT B.1 rejects them.

#include "Human/HumanTestUtil.h"

#include "rb/Human/AiProfiles.h"
#include "rb/Human/Chores.h"
#include "rb/Physics/CueStrike.h"

#include <cmath>
#include <limits>

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

// 3.8 / 5.5: the touring pro "uses Settle on pressure shots". t_c = 1.5 + 1.5 U25 s lies in [1.5, 3) s, so t_c - 2 s is
// negative on a third of the shots, and a negative t_s is IntendedStroke's "no Settle": the pro silently did not settle. The
// Settle now starts no earlier than the get-down (t_s = max(0, t_c - 2 s)); from t_c >= 1.5 s > the 1.2 s ramp it is held at
// contact (k_set 0.3) on every pressure shot, and t_s = t_c - 2 s whenever that is not negative (unchanged).
RB_TEST(Human_AiSettlesOnEveryPressureShot)
{
	const AiCharacter Pro{GetAiProfile(AiProfileId::TouringPro), 7u};
	const AiCharacter Regular{GetAiProfile(AiProfileId::BarRegular), 7u};
	PlannedStroke Plan;
	Plan.Azimuth = 0.2;
	Plan.Elevation = 3.0 * kDegToRad;
	Plan.Speed = 2.0;
	StrokeSituation Pressure;
	Pressure.Pressure = 0.6;
	const HumanParams Params;
	int EarlyShots = 0;
	int Failures = 0;
	for (std::uint32_t Shot = 0; Shot < 300u; ++Shot)
	{
		const NoiseKey Key = MakeKey(0x5E771Eu, Shot / 20u, Shot, 2u, Shot);
		const NoiseHistory History = RebuildNoiseHistory(Key.MatchSeed, ShooterKey(Key), Shot);
		const IntendedStroke I = SyntheticHand(Plan, Pro, Pressure, kR, Key, History, Params);
		EarlyShots += I.TimeDown < 2.0 ? 1 : 0;
		const bool Start = I.SettleStart >= 0.0 && (I.TimeDown >= 2.0 ? I.SettleStart == I.TimeDown - 2.0 : I.SettleStart == 0.0);
		Failures += Start ? 0 : 1;
		// Held at contact: the executed stroke sees k_set = 0.3.
		Setup S = MakeS0();
		S.Intended = I;
		S.Attributes = Pro.Profile.Attributes;
		S.Situation = Pressure;
		S.Key = Key;
		S.History = History;
		const ExecutedStroke X = Execute(S);
		Failures += (X.Error == ErrorCode::Ok && X.Channels.Factors.Settle == 0.3) ? 0 : 1;
		// No Settle at P <= 0.4, and never for a profile without the habit.
		Failures += SyntheticHand(Plan, Pro, StrokeSituation{}, kR, Key, History, Params).SettleStart < 0.0 ? 0 : 1;
		Failures += SyntheticHand(Plan, Regular, Pressure, kR, Key, History, Params).SettleStart < 0.0 ? 0 : 1;
	}
	RB_CHECK(EarlyShots > 50); // about a third of the 300 shots are the case that used to lose the Settle
	RB_CHECK(Failures == 0);
}

// 3.4: P is a clamp to [0, 1] of finite inputs, but rb::Clamp passes NaN through: a shot-clock fraction elapsed / limit with a
// zero limit (0 / 0) or a NaN bet made P NaN, and ExecuteStroke rejects a NaN pressure, so the shot could not be played at all.
// A non-finite fraction counts as 0 (+inf as 1), like the other unit inputs (UnitOrZero); finite inputs are unchanged.
RB_TEST(Human_PressureInputsNeverNaN)
{
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	PressureInputs Base;
	Base.Stakes = kStakesMoneyOrLeague;
	Base.GameBall = true;
	Base.Watchers = 4;
	Base.RunLength = 3;
	const double Reference = ComputePressure(Base, PressureMode::On);
	RB_CHECK_NEAR(Reference, 0.35 * 0.6 + 0.25 + 0.1 * 0.4 + 0.05 * 3.0 / 8.0, 1e-15);

	PressureInputs Clock = Base;
	Clock.ShotClockFraction = NaN; // 0 / 0: a clock flag without a limit
	RB_CHECK(ComputePressure(Clock, PressureMode::On) == Reference);
	RB_CHECK(ComputePressure(Clock, PressureMode::Subtle) == 0.5 * Reference);
	Clock.ShotClockFraction = Inf; // elapsed / 0
	RB_CHECK_NEAR(ComputePressure(Clock, PressureMode::On), Reference + 0.10, 1e-15);
	PressureInputs Stakes = Base;
	Stakes.Stakes = NaN;
	PressureInputs NoStakes = Base;
	NoStakes.Stakes = 0.0;
	RB_CHECK(ComputePressure(Stakes, PressureMode::On) == ComputePressure(NoStakes, PressureMode::On));
	PressureWeights Broken;
	Broken.Crowd = NaN;
	const double P = ComputePressure(Base, PressureMode::On, Broken);
	RB_CHECK(P >= 0.0 && P <= 1.0);

	// Money games (Q6): a NaN bet or an inf / inf ratio is a small bet (0.6), never NaN stakes; finite values unchanged.
	RB_CHECK(MoneyGameStakes(NaN, 100.0) == kStakesMoneyOrLeague);
	RB_CHECK(MoneyGameStakes(Inf, Inf) == kStakesMoneyOrLeague);
	RB_CHECK(MoneyGameStakes(10.0, NaN) == kStakesMoneyOrLeague);
	RB_CHECK(MoneyGameStakes(Inf, 100.0) == kStakesFinal);
	RB_CHECK_NEAR(MoneyGameStakes(30.0, 100.0), 0.8, 1e-12);
	static_assert(MoneyGameStakes(50.0, 100.0) == kStakesFinal && MoneyGameStakes(1.0, 0.0) == kStakesFinal);

	// The stroke is playable.
	Setup S = MakeS0();
	Clock.ShotClockFraction = NaN;
	S.Situation.Pressure = ComputePressure(Clock, PressureMode::On);
	RB_CHECK(Execute(S).Error == ErrorCode::Ok);
	PressureInputs Money = Base;
	Money.Stakes = MoneyGameStakes(NaN, 50.0);
	S.Situation.Pressure = ComputePressure(Money, PressureMode::On);
	RB_CHECK(Execute(S).Error == ErrorCode::Ok);
}

// 4.1: a corrupted chalk coverage (NaN, +-inf, outside [0, 1]: an old save, a replay header) was never repaired. NaN in the
// centre zone made AutoChalkTwists return 0 (NaN comparisons), so the automatic chalking before every shot never touched the tip
// and ExecuteStroke rejected every stroke with that cue until a retip; a -inf zone asked for +inf twists (a float -> int
// conversion out of range, undefined behaviour). A corrupted zone now counts as bare (0) in AutoChalkTwists and ApplyChalkTwist:
// the automatic chalking repairs it, and the twist count is bounded by the cap. Valid coverage in [0, 1] is unchanged bit for bit
// (HF-T13).
RB_TEST(Human_CorruptedCoverageIsRepairedByChalking)
{
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	const TipParams Params;
	const ChalkCube Own;
	TipState Bare;
	for (double& C : Bare.Coverage)
	{
		C = 0.0;
	}
	const int BareTwists = AutoChalkTwists(Bare, Own, Params);
	RB_CHECK(BareTwists == 7); // ceil(1 / 0.15)

	const double Corrupt[4] = {NaN, -Inf, Inf, -3.0};
	for (const double Bad : Corrupt)
	{
		for (int Zone = 0; Zone < kTipZoneCount; Zone += 3)
		{
			Setup S = MakeS0();
			S.Tip.Coverage[Zone] = Bad;
			RB_CHECK(AutoChalkTwists(S.Tip, Own, Params) == BareTwists); // a corrupted zone counts as bare: the chalking repairs it
			double Duration = 0.0;
			PerformChalking(S.Tip, Own, ChoreMode::Automatic, 0.5, 0.0, -1, Duration, Params);
			bool Valid = true;
			for (const double C : S.Tip.Coverage)
			{
				Valid = Valid && std::isfinite(C) && C >= 0.0 && C <= 1.0;
			}
			RB_CHECK(Valid);
			RB_CHECK(Execute(S).Error == ErrorCode::Ok); // the cue is usable again after the automatic chalking
		}
	}
	// One twist on a NaN zone chalks it like a bare zone; valid zones are untouched by the guard.
	TipState NaNTip;
	NaNTip.Coverage[2] = NaN;
	NaNTip.Coverage[4] = 0.37;
	TipState BareTip;
	BareTip.Coverage[2] = 0.0;
	BareTip.Coverage[4] = 0.37;
	ApplyChalkTwist(NaNTip, Own, 0.3, Params);
	ApplyChalkTwist(BareTip, Own, 0.3, Params);
	RB_CHECK(NaNTip.Coverage[2] == BareTip.Coverage[2] && NaNTip.Coverage[4] == BareTip.Coverage[4]);
	RB_CHECK(BareTip.Coverage[4] == 0.37 + (1.0 - 0.37) * (0.12 + 0.33 * 0.3));
}

// 4.2 shape chore: a non-finite target (a failed tool measurement) left an infinite dome radius in the saved tip (every later
// stroke InvalidArgument); it is ignored like a non-positive one. The height is still used up (the chore was done).
RB_TEST(Human_ShapeTipIgnoresNonFiniteTargets)
{
	for (const double Bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), -0.01, 0.0})
	{
		TipState Tip;
		ShapeTip(Tip, Bad);
		RB_CHECK(Tip.DomeRadius == 0.0106);
		RB_CHECK_NEAR(Tip.Height, 0.006 - 0.1e-3, 1e-15);
	}
	TipState Dime;
	ShapeTip(Dime, 0.00896);
	RB_CHECK(Dime.DomeRadius == 0.00896);
}

// MOT B.1: ExecuteStroke's contract is "Error Ok => a strike the core accepts" (HumanModel.h), but it only checked that the
// strike was finite. The fields the human layer writes from equipment state or parameters could still break B.1: a loose tip with
// a low (or a corrupted) e_tip gave TipRestitution outside [0, 1], a rho clamp of 0.97 (or NaN, which never clamps) an offset of
// 0.97 >= kCueOffsetValidLimit, a speed clamp of 20 m/s a speed above kMaxCueSpeed, a caller's CueSpec without an end mass an
// invalid cue: ExecuteStroke returned Ok and StrikeCueBall rejected the shot. The executed strike now goes through
// ValidateCueStrike; a rejected strike is InvalidArgument with the zero strike (the cue spec kept), like a non-finite one. A
// misconfigured clamp only matters when it bites: a stroke inside the core's limits stays Ok.
RB_TEST(Human_ExecutedStrikeOutsideTheCoreContractRejected)
{
	Setup S = MakeS0();
	S.Intended.AxisOffsetA = 1.5; // clamped: a = 1.5 R / (R + r_dome) = 1.09 > 0.9
	S.Intended.Speed = 14.0;      // clamped to 12 m/s
	const ExecutedStroke Good = Execute(S);
	RB_REQUIRE(Good.Error == ErrorCode::Ok);
	RB_CHECK(Good.OffsetClamped && Good.Rho == 0.9 && Good.Strike.Speed == 12.0);
	RB_CHECK(ValidateCueStrike(Good.Strike) == ErrorCode::Ok);
	const auto Rejected = [](const Setup& Bad) {
		const ExecutedStroke X = Execute(Bad);
		return X.Error == ErrorCode::InvalidArgument && X.Strike.Speed == 0.0 && X.Strike.OffsetA == 0.0 && X.Strike.Cue.Mass == Bad.Cue.Mass;
	};
	for (const double Clamp : {0.97, kCueOffsetValidLimit, std::numeric_limits<double>::quiet_NaN()})
	{
		Setup Bad = S;
		Bad.Params.OffsetClamp = Clamp;
		RB_CHECK(Rejected(Bad));
		Setup Small = Bad; // the clamp does not bite: a valid strike
		Small.Intended.AxisOffsetA = 0.2;
		RB_CHECK(Execute(Small).Error == ErrorCode::Ok);
	}
	for (const double Max : {20.0, std::numeric_limits<double>::infinity(), -1.0})
	{
		Setup Bad = S;
		Bad.Params.MaxSpeed = Max;
		Bad.Intended.Speed = 18.0; // executed 18.3 m/s > kMaxCueSpeed (or -1 m/s)
		RB_CHECK(Rejected(Bad));
		if (Max > 0.0)
		{
			Setup Slow = Bad; // the clamp does not bite: a valid strike
			Slow.Intended.Speed = 5.0;
			RB_CHECK(Execute(Slow).Error == ErrorCode::Ok);
		}
	}
	{
		Setup Loose = S; // e_tip 0.05 - 0.07 < 0
		Loose.Tip.Restitution = 0.05;
		Loose.Tip.Loose = true;
		RB_CHECK(Rejected(Loose));
		Setup Bouncy = S; // a corrupted e_tip above 1
		Bouncy.Tip.Restitution = 1.2;
		RB_CHECK(Rejected(Bouncy));
		Setup NoEnd = S; // the caller's cue has no end mass (squirt on)
		NoEnd.Cue.EndMass = 0.0;
		RB_CHECK(Rejected(NoEnd));
	}
	// The edges of the contract are valid.
	Setup Edge = S;
	Edge.Params.MaxSpeed = kMaxCueSpeed;
	Edge.Params.OffsetClamp = 0.0;
	const ExecutedStroke E = Execute(Edge);
	RB_CHECK(E.Error == ErrorCode::Ok && E.Strike.Speed > 12.0 && E.Strike.Speed <= kMaxCueSpeed);
	RB_CHECK(E.Rho == 0.0 && E.Strike.OffsetA == 0.0 && ValidateCueStrike(E.Strike) == ErrorCode::Ok);
}
