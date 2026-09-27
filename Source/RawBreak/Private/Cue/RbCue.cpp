#include "Cue/RbCue.h"

#include "Core/RbCoords.h"
#include "Table/RbTable.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

// Owner: UE-4. TODO(UE-4): mesh build / baked asset, material, visibility per drive, tests.

ARbCue::ARbCue()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	RuntimeMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("RuntimeMesh"));
	RuntimeMesh->SetupAttachment(Root);
	RuntimeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BakedMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BakedMesh"));
	BakedMesh->SetupAttachment(Root);
	BakedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BakedMesh->SetMobility(EComponentMobility::Movable);
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
	// TODO(UE-4): build RuntimeMesh (RbCueMeshBuilder) or load the baked mesh.
}

void ARbCue::SetPoseCore(const rb::Vec3& TipDomeCenter, const rb::Vec3& Direction)
{
	// Local mesh frame: +X from the butt to the tip, origin at the tip dome centre (RbCueMeshBuilder.h).
	const FVector Location = FRbCoords::PositionToUE(TipDomeCenter);
	const FVector Axis = FRbCoords::DirectionToUE(Direction).GetSafeNormal();
	SetActorRelativeLocation(Location, false, nullptr, ETeleportType::None);
	SetActorRelativeRotation(FRotationMatrix::MakeFromX(Axis).ToQuat(), false, nullptr, ETeleportType::None);
}

void ARbCue::SetDrive(ERbCueDrive InDrive)
{
	Drive = InDrive;
	SetActorHiddenInGame(Drive == ERbCueDrive::Hidden);
}
