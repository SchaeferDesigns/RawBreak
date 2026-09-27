// Owner: WP-4. The Mathavan step-count decision (architecture 15 row 22, O-4): accuracy gate A-CUSH-1, the
// micro-benchmark A-CUSH-2 (_Slow_, Release) and the random sweeps that back the choice of the step control.
//
// Decision (A-CUSH-1): CushionParams::MathavanSteps = 8, the smallest N from which M-2..M-4 stay within 2e-4 of the
// N = 20 000 reference for N and every larger N (checked up to 64). Measured with the split integrator: max error
// 1.0e-4 at N = 8 (M-3), 5e-5 at 12, 1.8e-5 at 16, 1.2e-6 at 32 (4th order); below 8 the error is set by the
// slip-direction step limiter rather than by N and is not monotone (N = 7: 2.8e-4, N = 5 / 6: 1.3e-4 / 1.0e-4, N = 4:
// 3.5e-4). The spec's plain RK4 needs N ~ 180 for the same M-2..M-4 accuracy (9e-5 at N = 200) and ~36 us per hit here.

#include "Cushion/CushionTestUtil.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace rbcushiontest;

namespace
{
	struct SweepCase
	{
		rb::Vec3 V;
		rb::Vec3 W;
		double E = 0.0;
	};

	std::vector<SweepCase> MakeSweep(bool Typical, int Count, std::uint64_t Seed)
	{
		std::vector<SweepCase> Cases;
		Cases.reserve(static_cast<size_t>(Count));
		rb::Rng Rng(Seed);
		const rb::CushionRestitutionLaw Law{};
		for (int i = 0; i < Count; ++i)
		{
			SweepCase C;
			if (Typical)
			{
				TypicalImpact(Rng, C.V, C.W);
			}
			else
			{
				StressImpact(Rng, C.V, C.W);
			}
			C.E = rb::CushionRestitution(C.V.y, Law);
			Cases.push_back(C);
		}
		return Cases;
	}

	// Max and 99th percentile of the velocity error relative to max(1 m/s, speed) against the converged split
	// integrator (N = 1024; it agrees with plain RK4 at N = 4e5 to ~1e-6 m/s).
	void SweepError(const std::vector<SweepCase>& Cases, int Steps, double& MaxErr, double& P99)
	{
		const rb::BallSpec Ball = PoolBall();
		std::vector<double> Errors;
		Errors.reserve(Cases.size());
		for (const SweepCase& C : Cases)
		{
			const rb::CushionImpactResult Ref = rb::ResolveMathavan(C.V, C.W, Ball, PoolMathavan(C.E, 1024, true));
			const rb::CushionImpactResult R = rb::ResolveMathavan(C.V, C.W, Ball, PoolMathavan(C.E, Steps, true));
			Errors.push_back(ImpactError(R, Ref, kR) / rb::Max(1.0, rb::Length(C.V)));
		}
		std::sort(Errors.begin(), Errors.end());
		MaxErr = Errors.back();
		P99 = Errors[static_cast<size_t>(0.99 * static_cast<double>(Errors.size()))];
	}

	double GateError(int Steps, const rb::CushionImpactResult* Refs)
	{
		const rb::BallSpec Ball = PoolBall();
		double Worst = 0.0;
		for (int c = 0; c < 3; ++c)
		{
			const GateCase G = GateCaseAt(c);
			Worst = rb::Max(Worst, ImpactError(rb::ResolveMathavan(G.V, G.W, Ball, PoolMathavan(0.97, Steps, true)), Refs[c], kR));
		}
		return Worst;
	}
}

RB_TEST(ARCH_CUSH1_MathavanAccuracyGate)
{
	const rb::BallSpec Ball = PoolBall();
	const int N = rb::CushionParams{}.MathavanSteps;
	RB_CHECK(N == 8); // the decision recorded in Cushion.h (update both with the gate)
	RB_CHECK(rb::CushionParams{}.MathavanSplitAtSlipReversal);
	RB_CHECK(rb::MathavanSettings{}.Steps == N);

	rb::CushionImpactResult Refs[3];
	for (int c = 0; c < 3; ++c)
	{
		const GateCase G = GateCaseAt(c);
		Refs[c] = rb::ResolveMathavan(G.V, G.W, Ball, PoolMathavan(0.97, 20000, false));
		// The plain N = 20 000 reference reproduces the spec's printed values.
		RB_CHECK(MaxAbsDiff(Refs[c].Velocity, G.ExpectedV) <= 1e-6);
		RB_CHECK(MaxAbsDiff(Refs[c].Omega * kR, G.ExpectedRW) <= 1.5e-6);
	}
	// The default N and every larger N (to 64) meet the gate; the default keeps a factor-2 margin.
	const double AtDefault = GateError(N, Refs);
	std::printf("  A-CUSH-1: N = %d max error %.3e (gate 2e-4); N - 1 = %d: %.3e\n", N, AtDefault, N - 1, GateError(N - 1, Refs));
	RB_CHECK(AtDefault <= 1.01e-4);
	for (int Steps = N; Steps <= 64; ++Steps)
	{
		RB_CHECK(GateError(Steps, Refs) <= 2e-4);
	}
	// Convergence is 4th order once N exceeds the limiter's step (N = 16 -> 32: ~15x).
	RB_CHECK(GateError(32, Refs) <= GateError(16, Refs) / 8.0);

	// Random rail hits (typical play and a stress set with masse-like spins) at the default N: relative error <= 1e-3 in
	// the worst case, <= 2.5e-4 at the 99th percentile (measured, 2000 hits: typical 3.4e-4 / 7.1e-5, stress 2.3e-4 /
	// 9.2e-5; the spec's plain RK4 at N = 200 gives about 2e-3 / 1.5e-3 on such sets).
	const int Count = SweepCount(2000, 300);
	double MaxErr = 0.0;
	double P99 = 0.0;
	SweepError(MakeSweep(true, Count, 777u), N, MaxErr, P99);
	std::printf("  A-CUSH-1: typical hits: max %.3e, p99 %.3e\n", MaxErr, P99);
	RB_CHECK(MaxErr <= 1e-3);
	RB_CHECK(P99 <= 2.5e-4);
	SweepError(MakeSweep(false, Count, 12345u), N, MaxErr, P99);
	std::printf("  A-CUSH-1: stress hits: max %.3e, p99 %.3e\n", MaxErr, P99);
	RB_CHECK(MaxErr <= 1e-3);
	RB_CHECK(P99 <= 2.5e-4);
}

RB_TEST(COL_Mathavan_SplitConvergesToThePlainIntegrator)
{
	// The Filippov (split) integrator and the spec's plain RK4 converge to the same solution (differences at large N
	// are the plain scheme's first-order error at slip zeros).
	const rb::BallSpec Ball = PoolBall();
	const std::vector<SweepCase> Cases = MakeSweep(false, SweepCount(40, 8), 4242u);
	for (const SweepCase& C : Cases)
	{
		const rb::CushionImpactResult S = rb::ResolveMathavan(C.V, C.W, Ball, PoolMathavan(C.E, 1024, true));
		const rb::CushionImpactResult P = rb::ResolveMathavan(C.V, C.W, Ball, PoolMathavan(C.E, 100000, false));
		RB_CHECK(ImpactError(S, P, kR) <= 2e-5 * rb::Max(1.0, rb::Length(C.V)));
	}
}

RB_TEST(COL_Mathavan_Deterministic)
{
	// Bitwise identical results for identical input (no hidden state), for both integrators.
	const rb::BallSpec Ball = PoolBall();
	const std::vector<SweepCase> Cases = MakeSweep(false, 200, 99u);
	for (const SweepCase& C : Cases)
	{
		for (int Split = 0; Split < 2; ++Split)
		{
			const rb::MathavanSettings S = PoolMathavan(C.E, Split != 0 ? 8 : 50, Split != 0);
			const rb::CushionImpactResult A = rb::ResolveMathavan(C.V, C.W, Ball, S);
			const rb::CushionImpactResult B = rb::ResolveMathavan(C.V, C.W, Ball, S);
			RB_CHECK(A.Velocity == B.Velocity && A.Omega == B.Omega && A.NormalImpulse == B.NormalImpulse);
		}
	}
}

RB_TEST(ARCH_CUSH2_Slow_MathavanMicroBenchmark)
{
	// <= 2 us per rail hit at the default N (Release; the minimum of 5 timed passes over 2000 typical hits, the
	// gate cases M-2..M-4 and the stress set reported). Debug builds only report.
	const rb::BallSpec Ball = PoolBall();
	const int N = rb::CushionParams{}.MathavanSteps;
	const std::vector<SweepCase> Typical = MakeSweep(true, 2000, 777u);
	const std::vector<SweepCase> Stress = MakeSweep(false, 2000, 12345u);
	auto Time = [&](const std::vector<SweepCase>& Cases) {
		double Best = 1e30;
		double Sink = 0.0;
		for (int Trial = 0; Trial < 5; ++Trial)
		{
			const auto T0 = std::chrono::steady_clock::now();
			for (int Repeat = 0; Repeat < 5; ++Repeat)
			{
				for (const SweepCase& C : Cases)
				{
					Sink += rb::ResolveMathavan(C.V, C.W, Ball, PoolMathavan(C.E, N, true)).Velocity.y;
				}
			}
			const auto T1 = std::chrono::steady_clock::now();
			Best = rb::Min(Best, std::chrono::duration<double, std::micro>(T1 - T0).count() / (5.0 * static_cast<double>(Cases.size())));
		}
		return Sink != 0.0 ? Best : Best + 0.0;
	};
	std::vector<SweepCase> Gate;
	for (int Repeat = 0; Repeat < 200; ++Repeat)
	{
		for (int c = 0; c < 3; ++c)
		{
			const GateCase G = GateCaseAt(c);
			Gate.push_back({G.V, G.W, 0.97});
		}
	}
	const double TypicalUs = Time(Typical);
	const double GateUs = Time(Gate);
	const double StressUs = Time(Stress);
	std::printf("  A-CUSH-2: N = %d: typical hits %.3f us, M-2..M-4 %.3f us, stress set %.3f us per hit\n", N, TypicalUs, GateUs, StressUs);
#if !(defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS)
	RB_CHECK(TypicalUs <= 2.0);
	RB_CHECK(GateUs <= 2.0);
#endif
}
