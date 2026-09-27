// WP-3: per-contact cling from chalk marks (human-factors 4.3): ChalkMarkWeight, ContactClingFactor (HF-S07, A-CLING-1).
#include "rbtest.h"

#include "BallBallTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Math/Quat.h"

using namespace rbbb;

namespace
{
	rb::BallChalkMarks OneMark(const rb::Vec3& BodyDir, double Strength = 1.0, double Radius = 2.5e-3)
	{
		rb::BallChalkMarks Marks;
		rb::ChalkMark Mark;
		Mark.BodyDir = BodyDir;
		Mark.Strength = Strength;
		Mark.Radius = Radius;
		Marks.PushBack(Mark);
		return Marks;
	}

	rb::BallBallParams ClingParams(double Venue, double Chalk = 2.5)
	{
		rb::BallBallParams P = Params(0.95);
		P.ClingFactor = Venue;
		P.ChalkClingFactor = Chalk;
		return P;
	}
}

RB_TEST(HF_S07_ClingWeightOfOneMark)
{
	// One mark (strength 1, radius 2.5 mm) with the contact 0 / 0.05 / 0.1 rad away: k_cling 2.5 / 2.082045 / 1.406170 (k_venue 1).
	const rb::BallChalkMarks Marks = OneMark({1.0, 0.0, 0.0});
	const rb::BallBallParams P = ClingParams(1.0);
	const double Offsets[3] = {0.0, 0.05, 0.1};
	const double Expected[3] = {2.5, 2.082045, 1.406170};
	for (int k = 0; k < 3; ++k)
	{
		const rb::Vec3 Contact{rb::Cos(Offsets[k]), rb::Sin(Offsets[k]), 0.0};
		const double Chi = rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, Contact);
		RB_CHECK_NEAR(rb::ContactClingFactor(Chi, 0.0, P), Expected[k], 1e-6);
		RB_CHECK_NEAR(rb::ContactClingFactor(0.0, Chi, P), Expected[k], 1e-6); // either ball's marks
	}
	// Random orientation: mean weight r^2 / (4 R^2) = 0.0019 +- 0.0002 (DERIVED).
	rb::Rng G(0x5070507ull);
	double Sum = 0.0;
	const int Samples = 400000;
	for (int k = 0; k < Samples; ++k)
	{
		const rb::Vec3 Axis = rb::Normalized(rb::Vec3{G.NextNormal(), G.NextNormal(), G.NextNormal()});
		const rb::Quat Q = rb::FromAxisAngle(Axis, G.NextUniform(0.0, rb::kTwoPi));
		const rb::Vec3 Contact = rb::Normalized(rb::Vec3{G.NextNormal(), G.NextNormal(), G.NextNormal()});
		Sum += rb::ChalkMarkWeight(Marks, Q, kR, Contact);
	}
	const double Mean = Sum / Samples;
	RB_CHECK_NEAR(Mean, 0.0019, 0.0002);
	RB_CHECK_NEAR(Mean, 2.5e-3 * 2.5e-3 / (4.0 * kR * kR), 0.0002);
}

RB_TEST(ARCH_CLING1_ContactClingFactor)
{
	// No marks -> k_venue exactly (bitwise), for any venue.
	const rb::BallChalkMarks None;
	for (double Venue : {1.0, 1.3, 1.5, 2.5, 3.0})
	{
		const rb::BallBallParams P = ClingParams(Venue);
		const double Chi = rb::ChalkMarkWeight(None, rb::Quat::Identity(), kR, {1.0, 0.0, 0.0});
		RB_CHECK(Chi == 0.0);
		RB_CHECK(rb::ContactClingFactor(Chi, Chi, P) == Venue);
	}
	// k_venue > k_chalk -> k_venue whatever the marks.
	RB_CHECK(rb::ContactClingFactor(0.7, 0.8, ClingParams(3.0, 2.5)) == 3.0);
	// Interpolation and saturation at chi_1 + chi_2 >= 1.
	const rb::BallBallParams Bar = ClingParams(1.3);
	RB_CHECK_NEAR(rb::ContactClingFactor(0.25, 0.25, Bar), 1.3 + 1.2 * 0.5, 1e-15);
	RB_CHECK(rb::ContactClingFactor(0.6, 0.4, Bar) == 2.5);
	RB_CHECK(rb::ContactClingFactor(1.7, 2.0, Bar) == 2.5);
	RB_CHECK(rb::ContactClingFactor(0.0, 1.0, ClingParams(1.0)) == 2.5);
	// A mark follows the ball's orientation: body +x rotated by 90 deg about z points along world +y.
	const rb::BallChalkMarks Marks = OneMark({1.0, 0.0, 0.0});
	const rb::Quat Turn = rb::FromAxisAngle({0.0, 0.0, 1.0}, 0.5 * rb::kPi);
	RB_CHECK_NEAR(rb::ChalkMarkWeight(Marks, Turn, kR, {0.0, 1.0, 0.0}), 1.0, 1e-12);
	RB_CHECK(rb::ChalkMarkWeight(Marks, Turn, kR, {1.0, 0.0, 0.0}) < 1e-100);
	RB_CHECK_NEAR(rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, {1.0, 0.0, 0.0}), 1.0, 1e-15);
	// Several marks add; the contact direction need not be unit; strength scales; zero-radius marks carry no weight.
	rb::BallChalkMarks Two = OneMark({0.0, 0.0, 1.0}, 0.4);
	rb::ChalkMark Second;
	Second.BodyDir = {0.0, 0.0, 1.0};
	Second.Strength = 0.3;
	Second.Radius = 4e-3;
	Two.PushBack(Second);
	rb::ChalkMark Degenerate;
	Degenerate.BodyDir = {0.0, 0.0, 1.0};
	Degenerate.Strength = 1.0;
	Degenerate.Radius = 0.0;
	Two.PushBack(Degenerate);
	RB_CHECK_NEAR(rb::ChalkMarkWeight(Two, rb::Quat::Identity(), kR, {0.0, 0.0, 5.0}), 0.7, 1e-12);
	// The cling multiplies the Alciatore friction used by the impulse (2.1).
	rb::BallBallParams Cling = Params(0.95);
	Cling.ClingFactor = rb::ContactClingFactor(1.0, 0.0, Cling);
	RB_CHECK_NEAR(rb::BallBallFriction(0.3, Cling), 2.5 * rb::BallBallFriction(0.3, Params(0.95)), 1e-15);
}
