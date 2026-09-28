// M1 test room, lux probe (E4) and look-dev camera tests (Docs/ue-architecture.md 8.3, 12 A7 / A8, 13 UE-8). Owner: UE-8.
//   RawBreak.Unit.Room.LuxProbe_*   the analytic probe against closed forms (T22 Lambertian source, parallel-rectangle form
//                                   factor, UE's barn-door clipping against the vertical-louvre geometry, point / spot lights in UE's units)
//   RawBreak.Unit.Room.E4_*         the room's WPA lamp: >= 520 lux on bed and rails, uniformity, ~50 lux ambient, no blinding at the eye, the lamp
//                                   follows a moved / yawed table, physical light setup (lumens, source size, 4000 K)
//   RawBreak.Unit.Room.Validator    the M1 level validator on a complete layout built in a test world (and a broken one)
//   RawBreak.Unit.LookDev.*         chin-on-cue placement (plan 4.2) and the R-06 optics of the look-dev camera

#include "Core/RbAssetPaths.h"
#include "Dev/RbLookDevCamera.h"
#include "Game/RbGameMode.h"
#include "Game/RbTestRoom.h"
#include "Table/RbTable.h"
#include "Tests/RbTestFlags.h"

#include "CineCameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"

#include "rb/Equipment/Cue.h"
#include "rb/Equipment/TableSpec.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbRoomTestsPrivate
{
	// Transient game world for actor tests (pattern of RbTableTests / CQTest's FActorTestSpawner).
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RbRoomTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->EndPlay(EEndPlayReason::LevelTransition);
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			if (GEngine->GetWorldContextFromWorld(World))
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}
	};

	// Parallel-rectangle form factor from a point below a Lambertian rectangle [x1, x2] x [y1, y2] at height H (the receiver at
	// the origin, both planes horizontal): superposition of the corner formula F = 1/(2 pi) [A/sqrt(1+A^2) atan(B/sqrt(1+A^2)) +
	// B/sqrt(1+B^2) atan(A/sqrt(1+B^2))] with A = a/H, B = b/H.
	double CornerFactor(double A, double B, double H)
	{
		const double X = A / H, Y = B / H;
		const double SX = FMath::Sqrt(1.0 + X * X), SY = FMath::Sqrt(1.0 + Y * Y);
		return (X / SX * FMath::Atan(Y / SX) + Y / SY * FMath::Atan(X / SY)) / (2.0 * UE_DOUBLE_PI);
	}

	double FormFactor(double X1, double X2, double Y1, double Y2, double H)
	{
		return CornerFactor(X2, Y2, H) - CornerFactor(X1, Y2, H) - CornerFactor(X2, Y1, H) + CornerFactor(X1, Y1, H);
	}

	// A rect light at Location facing down (local X = -Z), width along world Y, height along world X, in lumens.
	URectLightComponent* MakeDownLight(UWorld* World, const FVector& Location, double WidthCm, double HeightCm, double Lumens, double BarnAngle, double BarnLength)
	{
		AActor* Holder = World->SpawnActor<AActor>();
		URectLightComponent* Light = NewObject<URectLightComponent>(Holder);
		Holder->SetRootComponent(Light);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetWorldLocationAndRotation(Location, FRotator(-90.0, 0.0, 0.0));
		Light->IntensityUnits = ELightUnits::Lumens;
		Light->Intensity = static_cast<float>(Lumens);
		Light->SourceWidth = static_cast<float>(WidthCm);
		Light->SourceHeight = static_cast<float>(HeightCm);
		Light->BarnDoorAngle = static_cast<float>(BarnAngle);
		Light->BarnDoorLength = static_cast<float>(BarnLength);
		Light->AttenuationRadius = 1.0e6f; // window = 1 for the closed forms
		Light->RegisterComponent();
		return Light;
	}

	struct FM1Layout
	{
		ARbTable* Table = nullptr;
		ARbTestRoom* Room = nullptr;
		APlayerStart* Start = nullptr;
	};

	// The complete M1 layout of rb_make_test_room.py, built in C++.
	FM1Layout BuildM1Layout(UWorld* World)
	{
		FM1Layout L;
		World->GetWorldSettings()->DefaultGameMode = ARbGameMode::StaticClass();
		L.Table = World->SpawnActor<ARbTable>(FVector::ZeroVector, FRotator::ZeroRotator);
		L.Room = World->SpawnActor<ARbTestRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
		L.Room->RebuildRoom();
		L.Table->LampUndersideHeight = L.Room->GetLampUndersideHeightMeters();
		L.Table->RebuildTable();
		L.Start = World->SpawnActor<APlayerStart>(FVector(-205.0, 0.0, 100.0), FRotator::ZeroRotator);
		for (const TCHAR* Tag : {RbAssetPaths::CaptureCamera::Overhead, RbAssetPaths::CaptureCamera::ChinOnCue, RbAssetPaths::CaptureCamera::BallCloseUp,
				 RbAssetPaths::CaptureCamera::RoomOverview})
		{
			ARbLookDevCamera* Camera = World->SpawnActor<ARbLookDevCamera>(FVector(-100.0, 0.0, 150.0), FRotator::ZeroRotator);
			Camera->Tags.Add(FName(Tag));
		}
		return L;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRoomLuxProbeLambertian, "RawBreak.Unit.Room.LuxProbe_T22_Lambertian", RB_UNIT_TEST_FLAGS)
bool FRbRoomLuxProbeLambertian::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	// A 1 cm^2 Lambertian emitter of 1600 lm, 1 m above the bed: I0 = Phi / pi = 509.30 cd -> 509.30 lux straight below (T22),
	// I0 h^2 / d^4 off axis (cos^2 law of a Lambertian source on a horizontal receiver).
	const URectLightComponent* Light = MakeDownLight(TestWorld.World, FVector(0.0, 0.0, 100.0), 1.0, 1.0, 1600.0, 88.0, 0.1);
	const double Below = ARbTestRoom::IlluminanceFromLight(*Light, FVector::ZeroVector, FVector::UpVector);
	TestNearlyEqual(TEXT("T22: E straight below = 509.30 lux"), Below, 1600.0 / UE_DOUBLE_PI, 0.05);
	const double Off = ARbTestRoom::IlluminanceFromLight(*Light, FVector(50.0, 0.0, 0.0), FVector::UpVector);
	TestNearlyEqual(TEXT("E 0.5 m off axis = I0 h^2 / d^4"), Off, 1600.0 / UE_DOUBLE_PI / (1.25 * 1.25), 0.05);
	TestEqual(TEXT("nothing above the emitter plane"), ARbTestRoom::IlluminanceFromLight(*Light, FVector(0.0, 0.0, 150.0), FVector::UpVector), 0.0);
	TestEqual(TEXT("a surface facing away receives nothing"), ARbTestRoom::IlluminanceFromLight(*Light, FVector::ZeroVector, -FVector::UpVector), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRoomLuxProbeRect, "RawBreak.Unit.Room.LuxProbe_RectFormFactor", RB_UNIT_TEST_FLAGS)
bool FRbRoomLuxProbeRect::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	// 60 x 30 cm section, 2000 lm, 1.12 m above the bed, no barn doors: E = pi L F with L = Phi / (pi A) and the exact form factor.
	const double H = 112.0, W = 30.0, Ht = 60.0, Lumens = 2000.0;
	const URectLightComponent* Light = MakeDownLight(TestWorld.World, FVector(0.0, 0.0, H), W, Ht, Lumens, 88.0, 0.1);
	const double L = Lumens / (UE_DOUBLE_PI * W * Ht * 1e-4);
	for (const FVector2D& P : {FVector2D(0.0, 0.0), FVector2D(40.0, 10.0), FVector2D(-130.0, 70.0), FVector2D(145.0, -81.0)})
	{
		// Emitter height (local Z) along world X, width (local Y) along world Y; the rectangle relative to the receiver.
		const double Expected = UE_DOUBLE_PI * L * FormFactor((-0.5 * Ht - P.X) / 100.0, (0.5 * Ht - P.X) / 100.0, (-0.5 * W - P.Y) / 100.0, (0.5 * W - P.Y) / 100.0, H / 100.0);
		const double Probe = ARbTestRoom::IlluminanceFromLight(*Light, FVector(P.X, P.Y, 0.0), FVector::UpVector);
		TestTrue(FString::Printf(TEXT("probe %.3f vs form factor %.3f lux at (%.0f, %.0f) cm within 0.3 %%"), Probe, Expected, P.X, P.Y),
			FMath::Abs(Probe - Expected) <= 0.003 * Expected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRoomLuxProbeBarnDoors, "RawBreak.Unit.Room.LuxProbe_BarnDoors", RB_UNIT_TEST_FLAGS)
bool FRbRoomLuxProbeBarnDoors::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	// Vertical louvres (barn door angle 0) of length Lb around a 60 (x) x 30 (y) cm emitter at height H: a receiver at x = q beyond
	// the edge E = 30 sees the emitter part a <= E - Lb (q - E) / (H - Lb) (rays from the near side hit the near louvre), the full
	// width in y (the receiver is inside the y extent). Same as UE's RectLight.ush GetRect.
	const double H = 110.0, Lb = 12.0, E = 30.0, Lumens = 2000.0;
	const URectLightComponent* Doors = MakeDownLight(TestWorld.World, FVector(0.0, 0.0, H), 30.0, 60.0, Lumens, 0.0, Lb);
	const URectLightComponent* Open = MakeDownLight(TestWorld.World, FVector(0.0, 500.0, H), 30.0, 60.0, Lumens, 88.0, Lb);
	const double L = Lumens / (UE_DOUBLE_PI * 30.0 * 60.0 * 1e-4);
	for (const double Q : {50.0, 90.0, 140.0})
	{
		const double Visible = E - Lb * (Q - E) / (H - Lb); // visible emitter x in [-E, Visible]
		const double Expected = Visible > -E ? UE_DOUBLE_PI * L * FormFactor((-E - Q) / 100.0, (Visible - Q) / 100.0, -0.15, 0.15, H / 100.0) : 0.0;
		const double Probe = ARbTestRoom::IlluminanceFromLight(*Doors, FVector(Q, 0.0, 0.0), FVector::UpVector);
		const double Unclipped = ARbTestRoom::IlluminanceFromLight(*Open, FVector(Q, 500.0, 0.0), FVector::UpVector);
		TestTrue(FString::Printf(TEXT("louvred %.2f vs geometric %.2f lux at q = %.0f cm (open %.2f)"), Probe, Expected, Q, Unclipped),
			FMath::Abs(Probe - Expected) <= 0.003 * FMath::Max(Expected, 1.0) && Probe < Unclipped);
	}
	// Directly below the centre nothing is clipped.
	TestNearlyEqual(TEXT("no clipping below the centre"), ARbTestRoom::IlluminanceFromLight(*Doors, FVector::ZeroVector, FVector::UpVector),
		ARbTestRoom::IlluminanceFromLight(*Open, FVector(0.0, 500.0, 0.0), FVector::UpVector), 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRoomLuxProbePointSpot, "RawBreak.Unit.Room.LuxProbe_PointSpot", RB_UNIT_TEST_FLAGS)
bool FRbRoomLuxProbePointSpot::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	// Point / spot lights 1.5 m above the receiver, photometric units as UE converts them (PointLightComponent.cpp,
	// SpotLightComponent.cpp): lumens -> Phi / 4 pi (point) or Phi / (2 pi (1 - cos outer)) (spot), nits over the capsule area
	// 4 pi r (r + l / 2); E = I cos / d^2; spot falloff Square(saturate((cos - cos outer) / (cos inner - cos outer))) on UE's
	// clamped cone angles (the inner cone wins when it is set wider than the outer one).
	const double H = 150.0;
	auto Make = [&TestWorld](UClass* Class, ELightUnits Units, double Intensity) -> UPointLightComponent*
	{
		AActor* Holder = TestWorld.World->SpawnActor<AActor>();
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Holder, Class);
		Holder->SetRootComponent(Light);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetWorldLocationAndRotation(FVector(0.0, 0.0, 150.0), FRotator(-90.0, 0.0, 0.0));
		Light->IntensityUnits = Units;
		Light->Intensity = static_cast<float>(Intensity);
		Light->AttenuationRadius = 1.0e6f;
		Light->RegisterComponent();
		return Light;
	};
	auto At = [](const UPointLightComponent* Light, double X) { return ARbTestRoom::IlluminanceFromLight(*Light, FVector(X, 0.0, 0.0), FVector::UpVector); };
	const double D2Off = (H * H + 80.0 * 80.0) * 1e-4, CosOff = H / FMath::Sqrt(H * H + 80.0 * 80.0);

	const UPointLightComponent* Lumens = Make(UPointLightComponent::StaticClass(), ELightUnits::Lumens, 1000.0);
	TestNearlyEqual(TEXT("point, 1000 lm: E below = Phi / 4 pi / h^2"), At(Lumens, 0.0), 1000.0 / (4.0 * UE_DOUBLE_PI) / 2.25, 1e-6);
	TestNearlyEqual(TEXT("point, 1000 lm: cosine law off axis"), At(Lumens, 80.0), 1000.0 / (4.0 * UE_DOUBLE_PI) * CosOff / D2Off, 1e-6);
	const UPointLightComponent* Candelas = Make(UPointLightComponent::StaticClass(), ELightUnits::Candelas, 300.0);
	TestNearlyEqual(TEXT("point, 300 cd"), At(Candelas, 0.0), 300.0 / 2.25, 1e-6);
	UPointLightComponent* Nits = Make(UPointLightComponent::StaticClass(), ELightUnits::Nits, 20000.0);
	Nits->SourceRadius = 5.0f;
	Nits->SourceLength = 10.0f;
	const double NitsCd = 20000.0 * 4.0 * UE_DOUBLE_PI * 5.0 * (5.0 + 5.0) * 1e-4;
	TestNearlyEqual(TEXT("point, nits over the capsule area (UE)"), At(Nits, 0.0), NitsCd / 2.25, 1e-6);

	USpotLightComponent* Spot = Cast<USpotLightComponent>(Make(USpotLightComponent::StaticClass(), ELightUnits::Lumens, 1000.0));
	Spot->InnerConeAngle = 10.0f;
	Spot->OuterConeAngle = 30.0f;
	const double SpotCd = 1000.0 / (2.0 * UE_DOUBLE_PI * (1.0 - FMath::Cos(FMath::DegreesToRadians(30.0))));
	TestNearlyEqual(TEXT("spot, 1000 lm in a 30 deg cone: E below"), At(Spot, 0.0), SpotCd / 2.25, 1e-5 * SpotCd); // UE's cone angles are float
	const double X20 = H * FMath::Tan(FMath::DegreesToRadians(20.0)), D20 = (H * H + X20 * X20) * 1e-4, Cos20 = FMath::Cos(FMath::DegreesToRadians(20.0));
	const double Fall20 = FMath::Square((Cos20 - FMath::Cos(FMath::DegreesToRadians(30.0))) / (FMath::Cos(FMath::DegreesToRadians(10.0)) - FMath::Cos(FMath::DegreesToRadians(30.0))));
	TestNearlyEqual(TEXT("spot: penumbra falloff at 20 deg"), At(Spot, X20), SpotCd * Cos20 / D20 * Fall20, 1e-5 * SpotCd);
	TestEqual(TEXT("spot: nothing outside the outer cone"), At(Spot, H * FMath::Tan(FMath::DegreesToRadians(31.0))), 0.0);

	// Inner cone set wider than the outer one: UE lights up to the inner angle (outer = inner + 0.001 rad).
	Spot->InnerConeAngle = 40.0f;
	Spot->OuterConeAngle = 20.0f;
	const FVector2f Cone = Spot->GetClampedConeAngles();
	const double CosI = FMath::Cos(static_cast<double>(Cone.X)), CosO = FMath::Cos(static_cast<double>(Cone.Y));
	const double WideCd = 1000.0 / (2.0 * UE_DOUBLE_PI * (1.0 - CosO));
	const double X30 = H * FMath::Tan(FMath::DegreesToRadians(30.0)), D30 = (H * H + X30 * X30) * 1e-4, Cos30 = FMath::Cos(FMath::DegreesToRadians(30.0));
	const double Expected30 = WideCd * Cos30 / D30 * FMath::Square(FMath::Clamp((Cos30 - CosO) / (CosI - CosO), 0.0, 1.0));
	TestTrue(FString::Printf(TEXT("inverted cone: lit at 30 deg like UE (%.3f vs %.3f lux)"), At(Spot, X30), Expected30),
		Expected30 > 1.0 && FMath::IsNearlyEqual(At(Spot, X30), Expected30, 1e-5 * Expected30));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRoomE4Lamp,"RawBreak.Unit.Room.E4_LampCompliance", RB_UNIT_TEST_FLAGS)
bool FRbRoomE4Lamp::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	ARbTable* Table = TestWorld.World->SpawnActor<ARbTable>(FVector::ZeroVector, FRotator::ZeroRotator);
	ARbTestRoom* Room = TestWorld.World->SpawnActor<ARbTestRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("table with context"), Table && Table->HasContext()) || !TestNotNull(TEXT("room"), Room))
	{
		return false;
	}
	Room->RebuildRoom();
	const rb::TableSpec& Spec = Table->GetContext().Spec;
	TestNearlyEqual(TEXT("room bed height = TableSpec (R-14)"), Room->GetBedHeightUsed(), 100.0 * Spec.BedHeight, 1e-9);
	TestTrue(TEXT("lamp underside >= 1.016 m above the bed (WPA)"), Room->GetLampUndersideHeightMeters() >= 1.016 - 1e-12);

	const FRbLuxReport Lamp = Room->ComputeLux(5.0, ERbLuxSources::Lamp);
	const FRbLuxReport All = Room->ComputeLux(5.0, ERbLuxSources::All);
	const FRbLuxReport Ambient = Room->ComputeLux(5.0, ERbLuxSources::Ambient);
	AddInfo(FString::Printf(TEXT("lamp: %s"), *Lamp.ToString()));
	AddInfo(FString::Printf(TEXT("all:  %s"), *All.ToString()));
	TestTrue(TEXT("5 cm grid over bed and rails"), Lamp.BedPoints > 1000 && Lamp.RailPoints > 500);
	TestTrue(FString::Printf(TEXT("E4: the lamp alone >= 520 lux on bed and rails (min %.1f)"), Lamp.TableMin()), Lamp.TableMin() >= ARbTestRoom::WpaMinLux);
	TestTrue(FString::Printf(TEXT("uniformity: max / min over bed and rails <= 1.5 (%.2f)"), Lamp.TableUniformity()), Lamp.TableUniformity() <= 1.5);
	// WPA "blinding" (5000 lux) is about the direct view of the fixture: the illuminance AT THE EYE facing the lamp, from the
	// places a player looks up from (standing at the head end, down on a shot at the head rail and at the side rail, bent over
	// the middle of the bed under the canopy). The worst normal is searched over the directions to every section and straight up.
	{
		const FVector Bed = Table->GetBedCenterWorld();
		const FVector Eyes[] = {FVector(-205.0, 0.0, 170.0 - Bed.Z), FVector(-150.0, 0.0, 25.0), FVector(0.0, -90.0, 30.0), FVector(40.0, 20.0, 35.0)};
		double WorstEye = 0.0;
		for (const FVector& Local : Eyes)
		{
			const FVector Eye = Bed + Local;
			TArray<FVector> Normals = {FVector::UpVector};
			for (const URectLightComponent* Light : Room->GetLampLights())
			{
				Normals.Add((Light->GetComponentLocation() - Eye).GetSafeNormal());
			}
			for (const FVector& Normal : Normals)
			{
				double E = 0.0;
				for (const URectLightComponent* Light : Room->GetLampLights())
				{
					E += ARbTestRoom::IlluminanceFromLight(*Light, Eye, Normal);
				}
				WorstEye = FMath::Max(WorstEye, E);
			}
		}
		AddInfo(FString::Printf(TEXT("worst illuminance at a player's eye facing the lamp: %.0f lux"), WorstEye));
		TestTrue(FString::Printf(TEXT("no blinding direct view of the lamp (eye %.0f lux < 5000, WPA)"), WorstEye), WorstEye > 0.0 && WorstEye < 5000.0);
	}
	TestTrue(FString::Printf(TEXT("room >= 50 lux everywhere on the walkway (min %.1f)"), All.FloorMin), All.FloorMin >= 50.0);
	TestTrue(FString::Printf(TEXT("ambient panels ~50 lux (avg %.1f)"), Ambient.FloorAvg),
		Ambient.FloorAvg >= ARbTestRoom::AmbientMinLux && Ambient.FloorAvg <= ARbTestRoom::AmbientMaxLux);

	// Physical setup of the key lamp (plan 6.1): lumens, real source size, 4000 K, barn doors = the louvres; diffusers hidden
	// from ray tracing / Lumen / shadows (pitfall 9).
	const TArray<TObjectPtr<URectLightComponent>>& Lights = Room->GetLampLights();
	TestEqual(TEXT("one rect light per section"), Lights.Num(), Room->LampColumnX.Num() * Room->LampRowY.Num());
	double Flux = 0.0;
	for (const URectLightComponent* Light : Lights)
	{
		Flux += Light->Intensity;
		TestTrue(TEXT("lumens, source size > 0, 4000 K, louvre barn doors, facing down"), Light->IntensityUnits == ELightUnits::Lumens && Light->SourceWidth > 1.0f &&
			Light->SourceHeight > 1.0f && Light->bUseTemperature && FMath::IsNearlyEqual(Light->Temperature, 4000.0f) &&
			FMath::IsNearlyEqual(Light->BarnDoorAngle, static_cast<float>(Room->LampLouvreAngleDeg)) &&
			FMath::IsNearlyEqual(Light->BarnDoorLength, static_cast<float>(Room->LampLouvreLength)) &&
			Light->GetForwardVector().Equals(-FVector::UpVector, 1e-6));
	}
	AddInfo(FString::Printf(TEXT("table lamp: %d sections, %.0f lm"), Lights.Num(), Flux));
	int32 Diffusers = 0;
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Room);
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetName().StartsWith(TEXT("LampDiffuser")) || Mesh->GetName().StartsWith(TEXT("AmbientDiffuser")))
		{
			++Diffusers;
			TestTrue(TEXT("diffuser: no ray tracing, no Lumen, no shadow"), !Mesh->bVisibleInRayTracing && !Mesh->bAffectDynamicIndirectLighting && !Mesh->CastShadow);
		}
		TestTrue(TEXT("generated components are transient"), Mesh->HasAnyFlags(RF_Transient) && Mesh->ComponentHasTag(ARbTestRoom::GeneratedComponentTag));
	}
	TestEqual(TEXT("a diffuser per section and ambient panel"), Diffusers, Lights.Num() + Room->GetAmbientLights().Num());

	// Rebuilding does not leak components.
	const int32 Before = Meshes.Num();
	Room->RebuildRoom();
	TInlineComponentArray<UStaticMeshComponent*> After(Room);
	TestEqual(TEXT("rebuild replaces the components"), After.Num(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRoomLampFollowsTable, "RawBreak.Unit.Room.E4_LampFollowsTable", RB_UNIT_TEST_FLAGS)
bool FRbRoomLampFollowsTable::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	// A moved + yawed table: the lamp is centred over its bed in its frame, the probe (in the table frame) gives the same lux.
	ARbTable* Table = TestWorld.World->SpawnActor<ARbTable>(FVector(40.0, -25.0, 0.0), FRotator(0.0, 12.0, 0.0));
	ARbTestRoom* Room = TestWorld.World->SpawnActor<ARbTestRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
	FTestWorld Reference;
	Reference.World->SpawnActor<ARbTable>(FVector::ZeroVector, FRotator::ZeroRotator);
	ARbTestRoom* ReferenceRoom = Reference.World->SpawnActor<ARbTestRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("table"), Table) || !TestNotNull(TEXT("room"), Room) || !TestNotNull(TEXT("reference room"), ReferenceRoom))
	{
		return false;
	}
	Room->RebuildRoom();
	ReferenceRoom->RebuildRoom();
	FVector Centroid = FVector::ZeroVector;
	for (const URectLightComponent* Light : Room->GetLampLights())
	{
		Centroid += Light->GetComponentLocation();
	}
	Centroid /= FMath::Max(1, Room->GetLampLights().Num());
	const FVector Bed = Table->GetBedCenterWorld();
	TestTrue(TEXT("lamp centred over the yawed table's bed"), FVector2D(Centroid.X, Centroid.Y).Equals(FVector2D(Bed.X, Bed.Y), 1e-6));
	TestNearlyEqual(TEXT("emitter plane height above the cloth"), Centroid.Z - Bed.Z, Room->GetLampEmitterHeight() - 0.6, 1e-6);
	const FRbLuxReport Moved = Room->ComputeLux(10.0, ERbLuxSources::Lamp);
	const FRbLuxReport Origin = ReferenceRoom->ComputeLux(10.0, ERbLuxSources::Lamp);
	TestNearlyEqual(TEXT("same bed minimum in the table frame"), Moved.BedMin, Origin.BedMin, 1e-6 * Origin.BedMin);
	TestNearlyEqual(TEXT("same rail minimum in the table frame"), Moved.RailMin, Origin.RailMin, 1e-6 * Origin.RailMin);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRoomValidator, "RawBreak.Unit.Room.Validator", RB_UNIT_TEST_FLAGS)
bool FRbRoomValidator::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	const FM1Layout Layout = BuildM1Layout(TestWorld.World);
	bool bOk = false;
	FString Report = ARbTestRoom::ValidateM1Level(TestWorld.World, bOk);
	AddInfo(Report);
	TestTrue(TEXT("the M1 layout passes the validator"), bOk);
	TestFalse(TEXT("no FAIL line"), Report.Contains(TEXT("FAIL")));

	// Broken single sources and layout are caught.
	Layout.Table->LampUndersideHeight = 1.0;
	Layout.Start->Destroy(); // a PlayerStart's root is static: replace it at the foot end instead of moving it
	TestWorld.World->SpawnActor<APlayerStart>(FVector(100.0, 0.0, 100.0), FRotator::ZeroRotator);
	Report = ARbTestRoom::ValidateM1Level(TestWorld.World, bOk);
	TestFalse(TEXT("a wrong lamp height / player start fails"), bOk);
	TestTrue(TEXT("lamp line fails"), Report.Contains(TEXT("FAIL lamp underside height")));
	TestTrue(TEXT("player start line fails"), Report.Contains(TEXT("FAIL player start at the head end")));
	TestWorld.World->SpawnActor<ARbTable>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator);
	Report = ARbTestRoom::ValidateM1Level(TestWorld.World, bOk);
	TestTrue(TEXT("two tables fail"), Report.Contains(TEXT("FAIL table:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLookDevChinOnCue, "RawBreak.Unit.LookDev.ChinOnCuePlacement", RB_UNIT_TEST_FLAGS)
bool FRbLookDevChinOnCue::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	ARbTable* Table = TestWorld.World->SpawnActor<ARbTable>(FVector(30.0, 20.0, 0.0), FRotator(0.0, 25.0, 0.0));
	if (!TestTrue(TEXT("table with context"), Table && Table->HasContext()))
	{
		return false;
	}
	// Plan 4.2 by hand: d = (cos th cos ph, cos th sin ph, -sin th), tip dome centre R + r + gap behind the cue-ball centre,
	// e = tip - s_e d + h_c n_up + y_vc n_side.
	FRbCameraPresetParams Params = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	Params.EyeBehindTipM = 0.45;
	Params.EyeAboveCueM = 0.10;
	Params.VisionCenterM = 0.02;
	const FVector2D Cue(-0.70, 0.12), Aim(0.635, 0.0);
	const double R = Table->GetContext().BallRadius(0), Tip = rb::kCuePlaying19oz.TipDomeRadius, Gap = 0.015, Theta = FMath::DegreesToRadians(4.0);
	const FRbChinOnCuePose Pose = ARbLookDevCamera::ComputeChinOnCuePose(*Table, Params, Cue, Aim, 4.0, Gap, R, Tip);
	const double Phi = FMath::Atan2(Aim.Y - Cue.Y, Aim.X - Cue.X);
	const FVector3d D(FMath::Cos(Theta) * FMath::Cos(Phi), FMath::Cos(Theta) * FMath::Sin(Phi), -FMath::Sin(Theta));
	const FVector3d Up(FMath::Sin(Theta) * FMath::Cos(Phi), FMath::Sin(Theta) * FMath::Sin(Phi), FMath::Cos(Theta));
	const FVector3d Side(-FMath::Sin(Phi), FMath::Cos(Phi), 0.0);
	const FVector3d TipCore = FVector3d(Cue.X, Cue.Y, R) - D * (R + Tip + Gap);
	const FVector3d EyeCore = TipCore - D * 0.45 + Up * 0.10 + Side * 0.02;
	const rb::Vec3 Eye = Table->WorldToCore(Pose.Eye);
	TestTrue(TEXT("eye = P_axis(s_e) + h_c n_up + y_vc n_side (1e-9 m)"), FVector3d(Eye.x, Eye.y, Eye.z).Equals(EyeCore, 1e-9));
	TestTrue(TEXT("tip dome centre on the axis behind the cue ball"), Pose.TipDomeCore.Equals(TipCore, 1e-12) && Pose.DirectionCore.Equals(D, 1e-12));
	TestNearlyEqual(TEXT("n_up is perpendicular to the cue axis"), FVector3d::DotProduct(Up, D), 0.0, 1e-12);
	const FVector Forward = Pose.Rotation.Vector();
	TestTrue(TEXT("the eye looks at the aim point"), Forward.Equals((Pose.AimPoint - Pose.Eye).GetSafeNormal(), 1e-6));
	TestNearlyEqual(TEXT("no roll"), Pose.Rotation.Roll, 0.0, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbLookDevOptics, "RawBreak.Unit.LookDev.OpticsR06", RB_UNIT_TEST_FLAGS)
bool FRbLookDevOptics::RunTest(const FString& Parameters)
{
	using namespace RbRoomTestsPrivate;
	FTestWorld TestWorld;
	ARbLookDevCamera* Camera = TestWorld.World->SpawnActor<ARbLookDevCamera>(FVector::ZeroVector, FRotator::ZeroRotator);
	UCineCameraComponent* Cine = Camera ? Camera->GetCineCameraComponent() : nullptr;
	if (!TestNotNull(TEXT("look-dev camera"), Cine))
	{
		return false;
	}
	// R-06: filmback aspect = viewport aspect, V = the preset's vertical FOV at every aspect, aperture diameter f / N = the pupil.
	const FRbCameraPresetParams Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	for (const float Aspect : {16.0f / 9.0f, 64.0f / 27.0f, 4.0f / 3.0f})
	{
		Camera->ApplyOptics(Aspect);
		TestNearlyEqual(FString::Printf(TEXT("V = %.1f deg at aspect %.3f"), Eyes.VerticalFovDeg, Aspect), static_cast<double>(Cine->GetVerticalFieldOfView()),
			Eyes.VerticalFovDeg, 0.01);
		TestNearlyEqual(TEXT("filmback aspect = viewport aspect"), static_cast<double>(Cine->Filmback.SensorWidth / Cine->Filmback.SensorHeight), static_cast<double>(Aspect),
			1e-4);
		TestNearlyEqual(TEXT("aperture diameter f / N = pupil [mm]"), static_cast<double>(Cine->CurrentFocalLength / Cine->CurrentAperture), Eyes.ApertureDiameterMm, 1e-3);
		TestFalse(TEXT("no letterbox: the aspect follows the viewport"), Cine->bConstrainAspectRatio);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
