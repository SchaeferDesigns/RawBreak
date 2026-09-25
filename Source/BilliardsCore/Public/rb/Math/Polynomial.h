#pragma once

#include "rb/Config.h"

namespace rb
{
	// Real polynomial p(x) = c[0] + c[1] x + ... + c[Degree] x^Degree.
	// Event detection needs degrees up to 4 (ball-ball, ball-jaw); the cap leaves headroom.
	struct Polynomial
	{
		static constexpr int kMaxDegree = 8;

		double c[kMaxDegree + 1] = {};
		int Degree = 0;

		double Eval(double x) const;
		Polynomial Derivative() const;

		// Lowers Degree while the leading coefficient is negligible relative to the largest one.
		void Trim(double RelativeEpsilon = 1e-14);
	};

	// All real roots of P in [Lo, Hi], ascending, written to RootsOut (capacity >= P.Degree).
	// Returns the number of roots found.
	//
	// Method: recursive critical-point isolation (Yuksel, "High-Performance Polynomial Root Finding
	// for Graphics", HPG 2022). Roots of P' split [Lo, Hi] into monotonic pieces; each piece whose end
	// values change sign contains exactly one root, located by safeguarded Newton-bisection to
	// XTolerance. Roots of even multiplicity (tangential touches) are reported only if P vanishes at
	// the critical point within ValueTolerance — for collision detection a pure graze is not a hit.
	RB_API int SolveInInterval(const Polynomial& P, double Lo, double Hi, double* RootsOut, double XTolerance = 1e-13, double ValueTolerance = 0.0);

	// Smallest real root of P in [Lo, Hi]; returns false if there is none.
	RB_API bool SmallestRootInInterval(const Polynomial& P, double Lo, double Hi, double& RootOut, double XTolerance = 1e-13);
}
