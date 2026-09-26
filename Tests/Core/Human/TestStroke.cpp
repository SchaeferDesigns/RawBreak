// Owner: WP-11 (player model). human-factors 3.3-3.7 (the executed stroke): HF-T05..T11 (the parts that need WP-1's
// AimToContactOffset / MiscueLimit / SquirtAngle carry the Integ_ prefix), ARCH A-HUM-2, A-HUM-3, HF-B01, HF-B08, HF-B13.
// Expected values: Tools/reference/human-factors/recompute_v12.py (stroke), chalk.py (T09), the new warp law of 4.4 (T10).

#include "Human/HumanTestUtil.h"

#include "rb/Human/CueState.h"
#include "rb/Human/Progression.h"
#include "rb/Physics/CueStrike.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

namespace
{
	double SourceSum(const ExecutedStroke& S, double StrokeDelta::*Field)
	{
		double Sum = 0.0;
		for (int s = 0; s < kStrokeSourceCount; ++s)
		{
			Sum += S.Channels.Sources[s].*Field;
		}
		return Sum;
	}
}

RB_TEST(HF_T05_FullStrokeAttributes25)
{
	const Setup S = MakeS0();
	const ExecutedStroke X = Execute(S);
	RB_REQUIRE(X.Error == ErrorCode::Ok);
	// Spec digits (1e-9 relative) ...
	RB_CHECK_REL(X.Strike.Azimuth, 2.705488099e-4, 1e-9);
	RB_CHECK_REL(X.Strike.Elevation, 0.05293479862, 1e-9);
	RB_CHECK_REL(X.AxisOffset.x, 0.01012217658, 1e-9);
	RB_CHECK_REL(X.AxisOffset.y, 0.001521750879, 1e-9);
	RB_CHECK_REL(X.Strike.Speed, 2.034073630, 1e-9);
	RB_CHECK_REL(X.TipTransverseVelocity.x, 0.001953103889, 1e-9);
	RB_CHECK_REL(X.TipTransverseVelocity.y, -9.111677241e-4, 1e-9);
	// ... and the oracle's full digits.
	RB_CHECK_REL(X.Strike.Azimuth, 0.00027054880986183174, 1e-12);
	RB_CHECK_REL(X.Strike.Elevation, 0.052934798618283894, 1e-12);
	RB_CHECK_REL(X.AxisOffset.x, 0.010122176582561291, 1e-12);
	RB_CHECK_REL(X.AxisOffset.y, 0.0015217508792324094, 1e-12);
	RB_CHECK_REL(X.Strike.Speed, 2.0340736299606164, 1e-12);
	// Channels.
	const StrokeBreakdown& B = X.Channels;
	const double Yg = B.Factors.DriftSigma * B.DriftLat;
	const double Zg = 0.5 * B.Factors.DriftSigma * B.DriftVert;
	RB_CHECK_REL(Yg, 2.164390e-4, 1e-6);
	RB_CHECK_REL(Zg, 5.178315e-4, 1e-6);
	RB_CHECK_REL(Yg, 0.0002164390478894654, 1e-9);
	RB_CHECK_REL(Zg, 0.0005178315122236452, 1e-9);
	RB_CHECK_REL(B.TipA.Eps, 1.845629423, 1e-9);
	RB_CHECK_NEAR(B.TipB.Eps, 0.164127879, 1e-9); // exact: +-1 in the last listed digit
	RB_CHECK_REL(B.TipB.Eps, 0.16412787935213694, 1e-12);
	RB_CHECK_REL(B.Elevation.Eps, -0.01036599996, 1e-9);
	RB_CHECK_NEAR(B.Speed.Eps, 0.340736300, 1e-9);
	RB_CHECK_REL(B.Speed.Eps, 0.340736299606163, 1e-12);
	RB_CHECK_REL(B.Factors.SettleIn, 1.043936934, 1e-9);
	RB_CHECK(B.Factors.Settle == 1.0 && B.Factors.PressureGain == 1.0 && B.Factors.Flinch == 0.0);
	// The sources add up to the executed change (no floor / clamp bites here).
	RB_CHECK_NEAR(SourceSum(X, &StrokeDelta::Azimuth), X.Strike.Azimuth - S.Intended.Azimuth, 1e-15);
	RB_CHECK_NEAR(SourceSum(X, &StrokeDelta::Elevation), X.Strike.Elevation - S.Intended.Elevation, 1e-15);
	RB_CHECK_NEAR(SourceSum(X, &StrokeDelta::AxisA), X.AxisOffset.x - S.Intended.AxisOffsetA, 1e-15);
	RB_CHECK_NEAR(SourceSum(X, &StrokeDelta::AxisB), X.AxisOffset.y - S.Intended.AxisOffsetB, 1e-15);
	RB_CHECK_NEAR(SourceSum(X, &StrokeDelta::Speed), X.Strike.Speed - S.Intended.Speed, 1e-14);
	// Tip fields of the strike come from the TipState (4.1 / 4.2): fresh chalk mu 0.6 at the centre, the current dome.
	RB_CHECK(X.Strike.Cue.TipDomeRadius == S.Tip.DomeRadius && X.Strike.Cue.TipRestitution == S.Tip.Restitution);
	RB_CHECK(X.Strike.Cue.Mass == S.Cue.Mass && X.Strike.Cue.EndMass == S.Cue.EndMass);
	RB_CHECK(!X.Strike.TipTouchesCloth && !X.ElevationClamped && !X.OffsetClamped);
}

RB_TEST(Integ_HF_T05_ContactOffsetsAndFriction)
{
	const Setup S = MakeS0();
	const ExecutedStroke X = Execute(S);
	RB_CHECK_REL(X.Strike.OffsetA, 0.007383310679, 1e-9);
	RB_CHECK_REL(X.Strike.OffsetB, 0.001109994419, 1e-9);
	RB_CHECK_NEAR(X.Strike.Cue.TipFriction, 0.6, 1e-15);
	RB_CHECK(X.Strike.Cue.TipFrictionKinetic == X.Strike.Cue.TipFriction);
	RB_CHECK(!X.PredictedMiscue);
	RB_CHECK_REL(X.MiscueLimit, 0.6 / std::sqrt(1.36), 1e-12);
	RB_CHECK_NEAR(X.Rho, std::hypot(X.Strike.OffsetA, X.Strike.OffsetB), 1e-15);
}

RB_TEST(HF_T06_PressureAndSettle)
{
	Setup S = MakeS0();
	S.Situation.Pressure = 1.0;
	S.Intended.SettleStart = 0.8;
	const ExecutedStroke X = Execute(S);
	RB_REQUIRE(X.Error == ErrorCode::Ok);
	RB_CHECK(X.Channels.Factors.PressureGain == 5.0);
	RB_CHECK(X.Channels.Factors.Settle == 0.3);
	RB_CHECK_REL(X.Channels.Factors.Flinch, 0.072544, 1e-5);
	RB_CHECK_REL(X.Channels.Factors.Flinch, 0.07254386596154035, 1e-12);
	RB_CHECK_REL(X.Strike.Azimuth, 1.387895872e-4, 1e-9);
	RB_CHECK_REL(X.Strike.Elevation, 0.05261956401, 1e-9);
	RB_CHECK_REL(X.AxisOffset.x, 0.01086040465, 1e-9);
	RB_CHECK_REL(X.AxisOffset.y, -0.04940815491, 1e-9);
	RB_CHECK_REL(X.Strike.Speed, 1.925576035, 1e-9);
	RB_CHECK_REL(X.Strike.Azimuth, 0.00013878958717972179, 1e-12);
	RB_CHECK_REL(X.AxisOffset.y, -0.04940815491422475, 1e-12);
	RB_CHECK_REL(X.Strike.Speed, 1.9255760346392958, 1e-12);
	// Drift gain g^(1/3) (refit 9.2 item 3), tremor g, speed g^(1/2).
	const SituationFactors Calm = ComputeSituationFactors(S.Intended, S.Attributes, StrokeSituation{}, S.Params, 2.5, kR);
	RB_CHECK_REL(X.Channels.Factors.DriftSigma, Calm.DriftSigma * std::cbrt(5.0), 1e-12);
	RB_CHECK_REL(X.Channels.Factors.TremorSigma, Calm.TremorSigma * 5.0, 1e-12);
	RB_CHECK_REL(X.Channels.Factors.SpeedSigma, Calm.SpeedSigma * std::sqrt(5.0), 1e-12);
	RB_CHECK_REL(X.Channels.Factors.GripBias, -1.5e-3, 1e-12);
}

RB_TEST(HF_T07_Attributes100)
{
	Setup S = MakeS0();
	S.Attributes = UniformAttributes(100.0);
	const ExecutedStroke X = Execute(S);
	RB_REQUIRE(X.Error == ErrorCode::Ok);
	RB_CHECK_REL(X.Strike.Azimuth, 5.410976197e-5, 1e-9);
	RB_CHECK_REL(X.Strike.Elevation, 0.05247124335, 1e-9);
	RB_CHECK_REL(X.AxisOffset.x, 0.002165156180, 1e-9);
	RB_CHECK_REL(X.AxisOffset.y, -0.001228559438, 1e-9);
	RB_CHECK_REL(X.Strike.Speed, 2.010222089, 1e-9);
	RB_CHECK_REL(X.Strike.Speed, 2.010222088988185, 1e-12);
}

RB_TEST(HF_T08_IdentityAtNoiseScaleZero)
{
	Setup S = MakeS0();
	S.Params.NoiseScale = 0.0;
	const ExecutedStroke X = Execute(S);
	RB_REQUIRE(X.Error == ErrorCode::Ok);
	RB_CHECK(SameBits(X.Strike.Azimuth, S.Intended.Azimuth));
	RB_CHECK(SameBits(X.Strike.Elevation, 3.0 * kDegToRad));
	RB_CHECK(SameBits(X.AxisOffset.x, 0.0) && SameBits(X.AxisOffset.y, 0.0));
	RB_CHECK(SameBits(X.Strike.OffsetA, 0.0) && SameBits(X.Strike.OffsetB, 0.0));
	RB_CHECK(SameBits(X.Strike.Speed, 2.0));
	RB_CHECK(X.TipTransverseVelocity.x == 0.0 && X.TipTransverseVelocity.y == 0.0);
	RB_CHECK_NEAR(X.Strike.Cue.TipFriction, 0.6, 1e-15);
	RB_CHECK(X.Contact.Zone == TipZone::Dome && X.Contact.Coverage == 1.0);

	// Also with off-centre input and a masked-only human layer (ChannelMask of every human channel).
	Setup M = MakeS0();
	M.Intended.Azimuth = 0.3;
	M.Intended.AxisOffsetA = 0.2;
	M.Intended.AxisOffsetB = -0.3;
	M.Params.ChannelMask = kWatchableChannelMask | kPerShotChannelMask;
	const ExecutedStroke Y = Execute(M);
	RB_CHECK(SameBits(Y.Strike.Azimuth, 0.3) && SameBits(Y.AxisOffset.x, 0.2) && SameBits(Y.AxisOffset.y, -0.3) && SameBits(Y.Strike.Speed, 2.0));
}

RB_TEST(HF_T09_PivotGeometry)
{
	// Drift through the pivot: yaw = y_g / L_bg, the tip moves the other way by yaw L_bc (A shift -yaw L_bc / R).
	Setup S = MakeS0();
	S.Params.ChannelMask = kPerShotChannelMask | ChannelBit(NoiseChannel::DriftVert) | ChannelBit(NoiseChannel::TremorLat) | ChannelBit(NoiseChannel::TremorVert);
	const ExecutedStroke X = Execute(S);
	const StrokeDelta& Drift = X.Channels.Sources[static_cast<int>(StrokeSource::Drift)];
	const double Yg = X.Channels.Factors.DriftSigma * X.Channels.DriftLat;
	const double Lbc = S.Situation.BridgeLength + kR;
	RB_CHECK_REL(Drift.Azimuth, Yg / S.Situation.BridgeToGrip, 1e-14);
	RB_CHECK_REL(Drift.AxisA, -Drift.Azimuth * Lbc / kR, 1e-14);
	RB_CHECK_REL(X.AxisOffset.x, Drift.AxisA, 1e-14); // nothing else acts laterally
	// The same linear maps at y_g = 1 mm: yaw 1.25e-3 rad, A shift -0.009998906.
	const double Yaw = 1e-3 / S.Situation.BridgeToGrip;
	RB_CHECK_REL(Yaw, 1.25e-3, 1e-15);
	RB_CHECK_REL(-Yaw * Lbc / kR, -0.009998906, 1e-7);
	// Natural pivot of the budget model (TP A.31 squirt, 1/k = 2.5): L_p = (R + r_tip) / (d alpha_sq / da)|0.
	const double RDome = 0.0106;
	const double MassRatios[3] = {15.0, 20.0, 40.0};
	const double Pivot[3] = {0.289895, 0.368245, 0.681645};
	const double NetAtDefault[3] = {0.211535, 0.379294, 0.664677};
	for (int i = 0; i < 3; ++i)
	{
		const double Slope = 2.5 / (3.5 + MassRatios[i]);
		const double Lp = (kR + RDome) / Slope;
		RB_CHECK_NEAR(Lp, Pivot[i], 5e-7);
		const double YawIn = 1e-3;
		const double AtPivot = (YawIn + BudgetSquirt(-YawIn * Lp / (kR + RDome), MassRatios[i])) / YawIn;
		RB_CHECK(std::fabs(AtPivot) < 2e-4);
		const double AtDefault = (YawIn + BudgetSquirt(-YawIn * 0.228575 / (kR + RDome), MassRatios[i])) / YawIn;
		RB_CHECK_NEAR(AtDefault, NetAtDefault[i], 5e-7);
	}
}

RB_TEST(Integ_HF_T09_NaturalPivotWithCoreSquirt)
{
	// The same with WP-1's AimToContactOffset and SquirtAngle (MOT B.3, B.7).
	const double RDome = 0.0106;
	const double MassRatios[3] = {15.0, 20.0, 40.0};
	const double Pivot[3] = {0.289895, 0.368245, 0.681645};
	const double NetAtDefault[3] = {0.211535, 0.379294, 0.664677};
	for (int i = 0; i < 3; ++i)
	{
		const double H = 1e-7;
		const double Slope = (SquirtAngle(H, MassRatios[i]) - SquirtAngle(-H, MassRatios[i])) / (2.0 * H);
		const double Lp = (kR + RDome) / Slope;
		RB_CHECK_NEAR(Lp, Pivot[i], 5e-6);
		const double Yaw = 1e-3;
		const Vec2 Contact = AimToContactOffset({-Yaw * 0.228575 / kR, 0.0}, kR, RDome);
		RB_CHECK_NEAR((Yaw + SquirtAngle(Contact.x, MassRatios[i])) / Yaw, NetAtDefault[i], 5e-6);
	}
}

RB_TEST(HF_T10_Warp)
{
	const Setup S = MakeS0();
	CueBodyState Body;
	Body.BowSag = 3e-3;
	Body.WarpKnown = false;
	const CueSpec House = kCueHouse19oz;
	RB_REQUIRE(House.Length == 1.4478);
	const WarpEffect W = ComputeWarp(Body, House, S.Key, S.Params);
	RB_CHECK_REL(W.Gamma, 2.576182438e-3, 1e-9);
	RB_CHECK_REL(W.Roll, 5.442345308, 1e-9);
	RB_CHECK_REL(W.AzimuthError, -1.919780235e-3, 1e-9);
	RB_CHECK_REL(W.ElevationError, 1.717894002e-3, 1e-9);
	RB_CHECK_NEAR(W.Gamma * kRadToDeg, 0.148, 5e-4); // 0.148 deg for a 3 mm bow (4.4)
	Body.WarpKnown = true;
	const WarpEffect Known = ComputeWarp(Body, House, S.Key, S.Params);
	RB_CHECK(Known.AzimuthError == 0.0);
	RB_CHECK(Known.ElevationError == W.Gamma);
	// Equipment: neither NoiseScale nor the channel mask changes the warp.
	HumanParams Quiet = S.Params;
	Quiet.NoiseScale = 0.0;
	Quiet.ChannelMask = ~0u;
	Body.WarpKnown = false;
	RB_CHECK(ComputeWarp(Body, House, S.Key, Quiet).AzimuthError == W.AzimuthError);
	// ExecuteStroke adds it to the executed pose.
	Setup Bowed = MakeS0();
	Bowed.Cue = House;
	Bowed.CueBody = Body;
	Bowed.Params.NoiseScale = 0.0;
	const ExecutedStroke X = Execute(Bowed);
	RB_CHECK(X.Strike.Azimuth == W.AzimuthError && X.Strike.Elevation == 3.0 * kDegToRad + W.ElevationError);
	RB_CHECK(X.Channels.Sources[static_cast<int>(StrokeSource::Warp)].Azimuth == W.AzimuthError);
	// The pickup roll test notices 1 mm at habit 0, 0.3 mm at habit 1 (HF-31).
	RB_CHECK_NEAR(NoticeableBow(0.0), 1e-3, 1e-15);
	RB_CHECK_NEAR(NoticeableBow(1.0), 0.3e-3, 1e-15);
	CueBodyState Slight;
	Slight.BowSag = 0.5e-3;
	RB_CHECK(!AutoRollTestNotices(Slight, 0.0) && AutoRollTestNotices(Slight, 1.0));
}

RB_TEST(HF_T11_SwoopIsOutputOnly)
{
	Setup S = MakeS0();
	S.Params.NoiseScale = 0.0;
	S.Intended.AxisOffsetA = 0.658058;
	S.Intended.Speed = 1.0;
	ExecutedStroke X[3];
	const double Swoop[3] = {0.0, 0.05, -0.05};
	for (int i = 0; i < 3; ++i)
	{
		Setup V = S;
		V.Intended.TipVelocityRight = Swoop[i];
		X[i] = Execute(V);
		RB_CHECK(X[i].TipTransverseVelocity.x == Swoop[i]);
	}
	RB_CHECK(SameStrike(X[0].Strike, X[1].Strike) && SameStrike(X[0].Strike, X[2].Strike));
	RB_CHECK(X[0].PredictedMiscue == X[1].PredictedMiscue && X[0].PredictedMiscue == X[2].PredictedMiscue);
	RB_CHECK(SameBits(X[0].AxisOffset.x, 0.658058));
}

RB_TEST(Integ_HF_T11_SwoopNoMiscueAtA048)
{
	Setup S = MakeS0();
	S.Params.NoiseScale = 0.0;
	S.Intended.AxisOffsetA = 0.658058;
	S.Intended.Speed = 1.0;
	for (const double Swoop : {0.0, 0.05, -0.05})
	{
		Setup V = S;
		V.Intended.TipVelocityRight = Swoop;
		const ExecutedStroke X = Execute(V);
		RB_CHECK_NEAR(X.Strike.OffsetA, 0.48, 1e-6);
		RB_CHECK_NEAR(X.MiscueLimit, 0.514496, 1e-6);
		RB_CHECK(!X.PredictedMiscue);
	}
}

RB_TEST(HF_S04_RoutineBudget)
{
	// Budget model 3.10 (m / m_e 15), key {0xB00D, Rack i / 20, Shot i, Shooter 1, ShooterShot i}, t_c = 1.5 + 2.5 U01(HashKeys(99, i)),
	// V 1.5 m/s; perfect input. Counts are exact per CRT; tolerance +-2.
	const double Attributes[6] = {25.0, 10.0, 40.0, 60.0, 85.0, 100.0};
	const int Expected[6] = {214, 1310, 3, 0, 0, 0};
	for (int a = 0; a < 6; ++a)
	{
		Setup S = MakeS0();
		S.Attributes = UniformAttributes(Attributes[a]);
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
			const double Error = X.Strike.Azimuth - S.Intended.Azimuth + BudgetSquirt(C.A, 15.0);
			SumSq += Error * Error;
			Misses += BudgetPotMissed(Error, kR) ? 1 : 0;
			AdvanceNoiseHistory(History);
		}
		RB_CHECK(std::abs(Misses - Expected[a]) <= 2);
		if (a == 0)
		{
			RB_CHECK_NEAR(std::sqrt(SumSq / 20000.0) * kRadToDeg, 0.041, 5e-4); // CB direction sigma 0.041 deg (3.10)
		}
	}
}

RB_TEST(HF_S05_MaxDrawMiscues)
{
	// b aimed at -0.45 (B_i = -0.61693), fresh chalk, V 2 m/s; miscue = executed rho > rho_max (mu 0.6, no swoop term).
	const double Attributes[6] = {10.0, 25.0, 40.0, 60.0, 85.0, 100.0};
	const int Expected[6] = {3039, 1617, 516, 0, 0, 0};
	const double RhoMax = 0.6 / std::sqrt(1.0 + 0.36);
	for (int a = 0; a < 6; ++a)
	{
		Setup S = MakeS0();
		S.Attributes = UniformAttributes(Attributes[a]);
		S.Intended.AxisOffsetB = -0.61693;
		NoiseHistory History = RebuildNoiseHistory(0xD4A3u, 2u, 0u);
		int Miscues = 0;
		for (std::uint32_t i = 0; i < 20000u; ++i)
		{
			S.Key = MakeKey(0xD4A3u, i / 20u, i, 2u, i);
			S.History = History;
			S.Intended.TimeDown = 1.5 + 2.5 * U01(HashKeys(98u, i));
			const ExecutedStroke X = Execute(S);
			Miscues += BudgetContact(X, kR, S.Tip.DomeRadius).Rho > RhoMax ? 1 : 0;
			AdvanceNoiseHistory(History);
		}
		RB_CHECK(std::abs(Miscues - Expected[a]) <= 2);
	}
}

// HF-S04 / HF-S05 through the core (architecture 17: the budget model uses WP-1): the squirt of SquirtAngle at the executed
// contact offset Strike.OffsetA (AimToContactOffset with the current dome), and the miscue as ExecuteStroke's own prediction
// (rho after the 0.9 clamp against MiscueLimit of the chalk-map mu). Same counts as the self-contained model above.
RB_TEST(Integ_HF_S04_S05_BudgetsThroughTheCore)
{
	const double Attributes[3] = {25.0, 10.0, 40.0};
	const int ExpectedMisses[3] = {214, 1310, 3};
	for (int a = 0; a < 3; ++a)
	{
		Setup S = MakeS0();
		S.Attributes = UniformAttributes(Attributes[a]);
		S.Intended.Speed = 1.5;
		NoiseHistory History = RebuildNoiseHistory(0xB00Du, 1u, 0u);
		int Misses = 0;
		for (std::uint32_t i = 0; i < 20000u; ++i)
		{
			S.Key = MakeKey(0xB00Du, i / 20u, i, 1u, i);
			S.History = History;
			S.Intended.TimeDown = 1.5 + 2.5 * U01(HashKeys(99u, i));
			const ExecutedStroke X = Execute(S);
			const double Error = X.Strike.Azimuth - S.Intended.Azimuth + SquirtAngle(X.Strike.OffsetA, 15.0);
			Misses += BudgetPotMissed(Error, kR) ? 1 : 0;
			AdvanceNoiseHistory(History);
		}
		RB_CHECK(std::abs(Misses - ExpectedMisses[a]) <= 2);
	}
	const double DrawAttributes[4] = {10.0, 25.0, 40.0, 60.0};
	const int ExpectedMiscues[4] = {3039, 1617, 516, 0};
	for (int a = 0; a < 4; ++a)
	{
		Setup S = MakeS0();
		S.Attributes = UniformAttributes(DrawAttributes[a]);
		S.Intended.AxisOffsetB = -0.61693;
		NoiseHistory History = RebuildNoiseHistory(0xD4A3u, 2u, 0u);
		int Miscues = 0;
		for (std::uint32_t i = 0; i < 20000u; ++i)
		{
			S.Key = MakeKey(0xD4A3u, i / 20u, i, 2u, i);
			S.History = History;
			S.Intended.TimeDown = 1.5 + 2.5 * U01(HashKeys(98u, i));
			Miscues += Execute(S).PredictedMiscue ? 1 : 0;
			AdvanceNoiseHistory(History);
		}
		RB_CHECK(std::abs(Miscues - ExpectedMiscues[a]) <= 2);
	}
}

RB_TEST(HF_S06_PressureBudget)
{
	// As HF-S04, attributes 25. Rows: (P, Settle) -> misses for m / m_e 15, 20, 40 (-1 = not in the spec).
	struct Row
	{
		double Pressure;
		bool Settle;
		int Expected[3];
	};
	const Row Rows[4] = {{1.0, false, {1324, 1800, 4337}}, {1.0, true, {397, 439, 994}}, {0.0, true, {80, -1, -1}}, {0.0, false, {214, -1, 741}}};
	const double MassRatios[3] = {15.0, 20.0, 40.0};
	for (const Row& R : Rows)
	{
		Setup S = MakeS0();
		S.Intended.Speed = 1.5;
		S.Situation.Pressure = R.Pressure;
		NoiseHistory History = RebuildNoiseHistory(0xB00Du, 1u, 0u);
		int Misses[3] = {};
		for (std::uint32_t i = 0; i < 20000u; ++i)
		{
			S.Key = MakeKey(0xB00Du, i / 20u, i, 1u, i);
			S.History = History;
			S.Intended.TimeDown = 1.5 + 2.5 * U01(HashKeys(99u, i));
			S.Intended.SettleStart = R.Settle ? S.Intended.TimeDown - 2.0 : -1.0;
			const ExecutedStroke X = Execute(S);
			const ContactOffsets C = BudgetContact(X, kR, S.Tip.DomeRadius);
			for (int m = 0; m < 3; ++m)
			{
				Misses[m] += BudgetPotMissed(X.Strike.Azimuth - S.Intended.Azimuth + BudgetSquirt(C.A, MassRatios[m]), kR) ? 1 : 0;
			}
			AdvanceNoiseHistory(History);
		}
		for (int m = 0; m < 3; ++m)
		{
			if (R.Expected[m] >= 0)
			{
				RB_CHECK(std::abs(Misses[m] - R.Expected[m]) <= 2);
			}
		}
	}
}

// A-HUM-2: purity; SampleHand at t_c with the full ramp is the executed pose.
RB_TEST(ARCH_HUM2_PureStrokeAndWhatYouSeeIsWhatHits)
{
	Setup S = MakeS0();
	S.Situation.Pressure = 0.7;
	S.Intended.SettleStart = 1.0;
	S.Intended.AxisOffsetA = 0.2;
	S.Intended.HeadMovedBeforeContact = true;
	S.CueBody.BowSag = 1.5e-3;
	S.CueBody.WarpKnown = false;
	S.Key.ShooterShotIndex = 5u;
	S.History = RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), 5u);
	const ExecutedStroke A = Execute(S);
	const ExecutedStroke B = Execute(S);
	RB_CHECK(StrokeHash(A) == StrokeHash(B) && SameStrike(A.Strike, B.Strike));
	const HandPose P1 = Sample(S, 1.7);
	const HandPose P2 = Sample(S, 1.7);
	RB_CHECK(SameBits(P1.Azimuth, P2.Azimuth) && SameBits(P1.AxisOffsetB, P2.AxisOffsetB) && SameBits(P1.GripLateral, P2.GripLateral));
	// Full ramp at t_c (t_c - t_fwd = 0.3 s >= 0.1 s): the rendered pose is the executed stroke bit for bit.
	const HandPose AtContact = Sample(S, S.Intended.TimeDown);
	RB_CHECK(AtContact.Ramp == 1.0 && AtContact.RampShown);
	RB_CHECK(SameBits(AtContact.Azimuth, A.Strike.Azimuth));
	RB_CHECK(SameBits(AtContact.Elevation, A.Strike.Elevation));
	RB_CHECK(SameBits(AtContact.AxisOffsetA, A.AxisOffset.x));
	RB_CHECK(SameBits(AtContact.AxisOffsetB, A.AxisOffset.y));
	RB_CHECK_NEAR(AtContact.PerShot.Speed, A.Strike.Speed - S.Intended.Speed, 1e-15);
	RB_CHECK(SameBits(AtContact.Warp.AzimuthError, A.Channels.Warp.AzimuthError));
	// Before the final stroke: no per-shot offsets are shown (drift, tremor and warp are).
	const HandPose Early = Sample(S, 1.0);
	RB_CHECK(Early.Ramp == 0.0 && !Early.RampShown);
	RB_CHECK(Early.PerShot.AxisA == 0.0 && Early.PerShot.AxisB == 0.0 && Early.PerShot.Elevation == 0.0 && Early.PerShot.Speed == 0.0);
	RB_CHECK(Early.GripLateral != 0.0 && Early.Warp.AzimuthError == A.Channels.Warp.AzimuthError);
	// Half way through the ramp: SmoothStep01(0.5) = 0.5 of the per-shot offsets.
	const HandPose Mid = Sample(S, S.Intended.ForwardStart + 0.05);
	RB_CHECK_NEAR(Mid.Ramp, 0.5, 1e-12);
	RB_CHECK_NEAR(Mid.PerShot.AxisA, 0.5 * A.Channels.Sources[static_cast<int>(StrokeSource::TipPlacement)].AxisA, 1e-15);
	// A stale history (after the post-shot AdvanceNoiseHistory) and an empty one give the same stroke (pure function of the key).
	Setup Stale = S;
	AdvanceNoiseHistory(Stale.History);
	RB_CHECK(StrokeHash(Execute(Stale)) == StrokeHash(A));
	Stale.History = NoiseHistory{};
	RB_CHECK(StrokeHash(Execute(Stale)) == StrokeHash(A));
	RB_CHECK(SameBits(Sample(Stale, S.Intended.TimeDown).AxisOffsetB, A.AxisOffset.y));
	// Invalid input: InvalidArgument, zero strike.
	Setup Bad = S;
	Bad.Intended.Speed = std::nan("");
	const ExecutedStroke Rejected = Execute(Bad);
	RB_CHECK(Rejected.Error == ErrorCode::InvalidArgument && Rejected.Strike.Speed == 0.0 && Rejected.Strike.Azimuth == 0.0);
}

// A-HUM-3: product-owner defaults, the Q2 intoxication hook (exactly neutral at 0), money-game stakes.
RB_TEST(ARCH_HUM3_ProductConfigAndIntoxicationHook)
{
	const ProductConfig Config;
	RB_CHECK(Config.Alcohol == AlcoholMode::CosmeticOnly);                  // Q2
	RB_CHECK(Config.Attributes == AttributeVisibility::Hidden);             // Q3
	RB_CHECK(Config.HotSeatGuestAttribute == 50.0 && !Config.HotSeatGuestEarnsXp); // Q4
	RB_CHECK(Config.FirstVisitChores == ChoreSpeed::Full && Config.ReturnVisitChores == ChoreSpeed::Brisk); // Q5
	RB_CHECK(Config.Money == MoneyGames::SideBetsAndHustling);             // Q6
	RB_CHECK(Config.LowDeflectionMinSteadiness == 0.0);                    // Q7
	RB_CHECK(HumanParams{}.StreakGuard);                                   // Q1
	for (const double Level : {0.0, 0.2, 0.8, 1.0})
	{
		RB_CHECK(StrokeIntoxication(Config, Level) == 0.0);
	}

	// I = 0 (V1): every factor exactly 1, the stroke bitwise the one without the hook (HF-T05 / T06 values above).
	Setup S = MakeS0();
	S.Situation.Pressure = 0.8;
	const SituationFactors Sober = ComputeSituationFactors(S.Intended, S.Attributes, S.Situation, S.Params, 2.5, kR);
	RB_CHECK(Sober.IntoxicationDrift == 1.0 && Sober.IntoxicationTremor == 1.0 && Sober.Pressure == 0.8);
	Setup Zero = S;
	Zero.Situation.Intoxication = StrokeIntoxication(Config, 0.9);
	RB_CHECK(StrokeHash(Execute(Zero)) == StrokeHash(Execute(S)));
	RB_CHECK(SameBits(Sample(Zero, 2.0).AxisOffsetA, Sample(S, 2.0).AxisOffsetA));

	// Mechanic (the later hook): one drink calms (lower pressure gain, drift / tremor unchanged), more drinks add drift and tremor.
	ProductConfig Drunk = Config;
	Drunk.Alcohol = AlcoholMode::Mechanic;
	Setup OneDrink = S;
	OneDrink.Situation.Intoxication = StrokeIntoxication(Drunk, 0.2);
	const SituationFactors Calm = ComputeSituationFactors(OneDrink.Intended, OneDrink.Attributes, OneDrink.Situation, OneDrink.Params, 2.5, kR);
	RB_CHECK(Calm.PressureGain < Sober.PressureGain && Calm.Pressure < Sober.Pressure);
	RB_CHECK(Calm.IntoxicationDrift == 1.0 && Calm.IntoxicationTremor == 1.0);
	Setup Many = S;
	Many.Situation.Intoxication = StrokeIntoxication(Drunk, 0.8);
	const SituationFactors Wobbly = ComputeSituationFactors(Many.Intended, Many.Attributes, Many.Situation, Many.Params, 2.5, kR);
	RB_CHECK(Wobbly.IntoxicationDrift > 1.0 && Wobbly.IntoxicationTremor > 1.0);
	RB_CHECK_NEAR(Wobbly.IntoxicationDrift, 1.0 + (0.8 - 0.25) / 0.75, 1e-12);
	RB_CHECK_NEAR(Wobbly.Pressure, 0.8 * 0.5, 1e-15);
	RB_CHECK(std::fabs(Execute(Many).Channels.Sources[static_cast<int>(StrokeSource::Tremor)].AxisA) >
		std::fabs(Execute(OneDrink).Channels.Sources[static_cast<int>(StrokeSource::Tremor)].AxisA));

	// Money-game stakes (Q6): 0.6 for a small bet up to 1 at half the cash or more.
	RB_CHECK(MoneyGameStakes(5.0, 100.0) == 0.6 && MoneyGameStakes(10.0, 100.0) == 0.6);
	RB_CHECK_NEAR(MoneyGameStakes(30.0, 100.0), 0.8, 1e-12);
	RB_CHECK(MoneyGameStakes(50.0, 100.0) == 1.0 && MoneyGameStakes(80.0, 100.0) == 1.0 && MoneyGameStakes(5.0, 0.0) == 1.0);
	PressureInputs Money;
	Money.Stakes = MoneyGameStakes(50.0, 100.0);
	RB_CHECK_NEAR(ComputePressure(Money, PressureMode::On), 0.35, 1e-15);
	RB_CHECK_NEAR(ComputePressure(Money, PressureMode::Subtle), 0.175, 1e-15);
	RB_CHECK(ComputePressure(Money, PressureMode::Off) == 0.0);
	PressureInputs Final;
	Final.Stakes = kStakesFinal;
	Final.GameBall = true;
	Final.Hill = true;
	Final.Watchers = 25;
	Final.ShotClockFraction = 1.0;
	Final.RunLength = 12;
	RB_CHECK_NEAR(ComputePressure(Final, PressureMode::On), 1.0, 1e-15);
	RB_CHECK(ComputePressure(Final, PressureMode::On) <= 1.0);
	Final.Watchers = 1000;
	Final.Stakes = 7.0; // out-of-range inputs are clamped
	RB_CHECK(ComputePressure(Final, PressureMode::On) <= 1.0);
	PressureInputs League;
	League.Stakes = kStakesMoneyOrLeague;
	League.GameBall = true;
	League.Watchers = 5;
	League.RunLength = 4;
	RB_CHECK_NEAR(ComputePressure(League, PressureMode::On), 0.35 * 0.6 + 0.25 + 0.1 * 0.5 + 0.05 * 0.5, 1e-15);
	RB_CHECK(FatigueFromNight(1.5, true) == 0.0 && FatigueFromNight(3.0, true) == 0.5 && FatigueFromNight(5.0, true) == 1.0);
	RB_CHECK(FatigueFromNight(5.0, false) == 0.0);
}

// HF-B01 (core half): the same input log, keys and state give a bitwise identical executed stroke; the hash of a canonical
// set is pinned for the reference platform (MSVC x64, UCRT), so the Debug and the Release build must both produce it.
RB_TEST(HF_B01_BitwiseReproducibleStroke)
{
	std::uint64_t Hash = 0;
	for (std::uint32_t i = 0; i < 64u; ++i)
	{
		Setup S = MakeS0();
		S.Attributes = UniformAttributes(10.0 + i);
		S.Situation.Pressure = (i % 5u) * 0.25;
		S.Situation.Bridge = static_cast<BridgeType>(i % 5u);
		S.Intended.AxisOffsetA = 0.01 * (static_cast<double>(i % 7u) - 3.0);
		S.Intended.AxisOffsetB = -0.02 * static_cast<double>(i % 4u);
		S.Intended.Speed = 0.5 + 0.1 * i;
		S.Intended.TimeDown = 1.0 + 0.07 * i;
		S.Intended.SettleStart = (i % 3u == 0u) ? S.Intended.TimeDown - 1.5 : -1.0;
		S.CueBody.BowSag = (i % 4u == 1u) ? 2e-3 : 0.0;
		S.CueBody.WarpKnown = (i % 8u) != 1u;
		S.Key = MakeKey(0xB01u, i / 8u, i, 1u + (i & 1u), i / 2u);
		S.History = RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), S.Key.ShooterShotIndex);
		const ExecutedStroke X = Execute(S);
		// A replay rebuilds everything from the stored log: copies of every input, a history rebuilt from the key.
		const Setup Replay = S;
		const ExecutedStroke Y = Execute(Replay);
		RB_CHECK(StrokeHash(X) == StrokeHash(Y) && SameStrike(X.Strike, Y.Strike));
		Hash = HashKeys(Hash, StrokeHash(X));
		const HandPose P = Sample(S, S.Intended.TimeDown - 0.35);
		Hash = HashKeys(Hash, Bits(P.Azimuth), Bits(P.Elevation), Bits(P.AxisOffsetA), Bits(P.AxisOffsetB), Bits(P.PerShot.Speed));
	}
	std::printf("  HF_B01 canonical stroke hash 0x%016llX\n", static_cast<unsigned long long>(Hash));
#if defined(_MSC_VER) && defined(_M_X64)
	RB_CHECK(Hash == 0x4B12CD9FD78F022Cull);
#endif
}

// HF-B08 (core half): at attribute 25 every channel at 1 sigma moves the rendered cue by >= 1 px at 1440p in the Eyes
// preset (1544 px/rad, eye 0.46 m from the tip, 1 px at the tip = 0.30 mm; 3.7), except tremor and lateral tip placement,
// which stay below a pixel (their tells are audio / replay). SampleHand's pose moves by eps x sigma of each channel.
RB_TEST(HF_B08_OneSigmaVisibleInThePose)
{
	constexpr double kPxPerRad = 1544.0;
	constexpr double kEyeToTip = 0.46;
	constexpr double kTipMmPerPx = 0.30e-3;
	constexpr double kFrame = 1.0 / 60.0;
	Setup S = MakeS0();
	S.Situation.Pressure = 1.0; // channel 9 (flinch, grip tension) acts only under pressure
	const SituationFactors F = ComputeSituationFactors(S.Intended, S.Attributes, S.Situation, S.Params, S.Intended.TimeDown, kR);
	Setup Calm = MakeS0();
	const SituationFactors F0 = ComputeSituationFactors(Calm.Intended, Calm.Attributes, Calm.Situation, Calm.Params, Calm.Intended.TimeDown, kR);

	// The pose follows eps x sigma (the rendered delta of each per-shot channel is its draw times its sigma).
	const ExecutedStroke X = Execute(S);
	const HandPose Full = Sample(S, S.Intended.TimeDown);
	RB_CHECK_NEAR(Full.PerShot.Elevation, X.Channels.Elevation.Eps * F.ElevationSigma, 1e-15);
	RB_CHECK_NEAR(Full.PerShot.AxisA * kR, X.Channels.TipA.Eps * F.TipASigma, 1e-15);
	RB_CHECK_NEAR(Full.PerShot.AxisB * kR, X.Channels.TipB.Eps * F.TipBSigma + F.GripBias, 1e-15); // tip placement + grip tension
	const double Flinch = S.Params.FlinchLoss * F.NerveScale * X.Channels.Flinch.U;
	RB_CHECK_NEAR(Full.PerShot.Speed, S.Intended.Speed * ((1.0 + X.Channels.Speed.Eps * F.SpeedSigma) * (1.0 - Flinch) - 1.0), 1e-14);
	RB_CHECK_NEAR(Full.GripLateral, F.DriftSigma * X.Channels.DriftLat, 1e-15);        // the watchable drift, also before the ramp
	RB_CHECK_NEAR(Sample(S, 1.0).GripLateral,
		ComputeSituationFactors(S.Intended, S.Attributes, S.Situation, S.Params, 1.0, kR).DriftSigma *
			ProcessValue(MakeWatchableProcess(S.Key, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi), 1.0), 1e-15);

	// Vertical tip placement: 1.5 mm, 5 px.
	const double TipBPx = F0.TipBSigma / kTipMmPerPx;
	RB_CHECK(TipBPx >= 1.0 && TipBPx > 4.9);
	// Elevation 0.4 deg moves the tip about the bridge by sTh L_b = 1.4 mm, 5 px.
	const double ElevationPx = F0.ElevationSigma * S.Situation.BridgeLength / kTipMmPerPx;
	RB_CHECK(ElevationPx >= 1.0 && ElevationPx > 4.5);
	// Drift, seen on the shaft 8 cm under the eye: the shaft point under the eye moves by yaw (s_eye - L_b), 5 px.
	const double Yaw = F0.DriftSigma / S.Situation.BridgeToGrip;
	const double DriftPx = Yaw * (kEyeToTip - S.Situation.BridgeLength) / 0.08 * kPxPerRad;
	RB_CHECK(DriftPx >= 1.0 && DriftPx > 5.0);
	// Speed 5 % at 2 m/s: 6 px per frame.
	const double SpeedPx = F0.SpeedSigma * S.Intended.Speed * kFrame / kTipMmPerPx;
	RB_CHECK(SpeedPx >= 1.0 && SpeedPx > 5.5);
	// Grip tension (channel 9) at P = 1: a 1.5 mm tip drop, 5 px; flinch 8 % speed loss, 9 px per frame at 2 m/s.
	RB_CHECK(std::fabs(F.GripBias) / kTipMmPerPx >= 1.0);
	RB_CHECK(S.Params.FlinchLoss * F.NerveScale * S.Intended.Speed * kFrame / kTipMmPerPx >= 1.0);
	// The two sub-pixel channels: lateral tip placement 0.20 mm (0.7 px) and tremor 0.03 mm (0.1 px at rest, 0.5 px at g = 5).
	RB_CHECK(F0.TipASigma / kTipMmPerPx < 1.0);
	RB_CHECK(F0.TremorSigma / kTipMmPerPx < 1.0);
	RB_CHECK(F.TremorSigma / kTipMmPerPx < 1.0);
}

// HF-B13: a stroke that showed part of the per-shot ramp and stops without contact spends its draws.
RB_TEST(HF_B13_AbortedStrokeSpendsTheDraw)
{
	Setup S = MakeS0();
	S.Key.ShooterShotIndex = 3u;
	S.History = RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), 3u);
	// Attempt 1: the forward stroke starts at 2.2 s and is aborted at 2.25 s (half of the ramp shown).
	const HandPose Aborted = Sample(S, 2.25);
	RB_CHECK(Aborted.RampShown && Aborted.Ramp > 0.0 && Aborted.Ramp < 1.0);
	const HandPose NotYet = Sample(S, 2.19);
	RB_CHECK(!NotYet.RampShown);
	// The game advances the shooter's per-shot draws: ShooterShotIndex + 1 and AdvanceNoiseHistory.
	Setup Retry = S;
	Retry.Key.ShooterShotIndex = 4u;
	Retry.Key.AddressIndex = 1u; // the retry is a new get-down
	AdvanceNoiseHistory(Retry.History);
	RB_CHECK(HistoryMatchesKey(Retry.History, Retry.Key));
	const ExecutedStroke Hit = Execute(Retry);
	const ExecutedStroke WouldHaveBeen = Execute(S);
	RB_CHECK(Hit.Channels.TipB.U != WouldHaveBeen.Channels.TipB.U && Hit.Channels.Speed.U != WouldHaveBeen.Channels.Speed.U);
	RB_CHECK(Hit.Channels.DriftLat != WouldHaveBeen.Channels.DriftLat); // new get-down, new drift process
	// The replay of the input log reproduces both attempts (the history rebuilt from the keys).
	Setup ReplayAbort = S;
	ReplayAbort.History = NoiseHistory{};
	RB_CHECK(SameBits(Sample(ReplayAbort, 2.25).AxisOffsetB, Aborted.AxisOffsetB));
	Setup ReplayHit = Retry;
	ReplayHit.History = NoiseHistory{};
	RB_CHECK(StrokeHash(Execute(ReplayHit)) == StrokeHash(Hit));
}

// Cost of the stroke model (opt-in, Release): ExecuteStroke per shot, SampleHand per rendered frame, AdvanceNoiseHistory per
// revealed draw, and the rebuild of a history that does not match the key.
RB_TEST(Human_Slow_StrokeModelTiming)
{
	Setup S = MakeS0();
	BallObstacle Others[15];
	for (int i = 0; i < 15; ++i)
	{
		Others[i].Id = static_cast<BallId>(i + 1);
		Others[i].Position = {0.3 + 0.06 * (i % 5), -0.2 + 0.1 * (i / 5), kR};
	}
	constexpr int kCalls = 100000;
	NoiseHistory History = S.History;
	double Sink = 0.0;
	const auto T0 = std::chrono::steady_clock::now();
	for (int i = 0; i < kCalls; ++i)
	{
		S.Key.ShooterShotIndex = static_cast<std::uint32_t>(i);
		S.History = History;
		Sink += Execute(S, Others, 15).Strike.Speed;
		AdvanceNoiseHistory(History);
	}
	const auto T1 = std::chrono::steady_clock::now();
	for (int i = 0; i < kCalls; ++i)
	{
		Sink += Sample(S, 2.0 + 1e-5 * i).AxisOffsetA;
	}
	const auto T2 = std::chrono::steady_clock::now();
	S.Key.ShooterShotIndex = 1000u;
	S.History = NoiseHistory{};
	for (int i = 0; i < 100; ++i)
	{
		Sink += Execute(S).Strike.Speed;
	}
	const auto T3 = std::chrono::steady_clock::now();
	const auto Us = [](auto A, auto B, int N) { return std::chrono::duration<double, std::micro>(B - A).count() / N; };
	std::printf("  ExecuteStroke + AdvanceNoiseHistory %.3f us, SampleHand %.3f us, ExecuteStroke with a stale history at draw 1000 %.1f us\n",
		Us(T0, T1, kCalls), Us(T1, T2, kCalls), Us(T2, T3, 100));
	RB_CHECK(Sink > 0.0);
}

// 3.6 flags of the executed pose (the scoop and shaft tests need WP-1's cue frame and contact point).
RB_TEST(Integ_Human_ExecutedPoseScoopAndShaftContacts)
{
	Setup S = MakeS0();
	S.Params.NoiseScale = 0.0;
	RB_CHECK(!Execute(S).Strike.TipTouchesCloth);
	// Scoop: a low cue far below centre (b = -0.85 at 10 deg) digs the tip rim into the cloth; a moderate draw does not.
	Setup Scoop = S;
	Scoop.Intended.Elevation = 10.0 * kDegToRad;
	Scoop.Intended.AxisOffsetB = -0.85 * (kR + Scoop.Tip.DomeRadius) / kR;
	const ExecutedStroke Low = Execute(Scoop);
	RB_CHECK_NEAR(Low.Strike.OffsetB, -0.85, 1e-12);
	RB_CHECK(Low.Strike.TipTouchesCloth);
	Scoop.Intended.AxisOffsetB = -0.3 * (kR + Scoop.Tip.DomeRadius) / kR;
	RB_CHECK(!Execute(Scoop).Strike.TipTouchesCloth);

	// Shaft clearance (tapered cue, no margin): a ball under the shaft, a ball beside the cue, a ball ahead.
	Setup Over = S;
	Over.Intended.Elevation = 5.0 * kDegToRad;
	BallObstacle Balls[3];
	Balls[0].Id = 4;
	Balls[0].Position = {-0.30, 0.0, kR};
	Balls[1].Id = 7;
	Balls[1].Position = {-0.30, 0.20, kR};
	Balls[2].Id = 9;
	Balls[2].Position = {1.0, 0.0, kR};
	const ExecutedStroke Shaft = Execute(Over, Balls, 3);
	RB_REQUIRE(Shaft.ShaftContactCandidates.Size() == 1);
	RB_CHECK(Shaft.ShaftContactCandidates[0].Ball == 4 && Shaft.ShaftContactCandidates[0].Source == NonTipSource::Shaft);
	Setup Level = S;
	Level.Intended.Elevation = 1.0 * kDegToRad;
	BallObstacle Butt;
	Butt.Id = 2;
	Butt.Position = {-1.0, 0.0, kR};
	const ExecutedStroke UnderButt = Execute(Level, &Butt, 1);
	RB_REQUIRE(UnderButt.ShaftContactCandidates.Size() == 1);
	RB_CHECK(UnderButt.ShaftContactCandidates[0].Source == NonTipSource::Butt);
	// The elevation floor set by a ball: the cue rests on it.
	Setup Floor = S;
	Floor.Intended.Elevation = 2.0 * kDegToRad;
	Floor.Situation.ElevationFloor = 5.0 * kDegToRad;
	Floor.Situation.FloorBy = FloorSource::Ball;
	Floor.Situation.FloorBall = 11;
	const ExecutedStroke Floored = Execute(Floor);
	RB_CHECK(Floored.ElevationClamped && Floored.Strike.Elevation == 5.0 * kDegToRad);
	RB_REQUIRE(Floored.ShaftContactCandidates.Size() == 1);
	RB_CHECK(Floored.ShaftContactCandidates[0].Ball == 11 && Floored.ShaftContactCandidates[0].Source == NonTipSource::Shaft);
	Floor.Situation.FloorBy = FloorSource::Rail;
	RB_CHECK(Execute(Floor).ShaftContactCandidates.IsEmpty());
}

RB_TEST(Human_DoubleHitAndPushRisk)
{
	Setup S = MakeS0();
	S.Params.NoiseScale = 0.0;
	S.Intended.Elevation = 0.0;
	BallObstacle Ball;
	Ball.Id = 1;
	// 3 mm straight ahead: inside the frozen envelope but not frozen -> push and double-hit risk.
	Ball.Position = {2.0 * kR + 3e-3, 0.0, kR};
	const ExecutedStroke Near = Execute(S, &Ball, 1);
	RB_CHECK(Near.PushRisk && Near.DoubleHitRisk);
	// Frozen (0.05 mm): the F7 / F8 envelope exempts it.
	Ball.Position = {2.0 * kR + 0.05e-3, 0.0, kR};
	const ExecutedStroke Frozen = Execute(S, &Ball, 1);
	RB_CHECK(!Frozen.PushRisk && !Frozen.DoubleHitRisk);
	// 5 cm: beyond the envelope, within the follow-through: double-hit risk only.
	Ball.Position = {2.0 * kR + 0.05, 0.0, kR};
	const ExecutedStroke Follow = Execute(S, &Ball, 1);
	RB_CHECK(!Follow.PushRisk && Follow.DoubleHitRisk);
	// 0.5 m: nothing.
	Ball.Position = {0.5, 0.0, kR};
	const ExecutedStroke Far = Execute(S, &Ball, 1);
	RB_CHECK(!Far.PushRisk && !Far.DoubleHitRisk);
	// A graze (cut ~80 deg) 3 mm away: push risk from the gap, no double hit (phi_c >= GrazeAngle).
	const double Cut = 80.0 * kDegToRad;
	const double Distance = 2.0 * kR + 3e-3;
	Ball.Position = {Distance * std::cos(Cut), (2.0 * kR) * 0.999, kR};
	const ExecutedStroke Graze = Execute(S, &Ball, 1);
	RB_CHECK(Graze.PushRisk && !Graze.DoubleHitRisk);
	// Not in the corridor at all.
	Ball.Position = {0.0, 0.2, kR};
	RB_CHECK(!Execute(S, &Ball, 1).PushRisk);
}
