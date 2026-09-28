// Render mathematics of ue5-realism-plan 13 (T1, T6, T7, T19, T20, T21, T22) and the generated UE-3 content (materials,
// MPC_RbBalls, glyph atlas, cloth weave textures of Tools/unreal/editor/rb_make_materials.py). Owner: UE-3.

#include "Math/RbCameraMath.h"
#include "Core/RbAssetPaths.h"
#include "Tests/RbTestFlags.h"

#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbRenderMathTests
{
	// Generated assets the tests read (rb_make_materials.py writes them; RbAssetPaths lists the ones C++ code loads).
	const TCHAR* const GlyphAtlas = TEXT("/Game/Generated/Materials/Textures/T_RbBallGlyphs");
	const TCHAR* const ClothWeaveNormal = TEXT("/Game/Generated/Materials/Textures/T_RbClothWeave_N");
	const TCHAR* const ClothWeaveMask = TEXT("/Game/Generated/Materials/Textures/T_RbClothWeave_M");
	const TCHAR* const MatBrass = TEXT("/Game/Generated/Materials/M_RbBrass");
	const TCHAR* const MatLeather = TEXT("/Game/Generated/Materials/M_RbLeather");
	const TCHAR* const MiClothGreen = TEXT("/Game/Generated/Materials/MI_RbCloth_Green");
	const TCHAR* const MiCueLocalSections = TEXT("/Game/Generated/Materials/MI_RbCue_LocalSections");

	FString ObjectPathOf(const TCHAR* PackagePath)
	{
		return FString::Printf(TEXT("%s.%s"), PackagePath, *FPackageName::GetShortName(PackagePath));
	}

	template <typename T>
	T* LoadGenerated(const TCHAR* PackagePath)
	{
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			return nullptr;
		}
		return LoadObject<T>(nullptr, *ObjectPathOf(PackagePath), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	bool HasParameter(const TArray<FMaterialParameterInfo>& Infos, const FName Name)
	{
		return Infos.ContainsByPredicate([Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderMathT1, "RawBreak.Unit.Render.T1_FresnelF0", RB_UNIT_TEST_FLAGS)
bool FRbRenderMathT1::RunTest(const FString& Parameters)
{
	// T1: n = 1.57 -> F0 = 0.04919; legacy Specular = F0 / 0.08 = 0.6149 (tolerance 1e-4).
	const double F0 = RbCameraMath::FresnelF0FromIor(1.57);
	TestNearlyEqual(TEXT("T1 F0(1.57)"), F0, 0.04919, 1e-4);
	TestNearlyEqual(TEXT("T1 legacy Specular"), F0 / 0.08, 0.6149, 1e-4);
	// Plan 6.4 clear coat: n = 1.5 -> 0.04; n = 1 (no interface) -> 0.
	TestNearlyEqual(TEXT("lacquer n = 1.5"), RbCameraMath::FresnelF0FromIor(1.5), 0.04, 1e-12);
	TestNearlyEqual(TEXT("n = 1"), RbCameraMath::FresnelF0FromIor(1.0), 0.0, 1e-15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderMathT6, "RawBreak.Unit.Render.T6_Ev100FromIlluminance", RB_UNIT_TEST_FLAGS)
bool FRbRenderMathT6::RunTest(const FString& Parameters)
{
	// T6: E = 520 lux, rho = 0.15 -> L = 24.828 cd/m^2, EV100 = 7.634 (tolerance 0.005).
	const double L = RbCameraMath::LuminanceFromIlluminance(520.0, 0.15);
	TestNearlyEqual(TEXT("T6 luminance"), L, 24.828, 0.005);
	TestNearlyEqual(TEXT("T6 EV100"), RbCameraMath::Ev100FromLuminance(L), 7.634, 0.005);
	// The other rows of plan 4.4 (dive bar 30 lux, arena minimum 50 lux, rho = 0.20).
	TestNearlyEqual(TEXT("dive bar EV100"), RbCameraMath::Ev100FromLuminance(RbCameraMath::LuminanceFromIlluminance(30.0, 0.20)), 3.93, 0.005);
	TestNearlyEqual(TEXT("arena EV100"), RbCameraMath::Ev100FromLuminance(RbCameraMath::LuminanceFromIlluminance(50.0, 0.20)), 4.67, 0.005);
	// EV100 = log2(8 L): 1/8 cd/m^2 is EV 0; no NaN for a black surface.
	TestNearlyEqual(TEXT("EV100(1/8) = 0"), RbCameraMath::Ev100FromLuminance(0.125), 0.0, 1e-12);
	const double Black = RbCameraMath::Ev100FromLuminance(0.0);
	TestFalse(TEXT("EV100(0) is not NaN"), FMath::IsNaN(Black));
	TestTrue(TEXT("EV100(0) is the darkest value"), Black < -1e300);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderMathT7, "RawBreak.Unit.Render.T7_ExposureCoupledGrain", RB_UNIT_TEST_FLAGS)
bool FRbRenderMathT7::RunTest(const FString& Parameters)
{
	// T7: g0 = 0.05, EV_ref = 8.0, EV = 3.9, g_max = 0.35 -> 0.20705 (tolerance 1e-4).
	TestNearlyEqual(TEXT("T7 Headcam, dark bar"), RbCameraMath::ExposureCoupledGrain(0.05, 8.0, 3.9, 0.35), 0.20705, 1e-4);
	// No extra grain when brighter than the reference; clamped to g_max (Eyes preset: 0.015 / 0.06).
	TestNearlyEqual(TEXT("over-exposed -> g0"), RbCameraMath::ExposureCoupledGrain(0.05, 8.0, 11.0, 0.35), 0.05, 1e-12);
	TestNearlyEqual(TEXT("clamped to g_max"), RbCameraMath::ExposureCoupledGrain(0.015, 8.0, 0.0, 0.06), 0.06, 1e-12);
	TestNearlyEqual(TEXT("one EV under -> sqrt(2)"), RbCameraMath::ExposureCoupledGrain(0.015, 8.0, 7.0, 0.06), 0.015 * FMath::Sqrt(2.0), 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderMathT19, "RawBreak.Unit.Render.T19_BallOcclusion", RB_UNIT_TEST_FLAGS)
bool FRbRenderMathT19::RunTest(const FString& Parameters)
{
	// T19: rho = 0, R, 2R -> 1.0, 0.353553, 0.089443 (tolerance 1e-6), for the ball radius in cm (pitfall 24).
	const double R = 2.8575;
	TestNearlyEqual(TEXT("T19 Occ(0)"), RbCameraMath::BallOcclusion(0.0, R), 1.0, 1e-6);
	TestNearlyEqual(TEXT("T19 Occ(R)"), RbCameraMath::BallOcclusion(R, R), 0.353553, 1e-6);
	TestNearlyEqual(TEXT("T19 Occ(2R)"), RbCameraMath::BallOcclusion(2.0 * R, R), 0.089443, 1e-6);
	// Scale invariance (metres or centimetres give the same value) and the general form the cloth shader evaluates
	// (RbBallOcclusion.ush): (R / d)^2 cos(theta) with the cloth normal up equals the planar formula.
	TestNearlyEqual(TEXT("unit invariant"), RbCameraMath::BallOcclusion(0.03, 0.028575), RbCameraMath::BallOcclusion(3.0, 2.8575), 1e-12);
	for (const double Rho : {0.0, 0.5, 1.0, 2.8575, 7.0, 20.0})
	{
		const double D = FMath::Sqrt(Rho * Rho + R * R);
		const double General = (R / D) * (R / D) * (R / D);
		TestNearlyEqual(*FString::Printf(TEXT("general form at rho = %.2f"), Rho), RbCameraMath::BallOcclusion(Rho, R), General, 1e-12);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderMathT20, "RawBreak.Unit.Render.T20_SphereTessellation", RB_UNIT_TEST_FLAGS)
bool FRbRenderMathT20::RunTest(const FString& Parameters)
{
	// T20: R = 28.575 mm, N = 32 / 64 -> 0.13760 / 0.03442 mm (tolerance 1e-5 mm).
	TestNearlyEqual(TEXT("T20 N = 32"), RbCameraMath::SphereTessellationError(28.575, 32), 0.13760, 1e-5);
	TestNearlyEqual(TEXT("T20 N = 64"), RbCameraMath::SphereTessellationError(28.575, 64), 0.03442, 1e-5);
	// The 128-segment ball of UE-2 stays below the T20 bound; fewer than 3 segments is no polygon.
	TestTrue(TEXT("N = 128 below the N = 64 bound"), RbCameraMath::SphereTessellationError(28.575, 128) < 0.035);
	TestNearlyEqual(TEXT("N < 3 -> R"), RbCameraMath::SphereTessellationError(28.575, 2), 28.575, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderMathT21, "RawBreak.Unit.Render.T21_LuxProbe", RB_UNIT_TEST_FLAGS)
bool FRbRenderMathT21::RunTest(const FString& Parameters)
{
	// T21: three point lamps I = 600 cd, h = 1.0 m, x = -0.85 / 0 / 0.85, y = 0 -> E(0, 0) = 1130.81 lux;
	// E(1.27, 0.635) = 458.66 lux (tolerance 0.05 lux).
	const RbCameraMath::FPointLamp Lamps[3] = {
		{FVector3d(-0.85, 0.0, 1.0), 600.0},
		{FVector3d(0.0, 0.0, 1.0), 600.0},
		{FVector3d(0.85, 0.0, 1.0), 600.0},
	};
	TestNearlyEqual(TEXT("T21 centre"), RbCameraMath::IlluminanceAt(FVector3d(0.0, 0.0, 0.0), Lamps, 3), 1130.81, 0.05);
	TestNearlyEqual(TEXT("T21 bed corner"), RbCameraMath::IlluminanceAt(FVector3d(1.27, 0.635, 0.0), Lamps, 3), 458.66, 0.05);
	// A source at or below the point's plane adds nothing; no lamps -> 0.
	const RbCameraMath::FPointLamp Below[1] = {{FVector3d(0.2, 0.0, -0.1), 600.0}};
	TestNearlyEqual(TEXT("lamp below the bed"), RbCameraMath::IlluminanceAt(FVector3d::ZeroVector, Below, 1), 0.0, 1e-15);
	TestNearlyEqual(TEXT("no lamps"), RbCameraMath::IlluminanceAt(FVector3d::ZeroVector, nullptr, 0), 0.0, 1e-15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderMathT22, "RawBreak.Unit.Render.T22_LambertianLamp", RB_UNIT_TEST_FLAGS)
bool FRbRenderMathT22::RunTest(const FString& Parameters)
{
	// T22: Phi = 1600 lm, h = 1.0 m straight below -> I0 = 509.30 cd, E = 509.30 lux (tolerance 0.01).
	const double I0 = RbCameraMath::LambertianIntensityFromFlux(1600.0);
	TestNearlyEqual(TEXT("T22 I0"), I0, 509.30, 0.01);
	const RbCameraMath::FPointLamp Lamp[1] = {{FVector3d(0.0, 0.0, 1.0), I0}};
	TestNearlyEqual(TEXT("T22 E straight below"), RbCameraMath::IlluminanceAt(FVector3d::ZeroVector, Lamp, 1), 509.30, 0.01);
	// Inverse square: twice the height, a quarter of the illuminance.
	const RbCameraMath::FPointLamp High[1] = {{FVector3d(0.0, 0.0, 2.0), I0}};
	TestNearlyEqual(TEXT("inverse square"), RbCameraMath::IlluminanceAt(FVector3d::ZeroVector, High, 1), 509.30 / 4.0, 0.01);
	return true;
}

// ---- generated content (rb_make_materials.py) ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderGeneratedMaterials, "RawBreak.Unit.Render.GeneratedMaterials", RB_UNIT_TEST_FLAGS)
bool FRbRenderGeneratedMaterials::RunTest(const FString& Parameters)
{
	using namespace RbRenderMathTests;
	// Every material of RbAssetPaths exists and is a Substrate material (front material connected).
	const TCHAR* const Materials[] = {
		RbAssetPaths::MatBall, RbAssetPaths::MatCloth, RbAssetPaths::MatRailWood, RbAssetPaths::MatCushionRubber,
		RbAssetPaths::MatPocketLiner, RbAssetPaths::MatSight, RbAssetPaths::MatCue, RbAssetPaths::MatRoomWall,
		RbAssetPaths::MatRoomFloor, RbAssetPaths::MatLampDiffuser, MatBrass, MatLeather,
	};
	for (const TCHAR* Path : Materials)
	{
		UMaterial* Material = LoadGenerated<UMaterial>(Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s exists"), Path), Material))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s is Substrate"), Path), Material->HasSubstrateFrontMaterialConnected());
	}

	// M_RbBall exposes the parameters ARbBallSet writes (RbAssetPaths::Param) plus the layout parameters.
	if (UMaterial* Ball = LoadGenerated<UMaterial>(RbAssetPaths::MatBall))
	{
		TArray<FMaterialParameterInfo> Scalars;
		TArray<FGuid> Ids;
		Ball->GetAllScalarParameterInfo(Scalars, Ids);
		TArray<FMaterialParameterInfo> Vectors;
		Ball->GetAllVectorParameterInfo(Vectors, Ids);
		TArray<FMaterialParameterInfo> Textures;
		Ball->GetAllTextureParameterInfo(Textures, Ids);
		for (const FName Name : {RbAssetPaths::Param::BallNumber, RbAssetPaths::Param::ExposureTime, RbAssetPaths::Param::BallRadiusCm,
				 FName(TEXT("StripeHalfAngleDeg")), FName(TEXT("CircleHalfAngleDeg")), FName(TEXT("CueBallDots"))})
		{
			TestTrue(*FString::Printf(TEXT("M_RbBall scalar %s"), *Name.ToString()), HasParameter(Scalars, Name));
		}
		for (const FName Name : {RbAssetPaths::Param::BallColor, RbAssetPaths::Param::BallOmegaLocal})
		{
			TestTrue(*FString::Printf(TEXT("M_RbBall vector %s"), *Name.ToString()), HasParameter(Vectors, Name));
		}
		TestTrue(TEXT("M_RbBall glyph atlas parameter"), HasParameter(Textures, FName(TEXT("GlyphAtlas"))));
	}

	// Table parts are baked as Nanite meshes (UE-1): their materials must be allowed on Nanite, otherwise the renderer
	// substitutes the default material (a strict capture fails on that).
	for (const TCHAR* Path : {RbAssetPaths::MatCloth, RbAssetPaths::MatRailWood, RbAssetPaths::MatPocketLiner, RbAssetPaths::MatSight,
			 RbAssetPaths::MatCushionRubber, MatBrass, MatLeather})
	{
		if (const UMaterial* Material = LoadGenerated<UMaterial>(Path))
		{
			TestTrue(*FString::Printf(TEXT("%s used with Nanite"), Path), Material->GetUsageByFlag(MATUSAGE_Nanite));
		}
	}

	// MPC_RbBalls: Ball00 .. Ball15 (vector) + BallRadiusCm (scalar) with the defaults of an empty table.
	UMaterialParameterCollection* Mpc = LoadGenerated<UMaterialParameterCollection>(RbAssetPaths::BallMpc);
	if (TestNotNull(TEXT("MPC_RbBalls exists"), Mpc))
	{
		for (int32 I = 0; I < 16; ++I)
		{
			const FCollectionVectorParameter* P = Mpc->VectorParameters.FindByPredicate(
				[Name = RbAssetPaths::Param::MpcBall(I)](const FCollectionVectorParameter& V) { return V.ParameterName == Name; });
			if (TestNotNull(*FString::Printf(TEXT("MPC Ball%02d"), I), P))
			{
				TestEqual(*FString::Printf(TEXT("MPC Ball%02d hidden by default"), I), P->DefaultValue.A, 0.0f);
			}
		}
		const FCollectionScalarParameter* Radius = Mpc->ScalarParameters.FindByPredicate(
			[](const FCollectionScalarParameter& S) { return S.ParameterName == RbAssetPaths::Param::BallRadiusCm; });
		if (TestNotNull(TEXT("MPC BallRadiusCm"), Radius))
		{
			TestNearlyEqual(TEXT("MPC BallRadiusCm default = 57.15 mm / 2"), Radius->DefaultValue, 2.8575f, 1e-4f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderGlyphAtlas, "RawBreak.Unit.Render.GlyphAtlas", RB_UNIT_TEST_FLAGS)
bool FRbRenderGlyphAtlas::RunTest(const FString& Parameters)
{
#if WITH_EDITORONLY_DATA
	using namespace RbRenderMathTests;
	// 4 x 4 cells, cell n = the label of ball n inscribed in the number circle (cell 0 = cue ball, empty). Single-channel
	// signed distance field: 0.5 on the glyph outline, > 0.5 inside the ink.
	UTexture2D* Atlas = LoadGenerated<UTexture2D>(GlyphAtlas);
	if (!TestNotNull(TEXT("T_RbBallGlyphs exists"), Atlas))
	{
		return false;
	}
	TestFalse(TEXT("atlas is linear (distance field, not colour)"), Atlas->SRGB);
	FTextureSource& Source = Atlas->Source;
	const int32 W = Source.GetSizeX();
	const int32 H = Source.GetSizeY();
	TestEqual(TEXT("atlas is square"), W, H);
	TestTrue(TEXT("atlas >= 1024 px (256 px per cell)"), W >= 1024);
	TArray64<uint8> Mip;
	if (!TestTrue(TEXT("atlas source readable"), Source.GetMipData(Mip, 0)) || W < 4 || H < 4)
	{
		return false;
	}
	const int32 Bpp = Source.GetBytesPerPixel();
	const ETextureSourceFormat Format = Source.GetFormat();
	TestTrue(TEXT("atlas source is 8-bit grey or BGRA8"), Format == TSF_G8 || Format == TSF_BGRA8);
	const int32 Channel = Format == TSF_BGRA8 ? 2 : 0; // red of BGRA8

	const int32 Cell = W / 4;
	auto Ink = [&](int32 CellIndex, float U0, float U1, float V0, float V1) -> float
	{
		// Fraction of pixels inside the ink in the sub-rectangle [U0, U1] x [V0, V1] (cell units, v down) of a cell.
		const int32 Cx = (CellIndex % 4) * Cell;
		const int32 Cy = (CellIndex / 4) * Cell;
		int32 Inside = 0;
		int32 Count = 0;
		for (int32 Y = Cy + FMath::FloorToInt32(V0 * Cell); Y < Cy + FMath::FloorToInt32(V1 * Cell); ++Y)
		{
			for (int32 X = Cx + FMath::FloorToInt32(U0 * Cell); X < Cx + FMath::FloorToInt32(U1 * Cell); ++X)
			{
				Inside += Mip[(static_cast<int64>(Y) * W + X) * Bpp + Channel] > 127 ? 1 : 0;
				++Count;
			}
		}
		return Count > 0 ? static_cast<float>(Inside) / Count : 0.0f;
	};

	for (int32 N = 1; N <= 15; ++N)
	{
		const float Coverage = Ink(N, 0.0f, 1.0f, 0.0f, 1.0f);
		TestTrue(*FString::Printf(TEXT("cell %d has ink (%.3f)"), N, Coverage), Coverage > 0.04f && Coverage < 0.40f);
		// The label stays inside the number circle (inscribed in the cell): no ink in the cell corners.
		const float Corner = Ink(N, 0.0f, 0.12f, 0.0f, 0.12f) + Ink(N, 0.88f, 1.0f, 0.88f, 1.0f);
		TestEqual(*FString::Printf(TEXT("cell %d corners are empty"), N), Corner, 0.0f);
		// Centred: the ink centroid lies near the circle centre (optical centring; a "1" is not symmetric).
		double Sx = 0.0, Sy = 0.0, Sw = 0.0;
		for (int32 Y = 0; Y < Cell; ++Y)
		{
			for (int32 X = 0; X < Cell; ++X)
			{
				const int64 Index = (static_cast<int64>((N / 4) * Cell + Y) * W + (N % 4) * Cell + X) * Bpp + Channel;
				if (Mip[Index] > 127)
				{
					Sx += (X + 0.5) / Cell;
					Sy += (Y + 0.5) / Cell;
					Sw += 1.0;
				}
			}
		}
		const double Cx = Sw > 0.0 ? Sx / Sw : 0.0;
		const double Cy = Sw > 0.0 ? Sy / Sw : 0.0;
		TestTrue(*FString::Printf(TEXT("cell %d centred (centroid %.3f, %.3f)"), N, Cx, Cy), FMath::Abs(Cx - 0.5) < 0.08 && FMath::Abs(Cy - 0.5) < 0.08);
	}
	// 6 and 9 are underscored (WPA): ink in a band below the digits, which the other single digits do not have.
	auto Underscore = [&](int32 N) { return Ink(N, 0.30f, 0.70f, 0.765f, 0.84f); };
	TestTrue(TEXT("6 underscored"), Underscore(6) > 0.25f);
	TestTrue(TEXT("9 underscored"), Underscore(9) > 0.25f);
	for (const int32 N : {1, 2, 3, 4, 5, 7, 8})
	{
		TestTrue(*FString::Printf(TEXT("%d not underscored"), N), Underscore(N) < 0.05f);
	}
	// Cell 0 (the cue ball) carries no label.
	TestEqual(TEXT("cell 0 is empty"), Ink(0, 0.0f, 1.0f, 0.0f, 1.0f), 0.0f);
	return true;
#else
	return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderClothTextures, "RawBreak.Unit.Render.ClothWeaveTextures", RB_UNIT_TEST_FLAGS)
bool FRbRenderClothTextures::RunTest(const FString& Parameters)
{
	using namespace RbRenderMathTests;
	// Plan 6.3 / pitfall 15: fine weave normal map with mips, anisotropic filtering and the normal variance composited
	// into the roughness channel of the mask texture (normal-to-roughness on the mips).
	UTexture2D* Normal = LoadGenerated<UTexture2D>(ClothWeaveNormal);
	UTexture2D* Mask = LoadGenerated<UTexture2D>(ClothWeaveMask);
	if (!TestNotNull(TEXT("T_RbClothWeave_N exists"), Normal) || !TestNotNull(TEXT("T_RbClothWeave_M exists"), Mask))
	{
		return false;
	}
	TestEqual(TEXT("weave normal is a normal map"), Normal->CompressionSettings.GetValue(), TC_Normalmap);
	TestEqual(TEXT("weave normal: anisotropic filtering"), static_cast<int32>(Normal->Filter.GetValue()), static_cast<int32>(TF_Default));
	TestFalse(TEXT("mask is linear"), Mask->SRGB);
#if WITH_EDITORONLY_DATA
	// Import settings (editor-only data; the game target compiles the test without them).
	TestNotEqual(TEXT("weave normal has mips"), Normal->MipGenSettings.GetValue(), TMGS_NoMipmaps);
	TestTrue(TEXT("mask composites the weave normal"), Mask->GetCompositeTexture() == Normal);
	TestEqual(TEXT("normal variance -> roughness in green"), Mask->CompositeTextureMode.GetValue(), CTM_NormalRoughnessToGreen);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRenderGeneratedInstances, "RawBreak.Unit.Render.GeneratedInstances", RB_UNIT_TEST_FLAGS)
bool FRbRenderGeneratedInstances::RunTest(const FString& Parameters)
{
	using namespace RbRenderMathTests;
	// Classic green cloth: an instance of M_RbCloth that only overrides the dye colour.
	UMaterialInstanceConstant* Green = LoadGenerated<UMaterialInstanceConstant>(MiClothGreen);
	if (TestNotNull(TEXT("MI_RbCloth_Green exists"), Green))
	{
		TestTrue(TEXT("MI_RbCloth_Green parent is M_RbCloth"),
			Green->Parent && Green->Parent->GetPathName() == ObjectPathOf(RbAssetPaths::MatCloth));
		FLinearColor Color;
		TestTrue(TEXT("MI_RbCloth_Green overrides ClothColor"),
			Green->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(TEXT("ClothColor"))), Color, true));
		TestTrue(TEXT("green cloth is green"), Color.G > Color.R && Color.G > Color.B);
	}

#if WITH_EDITOR
	// Cue: UE-4's mesh carries the section index in UV1.x (RbCueMeshBuilder.h), so M_RbCue reads it by default; the
	// local-sections instance (dev swatches on meshes without UV1) switches to the sections along local X.
	UMaterial* Cue = LoadGenerated<UMaterial>(RbAssetPaths::MatCue);
	if (TestNotNull(TEXT("M_RbCue exists"), Cue))
	{
		bool bFromUV1 = false;
		FGuid Id;
		TestTrue(TEXT("M_RbCue has the static switch SectionsFromUV1"),
			Cue->GetStaticSwitchParameterDefaultValue(FHashedMaterialParameterInfo(FName(TEXT("SectionsFromUV1"))), bFromUV1, Id));
		TestTrue(TEXT("M_RbCue reads the sections from UV1 by default"), bFromUV1);
	}
	UMaterialInstanceConstant* Local = LoadGenerated<UMaterialInstanceConstant>(MiCueLocalSections);
	if (TestNotNull(TEXT("MI_RbCue_LocalSections exists"), Local))
	{
		TestTrue(TEXT("MI_RbCue_LocalSections parent is M_RbCue"),
			Local->Parent && Local->Parent->GetPathName() == ObjectPathOf(RbAssetPaths::MatCue));
		bool bFromUV1 = true;
		FGuid Id;
		TestTrue(TEXT("MI_RbCue_LocalSections overrides SectionsFromUV1"),
			Local->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(FName(TEXT("SectionsFromUV1"))), bFromUV1, Id, true));
		TestFalse(TEXT("MI_RbCue_LocalSections uses the local X sections"), bFromUV1);
	}
#endif
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
