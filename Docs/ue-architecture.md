# RAW BREAK — Unreal Architecture (road to the first playable, M1)

| | |
|---|---|
| Document | `Docs/ue-architecture.md` |
| Status | v1.1 (2026-09-27), UE-0 delivered and adversarially reviewed (section 16): module layout, compiling skeleton of every class (TODO(UE-x) markers), config, headless pipeline proven on the dev PC (build, Python level + bake, PIE tests, headless screenshot). Frozen for the parallel work packages UE-1..UE-8. v1.2 (2026-09-28): M1 integrated (section 17). **v1.3 (2026-09-29): M2 plan (section 18): 7 packages M2-F / M2-L / M2-A / M2-B / M2-C / M2-D / M2-E with disjoint owned files, the new contracts and compiling stubs (TODO(M2-x) markers), config and pipeline additions.** v1.4 (2026-09-29): M2-0 (architect package, 18.12): multi-view / UI / hide-tag / EV100 captures, frame-time recorder, `rbue.py perf / package / ledger / core / owners / selftest`, contract and level-smoke tests, integration runbook. |
| Scope | The Unreal Engine 5.8.3 side of RAW BREAK up to milestone **M1 "first playable"** (sections 0-17) and the **M2 plan** (section 18: feel fixes from the M1 playtest, table / cloth look-dev, dive-bar slice v1, audio v1, menus / settings, balls off the table, multi-table groundwork). The engine-agnostic core `BilliardsCore` (Docs/architecture.md) is complete and stays untouched. |
| Sources of truth | `Docs/specs/ue5-realism-plan.md` (**UE** below: camera 4.x, body/input/cue 5.x, adapter 5.6, playback 5.7, rendering 6.x, settings 9.4, tests 13), `Docs/specs/human-factors.md` (**HF**, layers UG/UA/US), `Docs/specs/rules.md` (**RUL**, what the match UI must show), `Docs/decisions.md`, `Docs/architecture.md` sections 7 and 13 (the core API and the integration contract). |
| Code | `Source/RawBreak/**` (game module), `Source/RawBreakEditor/**` (editor tools), `Source/RawBreakShaders/**` + `Shaders/**` (shader directory), `Tools/unreal/**` (headless pipeline), `Config/*.ini`, `RawBreak.uproject` |

---

## 0. Where this sits in the plan

| Round | Content | State |
|---|---|---|
| 1-2 | `BilliardsCore`: physics, geometry, rules, player model, playback, rbsim (WP-0..WP-9, WP-11), 896 tests | done, merged |
| (parallel) | WP-10 validation & benchmarks of the core (pooltool cross-checks, performance) | pending, independent of Unreal |
| **3 = this document (UE-0)** | Unreal architecture, compiling skeleton, headless pipeline proof | **done** |
| 4 = UE wave A + B | implementation of UE-1..UE-8 (UE-6 split into 6a/6b; section 13) in worktrees + reviewers | next |
| 5 = UE integration | merge, cross-package fixes, M1 acceptance (section 12), **user playtest** | then |

There is no extra round between the core and the playable version: round 3 *is* the start of the Unreal phase. M1 is
reached when the work packages of section 13 are merged and the acceptance checks of section 12 pass.

---

## 1. Goals and hard constraints

| Constraint | How the architecture meets it |
|---|---|
| Claude drives Unreal headless; no human opens the editor | Everything is created by code or scripts: C++ via UBT, assets/levels via editor Python in a commandlet, procedural geometry in C++ (`FDynamicMesh3`) baked to assets by C++ functions called from Python, Enhanced Input created at runtime, UI in code (Slate). Verification by headless automation tests and headless screenshots (section 9). |
| Core stays engine-agnostic and untouched | The UE side only includes `rb/...` headers; the mirror to UE axes lives in `FRbCoords` (section 4). `BilliardsCore.Build.cs` unchanged. |
| UObjects only on the game thread | The simulation worker sees plain data (`FRbShotRequest`, `TSharedPtr<const FRbTableContext>`) and returns an immutable `FRbShot` (section 7). |
| Determinism | The simulation runs inside the BilliardsCore module (FP precise, unity off); replays play the stored `ShotResult` bitwise; ROB-10 compares the UE module's input and result hashes with the standalone build (UE-6a, A5). |
| Single source of truth for geometry | Table/pocket/cushion/rail/sight meshes are generated from `rb::TableGeometry` (UE 6.7, pitfall 16). Nothing is modelled by eye. |
| Quality first (decisions 2026-09-27) | Baked static meshes (Nanite, distance fields, Lumen cards), Substrate Adaptive GBuffer, HWRT Lumen; presets Low..Cinematic, Epic/Cinematic never capped to the dev PC (section 8.1). |
| Render time decoupled from physics time | Playback evaluates the exact analytic state at any clock value (slow motion, scrubbing, replays, trailer) — section 5.5. |
| UBT path limit 260 chars | Build roots stay short (`C:\Users\Colin\Desktop\.00000\RawBreak`); worktrees under `.claude/worktrees/<short-name>`. |

---

## 2. Module layout

```
RawBreak.uproject            modules + plugins (PythonScriptPlugin, EditorScriptingUtilities, EnhancedInput; AndroidFileServer OFF)
Config/DefaultEngine.ini     HWRT Lumen, VSM, Nanite, Substrate (Adaptive), TSR, PSO precaching, near clip 1 cm,
                             MaintainYFOV, GameUserSettingsClassName, default maps          (UE-0)
Config/DefaultInput.ini      Enhanced Input classes, mouse capture                          (UE-0)
Config/DefaultScalability.ini  quality preset rows (plan 9.4)                                (UE-8, new)
Shaders/Private/*.ush        analytic ball surface, ball occlusion (included by material Custom nodes)   (UE-3)
Source/
  BilliardsCore/             engine-agnostic core (unchanged)
  RawBreakShaders/           Runtime, LoadingPhase PostConfigInit: maps /RawBreak -> <Project>/Shaders   (UE-0)
  RawBreak/                  Runtime game module (everything below)
    Public/  Core/ Math/ Simulation/ Table/ Balls/ Cue/ Input/ Player/ Camera/ Game/ UI/ Replay/ Settings/ Dev/
    Private/ same areas + Tests/ (unit tests, RawBreak.Unit.*)
  RawBreakEditor/            Editor module: asset bake library (called from Python), PIE functional tests
                             (RawBreak.Functional.*, need UnrealEd)
Tools/unreal/                headless pipeline: rbue.py (host runner) + editor/*.py (run inside Unreal)
Content/Generated/**         generated assets (materials, baked meshes, M1 level) - committed via LFS, never hand-edited
Content/Dev/**               scratch content of pipeline checks and per-WP dev maps - git-ignored
```

### 2.1 Build.cs dependencies (and why)

| Module | Dependency | Why |
|---|---|---|
| RawBreak | Core, CoreUObject, Engine, InputCore | basics |
| | EnhancedInput | input actions + mapping context created at runtime (no input assets) |
| | BilliardsCore | the core |
| | **GeometryCore, GeometryFramework** | procedural meshes as `UE::Geometry::FDynamicMesh3` shown by `UDynamicMeshComponent`. Chosen over ProceduralMeshComponent: engine modules (no plugin), normals/tangents/UV/material-id overlays and polygroups, complex collision for cue sweeps, and the **same mesh converts to MeshDescription -> UStaticMesh** in the editor bake (Nanite, distance fields, Lumen cards, HWRT). PMC is a legacy runtime-only path without that conversion. |
| | CinematicCamera | `UCineCameraComponent` (sensor, focal length, aperture = pupil, UE 4.5) |
| | Slate, SlateCore | code-only info overlay (no widget blueprints); UMG not needed |
| | DeveloperSettings | settings classes |
| | ApplicationCore (private) | `IWindowsMessageHandler` for timestamped raw mouse input (UE 5.4) |
| | ImageCore, RenderCore (private) | PNG writing of the headless capture |
| RawBreakEditor | RawBreak, BilliardsCore, GeometryCore (public); UnrealEd, AssetRegistry, AssetTools, MeshConversion, MeshDescription, StaticMeshDescription, GeometryFramework (private) | `FDynamicMesh3 -> FMeshDescription -> UStaticMesh`, saving packages, PIE automation helpers |
| RawBreakShaders | Core, RenderCore, Projects | `AddShaderSourceDirectoryMapping` at PostConfigInit |

Targets: `RawBreakEditor.Target.cs` builds RawBreak, BilliardsCore, RawBreakShaders, RawBreakEditor; `RawBreak.Target.cs` (game) the first three.

### 2.2 Config decisions made in UE-0

* **Substrate on with the Adaptive GBuffer** (`r.Substrate=True`, `r.Substrate.ProjectGBufferFormat=1`): project-wide, decided before the first material exists (UE 6.0, pitfall 14).
* `NearClipPlane=1.0` (pitfall 2), `AspectRatioAxisConstraint=AspectRatio_MaintainYFOV` (pitfall 7), `r.PSOPrecaching=1`, `r.DefaultFeature.MotionBlur=True`.
* `r.Lumen.HardwareRayTracing.LightingMode=2` (Hit Lighting for Reflections; the 5.8 default is 0 = surface cache): plan 6.2 / 6.6 / 9.4 require hit-lit ball reflections from High up, and High is the M1 default. Low / Medium lower it at runtime (UE-8).
* Baked Nanite meshes keep a **full-detail fallback** (`FallbackTarget = PercentTriangles`, 100 %): in 5.8 ray tracing traces the Nanite fallback (`r.RayTracing.Nanite.Mode=0`, RT proxies off), so a decimated fallback would put a coarser table into ball reflections, Lumen HWRT hits and complex collision (`RbAssetBake_Common.cpp`).
* `GameDefaultMap` / `EditorStartupMap` = `/Game/Generated/Maps/L_M1_TestRoom` (created by UE-8); `GlobalDefaultGameMode` stays `GameModeBase` — the M1 map sets `ARbGameMode` in its World Settings, so other maps (pipeline proof, dev maps) are unaffected.
* `AndroidFileServer` plugin disabled: the editor otherwise appends a section with a random security token to `DefaultEngine.ini` on every headless run.

---

## 3. Class catalogue

Every class exists as a compiling skeleton. "Owner" = the work package that implements it (section 13). Lifetime: *level* = placed/spawned actor of the world; *world* = world subsystem; *GI* = game-instance subsystem.

| Class (header) | Kind / lifetime | Responsibility | Owner |
|---|---|---|---|
| `FRbCoords` (Core/RbCoords.h) | static helpers | the ONLY core <-> UE mirror (section 4); **implemented + tested** | UE-0 |
| `ERbTablePreset`, `ERbBallSetPreset`, `ERbCuePreset`, `ERbDiscipline`, `ERbMatchMode`, `ERbCameraPreset`, `ERbQualityPreset`, `ERbTablePart`, `RbTypes::ToCore` (Core/RbTypes.h) | enums | BP/config mirrors of core enums | UE-0 |
| `RbAssetPaths` (Core/RbAssetPaths.h) | constants | paths of every generated asset + material parameter names + capture-camera tags (contract between C++ and the Python generators) | UE-0 |
| `RbStrokeMath` (Math/RbStrokeMath.h) | pure functions | gain curve, quadratic-fit velocity at the crossing, steering, bridge height (T9-T12) | UE-5a |
| `RbCameraMath` (Math/RbCameraMath.h) | pure functions | FOV/overscan/DoF (T2-T5, T8: UE-5b), F0/EV/grain/AO/tessellation/lux (T1, T6, T7, T19-T22: UE-3); the header is a UE-0 frozen contract, each owner implements its own .cpp | UE-5b / UE-3 |
| `FRbTableContext` (Simulation/RbTableContext.h) | immutable, `TSharedPtr<const>` owned by `ARbTable` | TableSpec, TableGeometry, PhysicsParams (`MakePhysicsParams(Spec, Condition)`), BallSet, RulesTable (`BuildRulesTable`) — built once per table | UE-6a |
| `FRbShotRequest`, `FRbShot`, `FRbStrokeRecord`, `RbShot::ResultHash/InputHash/CopyCompact/InitSimInput` (Simulation/RbShot.h) | plain data, `TSharedRef<const FRbShot>` after simulation | one shot: SimInput, human-layer record (replay header), compact ShotResult, hashes | UE-6a |
| `URbSimulationSubsystem` (Simulation/RbSimulationSubsystem.h) | world, tickable | runs `rb::Simulator::Run` on a UE::Tasks worker, same-frame hand-off, `OnShotSimulated`; `RunShotBlocking` for tests | UE-6a |
| `RbTableMeshBuilder` (Table/RbTableMeshBuilder.h) | pure functions | `rb::TableGeometry` -> one `FDynamicMesh3` per `ERbTablePart` in table-local UE cm | UE-1 |
| `ARbTable` (Table/RbTable.h) | level actor | owns the table context; Root (floor) -> ClothOrigin (bed centre on the cloth) -> part meshes (baked static or runtime dynamic); frame conversions core <-> world | UE-1 |
| `RbBallMeshBuilder` (Balls/RbBallMeshBuilder.h) | pure function | unit sphere, 128 segments | UE-2 |
| `ARbBallSet` (Balls/RbBallSet.h) | level actor, attached to ClothOrigin | one static-mesh component + MID per ball, no-teleport updates, `MPC_RbBalls` for cloth occlusion; pure presentation | UE-2 |
| `URbShotPlaybackComponent` (Balls/RbShotPlaybackComponent.h) | component on `ARbBallSet` | decoupled playback clock, `rb::StateAtCursor`, cue tip path, event firing, `OnFinished` | UE-2 |
| `RbCueMeshBuilder` (Cue/RbCueMeshBuilder.h) | pure function | lathe cue mesh (sections) from CueSpec + CueBodyState taper | UE-4 |
| `ARbCue` (Cue/RbCue.h) | level actor, attached to ClothOrigin | cue pose from core terms (tip dome centre + direction); drive Hidden / Input / Playback | UE-4 |
| `RbCueClearance` (Cue/RbCueClearance.h) | functions | minimum elevation vs balls/rails (T13) + environment capsule sweep -> `StrokeSituation::ElevationFloor` | UE-4 |
| `FRbRawMouseInput` (Input/RbRawMouseInput.h) | shared object owned by the stroke component; owns an input thread | raw mouse reports timestamped ON ARRIVAL by a dedicated raw-input thread (SPSC ring), deltas forwarded to Slate; pump-time reconstruction fallback; inactive headless (section 6.2.1) | UE-5a |
| `URbInputSetup` (Input/RbInputSetup.h) | transient UObject owned by the controller | runtime Enhanced Input actions + mapping context (section 6.5) | UE-5a |
| `URbStrokeComponent`, `FRbStrokeContext` (Player/RbStrokeComponent.h) | component on the pawn | stroke state machine -> `FRbStrokeCommit` (`rb::human::IntendedStroke` at the crossing); cue pose from `rb::human::SampleHand` while down (what you see is what hits), `OnStrokeAborted(bRampShown)`; ball-in-hand placement, scripted strokes | UE-5a |
| `ARbPlayerCharacter` (Player/RbPlayerCharacter.h) | pawn (one for both hot-seat players) | capsule walking, cine camera, rig + stroke components, input binding | UE-5b |
| `ARbPlayerController` (Player/RbPlayerController.h) | controller | creates `URbInputSetup`, mapping context, non-pawn actions, owns `URbOverlayComponent`, `CheatClass = URbCheatManager` | UE-5b |
| `FRbCameraPresetParams`, `URbCameraModel`, `RbCameraModel::Defaults` (Camera/RbCameraModel.h) | data | plan 4.9 parameter table (Eyes default, Headcam, Broadcast) | UE-5b |
| `URbCameraRigComponent` (Camera/RbCameraRigComponent.h) | component on the pawn | eye placement (standing / down on the shot / ball in hand), transitions, vertical FOV, DoF, exposure, grain | UE-5b |
| `FRbTableState`, `FRbShooterState` (Game/RbShooterState.h) | plain data in the director | physics mirror of the balls (core units) between shots, rebuilt from the rules' GameState + Finals (5.2 step 10); per shooter attributes, TipState, ChalkCube, CueBodyState, NoiseHistory, indices | UE-6b |
| `URbMatchDirector` (Game/RbMatchDirector.h) | UObject owned by the game mode | rb::rules match + rb::human stroke execution + shot pipeline (section 7); `MakeStrokeContext`, `OnStrokeAborted`, `SetLivePlaybackRate`, `IsReplayAllowed` | UE-6b |
| `ARbGameMode` (Game/RbGameMode.h) | game mode | find/spawn table, ball set, cue; create director; URL options `?Mode=&Game=&Race=&Lag=&Seed=` | UE-6b |
| `ARbTestRoom` (Game/RbTestRoom.h) | level actor | M1 room + WPA lamp in physical units | UE-8 |
| `SRbInfoOverlay`, `FRbOverlayModel` (UI/SRbInfoOverlay.h) | Slate widget | corner text panel | UE-7 |
| `URbOverlayComponent` (UI/RbOverlayComponent.h) | component on the controller | Hidden / Glance / Pinned + debug block; model from the director | UE-7 |
| `URbReplaySubsystem` (Replay/RbReplaySubsystem.h) | world | last N shots, replay with own clock and views, restore live state | UE-7 |
| `ARbReplayCamera` (Replay/RbReplayCamera.h) | spawned actor | Shooter / Overhead / Rail / Follow views | UE-7 |
| `URbGameUserSettings` (Settings/RbGameUserSettings.h) | engine user settings | quality preset, camera preset, FOV, comfort, stroke options | UE-8 |
| `URbHeadlessCaptureSubsystem` (Dev/RbHeadlessCaptureSubsystem.h) | GI, tickable | `-RBCapture=` headless screenshot (section 9.4); **implemented** | UE-0 |
| `ARbLookDevCamera` (Dev/RbLookDevCamera.h) | placed actor (ACineCameraActor) | capture camera that applies a RAW BREAK camera preset (`URbCameraRigComponent::ApplyPresetToCamera`) so look-dev screenshots use the game's camera model | UE-8 |
| `URbCheatManager` (Dev/RbCheatManager.h) | cheat manager | `RbStrike`, `RbStroke`, `RbPlaceCueBall`, `RbChoose`, `RbRerack`, `RbNewMatch`, `RbReplay`, `RbOverlay`, `RbPlaybackRate` (UE-7 adds it; drives `SetLivePlaybackRate`), `RbDumpState` for headless tests | UE-7 |
| `URbAssetBakeLibrary` (RawBreakEditor) | BP function library | `BakeTableMeshes` (UE-1), `BakeBallMesh` (UE-2), `BakeCueMesh` (UE-4), `BakeSelfTest` + common `WriteStaticMesh` (**implemented**, UE-0) | split by file |

**Header rule.** The public API above is a frozen contract: consumers compile against it in parallel. The owning package may add members/functions and change private parts freely; changing or removing a public signature that another package calls needs that consumer's sign-off (same rule as architecture.md §2 item 11). Every stub is marked `TODO(UE-x)`.

---

## 4. Coordinate adapter (UE 5.6; tests T14-T16 implemented)

Frames, from the physics to the screen:

```
core frame (rb)          right-handed, metres, origin = bed centre on the cloth, +x foot, +y left, +z up
   | FRbCoords           mirror M = diag(1, -1, 1), x100
table-local UE frame     left-handed, cm, X = x, Y = -y, Z = z  (= relative transforms under ARbTable::ClothOrigin)
   | ClothOrigin transform (translation + yaw, scale 1)
world
```

```
p_UE [cm]       = 100 (x, -y, z)                 FRbCoords::PositionToUE / PositionToCore
v_UE [cm/s]     = 100 (vx, -vy, vz)              VelocityToUE
w_UE [rad/s]    = (-wx, wy, -wz)                 AngularVelocityToUE   (pseudovector: w' = det(M) M w)
q_UE (w,x,y,z)  = (qw, -qx, qy, -qz)             OrientationToUE -> FQuat(X=-qx, Y=qy, Z=-qz, W=qw)
d_UE            = (dx, -dy, dz)                  DirectionToUE (unit vectors)
phi             = atan2(-Y, X) of a table-local UE direction          AzimuthFromUEDirection
cue axis        d = (cos th cos ph, cos th sin ph, -sin th) -> DirectionToUE          CueDirectionToUE
```

* `ARbTable` owns the world placement: `CoreToWorld`, `WorldToCore`, `CoreDirectionToWorld`, `CoreOrientationToWorld`, `WorldDirectionToAzimuth`. The ball set and the cue are **attached to ClothOrigin**, so their relative transforms are exactly the table-local values from `FRbCoords` and the table can be placed/rotated anywhere.
* Balls: `SetRelativeLocationAndRotation(PositionToUE(p), OrientationToUE(q), ETeleportType::None)`; never `FRotator` (pitfall 3).
* Rotation smear (UE 4.6) needs the angular velocity in ball-local axes: `OrientationToUE(q).UnrotateVector(AngularVelocityToUE(w))`.
* Mesh generation: the mirror flips handedness, so every generated triangle must be wound for an outward normal **in UE space** (UE-1/UE-2/UE-4 tests check normals).
* Material maths works in world centimetres (pitfall 24); ball radius parameter in cm.
* Tests (`RawBreak.Unit.Coords.*`): T14 position, T15 quaternion incl. "mirror commutes with rotation" for a random rotation, T16 omega incl. the rolling top-point direction (T24 in UE axes), azimuth/cue-direction round trip.

---

## 5. Data flow

### 5.1 Scene setup (StartPlay)

```
ARbGameMode::InitGame   parse ?Mode ?Game ?Race ?Lag ?Seed
ARbGameMode::StartPlay  ARbTable (placed in the map, else spawned) -> FRbTableContext::Create(Setup)  [BuildTableGeometry,
                        MakePhysicsParams, BuildBallSet, BuildRulesTable] -> meshes (baked or runtime)
                        spawn ARbBallSet (InitForTable: components per ball, MIDs), ARbCue (InitForTable: CueSpec, CueBodyState)
                        pawn: stroke component <- table, cue; playback <- cue
                        URbMatchDirector::Initialize(table, balls, cue, URbSimulationSubsystem) -> StartMatch(setup)
                        -> SetupRack -> FRbTableState -> ARbBallSet::ShowSimBalls -> AwaitPlacement (break: ball in hand behind the head string)
```

### 5.2 One shot (the M1 loop)

| # | Thread | Step |
|---|---|---|
| 1 | GT | Director publishes `GetShotConstraints` (placement region, call required, push-out, three-foul warning) -> overlay model, stroke component unlocked (`BeginAddress` or `BeginCueBallPlacement`) |
| 2 | GT | Ball in hand: the cue ball follows the aim point on the cloth; Confirm -> `PlaceCueBall` (`CueBallPlacementLegal` / `ValidateDeclaration` with the placed position) |
| 3 | GT | Player gets down (camera rig DownOnShot), aims (azimuth), elevation (wheel, floored by `RbCueClearance`), tip offsets, optional Settle; practice strokes stop short. The stroke component holds the director's `FRbStrokeContext` (`MakeStrokeContext`, pushed at `BeginAddress`) and poses the cue every frame with `rb::human::SampleHand(provisional IntendedStroke, context, t)`: drift, tremor and, from the committed forward stroke, the ramped per-shot draws (architecture.md 13 item 4, HF 3.7). A stroke that showed the ramp and ends without contact -> `OnStrokeAborted(true)` -> director spends the draws (`ShooterShotIndex`++, `AdvanceNoiseHistory`, HF-B13) and pushes a new context |
| 4 | GT (samples timestamped on arrival by the raw-input thread, 6.2.1) | Commit held + final stroke: raw samples -> gain curve -> cue displacement; crossing of the ball surface -> quadratic-fit tip speed at the crossing time -> `FRbStrokeCommit{IntendedStroke, InputLog, ContactTime, AddressIndex, EyeTransform}` |
| 5 | GT | Director: `rb::human::ExecuteStroke(Intended, attributes, StrokeSituation{bridge, clearance floor, pressure, ...}, TipState, CueBodyState, CueSpec, cue-ball spec/position, other balls, NoiseKey, NoiseHistory, HumanParams)` with the SAME context the stroke component rendered -> `CueStrikeInput`; `SimInput` from `FRbTableState` (+ `ShotContext`: in hand, placed position; `NonTipContacts` stay empty in M1 because the rules run `InputMode::Assisted`, 7.1) |
| 6 | GT -> worker | `URbSimulationSubsystem::SubmitShot` -> UE::Tasks: `Simulator::Run` into the pooled reserved result -> compact copy -> `ResultHash` |
| 7 | GT | Subsystem Tick (end of the world tick) waits up to 4 ms -> `OnShotSimulated(TSharedRef<const FRbShot>)` normally in the same frame (a break < 2 ms, UE 9.1) |
| 8 | GT | Director: playback `Play(shot, bAnchorToContact = true)` (or, with `SetLivePlaybackRate(0)` in headless tests, no playback: commit at once); rules immediately but **not shown**: `DeriveShotFacts(Result.Record, RulesTable, tolerances, clock)` -> `EvaluateShot(Config, RulesTable, GameState@start, Declaration, Facts)` |
| 9 | GT (every frame) | Playback: `ShotTime = (now - ContactTime) * Rate`; per ball `StateAtCursor` -> ball transforms; cue follows `CueTipAt(strike 0)`; events fire (`OnShotEvent`, later audio/VFX/chalk) |
| 10 | GT | Playback of THIS pending shot finished (a replay finishing on the same component is ignored) -> director commits: `ApplyShot` (spotting, fouls, turn / decision / rack over / match over), then ONE sync function rebuilds `FRbTableState`: status and plan position from `MatchState.Game.Balls` (authoritative, includes spotted balls), orientation / chalk marks / z = R from `Result.Finals`, cue ball in hand out of play until placed; `ApplyShotToEquipment`, `AdvanceNoiseHistory`, auto-chalk (`PerformChalking`, 7.1), `URbReplaySubsystem::RecordShot`, overlay refresh (auto-glance of the result, 6.6) -> step 1 for the next shooter |

Lag (optional): both players stroke in turn at their own lag ball; the director records both executed strokes first
and submits ONE `SimInput` with two `StrikeRequest`s at t = 0 (architecture.md 13 item 4, rules.md 4.1) ->
`DeriveLagBallFacts` / `EvaluateLag` -> `ApplyLagResult` / `ChooseBreaker`. In practice mode the lag is skipped.

### 5.3 Table meshes

`RbTableMeshBuilder::BuildAll(TableGeometry)` produces the parts of `ERbTablePart` (sources in the header: nose outline + cushion profile, RailTop polygons, PocketGeometry circles and front arcs, sights, apron, legs). Two consumers of the **same** meshes:

* runtime: `UDynamicMeshComponent` per part (tests, and any preset that has not been baked) — no Lumen cards, so only for development;
* editor bake: `URbAssetBakeLibrary::BakeTableMeshes(Preset)` -> `/Game/Generated/Tables/<Preset>/SM_Table_<Part>` (Nanite, distance fields, complex-as-simple collision). `ARbTable` loads these when `bUseBakedMeshes` and they exist.

### 5.4 Balls

`ARbBallSet` (attached to ClothOrigin): one `UStaticMeshComponent` per ball id (mesh `SM_RbBall`, unit sphere scaled by the ball's own radius in cm, so oversized cue balls are exact), Movable, no collision, MID of `M_RbBall` with `BallNumber`, `BallColor`, `BallOmegaLocal`, `ExposureTime`; ball centres into `MPC_RbBalls` (`Ball00..Ball15`) for the analytic cloth occlusion (UE 6.5). The ball set shows state, it never owns it (`FRbTableState` in the director is authoritative).

### 5.5 Playback (render time decoupled from physics time)

* Live: clock anchored at the tip contact (`FPlatformTime::Seconds()` at the crossing): the motion is correct even if the hand-off took a frame (`Play` already shows the state of *now*, not t = 0). Replay: own clock start, any rate (slow motion), pause, seek; the global time dilation never affects it.
* The one playback component serves live shots and replays: every `OnFinished` listener checks that the finished shot is its own (director: `PendingShot` in phase PlayingBack; replay subsystem: `ReplayShot` while replaying).
* Evaluation: `rb::StateAtCursor` per ball per frame (O(1) amortised, bitwise equal to random access; orientation law of `rb/Physics/Playback.h`); `Terminal` segments freeze the ball at capture, hidden after `DropHideDelay` (pocket-fall animation later); `CueTipAt` drives the cue until the tip path ends.
* No teleports (motion vectors for TSR/DLSS and motion blur, pitfall 4). Events fire once, in log order.
* `OnFinished` at `Result.StopTime` (+ drop delay) -> the director commits the shot.

### 5.6 Cue

The cue pose is always given in core terms (`SetPoseCore(tip dome centre, direction)`); local mesh frame: origin at the tip dome centre, +X toward the tip. Drivers: the stroke component (address + displacement; later `SampleHand` so what you see is what hits, HF 3.7) and the playback (`CueTipAt`). No body/hands in M1.

---

## 6. Player

### 6.1 Character

`ARbPlayerCharacter` (ACharacter): capsule r 25 cm, half height 88 cm, walk 1.4 m/s; the table's meshes block the pawn; `UCineCameraComponent` at standing eye height (1.65 m); `URbCameraRigComponent`, `URbStrokeComponent`. One pawn for both hot-seat players. MetaHuman body later (UE 5.1: world-space body, camera in a head socket, head hidden but reflected).

### 6.2 Stroke state machine (`URbStrokeComponent`)

| Phase | Enter | Input | Leave |
|---|---|---|---|
| Locked | not this player's turn, simulating, playing back, replay, decision | — | director unlocks |
| Walking | unlocked, cue ball in position | Move, Look | GetDown (cue ball in reach) -> GettingDown |
| PlacingCueBall | ball in hand | Look = placement point on the cloth (analytic bed plane z = 0 of the table frame); Confirm, or a Stroke (left mouse) press, which the component routes to Confirm in this phase | director accepted -> Walking |
| GettingDown | GetDown | — (camera transition 0.8-1.5 s, cue appears) | -> Down |
| Down | — | Look = aim (FineAim x0.2), wheel = elevation, arrows = tip offset, Settle held = `SettleStart`, Stroke held = cue follows raw mouse Y through the gain curve, Commit held = live; cue pose = `SampleHand` | tip crosses the ball while live -> Contact; GetDown -> Walking (AddressIndex++); a committed stroke that showed the ramp and stops -> `OnStrokeAborted(true)` |
| Contact | crossing | — | broadcasts `OnStrokeContact` once -> Watching |
| Watching | — | GetDown stands up | director locks/unlocks for the next shot |

Rules: practice strokes stop `PracticeStopShort` (4 mm) before the ball unless Commit/Hardcore (UE 14 Q2 proposal); the speed estimator never uses per-frame deltas (pitfall 19); `NoiseKey::AddressIndex` = earlier get-downs on this shot (HF 3.2); `InjectStrokeSamples` feeds scripted strokes through the same path (tests, `RbStroke` cheat). `IntendedStroke` fields from the input log: `TimeDown`, `ForwardStart` (start of the committed forward stroke), `SettleStart`, `PauseDuration` (HF-08), `ContactAcceleration` (quadratic fit, HF-09), `HeadMovedBeforeContact` (look input above a threshold after `ForwardStart`, HF-10).

#### 6.2.1 Raw mouse timestamps (review R-01)

Verified in the 5.8 source (`WindowsApplication.cpp`): UE registers the mouse for raw input on the game window and handles `WM_INPUT` in the once-per-frame message pump, so an `IWindowsMessageHandler` gets every report but all of a frame's reports in one burst; a QPC stamp taken there is the pump time (per-frame quantisation, the exact failure of pitfall 19). UE's optional worker (`WindowsApplication.UseWorkerThreadForRawInput`) queues `RAWMOUSE` without times and bypasses handlers. Design (`FRbRawMouseInput`, UE-5a):

1. A dedicated input thread (pattern of UE's `FWindowsRawInputRunnable`): message-only window, `RegisterRawInputDevices` (page 1, usage 2, flags 0) targeting it, blocking `GetMessage` loop, QPC timestamp at dispatch of each `WM_INPUT`, SPSC ring to the game thread.
2. Raw input is one window per device class and process, so the thread takes the mouse over; the game thread forwards every drained delta to `FSlateApplication::Get().OnRawMouseMove` (what UE does for its own worker mode), so Enhanced Input look / aim are unchanged. When UE re-registers its window (re-entering high-precision mode), an `IWindowsMessageHandler` sees `WM_INPUT` on the game window and asks the thread to register again; UE's `RIDEV_REMOVE` on leaving high-precision mode ends both registrations (nothing to forward while the cursor is visible).
3. Fallback (thread registration impossible, e.g. remote desktop; `-RbRawInputThread=0`): handler on the game window with reconstructed report times (the N reports of a pump spaced by the measured report interval, ending at the pump time). `HasTrueTimestamps()` is shown in the F2 block; the A9 playtest must run with true timestamps.
4. Stopped (`RIDEV_REMOVE`, `WM_QUIT`, join) in the destructor; inactive under `-nullrhi`, commandlets, without a Slate application.

### 6.3 Camera rig

Eyes preset default (decisions): vertical FOV 50 deg authored (MaintainYFOV), pupil 4 mm -> cine lens f/N, focus eased on the aim target, AE 1.5/0.7 EV/s, grain from exposure, head translation 0.3. **Cine camera rule (review R-06, verified in 5.8 `CameraStackTypes.cpp`):** with MaintainYFOV the engine takes the vertical FOV from the lens and the camera's own aspect (the filmback), `V = 2 atan(h_sensor / 2f)`, and the diaphragm DoF scales its circle of confusion by the sensor width. The rig therefore keeps the filmback aspect equal to the viewport aspect (fixed `h_sensor`, `w_sensor = h_sensor * aspect`, updated on resize), sets `f = h_sensor / (2 tan(V/2))` and `N = f / A`. `URbCameraRigComponent::ApplyPresetToCamera` does this for the rig and for the look-dev capture cameras (`ARbLookDevCamera`). Down on the shot: `e = P_axis(s_e) + h_c n_up + y_vc n_side` (UE 4.2) from the cue axis the stroke component reports. Headcam = menu option later (distortion post-process after the upscaler, UE 4.3).

### 6.4 Controller

`ARbPlayerController` creates `URbInputSetup` in `SetupInputComponent` (before the pawn binds), adds the mapping context, handles Glance / ToggleOverlay / ToggleDebug / Replay / CycleOption, owns `URbOverlayComponent`, `CheatClass = URbCheatManager`.

### 6.5 Input (runtime Enhanced Input, M1 defaults)

| Action | Binding | Meaning |
|---|---|---|
| Move | W A S D | walk |
| Look | mouse XY | look; aim while down; placement point while in hand |
| GetDown | right mouse button | get down / stand up |
| Stroke | left mouse button (hold) | stroke mode: mouse Y moves the cue (raw input) |
| Commit | Space (hold) | the stroke is live |
| Elevation | mouse wheel | butt up/down |
| TipOffset | arrow keys | cue-axis offset (english, follow, draw) |
| FineAim | Left Shift (hold) | x0.2 aim |
| Settle | Left Ctrl (hold) | exhale and hold while down (HF-06, `IntendedStroke::SettleStart`) |
| Glance | Tab (hold) | glance at the match info |
| ToggleOverlay / ToggleDebug | F1 / F2 | pin overlay / physics block |
| Replay | R | replay last shot (again: next view; Esc: back) |
| Confirm | Enter / F | place cue ball, accept decision, next rack / new match after RackOver / MatchOver. Left click is not bound twice: a Stroke press while PlacingCueBall is routed to Confirm by the stroke component |
| CycleOption | Q / E | cycle decision options / called pocket |

### 6.6 Info overlay and the "glance" concept

No HUD by default (decisions: diegetic information). M1 stand-in: `SRbInfoOverlay`, a small corner text panel, **Hidden** by default, visible while the glance key is held (fade in/out) or **Pinned** with F1; F2 adds the physics block. **Mandatory lines** (`FRbOverlayModel::MandatoryLines`, review R-05) are shown in every mode, because the rules require them on screen (rules.md 16 item 12: always display the foul counter, the display is the mandatory warning [Reg 8]; item 2: the enforced foul with its rule reference): the shooter's consecutive fouls while > 0 with the two-foul warning, a pending decision with its options, ball in hand for the incoming shooter; after every shot a 4 s auto-glance shows the result. The decisions' "no HUD" is kept for everything else. Content (RUL): discipline + mode, score / race, shooter, consecutive fouls with the mandatory two-foul warning, cue ball in hand (+ region), called ball/pocket where required, pending decision options, last shot (pocketed balls, fouls with rule reference, first contact), debug (tip speed, predicted vs physical miscue, sim ms, events). Later the glance becomes a diegetic scoreboard/chalkboard in the venue.

### 6.7 Replay

`URbReplaySubsystem` keeps the last 32 `FRbShot`s (compact, typically < 1 MB each). `PlayReplay(i, view, rate)`: only when `URbMatchDirector::IsReplayAllowed()` (never while a live shot simulates or plays back), lock input, show the shot's `Request.Input` balls, play with its own clock, `ARbReplayCamera` view (Shooter = stored eye transform, Overhead, Rail, Follow); at the end restore `FRbTableState` and the player view. The stored result is replayed bitwise; re-simulation is only a determinism check. This is the base of the trailer capture kit (slow motion at any rate, arbitrary cameras).

---

## 7. Match bridge and threading

### 7.1 Director phases

`ERbDirectorPhase`: Idle -> (Lag) -> AwaitPlacement / AwaitStroke -> Simulating -> PlayingBack -> AwaitStroke | AwaitPlacement | AwaitDecision | RackOver -> ... -> MatchOver. It wraps `rb::rules::MatchPhase` (Setup, Lag, LagWinnerChooses, RackSetup, AwaitShot, AwaitDecision, RackOver, MatchOver) and adds the simulation/playback/placement steps.

* **Practice**: one human, rules on (fouls shown, ball in hand, pushes), a won rack racks again. No career exists in M1, so the practice shooter also uses the neutral guest profile (50 in every attribute, `HotSeatGuestAttributes`); `?Attr=<0..100>` overrides it for playtests.
* **Hot-seat**: two humans alternate on one pawn; shooters at 50 in every attribute (`HotSeatGuestAttributes`, HF Q4); overlay names the shooter; the stroke component is re-armed for the new shooter (new `FRbStrokeContext`).
* **Rules input mode**: `RulesConfig::Input = InputMode::Assisted` (rules.md 16 item 22): no body or bridge hand exists in M1, so fouls 3.4 / 3.6 / 3.10 cannot occur, `ShotContext::NonTipContacts` stays empty (the `ExecutedStroke::ShaftContactCandidates` go to the F2 debug block only) and illegal cue-ball placements are refused by `ValidateDeclaration`.
* **Pressure** (HF-15): `StrokeSituation::Pressure = ComputePressure({Stakes = kStakesPractice / kStakesFriendly (hot-seat), GameBall = the 9 is the lowest ball on the table, Hill from RackWins}, PressureMode)`; hot-seat may switch it off for both (`PressureMode::Off`, URL `?Pressure=0`). Fatigue 0 (HF-16: off in practice and hot-seat), intoxication 0 (V1 cosmetic).
* Player-model state per shooter (`FRbShooterState`): attributes, habits, `TipState`, `CueBodyState`, `CueSpec`, `NoiseHistory`, `ShooterShotIndex`, `CuePickupIndex`; match seed -> `NoiseKey`. After each shot `ApplyShotToEquipment` + `AdvanceNoiseHistory` (HF 4.7, architecture 13 item 11). M1 has no chores UI: at the start of each visit the tip is auto-chalked with `rb::human::PerformChalking` (`AutoChalkTwists` twists of the shooter's `ChalkCube`, HF-22), so the tip state, and with it `mu` and the miscue limit, evolves exactly as in the full game; the F2 block shows the tip coverage.
* Table condition: M1 room table is level with clean balls (`TableCondition{}`); venues later use `MakeVenueTableCondition`.

### 7.2 Threading

| Thread | Work |
|---|---|
| Game thread | all UObjects: input, stroke machine, director, `ExecuteStroke` (pure, µs), rules evaluation (µs), playback evaluation, UI, replay |
| UE::Tasks worker | `rb::Simulator::Run` + compact copy + hash; reads only the request and the shared `const FRbTableContext`; the `Simulator` object is used by one task at a time |
| RAW BREAK raw-input thread (UE-5a) | owns the raw mouse registration, QPC-stamps every `WM_INPUT` on arrival into an SPSC ring; the game thread drains it once per frame and forwards the deltas to Slate (6.2.1). Stopped and joined in `~FRbRawMouseInput` |
| Render thread | engine standard; ball/cue transforms are set on the game thread without teleport |

No locks: the in-flight task handle is the only shared state; a shot becomes visible to the game thread only as a finished immutable `TSharedRef<const FRbShot>`. `Deinitialize` waits for an in-flight task (no cancellation needed: `Simulator::Run` is bounded by its MaxEvents / time-horizon guards and takes milliseconds). A table rebuilt while a shot is in flight is harmless: the request holds its own `TSharedPtr<const FRbTableContext>`. Later AI: N simulators (one per worker), rollouts with `RecordOptions` off, reduction on the game thread (architecture 5.2).

---

## 8. Rendering, settings and look-dev hooks

### 8.1 Quality presets (plan 9.4) — how they plug in

* `URbGameUserSettings` (registered via `GameUserSettingsClassName`) stores `ERbQualityPreset` Low / Medium / High / Epic / Cinematic / Custom plus camera and comfort options.
* `ApplyQualityPreset`: (1) UE scalability groups (`sg.*`) for Low..Epic, Cinematic = Epic + extras; (2) RAW BREAK rows from `Config/DefaultScalability.ini` sections per level (Lumen GI/reflection method and hit lighting, Lumen Lite on Low, texture pool 1000-4500 MB, volumetric fog, VSM resolution bias, screen percentage / upscaler); (3) material quality (`r.MaterialQualityLevel`) drives Quality Switch nodes in the generated materials (ball haze/SSS lobe off on Low); (4) Cinematic enables photo-mode/replay-only features (path tracer via Movie Render Queue later).
* Every row stays individually adjustable (`Custom`). **Epic and Cinematic are authored for the best image and never capped to the RTX 3070 Ti**, which is the High test tier (decisions 2026-09-27).
* M1 ships the hook (class, storage, High default) — the full matrix and the menu are post-M1.

### 8.2 Materials

Generated by `Tools/unreal/editor/rb_make_materials.py` (UE-3) as Substrate materials whose non-trivial logic sits in HLSL files under `Shaders/Private` included by Custom nodes (`#include "/RawBreak/Private/RbBall.ush"`; mapping by the RawBreakShaders module): diff-able text, no hand-made graphs. Ball: analytic number circles/stripes from object-space position (no UV seams), rotation smear; cloth: fuzz/sheen for grazing angles + `MPC_RbBalls` occlusion; rails: clear coat over wood; pocket liner, sights, cue, room surfaces, lamp diffuser (hidden from RT reflections, pitfall 9).
Headless feasibility (verified in 5.8): `UMaterialExpressionCustom::IncludeFilePaths` is an editable property (`set_editor_property("include_file_paths", ...)`), `MaterialExpressionSubstrateSlabBSDF` / `...VerticalLayering` / `...HazinessToSecondaryRoughness` exist, the Substrate root input is `MaterialProperty.MP_FRONT_MATERIAL`. The commandlet runs under `-NullRHI`, so a broken Custom node only shows at the first render, where the engine silently substitutes its default material: `rbue.py capture` therefore fails on material / shader compile errors (review R-10). The ball decal position must come from a node that is valid in ray-tracing hit shaders too (world -> local transform of the position), so the numbers also appear in the hit-lit reflections of other balls (UE-3 checks it in `ball_lineup`).

### 8.3 Test room and light

`ARbTestRoom`: closed room around the table (static cube meshes -> distance fields + Lumen cards), neutral wall/floor, WPA lamp: rect-light sections with real source size (never 0), lumen units, 4000 K, underside 1.016 m above the bed, target ≥ 520 lux on bed and rails (E4, computed with `RbCameraMath::IlluminanceAt`), dim ambient ≈ 50 lux in the room. Single source (review R-14): the room takes the bed height from the level's `ARbTable` (TableSpec), and the generator writes the lamp underside height into `ARbTable::LampUndersideHeight`, so the physics' off-table apex check sees the rendered lamp.

---

## 9. Headless pipeline (proven on this machine, 2026-09-27)

All commands run from the repo root in **PowerShell** (or Git Bash with `MSYS_NO_PATHCONV=1`; `rbue.py` also undoes Git Bash's `/Game/...` -> `C:/Program Files/Git/Game/...` rewrite for `--map`). `Tools/unreal/rbue.py` wraps every step (logs to `Saved/RbLogs/<cmd>-<time>.log`, exit code 0 = success, kills the process tree on timeout).

### 9.1 Build (UBT)

```
python Tools/unreal/rbue.py build                       # = Build.bat RawBreakEditor Win64 Development -Project=<abs>\RawBreak.uproject -WaitMutex -NoHotReload
python Tools/unreal/rbue.py build --target RawBreak     # game target (packaging later)
```
Measured: full editor build of all four modules 51 s (BilliardsCore) / 29 s (skeleton, 54 actions), incremental 6 s, **0 errors, 0 warnings**. No Unreal process may run while building (DLL locks); `rbue.py` runs steps sequentially.

### 9.2 Editor Python (assets, levels, bakes) — commandlet

```
python Tools/unreal/rbue.py py Tools/unreal/editor/<script>.py [-- args]
  = UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs script> <args>" -unattended -nop4 -nosplash -NoSound -NullRHI -stdout -FullStdOutLogOutput
```
The commandlet exits 0 even when the script raised, so `rbue.py` fails on `LogPython: Error` or the explicit marker `RBUE_FAIL` (`rb_common.fail()`). Measured: level creation 22 s cold, bake self-test 6.5 s. `-NullRHI` means materials are not compiled for SM6 here; the first rendering run compiles them.

### 9.3 Script conventions

* Idempotent: every script deletes/overwrites what it generates; never edits hand-made content.
* Generated assets under `/Game/Generated/**` (paths = `RbAssetPaths.h`), scratch under `/Game/Dev/**` (git-ignored).
* Geometry is generated in C++ (`URbAssetBakeLibrary`, exposed to Python as `unreal.RbAssetBakeLibrary.bake_*`), materials/levels in Python.
* Generated assets are committed (LFS) at milestones so a fresh clone plays without regenerating; the scripts remain the source of truth.

### 9.4 Headless screenshot

```
python Tools/unreal/rbue.py capture --map /Game/... --camera <tag|name> --res 1920x1080 --warmup 120 --out Docs/images/<name>.png
  = UnrealEditor-Cmd.exe <uproject> <map> -game -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -ForceRes
        -RBCapture="<abs png>" -RBCaptureWarmup=120 -RBCaptureCamera="<tag>" -unattended -nosplash -NoSound ...
```
`URbHeadlessCaptureSubsystem` (only created when `-RBCapture=` is present): view through the camera actor with that tag/name (hides the pawn), wait until shader + asset compilers are idle, stream everything, render at least `Warmup` frames AND `-RBCaptureWarmupSeconds` (default 4 s) of game time (auto exposure adapts in EV per second, 0.7 EV/s down for Eyes; Lumen, TSR converge; review R-09), capture through `UGameViewportClient::OnScreenshotCaptured`, write the PNG, `RequestExit`. `-ForceRes` is required (without it a windowed 1920x1080 is clamped below the desktop work area; observed 888x500). Full renderer (DX12 SM6, HWRT Lumen, VSM, Substrate) — not a NullRHI approximation.
M2 additions (18.12): several cameras in one process (`--camera A,B,C --out .../{camera}.png`), `--show-ui` (with the viewport's Slate UI), `--hide-tags`, the adapted EV100 of every view in the log, a missing named camera fails the run, and the `-RBPerf` frame-time recorder behind `rbue.py perf`.
Measured: first start with a cold DDC under Substrate: global shaders ~66 s + engine default materials ~85 s (~3 min); warm DDC: 12 s end to end (+ the 4 s warm-up floor). `rbue.py capture` fails when the log reports a material / shader compile error (`Failed to compile Material`, `Default Material will be used`, `LogShaderCompilers: Error`, `LogMaterial: Error`) unless `--allow-shader-errors`. Look-dev and acceptance captures use `ARbLookDevCamera` actors (a cine camera with the game's camera preset), never plain `ACameraActor`s with engine-default exposure / DoF.

### 9.5 Running the game, cheats

```
python Tools/unreal/rbue.py game [--map /Game/Generated/Maps/L_M1_TestRoom]      # windowed 1920x1080, for the human playtest
UnrealEditor-Cmd.exe <uproject> <map>?Mode=HotSeat -game -RenderOffscreen -ExecCmds="RbStrike 8 0 0 0 0; RbDumpState"   # scripted
```
The editor binary in `-game` mode runs uncooked content. A cooked build: `rbue.py package --label <name>` (M2-0, 18.12) -> `<RB_BUILDS_DIR>/<name>/Windows/RawBreak.exe`; `capture` / `perf` take `--exe <that exe>`.

### 9.6 Automation tests

```
python Tools/unreal/rbue.py test --filter RawBreak.Unit          # NullRHI, editor process
python Tools/unreal/rbue.py test --filter RawBreak.Functional    # PIE under NullRHI
python Tools/unreal/rbue.py test --filter RawBreak.Screenshot --render   # needs -RenderOffscreen
  = UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests <filter>; Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath=... -NullRHI|-RenderOffscreen
```
Naming: `RawBreak.Unit.<Area>.<Name>` (module RawBreak, pure logic), `RawBreak.Functional.<Name>` (RawBreakEditor, PIE via `AutomationOpenMap` + `FStartPIECommand` + latent checks + `FEndPlayMapCommand`), `RawBreak.Screenshot.<Name>`; spec test ids in the name (`...Coords.T14_Position`). Measured: 5 tests (4 unit + PIE smoke) in 16.5 s, all passed.

### 9.7 Proof results (UE-0)

| Step | Command | Result |
|---|---|---|
| Build | `rbue.py build` | Succeeded, 0 errors, 0 warnings |
| Python level | `rbue.py py Tools/unreal/editor/rb_pipeline_proof.py` | `/Game/Dev/PipelineProof/L_PipelineProof` created (22 s) |
| C++ bake from Python | `rbue.py py Tools/unreal/editor/rb_bake_selftest.py` | `SM_BakeSelfTest` saved, extent 50 cm verified (6.5 s) |
| Screenshot | `rbue.py capture --map /Game/Dev/PipelineProof/L_PipelineProof --camera ProofCam --out Docs/images/pipeline-proof.png` | 1920x1080 PNG, full HWRT/Substrate renderer (12 s warm) |
| Tests | `rbue.py test --filter RawBreak.` | 5/5 passed (Coords T14/T15/T16/Azimuth, Functional.PieSmoke) |
| Re-run after the review (section 16) | `rbue.py build`, `build --target RawBreak`, `test`, `py rb_bake_selftest.py`, `capture` | editor 25 s and **game target** 60 s, both 0 errors / 0 warnings; 5/5 tests; bake self-test incl. the Nanite path (full-detail fallback asserted); capture OK in 17.9 s with the 4 s warm-up floor and the strict shader check clean |

![pipeline proof](images/pipeline-proof.png)

### 9.8 Pitfalls found while proving the pipeline

1. Git Bash rewrites `/Game/...` arguments of native programs (MSYS path conversion) -> use PowerShell, `MSYS_NO_PATHCONV=1`, or `rbue.py` (repairs `--map`).
2. `cmd //c "Build.bat ..."` quoting from Git Bash fails; call Build.bat from PowerShell or via `rbue.py`.
3. `-game` windowed resolution is clamped to the desktop work area -> `-ForceRes`.
4. `FTickableGameObject` subsystems: the class default object ticks too -> `Conditional` + `IsTickable()` on an armed flag.
5. The editor appends `AndroidFileServer` settings with a random token to `DefaultEngine.ini` -> plugin disabled.
6. The Python commandlet returns 0 on script exceptions -> scan the log (`rbue.py` does).
7. Never run an Unreal process while UBT links (DLL locks).
8. Shader DDC is shared across projects (Zen): a content-only warm-up project with the same renderer settings pre-fills it.

---

## 10. Test conventions

* Unit tests beside the code in `Source/RawBreak/Private/Tests/Rb<Area>Tests.cpp` (flags `RB_UNIT_TEST_FLAGS`), functional PIE tests in `Source/RawBreakEditor/Private/Tests/`.
* Engine-agnostic maths (T1-T13, T19-T22) are plain `RunTest` checks with the tolerances of UE 13.
* Screenshot checks: each WP renders its own dev map under `/Game/Dev/<WP>/` (script `Tools/unreal/editor/rb_dev_<wp>.py`) with capture cameras and commits the PNGs to `Docs/images/<wp>/` for review (Claude inspects them); pixel-diff regression (E2) comes after M1 once baselines are approved.
* Headless determinism: every functional test uses fixed seeds (`?Seed=`) and scripted strokes/strikes.

---

## 11. Python tools layout

```
Tools/unreal/
  rbue.py                  host runner: build | py | capture | test | game | perf | package |    UE-0 / M2-0
                           ledger | core | owners | selftest (M2 additions: 18.12)
  capture_m1.py            host: all M1 acceptance captures (4 cameras, High, 1920x1080)          UE-8
  editor/                  run INSIDE Unreal (commandlet)
    rb_common.py           helpers: new_level, spawn, spawn_mesh, save, fail (RBUE_FAIL)          UE-0
    rb_pipeline_proof.py   proof level                                                            UE-0
    rb_bake_selftest.py    C++ bake path check                                                    UE-0
    rb_make_materials.py   M_Rb* materials + MPC_RbBalls                                          UE-3
    rb_bake_table.py       BakeTableMeshes for the M1 preset(s)                                   UE-1
    rb_bake_ball.py        BakeBallMesh                                                           UE-2
    rb_bake_cue.py         BakeCueMesh                                                            UE-4
    rb_make_test_room.py   L_M1_TestRoom (room, table, PlayerStart, capture cameras, GameMode)   UE-8
    rb_make_all.py         runs every generator in dependency order                               UE-8
    rb_dev_<wp>.py         per-WP dev map for screenshot checks under /Game/Dev/<WP>              owner WP
```

---

## 12. Milestone M1 — first playable

**Scope** (from the product brief): generated test room (clean, lamp in physical units, neutral walls/floor); 9-ft table built procedurally from `rb` geometry (bed/cloth, cushions, rails, pockets, diamonds); 16 balls with the analytic number/stripe material; first-person pawn: walk around the table, get down, aim with the mouse, tip offset/elevation, mouse stroke with timestamped raw input -> tip speed at contact; `ExecuteStroke` -> `Simulator` on a worker -> playback (position + orientation, decoupled clock); 9-ball **practice** and local **hot-seat** through the rules/match flow (optional lag, break, turns, fouls, ball in hand placement, win); toggleable info/debug overlay + glance key; replay of the last shot; simple procedural cue. **Not in M1**: body/hands (MetaHuman), audio, dive-bar venue, AI opponents, settings menu UI, packaging.

**Acceptance checks** (all headless except A9):

| # | Check | How |
|---|---|---|
| A1 | Build clean | `rbue.py build` (editor) and `--target RawBreak` succeed with 0 errors, 0 project warnings |
| A2 | Regenerate from scratch | delete `Content/Generated`, `rbue.py py Tools/unreal/editor/rb_make_all.py` recreates materials, baked 9-ft table, ball, cue and `L_M1_TestRoom`; a second run succeeds and gives the same asset metrics (per-mesh triangle counts and bounds, material parameter values, the level validator's report). Byte-identical packages are NOT required (UE re-saves packages with new GUIDs / timestamps) |
| A3 | Unit tests | `rbue.py test --filter RawBreak.Unit` green: T1-T16, T19-T22 and the package tests of section 13 |
| A4 | Functional flow | `rbue.py test --filter RawBreak.Functional` green: PieSmoke, MatchFlow (rack -> scripted break -> turn logic; scratch -> ball in hand to the opponent; illegal placement rejected; 9 pocketed wins), stroke path (scripted samples -> human layer -> shot: `RbStroke` in Replay and M1Rack), Replay (bitwise end state), M1Flow on `L_M1_TestRoom`, M1Rack (a complete 9-ball rack through the cheats on `L_M1_TestRoom`, section 17) |
| A5 | Determinism (ROB-10, UE half) | the break9 scenario rebuilt in the UE module through the same core calls as `rbsim` (9-ft pro, 9-ball rack, wooden gaps, seed 11, CB (-0.735, 0.12), break cue, 9 m/s, aim -5.006 deg, offset (0, -0.1)): `RbShot::InputHash` and `ResultHash` equal the values `rbsim ... --hash` printed into `Tools/rbsim/examples/break9.hash` (the rounded `break9.json` can never give a bitwise input) |
| A6 | Performance | logged: break simulation < 2 ms, hand-off in the contact frame for ≥ 95 % of the flow test's shots; capture of `stat unit` during playback on High 1080p: game thread < 6 ms (plan 9.1) |
| A7 | Look | `python Tools/unreal/capture_m1.py` -> `Docs/images/m1/{overhead,chin_on_cue,ball_closeup,room}.png` through `ARbLookDevCamera`s (Eyes preset), strict capture (no shader errors), inspected: correct table proportions and pocket/diamond positions (overhead vs rb geometry), 16 racked numbered/striped balls, no holes/z-fighting/black Lumen patches, lamp uniform on the bed, cue under the chin, sane exposure |
| A8 | Lamp compliance (E4) | computed illuminance ≥ 520 lux on bed and rails, room ≈ 50 lux |
| A9 | **Human playtest (the user)** | `rbue.py game`: walk, get down, aim, stroke with the mouse, 9-ball practice + hot-seat incl. fouls / ball in hand / win, F1/Tab overlay, R replay. The log line of `FRbRawMouseInput` must report true per-report timestamps (6.2.1). The user's verdict closes M1. |
| A10 | Replay | R replays the last shot bitwise from at least the Shooter and Overhead views and returns to the live table |

---

## 13. Work packages

Same process as the core (memory "Dev process"): one agent per WP in its own worktree (branch `ue/<id>`), an adversarial reviewer-fixer, merge by Claude. Every WP: owns exactly the files listed (disjoint), keeps the build green (`rbue.py build`), replaces its `TODO(UE-x)` stubs, adds its tests, runs `rbue.py test --filter RawBreak.` before handing over, commits screenshots of its dev map where listed. Shared contracts (headers of other areas, `RbAssetPaths.h`, `RbTypes.h`, config) change only through the owner/architect.

| WP | Title | Owned files | Depends on | Acceptance tests |
|---|---|---|---|---|
| **UE-0** | Architecture & skeleton (done, reviewed) | `RawBreak.uproject`, `Source/*.Target.cs`, all `Build.cs`, `Config/DefaultEngine.ini`, `Config/DefaultInput.ini`, `Source/RawBreak/Public/RawBreak.h`, `Private/RawBreakModule.cpp`, `Core/*` (RbCoords.h, RbTypes.h/.cpp, RbAssetPaths.h), `Math/RbCameraMath.h` (frozen header), `Dev/RbHeadlessCaptureSubsystem.*`, `Private/Tests/RbTestFlags.h`, `Private/Tests/RbCoordsTests.cpp`, `Source/RawBreakShaders/**`, `Source/RawBreakEditor/Public/RbAssetBakeLibrary.h`, `RawBreakEditorModule.cpp`, `RbAssetBake_Common.cpp`, `RawBreakEditor/Private/Tests/RbPieSmokeTest.cpp`, `Tools/unreal/rbue.py`, `editor/rb_common.py`, `rb_pipeline_proof.py`, `rb_bake_selftest.py`, this document | — | section 9.7 (all green) |
| **UE-1** | Table geometry -> meshes | `Table/RbTableMeshBuilder.h/.cpp`, `Table/RbTable.h/.cpp`, `RawBreakEditor/Private/RbAssetBake_Table.cpp`, `editor/rb_bake_table.py`, `editor/rb_dev_ue1.py`, `Private/Tests/RbTableTests.cpp` | UE-0 | `RawBreak.Unit.Table.*`: nose line at h and on `BuildNoseOutline` within 0.01 mm; pocket cut radius r_p and drop rounding r_d; 18 sights at `Sight::Position`; bed top z = 0; part bounds = `OuterBoundary` (+ apron); outward normals in UE space; closed boundaries; baked triangle count = runtime; `CoreToWorld`/`WorldToCore`/azimuth round trips with a translated + yawed table; part components are transient and rebuilt at BeginPlay (a saved level never stores stale meshes: reload test); screenshots `Docs/images/ue1/{overhead,pocket_closeup}.png` (strict capture) |
| **UE-2** | Balls & playback | `Balls/*` (.h/.cpp), `RawBreakEditor/Private/RbAssetBake_Ball.cpp`, `editor/rb_bake_ball.py`, `editor/rb_dev_ue2.py`, `Private/Tests/RbBallTests.cpp`, `Private/Tests/RbPlaybackTests.cpp` | UE-0 | ball mesh sagitta < 0.035 mm at R (T20 bound), closed; playback of a real simulated 2-ball shot: ball transforms == `FRbCoords(rb::StateAt)` within 1e-6 cm at 200 random times, cursor == random access bitwise, seek back, rate/pause without time jumps, anchored `Play` shows the state of now, Terminal hide, snap to finals, each event fired once in order, `ETeleportType::None`; MPC values; screenshot `Docs/images/ue2/rack.png` (fallback material is fine before UE-3) |
| **UE-3** | Materials & shaders | `Shaders/Private/*.ush`, `editor/rb_make_materials.py`, `editor/rb_dev_ue3.py`, `Private/Math/RbCameraMath_Render.cpp`, `Private/Tests/RbRenderMathTests.cpp` | UE-0 | T1, T6, T7, T19, T20, T21, T22; all `M_Rb*` + `MPC_RbBalls` generated idempotently; the dev-map captures pass `rbue.py capture` in strict mode (no material / shader compile errors); screenshots `Docs/images/ue3/{ball_lineup,cloth_grazing}.png`: 16 correct WPA colours/numbers (6 and 9 underscored), stripes centred, no seams, numbers visible in the hit-lit reflection of a neighbouring ball |
| **UE-4** | Cue | `Cue/*` (.h/.cpp), `RawBreakEditor/Private/RbAssetBake_Cue.cpp`, `editor/rb_bake_cue.py`, `Private/Tests/RbCueTests.cpp` | UE-0 | T13 (20.787 deg +- 0.01); rail floor case; mesh length / tip + butt radii = CueSpec/CueBodyState; `SetPoseCore` puts the tip dome centre exactly at the given point (1e-4 cm) in a yawed table; environment sweep blocked by a wall placed in a test world |
| **UE-5a** | Input & stroke | `Input/*` (.h/.cpp), `Math/RbStrokeMath.h/.cpp`, `Player/RbStrokeComponent.h/.cpp`, `Private/Tests/RbStrokeMathTests.cpp`, `Private/Tests/RbStrokeTests.cpp`, `Private/Tests/RbRawInputTests.cpp` | UE-0 (UE-4 clearance and UE-6b's `FRbStrokeContext` through frozen headers) | T9, T10, T11, T12; scripted stroke of known speed -> `Intended.Speed` within 1e-3 m/s and **bitwise identical** when the same samples arrive in 30 / 60 / 144 fps frame splits; practice stop-short; commit; AddressIndex; cue pose = `SampleHand` and, at t_c with the full ramp, equals `ExecuteStroke` (what you see is what hits); abort after the ramp -> `OnStrokeAborted(true)`, before it -> `false`; Settle sets `SettleStart`; a Stroke press in PlacingCueBall confirms; input setup maps every action, no key twice; raw input: SPSC ring order + fallback time reconstruction unit tests, thread start / stop without leaks; startup log states true timestamps in `rbue.py game` (checked in A9) |
| **UE-5b** | Pawn, camera, controller | `Player/RbPlayerCharacter.h/.cpp`, `Player/RbPlayerController.h/.cpp`, `Camera/*` (.h/.cpp), `Private/Math/RbCameraMath_Camera.cpp`, `Private/Tests/RbCameraMathTests.cpp`, `Private/Tests/RbCameraRigTests.cpp` | UE-0 | T2, T3, T4, T5, T8; eye placement formula (UE 4.2) vs hand-computed; the engine's projection (`FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle`, MaintainYFOV) gives V = 50 deg at 16:9 and 21:9 with the filmback `ApplyPresetToCamera` sets; f and N equal `PupilToCineLens` at the viewport aspect; Defaults == plan 4.9 table; functional: pawn cannot walk into the table (PIE, UE-1 table or a box) |
| **UE-6a** | Simulation service | `Simulation/*` (.h/.cpp), `Private/Tests/RbSimulationTests.cpp`, `Tools/rbsim/Main.cpp` (only a new `--hash` flag printing `InputHash` and `ResultHash`), `Tools/rbsim/examples/break9.hash` | UE-0 | worker hand-off in the same frame, results == `RunShotBlocking` bitwise, compact copy < 1 MB for a break; **ROB-10** as A5 (scenario rebuilt through core calls, both hashes == `break9.hash`); `Deinitialize` with a shot in flight (waits, no crash); `SubmitShot` refuses while busy |
| **UE-6b** | Match director & game mode | `Game/RbShooterState.h`, `Game/RbMatchDirector.h/.cpp`, `Game/RbGameMode.h/.cpp`, `Private/Tests/RbMatchTests.cpp`, `RawBreakEditor/Private/Tests/RbMatchFlowTest.cpp` | UE-0 (UE-6a through its frozen header; tests run with `SetLivePlaybackRate(0)` and scripted strikes, so they need neither UE-2 nor UE-5a) | headless match flows (A4 list: rack -> scripted break -> turn logic; scratch -> ball in hand to the opponent; illegal placement rejected; 9 pocketed wins; three fouls in hot-seat); `ExecuteStroke` path with NoiseScale 0 == intended stroke; TableState sync after spotting (9 pocketed on a foul -> on the table in GameState and TableState at the same position); an aborted stroke with the ramp advances `ShooterShotIndex` / the history; a replay finishing on the playback component never commits; auto-chalk changes `TipState`; `IsReplayAllowed` per phase; Assisted input mode |
| **UE-7** | UI, replay, cheats | `UI/*` (.h/.cpp), `Replay/*` (.h/.cpp), `Dev/RbCheatManager.h/.cpp`, `Private/Tests/RbOverlayTests.cpp`, `RawBreakEditor/Private/Tests/RbReplayTest.cpp`, `editor/rb_dev_ue7.py` | UE-0 | overlay model text for scripted states (two-foul warning, ball in hand, decision options); **mandatory lines present in Hidden mode**; modes Hidden/Glance/Pinned; post-shot auto-glance; replay end state == live end state bitwise; replay refused while a live shot plays; history cap; every cheat drives the director (RbDumpState grep-able; new `RbPlaybackRate <rate>` for tests) |
| **UE-8** | Room, level, settings, M1 look-dev | `Game/RbTestRoom.h/.cpp`, `Dev/RbLookDevCamera.h/.cpp`, `Settings/*` (.h/.cpp), `Config/DefaultScalability.ini`, `editor/rb_make_test_room.py`, `editor/rb_make_all.py`, `Tools/unreal/capture_m1.py`, `RawBreakEditor/Private/Tests/RbM1FlowTest.cpp`, `Private/Tests/RbRoomTests.cpp`, `Docs/images/m1/*` | UE-0; final checks after UE-1..UE-7 merge | E4 lux >= 520 on bed + rails; level validator (one table at the origin, room, PlayerStart at the head end, 4 `ARbLookDevCamera`s with `RbAssetPaths::CaptureCamera` tags, World Settings GameMode = `ARbGameMode`, table `LampUndersideHeight` == room lamp, room bed height == TableSpec); A2 (metrics, not bytes), A7; settings persist and apply the High preset (Hit Lighting reflections on; Low / Medium lower `r.Lumen.HardwareRayTracing.LightingMode`) |

**Waves.** All WPs depend only on UE-0's frozen headers, so any grouping works. Recommended (usage limits, memory "Dev process"): **Wave A** UE-1, UE-2, UE-5a, UE-6a, UE-6b (the playable core loop); **Wave B** UE-3, UE-4, UE-5b, UE-7, UE-8 (look, cue, pawn, UI, room). Then the **integration round**: merge (order UE-6a, UE-1, UE-2, UE-6b, UE-5a, UE-4, UE-5b, UE-3, UE-7, UE-8), cross-package fixes, `rb_make_all.py`, A1-A10, and the user playtest.

**Test-ID coverage.** T1 UE-3, T2-T5 UE-5b, T6-T7 UE-3, T8 UE-5b, T9-T12 UE-5a, T13 UE-4, T14-T16 UE-0 (done), T17-T18 core (Playback tests, done), T19-T22 UE-3, T23 audio (post-M1), T24 core + UE-0 (UE axes), E1 post-M1 (Gauntlet), E2 post-M1 (baselines from A7), E3 UE-2 (no-teleport functional; velocity-buffer check post-M1), E4 UE-8, E5 audio (post-M1), E6 body (post-M1).

---

## 14. Open risks

| Risk | Mitigation |
|---|---|
| Raw mouse timestamps (corrected in review R-01): `WM_INPUT` does reach an `IWindowsMessageHandler`, but in the once-per-frame pump, so handler timestamps are pump times | own raw-input thread with per-report QPC stamps + forwarding to Slate (6.2.1); fallback reconstruction; `HasTrueTimestamps()` logged and checked in A9 |
| Runtime dynamic meshes have no Lumen cards (black GI / reflections) | look-dev and M1 always use baked static meshes (A2); dynamic path only for tests |
| Substrate + HWRT shader compile times (cold DDC ~3 min for engine materials; every new material adds more) | generous capture timeouts, warm the DDC after `rb_make_all.py` with one render run; PSO precaching later for players |
| Headless render path (`-RenderOffscreen`, editor binary, no DLSS) differs from the shipped game | screenshots are for inspection, not pixel baselines, until packaging exists; DLSS plugin later |
| Mouse stroke feel (gain curve, commit rule) is unproven | A9 playtest with the user; parameters live in settings |
| Hot-seat on one pawn (camera / stroke state carry-over between players) | director re-arms the stroke component per turn; functional test covers the hand-over |
| UE 5.8 API drift (e.g. Nanite settings accessors since 5.7, `UE_LOGF`) | skeleton already uses the 5.8 accessors; reviewers build every WP |
| The editor rewrites config files on headless runs (seen: AndroidFileServer) | review `git diff Config/` on every merge |
| Generated binary assets in git (LFS size, merge conflicts) | only generators write them; regenerate instead of merging; `/Game/Dev` ignored |
| Determinism across the UE module vs standalone | ROB-10 check in UE-6a (A5) |

---

## 15. After M1 (hooks already in place)

MetaHuman body with world-space head (UE 5.1) and IK stance (5.3) driven by `SampleHand`; audio from `ShotResult::Events` (UE 8, T23, E5); the dive-bar venue (7-ft bar table, oversized cue ball: `ERbTablePreset::SevenFootBar`, `ERbBallSetPreset::OldBarOversizedCue`, `MakeVenueTableCondition`); AI opponents (`SyntheticHand` + simulator pool); Headcam post-process; full settings menu with the plan 9.4 matrix; trailer capture kit on top of the replay system (Movie Render Queue, path tracer in Cinematic); packaging and Steam.

---

## 16. Review resolution (adversarial review of UE-0, 2026-09-27)

Scope: UE 5.8.3 reality (engine source and plugin files grepped under `C:/Program Files/Epic Games/UE_5.8`), spec compliance (realism plan, human factors UG/UA, rules UI, decisions), threading / lifetime / determinism, work-package cut. Everything below is fixed in this document and, where code was involved, in the skeleton (rebuilt: editor and game target, 0 errors / 0 warnings; tests, bake self-test and capture re-run green, 9.7).

| # | Severity | Finding | Fix |
|---|---|---|---|
| R-01 | high | Raw mouse: UE 5.8 handles `WM_INPUT` of its game window in the once-per-frame message pump, so an `IWindowsMessageHandler` gets all reports of a frame in one burst and a QPC stamp taken there is the pump time: the stroke speed would be frame-quantised, the exact failure of pitfall 19. (The risk table assumed the opposite problem.) UE's own worker-thread mode stores no times and bypasses handlers. | `FRbRawMouseInput` owns a dedicated raw-input thread (message-only window, per-report QPC stamp, SPSC ring), forwards deltas to `FSlateApplication::OnRawMouseMove`, re-registers when UE takes the mouse back, reconstructs times as a fallback; `HasTrueTimestamps()` added; 6.2.1, 7.2, 14, UE-5a acceptance, A9. |
| R-02 | high | In 5.8 ray tracing traces the Nanite FALLBACK mesh (`r.RayTracing.Nanite.Mode=0`, RT proxies off by default) and the Auto fallback decimates: ball reflections, Lumen HWRT hits and complex-as-simple collision would see a coarser table (jaws, noses) than the one rendered. | `WriteStaticMesh` bakes Nanite meshes with a 100 % triangle fallback; `BakeSelfTest` now also bakes and checks a Nanite copy (2.2). |
| R-03 | high | `r.Lumen.HardwareRayTracing.LightingMode` defaults to 0 (surface cache) in 5.8; plan 6.2 / 6.6 / 9.4 require Hit Lighting for Reflections from High up (balls mirroring lamp, room, balls), and High is the M1 default. | `DefaultEngine.ini`: `r.Lumen.HardwareRayTracing.LightingMode=2`; Low / Medium lower it (UE-8 acceptance). |
| R-04 | high | Contract gap vs architecture.md 13 item 4 / HF 3.7 / HF-B13: the rendered cue must follow `rb::human::SampleHand` (what you see is what hits) and an aborted stroke that showed the per-shot ramp must spend the draws. UE-0 had deferred SampleHand to "later", so M1 noise would be invisible (HF principle 1) and the stroke component had no access to the inputs. | `FRbStrokeContext` + `SetStrokeContext`, `OnStrokeAborted(bRampShown)`, `GetHandPose` (stroke component); `MakeStrokeContext`, `OnStrokeAborted` (director); 5.2 steps 3 and 5, 6.2; tests in UE-5a / UE-6b. |
| R-05 | high | Rules UI obligation (rules.md 16 items 2, 12, Reg 8: always display the foul counter, the display IS the mandatory two-foul warning; show the enforced foul with its rule reference) contradicted the Hidden-by-default overlay. | `FRbOverlayModel::MandatoryLines` shown in every mode (fouls > 0 with warning, pending decision + options, ball in hand) plus a 4 s post-shot auto-glance; "no HUD" kept for everything else (6.6, UE-7). |
| R-06 | medium | Cine camera (verified in 5.8 `CameraStackTypes.cpp`): with MaintainYFOV the vertical FOV comes from the lens and the FILMBACK aspect, and diaphragm DoF scales by the sensor width; the plan's `f = w_sensor / (2 tan(H/2))` is right only if the filmback aspect equals the viewport aspect, otherwise FOV and DoF blur are off at 21:9 (or with a 3:2 back). | Rule in 6.3 and the rig header: filmback aspect = viewport aspect, `f = h_sensor / (2 tan(V/2))`, `N = f / A`; `URbCameraRigComponent::ApplyPresetToCamera`; UE-5b acceptance checks the engine's own projection matrix at 16:9 and 21:9. |
| R-07 | high | One playback component serves live shots and replays, and the director listened to its `OnFinished` unconditionally: a finished replay would have committed the replayed shot a second time (`ApplyShot` twice); the replay subsystem likewise reacted to live shots. | Guards implemented in the stubs (director commits only its `PendingShot` in PlayingBack; replay acts only on `ReplayShot`); `URbMatchDirector::IsReplayAllowed`; 5.5, 6.7, tests in UE-6b / UE-7. |
| R-08 | medium | Look-dev / acceptance captures (A7) used plain `ACameraActor`s, i.e. engine-default FOV, exposure and DoF, so "sane exposure" was judged through a camera the game never uses. | New `ARbLookDevCamera` (cine camera applying a RAW BREAK preset, owner UE-8); capture subsystem finds it as a camera actor; 9.4, A7, UE-8. |
| R-09 | low | Capture warm-up counted only frames; auto exposure adapts in EV per second (0.7 EV/s down for Eyes), so on a fast GPU 90 frames can end before exposure settles. | `-RBCaptureWarmupSeconds` (default 4 s of game time) in `URbHeadlessCaptureSubsystem`, `rbue.py capture --warmup-seconds`; verified (4.6 s warm-up in the re-run). |
| R-10 | medium | Materials are generated under `-NullRHI`; a broken Custom-node include only shows at the first render, where UE silently substitutes the default material, and the capture still "succeeds". | `rbue.py capture` fails on `Failed to compile Material`, `Default Material will be used`, `LogShaderCompilers: Error`, `LogMaterial: Error` (opt-out `--allow-shader-errors`); UE-3 acceptance uses it; clean re-run passes. |
| R-11 | medium | The rules input mode for M1 was unspecified (Sim mode would require body / cue colliders that M1 does not have, rules.md 16 item 22). | M1 = `InputMode::Assisted`: no 3.4 / 3.6 / 3.10 fouls, `NonTipContacts` empty, shaft-contact candidates only in the F2 block, illegal placement refused (7.1, director header). |
| R-12 | high | Two sources of truth for ball positions after a shot: `ApplyShot` spots balls in the rules' `GameState`, while 5.2 rebuilt `FRbTableState` from `Result.Finals` only, so a spotted 9 would be missing / misplaced for the next simulation. | One sync function: status + plan position from `MatchState.Game.Balls` (authoritative, incl. spotted balls), orientation / chalk marks / z from `Finals`, cue ball in hand out of play until placed (`RbShooterState.h`, 5.2 step 10, UE-6b test). |
| R-13 | high | ROB-10 as planned (`rbsim --in break9.json --hash`) cannot work: `break9.json` is a rounded OUTPUT file (6 significant digits), not an `rbsimInput` dump, so no bitwise input exists; the UE module also has no reader for it. | The UE test rebuilds the break9 scenario through the same core calls rbsim makes and compares `RbShot::InputHash` (added) and `ResultHash` with `Tools/rbsim/examples/break9.hash` written by `rbsim --hash` (UE-6a, A5). |
| R-14 | low | Duplicate constants: `ARbTestRoom::BedHeight` (76.5 cm) vs `TableSpec::BedHeight`, and `ARbTable::LampUndersideHeight` (1.0 m) vs the room lamp (1.016 m) feeding the physics' off-table apex check. | Single-source rule (room reads the table, generator writes the lamp height into the table), checked by the UE-8 level validator (8.3). |
| R-15 | medium | UE-6's headless match-flow tests could not pass inside Wave A: shots commit only on the playback's `OnFinished` (UE-2, same wave) and a break plays 11 s of real time. UE-6 was also the largest package (simulation service + director + game mode + ROB-10 + rbsim). | `URbMatchDirector::SetLivePlaybackRate(0)` = commit right after the simulation (tests, `RbPlaybackRate` cheat); UE-6 split into UE-6a (simulation service, ROB-10) and UE-6b (director, game mode, match tests), both Wave A (13). |
| R-16 | low | `URbShotPlaybackComponent::Play` showed t = 0 in the hand-off frame of a live shot instead of the state at "now - contact". | Fixed in the stub (5.5). |
| R-17 | medium | Left mouse was bound to both Stroke and Confirm, violating UE-5a's own "no key bound twice" acceptance. | Confirm = Enter / F; a Stroke press while PlacingCueBall is routed to Confirm (6.2, 6.5, input header). |
| R-18 | medium | HF V1 inputs of `ExecuteStroke` without a source: no Settle input (HF-06, `SettleStart`), no `ChalkCube` in the shooter state (HF-22 chalking needs it), pressure / fatigue / `PauseDuration` / `HeadMovedBeforeContact` undefined for M1. | `Settle` action (Left Ctrl) + handler; `FRbShooterState::Cube` + auto-chalk with `PerformChalking`; pressure via `ComputePressure` (practice / friendly stakes, game ball, hill, `?Pressure=0`), fatigue 0, intoxication 0; input-log derivations in 6.2 (7.1). |
| R-19 | low | A2 demanded that a second generator run "changes nothing": UE re-saves packages with new GUIDs / timestamps, so byte identity is not achievable. | A2 compares asset metrics and the level validator report instead (12). |
| R-20 | low | Playtest flow gaps: no input to continue after RackOver / MatchOver, and no attribute profile for practice (no career in M1; `ShooterAttributes` defaults to 25). | Confirm = next rack / new match; practice uses the neutral guest profile (50), `?Attr=` override (6.5, 7.1). |
| R-21 | info | A1 requires the game target, which UE-0 had never built. | Built: `rbue.py build --target RawBreak` succeeds, 60 s, 0 warnings (9.7). |
| R-22 | low | Ownership nits: the game mode's TODO claimed the M1Flow test (owned by UE-8's `RbM1FlowTest.cpp`); `RbCameraMath.h` was listed for no package. | Comment fixed; `RbCameraMath.h` is a UE-0 frozen header, each owner implements its own .cpp (3, 13). |

**Verified and unchanged.** Coordinate adapter and its tests (positions, the pseudovector sign rule `w_UE = (-wx, wy, -wz)`, quaternion mirror, azimuth / cue direction); no First Person Rendering FOV or scale anywhere (world-space cue, near clip 1 cm); every engine name the skeleton and the plan rely on exists in 5.8.3: `UInputMappingContext::MapKey`, `UMaterialExpressionCustom::IncludeFilePaths`, `UMaterialExpressionSubstrateSlabBSDF` / `VerticalLayering` / `HazinessToSecondaryRoughness`, `MP_FrontMaterial` (Python-visible `EMaterialProperty`), `FMeshNaniteSettings` fallback fields, `UE::Tasks::TTask::Wait(FTimespan)`, `IWindowsMessageHandler` + `FWindowsApplication::AddMessageHandler`, `FSlateApplication::OnRawMouseMove`, `UDynamicMeshComponent::SetComplexAsSimpleCollisionEnabled`, all `r.*` keys of `DefaultEngine.ini` as `URendererSettings` console variables (`r.Substrate.ProjectGBufferFormat` 1 = Adaptive); threading (the worker sees plain data and a thread-safe `TSharedPtr<const FRbTableContext>`, immutable hand-off, `Deinitialize` waits, no UObject off the game thread); replays play the stored result bitwise; headless screenshots work with the full renderer. Remaining open items stay in section 14 (stroke feel, cold shader cache, headless vs packaged look, config rewrites).

**Final work-package list** (details in section 13):

| WP | Title | Depends on |
|---|---|---|
| UE-0 | Architecture & skeleton | done, reviewed |
| UE-1 | Table geometry -> meshes | UE-0 |
| UE-2 | Balls & playback | UE-0 |
| UE-3 | Materials & shaders | UE-0 |
| UE-4 | Cue | UE-0 |
| UE-5a | Input & stroke (raw-input thread, SampleHand cue) | UE-0 (UE-4, UE-6b headers) |
| UE-5b | Pawn, camera, controller | UE-0 |
| UE-6a | Simulation service + ROB-10 | UE-0 |
| UE-6b | Match director & game mode | UE-0 (UE-6a header) |
| UE-7 | UI (mandatory lines), replay, cheats | UE-0 |
| UE-8 | Room, level, settings, look-dev cameras, M1 captures | UE-0; final checks after UE-1..UE-7 |

Wave A: UE-1, UE-2, UE-5a, UE-6a, UE-6b. Wave B: UE-3, UE-4, UE-5b, UE-7, UE-8. Then the integration round with the user playtest (A9) = M1.

---

## 17. M1 integration (round 5, 2026-09-28)

Branch `integ/m1` = main + `ue3-reviewed`, `ue4-reviewed`, `ue5b-reviewed`, `ue7-reviewed`, `ue8-reviewed` (`--no-ff`, no textual
conflicts, no file touched by two packages; wave A was already on main).

**Cross-package fixes made during the integration**

| Area (owner) | Fix |
|---|---|
| Stroke floor (UE-5a, requested by the UE-4 review) | `URbStrokeComponent::UpdateElevationFloor` passes `ContactElevation` = aim elevation, the tip dome radius / width, and uses `RbCueClearance::ComputeMinElevationWithEnvironment` (bisection to 0.01 deg) instead of its own 1 deg loop: a raised aim over a ball 0.10 m behind now floors at 21.40 deg (was 15.3 / 12.6 / 9.8 deg for aims 15 / 22 / 30 deg, cue drawn through the ball). `RawBreak.Unit.Cue.FollowsStrokePose` checks aims 10 / 15 / 20 deg below the floor. |
| Rack orientation (UE-6b) | Every rack used to lie in the identity orientation: all numbers upside down from the head end, every stripe vertical. `RackNext` now gives each ball a uniformly random orientation, deterministic per match seed / rack / ball (no effect on the motion; replays and seeded tests unchanged). |
| Packaging (UE-0 config) | `DefaultGame.ini` `ProjectPackagingSettings`: `MapsToCook` = `L_M1_TestRoom`, `DirectoriesToAlwaysCook` = `/Game/Generated` and `/Engine/BasicShapes` (the ball / cue / table meshes, the materials and the room's cube / cylinder shell are loaded by path at runtime and referenced by no saved package; UE-8 review risk). |
| Console on German keyboards (UE-0 config) | `DefaultInput.ini` `+ConsoleKeys=Caret` (the M1 build has no settings menu; presets are console commands). |
| `rbue.py` (UE-0) | stdout / stderr reconfigured to UTF-8 with `errors=replace` (UE-4 / UE-7 review requests). |
| M1 captures (UE-8) | `capture_m1.py --set player`: the player's own first-person views through the pawn's camera rig (`standing`, `down_on_shot`, `after_break`, layout in `rb_m1_layout.py`) next to the four A7 look-dev views. |
| First-shot cost (UE-6a) | `URbSimulationSubsystem::Prewarm` (at `OnWorldBeginPlay` of game worlds) runs the break9 reference shot once on the pooled simulator / result: the first break of a packaged game went from 5.5 ms to 3.0 ms (the rest is start-up contention: shaders / PSOs compile in the same seconds). |
| New test | `RawBreak.Functional.M1Rack` (`RawBreakEditor/Private/Tests/RbM1RackTest.cpp`): a complete 9-ball practice rack on `L_M1_TestRoom` through the console cheats with real-time playback (x4): `RbStroke` break (human layer), shots planned by simulating candidate strikes on the director's table state, a deliberate foul -> ball in hand anywhere + mandatory overlay line, an illegal placement refused, `RbStroke` shots through the human layer, the 9 pocketed legally -> RackWon; after every shot the ball set shows the committed table state; replays of the winning shot from the Shooter and the Overhead view return to the live table; Confirm racks again; A6 hand-off statistics. |

**Acceptance (A9 = the user's playtest)**

| # | Result |
|---|---|
| A1 | `rbue.py build` and `--target RawBreak`: 0 errors, 0 warnings |
| A2 | `Content/Generated` deleted, `rb_make_all.py --strict` regenerated 19 meshes, 15 materials and the level (validator OK); a second run with `--compare`: metrics identical |
| A3 | all `RawBreak.Unit.*` green (T1-T16, T19-T22 and the package tests) |
| A4 | all `RawBreak.Functional.*` green: PieSmoke, MatchFlow, Replay, M1Flow, M1Rack, Player.PawnBlockedByTable (163 UE tests in total, all green) |
| A5 | `RawBreak.Unit.Simulation.ROB10_Break9Hash` green |
| A6 | GT during the break playback in the packaged Development build, High, 1080p (CSV profile, 450 frames): median 1.13 ms, p95 1.34 ms, max 3.7 ms (< 6 ms); GPU 15 ms. Same-frame hand-off 4 / 4 strokes through the stroke component (M1Rack; strikes sent from a console command or a test's latent command run after the world tick and hand off one frame later by construction, 5.2 step 7). Break simulation in the editor 1.6-2.1 ms (prewarm 1.6-1.7 ms); the first break of the packaged game 3.0 ms during start-up (after the prewarm) |
| A7 | `Docs/images/m1/{overhead,chin_on_cue,ball_closeup,room}.png` (look-dev cameras) + `{standing,down_on_shot,after_break}.png` (the player's view), strict captures, inspected |
| A8 | lamp on bed 684-875 lux, rails 613-850 lux (>= 520), ambient panels 46.9 lux on the floor |
| A10 | M1Rack: replays from the Shooter and the Overhead view return to the live table; Functional.Replay: every frame bitwise == the stored result |

Packaged build: `RunUAT BuildCookRun ... -build -cook -stage -pak -archive` -> `RawBreak_Builds/M1/Windows/RawBreak.exe` (not in git);
it starts headless (`-RenderOffscreen`, `-RBCapture`, `-ExecCmds` break) and renders the same frame as the editor.

---

## 18. M2 — feel fixes, table look-dev, dive-bar slice v1, audio v1, menus, balls off the table (plan, 2026-09-29)

### 18.0 Scope and priorities

M2 turns the M1 first playable into a vertical slice in the first real venue and fixes what the product owner's M1 playtest
(`Docs/playtests/2026-09-28-m1.md`) found. **Priority order** (the playtest decides it, not the feature list):

| Prio | Item | Package |
|---|---|---|
| 1 | **Feel**: P1 the view stays calm on the shot after the contact; P3 aiming at normal mouse speed (coarse + Shift fine, sensitivity setting); P2 diegetic ball in hand (a hand carries the ball, exact preview, set down, illegal spots readable); **P5 the camera IS the eyes** (procedural human head / body motion, realism plan 4.8) | M2-F |
| 1 | **Table and cloth look real** (P4): worsted / napped cloth, cushions with visible rubber and rounded real-geometry profiles, lacquered wood with depth, pocket hardware, apron / legs, the dive bar's 7-ft coin-op cabinet | M2-L |
| 2 | Dive-bar vertical slice v1 (venue-dive-bar DB-0..DB-3 **without** Meshy / Higgsfield assets): shell, architecture, lighting, bar furniture and props from procedural generators, master materials with Age 0.80, CC0 scans, first decal set; `L_DiveBar` playable with the M1 gameplay (9-ball practice / hot-seat on the 7-ft bar table, oversized cue ball) | M2-A, M2-B |
| 2 | Audio v1 (audio.md): all physics sounds synthesised sample-accurately from the shot events, footsteps, room tone, reverb | M2-C |
| 2 | Menus / settings v1 (ui-ux.md): pause, settings with presets + individual rows persisted in `URbGameUserSettings`, minimal title / venue select | M2-D |
| 3 | Balls off the table under engine physics + pick-up / return; multi-table groundwork (nothing may assume one table per level) | M2-E |

**Not in M2**: Meshy / Higgsfield assets (stools by Meshy, taxidermy, brand art, posters), NPCs, MetaHuman body and arms (M3;
M2's carrying hand is a stand-in), AI opponents at other tables, the 3D main-menu scene "Closing Time", phone / notebook,
localisation (EN only; every string still goes through `FText`), controller support, library samples / voice / music, the DLSS
plugin, Steam packaging. VDB-T5 (performance gate), VDB-T6 (blind test) and VDB-T9 (story) belong to DB-5 / DB-6.

### 18.1 The architect step and the rules for M2

Done by the architect before any package starts (compiles, all M1 tests green; 18.10 M2-A1 / M2-A3):

| Area | What the architect step put in place |
|---|---|
| Modules | new runtime module **`RawBreakAudioDsp`** (Core only, no UObjects; `RawBreak.uproject`, both targets); `RawBreak.Build.cs` + `AudioMixer`, `RawBreakAudioDsp`, `PhysicsCore` |
| Config (`DefaultEngine.ini`) | `r.VirtualTextures=True` (venue H-3; a project-wide shader recompile, so it goes in once, now); collision channels `RbCueSweep` (trace, GameTraceChannel1) and `RbLooseBall` (object, GameTraceChannel2), profiles `RbVenueBlock`, `RbVenueProp`, `RbLooseBall`, `RbBallReturn`, `Pawn` ignores both new channels; physical surfaces `RbBall`, `RbVct`, `RbRubber`, `RbWood`, `RbConcrete`, `RbCloth` (SurfaceType1..6); UI scale rule `URbDpiScalingRule` (height / 1080, ui-ux C2); the **audio block** (48 kHz, 512 x 2 buffers, 96 channels, 4 source workers) between markers, owned by M2-C |
| Shared contracts (`Core/`) | `ERbVenue` (TestRoom, DiveBar) + `RbTypes::MapFor`, `ERbVenueKind` + `ToCore`; `RbAssetPaths`: `DiveBarMap`, `TitleMap`, venue / audio / player / physics directories, dive-bar table material instances, physical materials, tags (`RbPlayerTable`, `RbAudio_<Anchor>`, `RbDB_Ceiling`, `RbLooseBall`, `RbBallReturn`, `RbVenueInfo`), capture tags `RbCam_DB_V01..V12`, `RbCam_DB_TH1..TH7`, `RbCam_Menu_S0..S7`, `Collision::*`, `Surface::*` |
| `ARbTable` (hand-off H-2) | `TableIndex`, venue condition (`bUseVenueCondition`, `VenueKind`, `VenueSeed`, `bFirstCareerTable` -> `rb::human::MakeVenueTableCondition` / `VenueBallSetSeed`), lamp footprint (`EnvironmentSpec::LampFootprint`), `MakeTableSetup()`; all in the build key |
| Playback -> audio contract | `URbShotPlaybackComponent::OnPlaybackStarted` / `OnPlaybackClockChanged` with `FRbPlaybackClock` (origin clock, origin shot time, rate, held, live) and `GetClockMapping()`: the audio schedules every event from the mapping, never by polling (18.5) |
| Input | `Pause` action (Esc, triggers while paused) replaces M1's legacy Esc key binding: `ARbPlayerController::HandlePause` = replay back, else `URbUiSubsystem::TogglePauseMenu`; 16 actions (tests updated: mapping test, and `Player.ControllerActions` checks the Pause binding and that no legacy Esc key binding remains) |
| Camera -> audio | `URbCameraRigComponent::OnFootstep(FRbFootstep)` (fired by M2-F, heard by M2-C) |
| Interaction | `URbInteractionSubsystem` (providers with Offer / Interact; dispatch implemented); `ARbPlayerCharacter::HandleConfirm` asks it first after the ball-in-hand placement |
| Settings API | `FRbControlSettings`, `FRbCameraSettings`, `FRbAudioVolumes`, `bShowKeyHints` in `URbGameUserSettings` (validated ranges), static `OnSettingsChanged()` broadcast after every apply (18.4) |
| Stubs (`TODO(M2-x)`) | every class of 18.3-18.8 as a compiling skeleton; generator scripts that log "not implemented yet"; `rb_make_all.py` knows the M2 generator order and the venue validator; `Tools/blender/rbbl.py` + `common/rb_bl.py` + `divebar/db_build_all.py` (Blender runner, working); `Tools/art/fetch_cc0.py` (working: per-package input lists, SHA-256 locks, ledger fragments); `rbue.py test --sound`, `--extra` |

**Rules** (same process as M1, memory "Dev process"): one agent per package in its own worktree `.claude/worktrees/m2-<x>` on
branch `ue/m2-<x>`, an adversarial reviewer-fixer, merge by the architect. Each package edits **only** the files it owns (18.2);
everything else is a request in its report (the architect applies it at the merge). Build 0 errors / 0 warnings from project code;
all existing UE (163) and core (904) tests stay green; new tests for the package's acceptance. Generated assets only under the
package's `/Game/Generated/...` roots (LFS) and `/Game/Dev/M2<x>/` (git-ignored); dev screenshots under `Docs/images/dev/m2<x>/`,
**looked at**; external assets only CC0 from Poly Haven / ambientCG through `Tools/art/fetch_cc0.py`, each with a row in the
package's ledger fragment `Docs/licenses/ledger/M2-<x>.csv` (merged into `Docs/licenses/asset-ledger.csv` by the architect; nobody
else edits the merged file). The public headers of 18.1 are frozen contracts (additions allowed; renames / signature changes only
with the consumer's sign-off, as in section 3). Fresh worktrees: `git lfs install --local`, `git lfs pull`. Shared machine: at most
three packages compile shaders / capture at the same time; never leave an Unreal or Blender process running.

### 18.2 Work packages (disjoint owned files)

`Area/*` means `Source/RawBreak/Public/Area/*` + `Source/RawBreak/Private/Area/*`. Tests are files in `Source/RawBreak/Private/Tests/`
(unit, `RawBreak.Unit.*`) or `Source/RawBreakEditor/Private/Tests/` (PIE, `RawBreak.Functional.*`), named without `.cpp` below.

| Package | Title | Owned files / globs | Depends on | Acceptance (details in 18.3-18.8) |
|---|---|---|---|---|
| **M2-F** | Feel: stroke, look, aim, ball in hand, human motion (P1, P2, P3, P5) | `Input/*` (RbInputSetup, RbRawMouseInput, RbAimResponse), `Player/*` (RbStrokeComponent, RbPlayerCharacter, RbPlayerController, RbBallInHandComponent, RbLookIntentGate), `Camera/*` (RbCameraRigComponent, RbCameraModel, RbHeadMotion, RbHumanMotion), `Cue/*`, `Math/RbStrokeMath.*`, `Private/Math/RbCameraMath_Camera.cpp`, `RawBreakEditor/Private/RbAssetBake_Player.cpp`, `RbAssetBake_Cue.cpp`, `Tools/unreal/editor/rb_make_player.py`, `rb_bake_cue.py`, `rb_dev_m2f.py`, `Tools/feel/**` (trace plots), `Config/DefaultInput.ini`; tests RbStrokeTests, RbStrokeMathTests, RbRawInputTests, RbCameraRigTests, RbCameraMathTests, RbCueTests, new RbFeelTests (input traces P1 / P3), RbHumanMotionTests, RbBallInHandTests, RbFeelFlowTest (PIE); content `/Game/Generated/Player/**`, `/Game/Generated/Cues/**`; `Docs/images/dev/m2f/**` | architect step; reads M2-D's settings structs (frozen), M2-E's director API (existing) | 18.3: F1-F9 (P1 / P3 / P2 / P5 input traces, frame-rate and DPI independence, seeded determinism, captures and trace plots); M1 stroke tests unchanged |
| **M2-L** | Table & cloth look-dev (P4) + coin-op cabinet | `Table/*` (RbTableMeshBuilder, RbTable), the `ERbTablePart` enum block in `Core/RbTypes.h` and `RbTypes::ToString(ERbTablePart)` in `RbTypes.cpp` (**append-only exception**; nobody else edits them), `RawBreakEditor/Private/RbAssetBake_Table.cpp`, `Shaders/Private/*.ush` (top level only), `Private/Math/RbCameraMath_Render.cpp`, `Game/RbTestRoom.*`, `Dev/RbLookDevCamera.*`, `Tools/unreal/editor/rb_make_materials.py`, `rb_bake_table.py`, `rb_import_table.py` (new), `rb_make_test_room.py`, `rb_m1_layout.py`, `rb_dev_m2l.py`, `Tools/unreal/capture_m1.py`, `capture_table.py` (new), `Tools/blender/table/**` (body / cabinet generators), `Art/Tables/**` (rbsim geometry JSONs, body exports, `cc0_inputs.json`); tests RbTableTests, RbRenderMathTests, RbRoomTests, new RbTableLookTests; content `/Game/Generated/Tables/**`, `/Game/Generated/Materials/**` (incl. `MI_RbBall_DiveBar`, `MI_RbCloth_BarGreen`, `MI_RbRail_BlackLaminate`), `/Game/Generated/Maps/L_M1_TestRoom`; `Docs/images/m1/**`, `Docs/images/dev/m2l/**`, `Docs/references/table-lookdev.md` | architect step | 18.7: physics surfaces exactly on the rb geometry (M1 table tests + new profile tests), 9-ft and 7-ft captures (chin-on-cue, standing, pocket, cushion grazing, rail close-up, overhead) compared with reference photos per round, strict captures, M1 A7 re-captured |
| **M2-A** | Venue shell, architecture, lighting, level, cameras | `Venue/*` (ARbVenueInfo), `Tools/blender/rbbl.py`, `Tools/blender/common/**`, `Tools/blender/divebar/db_axis_test.py`, `db_arch.py`, `db_neon.py`, `cue_sweep_check.py`, `Art/DiveBar/layout.json`, `lights.json`, `calibration.json`, `neon/**`, `Art/DiveBar/Export/{AxisTest,Arch,Neon}/**`, `Tools/unreal/editor/rb_import_divebar.py`, `rb_make_divebar.py`, `rb_make_divebar_fx.py` (new), `Tools/unreal/capture_divebar.py`; tests new RbVenueTests, RbDiveBarTest (PIE: walkability, a complete 9-ball rack like M1Rack); content `/Game/Generated/Maps/L_DiveBar*`, `/Game/Generated/Venues/DiveBar/{Arch,Lighting,FX}/**`; `Docs/images/divebar/**`, `Docs/images/dev/m2a/**` | architect step; places M2-L's `ARbTable` and M2-B's props by asset id (greybox fallback: never blocked) | 18.8: DB-0 axis test, DB-1 greybox + walkability + cue sweeps (VDB-T3), DB-2 light (VDB-T1, T2, T8, T11), VDB-T10, VDB-T12 (M2 part), level validator, DiveBarRack, captures V01..V12 / TH1..TH7 |
| **M2-B** | Bar props, venue materials, CC0, decals | `Tools/blender/divebar/db_bar.py`, `db_backbar.py`, `db_booth.py`, `db_stool.py`, `db_ledges.py`, `db_lamp.py`, `db_cue_rack.py`, `db_jukebox.py`, `db_dart.py`, `db_lathe_props.py`, `db_props_common.py`, `db_decals.py`, `Art/DiveBar/Export/{Props,Decals}/**`, `Art/DiveBar/Textures/**`, `Art/DiveBar/cc0_inputs.json` (+ lock), `Tools/art/**` (fetch_cc0, text_textures, decal_prep), `Art/Fonts/**`, `Tools/unreal/editor/rb_make_divebar_materials.py`, `rb_dev_m2b.py`, `Shaders/Private/Venue/*.ush`; content `/Game/Generated/Venues/DiveBar/{Props,Materials,Textures,Decals}/**`; `Docs/images/dev/m2b/**` | architect step (Blender runner); M2-A's importer puts the props into the level (they stand alone in B's dev map) | 18.8: DB-0 stool end to end, every generator asserts its spec dimensions (VDB-T4), masters `M_DB_*` with Age 0.80 + `MPC_DB_Venue`, CC0 inputs pinned + ledger (VDB-T7 without the OCR pass), first decal set, look-dev captures per hero prop, DB-3 views with M2-A |
| **M2-C** | Audio v1 | `Source/RawBreakAudioDsp/**`, `Audio/*`, the audio block of `Config/DefaultEngine.ini` (between its markers), `Tools/audio/**`, `Tools/unreal/editor/rb_make_audio.py`; tests new RbAudioDspTests, RbAudioTests, RbAudioFunctionalTest (PIE, `--sound`); content `/Game/Generated/Audio/**`; `Docs/audio/m2/**`, `Docs/images/dev/m2c/**` | architect step (playback clock contract, footstep delegate, loose-ball impact delegate, volumes) | 18.5: AU-T01..T13 offline subset, engine AU-T08 / AU-T21 (sample-accurate), T16, T19; every sound class audible in a recorded break; venue room tone + reverb; volumes + pause mix |
| **M2-D** | Menus & settings | `UI/**` (incl. M1's SRbInfoOverlay / RbOverlayComponent), `Settings/**` (RbGameUserSettings, RbSettingsRegistry, RbSettingsTypes), `Config/DefaultScalability.ini`, `Tools/unreal/editor/rb_make_title.py`, `rb_dev_m2d.py`; tests RbSettingsTests, RbOverlayTests, new RbUiTests, RbMenuFlowTest (PIE); content `/Game/Generated/Maps/L_Title`, `/Game/Generated/UI/**`; `Docs/images/dev/m2d/**` | architect step; reads M2-F's stroke / ball-in-hand state and M2-E's interaction verbs (public getters) for the key hints | 18.4: UX-T01 (EN), T02, T05, T07, T09 (keyboard + mouse), T10, T20, T25 (with M2-E), T26; settings persist across a restart; the title opens both venues in Practice / Hot-seat |
| **M2-E** | Balls off the table, multi-table groundwork, match, interaction | `Balls/*` (RbBallSet, RbShotPlaybackComponent, RbBallMeshBuilder, RbLooseBall, RbLooseBallSubsystem, demo actors), `Game/*` except `RbTestRoom` (RbGameMode, RbMatchDirector, RbShooterState, RbTableSubsystem), `Interaction/*`, `Replay/*`, `Simulation/*`, `Dev/RbCheatManager.*`, `RawBreakEditor/Private/RbAssetBake_Ball.cpp`, `Tools/unreal/editor/rb_bake_ball.py`, `rb_make_physics.py`, `rb_dev_m2e.py`; tests RbBallTests, RbPlaybackTests, RbMatchTests, RbSimulationTests, new RbLooseBallTests, RbMultiTableTests, and (PIE) RbMatchFlowTest, RbReplayTest, RbM1FlowTest, RbM1RackTest, new RbLooseBallFunctionalTest, RbMultiTableFunctionalTest; content `/Game/Generated/Physics/**`, `/Game/Generated/Balls/**`; `Docs/images/dev/m2e/**` | architect step | 18.6: hand-off at the exact core state, rules untouched (hashes), floor bounce and rest, pick-up and automatic returns, replays hide, a two-table dev level with independent sessions, grep test "no single-table lookups", M1 flows green |
| **M2-0** | Architect: contracts, config, integration | `RawBreak.uproject`, `Source/*.Target.cs`, every `*.Build.cs`, `Config/DefaultEngine.ini` (except the audio block), `Config/DefaultGame.ini`, `Core/*` (except the `ERbTablePart` block), `RawBreak.h`, `RawBreakModule.cpp`, `Dev/RbHeadlessCaptureSubsystem.*`, `RbTestFlags.h`, `RbCoordsTests`, new `RbContractTests`, `RbPieSmokeTest`, the `RawBreakEditor` module, `RbAssetBake_Common.cpp`, `RbAssetBakeLibrary.h`, `Tools/unreal/rbue.py`, `editor/rb_common.py`, `rb_make_all.py`, `rb_pipeline_proof.py`, `rb_bake_selftest.py`, `Tools/blender/divebar/db_build_all.py`, `Docs/licenses/asset-ledger.csv`, `.gitignore`, `Docs/perf/**`, `Docs/images/dev/m20/**`, this document | — | 18.10 (18.12: what M2-0 delivered before the merges) |

**Ownership boundaries that are easy to get wrong**

* **M2-F / M2-D (settings).** M2-D owns the storage (`URbGameUserSettings`, `RbSettingsTypes.h`, `RbSettingsRegistry`), the menu
  rows, presets, validation and persistence. M2-F owns what the values *mean*: `RbAimResponse` (counts -> cm -> degrees, the
  acceleration curve), the look gate, the camera / motion scales. M2-F only **reads** `Controls`, `Camera`, `MouseDpi`,
  `CameraPreset`, `VerticalFovDeg`, `HeadBobScale`, `bReducedMotion` and re-reads them on `OnSettingsChanged()`; its tests write
  the structs directly. Field names, ranges and defaults of 18.4 are frozen for M2; a different default (e.g. after tuning P3) is
  a request to the architect, who changes `RbSettingsTypes.h` at the merge. The key hints (M2-D) read M2-F's public getters
  (`URbStrokeComponent::GetPhase`, `URbBallInHandComponent::GetState`) and add no code to M2-F's files. The pause menu changes the
  input mode through the controller's public API (`SetInputMode`, `SetShowMouseCursor`, `SetPause`) from M2-D's own code; the
  controller file stays M2-F's. M2-F guarantees that pausing while the Stroke button is held drops the stroke (no contact on
  resume).
* **M2-L / M2-B (table vs bar).** Everything that is the table is M2-L's: the playfield parts from `rb::TableGeometry` (C++, the
  only path allowed for physics surfaces), the body / cabinet (apron, legs, coin-op box, castings, coin mechanism, trap window,
  ball tray, cue-ball return), every table-family material (`M_Rb*`, `MI_Rb*`, incl. the dive-bar instances and the dirty balls)
  and the table's own wear (chalk dust, ball tracks, burns and rings that are part of the rail-cap material). M2-B makes every
  venue material (`M_DB_*`, `MI_DB_*`, `MPC_DB_Venue`), the decal materials and every prop that is not the table, **including the
  3-shade lamp body** (its lights are M2-A's) and the wall cue rack with its prop cues (the playable cue is M2-F's). Table
  materials never reference `MPC_DB_Venue` (M2-L carries its own Age scalar with the venue's 0.80), so neither waits for the other.
* **M2-A / M2-B (architecture vs props).** M2-A: shell and architecture (walls, floor, ceiling, soffit, columns, doors, openings,
  windows / glass block), neon tubes (a lighting element) and every light, fog and haze, the level, the importer, cameras,
  captures, the validator. M2-B: furniture and props. Contract: **asset ids and target dimensions come from venue-dive-bar 2.3 / 5 /
  13.3** (`SM_DB_<Family>_<Variant>`); each generator writes `Art/DiveBar/Export/<Asset>/<Asset>.json` (dimensions, pivot, material
  slots, collision hulls; no shared manifest file); the importer scans those files and places a mesh when it exists, else the
  greybox box of the element; **material slot names are the `MI_DB_*` names**, so the importer assigns M2-B's instances without a
  mapping table. Emissive calibration: M2-A measures and writes `Art/DiveBar/calibration.json` (`EmissiveScale`), M2-B's material
  generator reads it (1.0 when missing).
* **M2-A / M2-L (table in the level).** M2-A's generator places `ARbTable` with the layout values (preset, ball set, venue seed,
  `TableIndex`, lamp height / footprint, tag `RbPlayerTable`); M2-L's class loads its meshes. M2-A never builds table geometry.
* **M2-C / M2-E / M2-F (audio sources).** M2-C only listens to frozen delegates: playback (`OnPlaybackStarted`,
  `OnPlaybackClockChanged`, `OnShotEvent`, `OnFinished`; M2-E's file), footsteps (`OnFootstep`; M2-F fires it), loose balls
  (`OnImpact`, `OnRolling`; M2-E fires them), settings (`OnSettingsChanged`), the venue (`ARbVenueInfo`). No sound code in other
  packages' files; a missing hook is a request.
* **M2-E / M2-F (ball in hand after a pick-up).** A picked-up cue ball that is in hand goes into M2-F's carrying hand through
  `URbBallInHandComponent::BeginCarry` (public API); M2-E owns the loose actor, its removal and the ball set's visibility.
* **M2-D / M2-E (multi-table UI).** M2-D binds the overlay, key hints and pause info to `URbTableSubsystem::GetPlayerSession()`
  only; M2-E makes sure that session exists in every playable level.

**Waves and merge order.** File-wise all seven packages run in parallel. On the shared machine (one GPU, 32 GB): **wave 1** M2-F
(top priority), M2-L (P4), M2-E, M2-C; **wave 2** as soon as a slot frees: M2-A, M2-B, M2-D. Merge order (each after its
reviewer): M2-E, M2-F, M2-D, M2-C, M2-L, M2-A, M2-B; then the integration round (18.10): `rbbl.py all`, `rb_make_all.py --strict`,
captures, the M2 acceptance and the product owner's playtest. M2-F may be merged and handed to the owner for a feel-only playtest
in the test room before the rest lands.

### 18.3 M2-F — feel (P1, P2, P3, P5)

**P1 — calm view after the contact.** Cause (M1): once the stroke component left Down, `ARbPlayerCharacter::HandleLook` routed
every look delta to the head, so the follow-through of the mouse stroke (often with the Stroke button still held) turned the view
away from the balls. Design: `FRbLookIntentGate` between the Look action and the head. Mouse motion while Stroke is held is the
stroke, never look (also after the contact); after the release the gate stays closed for `QuietSeconds` 0.25 s, then opens only
when the mouse travels more than `DeadZoneCm` 1.5 cm within 0.40 s (a deliberate new move), fading in over 0.20 s; smaller motion
is dropped, not accumulated. The player stays down in Watching; the head follows the balls only through the human-motion
reactions (below), never through stroke input; right click stands up (a human StandUp). The gate works in centimetres of mouse
travel and input timestamps, never in per-frame deltas.

**P3 — aim at normal mouse speed.** Cause (M1): the Look action delivered counts x the engine's legacy `Mouse2D` axis scale 0.07
(with mouse smoothing) and the stroke turned that into azimuth with 0.0005 rad per unit -> 0.002 deg per count: 90 deg = 1.43 m of
travel at 800 DPI (the owner measured ~1.9 m). Design (`RbAimResponse`, pure): counts -> cm with `MouseDpi` -> degrees with
`FRbControlSettings`: coarse `AimDegreesPerCm` 7.2 x `AimSensitivity` (90 deg per 12.5 cm), fine x `FineAimFactor` 0.075 while Shift
is held (13x slower), optional acceleration (0 = linear; the curve is monotone, continuous, frame-rate independent, gain 1 at the
reference hand speed). The Look / aim path reads raw counts (no engine axis scale: `bEnableMouseSmoothing=False` and a neutral
`Mouse2D` axis config in `DefaultInput.ini`). Standing look: `LookDegreesPerCm` 22 x `LookSensitivity`, invert Y.

**P2 — diegetic ball in hand.** `URbBallInHandComponent` (on the pawn): Carrying = a hand (`SM_RbHand_Carry`, a procedural
stand-in from `rb_make_player.py` / `URbAssetBakeLibrary::BakeHandCarryMesh`) holds the ball `HoverHeightCm` 4 above the cloth
exactly over the target; the lamp shadow plus a contact-shadow preview under the ball show where it will touch; the target follows
the look point on the bed (analytic bed plane of the table frame), Shift = fine adjustment (the aim's fine factor); the hand
follows with human lag and tremor (never teleports). Confirm (LMB / F / Enter): Lowering (~0.25 s) onto exactly the previewed spot,
`PlaceCueBall` when the ball touches the cloth. Illegal spot (ball overlap, off the bed, outside the kitchen): the hand hesitates
and does not lower (state Refused), a soft knock (M2-C) and the mandatory line; a red outline only as a later assist option.

**P5 — the camera is the eyes.** `FRbHumanMotion` (pure maths, owned by the rig; realism plan 4.8) replaces the smoothstep dolly.
Posture changes (GetDown, StandUp, LeanOver, StraightenUp) as human movements: hip hinge (the head travels on an arc), weight shift
toward the bridge side, head rotation leading the translation by 80-150 ms, asymmetric timing (down slower with a long settle, up
faster at the start), a 3-15 mm overshoot below the final eye height and a damped settle (2-3 Hz, zeta ~0.6); every parameter varies
per movement from a **seed** (hash of match seed, shooter shot index, address index, change counter): never two identical
get-downs, bitwise reproducible. Continuous layer: breathing 0.2-0.33 Hz (faster / deeper with `StrokeSituation::Pressure`),
postural sway (standing ~4.5 mm, down ~1 mm), walking bob / sway with gaze stabilisation and `OnFootstep` at each foot contact, a
pressure-driven tremor share; Settle calms breathing and sway by ~70 % over 1-2 s. Reactions: after the contact a slow lean / turn
following the cue ball and the first object ball with ~150-200 ms latency (smooth pursuit, no jump); a 50-80 ms flinch of a few mm
on a loud impact (the break; loudness from the shot events). Presets: Eyes = vestibulo-ocular stabilisation (rotation from bob /
breathing removed, gaze kept on the target, continuous translation x 0.3; posture changes keep their full human path); Headcam =
nothing stabilised + mount jitter (`MountShakeScale`). Comfort: everything x `HeadBobScale` / `BodySwayScale`; Reduced motion or
`PostureTransition` Quick / Cut -> a short plain ease / a cut, no sway. The motion layer moves only the eye: the cue pose stays
`SampleHand` (what you see is what hits).

**Acceptance (input traces; `RawBreak.Unit.Feel.*`, `RawBreak.Unit.HumanMotion.*`, `RawBreak.Functional.Feel.*`):**

| # | Test | Pass |
|---|---|---|
| F1 | P1 trace: address, stroke 12 cm forward in 0.15 s with 3 cm lateral drift, Stroke held 0.3 s past the contact, release with 0.8 cm residual motion, 1 s watching | view yaw / pitch change caused by input after the contact = 0 (motion layer off) and < 0.3 deg in total with the human layer on (reactions only); the player stays down |
| F2 | P1 trace: after F1 a deliberate 4 cm move within 0.3 s | look resumes after the dead zone; no step > 0.5 deg between two frames (fade-in) |
| F3 | P3 trace: 12.5 cm of travel at 800 DPI (`MouseDpi` 800), and at 400 / 1600 DPI with `MouseDpi` set to match | 90 deg +- 0.5 deg in every case (DPI independent); the same counts in 30 / 60 / 144 fps frame splits give bitwise equal azimuths (linear) |
| F4 | P3 fine: the F3 trace with Shift | 90 x 0.075 deg (13.3x slower, inside the owner's 10-20x); acceleration curve monotone and continuous, gain 1 at the reference speed |
| F5 | P2 trace: carry across the bed, fine adjust, confirm | placed position == preview target within 0.1 mm; the ball never below HoverHeight while carrying; illegal target -> Refused, no `PlaceCueBall`; the hand never jumps (per-frame step bound) |
| F6 | P5 posture: 20 get-downs with different seeds, 2 with the same seed | durations 0.8-1.5 s; overshoot 3-15 mm, settled < 0.4 s later; head rotation reaches 50 % >= 80 ms before the translation; different seeds differ (max eye-path difference > 5 mm), equal seeds bitwise equal; stand-up reaches 50 % sooner than get-down; 30 / 60 / 144 fps paths within 0.1 mm |
| F7 | P5 continuous | breathing peak in 0.2-0.33 Hz, rate and depth rise with Pressure 0 -> 1; Settle -> amplitude x 0.3 +- 0.1 within 2 s; Headcam standing sway RMS 3-6 mm; walking 1.4 m/s: bob 3-5 cm p-p (Headcam), footsteps at ~2 Hz with `OnFootstep`; Eyes: view rotation from bob < 0.05 deg; Reduced motion: all zero |
| F8 | Functional (PIE, test room): get down, aim with the F3 trace, stroke, watch, stand up, ball in hand after a scratch; pause while the Stroke button is held | the whole loop with the new rig; no contact after resume; M1 stroke tests (bitwise frame-split test, SampleHand == ExecuteStroke) unchanged |
| F9 | Captures and plots | `Docs/images/dev/m2f/{ball_in_hand_legal,ball_in_hand_refused,down_after_contact}.png` (strict) and trace plots `getdown_seeds.png`, `aim_trace.png`, `look_gate.png` (from `Tools/feel/`), inspected |

The cue's environment sweep switches from `ECC_WorldDynamic` overlaps to the `RbCueSweep` channel (`Cue/RbCueClearance`, M2-F), so
venue clutter with `RbVenueProp` no longer blocks the cue (VDB-T3 needs it in-engine).

### 18.4 M2-D — menus and settings

Slate only (ui-ux 3.1), no UMG / widget assets; screens are `SCompoundWidget`s on the game viewport, laid out at 1080p and scaled
only by `URbDpiScalingRule` (height / 1080). `URbUiSubsystem` (local player) keeps the screen stack: **Title** (`L_Title`: RAW BREAK,
Play -> Test room / Dive bar x Practice / Hot-seat, Settings, Quit), **Pause** (Resume, Settings, Quit to title, Quit to desktop with
a confirm; pauses the world, the playback holds without a time jump), **Settings** (pages below). Input: menus UIOnly with a cursor;
closing the last screen restores GameOnly with the captured hidden mouse (the raw-input thread needs it). Esc =
`ARbPlayerController::HandlePause` -> `TogglePauseMenu` (open / back / resume). `ARbTitleGameMode` travels to `RbTypes::MapFor(venue)`
with `?Mode=`. Key hints: `SRbKeyHints` + `FRbKeyHintsModel` ("[RMB] Get down", "[F] Pick up the ball" ...), lower left, fading after
3 s without a context change, setting `bShowKeyHints`.

**Settings rows of M2** (registry ids; everything persists in `GameUserSettings.ini`; engine rows through the `UGameUserSettings`
setters, quality rows through `SetQualityOption`, never direct cvar writes):

| Page | Rows (default) |
|---|---|
| Graphics | `gfx.preset` Low / Medium / **High** / Epic / Cinematic / Custom ("Custom (based on ...)" after a row changes); the eleven `ERbQualityOption` rows (view distance, anti-aliasing, shadows, global illumination, reflections, post-process, textures, effects, foliage, shading, volumetric fog) 0..4; resolution scale 25-200 %; depth of field (on); motion blur 0-1 (1); film grain 0-1 (1) |
| Display | window mode (fullscreen / windowed fullscreen / windowed), resolution (15 s confirm-or-revert), frame cap (30, 60, 90, 120, 144, 165, 240, unlimited), v-sync |
| Camera | look **Eyes** / Headcam; vertical FOV 40-75 deg (50); body sway & breathing 0-1 (1); head bob 0-1 (1); mount shake 0-1 (1, Headcam only); posture transition **Natural** / Quick / Cut; reduced motion (off) |
| Controls | mouse DPI 200-6400 (800); aim speed `AimSensitivity` 0.1-5 (1; the row shows "90 deg per x cm"); fine-aim factor 0.03-0.5 (0.075); aim acceleration 0-1 (0); look sensitivity 0.1-5 (1); invert look Y (off); stroke sensitivity 0.5-2 (1); key hints (on) |
| Audio | master 1.0, music 0.7, table & balls 1.0, ambience 0.8, voices 1.0, interface 0.6 (0-1 each; M2-C maps them to dB) |

**Acceptance**: UX-T01 (every row: default / preset values, Set -> Get round trip, save -> reload of the ini; EN labels only in
M2), UX-T02 (presets incl. the High read-back of hit-lit reflections), UX-T05 (text >= 18 px at 1080p), UX-T07 (no clipping at
1280x800, 1920x1080, 3840x2160, EN), UX-T09 (every screen by keyboard only and by mouse only; one focused widget after every push /
pop; Back always returns; quitting needs a confirm), UX-T10 (pause mid-shot: no time jump, the director commits after the resumed
playback, `ResultHash` unchanged), UX-T20 (reduced motion sets and restores exactly its rows), UX-T25 (on M2-E's two-table dev
level: overlay, hints and pause show only the player's match), UX-T26 (DPI rule only, no double scale); functional MenuFlow
(`L_Title` -> Dive bar Hot-seat -> pause -> settings -> change the FOV -> resume -> quit to title); settings survive a process
restart; captures `Docs/images/dev/m2d/{title,pause,settings_graphics,settings_camera,settings_controls,settings_audio,key_hints}.png`
through `rbue.py capture --extra -RbUiScreen=<Screen>` (a dev switch of M2-D's subsystem), inspected. `GameDefaultMap` switches to
`L_Title` at the integration (architect) once `rb_make_title.py` exists.

### 18.5 M2-C — audio v1

Everything is synthesised in M2 (no library samples): the physics sounds come from the shot's events and tracks through the modal /
contact synthesis of `Tools/audio/click_synth.py` (`runtime_render`), ported to `RawBreakAudioDsp` (plain C++, unit-testable
offline: `FContactPulse` / Hertz + Tsuji, per-order ball radiation kernels, `FModalBank` for rails / bed / pockets / cue,
`FImpactRenderer`, noise sources). Sounds of M2: ball-ball (AU-20/21), cushion (AU-30), jaw (AU-31), rail cap (AU-32), slate landing
/ bounce (AU-24), liner and pocket drop (AU-34/35; coin-op gully run AU-36 and trap click AU-37 on the 7-ft), rolling / sliding on
cloth (AU-22/23), tip hit incl. miscue and recontact (AU-01..03), loose-ball floor hits and rolling (AU-25), footsteps (AU-65,
synthesised heel / toe per surface), room tone per venue (test room: HVAC bed; dive bar: HVAC, cooler compressors, neon hum as
synthesised layers), convolution reverb per venue (IR from `Tools/audio/ir_synth.py`; dive bar RT60 0.8 / 0.6 / 0.5 s low / mid /
high), spatialisation (point voices at the emitters, attenuation, panning / HRTF per audio.md 6.3).

**Scheduling contract** (the reason for the architect's playback hooks). One `FShotAudioClock` per table, shared by all its voices.
`URbTableAudioComponent` (one per `ARbTable`, created by `URbAudioSubsystem` for every table of `URbTableSubsystem`) binds to the
table's playback: `OnPlaybackStarted` -> plans built on a worker (events + tracks, listener geometry, variation seeded from the shot
hash so replays render identically) -> `PushPlan` + `StartShot(mapping)`; `OnPlaybackClockChanged` -> `SetMapping` (slow motion,
pause, seek, world pause). The first voice callback after `StartShot` anchors shot time in device frames; every voice renders its
events at fractional device frames (overlap-add). Voices are `USynthComponent`s with one `ISoundGenerator` callback per device block,
never virtualised or stolen; LOD tiers T0 (player's table or < 3 m: 30 voices, or the 8-channel fallback of audio.md 5.1 if AU-0
decides so), T1 (3-10 m: 4), T2 (> 10 m: 1). Settings volumes -> submixes on `OnSettingsChanged`; pause mix (world low-pass 800 Hz,
-12 dB), replay mix (ambience -10 dB). Assets (submixes, attenuation, concurrency, IRs) by `rb_make_audio.py` under
`/Game/Generated/Audio`. M2-C may change the audio block of `DefaultEngine.ini` (e.g. back to 1024 x 1 after AU-0) and asks the
architect for plugin switches in `RawBreak.uproject` if it needs any.

**Acceptance**: AU-0 spike documented in `Docs/audio/m2/au0.md`; offline `RawBreak.Unit.Audio.*`: AU-T01..T07, T09, T10, T11 (golden
vectors, residual <= -100 dB), T12 (event coverage on `divebar_break8.json`), T13 (replay determinism, bit-identical); engine
`RawBreak.Functional.Audio.*` (`rbue.py test --sound`): AU-T08 (0.5104 ms in two voices -> 24.50 +- 0.05 samples), AU-T21 (0 samples
inter-voice skew), T16 (no clipping, dive-bar break at the breaker's ears), T19 (audio thread <= 60 % of a block at the break's
peak); a recorded dive-bar break and a test-room break contain every class above (event log vs detected onsets), a slow-motion
replay follows the rate, pause holds and resumes without a click; spectrograms `Docs/images/dev/m2c/*.png` and the WAVs of the
recorded checks in `Docs/audio/m2/` (LFS), analysed with `click_synth.py --analyze` and inspected.

### 18.6 M2-E — balls off the table, multi-table groundwork

#### 18.6.1 Balls off the table

The core already decides everything (event `BallOffTable` with its `OffTableReason`, fouls, respot / ball in hand). The Unreal side
only continues the picture. On a **live** shot's `BallOffTable` event `URbLooseBallSubsystem::HandOff` hides the ball in its
`ARbBallSet` and spawns an `ARbLooseBall` at the exact core state of the event (position, velocity, spin -> `FRbCoords` -> world;
the ball's own mesh and material instance, radius, mass; profile `RbLooseBall`, physical material `PM_RbBall`, CCD). It bounces on
the floor (`PM_RbSurface_Vct` in the dive bar: friction 0.5, restitution 0.35, ESTIMATE; concrete in the test room), rolls under
stools (the venue hulls model the legs, venue-dive-bar 13.5) and knocks against furniture. `RestsOnRailOrFrame`: no physics, the
ball stays where the core froze it. The rules never wait: while a loose ball exists for a ball that the committed table state has
back in play, the table instance stays hidden ("awaiting return"). **Return (simple for M2):** a pick-up interaction ("Pick up the
ball", gazed within 1.2 m, a `URbInteractionSubsystem` provider), or automatically when the next address starts, when the ball
rests in an `RbBallReturn` volume, falls below kill Z or lies unreachable for 20 s; a picked-up cue ball that is in hand goes into
M2-F's carrying hand. Replays show the ball leaving and hide it at the hand-off time (no second loose actor). Events `OnImpact` /
`OnRolling` (audio AU-25) and `OnReturned`. Physical materials by `rb_make_physics.py`.
M2-E review additions (integration round): a loose ball that comes to lie on ANY table (a lamp rebound handed off above the bed, a
bounce off furniture onto a rail) is taken off after `OnTableReturnSeconds` 0.3 s (`ERbLooseBallReturn::OnTable`, appended to the
enum). **The cue ball:** while it lies loose, M2-F's hand does not take it at the start of the ball-in-hand placement
(`URbStrokeComponent::BeginCueBallPlacement`), Confirm with the empty hand first tries the gazed pick-up
(`ARbPlayerCharacter::HandleConfirm`) and never places a ball that is still on the floor; EVERY return of a loose cue ball while the
director waits for the placement (pick-up, unreachable, return volume, kill Z, on a table, manual) puts it into the player's
carrying hand (`URbLooseBallSubsystem::GiveCueBallToHand`), so the player is never left with nothing to place; a hand that already
carries the cue ball returns a loose copy as `Carried`.

#### 18.6.2 Several tables per level (how multi-table works)

Decision 2026-09-28: venues hold several tables, each with its own match (a pool hall ~8). Rules for every package from M2 on:

1. **Registry, no singletons.** `URbTableSubsystem` lists every `ARbTable` of the world sorted by `TableIndex` (unique per level,
   0..N-1; `ValidateTables` fails on duplicates, every level validator calls it). Code that needs a table asks for a specific one
   (`FindTable(index)`, `FindNearestTable(point)`, `FindBallSet(table)`) or for the **player's** table (tagged `RbPlayerTable`, else
   the lowest index). `TActorIterator<ARbTable>` / "the first table found" appears only inside `RbTableSubsystem.cpp` (M2-E's grep
   test enforces it; the M1 game mode's lookup moves there).
2. **Sessions.** One `FRbTableSession` (table, ball set, cue, director) per table that runs a match, registered by `ARbGameMode`. In
   M2 only the player's table plays; the others stay idle and rendered. `ARbGameMode::GetDirector()` / `GetTable()` /
   `GetBallSet()` / `GetCue()` remain as the player session's accessors (M1 code and tests keep working). V2: AI regulars get
   sessions with AI shooters (`SyntheticHand`) and independent directors; the simulation subsystem gets a FIFO of requests (one
   simulator per worker later) instead of refusing while busy; online later syncs only stroke inputs (the core re-simulates
   bitwise on every PC).
3. **Per-table state is keyed by `TableIndex`**: ball sets, playback, loose balls, table audio with its shot clock and LOD tier,
   replay history (the player's table only in M2), score slates later. The venue seed hashes the index
   (`MakeVenueTableCondition(seed, TableIndex, ...)`), so every table of a hall has its own slope and balls.
4. **The player's context** (overlay, key hints, pause info, replays, camera rig, stroke) binds to `GetPlayerSession()`; switching
   tables later is a rebind of that session (UX-T25).
5. **Test level**: `rb_dev_m2e.py` builds `/Game/Dev/M2E/L_TwoTables` (a 9-ft and a 7-ft table, different yaw, the 7-ft tagged
   `RbPlayerTable`), used by M2-E's functional test and by M2-C / M2-D for their multi-table checks.

**Acceptance (M2-E)**: `RawBreak.Unit.LooseBall.*` / `RawBreak.Functional.LooseBall`: a scripted jump shot that leaves the table
(reason Floor) -> the loose ball starts at the event state (position 0.1 mm, velocity 1e-6 relative), bounces and comes to rest on
the floor; the match's `ResultHash` and GameState equal a run with the loose-ball subsystem disabled; the table instance is hidden
while awaiting return; pick-up by interaction and each automatic return path; a replay hides at the hand-off time and spawns
nothing; RestsOnRailOrFrame spawns nothing. `RawBreak.Unit.MultiTable.*` / `Functional.MultiTable`: two tables -> two registered
sessions, player table by tag, `ValidateTables` catches a duplicate index, a shot on the player's table moves only its balls, the
grep test above; all M1 flows (MatchFlow, Replay, M1Flow, M1Rack) green; capture `Docs/images/dev/m2e/loose_ball_floor.png`.

### 18.7 M2-L — table and cloth look-dev (P4)

**Hard rule**: every physics-relevant surface (bed top, cushion nose line and profile, facings / jaws, pocket cut and drop, rail-cap
top height, sights) stays **exactly** on `rb::TableGeometry` (C++ `RbTableMeshBuilder`, the M1 tolerances: nose line 0.01 mm, bed
z = 0). Look-dev adds detail only where the physics does not look: rounded rail-cap and nose edges as real geometry (not shading
bands), a visible **rubber** strip between cushion cloth and rail where real tables show it, cloth wrapped around the nose with its
fold at the facings, pocket hardware (9-ft: leather pockets with nets or drop pockets with irons; 7-ft coin-op: castings and gully
openings with >= 2 mm clearance to jaws and capture volumes, automated fit check), apron and legs with real construction (9-ft:
veneered apron, legs with levelers; 7-ft: the HALVERSON Stallion 7 cabinet of venue-dive-bar 3.1: laminate box, trim bands,
pedestal legs, coin mechanism, trap window, ball tray, cue-ball return in the foot-end apron). New render parts are appended to
`ERbTablePart` by M2-L (with `ToString`). Non-physics body parts may come from Blender (`Tools/blender/table/**`, reading the
`rbsim --geometry` JSON in `Art/Tables/`, e.g. `rbsim --table 7ft-bar --balls oldbar --geometry --no-trajectories --no-states
--out Art/Tables/tablespec_seven_foot_bar.json`), imported by `rb_import_table.py` to `/Game/Generated/Tables/<Preset>/Body/` and
loaded by `ARbTable`; `db_build_all.py` runs the table generators with the venue ones.

**Materials** (`rb_make_materials.py` + `Shaders/Private/*.ush`, Substrate): worsted cloth for the 9-ft (tight weave, fibre-level
detail in a tiling micro-normal + a fuzz / sheen lobe at grazing angles, realistic albedo — cloth green about 0.05-0.10 linear,
checked against a known-albedo card in the capture — subtle unevenness, chalk dust around the head string and the pockets, faint
ball tracks); napped bar cloth for the 7-ft (`MI_RbCloth_BarGreen`: nap direction, pilling, worn lanes, stains); lacquered wood with
depth (clear coat over figured veneer from CC0 wood scans, `Art/Tables/cc0_inputs.json`); black laminate rail caps with burns /
rings for the bar table (`MI_RbRail_BlackLaminate`); rubber, leather, net, cast metal / ABS; dirty balls for the dive bar
(`MI_RbBall_DiveBar`, the oversized cue ball included). **References**: web reference photos are listed (URL + what to compare) in
`Docs/references/table-lookdev.md` and never committed as images unless their licence allows it; the owner's photos of his local
pool bar replace them when they arrive.

**Acceptance**: M1 table tests green + new `RawBreak.Unit.Table.*` (rail-cap edge radius >= 3 mm with >= 8 segments, nose-profile
sagitta < 0.05 mm, rubber strip inside the rail outline and never above the nose line, casting / jaw clearance >= 2 mm, baked
triangle counts == runtime); `rb_make_all.py --strict` regenerates both presets idempotently (A2 metrics); strict captures through
`ARbLookDevCamera`s (Eyes preset) with `capture_table.py`: `Docs/images/dev/m2l/{9ft,7ft}_{chin_on_cue,standing,pocket_closeup,
cushion_grazing,rail_closeup,overhead}.png` (the 7-ft in a neutral look-dev room `/Game/Dev/M2L/L_TableLookDev` until `L_DiveBar`
exists) and a side-by-side sheet against the reference list per iteration; iterate until table and cloth read as real at
chin-on-cue and standing (Claude's written verdict per view in the package report; the owner's verdict in the M2 playtest);
`Docs/images/m1/*` re-captured (A7).

### 18.8 M2-A / M2-B — The Low Bridge Tavern, slice v1 (DB-0..DB-3 without Meshy / Higgsfield)

**Pipeline** (venue-dive-bar 13): Blender 5.2 headless through `Tools/blender/rbbl.py` (`run <script>`; `all` = `db_build_all.py`,
every venue and table generator in order; `--factory-startup`, `--python-exit-code 1`, fails on `RBBL_FAIL` / a traceback, kills the
tree on timeout, never leaves Blender running). Generators are deterministic (`rb_bl.rng(asset, instance, seed)`) and export FBX +
`<Asset>.json` (target dimensions asserted: hero +-2 mm, others +-1 cm, VDB-T4; UV0 world scale, UV1 unique, `UCX_` hulls that are
leg-accurate for balls rolling under furniture) into `Art/DiveBar/Export/<Asset>/` (LFS). `rb_import_divebar.py` (M2-A) scans the
asset JSONs, imports with Interchange (materials off), sets Nanite (full fallback), collision from UCX, profiles `RbVenueBlock` /
`RbVenueProp`, material slots -> `MI_DB_<slot>`, and refuses an asset without a ledger row.

**M2-A**: `layout.json` / `lights.json` transcribed from venue-dive-bar 2.3 / 4.2; shell (`db_arch.py`); `rb_make_divebar.py` builds
`L_DiveBar` + lighting sublevels (Open, LightsUp; AfterHours as a stub state) with the imported mesh or the greybox box of every
element (always playable); `ARbTable` at (1375.9, 542.7, 0) cm, yaw 0 (SevenFootBar, OldBarOversizedCue, `TableIndex` 0, tag
`RbPlayerTable`, venue seed searched for the roll-off toward the jukebox, `LampUndersideHeight` 0.86 m, lamp footprint x [-0.650,
0.650] y [-0.210, 0.170] m); `ARbVenueInfo` (DiveBar, seed, Age 0.80); PlayerStart at the head end; World Settings `ARbGameMode`;
post-process (Eyes); height fog + local fog volumes with the haze rules of 4.1 / 4.7 (every light with volumetric scattering > 0
casts volumetric shadows; outside and enclosed lights scatter 0; neon proxies `SpecularScale` 0 and flux = tube flux / pi; tube
meshes out of Lumen GI while the proxy lights); the 3-shade lamp's bulbs (key light), neons (`db_neon.py`), practicals; floor
physical material, kick plates, the `RbBallReturn` volume behind the bar; audio anchor tags `RbAudio_<Anchor>` (venue-dive-bar 10);
capture cameras `RbCam_DB_V01..V12`, `RbCam_DB_TH1..TH7` and menu stations `RbCam_Menu_S0..S7` (all `ARbLookDevCamera`); the
validator `ARbVenueInfo::ValidateVenueLevel`; `capture_divebar.py` (12 s warm-up, EV report).

**M2-B**: bar counter (H08), back bar (H09), booths + tables (M02), stools (H10 procedural variants A / B / C, spectator stools),
ledges and column shelf, the 3-shade lamp body (H03), wall cue rack + prop cues + chalk cubes (H05 / H06), jukebox body (H12), dart
machine + board (M03), a first lathe-prop set (bottles, glasses, ashtray); masters `M_DB_Opaque`, `M_DB_Coated`, `M_DB_Glass`,
`M_DB_Emissive`, `M_DB_Floor` (baked 2K masks until the RVT path is proven), `M_DB_Decal` with the Age system (venue slider 0.80 in
`MPC_DB_Venue` + per-asset bias; HLSL in `Shaders/Private/Venue/RbVenueWear.ush`); every `MI_DB_*` of venue-dive-bar 6.3 incl. the
architecture surfaces (brick, VCT, ceiling tiles, paneling); CC0 inputs pinned by `fetch_cc0.py` (Poly Haven / ambientCG only); the
first decal set (rings, burns, scuffs, stains; atlases from procedural sources, no Higgsfield); text textures with fictional brands
only.

**Acceptance (the VDB tests that fit M2)**:

| Check | Owner | Pass |
|---|---|---|
| DB-0 | A, B | `SM_DB_AxisTest` bounds 100.0 +- 0.1 cm, arrow +X, pivot on the floor; the stool end to end (Blender -> UE); the ledger check fails on purpose once (negative test); `Docs/images/divebar/db0/{axis_test,stool}.png` |
| DB-1 / VDB-T3 | A | greybox level walkable (PIE: capsule r 0.25 m through the 1.04 m and 1.10 m gaps); `cue_sweep_check.py` on `layout.json` reproduces 2.5 (class shares +-0.5 points); in-engine sweeps at the 12 positions on the `RbCueSweep` channel (after M2-F's switch) blocked where 2.5 predicts, +-2 cm; V10 plan matches 2.4; `db1/{V10_plan,V01..V04}.png` |
| DB-2 / VDB-T1, T2, T8, T11 | A | lamp-only lux probe inside the 4.4 bands (min >= 90 lux; analytic + in-engine white card); adapted EV100 inside the 4.5 bands for Open and LightsUp (12 s warm-up); no sequence above 3 flashes / s; light-flag validator clean; `db2/{V01..V05,V08}.png` + `lux_report.txt` + `ev_report.txt` |
| VDB-T4, T7 | B (A re-checks on import) | every exported asset within its tolerance; every imported asset has a ledger row, no NC licence (the OCR pass waits for Higgsfield art in DB-5) |
| VDB-T10 | A | preset / ball set / actor transform / `LampUndersideHeight` 0.86 / `layout.json` bed height == `FRbTableContext::BedHeight()` 0.743; roll-off within +-25 deg of the jukebox (two-roll behaviour test) |
| VDB-T12 (M2 part) | A | the lighting sublevels switch with ramps >= 0.8 s (no one-frame step); `RbCam_Menu_S0..S7` exist with the UX 6.3 poses (phone anchor, Polaroid wall, TV-1 input, beer clock: later) |
| DB-3 pre-check | A, B, L | "is it real" views `db3/{V02,V03,V04,V06,V07}.png` with M2-B's props and M2-L's cabinet, reviewed by Claude against reference photos (written notes); no Meshy / Higgsfield item required |
| Playable | A | `RawBreak.Functional.DiveBarRack`: a complete 9-ball practice rack on `L_DiveBar` through the cheats (like M1Rack) with the oversized cue ball, a hot-seat turn change, ball in hand after a scratch, a replay; `capture_divebar.py --set m2` renders every V / TH camera |

### 18.9 Config, pipeline and content additions (architect)

* `DefaultEngine.ini`: see 18.1. `DefaultGame.ini` (at integration): `MapsToCook` + `L_DiveBar`, `L_Title`; `GameDefaultMap` ->
  `L_Title` once it exists. `DefaultInput.ini` is M2-F's (mouse smoothing / axis config), `DefaultScalability.ini` M2-D's.
* `rbue.py`: `test --sound` (keeps the audio device for `RawBreak.Functional.Audio.*`), `capture` / `test --extra <UE args>`; M2-0
  (18.12): `capture --camera A,B,... --out <dir>/{camera}.png [--show-ui] [--hide-tags T] [--exe <packaged exe>]`, `perf`, `package`,
  `ledger`, `core`, `owners` (the 18.2 ownership check of a package branch), `unity` (names two `.cpp` of a module both define:
  a build break once a merge puts them into one unity blob; fourth review), `selftest`.
* `rb_make_all.py` order: materials (M2-L) -> physics (M2-E) -> table playfield bake + table-body import (M2-L) -> ball (M2-E) -> cue (M2-F) -> player (M2-F) -> audio
  (M2-C) -> test room (M2-L) -> venue materials (M2-B) -> dive-bar import + FX (`rb_make_divebar_fx.py`) + level (M2-A) -> title
  (M2-D); validators: M1 level, venue level. Blender first: `python Tools/blender/rbbl.py all` (`db_build_all.py`: M2-A shell /
  neon, M2-L `table/tb_build_all.py`, M2-B props, signs and decals; missing generators are skipped, `--strict` fails on them).
  Packages never edit these lists; a new generator is a request (`rbue.py selftest` fails while a generator that a package owns
  in the 18.2 table is missing from them).
* Content roots per package: 18.2. Scratch maps `/Game/Dev/M2<x>/` (git-ignored). `Art/Third/` (raw CC0 downloads) is git-ignored;
  the lock files with the SHA-256 pins are committed.
* Licence ledger: per-package fragments `Docs/licenses/ledger/M2-<x>.csv` (the columns of `asset-ledger.csv`), merged by the
  architect; `ai_generated` = n for everything in M2.

### 18.10 M2 acceptance (integration round)

| # | Check | How |
|---|---|---|
| M2-A1 | Build clean | editor and game target: 0 errors, 0 project warnings (`rbue.py build`, `rbue.py build --target RawBreak`) |
| M2-A2 | Regenerate | `rbbl.py all -- --strict` + `rb_make_all.py --strict` from a clean `Content/Generated` recreate everything; a second run with `--compare` gives equal metrics; validators OK for `L_M1_TestRoom` and `L_DiveBar` |
| M2-A3 | Tests | all `RawBreak.Unit.*` and `RawBreak.Functional.*` green (M1's 163 + M2-0's 11 unit tests and one `Functional.LevelSmoke.<Map>` per playable level + the packages'), core green (`rbue.py core`: 904 on `m20`; **938** on `integ/m2`, because `main` merged WP-12's rb::ai tests after the M2 plan); `RawBreak.Functional.Audio.*` with `--sound`; `rbue.py ledger --check` clean |
| M2-A4 | Feel | 18.3 F1-F9 green; the owner re-tests P1, P2, P3, P5 |
| M2-A5 | Look | 18.7 captures (both tables) + `Docs/images/divebar/m2/{V01..V09,TH1..TH7}.png`, strict, inspected; the owner re-tests P4 |
| M2-A6 | Venue | the 18.8 table |
| M2-A7 | Audio | the 18.5 list; the owner hears a full dive-bar rack |
| M2-A8 | Menus | the 18.4 list; the owner plays from the title screen into both venues and back |
| M2-A9 | Performance (logged, no gate yet) | `L_DiveBar`, High, 1440p, TSR at the DLSS-Q internal resolution: GPU mean / P95 logged against the 10.3 / 11.1 ms target; game thread < 6 ms in a break; P95 <= 16.7 ms is the floor that must hold. `rbue.py perf --exe <build>/Windows/RawBreak.exe --map "/Game/Generated/Maps/L_DiveBar?Mode=Practice" --perf-exec "rb.Match.Break 9" --out Docs/perf/m2/divebar_1440p.json` (defaults: 2560x1440, `rb.Quality High`, `r.ScreenPercentage 66.67`, 600 frames) |
| M2-A10 | **Owner playtest** | packaged Development build (`rbue.py package --label M2`): title -> dive bar hot-seat and test-room practice; P1-P5 verdicts recorded in `Docs/playtests/` |

### 18.11 Risks

| Risk | Mitigation |
|---|---|
| `r.VirtualTextures=True` recompiles every shader (cold DDC) | switched on once in the architect step; the first capture per worktree needs a long timeout; warm the DDC once after the merge |
| Seven packages on one machine (RAM, GPU, shader compiles) | the waves of 18.2, at most three capturing packages, `rbue.py` sequential per worktree, never leave an editor / Blender running |
| P5 motion causes discomfort | Eyes preset stabilised, every scale in the settings, Reduced motion = Quick / Cut, the owner's playtest sets the defaults |
| Human motion vs "what you see is what hits" | the motion layer moves only the eye; the cue pose stays `SampleHand` (M1 tests stay green) |
| Audio anchor precision in the editor binary / on the null device | AU-0 spike first; fallback 1024 x 1 buffers; engine tests on the null device where no output exists |
| Blender -> UE axis / scale | DB-0 axis test frozen before any prop; exporter and importer both assert dimensions |
| Table look without the owner's photos | web references first (URLs only); the owner's photos replace them in a later round |
| Loose balls vs replays / determinism | loose balls are presentation only; hashes compared with the subsystem disabled; replays never spawn them |
| M2-F is the largest package | it may run as one agent with a reviewer per sub-feature; P1 / P3 land first (smallest, highest value), then P2, then P5 |

### 18.12 M2-0 — what the architect package delivered before the merges, and the integration runbook

M2-0 is the architect's own package: the contracts, config and stubs of 18.1 (merged with the plan), plus the tooling and tests the
integration round (18.10) needs, delivered on branch `m20` **before** the package merges so that every M2-Ax check is one command.
All of it lives in M2-0's files (18.2).

**Delivered**

| Area | What | Where |
|---|---|---|
| Captures (VDB-T2, DB-3, M2-A5) | several cameras in ONE process (`--camera A,B,C`, output pattern with `{camera}`, else `<stem>_<camera>.png`); every view starts with a camera cut (eye adaptation snaps, no inherited exposure), waits for the compilers, gets a second cut when its warm-up starts (the snap then sees the final shaders, not a cold DDC's default materials) and warms up on its own; the view's camera is looked up until found and then cached (no per-frame actor search inside a perf recording); `--show-ui` (screenshot with the viewport's Slate UI: overlay, menus, key hints); `--hide-tags T,...` (e.g. `RbDB_Ceiling` for the V10 plan view; it hides the **whole actor**, light components included, so an actor that carries a light that must stay on in the plan view must not carry the tag); the adapted **EV100 of every view** in the log (`RbCapture: EV100 <camera> <value>`, read back from the eye adaptation through a scene-view extension, the rig's formula) and echoed by `rbue.py`; a named camera that does not exist (or, for the player view, a missing player camera manager) **fails** the run after 60 grace frames (no silent player-view fallback, no wait for the global timeout), and a view that is not ready never advances its stage (after a failure nothing restarts); `--exe` renders through a packaged build. The process exits with 0 also after a failure (UE 5.8 drops the status of a graceful exit): callers detect failures by the `RbCapture: FAILED` line and missing outputs, as `rbue.py` does | `Dev/RbHeadlessCaptureSubsystem.*`, `rbue.py capture` |
| Frame-time log (M2-A9) | `-RBPerf` recorder: after the warm-up it runs `-RBPerfExec` (e.g. `rb.Match.Break 9`), records N frames of frame / game / render / RHI thread (`stat unit` values) and GPU time, writes a JSON report (mean / median / p95 / p99 / min / max, hitch counts, raw samples, and the `sg.*` scalability groups in effect at the end of the recording - what was really rendered, not only what `--quality` asked for); `rbue.py perf` adds the host block (GPU utilisation **before and after** the run, see below), prints the verdict against the M2-A9 targets (GPU mean 10.3 / p95 11.1 ms, game thread 6 ms, frame p95 16.7 ms floor) and fails with `--gate` only | `rbue.py perf`, `Docs/perf/m2/` |
| Packaging (M2-A10) | `rbue.py package [--label M2]`: a cook-list preflight first (every generated map in `MapsToCook` - the playable ones **and every other `.umap` under `Content/Generated/Maps`, e.g. the dive bar's sublevels**: `DirectoriesToAlwaysCook=/Game/Generated` cooks only `.uasset` files, never a `.umap` (UE 5.8 `CookOnTheFlyServer`), so a sublevel that is streamed in by name would be missing from the build; every listed map generated, `GameDefaultMap` exists and is `L_Title` once the title is generated), then the editor and the game target through UBT (`-WaitMutex`; BuildCookRun's own build step fails at once with `ConflictingInstance` while another agent's UBT runs; the editor target because the cook runs `UnrealEditor-Cmd` with `-nocompileeditor`), then `BuildCookRun -cook -stage -pak -archive` into `<RB_BUILDS_DIR or ../RawBreak_Builds>/<label>/Windows/RawBreak.exe` next to the **main** checkout (never inside an agent worktree); fails on cook errors | `rbue.py package` |
| Licence ledger (VDB-T7, M2-A3) | `rbue.py ledger [--check] [--merge] [--allow-ai]`: validates `Docs/licenses/asset-ledger.csv` and every fragment `Docs/licenses/ledger/M2-<x>.csv` (shared columns first and never renamed; sources and licences of venue-dive-bar 13.9; a source's only licence, e.g. polyhaven / ambientcg = CC0-1.0; NC / ND licences never; `ai_generated` y/n and none in M2; Meshy / Higgsfield rows need ai = y + the Steam disclosure; ISO date; sha256; trademark_check n/a / pending / cleared; no duplicate asset_id + source; fragments that disagree); `--merge` appends the unmerged fragment rows (architect only), `--check` fails while any is unmerged | `rbue.py ledger` |
| Core tests | `rbue.py core [--slow] [--config Debug] [-- filters]`: CMake configure (VS 2022, Release by default) + build, then the test binary itself (the `TestMain.cpp` filters; every `[FAIL]` and failed check echoed, full output in `Saved/RbLogs/core-*.log`, its own "N test(s) run" count). The default run excludes the opt-in `_Slow_` tests (Docs/architecture.md 18: PERF / CPU-time gates / statistical sets, nightly in Release on an idle machine), also when filters are given (`core -- MOT_` = `MOT_ -_Slow_`); `--slow` includes them | `rbue.py core` |
| **Ownership check** | `rbue.py owners --package M2-<x> --branch <branch>`: every file the branch changed since its merge base with `main` must belong to the package (the 18.2 table encoded in `rbue.py` `OWNERS`; the most specific pattern wins, an exact path beats any glob); the shared-file blocks are checked by content: an edit inside the audio block of `DefaultEngine.ini` belongs to M2-C, inside the `ERbTablePart` enum / `ToString(ERbTablePart)` to M2-L, everything else in those files to M2-0. Without `--branch` it checks the working tree incl. uncommitted and untracked files (a package can run it before its commit); `--who <path>` names a path's owner. Anything reported is a request, never a silent edit | `rbue.py owners` |
| Test runs (M2-A3) | `rbue.py test`: green only when nothing failed **and** every test the filter found (`Found N automation tests`) completed **and** the automation shut down normally (`TEST COMPLETE. EXIT CODE: 0`); an editor that crashes or hangs after N green tests is a failure, not "N passed, 0 failed". `--sound` keeps the audio device and mutes its final output (`-MuteAudio`: the mixer still renders every block; `--audible` for a human listener); failed `RawBreak.Functional.Audio.*` tests without `--sound` print the hint to re-run with it; a run that changed the working tree (`git status` before / after: a test writing into the repository, e.g. M2-C's master renders into `Docs/audio/m2/`) prints a warning per file (fourth review) | `rbue.py test` |
| Self-test | `rbue.py selftest`: the host helpers (capture names, perf verdict + report layout, the cook-list preflight in 8 states incl. a sublevel, the automation run verdict in 8 cases, `--sound` muting, the core test filters, the process runner - exit code and output, a child that hangs **without printing** killed at the timeout -, the build summary's compiler-warning count, the unity-hazard parser and blob packing in 9 cases (helpers in anonymous vs file-named namespaces, overloads, comments / strings / member definitions / declarations, console variables + log categories + test classes, one blob below 2 x 384 KB, the split, an oversized file), ledger incl. 13 negative cases, 41 ownership cases, no tie between two packages over every tracked file, the three shared-file blocks, every content generator a package owns run by `rb_make_all.py` / `db_build_all.py` and every listed generator owned) | `rbue.py selftest` |
| Process runner | every Unreal / UBT / UAT / CMake step of `rbue.py` waits for the **process** with the `--timeout` (a reader thread collects the output) and kills the whole process tree when it expires (exit 124) - also when the process hangs silently: the earlier check ran only when a log line arrived, so a deadlocked editor or a PIE test waiting forever blocked `rbue.py` for good and left the Unreal process running on the shared machine. `rbue.py build` also counts the compiler warnings in its summary line (M2-A1: 0 from project code; an incremental build only reports the files it recompiled) | `rbue.py` `_run`, `cmd_build` |
| **Unity-build hazards** (fourth review) | `rbue.py unity [--module M] [--strict]`: UBT compiles `RawBreak` (>= 32 files incl. the UHT `.gen.cpp`) in unity blobs - the `.gen.cpp` first (4 KB each), then the `.cpp` sorted by path, 384 KB per blob (`Unity.cs`; the check reproduces the blobs of the merged tree exactly). Two files that define the same name in the same namespace (an anonymous-namespace helper, a `static` function, a namespace-scope variable or console variable, a type, a test / latent-command class, a log category; functions only with the same parameter types, overloads are fine) build on their package's branch and break the build once a merge moves them into one blob. The check lists every pair with both owners: **COLLIDES NOW** (same blob in the committed layout: the build fails, exit 1) or **latent** (different blobs today; `--strict` fails on them too); modules below 32 files (`RawBreakEditor`, `RawBreakAudioDsp`) report latent hazards for the day they grow, `bUseUnity = false` modules (`BilliardsCore`) are skipped. Adaptive unity takes git-modified files out of their blobs, so a package's own incremental build hides exactly the collisions of the files it edits; the check sees the committed layout. Every finding is a request to the files' owners: helpers of a `.cpp` into a namespace named after the file | `rbue.py unity` |
| Regeneration lists | `rb_make_all.py`: the 18.9 order incl. M2-A's `rb_make_divebar_fx.py` (dust motes, glass-block light function; `rb_make_divebar.py` only places them when they exist, so a clean regeneration without it silently lost them), then the **test dev levels** (`TEST_DEV_LEVELS`: M2-E's `rb_dev_m2e.py` -> git-ignored `/Game/Dev/M2E/L_TwoTables`, which `RawBreak.Functional.MultiTable` / `LooseBall` open; a fresh tree failed both with "... missing"); `db_build_all.py` incl. M2-B's `db_signs.py` | `rb_make_all.py`, `db_build_all.py` |
| Contract tests | `RawBreak.Unit.Contracts.*` (7): collision channels / profiles (incl. the Pawn ignoring both new channels), physical surfaces, render config (Substrate, virtual textures, auto-exposure range, no static lighting, 1 cm near plane, `RawBreakAudioDsp` loaded), UI DPI rule, venues / maps / capture tags / shared tags, packaging (every `MapsToCook` entry is parsed to its package and must exist - a typo fails the test, not only the cook; the test room listed; every generated map - playable or a sublevel under `Content/Generated/Maps` - that is listed is fine, one that is not listed yet is a test warning, see the runbook; `GameDefaultMap` exists, and a generated title that is not the default map yet is a test warning). `RawBreak.Unit.Pipeline.*` (4): capture lists, output names, perf statistics, the four cook-list states of a map + the `MapsToCook` entry parser + the generated-map scan | `Tests/RbContractTests.cpp` |
| Level smoke | `RawBreak.Functional.LevelSmoke.<Map>`: one PIE test per playable level that exists (the title + every `ERbVenue` map): a venue starts `ARbGameMode` from its World Settings, the player possesses an `ARbPlayerCharacter`, the tables validate (unique `TableIndex`), the match runs on `GetPlayerTable()` with ball set and cue, and once sessions exist (M2-E) the player's session plays the player's table with the match director; the title starts `ARbTitleGameMode`. A package's new level is covered at its merge without touching the test | `RawBreakEditor/Private/Tests/RbPieSmokeTest.cpp` |
| Pipeline proof level | a canopy tagged `RbDB_Ceiling` over the shapes and a top camera `ProofCamTop` (proves `--hide-tags` and multi-view on engine content only) | `rb_pipeline_proof.py` |

**Results on `m20` (2026-09-29)**

| Check | Result |
|---|---|
| Build (M2-A1) | editor and game target 0 errors, 0 warnings from project code (again in the third review, 2026-10-04: "0 compiler warning(s)" in both summary lines). Found on the way: `RbContractTests.cpp` and `RbSettingsTests.cpp` both had an anonymous-namespace `CVarInt`, which only collides once both files are committed and land in the same unity file (adaptive unity builds exclude the files being edited, so a package's own build stays green): project test helpers go into a namespace named after their file |
| UE tests (M2-A3) | **175 / 175** green (after the review, `m20-reviewed`; again after the second review on 2026-09-30, with the stricter run verdict: 175 found, 175 completed, `TEST COMPLETE. EXIT CODE: 0`; also 175 / 175 with `--sound`, i.e. a muted audio device, 142 s instead of 71 s on the loaded machine; again 175 / 175 in the third review on 2026-10-04 through the new process runner, and in the fourth review after its editor rebuild, 113 s, no working-tree change reported): M1's 163 + `Contracts.*` 7 + `Pipeline.*` 4 + `LevelSmoke.L_M1_TestRoom` (the other two levels join when M2-A / M2-D generate them). The packaging contract was also run against a simulated M2-A state (copies standing in for `L_DiveBar` + `L_DiveBar_Light_Open`): two test warnings while unlisted, green once listed, a failure for a mistyped `MapsToCook` entry (`L_Dive_Bar`); `rbue.py package`'s preflight agreed in all three states. `RawBreak.Functional.M1Rack` (M2-E's) failed once under load in the review ("shot 7: played back in real time over several frames (1)": one frame longer than the shot at playback rate 4) and passed on the re-run: a load-dependent check to watch in the integration round |
| Core (M2-A3) | `rbue.py core` (Release, default run without the opt-in `_Slow_` tests): **904 / 904** green, 114 s (150 s in the second review's re-run under load). For information: with `--slow` (937 tests) 13 of the 33 `_Slow_` tests fail on this shared machine: the wall-clock budgets (PERF-02 / 03 / 05, A-PERF-1, A-CUSH-2, A-E2E-18, HF-B02) under ~90 % CPU load from the other agents, and the statistical / validation sets BB-09, BRK-02, POCK-01..03, SYS-01..04, HF-B12 (not load-dependent; the M1 figure "904 green" is this default run; to be looked at by the core's owner); BilliardsCore is untouched by M2 (no diff since the plan commit), so this is the nightly `_Slow_` state of the core, a note for the core's owner, not an M2 regression |
| Regeneration (M2-A2) | `rb_make_all.py` ran the 13 generators of the M2 order (the package scripts still stubs on this branch) twice: validator OK for `L_M1_TestRoom`, second run "A2: asset metrics identical"; `rbbl.py all` runs `db_build_all.py` (every M2 generator skipped as not present yet) |
| Captures | `Docs/images/dev/m20/`: `proof_{ProofCam,ProofCamTop}.png` and `proof_hidden_*` (the canopy hidden by `--hide-tags RbDB_Ceiling`: its shadow gone, the top view sees the shapes); `multiview_RbCam_{Overhead,BallCloseUp,RoomOverview,ChinOnCue}.png` (four look-dev views of the test room in one process, 35.5 s; EV100 8.42 / 8.52 / 7.63 / 8.43); `show_ui_overlay.png` (player view with the info overlay through `--show-ui`); `packaged_RbCam_{RoomOverview,ChinOnCue}.png` from the cooked build (`--exe`), identical in look to the editor frames (EV100 7.63 / 8.43 in both). Negative: `--camera NoSuchCam` -> `RbCapture: FAILED no CameraActor ...`, exit 1 |
| Packaging (M2-A10 tooling) | `rbue.py package --label M2-0` -> cooked Development build, BUILD SUCCESSFUL (544 assets in the registry), starts headless and renders the same frame as the editor |
| Perf log (M2-A9 tooling) | test room, 2560x1440, High, `r.ScreenPercentage 66.67`, `rb.Match.Break 9`, 600 frames: editor `-game` GPU mean 21.3 / p95 22.4 ms, game thread mean 1.50 / max 3.23 ms; packaged GPU mean 23.8 / p95 24.9 ms, game thread mean 1.09 / max 3.38 ms (`Docs/perf/m2/m20_testroom_1440p_{editor,packaged}.json`). **Not a baseline**: this machine's GPU sits at 46-70 % utilisation even without any Unreal process of this package (measured 2026-09-29 16:1x-16:3x: the owner's desktop apps - animated wallpaper, screen recorder, an Android emulator - and the other agents' editors), so these GPU times are upper bounds. `rbue.py perf` now samples the GPU utilisation before and after the run into the report (`host.gpu_busy_before_percent`, `host.gpu_busy_after_percent`) and warns above 10 %; **the M2-A9 log is taken on an idle GPU** (those apps closed, no other agent rendering). The same editor recording repeated later with the new host block (`m20_testroom_1440p_editor_gpubusy.json`: GPU 46 % busy before the start, other agents rendering) shows what contention does: median GPU 23.8 ms as before, but p95 112 ms, and 80 frames over 33 ms (200-450 ms each) in the first ~13.6 s (frames 0-97 of 600). A review run through `RbCam_RoomOverview` at **1920x1080** (GPU 19 % busy before, 36 % after) measured GPU mean 22.4 ms, as much as at 2560x1440 with 1.8x the pixels: on this shared GPU the timestamps are dominated by the other processes' work, so the ~21 ms say nothing yet about the test room's own cost or the 10.3 ms target |
| Ledger | merged ledger empty and valid, `rbue.py ledger --check` clean on `m20` |
| Ownership of the running packages | `rbue.py owners` on the current package branches (`m2a`, `m2b`, `m2c`, `m2d`, `m2e`, `m2f`, `m2l`, work in progress): every branch touches only its own files (third review, 2026-10-04: again on all ten package / reviewed branches, after M2-B's new `db_signs.py` got its owner; no file of any package branch is unowned or tied) |
| Integration dry run (third review, 2026-10-04) | a scratch tree = `main` (incl. WP-12) + `m20-reviewed` + `m2e-reviewed` + `m2f-reviewed` + `m2d-reviewed` (the runbook order; every merge clean), then M2-D's switch of step 4 (`GameDefaultMap` -> `L_Title`, `L_Title` in `MapsToCook`): editor target 0 errors / 0 warnings (WP-12's `rb/Ai` compiles in the UE `BilliardsCore` module without a warning); `rbue.py test --filter RawBreak.` first **219 / 221**: `Functional.MultiTable` and `Functional.LooseBall` failed with "/Game/Dev/M2E/L_TwoTables missing" (M2-E's git-ignored test level: now built by `rb_make_all.py`'s `TEST_DEV_LEVELS`, runbook step 4), and `Contracts.Packaging` warned twice before the switch, as designed; after `rb_dev_m2e.py` and the switch **221 / 221**, no contract warning, `LevelSmoke.L_Title` (`ARbTitleGameMode`) and `LevelSmoke.L_M1_TestRoom` with M2-E's sessions (player session = player table + match director) green; `cook_list_problems()` = `[]`; `rbue.py core` **938 / 938** (316 s under load); `rbue.py perf` on the test room with M2-D's settings merged: `r.ScreenPercentage 66.67` held, `rb.Match.Break 9` ran (GPU mean 16.0 / p95 17.2 ms at 26-35 % foreign GPU load: not a baseline; a run on `m20-reviewed` shows the report's new `sg.*` block: `rb.Quality High` = 2 in all eleven groups, resolution 66.66); `rb_make_all.py` (this review's version) after deleting `/Game/Dev/M2E`: the 13 present generators + the test dev level `rb_dev_m2e.py` (L_TwoTables rebuilt), M1 validator OK, 160 s; `rb_make_divebar_fx.py` reported "not present yet" until M2-A is merged |
| **Integration dry run of all seven packages** (fourth review, 2026-10-04) | merge commits only (`git merge-tree` + `commit-tree`, no branch): `main` + `m20-reviewed` + `m2e-reviewed` + `m2f-reviewed` + `m2d-reviewed` + `m2c-reviewed` + `m2l-reviewed` + `m2a-reviewed` + `m2b` (no `m2b-reviewed` yet), the runbook order, every merge textually clean; `rbue.py owners` OK on every package branch; the fragments of M2-A (25 rows), M2-B (82) and M2-L (3) validate together; no binary outside LFS. Step-4 switches applied in the scratch tree (`GameDefaultMap` -> `L_Title`; `L_Title`, `L_DiveBar`, `L_DiveBar_Geo`, `L_DiveBar_Light_{Open,LightsUp,AfterHours}` in `MapsToCook`): `cook_list_problems()` = `[]`. **Editor target: 2 errors** (C2374 / C2084 in `Module.RawBreak.2.cpp`): M2-F's `Camera/RbHumanMotion.cpp` and `Player/RbBallInHandComponent.cpp` both define `kInternalStep` and `SmoothStep(double)` in an anonymous namespace; on `m2f-reviewed` they sit in different unity blobs (M2-F's build and review were green), the M2-C merge (its audio sources sort before `Camera/`) moves them into one -> the new `rbue.py unity` (reproduces UBT's six blobs of the merged tree file for file, and the four of `m2e-reviewed`; `--rev` on every merge step shows the hazard latent from the M2-F merge and colliding from the M2-C merge) and a request to M2-F (below). With a scratch rename in `RbBallInHandComponent.cpp` (not committed, M2-F's file): editor target 0 errors / 0 warnings, game target (`--target RawBreak`, all six blobs) 0 / 0; `rb_dev_m2e.py` rebuilt `L_TwoTables`; `rbue.py test --filter RawBreak. --sound`: **275 / 278** (278 found and completed, 256 s), M2-0's 15 green (`Contracts.*` incl. `Packaging` with the switched lists, `Pipeline.*`, `PieSmoke`, `LevelSmoke.L_DiveBar` / `L_M1_TestRoom` / `L_Title` with the player session on the player table); the three failures are cross-package and in other packages' files (requests below): `Functional.DiveBarRack` (M2-A), `Unit.MultiTable.NoSingleTableLookups` (M2-E's grep vs M2-A's test), `Functional.PauseMidShot` (M2-D's test vs M2-C's pause mix) |

**Integration runbook (the architect, after the reviewers; merge order of 18.2: M2-E, M2-F, M2-D, M2-C, M2-L, M2-A, M2-B)**

Per package, on `integ/m2` (= `main` + **`m20-reviewed`** first: `m20` lacks every review fix of this package):

1. `python Tools/unreal/rbue.py owners --package M2-<x> --branch <reviewed branch>` must be OK; a reported file is either moved to
   its owner as a request or accepted by the architect, never merged silently.
2. `git merge --no-ff <reviewed branch>`; apply the package's requests to M2-0 / other packages' files (18.2 "a new generator is a
   request": `rb_make_all.py` / `db_build_all.py` lists, `DefaultGame.ini`, `RbSettingsTypes.h` defaults, ...).
3. `rbue.py unity` first (a second, no build: no **COLLIDES NOW**; a collision is a request to the files' owners, applied by the
   architect at the merge as a rename / file-named namespace; latent pairs are requests too, the next merge may move them into one
   blob), then `rbue.py build` and `rbue.py build --target RawBreak` (0 / 0), `rbue.py test --filter RawBreak.` (all green; **from the M2-C
   merge on** `rbue.py test --filter RawBreak. --sound`: M2-C's `RawBreak.Functional.Audio.*` fail by design without an audio
   device, and `--sound` mutes the output, so the whole suite runs in one muted process; afterwards `git restore Docs/audio/m2`
   until M2-C's request below is in), `rbue.py ledger` (fragments valid;
   `--merge` once the package is in), `rbue.py selftest`, `rbue.py package`'s preflight (`python -c "import sys;
   sys.path.insert(0, 'Tools/unreal'); import rbue; print(rbue.cook_list_problems())"` prints `[]`).
4. Package-specific switches at its merge: **M2-E** - LevelSmoke then requires a player session in every venue (the check is
   already conditional on registered sessions; M2-E makes them exist); before the suite, build M2-E's git-ignored test level once
   per integration tree: `rbue.py py Tools/unreal/editor/rb_dev_m2e.py` (`/Game/Dev/M2E/L_TwoTables`; without it
   `RawBreak.Functional.MultiTable` and `LooseBall` fail with "... missing"; `rb_make_all.py` rebuilds it from now on,
   `TEST_DEV_LEVELS`); **M2-D** - `GameDefaultMap` (`DefaultEngine.ini`) -> `L_Title` and `L_Title` into
   `MapsToCook` (until the generated map is listed and is the default map, `Contracts.Packaging` raises test warnings - printed by `rbue.py test` - and `rbue.py package` refuses to cook; the package's own branch stays green because only the architect edits `DefaultGame.ini` / `DefaultEngine.ini`); **M2-A** - `L_DiveBar` and **every** sublevel
   into `MapsToCook` (on `m2a`: `L_DiveBar_Geo`, `L_DiveBar_Light_Open`, `L_DiveBar_Light_LightsUp`, `L_DiveBar_Light_AfterHours`;
   they are always-loaded today and would also be cooked as references, but the contract lists every generated `.umap`, so a later
   switch to by-name streaming cannot drop a lighting state from the build), LevelSmoke gains `L_DiveBar` by itself; **M2-C** - from
   now on every suite run with `rbue.py test --sound` (step 3).

After the last merge (18.10): `python Tools/blender/rbbl.py all -- --strict`; delete `Content/Generated`; `rbue.py py
Tools/unreal/editor/rb_make_all.py -- --strict` twice (the second with `--compare`); all tests + `rbue.py core`; `rbue.py ledger
--merge` then `--check`; the captures of M2-A5 (`capture_table.py`, `capture_divebar.py --set m2`, strict, inspected); `rbue.py
package --label M2`; `rbue.py perf --exe <RawBreak_Builds>/M2/Windows/RawBreak.exe --map "/Game/Generated/Maps/L_DiveBar?Mode=Practice"
--perf-exec "rb.Match.Break 9" --out Docs/perf/m2/divebar_1440p.json` on an idle GPU; the owner's playtest (M2-A10).

**Requests / notes for the packages (found by M2-0's tools on their work-in-progress branches)**

* M2-A: at the first M2-0 review **none** of the 17 rows of `Docs/licenses/ledger/M2-A.csv` had a `date` (ISO date required,
  venue-dive-bar 13.9). Resolved on `m2a` since: at the third review (2026-10-04) all fragments validate - M2-A 25 rows, M2-B 82,
  M2-L 3, no asset_id + source in two fragments that disagree.
* M2-B (third review): the new generator `Tools/blender/divebar/db_signs.py` (wall signs and paper) was in no package's files
  (`rbue.py owners` on `m2b`: "nobody owns it") and not in `db_build_all.py`; the architect added it to M2-B's files and to the
  Blender list on `m20-reviewed` (its docstring asked for it), so `m2b` passes the ownership check.
* M2-A (third review): `rb_make_divebar_fx.py` (owned since the plan) was missing from `rb_make_all.py`; added before
  `rb_make_divebar.py` on `m20-reviewed` (the M2-A2 regeneration from a clean `Content/Generated` keeps the dust motes and the
  headlight light function).
* M2-A: `--hide-tags RbDB_Ceiling` hides whole actors, their light components included (`USceneComponent::ShouldRender` checks the
  owner's hidden flag): put the tag only on the ceiling geometry, not on an actor that carries a troffer / fixture light the V10 plan
  view should keep.
* M2-B exports to `Art/DiveBar/Export/Props/<Asset>/` and `Export/Decals/`, M2-A's importer scans `Export/**/<Asset>/<Asset>.json`
  (nested folders): compatible; the ownership table gives `Export/{AxisTest,Arch,Neon}` to M2-A and everything else below `Export/`
  to M2-B.
* **M2-F (fourth review, build break at the M2-C merge)**: `Source/RawBreak/Private/Camera/RbHumanMotion.cpp` and
  `Source/RawBreak/Private/Player/RbBallInHandComponent.cpp` both define `kInternalStep` and `SmoothStep(double)` in an anonymous
  namespace; from the M2-C merge on both land in `Module.RawBreak.2.cpp` (C2374 / C2084, editor and game target). Put each file's
  helpers into a namespace named after the file (`RbHumanMotion`, `RbBallInHandComponent`). Same file, latent today:
  `LoadGenerated<T>(const TCHAR*)` duplicates M2-E's helper in `Balls/RbBallSet.cpp` (blobs 1 / 2). `rbue.py unity` must show
  neither afterwards. If M2-F is merged before the fix, the architect applies the rename at the merge.
* **M2-A (fourth review, `Functional.DiveBarRack` red in the full merge)**: the four straight-in ball-in-hand pots of the practice
  rack (`RbStroke` through M2-F's stroke: contact at the centre, B = 0, elevation 1-3.5 deg) each also pocketed the oversized cue
  ball (`pocketed [2,0]`, `[4,0]`, `[5,0]`, `[6,0]`: a rolling cue ball follows a straight-in object ball), so player 0's third
  consecutive foul ended rack 1 (`next 3`) before the 9: "the 9 is off the table" / "the 9 dropped on the last shot" fail. `m2a`
  (`180a937`, M1's stroke) lists the test as green; the likely difference is M2-F's stroke model (centre contact). The planner
  should not leave follow scratches (a cut angle, a stun distance or `RbStrike` with draw), and the rack check should not expect
  the 9 when the rack ended on fouls.
* **M2-A / M2-E (fourth review, `Unit.MultiTable.NoSingleTableLookups` red in the full merge)**: M2-E's grep test flags
  `RbDiveBarTest.cpp:694` and `:765` (`TActorIterator<ARbTable>`, e.g. the cue sweep ignoring every table); M2-A iterates
  `URbTableSubsystem::GetTables()` instead (or M2-E lists the two lines as accepted, like its pending requests).
* **M2-C / M2-D (fourth review, `Functional.PauseMidShot` red in the full merge)**: right after `TogglePauseMenu()` resumes,
  `URbAudioSubsystem::IsPausedMix()` is still true (`bWorldPausedMix` is refreshed only by the audio subsystem's next tick), and
  M2-D's test checks it in the same frame. M2-C: refresh the world part in `SetPausedMix` / derive it live in `IsPausedMix()`
  (preferred: the getter then never lags the world); otherwise M2-D's test waits one tick. audio.md 7.3 should say which.
* **M2-C (fourth review)**: every `rbue.py test --sound` run rewrites the committed `Docs/audio/m2/{au0_two_voices,
  divebar_break_master, mix_replay_pause_master, testroom_break_master}.wav` (LFS) from `RbAudioFunctionalTest.cpp`, so each
  integration suite leaves four modified binaries in the tree (an accidental `git commit -a` would churn LFS with renders of an
  arbitrary run). Write them under `Saved/RbAudio/m2c/` like the stems and copy into `Docs/audio/m2/` only on request (an
  `--extra` flag of the test run). Until then the architect runs `git restore Docs/audio/m2` after a suite run.
* Every package: helpers of test files **and of every other `.cpp`** in a namespace named after the file (never an
  anonymous-namespace name that another file of the module may also use; unity builds - the fourth review's dry run broke on two
  production files, not tests). `rbue.py unity` before the commit shows a collision the package's own build cannot see.
* M2-C (second review, `m2c` work in progress): `RawBreak.Functional.Audio.*` add an error without an audio device ("no audio
  device in the play world"), so a default `rbue.py test --filter RawBreak.` (with `-NoSound`) goes red once M2-C is merged. Accepted
  as designed (a silent pass would hide a broken device path); the runbook runs the suite with `--sound` from that merge on, and
  `rbue.py test --sound` now adds `-MuteAudio` itself (the `--extra=-muteaudio` of M2-C's comments is no longer needed, harmless).
* M2-A (information): `capture_divebar.py` starts one process per view and hides the ceiling / logs the EV through `ARbVenueInfo`
  (`RbVenue EV:`) instead of `--camera V01,V02,...`, `--hide-tags RbDB_Ceiling` and `RbCapture: EV100`; compatible with `rbue.py
  capture`, only slower (a shader / DDC warm start per view). Its choice; the multi-view path stays available.

### 18.13 M2 integration (round 6, `integ/m2`, 2026-10-04)

Branch `integ/m2` = `main` + `m20-reviewed` first (the runbook), then `m2e-`, `m2f-`, `m2d-`, `m2c-`, `m2l-`, `m2a-`, `m2b-reviewed`
(`--no-ff`, the 18.2 order). Every merge textually clean; `rbue.py owners` OK on all seven package branches before their merge. All
eight reviewed branches existed (no fallback to an unreviewed branch).

**Integration fixes (applied by the architect at the merge; each one is a request of a review or found by this round)**

| Area (owner) | Fix |
|---|---|
| Unity build (M2-F, request of the M2-0 review) | `RbHumanMotion.cpp` and `RbBallInHandComponent.cpp` both defined `kInternalStep` / `SmoothStep(double)` in an anonymous namespace (C2374 / C2084 once the M2-C merge put them into one blob); `RbBallInHandComponent.cpp`'s `LoadGenerated<T>` duplicated `RbBallSet.cpp`'s. The three helpers moved into namespaces named after their files (`RbHumanMotion::`, `RbBallInHandComponent::`), every use qualified. `rbue.py unity`: 0 collisions, 0 latent |
| Config (M2-0, 18.12 step 4) | `GameDefaultMap` -> `L_Title`; `MapsToCook` + `L_Title`, `L_DiveBar` and its four sublevels (`_Geo`, `_Light_Open`, `_Light_LightsUp`, `_Light_AfterHours`); cook-list preflight `[]` |
| Licence ledger (M2-0) | `rbue.py ledger --merge`: the 110 fragment rows of M2-A (25), M2-B (82), M2-L (3) merged into `asset-ledger.csv`; `--check` clean |
| `Unit.MultiTable.NoSingleTableLookups` (M2-A / M2-E) | `RbDiveBarTest.cpp` counts / ignores the tables through `URbTableSubsystem::GetTables()` instead of `TActorIterator<ARbTable>`; the grep test's pending list keeps only the look-dev camera's cached fallback cue (M2-L), the resolved entries of M2-L / M2-F are gone |
| `Functional.PauseMidShot` (M2-C, preferred option of the M2-0 review) | `URbAudioSubsystem::IsPausedMix()` reads the world's pause live (it lagged a resume by one tick); the mix ramp stays in the tick |
| Audio renders in the tree (M2-C) | `RawBreak.Functional.Audio.*` write the master WAVs to `Saved/RbAudio/m2c/masters/`; `rbue.py test --sound --extra=-RbAudioWriteDocs` writes them into `Docs/audio/m2/` on purpose. A suite run no longer modifies the committed LFS renders |
| `Functional.DiveBarRack` (M2-A vs M2-F) | the planner played draw plans (B = -0.3) through `RbStroke`, which strikes the centre: the oversized cue ball followed every straight-in. `PlanPot` now also returns the best centre (B = 0) plan; only that one goes through the human layer, a draw plan is played with `RbStrike`. A practice rack lost on three fouls is racked again instead of failing the "9 down" checks |
| Cue ball off the table (M2-F vs M2-E, 18.6.1) | M2-F's `BeginCueBallPlacement` took the cue ball into the hand at once, so a cue ball lying on the floor vanished (`Unit.LooseBall.CueBallInHand` red). Now the hand does not take a loose cue ball; Confirm with the empty hand tries the gazed pick-up first and never places a ball still on the floor; every automatic return goes into the hand (M2-E). Key hints: an empty hand while placing shows "Pick up the cue ball first" (M2-D's hint model + test) |
| Test room surfaces (M2-L, request of the M2-E review) | `ARbTestRoom` floor / walls / ceiling get `PM_RbSurface_Concrete` (loose balls bounced with the engine default, no surface type for the audio) |
| Dive-bar floor (M2-A import vs M2-E physics) | the committed `SM_DB_Arch_Floor` had no physical material (M2-A imported it before M2-E's `PM_RbSurface_*` existed); the regeneration below sets `PM_RbSurface_Vct` (the new integration test asserts the VCT surface under a loose ball) |
| L_DiveBar without M2-B's props (M2-A / M2-B) | M2-A's committed level was built before M2-B's props existed: every element was a greybox. Regenerated (below): 12 elements with M2-B meshes + 27 hero props, 20 greyboxes left (elements M2-B has not modelled yet) |
| Hero pint H14b (M2-A layout) | `layout.json` stood "a pint left on the head rail" on the head-left corner pocket's jaws (y 4.93 = the side nose line); now on the head rail cap between the corner and the centre diamond (y 5.18) |
| `Functional.DiveBar.CueSweeps` TS-4 (M2-A vs M2-B) | with M2-B's jukebox in place its hulls (body 2 cm, arched top 8 cm less deep than the E10 box) free the 58-in cue at 0.260 m instead of the box's 0.305 m; the test expects 0.260 +- 0.02 when the prop stands there, 0.305 for the greybox |
| A2 metrics (M2-0) | `rb_make_all.py --compare` failed only on the editor's unique-name counters in the venue validator report (`RbTable_0` vs `RbTable_1`, `PointLight_0` vs `_15`); report lines are compared with `Name_#` |
| New test (M2-0) | `RawBreak.Functional.M2Integration.{DiveBar,TestRoom}` (`RawBreakEditor/Private/Tests/RbM2IntegrationTest.cpp`): a complete 9-ball practice rack in the venue through the director (break, planned pots, ball in hand, decisions) until a rack is won; for EVERY shot the table's audio plan is built once and every impact event (strike, recontact, ball-ball, cushion / jaw / rail top, slate, liner / rim, pocketed) is scheduled as a sound or counted silent by design; then the 9 is sent over a side rail: handed to engine physics, lands, rests grounded on the venue floor's surface outside the table, every engine hit sounded, the foul spots the 9 on the foot spot, the next shot returns the loose 9 (Address) and the table shows it at the committed position |

**Acceptance (18.10)**

| # | Result |
|---|---|
| M2-A1 | `rbue.py build` and `--target RawBreak`: 0 errors, 0 compiler warnings (after every fix of this round) |
| M2-A2 | `rbbl.py all -- --strict`: 16 Blender generators OK (164.7 s). `rb_make_all.py --strict`: 14 generators + the test dev level, M1 validator OK, venue validator OK (lamp-only lux of the analytic 4.4 model in every band; the UE direct-light trace below the bands as documented by M2-A); second run `--compare`: "A2: asset metrics identical". A regeneration from an EMPTY `Content/Generated` also runs through, but without the git-ignored raw CC0 downloads (`Art/Third`, `Tools/art/fetch_cc0.py`) M2-L's walnut / leather scans fall back to the procedural surfaces (by design of `rb_make_materials.py`); the committed content is the in-place regeneration (the CC0 textures kept) with the 64 stale assets the clean run does not produce removed (M2-A fallback / greybox instances replaced by M2-B's materials and props, two unused CC0 sets) |
| M2-A3 | `rbue.py test --filter RawBreak. --sound`: **285 / 285** (285 found and completed, `TEST COMPLETE. EXIT CODE: 0`, 302 s; no working-tree change). `rbue.py core`: **938 / 938** (default run without `_Slow_`). `rbue.py ledger --check` clean, `selftest` OK, `unity` OK, `Tools/art/check_m2b.py` 0 problems |
| M2-A4 | F1-F9 (`Unit.Feel.*`, `Unit.HumanMotion.*`, `Unit.BallInHand.*`, `Functional.Feel.*`) green in the suite; the owner's re-test of P1 / P2 / P3 / P5 is part of the playtest |
| M2-A5 | `Docs/images/m2/{V01..V12,TH1..TH7}.png` (venue-dive-bar 12.1-12.3: every test and trailer camera, High, 1920x1080, strict) + `ev_report.txt`, inspected (below). Table look-dev captures: M2-L's of its review |
| M2-A6 | `Functional.DiveBar.*` (validator, walkability with the real pawn, cue sweeps, rack) green; VDB-T2 bands: V01, V08, V12 in band; V02 3.47 (4.5-5.5), V03 5.50 (6.5-7.5), V04 6.04 (7.8-8.4), V05 5.76 (4.5-5.5) out - known since M2-A (darker cloth of M2-L's table than the greybox the bands were set with) |
| M2-A7 | `Functional.Audio.*` green with `--sound`; L_DiveBar "10 room-tone layers, 9 of them at the level's RbAudio_* anchors (21 anchors)" as M2-C asked; `OnFootstep` (M2-F) and `OnImpact` / `OnRolling` / `OnReturned` (M2-E) are bound and fire (M2Integration: 6 / 11 loose-ball hits = 6 / 11 floor hits played); M2Integration: dive bar 13 shots, 151 impact events, all 151 scheduled; test room 11 shots, 134 events, 133 scheduled + 1 silent by design. The owner's listening test is open |
| M2-A8 | `Functional.MenuFlow` (title -> dive bar hot-seat -> pause -> settings -> FOV 55 -> resume -> quit to title; "the camera rig follows the new FOV": M2-D's request to M2-F is resolved in the merged tree), `PauseMidShot`, `Unit.UI.*` green |
| M2-A9 | logged, NOT a baseline (GPU 42 % busy before the run from foreign processes, 9 % after): packaged build, `L_DiveBar`, 2560x1440, High, `r.ScreenPercentage 66.67`, `rb.Match.Break 9`, 600 frames -> GPU mean 25.0 / p95 26.6 ms, frame p95 28.4 ms (over the 16.7 ms floor), game thread mean 1.14 / max 2.92 ms (< 6 ms) - `Docs/perf/m2/divebar_1440p.json`. The GPU cost of the dive bar is the open M3 item: an idle-GPU measurement first (M2-0 saw the same ~21-24 ms in the test room under foreign load), then the venue's lights / Lumen budget |
| M2-A10 | `rbue.py package --label M2`: preflight `[]`, editor + game target 0 / 0, `BuildCookRun` BUILD SUCCESSFUL, no cook warning -> `RawBreak_Builds/M2/Windows/RawBreak.exe` (not in git). Started headless: the default map is the title (`Docs/images/m2/packaged/title.png`, `--show-ui`), `L_DiveBar` V01 / V04 render as in the editor (`packaged/divebar_RbCam_DB_{V01,V04}.png`), a muted-audio run plays a live break (shot 1 simulated in 4.4 ms, 199 events, room tone "10 layers, 9 at anchors", every voice "device found"). The owner's playtest is open; instructions in German: `RawBreak_Builds/M2/SO_STARTEST_DU.txt` |

**Captures inspected** (`Docs/images/m2/`): the regeneration turned the greybox room into M2-B's bar (stools, booths, back bar with
labelled bottles and the mug club, jukebox, dart machine, cue rack, ledges, signs, lamp with the Old Castor shades, chalk, drinks)
around M2-L's 7-ft coin-op table; V04 / V06 / TH3 read as real. Seen and left: the 20 greybox elements (radiator, draft curtain,
mats, ATM, coolers, POS, TV-1, popcorn machine, tap tower, beer clock, sconces, polaroid wall, ...) are plain boxes; the greybox ATM
screen (TH7) and the cooler interiors (V05) clip to white at their spec luminance; TV-1 shows a half-noise picture; TH3's 100 mm f/2.8
macro is soft (M2-A's note).

**Requests that stay open** (owners in brackets): E05 / E02m rubber mats let loose balls fall through to the floor (`RbVenueProp`
ignores `RbLooseBall`; M2-A); `ARbLookDevCamera`'s fallback cue lookup (M2-L); clipped greybox screens (M2-A / M2-B); VDB-T2 band
re-derivation for M2-L's cloth and VDB-T3 class split (architect / M2-A); audio.md 7.3 should state that `IsPausedMix` follows the
world pause live (M2-C); settings are saved when a menu closes, Alt+F4 inside the settings loses changes (M2-D); a reduced-motion
upgrade has no backup of the motion rows (M2-D); M2-C's open list (T1 / T2 voice reduction, replay low-pass, `PauseLowPassHz`, loudness
-32.4 LUFS vs -29); M2-F's doubts (stand-in sleeve, faint contact preview, Eyes stabilisation test true by construction, P3 not
tested through real Enhanced Input events); `capture_divebar.py`'s URL option `?Game=NineBall` is the ENGINE's game-mode option
("Failed to load game mode 'NineBall' specified by URL options", harmless: the World Settings mode runs; M2-A); the dive bar's GPU
cost (M2-A9 above).
