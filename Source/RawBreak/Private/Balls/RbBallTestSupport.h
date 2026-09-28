#pragma once

// Shared fixtures of the UE-2 automation tests (Private/Tests/RbBallTests.cpp, RbPlaybackTests.cpp): a transient game
// world, a (translated + yawed) table with its ball set, the reference 2-ball shot and a transient MPC_RbBalls stand-in.
// Owner: UE-2. Test-only (WITH_DEV_AUTOMATION_TESTS).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Balls/RbBallRackDemo.h"
#include "Balls/RbBallSet.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbTypes.h"
#include "Simulation/RbShot.h"
#include "Table/RbTable.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/Package.h"

#include "rb/Equipment/Cue.h"
#include "rb/Physics/Playback.h"

#include <bit>

namespace RbBallTest
{
	// A game world for unit tests (no BeginPlay, never ticked: tests drive the components themselves).
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("RbBallsTestWorld")); // rooted until the destructor
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}

		~FTestWorld()
		{
			World->RemoveFromRoot();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		FTestWorld(const FTestWorld&) = delete;
		FTestWorld& operator=(const FTestWorld&) = delete;
	};

	struct FTableWithBalls
	{
		ARbTable* Table = nullptr;
		ARbBallSet* Balls = nullptr;
	};

	// Table of the given presets placed at Location with Yaw [deg] (a non-trivial table-to-world transform), plus its
	// initialised ball set.
	inline FTableWithBalls SpawnTableWithBalls(UWorld* World, ERbTablePreset Preset = ERbTablePreset::NineFootPro,
		ERbBallSetPreset BallSetPreset = ERbBallSetPreset::StandardPool, int64 BallSetSeed = 0,
		const FVector& Location = FVector(123.0, -45.0, 7.0), double YawDeg = 30.0)
	{
		FTableWithBalls Out;
		const FTransform Transform(FRotator(0.0, YawDeg, 0.0), Location);
		Out.Table = World->SpawnActorDeferred<ARbTable>(ARbTable::StaticClass(), Transform);
		if (!Out.Table)
		{
			return Out;
		}
		Out.Table->Preset = Preset;
		Out.Table->BallSet = BallSetPreset;
		Out.Table->BallSetSeed = BallSetSeed;
		Out.Table->FinishSpawning(Transform);
		if (!Out.Table->HasContext())
		{
			Out.Table->RebuildTable();
		}
		Out.Balls = World->SpawnActor<ARbBallSet>();
		if (Out.Balls)
		{
			Out.Balls->InitForTable(Out.Table);
		}
		return Out;
	}

	// The reference shot of the playback tests: 9-ft table, object ball 1 straight in along the 45 deg diagonal to the
	// (+x, +y) corner pocket, cue ball 0.5 m behind it, 2 m/s with draw. Object ball pocketed, cue ball stays on the table.
	inline TSharedPtr<const FRbShot> SimulateTwoBallShot(const TSharedPtr<const FRbTableContext>& Context, FString& OutError,
		double Speed = 2.0, double OffsetB = -0.3)
	{
		if (!Context.IsValid())
		{
			OutError = TEXT("no table context");
			return nullptr;
		}
		rb::SimInput Input;
		RbShot::InitSimInput(*Context, Input);
		const double Diagonal = FMath::Sqrt(0.5);
		const rb::Vec3 ObjectBall(1.02, 0.385, Context->BallRadius(1));
		const rb::Vec3 CueBall(ObjectBall.x - 0.5 * Diagonal, ObjectBall.y - 0.5 * Diagonal, Context->BallRadius(0));
		Input.Balls[0].InPlay = true;
		Input.Balls[0].State.Position = CueBall;
		Input.Balls[0].Orientation = rb::Normalized(rb::Quat(0.9, 0.1, -0.3, 0.2));
		Input.Balls[1].InPlay = true;
		Input.Balls[1].State.Position = ObjectBall;
		Input.Balls[1].Orientation = rb::Normalized(rb::Quat(0.2, -0.7, 0.4, 0.5));
		rb::CueStrikeInput Strike;
		Strike.Cue = rb::kCuePlaying19oz;
		Strike.Speed = Speed;
		Strike.Azimuth = 0.25 * UE_DOUBLE_PI;
		Strike.Elevation = 0.05;
		Strike.OffsetB = OffsetB;
		return ARbBallRackDemo::SimulateStrike(Context, Input, Strike, OutError);
	}

	// Transient stand-in for MPC_RbBalls (UE-3 generates the real one): Ball00..Ball15 + BallRadiusCm.
	inline UMaterialParameterCollection* MakeBallMpc()
	{
		UMaterialParameterCollection* Mpc = NewObject<UMaterialParameterCollection>(GetTransientPackage(), NAME_None, RF_Transient);
		for (int32 Index = 0; Index < 16; ++Index)
		{
			FCollectionVectorParameter Parameter;
			Parameter.ParameterName = RbAssetPaths::Param::MpcBall(Index);
			Parameter.DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			Mpc->VectorParameters.Add(Parameter);
		}
		FCollectionScalarParameter Radius;
		Radius.ParameterName = RbAssetPaths::Param::BallRadiusCm;
		Mpc->ScalarParameters.Add(Radius);
#if WITH_EDITOR
		Mpc->PostEditChange(); // builds the uniform-buffer layout like a loaded asset
#endif
		return Mpc;
	}

	inline bool SameBits(double A, double B) { return std::bit_cast<uint64>(A) == std::bit_cast<uint64>(B); }
	inline bool SameBits(const rb::Vec3& A, const rb::Vec3& B) { return SameBits(A.x, B.x) && SameBits(A.y, B.y) && SameBits(A.z, B.z); }
	inline bool SameBits(const rb::Quat& A, const rb::Quat& B) { return SameBits(A.w, B.w) && SameBits(A.x, B.x) && SameBits(A.y, B.y) && SameBits(A.z, B.z); }
	inline bool SameBits(const rb::BallState& A, const rb::BallState& B)
	{
		return A.State == B.State && SameBits(A.Position, B.Position) && SameBits(A.Velocity, B.Velocity) && SameBits(A.Omega, B.Omega);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
