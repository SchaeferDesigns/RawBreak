// Owner: WP-10 (validation & benchmarks). prior-art 9.8 POCK-01 ... POCK-04 (Tier D; Tier C once OQ-7 data exists) and
// physics-collisions 5.5 (what emerges from the Level A pocket): effective pocket sizes on the 9-ft pro table (the table's own
// cushions, facings and pockets: MakePhysicsParams; VAL cloth, g 9.81).
//
// Effective target size (INTERPRETATION of Alciatore's TP 3.5-3.8 / BD Dec 2004 measure): an object ball rolls toward the pocket
// along a direction at angle alpha from the pocket axis (0 = straight in; for a corner pocket 45 deg = parallel to the long rail),
// arriving at the mouth at the stated speed (its speed there without contacts); its path is shifted sideways in 0.25 mm steps across
// the mouth, and the size is the total width of the shifts that drop (in inches). Every path starts 1 m before the mouth, so paths
// that glance off the near rail first count (see Approach); paths inside a cushion over that whole distance are skipped.

#include "Validation/ValidationUtil.h"

using namespace rb;
using simtest::kR;

namespace
{
	constexpr double kStep = 0.25e-3;

	struct Scan
	{
		double SizeInch = 0.0;
		int Samples = 0;
		int Pocketed = 0;
	};

	Vec2 Rotate(const Vec2& V, double Angle) { return {V.x * Cos(Angle) - V.y * Sin(Angle), V.x * Sin(Angle) + V.y * Cos(Angle)}; }

	ShotResult& LastResult()
	{
		static ShotResult R;
		return R;
	}

	// One approach: the object ball's path through MouthMid + Normal * Offset along Direction, rolling at SpeedAtMouth there.
	// Returns 1 if pocketed in Pocket, 0 if not, -1 if the start is not a legal position; FirstJaw = its first table contact was a
	// jaw element of that pocket.
	int Approach(int Pocket, const Vec2& Direction, double Offset, double SpeedAtMouth, bool* FirstJaw = nullptr)
	{
		const TableGeometry& T = simtest::NineFoot();
		const PocketGeometry& G = T.Pockets[Pocket];
		const PhysicsParams P = val::TableParams(kTableNineFootPro);
		const Vec2 Normal = PerpCcw(Direction);
		const Vec2 AtMouth = G.MouthMid + Normal * Offset;
		// Start 1 m back along the path (INTERPRETATION: far enough for every rail glance that can still drop; a path 2 deg off a
		// rail that meets it up to ~0.7 m before the mouth comes off it close enough to the rail). Every such path aimed at the
		// pocket counts, also one whose line reaches the near rail's cushion before the mouth: the ball glances off the rail (the
		// simulator resolves that contact) and may still drop - part of Alciatore's effective size ("the ball can glance off the
		// near rail and still drop"). A path whose start lies inside a cushion (its line comes out of the rail within that
		// distance) starts where it leaves the cushion's contact line, glancing along the rail; a path inside the cushion over the
		// whole distance is not a shot and is skipped. (A 0.4 m run-up cut the rail glances short: 2.05 in at 43 deg.)
		const double Rc = ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
		const auto Legal = [&](const Vec2& Q) { return Abs(Q.x) <= T.HalfLength - Rc && Abs(Q.y) <= T.HalfWidth - Rc; };
		double Back = 1.0;
		if (!Legal(AtMouth - Direction * Back))
		{
			double Hi = Back; // inside the cushion
			double Lo = -1.0; // the first legal point toward the mouth
			for (int k = 1; k <= 1000; ++k)
			{
				const double D = Back - 1e-3 * k;
				if (Legal(AtMouth - Direction * D))
				{
					Lo = D;
					break;
				}
			}
			if (!(Lo >= 0.0))
			{
				return -1;
			}
			for (int k = 0; k < 60; ++k)
			{
				const double Mid = 0.5 * (Lo + Hi);
				(Legal(AtMouth - Direction * Mid) ? Lo : Hi) = Mid;
			}
			Back = Lo;
		}
		const Vec2 Start = AtMouth - Direction * Back;
		SimInput& In = val::Fresh(T, P);
		In.Record.Trajectories = false;
		const double V0 = val::RollingStartSpeed(SpeedAtMouth, Back, P);
		val::PlaceRollingSpin(In, 0, ToVec3(Start, kR), ToVec3(Direction * V0), 0.0, MakeBallSpec(kR, kDefaultBallMass));
		ShotResult& R = LastResult();
		Simulator Sim;
		if (Sim.Run(In, R) != SimStatus::Ok)
		{
			return -1;
		}
		if (FirstJaw != nullptr)
		{
			*FirstJaw = false;
			for (const ShotEvent& E : R.Events)
			{
				if (E.A == 0 && (E.Type == ShotEventType::BallCushion || E.Type == ShotEventType::BallJaw || E.Type == ShotEventType::BallPocketEnter))
				{
					*FirstJaw = E.Type == ShotEventType::BallJaw && E.Feature == Pocket;
					break;
				}
			}
		}
		const BallFinal& F = R.Finals[0];
		return F.Status == BallFinalStatus::Pocketed && static_cast<int>(F.Pocket) == Pocket ? 1 : 0;
	}

	Scan EffectiveSize(int Pocket, double AlphaDeg, double SpeedAtMouth)
	{
		const PocketGeometry& G = simtest::NineFoot().Pockets[Pocket];
		const Vec2 Direction = Rotate(G.Axis, AlphaDeg * kDegToRad);
		Scan S;
		for (double Offset = -0.12; Offset <= 0.12 + 1e-12; Offset += kStep)
		{
			const int Result = Approach(Pocket, Direction, Offset, SpeedAtMouth);
			if (Result < 0)
			{
				continue;
			}
			++S.Samples;
			S.Pocketed += Result;
		}
		S.SizeInch = S.Pocketed * kStep / kInch;
		return S;
	}
}

// POCK-01 / POCK-02: corner pocket (foot right), slow (0.3 m/s at the mouth) and fast (2.5 m/s) rolling object balls, approach
// angles 0 - 45 deg from the pocket axis toward the long rail. Slow: the largest size near 40 - 45 deg, 2.5 - 4.0 in at 43 deg
// (Alciatore: 3.4 in). Fast: never larger than slow, at 43 deg at most 70 % of slow.
RB_TEST(Integ_VAL_POCK01_POCK02_Slow_CornerPocketEffectiveSize)
{
	const int Pocket = static_cast<int>(PocketId::FootRight);
	const double Angles[] = {0.0, 10.0, 20.0, 30.0, 35.0, 40.0, 43.0, 45.0};
	double SlowSize[8] = {};
	double FastSize[8] = {};
	double BestAngle = 0.0;
	double BestSize = -1.0;
	int FastNotLarger = 0;
	for (int a = 0; a < 8; ++a)
	{
		SlowSize[a] = EffectiveSize(Pocket, Angles[a], 0.3).SizeInch;
		FastSize[a] = EffectiveSize(Pocket, Angles[a], 2.5).SizeInch;
		FastNotLarger += FastSize[a] <= SlowSize[a] + kStep / kInch ? 1 : 0;
		if (SlowSize[a] > BestSize)
		{
			BestSize = SlowSize[a];
			BestAngle = Angles[a];
		}
		std::printf("  POCK-01/02 corner, %4.1f deg from the axis: slow %.2f in, fast %.2f in (%.0f %%)\n", Angles[a], SlowSize[a], FastSize[a],
			SlowSize[a] > 0.0 ? 100.0 * FastSize[a] / SlowSize[a] : 0.0);
	}
	const double At43 = SlowSize[6];
	std::printf("  POCK-01: largest slow size %.2f in at %.0f deg (expected near 40 - 45), at 43 deg %.2f in (expected 2.5 - 4.0) %s\n", BestSize, BestAngle,
		At43, val::Verdict(BestAngle >= 40.0 && At43 >= 2.5 && At43 <= 4.0));
	std::printf("  POCK-02: fast <= slow at %d/8 angles, at 43 deg %.0f %% of slow (<= 70 %%) %s\n", FastNotLarger,
		At43 > 0.0 ? 100.0 * FastSize[6] / At43 : 0.0, val::Verdict(FastNotLarger == 8 && FastSize[6] <= 0.7 * At43));
	RB_CHECK(BestAngle >= 40.0);
	RB_CHECK(At43 >= 2.5 && At43 <= 4.0);
	RB_CHECK(FastNotLarger == 8);
	RB_CHECK(FastSize[6] <= 0.7 * At43);
}

// POCK-03: side pocket (side right), slow, 0 - 60 deg from the axis: the size decreases beyond ~20 deg (monotone within one sample
// step) and is near zero at 60 deg (INTERPRETATION: below 0.5 in).
RB_TEST(Integ_VAL_POCK03_Slow_SidePocketAngleDependence)
{
	const int Pocket = static_cast<int>(PocketId::SideRight);
	const double Angles[] = {0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0};
	double Size[7] = {};
	bool Monotone = true;
	for (int a = 0; a < 7; ++a)
	{
		Size[a] = EffectiveSize(Pocket, Angles[a], 0.3).SizeInch;
		if (Angles[a] > 20.0 && Size[a] > Size[a - 1] + kStep / kInch)
		{
			Monotone = false;
		}
		std::printf("  POCK-03 side, %4.1f deg: %.2f in\n", Angles[a], Size[a]);
	}
	std::printf("  POCK-03: monotone beyond 20 deg %s, at 60 deg %.2f in (near zero: < 0.5)\n", Monotone ? "yes" : "NO", Size[6]);
	RB_CHECK(Monotone);
	RB_CHECK(Size[6] < 0.5);
}

// POCK-04: a fast object ball (2.5 m/s) hitting the near jaw first at 30 deg rattles out in at least one configuration that a slow
// ball (0.3 m/s) makes.
RB_TEST(Integ_VAL_POCK04_Slow_FastBallRattlesOut)
{
	const int Pocket = static_cast<int>(PocketId::FootRight);
	const PocketGeometry& G = simtest::NineFoot().Pockets[Pocket];
	const Vec2 Direction = Rotate(G.Axis, 30.0 * kDegToRad);
	int Rattles = 0;
	int NearJawFirst = 0;
	for (double Offset = -0.12; Offset <= 0.12 + 1e-12; Offset += kStep)
	{
		bool FastJaw = false;
		const int Fast = Approach(Pocket, Direction, Offset, 2.5, &FastJaw);
		if (Fast < 0 || !FastJaw)
		{
			continue;
		}
		++NearJawFirst;
		const int Slow = Approach(Pocket, Direction, Offset, 0.3);
		Rattles += Slow == 1 && Fast == 0 ? 1 : 0;
	}
	std::printf("  POCK-04: %d of %d near-jaw-first fast approaches rattle out where the slow ball drops (>= 1)\n", Rattles, NearJawFirst);
	RB_CHECK(Rattles >= 1);
}
