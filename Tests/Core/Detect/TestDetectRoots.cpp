// Owner: WP-5 (event detection). Solver agreement and root robustness: physics-collisions 9.2 D-10, prior-art 9.11
// ROOT-01 and ROOT-02.
//
// Reference (spec ambiguity resolved by WP-5): the spec names a companion-matrix + Newton reference (D-10) and a 256-bit
// MPFR brute force (ROOT-01); neither exists in the dependency-free harness. Both tests use a 106-bit double-double
// reference built from the EXACT segment data (expansion to the common origin, coefficients and every evaluation in
// double-double), with critical-point isolation and pure bisection to adjacent doubles (DetectTestUtil.h). Grazes are
// allowed to disagree as hit / miss when the reference's run minimum is within 2 eps_f of zero or the reference approach
// speed is below 1e-6 m/s (D-10 wording). Counts: 1e4 cases in Debug and 1e5 in Release by default (architecture 18:
// heavy property tests run a reduced count), the full 1e6 in the _Slow_ variants (nightly).

#include "rbtest.h"

#include "DetectTestUtil.h"

#include "rb/Core/Random.h"

#include <chrono>
#include <cstdio>

using namespace detecttest;
using rb::MotionSegment;

namespace
{
#ifdef NDEBUG
	constexpr int kDefaultCases = 100000;
#else
	constexpr int kDefaultCases = 10000;
#endif

	Vec3 RandomUnitPlan(rb::Rng& Rng)
	{
		const double A = Rng.NextUniform(0.0, rb::kTwoPi);
		return {rb::Cos(A), rb::Sin(A), 0.0};
	}

	// One random segment of any kind on a 9-ft bed (optionally with an own time base).
	MotionSegment RandomSegment(rb::Rng& Rng, double T0, double R, bool GeneralQuadratic)
	{
		const Vec3 P{Rng.NextUniform(-1.2, 1.2), Rng.NextUniform(-0.6, 0.6), R};
		const std::uint32_t Kind = Rng.NextBelow(10);
		if (Kind < 4)
		{
			MotionSegment S = Rolling(P, RandomUnitPlan(Rng) * Rng.NextUniform(0.005, 4.0), kMuR, kG, T0, R);
			if (GeneralQuadratic && Rng.NextBelow(2) == 0)
			{
				// Tilt chain piece (architecture 8.11): Accel2 NOT parallel to Vel0.
				S.Accel2 = S.Accel2 + RandomUnitPlan(Rng) * Rng.NextUniform(0.0, 0.03);
				S.Tilt.Active = true;
			}
			return S;
		}
		if (Kind < 7)
		{
			const Vec3 V = RandomUnitPlan(Rng) * Rng.NextUniform(0.0, 5.0);
			const Vec3 W{Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-50.0, 50.0)};
			if (rb::Length(rb::Planar(rb::SlipVelocity(V, W, R))) < 1e-3)
			{
				return Stationary(P, T0);
			}
			return Sliding(P, V, W, kMuS, kG, T0, R);
		}
		if (Kind < 9)
		{
			const Vec3 Up{P.x, P.y, R + Rng.NextUniform(0.0, 0.08)};
			return Airborne(Up, RandomUnitPlan(Rng) * Rng.NextUniform(0.0, 4.0) + Vec3{0.0, 0.0, Rng.NextUniform(0.05, 2.5)}, kG, T0, R);
		}
		return Stationary(P, T0);
	}

	// Shifts B (in plan) so that at a random common time both centers are within Spread of each other: hits, misses and
	// grazes are then all frequent.
	void AimAt(rb::Rng& Rng, const MotionSegment& A, MotionSegment& B, double Spread)
	{
		const double RefTime = rb::Max(A.T0, B.T0);
		const double End = rb::Min(rb::Min(A.T0 + A.TauEnd, B.T0 + B.TauEnd), RefTime + 3.0);
		if (!(End > RefTime))
		{
			return;
		}
		const double t = Rng.NextUniform(RefTime, End);
		const Vec3 Offset = RandomUnitPlan(Rng) * Rng.NextUniform(0.0, Spread);
		const Vec3 Shift = rb::Planar(rb::PositionAt(A, t - A.T0) - rb::PositionAt(B, t - B.T0)) + Offset;
		B.Pos0 = B.Pos0 + Shift;
	}

	struct AgreementStats
	{
		int Cases = 0;
		int Hits = 0;
		int GrazeDisagreements = 0;
		int Violations = 0;
		double MaxTimeError = 0.0; // [s] absolute (D-10) or relative to max(1 s, t) (ROOT-01)
	};

	// One case of D-10 / ROOT-01: PredictBallBall against the reference. Times must agree to 1e-12 s (D-10: absolute;
	// ROOT-01: "1e-12 s or relative 1e-12"). A hit / miss disagreement (or an unchecked time) is allowed only for a graze:
	// the reference's run minimum within eps_f of zero (0.1 % allowance for the double rounding of the solver's own f_min,
	// ~1e-15 m^2 against eps_f = 1.1e-10 m^2) or the reference approach speed below 1e-6 m/s (D-10 wording).
	void CompareCase(const MotionSegment& A, double RA, const MotionSegment& B, double RB, bool RelativeTime, AgreementStats& Stats)
	{
		const double RefTime = rb::Max(A.T0, B.T0);
		const double TauMax = rb::Min(A.T0 + A.TauEnd, B.T0 + B.TauEnd) - RefTime;
		const double EpsF = 2.0 * (RA + RB) * 1e-9;
		const double GrazeBand = EpsF * 1.001;
		const rb::ContactPrediction P = rb::PredictBallBall(A, RA, B, RB, rb::kInfinity, Numerics());
		const ReferenceEntry E = ReferenceBallBallEntry(A, RA, B, RB, TauMax);
		++Stats.Cases;
		const bool NearGraze = E.Found && (rb::Abs(E.RunMinimum) <= GrazeBand || E.ApproachSpeed < 1e-6);
		if (P.Found && E.Found)
		{
			++Stats.Hits;
			const double RefAbs = RefTime + E.Tau;
			const double Error = rb::Abs(P.Time - RefAbs);
			if (!NearGraze)
			{
				const double Scaled = RelativeTime ? Error / rb::Max(1.0, RefAbs) : Error;
				Stats.MaxTimeError = rb::Max(Stats.MaxTimeError, Scaled);
				if (Scaled > 1e-12)
				{
					++Stats.Violations;
				}
			}
			return;
		}
		if (P.Found == E.Found)
		{
			return;
		}
		// Hit / miss disagreement: allowed only for grazes. When the reference misses, look at its closest approach: the
		// interior minima and the window end of f.
		bool Graze = NearGraze;
		if (!E.Found)
		{
			const rb::Polynomial F = rb::BallBallGapPolynomial(A, RA, B, RB, RefTime);
			double Crit[8];
			const int NumCrit = rb::SolveInInterval(F.Derivative(), 0.0, TauMax, Crit);
			for (int i = 0; i < NumCrit; ++i)
			{
				if (rb::Abs(F.Eval(Crit[i])) <= GrazeBand)
				{
					Graze = true;
				}
			}
			if (rb::Abs(F.Eval(TauMax)) <= GrazeBand)
			{
				Graze = true;
			}
		}
		if (Graze)
		{
			++Stats.GrazeDisagreements;
		}
		else
		{
			++Stats.Violations;
		}
	}

	void RunD10(int Cases, AgreementStats& Stats)
	{
		rb::Rng Rng(0xD10D10D10ull);
		while (Stats.Cases < Cases)
		{
			const MotionSegment A = RandomSegment(Rng, 0.0, kR, false);
			MotionSegment B = RandomSegment(Rng, 0.0, kR, false);
			if (Rng.NextBelow(2) == 0)
			{
				AimAt(Rng, A, B, 2.5 * kR);
			}
			if (rb::Length(B.Pos0 - A.Pos0) < 2.0 * kR + 1e-6)
			{
				continue; // start separated (touching starts are D-7 / D-12)
			}
			CompareCase(A, kR, B, kR, false, Stats);
		}
	}

	void RunRoot01(int Cases, AgreementStats& Stats)
	{
		rb::Rng Rng(0x0001ull);
		while (Stats.Cases < Cases)
		{
			// Own time bases, per-ball radii, general quadratics, near-graze aiming and slow approaches.
			const double RA = Rng.NextBelow(8) == 0 ? 0.0301625 : kR;
			const double RB = kR;
			const MotionSegment A = RandomSegment(Rng, Rng.NextUniform(0.0, 2.0), RA, true);
			MotionSegment B = RandomSegment(Rng, Rng.NextUniform(0.0, 2.0), RB, true);
			const std::uint32_t Mode = Rng.NextBelow(4);
			if (Mode == 0)
			{
				AimAt(Rng, A, B, RA + RB + Rng.NextUniform(-1e-5, 1e-5)); // grazing and near-grazing impact parameters
			}
			else if (Mode == 1)
			{
				AimAt(Rng, A, B, 3.0 * kR);
			}
			const double RefTime = rb::Max(A.T0, B.T0);
			const Vec3 Gap = rb::PositionAt(B, RefTime - B.T0) - rb::PositionAt(A, RefTime - A.T0);
			if (!(rb::Min(A.T0 + A.TauEnd, B.T0 + B.TauEnd) > RefTime) || rb::Length(Gap) < RA + RB + 1e-6)
			{
				continue;
			}
			CompareCase(A, RA, B, RB, true, Stats);
		}
	}

	// Random polynomials for ROOT-02 on the scaled detection domain [0, 1]: clustered, double and near-double roots,
	// coefficients spanning 1e-8 ... 1e8.
	rb::Polynomial RandomHardPolynomial(rb::Rng& Rng)
	{
		rb::Polynomial P;
		P.Degree = 0;
		P.c[0] = rb::Pow(10.0, Rng.NextUniform(-8.0, 8.0)) * (Rng.NextBelow(2) == 0 ? 1.0 : -1.0);
		if (Rng.NextBelow(5) == 0)
		{
			// Unstructured coefficients of random magnitude 1e-8 ... 1e8.
			P.Degree = 2 + static_cast<int>(Rng.NextBelow(7));
			for (int i = 0; i <= P.Degree; ++i)
			{
				P.c[i] = rb::Pow(10.0, Rng.NextUniform(-8.0, 8.0)) * (Rng.NextBelow(2) == 0 ? 1.0 : -1.0);
			}
			return P;
		}
		const int Degree = 2 + static_cast<int>(Rng.NextBelow(7));
		double Base = Rng.NextDouble01();
		while (P.Degree < Degree)
		{
			double Root = Base;
			const std::uint32_t Pattern = Rng.NextBelow(6);
			if (Pattern == 0)
			{
				Root = Base + rb::Pow(10.0, Rng.NextUniform(-12.0, -3.0)); // cluster
			}
			else if (Pattern == 1 || Pattern == 2)
			{
				Base = Rng.NextUniform(-0.5, 1.5);
				Root = Base;
			}
			const bool Double = Pattern == 3 && P.Degree + 2 <= Degree;
			const int Times = Double ? 2 : 1;
			for (int t = 0; t < Times; ++t)
			{
				rb::Polynomial Next;
				Next.Degree = P.Degree + 1;
				for (int i = 0; i <= P.Degree; ++i)
				{
					Next.c[i + 1] += P.c[i];
					Next.c[i] -= Root * P.c[i];
				}
				P = Next;
			}
			if (Pattern == 4 && P.Degree + 2 <= Degree)
			{
				// Complex pair (near-graze): (x - Base)^2 + tiny.
				const double Tiny = rb::Pow(10.0, Rng.NextUniform(-16.0, -4.0));
				rb::Polynomial Next;
				Next.Degree = P.Degree + 2;
				const double Q[3] = {Base * Base + Tiny, -2.0 * Base, 1.0};
				for (int i = 0; i <= P.Degree; ++i)
				{
					for (int j = 0; j < 3; ++j)
					{
						Next.c[i + j] += P.c[i] * Q[j];
					}
				}
				P = Next;
			}
		}
		return P;
	}

	int SignOf(double v) { return (v > 0.0) - (v < 0.0); }
}

RB_TEST(COL_D10_SolverAgreesWithHighPrecisionReference)
{
	AgreementStats Stats;
	RunD10(kDefaultCases, Stats);
	RB_CHECK(Stats.Violations == 0);
	RB_CHECK(Stats.Hits > Stats.Cases / 10); // the aimed half produces plenty of hits
	RB_CHECK(Stats.GrazeDisagreements * 1000 < Stats.Cases);
	RB_CHECK(Stats.MaxTimeError <= 1e-12);
}

RB_TEST(COL_D10_Slow_MillionCases)
{
	AgreementStats Stats;
	RunD10(1000000, Stats);
	std::printf("  D-10: %d cases, %d hits, %d graze disagreements, %d violations, max error %.3g s\n", Stats.Cases, Stats.Hits,
		Stats.GrazeDisagreements, Stats.Violations, Stats.MaxTimeError);
	RB_CHECK(Stats.Violations == 0);
}

RB_TEST(VAL_ROOT01_FirstEntryMatchesHighPrecisionReference)
{
	AgreementStats Stats;
	RunRoot01(kDefaultCases, Stats);
	RB_CHECK(Stats.Violations == 0);
	RB_CHECK(Stats.Hits > Stats.Cases / 10);
	RB_CHECK(Stats.MaxTimeError <= 1e-12);
}

RB_TEST(VAL_ROOT01_Slow_MillionCases)
{
	AgreementStats Stats;
	RunRoot01(1000000, Stats);
	std::printf("  ROOT-01: %d cases, %d hits, %d graze disagreements, %d violations, max error %.3g\n", Stats.Cases, Stats.Hits,
		Stats.GrazeDisagreements, Stats.Violations, Stats.MaxTimeError);
	RB_CHECK(Stats.Violations == 0);
}

RB_TEST(VAL_ROOT02_ClusteredDoubleAndWideRangeRootsKeepTheBracketInvariant)
{
	rb::Rng Rng(0x2002ull);
	int Failures = 0;
	int RootsSeen = 0;
	for (int Trial = 0; Trial < 20000; ++Trial)
	{
		const rb::Polynomial P = RandomHardPolynomial(Rng);
		const double Lo = Rng.NextBelow(4) == 0 ? -0.5 : 0.0;
		const double Hi = Rng.NextBelow(4) == 0 ? 1.5 : 1.0;
		double Roots[rb::Polynomial::kMaxDegree];
		const int Count = rb::SolveInInterval(P, Lo, Hi, Roots);
		RootsSeen += Count;

		// The monotone pieces of the (trimmed) polynomial, exactly as the isolation builds them.
		rb::Polynomial T = P;
		T.Trim();
		bool Ok = Count >= 0 && Count <= rb::Max(T.Degree, 0);
		double Knots[rb::Polynomial::kMaxDegree + 2];
		int NumKnots = 0;
		Knots[NumKnots++] = Lo;
		if (T.Degree >= 3)
		{
			rb::Polynomial D = T.Derivative();
			D.Trim();
			double Crit[rb::Polynomial::kMaxDegree];
			const int NumCrit = rb::SolveInInterval(D, Lo, Hi, Crit);
			for (int i = 0; i < NumCrit; ++i)
			{
				Ok = Ok && rb::IsFinite(Crit[i]);
				if (Crit[i] > Knots[NumKnots - 1])
				{
					Knots[NumKnots++] = Crit[i];
				}
			}
		}
		if (Hi > Knots[NumKnots - 1])
		{
			Knots[NumKnots++] = Hi;
		}
		for (int r = 0; r < Count; ++r)
		{
			Ok = Ok && rb::IsFinite(Roots[r]) && Roots[r] >= Lo && Roots[r] <= Hi && (r == 0 || Roots[r] > Roots[r - 1]);
			if (T.Degree <= 2)
			{
				continue; // closed form: no pieces
			}
			// Monotone-bracket invariant: the root lies in a piece whose end values differ in sign (or it is a knot where P = 0).
			bool InBracket = false;
			for (int k = 0; k + 1 < NumKnots && !InBracket; ++k)
			{
				if (Roots[r] >= Knots[k] && Roots[r] <= Knots[k + 1])
				{
					const int Sa = SignOf(T.Eval(Knots[k]));
					const int Sb = SignOf(T.Eval(Knots[k + 1]));
					InBracket = Sa * Sb < 0 || (Roots[r] == Knots[k] && Sa == 0) || (Roots[r] == Knots[k + 1] && Sb == 0);
				}
			}
			Ok = Ok && InBracket;
		}
		if (!Ok)
		{
			++Failures;
		}
	}
	RB_CHECK(Failures == 0);
	RB_CHECK(RootsSeen > 10000);
}

RB_TEST(Polynomial_ScaledArgumentAndRootBound)
{
	rb::Polynomial P;
	P.Degree = 3;
	P.c[0] = -6.0;
	P.c[1] = 11.0;
	P.c[2] = -6.0;
	P.c[3] = 1.0; // (x - 1)(x - 2)(x - 3)
	const rb::Polynomial S = P.ScaledArgument(4.0);
	RB_CHECK_NEAR(S.Eval(0.5), P.Eval(2.0), 1e-12);
	RB_CHECK_NEAR(S.Eval(0.75), P.Eval(3.0), 1e-12);
	RB_CHECK(P.RootBound() >= 3.0);
	RB_CHECK(P.RootBound() == 12.0);
	rb::Polynomial Constant;
	Constant.c[0] = 2.0;
	RB_CHECK(Constant.RootBound() == 0.0);
	RB_CHECK(rb::Polynomial{}.RootBound() == rb::kInfinity);

	const rb::Polynomial D = P.Derivative();
	RB_CHECK_NEAR(rb::RefineBracketedRoot(P, D, 1.5, 2.5), 2.0, 1e-13);
	RB_CHECK_NEAR(rb::RefineBracketedRoot(P, D, 2.5, 3.5), 3.0, 1e-13);
}

RB_TEST(Polynomial_StableQuadraticEdgeCases)
{
	// Pitfall 18: b = c = 0 is the double root 0 (no division by q = 0); cancellation-free small root.
	rb::Polynomial P;
	P.Degree = 2;
	P.c[2] = 3.0;
	double Out[2];
	RB_REQUIRE(rb::SolveInInterval(P, -1.0, 1.0, Out) == 1);
	RB_CHECK(Out[0] == 0.0);
	P.c[0] = 1e-12;
	P.c[1] = -1e4;
	P.c[2] = 1.0; // roots 1e-16 and ~1e4
	RB_REQUIRE(rb::SolveInInterval(P, 0.0, 1.0, Out) == 1);
	RB_CHECK_NEAR(Out[0], 1e-16, 1e-30);
}

RB_TEST(Detect_Slow_BallBallThroughput)
{
	// Performance probe (architecture 12: ~0.1-0.4 us per pair). Release numbers are the relevant ones.
	rb::Rng Rng(77);
	constexpr int Count = 4096;
	static MotionSegment A[Count];
	static MotionSegment B[Count];
	for (int i = 0; i < Count; ++i)
	{
		A[i] = RandomSegment(Rng, 0.0, kR, false);
		B[i] = RandomSegment(Rng, 0.0, kR, false);
		AimAt(Rng, A[i], B[i], 3.0 * kR);
	}
	const rb::NumericsConfig N = Numerics();
	int Found = 0;
	const auto Start = std::chrono::steady_clock::now();
	constexpr int Rounds = 100;
	for (int Round = 0; Round < Rounds; ++Round)
	{
		for (int i = 0; i < Count; ++i)
		{
			Found += rb::PredictBallBall(A[i], kR, B[i], kR, rb::kInfinity, N).Found ? 1 : 0;
		}
	}
	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	std::printf("  PredictBallBall (aimed pairs, no broad-phase rejection): %.1f ns per pair, %d hits\n", Seconds * 1e9 / (Count * Rounds), Found / Rounds);
	RB_CHECK(Found > 0);

	// Table predictors on the same segments: a nose line (quadratic) and a jaw arc (quartic) near every path.
	int TableHits = 0;
	const auto TableStart = std::chrono::steady_clock::now();
	for (int Round = 0; Round < Rounds; ++Round)
	{
		for (int i = 0; i < Count; ++i)
		{
			const Vec3 P = rb::PositionAt(A[i], rb::Min(A[i].TauEnd, 1.0));
			const rb::NoseSegment Nose = MakeNose({P.x + 1.0, P.y + 0.03}, {P.x - 1.0, P.y + 0.03}, {0.0, -1.0});
			TableHits += rb::PredictNoseOnCloth(A[i], kR, Nose, NoseContactOffset(), rb::kInfinity, N).Found ? 1 : 0;
			const rb::JawArc Jaw = MakeJaw({P.x, P.y - 0.03}, 0.004, -rb::kPi, rb::kTwoPi);
			TableHits += rb::PredictJawArcOnCloth(A[i], kR, Jaw, NoseContactOffset(), rb::kInfinity, N).Found ? 1 : 0;
		}
	}
	const double TableSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - TableStart).count();
	std::printf("  PredictNoseOnCloth + PredictJawArcOnCloth: %.1f ns per pair of calls, %d hits\n", TableSeconds * 1e9 / (Count * Rounds),
		TableHits / Rounds);
	RB_CHECK(TableHits > 0);
}
