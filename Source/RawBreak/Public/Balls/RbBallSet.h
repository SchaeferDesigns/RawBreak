#pragma once

// The rendered balls of one table (Docs/ue-architecture.md 5.4). Pure presentation: the AUTHORITATIVE ball
// state (positions, orientations, in play) lives in the match director's FRbTableState in core units; this actor
// shows it, and the playback component animates it from a ShotResult. Attached to the table's ClothOrigin, so
// every transform here is TABLE-LOCAL UE (FRbCoords). Owner: UE-2.
//
// One UStaticMeshComponent per ball id (0 = cue ball .. 15), Movable, no physics / no collision (Chaos never
// touches balls, plan pitfall 23), mesh RbAssetPaths::BallMesh (unit sphere, scale = radius in cm) or a runtime
// fallback, one MID of M_RbBall per ball (BallNumber, BallColor, BallOmegaLocal, ExposureTime). Transform updates
// never teleport (motion vectors for TSR/DLSS and motion blur, plan 4.6, pitfall 4). The ball centres are also
// written to MPC_RbBalls for the analytic cloth occlusion (plan 6.5).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "rb/Core/Constants.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/Simulator.h"

#include "RbBallSet.generated.h"

class ARbTable;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class URbShotPlaybackComponent;
class UStaticMeshComponent;

UCLASS()
class RAWBREAK_API ARbBallSet : public AActor
{
	GENERATED_BODY()

public:
	ARbBallSet();

	// Attaches to the table's ClothOrigin, creates the ball components for the table's ball set (radii from
	// FRbTableContext::Balls) and hides them all. Call again after the table was rebuilt.
	void InitForTable(ARbTable* InTable);

	ARbTable* GetTable() const { return Table.Get(); }
	int32 GetBallCount() const { return BallComponents.Num(); }

	// Table-local placement of one ball (core units), no teleport. Orientation is the core quaternion.
	void SetBallCore(int32 BallId, const rb::Vec3& Position, const rb::Quat& Orientation);

	// Angular velocity for the rotation-smear material parameter (core rad/s, table frame).
	void SetBallSpinCore(int32 BallId, const rb::Vec3& Omega);

	void SetBallVisible(int32 BallId, bool bVisible);
	bool IsBallVisible(int32 BallId) const;

	// Shows every ball of a simulator input as it would start (in play -> visible at State.Position / Orientation).
	void ShowSimBalls(const rb::SimBall* Balls, int32 Count);

	URbShotPlaybackComponent* GetPlayback() const { return Playback; }
	UStaticMeshComponent* GetBallComponent(int32 BallId) const;

	// Shutter time [s] used for the rotation smear (from the camera model; 1/120 s default).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Balls")
	float ExposureTime = 1.0f / 120.0f;

protected:
	// Pushes ball centres into MPC_RbBalls (world cm, W = 1 visible on the cloth).
	void UpdateOcclusionParameters();

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Balls")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Balls")
	TObjectPtr<URbShotPlaybackComponent> Playback;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BallComponents; // index = ball id

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BallMaterials; // index = ball id

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> OcclusionCollection;

	TWeakObjectPtr<ARbTable> Table;
};
