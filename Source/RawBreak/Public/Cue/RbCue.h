#pragma once

// The rendered cue (Docs/ue-architecture.md 5.6). Its pose is always given in CORE terms (tip dome centre and
// butt -> tip direction in the table frame) so the stroke input, the human layer's SampleHand pose (what you see is
// what hits, HF 3.7) and the simulated tip path (rb::CueTipAt, what the rules judged) all drive it the same way.
// NO body/hands in M1: the cue floats in the right place. Owner: UE-4.
//
// Mesh: RbAssetPaths::CueMeshDir/SM_Cue_<Preset> when baked, else a UDynamicMeshComponent from RbCueMeshBuilder.
// Collision: none on the mesh; the cue-vs-world test is FRbCueClearance (analytic vs balls and rails) plus a
// capsule sweep against the environment.
//
// Details (UE-4):
//  * Mesh choice: the baked SM_Cue_<Preset> of the preset whose CueSpec geometry (length, tip dome, tip diameter) equals
//    the given spec, when the body is the default CueBodyState (the bake's), bUseBakedMesh and the asset exists; otherwise
//    the same builder output at runtime (RuntimeMesh). Both show one mesh in the local frame of RbCueMeshBuilder.h (origin
//    = tip dome centre, +X toward the tip).
//  * Material: MaterialOverride, else M_RbCue (UE-3), else the engine's vertex-colour material (the mesh carries a default
//    albedo per section in its vertex colours), so the cue reads correctly before UE-3's material exists.
//  * Pose: exact (the quaternion goes into the root's rotation cache, no FRotator round trip and no engine dead band of
//    1e-4 cm / deg), ETeleportType::None (motion vectors for TSR / motion blur). Becoming visible (Hidden -> Input /
//    Playback) drops the motion history, so a cue shown at a new place never streaks from where it was hidden; ResetMotion
//    does the same for other discontinuous re-placements (replay start).
//  * Visibility per drive: Hidden = not rendered (walking); Input and Playback = rendered, casts shadows, visible in
//    ray-traced reflections (the balls mirror the cue).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Core/RbTypes.h"
#include "Cue/RbCueMeshBuilder.h"

#include "rb/Equipment/Cue.h"
#include "rb/Human/CueState.h"
#include "rb/Math/Vec3.h"

#include "RbCue.generated.h"

class ARbTable;
class UDynamicMeshComponent;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ERbCueDrive : uint8
{
	Hidden,    // not shown (walking)
	Input,     // pose from the stroke component (aiming, practice strokes, final stroke)
	Playback,  // pose from the shot's tip path (rb::CueTipAt) after contact
};

UCLASS()
class RAWBREAK_API ARbCue : public AActor
{
	GENERATED_BODY()

public:
	ARbCue();

	// Attaches to the table's ClothOrigin (table-local transforms) and builds / loads the mesh for Spec.
	void InitForTable(ARbTable* InTable, const rb::CueSpec& InSpec, const rb::human::CueBodyState& InBody);

	// Pose in core terms: tip dome centre [m] and unit butt -> tip direction, table frame.
	void SetPoseCore(const rb::Vec3& TipDomeCenter, const rb::Vec3& Direction);

	void SetDrive(ERbCueDrive InDrive);
	ERbCueDrive GetDrive() const { return Drive; }

	const rb::CueSpec& GetCueSpec() const { return Spec; }
	const rb::human::CueBodyState& GetCueBody() const { return Body; }

	// --- additions (UE-4) ----------------------------------------------------------------------------

	// Replaces M_RbCue (dev maps, look-dev). Applied by InitForTable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Cue")
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	// Use the baked SM_Cue_<Preset> when it matches (else the runtime mesh).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Cue")
	bool bUseBakedMesh = true;

	// Last pose given to SetPoseCore (core frame).
	const rb::Vec3& GetTipDomeCenterCore() const { return PoseTip; }
	const rb::Vec3& GetDirectionCore() const { return PoseDirection; }

	// The mesh component in use (the baked static mesh or the runtime dynamic mesh), nullptr before InitForTable.
	UPrimitiveComponent* GetMeshComponent() const;
	bool IsUsingBakedMesh() const { return bUsingBakedMesh; }
	UMaterialInterface* GetAppliedMaterial() const { return AppliedMaterial; }

	// Drops the motion history of the cue (a discontinuous re-placement renders without velocity this frame).
	void ResetMotion();

	// Mesh-layout options of the runtime mesh (the bake uses the defaults: set bUseBakedMesh = false to show other options).
	// Applied by InitForTable.
	FRbCueMeshOptions MeshOptions;

	// The preset whose CueSpec has the geometry of Spec (length, tip dome radius, tip diameter); false if none.
	static bool FindPresetForSpec(const rb::CueSpec& InSpec, ERbCuePreset& OutPreset);

	// True if the body has the default taper / section lengths (the baked meshes are built with CueBodyState{}).
	static bool IsDefaultBody(const rb::human::CueBodyState& InBody);

	// Long package name of the baked mesh: <RbAssetPaths::CueMeshDir>/SM_Cue_<Preset>.
	static FString BakedMeshPackage(ERbCuePreset Preset);

	// The baked mesh of Preset, nullptr if it was not baked.
	static UStaticMesh* LoadBakedMesh(ERbCuePreset Preset);

protected:
	void RebuildMesh();
	void ApplyMaterial();

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	TObjectPtr<UDynamicMeshComponent> RuntimeMesh;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	TObjectPtr<UStaticMeshComponent> BakedMesh;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	ERbCueDrive Drive = ERbCueDrive::Hidden;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> AppliedMaterial;

	TWeakObjectPtr<ARbTable> Table;
	rb::CueSpec Spec = rb::kCuePlaying19oz;
	rb::human::CueBodyState Body;
	rb::Vec3 PoseTip;
	rb::Vec3 PoseDirection{1.0, 0.0, 0.0};
	bool bUsingBakedMesh = false;
	bool bMeshBuilt = false;
};
