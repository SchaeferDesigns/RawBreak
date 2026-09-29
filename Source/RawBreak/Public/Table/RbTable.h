#pragma once

// The physical pool table (Docs/ue-architecture.md 5.3, 4): owns the immutable FRbTableContext (spec, exact
// geometry, physics parameters, ball specs, rules table) and shows the table meshes generated from that geometry.
// It is also the ONLY place that maps the table-local core frame into the world (FRbCoords + the ClothOrigin
// transform). Owner: UE-1 (the header is a UE-0 contract: additions allowed, no signature changes).
//
// Placement: translation + yaw only (the physics has no tilted table and FRbCoords maps centimetres 1:1); a scaled or tilted
// actor is reset to upright and unscaled at construction / BeginPlay, with a warning.
// Components:  Root (floor, actor location) -> ClothOrigin (bed centre on the cloth, +BedHeight, no rotation /
// scale relative to the actor) -> one mesh component per ERbTablePart: a UStaticMeshComponent with the baked asset
// <RbAssetPaths::TableMeshDir>/<Preset>/SM_Table_<Part> when it exists and bUseBakedMeshes, otherwise a
// UDynamicMeshComponent built at runtime by RbTableMeshBuilder (same data; baked = Nanite / Lumen cards / HWRT).
// Collision: complex-as-simple on every part (the render triangles; pawn walking, traces - UE-4's cue sweep ignores the table,
// whose rails it handles analytically); baked parts get the body cooked at bake time, runtime parts when the mesh is set.
// Balls never use Chaos (plan pitfall 23).
// The part components are TRANSIENT (RF_Transient, tagged PartComponentTag): OnConstruction builds them in the editor,
// BeginPlay rebuilds them in PIE / -game, and a saved level never stores (stale) meshes - a changed TableSpec shows up
// on the next load.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Core/RbTypes.h"
#include "Simulation/RbTableContext.h"

#include "rb/Math/Quat.h"
#include "rb/Math/Vec3.h"

#include "RbTable.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;

UCLASS()
class RAWBREAK_API ARbTable : public AActor
{
	GENERATED_BODY()

public:
	ARbTable();

	// --- configuration ------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	ERbTablePreset Preset = ERbTablePreset::NineFootPro;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	ERbBallSetPreset BallSet = ERbBallSetPreset::StandardPool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	int64 BallSetSeed = 0;

	// Lamp underside above the cloth [m] (off-table apex check); <= 0 = no lamp.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	double LampUndersideHeight = 1.0;

	// Use /Game/Generated/Tables/<Preset>/SM_Table_<Part> when present (else runtime dynamic meshes).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	bool bUseBakedMeshes = true;

	// Material per ERbTablePart (index = part). Defaults: the generated materials of RbAssetPaths.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	TArray<TSoftObjectPtr<UMaterialInterface>> PartMaterials;

	// --- M2 additions (Docs/ue-architecture.md 18.6 multi-table, venue-dive-bar hand-off H-2; architect) ---------------

	// Index of this table in its level (0..N-1, unique per level). Multi-table rooms key directors, loose balls, audio and
	// replays by it, and the venue seed hashes it (MakeVenueTableCondition / VenueBallSetSeed).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	int32 TableIndex = 0;

	// Venue table (human-factors 4.5.5, HF-41, HF-43): the table condition (seeded slope, ball cling) comes from
	// rb::human::MakeVenueTableCondition(VenueSeed, TableIndex, VenueKind, bFirstCareerTable, false) and the ball set seed from
	// rb::human::VenueBallSetSeed(VenueSeed, TableIndex) (BallSetSeed is then ignored). Off: level table, clean balls (M1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table|Venue")
	bool bUseVenueCondition = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table|Venue")
	ERbVenueKind VenueKind = ERbVenueKind::DiveBar;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table|Venue")
	int64 VenueSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table|Venue")
	bool bFirstCareerTable = false;

	// Plan area the lamp covers in the CORE table frame [m] (rb::EnvironmentSpec::LampFootprint, off-table apex check). Off =
	// the whole table is under the lamp (the M1 room). The Low Bridge lamp: x [-0.650, 0.650], y [-0.210, 0.170] (VDB E14).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	bool bUseLampFootprint = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	FVector2D LampFootprintMin = FVector2D(-0.650, -0.210);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RawBreak|Table")
	FVector2D LampFootprintMax = FVector2D(0.650, 0.170);

	// The FRbTableSetup these properties give (what RebuildTable builds the context from; tests).
	FRbTableSetup MakeTableSetup() const;

	// --- the core table -------------------------------------------------------------------------------

	// Valid after construction (OnConstruction / BeginPlay); rebuilt when the preset changes.
	TSharedPtr<const FRbTableContext> GetContextPtr() const { return Context; }
	const FRbTableContext& GetContext() const;
	bool HasContext() const { return Context.IsValid(); }

	// Rebuilds the context and the meshes from the current properties.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Table")
	void RebuildTable();

	// --- frames (table-local core <-> world) ----------------------------------------------------------

	// Table-local UE frame (FRbCoords) -> world: the ClothOrigin component transform (scale 1).
	FTransform GetTableToWorld() const;

	FVector CoreToWorld(const rb::Vec3& P) const;
	rb::Vec3 WorldToCore(const FVector& World) const;
	FVector CoreDirectionToWorld(const rb::Vec3& D) const;
	rb::Vec3 WorldDirectionToCore(const FVector& World) const;
	FQuat CoreOrientationToWorld(const rb::Quat& Q) const;
	rb::Quat WorldOrientationToCore(const FQuat& Q) const;

	// Core azimuth phi [rad] of a world-space direction (plan projection in the table frame).
	double WorldDirectionToAzimuth(const FVector& WorldDirection) const;

	UFUNCTION(BlueprintCallable, Category = "RawBreak|Table")
	FVector GetBedCenterWorld() const;

	USceneComponent* GetClothOrigin() const { return ClothOrigin; }

	// Mesh component of a part (static or dynamic), nullptr before the first build.
	UPrimitiveComponent* GetPartComponent(ERbTablePart Part) const;

	// True if the part shows the baked static mesh asset (else the runtime dynamic mesh, or nothing).
	bool IsPartBaked(ERbTablePart Part) const;

	// Tag of every part component (they are transient: created at construction / BeginPlay, never saved).
	static const FName PartComponentTag;

	// AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

protected:
	// Creates / replaces the part components (baked static meshes or runtime dynamic meshes).
	void RebuildMeshes();

	// Destroys every part component (also stale copies, found by PartComponentTag).
	void DestroyPartComponents();

	// Everything the context and the meshes depend on (skips redundant rebuilds when the editor re-runs construction).
	FString MakeBuildKey() const;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Table")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Table")
	TObjectPtr<USceneComponent> ClothOrigin;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> PartComponents; // index = ERbTablePart

private:
	TSharedPtr<const FRbTableContext> Context;

	// Build key of the current context + meshes (empty = nothing built).
	FString BuiltKey;
};
