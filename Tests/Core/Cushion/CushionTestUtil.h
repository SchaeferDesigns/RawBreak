#pragma once

// Shared helpers of the WP-4 cushion tests (Tests/Core/Cushion, Tests/Core/Pocket). Every test pins its parameters
// explicitly (architecture 18, prior-art 8.9); the constants below are the spec test tables' values.

#include "rbtest.h"

#include "rb/Core/Random.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Cushion.h"

namespace rbcushiontest
{
	// physics-collisions 9 common constants (pool ball, 6 oz) and the pool nose height h = 0.635 D.
	inline constexpr double kR = 0.028575;
	inline constexpr double kM = 0.17009713875;
	inline constexpr double kG = 9.80665;
	inline constexpr double kH = 0.635 * 2.0 * kR;

	// Mathavan 2010 / prior-art snooker set (collisions M-1, prior-art CUSH-02/03).
	inline constexpr double kSnookerM = 0.1406;
	inline constexpr double kSnookerR = 0.02625;
	inline constexpr double kSnookerClothFriction = 0.212;

	inline rb::BallSpec PoolBall() { return rb::MakeBallSpec(kR, kM); }
	inline rb::BallSpec SnookerBall() { return rb::MakeBallSpec(kSnookerR, kSnookerM); }

	// Contact elevation of an edge at height H for a ball of radius R on the cloth: sin(theta) = (H - R) / R (4.1).
	inline double EdgeElevation(double H, double R) { return rb::Asin((H - R) / R); }
	inline double PoolElevation() { return EdgeElevation(kH, kR); }

	// Rolling on the cloth in the cushion-local frame: w = (-v_Y / R, v_X / R, 0).
	inline rb::Vec3 RollingOmega(const rb::Vec3& V, double R) { return {-V.y / R, V.x / R, 0.0}; }

	// Local velocity of a ball arriving at the given angle from the cushion line (X along the cushion, Y into it).
	inline rb::Vec3 AtIncidence(double Speed, double DegreesFromCushionLine)
	{
		const double A = DegreesFromCushionLine * (rb::kPi / 180.0);
		return {Speed * rb::Cos(A), Speed * rb::Sin(A), 0.0};
	}

	inline double Degrees(double Radians) { return Radians * (180.0 / rb::kPi); }

	inline double KineticEnergy(const rb::BallSpec& Spec, const rb::Vec3& V, const rb::Vec3& W)
	{
		return 0.5 * Spec.Mass * rb::LengthSquared(V) + 0.5 * Spec.Inertia * rb::LengthSquared(W);
	}

	inline double MaxAbsDiff(const rb::Vec3& A, const rb::Vec3& B)
	{
		return rb::Max(rb::Abs(A.x - B.x), rb::Max(rb::Abs(A.y - B.y), rb::Abs(A.z - B.z)));
	}

	// Pool Mathavan settings of M-2..M-4 (e fixed, mu_w 0.14, mu_s 0.2) with the given integrator.
	inline rb::MathavanSettings PoolMathavan(double Restitution, int Steps, bool Split)
	{
		rb::MathavanSettings S;
		S.Elevation = PoolElevation();
		S.Restitution = Restitution;
		S.CushionFriction = 0.14;
		S.ClothFriction = 0.2;
		S.Steps = Steps;
		S.MaxBisections = 60;
		S.SplitAtSlipReversal = Split;
		S.SlipEps = 1e-6;
		S.RestSpeed = 2e-3;
		return S;
	}

	// Max velocity error [m/s] of a result against a reference: max over |v| components and R |w| components.
	inline double ImpactError(const rb::CushionImpactResult& A, const rb::CushionImpactResult& B, double R)
	{
		return rb::Max(MaxAbsDiff(A.Velocity, B.Velocity), R * MaxAbsDiff(A.Omega, B.Omega));
	}

	// The accuracy-gate cases M-2, M-3, M-4 (physics-collisions 9.3), local frame, pool ball, with the spec's
	// N = 20 000 values (v', R w').
	struct GateCase
	{
		const char* Name;
		rb::Vec3 V;
		rb::Vec3 W;
		rb::Vec3 ExpectedV;
		rb::Vec3 ExpectedRW;
	};

	inline GateCase GateCaseAt(int Index)
	{
		if (Index == 0)
		{
			const rb::Vec3 V = AtIncidence(1.0, 45.0);
			return {"M-2", V, RollingOmega(V, kR), {0.543143, -0.679940, 0.0}, {-0.273446, 0.588273, 0.400870}};
		}
		if (Index == 1)
		{
			return {"M-3", AtIncidence(2.0, 30.0), rb::Vec3{0.0, 0.0, 1.0 / kR}, {1.407479, -0.921805, 0.0}, {-0.162184, 0.147018, 1.503729}};
		}
		return {"M-4", AtIncidence(3.0, 60.0), rb::Vec3{1.299038, -0.75, -1.0} / kR, {0.675253, -2.366062, 0.0}, {0.160138, -0.446190, 0.332884}};
	}

	// Typical rail hits: rolling (60 %) or partly rolled (follow / stun / draw) balls at 0.3-5 m/s, incidence 5-85 deg
	// from the cushion line, English up to |b| = 0.5 R (R w_z up to 1.25 v). Local frame.
	inline void TypicalImpact(rb::Rng& Rng, rb::Vec3& V, rb::Vec3& W)
	{
		const double Speed = 0.3 * rb::Pow(5.0 / 0.3, Rng.NextDouble01());
		V = AtIncidence(Speed, 5.0 + 80.0 * Rng.NextDouble01());
		if (Rng.NextDouble01() < 0.5)
		{
			V.x = -V.x;
		}
		const double Roll = Rng.NextDouble01() < 0.6 ? 1.0 : (Rng.NextDouble01() * 2.0 - 1.0);
		W = RollingOmega(V, kR) * Roll;
		W.z = (Rng.NextDouble01() * 2.5 - 1.25) * Speed / kR;
	}

	// Stress set: speeds 0.1-8 m/s, incidence 1-89 deg, rolling / stun / arbitrary spin up to 2 v / R per component
	// (masse-like), extra side spin on 30 %.
	inline void StressImpact(rb::Rng& Rng, rb::Vec3& V, rb::Vec3& W)
	{
		const double Speed = rb::Pow(10.0, -1.0 + Rng.NextDouble01() * 1.9);
		V = AtIncidence(Speed, 1.0 + 88.0 * Rng.NextDouble01());
		if (Rng.NextDouble01() < 0.5)
		{
			V.x = -V.x;
		}
		const double Kind = Rng.NextDouble01();
		if (Kind < 0.33)
		{
			W = RollingOmega(V, kR);
		}
		else if (Kind < 0.66)
		{
			W = {};
		}
		else
		{
			W = rb::Vec3{Rng.NextDouble01() * 4.0 - 2.0, Rng.NextDouble01() * 4.0 - 2.0, Rng.NextDouble01() * 4.0 - 2.0} * (Speed / kR);
		}
		if (Rng.NextDouble01() < 0.3)
		{
			W.z = (Rng.NextDouble01() * 4.0 - 2.0) * Speed / kR;
		}
	}

	// Heavy property sweeps run a reduced count in Debug (architecture 18).
	constexpr int SweepCount(int ReleaseCount, int DebugCount)
	{
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
		(void)ReleaseCount;
		return DebugCount;
#else
		(void)DebugCount;
		return ReleaseCount;
#endif
	}
}
