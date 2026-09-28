#include "Balls/RbBallSet.h"

#include "RawBreak.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Table/RbTable.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/PackageName.h"

// Owner: UE-2.

namespace
{
	const TCHAR* const EngineSphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const EngineBasicMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const FName EngineColorParam(TEXT("Color"));

	// Long package name (RbAssetPaths) -> object path "<pkg>.<name>".
	FString ObjectPathOf(const TCHAR* PackagePath)
	{
		return FString::Printf(TEXT("%s.%s"), PackagePath, *FPackageName::GetShortName(PackagePath));
	}

	// Loads a generated asset only if its package exists (no warnings for assets another package has not generated yet).
	template <typename T>
	T* LoadGenerated(const TCHAR* PackagePath)
	{
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			return nullptr;
		}
		return LoadObject<T>(nullptr, *ObjectPathOf(PackagePath), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	constexpr int32 MpcBallCount = 16; // MPC_RbBalls holds Ball00 .. Ball15

	const FName& MpcBallName(int32 Index)
	{
		static const TArray<FName> Names = []
		{
			TArray<FName> Out;
			for (int32 I = 0; I < MpcBallCount; ++I)
			{
				Out.Add(RbAssetPaths::Param::MpcBall(I));
			}
			return Out;
		}();
		return Names[Index];
	}

	// Table-local pose WITHOUT the engine's FRotator round trip (plan pitfall 3): USceneComponent::SetRelativeLocationAndRotation
	// stores the rotation as a rotator (snapping by up to 0.08 deg within 0.08 deg of pitch +-90) and drops changes below
	// 1e-4 cm / 1e-4 deg. Here the exact quaternion goes into the rotation cache, so UpdateComponentToWorld uses it bitwise,
	// and the update is an ordinary transform change with ETeleportType::None (motion vectors kept).
	void ApplyExactPose(UStaticMeshComponent* Ball, const FVector& Location, const FQuat& Rotation)
	{
		FRotationConversionCache Cache;
		const FRotator Rotator = Cache.QuatToRotator(Rotation);
		Ball->SetRelativeRotationCache(Cache);
		Ball->SetRelativeLocation_Direct(Location);
		Ball->SetRelativeRotation_Direct(Rotator);
		Ball->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::None);
	}
}

ARbBallSet::ARbBallSet()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	Playback = CreateDefaultSubobject<URbShotPlaybackComponent>(TEXT("Playback"));
}

FLinearColor ARbBallSet::DefaultBallColor(int32 BallNumber)
{
	// Linear albedo, ESTIMATE (plan 6.2: cue ball 0.75-0.80, yellow (0.75, 0.50, 0.02), red (0.50, 0.03, 0.03),
	// black 0.02-0.03; the others by eye from WPA-style sets, to be measured with a colour checker).
	static const FLinearColor Colors[9] = {
		FLinearColor(0.78f, 0.77f, 0.74f),  // 0 cue ball (off-white)
		FLinearColor(0.75f, 0.50f, 0.02f),  // 1 yellow
		FLinearColor(0.02f, 0.06f, 0.38f),  // 2 blue
		FLinearColor(0.50f, 0.03f, 0.03f),  // 3 red
		FLinearColor(0.13f, 0.03f, 0.24f),  // 4 purple
		FLinearColor(0.80f, 0.20f, 0.02f),  // 5 orange
		FLinearColor(0.02f, 0.22f, 0.07f),  // 6 green
		FLinearColor(0.22f, 0.03f, 0.03f),  // 7 maroon
		FLinearColor(0.025f, 0.025f, 0.025f), // 8 black
	};
	if (BallNumber <= 0 || BallNumber > 15)
	{
		return Colors[0];
	}
	return Colors[BallNumber > 8 ? BallNumber - 8 : BallNumber];
}

void ARbBallSet::DestroyBalls()
{
	for (UStaticMeshComponent* Ball : BallComponents)
	{
		if (Ball)
		{
			RemoveInstanceComponent(Ball);
			Ball->DestroyComponent();
		}
	}
	BallComponents.Reset();
	BallMaterials.Reset();
	BallRadiiCm.Reset();
	BallOrientations.Reset();
}

UStaticMesh* ARbBallSet::ResolveBallMesh()
{
	if (UStaticMesh* Baked = LoadGenerated<UStaticMesh>(RbAssetPaths::BallMesh))
	{
		return Baked;
	}
	// Not baked yet (rb_bake_ball.py): the engine sphere (radius 50 cm) - the scale uses the mesh radius, so every ball
	// still has its exact own radius.
	UE_LOG(LogRawBreak, Display, TEXT("ARbBallSet: %s not baked - using the engine sphere"), RbAssetPaths::BallMesh);
	return LoadObject<UStaticMesh>(nullptr, EngineSphere);
}

UMaterialInterface* ARbBallSet::ResolveBallMaterial(bool& bOutEngineFallback) const
{
	bOutEngineFallback = false;
	if (!BallMaterialOverride.IsNull())
	{
		if (UMaterialInterface* Override = BallMaterialOverride.LoadSynchronous())
		{
			return Override;
		}
	}
	if (UMaterialInterface* Generated = LoadGenerated<UMaterialInterface>(RbAssetPaths::MatBall))
	{
		return Generated;
	}
	bOutEngineFallback = true;
	return LoadObject<UMaterialInterface>(nullptr, EngineBasicMaterial);
}

void ARbBallSet::InitForTable(ARbTable* InTable)
{
	Table = InTable;
	DestroyBalls();
	if (!InTable || !InTable->HasContext())
	{
		UE_LOG(LogRawBreak, Warning, TEXT("ARbBallSet %s: InitForTable without a table context"), *GetName());
		return;
	}
	AttachToComponent(InTable->GetClothOrigin(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Root->SetRelativeTransform(FTransform::Identity);

	const FRbTableContext& Context = InTable->GetContext();
	const int32 Count = FMath::Clamp(Context.Balls.Count, 0, rb::kMaxBalls);

	BallMesh = ResolveBallMesh();
	const double MeshRadius = BallMesh ? FMath::Max(BallMesh->GetBounds().BoxExtent.GetMax(), UE_DOUBLE_SMALL_NUMBER) : 1.0;
	bool bEngineMaterial = false;
	UMaterialInterface* Material = ResolveBallMaterial(bEngineMaterial);

	BallComponents.Reserve(Count);
	BallMaterials.Reserve(Count);
	for (int32 Id = 0; Id < Count; ++Id)
	{
		const double RadiusCm = FRbCoords::CmPerMeter * Context.BallRadius(Id);
		const FName Name = MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(*FString::Printf(TEXT("Ball%02d"), Id)));
		UStaticMeshComponent* Ball = NewObject<UStaticMeshComponent>(this, Name, RF_Transient);
		Ball->SetMobility(EComponentMobility::Movable);
		Ball->SetStaticMesh(BallMesh);
		Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ball->SetGenerateOverlapEvents(false);
		Ball->CanCharacterStepUpOn = ECB_No;
		Ball->SetCanEverAffectNavigation(false);
		Ball->bReceivesDecals = false;
		Ball->SetupAttachment(Root);
		Ball->SetRelativeScale3D(FVector(RadiusCm / MeshRadius));
		Ball->SetVisibility(false);

		UMaterialInstanceDynamic* Mid = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
		if (Mid)
		{
			const FLinearColor Color = DefaultBallColor(Id);
			Mid->SetScalarParameterValue(RbAssetPaths::Param::BallNumber, static_cast<float>(Id));
			Mid->SetVectorParameterValue(RbAssetPaths::Param::BallColor, Color);
			Mid->SetScalarParameterValue(RbAssetPaths::Param::BallRadiusCm, static_cast<float>(RadiusCm));
			Mid->SetScalarParameterValue(RbAssetPaths::Param::ExposureTime, ExposureTime);
			Mid->SetVectorParameterValue(RbAssetPaths::Param::BallOmegaLocal, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
			if (bEngineMaterial)
			{
				// Stripes cannot be shown by the engine material: a striped ball gets a light tint of its colour.
				Mid->SetVectorParameterValue(EngineColorParam, Id > 8 ? FMath::Lerp(Color, FLinearColor(0.78f, 0.77f, 0.74f), 0.5f) : Color);
			}
			Ball->SetMaterial(0, Mid);
		}
		Ball->RegisterComponent();
		AddInstanceComponent(Ball);

		BallComponents.Add(Ball);
		BallMaterials.Add(Mid);
		BallRadiiCm.Add(RadiusCm);
		BallOrientations.Add(FQuat::Identity);
	}

	if (!bOcclusionCollectionOverridden)
	{
		OcclusionCollection = LoadGenerated<UMaterialParameterCollection>(RbAssetPaths::BallMpc);
	}
	UpdateOcclusionParameters();
}

void ARbBallSet::SetBallCore(int32 BallId, const rb::Vec3& Position, const rb::Quat& Orientation)
{
	if (UStaticMeshComponent* Ball = GetBallComponent(BallId))
	{
		const FQuat Rotation = FRbCoords::OrientationToUE(Orientation);
		ApplyExactPose(Ball, FRbCoords::PositionToUE(Position), Rotation);
		BallOrientations[BallId] = Rotation;
		UpdateOcclusionParameter(BallId);
	}
}

void ARbBallSet::SetBallSpinCore(int32 BallId, const rb::Vec3& Omega)
{
	if (UMaterialInstanceDynamic* Mid = GetBallMaterial(BallId))
	{
		// Ball-local axes: the rotation smear works in the ball's own (mesh) frame (ue-architecture 4).
		const FVector Local = BallOrientations[BallId].UnrotateVector(FRbCoords::AngularVelocityToUE(Omega));
		Mid->SetVectorParameterValue(RbAssetPaths::Param::BallOmegaLocal,
			FLinearColor(static_cast<float>(Local.X), static_cast<float>(Local.Y), static_cast<float>(Local.Z), 0.0f));
	}
}

void ARbBallSet::SetBallVisible(int32 BallId, bool bVisible)
{
	if (UStaticMeshComponent* Ball = GetBallComponent(BallId))
	{
		if (Ball->IsVisible() != bVisible)
		{
			if (!bVisible)
			{
				// The renderer keeps a hidden primitive's last transform as its previous one: a ball shown again elsewhere (spotted,
				// a replay start, a seek back before its capture) would streak from where it disappeared. Drop it while the proxy
				// still exists (a hidden ball has none).
				Ball->ResetSceneVelocity();
			}
			Ball->SetVisibility(bVisible);
		}
		UpdateOcclusionParameter(BallId);
	}
}

bool ARbBallSet::IsBallVisible(int32 BallId) const
{
	const UStaticMeshComponent* Ball = GetBallComponent(BallId);
	return Ball && Ball->IsVisible();
}

void ARbBallSet::ShowSimBalls(const rb::SimBall* Balls, int32 Count)
{
	for (int32 Id = 0; Id < GetBallCount(); ++Id)
	{
		UStaticMeshComponent* Ball = BallComponents[Id];
		const bool bInPlay = Balls && Id < Count && Balls[Id].InPlay;
		if (!Ball)
		{
			continue;
		}
		if (!bInPlay)
		{
			SetBallVisible(Id, false);
			continue;
		}
		const FVector NewLocation = FRbCoords::PositionToUE(Balls[Id].State.Position);
		const FQuat NewRotation = FRbCoords::OrientationToUE(Balls[Id].Orientation);
		// Moved by more than 1e-3 cm / ~3e-6 rad (q and -q are one rotation; no acos, which is NaN at |dot| > 1).
		const bool bJump = !Ball->IsVisible() || !Ball->GetRelativeLocation().Equals(NewLocation, 1.0e-3)
			|| FMath::Abs(BallOrientations[Id] | NewRotation) < 1.0 - 1.0e-12;
		SetBallCore(Id, Balls[Id].State.Position, Balls[Id].Orientation);
		SetBallSpinCore(Id, rb::Vec3(0.0, 0.0, 0.0));
		SetBallVisible(Id, true);
		if (bJump)
		{
			ResetBallMotion(Id);
		}
	}
}

void ARbBallSet::ResetBallMotion(int32 BallId)
{
	if (UStaticMeshComponent* Ball = GetBallComponent(BallId))
	{
		// UE 5.8 keeps a primitive's previous transform ACROSS render-state re-creation (FSceneVelocityData: "persistent across
		// rendering state recreates"), and the first transform update of a primitive without velocity data takes the proxy's OLD
		// transform as the previous one - so neither a new proxy alone nor ResetSceneVelocity alone removes the streak of a jump.
		// Both together do: ResetSceneVelocity drops the velocity data by a render command that runs before this frame's scene
		// update, and the re-created proxy registers with previous = current. (Harmless while hidden / unregistered.)
		Ball->ResetSceneVelocity();
		Ball->MarkRenderStateDirty();
	}
}

UStaticMeshComponent* ARbBallSet::GetBallComponent(int32 BallId) const
{
	return BallComponents.IsValidIndex(BallId) ? BallComponents[BallId].Get() : nullptr;
}

UMaterialInstanceDynamic* ARbBallSet::GetBallMaterial(int32 BallId) const
{
	return BallMaterials.IsValidIndex(BallId) ? BallMaterials[BallId].Get() : nullptr;
}

double ARbBallSet::GetBallRadiusCm(int32 BallId) const
{
	return BallRadiiCm.IsValidIndex(BallId) ? BallRadiiCm[BallId] : 0.0;
}

FQuat ARbBallSet::GetBallOrientationUE(int32 BallId) const
{
	return BallOrientations.IsValidIndex(BallId) ? BallOrientations[BallId] : FQuat::Identity;
}

void ARbBallSet::SetExposureTime(float Seconds)
{
	ExposureTime = Seconds;
	for (UMaterialInstanceDynamic* Mid : BallMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(RbAssetPaths::Param::ExposureTime, ExposureTime);
		}
	}
}

void ARbBallSet::SetOcclusionCollection(UMaterialParameterCollection* Collection)
{
	OcclusionCollection = Collection;
	bOcclusionCollectionOverridden = true;
	UpdateOcclusionParameters();
}

void ARbBallSet::UpdateOcclusionParameters()
{
	UWorld* World = GetWorld();
	if (!OcclusionCollection || !World)
	{
		return;
	}
	if (UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(OcclusionCollection))
	{
		// Nominal (object-ball) radius; a ball's own radius is its centre height above the cloth.
		const double Nominal = BallRadiiCm.IsValidIndex(1) ? BallRadiiCm[1] : (BallRadiiCm.IsValidIndex(0) ? BallRadiiCm[0] : 0.0);
		Instance->SetScalarParameterValue(RbAssetPaths::Param::BallRadiusCm, static_cast<float>(Nominal));
	}
	for (int32 Id = 0; Id < MpcBallCount; ++Id)
	{
		UpdateOcclusionParameter(Id);
	}
}

void ARbBallSet::UpdateOcclusionParameter(int32 BallId)
{
	UWorld* World = GetWorld();
	if (!OcclusionCollection || !World || BallId < 0 || BallId >= MpcBallCount)
	{
		return;
	}
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(OcclusionCollection);
	if (!Instance)
	{
		return;
	}
	FLinearColor Value(0.0f, 0.0f, 0.0f, 0.0f);
	if (const UStaticMeshComponent* Ball = GetBallComponent(BallId))
	{
		const FVector World3 = Ball->GetComponentLocation();
		const bool bOnCloth = Ball->IsVisible() && Ball->GetRelativeLocation().Z >= 0.0;
		Value = FLinearColor(static_cast<float>(World3.X), static_cast<float>(World3.Y), static_cast<float>(World3.Z), bOnCloth ? 1.0f : 0.0f);
	}
	Instance->SetVectorParameterValue(MpcBallName(BallId), Value);
}
