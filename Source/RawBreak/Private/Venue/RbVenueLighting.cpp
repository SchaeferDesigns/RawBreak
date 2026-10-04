#include "Venue/RbVenueLighting.h"

#include "Math/RandomStream.h"

// Owner: M2-A.

namespace RbVenueLightingPrivate
{
	uint32 Mix(uint32 A, uint32 B)
	{
		uint32 H = A * 0x9E3779B1u ^ (B + 0x7F4A7C15u + (A << 6) + (A >> 2));
		H ^= H >> 16;
		H *= 0x85EBCA6Bu;
		H ^= H >> 13;
		H *= 0xC2B2AE35u;
		H ^= H >> 16;
		return H;
	}

	double Unit(uint32 Seed, uint32 Index, uint32 Salt)
	{
		return static_cast<double>(Mix(Mix(Seed, Index), Salt) & 0xFFFFFFu) / static_cast<double>(0x1000000u);
	}

	double Smooth(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return X * X * (3.0 - 2.0 * X);
	}

	// Band-limited noise in [-1, 1]: three incommensurate sines at <= Rate Hz with seeded phases (no step, no flash).
	double TvNoise(int32 Seed, double Rate, double T)
	{
		const double R = FMath::Max(Rate, 0.01);
		const double F1 = R, F2 = R * 0.618034, F3 = R * 0.381966;
		const double P1 = 2.0 * UE_DOUBLE_PI * Unit(Seed, 1, 11), P2 = 2.0 * UE_DOUBLE_PI * Unit(Seed, 2, 11), P3 = 2.0 * UE_DOUBLE_PI * Unit(Seed, 3, 11);
		const double S = 0.5 * FMath::Sin(2.0 * UE_DOUBLE_PI * F1 * T + P1) + 0.3 * FMath::Sin(2.0 * UE_DOUBLE_PI * F2 * T + P2) +
			0.2 * FMath::Sin(2.0 * UE_DOUBLE_PI * F3 * T + P3);
		return FMath::Clamp(S, -1.0, 1.0);
	}

	struct FSweep
	{
		double Start = 0.0;
		double Duration = 0.0;
		double YawFrom = 0.0;
		double YawTo = 0.0;
	};

	// The sweep at or before T (sweeps follow each other with seeded gaps and durations).
	bool FindSweep(const FRbVenueLight& Light, double T, FSweep& Out)
	{
		const double MinGap = FMath::Max(1.0, Light.IntervalRange.X);
		const double MaxGap = FMath::Max(MinGap, Light.IntervalRange.Y);
		const double MinSweep = FMath::Max(0.5, Light.SweepRange.X);
		const double MaxSweep = FMath::Max(MinSweep, Light.SweepRange.Y);
		double Start = MinGap * 0.5 + (MaxGap - MinGap) * 0.25 * Unit(Light.AnimSeed, 0, 5); // first car comes early
		for (uint32 K = 0; K < 100000; ++K)
		{
			const double Duration = MinSweep + (MaxSweep - MinSweep) * Unit(Light.AnimSeed, K, 7);
			if (T < Start)
			{
				return false;
			}
			if (T <= Start + Duration)
			{
				const bool bLeftToRight = Unit(Light.AnimSeed, K, 13) < 0.5;
				Out.Start = Start;
				Out.Duration = Duration;
				Out.YawFrom = bLeftToRight ? Light.YawRange.X : Light.YawRange.Y;
				Out.YawTo = bLeftToRight ? Light.YawRange.Y : Light.YawRange.X;
				return true;
			}
			Start += Duration + MinGap + (MaxGap - MinGap) * Unit(Light.AnimSeed, K, 3);
		}
		return false;
	}
}

float FRbVenueLight::FactorFor(uint8 State) const
{
	switch (State)
	{
	case 0: return OpenFactor;
	case 1: return LightsUpFactor;
	case 2: return AfterHoursFactor;
	default: return OpenFactor;
	}
}

void FRbLightRamp::Start(float Target, float Seconds)
{
	if (FMath::IsNearlyEqual(Current, Target) && !IsActive())
	{
		To = Target;
		return;
	}
	From = Current;
	To = Target;
	Time = 0.0f;
	Duration = FMath::Max(Seconds, RbVenueLighting::MinRampSeconds);
}

void FRbLightRamp::Snap(float Value)
{
	From = To = Current = Value;
	Time = Duration = 0.0f;
}

float FRbLightRamp::Advance(float Dt)
{
	if (Duration <= 0.0f)
	{
		Current = To;
		return Current;
	}
	Time += FMath::Max(0.0f, Dt);
	if (Time >= Duration)
	{
		Current = To;
		Duration = 0.0f;
		Time = 0.0f;
		return Current;
	}
	Current = FMath::Lerp(From, To, static_cast<float>(RbVenueLighting::Ramp01(Time, Duration)));
	return Current;
}

double FRbLampModel::I0() const
{
	const double CosC = FMath::Cos(FMath::DegreesToRadians(CutoffDeg));
	return Efficiency * FluxLm / (UE_DOUBLE_PI * FMath::Max(1e-9, 1.0 - CosC * CosC));
}

double FRbLampModel::IlluminanceAt(const FVector3d& CorePoint, const FVector3d& Normal) const
{
	const double Intensity0 = I0();
	const double CosCut = FMath::Cos(FMath::DegreesToRadians(CutoffDeg));
	double E = 0.0;
	for (const FVector3d& Bulb : BulbsCore)
	{
		const FVector3d D = Bulb - CorePoint;
		const double Dist = D.Length();
		if (Dist < 1e-6)
		{
			continue;
		}
		const double CosTheta = (Bulb.Z - CorePoint.Z) / Dist; // angle from the downward vertical at the bulb
		if (CosTheta < CosCut)
		{
			continue;
		}
		const double CosIncidence = FMath::Max(0.0, FVector3d::DotProduct(Normal.GetSafeNormal(), D / Dist));
		E += Intensity0 * CosTheta * CosIncidence / (Dist * Dist);
	}
	return E;
}

namespace RbVenueLighting
{
	FName LightTag(const FName& Id)
	{
		return FName(*FString::Printf(TEXT("LT_DB_%s"), *Id.ToString()));
	}

	FName VenueLightTag()
	{
		return FName(TEXT("RbDB_Light"));
	}

	double Luminance(const FLinearColor& LinearColor)
	{
		return 0.2126 * LinearColor.R + 0.7152 * LinearColor.G + 0.0722 * LinearColor.B;
	}

	double Ramp01(double T, double Duration)
	{
		if (Duration <= 0.0)
		{
			return 1.0;
		}
		return RbVenueLightingPrivate::Smooth(T / Duration);
	}

	double AnimationFactor(const FRbVenueLight& Light, double T)
	{
		using namespace RbVenueLightingPrivate;
		switch (Light.Animation)
		{
		case ERbVenueLightAnimation::Tv:
			return FMath::Max(0.0, 1.0 + Light.AnimDepth * TvNoise(Light.AnimSeed, Light.AnimRate, T));
		case ERbVenueLightAnimation::Chase:
		{
			// Steps at AnimRate / s; the marquee's total luminance alternates by AnimDepth (< 10 %) with a 60 ms LED fade.
			const double Rate = FMath::Clamp(static_cast<double>(Light.AnimRate), 0.1, 2.0);
			const double Phase = T * Rate;
			const double Step = FMath::FloorToDouble(Phase);
			const double Frac = Phase - Step;
			// time since the step [s] = Frac / Rate (a step lasts 1 / Rate s); the LED fade takes ChaseFadeSeconds at every rate
			const double Fade = Smooth(Frac / Rate / ChaseFadeSeconds);
			const double Prev = FMath::Fmod(Step + 1.0, 2.0);
			const double Curr = FMath::Fmod(Step, 2.0);
			const double Pattern = FMath::Lerp(Prev, Curr, Fade);
			return 1.0 - 0.5 * Light.AnimDepth + Light.AnimDepth * Pattern;
		}
		case ERbVenueLightAnimation::Headlights:
		{
			FSweep Sweep;
			if (!FindSweep(Light, T, Sweep))
			{
				return 0.0;
			}
			const double U = (T - Sweep.Start) / Sweep.Duration;
			const double Envelope = FMath::Square(FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(U, 0.0, 1.0)));
			return Envelope;
		}
		case ERbVenueLightAnimation::Cycle:
		case ERbVenueLightAnimation::None:
		default:
			return 1.0;
		}
	}

	FLinearColor AnimationColor(const FRbVenueLight& Light, double T)
	{
		if (Light.Animation != ERbVenueLightAnimation::Cycle || Light.CycleColors.Num() == 0)
		{
			return FLinearColor::White;
		}
		const int32 N = Light.CycleColors.Num();
		const double Period = FMath::Max(4.0, static_cast<double>(Light.AnimRate)); // >= 4 s (4.2 L18)
		const double Phase = FMath::Fmod(T / Period, 1.0) * N;
		const int32 I = FMath::FloorToInt(Phase) % N;
		const double F = RbVenueLightingPrivate::Smooth(Phase - FMath::FloorToDouble(Phase));
		return FMath::Lerp(Light.CycleColors[I], Light.CycleColors[(I + 1) % N], static_cast<float>(F));
	}

	double HeadlightYaw(const FRbVenueLight& Light, double T, bool* bOutActive)
	{
		RbVenueLightingPrivate::FSweep Sweep;
		const bool bActive = Light.Animation == ERbVenueLightAnimation::Headlights && RbVenueLightingPrivate::FindSweep(Light, T, Sweep);
		if (bOutActive)
		{
			*bOutActive = bActive;
		}
		if (!bActive)
		{
			return 0.0;
		}
		const double U = RbVenueLightingPrivate::Smooth((T - Sweep.Start) / Sweep.Duration);
		return FMath::Lerp(Sweep.YawFrom, Sweep.YawTo, U);
	}

	double MaxFlashesPerSecondOf(const TArray<double>& Luminance, double SampleRateHz, double Threshold)
	{
		if (Luminance.Num() < 2 || SampleRateHz <= 0.0)
		{
			return 0.0;
		}
		double MaxL = 0.0;
		for (const double L : Luminance)
		{
			MaxL = FMath::Max(MaxL, L);
		}
		if (MaxL <= 0.0)
		{
			return 0.0;
		}
		const double Delta = Threshold * MaxL;
		// Transitions: moves of >= Delta away from the running extremum of the current direction, alternating (hysteresis).
		// Before the first transition both extremes are tracked (the sequence may start rising or falling).
		TArray<double> Times;
		double Hi = Luminance[0], Lo = Luminance[0];
		double Extremum = Luminance[0];
		int32 Direction = 0; // +1 rising (Extremum = running maximum), -1 falling (running minimum), 0 unknown
		for (int32 I = 1; I < Luminance.Num(); ++I)
		{
			const double L = Luminance[I];
			if (Direction == 0)
			{
				Hi = FMath::Max(Hi, L);
				Lo = FMath::Min(Lo, L);
				if (Hi - L >= Delta)
				{
					Times.Add(I / SampleRateHz);
					Direction = -1;
					Extremum = L;
				}
				else if (L - Lo >= Delta)
				{
					Times.Add(I / SampleRateHz);
					Direction = 1;
					Extremum = L;
				}
			}
			else if (Direction > 0)
			{
				Extremum = FMath::Max(Extremum, L);
				if (Extremum - L >= Delta)
				{
					Times.Add(I / SampleRateHz);
					Direction = -1;
					Extremum = L;
				}
			}
			else
			{
				Extremum = FMath::Min(Extremum, L);
				if (L - Extremum >= Delta)
				{
					Times.Add(I / SampleRateHz);
					Direction = 1;
					Extremum = L;
				}
			}
		}
		// Largest number of flashes (pairs of opposing transitions) inside a 1-s window.
		double Best = 0.0;
		int32 J = 0;
		for (int32 I = 0; I < Times.Num(); ++I)
		{
			while (Times[I] - Times[J] > 1.0)
			{
				++J;
			}
			Best = FMath::Max(Best, FMath::FloorToDouble((I - J + 1) / 2.0));
		}
		return Best;
	}

	double TubeFlux(double LuminanceNits, double DiameterM, double LengthM)
	{
		return UE_DOUBLE_PI * UE_DOUBLE_PI * LuminanceNits * DiameterM * LengthM;
	}

	double NeonProxyFlux(double TubeFluxLm)
	{
		return TubeFluxLm / UE_DOUBLE_PI;
	}

	TArray<FRbLuxPoint> LuxBandPoints(double HalfLength, double HalfWidth, double RailWidth, double RailTopZ)
	{
		const double L = HalfLength, W = HalfWidth, R = 0.5 * RailWidth, Z = RailTopZ;
		TArray<FRbLuxPoint> Points;
		auto Add = [&Points](const TCHAR* Name, double X, double Y, double PZ, double Lo, double Hi)
		{
			FRbLuxPoint P;
			P.Name = Name;
			P.Core = FVector3d(X, Y, PZ);
			P.BandMin = Lo;
			P.BandMax = Hi;
			Points.Add(P);
		};
		Add(TEXT("bed centre"), 0.0, 0.0, 0.0, 750.0, 910.0);
		Add(TEXT("head spot"), -0.5 * L, 0.0, 0.0, 600.0, 730.0);
		Add(TEXT("foot spot"), 0.5 * L, 0.0, 0.0, 600.0, 730.0);
		for (const double SY : {-1.0, 1.0})
		{
			Add(SY < 0 ? TEXT("side pocket nose (-y)") : TEXT("side pocket nose (+y)"), 0.0, SY * W, 0.0, 490.0, 610.0);
			Add(SY < 0 ? TEXT("rail cap side mid (-y)") : TEXT("rail cap side mid (+y)"), 0.0, SY * (W + R), Z, 450.0, 550.0);
		}
		for (const double SX : {-1.0, 1.0})
		{
			Add(SX < 0 ? TEXT("rail cap end centre (head)") : TEXT("rail cap end centre (foot)"), SX * (L + R), 0.0, Z, 220.0, 300.0);
			for (const double SY : {-1.0, 1.0})
			{
				Add(TEXT("corner pocket nose"), SX * L, SY * W, 0.0, 170.0, 250.0);
				Add(TEXT("rail cap at a corner"), SX * (L + R), SY * (W + R), Z, 95.0, 140.0);
			}
		}
		return Points;
	}

	double SceneEv100(double AverageLuminance)
	{
		return FMath::Log2(FMath::Max(1e-9, 8.0 * AverageLuminance));
	}

	double CameraEv100(double Exposure)
	{
		return FMath::Log2(1.0 / FMath::Max(1e-12, Exposure));
	}

	const TArray<FVector2D>& NightCompensationKeys()
	{
		// x: metered scene EV100, y: compensation [EV] (ESTIMATE, tuned on the V01 / V02 / V03 captures against night bar photos)
		static const TArray<FVector2D> Keys = {
			{2.0, -1.10}, {3.0, -1.00}, {4.0, -0.80}, {5.0, -0.55}, {6.0, -0.30}, {7.0, -0.10}, {8.0, 0.0}};
		return Keys;
	}

	double NightCompensation(double SceneEv)
	{
		const TArray<FVector2D>& K = NightCompensationKeys();
		if (SceneEv <= K[0].X)
		{
			return K[0].Y;
		}
		for (int32 I = 1; I < K.Num(); ++I)
		{
			if (SceneEv <= K[I].X)
			{
				const double T = (SceneEv - K[I - 1].X) / (K[I].X - K[I - 1].X);
				return FMath::Lerp(K[I - 1].Y, K[I].Y, T);
			}
		}
		return K.Last().Y;
	}

	double UeCurveX(double SceneEv)
	{
		// log2(L / 0.18) = log2(8 L) - log2(8 x 0.18)
		return SceneEv - FMath::Log2(8.0 * 0.18);
	}
}
