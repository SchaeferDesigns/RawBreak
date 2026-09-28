#include "Game/RbTestRoom.h"

#include "RawBreak.h"
#include "Camera/RbCameraModel.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbTypes.h"
#include "Dev/RbLookDevCamera.h"
#include "Game/RbGameMode.h"
#include "Table/RbTable.h"

#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

#include "rb/Equipment/TableSpec.h"

// Owner: UE-8.

const FName ARbTestRoom::GeneratedComponentTag(TEXT("RbTestRoomPart"));

namespace RbTestRoomPrivate
{
	const TCHAR* const CubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");         // 100 cm, pivot at the centre
	const TCHAR* const CylinderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); // 100 cm high, 100 cm diameter, centred
	const TCHAR* const BasicMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");                  // Color, Roughness
	const TCHAR* const EmissiveMaterial = TEXT("/Engine/ArtTools/RenderToTexture/Materials/Debug/M_Emissive_Color.M_Emissive_Color"); // unlit, Color

	// Loads an asset only if its package exists (no "failed to find" warnings before the generators ran).
	template <class T>
	T* LoadIfExists(const TCHAR* ObjectPath)
	{
		const FSoftObjectPath Path(ObjectPath);
		if (Path.IsNull() || !FPackageName::DoesPackageExist(Path.GetLongPackageName()))
		{
			return nullptr;
		}
		return Cast<T>(Path.TryLoad());
	}

	// "/Game/Generated/Materials/M_RbRoomWall" -> object path "/Game/Generated/Materials/M_RbRoomWall.M_RbRoomWall".
	FString ObjectPathOf(const TCHAR* PackagePath)
	{
		const FString Package(PackagePath);
		return Package.Contains(TEXT(".")) ? Package : Package + TEXT(".") + FPackageName::GetShortName(Package);
	}

	rb::TableSpec SpecOf(const ARbTable* Table)
	{
		if (Table && Table->HasContext())
		{
			return Table->GetContext().Spec;
		}
		return rb::GetTableSpec(Table ? RbTypes::ToCore(Table->Preset) : rb::TablePreset::NineFootPro);
	}

	// Luminance [cd/m^2] of a one-sided Lambertian rect emitter from UE's photometric units (RectLightComponent.cpp: lumens
	// -> Phi / pi over the area, candelas -> I / area, nits as is). Other units are not photometric: 0.
	double RectLuminance(const URectLightComponent& Light)
	{
		const double AreaM2 = FMath::Max(1e-8, Light.SourceWidth * Light.SourceHeight * 1e-4);
		switch (Light.IntensityUnits)
		{
		case ELightUnits::Lumens: return Light.Intensity / (UE_DOUBLE_PI * AreaM2);
		case ELightUnits::Candelas: return Light.Intensity / AreaM2;
		case ELightUnits::Nits: return Light.Intensity;
		default: return 0.0;
		}
	}

	// UE's attenuation-radius window of inverse-square lights: Square(saturate(1 - Square(d^2 / r^2))).
	double RadiusWindow(double DistanceSqCm, double RadiusCm)
	{
		if (RadiusCm <= 0.0)
		{
			return 0.0;
		}
		const double Ratio = DistanceSqCm / (RadiusCm * RadiusCm);
		const double W = FMath::Clamp(1.0 - Ratio * Ratio, 0.0, 1.0);
		return W * W;
	}

	// Visible part of a rect light's emitter seen from a shading point, port of GetRect() in Engine/Shaders/Private/RectLight.ush
	// (5.8). Input: the light centre relative to the shading point in the light's axes (S.xy along the width / height axes,
	// S.z = depth of the point in front of the light), half extents. Output: emitter coordinates (a, b) of the visible region.
	bool VisibleEmitterRect(const FVector3d& S, const FVector2d& Extent, double BarnCosAngle, double BarnLength, FVector2d& OutMin, FVector2d& OutMax)
	{
		FVector2d MinXY(-Extent.X, -Extent.Y);
		FVector2d MaxXY(Extent.X, Extent.Y);
		if (BarnCosAngle > 0.035 && BarnLength > 0.0)
		{
			const double CosTheta = BarnCosAngle;
			const double SinTheta = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosTheta * CosTheta));
			const double BarnDepth = FMath::Min(S.Z, CosTheta * BarnLength);
			const double Ratio = BarnDepth / FMath::Max(0.0001, CosTheta * BarnLength);
			const double DB = SinTheta * BarnLength * Ratio;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const double Coordinate = Axis == 0 ? S.X : S.Y;
				const double E = Axis == 0 ? Extent.X : Extent.Y;
				const double Sign = Coordinate > 0.0 ? 1.0 : (Coordinate < 0.0 ? -1.0 : 0.0);
				const double Clamped = FMath::Max(FMath::Abs(Coordinate), E + DB);
				const double SProj = Clamped - (E + DB);
				const double CosEta = FMath::Max(S.Z - BarnDepth, 0.001);
				const double DS = BarnDepth * SProj / CosEta;
				const double Lo = FMath::Clamp(-E + (DS - DB) * FMath::Max(0.0, -Sign), -E, E);
				const double Hi = FMath::Clamp(E - (DS - DB) * FMath::Max(0.0, Sign), -E, E);
				(Axis == 0 ? MinXY.X : MinXY.Y) = Lo;
				(Axis == 0 ? MaxXY.X : MaxXY.Y) = Hi;
			}
		}
		// The shader's rect is centred at S - offset: in emitter coordinates the visible region is [-Max, -Min].
		OutMin = FVector2d(-MaxXY.X, -MaxXY.Y);
		OutMax = FVector2d(-MinXY.X, -MinXY.Y);
		return OutMax.X > OutMin.X && OutMax.Y > OutMin.Y;
	}

	double RectIlluminance(const URectLightComponent& Light, const FVector& Point, const FVector& Normal)
	{
		const double L = RectLuminance(Light);
		if (L <= 0.0)
		{
			return 0.0;
		}
		const FTransform& T = Light.GetComponentTransform();
		const FVector3d O = T.GetLocation();
		const FVector3d Forward = T.GetUnitAxis(EAxis::X); // emission direction
		const FVector3d WidthAxis = T.GetUnitAxis(EAxis::Y);  // SourceWidth
		const FVector3d HeightAxis = T.GetUnitAxis(EAxis::Z); // SourceHeight
		const FVector3d ToLight = O - FVector3d(Point);
		const double Depth = FVector3d::DotProduct(Forward, FVector3d(Point) - O);
		if (Depth <= 0.0)
		{
			return 0.0; // behind the emitter
		}
		const FVector3d S(FVector3d::DotProduct(WidthAxis, ToLight), FVector3d::DotProduct(HeightAxis, ToLight), Depth);
		const FVector2d Extent(0.5 * Light.SourceWidth, 0.5 * Light.SourceHeight);
		FVector2d Min, Max;
		if (!VisibleEmitterRect(S, Extent, FMath::Cos(FMath::DegreesToRadians(static_cast<double>(Light.BarnDoorAngle))), Light.BarnDoorLength, Min, Max))
		{
			return 0.0;
		}
		// Midpoint rule over the visible emitter [cm -> m]: E = L * sum cos(e) cos(r) dA / d^2.
		constexpr int32 N = 12;
		const double DA = (Max.X - Min.X) * (Max.Y - Min.Y) / (N * N) * 1e-4;
		double Sum = 0.0;
		for (int32 I = 0; I < N; ++I)
		{
			const double A = Min.X + (I + 0.5) * (Max.X - Min.X) / N;
			for (int32 J = 0; J < N; ++J)
			{
				const double B = Min.Y + (J + 0.5) * (Max.Y - Min.Y) / N;
				const FVector3d Q = O + WidthAxis * A + HeightAxis * B;
				const FVector3d D = (FVector3d(Point) - Q) * 0.01; // emitter -> point [m]
				const double D2 = D.SizeSquared();
				if (D2 <= 1e-12)
				{
					continue;
				}
				const double InvD = 1.0 / FMath::Sqrt(D2);
				const double CosE = FVector3d::DotProduct(D, Forward) * InvD;
				const double CosR = -FVector3d::DotProduct(D, FVector3d(Normal)) * InvD;
				if (CosE > 0.0 && CosR > 0.0)
				{
					Sum += CosE * CosR / D2;
				}
			}
		}
		return L * Sum * DA * RadiusWindow(ToLight.SizeSquared(), Light.AttenuationRadius);
	}

	double PointIlluminance(const UPointLightComponent& Light, const FVector& Point, const FVector& Normal)
	{
		if (!Light.bUseInverseSquaredFalloff)
		{
			return 0.0; // exponent falloff is not photometric
		}
		const USpotLightComponent* Spot = Cast<USpotLightComponent>(&Light);
		double Intensity = 0.0; // [cd]
		switch (Light.IntensityUnits)
		{
		case ELightUnits::Lumens:
			Intensity = Spot ? Light.Intensity / (2.0 * UE_DOUBLE_PI * (1.0 - Spot->GetCosHalfConeAngle())) : Light.Intensity / (4.0 * UE_DOUBLE_PI);
			break;
		case ELightUnits::Candelas: Intensity = Light.Intensity; break;
		default: return 0.0;
		}
		const FVector3d ToPoint = FVector3d(Point) - FVector3d(Light.GetComponentLocation());
		const double D2 = (ToPoint * 0.01).SizeSquared();
		if (D2 <= 1e-12)
		{
			return 0.0;
		}
		const FVector3d Dir = ToPoint.GetSafeNormal();
		const double CosR = -FVector3d::DotProduct(Dir, FVector3d(Normal));
		if (CosR <= 0.0)
		{
			return 0.0;
		}
		double Cone = 1.0;
		if (Spot)
		{
			const double CosOuter = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(Spot->OuterConeAngle), 0.0, 89.0)));
			const double CosInner = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(Spot->InnerConeAngle), 0.0, 89.0)));
			const double InvDiff = 1.0 / FMath::Max(CosInner - CosOuter, 1e-4);
			const double C = FMath::Clamp((FVector3d::DotProduct(Dir, FVector3d(Spot->GetForwardVector())) - CosOuter) * InvDiff, 0.0, 1.0);
			Cone = C * C;
		}
		return Intensity * CosR / D2 * Cone * RadiusWindow(ToPoint.SizeSquared(), Light.AttenuationRadius);
	}

	struct FLuxAccumulator
	{
		double Min = TNumericLimits<double>::Max(), Max = 0.0, Sum = 0.0;
		int32 Count = 0;
		void Add(double V)
		{
			Min = FMath::Min(Min, V);
			Max = FMath::Max(Max, V);
			Sum += V;
			++Count;
		}
		void Write(double& OutMin, double& OutMax, double& OutAvg, int32& OutCount) const
		{
			OutMin = Count ? Min : 0.0;
			OutMax = Max;
			OutAvg = Count ? Sum / Count : 0.0;
			OutCount = Count;
		}
	};

	// Centre-weighted metering mask (plan 4.4): a Gaussian weight around the image centre (sigma 0.5 of the half extent) over a
	// floor, so the surroundings still count. Linear greyscale, 64 x 64, stretched to the viewport.
	UTexture2D* MakeCentreWeightedMask()
	{
		constexpr int32 Size = 64;
		constexpr double Sigma = 0.5, Floor = 0.25;
		TArray64<uint8> Pixels;
		Pixels.SetNumUninitialized(int64(Size) * Size * 4);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const double U = (X + 0.5) / Size * 2.0 - 1.0, V = (Y + 0.5) / Size * 2.0 - 1.0;
				const double W = Floor + (1.0 - Floor) * FMath::Exp(-(U * U + V * V) / (2.0 * Sigma * Sigma));
				const uint8 Byte = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(W * 255.0), 0, 255));
				uint8* P = &Pixels[(int64(Y) * Size + X) * 4];
				P[0] = Byte; // B
				P[1] = Byte; // G
				P[2] = Byte; // R
				P[3] = 255;
			}
		}
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None, TConstArrayView64<uint8>(Pixels));
		if (Texture)
		{
			Texture->SRGB = false;
			Texture->Filter = TF_Bilinear;
			Texture->AddressX = TA_Clamp;
			Texture->AddressY = TA_Clamp;
			Texture->UpdateResource();
		}
		return Texture;
	}

	void AddLine(FString& Report, bool& bAllOk, bool bOk, const FString& Check, const FString& Detail)
	{
		bAllOk &= bOk;
		Report += FString::Printf(TEXT("%s %s: %s\n"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *Check, *Detail);
	}
}

FString FRbLuxReport::ToString() const
{
	return FString::Printf(TEXT("bed %.0f..%.0f (avg %.0f, %d pts), rails %.0f..%.0f (avg %.0f, %d pts), max/min %.2f, floor %.0f..%.0f (avg %.0f, %d pts)"),
		BedMin, BedMax, BedAvg, BedPoints, RailMin, RailMax, RailAvg, RailPoints, TableUniformity(), FloorMin, FloorMax, FloorAvg, FloorPoints);
}

ARbTestRoom::ARbTestRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static); // the shell and fixture meshes are Static (distance fields, Lumen cards)
	SetRootComponent(Root);
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
}

ARbTable* ARbTestRoom::FindTable() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ARbTable> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

double ARbTestRoom::GetLampEmitterHeight() const
{
	return LampHeightAboveBed + FMath::Cos(FMath::DegreesToRadians(LampLouvreAngleDeg)) * LampLouvreLength;
}

FTransform ARbTestRoom::GetTableFrame() const
{
	if (const ARbTable* Table = FindTable())
	{
		const rb::TableSpec Spec = RbTestRoomPrivate::SpecOf(Table);
		const FVector Floor = Table->GetActorLocation();
		return FTransform(FRotator(0.0, Table->GetActorRotation().Yaw, 0.0), Floor + FVector(0.0, 0.0, 100.0 * Spec.BedHeight));
	}
	return FTransform(FRotator(0.0, GetActorRotation().Yaw, 0.0), GetActorLocation() + FVector(0.0, 0.0, BedHeight));
}

void ARbTestRoom::DestroyGeneratedComponents()
{
	TInlineComponentArray<UActorComponent*> Components(this);
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->ComponentHasTag(GeneratedComponentTag))
		{
			Component->DestroyComponent();
		}
	}
	Surfaces.Reset();
	FixtureMeshes.Reset();
	LampLights.Reset();
	AmbientLights.Reset();
	TransientMaterials.Reset();
}

UMaterialInterface* ARbTestRoom::SurfaceMaterial(const TCHAR* GeneratedPath, const FLinearColor& FallbackAlbedo, float FallbackRoughness)
{
	if (GeneratedPath)
	{
		if (UMaterialInterface* Generated = RbTestRoomPrivate::LoadIfExists<UMaterialInterface>(*RbTestRoomPrivate::ObjectPathOf(GeneratedPath)))
		{
			return Generated;
		}
	}
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, RbTestRoomPrivate::BasicMaterial);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
	Mid->SetVectorParameterValue(TEXT("Color"), FallbackAlbedo);
	Mid->SetScalarParameterValue(TEXT("Roughness"), FallbackRoughness);
	TransientMaterials.Add(Mid);
	return Mid;
}

UMaterialInterface* ARbTestRoom::EmissiveMaterial(double LuminanceNits)
{
	// UE-3's diffuser material when generated (it reads the section luminance from "Luminance" [cd/m^2] if it has one), else
	// the engine's unlit colour material: emissive 1 = 1 cd/m^2 with the extended (EV100) luminance range.
	if (UMaterialInterface* Generated = RbTestRoomPrivate::LoadIfExists<UMaterialInterface>(*RbTestRoomPrivate::ObjectPathOf(RbAssetPaths::MatLampDiffuser)))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Generated, this);
		Mid->SetScalarParameterValue(TEXT("Luminance"), static_cast<float>(LuminanceNits));
		TransientMaterials.Add(Mid);
		return Mid;
	}
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, RbTestRoomPrivate::EmissiveMaterial);
	if (!Parent)
	{
		return SurfaceMaterial(nullptr, FLinearColor::White, 0.5f);
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
	const float V = static_cast<float>(LuminanceNits);
	Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(V, V, V, 1.0f));
	TransientMaterials.Add(Mid);
	return Mid;
}

UStaticMeshComponent* ARbTestRoom::AddMesh(const FName& Name, UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, bool bFixture)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name), RF_Transient);
	Component->ComponentTags.Add(GeneratedComponentTag);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}
	Component->SetupAttachment(Root);
	Component->SetWorldTransform(Transform);
	if (bFixture)
	{
		// The lamp's louvres are modelled by the rect lights' barn doors; the meshes must not shadow the table a second time.
		Component->SetCastShadow(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetHiddenInGame(bFixtureHidden);
		FixtureMeshes.Add(Component);
	}
	else
	{
		Component->SetCollisionProfileName(TEXT("BlockAll"));
		Surfaces.Add(Component);
	}
	Component->RegisterComponent();
	return Component;
}

UStaticMeshComponent* ARbTestRoom::AddBox(const FName& Name, const FVector& Center, const FVector& Size, const FRotator& Rotation, UMaterialInterface* Material,
	bool bFixture)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, RbTestRoomPrivate::CubeMesh);
	return AddMesh(Name, Cube, FTransform(Rotation, Center, Size / 100.0), Material, bFixture);
}

URectLightComponent* ARbTestRoom::AddRectLight(const FName& Name, const FVector& Location, const FRotator& Rotation, const FVector2D& SourceSize, double Lumens,
	double TemperatureK, double BarnAngleDeg, double BarnLength)
{
	URectLightComponent* Light = NewObject<URectLightComponent>(this, MakeUniqueObjectName(this, URectLightComponent::StaticClass(), Name), RF_Transient);
	Light->ComponentTags.Add(GeneratedComponentTag);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetupAttachment(Root);
	Light->SetWorldLocationAndRotation(Location, Rotation);
	Light->IntensityUnits = ELightUnits::Lumens;
	Light->Intensity = static_cast<float>(Lumens);
	Light->SourceWidth = static_cast<float>(SourceSize.X);  // local Y
	Light->SourceHeight = static_cast<float>(SourceSize.Y); // local Z
	Light->BarnDoorAngle = static_cast<float>(FMath::Clamp(BarnAngleDeg, 0.0, 88.0));
	Light->BarnDoorLength = static_cast<float>(FMath::Max(BarnLength, 0.1));
	Light->bUseTemperature = true;
	Light->Temperature = static_cast<float>(TemperatureK);
	Light->AttenuationRadius = 1500.0f; // the whole room: the window is > 0.999 on the table (probe includes it)
	Light->CastShadows = true;
	Light->RegisterComponent();
	return Light;
}

void ARbTestRoom::BuildShell()
{
	const double W = WallThickness;
	const FVector S = InnerSize;
	const FTransform Frame(FRotator(0.0, GetActorRotation().Yaw, 0.0), GetActorLocation());
	auto Box = [&](const TCHAR* Name, const FVector& LocalCenter, const FVector& Size, UMaterialInterface* Material)
	{
		AddBox(Name, Frame.TransformPosition(LocalCenter), Size, Frame.Rotator(), Material, false);
	};
	UMaterialInterface* Wall = SurfaceMaterial(RbAssetPaths::MatRoomWall, WallAlbedo, 0.85f);
	UMaterialInterface* Floor = SurfaceMaterial(RbAssetPaths::MatRoomFloor, FloorAlbedo, 0.55f);
	UMaterialInterface* Ceiling = SurfaceMaterial(nullptr, CeilingAlbedo, 0.9f);
	Box(TEXT("RoomFloor"), FVector(0.0, 0.0, -0.5 * W), FVector(S.X + 2.0 * W, S.Y + 2.0 * W, W), Floor);
	Box(TEXT("RoomCeiling"), FVector(0.0, 0.0, S.Z + 0.5 * W), FVector(S.X + 2.0 * W, S.Y + 2.0 * W, W), Ceiling);
	Box(TEXT("RoomWallFoot"), FVector(0.5 * (S.X + W), 0.0, 0.5 * S.Z), FVector(W, S.Y + 2.0 * W, S.Z), Wall);
	Box(TEXT("RoomWallHead"), FVector(-0.5 * (S.X + W), 0.0, 0.5 * S.Z), FVector(W, S.Y + 2.0 * W, S.Z), Wall);
	Box(TEXT("RoomWallLeft"), FVector(0.0, -0.5 * (S.Y + W), 0.5 * S.Z), FVector(S.X, W, S.Z), Wall);
	Box(TEXT("RoomWallRight"), FVector(0.0, 0.5 * (S.Y + W), 0.5 * S.Z), FVector(S.X, W, S.Z), Wall);
}

void ARbTestRoom::BuildLamp()
{
	const FTransform Table = GetTableFrame(); // bed centre on the cloth, yaw of the table
	const FRotator Yaw = Table.Rotator();
	const double Alpha = FMath::DegreesToRadians(FMath::Clamp(LampLouvreAngleDeg, 0.0, 88.0));
	const double Depth = FMath::Cos(Alpha) * LampLouvreLength; // louvre depth below the diffuser plane
	const double Flare = FMath::Sin(Alpha) * LampLouvreLength; // outward reach at the louvre bottom
	const double Emitter = GetLampEmitterHeight();             // diffuser plane above the cloth
	const FVector2D Size(FMath::Max(LampSectionSize.X, 1.0), FMath::Max(LampSectionSize.Y, 1.0));
	if (LampColumnX.Num() == 0 || LampRowY.Num() == 0)
	{
		return;
	}
	constexpr double Margin = 4.0;     // housing beyond the outer louvre bottoms [cm]
	constexpr double Thickness = 9.0;  // housing top above the diffusers [cm]
	constexpr double Sheet = 0.6;      // louvre / skirt sheet thickness [cm]

	UMaterialInterface* Housing = SurfaceMaterial(nullptr, FLinearColor(0.035f, 0.035f, 0.037f), 0.4f);
	auto World = [&](const FVector& Local) { return Table.TransformPosition(Local); };
	auto Rot = [&](const FRotator& Local) { return (Table.GetRotation() * Local.Quaternion()).Rotator(); };

	// Canopy: a closed box from the fixture underside (louvre bottoms) to Thickness above the diffusers; the cells are open
	// below. From the side it reads as one clean light box; the skirt lies outside every section's barn-door cone.
	double MinX = TNumericLimits<double>::Max(), MaxX = -MinX, MinY = MinX, MaxY = -MinX;
	for (const double X : LampColumnX)
	{
		MinX = FMath::Min(MinX, X);
		MaxX = FMath::Max(MaxX, X);
	}
	for (const double Y : LampRowY)
	{
		MinY = FMath::Min(MinY, Y);
		MaxY = FMath::Max(MaxY, Y);
	}
	const FVector2D Lo(MinX - 0.5 * Size.X - Flare - Margin, MinY - 0.5 * Size.Y - Flare - Margin);
	const FVector2D Hi(MaxX + 0.5 * Size.X + Flare + Margin, MaxY + 0.5 * Size.Y + Flare + Margin);
	const FVector2D Mid = 0.5 * (Lo + Hi);
	const FVector2D Extent = Hi - Lo;
	const double Bottom = Emitter - Depth; // = LampHeightAboveBed
	AddBox(TEXT("LampHousing"), World(FVector(Mid.X, Mid.Y, Emitter + 0.5 * Thickness)), FVector(Extent.X, Extent.Y, Thickness), Yaw, Housing, true);
	const double SkirtZ = 0.5 * (Bottom + Emitter);
	AddBox(TEXT("LampSkirt"), World(FVector(Lo.X + 0.5 * Sheet, Mid.Y, SkirtZ)), FVector(Sheet, Extent.Y, Depth), Yaw, Housing, true);
	AddBox(TEXT("LampSkirt"), World(FVector(Hi.X - 0.5 * Sheet, Mid.Y, SkirtZ)), FVector(Sheet, Extent.Y, Depth), Yaw, Housing, true);
	AddBox(TEXT("LampSkirt"), World(FVector(Mid.X, Lo.Y + 0.5 * Sheet, SkirtZ)), FVector(Extent.X, Sheet, Depth), Yaw, Housing, true);
	AddBox(TEXT("LampSkirt"), World(FVector(Mid.X, Hi.Y - 0.5 * Sheet, SkirtZ)), FVector(Extent.X, Sheet, Depth), Yaw, Housing, true);

	// Suspension rods to the ceiling.
	if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, RbTestRoomPrivate::CylinderMesh))
	{
		const double Top = GetActorLocation().Z + InnerSize.Z - Table.GetLocation().Z; // ceiling above the cloth
		const double RodBottom = Emitter + Thickness;
		if (Top > RodBottom)
		{
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector Local((Corner & 1) ? Hi.X - 20.0 : Lo.X + 20.0, (Corner & 2) ? Hi.Y - 20.0 : Lo.Y + 20.0, 0.5 * (Top + RodBottom));
				AddMesh(TEXT("LampRod"), Cylinder, FTransform(Yaw, World(Local), FVector(0.016, 0.016, (Top - RodBottom) / 100.0)), Housing, true);
			}
		}
	}

	for (int32 IX = 0; IX < LampColumnX.Num(); ++IX)
	{
		const double Lumens = LampColumnLumens.Num() ? LampColumnLumens[FMath::Min(IX, LampColumnLumens.Num() - 1)] : 0.0;
		const double Luminance = Lumens / (UE_DOUBLE_PI * Size.X * Size.Y * 1e-4);
		UMaterialInterface* Diffuser = EmissiveMaterial(Luminance);
		for (int32 IY = 0; IY < LampRowY.Num(); ++IY)
		{
			const FVector C(LampColumnX[IX], LampRowY[IY], Emitter);
			// Diffuser panel (emissive, invisible to ray tracing / Lumen / shadows: the analytic light is the only highlight).
			if (UStaticMeshComponent* Panel = AddBox(TEXT("LampDiffuser"), World(C + FVector(0.0, 0.0, -0.25)), FVector(Size.X, Size.Y, 0.5), Yaw, Diffuser, true))
			{
				Panel->bVisibleInRayTracing = false;
				Panel->bAffectDynamicIndirectLighting = false;
				Panel->bAffectDistanceFieldLighting = false;
				Panel->bVisibleInReflectionCaptures = false;
				Panel->bVisibleInRealTimeSkyCaptures = false;
				Panel->MarkRenderStateDirty();
			}
			// Rect light pointing down (local X = down): width (local Y) across the table, height (local Z) along it.
			if (Lumens > 0.0)
			{
				if (URectLightComponent* Light = AddRectLight(TEXT("LampSection"), World(C + FVector(0.0, 0.0, -0.6)), Rot(FRotator(-90.0, 0.0, 0.0)),
						FVector2D(Size.Y, Size.X), Lumens, LampTemperatureK, LampLouvreAngleDeg, LampLouvreLength))
				{
					Light->ContactShadowLength = static_cast<float>(LampContactShadowLength);
					Light->MarkRenderStateDirty();
					LampLights.Add(Light);
				}
			}
			// Louvre walls: from the diffuser edge down by Depth, flared outward by Flare (= the barn doors).
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const bool bAlongX = Side < 2; // walls at the +-x edges
				const double Sign = (Side % 2 == 0) ? 1.0 : -1.0;
				const FVector Outward = bAlongX ? FVector(Sign, 0.0, 0.0) : FVector(0.0, Sign, 0.0);
				const double HalfEdge = bAlongX ? 0.5 * Size.X : 0.5 * Size.Y;
				const double EdgeLength = (bAlongX ? Size.Y : Size.X) + 2.0 * Flare;
				const FVector Top = C + Outward * HalfEdge;
				const FVector Slant = Outward * Flare - FVector(0.0, 0.0, Depth); // top -> bottom
				const FVector LocalZ = Slant.GetSafeNormal();
				const FVector LocalX = FVector::CrossProduct(bAlongX ? FVector(0.0, 1.0, 0.0) : FVector(1.0, 0.0, 0.0), LocalZ).GetSafeNormal();
				const FRotator WallRot = FRotationMatrix::MakeFromZX(LocalZ, LocalX).Rotator();
				AddBox(TEXT("LampLouvre"), World(Top + 0.5 * Slant), FVector(Sheet, EdgeLength, LampLouvreLength), Rot(WallRot), Housing, true);
			}
		}
	}
}

void ARbTestRoom::BuildAmbient()
{
	const FTransform Frame(FRotator(0.0, GetActorRotation().Yaw, 0.0), GetActorLocation());
	const int32 NX = FMath::Max(0, AmbientPanels.X);
	const int32 NY = FMath::Max(0, AmbientPanels.Y);
	if (NX == 0 || NY == 0 || AmbientPanelLumens <= 0.0)
	{
		return;
	}
	const double Size = FMath::Max(AmbientPanelSize, 1.0);
	UMaterialInterface* Trim = SurfaceMaterial(nullptr, FLinearColor(0.55f, 0.55f, 0.54f), 0.5f);
	UMaterialInterface* Diffuser = EmissiveMaterial(AmbientPanelLumens / (UE_DOUBLE_PI * Size * Size * 1e-4));
	for (int32 IX = 0; IX < NX; ++IX)
	{
		for (int32 IY = 0; IY < NY; ++IY)
		{
			const FVector C((IX - 0.5 * (NX - 1)) * AmbientPanelPitch.X, (IY - 0.5 * (NY - 1)) * AmbientPanelPitch.Y, InnerSize.Z);
			AddBox(TEXT("AmbientTrim"), Frame.TransformPosition(C - FVector(0.0, 0.0, 1.0)), FVector(Size + 6.0, Size + 6.0, 2.0), Frame.Rotator(), Trim, true);
			if (UStaticMeshComponent* Panel =
					AddBox(TEXT("AmbientDiffuser"), Frame.TransformPosition(C - FVector(0.0, 0.0, 2.2)), FVector(Size, Size, 0.4), Frame.Rotator(), Diffuser, true))
			{
				Panel->bVisibleInRayTracing = false;
				Panel->bAffectDynamicIndirectLighting = false;
				Panel->bAffectDistanceFieldLighting = false;
				Panel->bVisibleInReflectionCaptures = false;
				Panel->bVisibleInRealTimeSkyCaptures = false;
				Panel->MarkRenderStateDirty();
			}
			if (URectLightComponent* Light = AddRectLight(TEXT("AmbientPanel"), Frame.TransformPosition(C - FVector(0.0, 0.0, 2.6)),
					(Frame.GetRotation() * FRotator(-90.0, 0.0, 0.0).Quaternion()).Rotator(), FVector2D(Size, Size), AmbientPanelLumens, AmbientTemperatureK, 88.0, 0.1))
			{
				AmbientLights.Add(Light);
			}
		}
	}
}

void ARbTestRoom::ApplyPostProcess()
{
	// Level baseline = the Eyes camera preset's exposure law (one source: RbCameraModel), plus the room's own grading.
	const FRbCameraPresetParams Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	FPostProcessSettings& S = PostProcess->Settings;
	S = FPostProcessSettings();
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = static_cast<float>(Eyes.MinEv100); // EV100 (ExtendDefaultLuminanceRange)
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = static_cast<float>(Eyes.MaxEv100);
	S.bOverride_AutoExposureSpeedUp = true;
	S.AutoExposureSpeedUp = static_cast<float>(Eyes.AdaptSpeedUp);
	S.bOverride_AutoExposureSpeedDown = true;
	S.AutoExposureSpeedDown = static_cast<float>(Eyes.AdaptSpeedDown);
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = static_cast<float>(Eyes.ExposureCompensation);
	// Metering (plan 4.4): the eye adapts to what it looks at - the lit table - not to the dim room around it. The histogram
	// meters the brighter part of a centre-weighted frame (70..95 %: the cloth when down on the shot, the table in a room view);
	// the top 5 % are ignored so the lamp diffusers entering the frame do not pump the exposure (pitfall 8). The mask texture
	// exists in game worlds only (a transient texture must not be saved with the level).
	S.bOverride_AutoExposureLowPercent = true;
	S.AutoExposureLowPercent = 70.0f;
	S.bOverride_AutoExposureHighPercent = true;
	S.AutoExposureHighPercent = 95.0f;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (!MeterMask)
		{
			MeterMask = RbTestRoomPrivate::MakeCentreWeightedMask();
		}
		S.bOverride_AutoExposureMeterMask = MeterMask != nullptr;
		S.AutoExposureMeterMask = MeterMask;
	}
	S.bOverride_LocalExposureHighlightContrastScale = true;
	S.LocalExposureHighlightContrastScale = LocalExposureHighlightContrast;
	S.bOverride_LocalExposureShadowContrastScale = true;
	S.LocalExposureShadowContrastScale = LocalExposureShadowContrast;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = BloomIntensity;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = static_cast<float>(Eyes.Vignette);
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = static_cast<float>(Eyes.GrainG0);
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = static_cast<float>(Eyes.ShutterAngleDeg / 360.0);
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.0f; // Eyes: no chromatic aberration
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.0f;
	// The eye adapts to the illuminant: white balance close to the lamp's temperature (a small warm residue remains).
	S.bOverride_WhiteTemp = true;
	S.WhiteTemp = static_cast<float>(LampTemperatureK + 200.0);
	PostProcess->bUnbound = true;
	PostProcess->Priority = 0.0f;
	PostProcess->BlendWeight = 1.0f;
}

void ARbTestRoom::RebuildRoom()
{
	DestroyGeneratedComponents();
	const ARbTable* Table = FindTable();
	BedHeightUsed = Table ? 100.0 * RbTestRoomPrivate::SpecOf(Table).BedHeight : BedHeight;
	BuildShell();
	BuildLamp();
	BuildAmbient();
	ApplyPostProcess();
}

void ARbTestRoom::SetLampFixtureHiddenInGame(bool bHide)
{
	bFixtureHidden = bHide;
	for (UStaticMeshComponent* Mesh : FixtureMeshes)
	{
		if (IsValid(Mesh))
		{
			Mesh->SetHiddenInGame(bHide);
		}
	}
}

void ARbTestRoom::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRoom();
}

void ARbTestRoom::BeginPlay()
{
	Super::BeginPlay();
	// Loaded / PIE-duplicated rooms carry no transient components.
	if (LampLights.Num() == 0 || Surfaces.Num() == 0)
	{
		RebuildRoom();
	}
}

double ARbTestRoom::IlluminanceFromLight(const ULocalLightComponent& Light, const FVector& WorldPoint, const FVector& Normal)
{
	if (!Light.IsVisible() || Light.Intensity <= 0.0f)
	{
		return 0.0;
	}
	if (const URectLightComponent* Rect = Cast<URectLightComponent>(&Light))
	{
		return RbTestRoomPrivate::RectIlluminance(*Rect, WorldPoint, Normal);
	}
	if (const UPointLightComponent* Point = Cast<UPointLightComponent>(&Light))
	{
		return RbTestRoomPrivate::PointIlluminance(*Point, WorldPoint, Normal);
	}
	return 0.0;
}

FRbLuxReport ARbTestRoom::ComputeLux(double GridCm, ERbLuxSources Sources) const
{
	using namespace RbTestRoomPrivate;
	TArray<const ULocalLightComponent*> Lights;
	if (Sources != ERbLuxSources::Ambient)
	{
		for (const URectLightComponent* Light : LampLights)
		{
			if (IsValid(Light))
			{
				Lights.Add(Light);
			}
		}
	}
	if (Sources != ERbLuxSources::Lamp)
	{
		for (const URectLightComponent* Light : AmbientLights)
		{
			if (IsValid(Light))
			{
				Lights.Add(Light);
			}
		}
	}
	auto At = [&Lights](const FVector& Point)
	{
		double E = 0.0;
		for (const ULocalLightComponent* Light : Lights)
		{
			E += IlluminanceFromLight(*Light, Point, FVector::UpVector);
		}
		return E;
	};

	const rb::TableSpec Spec = SpecOf(FindTable());
	const FTransform Table = GetTableFrame();
	const double HL = 50.0 * Spec.Length, HW = 50.0 * Spec.Width, Rail = 100.0 * Spec.RailWidthTotal, RailZ = 100.0 * Spec.RailTopZ;
	const double Step = FMath::Max(GridCm, 0.5);
	FLuxAccumulator Bed, Rails, Floor;
	const int32 NX = FMath::RoundToInt(2.0 * (HL + Rail) / Step);
	const int32 NY = FMath::RoundToInt(2.0 * (HW + Rail) / Step);
	for (int32 I = 0; I <= NX; ++I)
	{
		const double X = -(HL + Rail) + I * 2.0 * (HL + Rail) / NX;
		for (int32 J = 0; J <= NY; ++J)
		{
			const double Y = -(HW + Rail) + J * 2.0 * (HW + Rail) / NY;
			const bool bBed = FMath::Abs(X) <= HL + 1e-6 && FMath::Abs(Y) <= HW + 1e-6;
			(bBed ? Bed : Rails).Add(At(Table.TransformPosition(FVector(X, Y, bBed ? 0.0 : RailZ))));
		}
	}
	// Floor walkway: 30 cm from the walls, outside the table's outer boundary + 30 cm, on a coarser grid.
	const FTransform RoomFrame(FRotator(0.0, GetActorRotation().Yaw, 0.0), GetActorLocation());
	const double FloorStep = FMath::Max(Step * 4.0, 20.0);
	for (double X = -0.5 * InnerSize.X + 30.0; X <= 0.5 * InnerSize.X - 30.0 + 1e-6; X += FloorStep)
	{
		for (double Y = -0.5 * InnerSize.Y + 30.0; Y <= 0.5 * InnerSize.Y - 30.0 + 1e-6; Y += FloorStep)
		{
			const FVector World = RoomFrame.TransformPosition(FVector(X, Y, 0.0));
			const FVector InTable = Table.InverseTransformPosition(World);
			if (FMath::Abs(InTable.X) <= HL + Rail + 30.0 && FMath::Abs(InTable.Y) <= HW + Rail + 30.0)
			{
				continue;
			}
			Floor.Add(At(World));
		}
	}
	FRbLuxReport Report;
	Bed.Write(Report.BedMin, Report.BedMax, Report.BedAvg, Report.BedPoints);
	Rails.Write(Report.RailMin, Report.RailMax, Report.RailAvg, Report.RailPoints);
	Floor.Write(Report.FloorMin, Report.FloorMax, Report.FloorAvg, Report.FloorPoints);
	return Report;
}

FString ARbTestRoom::ValidateM1Level(const UObject* WorldContextObject, bool& bOutOk)
{
	using namespace RbTestRoomPrivate;
	bOutOk = true;
	FString Report;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		bOutOk = false;
		return TEXT("FAIL world: no world\n");
	}

	// Table: exactly one, at the origin, unrotated.
	TArray<ARbTable*> Tables;
	for (TActorIterator<ARbTable> It(World); It; ++It)
	{
		Tables.Add(*It);
	}
	ARbTable* Table = Tables.Num() ? Tables[0] : nullptr;
	AddLine(Report, bOutOk, Tables.Num() == 1, TEXT("table"), FString::Printf(TEXT("%d ARbTable actor(s)"), Tables.Num()));
	if (Table)
	{
		const bool bOrigin = Table->GetActorLocation().Equals(FVector::ZeroVector, 1e-3) && FMath::IsNearlyZero(Table->GetActorRotation().Yaw, 1e-3);
		AddLine(Report, bOutOk, bOrigin, TEXT("table at the origin"), FString::Printf(TEXT("location %s yaw %.4f, preset %s"),
			*Table->GetActorLocation().ToString(), Table->GetActorRotation().Yaw, *UEnum::GetValueAsString(Table->Preset)));
	}

	// Room: exactly one.
	TArray<ARbTestRoom*> Rooms;
	for (TActorIterator<ARbTestRoom> It(World); It; ++It)
	{
		Rooms.Add(*It);
	}
	ARbTestRoom* Room = Rooms.Num() ? Rooms[0] : nullptr;
	AddLine(Report, bOutOk, Rooms.Num() == 1, TEXT("room"), FString::Printf(TEXT("%d ARbTestRoom actor(s)"), Rooms.Num()));

	// Game mode from the World Settings.
	const AWorldSettings* Settings = World->GetWorldSettings();
	const UClass* GameMode = Settings ? Settings->DefaultGameMode.Get() : nullptr;
	AddLine(Report, bOutOk, GameMode && GameMode->IsChildOf(ARbGameMode::StaticClass()), TEXT("world settings game mode"),
		GameMode ? GameMode->GetPathName() : FString(TEXT("none")));

	const rb::TableSpec Spec = SpecOf(Table);
	const double HeadEnd = -100.0 * (0.5 * Spec.Length + Spec.RailWidthTotal); // outer rail edge at the head (core -x)

	// PlayerStart at the head end, facing the table (+X).
	TArray<APlayerStart*> Starts;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Starts.Add(*It);
	}
	if (Starts.Num() == 1)
	{
		const FVector P = Starts[0]->GetActorLocation();
		const double Yaw = FRotator::NormalizeAxis(Starts[0]->GetActorRotation().Yaw);
		const bool bHead = P.X < HeadEnd && FMath::Abs(P.Y) < 50.0 * Spec.Width && FMath::Abs(Yaw) < 30.0 && P.Z > 0.0;
		AddLine(Report, bOutOk, bHead, TEXT("player start at the head end"), FString::Printf(TEXT("location %s yaw %.1f (head rail edge x = %.1f)"), *P.ToString(), Yaw, HeadEnd));
	}
	else
	{
		AddLine(Report, bOutOk, false, TEXT("player start at the head end"), FString::Printf(TEXT("%d PlayerStart actors (expected 1)"), Starts.Num()));
	}

	// The four look-dev capture cameras.
	for (const TCHAR* Tag : {RbAssetPaths::CaptureCamera::Overhead, RbAssetPaths::CaptureCamera::ChinOnCue, RbAssetPaths::CaptureCamera::BallCloseUp,
			 RbAssetPaths::CaptureCamera::RoomOverview})
	{
		int32 LookDev = 0, Plain = 0;
		for (TActorIterator<ACameraActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(FName(Tag)))
			{
				(Cast<ARbLookDevCamera>(*It) ? LookDev : Plain) += 1;
			}
		}
		AddLine(Report, bOutOk, LookDev == 1 && Plain == 0, FString::Printf(TEXT("capture camera %s"), Tag),
			FString::Printf(TEXT("%d ARbLookDevCamera, %d plain camera(s)"), LookDev, Plain));
	}

	if (Table && Room)
	{
		// Single sources (R-14): the physics' lamp = the rendered lamp; the room's bed = TableSpec.
		AddLine(Report, bOutOk, FMath::IsNearlyEqual(Table->LampUndersideHeight, Room->GetLampUndersideHeightMeters(), 1e-9), TEXT("lamp underside height"),
			FString::Printf(TEXT("table %.4f m, room %.4f m above the cloth"), Table->LampUndersideHeight, Room->GetLampUndersideHeightMeters()));
		AddLine(Report, bOutOk, FMath::IsNearlyEqual(Room->GetBedHeightUsed(), 100.0 * Spec.BedHeight, 1e-6), TEXT("room bed height"),
			FString::Printf(TEXT("room %.2f cm, TableSpec %.2f cm"), Room->GetBedHeightUsed(), 100.0 * Spec.BedHeight));
		AddLine(Report, bOutOk, Room->GetLampUndersideHeightMeters() >= 1.016 - 1e-9, TEXT("lamp WPA height"),
			FString::Printf(TEXT("fixture underside %.3f m above the bed (WPA >= 1.016 m)"), Room->GetLampUndersideHeightMeters()));

		// E4 / A8: lux probe on the placed lights.
		const FRbLuxReport Lamp = Room->ComputeLux(5.0, ERbLuxSources::Lamp);
		const FRbLuxReport All = Room->ComputeLux(5.0, ERbLuxSources::All);
		const FRbLuxReport Ambient = Room->ComputeLux(5.0, ERbLuxSources::Ambient);
		AddLine(Report, bOutOk, Lamp.TableMin() >= WpaMinLux, TEXT("E4 lamp >= 520 lux on bed and rails"), Lamp.ToString());
		AddLine(Report, bOutOk, All.TableMin() >= WpaMinLux && All.FloorMin >= 50.0, TEXT("E4 all lights (WPA >= 50 lux elsewhere)"), All.ToString());
		AddLine(Report, bOutOk, Ambient.FloorAvg >= AmbientMinLux && Ambient.FloorAvg <= AmbientMaxLux, TEXT("ambient ~50 lux"),
			FString::Printf(TEXT("ambient panels alone: floor avg %.1f lux (band %.0f..%.0f)"), Ambient.FloorAvg, AmbientMinLux, AmbientMaxLux));
	}
	return Report;
}
