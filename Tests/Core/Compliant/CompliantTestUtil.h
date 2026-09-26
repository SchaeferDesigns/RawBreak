#pragma once

// Shared set-ups of the compliant-island tests (physics-collisions 9.4, architecture A-CLI-*). Every test pins its parameters.

#include "rb/Core/Constants.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/Compliant.h"

namespace rbcl
{
	inline constexpr double kR = 0.028575;       // [m]
	inline constexpr double kM = 0.17009713875;  // [kg]
	inline constexpr double kI = 0.4 * kM * kR * kR;
	inline constexpr double kG = 9.80665;
	inline constexpr double kAlpha095 = 0.03689; // CL tests pin alpha_T

	inline rb::IslandBody Body(int Ball, const rb::Vec3& Position, const rb::Vec3& Velocity = {}, const rb::Vec3& Omega = {}, bool Cloth = false)
	{
		rb::IslandBody B;
		B.Ball = Ball;
		B.Position = Position;
		B.Velocity = Velocity;
		B.Omega = Omega;
		B.Radius = kR;
		B.Mass = kM;
		B.Inertia = kI;
		B.ClothSupport = Cloth;
		return B;
	}

	// Pure contact dynamics (CL-1 ... CL-8): no cloth, no gravity.
	inline rb::CliParams PureCli(double Alpha = kAlpha095)
	{
		rb::CliParams P;
		P.HertzStiffness = 8.0587e8;
		P.TsujiAlpha = Alpha;
		P.TimeStep = 1.0e-6;
		P.CushionStiffness = 1.0e6;
		P.SlipRegularization = 1.0e-3;
		P.ExitZeroForceSteps = 5;
		P.RigidTimeStep = 20.0e-6;
		P.RigidIterations = 4;
		P.SustainedSpeed = 2.0e-3;
		P.SustainedSteps = 200;
		P.ClothSupport = false;
		return P;
	}

	inline rb::BallBallParams BallModel(double Restitution = 0.95, rb::BallBallFrictionModel Friction = rb::BallBallFrictionModel::Alciatore)
	{
		rb::BallBallParams P;
		P.Restitution = Restitution;
		P.Friction = Friction;
		P.MuA = 9.951e-3;
		P.MuB = 0.108;
		P.MuC = 1.088;
		P.ClingFactor = 1.0;
		return P;
	}

	inline constexpr rb::ClothParams kCloth{0.2, 0.010, 10.0};

	// Steps until CanExit() (or MaxSteps); returns the number of steps taken, -1 if it never exits.
	inline int RunToExit(rb::CompliantIsland& Island, int MaxSteps, rb::IslandRecordList* AllRecords = nullptr, int* RecordCount = nullptr)
	{
		rb::IslandRecordList Step;
		for (int s = 1; s <= MaxSteps; ++s)
		{
			Step.Clear();
			Island.Step(Step);
			if (RecordCount != nullptr)
			{
				*RecordCount += Step.Size();
			}
			if (AllRecords != nullptr)
			{
				for (const rb::IslandContactRecord& R : Step)
				{
					AllRecords->PushBack(R);
				}
			}
			if (Island.CanExit())
			{
				return s;
			}
		}
		return -1;
	}

	// Head-on chain along +x: ball 0 moving at V0, the others at rest; Gaps[i] between ball i+1 and ball i+2.
	inline void MakeChain(rb::CompliantIsland& Island, int Count, double V0, double Alpha, const double* Gaps = nullptr)
	{
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(Alpha), BallModel(0.95, rb::BallBallFrictionModel::None), kCloth, 0.0, rb::NumericsConfig{});
		double X = 0.0;
		for (int i = 0; i < Count; ++i)
		{
			if (i >= 2 && Gaps != nullptr)
			{
				X += Gaps[i - 2];
			}
			Island.AddBody(Body(i, {X, 0.0, kR}, {i == 0 ? V0 : 0.0, 0.0, 0.0}));
			X += 2.0 * kR;
		}
	}

	inline double VelocityOf(const rb::CompliantIsland& Island, int Ball)
	{
		return Island.Body(Island.FindBody(Ball)).Velocity.x;
	}
}
