// Owner: WP-10 (validation & benchmarks). prior-art 9.9 BRK-02 ... BRK-05 (Tier A/D) on the 9-ft pro table (the table's own
// cushions and pockets, VAL cloth, g 9.81), 9-ball racks from rules::GenerateRack with the prior-art 5.6 gap mixture
// (kRackGapMixture, the B2 generator) unless stated. The square stun break: the cue ball on the long string 0.05 m behind the
// head string, aimed at the apex ball's centre with the draw that makes it arrive without spin about the axis across its path
// (e2e::StunDistance, the loop's own strike), a 21 oz break cue whose tip speed sends the cue ball off at the stated BALL speed (10.7 m/s
// = 24 mph, the pro average of prior-art 6.11; radar speeds are ball speeds).

#include "Validation/ValidationUtil.h"

#include "rb/Equipment/Cue.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/TableRules.h"

using namespace rb;
using simtest::kR;

namespace
{
	struct BreakSetup
	{
		int Apex = 1;
		Vec2 Cue;
	};

	// The draw offset B at which a level center-line stroke at Speed reaches Distance without spin across its path (bisection).
	double StunDraw(SimInput& In, StrikeRequest S, double Distance)
	{
		double Lo = -0.5; // strong draw: the spin crosses zero late
		double Hi = 0.0;  // center: the spin is zero at once, then follow builds up
		for (int k = 0; k < 60; ++k)
		{
			S.Input.OffsetB = 0.5 * (Lo + Hi);
			// Crossing before the object ball: more draw (B toward Lo); after it: less.
			(e2e::StunDistance(In, S) < Distance ? Hi : Lo) = S.Input.OffsetB;
		}
		return 0.5 * (Lo + Hi);
	}

	// Planar cue-ball speed right after the strike (the loop's own StrikeCueBall).
	double CueBallSpeed(const SimInput& In, const StrikeRequest& S)
	{
		const PhysicsParams& P = In.Params;
		const StrikeResult Hit = StrikeCueBall(S.Input, In.Balls[S.Ball].State, In.Balls[S.Ball].Spec, P.Cloth, P.Slate, P.Pinch, P.Gravity, P.Numerics);
		return Length(XY(Hit.State.Velocity));
	}

	// The tip speed (and its stun draw) that sends the cue ball off at CueSpeed: prior-art 6.11 / 9.9 give BALL speeds (radar, 24 mph
	// = 10.7 m/s for the pro average); a 21 oz break cue at tip speed V gives the ball about 1.44 V. Bisection on V, each with the
	// draw that arrives without spin (the ball speed falls slightly with the draw offset, B.5).
	void StunStrike(SimInput& In, StrikeRequest& S, double CueSpeed, double Distance)
	{
		double Lo = 0.3 * CueSpeed;
		double Hi = 1.5 * CueSpeed;
		for (int k = 0; k < 60; ++k)
		{
			S.Input.Speed = 0.5 * (Lo + Hi);
			S.Input.OffsetB = StunDraw(In, S, Distance);
			(CueBallSpeed(In, S) < CueSpeed ? Lo : Hi) = S.Input.Speed;
		}
		S.Input.Speed = 0.5 * (Lo + Hi);
		S.Input.OffsetB = StunDraw(In, S, Distance);
	}

	BreakSetup SetupSquareBreak(SimInput& In, std::uint64_t Seed, double CueSpeed, const RackGapParams& Gaps)
	{
		const TableGeometry& T = simtest::NineFoot();
		In = SimInput{};
		In.Table = &T;
		In.Params = val::TableParams(kTableNineFootPro);
		BreakSetup B;
		B.Apex = e2e::PlaceRack(In, T, rules::Discipline::NineBall, rules::RulesPreset::Wpa9Ball, Seed, Gaps, nullptr);
		B.Cue = {-T.HalfLength / 2.0 - 0.05, 0.0};
		simtest::Place(In, 0, ToVec3(B.Cue, kR));
		const Vec2 Apex = XY(In.Balls[B.Apex].State.Position);
		const Vec2 Aim = Apex - B.Cue;
		StrikeRequest S = simtest::Strike(0, CueSpeed, Atan2(Aim.y, Aim.x), 0.0, 0.0, 0.0);
		S.Input.Cue = kCueBreak21oz;
		S.Input.SquirtEnabled = false;
		StunStrike(In, S, CueSpeed, Length(Aim) - 2.0 * kR);
		In.Strikes.PushBack(S);
		In.Context.InHand = CueBallInHand::AboveHeadString;
		In.Context.PlacedPosition = B.Cue;
		return B;
	}

	struct BreakStats
	{
		bool Ok = false;
		double CueFromCenter = kInfinity; // [m], infinity if the cue ball left the table / was pocketed
		double CueX = kInfinity;          // final x of the cue ball [m]
		int Pocketed = 0;                 // object balls
		int CrossedHeadString = 0;        // object balls that crossed the head string at least once
	};

	BreakStats Stats(const ShotResult& R)
	{
		BreakStats S;
		S.Ok = R.Status == SimStatus::Ok;
		if (R.Finals[0].Status == BallFinalStatus::OnTable)
		{
			S.CueFromCenter = Length(XY(R.Finals[0].State.Position));
			S.CueX = R.Finals[0].State.Position.x;
		}
		std::uint32_t Crossed = 0;
		for (const ShotEvent& E : R.Events)
		{
			if (E.Type == ShotEventType::BallLineCross && E.A >= 1 && E.Feature == static_cast<std::uint8_t>(TableLine::HeadString))
			{
				Crossed |= 1u << E.A;
			}
		}
		for (int b = 1; b < kMaxBalls; ++b)
		{
			S.Pocketed += R.Finals[b].Status == BallFinalStatus::Pocketed ? 1 : 0;
			S.CrossedHeadString += ((Crossed >> b) & 1u) != 0 ? 1 : 0;
		}
		return S;
	}

#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kBreaks = 100; // Debug: reduced counts (architecture 18)
#else
	constexpr int kBreaks = 1000;
#endif

	struct BreakSet
	{
		std::vector<BreakStats> Runs;
	};

	const BreakSet& SquareBreaks()
	{
		static BreakSet Set;
		if (Set.Runs.empty())
		{
			static SimInput In;
			static ShotResult R;
			Simulator Sim;
			for (int s = 0; s < kBreaks; ++s)
			{
				SetupSquareBreak(In, 51000u + static_cast<std::uint64_t>(s), 10.7, kRackGapMixture);
				In.Record.Trajectories = false;
				In.Record.EventStates = false;
				Sim.Run(In, R);
				Set.Runs.push_back(Stats(R));
			}
		}
		return Set;
	}
}

// BRK-02: square stun breaks at 10.7 m/s leave the cue ball within 0.3 m of the table centre in >= 50 % of the runs.
RB_TEST(Integ_VAL_BRK02_Slow_SquareBreakCueBallCentered)
{
	const BreakSet& Set = SquareBreaks();
	int Near = 0;
	int NotOk = 0;
	for (const BreakStats& S : Set.Runs)
	{
		Near += S.CueFromCenter <= 0.3 ? 1 : 0;
		NotOk += S.Ok ? 0 : 1;
	}
	const double Share = static_cast<double>(Near) / static_cast<double>(Set.Runs.size());
	std::vector<double> Xs;
	std::vector<double> Distances;
	for (const BreakStats& S : Set.Runs)
	{
		if (S.CueX < kInfinity)
		{
			Xs.push_back(S.CueX);
			Distances.push_back(S.CueFromCenter);
		}
	}
	std::sort(Xs.begin(), Xs.end());
	std::sort(Distances.begin(), Distances.end());
	std::printf("  BRK-02: cue ball within 0.3 m of the centre in %.1f %% of %zu square 10.7 m/s breaks (>= 50 %%) %s; final x median %.3f m "
				"(quartiles %.3f / %.3f), distance from the centre median %.3f m, %zu on the table\n",
		100.0 * Share, Set.Runs.size(), val::Verdict(Share >= 0.5), Xs.empty() ? 0.0 : Xs[Xs.size() / 2], Xs.empty() ? 0.0 : Xs[Xs.size() / 4],
		Xs.empty() ? 0.0 : Xs[3 * Xs.size() / 4], Distances.empty() ? 0.0 : Distances[Distances.size() / 2], Xs.size());
	RB_CHECK(NotOk == 0);
	RB_CHECK(Share >= 0.5);
}

// BRK-03: the distributions of the number of object balls pocketed and of object balls crossing the head string are stable: the
// two halves of the seeded set agree (two-sample KS, p > 0.01), and the whole set agrees with this build's reference histograms
// (recorded below; a later build or another platform that shifts a distribution fails with p <= 0.01).
RB_TEST(Integ_VAL_BRK03_Slow_BreakStatisticsStable)
{
	const BreakSet& Set = SquareBreaks();
	std::vector<double> PocketedA;
	std::vector<double> PocketedB;
	std::vector<double> CrossA;
	std::vector<double> CrossB;
	std::vector<double> PocketedAll;
	std::vector<double> CrossAll;
	int PocketedHist[10] = {};
	int CrossHist[10] = {};
	for (std::size_t k = 0; k < Set.Runs.size(); ++k)
	{
		const BreakStats& S = Set.Runs[k];
		((k & 1u) == 0 ? PocketedA : PocketedB).push_back(S.Pocketed);
		((k & 1u) == 0 ? CrossA : CrossB).push_back(S.CrossedHeadString);
		PocketedAll.push_back(S.Pocketed);
		CrossAll.push_back(S.CrossedHeadString);
		++PocketedHist[S.Pocketed < 9 ? S.Pocketed : 9];
		++CrossHist[S.CrossedHeadString < 9 ? S.CrossedHeadString : 9];
	}
	double D1 = 0.0;
	double D2 = 0.0;
	const double P1 = val::KolmogorovSmirnovP(PocketedA, PocketedB, D1);
	const double P2 = val::KolmogorovSmirnovP(CrossA, CrossB, D2);
	std::printf("  BRK-03 halves: pocketed KS D %.3f p %.3f, head-string crossings KS D %.3f p %.3f (p > 0.01)\n", D1, P1, D2, P2);
	std::printf("  BRK-03 histograms (0..9+): pocketed {");
	for (int h = 0; h < 10; ++h)
	{
		std::printf("%d%s", PocketedHist[h], h < 9 ? ", " : "}, crossings {");
	}
	for (int h = 0; h < 10; ++h)
	{
		std::printf("%d%s", CrossHist[h], h < 9 ? ", " : "}\n");
	}
	RB_CHECK(P1 > 0.01);
	RB_CHECK(P2 > 0.01);
#if !(defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS)
	// Reference histograms of the full Release set (WP-10, 2026-09-28: cue ball off at 10.7 m/s); the KS test against them is the cross-build check.
	constexpr int kRefPocketed[10] = {508, 364, 106, 17, 4, 1, 0, 0, 0, 0};
	constexpr int kRefCross[10] = {0, 2, 21, 83, 219, 380, 235, 58, 2, 0};
	std::vector<double> RefPocketed;
	std::vector<double> RefCross;
	for (int h = 0; h < 10; ++h)
	{
		RefPocketed.insert(RefPocketed.end(), static_cast<std::size_t>(kRefPocketed[h]), static_cast<double>(h));
		RefCross.insert(RefCross.end(), static_cast<std::size_t>(kRefCross[h]), static_cast<double>(h));
	}
	if (!RefPocketed.empty())
	{
		double D3 = 0.0;
		double D4 = 0.0;
		const double P3 = val::KolmogorovSmirnovP(PocketedAll, RefPocketed, D3);
		const double P4 = val::KolmogorovSmirnovP(CrossAll, RefCross, D4);
		std::printf("  BRK-03 against the reference: pocketed p %.3f, crossings p %.3f (p > 0.01)\n", P3, P4);
		RB_CHECK(P3 > 0.01);
		RB_CHECK(P4 > 0.01);
	}
#endif
}

namespace
{
	// A 9-ball rack on the frozen lattice (zero gaps) or with uniform random gaps, the ball ids at the sites permuted by Perm
	// (Perm[site] = id; the apex site holds Perm[0]).
	void SetupPermutedRack(SimInput& In, const int* Perm, const RackGapParams& Gaps, std::uint64_t Seed)
	{
		const TableGeometry& T = simtest::NineFoot();
		In = SimInput{};
		In.Table = &T;
		In.Params = val::TableParams(kTableNineFootPro);
		RackSite Sites[kMaxRackSites];
		const double Diameter = 2.0 * kR;
		const double ApexX = RackApexX(RackShape::Diamond9, RackAnchor::ApexOnFootSpot, T.Landmarks.FootSpot.x, Diameter);
		const int Count = BuildRackLattice(RackShape::Diamond9, ApexX, Diameter, Sites);
		Vec2 Positions[kMaxRackSites];
		for (int k = 0; k < Count; ++k)
		{
			Positions[k] = Sites[k].Position;
		}
		ApplyRackGaps(Positions, Count, RackAnchorSiteIndex(RackShape::Diamond9, RackAnchor::ApexOnFootSpot), Diameter, Gaps, Seed);
		for (int k = 0; k < Count; ++k)
		{
			simtest::Place(In, Perm[k], ToVec3(Positions[k], kR));
		}
		const Vec2 Cue{-T.HalfLength / 2.0 - 0.05, 0.0};
		simtest::Place(In, 0, ToVec3(Cue, kR));
		const Vec2 Aim = Positions[0] - Cue;
		StrikeRequest S = simtest::Strike(0, 10.7, Atan2(Aim.y, Aim.x), 0.0, 0.0, -0.1);
		S.Input.Cue = kCueBreak21oz;
		S.Input.SquirtEnabled = false;
		In.Strikes.PushBack(S);
	}
}

// BRK-04: zero-gap and 50 um random-gap racks both terminate and give different outcomes (chaos); the zero-gap result does not
// depend on which ball id sits on which site (the CLI's id-independent contact order, architecture 8.8): final positions per site
// are bitwise identical under a permutation of the ids.
RB_TEST(Integ_VAL_BRK04_Slow_ZeroGapRackIdIndependent)
{
	const int Identity[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	const int Permuted[9] = {7, 3, 9, 1, 8, 2, 6, 4, 5};
	static SimInput In;
	static ShotResult A;
	static ShotResult B;
	static ShotResult G;
	Simulator Sim;
	SetupPermutedRack(In, Identity, kRackGapNone, 1u);
	RB_REQUIRE(Sim.Run(In, A) == SimStatus::Ok);
	SetupPermutedRack(In, Permuted, kRackGapNone, 1u);
	RB_REQUIRE(Sim.Run(In, B) == SimStatus::Ok);
	int SameSites = 0;
	for (int k = 0; k < 9; ++k)
	{
		const BallFinal& FA = A.Finals[Identity[k]];
		const BallFinal& FB = B.Finals[Permuted[k]];
		SameSites += FA.Status == FB.Status && simtest::Bits(FA.State.Position.x) == simtest::Bits(FB.State.Position.x) &&
				simtest::Bits(FA.State.Position.y) == simtest::Bits(FB.State.Position.y) && FA.Pocket == FB.Pocket
			? 1
			: 0;
	}
	const bool CueSame = simtest::Bits(A.Finals[0].State.Position.x) == simtest::Bits(B.Finals[0].State.Position.x) &&
		simtest::Bits(A.Finals[0].State.Position.y) == simtest::Bits(B.Finals[0].State.Position.y);
	const RackGapParams Gaps50{0.05e-3, 0.05e-3, 0.0, 0.0, 0.0}; // 0 - 100 um, mean 50 um
	SetupPermutedRack(In, Identity, Gaps50, 7u);
	RB_REQUIRE(Sim.Run(In, G) == SimStatus::Ok);
	double Difference = 0.0;
	for (int b = 0; b <= 9; ++b)
	{
		if (A.Finals[b].Status == BallFinalStatus::OnTable && G.Finals[b].Status == BallFinalStatus::OnTable)
		{
			Difference = Max(Difference, Length(A.Finals[b].State.Position - G.Finals[b].State.Position));
		}
		else if (A.Finals[b].Status != G.Finals[b].Status)
		{
			Difference = kInfinity;
		}
	}
	std::printf("  BRK-04: permuted ids give %d/9 identical object-ball finals per site, cue ball %s; 50 um gaps move a ball by %.3f m\n", SameSites,
		CueSame ? "identical" : "DIFFERENT", Difference);
	RB_CHECK(SameSites == 9 && CueSame);
	RB_CHECK(Difference > 0.01);
}

// BRK-05: the mean kinetic energy delivered to the object balls grows monotonically with the break speed (7.6 -> 13.4 m/s). The
// energy is taken just after the break's first island ends (every object ball back in event mode), averaged over seeded racks.
RB_TEST(Integ_VAL_BRK05_Slow_EnergyGrowsWithBreakSpeed)
{
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kSeeds = 5;
#else
	constexpr int kSeeds = 40;
#endif
	const double Speeds[6] = {7.6, 8.8, 10.0, 11.2, 12.4, 13.4};
	double Mean[6] = {};
	static SimInput In;
	static ShotResult R;
	Simulator Sim;
	bool Monotone = true;
	for (int v = 0; v < 6; ++v)
	{
		double Sum = 0.0;
		int Count = 0;
		for (int s = 0; s < kSeeds; ++s)
		{
			SetupSquareBreak(In, 61000u + static_cast<std::uint64_t>(s), Speeds[v], kRackGapMixture);
			if (Sim.Run(In, R) != SimStatus::Ok)
			{
				continue;
			}
			const ShotEvent* End = e2e::FirstEvent(R, ShotEventType::IslandEnd);
			if (End == nullptr)
			{
				continue;
			}
			const double T = End->Time + 1e-6;
			double Energy = 0.0;
			bool Valid = true;
			for (int b = 1; b < kMaxBalls && Valid; ++b)
			{
				if (!In.Balls[b].InPlay)
				{
					continue;
				}
				BallState S;
				SegmentKind Kind = SegmentKind::Analytic;
				if (!simtest::TrackState(R, b, T, S, &Kind))
				{
					continue;
				}
				if (Kind == SegmentKind::Sampled)
				{
					Valid = false; // still in an island (a later cluster): skip this break
					break;
				}
				if (Kind == SegmentKind::Analytic)
				{
					const BallSpec& Spec = In.Balls[b].Spec;
					Energy += 0.5 * Spec.Mass * LengthSquared(S.Velocity) + 0.5 * Spec.Inertia * LengthSquared(S.Omega);
				}
			}
			if (Valid)
			{
				Sum += Energy;
				++Count;
			}
		}
		Mean[v] = Count > 0 ? Sum / Count : 0.0;
		if (v > 0 && !(Mean[v] > Mean[v - 1]))
		{
			Monotone = false;
		}
		std::printf("  BRK-05: %.1f m/s -> mean object-ball kinetic energy %.3f J (%d breaks)\n", Speeds[v], Mean[v], Count);
	}
	RB_CHECK(Monotone);
}
