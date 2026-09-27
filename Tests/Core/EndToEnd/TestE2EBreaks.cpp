// Integration round 2 (end-to-end, every package): full 9-ball breaks through the whole core - WP-2 geometry and ball sets,
// WP-9 rack generation, WP-1 strike, WP-6a loop, WP-6b islands / pockets, WP-3 CLI, WP-4 cushions and pockets, WP-5 detection,
// WP-7 record, WP-8 facts. Architecture tests A-E2E-1 (9-ft table) and A-E2E-2 (7-ft dive-bar table, oversized cue ball).
// Invariants (prior-art ROB-11 / ROB-12 / ROB-10 applied to one real break each): every ball comes to rest, no final overlap
// beyond 1 um, no ball inside a cushion, energy never increases between events, clean diagnostics, bitwise determinism across
// two independent simulators. Physical expectations: the cue ball's first contact is the apex ball, and a full-speed break is
// legal by WPA 9-ball (at least four object balls driven to a rail or a ball pocketed, rules.md 5.3 / F3).

#include "rbtest.h"

#include "EndToEnd/EndToEndUtil.h"

#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Shot/ShotRecordBuilder.h"

using namespace rb;

namespace
{
	const TableGeometry& Geometry(const TableSpec& Spec) { return simtest::Table(Spec); }

	// A 9-ball break on T with the ball set Set (nullptr: standard pool balls): wooden-rack micro-gaps, the cue ball 10 cm behind
	// the head string, 12 cm off the long string, a break cue at Speed aimed at the apex ball's center, slight draw.
	int SetupNineBallBreak(SimInput& In, const TableGeometry& T, const BallSet* Set, std::uint64_t Seed, double Speed)
	{
		In = SimInput{};
		In.Table = &T;
		In.Params = MakePhysicsParams(T.Spec);
		const int Apex = e2e::PlaceRack(In, T, rules::Discipline::NineBall, rules::RulesPreset::Wpa9Ball, Seed, kRackGapWoodenRack, Set);
		const BallSpec CueBall = Set != nullptr ? Set->Balls[0] : MakeBallSpec(simtest::kR, kDefaultBallMass);
		const Vec3 Cue{-T.HalfLength / 2.0 - 0.10, 0.12, CueBall.Radius};
		simtest::Place(In, 0, Cue, {}, {}, CueBall);
		const Vec3 Aim = In.Balls[Apex].State.Position - Cue;
		StrikeRequest Break = simtest::Strike(0, Speed, std::atan2(Aim.y, Aim.x), 0.0, 0.0, -0.1);
		Break.Input.Cue = kCueBreak21oz;
		In.Strikes.PushBack(Break);
		In.Context.InHand = CueBallInHand::AboveHeadString;
		In.Context.PlacedPosition = XY(Cue);
		return Apex;
	}

	void CheckBreak(const TableGeometry& T, const BallSet* Set, std::uint64_t Seed, double Speed, const char* Label)
	{
		static SimInput In;
		static ShotResult R;
		static ShotResult Again;
		const int Apex = SetupNineBallBreak(In, T, Set, Seed, Speed);
		Simulator Sim;
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);

		const e2e::ShotInvariants I = e2e::CheckInvariants(R, In);
		std::printf("  %s: stop %.3f s, %d events, %d islands (%d steps), final overlap %.3g m, cushion depth %.3g m, energy rise %.3g J over %d steps\n", Label,
			R.StopTime, R.Diagnostics.EventsProcessed, R.Diagnostics.Islands, R.Diagnostics.IslandSteps, I.Overlap, I.CushionPenetration, I.EnergyRise,
			I.EnergyComparisons);
		RB_CHECK(I.AtRest);
		RB_CHECK(I.Overlap <= 1e-6);
		RB_CHECK(I.CushionPenetration <= 1e-6);
		RB_CHECK(I.EnergyComparisons >= 20);
		RB_CHECK(I.EnergyRise <= 1e-9);
		RB_CHECK(I.Clean);
		if (!I.AtRest || !I.Clean || I.Overlap > 1e-6)
		{
			e2e::Dump(R, 80);
		}

		// Bitwise determinism: a second, independent simulator gives the identical result.
		Simulator Other;
		RB_REQUIRE(Other.Run(In, Again) == SimStatus::Ok);
		RB_CHECK(e2e::BitwiseEqual(R, Again));

		// The rules read the real record: first contact on the apex ball, a legal (open) break.
		double BallRadii[kMaxBalls] = {};
		for (int b = 0; b < kMaxBalls; ++b)
		{
			BallRadii[b] = In.Balls[b].InPlay ? In.Balls[b].Spec.Radius : 0.0;
		}
		const rules::RulesTable Rules = BuildRulesTable(T, simtest::kR, BallRadii, kMaxBalls);
		rules::ShotFacts Facts;
		rules::DeriveShotFacts(R.Record, Rules, RulesTolerances{}, kInfinity, Facts);
		RB_CHECK(!Facts.RecordTruncated);
		RB_CHECK(Facts.EarliestContact == Apex);
		RB_CHECK(Facts.NumObjectBallsDrivenToRail >= 4 || Facts.AnyObjectBallPocketed);
		RB_CHECK(!Facts.DoubleHit && !Facts.PushShot && !Facts.Miscue);
		int Moved = 0;
		for (int b = 1; b <= 9; ++b)
		{
			const bool Gone = R.Finals[b].Status != BallFinalStatus::OnTable;
			Moved += Gone || Length(R.Finals[b].State.Position - In.Balls[b].State.Position) > 0.05 ? 1 : 0;
		}
		std::printf("  %s: first contact %d (apex %d), %d object balls to a rail, %d pocketed, %d of 9 moved > 5 cm\n", Label,
			static_cast<int>(Facts.EarliestContact), Apex, Facts.NumObjectBallsDrivenToRail, Facts.Pocketed.Size(), Moved);
		RB_CHECK(Moved >= 7); // the rack is broken open
		RB_CHECK(R.StopTime > 2.0 && R.StopTime < 60.0);
	}
}

// A-E2E-1: a 9 m/s 9-ball break on the 9-ft pro table with standard balls (MakePhysicsParams: the table's own cloth, cushions and
// pockets).
RB_TEST(Integ_ARCH_E2E1_NineBallBreakNineFoot)
{
	CheckBreak(Geometry(kTableNineFootPro), nullptr, 11u, 9.0, "9-ft break");
}

// A-E2E-2: the same break on the 7-ft dive-bar table (napped bar cloth, soft facings, bar pockets) with the old-bar ball set: an
// oversized 60.3 mm / 221 g cue ball and seeded bar object balls of 57.00-57.15 mm and 155-167 g (per-ball radius, mass and
// inertia everywhere, equipment 6.3).
RB_TEST(Integ_ARCH_E2E2_NineBallBreakSevenFootDiveBarOversizedCueBall)
{
	static BallSet Set;
	RB_REQUIRE(BuildBallSet(BallSetPreset::OldBarOversizedCue, 7u, Set) == ErrorCode::Ok);
	RB_REQUIRE(Set.Balls[0].Radius > 0.030 && Set.Balls[0].Mass > 0.22);
	CheckBreak(Geometry(kTableSevenFootBar), &Set, 23u, 9.0, "7-ft dive-bar break");
}
