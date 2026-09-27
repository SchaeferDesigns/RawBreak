#pragma once

// Real polynomials and real-root isolation on an interval (physics-collisions 3.3-3.4, prior-art 5.5).
// Owner: WP-5 (event detection). Allocation-free, deterministic, no complex roots, no closed-form quartic.

#include "rb/Config.h"

namespace rb
{
	// Real polynomial p(x) = c[0] + c[1] x + ... + c[Degree] x^Degree.
	// Event detection needs degrees up to 4 (ball-ball, jaw arcs, drop edge) and 8 (rim torus, pocket-cut rim).
	struct Polynomial
	{
		static constexpr int kMaxDegree = 8;

		double c[kMaxDegree + 1] = {};
		int Degree = 0;

		// Horner evaluation.
		RB_API double Eval(double x) const;
		RB_API Polynomial Derivative() const;

		// Lowers Degree while the leading coefficient is negligible relative to the largest one
		// (|c[Degree]| <= RelativeEpsilon * max|c|, collisions 3.3 "degree reduction"); dropped coefficients are set to 0.
		RB_API void Trim(double RelativeEpsilon = 1e-14);

		// q(s) = p(Scale s): coefficients c[i] Scale^i (time scaling tau = T s, collisions 3.4).
		RB_API Polynomial ScaledArgument(double Scale) const;

		// Cauchy bound: every real root x satisfies |x| <= 1 + max_{i < n} |c[i] / c[n]| with n the highest index of a
		// non-zero coefficient. 0 for a non-zero constant (no roots), +inf for the zero polynomial (every x is a root).
		RB_API double RootBound() const;
	};

	// All real roots of P in [Lo, Hi], ascending, written to RootsOut (capacity >= P.Degree).
	// Returns the number of roots found.
	//
	// Method: recursive critical-point isolation (Yuksel, "High-Performance Polynomial Root Finding
	// for Graphics", HPG 2022; collisions 3.3). Roots of P' split [Lo, Hi] into monotonic pieces; each piece whose end
	// values change sign contains exactly one root, located by safeguarded Newton-bisection to
	// XTolerance. Degree 2 uses the stable closed form q = -(b + sgn(b) sqrt(b^2 - 4ac)) / 2 (pitfall 18), degree 1
	// -c0/c1. Roots of even multiplicity (tangential touches) are reported only if P vanishes at the
	// critical point within ValueTolerance — for collision detection a pure graze is not a hit.
	RB_API int SolveInInterval(const Polynomial& P, double Lo, double Hi, double* RootsOut, double XTolerance = 1e-13, double ValueTolerance = 0.0);

	// Smallest real root of P in [Lo, Hi]; returns false if there is none.
	RB_API bool SmallestRootInInterval(const Polynomial& P, double Lo, double Hi, double& RootOut, double XTolerance = 1e-13);

	// The root of P in [A, B] (A < B) when P(A) and P(B) are non-zero with opposite signs and P is monotone on [A, B]
	// (a piece of the isolation above): safeguarded Newton from the bracket midpoint, every step that leaves the
	// shrinking bracket replaced by bisection; stops when a step is <= XTolerance or after MaxIterations.
	// Derivative must be P.Derivative() (passed in so callers reuse it).
	RB_API double RefineBracketedRoot(const Polynomial& P, const Polynomial& Derivative, double A, double B, double XTolerance = 1e-13,
		int MaxIterations = 100);
}
