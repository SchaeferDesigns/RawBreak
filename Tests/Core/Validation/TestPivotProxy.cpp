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
