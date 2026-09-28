#include "Table/RbTable.h"

#include "RawBreak.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Table/RbTableMeshBuilder.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

// Owner: UE-1.

const FName ARbTable::PartComponentTag(TEXT("RbTablePart"));

namespace RbTablePrivate
{
	// Loads an asset only if its package exists (no "failed to find" warnings before the generators ran).
	template <class T>
	T* LoadIfExists(const FSoftObjectPath& Path)
	{
		if (Path.IsNull() || !FPackageName::DoesPackageExist(Path.GetLongPackageName()))
		{
			return nullptr;
		}
		return Cast<T>(Path.TryLoad());
	}
}

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

FString ARbTable::MakeBuildKey() const
{
	FString Key = FString::Printf(TEXT("%d|%d|%lld|%.12g|%d"), static_cast<int32>(Preset), static_cast<int32>(BallSet), BallSetSeed, LampUndersideHeight,
		bUseBakedMeshes ? 1 : 0);
	for (const TSoftObjectPtr<UMaterialInterface>& Material : PartMaterials)
	{
		Key += TEXT("|") + Material.ToSoftObjectPath().ToString();
	}
	return Key;
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
	BuiltKey.Reset();
	if (!Context.IsValid())
	{
		DestroyPartComponents();
		UE_LOG(LogRawBreak, Error, TEXT("ARbTable %s: %s"), *GetName(), *Error);
		return;
	}
	ClothOrigin->SetRelativeLocation(FVector(0.0, 0.0, FRbCoords::CmPerMeter * Context->BedHeight()));
	RebuildMeshes();
	BuiltKey = MakeBuildKey();
}

void ARbTable::DestroyPartComponents()
{
	TInlineComponentArray<UPrimitiveComponent*> Components(this);
	for (UPrimitiveComponent* Component : Components)
	{
		if (Component && Component->ComponentHasTag(PartComponentTag))
		{
			Component->DestroyComponent();
		}
	}
	for (UPrimitiveComponent* Component : PartComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	PartComponents.Reset();
}

void ARbTable::RebuildMeshes()
{
	DestroyPartComponents();
	if (!Context.IsValid())
	{
		return;
	}
	PartComponents.SetNum(static_cast<int32>(ERbTablePart::Count));

	TUniquePtr<FRbTableMeshSet> Runtime; // built only if some part has no baked asset
	for (int32 Index = 0; Index < static_cast<int32>(ERbTablePart::Count); ++Index)
	{
		const ERbTablePart Part = static_cast<ERbTablePart>(Index);
		UMaterialInterface* Material =
			PartMaterials.IsValidIndex(Index) ? RbTablePrivate::LoadIfExists<UMaterialInterface>(PartMaterials[Index].ToSoftObjectPath()) : nullptr;
		UStaticMesh* Baked =
			bUseBakedMeshes ? RbTablePrivate::LoadIfExists<UStaticMesh>(FSoftObjectPath(RbTableMeshBuilder::GetBakedMeshObjectPath(Preset, Part))) : nullptr;
		const FName Name =
			MakeUniqueObjectName(this, UPrimitiveComponent::StaticClass(), FName(*FString::Printf(TEXT("RbTablePart_%s"), RbTypes::ToString(Part))));

		UPrimitiveComponent* Component = nullptr;
		if (Baked)
		{
			UStaticMeshComponent* StaticComponent = NewObject<UStaticMeshComponent>(this, Name, RF_Transient);
			StaticComponent->SetStaticMesh(Baked);
			if (Material)
			{
				StaticComponent->SetMaterial(0, Material);
			}
			Component = StaticComponent;
		}
		else
		{
			if (!Runtime)
			{
				Runtime = MakeUnique<FRbTableMeshSet>();
				FString Error;
				if (!RbTableMeshBuilder::BuildAll(Context->Geometry, FRbTableMeshOptions(), *Runtime, Error))
				{
					UE_LOG(LogRawBreak, Error, TEXT("ARbTable %s: table meshes: %s"), *GetName(), *Error);
					DestroyPartComponents(); // no half-built table
					return;
				}
			}
			UE::Geometry::FDynamicMesh3& Mesh = Runtime->Get(Part);
			if (Mesh.TriangleCount() == 0)
			{
				continue;
			}
			UDynamicMeshComponent* DynamicComponent = NewObject<UDynamicMeshComponent>(this, Name, RF_Transient);
			DynamicComponent->SetMesh(MoveTemp(Mesh));
			DynamicComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::ExternallyProvided);
			if (Material)
			{
				DynamicComponent->SetMaterial(0, Material);
			}
			if (RbTableMeshBuilder::PartHasCollision(Part))
			{
				DynamicComponent->SetComplexAsSimpleCollisionEnabled(true, false);
			}
			Component = DynamicComponent;
		}

		Component->ComponentTags.Add(PartComponentTag);
		Component->SetCollisionProfileName(RbTableMeshBuilder::PartHasCollision(Part) ? UCollisionProfile::BlockAll_ProfileName
																					 : UCollisionProfile::NoCollision_ProfileName);
		Component->SetupAttachment(ClothOrigin);
		Component->RegisterComponent();
		PartComponents[Index] = Component;
	}
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

bool ARbTable::IsPartBaked(ERbTablePart Part) const
{
	return Cast<UStaticMeshComponent>(GetPartComponent(Part)) != nullptr;
}

void ARbTable::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The editor re-runs construction on every move / property edit: rebuild only when something the table depends on
	// changed (or the transient parts are gone, e.g. after a duplication).
	if (!Context.IsValid() || BuiltKey != MakeBuildKey() || PartComponents.Num() == 0)
	{
		RebuildTable();
	}
}

void ARbTable::BeginPlay()
{
	Super::BeginPlay();
	// Loaded / PIE-duplicated tables carry neither the context (plain C++) nor the transient part components.
	if (!Context.IsValid() || BuiltKey != MakeBuildKey() || PartComponents.Num() == 0)
	{
		RebuildTable();
	}
}
