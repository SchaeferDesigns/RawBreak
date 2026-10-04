#include "Input/RbAimResponse.h"

// Owner: M2-F. The mapping of the header; tests RawBreak.Unit.Feel.F3_* / F4_* (Private/Tests/RbFeelTests.cpp).

namespace RbAimResponse
{
	double CountsToCm(double Counts, double Dpi)
	{
		return Dpi > 0.0 ? Counts * 2.54 / Dpi : 0.0;
	}

	double AccelerationGain(double HandSpeedCmPerSecond, double Acceleration)
	{
		const double A = FMath::Clamp(Acceleration, 0.0, 1.0);
		if (A <= 0.0)
		{
			return 1.0;
		}
		const double V = FMath::Max(0.0, HandSpeedCmPerSecond);
		const double G = FMath::Clamp(FMath::Pow(V / ReferenceSpeedCmPerSecond, AccelerationExponent), MinAccelerationGain, MaxAccelerationGain);
		return 1.0 + A * (G - 1.0);
	}

	double RadiansPerCount(bool bFine, double Dpi, const FRbControlSettings& Settings)
	{
		const double Degrees = CountsToCm(1.0, Dpi) * static_cast<double>(Settings.AimDegreesPerCm) * static_cast<double>(Settings.AimSensitivity) *
			(bFine ? static_cast<double>(Settings.FineAimFactor) : 1.0);
		return FMath::DegreesToRadians(Degrees);
	}

	double AimRadians(double Counts, double DeltaSeconds, bool bFine, double Dpi, const FRbControlSettings& Settings)
	{
		const double Linear = Counts * RadiansPerCount(bFine, Dpi, Settings);
		if (Settings.AimAcceleration <= 0.0f)
		{
			return Linear;
		}
		const double Speed = DeltaSeconds > 0.0 ? FMath::Abs(CountsToCm(Counts, Dpi)) / DeltaSeconds : ReferenceSpeedCmPerSecond;
		return Linear * AccelerationGain(Speed, Settings.AimAcceleration);
	}

	double LookDegrees(double Counts, double Dpi, const FRbControlSettings& Settings)
	{
		return CountsToCm(Counts, Dpi) * static_cast<double>(Settings.LookDegreesPerCm) * static_cast<double>(Settings.LookSensitivity);
	}

	double CmForAimDegrees(double Degrees, const FRbControlSettings& Settings)
	{
		const double PerCm = static_cast<double>(Settings.AimDegreesPerCm) * static_cast<double>(Settings.AimSensitivity);
		return PerCm > 0.0 ? Degrees / PerCm : 0.0;
	}
}
