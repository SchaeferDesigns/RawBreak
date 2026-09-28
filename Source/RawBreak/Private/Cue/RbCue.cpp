#include "Cue/RbCue.h"

#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Table/RbTable.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

// Owner: UE-4. Mesh build / baked asset, material, exact pose, visibility per drive (RbCue.h).

// File-local helpers in a named namespace (as RbTablePrivate): unity builds share one translation unit between files.
namespace RbCuePrivate
{
	// Engine material whose base colour is the vertex colour: the cue's section albedo before UE-3's M_RbCue exists.
	const TCHAR* const VertexColorMaterialPath = TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial");

	template <typename T>
	T* LoadIfExists(const TCHAR* PackagePath)
	{
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			return nullptr;
		}
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackagePath, *FPackageName::GetShortName(PackagePath));
		return LoadObject<T>(nullptr, *ObjectPath);
	}

	// Exact pose (same approach as ARbBallSet): the quaternion goes into the rotation cache, so UpdateComponentToWorld uses it
	// bitwise; SetRelativeLocationAndRotation would round-trip through FRotator (snapping near pitch +-90 deg) and ignore
	// changes below 1e-4 cm / deg. An ordinary transform update without teleport keeps the motion vectors.
	void ApplyExactPose(USceneComponent* Component, const FVector& Location, const FQuat& Rotation)
	{
		FRotationConversionCache Cache;
		const FRotator Rotator = Cache.QuatToRotator(Rotation);
		Component->SetRelativeRotationCache(Cache);
		Component->SetRelativeLocation_Direct(Location);
		Component->SetRelativeRotation_Direct(Rotator);
		Component->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::None);
	}

	bool SameGeometry(const rb::CueSpec& A, const rb::CueSpec& B)
	{
		return A.Length == B.Length && A.TipDomeRadius == B.TipDomeRadius && A.TipDiameter == B.TipDiameter;
	}
}

ARbCue::ARbCue()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	RuntimeMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("RuntimeMesh"));
	RuntimeMesh->SetupAttachment(Root);
	RuntimeMesh->SetMobility(EComponentMobility::Movable);
	RuntimeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RuntimeMesh->SetGenerateOverlapEvents(false);
	RuntimeMesh->SetCastShadow(true);
	BakedMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BakedMesh"));
	BakedMesh->SetupAttachment(Root);
	BakedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BakedMesh->SetGenerateOverlapEvents(false);
	BakedMesh->SetMobility(EComponentMobility::Movable);
	BakedMesh->SetCastShadow(true);
	SetHidden(true); // Drive = Hidden
}

void ARbCue::InitForTable(ARbTable* InTable, const rb::CueSpec& InSpec, const rb::human::CueBodyState& InBody)
{
	Table = InTable;
	Spec = InSpec;
	Body = InBody;
	if (InTable)
	{
		AttachToComponent(InTable->GetClothOrigin(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	RebuildMesh();
	ApplyMaterial();
	SetDrive(Drive);
}

void ARbCue::RebuildMesh()
{
	UStaticMesh* Baked = nullptr;
	ERbCuePreset Preset = ERbCuePreset::Playing19oz;
	if (bUseBakedMesh && IsDefaultBody(Body) && FindPresetForSpec(Spec, Preset))
	{
		Baked = LoadBakedMesh(Preset);
	}
	bUsingBakedMesh = Baked != nullptr;
	if (Baked)
	{
		BakedMesh->SetStaticMesh(Baked);
		BakedMesh->SetVisibility(true);
		RuntimeMesh->SetVisibility(false);
		RuntimeMesh->SetMesh(UE::Geometry::FDynamicMesh3());
	}
	else
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		RbCueMeshBuilder::BuildCue(Spec, Body, MeshOptions, Mesh);
		RuntimeMesh->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated); // the mesh carries no tangents
		RuntimeMesh->SetMesh(MoveTemp(Mesh));
		RuntimeMesh->SetVisibility(true);
		BakedMesh->SetStaticMesh(nullptr);
		BakedMesh->SetVisibility(false);
	}
	bMeshBuilt = true;
}

void ARbCue::ApplyMaterial()
{
	UMaterialInterface* Material = MaterialOverride.LoadSynchronous();
	if (!Material)
	{
		Material = RbCuePrivate::LoadIfExists<UMaterialInterface>(RbAssetPaths::MatCue);
	}
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, RbCuePrivate::VertexColorMaterialPath);
	}
	AppliedMaterial = Material;
	if (Material)
	{
		RuntimeMesh->SetMaterial(0, Material);
		BakedMesh->SetMaterial(0, Material);
	}
}

UPrimitiveComponent* ARbCue::GetMeshComponent() const
{
	if (!bMeshBuilt)
	{
		return nullptr;
	}
	return bUsingBakedMesh ? static_cast<UPrimitiveComponent*>(BakedMesh.Get()) : static_cast<UPrimitiveComponent*>(RuntimeMesh.Get());
}

void ARbCue::SetPoseCore(const rb::Vec3& TipDomeCenter, const rb::Vec3& Direction)
{
	// Local mesh frame: +X from the butt to the tip, origin at the tip dome centre (RbCueMeshBuilder.h). The roll about the
	// axis is fixed by MakeFromX (table up), so a moving cue never spins about its own axis.
	const FVector Axis = FRbCoords::DirectionToUE(Direction).GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		return;
	}
	PoseTip = TipDomeCenter;
	PoseDirection = Direction;
	RbCuePrivate::ApplyExactPose(Root,FRbCoords::PositionToUE(TipDomeCenter), FRotationMatrix::MakeFromX(Axis).ToQuat());
}

void ARbCue::SetDrive(ERbCueDrive InDrive)
{
	const bool bWasHidden = IsHidden();
	Drive = InDrive;
	const bool bHide = Drive == ERbCueDrive::Hidden;
	SetActorHiddenInGame(bHide);
	if (bWasHidden && !bHide)
	{
		ResetMotion(); // shown at a new place: no streak from where it was hidden
	}
}

void ARbCue::ResetMotion()
{
	// Same as ARbBallSet::ResetBallMotion: drop the velocity data and re-create the proxy (previous = current transform).
	if (UPrimitiveComponent* Mesh = GetMeshComponent())
	{
		Mesh->ResetSceneVelocity();
		Mesh->MarkRenderStateDirty();
	}
}

bool ARbCue::FindPresetForSpec(const rb::CueSpec& InSpec, ERbCuePreset& OutPreset)
{
	static constexpr ERbCuePreset Presets[] = {ERbCuePreset::Playing19oz, ERbCuePreset::Break21oz, ERbCuePreset::Jump9oz, ERbCuePreset::House19oz};
	for (const ERbCuePreset Preset : Presets)
	{
		if (RbCuePrivate::SameGeometry(rb::GetCueSpec(RbTypes::ToCore(Preset)), InSpec))
		{
			OutPreset = Preset;
			return true;
		}
	}
	return false;
}

bool ARbCue::IsDefaultBody(const rb::human::CueBodyState& InBody)
{
	const rb::human::CueBodyState Default;
	return InBody.TipRadius == Default.TipRadius && InBody.ButtRadius == Default.ButtRadius && InBody.FerruleLength == Default.FerruleLength &&
		InBody.ShaftLength == Default.ShaftLength;
}

FString ARbCue::BakedMeshPackage(ERbCuePreset Preset)
{
	const UEnum* Enum = StaticEnum<ERbCuePreset>();
	const FString Name = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Preset)) : FString::FromInt(static_cast<int32>(Preset));
	return FString::Printf(TEXT("%s/SM_Cue_%s"), RbAssetPaths::CueMeshDir, *Name);
}

UStaticMesh* ARbCue::LoadBakedMesh(ERbCuePreset Preset)
{
	const FString Package = BakedMeshPackage(Preset);
	return RbCuePrivate::LoadIfExists<UStaticMesh>(*Package);
}
