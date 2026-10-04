#include "Venue/RbVenueTableCheck.h"

#include "Simulation/RbShot.h"
#include "Simulation/RbTableContext.h"

#include "rb/Human/Venue.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"

// Owner: M2-A.

namespace RbVenueTableCheckPrivate
{
	struct FRoll
	{
		bool bOk = false;
		FVector2d Travel = FVector2d::ZeroVector; // end - start [m]
	};

	FRoll Roll(const FRbTableContext& Context, const FVector2d& Start, double VelocityX)
	{
		FRoll Out;
		rb::SimInput Input;
		RbShot::InitSimInput(Context, Input);
		Input.Record.Trajectories = false;
		Input.Record.EventStates = false;
		Input.Record.LogTransitions = false;
		Input.Record.LogObservers = false;
		Input.Record.ShotRecord = false;
		rb::SimBall& Ball = Input.Balls[rb::kCueBallId];
		const double R = Ball.Spec.Radius;
		Ball.InPlay = true;
		Ball.State.Position = rb::Vec3(Start.X, Start.Y, R);
		Ball.State.Velocity = rb::Vec3(VelocityX, 0.0, 0.0);
		Ball.State.Omega = rb::Vec3(0.0, VelocityX / R, 0.0); // rolling without slip along x: w = (-v_y / R, v_x / R, 0)
		Ball.State.State = rb::MotionState::Rolling;
		rb::Simulator Simulator;
		rb::ShotResult Result;
		const rb::SimStatus Status = Simulator.Run(Input, Result);
		if (Status != rb::SimStatus::Ok || Result.Finals[rb::kCueBallId].Status != rb::BallFinalStatus::OnTable)
		{
			return Out;
		}
		const rb::Vec3 End = Result.Finals[rb::kCueBallId].State.Position;
		Out.Travel = FVector2d(End.x - Start.X, End.y - Start.Y);
		Out.bOk = true;
		return Out;
	}

	double WrapDeg(double A)
	{
		A = FMath::Fmod(A + 180.0, 360.0);
		if (A < 0.0)
		{
			A += 360.0;
		}
		return A - 180.0;
	}
}

FString FRbRollOffCheck::ToString() const
{
	if (!bValid)
	{
		return FString::Printf(TEXT("roll-off check invalid: %s"), *Error);
	}
	return FString::Printf(TEXT("slope %.2f mm/m, downhill azimuth %.1f deg (error %.1f deg, sign %+d), roll toward the foot %.3f m / drift %+.4f m, toward the head %.3f m / drift %+.4f m -> %s"),
		SlopeMmPerM, DownhillAzimuthDeg, AzimuthErrorDeg, SlopeSign, TowardFootTravel, TowardFootDriftY, TowardHeadTravel, TowardHeadDriftY, bPass ? TEXT("PASS") : TEXT("FAIL"));
}

namespace RbVenueTableCheck
{
	FRbRollOffCheck CheckRollOff(ERbTablePreset Preset, ERbBallSetPreset BallSet, ERbVenueKind Kind, int64 VenueSeed, int32 TableIndex, bool bFirstCareerTable,
		double TargetAzimuthDeg, double ToleranceDeg)
	{
		using namespace RbVenueTableCheckPrivate;
		FRbRollOffCheck Check;
		const uint64 Seed = static_cast<uint64>(VenueSeed);
		FRbTableSetup Setup;
		Setup.Table = Preset;
		Setup.BallSet = BallSet;
		Setup.BallSetSeed = rb::human::VenueBallSetSeed(Seed, TableIndex);
		Setup.Condition = rb::human::MakeVenueTableCondition(Seed, TableIndex, RbTypes::ToCore(Kind), bFirstCareerTable, false);
		FString Error;
		const TSharedPtr<const FRbTableContext> Context = FRbTableContext::Create(Setup, Error);
		if (!Context.IsValid())
		{
			Check.Error = Error;
			return Check;
		}
		const double HeadSpotX = -0.25 * Context->Spec.Length;
		const FRoll ToFoot = Roll(*Context, FVector2d(HeadSpotX, 0.0), RollSpeed);
		const FRoll ToHead = Roll(*Context, FVector2d(-HeadSpotX, 0.0), -RollSpeed);
		if (!ToFoot.bOk || !ToHead.bOk)
		{
			Check.Error = TEXT("a roll did not end on the table");
			return Check;
		}
		Check.bValid = true;
		const rb::Vec2 Slope = Setup.Condition.Slope;
		Check.SlopeMmPerM = 1000.0 * FMath::Sqrt(Slope.x * Slope.x + Slope.y * Slope.y);
		Check.TowardFootTravel = ToFoot.Travel.X;
		Check.TowardHeadTravel = -ToHead.Travel.X;
		Check.TowardFootDriftY = ToFoot.Travel.Y;
		Check.TowardHeadDriftY = ToHead.Travel.Y;

		// Behaviour: the lateral drift has the sign of downhill y; the downhill roll along x goes farther.
		const double DownhillY = 0.5 * (ToFoot.Travel.Y + ToHead.Travel.Y);
		const double DownhillX = Check.TowardFootTravel - Check.TowardHeadTravel; // > 0: the foot is downhill
		// Sign convention of the slope vector, proved by the component the rolls see best.
		const bool bUseY = FMath::Abs(Slope.y) * 1.0 >= FMath::Abs(Slope.x) * 0.25;
		const double Evidence = bUseY ? DownhillY * Slope.y : DownhillX * Slope.x;
		Check.SlopeSign = Evidence >= 0.0 ? 1 : -1;
		const double DX = Check.SlopeSign * Slope.x, DY = Check.SlopeSign * Slope.y;
		Check.DownhillAzimuthDeg = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));
		Check.AzimuthErrorDeg = FMath::Abs(WrapDeg(Check.DownhillAzimuthDeg - TargetAzimuthDeg));

		// The target's own behaviour signs (the Low Bridge: toward core -y and toward the head, so both rolls drift -y and the roll
		// toward the foot comes up shorter).
		const double TargetRad = FMath::DegreesToRadians(TargetAzimuthDeg);
		const double TY = FMath::Sin(TargetRad), TX = FMath::Cos(TargetRad);
		const bool bDriftOk = (TY < 0.0) ? (ToFoot.Travel.Y < 0.0 && ToHead.Travel.Y < 0.0) : (ToFoot.Travel.Y > 0.0 && ToHead.Travel.Y > 0.0);
		const bool bLengthOk = (TX < 0.0) ? (Check.TowardFootTravel < Check.TowardHeadTravel) : (Check.TowardFootTravel > Check.TowardHeadTravel);
		Check.bPass = bDriftOk && bLengthOk && Check.AzimuthErrorDeg <= ToleranceDeg;
		return Check;
	}

	int64 FindRollOffSeed(ERbTablePreset Preset, ERbBallSetPreset BallSet, ERbVenueKind Kind, int32 TableIndex, int64 StartSeed, int32 MaxTries,
		double TargetAzimuthDeg, double ToleranceDeg)
	{
		for (int32 Try = 0; Try < MaxTries; ++Try)
		{
			const int64 Seed = StartSeed + Try;
			const FRbRollOffCheck A = CheckRollOff(Preset, BallSet, Kind, Seed, TableIndex, false, TargetAzimuthDeg, ToleranceDeg);
			if (!A.bPass)
			{
				continue;
			}
			const FRbRollOffCheck B = CheckRollOff(Preset, BallSet, Kind, Seed, TableIndex, true, TargetAzimuthDeg, ToleranceDeg);
			if (B.bPass)
			{
				return Seed;
			}
		}
		return -1;
	}
}
