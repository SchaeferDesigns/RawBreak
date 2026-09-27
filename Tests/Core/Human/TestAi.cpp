// Owner: WP-11 (player model). human-factors 3.8, 5.5 (AI synthetic hand and profiles): HF-B07 (core half).

#include "Human/HumanTestUtil.h"

#include "rb/Human/AiProfiles.h"

#include <cmath>
#include <type_traits>

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

// HF-B07 (core half): the AI's synthetic hand returns an IntendedStroke, never a CueStrikeInput; the only producer of a
// CueStrikeInput in rb::human is ExecuteStroke. (The static check over the AI sources runs where the AI lives, O-16.)
RB_TEST(HF_B07_SyntheticHandReturnsAnIntendedStroke)
{
	static_assert(std::is_same_v<decltype(SyntheticHand(PlannedStroke{}, AiCharacter{}, StrokeSituation{}, 0.0, NoiseKey{}, NoiseHistory{}, HumanParams{})),
		IntendedStroke>);
	static_assert(std::is_same_v<decltype(ExecuteStroke(IntendedStroke{}, ShooterAttributes{}, StrokeSituation{}, TipState{}, CueBodyState{}, CueSpec{},
		BallSpec{}, Vec3{}, nullptr, 0, NoiseKey{}, NoiseHistory{}, HumanParams{})), ExecutedStroke>);
	static_assert(!std::is_convertible_v<IntendedStroke, CueStrikeInput>);

	// The AI goes through the same ExecuteStroke with its own attributes (principle 4).
	const AiCharacter Pro{GetAiProfile(AiProfileId::TouringPro), 99u};
	PlannedStroke Plan;
	Plan.Azimuth = 0.4;
	Plan.Elevation = 4.0 * kDegToRad;
	Plan.AxisOffsetB = -0.2;
	Plan.Speed = 2.5;
	Setup S = MakeS0();
	S.Key = MakeKey(0xA1u, 2u, 17u, 2u, 8u);
	S.History = RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), 8u);
	S.Intended = SyntheticHand(Plan, Pro, S.Situation, kR, S.Key, S.History, S.Params);
	S.Attributes = Pro.Profile.Attributes;
	const ExecutedStroke X = Execute(S);
	RB_REQUIRE(X.Error == ErrorCode::Ok);
	RB_CHECK(std::fabs(X.Strike.Azimuth - 0.4) < 0.01 && std::fabs(X.Strike.Speed - 2.5) < 0.5);
}

RB_TEST(Human_AiProfilesTable)
{
	const double Ratings[kAiProfileCount] = {250.0, 400.0, 500.0, 600.0, 680.0, 750.0};
	const double Attr[kAiProfileCount] = {15.0, 35.0, 50.0, 65.0, 75.0, 90.0};
	const double Nerve[kAiProfileCount] = {10.0, 40.0, 50.0, 80.0, 75.0, 90.0};
	const double AimDeg[kAiProfileCount] = {0.20, 0.10, 0.05, 0.035, 0.025, 0.015};
	const double SteerMm[kAiProfileCount] = {3.0, 1.5, 0.8, 0.5, 0.35, 0.2};
	const double SpeedPct[kAiProfileCount] = {20.0, 12.0, 7.0, 5.0, 4.0, 3.0};
	const int Samples[kAiProfileCount] = {0, 0, 0, 0, 8, 16};
	for (int i = 0; i < kAiProfileCount; ++i)
	{
		const AiProfile P = GetAiProfile(static_cast<AiProfileId>(i));
		RB_CHECK(P.Id == static_cast<AiProfileId>(i) && P.Rating == Ratings[i]);
		RB_CHECK(P.Attributes.Steadiness == Attr[i] && P.Attributes.SpinTouch == Attr[i] && P.Attributes.Nerve == Nerve[i]);
		RB_CHECK_NEAR(P.Hand.AimSigma * kRadToDeg, AimDeg[i], 1e-12);
		RB_CHECK_NEAR(P.Hand.SteerSigma, SteerMm[i] * 1e-3, 1e-15);
		RB_CHECK_NEAR(P.Hand.SpeedSigma, SpeedPct[i] * 0.01, 1e-15);
		RB_CHECK(P.Knowledge.SelfNoiseSamples == Samples[i]);
		RB_CHECK(P.Knowledge.AssumesPerfectExecution == (Samples[i] == 0));
	}
	const AiProfile Tourist = GetAiProfile(AiProfileId::Tourist);
	RB_CHECK(!Tourist.Knowledge.KnowsTableSlope && !Tourist.Knowledge.ModelsThrowAndSquirt && Tourist.Hand.JabProbability == 0.4);
	RB_CHECK(GetAiProfile(AiProfileId::BarRegular).Knowledge.KnowsTableSlope); // knows this table's slope
	RB_CHECK(GetAiProfile(AiProfileId::LeaguePlayer).Knowledge.ModelsThrowAndSquirt && GetAiProfile(AiProfileId::LeaguePlayer).Habits.WarpCheck == 1.0);
	const AiProfile Hustler = GetAiProfile(AiProfileId::LocalHustler);
	RB_CHECK(Hustler.Knowledge.SandbagsUntilMoney && Hustler.MoneyGameRackHabit == 0.2 && Hustler.Habits.ChalkSweep == 0.95);
	RB_CHECK(GetAiProfile(AiProfileId::TouringPro).Hand.UsesSettle && GetAiProfile(AiProfileId::TouringPro).Hand.AimBiasMax == 0.0);
	// The character's aim bias is constant per character and bounded by the profile value.
	const AiCharacter A{GetAiProfile(AiProfileId::BarRegular), 5u};
	const AiCharacter B{GetAiProfile(AiProfileId::BarRegular), 6u};
	RB_CHECK(CharacterAimBias(A) == CharacterAimBias(A) && CharacterAimBias(A) != CharacterAimBias(B));
	RB_CHECK(std::fabs(CharacterAimBias(A)) <= A.Profile.Hand.AimBiasMax);
	RB_CHECK(CharacterAimBias(AiCharacter{GetAiProfile(AiProfileId::TouringPro), 5u}) == 0.0);
}

RB_TEST(Human_SyntheticHand)
{
	const AiCharacter Tourist{GetAiProfile(AiProfileId::Tourist), 11u};
	PlannedStroke Plan;
	Plan.Azimuth = 1.0;
	Plan.Elevation = 0.1;
	Plan.AxisOffsetA = 0.1;
	Plan.AxisOffsetB = 0.2;
	Plan.Speed = 1.5;
	StrokeSituation Situation;
	const NoiseKey Key = MakeKey(0x5EEDu, 1u, 9u, 4u, 3u);
	const NoiseHistory History = RebuildNoiseHistory(Key.MatchSeed, ShooterKey(Key), 3u);
	const HumanParams Params;
	const IntendedStroke I = SyntheticHand(Plan, Tourist, Situation, kR, Key, History, Params);
	const SyntheticHandParams& H = Tourist.Profile.Hand;
	const double Eps20 = DrawPerShot(Key, NoiseChannel::HandAim, History, true).Eps;
	const double Eps21 = DrawPerShot(Key, NoiseChannel::HandSteer, History, true).Eps;
	const double Eps22 = DrawPerShot(Key, NoiseChannel::HandSpeed, History, true).Eps;
	const double Steer = H.SteerSigma * Eps21;
	RB_CHECK_NEAR(I.Azimuth, 1.0 + CharacterAimBias(Tourist) + H.AimSigma * Eps20 + Steer / 0.8, 1e-15);
	RB_CHECK_NEAR(I.AxisOffsetA, 0.1 - Steer * (0.2 + kR) / (0.8 * kR), 1e-15);
	RB_CHECK(I.AxisOffsetB == 0.2 && I.Elevation == 0.1);
	RB_CHECK_NEAR(I.TipVelocityRight, -(Steer * 1.5 / 0.15) * 0.2 / 0.8, 1e-15);
	RB_CHECK_NEAR(I.Speed, 1.5 * (1.0 + 0.2 * Eps22), 1e-15);
	RB_CHECK_NEAR(I.PauseDuration, 0.1 * (0.7 + 0.6 * PlainUniform(Key, NoiseChannel::HandPause)), 1e-15);
	RB_CHECK(I.ContactAcceleration == (PlainUniform(Key, NoiseChannel::HandJab) < 0.4 ? -5.0 : 0.0));
	RB_CHECK_NEAR(I.TimeDown, 1.5 + 1.5 * PlainUniform(Key, NoiseChannel::HandTimeDown), 1e-15);
	RB_CHECK(I.TimeDown >= 1.5 && I.TimeDown < 3.0);
	RB_CHECK(I.HeadMovedBeforeContact == (PlainUniform(Key, NoiseChannel::HandHead) < 0.4));
	RB_CHECK(I.SettleStart < 0.0);
	// t_fwd = t_c - max(0.1 s, 2 L_stroke / V_p): 0.2 s at 1.5 m/s, so the per-shot ramp is in the final stroke (3.8).
	RB_CHECK_NEAR(I.TimeDown - I.ForwardStart, 0.2, 1e-15);
	Plan.Speed = 6.0;
	RB_CHECK_NEAR(SyntheticHand(Plan, Tourist, Situation, kR, Key, History, Params).TimeDown -
			SyntheticHand(Plan, Tourist, Situation, kR, Key, History, Params).ForwardStart, 0.1, 1e-15);
	// The pro settles on pressure shots (t_s = t_c - 2 s when P > 0.4).
	const AiCharacter Pro{GetAiProfile(AiProfileId::TouringPro), 1u};
	Situation.Pressure = 0.6;
	const IntendedStroke Settled = SyntheticHand(Plan, Pro, Situation, kR, Key, History, Params);
	RB_CHECK_NEAR(Settled.SettleStart, Settled.TimeDown - 2.0, 1e-15);
	// The draws are streak-guarded like 3.2 and pure functions of the key (a stale history changes nothing); rollout keys draw
	// plain, history-free samples.
	const IntendedStroke Again = SyntheticHand(Plan, Tourist, StrokeSituation{}, kR, Key, NoiseHistory{}, Params);
	const IntendedStroke First = SyntheticHand(Plan, Tourist, StrokeSituation{}, kR, Key, History, Params);
	RB_CHECK(SameBits(Again.Azimuth, First.Azimuth) && SameBits(Again.Speed, First.Speed));
	const NoiseKey Rollout = RolloutKey(Key, 0u);
	const IntendedStroke R1 = SyntheticHand(Plan, Tourist, StrokeSituation{}, kR, Rollout, History, Params);
	RB_CHECK(R1.Azimuth != First.Azimuth);
	RB_CHECK_NEAR(R1.Speed, 6.0 * (1.0 + 0.2 * TruncNormal(DrawCandidate(Key.MatchSeed, ShooterKey(Rollout), NoiseChannel::HandSpeed, 3u, 0u))), 1e-14);
}
