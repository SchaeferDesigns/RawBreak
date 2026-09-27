#pragma once

// Shared helpers of the WP-5 detection tests (Tests/Core/Detect): hand-built closed-form segments (motion spec A/C,
// k = 2/5, so no WP-1 implementation is needed), hand-built table features, brute-force samplers and a double-double
// (106-bit) reference for the ball-ball first-entry time (D-10, ROOT-01).

#include "rb/Core/Constants.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/Detect.h"
#include "rb/Physics/Motion.h"

#include <cmath>

namespace detecttest
{
	using rb::Vec2;
	using rb::Vec3;

	inline constexpr double kR = 0.028575;         // pool ball radius [m]
	inline constexpr double kG = 9.80665;          // COL / MOT gravity
	inline constexpr double kGVal = 9.81;          // VAL test tables pin g = 9.81
	inline constexpr double kMuS = 0.2;
	inline constexpr double kMuR = 0.010;
	inline constexpr double kNoseH = 0.635 * 2.0 * kR; // h = 0.635 D = 0.03629025 m

	// R_c = sqrt(R^2 - (h - R)^2) (collisions 4.1; computed here, not by WP-2's ComputeCushionContact).
	inline double NoseContactOffset(double R = kR, double H = kNoseH) { return std::sqrt(R * R - (H - R) * (H - R)); }

	inline rb::NumericsConfig Numerics() { return rb::NumericsConfig{}; }

	// ---------------------------------------------------------------------------------------------
	// Closed-form segments (motion spec A.3-A.8, C.1), k = 2/5
	// ---------------------------------------------------------------------------------------------
	inline rb::MotionSegment Stationary(const Vec3& P, double T0 = 0.0)
	{
		rb::MotionSegment S;
		S.State = rb::MotionState::Stationary;
		S.T0 = T0;
		S.Pos0 = P;
		S.TauEnd = rb::kInfinity;
		return S;
	}

	inline rb::MotionSegment Rolling(const Vec3& P, const Vec3& V, double MuR = kMuR, double G = kG, double T0 = 0.0, double R = kR)
	{
		rb::MotionSegment S;
		S.State = rb::MotionState::Rolling;
		S.T0 = T0;
		S.Radius = R;
		S.Pos0 = P;
		S.Vel0 = V;
		const double Speed = rb::Length(V);
		S.Accel2 = V * (-0.5 * MuR * G / Speed);
		S.Omega0 = rb::RollingOmegaH(V, R);
		S.TauEnd = Speed / (MuR * G);
		return S;
	}

	inline rb::MotionSegment Sliding(const Vec3& P, const Vec3& V, const Vec3& W, double MuS = kMuS, double G = kG, double T0 = 0.0, double R = kR)
	{
		rb::MotionSegment S;
		S.State = rb::MotionState::Sliding;
		S.T0 = T0;
		S.Radius = R;
		S.Pos0 = P;
		S.Vel0 = V;
		S.Omega0 = W;
		const Vec3 U = rb::Planar(rb::SlipVelocity(V, W, R));
		const double Slip = rb::Length(U);
		S.Accel2 = U * (-0.5 * MuS * G / Slip);
		S.TauEnd = 2.0 * Slip / (7.0 * MuS * G);
		return S;
	}

	inline rb::MotionSegment Airborne(const Vec3& P, const Vec3& V, double G = kG, double T0 = 0.0, double R = kR)
	{
		rb::MotionSegment S;
		S.State = rb::MotionState::Airborne;
		S.T0 = T0;
		S.Radius = R;
		S.Pos0 = P;
		S.Vel0 = V;
		S.Accel2 = {0.0, 0.0, -0.5 * G};
		S.TauEnd = (V.z + std::sqrt(V.z * V.z + 2.0 * G * (P.z - R))) / G;
		return S;
	}

	// Ballistic inside a pocket: no landing (TauEnd = +inf).
	inline rb::MotionSegment PocketFall(const Vec3& P, const Vec3& V, double G = kG, double T0 = 0.0)
	{
		rb::MotionSegment S = Airborne(P, V, G, T0);
		S.State = rb::MotionState::PocketFall;
		S.TauEnd = rb::kInfinity;
		return S;
	}

	// ---------------------------------------------------------------------------------------------
	// Hand-built features
	// ---------------------------------------------------------------------------------------------
	inline rb::NoseSegment MakeNose(const Vec2& Start, const Vec2& End, const Vec2& Inward, double H = kNoseH)
	{
		rb::NoseSegment N;
		N.Start = Start;
		N.End = End;
		N.Length = rb::Length(End - Start);
		N.Direction = (End - Start) / N.Length;
		N.InwardNormal = Inward;
		N.Height = H;
		return N;
	}

	inline rb::JawArc MakeJaw(const Vec2& Center, double Radius, double From, double Sweep, double H = kNoseH)
	{
		rb::JawArc J;
		J.Center = Center;
		J.Radius = Radius;
		J.AngleFrom = From;
		J.AngleSweep = Sweep;
		J.Height = H;
		return J;
	}

	inline rb::Facing MakeFacing(const Vec2& Start, const Vec2& End, const Vec2& PocketNormal, double BackdraftDeg = 12.0, double H = kNoseH)
	{
		rb::Facing F;
		F.Start = Start;
		F.End = End;
		F.Length = rb::Length(End - Start);
		F.Direction = (End - Start) / F.Length;
		F.PocketNormal = PocketNormal;
		F.TopHeight = H;
		F.Backdraft = BackdraftDeg * rb::kDegToRad;
		return F;
	}

	// 9FT_PRO FOOT_LEFT corner pocket (P3) of collisions 9.5 / equipment 5.3: C_cap (1.302615, 0.667615), r_p 0.062,
	// r_d 0.0047625; the front arc spans +-FrontHalfDeg around the direction back to the table (-135 deg).
	inline rb::PocketGeometry CornerPocket(double FrontHalfDeg = 60.0)
	{
		rb::PocketGeometry P;
		P.Id = rb::PocketId::FootLeft;
		P.Kind = rb::PocketKind::Corner;
		P.Axis = {std::sqrt(0.5), std::sqrt(0.5)};
		P.MouthMid = {1.229589, 0.594589};
		P.CaptureCenter = {1.302615, 0.667615};
		P.CaptureRadius = 0.062;
		P.DropRadius = 0.0047625;
		P.DropEdgeRadius = P.CaptureRadius + P.DropRadius;
		P.FrontArcFrom = (-135.0 - FrontHalfDeg) * rb::kDegToRad;
		P.FrontArcSweep = 2.0 * FrontHalfDeg * rb::kDegToRad;
		P.LinerUndercut = 12.0 * rb::kDegToRad;
		P.Backdraft = 12.0 * rb::kDegToRad;
		P.WallTopZ = 0.048;
		return P;
	}

	// ---------------------------------------------------------------------------------------------
	// Brute force: first time in [T0, T0 + TauMax] at which Gap(tau) <= 0, sampled on N points and bisected.
	// ---------------------------------------------------------------------------------------------
	template <class GapFn>
	inline double BruteForceFirstContact(const GapFn& Gap, double TauMax, int N = 200000)
	{
		double Prev = 0.0;
		for (int i = 0; i <= N; ++i)
		{
			const double Tau = TauMax * static_cast<double>(i) / static_cast<double>(N);
			if (Gap(Tau) <= 0.0)
			{
				if (i == 0)
				{
					return 0.0;
				}
				double Lo = Prev;
				double Hi = Tau;
				for (int k = 0; k < 200 && Hi - Lo > 0.0; ++k)
				{
					const double Mid = 0.5 * (Lo + Hi);
					if (Mid <= Lo || Mid >= Hi)
					{
						break;
					}
					(Gap(Mid) <= 0.0 ? Hi : Lo) = Mid;
				}
				return Hi;
			}
			Prev = Tau;
		}
		return rb::kInfinity;
	}

	// ---------------------------------------------------------------------------------------------
	// Double-double arithmetic (Dekker / Knuth error-free transformations; no FMA) for the 106-bit reference.
	// ---------------------------------------------------------------------------------------------
	struct DD
	{
		double Hi = 0.0;
		double Lo = 0.0;
	};

	inline DD TwoSum(double a, double b)
	{
		const double s = a + b;
		const double bb = s - a;
		return {s, (a - (s - bb)) + (b - bb)};
	}

	inline DD QuickTwoSum(double a, double b)
	{
		const double s = a + b;
		return {s, b - (s - a)};
	}

	inline void SplitDouble(double a, double& Hi, double& Lo)
	{
		const double t = 134217729.0 * a;
		Hi = t - (t - a);
		Lo = a - Hi;
	}

	inline DD TwoProd(double a, double b)
	{
		const double p = a * b;
		double ah = 0.0, al = 0.0, bh = 0.0, bl = 0.0;
		SplitDouble(a, ah, al);
		SplitDouble(b, bh, bl);
		return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
	}

	inline DD operator+(const DD& a, const DD& b)
	{
		DD s = TwoSum(a.Hi, b.Hi);
		const DD t = TwoSum(a.Lo, b.Lo);
		s.Lo += t.Hi;
		s = QuickTwoSum(s.Hi, s.Lo);
		s.Lo += t.Lo;
		return QuickTwoSum(s.Hi, s.Lo);
	}

	inline DD operator-(const DD& a) { return {-a.Hi, -a.Lo}; }
	inline DD operator-(const DD& a, const DD& b) { return a + (-b); }

	inline DD operator*(const DD& a, const DD& b)
	{
		DD p = TwoProd(a.Hi, b.Hi);
		p.Lo += a.Hi * b.Lo + a.Lo * b.Hi;
		return QuickTwoSum(p.Hi, p.Lo);
	}

	inline DD ToDD(double a) { return {a, 0.0}; }
	inline int SignDD(const DD& a) { return a.Hi > 0.0 ? 1 : (a.Hi < 0.0 ? -1 : (a.Lo > 0.0 ? 1 : (a.Lo < 0.0 ? -1 : 0))); }

	struct DDVec
	{
		DD x, y, z;
	};

	inline DDVec operator+(const DDVec& a, const DDVec& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
	inline DDVec operator-(const DDVec& a, const DDVec& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
	inline DDVec operator*(const DDVec& a, const DD& s) { return {a.x * s, a.y * s, a.z * s}; }
	inline DD DotDD(const DDVec& a, const DDVec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline DDVec ToDDVec(const Vec3& v) { return {ToDD(v.x), ToDD(v.y), ToDD(v.z)}; }

	// Polynomial with DD coefficients (degree <= 4).
	struct DDPoly
	{
		DD c[5];
		int Degree = 0;

		DD Eval(double x) const
		{
			const DD X = ToDD(x);
			DD r = c[Degree];
			for (int i = Degree - 1; i >= 0; --i)
			{
				r = r * X + c[i];
			}
			return r;
		}

		DDPoly Derivative() const
		{
			DDPoly D;
			D.Degree = Degree > 0 ? Degree - 1 : 0;
			for (int i = 1; i <= Degree; ++i)
			{
				D.c[i - 1] = c[i] * ToDD(static_cast<double>(i));
			}
			return D;
		}
	};

	// Bisection of a sign change of P on [Lo, Hi] down to adjacent doubles.
	inline double BisectDD(const DDPoly& P, double Lo, double Hi)
	{
		const int SLo = SignDD(P.Eval(Lo));
		for (int k = 0; k < 2000; ++k)
		{
			const double Mid = 0.5 * (Lo + Hi);
			if (Mid <= Lo || Mid >= Hi)
			{
				break;
			}
			const int SMid = SignDD(P.Eval(Mid));
			if (SMid == 0)
			{
				return Mid;
			}
			(SMid == SLo ? Lo : Hi) = Mid;
		}
		return 0.5 * (Lo + Hi);
	}

	// All sign changes of P in [Lo, Hi] (critical-point isolation with pure bisection; ascending).
	inline int RootsDD(DDPoly P, double Lo, double Hi, double* Out)
	{
		while (P.Degree > 0 && SignDD(P.c[P.Degree]) == 0)
		{
			--P.Degree;
		}
		if (P.Degree <= 0)
		{
			return 0;
		}
		double Knots[6];
		int NumKnots = 0;
		Knots[NumKnots++] = Lo;
		if (P.Degree >= 2)
		{
			double Crit[4];
			const int NumCrit = RootsDD(P.Derivative(), Lo, Hi, Crit);
			for (int i = 0; i < NumCrit; ++i)
			{
				if (Crit[i] > Knots[NumKnots - 1] && Crit[i] < Hi)
				{
					Knots[NumKnots++] = Crit[i];
				}
			}
		}
		Knots[NumKnots++] = Hi;
		int Count = 0;
		for (int i = 0; i + 1 < NumKnots; ++i)
		{
			const int Sa = SignDD(P.Eval(Knots[i]));
			const int Sb = SignDD(P.Eval(Knots[i + 1]));
			if (Sa != 0 && Sb != 0 && Sa != Sb)
			{
				Out[Count++] = BisectDD(P, Knots[i], Knots[i + 1]);
			}
			else if (Sb == 0 && i + 2 < NumKnots)
			{
				Out[Count++] = Knots[i + 1];
			}
		}
		return Count;
	}

	struct ReferenceEntry
	{
		bool Found = false;
		double Tau = rb::kInfinity;    // local time of the first downward crossing (exact to ~1 ulp)
		double RunMinimum = 0.0;       // f at the end of the decreasing run that contains it [m^2]
		double ApproachSpeed = 0.0;    // -f'(tau) / (2 (R1 + R2)) [m/s]
	};

	// 106-bit reference of the first ENTRY of the ball-ball gap function on [0, TauMax] (local time from
	// RefTime = max(T0)), from the exact segment data: the expansions, the coefficients and every evaluation are
	// double-double; the roots are isolated by critical points and located by pure bisection.
	inline ReferenceEntry ReferenceBallBallEntry(const rb::MotionSegment& A, double RA, const rb::MotionSegment& B, double RB, double TauMax)
	{
		const double RefTime = A.T0 > B.T0 ? A.T0 : B.T0;
		const auto ExpandDD = [RefTime](const rb::MotionSegment& S, DDVec& C, DDVec& V, DDVec& Acc)
		{
			const DD Dt = TwoSum(RefTime, -S.T0);
			const DDVec P0 = ToDDVec(S.Pos0);
			const DDVec V0 = ToDDVec(S.Vel0);
			Acc = ToDDVec(S.Accel2);
			C = P0 + V0 * Dt + Acc * (Dt * Dt);
			V = V0 + Acc * (Dt * ToDD(2.0));
		};
		DDVec Ca, Va, Aa, Cb, Vb, Ab;
		ExpandDD(A, Ca, Va, Aa);
		ExpandDD(B, Cb, Vb, Ab);
		const DDVec dC = Cb - Ca;
		const DDVec dB = Vb - Va;
		const DDVec dA = Ab - Aa;
		const DD Sum = TwoSum(RA, RB);
		DDPoly F;
		F.Degree = 4;
		F.c[0] = DotDD(dC, dC) - Sum * Sum;
		F.c[1] = DotDD(dB, dC) * ToDD(2.0);
		F.c[2] = DotDD(dB, dB) + DotDD(dA, dC) * ToDD(2.0);
		F.c[3] = DotDD(dA, dB) * ToDD(2.0);
		F.c[4] = DotDD(dA, dA);

		ReferenceEntry Out;
		if (!(TauMax > 0.0))
		{
			return Out;
		}
		double Knots[6];
		int NumKnots = 0;
		Knots[NumKnots++] = 0.0;
		double Crit[4];
		const int NumCrit = RootsDD(F.Derivative(), 0.0, TauMax, Crit);
		for (int i = 0; i < NumCrit; ++i)
		{
			if (Crit[i] > Knots[NumKnots - 1] && Crit[i] < TauMax)
			{
				Knots[NumKnots++] = Crit[i];
			}
		}
		Knots[NumKnots++] = TauMax;
		double Values[6];
		for (int k = 0; k < NumKnots; ++k)
		{
			const DD V = F.Eval(Knots[k]);
			Values[k] = V.Hi + V.Lo;
		}
		for (int i = 0; i + 1 < NumKnots; ++i)
		{
			if (!(Values[i + 1] < Values[i]))
			{
				continue;
			}
			int j = i + 1;
			while (j + 1 < NumKnots && Values[j + 1] <= Values[j])
			{
				++j;
			}
			if (Values[i] > 0.0 && Values[j] <= 0.0)
			{
				int p = i;
				while (!(Values[p + 1] <= 0.0))
				{
					++p;
				}
				Out.Found = true;
				Out.Tau = Values[p + 1] == 0.0 ? Knots[p + 1] : BisectDD(F, Knots[p], Knots[p + 1]);
				Out.RunMinimum = Values[j];
				const DD Slope = F.Derivative().Eval(Out.Tau);
				Out.ApproachSpeed = -(Slope.Hi + Slope.Lo) / (2.0 * (RA + RB));
				return Out;
			}
			i = j - 1;
		}
		return Out;
	}
}
