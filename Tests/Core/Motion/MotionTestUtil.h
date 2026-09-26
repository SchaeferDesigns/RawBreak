#pragma once

// Shared helpers of the WP-1 tests (Tests/Core/Motion, Tests/Core/CueStrike): the parameter sets pinned by the specs
// and a single-ball runner on an empty, unbounded table (segments, transitions, slate landings, tilt refresh nodes).

#include "rbtest.h"

#include "rb/Core/Constants.h"
#include "rb/Core/Tolerances.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

#include <bit>
#include <cstdint>
#include <vector>

namespace mottest
{
	// physics-motion-and-cue "Test cases": g = 9.80665, R = 0.028575, m = 0.170, mu_s 0.2, mu_r 0.010, alpha_sp 10,
	// e_slate 0.6, h_min 2 mm (architecture.md 18: MOT tests pin m = 0.170).
	inline constexpr double kG = 9.80665;
	inline constexpr double kR = 0.028575;
	inline constexpr double kM = 0.170;

	inline rb::BallSpec MotSpec() { return rb::MakeBallSpec(kR, kM); }
	inline rb::ClothParams MotCloth() { return {0.20, 0.010, 10.0}; }
	inline rb::SlateParams MotSlate()
	{
		rb::SlateParams Slate;
		Slate.Restitution = 0.6;
		Slate.MinBounceHeight = 0.002;
		Slate.MaxBounces = 10;
		return Slate;
	}

	// prior-art-and-validation 9: g = 9.81, m = 0.17009713875 kg.
	inline constexpr double kValG = 9.81;
	inline constexpr double kValM = 0.17009713875;
	inline rb::BallSpec ValSpec() { return rb::MakeBallSpec(kR, kValM); }

	inline rb::BallState OnCloth(const rb::Vec3& Velocity, const rb::Vec3& Omega, double Radius = kR)
	{
		rb::BallState S;
		S.Position = {0.0, 0.0, Radius};
		S.Velocity = Velocity;
		S.Omega = Omega;
		S.State = rb::MotionState::Stationary;
		const rb::NumericsConfig Numerics;
		rb::ClassifyState(S, Radius, 0.0, Numerics);
		return S;
	}

	// "pub" in the motion spec: |model - published| <= 1 unit in the last published digit (Decimals after the point).
	inline bool PubNear(double Model, double Published, int Decimals)
	{
		double Unit = 1.0;
		for (int i = 0; i < Decimals; ++i)
		{
			Unit *= 0.1;
		}
		return std::fabs(Model - Published) <= Unit * (1.0 + 1e-9);
	}

	// Looser variant for a single documented published value: the model value ROUNDED to the published digits lies within
	// one unit (TP B.10's bounce-3 spin 31.031 rps against the model's 31.03203 rps, 1.03 units: TP B.10 rounds its own
	// intermediate values). Never use it where PubNear holds.
	inline bool PubNearRounded(double Model, double Published, int Decimals)
	{
		double Unit = 1.0;
		for (int i = 0; i < Decimals; ++i)
		{
			Unit *= 0.1;
		}
		const double Rounded = std::round(Model / Unit) * Unit;
		return std::fabs(Rounded - Published) <= Unit * (1.0 + 1e-9);
	}

	inline std::uint64_t Bits(double X) { return std::bit_cast<std::uint64_t>(X); }
	inline bool SameBits(double A, double B) { return Bits(A) == Bits(B); }
	inline bool SameBits(const rb::Vec3& A, const rb::Vec3& B) { return SameBits(A.x, B.x) && SameBits(A.y, B.y) && SameBits(A.z, B.z); }
	inline bool SameBits(const rb::Vec2& A, const rb::Vec2& B) { return SameBits(A.x, B.x) && SameBits(A.y, B.y); }

	// Bitwise equality of every field (A-MOT-3).
	inline bool SameSegment(const rb::MotionSegment& A, const rb::MotionSegment& B)
	{
		return A.State == B.State && SameBits(A.T0, B.T0) && SameBits(A.TauEnd, B.TauEnd) && SameBits(A.Radius, B.Radius) &&
			SameBits(A.SupportZ, B.SupportZ) && SameBits(A.Pos0, B.Pos0) && SameBits(A.Vel0, B.Vel0) && SameBits(A.Accel2, B.Accel2) &&
			SameBits(A.Omega0, B.Omega0) && SameBits(A.OmegaDotH, B.OmegaDotH) && SameBits(A.OmegaZRate, B.OmegaZRate) &&
			SameBits(A.OmegaZStopTau, B.OmegaZStopTau) && A.Tilt.Active == B.Tilt.Active && A.Tilt.EndsInRefresh == B.Tilt.EndsInRefresh &&
			SameBits(A.Tilt.X0, B.Tilt.X0) && SameBits(A.Tilt.G, B.Tilt.G) && SameBits(A.Tilt.K, B.Tilt.K) && SameBits(A.Tilt.Cs, B.Tilt.Cs) &&
			SameBits(A.Tilt.XEnd, B.Tilt.XEnd);
	}

	struct RunEvent
	{
		double Time = 0.0;             // absolute [s]
		rb::MotionState From = rb::MotionState::Stationary;
		rb::BallState State;           // state after the event (landing resolved and classified)
		bool Landing = false;          // slate impact of an airborne ball
		bool Refresh = false;          // tilt chain node (state unchanged)
		int Bounce = 0;                // bounce index within the airborne sequence (landings)
		rb::SlateImpactResult Impact;  // landings only
		rb::BallState PreImpact;       // landings only: the state just before the impact
	};

	struct BallRun
	{
		std::vector<rb::MotionSegment> Segments;
		std::vector<RunEvent> Events;
		rb::BallState Final;
		double StopTime = 0.0;
	};

	// Runs one ball from absolute time T0 until it has no internal end (Stationary) or MaxSegments pieces were built.
	// Landings: ResolveSlateImpact with the 1-based bounce index of the current airborne sequence, then ClassifyState.
	inline BallRun RunBall(rb::BallState S, const rb::BallSpec& Spec, const rb::ClothParams& Cloth, const rb::SlateParams& Slate, double Gravity,
		const rb::TiltParams& Tilt = {}, double T0 = 0.0, int MaxSegments = 2000)
	{
		const rb::NumericsConfig Numerics;
		BallRun Run;
		double T = T0;
		int Bounce = 0;
		for (int Index = 0; Index < MaxSegments; ++Index)
		{
			const rb::MotionSegment Seg = rb::MakeSegment(S, T, Spec, Cloth, 0.0, Gravity, Tilt);
			Run.Segments.push_back(Seg);
			if (!(Seg.TauEnd < rb::kInfinity))
			{
				break;
			}
			RunEvent Event;
			Event.From = Seg.State;
			rb::BallState End = rb::SegmentEndState(Seg, Numerics);
			T = T + Seg.TauEnd;
			Event.Time = T;
			if (Seg.State == rb::MotionState::Airborne)
			{
				++Bounce;
				Event.Landing = true;
				Event.Bounce = Bounce;
				Event.PreImpact = End;
				Event.Impact = rb::ResolveSlateImpact(End.Velocity, End.Omega, Spec, Slate.Restitution, Cloth.SlidingFriction, Slate, Bounce, Gravity,
					Numerics);
				End.Velocity = Event.Impact.Velocity;
				End.Omega = Event.Impact.Omega;
				rb::ClassifyState(End, Spec.Radius, 0.0, Numerics);
			}
			else
			{
				Event.Refresh = Seg.Tilt.Active && Seg.Tilt.EndsInRefresh;
			}
			if (End.State != rb::MotionState::Airborne)
			{
				Bounce = 0;
			}
			Event.State = End;
			Run.Events.push_back(Event);
			S = End;
		}
		Run.Final = S;
		Run.StopTime = T;
		return Run;
	}

	// First event after which the ball is in state State (nullptr if none).
	inline const RunEvent* FirstEventInto(const BallRun& Run, rb::MotionState State)
	{
		for (const RunEvent& Event : Run.Events)
		{
			if (!Event.Refresh && Event.State.State == State)
			{
				return &Event;
			}
		}
		return nullptr;
	}
}
