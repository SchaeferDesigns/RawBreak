#pragma once

// Helpers of the cue-strike tests (physics-motion-and-cue Part B): the common test constants ("cue: squirt off,
// M19 = 19 oz, phi = 0, r0 = (0, 0, R)"; "slate: e_slate 0.6, h_min 2 mm, lambda = 0").

#include "Motion/MotionTestUtil.h"

#include "rb/Core/Constants.h"
#include "rb/Equipment/Cue.h"
#include "rb/Physics/CueStrike.h"

namespace cuetest
{
	inline constexpr double kM19 = 19.0 * rb::kOunce; // 0.5386409 kg
	inline constexpr double kM21 = 21.0 * rb::kOunce; // 0.5953400 kg
	inline constexpr double kM9 = 9.0 * rb::kOunce;   // 0.2551457 kg

	inline rb::CueStrikeInput Input(double Speed, double ElevationDeg, double A, double B, double CueMass, double TipE, double Azimuth = 0.0)
	{
		rb::CueStrikeInput In;
		In.Speed = Speed;
		In.Elevation = ElevationDeg * rb::kDegToRad;
		In.Azimuth = Azimuth;
		In.OffsetA = A;
		In.OffsetB = B;
		In.Cue = rb::kCuePlaying19oz;
		In.Cue.Mass = CueMass;
		In.Cue.TipRestitution = TipE;
		In.Cue.TipFriction = 0.6;
		In.Cue.TipFrictionKinetic = 0.6;
		In.Cue.EndMass = mottest::kM / 20.0; // m / m_e = 20
		In.Cue.JumpCue = false;
		In.LambdaOverride = 0.0; // lambda = 0 unless a test says otherwise
		In.SquirtEnabled = false;
		return In;
	}

	inline rb::BallState Resting(double Radius = mottest::kR)
	{
		rb::BallState S;
		S.Position = {0.0, 0.0, Radius};
		S.State = rb::MotionState::Stationary;
		return S;
	}

	inline rb::StrikeResult Strike(const rb::CueStrikeInput& In, const rb::BallSpec& Spec = mottest::MotSpec(), const rb::PinchParams& Pinch = {})
	{
		const rb::NumericsConfig Numerics;
		return rb::StrikeCueBall(In, Resting(Spec.Radius), Spec, mottest::MotCloth(), mottest::MotSlate(), Pinch, mottest::kG, Numerics);
	}

	inline double Degrees(double Radians) { return Radians / rb::kDegToRad; }
}
