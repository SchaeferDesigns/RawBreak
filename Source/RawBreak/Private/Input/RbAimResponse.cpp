#include "Input/RbAimResponse.h"

// Owner: M2-F.

namespace RbAimResponse
{
	double CountsToCm(double Counts, double Dpi)
	{
		return Dpi > 0.0 ? Counts * 2.54 / Dpi : 0.0;
	}

	double AccelerationGain(double /*HandSpeedCmPerSecond*/, double /*Acceleration*/)
	{
		return 1.0; // TODO(M2-F): the acceleration curve (header)
	}

	double AimRadians(double Counts, double DeltaSeconds, bool bFine, double Dpi, const FRbControlSettings& Settings)
	{
		const double Cm = CountsToCm(Counts, Dpi);
		const double Speed = DeltaSeconds > 0.0 ? FMath::Abs(Cm) / DeltaSeconds : 0.0;
		const double Degrees = Cm * Settings.AimDegreesPerCm * Settings.AimSensitivity * (bFine ? Settings.FineAimFactor : 1.0)
			* AccelerationGain(Speed, Settings.AimAcceleration);
		return FMath::DegreesToRadians(Degrees);
	}

	double LookDegrees(double Counts, double Dpi, const FRbControlSettings& Settings)
	{
		return CountsToCm(Counts, Dpi) * Settings.LookDegreesPerCm * Settings.LookSensitivity;
	}
}
