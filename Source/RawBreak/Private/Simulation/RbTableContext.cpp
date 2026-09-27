#include "Simulation/RbTableContext.h"

#include "rb/Core/Error.h"
#include "rb/Shot/ShotRecordBuilder.h"

// Owner: UE-6a.

namespace RbTableContextPrivate
{
	// Bit-level checks (FMath): the game target compiles this module with /fp:fast, where IEEE comparisons with NaN are
	// not reliable.
	bool IsFiniteNumber(double V) { return FMath::IsFinite(V); }

	// A ball the simulator accepts: finite, positive radius and mass, inertia factor k = I / (m R^2) in (0, 2/3]
	// (solid sphere 2/5, thin shell 2/3 - rb/Physics/BallState.h).
	bool IsPhysicalBall(const rb::BallSpec& B)
	{
		if (!IsFiniteNumber(B.Radius) || !IsFiniteNumber(B.Mass) || !IsFiniteNumber(B.Inertia) || B.Radius <= 0.0 || B.Mass <= 0.0 || B.Inertia <= 0.0)
		{
			return false;
		}
		const double K = rb::InertiaFactor(B);
		return K > 0.0 && K <= 2.0 / 3.0 + 1e-12;
	}

	// Aabb2 with Lo <= Hi on both axes; bounds may be infinite (the default "everywhere" footprint), never NaN.
	bool IsValidFootprint(const rb::Aabb2& A)
	{
		const double V[4] = {A.Lo.x, A.Lo.y, A.Hi.x, A.Hi.y};
		for (double X : V)
		{
			if (FMath::IsNaN(X))
			{
				return false;
			}
		}
		return A.Lo.x <= A.Hi.x && A.Lo.y <= A.Hi.y;
	}
}

TSharedPtr<const FRbTableContext> FRbTableContext::Create(const FRbTableSetup& Setup, FString& OutError)
{
	using namespace RbTableContextPrivate;
	TSharedPtr<FRbTableContext> Ctx = MakeShared<FRbTableContext>();
	Ctx->Setup = Setup;
	Ctx->Spec = rb::GetTableSpec(RbTypes::ToCore(Setup.Table));

	const rb::ErrorCode GeoError = rb::BuildTableGeometry(Ctx->Spec, Ctx->Geometry);
	if (!rb::Succeeded(GeoError))
	{
		OutError = FString::Printf(TEXT("BuildTableGeometry failed: %hs"), rb::ToString(GeoError));
		return nullptr;
	}

	// Single source of truth for per-table physics (architecture 13.3), validated with the venue's condition applied:
	// a too steep slope or a non-positive cling factor would otherwise only surface as InvalidInput on every shot.
	Ctx->Physics = rb::MakePhysicsParams(Ctx->Spec, Setup.Condition);
	const rb::ErrorCode ParamError = rb::ValidatePhysicsParams(Ctx->Physics);
	if (!rb::Succeeded(ParamError))
	{
		OutError = FString::Printf(TEXT("Physics parameters of the table condition rejected: %hs"), rb::ToString(ParamError));
		return nullptr;
	}

	if (FMath::IsNaN(Setup.LampUndersideZ) || Setup.LampUndersideZ <= 0.0)
	{
		OutError = FString::Printf(TEXT("Lamp underside height %g m above the cloth is invalid (> 0, or infinity for no lamp)"), Setup.LampUndersideZ);
		return nullptr;
	}
	if (!IsValidFootprint(Setup.LampFootprint))
	{
		OutError = TEXT("Lamp footprint is invalid (NaN or Lo > Hi)");
		return nullptr;
	}
	Ctx->Environment.LampUndersideZ = Setup.LampUndersideZ;
	Ctx->Environment.LampFootprint = Setup.LampFootprint;

	const rb::ErrorCode BallError = rb::BuildBallSet(RbTypes::ToCore(Setup.BallSet), Setup.BallSetSeed, Ctx->Balls);
	if (!rb::Succeeded(BallError))
	{
		OutError = FString::Printf(TEXT("BuildBallSet failed: %hs"), rb::ToString(BallError));
		return nullptr;
	}
	if (Ctx->Balls.Count < 2 || Ctx->Balls.Count > rb::kMaxBalls)
	{
		OutError = FString::Printf(TEXT("Ball set has %d balls (2..%d required)"), Ctx->Balls.Count, rb::kMaxBalls);
		return nullptr;
	}
	for (int32 Id = 0; Id < Ctx->Balls.Count; ++Id)
	{
		if (!IsPhysicalBall(Ctx->Balls.Balls[Id]))
		{
			OutError = FString::Printf(TEXT("Ball %d of the set is not physical (R %g m, m %g kg, I %g kg m^2)"), Id, Ctx->Balls.Balls[Id].Radius,
				Ctx->Balls.Balls[Id].Mass, Ctx->Balls.Balls[Id].Inertia);
			return nullptr;
		}
	}

	// The rules' table from the single geometry source, exactly as rbsim builds it (landmarks, pocket openings,
	// per-ball radii; nominal = object ball 1).
	double Radii[rb::kMaxBalls] = {};
	for (int32 Id = 0; Id < Ctx->Balls.Count; ++Id)
	{
		Radii[Id] = Ctx->Balls.Balls[Id].Radius;
	}
	Ctx->RulesTable = rb::BuildRulesTable(Ctx->Geometry, Ctx->Balls.Balls[1].Radius, Radii, Ctx->Balls.Count);
	return Ctx;
}

double FRbTableContext::BallRadius(int32 Id) const
{
	return (Id >= 0 && Id < Balls.Count) ? Balls.Balls[Id].Radius : rb::kDefaultBallRadius;
}
