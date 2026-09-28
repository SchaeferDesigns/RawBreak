#pragma once

// The rendered cue (Docs/ue-architecture.md 5.6). Its pose is always given in CORE terms (tip dome centre and
// butt -> tip direction in the table frame) so the stroke input, the human layer's SampleHand pose (what you see is
// what hits, HF 3.7) and the simulated tip path (rb::CueTipAt, what the rules judged) all drive it the same way.
// NO body/hands in M1: the cue floats in the right place. Owner: UE-4.
//
// Mesh: RbAssetPaths::CueMeshDir/SM_Cue_<Preset> when baked, else a UDynamicMeshComponent from RbCueMeshBuilder.
// Collision: none on the mesh; the cue-vs-world test is FRbCueClearance (analytic vs balls and rails) plus a
// capsule sweep against the environment.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "rb/Equipment/Cue.h"
#include "rb/Human/CueState.h"
#include "rb/Math/Vec3.h"

#include "RbCue.generated.h"

class ARbTable;
class UDynamicMeshComponent;
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

protected:
	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	TObjectPtr<UDynamicMeshComponent> RuntimeMesh;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	TObjectPtr<UStaticMeshComponent> BakedMesh;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Cue")
	ERbCueDrive Drive = ERbCueDrive::Hidden;

	TWeakObjectPtr<ARbTable> Table;
	rb::CueSpec Spec = rb::kCuePlaying19oz;
	rb::human::CueBodyState Body;
};
