#include "Table/RbTable.h"

#include "RawBreak.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Table/RbTableMeshBuilder.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

// Owner: UE-1. TODO(UE-1): RebuildMeshes (baked static meshes or dynamic meshes from RbTableMeshBuilder, materials,
// collision), editor property-change rebuild, tests.

ARbTable::ARbTable()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	ClothOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("ClothOrigin"));
	ClothOrigin->SetupAttachment(Root);

	PartMaterials.SetNum(static_cast<int32>(ERbTablePart::Count));
	PartMaterials[static_cast<int32>(ERbTablePart::Bed)] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(RbAssetPaths::MatCloth));
	PartMaterials[static_cast<int32>(ERbTablePart::CushionCloth)] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(RbAssetPaths::MatCloth));
	PartMaterials[static_cast<int32>(ERbTablePart::RailCaps)] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(RbAssetPaths::MatRailWood));
	PartMaterials[static_cast<int32>(ERbTablePart::Apron)] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(RbAssetPaths::MatRailWood));
	PartMaterials[static_cast<int32>(ERbTablePart::PocketLiners)] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(RbAssetPaths::MatPocketLiner));
	PartMaterials[static_cast<int32>(ERbTablePart::Sights)] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(RbAssetPaths::MatSight));
	PartMaterials[static_cast<int32>(ERbTablePart::Legs)] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(RbAssetPaths::MatRailWood));
}

const FRbTableContext& ARbTable::GetContext() const
{
	check(Context.IsValid());
	return *Context;
}

void ARbTable::RebuildTable()
{
	FRbTableSetup Setup;
	Setup.Table = Preset;
	Setup.BallSet = BallSet;
	Setup.BallSetSeed = static_cast<uint64>(BallSetSeed);
	Setup.LampUndersideZ = LampUndersideHeight > 0.0 ? LampUndersideHeight : rb::kInfinity;

	FString Error;
	Context = FRbTableContext::Create(Setup, Error);
	if (!Context.IsValid())
	{
		UE_LOG(LogRawBreak, Error, TEXT("ARbTable %s: %s"), *GetName(), *Error);
		return;
	}
	ClothOrigin->SetRelativeLocation(FVector(0.0, 0.0, FRbCoords::CmPerMeter * Context->BedHeight()));
	RebuildMeshes();
}

void ARbTable::RebuildMeshes()
{
	// TODO(UE-1)
}

FTransform ARbTable::GetTableToWorld() const
{
	return ClothOrigin->GetComponentTransform();
}

FVector ARbTable::CoreToWorld(const rb::Vec3& P) const
{
	return GetTableToWorld().TransformPosition(FRbCoords::PositionToUE(P));
}

rb::Vec3 ARbTable::WorldToCore(const FVector& World) const
{
	return FRbCoords::PositionToCore(GetTableToWorld().InverseTransformPosition(World));
}

FVector ARbTable::CoreDirectionToWorld(const rb::Vec3& D) const
{
	return GetTableToWorld().TransformVectorNoScale(FRbCoords::DirectionToUE(D));
}

rb::Vec3 ARbTable::WorldDirectionToCore(const FVector& World) const
{
	return FRbCoords::DirectionToCore(GetTableToWorld().InverseTransformVectorNoScale(World));
}

FQuat ARbTable::CoreOrientationToWorld(const rb::Quat& Q) const
{
	return GetTableToWorld().GetRotation() * FRbCoords::OrientationToUE(Q);
}

rb::Quat ARbTable::WorldOrientationToCore(const FQuat& Q) const
{
	return FRbCoords::OrientationToCore(GetTableToWorld().GetRotation().Inverse() * Q);
}

double ARbTable::WorldDirectionToAzimuth(const FVector& WorldDirection) const
{
	return FRbCoords::AzimuthFromUEDirection(GetTableToWorld().InverseTransformVectorNoScale(WorldDirection));
}

FVector ARbTable::GetBedCenterWorld() const
{
	return GetTableToWorld().GetLocation();
}

UPrimitiveComponent* ARbTable::GetPartComponent(ERbTablePart Part) const
{
	const int32 Index = static_cast<int32>(Part);
	return PartComponents.IsValidIndex(Index) ? PartComponents[Index].Get() : nullptr;
}

void ARbTable::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildTable();
}

void ARbTable::BeginPlay()
{
	Super::BeginPlay();
	if (!Context.IsValid())
	{
		RebuildTable();
	}
}
