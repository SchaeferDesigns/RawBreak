// prior-art-and-validation 9.5: SQ-01 ... SQ-04 and CUE-01 ... CUE-04 (WP-1).
// VAL parameters: R = 0.028575, m = 0.17009713875, g = 9.81.

#include "rbtest.h"

#include "CueStrike/CueTestUtil.h"

#include "rb/Equipment/Cue.h"
#include "rb/Physics/CueStrike.h"

#include <cmath>

using namespace mottest;
using namespace cuetest;
using rb::StrikeResult;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	// Level strike with contact offsets (a, b), tip restitution e, ball mass / cue mass = MassRatio (VAL 6.7).
	StrikeResult ValStrike(double A, double B, double TipE, double MassRatio, double MuTip = 0.6, bool Squirt = false)
	{
		rb::CueStrikeInput In = Input(1.0, 0.0, A, B, kValM / MassRatio, TipE);
		In.Cue.TipFriction = MuTip;
		In.Cue.TipFrictionKinetic = MuTip;
		In.Cue.EndMass = kValM / 20.0;
		In.SquirtEnabled = Squirt;
		return rb::StrikeCueBall(In, Resting(), ValSpec(), rb::kClothDefault, rb::SlateParams{}, rb::PinchParams{}, kValG, kNumerics);
	}
}

RB_TEST(VAL_SQ01_MeasuredCues)
{
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.453333, 20.151)), 2.500, 1e-3); // Players (regular shaft)
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.453333, 29.158)), 1.800, 1e-3); // Predator Z (low squirt)
	RB_CHECK_NEAR(Degrees(rb::SquirtAngle(0.266667, 12.008)), 2.400, 1e-3); // Stinger (break / jump)
}

RB_TEST(VAL_SQ02_ModelGrid)
{
	const double Ratios[4] = {12.0, 20.0, 30.0, 50.0};
	const double Offsets[3] = {0.1, 0.25, 0.5};
	const double Table[4][3] = {{0.921, 2.259, 4.162}, {0.607, 1.485, 2.709}, {0.426, 1.040, 1.886}, {0.267, 0.650, 1.173}};
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			RB_CHECK_NEAR(Degrees(rb::SquirtAngle(Offsets[j], Ratios[i])), Table[i][j], 1e-3);
		}
	}
}

RB_TEST(VAL_SQ03_DefaultPlayingCueInMeasuredRange)
{
	const rb::CueSpec Cue = rb::kCuePlaying19oz;
	const double Squirt = Degrees(rb::SquirtAngle(0.5, kValM / Cue.EndMass));
	RB_CHECK(Squirt >= 1.5 && Squirt <= 3.0);
	// The strike applies it: the ball leaves at phi + alpha_sq (ValStrike uses m / m_e = 20).
	const StrikeResult R = ValStrike(0.5, 0.0, Cue.TipRestitution, kValM / Cue.Mass, 0.6, true);
	RB_CHECK_NEAR(Degrees(std::atan2(R.State.Velocity.y, R.State.Velocity.x)), Degrees(rb::SquirtAngle(0.5, 20.0)), 1e-9);
}

RB_TEST(VAL_SQ04_SquirtSign)
{
	// Right English (contact point right of center, a > 0) sends the cue ball LEFT of the cue line (+y for a +x stroke).
	const StrikeResult Right = ValStrike(0.3, 0.0, 0.73, 0.3, 0.6, true);
	RB_CHECK(Right.State.Velocity.y > 0.0);
	RB_CHECK(Right.State.Omega.z > 0.0);
	const StrikeResult Left = ValStrike(-0.3, 0.0, 0.73, 0.3, 0.6, true);
	RB_CHECK(Left.State.Velocity.y < 0.0);
	RB_CHECK_NEAR(Left.State.Velocity.y, -Right.State.Velocity.y, 1e-12);
	RB_CHECK(rb::SquirtAngle(0.0, 20.0) == 0.0);
	for (double A : {0.05, 0.2, 0.37, 0.5})
	{
		RB_CHECK_NEAR(rb::SquirtAngle(-A, 20.0), -rb::SquirtAngle(A, 20.0), 1e-12);
	}
	// Pure follow / draw: no squirt.
	const StrikeResult Follow = ValStrike(0.0, 0.4, 0.73, 0.3, 0.6, true);
	RB_CHECK(Follow.State.Velocity.y == 0.0 && Follow.SquirtAngle == 0.0);
}

RB_TEST(VAL_CUE01_ElasticCenterHit)
{
	const StrikeResult R = ValStrike(0.0, 0.0, 1.0, 1.0 / 3.0);
	RB_CHECK_NEAR(R.State.Velocity.x, 1.5, 1e-9 * 1.5);
	RB_CHECK(R.State.Omega == Vec3::Zero());
}

RB_TEST(VAL_CUE02_ElasticOffsetHit)
{
	// Spin-rate factor R w / v_b = (5/2)(x/R) = 1.25 at x = R/2 (side or top), as listed.
	// Ball speed: VAL 6.7 prints v_b = 2 v_s / (1 + m_r'(1 + (5/2)(x/R)^2)) = 1.297297 v_s, which puts m_r' on the offset term
	// and creates energy (1.851 vs 1.5 J for m = 1, M = 3, v_s = 1). The verified motion spec B.5 (L&G, pooltool, Kim 2021
	// Eq. 83, and VAL's own CUE-04 maximum) gives v_b = 2 v_s / (1 + m_r' + (5/2)(x/R)^2) = 48/47 v_s = 1.0212766 v_s,
	// which conserves the energy of an elastic strike exactly. The core follows MOT (architecture 1: verified physics wins).
	for (int Side = 0; Side < 2; ++Side)
	{
		const StrikeResult R = Side == 0 ? ValStrike(0.5, 0.0, 1.0, 1.0 / 3.0) : ValStrike(0.0, 0.5, 1.0, 1.0 / 3.0);
		RB_REQUIRE(!R.Miscue);
		const double Vb = rb::Length(R.State.Velocity);
		RB_CHECK_NEAR(Vb, 48.0 / 47.0, 1e-9);
		RB_CHECK_NEAR(Vb, 2.0 / (1.0 + 1.0 / 3.0 + 2.5 * 0.25), 1e-9);
		const double Spin = Side == 0 ? std::fabs(R.State.Omega.z) : std::fabs(R.State.Omega.y);
		RB_CHECK_NEAR(Spin * kR / Vb, 1.25, 1e-9 * 1.25);
		// Elastic: kinetic energy of ball + cue is conserved.
		const double M = 3.0 * kValM;
		const double Before = 0.5 * M * 1.0;
		const double After = 0.5 * kValM * Vb * Vb + 0.5 * ValSpec().Inertia * rb::LengthSquared(R.State.Omega) +
			0.5 * M * R.CueSpeedAfter * R.CueSpeedAfter;
		RB_CHECK_NEAR(After, Before, 1e-12 * Before);
	}
}

RB_TEST(VAL_CUE03_CenterHitLeatherTip)
{
	const StrikeResult R = ValStrike(0.0, 0.0, 0.75, 1.0 / 3.0);
	RB_CHECK_NEAR(R.State.Velocity.x, 1.3125, 1e-9 * 1.3125);
}

RB_TEST(VAL_CUE04_MaximumSpinOffset)
{
	// |w|(x) of an elastic level strike, miscue limit disabled (mu_tip huge): maximum at sqrt(0.4 (1 + 1/3)).
	auto Spin = [](double X) { return std::fabs(ValStrike(0.0, X, 1.0, 1.0 / 3.0, 1e6).State.Omega.y); };
	double Lo = 0.3;
	double Hi = 0.94;
	const double Phi = 0.5 * (std::sqrt(5.0) - 1.0);
	double X1 = Hi - Phi * (Hi - Lo);
	double X2 = Lo + Phi * (Hi - Lo);
	double F1 = Spin(X1);
	double F2 = Spin(X2);
	for (int i = 0; i < 80; ++i)
	{
		if (F1 < F2)
		{
			Lo = X1;
			X1 = X2;
			F1 = F2;
			X2 = Lo + Phi * (Hi - Lo);
			F2 = Spin(X2);
		}
		else
		{
			Hi = X2;
			X2 = X1;
			F2 = F1;
			X1 = Hi - Phi * (Hi - Lo);
			F1 = Spin(X1);
		}
	}
	const double XMax = 0.5 * (Lo + Hi);
	RB_CHECK_NEAR(XMax, std::sqrt(0.4 * (1.0 + 1.0 / 3.0)), 1e-6);
	RB_CHECK_NEAR(XMax, 0.730297, 1e-6);
	RB_CHECK(!ValStrike(0.0, XMax, 1.0, 1.0 / 3.0, 1e6).Miscue);
	RB_CHECK(ValStrike(0.0, XMax, 1.0, 1.0 / 3.0).Miscue); // beyond the practical miscue limit with chalk
}
