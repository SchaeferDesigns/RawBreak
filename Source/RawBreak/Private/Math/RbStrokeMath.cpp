#include "Math/RbStrokeMath.h"

// Stroke-input mathematics (plan 5.3-5.4, T9-T12). Owner: UE-5a.

namespace
{
	using RbStrokeMath::kWindowSlack;

	double GainSlope(const FRbStrokeGain& P)
	{
		return P.VSat > P.VKnee ? (P.GMax - P.G0) / (P.VSat - P.VKnee) : 0.0;
	}

	// S(v) without the tip-speed clamp: integral_0^v G(u) u du for v >= 0.
	double TravelIntegralUnclamped(double V, const FRbStrokeGain& P)
	{
		const double Vk = P.VKnee;
		const double Vs = P.VSat;
		if (V <= Vk || Vs <= Vk)
		{
			const double Vc = V <= Vk ? V : Vk;
			double S = 0.5 * P.G0 * Vc * Vc;
			if (V > Vk)
			{
				S += 0.5 * P.GMax * (V * V - Vk * Vk); // degenerate curve: a step from G0 to GMax at the knee
			}
			return S;
		}
		const double K = GainSlope(P);
		const double S1 = 0.5 * P.G0 * Vk * Vk;
		const double V2 = V < Vs ? V : Vs;
		const double S2 = 0.5 * (P.G0 - K * Vk) * (V2 * V2 - Vk * Vk) + K * (V2 * V2 * V2 - Vk * Vk * Vk) / 3.0;
		const double S3 = V > Vs ? 0.5 * P.GMax * (V * V - Vs * Vs) : 0.0;
		return S1 + S2 + S3;
	}

	// Least-squares quadratic y = c0 + c1 u + c2 u^2 over (u_i, y_i), accumulated one point at a time (no buffers: the
	// presentation fits every frame, and a 20 ms window of an 8 kHz mouse holds 160 samples); Cramer's rule on the normal
	// equations.
	struct FQuadraticSums
	{
		double S0 = 0.0, S1 = 0.0, S2 = 0.0, S3 = 0.0, S4 = 0.0;
		double T0 = 0.0, T1 = 0.0, T2 = 0.0;
		int32 N = 0;

		void Add(double u, double y)
		{
			const double u2 = u * u;
			S0 += 1.0;
			S1 += u;
			S2 += u2;
			S3 += u2 * u;
			S4 += u2 * u2;
			T0 += y;
			T1 += u * y;
			T2 += u2 * y;
			++N;
		}

		bool Solve(double& C1, double& C2) const
		{
			// | S0 S1 S2 |   | c0 |   | T0 |
			// | S1 S2 S3 | * | c1 | = | T1 |
			// | S2 S3 S4 |   | c2 |   | T2 |
			const double Det = S0 * (S2 * S4 - S3 * S3) - S1 * (S1 * S4 - S3 * S2) + S2 * (S1 * S3 - S2 * S2);
			const double Scale = S0 * S2 * S4;
			if (!(FMath::Abs(Det) > 1.0e-12 * FMath::Max(Scale, 1.0e-300)))
			{
				return false;
			}
			const double Det1 = S0 * (T1 * S4 - S3 * T2) - T0 * (S1 * S4 - S3 * S2) + S2 * (S1 * T2 - T1 * S2);
			const double Det2 = S0 * (S2 * T2 - T1 * S3) - S1 * (S1 * T2 - T1 * S2) + T0 * (S1 * S3 - S2 * S2);
			C1 = Det1 / Det;
			C2 = Det2 / Det;
			return true;
		}
	};

	template <typename TValue>
	bool FitQuadratic(const FRbStrokeSample* Samples, int32 Count, double T, double WindowStart, double WindowEnd, TValue Value,
		double& OutVelocity, double& OutAcceleration)
	{
		OutVelocity = 0.0;
		OutAcceleration = 0.0;
		if (!Samples || Count < 3)
		{
			return false;
		}
		const double Scale = FMath::Max(WindowEnd - WindowStart, 1.0e-6);
		FQuadraticSums Sums;
		double Origin = 0.0;
		bool bHaveOrigin = false;
		for (int32 i = 0; i < Count; ++i)
		{
			const double Time = Samples[i].Time;
			if (Time < WindowStart - kWindowSlack || Time > WindowEnd + kWindowSlack)
			{
				continue;
			}
			const double V = Value(Samples[i]);
			if (!bHaveOrigin)
			{
				Origin = V; // positions relative to the first sample of the window (smaller sums, same derivatives)
				bHaveOrigin = true;
			}
			Sums.Add((Time - T) / Scale, V - Origin);
		}
		double C1 = 0.0;
		double C2 = 0.0;
		if (Sums.N < 3 || !Sums.Solve(C1, C2))
		{
			return false;
		}
		OutVelocity = C1 / Scale;
		OutAcceleration = 2.0 * C2 / (Scale * Scale);
		return true;
	}
}

// ---------------------------------------------------------------------------------------------------------
// FRbCueIntegrator
// ---------------------------------------------------------------------------------------------------------

void FRbCueIntegrator::Reset(double InX)
{
	X = InX;
	bHasPrevious = false;
	Previous = FRbStrokeSample();
}

FRbCueStepResult FRbCueIntegrator::Step(const FRbStrokeSample& Sample, const FRbStrokeGain& Gain, bool bLive, double StopShort, double MaxBackswing)
{
	FRbCueStepResult Result;
	if (!bHasPrevious)
	{
		Previous = Sample;
		bHasPrevious = true;
		Result.bReference = true;
		return Result;
	}
	const double Dt = Sample.Time - Previous.Time;
	const double StepDt = Dt > MinStep ? Dt : MinStep;
	const double HandDelta = Sample.Position - Previous.Position;
	const double CueSpeed = RbStrokeMath::CueSpeedFromHandSpeed(HandDelta / StepDt, Gain);
	const double Dx = CueSpeed * StepDt;
	const double X0 = X;
	double X1 = X0 + Dx;

	if (bLive)
	{
		if (X0 < 0.0 && X1 >= 0.0 && Dx > 0.0)
		{
			Result.bCrossed = true;
			Result.CrossingTime = Previous.Time + (-X0) / (X1 - X0) * Dt;
		}
	}
	else
	{
		const double Front = -StopShort;
		if (Dx > 0.0 && X1 > Front)
		{
			X1 = X0 > Front ? X0 : Front;
			Result.bHitFrontLimit = true;
		}
	}
	const double Back = -MaxBackswing;
	if (Dx < 0.0 && X1 < Back)
	{
		X1 = X0 < Back ? X0 : Back;
		Result.bHitBackLimit = true;
	}
	Result.CueDelta = X1 - X0;
	X = X1;
	Previous = Sample;
	return Result;
}

// ---------------------------------------------------------------------------------------------------------
// RbStrokeMath
// ---------------------------------------------------------------------------------------------------------

namespace RbStrokeMath
{
	double CountsToMeters(double Counts, double Dpi)
	{
		return Dpi > 0.0 ? Counts / Dpi * 0.0254 : 0.0;
	}

	double Gain(double HandSpeed, const FRbStrokeGain& Params)
	{
		const double V = FMath::Abs(HandSpeed);
		if (V <= Params.VKnee)
		{
			return Params.G0;
		}
		if (V >= Params.VSat)
		{
			return Params.GMax;
		}
		return Params.G0 + (Params.GMax - Params.G0) * (V - Params.VKnee) / (Params.VSat - Params.VKnee);
	}

	double CueSpeedFromHandSpeed(double HandSpeed, const FRbStrokeGain& Params)
	{
		return FMath::Clamp(Gain(HandSpeed, Params) * HandSpeed, -Params.VTipMax, Params.VTipMax);
	}

	double HandSpeedForCueSpeed(double CueSpeed, const FRbStrokeGain& Params)
	{
		const double Sign = CueSpeed < 0.0 ? -1.0 : 1.0;
		const double V = FMath::Min(FMath::Abs(CueSpeed), Params.VTipMax);
		double Hand = 0.0;
		if (V <= Params.G0 * Params.VKnee)
		{
			Hand = Params.G0 > 0.0 ? V / Params.G0 : 0.0;
		}
		else if (Params.VSat > Params.VKnee && V < Params.GMax * Params.VSat)
		{
			// (G0 + k (v - v_k)) v = V  ->  k v^2 + (G0 - k v_k) v - V = 0 (positive root)
			const double K = GainSlope(Params);
			const double B = Params.G0 - K * Params.VKnee;
			Hand = K > 0.0 ? (-B + FMath::Sqrt(B * B + 4.0 * K * V)) / (2.0 * K) : V / Params.G0;
		}
		else
		{
			Hand = Params.GMax > 0.0 ? V / Params.GMax : 0.0;
		}
		return Sign * Hand;
	}

	double CueSpeedDerivative(double HandSpeed, const FRbStrokeGain& Params)
	{
		const double V = FMath::Abs(HandSpeed);
		if (Gain(V, Params) * V >= Params.VTipMax)
		{
			return 0.0; // the clamp bites
		}
		if (V <= Params.VKnee)
		{
			return Params.G0;
		}
		if (V >= Params.VSat)
		{
			return Params.GMax;
		}
		// d/dv [G(v) v] = G(v) + v G'(v)
		return Gain(V, Params) + V * GainSlope(Params);
	}

	double CueTravelIntegral(double HandSpeed, const FRbStrokeGain& Params)
	{
		const double V = FMath::Abs(HandSpeed);
		const double Clamp = FMath::Abs(HandSpeedForCueSpeed(Params.VTipMax, Params));
		if (V <= Clamp)
		{
			return TravelIntegralUnclamped(V, Params);
		}
		return TravelIntegralUnclamped(Clamp, Params) + Params.VTipMax * (V - Clamp);
	}

	bool QuadraticFit(const FRbStrokeSample* Samples, int32 Count, double T, double WindowStart, double WindowEnd,
		double& OutVelocity, double& OutAcceleration)
	{
		return FitQuadratic(Samples, Count, T, WindowStart, WindowEnd, [](const FRbStrokeSample& S) { return S.Position; },
			OutVelocity, OutAcceleration);
	}

	bool QuadraticFitLateral(const FRbStrokeSample* Samples, int32 Count, double T, double WindowStart, double WindowEnd,
		double& OutVelocity, double& OutAcceleration)
	{
		return FitQuadratic(Samples, Count, T, WindowStart, WindowEnd, [](const FRbStrokeSample& S) { return S.Lateral; },
			OutVelocity, OutAcceleration);
	}

	int32 FirstSampleInWindow(const FRbStrokeSample* Samples, int32 Count, double WindowStart)
	{
		if (!Samples || Count <= 0)
		{
			return 0;
		}
		// The same bound as the fit's filter (Time < WindowStart - kWindowSlack is outside), so the subrange keeps exactly
		// the samples the full-range fit keeps.
		const double Bound = WindowStart - kWindowSlack;
		int32 Lo = 0;
		int32 Hi = Count;
		while (Lo < Hi)
		{
			const int32 Mid = Lo + (Hi - Lo) / 2;
			if (Samples[Mid].Time < Bound)
			{
				Lo = Mid + 1;
			}
			else
			{
				Hi = Mid;
			}
		}
		return Lo;
	}

	bool QuadraticFitVelocity(const FRbStrokeSample* Samples, int32 Count, double T, double Window, double& OutVelocity)
	{
		double Acceleration = 0.0;
		return QuadraticFit(Samples, Count, T, T - Window, T, OutVelocity, Acceleration);
	}

	bool LinearFitVelocity(const FRbStrokeSample* Samples, int32 Count, double T, double Window, double& OutVelocity)
	{
		OutVelocity = 0.0;
		if (!Samples || Count < 2)
		{
			return false;
		}
		const double Scale = FMath::Max(Window, 1.0e-6);
		double N = 0.0, SU = 0.0, SY = 0.0;
		double Origin = 0.0;
		bool bHaveOrigin = false;
		for (int32 i = 0; i < Count; ++i)
		{
			if (Samples[i].Time < T - Window - kWindowSlack || Samples[i].Time > T + kWindowSlack)
			{
				continue;
			}
			if (!bHaveOrigin)
			{
				Origin = Samples[i].Position;
				bHaveOrigin = true;
			}
			N += 1.0;
			SU += (Samples[i].Time - T) / Scale;
			SY += Samples[i].Position - Origin;
		}
		if (N < 2.0)
		{
			return false;
		}
		const double MU = SU / N;
		const double MY = SY / N;
		double SUU = 0.0, SUY = 0.0;
		for (int32 i = 0; i < Count; ++i)
		{
			if (Samples[i].Time < T - Window - kWindowSlack || Samples[i].Time > T + kWindowSlack)
			{
				continue;
			}
			const double DU = (Samples[i].Time - T) / Scale - MU;
			SUU += DU * DU;
			SUY += DU * (Samples[i].Position - Origin - MY);
		}
		if (!(SUU > 0.0))
		{
			return false;
		}
		OutVelocity = SUY / SUU / Scale;
		return true;
	}

	bool FindContactCrossing(const FRbStrokeSample* CuePositions, int32 Count, double& OutTime)
	{
		OutTime = 0.0;
		if (!CuePositions)
		{
			return false;
		}
		for (int32 i = 1; i < Count; ++i)
		{
			const double X0 = CuePositions[i - 1].Position;
			const double X1 = CuePositions[i].Position;
			if (X0 < 0.0 && X1 >= 0.0 && X1 > X0)
			{
				// Same interpolation as FRbCueIntegrator::Step.
				OutTime = CuePositions[i - 1].Time + (-X0) / (X1 - X0) * (CuePositions[i].Time - CuePositions[i - 1].Time);
				return true;
			}
		}
		return false;
	}

	void Steering(double GripLateral, double BridgeToGrip, double BridgeToTip, double& OutYawError, double& OutTipShift)
	{
		// Grip right -> the cue rotates about the bridge -> the tip moves the other way (plan 5.4, human-factors 3.5).
		OutYawError = BridgeToGrip > 0.0 ? GripLateral / BridgeToGrip : 0.0;
		OutTipShift = -OutYawError * BridgeToTip;
	}

	double BridgeHeight(double BallRadius, double Elevation, double BridgeLength)
	{
		const double S = FMath::Sin(Elevation);
		const double ContactHeight = BallRadius + BallRadius * S; // z_P for a centre hit
		return ContactHeight + BridgeLength * S;
	}

	bool MakeScriptedStroke(const FRbScriptedStroke& P, const FRbStrokeGain& Gain, TArray<FRbStrokeSample>& OutSamples,
		double* OutContactTime, double* OutForwardStart)
	{
		OutSamples.Reset();
		if (!(P.SampleRate > 0.0) || !(P.TipSpeed > 0.0) || !(P.ForwardTravel > 0.0) || !(P.StartCueDisplacement < 0.0) ||
			!(P.MaxBackswing > 0.0) || P.BackswingTime < 0.0 || P.Pause < 0.0)
		{
			return false;
		}
		const double TipSpeed = FMath::Min(P.TipSpeed, Gain.VTipMax);
		const double HandSpeed = HandSpeedForCueSpeed(TipSpeed, Gain);
		const double Accel = CueTravelIntegral(HandSpeed, Gain) / P.ForwardTravel;
		if (!(HandSpeed > 0.0) || !(Accel > 0.0))
		{
			return false;
		}
		const double TauStar = HandSpeed / Accel; // forward-stroke time at which the hand reaches HandSpeed
		const double Dt = 1.0 / P.SampleRate;
		const int32 NBack = FMath::Max(1, FMath::RoundToInt(P.BackswingTime * P.SampleRate));
		const int32 NFwd = FMath::CeilToInt((TauStar + P.FollowThrough) * P.SampleRate) + 1;
		const double TBack = P.StartTime + NBack * Dt;
		const double TFwd0 = TBack + P.Pause;

		auto Build = [&](double Backswing, TArray<FRbStrokeSample>& S)
		{
			S.Reset(1 + NBack + NFwd);
			S.Add({P.StartTime, 0.0, 0.0});
			for (int32 k = 1; k <= NBack; ++k)
			{
				S.Add({P.StartTime + k * Dt, -Backswing * static_cast<double>(k) / NBack, 0.0});
			}
			for (int32 k = 1; k <= NFwd; ++k)
			{
				const double Tau = k * Dt;
				S.Add({TFwd0 + Tau, -Backswing + 0.5 * Accel * Tau * Tau, P.LateralDrift * FMath::Min(1.0, Tau / TauStar)});
			}
		};
		TArray<FRbStrokeSample> Work;
		// Forward-stroke time of the crossing for a given backswing, with the component's own integration (live stroke).
		auto CrossingTau = [&](double Backswing, double& OutTc) -> bool
		{
			Build(Backswing, Work);
			FRbCueIntegrator Integrator;
			Integrator.Reset(P.StartCueDisplacement);
			for (const FRbStrokeSample& S : Work)
			{
				const FRbCueStepResult R = Integrator.Step(S, Gain, true, 0.0, P.MaxBackswing);
				if (R.bCrossed)
				{
					OutTc = R.CrossingTime;
					return true;
				}
			}
			return false;
		};

		// f(B) = tau_c(B) - tau*: increasing in the backswing B (a longer backswing crosses later); no crossing = too late.
		auto TooLate = [&](double Backswing) -> bool
		{
			double Tc = 0.0;
			return !CrossingTau(Backswing, Tc) || Tc - TFwd0 >= TauStar;
		};
		double Lo = 0.0;
		if (TooLate(Lo))
		{
			return false; // even without a backswing the stroke reaches the ball too late (ForwardTravel too short)
		}
		double Hi = 0.01;
		while (!TooLate(Hi))
		{
			Hi *= 2.0;
			if (Hi > 4.0)
			{
				return false; // the forward travel does not fit inside MaxBackswing
			}
		}
		for (int32 Iteration = 0; Iteration < 200 && Hi - Lo > 1.0e-15; ++Iteration)
		{
			const double Mid = 0.5 * (Lo + Hi);
			if (Mid <= Lo || Mid >= Hi)
			{
				break;
			}
			(TooLate(Mid) ? Hi : Lo) = Mid;
		}
		double Tc = 0.0;
		if (!CrossingTau(Hi, Tc))
		{
			return false;
		}
		OutSamples = Work;
		if (OutContactTime)
		{
			*OutContactTime = Tc;
		}
		if (OutForwardStart)
		{
			*OutForwardStart = TFwd0;
		}
		return true;
	}
}
