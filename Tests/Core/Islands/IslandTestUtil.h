#pragma once

// Shared set-ups of the WP-6b tests (Tests/Core/Islands, Tests/Core/PocketFlow): a scene (table geometry + SimInput + ShotResult)
// with pinned parameters, ball placement, event queries and a small track evaluator (Analytic segments via EvaluateSegment, Sampled
// ones linear), so that the tests do not depend on WP-7's playback. Every test pins its parameters (architecture 18).

#include "rb/Core/Constants.h"
#include "rb/Core/Tolerances.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"

#include <cstring>
#include <memory>
#include <vector>

namespace isltest
{
	using rb::Vec2;
	using rb::Vec3;

	inline constexpr double kR = 0.028575;         // [m]
	inline constexpr double kM = 0.17009713875;    // [kg]
	inline constexpr double kG = 9.80665;          // COL tests
	inline constexpr double kGVal = 9.81;          // VAL tests pin g = 9.81
	inline constexpr int kFootLeft = static_cast<int>(rb::PocketId::FootLeft);

	// Every value the WP-6b tests depend on, pinned (collisions 0.4 / 9 common constants; CL tests' alpha_T).
	inline rb::PhysicsParams PinnedParams(const rb::TableSpec& Spec, double Gravity = kG)
	{
		rb::PhysicsParams P = rb::MakePhysicsParams(Spec);
		P.Origin = rb::ParamsOrigin::Explicit;
		P.Gravity = Gravity;
		P.Cloth = rb::ClothParams{0.2, 0.010, 10.0};
		P.Slate = rb::SlateParams{};
		P.Slate.Restitution = 0.6;
		P.Slate.MinBounceHeight = 0.002;
		P.Slate.MaxBounces = 10;
		P.BallBall = rb::BallBallParams{};
		P.BallBall.Restitution = 0.95;
		P.Cushion = rb::CushionParams{};
		P.Cushion.FacingRestitutionScale = Spec.FacingRestitutionScale;
		P.PocketContacts = rb::PocketContactParams{};
		P.PocketContacts.LinerRestitution = Spec.LinerRestitution;
		P.PocketContacts.LinerFriction = Spec.LinerFriction;
		P.Pockets = rb::PocketModel::GeometricLevelA;
		P.Cli = rb::CliParams{};
		P.Cli.TsujiAlpha = 0.03689;
		P.Numerics = rb::NumericsConfig{};
		P.Tilt = rb::TiltParams{};
		P.ChalkCling = false;
		return P;
	}

	// Everything one Run needs (heap: SimInput and ShotResult are large).
	struct Scene
	{
		rb::TableGeometry Table;
		rb::SimInput Input;
		rb::ShotResult Result;
		rb::ErrorCode Built = rb::ErrorCode::NotImplemented;
	};

	inline std::unique_ptr<Scene> MakeScene(const rb::TableSpec& Spec = rb::kTableNineFootPro, double Gravity = kG)
	{
		std::unique_ptr<Scene> S = std::make_unique<Scene>();
		S->Built = rb::BuildTableGeometry(Spec, S->Table);
		S->Input.Table = &S->Table;
		S->Input.Params = PinnedParams(Spec, Gravity);
		return S;
	}

	// Puts ball Ball in play in the given state (classified relative to the cloth, like the loop does).
	inline void Place(Scene& S, int Ball, const Vec3& Position, const Vec3& Velocity = {}, const Vec3& Omega = {}, double Radius = kR, double Mass = kM)
	{
		rb::SimBall& B = S.Input.Balls[Ball];
		B.InPlay = true;
		B.Spec = rb::MakeBallSpec(Radius, Mass);
		B.State.Position = Position;
		B.State.Velocity = Velocity;
		B.State.Omega = Omega;
		B.State.State = rb::MotionState::Sliding;
		rb::ClassifyState(B.State, Radius, 0.0, S.Input.Params.Numerics);
	}

	inline rb::SimStatus Run(Scene& S)
	{
		rb::Simulator Sim;
		return Sim.Run(S.Input, S.Result);
	}

	inline int Count(const rb::ShotResult& R, rb::ShotEventType Type, int Ball = -1)
	{
		int N = 0;
		for (const rb::ShotEvent& E : R.Events)
		{
			N += E.Type == Type && (Ball < 0 || E.A == Ball || E.B == Ball) ? 1 : 0;
		}
		return N;
	}

	inline const rb::ShotEvent* First(const rb::ShotResult& R, rb::ShotEventType Type, int Ball = -1)
	{
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Type == Type && (Ball < 0 || E.A == Ball || E.B == Ball))
			{
				return &E;
			}
		}
		return nullptr;
	}

	inline int IndexOf(const rb::ShotResult& R, const rb::ShotEvent* E) { return E != nullptr ? static_cast<int>(E - R.Events.data()) : -1; }

	// Center of ball Ball at time T from its recorded track (Analytic: EvaluateSegment; Sampled: linear; Terminal: frozen).
	inline bool TrackPosition(const rb::ShotResult& R, int Ball, double T, Vec3& Out)
	{
		const std::vector<rb::TrajectorySegment>& Segs = R.Tracks[Ball].Segments;
		for (const rb::TrajectorySegment& Seg : Segs)
		{
			if (T < Seg.Motion.T0 || T > Seg.T1)
			{
				continue;
			}
			switch (Seg.Kind)
			{
			case rb::SegmentKind::Analytic:
				Out = rb::EvaluateSegment(Seg.Motion, T - Seg.Motion.T0).Position;
				return true;
			case rb::SegmentKind::Sampled:
			{
				const double Span = Seg.T1 - Seg.Motion.T0;
				const double U = Span > 0.0 && Span < rb::kInfinity ? (T - Seg.Motion.T0) / Span : 0.0;
				Out = Seg.Motion.Pos0 + (Seg.EndPosition - Seg.Motion.Pos0) * U;
				return true;
			}
			case rb::SegmentKind::Terminal:
				Out = Seg.Motion.Pos0;
				return true;
			}
		}
		return false;
	}

	// Every segment boundary (and the midpoint of every segment) of the given balls' tracks up to TMax: the times at which
	// tests check gaps.
	inline std::vector<double> TrackTimes(const rb::ShotResult& R, std::uint32_t Balls, double TMax)
	{
		std::vector<double> Times;
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			if (((Balls >> b) & 1u) == 0)
			{
				continue;
			}
			for (const rb::TrajectorySegment& Seg : R.Tracks[b].Segments)
			{
				if (Seg.Motion.T0 <= TMax)
				{
					Times.push_back(Seg.Motion.T0);
				}
				const double End = Seg.T1 < TMax ? Seg.T1 : TMax;
				if (End > Seg.Motion.T0)
				{
					Times.push_back(0.5 * (Seg.Motion.T0 + End));
					Times.push_back(End);
				}
			}
		}
		return Times;
	}

	// Smallest ball-ball gap of the pair over the given times (from the tracks) [m].
	inline double MinPairGap(const rb::ShotResult& R, int A, int B, double RA, double RB, const std::vector<double>& Times)
	{
		double Min = rb::kInfinity;
		for (double T : Times)
		{
			Vec3 PA;
			Vec3 PB;
			if (TrackPosition(R, A, T, PA) && TrackPosition(R, B, T, PB))
			{
				Min = rb::Min(Min, rb::Length(PB - PA) - (RA + RB));
			}
		}
		return Min;
	}

	// Bitwise equality of two states (position, velocity, spin and the motion state; BallState's padding bytes are not compared).
	inline bool SameState(const rb::BallState& A, const rb::BallState& B)
	{
		return std::memcmp(&A.Position, &B.Position, sizeof(rb::Vec3)) == 0 && std::memcmp(&A.Velocity, &B.Velocity, sizeof(rb::Vec3)) == 0 &&
			std::memcmp(&A.Omega, &B.Omega, sizeof(rb::Vec3)) == 0 && A.State == B.State;
	}

	inline double KineticEnergy(const rb::BallState& S, double Mass = kM, double Radius = kR)
	{
		return 0.5 * Mass * rb::LengthSquared(S.Velocity) + 0.5 * (0.4 * Mass * Radius * Radius) * rb::LengthSquared(S.Omega);
	}
}
