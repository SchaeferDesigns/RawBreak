// Owner: WP-9 (rules table procedures & match). rules.md 5.1 rack fill rules and lattice positions;
// tests K01-K04, K06 (integration: need the WP-2 lattice RackApexX / BuildRackLattice / ApplyRackGaps).

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Math/Scalar.h"

#include <cstring>

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

namespace
{
	int CountRacked(const RackAssignment& Rack)
	{
		int Count = 0;
		for (const bool R : Rack.Racked)
		{
			Count += R ? 1 : 0;
		}
		return Count;
	}

	bool NearPoint(const Vec2& P, double X, double Y) { return Abs(P.x - X) <= 1e-6 && Abs(P.y - Y) <= 1e-6; }

	int BallAt(const RackAssignment& Rack, double X, double Y)
	{
		for (int b = 1; b < kRulesBallCount; ++b)
		{
			if (Rack.Racked[b] && NearPoint(Rack.Position[b], X, Y))
			{
				return b;
			}
		}
		return -1;
	}
}

RB_TEST(Rules_Table_ApexEmptyOnlyForStraightPool)
{
	// Standalone: argument check before any lattice work.
	RackAssignment Rack;
	Rack.Racked[3] = true;
	RB_CHECK(GenerateRack(Discipline::NineBall, RulesConfig{}, NineFootTable(), 7, true, kRackGapNone, Rack) == ErrorCode::InvalidArgument);
	RB_CHECK(CountRacked(Rack) == 0); // reset on error
}

RB_TEST(Integ_Rules_K01_EightBallRack)
{
	RackAssignment Rack;
	RB_REQUIRE(GenerateRack(Discipline::EightBall, RulesConfig{}, NineFootTable(), 1, false, kRackGapNone, Rack) == ErrorCode::Ok);
	RB_CHECK(CountRacked(Rack) == 15);
	RB_CHECK(Rack.SiteCount == 15);
	RB_CHECK(Rack.BallAtSite[4] == 8);
	RB_CHECK(BallAt(Rack, 0.733987, 0.0) == 8);
	const int Apex = BallAt(Rack, 0.635, 0.0);
	RB_CHECK(Apex > 0 && Apex != 8);
	const int CornerA = BallAt(Rack, 0.832973, 0.114300);
	const int CornerB = BallAt(Rack, 0.832973, -0.114300);
	RB_REQUIRE(CornerA > 0 && CornerB > 0);
	RB_CHECK(GroupOf(CornerA) != BallGroup::None && GroupOf(CornerB) != BallGroup::None && GroupOf(CornerA) != GroupOf(CornerB));
	// 15-ball back row x = 0.832973, y = 0, +-0.057150, +-0.114300
	RB_CHECK(BallAt(Rack, 0.832973, 0.0) > 0);
	RB_CHECK(BallAt(Rack, 0.832973, 0.057150) > 0);
	RB_CHECK(BallAt(Rack, 0.832973, -0.057150) > 0);
}

RB_TEST(Integ_Rules_K02_NineBallNineOnSpot)
{
	RulesConfig Config;
	Config.NineBallRack = NineBallRackRule::NineOnSpot;
	RackAssignment Rack;
	RB_REQUIRE(GenerateRack(Discipline::NineBall, Config, NineFootTable(), 5, false, kRackGapNone, Rack) == ErrorCode::Ok);
	RB_CHECK(CountRacked(Rack) == 9);
	for (int b = 1; b <= 9; ++b)
	{
		RB_CHECK(Rack.Racked[b]);
	}
	RB_CHECK(NearPoint(Rack.Position[9], 0.635, 0.0));
	RB_CHECK(NearPoint(Rack.Position[1], 0.536013, 0.0));
	RB_CHECK(BallAt(Rack, 0.733987, 0.0) > 0); // last ball (r = 4)
	RB_CHECK(Rack.Position[9] == NineFootTable().FootSpot); // the anchor stays exactly on the spot
}

RB_TEST(Integ_Rules_K03_NineBallOneOnSpotLegacy)
{
	RulesConfig Config;
	Config.NineBallRack = NineBallRackRule::OneOnSpot;
	RackAssignment Rack;
	RB_REQUIRE(GenerateRack(Discipline::NineBall, Config, NineFootTable(), 5, false, kRackGapNone, Rack) == ErrorCode::Ok);
	RB_CHECK(NearPoint(Rack.Position[1], 0.635, 0.0));
	RB_CHECK(NearPoint(Rack.Position[9], 0.733987, 0.0));
}

RB_TEST(Integ_Rules_K04_TenBallRack)
{
	RackAssignment Rack;
	RB_REQUIRE(GenerateRack(Discipline::TenBall, RulesConfig{}, NineFootTable(), 9, false, kRackGapNone, Rack) == ErrorCode::Ok);
	RB_CHECK(CountRacked(Rack) == 10);
	RB_CHECK(NearPoint(Rack.Position[1], 0.635, 0.0));
	RB_CHECK(NearPoint(Rack.Position[10], 0.733987, 0.0));
	const double BackY[4] = {-0.085725, -0.028575, 0.028575, 0.085725};
	for (const double Y : BackY)
	{
		RB_CHECK(BallAt(Rack, 0.783480, Y) > 0);
	}
}

RB_TEST(Integ_Rules_K05_StraightPoolRacksInsideOutline)
{
	const RulesTable Table = NineFootTable();
	RackAssignment Full;
	RB_REQUIRE(GenerateRack(Discipline::StraightPool, RulesConfig{}, Table, 3, false, kRackGapWoodenRack, Full) == ErrorCode::Ok);
	RB_CHECK(CountRacked(Full) == 15);
	for (int b = 1; b <= 15; ++b)
	{
		RB_CHECK(InterferesWithRack(Full.Position[b], kR, Table));
	}
	// Continuation rack of a given set: the 14 balls other than 7, apex left empty.
	RackAssignment Fourteen;
	const std::uint32_t Mask = 0xFFFEu & ~(1u << 7);
	RB_REQUIRE(GenerateStraightPoolRack(Table, 3, true, Mask, kRackGapNone, Fourteen) == ErrorCode::Ok);
	RB_CHECK(CountRacked(Fourteen) == 14);
	RB_CHECK(!Fourteen.Racked[7]);
	RB_CHECK(Fourteen.BallAtSite[0] == kNoBall);
	RB_CHECK(BallAt(Fourteen, 0.635, 0.0) == -1);
	// Public GenerateRack with ApexEmpty: 14 of the 15 balls.
	RackAssignment Any14;
	RB_REQUIRE(GenerateRack(Discipline::StraightPool, RulesConfig{}, Table, 3, true, kRackGapNone, Any14) == ErrorCode::Ok);
	RB_CHECK(CountRacked(Any14) == 14);
	RB_CHECK(Any14.BallAtSite[0] == kNoBall);
}

RB_TEST(Integ_Rules_K06_EightBallRackOverSeeds)
{
	const RulesTable Table = NineFootTable();
	const RulesConfig Config;
	for (std::uint64_t Seed = 0; Seed < 10000; ++Seed)
	{
		RackAssignment A;
		RackAssignment B;
		const ErrorCode Ea = GenerateRack(Discipline::EightBall, Config, Table, Seed, false, kRackGapWoodenRack, A);
		const ErrorCode Eb = GenerateRack(Discipline::EightBall, Config, Table, Seed, false, kRackGapWoodenRack, B);
		if (Ea != ErrorCode::Ok || Eb != ErrorCode::Ok)
		{
			RB_CHECK(Ea == ErrorCode::Ok && Eb == ErrorCode::Ok);
			return;
		}
		RB_CHECK(A.BallAtSite[4] == 8);
		RB_CHECK(A.BallAtSite[0] != 8);
		const BallGroup Low = GroupOf(A.BallAtSite[10]);
		const BallGroup High = GroupOf(A.BallAtSite[14]);
		RB_CHECK(Low != BallGroup::None && High != BallGroup::None && Low != High);
		RB_CHECK(CountRacked(A) == 15);
		// same seed -> identical rack (sites and positions bitwise)
		RB_CHECK(std::memcmp(A.BallAtSite, B.BallAtSite, sizeof(A.BallAtSite)) == 0);
		for (int b = 1; b <= 15; ++b)
		{
			RB_CHECK(A.Position[b] == B.Position[b]);
		}
		// the apex ball (the anchor) stays exactly on the foot spot
		RB_CHECK(A.Position[A.BallAtSite[0]] == Table.FootSpot);
	}
}

RB_TEST(Integ_Rules_K06_RackFillsVaryWithSeed)
{
	// Not a pattern: over 200 seeds every ball other than the 8 appears on the apex at least once.
	const RulesTable Table = NineFootTable();
	bool SeenOnApex[kRulesBallCount] = {};
	for (std::uint64_t Seed = 0; Seed < 200; ++Seed)
	{
		RackAssignment A;
		RB_REQUIRE(GenerateRack(Discipline::EightBall, RulesConfig{}, Table, Seed, false, kRackGapNone, A) == ErrorCode::Ok);
		SeenOnApex[A.BallAtSite[0]] = true;
	}
	for (int b = 1; b <= 15; ++b)
	{
		RB_CHECK(SeenOnApex[b] == (b != 8));
	}
}
