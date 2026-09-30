#include "Player/RbBallInHandComponent.h"

#include "RawBreak.h"
#include "Balls/RbBallSet.h"
#include "Core/RbAssetPaths.h"
#include "Core/RbCoords.h"
#include "Game/RbTableSubsystem.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

// Owner: M2-F. The carrying hand of the header (P2): state machine, human follow (critically damped, sub-stepped, speed-limited),
// exact set-down on the previewed target, the refusal hitch, the visuals (hand, carried ball, contact preview).

namespace
{
	constexpr double kInternalStep = 1.0 / 480.0;
	constexpr double kReferenceRadius = 0.028575;   // the hand mesh is modelled around a 2 1/4 in ball
	constexpr double kRefuseSeconds = 0.45;         // the hesitation
	constexpr double kRefuseLiftCm = 0.8;
	constexpr double kRefuseShakeCm = 0.25;
	constexpr double kRefuseMoveOn = 0.005;         // [m] moving the target this far ends the refusal
	constexpr double kReleaseSeconds = 0.35;        // the hand withdraws after the set-down
	constexpr double kBedMargin = 0.001;            // [m] inside the nose rectangle less a radius
	constexpr double kOutsideLiftPerCm = 1.2;       // the ball rises over the rail when the hand is outside the bed
	constexpr double kOutsideLiftMaxCm = 6.0;
	constexpr double kPreviewDiameterRadii = 2.6;   // contact preview disc diameter / R
	constexpr double kClearanceMarginCm = 0.8;      // the carried ball passes this far above another ball
	constexpr double kClearanceLookAhead = 0.12;    // [s] of the hand's motion
	constexpr double kClearanceRiseSeconds = 0.06;
	constexpr double kClearanceFallSeconds = 0.25;
	const TCHAR* const kPreviewMaterial = TEXT("/Game/Generated/Player/M_RbContactPreview");
	const TCHAR* const kPlaneMesh = TEXT("/Engine/BasicShapes/Plane.Plane");

	double SmoothStep(double A)
	{
		A = FMath::Clamp(A, 0.0, 1.0);
		return A * A * (3.0 - 2.0 * A);
	}

	template <class T>
	T* LoadGenerated(const TCHAR* PackagePath)
	{
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			return nullptr;
		}
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackagePath, *FPackageName::GetShortName(PackagePath));
		return LoadObject<T>(nullptr, *ObjectPath);
	}

	UStaticMeshComponent* NewVisual(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), Name));
		Mesh->SetupAttachment(Parent);
		Mesh->SetUsingAbsoluteLocation(true);
		Mesh->SetUsingAbsoluteRotation(true);
		Mesh->SetUsingAbsoluteScale(true);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetHiddenInGame(true);
		Mesh->RegisterComponent();
		return Mesh;
	}
}

URbBallInHandComponent::URbBallInHandComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after the stroke component set the target this frame
}

// ---------------------------------------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------------------------------------

void URbBallInHandComponent::BeginCarry(ARbTable* InTable, int32 InBallId, double InBallRadius, TFunction<bool(const rb::Vec2&)> InIsLegal)
{
	if (bHidTableBall && (Table.Get() != InTable || BallId != InBallId))
	{
		RestoreTableBall(); // another ball / table: the previous one goes back where it was
	}
	Table = InTable;
	BallId = InBallId;
	BallRadius = InBallRadius > 0.0 ? InBallRadius : kReferenceRadius;
	IsLegal = MoveTemp(InIsLegal);
	FineOffset = rb::Vec2();
	bHasTarget = false;
	bHandValid = false;
	HandVelocity = rb::Vec2();
	StepRemainder = 0.0;
	ClearanceLiftCm = 0.0;
	// The ball was picked up: the table's instance disappears while the hand carries it.
	if (ARbBallSet* Balls = FindBallSet())
	{
		if (Balls->IsBallVisible(BallId))
		{
			Balls->SetBallVisible(BallId, false);
			bHidTableBall = true;
		}
	}
	SetState(ERbBallInHandState::Carrying);
	EnsureVisuals();
}

void URbBallInHandComponent::SetTargetCore(const rb::Vec2& Plan)
{
	LookTarget = Plan;
	bHasTarget = true;
	RecomputeTarget();
	if (State == ERbBallInHandState::Refused)
	{
		const double Dx = Target.x - RefusedAt.x;
		const double Dy = Target.y - RefusedAt.y;
		if (Dx * Dx + Dy * Dy > kRefuseMoveOn * kRefuseMoveOn)
		{
			SetState(ERbBallInHandState::Carrying);
		}
	}
}

void URbBallInHandComponent::AddFineAdjustCm(const FVector2D& DeltaCm)
{
	if (State != ERbBallInHandState::Carrying && State != ERbBallInHandState::Refused)
	{
		return;
	}
	// Mouse right / forward move the ball right / away from the player on the cloth, in the player's view frame.
	const AActor* Owner = GetOwner();
	const FVector Right = Owner ? Owner->GetActorRightVector().GetSafeNormal2D() : FVector::RightVector;
	const FVector Forward = Owner ? Owner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	const FVector WorldDelta = (Right * DeltaCm.X + Forward * DeltaCm.Y) * static_cast<double>(FineMetersPerCm);
	const ARbTable* T = Table.Get();
	const rb::Vec3 Core = T ? T->WorldDirectionToCore(WorldDelta) : FRbCoords::DirectionToCore(WorldDelta);
	FineOffset = rb::Vec2(FineOffset.x + Core.x, FineOffset.y + Core.y);
	const double Len = FMath::Sqrt(FineOffset.x * FineOffset.x + FineOffset.y * FineOffset.y);
	if (Len > MaxFineOffset && Len > 0.0)
	{
		FineOffset = rb::Vec2(FineOffset.x * MaxFineOffset / Len, FineOffset.y * MaxFineOffset / Len);
	}
	if (bHasTarget)
	{
		SetTargetCore(LookTarget);
	}
}

bool URbBallInHandComponent::RequestSetDown()
{
	if ((State != ERbBallInHandState::Carrying && State != ERbBallInHandState::Refused) || !bHasTarget)
	{
		return false;
	}
	if (IsLegal && !IsLegal(Target))
	{
		RefusedAt = Target;
		SetState(ERbBallInHandState::Refused); // restarts the hesitation even at the same spot
		OnRefused.Broadcast(Target);
		UE_LOG(LogRawBreak, Log, TEXT("RbBallInHand: set-down at (%.4f, %.4f) refused (illegal spot)"), Target.x, Target.y);
		return false;
	}
	LowerFrom = bHandValid ? HandPlan : Target;
	LowerFromBottomCm = bHandValid ? ShownBottomCm : HoverHeightCm;
	SetState(ERbBallInHandState::Lowering);
	return true;
}

void URbBallInHandComponent::Cancel()
{
	RestoreTableBall();
	SetState(ERbBallInHandState::Inactive);
	HideVisuals();
}

FVector URbBallInHandComponent::GetBallWorld() const
{
	const ARbTable* T = Table.Get();
	const rb::Vec2 Plan = bHandValid ? ShownPlan : Target;
	const double Bottom = bHandValid ? ShownBottomCm : HoverHeightCm;
	const rb::Vec3 Core(Plan.x, Plan.y, BallRadius + 0.01 * Bottom);
	if (!T || !T->HasContext())
	{
		return T ? T->CoreToWorld(Core) : FRbCoords::PositionToUE(Core);
	}
	return T->CoreToWorld(Core);
}

bool URbBallInHandComponent::IsTargetLegal() const
{
	return bHasTarget && (!IsLegal || IsLegal(Target));
}

// ---------------------------------------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------------------------------------

void URbBallInHandComponent::SetState(ERbBallInHandState NewState)
{
	State = NewState;
	StateTime = 0.0;
}

rb::Vec2 URbBallInHandComponent::ClampToBed(const rb::Vec2& Plan) const
{
	const ARbTable* T = Table.Get();
	if (!T || !T->HasContext())
	{
		return Plan;
	}
	const rb::rules::RulesTable& Rules = T->GetContext().RulesTable;
	const double HalfL = FMath::Max(0.0, 0.5 * Rules.Length - BallRadius - kBedMargin);
	const double HalfW = FMath::Max(0.0, 0.5 * Rules.Width - BallRadius - kBedMargin);
	return rb::Vec2(FMath::Clamp(Plan.x, -HalfL, HalfL), FMath::Clamp(Plan.y, -HalfW, HalfW));
}

void URbBallInHandComponent::RecomputeTarget()
{
	Target = ClampToBed(rb::Vec2(LookTarget.x + FineOffset.x, LookTarget.y + FineOffset.y));
}

void URbBallInHandComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickCarry(DeltaTime);
}

void URbBallInHandComponent::TickCarry(double DeltaSeconds)
{
	const double Dt = FMath::Max(0.0, DeltaSeconds);
	Time += Dt;
	StateTime += Dt;
	switch (State)
	{
	case ERbBallInHandState::Inactive:
		return;

	case ERbBallInHandState::Carrying:
	case ERbBallInHandState::Refused:
	{
		if (!bHasTarget)
		{
			HideVisuals();
			return;
		}
		if (!bHandValid)
		{
			// The hand comes into view from where the player stands (never pops up at the target), or at the target without a player.
			HandPlan = Target;
			const ARbTable* T = Table.Get();
			if (GetOwner() && T)
			{
				const rb::Vec3 Near = T->WorldToCore(ShoulderWorld());
				HandPlan = rb::Vec2(Near.x, Near.y);
			}
			HandVelocity = rb::Vec2();
			bHandValid = true;
		}
		// Human lag: a critically damped follow, sub-stepped at a fixed rate and speed-limited (the per-frame step is bounded).
		const double Omega = UE_DOUBLE_TWO_PI * FMath::Max(0.1, static_cast<double>(FollowHz));
		const double MaxSpeed = FMath::Max(0.05, static_cast<double>(MaxHandSpeed));
		StepRemainder += Dt;
		while (StepRemainder >= kInternalStep)
		{
			StepRemainder -= kInternalStep;
			const double Ax = Omega * Omega * (Target.x - HandPlan.x) - 2.0 * Omega * HandVelocity.x;
			const double Ay = Omega * Omega * (Target.y - HandPlan.y) - 2.0 * Omega * HandVelocity.y;
			double Vx = HandVelocity.x + Ax * kInternalStep;
			double Vy = HandVelocity.y + Ay * kInternalStep;
			const double V = FMath::Sqrt(Vx * Vx + Vy * Vy);
			if (V > MaxSpeed)
			{
				Vx *= MaxSpeed / V;
				Vy *= MaxSpeed / V;
			}
			HandVelocity = rb::Vec2(Vx, Vy);
			HandPlan = rb::Vec2(HandPlan.x + Vx * kInternalStep, HandPlan.y + Vy * kInternalStep);
		}
		// Tremor (horizontal ~7 Hz + a slow drift; vertical only upward) and, when refused, the hesitation: a small lift and shake.
		const double Tremor = 0.001 * TremorMm;
		double Px = HandPlan.x + Tremor * (0.6 * FMath::Sin(UE_DOUBLE_TWO_PI * 6.7 * Time) + 0.4 * FMath::Sin(UE_DOUBLE_TWO_PI * 0.43 * Time + 1.3));
		double Py = HandPlan.y + Tremor * (0.6 * FMath::Sin(UE_DOUBLE_TWO_PI * 7.3 * Time + 2.1) + 0.4 * FMath::Sin(UE_DOUBLE_TWO_PI * 0.37 * Time));
		double Lift = 0.05 * TremorMm * (1.0 + FMath::Sin(UE_DOUBLE_TWO_PI * 5.9 * Time)); // [cm], >= 0
		if (State == ERbBallInHandState::Refused && StateTime < kRefuseSeconds)
		{
			const double A = StateTime / kRefuseSeconds;
			Lift += kRefuseLiftCm * FMath::Sin(UE_DOUBLE_PI * A);
			Px += 0.01 * kRefuseShakeCm * FMath::Sin(UE_DOUBLE_TWO_PI * 6.0 * StateTime) * (1.0 - A);
		}
		ShownPlan = rb::Vec2(Px, Py);
		// Over the rail (the hand still outside the bed) the ball rises so it never passes through the cushion.
		const rb::Vec2 Clamped = ClampToBed(HandPlan);
		const double Outside = 100.0 * FMath::Sqrt(FMath::Square(HandPlan.x - Clamped.x) + FMath::Square(HandPlan.y - Clamped.y));
		// Over another ball the hand lifts the carried one clear of it (a person never drags a ball through the others): the
		// clearance needed here and a moment ahead on the hand's path, eased up quickly and down slowly (never a jump).
		const rb::Vec2 Ahead(HandPlan.x + HandVelocity.x * kClearanceLookAhead, HandPlan.y + HandVelocity.y * kClearanceLookAhead);
		const double Needed = FMath::Max(0.0, FMath::Max(ClearanceBottomCm(HandPlan), ClearanceBottomCm(Ahead)) - HoverHeightCm);
		const double Tau = Needed > ClearanceLiftCm ? kClearanceRiseSeconds : kClearanceFallSeconds;
		ClearanceLiftCm = Needed + (ClearanceLiftCm - Needed) * FMath::Exp(-Dt / Tau);
		ClearanceLiftCm = FMath::Max(ClearanceLiftCm, FMath::Max(0.0, ClearanceBottomCm(HandPlan) - HoverHeightCm)); // never inside a ball
		ShownBottomCm = HoverHeightCm + Lift + FMath::Max(ClearanceLiftCm, FMath::Min(kOutsideLiftMaxCm, kOutsideLiftPerCm * Outside));
		break;
	}

	case ERbBallInHandState::Lowering:
	{
		// The hand lines the ball up with the target in the first part and sets it down on EXACTLY the target.
		const double A = LowerSeconds > 0.0f ? FMath::Clamp(StateTime / LowerSeconds, 0.0, 1.0) : 1.0;
		const double Align = SmoothStep(FMath::Min(1.0, 1.6 * A));
		ShownPlan = rb::Vec2(FMath::Lerp(LowerFrom.x, Target.x, Align), FMath::Lerp(LowerFrom.y, Target.y, Align));
		ShownBottomCm = LowerFromBottomCm * (1.0 - SmoothStep(A));
		HandPlan = ShownPlan;
		HandVelocity = rb::Vec2();
		if (A >= 1.0)
		{
			ShownPlan = Target;
			HandPlan = Target;
			ShownBottomCm = 0.0;
			bHidTableBall = false; // the director shows the placed ball; a refusal of the placement re-arms the carry
			SetState(ERbBallInHandState::Placed);
			UpdateVisuals();
			OnSetDown.Broadcast(Target);
			return;
		}
		break;
	}

	case ERbBallInHandState::Placed:
		if (StateTime >= kReleaseSeconds)
		{
			SetState(ERbBallInHandState::Inactive);
			HideVisuals();
			return;
		}
		break;
	}
	UpdateVisuals();
}

// ---------------------------------------------------------------------------------------------------------
// Visuals
// ---------------------------------------------------------------------------------------------------------

ARbBallSet* URbBallInHandComponent::FindBallSet() const
{
	const ARbTable* T = Table.Get();
	const URbTableSubsystem* Tables = T ? URbTableSubsystem::Get(T) : nullptr;
	return Tables ? Tables->FindBallSet(T) : nullptr;
}

void URbBallInHandComponent::RestoreTableBall()
{
	if (!bHidTableBall)
	{
		return;
	}
	bHidTableBall = false;
	if (ARbBallSet* Balls = FindBallSet())
	{
		Balls->SetBallVisible(BallId, true);
	}
}

double URbBallInHandComponent::ClearanceBottomCm(const rb::Vec2& Plan) const
{
	// The lowest bottom height [cm above the cloth] of the carried ball at Plan that clears every other ball shown on the table:
	// spheres R and Rj with plan distance d need a centre height difference sqrt((R + Rj)^2 - d^2).
	const ARbTable* T = Table.Get();
	const ARbBallSet* Balls = T ? FindBallSet() : nullptr;
	if (!Balls)
	{
		return 0.0;
	}
	double Bottom = 0.0;
	for (int32 Id = 0; Id < Balls->GetBallCount(); ++Id)
	{
		const UStaticMeshComponent* Other = Id != BallId && Balls->IsBallVisible(Id) ? Balls->GetBallComponent(Id) : nullptr;
		if (!Other)
		{
			continue;
		}
		const rb::Vec3 C = T->WorldToCore(Other->GetComponentLocation());
		const double Rj = 0.01 * Balls->GetBallRadiusCm(Id);
		const double D2 = FMath::Square(C.x - Plan.x) + FMath::Square(C.y - Plan.y);
		const double Reach = BallRadius + Rj + 0.01 * kClearanceMarginCm;
		if (D2 < Reach * Reach)
		{
			// Carried centre height = BallRadius + bottom; the other centre at C.z.
			Bottom = FMath::Max(Bottom, 100.0 * (C.z + FMath::Sqrt(Reach * Reach - D2) - BallRadius));
		}
	}
	return Bottom;
}

FVector URbBallInHandComponent::ShoulderWorld() const
{
	// The carrying (right) shoulder: below and to the right of the eye.
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return GetComponentLocation();
	}
	const UCameraComponent* Eye = Owner->FindComponentByClass<UCameraComponent>();
	const FVector EyeLocation = Eye ? Eye->GetComponentLocation() : Owner->GetActorLocation();
	const FVector Forward = Eye ? Eye->GetForwardVector().GetSafeNormal2D() : Owner->GetActorForwardVector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	return EyeLocation - FVector::UpVector * 28.0 + Right * 18.0 + Forward * 5.0;
}

void URbBallInHandComponent::EnsureVisuals()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !World->IsGameWorld() || HandMesh)
	{
		return;
	}
	HandMesh = NewVisual(Owner, this, TEXT("CarryHand"));
	if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(RbAssetPaths::HandCarryMesh))
	{
		HandMesh->SetStaticMesh(Mesh);
	}
	HandMesh->SetCastShadow(true);
	ArmMesh = NewVisual(Owner, this, TEXT("CarryArm"));
	if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(RbHandMesh::ArmMeshPath))
	{
		ArmMesh->SetStaticMesh(Mesh);
	}
	ArmMesh->SetCastShadow(true);
	BallMesh = NewVisual(Owner, this, TEXT("CarriedBall"));
	BallMesh->SetCastShadow(true);
	PreviewMesh = NewVisual(Owner, this, TEXT("ContactPreview"));
	PreviewMesh->SetCastShadow(false);
	UMaterialInterface* PreviewMaterial = LoadGenerated<UMaterialInterface>(kPreviewMaterial);
	if (PreviewMaterial)
	{
		PreviewMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, kPlaneMesh));
		PreviewMesh->SetMaterial(0, UMaterialInstanceDynamic::Create(PreviewMaterial, PreviewMesh));
	}
}

void URbBallInHandComponent::HideVisuals()
{
	for (UStaticMeshComponent* Mesh : {HandMesh.Get(), ArmMesh.Get(), BallMesh.Get(), PreviewMesh.Get()})
	{
		if (Mesh)
		{
			Mesh->SetHiddenInGame(true);
		}
	}
}

void URbBallInHandComponent::UpdateVisuals()
{
	const ARbTable* T = Table.Get();
	if (!HandMesh || !T || !T->HasContext())
	{
		return;
	}
	const bool bCarried = State == ERbBallInHandState::Carrying || State == ERbBallInHandState::Refused || State == ERbBallInHandState::Lowering;
	const FVector Ball = GetBallWorld();
	const FVector Shoulder = ShoulderWorld();

	// The carried ball: the table set's mesh and material of this ball, at the table set's scale.
	if (BallMesh)
	{
		if (const ARbBallSet* Balls = FindBallSet())
		{
			if (!BallMesh->GetStaticMesh() && Balls->GetBallMesh())
			{
				BallMesh->SetStaticMesh(Balls->GetBallMesh());
				if (UMaterialInterface* Material = Balls->GetBallMaterial(BallId))
				{
					BallMesh->SetMaterial(0, Material);
				}
			}
			if (const UStaticMeshComponent* TableBall = Balls->GetBallComponent(BallId))
			{
				BallMesh->SetWorldScale3D(TableBall->GetComponentScale());
				BallMesh->SetWorldRotation(TableBall->GetComponentQuat());
			}
		}
		BallMesh->SetWorldLocation(Ball);
		BallMesh->SetHiddenInGame(!bCarried || !BallMesh->GetStaticMesh());
	}

	// The contact preview: a soft dark disc on the cloth under EXACTLY the target (where the ball will touch), stronger as it
	// comes down.
	if (PreviewMesh)
	{
		const FVector OnCloth = T->CoreToWorld(rb::Vec3(Target.x, Target.y, 0.0003));
		const double DiameterCm = kPreviewDiameterRadii * BallRadius * 100.0;
		PreviewMesh->SetWorldLocationAndRotation(OnCloth, T->GetActorQuat());
		PreviewMesh->SetWorldScale3D(FVector(DiameterCm / 100.0, DiameterCm / 100.0, 1.0));
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(PreviewMesh->GetMaterial(0)))
		{
			const double Near = 1.0 - FMath::Clamp(ShownBottomCm / FMath::Max(1.0, static_cast<double>(HoverHeightCm)), 0.0, 1.0);
			Mid->SetScalarParameterValue(TEXT("Strength"), static_cast<float>(0.55 + 0.35 * Near));
		}
		PreviewMesh->SetHiddenInGame(!bCarried || !bHasTarget || !PreviewMesh->GetStaticMesh());
	}

	// The hand: the palm over the ball, the fingers pointing away from the shoulder and turned inward by HandYawOffsetDeg (the elbow
	// out to the side: the wrist is not between the eye and the ball, the ball stays visible), the forearm rising toward the elbow
	// (the mesh's RbHandMesh::ForearmElevationDeg, pitched by + (that - the elevation of the shoulder), limited so the fingers stay on
	// the ball); after the set-down it lets go and withdraws.
	FVector Away = (Ball - Shoulder).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}
	const double Horizontal = FVector::Dist2D(Ball, Shoulder);
	const double ForearmElevation = FMath::RadiansToDegrees(FMath::Atan2(Shoulder.Z - Ball.Z, FMath::Max(1.0, Horizontal)));
	const double Tilt = FMath::Clamp(RbHandMesh::ForearmElevationDeg - ForearmElevation, -15.0, 15.0);
	const FQuat HandRot = FRotator(Tilt, Away.Rotation().Yaw + HandYawOffsetDeg, 0.0).Quaternion();
	FVector HandLocation = Ball;
	if (State == ERbBallInHandState::Placed)
	{
		const double A = SmoothStep(StateTime / kReleaseSeconds);
		HandLocation += (Shoulder - Ball).GetSafeNormal() * (18.0 * A) + FVector::UpVector * (4.0 * A);
	}
	const double Scale = BallRadius / kReferenceRadius;
	const bool bShowHand = bCarried || State == ERbBallInHandState::Placed;
	HandMesh->SetWorldLocationAndRotation(HandLocation, HandRot);
	HandMesh->SetWorldScale3D(FVector(Scale));
	HandMesh->SetHiddenInGame(!bShowHand || !HandMesh->GetStaticMesh());

	// The shirt sleeve from the rolled cuff to the shoulder (a straight arm: the elbow is out of view, M3 brings the real arm).
	if (ArmMesh)
	{
		const FVector Cuff = HandLocation + HandRot.RotateVector(RbHandMesh::CuffEnd() * Scale);
		const FVector ToShoulder = Shoulder - Cuff;
		const double Length = ToShoulder.Size() + 8.0; // a little past the shoulder: the end is never seen
		if (Length > 10.0)
		{
			ArmMesh->SetWorldLocationAndRotation(Cuff, FRotationMatrix::MakeFromXZ(ToShoulder, FVector::UpVector).ToQuat());
			ArmMesh->SetWorldScale3D(FVector(Length / RbHandMesh::ArmLengthCm, Scale, Scale));
		}
		ArmMesh->SetHiddenInGame(!bShowHand || Length <= 10.0 || !ArmMesh->GetStaticMesh());
	}
}

void URbBallInHandComponent::OnUnregister()
{
	RestoreTableBall();
	Super::OnUnregister();
}
