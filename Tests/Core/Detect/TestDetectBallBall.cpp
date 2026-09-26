// Owner: WP-5 (event detection). Ball-ball detection: physics-collisions 9.2 (D-1 ... D-7, D-11, D-12a) and
// prior-art 9.11 (ROB-07, ROB-08, ROB-13). Segments are built by hand (motion spec closed forms, k = 2/5).

#include "rbtest.h"

#include "DetectTestUtil.h"

#include <cstring>

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
	ContactPrediction Predict(const MotionSegment& A, const MotionSegment& B, double TimeLimit = rb::kInfinity)
	{
		return rb::PredictBallBall(A, kR, B, kR, TimeLimit, Numerics());
	}
}

RB_TEST(COL_D1_RollingHeadOnIntoRestingBall)
{
	const MotionSegment A = Rolling({0.0, 0.0, kR}, {1.0, 0.0, 0.0});
	const MotionSegment B = Stationary({0.5, 0.0, kR});
	const ContactPrediction P = Predict(A, B);
	RB_REQUIRE(P.Found);
	RB_CHECK(P.Flags == 0);
	RB_CHECK_NEAR(P.Time, 0.4529079766, 1e-10);
	RB_CHECK_NEAR(rb::PositionAt(A, P.Time).x, 0.5 - 2.0 * kR, 1e-9); // x_A = 0.44285
}

RB_TEST(COL_D2_RollingCutIntoOffsetBall)
{
	const ContactPrediction P = Predict(Rolling({0.0, 0.0, kR}, {1.0, 0.0, 0.0}), Stationary({0.5, 0.05, kR}));
	RB_REQUIRE(P.Found);
	RB_CHECK_NEAR(P.Time, 0.4837978208, 1e-10);
}

RB_TEST(COL_D3_OffsetBeyondTwoRadiiIsAMiss)
{
	const ContactPrediction P = Predict(Rolling({0.0, 0.0, kR}, {1.0, 0.0, 0.0}), Stationary({0.5, 0.06, kR}));
	RB_CHECK(!P.Found);
	RB_CHECK(P.Flags == 0);
}

RB_TEST(COL_D4_RootsOutsideTheSlidingWindowAreIgnored)
{
	const MotionSegment A = Sliding({0.0, 0.0, kR}, {2.0, 0.0, 0.0}, {});
	const MotionSegment B = Rolling({1.0, 0.02, kR}, {-1.0, 0.0, 0.0});
	RB_CHECK_NEAR(A.TauEnd, 0.2913475, 1e-7); // tau_max = A's sliding end
	RB_CHECK(!Predict(A, B).Found);

	// The extrapolated polynomials do meet, but only after the window (0.35996 and 0.40844 s).
	const rb::Polynomial F = rb::BallBallGapPolynomial(A, kR, B, kR, 0.0);
	double Roots[4];
	RB_REQUIRE(rb::SolveInInterval(F, 0.0, 1.0, Roots) == 2);
	RB_CHECK_NEAR(Roots[0], 0.35996, 1e-5);
	RB_CHECK_NEAR(Roots[1], 0.40844, 1e-5);
}

RB_TEST(COL_D5_AirborneBallHitsRestingBall3D)
{
	const MotionSegment A = Airborne({0.0, 0.0, kR}, {2.0, 0.0, 1.5});
	const MotionSegment B = Stationary({0.1, 0.0, kR});
	const ContactPrediction P = Predict(A, B);
	RB_REQUIRE(P.Found);
	RB_CHECK_NEAR(P.Time, 0.0297042086, 1e-9);
	const Vec3 N = rb::Normalized(B.Pos0 - rb::PositionAt(A, P.Time));
	RB_CHECK_NEAR(N.x, 0.710264, 1e-6);
	RB_CHECK_NEAR(N.y, 0.0, 1e-12);
	RB_CHECK_NEAR(N.z, -0.703935, 1e-6);
}

RB_TEST(COL_D5b_AirborneBallJumpsOverRestingBall)
{
	const ContactPrediction P = Predict(Airborne({0.0, 0.0, kR}, {2.0, 0.0, 1.5}), Stationary({0.4, 0.0, kR}));
	RB_CHECK(!P.Found);
}

RB_TEST(COL_D6_DegenerateSeparatingPairHasNoEvent)
{
	// Just after a head-on stun hit: touching, both sliding along +x with identical acceleration.
	const MotionSegment A = Sliding({0.0, 0.0, kR}, {0.025, 0.0, 0.0}, {});
	const MotionSegment B = Sliding({2.0 * kR, 0.0, kR}, {0.975, 0.0, 0.0}, {});
	const rb::Polynomial F = rb::BallBallGapPolynomial(A, kR, B, kR, 0.0);
	RB_CHECK(F.c[4] == 0.0);
	RB_CHECK(F.c[3] == 0.0);
	RB_CHECK(F.c[0] == 0.0);
	RB_CHECK(F.c[1] > 0.0); // f'(0) > 0: separating
	RB_CHECK(F.Degree == 2); // a quadratic (a2 = |dB|^2), never handed to a quartic routine
	const ContactPrediction P = Predict(A, B);
	RB_CHECK(!P.Found);
	RB_CHECK(P.Flags == 0);
}

RB_TEST(COL_D7_FrozenAndApproachingContactAtZero)
{
	const MotionSegment A = Sliding({0.0, 0.0, kR}, {1.0, 0.0, 0.0}, {});
	const MotionSegment B = Stationary({2.0 * kR, 0.0, kR});
	const rb::Polynomial F = rb::BallBallGapPolynomial(A, kR, B, kR, 0.0);
	RB_CHECK(F.c[0] == 0.0);
	RB_CHECK_NEAR(F.c[1], -0.1143, 1e-12);
	const ContactPrediction P = Predict(A, B);
	RB_REQUIRE(P.Found);
	RB_CHECK(P.Time == 0.0);
	RB_CHECK(P.Flags == rb::ContactFlags::AtStart);
}

RB_TEST(COL_D11_NoRedetectionAfterHeadOnStunImpulse)
{
	// State right after BB-1: CB (0.025, 0, 0), OB (0.975, 0, 0), no spin, exactly touching. Follow the pair through
	// every segment change up to 1 s: no event may appear (no zero-time loop, no ghost re-contact).
	const MotionSegment A0 = Sliding({0.0, 0.0, kR}, {0.025, 0.0, 0.0}, {});
	const MotionSegment B0 = Sliding({2.0 * kR, 0.0, kR}, {0.975, 0.0, 0.0}, {});
	RB_CHECK(!Predict(A0, B0, 1.0).Found);

	const double T1 = A0.TauEnd; // A rolls (5/7 of 0.025)
	const MotionSegment A1 = Rolling(rb::PositionAt(A0, T1), rb::VelocityAt(A0, T1), kMuR, kG, T1);
	RB_CHECK(!Predict(A1, B0, 1.0).Found);

	const double T2 = B0.TauEnd; // B rolls
	const MotionSegment B1 = Rolling(rb::PositionAt(B0, T2), rb::VelocityAt(B0, T2), kMuR, kG, T2);
	RB_CHECK(T2 < A1.T0 + A1.TauEnd);
	RB_CHECK(!Predict(A1, B1, 1.0).Found);

	const double T3 = A1.T0 + A1.TauEnd; // A stops
	const MotionSegment A2 = Stationary(rb::PositionAt(A1, A1.TauEnd), T3);
	RB_CHECK(!Predict(A2, B1, 1.0).Found);
}

RB_TEST(COL_D12a_PressingContactFlags)
{
	// CB frozen to an OB with pure topspin: w = (0, 10, 0), sliding, A = +(1/2) mu_s g x_hat.
	const MotionSegment A = Sliding({0.0, 0.0, kR}, {}, {0.0, 10.0, 0.0});
	const MotionSegment B = Stationary({2.0 * kR, 0.0, kR});
	RB_CHECK_NEAR(A.Accel2.x, 0.5 * kMuS * kG, 1e-15);
	const rb::Polynomial F = rb::BallBallGapPolynomial(A, kR, B, kR, 0.0);
	RB_CHECK(F.c[0] == 0.0);
	RB_CHECK(F.c[1] == 0.0);
	RB_CHECK_NEAR(2.0 * F.c[2], -0.224180, 1e-6); // f''(0)
	const ContactPrediction P = Predict(A, B);
	RB_REQUIRE(P.Found);
	RB_CHECK(P.Time == 0.0);
	RB_CHECK(P.Flags == (rb::ContactFlags::AtStart | rb::ContactFlags::Pressing));

	// Without the spin (both at rest, touching) nothing happens: f' = f'' = 0.
	RB_CHECK(!Predict(Stationary({0.0, 0.0, kR}), B).Found);
}

RB_TEST(VAL_ROB07_DegenerateQuarticEqualAccelerations)
{
	// Same mu_r and direction: dA = 0 exactly, the quartic is a quadratic.
	const MotionSegment A = Rolling({-0.5, 0.0, kR}, {1.0, 0.0, 0.0}, kMuR, kGVal);
	const MotionSegment B = Rolling({0.0, 0.0, kR}, {0.5, 0.0, 0.0}, kMuR, kGVal);
	const rb::Polynomial F = rb::BallBallGapPolynomial(A, kR, B, kR, 0.0);
	RB_CHECK(F.Degree == 2);
	const ContactPrediction P = Predict(A, B);
	RB_REQUIRE(P.Found);
	RB_CHECK_NEAR(P.Time, (0.5 - 2.0 * kR) / 0.5, 1e-12); // 0.885700 s
}

RB_TEST(VAL_ROB08_GrazeDecisionsAreExactAndBitwiseStable)
{
	const MotionSegment Cue = Rolling({0.0, 0.0, kR}, {1.0, 0.0, 0.0});
	const MotionSegment Miss = Stationary({0.5, 2.0 * kR + 1e-7, kR});
	const MotionSegment Hit = Stationary({0.5, 2.0 * kR - 1e-7, kR});
	const ContactPrediction MissRef = Predict(Cue, Miss);
	const ContactPrediction HitRef = Predict(Cue, Hit);
	RB_CHECK(!MissRef.Found);
	RB_REQUIRE(HitRef.Found);
	int Mismatches = 0;
	for (int Run = 0; Run < 1000; ++Run)
	{
		const ContactPrediction M = Predict(Cue, Miss);
		const ContactPrediction H = Predict(Cue, Hit);
		if (M.Found != MissRef.Found || H.Found != HitRef.Found || std::memcmp(&H.Time, &HitRef.Time, sizeof(double)) != 0 || H.Flags != HitRef.Flags)
		{
			++Mismatches;
		}
	}
	RB_CHECK(Mismatches == 0);
}

RB_TEST(VAL_ROB13_NoGhostCollisionBehindAStoppingBall)
{
	// The extrapolated parabola of A would turn back through B; the validity window forbids it.
	const MotionSegment A = Rolling({0.0, 0.0, kR}, {0.5, 0.0, 0.0}, kMuR, kGVal);
	const MotionSegment B = Stationary({-0.2, 0.0, kR});
	RB_CHECK(!Predict(A, B).Found);
	RB_CHECK_NEAR(rb::PositionAt(A, A.TauEnd).x, 1.27421, 1e-5);
	// Sanity: the unbounded polynomial does have roots (the ghost).
	double Roots[4];
	RB_CHECK(rb::SolveInInterval(rb::BallBallGapPolynomial(A, kR, B, kR, 0.0), 0.0, 100.0, Roots) >= 1);
}

// -------------------------------------------------------------------------------------------------
// Further contract checks (Detect.h)
// -------------------------------------------------------------------------------------------------

RB_TEST(Detect_BallBallOverlapFlagIsDiagnosticOnly)
{
	// 2 um overlap (> OverlapGuard 1 um): approaching -> event at the start with Overlap; separating -> no event, flag kept.
	const MotionSegment B = Stationary({2.0 * kR - 2e-6, 0.0, kR});
	const ContactPrediction In = Predict(Sliding({0.0, 0.0, kR}, {1.0, 0.0, 0.0}, {}), B);
	RB_REQUIRE(In.Found);
	RB_CHECK(In.Time == 0.0);
	RB_CHECK(In.Flags == (rb::ContactFlags::AtStart | rb::ContactFlags::Overlap));
	const ContactPrediction Out = Predict(Sliding({0.0, 0.0, kR}, {-1.0, 0.0, 0.0}, {}), B);
	RB_CHECK(!Out.Found);
	RB_CHECK(Out.Flags == rb::ContactFlags::Overlap);
	// Sub-micron rounding overlap: touching, no flag.
	const ContactPrediction Tiny = Predict(Sliding({0.0, 0.0, kR}, {1.0, 0.0, 0.0}, {}), Stationary({2.0 * kR - 1e-12, 0.0, kR}));
	RB_REQUIRE(Tiny.Found);
	RB_CHECK(Tiny.Flags == rb::ContactFlags::AtStart);
}

RB_TEST(Detect_BallBallCommonOriginIsTheLaterSegmentStart)
{
	// A has rolled since t = 0.1, B started sliding at t = 0.3: the window starts at 0.3 and the event is absolute.
	const MotionSegment A = Rolling({-0.3, 0.02, kR}, {1.5, 0.0, 0.0}, kMuR, kG, 0.1);
	const MotionSegment B = Sliding({0.2, -0.1, kR}, {0.0, 0.8, 0.0}, {5.0, 0.0, 0.0}, kMuS, kG, 0.3);
	const ContactPrediction P = Predict(A, B);
	RB_REQUIRE(P.Found);
	const double TauMax = rb::Min(A.T0 + A.TauEnd, B.T0 + B.TauEnd) - 0.3;
	const auto Gap = [&](double Tau)
	{
		const double t = 0.3 + Tau;
		return rb::Length(rb::PositionAt(B, t - B.T0) - rb::PositionAt(A, t - A.T0)) - 2.0 * kR;
	};
	RB_CHECK_NEAR(P.Time, 0.3 + BruteForceFirstContact(Gap, TauMax), 1e-9);
	// A window that ends before the contact finds nothing; one that ends exactly at it still does.
	RB_CHECK(!Predict(A, B, P.Time - 1e-6).Found);
	RB_CHECK(Predict(A, B, P.Time + 1e-12).Found);
	// A window that ends before the later segment start is empty.
	RB_CHECK(!Predict(A, B, 0.29).Found);
}

RB_TEST(Detect_BallBallUnboundedWindowsAndDegenerateInputs)
{
	// Two balls falling in the same pocket (TauEnd = +inf, equal accelerations: a quadratic on an unbounded window).
	const MotionSegment A = PocketFall({1.30, 0.66, 0.0}, {0.5, 0.0, 0.0});
	const MotionSegment B = PocketFall({1.40, 0.66, -0.01}, {-0.2, 0.0, 0.0});
	const ContactPrediction P = Predict(A, B);
	RB_REQUIRE(P.Found);
	RB_CHECK(rb::IsFinite(P.Time));
	RB_CHECK_NEAR(rb::Length(rb::PositionAt(B, P.Time) - rb::PositionAt(A, P.Time)), 2.0 * kR, 1e-12);
	// Falling ball beside a resting one, never touching: nothing, and no NaN on the unbounded window.
	const ContactPrediction Q = Predict(PocketFall({1.30, 0.66, 0.0}, {}), Stationary({1.30, 0.66 + 2.0 * kR + 1e-3, kR}));
	RB_CHECK(!Q.Found && Q.Flags == 0);
	// Identical motion (dA = dB = 0): the zero-variation polynomial, touching -> no event, apart -> no event.
	RB_CHECK(!Predict(PocketFall({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), PocketFall({2.0 * kR, 0.0, 0.0}, {1.0, 0.0, 0.0})).Found);
	RB_CHECK(!Predict(PocketFall({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), PocketFall({0.3, 0.0, 0.0}, {1.0, 0.0, 0.0})).Found);
	// A zero-length window (both segments end now) still applies the start rules.
	MotionSegment Now = Sliding({0.0, 0.0, kR}, {1.0, 0.0, 0.0}, {});
	Now.TauEnd = 0.0;
	const ContactPrediction Z = Predict(Now, Stationary({2.0 * kR, 0.0, kR}));
	RB_CHECK(Z.Found && Z.Time == 0.0 && Z.Flags == rb::ContactFlags::AtStart);
	RB_CHECK(!Predict(Now, Stationary({2.0 * kR + 1e-3, 0.0, kR})).Found);
	// TimeLimit before both segment starts: empty window.
	RB_CHECK(!Predict(Rolling({0.0, 0.0, kR}, {1.0, 0.0, 0.0}, kMuR, kG, 1.0), Stationary({0.5, 0.0, kR}), 0.5).Found);
}

RB_TEST(Detect_BallBallUnequalRadiiAndLaterReContact)
{
	// Masse-like curve: B slides away and its slip turns it back into A (a genuine second downward crossing).
	const double RB = 0.0301625; // oversized cue ball
	const MotionSegment A = Stationary({0.0, 0.0, kR});
	const MotionSegment B = Sliding({kR + RB, 0.0, RB}, {0.3, 0.0, 0.0}, {0.0, -100.0, 0.0}, kMuS, kG, 0.0, RB);
	const ContactPrediction P = rb::PredictBallBall(A, kR, B, RB, rb::kInfinity, Numerics());
	RB_REQUIRE(P.Found);
	RB_CHECK(P.Time > 0.01);
	RB_CHECK(P.Flags == 0);
	const auto Gap = [&](double Tau) { return rb::Length(rb::PositionAt(B, Tau) - A.Pos0) - (kR + RB); };
	// Brute force from after the separation (the first samples are touching and separating).
	const double Skip = 1e-3;
	const double Found = BruteForceFirstContact([&](double Tau) { return Gap(Skip + Tau); }, B.TauEnd - Skip);
	RB_CHECK_NEAR(P.Time, Skip + Found, 1e-9);
}
