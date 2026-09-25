# RAW BREAK — Prior Art, Numerical Robustness & Validation Spec

| | |
|---|---|
| Document | `Docs/specs/prior-art-and-validation.md` |
| Scope | Survey of existing pool-physics implementations and research, the numerical pitfalls of event-based simulation, a catalogue of real-world validation data, and performance targets for `BilliardsCore` |
| Status | v1.0 (2026-09-25) |
| Consumers | `BilliardsCore` physics and simulator authors, test authors, AI (shot search) authors |
| Related specs | `equipment.md` (geometry and constants), `physics-motion-and-cue.md` (ball motion and cue strike), `rules.md`, `ue5-realism-plan.md` |

---

## 0. Conventions, tags and validation tiers

### 0.1 Frame, units, symbols (project-wide)

- **Units:** SI throughout (m, s, kg, rad). Degrees appear only in human-readable tables.
- **Frame:** right-handed.
  - The origin is at the center of the table bed.
  - **+x** runs along the table length toward the foot (rack) end.
  - **+y** runs across the width. Looking from the head toward the foot, +y points left.
  - **+z** points up.
  - The cloth is z = 0, so a resting ball's center is at z = R.
  - Rails, pockets and diamonds use the names defined in `equipment.md` §0.3 and §3.2: `RAIL_HEAD` (x = −L/2), `RAIL_FOOT` (x = +L/2), `RAIL_LEFT` (y = +W/2) and `RAIL_RIGHT` (y = −W/2). The inset of the diamond (sight) line behind the cushion nose is s = 0.0936625 m.
- **Ball symbols:**

| Symbol | Meaning | Unit |
|---|---|---|
| R | ball radius (0.028575 m for pool) | m |
| m | ball mass (0.17009713875 kg, 6 oz) | kg |
| I = (2/5) m R² | moment of inertia | kg·m² |
| r, v, ω | position, linear velocity, angular velocity (world-frame vectors) | m, m/s, rad/s |
| g | gravitational acceleration, 9.81 | m/s² |
| mu_s | ball–cloth sliding friction coefficient | 1 |
| mu_r | ball–cloth rolling resistance (rolling deceleration / g) | 1 |
| alpha_sp | spin-down rate of ω_z on the cloth (see `physics-motion-and-cue.md` §A.2) | rad/s² |
| e_b, mu_b | ball–ball coefficient of restitution and friction | 1 |
| e_c, mu_c | ball–cushion coefficient of restitution and friction | 1 |
| e_t | ball–table (slate) coefficient of restitution | 1 |
| φ | cut angle: the angle between the cue ball's incoming velocity and the line of centers at impact | rad |

- **Contact-point slip velocity with the cloth:** `u = v + ω × (−R ẑ)`
  - Sign check: a ball rolling in +x has ω = (0, v_x/R, 0). Then ω × (−R ẑ) = −R(ω_y, −ω_x, 0) = (−v_x, 0, 0), so u = 0.
  - Equivalently, u = v − R(ω_y, −ω_x, 0). pooltool writes the same quantity as `v + R ẑ × ω` (identical, since ẑ × ω = −ω × ẑ).
  - Alciatore (TP A.4, Eq. 3) uses the same definition.

### 0.2 Tags

| Tag | Meaning |
|---|---|
| [KEY] | A cited source (the key resolves in §11) |
| **DERIVED** | Computed or derived by us. The derivation is shown or referenced. |
| **INTERPRETATION** | The source is ambiguous, and our reading is stated. |
| **ESTIMATE** | No authoritative number exists. The value is a reasoned default. |

### 0.3 Validation tiers (used by every test in §9)

| Tier | What it checks | Typical tolerance | Fails if |
|---|---|---|---|
| **A: exact invariant** | Closed-form consequences of our own equations (e.g., the 5/7 rolling speed, the 90° rule with e = 1, μ = 0) | relative 1e-9 (or absolute 1e-9 m, 1e-9 s, 1e-7 rad) | there is a bug in the integrator, event detection or resolution |
| **B: published model reproduction** | We reproduce a published model's output with the same inputs (e.g., Alciatore TP A.14 throw, TP A.31 squirt) | 0.01°–0.05° or 0.5 % | there is a transcription error or a sign or convention mismatch |
| **C: measured reality** | Agreement with lab measurements (high-speed video, tracking) | as stated per test, typically 5–15 % or 1–3° | our parameter set or model is unrealistic |
| **D: practice / plausibility** | Diamond systems, pocket behaviour, WPA table-speed test, break statistics | loose (±0.5 diamond, trend or sign) | the table "feels wrong" to a real player |

Tier A and B tests run in CI on every commit. Tier C and D run nightly and produce a report (plots plus pass/fail). Tier C/D tolerances are deliberately loose because real tables vary (cloth, humidity, cushion rubber, ball wear).

---

## 1. Executive summary (key decisions)

1. **Reference implementation: pooltool** (Apache-2.0, Python + Numba, event-based, actively developed; v0.6.0 released 2026-03-15, commits as recent as 2026-09-25) [PT-GH][PT-CL].
   - We adopt its architecture concepts: motion states, per-event-type detectors, a transition cache, a pair collision cache, and pluggable resolver strategies.
   - We also reuse its catalogue of models: Alciatore ball–ball friction, Han 2005, Mathavan 2010, Stronge compliant cushions, and TP A.30/A.31 for the cue strike.
   - We use pooltool as an **oracle**: identical initial conditions must give the same results within tolerance when models and parameters match (tests `XREF-*`).
2. **Licensing:**
   - pooltool is Apache-2.0. Porting ideas or code is allowed with attribution; we keep the license and copyright notice in any ported file.
   - **GPL projects** (FooBillard/FooBillard++, tailuge/billiards, Billiards) are read-only references. **No code copying.**
   - The pooltool quartic solver is a translation of ACM TOMS Algorithm 1010. The upstream repository (`cridemichel/quarticpp`) ships no license file, and ACM algorithm code has historically carried a non-commercial license (INTERPRETATION; see OQ-1). We therefore **write our own root finder** (§5.5).
3. **Root finding:** we do not need all four complex roots of a quartic. We need the **first entry root inside the validity window** `[t_now, min(next transition of either ball)]`.
   - We use a deterministic real-root isolation on that bounded interval: split at the critical points (roots of the cubic derivative), then run safeguarded Newton/bisection.
   - This removes the "is this complex root real?" heuristics (pooltool: ATOL 1e-9, RTOL 1e-3) and the spurious roots that come from extrapolating a polynomial past a transition.
4. **Determinism:** events are processed in a **strict total order**: (time, tier, stable pair key). Doubles are compared exactly. No epsilon grouping of times.
   - BilliardsCore must be compiled **without fast-math**. This must be verified inside the UE build (OQ-3).
   - Replays, online play and AI all rely on bitwise reproducibility.
5. **Zeno and touching balls:**
   - We handle them explicitly instead of hoping they are rare. Tools:
     - an approach-velocity gate
     - a resting-contact threshold for cushions
     - a minimum bounce height for airborne balls
     - a per-pair event-rate detector that switches frozen clusters (the rack, Newton's cradle, frozen combos) to a **simultaneous-impulse cluster solver**
     - a hard event cap
   - pooltool uses a 1 µm spacer, a phenomenological "momentum theft" (10 %) and exact-time priority tiers.
6. **Validation:** about 80 concrete tests in 12 groups (§9). The strongest real-world anchors are:
   - Mathavan et al. 2009/2014 high-speed tracking: 5 ball–ball shots with measured speeds and angles; a cushion rebound-speed polynomial; cloth friction.
   - Alciatore's technical proofs, which were checked against experiments: throw vs. cut angle and speed, squirt for 3 real cues, the 90°/30° rules with e and μ, and stun/draw distances.
   - Practice systems (Corner-5, Plus) and the WPA cushion-speed test as Tier D.
7. **Performance targets (DERIVED, §7):**
   - A typical non-break shot takes ≤ 100 µs median and ≤ 1 ms p99 per core.
   - A 9-ball/8-ball break takes ≤ 2 ms median.
   - Throughput is ≥ 10,000 typical shots/s per core and scales ≥ 0.8·N across 6–8 worker threads.
   - That covers 50–100 k simulations per AI decision in ≤ 1.5 s on 6 threads, which is about 2–10× more than a CueCard-style Monte-Carlo search needs.

---

## 2. pooltool — deep dive (the reference implementation)

### 2.1 Facts

| Item | Value | Source |
|---|---|---|
| Repository | github.com/ekiefl/pooltool (≈400 stars in Sep 2026; last push 2026-09-25) | [PT-GH] |
| Author | Evan Kiefl; the project has a Discord and external contributors (e.g., the 3D work by derek-mcblane) | [PT-GH] |
| License | **Apache-2.0** (`LICENSE.txt`). There is no NOTICE file. | [PT-GH] |
| Publication | Kiefl, E. (2024). *Pooltool: A Python package for realistic billiards simulation.* JOSS 9(101), 7301, doi:10.21105/joss.07301. It is also peer-reviewed by pyOpenSci. | [PT-JOSS] |
| Language | Python 3; hot paths are JIT-compiled with Numba; the 3D GUI uses Panda3D | [PT-JOSS] |
| Releases | v0.4.2 (2024-10), v0.4.3/0.4.4 (2025-03), v0.5.0 (2025-09-18), **v0.6.0 (2026-03-15)** | [PT-CL] |
| Games | 8-ball, 9-ball, snooker, three-cushion, sum-to-three | [PT-GH] |
| Stated purpose | Research-grade, general-purpose simulator for AI, robotics and computer vision; speed, flexibility, introspection | [PT-JOSS] |

The JOSS paper positions pooltool against:
- **FastFiz**: the AAAI/ICGA computational-pool tournament simulator. It is unmaintained, has no modularity and only 2D visualisation.
- **Billiards** (Lua): realistic, but not interoperable and without Windows support.
- **FooBillard++**: focused on game aesthetics.
- **Commercial simulators** (ShootersPool, Virtual Pool 4): realistic physics, but closed source and without an API [PT-JOSS].

### 2.2 Architecture (as of `main`, Sep 2026)

- **Data model**
  - `System` = balls + table + cue.
  - Each `Ball` has:
    - `params`: m, R, u_s, u_r, u_sp, u_b, e_b, e_t, e_c, f_c, g
    - `state`: a 3×3 array `rvw` = [r; v; ω], a motion-state label `s`, and a time `t`
    - `history`
  - The table consists of linear cushion segments, circular cushion segments (the pocket jaws) and pockets (circles).
- **Main loop** (`evolution/event_based/simulate.py`, paraphrased):
  1. Ask the detector for the next event across all event types, using the caches.
  2. If there is no event (t = ∞), stop.
  3. Require `dt = t_event − t_now ≥ 0`; otherwise raise `SimulateError`.
  4. Evolve **every** ball analytically by dt. The code itself flags this with a FIXME as inefficient.
  5. Resolve the event with the configured strategy and append it to the history.
  6. Update the transition cache for the balls involved, and invalidate every cached pair time that involves them.
  7. Stop early if `t_final` is reached or `num_events > max_events`. In the second case, all balls are set to stationary.
- **Detector** (`detect/detector.py`)
  - It collects the earliest candidate from:
    - stick–ball (only at t = 0)
    - transitions (cached)
    - ball–linear-cushion, ball–circular-cushion and ball–pocket
    - ball–ball
    - ball–table (3D only)
  - **Tie-break for exactly equal times:**
    - tier 1: stick–ball
    - tier 2: transitions and pocket events
    - tier 3: ball–ball, ball–cushion and ball–table
    - within a tier, higher energy goes first (introduced in PR #257)
- **Caches**
  - `TransitionCache`: next transition per ball.
  - `CollisionCache`: event time per (type, pair), invalidated by ball id.
  - The ball–ball detector skips pairs where both balls are non-translating or either ball is pocketed.
  - An already-overlapping pair gets `t = t_now`, i.e., it is resolved immediately (v0.6.0 change).
- **Resolver:** strategy objects per event type. They are serialized to `~/.config/pooltool/physics/resolver.yaml` (a separate `resolver_3d.yaml` exists), with a version guard (`VERSION = 13`). A stale file is replaced by the defaults.
- **Playback:** `continuize(dt=0.01)` samples the analytic trajectories at a fixed step for visualisation. The docs note that 0.01 s looks right at 60 fps [PT-EVO].
- **Introspection:** `simulate_with_snapshots()` records pre-evolve, post-evolve and post-resolve states for every step (PR #232). Worth copying as a debug feature.

### 2.3 Motion states and transition times (pooltool = Leckie & Greenspan, in our notation)

| State | Meaning | Duration until the next transition |
|---|---|---|
| stationary | v = 0, ω = 0 | ∞ (changes only by collision) |
| spinning | v = 0, ω = (0, 0, ω_z) | `2 R |ω_z| / (5 u_sp g)` = `|ω_z| / alpha_sp` |
| sliding | u ≠ 0 | `2 |u| / (7 mu_s g)` |
| rolling | u = 0, v ≠ 0 | `|v| / (mu_r g)`. The next state is spinning if ω_z is still non-zero, otherwise stationary. |
| pocketed | terminal | ∞ |
| airborne (3D) | z > R or v_z < 0 at z = R | the time of the ball–table event |

- pooltool's `u_sp` has units of length: `u_sp = (10·2/5/9)·R`.
  - This gives alpha_sp = 5 u_sp g /(2R) = 10.9 rad/s² for any R.
  - That lies inside Alciatore's measured 5–15 rad/s² [DD-CONST].
  - The RawBreak motion spec parametrizes alpha_sp directly (default 10 rad/s²).

### 2.4 Model catalogue (pooltool `main`, Sep 2026)

| Interaction | Models available | Default (2D engine) | Notes |
|---|---|---|---|
| Ball–ball | `frictionless_elastic`; `frictional_inelastic_2d/3d` (Alciatore TP A.5/A.6/A.14, extended to both balls in vector form); `frictional_mathavan` (Mathavan 2014, iterative impulse integration with `num_iterations` = 1000) | `frictional_inelastic_2d` with the Alciatore friction law mu(v) = a + b·exp(−c·v): **a = 0.009951, b = 0.108, c = 1.088** (v = relative sliding speed, m/s) | Mathavan was the default in v0.4.4 and was replaced later. The 2D variant zeroes v_z. |
| Ball–linear cushion | `han_2005`; `mathavan_2010`; `impulse_frictional_inelastic_2d/3d`; `stronge_compliant_2d/3d`; `unrealistic` (mirror reflection, spin untouched) | **`stronge_compliant_2d`, `omega_ratio = 1.8`** (default since v0.6.0, replacing Mathavan 2010) | The Stronge model: tangential compliance with slip reversal, solved analytically. Han 2005 had "correctness issues" fixed in PR #247. |
| Ball–circular cushion (jaws) | same set | `stronge_compliant_2d` | Used at pocket jaws |
| Ball–pocket | `canonical` | `canonical` | The ball is **pocketed the instant its center enters the pocket circle** and is placed at the pocket center below the table. No rattle, drop-edge, or back-of-pocket physics. |
| Stick–ball | `instantaneous_point_2d/3d` (Leckie & Greenspan / Alciatore TP A.30; elastic; finite tip radius maps (a, b) to the contact point; squirt per TP A.31) | `english_throttle = 1`, `squirt_throttle = 1`; default pool cue M = 0.567 kg, tip radius 0.0106 m, end mass m_b/30 | No tip COR, no miscue, no stroke dynamics |
| Ball–table (3D) | `frictional_inelastic` (TP A.14-style slip/no-slip impulse); `frictionless_inelastic` | `frictional_inelastic`, **`min_bounce_height = 0.005 m`** | The Zeno guard sets v_z = 0 if the next apex would be below 5 mm |
| Transitions | `canonical` | `canonical` | Snaps near-zero components to exactly 0, with tolerance 1e-12 (§5.8) |

### 2.5 pooltool default parameters (pool ball, `POOL_GENERIC`)

| Param | Value | Our comment |
|---|---|---|
| m | 0.170097 kg | same as `BALL_MASS` |
| R | 0.028575 m | same |
| u_s | 0.2 | = our mu_s default |
| u_r | 0.01 | = our mu_r default (gives a 7.2 s lag over 2.54 m; test CLOTH-03) |
| u_sp_proportionality | 10·2/5/9 m⁻¹ (alpha_sp = 10.9 rad/s²) | we use alpha_sp = 10 rad/s² |
| u_b | 0.05 | used only by some models. The Alciatore law gives 0.01–0.12 depending on slip speed. |
| e_b | 0.95 | inside Alciatore's 0.92–0.98 |
| e_c | 0.85 | model-dependent (see the cushion spec) |
| f_c | 0.2 | cushion friction |
| e_t | 0.5 | inside the 0.5–0.7 range [DD-CONST] |
| default pool table | 7-ft (l = 1.9812 m), corner-pocket mouth 0.118 m, pocket radius 0.062 m, cushion height 0.64·2R | our tables follow `equipment.md` instead |
| rack spacing | balls placed within a virtual radius R(1 + 1e-3) and randomly "wiggled" by up to 1e-3·R | gives random gaps of 0 to ≈57 µm (§5.6) |

### 2.6 pooltool numerical machinery (constants worth knowing)

| Mechanism | Value | Where |
|---|---|---|
| `EPS` | 100 × machine epsilon ≈ 2.2e-14 (used for times, speeds and |u| checks) | `constants.py` |
| `MIN_DIST` spacer | **1e-6 m**. `make_kiss` sets ball–ball separation to 2R + 1 µm before resolving. The balls are moved along their own velocities (r + t·v); the fallback moves them along the line of centers if the midpoint would shift by more than 5 µm. Cushions work the same way (R + nose radius + 1 µm). | `resolve/ball_ball/core.py`, `ball_cushion/core.py` |
| Continually-touching fix | If the radial relative speed is < 0.01 m/s and the velocities are aligned (cosine > 0.9), the "chased" ball takes 10 % of the "chaser's" radial momentum. This is phenomenological; the author explicitly says it is not grounded in theory. | PR #257 |
| Root realness | For |Re| > 1e-3 the root is real if |Im| < 1e-9; otherwise if |Im|/|Re| < 1e-3 | `ptmath/roots/core.py` |
| Cushion root gating | Reject roots with t ≤ EPS, outside the segment (s ∉ [0, 1]), or with approach speed ≤ EPS | `detect/ball_cushion.py` |
| Quartic solver | Algorithm 1010 (Orellana & De Michele 2020), translated to Numba: 2.8 M quartics/s (the C original reaches 3.3 M/s; C called directly: 0.34 µs per polynomial). It replaced a hybrid analytic/companion-matrix solver in v0.6.0. | PRs #80, #235, #236 |
| Degenerate quartic | The leading coefficient is 0 when the relative acceleration is zero. This is dispatched to the quadratic solver, and an exact-contact t = 0 root is kept only if the balls are closing. | PR #354 |
| Parameter validation | Zero or negative friction coefficients are rejected, because they made the simulation hang with endless events | Issues #263/#264, PR #265 |

### 2.7 Known limitations of pooltool (relevant for a realism game)

1. **Pockets are circles.** A ball is pocketed when its center crosses the circle. There are no rattles, no ball hanging on the lip, no facing-to-facing bounces inside the throat and no drop-edge physics. Jaw cushions exist as circular/linear segments.
2. **3D / airborne support is still in development** in 2026 (support-3d issues and PRs #295–#383). The default engine is 2D, where cushion and ball–ball resolvers clamp v_z = 0, so there are no jump shots, no balls leaving the table, and no rail-induced hops in the default engine.
3. **Instantaneous pairwise collisions.** A multi-ball contact (rack, frozen combos) is resolved as a sequence of pairwise impulses. The result depends on the order and on the random rack gaps; the "continually touching" hack is needed for Newton's-cradle cases.
4. **Cue model:** elastic and instantaneous. There is no tip COR (0.71–0.75 for leather [DD-CONST]), no miscue limit, no cue elevation "dig" into the slate in 2D, and no stroke or follow-through.
5. **Cloth:**
   - constant mu_s and mu_r
   - no speed dependence of sliding friction (Witters & Duymelinck report a rise from 0.14 to 0.21)
   - no nap
   - no table roll or slope
   - no ball-to-ball variation
   - no coupling between sidespin decay and linear motion (Mathavan 2009 observed "disk-like" coupling: sidespin ends when the ball stops)
6. **Performance** is limited by Python/Numba overhead and the GIL. Parallelism is only possible across processes. No shots/s benchmark is published (§7.1).
7. **Historical robustness bugs** (all fixed by v0.6.0 or earlier). They are useful as regression-test ideas:
   - Missed ball–ball collisions and balls passing through each other (#154, fixed by the `make_kiss` rework in #257).
   - A missed second cushion at a 45° corner hit, where the ball escaped the table (#217).
   - Endless near-zero-dt circular-cushion events (#239).
   - Hangs with zero friction (#264).
   - Degenerate quartics (#263/#354).
   - Reports that micrometre-level intersections were "still possible but extremely rare" (#154). When one happens, the overlapping pair is re-collided at t = 0.

### 2.8 What RawBreak takes, and what it changes

| Take (concept) | Change (implementation) |
|---|---|
| Motion states and transition formulas | Store each ball's trajectory as polynomial coefficients around its **own** reference time t0 (lazy evaluation). Only balls involved in an event are re-based; pooltool evolves every ball at every event. |
| Per-type detectors + pair cache with invalidation | A binary-heap priority queue with **lazy deletion via per-ball version stamps**, instead of a full min-scan per step |
| Pluggable resolvers (model catalogue) | Compile-time strategy interfaces in C++ (no RTTI; static dispatch or function tables). The model and parameters are recorded in every shot record for reproducibility. |
| Alciatore ball–ball friction law, TP A.30/A.31 cue and squirt, Stronge/Mathavan/Han cushions | Implemented from the papers, cross-checked against pooltool outputs (XREF tests) |
| `make_kiss` + 1 µm spacer | Approach-velocity gating plus a tiny contact tolerance (1e-9 m); positions are not moved except for rare de-penetration, which is logged (§5.4) |
| Equal-time priority tiers | A strict total order (time, tier, pair key); no energy-based ordering, because energy ties are themselves possible |
| "Momentum theft" for continually touching balls | A cluster simultaneous-impulse solver (§5.6) |
| Circle pockets | Full pocket geometry with rattles (pocket spec), possibly with a local fixed-step sub-simulator inside the pocket capture volume (§8) |

---

## 3. Other implementations and games

### 3.1 Open-source implementations

| Project | Approach | License | Physics notes | Relevance |
|---|---|---|---|---|
| **FooBillard / FooBillard++** (Florian Berger ~2002; FooBillard++ fork; Linux, OpenGL) [FB-WIKI][FB-GH] | **Fixed time step** with explicit Euler. Each step moves all balls, finds the earliest penetration (negative collision time in the step), rewinds to it, resolves, and recurses on the remaining dt (`proceed_dt_euler`). | GPL | `MU_SLIDE = 0.2`, `MU_ROLL = 0.03` (3× typical measured values), a constant ball–ball friction `MU_BALL` impulse, spot-radius spin friction (12 mm contact radius), slide threshold 1 cm/s. The pocket test is center within the hole radius. FooBillard++ added jump shots. | Shows the classic step-and-rewind pitfalls: dt-dependent results and missed grazes. Do not copy (GPL). |
| **Billiards** (Dimitris Papavasiliou, GNU Savannah) [BIL-NONGNU] | Built on "Techne", a scriptable simulator/renderer using **ODE** (rigid-body time stepping) plus Lua 5.1 | free software (copyleft; verify before any reuse) | Carom and pool games | Cited by pooltool as realistic but not interoperable, and Linux-only |
| **tailuge/billiards** (TypeScript, browser; very active in 2026) [TAIL-GH] | **Time-stepping** integration | **GPL-3.0** | Motion from Han 2005 with Kiefl's corrections; Mathavan 2010 cushions (paper figures recreated; missing equations inferred); Stronge compliant cushion; Alciatore TP A.14 throw. Supports trajectory fitting against recorded shots. | Reports **~500 rollouts/s on 4 CPU cores** (the only published throughput number for a modern open-source sim). Do not copy (GPL). |
| **PoolFiz / FastFiz** (Leckie & Greenspan, Queen's University; used in the AAAI/ICGA Computational Pool tournaments 2005–2008) [LG-2006][ARCH-2010] | **Event-based**, analytic; polynomial roots up to 4th order, closed form | research code (FastFiz mirror at github.com/ekiefl/FastFiz) | The original event-prediction formulation that pooltool follows | CueCard's team re-engineered it for a **~5× speed-up** [ARCH-2010] |
| **jzitelli/PoolPhysics** [ZIT-GH] | Event-based Python port of Leckie & Greenspan | MIT | mu_sp = 0.044 gives alpha_sp ≈ 38 rad/s², about 4× the measured value (see `physics-motion-and-cue.md` §A.2) | Small reference only |

### 3.2 Commercial and Steam games (what is known about their physics)

Closed-source games publish no technical details. The table below separates **developer claims** from **player and critic observations**. The player feedback is anecdotal (reviews and forums) and is used here only to build a realism checklist (§3.3).

| Game | Developer / year | Physics approach (known or claimed) | Praise | Criticism |
|---|---|---|---|---|
| **Virtual Pool 4** | Celeris, 2015 (series since the 1990s) [VP4] | Claims "completely rewritten physics": any real shot (spin, jump, massé, trick shots) is possible with true-to-life results. Adjustable cloth speed, rail speed, pocket size and pocket cut [DD-SIM]. | Bob Jewett (a BD columnist): accurate enough to test a shot before trying it on a real table; the CEO is a strong player who tuned the action to look right [AZB-VIRT]. Squirt, swerve and throw are modeled. | Some reviewers say subtle effects are missing (spin-induced throw is named) [VP4-REV] |
| **ShootersPool** | Eveho Ingeniería SL (Spain) [SP-WEB] | Marketing: balls, cushions and spin react as in a real game. No technical details published. | Often cited by serious players as the most realistic PC simulator, especially the pocket behaviour [AZB-VIRT]; pooltool's JOSS paper names it (with VP4) as the example of realistic commercial physics [PT-JOSS] | Few specifics published |
| **Pure Pool** | VooFoo Studios, 2014 | Proprietary | Widely praised feel for console pool | Players report cushions that feel too "slidey", and escapes from hooked (snookered) positions via 2+ cushions that seem too easy [PURE-REV] |
| **Pool Nation FX** | Cherry Pop Games, 2015/16 | Proprietary, with deforming cushions and chalk-dust visuals | Visuals, consistency | Players reported cloth too slow compared with tournament Simonis, too little spin, massé only working with exaggerated hops, and **oversized pockets** compared with WPA 4.5–4.625 in corner mouths. The developer promised fixes (2016) [PNFX-STEAM]. |
| **Pool Shark** | Motivation Stack LLC, released **2026-09-10** on Steam [PS-STEAM] | An 8-ball roguelike deck-builder with "real physics" (spin, banks, combos); free-to-play; ranked online 1v1 | too new (4 reviews at the time of writing) | Makes no realism claims; arcade/meta-game focus |

### 3.3 Realism checklist derived from player feedback (what RawBreak must get right)

1. **Cloth speed matches tournament cloth by default.** Simonis 860: mu_r ≈ 0.006–0.010, giving a 7–9 s lag over a 9-ft table (CLOTH-03).
2. **Pocket geometry per WPA** (`equipment.md` §5). No enlarged "arcade" pockets unless a bar table is chosen deliberately.
3. **Cushions must not be "slidey".** Friction at the cushion must remove tangential speed and change spin realistically. Validate against Mathavan 2009/2010 and the diamond systems (CUSH-*, SYS-*).
4. **Massé and swerve come from an elevated cue plus side spin on the cloth, without the ball hopping.** The ball leaves the slate only when the cue's downward impulse is large enough (the airborne spec). Test: massé final direction (KIN-05).
5. **Throw (collision- and spin-induced) and squirt must be present**, with correct magnitudes and signs (THR-*, SQ-*).
6. **Pocket rattles** and speed-dependent effective pocket size (POCK-*).
7. **Escapes from hooks must obey real angles.** This follows from items 3 and 5.

---

## 4. Academic and technical literature

| Work | Contribution | Numbers we use |
|---|---|---|
| **Leckie & Greenspan** — "An Event-Based Pool Physics Simulator", *Advances in Computer Games 11*, LNCS 4250, pp. 247–262 (2006), doi:10.1007/11922155_19; also "Pool Physics Simulation by Event Prediction 1: Motion Transitions", ICGA J. 28(4) (2005), and "…2: Collisions", ICGA J. 29(1) (2006) [LG-2006] | Parametrizes ball motion so that event times are the roots of polynomials of degree ≤ 4. Motion states: sliding, rolling, spinning, stationary. Event-based, with no time stepping. Intended for game-tree search and robot pool (PoolFiz). | Transition times (§2.3); the quartic ball–ball and quadratic ball–cushion event equations |
| **Han, I.** (2005) — "Dynamics in carom and three cushion billiards", *J. Mech. Sci. Technol.* 19(4):976–984 [HAN-2005] | Cushion impulse model with the contact above the ball center (friction and restitution). Carom/three-cushion motion equations. | Contact angle θ = asin((h − R)/R). pooltool and tailuge both needed corrections to the published equations (Kiefl's blog; pooltool #247). |
| **Mathavan, Jackson & Parkin** (2009) — "Application of high-speed imaging to determine the dynamics of billiards", *Am. J. Phys.* 77(9):788–794, doi:10.1119/1.3157159 [MAT-2009] | Overhead high-speed camera (up to 1000 fps; 1 mm tracking; snooker). Measures cloth friction, cushion rebound and ball–ball collisions. | Rolling deceleration **0.124–0.126 m/s²** (μ_r ≈ 0.0127–0.0129). Sliding deceleration **1.75–2.40 m/s²** (μ_s ≈ 0.178–0.245), 15–20× rolling. Cushion: rebound vs. incident speed for rolling perpendicular shots; linear fit **e = 0.818** over 0.28–3.5 m/s; better quadratic fit `y = −0.0877x² + 1.131x − 0.0953`; low-speed slope ≈ 0.91. Table I: 5 oblique shots. |
| **Mathavan, Jackson & Parkin** (2010) — "A theoretical analysis of billiard ball dynamics under cushion impacts", *Proc. IMechE C* 224(9):1863–1873, doi:10.1243/09544062JMES1964 [MAT-2010] | Cushion impact ODEs over accumulated impulse, with slip at the cushion and the table contacts. Contact height h = 7R/5 (snooker and pool). | Fitted to their experiment: **e_c = 0.98, μ_c = 0.14** (with μ_s = 0.212, M = 0.1406 kg, R = 26.25 mm). For rolling balls the rebound angle is nearly independent of speed; the speed loss is largest near 40° incidence. |
| **Mathavan, Jackson & Parkin** (2014) — "Numerical simulations of the frictional collisions of solid balls on a rough surface", *Sports Eng.* 17:227–237, doi:10.1007/s12283-014-0158-y [MAT-2014] | 3D frictional ball–ball impact including the **table contact** during impact. Validated against the 2009 shots. | Fitted **μ_bb = 0.05, e = 0.89** (snooker); μ_s = 0.21. Velocity errors mostly < 5 %; object-ball angle errors < 5 %; cue-ball angle errors up to about 11 %. |
| **Alciatore, D.** — *The Illustrated Principles of Pool and Billiards* (2004) and its **Technical Proofs** [DD-TP] | Closed-form models, many checked against experiments | TP 3.1 (90° rule), 3.3 (30° rule), 4.1 (stun/roll distances), A.4 (post-impact CB path), A.5/A.6 (e and μ corrections), A.14/A.28 (throw), A.30 (cue tip offset), A.31/B.1 (squirt), 6.3 (rail COR), 7.2 (Corner-5), A.19 (massé), 3.5–3.8 (pocket target sizes) |
| **de la Torre Juárez, M.** (1994) — "The effect of impulsive forces on a system with friction: the example of the billiard game", *Eur. J. Phys.* 15(4):184–190 [DLTJ-1994] | Shows that impulsive forces create impulsive **friction** at the ball–table contact during collisions, so the table cannot be ignored during impact | Motivation for including table friction in ball–cushion impacts (Mathavan 2010) and ball–ball impacts (Mathavan 2014) |
| **Wallace & Schroeder** (1988) — "Analysis of billiard ball collisions in two dimensions", *Am. J. Phys.* 56(9):815–819 [WS-1988] | Ideal rolling-ball collision predictions (the 30° rule family) | "Theoretical" columns of Mathavan 2009 Table I (reproduced exactly by our DERIVED formulas, §6.4) |
| **Cross, R.** (2008) — "Cue and ball deflection (or 'squirt') in billiards", *Am. J. Phys.* 76(3):205–212 [CROSS-2008] | Measured squirt vs. tip offset. A chalked tip grips the ball (small squirt); an unchalked tip slides (large squirt). | Squirt is a few degrees; it depends on grip, end mass and offset |
| **Shepard, R.** (2001) — "Everything you always wanted to know about cue ball squirt…" [SHEP-2001] | End-mass theory of squirt | Predicts end-mass ratios of 20–100. Alciatore's cue data give 12–29 (TP B.1). |
| **Marlow, W. C.** (1994/95) — *The Physics of Pocket Billiards* [MARLOW] | Ball–ball friction vs. speed data (the basis of the Alciatore friction law) and a rail COR | Friction ≈ 0.11 / 0.06 / 0.01 at relative slip ≈ 0.07 / 0.71 / 7.1 m/s (TP A.14). Rail COR 0.55 (disputed by Mathavan 2009). |
| **Coriolis, G.-G.** (1835) — *Théorie mathématique des effets du jeu de billard* | Origin of the 5/7 rolling result and the parabolic path of a sliding ball | Invariant KIN-05 |
| **Kim, H.-C.** (2024) — "Collision of two spinning billiard balls and the role of table", arXiv:2402.13258 [KIM-2024] | Theory of table friction during a ball–ball collision; the CB can briefly move backward | e* 0.92–0.98; μ_bb 0.03–0.08; static ball–table friction 0.2–0.4; contact time 250–300 µs |
| **Stronge, W. J.** — *Impact Mechanics* (2018), Ch. "Tangential compliance…" | Compliant-contact impact with stick/slip/reversal regimes, solved analytically | pooltool's default cushion model (`omega_ratio` = 1.8) |
| **Orellana & De Michele** (2020) — "Algorithm 1010: Boosting efficiency in solving quartic equations with no compromise in accuracy", *ACM TOMS* 46(2), art. 20, doi:10.1145/3386241 [OQS-2020] | Quartic → two quadratics, with careful error handling and Newton–Raphson refinement | Used by pooltool since v0.6.0. We do **not** copy its code (license, OQ-1). |
| **Archibald, Altman, Greenspan & Shoham** (2010) — "Computational Pool: A New Challenge for Game Theory Pragmatics", *AI Magazine* 31(4):33–41 [ARCH-2010] | CueCard (winner in 2008): Monte-Carlo shot search on a noisy simulator | Tournament noise σ: φ 0.125°, θ 0.1°, V 0.075 m/s, a and b 0.5 mm. **25–100 noise samples per candidate** (sweet spot 30–50). 10 min per game. 20 CPUs. The re-engineered simulator was about 5× faster. |
| **Smith, M.** (2007) — "PickPocket: A computer billiards shark", *Artif. Intell.* 171(16):1069–1091 [SMITH-2007] | Probabilistic vs. Monte-Carlo search; won the 2005/2006 Olympiads | "Hundreds of nodes per second"; 60 s per shot in experiments |

---

## 5. Numerical robustness of event-based simulators

### 5.1 Failure-mode overview

| # | Pitfall | Symptom | pooltool handling | RawBreak decision |
|---|---|---|---|---|
| R1 | **Zeno: frozen / continually touching balls** (rack, Newton's cradle, frozen combos, a ball pinned between two balls) | Collisions microseconds apart that never end; the sim stalls | 1 µm spacer, momentum theft, exact-time tiers (#257) | Event-rate detector → cluster simultaneous-impulse solver (§5.6); hard event cap |
| R2 | **Zeno: ball creeping into a cushion** (rolling or overspinning ball rebounds weakly, then spin drives it back) | Endless cushion events with a geometrically shrinking v_n | Approach-speed gate `> EPS`; issue #239 (fixed in 0.5.0) | Resting-contact threshold v_rest = 1e-3 m/s: below it the normal restitution is 0 and the ball stays in contact (§5.2) |
| R3 | **Zeno: bouncing ball** (airborne) | An infinite series of bounces in finite time (sum of a geometric series) | min_bounce_height 5 mm | h_min = 0.5 mm (DERIVED; visible-bounce criterion) |
| R4 | **Simultaneous events** (exact ties: symmetric shots, rack, corner double-cushion) | Order dependence; a missed second event (#217: ball escaped the table) | Tie tiers + energy | Strict total order + re-detection after each resolution; corner test ROB-03 |
| R5 | **Near-simultaneous events** (Δt ~ 1e-15 s) | Evolving to the first event puts the ball fractionally past the second contact → negative or complex roots → the event is missed | `make_kiss` spacer; overlap ⇒ immediate collision | Gap-function isolation that accepts f(0) ≤ δ with closing speed (§5.5); de-penetration logged |
| R6 | **Quartic ill-conditioning** | Wrong earliest root; spurious roots; missed grazes | Hybrid solver → Algorithm 1010; realness tolerances | Validity-window isolation on scaled variables; Newton polishing on the exact gap function (§5.5) |
| R7 | **Degenerate polynomial** (leading coefficient 0) | Solver error or garbage | Dispatch to the quadratic solver (#354) | Degree reduction by relative-magnitude test, then the same isolation routine |
| R8 | **Extrapolating a polynomial past a transition** | "Ghost" collisions after a ball has stopped (the parabola turns back) | Mostly implicit (transitions come first in time) | Always bound the root search by the validity window (ROB-13) |
| R9 | **Transition round-off** (|u| ≈ 1e-17 after sliding, instead of 0) | State chatter (sliding ↔ rolling), extra events | Snap with a 1e-12 tolerance; asserts | Construct exact post-transition states (§5.8) |
| R10 | **Culling with velocity sign** | Missed collisions with massé/curving balls (the velocity direction changes) | Rejected on purpose (#217 discussion) | Conservative swept-bounds culling over the validity window only (§5.10) |
| R11 | **Degenerate parameters** (μ = 0) | Infinite loop | Reject at construction (#264) | Reject + clamp in the API; unit test ROB-06 |
| R12 | **Floating-point nondeterminism** | Replays diverge; AI and online desync | n/a (Python) | No fast-math, no FMA contraction differences, a fixed evaluation order, a deterministic heap order (§5.9) |
| R13 | **Segment joints and corners** (cushion line meets jaw arc) | A ball slips through the gap or hits a phantom edge | Overlapping segments; `make_kiss` for cushions | Watertight cushion topology with closed chains; parameter s ∈ [0, 1] with tolerance; unit tests at every joint (ROB-14) |

### 5.2 Zeno behaviour in detail

**Definition.** A Zeno execution produces infinitely many events in a finite time. Event-based simulators hit it when an idealised model has an accumulation point. There are three classic billiards cases.

1. **Bouncing ball (R3).** Let the pre-impact vertical speed be v₀ and let restitution scale it by e_t at each bounce.
   - The flight times are 2v₀e_t/g, 2v₀e_t²/g, …, so the total time is (2v₀/g)·e_t/(1−e_t), which is finite.
   - DERIVED example: a drop from h = 0.05 m with e_t = 0.5 takes `sqrt(2h/g)·(1 + 2e_t/(1−e_t)) = 0.3029 s` in total, with infinitely many bounces.
   - **Guard:** if the post-impact apex `v_z²/(2g) < h_min`, set v_z = 0 (the ball lands and slides or rolls).
     - pooltool uses h_min = 5 mm, i.e., v_z < 0.313 m/s.
     - We choose **h_min = 0.5 mm (v_z < 0.099 m/s)**. Bounces of a few millimetres are visible in close-up first-person camera shots, and a 0.5 mm hop is below the cloth-fibre scale. This is the visual criterion (DERIVED/ESTIMATE; tunable).
2. **Cushion creep (R2).** A rolling ball that hits a cushion head-on keeps its topspin. After rebounding with normal speed e·v_n it is sliding with "draw" relative to its new direction, so friction decelerates it and may drive it back into the rail.
   - The next impact speed is again a fraction of the previous one. The number of events to reach speed v_th grows like `log(v_th/v_n0)/log(e_eff)`.
   - This is finite in floating point, but it wastes events, and at very small speeds rounding can create a genuine accumulation point.
   - **Guard:** if the cushion approach speed `v_n < v_rest = 1e-3 m/s`, resolve with e_n = 0 (plastic). The ball keeps only its tangential velocity and is flagged `restingOnCushion` until its tangential motion takes it away (DERIVED).
   - With e ≈ 0.8, a 1 m/s impact reaches 1e-3 m/s after about 31 rebounds in the worst case. In practice spin equilibrates much sooner.
3. **Frozen balls (R1).** Two touching balls that move together (a rolling pair after a combo, Newton's cradle, or a CB frozen to an OB and pushed by the cue) produce collisions with vanishing gaps and relative speeds.
   - Pairwise sequential impulses converge only geometrically, which means thousands of events.
   - **Guard:** a per-pair event-rate detector. If more than N_z = 16 events hit the same pair (or connected cluster) within Δt_z = 1e-4 s, collect the **contact graph** (all pairs with gap ≤ δ_cluster = 1e-7 m) and resolve it with a simultaneous impulse solver (§5.6).
   - Hard cap: 100,000 events per shot. On overflow, stop all balls, set `ShotResult::Truncated`, and log the full state. Every occurrence in testing is a bug to investigate.

### 5.3 Simultaneous events and determinism

- **Exact ties happen.** Examples: a CB aimed exactly at the contact point of two frozen OBs; a symmetric break; a ball arriving at the corner exactly at the 45° diagonal (#217); and the rack itself, where many pairs share t = 0 once they touch.
- **Ordering rule (RawBreak):** process events by the key `(t, tier, pairKey)`:
  - `t` is the event time, compared exactly as a double.
  - `tier`:
    - 0: stick–ball
    - 1: transitions
    - 2: pocket capture
    - 3: ball–cushion
    - 4: ball–ball
    - 5: ball–table
  - `pairKey` = (min id, max id, segment id).
  - Ids are stable integers (0 = cue ball, 1–15 = object balls).
- After resolving one event, **all** events of the involved balls are re-detected. The next tied event is therefore evaluated with updated states and may disappear, which is physically correct: the second cushion of a corner hit is re-evaluated after the first.
- Energy-based ordering (pooltool) is not used, because energies can tie and the rule adds no physics.
- A *physically* symmetric outcome for symmetric ties requires the cluster solver (§5.6). Test ROB-09 documents both behaviours.

### 5.4 Tolerances (RawBreak proposal)

| Name | Value | Rationale (DERIVED) |
|---|---|---|
| `kContactTol` δ | **1e-9 m** | Double resolution at table scale is ≈ 1.3 m × 2.2e-16 ≈ 3e-16 m. Polynomial evaluation error is ≈ 1e-14 m. 1e-9 m is 5 orders above the noise and 5 orders below the ball tolerance (127 µm). |
| `kClusterTol` | 1e-7 m | Pairs with gap ≤ 0.1 µm count as "frozen" for the cluster solver. Real racks are tighter or looser than this; see §5.6. |
| `kApproachTol` | 1e-9 m/s | Minimum closing speed for accepting a t ≈ 0 contact |
| `kRestCushion` v_rest | 1e-3 m/s | Zeno guard R2 (a 1 mm/s rebound is invisible) |
| `kMinBounce` h_min | 5e-4 m (v_z 0.099 m/s) | Zeno guard R3 |
| `kSlipZero` | 1e-12 m/s | Slip below this counts as rolling (post-transition snap) |
| `kTimeHorizon` | 600 s | The maximum shot time. A ball rolling 2 m/s with mu_r = 0.005 stops after 41 s, so this is only a safety net. |
| `kMaxEvents` | 100,000 | A pro break is typically < 1000 events (§7.2) |
| `kZenoRate` | 16 events per pair per 1e-4 s | Switch to the cluster solver |
| Time comparisons | exact (no epsilon) | Needed for a strict total order |
| Root acceptance | f(t) sign change with f′ < 0, or f(0) ≤ δ_f with f′(0) < 0 | Removes pooltool's complex-part heuristics |

These are defaults. All tolerances live in one `NumericsConfig` struct that is recorded with every shot.

### 5.5 Quartic (and quadratic) roots

**Ball–ball gap function (both balls on the cloth, 2D).** DERIVED; this matches pooltool's `ball_ball_collision_time_2d`.
- Each ball's center within its current state is `r_i(t) = c_i + b_i t + a_i t²`, where a_i = ½ × the constant acceleration of its state.
- The relative position is Δr(t) = C + B t + A t², with C = c_i − c_j, B = b_i − b_j and A = a_i − a_j.
- The gap function is `f(t) = |Δr|² − (R_i + R_j)² = |A|² t⁴ + 2(A·B) t³ + (|B|² + 2 A·C) t² + 2(B·C) t + (|C|² − (R_i+R_j)²)`.
- A collision is the **first t in [0, T_w] with f(t) = 0 and f′(t) < 0**, where `T_w = min(transition time of i, transition time of j, horizon)` measured from the common reference time.

**Why not a closed-form quartic?**
1. The polynomials are valid only inside T_w. Roots outside the window are artifacts (R8).
2. Closed forms (Ferrari, Cardano) lose accuracy through cancellation. pooltool's hybrid solver needed a numeric fallback, and it then moved to Algorithm 1010.
3. A grazing contact is a near-double root. Its imaginary part is O(sqrt(ε)), so realness thresholds of ~1e-9 accept or reject graze distances of order v·1e-9 s arbitrarily.

**RawBreak algorithm** (deterministic, allocation-free):
1. **Scale.** τ = t / T_w ∈ [0, 1], with lengths in units of R. The coefficients become O(1).
2. **Degree reduction.** If |A|² ≤ 1e-14·max(|coefficients|), treat A = 0 exactly and drop the degree (to quadratic or linear). This covers equal accelerations: both balls rolling in the same direction, or both in constant-velocity states.
3. **Critical points.** Solve f′(τ) = 0, a cubic, with a robust cubic solver that uses the trigonometric form for three real roots and Cardano otherwise, followed by a Newton polish. Keep the roots in (0, 1), sorted. They split [0, 1] into at most 4 monotone intervals.
4. **First entry.** Scan the intervals in order. For each monotone interval [τ_a, τ_b] where f(τ_a) > 0 ≥ f(τ_b), find the root with safeguarded Newton + bisection (Brent-style) to |Δτ| ≤ 4 ulp, then un-scale t = τ T_w.
5. **Start contact.** If f(0) ≤ δ_f (with δ_f = 2(R_i+R_j)·δ) and f′(0) < 0, the collision is at t = 0 (this includes a slight penetration due to rounding). If f(0) ≤ δ_f and f′(0) ≥ 0, the balls are separating, so ignore the root at 0.
6. **Graze.** If a local minimum of f in (0, 1) has −δ_f < f_min ≤ δ_f, the balls touch tangentially without closing speed. Treat this as **no collision**, deterministically.

**Cost:** about 0.1–0.4 µs per pair in C++ (DERIVED estimate: one cubic, 2–6 Newton steps, a few Horner evaluations). That is comparable to Algorithm 1010's C figure of 0.34 µs [PT-PR235].

The same isolation applies to:
- ball–pocket capture (quartic)
- ball–circular-cushion (quartic)
- ball–linear-cushion (quadratic, closed form with the stable `q = −½(b + sign(b)·sqrt(disc))` variant)
- ball–table (quadratic)

**Cross-check:** during development, compare every root with a long-double or MPFR brute-force reference on 10⁶ random configurations (test ROOT-01).

### 5.6 Touching balls, the rack, and the cluster solver

- **Physics of the rack.** In a real "tight" rack the balls touch or nearly touch. The break energy passes through the pack in about 0.2–0.3 ms per contact (collision time 250–300 µs [KIM-2024]). This is a *simultaneous* multi-body impact. Its outcome depends on the gap distribution, which makes it effectively chaotic.
  - pooltool models it with random gaps of 0–57 µm and sequential pairwise impulses.
  - With exactly zero gaps and pairwise resolution, the result depends on the order in which pairs are processed.
- **RawBreak approach:**
  1. The rack generator (`rules.md` / `equipment.md` §9) produces **realistic gaps**: most pairs touching (gap 0 ± 20 µm, clamped to ≥ 0) and a few gaps of 0.1–0.5 mm. This models rack quality and gives a game-level "tight rack" vs. "loose rack" setting.
  2. When a collision involves a ball that is part of a contact cluster (gap ≤ `kClusterTol`), resolve the **whole cluster simultaneously**. Use a sequential-impulse / projected Gauss–Seidel solver on the contact graph with per-contact restitution (Poisson or Newton model) and Coulomb friction. Iterate to convergence (relative impulse change < 1e-12, max 200 iterations). Contacts are ordered by stable pair key, so the iteration is deterministic.
  3. Then continue event-based. In most breaks the cluster solver runs once at the first impact and a few times afterwards.
- **Validation:**
  - Newton's cradle (ROB-01)
  - the symmetric two-ball hit (ROB-09)
  - break statistics against expectations (BRK-*)
- **Open research question (OQ-5):** the correct restitution law for simultaneous multi-contact impacts is non-trivial (see Stronge). We choose the model that makes Newton's cradle and frozen-combo throw look right (Tier D).

### 5.7 Cushion topology, corners and jaws

- Every cushion chain (a linear nose segment → a jaw arc → a facing segment → …) must be **watertight**. Adjacent primitives share endpoints exactly, and the contact test for linear segments accepts the segment parameter s ∈ [−σ, 1+σ] with σ = δ/segment length. The arc covers the joint.
- **The corner (two cushions nearly simultaneously)** must re-detect after the first impulse (§5.3). pooltool #217 shows the failure: a 45° shot into a pocketless corner escaped the table. Test ROB-03.
- **A ball placed exactly touching a cushion** and shot into it: the contact at t = 0 must be accepted (f(0) ≤ δ_f, closing). Test ROB-04.

### 5.8 Transitions and state snapping

At a transition time the new state is **constructed exactly**, not taken from the evolved values:
- **Sliding → rolling:** keep v (evolved) and set the horizontal ω so that u = 0 exactly: ω_x = −v_y/R and ω_y = v_x/R (DERIVED from u = v + ω × (−R ẑ) = 0). Keep ω_z.
- **Rolling → spinning:** set v = 0, ω_x = ω_y = 0, and keep ω_z.
- **→ stationary:** set everything to 0.
- Assert that the discarded residuals are ≤ 1e-9 of the pre-transition magnitudes. If not, flag a bug. pooltool asserts 1e-12 absolute.

### 5.9 Time representation and floating-point determinism

- Store absolute shot time as a double. Store each ball's trajectory relative to its own `t0` (the last event involving it), so polynomial arguments stay small (typically < 10 s). Re-base only the balls involved in an event.
- **Determinism rules for BilliardsCore:**
  - no `-ffast-math` / `/fp:fast`
  - `-ffp-contract=off` (or `#pragma STDC FP_CONTRACT OFF`, and on MSVC `/fp:precise`)
  - no `long double` differences between compilers
  - no unordered containers in iteration paths
  - deterministic heap tie-break
  - no parallelism inside one shot (parallelism happens only across shots)
- **Unreal:** BilliardsCore is compiled inside a UE module as well as standalone. The UE toolchain's floating-point flags must be checked; historically MSVC builds of UE have used fast floating-point settings (INTERPRETATION, OQ-3). If they do, BilliardsCore gets a per-module override or ships as a prebuilt static library built with our CMake flags.
- CI test ROB-10 compares a golden hash of 1000 shot records between the standalone build and the UE-module build.

### 5.10 Caching, invalidation and broad-phase culling

- **Priority queue:**
  - Each ball has a `version` counter that is incremented whenever its state changes.
  - Every queued event stores the versions of its balls at prediction time.
  - A popped event with stale versions is discarded (lazy deletion).
  - This replaces pooltool's "min over caches" scan.
- **Culling must be conservative.** Velocity-direction tests are invalid for sliding balls: a massé ball can reverse direction (pooltool #217 discussion). Use instead:
  - the swept axis-aligned bounds of the parabola over its validity window (a quadratic per axis, so the extremes are analytic)
  - a pair test against the other ball's bounds, inflated by R_i + R_j + δ
  - only pairs whose bounds overlap go to root isolation
- **Stationary–stationary pairs** are never tested. **Moving–stationary pairs** use the stationary ball's point bounds.

### 5.11 Parameter validation

- Reject μ ≤ 0, e ∉ [0, 1], R ≤ 0, m ≤ 0, and alpha_sp ≤ 0 at API level. This is an `Expected<…>` error, since there are no exceptions.
- Clamp the cue tip offset to the miscue limit (the cue spec) and the cue speed to 0–15 m/s.

---

## 6. Validation data (sources, derivations, numbers)

### 6.1 Cloth kinematics (analytic; Tier A)

These follow from the equations in `physics-motion-and-cue.md` Part A and are confirmed by Alciatore TP 4.1 and TP A.4 [DD-TP].

- **Sliding duration:** `τ_s = 2|u0| / (7 mu_s g)`.
- **Final rolling velocity after any sliding phase (Coriolis invariant):** `v_f = (5/7) v0 + (2/7) (ω0 × R ẑ)`.
  - Horizontal ω only; ω_z does not enter.
  - This is independent of mu_s (TP A.4 Eq. 24).
  - Equivalently `v_f = v0 − (2/7) u0`.
- **1D straight shot** (ω0 = topspin component, Rω0 > 0 is follow). DERIVED from TP 4.1:
  - `d_s = 2 (6v0² − 5 v0 Rω0 − (Rω0)²) / (49 mu_s g)` for u0 > 0 (sliding with less than natural roll).
  - For a stun shot (ω0 = 0): `d_s = 12 v0² / (49 mu_s g)`, `τ_s = 2 v0 / (7 mu_s g)`, `v_f = 5 v0 / 7`.
  - For overspin (u0 < 0), friction accelerates the ball: `d_s = v0 τ_s + ½ mu_s g τ_s²`.
- **Rolling to rest:** `t = |v|/(mu_r g)`, `d = |v|²/(2 mu_r g)`.

### 6.2 Measured cloth properties (Tier C)

| Quantity | Measured | Source |
|---|---|---|
| Rolling deceleration | 0.124–0.126 m/s² (μ_r = 0.0127–0.0129), snooker cloth | [MAT-2009] |
| Sliding deceleration | 1.75–2.40 m/s² (μ_s = 0.178–0.245); 15–20× rolling | [MAT-2009] |
| Ranges (pool) | μ_s 0.15–0.4 (typ. 0.2); μ_r 0.005–0.015; spin-down 5–15 rad/s²; ball–table e 0.5–0.7 | [DD-CONST] |
| Lag-time rule (Bob Jewett) | For a lag that rolls one table length L and stops: **μ_r = 2L/(g t²)** (DERIVED from L = ½ μ_r g t²). Jewett's rule of thumb: "t² × 2 ≈ 1/slope" for a 9-ft table, so 7 s ≈ 1 %. | [DD-CLOTH] |

The lag rule is the cheapest real-table calibration: a stopwatch in a real bar gives mu_r. With mu_r = 0.01 on a 9-ft table (L = 2.54 m): t = 7.196 s, starting speed 0.7059 m/s.

### 6.3 Ball–ball rules (Tier A with ideal parameters, Tier B with corrections)

- **90° rule** (TP 3.1): with equal masses, e = 1 and no ball–ball friction, a stun CB and the OB separate at exactly 90°. Immediately after impact, |v_CB| = v sin φ and |v_OB| = v cos φ.
- **Inelasticity and friction corrections** (TP A.5), stun CB, half-ball hit (φ = 30°).
  - Normal impulse per unit mass: (1+e) v cos φ / 2.
  - Friction limited by `min(mu_b (1+e) cos φ / 2, sin φ / 7)` (the 1/7 is the kinematic "sliding stops" limit).
  - With e = 0.94 and mu_b = 0.06: OB throw **3.434°**, CB deviation from the tangent line **3.307°**, separation **83.259°**.
  - e only: 87.025°. μ only: 86.566°.
- **30° rule** (TP 3.3), naturally rolling CB, e = 1, no ball friction.
  - Final CB deflection from its original direction: `θ_c = atan( sin φ cos φ / (sin² φ + 2/5) )`.
  - Half-ball (φ = 30°): **33.670°**. 1/4-ball (φ = 48.590°): **27.267°**. 3/4-ball (φ = 14.478°): **27.626°**.
  - Maximum **33.749° at φ = 28.126°** (sin φ = sqrt(2/9)).
  - Final CB speed at half-ball: 0.5579 v (DERIVED).
- **With e = 0.94** (TP A.6; DERIVED check): half-ball deflection **31.988°**; maximum **32.009° at φ = 28.995°**. TP A.6 also gives friction-only and combined values (35.907° / 34.026° at half-ball) under its own friction assumption. We reproduce those only as a Tier B cross-check with TP A.6's simplified friction model.
- **Head-on, rolling CB, e = 1:** after sliding, the CB follows at **2/7 v**. With e < 1 the result is `(5/7)(1−e)/2 v + 2/7 v` (e = 0.95: 0.30357 v).
- **Stop shot:** a stun CB head-on keeps `(1−e)/2 v` (e = 0.95: 0.025 v), which becomes a rolling creep of `(5/7)(1−e)/2 v`. The OB leaves at `(1+e)/2 v`.

### 6.4 Measured ball–ball collisions (Tier C): Mathavan et al. 2009/2014

Setup:
- Snooker balls: M = 0.1406 kg, R = 26.25 mm.
- Overhead camera, 45 fps for these shots, 0.25 mm position accuracy, velocity accuracy ≈ 0.011 m/s.
- The CB **rolls** into a stationary OB (ω_T = V0/R); sidespin is "very low" but unmeasured.
- The cut angle is defined by the measured OB direction.
- The values are speeds and CB directions **after the slip phase** (V_S).
- The fitted model parameters: μ_bb = 0.05, e = 0.89, μ_s = 0.21.

| Shot | V0 (m/s) | Cut θ (°) | Meas. V_S CB (m/s) | Meas. V_S OB (m/s) | Meas. CB exit angle (°) | Ideal (W&S) CB speed / angle / OB speed (DERIVED, matches the paper's "theoretical" columns) | Mathavan 2014 prediction CB / OB / CB angle |
|---|---|---|---|---|---|---|---|
| 1 | 1.539 | 33.83 | 0.816 | 0.836 | 35.96 | 0.931 / 33.08° / 0.913 | 0.914 / 0.831 / 31.93° |
| 2 | 1.032 | 26.36 | 0.520 | 0.629 | 33.20 | 0.529 / 33.67° / 0.660 | 0.520 / 0.599 / 32.45° |
| 3 | 1.364 | 40.52 | 0.925 | 0.700 | 30.50 | 0.934 / 31.00° / 0.741 | 0.917 / 0.676 / 29.91° |
| 4 | 1.731 | 46.50 | 1.275 | 0.787 | 27.97 | 1.301 / 28.33° / 0.851 | 1.28 (the paper prints 0.128, a typo) / 0.780 / 27.32° |
| 5 | 0.942 | 18.05 | 0.365 | 0.581 | 29.86 | 0.388 / 30.71° / 0.640 | 0.383 / 0.579 / 29.47° |

- The "exit angle" is measured from the CB's incoming direction (INTERPRETATION, consistent with the W&S formula reproducing the paper's theoretical values).
- The ideal model overestimates OB speed by 4–9 % (from e < 1 and throw losses). A realistic model must reduce the OB error.

### 6.5 Throw (Tier B model + Tier C measured)

**Alciatore model** (TP A.14, revised 2025) [DD-TP], in our words:
- Relative sliding speed at the ball–ball contact: `v_rel = sqrt( (v sin φ − R ω_z)² + (R ω_x cos φ)² )`.
  - v is the CB speed at impact.
  - ω_x is vertical-plane spin (follow > 0; natural roll ω_x = v/R).
  - ω_z is sidespin.
- Friction law (Marlow data fit): `mu(v_rel) = a + b·exp(−c·v_rel)` with a = 9.951e-3, b = 0.108, c = 1.088 (v_rel in m/s).
- OB tangential speed: `v_OBt = min( mu(v_rel) v cos φ / v_rel, 1/7 ) · (v sin φ − R ω_z)`. Normal speed: `v_OBn = v cos φ` (the elastic normal impulse is assumed).
- Throw angle: `θ_throw = atan(v_OBt / v_OBn)`. It is opposite to the side of the sliding velocity: left English throws the OB right.
- Reference speeds (Alciatore): slow 0.447, medium 1.341, fast 3.129 m/s (1, 3, 7 mph).

**DERIVED oracle values** (computed with the formulas above; test THR-01..05):

| Case | slow 0.447 | medium 1.341 | fast 3.129 |
|---|---|---|---|
| Stun, φ = 5° | 0.716° | 0.716° | 0.716° |
| Stun, φ = 10° | 1.443° | 1.443° | 1.443° |
| Stun, φ = 20° | 2.976° | 2.976° | 2.500° |
| Stun, φ = 30° | **4.715°** | **3.549°** | **1.698°** |
| Stun, φ = 45° | 4.945° | 2.773° | 1.127° |
| Stun, φ = 60° | 4.621° | 2.318° | 0.895° |
| Stun, maximum over φ | 5.300° at φ ≈ 33.0° | 3.874° at ≈ 25.4° | 2.693° at ≈ 18.3° |
| Natural roll, φ = 30° | 2.186° | 1.004° | 0.388° |
| Straight-on stun, sidespin R ω_z = 1.25·pE·v, pE = 25 % | 2.556° | 2.556° | 2.556° |
| … pE = 50 % | 5.102° | 3.053° | 1.307° |
| … pE = 100 % | 3.933° | 1.569° | 0.658° |
| "Gearing" outside English (R ω_z = v sin φ), any φ | 0 | 0 | 0 |

**Measured** (Alciatore, Billiards Digest Sept. 2006, "Throw – Part II") [DD-BD0906]:
- Setup: a frozen 2-ball pair on the foot spot, 13 cut angles × 3 speeds, 3 attempts each, measured on video at the head rail.
- The measured curves agree closely with the theory in shape and magnitude.
- About 1.5° at a 10° cut at any speed.
- The maximum throw is "almost 6°" (5.8°) for a slow stun shot near a 35° cut.
- Clean, polished balls throw less; chalk smudges ("cling") throw much more.
- Bob Jewett's independent data agree (sfbilliards throw plot, cited in TP A.14).
- Follow and draw reduce collision-induced throw by the same amount.

### 6.6 Squirt (Tier B + C)

**Model** (TP A.31):
`α = atan( (5/2)(b/R) sqrt(1 − (b/R)²) / (1 + m_r + (5/2)(1 − (b/R)²) ) )`.
- b is the lateral tip offset at contact.
- m_r = m_ball / m_endmass.
- The squirt direction is opposite to the side struck.
- It is nearly linear in b/R.

**Measured cues** (TP B.1, from Alciatore's Sept '07 BD data; R = 1.125 in):

| Cue | Measured squirt | at offset b | ⇒ m_r (DERIVED by TP B.1) | Model check (DERIVED) |
|---|---|---|---|---|
| Players (regular shaft) | 2.5° | 0.51 in (b/R = 0.4533) | 20.15 | 2.500° |
| Predator Z (low-squirt) | 1.8° | 0.51 in | 29.16 | 1.800° |
| Stinger (break/jump) | 2.4° | 0.30 in (b/R = 0.2667) | 12.01 | 2.400° |

- Model grid (DERIVED), squirt in degrees for b/R = 0.1 / 0.25 / 0.5:
  - m_r = 12: 0.921 / 2.259 / 4.162
  - m_r = 20: 0.607 / 1.485 / 2.709
  - m_r = 30: 0.426 / 1.040 / 1.886
  - m_r = 50: 0.267 / 0.650 / 1.173
- Shepard predicts m_r = 20–100; Alciatore's data show 12–29. pooltool uses m_r = 30.
- Cross (2008): a chalked tip grips the ball and gives a few degrees; an unchalked tip slides and squirts much more. Gameplay link: miscues and missing chalk.

### 6.7 Cue strike (Tier A/B): TP A.30

- **Elastic, horizontal cue**, m_r' = m_ball/m_cue:
  - `v_b = 2 v_s / (1 + m_r'(1 + (5/2)(x/R)²))`
  - `ω = 5 v_b x / (2R²)`, so the spin-rate factor is `Rω/v_b = (5/2)(x/R)`, which equals 1.25 at x = R/2.
- **Center hit with 6 oz ball and 18 oz cue** (m_r' = 1/3): v_b = 1.5 v_s.
- **With tip restitution e_tip, center hit:** `v_b = (1 + e_tip) v_s / (1 + m_r')`. Leather tips have e_tip 0.71–0.75, phenolic 0.81–0.87 [DD-CONST].
- **Maximum spin rate:** at `x/R = sqrt((2/5)(1 + m_r'))` = 0.730 for m_r' = 1/3. This is beyond the practical miscue limit of about 0.5–0.6 R.

### 6.8 Cushion rebound (Tier A/B/C/D)

- **Frictionless check** (TP 6.3): with normal restitution e and no friction, the tangential speed is kept and `v'_n = e v_n`. The rebound angle from the normal is therefore `θ_r = atan(tan θ_a / e) > θ_a`.
- **Measured, rolling perpendicular incidence, no side spin** (Mathavan 2009; snooker; Riley Renaissance-type table; 31 shots; rebound speed "immediately after" the impulse):
  - Quadratic fit `v_reb = −0.0877 v² + 1.131 v − 0.0953` for v ∈ [0.28, 3.5] m/s.
  - Ratios (DERIVED): 0.787 (0.3 m/s), 0.897 (0.5), 0.948 (1.0), 0.908 (2.0), 0.836 (3.0), 0.797 (3.5).
  - Linear-fit COR 0.818. The authors attribute the drop at high speed to cushion deformation.
  - Marlow's 0.55 is probably the speed after the post-rebound slip phase.
- **Mathavan 2010 model fit:** e_c = 0.98, μ_c = 0.14 (with h = 7R/5) reproduces the Fig. 7 data. For rolling balls the rebound angle vs. incidence angle curve is nearly speed-independent. Topspin changes it little; sidespin changes it strongly. With left spin at near-90° incidence the ball can come back to the side it came from.
- **WPA cushion speed test** [WPA-EQ §8]: ball on the head spot, shot through the foot spot, center ball, level cue, "firm" stroke. It must travel **at least 4 to 4½ table lengths** without jumping. "Firm" is not quantified, which makes this a Tier D calibration (CUSH-05).
- **Speed and aim reference lines for kicks** (Dr. Dave FAQ): slow kicks toward targets 2–4 diamonds from the rail track the mirror image across the **diamond line**; faster kicks and nearer targets use lines closer to the cushion. A realistic cushion model shows this speed dependence (SYS-06).

### 6.9 Diamond systems (Tier D) — exact coordinate definitions (INTERPRETATION)

Both systems below require a **rolling cue ball with running English**. They are table-dependent: Dr. Dave recommends calibrating speed and spin on a benchmark shot, then using the formula [DD-BD0810][DD-BD1110][DD-TP72]. We mirror that: **calibrate once, then predict**.

- **Common geometry** (9-ft, `equipment.md` §3.2): L = 2.54 m, W = 1.27 m, diamond spacing Δ = L/8 = W/4 = 0.3175 m, sight inset s = 0.0936625 m.
  - The long-rail diamond lines are y = ±(W/2 + s); the end-rail diamond lines are x = ±(L/2 + s).
  - "Aiming through a diamond" means the CB center's straight path passes through that point on the diamond line (in plan view).
  - "The corner diamond is taken to be centered in the pocket" (Dr. Dave). We use the intersection of the two diamond lines as the corner point (INTERPRETATION).
- **Corner-5 system** (three rails: `RAIL_LEFT` → `RAIL_FOOT` → `RAIL_RIGHT`):
  - **D** (origin number) is where the CB's aim line, extended backward, crosses the diamond line nearest the shooter.
    - D = 5 at the head-right corner point (−L/2 − s, −W/2 − s).
    - Along the `RAIL_RIGHT` diamond line toward the foot, D decreases by 1/2 per diamond: D = 5 − 0.5·(x + L/2)/Δ.
    - Along the `RAIL_HEAD` diamond line, D increases by 1 per diamond: D = 5 + (y + W/2)/Δ.
  - **F** (first-rail number) on the `RAIL_LEFT` diamond line: F = (L/2 − x)/Δ, so F = 1 is the first diamond from the foot end.
  - **T** (third-rail number) on the `RAIL_RIGHT` diamond line: T = (L/2 − x)/Δ.
  - **Formula: T = D − F.** For the traditional numbering, TP 7.2 gives the examples D 4.5 / F 2 → T 2.5 and D 6 (end rail) / F 3 → T 3.
  - **Benchmark 5-3-2:** the CB path through D = 5 and F = 3 reaches the third rail at T ≈ 2 "fairly accurately" on typical pool tables. Leaving the third rail, it arrives about **one diamond short of the head-left corner** on the fourth rail (`RAIL_LEFT`). On a carom table it would go into the corner [DD-BD1110].
- **Plus system** (two rails: `RAIL_FOOT` → `RAIL_LEFT`, returning toward `RAIL_RIGHT`):
  - **S** (short-rail number) on the `RAIL_FOOT` diamond line: S = 1 at the foot-left corner point, increasing by 1 per **half** diamond toward `RAIL_RIGHT`: S = 1 + 2·(W/2 − y)/Δ. The three sights are S = 3, 5, 7.
  - **P** is the CB line's position on the `RAIL_RIGHT` diamond line, in diamonds from the foot end.
  - **Prediction:** after two rails the CB's line crosses the `RAIL_RIGHT` diamond line at **P + S** diamonds from the foot end.
  - **Benchmark:** from P = 3, aiming through S = 5, the CB goes to P + S = 8, the head-right corner pocket [DD-BD0810].

### 6.10 Pockets (Tier D)

Alciatore's pocket analyses (TP 3.5–3.8, BD Dec 2004) [DD-BD1204]:
- The corner pocket's effective target size for **slow** shots is largest for shots nearly along the rail (≈ 43° to the pocket, **≈ 3.4 in / 86 mm**), because the ball can glance off the near rail and still drop.
- Side pockets are largest near straight-in and **shrink dramatically** as the angle grows.
- **Faster shots shrink every pocket.** The effective pocket center shifts by up to about 0.6 in (corner) and 0.3 in (side).
- Bob Jewett's BD May/June 2013 articles contain measured pocket-acceptance experiments. We need them for Tier C (OQ-7).

### 6.11 Break (Tier C/D)

- **Speeds:**
  - "powerful break" 25–30 mph = 11.2–13.4 m/s; "ridiculously powerful" 35 mph = 15.6 m/s [DD-SPEED]
  - pro men 22–26 mph (9.8–11.6 m/s), average ≈ 24 mph (10.7 m/s); average amateur ≈ 17 mph (7.6 m/s) [BLB-BREAK]
  - Measurement methods: radar, or sound timing between cue-strike and rack-contact peaks (Dr. Dave FAQ).
- **Physics expectations:**
  - Most CB energy goes into the pack.
  - With a square stun hit, the CB stops near the rack apex (the "cue ball in the center" goal).
  - The outcomes are chaotic with respect to rack gaps, so validation is **statistical**.

### 6.12 Airborne (Tier A/B)

- **Vertical drop/bounce:**
  - Apex after each impact: h_{k+1} = e_t² h_k.
  - A drop from 0.05 m with e_t = 0.5 gives impact 1 at t = 0.100964 s and apex 12.5 mm.
  - Impact 2 comes at t = 0.201928 s, with a next apex of 3.125 mm.
  - With pooltool's h_min = 5 mm the ball stops bouncing after impact 2.
  - With our h_min = 0.5 mm it bounces twice more (DERIVED):
    - impact 3 at t = 0.252410 s (0.201928 + 2·0.025241), next apex 0.781 mm
    - impact 4 at t = 0.277651 s, next apex 0.195 mm < 0.5 mm, so it settles
- **Massé/swerve:** the final direction obeys KIN-05. The airborne spec supplies the jump-shot tests.

---

## 7. Performance

### 7.1 Known numbers (prior art)

| System | Number | Context | Source |
|---|---|---|---|
| pooltool (2021, pre-Numba) | 0.73 s for a 54-event shot | the author's time-dilation demo; the discrete-time comparison was ~80× slower (8 s at dt ≤ 250 µs) | [PT-BLOG21] |
| pooltool (Algorithm 1010 solver) | 2.8 M quartics/s (Numba); 3.3 M/s (C); 0.34 µs per polynomial (C, direct) | solver micro-benchmark | [PT-PR235][PT-PR236] |
| pooltool full-shot rate | **not published**; `sandbox/break.py --time-it` exists | — | [PT-GH] |
| tailuge/billiards | ~500 rollouts/s on 4 cores (TypeScript, time-stepping) | batch fitting | [TAIL-GH] |
| PickPocket (2007) | "hundreds of nodes per second"; 60 s per shot in experiments | PoolFiz | [SMITH-2007] |
| CueCard (2008) | 20 CPUs; 10 min per game; 25–100 noisy samples per candidate; simulator ≈ 5× faster than the stock PoolFiz | ICGA Olympiad | [ARCH-2010] |
| pooltool blog | discrete time needs ~30 µs steps (300,000+ iterations for a 10 s shot at 10 m/s); event-based needs 5–50 events | order-of-magnitude argument | [PT-BLOG20] |

### 7.2 Cost model (DERIVED)

- **Events per shot (estimate):**
  - A typical position shot with 2–4 moving balls: 15–60 events. Each ball contributes 1–3 transitions, plus 1–4 collisions and 0–4 cushion hits.
  - A 9-ball or 8-ball break: 200–800 events, from 10–16 moving balls with ~3 transitions each, plus many ball–ball and cushion hits in the first second.
- **Work per event:** re-predict events for 1–2 balls.
  - With conservative culling: ~5–20 root isolations per event for typical shots, ~20–60 during a break.
  - At 0.1–0.4 µs each, plus ~0.5 µs of queue and bookkeeping: **≈ 2–10 µs per event**.
- **Result:**
  - Typical shot ≈ 30–600 µs; target the lower half.
  - Break ≈ 0.4–8 ms.

### 7.3 What the AI needs (DERIVED)

- **Budget:** an AI decision should feel immediate. That means about 1.5 s of wall time while the "opponent walks around the table" animation plays, on 6 worker threads, leaving 2 cores for game and render.
- **Search shape** (CueCard-like):
  - 100–500 candidate shots (aim directions from ghost-ball geometry × 3–5 speed/spin variants).
  - Plain simulations first to filter, then n = 30–50 noisy samples for survivors (player-skill noise; CueCard's sweet spot).
  - A second ply on the top-k = 5–10.
  - Total **≈ 15,000–75,000 simulations per decision**, i.e., 10–50 k sims/s aggregate, i.e., **1.7–8.3 k sims/s per thread**.
- **Conclusion:** a median typical-shot simulation must cost **≤ 100–120 µs**. Our target of 100 µs gives ≥ 1× margin at the top of the range and 5× at the bottom. Break-shot AI (choosing break speed and position) is offline or precomputed.

### 7.4 Targets (binding for BilliardsCore)

| ID | Target | Measurement |
|---|---|---|
| P1 | Typical non-break shot: **median ≤ 100 µs, p99 ≤ 1 ms**, 1 core | Release build, benchmark set B1 (§7.5), reference CPU (OQ-6) |
| P2 | Break (15 balls, 10.7 m/s): **median ≤ 2 ms, p99 ≤ 10 ms** | Set B2 |
| P3 | Throughput **≥ 10,000 typical shots/s per core**; parallel efficiency ≥ 0.8 up to 8 threads | Set B1, thread pool; no shared mutable state |
| P4 | **Zero heap allocations** inside the event loop after warm-up | allocation hook counter in the test build |
| P5 | Memory per in-flight simulation ≤ 64 KB (excluding trajectory recording) | static sizing: 16 balls × states + queue |
| P6 | Playback: analytic state evaluation at an arbitrary t ≤ 0.2 µs per ball | render path |

### 7.5 Benchmark protocol

- **B1:** 10,000 random legal shots from mid-game 9-ball layouts (seeded; 2–9 object balls). Parameters: V ∈ [0.5, 6] m/s, tip offsets within ±0.5R, cue elevation 0–15°.
- **B2:** 1,000 9-ball and 1,000 8-ball breaks with seeded rack gaps (§5.6), V ∈ [8, 13] m/s.
- **B3 (stress):** frozen clusters, Newton's cradle, balls resting on cushions, massé into the rail.
- **Reporting:** median, p90, p99, max, events per shot, and root isolations per event. Keep a history in CI to detect performance regressions > 10 %.

---

## 8. Implementation notes & pitfalls

1. **Validity windows everywhere.** Every predicted event must lie inside the polynomial validity window of every ball involved. Ghost collisions from extrapolated parabolas are the most common bug in hand-written event simulators (ROB-13).
2. **Gate by approach, not by distance alone.** A pair touching (|gap| ≤ δ) and separating must never generate an event. A pair touching and closing must generate one at t = 0, including a tiny rounding penetration.
3. **Re-detect after every resolution** for all balls touched by the event. Never assume a second pre-computed event (e.g., the other cushion of a corner) is still valid.
4. **Construct transition states exactly** (§5.8). Never let the round-off of the evolved state decide the motion state.
5. **The corner is a unit test, not an edge case.** Test 45° shots into every corner and every jaw joint, at many speeds and with sub-micron offsets (ROB-03/14).
6. **Do not cull with velocity direction.** Sliding balls curve (massé, draw reversing the direction).
7. **Zeno guards are physics decisions.** Document them in the shot record, e.g., "cushion rest contact engaged at t = …", so AI or replay discrepancies are explainable.
8. **The rack is a cluster.** The first break impact goes to the cluster solver, and the gaps come from the rack generator. Never use exactly identical positions for all racks: real racks vary, and break outcomes must vary with them.
9. **One source of parameters.** Test oracles must read the same `PhysicsParams` / `NumericsConfig` as the engine. Tier B/C tests pin their own parameter sets (e.g., the snooker set for the Mathavan tests) explicitly.
10. **Unit conventions in sources differ.**
    - Alciatore uses inches and mph (convert exactly: 1 in = 0.0254 m, 1 mph = 0.44704 m/s).
    - His throw friction law expects the **relative sliding speed** (m/s), not the ball speed.
    - pooltool's `u_sp` is a length, and its cue offsets (a, b) are normalized by R.
    - Mathavan uses snooker balls.
11. **Sign conventions:**
    - Sidespin sign vs. squirt and throw direction: left English squirts the CB right and throws the OB right.
    - Contact slip u = v + ω × (−R ẑ).
    - Encode each convention once in a helper and test it (THR-06, SQ-04).
12. **Licenses:** Apache-2.0 attribution for anything derived from pooltool code (keep the header plus a `THIRD_PARTY_NOTICES.md` entry). No GPL code. No Algorithm 1010 code.
13. **Pocket physics** (rattles, lips, drop edge) may be hard to express with low-degree polynomial events. Recommended fallback: inside a small capture volume around each pocket, switch the ball to a deterministic fixed-step rigid-body micro-simulation (e.g., 50 µs steps against exact jaw, facing, shelf and drop-edge geometry) until it is captured or ejected, then return to analytic motion. This keeps the rest of the engine event-based. The step size must be validated for convergence (results change < 0.1 mm when dt is halved).
14. **Performance hygiene:**
    - structure-of-arrays for ball states
    - fixed-capacity containers (16 balls, ≤ 64 cushion primitives per table)
    - a preallocated event heap
    - no virtual calls in the inner loop
    - Profile with B1/B2 before optimizing.
15. **Keep pooltool as a living oracle.** A small Python harness (outside the shipped product) runs pooltool on the same initial conditions with matched models and parameters. It diffs final positions (tolerance 1 mm), event sequences (types and order) and event times (1e-6 s) for XREF tests. This needs a local Python setup (OQ-2).

---

## 9. Test cases

Unless stated otherwise:
- Parameters: pool ball (R = 0.028575 m, m = 0.17009713875 kg), g = 9.81, mu_s = 0.2, mu_r = 0.01, alpha_sp = 10 rad/s², e_b = 0.95, 9-ft table.
- Angles are compared in degrees.
- "stun" means ω = 0 at the event of interest.
- Tier and tolerance are given per row.

### 9.1 Cloth kinematics (KIN)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| KIN-01 | v0 = (2, 0, 0) m/s, ω0 = 0 (stun) | slide ends at τ = 0.291248 s after d = 0.499282 m; v = 1.428571 m/s; then rolls to rest after a further 14.5624 s and 10.4017 m | rel 1e-9 | A |
| KIN-02 | v0 = 2 m/s along +x, R ω_y = −2 m/s (draw, ω_y = −69.991 rad/s) | τ = 0.582496 s; d = 0.832137 m; final rolling v = +0.857143 m/s (forward) | rel 1e-9 | A |
| KIN-03 | v0 = 2, R ω_y = +1 (half-roll follow) | τ = 0.145624 s; d = 0.270445 m; v_f = 1.714286 m/s | rel 1e-9 | A |
| KIN-04 | v0 = 2, R ω_y = +4 (overspin) | τ = 0.291248 s; d = 0.665710 m; v_f = 2.571429 m/s | rel 1e-9 | A |
| KIN-05 | v0 = (1.0, 0.5, 0), ω0 = (10, −20, 5) rad/s; repeat with mu_s ∈ {0.15, 0.2, 0.4} | |u0| = 1.756990 m/s; τ(mu_s = 0.2) = 0.255860 s; final rolling v_f = (0.551000, 0.275500, 0) m/s for **all** mu_s (Coriolis invariant) | rel 1e-9 | A |
| KIN-06 | Path shape of KIN-05 | positions during sliding lie on the parabola r0 + v0 t − ½ mu_s g û0 t² (check 100 samples) | 1e-12 m | A |
| KIN-07 | Spinning in place, ω_z0 = 20 rad/s | stops at t = 2.000 s after 20.0 rad of rotation | rel 1e-9 | A |
| KIN-08 | Rolling with ω_z0 = 10 rad/s and v0 = 1 m/s | ω_z decays at 10 rad/s² independently of rolling; transition rolling→stationary at 10.19368 s (ω_z already 0 at 1.0 s) | rel 1e-9 | A |

### 9.2 Cloth reality (CLOTH)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| CLOTH-01 | Snooker parameter set; rolling deceleration | μ_r g ∈ [0.124, 0.126] m/s² when mu_r is set to the measured 0.0127 (sanity of the unit conventions) | exact | C |
| CLOTH-02 | Default pool preset | mu_s ∈ [0.15, 0.40]; mu_r ∈ [0.005, 0.015]; alpha_sp ∈ [5, 15] rad/s²; sliding decel / rolling decel ∈ [10, 40] | inside ranges | C |
| CLOTH-03 | Lag: a ball starts rolling at the foot rail toward the head rail with v = sqrt(2 mu_r g L) | stops at the head rail after t = sqrt(2L/(mu_r g)) = 7.196 s (mu_r = 0.01, L = 2.54 m); the "tournament cloth" preset must give 7–9 s | 1e-6 s (A); 7–9 s (C) | A/C |

### 9.3 Ball–ball (BB)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| BB-01 | e_b = 1, mu_b = 0, stun CB 1.5 m/s, φ ∈ {10, 30, 45, 60, 80}° | angle between post-impact velocities = 90°; |v_CB| = v sin φ; |v_OB| = v cos φ | 1e-7° / rel 1e-9 | A |
| BB-02 | TP A.5 mode: e_b = 0.94, constant mu_b = 0.06, stun, φ = 30° | OB throw 3.4336°; CB off tangent 3.3073°; separation 83.2591°. With mu_b = 0: 87.0255°. With e_b = 1: 86.5664°. | 0.001° | B |
| BB-03 | e_b = 1, mu_b = 0, natural-roll CB (1 m/s), φ = 30°, 48.590°, 14.478° | final CB deflection after sliding: 33.6705°, 27.2669°, 27.6265°; speed at φ = 30°: 0.557875 m/s | 1e-6° | A |
| BB-04 | Same as BB-03, scan φ | maximum 33.7490° at φ = 28.1255° | 1e-4° / 0.01° | A |
| BB-05 | e_b = 0.94, mu_b = 0, rolling, φ = 30° | 31.9876°; maximum 32.0091° at φ ≈ 28.995° | 1e-4° / 0.01° | A |
| BB-06 | Head-on, rolling CB 1 m/s, e_b = 1 / 0.95 | CB final rolling speed 0.285714 / 0.303571 m/s forward | rel 1e-9 | A |
| BB-07 | Head-on stun 1 m/s, e_b = 0.95 | immediately after: CB 0.025 m/s, OB 0.975 m/s; CB final roll 0.017857 m/s | rel 1e-9 | A |
| BB-08 | Any ball–ball impulse (random 10⁵ cases, all models) | linear momentum conserved (rel 1e-12); total kinetic energy non-increasing (≤ +1e-12 J); angular momentum about the contact point conserved for frictional models | as stated | A |
| BB-09 | Mathavan 2009/2014 shots 1–5 (§6.4) with the snooker set (M = 0.1406, R = 0.02625, mu_s = 0.21, e_b = 0.89, mu_b = 0.05 or the Alciatore law), CB rolling, zero side | post-slip CB speed, OB speed and CB exit angle vs. measured: each speed within ±12 %; RMS relative speed error ≤ 7 %; each CB angle within ±4°; OB speed error ≤ the ideal model's error for ≥ 4 of 5 shots | as stated | C |

### 9.4 Throw (THR)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| THR-01 | Alciatore-law model, stun, φ = 30°, v ∈ {0.447, 1.341, 3.129} m/s | 4.715°, 3.549°, 1.698° | 0.02° | B |
| THR-02 | Stun, φ = 10°, all three speeds | 1.443° (speed-independent) | 0.02° | B |
| THR-03 | Natural roll, φ = 30°, three speeds | 2.186°, 1.004°, 0.388° | 0.02° | B |
| THR-04 | Straight-on stun, R ω_z = 1.25·pE·v, pE ∈ {25, 50, 100} % | 2.556° (all speeds); 5.102/3.053/1.307°; 3.933/1.569/0.658° | 0.02° | B |
| THR-05 | "Gearing" outside English (R ω_z = v sin φ), φ ∈ {15, 30, 45}° | throw = 0 | 1e-6° | A |
| THR-06 | Sign convention | left English throws the OB to the right of the line of centers. Left English means the tip contacts the CB on its +y side for a CB moving +x (+y is left from the shooter's view). DERIVED: angular impulse r × F = (0, b, 0) × (F, 0, 0) = (0, 0, −bF), so ω_z < 0. | sign | A |
| THR-07 | Reality: stun, 10° cut, 0.45–3.1 m/s | 1.0–2.0° | range | C |
| THR-08 | Reality: slow stun (0.45 m/s), φ = 30–35° | 4.5–6.0° | range | C |
| THR-09 | Reality: fast stun (3.1 m/s), φ = 30° vs slow | fast throw < 50 % of slow throw | trend | C |
| THR-10 | Follow vs. draw of equal magnitude, medium, φ = 30° | equal throw reduction relative to stun | 0.05° | B |

### 9.5 Squirt and cue (SQ, CUE)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| SQ-01 | Squirt model: (b/R, m_r) = (0.453333, 20.151), (0.453333, 29.158), (0.266667, 12.008) | 2.500°, 1.800°, 2.400° | 0.001° | B |
| SQ-02 | Grid m_r ∈ {12, 20, 30, 50} × b/R ∈ {0.1, 0.25, 0.5} | the §6.6 table | 0.001° | B |
| SQ-03 | Reality: default playing cue at b = 0.5R | 1.5°–3.0° (range of the measured regular and low-squirt cues) | range | C |
| SQ-04 | Sign | a right-English strike (contact point right of center) sends the CB left of the cue line; squirt = 0 at b = 0; odd in b | sign / 1e-12 | A |
| CUE-01 | Elastic, center, level cue, m_ball/m_cue = 1/3 | v_b = 1.5 v_s; ω = 0 | rel 1e-9 | A |
| CUE-02 | Elastic, x = 0.5R (side or top) | v_b = 1.297297 v_s; Rω/v_b = 1.25 | rel 1e-9 | A |
| CUE-03 | Center, e_tip = 0.75 | v_b = 1.3125 v_s | rel 1e-9 | A |
| CUE-04 | Spin rate vs. offset | maximum at x/R = sqrt(0.4·(1 + 1/3)) = 0.730297 (if the miscue limit is disabled) | 1e-6 | A |

### 9.6 Cushion (CUSH)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| CUSH-01 | Frictionless model (e = 0.8, μ = 0), stun ball, incidence 30° / 60° from the normal | rebound 35.8175° / 65.2087° from the normal (atan(tan θ/e)); tangential speed kept | 1e-4° | A |
| CUSH-02 | Snooker set, rolling ball, perpendicular, no side, v ∈ {0.3, 0.5, 1, 1.5, 2, 2.5, 3, 3.5} m/s | rebound speed immediately after impact vs. y = −0.0877v² + 1.131v − 0.0953 | ±0.08 m/s each; RMS ≤ 0.05 m/s | C |
| CUSH-03 | If the Mathavan 2010 model is used, with e_c = 0.98, μ_c = 0.14, h = 7R/5 | reproduces CUSH-02 with no further tuning | same | B/C |
| CUSH-04 | Energy | kinetic energy after the cushion ≤ before, for all 10⁵ random incidences, spins and speeds | +1e-12 J | A |
| CUSH-05 | WPA speed test: CB on the head spot, shot through the foot spot, center, level cue | find v0 (bisection) such that the CB travels exactly 4.0 and 4.5 table lengths; record it; **flag** if v0 for 4.0 lengths is outside [2.5, 5.0] m/s (ESTIMATE of a "firm" stroke) or the ball leaves the slate | band | D |
| CUSH-06 | Rolling ball at 45° incidence, running vs. reverse English (0.3R) vs. none | rebound angle ordering: running > none > reverse (angles from the normal) | sign | C |
| CUSH-07 | Sidespin extreme (Mathavan 2010 Fig. 10): left spin, 85° incidence from the rail line (near-perpendicular) | the ball may come back to its approach side; must not stick or penetrate | qualitative | B |

### 9.7 Diamond systems and kicks (SYS), 9-ft table, Tier D

| ID | Setup | Expected | Tol |
|---|---|---|---|
| SYS-01 | **Calibrate Corner-5**: CB center on the line through D = 5 (−1.3637, −0.7287) and F = 3 (0.3175, +0.7287), placed at x = −1.00 m; rolling on arrival at the first rail; running English with side offset b_s ∈ [0, 0.5R]; speed so that it rolls ~5 table lengths (search) | there exist (speed, b_s) in the "medium" band (CB speed 1.2–2.5 m/s at the first rail) such that the third-rail crossing of the `RAIL_RIGHT` diamond line is at T = 2 (x = +0.635 m) | ±0.25 diamond |
| SYS-02 | With the SYS-01 calibration: D = 4.5, F = 2 | T = 2.5 | ±0.5 diamond |
| SYS-03 | With the SYS-01 calibration: D = 6 (on the `RAIL_HEAD` diamond line), F = 3 | T = 3 | ±0.5 diamond |
| SYS-04 | 5-3-2 fourth rail | after the third rail, the CB hits `RAIL_LEFT` 0.3–1.5 diamonds short of the head-left corner (not in the pocket) | range |
| SYS-05 | **Plus system**: calibrate at P = 3, S = 5 (target: head-right corner, P + S = 8); then predict P = 3, S = 3 → 6; P = 2, S = 5 → 7; P = 4, S = 3 → 7 | crossing of the `RAIL_RIGHT` diamond line after 2 rails | calibration ±0.25; predictions ±0.5 diamond |
| SYS-06 | One-rail kick, slow rolling CB (arriving at the rail at ≈ 0.5 m/s), target 3 diamonds off `RAIL_LEFT`; aim at the mirror image across the diamond line (s = 93.7 mm behind the nose) | the CB passes within ±1 ball diameter of the target; faster speeds (2 m/s) miss short or long **consistently** (report the side; trend only) | ±2R |

### 9.8 Pockets (POCK), Tier D (Tier C once OQ-7 data exists)

| ID | Setup | Expected | Tol |
|---|---|---|---|
| POCK-01 | Effective target size (lateral range of OB paths that drop), 9FT_PRO corner, slow rolling OB (0.3 m/s at the mouth), approach angles 0–45° | maximum near 40–45° ("along the rail"); size at 43° in [2.5, 4.0] in (Dr. Dave's pocket: 3.4 in) | range |
| POCK-02 | Same, fast (2.5 m/s) | effective size ≤ slow size at every angle; at 43° at most 70 % of slow | trend |
| POCK-03 | Side pocket, slow, angles 0–60° | monotone decrease beyond ~20°; near-zero acceptance at large angles | trend |
| POCK-04 | Rattle | a fast OB hitting the near jaw first at 30° rattles out in ≥ 1 configuration that a slow ball makes | existence |

### 9.9 Break (BRK)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| BRK-01 | 9-ball rack, B2 gap generator, CB 10.7 m/s square stun hit on the 1-ball | terminates; no NaN; ≤ 5,000 events; no overlaps (ROB-11); kinetic energy non-increasing after the stick event | as stated | A |
| BRK-02 | 1000 seeded breaks at 10.7 m/s | the CB ends within 0.3 m of the table center in ≥ 50 % of runs (a square stun hit is the pro goal) | band | D |
| BRK-03 | Same set | the distribution of the number of balls pocketed and of balls crossing the head string is stable (two-sample KS test, p > 0.01) across builds and platforms | statistical | D |
| BRK-04 | Zero-gap rack vs. 50 µm random gaps | both terminate; outcomes differ (chaos); the zero-gap result is independent of ball-id ordering when the cluster solver is enabled | as stated | A/D |
| BRK-05 | Speed sweep 7.6 → 13.4 m/s | mean kinetic energy delivered to the object balls increases monotonically | trend | D |

### 9.10 Airborne (AIR)

| ID | Setup | Expected | Tol | Tier |
|---|---|---|---|---|
| AIR-01 | Vertical drop, z_center = R + 0.05 m, e_t = 0.5, h_min = 5 mm | impacts at 0.100964 s and 0.201928 s; after the second impact v_z = 0 (only 2 table events) | 1e-6 s | A |
| AIR-02 | Same with h_min = 0.5 mm | impacts 3 and 4 at 0.252410 s and 0.277651 s, then settles (4 table events) | 1e-6 s | A |
| AIR-03 | Bounce-apex ratio | h_{k+1}/h_k = e_t² | rel 1e-9 | A |

### 9.11 Robustness (ROB) and roots (ROOT)

| ID | Setup | Expected | Tier |
|---|---|---|---|
| ROB-01 | Newton's cradle: 5 balls frozen along x (gap 0), CB 2 m/s along the line | terminates < 1,000 events; momentum conserved (rel 1e-9); no overlap; the last ball carries the largest speed | A |
| ROB-02 | 15-ball rack with **exactly zero gaps**, break at 13 m/s | terminates < 10,000 events and within the P2 time budget ×5 | A |
| ROB-03 | Pocketless square table (pooltool #217): ball at (1, 1) from the corner of a 4.5 × 4.5 m table, 45° into the corner at 2 m/s; plus 1000 random sub-µm offsets | the ball never leaves the table; both cushions are hit, in either order | A |
| ROB-04 | Ball placed exactly touching a cushion (distance = contact distance), shot into it at 1 m/s; also at contact + 1e-12 m and − 1e-12 m | an immediate cushion event; the ball rebounds; no tunneling | A |
| ROB-05 | Rolling ball into a rail head-on at 0.05 m/s with natural topspin | a finite number of cushion events (≤ 50); ends at rest touching the rail; `restingOnCushion` engaged | A |
| ROB-06 | Construct parameters with mu_s = 0, mu_r = 0, alpha_sp = 0, e = 1.2 | the API returns an error (no hang, no exception) | A |
| ROB-07 | Degenerate quartic: A rolling +x at 1.0 m/s from x = −0.5; B rolling +x at 0.5 m/s from x = 0 (same y, same mu_r) | collision at t = (0.5 − 2R)/0.5 = 0.885700 s | A (1e-12 s) |
| ROB-08 | Graze: CB path whose closest approach to the OB center is 2R + 1e-7 m / 2R − 1e-7 m | no collision / collision; bitwise-identical decisions across 1000 runs | A |
| ROB-09 | Symmetric double hit: CB straight at the contact point of two frozen OBs side by side (y-symmetric) | deterministic; with the cluster solver the outcome is y-symmetric within 1e-9 m/s; with pairwise mode the asymmetry is documented and reproducible | A |
| ROB-10 | Determinism: 1000 B1 shots + 100 B2 breaks, standalone vs. UE-module build vs. Debug | identical SHA-256 of the serialized event logs | A |
| ROB-11 | Invariant monitor on all benchmark sets | at every event: all ball–ball gaps ≥ −1e-9 m, all cushion gaps ≥ −1e-9 m; de-penetrations logged = 0 | A |
| ROB-12 | Energy monitor | total kinetic energy (translational + rotational) non-increasing between events (+1e-12 J), except at the stick event | A |
| ROB-13 | Validity window: A at (0, 0) rolling +x at 0.5 m/s; stationary B at (−0.2, 0) behind it | **no** collision (the extrapolated parabola would come back); A stops at x = 1.27421 m | A |
| ROB-14 | Every cushion joint and jaw arc of each table preset: 10,000 balls shot at the joint with random sub-mm offsets and angles | no escapes; no events with t < t_now; no NaN | A |
| ROB-15 | Event cap | an artificial Zeno setup with the guards disabled hits `kMaxEvents` and returns `Truncated` cleanly | A |
| ROOT-01 | 10⁶ random ball–ball configurations with random states | the first-entry time agrees with a 256-bit MPFR brute-force reference within 1e-12 s (or relative 1e-12), and root existence decisions agree except within δ of a graze | A |
| ROOT-02 | Polynomials with clustered or double roots and huge coefficient ranges (1e-8 … 1e8) | no NaN/Inf; the monotone-bracket invariant holds | A |

### 9.12 Cross-reference with pooltool (XREF; nightly, needs OQ-2)

| ID | Setup | Expected |
|---|---|---|
| XREF-01 | 1000 B1 shots; pooltool resolver: frictionless-elastic ball–ball, `unrealistic` cushions, canonical everything else; RawBreak configured identically (circle pockets) | same event-type sequence in ≥ 99 % of shots; final positions within 1 mm; event times within 1e-6 s |
| XREF-02 | Same with pooltool's default models (Alciatore ball–ball, Stronge cushions `omega_ratio` 1.8) and our ports | final positions within 5 mm in ≥ 95 % of shots (differences come from spacer/cluster handling; examine the outliers) |

### 9.13 Performance (PERF)

| ID | Check | Pass |
|---|---|---|
| PERF-01 | B1 median / p99 per shot, 1 thread, Release | ≤ 100 µs / ≤ 1 ms |
| PERF-02 | B2 median / p99 | ≤ 2 ms / ≤ 10 ms |
| PERF-03 | B1 throughput 1 / 4 / 8 threads | ≥ 10 k/s per core; efficiency ≥ 0.8 |
| PERF-04 | Allocation counter during B1 (after warm-up) | 0 |
| PERF-05 | AI budget: 50,000 B1-like simulations on 6 threads | ≤ 1.0 s wall |

---

## 10. Open questions

- **OQ-1 (legal):** confirm that no Algorithm 1010 code (ACM license, and the unlicensed `quarticpp` repo) enters the product. Our own solver (§5.5) is planned. Also confirm that porting from Apache-2.0 pooltool with attribution is acceptable to the publisher, and list it in third-party notices.
- **OQ-2 (tooling):** may we install Python + pooltool (from PyPI) locally for the XREF harness and a pooltool performance baseline? This needs the user's permission for the download.
- **OQ-3 (determinism):** check the actual UE 5.8 MSVC and Clang floating-point flags applied to game modules. If fast-math-like flags apply, decide between a module-level override and a prebuilt static library.
- **OQ-4 (Zeno thresholds):** v_rest = 1e-3 m/s, h_min = 0.5 mm and the cluster tolerance 1e-7 m are reasoned defaults. Tune them visually with the realism team using slow-motion first-person captures.
- **OQ-5 (multi-contact law):** which restitution law should the cluster solver use (Newton vs. Poisson, per-contact vs. global)? Validate against a high-speed video of a real rack break (Dr. Dave's HSV library, or our own recording).
- **OQ-6 (reference CPU):** define the benchmark machine, e.g., Steam Hardware Survey median, 6-core, ~4.0 GHz.
- **OQ-7 (pocket data):** obtain Bob Jewett's BD May/June 2013 pocket experiments and Dr. Dave's TP 3.5–3.8 parameter sets to move POCK tests from Tier D to Tier C.
- **OQ-8 (own measurements):** a cheap in-house validation rig would add Tier C data on *our* reference table: a phone camera at 240 fps over a real table, with lag times, stun distances, cushion rebounds and throw with Dr. Dave's template. Recommended before tuning the final presets.
- **OQ-9 (Han 2005):** the published paper's equations contain errors (see the pooltool and tailuge notes). Decide whether Han is offered at all or only Stronge/Mathavan.
- **OQ-10 ("Pool Shark"):** the 2026 Steam title reviewed here is Motivation Stack's roguelike. Confirm that this is the game the brief meant.

---

## 11. References

- **[PT-GH]** pooltool repository and README — https://github.com/ekiefl/pooltool (Apache-2.0; `LICENSE.txt`)
- **[PT-JOSS]** Kiefl, E. (2024) JOSS 9(101):7301 — https://joss.theoj.org/papers/10.21105/joss.07301 (PDF: https://www.theoj.org/joss-papers/joss.07301/10.21105.joss.07301.pdf)
- **[PT-DOCS]** pooltool documentation — https://pooltool.readthedocs.io/en/latest/ ; custom physics — https://pooltool.readthedocs.io/en/latest/resources/custom_physics.html
- **[PT-EVO]** `simulate()` API — https://pooltool.readthedocs.io/en/latest/autoapi/pooltool/evolution/index.html
- **[PT-CL]** CHANGELOG — https://github.com/ekiefl/pooltool/blob/main/CHANGELOG.md
- **[PT-PR235]** Replace the quartic solver with Algorithm 1010 — https://github.com/ekiefl/pooltool/pull/235 ; **[PT-PR236]** Numba optimizations — https://github.com/ekiefl/pooltool/pull/236
- pooltool PR #257 (ball–ball edge cases, priorities, continually touching) — https://github.com/ekiefl/pooltool/pull/257 ; PR #354 (degenerate roots) — https://github.com/ekiefl/pooltool/pull/354 ; PR #80 (hybrid solver) — https://github.com/ekiefl/pooltool/pull/80
- pooltool issues: #154 https://github.com/ekiefl/pooltool/issues/154 ; #217 https://github.com/ekiefl/pooltool/issues/217 ; #239 https://github.com/ekiefl/pooltool/issues/239 ; #264 https://github.com/ekiefl/pooltool/issues/264 ; #347 https://github.com/ekiefl/pooltool/issues/347
- **[PT-BLOG20]** Kiefl, "The algorithmic theory behind pool/billiards simulation" — https://ekiefl.github.io/2020/12/20/pooltool-alg/ ; "The physics of pool/billiards" — https://ekiefl.github.io/2020/04/24/pooltool-theory/
- **[PT-BLOG21]** Kiefl, "Creating a billiards simulator: the transition from equations to code" — https://ekiefl.github.io/2021/03/25/pooltool-start/
- **[LG-2006]** Leckie, W., Greenspan, M. "An Event-Based Pool Physics Simulator", Advances in Computer Games, LNCS 4250, 247–262 (2006) — https://link.springer.com/chapter/10.1007/11922155_19 ; "Pool Physics Simulation by Event Prediction 1: Motion Transitions", ICGA J. 28(4) (2005) — https://journals.sagepub.com/doi/abs/10.3233/ICG-2005-28403
- **[HAN-2005]** Han, I. "Dynamics in carom and three cushion billiards", J. Mech. Sci. Technol. 19(4):976–984 (2005) — https://drdavepoolinfo.com/physics_articles/Han_paper.pdf
- **[MAT-2009]** Mathavan, S., Jackson, M. R., Parkin, R. M. Am. J. Phys. 77(9):788–794 (2009), doi:10.1119/1.3157159 — https://drdavepoolinfo.com/physics_articles/ajp_09_hsv_article.pdf
- **[MAT-2010]** Mathavan et al., Proc. IMechE C 224(9):1863–1873 (2010) — https://drdavepoolinfo.com/physics_articles/Mathavan_IMechE_2010.pdf
- **[MAT-2014]** Mathavan et al., Sports Eng. 17:227–237 (2014) — https://drdavepoolinfo.com/physics_articles/Mathavan_Sports_2014.pdf
- **[DD-TP]** Alciatore technical proofs index — https://drdavepoolinfo.com/technical-proof/ ; TP 3.1 https://drdavepoolinfo.com/technical_proofs/TP_3-1.pdf ; TP 3.3 https://drdavepoolinfo.com/technical_proofs/TP_3-3.pdf ; TP 4.1 https://drdavepoolinfo.com/technical_proofs/TP_4-1.pdf ; TP 6.3 https://drdavepoolinfo.com/technical_proofs/TP_6-3.pdf ; **[DD-TP72]** TP 7.2 https://drdavepoolinfo.com/technical_proofs/TP_7-2.pdf ; TP A.4 https://drdavepoolinfo.com/technical_proofs/new/TP_A-4.pdf ; TP A.5 https://drdavepoolinfo.com/technical_proofs/new/TP_A-5.pdf ; TP A.6 https://drdavepoolinfo.com/technical_proofs/new/TP_A-6.pdf ; TP A.14 https://drdavepoolinfo.com/technical_proofs/new/TP_A-14.pdf ; TP A.28 https://drdavepoolinfo.com/technical_proofs/new/TP_A-28.pdf ; TP A.30 https://drdavepoolinfo.com/technical_proofs/new/TP_A-30.pdf ; TP A.31 https://drdavepoolinfo.com/technical_proofs/new/TP_A-31.pdf ; TP B.1 https://drdavepoolinfo.com/technical_proofs/new/TP_B-1.pdf ; TP A.19 https://drdavepoolinfo.com/technical_proofs/new/TP_A-19.pdf
- **[DD-CONST]** Alciatore, "Pool Physics Property Constants" — https://drdavepoolinfo.com/faq/physics/physical-properties/
- **[DD-CLOTH]** Table cloth speed (Jewett lag rule) — https://drdavepoolinfo.com/faq/table/cloth-speed/
- **[DD-SPEED]** Typical pool ball speeds — https://drdavepoolinfo.com/faq/speed/typical/ ; break speed FAQ — https://drdavepoolinfo.com/faq/break/speed/
- **[DD-BD0906]** Alciatore, "Throw – Part II: results", Billiards Digest, Sept. 2006 — https://drdavepoolinfo.com/bd_articles/2006/sept06.pdf (series: aug06, oct06, nov06, dec06)
- **[DD-BD0810]** Alciatore, Plus System, BD Aug. 2010 — https://drdavepoolinfo.com/bd_articles/2010/aug10.pdf ; Plus System page — https://drdavepoolinfo.com/faq/bank-kick/plus/
- **[DD-BD1110]** Alciatore, "VEPS GEMS – Part XI: Corner-5 System Intro", BD Nov. 2010 — https://drdavepoolinfo.com/bd_articles/2010/nov10.pdf ; Corner-5 page — https://drdavepoolinfo.com/faq/bank-kick/corner-5/
- **[DD-BD1204]** Alciatore, "Just How Big are the Pockets, Anyway? – Part II", BD Dec. 2004 — https://drdavepoolinfo.com/bd_articles/2004/dec04.pdf ; pocket effective size FAQ — https://drdavepoolinfo.com/faq/pocket/size-and-center/
- **[DD-SIM]** Dr. Dave, pool physics simulator resources — https://drdavepoolinfo.com/faq/physics/simulator/ ; kick speed reference lines — https://drdavepoolinfo.com/faq/bank-kick/speed-effects/
- **[DLTJ-1994]** de la Torre Juárez, M. Eur. J. Phys. 15(4):184–190 (1994)
- **[WS-1988]** Wallace, R. E., Schroeder, M. C. Am. J. Phys. 56(9):815–819 (1988)
- **[CROSS-2008]** Cross, R. Am. J. Phys. 76(3):205–212 (2008) — https://www.researchgate.net/publication/228724338_Cue_and_ball_deflection_or_squirt_in_billiards
- **[SHEP-2001]** Shepard, R. (2001) — https://drdavepoolinfo.com/physics_articles/Shepard_squirt.pdf
- **[MARLOW]** Marlow, W. C. *The Physics of Pocket Billiards*, MAST (1994/95)
- **[KIM-2024]** Kim, H.-C. arXiv:2402.13258 — https://arxiv.org/abs/2402.13258
- **[OQS-2020]** Orellana, A. G., De Michele, C. ACM TOMS 46(2):20 (2020), doi:10.1145/3386241 ; code repository (no license file) — https://github.com/cridemichel/quarticpp
- **[ARCH-2010]** Archibald, C. et al. AI Magazine 31(4):33–41 (2010) — http://www-cs-students.stanford.edu/~shoham/www%20papers/CompPool_AIMag_April28.pdf
- **[SMITH-2007]** Smith, M. Artif. Intell. 171(16):1069–1091 (2007); workshop version — http://webdocs.cs.ualberta.ca/~jonathan/PREVIOUS/Grad/Papers/pickpocket.pdf
- **[WPA-EQ]** WPA, *Recommended Equipment Specifications* (cushion speed test §8; sights §6; pockets §9) — https://wpapool.com/wp-content/uploads/2024/01/RECOMMENDED-EQUIPMENT-SPECIFICATIONS.pdf (see `equipment.md`)
- **[FB-WIKI]** FooBillard — https://en.wikipedia.org/wiki/FooBillard ; **[FB-GH]** FooBillard++ source (`src/billmove.c`) — https://github.com/alrusdi/foobillardplus
- **[BIL-NONGNU]** Billiards — https://www.nongnu.org/billiards/ ; Techne — https://www.nongnu.org/techne/
- **[TAIL-GH]** tailuge/billiards (GPL-3.0) — https://github.com/tailuge/billiards
- **[ZIT-GH]** jzitelli/PoolPhysics (MIT) — https://github.com/jzitelli/PoolPhysics
- **[VP4]** Virtual Pool 4 — https://vponline.celeris.com/vp4offover ; **[VP4-REV]** e.g., https://gertlushgaming.co.uk/review-virtual-pool-4-steam/ and Steam reviews https://steamcommunity.com/app/336150/reviews/
- **[AZB-VIRT]** AzBilliards forum, "Are there any really great virtual pool games out there?" — https://forums.azbilliards.com/threads/are-there-any-really-great-virtual-pool-games-out-there.509664/
- **[SP-WEB]** ShootersPool — https://www.shooterspool.net/
- **[PURE-REV]** Pure Pool reviews (Metacritic aggregate) — https://www.metacritic.com/game/pure-pool/
- **[PNFX-STEAM]** Pool Nation FX Steam discussion "Table Cloth Speed / Ball Spin / Pocket Size Needs improvement" — https://steamcommunity.com/app/314000/discussions/0/494631873668604316/
- **[PS-STEAM]** Pool Shark (Motivation Stack LLC, 2026) — https://store.steampowered.com/app/4959800
- **[BLB-BREAK]** Black Label Billiards, "How fast does the cue ball really travel on a typical break shot?" — https://blacklabelbilliards.com/blogs/blog/how-fast-does-the-cue-ball-really-travel-on-a-typical-break-shot
