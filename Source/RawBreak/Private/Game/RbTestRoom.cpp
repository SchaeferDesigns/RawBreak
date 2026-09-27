#include "Game/RbTestRoom.h"

#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

// Owner: UE-8. TODO(UE-8): surfaces from /Engine/BasicShapes/Cube with M_RbRoomWall / M_RbRoomFloor, lamp housing +
// diffuser (hidden in RT reflections), rect lights in lumen units with source size, ambient light, tests (E4 lux probe
// >= 520 lux on bed and rails using RbCameraMath::IlluminanceAt on the placed lights).

ARbTestRoom::ARbTestRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ARbTestRoom::RebuildRoom()
{
	// TODO(UE-8)
}

void ARbTestRoom::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRoom();
}
