// Owner: WP-2 (equipment & table geometry). Rack lattice and micro-gaps: equipment 9, 13 (T-RACK-1..7),
// physics-collisions 3.9.5 and architecture A-RACK-1 (section 15 row 23).

#include "rbtest.h"

#include "Geometry/GeometryTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"

#include <chrono>
#include <cstdio>
#include <initializer_list>

using namespace rb;
using namespace rb::geotest;

namespace
{
	constexpr double kD = 0.05715;

	struct Rack
	{
		int Count = 0;
		RackSite Sites[kMaxRackSites];
		Vec2 Positions[kMaxRackSites];
	};

	Rack MakeRack(RackShape Shape, RackAnchor Anchor, double FootSpotX, double D)
	{
		Rack R;
		R.Count = BuildRackLattice(Shape, RackApexX(Shape, Anchor, FootSpotX, D), D, R.Sites);
		for (int i = 0; i < R.Count; ++i)
		{
			R.Positions[i] = R.Sites[i].Position;
		}
		return R;
	}

	double MinPairDistance(const Vec2* P, int Count)
	{
		double Min2 = kInfinity;
		for (int i = 0; i < Count; ++i)
		{
			for (int j = i + 1; j < Count; ++j)
			{
				Min2 = Min(Min2, LengthSquared(P[j] - P[i]));
			}
		}
		return Sqrt(Min2);
	}

	bool HasBallAt(const Rack& R, double X, double Y, double Tol)
	{
		for (int i = 0; i < R.Count; ++i)
		{
			if (Near(R.Positions[i], Vec2{X, Y}, Tol))
			{
				return true;
			}
		}
		return false;
	}

	// Nominal contacts of the frozen lattice and their target gaps as ApplyRackGaps draws them (RackLayout.h:
	// canonical pair order, two draws per contact).
	int TargetGaps(const Vec2* Lattice, int Count, double D, const RackGapParams& Gaps, std::uint64_t Seed, int* A, int* B, double* G)
	{
		Rng Stream(Seed);
		int N = 0;
		for (int i = 0; i < Count; ++i)
		{
			for (int j = i + 1; j < Count; ++j)
			{
				if (LengthSquared(Lattice[j] - Lattice[i]) <= Square(D * (1.0 + 1e-7)))
				{
					const double Uo = Stream.NextDouble01();
					const double U = Stream.NextDouble01();
					const double Raw = Uo < Gaps.OutlierProbability ? Gaps.OutlierMin + (Gaps.OutlierMax - Gaps.OutlierMin) * U : Gaps.Mean + Gaps.Jitter * (2.0 * U - 1.0);
					A[N] = i;
					B[N] = j;
					G[N] = Max(0.0, Raw);
					++N;
				}
			}
		}
		return N;
	}

	struct Shape
	{
		RackShape Kind;
		RackAnchor Anchor;
	};
	constexpr Shape kShapes[] = {{RackShape::Triangle15, RackAnchor::ApexOnFootSpot}, {RackShape::Triangle15, RackAnchor::CenterOnFootSpot},
		{RackShape::Triangle10, RackAnchor::ApexOnFootSpot}, {RackShape::Diamond9, RackAnchor::CenterOnFootSpot},
		{RackShape::Diamond9, RackAnchor::ApexOnFootSpot}};
}

// ---------------------------------------------------------------------------------------------------------
// equipment 13: T-RACK-1..7
// ---------------------------------------------------------------------------------------------------------

RB_TEST(EQP_TRACK1_EightBallRackNineFoot)
{
	const Rack R = MakeRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, 0.635, kD);
	RB_REQUIRE(R.Count == 15);
	RB_CHECK(R.Positions[0] == (Vec2{0.635, 0.0})); // apex exactly on the foot spot
	RB_CHECK_NEAR(R.Positions[4].x, 0.7339867, 1e-7); // the 8-ball site (row 2, index 1)
	RB_CHECK(R.Positions[4].y == 0.0);
	for (const double Y : {-0.1143, -0.05715, 0.0, 0.05715, 0.1143})
	{
		RB_CHECK(HasBallAt(R, 0.8329734, Y, 1e-7));
	}
	// Row x values of equipment 9.2 (9-ft).
	const double RowX[5] = {0.63500, 0.68449, 0.73399, 0.78348, 0.83297};
	for (int i = 0; i < R.Count; ++i)
	{
		RB_CHECK_NEAR(R.Positions[i].x, RowX[R.Sites[i].Row], 1e-5);
		RB_CHECK(R.Sites[i].Index >= 0 && R.Sites[i].Index <= R.Sites[i].Row);
	}
	// Sites ordered by row, then index increasing with y.
	for (int i = 1; i < R.Count; ++i)
	{
		const bool Order = R.Sites[i].Row > R.Sites[i - 1].Row || (R.Sites[i].Row == R.Sites[i - 1].Row && R.Positions[i].y > R.Positions[i - 1].y);
		RB_CHECK(Order);
	}
	RB_CHECK(RackAnchorSiteIndex(RackShape::Triangle15, RackAnchor::ApexOnFootSpot) == 0);
	// 7-ft BAR column: apex 0.508 and the gap from the back row to the foot nose.
	const Rack Bar = MakeRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, 0.508, kD);
	RB_CHECK_NEAR(Bar.Positions[14].x, 0.70597, 1e-5);
	RB_CHECK_NEAR(1.016 - Bar.Positions[14].x, 0.31003, 1e-5);
	RB_CHECK_NEAR(1.27 - R.Positions[14].x, 0.43703, 1e-5);
}

RB_TEST(EQP_TRACK2_FrozenRackContacts)
{
	const Rack R = MakeRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, 0.635, kD);
	RB_CHECK_NEAR(MinPairDistance(R.Positions, R.Count), kD, 1e-12);
	RB_CHECK(MinPairDistance(R.Positions, R.Count) >= kD - 1e-15); // the lattice never overlaps
	RB_CHECK(CountTouchingPairs(R.Positions, R.Count, kD, 1e-12) == 30);
	// The next neighbours are sqrt(3) D away.
	RB_CHECK(CountTouchingPairs(R.Positions, R.Count, kD, 0.5 * kD) == 30);
	RB_CHECK(CountTouchingPairs(nullptr, 15, kD, 1e-12) == 0);
}

RB_TEST(EQP_TRACK3_NineBallNineOnSpot)
{
	const Rack R = MakeRack(RackShape::Diamond9, RackAnchor::CenterOnFootSpot, 0.635, kD);
	RB_REQUIRE(R.Count == 9);
	const int Anchor = RackAnchorSiteIndex(RackShape::Diamond9, RackAnchor::CenterOnFootSpot);
	RB_CHECK(Anchor == 4);
	RB_CHECK(R.Positions[Anchor] == (Vec2{0.635, 0.0})); // the 9 bitwise on the foot spot
	RB_CHECK_NEAR(R.Positions[0].x, 0.5360133, 1e-7);   // 1-ball at the apex
	RB_CHECK(R.Positions[0].y == 0.0);
	RB_CHECK_NEAR(R.Positions[8].x, 0.7339867, 1e-7); // last ball
	RB_CHECK(R.Positions[8].y == 0.0);
	RB_CHECK(CountTouchingPairs(R.Positions, R.Count, kD, 1e-12) == 16);
	// Rows 1, 2, 3, 2, 1.
	const int RowOf[9] = {0, 1, 1, 2, 2, 2, 3, 3, 4};
	for (int i = 0; i < 9; ++i)
	{
		RB_CHECK(R.Sites[i].Row == RowOf[i]);
	}
}

RB_TEST(EQP_TRACK4_NineBallOneOnSpotSevenFoot)
{
	const Rack R = MakeRack(RackShape::Diamond9, RackAnchor::ApexOnFootSpot, 0.508, kD);
	RB_REQUIRE(R.Count == 9);
	RB_CHECK(R.Positions[0] == (Vec2{0.508, 0.0}));
	RB_CHECK_NEAR(R.Positions[4].x, 0.6069867, 1e-7);
	RB_CHECK(R.Positions[4].y == 0.0);
	RB_CHECK(RackApexX(RackShape::Diamond9, RackAnchor::ApexOnFootSpot, 0.508, kD) == 0.508);
}

RB_TEST(EQP_TRACK5_TenBallRack)
{
	const Rack R = MakeRack(RackShape::Triangle10, RackAnchor::ApexOnFootSpot, 0.635, kD);
	RB_REQUIRE(R.Count == 10);
	RB_CHECK(R.Positions[0] == (Vec2{0.635, 0.0}));
	RB_CHECK_NEAR(R.Positions[4].x, 0.7339867, 1e-7); // the 10 (row 2 center)
	RB_CHECK(R.Positions[4].y == 0.0);
	for (int i = 6; i < 10; ++i)
	{
		RB_CHECK_NEAR(R.Positions[i].x, 0.7834801, 1e-7);
	}
	RB_CHECK(CountTouchingPairs(R.Positions, R.Count, kD, 1e-12) == 18);
}

RB_TEST(EQP_TRACK6_RacksInsideTheNosesOnAllPresets)
{
	for (const TablePreset Preset : kAllPresets)
	{
		TableGeometry G;
		RB_REQUIRE(BuildTableGeometry(GetTableSpec(Preset), G) == ErrorCode::Ok);
		const double Rc = ComputeCushionContact(0.5 * kD, G.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
		for (const Shape& S : kShapes)
		{
			for (const RackGapParams& Gaps : {kRackGapNone, kRackGapSloppyBar, kRackGapMixture})
			{
				Rack R = MakeRack(S.Kind, S.Anchor, G.Landmarks.FootSpot.x, kD);
				ApplyRackGaps(R.Positions, R.Count, RackAnchorSiteIndex(S.Kind, S.Anchor), kD, Gaps, 17);
				for (int i = 0; i < R.Count; ++i)
				{
					const Vec2 P = R.Positions[i];
					RB_CHECK(Abs(P.x) <= G.HalfLength - Rc && Abs(P.y) <= G.HalfWidth - Rc);
					RB_CHECK(!IsOverPocketOpening(G, P));
				}
			}
		}
	}
}

RB_TEST(EQP_TRACK7_TightRackInnerSides)
{
	RB_CHECK_NEAR(RackInnerSide(RackShape::Triangle15, kD), 0.3275867, 1e-7);
	RB_CHECK_NEAR(RackInnerSide(RackShape::Triangle10, kD), 0.2704367, 1e-7);
	RB_CHECK_NEAR(RackInnerSide(RackShape::Diamond9, kD), 0.1802911, 1e-7);
	RB_CHECK_NEAR(RackInnerSide(RackShape::Triangle15, kD) / kInch, 12.90, 0.005);
	RB_CHECK_NEAR(RackInnerSide(RackShape::Triangle10, kD) / kInch, 10.65, 0.005);
	RB_CHECK_NEAR(RackInnerSide(RackShape::Diamond9, kD) / kInch, 7.10, 0.005);
	RB_CHECK_NEAR(RackInnerSide(RackShape::Triangle15, kD), kRack15InnerSide, 1e-15);
	// Geometric check: the lattice's corner balls touch the offset triangle (15 balls: the outer rows span 4 D).
	const Rack R = MakeRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, 0.0, kD);
	RB_CHECK_NEAR(Length(R.Positions[14] - R.Positions[10]), 4.0 * kD, 1e-12);
	RB_CHECK_NEAR(Length(R.Positions[10] - R.Positions[0]), 4.0 * kD, 1e-12);
}

// ---------------------------------------------------------------------------------------------------------
// Lattice details
// ---------------------------------------------------------------------------------------------------------

RB_TEST(Geometry_RackAnchorBitwiseOnTheSpot)
{
	// RackApexX + BuildRackLattice put the CenterOnFootSpot anchor exactly on the spot for any table and ball.
	for (const double FootX : {0.635, 0.508, 0.4826, 0.4953, 0.5842, 0.559, 1.0, 0.1234567})
	{
		for (const double D : {0.05715, 0.0570, 0.05708, 0.060325, 0.0508, 0.047625, 0.0525, 0.05715 * (1.0 + 1e-9)})
		{
			for (const Shape& S : kShapes)
			{
				const Rack R = MakeRack(S.Kind, S.Anchor, FootX, D);
				RB_CHECK(R.Positions[RackAnchorSiteIndex(S.Kind, S.Anchor)].x == FootX);
				RB_CHECK(R.Positions[RackAnchorSiteIndex(S.Kind, S.Anchor)].y == 0.0);
				// Frozen: every nominal contact at D to rounding (the row spacing is rounded up by < 6e-14 m).
				RB_CHECK(MinPairDistance(R.Positions, R.Count) >= D * (1.0 - 1e-15));
				RB_CHECK(MinPairDistance(R.Positions, R.Count) < D * (1.0 + 1e-12));
			}
		}
	}
}

RB_TEST(Geometry_RackLatticeSymmetricAndValidated)
{
	for (const Shape& S : kShapes)
	{
		const Rack R = MakeRack(S.Kind, S.Anchor, 0.635, kD);
		for (int i = 0; i < R.Count; ++i)
		{
			bool Mirrored = false;
			for (int j = 0; j < R.Count; ++j)
			{
				Mirrored = Mirrored || (R.Positions[j].x == R.Positions[i].x && R.Positions[j].y == -R.Positions[i].y);
			}
			RB_CHECK(Mirrored); // exactly symmetric about the long string
		}
	}
	RackSite Sites[kMaxRackSites];
	RB_CHECK(BuildRackLattice(RackShape::Triangle15, 0.635, 0.0, Sites) == 0);
	RB_CHECK(BuildRackLattice(RackShape::Triangle15, 0.635, -kD, Sites) == 0);
	RB_CHECK(BuildRackLattice(RackShape::Triangle15, std::nan(""), kD, Sites) == 0);
	RB_CHECK(BuildRackLattice(RackShape::Triangle15, 0.635, kD, nullptr) == 0);
	RB_CHECK(BuildRackLattice(static_cast<RackShape>(9), 0.635, kD, Sites) == 0);
	RB_CHECK(RackAnchorSiteIndex(RackShape::Diamond9, RackAnchor::CenterOnFootSpot) == 4);
	RB_CHECK(RackAnchorSiteIndex(RackShape::Triangle10, RackAnchor::CenterOnFootSpot) == 4);
	RB_CHECK(RackInnerSide(static_cast<RackShape>(9), kD) == 0.0);
}

// ---------------------------------------------------------------------------------------------------------
// Micro-gaps (collisions 3.9.5): A-RACK-1
// ---------------------------------------------------------------------------------------------------------

RB_TEST(ARCH_RACK1_MicroGaps)
{
	// Anchor unchanged, no pair closer than D, realised gaps within 20 % of the targets on average (per rack: the
	// mean realised gap of the nominal contacts vs their mean target gap, for the collisions 3.9.5 presets),
	// deterministic per seed.
	const RackGapParams Presets[3] = {kRackGapTightTemplate, kRackGapWoodenRack, kRackGapSloppyBar};
	double WorstRelative = 0.0;
	for (const Shape& S : kShapes)
	{
		const Rack Lattice = MakeRack(S.Kind, S.Anchor, 0.635, kD);
		const int Anchor = RackAnchorSiteIndex(S.Kind, S.Anchor);
		for (const RackGapParams& Gaps : Presets)
		{
			double MeanRelativeResidual = 0.0;
			for (std::uint64_t Seed = 0; Seed < 200; ++Seed)
			{
				Vec2 P[kMaxRackSites];
				for (int i = 0; i < Lattice.Count; ++i)
				{
					P[i] = Lattice.Positions[i];
				}
				ApplyRackGaps(P, Lattice.Count, Anchor, kD, Gaps, Seed);
				RB_CHECK(P[Anchor] == Lattice.Positions[Anchor]);
				RB_CHECK(MinPairDistance(P, Lattice.Count) >= kD);

				int A[3 * kMaxRackSites];
				int B[3 * kMaxRackSites];
				double Target[3 * kMaxRackSites];
				const int N = TargetGaps(Lattice.Positions, Lattice.Count, kD, Gaps, Seed, A, B, Target);
				double SumTarget = 0.0;
				double SumRealised = 0.0;
				double SumResidual = 0.0;
				for (int c = 0; c < N; ++c)
				{
					const double Realised = Length(P[B[c]] - P[A[c]]) - kD;
					SumTarget += Target[c];
					SumRealised += Realised;
					SumResidual += Abs(Realised - Target[c]);
				}
				const double Relative = Abs(SumRealised - SumTarget) / SumTarget;
				RB_CHECK(Relative <= 0.2);
				WorstRelative = Max(WorstRelative, Relative);
				MeanRelativeResidual += SumResidual / SumTarget / 200.0;

				// Deterministic: the same seed gives bitwise the same rack; another seed a different one.
				Vec2 Again[kMaxRackSites];
				Vec2 Other[kMaxRackSites];
				for (int i = 0; i < Lattice.Count; ++i)
				{
					Again[i] = Lattice.Positions[i];
					Other[i] = Lattice.Positions[i];
				}
				ApplyRackGaps(Again, Lattice.Count, Anchor, kD, Gaps, Seed);
				ApplyRackGaps(Other, Lattice.Count, Anchor, kD, Gaps, Seed + 1000);
				bool Same = true;
				bool Differs = false;
				for (int i = 0; i < Lattice.Count; ++i)
				{
					Same = Same && Again[i] == P[i];
					Differs = Differs || !(Other[i] == P[i]);
				}
				RB_CHECK(Same);
				RB_CHECK(Differs);
			}
			// Per-contact residuals: the least-squares optimum of the lattice is ~10-13 % of the mean gap.
			RB_CHECK(MeanRelativeResidual <= 0.2);
		}
	}
	std::printf("  [A-RACK-1] worst per-rack |mean realised - mean target| / mean target = %.3f\n", WorstRelative);
}

RB_TEST(ARCH_RACK1_MixtureAndZeroGaps)
{
	for (const Shape& S : kShapes)
	{
		const Rack Lattice = MakeRack(S.Kind, S.Anchor, 0.635, kD);
		const int Anchor = RackAnchorSiteIndex(S.Kind, S.Anchor);
		// kRackGapNone: the frozen lattice stays bitwise (every target gap is 0).
		Vec2 Frozen[kMaxRackSites];
		for (int i = 0; i < Lattice.Count; ++i)
		{
			Frozen[i] = Lattice.Positions[i];
		}
		ApplyRackGaps(Frozen, Lattice.Count, Anchor, kD, kRackGapNone, 99);
		for (int i = 0; i < Lattice.Count; ++i)
		{
			RB_CHECK(Frozen[i] == Lattice.Positions[i]);
		}
		// prior-art 5.6 mixture: anchor, no overlap, loose contacts realised (the outliers are inconsistent with
		// their zero-gap neighbours, so the realised mean runs above the target mean; see RackLayout.h).
		double SumTarget = 0.0;
		double SumRealised = 0.0;
		for (std::uint64_t Seed = 0; Seed < 200; ++Seed)
		{
			Vec2 P[kMaxRackSites];
			for (int i = 0; i < Lattice.Count; ++i)
			{
				P[i] = Lattice.Positions[i];
			}
			ApplyRackGaps(P, Lattice.Count, Anchor, kD, kRackGapMixture, Seed);
			RB_CHECK(P[Anchor] == Lattice.Positions[Anchor]);
			RB_CHECK(MinPairDistance(P, Lattice.Count) >= kD);
			int A[3 * kMaxRackSites];
			int B[3 * kMaxRackSites];
			double Target[3 * kMaxRackSites];
			const int N = TargetGaps(Lattice.Positions, Lattice.Count, kD, kRackGapMixture, Seed, A, B, Target);
			for (int c = 0; c < N; ++c)
			{
				SumTarget += Target[c];
				SumRealised += Length(P[B[c]] - P[A[c]]) - kD;
				RB_CHECK(Length(P[B[c]] - P[A[c]]) - kD <= 0.6e-3); // no contact opens beyond the largest outlier
			}
		}
		RB_CHECK(SumRealised >= SumTarget * 0.95 && SumRealised <= SumTarget * 1.5);
	}
}

RB_TEST(Geometry_RackGapsArgumentsAndDisplacement)
{
	const Rack Lattice = MakeRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, 0.635, kD);
	Vec2 P[kMaxRackSites];
	for (int i = 0; i < Lattice.Count; ++i)
	{
		P[i] = Lattice.Positions[i];
	}
	// Invalid arguments leave the positions unchanged.
	ApplyRackGaps(P, Lattice.Count, -1, kD, kRackGapSloppyBar, 1);
	ApplyRackGaps(P, Lattice.Count, Lattice.Count, kD, kRackGapSloppyBar, 1);
	ApplyRackGaps(P, 1, 0, kD, kRackGapSloppyBar, 1);
	ApplyRackGaps(P, Lattice.Count, 0, 0.0, kRackGapSloppyBar, 1);
	ApplyRackGaps(nullptr, Lattice.Count, 0, kD, kRackGapSloppyBar, 1);
	for (int i = 0; i < Lattice.Count; ++i)
	{
		RB_CHECK(P[i] == Lattice.Positions[i]);
	}
	// Micro-gaps are micro: the sloppy bar rack moves no ball by more than 1 mm, and the rack grows away from
	// the anchor (the back row moves toward +x).
	ApplyRackGaps(P, Lattice.Count, 0, kD, kRackGapSloppyBar, 1);
	for (int i = 0; i < Lattice.Count; ++i)
	{
		RB_CHECK(Length(P[i] - Lattice.Positions[i]) < 1e-3);
	}
	RB_CHECK(P[12].x > Lattice.Positions[12].x);
	// A center-anchored rack grows both ways.
	const Rack Center = MakeRack(RackShape::Triangle15, RackAnchor::CenterOnFootSpot, 0.635, kD);
	Vec2 Q[kMaxRackSites];
	for (int i = 0; i < Center.Count; ++i)
	{
		Q[i] = Center.Positions[i];
	}
	ApplyRackGaps(Q, Center.Count, 4, kD, kRackGapSloppyBar, 3);
	RB_CHECK(Q[0].x < Center.Positions[0].x && Q[12].x > Center.Positions[12].x);
	RB_CHECK(Q[4] == Center.Positions[4]);
}

RB_TEST(Geometry_Slow_BuildAndRackTiming)
{
	// Release timing (reported, not gated): BuildTableGeometry once per table, ApplyRackGaps once per rack.
	using Clock = std::chrono::steady_clock;
	constexpr int Builds = 2000;
	TableGeometry G;
	const auto T0 = Clock::now();
	for (int i = 0; i < Builds; ++i)
	{
		BuildTableGeometry(kTableNineFootPro, G);
	}
	const auto T1 = Clock::now();
	const Rack Lattice = MakeRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, 0.635, kD);
	constexpr int Racks = 20000;
	double Sink = 0.0;
	const RackGapParams All[2] = {kRackGapWoodenRack, kRackGapMixture};
	double PerRack[2] = {};
	for (int p = 0; p < 2; ++p)
	{
		const auto R0 = Clock::now();
		for (int i = 0; i < Racks; ++i)
		{
			Vec2 P[kMaxRackSites];
			for (int k = 0; k < Lattice.Count; ++k)
			{
				P[k] = Lattice.Positions[k];
			}
			ApplyRackGaps(P, Lattice.Count, 0, kD, All[p], static_cast<std::uint64_t>(i));
			Sink += P[14].x;
		}
		PerRack[p] = std::chrono::duration<double, std::micro>(Clock::now() - R0).count() / Racks;
	}
	std::printf("  [timing] BuildTableGeometry %.2f us, ApplyRackGaps wooden %.2f us, mixture %.2f us (sink %.3f)\n",
		std::chrono::duration<double, std::micro>(T1 - T0).count() / Builds, PerRack[0], PerRack[1], Sink);
	RB_CHECK(Sink > 0.0);
}
