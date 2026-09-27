// Tilted table (human-factors 4.5, architecture 8.11): HF-T15 ... T17 and A-MOT-3 ... A-MOT-5 (WP-1).

#include "rbtest.h"

#include "Motion/MotionTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Physics/Motion.h"

#include <cmath>
#include <vector>

using namespace mottest;
using rb::BallState;
using rb::MotionSegment;
using rb::MotionState;
using rb::PursuitState;
using rb::TiltParams;
using rb::Vec2;
using rb::Vec3;

namespace
{
	const rb::NumericsConfig kNumerics;

	TiltParams Slope(double Sx, double Sy, double Tolerance = 5e-5)
	{
		TiltParams Tilt;
		Tilt.Slope = {Sx, Sy};
		Tilt.Tolerance = Tolerance;
		return Tilt;
	}

	BallState Rolling(const Vec2& V) { return OnCloth({V.x, V.y, 0.0}, {-V.y / kR, V.x / kR, 0.0}); }

	// RK4 of the pursuit ODE dx/dt = G - K x_hat, dX/dt = x (independent oracle, as the spec's verifier).
	struct Rk4Result
	{
		Vec2 X;
		Vec2 Integral;
	};

	Rk4Result PursuitRk4(const Vec2& X0, const Vec2& G, double K, double T, int Steps)
	{
		auto F = [&](const Vec2& X) { return G - X * (K / rb::Length(X)); };
		Vec2 X = X0;
		Vec2 I;
		const double H = T / Steps;
		for (int i = 0; i < Steps; ++i)
		{
			const Vec2 K1 = F(X);
			const Vec2 K2 = F(X + K1 * (0.5 * H));
			const Vec2 K3 = F(X + K2 * (0.5 * H));
			const Vec2 K4 = F(X + K3 * H);
			const Vec2 L1 = X;
			const Vec2 L2 = X + K1 * (0.5 * H);
			const Vec2 L3 = X + K2 * (0.5 * H);
			const Vec2 L4 = X + K3 * H;
			I += (L1 + L2 * 2.0 + L3 * 2.0 + L4) * (H / 6.0);
			X += (K1 + K2 * 2.0 + K3 * 2.0 + K4) * (H / 6.0);
		}
		return {X, I};
	}

	// Rolling pursuit data of the 4.5.2 table (k = InertiaFactor(Spec)).
	struct RollProblem
	{
		Vec2 G;
		double K = 0.0;
	};

	RollProblem RollingProblem(const TiltParams& Tilt, const rb::BallSpec& Spec = MotSpec(), double MuR = 0.010)
	{
		return {rb::InPlaneGravity(Tilt, kG) / (1.0 + rb::InertiaFactor(Spec)), MuR * kG};
	}

	// Exact position (relative to the phase start) and velocity of a Sliding / Rolling phase at phase time T.
	struct ExactPhase
	{
		bool Sliding = false;
		Vec2 X0;
		Vec2 V0;
		Vec2 G;
		double K = 0.0;
		double Cs = 1.0;

		Vec2 Displacement(double T) const
		{
			const PursuitState P = rb::EvaluatePursuit(X0, G, K, T);
			if (!Sliding)
			{
				return P.Integral;
			}
			const Vec2 Lc0 = V0 - X0 * Cs;
			return Lc0 * T + G * (0.5 * (1.0 - Cs) * T * T) + P.Integral * Cs;
		}
		Vec2 Velocity(double T) const
		{
			const PursuitState P = rb::EvaluatePursuit(X0, G, K, T);
			if (!Sliding)
			{
				return P.X;
			}
			return V0 - X0 * Cs + G * ((1.0 - Cs) * T) + P.X * Cs;
		}
		Vec2 Pursuit(double T) const { return rb::EvaluatePursuit(X0, G, K, T).X; }
	};

	ExactPhase PhaseOf(const MotionSegment& First)
	{
		ExactPhase P;
		P.Sliding = First.State == MotionState::Sliding;
		P.X0 = First.Tilt.X0;
		P.V0 = XY(First.Vel0);
		P.G = First.Tilt.G;
		P.K = First.Tilt.K;
		P.Cs = First.Tilt.Cs;
		return P;
	}

	// The chain pieces of the first phase with state State of a run, node times and the max deviation from the exact path
	// (400 samples per piece, as the spec's chain oracle).
	struct ChainStats
	{
		std::vector<MotionSegment> Pieces;
		std::vector<double> Nodes;
		double MaxDeviation = 0.0;
		double PhaseStart = 0.0;
		Vec3 PhaseOrigin;
	};

	ChainStats ChainOf(const BallRun& Run, MotionState State)
	{
		ChainStats C;
		for (const MotionSegment& S : Run.Segments)
		{
			if (S.State != State)
			{
				if (!C.Pieces.empty())
				{
					break;
				}
				continue;
			}
			C.Pieces.push_back(S);
		}
		if (C.Pieces.empty() || !C.Pieces.front().Tilt.Active)
		{
			return C;
		}
		const ExactPhase Exact = PhaseOf(C.Pieces.front());
		C.PhaseStart = C.Pieces.front().T0;
		C.PhaseOrigin = C.Pieces.front().Pos0;
		for (const MotionSegment& S : C.Pieces)
		{
			C.Nodes.push_back(S.T0 + S.TauEnd);
			for (int j = 1; j < 400; ++j)
			{
				const double Tau = S.TauEnd * j / 400.0;
				const Vec3 P = rb::PositionAt(S, Tau);
				const Vec2 E = XY(C.PhaseOrigin) + Exact.Displacement(S.T0 - C.PhaseStart + Tau);
				C.MaxDeviation = rb::Max(C.MaxDeviation, rb::Length(XY(P) - E));
			}
		}
		return C;
	}
}

// ---------------------------------------------------------------------------------------------
// HF-T15 ... T17
// ---------------------------------------------------------------------------------------------

RB_TEST(HF_T15_TiltOracleRolling)
{
	{
		// (a) v0 (0.5, 0), s (0, 1e-3)
		const Vec2 V0{0.5, 0.0};
		const RollProblem P = RollingProblem(Slope(0.0, 1e-3));
		RB_CHECK_NEAR(rb::PursuitStopTime(V0, P.G, P.K), 5.124727634, 1e-9);
		const PursuitState Stop = rb::EvaluatePursuit(V0, P.G, P.K, 100.0);
		RB_CHECK(Stop.X == Vec2{} && !(Stop.Lambda < rb::kInfinity));
		RB_CHECK_NEAR(Stop.Integral.x, 1.276273166, 1e-9);
		RB_CHECK_NEAR(Stop.Integral.y, -0.045756497, 1e-9);
		const PursuitState At2 = rb::EvaluatePursuit(V0, P.G, P.K, 2.0);
		RB_CHECK_NEAR(At2.X.x, 0.303903300, 1e-9);
		RB_CHECK_NEAR(At2.X.y, -0.010810351, 1e-9);
		RB_CHECK_NEAR(At2.Integral.x, 0.803883823, 1e-9);
		RB_CHECK_NEAR(At2.Integral.y, -0.011963026, 1e-9);
		const Rk4Result Rk = PursuitRk4(V0, P.G, P.K, 2.0, 200000);
		RB_CHECK_NEAR(Rk.X.x, At2.X.x, 1e-9);
		RB_CHECK_NEAR(Rk.X.y, At2.X.y, 1e-9);
		RB_CHECK_NEAR(Rk.Integral.x, At2.Integral.x, 1e-9);
		RB_CHECK_NEAR(Rk.Integral.y, At2.Integral.y, 1e-9);

		// The same through the chain of a rolling ball: exact velocity inside a piece, position within eps_tilt.
		const BallRun Run = RunBall(Rolling(V0), MotSpec(), MotCloth(), MotSlate(), kG, Slope(0.0, 1e-3));
		bool Found = false;
		for (const MotionSegment& S : Run.Segments)
		{
			if (S.State == MotionState::Rolling && S.T0 <= 2.0 && 2.0 <= S.T0 + S.TauEnd)
			{
				const BallState E = rb::EvaluateSegmentForEvent(S, 2.0 - S.T0);
				RB_CHECK_NEAR(E.Velocity.x, 0.303903300, 1e-9);
				RB_CHECK_NEAR(E.Velocity.y, -0.010810351, 1e-9);
				RB_CHECK(rb::Length(XY(E.Position) - Vec2{0.803883823, -0.011963026}) <= 5e-5);
				Found = true;
				break;
			}
		}
		RB_CHECK(Found);
		RB_CHECK_NEAR(Run.Final.Position.x, 1.276273166, 1e-9);
		RB_CHECK_NEAR(Run.Final.Position.y, -0.045756497, 1e-9);
		RB_CHECK_NEAR(Run.StopTime, 5.124727634, 1e-9);
	}
	{
		// (b) v0 (1.0, 0.3), s (2e-3, -1e-3)
		const Vec2 V0{1.0, 0.3};
		const RollProblem P = RollingProblem(Slope(2e-3, -1e-3));
		RB_CHECK_NEAR(rb::PursuitStopTime(V0, P.G, P.K), 9.654203692, 1e-9);
		const PursuitState At = rb::EvaluatePursuit(V0, P.G, P.K, 3.3);
		RB_CHECK_NEAR(At.X.x, 0.645704992, 1e-9);
		RB_CHECK_NEAR(At.X.y, 0.224066759, 1e-9);
		RB_CHECK_NEAR(At.Integral.x, 2.714259691, 1e-9);
		RB_CHECK_NEAR(At.Integral.y, 0.868289848, 1e-9);
		const Rk4Result Rk = PursuitRk4(V0, P.G, P.K, 3.3, 200000);
		RB_CHECK_NEAR(Rk.X.x, At.X.x, 1e-9);
		RB_CHECK_NEAR(Rk.X.y, At.X.y, 1e-9);
		RB_CHECK_NEAR(Rk.Integral.x, At.Integral.x, 1e-9);
		RB_CHECK_NEAR(Rk.Integral.y, At.Integral.y, 1e-9);
	}
}

RB_TEST(HF_T16_SecantChain)
{
	const BallRun Run = RunBall(Rolling({0.5, 0.0}), MotSpec(), MotCloth(), MotSlate(), kG, Slope(0.0, 1e-3));
	const ChainStats C = ChainOf(Run, MotionState::Rolling);
	const double Nodes[10] = {1.047533, 2.004048, 2.861368, 3.608756, 4.231390, 4.707615, 5.005814, 5.110593, 5.123343, 5.124728};
	RB_REQUIRE(C.Pieces.size() == 10u);
	for (std::size_t i = 0; i < 10; ++i)
	{
		RB_CHECK_NEAR(C.Nodes[i], Nodes[i], 1e-6);
		RB_CHECK(C.Pieces[i].Tilt.Active);
		RB_CHECK(C.Pieces[i].Tilt.EndsInRefresh == (i < 9));
	}
	RB_CHECK(C.MaxDeviation <= 5e-5);
	RB_CHECK_NEAR(C.MaxDeviation, 4.27e-5, 1e-7);
	RB_CHECK(Run.Final.State == MotionState::Stationary);

	// Counts: 1 m/s 14, 0.3 m/s 8, eps 5e-4: 6 for (a).
	RB_CHECK(ChainOf(RunBall(Rolling({1.0, 0.0}), MotSpec(), MotCloth(), MotSlate(), kG, Slope(0.0, 1e-3)), MotionState::Rolling).Pieces.size() == 14u);
	RB_CHECK(ChainOf(RunBall(Rolling({0.3, 0.0}), MotSpec(), MotCloth(), MotSlate(), kG, Slope(0.0, 1e-3)), MotionState::Rolling).Pieces.size() == 8u);
	const ChainStats Coarse = ChainOf(RunBall(Rolling({0.5, 0.0}), MotSpec(), MotCloth(), MotSlate(), kG, Slope(0.0, 1e-3, 5e-4)), MotionState::Rolling);
	RB_CHECK(Coarse.Pieces.size() == 6u);
	RB_CHECK(Coarse.MaxDeviation <= 5e-4);
}

RB_TEST(HF_T17_TiltSlidingAndCollinear)
{
	// Stun v0 (2, 0), w0 0, mu_s 0.2, s (0, 3e-3).
	const TiltParams Tilt = Slope(0.0, 3e-3);
	const BallState Start = OnCloth({2.0, 0.0, 0.0}, Vec3::Zero());
	const BallRun Run = RunBall(Start, MotSpec(), MotCloth(), MotSlate(), kG, Tilt);
	const ChainStats Slide = ChainOf(Run, MotionState::Sliding);
	RB_CHECK(Slide.Pieces.size() == 3u);
	RB_CHECK(Slide.MaxDeviation <= 5e-5);
	const RunEvent* Roll = FirstEventInto(Run, MotionState::Rolling);
	RB_REQUIRE(Roll != nullptr);
	RB_CHECK_NEAR(Roll->Time, 0.291352841, 1e-9);
	RB_CHECK_NEAR(rb::MakeSegment(Start, 0.0, MotSpec(), MotCloth(), 0.0, kG).TauEnd, 0.291347489, 1e-9); // level
	RB_CHECK_NEAR(Roll->State.Velocity.x, 1.428571429, 1e-9);
	RB_CHECK_NEAR(Roll->State.Velocity.y, -0.006122561, 1e-9);
	RB_CHECK_NEAR(Roll->State.Position.x, 0.499460866, 1e-9);
	RB_CHECK_NEAR(Roll->State.Position.y, -0.001070292, 1e-9);
	const Vec3 U = rb::SlipVelocity(Roll->State.Velocity, Roll->State.Omega, kR);
	RB_CHECK_NEAR(rb::Length(U), 0.0, 1e-12);
	// Frozen slip direction (not the model): 0.178 mm off the exact end point.
	const double T = Roll->Time;
	const Vec2 Gt = rb::InPlaneGravity(Tilt, kG);
	const Vec2 Frozen = Vec2{2.0, 0.0} * T + (Gt - Vec2{0.2 * kG, 0.0}) * (0.5 * T * T);
	RB_CHECK_NEAR(rb::Length(Frozen - XY(Roll->State.Position)), 0.178e-3, 1e-6);

	// Collinear rolls v0 (0.5, 0): downhill s (-3e-3, 0), uphill s (3e-3, 0): one exact segment each.
	struct Collinear
	{
		double Sx, T, D;
	};
	for (const Collinear& C : {Collinear{-3e-3, 6.489103173, 1.622275793}, Collinear{3e-3, 4.198831465, 1.049707866}})
	{
		const TiltParams CT = Slope(C.Sx, 0.0);
		const BallRun R = RunBall(Rolling({0.5, 0.0}), MotSpec(), MotCloth(), MotSlate(), kG, CT);
		const ChainStats Chain = ChainOf(R, MotionState::Rolling);
		RB_CHECK(Chain.Pieces.size() == 1u);
		RB_CHECK_NEAR(R.StopTime, C.T, 1e-9);
		RB_CHECK_NEAR(R.Final.Position.x, C.D, 1e-9);
		RB_CHECK(R.Final.Position.y == 0.0);
		const RollProblem P = RollingProblem(CT);
		const double Decel = P.K - rb::Dot(P.G, Vec2{1.0, 0.0}); // k - |G| downhill, k + |G| uphill
		RB_CHECK_NEAR(R.StopTime, 0.5 / Decel, 1e-9 * R.StopTime);
		RB_CHECK_NEAR(R.Final.Position.x, 0.25 / (2.0 * Decel), 1e-9 * C.D);
		RB_CHECK(Chain.MaxDeviation <= 1e-12);
	}
}

// ---------------------------------------------------------------------------------------------
// A-MOT-3 ... A-MOT-5
// ---------------------------------------------------------------------------------------------

RB_TEST(ARCH_MOT3_LevelTiltIsBitwiseLevel)
{
	std::vector<BallState> States;
	States.push_back(OnCloth(Vec3::Zero(), Vec3::Zero()));
	States.push_back(OnCloth(Vec3::Zero(), {0.0, 0.0, -7.0}));
	States.push_back(OnCloth({2.0, 0.3, 0.0}, {30.0, -10.0, 5.0}));
	States.push_back(Rolling({0.7, -0.2}));
	BallState Air;
	Air.Position = {0.1, 0.2, kR + 0.01};
	Air.Velocity = {1.0, -0.5, 0.8};
	Air.Omega = {3.0, 4.0, 5.0};
	Air.State = MotionState::Airborne;
	States.push_back(Air);
	for (MotionState Owned : {MotionState::PocketFall, MotionState::PocketPivot, MotionState::Pocketed, MotionState::OffTable})
	{
		BallState P = Air;
		P.State = Owned;
		States.push_back(P);
	}
	rb::Rng Rng(0x3A3u);
	for (int i = 0; i < 1000; ++i)
	{
		States.push_back(OnCloth({Rng.NextUniform(-5.0, 5.0), Rng.NextUniform(-5.0, 5.0), 0.0},
			{Rng.NextUniform(-200.0, 200.0), Rng.NextUniform(-200.0, 200.0), Rng.NextUniform(-200.0, 200.0)}));
	}

	TiltParams Variants[3];
	Variants[1].Tolerance = 5e-4;
	Variants[1].RefreshMaxInterval = 0.5;
	Variants[2].NapResistance = 0.1; // nap resistance without a nap direction is ignored
	for (const BallState& S : States)
	{
		for (double SupportZ : {0.0, 0.035})
		{
			BallState Placed = S;
			Placed.Position.z += SupportZ;
			const MotionSegment Level = rb::MakeSegment(Placed, 0.25, MotSpec(), MotCloth(), SupportZ, kG);
			for (const TiltParams& Tilt : Variants)
			{
				RB_REQUIRE(rb::IsLevel(Tilt));
				const MotionSegment Tilted = rb::MakeSegment(Placed, 0.25, MotSpec(), MotCloth(), SupportZ, kG, Tilt);
				RB_CHECK(SameSegment(Level, Tilted));
			}
			const double Horizon = Level.TauEnd < rb::kInfinity ? Level.TauEnd : 1.0;
			for (int j = 0; j <= 4; ++j)
			{
				const BallState A = rb::EvaluateSegment(Level, Horizon * j / 4.0);
				const BallState B = rb::EvaluateSegmentForEvent(Level, Horizon * j / 4.0);
				RB_CHECK(A.State == B.State && SameBits(A.Position, B.Position) && SameBits(A.Velocity, B.Velocity) && SameBits(A.Omega, B.Omega));
			}
		}
	}

	// Nap without slope: Sliding (G = g_t = 0) and the flat rail cap (no nap there) keep the level segment bitwise.
	TiltParams NapOnly;
	NapOnly.NapPseudoSlope = {2e-4, 0.0};
	NapOnly.NapResistance = 0.1;
	const BallState Slide = OnCloth({2.0, 0.3, 0.0}, {30.0, -10.0, 5.0});
	RB_CHECK(SameSegment(rb::MakeSegment(Slide, 0.0, MotSpec(), MotCloth(), 0.0, kG), rb::MakeSegment(Slide, 0.0, MotSpec(), MotCloth(), 0.0, kG, NapOnly)));
	BallState CapRoll = Rolling({0.7, -0.2});
	CapRoll.Position.z += 0.035;
	RB_CHECK(SameSegment(rb::MakeSegment(CapRoll, 0.0, MotSpec(), MotCloth(), 0.035, kG), rb::MakeSegment(CapRoll, 0.0, MotSpec(), MotCloth(), 0.035, kG, NapOnly)));
}

RB_TEST(ARCH_MOT4_TiltedAirborneAndRestingBalls)
{
	const TiltParams Tilt = Slope(1.5e-3, -2e-3);
	const Vec2 Gt = rb::InPlaneGravity(Tilt, kG);
	for (double InertiaK : {0.4, 0.5, 2.0 / 3.0})
	{
		rb::BallSpec Spec = MotSpec();
		Spec.Inertia = InertiaK * Spec.Mass * Spec.Radius * Spec.Radius;
		BallState Air;
		Air.Position = {0.1, 0.2, kR};
		Air.Velocity = {1.2, -0.4, 1.1};
		Air.Omega = {20.0, -35.0, 12.0};
		Air.State = MotionState::Airborne;
		const MotionSegment Level = rb::MakeSegment(Air, 0.0, Spec, MotCloth(), 0.0, kG);
		const MotionSegment M = rb::MakeSegment(Air, 0.0, Spec, MotCloth(), 0.0, kG, Tilt);
		RB_CHECK(M.Accel2.x == 0.5 * Gt.x && M.Accel2.y == 0.5 * Gt.y && M.Accel2.z == -0.5 * kG);
		RB_CHECK(M.TauEnd == Level.TauEnd); // the landing time is vertical only
		RB_CHECK(!M.Tilt.Active);
		const Vec3 L0 = rb::CoriolisInvariant(Air.Velocity, Air.Omega, kR, InertiaK);
		for (int j = 1; j <= 8; ++j)
		{
			const double Tau = M.TauEnd * j / 8.0;
			const BallState E = rb::EvaluateSegment(M, Tau);
			RB_CHECK(E.Omega == Air.Omega);
			const Vec3 L = rb::CoriolisInvariant(E.Velocity, E.Omega, kR, InertiaK);
			RB_CHECK_NEAR(L.x - L0.x, Gt.x * Tau / (1.0 + InertiaK), 1e-12);
			RB_CHECK_NEAR(L.y - L0.y, Gt.y * Tau / (1.0 + InertiaK), 1e-12);
		}
		BallState Fall = Air;
		Fall.State = MotionState::PocketFall;
		const MotionSegment F = rb::MakeSegment(Fall, 0.0, Spec, MotCloth(), 0.0, kG, Tilt);
		RB_CHECK(F.Accel2.x == 0.5 * Gt.x && F.Accel2.y == 0.5 * Gt.y && !(F.TauEnd < rb::kInfinity));

		// Stationary and Spinning balls stay put (static rolling resistance holds them): bitwise the level segment.
		for (const BallState& Rest : {OnCloth(Vec3::Zero(), Vec3::Zero()), OnCloth(Vec3::Zero(), {0.0, 0.0, 15.0})})
		{
			RB_CHECK(SameSegment(rb::MakeSegment(Rest, 0.0, Spec, MotCloth(), 0.0, kG), rb::MakeSegment(Rest, 0.0, Spec, MotCloth(), 0.0, kG, Tilt)));
		}
	}
}

RB_TEST(ARCH_MOT5_ChainPieceExactStates)
{
	const TiltParams Tilt = Slope(1e-3, 2e-3);
	for (double InertiaK : {0.4, 0.5, 2.0 / 3.0})
	{
		rb::BallSpec Spec = MotSpec();
		Spec.Inertia = InertiaK * Spec.Mass * Spec.Radius * Spec.Radius;
		// A swerve-like slide that curves (u not parallel to G), then a curving roll.
		const BallState Start = OnCloth({1.5, 0.4, 0.0}, {25.0, -10.0, 8.0});
		RB_REQUIRE(Start.State == MotionState::Sliding);
		const BallRun Run = RunBall(Start, Spec, MotCloth(), MotSlate(), kG, Tilt);
		for (MotionState Phase : {MotionState::Sliding, MotionState::Rolling})
		{
			const ChainStats C = ChainOf(Run, Phase);
			RB_REQUIRE(C.Pieces.size() >= 2u);
			RB_CHECK(C.MaxDeviation <= Tilt.Tolerance);
			const ExactPhase Exact = PhaseOf(C.Pieces.front());
			if (Phase == MotionState::Sliding)
			{
				RB_CHECK_NEAR(Exact.K, 0.2 * kG * (1.0 + InertiaK) / InertiaK, 1e-15);
				RB_CHECK_NEAR(Exact.Cs, InertiaK / (1.0 + InertiaK), 1e-15);
				RB_CHECK_NEAR(C.Pieces.back().T0 + C.Pieces.back().TauEnd - C.PhaseStart, rb::PursuitStopTime(Exact.X0, Exact.G, Exact.K), 1e-12);
			}
			for (std::size_t i = 0; i < C.Pieces.size(); ++i)
			{
				const MotionSegment& S = C.Pieces[i];
				const double Offset = S.T0 - C.PhaseStart;
				// Inside a piece: position from the piece, velocity and spin exact (EvaluatePursuit), to 1e-12.
				for (int j = 0; j <= 10; ++j)
				{
					const double Tau = S.TauEnd * j / 10.0;
					const BallState E = rb::EvaluateSegmentForEvent(S, Tau);
					RB_CHECK(SameBits(E.Position, rb::PositionAt(S, Tau)));
					const Vec2 V = Exact.Velocity(Offset + Tau);
					RB_CHECK_NEAR(E.Velocity.x, V.x, 1e-12);
					RB_CHECK_NEAR(E.Velocity.y, V.y, 1e-12);
					RB_CHECK(E.Velocity.z == 0.0);
					const Vec3 U = rb::SlipVelocity(E.Velocity, E.Omega, kR);
					const Vec2 X = Phase == MotionState::Sliding ? Exact.Pursuit(Offset + Tau) : Vec2{};
					RB_CHECK_NEAR(U.x, X.x, 1e-12);
					RB_CHECK_NEAR(U.y, X.y, 1e-12);
					RB_CHECK(E.Omega.z == rb::OmegaZAt(S, Tau));
					// The piece's own evaluation (what detection used) stays close: its velocity error is bounded.
					RB_CHECK(rb::Length(XY(rb::EvaluateSegment(S, Tau).Velocity) - V) <= 5e-2);
				}
				const BallState End = rb::SegmentEndState(S, kNumerics);
				RB_CHECK(SameBits(End.Position, rb::PositionAt(S, S.TauEnd)));
				if (S.Tilt.EndsInRefresh)
				{
					// Exact node: state unchanged, velocity from the pursuit solution, no snap.
					RB_CHECK(End.State == Phase);
					const Vec2 V = Exact.Velocity(Offset + S.TauEnd);
					RB_CHECK_NEAR(End.Velocity.x, V.x, 1e-12);
					RB_CHECK_NEAR(End.Velocity.y, V.y, 1e-12);
					const BallState Event = rb::EvaluateSegmentForEvent(S, S.TauEnd);
					RB_CHECK(SameBits(Event.Velocity, End.Velocity) && SameBits(Event.Omega, End.Omega));
				}
				else if (Phase == MotionState::Sliding)
				{
					// End of the tilted slide: u := 0 exactly, v := L_c(T_stop).
					RB_CHECK(i + 1 == C.Pieces.size());
					RB_CHECK(End.State == MotionState::Rolling);
					const Vec3 U = rb::SlipVelocity(End.Velocity, End.Omega, kR);
					RB_CHECK_NEAR(U.x, 0.0, 1e-15);
					RB_CHECK_NEAR(U.y, 0.0, 1e-15);
					const Vec2 V = Exact.Velocity(Offset + S.TauEnd);
					RB_CHECK_NEAR(End.Velocity.x, V.x, 1e-12);
					RB_CHECK_NEAR(End.Velocity.y, V.y, 1e-12);
				}
				else
				{
					// End of the tilted roll: v := 0, w_h := 0.
					RB_CHECK(i + 1 == C.Pieces.size());
					RB_CHECK(End.State == MotionState::Stationary || End.State == MotionState::Spinning);
					RB_CHECK(End.Velocity == Vec3::Zero() && End.Omega.x == 0.0 && End.Omega.y == 0.0);
				}
			}
		}
		RB_CHECK(Run.Final.State == MotionState::Stationary);
	}
}

// ---------------------------------------------------------------------------------------------
// Pursuit and refresh-rule robustness (no spec IDs)
// ---------------------------------------------------------------------------------------------

RB_TEST(MOT_Tilt_PursuitMatchesRk4AcrossGeometries)
{
	const double K = 0.0980665;
	const double GNorm = 0.02;
	const Vec2 G{0.0, -GNorm};
	// Angles between x0 and G from parallel (downhill) to anti-parallel (uphill), incl. nearly collinear ones.
	for (double BetaDeg : {0.0, 1e-7, 0.5, 30.0, 90.0, 150.0, 179.5, 180.0 - 1e-6, 180.0})
	{
		const double Beta = BetaDeg * rb::kDegToRad;
		const Vec2 X0 = Vec2{std::sin(Beta), -std::cos(Beta)} * 0.8; // G_hat = -y
		const double TStop = rb::PursuitStopTime(X0, G, K);
		RB_CHECK(TStop > 0.8 / (K + GNorm) * (1.0 - 1e-12) && TStop < 0.8 / (K - GNorm) * (1.0 + 1e-12));
		double LastLambda = 0.0;
		for (double Fraction : {1e-9, 0.1, 0.5, 0.9, 0.99, 0.999999})
		{
			const double T = Fraction * TStop;
			const PursuitState P = rb::EvaluatePursuit(X0, G, K, T);
			if (Fraction < 0.999)
			{
				// RK4 is not a valid oracle in the last steps before the stop (x_hat is singular at x = 0).
				const Rk4Result Rk = PursuitRk4(X0, G, K, T, 20000);
				RB_CHECK_NEAR(P.X.x, Rk.X.x, 1e-9);
				RB_CHECK_NEAR(P.X.y, Rk.X.y, 1e-9);
				RB_CHECK_NEAR(P.Integral.x, Rk.Integral.x, 1e-9);
				RB_CHECK_NEAR(P.Integral.y, Rk.Integral.y, 1e-9);
			}
			RB_CHECK(rb::Length(P.X) <= 0.8 * (1.0 - Fraction) + 1e-12); // |x| decreases at least at rate K - |G|
			RB_CHECK(P.Lambda >= LastLambda);
			LastLambda = P.Lambda;
		}
		// Past the stop: x = 0 and the integral is continuous at T_stop.
		const PursuitState End = rb::EvaluatePursuit(X0, G, K, TStop);
		const PursuitState Late = rb::EvaluatePursuit(X0, G, K, 2.0 * TStop);
		const PursuitState Near = rb::EvaluatePursuit(X0, G, K, TStop * (1.0 - 1e-12));
		RB_CHECK(End.X == Vec2{} && Late.X == Vec2{});
		RB_CHECK(SameBits(End.Integral, Late.Integral));
		RB_CHECK(rb::Length(Near.Integral - End.Integral) <= 1e-12);
	}
	// Level G = 0 reduces to the level law.
	const PursuitState Level = rb::EvaluatePursuit({0.6, 0.8}, Vec2{}, K, 3.0);
	RB_CHECK_NEAR(Level.X.x, 0.6 * (1.0 - K * 3.0), 1e-15);
	RB_CHECK_NEAR(Level.X.y, 0.8 * (1.0 - K * 3.0), 1e-15);
	RB_CHECK_NEAR(rb::PursuitStopTime({0.6, 0.8}, Vec2{}, K), 1.0 / K, 1e-15);
	RB_CHECK(rb::PursuitStopTime(Vec2{}, G, K) == 0.0);
}

RB_TEST(MOT_Tilt_RefreshRuleAndTerminationBound)
{
	// Tail and collinear pieces run to the stop; the cubic root makes the Hermite bound equal eps.
	const double K = 0.0980665;
	const double GNorm = 0.007;
	const double Eps = 5e-5;
	const double XTail = std::sqrt(Eps * (K - GNorm) / 4.0);
	RB_CHECK(rb::TiltPieceDuration(XTail, K, GNorm, 1.0, 1.0, Eps, 2.0, 0.123) == 0.123);
	RB_CHECK(rb::TiltPieceDuration(0.5, K, GNorm, 1.0, 1e-13, Eps, 2.0, 7.5) == 7.5);
	const double D = rb::TiltPieceDuration(0.5, K, GNorm, 1.0, 0.7, Eps, 100.0, 100.0);
	RB_CHECK_NEAR((2.0 / 81.0) * K * GNorm * 0.7 * D * D * D + Eps * (K + GNorm) * D - Eps * 0.5, 0.0, 1e-18);
	RB_CHECK(rb::TiltPieceDuration(0.5, K, GNorm, 1.0, 0.7, Eps, 0.25, 100.0) == 0.25);
	RB_CHECK(rb::TiltPieceDuration(0.5, K, GNorm, 1.0, 0.7, 0.0, 2.0, 3.0) == 3.0); // invalid settings terminate

	// Section 9 row 14: a phase has at most T_stop / Delta_min + 1 pieces.
	rb::Rng Rng(0x914u);
	for (int Case = 0; Case < 200; ++Case)
	{
		const double Angle = Rng.NextUniform(0.0, rb::kTwoPi);
		const double SlopeMag = Rng.NextUniform(1e-4, 0.7 * 0.010 * 0.99);
		const TiltParams Tilt = Slope(SlopeMag * std::cos(Angle), SlopeMag * std::sin(Angle), Case % 2 == 0 ? 5e-5 : 5e-4);
		const Vec2 V{Rng.NextUniform(-2.0, 2.0), Rng.NextUniform(-2.0, 2.0)};
		const BallRun Run = RunBall(Rolling(V), MotSpec(), MotCloth(), MotSlate(), kG, Tilt);
		const ChainStats C = ChainOf(Run, MotionState::Rolling);
		RB_REQUIRE(!C.Pieces.empty());
		RB_CHECK(Run.Final.State == MotionState::Stationary);
		if (!C.Pieces.front().Tilt.Active)
		{
			continue;
		}
		RB_CHECK(C.MaxDeviation <= Tilt.Tolerance);
		const RollProblem P = RollingProblem(Tilt);
		const double G = rb::Length(P.G);
		const double Tail = std::sqrt(Tilt.Tolerance * (P.K - G) / 4.0);
		const double DeltaMin = rb::Min(Tilt.RefreshMaxInterval,
			rb::Min(Tail / (2.0 * (P.K + G)), std::cbrt(Tilt.Tolerance * Tail / (2.0 * (2.0 / 81.0) * P.K * G))));
		const double TStop = rb::PursuitStopTime(V, P.G, P.K);
		RB_CHECK(static_cast<double>(C.Pieces.size()) <= TStop / DeltaMin + 1.0);
		RB_CHECK_NEAR(C.Nodes.back() - C.PhaseStart, TStop, 1e-12 * TStop);
	}
}

RB_TEST(MOT_Tilt_NapDriftAndResistance)
{
	TiltParams Nap;
	Nap.NapPseudoSlope = {2e-4, 0.0};
	Nap.NapResistance = 0.1;
	// Rolling across the nap (+y): drifts toward +NapPseudoSlope, pursuit rate unchanged (v_hat . n_nap = 0).
	const MotionSegment Across = rb::MakeSegment(Rolling({0.0, 0.5}), 0.0, MotSpec(), MotCloth(), 0.0, kG, Nap);
	RB_CHECK(Across.Tilt.Active);
	RB_CHECK_NEAR(Across.Tilt.G.x, 2e-4 * kG, 1e-18);
	RB_CHECK(Across.Tilt.G.y == 0.0);
	RB_CHECK_NEAR(Across.Tilt.K, 0.010 * kG, 1e-18);
	RB_CHECK(Across.Accel2.x > 0.0);
	// Rolling with the nap (+x): collinear, one exact piece, K = mu_r g (1 - eta_n).
	const BallRun With = RunBall(Rolling({0.5, 0.0}), MotSpec(), MotCloth(), MotSlate(), kG, Nap);
	const ChainStats C = ChainOf(With, MotionState::Rolling);
	RB_REQUIRE(C.Pieces.size() == 1u);
	const double Decel = 0.010 * kG * 0.9 - 2e-4 * kG;
	RB_CHECK_NEAR(C.Pieces[0].Tilt.K, 0.010 * kG * 0.9, 1e-17);
	RB_CHECK_NEAR(With.StopTime, 0.5 / Decel, 1e-12);
	RB_CHECK_NEAR(With.Final.Position.x, 0.25 / (2.0 * Decel), 1e-12);
}
