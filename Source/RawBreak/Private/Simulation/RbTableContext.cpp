#include "Simulation/RbTableContext.h"

#include "rb/Core/Error.h"
#include "rb/Shot/ShotRecordBuilder.h"

// Owner: UE-6a. TODO(UE-6a): validate the ball set, lamp footprint from the room, per-venue conditions; tests.

TSharedPtr<const FRbTableContext> FRbTableContext::Create(const FRbTableSetup& Setup, FString& OutError)
{
	TSharedPtr<FRbTableContext> Ctx = MakeShared<FRbTableContext>();
	Ctx->Setup = Setup;
	Ctx->Spec = rb::GetTableSpec(RbTypes::ToCore(Setup.Table));

	const rb::ErrorCode GeoError = rb::BuildTableGeometry(Ctx->Spec, Ctx->Geometry);
	if (!rb::Succeeded(GeoError))
	{
		OutError = FString::Printf(TEXT("BuildTableGeometry failed: %hs"), rb::ToString(GeoError));
		return nullptr;
	}

	Ctx->Physics = rb::MakePhysicsParams(Ctx->Spec, Setup.Condition);
	Ctx->Environment.LampUndersideZ = Setup.LampUndersideZ;

	const rb::ErrorCode BallError = rb::BuildBallSet(RbTypes::ToCore(Setup.BallSet), Setup.BallSetSeed, Ctx->Balls);
	if (!rb::Succeeded(BallError))
	{
		OutError = FString::Printf(TEXT("BuildBallSet failed: %hs"), rb::ToString(BallError));
		return nullptr;
	}

	double Radii[rb::kMaxBalls] = {};
	for (int32 Id = 0; Id < Ctx->Balls.Count && Id < rb::kMaxBalls; ++Id)
	{
		Radii[Id] = Ctx->Balls.Balls[Id].Radius;
	}
	const double Nominal = Ctx->Balls.Count > 1 ? Ctx->Balls.Balls[1].Radius : rb::kDefaultBallRadius;
	Ctx->RulesTable = rb::BuildRulesTable(Ctx->Geometry, Nominal, Radii, Ctx->Balls.Count);
	return Ctx;
}

double FRbTableContext::BallRadius(int32 Id) const
{
	return (Id >= 0 && Id < Balls.Count) ? Balls.Balls[Id].Radius : rb::kDefaultBallRadius;
}
