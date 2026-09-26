// Adversarial review tests of the cue strike and the tip re-contacts (WP-1 review; no spec IDs): momentum and angular
// momentum of the tip impulse, energy, mirror symmetry, extreme inputs, NaN guards.

#include "rbtest.h"

#include "CueStrike/CueTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

#include <cmath>

using namespace mottest;
using namespace cuetest;
using rb::BallState;
using rb::MotionState;
using rb::StrikeResult;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	bool Finite(const Vec3& V) { return std::isfinite(V.x) && std::isfinite(V.y) && std::isfinite(V.z); }

	double BallEnergy(const Vec3& V, const Vec3& W, const rb::BallSpec& Spec)
	{
		return 0.5 * Spec.Mass * rb::LengthSquared(V) + 0.5 * Spec.Inertia * rb::LengthSquared(W);
	}

	rb::CueStrikeInput RandomInput(rb::Rng& Rng)
	{
		double A = 0.0;
		double B = 0.0;
		do
		{
			A = Rng.NextUniform(-0.94, 0.94);
			B = Rng.NextUniform(-0.94, 0.94);
		} while (A * A + B * B >= 0.94 * 0.94);
		rb::CueStrikeInput In = Input(Rng.NextUniform(0.0, 15.0), Rng.NextUniform(0.0, 89.9), A, B, Rng.NextUniform(0.2, 0.7),
			Rng.NextUniform(0.0, 1.0), Rng.NextUniform(-rb::kPi, rb::kPi));
		In.Cue.TipFriction = Rng.NextUniform(0.0, 1.0);
		In.Cue.TipFrictionKinetic = Rng.NextUniform(0.0, 1.0);
		In.SquirtEnabled = Rng.NextUniform(0.0, 1.0) < 0.5;
		In.Cue.EndMass = kM / Rng.NextUniform(10.0, 50.0);
		return In;
	}
}

RB_TEST(MOT_Review_StrikeMomentumAngularMomentumEnergy)
{
	// Tip impulse alone (VelocityAfterTip / OmegaAfterTip, lambda = 0): the cue's axial momentum goes to the ball
	// (m v . d + M V' = M V), the ball's angular momentum about the contact point Q stays 0 without squirt
	// (I w = m Q x v), and the kinetic energy of cue + ball never grows (restitution <= 1 on the impulse direction).
	// The slate reaction afterwards only removes energy.
	rb::Rng Rng(0xC0E5u);
	const rb::BallSpec Spec = MotSpec();
	int Grips = 0;
	int Miscues = 0;
	for (int Case = 0; Case < 20000; ++Case)
	{
		const rb::CueStrikeInput In = RandomInput(Rng);
		const StrikeResult R = Strike(In, Spec);
		RB_REQUIRE(R.Error == rb::ErrorCode::Ok);
		(R.Miscue ? Miscues : Grips) += 1;
		const rb::CueFrame F = rb::MakeCueFrame(In.Elevation, In.Azimuth);
		const double M = In.Cue.Mass;
		const double V = In.Speed;
		RB_CHECK_NEAR(kM * rb::Dot(R.VelocityAfterTip, F.Axis) + M * R.CueSpeedAfter, M * V, 1e-12 * rb::Max(1.0, M * V));
		if (!In.SquirtEnabled || R.Miscue)
		{
			const Vec3 Q = rb::CueContactPoint(F, In.OffsetA, In.OffsetB, kR);
			const Vec3 L = R.OmegaAfterTip * Spec.Inertia - rb::Cross(Q, R.VelocityAfterTip) * kM;
			RB_CHECK(rb::Length(L) <= 1e-15 * rb::Max(1.0, V));
		}
		const double Before = 0.5 * M * V * V;
		const double AfterTip = BallEnergy(R.VelocityAfterTip, R.OmegaAfterTip, Spec) + 0.5 * M * R.CueSpeedAfter * R.CueSpeedAfter;
		RB_CHECK(AfterTip <= Before * (1.0 + 1e-12) + 1e-300);
		if (In.Cue.TipRestitution == 1.0 && !R.Miscue && !In.SquirtEnabled)
		{
			RB_CHECK_NEAR(AfterTip, Before, 1e-12 * Before);
		}
		RB_CHECK(BallEnergy(R.State.Velocity, R.State.Omega, Spec) <= BallEnergy(R.VelocityAfterTip, R.OmegaAfterTip, Spec) * (1.0 + 1e-12) + 1e-300);
		RB_CHECK(Finite(R.State.Position) && Finite(R.State.Velocity) && Finite(R.State.Omega));
		// The slate reaction keeps the Coriolis invariant (A.5): the rolling velocity is fixed by the tip impulse.
		const Vec3 L0 = rb::CoriolisInvariant(R.VelocityAfterTip, R.OmegaAfterTip, kR);
		const Vec3 L1 = rb::CoriolisInvariant(R.State.Velocity, R.State.Omega, kR);
		RB_CHECK(std::fabs(L0.x - L1.x) <= 1e-12 * rb::Max(1.0, V) && std::fabs(L0.y - L1.y) <= 1e-12 * rb::Max(1.0, V));
	}
	RB_CHECK(Grips > 2000 && Miscues > 2000);
}

RB_TEST(MOT_Review_PinchedStrikeEnergyForLeatherTips)
{
	// The pinch model (B.8.2 / B.8.3) with a leather tip (e_tip <= 0.73) never adds energy for any lambda and elevation.
	// (With e_tip -> 1 and lambda -> 1 the rebound e_pinch w_n of the blocked "virtual" speed exceeds the tip's loss:
	// a spec-model limit reported by the WP-1 review, not tested here.)
	rb::Rng Rng(0x91C4u);
	const rb::BallSpec Spec = MotSpec();
	for (int Case = 0; Case < 20000; ++Case)
	{
		rb::CueStrikeInput In = RandomInput(Rng);
		In.Cue.TipRestitution = Rng.NextUniform(0.0, 0.73);
		In.LambdaOverride = Rng.NextUniform(0.0, 1.0);
		const StrikeResult R = Strike(In, Spec);
		RB_REQUIRE(R.Error == rb::ErrorCode::Ok);
		const double Before = 0.5 * In.Cue.Mass * In.Speed * In.Speed;
		const double After = BallEnergy(R.State.Velocity, R.State.Omega, Spec) + 0.5 * In.Cue.Mass * R.CueSpeedAfter * R.CueSpeedAfter;
		RB_CHECK(After <= Before * (1.0 + 1e-12) + 1e-300);
	}
}

RB_TEST(MOT_Review_StrikeMirrorSymmetry)
{
	// a -> -a mirrors the strike about the vertical plane of the stroke: v_y -> -v_y, w -> (-w_x, w_y, -w_z) for phi = 0.
	rb::Rng Rng(0x3141u);
	for (int Case = 0; Case < 5000; ++Case)
	{
		rb::CueStrikeInput In = RandomInput(Rng);
		In.Azimuth = 0.0;
		rb::CueStrikeInput Mirror = In;
		Mirror.OffsetA = -In.OffsetA;
		const StrikeResult A = Strike(In);
		const StrikeResult B = Strike(Mirror);
		RB_REQUIRE(A.Error == rb::ErrorCode::Ok && B.Error == rb::ErrorCode::Ok);
		RB_CHECK(A.Miscue == B.Miscue && A.State.State == B.State.State);
		const double Scale = rb::Max(1.0, In.Speed);
		RB_CHECK(std::fabs(A.State.Velocity.x - B.State.Velocity.x) <= 1e-14 * Scale);
		RB_CHECK(std::fabs(A.State.Velocity.y + B.State.Velocity.y) <= 1e-14 * Scale);
		RB_CHECK(std::fabs(A.State.Velocity.z - B.State.Velocity.z) <= 1e-14 * Scale);
		RB_CHECK(std::fabs(A.State.Omega.x + B.State.Omega.x) <= 1e-11 * Scale);
		RB_CHECK(std::fabs(A.State.Omega.y - B.State.Omega.y) <= 1e-11 * Scale);
		RB_CHECK(std::fabs(A.State.Omega.z + B.State.Omega.z) <= 1e-11 * Scale);
		RB_CHECK(std::fabs(A.CueSpeedAfter - B.CueSpeedAfter) <= 1e-14 * Scale);
	}
}

RB_TEST(MOT_Review_StrikeExtremeInputs)
{
	const rb::BallSpec Spec = MotSpec();
	// V = 0: nothing moves.
	const StrikeResult Still = Strike(Input(0.0, 30.0, 0.2, -0.3, kM19, 0.73));
	RB_CHECK(Still.Error == rb::ErrorCode::Ok && Still.Impulse == 0.0 && Still.State.State == MotionState::Stationary);
	RB_CHECK(Still.State.Velocity == Vec3::Zero() && Still.State.Omega == Vec3::Zero() && Still.CueSpeedAfter == 0.0);
	// Edges of the valid domain and extreme equipment: finite, classified, energy bounded.
	struct Edge
	{
		double V, ThetaDeg, A, B, CueMass, TipE, MuTip, MuTipK;
	};
	const Edge Edges[] = {
		{15.0, 0.0, 0.0, 0.0, kM19, 1.0, 0.6, 0.6},
		{15.0, 89.99999, 0.0, 0.0, kM19, 0.85, 0.6, 0.6},
		{15.0, 89.99999, 0.9, 0.3, kM19, 0.85, 0.6, 0.6},
		{2.0, 0.0, 0.9499, 0.0, kM19, 0.75, 0.6, 0.6},
		{2.0, 0.0, 0.0, -0.9499, kM19, 0.75, 0.6, 0.0},
		{2.0, 0.0, 1e-300, 0.0, kM19, 0.75, 0.0, 0.0}, // mu_tip = 0: any offset slides, t_hat nearly undefined
		{2.0, 0.0, 0.0, 0.0, kM19, 0.75, 0.0, 0.0},    // rho = rho_max = 0: grip
		{2.0, 5.0, 0.3, 0.2, 1e-9, 0.75, 0.6, 0.6},    // feather-light cue
		{2.0, 5.0, 0.3, 0.2, 1e12, 0.75, 0.6, 0.6},    // immovable cue
		{2.0, 45.0, 0.3, 0.2, kM19, 0.0, 0.6, 0.6},    // perfectly plastic tip
		{2.0, 45.0, 0.6, 0.2, kM19, 0.75, 1e6, 1e6},   // huge friction: grip up to rho ~ 1
	};
	for (const Edge& E : Edges)
	{
		rb::CueStrikeInput In = Input(E.V, E.ThetaDeg, E.A, E.B, E.CueMass, E.TipE);
		In.Cue.TipFriction = E.MuTip;
		In.Cue.TipFrictionKinetic = E.MuTipK;
		In.SquirtEnabled = true;
		const StrikeResult R = Strike(In, Spec);
		RB_REQUIRE(R.Error == rb::ErrorCode::Ok);
		RB_CHECK(Finite(R.State.Velocity) && Finite(R.State.Omega) && Finite(R.ImpulseDirection) && std::isfinite(R.CueSpeedAfter));
		RB_CHECK(std::fabs(rb::Length(R.ImpulseDirection) - 1.0) <= 1e-12);
		RB_CHECK(R.Impulse >= 0.0);
		const double Before = 0.5 * E.CueMass * E.V * E.V;
		const double After = BallEnergy(R.State.Velocity, R.State.Omega, Spec) + 0.5 * E.CueMass * R.CueSpeedAfter * R.CueSpeedAfter;
		RB_CHECK(After <= Before * (1.0 + 1e-9));
		RB_CHECK(R.State.State != MotionState::Stationary);
	}
}

RB_TEST(MOT_Review_StrikeRejectsNonFiniteParameters)
{
	const BallState Ball = Resting();
	const double Nan = std::nan("");
	for (int Field = 0; Field < 9; ++Field)
	{
		rb::CueStrikeInput In = Input(2.0, 10.0, 0.1, 0.1, kM19, 0.73);
		In.SquirtEnabled = true;
		switch (Field)
		{
		case 0: In.Azimuth = rb::kInfinity; break;
		case 1: In.OffsetB = Nan; break;
		case 2: In.Cue.Mass = Nan; break;
		case 3: In.Cue.EndMass = 0.0; break;
		case 4: In.Cue.TipRestitution = 1.5; break;
		case 5: In.Cue.TipFriction = -0.1; break;
		case 6: In.LambdaOverride = Nan; break;
		case 7: In.LambdaOverride = 1.5; break;
		default: In.Cue.TipDomeRadius = -1e-3; break;
		}
		RB_CHECK(rb::ValidateCueStrike(In) != rb::ErrorCode::Ok);
		const StrikeResult R = rb::StrikeCueBall(In, Ball, MotSpec(), MotCloth(), MotSlate(), rb::PinchParams{}, kG, kNumerics);
		RB_CHECK(R.Error != rb::ErrorCode::Ok && SameBits(R.State.Velocity, Ball.Velocity) && SameBits(R.State.Omega, Ball.Omega));
	}
	// A broken ball spec is rejected as well (no NaN state).
	rb::BallSpec Bad = MotSpec();
	Bad.Inertia = Nan;
	const StrikeResult R = rb::StrikeCueBall(Input(2.0, 0.0, 0.0, 0.0, kM19, 0.73), Ball, Bad, MotCloth(), MotSlate(), rb::PinchParams{}, kG, kNumerics);
	RB_CHECK(R.Error == rb::ErrorCode::InvalidArgument && R.State.State == Ball.State);
}

RB_TEST(MOT_Review_TipRecontactConservation)
{
	// Random tip-ball contacts (airborne ball: no table reaction): the impulse is along p_hat, the cue's axial momentum
	// goes to the ball, the relative velocity along p_hat is reversed with e_tip, and the energy never grows.
	rb::Rng Rng(0x71B5u);
	const rb::BallSpec Spec = MotSpec();
	int Hits = 0;
	for (int Case = 0; Case < 20000; ++Case)
	{
		rb::CueSpec Cue = rb::kCuePlaying19oz;
		Cue.TipRestitution = Rng.NextUniform(0.0, 1.0);
		Cue.TipFriction = Rng.NextUniform(0.0, 1.0);
		Cue.TipFrictionKinetic = Rng.NextUniform(0.0, 1.0);
		Cue.Mass = Rng.NextUniform(0.2, 0.7);
		const double Theta = Rng.NextUniform(0.0, 1.5);
		const double Phi = Rng.NextUniform(-rb::kPi, rb::kPi);
		rb::CueTipPath Path;
		Path.Direction = rb::MakeCueFrame(Theta, Phi).Axis;
		Path.Speed0 = Rng.NextUniform(0.0, 8.0);
		Path.Deceleration = Rng.NextUniform(1.0, 50.0);
		Path.StartTime = 0.0;
		Path.StopTime = Path.Speed0 / Path.Deceleration;
		Path.DomeRadius = Cue.TipDomeRadius;
		const double T = Rng.NextUniform(0.0, Path.StopTime);
		const double Travel = Path.Speed0 * T - 0.5 * Path.Deceleration * T * T;
		Path.Start = Vec3{0.0, 0.0, 0.3} - Path.Direction * Travel; // the tip is 0.3 m up at the contact: the ball stays airborne
		const Vec3 TipCenter = Path.Start + Path.Direction * Travel;
		// Ball touching the dome in a random direction (half-space ahead of the tip mostly).
		Vec3 N{Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-1.0, 1.0)};
		N = rb::Normalized(N + Path.Direction * 0.8);
		if (rb::Length(N) < 0.5)
		{
			continue;
		}
		BallState Ball;
		Ball.Position = TipCenter + N * (kR + Cue.TipDomeRadius);
		Ball.Velocity = {Rng.NextUniform(-3.0, 3.0), Rng.NextUniform(-3.0, 3.0), Rng.NextUniform(-3.0, 3.0)};
		Ball.Omega = {Rng.NextUniform(-200.0, 200.0), Rng.NextUniform(-200.0, 200.0), Rng.NextUniform(-200.0, 200.0)};
		Ball.State = MotionState::Airborne;
		const rb::TipRecontactResult Hit = rb::ResolveTipRecontact(Path, T, Ball, Spec, Cue, MotCloth(), MotSlate(), kG, kNumerics);
		const double TipSpeed = rb::Max(0.0, Path.Speed0 - Path.Deceleration * T);
		if (!(Hit.Impulse > 0.0))
		{
			RB_CHECK(SameBits(Hit.Ball.Velocity, Ball.Velocity) && SameBits(Hit.Ball.Omega, Ball.Omega) && Hit.Tip.Speed0 == Path.Speed0);
			continue;
		}
		++Hits;
		const Vec3 Dv = Hit.Ball.Velocity - Ball.Velocity;
		const Vec3 PHat = Dv / rb::Length(Dv);
		RB_CHECK(rb::Dot(PHat, N) > 0.0); // pushes the ball away from the tip
		const double NewCue = Hit.Tip.Speed0 > 0.0 ? Hit.Tip.Speed0 : TipSpeed - Hit.Impulse * rb::Dot(Path.Direction, PHat) / Cue.Mass;
		RB_CHECK(Hit.Ball.State == MotionState::Airborne);
		RB_CHECK_NEAR(rb::Length(Dv), Hit.Impulse / kM, 1e-12 * rb::Max(1.0, Hit.Impulse / kM));
		RB_CHECK_NEAR(kM * rb::Dot(Dv, Path.Direction) + Cue.Mass * NewCue, Cue.Mass * TipSpeed, 1e-11 * rb::Max(1.0, Cue.Mass * TipSpeed));
		// Relative contact-point velocity along p_hat after = -e_tip * before.
		const Vec3 Q = -N * kR;
		const double Before = rb::Dot(Path.Direction * TipSpeed - (Ball.Velocity + rb::Cross(Ball.Omega, Q)), PHat);
		const double After = rb::Dot(Path.Direction * NewCue - (Hit.Ball.Velocity + rb::Cross(Hit.Ball.Omega, Q)), PHat);
		RB_CHECK_NEAR(After, -Cue.TipRestitution * Before, 1e-10 * rb::Max(1.0, std::fabs(Before)));
		const double E0 = BallEnergy(Ball.Velocity, Ball.Omega, Spec) + 0.5 * Cue.Mass * TipSpeed * TipSpeed;
		const double E1 = BallEnergy(Hit.Ball.Velocity, Hit.Ball.Omega, Spec) + 0.5 * Cue.Mass * NewCue * NewCue;
		RB_CHECK(E1 <= E0 * (1.0 + 1e-12));
		// The follow-through path continues from the contact with the same braking.
		RB_CHECK(Hit.Tip.StartTime == T && SameBits(Hit.Tip.Start, TipCenter));
		RB_CHECK(!(Hit.Tip.Speed0 > 0.0 && Path.Deceleration > 0.0) || Hit.Tip.Deceleration == Path.Deceleration);
	}
	RB_CHECK(Hits > 5000);
}

RB_TEST(MOT_Review_TableReactionBranches)
{
	const rb::BallSpec Spec = MotSpec();
	const rb::SlateParams Slate = MotSlate();
	const double VzMin = rb::MinBounceSpeed(Slate, kG);
	BallState S;
	S.Position = {0.0, 0.0, kR};
	S.Velocity = {1.0, 0.0, 0.5 * VzMin};
	S.Omega = {0.0, 10.0, 3.0};
	BallState Small = S;
	rb::ApplyTableReaction(Small, true, Spec, MotCloth(), Slate, kG, kNumerics);
	RB_CHECK(Small.Velocity.z == 0.0 && SameBits(Small.Omega, S.Omega)); // sub-v_z_min hop suppressed
	BallState Hop = S;
	Hop.Velocity.z = 1.5 * VzMin;
	rb::ApplyTableReaction(Hop, true, Spec, MotCloth(), Slate, kG, kNumerics);
	RB_CHECK(Hop.Velocity.z == 1.5 * VzMin); // a real hop stays
	BallState Air = S;
	Air.Velocity.z = -2.0;
	rb::ApplyTableReaction(Air, false, Spec, MotCloth(), Slate, kG, kNumerics);
	RB_CHECK(SameBits(Air.Velocity, Vec3{1.0, 0.0, -2.0})); // airborne: untouched
	BallState Down = S;
	Down.Velocity.z = -2.0;
	rb::ApplyTableReaction(Down, true, Spec, MotCloth(), Slate, kG, kNumerics);
	RB_CHECK_NEAR(Down.Velocity.z, 1.2, 1e-15); // e_slate * 2
	RB_CHECK(Down.Omega.z == 3.0);
}
