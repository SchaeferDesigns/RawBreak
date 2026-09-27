// Owner: WP-4. Adversarial checks the spec does not list (WP-4 review): conservation laws of the cushion models,
// symmetry, extreme inputs, determinism, and the regression tests of the review fixes (resting rule of the on-cloth
// options, bounded plain bisection).
//
// Conservation laws used (DERIVED):
//  * GRI / Han / Stronge: the only impulse acts at the contact point I, so the angular momentum about I,
//    L_I = m (R k_hat) x v + I w, is conserved exactly (all three components).
//  * Mathavan: impulses act at I and at the cloth contact C, so only the component of the angular momentum about C along
//    the line CI is conserved: cos(theta) (m R v_X + I w_Y) + (1 + sin(theta)) I w_Z (local frame; its rate is identically
//    0 in the equations of 4.5, whatever the friction directions, so every integrator step preserves it).

#include "Cushion/CushionTestUtil.h"

#include "rb/Core/Tolerances.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/PocketDrop.h"

#include <chrono>
#include <cstdio>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define RB_CUSHION_TEST_ALLOC_HOOK 1
#else
#define RB_CUSHION_TEST_ALLOC_HOOK 0
#endif

using namespace rbcushiontest;

namespace
{
#if RB_CUSHION_TEST_ALLOC_HOOK
	// Debug CRT allocation hook (MSVC): counts heap allocations while installed.
	int g_CushionTestAllocations = 0;
	int CountAllocations(int AllocType, void*, size_t, int, long, const unsigned char*, int)
	{
		if (AllocType == _HOOK_ALLOC || AllocType == _HOOK_REALLOC)
		{
			++g_CushionTestAllocations;
		}
		return 1;
	}
#endif

	rb::Vec3 RandomUnitishVector(rb::Rng& Rng)
	{
		return {Rng.NextDouble01() * 2.0 - 1.0, Rng.NextDouble01() * 2.0 - 1.0, Rng.NextDouble01() * 2.0 - 1.0};
	}

	// Angular momentum about the contact point I (k_hat from I to the center): m (R k_hat) x v + I w.
	rb::Vec3 AngularMomentumAboutContact(const rb::BallSpec& Ball, const rb::Vec3& Normal, const rb::Vec3& V, const rb::Vec3& W)
	{
		return rb::Cross(Normal * Ball.Radius, V) * Ball.Mass + W * Ball.Inertia;
	}

	// Mathavan's conserved component (header comment), local frame.
	double MathavanInvariant(const rb::BallSpec& Ball, double Theta, const rb::Vec3& V, const rb::Vec3& W)
	{
		return rb::Cos(Theta) * (Ball.Mass * Ball.Radius * V.x + Ball.Inertia * W.y) + (1.0 + rb::Sin(Theta)) * Ball.Inertia * W.z;
	}

	bool IsFiniteVec(const rb::Vec3& V) { return rb::IsFinite(V.x) && rb::IsFinite(V.y) && rb::IsFinite(V.z); }

	rb::FixedContact OnClothNose(const rb::Vec3& IntoFeature, double Elevation)
	{
		rb::FixedContact C;
		C.Kind = rb::FixedContactKind::NoseEdge;
		C.BallOnCloth = true;
		C.IntoFeature = IntoFeature;
		C.Elevation = Elevation;
		const rb::CushionFrame F = rb::MakeCushionFrame(IntoFeature);
		C.Normal = F.Y * (-rb::Cos(Elevation)) + F.Z * (-rb::Sin(Elevation));
		return C;
	}

	rb::BallState OnCloth(const rb::Vec3& V, const rb::Vec3& W)
	{
		rb::BallState S;
		S.Position = {0.0, 0.0, kR};
		S.Velocity = V;
		S.Omega = W;
		S.State = rb::MotionState::Sliding;
		return S;
	}

	rb::Vec3 RotateZ(const rb::Vec3& V, double C, double S) { return {C * V.x - S * V.y, S * V.x + C * V.y, V.z}; }

	// Wide Mathavan input set: typical, stress, grazing (v_Y just above v_rest), huge speed / spin, spin-dominated, and
	// non-default contact parameters (facing angle, mu_w above tan(theta), high cloth friction, any e).
	struct MathavanInput
	{
		rb::Vec3 V;
		rb::Vec3 W;
		rb::MathavanSettings S;
	};

	MathavanInput WideMathavanInput(rb::Rng& Rng, int Index)
	{
		MathavanInput In;
		In.S = PoolMathavan(0.97, rb::CushionParams{}.MathavanSteps, true);
		switch (Index % 6)
		{
		case 0:
			TypicalImpact(Rng, In.V, In.W);
			break;
		case 1:
			StressImpact(Rng, In.V, In.W);
			break;
		case 2:
			In.V = {(Rng.NextDouble01() * 2.0 - 1.0) * 8.0, 0.0021 * rb::Pow(100.0, Rng.NextDouble01()), 0.0};
			In.W = RandomUnitishVector(Rng) * (300.0 * Rng.NextDouble01());
			break;
		case 3:
			In.V = AtIncidence(5.0 + 45.0 * Rng.NextDouble01(), 1.0 + 88.0 * Rng.NextDouble01());
			In.W = RandomUnitishVector(Rng) * 3000.0;
			break;
		case 4:
			In.V = AtIncidence(0.01 + 0.2 * Rng.NextDouble01(), 1.0 + 88.0 * Rng.NextDouble01());
			In.W = RandomUnitishVector(Rng) * 200.0;
			break;
		default:
			TypicalImpact(Rng, In.V, In.W);
			In.S.CushionFriction = 0.1 + 0.2 * Rng.NextDouble01();
			In.S.ClothFriction = 0.1 + 0.3 * Rng.NextDouble01();
			In.S.Elevation = Rng.NextDouble01() < 0.5 ? 12.0 * rb::kPi / 180.0 : PoolElevation();
			break;
		}
		In.S.Restitution = (Index % 7 == 3) ? 0.0 : (Index % 7 == 5 ? 1.0 : 0.3 + 0.7 * Rng.NextDouble01());
		return In;
	}
}

// ---- Review fix: the resting rule on the cloth for the option models --------------------------------------------------

RB_TEST(COL_Dispatcher_RestingRuleStopsTheApproachForEveryModel)
{
	// Approach below v_rest (7.1, 7.3): e = 0 and v_Y := 0 for EVERY on-cloth model. Before the fix Han and Stronge
	// (Stronge falls back to Han at e = 0) returned v_Y' = v_Y sin^2(theta) > 0: the ball kept approaching the nose and
	// the contact re-triggered at tau = 0 (a Zeno chain that only the event guards ended).
	const rb::BallSpec Ball = PoolBall();
	const rb::FixedContact C = OnClothNose({0.0, 1.0, 0.0}, PoolElevation());
	const rb::Vec3 Vs[] = {{0.4, 0.0015, 0.0}, {0.0, 0.0019, 0.0}, {-1.0, 0.001, 0.0}, {0.2, 1e-9, 0.0}};
	const rb::CushionModel Models[] = {rb::CushionModel::Mathavan2010, rb::CushionModel::Han2005, rb::CushionModel::Mirror, rb::CushionModel::StrongeCompliant};
	for (const rb::Vec3& V : Vs)
	{
		const rb::Vec3 W = RollingOmega(V, kR) + rb::Vec3{0.0, 0.0, 7.0};
		for (rb::CushionModel Model : Models)
		{
			rb::CushionParams P;
			P.OnClothModel = Model;
			const rb::CushionImpactResult R = rb::ResolveFixedContact(C, OnCloth(V, W), Ball, P, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{});
			RB_CHECK(R.Resting);
			RB_CHECK(R.Restitution == 0.0);
			RB_CHECK(R.Velocity.y == 0.0); // local = world here: no approach left
			RB_CHECK(R.Velocity.x == V.x && R.Velocity.z == 0.0);
			RB_CHECK(R.Omega == W); // nothing else changes
			RB_CHECK_NEAR(R.NormalImpulse, kM * V.y / rb::Cos(PoolElevation()), 1e-18);
		}
	}
	// A separating ball: no impulse for any model.
	for (rb::CushionModel Model : Models)
	{
		rb::CushionParams P;
		P.OnClothModel = Model;
		const rb::CushionImpactResult R =
			rb::ResolveFixedContact(C, OnCloth({0.3, -0.2, 0.0}, {1.0, 2.0, 3.0}), Ball, P, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{});
		RB_CHECK(R.Resting && R.NormalImpulse == 0.0);
		RB_CHECK(R.Velocity == (rb::Vec3{0.3, -0.2, 0.0}));
	}
}

RB_TEST(COL_Dispatcher_OnClothModelsNeverLeaveTheBallApproaching)
{
	// Every on-cloth model at every approach speed (incl. the resting band): the ball leaves the nose (local v_Y' <= 0).
	const rb::BallSpec Ball = PoolBall();
	const rb::CushionModel Models[] = {rb::CushionModel::Mathavan2010, rb::CushionModel::Han2005, rb::CushionModel::Mirror, rb::CushionModel::StrongeCompliant};
	rb::Rng Rng(0xA11CEull);
	const rb::FixedContact C = OnClothNose({0.0, 1.0, 0.0}, PoolElevation());
	for (int i = 0; i < SweepCount(4000, 600); ++i)
	{
		rb::Vec3 V;
		rb::Vec3 W;
		TypicalImpact(Rng, V, W);
		if ((i & 3) == 0)
		{
			V.y = 1e-5 + 3e-3 * Rng.NextDouble01(); // around v_rest = 2 mm/s
		}
		for (rb::CushionModel Model : Models)
		{
			rb::CushionParams P;
			P.OnClothModel = Model;
			const rb::CushionImpactResult R = rb::ResolveFixedContact(C, OnCloth(V, W), Ball, P, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{});
			RB_CHECK(R.Velocity.y <= 0.0);
			RB_CHECK(R.Velocity.z == 0.0);
			RB_CHECK(KineticEnergy(Ball, R.Velocity, R.Omega) <= KineticEnergy(Ball, V, W) + 1e-12);
		}
	}
}

// ---- Review fix: the plain integrator's bisection is bounded by the bracket, not only by MaxBisections --------------------

RB_TEST(COL_Mathavan_PlainBisectionStopsWhenTheBracketCollapses)
{
	// cushion.mathavan_max_bisections is a free integer in the parameter table. Beyond ~53 halvings the bracket of the
	// plain integrator cannot shrink, and every further halving is a no-op; before the fix the loop still ran all of
	// them (2 x 2^26 RK4 steps here, seconds per impact). The result is bitwise the one of the spec's 60.
	const rb::BallSpec Ball = PoolBall();
	const GateCase G = GateCaseAt(1);
	rb::MathavanSettings Huge = PoolMathavan(0.97, 200, false);
	Huge.MaxBisections = 1 << 26;
	const rb::MathavanSettings Spec60 = PoolMathavan(0.97, 200, false);
	const auto T0 = std::chrono::steady_clock::now();
	const rb::CushionImpactResult A = rb::ResolveMathavan(G.V, G.W, Ball, Huge);
	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
	const rb::CushionImpactResult B = rb::ResolveMathavan(G.V, G.W, Ball, Spec60);
	RB_CHECK(A.Velocity == B.Velocity && A.Omega == B.Omega && A.NormalImpulse == B.NormalImpulse);
	RB_CHECK(Seconds < 0.25);
}

// ---- Conservation laws -------------------------------------------------------------------------------------------------

RB_TEST(COL_Mathavan_ConservesAngularMomentumAlongTheContactLine)
{
	// Wide input set (typical, stress, grazing, huge speed and spin, spin-dominated, non-default contact parameters, e in
	// [0.3, 1] plus e = 0 and e = 1): the CI component of the angular momentum is conserved to rounding; the result is
	// finite, on the slate (v_Z = 0), dissipative, and leaves the cushion (v_Y' < 0 for e > 0, v_Y' = 0 for e = 0).
	const rb::BallSpec Ball = PoolBall();
	rb::Rng Rng(0xC0FFEEull);
	double WorstInvariant = 0.0;
	double WorstGain = -1.0;
	for (int i = 0; i < SweepCount(30000, 3000); ++i)
	{
		const MathavanInput In = WideMathavanInput(Rng, i);
		if (!(In.V.y >= In.S.RestSpeed))
		{
			continue;
		}
		for (int Split = 0; Split < ((i % 16 == 0) ? 2 : 1); ++Split)
		{
			rb::MathavanSettings S = In.S;
			if (Split != 0)
			{
				S.SplitAtSlipReversal = false;
				S.Steps = 200;
			}
			const rb::CushionImpactResult R = rb::ResolveMathavan(In.V, In.W, Ball, S);
			RB_REQUIRE(IsFiniteVec(R.Velocity) && IsFiniteVec(R.Omega));
			RB_CHECK(R.Velocity.z == 0.0);
			const double Scale = Ball.Mass * kR * (rb::Length(In.V) + kR * rb::Length(In.W));
			WorstInvariant = rb::Max(WorstInvariant, rb::Abs(MathavanInvariant(Ball, S.Elevation, R.Velocity, R.Omega) - MathavanInvariant(Ball, S.Elevation, In.V, In.W)) / Scale);
			const double Gain = KineticEnergy(Ball, R.Velocity, R.Omega) - KineticEnergy(Ball, In.V, In.W);
			WorstGain = rb::Max(WorstGain, Gain);
			RB_CHECK(Gain <= 1e-12);
			if (S.Restitution > 0.0)
			{
				RB_CHECK(R.Velocity.y < 0.0);
			}
			else
			{
				RB_CHECK(rb::Abs(R.Velocity.y) <= 1e-12 * In.V.y);
			}
			RB_CHECK(R.NormalImpulse > 0.0);
		}
	}
	std::printf("  Mathavan wide set: worst CI angular momentum change %.2e (relative), worst energy change %+.2e J\n", WorstInvariant, WorstGain);
	RB_CHECK(WorstInvariant <= 1e-13);
}

RB_TEST(COL_GRI_ConservesAngularMomentumAboutTheContactPoint)
{
	// Random free-body impacts (arbitrary normals, general inertia, e in [0, 1], mu in [0, 0.5], speeds to 10 m/s, spins to
	// 300 rad/s): L_I conserved, energy non-increasing, v' . k_hat = e v_c exactly (friction is tangential), the contact
	// slip keeps its direction and does not grow (stick: 0), and Han (GRI with the nose normal) behaves the same.
	rb::Rng Rng(0x6E1ull);
	double WorstL = 0.0;
	for (int i = 0; i < SweepCount(100000, 10000); ++i)
	{
		const double R = 0.025 + 0.01 * Rng.NextDouble01();
		const double M = 0.1 + 0.2 * Rng.NextDouble01();
		const double K = 0.3 + 0.4 * Rng.NextDouble01();
		const rb::BallSpec Ball{R, M, K * M * R * R};
		rb::Vec3 N = RandomUnitishVector(Rng);
		if (rb::Length(N) < 0.1)
		{
			continue;
		}
		N = rb::Normalized(N);
		const bool Han = (i % 5) == 0;
		const double Theta = 0.3 * Rng.NextDouble01();
		if (Han)
		{
			N = {0.0, -rb::Cos(Theta), -rb::Sin(Theta)};
		}
		const rb::Vec3 V = RandomUnitishVector(Rng) * (10.0 * Rng.NextDouble01());
		const rb::Vec3 W = RandomUnitishVector(Rng) * (300.0 * Rng.NextDouble01());
		const double E = Rng.NextDouble01();
		const double Mu = 0.5 * Rng.NextDouble01();
		const rb::CushionImpactResult G = Han ? rb::ResolveHan(V, W, Ball, Theta, E, Mu) : rb::ResolveGri(V, W, Ball, N, E, Mu);
		const double Scale = M * R * (rb::Length(V) + R * rb::Length(W)) + 1e-300;
		WorstL = rb::Max(WorstL, MaxAbsDiff(AngularMomentumAboutContact(Ball, N, V, W), AngularMomentumAboutContact(Ball, N, G.Velocity, G.Omega)) / Scale);
		RB_CHECK(KineticEnergy(Ball, G.Velocity, G.Omega) <= KineticEnergy(Ball, V, W) * (1.0 + 1e-14) + 1e-300);
		if (G.Resting)
		{
			RB_CHECK(G.Velocity == V && G.Omega == W);
			continue;
		}
		RB_CHECK_NEAR(rb::Dot(G.Velocity, N), E * G.NormalSpeed, 1e-14 * (rb::Length(V) + 1.0));
		const rb::Vec3 Arm = N * (-R);
		const rb::Vec3 SlipIn = (V + rb::Cross(W, Arm)) - N * rb::Dot(V + rb::Cross(W, Arm), N);
		const rb::Vec3 SlipOut = (G.Velocity + rb::Cross(G.Omega, Arm)) - N * rb::Dot(G.Velocity + rb::Cross(G.Omega, Arm), N);
		const double Tolerance = 1e-12 * (rb::Length(V) + R * rb::Length(W) + 1.0);
		if (G.Stick)
		{
			RB_CHECK(rb::Length(SlipOut) <= Tolerance);
		}
		else
		{
			RB_CHECK(rb::Length(SlipOut) <= rb::Length(SlipIn) + Tolerance);
			RB_CHECK(rb::Dot(SlipOut, SlipIn) >= -Tolerance * rb::Length(SlipIn));
		}
	}
	std::printf("  GRI: worst L_I change %.2e (relative)\n", WorstL);
	RB_CHECK(WorstL <= 1e-14);
}

RB_TEST(COL_Stronge_ConservesAngularMomentumAboutTheContactPoint)
{
	// The compliant model's impulse also acts at I only: L_I conserved; energy non-increasing; the contact separates at
	// e times the approach speed (normal impulse (1 + e) m U); omega_ratio outside (1, 2) is clamped, never NaN.
	const rb::BallSpec Ball = PoolBall();
	const double Theta = PoolElevation();
	const rb::Vec3 N{0.0, -rb::Cos(Theta), -rb::Sin(Theta)};
	rb::Rng Rng(0x57A0ull);
	double WorstL = 0.0;
	for (int i = 0; i < SweepCount(40000, 4000); ++i)
	{
		rb::Vec3 V;
		rb::Vec3 W;
		if ((i & 1) != 0)
		{
			TypicalImpact(Rng, V, W);
		}
		else
		{
			StressImpact(Rng, V, W);
		}
		const double E = (i % 11 == 0) ? 1.0 : 1e-3 + (1.0 - 1e-3) * Rng.NextDouble01();
		const double Mu = (i % 13 == 0) ? 0.0 : 0.4 * Rng.NextDouble01();
		const double Ratio = (i % 17 == 0) ? 0.5 + 2.0 * Rng.NextDouble01() : 1.05 + 0.9 * Rng.NextDouble01();
		const rb::CushionImpactResult S = rb::ResolveStronge(V, W, Ball, Theta, E, Mu, Ratio);
		RB_REQUIRE(IsFiniteVec(S.Velocity) && IsFiniteVec(S.Omega));
		const double Scale = kM * kR * (rb::Length(V) + kR * rb::Length(W));
		WorstL = rb::Max(WorstL, MaxAbsDiff(AngularMomentumAboutContact(Ball, N, V, W), AngularMomentumAboutContact(Ball, N, S.Velocity, S.Omega)) / Scale);
		RB_CHECK(KineticEnergy(Ball, S.Velocity, S.Omega) <= KineticEnergy(Ball, V, W) + 1e-12); // failed before the regime fix
		if (!S.Resting)
		{
			RB_CHECK_NEAR(rb::Dot(S.Velocity, N), E * S.NormalSpeed, 1e-13 * (rb::Length(V) + 1.0));
			RB_CHECK_NEAR(S.NormalImpulse, (1.0 + E) * kM * S.NormalSpeed, 1e-15);
		}
	}
	std::printf("  Stronge: worst L_I change %.2e (relative)\n", WorstL);
	RB_CHECK(WorstL <= 1e-14);
}

RB_TEST(COL_Stronge_RegimesBelowTheCriticalRestitution)
{
	// Review fix: for e < 1 / OmegaRatio pooltool 0.6.0 takes these impacts for gross slip (s_f / U = r - 3.5 mu (1 + e):
	// -0.75, -0.55, -0.39, -0.90) and gains energy. Oracle: a direct time integration of the spring-slider contact
	// (linear normal spring loading k / unloading k / e^2, tangential spring OmegaRatio^2 k / 3.5 in series with a
	// Coulomb slider; converged to 1e-7 with dt 2e-6 and 5e-7 of 1 / omega_n): the final contact slip along the initial
	// slip direction, per unit approach speed. Stun ball, W = 0, so r = |s_0| / U = sqrt(v_X^2 + sin^2) / cos for v_Y = 1.
	struct RegimeCase
	{
		double RByMu;
		double Mu;
		double E;
		double Ratio;
		double ExpectedSlip; // s_f / U (direct integration)
		double PooltoolSlip; // s_f / U of pooltool's gross-slip branch
	};
	const RegimeCase Cases[] = {
		{1.5, 0.2, 0.5, 1.3, -0.2985520, -0.75},  // pooltool: gross slip; really initial stick (r < mu eta^2)
		{2.5, 0.2, 0.5, 1.3, -0.4617474, -0.55},  // really slip - stick (compression) - slip
		{3.3, 0.2, 0.5, 1.8, -0.3825290, -0.39},  // omega_ratio 1.8 with e = 0.5 < 1 / 1.8
		{1.9, 0.3, 0.4, 1.5, -0.5523460, -0.90},
	};
	const rb::BallSpec Ball = PoolBall();
	const double Theta = PoolElevation();
	const double SinT = rb::Sin(Theta);
	const double CosT = rb::Cos(Theta);
	const rb::Vec3 N{0.0, -CosT, -SinT};
	for (const RegimeCase& C : Cases)
	{
		const double R = C.RByMu * C.Mu;
		const rb::Vec3 V{rb::Sqrt(R * CosT * R * CosT - SinT * SinT), 1.0, 0.0};
		const double U = CosT;
		const rb::Vec3 SlipDirection = rb::Normalized(V + N * U);
		const rb::CushionImpactResult S = rb::ResolveStronge(V, {}, Ball, Theta, C.E, C.Mu, C.Ratio);
		const rb::Vec3 ContactVelocity = S.Velocity + rb::Cross(S.Omega, N * (-kR));
		const double SlipRatio = rb::Dot(ContactVelocity, SlipDirection) / U;
		RB_CHECK_NEAR(SlipRatio, C.ExpectedSlip, 1e-6);
		RB_CHECK(rb::Abs(SlipRatio - C.PooltoolSlip) > 5e-3);
		RB_CHECK_NEAR(rb::Dot(ContactVelocity, N), C.E * U, 1e-14);
		RB_CHECK(KineticEnergy(Ball, S.Velocity, S.Omega) <= KineticEnergy(Ball, V, {}));
	}
}

// ---- Symmetry ----------------------------------------------------------------------------------------------------------

RB_TEST(COL_CushionModels_MirrorSymmetry)
{
	// Mirror X -> -X in the cushion frame: (v_X, w_Y, w_Z) flip, (v_Y, v_Z, w_X) do not. Han, Stronge and GRI (with a
	// mirrored normal) mirror to rounding; Mathavan to its integration tolerance (the step control sees the mirrored
	// slips identically, so it is exact to rounding as well).
	const rb::BallSpec Ball = PoolBall();
	const double Theta = PoolElevation();
	rb::Rng Rng(0x3141ull);
	auto Mirror = [](const rb::Vec3& V) { return rb::Vec3{-V.x, V.y, V.z}; };
	auto MirrorW = [](const rb::Vec3& W) { return rb::Vec3{W.x, -W.y, -W.z}; };
	for (int i = 0; i < SweepCount(3000, 400); ++i)
	{
		rb::Vec3 V;
		rb::Vec3 W;
		StressImpact(Rng, V, W);
		const double Scale = rb::Length(V) + kR * rb::Length(W);
		const double E = 0.5 + 0.5 * Rng.NextDouble01();
		const rb::CushionImpactResult H = rb::ResolveHan(V, W, Ball, Theta, E, 0.2);
		const rb::CushionImpactResult Hm = rb::ResolveHan(Mirror(V), MirrorW(W), Ball, Theta, E, 0.2);
		RB_CHECK(ImpactError(Hm, {Mirror(H.Velocity), MirrorW(H.Omega)}, kR) <= 1e-14 * Scale);
		const rb::CushionImpactResult S = rb::ResolveStronge(V, W, Ball, Theta, E, 0.2, 1.8);
		const rb::CushionImpactResult Sm = rb::ResolveStronge(Mirror(V), MirrorW(W), Ball, Theta, E, 0.2, 1.8);
		RB_CHECK(ImpactError(Sm, {Mirror(S.Velocity), MirrorW(S.Omega)}, kR) <= 1e-13 * Scale);
		const rb::CushionImpactResult M = rb::ResolveMathavan(V, W, Ball, PoolMathavan(E, 8, true));
		const rb::CushionImpactResult Mm = rb::ResolveMathavan(Mirror(V), MirrorW(W), Ball, PoolMathavan(E, 8, true));
		RB_CHECK(ImpactError(Mm, {Mirror(M.Velocity), MirrorW(M.Omega)}, kR) <= 1e-11 * Scale);
		// GRI with a general normal mirrored across the X = 0 plane.
		rb::Vec3 N = rb::Normalized(rb::Vec3{Rng.NextDouble01() - 0.5, -1.0, Rng.NextDouble01() - 0.5});
		const rb::CushionImpactResult G = rb::ResolveGri(V, W, Ball, N, E, 0.3);
		const rb::CushionImpactResult Gm = rb::ResolveGri(Mirror(V), MirrorW(W), Ball, Mirror(N), E, 0.3);
		RB_CHECK(ImpactError(Gm, {Mirror(G.Velocity), MirrorW(G.Omega)}, kR) <= 1e-14 * Scale);
	}
}

RB_TEST(COL_Dispatcher_RotationInvariance)
{
	// Rotating the whole configuration about z (feature direction, contact normal, velocity, spin) rotates the result:
	// the dispatcher's frame handling does not depend on the rail's orientation. Every on-cloth model and GRI.
	const rb::BallSpec Ball = PoolBall();
	rb::Rng Rng(0x2077ull);
	const rb::CushionModel Models[] = {rb::CushionModel::Mathavan2010, rb::CushionModel::Han2005, rb::CushionModel::Mirror, rb::CushionModel::StrongeCompliant};
	for (int i = 0; i < SweepCount(800, 100); ++i)
	{
		rb::Vec3 V;
		rb::Vec3 W;
		TypicalImpact(Rng, V, W);
		const double Phi = 2.0 * rb::kPi * Rng.NextDouble01();
		const double C = rb::Cos(Phi);
		const double S = rb::Sin(Phi);
		const rb::FixedContact Base = OnClothNose({0.0, 1.0, 0.0}, PoolElevation());
		rb::FixedContact Rotated = OnClothNose(RotateZ({0.0, 1.0, 0.0}, C, S), PoolElevation());
		const double Scale = rb::Length(V) + kR * rb::Length(W);
		for (int m = 0; m < 5; ++m)
		{
			rb::CushionParams P;
			rb::FixedContact A = Base;
			rb::FixedContact B = Rotated;
			if (m < 4)
			{
				P.OnClothModel = Models[m];
			}
			else
			{
				A.BallOnCloth = false; // GRI with the nose normal
				B.BallOnCloth = false;
			}
			const rb::CushionImpactResult Ra = rb::ResolveFixedContact(A, OnCloth(V, W), Ball, P, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{});
			const rb::CushionImpactResult Rb =
				rb::ResolveFixedContact(B, OnCloth(RotateZ(V, C, S), RotateZ(W, C, S)), Ball, P, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{});
			RB_CHECK(MaxAbsDiff(Rb.Velocity, RotateZ(Ra.Velocity, C, S)) <= 1e-10 * Scale);
			RB_CHECK(MaxAbsDiff(Rb.Omega, RotateZ(Ra.Omega, C, S)) * kR <= 1e-10 * Scale);
			RB_CHECK(Ra.Resting == Rb.Resting && Ra.Stick == Rb.Stick);
		}
	}
}

// ---- Extreme inputs and determinism --------------------------------------------------------------------------------------

RB_TEST(COL_Mathavan_CreepingSlipNearZeroStaysAccurate)
{
	// Found by the review sweep: a cushion slip that passes CLOSE TO (not through) zero and then creeps just above s_eps
	// (stick marginally infeasible). The split integrator's direction-stability limit makes its steps tiny there; it
	// may exhaust its loop budget and hand the impact to the plain integrator (N = 200; ~1 in 1e5 rail hits at the
	// default mu_w, ~3 in 1e4 at mu_w 0.1-0.3, ~0.2 ms each). Pinned: the answer stays within the plain scheme's accuracy
	// of the converged solution, finite and dissipative.
	const rb::BallSpec Ball = PoolBall();
	struct Creep
	{
		rb::Vec3 V;
		rb::Vec3 W;
		double E;
		double MuW;
		double MuS;
	};
	const Creep Cases[] = {
		{{0.37928680980539597, 1.2680183500586546, 0.0}, {-44.375095365132267, 13.273379170792511, 13.630675952400109}, 0.96061935774794704, 0.14, 0.11169766594590085},
		{{0.069666148778188219, 0.34232728271270785, 0.0}, {-11.979957400269742, 2.4380104559295965, -4.3343828796874844}, 0.86789282560204173, 0.25982144867757073,
			0.3539557726493161},
		{{1.9199699168972506, 1.7994665684511255, 0.0}, {-62.973458213512707, 67.190548272869663, 106.45098083094835}, 0.73569292415385457, 0.22324514957876229,
			0.29264454339387663},
	};
	for (const Creep& C : Cases)
	{
		rb::MathavanSettings S = PoolMathavan(C.E, rb::CushionParams{}.MathavanSteps, true);
		S.CushionFriction = C.MuW;
		S.ClothFriction = C.MuS;
		const rb::CushionImpactResult R = rb::ResolveMathavan(C.V, C.W, Ball, S);
		rb::MathavanSettings Converged = S;
		Converged.Steps = 1024;
		const rb::CushionImpactResult Ref = rb::ResolveMathavan(C.V, C.W, Ball, Converged);
		RB_REQUIRE(IsFiniteVec(R.Velocity) && IsFiniteVec(R.Omega));
		RB_CHECK(ImpactError(R, Ref, kR) <= 3e-3 * (rb::Length(C.V) + kR * rb::Length(C.W)));
		RB_CHECK(KineticEnergy(Ball, R.Velocity, R.Omega) <= KineticEnergy(Ball, C.V, C.W));
		RB_CHECK(R.Velocity.y < 0.0);
	}
}

RB_TEST(COL_CushionModels_ExtremeInputsStayFinite)
{
	// Zero / tiny / huge speeds, grazing incidence, pure spin, denormal components: every model returns finite values and
	// never feeds energy in; a zero approach is never an impulse.
	const rb::BallSpec Ball = PoolBall();
	const double Theta = PoolElevation();
	const double Tiny = 1e-300;
	const rb::Vec3 Vs[] = {{0.0, 1e-12, 0.0}, {Tiny, Tiny, 0.0}, {50.0, 1e-3, 0.0}, {0.0, 100.0, 0.0}, {-80.0, 60.0, 0.0}, {1e-9, 3e-3, 0.0}, {3.0, 2.0000001e-3, 0.0}};
	const rb::Vec3 Ws[] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 5000.0}, {-3000.0, 2000.0, -1000.0}, {Tiny, -Tiny, Tiny}};
	for (const rb::Vec3& V : Vs)
	{
		for (const rb::Vec3& W : Ws)
		{
			const double E0 = KineticEnergy(Ball, V, W);
			const rb::CushionImpactResult Results[] = {
				rb::ResolveMathavan(V, W, Ball, PoolMathavan(0.97, 8, true)),
				rb::ResolveMathavan(V, W, Ball, PoolMathavan(0.6, 200, false)),
				rb::ResolveHan(V, W, Ball, Theta, 0.9, 0.2),
				rb::ResolveStronge(V, W, Ball, Theta, 0.9, 0.2, 1.8),
				rb::ResolveGri(V, W, Ball, {0.0, -1.0, 0.0}, 0.9, 0.2),
			};
			for (const rb::CushionImpactResult& R : Results)
			{
				RB_CHECK(IsFiniteVec(R.Velocity) && IsFiniteVec(R.Omega) && rb::IsFinite(R.NormalImpulse));
				RB_CHECK(KineticEnergy(Ball, R.Velocity, R.Omega) <= E0 * (1.0 + 1e-12) + 1e-300);
			}
		}
	}
	// A non-finite state is rejected by every model (no impulse, no NaN spreading into the impulse bookkeeping).
	const double NaN = rb::kInfinity - rb::kInfinity;
	RB_CHECK(rb::ResolveGri({NaN, 1.0, 0.0}, {}, Ball, {0.0, -1.0, 0.0}, 0.9, 0.2).NormalImpulse == 0.0);
	RB_CHECK(rb::ResolveStronge({0.0, NaN, 0.0}, {}, Ball, Theta, 0.9, 0.2, 1.8).NormalImpulse == 0.0);
	RB_CHECK(rb::ResolveMirror({0.0, NaN, 0.0}, {}, 0.9).Resting);
}

RB_TEST(COL_CushionModels_BitwiseDeterministic)
{
	// Pure functions: identical input -> identical bits, for every model, the dispatcher and the pivot.
	const rb::BallSpec Ball = PoolBall();
	rb::Rng Rng(0xDE7ull);
	for (int i = 0; i < 300; ++i)
	{
		rb::Vec3 V;
		rb::Vec3 W;
		StressImpact(Rng, V, W);
		const rb::CushionImpactResult H1 = rb::ResolveHan(V, W, Ball, PoolElevation(), 0.85, 0.2);
		const rb::CushionImpactResult H2 = rb::ResolveHan(V, W, Ball, PoolElevation(), 0.85, 0.2);
		RB_CHECK(H1.Velocity == H2.Velocity && H1.Omega == H2.Omega);
		const rb::CushionImpactResult S1 = rb::ResolveStronge(V, W, Ball, PoolElevation(), 0.85, 0.2, 1.8);
		const rb::CushionImpactResult S2 = rb::ResolveStronge(V, W, Ball, PoolElevation(), 0.85, 0.2, 1.8);
		RB_CHECK(S1.Velocity == S2.Velocity && S1.Omega == S2.Omega);
		const rb::FixedContact C = OnClothNose(rb::Normalized(rb::Vec3{0.3, 1.0, 0.0}), PoolElevation());
		const rb::CushionImpactResult D1 = rb::ResolveFixedContact(C, OnCloth(V, W), Ball, rb::CushionParams{}, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{});
		const rb::CushionImpactResult D2 = rb::ResolveFixedContact(C, OnCloth(V, W), Ball, rb::CushionParams{}, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{});
		RB_CHECK(D1.Velocity == D2.Velocity && D1.Omega == D2.Omega && D1.NormalImpulse == D2.NormalImpulse);
		const double V0 = 0.6 * Rng.NextDouble01();
		const rb::PivotResult P1 = rb::ComputePivot(V0, kR + 0.0047625, 0.4, kG, rb::NumericsConfig{});
		const rb::PivotResult P2 = rb::ComputePivot(V0, kR + 0.0047625, 0.4, kG, rb::NumericsConfig{});
		RB_CHECK(P1.Duration == P2.Duration && P1.LeaveAngle == P2.LeaveAngle && P1.LeaveSpeed == P2.LeaveSpeed);
	}
}

RB_TEST(COL_CushionModels_NoHeapAllocation)
{
	// Code rule: no heap allocation in the event loop. Every WP-4 entry point (both Mathavan integrators incl. the budget
	// fallback case, Han, GRI, Mirror, Stronge, the dispatcher, the pivot path) under the MSVC Debug CRT allocation hook;
	// other builds only run the calls.
	const rb::BallSpec Ball = PoolBall();
	const GateCase G = GateCaseAt(2);
	const rb::FixedContact C = OnClothNose(rb::Normalized(rb::Vec3{0.3, 1.0, 0.0}), PoolElevation());
	rb::MathavanSettings Creep = PoolMathavan(0.96061935774794704, 8, true);
	Creep.ClothFriction = 0.11169766594590085;
	const rb::Vec3 CreepV{0.37928680980539597, 1.2680183500586546, 0.0};
	const rb::Vec3 CreepW{-44.375095365132267, 13.273379170792511, 13.630675952400109};
	rb::PocketGeometry Pocket;
	Pocket.CaptureCenter = {1.0, 0.2};
	Pocket.CaptureRadius = 0.062;
	Pocket.DropRadius = 0.0047625;
	Pocket.DropEdgeRadius = Pocket.CaptureRadius + Pocket.DropRadius;
	Pocket.Axis = {1.0, 0.0};
	rb::BallState AtEdge;
	AtEdge.Position = {1.0 - Pocket.DropEdgeRadius, 0.2, kR};
	AtEdge.Velocity = {0.2, 0.05, 0.0};
	AtEdge.Omega = {-0.05 / kR, 0.2 / kR, 3.0};
	double Sink = 0.0;
#if RB_CUSHION_TEST_ALLOC_HOOK
	g_CushionTestAllocations = 0;
	const _CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(&CountAllocations);
#endif
	Sink += rb::ResolveMathavan(G.V, G.W, Ball, PoolMathavan(0.97, 8, true)).Velocity.y;
	Sink += rb::ResolveMathavan(G.V, G.W, Ball, PoolMathavan(0.97, 200, false)).Velocity.y;
	Sink += rb::ResolveMathavan(CreepV, CreepW, Ball, Creep).Velocity.y;
	Sink += rb::ResolveHan(G.V, G.W, Ball, PoolElevation(), 0.85, 0.2).Velocity.y;
	Sink += rb::ResolveGri(G.V, G.W, Ball, {0.0, -1.0, 0.0}, 0.85, 0.2).Velocity.y;
	Sink += rb::ResolveMirror(G.V, G.W, 0.9).Velocity.y;
	Sink += rb::ResolveStronge(G.V, G.W, Ball, PoolElevation(), 0.85, 0.2, 1.8).Velocity.y;
	Sink += rb::ResolveStronge(G.V, G.W, Ball, PoolElevation(), 0.4, 0.3, 1.2).Velocity.y;
	Sink += rb::ResolveFixedContact(C, OnCloth(G.V, G.W), Ball, rb::CushionParams{}, rb::PocketContactParams{}, rb::kClothDefault, rb::NumericsConfig{}).Velocity.y;
	const rb::PivotPath Path = rb::MakePivotPath(AtEdge, 0.0, Pocket, Ball, kG, rb::NumericsConfig{});
	Sink += rb::EvaluatePivot(Path, 0.4 * Path.Result.Duration).Position.z;
	Sink += rb::PivotLeaveState(Path).Velocity.z;
	Sink += rb::PivotDetectionProxy(Path).Accel2.z;
#if RB_CUSHION_TEST_ALLOC_HOOK
	_CrtSetAllocHook(Previous);
	RB_CHECK(g_CushionTestAllocations == 0);
#endif
	RB_CHECK(rb::IsFinite(Sink));
}

RB_TEST(COL_Pivot_ExtremeInputs)
{
	// Negative (outward) and zero normal speeds use the 1e-4 m/s floor; exactly sqrt(g rho) leaves at once; the pivot time
	// is finite and positive, decreasing with v0, and the leave angle never exceeds acos(2 / (3 + k)).
	const rb::NumericsConfig Numerics;
	const double Rho = kR + 0.0047625;
	const rb::PivotResult Floor = rb::ComputePivot(1e-4, Rho, 0.4, kG, Numerics);
	RB_CHECK(rb::ComputePivot(-0.3, Rho, 0.4, kG, Numerics).Duration == Floor.Duration);
	RB_CHECK(rb::ComputePivot(0.0, Rho, 0.4, kG, Numerics).LeaveAngle == Floor.LeaveAngle);
	RB_CHECK(rb::ComputePivot(rb::Sqrt(kG * Rho) * (1.0 + 1e-12), Rho, 0.4, kG, Numerics).Immediate);
	double Last = 1e30;
	for (int i = 0; i <= 200; ++i)
	{
		const double V0 = 0.5717 * i / 200.0;
		const rb::PivotResult P = rb::ComputePivot(V0, Rho, 0.4, kG, Numerics);
		RB_CHECK(!P.Immediate);
		RB_CHECK(rb::IsFinite(P.Duration) && P.Duration > 0.0);
		RB_CHECK(P.Duration <= Last);
		RB_CHECK(P.LeaveAngle <= rb::Acos(2.0 / 3.4) + 1e-15);
		Last = P.Duration;
	}
}
