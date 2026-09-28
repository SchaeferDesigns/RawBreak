#pragma once

// Paths of every GENERATED asset (Docs/ue-architecture.md 9.3). The editor Python scripts create them under
// these exact names; C++ loads them with soft references and falls back to runtime geometry / engine
// materials when they do not exist yet (so every package can run before the generators are finished).
// Owner: UE-0 (frozen contract: additions allowed, no renames without updating the generator scripts).

#include "CoreMinimal.h"

namespace RbAssetPaths
{
	// --- levels --------------------------------------------------------------------------------------
	inline const TCHAR* const M1TestRoomMap = TEXT("/Game/Generated/Maps/L_M1_TestRoom");            // UE-8
	inline const TCHAR* const PipelineProofMap = TEXT("/Game/Dev/PipelineProof/L_PipelineProof");     // UE-0

	// --- baked meshes (RawBreakEditor URbAssetBakeLibrary) ---------------------------------------------
	// Table parts: <TableMeshDir>/<PresetName>/SM_Table_<Part>, e.g. /Game/Generated/Tables/NineFootPro/SM_Table_Bed.
	inline const TCHAR* const TableMeshDir = TEXT("/Game/Generated/Tables");                         // UE-1
	inline const TCHAR* const BallMesh = TEXT("/Game/Generated/Balls/SM_RbBall");                     // UE-2 (unit radius 1 cm, scaled per ball)
	inline const TCHAR* const CueMeshDir = TEXT("/Game/Generated/Cues");                             // UE-4: <dir>/SM_Cue_<CuePreset>

	// --- materials (Tools/unreal/editor/rb_make_materials.py, UE-3) ------------------------------------
	inline const TCHAR* const MatBall = TEXT("/Game/Generated/Materials/M_RbBall");
	inline const TCHAR* const MatCloth = TEXT("/Game/Generated/Materials/M_RbCloth");
	inline const TCHAR* const MatRailWood = TEXT("/Game/Generated/Materials/M_RbRailWood");
	inline const TCHAR* const MatCushionRubber = TEXT("/Game/Generated/Materials/M_RbCushionRubber");
	inline const TCHAR* const MatPocketLiner = TEXT("/Game/Generated/Materials/M_RbPocketLiner");
	inline const TCHAR* const MatSight = TEXT("/Game/Generated/Materials/M_RbSight");
	inline const TCHAR* const MatCue = TEXT("/Game/Generated/Materials/M_RbCue");
	inline const TCHAR* const MatRoomWall = TEXT("/Game/Generated/Materials/M_RbRoomWall");
	inline const TCHAR* const MatRoomFloor = TEXT("/Game/Generated/Materials/M_RbRoomFloor");
	inline const TCHAR* const MatLampDiffuser = TEXT("/Game/Generated/Materials/M_RbLampDiffuser");

	// Material parameter collection with the ball centres for the analytic cloth occlusion (plan 6.5).
	inline const TCHAR* const BallMpc = TEXT("/Game/Generated/Materials/MPC_RbBalls");

	// --- material parameter names (C++ writes them, the generated materials read them) -----------------
	namespace Param
	{
		inline const FName BallNumber(TEXT("BallNumber"));          // scalar 0..15 (M_RbBall)
		inline const FName BallColor(TEXT("BallColor"));            // vector, linear
		inline const FName BallOmegaLocal(TEXT("BallOmegaLocal"));  // vector [rad/s] ball-local (rotation smear, plan 4.6)
		inline const FName ExposureTime(TEXT("ExposureTime"));      // scalar [s] shutter time (rotation smear)
		inline const FName BallRadiusCm(TEXT("BallRadiusCm"));      // scalar (MPC and M_RbBall)
		// MPC_RbBalls: vector parameters Ball00 .. Ball15 = (X, Y, Z world cm, W = 1 on the cloth / 0 hidden).
		inline FName MpcBall(int32 Index) { return FName(*FString::Printf(TEXT("Ball%02d"), Index)); }
	}

	// Look-dev / acceptance capture cameras placed by the level generator (ACameraActor tags, UE-8).
	namespace CaptureCamera
	{
		inline const TCHAR* const Overhead = TEXT("RbCam_Overhead");
		inline const TCHAR* const ChinOnCue = TEXT("RbCam_ChinOnCue");
		inline const TCHAR* const BallCloseUp = TEXT("RbCam_BallCloseUp");
		inline const TCHAR* const RoomOverview = TEXT("RbCam_RoomOverview");
	}
}
