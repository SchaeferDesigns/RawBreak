#pragma once

// Scalar math funnel. ALL transcendental calls in the core go through these wrappers so that
// bitwise determinism across platforms/CRTs can later be obtained by swapping the implementation in
// one place (see Docs/architecture.md, "Determinism"). sqrt and floor are IEEE-exact everywhere; sin, cos,
// exp, expm1, log, log1p, cbrt, atan2, asinh are CRT-dependent.
// Owner: WP-0 (architecture, frozen). Header-only.

#include "rb/Config.h"

#include <cmath>

namespace rb
{
	inline double Sqrt(double X) { return std::sqrt(X); }
	inline double Sin(double X) { return std::sin(X); }
	inline double Cos(double X) { return std::cos(X); }
	inline double Tan(double X) { return std::tan(X); }
	inline double Asin(double X) { return std::asin(X); }
	inline double Acos(double X) { return std::acos(X); }
	inline double Atan(double X) { return std::atan(X); }
	inline double Atan2(double Y, double X) { return std::atan2(Y, X); }
	inline double Exp(double X) { return std::exp(X); }
	inline double Log(double X) { return std::log(X); }
	// exp(x) - 1 and log(1 + x) without cancellation near 0: the tilted-table pursuit solution uses
	// E_n = -Expm1(-n lambda) (human-factors 4.5.2); CRT-dependent like Exp / Log.
	inline double Expm1(double X) { return std::expm1(X); }
	inline double Log1p(double X) { return std::log1p(X); }
	// Real cube root (Newton start value of the tilt refresh rule, human-factors 4.5.3); CRT-dependent.
	inline double Cbrt(double X) { return std::cbrt(X); }
	// floor is exact (IEEE) everywhere; wrapped for a single math entry point (chalk-zone lookup, noise eighths).
	inline double Floor(double X) { return std::floor(X); }
	inline double Asinh(double X) { return std::asinh(X); }
	inline double Sinh(double X) { return std::sinh(X); }
	inline double Pow(double X, double Y) { return std::pow(X, Y); }
	inline bool IsFinite(double X) { return std::isfinite(X); }

	constexpr double Abs(double X) { return X < 0.0 ? -X : X; }
	constexpr double Square(double X) { return X * X; }
	constexpr double Min(double A, double B) { return B < A ? B : A; }
	constexpr double Max(double A, double B) { return A < B ? B : A; }
	constexpr double Clamp(double X, double Lo, double Hi) { return X < Lo ? Lo : (Hi < X ? Hi : X); }

	// sign with sgn(0) = +1, as required by the stable quadratic formula (collisions pitfall 18).
	constexpr double SignNonZero(double X) { return X < 0.0 ? -1.0 : 1.0; }

	// sign with sgn(0) = 0 (spin decay laws, motion spec A.4).
	constexpr double Sign(double X) { return X > 0.0 ? 1.0 : (X < 0.0 ? -1.0 : 0.0); }

	// Smoothstep 3 s^2 - 2 s^3 on clamp(x, 0, 1) (motion spec B.8.2 lambda schedule).
	constexpr double SmoothStep01(double X)
	{
		const double S = Clamp(X, 0.0, 1.0);
		return S * S * (3.0 - 2.0 * S);
	}
}
