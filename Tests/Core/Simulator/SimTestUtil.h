#pragma once

// Shared helpers of the WP-6a simulator tests (Tests/Core/Simulator): pinned parameter sets (COL: g = 9.80665; VAL: g = 9.81),
// table geometries built once, inputs and results in static storage (large objects), event queries and track evaluation.

#include "rb/Core/Constants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"

#include <cstdint>
#include <cstring>

namespace simtest
{
	inline constexpr double kR = 0.028575;   // pool ball radius [m]
	inline constexpr double kGCol = 9.80665; // COL / MOT gravity
	inline constexpr double kGVal = 9.81;    // VAL test tables pin g = 9.81

	// Geometry of a preset, built once (index = TablePreset).
	inline const rb::TableGeometry& Table(const rb::TableSpec& Spec)
	{
		static rb::TableGeometry Tables[8];
		static bool Built[8] = {};
		const int Index = static_cast<int>(Spec.Preset);
		if (!Built[Index])
		{
			rb::BuildTableGeometry(Spec, Tables[Index]);
			Built[Index] = true;
		}
		return Tables[Index];
	}

	inline const rb::TableGeometry& NineFoot() { return Table(rb::kTableNineFootPro); }

	// collisions 9 common constants: g 9.80665, mu_s 0.2, mu_r 0.010, alpha_sp 10, e_b 0.95 (every value pinned, VAL pitfall 9).
	inline rb::PhysicsParams ColParams()
	{
		rb::PhysicsParams P;
		P.Origin = rb::ParamsOrigin::Explicit;
		P.Gravity = kGCol;
		P.Cloth = rb::ClothParams{0.2, 0.010, 10.0};
		P.BallBall.Restitution = 0.95;
		return P;
	}

	// prior-art 9 defaults: g 9.81, mu_s 0.2, mu_r 0.01, alpha_sp 10, e_b 0.95.
	inline rb::PhysicsParams ValParams()
	{
		rb::PhysicsParams P = ColParams();
		P.Gravity = kGVal;
		return P;
	}

	// Input slots in static storage (a SimInput is ~15 KB).
	inline rb::SimInput& InputSlot(int Slot = 0)
	{
		static rb::SimInput Inputs[4];
		return Inputs[Slot];
	}

	inline rb::SimInput& NewInput(const rb::TableGeometry& T, const rb::PhysicsParams& P, int Slot = 0)
	{
		rb::SimInput& In = InputSlot(Slot);
		In = rb::SimInput{};
		In.Table = &T;
		In.Params = P;
		return In;
	}

	inline rb::ShotResult& ResultSlot(int Slot = 0)
	{
		static rb::ShotResult Results[4];
		return Results[Slot];
	}

	// Ball Id at P with velocity V and spin W (the simulator classifies the state).
	inline void Place(rb::SimInput& In, int Id, const rb::Vec3& P, const rb::Vec3& V = {}, const rb::Vec3& W = {},
		const rb::BallSpec& Spec = rb::MakeBallSpec(kR, rb::kDefaultBallMass))
	{
		rb::SimBall& Ball = In.Balls[Id];
		Ball.InPlay = true;
		Ball.Spec = Spec;
		Ball.State.Position = P;
		Ball.State.Velocity = V;
		Ball.State.Omega = W;
		const bool Moving = rb::LengthSquared(V) > 0.0 || rb::LengthSquared(W) > 0.0;
		Ball.State.State = Moving ? (P.z > Spec.Radius || V.z > 0.0 ? rb::MotionState::Airborne : rb::MotionState::Sliding) : rb::MotionState::Stationary;
	}

	// Ball Id rolling (no slip) at P with plan velocity V.
	inline void PlaceRolling(rb::SimInput& In, int Id, const rb::Vec3& P, const rb::Vec3& V, double Radius = kR)
	{
		Place(In, Id, P, V, rb::RollingOmegaH(V, Radius), rb::MakeBallSpec(Radius, rb::kDefaultBallMass));
	}

	inline rb::StrikeRequest Strike(int Ball, double Speed, double Azimuth, double Elevation = 0.0, double A = 0.0, double B = 0.0)
	{
		rb::StrikeRequest R;
		R.Ball = static_cast<rb::BallId>(Ball);
		R.Input.Speed = Speed;
		R.Input.Azimuth = Azimuth;
		R.Input.Elevation = Elevation;
		R.Input.OffsetA = A;
		R.Input.OffsetB = B;
		R.Input.Cue = rb::kCuePlaying19oz;
		return R;
	}

	// Number of events of Type (on ball A, or any ball with A = -2).
	inline int Count(const rb::ShotResult& R, rb::ShotEventType Type, int A = -2)
	{
		int N = 0;
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Type == Type && (A == -2 || E.A == A))
			{
				++N;
			}
		}
		return N;
	}

	inline const rb::ShotEvent* First(const rb::ShotResult& R, rb::ShotEventType Type, int A = -2)
	{
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Type == Type && (A == -2 || E.A == A))
			{
				return &E;
			}
		}
		return nullptr;
	}

	// State of a ball at T from its recorded track (Analytic: EvaluateSegment; Sampled: linear; Terminal: frozen).
	inline bool TrackState(const rb::ShotResult& R, int Ball, double T, rb::BallState& Out, rb::SegmentKind* Kind = nullptr)
	{
		const auto& Segments = R.Tracks[Ball].Segments;
		for (std::size_t k = 0; k < Segments.size(); ++k)
		{
			const rb::TrajectorySegment& S = Segments[k];
			const bool Last = k + 1 == Segments.size();
			if (T < S.Motion.T0 || (!Last && T >= S.T1))
			{
				continue;
			}
			if (Kind != nullptr)
			{
				*Kind = S.Kind;
			}
			switch (S.Kind)
			{
			case rb::SegmentKind::Analytic:
				Out = rb::EvaluateSegment(S.Motion, T - S.Motion.T0);
				return true;
			case rb::SegmentKind::Sampled:
			{
				const double F = S.T1 > S.Motion.T0 ? (T - S.Motion.T0) / (S.T1 - S.Motion.T0) : 0.0;
				Out = rb::BallState{};
				Out.Position = S.Motion.Pos0 + (S.EndPosition - S.Motion.Pos0) * F;
				Out.State = S.Motion.State;
				return true;
			}
			case rb::SegmentKind::Terminal:
				Out = rb::BallState{};
				Out.Position = S.Motion.Pos0;
				Out.State = S.Motion.State;
				return true;
			}
		}
		return false;
	}

	inline double KineticEnergy(const rb::BallState& S, const rb::BallSpec& Spec, double Gravity) { return rb::MechanicalEnergy(S, Spec, Gravity); }

	inline std::uint64_t Bits(double X)
	{
		std::uint64_t U = 0;
		std::memcpy(&U, &X, sizeof(U));
		return U;
	}

	// FNV-1a over 64-bit words.
	inline void Mix(std::uint64_t& H, std::uint64_t V)
	{
		for (int i = 0; i < 8; ++i)
		{
			H ^= (V >> (8 * i)) & 0xFFu;
			H *= 0x100000001B3ull;
		}
	}

	inline void MixState(std::uint64_t& H, const rb::BallState& S)
	{
		Mix(H, Bits(S.Position.x));
		Mix(H, Bits(S.Position.y));
		Mix(H, Bits(S.Position.z));
		Mix(H, Bits(S.Velocity.x));
		Mix(H, Bits(S.Velocity.y));
		Mix(H, Bits(S.Velocity.z));
		Mix(H, Bits(S.Omega.x));
		Mix(H, Bits(S.Omega.y));
		Mix(H, Bits(S.Omega.z));
		Mix(H, static_cast<std::uint64_t>(S.State));
	}

	// Hash of the serialized event log, the finals and the diagnostics counters (ROB-10).
	inline std::uint64_t ResultHash(const rb::ShotResult& R)
	{
		std::uint64_t H = 0xCBF29CE484222325ull;
		Mix(H, static_cast<std::uint64_t>(R.Status));
		Mix(H, Bits(R.StopTime));
		for (const rb::ShotEvent& E : R.Events)
		{
			Mix(H, Bits(E.Time));
			Mix(H, static_cast<std::uint64_t>(E.Type));
			Mix(H, static_cast<std::uint64_t>(static_cast<std::uint8_t>(E.A)) | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(E.B)) << 8) |
				(static_cast<std::uint64_t>(E.Feature) << 16) | (static_cast<std::uint64_t>(E.SubFeature) << 24) | (static_cast<std::uint64_t>(E.Flags) << 32));
			Mix(H, Bits(E.Normal.x));
			Mix(H, Bits(E.Normal.y));
			Mix(H, Bits(E.Normal.z));
			Mix(H, Bits(E.NormalSpeed));
			Mix(H, Bits(E.NormalImpulse));
			Mix(H, Bits(E.TangentImpulse));
			Mix(H, Bits(E.CutAngle));
			Mix(H, Bits(E.Value));
			MixState(H, E.Pre[0]);
			MixState(H, E.Pre[1]);
			MixState(H, E.Post[0]);
			MixState(H, E.Post[1]);
		}
		for (const rb::BallFinal& F : R.Finals)
		{
			Mix(H, static_cast<std::uint64_t>(F.Status));
			MixState(H, F.State);
			Mix(H, Bits(F.Time));
		}
		Mix(H, static_cast<std::uint64_t>(R.Diagnostics.EventsProcessed));
		Mix(H, static_cast<std::uint64_t>(R.Diagnostics.Predictions));
		return H;
	}

	// Plan gap of a ball center to a nose line (R_c contact distance): > 0 = free.
	inline double NoseGap(const rb::TableGeometry& T, int Cushion, const rb::Vec3& P, double Radius)
	{
		const rb::NoseSegment& Nose = T.Noses[Cushion];
		const double Rc = rb::ComputeCushionContact(Radius, Nose.Height, 0.0, false).HorizontalOffset;
		return rb::Dot(rb::XY(P) - Nose.Start, Nose.InwardNormal) - Rc;
	}
}
