#include "rbtest.h"

#include "rb/Math/Polynomial.h"

#include <algorithm>
#include <cstdint>

using rb::Polynomial;

namespace
{
	// Builds prod (x - r_i) * Scale.
	Polynomial FromRoots(const double* Roots, int Count, double Scale = 1.0)
	{
		Polynomial P;
		P.Degree = 0;
		P.c[0] = Scale;
		for (int k = 0; k < Count; ++k)
		{
			Polynomial Next;
			Next.Degree = P.Degree + 1;
			for (int i = 0; i <= P.Degree; ++i)
			{
				Next.c[i + 1] += P.c[i];
				Next.c[i] -= Roots[k] * P.c[i];
			}
			P = Next;
		}
		return P;
	}

	struct Lcg
	{
		uint64_t State = 0x9E3779B97F4A7C15ull;
		double Next01()
		{
			State = State * 6364136223846793005ull + 1442695040888963407ull;
			return static_cast<double>(State >> 11) * (1.0 / 9007199254740992.0);
		}
	};
}

RB_TEST(Polynomial_QuadraticTwoRoots)
{
	const double R[] = {1.0, 3.0};
	double Out[4];
	const int N = rb::SolveInInterval(FromRoots(R, 2), 0.0, 5.0, Out);
	RB_REQUIRE(N == 2);
	RB_CHECK_NEAR(Out[0], 1.0, 1e-12);
	RB_CHECK_NEAR(Out[1], 3.0, 1e-12);
}

RB_TEST(Polynomial_QuarticFourRootsAndIntervalClipping)
{
	const double R[] = {0.5, 1.0, 2.0, 4.0};
	const Polynomial P = FromRoots(R, 4, -2.5);
	double Out[4];
	RB_REQUIRE(rb::SolveInInterval(P, -10.0, 10.0, Out) == 4);
	for (int i = 0; i < 4; ++i)
	{
		RB_CHECK_NEAR(Out[i], R[i], 1e-11);
	}
	RB_REQUIRE(rb::SolveInInterval(P, 0.75, 3.0, Out) == 2);
	RB_CHECK_NEAR(Out[0], 1.0, 1e-11);
	RB_CHECK_NEAR(Out[1], 2.0, 1e-11);

	double Smallest = 0.0;
	RB_REQUIRE(rb::SmallestRootInInterval(P, 1.5, 10.0, Smallest));
	RB_CHECK_NEAR(Smallest, 2.0, 1e-11);
}

RB_TEST(Polynomial_NoRealRoots)
{
	Polynomial P;
	P.Degree = 2;
	P.c[0] = 1.0;
	P.c[2] = 1.0; // x^2 + 1
	double Out[2];
	RB_CHECK(rb::SolveInInterval(P, -100.0, 100.0, Out) == 0);
}

RB_TEST(Polynomial_ExactDoubleRootIsReported)
{
	const double R[] = {2.0, 2.0};
	double Out[2];
	RB_REQUIRE(rb::SolveInInterval(FromRoots(R, 2), 0.0, 5.0, Out) == 1);
	RB_CHECK_NEAR(Out[0], 2.0, 1e-12);
}

RB_TEST(Polynomial_NearGrazeIsIgnoredUnlessToleranceGiven)
{
	// (x - 2)^2 + 1e-12: minimum just above zero, i.e. two balls that pass within a hair.
	const double R[] = {2.0, 2.0};
	Polynomial P = FromRoots(R, 2);
	P.c[0] += 1e-12;
	double Out[2];
	RB_CHECK(rb::SolveInInterval(P, 0.0, 5.0, Out) == 0);
	RB_REQUIRE(rb::SolveInInterval(P, 0.0, 5.0, Out, 1e-13, 1e-11) == 1);
	RB_CHECK_NEAR(Out[0], 2.0, 1e-9);
}

RB_TEST(Polynomial_DegenerateLeadingCoefficient)
{
	const double R[] = {-1.0, 0.25, 0.75};
	Polynomial P = FromRoots(R, 3);
	P.Degree = 4; // c[4] == 0: a "quartic" that is really a cubic
	double Out[4];
	RB_REQUIRE(rb::SolveInInterval(P, -5.0, 5.0, Out) == 3);
	RB_CHECK_NEAR(Out[0], -1.0, 1e-12);
	RB_CHECK_NEAR(Out[1], 0.25, 1e-12);
	RB_CHECK_NEAR(Out[2], 0.75, 1e-12);
}

RB_TEST(Polynomial_CloseRoots)
{
	// Roots 1 microsecond apart are ill-conditioned (error ~ eps * |P| / |P'|); 1e-9 s is still
	// far below anything that matters for ball positions (10 m/s * 1e-9 s = 10 nm).
	const double R[] = {1.0, 1.000001, 3.0};
	double Out[3];
	RB_REQUIRE(rb::SolveInInterval(FromRoots(R, 3), 0.0, 5.0, Out) == 3);
	RB_CHECK_NEAR(Out[0], 1.0, 1e-9);
	RB_CHECK_NEAR(Out[1], 1.000001, 1e-9);
	RB_CHECK_NEAR(Out[2], 3.0, 1e-12);
}

RB_TEST(Polynomial_RandomQuarticsMatchKnownRoots)
{
	Lcg Rng;
	for (int Trial = 0; Trial < 2000; ++Trial)
	{
		double R[4];
		for (double& r : R)
		{
			r = Rng.Next01() * 4.0; // collision times within a 4 s window
		}
		std::sort(R, R + 4);
		if (R[1] - R[0] < 1e-2 || R[2] - R[1] < 1e-2 || R[3] - R[2] < 1e-2)
		{
			continue; // clustered roots are ill-conditioned by nature; covered by Polynomial_CloseRoots
		}
		const double Scale = 0.1 + Rng.Next01() * 10.0;
		const Polynomial P = FromRoots(R, 4, Scale);
		double Out[4];
		const int N = rb::SolveInInterval(P, 0.0, 4.0, Out);
		RB_REQUIRE(N == 4);
		for (int i = 0; i < 4; ++i)
		{
			// Forward error is bounded by the conditioning of the rounded coefficients...
			RB_CHECK_NEAR(Out[i], R[i], 1e-8);

			// ...while the backward error shows the solver converged to its 1e-13 time tolerance:
			// |P(x)| <= |P'(x)| * XTolerance + rounding.
			double Magnitude = 0.0;
			double Power = 1.0;
			for (int k = 0; k <= P.Degree; ++k)
			{
				Magnitude += std::fabs(P.c[k]) * Power;
				Power *= std::fabs(Out[i]);
			}
			const double Slope = std::fabs(P.Derivative().Eval(Out[i]));
			RB_CHECK(std::fabs(P.Eval(Out[i])) <= Slope * 1e-13 + 1e-14 * Magnitude);
		}
	}
}
