#include "rb/Core/FpGuard.h"
// Owner: WP-5 (event detection). Spec: physics-collisions 3.3-3.4 and pitfall 18; prior-art 5.5.
#include "rb/Math/Polynomial.h"

#include "rb/Core/Constants.h"
#include "rb/Math/Scalar.h"

#include <cmath>

namespace rb
{
	double Polynomial::Eval(double x) const
	{
		double Result = c[Degree];
		for (int i = Degree - 1; i >= 0; --i)
		{
			Result = Result * x + c[i];
		}
		return Result;
	}

	Polynomial Polynomial::Derivative() const
	{
		Polynomial D;
		D.Degree = Degree > 0 ? Degree - 1 : 0;
		for (int i = 1; i <= Degree; ++i)
		{
			D.c[i - 1] = c[i] * static_cast<double>(i);
		}
		return D;
	}

	void Polynomial::Trim(double RelativeEpsilon)
	{
		double MaxAbs = 0.0;
		for (int i = 0; i <= Degree; ++i)
		{
			MaxAbs = std::fmax(MaxAbs, std::fabs(c[i]));
		}
		while (Degree > 0 && std::fabs(c[Degree]) <= RelativeEpsilon * MaxAbs)
		{
			c[Degree] = 0.0;
			--Degree;
		}
	}

	Polynomial Polynomial::ScaledArgument(double Scale) const
	{
		Polynomial Q;
		Q.Degree = Degree;
		double Power = 1.0;
		for (int i = 0; i <= Degree; ++i)
		{
			Q.c[i] = c[i] * Power;
			Power *= Scale;
		}
		return Q;
	}

	double Polynomial::RootBound() const
	{
		int n = Degree;
		while (n > 0 && c[n] == 0.0)
		{
			--n;
		}
		if (n == 0)
		{
			return c[0] == 0.0 ? kInfinity : 0.0;
		}
		const double Lead = std::fabs(c[n]);
		double MaxRatio = 0.0;
		for (int i = 0; i < n; ++i)
		{
			MaxRatio = std::fmax(MaxRatio, std::fabs(c[i]) / Lead);
		}
		return 1.0 + MaxRatio;
	}

	namespace
	{
		int SignOf(double v) { return (v > 0.0) - (v < 0.0); }

		// Stable closed form for a trimmed quadratic (c[2] != 0), collisions 3.3 / pitfall 18: sgn(0) := +1; q = 0
		// (b = c = 0) is the double root 0. A negative discriminant has no real root; its vertex counts as an
		// (even-multiplicity) root only when |P(vertex)| <= ValueTolerance, as in the recursive case.
		int SolveQuadratic(const Polynomial& P, double Lo, double Hi, double* RootsOut, double XTolerance, double ValueTolerance)
		{
			const double A = P.c[2];
			const double B = P.c[1];
			const double C = P.c[0];
			const double Disc = B * B - 4.0 * A * C;
			if (Disc < 0.0)
			{
				if (ValueTolerance > 0.0)
				{
					const double Vertex = -B / (2.0 * A);
					if (Vertex > Lo && Vertex < Hi && std::fabs(P.Eval(Vertex)) <= ValueTolerance)
					{
						RootsOut[0] = Vertex;
						return 1;
					}
				}
				return 0;
			}
			const double Q = -0.5 * (B + SignNonZero(B) * std::sqrt(Disc));
			double R0 = 0.0;
			double R1 = 0.0;
			if (Q != 0.0)
			{
				R0 = Q / A;
				R1 = C / Q;
			}
			if (R1 < R0)
			{
				const double Tmp = R0;
				R0 = R1;
				R1 = Tmp;
			}
			int Count = 0;
			if (R0 >= Lo && R0 <= Hi)
			{
				RootsOut[Count++] = R0;
			}
			if (R1 >= Lo && R1 <= Hi && (Count == 0 || std::fabs(R1 - RootsOut[0]) > XTolerance))
			{
				RootsOut[Count++] = R1;
			}
			return Count;
		}

		int SolveRecursive(const Polynomial& P, double Lo, double Hi, double* RootsOut, double XTolerance, double ValueTolerance)
		{
			if (P.Degree <= 0)
			{
				return 0;
			}

			if (P.Degree == 1)
			{
				if (P.c[1] == 0.0)
				{
					return 0;
				}
				const double x = -P.c[0] / P.c[1];
				if (x >= Lo && x <= Hi)
				{
					RootsOut[0] = x;
					return 1;
				}
				return 0;
			}

			if (P.Degree == 2)
			{
				return SolveQuadratic(P, Lo, Hi, RootsOut, XTolerance, ValueTolerance);
			}

			// Split [Lo, Hi] at the critical points (roots of P'), then scan monotonic pieces.
			Polynomial D = P.Derivative();
			D.Trim();
			double Critical[Polynomial::kMaxDegree];
			const int NumCritical = SolveRecursive(D, Lo, Hi, Critical, XTolerance, 0.0);

			double Bounds[Polynomial::kMaxDegree + 2];
			int NumBounds = 0;
			Bounds[NumBounds++] = Lo;
			for (int i = 0; i < NumCritical; ++i)
			{
				if (Critical[i] > Bounds[NumBounds - 1])
				{
					Bounds[NumBounds++] = Critical[i];
				}
			}
			if (Hi > Bounds[NumBounds - 1])
			{
				Bounds[NumBounds++] = Hi;
			}

			int NumRoots = 0;
			auto AddRoot = [&](double x)
			{
				if (NumRoots > 0 && std::fabs(x - RootsOut[NumRoots - 1]) <= XTolerance)
				{
					return;
				}
				if (NumRoots < P.Degree)
				{
					RootsOut[NumRoots++] = x;
				}
			};

			// The refinement below needs the exact derivative of P (the trimmed D is only used for the knots).
			const Polynomial DExact = P.Derivative();
			double Fa = P.Eval(Bounds[0]);
			if (Fa == 0.0)
			{
				AddRoot(Bounds[0]);
			}
			for (int i = 0; i + 1 < NumBounds; ++i)
			{
				const double a = Bounds[i];
				const double b = Bounds[i + 1];
				const double Fb = P.Eval(b);

				if (Fb == 0.0)
				{
					AddRoot(b);
				}
				else if (Fa != 0.0 && SignOf(Fa) != SignOf(Fb))
				{
					AddRoot(RefineBracketedRoot(P, DExact, a, b, XTolerance, 200));
				}
				else if (ValueTolerance > 0.0 && i + 1 < NumBounds - 1 && std::fabs(Fb) <= ValueTolerance)
				{
					// Interior critical point touching zero: even-multiplicity root.
					AddRoot(b);
				}
				Fa = Fb;
			}
			return NumRoots;
		}
	}

	double RefineBracketedRoot(const Polynomial& P, const Polynomial& Derivative, double A, double B, double XTolerance, int MaxIterations)
	{
		double Fa = P.Eval(A);
		double x = 0.5 * (A + B);
		for (int Iter = 0; Iter < MaxIterations; ++Iter)
		{
			const double Fx = P.Eval(x);
			if (Fx == 0.0)
			{
				return x;
			}
			if (SignOf(Fx) == SignOf(Fa))
			{
				A = x;
				Fa = Fx;
			}
			else
			{
				B = x;
			}

			const double Dx = Derivative.Eval(x);
			double Next = (Dx != 0.0) ? x - Fx / Dx : 0.5 * (A + B);
			if (!(Next > A && Next < B))
			{
				Next = 0.5 * (A + B);
			}
			if (std::fabs(Next - x) <= XTolerance || (B - A) <= XTolerance)
			{
				return Next;
			}
			x = Next;
		}
		return x;
	}

	int SolveInInterval(const Polynomial& P, double Lo, double Hi, double* RootsOut, double XTolerance, double ValueTolerance)
	{
		if (!(Lo <= Hi))
		{
			return 0;
		}
		Polynomial Trimmed = P;
		Trimmed.Trim();
		return SolveRecursive(Trimmed, Lo, Hi, RootsOut, XTolerance, ValueTolerance);
	}

	bool SmallestRootInInterval(const Polynomial& P, double Lo, double Hi, double& RootOut, double XTolerance)
	{
		double Roots[Polynomial::kMaxDegree];
		const int Count = SolveInInterval(P, Lo, Hi, Roots, XTolerance);
		if (Count == 0)
		{
			return false;
		}
		RootOut = Roots[0];
		return true;
	}
}
