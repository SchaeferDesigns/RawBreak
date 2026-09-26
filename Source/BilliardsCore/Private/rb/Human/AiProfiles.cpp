#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 3.8, 5.5.
#include "rb/Human/AiProfiles.h"

#include "rb/Math/Scalar.h"

namespace rb::human
{
	namespace
	{
		constexpr double kStrokeLength = 0.15;      // L_stroke [m] of the synthetic final stroke (3.8)
		constexpr double kJabAcceleration = -5.0;   // a_c [m/s^2] of a jab
		constexpr double kSettleLead = 2.0;         // t_s = t_c - 2 s
		constexpr double kSettlePressure = 0.4;     // uses Settle when P > 0.4
		constexpr double kMinFinalStroke = 0.1;     // [s]

		AiProfile MakeProfile(AiProfileId Id, double Rating, double Attribute, double Nerve, double AimDeg, double BiasDeg, double SteerMm, double SpeedPct,
			double Pause, double Jab, double Head, double ChalkHabit, double RackHabit, bool WarpCheck)
		{
			AiProfile Profile;
			Profile.Id = Id;
			Profile.Rating = Rating;
			Profile.Attributes = UniformAttributes(Attribute);
			Profile.Attributes.Nerve = Nerve;
			Profile.Hand.AimSigma = AimDeg * kDegToRad;
			Profile.Hand.AimBiasMax = BiasDeg * kDegToRad;
			Profile.Hand.SteerSigma = SteerMm * 1.0e-3;
			Profile.Hand.SpeedSigma = SpeedPct * 0.01;
			Profile.Hand.PauseMean = Pause;
			Profile.Hand.JabProbability = Jab;
			Profile.Hand.HeadMoveProbability = Head;
			Profile.Habits.ChalkSweep = ChalkHabit;
			Profile.Habits.Rack = RackHabit;
			Profile.Habits.WarpCheck = WarpCheck ? 1.0 : 0.0;
			return Profile;
		}
	}

	AiProfile GetAiProfile(AiProfileId Id)
	{
		// 5.5 (TUNING, fitted with rbsim round-robins, HF-B09). Values the table does not state are marked (DECISION).
		AiProfile Profile;
		switch (Id)
		{
		case AiProfileId::Tourist:
			Profile = MakeProfile(Id, 250.0, 15.0, 10.0, 0.20, 0.25, 3.0, 20.0, 0.1, 0.4, 0.4, 0.2, 0.2, false);
			Profile.Knowledge.PlanDepth = 0; // pots only
			break;
		case AiProfileId::BarRegular:
			Profile = MakeProfile(Id, 400.0, 35.0, 40.0, 0.10, 0.12, 1.5, 12.0, 0.25, 0.2, 0.2, 0.5, 0.5, false);
			Profile.Knowledge.PlanDepth = 1;         // next-ball position
			Profile.Knowledge.KnowsTableSlope = true; // knows this table's slope
			break;
		case AiProfileId::LeaguePlayer:
			Profile = MakeProfile(Id, 500.0, 50.0, 50.0, 0.05, 0.05, 0.8, 7.0, 0.4, 0.05, 0.1, 0.8, 0.7, true);
			Profile.Knowledge.PlanDepth = 2;
			Profile.Knowledge.Safeties = true; // some safeties
			Profile.Knowledge.ModelsThrowAndSquirt = true;
			break;
		case AiProfileId::LocalHustler:
			// H_rack outside money games is not in the table (DECISION: 0.8, a careful local racker); 0.2 in money games (4.6).
			Profile = MakeProfile(Id, 600.0, 65.0, 80.0, 0.035, 0.02, 0.5, 5.0, 0.5, 0.0, 0.05, 0.95, 0.8, true);
			Profile.MoneyGameRackHabit = 0.2;
			Profile.Knowledge.PlanDepth = 3;
			Profile.Knowledge.Safeties = true;
			Profile.Knowledge.ModelsThrowAndSquirt = true;
			Profile.Knowledge.KnowsTableSlope = true; // plays his home table (DECISION)
			Profile.Knowledge.SandbagsUntilMoney = true;
			break;
		case AiProfileId::RoadPlayer:
			Profile = MakeProfile(Id, 680.0, 75.0, 75.0, 0.025, 0.01, 0.35, 4.0, 0.5, 0.0, 0.0, 1.0, 0.9, true);
			Profile.Knowledge.PlanDepth = 3; // full safety and kicking game (DECISION: depth 3)
			Profile.Knowledge.Safeties = true;
			Profile.Knowledge.ModelsThrowAndSquirt = true;
			Profile.Knowledge.KnowsTableSlope = true; // reads the table (DECISION)
			Profile.Knowledge.SelfNoiseSamples = 8;
			Profile.Knowledge.AssumesPerfectExecution = false;
			break;
		case AiProfileId::TouringPro:
			Profile = MakeProfile(Id, 750.0, 90.0, 90.0, 0.015, 0.0, 0.2, 3.0, 0.6, 0.0, 0.0, 1.0, 1.0, true);
			Profile.Hand.UsesSettle = true; // uses Settle on pressure shots
			Profile.Knowledge.PlanDepth = 4; // full model (DECISION: depth 4)
			Profile.Knowledge.Safeties = true;
			Profile.Knowledge.ModelsThrowAndSquirt = true;
			Profile.Knowledge.KnowsTableSlope = true;
			Profile.Knowledge.SelfNoiseSamples = 16;
			Profile.Knowledge.AssumesPerfectExecution = false;
			break;
		}
		return Profile;
	}

	double CharacterAimBias(const AiCharacter& Character)
	{
		const double U = U01(HashKeys(Character.CharacterSeed, std::uint64_t{20}, std::uint64_t{1}));
		return Character.Profile.Hand.AimBiasMax * (2.0 * U - 1.0);
	}

	IntendedStroke SyntheticHand(const PlannedStroke& Plan, const AiCharacter& Character, const StrokeSituation& Situation, double BallRadius,
		const NoiseKey& Key, const NoiseHistory& History, const HumanParams& Params)
	{
		const SyntheticHandParams& Hand = Character.Profile.Hand;
		const auto Enabled = [&Params](NoiseChannel Channel) { return (Params.ChannelMask & ChannelBit(Channel)) != 0u ? 0.0 : 1.0; };
		const double Eps20 = Enabled(NoiseChannel::HandAim) * DrawPerShot(Key, NoiseChannel::HandAim, History, Params.StreakGuard).Eps;
		const double Eps21 = Enabled(NoiseChannel::HandSteer) * DrawPerShot(Key, NoiseChannel::HandSteer, History, Params.StreakGuard).Eps;
		const double Eps22 = Enabled(NoiseChannel::HandSpeed) * DrawPerShot(Key, NoiseChannel::HandSpeed, History, Params.StreakGuard).Eps;
		const double U23 = PlainUniform(Key, NoiseChannel::HandPause);
		const double U24 = PlainUniform(Key, NoiseChannel::HandJab);
		const double U25 = PlainUniform(Key, NoiseChannel::HandTimeDown);
		const double U26 = PlainUniform(Key, NoiseChannel::HandHead);

		const double Lb = Situation.BridgeLength;
		const double Lbg = Situation.BridgeToGrip > 0.0 ? Situation.BridgeToGrip : 0.80;
		const double R = BallRadius > 0.0 ? BallRadius : kDefaultBallRadius;
		const double Lbc = Lb + R;

		IntendedStroke Stroke;
		// Aim: planned + the character's constant bias + scatter; steering through the pivot (the tip moves the other way).
		const double Steer = Hand.SteerSigma * Eps21;
		Stroke.Azimuth = Plan.Azimuth + CharacterAimBias(Character) + Hand.AimSigma * Eps20;
		Stroke.Azimuth += Steer / Lbg;
		Stroke.AxisOffsetA = Plan.AxisOffsetA - Steer * Lbc / (Lbg * R);
		Stroke.AxisOffsetB = Plan.AxisOffsetB;
		Stroke.Elevation = Plan.Elevation;
		Stroke.TipVelocityRight = -(Steer * Plan.Speed / kStrokeLength) * Lb / Lbg; // animation only
		Stroke.TipVelocityUp = 0.0;
		Stroke.Speed = Plan.Speed * (1.0 + Hand.SpeedSigma * Eps22);
		Stroke.PauseDuration = Hand.PauseMean * (0.7 + 0.6 * U23);
		Stroke.ContactAcceleration = U24 < Hand.JabProbability ? kJabAcceleration : 0.0;
		Stroke.TimeDown = 1.5 + 1.5 * U25;
		Stroke.SettleStart = (Hand.UsesSettle && Situation.Pressure > kSettlePressure) ? Stroke.TimeDown - kSettleLead : -1.0;
		Stroke.HeadMovedBeforeContact = U26 < Hand.HeadMoveProbability;
		Stroke.ForwardStart = Stroke.TimeDown - (Plan.Speed > 0.0 ? Max(kMinFinalStroke, 2.0 * kStrokeLength / Plan.Speed) : kMinFinalStroke);
		return Stroke;
	}
}
