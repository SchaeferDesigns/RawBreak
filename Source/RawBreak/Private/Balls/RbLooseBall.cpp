#include "Balls/RbLooseBall.h"

#include "Balls/RbLooseBallSubsystem.h"
#include "Core/RbAssetPaths.h"
#include "Table/RbTable.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

// Owner: M2-E. Engine-physics ball after the hand-off (header: components, contact materials, rolling resistance, rest, events).

namespace RbLooseBallPrivate
{
	constexpr double GravityCmS2 = 980.665;
	constexpr double GroundProbeCm = 0.6;        // below the ball's bottom: "on a surface"
	constexpr double MinGroundNormalZ = 0.5;     // steeper surfaces do not support (walls)
	constexpr double FloorImpactNormalZ = 0.5;   // hits from below (within 60 deg of up)

	UPhysicalMaterial* LoadPhysicalMaterial(const TCHAR* PackagePath)
	{
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			return nullptr; // rb_make_physics.py has not run: the engine default material (friction 0.7, restitution 0.3)
		}
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackagePath, *FPackageName::GetShortName(PackagePath));
		return LoadObject<UPhysicalMaterial>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

ARbLooseBall::ARbLooseBall()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics; // rolling resistance goes into the coming physics step

	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitSphereRadius(2.8575f);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(RbAssetPaths::Collision::LooseBallProfile);
	Body->SetGenerateOverlapEvents(false);
	Body->CanCharacterStepUpOn = ECB_No;
	Body->SetCanEverAffectNavigation(false);
	Body->SetHiddenInGame(true); // the sphere is the physics shape only; Ball is what is seen
	Body->BodyInstance.bUseCCD = true;
	Body->BodyInstance.bNotifyRigidBodyCollision = true;
	Body->BodyInstance.LinearDamping = 0.0f;
	Body->BodyInstance.AngularDamping = 0.0f;
	// The engine's 3600 deg/s spin cap would clamp a ball rolling faster than 1.8 m/s (header: spin cap).
	Body->BodyInstance.bOverrideMaxAngularVelocity = true;
	Body->BodyInstance.MaxAngularVelocity = static_cast<float>(FMath::RadiansToDegrees(MaxSpinRadS));

	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ball"));
	Ball->SetupAttachment(Body);
	Ball->SetMobility(EComponentMobility::Movable);
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetGenerateOverlapEvents(false);
	Ball->SetCanEverAffectNavigation(false);
	Ball->bReceivesDecals = false;

	Tags.Add(RbAssetPaths::Tag::LooseBall);
}

void ARbLooseBall::Launch(ARbTable* InTable, int32 InBallId, UStaticMesh* Mesh, UMaterialInterface* Material, double InRadiusCm,
	double InMassKg, const FVector& LinearVelocityCmS, const FVector& AngularVelocityRadS)
{
	Table = InTable;
	TableIndex = InTable ? InTable->TableIndex : INDEX_NONE;
	BallId = InBallId;
	RadiusCm = FMath::Max(InRadiusCm, 0.1);
	MassKg = InMassKg > 0.0 ? InMassKg : 0.17;

	// Visual: the ball set's unit mesh scaled to the radius (SM_RbBall: 1 cm; the engine sphere fallback: 50 cm).
	Ball->SetStaticMesh(Mesh);
	const double MeshRadius = Mesh ? FMath::Max(Mesh->GetBounds().BoxExtent.GetMax(), UE_DOUBLE_SMALL_NUMBER) : 1.0;
	Ball->SetRelativeScale3D(FVector(RadiusCm / MeshRadius));
	if (Material)
	{
		// Its own instance (the rotation smear follows THIS ball's spin), with every parameter of the table ball's instance.
		if (UMaterialInstanceDynamic* Source = Cast<UMaterialInstanceDynamic>(Material))
		{
			BallMaterial = UMaterialInstanceDynamic::Create(Source->Parent ? Source->Parent.Get() : Material, this);
			BallMaterial->CopyParameterOverrides(Source);
		}
		else
		{
			BallMaterial = UMaterialInstanceDynamic::Create(Material, this);
		}
		Ball->SetMaterial(0, BallMaterial);
	}

	// Physics: exact sphere, profile, phenolic ball material, CCD, hit events.
	Body->SetSphereRadius(static_cast<float>(RadiusCm), false);
	Body->SetCollisionProfileName(RbAssetPaths::Collision::LooseBallProfile);
	if (UPhysicalMaterial* BallMaterialPhys = RbLooseBallPrivate::LoadPhysicalMaterial(RbAssetPaths::PhysMatBall))
	{
		Body->SetPhysMaterialOverride(BallMaterialPhys);
	}
	Body->SetUseCCD(true);
	Body->SetNotifyRigidBodyCollision(true);
	Body->OnComponentHit.AddUniqueDynamic(this, &ARbLooseBall::OnBodyHit);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(0.0f);
	Body->SetSimulatePhysics(true);
	Body->BodyInstance.SetMaxAngularVelocityInRadians(static_cast<float>(MaxSpinRadS), false); // onto the live particle, before the spin
	Body->SetMassOverrideInKg(NAME_None, static_cast<float>(MassKg), true);
	Body->SetPhysicsLinearVelocity(LinearVelocityCmS);
	Body->SetPhysicsAngularVelocityInRadians(AngularVelocityRadS);

	PreStepVelocity = LinearVelocityCmS;
	RestSeconds = 0.0;
	bGrounded = false;
	GroundActor.Reset();
	ImpactCount = 0;
	FloorImpactCount = 0;
	MaxImpactSpeed = 0.0;
	MaxZAfterFloorImpact = -UE_BIG_NUMBER;
	SetActorTickEnabled(true);
}

FVector ARbLooseBall::GetLinearVelocity() const
{
	return Body->IsSimulatingPhysics() ? Body->GetPhysicsLinearVelocity() : FVector::ZeroVector;
}

FVector ARbLooseBall::GetAngularVelocity() const
{
	return Body->IsSimulatingPhysics() ? Body->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
}

bool ARbLooseBall::IsResting() const
{
	return !Body->IsSimulatingPhysics() || !Body->IsAnyRigidBodyAwake() || RestSeconds >= RestHoldSeconds;
}

bool ARbLooseBall::IsOnTable() const
{
	return bGrounded && Cast<ARbTable>(GroundActor.Get()) != nullptr;
}

double ARbLooseBall::RollingResistanceFor(EPhysicalSurface Surface)
{
	switch (Surface)
	{
	case RbAssetPaths::Surface::Vct: return 0.020;
	case RbAssetPaths::Surface::Concrete: return 0.020;
	case RbAssetPaths::Surface::Wood: return 0.015;
	case RbAssetPaths::Surface::Rubber: return 0.080;
	case RbAssetPaths::Surface::Cloth: return 0.010;
	default: break;
	}
	return 0.020;
}

void ARbLooseBall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DeltaSeconds > 0.0f)
	{
		UpdateMotion(DeltaSeconds);
	}
}

void ARbLooseBall::UpdateGround()
{
	using namespace RbLooseBallPrivate;
	bGrounded = false;
	GroundActor.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RbLooseBallGround), false, this);
	Params.bReturnPhysicalMaterial = true;
	const FVector Center = Body->GetComponentLocation();
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Center, Center - FVector(0.0, 0.0, RadiusCm + GroundProbeCm), RbAssetPaths::Collision::LooseBallChannel, Params)
		&& Hit.bBlockingHit && Hit.ImpactNormal.Z >= MinGroundNormalZ)
	{
		bGrounded = true;
		GroundNormal = Hit.ImpactNormal;
		GroundSurface = Hit.PhysMaterial.IsValid() ? Hit.PhysMaterial->SurfaceType.GetValue() : SurfaceType_Default;
		GroundActor = Hit.GetActor();
	}
}

void ARbLooseBall::UpdateMotion(double DeltaSeconds)
{
	using namespace RbLooseBallPrivate;
	if (!Body->IsSimulatingPhysics())
	{
		RestSeconds += DeltaSeconds;
		bGrounded = false;
		GroundActor.Reset();
		return;
	}
	FVector V = Body->GetPhysicsLinearVelocity();
	FVector W = Body->GetPhysicsAngularVelocityInRadians();
	UpdateGround();
	const bool bAwake = Body->IsAnyRigidBodyAwake();
	if (bGrounded && bAwake)
	{
		// Constant rolling resistance mu_r g: the tangential velocity and the rolling spin's surface speed |w_t| R each lose the
		// same amount (clamped at zero), so a rolling ball keeps rolling (v = w x r stays true) while a ball spinning in place
		// (it landed with spin, or bounced straight back from a wall) keeps its spin for the contact friction to turn into roll
		// (M2-E review: one shared factor zeroed that spin whenever the ball's own speed was below one tick's decrement). The
		// spin about the normal decays (pivoting friction).
		const double Mu = RollingResistanceFor(GroundSurface);
		const FVector Vn = GroundNormal * (V | GroundNormal);
		const FVector Vt = V - Vn;
		const FVector Wn = GroundNormal * (W | GroundNormal);
		const FVector Wt = W - Wn;
		const double Decel = Mu * GravityCmS2 * DeltaSeconds;
		const auto Shrink = [Decel](double SurfaceSpeed) { return SurfaceSpeed > Decel ? (SurfaceSpeed - Decel) / SurfaceSpeed : 0.0; };
		const double PivotDecay = FMath::Exp(-DeltaSeconds / FMath::Max(PivotSpinDecaySeconds, 1.0e-3));
		const FVector NewV = Vn + Vt * Shrink(Vt.Size());
		const FVector NewW = Wt * Shrink(Wt.Size() * RadiusCm) + Wn * PivotDecay;
		Body->SetPhysicsLinearVelocity(NewV);
		Body->SetPhysicsAngularVelocityInRadians(NewW);
		V = NewV;
		W = NewW;
	}
	PreStepVelocity = V;

	const FVector Location = Body->GetComponentLocation();
	if (FloorImpactCount > 0)
	{
		MaxZAfterFloorImpact = FMath::Max(MaxZAfterFloorImpact, Location.Z);
	}

	// Rest: slower than RestSpeedCmS (centre and surface speed) for RestHoldSeconds -> asleep.
	const bool bSlow = V.Size() < RestSpeedCmS && W.Size() * RadiusCm < RestSpeedCmS;
	RestSeconds = (!bAwake || bSlow) ? RestSeconds + DeltaSeconds : 0.0;
	if (bAwake && RestSeconds >= RestHoldSeconds)
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		Body->PutRigidBodyToSleep();
		V = FVector::ZeroVector;
		W = FVector::ZeroVector;
	}

	// Rolling noise (AU-25) - not while hidden: its table replays, the live copy is neither seen nor heard.
	URbLooseBallSubsystem* Subsystem = IsHidden() ? nullptr : URbLooseBallSubsystem::Get(this);
	const double Speed = V.Size();
	if (Subsystem && bGrounded && Speed >= RestSpeedCmS)
	{
		FRbLooseBallRolling Rolling;
		Rolling.TableIndex = TableIndex;
		Rolling.BallId = BallId;
		Rolling.WorldLocation = Location;
		Rolling.SpeedMps = 0.01 * Speed;
		Rolling.Surface = GroundSurface;
		Subsystem->OnRolling.Broadcast(Rolling);
	}

	// Rotation smear from the physics spin (ball-local axes, like ARbBallSet::SetBallSpinCore).
	if (BallMaterial)
	{
		const FVector Local = Body->GetComponentQuat().UnrotateVector(W);
		BallMaterial->SetVectorParameterValue(RbAssetPaths::Param::BallOmegaLocal,
			FLinearColor(static_cast<float>(Local.X), static_cast<float>(Local.Y), static_cast<float>(Local.Z), 0.0f));
	}
}

void ARbLooseBall::OnBodyHit(UPrimitiveComponent* /*HitComponent*/, AActor* /*OtherActor*/, UPrimitiveComponent* /*OtherComponent*/,
	FVector NormalImpulse, const FHitResult& Hit)
{
	using namespace RbLooseBallPrivate;
	const FVector Center = Body->GetComponentLocation();
	FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		Normal = (Center - Hit.ImpactPoint).GetSafeNormal();
	}
	if ((Normal | (Center - Hit.ImpactPoint)) < 0.0)
	{
		Normal = -Normal; // from the contact toward the ball's centre
	}
	const double ImpulseNs = 0.01 * NormalImpulse.Size(); // kg cm/s -> N s
	const double ApproachMps = FMath::Max(0.0, -(PreStepVelocity | Normal)) * 0.01;
	const double NormalSpeed = FMath::Max(ApproachMps, 0.0);
	if (NormalSpeed < MinImpactSpeedMps)
	{
		return; // continuous contact (rolling), not an impact
	}
	++ImpactCount;
	MaxImpactSpeed = FMath::Max(MaxImpactSpeed, NormalSpeed);
	if (Normal.Z >= FloorImpactNormalZ)
	{
		if (FloorImpactCount == 0)
		{
			MaxZAfterFloorImpact = Center.Z;
		}
		++FloorImpactCount;
	}
	// The next hit in this step compares against the velocity after this one (a second contact of the same step is rarer).
	PreStepVelocity = Body->GetPhysicsLinearVelocity();

	URbLooseBallSubsystem* Subsystem = IsHidden() ? nullptr : URbLooseBallSubsystem::Get(this); // hidden: a replay of its table plays
	if (Subsystem)
	{
		FRbLooseBallImpact Impact;
		Impact.TableIndex = TableIndex;
		Impact.BallId = BallId;
		Impact.WorldLocation = Hit.ImpactPoint;
		Impact.Normal = Normal;
		Impact.NormalImpulse = ImpulseNs;
		Impact.NormalSpeed = NormalSpeed;
		Impact.MassKg = MassKg;
		Impact.Surface = Hit.PhysMaterial.IsValid() ? Hit.PhysMaterial->SurfaceType.GetValue() : SurfaceType_Default;
		Subsystem->OnImpact.Broadcast(Impact);
	}
}
