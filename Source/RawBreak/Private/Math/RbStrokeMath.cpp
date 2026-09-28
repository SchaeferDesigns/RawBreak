#include "Math/RbStrokeMath.h"

// TODO(UE-5a): implement every function and the tests T9-T12 (Private/Tests/RbStrokeMathTests.cpp).

namespace RbStrokeMath
{
	double CountsToMeters(double Counts, double Dpi)
	{
		return Dpi > 0.0 ? Counts / Dpi * 0.0254 : 0.0;
	}

	double Gain(double /*HandSpeed*/, const FRbStrokeGain& Params)
	{
		return Params.G0; // TODO(UE-5a)
	}

	double CueSpeedFromHandSpeed(double HandSpeed, const FRbStrokeGain& Params)
	{
		return FMath::Clamp(Gain(FMath::Abs(HandSpeed), Params) * HandSpeed, -Params.VTipMax, Params.VTipMax); // TODO(UE-5a) verify vs T10
	}

	bool QuadraticFitVelocity(const FRbStrokeSample* /*Samples*/, int32 /*Count*/, double /*T*/, double /*Window*/, double& OutVelocity)
	{
		OutVelocity = 0.0;
		return false; // TODO(UE-5a)
	}

	bool LinearFitVelocity(const FRbStrokeSample* /*Samples*/, int32 /*Count*/, double /*T*/, double /*Window*/, double& OutVelocity)
	{
		OutVelocity = 0.0;
		return false; // TODO(UE-5a)
	}

	bool FindContactCrossing(const FRbStrokeSample* /*CuePositions*/, int32 /*Count*/, double& OutTime)
	{
		OutTime = 0.0;
		return false; // TODO(UE-5a)
	}

	void Steering(double GripLateral, double BridgeToGrip, double BridgeToTip, double& OutYawError, double& OutTipShift)
	{
		OutYawError = BridgeToGrip > 0.0 ? GripLateral / BridgeToGrip : 0.0;
		OutTipShift = -OutYawError * BridgeToTip; // TODO(UE-5a) verify vs T11
	}

	double BridgeHeight(double BallRadius, double Elevation, double BridgeLength)
	{
		const double S = FMath::Sin(Elevation);
		return BallRadius + BallRadius * S + BridgeLength * S; // TODO(UE-5a) verify vs T12
	}
}
