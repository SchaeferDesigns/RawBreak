// TEMPORARY review scratch (deleted before commit).
#include "rbtest.h"

#include "DetectTestUtil.h"

#include "rb/Core/Random.h"

#include <cstdio>
#include <cstring>

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
	Vec3 RandDir(rb::Rng& Rng)
	{
		for (;;)
		{
			const Vec3 V{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)};
			const double L = rb::Length(V);
			if (L > 0.1 && L <= 1.0)
			{
				return V / L;
			}
		}
	}

	double TorusGapS(const rb::PocketGeometry& P, const Vec3& C)
	{
		const double Rho = rb::Length(rb::XY(C) - P.CaptureCenter);
		return std::sqrt(rb::Square(Rho - P.DropEdgeRadius) + rb::Square(C.z + P.DropRadius)) - (kR + P.DropRadius);
	}

	bool InArc(const Vec2& D, double From, double Sweep)
	{
		double Delta = std::atan2(D.y, D.x) - From;
		Delta -= rb::kTwoPi * std::floor(Delta / rb::kTwoPi);
		return Delta <= Sweep;
	}

	// First valid downward crossing of Gap by sampling + bisection.
	template <class GapFn, class ValidFn>
	double BruteFirst(const GapFn& Gap, const ValidFn& Valid, double TauMax, int N, bool& Graze)
	{
		Graze = false;
		double Prev = Gap(0.0);
		double PrevT = 0.0;
		for (int i = 1; i <= N; ++i)
		{
			const double T = TauMax * i / N;
			const double G = Gap(T);
			if (Prev > 0.0 && G <= 0.0)
			{
				double Lo = PrevT, Hi = T;
				for (int k = 0; k < 200; ++k)
				{
					const double M = 0.5 * (Lo + Hi);
					if (M <= Lo || M >= Hi)
						break;
					(Gap(M) <= 0.0 ? Hi : Lo) = M;
				}
				if (Valid(Hi))
				{
					return Hi;
				}
			}
			Prev = G;
			PrevT = T;
		}
		return rb::kInfinity;
	}
}

RB_TEST(Scratch_RimTorusRandom)
{
	const rb::PocketGeometry P = CornerPocket();
	rb::Rng Rng(123);
	int Cases = 0, Mismatch = 0, Hits = 0;
	for (int Trial = 0; Trial < 3000; ++Trial)
	{
		const double Ang = P.FrontArcFrom + Rng.NextUniform(-0.2, P.FrontArcSweep + 0.2);
		const double Rho = Rng.NextUniform(0.0, P.DropEdgeRadius + 0.01);
		const Vec2 H = P.CaptureCenter + Vec2{std::cos(Ang), std::sin(Ang)} * Rho;
		const Vec3 Start{H.x, H.y, Rng.NextUniform(-0.06, 0.06)};
		if (TorusGapS(P, Start) < 1e-5)
			continue;
		const Vec3 V = RandDir(Rng) * Rng.NextUniform(0.0, 3.0);
		MotionSegment S = PocketFall(Start, V);
		S.TauEnd = 0.3;
		const ContactPrediction C = rb::PredictRimTorus(S, kR, P, rb::kInfinity, Numerics());
		const auto Gap = [&](double Tau) { return TorusGapS(P, rb::PositionAt(S, Tau)); };
		const auto Valid = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			const Vec2 D = rb::XY(X) - P.CaptureCenter;
			return InArc(D, P.FrontArcFrom - 1e-8, P.FrontArcSweep + 2e-8) && rb::Length(D) <= P.DropEdgeRadius + 1e-9 && X.z >= -P.DropRadius - 1e-9;
		};
		bool Graze = false;
		const double Ref = BruteFirst(Gap, Valid, S.TauEnd, 20000, Graze);
		++Cases;
		const bool RefFound = Ref < rb::kInfinity;
		Hits += RefFound;
		if (RefFound != C.Found || (RefFound && rb::Abs(Ref - C.Time) > 1e-9))
		{
			++Mismatch;
			if (Mismatch < 10)
				std::printf("  torus mismatch trial %d: ref %.12g (%d) det %.12g (%d) flags %d start gap %.3g\n", Trial, Ref, RefFound, C.Time, C.Found,
					C.Flags, TorusGapS(P, Start));
		}
	}
	std::printf("  torus: %d cases, %d hits, %d mismatches\n", Cases, Hits, Mismatch);
}

RB_TEST(Scratch_BallBallSymmetry)
{
	rb::Rng Rng(99);
	int Asym = 0, Mirror = 0, Cases = 0;
	for (int Trial = 0; Trial < 20000; ++Trial)
	{
		const Vec3 PA{Rng.NextUniform(-1, 1), Rng.NextUniform(-0.5, 0.5), kR};
		const Vec3 PB = PA + Vec3{Rng.NextUniform(-0.3, 0.3), Rng.NextUniform(-0.3, 0.3), 0.0};
		if (rb::Length(PB - PA) < 2 * kR + 1e-6)
			continue;
		const Vec3 VA = Vec3{Rng.NextUniform(-3, 3), Rng.NextUniform(-3, 3), 0.0};
		const Vec3 WA{Rng.NextUniform(-100, 100), Rng.NextUniform(-100, 100), Rng.NextUniform(-50, 50)};
		const MotionSegment A = Sliding(PA, VA, WA, kMuS, kG, Rng.NextUniform(0, 1));
		const MotionSegment B = Rng.NextBelow(2) ? Rolling(PB, Vec3{Rng.NextUniform(-2, 2), Rng.NextUniform(-2, 2), 0.0}, kMuR, kG, Rng.NextUniform(0, 1)) : Stationary(PB);
		const ContactPrediction P1 = rb::PredictBallBall(A, kR, B, kR, rb::kInfinity, Numerics());
		const ContactPrediction P2 = rb::PredictBallBall(B, kR, A, kR, rb::kInfinity, Numerics());
		++Cases;
		if (P1.Found != P2.Found || std::memcmp(&P1.Time, &P2.Time, 8) != 0 || P1.Flags != P2.Flags)
			++Asym;
		MotionSegment MA = A, MB = B;
		for (MotionSegment* M : {&MA, &MB})
		{
			M->Pos0.y = -M->Pos0.y;
			M->Vel0.y = -M->Vel0.y;
			M->Accel2.y = -M->Accel2.y;
		}
		const ContactPrediction P3 = rb::PredictBallBall(MA, kR, MB, kR, rb::kInfinity, Numerics());
		if (P1.Found != P3.Found || std::memcmp(&P1.Time, &P3.Time, 8) != 0)
			++Mirror;
	}
	std::printf("  bb symmetry: %d cases, %d swap asym, %d mirror asym\n", Cases, Asym, Mirror);
}

RB_TEST(Scratch_NaNInf)
{
	const double Nan = std::nan("");
	const double Inf = rb::kInfinity;
	const double Vals[] = {Nan, Inf, -Inf, 1e300, -1e300, 1e-300};
	const rb::PocketGeometry P = CornerPocket();
	int Weird = 0;
	for (double X : Vals)
	{
		for (int Field = 0; Field < 6; ++Field)
		{
			MotionSegment S = Airborne({0.1, 0.2, 0.05}, {1.0, 0.5, 0.3});
			double* F[6] = {&S.Pos0.x, &S.Vel0.y, &S.Accel2.z, &S.TauEnd, &S.T0, &S.Pos0.z};
			*F[Field] = X;
			const MotionSegment B = Stationary({0.2, 0.2, kR});
			const ContactPrediction C1 = rb::PredictBallBall(S, kR, B, kR, rb::kInfinity, Numerics());
			const ContactPrediction C2 = rb::PredictRimTorus(S, kR, P, rb::kInfinity, Numerics());
			const ContactPrediction C3 = rb::PredictLinerWall(S, kR, P, rb::kInfinity, Numerics());
			const ContactPrediction C4 = rb::PredictNoseAirborne(S, kR, MakeNose({1, 0.635}, {-1, 0.635}, {0, -1}), 0.0, rb::kInfinity, Numerics());
			for (const ContactPrediction& C : {C1, C2, C3, C4})
			{
				if (C.Found && !rb::IsFinite(C.Time))
				{
					++Weird;
					std::printf("  non-finite found time: value %g field %d\n", X, Field);
				}
			}
		}
	}
	std::printf("  NaN/Inf weird: %d\n", Weird);
}

RB_TEST(Scratch_DumpForMpmath)
{
	rb::Rng Rng(4242);
	std::FILE* F = nullptr;
	fopen_s(&F, "C:/Users/Colin/AppData/Local/Temp/claude/C--Users-Colin-Desktop--00000-RawBreak/5b3aed06-5b65-44e3-a364-54bee81bf06a/scratchpad/bbcases.txt", "w");
	RB_REQUIRE(F != nullptr);
	int Written = 0;
	while (Written < 3000)
	{
		const auto Seg = [&](double T0, double R) -> MotionSegment
		{
			const Vec3 P{Rng.NextUniform(-1.2, 1.2), Rng.NextUniform(-0.6, 0.6), R};
			const std::uint32_t K = Rng.NextBelow(10);
			const double A = Rng.NextUniform(0, rb::kTwoPi);
			const Vec3 D{std::cos(A), std::sin(A), 0.0};
			if (K < 4)
			{
				MotionSegment S = Rolling(P, D * Rng.NextUniform(0.001, 4.0), kMuR, kG, T0, R);
				if (Rng.NextBelow(2) == 0)
				{
					const double B = Rng.NextUniform(0, rb::kTwoPi);
					S.Accel2 = S.Accel2 + Vec3{std::cos(B), std::sin(B), 0.0} * Rng.NextUniform(0.0, 0.03);
				}
				return S;
			}
			if (K < 7)
			{
				const Vec3 W{Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-50.0, 50.0)};
				const Vec3 V = D * Rng.NextUniform(0.0, 5.0);
				if (rb::Length(rb::Planar(rb::SlipVelocity(V, W, R))) < 1e-3)
					return Stationary(P, T0);
				return Sliding(P, V, W, kMuS, kG, T0, R);
			}
			if (K < 9)
				return Airborne({P.x, P.y, R + Rng.NextUniform(0.0, 0.08)}, D * Rng.NextUniform(0.0, 4.0) + Vec3{0.0, 0.0, Rng.NextUniform(0.05, 2.5)}, kG, T0, R);
			return Stationary(P, T0);
		};
		const double RA = Rng.NextBelow(8) == 0 ? 0.0301625 : kR;
		const MotionSegment A = Seg(Rng.NextUniform(0.0, 2.0), RA);
		MotionSegment B = Seg(Rng.NextUniform(0.0, 2.0), kR);
		const double RefTime = rb::Max(A.T0, B.T0);
		const double End = rb::Min(A.T0 + A.TauEnd, B.T0 + B.TauEnd);
		if (!(End > RefTime))
			continue;
		const double t = Rng.NextUniform(RefTime, rb::Min(End, RefTime + 3.0));
		const double Ang = Rng.NextUniform(0, rb::kTwoPi);
		const double Off = Rng.NextBelow(3) == 0 ? RA + kR + Rng.NextUniform(-1e-5, 1e-5) : Rng.NextUniform(0.0, 3.0 * kR);
		const Vec3 Shift = rb::Planar(rb::PositionAt(A, t - A.T0) - rb::PositionAt(B, t - B.T0)) + Vec3{std::cos(Ang), std::sin(Ang), 0.0} * Off;
		B.Pos0 = B.Pos0 + Shift;
		const Vec3 Gap = rb::PositionAt(B, RefTime - B.T0) - rb::PositionAt(A, RefTime - A.T0);
		if (rb::Length(Gap) < RA + kR + 1e-6)
			continue;
		const rb::ContactPrediction P = rb::PredictBallBall(A, RA, B, kR, rb::kInfinity, Numerics());
		const MotionSegment* Both[2] = {&A, &B};
		for (const MotionSegment* S : Both)
		{
			std::fprintf(F, "%a %a %a %a %a %a %a %a %a %a %a ", S->T0, S->TauEnd, S->Pos0.x, S->Pos0.y, S->Pos0.z, S->Vel0.x, S->Vel0.y, S->Vel0.z, S->Accel2.x,
				S->Accel2.y, S->Accel2.z);
		}
		std::fprintf(F, "%a %a %d %a\n", RA, kR, P.Found ? 1 : 0, P.Time);
		++Written;
	}
	std::fclose(F);
}
