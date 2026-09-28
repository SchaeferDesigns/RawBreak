// Owner: WP-10 (validation & benchmarks). prior-art 9.3 BB-09 (Tier C): the five measured ball-ball collisions of Mathavan et al.
// 2009/2014 (prior-art 6.4) through the whole simulator. Snooker balls (M 0.1406 kg, R 26.25 mm), mu_s 0.21, e_b 0.89, mu_b 0.05
// (constant; the fit of the paper) - the Alciatore law is printed for information; mu_r 0.0127 (the measured snooker cloth, 6.2),
// g 9.81. The cue ball rolls into the resting object ball at V0; the cut angle is defined by the OB's measured direction, so the
// line-of-centres angle is solved (bisection) until the OB's post-slip direction is the measured cut. Compared after the slip
// phase: CB speed, OB speed, CB exit angle (from the incoming direction). Pass (spec): each speed within 12 %, RMS relative speed
// error <= 7 %, each CB angle within 4 deg, OB speed error <= the ideal (Wallace & Schroeder) model's in >= 4 of 5 shots.

#include "Validation/ValidationUtil.h"

using namespace rb;
using simtest::kR;

namespace
{
	struct MathavanShot
	{
		double V0;          // [m/s]
		double CutDeg;      // measured OB direction from the CB's incoming direction [deg]
		double CbSpeed;     // measured post-slip CB speed [m/s]
		double ObSpeed;     // measured post-slip OB speed [m/s]
		double CbAngleDeg;  // measured CB exit angle [deg]
		double IdealObSpeed;// ideal-model OB speed (prior-art 6.4, DERIVED) [m/s]
	};

	constexpr MathavanShot kShots[5] = {
		{1.539, 33.83, 0.816, 0.836, 35.96, 0.913},
		{1.032, 26.36, 0.520, 0.629, 33.20, 0.660},
		{1.364, 40.52, 0.925, 0.700, 30.50, 0.741},
		{1.731, 46.50, 1.275, 0.787, 27.97, 0.851},
		{0.942, 18.05, 0.365, 0.581, 29.86, 0.640},
	};

	struct Outcome
	{
		bool Ok = false;
		double CbSpeed = 0.0;
		double ObSpeed = 0.0;
		double CbAngleDeg = 0.0;
		double ObAngleDeg = 0.0;
	};

	PhysicsParams SnookerParams(bool Alciatore)
	{
		PhysicsParams P = val::TableParams(kTableNineFootPro);
		P.Cloth = ClothParams{0.21, 0.0127, 10.0};
		P.BallBall.Restitution = 0.89;
		P.BallBall.Friction = Alciatore ? BallBallFrictionModel::Alciatore : BallBallFrictionModel::Constant;
		P.BallBall.MuConstant = 0.05;
		P.Cli.TsujiAlpha = -1.0; // islands (none expected) use the same e_b
		return P;
	}

	// CB rolling along +x, arriving at V0 with the line of centres at LineDeg below its path (the OB goes to +y side for LineDeg > 0).
	Outcome Collide(const MathavanShot& S, double LineDeg, bool Alciatore)
	{
		const TableGeometry& T = simtest::NineFoot();
		const PhysicsParams P = SnookerParams(Alciatore);
		SimInput& In = val::Fresh(T, P);
		In.Record.LogTransitions = true;
		const BallSpec Ball = MakeBallSpec(0.02625, 0.1406);
		const double D = 2.0 * Ball.Radius;
		const double Line = LineDeg * kDegToRad;
		const Vec3 Object{0.3, -0.25, Ball.Radius};
		// Contact position of the CB centre, then back along -x by the approach distance.
		const Vec3 Contact = Object - Vec3{Cos(Line), Sin(Line), 0.0} * D;
		constexpr double kApproach = 0.05;
		const double V = val::RollingStartSpeed(S.V0, kApproach, P);
		val::PlaceRollingSpin(In, 0, Contact - Vec3{kApproach, 0.0, 0.0}, {V, 0.0, 0.0}, 0.0, Ball);
		simtest::Place(In, 1, Object, {}, {}, Ball);
		static ShotResult R;
		Simulator Sim;
		Outcome O;
		if (Sim.Run(In, R) != SimStatus::Ok)
		{
			return O;
		}
		const ShotEvent* Hit = e2e::FirstEvent(R, ShotEventType::BallBall, 0, 1);
		if (Hit == nullptr)
		{
			return O;
		}
		bool FoundCb = false;
		bool FoundOb = false;
		const BallState Cb = val::PostSlipState(R, 0, Hit->Time, FoundCb);
		const BallState Ob = val::PostSlipState(R, 1, Hit->Time, FoundOb);
		O.Ok = FoundCb && FoundOb;
		O.CbSpeed = val::PlanSpeed(Cb.Velocity);
		O.ObSpeed = val::PlanSpeed(Ob.Velocity);
		O.CbAngleDeg = Abs(val::PlanAngleDeg(Cb.Velocity));
		O.ObAngleDeg = val::PlanAngleDeg(Ob.Velocity);
		return O;
	}

	// The line-of-centres angle whose OB post-slip direction is the measured cut (the OB direction grows with the line angle).
	Outcome Solve(const MathavanShot& S, bool Alciatore)
	{
		double Lo = S.CutDeg - 8.0;
		double Hi = S.CutDeg + 8.0;
		for (int k = 0; k < 50; ++k)
		{
			const double Mid = 0.5 * (Lo + Hi);
			const Outcome O = Collide(S, Mid, Alciatore);
			(O.Ok && O.ObAngleDeg < S.CutDeg ? Lo : Hi) = Mid;
		}
		return Collide(S, 0.5 * (Lo + Hi), Alciatore);
	}
}

RB_TEST(Integ_VAL_BB09_Slow_MathavanMeasuredCollisions)
{
	for (int Model = 0; Model < 2; ++Model)
	{
		const bool Alciatore = Model == 1;
		double SumSq = 0.0;
		int Speeds = 0;
		int WorstSpeedOk = 0;
		int AnglesOk = 0;
		int BetterThanIdeal = 0;
		std::printf("  BB-09 (%s ball-ball friction):\n", Alciatore ? "Alciatore law, info" : "mu_b 0.05 constant");
		for (int s = 0; s < 5; ++s)
		{
			const MathavanShot& S = kShots[s];
			const Outcome O = Solve(S, Alciatore);
			RB_CHECK(O.Ok);
			const double ErrCb = (O.CbSpeed - S.CbSpeed) / S.CbSpeed;
			const double ErrOb = (O.ObSpeed - S.ObSpeed) / S.ObSpeed;
			SumSq += ErrCb * ErrCb + ErrOb * ErrOb;
			Speeds += 2;
			WorstSpeedOk += (Abs(ErrCb) <= 0.12 ? 1 : 0) + (Abs(ErrOb) <= 0.12 ? 1 : 0);
			const double AngleErr = O.CbAngleDeg - S.CbAngleDeg;
			AnglesOk += Abs(AngleErr) <= 4.0 ? 1 : 0;
			BetterThanIdeal += Abs(O.ObSpeed - S.ObSpeed) <= Abs(S.IdealObSpeed - S.ObSpeed) ? 1 : 0;
			std::printf("    shot %d: V0 %.3f cut %.2f | CB %.3f m/s (meas %.3f, %+.1f %%)  OB %.3f m/s (meas %.3f, %+.1f %%, ideal %+.1f %%)  CB angle %.2f deg (meas %.2f, %+.2f)\n",
				s + 1, S.V0, S.CutDeg, O.CbSpeed, S.CbSpeed, 100.0 * ErrCb, O.ObSpeed, S.ObSpeed, 100.0 * ErrOb,
				100.0 * (S.IdealObSpeed - S.ObSpeed) / S.ObSpeed, O.CbAngleDeg, S.CbAngleDeg, AngleErr);
		}
		const double Rms = Sqrt(SumSq / Speeds);
		std::printf("    speeds within 12 %%: %d/10, RMS %.2f %% (<= 7 %%), CB angles within 4 deg: %d/5, OB error <= ideal: %d/5 (>= 4)\n", WorstSpeedOk,
			100.0 * Rms, AnglesOk, BetterThanIdeal);
		if (!Alciatore)
		{
			RB_CHECK(WorstSpeedOk == 10);
			RB_CHECK(Rms <= 0.07);
			RB_CHECK(AnglesOk == 5);
			RB_CHECK(BetterThanIdeal >= 4);
		}
	}
}
