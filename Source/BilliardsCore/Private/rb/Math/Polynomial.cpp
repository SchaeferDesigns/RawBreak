#include "rb/Core/FpGuard.h"
#include "rb/Math/Polynomial.h"

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

	namespace
	{
		int Sign(double v) { return (v > 0.0) - (v < 0.0); }

		// Single root of a polynomial that is monotonic on [a, b] with P(a), P(b) of opposite sign.
		double RefineBracketedRoot(const Polynomial& P, const Polynomial& D, double a, double b, double Fa, double XTolerance)
		{
			double x = 0.5 * (a + b);
			for (int Iter = 0; Iter < 200; ++Iter)
			{
				const double Fx = P.Eval(x);
				if (Fx == 0.0)
				{
					return x;
				}
				if (Sign(Fx) == Sign(Fa))
				{
					a = x;
					Fa = Fx;
				}
				else
				{
					b = x;
				}

				const double Dx = D.Eval(x);
				double Next = (Dx != 0.0) ? x - Fx / Dx : 0.5 * (a + b);
				if (!(Next > a && Next < b))
				{
					Next = 0.5 * (a + b);
				}
				if (std::fabs(Next - x) <= XTolerance || (b - a) <= XTolerance)
				{
					return Next;
				}
				x = Next;
			}
			return x;
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
				else if (Fa != 0.0 && Sign(Fa) != Sign(Fb))
				{
					AddRoot(RefineBracketedRoot(P, D, a, b, Fa, XTolerance));
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
