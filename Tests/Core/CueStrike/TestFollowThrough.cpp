// Follow-through tip path and tip re-contacts (architecture 8.6, section 15 row 18; architecture decision, no spec IDs),
// the strike with a general inertia factor (architecture 7.2), frame and contact-point helpers (MOT B.2, B.3). WP-1.

#include "rbtest.h"

#include "CueStrike/CueTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"

#include <cmath>

using namespace mottest;
using namespace cuetest;
using rb::BallState;
using rb::CueTipPath;
using rb::MotionSegment;
using rb::MotionState;
using rb::StrikeResult;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	double TipGap(const MotionSegment& Tip, const MotionSegment& Ball, double T)
	{
		const Vec3 A = rb::EvaluateSegment(Tip, T - Tip.T0).Position;
		const Vec3 B = rb::EvaluateSegment(Ball, T - Ball.T0).Position;
		return rb::Length(B - A) - (Tip.Radius + Ball.Radius);
	}
}

RB_TEST(MOT_Frame_RightHandedTriadAndContactPoint)
{
	rb::Rng Rng(0xF4u);
	for (int Case = 0; Case < 1000; ++Case)
	{
		const double Theta = Rng.NextUniform(0.0, 1.5);
		const double Phi = Rng.NextUniform(-rb::kPi, rb::kPi);
		const rb::CueFrame F = rb::MakeCueFrame(Theta, Phi);
		RB_CHECK_NEAR(rb::Length(F.Axis), 1.0, 1e-15);
		RB_CHECK_NEAR(rb::Dot(F.Axis, F.Right), 0.0, 1e-15);
		RB_CHECK_NEAR(rb::Dot(F.Axis, F.Up), 0.0, 1e-15);
		const Vec3 Up = rb::Cross(F.Right, F.Axis); // e_u = e_r x d
		RB_CHECK_NEAR(Up.x, F.Up.x, 1e-15);
		RB_CHECK_NEAR(Up.y, F.Up.y, 1e-15);
		RB_CHECK_NEAR(Up.z, F.Up.z, 1e-15);
		RB_CHECK(F.Right.z == 0.0);
		const double A = Rng.NextUniform(-0.6, 0.6);
		const double B = Rng.NextUniform(-0.6, 0.6);
		const Vec3 Q = rb::CueContactPoint(F, A, B, kR);
		RB_CHECK_NEAR(rb::Length(Q), kR, 1e-15);
		// Q x d = R (a e_u - b e_r) (B.6).
		const Vec3 QxD = rb::Cross(Q, F.Axis);
		const Vec3 Expected = (F.Up * A - F.Right * B) * kR;
		RB_CHECK_NEAR(QxD.x, Expected.x, 1e-15);
		RB_CHECK_NEAR(QxD.y, Expected.y, 1e-15);
		RB_CHECK_NEAR(QxD.z, Expected.z, 1e-15);
	}
	// Level cue along +x: e_r = -y (shooter's right), e_u = +z.
	const rb::CueFrame Level = rb::MakeCueFrame(0.0, 0.0);
	RB_CHECK(Level.Axis == Vec3(1.0, 0.0, 0.0) && Level.Right == Vec3(0.0, -1.0, 0.0) && Level.Up == Vec3(0.0, 0.0, 1.0));
	RB_CHECK_NEAR(rb::PinchLambda(50.0 * rb::kDegToRad, rb::kCuePlaying19oz, rb::PinchParams{}), 0.5, 1e-15);
	RB_CHECK(rb::PinchLambda(20.0 * rb::kDegToRad, rb::kCuePlaying19oz, rb::PinchParams{}) == 0.0);
	RB_CHECK(rb::PinchLambda(80.0 * rb::kDegToRad, rb::kCuePlaying19oz, rb::PinchParams{}) == 1.0);
	RB_CHECK(rb::PinchLambda(70.0 * rb::kDegToRad, rb::kCueJump9oz, rb::PinchParams{}) > 0.0);
	RB_CHECK(rb::PinchLambda(55.0 * rb::kDegToRad, rb::kCueJump9oz, rb::PinchParams{}) == 0.0);
}

RB_TEST(MOT_Strike_GeneralInertiaFactor)
{
	for (double InertiaK : {0.35, 0.4, 0.5, 2.0 / 3.0})
	{
		rb::BallSpec Spec = MotSpec();
		Spec.Inertia = InertiaK * Spec.Mass * Spec.Radius * Spec.Radius;
		// Natural roll immediately at b = k (w R = v); SRF = rho / k; J with rho^2 / k.
		const StrikeResult Roll = Strike(Input(2.0, 0.0, 0.0, InertiaK < 0.5 ? InertiaK : 0.45, kM19, 0.75, 0.3), Spec);
		if (InertiaK < 0.5)
		{
			const Vec3 U = rb::SlipVelocity(Roll.State.Velocity, Roll.State.Omega, kR);
			RB_CHECK_NEAR(rb::Length(U), 0.0, 1e-12);
			RB_CHECK(Roll.State.State == MotionState::Rolling);
		}
		const double B = InertiaK < 0.5 ? InertiaK : 0.45;
		const double J = 1.75 * 2.0 / (1.0 / kM19 + (1.0 + B * B / InertiaK) / kM);
		RB_CHECK_NEAR(Roll.Impulse, J, 1e-12);
		const double Srf = rb::Length(Roll.OmegaAfterTip) * kR / rb::Length(Roll.VelocityAfterTip);
		RB_CHECK_NEAR(Srf, B / InertiaK, 1e-12);
		RB_CHECK_NEAR(Roll.SeparationMargin, InertiaK * 0.75 * (1.0 + kM / kM19) - B * B, 1e-12);
		// Squirt generalised with 1/k.
		const double C = std::sqrt(1.0 - 0.09);
		RB_CHECK_NEAR(rb::SquirtAngle(0.3, 20.0, InertiaK), std::atan2(0.3 * C / InertiaK, 21.0 + 0.91 / InertiaK), 1e-15);
	}
}

RB_TEST(MOT_Tip_PathAfterTheStrike)
{
	const rb::CueStrikeInput In = Input(3.0, 8.0, 0.2, -0.3, kM19, 0.73, 0.4);
	const StrikeResult R = Strike(In);
	RB_REQUIRE(R.Error == rb::ErrorCode::Ok);
	const Vec3 Center{0.0, 0.0, kR};
	CueTipPath Path = rb::MakeCueTipPath(In, R, Center, kR);
	const rb::CueFrame F = rb::MakeCueFrame(In.Elevation, In.Azimuth);
	// The dome touches the ball at the contact point Q: |C - center| = R + r_tip along Q.
	const Vec3 Q = rb::CueContactPoint(F, In.OffsetA, In.OffsetB, kR);
	RB_CHECK_NEAR(rb::Length(Path.Start - Center), kR + In.Cue.TipDomeRadius, 1e-15);
	const Vec3 Along = Q * ((kR + In.Cue.TipDomeRadius) / kR);
	RB_CHECK_NEAR(Path.Start.x - Center.x, Along.x, 1e-15);
	RB_CHECK_NEAR(Path.Start.y - Center.y, Along.y, 1e-15);
	RB_CHECK_NEAR(Path.Start.z - Center.z, Along.z, 1e-15);
	RB_CHECK(Path.Direction == F.Axis);
	RB_CHECK(Path.Speed0 == R.CueSpeedAfter && Path.Speed0 > 0.0);
	RB_CHECK(Path.StartTime == 0.0 && Path.DomeRadius == In.Cue.TipDomeRadius);
	RB_CHECK_NEAR(Path.Deceleration, Path.Speed0 * Path.Speed0 / (2.0 * In.Cue.FollowThroughDistance), 1e-12);
	RB_CHECK_NEAR(Path.StopTime, 2.0 * In.Cue.FollowThroughDistance / Path.Speed0, 1e-15);
	// As a segment: a no-gravity quadratic that travels exactly FollowThroughDistance and stops.
	const MotionSegment Tip = rb::CueTipAsSegment(Path);
	RB_CHECK(Tip.State == MotionState::Airborne && Tip.Radius == In.Cue.TipDomeRadius && Tip.T0 == 0.0);
	RB_CHECK_NEAR(Tip.TauEnd, Path.StopTime, 1e-15);
	const BallState Stop = rb::EvaluateSegment(Tip, Tip.TauEnd);
	RB_CHECK_NEAR(rb::Length(Stop.Position - Path.Start), In.Cue.FollowThroughDistance, 1e-14);
	RB_CHECK_NEAR(rb::Length(Stop.Velocity), 0.0, 1e-14);
	RB_CHECK(rb::EvaluateSegment(Tip, 10.0).Position == Stop.Position); // at rest after StopTime
	// A strike that failed leaves a cue at rest.
	rb::CueStrikeInput Bad = In;
	Bad.OffsetA = 0.99;
	const CueTipPath Idle = rb::MakeCueTipPath(Bad, Strike(Bad), Center, kR);
	RB_CHECK(Idle.Speed0 == 0.0 && Idle.StopTime == Idle.StartTime);
}

RB_TEST(MOT_Tip_DoubleHitGeometryMatchesSeparationMargin)
{
	// Level follow / side strikes: the follow-through tip catches the struck ball again iff SeparationMargin < 0 (B.6).
	struct Case
	{
		double A, B, TipE;
	};
	const Case Cases[] = {{0.0, 0.45, 0.73}, {0.3, 0.3, 0.73}, {0.0, 0.5, 0.2}, {0.45, 0.0, 0.3}, {0.0, 0.0, 0.1}};
	for (const Case& C : Cases)
	{
		const rb::CueStrikeInput In = Input(2.0, 0.0, C.A, C.B, kM19, C.TipE);
		const StrikeResult R = Strike(In);
		RB_REQUIRE(R.Error == rb::ErrorCode::Ok && !R.Miscue);
		const MotionSegment Ball = rb::MakeSegment(R.State, 0.0, MotSpec(), MotCloth(), 0.0, kG);
		const MotionSegment Tip = rb::CueTipAsSegment(rb::MakeCueTipPath(In, R, Resting().Position, kR));
		bool Recontact = false;
		for (int i = 1; i <= 2000 && !Recontact; ++i)
		{
			Recontact = TipGap(Tip, Ball, 1e-5 * i) < -1e-12; // first 20 ms
		}
		RB_CHECK(Recontact == (R.SeparationMargin < 0.0));
	}
}

RB_TEST(MOT_Tip_RecontactImpulse)
{
	const rb::CueSpec Cue = Input(1.0, 0.0, 0.0, 0.0, kM19, 0.73).Cue;
	const rb::BallSpec Spec = MotSpec();
	CueTipPath Path;
	Path.Strike = 1;
	Path.StruckBall = 3;
	Path.Start = {-kR - Cue.TipDomeRadius, 0.0, kR};
	Path.Direction = {1.0, 0.0, 0.0};
	Path.Speed0 = 1.2;
	Path.Deceleration = 2.0;
	Path.StartTime = 0.1;
	Path.StopTime = 0.1 + 1.2 / 2.0;
	Path.DomeRadius = Cue.TipDomeRadius;

	// Head-on: a slower ball ahead of the tip (double hit). The ball sits where the tip is at t = 0.15.
	const double T = 0.15;
	const double Tau = T - Path.StartTime;
	const double Travel = 1.2 * Tau - 0.5 * 2.0 * Tau * Tau;
	BallState Ball;
	Ball.Position = {Travel, 0.0, kR};
	Ball.Velocity = {0.3, 0.0, 0.0};
	Ball.Omega = {0.0, 0.3 / kR, 0.0};
	Ball.State = MotionState::Rolling;
	const rb::TipRecontactResult Hit = rb::ResolveTipRecontact(Path, T, Ball, Spec, Cue, MotCloth(), MotSlate(), kG, kNumerics);
	const double TipSpeed = 1.2 - 2.0 * Tau;
	RB_CHECK_NEAR(Hit.RelativeSpeed, TipSpeed - 0.3, 1e-12);
	RB_CHECK(Hit.Impulse > 0.0);
	// Momentum along d: m dv = -M dV; restitution e_tip on the relative speed along the axis (center hit).
	RB_CHECK_NEAR(kM * (Hit.Ball.Velocity.x - 0.3), Cue.Mass * (TipSpeed - Hit.Tip.Speed0), 1e-12);
	RB_CHECK_NEAR(Hit.Ball.Velocity.x - Hit.Tip.Speed0, Cue.TipRestitution * (TipSpeed - 0.3), 1e-12);
	RB_CHECK(Hit.Ball.Velocity.y == 0.0 && Hit.Ball.Velocity.z == 0.0);
	RB_CHECK(Hit.Ball.State == MotionState::Sliding);
	RB_CHECK(Hit.Tip.Strike == 1 && Hit.Tip.StruckBall == 3 && Hit.Tip.StartTime == T);
	RB_CHECK_NEAR(Hit.Tip.Start.x, Path.Start.x + Travel, 1e-15);
	RB_CHECK(Hit.Tip.Deceleration == Path.Deceleration); // the arm keeps braking at the same rate
	RB_CHECK_NEAR(Hit.Tip.StopTime, T + Hit.Tip.Speed0 / Path.Deceleration, 1e-15);

	// Separating (ball faster than the tip): no impulse, nothing changes.
	BallState Fast = Ball;
	Fast.Velocity = {1.5, 0.0, 0.0};
	Fast.Omega = {0.0, 1.5 / kR, 0.0};
	const rb::TipRecontactResult None = rb::ResolveTipRecontact(Path, T, Fast, Spec, Cue, MotCloth(), MotSlate(), kG, kNumerics);
	RB_CHECK(None.Impulse == 0.0 && None.RelativeSpeed < 0.0);
	RB_CHECK(SameBits(None.Ball.Velocity, Fast.Velocity) && None.Tip.Speed0 == Path.Speed0);

	// Another ball touched off-axis inside the friction cone (grip): impulse along the cue axis, spin about the offset.
	const double Reach = kR + Cue.TipDomeRadius;
	const Vec3 TipCenter = Path.Start + Path.Direction * Travel;
	BallState Other;
	const double Offset = 0.3 * Reach; // sin(psi) = 0.3 < rho_max
	Other.Position = {TipCenter.x + std::sqrt(Reach * Reach - Offset * Offset), Offset, kR};
	Other.State = MotionState::Stationary;
	RB_CHECK_NEAR(rb::Length(Other.Position - TipCenter), Reach, 1e-12);
	const rb::TipRecontactResult Grip = rb::ResolveTipRecontact(Path, T, Other, Spec, Cue, MotCloth(), MotSlate(), kG, kNumerics);
	RB_CHECK(Grip.Impulse > 0.0);
	RB_CHECK_NEAR(Grip.Ball.Velocity.y, 0.0, 1e-15); // p_hat = d
	RB_CHECK(Grip.Ball.Velocity.x > 0.0 && Grip.Ball.Omega.z > 0.0); // pushed on its -y side: counter-clockwise
	// Grazing contact outside the cone: impulse on the cone edge, pushes the ball sideways.
	BallState Graze = Other;
	const double Wide = 0.8 * Reach;
	Graze.Position = {TipCenter.x + std::sqrt(Reach * Reach - Wide * Wide), Wide, kR};
	const rb::TipRecontactResult Edge = rb::ResolveTipRecontact(Path, T, Graze, Spec, Cue, MotCloth(), MotSlate(), kG, kNumerics);
	RB_CHECK(Edge.Impulse > 0.0 && Edge.Ball.Velocity.y > 0.0 && Edge.Ball.Velocity.x > 0.0);
	// Energy never grows for e_tip <= 1.
	for (const rb::TipRecontactResult* H : {&Hit, &Grip, &Edge})
	{
		const BallState& Pre = H == &Hit ? Ball : (H == &Grip ? Other : Graze);
		const double Before = rb::MechanicalEnergy(Pre, Spec, kG) + 0.5 * Cue.Mass * TipSpeed * TipSpeed;
		const double After = rb::MechanicalEnergy(H->Ball, Spec, kG) + 0.5 * Cue.Mass * H->Tip.Speed0 * H->Tip.Speed0;
		RB_CHECK(After <= Before * (1.0 + 1e-12));
	}
}
