// physics-motion-and-cue Part A tests T-A1 ... T-A12 and ue5-realism-plan T24 (WP-1).
// Tolerances (motion spec "Test cases"): "exact" = +-1 unit in the last listed digit; closed-form identities 1e-9 relative.

#include "rbtest.h"

#include "Motion/MotionTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/Motion.h"

#include <cmath>

using namespace mottest;
using rb::BallState;
using rb::MotionSegment;
using rb::MotionState;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	MotionSegment Seg(const BallState& S) { return rb::MakeSegment(S, 0.0, MotSpec(), MotCloth(), 0.0, kG); }

	double RelTol(double Value) { return 1e-9 * std::fabs(Value); }

	// Sliding distance and final state of a straight stun / draw / follow shot along +x (Rw_y = Spin * R).
	struct StraightSlide
	{
		double Tau = 0.0;
		double Distance = 0.0;
		BallState End;
	};

	StraightSlide Slide(double V0, double OmegaY, const rb::BallSpec& Spec = MotSpec(), const rb::ClothParams& Cloth = MotCloth(), double G = kG)
	{
		const BallState S = OnCloth({V0, 0.0, 0.0}, {0.0, OmegaY, 0.0});
		const MotionSegment M = rb::MakeSegment(S, 0.0, Spec, Cloth, 0.0, G);
		StraightSlide Out;
		Out.Tau = M.TauEnd;
		Out.End = rb::SegmentEndState(M, kNumerics);
		Out.Distance = Out.End.Position.x;
		return Out;
	}
}

RB_TEST(MOT_TA1_RollingStop)
{
	const BallState S = OnCloth({1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 0.0});
	RB_REQUIRE(S.State == MotionState::Rolling);
	const MotionSegment M = Seg(S);
	RB_CHECK_NEAR(M.TauEnd, 10.197162, 1e-6);
	RB_CHECK_NEAR(M.TauEnd, 1.0 / (0.010 * kG), RelTol(M.TauEnd));
	const BallState End = rb::SegmentEndState(M, kNumerics);
	RB_CHECK_NEAR(End.Position.x, 5.098581, 1e-6);
	RB_CHECK_NEAR(End.Position.x, 1.0 / (2.0 * 0.010 * kG), RelTol(End.Position.x));
	RB_CHECK(End.State == MotionState::Stationary);
	RB_CHECK(End.Velocity == Vec3::Zero() && End.Omega == Vec3::Zero());
	RB_CHECK(End.Position.z == kR);
}

RB_TEST(MOT_TA2_StunShot)
{
	const StraightSlide R = Slide(2.0, 0.0);
	RB_CHECK_NEAR(R.Tau, 0.2913475, 1e-7);
	RB_CHECK_NEAR(R.Tau, 2.0 * 2.0 / (7.0 * 0.2 * kG), RelTol(R.Tau));
	RB_CHECK_NEAR(R.Distance, 0.4994528, 1e-7);
	RB_CHECK_NEAR(R.Distance, 12.0 * 4.0 / (49.0 * 0.2 * kG), RelTol(R.Distance));
	RB_CHECK_NEAR(R.End.Velocity.x, 1.4285714, 1e-7);
	RB_CHECK_NEAR(R.End.Velocity.x, 5.0 / 7.0 * 2.0, 1e-9 * 2.0);
	RB_CHECK(R.End.Velocity.y == 0.0 && R.End.Velocity.z == 0.0);
	RB_CHECK_NEAR(R.End.Omega.y, 49.99375, 1e-5);
	RB_CHECK_NEAR(R.End.Omega.y, R.End.Velocity.x / kR, RelTol(R.End.Omega.y));
	RB_CHECK(R.End.State == MotionState::Rolling);
}

RB_TEST(MOT_TA3_AlciatoreStunDistances)
{
	const double Speeds[3] = {1.341120, 3.129280, 5.364480}; // 3, 7, 12 mph
	const double Meters[3] = {0.22458, 1.22271, 3.59327};
	const double Feet[3] = {0.737, 4.012, 11.789}; // TP A.18 published
	for (int i = 0; i < 3; ++i)
	{
		const StraightSlide R = Slide(Speeds[i], 0.0);
		RB_CHECK_NEAR(R.Distance, Meters[i], 1e-5);
		RB_CHECK_NEAR(R.Distance / 0.3048, Feet[i], 1e-3);
		RB_CHECK(R.End.State == MotionState::Rolling);
	}
}

RB_TEST(MOT_TA4_DrawHalfTip)
{
	const double OmegaY = -2.5 / kR; // -87.489064 rad/s: R w_y = -1.25 v0
	RB_CHECK_NEAR(OmegaY, -87.489064, 1e-6);
	const BallState S = OnCloth({2.0, 0.0, 0.0}, {0.0, OmegaY, 0.0});
	const Vec3 U = rb::SlipVelocity(S.Velocity, S.Omega, kR);
	RB_CHECK_NEAR(U.x, 4.5, 1e-7);
	RB_CHECK(U.y == 0.0);
	const MotionSegment M = Seg(S);
	RB_CHECK_NEAR(M.TauEnd, 0.6555319, 1e-7);
	const BallState End = rb::SegmentEndState(M, kNumerics);
	RB_CHECK_NEAR(End.Position.x, 0.8896504, 1e-7);
	RB_CHECK_NEAR(End.Velocity.x, 0.7142857, 1e-7);
	RB_CHECK(End.State == MotionState::Rolling);

	// Stun instant (w_y = 0) inside the slide.
	const double TauStun = -OmegaY / M.OmegaDotH.y;
	RB_CHECK_NEAR(TauStun, 0.5098581, 1e-7);
	const BallState Stun = rb::EvaluateSegment(M, TauStun);
	RB_CHECK_NEAR(Stun.Omega.y, 0.0, 1e-12);
	RB_CHECK_NEAR(Stun.Position.x, 0.7647872, 1e-7);
	RB_CHECK_NEAR(Stun.Velocity.x, 1.0000000, 1e-7);

	// Published cross-check, TP A.18 b/R = -0.5 at 3 mph: stun 1.128 ft, sliding 1.312 ft.
	const double V3 = 1.34112;
	const BallState S3 = OnCloth({V3, 0.0, 0.0}, {0.0, -1.25 * V3 / kR, 0.0});
	const MotionSegment M3 = Seg(S3);
	const double Tau3 = 1.25 * V3 / kR / M3.OmegaDotH.y;
	const BallState Stun3 = rb::EvaluateSegment(M3, Tau3);
	RB_CHECK_NEAR(Stun3.Position.x, 0.34389, 1e-5);
	RB_CHECK_NEAR(Stun3.Position.x / 0.3048, 1.1282, 1e-4);
	RB_CHECK_NEAR(Stun3.Position.x / 0.3048, 1.128, 1e-3);
	const BallState End3 = rb::SegmentEndState(M3, kNumerics);
	RB_CHECK_NEAR(End3.Position.x, 0.40003, 1e-5);
	RB_CHECK_NEAR(End3.Position.x / 0.3048, 1.3124, 1e-4);
	RB_CHECK_NEAR(End3.Position.x / 0.3048, 1.312, 1e-3);
}

RB_TEST(MOT_TA5_FollowHalfTip)
{
	const BallState S = OnCloth({2.0, 0.0, 0.0}, {0.0, 2.5 / kR, 0.0});
	const Vec3 U = rb::SlipVelocity(S.Velocity, S.Omega, kR);
	RB_CHECK_NEAR(U.x, -0.5, 1e-7);
	const MotionSegment M = Seg(S);
	RB_CHECK_NEAR(M.TauEnd, 0.0728369, 1e-7);
	const BallState End = rb::SegmentEndState(M, kNumerics);
	RB_CHECK_NEAR(End.Position.x, 0.1508764, 1e-7);
	RB_CHECK_NEAR(End.Velocity.x, 2.1428571, 1e-7); // friction accelerates the overspinning ball (impl. note 15)
	RB_CHECK(End.State == MotionState::Rolling);
}

RB_TEST(MOT_TA6_SwerveParabola)
{
	const BallState S = OnCloth({2.0, 0.0, 0.0}, {30.0, 0.0, 0.0});
	RB_REQUIRE(S.State == MotionState::Sliding);
	const Vec3 U = rb::SlipVelocity(S.Velocity, S.Omega, kR);
	RB_CHECK_NEAR(U.x, 2.0, 1e-7);
	RB_CHECK_NEAR(U.y, 0.85725, 1e-7);
	const MotionSegment M = Seg(S);
	RB_CHECK_NEAR(M.TauEnd, 0.3169828, 1e-7);
	const BallState Mid = rb::EvaluateSegment(M, 0.5 * M.TauEnd);
	RB_CHECK_NEAR(Mid.Position.x, 0.2943411, 1e-7); // verification log correction 2
	RB_CHECK_NEAR(Mid.Position.y, -0.0097048, 1e-7);
	const BallState AtEnd = rb::EvaluateSegment(M, M.TauEnd);
	const BallState End = rb::SegmentEndState(M, kNumerics);
	for (const BallState* E : {&AtEnd, &End})
	{
		RB_CHECK_NEAR(E->Position.x, 0.5433990, 1e-7);
		RB_CHECK_NEAR(E->Position.y, -0.0388191, 1e-7);
		RB_CHECK_NEAR(E->Velocity.x, 1.4285714, 1e-7);
		RB_CHECK_NEAR(E->Velocity.y, -0.2449286, 1e-7);
		RB_CHECK_NEAR(E->Omega.x, 8.5714286, 1e-7);
		RB_CHECK_NEAR(E->Omega.y, 49.99375, 1e-5);
		RB_CHECK_NEAR(E->Omega.z, 0.0, 1e-12);
		const Vec3 UEnd = rb::SlipVelocity(E->Velocity, E->Omega, kR);
		RB_CHECK_NEAR(UEnd.x, 0.0, 1e-12);
		RB_CHECK_NEAR(UEnd.y, 0.0, 1e-12);
	}
	RB_CHECK(End.State == MotionState::Rolling);
}

RB_TEST(MOT_TA7_CoriolisInvariantProperty)
{
	rb::Rng Rng(0x7A7u);
	int Checked = 0;
	for (int Case = 0; Case < 10000; ++Case)
	{
		// |v| <= 12, |w| <= 500 (uniform in the disc / ball by rejection).
		Vec3 V;
		do
		{
			V = {Rng.NextUniform(-12.0, 12.0), Rng.NextUniform(-12.0, 12.0), 0.0};
		} while (rb::LengthSquared(V) > 144.0);
		Vec3 W;
		do
		{
			W = {Rng.NextUniform(-500.0, 500.0), Rng.NextUniform(-500.0, 500.0), Rng.NextUniform(-500.0, 500.0)};
		} while (rb::LengthSquared(W) > 250000.0);
		const BallState S = OnCloth(V, W);
		if (S.State != MotionState::Sliding)
		{
			continue;
		}
		++Checked;
		const MotionSegment M = Seg(S);
		// v(tau_slide) = (5/7) v0 - (2/7) R z_hat x w0
		const Vec3 Expected = {5.0 / 7.0 * V.x + 2.0 / 7.0 * kR * W.y, 5.0 / 7.0 * V.y - 2.0 / 7.0 * kR * W.x, 0.0};
		const BallState AtEnd = rb::EvaluateSegment(M, M.TauEnd);
		const BallState End = rb::SegmentEndState(M, kNumerics);
		RB_CHECK_NEAR(AtEnd.Velocity.x, Expected.x, 1e-12);
		RB_CHECK_NEAR(AtEnd.Velocity.y, Expected.y, 1e-12);
		RB_CHECK_NEAR(End.Velocity.x, Expected.x, 1e-12);
		RB_CHECK_NEAR(End.Velocity.y, Expected.y, 1e-12);
		// |u(tau)| linear, u_hat constant.
		const Vec3 U0 = rb::SlipVelocity(V, W, kR);
		const double U0Len = rb::Length(U0);
		for (int j = 1; j <= 9; ++j)
		{
			const double Tau = M.TauEnd * j / 10.0;
			const BallState E = rb::EvaluateSegment(M, Tau);
			const Vec3 U = rb::SlipVelocity(E.Velocity, E.Omega, kR);
			const double ULen = rb::Length(rb::Planar(U));
			RB_CHECK_NEAR(ULen, U0Len * (1.0 - Tau / M.TauEnd), 1e-12);
			if (ULen > 1e-2)
			{
				RB_CHECK_NEAR(U.x / ULen, U0.x / U0Len, 1e-12);
				RB_CHECK_NEAR(U.y / ULen, U0.y / U0Len, 1e-12);
			}
		}
	}
	RB_CHECK(Checked > 9900);
}

RB_TEST(MOT_TA8_Spinning)
{
	const BallState S = OnCloth(Vec3::Zero(), {0.0, 0.0, 20.0});
	RB_REQUIRE(S.State == MotionState::Spinning);
	const MotionSegment M = Seg(S);
	RB_CHECK_NEAR(rb::EvaluateSegment(M, 1.0).Omega.z, 10.0, 1e-9 * 10.0);
	RB_CHECK_NEAR(M.TauEnd, 2.000000, 1e-6);
	const BallState End = rb::SegmentEndState(M, kNumerics);
	RB_CHECK(End.State == MotionState::Stationary);
	RB_CHECK(End.Omega == Vec3::Zero());
}

RB_TEST(MOT_TA9_RollingSmallSideSpin)
{
	const BallState S = OnCloth({1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 5.0});
	RB_REQUIRE(S.State == MotionState::Rolling);
	const MotionSegment M = Seg(S);
	RB_CHECK(rb::EvaluateSegment(M, 0.5).Omega.z == 0.0);
	for (double T : {0.5000001, 1.0, 5.0, 10.0, M.TauEnd})
	{
		RB_CHECK(rb::EvaluateSegment(M, T).Omega.z == 0.0); // stays exactly 0
	}
	RB_CHECK_NEAR(M.TauEnd, 10.197162, 1e-6);
	const BallState End = rb::SegmentEndState(M, kNumerics);
	RB_CHECK(End.State == MotionState::Stationary); // no Spinning segment emitted
}

RB_TEST(MOT_TA10_RollingLargeSideSpin)
{
	const BallRun Run = RunBall(OnCloth({0.1, 0.0, 0.0}, {0.0, 0.1 / kR, 20.0}), MotSpec(), MotCloth(), MotSlate(), kG);
	RB_REQUIRE(Run.Events.size() == 2u);
	RB_CHECK(Run.Events[0].From == MotionState::Rolling && Run.Events[0].State.State == MotionState::Spinning);
	RB_CHECK_NEAR(Run.Events[0].Time, 1.0197162, 1e-7);
	RB_CHECK_NEAR(Run.Events[0].State.Omega.z, 9.8028379, 1e-7);
	RB_CHECK(Run.Events[1].State.State == MotionState::Stationary);
	RB_CHECK_NEAR(Run.Events[1].Time, 2.000000, 1e-6);
	RB_CHECK(Run.Final.State == MotionState::Stationary);
}

RB_TEST(MOT_TA11_Classification)
{
	struct Case
	{
		Vec3 V;
		Vec3 W;
		MotionState Expected;
	};
	const Case Cases[] = {
		{{1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 0.0}, MotionState::Rolling},
		{{1.0, 0.0, 0.0}, Vec3::Zero(), MotionState::Sliding},
		{Vec3::Zero(), {0.0, 0.0, 3.0}, MotionState::Spinning},
		{Vec3::Zero(), Vec3::Zero(), MotionState::Stationary},
		{{0.0, 0.0, 0.5}, Vec3::Zero(), MotionState::Airborne},
	};
	for (const Case& C : Cases)
	{
		BallState S;
		S.Position = {0.1, -0.2, kR};
		S.Velocity = C.V;
		S.Omega = C.W;
		RB_CHECK(rb::ClassifyState(S, kR, 0.0, kNumerics) == C.Expected);
		RB_CHECK(S.State == C.Expected);
	}
	// Airborne by height; snaps of the on-cloth states.
	BallState High;
	High.Position = {0.0, 0.0, kR + 1e-6};
	RB_CHECK(rb::ClassifyState(High, kR, 0.0, kNumerics) == MotionState::Airborne);
	BallState Low;
	Low.Position = {0.0, 0.0, kR + 5e-10};
	Low.Velocity = {1.0, 0.0, 5e-10};
	Low.Omega = {0.0, 1.0 / kR + 1e-12, 0.0};
	RB_CHECK(rb::ClassifyState(Low, kR, 0.0, kNumerics) == MotionState::Rolling);
	RB_CHECK(Low.Position.z == kR && Low.Velocity.z == 0.0);
	RB_CHECK(Low.Omega.y == 1.0 / kR && Low.Omega.x == 0.0); // w_h := z_hat x v / R
	BallState Spin;
	Spin.Position = {0.0, 0.0, kR};
	Spin.Velocity = {1e-10, 0.0, 0.0};
	Spin.Omega = {1e-9, 0.0, 2.0};
	RB_CHECK(rb::ClassifyState(Spin, kR, 0.0, kNumerics) == MotionState::Spinning);
	RB_CHECK(Spin.Velocity == Vec3::Zero() && Spin.Omega.x == 0.0 && Spin.Omega.y == 0.0 && Spin.Omega.z == 2.0);
	// Simulator-owned states are returned unchanged.
	for (MotionState Owned : {MotionState::PocketPivot, MotionState::PocketFall, MotionState::Pocketed, MotionState::OffTable})
	{
		BallState P;
		P.Position = {0.0, 0.0, -0.01};
		P.Velocity = {0.3, 0.0, -1.0};
		P.State = Owned;
		const BallState Before = P;
		RB_CHECK(rb::ClassifyState(P, kR, 0.0, kNumerics) == Owned);
		RB_CHECK(P.Position == Before.Position && P.Velocity == Before.Velocity);
	}
}

RB_TEST(MOT_TA12_EnergyNonIncreasingProperty)
{
	rb::Rng Rng(0xA12u);
	const rb::BallSpec Spec = MotSpec();
	int Counts[4] = {0, 0, 0, 0};
	for (int Case = 0; Case < 10000; ++Case) // property test: >= 1e4 cases (architecture 18)
	{
		const int Kind = Case % 3; // Sliding, Rolling, Spinning
		Vec3 V{Rng.NextUniform(-6.0, 6.0), Rng.NextUniform(-6.0, 6.0), 0.0};
		Vec3 W{Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0), Rng.NextUniform(-300.0, 300.0)};
		if (Kind == 1)
		{
			W = {-V.y / kR, V.x / kR, W.z};
		}
		else if (Kind == 2)
		{
			V = Vec3::Zero();
			W = {0.0, 0.0, W.z};
		}
		const BallState S = OnCloth(V, W);
		++Counts[static_cast<int>(S.State)];
		const MotionSegment M = Seg(S);
		const double Horizon = M.TauEnd < rb::kInfinity ? M.TauEnd : 1.0;
		double Previous = rb::MechanicalEnergy(rb::EvaluateSegment(M, 0.0), Spec, kG);
		for (int j = 1; j <= 100; ++j)
		{
			const double E = rb::MechanicalEnergy(rb::EvaluateSegment(M, Horizon * j / 100.0), Spec, kG);
			RB_CHECK(E <= Previous * (1.0 + 1e-12));
			Previous = E;
		}
		const double EndEnergy = rb::MechanicalEnergy(rb::SegmentEndState(M, kNumerics), Spec, kG);
		RB_CHECK(EndEnergy <= Previous * (1.0 + 1e-12) + 1e-300);
	}
	RB_CHECK(Counts[static_cast<int>(MotionState::Sliding)] > 3000);
	RB_CHECK(Counts[static_cast<int>(MotionState::Rolling)] > 3000);
	RB_CHECK(Counts[static_cast<int>(MotionState::Spinning)] > 3000);
}

RB_TEST(UE_T24_RollingSignCheck)
{
	// Ball rolling toward +x at 1 m/s: core w = (0, 34.9956, 0) rad/s, u = 0 (the UE adapter mirrors it; UE 5.6).
	const BallState S = OnCloth({1.0, 0.0, 0.0}, {0.0, 1.0 / kR, 0.0});
	const MotionSegment M = Seg(S);
	const BallState E = rb::EvaluateSegment(M, 0.0);
	RB_CHECK(E.State == MotionState::Rolling);
	RB_CHECK_NEAR(E.Omega.x, 0.0, 1e-6);
	RB_CHECK_NEAR(E.Omega.y, 1.0 / kR, 1e-6);
	RB_CHECK_NEAR(E.Omega.y, 34.9956, 1e-4); // listed to 4 decimals
	RB_CHECK_NEAR(E.Omega.z, 0.0, 1e-6);
	const Vec3 U = rb::SlipVelocity(E.Velocity, E.Omega, kR);
	RB_CHECK_NEAR(rb::Length(U), 0.0, 1e-6);
	RB_CHECK(E.Omega.y > 0.0); // topspin of a +x roll is +w_y (MOT 0.3 sign check)
}
