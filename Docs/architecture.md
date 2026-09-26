# RAW BREAK — BilliardsCore Architecture

| | |
|---|---|
| Document | `Docs/architecture.md` |
| Status | v1.2 (2026-09-26): integration of `human-factors.md` v1.3 (player model `rb::human`, work package WP-11; table tilt and chalk-mark cling in the physics), incl. the HF integration review (section 20.1) and the product owner's answers to every HF question. Frozen for parallel implementation. |
| Changelog | **v1.2** (2026-09-26): `rb::human` headers and WP-11 stubs (7.5); `TiltParams`/`TiltChain`, the `TiltRefresh` event, the exact re-anchor of events inside tilt pieces, `ChalkMark`/`ContactClingFactor`, `TableCondition` + `MakePhysicsParams(Spec, Condition)`, `Expm1`/`Log1p`/`Cbrt`/`Floor`, 9 new `ParamTable` keys, a layering guard in the root CMake (8.11, 15 rows 28-36); every HF test assigned (17.12); rules-side follow-ups recorded (O-14); HF integration review (20.1: Q2/Q3/Q6 answered, intoxication hook, money-game stakes, exact streak fallback, per-address drift keys, tilt inside islands, tilt-chain termination bound, `Integ_` test prefix; section 15 rows 37-40). **v1.1** (2026-09-26): revision after the adversarial architecture review (35 findings, all resolved; section 20). **v1.0** (2026-09-25): initial architecture. |
| Scope | Engine-agnostic C++20 core `BilliardsCore` (physics + rules + the player model `rb::human`), its test tree and the `rbsim` tool |
| Sources of truth | `Docs/specs/physics-motion-and-cue.md`, `physics-collisions.md`, `equipment.md`, `rules.md`, `prior-art-and-validation.md`, `human-factors.md` (v1.3, incl. the product-owner decisions of its section 7), `ue5-realism-plan.md` (boundary parts). The verification logs at the end of each spec override earlier text; where specs disagree, section 15 records the decision. |
| Code | Public headers `Source/BilliardsCore/Public/rb/**` (complete declarations, units, spec defaults), stubs `Source/BilliardsCore/Private/rb/**` (every stub is marked `// TODO(WP-x)`), the private loop/island interface `Private/rb/Physics/SimInternal.h`, tests `Tests/Core/**`, tool `Tools/rbsim/**` |

Spec references use short prefixes: **MOT** = physics-motion-and-cue, **COL** = physics-collisions, **EQP** = equipment, **RUL** = rules, **VAL** = prior-art-and-validation, **HF** = human-factors, **UE** = ue5-realism-plan. Example: `COL 3.6` is section 3.6 of physics-collisions; `COL:D-12` is its test D-12. Architecture-level tests that are not in a spec are named `A-<AREA>-<n>` (section 18).

---

## 1. Goals and hard constraints

| Constraint | How the architecture meets it |
|---|---|
| C++20, namespace `rb` (`rb::rules` for rules), double precision, no exceptions, no RTTI, no Unreal headers | CMake flags `/W4 /WX /permissive- /GR- /EHs-c-` (GCC/Clang: `-Wall -Wextra -Wpedantic -Wshadow -Werror -fno-rtti -fno-exceptions -ffp-contract=off`); errors are `rb::ErrorCode` values; no `dynamic_cast`/`typeid`, no virtual dispatch in hot paths (model selection by enum `switch`). Debug checks use `RB_ASSERT` (`rb/Core/Assert.h`), never Unreal's `check`. |
| Never use identifiers Unreal/Windows define as macros | Coding rule 1 plus a compile-time safety net: `Tests/Core/Architecture/TestMacroTraps.cpp` defines look-alikes of the Unreal and Windows macros first and then includes every public header. `_USE_MATH_DEFINES` is not defined anywhere. |
| DLL-per-module Unreal editor builds | Every non-inline exported function and every exported member function carries `RB_API` (incl. `Polynomial::Eval/Derivative/Trim`). No class is exported as a whole (avoids C4251 with STL members). |
| Determinism: identical inputs → bitwise identical outputs | Strict total event order, exact-time shared-ball events merged into one island, id-independent geometric contact order inside islands, no unordered containers, no global mutable state, no randomness in the event loop, precise floating point pinned three times (CMake flags; `FPSemantics = Precise` in `BilliardsCore.Build.cs`; `rb/Core/FpGuard.h` pragmas as the first include of every core `.cpp`, enforced at configure time), transcendental calls funnelled through `rb/Math/Scalar.h` (section 11). |
| No heap allocation inside the event loop | Fixed-capacity containers (`rb::FixedVector`, `rb::EventHeap<512>`, sparse island contact list) sized from `kMaxBalls = 24`; `ShotResult`/`ShotRecord` vectors reserved once (`ReserveShotResult`) and never grown in the loop (overflow → diagnostic flag). |
| Performance: typical shot ≤ 100 µs, break ≤ 2 ms, ≥ 10 k shots/s/core, parallel AI | Lazy per-ball re-prediction with version stamps, swept-AABB broad phase, root isolation only inside validity windows, CLI islands only for real clusters, sustained contacts in 20 µs rigid mode, Mathavan step count chosen by an accuracy gate with slip-reversal splitting (≤ 2 µs per rail hit), recording switchable, independent `Simulator` objects sharing a read-only `TableGeometry` (section 12). |
| Per-ball radius, mass, inertia | `rb::BallSpec` per ball everywhere (`SimBall::Spec`); every model uses the per-ball inertia factor `k = I/(m R²)` (spec formulas are the `k = 2/5` case, section 7.2); the rules carry per-ball radii (`RulesTable::BallRadius[]`, `ShotStartSnapshot::Radius[]`). |
| Everything the specs define is expressible | Motion states incl. `Airborne`, `PocketPivot`, `PocketFall`, `Pocketed`, `OffTable`, and surface states on the cloth *or the flat rail cap* (`MotionSegment::SupportZ`); event types for every COL 1 / RUL 3.3 event; models Mathavan/Han/Mirror/Stronge/GRI/CLI (compliant + rigid)/pivot; two strikes for the lag; section 16 maps every spec section to an API. |
| Single source of truth for table geometry and table physics | `TableSpec` presets → `BuildTableGeometry` → `TableGeometry` (detection, contact frames, island features, rules landmarks via `BuildRulesTable`, `rbsim --geometry`, Unreal meshes) and `TableSpec` → `MakePhysicsParams` → `PhysicsParams` (cloth, facing `k_f`, liner). `Simulator::Run` rejects `ParamsOrigin::Unset` parameters. |
| Player model above the physics | `rb::human` (WP-11, section 7.5) turns an intended stroke into the `CueStrikeInput` with seeded, stateless noise and persistent equipment state; the core physics decides every outcome (HF principle 1: no dice rolls on make, miscue or cling). It is never included by the event loop, the shot record or the rules (layering guard in the root `CMakeLists.txt`). |
| Rules depend only on facts | `rb::rules` includes only `rb/Shot/ShotRecord.h` (plain data), `rb/Geometry/RackLayout.h` (lattice data) and its own headers; `DeriveShotFacts(ShotRecord)` → `ShotFacts` → `EvaluateShot`. Rules code never includes `rb/Physics/**` or `TableGeometry.h`. |

---

## 2. Coding rules (binding for every work package)

1. **Identifiers.** Never use names that Unreal or Windows headers define as macros: `PI`, `HALF_PI`, `TWO_PI`, `check`, `checkf`, `verify`, `ensure`, `SMALL_NUMBER`, `KINDA_SMALL_NUMBER`, `BIG_NUMBER`, `DELTA`, `INDEX_NONE`, `TEXT`, `LIKELY`, `UNLIKELY`, `FORCEINLINE`, `IN`, `OUT`, `OPTIONAL`, `CONST`, `VOID`, `ERROR`, `DELETE`, `IGNORE`, `ABSOLUTE`, `RELATIVE`, `TRANSPARENT`, `OPAQUE`, `TRUE`/`FALSE`, `CALLBACK`, `PASCAL`, lowercase `min`, `max`, `near`, `far`, `small`, `hyper`, `interface`. Use `rb::kPi`, `rb::Min`/`rb::Max` (never `std::min/max` in headers). `TestMacroTraps.cpp` enforces this for every public header.
2. **Style.** Tabs, PascalCase types/functions/members/locals (existing code), `k`-prefixed constants, enums are `enum class` with PascalCase enumerators. Math vectors keep lowercase `x, y, z`.
3. **Warnings are errors** (MSVC `/W4`, GCC/Clang `-Wall -Wextra -Wpedantic -Wshadow`). No shadowing, omit names of unused parameters, no constant conditions (use `static_assert` / `if constexpr`), no implicit narrowing, no sign mismatches, no unused static functions.
4. **Headers** are self-contained and include only what they use; no `using namespace` in headers.
5. **No global mutable state**, no function-local `static` variables with state, no singletons, no thread-locals. `constexpr` tables are fine.
6. **No I/O, no allocation, no randomness** in the core (except `ReserveShotResult` and the `Simulator` constructor, which allocate once). Randomness consumers (`GenerateRack`, `ApplyRackGaps`, `BuildBallSet`) take an explicit seed and use `rb::Rng`; the player model's noise is a pure hash of explicit keys (`rb/Human/NoiseHash.h`, no generator state).
7. **Units** are SI in every signature; doc comments state units in brackets. Angles are radians.
8. **Floating point.** Every core `.cpp` starts with `#include "rb/Core/FpGuard.h"` (checked by the root `CMakeLists.txt`). No `long double`, no FMA intrinsics, no parallel reductions inside a shot.
9. **Assertions** use `RB_ASSERT` (`rb/Core/Assert.h`, active when `RB_DEBUG_ASSERTS=1`, i.e. CMake Debug); the expression must be side-effect free.
10. **Inertia.** Never hard-code `2/5`, `5/2`, `2/7`, `5/7`, `7/2`, `7/10` in a model: use `k = InertiaFactor(Spec)` (section 7.2).
11. **Ownership.** A package edits only the files it owns (section 17). A needed change in another package's file (including a header) is requested from that owner; changes to WP-0 files need architect sign-off. Contract headers are frozen: additions are allowed, signature changes need sign-off from all consumers. `Private/rb/Physics/SimInternal.h` needs WP-6a + WP-6b sign-off.
12. **Stubs** stay compiling and linking at all times; replacing a stub is the owning package's job. Leave `// TODO(WP-x)` only where work remains.

---

## 3. Directory layout and ownership

```
Source/BilliardsCore/
  BilliardsCore.Build.cs                    Unreal module (NoPCHs, unity off, FPSemantics Precise)    WP-0
  Private/BilliardsCoreModule.cpp           Unreal-only module entry                                   WP-0
  Public/rb/
    Config.h  Version.h                     RB_API export macro, version                              WP-0 (frozen)
    Core/Assert.h                           RB_ASSERT (debug only, never Unreal's check)              WP-0
    Core/Constants.h                        kMaxBalls=24, kMaxStrikes=2, units, g                     WP-0
    Core/Error.h                            ErrorCode                                                  WP-0
    Core/FixedVector.h                      allocation-free vector                                     WP-0
    Core/Ids.h                              MotionState, PocketId, CushionId (pocketless mapping),
                                            JawSide, rail features, TableLine, presets                 WP-0
    Core/Random.h                           seeded xoshiro256** (game-seeded uses only)                WP-0
    Core/Tolerances.h                       NumericsConfig + RulesTolerances                           WP-0
    Math/Vec3.h Vec2.h Aabb.h Scalar.h      vector math, transcendental funnel                         WP-0
    Math/Polynomial.h                       root isolation (degree <= 8)                               WP-5
    Math/Quat.h                             orientation quaternion                                     WP-7
    Physics/BallState.h                     BallSpec (per-ball I), InertiaFactor, BallState            WP-1
    Physics/Motion.h                        ClothParams, MotionSegment (SupportZ), classify            WP-1
    Physics/Slate.h                         SlateParams, slate impact, table reaction                  WP-1
    Physics/CueStrike.h                     strike, squirt, miscue, pinch, follow-through tip path     WP-1
    Equipment/EquipmentConstants.h          EQP 11.1 constants (+ Blackball balls)                     WP-2
    Equipment/TableSpec.h                   TableSpec presets (+ k_f, liner), ValidateWpa              WP-2
    Equipment/BallSets.h                    ball-set presets (+ Blackball)                             WP-2
    Equipment/Cue.h                         CueSpec presets                                            WP-2
    Geometry/TableGeometry.h                BuildTableGeometry (physics + renderer), rail-top polygons WP-2
    Geometry/RackLayout.h                   rack lattice, anchor site, micro-gap algorithm             WP-2
    Physics/BallBall.h                      frictional ball-ball impulse                               WP-3
    Physics/Compliant.h                     CLI island solver (compliant + rigid, tips, features)      WP-3
    Physics/Cushion.h                       Mathavan, Han, Mirror, Stronge, GRI, e_c law, dispatcher   WP-4
    Physics/PocketDrop.h                    PocketModel, drop-edge pivot macro-step                    WP-4
    Physics/Detect.h                        all event predictors, feature queries                      WP-5
    Physics/EventQueue.h                    deterministic lazy-deletion heap                           WP-6a
    Physics/ShotResult.h                    simulation output (events, tracks, cue tips, record)       WP-6a
    Physics/Simulator.h                     PhysicsParams, MakePhysicsParams, SimInput, Simulator      WP-6a
    Physics/ParamTable.h                    reflected PhysicsParams keys (tools, replay, console)      WP-6a
    Physics/Playback.h                      StateAt, orientation law, cursors, cue tips, sampling      WP-7
    Shot/ShotRecordBuilder.h                physics -> rules record adapter, BuildRulesTable           WP-7
    Shot/ShotRecord.h                       physics -> rules CONTRACT (plain data)                     WP-8
    Rules/RulesTypes.h RulesConfig.h        rules vocabulary, per-ball radii, config/presets           WP-8
    Rules/ShotFacts.h Evaluate.h            F1-F13, evaluateShot                                       WP-8
    Rules/TableRules.h Lag.h Match.h        racks, spotting, placement, re-racks, lag, match           WP-9
    Human/NoiseHash.h Skill.h TipState.h    player model rb::human (HF): seeded noise + Q1 streak      WP-11
    Human/CueState.h BallMarks.h            guard, attributes and situation, tip / chalk / cue / ball
    Human/HumanModel.h AiProfiles.h         state, ExecuteStroke, SampleHand, diagnosis, synthetic
    Human/Chores.h Progression.h Venue.h    hand, progression and product switches, chores, venues
  Private/rb/Core/FpGuard.h                 precise-FP pragmas, first include of every core .cpp      WP-0
  Private/rb/Version.cpp                                                                              WP-0
  Private/rb/Physics/SimInternal.h          PRIVATE loop <-> island/pocket interface (workspace)       WP-6a (+6b sign-off)
  Private/rb/Physics/Simulator.cpp SimLoop*.cpp ShotResult.cpp ParamTable.cpp                         WP-6a
  Private/rb/Physics/SimIsland* SimPocket* SimRailTop*                                                WP-6b
  Private/rb/Human/*.cpp                    player-model stubs                                         WP-11
  Private/rb/<other subdirectories>/*.cpp   implementations (stubs now)                                per header owner
Tests/Core/
  rbtest.h TestMain.cpp CMakeLists.txt      harness (dependency-free)                                  WP-0
  Architecture/                             header smoke test, pinned defaults, macro traps            WP-0
  Math/                                     Polynomial/Vec3 tests                                      WP-5
  Motion/ CueStrike/                                                                                   WP-1
  Equipment/ Geometry/                                                                                 WP-2
  BallBall/ Compliant/                                                                                 WP-3
  Cushion/ Pocket/                                                                                     WP-4
  Detect/                                                                                              WP-5
  Simulator/                                                                                           WP-6a
  Islands/ PocketFlow/                                                                                 WP-6b
  Playback/ ShotRecord/                                                                                WP-7
  Rules/Facts/ Rules/Eval/                                                                             WP-8
  Rules/Table/ Rules/Match/                                                                            WP-9
  Human/                                                                                               WP-11
  Validation/ Benchmarks/                                                                              WP-10
Tools/rbsim/  CMakeLists.txt Main.cpp JsonWriter.*                                                     WP-7
Tools/xref/   pooltool cross-check harness (Python, needs VAL OQ-2 approval)                           WP-10
Tools/reference/*.py, human-factors/*.py    spec oracle scripts (read-only; packages port golden values) WP-0
THIRD_PARTY_NOTICES.md                      Apache-2.0 attributions (created on WP-4's request)        WP-0
```

Outside the core (no work package of this plan): `Source/RawBreak/**`, `Config/**`, `RawBreak.uproject` (Unreal game), `Docs/specs/**` (spec owners), `Docs/decisions.md`, `Docs/trailer-plan.md`, `README.md`.

---

## 4. Layering and dependencies

```
                         Core/* , Math/Vec*, Aabb, Scalar, Polynomial, Quat      (foundation)
                                              |
        +---------------------+---------------+------------------+
        |                     |                                  |
  Equipment/*, Geometry/*   Physics/BallState, Motion, Slate, CueStrike
        |                     |
        +----------+----------+
                   |
   Physics/Cushion, PocketDrop      Physics/BallBall, Compliant (uses Cushion's restitution law, CueStrike's tip path)
                   |                     |
                   +------ Physics/Detect (segments x geometry -> event times, contact frames, feature queries)
                                         |
            Physics/EventQueue, ShotResult, Simulator, ParamTable  <---  Shot/ShotRecord.h (contract)
            [Private SimInternal.h: loop (6a) <-> islands/pockets/rail top (6b)]   ^
                                         |                                        |
                     Physics/Playback, Shot/ShotRecordBuilder (-> rules::RulesTable) +
                                                                                  |
                         Rules/RulesTypes, RulesConfig, ShotFacts, Evaluate   (never include Physics/* or TableGeometry.h)
                                         |
                         Rules/TableRules (uses Geometry/RackLayout), Lag, Match
```

```
       rb::human (Human/*, WP-11): NoiseHash, Skill, TipState, CueState, BallMarks, HumanModel, AiProfiles, Chores, Progression, Venue
          uses: Core, Math, Equipment/*, Geometry/RackLayout, Physics/BallState, Motion, CueStrike, BallBall (ChalkMark),
                ShotResult (after-shot update), Simulator (PhysicsParams, SimBall, TableCondition), Shot/ShotRecord (NonTipContact data)
          produces: CueStrikeInput, TableCondition, SimBall::ChalkMarks, RackGapParams     (never uses Rules/*)
```

Header dependencies only point downward (or sideways to plain-data contracts). `ShotRecordBuilder.h` is the only header that sees both physics and rules types; the rules never include it. `rb::human` sits beside the rules on top of the physics: nothing in `rb/Physics`, `rb/Shot` or `rb/Rules` includes `rb/Human/*` (checked at configure time by the root `CMakeLists.txt`).

---

## 5. Data flow

### 5.1 One shot

```
 UE / AI / rbsim                    BilliardsCore                                       rules (rb::rules)
 ---------------                    -------------                                       -----------------
 TableSpec preset -- BuildTableGeometry --> TableGeometry (once per table, shared, read-only)
 venue seed + table index -- human::MakeVenueTableCondition --> TableCondition (slope, ball cling)
 TableSpec + TableCondition -- MakePhysicsParams --> PhysicsParams (Origin = Table, Tilt; overrides allowed afterwards)
 BallSet preset   -- BuildBallSet ------> per-ball BallSpec
 TableGeometry + radii -- BuildRulesTable --> rules::RulesTable (landmarks, pocket openings, per-ball radii)
 rack seed        -- rules::GenerateRack --> RackAssignment (positions incl. micro-gaps, anchor on the spot)
 match state      -- GetShotConstraints / ValidateDeclaration(placement) (before the stroke)
 UE 5.4 stroke input / AI SyntheticHand --> human::IntendedStroke
   -- human::ExecuteStroke(attributes, situation, TipState, CueBodyState, NoiseKey + NoiseHistory) -->
      CueStrikeInput (contact offsets with the current dome radius, mu of the chalk zone, e_tip, TipTouchesCloth)
      + prediction flags + breakdown (replay, diagnosis)
                                   |
 SimInput { Table*, Environment, PhysicsParams, SimBall[24], Strikes[1..2], ShotContext, RecordOptions }
                                   |
                            Simulator::Run   (event loop, section 8)
                                   |
 ShotResult { Status, Strikes[], Events[] (pre/post, impulses), Tracks[24] (segments + Orientation0),
              CueTips[], Finals[24], Record (ShotRecord), Diagnostics }
        |                     |                                   |
   Playback (StateAt,     audio/VFX from Events          rules::DeriveShotFacts(Record, RulesTable, tolerances, clock)
   OrientationAt, cursors,(NormalSpeed, impulses)                   |
   CueTipAt)                                                ShotFacts (F1-F13)
                                                                    |
                                                        rules::EvaluateShot(config, RulesTable, GameState@start, declaration, facts)
                                                                    |
                                                        ShotOutcome -> Match ApplyShot / ApplyOption -> next GameState
 after the shot: human::ApplyShotToEquipment (tip wear, chalk marks deposited / faded -> next SimBall::ChalkMarks);
                 human::AdvanceNoiseHistory (the draw is spent); on a miss the diagnosis re-runs of HF 3.9
```

### 5.2 AI rollout

The AI owns N `Simulator` objects (one per worker thread) sharing one `TableGeometry` and one `PhysicsParams` made by `MakePhysicsParams` (the referee's physics: same cushion model, no "fast" model). Each rollout sets `RecordOptions{Trajectories = false, EventStates = false, LogTransitions = false, LogObservers = false}` and a small `ResultCapacity`, runs `Run`, then `DeriveShotFacts` + `EvaluateShot` on `Result.Record`. The record is identical to the referee's record whatever the logging switches are: observers (line crossings, freeze-leave, jump-over) and tip-contact events are always produced for the record, and `TipContacts` are built from record events, never from the (possibly overflowing) physics log (RUL pitfall 17). No allocation after warm-up; no shared mutable state. The AI's strokes go through `rb::human::SyntheticHand` and `ExecuteStroke` like the player's (HF principle 4, HF-B07); rollout sample `s` uses `RolloutKey(Key, s)` (common random numbers across the candidates of one decision, no streak history). On a tilted table rollouts may use `Tilt.Tolerance = 5e-4` m (5-8 pieces per roll instead of 8-14, HF 4.5.3: a numerical tolerance, not a model change); weak AIs plan on a level table (a knowledge knob of HF 5.5), the referee's shot always uses the table's real `TableCondition`.

### 5.3 Replay / network

A shot is reproduced from: core version, `PhysicsParams` (all keys of `ParamTable.h`, incl. `NumericsConfig`), `TableSpec`, ball specs, initial states/orientations, `Strikes`, `ShotContext`, `RecordOptions`, and for the rules the `ShotDeclaration`, `RulesConfig`, `RulesTable` and rack seed. `rbsim --dump-input` writes exactly this set (`rbsimInput` schema, section 14). Re-running produces a bitwise identical `ShotResult` (ROB-10 hashes the serialized event logs). The tilt and cling parameters are `ParamTable` keys, the chalk marks part of `SimBall`. For strokes executed by the player model the replay header also stores the input log, the `NoiseKey`, the shooter's `NoiseHistory` (a cache: rebuildable from the key), attributes, situation, `TipState`, `CueBodyState` and the final `CueStrikeInput`; playback uses the stored `CueStrikeInput`, so CRT differences cannot break a replay, and only re-execution (HF-B01) needs the same platform (section 11).

---

## 6. Frames, units, identifiers, sign conventions

* **Core frame** (all specs): right-handed, origin at the center of the bed, `+x` toward the foot rail, `+y` across (left for a player at the head end), `+z` up; cloth `z = 0`, resting center `z = R`. SI units; radians.
* **Slip velocity** `u = v + R (z_hat x w)`; right English `a > 0` gives `w_z > 0`; `rb::SlipVelocity`, `rb::CoriolisInvariant` encode the conventions once (VAL pitfall 11).
* **Contact normals** (COL pitfall 1): ball-ball `n_hat` from ball 1 (lower id) to ball 2, impulse on ball 1 is `-J_n n_hat - J_t t_hat`; fixed contacts use `k_hat` from the contact point to the center, impulse `+P_N k_hat`.
* **Ids** (`rb/Core/Ids.h`): ball id 0 = cue ball; pockets `P0..P5` and cushions `C0..C5` follow RUL 2.3 (counter-clockwise, cushion `Ck` runs from `Pk` to `P(k+1)`); EQP names map as `POCKET_HEAD_RIGHT = P0 … POCKET_HEAD_LEFT = P5`. Each pocket has jaws `Incoming` (ends cushion `C(k-1)`) and `Outgoing` (starts `Ck`). Rail features for 32-bit masks: cushions 0-5, jaws `6 + 2 pocket + side`. **Pocketless tables** (carom, ROB-03) keep the ids: `C0` = whole right rail, `C2` = foot, `C3` = whole left rail, `C5` = head, `C1`/`C4` absent (`NoseSegment::Present = false`); containers indexed by `CushionId` always have 6 entries.
* **Time**: `t = 0` at the first tip contact (both lag strokes at `t = 0`). Each ball's segment has its own time base `T0`; all polynomials are evaluated in local time `tau = t - T0` (MOT impl. note 1).
* **Unreal** is left-handed and in centimetres; the mirror `P = diag(1, -1, 1)` lives in the UE module only (section 13).

---

## 7. Public type catalogue

### 7.1 Foundation (WP-0)

| Header | Type / function | Purpose |
|---|---|---|
| `Core/Assert.h` | `RB_ASSERT`, `RB_DEBUG_BREAK` | debug-only assertions (`RB_DEBUG_ASSERTS=1`) |
| `Core/Constants.h` | `kMaxBalls` (24), `kMaxBallPairs`, `kMaxStrikes` (2), `kCueBallId`, `kPoolBallCount`, `kInch`, `kOunce`, `kMph`, `kDegToRad`, `kStandardGravity`, `kInfinity` | capacities, exact units, physical constants |
| `Core/Error.h` | `ErrorCode`, `Succeeded`, `ToString` | error channel (no exceptions) |
| `Core/FixedVector.h` | `FixedVector<T, N>` | allocation-free contiguous container; `PushBack` returns false when full |
| `Core/Ids.h` | `BallId`, `MotionState` (+ `IsOnSurface`, `IsMoving`, `IsTerminal`, `IsInPocket`), `PocketId`, `PocketKind`, `CushionId`, `JawSide`, `RailFeatureOf*`, `TableLine`, `ClothPreset`, `TablePreset` | identifiers shared by physics, output and rules |
| `Core/Random.h` | `Rng` (xoshiro256**), `SplitMix64Next` | deterministic, seeded; only for game-seeded rack/ball-set generation |
| `Core/Tolerances.h` | `NumericsConfig`, `RulesTolerances` | every numerical tolerance and guard (section 10) |
| `Math/Vec3.h`, `Vec2.h`, `Aabb.h` | `Vec3`, `Vec2`, `Aabb2`, `Aabb3` | vectors, plan-view helpers, broad-phase boxes |
| `Math/Scalar.h` | `Sqrt`, `Sin`, `Cos`, `Atan2`, `Exp`, `Expm1`, `Log`, `Log1p`, `Cbrt`, `Floor`, `Asinh`, `Clamp`, `Min`, `Max`, `Sign`, `SignNonZero`, `SmoothStep01` | transcendental funnel for determinism (`Expm1` for the tilt pursuit solution, HF 4.5.2) |
| `Math/Polynomial.h` (WP-5) | `Polynomial` (exported members), `SolveInInterval`, `SmallestRootInInterval` | derivative-isolation real-root finder on a window (COL 3.3) |
| `Math/Quat.h` (WP-7) | `Quat`, `Rotate`, `FromAxisAngle`, `FromRotationVector`, `IntegrateConstantOmega` | ball orientation for playback |
| `Private: Core/FpGuard.h` | precise-FP pragmas | determinism guard (section 11) |

### 7.2 Physics

**Per-ball inertia (review item 17).** `BallSpec{}` equals `MakeBallSpec(0.028575, 6 oz)` bitwise (so `I == 0.4 m R²` exactly for defaults and `kBallInertia`). Every model uses `k = InertiaFactor(Spec) = I/(m R²)`; the specs' constants are the `k = 2/5` case: `5/2 → 1/k`, `2/7 → k/(1+k)`, `5/7 → 1/(1+k)`, `7/2 → (1+k)/k`, `7/10 → (1+k)/2`, pivot leave angle `cos ψ = (2 + (1+k) v0²/(g ρ)) / (3+k)`, ball-ball `k_t = 1/m1 + 1/m2 + R1²/I1 + R2²/I2`. Spec golden values then agree to rounding. `Run` rejects `k ∉ (0, 2/3]`, `R ≤ 0`, `m ≤ 0`.

| Header | Type / function | Purpose |
|---|---|---|
| `BallState.h` | `BallSpec {Radius, Mass, Inertia}`, `MakeBallSpec`, `InertiaFactor`, `kSolidSphereInertiaFactor`, `BallState {Position, Velocity, Omega, State}` | per-ball properties and state |
| `Motion.h` | `ClothParams` + presets `kClothDefault/WorstedFast/NappedBar`, `ClothParamsFor`; `MotionSegment` (incl. `SupportZ`); `SlipVelocity`, `RollingOmegaH`, `CoriolisInvariant(k)`, `MechanicalEnergy`, `PositionAt`, `VelocityAt`, `OmegaZAt`, `SlideDuration(k)`, `RollDuration`, `SpinDuration`; `ClassifyState(S, R, SupportZ)`, `MakeSegment(S, T0, Spec, Surface, SupportZ, g)`, `EvaluateSegment`, `SegmentEndState`; tilt: `TiltParams`, `IsLevel`, `InPlaneGravity`, `TiltChain` (`MotionSegment::Tilt`), `PursuitState`, `PursuitStopTime`, `EvaluatePursuit`, `TiltPieceDuration`, `MakeSegment(…, Tilt)`, `EvaluateSegmentForEvent` | closed-form motion on the cloth or the flat rail cap, flight (MOT A, C.1); tilted table as chains of quadratic pieces with exact nodes (HF 4.5, section 8.11) |
| `Slate.h` | `SlateParams`, `MinBounceSpeed`, `LandingTau`, `SlateImpactResult`, `ResolveSlateImpact(…, Spec, …)`, `ApplyTableReaction(…, Spec, Surface, …)` | slate impact and Zeno guard (MOT C.2-C.4), table reaction (COL 2.4 step 6) |
| `CueStrike.h` | `kCueOffsetValidLimit`, `kMaxCueSpeed`, `PinchParams`, `CueStrikeInput`, `CueFrame`, `StrikeResult`, `ValidateCueStrike`, `MakeCueFrame`, `CueContactPoint`, `AimToContactOffset`, `MiscueLimit`, `SquirtAngle(k)`, `PinchLambda`, `SeparationMargin(k)`, `StrikeCueBall`; follow-through `CueTipPath` (strike index, struck ball), `MakeCueTipPath`, `CueTipAsSegment`, `TipRecontactResult`, `ResolveTipRecontact` (any ball) | cue strike (MOT B), follow-through tip for double hit / push / touched-ball detection |
| `BallBall.h` | `BallBallFrictionModel`, `BallBallModel`, `BallBallParams` (+ `ChalkClingFactor`; `ClingFactor` = `k_venue`), `ImpactBody`, `BallBallImpulse`, `BallBallFriction`, `ResolveBallBall`, `CutAngle`; `ChalkMark`, `kMaxChalkMarks`, `BallChalkMarks`, `ChalkMarkWeight`, `ContactClingFactor` | frictional ball-ball impulse (COL 2); per-contact cling from chalk marks (HF 4.3) |
| `Compliant.h` | `CliMode {Compliant, Rigid}`, `CliParams` (Tsuji alpha derived, sustained-contact switch), `IslandFeatureKind {EdgeLine, JawCircle, FacingPlane, Plane}`, `IslandFeature` (bounded: segment extents, arc range, plane polygon + cut), `IslandBody`, `IslandTip`, `IslandRecordKind`, `IslandContactRecord`, `IslandRecordList`, `HertzContactTime`, `TsujiDamping`, `TsujiAlphaForRestitution`, `CueTipContactStiffness`, `IslandJoinDistance`, `CompliantIsland` (`AddBody/AddFeature/RemoveBody/SetTip/RemoveTip/SetMode/SetInPlaneGravity/Step/SustainedContact/CanExit/BodyAtRest/GapToIsland`) | Compliant Local Integrator for clusters, the break, pressing and sustained contacts, Zeno chains, the sloped rail top, frozen-ball strikes (COL 3.9, 6.2, 7.3) |
| `Cushion.h` | `CushionModel {Mathavan2010, Han2005, Mirror, StrongeCompliant}`, `CushionRestitutionLaw`, `CushionParams` (N by accuracy gate, split on), `PocketContactParams` (+ rail-top rolling), `RailCapSurface`, `CushionFrame`, `MakeCushionFrame`, `ToLocal`, `ToWorld`, `FixedContactKind` (+ `RailTopEdge`), `FixedContact`, `CushionImpactResult`, `CushionRestitution`, `MathavanSettings`, `ResolveMathavan`, `ResolveHan`, `ResolveMirror`, `ResolveStronge`, `ResolveGri`, `ResolveFixedContact` | ball vs fixed geometry (COL 4, 5.3, 6.2; VAL XREF) |
| `PocketDrop.h` | `PocketModel {GeometricLevelA, CaptureCircle}`, `PivotResult`, `ComputePivot(V0, ρ, k, g)`, `PivotPath`, `MakePivotPath(…, Spec, …)`, `EvaluatePivot`, `PivotLeaveState`, `PivotDetectionProxy` | rolling over the rounded drop edge (COL 5.4); pooltool circle pockets for XREF-01 |
| `Detect.h` | `ContactFlags`, `ContactPrediction`, `DetectOptions` (+ `Pockets`), `BallBallGapPolynomial`, `PredictBallBall`, `SweptBounds`, `PredictNose{OnCloth,Airborne}`, `PredictJawArc{OnCloth,Airborne}`, `PredictFacing{OnShelf,Airborne,TopEdge}`, `PredictDropEdge`, `PredictLinerWall` (front arc below the rim, back wall up to `WallTopZ`), `PredictRimTorus`, `PredictCaptureCircle`, `PredictCaptureDepth`, `PredictPocketExit`, `PredictSlateLanding`, `PredictRailTop(polygon)`, `PredictRailTopEdge`, `PredictSupportExit`, `PredictOuterBoundary`, `PredictLampApex`, `LineCrossing`, `PredictLineCrossings(IncludeFrom)`, `PredictPlanDistanceCrossing`, `TableFeatureKind`, `TableFeatureRef {Kind, Index, SubIndex}`, `SupportKind`, `BallTableContext`, `FeaturePrediction`, `PredictTableEvent`, `QueryTableFeatures`, `MakeFixedContact` | event detection (COL 3, 4.10, 5.3, 6), island feature queries, rules observers |
| `EventQueue.h` | `EventTier`, `QueuedEventKind` (+ `TipContact`, `TiltRefresh`), `kTransitionFeature`, `kTiltRefreshFeature`, `QueuedEvent` (+ `FeatureSub`), `EventPrecedes`, `EventHeap<N>`, `kEventHeapCapacity` (512) | deterministic lazy-deletion priority queue |
| `ShotResult.h` | `ShotEventType` (+ `TipContactBegin/End`, `BallJumpedOver`, `IslandRigid`, `TiltRefresh`), `ShotEventFlags`, `ShotEvent`, `SegmentKind`, `TrajectorySegment`, `BallTrack`, `CueTipSegment`, `BallFinalStatus`, `BallFinal`, `SimStatus`, `SimDiagnostics`, `StrikeOutcome`, `ShotResult`, `ResultCapacity`, `ReserveShotResult`, `ResetShotResult` | simulation output |
| `Simulator.h` | `ParamsOrigin`, `PhysicsParams` (+ `Pockets`, `Tilt`, `ChalkCling`), `TableCondition`, `MakePhysicsParams(Spec)`, `MakePhysicsParams(Spec, Condition)`, `ValidatePhysicsParams`, `RecordOptions` (`LogObservers`), `SimBall` (+ `ChalkMarks`), `StrikeRequest`, `SimInput` (`Strikes`), `Simulator {Run}` | the event-based simulator; the venue table's condition (tilt, ball cling) enters only through `MakePhysicsParams(Spec, Condition)` |
| `ParamTable.h` | `ParamType`, `PhysicsParamInfo`, `PhysicsParamCount`, `PhysicsParamAt`, `GetPhysicsParam`, `SetPhysicsParam` | reflected parameter keys (rbsim `--param`, dumps, UE console); 93 keys (v1.2 appended `tilt.*`, `ballball.k_chalk`, `ballball.chalk_cling`) |
| `Playback.h` | `kOrientationSubstep`, `EvaluateTrajectorySegment`, `HasConstantRotationAxis`, `SegmentOrientationAt`, `IntegrateOrientationSteps`, `StateAt`, `OrientationAt`, `PlaybackCursor` (per-ball grid cache), `ResetCursor`, `StateAtCursor`, `CueTipAt`, `TrajectorySample`, `SampleTrajectory` | exact playback, the orientation law, cue animation |

### 7.3 Equipment and geometry (WP-2)

| Header | Type / function | Purpose |
|---|---|---|
| `EquipmentConstants.h` | `kBallRadius`, `kBallInertia` (exact expression), `kCushionNoseHeight`, `kPocketDropPointRadius`, Blackball ball constants, … (EQP 11.1) | constants with source tags |
| `TableSpec.h` | `PocketSpec`, `TableSpec` (+ `FacingRestitutionScale`, `LinerRestitution`, `LinerFriction`), presets `kTableNineFootPro` … `kTableSevenFootTrue`, `GetTableSpec`, `WpaCheck`, `WpaReport`, `ValidateWpa` | the single source of truth for table dimensions and table-dependent contact values |
| `BallSets.h` | `BallSetPreset` (+ `Blackball`), `BallSet`, `kStandardPoolBall`, `kMagneticCueBall`, `kOversizedCueBall`, `BuildBallSet` | per-ball specs (standard, dive bar, old bar, snooker, blackball) |
| `Cue.h` | `CueSpec`, `CuePreset`, `kCuePlaying19oz`, `kCueBreak21oz`, `kCueJump9oz`, `kCueHouse19oz`, `GetCueSpec` | cue properties |
| `TableGeometry.h` | `NoseSegment` (+ `Present`), `JawArc`, `Facing`, `PocketGeometry` (+ `WallTopZ`), `RailTopKind`, `RailEdgeKind`, `RailTopPolygon` (convex polygon, edge kinds, cut disc), `Sight`, `TableLandmarks`, `CushionProfile`, `TableGeometry`, `EnvironmentSpec`, `CushionContactGeometry`, `BuildTableGeometry`, `ComputeCushionContact`, `FacingContactOffset`, `CornerThroat`, `SideThroat`, `IsOverPocketOpening`, `BuildNoseOutline`, `IsAboveHeadString` | exact geometry for physics and renderer |
| `RackLayout.h` | `RackShape`, `RackAnchor`, `RackSite`, `RackGapParams` (+ outliers) + presets (+ `kRackGapMixture`), `RackApexX`, `RackAnchorSiteIndex`, `BuildRackLattice`, `ApplyRackGaps(…, AnchorIndex, …)`, `CountTouchingPairs`, `RackInnerSide` | rack lattice and micro-gaps |

### 7.4 Physics → rules contract and rules

| Header | Type / function | Purpose |
|---|---|---|
| `Shot/ShotRecord.h` (WP-8) | `BallPresence`, `CueBallInHand`, `ExternalObject`, `OffTableReason`, `RecordEventType`, `RecordEvent` (+ `OtherContactBefore`), `ShotStartSnapshot` (+ `Radius[]`), `TipContact` (+ `Ball`, `Strike`), `NonTipSource` (+ `CueTip`), `NonTipContact`, `StrokeInfo`, `StrokeRecord` (per-strike `Strokes`), `ShotContext`, `BallEndStatus`, `BallEnd`, `SupportedBall`, `ShotEndSnapshot`, `ShotRecord` | RUL 3.1-3.4 record (plain data) |
| `Shot/ShotRecordBuilder.h` (WP-7) | `BuildShotStartSnapshot`, `IsRecordRelevant`, `ToRecordEvent`, `BeginShotRecord`, `AppendRecordEvent`, `FinishShotRecord`, `BuildShotRecord`, `BuildRulesTable` | physics → record adapter; geometry → rules table |
| `Rules/RulesTypes.h` | `Discipline`, `CueBallNext`, `ShotKind`, `BallGroup`, `GroupOf`, `Call`, `ShotDeclaration`, `PlayerState`, `BallStatusKind`, `BallStatus`, `GameState` (+ `VisitsRemaining`, `LastGroupBallPocket`), `Foul`, `FoulSet`, `Option`, `NextAction`, `RackCommandKind`, `PlacementCommand`, `RackCommand`, `ShotOutcome` (+ `NextFreeShot`, `NextVisits`), `PocketOpening`, `RulesTable` (per-ball radii, pocket openings), `MakeRulesTable`, `AboveHeadString`, `OnHeadString`, `BelowHeadString`, `InBaulk` | RUL 2, 10.1 vocabulary (the spec's `E`-prefixed names drop the prefix) |
| `Rules/RulesConfig.h` | all RUL 12.1 switch enums, `RulesConfig`, `RulesPreset`, `MakeRulesConfig`, `DisciplineOf` | WPA defaults and variants |
| `Rules/ShotFacts.h` | `PocketedBall`, `BallContactEntry`, `RailContactKind`, `RailContactEntry`, `BallShotSummary` (ordered ball and rail contacts, overflow flags, radius), `ShotFacts`, `DeriveShotFacts`, `ResolveFirstContact` | F1-F13 |
| `Rules/Evaluate.h` | `EvaluateShot(Config, Table, State, Declaration, Facts)`, `ResolveCall`, `LowestObjectBallAtStart`, `GroupCleared`, `CountObjectBallsOnTable`, `LegalFirstContactMask` | evaluateShot |
| `Rules/TableRules.h` | `RackAssignment`, `GenerateRack`, `GenerateStraightPoolRack` (WP-9, merged), `SpotBall`, `SpotBalls`, `SpotRequestCandidate`, `RackOutline`, `StraightPoolRackOutline`, `InterferesWithRack(P, R, Table)`, `BlocksSpot(Spot, Rs, Other, Ro)`, `PlanRerack14`, `PlanRerack15AfterFifteenthPocketed`, `OverPocketOpening`, `CueBallPlacementLegal` | table procedures (per-ball radii) |
| `Rules/Lag.h` | `LagBallFacts`, `LagOutcome`, `LagResult`, `DeriveLagBallFacts`, `EvaluateLag`, `LagStartPositions` | lag (both balls in one record) |
| `Rules/Match.h` | `MatchPhase`, `ShotClockConfig`, `ShotClockState`, `MatchConfig`, `MatchState` (+ `ActiveMember`, `TeamBreaker`, `InningHadProgress`, `ShotAfterBreak`), `CueBallChoiceBit`, `ShotConstraints` (+ `PlacementChoices`, `FreeShot`, `VisitsRemaining`, `Member`), `StartMatch`, `ApplyLagResult`, `ChooseBreaker`, `SetupRack`, `SetupRackWith`, `GetShotConstraints`, `CompleteDeclaration`, `ValidateDeclaration(…, PlacedCueBall)`, `ApplyShot`, `ApplyOption`, `RequestSpot`, `DeclareStalemate`, `Concede`, `ShotClockAllowed`, `ShotClockExpired`, `RequestShotClockExtension`, `ShotClockStartTime` | match state machine |

### 7.5 Player model `rb::human` (WP-11, human-factors)

Namespace `rb::human`, headers `Public/rb/Human/*.h`. Pure functions of their inputs (no side effects, no allocation, no global state, deterministic); equipment and ball state change only through the explicit after-shot and chore functions. Never included by `rb/Physics`, `rb/Shot` or `rb/Rules` (root CMake guard).

| Header | Type / function | Purpose |
|---|---|---|
| `Human/NoiseHash.h` | `Mix64`, `HashKeys`, `U01` (exact integer definitions, header-only), `InvNorm`, `TruncNormal`, `kNoisePhiLo`, `kTruncatedSigma`, `NoiseChannel`, `ChannelBit`, `NoiseKey` (+ `AddressIndex`), `ShooterKey` (64-bit rollout key), `RolloutKey`, `ProcessShotKey`; streak guard `kStreakWindow` (7), `kStreakMaxPerEighth` (2), `kStreakMaxRedraws` (32), `StreakSlot`, `StreakHistory`, `NoiseHistory`, `GuardedDraw`, `DrawCandidate`, `EighthOf`, `DrawGuarded`, `PushStreak`, `RebuildStreakHistory`, `RebuildNoiseHistory`, `AdvanceNoiseHistory`, `HistoryMatchesKey`, `DrawPerShot`, `PlainUniform`; `WatchableProcess`, `MakeWatchableProcess`, `ProcessValue`, `ProcessRate` | stateless seeded noise: hash, truncated draws, the Q1 streak guard, band-limited drift / tremor (HF 3.2, 7) |
| `Human/Skill.h` | `ShooterAttributes`, `UniformAttributes`, `ShooterHabits`, `GrowHabit`, `SkillScale` (L(x; rho)), `BridgeType`, `BridgeSpec`, `BridgeSpecFor`, `HumanParams` (every constant of HF 3.3-3.7, `NoiseScale`, `ChannelMask`, `StreakGuard`, `WarpSightLength`, `RulesTolerances`), `FloorSource`, `StrokeSituation` (+ `Intoxication`, the Q2 hook), `PressureInputs`, `PressureWeights`, `PressureMode`, `MoneyGameStakes`, `ComputePressure`, `FatigueFromNight` | attributes, habits, human-layer parameters, the per-shot situation incl. pressure (money-game stakes), fatigue and intoxication inputs (HF 3.3, 3.4, 5.1) |
| `Human/TipState.h` | `kTipZoneCount`, `TipHardness`, `ChalkGrade`, `ChalkGradeSpecFor`, `ChalkCube`, `TipState`, `TipParams`, `TipZone`, `TipContactPoint`, `LookupTipContact`, `TipEdgeLimit`, `EffectiveTipRestitution`, `HitSeverity`, `ApplyTipWear`, `ChalkCap`, `ApplyChalkTwist`, `AutoChalkTwists`, `TwistDuration`, `ScuffTip`, `ShapeTip`, `TrimTip`, `Retip` | chalk coverage per tip zone -> `mu` at the contact, tip shape and condition, chalk cubes, the revolver rule (HF 4.1, 4.2) |
| `Human/CueState.h` | `CueBodyState` (bow, `WarpKnown`, taper for the clearance test), `WarpEffect`, `ComputeWarp`, `NoticeableBow`, `AutoRollTestNotices` | cue warp in the hand (HF 4.4), cue body for 3.6 |
| `Human/BallMarks.h` | `MarkParams`, `DepositChalkMark`, `FadeChalkMarks`, `WipeChalkMarks`, `TravelDistances`, `ComputeTravelDistances` | chalk marks on balls (HF 4.3); the cling itself is physics (`BallBall.h`, 8.11) |
| `Human/HumanModel.h` | `IntendedStroke`, `BallObstacle`, `SituationFactors`, `StrokeDelta`, `StrokeSource`, `StrokeBreakdown`, `ExecutedStroke`, `ExecuteStroke`, `ComputeSituationFactors`, `SettleInEnvelope`, `SettleFactor`, `HandPose`, `SampleHand`, `MissCause`, `DiagnosisStep`, `DiagnosisStepAt`, `ApplyDiagnosisStep` (human side; physics side), `StrokeShares`, `ComputeStrokeShares`, `ApplyShotToEquipment` | the deterministic stroke execution: intended stroke + attributes + situation + equipment state + seed -> `CueStrikeInput` + prediction flags + breakdown; the rendered pose (what you see is what hits); the counterfactual diagnosis; the after-shot update (HF 3.1, 3.5-3.9, 4.1-4.3) |
| `Human/AiProfiles.h` | `SyntheticHandParams`, `AiKnowledge`, `AiProfileId`, `AiProfile`, `GetAiProfile`, `AiCharacter`, `CharacterAimBias`, `PlannedStroke`, `SyntheticHand` | AI opponents on the same model: the synthetic hand returns an `IntendedStroke`, never a `CueStrikeInput` (HF 3.8, 5.5, HF-B07) |
| `Human/Chores.h` | `ChoreMode` (R / A / C / P), `ChoreTiming`, `CoinChoreDuration`, `BallClearDuration`, `PerformChalking`, `RackGapsForQuality`, `HabitualRackQuality`, `CapRitualResult` | chore durations from physical steps, partial state, habitual results; rack quality -> `RackGapParams` for `rules::GenerateRack` (HF principle 5, 4.6) |
| `Human/Progression.h` | `AlcoholMode`, `AttributeVisibility`, `MoneyGames`, `ChoreSpeed`, `ProductConfig`, `DefaultChoreSpeed`, `HotSeatGuestAttributes`, `ShowAttributeNumbers`, `MoneyGamesAllowed`, `LowDeflectionShaftUnlocked`, `StrokeIntoxication`, `XpSource`, `XpAward`, `AttributePointCost`, `XpToRaise`, `RepeatFactor`, `DifficultyPreset`, `AimLineAssist`, `TipRingAssist`, `BodyFoulDisplay`, `AssistSettings`, `GetAssistSettings`, `ImperfectionSetting`, `NoiseScaleFor` | product-owner switches (HF 7), XP, difficulty presets and assists (HF 5.2-5.4) |
| `Human/Venue.h` | `VenueKind`, `SeedTableSlope`, `VenueBallCling`, `MakeVenueTableCondition`, `VenueBallSetSeed`, `kMaxHouseCues`, `HouseCue`, `SeedHouseCue`, `BarChalkCube` | per-venue seeded state: table slopes, ball dirt, ball-set seed, the house-cue rack (HF 4.4, 4.5.5, HF-B11) |

**Product-owner decisions (HF 7, all answered).** Each decision is one `ProductConfig` value (or `HumanParams::StreakGuard`), so a later change of mind is a configuration change: Q1 streak guard (on); Q2 alcohol **cosmetic only for V1** (`AlcoholMode::CosmeticOnly`); Q3 attribute numbers **hidden** (`AttributeVisibility::Hidden`, diegetic); Q4 hot-seat guests at 50 in every attribute without career XP; Q5 chores Full on the first visit of a venue and Brisk afterwards; Q6 money games **in** (`MoneyGames::SideBetsAndHustling`: side bets and hustling with in-game cash, never bought with real money or cashed out); Q7 low-deflection shaft without an unlock gate (`LowDeflectionMinSteadiness` 0). `Arch_HumanFactorsDefaultsFromSpec` pins all of them.

**Intoxication hook (Q2, for a later mechanic).** `StrokeSituation::Intoxication` `I` in [0, 1] is set by `StrokeIntoxication(ProductConfig, level)`: 0 under `CosmeticOnly`, the game's level under `Mechanic`. `ExecuteStroke` maps it onto the pressure (`P_x = P (1 - c_calm min(1, I / I_c))`: one drink calms), the drift and the tremor (`1 + k max(0, I - I_c)/(1 - I_c)`), HF 3.4-3.5, with placeholder gains `HumanParams::Intoxication*`. Every factor is exactly 1 at `I` = 0, so V1 is bitwise the model without the hook, and switching the mechanic on later needs tuning only, no core change. The same path serves the AI (principle 4). UE camera effects of alcohol are cosmetic in V1 and only while not down on a shot (section 13 item 11, HF-20).

**Money games (Q6).** `MoneyGameStakes(bet, cash)` feeds the stakes term of `ComputePressure` (0.6 for a small bet up to 1 at half the cash or more, HF 3.4); money matches count as real matches for Nerve XP; the hustler's sandbagging and money-game racks are on by default (HF 5.5, 4.6).

**Streak guard (Q1).** Per-shot draw `n` of a shooter key `S` and channel `c` (5-9, 20-22) is the first candidate `U01(HashKeys(MatchSeed, S, c, n, Sub))`, `Sub` = 0, 1, …, whose eighth occurs fewer than twice among the accepted draws `n-7 .. n-1` of `(S, c)`; after 32 rejections a deterministic fallback maps the last candidate into the allowed eighths in exact integer arithmetic (HF 3.2). The sequence is a pure function of `(MatchSeed, S, c, n)`: `RebuildNoiseHistory` (the recursion from draw 0) is the definition, `NoiseHistory` the O(1) cache the game keeps per shooter and advances once per revealed draw (a tip contact, or an aborted stroke that showed the per-shot ramp, HF 3.7). A cache that does not match the key (`HistoryMatchesKey`: match seed, shooter key, index) is rebuilt by `DrawPerShot`, so a stale cache (diagnosis re-runs after `AdvanceNoiseHistory`, a replay without it) can never change a draw: no hidden state. Rollout keys and `StreakGuard = false` take the plain candidate `Sub` 0 and never read the history. The watchable drift and tremor processes are keyed per get-down (`ProcessShotKey`: `ShotIndex | AddressIndex << 32`), so a dry run cannot reveal the drift of the next address.

**Data flow of one human stroke.** UE input (or `SyntheticHand`) -> `IntendedStroke`; the game assembles `StrokeSituation` (IK stance, bridge, `ComputePressure` with `MoneyGameStakes` in money games, `FatigueFromNight`, sweat, `StrokeIntoxication`) and passes the shooter's attributes, `TipState`, `CueBodyState`, the other balls, the `NoiseKey` and `NoiseHistory` -> `ExecuteStroke` -> `ExecutedStroke::Strike` goes into `StrikeRequest::Input`; `SampleHand` renders the same pose before contact; after `Simulator::Run`, `ApplyShotToEquipment` and `AdvanceNoiseHistory`; on a miss the diagnosis loop (`DiagnosisStepAt` 0..4, `ApplyDiagnosisStep`, re-execute, re-run) names the first change that makes the shot (HF 3.9).

---

## 8. The event loop (WP-6a, with WP-6b hooks)

The loop lives in WP-6a (`Simulator.cpp`, `SimLoop*.cpp`); islands, pockets, landings and the rail top live in WP-6b (`SimIsland*`, `SimPocket*`, `SimRailTop*`). They share the private `sim::Workspace` and the hook/service functions declared in `Private/rb/Physics/SimInternal.h` (`StartIsland`, `AdvanceIsland`, `NextIslandStepTime`, `ProcessPocketEvent`, `RouteLanding`, `ProcessRailTopEvent` implemented by 6b; `ReplaceSegment`, `EmitEvent`, `MakeTerminal`, `BallStateAt` implemented by 6a).

### 8.1 Per-ball state (`sim::BallSlot`)

| Field | Meaning |
|---|---|
| `Seg` | current `MotionSegment` with its own time base `T0` (only balls touched by an event are re-based) and `SupportZ` (0 cloth, `RailTopZ` flat rail cap); on a tilted table a chain piece (`Seg.Tilt`, 8.11) |
| `Version` | `uint32` stamp, incremented whenever `Seg` is replaced; every queued event stores the versions of its balls |
| `Context` | `BallTableContext`: pocket, support kind and rail-cap polygon. There is **no over-the-rail flag**: every feature's applicability is decided by its own geometric validity region along the segment (review item 21). |
| `Pivot`, `BounceIndex`, `SequenceMaxZ` | pivot path while `PocketPivot`; airborne sequence (`N_max`, `BallLand` value) |
| `InIsland` | ball currently integrated by the island (no analytic predictions) |
| `InitialFreezeRails/Balls` | rail features / balls the ball was frozen to at `t = 0` (`ShotContext.FrozenTolerance`), cleared when it separates by more than `LeaveDistance` (event mode: `FreezeLeave` observer; island: per step) |
| `JumpPending`, `ContactSinceJump` | jump-over observer state (rules F9) |
| `Observers` | up to 16 pending observer times of the current segment: line crossings, freeze-leave, jump-over enter/leave |
| `Orientation0`, `SampleStart/Position`, `RotationAccumulator` | recording cursor (orientation law; adaptive Sampled segments inside islands and pivots); `Orientation0` is also maintained for every ball with chalk marks when `ChalkCling` is on, whatever the `RecordOptions` (8.11) |
| Zeno history | `sim::ZenoEntry`: last 8 contact times per ball-ball pair and per ball-feature slot |

Per strike (`sim::TipSlot`): moving flag, `CueTipPath`, version, open tip-contact ball, `FrozenTarget` (the ball `f` of RUL F7: among the balls frozen to the struck ball at `t = 0`, the one with the largest positive `n_hat · d`; `kNoBall` if none), `StruckTouchedOther`, open `CueTipSegment`.

### 8.2 Predictions ("slots")

After a ball's segment changes it is re-predicted into its slots; each slot has at most one live entry in the queue:

1. **End slot** (sole owner of landings, review item 22): `T0 + TauEnd` → `Transition` (tier `Transition`) for surface states, `SlateLanding` for `Airborne` (tier `Slate`), pivot end for `PocketPivot`, `TiltRefresh` (tier `Transition`, `FeatureKind = kTiltRefreshFeature`) when a tilt chain piece ends in a node (`Seg.Tilt.EndsInRefresh`, 8.11). `PredictTableEvent` never returns `SlateLanding`.
2. **Table slot**: `PredictTableEvent` — earliest applicable feature by state **and position** (Detect.h): surface on the cloth (noses, jaw arcs, facing faces, drop edges or capture circles); surface on the flat cap (`SupportExit`, outer boundary); `Airborne` (airborne nose/arc/facing face and top edge, rail-top planes and edges, outer boundary, lamp apex, and **rim torus + liner/back wall of every pocket whose `a_d` cylinder the swept bounds reach** — break hops and jump shots near pockets, review item 3); `PocketPivot` (facings and jaw arcs of its pocket on `PivotDetectionProxy`); `PocketFall` (facings, arcs, liner, rim torus, capture depth, pocket exit). Tier by feature (`Cushion`, `Pocket`, `Boundary`).
3. **Pair slots**: for every other in-play, non-terminal, non-island ball `j`: `PredictBallBall(Seg[i], Seg[j])` if at least one moves and the swept AABBs (inflated by `R_i + R_j + ContactTol`) overlap. Window `[max(T0_i, T0_j), min(end_i, end_j, horizon)]`. A pivoting ball uses its `PivotDetectionProxy`.
4. **Tip slots** (one per moving cue, review item 5): the earliest `PredictBallBall(CueTipAsSegment(Path), r_tip, Seg[j], R_j)` over **every** in-play, non-island ball `j` → `TipContact` (tier `Strike`). Re-predicted when the tip path or any ball segment changes, only while the cue moves (`t < Path.StopTime`).

Observers are computed with the segment and kept outside the heap (section 8.7).

### 8.3 Queue, ordering, invalidation, simultaneity

* `EventHeap<512>` binary min-heap of `QueuedEvent`, ordered by the strict total key **`(Time, Tier, BallA, BallB, FeatureKind, FeatureIndex, FeatureSub)`** with exact double comparison. Tier order (COL 3.7, extended): `Strike < BallBall < Cushion < Pocket < Slate < Boundary < Transition`.
* **Lazy deletion**: a popped entry is valid iff its version stamps equal the balls' (tip's) current versions and the ball is not in an island / terminal. Stale entries are skipped (`Diagnostics.StaleEventsSkipped`). When the heap is full, `Compact(IsValid)` rebuilds it (≤ 2·24 + 276 + 2 = 326 live entries; deterministic because live keys are unique).
* **Exact simultaneity (COL 3.7, review items 7 and 34):** before processing a contact entry `E` (tiers `Strike` … `Boundary`), all other valid contact entries with exactly the same `Time` are examined; if any shares a ball with `E` (directly or transitively), the whole group is handed to `StartIsland` as one seed set. Entries of tier `Transition` (motion transitions, `TiltRefresh`) carry no impulse and never form or join a group (v1.2, section 15 row 32): at an equal time they pop after every contact, and a contact that replaced the ball's segment has already made them stale. Equal-time events that remain are therefore on disjoint ball sets and commute, so the tie order never changes results (only the log order), which makes results independent of ball-id permutations. The COL 1e-12 s window is not used for ordering: near-simultaneous contacts are islanded by the `delta_cl` BFS (a much larger window, `≈ T_H`), which subsumes it (section 15 row 19).
* After resolving an event, **all** slots of every ball whose state changed are re-predicted (VAL 8.3).

### 8.4 Main loop

```
Run(Input, Result):
  validate: Table != null, Params.Origin != Unset, ValidatePhysicsParams, per-ball specs (R, m > 0, k in (0, 2/3]),
            balls inside the table, no overlap > OverlapGuard, 0..kMaxStrikes strikes on distinct balls at rest,
            ValidateCueStrike                                   -> SimStatus::InvalidInput on failure
  resolve derived params: Cli.TsujiAlpha < 0 -> TsujiAlphaForRestitution(BallBall.Restitution)
  reserve / reset Result; BeginShotRecord (if RecordOptions::ShotRecord)
  for each ball in play: ClassifyState, Seg = MakeSegment(state, 0, ..., Params.Tilt), open track (Orientation0 = input)
  for each strike: process CueStrike at t = 0 (section 8.6)
  predict all slots of all moving balls and moving tips
  loop:
    E = earliest valid heap entry (skip stale)
    if an island is active and (E missing or E.Time > NextIslandStepTime):
        if !AdvanceIsland(Ws, E ? E.Time : +inf): stop all balls -> Aborted (island budget); break
        continue
    if E missing: break                                        (every ball stationary or terminal)
    emit observers with time < E.Time                          (section 8.7)
    if E.Time > TimeHorizon: stop all balls -> HorizonReached; break
    if ++EventsProcessed > MaxEvents: stop all balls where they are -> Aborted; break
    if E is a contact and another valid contact entry has exactly E.Time and shares a ball: StartIsland(group); continue
    process E (section 8.5); re-predict the slots of every ball whose state changed
    emit observers with time == E.Time of balls whose segment E did not replace (section 8.7)
  flush remaining observers; close tracks and cue tips; fill Finals; StopTime = last ball stationary/terminal
  FinishShotRecord; Status = Ok
```

### 8.5 Processing per event kind

| Event | Processing |
|---|---|
| `Transition(i)` | `s = SegmentEndState(Seg[i])` (exact snaps); log `MotionTransition` (if `LogTransitions`); new segment at `t`; `Stationary` on the flat rail cap → `MakeTerminal(OffTable, RestsOnRailOrFrame)` (COL 6.2); otherwise final time. |
| `TiltRefresh(i)` | `s = SegmentEndState(Seg[i])` = the exact node state (no snap, state unchanged); `ReplaceSegment` (next chain piece, version bump, observers recomputed from `t` with `IncludeFrom`, all slots of `i` re-predicted); log `TiltRefresh` only with `LogTransitions`; `Diagnostics.TiltRefreshes++` (8.11). Counts against `MaxEvents`. |
| `SlateLanding(i)` | `RouteLanding` (WP-6b, COL 6.1): center inside a capture circle → `PocketFall`, `BallPocketEnter`; inside the annulus `r_p < ρ < a_d` → the torus event must have fired (it is predicted for airborne balls near pockets, 8.2) — else `MissedEvents++`; over the playing surface or shelf → `ResolveSlateImpact(bounce index)`, `BallSlate`; still airborne → next flight; back on the surface → classify, `BallLand` (max height of the sequence). Behind a nose line → the rail-top event must have fired → `MissedEvents++`. |
| `BallBall(i, j)` | Evaluate both at `t`. **Island test**: BFS from `{i, j}` over balls with gap ≤ `delta_cl = IslandJoinDistance(v_n, m*)` **and over table features** (`QueryTableFeatures`) with gap ≤ `delta_cl`; also forced when the event is `Pressing`, the Zeno detector fired for the pair, or a moving cue tip can reach `i` or `j` within `delta_cl`. Exactly `{i, j}`: `ResolveBallBall` → `ApplyTableReaction` for balls that were on a surface → `ClassifyState` → log `BallBall` (`CutAngle` if one is the cue ball; `Resting` if `e = 0`) → Zeno history. Otherwise `StartIsland` (8.8). |
| `TableFeature(i, f)` cushion-like (nose, jaw arc, facing, liner, rim) | `Pressing` or Zeno → ball-feature island. Else `MakeFixedContact` → `ResolveFixedContact` (Mathavan/Han/Mirror/Stronge on the cloth, GRI airborne/liner/rim) → classify → `continuesInitialFreeze` from `InitialFreezeRails` → log `BallCushion` / `BallJaw` / `BallLiner` / `BallPocketRim`. |
| `TableFeature(i, f)` rail top (`RailTop`, `RailTopEdge`, `SupportExit`) | `ProcessRailTopEvent` (WP-6b, section 8.9). |
| `DropEdge`, `LinerWall`, `RimTorus`, `CaptureDepth`, `PocketExit`, `CaptureCircle`, pivot end | `ProcessPocketEvent` (WP-6b, section 8.9). |
| `OuterBoundary(i)` | log `BallOffTable(Floor)`; `MakeTerminal(OffTable)` (no floor physics). |
| `LampApex(i)` | log `BallExternalContact(Lamp)` and `BallOffTable(ExternalObjectRebound)`; `MakeTerminal(OffTable)`. |
| `TipContact(j, strike)` | `ResolveTipRecontact(Tip, t, Ball j)` (any ball); log `TipRecontact` + `TipContactBegin`/`TipContactEnd` (interval `[t, t + ContactTime]`, `B = FrozenTarget`, `Value` = gap to it, `SubFeature` = `StruckTouchedOther`); new tip path, new `CueTipSegment`. A tip contact on a ball other than the strike's struck ball becomes `NonTipContact{Source = CueTip}` in the record (RUL F10). |

Every resolution is followed by `ClassifyState` (with its snaps); a downward `v_z` of a ball that was on a surface is resolved by `ApplyTableReaction` **at the same timestamp**, never scheduled (MOT impl. note 6, COL 2.5). Resolvers take the ball state from `sim::BallStateForEvent` (inside a tilt chain piece: position from the piece, velocity and spin exact, 8.11); approach tests use the segment state (`BallStateAt`), which is what detection used.

### 8.6 Cue strikes and follow-through (t = 0)

For each `StrikeRequest` (1 normally, 2 for the lag, RUL 4.1): `StrikeCueBall` returns the post-strike state incl. the slate reaction at `t = 0+` (MOT B.8.3, never a separate event). The simulator logs `CueStrike` (Feature = strike index), `TipContactBegin` at `t = 0` and `TipContactEnd` at `ContactTime` (unless an island later extends the interval), `BallAirborne` if the ball hops, and opens the tip path `MakeCueTipPath`. **Architecture decision (DERIVED/TUNING):** the cue then follows through along `d` with speed `V'` and decelerates uniformly to rest over `CueSpec::FollowThroughDistance`; the tip dome (radius `r_tip`) is a moving sphere whose contacts with **any** ball are tip-slot events in event mode, and an `IslandTip` participant (linear compliant contact, stiffness `CueTipContactStiffness(ContactTime, m, M)`, damping from `e_tip`) when the touched ball is inside an island — which is the frozen cue-ball case of RUL F7/G17: the CB frozen to an OB enters an island at `t = 0+` and the tip keeps pushing it while the OB decelerates it. Tip-contact intervals inside islands come from positive tip force. Two separated intervals on the struck ball = double hit (F7a), one interval longer than `T_push` = push (F8), both judged by the rules with the envelope data (`B = f`, gap, `OtherContactBefore`). All tip path pieces are recorded in `ShotResult::CueTips` so that Unreal animates the cue the rules judged.

### 8.7 Observers (no state change)

Line crossings (`HeadString`, `FootString`, `CenterString`, `LongString`, `Baulk`; the center must pass the line by more than `LineCrossEps`, direction recorded), freeze-leave crossings and jump-over enter/leave (`PredictPlanDistanceCrossing` while a ball is airborne; `BallJumpedOver(A, B)` when an entered plan overlap is left without a `BallBall(A, B)` in between) are computed per segment. **Ordering (review item 20):** observers with `t < E.Time` are emitted before `E`; observers with `t == E.Time` after `E`, and only for balls whose segment `E` did not replace — for a replaced segment, observers are recomputed from the new segment on the closed interval `[E.Time, …)` (`PredictLineCrossings(IncludeFrom = true)`). Ties among observers: by ball id, then kind, then line. **Inside islands** (review item 23) the island evaluates line crossings, freeze-leave distances and jump-over overlaps of every member at every step (crossing time = linear interpolation within the step). Observers never bump versions. **They are always produced for the rules record when `RecordOptions::ShotRecord` is set**; `LogObservers` only controls the copy in `ShotResult::Events` (review item 24).

### 8.8 Islands (CLI) — start, stepping, joining, sustained contacts, exit (WP-6b; solver WP-3)

* **Start** (`StartIsland`): members = BFS result over balls **and table features** (nose lines, jaw arcs, facings, rail-top planes and edges within `delta_cl`, converted to bounded `IslandFeature`s: segment extents, arc angle range, facing extent, plane polygon + cut — review item 26) + moving cue tips that can reach a member (`IslandTip`). Members get `InIsland`, their versions are bumped (their queue entries become stale), the Zeno history of every member pair is cleared, `IslandBegin` is logged. Mode `Compliant` (1 µs), except rail-top islands which start `Rigid` (COL 6.2). On a tilted table the island gets `SetInPlaneGravity(InPlaneGravity(Params.Tilt, g))` right after `Reset` (8.11).
* **Stepping** (`AdvanceIsland`): before each step, all valid non-island events with `Time <= step end` are processed first. After each step: records → `BallBall` / `BallCushion` / `BallJaw` / `BallRailTop` events with `FromIsland`, `TipContactBegin/End`; observers of members; **joining of balls** (`GapToIsland ≤ delta_cl`, or a queued ball-ball event inside the step window) **and of table features** (review item 6): every step, or every `n` steps with a conservative reach bound `n·dt·(v_max + a_max·n·dt) < d_min`, `QueryTableFeatures` on the bodies' reach AABB → `AddFeature`; **member exits**: a member whose center reaches a drop-edge circle, leaves the cloth region, or leaves the rail-top region is removed (`RemoveBody`) and continues in event mode (pocket state machine / Airborne); adaptive `Sampled` recording (a new sample when linear interpolation would deviate by more than `SampleTolerance` = 10 µm, at every contact begin/end, at least every `SampleMaxInterval` = 10 ms — review item 35; `Omega0` = accumulated rotation / duration, so the orientation law is exact).
* **Sustained contacts (review item 1).** When `SustainedContact()` holds (every active contact `|v_n| < SustainedSpeed = v_rest` for `SustainedSteps` = 200 steps: the Hertz transient is over) or the compliant phase exceeds `CompliantMaxDuration` (50 ms), the island switches to **Rigid** mode (sequential impulses + position projection, 20 µs, `IslandRigid` logged). Examples: a CB with topspin frozen to an OB pushing it (D-12), a ball with running English slip-pressed along a rail (Z-3), a ball on the sloped rail top. A rigid island runs **until its contacts open or its bodies rest** (COL 7.3); there is **no fallback to plain impulses** (they cannot resolve zero-speed contacts, COL pitfall 16).
* **Exit** (`CanExit` + rest): no contact force for `ExitZeroForceSteps` steps, all pairs separating, every gap ≥ 0 (COL 3.9.2, pitfall 17) **and no touching pair that would immediately re-trigger the pressing rule** (`|gap| ≤ ContactTol`, `|f'| ≤ ApproachSpeedTol`, `f'' < 0`) — or all bodies at rest (rigid mode). Each member: state → `ApplyTableReaction` → `ClassifyState` → new segment at the island end time → re-predict; tips leave with a new analytic path. `IslandEnd` logged.
* **Re-entry guard.** Zeno histories of member pairs are cleared at island start and exit, so the Zeno detector needs 8 fresh impulse contacts before it islands a pair again; a pressing re-entry at the exit instant is prevented by the exit condition above; every island advances at least `ExitZeroForceSteps` steps.
* **Budget.** `NumericsConfig::MaxIslandSteps` (200 000 steps per shot, compliant + rigid). Exceeding it stops all balls where they are, `SimStatus::Aborted`, `Diagnostics.IslandBudgetExceeded` (the rules replay the shot, like the event cap). CPU targets: D-12b and Z-3 < 1 ms (Release).
* At most one island exists at a time; a new cluster event during an active island merges into it (the CLI integrates disjoint components correctly; cost only).
* **Id independence (CL-8, BRK-04; review item 7):** contacts are processed in an id-independent geometric key order (lexicographic contact point, then canonically signed normal, then feature index), forces are Jacobi-accumulated (compliant) or swept Gauss-Seidel in that order (rigid); together with the exact-simultaneity rule of 8.3 this makes results bit-identical under id permutations.

### 8.9 Pocket state machine (COL 5.4), landing and rail-top routing (COL 6.1-6.2) — WP-6b

```
surface on cloth --DropEdge (a_d = r_p + r_d, entering, front arc)--> BallPocketEnter
   v_perp >= sqrt(g rho) -> PocketFall ;  else PocketPivot --(T_p, or truncated by a facing / jaw / ball on the proxy)--> PocketFall
PocketFall --facing / jaw arc / liner (GRI) / rim torus (GRI) / other balls--> PocketFall
          --CaptureDepth z <= -R--> Pocketed (BallPocketed, terminal)
          --PocketExit (center outside a_d, z > R)--> Airborne over the table (BallPocketExit)
Airborne near a pocket: rim torus and liner / back wall are predicted (front arc below -r_d; back wall up to RailTopZ)
Airborne landing: inside capture circle -> PocketFall (no slate); annulus r_p < rho < a_d -> torus must have fired;
                  playing surface / shelf -> ResolveSlateImpact; behind a nose line -> rail-top event must have fired
PocketModel::CaptureCircle (XREF-01): center enters the circle -> BallPocketEnter + BallPocketed at once
```

**Rail top (review item 4).** `TableGeometry::RailTops` is a list of convex polygons (optionally minus the pocket cut disc) that covers the whole rail top incl. the corner and side pocket surrounds; each edge is classified (`Seam`, `Nose`, `CushionBack`, `OuterEdge`, `Facing`). Routing:
* airborne ball hits a rail-top plane or edge → GRI (`e_rt`, `mu_rt`), `BallRailTop`;
* low bounce (`< h_min`) on the **sloped cushion top** → rigid island with `Plane` features (cushion top, cap) and `EdgeLine` features (ridge, nose edge) until it drops back over the nose (→ `Airborne`, event mode), passes the outer boundary (→ `OffTable(Floor)`), or settles on the flat cap;
* settled on the **flat cap** → analytic surface segment with `SupportZ = RailTopZ` and `RailCapSurface()` friction; `SupportExit` across a `Seam` → context update; across `CushionBack` → rigid island (slope); across `OuterEdge` → the outer-boundary event follows; into the cut disc → `Airborne` (drops into the pocket hole; the edge pivot is not modelled);
* at rest on the flat cap → `BallOffTable(RestsOnRailOrFrame)` (WPA 2.6). A ball that touches the rail top and returns to the cloth or enters a pocket is not off the table (the rules derive that).

### 8.10 Termination and recording

* The loop ends when no valid event remains and no island is active — every ball `Stationary` (spin included, RUL 1) or terminal. `StopTime` is the last such time.
* Tracks: when a ball's segment is replaced at `t`, the previous `TrajectorySegment.T1 = t`; the new segment gets `Orientation0 = SegmentOrientationAt(prev.Orientation0, prev, t - prev.T0)` — **the orientation law of `Playback.h`** (review item 8): constant-axis segments in closed form, all others on the fixed grid `T0 + k·1 ms` with an exact-integral rotation vector per step and a final partial step. Playback uses the same function, so segment boundaries are bitwise continuous; `PlaybackCursor` caches the last grid point per ball, so per-frame cost is O(1) (VAL P6).
* `ShotResult::Events` are logged in processing order; `Record.Events` are appended in the same order via `AppendRecordEvent`, independent of the logging switches and of `EventLogOverflow`.

### 8.11 Tilted table and chalk-mark cling (HF 4.3, 4.5; WP-1, WP-3, WP-5, WP-6a)

* **Segments (WP-1).** The loop builds every segment with `MakeSegment(state, t, Spec, Surface, SupportZ, g, Params.Tilt)`. On a level table (`IsLevel`) this is bitwise the level segment (A-MOT-3), so every MOT/COL/VAL test is unchanged (HF-B10). On a tilted table a Sliding or Rolling phase becomes a chain of quadratic pieces (HF 4.5.3): each piece matches the exact pursuit solution (HF 4.5.2) in position and velocity at its start and in position at its end (the node), stays within `Tilt.Tolerance` (5e-5 m) of it, and ends either in the phase's own transition (with the HF 4.5.3 snaps) or in a node (`TiltChain::EndsInRefresh`). Nodes are exact, so errors do not accumulate. Airborne and PocketFall segments add the in-plane `g_t / 2` to `Accel2` (exact); Stationary and Spinning balls stay put (`ValidatePhysicsParams`: `|s| <= 0.7 mu_r` of cloth and rail cap); pivots neglect the tilt. Nap (HF 4.5.6, Later) changes only the rolling drive and resistance of a piece; its defaults are 0 for every cloth (section 15 row 30).
* **`TiltRefresh` (WP-6a).** The end slot of a piece that ends in a node queues `QueuedEventKind::TiltRefresh` with tier `Transition` and `FeatureKind = kTiltRefreshFeature` (unique key: one live end slot per ball). Processing (8.5): the exact node state starts the next piece through `ReplaceSegment`: version bump, observers recomputed on `[t, …)` with `IncludeFrom = true` (8.7), all slots of the ball re-predicted (8.3); logged as `ShotEventType::TiltRefresh` only with `LogTransitions`; never record-relevant. **Tie-break:** a refresh carries no impulse, never joins an exact-simultaneity group, and at an equal time pops after every contact (tier order) and after a motion-transition key of the same ball (`FeatureKind` 1 > 0); a contact that replaced the segment first has made it stale (`Arch_EventQueueTiltRefreshAfterContacts`). **Invalidation:** any event that replaces the ball's segment bumps its version, so the pending refresh is skipped lazily like any stale entry. Refreshes count against `MaxEvents` (a 10 s roll needs 14 pieces at the default tolerance, HF-T16).
* **Detection (WP-5) is unchanged:** pieces are quadratics, but a rolling piece's `Accel2` is in general not parallel to `Vel0`, so no predictor or swept bound may assume it (A-DET-6).
* **Islands (WP-3 solver, WP-6b hook; HF integration review).** A CLI island integrates the in-plane gravity `g_t` on every body (`CompliantIsland::SetInPlaneGravity`, called by `StartIsland` after `Reset`; the step applies it only when non-zero, so a level island stays bitwise the v1.1 island, incl. signed zeros; A-CLI-5). Without it the slope would vanish inside every island: a ball rolled into a cushion by the slope and held there in a rigid sustained contact, a cluster settling after the break, a pressing contact routed to an island by the exact re-anchor below. Nap is not applied inside islands; pivots neglect the tilt.
* **Exact re-anchor of events inside a piece (WP-6a, HF 4.5.3 and 9.2 item 6).** The resolver's state is `sim::BallStateForEvent`: position from the piece (consistent with detection, within `Tolerance` of the exact path), velocity and spin from the exact pursuit state at the event time (`EvaluateSegmentForEvent`, one 1-D solve). Approach tests (COL 2.4 step 1, fixed contacts) keep the piece velocity that detection used. A contact that approaches by the piece velocity but whose exact normal speed is `<= ApproachSpeedTol` is routed like a pressing contact to an island (8.8), which resolves zero-speed contacts, so it is never dropped (section 15 row 31). The piece-velocity error (up to 70 degrees of direction in the tail below about 1 mm/s) therefore never enters an impulse (HF-B14).
* **Cost.** Each refresh re-predicts that ball's slots: about 10-30 extra prediction rounds per moving ball at the default tolerance. AI rollouts may use `Tolerance = 5e-4` m (5-8 pieces per roll, 5.2). A-PERF-1 (WP-10) re-measures the 100 µs typical-shot budget with tilt on.
* **Chalk-mark cling (WP-6a with WP-3's functions; `PhysicsParams::ChalkCling`, default off).** At an event-mode ball-ball contact of balls 1 < 2 with normal `n_hat` (1 -> 2), the loop computes `chi_1 = ChalkMarkWeight(marks_1, q_1(t), R_1, +n_hat)`, `chi_2 = ChalkMarkWeight(marks_2, q_2(t), R_2, -n_hat)` and resolves with a copy of `Params.BallBall` whose `ClingFactor = ContactClingFactor(chi_1, chi_2, Params.BallBall)`; `ResolveBallBall` is unchanged. The orientations `q(t)` come from the orientation law of `Playback.h` (`SegmentOrientationAt` from `BallSlot::Orientation0`), which the loop maintains for every marked ball whatever the `RecordOptions` (AI rollouts too; section 15 row 28). Islands use `k_venue` (marks ignored inside the CLI, O-15). With `ChalkCling` off every contact uses `Params.BallBall.ClingFactor` (`k_venue` from the venue's `TableCondition`, 1.0 by default). Marks are deposited, faded and wiped by `rb::human` between shots, never inside `Run`.

---

## 9. Zeno guards and termination guarantees

| # | Guard | Where | Default |
|---|---|---|---|
| 1 | Approach test: only downward crossings of the gap function; separating touches never create events | `Detect.h` | — |
| 2 | Pressing rule: touching + zero normal speed + gap'' < 0 → event at `tau = 0` flagged `Pressing` → CLI island (never an impulse) | `Detect.h`, simulator | `ApproachSpeedTol` 1e-9 m/s |
| 3 | Micro-impacts: approach speed < `v_rest` → restitution 0 (ball-ball, cushions, rigid islands) | `ResolveBallBall`, `ResolveFixedContact`, rigid mode | 2e-3 m/s |
| 4 | Slate bounce termination: `v_z' < sqrt(2 g h_min)` → 0; `N_max` impacts per airborne sequence | `ResolveSlateImpact` | 2 mm, 10 |
| 5 | Zeno detector: same pair / ball-feature with 8 contacts within 10 ms → CLI island; histories cleared at island start and exit | simulator | 8 / 10 ms |
| 6 | Sustained contacts: compliant → Rigid mode when the Hertz transient is over (`SustainedSpeed`, `SustainedSteps`) or after `CompliantMaxDuration`; rigid islands run until contacts open or bodies rest; no impulse fallback | island (8.8) | 2e-3 m/s, 200 steps, 50 ms |
| 7 | Island exit hysteresis: no exit while a touching pair would re-trigger the pressing rule; ≥ 5 steps per island | `CompliantIsland::CanExit` | — |
| 8 | Island step budget per shot → stop all balls, `SimStatus::Aborted`, `IslandBudgetExceeded` | simulator | 200 000 steps |
| 9 | Event cap → stop all balls, `SimStatus::Aborted`, last events kept | simulator | 20 000 |
| 10 | Time horizon → `HorizonReached` | simulator | 600 s |
| 11 | Exact transition snaps (no micro-segments of 1e-15 s) | `ClassifyState`, `SegmentEndState` | residual check 1e-9 rel |
| 12 | Validity windows on every prediction (no ghost events) | `Detect.h` | — |
| 13 | Parameter validation (mu ≤ 0, e ∉ [0, 1], `Origin == Unset`, bad inertia factor, `|Slope| > 0.7 mu_r` …) | `ValidatePhysicsParams`, `Run` | ROB-06 |
| 14 | Tilt chains terminate, no Zeno: the tail piece (`\|x\| <= x_tail = sqrt(eps (K - \|G\|) / (4 c_s))`) runs to the exact stop; every other piece either ends the phase (`Delta = T_rem`) or, since `\|x_i\| > x_tail`, lasts at least `Delta_min = min(RefreshMaxInterval, x_tail / (2 (K + \|G\|)), (eps x_tail / (2 C_int c_s K \|G\|))^(1/3))` (the root of `a D^3 + b D - c` is at least `min(c / 2b, (c / 2a)^(1/3))`). A phase lasts `T_stop <= \|x_0\| / (K - \|G\|)` with `K - \|G\| >= K / 2` (validity rule), so it has at most `T_stop / Delta_min + 1` pieces (10-14 at the defaults, HF-T16); refreshes count against `MaxEvents` | `TiltPieceDuration`, `MakeSegment(…, Tilt)`, simulator | 5e-5 m, 2 s |

---

## 10. Numerical tolerances (single table)

All physics tolerances are fields of `rb::NumericsConfig` (`rb/Core/Tolerances.h`, recorded with every shot); rules tolerances are `rb::RulesTolerances`. Model step sizes and thresholds that are physics decisions are listed with their home struct.

| Name (code) | Default | Used by | Source |
|---|---|---|---|
| `EpsZ` | 1e-9 m | airborne classification (relative to `SupportZ + R`) | MOT A.8 |
| `EpsV` | 1e-9 m/s | zero speed / slip | MOT A.8 |
| `EpsWTimesRadius` (`eps_w = /R`) | 1e-9 m/s (3.5e-8 rad/s) | zero spin | MOT A.8 |
| `SnapResidualRel` | 1e-9 | snap diagnostics | VAL 5.8 |
| `ContactTol` (`eps_touch`) | 1e-9 m | touching test, joining floor, exit hysteresis | COL 3.4, VAL 5.4 |
| `ApproachSpeedTol` (`v_eps`) | 1e-9 m/s | pressing rule, exit hysteresis | COL 3.6 |
| `TangencyTolPerLength` (`eps_f = 2(R1+R2)·`) | 1e-9 m | graze = miss | COL 3.4 |
| `OverlapGuard` | 1e-6 m | corrupt-state diagnostic | COL 3.6 |
| `SegmentParamSlack` | 1e-9 m | cushion joints watertight | VAL 5.7 |
| `RootTrimRel` | 1e-14 | degree reduction | COL 3.3 |
| `RootTimeTol`, `RootMaxIterations` | 1e-13 (scaled), 100 | safeguarded Newton | COL 3.3/3.4 |
| `RestSpeed` (`v_rest`) | 2e-3 m/s | e = 0 micro-impacts | COL 0.4 / 7.3 |
| `ZenoContactCount`, `ZenoWindow` | 8, 10 ms | Zeno detector | COL 7.3 |
| `MaxEvents` | 20 000 | hard cap | COL 7.3 |
| `TimeHorizon` | 600 s | safety net | VAL 5.4 |
| `CompliantMaxDuration` | 50 ms | compliant phase cap → Rigid mode | COL 3.9.2 (section 15 row 20) |
| `MaxIslandSteps` | 200 000 | per-shot island budget | architecture (review item 1) |
| `CushionSlipEps` (`s_eps`) | 1e-6 m/s | Mathavan friction off | COL 4.5 |
| `PivotMinSpeed`, `PivotSimpsonPanels` | 1e-4 m/s, 16 | pivot time integral | COL 5.4 |
| `LineCrossEps` | 1e-6 m (= `eps_line`) | line-crossing observers | RUL 2.2 |
| `LeaveDistance` | 5e-4 m (= `eps_leave`) | continuesInitialFreeze, island record re-arm | RUL 3.6 |
| `SampleTolerance`, `SampleMaxInterval` | 10 µm, 10 ms | adaptive Sampled segments (playback only) | architecture (review item 35) |
| `TiltParams::Tolerance` (`eps_tilt`) | 5e-5 m (AI rollouts may use 5e-4) | max deviation of a tilt chain piece from the exact path | HF 4.5.3 |
| `TiltParams::RefreshMaxInterval` | 2 s | longest tilt chain piece | HF 4.5.3 |
| pursuit solve (`EvaluatePursuit`) | `|dlam| <= 1e-15 max(1, lam)` | 1-D solve of `t(lam) = t` (fixed, not a parameter) | HF 4.5.2 |
| `SlateParams::MinBounceHeight`, `MaxBounces` | 2 mm, 10 | bounce Zeno | MOT C.4 |
| `CliParams::TimeStep`, `ExitZeroForceSteps`, `JoinFactor`, `SlipRegularization` | 1 µs, 5, 1.2, 1e-3 m/s | CLI | COL 3.9 |
| `CliParams::RigidTimeStep`, `RigidIterations` | 20 µs, 4 | rigid mode | COL 5.6 / 6.2 |
| `CliParams::SustainedSpeed`, `SustainedSteps` | 2e-3 m/s, 200 | compliant → rigid switch | architecture (review item 1) |
| `CliParams::TsujiAlpha` | < 0 = derived (0.03689 at e_b = 0.95) | CLI damping | COL 3.9.3 (review item 27) |
| `CushionParams::MathavanSteps`, `MathavanMaxBisections`, `MathavanSplitAtSlipReversal` | 32 (placeholder until the WP-4 accuracy gate), 60, on | Mathavan RK4 | COL 4.5 (section 15 row 22) |
| `RulesTolerances::TieWindow` | 0.5 ms | first-contact ties, rail presumption | RUL 3.6 |
| `…::Frozen` | 0.1 mm | frozen declarations (start snapshot) | RUL 3.6 |
| `…::Leave`, `Line`, `PlacementOverlap` | 0.5 mm, 1 µm, 0.1 µm | rules predicates | RUL 3.6 |
| `…::PushDuration`, `GrazeAngle`, `FrozenEnvelope` | 4 ms, 75°, 5 mm | double hit / push | RUL 3.6 |
| `…::SettleWindow`, `LagTie`, `SpotCueBallGap` | 5 s, 0.5 mm, 1 mm | hanging balls, lag, spotting | RUL 3.6 |

Physics must never use the human-scale rules tolerances (e.g. `eps_frozen` 0.1 mm) for contact decisions (COL 3.4); the only crossings are `LineCrossEps`/`LeaveDistance`, which are pinned equal to the rules values (`Arch_RulesTolerancesFromSpec`), and `ShotContext.FrozenTolerance` for the start snapshot and the frozen target `f`.

---

## 11. Determinism strategy

1. **Event order**: strict total key (section 8.3); exact double comparison; live keys unique; no insertion-order or energy-based tie-breaks. Exactly simultaneous events that share a ball are resolved together in one island; the remaining equal-time events commute.
2. **Island order**: contacts in an id-independent geometric key order, Jacobi force accumulation (compliant) and key-ordered sweeps (rigid) — results are invariant under ball-id permutations (COL:CL-8, VAL:BRK-04), not merely under insertion order.
3. **No hidden state**: no globals, statics, thread-locals; `Simulator` workspace is private; `TableGeometry` read-only.
4. **No randomness** in the loop; game-seeded randomness only through `rb::Rng` with explicit seeds (racks, gaps, bar-ball masses).
5. **Floating point, pinned three times**: (a) CMake `/fp:precise` (MSVC), `-ffp-contract=off` (GCC/Clang), no `-ffast-math`, no `long double`; (b) `BilliardsCore.Build.cs`: `FPSemantics = FPSemanticsMode.Precise` (verify the name in UE 5.8; `bUseRTTI = false`, `bEnableExceptions = false`); (c) `Private/rb/Core/FpGuard.h` (`#pragma float_control(precise, on)`, `#pragma fp_contract(off)` / `#pragma clang fp contract(off)`) as the first include of every core `.cpp`, checked by the root `CMakeLists.txt` at configure time. No parallel reductions inside one shot. All transcendental calls go through `rb/Math/Scalar.h`; on one platform/CRT this is bitwise reproducible. Cross-platform bitwise identity needs portable implementations behind the same wrappers (open issue O-2).
6. **One parameter source**: `MakePhysicsParams(TableSpec)`; `Run` rejects `ParamsOrigin::Unset`, so the game, the AI and rbsim cannot silently simulate different cloths on the same table (review item 12).
7. **ROB-10** compares event-log hashes between Debug and Release standalone builds and the UE-module build; the standalone half runs as soon as the first shot runs (WP-6a), the UE half as soon as the UE project builds the module.
8. **Player model** (`rb::human`): every noise value is a pure function of explicit keys (counter-based `HashKeys`, no generator state); the Q1 streak history is itself a pure function of the key (recursion from draw 0, cached in `NoiseHistory`; a cache that does not match the key is rebuilt, so it can never change a draw); the fallback of the guard is exact integer arithmetic; `ExecuteStroke` and `SampleHand` are pure (the intoxication hook is exactly neutral at 0, the get-down count `AddressIndex` comes from the input log); transcendental calls go through `Scalar.h`. A replay stores the final `CueStrikeInput` and plays it back exactly; re-executing the stroke from the input log (HF-B01) is bitwise on one platform / CRT, like the physics (O-2).
9. **Tilted table**: the chain of pieces is a deterministic function of the state at the phase start and `TiltParams` (nodes from the exact solution, fixed Newton iteration rules); refresh events are ordered by the strict key like every other event.

---

## 12. Performance strategy and memory budget

| Measure | Effect |
|---|---|
| Per-ball time bases, re-predict only balls whose state changed | O(N) predictions per event instead of O(N²) |
| Swept-AABB broad phase over validity windows (never velocity-direction culling) | most pairs rejected before the quartic |
| Root isolation on the scaled window only; degree reduction; no complex roots | ~0.1-0.4 µs per pair (VAL 5.5) |
| Stationary-stationary pairs never tested; terminal balls removed | quiet tables cost nothing |
| Lazy-deletion heap (512 entries) with O(n) compaction | no per-event scans of all pairs |
| CLI only for real clusters (δ_cl), sparse contact list, uniform grid for large islands | break ≈ 1 ms (COL 3.9.5 estimate; PERF-02) |
| Sustained contacts in 20 µs rigid mode (not 1 µs compliant) | a 100 ms pressing contact costs ~5 000 cheap steps, not 10⁵ (D-12b, Z-3 < 1 ms) |
| Mathavan: slip-reversal splitting (4th-order) and the smallest N meeting M-2..M-4 within 2e-4 (expected 20-40, placeholder 32); micro-benchmark gate ≤ 2 µs per hit | 4-8 rail contacts ≈ 10-16 µs, inside the 100 µs budget; the AI uses the referee's model (no Han "fast mode") |
| Recording switchable (`RecordOptions`) and bounded (`ResultCapacity`), adaptive island sampling | AI rollouts without trajectories/states; no track overflow on long islands |
| No virtual calls, no allocation, AoS structs of ≤ 24 balls | cache-friendly, predictable |
| Tilted table: chains of quadratic pieces with exact nodes instead of numerical integration; detection unchanged; AI rollouts at `Tolerance = 5e-4` | about 10-30 extra prediction rounds per moving ball (5-8 for AI rollouts); budget re-measured by A-PERF-1 |

Memory (estimates, verify with PERF): `Simulator` workspace ≈ 60-65 KB (heap 12 KB, per-ball slots ≈ 17 KB, Zeno table ≈ 7 KB, `CompliantIsland` ≈ 22 KB with the sparse 192-entry contact list and 32 bounded features) — P5 target 64 KB; a fully reserved `ShotResult` with default `ResultCapacity` ≈ 5 MB (events 4096 × ~400 B, segments 24 × 512 × ~250 B, record 4096 × ~120 B, cue tips 64); AI capacities (256 events, 0 segments, 1024 record events) ≈ 0.2 MB. v1.2: `MotionSegment` grows by the `TiltChain` (≈ 72 B, so ≈ 250 B per segment and ≈ 320 B per `TrajectorySegment`; a fully reserved `ShotResult` ≈ 5.8 MB), `SimBall` by 8 chalk marks (≈ 330 B, `SimInput` ≈ +8 KB).

---

## 13. How Unreal consumes the core

The UE game module (`Source/RawBreak`, not part of the core) owns:

1. **Coordinate adapter** (UE 5.6; tests UE T14-T16): table-local UE axes `X = x`, `Y = -y`, `Z = z`, centimetres: `p_UE = 100 (x, -y, z)`, `v_UE = 100 (v_x, -v_y, v_z)`, `w_UE = (-w_x, w_y, -w_z)` (pseudovector), `q_UE = (q_w, -q_x, q_y, -q_z)`; directions for the strike: `phi = atan2(-Y_UE, X_UE)` of the cue direction. Use `FQuat`, never `FRotator`. **The core never contains this mapping.**
2. **Table meshes** from `TableGeometry`: `BuildNoseOutline`, `Facings`, `PocketGeometry` (slate cut `r_p`, drop rounding `r_d`, shelf, liner/back wall up to `WallTopZ`, undercuts), `CushionProfile` + `RailTops` (the same convex polygons the physics uses, incl. pocket surrounds and cut discs), `Sights`, `Landmarks`. `rbsim --geometry` exports the same data as JSON for the DCC pipeline (UE 6.7).
3. **Physics parameters**: `MakePhysicsParams(TableSpec, TableCondition)` once per table, the condition from `rb::human::MakeVenueTableCondition` (the venue's seeded slope and ball cling; HF 4.5.5, HF-41) (optionally overridden through `ParamTable.h` keys from a developer console); never a hand-built `PhysicsParams`.
4. **Stroke input → strike** (UE 5.4, HF 3): tip speed from the raw-input fit at the contact crossing, azimuth/elevation, cue-axis offsets `(A, B)`, steering, timing → `rb::human::IntendedStroke` → `ExecuteStroke` (attributes, situation from IK / pressure / fatigue / sweat, `TipState`, `CueBodyState`, the other balls, `NoiseKey` + the shooter's `NoiseHistory`) → `CueStrikeInput` (shooter-relative, no mirroring; contact offsets with the current dome radius; `TipTouchesCloth` from the executed pose, which the UE cue-body model may override in Sim mode). The AI uses `SyntheticHand` + `ExecuteStroke` (never a hand-built `CueStrikeInput`, HF-B07). `SampleHand` drives the rendered cue before contact (what you see is what hits, HF 3.7); an aborted stroke that showed the per-shot ramp advances `ShooterShotIndex` and the `NoiseHistory` (HF-B13). The lag submits two `StrikeRequest`s in one `SimInput` (networked play records both strokes first).
5. **Simulation**: `Simulator::Run` on a worker thread at stroke contact; keep the `ShotResult` for playback and replay.
6. **Playback**: per ball `StateAtCursor` (monotone, O(1)) → position + orientation (the orientation law, bitwise continuous across segments) → adapter → `SetActorLocationAndRotation`. The cue follows `CueTipAt(strike, t)` (tip dome center + direction), so the animated cue matches the double hits the rules judged. `Sampled` segments cover islands and pivots; `Terminal` segments start at capture or at leaving the table.
7. **Audio / VFX** from `ShotResult::Events` (UE 8.1-8.3): `CueStrike` (+ `Miscue` flag), `TipRecontact`, `BallBall`, `BallCushion`, `BallJaw`, `BallRailTop`, `BallSlate`, `BallLiner`, `BallPocketRim`, `BallPocketed`; `NormalSpeed` drives level and cutoff; rolling rumble from the segment state.
8. **Rules flow**: `rules::Match`; `BuildRulesTable(TableGeometry, radii)` once per table + ball set; UI from `GetShotConstraints` (placement choices, free shot, visits, doubles member); `ValidateDeclaration(…, PlacedCueBall)` before the stroke; `ShotContext` from the game; after the simulation `DeriveShotFacts(Result.Record)` → `EvaluateShot(Config, Table, …)` → `ApplyShot` / `ApplyOption`.
9. **AI**: a thread pool of `Simulator` objects, one shared `TableGeometry` and `PhysicsParams`, `RecordOptions` for rollouts; the same `EvaluateShot` as the referee (RUL pitfall 17).
10. **Build**: `BilliardsCore.Build.cs` pins `FPSemantics = Precise` (section 11).
11. **Player-model state and chores** (HF 4.7): the career save and every replay header hold `TipState`, `CueBodyState`, `ChalkCube`, habits, attributes, the per-shooter `NoiseHistory`, the ball chalk marks (`SimBall::ChalkMarks`) and the venue seeds; after each shot `ApplyShotToEquipment`; chores through `rb/Human/Chores.h` (durations, partial state, habitual results); the diagnosis loop of HF 3.9 within 3 s (HF-B06); `ProductConfig` from the product settings (HF 7); `StrokeIntoxication` (0 in V1) into `StrokeSituation::Intoxication`, `MoneyGameStakes` into `PressureInputs::Stakes` in money games, `NoiseKey::AddressIndex` = earlier get-downs on the shot (input log); alcohol camera effects (UE 4.8) are cosmetic in V1 and run only while standing or walking, never while down on a shot or on the cue, and with `AlcoholMode::Mechanic` a view sway while down must be driven by `SampleHand`'s drift (what you see is what hits, HF-20); the pixel tells of HF-B08 are checked by UE Automation.

---

## 14. rbsim (WP-7)

```
rbsim [options]
Scenario:
  --table 9ft-pro|9ft-tight|8ft-pro|8ft-home|7ft-bar|7ft-78|7ft-true   (default 9ft-pro)
  --cloth table|default|fast|bar           cloth preset (default: the table's, via MakePhysicsParams)
  --balls standard|divebar|oldbar|snooker|blackball  ball set (default standard)
  --rack none|8ball|9ball|10ball|14.1      rack the object balls (rules::GenerateRack; needs WP-9 + WP-2)
  --rack-gap none|tight|wooden|sloppy|mixture  rack micro-gaps (default wooden)
  --seed N                                 rack / ball-set seed (default 1)
  --ball ID:X,Y                            ball at rest at (X, Y) [m] (repeatable; default CB on the head spot)
  --state ID:X,Y,Z,VX,VY,VZ,WX,WY,WZ       explicit initial state (repeatable)
Strikes (t = 0):
  --cue playing|break|jump|house  --speed V [m/s]  --strike-ball ID  --aim DEG  --elevation DEG
  --offset A,B (contact-point offsets / R)   --axis-offset A,B (cue-axis offsets / R)   --no-squirt
  --strike ID:V,AIM,ELEV[,A,B]             additional strike (the lag: two strikes in one simulation)
Parameters:
  --param KEY=VALUE                        override any PhysicsParams field by its ParamTable key (repeatable)
  --list-params                            print every key, its value for the chosen table and its unit
                                           (tilted table: --param tilt.slope_x=0.001 --param tilt.slope_y=0; cling:
                                            --param ballball.k_cling=1.3, ballball.chalk_cling=1 with dumped marks)
Replay:
  --dump-input FILE                        write the complete SimInput (rbsimInput schema v1)
  --in FILE                                simulate a dumped SimInput (scenario options ignored)   [TODO(WP-7)]
Output:
  --out FILE (default stdout)  --dt S (sample interval, default 0.01, 0 = none)  --no-trajectories
  --no-states  --record  --facts (needs WP-8)  --compact  --bench N (timing to stderr)  --geometry (table export)
Exit code: 0 = SimStatus::Ok, 2 = usage / setup error, 3 = simulation not Ok, 1 = I/O
```

Output JSON (`"rbsim": 2`; numbers with 17 significant digits; non-finite → `null`):

```
{ "rbsim": 2, "coreVersion": "0.1.0",
  "input": { "table", "gravity", "cloth": {mu_s, mu_r, alpha_sp}, "strikes": [{ball, V, theta, phi, a, b, cueMass}],
             "paramOverrides": ["key=value", ...], "seed" },
  "status": "Ok|InvalidInput|Aborted|HorizonReached|NotImplemented", "stopTime",
  "diagnostics": { inputError, eventsProcessed, staleEventsSkipped, predictions, islands, islandSteps,
                   islandRigidSwitches, islandBudgetExceeded, zenoTriggers, overlapWarnings, missedEvents,
                   tiltRefreshes },
  "strikeResults": [ { ball, error, impulse, squirt, miscue, cueSpeedAfter, state } ],
  "events": [ { t, type, a, b, feature, sub, flags, normal: [3], vn, jn, jt, cut, value,
                from?, to?, pre?: [state, state], post?: [state, state] } ],
  "balls": [ { id, radius, mass, initial: state, final, finalState: state, pocket, segments,
               samples: [[t, x, y, z, qw, qx, qy, qz, stateIndex], ...] } ],
  "cueTips"?: [[strike, t0, t1, x0, y0, z0, dx, dy, dz, speed0, decel, sampled], ...],
  "record"?: { events, tipContacts, truncated, stopTime, facts?: {...} },
  "geometry"?: { name, length, width, noseHeight, railTopZ, railWidthTotal, noses, jawArcs, facings,
                 pockets (incl. wallTopZ), railTops (kind, cushion, pocket, planePoint, planeNormal, vertices,
                 edges, cutCenter?, cutRadius?), sights, profile, noseOutline } }
state = { "r": [3], "v": [3], "w": [3], "state": "Rolling" }
```

Input dump (`"rbsimInput": 1`): `tableSpec` (every `TableSpec` field), `environment` (lamp underside, footprint), `params` (every `ParamTable` key → value), `balls` (id, radius, mass, inertia, state, `q`, `chalkMarks` [v1.2, WP-7]), `strikes` (ball, V, theta, phi, a, b, lambdaOverride, squirt, tipTouchesCloth, full `cue`), `context` (in hand, placed position, template, shot clock, foot on floor, frozen tolerance, non-tip contacts), `record` (all `RecordOptions`). `--in` reads exactly this schema (WP-7 implements the reader; the option exists and reports "not implemented").

`rbsim` is also the calibration harness of MOT implementation note 14 (stopping distance, stun distance, squirt, hop height via `--state`/`--speed`/`--param` + samples), the replay tool for UE bug reports (`--dump-input` / `--in`) and the source for visual debugging.

---

## 15. Spec conflicts and decisions

| # | Topic | Conflict | Decision |
|---|---|---|---|
| 1 | Minimum bounce height | MOT C.4 (verified) 2 mm vs VAL 5.2 0.5 mm (visual criterion) | **2 mm** default (the verified physics spec wins; a 0.5 mm hop is below the cloth-fibre scale and doubles the bounce events); VAL:AIR-02 pins 0.5 mm explicitly |
| 2 | Resting speed `v_rest` | COL 2e-3 m/s vs VAL 1e-3 m/s | **2e-3 m/s** |
| 3 | Event cap | COL 20 000 vs VAL 100 000 | **20 000** (plus the island step budget) |
| 4 | Zeno detector | COL 8 contacts / 10 ms vs VAL 16 / 0.1 ms | **COL** |
| 5 | Equal-time tiers | COL: ball-ball < cushion < pocket < slate < transitions; VAL: transitions first | **COL order**, plus `Strike` first and `Boundary` before transitions |
| 6 | Cluster solver | COL CLI (Hertz/Tsuji) vs VAL projected Gauss-Seidel impulses with `kClusterTol` 1e-7 m | **CLI**; `kClusterTol` replaced by the speed-dependent `delta_cl` (0.4 mm at 1 m/s): a fixed 0.1 µm tolerance would treat a 10 µm gap as sequential although COL 3.9.4 shows it is not (CL-5) |
| 7 | Gravity in tests | 9.80665 (MOT/COL) vs 9.81 (VAL test tables) | default 9.80665; VAL tests pin 9.81 |
| 8 | Ball mass | 0.17009713875 kg default (EQP/COL) vs 0.170 in MOT tests | default 6 oz; MOT tests pin 0.170 |
| 9 | Tip dome radius | EQP nickel 10.6 mm vs MOT "≈ R/3" | 10.6 mm in `CueSpec`; MOT:T-B11 pins R/3 |
| 10 | Jump cue mass | MOT 9 oz (TUNING default) vs EQP ≈ 10 oz | physics preset 9 oz; `kJumpCueMass` keeps 0.2835 as equipment reference |
| 11 | Shaft end mass | MOT m/m_e = 20 (8.5 g) vs EQP "typical 5 g" | 8.5 g playing preset (TUNING) |
| 12 | Resting on a cushion | VAL ROB-05 "restingOnCushion" flag vs COL pressing rule + CLI | COL mechanism (pressing → island → rigid sustained mode); `Resting` event flag reports it |
| 13 | WPA rail-speed "firm" stroke | VAL CUSH-05 flags if 4 lengths need > 5 m/s; COL CAL-1 needs 6-7 m/s with the default `e_c` law | keep the COL law; CUSH-05 is a Tier-D flag expected to fire until calibrated (COL OQ 10) |
| 14 | Cue speed limit | MOT B.9 input clamp 12 m/s vs VAL 5.11 API clamp 15 m/s | core rejects > 15 m/s (`kMaxCueSpeed`), UE clamps to 12 |
| 15 | Transition snap tolerance | pooltool 1e-12 abs vs VAL 1e-9 rel | exact construction; 1e-9 relative residual check |
| 16 | Pocket/cushion numbering | RUL P0..P5/C0..C5 vs EQP names | RUL numbering (counter-clockwise), EQP names mapped in `Ids.h`; pocketless mapping C0/C2/C3/C5 |
| 17 | Rules ball mass 0.163 kg (RUL 13) | rules do not use mass | ignored |
| 18 | Double hit / push detection | RUL pitfall 21 requires a physical cue; MOT treats the strike as instantaneous | follow-through tip path + tip-slot events against every ball; compliant `IslandTip` inside islands (section 8.6) |
| 19 | Simultaneity window | COL 3.7: events within 1e-12 s are simultaneous and go to an island | exact ordering; exactly equal times sharing a ball are islanded (8.3); near-simultaneous contacts are islanded by the `delta_cl` BFS, a window of ≈ `T_H` that subsumes 1e-12 s |
| 20 | Island duration | COL 3.9.2: after 50 ms fall back to sequential impulses; COL 7.3: islands run until contacts open or balls rest | **COL 7.3**: 50 ms caps only the compliant phase (then Rigid mode); no impulse fallback (it cannot resolve zero-speed contacts, COL pitfall 16); per-shot step budget as the hard stop |
| 21 | CLI iteration order | COL pitfall 10: fixed iteration order by ball id; COL:CL-8 / VAL:BRK-04: bit-identical under id permutation | geometric contact-key order + Jacobi accumulation (deterministic **and** id-independent) |
| 22 | Mathavan step count | COL 4.5: N = 200 RK4 steps, splitting optional; VAL 7.4: ≤ 100 µs per shot | splitting on; default N = smallest value meeting M-2..M-4 within 2e-4 (WP-4 gate, placeholder 32); N = 200 only in M-6; AI uses the same model |
| 23 | Rack gap distribution | COL 3.9.5 uniform per-contact presets vs VAL 5.6 mixture (mostly 0 ± 20 µm, a few 0.1-0.5 mm) | COL presets are the game presets; `kRackGapMixture` reproduces VAL 5.6 for break statistics; anchor ball stays on the spot; algorithm in `RackLayout.h` |
| 24 | Inertia | all spec formulas assume `I = 2/5 m R²`; hard requirement "per-ball inertia" | generalised with `k = I/(m R²)` (section 7.2); spec values are the `k = 2/5` case |
| 25 | Rail-top motion | COL 6.2: rigid CLI mode on the sloped top "until it drops back or passes the outer edge"; resting on the flat cap = off table | sloped top: rigid island; flat cap: analytic surface segments (`SupportZ = RailTopZ`) with `SupportExit` events, so resting on the cap costs no island steps |
| 26 | Frozen-ball target `f` | RUL F7 "the shot goes into f" is a rules judgement | physics reports `f` = frozen ball with the largest positive `n_hat · d` (none if shooting away), the gap to it and "touched another ball or rail yet" on every tip-contact event; the rules apply the envelope |
| 27 | Airborne pocket walls | COL 5.3 lists liner and torus contacts only for balls inside a pocket | predicted for every airborne ball whose swept bounds reach a pocket's `a_d` cylinder (break hops, jump shots, flying over a pocket); back wall up to `RailTopZ` |
| 28 | Orientation of marked balls | HF 4.3: move `IntegrateSegmentOrientation` from Playback (WP-7) into WP-1 so the loop can integrate orientation | **Not moved.** The loop already evaluates the orientation law of `Playback.h` (`SegmentOrientationAt`, 8.10); for `ChalkCling` it maintains `Orientation0` of every marked ball whatever the `RecordOptions` (8.11). No ownership change, one orientation law. |
| 29 | Tilt parameters | HF 4.5.7: `PhysicsParams::TableTilt {…}` | `rb::TiltParams` in `Motion.h` (WP-1: `MakeSegment` needs it), held as `PhysicsParams::Tilt`; the venue table's slope and ball cling arrive through `TableCondition` and `MakePhysicsParams(Spec, Condition)`, so the single parameter source (section 11 item 6) stays single. |
| 30 | Nap defaults | HF 4.5.6: napped bar cloth `zeta_n` 2e-4, `eta_n` 0.1 (EST, "Later") | defaults **0** for every cloth preset (HF-B10 and "Later"); setting them per cloth preset is a reviewed default change once calibrated (HF 9.3: no measured nap effect on snooker cloth). |
| 31 | Approach test inside tilt pieces | HF 4.5.3: the approach test keeps the segment velocity, the impulse uses the exact state | adopted (`BallStateAt` for tests, `BallStateForEvent` for resolvers); the rare contact that approaches by the piece but not by the exact velocity is routed like a pressing contact to an island (resolves zero-speed contacts), never dropped (8.11). |
| 32 | Simultaneity and transition-tier events | v1.1 8.3 grouped every exactly-equal valid entry sharing a ball, incl. transitions | only contact tiers (`Strike` … `Boundary`) form groups; `Transition`-tier entries (transitions, `TiltRefresh`) carry no impulse, never group, and pop after the contacts of the same instant (an exact coincidence is rare, but a refresh is not a contact and must never create an island). |
| 33 | `ExecuteStroke` signature | HF 3.1 sketch: `(…, Span<const BallObstacle>, const NoiseKey&, const HumanParams&)` | `(…, const BallObstacle*, int, const NoiseKey&, const NoiseHistory&, const HumanParams&, const TipParams& = {})`: the Q1 streak guard needs the shooter's history; pointer + count as elsewhere in the core API (no `std::span`). |
| 34 | `rb::human` header list | HF 3.11: `NoiseHash.h, HumanModel.h, TipState.h, CueState.h, Progression.h` | ten headers (7.5): `Skill.h` (attributes, parameters, situation), `BallMarks.h`, `AiProfiles.h`, `Chores.h` and `Venue.h` added so that each state model has one header; HF 3.11 updated. |
| 35 | Per-shot draws | HF v1.1 3.2: stratified bags of 8 | product-owner decision Q1: streak-guarded independent draws (HF v1.2 3.2); `HumanParams::StreakGuard` replaces `Stratify`; the synthetic-hand channels 20-22 use the same guard. |
| 36 | Chalk-mark cling inside islands | HF 4.3: cling per ball-ball contact | event-mode contacts only (V2 feature); CLI islands use `k_venue` (O-15). |
| 37 | Alcohol (Q2) | HF v1.2: an alcohol mechanic as attribute modifiers (`EffectiveAttributes`, `AlcoholParams`); product owner: cosmetic for V1, but an intoxication input in the stroke situation that `ExecuteStroke` can map onto drift, tremor and nerve | `StrokeSituation::Intoxication`, set by `StrokeIntoxication(ProductConfig, level)` (0 under `CosmeticOnly`), mapped in HF v1.3 3.4-3.5 with placeholder `HumanParams::Intoxication*`; exactly 1 at `I` = 0; the attribute modifiers are removed (one mechanism, acting on the stroke where it is visible). |
| 38 | Streak fallback | HF v1.2 3.2: floating-point `(A[j] + (m u - j)) / 8` rounds and can land in an excluded eighth | exact integer arithmetic (`NoiseHash.h`, HF v1.3 3.2, `streak.py` with the counter-example). |
| 39 | Drift / tremor key | HF v1.2 3.2: watchable processes keyed by `ShotIndex`, identical on every get-down of a shot (a dry run reveals `D(t)`) | `ProcessShotKey = ShotIndex \| AddressIndex << 32` (`NoiseKey::AddressIndex`); the v1.2 key for the first get-down (HF-T04, HF-S03 unchanged). |
| 40 | Tilt inside CLI islands | HF 4.5 defines the tilt for analytic segments only; the v1.2 island had level gravity | `CompliantIsland::SetInPlaneGravity` (WP-3), set by WP-6b after `Reset`, applied only when non-zero; no nap in islands (8.11). |

---

## 16. Traceability (spec section → implementation)

### 16.1 physics-motion-and-cue (MOT)

| Section | Header :: function / type | WP |
|---|---|---|
| 0.1-0.3 frame, symbols, slip | `Math/Vec3.h`, `Core/Constants.h`; `Motion.h::SlipVelocity` | 0/1 |
| A.1 states | `Core/Ids.h::MotionState`, `BallState.h` | 0/1 |
| A.2 friction models, `alpha_sp` | `Motion.h::ClothParams`, `SpinFrictionCoefficient` | 1 |
| A.3-A.7 closed forms, w_z clamp | `Motion.h::MakeSegment`, `EvaluateSegment`, `PositionAt`, `OmegaZAt` | 1 |
| A.5 Coriolis invariant | `Motion.h::CoriolisInvariant(k)` | 1 |
| A.8 transition times, classification | `Motion.h::MakeSegment` (TauEnd), `SegmentEndState`, `ClassifyState(SupportZ)` | 1 |
| A.9 parameters and presets | `Motion.h::kCloth*`, `ClothParamsFor`; `TableSpec::Cloth` → `MakePhysicsParams` | 1/2/6a |
| B.1 inputs, validation | `CueStrike.h::CueStrikeInput`, `ValidateCueStrike`, `kCueOffsetValidLimit`, `kMaxCueSpeed` | 1 |
| B.2 cue frame | `MakeCueFrame`, `CueFrame` | 1 |
| B.3 contact point, aim conversion | `CueContactPoint`, `AimToContactOffset` | 1 |
| B.4 miscue | `MiscueLimit`; `StrikeResult::Miscue` | 1 |
| B.5 impulse (grip / miscue) | `StrikeCueBall` | 1 |
| B.6 spin, separation margin | `StrikeCueBall`, `SeparationMargin(k)` | 1 |
| B.7 squirt | `SquirtAngle(k)`, `CueStrikeInput::SquirtEnabled` | 1 |
| B.8.1-B.8.2 pinch model | `PinchParams`, `PinchLambda`, `CueStrikeInput::LambdaOverride` | 1 |
| B.8.3 slate reaction at t = 0+ | `StrikeCueBall` (uses `Slate.h::ResolveSlateImpact`) | 1 |
| B.8.4 masse check | `CoriolisInvariant` (tests) | 1 |
| B.9 equipment ranges | `Equipment/Cue.h` presets | 2 |
| C.1 ballistic flight | `MakeSegment` (Airborne, PocketFall) | 1 |
| C.2 landing | `Slate.h::LandingTau`, `Detect.h::PredictSlateLanding` (end slot), `RouteLanding` | 1/5/6b |
| C.3 slate impact | `Slate.h::ResolveSlateImpact` | 1 |
| C.4 Zeno bounce guard | `SlateParams`, `ResolveSlateImpact(BounceIndex)` | 1 |
| C.5 parameters | `SlateParams`, `PinchParams::PinchRestitution` | 1 |
| Impl. notes 1-2 (segments, snaps) | `MotionSegment`, `SegmentEndState`, `ClassifyState` | 1 |
| Impl. note 6 (strike includes slate) | `StrikeCueBall`; simulator never schedules it | 1/6a |
| Impl. notes 7-9 (squirt once, Zeno, degenerate divisions) | `StrikeCueBall`, `ResolveSlateImpact` | 1 |
| Impl. note 10 (determinism) | section 11 | 0 |
| Impl. note 11 (UE boundary) | section 13 (UE module) | UE |
| Impl. note 12 (energy check) | `MechanicalEnergy` (debug checks, VAL ROB-12) | 1/6a |
| Impl. note 14 (calibration harness) | `rbsim` (`--param`, `--state`, samples) | 7 |

### 16.2 physics-collisions (COL)

| Section | Header :: function / type | WP |
|---|---|---|
| 0.4 parameter table | `BallBallParams`, `CushionParams`, `CliParams`, `SlateParams`, `PocketContactParams`, `NumericsConfig` | 3/4/1/0 |
| 1 event types | `ShotResult.h::ShotEventType`, `Detect.h::TableFeatureKind`, `EventQueue.h::EventTier` | 6a/5 |
| 2.1-2.3 impulse model, friction | `BallBall.h::ResolveBallBall`, `BallBallFriction` | 3 |
| 2.4 algorithm, cutAngle | `ResolveBallBall`, `CutAngle`; step 6 `Slate.h::ApplyTableReaction`; steps 7-8 simulator | 3/1/6a |
| 2.5 vertical impulses | `ApplyTableReaction` | 1 |
| 2.6-2.7 throw oracle, spin transfer | tests (BB-3..BB-7) | 3 |
| 2.8 Mathavan 2014 option | `BallBallModel::Mathavan2014` (reserved) | 3 |
| 3.1-3.2 polynomials, quartic, degenerate degrees | `Detect.h::BallBallGapPolynomial`, `PredictBallBall` | 5 |
| 3.3-3.4 solver, scaling, tolerances | `Math/Polynomial.h::SolveInInterval`; `NumericsConfig` | 5/0 |
| 3.5 broad phase | `Detect.h::SweptBounds` | 5 |
| 3.6 touching, pressing, overlap | `ContactFlags`, `PredictBallBall`; simulator island routing | 5/6a |
| 3.7 simultaneity, determinism | `EventQueue.h::EventPrecedes`; section 8.3 exact-time rule; section 15 row 19 | 6a |
| 3.9.1-3.9.4 CLI | `Compliant.h::CompliantIsland`, `HertzContactTime`, `TsujiDamping`, `TsujiAlphaForRestitution`, `IslandJoinDistance` | 3 |
| 3.9.2 joining / exit / safety | `StartIsland`, `AdvanceIsland` (section 8.8) + `CompliantIsland::CanExit`, `SustainedContact`, `GapToIsland`, `QueryTableFeatures` | 6b/3/5 |
| 3.9.5 rack realism | `Geometry/RackLayout.h::RackGapParams`, `ApplyRackGaps`; `rules::GenerateRack` | 2/9 |
| 3.9.6 island records | `IslandContactRecord`, `ShotEventFlags::FromIsland` | 3/6b |
| 4.1 cushion geometry | `TableGeometry.h::ComputeCushionContact`; `Cushion.h::MakeCushionFrame` | 2/4 |
| 4.2-4.3 contact velocities | `ResolveMathavan`, `ResolveHan` | 4 |
| 4.4 Han 2005 | `ResolveHan`, `CushionModel::Han2005` | 4 |
| 4.5 Mathavan 2010 | `ResolveMathavan`, `MathavanSettings` (split, N gate) | 4 |
| 4.6 GRI | `ResolveGri` | 4 |
| 4.7 model selection | `ResolveFixedContact`, `CushionParams::OnClothModel` | 4 |
| 4.8 e_c law | `CushionRestitutionLaw`, `CushionRestitution` | 4 |
| 4.9 oversized cue balls | per-ball `R` in `ComputeCushionContact` | 2 |
| 4.10 cushion detection | `PredictNoseOnCloth/Airborne`, `PredictJawArcOnCloth/Airborne` | 5 |
| 5.1-5.3 pocket geometry | `TableGeometry.h::PocketGeometry` (+ `WallTopZ`), `JawArc`, `Facing`, `FacingContactOffset` | 2 |
| 5.3 element models (table) | `Detect.h::PredictFacing*`, `PredictDropEdge`, `PredictLinerWall`, `PredictRimTorus`, `PredictCaptureDepth`; `Cushion.h::ResolveFixedContact` | 5/4 |
| 5.4 pocket state machine | `ProcessPocketEvent` (section 8.9) | 6b |
| 5.4 pivot macro-step | `PocketDrop.h::ComputePivot(k)`, `MakePivotPath`, `EvaluatePivot`, `PivotLeaveState`, `PivotDetectionProxy` | 4 |
| 5.5 emergent behaviours, facing params | `TableSpec::FacingRestitutionScale` → `CushionParams`; tests POCK | 2/4/10 |
| 5.6 Level B | `CliMode::Rigid` (reserved use) | 3 (later) |
| 6.1 landing routing | `RouteLanding`; `IsOverPocketOpening` | 6b/2 |
| 6.2 rail top | `RailTopPolygon`, `PredictRailTop`, `PredictRailTopEdge`, `PredictSupportExit`, `ResolveGri`, `CliMode::Rigid`, `IslandFeatureKind::Plane`, `ProcessRailTopEvent`, `MotionSegment::SupportZ` | 2/5/4/3/6b/1 |
| 6.3 off table | `PredictOuterBoundary`, `PredictLampApex`, `EnvironmentSpec`, `OffTableReason` | 5/2 |
| 7.1-7.3 persistent contacts, Zeno | islands with rigid sustained mode, `NumericsConfig` (section 9) | 6b/3 |
| 7.4 airborne ball-ball | `PredictBallBall` (3D), `ResolveBallBall` | 5/3 |
| 7.5 pocketed-ball contacts | `RecordEventType::BallTouchesPocketedBall` (Level B only) | 8 |
| 8 pitfalls 1-19 | coding rules (section 2) and the respective functions | all |

### 16.3 equipment (EQP)

| Section | Header :: function / type | WP |
|---|---|---|
| 0.1-0.3 frame, tags, naming | `Core/Ids.h` | 0 |
| 1 presets | `TableSpec.h` presets, `GetTableSpec` | 2 |
| 2 table body | `TableSpec` fields (`BedHeight`, `RailWidthTotal`, `RailTopZ`, `SlateThickness`) | 2 |
| 3.1 strings, spots | `TableLandmarks`, `IsAboveHeadString` | 2 |
| 3.2 diamonds | `Sight`, `TableGeometry::Sights` | 2 |
| 4.1 nose height, contact geometry | `ComputeCushionContact`, `kCushionContact*` | 2 |
| 4.2 profile | `CushionProfile`, `RailTopPolygon` | 2 |
| 4.3 facings | `Facing` | 2 |
| 5.1-5.3 pockets | `PocketSpec`, `PocketGeometry`, `JawArc`, `CornerThroat`, `SideThroat` | 2 |
| 6 balls | `BallSets.h` (+ Blackball), `EquipmentConstants.h` | 2 |
| 7 cloth | `Core/Ids.h::ClothPreset`, `Motion.h::kCloth*` | 0/1 |
| 8 cues | `Equipment/Cue.h` | 2 |
| 9 racks | `Geometry/RackLayout.h` | 2 |
| 10 lighting | `EquipmentConstants.h` (light), `EnvironmentSpec::LampUndersideZ` | 2 |
| 11 constants, code sketch | `EquipmentConstants.h`, `TableSpec.h`, `ValidateWpa`, `BuildTableGeometry` | 2 |
| 12 pitfalls | coding rules; `CushionParams::PooltoolCompat` | 2/4 |

### 16.4 rules (RUL)

| Section | Header :: function / type | WP |
|---|---|---|
| 1 conventions | `Shot/ShotRecord.h`, `Rules/RulesTypes.h` | 8 |
| 2.1-2.2 landmarks, predicates | `RulesTable` (per-ball radii, pocket openings), `MakeRulesTable`, `BuildRulesTable`, `AboveHeadString` … | 8/7 |
| 2.3 pockets, cushions, rails | `Core/Ids.h`, rail features | 0 |
| 3.1 ShotStart | `ShotStartSnapshot` (+ radii), `BuildShotStartSnapshot` | 8/7 |
| 3.2 StrokeRecord | `StrokeRecord` (`Strokes`, per-ball `TipContacts`), `FinishShotRecord`, follow-through (section 8.6) | 8/7/1/6a |
| 3.3 event log | `RecordEvent`, `ToRecordEvent`, `AppendRecordEvent` | 8/7 |
| 3.4 ShotEnd, settle window | `ShotEndSnapshot`, `RulesTolerances::SettleWindow` | 8 |
| 3.5 facts F1-F13 | `ShotFacts.h::DeriveShotFacts`, `ResolveFirstContact`, `RailContactEntry` | 8 |
| 3.6 tolerances | `Core/Tolerances.h::RulesTolerances` | 0 |
| 4.1 lag | `Lag.h`; two `StrikeRequest`s | 9/6a |
| 4.2 break order | `RulesConfig::Breaks`, `Match.h` | 8/9 |
| 4.3 spotting | `TableRules.h::SpotBall`, `SpotBalls` (per-ball radii) | 9 |
| 4.4 ball in hand, spot request | `SpotRequestCandidate`, `RequestSpot`, `GetShotConstraints`, `CueBallPlacementLegal` | 9 |
| 4.5 calls | `Evaluate.h::ResolveCall` (ordered rail contacts), `RulesConfig::Calls` | 8 |
| 4.6-4.7 pocketed/hanging, off table | `ShotFacts` F4/F5 | 8 |
| 4.8 stalemate, concession | `Match.h::DeclareStalemate`, `Concede` | 9 |
| 4.9 foul catalogue, severity | `Foul`, `FoulSet`, `EvaluateShot` | 8 |
| 4.10 shot clock | `Match.h::ShotClock*` | 9 |
| 4.11 frozen calls | `BuildShotStartSnapshot` (auto-declare) | 7 |
| 5 racks, 14.1 outline | `TableRules.h::GenerateRack`, `StraightPoolRackOutline`, `InterferesWithRack` | 9 |
| 6 8-ball | `EvaluateShot` (8-ball path) | 8 |
| 7 9-ball | `EvaluateShot` (9-ball path) | 8 |
| 8 10-ball | `EvaluateShot` (10-ball path) | 8 |
| 9.1-9.4 14.1 | `EvaluateShot` (14.1 path, calls `PlanRerack*` with `RulesTable`) | 8/9 |
| 9.5 14.1 re-racks | `PlanRerack14`, `PlanRerack15AfterFifteenthPocketed` | 9 |
| 10 evaluateShot | `Evaluate.h` | 8 |
| 11 match state machine | `Match.h` (incl. Doubles `ActiveMember`) | 9 |
| 12 variants | `RulesConfig.h` presets, `EvaluateShot` (Blackball path: `NextFreeShot`, visits), `BallSetPreset::Blackball` | 8/2 |
| 16 pitfalls | respective functions; start-state evaluation is a signature property (`GameState` at shot start) | 8/9 |

### 16.5 prior-art-and-validation (VAL)

| Section | Implementation | WP |
|---|---|---|
| 2.8 take / change | lazy-deletion heap (`EventQueue.h`), version stamps, strategy enums, no `make_kiss`; pooltool models `Mirror`, `StrongeCompliant`, `PocketModel::CaptureCircle` for XREF | 6a/4/5 |
| 5.1 failure modes R1-R13 | section 9 guards; `Detect.h` windows; snaps; parameter validation | 5/6a/1 |
| 5.3 ordering | `EventPrecedes` (tier order per COL, section 15 #5) | 6a |
| 5.4 tolerances | `NumericsConfig` | 0 |
| 5.5 root finding | `Polynomial.h`, `PredictBallBall` | 5 |
| 5.6 cluster solver | `Compliant.h` (COL CLI instead of PGS; section 15 #6) | 3 |
| 5.7 cushion topology | `TableGeometry` (shared endpoints), `SegmentParamSlack` | 2/5 |
| 5.8 snapping | `SegmentEndState`, `ClassifyState` | 1 |
| 5.9 time representation, determinism | per-ball `T0`; section 11 (FP pinned three times) | 6a/0 |
| 5.10 caching, culling | slots + versions; `SweptBounds` | 6a/5 |
| 5.11 parameter validation | `ValidatePhysicsParams`, `ValidateCueStrike` | 6a/1 |
| 7 performance targets | section 12; `Tests/Core/Benchmarks` | 10 |
| 8 implementation notes | sections 2, 8, 11, 12 | all |

### 16.6 ue5-realism-plan (UE, boundary parts)

| Section | Implementation | Owner |
|---|---|---|
| 1.5 / 6.7 geometry single source | `TableGeometry` (incl. rail-top polygons), `BuildNoseOutline`, `rbsim --geometry` | WP-2/WP-7 |
| 5.4 stroke input | UE module → `rb::human::IntendedStroke` → `ExecuteStroke` → `CueStrikeInput` (`AimToContactOffset` inside) | UE / WP-11 / WP-1 |
| 5.5 cue geometry, min elevation | UE module using `TableGeometry` | UE |
| 5.6 coordinate conversion | UE module adapter (never in the core) | UE |
| 5.7 playback, orientation | `Playback.h` (orientation law, cursor cache), `TrajectorySegment::Orientation0`, `Quat.h`, `CueTipAt` | WP-7 |
| 8.1-8.3 audio events | `ShotEvent` (`NormalSpeed`, impulses, types) | WP-6a |

### 16.7 human-factors (HF)

| Section | Header :: function / type | WP |
|---|---|---|
| 1 principles (visible cause, input first, seeded noise, same model for the AI, player's time, skill never removes error, errors carry information, fairness caps, options) | `rb::human` as a whole: pure `ExecuteStroke` / `SampleHand`, `NoiseHash.h` (streak guard), `SyntheticHand`, `Chores.h`, `SkillScale`, `StrokeBreakdown` + diagnosis, `ProductConfig`, `AssistSettings` | 11 |
| 2.1 stroke and body HF-01..HF-20 | `HumanParams`, `StrokeSituation`, `ComputeSituationFactors`, `ExecuteStroke`, `SampleHand`; HF-18 vision centre: UE camera calibration, `CharacterAimBias` for the AI; HF-20 `ProductConfig::Alcohol` (Q2: cosmetic in V1), hook `StrokeSituation::Intoxication` / `StrokeIntoxication`, camera effects in UE | 11 / UE |
| 2.2 tip, chalk, cue HF-21..HF-34 | `TipState.h`, `ChalkCube`, `CueState.h`, `Venue.h::SeedHouseCue`; HF-32 `StrokeSituation::ShortCue`; HF-34 `CueSpec` presets | 11 / 2 |
| 2.3 balls, table, rack HF-40..HF-55 | HF-40 `BallMarks.h` + `BallBall.h::ContactClingFactor` + 8.11; HF-41 `TableCondition::BallCling`; HF-42 / HF-43 `BallSets.h`, `VenueBallSetSeed`; HF-50 / HF-51 `TiltParams`, 8.11; HF-52..HF-54 not in v1.2 (need spatial parameters, V2 / Later); HF-55 `RackGapsForQuality` -> `rules::GenerateRack` | 11 / 3 / 2 / 1 / 6a |
| 2.4 fouls and chores HF-60..HF-79 | HF-60 `ExecutedStroke::ShaftContactCandidates` -> `ShotContext::NonTipContacts` (UE mesh collision authoritative in Sim mode); HF-61 follow-through (8.6); HF-62 `TipTouchesCloth` -> `StrokeInfo::TipClothContact`, `RulesConfig::Scoop`; HF-63 `ExternalObject::Chalk`; HF-70..HF-76 `Chores.h` + UE; HF-77..HF-79 UE | 11 / 1 / 6a / 8 / UE |
| 3.1 interface | `HumanModel.h` (`IntendedStroke`, `ExecutedStroke`, `ExecuteStroke`, `SampleHand`), `Skill.h` (`ShooterAttributes`, `StrokeSituation`, `HumanParams`), `NoiseHash.h::NoiseKey` | 11 |
| 3.2 noise primitives, channels, rollout keys, streak guard (Q1), watchable processes | `NoiseHash.h` (`DrawPerShot`, `HistoryMatchesKey`, `ProcessShotKey`) | 11 |
| 3.3 parameters and skill scaling | `HumanParams`, `SkillScale`, `BridgeSpecFor` | 11 |
| 3.4 situation multipliers, pressure, intoxication hook | `ComputeSituationFactors` (+ `P_x`, `m_alc`), `SettleInEnvelope`, `SettleFactor`, `ComputePressure`, `MoneyGameStakes`, `FatigueFromNight`, `StrokeIntoxication` | 11 |
| 3.5 executed stroke, natural pivot | `ExecuteStroke`, `StrokeBreakdown` (the squirt that cancels drift yaw is the core's, MOT B.7) | 11 / 1 |
| 3.6 tip contact, friction, flags | `LookupTipContact`, `ExecuteStroke` (with `AimToContactOffset`, `MiscueLimit`, `SeparationMargin` of `CueStrike.h`; `HumanParams::Rules`) | 11 / 1 |
| 3.7 visibility, rendering, aborted strokes | `SampleHand`, `HandPose`, `AdvanceNoiseHistory`; pixel capture in UE Automation | 11 / UE |
| 3.8 AI synthetic hand | `AiProfiles.h::SyntheticHand`, `CharacterAimBias`, `RolloutKey` | 11 |
| 3.9 counterfactual diagnosis | `MissCause`, `DiagnosisStep`, `DiagnosisStepAt`, `ApplyDiagnosisStep`, `ComputeStrokeShares` (the game orchestrates the re-runs) | 11 |
| 3.10 routine-shot budget | HF-S04..S06 (budget model); HF-B02 (full core) | 11 / 10 |
| 3.11 core contract changes | `CueStrikeInput` unchanged (tip fields filled per stroke); package layout (7.5, section 15 row 34) | 11 / 0 |
| 4.1 chalk map | `TipState.h` (`LookupTipContact`, `HitSeverity`, `ApplyTipWear`, `ApplyChalkTwist`, `AutoChalkTwists`, `ChalkGradeSpecFor`) | 11 |
| 4.2 tip condition | `TipState.h` (`TipState`, `TipEdgeLimit`, `EffectiveTipRestitution`, `ScuffTip`, `ShapeTip`, `TrimTip`, `Retip`) | 11 |
| 4.3 chalk marks, per-contact cling | `BallMarks.h` (deposit, fade, wipe; WP-11); `ChalkMark`, `ChalkMarkWeight`, `ContactClingFactor`, `BallBallParams::ChalkClingFactor` (WP-3); `SimBall::ChalkMarks`, `PhysicsParams::ChalkCling`, the loop (8.11, WP-6a) | 11 / 3 / 6a |
| 4.4 house cues and warp | `CueState.h::ComputeWarp`, `NoticeableBow`, `AutoRollTestNotices`; `Venue.h::SeedHouseCue` | 11 |
| 4.5.1 tilt model | `TiltParams`, `InPlaneGravity`, `MakeSegment(…, Tilt)` | 1 |
| 4.5.2 exact solution | `PursuitState`, `PursuitStopTime`, `EvaluatePursuit`; `Scalar.h::Expm1` | 1 / 0 |
| 4.5.3 chains, refresh events, events inside pieces | `TiltChain`, `TiltPieceDuration`, `SegmentEndState`, `EvaluateSegmentForEvent` (WP-1); `QueuedEventKind::TiltRefresh`, `sim::BallStateForEvent`, 8.11 (WP-6a); detection unchanged (WP-5, A-DET-6); playback of curving pieces (WP-7, A-PLAY-4) | 1 / 6a / 5 / 7 |
| 4.5.4 magnitudes | tests HF-T15..T17 | 1 |
| 4.5.5 persistence | `Venue.h::SeedTableSlope`, `MakeVenueTableCondition` -> `TableCondition` -> `MakePhysicsParams(Spec, Condition)` | 11 / 6a |
| 4.5.6 nap | `TiltParams::NapPseudoSlope`, `NapResistance` (defaults 0, section 15 row 30) | 1 |
| 4.5.7 contract additions | `PhysicsParams::Tilt`, `ValidatePhysicsParams`, `ParamTable` keys `tilt.*` (WP-6a); `MotionSegment::Tilt` (WP-1); `TiltRefresh` (WP-6a); `Expm1` (WP-0) | 6a / 1 / 0 |
| 4.6 racks | `Chores.h::RackGapsForQuality` -> `rules::GenerateRack(…, Gaps, …)` | 11 / 9 |
| 4.7 where state lives | 7.5 (data flow), section 13 item 11 | 11 / UE |
| 5.1 attributes, habits | `ShooterAttributes`, `ShooterHabits`, `GrowHabit`; number visibility `ProductConfig::Attributes` (Q3: hidden) | 11 |
| 5.2 XP | `XpSource`, `XpAward`, `AttributePointCost`, `XpToRaise`, `RepeatFactor` | 11 |
| 5.3 unlocks and equipment | `CueSpec` presets (WP-2), `ChalkGrade`, `TipHardness`, `LowDeflectionShaftUnlocked` (Q7) | 11 / 2 |
| 5.4 difficulty presets, assists | `DifficultyPreset`, `AssistSettings`, `GetAssistSettings`, `ImperfectionSetting`, `NoiseScaleFor`, `PressureMode` | 11 |
| 5.5 AI profiles | `AiProfileId`, `AiProfile`, `GetAiProfile`, `AiKnowledge`, `AiCharacter` (the planner is outside the core) | 11 / AI |
| 6 tests | section 17.12 | all |
| 7 product-owner decisions (all answered) | `ProductConfig` (Q2 cosmetic + hook, Q3 hidden, Q4, Q5, Q6 money games in, Q7), `HumanParams::StreakGuard` (Q1) | 11 |
| 9 verification log | oracles `Tools/reference/human-factors/*.py` | 0 |

---

## 17. Work packages

Twelve packages (WP-0 … WP-11) for parallel implementation in separate git worktrees (WP-10 starts after integration). **Every package compiles against the complete public headers from day one**; stubs of other packages return neutral values, so tests of a package that need another package's *implementation* fail until that package is merged (listed as "integration dependencies"; such tests carry the name prefix `Integ_`, section 18). Owned file sets are disjoint. `WP-0` (architect) owns the foundation, build files, the harness and this document.

Spec test IDs are assigned exactly once (section 17.12). Sub-IDs: COL `D-12` is split into `D-12a` (detection flags, WP-5) and `D-12b` (island routing, no interpenetration, CPU < 1 ms, WP-6b); COL `BB-3b` and `D-5b` are separate spec IDs; COL `CL-5` includes both its 100 µm and 10 µm cases. Architecture tests `A-*` (section 18) are listed per package.

### WP-0 — Architecture & foundation (architect)

* **Owned files**: `Public/rb/Config.h`, `Version.h`, `Core/*.h`, `Math/Vec2.h`, `Vec3.h`, `Aabb.h`, `Scalar.h`; `Private/rb/Core/FpGuard.h`, `Private/rb/Version.cpp`, `Private/BilliardsCoreModule.cpp`; `BilliardsCore.Build.cs`; root `CMakeLists.txt`; `Tests/Core/rbtest.h`, `TestMain.cpp`, `CMakeLists.txt`, `Tests/Core/Architecture/**`; `Tools/reference/**` (incl. `human-factors/`); `Docs/architecture.md`; `THIRD_PARTY_NOTICES.md`.
* **Tests**: architecture smoke tests (pinned defaults, macro traps incl. every `rb/Human` header, parameter table, single parameter source; v1.2: `Arch_TiltAndClingDefaultsAreNeutral`, `Arch_ScalarFunnelExpm1Log1pCbrt`, `Arch_EventQueueTiltRefreshAfterContacts`, `Arch_HumanFactorsDefaultsFromSpec`, `Arch_HumanStubsLinkAndStayNeutral`) and the configure-time guards of the root `CMakeLists.txt` (FpGuard first include; no `rb/Human` include from `rb/Physics`, `rb/Shot`, `rb/Rules`); no spec IDs.

### WP-1 — Motion, slate & cue strike

* **Owned files**: `Public/rb/Physics/BallState.h`, `Motion.h`, `Slate.h`, `CueStrike.h`; `Private/rb/Physics/Motion.cpp`, `Slate.cpp`, `CueStrike.cpp`; `Tests/Core/Motion/**`, `Tests/Core/CueStrike/**`.
* **Header dependencies**: foundation; `Equipment/Cue.h` (WP-2, data only).
* **Integration dependencies**: none (fully standalone).
* **Spec sections**: MOT 0, A.1-A.9, B.1-B.9, C.1-C.5, implementation notes 1-10, 12, 15; COL 2.4 step 6 and 2.5 (`ApplyTableReaction`); architecture 7.2 (inertia factor), 8.6 (follow-through: `MakeCueTipPath`, `CueTipAsSegment`, `ResolveTipRecontact` on any ball), surface segments with `SupportZ`; HF 4.5.1-4.5.4, 4.5.6, 4.5.7 and architecture 8.11 (tilted table: `TiltParams`, `TiltChain`, pursuit solution, refresh rule, `MakeSegment(…, Tilt)`, `SegmentEndState` of chain pieces, `EvaluateSegmentForEvent`, nap).
* **Tests**: MOT T-A1…T-A12, T-B1…T-B20, T-C1…T-C8; VAL KIN-01…KIN-08, CLOTH-01…CLOTH-03, SQ-01…SQ-04, CUE-01…CUE-04, AIR-01…AIR-03 (`g = 9.81`, AIR-02 with `h_min = 0.5 mm`); UE T24; HF T15, T16, T17 (tilt oracle, secant chain, sliding and collinear). Architecture: A-MOT-1 (non-solid `k`: slide duration and Coriolis invariant against the general formulas), A-MOT-2 (segment on the flat cap with `SupportZ = RailTopZ`), A-MOT-3 (a level `TiltParams` gives the level segment bitwise for every state), A-MOT-4 (tilted Airborne: in-plane `g_t / 2`, the Coriolis invariant drifts by `g_t / (1 + k)`; tilted Stationary / Spinning unchanged), A-MOT-5 (inside a chain piece `EvaluateSegmentForEvent` equals `EvaluatePursuit` to 1e-12; `SegmentEndState` of a refresh piece is the exact node, of the last piece the HF 4.5.3 snaps; general `k`).

### WP-2 — Equipment & table geometry

* **Owned files**: `Public/rb/Equipment/*.h`, `Public/rb/Geometry/*.h`; `Private/rb/Equipment/*.cpp`, `Private/rb/Geometry/*.cpp`; `Tests/Core/Equipment/**`, `Tests/Core/Geometry/**`.
* **Header dependencies**: foundation; `Physics/BallState.h` (WP-1, `BallSpec`).
* **Integration dependencies**: none.
* **Spec sections**: EQP 0-13; COL 4.1 and 4.9 (contact geometry), 5.1-5.3 (pocket construction: jaw arcs, facings with draft, shelf, capture circle, drop edge `a_d`, liner, `WallTopZ`), 6.2 (rail-top polygons incl. pocket surrounds, edge kinds, cut discs), 3.9.5 (gap presets, gap algorithm with fixed anchor); RUL 12.4 (Blackball ball set); UE 6.7 (renderer outputs).
* **Tests**: EQP T-UNIT-1…3, T-CUSH-1…5, T-GEOM-1…6, T-POCKET-1…9, T-RACK-1…7, T-WPA-1…3; COL C-G1, P-1. Architecture: A-GEO-1 (rail-top polygons cover every rail-top point between the nose lines and the outer boundary outside the pocket cuts exactly once, incl. pocket surrounds; T-GEOM-6 mirror symmetry of the polygons), A-GEO-2 (pocketless table: C1/C4 absent, C0/C3 full rails), A-RACK-1 (`ApplyRackGaps`: anchor ball unchanged, no pair closer than D, realised gaps within 20 % of the targets on average, deterministic per seed).
* **Notes**: verify the `TABLE_7FT_78` pocket values (incl. the capture circles used by `PocketModel::CaptureCircle`) against pooltool before XREF use.

### WP-3 — Ball-ball & compliant islands

* **Owned files**: `Public/rb/Physics/BallBall.h`, `Compliant.h`; `Private/rb/Physics/BallBall.cpp`, `Compliant*.cpp`; `Tests/Core/BallBall/**`, `Tests/Core/Compliant/**`.
* **Header dependencies**: foundation; `Motion.h`, `CueStrike.h` (WP-1); `Cushion.h` (WP-4, `CushionRestitutionLaw`); `Math/Quat.h` (WP-7, chalk-mark weight).
* **Integration dependencies**: COL:BB-2's table step needs WP-1 (`ApplyTableReaction`); island cushion damping needs WP-4 (`CushionRestitution`).
* **Spec sections**: COL 2.1-2.8, 3.6 (pressing resolution in the CLI), 3.9.1-3.9.4, 3.9.6, 5.6/6.2 (rigid mode, plane features), 7.3 (sustained contacts), pitfalls 2-4, 10-12, 16-17; architecture 8.8 (geometric contact order, sustained switch, tips, bounded features, exit hysteresis); HF 4.3 (per-contact cling: `ChalkMark`, `ChalkMarkWeight`, `ContactClingFactor`, `ChalkClingFactor`); architecture 8.11 (in-plane gravity inside islands: `SetInPlaneGravity`).
* **Tests**: COL BB-1…BB-10 incl. BB-3b, CL-1…CL-8 (CL-5 both gap cases); VAL BB-01…BB-08, THR-01…THR-10; HF S07 (cling weight of one mark). Architecture: A-CLI-1 (pressing pair with slip acceleration switches to Rigid and runs to separation/rest, step count bounded), A-CLI-2 (`TsujiAlphaForRestitution` vs bisection with the integrator at e = 0.93, 0.95, 0.98, 5e-5), A-CLI-3 (tip participant: contact duration ≈ `ContactTime` for a free ball, impulse ≈ `StrikeCueBall`), A-CLI-4 (ball on a sloped `Plane` rolls down and leaves over an `EdgeLine`), A-CLING-1 (`ContactClingFactor`: no marks -> `k_venue` exactly; `k_venue > k_chalk` -> `k_venue`; saturation at `chi_1 + chi_2 >= 1`; a mark follows the ball's orientation), A-CLI-5 (tilted island: `g_t` on every body; a body at rest on the cloth stays at rest within the validity rule; a ball pushed against an `EdgeLine` by the slope comes to rest there and the island exits; `SetInPlaneGravity` not called or `(0, 0)` -> bitwise the level island, incl. signed zeros).

### WP-4 — Cushion, facing & pocket-edge resolution

* **Owned files**: `Public/rb/Physics/Cushion.h`, `PocketDrop.h`; `Private/rb/Physics/Cushion*.cpp`, `PocketDrop.cpp`; `Tests/Core/Cushion/**`, `Tests/Core/Pocket/**`.
* **Header dependencies**: foundation; WP-1 (`BallState.h`, `Motion.h`); WP-2 (`TableGeometry.h::PocketGeometry`).
* **Integration dependencies**: the 1D rail-speed harness of COL:CAL-1 / EQP:T-CAL-1 / VAL:CUSH-05 needs WP-1 (motion segments).
* **Spec sections**: COL 4.1-4.9, 5.3 (contact models of the elements), 5.4 (pivot, general `k`), 5.5 (facing parameters), 6.2 (GRI on rail tops and edges), 7.1/7.3 (resting rule in the dispatcher), pitfalls 1, 7, 8, 19; VAL 2.8 (Mirror and Stronge ports, Apache-2.0 notice via WP-0).
* **Tests**: COL C-H1…C-H4, M-1…M-7, G-1…G-3, CAL-1, P-2; EQP T-CAL-1; VAL CUSH-01…CUSH-07. Architecture: A-CUSH-1 (Mathavan accuracy gate: default N with splitting keeps M-2…M-4 within 2e-4 of N = 20 000; sets the default N), A-CUSH-2 (`_Slow_` Release micro-benchmark ≤ 2 µs per hit), A-CUSH-3 (Mirror model), A-CUSH-4 (`ComputePivot` with k = 0.4 equals P-2; general-k energy balance).

### WP-5 — Event detection

* **Owned files**: `Public/rb/Physics/Detect.h`, `Public/rb/Math/Polynomial.h`; `Private/rb/Physics/Detect*.cpp`, `Private/rb/Math/Polynomial.cpp`; `Tests/Core/Detect/**`, `Tests/Core/Math/**`.
* **Header dependencies**: foundation; WP-1 (`MotionSegment`); WP-2 (geometry types); WP-4 (`FixedContact`, `PocketModel`).
* **Integration dependencies**: none for the numbered tests (segments and features are built by hand).
* **Spec sections**: COL 3.1-3.6, 4.10, 5.3 (detection column incl. the degree-8 rim torus, liner/back-wall height rules), 6.1-6.3 (landing, rail-top polygons and edges, support exit, boundary, lamp apex), 7.4; MOT C.2; VAL 5.5, 5.7, 5.10; rules observers (RUL 2.2 line crossings, F9 plan-distance crossings); architecture 8.2 (state- and position-dependent applicability), 8.8 (`QueryTableFeatures`), 8.11 (tilt chain pieces are general quadratics: no predictor or swept bound may assume `Accel2` parallel to `Vel0` for Sliding / Rolling segments).
* **Tests**: COL D-1…D-11 incl. D-5b, D-12a; VAL ROOT-01, ROOT-02, ROB-07, ROB-08, ROB-13. Architecture: A-DET-1 (airborne ball over a corner pocket: rim torus and back wall predicted; back wall ignored above `WallTopZ`), A-DET-2 (rail-top polygon contact incl. a pocket surround, cut disc excluded), A-DET-3 (`PredictSupportExit` across a seam, the ridge and the cut), A-DET-4 (`PredictPlanDistanceCrossing` for a jump over a ball, D-5b geometry), A-DET-5 (`PredictLineCrossings` with `IncludeFrom`), A-DET-6 (a curving Rolling tilt piece against a ball, a nose line and a drop edge: event times equal brute-force sampling of the quadratic; `SweptBounds` contain the piece).

### WP-6a — Simulator core loop

* **Owned files**: `Public/rb/Physics/Simulator.h`, `ShotResult.h`, `EventQueue.h`, `ParamTable.h`; `Private/rb/Physics/SimInternal.h`, `Simulator.cpp`, `SimLoop*.cpp`, `ShotResult.cpp`, `ParamTable.cpp`; `Tests/Core/Simulator/**`.
* **Header dependencies**: all physics headers, WP-7 (`Playback.h::SegmentOrientationAt`, `ShotRecordBuilder.h`), WP-8 (`ShotRecord.h`).
* **Integration dependencies**: WP-1, WP-2, WP-4, WP-5 (all shots), WP-7 (orientation law, record building), WP-6b (islands, pockets, landings, rail top). Start immediately with validation, strikes, queue, slots, versions, time bases, observers, tip slots, recording and guards; tests go green as the models land.
* **Spec sections**: architecture 8.1-8.7, 8.10, 8.11, 9, 11; HF 4.3 (loop side: marks, orientation of marked balls, per-contact `k_cling`), 4.5.3 (`TiltRefresh`, invalidation, tie-break, exact re-anchor), 4.5.7 (`PhysicsParams::Tilt`, `ChalkCling`, validation, `ParamTable` keys), `TableCondition` and `MakePhysicsParams(Spec, Condition)`; COL 1, 3.6-3.7, 7.1-7.4 (dispatch side); MOT impl. notes 1, 2, 6, 8, 10, 12; VAL 2.8, 5.1-5.4, 5.8-5.10; RUL 3.3 (emission: continuesInitialFreeze, observers, tip-contact events), 4.1 (two strikes).
* **Tests**: COL Z-1, Z-2, Z-4, O-1, O-2; VAL ROB-04, ROB-05, ROB-06, ROB-10 (standalone half first), ROB-11, ROB-12, ROB-14, ROB-15; HF B14 (`Integ_`: a slow roll kissing a ball at 2 mm/m gives the RK4 object-ball direction within 0.1 deg). Architecture: A-SIM-1 (`Run` rejects `ParamsOrigin::Unset`, bad inertia factors, strikes on moving balls), A-SIM-2 (lag: two strikes, per-ball tip contacts, per-ball facts), A-SIM-3 (observer at exactly an event time: emitted after it, recomputed for a replaced segment), A-SIM-4 (follow-through tip touches a ball other than the struck one → `NonTipContact{CueTip}`), A-SIM-5 (the record is identical for all `RecordOptions` logging switches, incl. `BallJumpedOver` with trajectories off), A-SIM-6 (exactly simultaneous shared-ball events are islanded; permuted ids give the same final states), A-SIM-7 (`TiltRefresh`: exact node state, version bump, all slots re-predicted, observers with `IncludeFrom`, pops after a contact at the same time and never islands, skipped when stale, logged only with `LogTransitions`, `TiltRefreshes` counted), A-SIM-8 (`ChalkCling`: per-contact `k_cling` from marks with `Trajectories = false`; off -> `k_venue` for every contact), A-SIM-9 (exact re-anchor: a contact approaching by the piece but not by the exact velocity goes to an island, never dropped; on a level table `BallStateForEvent` equals `BallStateAt` bitwise).

### WP-6b — Islands, pockets & rail-top routing

* **Owned files**: `Private/rb/Physics/SimIsland*`, `SimPocket*`, `SimRailTop*` (`.cpp` and private helper headers); `Tests/Core/Islands/**`, `Tests/Core/PocketFlow/**`.
* **Header dependencies**: `SimInternal.h` (WP-6a), WP-2, WP-3, WP-4, WP-5 headers.
* **Integration dependencies**: WP-6a loop; WP-3 solver; WP-4 pivot and contact models; WP-5 predictors and feature queries; WP-1 segments.
* **Spec sections**: architecture 8.8, 8.9, 8.11 (pocket, landing and rail-top resolvers and island members take `BallStateForEvent`; islands use `k_venue` and get `SetInPlaneGravity(InPlaneGravity(Params.Tilt, g))` after `Reset`); COL 3.6 (pressing islands), 3.9.2 (joining incl. features, member exits, exit), 5.4 (pocket state machine, pivot truncation), 6.1 (landing routing), 6.2 (rail top: rigid slope, flat-cap segments, exits), 7.3 (sustained contacts, budget).
* **Tests**: COL D-12b, Z-3, P-3…P-6; VAL ROB-01, ROB-02, ROB-03, ROB-09, BRK-01. Architecture: A-ISL-1 (CB at 5 m/s into an OB frozen to the rail 5 cm from the corner jaw: no tunnelling through the jaw/facing), A-ISL-2 (D-12b and Z-3 CPU < 1 ms, `_Slow_`, Release), A-ISL-3 (frozen CB struck toward the OB: tip intervals from the island, G17-like envelope data), A-ISL-4 (50 ms rigid island stays within `MaxSegmentsPerBall`), A-POCK-1 (break hop landing in the drop-edge annulus: torus event, no missed event), A-POCK-2 (jumped ball flying across a pocket hits the back wall), A-RAIL-1 (ball landing on the flat cap comes to rest → `OffTable(RestsOnRailOrFrame)`, no island steps while rolling on the cap), A-RAIL-2 (ball bouncing on the sloped cushion top rolls back over the nose onto the cloth).

### WP-7 — Output, playback & tools

* **Owned files**: `Public/rb/Physics/Playback.h`, `Public/rb/Math/Quat.h`, `Public/rb/Shot/ShotRecordBuilder.h`; `Private/rb/Physics/Playback.cpp`, `Private/rb/Shot/ShotRecordBuilder.cpp`; `Tools/rbsim/**`; `Tests/Core/Playback/**`, `Tests/Core/ShotRecord/**`.
* **Header dependencies**: WP-6a (`ShotResult.h`, `Simulator.h`, `ParamTable.h`), WP-8 (`ShotRecord.h`, `RulesTypes.h`), WP-1, WP-2.
* **Integration dependencies**: playback tests use hand-built `ShotResult`s but need WP-1 `EvaluateSegment`; frozen sets in `BuildShotStartSnapshot` use WP-2 `ComputeCushionContact`; `BuildRulesTable` uses WP-2 geometry; `rbsim --rack` needs WP-9 (`GenerateRack`) + WP-2, `--facts` needs WP-8; meaningful `rbsim` output needs everything.
* **Spec sections**: UE 5.7 (playback, the orientation law, cursor cache), 6.7 (geometry export incl. rail-top polygons), 8.1-8.3 (event data for audio); RUL 3.1-3.4 and 4.11 (record building, frozen auto-declaration, tip contacts from record events, `f` envelope data, CueTip non-tip contacts); VAL 7.4 P6; MOT impl. note 14 (rbsim `--param`, `--dump-input`, `--in`); HF 4.5.3 (playback of curving tilt pieces: the orientation law's grid path), HF 4.3 (`--dump-input` / `--in` carry `SimBall::ChalkMarks`), `ShotEventType::TiltRefresh` and `tiltRefreshes` in the JSON output.
* **Tests**: UE T17, T18. Architecture: A-PLAY-1 (orientation at `T1` of every segment equals the next `Orientation0` bitwise), A-PLAY-2 (cursor ≡ random access bitwise; `_Slow_` P6 timing), A-PLAY-3 (`CueTipAt` along analytic and sampled pieces), A-REC-1 (per-strike tip intervals merged from record events; tip contact on a non-struck ball → `NonTipContact{CueTip}`), A-REC-2 (`BuildRulesTable`: landmarks, pocket openings, per-ball radii), A-TOOL-1 (`--dump-input` → `--in` round trip reproduces the event log bitwise, incl. tilt keys and chalk marks), A-PLAY-4 (a curving Rolling tilt piece is not constant-axis; orientation at the refresh boundary bitwise continuous).

### WP-8 — Rules: facts & evaluation

* **Owned files**: `Public/rb/Shot/ShotRecord.h`, `Public/rb/Rules/RulesTypes.h`, `RulesConfig.h`, `ShotFacts.h`, `Evaluate.h`; `Private/rb/Rules/RulesConfig.cpp`, `ShotFacts.cpp`, `Evaluate*.cpp`; `Tests/Core/Rules/Facts/**`, `Tests/Core/Rules/Eval/**`.
* **Header dependencies**: foundation; WP-9 (`TableRules.h::PlanRerack14/15`).
* **Integration dependencies**: the 14.1 evaluator calls WP-9's `PlanRerack14` / `PlanRerack15AfterFifteenthPocketed` for every re-rack (S12…S19 paths; S19 is WP-8's test); everything else runs on hand-built `ShotRecord`s / `ShotFacts`.
* **Spec sections**: RUL 0-3 (record and facts incl. F13 ordered lists and per-ball radii), 4.5-4.7, 4.9, 6, 7, 8, 9.1-9.4, 10, 12 (variant evaluation incl. Blackball free shot and visits), 16.
* **Tests**: RUL G01…G21, G27…G31, E01…E34, N01…N18, N21, N23, T01…T10, T12, T13, S01…S11, S19, S20, S22, S24, S25, V01…V09. Architecture: A-CALL-1 (ObviousAssist: an OB that touches a jaw of another pocket before dropping is not obvious; one that touches only the jaws of its pocket is), A-FACT-1 (overflow flags of the ordered contact lists).

### WP-9 — Rules: table procedures & match

* **Owned files**: `Public/rb/Rules/TableRules.h`, `Lag.h`, `Match.h`; `Private/rb/Rules/TableRules*.cpp`, `Lag.cpp`, `Match*.cpp`; `Tests/Core/Rules/Table/**`, `Tests/Core/Rules/Match/**`.
* **Header dependencies**: WP-8 headers; WP-2 (`RackLayout.h`).
* **Integration dependencies**: RUL:K01-K06 need WP-2 (`RackApexX`, `RackAnchorSiteIndex`, `BuildRackLattice`, `ApplyRackGaps`); RUL:N22 end-to-end needs WP-8 (`EvaluateShot`) — match tests otherwise feed hand-built `ShotOutcome`s.
* **Spec sections**: RUL 4.1-4.4, 4.8, 4.10, 5, 9.5, 11 (incl. Doubles, Reg 27), 12.1 (match-level switches), 12.4 (Blackball free-shot placement choices, visits), 16 (pitfalls 9, 12, 13-16, 23-26).
* **Tests**: RUL G22…G26, G32, K01…K06, L01…L07, S12…S18, S21, S23, M01…M06, C01…C03, N19, N20, N22, T11. Architecture: A-SPOT-1 (spot the 9 next to an oversized 60.325 mm CB: no overlap, gap `δ_cbGap`), A-PLACE-1 (`ValidateDeclaration` rejects a placement over a pocket opening / overlapping a ball with per-ball radii), A-DBL-1 (Doubles: pass-back after a push-out goes to the partner), A-VIS-1 (Blackball free shot: `PlacementChoices` = in position | in hand in baulk).

### WP-10 — Validation & benchmarks (after integration of WP-1…WP-9 and WP-11)

* **Owned files**: `Tests/Core/Validation/**`, `Tests/Core/Benchmarks/**`, `Tools/xref/**` (pooltool harness, Python; installing pooltool needs VAL OQ-2 approval).
* **Header dependencies**: all public headers.
* **Integration dependencies**: everything (WP-1…WP-9 merged; WP-11 for HF-B02, HF-B12; HF-B09 also needs an AI planner, O-16).
* **Spec sections**: VAL 6 (validation data), 7 (performance targets and benchmark protocol B1/B2/B3), 9.3/9.7-9.9/9.12-9.13; COL 5.5 (pocket behaviours); HF 3.10 (full-core budget, per cue class), 5.5 (rating fit), 4.5.3 (tilt cost).
* **Tests**: VAL BB-09, BRK-02…BRK-05, SYS-01…SYS-06, POCK-01…POCK-04, PERF-01…PERF-05, XREF-01, XREF-02; HF B02 (release blocker: 3.10 budgets with the full core, 10^5 shots per profile and cue class), B09 (rating round-robins), B10 (every MOT/COL/VAL/RUL test passes with `Slope = 0`, `ChalkCling` off: the whole suite on default parameters), B12 (human-layer share of misses, simulated proxy of the playtest KPI). Architecture: A-PERF-1 (typical shot <= 100 µs on a 2 mm/m table at `Tolerance` 5e-5; AI rollout at 5e-4). All `_Slow_` (nightly, Release) except HF-B10, which is the default test run itself.

### WP-11 — Player model (`rb::human`, human-factors)

* **Owned files**: `Public/rb/Human/*.h`; `Private/rb/Human/*.cpp`; `Tests/Core/Human/**`.
* **Header dependencies**: foundation; WP-1 (`BallState.h`, `Motion.h`, `CueStrike.h`); WP-2 (`Cue.h`, `BallSets.h`, `RackLayout.h`); WP-3 (`BallBall.h`: `ChalkMark`); WP-6a (`ShotResult.h`, `Simulator.h`: `PhysicsParams`, `SimBall`, `TableCondition`); WP-7 (`Quat.h`); WP-8 (`ShotRecord.h`: `NonTipContact` data only). Never `rb/Rules/*`; never included by `rb/Physics`, `rb/Shot`, `rb/Rules` (root CMake guard).
* **Integration dependencies**: HF-T05…T07, T09, T11 and the budget model of HF-S04…S06 use WP-1 (`AimToContactOffset`, `MiscueLimit`, `SeparationMargin`, `SquirtAngle`) and carry the `Integ_` prefix until WP-1 lands; HF-B03 needs WP-1 `StrikeCueBall`; HF-B06 needs the simulator (WP-1…WP-7) and the rules (WP-8) to judge make or miss; the fading in `ApplyShotToEquipment` needs recorded tracks (WP-6a, WP-7). Noise, chalk, warp, progression, chores and venues are standalone.
* **Spec sections**: HF 1, 2 (the HM items of the catalogue), 3.1-3.11 (incl. the intoxication hook and money-game stakes of 3.4), 4.1-4.4, 4.5.5, 4.6, 4.7, 5.1-5.5, 6, 7 (product-owner decisions), 9.4, 9.5; architecture 7.5, 13 item 11.
* **Tests**: HF T01…T14, S01…S06, B01 (core half: bitwise identical `ExecutedStroke` and shot hash for the same input log, keys and state across Debug / Release; the UE half with ROB-10's), B03, B04, B05, B06 (`Integ_`), B07 (core half: `SyntheticHand` returns an `IntendedStroke`; the static check over the AI sources runs where the AI lives, O-16), B08 (core half: at attribute 25 every channel at 1 sigma moves the `SampleHand` pose at the tip by at least 0.30 mm, 1 px at 1440p in the Eyes preset (HF 3.7), except tremor and lateral tip placement, whose tells are audio / replay; the capture half is UE Automation), B11, B13. Architecture: A-HUM-1 (streak guard: incremental `NoiseHistory` equals `RebuildNoiseHistory` over 10^4 draws of every guarded channel; a history that does not match the key gives the same draws as the matching one; rollout keys ignore the history; `StreakGuard = false` gives candidate `Sub` 0; the exact-integer fallback lands in an allowed eighth, incl. the HF 3.2 counter-example; `AddressIndex` 0 gives the v1.2 process key, `AddressIndex` 1 a different process), A-HUM-2 (purity: repeated `ExecuteStroke` / `SampleHand` calls bitwise equal; `SampleHand` at `t_c` with the full ramp equals `ExecuteStroke`), A-HUM-3 (`ProductConfig`: every decision Q2-Q7 is the default; `StrokeIntoxication` is 0 under `CosmeticOnly`, and `Intoxication` 0 leaves `ExecuteStroke` / `SampleHand` bitwise unchanged; under `Mechanic` one drink lowers the pressure gain and more raise drift and tremor; `MoneyGameStakes` from 0.6 to 1), A-HUM-4 (diagnosis: the 3.9 order and overrides; `LevelCleanTable` gives a level, clean `PhysicsParams` and empty mark lists).
* **Notes**: oracles `Tools/reference/human-factors/noise.py` (hash, InvNorm, processes), `streak.py` (Q1, exact-integer fallback), `stroke.py` + `recompute_v12.py` (stroke and budgets with the v1.2 draws; valid for v1.3, whose hook is exactly neutral at `I` = 0), `chalk.py`, `equil.py`, `tilt.py`, `chain*.py`; port values, do not re-derive them. The tuning values (HF 3.3, 5.2, 5.5) are data in `HumanParams`, `XpAward` and `GetAiProfile`.

### 17.12 Test assignment check (every spec test ID exactly once)

| Spec | IDs | Package |
|---|---|---|
| MOT | T-A1…12, T-B1…20, T-C1…8 (40) | WP-1 |
| COL | BB-1…10 + BB-3b, CL-1…8 (19) | WP-3 |
| COL | D-1…11 + D-5b, D-12a (13) | WP-5 |
| COL | D-12b, Z-3, P-3…6 (6) | WP-6b |
| COL | C-G1, P-1 (2) | WP-2 |
| COL | C-H1…4, M-1…7, G-1…3, CAL-1, P-2 (16) | WP-4 |
| COL | Z-1, Z-2, Z-4, O-1, O-2 (5) | WP-6a |
| EQP | T-UNIT-1…3, T-CUSH-1…5, T-GEOM-1…6, T-POCKET-1…9, T-RACK-1…7, T-WPA-1…3 (33) | WP-2 |
| EQP | T-CAL-1 (1) | WP-4 |
| RUL | G01…21, G27…31, E01…34, N01…18, N21, N23, T01…10, T12, T13, S01…11, S19, S20, S22, S24, S25, V01…09 (117) | WP-8 |
| RUL | G22…26, G32, K01…06, L01…07, S12…18, S21, S23, M01…06, C01…03, N19, N20, N22, T11 (41) | WP-9 |
| VAL | KIN-01…08, CLOTH-01…03, SQ-01…04, CUE-01…04, AIR-01…03 (22) | WP-1 |
| VAL | BB-01…08, THR-01…10 (18) | WP-3 |
| VAL | CUSH-01…07 (7) | WP-4 |
| VAL | ROOT-01…02, ROB-07, ROB-08, ROB-13 (5) | WP-5 |
| VAL | ROB-04, ROB-05, ROB-06, ROB-10, ROB-11, ROB-12, ROB-14, ROB-15 (8) | WP-6a |
| VAL | ROB-01, ROB-02, ROB-03, ROB-09, BRK-01 (5) | WP-6b |
| VAL | BB-09, BRK-02…05, SYS-01…06, POCK-01…04, PERF-01…05, XREF-01…02 (22) | WP-10 |
| UE | T24 (1) | WP-1 |
| UE | T17, T18 (2) | WP-7 |
| UE | T1…T16, T19…T23, E1…E6 | **not BilliardsCore**: render math (`RenderMath` target: T1-T13, T19-T23), UE coordinate adapter (T14-T16), UE Automation (E1-E6) |
| HF | T15…T17 (3) | WP-1 |
| HF | S07 (1) | WP-3 |
| HF | B14 (1) | WP-6a |
| HF | B02, B09, B10, B12 (4) | WP-10 |
| HF | T01…T14, S01…S06, B01, B03…B08, B11, B13 (29) | WP-11 |

Totals: MOT 40, COL 61 (60 spec IDs, D-12 counted as D-12a + D-12b), EQP 34, RUL 158, VAL 87, HF 38 (T01…T17, S01…S07, B01…B14), UE 3 core-owned = 421 assignments. Per package: WP-1 66, WP-2 35, WP-3 38, WP-4 24, WP-5 18, WP-6a 14, WP-6b 11, WP-7 2, WP-8 117, WP-9 41, WP-10 26, WP-11 29. HF-B01, B07 and B08 have a UE half (ROB-10 style, AI sources, UE Automation capture) that the owning package tracks; HF-B09 waits for an AI planner (O-16).

### 17.13 Suggested merge order

All packages except WP-10 start at once. Merge as soon as green: WP-2 → WP-1 → WP-5 → WP-4 → WP-3 → WP-7 → WP-6a → WP-6b; WP-8 and WP-9 are merged (2026-09-26, reviewed; rules are independent of physics); WP-11 at any time after WP-1 (noise, chalk, warp, progression, chores and venues are standalone; its stroke tests need WP-1, HF-B06 the full core); WP-10 after WP-6b and WP-11. The first end-to-end milestone is `rbsim --speed 2 --ball 1:0.5,0` producing a correct stun shot (WP-1/2/3/5/6a/7); ROB-10's standalone half runs from then on.

---

## 18. Test conventions

* **Location**: `Tests/Core/<Area>/Test<Topic>.cpp` in the package's own directories (section 3); the test CMake globs every `.cpp` under `Tests/Core`.
* **Names**: `RB_TEST(<SPEC>_<ID>_<Description>)` with the ID's punctuation removed, e.g. `MOT_TA1_RollingStop`, `COL_D12a_PressingFlags`, `EQP_TPOCKET1_JawPoints`, `VAL_KIN05_CoriolisInvariant`, `HF_T03_StreakGuard`, `UE_T17_OrientationIntegration`; RUL IDs use `Rules_` as merged by WP-8 / WP-9 (`Rules_G01_TieWithinWindowFavoursLegalBall`). Architecture tests: `ARCH_<AREA><n>_<Description>` for the `A-<AREA>-<n>` IDs of section 17 (e.g. `ARCH_CALL1_ObviousAssistOnlyJawsOfOwnPocket`), and `Arch_…` for WP-0's own tests. **Integration tests** carry the prefix `Integ_` in front of the whole name (the convention of the merged WP-8 / WP-9, adopted for every package; no `_Integration` suffix): `Integ_COL_D12b_PressingIsland`, `Integ_HF_B14_ExactVelocityInsideTiltPiece`, `Integ_ARCH_ISL1_FrozenRailNoTunnel`, `Integ_Rules_S12_Rerack14ExecutedByApplyShot`.
* **Filters** (`TestMain.cpp`): every argument is a substring; a leading `-` excludes. A test runs when its name contains every include and no exclude: `BilliardsCoreTests MOT_`, `BilliardsCoreTests Integ_` (only integration tests), `BilliardsCoreTests -Integ_ -_Slow_` (a package's standalone default run).
* **Parameters**: every test pins its parameters explicitly (never rely on defaults; VAL 8.9): `PhysicsParams.Origin = Explicit`; MOT tests use `m = 0.170`, `g = 9.80665`; VAL tests `g = 9.81`, `m = 0.17009713875`; CL tests `TsujiAlpha = 0.03689`; M-6 pins N = 50/200/1000 without splitting; snooker sets where stated.
* **Tolerances**: exactly as defined by each spec (MOT "exact" = ±1 unit in the last printed digit, closed-form identities 1e-9 relative; "pub" = ±1 unit of the published digit; COL/EQP/RUL per row; VAL per tier).
* **Property tests** (random 1e4-1e6 cases: MOT:T-A7, T-A12, T-C6, T-C7; COL:BB-9, D-10; VAL:BB-08, CUSH-04, ROOT-01) use `rb::Rng` with fixed seeds; heavy ones may run a reduced count in Debug and the full count in Release.
* **Opt-in tests** (PERF, XREF, statistical BRK/SYS/POCK, CPU-time gates, HF-B02 / B09 / B12, A-PERF-1): name them `<SPEC>_<ID>_Slow_…` and exclude them from the default run with the `-_Slow_` filter; CI runs them nightly in Release.
* **Integration tests** that need another package's implementation carry the `Integ_` prefix, so a package runs only its standalone tests (`-Integ_`) before the dependency is merged; until then a failing `Integ_` test is expected (e.g. the WP-8 / WP-9 rack tests wait for WP-2's lattice).

---

## 19. Open issues

| # | Issue | Owner |
|---|---|---|
| O-1 | Verify `ModuleRules.FPSemantics = FPSemanticsMode.Precise`, `bUseRTTI`, `bEnableExceptions`, `bAddDefaultIncludePaths` names in the UE 5.8 toolchain; then run ROB-10's UE half. | WP-0 / UE |
| O-2 | Cross-platform bitwise determinism requires portable implementations of `Sin/Cos/Exp/Log/Atan2/Asinh` behind `rb/Math/Scalar.h` (only needed for mixed-platform online play). | WP-0 |
| O-3 | Follow-through model (section 8.6) is an architecture decision: `FollowThroughDistance`, the merge gap and the tip stiffness derivation need play-testing against Dr. Dave's double-hit/push examples (RUL G14-G18). | WP-1 / WP-3 / WP-6a |
| O-4 | Mathavan default N: WP-4 sets it by the accuracy gate (A-CUSH-1) and confirms the ≤ 2 µs per hit gate (A-CUSH-2); escalate if the gate fails. | WP-4 |
| O-5 | `P5` memory target (64 KB per in-flight simulation): estimate ≈ 60-65 KB with the sparse island contact list; confirm with PERF, shrink the Zeno table if needed. | WP-6a / WP-3 |
| O-6 | `TABLE_7FT_78` pocket values (incl. pooltool capture circles) and `TABLE_8FT_HOME` pockets are placeholders (VERIFY). | WP-2 |
| O-7 | `rbsim --in` reader (the schema and `--dump-input` exist). | WP-7 |
| O-8 | Level B pocket interior and pocket fill (`BallTouchesPocketedBall`, `SupportedOverPocket`) are reserved in the contract but not produced by Level A physics. | later |
| O-9 | ESTIMATE values introduced by this revision: rail-top rolling resistance and spin deceleration, per-preset `k_f` and liner values, Blackball ball masses (same density), `SustainedSteps`, `MaxIslandSteps`, `SampleTolerance`. Tune with play-tests / measurements. | WP-4 / WP-2 / WP-3 / WP-6b |
| O-10 | XREF-01/02 need VAL OQ-2 approval (installing Python + pooltool) before WP-10 can run them. | user / WP-10 |
| O-11 | Unchanged spec open questions: cushion calibration (COL OQ 2, 10), rack gap distribution (COL OQ 7), `lambda(theta)` and cloth presets (MOT OQ 1-3), rules interpretations (RUL 14). Data/tuning changes; they do not affect the architecture. | spec owners |
| O-12 | **Closed (2026-09-26).** The product owner answered the last HF questions: Q2 alcohol cosmetic for V1 with an intoxication hook for a later mechanic, Q3 attribute numbers hidden, Q6 money games in (HF v1.3 section 7; 7.5). Specifying the alcohol mechanic later (effect sizes, the game's intoxication level) is a tuning task of WP-11 / UE, not an architecture change. | product owner |
| O-13 | Tilt performance against the 100 µs typical-shot budget is unmeasured (A-PERF-1); nap (HF-51) needs calibration before its per-cloth defaults leave 0 (section 15 row 30, HF 9.3). | WP-10 / WP-1 |
| O-14 | **Rules-side follow-ups requested by human-factors** (not done by the architecture: WP-8 / WP-9 were implemented in parallel, are now merged, and their files are edited only by their owners): (a) pure queries for the pressure scalar, so that the game and the AI compute `P` from the same rules state (HF 3.4): `gameBall` = "this shot can win the rack" from `GameState` + `RulesConfig` (WP-8, e.g. next to `LegalFirstContactMask` in `Evaluate.h`), `hill` and the current run length from `MatchState` (WP-9, `Match.h`); (b) rules.md F10 / `ShotContext::NonTipContacts` documentation: the game collects body contacts from "down on the shot" on (HF-60), in Sim mode for both players incl. the AI avatar, ghosted for both in Assisted / Relaxed (HF 5.4) (spec text only); (c) confirm `ScoopPolicy::WpaMiscue` as the default with `TipTouchesCloth` now set from `ExecuteStroke`'s executed pose (HF-62; no code change expected); (d) none needed for racks: `GenerateRack(…, Gaps, …)` already takes the `RackGapParams` from `RackGapsForQuality` (HF 4.6). | WP-8 / WP-9 (rules owners) |
| O-15 | Chalk-mark cling inside CLI islands (e.g. a cue ball frozen to an object ball with a mark at the contact) is not modelled; islands use `k_venue` (section 15 row 36). | WP-3 / WP-6b (V2) |
| O-16 | HF-B09 needs an AI planner (outside the core) driving rbsim round-robins; the static check of HF-B07 needs the AI source location; the capture half of HF-B08 is UE Automation. | AI / UE / WP-10 |
| O-17 | Human-layer, XP and profile values are TUNING (HF 3.3, 5.2, 5.5); HF-B02 with the full core may force a refit (B3 with the standard cue 9.00 % against the 10 % budget, HF 3.10). | WP-11 / WP-10 |

---

## 20. Review resolution

The adversarial review of v1.0 (2026-09-25, 1 blocker, 15 major, 19 minor findings) is resolved as follows. (v1.2, the human-factors integration, is not a review resolution: its decisions are section 15 rows 28-40 and the changelog in the header; its own review is 20.1.) "Accepted" means implemented as proposed; deviations from the proposal are stated with the reason.

| # | Sev. | Finding | Resolution |
|---|---|---|---|
| 1 | blocker | Long-lasting pressing contacts have no workable end state | **Accepted.** Sustained-contact mode: compliant → Rigid switch (`CliParams::SustainedSpeed/SustainedSteps`, `SustainedContact()`), `CompliantMaxDuration` caps only the compliant phase, rigid islands run until contacts open or bodies rest, **no impulse fallback**; Zeno histories cleared at island start/exit plus an exit hysteresis against pressing re-entry; per-shot `MaxIslandSteps` budget with `IslandBudgetExceeded`; CPU gates A-ISL-2 (D-12b, Z-3 < 1 ms). Sections 8.8, 9, 15 row 20. The analytic "coupled pair" alternative was not chosen: it does not cover rail-pressed balls, clusters or the rail top. |
| 2 | major | The lag cannot be simulated | **Accepted.** `SimInput::Strikes` (`FixedVector<StrikeRequest, kMaxStrikes = 2>`), `ShotResult::Strikes`, `StrokeRecord::Strokes`, `TipContact::{Ball, Strike}`, tip record events carry the ball; rbsim `--strike-ball`, `--strike`; `Lag.h` documents per-ball facts. |
| 3 | major | Airborne balls miss pocket geometry | **Accepted.** Position-dependent applicability in `PredictTableEvent`: rim torus and liner/back wall for every airborne ball whose swept bounds reach a pocket's `a_d` cylinder, wall height rules (`PocketGeometry::WallTopZ`); `PocketPivot` predicts facings and jaw arcs on `PivotDetectionProxy`; tests A-DET-1, A-POCK-1, A-POCK-2. Section 15 row 27. |
| 4 | major | Motion on the rail top cannot be represented | **Accepted with a modification.** `IslandFeatureKind::Plane` (convex polygon + cut, restitution, friction, rolling resistance) and `EdgeLine`; rail top = `RailTopPolygon` list covering the pocket surrounds with edge kinds (`kMaxRailTopPolygons = 48`), exported to the mesh generator; the sloped cushion top is a rigid island. Modification: the **flat cap** is an analytic surface segment (`MotionSegment::SupportZ = RailTopZ`, `SupportExit` events) instead of a rigid island — exact, and a ball rolling to rest on the cap costs no island steps (section 15 row 25). |
| 5 | major | Cue-tip re-contacts are missed inside islands; follow-through checked only against the struck ball | **Accepted.** `IslandTip` kinematic participant (compliant tip, stiffness from `ContactTime`), tip intervals from positive tip force; tip slots predict against every ball; hits on other balls → `NonTipContact{Source = CueTip}`; tip paths stored in `ShotResult::CueTips`, `CueTipAt` for Unreal. |
| 6 | major | Cushions join a cluster only when it starts | **Accepted.** Feature joining during stepping via `QueryTableFeatures` with a conservative reach bound, `CompliantIsland::RemoveBody` for members reaching a drop edge or leaving the cloth / rail-top region; test A-ISL-1. |
| 7 | major | CL-8 fails by design | **Accepted.** Id-independent geometric contact key, Jacobi accumulation (compliant) and key-ordered sweeps (rigid), plus the exact-simultaneity rule of 8.3; no tolerance relaxation of CL-8 / BRK-04 needed. Section 15 row 21. |
| 8 | major | Orientation playback too slow; boundary jumps | **Accepted.** One orientation law (`SegmentOrientationAt`: closed forms for constant-axis segments, 1 ms grid otherwise) used by simulator and playback; `PlaybackCursor` caches the grid orientation per ball; tests A-PLAY-1/2. |
| 9 | major | The rules use one ball radius | **Accepted.** `RulesTable::BallRadius[16]` + `NominalBallRadius`, `ShotStartSnapshot::Radius[]`, `BallShotSummary::Radius`; `SpotBall` with `D_j = R_b + R_j (+ δ_cbGap)`, `BlocksSpot`/`InterferesWithRack`/F11/lag metric per ball; test A-SPOT-1. |
| 10 | major | `EvaluateShot` gets no table landmarks | **Accepted for `EvaluateShot`** (`const RulesTable&` added). **Not for `ResolveCall`**: with item 11 the obvious-shot inference reads pocket ids from the ordered jaw contacts and needs no landmarks. |
| 11 | major | Shot facts hold only rail-contact counts | **Accepted.** `RailContactEntry` ordered list (32) per ball, `BallContacts` raised to 32, overflow flags; test A-CALL-1. |
| 12 | major | Cloth has two sources of truth | **Accepted.** `MakePhysicsParams(TableSpec)` used by rbsim/Unreal/AI; `k_f` and liner values moved into `TableSpec`; `ParamsOrigin` makes `Run` reject unset parameters; test `Arch_MakePhysicsParamsIsTheSingleTableSource`. |
| 13 | major | Unreal module does not pin precise FP | **Accepted.** `FPSemantics = FPSemanticsMode.Precise` (+ no RTTI / exceptions, explicit include paths) in `BilliardsCore.Build.cs`; `Private/rb/Core/FpGuard.h` first in every core `.cpp`, enforced by the root CMake; ROB-10 scheduled (section 11). Name verification in UE 5.8 is O-1. |
| 14 | major | Mathavan eats the 100 µs budget | **Accepted.** Splitting on by default, N by accuracy gate (placeholder 32; A-CUSH-1), ≤ 2 µs/hit gate (A-CUSH-2), N = 200 only in M-6; the AI uses the referee's model (old O-4 mitigation removed). Section 15 row 22. |
| 15 | major | WP-6 too large | **Accepted with a modification.** WP-6a (core loop), WP-6b (islands, pockets, rail top), WP-10 (validation and benchmarks) with the private interface `SimInternal.h`. ROB-07, ROB-08 and ROB-13 stay in WP-5 (they test predictors, not the loop). |
| 16 | major | XREF cannot be configured | **Accepted.** `CushionModel::Mirror`, `CushionModel::StrongeCompliant` (Apache-2.0 port, notice via WP-0), `PocketModel::CaptureCircle`, `PredictCaptureCircle`; XREF still needs OQ-2 approval (O-10). |
| 17 | minor | Default inertia literal rounded | **Accepted.** `BallSpec{}` = `MakeBallSpec` bitwise, `kBallInertia` exact expression, and models generalised with `k = I/(m R²)` (section 7.2) so per-ball inertia is honoured; `Run` validates `k`. |
| 18 | minor | Missing `RB_API` on `Polynomial` members | **Accepted.** |
| 19 | minor | No macro-collision safety net | **Accepted, except the X11 traps.** `TestMacroTraps.cpp` (Unreal + Windows macros), `_USE_MATH_DEFINES` removed, `rb/Core/Assert.h` (`RB_ASSERT`). The X11 macros `None` and `Status` are not trapped: Unreal itself uses `None`/`Status` as identifiers everywhere, so no Unreal translation unit can have them defined, and renaming the core's `PocketId::None`, `ShotResult::Status`, … would protect against nothing. |
| 20 | minor | Observer ordering at equal times | **Accepted** (section 8.7, `PredictLineCrossings(IncludeFrom)`). |
| 21 | minor | `OverRail` flag never updated | **Accepted.** Flag removed; applicability from each feature's geometric validity region. |
| 22 | minor | Slate landing scheduled twice | **Accepted.** End slot is the single owner. |
| 23 | minor | No crossings inside islands | **Accepted.** Island members' line crossings, freeze-leave and jump-over are evaluated every step. |
| 24 | minor | Recording options can corrupt the rules record | **Accepted.** Observers and tip events always produced for the record; `LineCrossings` replaced by `LogObservers` (physics log only); `BallJumpedOver` is a simulator observer; `TipContacts` built from record events. |
| 25 | minor | Frozen-ball / tip data against the nearest ball | **Accepted.** Tip events carry `B = f` (largest positive `n_hat · d`), the gap at begin and end, and `OtherContactBefore`. Section 15 row 26. |
| 26 | minor | Unbounded island features | **Accepted.** Segment endpoints/length, jaw angle range, facing extent, plane polygon. |
| 27 | minor | Tsuji alpha not tied to e_b | **Accepted with a modification.** `TsujiAlphaForRestitution` (tabulated, checked by bisection) and `TsujiAlpha < 0 = derived` resolved at `Run` start — not inside `ValidatePhysicsParams`, which must not mutate its input. |
| 28 | minor | No cue-ball placement in `ValidateDeclaration` | **Accepted.** `const Vec2* PlacedCueBall`; pocket openings in `RulesTable` (filled by `BuildRulesTable`); `OverPocketOpening`, `CueBallPlacementLegal` with per-ball radii. |
| 29 | minor | Variant cases not expressible | **Accepted.** `ShotOutcome::NextFreeShot/NextVisits`, `GameState::VisitsRemaining`, `ShotConstraints::PlacementChoices/FreeShot/VisitsRemaining/Member`, `MatchState::ActiveMember/TeamBreaker`, `BallSetPreset::Blackball` + constants. |
| 30 | minor | Pocketless tables vs cushion-id indexing | **Accepted.** Pocketless mapping (C0/C2/C3/C5, C1/C4 absent) and `NoseSegment::Present`. |
| 31 | minor | Unlisted sub-IDs and dependencies | **Accepted.** BB-3b, D-5b, CL-5 (both cases), D-12a/b listed; dependencies WP-7→WP-1, rbsim→WP-8/WP-9, WP-8→WP-9 (all 14.1 re-racks) added. |
| 32 | minor | Rack-gap algorithm under-specified | **Accepted.** Anchor site fixed (`RackAnchorSiteIndex`), row expansion + relaxation + projection algorithm in `RackLayout.h`, mixture preset, decision in section 15 row 23. |
| 33 | minor | rbsim cannot override parameters or replay inputs | **Accepted.** `ParamTable.h` (84 reflected keys), `--param`, `--list-params`, `--strike-ball`, `--strike`, `--dump-input` (rbsimInput schema) implemented; `--in` defined and assigned to WP-7 (O-7). |
| 34 | minor | Missing conflict-table rows | **Accepted.** Section 15 rows 19-27 (incl. the 1e-12 s window, the 50 ms cap, `kClusterTol`, `h_min` reasons). |
| 35 | minor | Segment buffer overflow on long islands | **Accepted.** Adaptive Sampled recording (`SampleTolerance` 10 µm, `SampleMaxInterval` 10 ms, samples at contact changes); capacity unchanged; test A-ISL-4. |

### 20.1 HF integration review (v1.2, 2026-09-26)

Adversarial review of the human-factors integration against `human-factors.md`, `physics-motion-and-cue.md` and this document, after the product owner answered the last open HF questions and WP-8 / WP-9 were merged. The oracles were re-run (`recompute_v12.py` draws / stroke / budget); Debug `/W4 /WX` builds clean, and the only failing tests are the 12 `Integ_Rules_*` rack tests that wait for WP-2's lattice (the same 12 fail at HEAD). No file under `Rules/`, `Shot/ShotRecord.h` or `Tests/Core/Rules/` was touched.

| # | Sev. | Finding | Resolution |
|---|---|---|---|
| 1 | major | The product owner answered Q2, Q3 and Q6 after the integration; the code and docs still had them open, and `ProductConfig::Money` defaulted to `LeaguePrizeOnly`. | Defaults and comments updated (`Progression.h`: Q2 `CosmeticOnly` for V1, Q3 `Hidden`, Q6 `SideBetsAndHustling`); HF v1.3 section 7 and log 9.5; 7.5; O-12 closed; `Arch_HumanFactorsDefaultsFromSpec` pins Q2-Q7. |
| 2 | major | No intoxication hook: the Q2 mechanic existed only as attribute modifiers (`EffectiveAttributes`, `AlcoholParams`), with no stroke-situation input that `ExecuteStroke` maps onto drift, tremor and nerve. | `StrokeSituation::Intoxication`, `StrokeIntoxication(ProductConfig, level)` (0 under `CosmeticOnly`), placeholder `HumanParams::Intoxication*`; HF 3.4-3.5 `P_x`, `m_alc`, `m_alc,t`, exactly 1 at `I` = 0 (V1 bitwise unchanged, no expected value moves); attribute modifiers removed; UE camera note (section 13 item 11, HF-20); A-HUM-3; section 15 row 37. |
| 3 | minor | Money games in (Q6), but the stakes term of the pressure scalar was a flat 0.6 whatever the bet. | `MoneyGameStakes(bet, cash)`: 0.6 up to 10 % of the cash, 1 at 50 % or all in (HF 3.4, TUNING, same for the AI); money matches count for Nerve XP (HF 5.2); hustler behaviour on by default (HF 5.5). |
| 4 | major | The streak-guard fallback `(A[j] + (m u - j)) / 8` rounds `m u` and can land in an excluded eighth (`N` = 7205759403792793, `A` = {0, 1, 3, 5, 7} gives eighth 6), breaking the Q1 guarantee. | Exact integer mapping (`P = m N`, `j = P >> 53`) in `NoiseHash.h`, HF 3.2 and `streak.py` (counter-example plus a 1e5-case self-check); probability 2.4e-14 per draw, so no expected value changes; section 15 row 38. |
| 5 | major | `NoiseHistory` was hidden state: a cache that does not match the key (diagnosis re-runs after `AdvanceNoiseHistory`, a replay without it, another shooter's history) would silently change the draws. | `HistoryMatchesKey`; `DrawPerShot`, `ExecuteStroke`, `SampleHand` and `SyntheticHand` rebuild a mismatching history (O(n), allocation-free), so the result is a pure function of the key; 7.5, HF 3.2, A-HUM-1. |
| 6 | minor | Drift and tremor were keyed by `ShotIndex` and time since getting down, so they repeated on every get-down of a shot: a dry run revealed `D(t)` for the real address. | `NoiseKey::AddressIndex`, `ProcessShotKey` (address count in the high word): the v1.2 key for the first get-down, so HF-T04 and HF-S03 are unchanged; HF 3.2; section 15 row 39. |
| 7 | major | CLI islands integrated level gravity, so the tilt vanished inside every island (a ball rolled against a cushion by the slope and held in a rigid sustained contact, clusters, pressing contacts routed there by the exact re-anchor). | `CompliantIsland::SetInPlaneGravity` (WP-3), set by `StartIsland` after `Reset` (WP-6b), applied only when non-zero so a level island stays bitwise identical incl. signed zeros; A-CLI-5; 8.8, 8.11, section 15 row 40. |
| 8 | minor | Tilt-chain termination (section 9 row 14) rested on "every piece has a positive length", which does not exclude Zeno behaviour. | Explicit lower bound `Delta_min` on every non-final piece and at most `T_stop / Delta_min + 1` pieces per phase (section 9 row 14). |
| 9 | minor | The synthetic hand set no `t_fwd`, so an AI's per-shot offsets would show for its whole time down (unlike the player's) and A-HUM-2's full-ramp precondition failed for the AI. | HF 3.8 `t_fwd = t_c - max(0.1 s, 2 L_stroke / V_p)` (`AiProfiles.h`). |
| 10 | minor | Diagnosis step 1 "fresh chalk" was defined as "the cube's cap", which is undefined for `TipState` and hides the bar cube's 0.7 cap, itself equipment. | `c_z` = 1 (`mu_fresh`); tip shape, glaze and overhang unchanged (`HumanModel.h`). |
| 11 | minor | A house cue's `e_tip` and dome radius lived in both `CueSpec` and `TipState`. | `TipState` is authoritative (it feeds the strike); `SeedHouseCue` sets both equal (`Venue.h`). |
| 12 | minor | The layering guard missed relative includes such as `"../Human/Skill.h"`. | Root `CMakeLists.txt` regex matches any path ending in `Human/`. |
| 13 | minor | Test names: the merged WP-8 / WP-9 use the prefix `Integ_` (and `Rules_` for RUL IDs), but the architecture prescribed an `_Integration` suffix; the harness could not exclude, so "exclude `_Slow_` by filter" was impossible. | Sections 17 and 18 aligned (HF-B14, WP-11 stroke tests, B06); `TestMain.cpp` accepts `-substring` excludes (`-Integ_ -_Slow_`). |
| 14 | minor | WP-8 / WP-9 merged, but the architecture still called them in progress and did not list their new API. | 7.4 (`SetupRackWith`, `CompleteDeclaration`, `GenerateStraightPoolRack`, `LastGroupBallPocket`, `InningHadProgress`, `ShotAfterBreak`), O-14, 17.13; the O-14 rules follow-ups stay with the rules owners. |
| 15 | - | Checked without change. | Every HF test ID assigned once (38; 421 in total; per-package sums match); owned file sets disjoint. `TiltRefresh`: tier `Transition`, pops after every contact and a transition of the same instant, version bump and full re-prediction on the ball's own time base, stale refreshes skipped lazily, never islanded, counted against `MaxEvents`. Exact re-anchor: piece position, exact velocity and spin, approach test on the piece velocity, disagreements routed to an island. Playback: structural constant-axis test, grid law for tilt pieces. Zero tilt bitwise: `IsLevel` gives the level `MakeSegment`, `EvaluateSegmentForEvent` equals `EvaluateSegment`, `MakePhysicsParams(Spec, {})` equals `MakePhysicsParams(Spec)` on all 93 keys. `ExecuteStroke` is pure and allocation-free, the shooter key is widened before the shift, rollouts are plain, history-free and use common random numbers. Defaults match HF 3.3-4.6; `RB_API` everywhere; the macro traps include every `rb/Human` header. The oracles reproduce HF-T03, T05-T08, S01, S02, S04-S06 and the 3.10 table to the printed digits. |
