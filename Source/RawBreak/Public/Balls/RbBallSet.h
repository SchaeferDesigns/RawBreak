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
//
// Details (UE-2):
//  * Radius: every ball uses ITS OWN radius from FRbTableContext::Balls (the oversized bar cue ball is exact); the
//    component scale is radius [cm] / mesh radius (1 for SM_RbBall, 50 for the engine sphere of the last fallback).
//  * Mesh: SM_RbBall when baked (rb_bake_ball.py), else the engine sphere (a runtime-built copy of the ball mesh would need
//    the MeshDescription modules in RawBreak.Build.cs, owned by UE-0).
//  * Material: BallMaterialOverride, else M_RbBall, else the engine's BasicShapeMaterial ("Color" = ball colour), so
//    every package can run before UE-3's materials exist. Parameters: BallNumber, BallColor (DefaultBallColor, WPA
//    colours, linear), BallRadiusCm, ExposureTime, BallOmegaLocal (updated by SetBallSpinCore).
//  * MPC_RbBalls: Ball00..Ball15 = (world X, Y, Z [cm], W) with W = 1 while the ball is visible with its centre above
//    the bed plane, 0 otherwise; BallRadiusCm = the object-ball radius. Written on every change; the engine defers the
//    uniform-buffer update to once per frame. (Per-ball radii are implicit: centre height above the cloth = R.)
//  * Poses are exact: the quaternion goes into the component's rotation cache (no FRotator round trip, pitfall 3; the
//    engine's SetRelativeLocationAndRotation would snap near pitch +-90 deg and drop changes below 1e-4 cm / deg), then an
//    ordinary UpdateComponentToWorld with ETeleportType::None.
//  * ShowSimBalls re-creates the render proxy of every ball that JUMPS (a new rack, a replay start, restoring the live
//    table; ResetBallMotion), so a discontinuous re-placement never smears into motion blur / TSR history; continuous
//    updates (SetBallCore every frame, cue-ball placement) keep their motion vectors. The playback does the same for
//    seeks, replays started from another table state and Stop(true).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "rb/Core/Constants.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/Simulator.h"

#include "RbBallSet.generated.h"

class ARbTable;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class URbShotPlaybackComponent;
class UStaticMesh;
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

	// Replaces M_RbBall (dev maps, look-dev). Applied by InitForTable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RawBreak|Balls")
	TSoftObjectPtr<UMaterialInterface> BallMaterialOverride;

	// --- additions (UE-2) ----------------------------------------------------------------------------

	// Linear WPA ball colour of a ball number (0 = cue ball, 1 yellow, 2 blue, 3 red, 4 purple, 5 orange, 6 green,
	// 7 maroon, 8 black, 9..15 = the colour of number - 8 on a white ball). ESTIMATE values (plan 6.2); UE-3's
	// ball_lineup is the check.
	static FLinearColor DefaultBallColor(int32 BallNumber);

	// Pushes ExposureTime to every ball material (the camera model calls this when the shutter changes).
	void SetExposureTime(float Seconds);

	// Drops the motion history of one ball: its render proxy is re-created at the end of the frame, so the next frame has
	// no previous transform and no velocity. For DISCONTINUOUS re-placements only (a new rack, a seek, a replay start,
	// a snap to the finals) - a jump must never smear into motion blur / TSR history. Continuous updates keep it.
	void ResetBallMotion(int32 BallId);

	// Radius of ball BallId [cm] as built (0 for an unknown id).
	double GetBallRadiusCm(int32 BallId) const;

	// Last pose given to SetBallCore / ShowSimBalls (table-local UE orientation), identity before the first.
	FQuat GetBallOrientationUE(int32 BallId) const;

	UMaterialInstanceDynamic* GetBallMaterial(int32 BallId) const;
	UStaticMesh* GetBallMesh() const { return BallMesh; }

	// Occlusion collection (MPC_RbBalls by default, loaded by InitForTable); tests and look-dev may substitute one.
	void SetOcclusionCollection(UMaterialParameterCollection* Collection);
	UMaterialParameterCollection* GetOcclusionCollection() const { return OcclusionCollection; }

	// Pushes every ball centre into the occlusion collection (also done automatically on every change).
	void UpdateOcclusionParameters();

protected:
	// One ball's MPC entry.
	void UpdateOcclusionParameter(int32 BallId);

	void DestroyBalls();
	UStaticMesh* ResolveBallMesh();
	UMaterialInterface* ResolveBallMaterial(bool& bOutEngineFallback) const;

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

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BallMesh;

	TWeakObjectPtr<ARbTable> Table;
	TArray<double> BallRadiiCm;          // index = ball id
	TArray<FQuat> BallOrientations;      // index = ball id, table-local UE
	bool bOcclusionCollectionOverridden = false;
};
