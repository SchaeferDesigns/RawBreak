// Owner: WP-10 (validation & benchmarks). The piecewise detection proxy of the drop-edge pivot (collisions 5.4; the WP-10 fix of
// prior-art ROB-11): architecture test A-VAL-1. The whole-arc quadratic of PivotDetectionProxy deviates from the true circular path
// by up to ~0.1 mm, and contacts found on it were resolved with the balls up to 52 um inside each other (ROB-11 on the full B2
// set). MakePivotProxyPiece covers the pivot with quadratic pieces through exact points of the true path whose DERIVED error bound
// is <= kPivotProxyTolerance; this test checks the bound against dense sampling of the true path (EvaluatePivot) for random edge
// crossings (normal speed, speed along the edge, inertia factor, rounding radius), that the pieces tile [0, T_p] without gaps, and
// that their number stays small (the loop pays one re-prediction per piece).

#include "rbtest.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/PocketDrop.h"

#include "Simulator/SimTestUtil.h"

#include <cstdio>

using namespace rb;

namespace
{
	struct PieceStats
	{
		int Paths = 0;
		int Pieces = 0;
		int MaxPieces = 0;
		double WorstDeviation = 0.0; // sampled |true - piece| [m]
		double WorstBound = 0.0;     // the pieces' own bound [m]
		double WorstGap = 0.0;       // |node time mismatch| [s]
		int BoundViolations = 0;     // sampled deviation above the tolerance
	};

	// Walks the pieces of Path like the loop does (each piece from the previous one's end time and U) and samples each piece.
	void CheckPath(const PivotPath& Path, double Tolerance, PieceStats& S)
	{
		double U = 0.0;
		double T = Path.T0;
		int Count = 0;
		for (int k = 0; k < 10000; ++k)
		{
			const PivotProxyPiece P = MakePivotProxyPiece(Path, U, T, Tolerance);
			++Count;
			S.WorstBound = Max(S.WorstBound, P.Deviation);
			S.WorstGap = Max(S.WorstGap, Abs(P.Seg.T0 - T));
			constexpr int kSamples = 64;
			for (int i = 0; i <= kSamples; ++i)
			{
				const double Tau = P.Seg.TauEnd * static_cast<double>(i) / kSamples;
				const Vec3 OnPiece = PositionAt(P.Seg, Tau);
				const Vec3 True = EvaluatePivot(Path, P.Seg.T0 + Tau - Path.T0).Position;
				const double D = Length(OnPiece - True);
				S.WorstDeviation = Max(S.WorstDeviation, D);
				S.BoundViolations += D > Tolerance ? 1 : 0;
			}
			if (P.Last)
			{
				S.WorstGap = Max(S.WorstGap, Abs(P.Seg.T0 + P.Seg.TauEnd - (Path.T0 + Path.Result.Duration)));
				break;
			}
			U = P.UpperU;
			T = P.Seg.T0 + P.Seg.TauEnd;
		}
		++S.Paths;
		S.Pieces += Count;
		S.MaxPieces = Count > S.MaxPieces ? Count : S.MaxPieces;
	}
}

// A-VAL-1: pieces of random pivots stay within kPivotProxyTolerance of the true path (sampled), tile the pivot exactly, and
// their number stays bounded (mean <= 100, max <= 1000 per pivot).
RB_TEST(Integ_ARCH_VAL1_PivotProxyPiecesWithinTolerance)
{
	TableGeometry Table;
	RB_REQUIRE(BuildTableGeometry(kTableNineFootPro, Table) == ErrorCode::Ok);
	const PocketGeometry& Corner = Table.Pockets[0];
	const PocketGeometry& Side = Table.Pockets[1];
	Rng Random(2026);
	PieceStats S;
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kPaths = 60;
#else
	constexpr int kPaths = 400;
#endif
	for (int n = 0; n < kPaths; ++n)
	{
		const PocketGeometry& G = (n & 1) != 0 ? Side : Corner;
		const double Radius = Random.NextUniform(0.0262, 0.0302);
		const double K = n % 7 == 0 ? Random.NextUniform(0.3, 0.6) : kSolidSphereInertiaFactor;
		const BallSpec Spec{Radius, 0.17, K * 0.17 * Radius * Radius};
		// A point on the drop-edge circle inside the front arc, with a normal speed below sqrt(g rho) and any speed along the edge.
		const double Angle = G.FrontArcFrom + Random.NextUniform(0.1, 0.9) * G.FrontArcSweep;
		const Vec2 Dir{Cos(Angle), Sin(Angle)};
		const Vec2 At = G.CaptureCenter + Dir * G.DropEdgeRadius;
		const double Vn = n % 5 == 0 ? Random.NextUniform(0.0, 0.002) : Random.NextUniform(0.0, 0.56);
		const double Vt = Random.NextUniform(-1.0, 1.0);
		BallState B;
		B.Position = ToVec3(At, Radius);
		B.Velocity = ToVec3(-Dir * Vn + PerpCcw(Dir) * Vt);
		B.Omega = RollingOmegaH(B.Velocity, Radius);
		B.State = MotionState::Rolling;
		const PivotPath Path = MakePivotPath(B, 1.25, G, Spec, simtest::kGVal, NumericsConfig{});
		if (Path.Result.Immediate)
		{
			continue;
		}
		CheckPath(Path, kPivotProxyTolerance, S);
	}
	std::printf("  A-VAL-1: %d pivots, %d pieces (mean %.1f, max %d), sampled deviation max %.3g m, bound max %.3g m (tolerance %.1g m), node mismatch %.3g s\n",
		S.Paths, S.Pieces, S.Paths > 0 ? static_cast<double>(S.Pieces) / S.Paths : 0.0, S.MaxPieces, S.WorstDeviation, S.WorstBound, kPivotProxyTolerance,
		S.WorstGap);
	RB_CHECK(S.Paths > kPaths / 2);
	RB_CHECK(S.BoundViolations == 0);
	RB_CHECK(S.WorstDeviation <= kPivotProxyTolerance);
	RB_CHECK(S.WorstBound <= kPivotProxyTolerance);
	RB_CHECK(S.WorstGap <= 1e-12);
	// Cost: one node event (a re-prediction of the ball) per piece. The longest pivots here (v0 ~ 0, T_p ~ 0.3 s, rolling along
	// the edge at up to 1 m/s - in play a facing truncates such a pivot early) need a few hundred; the mean is a few dozen
	// (B1 shots: about 35 per pivot at 1e-7 m, 9 % of the shots pivot). Far below the event cap.
	RB_CHECK(S.MaxPieces <= 1000);
	RB_CHECK(static_cast<double>(S.Pieces) <= 100.0 * S.Paths);
}

// A-VAL-1b (WP-10 review, adversarial): the piece bound at the edges of the input space A-VAL-1 does not sample - every pocket of
// the 9-ft pro and TABLE_7FT_78 (another drop radius, the smallest rounding-axis circle), normal speeds at the PivotMinSpeed floor
// and just below the immediate-leave threshold sqrt(g rho), speeds along the edge up to 4 m/s (a ball skimming along a corner
// pocket's lip; the meridian plane turns fastest), balls from 50.8 mm (the smallest set) to 61 mm, late start times (the pieces
// accumulate absolute time: t0 up to 60 s) and the tolerances 1e-6 and 1e-8 besides the simulator's 1e-7. Each piece keeps
// the true path within its tolerance (sampled from each piece's start, so every node lies on the true path at its accumulated
// absolute time), the bound it reports is within the tolerance, the last piece ends at t0 + T_p (1e-12 s), every piece but a
// last one has a positive span and the count stays bounded.
RB_TEST(Integ_ARCH_VAL1b_PivotProxyPiecesAdversarial)
{
	TableGeometry Pro;
	TableGeometry Small;
	RB_REQUIRE(BuildTableGeometry(kTableNineFootPro, Pro) == ErrorCode::Ok);
	RB_REQUIRE(BuildTableGeometry(kTableSevenFoot78, Small) == ErrorCode::Ok);
	Rng Random(0x5EEDu);
	const double Tolerances[3] = {1e-6, kPivotProxyTolerance, 1e-8};
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kPaths = 40;
#else
	constexpr int kPaths = 300;
#endif
	int Paths = 0;
	int Violations = 0;
	int BoundOver = 0;
	int EmptyPieces = 0;
	int MaxPieces[3] = {};
	double WorstRatio = 0.0; // sampled deviation / tolerance
	double WorstNode = 0.0;  // |piece start - (t0 + tau(U))| [s]
	for (int n = 0; n < kPaths; ++n)
	{
		const TableGeometry& T = (n % 3) == 2 ? Small : Pro;
		const PocketGeometry& G = T.Pockets[n % T.Pockets.Size()];
		const double Radius = Random.NextUniform(0.0254, 0.0305);
		const double K = n % 4 == 0 ? Random.NextUniform(0.3, 0.6) : kSolidSphereInertiaFactor;
		const BallSpec Spec{Radius, 0.17, K * 0.17 * Radius * Radius};
		const double Angle = G.FrontArcFrom + Random.NextUniform(0.02, 0.98) * G.FrontArcSweep;
		const Vec2 Dir{Cos(Angle), Sin(Angle)};
		const double Rho = Radius + G.DropRadius;
		const double Immediate = Sqrt(simtest::kGVal * Rho);
		double Vn = 0.0;
		switch (n % 3)
		{
		case 0: Vn = Random.NextUniform(0.0, 2e-4); break;                              // at the PivotMinSpeed floor
		case 1: Vn = Immediate * (1.0 - Random.NextUniform(1e-9, 1e-3)); break;         // just below the immediate leave
		default: Vn = Random.NextUniform(0.0, Immediate); break;
		}
		const double Vt = Random.NextUniform(-4.0, 4.0);
		BallState B;
		B.Position = ToVec3(G.CaptureCenter + Dir * G.DropEdgeRadius, Radius);
		B.Velocity = ToVec3(-Dir * Vn + PerpCcw(Dir) * Vt);
		B.Omega = RollingOmegaH(B.Velocity, Radius);
		B.State = MotionState::Rolling;
		const double T0 = Random.NextUniform(0.0, 60.0);
		const PivotPath Path = MakePivotPath(B, T0, G, Spec, simtest::kGVal, NumericsConfig{});
		if (Path.Result.Immediate)
		{
			continue;
		}
		++Paths;
		for (int t = 0; t < 3; ++t)
		{
			const double Tolerance = Tolerances[t];
			double U = 0.0;
			double Start = Path.T0;
			int Count = 0;
			for (int k = 0; k < 100000; ++k)
			{
				const PivotProxyPiece P = MakePivotProxyPiece(Path, U, Start, Tolerance);
				++Count;
				BoundOver += P.Deviation > Tolerance ? 1 : 0;
				EmptyPieces += !P.Last && !(P.Seg.TauEnd > 0.0) ? 1 : 0;
				constexpr int kSamples = 48;
				for (int i = 0; i <= kSamples; ++i)
				{
					const double Tau = P.Seg.TauEnd * static_cast<double>(i) / kSamples;
					const double D = Length(PositionAt(P.Seg, Tau) - EvaluatePivot(Path, P.Seg.T0 + Tau - Path.T0).Position);
					WorstRatio = Max(WorstRatio, D / Tolerance);
					Violations += D > Tolerance ? 1 : 0;
				}
				if (P.Last)
				{
					WorstNode = Max(WorstNode, Abs(P.Seg.T0 + P.Seg.TauEnd - (Path.T0 + Path.Result.Duration)));
					break;
				}
				U = P.UpperU;
				Start = P.Seg.T0 + P.Seg.TauEnd;
			}
			MaxPieces[t] = Count > MaxPieces[t] ? Count : MaxPieces[t];
		}
	}
	std::printf("  A-VAL-1b: %d pivots, max pieces %d / %d / %d at 1e-6 / 1e-7 / 1e-8 m, sampled deviation / tolerance max %.3f, bounds over the "
				"tolerance %d, empty pieces %d, end-node time error %.3g s\n",
		Paths, MaxPieces[0], MaxPieces[1], MaxPieces[2], WorstRatio, BoundOver, EmptyPieces, WorstNode);
	RB_CHECK(Paths > kPaths / 2);
	RB_CHECK(Violations == 0);
	RB_CHECK(BoundOver == 0);
	RB_CHECK(EmptyPieces == 0);
	RB_CHECK(WorstNode <= 1e-12);
	// The simulator's tolerance: one node event per piece, far below the event cap (MaxEvents 20 000) even for a ball skimming
	// along the lip at 4 m/s with v0 at the floor (~900 pieces: its meridian plane would turn about the pocket axis for ~0.3 s;
	// in play the facing truncates such a pivot within a few centimetres).
	RB_CHECK(MaxPieces[1] <= 2000);
}
