// RawBreak.Unit.Venue.* (M2-A, Docs/ue-architecture.md 18.8; venue-dive-bar 16): the dive bar's lighting maths (4.3, 4.4, 4.6,
// 4.7), the transcription of Art/DiveBar/layout.json + lights.json against the spec and the core (TableSpec), the seeded roll-off
// (VDB-T10), the photosensitivity of every animated light (VDB-T8), the light flags (VDB-T11 on the data), the state ramps of
// ARbVenueInfo (VDB-T12), the night-look exposure curve and the DB-0 axis test asset (bounds, pivot, arrow +X, +Y marker, letters
// on +X). Owner: M2-A.

#include "Core/RbAssetPaths.h"
#include "Tests/RbTestFlags.h"
#include "Venue/RbVenueInfo.h"
#include "Venue/RbVenueJson.h"
#include "Venue/RbVenueLighting.h"
#include "Venue/RbVenueTableCheck.h"

#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "Tests/AutomationCommon.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif


#include "rb/Equipment/TableSpec.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbVenueTestsPrivate
{
	FRbJson LoadJson(const TCHAR* Relative)
	{
		FRbJson Out;
		FRbJson::LoadProjectFile(Relative, Out);
		return Out;
	}

	double Num(const FRbJson& O, const TCHAR* Key, double Default = 0.0)
	{
		return O.GetNumber(Key, Default);
	}

	// The same mapping as rb_make_divebar.py (lights.json "animation" -> FRbVenueLight).
	FRbVenueLight MakeAnimated(const FRbJson& L)
	{
		FRbVenueLight Out;
		Out.Id = FName(*L.GetString(TEXT("id")));
		const FRbJson* AnimPtr = L.Find(TEXT("animation"));
		if (!AnimPtr)
		{
			return Out;
		}
		const FRbJson& A = *AnimPtr;
		const FString Type = A.GetString(TEXT("type"));
		Out.AnimSeed = static_cast<int32>(Num(A, TEXT("seed"), 0.0));
		if (Type == TEXT("tv"))
		{
			Out.Animation = ERbVenueLightAnimation::Tv;
			Out.AnimDepth = Num(A, TEXT("depth"));
			Out.AnimRate = Num(A, TEXT("rate_hz"));
		}
		else if (Type == TEXT("cycle"))
		{
			Out.Animation = ERbVenueLightAnimation::Cycle;
			Out.AnimRate = Num(A, TEXT("period_s"));
			for (const FRbJson& C : A[TEXT("colors")].Array)
			{
				Out.CycleColors.Add(FLinearColor(C[0].AsNumber(), C[1].AsNumber(), C[2].AsNumber()));
			}
		}
		else if (Type == TEXT("chase"))
		{
			Out.Animation = ERbVenueLightAnimation::Chase;
			Out.AnimDepth = Num(A, TEXT("depth"));
			Out.AnimRate = Num(A, TEXT("steps_per_s"));
		}
		else if (Type == TEXT("headlights"))
		{
			Out.Animation = ERbVenueLightAnimation::Headlights;
			Out.IntervalRange = A[TEXT("interval_s")].AsVector2D(FVector2D(20.0, 120.0));
			Out.SweepRange = A[TEXT("sweep_s")].AsVector2D(FVector2D(1.5, 3.0));
			Out.YawRange = A[TEXT("yaw_deg")].AsVector2D(FVector2D(-35.0, 45.0));
		}
		return Out;
	}
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("RbVenueTestWorld"), EUniqueObjectNameOptions::GloballyUnique);
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
}

// --- 4.4 lamp model -------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueLampModel, "RawBreak.Unit.Venue.T1_LampModel_4_4", RB_UNIT_TEST_FLAGS)

bool FRbVenueLampModel::RunTest(const FString& Parameters)
{
	// The nominal lamp of 4.4 (no offset / yaw): bulbs 1.01 m above the bed, 0.46 m apart.
	FRbLampModel Lamp;
	Lamp.BulbsCore = {FVector3d(-0.46, 0.0, 1.01), FVector3d(0.0, 0.0, 1.01), FVector3d(0.46, 0.0, 1.01)};
	TestEqual(TEXT("I0 = eta Phi / (pi (1 - cos^2 50.2 deg)) = 355.9 cd"), Lamp.I0(), 355.9, 0.1);
	const rb::TableSpec Spec = rb::GetTableSpec(RbTypes::ToCore(ERbTablePreset::SevenFootBar));
	const double L = 0.5 * Spec.Length, W = 0.5 * Spec.Width, R = 0.5 * Spec.RailWidthTotal, Z = Spec.RailTopZ;
	struct FCase
	{
		const TCHAR* Name;
		FVector3d P;
		double Want;
	};
	const FCase Cases[] = {
		{TEXT("bed centre"), FVector3d(0, 0, 0), 828.0},
		{TEXT("foot spot"), FVector3d(0.5 * L, 0, 0), 664.0},
		{TEXT("side pocket nose"), FVector3d(0, W, 0), 549.0},
		{TEXT("corner pocket nose"), FVector3d(L, W, 0), 212.0},
		{TEXT("rail cap side mid"), FVector3d(0, W + R, Z), 501.0},
		{TEXT("rail cap end centre"), FVector3d(L + R, 0, Z), 258.0},
		{TEXT("rail cap at a corner"), FVector3d(L + R, W + R, Z), 117.0},
	};
	for (const FCase& C : Cases)
	{
		TestEqual(*FString::Printf(TEXT("4.4 %s"), C.Name), Lamp.IlluminanceAt(C.P), C.Want, 1.5);
	}
	// Every band point of the probe is inside its band for the nominal lamp.
	for (const FRbLuxPoint& P : RbVenueLighting::LuxBandPoints(L, W, Spec.RailWidthTotal, Z))
	{
		const double E = Lamp.IlluminanceAt(P.Core);
		TestTrue(*FString::Printf(TEXT("%s %.1f lux in %.0f..%.0f"), P.Name, E, P.BandMin, P.BandMax), E >= P.BandMin && E <= P.BandMax);
	}
	// Cloth EV of 4.4: L = rho E / pi, EV100 = log2(8 L) = 8.30.
	TestEqual(TEXT("cloth EV100 8.30"), RbVenueLighting::SceneEv100(0.15 * 828.0 / UE_DOUBLE_PI), 8.30, 0.01);
	return true;
}

// --- 4.3 neon flux ----------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueNeonFlux, "RawBreak.Unit.Venue.T11_NeonFlux_4_3", RB_UNIT_TEST_FLAGS)

bool FRbVenueNeonFlux::RunTest(const FString& Parameters)
{
	// Lambertian cylinder, 15 mm tubes: lm/m of 4.3.
	TestEqual(TEXT("clear red 320 lm/m"), RbVenueLighting::TubeFlux(2160, 0.015, 1.0), 320.0, 1.0);
	TestEqual(TEXT("standard blue 379 lm/m"), RbVenueLighting::TubeFlux(2560, 0.015, 1.0), 379.0, 1.0);
	TestEqual(TEXT("ruby 118 lm/m"), RbVenueLighting::TubeFlux(800, 0.015, 1.0), 118.0, 1.0);
	TestEqual(TEXT("cobalt 237 lm/m"), RbVenueLighting::TubeFlux(1600, 0.015, 1.0), 237.0, 1.0);
	TestEqual(TEXT("green 1484 lm/m"), RbVenueLighting::TubeFlux(10026, 0.015, 1.0), 1484.0, 1.5);
	TestEqual(TEXT("gold 962 lm/m"), RbVenueLighting::TubeFlux(6500, 0.015, 1.0), 962.0, 1.0);
	// Sign intensities I = Phi_rect / pi of 4.3.
	const double Tubes[] = {1830.0, 1060.0, 1680.0, 1290.0, 455.0};
	const double Candela[] = {185.0, 108.0, 170.0, 130.0, 46.0};
	for (int32 I = 0; I < 5; ++I)
	{
		TestEqual(*FString::Printf(TEXT("N%d intensity"), I + 1), RbVenueLighting::NeonProxyFlux(Tubes[I]) / UE_DOUBLE_PI, Candela[I], 1.0);
	}
	return true;
}

// --- 4.6 ramps ------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueRamp, "RawBreak.Unit.Venue.T12_RampNoStep", RB_UNIT_TEST_FLAGS)

bool FRbVenueRamp::RunTest(const FString& Parameters)
{
	for (const double Fps : {30.0, 60.0, 144.0})
	{
		FRbLightRamp Ramp;
		Ramp.Snap(0.0f);
		Ramp.Start(1.0f, 0.1f); // clamped to 0.8 s
		TestEqual(TEXT("ramp clamped to >= 0.8 s"), Ramp.Duration, RbVenueLighting::MinRampSeconds);
		double Time = 0.0, MaxStep = 0.0;
		float Prev = Ramp.Current;
		while (Ramp.IsActive() && Time < 5.0)
		{
			Ramp.Advance(static_cast<float>(1.0 / Fps));
			Time += 1.0 / Fps;
			MaxStep = FMath::Max(MaxStep, static_cast<double>(FMath::Abs(Ramp.Current - Prev)));
			Prev = Ramp.Current;
		}
		TestTrue(*FString::Printf(TEXT("%.0f fps: the ramp took >= 0.8 s (%.3f s)"), Fps, Time), Time >= 0.8 - 1e-6);
		TestEqual(*FString::Printf(TEXT("%.0f fps: reaches the target"), Fps), Ramp.Current, 1.0f);
		// smoothstep's steepest slope is 1.5 / duration: never a one-frame step.
		TestTrue(*FString::Printf(TEXT("%.0f fps: largest per-frame change %.4f <= 1.5 dt / 0.8 + eps"), Fps, MaxStep), MaxStep <= 1.5 / (0.8 * Fps) + 1e-4);
	}
	// A new target mid-ramp continues from the current value (no jump back).
	FRbLightRamp Ramp;
	Ramp.Snap(0.0f);
	Ramp.Start(1.0f, 0.8f);
	Ramp.Advance(0.4f);
	const float Mid = Ramp.Current;
	Ramp.Start(0.0f, 0.8f);
	TestEqual(TEXT("reversal starts at the current value"), Ramp.From, Mid);
	Ramp.Advance(0.0167f);
	TestTrue(TEXT("reversal: no jump"), FMath::Abs(Ramp.Current - Mid) < 0.05f);
	return true;
}

// --- 4.7 photosensitivity (VDB-T8) ---------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenuePhotosensitivity, "RawBreak.Unit.Venue.T8_Photosensitivity", RB_UNIT_TEST_FLAGS)

bool FRbVenuePhotosensitivity::RunTest(const FString& Parameters)
{
	using namespace RbVenueTestsPrivate;
	// The counter itself: a 5 Hz square wave of 50 % depth = 5 flashes / s; a 2 Hz one = 2; a 8 % wiggle = none.
	auto Wave = [](double Hz, double Depth)
	{
		TArray<double> S;
		for (int32 I = 0; I < 600; ++I)
		{
			const double T = I / 60.0;
			S.Add(1.0 - Depth * (FMath::Fmod(T * Hz, 1.0) < 0.5 ? 0.0 : 1.0));
		}
		return S;
	};
	TestEqual(TEXT("5 Hz square wave -> 5 flashes / s"), RbVenueLighting::MaxFlashesPerSecondOf(Wave(5.0, 0.5), 60.0), 5.0, 1.0);
	TestEqual(TEXT("2 Hz square wave -> 2 flashes / s"), RbVenueLighting::MaxFlashesPerSecondOf(Wave(2.0, 0.5), 60.0), 2.0, 0.5);
	TestEqual(TEXT("8 % wiggle is no flash"), RbVenueLighting::MaxFlashesPerSecondOf(Wave(5.0, 0.08), 60.0), 0.0);

	// Every animated light of lights.json (the data the level generator uses).
	const FRbJson Lights = LoadJson(TEXT("Art/DiveBar/lights.json"));
	if (!TestTrue(TEXT("lights.json parsed"), Lights.IsObject()))
	{
		return false;
	}
	int32 Animated = 0;
	for (const FRbJson& V : Lights[TEXT("lights")].Array)
	{
		const FRbVenueLight Light = MakeAnimated(V);
		if (Light.Animation == ERbVenueLightAnimation::None)
		{
			continue;
		}
		++Animated;
		TArray<double> S;
		double MinGap = TNumericLimits<double>::Max(), LastEnd = -1.0;
		bool bWasActive = false;
		for (int32 I = 0; I < 600 * 60; ++I) // 10 minutes (several headlight sweeps)
		{
			const double T = I / 60.0;
			const FLinearColor C = RbVenueLighting::AnimationColor(Light, T);
			S.Add(RbVenueLighting::AnimationFactor(Light, T) * (0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B));
			bool bActive = false;
			RbVenueLighting::HeadlightYaw(Light, T, &bActive);
			if (bActive && !bWasActive && LastEnd >= 0.0)
			{
				MinGap = FMath::Min(MinGap, T - LastEnd);
			}
			if (!bActive && bWasActive)
			{
				LastEnd = T;
			}
			bWasActive = bActive;
		}
		const double Flashes = RbVenueLighting::MaxFlashesPerSecondOf(S, 60.0);
		TestTrue(*FString::Printf(TEXT("%s: %.0f flash(es) / s <= 3"), *Light.Id.ToString(), Flashes), Flashes <= RbVenueLighting::MaxFlashesPerSecond);
		if (Light.Animation == ERbVenueLightAnimation::Headlights)
		{
			TestTrue(*FString::Printf(TEXT("%s: sweeps >= 20 s apart (min gap %.1f s)"), *Light.Id.ToString(), MinGap), MinGap >= 20.0);
		}
		if (Light.Animation == ERbVenueLightAnimation::Cycle)
		{
			TestTrue(TEXT("jukebox colour cycle period >= 4 s"), Light.AnimRate >= 4.0f);
		}
	}
	TestTrue(TEXT("lights.json has animated lights (TV, jukebox, dart, headlights)"), Animated >= 4);
	// The ceiling fan: 4 blades at 40 rpm -> blade pass 2.7 Hz < 3 Hz (E24).
	TestTrue(TEXT("fan blade pass < 3 Hz"), 4.0 * GetDefault<ARbVenueInfo>()->FanRpm / 60.0 < 3.0);
	return true;
}

// --- L19 dart-machine chase: the LED fade lasts ChaseFadeSeconds at every step rate (review M2-A: it scaled with the rate squared) -----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueChaseFade, "RawBreak.Unit.Venue.T8_ChaseFade", RB_UNIT_TEST_FLAGS)

bool FRbVenueChaseFade::RunTest(const FString& Parameters)
{
	for (const double Rate : {0.5, 1.0, 1.5, 2.0})
	{
		FRbVenueLight Light;
		Light.Animation = ERbVenueLightAnimation::Chase;
		Light.AnimRate = static_cast<float>(Rate);
		Light.AnimDepth = 0.08f;
		const double Depth = Light.AnimDepth;
		const double StepStart = 1.0 / Rate; // step 1 begins: the pattern goes 0 -> 1
		const double Before = RbVenueLighting::AnimationFactor(Light, StepStart - 1e-6);
		const double Half = RbVenueLighting::AnimationFactor(Light, StepStart + 0.5 * RbVenueLighting::ChaseFadeSeconds);
		const double Done = RbVenueLighting::AnimationFactor(Light, StepStart + 1.01 * RbVenueLighting::ChaseFadeSeconds);
		TestEqual(*FString::Printf(TEXT("%.1f steps/s: low level before the step"), Rate), Before, 1.0 - 0.5 * Depth, 1e-6);
		TestEqual(*FString::Printf(TEXT("%.1f steps/s: half way through the fade after %.0f ms"), Rate, 500.0 * RbVenueLighting::ChaseFadeSeconds), Half, 1.0, 1e-6);
		TestEqual(*FString::Printf(TEXT("%.1f steps/s: fade done after %.0f ms"), Rate, 1000.0 * RbVenueLighting::ChaseFadeSeconds), Done, 1.0 + 0.5 * Depth, 1e-6);
		// smooth: the largest change between two 144 Hz frames stays below the whole step
		double MaxStep = 0.0, Prev = RbVenueLighting::AnimationFactor(Light, 0.0);
		for (int32 I = 1; I < 144 * 4; ++I)
		{
			const double Now = RbVenueLighting::AnimationFactor(Light, I / 144.0);
			MaxStep = FMath::Max(MaxStep, FMath::Abs(Now - Prev));
			Prev = Now;
		}
		TestTrue(*FString::Printf(TEXT("%.1f steps/s: largest 144 Hz frame change %.4f < 0.6 x the step %.2f"), Rate, MaxStep, Depth), MaxStep < 0.6 * Depth);
	}
	return true;
}

// --- lights.json data (VDB-T11 on the data) ----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueLightsData, "RawBreak.Unit.Venue.T11_LightsData", RB_UNIT_TEST_FLAGS)

bool FRbVenueLightsData::RunTest(const FString& Parameters)
{
	using namespace RbVenueTestsPrivate;
	const FRbJson Root = LoadJson(TEXT("Art/DiveBar/lights.json"));
	const FRbJson Layout = LoadJson(TEXT("Art/DiveBar/layout.json"));
	if (!TestTrue(TEXT("lights.json + layout.json parsed"), Root.IsObject() && Layout.IsObject()))
	{
		return false;
	}
	const FRbJson& Defaults = Root[TEXT("defaults")];
	int32 Count = 0, Bulbs = 0, Troffers = 0, Neons = 0;
	for (const FRbJson& L : Root[TEXT("lights")].Array)
	{
		const FString Id = L.GetString(TEXT("id"));
		const FString Type = L.GetString(TEXT("type"));
		const double Vol = Num(L, TEXT("vol"), Num(Defaults, TEXT("vol")));
		const bool bVolShadow = L.GetBool(TEXT("vol_shadow"), Defaults.GetBool(TEXT("vol_shadow")));
		const bool bOutside = L.GetBool(TEXT("outside"));
		const bool bEnclosed = L.GetBool(TEXT("enclosed"));
		TestTrue(*FString::Printf(TEXT("%s: flux > 0"), *Id), Num(L, TEXT("flux_lm")) > 0.0);
		TestTrue(*FString::Printf(TEXT("%s: volumetric scattering > 0 -> volumetric shadow (4.1)"), *Id), Vol <= 0.0 || bVolShadow);
		TestTrue(*FString::Printf(TEXT("%s: outside / enclosed -> scattering 0"), *Id), !(bOutside || bEnclosed) || Vol <= 0.0);
		if (Type == TEXT("point") || Type == TEXT("spot"))
		{
			TestTrue(*FString::Printf(TEXT("%s: source radius > 0 (pitfall 10)"), *Id), Num(L, TEXT("source_radius_m")) > 0.0);
		}
		else
		{
			const FRbJson& Size = L[TEXT("size_m")];
			TestTrue(*FString::Printf(TEXT("%s: rect size > 0"), *Id), Size[0].AsNumber() > 0.0 && Size[1].AsNumber() > 0.0);
		}
		const double Tubes = L.GetNumber(TEXT("tubes_lm"));
		if (L.Has(TEXT("tubes_lm")))
		{
			++Neons;
			TestEqual(*FString::Printf(TEXT("%s: neon proxy flux = tubes / pi"), *Id), Num(L, TEXT("flux_lm")), Tubes / UE_DOUBLE_PI, 0.005 * Tubes / UE_DOUBLE_PI);
			TestEqual(*FString::Printf(TEXT("%s: neon proxy specular 0"), *Id), Num(L, TEXT("specular"), 1.0), 0.0);
			double GasSum = 0.0;
			for (const TPair<FString, FRbJson>& Gas : L[TEXT("gases")].Object)
			{
				GasSum += Gas.Value.AsNumber();
			}
			TestEqual(*FString::Printf(TEXT("%s: gas fluxes sum to the tube flux"), *Id), GasSum, Tubes, 1.0);
		}
		const FString Group = L.GetString(TEXT("group"));
		if (Group == TEXT("table_lamp"))
		{
			++Bulbs;
			const FRbJson& Pos = L[TEXT("pos")];
			// Bulb = bed + lamp underside + 0.15 m above the rim (4.4: 1.01 m above the bed).
			const FRbJson& Table = Layout[TEXT("tables")][0];
			TestEqual(*FString::Printf(TEXT("%s: bulb 1.01 m above the bed"), *Id), Pos[2].AsNumber(),
				Num(Table, TEXT("bed_height_m")) + Num(Table, TEXT("lamp_underside_height_m")) + 0.15, 1e-6);
			TestEqual(*FString::Printf(TEXT("%s: 1100 lm"), *Id), Num(L, TEXT("flux_lm")), 1100.0);
		}
		if (Group == TEXT("troffers"))
		{
			++Troffers;
			const FRbJson& States = L[TEXT("states")];
			TestTrue(*FString::Printf(TEXT("%s: Lights-Up only"), *Id), Num(States, TEXT("Open"), 1.0) == 0.0 && Num(States, TEXT("LightsUp")) == 1.0);
		}
		++Count;
	}
	TestEqual(TEXT("3 table-lamp bulbs (L1-L3)"), Bulbs, 3);
	TestEqual(TEXT("4 troffers (E23)"), Troffers, 4);
	TestEqual(TEXT("5 neon proxies (N1-N5)"), Neons, 5);
	TestTrue(TEXT("the 4.2 light list"), Count >= 40);
	TestTrue(TEXT("state ramps >= 0.8 s"), Num(Root, TEXT("ramp_seconds")) >= 0.8);
	return true;
}

// --- layout.json transcription vs spec + core -------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueLayoutData, "RawBreak.Unit.Venue.T10_LayoutData", RB_UNIT_TEST_FLAGS)

bool FRbVenueLayoutData::RunTest(const FString& Parameters)
{
	using namespace RbVenueTestsPrivate;
	const FRbJson Layout = LoadJson(TEXT("Art/DiveBar/layout.json"));
	if (!TestTrue(TEXT("layout.json parsed"), Layout.IsObject()))
	{
		return false;
	}
	const FRbJson& Table = Layout[TEXT("tables")][0];
	TestEqual(TEXT("preset"), Table.GetString(TEXT("preset")), FString(TEXT("SevenFootBar")));
	TestEqual(TEXT("ball set"), Table.GetString(TEXT("ball_set")), FString(TEXT("OldBarOversizedCue")));
	const rb::TableSpec Spec = rb::GetTableSpec(RbTypes::ToCore(ERbTablePreset::SevenFootBar));
	TestEqual(TEXT("bed height == kTableSevenFootBar (VDB-T10)"), Num(Table, TEXT("bed_height_m")), Spec.BedHeight, 1e-9);
	const FRbJson& Loc = Table[TEXT("location_m")];
	TestEqual(TEXT("table actor x (2.1)"), Loc[0].AsNumber(), 13.759, 1e-9);
	TestEqual(TEXT("table actor y (2.1)"), Loc[1].AsNumber(), 5.427, 1e-9);
	TestEqual(TEXT("table actor on the floor"), Loc[2].AsNumber(), 0.0);
	// E13 outline and noses follow from the core TableSpec (2.3 is DERIVED from it).
	const FRbJson& Outer = Table[TEXT("outer")];
	const double CX = Loc[0].AsNumber(), CY = Loc[1].AsNumber();
	const double OL = 0.5 * Spec.Length + Spec.RailWidthTotal, OW = 0.5 * Spec.Width + Spec.RailWidthTotal;
	TestEqual(TEXT("outer x0"), Outer[0][0].AsNumber(), CX - OL, 0.001);
	TestEqual(TEXT("outer x1"), Outer[0][1].AsNumber(), CX + OL, 0.001);
	TestEqual(TEXT("outer y0"), Outer[1][0].AsNumber(), CY - OW, 0.001);
	TestEqual(TEXT("outer y1"), Outer[1][1].AsNumber(), CY + OW, 0.001);
	const FRbJson& Noses = Table[TEXT("noses")];
	TestEqual(TEXT("nose x0"), Noses[0][0].AsNumber(), CX - 0.5 * Spec.Length, 0.001);
	TestEqual(TEXT("nose y1"), Noses[1][1].AsNumber(), CY + 0.5 * Spec.Width, 0.001);
	TestEqual(TEXT("rail top z"), Num(Table, TEXT("rail_top_z_m")), Spec.BedHeight + Spec.RailTopZ, 0.001);
	TestEqual(TEXT("head string x"), Num(Table, TEXT("head_string_x")), CX - 0.25 * Spec.Length, 0.001);
	TestEqual(TEXT("lamp underside 0.86 m"), Num(Table, TEXT("lamp_underside_height_m")), 0.86);

	// Shell (2.2) and the tight spots of 2.5 that the cue sweep reproduces.
	const FRbJson& Shell = Layout[TEXT("shell")];
	TestEqual(TEXT("suspended ceiling 2.74 m"), Num(Shell, TEXT("ceiling_z")), 2.74);
	const FRbJson& Columns = Shell[TEXT("columns")];
	TestEqual(TEXT("3 columns"), Columns.Num(), 3);
	const FRbJson& C3 = Columns[2];
	TestEqual(TEXT("C3 x"), Num(C3, TEXT("x")), 13.72);
	// Right rail outer -> right wall 1.220 m; left rail outer -> C3 surface 1.037 m (2.5).
	TestEqual(TEXT("right rail -> right wall 1.220 m"), 7.32 - (CY + OW), 1.220, 0.002);
	TestEqual(TEXT("left rail -> C3 surface 1.037 m"), (CY - OW) - (Num(C3, TEXT("y")) + 0.5 * Num(C3, TEXT("d"))), 1.037, 0.002);
	TestEqual(TEXT("foot rail -> back wall 1.520 m"), 16.46 - (CX + OL), 1.520, 0.002);

	// Cameras: V01-V12, TH1-TH7, S0-S7 (12.2, 12.3, UX 6.3).
	const FRbJson& Cameras = Layout[TEXT("cameras")];
	TestEqual(TEXT("12 views"), Cameras[TEXT("views")].Num(), 12);
	TestEqual(TEXT("7 trailer hooks"), Cameras[TEXT("trailer")].Num(), 7);
	TestEqual(TEXT("8 menu stations"), Cameras[TEXT("menu")].Num(), 8);
	for (int32 I = 1; I <= 12; ++I)
	{
		TestEqual(TEXT("view tag"), Cameras[TEXT("views")][I - 1].GetString(TEXT("tag")), RbAssetPaths::CaptureCamera::DiveBarView(I));
	}
	for (int32 I = 0; I <= 7; ++I)
	{
		TestEqual(TEXT("menu tag"), Cameras[TEXT("menu")][I].GetString(TEXT("tag")), RbAssetPaths::CaptureCamera::MenuStation(I));
	}
	TestTrue(TEXT("audio anchors (10)"), Layout[TEXT("audio_anchors")].Num() >= 14);
	return true;
}

// --- VDB-T10 roll-off ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueRollOff, "RawBreak.Unit.Venue.T10_RollOffSeed", RB_UNIT_TEST_FLAGS)

bool FRbVenueRollOff::RunTest(const FString& Parameters)
{
	using namespace RbVenueTestsPrivate;
	const FRbJson Layout = LoadJson(TEXT("Art/DiveBar/layout.json"));
	if (!TestTrue(TEXT("layout.json parsed"), Layout.IsObject()))
	{
		return false;
	}
	const FRbJson& Table = Layout[TEXT("tables")][0];
	const int64 Seed = static_cast<int64>(Num(Table, TEXT("venue_seed")));
	const int64 Found = RbVenueTableCheck::FindRollOffSeed(ERbTablePreset::SevenFootBar, ERbBallSetPreset::OldBarOversizedCue, ERbVenueKind::DiveBar, 0, 1958, 400);
	AddInfo(FString::Printf(TEXT("first roll-off seed >= 1958: %lld (layout.json: %lld)"), Found, Seed));
	TestEqual(TEXT("layout.json venue_seed is the searched seed (first >= 1958)"), Seed, Found);
	for (const bool bFirst : {false, true})
	{
		const FRbRollOffCheck Check = RbVenueTableCheck::CheckRollOff(ERbTablePreset::SevenFootBar, ERbBallSetPreset::OldBarOversizedCue, ERbVenueKind::DiveBar, Seed, 0,
			bFirst);
		AddInfo(FString::Printf(TEXT("FirstCareerTable %d: %s"), bFirst ? 1 : 0, *Check.ToString()));
		TestTrue(*FString::Printf(TEXT("roll-off toward the jukebox, FirstCareerTable %d"), bFirst ? 1 : 0), Check.bPass);
		TestTrue(TEXT("dive-bar slope 0.5-2.5 mm/m"), Check.SlopeMmPerM >= 0.5 - 1e-9 && Check.SlopeMmPerM <= 2.5 + 1e-9);
	}
	// A seed that points elsewhere fails (the check is not vacuous).
	int32 Failing = 0;
	for (int64 S = 1958; S < 1990; ++S)
	{
		Failing += RbVenueTableCheck::CheckRollOff(ERbTablePreset::SevenFootBar, ERbBallSetPreset::OldBarOversizedCue, ERbVenueKind::DiveBar, S, 0, false).bPass ? 0 : 1;
	}
	TestTrue(TEXT("most seeds do not roll toward the jukebox (about 1 in 7 qualifies)"), Failing >= 20);
	return true;
}

// --- ARbVenueInfo ramps on real light components (VDB-T12) ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueInfoStates, "RawBreak.Unit.Venue.T12_StateRamps", RB_UNIT_TEST_FLAGS)

bool FRbVenueInfoStates::RunTest(const FString& Parameters)
{
	RbVenueTestsPrivate::FTestWorld W;
	APointLight* Lamp = W.World->SpawnActor<APointLight>();
	APointLight* Troffer = W.World->SpawnActor<APointLight>();
	Lamp->Tags.Add(RbVenueLighting::LightTag(TEXT("L1")));
	Troffer->Tags.Add(RbVenueLighting::LightTag(TEXT("T1")));
	for (APointLight* L : {Lamp, Troffer})
	{
		L->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		CastChecked<UPointLightComponent>(L->GetLightComponent())->SetIntensityUnits(ELightUnits::Lumens);
	}
	ARbVenueInfo* Info = W.World->SpawnActor<ARbVenueInfo>();
	FRbVenueLight L1;
	L1.Id = TEXT("L1");
	L1.Intensity = 1100.0f;
	L1.OpenFactor = L1.LightsUpFactor = L1.AfterHoursFactor = 1.0f;
	FRbVenueLight T1;
	T1.Id = TEXT("T1");
	T1.Intensity = 7000.0f;
	T1.OpenFactor = 0.0f;
	T1.LightsUpFactor = 1.0f;
	T1.AfterHoursFactor = 0.0f;
	Info->Lights = {L1, T1};
	TestEqual(TEXT("both lights bound by tag"), Info->BindLights(), 2);
	Info->UpdateLighting(0.0f);
	TestEqual(TEXT("Open: troffer off"), Troffer->GetLightComponent()->Intensity, 0.0f);
	TestEqual(TEXT("Open: lamp on"), Lamp->GetLightComponent()->Intensity, 1100.0f);
	Info->SetLightingState(ERbLightingState::LightsUp);
	double Time = 0.0;
	float Prev = 0.0f, MaxStep = 0.0f;
	const double Dt = 1.0 / 60.0;
	while (Info->IsRamping() && Time < 3.0)
	{
		Info->UpdateLighting(static_cast<float>(Dt));
		Time += Dt;
		const float Now = Troffer->GetLightComponent()->Intensity;
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Now - Prev));
		Prev = Now;
	}
	TestTrue(*FString::Printf(TEXT("Lights-Up ramp took >= 0.8 s (%.2f s)"), Time), Time >= 0.8 - 1e-6);
	TestEqual(TEXT("Lights-Up: troffer at full"), Troffer->GetLightComponent()->Intensity, 7000.0f, 0.5f);
	TestTrue(*FString::Printf(TEXT("no one-frame step (largest %.0f lm of 7000)"), MaxStep), MaxStep < 0.04f * 7000.0f);
	TestEqual(TEXT("the lamp stays on"), Lamp->GetLightComponent()->Intensity, 1100.0f);
	TestEqual(TEXT("state"), Info->GetLightingState(), ERbLightingState::LightsUp);
	// Look-dev group scale (rb.Venue.GroupScale): per group and "all", multiplied, 1 = as generated.
	Info->Lights[0].Group = TEXT("table_lamp");
	Info->SetGroupScale(TEXT("table_lamp"), 0.5f);
	TestEqual(TEXT("group scale 0.5 halves the lamp"), Lamp->GetLightComponent()->Intensity, 550.0f, 0.5f);
	Info->SetGroupScale(TEXT("all"), 0.0f);
	TestEqual(TEXT("group scale all 0: lamp off"), Lamp->GetLightComponent()->Intensity, 0.0f, 0.5f);
	TestEqual(TEXT("group scale all 0: troffer off"), Troffer->GetLightComponent()->Intensity, 0.0f, 0.5f);
	Info->SetGroupScale(TEXT("all"), 1.0f);
	Info->SetGroupScale(TEXT("table_lamp"), 1.0f);
	TestEqual(TEXT("group scale 1 restores the rig"), Lamp->GetLightComponent()->Intensity, 1100.0f, 0.5f);
	return true;
}

// --- night look: the venue's exposure compensation curve (incomplete mesopic adaptation) --------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueNightLook, "RawBreak.Unit.Venue.T2_NightCompensation", RB_UNIT_TEST_FLAGS)

bool FRbVenueNightLook::RunTest(const FString& Parameters)
{
	// Monotone, about -1 EV in the dark room (EV100 3-4, V01 band), untouched on the lamp-lit cloth (V04: EV100 7.8-8.4).
	double Prev = -100.0;
	for (double Ev = 0.0; Ev <= 12.0; Ev += 0.25)
	{
		const double C = RbVenueLighting::NightCompensation(Ev);
		TestTrue(*FString::Printf(TEXT("monotone at EV100 %.2f"), Ev), C >= Prev - 1e-9);
		TestTrue(*FString::Printf(TEXT("never brightens (EV100 %.2f: %.2f)"), Ev, C), C <= 0.0);
		Prev = C;
	}
	TestTrue(TEXT("entrance (EV100 3.5): 0.8-1.0 EV darker"), RbVenueLighting::NightCompensation(3.5) <= -0.8 && RbVenueLighting::NightCompensation(3.5) >= -1.0);
	TestEqual(TEXT("chin on cue (EV100 8.0): untouched"), RbVenueLighting::NightCompensation(8.0), 0.0, 1e-9);
	TestEqual(TEXT("clamped below the Eyes minimum"), RbVenueLighting::NightCompensation(-3.0), RbVenueLighting::NightCompensation(2.0), 1e-9);
	TestEqual(TEXT("UE's curve x: log2(L / 0.18) for L = 1 cd/m^2 (spec EV100 3.0)"), RbVenueLighting::UeCurveX(3.0), FMath::Log2(1.0 / 0.18), 1e-9);
	// The venue post-process carries it as UE's exposure compensation curve (x = metered EV100).
	RbVenueTestsPrivate::FTestWorld W;
	ARbVenueInfo* Info = W.World->SpawnActor<ARbVenueInfo>();
	if (!TestNotNull(TEXT("venue info"), Info))
	{
		return false;
	}
	const FPostProcessSettings& S = Info->GetPostProcess()->Settings;
	TestTrue(TEXT("curve overridden on the venue post-process"), S.bOverride_AutoExposureBiasCurve && S.AutoExposureBiasCurve != nullptr);
	if (S.AutoExposureBiasCurve)
	{
		for (const double Ev : {2.0, 3.65, 5.5, 7.2, 9.0})
		{
			// UE samples the curve at log2(L / 0.18), 0.53 EV below the spec's log2(8 L)
			const float UeX = static_cast<float>(RbVenueLighting::UeCurveX(Ev));
			TestEqual(*FString::Printf(TEXT("curve at UE's x == NightCompensation at EV100 %.2f"), Ev), static_cast<double>(S.AutoExposureBiasCurve->GetFloatValue(UeX)),
				RbVenueLighting::NightCompensation(Ev), 1e-4);
		}
	}
	return true;
}

// --- capture-camera lenses of 12.3 / ui-ux 6.3 (the generator's CameraOptics) ---------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueCameraOpticsTest, "RawBreak.Unit.Venue.T12_CameraOptics", RB_UNIT_TEST_FLAGS)

bool FRbVenueCameraOpticsTest::RunTest(const FString& Parameters)
{
	// Filmback 42.667 x 24 mm (16:9 at a 24 mm height).
	const double W = 24.0 * 16.0 / 9.0, H = 24.0;
	auto Vertical = [H](double F) { return FMath::RadiansToDegrees(2.0 * FMath::Atan(H / (2.0 * F))); };
	auto Horizontal = [W](double F) { return FMath::RadiansToDegrees(2.0 * FMath::Atan(W / (2.0 * F))); };
	FRbVenueCameraOptics O;
	TestEqual(TEXT("no override -> 0"), ARbVenueInfo::FocalLengthFor(O, W, H), 0.0);
	O.VerticalFovDeg = 30.0f;
	TestEqual(TEXT("menu station S1: vertical FOV 30 deg"), Vertical(ARbVenueInfo::FocalLengthFor(O, W, H)), 30.0, 1e-6);
	O = FRbVenueCameraOptics();
	O.HorizontalFovDeg = 69.0f;
	TestEqual(TEXT("TH-1 phone: horizontal FOV 69 deg"), Horizontal(ARbVenueInfo::FocalLengthFor(O, W, H)), 69.0, 1e-6);
	O = FRbVenueCameraOptics();
	O.FocalLength35mm = 100.0f;
	const double Expected = FMath::RadiansToDegrees(2.0 * FMath::Atan(36.0 / 200.0));
	TestEqual(TEXT("TH-3 100 mm macro: the full-frame horizontal field (20.4 deg)"), Horizontal(ARbVenueInfo::FocalLengthFor(O, W, H)), Expected, 1e-6);
	// Every lens entry of layout.json has a camera and a sane field.
	const FRbJson Layout = RbVenueTestsPrivate::LoadJson(TEXT("Art/DiveBar/layout.json"));
	int32 Lenses = 0;
	for (const TCHAR* Group : {TEXT("views"), TEXT("trailer"), TEXT("menu")})
	{
		for (const FRbJson& C : Layout[TEXT("cameras")][Group].Array)
		{
			FRbVenueCameraOptics L;
			L.VerticalFovDeg = C.GetNumber(TEXT("vfov_deg"));
			L.HorizontalFovDeg = C.GetNumber(TEXT("hfov_deg"));
			L.FocalLength35mm = C.GetNumber(TEXT("focal_mm"));
			const double F = ARbVenueInfo::FocalLengthFor(L, W, H);
			if (F > 0.0)
			{
				++Lenses;
				TestTrue(*FString::Printf(TEXT("%s: vertical field %.1f deg in 5..90"), *C.GetString(TEXT("tag")), Vertical(F)), Vertical(F) > 5.0 && Vertical(F) < 90.0);
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("lens overrides in layout.json (%d)"), Lenses), Lenses >= 13);
	return true;
}

// --- DB-0 axis test asset ------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbVenueAxisTest, "RawBreak.Unit.Venue.DB0_AxisTest", RB_UNIT_TEST_FLAGS)

bool FRbVenueAxisTest::RunTest(const FString& Parameters)
{
	const FString Package = TEXT("/Game/Generated/Venues/DiveBar/Arch/AxisTest/SM_DB_AxisTest");
	if (!FPackageName::DoesPackageExist(Package))
	{
		AddError(TEXT("SM_DB_AxisTest missing: run Tools/blender/rbbl.py run Tools/blender/divebar/db_axis_test.py, then rb_import_divebar.py"));
		return false;
	}
	UStaticMesh* Mesh = Cast<UStaticMesh>(FSoftObjectPath(Package + TEXT(".SM_DB_AxisTest")).TryLoad());
	if (!TestNotNull(TEXT("SM_DB_AxisTest"), Mesh))
	{
		return false;
	}
	const FBox Box = Mesh->GetBoundingBox();
	const FVector Size = Box.GetSize();
	TestEqual(TEXT("bounds X 100.0 +- 0.1 cm"), Size.X, 100.0, 0.1);
	TestEqual(TEXT("bounds Y 100.0 +- 0.1 cm"), Size.Y, 100.0, 0.1);
	TestEqual(TEXT("bounds Z 100.0 +- 0.1 cm"), Size.Z, 100.0, 0.1);
	TestEqual(TEXT("pivot on the floor (min z = 0)"), Box.Min.Z, 0.0, 0.05);
	TestEqual(TEXT("pivot centred in x"), Box.GetCenter().X, 0.0, 0.05);
	TestEqual(TEXT("pivot centred in y"), Box.GetCenter().Y, 0.0, 0.05);
	// The markers by complex line traces against the imported mesh (render triangles) in a test world: the relief is 1 cm high.
	// The editor builds static meshes asynchronously and a hand-ticked test world never pumps the compiling manager: finish the
	// build first, then tick so the physics scene holds the body (as RbCameraRigTests does for the table).
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("test world"), Wrapper.CreateTestWorld(EWorldType::Game) && Wrapper.BeginPlayInTestWorld()))
	{
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
#if WITH_EDITOR
	FAssetCompilingManager::Get().FinishAllCompilation();
	FAssetCompilingManager::Get().ProcessAsyncTasks();
#endif
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Actor->GetStaticMeshComponent()->RecreatePhysicsState();
	for (int32 I = 0; I < 3; ++I)
	{
		Wrapper.TickTestWorld(1.0f / 60.0f);
	}
	auto TopZ = [World](double X, double Y)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RbAxisTest), true);
		return World->LineTraceSingleByChannel(Hit, FVector(X, Y, 300.0), FVector(X, Y, -50.0), ECC_Visibility, Params) ? Hit.ImpactPoint.Z : -1.0;
	};
	auto FrontX = [World](double Y, double Z)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RbAxisTest), true);
		return World->LineTraceSingleByChannel(Hit, FVector(300.0, Y, Z), FVector(-300.0, Y, Z), ECC_Visibility, Params) ? Hit.ImpactPoint.X : -1.0;
	};
	// The head is a triangle x 0.10..0.45 m, half width 0.20 m at its base: at x = 0.30 it covers |y| < 0.086 m, the shaft only |y| < 0.06.
	TestEqual(TEXT("arrow head at +X (x +30, y -7: relief top z = 100)"), TopZ(30.0, -7.0), 100.0, 0.2);
	TestEqual(TEXT("no arrow head at -X (x -30, y -7: body top z = 99)"), TopZ(-30.0, -7.0), 99.0, 0.2);
	TestEqual(TEXT("arrow shaft along x (x -30, y 0: z = 100)"), TopZ(-30.0, 0.0), 100.0, 0.2);
	TestEqual(TEXT("+Y marker on UE +Y (x +35, y +38: z = 100)"), TopZ(35.0, 38.0), 100.0, 0.2);
	TestEqual(TEXT("mirrored import would put the marker at -Y (x +35, y -38: z = 99)"), TopZ(35.0, -38.0), 99.0, 0.2);
	int32 LetterHits = 0, BodyHits = 0;
	for (double Y = -25.0; Y <= 25.0; Y += 0.5)
	{
		for (const double Z : {35.0, 50.0, 65.0})
		{
			const double X = FrontX(Y, Z);
			LetterHits += FMath::IsNearlyEqual(X, 50.0, 0.2) ? 1 : 0;
			BodyHits += FMath::IsNearlyEqual(X, 49.0, 0.2) ? 1 : 0;
		}
	}
	TestTrue(*FString::Printf(TEXT("\"UP\" letters on the +X face (%d relief / %d body hits)"), LetterHits, BodyHits), LetterHits > 5 && BodyHits > 5);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
