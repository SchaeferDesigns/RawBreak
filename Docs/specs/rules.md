# RAW BREAK — Rules Engine Specification (WPA baseline)

| | |
|---|---|
| File | `Docs/specs/rules.md` |
| Version | 1.1 — 2026-09-25 (adversarially verified against the WPA 2025 texts; see §18 Verification log) |
| Normative baseline | WPA *Rules of Play*, effective 2025-09-15 (PDF revision published 2026-01-02) and WPA *Playing Regulations*, effective 2025-09-15 |
| Disciplines (full spec) | 8-Ball, 9-Ball, 10-Ball, 14.1 Continuous (straight pool) |
| Disciplines (variant spec) | WPA Blackball, APA 8-ball, US bar / house 8-ball, UK pub / WEPF / IEPF 8-ball |
| Consumer | `BilliardsCore` rules module (namespace `rb::rules`), AI shot planner, referee/UI overlay, replay system |
| Producer of input | the event-based physics simulator (see the physics, table-geometry and cue-strike specs in `Docs/specs/`) |

> **Copyright / wording.** The WPA rule book is copyrighted by the World Pool-Billiard Association. Everything below is a
> paraphrase written for programmers. Bracketed references point to the original text for verification:
> `[R 3.7]` = *Rules of Play* section 3.7, `[Reg 16]` = *Playing Regulations* section 16, `[Eq §16]` = WPA *Recommended
> Equipment Specifications* section 16.

**Tags used in this document**

| Tag | Meaning |
|---|---|
| **INTERPRETATION** | The WPA text is silent or ambiguous; we chose a reading. Each one is listed again in §14 (open questions). |
| **DERIVED** | A number or criterion that is not in any rule book; we derived it (derivation given). |
| **N/A-VG** | Rule exists but cannot occur (or is not modelled) in the video game; kept for completeness. |
| **LEGACY** | Behaviour of pre-2025 WPA rules, still selectable through `RulesConfig`. |
| **CONFIG** | Behaviour is a `RulesConfig` switch; the WPA default is stated. |

---

## 0. What changed in the current WPA rules (read this first)

The WPA rewrote its rules in 2025 (draft 2025-06-27, final edition effective 2025-09-15). Several points in older
references — and in the original project brief — are now outdated. The engine implements the **2025 text**.
Items tagged **(2025)** are real changes against the previous (2016) WPA edition; items tagged **(since 2016)** were
already in the 2016 text but are listed because they are frequently mis-implemented (verified against the 2016 PDF, §18).

1. **(2025) 9-ball rack:** the **9-ball sits on the foot spot** by default (1-ball at the head-side apex, two rows closer to the
   head). "1-ball on the foot spot" survives only as an organizer option → `NineBallRack = OneOnSpot` (LEGACY). Note: the
   final text of [R 5.2] no longer mentions that option at all; it is documented only in Dr. Dave's summary of the
   2025-06-27 draft. The 2016 edition racked the 1 on the foot spot.
2. **(2025) All-ball fouls only.** Touching/moving *any* ball illegally is a foul [R 3.6]. "Cue-ball fouls only" is **no longer
   an allowed alternative** under WPA (Dr. Dave 2025 change summary). The brief's note "WPA: cue ball fouls only" is
   therefore wrong for the current rules; cue-ball-fouls-only is kept only as a league variant (§12).
3. **(2025) Three-ball break rule (9-ball):** an object ball counts only if its **center passes beyond** the head string (or it
   is pocketed); merely reaching ("touching") the line no longer counts [Reg 16(1)]. See §7.2 for a wording conflict
   between [R 5.3(c)] and [Reg 16(1)].
4. **(2025) Unintentional scoop / miscue** that makes the cue ball leave the cloth (even jumping a ball) is handled like a normal
   miscue/jump — **not a foul** [R 2.11].
5. **(2025) Uncertainty favours the shooter:** simultaneous hits of a legal and an illegal ball, or of a legal ball and a
   cushion, are resolved as "legal ball first" [R 3.2, 3.3]; a foul that cannot be established is not a foul [Reg 25].
6. **8-ball:** (since 2016) shots are called even on an open table and the called ball decides the group [R 4.4];
   (since 2016) **8-ball has no three-foul rule** [R 3.13]; **(2025)** hitting the 8 first on an open table is a foul
   **unless one group is already completely off the table** — that exception is new [R 4.4]; **(2025)** 8 pocketed on
   the break together with **any** foul gives the opponent the spot/re-break option — the 2016 text said "scratches"
   only [R 4.3(f)].
7. **(2025) 10-ball:** there is **no safety call** (the 2016 text had one); if a ball goes down but the called shot was not
   made, the opponent chooses who shoots next [R 6.5, 6.6]; the 10 wins only when it is the **last** object ball [R 6.7].
8. **(since 2016) Hanging balls:** a ball that stays motionless at the pocket edge for 5 s after the shot is not pocketed
   even if it drops later [R 2.2].

---

## 1. Conventions (mandatory, shared with all RAW BREAK specs)

* **Units:** SI (m, s, kg, rad). Angles in rules text are sometimes given in degrees for readability; code uses rad.
* **World frame:** right-handed; origin at the **center of the table bed**; **+x** along the table length toward the
  **foot** (rack) end, **+y** across the width, **+z** up. The cloth/slate surface is **z = 0**; a resting ball's center is
  at **z = R**. Seen from the head end looking toward the foot, **+y is to the left**.
* **Ball:** radius **R**, mass **m**, moment of inertia **I = (2/5) m R²**. State per ball: position **r**, linear
  velocity **v**, angular velocity **ω** (world frame, rad/s).
* **Contact-point slip velocity** (used here only to decide "ball still moving"):

  `u = v + ω × (−R ẑ) = ( v_x − R ω_y ,  v_y + R ω_x ,  v_z )`

  Sign check: a ball rolling without slip toward +x has `ω = (ẑ × v)/R = (0, v_x/R, 0)`, so
  `u_x = v_x − R·(v_x/R) = 0` ✓.
* **Shot timing:** `t = 0` is the instant of the **first cue-tip / cue-ball contact**. All event times are `double` seconds.
* **Ball ids:** `0` = cue ball (CB); `1..15` = numbered object balls (OB). 8-ball groups: **solids 1–7**, **stripes 9–15**, 8-ball.
* **Players:** index `0/1`. *Shooter* = player at the table; *incoming player* / *opponent* = the other one.
* **"Position" of a ball** = vertical projection of its center onto the cloth [R 2.13]. All line/spot/region tests use
  the center projection `p = (x, y)`.
* **Shot begins** at the first forward-stroke tip contact; **shot ends** when every ball in play has stopped moving
  **and spinning** [R 2.19] — in the event-based simulator: every ball is in the *stationary* motion state
  (`v = 0`, `ω = 0`; a ball in the *spinning* state with `v = 0, ω_z ≠ 0` is still moving), or pocketed, or off the table.

---

## 2. Table landmarks and identifiers

### 2.1 Dimensions

| Symbol | Meaning | Default (9-ft) | Other | Source |
|---|---|---|---|---|
| `L` | playing-surface length, cushion nose to cushion nose | 2.540 m (±3.175 mm) | 8-ft: 2.3368 m | [Eq §5] |
| `W` | playing-surface width | 1.270 m (±3.175 mm) | 8-ft: 1.1684 m | [Eq §5] |
| `R` | ball radius | 0.028575 m (Ø 57.15 mm ±0.127 mm) | UK pool: OB Ø ≈ 50.8–52.4 mm, CB Ø ≈ 47.6 mm | [Eq §16], Wikipedia (British 8-ball) |
| `m` | ball mass | 0.163 kg (range 0.156–0.170 kg) | | [Eq §16] |
| corner / side pocket mouth | lip to lip | 114.3–117.5 mm / 127.0–130.2 mm | | [Eq §9] |

The rules engine only needs `L`, `W`, `R`, the pocket ids and the landmark lines below; pocket/jaw geometry is owned by
the table-geometry spec.

### 2.2 Strings, spots and regions

| Landmark | Definition (paraphrase) | Formula | 9-ft value |
|---|---|---|---|
| Head cushion nose | head end | `x = −L/2` | −1.270 m |
| Foot cushion nose | foot end | `x = +L/2` | +1.270 m |
| Side cushion noses | | `y = ±W/2` | ±0.635 m |
| **Head string** `x_HS` | bounds the quarter of the table nearest the head rail [R 2.1] | `x_HS = −L/4` | −0.635 m |
| **Foot string** `x_FS` | bounds the quarter nearest the foot rail | `x_FS = +L/4` | +0.635 m |
| Center string | line between the side pockets | `x = 0` | 0 |
| Long string | lengthwise center line | `y = 0` | 0 |
| **Head spot** `HS_pt` | head string ∩ long string | `(−L/4, 0)` | (−0.635, 0) |
| **Foot spot** `FS_pt` | foot string ∩ long string | `(+L/4, 0)` | (+0.635, 0) |
| **Center spot** `CS_pt` | center string ∩ long string | `(0, 0)` | (0, 0) |
| Baulk line (Blackball only) | 1/5 of the length from the head cushion [R 8.1] | `x_B = −L/2 + L/5 = −0.3 L` | −0.762 m (Blackball is normally played on a 7-ft table) |
| Diamonds | 1/8 of `L`, 1/4 of `W`, nose to nose [R 2.1] | spacing `L/8`, `W/4` | 0.3175 m, 0.3175 m |

**Region predicates** (all with a purely numerical tolerance `ε_line = 1e-6 m`):

* `AboveHeadString(p) ⇔ p.x < x_HS − ε_line`. The string itself is **not** "above" [R 2.1]; a ball exactly on the head string
  is "in front of or on" it and is **playable** from the kitchen [R 1.6, 3.11].
* `OnHeadString(p) ⇔ |p.x − x_HS| ≤ ε_line`; `BelowHeadString(p) ⇔ p.x > x_HS + ε_line` ("in front of" = toward the foot).
* **Crossing** a line means the ball's center strictly passes beyond it at some instant during the shot (not only at rest).
  This is written explicitly for object balls in [Reg 16(1)]; we use the same definition for the cue ball in [R 3.11]
  (INTERPRETATION, consistent with [R 2.13]).
  * `CueBallCrossedHeadString ⇔ ∃t: x_CB(t) > x_HS + ε_line` (for a CB that started above the head string).
  * `ObjectBallCrossedHeadString(b) ⇔ ∃t: x_b(t) < x_HS − ε_line` (for an OB that started below it).
* `InBaulk(p) ⇔ p.x < x_B − ε_line` (Blackball; "above the head string" reads as "in baulk" there [R 8.1]).

### 2.3 Pockets, cushions and "rails"

```
                          +y  ("left" for a player at the head end facing the foot)
      P5 ●──────────── C4 ────────────● P4 ──────────── C3 ────────────● P3
         │               :                 :                 :   ▲▲      │
         │               :                 :                 :  ▲▲▲ rack │
   C5    │  kitchen      : HS              : CS              : FS●  ▲▲▲  │   C2
  (head) │  (above HS)   : x=-L/4          : x=0             : x=+L/4    │  (foot)
         │               :                 :                 :           │
      P0 ●──────────── C0 ────────────● P1 ──────────── C1 ────────────● P2
                          -y  ("right")                                   ───► +x
```

| Pocket id | Center (nominal) | Name (player at head end facing foot) |
|---|---|---|
| `P0` | (−L/2, −W/2) | head-right corner |
| `P1` | (0, −W/2) | right side |
| `P2` | (+L/2, −W/2) | foot-right corner |
| `P3` | (+L/2, +W/2) | foot-left corner |
| `P4` | (0, +W/2) | left side |
| `P5` | (−L/2, +W/2) | head-left corner |

Cushion segments: `C0` (−y, head half, P0–P1), `C1` (−y, foot half, P1–P2), `C2` (foot, P2–P3), `C3` (+y, foot half,
P3–P4), `C4` (+y, head half, P4–P5), `C5` (head, P5–P0). Each pocket has two jaw facings. CONFIG: if the table-geometry
spec numbers pockets or segments differently, it wins; this table is then an adapter.

**"Rail" for rules purposes** [R 2.1, 2.7]: cushions, pocket jaws/facings, rail tops, pockets and pocket liners are all
*parts of the rails*. A contact with any of them is a rail contact. In addition, a ball that is **pocketed** or **driven off
the table** counts as having been driven to a rail [R 2.7].

---

## 3. Physics → rules interface

The physics simulates the whole shot instantly and hands the rules engine one immutable **`ShotRecord`**. The rules engine
never looks at physics internals; it derives everything from the record (§3.5). The same record feeds replays and the AI.

### 3.1 `ShotStart` snapshot (taken at `t = 0⁻`)

| Field | Type | Notes |
|---|---|---|
| `Balls[16]` | status {`OnTable`, `Pocketed`, `OutOfPlay`} + position + motion state | `OutOfPlay` = driven off earlier and not spotted (8-ball) |
| `AllBallsAtRest` | bool | false ⇒ foul [R 3.9]; UI normally prevents the stroke |
| `CueBallInHand` | {`No`, `Anywhere`, `AboveHeadString`, `Baulk`} + placed position | |
| `FrozenToRail[b]` | set of cushion/jaw segment ids with gap ≤ `ε_frozen` | auto-declared and shown in the UI before the shot [R 2.7, Reg 26] |
| `FrozenToCueBall` | set of OB ids with CB–OB gap ≤ `ε_frozen` | [R 3.7] ¶2 |
| `TemplatePresent` | bool | only if the rack template is modelled (8/9/10-ball, never 14.1 [Reg 4]) |
| `ShotClockElapsed` | s, measured from "all balls at rest" to tip contact | [Reg 18] |
| `FootOnFloor` | bool at tip contact (avatar IK) | [R 3.4]; `true` unless the avatar can lift both feet |

### 3.2 `StrokeRecord` (from the cue-strike model)

| Field | Meaning |
|---|---|
| `TipContacts[]` | list of intervals `[t_s, t_e]` during which the tip touches the CB. Normal stroke: exactly one, duration 0.8–2.0 ms (Dr. Dave FAQ "cue tip contact time"). |
| `TipClothContact` | tip touched the cloth during or immediately before a tip contact (scoop candidate) |
| `Miscue` | the cue-strike model's miscue criterion fired (tip slid off) |
| `CueElevation` | rad, at first contact (replay / jump classification) |
| `NonTipContacts[]` | every contact of player-controlled equipment with **any** ball other than the tip→CB stroke: shaft/ferrule/butt, bridge hand, mechanical bridge, body, clothing, hair, chalk, the hand placing the CB, the CB-in-hand touching an OB. Window: from the moment the player is "down on the shot" (or from CB placement) until the shot ends. [R 3.6] |

### 3.3 Event log (time-ordered; ties ordered by a stable sequence number)

| Event | Fields | Used for |
|---|---|---|
| `BallBall` | `a, b, t`, contact normal, `cutAngle` φ when one ball is the CB (angle between the CB pre-impact velocity and the line of centers; 0 = full hit) | first contact, double hit, carom/combination detection |
| `BallCushion` | `ball, segment, t, continuesInitialFreeze` | rail rules |
| `BallJaw` | `ball, pocket, side, t, continuesInitialFreeze` | rail rules (jaws are rails) |
| `BallRailTop` | `ball, t` | off-table / rail rules |
| `BallPocketEnter` / `BallPocketExit` | `ball, pocket, t` | rattles; exit = rebounded onto the playing surface (not pocketed [R 2.2]) |
| `BallPocketed` | `ball, pocket, t` | final: ball came to rest below the playing surface or entered the ball return |
| `BallTouchesPocketedBall` | `ball, pocketedBall, t` | a CB touching an already pocketed ball **is pocketed**, even if it comes back out [R 2.2] |
| `BallAirborne` / `BallLand` | `ball, t, zMax` | jump detection |
| `BallLineCross` | `ball, line ∈ {HS, FS, CS, LS, Baulk}, direction, t` | roots of `x(t) − x_line` (or `y(t)`) on the analytic trajectories |
| `BallExternalContact` | `ball, object ∈ {Lamp, Player, Floor, Furniture, Chalk, Template}, t` | off-table, template foul |
| `BallOffTable` | `ball, reason ∈ {Floor, RestsOnRailOrFrame, ExternalObjectRebound}, t` | [R 2.6] |
| `MotionTransition` | `ball, from, to, t` (sliding/rolling/spinning/stationary) | end of shot, replay |
| `TipBallBegin` / `TipBallEnd` | `t` | copy of `TipContacts` for ordering |

`continuesInitialFreeze` is set by the physics on a cushion/jaw contact of a ball with a segment it was frozen to at
`t = 0`, **as long as** that ball has not yet separated from that segment by more than `ε_leave` since `t = 0`. It
implements "a ball frozen to a rail is not driven to that rail unless it leaves and comes back" [R 2.7].

### 3.4 `ShotEnd` snapshot and the 5-second window

* `tStop` = time the last ball became stationary (spin included).
* **Settle window** `T_settle = 5 s` [R 2.2]: a ball resting motionless at a pocket edge is only "not pocketed" if it stays
  up for 5 s after the shot is over; play is suspended during that time. If the physics models slow creep/settling, any
  ball that drops in `[tStop, tStop + T_settle]` is pocketed **on this shot**. A ball that drops later (settling) is put
  back as closely as possible to where it stood [R 1.8]; if that happens during or just before the next shot and affects
  it, the position is restored and the shot replayed without penalty [R 1.8]. If the physics does not model settling,
  the window is empty and costs no wall-clock time (the UI may still show a short "hanging ball" moment).
* **Supported ball over a pocket:** a ball that is held at the pocket edge only because another ball (jammed in the jaws,
  or a full pocket) supports it counts as pocketed if it would fall when the supporter is removed [R 2.2]. The physics
  reports it as `SupportedOverPocket(pocket, supporters)`; the rules treat it exactly like `BallPocketed(pocket)`, and the
  engine removes it from the table before spotting.
* Final per-ball status: `OnTable(p, FrozenToRail, FrozenToOtherBalls)`, `Pocketed(pocket)`, `OffTable`.

### 3.5 Derived facts (`ShotFacts`) — normative definitions

The rules module computes these from `ShotRecord` in one pass (pure function, unit-testable). "OB" = object ball on the
table at shot start.

**F1 — First contact and tie set** [R 3.2]
* `t1` = earliest `BallBall` event involving the CB and an OB. If the CB started **frozen** to an OB `f` and the stroke
  moved `f`, the physics emits a `BallBall(CB, f)` at `t ≈ 0`; then `f` has been contacted [R 3.7 ¶2]. Shooting **away**
  from a frozen ball does **not** count as contacting it under WPA [R 3.7 ¶3] (Blackball differs, §12.4).
* `TieSet = { OB k : first CB–k contact time ≤ t1 + ε_tie }`.
* `FirstContact(IsLegal)`: if any ball in `TieSet` is legal for this shot, the first contact **is** that legal ball
  (lowest id if several — INTERPRETATION); otherwise it is the ball with the earliest contact. No CB–OB contact ⇒ `None`.

**F2 — Driven to a rail after first contact** [R 2.7, 3.3]
* Reference time: `tRef = t1`, but if the first contact was legal, CB rail contacts in `[t1 − ε_tie, t1)` are treated as
  occurring after it (legal-ball-first presumption of [R 3.3]).
* `DrivenToRail(b, after tRef)` ⇔ ∃ event of ball `b` at `t ≥ tRef` of type `BallCushion`, `BallJaw`, `BallRailTop`,
  pocket-liner contact, `BallPocketed`, `BallOffTable`, **with** `continuesInitialFreeze == false`.
* `AnyBallDrivenToRailAfterFirstContact` = OR over all balls including the CB.

**F3 — Break counters**
* `NumObjectBallsDrivenToRail` = number of **distinct** OBs with a rail contact, pocketing or off-table event at any time
  in the shot (off-table and pocketed balls count [R 2.7]).
* `NumObjectBallsDrivenToRailAfterRackContact` (14.1): same, restricted to events after the CB's first contact with a
  rack ball.
* `CountPocketedOrCrossedHeadString` (9-ball, [Reg 16]) = number of **distinct** OBs that were pocketed **or** whose center
  crossed the head string (each ball counted once).
* `NumObjectBallsCrossedCenterString` (Blackball break).

**F4 — Pocketing** — ordered list `(ball, pocket, t)` after the settle window; `PocketOf(b)` (or `None`);
`CueBallPocketed` (includes `BallTouchesPocketedBall` and a CB supported over a pocket); `AnyObjectBallPocketed`.

**F5 — Off table** [R 2.6] — a ball is off the table if it comes to rest anywhere other than the playing surface without
being pocketed, **or** if it hit an outside object (lamp, player, furniture, chalk on the rail, floor) that sent it back
(it "would have left the table"). A ball that only touches the rail top and returns to the cloth or drops into a pocket is
**not** off the table. `CueBallOffTable`, `ObjectBallsOffTable`.

**F6 — Head-string facts** — `CueBallCrossedHeadString`, `CueBallCrossedHeadStringBeforeFirstContact`,
`CueBallContactedBallOnOrBelowHeadString` (any CB–OB contact with an OB whose position at contact time satisfies
`!AboveHeadString`), `FirstContactBallAboveHeadString`.

**F7 — Double hit** [R 3.7] — `DoubleHit` is true if
* (a) `TipContacts.size() ≥ 2` (the cue touched the CB more than once), **or**
* (b) a `BallBall(CB, k)` occurs while a tip contact is in progress (`t_s ≤ t ≤ t_e`) with `k` **not** frozen to the CB at
  start, **unless** it is a thin graze `φ ≥ φ_graze` ("barely grazes" exemption, [R 3.7 ¶1]).
* **Frozen-ball exemption** (DERIVED from [R 3.7 ¶2]): if the CB started frozen to `f` and the shot goes into `f`, tip
  re-contacts and a long contact are legal **while** the CB is within `d_sep` of `f` and has not yet touched any other ball
  or cushion. Contacts outside that envelope still count under (a)/(b).

**F8 — Push shot** [R 3.8] — `PushShot ⇔ max(t_e − t_s) > T_push`, except inside the frozen-ball envelope of F7.
DERIVED: normal tip contact lasts 0.8–2.0 ms (Dr. Dave); `T_push = 4 ms` is twice the upper end, so no normal stroke is
ever flagged, while a genuine "shove" (the tip travelling with the ball) lasts tens of ms.

**F9 — Jump, scoop, miscue** [R 2.9, 2.11]
* `CueBallAirborne` = any `BallAirborne` on the CB.
* `Scoop = TipClothContact && CueBallAirborne` with the lift-off following that tip contact.
* `JumpedOver(k)` (needed only for Blackball [R 8.13.3]; DERIVED approximation): during an airborne interval of the CB,
  the horizontal distance between the CB and OB `k` drops below `2R` without a `BallBall(CB, k)` event. (The exact test —
  re-simulating the same stroke with vertical motion suppressed and checking whether `k` would be struck — is an optional
  higher-fidelity implementation.)
* WPA consequences: a legal jump (cue elevated, CB driven down into the slate and rebounding) is legal in all four WPA
  disciplines. A scoop is **treated like a miscue**, and an unintentional miscue that makes the CB leave the cloth is
  **treated like a legal jump** [R 2.11] → **no foul by itself**. An intentional miscue is unsportsmanlike conduct
  [R 3.16(c)]; the engine cannot judge intent → `ScoopPolicy` CONFIG (§12), default `WpaMiscue` (no foul).
  A miscue can still produce a double hit (F7), which is a foul.

**F10 — Touched ball** [R 3.6] — `NonTipBallContact = !NonTipContacts.empty()`. Applies to **all** balls (all-ball fouls).
In "Assisted" input mode the avatar/cue colliders are ghosted so this cannot happen; in "Sim" mode it is live.

**F11 — Cue-ball placement legality** [R 1.6, 3.10] — when the CB is in hand, the placed center `p` must satisfy
`|p.x| ≤ L/2 − R`, `|p.y| ≤ W/2 − R`, not over a pocket opening (table spec), distance to every other ball's center
`≥ 2R − ε_overlap`, and the region constraint (`AboveHeadString(p)` / `InBaulk(p)` where required). The UI normally
clamps placement; if a mode allows free placement, a violation is foul 3.10.

**F12 — Misc** — `BallsMovingAtStart` [R 3.9], `FootOnFloor` [R 3.4], `TemplateTouched` (any ball contacts the removed
template lying on the rail [R 3.15]), `ShotClockExpired` [R 3.14, Reg 18].

**F13 — Per-ball contact summary** — for every ball: ordered list of its ball–ball contacts (with partner and time), its cushion/jaw contacts split into **before** and **after** `t1`, its pocket id or off-table flag, whether it crossed the head/center string, and its final position and frozen sets. Used by the obvious-shot predicate (§4.5), 8-ball group logic, statistics, the AI evaluator and the replay HUD.

### 3.6 Tolerances

| Symbol | Default | Meaning | Basis |
|---|---|---|---|
| `ε_tie` | 0.5 ms | two contacts within this window count as "approximately the same instant" | DERIVED: at 1–3 m/s a ball moves 0.5–1.5 mm in 0.5 ms — below what a referee can resolve by eye; the rules resolve such cases for the shooter [R 3.2, 3.3, Reg 25]. Range 0–2 ms. |
| `ε_frozen` | 1.0e-4 m | gap at rest that counts as "touching" (ball–ball, ball–cushion nose) | DERIVED (0.1 mm is invisible at viewing distance). Range 1e-5–5e-4 m. |
| `ε_leave` | 5.0e-4 m | separation after which a frozen ball has "left" the rail/ball | DERIVED |
| `ε_line` | 1.0e-6 m | numerical tolerance for string/spot predicates | numerical |
| `ε_overlap` | 1.0e-7 m | CB placement overlap tolerance | numerical |
| `T_push` | 4.0e-3 s | longest normal tip contact | DERIVED from 0.8–2.0 ms (Dr. Dave FAQ) × 2 |
| `φ_graze` | 75° (1.309 rad) | thinner hits are "barely grazing" for [R 3.7 ¶1] | DERIVED: ball-fraction `1 − sin φ ≈ 3.4 %` |
| `d_sep` | 5.0e-3 m | frozen-ball exemption envelope (F7/F8) | DERIVED |
| `T_settle` | 5 s | hanging-ball window | [R 2.2] |
| `ε_lag` | 5.0e-4 m | lag distances closer than this are a tie → re-lag | DERIVED ([R 1.2(g)] "cannot determine") |
| `δ_cbGap` | 1.0e-3 m | gap kept between a spotted ball and the CB | DERIVED from [R 1.5] "small separation" |

---

## 4. General rules mapped to the engine (WPA ch. 1–3)

### 4.1 Lag for the first break [R 1.2]
* Two balls are placed above the head string, near it, one on each side of the long string. Default placement (DERIVED):
  `(x_HS − R − 0.01 m, ±W/4)`. Both players stroke; the two lag shots are **simulated together in one simulation** with
  both tip contacts at `t = 0` (so the "struck after the other ball touched the foot cushion" case cannot occur; in
  networked play record both strokes first, then simulate).
* Goal: hit the foot cushion and come back as close as possible to the head cushion. Distance metric:
  `d = (x_rest − R) − (−L/2)` (nearest point of the ball to the head cushion nose line).
* **Bad lag** (cannot win) if the ball: (a) crosses the long string (`y` changes sign beyond `ε_line`); (b) contacts the
  foot cushion `C2` other than exactly once; (c) is pocketed or off the table; (d) touches a side cushion (`C0,C1,C3,C4`)
  — jaw contacts at the side pockets count as side-cushion contact (INTERPRETATION); (e) rests inside the corner pocket
  beyond the head cushion nose line (`x_rest − R < −L/2`); or any non-object-ball foul other than 3.9 occurs on it
  (double hit, push, touched ball, …).
* **Re-lag** if both lags are bad, or `|d_A − d_B| ≤ ε_lag`, or (networked hot-seat only) one ball was struck after the
  other reached the foot cushion.
* Winner chooses who breaks first (all four disciplines) [R 1.2, 4.1, 5.1, 6.1, 7.1]. AI default: break in 8/9/10-ball;
  make the opponent break in 14.1.

### 4.2 Subsequent breaks [R 1.3, Reg 1]
In rack-scored disciplines (8, 9, 10-ball) the break **alternates**. Organizers may choose otherwise (Reg 1) → CONFIG
`BreakOrder ∈ {Alternate (default), WinnerBreaks, LoserBreaks}`. 14.1 has one opening break per match (plus re-breaks
after stalemate or the three-foul penalty).

### 4.3 Spotting balls [R 1.5]
Spotted balls go on the long string, as close as possible to the foot spot, **between the foot spot and the foot rail**,
without moving other balls; if the foot spot is occupied, the ball is placed touching the interfering ball if possible —
**but never touching the cue ball** (keep a small gap). If the whole long string from the foot spot to the foot rail is
blocked, the ball goes **toward the head** from the foot spot, as close to it as possible.

**Algorithm `SpotBall(b)`** (DERIVED implementation of the text; exact, no iteration):
1. For every ball `j` on the table (other than `b`) with `|y_j| < D_j`, where `D_j = 2R` for object balls and
   `D_j = 2R + δ_cbGap` for the CB, block the open interval `( x_j − h_j , x_j + h_j )` on the long string, with
   `h_j = sqrt(D_j² − y_j²)`. (A ball at `(x, 0)` would overlap `j` exactly for `x` inside that interval.)
2. Allowed range on the long string: `x ∈ [−L/2 + R, L/2 − R]`.
3. `x* = ` the smallest `x ≥ x_FS` in the allowed range that is not inside any blocked interval. The candidates are
   `x_FS` itself and every right end `x_j + h_j ≥ x_FS` of a blocked interval — test them in ascending order.
4. If none exists, `x* =` the largest `x < x_FS` not blocked (candidates: left ends `x_j − h_j`).
5. Place `b` at `(x*, 0, R)`, at rest. If it ends exactly tangent to another ball, the pair is frozen (physics must accept
   exact tangency without overlap).

Several balls to spot at once: spot them one after another in **ascending number** (INTERPRETATION; the WPA text gives no
order — BCA convention), except Blackball which has its own order [R 8.11]. Spotting happens after the shot ends and
before the next shot [R 6.8].

### 4.4 Cue ball in hand [R 1.6]
* `Anywhere`: any legal placement on the playing surface (F11). The player may keep adjusting until the stroke.
* `AboveHeadString`: placement strictly above the head string; then fouls 3.10 and 3.11 can apply.
* **Spot request:** if the CB is in hand above the head string and **every legal object ball** is also above the head
  string, the shooter may ask for the legal object ball **nearest the head string** (smallest `x_HS − x_b`) to be spotted
  (§4.3). If several are equally near, the shooter picks. A ball *on* the head string is playable and blocks the request.

### 4.5 Calling shots [R 1.7]
* Where calling is required, the shooter designates **one** ball and **one** pocket. Everything else (cushions, kisses,
  other balls made) is irrelevant: the call is fulfilled iff the called ball finally lies in the called pocket and the
  shot has no foul.
* Obvious shots need no explicit call in real play; banks, kicks, combinations and caroms should be called. A video game
  cannot read intent, so CONFIG `CallMode`:
  * `Explicit` (ranked/tournament default): ball + pocket (or "Safety" where allowed) must be selected before every
    called shot.
  * `ObviousAssist` (casual default): if the player made no call, the engine infers one **only** for an obvious shot:
    `Obvious(b, p)` ⇔ `b` = first contact; before `b` is pocketed in `p` it touches **no other ball** and **no cushion
    other than the jaws of `p`**; and the CB touched **no cushion before** hitting `b`. The inferred call is `(b, p)` for
    the first ball satisfying this. Any other pocketing counts as "not called". (DERIVED from [R 1.7].)
* "Safety" call: allowed in 8-ball [R 4.6] and 14.1 [R 7.5]; **not** in 10-ball [R 6.5]; 9-ball has no calls at all.
  After a safety the turn passes at the end of the shot.

### 4.6 Pocketed, hanging and settling balls [R 1.8, 2.2]
See §3.4. Additional: an OB that bounces out of a pocket onto the cloth is not pocketed; a ball that leaves one pocket and
drops in another is pocketed in the second one.

### 4.7 Off the table [R 2.6] — see F5. Which balls are spotted depends on the discipline (§4.9 table).

### 4.8 Stalemate, concession, interference [R 1.10, 1.12, 1.13]
* **Stalemate:** when no progress is being made, the referee announces it; each player then gets **three more turns**;
  if still no progress, stalemate is declared (players may accept it earlier by agreement). Consequence per discipline:
  8/9/10-ball → the **original breaker of the rack breaks again** [R 4.11, 5.9, 6.11]; 14.1 → **new lag**, opening break,
  scores carried over [R 7.12].
  Game heuristic (DERIVED, CONFIG): arm the stalemate warning after `N_stall = 8` consecutive innings in which no object
  ball was pocketed **and** no foul occurred; in PvP both players may also request it (mutual agreement).
* **Concession** loses the match [R 1.12].
* **Outside interference** [R 1.10] — N/A-VG in single player. In online play a desync/fault is treated as interference:
  restore the pre-shot position and replay the shot.

### 4.9 Foul catalogue [R 3]

"✓" = applies, "✗" = not part of that discipline, "BIH" = cue ball in hand, "HS" = head string.

| Rule | Foul | Detection | 8-ball | 9-ball | 10-ball | 14.1 | Notes |
|---|---|---|---|---|---|---|---|
| 3.1 | Cue ball scratch / off table | F4, F5 | ✓ BIH anywhere | ✓ BIH anywhere | ✓ BIH anywhere | ✓ −1, **BIH above HS** | break specifics per discipline |
| 3.2 | Wrong ball first | F1 | ✓ own group / open: not the 8 | ✓ lowest ball | ✓ lowest ball | ✗ | suspended on 9/10 push-out |
| 3.3 | No rail after contact | F2 | ✓ | ✓ | ✓ | ✓ | only if no OB pocketed; suspended on push-out |
| 3.4 | No foot on floor | F12 | ✓ | ✓ | ✓ | ✓ | N/A-VG unless the avatar can lift both feet |
| 3.5 | Object ball off table | F5 | ✓ ball stays out (8 ⇒ loss) | ✓ only the 9 is spotted | ✓ only the 10 is spotted | ✓ all spotted | |
| 3.6 | Touched ball (all-ball fouls) | F10 | ✓ | ✓ | ✓ | ✓ | "Sim" input mode only |
| 3.7 | Double hit / frozen balls | F7 | ✓ | ✓ | ✓ | ✓ | from cue-strike physics |
| 3.8 | Push shot | F8 | ✓ | ✓ | ✓ | ✓ | from cue-strike physics |
| 3.9 | Balls still moving | F12 | ✓ | ✓ | ✓ | ✓ | UI prevents |
| 3.10 | Bad CB placement | F11 | ✓ | ✓ (break) | ✓ (break) | ✓ | UI clamps |
| 3.11 | Bad play from above HS | F6 | ✓ | ✗ | ✗ | ✓ (¶2 ⇒ BIH above HS) | only when the CB started in hand above HS |
| 3.12 | Playing out of turn | — | ✓ | ✓ | ✓ | ✓ | N/A-VG: the engine owns turn order |
| 3.13 | Three consecutive fouls | counters | ✗ | ✓ loss of rack | ✓ loss of rack | ✓ −1 −15, re-rack, re-break | serious foul |
| 3.14 | Slow play / shot clock | F12 | ✓ | ✓ | ✓ | ✓ | only if a shot clock is active |
| 3.15 | Rack template foul | F12 | ✓ | ✓ | ✓ | (✓) listed in R 7.9, but cannot occur: no template in 14.1 [R 7.2, Reg 4] | N/A-VG unless template modelled |
| 3.16 | Unsportsmanlike conduct | — | ✓ | ✓ | ✓ | ✓ | not auto-detected; penalty = referee choice (default: serious-foul penalty) |

**Rule 3.11 in detail** (only when the shot starts with the CB in hand above the head string):
* ¶1 — if the first ball the CB contacts is above the head string, the shot is a foul unless the CB's center crossed the
  head string before that contact (kick shots off the foot side are therefore legal).
* ¶2 — the CB must either cross the head string or contact a ball that is on or below the head string; otherwise foul.
  In 14.1 (and One-Pocket/Bank) a ¶2 foul gives the incoming player the CB in hand above the head string [R 7.9].
* Intentional violation is unsportsmanlike (N/A-VG).

**Several fouls on one shot** [R 3 intro]: only the most serious is enforced. Engine order (DERIVED from the penalties):
0. **14.1 opening-break shots first:** if the opening-break requirement fails, the shot **is a breaking foul and nothing
   else** [R 7.10] — any standard foul on it (e.g. a scratch) is absorbed, and it is **never** counted as a standard foul,
   so it can **not** become a third consecutive foul even if the breaker is on two fouls [R 7.11]. (Corrected in v1.1:
   v1.0 ranked "third consecutive foul" above the breaking foul, which contradicts R 7.10/7.11; the §10.6 code was
   already right.)
1. Rack/match-level: 8-ball loss conditions, third consecutive foul (9/10 loss of rack; 14.1 −15 & re-break).
2. Standard fouls that move the CB to the kitchen in 14.1 (3.1, 3.11 ¶2).
3. All other standard fouls.

All detected fouls are still recorded in the outcome (UI, replay, statistics); only one penalty is applied.

**Other general principles**
* A foul not called before the next shot is treated as not having happened [R 3] — the engine always calls immediately,
  so this is N/A-VG.
* "If the referee cannot determine whether a foul occurred, the shot is legal" [Reg 25] → the tie windows of §3.6 are
  the only place where uncertainty is modelled, and they may only remove a foul, never create one.
* All-ball fouls: moving any ball other than by the stroke/ball-to-ball contact is a foul [R 3.6]. There is no
  cue-ball-fouls-only option in WPA play (§0).
* Three-foul warning: the rule is only enforceable if the player was warned when coming to the table on two fouls
  [R 3.13]; a visible foul counter on the scoreboard satisfies the warning [Reg 8]. The HUD must always show it.

### 4.10 Shot clock and time-outs (optional) [R 3.14, Reg 14, Reg 18]
* Shot clock (CONFIG, off by default): recommended **35 s** per shot, **warning at 10 s remaining**, **one 25 s extension
  per player per rack**. The clock starts when all balls (including spinning ones) are at rest and stops at tip contact.
  Running out of time is a **standard foul**. For the shot after the opening break the organizer may allow more time, but
  never more than **60 s** (INTERPRETATION of the last sentence of Reg 18).
* 14.1 has no racks → one extension per player per rack of 15 balls (INTERPRETATION).
* Time-out: one **5 min** time-out per player in matches longer than **9 racks (8-ball)** or **13 racks (9/10-ball)**, taken
  between racks; in 14.1 it starts between racks [Reg 14]. N/A-VG except for spectator/online pause UX.

### 4.11 Frozen-ball calls [Reg 26, R 2.7, 3.7]
A ball counts as frozen (to a rail or to the CB) only if declared. The engine auto-declares using `ε_frozen` and shows a
"FROZEN" marker before the stroke; this replaces the referee call.

---

## 5. Racks (geometry and fill rules)

### 5.1 Lattice
Balls are racked "as tightly as possible" [R 4.2, 5.2, 6.2]. Nominal lattice (touching balls): row spacing along +x is
`√3·R`, lateral spacing `2R`. For a rack whose apex (row 0) ball center is at `A = (x_A, 0)`:

`center(r, k) = ( x_A + r·√3·R ,  (2k − n_r + 1)·R )`, `k = 0 … n_r − 1`, `n_r` = balls in row `r`.

The apex points **toward the head** (the breaker), rows grow toward the foot rail. The physics/break spec may add
realistic micro-gaps or a seeded "rack quality" perturbation; the rules only care about which ball is on which lattice
site. All random fills use a **seeded** RNG stored in the replay (determinism). "Without intentional pattern" = uniformly
random permutation of the free balls over the free sites, subject to the constraints below.

| Discipline | Shape (`n_r`) | Fixed sites | Apex position `x_A` |
|---|---|---|---|
| 8-ball [R 4.2] | triangle 1,2,3,4,5 | apex: any ball except the 8 (random); **8 at (r=2, k=1)**, i.e. the first ball straight behind the apex; the two back corners (r=4, k=0 and k=4): **one solid and one stripe** (which corner is random) | `x_FS` |
| 9-ball [R 5.2] default | diamond 1,2,3,2,1 | **1 at apex** (r=0); **9 at the diamond center (r=2, k=1) = foot spot** | `x_FS − 2√3·R` |
| 9-ball LEGACY `OneOnSpot` | same | 1 at apex on the foot spot; 9 at center | `x_FS` |
| 10-ball [R 6.2] | triangle 1,2,3,4 | **1 at apex on the foot spot**; **10 at the middle of the third row (r=2, k=1)** | `x_FS` |
| 14.1 opening [R 7.2] | triangle 1,2,3,4,5 | none (random fill) | `x_FS` |
| 14.1 re-rack [R 7.2] | triangle, **apex site left empty** (14 balls) | none | `x_FS` (apex site empty) |
| Blackball [R 8.4] | triangle 1,2,3,4,5 | black on the foot spot at (r=2, k=1); colour pattern per WPA diagram (not in the text — see §14) | `x_FS − 2√3·R` |

(9-ball diamond row offsets: r0: 0; r1: ±R; r2: −2R, 0, +2R; r3: ±R; r4: 0 — this is the general formula with `n_r`.)

**Numeric reference (9-ft, R = 0.028575 m, √3·R = 0.049493352 m, x_FS = 0.635 m)**

| Item | Position (m) |
|---|---|
| 8-ball apex | (0.635000, 0) |
| 8-ball 8-ball | (0.733987, 0) |
| 8-ball back corners | (0.832973, ±0.114300) |
| 9-ball 9 (default) | (0.635000, 0) |
| 9-ball 1 (default) | (0.536013, 0) |
| 9-ball last ball (r=4, default) | (0.733987, 0) |
| 10-ball 1 | (0.635000, 0) |
| 10-ball 10 | (0.733987, 0) |
| 10-ball back row | x = 0.783480, y = ±0.085725, ±0.028575 |
| 15-ball back row | x = 0.832973, y = 0, ±0.057150, ±0.114300 |

### 5.2 14.1 rack outline (for "does a ball interfere with the rack?") [R 7.2, 7.8]
The outline is the triangle drawn around a tight 15-ball rack with the apex ball on the foot spot (DERIVED: each edge of
the triangle of ball centers moved outward by `R`, sharp corners):

* apex vertex `(x_FS − 2R, 0)`; back vertices `(x_FS + 4√3R + R, ±(4R + √3R))`; side length `8R + 2√3R`.
* 9-ft values: apex (0.577850, 0); back vertices (0.861548, ±0.163793); side 0.327587 m.
* `InterferesWithRack(p) ⇔ dist(p, OutlineTriangle) < R` (distance 0 if `p` is inside) — a ball interferes if it is
  inside or overlaps the outline. The referee answers this on request [R 7.8]; the UI shows it live.

---

## 6. 8-Ball (WPA ch. 4)

**Objective.** 15 object balls: solids 1–7, stripes 9–15, and the 8. The shooter must clear his group, then legally pocket
the 8 in a called pocket. Shots are called (except the break).

### 6.1 First break — lag winner chooses who breaks [R 4.1]; later racks per §4.2.

### 6.2 Break shot [R 4.3]
* CB in hand **above the head string**. No call. Any ball may be hit first (3.2 does not apply to the break).
* **Legal break requirement:** at least one OB pocketed, **or** at least **four** distinct OBs driven to a rail (F3).

| Situation (evaluated in this order) | Result |
|---|---|
| 8 pocketed, **no foul** [R 4.3(e)] | Not a foul. **Breaker chooses:** (1) spot the 8 (§4.3) and continue shooting with the balls as they lie; or (2) re-rack and break again. |
| 8 pocketed **with a foul** (any: scratch, CB off table, OB off table, …) [R 4.3(f)] | **Incoming player chooses:** (1) spot the 8 and shoot with **CB in hand above the head string**; or (2) re-rack and break (the incoming player breaks — INTERPRETATION of "re-breaking"). |
| No OB pocketed and fewer than 4 OBs to a rail — **illegal break** [R 4.3(d)] | **Incoming player chooses:** (1) accept the table and shoot; (2) re-rack and break himself; (3) re-rack and let the same breaker break again. INTERPRETATION: if the breaker also fouled (e.g. scratched), option (1) comes with the CB in hand above the head string. |
| Any OB driven off the table (legal count met) [R 4.3(g)] | Foul. Off-table balls **stay out of play** — except the 8, which is spotted. Incoming player: accept the table **or** take CB in hand above the head string. |
| Any other foul, e.g. scratch (legal count met) [R 4.3(h)] | Incoming player: accept the table **or** CB in hand above the head string. If the CB is not on the table (scratch/off table), only the second option exists. |
| Legal, no foul, ≥ 1 OB pocketed [R 4.3(c)] | Breaker continues; **table stays open** (balls made on the break never assign groups). |
| Legal, no foul, nothing pocketed | Turn passes; incoming player shoots from position; table open. |

After a break foul the incoming player's CB is "in hand above the head string", so 3.10, 3.11 and the spot request of
§4.4 apply to that shot.

### 6.3 Open table and groups [R 4.4]
* The table is open until groups are assigned. On an open table the shooter **must call** a ball (and pocket).
* First contact on an open table: **any OB except the 8**. Hitting the 8 first is a foul — **unless one group is already
  completely off the table**: then the shooter may temporarily **claim** that group and play the 8 (possibly for the win).
  The claim lasts for that shot only (INTERPRETATION of "temporarily").
  * Literal reading of [R 4.4] (fixed in v1.1): once a group is completely off the table, hitting the 8 first on an open
    table is **not a foul even without an explicit claim**. Only a *claimed* 8 can win, though: pocketing the 8 without
    a claim is a loss under 4.8(b) (the shooter has no cleared group). The declaration validator therefore **auto-sets
    the claim** whenever the shooter calls the 8 on an open table (legal only if a group is completely off the table);
    if both groups qualify, the shooter picks (it has no effect beyond this shot).
  * The text says "completely pocketed"; we read it as "completely off the table" because 8-ball never spots jumped
    balls [R 4.7] and [R 4.6] speaks of a group "cleared from the table".
* If the shooter **legally pockets the called ball** (called pocket, no foul), he gets that ball's group and the opponent
  the other — regardless of what else dropped (a combination via a ball of the other group is fine). Otherwise the table
  stays open and the turn passes (if nothing legal was pocketed).
* "Group cleared" and "on the 8" are evaluated at **shot start**.

### 6.4 Calls and continuing play [R 4.5, 4.6]
* Every shot after the break is called (§4.5). The called ball must belong to the shooter's group; once the group is off
  the table, the 8 is the called ball. The shooter may call **safety** instead: the turn ends after the shot and anything
  pocketed stays down.
* The shooter continues while he legally pockets called balls. Extra balls pocketed on the shot (own or opponent's) stay
  down. A shot that pockets balls but not the called one ends the turn; those balls stay down.

### 6.5 Spotting [R 4.7] — only the 8, and only after the break (§6.2). Any other object ball driven off the table stays
out of play (it counts as "off the table" for clearing a group).

### 6.6 Loss of the rack [R 4.8, 4.10] (not on the break)
The shooter **loses** if he: (a) pockets the 8 on a shot with any foul (incl. scratch); (b) pockets the 8 while his group
was not yet cleared at the start of the shot (so the 8 and the last group ball on the same shot = loss); (c) pockets the 8
in a pocket other than the called one (including on a safety call); (d) drives the 8 off the table.
Note: under WPA a scratch while playing the 8 **without** pocketing it is only a standard foul.

### 6.7 Standard fouls [R 4.9] — the incoming player gets **CB in hand anywhere**. List: 3.1, 3.2 (own group first; open
table: not the 8), 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.11, 3.12, 3.14, 3.15. **No three-foul rule.**
Unsportsmanlike conduct: referee's choice [R 4.10].

### 6.8 Stalemate [R 4.11] — the original breaker of the rack breaks again.

### 6.9 Mixed-up groups [Reg 10] — N/A-VG (the engine always knows the groups).

---

## 7. 9-Ball (WPA ch. 5)

**Objective.** Balls 1–9, played in ascending order: the CB must first contact the **lowest-numbered ball on the table**.
Any ball legally pocketed keeps the turn; legally pocketing the **9** on any shot (except a push-out) wins the rack.
No calls.

### 7.1 Rack [R 5.2, Reg 15] — §5.1 (9 on the foot spot by default). Balls other than 1 and 9 random.

### 7.2 Break [R 5.3, Reg 16]
* CB in hand above the head string. The CB must contact the **1-ball first** (3.2 applies to the break; the 1 is the lowest).
* If no ball is pocketed, at least **four** OBs must reach a rail — otherwise **standard foul** (BIH anywhere).
* **Three-ball rule** (CONFIG `ThreeBallRule`, default **on**; mandatory at WPA ranking events [Reg 16]): at least
  **three** OBs must be pocketed and/or have their center cross the head string (any mix; each ball counted once).
  **Wording conflict (found in verification):** [R 5.3(c)] says "if no ball is pocketed, three balls must cross the Head
  String", i.e. literally the rule would not apply at all once any ball is pocketed, whereas [Reg 16(1)] counts pocketed
  and crossing balls together ("if one object-ball is pocketed, then at least two … must cross"). The Regulations
  normally yield to the Rules [Reg 1], but R 5.3(c) explicitly points to Reg 16 for the rule, and Reg 16(5) (the 9
  pocketed on a failing break) would be dead text under the literal R 5.3(c) reading. Default: **Reg 16 combined count**;
  CONFIG `ThreeBallRuleScope ∈ {Reg16Combined (default), OnlyIfNothingPocketed}` (§12.1, §14, test N23).
  If this fails on an otherwise legal break (no foul), the break is **illegal but not a foul**: the incoming player either
  (a) **accepts the table** as it lies — and may **not** push out — or (b) **hands the shot back** to the breaker, who may
  then push out (and after that push-out the opponent again chooses to play or hand back) [Reg 16(2)–(4)]. A 9 pocketed on
  such a break is **spotted** before the next shot [Reg 16(5)].
* 9 pocketed on a legal break (three-ball rule satisfied, no foul) → **breaker wins** the rack.
* Foul on the break (incl. wrong ball first): BIH anywhere for the incoming player; a pocketed 9 is spotted; other
  pocketed balls stay down. The three-ball rule is not evaluated after a foul (the foul already hands over the table).

### 7.3 Push-out [R 5.4]
* Only on the **shot right after a legal (foul-free) break**, by whoever takes that shot (the breaker if he pocketed a
  ball, otherwise the opponent — or the breaker after a three-ball-rule hand-back). Must be declared before the stroke.
* On a push-out, **3.2 (wrong ball first) and 3.3 (no rail) are suspended**; every other foul still applies.
* After a foul-free push-out the **other player chooses** who shoots next from the position. Balls pocketed on the push-out
  stay down, **except the 9, which is spotted** [R 5.6]. A foul on the push-out is a normal standard foul.

### 7.4 Continuing play [R 5.5] — legally pocketing any ball keeps the turn; failing to pocket or fouling passes it; after a
foul-free miss the incoming player plays from the position left.

### 7.5 Spotting [R 5.6] — only the 9: when pocketed on a foul or push-out, or driven off the table.

### 7.6 Fouls [R 5.7, 5.8] — standard fouls ⇒ **BIH anywhere**: 3.1, 3.2 (lowest ball first), 3.3, 3.4, 3.5 (only the 9
spotted), 3.6, 3.7, 3.8, 3.9, 3.10, 3.12, 3.14, 3.15. **Three consecutive fouls** by the same player within one rack
⇒ **loss of the rack**. The counter resets on any legal shot by that player and at every new rack.

### 7.7 Stalemate [R 5.9] — the original breaker of the rack breaks again.

---

## 8. 10-Ball (WPA ch. 6)

**Objective.** Balls 1–10, lowest ball first, **call shot** (no safety call). The rack is won by legally pocketing the
**10 on a called shot when it is the only object ball on the table**.

### 8.1 Rack [R 6.2] — §5.1 (1 on the foot spot, 10 in the middle of the third row, rest random).

### 8.2 Break [R 6.3] — CB in hand above the head string; CB must hit the 1 first (3.2); if nothing is pocketed at least
four OBs must reach a rail, otherwise standard foul. No call on the break. A 10 pocketed on the break is **spotted**
[R 6.8]. If the break is legal and at least one ball was pocketed, the **breaker continues** (INTERPRETATION: the 2025
text is silent; this matches every previous WPA 10-ball edition). No three-ball rule in 10-ball.

### 8.3 Push-out [R 6.4] — identical to 9-ball (§7.3); a 10 pocketed on a push-out is spotted.

### 8.4 Calls, wrongfully pocketed balls, continuing [R 6.5–6.7]
* Every shot after the break is called (§4.5). **There is no "safety" call.** A player who wants to play safe calls some
  ball and deliberately misses.
* **Called ball made** (called pocket, no foul): the shooter continues; other balls pocketed stay down, except the 10,
  which is spotted unless this was the winning shot.
* **Winning shot:** the called ball is the 10, it drops in the called pocket, no foul, **and the 10 was the only object ball
  on the table at the start of the shot** (INTERPRETATION of "when it is the only object-ball on the table"; CONFIG
  `TenOnlyBallMoment ∈ {ShotStart (default), AtPocketing}`).
* **Wrongfully pocketed ball(s)**: a ball went down but the called shot was not made, no foul → the **incoming player
  chooses** who shoots next from the position [R 6.6]; pocketed balls stay down except the 10 (spotted).
* **Miss, nothing pocketed**, no foul → turn passes; incoming player plays from position.

### 8.5 Spotting [R 6.8] — only the 10: driven off the table, or pocketed on anything but the winning shot; spotted before
the next shot.

### 8.6 Fouls [R 6.9, 6.10] — same list as 9-ball (3.5: only the 10 spotted) ⇒ **BIH anywhere**; three consecutive fouls ⇒
loss of the rack. Doubles: after a pass-back following an uncalled ball, the turn goes to the other team member
[Reg 27(8)].

### 8.7 Stalemate [R 6.11] — the original breaker breaks again.

---

## 9. 14.1 Continuous / Straight Pool (WPA ch. 7)

**Objective.** Play to a target score (typically 100–150 points, CONFIG `TargetPoints`). Each legally pocketed called ball
scores 1 point, and **every additional ball pocketed on that same shot also scores 1**. When 14 balls of a rack have been
legally pocketed, they are re-racked (apex empty) and the shooter continues. Scores can be negative.

### 9.1 Rack [R 7.2] — §5.1; no rack template in 14.1 [Reg 4].

### 9.2 Opening break [R 7.3, 7.10]
* CB in hand above the head string. The breaker may call a ball and pocket.
* Requirement: **either** the called ball is pocketed in the called pocket, **or** after the CB first contacts the rack,
  the **CB and at least two OBs are each driven to a rail** (a pocketed CB or OB counts as driven to a rail [R 2.7]).
  (Corrected in v1.1 from "legally pocketed": [R 7.3(b)] only says "if no called ball is pocketed". So a called ball that
  drops on a shot where the CB also scratches **meets** the break requirement; the scratch is then a standard foul
  (−1, ball spotted, CB in hand above HS), not a breaking foul (−2) — test S24. The §10.6 code already did this.)
* Failing it is a **breaking foul: −2 points**. The opponent then chooses: accept the table in position, or make the breaker
  **re-break** (re-rack all 15; each further failure costs another −2) until the requirement is met or the opponent accepts.
* A breaking foul **does not count** toward the three-consecutive-foul rule, and if a standard foul happens on the same
  shot, only the breaking foul is charged [R 7.10, 7.11]. INTERPRETATION: if the opponent accepts the table after a
  breaking foul in which the CB was pocketed/off the table, he gets CB in hand above the head string.
* If the requirement is met, the shot is then judged as a normal shot: e.g. a scratch after a legal opening break is a
  standard foul (−1, CB in hand above the head string for the opponent); uncalled pocketed balls are spotted and the
  turn passes.

### 9.3 Normal shots [R 7.4–7.7]
* Every shot is called (ball + pocket) or "safety" [R 7.5]. Any ball may be hit first (no 3.2). 3.3 applies.
* **Called ball made, no foul:** `+1` for it and `+1` for every other ball pocketed on the shot; continue.
* **Safety, or miss (called ball not made), no foul:** no points; **every ball pocketed on the shot is spotted**; turn passes.
* **Standard foul** [R 7.9]: `−1`; all balls pocketed on the shot and all balls driven off the table are spotted; turn
  passes. The CB **stays where it stopped**, except after 3.1 (scratch / off table) or a 3.11 ¶2 foul, when the incoming
  player has the **CB in hand above the head string**.
* The match ends the moment the shooter reaches the target score [R 7.4].

### 9.4 Three consecutive fouls [R 7.11]
Only standard fouls count. On the third: `−1` as usual **plus an extra −15** (total −16 for that shot), the player's foul
count resets to 0, **all 15 balls are re-racked**, and the **offending player** must shoot an **opening break** under §9.2.

### 9.5 Re-racking during play [R 7.4, 7.6, 7.8, Table 1]
Applies after a scoring shot leaves exactly **one** object ball ("the 15th ball") on the table (spotting done first).
The 14 pocketed balls are racked with the apex site empty. Then:

| 15th ball ↓ / Cue ball → | CB interferes with the rack | CB not in rack, not blocking the head spot | CB blocking the head spot |
|---|---|---|---|
| **In the rack** | re-rack **all 15**; CB in hand above HS | 15th → **head spot**; CB stays | 15th → **center spot**; CB stays |
| **Pocketed** (same shot as the 14th) | re-rack all 15; CB in hand above HS | re-rack all 15; CB stays | re-rack all 15; CB stays |
| **Above HS, not blocking head spot** | 15th stays; CB → **head spot** | — | — |
| **On or below HS (not in rack)** | 15th stays; CB in hand **above HS** | — | — |
| **Above HS, blocking head spot** | 15th stays; CB → **center spot** | — | — |
| (neither interferes) | — | both stay | both stay |

* "Blocking a spot" (DERIVED): a ball placed on that spot would overlap it, i.e. `|p_other − spot| < 2R`.
* "Above HS" here uses the strict predicate of §2.2; a 15th ball exactly on the head string belongs to the "on or below"
  row [R 7.8(d)].
* After any re-rack the shooter continues his inning; the first shot of the new rack may be called on **any** ball
  [R 7.8]. There are no break requirements for these continuation racks.
* If a ball must be spotted while the 14 balls are still untouched, it goes on the (empty) apex site — the spotting
  algorithm of §4.3 produces exactly this, because the foot spot is free [R 7.6].

### 9.6 Stalemate [R 7.12] — new lag; the winner decides who takes an opening break; scores are kept.

---

## 10. `evaluateShot` — reference pseudocode

C++-flavoured pseudocode; no exceptions/RTTI, double precision, value types. Declaration validation (call present, call
legal for the discipline, push-out/safety allowed, CB placement region) happens **before** the stroke in the UI/AI layer;
the evaluator assumes a valid declaration.

### 10.1 Types

```cpp
namespace rb::rules {

enum class EDiscipline  { EightBall, NineBall, TenBall, StraightPool, Blackball };
enum class ECueBallNext { InPosition, InHandAnywhere, InHandAboveHeadString, InHandBaulk };
enum class EShotKind    { Break, Normal, PushOut, Safety };      // 14.1 opening break = Break
enum class EGroup       { None, Solids, Stripes };

struct Call { int Ball = -1; int Pocket = -1; };                  // -1 = none

struct ShotDeclaration {
    EShotKind Kind = EShotKind::Normal;
    Call      Called;                                            // where the discipline calls shots
    EGroup    ClaimedClearedGroup = EGroup::None;                // 8-ball open table, R 4.4
};

struct PlayerState { int Score = 0; int ConsecutiveFouls = 0; EGroup Group = EGroup::None; };

struct GameState {                                               // state at SHOT START
    EDiscipline  Discipline;
    int          Shooter = 0;
    int          RackBreaker = 0;
    BallStatus   Balls[16];                                      // OnTable(pos) / Pocketed / OutOfPlay
    ECueBallNext CueBall = ECueBallNext::InHandAboveHeadString;
    bool         IsBreakShot = true;                             // 8/9/10: first shot of rack; 14.1: opening break
    bool         PushOutAvailable = false;                       // 9/10
    bool         TableOpen = true;                               // 8-ball
    PlayerState  P[2];
};

enum class EFoul { CueBallScratch, CueBallOffTable, WrongBallFirst, NoRailAfterContact, BreakTooFewRails,
                   NoFootOnFloor, ObjectBallOffTable, TouchedBall, DoubleHit, PushShot, BallsStillMoving,
                   BadCueBallPlacement, BadPlayAboveHS_P1, BadPlayAboveHS_P2, SlowPlay, TemplateFoul,
                   IllegalScoop /*variant*/, BreakingFoul141, ThreeConsecutiveFouls };

enum class EOption { AcceptTable, BallInHandAboveHeadString, RerackDeciderBreaks, RerackOffenderBreaks,
                     Spot8ContinueFromPosition, Spot8BallInHandAboveHeadString,
                     AcceptTableNoPushOut, HandBackPushOutAllowed,          // 9-ball three-ball rule
                     ShootFromPosition, PassBack,                           // after push-out / 10-ball wrongful pocket
                     RequireRebreak };                                      // 14.1 breaking foul

enum class ENext { Continue, Pass, AwaitDecision, RackWon, MatchWon, RerackAndBreak };

struct ShotOutcome {
    FoulSet        Detected;                 // every foul found (UI/replay)
    EFoul          Enforced;  bool AnyFoul = false;
    ENext          Next;
    int            NextShooter = -1;         // or deciding player for AwaitDecision
    int            Winner = -1;              // RackWon / MatchWon
    ECueBallNext   NextCueBall = ECueBallNext::InPosition;
    ECueBallNext   CueBallIfAccepted = ECueBallNext::InPosition;   // for AcceptTable options
    OptionList     Options;
    SmallVec<int>  BallsToSpot;              // in order
    int            ScoreDelta[2] = {0, 0};
    int            FoulsAfter[2];            // consecutive-foul counters after this shot
    EGroup         AssignShooterGroup = EGroup::None;
    bool           NextPushOutAvailable = false;
    RackCommand    Rack;                     // None / Rerack15 / Rerack14 plan (§9.5)
    const char*    RuleRef = "";             // e.g. "R 4.8(b)" for the HUD
};
}
```

### 10.2 Common helpers

```cpp
FoulSet DetectCommonFouls(const RulesConfig& C, const ShotFacts& F)
{
    FoulSet X;
    if (F.BallsMovingAtStart)             X.Add(EFoul::BallsStillMoving);      // R 3.9
    if (!F.FootOnFloor)                   X.Add(EFoul::NoFootOnFloor);         // R 3.4
    if (!F.CueBallPlacementLegal)         X.Add(EFoul::BadCueBallPlacement);   // R 3.10
    if (F.NonTipBallContact)              X.Add(EFoul::TouchedBall);           // R 3.6 (all-ball fouls)
    if (F.DoubleHit)                      X.Add(EFoul::DoubleHit);             // R 3.7
    if (F.PushShot)                       X.Add(EFoul::PushShot);              // R 3.8
    if (F.CueBallPocketed)                X.Add(EFoul::CueBallScratch);        // R 3.1
    if (F.CueBallOffTable)                X.Add(EFoul::CueBallOffTable);       // R 3.1
    if (!F.ObjectBallsOffTable.Empty())   X.Add(EFoul::ObjectBallOffTable);    // R 3.5
    if (F.ShotClockExpired)               X.Add(EFoul::SlowPlay);              // R 3.14
    if (C.UseRackTemplate && F.TemplateTouched) X.Add(EFoul::TemplateFoul);   // R 3.15
    if (C.Scoop == EScoopPolicy::Foul && F.Scoop) X.Add(EFoul::IllegalScoop);  // variants only
    return X;
}

template <class LegalFn>
bool WrongBallFirst(const ShotFacts& F, LegalFn IsLegal)                        // R 3.2 + tie rule
{
    if (F.FirstContactTieSet.Empty()) return false;          // no contact at all -> R 3.3 handles it
    for (int b : F.FirstContactTieSet) if (IsLegal(b)) return false;
    return true;
}

bool NoRailAfterContact(const ShotFacts& F, bool FirstContactWasLegal)          // R 3.3 + tie rule
{
    if (F.AnyObjectBallPocketed) return false;
    if (F.FirstContactTieSet.Empty()) return true;           // CB touched no object ball
    return !F.AnyBallDrivenToRailAfterFirstContact(FirstContactWasLegal);       // F2
}

int BadPlayFromAboveHeadString(const GameState& S, const ShotFacts& F)          // R 3.11; 0, 1 or 2
{
    if (S.CueBall != ECueBallNext::InHandAboveHeadString) return 0;
    if (!F.CueBallCrossedHeadString && !F.CueBallContactedBallOnOrBelowHeadString) return 2;  // para 2
    if (F.FirstContact >= 0 && F.FirstContactBallAboveHeadString
        && !F.CueBallCrossedHeadStringBeforeFirstContact) return 1;                            // para 1
    return 0;
}

int LowestObjectBallAtStart(const GameState& S);        // 9/10-ball
bool GroupCleared(const GameState& S, EGroup g);        // no ball of g OnTable at shot start
Call ResolveCall(const RulesConfig& C, const ShotDeclaration& D, const ShotFacts& F);  // Explicit or ObviousAssist (4.5)
```

`Lose(p)`, `Win(p)`, `Pass(next, cb)`, `Continue(p)`, `Decide(p, options)` are small constructors filling `ShotOutcome`
(they also copy `Detected` and pick `Enforced` by the severity order of §4.9).

### 10.3 8-ball

```cpp
ShotOutcome Evaluate8Ball(const RulesConfig& C, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
{
    const int me = S.Shooter, opp = 1 - me;
    FoulSet X = DetectCommonFouls(C, F);
    if (int p = BadPlayFromAboveHeadString(S, F))                                // R 3.11 is in the R 4.9 list and the
        X.Add(p == 2 ? EFoul::BadPlayAboveHS_P2 : EFoul::BadPlayAboveHS_P1);     // break starts in hand above HS (v1.1)

    if (S.IsBreakShot) {                                                         // R 4.3
        const bool eightIn = F.IsPocketed(8), eightOff = F.IsOffTable(8);
        if (eightIn)                                                             // (e)/(f)
            return X.Empty() ? Decide(me,  {Spot8ContinueFromPosition, RerackDeciderBreaks}, "R 4.3(e)")
                             : Decide(opp, {Spot8BallInHandAboveHeadString, RerackDeciderBreaks}, X, "R 4.3(f)");
        ShotOutcome O;
        if (eightOff) O.BallsToSpot.Add(8);                                      // (g), R 4.7
        const bool countOk = F.AnyObjectBallPocketed || F.NumObjectBallsDrivenToRail >= 4;
        if (!countOk) {                                                          // (d) illegal break
            O.CueBallIfAccepted = X.Empty() ? ECueBallNext::InPosition : ECueBallNext::InHandAboveHeadString; // INTERPRETATION
            return Decide(opp, {AcceptTable, RerackDeciderBreaks, RerackOffenderBreaks}, X, O, "R 4.3(d)");
        }
        if (!X.Empty()) {                                                        // (g)/(h)
            if (F.CueBallPocketed || F.CueBallOffTable)
                return Pass(opp, ECueBallNext::InHandAboveHeadString, X, O, "R 4.3(h)");
            O.CueBallIfAccepted = ECueBallNext::InPosition;
            return Decide(opp, {AcceptTable, BallInHandAboveHeadString}, X, O, "R 4.3(g/h)");
        }
        return F.AnyObjectBallPocketed ? Continue(me, O) : Pass(opp, ECueBallNext::InPosition, O);   // (c)
    }

    const bool   open    = S.TableOpen;
    const EGroup mine    = S.P[me].Group;
    const bool   claimOk = open && D.ClaimedClearedGroup != EGroup::None && GroupCleared(S, D.ClaimedClearedGroup);
    const bool   onEight = (!open && GroupCleared(S, mine)) || claimOk;          // at shot START
    const bool   anyGroupGone = open && (GroupCleared(S, EGroup::Solids) || GroupCleared(S, EGroup::Stripes));
    auto IsLegalFirst = [&](int b) {
        if (onEight) return b == 8;
        if (open)    return b != 8 || anyGroupGone;                              // R 4.4 (8 first ok once a group is gone)
        return GroupOf(b) == mine;                                               // R 4.9 / 3.2
    };
    const bool wbf = WrongBallFirst(F, IsLegalFirst);
    if (wbf)                                X.Add(EFoul::WrongBallFirst);
    if (NoRailAfterContact(F, !wbf))        X.Add(EFoul::NoRailAfterContact);
    const Call c = ResolveCall(C, D, F);                                         // explicit or inferred (4.5), used for the 8 too

    if (F.IsOffTable(8))                          return Lose(me, X, "R 4.8(d)");    // loss of rack, R 4.8/4.10
    if (F.IsPocketed(8)) {
        if (!X.Empty())                           return Lose(me, X, "R 4.8(a)");
        if (!onEight)                             return Lose(me, X, "R 4.8(b)");    // incl. unclaimed 8 on open table
        if (D.Kind == EShotKind::Safety || c.Ball != 8 || F.PocketOf(8) != c.Pocket)
                                                  return Lose(me, X, "R 4.8(c)");
        return Win(me, "R 4.5");
    }
    if (!X.Empty()) return Pass(opp, ECueBallNext::InHandAnywhere, X, "R 4.9");  // balls stay down, table unchanged

    if (D.Kind == EShotKind::Safety) return Pass(opp, ECueBallNext::InPosition, "R 4.6");
    const bool made = c.Ball >= 0 && F.PocketOf(c.Ball) == c.Pocket;
    if (!made) return Pass(opp, ECueBallNext::InPosition, "R 4.5");
    ShotOutcome O = Continue(me, "R 4.5");
    if (open) O.AssignShooterGroup = GroupOf(c.Ball);                            // R 4.4 (c.Ball != 8 here)
    return O;
}
```

### 10.4 9-ball

```cpp
ShotOutcome Evaluate9Ball(const RulesConfig& C, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
{
    const int  me = S.Shooter, opp = 1 - me;
    const int  lowest = LowestObjectBallAtStart(S);
    const bool push = (D.Kind == EShotKind::PushOut);                 // validator: only if S.PushOutAvailable
    FoulSet X = DetectCommonFouls(C, F);
    if (!push) {                                                      // R 5.4: 3.2 and 3.3 suspended on a push-out
        const bool wbf = WrongBallFirst(F, [&](int b) { return b == lowest; });
        if (wbf) X.Add(EFoul::WrongBallFirst);
        if (S.IsBreakShot) {
            if (!F.AnyObjectBallPocketed && F.NumObjectBallsDrivenToRail < 4) X.Add(EFoul::BreakTooFewRails);  // R 5.3(b)
        } else if (NoRailAfterContact(F, !wbf)) X.Add(EFoul::NoRailAfterContact);
    }
    ShotOutcome O;
    const bool nineIn = F.IsPocketed(9), nineOff = F.IsOffTable(9);

    if (!X.Empty()) {                                                             // R 5.7
        if (nineIn || nineOff) O.BallsToSpot.Add(9);                              // R 5.6
        const int n = S.P[me].ConsecutiveFouls + 1;
        if (C.ThreeFoulRule && n >= 3) { O.FoulsAfter[me] = 0; return Lose(me, X, O, "R 5.8"); }
        O.FoulsAfter[me] = n;
        return Pass(opp, ECueBallNext::InHandAnywhere, X, O, "R 5.7");
    }
    O.FoulsAfter[me] = 0;

    if (S.IsBreakShot) {
        const bool threeBallApplies = C.ThreeBallRule &&
            (C.ThreeBallRuleScope == Reg16Combined || !F.AnyObjectBallPocketed); // R 5.3(c) vs Reg 16(1), see 7.2
        if (threeBallApplies && F.CountPocketedOrCrossedHeadString < 3) {         // Reg 16
            if (nineIn) O.BallsToSpot.Add(9);                                     // Reg 16(5)
            return Decide(opp, {AcceptTableNoPushOut, HandBackPushOutAllowed}, O, "Reg 16");
        }
        if (nineIn) return Win(me, "R 5.5");
        O.NextPushOutAvailable = true;                                            // R 5.4
        return F.AnyObjectBallPocketed ? Continue(me, O) : Pass(opp, ECueBallNext::InPosition, O);
    }
    if (push) {
        if (nineIn) O.BallsToSpot.Add(9);                                         // R 5.6
        return Decide(opp, {ShootFromPosition, PassBack}, O, "R 5.4");
    }
    if (nineIn) return Win(me, "R 5.5");
    return F.AnyObjectBallPocketed ? Continue(me, O) : Pass(opp, ECueBallNext::InPosition, O);
}
```

### 10.5 10-ball

```cpp
ShotOutcome Evaluate10Ball(const RulesConfig& C, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
{
    const int  me = S.Shooter, opp = 1 - me;
    const int  lowest = LowestObjectBallAtStart(S);
    const bool push = (D.Kind == EShotKind::PushOut);
    FoulSet X = DetectCommonFouls(C, F);
    if (!push) {
        const bool wbf = WrongBallFirst(F, [&](int b) { return b == lowest; });
        if (wbf) X.Add(EFoul::WrongBallFirst);
        if (S.IsBreakShot) {
            if (!F.AnyObjectBallPocketed && F.NumObjectBallsDrivenToRail < 4) X.Add(EFoul::BreakTooFewRails);  // R 6.3(b)
        } else if (NoRailAfterContact(F, !wbf)) X.Add(EFoul::NoRailAfterContact);
    }
    ShotOutcome O;
    const bool tenIn = F.IsPocketed(10), tenOff = F.IsOffTable(10);

    if (!X.Empty()) {                                                             // R 6.9
        if (tenIn || tenOff) O.BallsToSpot.Add(10);                               // R 6.8
        const int n = S.P[me].ConsecutiveFouls + 1;
        if (C.ThreeFoulRule && n >= 3) { O.FoulsAfter[me] = 0; return Lose(me, X, O, "R 6.10"); }
        O.FoulsAfter[me] = n;
        return Pass(opp, ECueBallNext::InHandAnywhere, X, O, "R 6.9");
    }
    O.FoulsAfter[me] = 0;

    if (S.IsBreakShot) {                                                          // no call on the break, R 6.5
        if (tenIn) O.BallsToSpot.Add(10);                                         // R 6.8
        O.NextPushOutAvailable = true;                                            // R 6.4
        return F.AnyObjectBallPocketed ? Continue(me, O)                          // INTERPRETATION (8.2)
                                       : Pass(opp, ECueBallNext::InPosition, O);
    }
    if (push) {
        if (tenIn) O.BallsToSpot.Add(10);
        return Decide(opp, {ShootFromPosition, PassBack}, O, "R 6.4");
    }
    const Call c = ResolveCall(C, D, F);                                          // never Safety in 10-ball
    const bool made = c.Ball >= 0 && F.PocketOf(c.Ball) == c.Pocket;
    if (made) {
        const bool lastBall = (C.TenOnlyBallMoment == ShotStart)
                                ? OnlyObjectBallAtStart(S) == 10
                                : NoOtherObjectBallOnTableWhen(F, 10);           // CONFIG alternative
        if (c.Ball == 10 && (lastBall || C.TenOnlyBallMoment == EarlyTenWins))  // EarlyTenWins: non-WPA variant
            return Win(me, "R 6.7");
        if (tenIn) O.BallsToSpot.Add(10);                                         // R 6.8
        return Continue(me, O, "R 6.7");
    }
    if (F.AnyObjectBallPocketed) {                                                // R 6.6 wrongfully pocketed
        if (tenIn) O.BallsToSpot.Add(10);
        return Decide(opp, {ShootFromPosition, PassBack}, O, "R 6.6");
    }
    return Pass(opp, ECueBallNext::InPosition, O);
}
```

### 10.6 14.1 continuous

```cpp
ShotOutcome EvaluateStraightPool(const RulesConfig& C, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
{
    const int me = S.Shooter, opp = 1 - me;
    ShotOutcome O;
    FoulSet X = DetectCommonFouls(C, F);                                          // no 3.2 in 14.1
    const int p311 = BadPlayFromAboveHeadString(S, F);
    if (p311) X.Add(p311 == 2 ? EFoul::BadPlayAboveHS_P2 : EFoul::BadPlayAboveHS_P1);
    const Call c    = ResolveCall(C, D, F);
    const bool made = D.Kind != EShotKind::Safety && c.Ball >= 0 && F.PocketOf(c.Ball) == c.Pocket;

    if (S.IsBreakShot) {                                                          // opening break, R 7.3
        const bool req = made || (F.CueBallContactedRack
                                  && F.DrivenToRailAfterRackContact(kCueBall)
                                  && F.NumObjectBallsDrivenToRailAfterRackContact >= 2);
        if (!req) {                                                               // breaking foul, R 7.10
            O.ScoreDelta[me] = -2;                                                // absorbs any standard foul
            O.FoulsAfter[me] = S.P[me].ConsecutiveFouls;                          // not counted, R 7.11
            O.BallsToSpot = PocketedAndOffTableObjectBalls(F);
            O.CueBallIfAccepted = (F.CueBallPocketed || F.CueBallOffTable)
                                  ? ECueBallNext::InHandAboveHeadString : ECueBallNext::InPosition;  // INTERPRETATION
            X.Add(EFoul::BreakingFoul141);
            return Decide(opp, {AcceptTable, RequireRebreak}, X, O, "R 7.3(b)");
        }
    } else if (NoRailAfterContact(F, true)) {                                     // any ball may be hit first
        X.Add(EFoul::NoRailAfterContact);
    }

    if (!X.Empty()) {                                                             // standard foul, R 7.9
        O.ScoreDelta[me] = -1;
        O.BallsToSpot = PocketedAndOffTableObjectBalls(F);                        // R 7.6
        const int n = S.P[me].ConsecutiveFouls + 1;
        if (C.ThreeFoulRule && n >= 3) {                                          // R 7.11
            O.ScoreDelta[me] -= 15;
            O.FoulsAfter[me] = 0;
            O.Rack = RackCommand::Rerack15;
            O.Next = ENext::RerackAndBreak; O.NextShooter = me;                   // offender takes an opening break
            O.NextCueBall = ECueBallNext::InHandAboveHeadString;
            return Finish(O, X, "R 7.11");
        }
        O.FoulsAfter[me] = n;
        const bool kitchen = F.CueBallPocketed || F.CueBallOffTable || p311 == 2;
        return Pass(opp, kitchen ? ECueBallNext::InHandAboveHeadString : ECueBallNext::InPosition, X, O, "R 7.9");
    }
    O.FoulsAfter[me] = 0;
    if (!made) {                                                                  // miss or safety
        O.BallsToSpot = PocketedObjectBalls(F);                                   // R 7.5, 7.6
        return Pass(opp, ECueBallNext::InPosition, O);
    }
    const int pts = CountPocketedObjectBalls(F);                                  // R 7.7
    O.ScoreDelta[me] = pts;
    if (S.P[me].Score + pts >= C.TargetPoints) return WinMatch(me, O, "R 7.4");
    const int left = ObjectBallsOnTableAfterShot(S, F);
    if      (left == 1) O.Rack = PlanRerack14(S, F);                              // R 7.8 / Table 1 (9.5)
    else if (left == 0) O.Rack = PlanRerack15AfterFifteenthPocketed(S, F);        // R 7.8(a)
    return Continue(me, O, "R 7.4");
}

RackPlan PlanRerack14(const GameState& S, const ShotFacts& F)                     // §9.5
{
    const Vec2 cb = F.FinalPos(kCueBall);
    const Vec2 ob = F.FinalPos(TheOnlyObjectBall(S, F));
    const bool cbIn = InterferesWithRack(cb), obIn = InterferesWithRack(ob);
    if (!cbIn && !obIn) return { Rack14, Keep, Keep };
    if ( cbIn &&  obIn) return { Rack15, Keep /*15th racked*/, InHandAboveHeadString };        // R 7.8(b)
    if (obIn)           return { Rack14, BlocksSpot(HeadSpot, cb) ? ToCenterSpot : ToHeadSpot, Keep };  // (c)
    if (!AboveHeadString(ob)) return { Rack14, Keep, InHandAboveHeadString };                   // (d)
    return { Rack14, Keep, BlocksSpot(HeadSpot, ob) ? ToCenterSpot : ToHeadSpot };
}
// PlanRerack15AfterFifteenthPocketed: all 15 racked; CB -> InHandAboveHeadString if it interferes, else Keep.
```

Applying an outcome (engine side, all disciplines): add `ScoreDelta`; store `FoulsAfter`; assign groups; remove
supported-over-pocket balls; spot `BallsToSpot` in order (§4.3); execute `Rack` commands; set CB state; set
`PushOutAvailable = NextPushOutAvailable` for exactly the next shot; `IsBreakShot = false` unless a (re-)break follows.

---

## 11. Match state machine (race to N)

### 11.1 Configuration
`MatchConfig { Discipline; RaceTo (racks, 8/9/10-ball) or TargetPoints (14.1); optional Sets { RacksPerSet, SetsToWin };
BreakOrder; RulesConfig; ShotClock; Doubles }`.

### 11.2 States

```
          ┌────────┐   lag result: Relag
 Setup ──►│  Lag   │◄───────────────┐
          └───┬────┘                │
     winner   │                     │
              ▼                     │  (14.1 stalemate → Lag, scores kept)
       LagWinnerChooses ──► RackSetup(breaker) ◄──────────────────────────────┐
                                  │                                          │
                                  ▼                                          │
                      ┌──────► AwaitShot(shooter, constraints) ◄──────┐      │
                      │           │ declare / place CB / stroke        │      │
                      │           ▼                                    │      │
                      │       Simulate ──► Evaluate(evaluateShot)      │      │
                      │                      │                         │      │
                      │  Continue / Pass ────┘──► ApplyOutcome ────────┘      │
                      │                      │                                │
                      └── AwaitDecision ◄────┤ (options for a player)         │
                          (apply option)     │                                │
                                             ├── RackWon(w) ──► RackOver(w) ──┤ (not yet N)
                                             ├── RerackAndBreak ──────────────┘ (8-ball re-rack options, 14.1 penalty)
                                             └── MatchWon(w) ──► MatchOver
   Any AwaitShot: Stalemate ──► 8/9/10: RackSetup(RackBreaker), no score | 14.1: Lag
   Any time: Concede(p) ──► MatchOver(opponent)
```

### 11.3 Transition details
* **Setup:** create players, seed RNG, `Score = 0`.
* **Lag** (§4.1) → `Relag` or winner → **LagWinnerChooses** (who breaks).
* **RackSetup(breaker):** build the rack (§5), `CueBall = InHandAboveHeadString`, `IsBreakShot = true`, `TableOpen = true`,
  groups cleared, `PushOutAvailable = false`, `RackBreaker = breaker`; 9/10-ball: reset both `ConsecutiveFouls` (fouls count
  only within one rack [R 3.13]). 14.1 keeps its counters and scores across racks.
* **AwaitShot:** publish constraints to UI/AI: CB placement region, legal first-contact set (hint), call required?, safety
  allowed?, push-out allowed?, may request spot (§4.4)?, may claim a cleared group (8-ball)?, foul counter warning.
* **Evaluate** → `ShotOutcome` → **ApplyOutcome** (end of §10).
* **AwaitDecision(player, options):** the deciding player (or AI) picks one option:

| Option | Effect |
|---|---|
| `AcceptTable` | decider shoots next, balls as they lie, CB = `CueBallIfAccepted` |
| `BallInHandAboveHeadString` | decider shoots next with CB in hand above the head string |
| `RerackDeciderBreaks` / `RerackOffenderBreaks` | new rack of the same game (no score change); the named player breaks |
| `Spot8ContinueFromPosition` | spot the 8; breaker keeps shooting, CB in position, table open |
| `Spot8BallInHandAboveHeadString` | spot the 8; incoming player shoots with CB in hand above the head string |
| `AcceptTableNoPushOut` | incoming player shoots from position; no push-out on this shot |
| `HandBackPushOutAllowed` | breaker shoots from position; a push-out is allowed on this shot |
| `ShootFromPosition` | decider shoots from position (push-out window closed) |
| `PassBack` | the other player must shoot from position (no second push-out) |
| `RequireRebreak` | 14.1: re-rack 15, same breaker takes another opening break |

* **RackOver(w):** `Score[w] += 1`; if sets are used, update set score; if `Score[w] == RaceTo` → **MatchOver(w)**; else the
  next breaker per `BreakOrder` → RackSetup.
* **14.1:** the match ends inside Evaluate when a player reaches `TargetPoints` (`MatchWon`). `RerackAndBreak` (three-foul
  penalty) → RackSetup with `IsBreakShot = true` for the offender. Continuation re-racks (§9.5) happen inside ApplyOutcome
  without leaving the inning.
* **Stalemate** (§4.8) and **Concession** as shown.
* **Doubles** [Reg 27]: shooters alternate within the team; the first breaker of each team is chosen, then team breakers
  alternate; a pass-back after a push-out goes to the partner (default "push-out for the partner"); in 10-ball, a pass-back
  after an uncalled ball goes to the other team member; 14.1 alternates strictly.

---

## 12. Optional rule sets (variants) and `RulesConfig`

WPA is the default and the only rule set used for ranked play. Variants are pure data: a `RulesConfig` preset plus, for the
UK games, a few extra predicates. Nothing in §10 changes except where a flag is read.

### 12.1 `RulesConfig` switches (WPA defaults in bold)

| Flag | Values | Used by |
|---|---|---|
| `FoulScope` | **`AllBall`** / `CueBallOnly` | 3.6 (touched object ball is a foul or only "restore at opponent's option") |
| `CallMode` | `Explicit` / `ObviousAssist` / `EightOnly` / `None` | §4.5 (WPA: Explicit or ObviousAssist) |
| `Scoop` | **`WpaMiscue`** / `Foul` | F9 |
| `ThreeFoulRule` | 8-ball **off**; 9/10/14.1 **on** | 3.13 |
| `BreakOrder` | **`Alternate`** / `WinnerBreaks` / `LoserBreaks` | §4.2 |
| `FoulCueBall` (8-ball) | **`InHandAnywhere`** / `InHandBehindHeadString` / `InHandBaulk` / `FreeShotPlusVisit` / `TwoVisits` / `InPosition` | §6.7 |
| `RailAfterContactRequired` | **on** / off | 3.3 |
| `EightOnBreak` | **`SpotOrRebreakOption`** / `Win` / `Rerack` | §6.2 |
| `EightOnBreakWithFoul` | **`OpponentOption`** / `Lose` / `Rerack` | §6.2 |
| `ScratchWhileShootingEightLoses` | **off** / on | §6.6 |
| `LastPocketRule` | **off** / on | 8 must drop in the pocket of the shooter's last group ball |
| `BreakAssignsGroup` | **off** / `IfOnlyOneGroupPocketed` | §6.3 |
| `OpenTableEightFirstFoul` | **on** / off | §6.3 |
| `SpotJumpedObjectBalls` | **off** (8-ball) / on | §6.5 |
| `BreakFirstContact` | **`Any`** (8-ball) / `HeadBallOrSecondRow` | APA break |
| `NineBallRack` | **`NineOnSpot`** / `OneOnSpot` | §5.1 |
| `ThreeBallRule` | **on** / off | §7.2 |
| `ThreeBallRuleScope` | **`Reg16Combined`** / `OnlyIfNothingPocketed` (literal R 5.3(c)) | §7.2 |
| `TenOnlyBallMoment` | **`ShotStart`** / `AtPocketing` / `EarlyTenWins` | §8.4 |
| `TargetPoints` (14.1) | 100 / 125 / 150 | §9 |
| `JumpShots` | **`Legal`** / `Illegal` | Blackball, APA-scoop |
| `UseRackTemplate` | **off** (VG) / on | 3.15 |
| tolerances | §3.6 | all |

### 12.2 WPA LEGACY (pre-2025 editions) — diff to current WPA
* 9-ball racked with the **1 on the foot spot** (`NineBallRack = OneOnSpot`).
* Three-ball rule counted balls that merely **reached** the head string (use `ε_line` sign flip; `ThreeBallRuleReach`).
* "Cue-ball fouls only" was an allowed alternative for unrefereed play (`FoulScope = CueBallOnly`).
* 8-ball (corrected in v1.1 after checking the 2016 PDF): the 2016 WPA text already required a call on the open table and
  already made the 8 an illegal first contact on an open table — but **without** the 2025 "one group completely off the
  table" exception (`OpenTableEightFirstFoul = on` with no exception). The 8-on-break-with-foul option (R 4.3(f)) applied
  to a **scratch** only. Allowing the 8 as first contact in an open-table combination is an old **BCA/league** rule, not
  WPA (`OpenTableEightFirstFoul = off`, variant only).
* 10-ball 2016: a "safety" call existed, and pocketing the legal ball on a safety gave the opponent the play/pass-back
  option; the 10 made on a called shot before it was the last ball was re-spotted and the shooter continued (same as 2025).
* 9-ball 2016: the three-ball rule was optional ("only when … used") and counted balls that **touched** the head string.
* Some European events (2024) penalised unintentional scoops (`Scoop = Foul`). (Dr. Dave 2025 summary; AzBilliards thread.)

### 12.3 APA 8-ball (US amateur league) — diff to WPA 8-ball
Source: APA "Official Rules" pages (rules.poolplayers.com). The full APA Team Manual is not freely published — verify each
item against the current edition before shipping (§14).

| Topic | WPA | APA |
|---|---|---|
| Break requirement | pocket 1, or 4 OBs to a rail; any ball first | same count, **plus** the head ball or a second-row ball must be struck first and the CB may not hit a rail before the rack |
| Illegal break | incoming: accept / re-rack & break / re-rack & offender breaks | re-rack; **same breaker** breaks again (if the illegal break also scratched, the **opponent** breaks) |
| 8 on the break | breaker's option: spot & continue, or re-break | **breaker wins** |
| 8 on the break + CB foul | opponent's option | **breaker loses** |
| Scratch on a legal break | BIH above the head string (or accept) | BIH **behind the head string**, first contact must be an OB outside the kitchen |
| Groups | first **called** ball legally made | if only one group went down on the break it becomes the breaker's; otherwise the first group legally pocketed (no call needed) |
| Calling | every shot (obvious ones implicitly) | only the **8** (pocket marked) |
| Foul penalty | BIH anywhere | BIH anywhere |
| Touched object ball | foul (all-ball fouls) | not by itself a foul (opponent may have it replaced); CB touches are fouls → `FoulScope = CueBallOnly` |
| Frozen ball | covered by 2.7/3.3 | explicit frozen-ball rule (same effect: pocket it, drive it or another ball to a rail, or CB to a rail after contact) |
| Jump shots | legal; scoop = miscue | legal only by striking down through the CB; scooping/miscue on a jump = foul → `Scoop = Foul` |
| OB off the table | stays out | **spotted** on the foot spot (`SpotJumpedObjectBalls = on`) |
| Loss of game | 4.8 (a)–(d) | same, plus **scratch while shooting at the 8** even if the 8 does not drop, and altering the path of the 8/CB |
| Three fouls | none | none |

### 12.4 WPA Blackball (UK-style 8-ball, WPA ch. 8) — diff to WPA 8-ball
Primary source: WPA *Rules of Play* ch. 8 (same document as the baseline).

* Equipment: two groups of 7 (reds/yellows, or solids/stripes) + black; usually a 7-ft table with smaller balls (physics
  must support a different CB radius). Foot spot and **baulk line** (`x_B = −L/2 + L/5`) marked.
* Rack: black on the foot spot (third row center), pattern per the WPA diagram.
* **Break:** CB in hand **in baulk**. At least one ball potted **or at least two OBs cross the center string**, else foul.
  **Black potted on the break → re-rack, same player breaks again**; a scratch or ball off the table on that break is
  ignored.
* **Open table** until a player pots balls of **only one group** on a legal *normal* shot (not the break, not a free shot);
  no calling ("shots are not called").
* **Foul penalty:** the incoming player gets a **free shot**: 3.2 (wrong ball first) is suspended for that shot, and he may
  play the CB from where it lies or take it in hand **in baulk** (and need not play out of baulk). Then his inning continues
  normally.
* **Touching balls:** a CB touching an OB must not be played toward that ball; playing away from it counts as hitting it if
  it is "on" (differs from WPA 3.7).
* **Snookered** (declared by the referee: no straight path to hit any part of a ball "on") → 3.3 suspended.
* **Spotting:** all OBs driven off the table are spotted on the long string, in the order: black, then the next shooter's
  group (or reds/yellows if open), then the rest.
* **Extra fouls:** potting an opponent's ball without also potting one of your own; playing before balls are re-spotted;
  **any jump over a ball** (definition via F9 `JumpedOver`).
* **Loss of rack:** black potted on an illegal shot; black potted while any of your group remains on the table after the
  shot; intentional wrong-ball-first; not attempting to hit a ball "on" (N/A-VG: intent).
* **Stalemate:** breaker of the rack re-breaks; also declared when no legal shot is possible.

### 12.5 Other UK rule sets (summary only; verify before implementing)
Source: Wikipedia "Eight-ball pool (British variation)" comparison.

| Topic | WEPF (World Eightball Pool Federation) | IEPF "International Rules" (2022–) | Traditional pub rules |
|---|---|---|---|
| Break | pot a ball or ≥ 4 OBs hit a cushion | score ≥ 3 points: 1 per ball potted + 1 per ball entirely passing the center line | varies |
| Penalty | **two visits** for the incoming player, no free shot | **one visit** + CB in hand anywhere | "two shots", often carried to the black |
| Potting opponent's balls | not allowed | allowed if own group contacted first | varies |
| Jump / push shots | jump illegal; push allowed without double contact | both illegal | usually illegal |
| Deliberate foul | standard foul | loss of frame | — |

### 12.6 US bar / house rules (no governing body)
Bar rules vary from venue to venue (Wikipedia "Eight-ball"). Offer them as a "House rules" preset built from these toggles,
each individually selectable in the lobby:
* scratch/foul → **BIH behind the head string** with the 3.11 requirement (`FoulCueBall = InHandBehindHeadString`);
* **no rail-after-contact** requirement;
* **8 on the break wins** (and 8 on the break + scratch loses);
* **call only the 8** ("slop counts" for group balls) (`CallMode = EightOnly`);
* **scratch while shooting at the 8 loses**;
* **last-pocket** rule;
* **break assigns the group** if only one group was pocketed;
* **jumped balls re-spotted** on the foot spot.

---

## 13. Parameter table (all values with units)

| Parameter | Default | Typical range | Source |
|---|---|---|---|
| Table 9-ft `L × W` | 2.540 × 1.270 m | ±3.175 mm | [Eq §5] |
| Table 8-ft `L × W` | 2.3368 × 1.1684 m | ±3.175 mm | [Eq §5] |
| Ball radius `R` | 0.028575 m | ±0.0000635 m | [Eq §16] |
| Ball mass `m` | 0.163 kg | 0.156–0.170 kg | [Eq §16] |
| Head / foot string | `x = ∓L/4` (∓0.635 m) | — | [R 2.1] |
| Baulk line (Blackball) | `x = −L/2 + L/5` | — | [R 8.1] |
| Break: OBs to rail (8/9/10-ball) | 4 | — | [R 4.3(d), 5.3(b), 6.3(b)] |
| 9-ball three-ball rule | 3 balls pocketed/crossed | — | [Reg 16] |
| 14.1 opening break | CB + 2 OBs to a rail | — | [R 7.3(b)] |
| Blackball break | 1 potted or 2 OBs across center | — | [R 8.5(b)] |
| 14.1 penalties | standard −1, breaking −2, third foul −1 −15 | — | [R 7.9–7.11] |
| 14.1 target | 100 pts | 50–150 | organizer |
| Race (8/9/10-ball) | 7 racks (casual), 9–13 (pro) | 1–21 | organizer |
| Three-foul limit | 3 | — | [R 3.13] |
| Hanging-ball window `T_settle` | 5 s | — | [R 2.2] |
| Shot clock | 35 s, warning at 10 s left, one 25 s extension per rack, ≤ 60 s after the break | organizer | [Reg 18] |
| Time-out | 5 min, matches > 9 racks (8-ball) / > 13 racks (9/10) | — | [Reg 14] |
| Normal tip contact | 0.8–2.0 ms | — | Dr. Dave FAQ (tip contact time) |
| `T_push` | 4 ms | 3–10 ms | DERIVED (§3.5 F8) |
| `φ_graze` | 75° | 70–85° | DERIVED (§3.5 F7) |
| `d_sep` | 5 mm | 2–10 mm | DERIVED |
| `ε_tie` | 0.5 ms | 0–2 ms | DERIVED (§3.6) |
| `ε_frozen` | 0.1 mm | 0.01–0.5 mm | DERIVED |
| `ε_leave` | 0.5 mm | 0.1–2 mm | DERIVED |
| `ε_lag` | 0.5 mm | 0.1–2 mm | DERIVED |
| `δ_cbGap` | 1 mm | 0.5–3 mm | DERIVED from [R 1.5] |
| Stalemate trigger `N_stall` | 8 innings | 6–20 | DERIVED (game design) |
| Lag ball placement | `(x_HS − R − 0.01 m, ±W/4)` | — | DERIVED from [R 1.2] |

---

## 14. Open questions (need a product/rules decision or a primary-source check)

1. **10-ball winning moment** (§8.4): does "only object ball on the table" mean at shot start (default) or at the instant
   of pocketing? `TenOnlyBallMoment` CONFIG. A WPA clarification would settle it.
2. **8-ball illegal break combined with a foul** (§6.2): our combination of R 4.3(d) and (h) is an INTERPRETATION.
3. **8-ball R 4.3(f) "re-breaking"**: assumed the incoming player breaks.
4. **10-ball break continuation** (§8.2): the 2025 text does not say the breaker continues after pocketing a ball; assumed yes.
5. **14.1 accepting after a breaking foul with a scratch** (§9.2): CB in hand above HS assumed.
6. **Derived thresholds** `T_push`, `φ_graze`, `d_sep`, `ε_tie`, `ε_frozen`, `ε_leave` — tune with the cue-strike model and
   play-testing; document final values in the physics spec.
7. **Scoop policy**: intent cannot be judged; default WPA "miscue, no foul". Decide whether ranked mode uses `Foul`.
8. **Obvious-shot inference** (§4.5) vs explicit calls — which is the default per game mode (casual/ranked)?
9. **Spotting order for several balls** (14.1): ascending number assumed (BCA convention, WPA silent).
10. **Blackball rack colour pattern** is only in the WPA diagram, not in the text — needs the image from the PDF.
11. **APA / WEPF / IEPF details** come from secondary summaries; verify against current official manuals before shipping
    those presets.
12. **Shot clock "60 s" sentence** in Reg 18 is ambiguous (cap for the shot after the break assumed).
13. **Pocket/cushion id mapping** must be aligned with the table-geometry spec.
14. **Foot on floor / touched ball realism**: will the first-person avatar ever lift both feet or be allowed to touch balls
    (Sim mode)? If not, 3.4 is permanently N/A.
15. **Stalemate heuristic** (`N_stall`) is a game-design choice; PvP should rely on mutual agreement.
16. **9-ball three-ball rule scope** (added v1.1): [R 5.3(c)] ("if no ball is pocketed …") conflicts with [Reg 16(1)]
    (pocketed and crossing balls count together). Default Reg 16; `ThreeBallRuleScope` CONFIG. A WPA clarification would settle it.
17. **14.1 foul counters across a stalemate** (added v1.1): [R 7.12] carries the score over but is silent on consecutive-foul
    counts. Default: carry them over (no legal shot has intervened, [R 3.13]). Resetting them is the alternative.
18. **8-ball open-table 8-first exception** (added v1.1): we apply "no foul once a group is completely off the table" even
    without an explicit claim (literal [R 4.4]), and a win needs the claim (auto-set by calling the 8).

---

## 15. Sources

* WPA, *Rules of Play*, effective 2025-09-15 (PDF 2026-01-02):
  https://wpapool.com/wp-content/uploads/2026/01/2026.01.02-WPA-Rules.pdf — index page: https://wpapool.com/rules/
* WPA, *Playing Regulations*, effective 2025-09-15: https://wpapool.com/wp-content/uploads/2025/10/2025.09.15-WPA-Regs-NP.pdf
* WPA, *Recommended Equipment Specifications*: https://wpapool.com/wp-content/uploads/2024/01/RECOMMENDED-EQUIPMENT-SPECIFICATIONS.pdf
* WPA, *Rules of Play* version 2016-03-15 (previous edition, used only to separate real 2025 changes from old rules):
  https://www.wpapool.com/wp-content/uploads/2024/01/WPA_New_Rules_01MAR2016-fixed-spelling.pdf
* D. Alciatore ("Dr. Dave"), *Summary of WPA Rule Changes 2025-06-27*:
  https://drdavepoolinfo.com/resource_files/2025_WPA_Rule_Change_Summary.pdf and video "New WPA Official Rules of Pool":
  https://www.youtube.com/watch?v=qLpE_ulRsGM
* Dr. Dave FAQ pages: double hits https://drdavepoolinfo.com/faq/foul/double-hit/ ; push shots
  https://drdavepoolinfo.com/faq/foul/push/ ; frozen-ball fouls https://drdavepoolinfo.com/faq/foul/frozen/ ; cue-tip
  contact time (0.8–2.0 ms) https://drdavepoolinfo.com/faq/cue-tip/contact-time/
* AzBilliards forum discussion of the 2025 changes:
  https://forums.azbilliards.com/threads/new-wpa-official-rules-of-pool-%E2%80%A6-learn-about-all-the-changes.577065/
* pooltool ruleset API (prior art for rule helpers on top of an event-based simulator):
  https://pooltool.readthedocs.io/en/latest/autoapi/pooltool/ruleset/index.html
* W. Leckie, M. Greenspan, "An event-based pool physics simulator", *Advances in Computer Games* 11 (2005), LNCS 4250 —
  event taxonomy the `ShotRecord` builds on.
* APA official rules pages: https://rules.poolplayers.com/game-rules/breaking/ , https://rules.poolplayers.com/game-rules/after-the-break/ ,
  https://rules.poolplayers.com/game-rules/fouls/ , https://rules.poolplayers.com/game-rules/how-to-win-a-game/
* Wikipedia, "Eight-ball pool (British variation)": https://en.wikipedia.org/wiki/Eight-ball_pool_(British_variation) ;
  "Eight-ball": https://en.wikipedia.org/wiki/Eight-ball

---

## 16. Implementation notes & pitfalls

1. **Evaluate against the START state.** "Lowest ball", "group cleared", "on the 8", "only object ball left", "table
   open", "CB in hand above the head string" and the foul counters are all properties of the position before the stroke.
   Using the end state is the most common rules bug (e.g. the 8 and the last solid on one shot looks "cleared" at the end).
2. **Keep detection and enforcement separate.** Record every foul; enforce one (§4.9 severity). The HUD shows the enforced
   foul plus its rule reference; replays show all.
3. **Rails are more than cushions.** Jaws, liners, rail tops, being pocketed and leaving the table all count as "driven to a
   rail" [R 2.1, 2.7]. Frozen-to-rail balls need `continuesInitialFreeze` from the physics — do not guess it from distances.
4. **Tie windows only help the shooter.** They may convert a foul into a legal shot, never the reverse [R 3.2, 3.3, Reg 25].
5. **Spin counts as motion.** The shot (and the shot clock start) waits for spinning balls [R 2.19, Reg 18]. With an
   event-based simulator this is exact — do not add a velocity threshold on top.
6. **CB touching a pocketed ball = scratch**, even if it comes back out [R 2.2]; the physics must keep pocketed balls as
   colliders inside the pocket (or emit `BallTouchesPocketedBall`).
7. **Supported-over-pocket balls** are pocketed [R 2.2]; remove them before spotting and before the next shot.
8. **8-ball never spots** except the 8 after the break; balls driven off stay out and count as gone for group clearing.
   9-ball/10-ball spot only the money ball; 14.1 spots everything that was not scored.
9. **Push-out window is exactly one shot**; the three-ball-rule "accept" option closes it; "hand back" keeps it open for
   the breaker. A push-out can never win the rack (9/10 spotted).
10. **10-ball has no safety call**; the declaration validator must reject it. Wrongfully pocketed balls give the opponent a
    choice, they are not fouls and do not touch the foul counter (a legal shot resets it).
11. **14.1 bookkeeping:** extra balls on a scoring shot score; balls on fouls/safeties/misses are spotted; scores go
    negative; breaking fouls do not count toward three fouls; the −15 penalty re-racks all 15 and forces an opening break.
    Re-rack checks happen after spotting.
12. **Foul counters:** 9/10-ball reset at every new rack; 14.1 never resets between racks. Always display the counter
    (the display is the mandatory warning [Reg 8]).
13. **Spotting next to the CB** keeps `δ_cbGap`; next to object balls it is exact tangency — the physics must accept an
    initial frozen pair without penetration and treat it as touching (frozen) in the next shot.
14. **Deterministic racks:** seeded RNG per rack; seed stored in the replay and in the network state.
15. **Region predicates are strict.** Kitchen placement must be strictly above the head string; a ball *on* the string is
    playable from the kitchen; "above HS" for the 14.1 re-rack table is strict too.
16. **Validation before simulation:** call present/legal, safety/push-out allowed, CB placement region, claimed group
    really cleared. Invalid input is refused in the UI, not converted into a foul (except in "Sim" realism mode where free
    CB placement can produce foul 3.10).
17. **AI uses the same evaluator.** The planner simulates candidate shots with the same physics and calls
    `evaluateShot`, so it can never plan with different rules than the referee applies.
18. **Replays / versioning:** store `ShotRecord`, `ShotDeclaration`, `RulesConfig` hash and rules version with each shot so
    old replays re-evaluate identically after rule-engine updates.
19. **Event ordering:** sort by `(t, sequence)`; never compare event times for equality without the tie windows.
20. **Head-string crossing direction:** the CB must move from above to below (x increasing) for 3.11; OBs must move the other
    way for the three-ball rule. Use `BallLineCross` events, not end positions ([Reg 16] counts a ball that crossed and came
    back).
21. **Double hit / push need the cue as a physical body** that can touch the CB again after the first contact (follow-
    through). An impulse-only cue model cannot detect 3.7/3.8; the cue-strike spec must provide `TipContacts[]`.
22. **"Sim" vs "Assisted" input:** in Assisted mode the avatar/cue/bridge colliders never touch balls and foul 3.6/3.4/3.10
    cannot occur; in Sim mode they are live. Make the mode part of `RulesConfig` so replays are reproducible.
23. **Lag in one simulation:** both lag balls in the same world, one per side; the long-string check needs `y` crossing
    events for both.
24. **Group assignment happens after evaluation** of the current shot: the shot itself is judged with open-table rules.
25. **Stalemate in AI-vs-AI:** without the `N_stall` heuristic two safety-playing AIs can loop forever.
26. **Exact tangency in `SpotBall`** (added v1.1): in a 14-ball re-rack the two second-row balls are exactly `2R` from the
    empty apex (foot spot), so their blocked intervals start *exactly* at `x_FS` (`h = √3R`). In floating point
    `x_j − h_j` can round to just below `x_FS` and wrongly block the apex site. Compare with a tolerance: treat `x` as
    blocked only if `x_j − h_j + ε_line < x < x_j + h_j − ε_line` (test S21).
27. **3.11 also applies on the break** (added v1.1) in 8-ball and 14.1, because the break is played with the CB in hand
    above the head string. A break that neither crosses the head string nor touches a ball on or below it is a
    3.11 ¶2 foul on top of any illegal-break result (test E33).

---

## 17. Test cases

Conventions: 9-ft table, `R = 0.028575 m`, `x_HS = −0.635`, `x_FS = +0.635`; positions in m, times in s. Tolerances:
positions ±1e-6 m unless stated, counts/enums exact. "FC" = first contact (after tie resolution), "BIH" = CB in hand,
"A" = shooter, "B" = opponent. Unless stated: no foul other than the ones described, all balls at rest at shot start, CB
not in hand.

### 17.1 Derived facts and general rules (G)

| ID | Given | Shot facts | Expected |
|---|---|---|---|
| G01 | 9-ball, lowest = 3 | `BallBall(CB,7)` t=0.1000, `BallBall(CB,3)` t=0.1003 | tie (0.3 ms ≤ 0.5 ms) → FC = 3; no 3.2 foul |
| G02 | as G01 | `BallBall(CB,7)` t=0.1000, `BallBall(CB,3)` t=0.1010 | FC = 7 → foul 3.2, B gets BIH anywhere |
| G03 | 9-ball, lowest = 2 lying 5 mm off cushion C3 | `BallCushion(CB,C3)` t=0.25000, `BallBall(CB,2)` t=0.25030, no later rail contact, nothing pocketed | CB rail counted as after contact → no 3.3 foul; turn passes, CB in position |
| G04 | as G03 | CB cushion at t=0.24870 (1.3 ms before) | foul 3.3 (NoRailAfterContact) |
| G05 | 8-ball, A = solids; 5 frozen to C2 | FC = 5 at t=0.3; `BallCushion(5,C2,continuesInitialFreeze=true)` t=0.3001; no other rail; nothing pocketed | foul 3.3; B BIH anywhere |
| G06 | as G05 | additionally 5 separates 3 mm, then `BallCushion(5,C2,continues=false)` t=1.2 | legal; turn passes; CB in position |
| G07 | 9-ball, lowest 1 | FC = 1; 1 hits jaw of P2 (`BallJaw`) and stays out; no other rail | legal (jaw = rail); turn passes |
| G08 | 9-ball | CB enters P3, `BallTouchesPocketedBall(CB,…)`, rebounds onto cloth | CB pocketed → foul 3.1; B BIH anywhere |
| G09 | 9-ball, lowest 2 | FC = 2, rail OK; at rest ball 4 `SupportedOverPocket(P1, {6})` | 4 pocketed in P1 → A continues; 4 removed |
| G10 | 9-ball | ball 5 motionless on P0 lip at `tStop` = 6.0; drops at 9.0 | 5 pocketed on this shot |
| G11 | as G10 | drops at 11.5 (5.5 s after `tStop`) | not pocketed; 5 restored to its lip position (settling) |
| G12 | 9-ball, lowest 3 | FC = 3; ball 6 hits the lamp (`BallExternalContact(6,Lamp)`) and lands on the cloth | 6 off table → foul 3.5; 6 stays out of play (only the 9 is spotted); B BIH anywhere |
| G13 | any | ball 6 touches the rail top and falls back onto the cloth | not off table; counts as rail contact |
| G14 | any | `TipContacts = [0,0.0012], [0.0150,0.0158]` | foul 3.7 (double hit) |
| G15 | CB–OB gap 3 mm, not frozen | `TipContacts=[0,0.0014]`, `BallBall(CB,OB)` t=0.0006, φ = 5° | foul 3.7 |
| G16 | as G15 | φ = 82° | graze exemption → no 3.7 foul |
| G17 | CB frozen to lowest ball 1 (declared) | `TipContacts=[0,0.0018],[0.0031,0.0036]`, CB 2 mm from ball 1 at t=0.0031, no other contact yet | no 3.7/3.8 foul; FC = 1 |
| G18 | not frozen | `TipContacts=[0,0.0062]` (6.2 ms) | foul 3.8 (push shot) |
| G19 | 9-ball, `Scoop = WpaMiscue` | `TipClothContact`, CB airborne, `JumpedOver={5}`, FC = lowest 2, 2 pocketed | no foul; A continues |
| G20 | as G19 without scoop, `Discipline = Blackball` | CB jumps over 5 | foul (Blackball 8.13.3); under 9-ball WPA: legal |
| G21 | 9-ball, Sim mode | `NonTipContacts = {BridgeHand → ball 7}` during aiming | foul 3.6; B BIH anywhere |
| G22 | spot 9; nothing on the long string | — | 9 at (0.635000, 0) |
| G23 | spot 9; ball 5 at (0.635, 0) | — | 9 at (0.692150, 0) ±1e-9 |
| G24 | spot 9; ball 5 at (0.645, 0.020) | — | 9 at (0.698536, 0) (= 0.645 + √(0.05715² − 0.02²)) |
| G25 | spot 9; CB at (0.635, 0) | — | 9 at (0.693150, 0) (2R + 1 mm gap) |
| G26 | spot ball 3 (14.1); 11 balls at (0.635 + k·0.05715, 0), k = 0…10 | whole long string from foot spot to `L/2 − R` = 1.241425 blocked | 3 at (0.577850, 0) (touching the foot-spot ball on its head side) |
| G27 | CB in hand above HS, placed at x = −0.635 exactly | — | foul 3.10 (must be strictly above) |
| G28 | 14.1, CB in hand above HS at (−0.9, 0); ball 7 at (−0.8, 0.2) | FC = 7 without crossing; afterwards CB crosses HS; rail OK | foul 3.11 ¶1 only → −1; B plays CB **in position** |
| G29 | as G28 | CB never crosses HS after hitting 7 | foul 3.11 ¶2 → −1; B has BIH **above HS** |
| G30 | as G28 but ball 7 at (−0.635, 0.2) (on the HS) | FC = 7 directly | legal (ball on the head string is playable) |
| G31 | as G28 | CB crosses HS, banks off C2, returns, FC = 7 | legal (crossed before contact) |
| G32 | 14.1 after B scratched: A has BIH above HS; all OBs above HS: 3 at (−0.90, 0.10), 12 at (−0.80, −0.20) | A requests spot | 12 (nearest HS, 0.165 m) is spotted at (0.635, 0) |

### 17.2 Rack geometry and lag (K, L)

| ID | Given | Expected |
|---|---|---|
| K01 | 8-ball rack | 8 at (0.733987, 0); apex (0.635, 0) ≠ 8; back corners (0.832973, ±0.114300) hold one solid and one stripe |
| K02 | 9-ball, `NineOnSpot` | 9 at (0.635, 0); 1 at (0.536013, 0); last ball at (0.733987, 0) |
| K03 | 9-ball, `OneOnSpot` (LEGACY) | 1 at (0.635, 0); 9 at (0.733987, 0) |
| K04 | 10-ball rack | 1 at (0.635, 0); 10 at (0.733987, 0); back row x = 0.783480 |
| K05 | 14.1 outline | ball at (0.552133, 0) interferes (0.9 R from apex vertex 0.577850); ball at (0.546418, 0) does not (1.1 R) |
| K06 | 8-ball rack, 10 000 seeds | 8 always at (r2,k1); corners always one solid + one stripe; same seed → identical rack |
| L01 | lag: A rests at x = −1.27 + R + 0.050, B at d = 0.080, both good | A wins, chooses breaker |
| L02 | A touches side cushion C4; B good at d = 0.30 | B wins |
| L03 | A contacts C2 twice | A bad |
| L04 | both bad | re-lag |
| L05 | d_A = 0.0500, d_B = 0.0503 | difference 0.3 mm ≤ `ε_lag` = 0.5 mm → re-lag |
| L06 | A's ball crosses the long string | A bad |
| L07 | A's ball rests in P5 with x_rest − R = −1.2705 | A bad (past the head cushion nose) |

### 17.3 8-ball (E)

| ID | Given | Shot facts | Expected |
|---|---|---|---|
| E01 | break | nothing pocketed, 5 OBs to rail | Pass; B from position; table open |
| E02 | break | nothing pocketed, 3 OBs to rail | Decide(B): AcceptTable (CB in position) / RerackDeciderBreaks / RerackOffenderBreaks |
| E03 | break | nothing pocketed, 3 OBs to rail, CB scratched | Decide(B): same options; AcceptTable gives BIH above HS |
| E04 | break | 2 and 11 pocketed | A continues; table open (no group) |
| E05 | break | 8 and 4 pocketed, no foul | Decide(A): Spot8ContinueFromPosition / RerackDeciderBreaks |
| E06 | break | 8 pocketed + CB scratch | Decide(B): Spot8BallInHandAboveHeadString / RerackDeciderBreaks |
| E07 | break | 6 OBs to rail, CB scratch, nothing pocketed | foul; Pass(B, BIH above HS) (no "accept" — CB gone) |
| E08 | break | 13 off table, 5 OBs to rail | foul; 13 out of play; Decide(B): AcceptTable / BallInHandAboveHeadString |
| E09 | break | 8 off table, 5 OBs to rail | foul; 8 spotted per §4.3 (foot spot if free); Decide(B) as E08 |
| E10 | open | call 3→P2; FC = 12; 12 drives 3 into P2 | legal; A continues; A = solids |
| E11 | open | call 3→P2; FC = 8 | foul 3.2; B BIH anywhere; table stays open |
| E12 | open | call 3→P2; 3 misses; 11 drops in P4 | Pass(B, in position); table open; 11 stays down |
| E13 | open | call 3→P2; 3 in P2 but CB scratches | foul; B BIH anywhere; table open; 3 stays down |
| E14 | A = solids | FC = 10 | foul 3.2; B BIH anywhere |
| E15 | A = solids | call 5→P1; 5 drops in P4 | Pass(B); 5 stays down |
| E16 | A = solids | call 5→P1 made; 12 also drops | A continues; 12 stays down |
| E17 | A = solids, only 7 left | call 7→P0; 7 in P0, then 8 in P3 | A loses rack (R 4.8(b)) |
| E18 | A cleared | call 8→P5; 8 in P5 + scratch | A loses (R 4.8(a)) |
| E19 | A cleared | call 8→P5; 8 in P0 | A loses (R 4.8(c)) |
| E20 | A cleared | call 8→P5; 8 in P5, no foul | A wins rack |
| E21 | A = solids, not cleared | 8 driven off the table | A loses (R 4.8(d)) |
| E22 | A cleared | 8 missed; CB scratches | standard foul; B BIH anywhere; no loss |
| E23 | A = solids | safety call; 3 pocketed | Pass(B, in position); 3 stays down; no foul |
| E24 | A cleared | safety call; 8 drops | A loses (R 4.8(c)) |
| E25 | open; all stripes already off the table | A claims stripes; FC = 8; call 8→P3; made | A wins |
| E26 | as E25 | 8 missed, no foul | Pass(B); table still open |
| E27 | open, no group cleared | FC = 8, 8 pocketed | foul → A loses (R 4.8(a)) |
| E28 | A has 2 prior consecutive fouls | A fouls again (FC wrong group) | standard foul only; B BIH anywhere (8-ball has no 3-foul rule) |
| E29 | A = solids | FC = 3; no ball reaches a rail; nothing pocketed | foul 3.3; B BIH anywhere |
| E30 | A = solids, B = stripes, only 13 left for B | A's shot drives 13 off the table | foul; 13 stays out; next shot B is on the 8 |
| E31 | after a break foul, B has BIH above HS; ball 4 at (−0.90, 0.10) | FC = 4 directly without crossing HS | foul 3.11; A BIH anywhere |
| E32 | open; all stripes already off the table; A calls 3→P2, no claim | FC = 8; 8 and 3 not pocketed; 8 reaches a rail | **no foul** (R 4.4 exception); Pass(B, in position); table still open |
| E33 | break, CB in hand above HS | CB rolls slowly, stops above HS, touches no ball | foul 3.11 ¶2 + illegal break → Decide(B): AcceptTable (**BIH above HS**) / RerackDeciderBreaks / RerackOffenderBreaks |
| E34 | open; all stripes already off the table; A calls 3→P2, no claim | FC = 8; 8 drops in P3; no foul | A loses (R 4.8(b): the 8 was not claimed) |

### 17.4 9-ball (N)

| ID | Given | Shot facts | Expected |
|---|---|---|---|
| N01 | break | FC = 1; 3 pocketed; 5 OBs to rail; 3 balls crossed HS | A continues; push-out available |
| N02 | break | FC = 1; 9 and 5 pocketed; 2 others crossed HS | A wins rack |
| N03 | break | FC = 2; 6 pocketed | foul 3.2; B BIH anywhere; 6 stays down |
| N04 | break | nothing pocketed; 3 OBs to rail | foul (4-rail rule); B BIH anywhere |
| N05 | break | nothing pocketed; 6 OBs to rail; 2 crossed HS | three-ball rule fails → Decide(B): AcceptTableNoPushOut / HandBackPushOutAllowed |
| N06 | break | 9 pocketed; 1 other crossed HS; no foul | 9 spotted; Decide(B) as N05 |
| N07 | break | as N05 but a third ball stopped at x = −0.6349 (never beyond) | not counted → still 2 → N05 outcome |
| N08 | shot after legal break, push-out declared | CB hits nothing, no rail | legal; Decide(B): ShootFromPosition / PassBack |
| N09 | push-out | CB scratches | foul; B BIH anywhere; A's counter +1 |
| N10 | push-out | 9 pocketed | 9 spotted; Decide(B) |
| N11 | push-out | 4 pocketed | 4 stays down; Decide(B) |
| N12 | lowest 1 | FC = 1; 1 drives 9 in | A wins rack |
| N13 | lowest 1 | FC = 1; 9 pocketed; CB scratches | foul; 9 spotted; B BIH anywhere |
| N14 | lowest 3 | tie set {3, 5} | legal first contact |
| N15 | A on 2 fouls (HUD warned) | A fouls | A loses rack (R 5.8) |
| N16 | A on 2 fouls | A plays a legal miss | A's counter → 0 |
| N17 | lowest 2 | FC = 2; 9 driven off the table | foul 3.5; 9 spotted; B BIH anywhere |
| N18 | lowest 1 | FC = 1; 6 drops in some pocket | A continues (no calls in 9-ball) |
| N19 | third shot of the rack | push-out declaration | rejected by validator |
| N20 | A on 2 fouls; B wins the rack | new rack | both counters = 0 |
| N21 | `ThreeBallRule = off` | as N05 | Pass(B, in position); push-out available for B |
| N22 | after N05, B chose HandBack; A declares push-out; B then PassBack | — | A must shoot from position; no second push-out |
| N23 | break | FC = 1; ball 4 pocketed; 1 other ball crossed HS; 5 OBs to rail; no foul | `Reg16Combined` (default): count 2 < 3 → Decide(B): AcceptTableNoPushOut / HandBackPushOutAllowed; `OnlyIfNothingPocketed`: A continues, push-out available |

### 17.5 10-ball (T)

| ID | Given | Shot facts | Expected |
|---|---|---|---|
| T01 | break | FC = 1; 4 pocketed; rails OK | A continues; push-out available |
| T02 | break | 10 pocketed; legal | 10 spotted; A continues |
| T03 | lowest 2 | call 2→P1; made | A continues |
| T04 | lowest 2 | call 2→P1; 2 drops in P2 | Decide(B): ShootFromPosition / PassBack; 2 stays down |
| T05 | lowest 2 | call 2→P1 made; 10 also drops | 10 spotted; A continues |
| T06 | only 9 and 10 left | call 10→P3; FC = 9; 10 into P3; 9 stays | not a win (`ShotStart`); 10 spotted; A continues |
| T07 | only 10 left | call 10→P3; made | A wins rack |
| T08 | only 10 left | call 10→P3; 10 in P4 | 10 spotted; Decide(B) |
| T09 | lowest 3 | FC = 5; 10 pocketed | foul 3.2; 10 spotted; B BIH anywhere |
| T10 | A on 2 fouls | A fouls | A loses rack (R 6.10) |
| T11 | any | declaration `Safety` | rejected by validator |
| T12 | lowest 4 | call 4→P0; nothing pocketed | Pass(B, in position) |
| T13 | `TenOnlyBallMoment = AtPocketing`; only 9 and 10 left | call 10→P3; 9 drops at t=1.20, 10 in P3 at t=1.55 | A wins |

### 17.6 14.1 (S)

| ID | Given | Shot facts | Expected |
|---|---|---|---|
| S01 | opening break | call 5→P2; made | +1; A continues |
| S02 | opening break | no call made; CB, 3 and 11 to rail after rack contact; 7 dropped | 7 spotted; 0 pts; Pass(B, in position) |
| S03 | opening break | only 1 OB to rail | −2; A's counter unchanged; Decide(B): AcceptTable / RequireRebreak |
| S04 | opening break | only 1 OB to rail; CB scratched | −2 only; AcceptTable gives B BIH above HS |
| S05 | opening break | requirement met; CB scratched | −1; B BIH above HS; A's counter +1 |
| S06 | normal | call 3→P1 made; 7 and 9 also drop | +3; A continues |
| S07 | normal | call 3→P1 missed; 7 drops | 0; 7 spotted; Pass(B) |
| S08 | normal, A on 2 fouls | safety; 7 drops | 7 spotted; 0; Pass(B); A's counter → 0 |
| S09 | normal | no rail after contact | −1; CB stays; Pass(B) |
| S10 | A on 2 fouls, score 20 | A fouls | score 4 (−16); counter 0; all 15 re-racked; A takes an opening break (BIH above HS) |
| S11 | A score 0 | A fouls | A score −1 |
| S12 | scoring shot leaves one ball: 15th at (0.0, 0.3), CB at (−0.9, −0.2) | — | Rack14 (apex empty); both stay; A continues |
| S13 | 15th at (0.70, 0.05) (in rack), CB at (−0.2, 0.3) | — | 15th → head spot (−0.635, 0); CB stays |
| S14 | 15th at (0.70, 0.05), CB at (−0.64, 0.01) (0.0112 m from head spot) | — | 15th → center spot (0, 0) |
| S15 | CB at (0.75, 0.0) (in rack), 15th at (−0.2, 0.3) | — | CB in hand above HS; 15th stays |
| S16 | CB in rack, 15th at (−0.9, 0.3) | — | CB → head spot |
| S17 | CB in rack, 15th at (−0.66, 0.02) (0.032 m from head spot) | — | CB → center spot |
| S18 | CB and 15th both in rack | — | all 15 re-racked; CB in hand above HS |
| S19 | 14th and 15th legally pocketed on one shot; CB at (−0.2, 0.3) | — | +2; all 15 re-racked; CB stays; A continues |
| S20 | target 100; A at 98 | call made + 1 extra | A = 100 → match won immediately |
| S21 | right after a 14-ball re-rack (apex empty) | A fouls and pockets ball 6 | −1; 6 spotted on the empty apex (0.635, 0) |
| S22 | normal | FC = any ball, call made | legal (no 3.2 in 14.1) |
| S23 | CB in rack, 15th at (−0.635, 0.3) (on the HS) | — | CB in hand above HS (on the string counts as "on or below") |
| S24 | opening break, call 5→P2 | 5 in P2; CB scratches; only 1 other OB to a rail | requirement met by the called ball → **standard** foul: −1 (not −2); 5 spotted; B BIH above HS; A's counter +1 |
| S25 | after a stalemate re-lag, A is on 2 fouls (carried over) and takes the opening break | only 1 OB to a rail; CB scratches | breaking foul only: −2; A's counter **stays 2** (no third foul, no −15); Decide(B): AcceptTable (BIH above HS) / RequireRebreak |

### 17.7 Match flow, clock and variants (M, C, V)

| ID | Given | Expected |
|---|---|---|
| M01 | 9-ball race to 3, `Alternate`, A won the lag and breaks | breakers: rack 1 A, rack 2 B, rack 3 A, independent of rack winners |
| M02 | `WinnerBreaks` | winner of rack n breaks rack n+1 |
| M03 | 9-ball stalemate in rack 4 (breaker B), score 2–1 | rack replayed, B breaks, score stays 2–1 |
| M04 | 14.1 stalemate at 47–33 | new lag; opening break; score stays 47–33 |
| M05 | A concedes | B wins the match |
| M06 | race to 5 at 4–4, A wins the rack | MatchOver(A) |
| C01 | clock 35 s, no extension, tip contact at 35.2 s | foul 3.14 (standard foul; counts toward three fouls in 9/10/14.1) |
| C02 | extension used, tip contact at 50 s | no foul; a second extension in the same rack is refused |
| C03 | previous shot: last ball stops rolling at 4.0 s, stops spinning at 4.6 s | clock starts at 4.6 s |
| V01 | APA: 8 pocketed on the break, no foul | breaker wins |
| V02 | APA: 8 on the break + scratch | breaker loses |
| V03 | APA: shooting at the 8, 8 not pocketed, CB scratches | loss (APA); WPA: standard foul |
| V04 | APA: break pockets only solids 2 and 5 | breaker = solids |
| V05 | Blackball break: nothing potted, 1 OB crosses the center string / 2 OBs cross | foul / legal |
| V06 | Blackball: black potted on the break + scratch | re-rack, same breaker; scratch ignored |
| V07 | Blackball free shot: FC = opponent's ball | no 3.2 foul |
| V08 | `FoulScope = CueBallOnly`: bridge hand moves OB 7 | no foul (restore at B's option); moving the CB would be a foul |
| V09 | `LastPocketRule`: A's last solid went into P2; A calls 8→P0 and makes it | A loses (variant); WPA: A wins |

---

## 18. Verification log

**v1.1, 2026-09-25: adversarial check of v1.0 against the primary texts.**

**Method.** Both current WPA PDFs were fetched from wpapool.com/rules on 2026-09-25 (the index page still lists them as
the current editions), converted to text with `pdftotext -layout`, and compared clause by clause with §0–§12 and every
test in §17:
* *Rules of Play*, effective 2025-09-15, file `2026.01.02-WPA-Rules.pdf`, 44 pages.
* *Playing Regulations*, effective 2025-09-15, file `2025.09.15-WPA-Regs-NP.pdf`, 16 pages.

The 2016 WPA edition was also checked, only to find out which "2025 changes" are real changes. Dr. Dave's 2025 change
summary was used for cross-reference, and the APA pages for the APA rows. All rack, outline, spotting and 14.1 re-rack
numbers were recomputed (R = 0.028575 m); every value in §5 and §17 matched to 1e-6 m.

### 18.1 Confirmed without change (rule → spec location)

| Topic | WPA text | Verdict |
|---|---|---|
| 8 on the break, no foul → breaker: spot 8 and continue, or re-break | R 4.3(e) | ✓ §6.2, E05 |
| 8 on the break with **any** foul → opponent: spot 8 and BIH above HS, or re-break | R 4.3(f) | ✓ (2016 text: scratch only) |
| Illegal break (no OB pocketed and fewer than 4 OBs to a rail) → incoming player: accept / re-rack and break / re-rack and offender breaks | R 4.3(d) | ✓ E02 |
| OB off the table on the break → foul, ball stays out (8 spotted); incoming player: accept or BIH above HS | R 4.3(g) | ✓ E08, E09 |
| Other break foul → accept or BIH above HS (after a scratch, only BIH is possible) | R 4.3(h) | ✓ E07 |
| Table open after the break, balls made on the break never assign a group | R 4.3(c), 4.4 | ✓ E04 |
| Calls on the open table; the called ball decides the group | R 4.4 | ✓ |
| 8-ball loss: 8 made with a foul / before the group is cleared / in an uncalled pocket / off the table; not on the break | R 4.8 | ✓ E17–E24 |
| Scratch on the 8 without pocketing it = standard foul only | R 4.8, 4.9 | ✓ E22 |
| 8-ball: no three-foul rule; only the 8 is ever spotted | R 3.13, 4.7, 4.9 | ✓ E28, E30 |
| 9-ball rack: 1 at the apex, 9 in the middle **on the foot spot** | R 5.2 | ✓ K02 |
| 9-ball break: 4 OBs to a rail or foul; 3.2 applies (the 1 first) | R 5.3, 5.7 | ✓ N03, N04 |
| Three-ball rule: centre must go beyond the head string; accept = no push-out; hand back = breaker may push out; a 9 made on the failing break is spotted | Reg 16(1)–(5) | ✓ N05–N07, N22 (but see 18.3 D1) |
| Push-out: only after a foul-free break; 3.2 and 3.3 suspended; opponent chooses who shoots next; a 9 made on it is spotted | R 5.4, 5.6 | ✓ N08–N11 |
| Fouls: 9/10-ball → BIH **anywhere** (break fouls too); 14.1 scratch → BIH **above HS**; 8-ball break fouls → options above; 8-ball normal fouls → anywhere | R 4.9, 5.7, 6.9, 7.9 | ✓ |
| Three fouls: 9/10 loss of rack (fouls within one rack); warning required; a visible counter counts as the warning | R 3.13, 5.8, 6.10, Reg 8 | ✓ N15, T10 |
| 10-ball: every shot called except the break; no safety call; wrongful pocketing → the other player chooses who shoots; the 10 is spotted unless it is the winning shot; the 10 wins only as the last ball | R 6.5–6.8 | ✓ T02–T12 |
| 14.1: opening break needs the CB + 2 OBs to a rail after rack contact, or the called ball made; breaking foul −2; opponent accepts or makes the breaker re-break | R 7.3, 7.10 | ✓ (prose fixed, see C3) |
| 14.1: −1 per standard foul; third foul −1 −15, counter reset, full 15-ball re-rack and opening break by the offender; breaking fouls not counted | R 7.9, 7.11 | ✓ S10 |
| 14.1: spot balls pocketed on fouls, safeties and misses plus every ball driven off the table; extra balls on a scoring shot score | R 7.5–7.7 | ✓ S06–S08 |
| 14.1 re-rack cases (a)–(d) and Table 1 | R 7.8 | ✓ S12–S19, S23 |
| Jumped balls: 8-ball stays out (8 spotted on the break, otherwise loss); 9/10 only the money ball is spotted; 14.1 all spotted; Blackball order: black, then the next shooter's group, then the rest | R 4.7, 5.7, 6.9, 7.9, 8.11 | ✓ G12, E30, N17 |
| Spotting rule: long string, foot-rail side first, never touching the CB, then toward the head | R 1.5 | ✓ G22–G26 |
| Spot request when all legal OBs are above HS | R 1.6 | ✓ G32 |
| Lag conditions (a)–(h) | R 1.2 | ✓ L01–L07 |
| 5-second hanging ball; supported ball counts as pocketed; CB touching a pocketed ball counts as pocketed | R 2.2 | ✓ G08–G11 |
| Scoop / unintentional miscue treated as miscue or legal jump | R 2.11 | ✓ G19 |
| Shot clock 35 s, warning at 10 s, one 25 s extension per rack, starts when spinning stops, time-out = standard foul | Reg 18 | ✓ C01–C03 |
| Blackball break, free shot, open table, loss conditions, jump-over definition | R 8.5–8.14 | ✓ §12.4 |
| Equipment: 100 × 50 in, ball Ø 2.25 in ±0.005, 156–170 g, pocket mouths | Equipment spec §5, 9, 16 | ✓ |

### 18.2 Corrections made in v1.1

| # | Where | Error in v1.0 | Fix |
|---|---|---|---|
| C1 | §0 | Every item was presented as a 2025 change. Checking the 2016 PDF shows that calls on the open table, the 8-ball "no three-foul" rule and the 5-second hanging ball were already in the 2016 rules. The real 2025 changes are the cleared-group exception, "any foul" in R 4.3(f), the 9 on the spot, the head-string crossing test, scoop, tie presumptions, all-ball fouls and no 10-ball safety. | Items tagged (2025) or (since 2016); R 5.2 note added. |
| C2 | §6.3, §10.3 | On an open table, an 8 hit first was only legal with an explicit claim. R 4.4 literally makes it no foul once any group is completely off the table. | `IsLegalFirst` allows the 8 when a group is gone; a win still requires the claim, which calling the 8 sets automatically. Tests E32, E34. |
| C3 | §9.2 | "The called ball is **legally** pocketed". R 7.3(b) only needs the called ball pocketed, so called ball + scratch is a −1 standard foul, not a −2 breaking foul. | Prose fixed (the code was already right). Test S24. |
| C4 | §4.9 | The severity list ranked "third consecutive foul" above the 14.1 breaking foul. R 7.10/7.11: a shot with a breaking foul is only a breaking foul and never counts toward three fouls. | List reordered (the code was already right). Test S25. |
| C5 | §10.3 | 8-ball did not check 3.11 on the break, although the break starts in hand above HS and 3.11 is in the R 4.9 list. | 3.11 check moved before the break branch. Test E33, pitfall 27. |
| C6 | §10.3 | The 8's pocket was checked against `D.Called`, not the resolved call, so in `ObviousAssist` mode an obvious 8 with an inferred call was scored as a loss. | Uses `ResolveCall`. |
| C7 | §12.2 | The LEGACY entry said older WPA texts allowed the 8 as first contact on an open table and did not require calls there. The 2016 WPA text did neither (that is old BCA). | Rewritten; 2016 10-ball safety and 9-ball "touch" rule added. |
| C8 | §4.9 | 3.15 for 14.1 was marked "✗". It is listed in R 7.9 but cannot occur. | Wording. |
| C9 | §10.5 | `TenOnlyBallMoment = EarlyTenWins` was listed in §12.1 but never read by the code. | Handled in the code. |
| C10 | §16 | Spotting onto the empty apex of a 14-ball rack is an exact-tangency case that floating point can get wrong. | Pitfall 26 (tolerance). |

### 18.3 Remaining doubts (also in §14)

* **D1: 9-ball three-ball scope.** R 5.3(c) ("if no ball is pocketed") contradicts Reg 16(1), which counts pocketed
  balls toward the three. We chose Reg 16, as a CONFIG (`ThreeBallRuleScope`).
* **D2: 10-ball "only object ball" moment.** Shot start or pocketing instant? Unresolved. The 2016 wording ("as a final
  ball at the table") also fits the shot-start default.
* **D3: 10-ball break.** Neither the 2016 nor the 2025 text says the breaker continues after pocketing a ball; we assume
  he does.
* **D4: 8-ball R 4.3(f) "re-breaking".** Who breaks is not stated (the text is unchanged since 2016). We follow the BCA
  reading, where the opponent breaks. Offering both breakers is a possible alternative.
* **D5: 8-ball illegal break plus foul.** Combining R 4.3(d) and 4.3(h) is our interpretation. Accepting the table then
  gives BIH above HS.
* **D6: 14.1 accepting a breaking foul with a scratch.** BIH above HS assumed (consistent with R 7.9 3.1).
* **D7: 14.1 foul counters after a stalemate.** Carried over; R 7.12 is silent.
* **D8: 9-ball "1 on the spot" option.** It exists only in Dr. Dave's summary of the 2025-06-27 draft; the final R 5.2
  text does not mention it.
* **D9: variant presets.** APA rows were spot-checked against rules.poolplayers.com (8 on the break wins, 8 + CB foul
  loses, illegal break re-broken by the same player or by the opponent after a scratch, BIH behind the head string after
  a break scratch, group from a one-group break). WEPF and IEPF rows remain secondary-source only. WPA ch. 14 only links
  to the IEPF rules.
