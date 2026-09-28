#pragma once

// Owner: WP-10 (validation & benchmarks). Shared helpers of the validation tests (Tests/Core/Validation): pinned parameter sets
// (prior-art 9: g = 9.81, pitfall 9: every parameter explicit), single-ball placements, post-slip states from the event log,
// the diamond coordinates of prior-art 6.9, a two-sample Kolmogorov-Smirnov test and printing of the validation rows.
// Everything runs through the public API (Simulator::Run, ShotResult); the simulator helpers are those of the end-to-end tests.

#include "rbtest.h"

#include "EndToEnd/EndToEndUtil.h"

#include "rb/Core/Constants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace val
{
	using simtest::kR;

	// prior-art 9 defaults (g 9.81, mu_s 0.2, mu_r 0.01, alpha_sp 10, e_b 0.95) on the table's own cushions and pockets
	// (MakePhysicsParams of the preset, then the VAL cloth and ball-ball values pinned; Origin Explicit).
	inline rb::PhysicsParams TableParams(const rb::TableSpec& Spec)
	{
		rb::PhysicsParams P = rb::MakePhysicsParams(Spec);
		P.Origin = rb::ParamsOrigin::Explicit;
		P.Gravity = simtest::kGVal;
		P.Cloth = rb::ClothParams{0.2, 0.010, 10.0};
		P.BallBall.Restitution = 0.95;
		return P;
	}

	inline rb::SimInput& Fresh(const rb::TableGeometry& T, const rb::PhysicsParams& P, int Slot = 0)
	{
		return simtest::NewInput(T, P, Slot);
	}

	// A ball rolling (no slip) at P with plan velocity V and side spin Wz (the spin about z does not affect rolling).
	inline void PlaceRollingSpin(rb::SimInput& In, int Id, const rb::Vec3& P, const rb::Vec3& V, double Wz, const rb::BallSpec& Spec)
	{
		rb::Vec3 W = rb::RollingOmegaH(V, Spec.Radius);
		W.z = Wz;
		simtest::Place(In, Id, P, V, W, Spec);
		In.Balls[Id].State.State = rb::LengthSquared(V) > 0.0 ? rb::MotionState::Rolling : (Wz != 0.0 ? rb::MotionState::Spinning : rb::MotionState::Stationary);
	}

	// Speed a rolling ball needs at the start to arrive at Speed after Distance of rolling (deceleration mu_r g).
	inline double RollingStartSpeed(double Speed, double Distance, const rb::PhysicsParams& P)
	{
		return rb::Sqrt(Speed * Speed + 2.0 * P.Cloth.RollingResistance * P.Gravity * Distance);
	}

	// State of Ball at the first Sliding -> Rolling transition at or after From (the post-slip state), from the event log
	// (RecordOptions::LogTransitions and EventStates). Found = false if the ball never rolled after From.
	inline rb::BallState PostSlipState(const rb::ShotResult& R, int Ball, double From, bool& Found)
	{
		Found = false;
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Type == rb::ShotEventType::MotionTransition && E.A == Ball && E.Time >= From && E.From == rb::MotionState::Sliding &&
				E.To == rb::MotionState::Rolling)
			{
				Found = true;
				return E.Post[0];
			}
		}
		return rb::BallState{};
	}

	inline double PlanAngleDeg(const rb::Vec3& V) { return rb::Atan2(V.y, V.x) * rb::kRadToDeg; }
	inline double PlanSpeed(const rb::Vec3& V) { return rb::Sqrt(V.x * V.x + V.y * V.y); }

	// prior-art 6.9 diamond geometry (INTERPRETATION of the spec, 9-ft table): the diamond (sight) lines lie s behind the nose
	// lines, Delta = L / 8 = W / 4 is the diamond spacing.
	struct Diamonds
	{
		double HalfLength = 1.27;
		double HalfWidth = 0.635;
		double Inset = 0.0936625; // s
		double Spacing = 0.3175;  // Delta

		double LongLineY() const { return HalfWidth + Inset; }  // RAIL_LEFT diamond line y = +this, RAIL_RIGHT y = -this
		double EndLineX() const { return HalfLength + Inset; }  // RAIL_FOOT diamond line x = +this, RAIL_HEAD x = -this
	};

	// Where the straight line through P along the plan direction D crosses y = Y (nan if parallel).
	inline double CrossX(const rb::Vec2& P, const rb::Vec2& D, double Y)
	{
		if (!(rb::Abs(D.y) > 0.0))
		{
			return std::nan("");
		}
		return P.x + D.x * (Y - P.y) / D.y;
	}

	inline double CrossY(const rb::Vec2& P, const rb::Vec2& D, double X)
	{
		if (!(rb::Abs(D.x) > 0.0))
		{
			return std::nan("");
		}
		return P.y + D.y * (X - P.x) / D.x;
	}

	// Two-sample Kolmogorov-Smirnov test: the statistic D = sup |F1 - F2| and the asymptotic p-value
	// Q_KS(sqrt(ne) D (1 + 0.12 / sqrt(ne) ... )) (Numerical Recipes 14.3, ne = n1 n2 / (n1 + n2)). Conservative for discrete data.
	inline double KolmogorovSmirnovP(std::vector<double> A, std::vector<double> B, double& Statistic)
	{
		std::sort(A.begin(), A.end());
		std::sort(B.begin(), B.end());
		const double N1 = static_cast<double>(A.size());
		const double N2 = static_cast<double>(B.size());
		std::size_t i = 0;
		std::size_t j = 0;
		double D = 0.0;
		while (i < A.size() && j < B.size())
		{
			const double X = rb::Min(A[i], B[j]);
			while (i < A.size() && A[i] <= X)
			{
				++i;
			}
			while (j < B.size() && B[j] <= X)
			{
				++j;
			}
			D = rb::Max(D, rb::Abs(static_cast<double>(i) / N1 - static_cast<double>(j) / N2));
		}
		Statistic = D;
		const double Ne = N1 * N2 / (N1 + N2);
		const double SqrtNe = rb::Sqrt(Ne);
		const double Lambda = (SqrtNe + 0.12 + 0.11 / SqrtNe) * D;
		double Sum = 0.0;
		double Sign = 1.0;
		for (int k = 1; k <= 100; ++k)
		{
			const double Term = Sign * 2.0 * rb::Exp(-2.0 * k * k * Lambda * Lambda);
			Sum += Term;
			if (rb::Abs(Term) <= 1e-12 * rb::Abs(Sum))
			{
				break;
			}
			Sign = -Sign;
		}
		return Lambda < 1e-3 ? 1.0 : rb::Clamp(Sum, 0.0, 1.0);
	}

	inline const char* Verdict(bool Pass) { return Pass ? "PASS" : "FAIL"; }
}
