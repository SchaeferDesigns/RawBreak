#pragma once

// Paths of every GENERATED asset (Docs/ue-architecture.md 9.3). The editor Python scripts create them under
// these exact names; C++ loads them with soft references and falls back to runtime geometry / engine
// materials when they do not exist yet (so every package can run before the generators are finished).
// Owner: UE-0 (frozen contract: additions allowed, no renames without updating the generator scripts).

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/EngineTypes.h"

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

		// --- M2 (Docs/ue-architecture.md 18; venue-dive-bar 12.2 / 12.3, ui-ux 6.3): ARbLookDevCamera tags placed by
		// rb_make_divebar.py (M2-A). "Is it real" views V01..V12, trailer hooks TH1..TH7, menu stations S0..S7.
		inline FString DiveBarView(int32 Index) { return FString::Printf(TEXT("RbCam_DB_V%02d"), Index); }  // 1..12
		inline FString DiveBarTrailer(int32 Index) { return FString::Printf(TEXT("RbCam_DB_TH%d"), Index); } // 1..7
		inline FString MenuStation(int32 Index) { return FString::Printf(TEXT("RbCam_Menu_S%d"), Index); }  // 0..7
	}

	// --- M2 additions (Docs/ue-architecture.md 18; owners in brackets) --------------------------------------------

	// Levels.
	inline const TCHAR* const DiveBarMap = TEXT("/Game/Generated/Maps/L_DiveBar");                    // M2-A (+ sublevels L_DiveBar_*)
	inline const TCHAR* const TitleMap = TEXT("/Game/Generated/Maps/L_Title");                        // M2-D (title / venue select)

	// Venue content: <VenueDir>/DiveBar/{Arch,Props,Materials,Textures,Decals,Lighting,FX} (venue-dive-bar 13.3).
	inline const TCHAR* const VenueDir = TEXT("/Game/Generated/Venues");                              // M2-A / M2-B
	// Venue master materials + the venue MPC (Age, grime, dust, nicotine, EmissiveScale; venue-dive-bar 6.2).
	inline const TCHAR* const DiveBarMaterialDir = TEXT("/Game/Generated/Venues/DiveBar/Materials");  // M2-B
	inline const TCHAR* const VenueMpc = TEXT("/Game/Generated/Venues/DiveBar/Materials/MPC_DB_Venue"); // M2-B

	// Table-family material instances for the dive bar's coin-op table (venue-dive-bar 6.4; generated by rb_make_materials.py).
	inline const TCHAR* const MatBallDiveBar = TEXT("/Game/Generated/Materials/MI_RbBall_DiveBar");   // M2-L
	inline const TCHAR* const MatClothBarGreen = TEXT("/Game/Generated/Materials/MI_RbCloth_BarGreen"); // M2-L
	inline const TCHAR* const MatRailBlackLaminate = TEXT("/Game/Generated/Materials/MI_RbRail_BlackLaminate"); // M2-L

	// Physical materials of the off-table ball hand-off (M2-E generates them with rb_make_physics.py; the venue generators assign
	// them to floors / mats / ledges by these paths; venue-dive-bar 13.5 values: VCT 0.5 / 0.35, rubber 0.8 / 0.15 = friction /
	// restitution, ESTIMATE).
	inline const TCHAR* const PhysMatDir = TEXT("/Game/Generated/Physics");                            // M2-E
	inline const TCHAR* const PhysMatBall = TEXT("/Game/Generated/Physics/PM_RbBall");                  // phenolic ball
	inline const TCHAR* const PhysMatVct = TEXT("/Game/Generated/Physics/PM_RbSurface_Vct");           // vinyl tile on concrete
	inline const TCHAR* const PhysMatRubber = TEXT("/Game/Generated/Physics/PM_RbSurface_Rubber");     // rubber mats
	inline const TCHAR* const PhysMatWood = TEXT("/Game/Generated/Physics/PM_RbSurface_Wood");         // ledges, bar die, booth plinths
	inline const TCHAR* const PhysMatConcrete = TEXT("/Game/Generated/Physics/PM_RbSurface_Concrete"); // test room floor, block walls

	// First-person player assets (M2-F): the carrying hand of the diegetic ball in hand (a stand-in until the M3 arms/hands).
	inline const TCHAR* const PlayerDir = TEXT("/Game/Generated/Player");                             // M2-F
	inline const TCHAR* const HandCarryMesh = TEXT("/Game/Generated/Player/SM_RbHand_Carry");         // M2-F

	// Audio (M2-C): generated sound waves, submixes, attenuation / concurrency settings (audio.md 8.5, generated subset).
	inline const TCHAR* const AudioDir = TEXT("/Game/Generated/Audio");                               // M2-C

	// Actor / component tags shared between packages.
	namespace Tag
	{
		// The table the local player plays at when a level has several (else TableIndex 0; M2-E).
		inline const FName PlayerTable(TEXT("RbPlayerTable"));
		// Audio anchors on venue actors (venue-dive-bar 10 -> audio.md 12.1): the audio subsystem (M2-C) attaches its emitters
		// to actors with these tags; the level generator (M2-A) sets them. Format "RbAudio_<Anchor>".
		inline FName AudioAnchor(const TCHAR* Anchor) { return FName(*FString::Printf(TEXT("RbAudio_%s"), Anchor)); }
		// Ceiling actors hidden for the V10 plan capture (venue-dive-bar 12.2).
		inline const FName DiveBarCeiling(TEXT("RbDB_Ceiling"));
		// A ball under engine physics after it left its table (ARbLooseBall, M2-E).
		inline const FName LooseBall(TEXT("RbLooseBall"));
		// Overlap volumes (profile RbBallReturn) where a resting loose ball is handed back automatically (behind the bar counter,
		// venue-dive-bar 13.5; placed by the level generator, M2-A).
		inline const FName BallReturnVolume(TEXT("RbBallReturn"));
		// The single ARbVenueInfo of a venue level (M2-A).
		inline const FName VenueInfo(TEXT("RbVenueInfo"));
	}

	// Collision profiles / channels of Config/DefaultEngine.ini [/Script/Engine.CollisionProfile] (venue-dive-bar 13.5).
	namespace Collision
	{
		inline const FName VenueBlockProfile(TEXT("RbVenueBlock")); // walls, columns, furniture: blocks pawn, cue sweep, loose balls
		inline const FName VenuePropProfile(TEXT("RbVenueProp"));   // clutter: query only, no pawn block, loose balls pass
		inline const FName LooseBallProfile(TEXT("RbLooseBall"));   // a ball off the table under engine physics (M2-E)
		inline const FName BallReturnProfile(TEXT("RbBallReturn")); // overlap volume: a loose ball that ends here is handed back
		// ECC_GameTraceChannel1 = trace channel "RbCueSweep" (the cue's environment sweep), ECC_GameTraceChannel2 = object channel
		// "RbLooseBall". C++ uses these constants, never the raw channel numbers.
		inline constexpr ECollisionChannel CueSweepChannel = ECC_GameTraceChannel1;
		inline constexpr ECollisionChannel LooseBallChannel = ECC_GameTraceChannel2;
	}

	// Surface types of Config/DefaultEngine.ini [/Script/Engine.PhysicsSettings] PhysicalSurfaces (M2-E physical materials; the
	// loose-ball impacts carry them to the audio).
	namespace Surface
	{
		inline constexpr EPhysicalSurface Ball = SurfaceType1;
		inline constexpr EPhysicalSurface Vct = SurfaceType2;
		inline constexpr EPhysicalSurface Rubber = SurfaceType3;
		inline constexpr EPhysicalSurface Wood = SurfaceType4;
		inline constexpr EPhysicalSurface Concrete = SurfaceType5;
		inline constexpr EPhysicalSurface Cloth = SurfaceType6;
	}
}
