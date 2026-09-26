#pragma once

// Shared set-ups of the ball-ball tests (physics-collisions 9.1, prior-art 9.3 / 9.4). Every test pins its parameters.

#include "rb/Core/Constants.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/BallBall.h"

namespace rbbb
{
	inline constexpr double kR = 0.028575;       // [m]
	inline constexpr double kM = 0.17009713875;  // [kg]
	inline constexpr double kI = 0.4 * kM * kR * kR;

	inline rb::ImpactBody Ball(const rb::Vec3& Position, const rb::Vec3& Velocity, const rb::Vec3& Omega = {})
	{
		rb::ImpactBody B;
		B.Position = Position;
		B.Velocity = Velocity;
		B.Omega = Omega;
		B.Radius = kR;
		B.Mass = kM;
		B.Inertia = kI;
		return B;
	}

	// COL 9 set-up: CB at (0, 0, R) moving along +x at V with spin W, OB at rest at r1 + 2R (cos phi, sin phi, 0).
	struct CutShot
	{
		rb::ImpactBody Cue;
		rb::ImpactBody Object;
	};

	inline CutShot MakeCut(double V, double PhiRad, const rb::Vec3& Omega = {})
	{
		CutShot S;
		S.Cue = Ball({0.0, 0.0, kR}, {V, 0.0, 0.0}, Omega);
		S.Object = Ball({2.0 * kR * rb::Cos(PhiRad), 2.0 * kR * rb::Sin(PhiRad), kR}, {});
		return S;
	}

	inline rb::BallBallParams Params(double Restitution, rb::BallBallFrictionModel Friction = rb::BallBallFrictionModel::Alciatore)
	{
		rb::BallBallParams P;
		P.Restitution = Restitution;
		P.Friction = Friction;
		P.MuA = 9.951e-3;
		P.MuB = 0.108;
		P.MuC = 1.088;
		P.MuConstant = 0.06;
		P.ClingFactor = 1.0;
		P.ChalkClingFactor = 2.5;
		return P;
	}

	inline rb::BallBallImpulse Resolve(const CutShot& S, const rb::BallBallParams& P)
	{
		const rb::NumericsConfig N;
		return rb::ResolveBallBall(S.Cue, S.Object, P, N.RestSpeed, N.EpsV);
	}

	// Signed throw [deg]: angle from n_hat to the OB's horizontal velocity, positive counter-clockwise about +z (COL 9).
	inline double ThrowDeg(const rb::BallBallImpulse& I)
	{
		const double Out = rb::Atan2(I.Velocity2.y, I.Velocity2.x);
		const double In = rb::Atan2(I.Normal.y, I.Normal.x);
		return (Out - In) * rb::kRadToDeg;
	}

	// Alciatore TP A.14 closed-form throw [deg] (COL 2.6 oracle), unsigned, generalised to e_b by the factor 2 / (1 + e).
	inline double AlciatoreThrowDeg(double V, double PhiRad, double Wx, double Wz, double Restitution)
	{
		const double Sn = rb::Sin(PhiRad);
		const double Cs = rb::Cos(PhiRad);
		const double Vrel = rb::Sqrt(rb::Square(V * Sn - kR * Wz) + rb::Square(kR * Wx * Cs));
		const double Mu = 9.951e-3 + 0.108 * rb::Exp(-1.088 * Vrel);
		const double Ratio = rb::Min(Mu * V * Cs / Vrel, (2.0 / (1.0 + Restitution)) / 7.0);
		return rb::Atan(Ratio * (V * Sn - kR * Wz) / (V * Cs)) * rb::kRadToDeg;
	}
}
