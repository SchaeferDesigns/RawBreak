// Owner: WP-11 (player model). Adversarial tests of the review (not spec IDs): a differential check of ExecuteStroke against
// the verifier's oracle over situations the spec tests do not cover, the ritual cap of principle 5 with extra twists, NaN / Inf
// guards, mirror symmetry of the chalk map and the executed stroke, key independence of the per-shot draws, the streak cap of
// every guarded channel, skill scaling, extreme inputs and (MSVC Debug CRT) the absence of heap allocations in the hot paths.
// Expected values of Human_OracleVariedSituations: Tools/reference/human-factors/recompute_v12.py (stroke.execute with the
// streak-guarded draws, drift gain g^(1/3), tip-velocity lever L_b).

#include "Human/HumanTestUtil.h"

#include "rb/Human/AiProfiles.h"
#include "rb/Human/BallMarks.h"
#include "rb/Human/Chores.h"
#include "rb/Human/CueState.h"
#include "rb/Human/Venue.h"

#include <cmath>
#include <limits>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

namespace
{
	struct OracleCase
	{
		// Intended stroke
		double Azimuth, ElevationDeg, A, B, Speed, TimeDown, SettleStart, Pause, Accel;
		bool Head;
		// Attributes St, SC, ST, BS, Sta, N
		double St, SC, ST, BS, Sta, N;
		// Situation
		BridgeType Bridge;
		double Lb, Lbg, Stance, Pressure, Fatigue, Sweat;
		bool Glove, OffHand;
		// Key
		std::uint64_t Seed;
		std::uint32_t Rack, Shot, Shooter, ShooterShot;
		double NoiseScale;
		std::uint32_t Mask;
		// Oracle outputs
		double PhiX, ThetaX, AX, BX, VX, Vr, Vu, SigDrift, SigA, SigB, SigTheta, SigV, Flinch, Gain;
	};

	constexpr std::uint32_t Bit(int Channel) { return 1u << Channel; }

	// Oracle values: recompute_v12.py (see the file header), printed with repr.
	const OracleCase kOracleCases[] = {
		{0.0, 3.0, 0.15, -0.2, 0.6, 0.7, -1.0, 0.05, -4.0, true, 40, 12, 70, 55, 30, 65, BridgeType::Open, 0.20, 0.80, 0.6, 0.45, 0.5, 0.3, false,
			false, 0xC0FFEEu, 3u, 41u, 2u, 17u, 1.0, 0u, -0.005862350845577876, 0.06451936544189793, 0.19507220946153908, -0.20375368026251417,
			0.5972920825137275, -0.004550909490653012, 0.0004233332195484994, 0.003589568586822701, 0.00044111078480685414, 0.0012240850278706346,
			0.0058573673207698174, 0.15090637132777174, 0.005289415953162181, 1.5937785598478023},
		{1.1, 8.0, -0.3, 0.35, 5.5, 4.2, 1.0, 0.3, 0.0, false, 5, 95, 0, 10, 100, 0, BridgeType::Rail, 0.12, 0.60, 0.2, 0.9, 0.0, 0.8, true, false,
			77u, 1u, 9u, 5u, 4u, 0.6, 0u, 1.098853784407223, 0.11964328699173456, -0.2772714329973825, 0.28131274473136375, 5.252264982717601,
			-0.002469370809627232, -0.005118558824291913, 0.0021270501171082173, 0.0009110737371984095, 0.0028455756732166946, 0.025962385699843445,
			0.04654325670319803, 0.06482550971876054, 8.2},
		{-2.0, 25.0, 0.05, -0.5, 8.0, 13.5, 6.0, 0.25, -12.0, true, 85, 60, 45, 100, 0, 100, BridgeType::Elevated, 0.30, 0.90, 1.0, 1.0, 1.0, 1.0,
			false, true, 0xDEADBEEFu, 7u, 200u, 1u, 99u, 1.0, 0u, -2.0220036335415665, 0.5232911979663514, 0.186061551573191, -0.45720373462287156,
			9.244943557457605, -0.004753466019926931, 0.0056621647649610085, 0.017842540954804302, 0.0018335087865327662, 0.009285632955920498,
			0.1058859854782446, 0.15711515077032717, 0.007485654668858988, 1.5},
		{0.5, 2.0, 0.4, 0.1, 2.5, 3.0, 2.0, 0.1, -2.0, false, 25, 25, 25, 25, 25, 25, BridgeType::Mechanical, 0.20, 0.80, 0.3, 0.3, 0.2, 0.1, true,
			false, 12345u, 0u, 3u, 3u, 2u, 1.0, Bit(2) | Bit(4) | Bit(6), 0.5009345417372272, 0.03158179457888624, 0.3767994188388838,
			0.08425196850393701, 2.3236235131713294, 0.00013061362498273193, 0.0, 0.0010361179356043622, 0.0011106993670268231,
			0.0023473850664398055, 0.0169897330706136, 0.09756750729623055, 0.011312476700447684, 2.2},
		{3.0, 1.0, 0.0, 0.2, 0.3, 1.2, -1.0, 0.6, 3.0, false, 100, 0, 100, 0, 50, 10, BridgeType::Closed, 0.20, 0.80, 0.0, 0.2, 0.0, 0.0, false, false,
			1u, 2u, 30u, 9u, 30u, 1.0, Bit(9), 2.999959362223707, 0.019219683210195685, 0.002778480517770562, 0.19969742963705317,
			0.28350788445557346, 0.004230804241887844, -0.0009472542306446062, 0.0002868871303184038, 5e-05, 0.00031200765315132897,
			0.0017453292519943296, 0.1499842020765979, 0.0, 2.2125732532083187},
	};

	Setup SetupFor(const OracleCase& C)
	{
		Setup S = MakeS0();
		S.Intended.Azimuth = C.Azimuth;
		S.Intended.Elevation = C.ElevationDeg * kDegToRad;
		S.Intended.AxisOffsetA = C.A;
		S.Intended.AxisOffsetB = C.B;
		S.Intended.Speed = C.Speed;
		S.Intended.TimeDown = C.TimeDown;
		S.Intended.ForwardStart = C.TimeDown - 0.2;
		S.Intended.SettleStart = C.SettleStart;
		S.Intended.PauseDuration = C.Pause;
		S.Intended.ContactAcceleration = C.Accel;
		S.Intended.HeadMovedBeforeContact = C.Head;
		S.Attributes = {C.St, C.SC, C.ST, C.BS, C.Sta, C.N};
		S.Situation.Bridge = C.Bridge;
		S.Situation.BridgeLength = C.Lb;
		S.Situation.BridgeToGrip = C.Lbg;
		S.Situation.StanceDifficulty = C.Stance;
		S.Situation.Pressure = C.Pressure;
		S.Situation.Fatigue = C.Fatigue;
		S.Situation.Sweat = C.Sweat;
		S.Situation.Glove = C.Glove;
		S.Situation.OffHand = C.OffHand;
		S.Key = MakeKey(C.Seed, C.Rack, C.Shot, C.Shooter, C.ShooterShot);
		S.History = RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), S.Key.ShooterShotIndex);
		S.Params.NoiseScale = C.NoiseScale;
		S.Params.ChannelMask = C.Mask;
		return S;
	}

	void CheckRel(double Value, double Expected)
	{
		if (Expected == 0.0)
		{
			RB_CHECK(Value == 0.0);
			return;
		}
		RB_CHECK_REL(Value, Expected, 1e-9);
	}

	TipState Uniform(double Coverage)
	{
		TipState Tip;
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			Tip.Coverage[z] = Coverage;
		}
		return Tip;
	}

	// Sum of the elevation contributions of every source (the executed change before the floor and the clamps).
	double SourceSumElevation(const ExecutedStroke& X)
	{
		double Sum = 0.0;
		for (int s = 0; s < kStrokeSourceCount; ++s)
		{
			Sum += X.Channels.Sources[s].Elevation;
		}
		return Sum;
	}

	// Ring zone seen in the mirror a -> -a (beta -> pi - beta): 1 <-> 4, 2 <-> 3, 5 <-> 6; the centre disc stays.
	int MirrorZone(int Zone)
	{
		static const int Map[kTipZoneCount] = {0, 4, 3, 2, 1, 6, 5};
		return Map[Zone];
	}

#if defined(_MSC_VER) && defined(_DEBUG)
	int GAllocations = 0;

	int __cdecl CountAllocations(int AllocType, void*, size_t, int, long, const unsigned char*, int)
	{
		if (AllocType == _HOOK_ALLOC || AllocType == _HOOK_REALLOC)
		{
			++GAllocations;
		}
		return 1;
	}
#endif
}

// Differential check against the oracle: bridges, stance, fatigue, sweat, glove, off hand, head lift, rush, jab, soft shot,
// bridge slip, long holds, Settle release, pressure with Nerve, NoiseScale and channel masks (none of these is in HF-T05..T08).
RB_TEST(Human_OracleVariedSituations)
{
	for (const OracleCase& C : kOracleCases)
	{
		const Setup S = SetupFor(C);
		const ExecutedStroke X = Execute(S);
		RB_REQUIRE(X.Error == ErrorCode::Ok);
		CheckRel(X.Strike.Azimuth, C.PhiX);
		CheckRel(X.Strike.Elevation, C.ThetaX);
		CheckRel(X.AxisOffset.x, C.AX);
		CheckRel(X.AxisOffset.y, C.BX);
		CheckRel(X.Strike.Speed, C.VX);
		CheckRel(X.TipTransverseVelocity.x, C.Vr);
		CheckRel(X.TipTransverseVelocity.y, C.Vu);
		const SituationFactors& F = X.Channels.Factors;
		CheckRel(F.DriftSigma, C.SigDrift);
		CheckRel(F.TipASigma, C.SigA);
		CheckRel(F.TipBSigma, C.SigB);
		CheckRel(F.ElevationSigma, C.SigTheta);
		CheckRel(F.SpeedSigma, C.SigV);
		CheckRel(F.Flinch, C.Flinch);
		CheckRel(F.PressureGain, C.Gain);
		// SampleHand with the full ramp renders the same pose.
		const HandPose P = Sample(S, S.Intended.TimeDown);
		RB_CHECK(SameBits(P.Azimuth, X.Strike.Azimuth) && SameBits(P.AxisOffsetA, X.AxisOffset.x) && SameBits(P.AxisOffsetB, X.AxisOffset.y));
	}
}

// Principle 5 / HF-B05: a ritual (R mode) can match or beat the habit, but never the habit-1 result, also when the player
// twists more often than the automatic count or drills the centre with extra twists.
RB_TEST(Human_RitualChalkingCappedAtHabitOne)
{
	const TipParams Params;
	const ChalkCube Own;
	TipState Start = Uniform(0.25);
	Start.Coverage[0] = 0.4;
	const int Planned = AutoChalkTwists(Start, Own, Params);
	RB_REQUIRE(Planned == 5);
	// The habit-1 result: the automatic chalking with H_chalk = 1.
	TipState Habit1 = Start;
	double Duration = 0.0;
	RB_CHECK(PerformChalking(Habit1, Own, ChoreMode::Automatic, 1.0, 0.0, -1, Duration, Params) == Planned);
	// Ritual with more twists than the automatic count and a perfect sweep: capped at the habit-1 result, zone by zone.
	TipState Extra = Start;
	PerformChalking(Extra, Own, ChoreMode::Ritual, 0.3, 1.0, 9, Duration, Params);
	// Ritual with extra twists and a mediocre sweep: the centre (eta_0 does not depend on the sweep) must not beat it either.
	TipState ExtraCentre = Start;
	PerformChalking(ExtraCentre, Own, ChoreMode::Ritual, 0.3, 0.5, 12, Duration, Params);
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		RB_CHECK(Extra.Coverage[z] <= Habit1.Coverage[z]);
		RB_CHECK(ExtraCentre.Coverage[z] <= Habit1.Coverage[z]);
	}
	RB_CHECK(SameBits(Extra.Coverage[1], Habit1.Coverage[1]));
	// The planned twists with a perfect sweep match the habit-1 result bit for bit; fewer twists or a worse sweep stay below.
	TipState Match = Start;
	PerformChalking(Match, Own, ChoreMode::Ritual, 0.0, 1.0, Planned, Duration, Params);
	TipState Fewer = Start;
	PerformChalking(Fewer, Own, ChoreMode::Ritual, 0.0, 1.0, 2, Duration, Params);
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		RB_CHECK(SameBits(Match.Coverage[z], Habit1.Coverage[z]));
		RB_CHECK(Fewer.Coverage[z] < Habit1.Coverage[z]);
	}
	// A ritual still beats a low habit (it pays off while the habit is being learned).
	TipState Habit0 = Start;
	PerformChalking(Habit0, Own, ChoreMode::Automatic, 0.0, 0.0, -1, Duration, Params);
	RB_CHECK(Match.Coverage[3] > Habit0.Coverage[3]);
	// Nothing missing: the ritual cannot add coverage beyond the (unchanged) habit-1 result.
	TipState Full;
	PerformChalking(Full, Own, ChoreMode::Ritual, 0.0, 1.0, 4, Duration, Params);
	RB_CHECK(Full.Coverage[0] == 1.0 && Full.Coverage[4] == 1.0);
	TipState BarFull = Uniform(0.7);
	PerformChalking(BarFull, BarChalkCube(), ChoreMode::Ritual, 0.0, 1.0, 4, Duration, Params);
	RB_CHECK(BarFull.Coverage[2] == 0.7);
}

// ExecuteStroke promises InvalidArgument (and a zero strike) for non-finite inputs; the equipment state and the parameters are
// inputs too (a corrupted save or replay header must not produce a NaN strike). SampleHand returns the neutral pose.
RB_TEST(Human_NonFiniteEquipmentAndParamsRejected)
{
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	const auto Rejected = [](const Setup& S) {
		const ExecutedStroke X = Execute(S);
		const HandPose P = Sample(S, S.Intended.TimeDown);
		const bool StrokeRejected = X.Error == ErrorCode::InvalidArgument && X.Strike.Speed == 0.0 && X.Strike.Azimuth == 0.0 &&
			X.Strike.Elevation == 0.0 && X.Strike.OffsetA == 0.0 && X.Strike.OffsetB == 0.0 && X.Strike.Cue.Mass == S.Cue.Mass;
		const bool PoseNeutral = P.Time == S.Intended.TimeDown && P.Azimuth == 0.0 && P.Elevation == 0.0 && P.AxisOffsetA == 0.0 &&
			P.AxisOffsetB == 0.0 && P.GripLateral == 0.0 && !P.RampShown;
		return StrokeRejected && PoseNeutral;
	};
	Setup S = MakeS0();
	S.CueBody.WarpKnown = false;
	RB_REQUIRE(Execute(S).Error == ErrorCode::Ok);
	{
		Setup Bad = S;
		Bad.CueBody.BowSag = NaN;
		RB_CHECK(Rejected(Bad));
	}
	{
		Setup Bad = S;
		Bad.CueBody.BowSag = 1e-3;
		Bad.Params.WarpSightLength = Inf;
		RB_CHECK(Rejected(Bad));
	}
	{
		Setup Bad = S;
		Bad.Params.DriftSigma = NaN;
		RB_CHECK(Rejected(Bad));
	}
	{
		Setup Bad = S;
		Bad.Params.PressureGainMax = Inf;
		Bad.Situation.Pressure = 1.0;
		RB_CHECK(Rejected(Bad));
	}
	{
		Setup Bad = S;
		Bad.Tip.Coverage[0] = NaN;
		RB_CHECK(Execute(Bad).Error == ErrorCode::InvalidArgument);
	}
	{
		Setup Bad = S;
		Bad.Tip.Restitution = NaN;
		RB_CHECK(Execute(Bad).Error == ErrorCode::InvalidArgument);
	}
	{
		Setup Bad = S;
		Bad.Intended.TipVelocityRight = Inf;
		RB_CHECK(Rejected(Bad));
	}
	// SampleHand at a non-finite time: the neutral pose.
	const HandPose AtNaN = Sample(S, NaN);
	RB_CHECK(AtNaN.Azimuth == 0.0 && !AtNaN.RampShown);
}

// The chalk map is mirror symmetric (a -> -a maps ring sector k onto its mirror), its weights always sum to 1, and the
// coverage is continuous across the beta = +-pi seam and the sector borders.
RB_TEST(Human_TipLookupMirrorAndContinuity)
{
	const TipParams Params;
	TipState Tip;
	const double Coverage[kTipZoneCount] = {0.9, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
	TipState Mirror;
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		Tip.Coverage[z] = Coverage[z];
		Mirror.Coverage[MirrorZone(z)] = Coverage[z];
	}
	double Previous = LookupTipContact(Tip, 0.45, 0.0, Params).Coverage;
	double MaxJump = 0.0;
	for (int i = 0; i <= 3600; ++i)
	{
		const double Angle = kTwoPi * i / 3600.0;
		for (const double Rho : {0.05, 0.2, 0.35, 0.45, 0.6})
		{
			const double A = Rho * std::cos(Angle);
			const double B = Rho * std::sin(Angle);
			const TipContactPoint C = LookupTipContact(Tip, A, B, Params);
			const TipContactPoint M = LookupTipContact(Mirror, -A, B, Params);
			double Sum = 0.0;
			for (int z = 0; z < kTipZoneCount; ++z)
			{
				RB_CHECK(C.Weights[z] >= 0.0);
				Sum += C.Weights[z];
				RB_CHECK_NEAR(C.Weights[z], M.Weights[MirrorZone(z)], 1e-12);
			}
			RB_CHECK_NEAR(Sum, 1.0, 1e-15);
			RB_CHECK_NEAR(C.Coverage, M.Coverage, 1e-12);
			RB_CHECK(C.Zone == TipZone::Dome && M.Zone == TipZone::Dome);
			RB_CHECK_NEAR(C.Friction, Params.BareFriction + (Params.FreshFriction - Params.BareFriction) * C.Coverage, 1e-15);
			RB_CHECK(C.Friction >= Params.BareFriction && C.Friction <= Params.FreshFriction);
		}
		const double Now = LookupTipContact(Tip, 0.45 * std::cos(Angle), 0.45 * std::sin(Angle), Params).Coverage;
		MaxJump = std::fmax(MaxJump, std::fabs(Now - Previous));
		Previous = Now;
	}
	RB_CHECK(MaxJump < 0.01); // 0.1 deg steps: no seam, no jump at the sector borders
	// Signed zeros on the seam (beta = +pi or -pi) give the same sector.
	const TipContactPoint PlusZero = LookupTipContact(Tip, 0.45, 0.0, Params);
	const TipContactPoint MinusZero = LookupTipContact(Tip, 0.45, -0.0, Params);
	RB_CHECK(PlusZero.Coverage == MinusZero.Coverage && PlusZero.Weights[4] == 1.0 && MinusZero.Weights[4] == 1.0);
}

// With the human layer off, a mirrored intended stroke (phi -> -phi, A -> -A) gives the mirrored executed stroke, and the
// warp of a bow held up (WarpKnown) is pure elevation, the same for both.
RB_TEST(Human_MirroredStroke)
{
	Setup S = MakeS0();
	S.Params.NoiseScale = 0.0;
	S.Intended.Azimuth = 0.37;
	S.Intended.AxisOffsetA = 0.31;
	S.Intended.AxisOffsetB = -0.22;
	S.Intended.TipVelocityRight = 0.04;
	S.CueBody.BowSag = 2e-3;
	S.CueBody.WarpKnown = true;
	Setup M = S;
	M.Intended.Azimuth = -0.37;
	M.Intended.AxisOffsetA = -0.31;
	M.Intended.TipVelocityRight = -0.04;
	const ExecutedStroke X = Execute(S);
	const ExecutedStroke Y = Execute(M);
	RB_REQUIRE(X.Error == ErrorCode::Ok && Y.Error == ErrorCode::Ok);
	RB_CHECK(X.Strike.Azimuth == -Y.Strike.Azimuth && X.AxisOffset.x == -Y.AxisOffset.x && X.AxisOffset.y == Y.AxisOffset.y);
	RB_CHECK(X.Strike.Elevation == Y.Strike.Elevation && X.Strike.Speed == Y.Strike.Speed);
	RB_CHECK(X.TipTransverseVelocity.x == -Y.TipTransverseVelocity.x);
	RB_CHECK(X.Contact.Coverage == Y.Contact.Coverage && X.Strike.Cue.TipFriction == Y.Strike.Cue.TipFriction);
	RB_CHECK(X.DoubleHitRisk == Y.DoubleHitRisk && X.PushRisk == Y.PushRisk);
	// Every per-shot draw is odd in eps: flipping a source's sign flips its contribution.
	Setup Noisy = MakeS0();
	const ExecutedStroke N = Execute(Noisy);
	const double Sigma = N.Channels.Factors.TipASigma;
	RB_CHECK_NEAR(N.Channels.Sources[static_cast<int>(StrokeSource::TipPlacement)].AxisA * kR, N.Channels.TipA.Eps * Sigma, 1e-18);
	RB_CHECK_NEAR(TruncNormal(1.0 - N.Channels.TipA.U), -N.Channels.TipA.Eps, 1e-9); // TruncNormal(1 - u) = -TruncNormal(u)
}

// The per-shot draws are a pure function of (MatchSeed, shooter key, channel, ShooterShotIndex): the rack, the shot index,
// the pickup and the address never change them; the watchable processes do depend on rack, shot and address (3.2).
RB_TEST(Human_PerShotDrawsDependOnlyOnTheirKeys)
{
	const NoiseKey Base = MakeKey(0x5EEDu, 2u, 30u, 4u, 12u);
	const NoiseHistory History = RebuildNoiseHistory(Base.MatchSeed, ShooterKey(Base), 12u);
	NoiseKey Other = Base;
	Other.RackIndex = 9u;
	Other.ShotIndex = 1000u;
	Other.CuePickupIndex = 3u;
	Other.AddressIndex = 2u;
	for (int Slot = 0; Slot < kStreakChannelCount; ++Slot)
	{
		const NoiseChannel Channel = StreakChannelAt(Slot);
		RB_CHECK(SameBits(DrawPerShot(Base, Channel, History, true).U, DrawPerShot(Other, Channel, History, true).U));
		RB_CHECK(SameBits(DrawPerShot(Base, Channel, History, false).U, DrawPerShot(Other, Channel, History, false).U));
	}
	NoiseKey Shifted = Base;
	Shifted.ShooterShotIndex = 13u;
	RB_CHECK(DrawPerShot(Base, NoiseChannel::TipB, History, true).U != DrawPerShot(Shifted, NoiseChannel::TipB, History, true).U);
	NoiseKey OtherShooter = Base;
	OtherShooter.ShooterId = 5u;
	RB_CHECK(DrawPerShot(Base, NoiseChannel::TipB, History, true).U != DrawPerShot(OtherShooter, NoiseChannel::TipB, History, true).U);
	for (const NoiseKey& Changed : {MakeKey(0x5EEDu, 3u, 30u, 4u, 12u), MakeKey(0x5EEDu, 2u, 31u, 4u, 12u)})
	{
		const WatchableProcess P = MakeWatchableProcess(Base, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
		const WatchableProcess Q = MakeWatchableProcess(Changed, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
		RB_CHECK(P.Phase[0] != Q.Phase[0]);
	}
	// The executed stroke of the same draw index on another shot differs only through the watchable processes.
	Setup A = MakeS0();
	Setup B = A;
	B.Key.ShotIndex = 7u;
	B.Key.RackIndex = 1u;
	const ExecutedStroke XA = Execute(A);
	const ExecutedStroke XB = Execute(B);
	RB_CHECK(SameBits(XA.Channels.TipB.U, XB.Channels.TipB.U) && SameBits(XA.Channels.Speed.U, XB.Channels.Speed.U));
	RB_CHECK(XA.Channels.DriftLat != XB.Channels.DriftLat);
}

// Q1 cap for every guarded channel (5-9 and the synthetic hand's 20-22) and several match seeds and shooter keys (incl. the
// extreme values): no eighth more than twice in any 8 consecutive draws; the marginal stays uniform (every eighth within
// 12.5 % +- 2 % of the draws).
RB_TEST(Human_StreakCapEveryGuardedChannel)
{
	constexpr int kDraws = 3000;
	static int Eighths[kStreakChannelCount][kDraws];
	for (const std::uint64_t Seed : {std::uint64_t{0x5EED}, std::uint64_t{0xFFFFFFFFFFFFFFFFull}, std::uint64_t{0}})
	{
		for (const std::uint32_t Shooter : {0u, 1u, 0xFFFFFFFFu})
		{
			NoiseKey Key = MakeKey(Seed, 0u, 0u, Shooter, 0u);
			NoiseHistory History = RebuildNoiseHistory(Seed, ShooterKey(Key), 0u);
			int Counts[kStreakChannelCount][8] = {};
			int Worst = 0;
			for (int n = 0; n < kDraws; ++n)
			{
				Key.ShooterShotIndex = static_cast<std::uint32_t>(n);
				for (int Slot = 0; Slot < kStreakChannelCount; ++Slot)
				{
					const GuardedDraw Draw = DrawPerShot(Key, StreakChannelAt(Slot), History, true);
					RB_CHECK(Draw.U >= 0.0 && Draw.U < 1.0 && std::fabs(Draw.Eps) <= 2.5000001);
					Eighths[Slot][n] = EighthOf(Draw.U);
					++Counts[Slot][Eighths[Slot][n]];
					if (n >= 7)
					{
						int Window[8] = {};
						for (int j = n - 7; j <= n; ++j)
						{
							++Window[Eighths[Slot][j]];
						}
						for (int e = 0; e < 8; ++e)
						{
							Worst = Window[e] > Worst ? Window[e] : Worst;
						}
					}
				}
				AdvanceNoiseHistory(History);
			}
			RB_CHECK(Worst <= 2);
			for (int Slot = 0; Slot < kStreakChannelCount; ++Slot)
			{
				for (int e = 0; e < 8; ++e)
				{
					RB_CHECK(std::abs(Counts[Slot][e] - kDraws / 8) <= kDraws / 50);
				}
			}
		}
	}
}

// L(x; rho) shrinks every channel with skill but never to zero (principle 6); weak AIs below 25 get more noise.
RB_TEST(Human_SkillScalingMonotoneNeverZero)
{
	const HumanParams Params;
	Setup S = MakeS0();
	S.Situation.Pressure = 0.5;
	double Previous[7] = {};
	for (int x = -10; x <= 110; ++x)
	{
		S.Attributes = UniformAttributes(static_cast<double>(x));
		const SituationFactors F = ComputeSituationFactors(S.Intended, S.Attributes, S.Situation, Params, 2.5, kR);
		const double Now[7] = {F.DriftSigma, F.TipASigma, F.TipBSigma, F.ElevationSigma, F.SpeedSigma, F.TremorSigma, F.PressureGain - 1.0};
		for (int i = 0; i < 7; ++i)
		{
			RB_CHECK(Now[i] > 0.0 && IsFinite(Now[i]));
			if (x > -10)
			{
				RB_CHECK(Now[i] <= Previous[i]);
			}
			Previous[i] = Now[i];
		}
	}
	RB_CHECK(SkillScale(-5.0, 0.2) == SkillScale(0.0, 0.2) && SkillScale(250.0, 0.2) == SkillScale(100.0, 0.2));
	RB_CHECK_NEAR(SkillScale(100.0, 0.2), 0.2, 1e-15);
	RB_CHECK(SkillScale(25.0, 0.2) == 1.0 && SkillScale(10.0, 0.2) > 1.0);
}

// Extreme but finite inputs stay finite and inside the documented ranges: speed clamp, rho clamp, huge / zero times, a vertical
// cue, a zero bridge length, attributes outside [0, 100], an off-centre aim far past the ferrule.
RB_TEST(Human_ExtremeInputsStayInRange)
{
	struct Variant
	{
		double Speed, TimeDown, ElevationDeg, A, B, Lb, Attribute, Pressure;
	};
	const Variant Variants[] = {
		{100.0, 2.5, 3.0, 0.0, 0.0, 0.2, 25.0, 0.0},   // speed far above the clamp
		{0.0, 2.5, 3.0, 0.0, 0.0, 0.2, 25.0, 0.0},     // no speed
		{2.0, 0.0, 3.0, 0.0, 0.0, 0.2, 25.0, 1.0},     // contact at the moment of getting down
		{2.0, 1e6, 3.0, 0.0, 0.0, 0.2, 25.0, 1.0},     // an endless hold
		{2.0, 2.5, 90.0, 0.0, 0.0, 0.2, 25.0, 0.0},    // vertical cue (masse)
		{2.0, 2.5, 3.0, 5.0, -5.0, 0.2, 25.0, 0.0},    // aim far outside the ball
		{2.0, 2.5, 3.0, 0.3, 0.3, 0.0, -50.0, 1.0},    // zero bridge length, attributes below 0
		{12.0, 2.5, 3.0, 0.0, -0.5, 0.2, 500.0, 1.0},  // attributes above 100, full power
	};
	for (const Variant& V : Variants)
	{
		Setup S = MakeS0();
		S.Intended.Speed = V.Speed;
		S.Intended.TimeDown = V.TimeDown;
		S.Intended.ForwardStart = V.TimeDown - 0.2;
		S.Intended.Elevation = V.ElevationDeg * kDegToRad;
		S.Intended.AxisOffsetA = V.A;
		S.Intended.AxisOffsetB = V.B;
		S.Situation.BridgeLength = V.Lb;
		S.Situation.Pressure = V.Pressure;
		S.Attributes = UniformAttributes(V.Attribute);
		const ExecutedStroke X = Execute(S);
		RB_REQUIRE(X.Error == ErrorCode::Ok);
		RB_CHECK(X.Strike.Speed >= 0.0 && X.Strike.Speed <= S.Params.MaxSpeed);
		RB_CHECK(X.Rho <= S.Params.OffsetClamp);
		RB_CHECK(IsFinite(X.Strike.Azimuth) && IsFinite(X.Strike.Elevation) && IsFinite(X.AxisOffset.x) && IsFinite(X.AxisOffset.y));
		RB_CHECK(IsFinite(X.TipTransverseVelocity.x) && IsFinite(X.TipTransverseVelocity.y));
		RB_CHECK(X.Channels.Factors.SettleIn >= 1.0 && X.Channels.Factors.SettleIn <= 2.5);
		const HandPose P = Sample(S, V.TimeDown);
		RB_CHECK(SameBits(P.Azimuth, X.Strike.Azimuth));
	}
	// The short cue caps the intended speed at 0.8 x 12 m/s before the human layer (HF-32).
	Setup Short = MakeS0();
	Short.Params.NoiseScale = 0.0;
	Short.Situation.ShortCue = true;
	Short.Intended.Speed = 11.0;
	RB_CHECK(Execute(Short).Strike.Speed == 0.8 * 12.0);
	Short.Intended.Speed = 3.0;
	RB_CHECK(Execute(Short).Strike.Speed == 3.0);
}

// MOT B.1 accepts 0 <= theta < pi/2 only. A masse near vertical plus the unintended elevation (and a bow held up) must still
// be a valid strike, and a negative floor must not produce a negative elevation; SampleHand shows the same clamped pose.
RB_TEST(Human_ExecutedElevationStaysInTheStrikeContract)
{
	int Capped = 0;
	for (std::uint32_t i = 0; i < 400u; ++i)
	{
		Setup S = MakeS0();
		S.Intended.Elevation = 89.7 * kDegToRad;
		S.Intended.Speed = 3.0;
		S.Situation.Bridge = BridgeType::Elevated;
		S.CueBody.BowSag = 2e-3; // held up: + gamma in elevation
		S.Key = MakeKey(0xE1E7u, i / 20u, i, 1u, i);
		S.History = RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), i);
		const ExecutedStroke X = Execute(S);
		RB_REQUIRE(X.Error == ErrorCode::Ok);
		RB_CHECK(X.Strike.Elevation >= 0.0 && X.Strike.Elevation < 0.5 * kPi);
		RB_CHECK(SameBits(Sample(S, S.Intended.TimeDown).Elevation, X.Strike.Elevation));
		const double Raw = S.Intended.Elevation + SourceSumElevation(X);
		Capped += Raw >= 0.5 * kPi ? 1 : 0;
		if (Raw >= 0.5 * kPi)
		{
			RB_CHECK(X.Strike.Elevation == std::nextafter(0.5 * kPi, 0.0));
		}
		else
		{
			RB_CHECK_NEAR(X.Strike.Elevation, Raw, 1e-15);
		}
		RB_CHECK(!X.ElevationClamped);
	}
	RB_CHECK(Capped > 50); // the cap is exercised (about half of the draws push past vertical)
	// A negative floor is the cloth: the elevation never drops below 0 (the floor bit is set when it bites).
	Setup Low = MakeS0();
	Low.Intended.Elevation = 0.0;
	Low.Situation.ElevationFloor = -0.05;
	int Floored = 0;
	for (std::uint32_t i = 0; i < 40u; ++i)
	{
		Low.Key = MakeKey(0xF100u, 0u, i, 1u, i);
		Low.History = RebuildNoiseHistory(Low.Key.MatchSeed, ShooterKey(Low.Key), i);
		const ExecutedStroke X = Execute(Low);
		RB_CHECK(X.Strike.Elevation >= 0.0);
		Floored += X.ElevationClamped ? 1 : 0;
		RB_CHECK(X.ElevationClamped == (X.Strike.Elevation == 0.0));
	}
	RB_CHECK(Floored > 5);
}

// ExecuteStroke, SampleHand, the draws (also with a stale history), AdvanceNoiseHistory, SyntheticHand and the after-shot
// update never touch the heap (architecture 7.5: no allocation). Counted with the Debug CRT's allocation hook (MSVC Debug
// only; elsewhere this test checks nothing).
RB_TEST(Human_NoHeapAllocationInHotPaths)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	Setup S = MakeS0();
	S.Situation.Pressure = 0.6;
	S.CueBody.BowSag = 1e-3;
	S.CueBody.WarpKnown = false;
	BallObstacle Others[15];
	for (int i = 0; i < 15; ++i)
	{
		Others[i].Id = static_cast<BallId>(i + 1);
		Others[i].Position = {0.3 + 0.06 * (i % 5), -0.2 + 0.1 * (i / 5), kR};
	}
	static ShotResult Result; // large: kept off the stack
	Result = ShotResult{};
	StrikeOutcome Outcome;
	Outcome.Ball = 0;
	Result.Strikes.PushBack(Outcome);
	Result.BallsInPlay = 0xFFFFu;
	static BallChalkMarks Marks[kMaxBalls];
	TipState Tip = S.Tip;
	const AiCharacter Pro{GetAiProfile(AiProfileId::TouringPro), 3u};
	PlannedStroke Plan;
	Plan.Speed = 2.0;
	NoiseHistory History = S.History;
	Setup Stale = S;
	Stale.Key.ShooterShotIndex = 40u;
	Stale.History = NoiseHistory{};
	double Sink = 0.0;

	// Control: the hook sees a heap allocation.
	GAllocations = 0;
	_CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(&CountAllocations);
	int* Probe = new int(7);
	_CrtSetAllocHook(Previous);
	Sink += *Probe;
	delete Probe;
	RB_REQUIRE(GAllocations == 1);

	GAllocations = 0;
	Previous = _CrtSetAllocHook(&CountAllocations);
	for (int i = 0; i < 20; ++i)
	{
		const ExecutedStroke X = Execute(S, Others, 15);
		Sink += X.Strike.Speed + Sample(S, 1.0 + 0.1 * i).AxisOffsetA;
		Sink += Execute(Stale).Strike.Speed;
		Sink += SyntheticHand(Plan, Pro, S.Situation, kR, S.Key, S.History, S.Params).Speed;
		ApplyShotToEquipment(X, Result, 0, Quat{}, Tip, Marks);
		AdvanceNoiseHistory(History);
	}
	_CrtSetAllocHook(Previous);
	RB_CHECK(GAllocations == 0);
	RB_CHECK(Sink > 0.0);
#endif
}
