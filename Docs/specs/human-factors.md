# RAW BREAK - Human Factors Spec: stroke execution, imperfections, equipment state, progression

| Field | Value |
|---|---|
| Spec ID | `human-factors` (prefix **HF**) |
| Modules | New engine-agnostic package `rb::human` inside `BilliardsCore` (work package **WP-10**, same coding rules as ARCH 2; no Unreal headers). Physics additions in WP-1/3/6 (listed in 3.11 and 4.5.7). Chores, animation and audio live in the UE game module. |
| Scope | Design principles; catalogue of human, equipment and venue imperfections; the deterministic stroke execution model (intended stroke -> `CueStrikeInput`); tip/chalk/cue/ball/table state models, including table tilt in the event-based physics; progression, difficulty and AI profiles; tests. |
| Builds on | MOT = physics-motion-and-cue (B.3-B.8), COL = physics-collisions (2.1 `k_cling`, 3.9.5 rack gaps), EQP = equipment (6-9), RUL = rules (3.2, F7-F10), UE = ue5-realism-plan (4.8, 5.3-5.5), ARCH = architecture (2, 5, 8, 11). |
| Status | Draft v1.0 (2026-09-26). Every number in section 6 was produced by an independent Python reference implementation of this text (hash, bags, stroke model, chalk model, tilt oracle, Monte Carlo). |

Tags: **[SRC]** sourced (section 8), **DERIVED** derived here, **TUNING** gameplay value to calibrate, **EST** estimate. Units SI, angles in radians in code (degrees only in prose). Frames and signs follow MOT 0: `+z` up, right English `a > 0`, cue frame `(e_r, d, e_u)`.

---

## 1. Design principles

1. **Visible cause.** Every imperfection acts on the avatar's hands, cue, tip, balls or table **before** the tip touches the cue ball, and it is drawn in first person (tip wander, bridge creep, bare tip, bowed cue, slow ball curling). Nothing random happens after contact. There is no dice roll on outcomes (make/miss, miscue yes/no, cling yes/no): the core physics decides from perturbed inputs and persistent state.
2. **Input first.** The player's mouse stroke (UE 5.4: aim, tip offset, speed, lateral steering, rhythm) is the main source of error. The avatar adds a thin **human layer** whose size is fitted to a routine-shot budget (3.10). Skill never straightens, smooths or rescales the player's input; that is an Execution assist (5.4).
3. **Deterministic seeded noise.** Noise is a pure function of `(MatchSeed, RackIndex, ShotIndex, ShooterId, Channel, Sub)` through a counter-based hash (3.2). No RNG state, no wall-clock. A replay stores the raw input log, the seeds and the final `CueStrikeInput`, and reproduces bit for bit (ARCH 5.3, 11).
4. **Same model for the AI.** The AI plans a stroke, a *synthetic hand* adds that profile's input flaws (3.8), and the result goes through the same `ExecuteStroke`, the same `CueStrikeInput`, the same simulator and the same rules. The AI never builds a `CueStrikeInput` directly (test HF-B07).
5. **Respect the player's time.** Chore duration is the sum of its physical steps (coins x 1 s, twists x 0.4 s, balls x walking). A partial job leaves partial state (the revolver rule: chalk only what is worn, rack only the balls you have, an aborted chore keeps what was done). Every chore has four speeds: **R** full interactive ritual, **A** hands do it automatically (hold to speed up 2-3x), **C** 1.5 s cut, **P** done while the opponent shoots. A skipped chore produces the character's *habitual* result (seeded, deterministic), so skipping never changes the outcome versus automatic play; only doing it interactively and well can beat the habit. Target overhead per 8-ball rack in the dive bar (TUNING): Full 45-90 s, Brisk 15-25 s, Minimal < 8 s. Saving is never restricted.
6. **Skill reduces but never removes human error.** Every noise channel follows `sigma(x) = sigma_25 * rho^((x-25)/75)` with `rho > 0` (5.1); at attribute 100 the noise is small but not zero. Equipment and table faults never scale with skill; experience shows up as habits (automatic chalking, rolling house cues) and knowledge (the notebook).
7. **Errors carry information.** Each channel produces at least a 1-pixel displacement at 1440p in the Eyes preset at 1 sigma (HF-B08), and every miss gets a counterfactual diagnosis within 3 s (3.9), delivered diegetically (mentor line, replay close-up, notebook).
8. **Fairness caps.** Per-shot draws are truncated at +-2.5 sigma and taken from stratified bags (any 8 consecutive shots contain each eighth of the distribution at most twice). No rubber-banding, no hidden streak breakers, no paid performance.
9. **Options and accessibility.** Sliders: Chores (Full / Brisk / Minimal), Imperfections (Sim / Scaled / Off = human-layer scale 1 / 0.6 / 0), Pressure (On / Subtle / Off), Diegetic-only information (On / Off), and the presets of 5.4. Accessibility options never lock career content; only ranked leaderboards group by preset.

---

## 2. Catalogue of details

Columns: **Skill** = the attribute (5.1) that shrinks it, H = habit, K = knowledge (notebook), P = Nerve, `-` = none. **Prio**: V1 = first playable dive-bar release, V2 = next update, Later. **Layer**: CP = BilliardsCore physics, HM = BilliardsCore player model `rb::human`, UG = Unreal gameplay/chores, UA = Unreal animation/camera/VFX, US = Unreal audio. Parameters are defaults; formulas are in sections 3-4.

### 2.1 Stroke and body (the human layer, section 3)

| ID | Detail - real mechanism | Player sees / hears / does | State | Skill | Prio | Layer | Parameters (default, unit, source) |
|---|---|---|---|---|---|---|---|
| HF-01 | **Grip-hand drift.** Slow wander of the grip hand; the cue pivots at the bridge, so the stroke line yaws and the tip shifts the other way; the resulting squirt partly cancels the yaw (natural pivot, 3.5) | Tip wanders across the cue ball in the DOF close-up; the player times the stroke or holds Settle | per shot (seeded process) | Steadiness | V1 | HM, UA | `sigma_drift` 0.9 mm at the grip (x=25) -> 0.18 mm (x=100), band 0.15-0.6 Hz, vertical 0.5x. TUNING, fitted to the routine budget 3.10 |
| HF-02 | **Tip-placement scatter.** The tip lands off the addressed point; off-center aiming is less precise ("English costs accuracy") | Tip settles a hair off the aimed spot during the final forward stroke (visible, not correctable, 3.7) | per shot | Spin Touch | V1 | HM | `sigma_A` 0.20 -> 0.05 mm, `sigma_B` 1.5 -> 0.30 mm, `kappa_off` 6 % -> 1.5 % of the aimed offset. EST from make-rate budgets [DD-MARGIN], [DD-SIDESPIN], [HAAR20] |
| HF-03 | **Speed inconsistency**, worst on soft shots | Cue speed in the last 100 ms; lags come up short or long | per shot | Speed Control | V1 | HM | `s_V` 5 % -> 1.5 %, x (1 + 0.5 max(0, 1 - V/1 m/s)). EST [DIGICUE] |
| HF-04 | **Unintended elevation** (elbow drop, vertical drift): draw/follow error; with side spin it adds swerve | Butt dips or rises at contact | per shot | Spin Touch | V1 | HM | `sigma_theta` 0.4 deg -> 0.1 deg. EST [DD-ELBOW] |
| HF-05 | **Physiological tremor** 8-12 Hz, amplified by adrenaline (why WADA bans beta-blockers in billiards) | Tip shiver in the focus plane; faint heartbeat under pressure | none | Nerve (gain) | V1 | HM, UA, US | `sigma_tr` 0.03 mm at rest, x g (g <= 5 at Nerve 25, <= 1.5 at 100). [UE 4.8], [WADA] |
| HF-06 | **Settle** (exhale and hold): sway and tremor drop 70 % for about 4 s, then get worse | Exhale audio, steadier view; hold key | none | player timing | V1 | HM, UA | ramp 1.2 s, hold 4 s, release 1.5 s, floor 0.3, after 1.15. [UE 4.8] |
| HF-07 | **Settle-in and long holds.** The first second down is unsteady; holding for more than 10 s tires the arm | Body and view settle after getting down | none | - | V1 | HM | `E(t) = 1 + exp(-t/0.8 s) + min(0.5, 0.03 max(0, t - 10 s))`. EST |
| HF-08 | **Rushed transition** (no pause at the back of the stroke) | Jerky backstroke | per stroke | input | V1 | HM | x(1 + 0.3 clamp(1 - T_pause/0.2 s)) on tip and speed channels. [DIGICUE] backstroke pause |
| HF-09 | **Jab / decelerating stroke** | Stabbing short finish | per stroke | input | V1 | HM | speed sigma x(1 + 0.5 clamp(-a_c/10 m/s^2)). [DIGICUE] |
| HF-10 | **Head lift before contact** (the one kinematic novice/expert difference found in stroke video) | View rises early; mentor: "Stay down." | per stroke | input (H with coaching) | V1 | UA -> HM | drift and lateral placement x1.5. [DD-STROKE24] |
| HF-11 | **Swoop.** Tip moving sideways at contact tilts the effective stroke direction, so the friction cone is exceeded at smaller offsets | Squeak, chalk puff, "safe" draw miscues | per stroke | input + Steadiness | V1 | CP (MOT B.4a, 3.11), HM | cone uses `d' = V d + v_r e_r + v_u e_u`. DERIVED: 5 cm/s at 1 m/s turns rho 0.48 into 0.523 (HF-T11) |
| HF-12 | **Bridge type and bridge slip.** Closed loop constrains best; open, rail, elevated and mechanical bridges less. Above a speed V_b the bridge slips | Hand pose; fingers creep, cue rattles on power shots | per stroke | Bridge Stability | V1 | HM, UA, US | `m_br0` closed 1.0 / open 1.2 / rail 1.4 / elevated 2.0 / mechanical 1.8; `V_b0` 6 / 4 / 3.5 / 2.5 / 3 m/s. EST |
| HF-13 | **Awkward stance** (stretch, one foot down, over a ball) | Strained body, back foot on its toe | per shot (IK) | Stance | V1 | UA -> HM | `m_st = 1 + d_s L` (x2 at x=25, x1.2 at 100). EST |
| HF-14 | **Grip tension and flinch under pressure**: the tip drops and the stroke decelerates | Knuckles whiten; stroke dies | per shot | Nerve | V1 | HM, UA | tip bias -1.5 mm x P x L_N; flinch <= 8 % -> 1 % speed. EST; pressure and gaze [WILLIAMS02] |
| HF-15 | **Pressure scalar** from stakes, game ball, hill, crowd, shot clock, run length (3.4) | Heartbeat, breathing, crowd ducking; no vignette by default | match state | Nerve | V1 | UG | weights 0.35 / 0.25 / 0.15 / 0.10 / 0.10 / 0.05. TUNING |
| HF-16 | **Fatigue** after 2 h+ | Heavier breathing, slower get-down | session | - | V2 | UG -> HM | `m_fat = 1 + 0.2 F`, `F = clamp((hours played - 2)/2, 0, 1)`. EST |
| HF-17 | **Sweaty or sticky bridge hand**, glove | Drag squeak in the bridge; wipe on the jeans | per night | H (glove, wipe) | V2 | UG -> HM | drift x(1 + 0.5 S), V_b x(1 - 0.3 S); glove: sweat term x0.2, V_b x1.15. EST |
| HF-18 | **Vision centre.** The sighting eye is off the cue line, giving a constant aim bias | Nothing obvious; found by a career drill | per character | K | V2 | UA | camera eye offset 0-15 mm, no noise channel (the bias is real parallax). [DD-VISION] |
| HF-19 | **Off-hand stroke** | Clumsy pose | per shot | - | Later | HM | all channels x2.5. EST |
| HF-20 | **Alcohol** | Blur, sway | session | - | Later | - | product-owner decision (section 7) |

### 2.2 Tip, chalk and cue (section 4.1-4.3)

| ID | Detail - real mechanism | Player sees / hears / does | State | Skill | Prio | Layer | Parameters |
|---|---|---|---|---|---|---|---|
| HF-21 | **Chalk coverage per tip zone.** Each hit strips chalk where the tip touched; friction at the actual contact sets the miscue limit | Blue fades where you hit; dry "click" instead of "thock" | per cue, saved | H | V1 | HM state, CP input `mu_tip` | `mu_fresh` 0.60, `mu_bare` 0.35; `n_c` 10 / 18 / 30 / 45 hits per grade. [DD-CHALK] counts x2, DERIVED 4.1 |
| HF-22 | **Chalking in proportion to wear** (fire 1, reload 1) | Twists: 1 per 15 % missing, 0.4 s each; drilling leaves the rim bare, sweeping covers it | partial chalking persists | H | V1 | UG, UA, HM | `eta_center` 0.6, `eta_ring` 0.12 (drill) -> 0.45 (sweep) |
| HF-23 | **Chalk cube.** The bar cube is dried and cupped; your own cube hollows over the career | Cup in the cube, paper edge | own cube saved; bar cubes per venue | H | V1 bar, V2 own | UG | bar cap 0.7; rim efficiency x(1 - 0.7 h) |
| HF-24 | **Flat dome and edge limit.** Contact sits `r_dome rho` from the tip axis; a flat or small tip reaches its edge before the friction limit, and gives less spin per aim offset | Tip profile visible when chalking | per cue, saved | H (shape) | V1 seeded house tips, V2 wear | HM | `rho_edge = (w_tip/2)/r_dome`: 0.601 nickel, 0.354 flat 18 mm. DERIVED |
| HF-25 | **Glazed tip** does not hold chalk | Shiny tip, chalk flakes | per cue | H (scuff) | V2 | HM | `N_glaze` 400 hits; retention x(1 - 0.5 G). EST [DD-TIPCARE] |
| HF-26 | **Mushroomed tip**: soft chalk-free rim over the ferrule | Lip on the tip | per cue | H (trim) | V2 | HM | 0.2 um per hit; `mu_rim` 0.30. EST |
| HF-27 | **Tip hardness** | Sharper click on hard tips | per cue | K | V2 | HM, US | `n_c` x0.7 hard / x1.15 soft. [DD-HARD] |
| HF-28 | **Loose or screw-on tip, cracked ferrule** | Buzz on every hit | per house cue | H (take another) | V2 | HM -> CP `e_tip` | `e_tip` -0.07. EST |
| HF-30 | **House-cue rack with seeded defects** (bow, weight 18-21 oz, tip) | Weight stamped on the butt; tip look; roll the cue on the cloth to see the wobble | per venue, same every visit | H, K | V1 | UG, HM | bow: 60 % < 0.5 mm, 30 % 0.5-2 mm, 10 % 2-5 mm. EST, [EQP 8.2] |
| HF-31 | **Warp orientation.** A bowed cue has a fixed tip-direction error that depends on how it is rolled in the hand; experienced players hold the bow up | Roll test; habit "bow up" | per pickup | K, H | V1 | HM | `gamma = 0.5 x 4 s_w / L`; yaw `gamma sin(chi)`, pitch `gamma cos(chi)`. DERIVED/TUNING 4.4 |
| HF-32 | **Blocked backstroke, short cues** (walls, stools, lamp) | Butt bumps; offer 48/52 in cue or mechanical bridge | per shot | - | V1 | UG (sweeps UE 5.5), HM | short cue: `V_i` capped at 0.8 x normal max. EST |
| HF-33 | **Shaft grime** | Drag; towel wipe 4 s | per night | H | V2 | HM | drift x(1 + 0.3 grime). EST |
| HF-34 | **Cue mass, LD shaft, break/jump cue** | Weight sticker, case | owned cues | K | V1 | CP (`CueSpec`) | [EQP 8], [MOT B.7] |

### 2.3 Balls, table, rack (section 4.3-4.6)

| ID | Detail - real mechanism | Player sees / hears / does | State | Skill | Prio | Layer | Parameters |
|---|---|---|---|---|---|---|---|
| HF-40 | **Chalk marks on the cue ball** tracked in the ball's body frame; a mark at the ball-ball contact raises friction ("kick") | Blue dots rotate with the ball; wipe with a towel at ball in hand (2-3 s) | per ball until wiped or faded | H (wipe) | V1 visual, V2 physics | HM marks, CP cling | `r_mark` 2.5 mm, `k_chalk` 2.5. [COL 2.1] (TP A.14) |
| HF-41 | **Dirty bar balls** | Dull, hazy balls | per venue | K | V1 | CP (`ClingFactor`) | `k_venue` 1.3 dive bar (TP A.14 gives 1.5 for dirty). EST |
| HF-42 | **Oversized heavy bar cue ball** (221 g, 60.3 mm): draws less, climbs rails, ghost-ball habit undercuts | "Can't draw it" | per table | K | V1 (physics exists) | CP, UG notebook | [EQP 6.2] |
| HF-43 | **Worn mismatched ball set** (155-167 g) | One odd ball | per venue | - | V1 | CP (`BuildBallSet`) | [EQP 6.3] |
| HF-44 | **Chipped ball** ticks on slow rolls | Tick per revolution | persistent | - | Later | CP | - |
| HF-50 | **Table tilt / roll-off.** In-plane gravity bends slow balls (exact model 4.5) | Slow balls curl near the end; coaster under a leg | per table, persistent seed | K (notebook) | V1 | CP | bar `|s|` 0.5-2.5 mm/m, pool hall <= 0.3, arena <= 0.1. EST [DD-ROLLOFF] |
| HF-51 | **Nap drift** on napped bar cloth | Slow balls drift toward the foot | per table | K | Later | CP | pseudo-slope 0.2 mm/m, `eta_n` 0.1. EST 4.5.6 |
| HF-52 | **Dead rail spot** | Short, soft rebound, hollow knock | per table | K | V2 | CP | `e_c` x0.6-0.8 over 10-20 cm. EST |
| HF-53 | **Worn lanes, spills, burns** (local rolling resistance) | Visible marks | persistent | K | Later | CP (needs spatial `mu_r`) | spill `mu_r` x3-5 in 10-20 cm. EST |
| HF-54 | **Night climate** (humid, cold) | Sticky feel, fogged windows | per night | K | Later | CP params | - |
| HF-55 | **Rack tightness from racking effort** | Light between balls; ask for a re-rack | per rack | H | V1 | UG -> CP `RackGapParams` | 4.6 |

### 2.4 Fouls from the body, and chores

| ID | Detail | Player sees / hears / does | State | Skill | Prio | Layer | Parameters |
|---|---|---|---|---|---|---|---|
| HF-60 | **Shaft, hand, sleeve or chalk touches a ball** (all-ball foul) | Replay shows the contact | - | H (sleeve tuck) | V1 | UG (mesh collision, authoritative in Sim mode), HM (shaft check for AI/rbsim, 3.6) | [RUL F10] |
| HF-61 | **Double hit / push** with near or frozen balls, from the executed follow-through | Foul call | - | K | V1 | CP (`ResolveTipRecontact`) | [RUL F7/F8], [ARCH 8.6] |
| HF-62 | **Scoop**: elevated tip digs into the cloth | Scuff sound | - | - | V1 | UA (cue-body model) | [RUL F9] |
| HF-63 | **Chalk left on the rail** becomes an outside object | Cube on the rail | per session | H | V2 | UG | [RUL F5] |
| HF-70 | **Pay per rack**: quarters one by one, push the slide, balls rumble | Clunk, rumble, pocket jingle | wallet | - | V1 | UG, US | $1.00-2.00; 1 s per coin + 2 s |
| HF-71 | **Balls locked after pocketing**; leftover balls rolled into pockets before the next rack | Loser clears the table | per game | - | V1 | UG | 1.5-3 s per ball |
| HF-72 | **Scratch**: cue ball travels the return, you walk to fetch it | Rolling sound in the return | - | - | V1 | UG, US | 2-6 s return |
| HF-73 | **Cue-ball separation faults** (object ball in the cue-ball chute, weak magnet) | Bartender with a key | per table | - | V2 | UG | rare, seeded |
| HF-74 | **Racking by hand** from the tray (only balls that came out); **spotting** one ball at a time | Hands on the triangle | per rack | H | V1 | UG | 15-25 s per 15 balls; 2-3 s per spotted ball |
| HF-75 | **Ball in hand placed by hand** | Hand near clusters | - | H | V1 | UG | [RUL F10/F11] |
| HF-76 | **Calls, 8-ball pocket marker, chalkboard score, quarters on the rail queue** | Gestures, voice line, coaster | per game | - | V1 | UG | [RUL 4.5] |
| HF-77 | **Opponent waiting spot** sometimes in your line; wave to move; hustler sharks (V2) | NPC behind the pocket | - | - | V1 / V2 | UG | - |
| HF-78 | **Lamp swing** after the butt hits it: moving shadows for 5-10 s | Swinging light | - | - | V2 | UA | - |
| HF-79 | **Drink on the rail** can be knocked over: stain and local friction | Glass, stain | persistent | H | Later | UG, CP | - |

---

## 3. Stroke execution model (`rb::human`, pure function)

### 3.1 Interface

```cpp
namespace rb::human {
enum class BridgeType : uint8_t { Closed, Open, Rail, Elevated, Mechanical };
enum class FloorSource : uint8_t { None, Rail, Ball };
struct IntendedStroke {          // from UE 5.4 input (player) or SyntheticHand (AI, 3.8)
  double Azimuth, Elevation;     // phi_i, theta_i [rad]
  double AxisOffsetA, AxisOffsetB; // A_i, B_i [1]: cue-AXIS offsets / R (MOT B.3)
  double Speed;                  // V_i [m/s] tip speed at contact (quadratic fit, UE 5.4)
  double TipVelocityRight, TipVelocityUp; // v_r,i, v_u,i [m/s] swoop from input steering
  double TimeDown;               // t_c [s] contact time since "down on the shot"
  double ForwardStart;           // t_fwd [s] start of the final forward stroke (rendering ramp, 3.7)
  double SettleStart = -1;       // t_s [s] since down on the shot; < 0 = no Settle
  double PauseDuration;          // T_pause [s] pause at the back of the final stroke
  double ContactAcceleration;    // a_c [m/s^2] tip acceleration at contact (< 0 = decelerating)
  bool HeadMovedBeforeContact;
};
struct ShooterAttributes { double Steadiness = 25, SpeedControl = 25, SpinTouch = 25,
                           BridgeStability = 25, Stance = 25, Nerve = 25; };   // [0, 100]
struct StrokeSituation {
  BridgeType Bridge; double BridgeLength = 0.20, BridgeToGrip = 0.80; // L_b, L_bg [m] (UE 5.3/5.4)
  double StanceDifficulty = 0;   // d_s [0, 1] from the IK solver (0 comfortable, 1 max stretch/one foot)
  double Pressure = 0, Fatigue = 0, Sweat = 0; bool Glove = false, OffHand = false;
  double ElevationFloor = 0; FloorSource FloorBy = FloorSource::None; int FloorBall = -1;
  bool ShortCue = false;
};
struct NoiseKey { uint64_t MatchSeed; uint32_t RackIndex, ShotIndex, ShooterId, ShooterShotIndex, CuePickupIndex;
                  uint32_t Purpose = 0; };      // Purpose != 0: AI rollout keys (never the match stream)
struct HumanParams { /* all defaults of 3.3 */ double NoiseScale = 1.0; uint32_t ChannelMask = 0; bool Stratify = true; };
struct ExecutedStroke {
  CueStrikeInput Strike;         // what the physics gets (MOT B.1 + TipTransverseVelocity, 3.11)
  double Rho, RhoEffective, MiscueLimit; TipZone Zone;  // Dome / Overhang / Ferrule (3.6)
  bool PredictedMiscue, DoubleHitRisk, PushRisk, OffsetClamped, ElevationClamped;
  FixedVector<NonTipContact, 4> ShaftContactCandidates; // core geometry check (3.6)
  Breakdown Channels;            // per-channel contributions, for replay and diagnosis (3.9)
};
ExecutedStroke ExecuteStroke(const IntendedStroke&, const ShooterAttributes&, const StrokeSituation&,
                             const TipState&, const CueBodyState&, const CueSpec&, const BallSpec& CueBall,
                             const Vec3& CueBallPosition, Span<const BallObstacle> OtherBalls,
                             const NoiseKey&, const HumanParams&);
HandPose SampleHand(/* same inputs */, double t);  // pose at time t for rendering (3.7)
}
```

`ExecuteStroke` has no side effects and allocates nothing. Equipment and table state is updated **after** the shot by `ApplyShotToEquipment` (4.1-4.3), never inside it.

### 3.2 Noise primitives (exact definitions)

```
Mix64(x)      = SplitMix64Next(copy of x)            (rb/Core/Random.h: add 0x9E3779B97F4A7C15, then mix)
HashKeys(k..) : h = 0x243F6A8885A308D3; for each key k_i (uint64, in order): h = Mix64(h XOR k_i)
U01(h)        = (h >> 11) * 2^-53
InvNorm(p)    = Acklam's rational approximation (no refinement step; |rel err| < 1.15e-9), through rb::Log/Sqrt
TruncNormal(u)= InvNorm(Phi_lo + u (1 - 2 Phi_lo)),  Phi_lo = Phi(-2.5) = 0.0062096653257761   ->  z in [-2.5, 2.5]
```

**Channels** (`ChannelId`): 1 DriftLat, 2 DriftVert, 3 TremorLat, 4 TremorVert, 5 TipA, 6 TipB, 7 Elevation, 8 Speed, 9 Flinch, 10 WarpRoll, 20-27 SyntheticHand (3.8). AI rollout calls add `Purpose << 32` to `ShooterId` in every key, so they never reuse match values and cannot peek at the shooter's real future noise. Diagnosis re-runs (3.9) use the match key itself with channels masked, so they see exactly the noise of the real shot.

**Stratified bags** (channels 5-9). Per shooter and channel, shot `n = ShooterShotIndex` uses bag `b = n / 8`, slot `j = n % 8`:

```
perm = [0..7]; for i = 7 down to 1: r = HashKeys(MatchSeed, ShooterId, Channel, b, 100 + i);
                                    k = ((r >> 32) * (i + 1)) >> 32;  swap(perm[i], perm[k])
u = (perm[j] + U01(HashKeys(MatchSeed, ShooterId, Channel, b, 200 + j))) / 8
epsilon = TruncNormal(u)      (Flinch uses u directly)
```

With `Stratify = false`: `u = U01(HashKeys(MatchSeed, RackIndex, ShotIndex, ShooterId, Channel, 0))`. Bags are explained on the in-game "How it works" page.

**Watchable processes** (channels 1-4) are band-limited sums of `K = 6` cosines, evaluable at any time in O(K):

```
f_k  = f_lo + (f_hi - f_lo) (k + U01(HashKeys(MatchSeed, RackIndex, ShotIndex, ShooterId, Channel, 2k))) / K
ph_k = 2 pi U01(HashKeys(MatchSeed, RackIndex, ShotIndex, ShooterId, Channel, 2k + 1))
D(t) = sqrt(2/K) SUM_k cos(2 pi f_k t + ph_k),   D'(t) = -sqrt(2/K) SUM_k 2 pi f_k sin(2 pi f_k t + ph_k)
```

Unit mean square; drift band 0.15-0.6 Hz (slow enough to watch and time), tremor band 8-12 Hz. `t` is time since "down on the shot" from the input log, so identical input timing gives identical noise.

### 3.3 Parameters and skill scaling

`L(x; rho) = rho^((clamp(x, 0, 100) - 25)/75)` (1 at x = 25, `rho` at x = 100, > 1 below 25 for weak AIs).

| Symbol | Default at x = 25 | rho (value at 100 / at 25) | Attribute | Notes |
|---|---|---|---|---|
| `sigma_drift` | 0.9 mm (grip lateral) | 0.2 | Steadiness | vertical drift = 0.5 x lateral |
| `sigma_tr` | 0.03 mm (tip) | - | Nerve via g | UE 4.8 tip wobble 0.05-0.2 mm |
| `sigma_A` | 0.20 mm | 0.25 | Spin Touch | lateral tip placement |
| `sigma_B` | 1.5 mm | 0.2 | Spin Touch | vertical tip placement |
| `kappa_off` | 0.06 of `|A_i| R`, `|B_i| R` | 0.25 | Spin Touch | offset-proportional term |
| `sigma_theta` | 0.4 deg | 0.25 | Spin Touch | unintended elevation |
| `s_V` | 5 % | 0.3 | Speed Control | soft-shot factor `1 + 0.5 max(0, 1 - V_i/(1 m/s))` |
| `phi_fl` | 8 % | 0.125 | Nerve | flinch speed loss at P = 1 |
| `g_max - 1` | 4 | 0.125 | Nerve | pressure gain `g = 1 + P (g_max - 1)` |
| `b_grip` | 1.5 mm | 0.125 | Nerve | tip drop at P = 1 |
| bridge excess `m_br0 - 1` | table HF-12 | 0.4 | Bridge Stability | |
| `V_b` | table HF-12 | x(1 + 0.6 (x - 25)/75) | Bridge Stability | x1.15 glove, x(1 - 0.3 S) sweat |
| stance excess | 1.0 | 0.2 | Stance | |

All values TUNING; the fit criterion is the routine budget 3.10. The 1-sigma values are those of the untruncated normal; the realized standard deviation of a truncated draw is 0.9546 sigma.

### 3.4 Situation multipliers

```
L_N     = L(Nerve; 0.125)
g       = 1 + 4 P L_N                                   pressure gain (P = pressure in [0, 1])
m_br    = 1 + (m_br0(bridge) - 1) L(BridgeStability; 0.4)
V_b     = V_b0(bridge) (1 + 0.6 (clamp(BridgeStability,0,100) - 25)/75) (Glove ? 1.15 : 1) (1 - 0.3 Sweat)
m_slip  = 1 + 0.5 max(0, V_i - V_b)/V_b                 bridge slip on power shots
m_st    = 1 + d_s L(Stance; 0.2)
m_head  = HeadMovedBeforeContact ? 1.5 : 1
m_stick = 1 + 0.5 Sweat (Glove ? 0.2 : 1)
m_fat   = 1 + 0.2 Fatigue;   m_off = OffHand ? 2.5 : 1
m_rush  = 1 + 0.3 clamp(1 - T_pause/0.2 s, 0, 1)
m_jab   = 1 + 0.5 clamp(-a_c/(10 m/s^2), 0, 1)
E(t)    = 1 + exp(-t/0.8 s) + min(0.5, 0.03 max(0, t - 10 s))            settle-in and long holds
k_set(t): tau = t - t_s; 1 if t_s < 0 or tau < 0; 1 - 0.7 SmoothStep01(tau/1.2 s) for tau < 1.2 s;
          0.3 for tau < 5.2 s; 0.3 + 0.85 SmoothStep01((tau - 5.2 s)/1.5 s) afterwards (ends at 1.15)
```

**Pressure** (UG computes it, same formula for the AI): `P = clamp(0.35 stakes + 0.25 gameBall + 0.15 hill + 0.10 crowd + 0.10 clock + 0.05 run, 0, 1)`, with stakes 0 practice / 0.3 friendly / 0.6 money or league / 1 final; gameBall = 1 when the shot can win the rack; hill = 1 when either player needs one rack; crowd = min(1, watchers/10); clock = elapsed/limit when a shot clock runs; run = min(1, run length/8). Hot-seat can switch pressure off for both players.

### 3.5 Executed stroke (in this order)

```
R = CueBall.Radius;  L_bc = L_b + R           bridge to cue-ball centre along the cue
x_St, x_ST, x_SC = Steadiness, SpinTouch, SpeedControl;  NS = HumanParams.NoiseScale (0 for masked channels)
t = t_c

# watchable channels (drawn continuously, 3.7)
sigma_dr = sigma_drift L(x_St; 0.2) E(t) k_set(t) sqrt(g) m_br m_st m_head m_stick m_slip m_fat m_off
y_g  = NS sigma_dr D_lat(t)            grip lateral offset, + = shooter's right (e_r)
z_g  = NS 0.5 sigma_dr D_vert(t)       grip vertical offset, + = up
sigma_t = sigma_tr g k_set(t) m_fat m_off
tr_r = NS sigma_t T_lat(t);  tr_u = NS sigma_t T_vert(t)      tip tremor [m]

# per-shot channels (bagged, truncated)
sA  = hypot(sigma_A L(x_ST;.25), kappa_off L(x_ST;.25) A_i R) m_st m_rush m_head m_fat m_off
sB  = hypot(sigma_B L(x_ST;.2),  kappa_off L(x_ST;.25) B_i R) m_st m_rush m_fat m_off
sTh = sigma_theta L(x_ST;.25) m_br m_st m_slip m_fat m_off
sV  = s_V L(x_SC;.3) (1 + 0.5 max(0, 1 - V_i/1)) m_rush m_jab sqrt(g) m_fat m_off
fl  = NS P phi_fl L_N u_Flinch;    bias = -NS b_grip P L_N

# warp (equipment, not scaled by NS), section 4.4
gamma = 0.5 * 4 s_w / L_cue;  chi = WarpKnown ? 0 : 2 pi U01(HashKeys(MatchSeed, ShooterId, 10, CuePickupIndex))
dphi_w = gamma sin(chi);  dth_w = gamma cos(chi)

# pivot geometry (UE 5.4 steering, DERIVED): grip right -> cue rotates CCW about the bridge -> tip moves left
yaw = y_g / L_bg;  pitch = z_g / L_bg
phi_x   = phi_i + yaw + dphi_w
theta_x = max(ElevationFloor, theta_i + pitch + NS eps_El sTh + dth_w)      (ElevationClamped if the max bites)
A_x = A_i + (-yaw L_bc + tr_r + NS eps_A sA) / R
B_x = B_i + (-pitch L_bc + tr_u + NS eps_B sB + bias) / R
V_x = clamp(V_i (1 + NS eps_V sV) (1 - fl), 0, 12 m/s)
v_r = v_r,i - NS sigma_dr D_lat'(t) L_bc/L_bg + NS sigma_t T_lat'(t)
v_u = v_u,i - NS 0.5 sigma_dr D_vert'(t) L_bc/L_bg + NS sigma_t T_vert'(t)
```

The envelope factors (`E`, `k_set`) are treated as constant when differentiating. **Natural pivot (DERIVED, MOT B.7):** a yaw `delta` shifts the contact offset by `a = -delta L_bc/(R + r_tip)`, whose squirt turns the ball back by `(d alpha_sq/da) a`, so the net cue-ball direction error is `delta (1 - L_bc/L_p)` with `L_p = (R + r_tip)/(d alpha_sq/da)|_0`, `d alpha_sq/da|_0 = 2.5/(3.5 + m/m_e)`: 0.290 m house cue (m/m_e = 15), 0.368 m standard (20), 0.682 m LD (40). At the default `L_bc` = 0.229 m a house cue cancels 79 % of the drift yaw (HF-T09). This is real back-hand-English behaviour and falls out without extra code.

### 3.6 Tip contact, friction and flags

```
(a, b) = (A_x, B_x) R/(R + r_dome)                      AimToContactOffset (MOT B.3) with the CURRENT dome radius
rho = hypot(a, b); if rho > 0.90: scale (a, b) to rho = 0.90, OffsetClamped = true   (stays below kCueOffsetValidLimit)
q     = r_dome rho / (w_tip/2)                          contact radius on the tip / usable tip radius
beta  = atan2(-b, -a)                                   the tip side opposite the offset, seen from behind the cue
Zone  = Dome if q <= 1; Overhang if q <= 1 + overhang/(w_tip/2); else Ferrule
mu    = Dome: mu_bare + (mu_fresh - mu_bare) c(q, beta)  (4.1);  Overhang: mu_rim = 0.30;  Ferrule: 0.20
Strike.Cue.TipFriction = Strike.Cue.TipFrictionKinetic = mu;  Strike.Cue.TipDomeRadius = r_dome;
Strike.Cue.TipRestitution = e_tip(tip state);  Strike.{Speed, Elevation, Azimuth, OffsetA, OffsetB} = (V_x, theta_x, phi_x, a, b)
Strike.TipTransverseVelocity = (v_r, v_u)
RhoEffective = sin(psi_eff), cos(psi_eff) = (-a v_r + c V_x - b v_u)/sqrt(V_x^2 + v_r^2 + v_u^2),  c = sqrt(1 - rho^2)
PredictedMiscue = RhoEffective > MiscueLimit = mu/sqrt(1 + mu^2)        (prediction; the core decides, MOT B.4a)
DoubleHitRisk   = SeparationMargin(e_tip, m/M, rho) < 0  or  gap to the nearest ball ahead < CueSpec::FollowThroughDistance
PushRisk        = gap to the nearest ball ahead <= RulesTolerances::FrozenEnvelope (5 mm)
ShaftContactCandidates: UE 5.5 clearance test (tapered cue r(s) = r_t + (r_b - r_t) s/L, no margin) of the EXECUTED
                  pose against OtherBalls: s* <= 0.025 m Ferrule, <= 0.74 m Shaft, else Butt; plus FloorBall if
                  ElevationClamped by a ball.  UE mesh collision stays authoritative in Sim mode (RUL F10).
```

A flat or small tip therefore miscues at its edge before the friction limit, and a mushroomed rim or a bare zone lowers `mu` exactly where the tip touches. **Miscues are never rolled**: they come from `rho`, swoop, the chalk map and the tip geometry.

### 3.7 Visibility and rendering

`SampleHand(t)` returns the grip-hand offset `(y_g(t), z_g(t))`, tip tremor, the warp pose and the ramped per-shot offsets, evaluated with the same functions, so **what you see is what hits**. Per-shot channels (tip placement, elevation, speed, flinch, grip bias) ramp in as `SmoothStep01((t - t_fwd)/0.1 s)` from the start of the final forward stroke `t_fwd`: visible in the rendered cue and in replays, too late to correct with the mouse. The 1-euro filter stays visual-only (UE 5.4). Tremor at 8-12 Hz is rendered as sampled; aliasing is acceptable.

### 3.8 AI: synthetic hand (input layer) + the same human layer

The planner outputs `(phi_p, theta_p, A_p, B_p, V_p, bridge)`. `SyntheticHand(profile, NoiseKey)` produces the `IntendedStroke` with the profile's input flaws, drawn from channels 20-27 (bagged like 3.2):

```
phi_i = phi_p + bias_aim + sigma_aim eps20                   bias_aim: constant per character (vision centre; sign and size seeded, |bias_aim| <= profile value)
y_s   = sigma_steer eps21;  phi_i += y_s/L_bg;  A_i = A_p - y_s L_bc/(L_bg R)      (steering through the pivot)
v_r,i = -(y_s V_p / L_stroke) L_bc/L_bg,  L_stroke = 0.15 m;   B_i = B_p;  theta_i = theta_p
V_i   = V_p (1 + sigma_spd eps22);  T_pause = pause_mean (0.7 + 0.6 U23);  a_c = U24 < p_jab ? -5 : 0
t_c   = 1.5 + 1.5 U25 s;  t_s = (uses Settle and P > 0.4) ? t_c - 2 s : -1;  HeadMoved = U26 < p_head
```

`U_c = U01(HashKeys(MatchSeed, RackIndex, ShotIndex, ShooterId, c, 0))`. Then `ExecuteStroke` runs with the AI's own attributes and pressure. Profile values are in 5.5. Weak AIs plan with a simplified model (no throw, squirt, swerve, tilt, nominal ball masses) and assume perfect execution; strong AIs score candidates over `K` samples of their own noise using `Purpose = Rollout` keys ("percentage play").

### 3.9 Counterfactual diagnosis (after every shot)

A shot costs about 100 us (ARCH 1), so the game re-runs the missed shot up to 5 times with the same `NoiseKey`, a `ChannelMask` and state overrides, in this fixed order, and names the **first** change that turns the miss into a make: (1) fresh chalk and no warp -> "equipment"; (2) drift and tremor off -> "hand drift / nerves"; (3) per-shot channels off -> "tip placement / speed"; (4) all human channels off -> "human layer"; otherwise "input" (the player's aim, steering or speed). Output drives the mentor line (at most one per 3 shots, never during the opponent's turn), the replay close-up and, in Practice or Assisted, a stroke report such as "input 70 % / hand 20 % / chalk 10 %" (shares from the norms of the `Breakdown` contributions).

### 3.10 Routine-shot budget (release criterion) and fit

**Routine shot**: cue ball <= 1 m from the object ball, object ball <= 1 m from the pocket, cut <= 30 deg, medium speed, tip within 0.1 R of centre, comfortable closed bridge. With **perfect input** (no steering, exact aim) the human layer may cause at most:

| Case | Budget | Budget-model result (HF-S04..S06) |
|---|---|---|
| B1: P = 0, no Settle, attributes 25 | <= 3 % | 1.06 % (CB direction sigma 0.041 deg) |
| B1: P = 0, attributes 100 | <= 0.3 % | 0.00 % (0.0115 deg) |
| B2: P = 1, Settle held at contact, 25 | <= 3 % | 2.39 % |
| B3: P = 1, no Settle, 25 | <= 10 % | 8.63 % |
| Info: attributes 10 (tourist AI) | - | 6.72 % |
| Info: elevated bridge, stance 0.5, 25 | - | 16.8 % |

Budget model: the net cue-ball direction error is `phi_x - phi_i + alpha_sq(a)` (house cue, m/m_e = 15); the pot fails when `|error| d_CO / (2 R cos 30 deg)` exceeds 2.0 deg (bar corner pocket at 1 m, effective half-window [DD-POCKET]). The release test **HF-B02** re-checks the budget with the full core (squirt, throw, swerve, cushions) over 10^5 shots per profile. The same model gives the miscue rate on a maximum draw (`b` aimed at -0.45, fresh chalk, V = 2 m/s): 15.2 % at attribute 10, 7.8 % at 25, 2.7 % at 40, 0 % from 60 up (HF-S05). **Misses from the human layer shrink with skill but never reach exactly zero on hard shots** (long thin cuts, awkward bridges).

### 3.11 Core contract changes (sign-off per ARCH 2.8)

| Change | Owner | Effect on existing tests |
|---|---|---|
| **MOT B.4a** `CueStrikeInput::TipTransverseVelocity` (Vec2, `e_r`/`e_u` components, m/s, default 0). Grip criterion `sin(psi_eff) <= rho_max` with `cos(psi_eff) = d'.n`, `d' = normalize(V d + v_r e_r + v_u e_u)`; the miscue branch builds `t_hat` from `d'` instead of `d`; everything else unchanged | WP-1 | none (default 0) |
| `CueSpec` fields are filled per stroke from `TipState` (`TipFriction`, `TipDomeRadius`, `TipRestitution`) | WP-10 | none |
| New package `rb/Human/{NoiseHash.h, HumanModel.h, TipState.h, CueState.h, Progression.h}`; depends on Core, Math, `Physics/CueStrike.h`, `Equipment/Cue.h`; never included by the event loop or by rules | WP-10 | - |
| `SyntheticHand` + static check "AI builds strikes only via ExecuteStroke" (HF-B07) | WP-10 / AI | - |

---

## 4. Equipment and table state models

All state is plain data, serialized in the career save and in every replay header. `ApplyShotToEquipment(StrikeResult, ShotResult, ...)` updates tip and ball state after each shot; chores update it through the functions below. Every function is deterministic.

### 4.1 Tip chalk map -> `mu_tip`

**Zones.** 7 coverage values `c_z` in [0, 1]: zone 0 = centre disc, zones 1-6 = outer ring sectors centred at tip angles `beta_k = 60 deg (k - 1)` (measured on the tip face from `+e_r` toward `+e_u`, seen from behind the cue).

**Lookup** at a contact `(q, beta)` (3.6):

```
w_r = SmoothStep01((q - 0.2)/0.3);   s = beta/(pi/3);  s = s - 6 floor(s/6);  k0 = floor(s);  f = s - k0
c(q, beta) = (1 - w_r) c_0 + w_r [(1 - f) c_{1+k0} + f c_{1+((k0+1) mod 6)}]
weights: omega_0 = 1 - w_r, omega_{1+k0} += w_r (1 - f), omega_{1+((k0+1) mod 6)} += w_r f
mu = mu_bare + (mu_fresh - mu_bare) c                  mu_fresh = 0.60 [MOT B.4], mu_bare = 0.35 (TUNING, no primary data)
```

**Wear per hit** (after the strike):

```
severity = (0.5 + 0.25 V/(1 m/s)) (0.6 + 2 rho^2) (Miscue ? 3 : 1)      = 1.005 at V = 2 m/s, rho = 0.45
c_z <- c_z exp(-omega_z severity / n_c_eff),   n_c_eff = n_c(grade) h(hardness) (1 - 0.5 glaze)
```

**Calibration (DERIVED).** Chalking once and then hitting the same zone at `rho = 0.45`, the first miscue comes when `mu(c) < 0.45/sqrt(1 - 0.45^2) = 0.5039`, i.e. `c < 0.6156`, after `0.485 n_c` hits. Dr. Dave's no-rechalk test at near-maximum English counted Taom 4-6, Master 6-13, Predator 8, Blue Diamond 11, Kamui 15-17 and Magic Chalk up to 29 shots [DD-CHALK]. Fictional grades:

| Grade (fictional) | `n_c` | First miscue at rho 0.45, V = 2 m/s, no re-chalk | Reference | Unlock |
|---|---|---|---|---|
| "Rail Rat" bar cube (dried, cupped) | 10, cap 0.7 | 3rd hit (a fresh chalking only reaches c = 0.7, rho_max 0.465) | below Taom | free in bars |
| "Old Blue" standard | 18 | 10th hit | Master 6-13 | start |
| "Tensile" premium | 30 | 16th hit | Blue Diamond / Kamui | cash |
| "Glasshouse" elite | 45 | 23rd hit | Magic Chalk range | cash + reputation |

Premium chalk never raises `mu` above `mu_fresh`; it only slows the decay. Hardness factor `h`: soft 1.15, medium 1.0, hard 0.7 [DD-HARD].

**Chalking (the revolver rule).** One twist: `c_0 <- c_0 + max(0, cap - c_0) eta_0`, ring zones `c_z <- c_z + max(0, cap - c_z) eta_r`, with `eta_0 = 0.6 (1 - 0.5 glaze)`, `eta_r = (0.12 + 0.33 H_chalk)(1 - 0.5 glaze)(1 - 0.7 hollow)`; `cap` = 1 (own cube) or 0.7 (bar cube). `H_chalk` in [0, 1] is the chalking habit (sweep vs drill). In R mode the player's motion sets the twists and `eta_r` (sweep coverage measured from the input); in A/C/P mode the avatar does `n_tw = max(1, ceil((1 - min_z c_z)/0.15))` twists at `0.4 (1 - 0.3 H_chalk)` s each. Aborting keeps the twists already done. Dropped or left chalk is HF-63.

### 4.2 Tip condition

```
TipState { cov[7]; r_dome [m] (nickel 0.0106, dime 0.00896); w_tip [m]; height [m]; glaze G in [0,1];
           overhang o [m]; hardness {Soft, Medium, Hard}; e_tip; loose flag; hits }
per hit:   r_dome += 1.0e-6 m * severity (cap 0.025);  G += (1 - G) severity / N_glaze (400; hard 250, soft 600);
           o += 2e-7 m * severity * (soft 1.5 / medium 1 / hard 0.4)
scuff  (5-8 s): G *= 0.2; height -= 0.02 mm        shape (20-40 s): r_dome = target; height -= 0.1 mm
trim   : o = 0                                     retip (career days): new tip, height 6 mm, glaze 0.3 until 50 hits
edge limit: rho_edge = (w_tip/2)/r_dome   (12.75 mm nickel 0.601, dime 0.711, flat 18 mm 0.354; 11 mm flat 0.306)
loose tip / screw-on / cracked ferrule: e_tip -= 0.07
```

All rates EST (TUNING). A flatter dome also gives less spin per aim offset: `R/(R + r_dome)` falls from 0.729 (nickel) to 0.614 (18 mm), -16 %.

### 4.3 Chalk marks on balls -> per-contact cling

```
ChalkMark { Vec3 BodyDir (unit, ball body frame); double Strength in [0,1]; double Radius [m] }   <= 8 per ball
deposit at each tip contact: BodyDir = body-frame direction of Q (MOT B.3); Strength = 0.3 + 0.7 c(q, beta);
                             Radius 2.5 mm (miscue: Strength 1, Radius 4 mm)
fade per shot: Strength *= exp(-d_slide/0.5 m - d_roll/20 m)   (distances of that ball from ShotResult); drop < 0.05
wipe chore: all marks removed (2-3 s towel animation)
cling at a ball-ball contact (V2 physics): for each ball i, delta_ij = angle between mark j and the contact
   direction in ball i's body frame; chi_i = SUM_j Strength_j exp(-(R_i delta_ij / Radius_j)^2)
   k_cling,contact = k_venue (1 + (k_chalk - 1) min(1, chi_1 + chi_2)),  k_chalk = 2.5 [COL 2.1, TP A.14]
```

A random orientation puts a single 2.5 mm mark on the contact with an expected weight of 0.19 % (DERIVED, = `r^2/(4R^2)`); straight follow shots keep the mark on the travel great circle, so it comes back far more often, as players report [DD-CLING]. **Core additions (V2):** `SimBall::ChalkMarks`; the simulator integrates orientation of marked balls inside the loop (move `IntegrateSegmentOrientation` from Playback into WP-1); `BallBallParams::ClingFactor` becomes `k_venue`; switch `PhysicsParams::ChalkCling` (default off, so all COL tests are unchanged).

### 4.4 House cues and warp

Each venue seeds its wall rack once (`HashKeys(VenueSeed, rackSlot, ...)`): mass U{18, 19, 20, 21} oz; length 57 in (plus 48 and 52 in short cues near walls); bow `s_w` from the distribution in HF-30, direction random; tip: `w_tip` 11-13 mm, `r_dome` 12-20 mm, glaze 0.3-0.9, overhang 0-1 mm, 10 % loose; `m/m_e` = 15; `e_tip` 0.68-0.72 [EQP 8.2]. The same cue in the same slot has the same defects on every visit until the bar "replaces" it (career event).

**Warp (DERIVED/TUNING).** A circular bow of sag `s_w` over length `L` has end slopes `4 s_w/L` relative to the chord. With the bridge near the tip, half of that reaches the stroke (`k_w = 0.5`, TUNING): `gamma = 2 s_w/L`. Bow direction `n_w = cos(chi) e_u + sin(chi) e_r`; the tip end points toward `-n_w`, so `dphi_w = gamma sin(chi)` (toward the left for `chi = +90 deg`) and `dth_w = gamma cos(chi)`. A 3 mm bow on a 57-in cue gives `gamma` = 0.237 deg. Holding the bow up (`chi = 0`) turns the whole error into elevation, the trick experienced players use. `WarpKnown` becomes true when the shooter has roll-tested that cue (habit or R-mode test); otherwise `chi` is fixed per pickup by channel 10.

### 4.5 Table tilt and roll-off in the event-based physics

#### 4.5.1 Model

`TableTilt.Slope = s = (s_x, s_y)` = gradient of the bed height in the core frame (dimensionless, 1 mm/m = 1e-3). The core frame stays attached to the bed. Gravity in that frame is `(-g s_x, -g s_y, -g)` to first order: the in-plane part `g_t = -g s`; the normal part stays `g` (neglected factor `1/sqrt(1+|s|^2)`: relative error <= 1.3e-5 for `|s| <= 5e-3`).

```
Sliding:   dv/dt = g_t - mu_s g u_hat;  dw_h/dt = (5 mu_s g/(2R)) z_hat x u_hat;  du/dt = g_t - (7/2) mu_s g u_hat
Rolling:   dv/dt = (5/7) g_t - mu_r g v_hat          (static friction supplies (2/7) m g_t; needs (2/7)|s| < mu_s)
Airborne:  dv/dt = (g_t, -g)                          (exact quadratic, as C.1)
Spinning / Stationary: unchanged. Validation: |s| <= 0.7 mu_r, i.e. (5/7) g |s| <= mu_r g / 2, so resting
           balls stay put (static rolling resistance); 7 mm/m on default cloth, 4.9 mm/m on fast cloth.
w_z: unchanged (alpha_sp law, MOT A.7).
```

The constant in-plane force is not parallel to the friction force in general, so `u_hat` (sliding) and `v_hat` (rolling) **rotate** and the motion is no longer polynomial. Consequences for MOT invariants: the Coriolis quantity drifts linearly, `L_c(t) = L_c0 + (5/7) g_t t`, in sliding and in flight (DERIVED: gravity acts through the centre); MOT tests assume `s = 0`.

#### 4.5.2 Exact solution (DERIVED): the pursuit problem

Both states reduce to `dx/dt = G - k x_hat` with constant `G`, `k > |G|`:

| State | pursuit variable `x` | `k` | `G` | position | `c_s` |
|---|---|---|---|---|---|
| Rolling | `v` | `mu_r g` | `(5/7) g_t` | `r = r_0 + X(t)` | 1 |
| Sliding | `u` | `(7/2) mu_s g` | `g_t` | `r = r_0 + L_c0 t + (5/14) g_t t^2 + (2/7) X(t)`, `v = L_c0 + (5/7) g_t t + (2/7) x`, `w_h = (1/R) z_hat x (v - u)` | 2/7 |

with `X(t) = integral of x`. Let `Ghat = G/|G|`, `c0 = x_hat0 . Ghat`, `s0 = |x_hat0 x Ghat|`, `e_perp` = the unit vector perpendicular to `Ghat` on the side of `x0`, `p = k/|G|`, `A = (1 + c0)/2`, `B = (1 - c0)/2`, and the parameter `lam >= 0` (`lam = -ln(w/w0)`, `w = tan(beta/2)`, `beta` = angle between `x` and `G`). With `E_n = -expm1(-n lam)`:

```
t(lam) = |x0| [ A E_{p-1}/(k - |G|) + B E_{p+1}/(k + |G|) ]                    (monotone increasing)
x(lam) = |x0| [ (A e^{-(p-1) lam} - B e^{-(p+1) lam}) Ghat + s0 e^{-p lam} e_perp ]
X(lam) = |x0|^2 [ (A^2 E_{2p-2}/(2(k - |G|)) - B^2 E_{2p+2}/(2(k + |G|))) Ghat
                  + s0 (A E_{2p-1}/(2k - |G|) + B E_{2p+1}/(2k + |G|)) e_perp ]
T_stop = t(inf) = |x0| [ A/(k - |G|) + B/(k + |G|) ]
```

Derivation: `d|x|/dt = -k + |G| cos(beta)`, `|x| dbeta/dt = -|G| sin(beta)` give `|x| proportional to tan(beta/2)^p / sin(beta)`; substituting `w = tan(beta/2)` makes `dt`, `x dt` and `X` sums of powers of `w`. The direction turns toward `G`, and the slip or roll stops exactly aligned with it. The collinear cases are exact quadratics (`s0 = 0`: downhill with deceleration `k - |G|`, `c0 = 1`; uphill with `k + |G|`, `c0 = -1`). Evaluating the state at a given `t` needs one monotone 1-D solve of `t(lam) = t` (safeguarded Newton with bisection, `|dlam| <= 1e-15 max(1, lam)`; `dt/dlam = (|x0|/|G|) (A e^{-(p-1) lam} + B e^{-(p+1) lam})`). Checked against RK4 to 1e-9 m (HF-T15..T17). Needs `rb::Expm1` in `Math/Scalar.h` (WP-0 addition).

#### 4.5.3 Keeping the event-based analytic core: secant segments with refresh events

Collision and cushion detection need polynomial trajectories (ARCH 8.2, COL 3). Each sliding or rolling phase on a tilted table is therefore covered by a **chain of quadratic segments** whose nodes lie exactly on the pursuit solution:

```
MakeTiltSegment(state at t_i):
  if |s| == 0 or state not in {Sliding, Rolling}:  existing MakeSegment
  x_i, k, G, c_s from 4.5.2;  T_rem = T_stop(x_i)
  sigma_i = (c_i >= 0) ? s_i : 1               (bound on sin(beta) over the interval; beta decreases)
  x_tail  = sqrt(eps_tilt (k - |G|) / (4 c_s))
  if sigma_i <= 1e-12:        Delta = T_rem                      (collinear: one exact segment)
  elif |x_i| <= x_tail:       Delta = T_rem                      (tail segment to the exact stop)
  else: Delta = min(RefreshMaxInterval, T_rem, root of  C_int c_s k |G| sigma_i D^3 + eps_tilt (k + |G|) D - eps_tilt |x_i| = 0)
        (C_int = 2/81; unique positive root, Newton from min(|x_i|/(k+|G|), (eps_tilt |x_i|/(C_int c_s k |G| sigma_i))^(1/3)))
  node   = exact state at t_i + Delta (4.5.2), re-anchored at t_i (the ODE is autonomous)
  A2     = (r_node - r_i - v_i Delta) / Delta^2        r(tau) = r_i + v_i tau + A2 tau^2  (as MOT impl. note 1)
  spin:  Rolling w_h = z_hat x v(tau)/R;  Sliding w_h(tau) = (1/R) z_hat x (v(tau) - u(tau)) with
         u(tau) = (7/2)(v(tau) - L_c(tau)), L_c(tau) = L_c,i + (5/7) g_t tau   (linear, so Wdot is constant)
  TauEnd = Delta; end kind = TiltRefresh if Delta < T_rem, else the normal transition
```

- **Refresh event** (`QueuedEventKind::TiltRefresh`, tier `Transition`): the new state is the exact node state; bump the ball's version and re-predict its slots (ARCH 8.3). Logged only with `LogTransitions`.
- **Transitions**: sliding end snaps `u := 0` with `v := v_node` (exact) and `w_h := z_hat x v/R`; rolling end snaps `v := 0`, `w_h := 0`. MOT impl. note 2 is kept.
- **Error bound (DERIVED).** For the quadratic that matches `r` and `v` at the start and `r` at the end, `|r_seg(tau) - r_exact(tau)| <= (2/81) J Delta^3`, with jerk `J = c_s k |dx_hat/dt| <= c_s k |G| sigma_i / |x(Delta)|` and `|x(Delta)| >= |x_i| - (k + |G|) Delta`. The root rule makes this `<= eps_tilt`. The tail segment's deviation is `<= 4 c_s |x_i|^2/(k - |G|) <= eps_tilt`. Nodes are exact to the `lam` tolerance, so **errors do not accumulate** along the chain; the velocity mismatch at a node is `<= c_s J Delta^2/6`. After a collision inside a segment the new anchor carries at most `eps_tilt` position error. Measured maximum error / `eps_tilt`: 0.30-0.95 over the test cases.
- **Cost.** Segments per phase (HF-T16): 0.3 m/s lag: 8 at `eps_tilt` = 5e-5 m; 0.5 m/s roll (5.1 s): 10; 1 m/s roll (10.2 s): 14; stun slide at 2 m/s: 3. Defaults: `eps_tilt` = 5e-5 m (game), 5e-4 m for AI rollouts (5-8 segments per roll), `RefreshMaxInterval` = 2 s. Weak AIs plan on a level table (knowledge knob, 5.5).
- **Why not freeze the direction?** Freezing `u_hat` for a whole 2 m/s stun on 3 mm/m misplaces the ball by 0.18 mm sideways (HF-T17); a dying roll turns by tens of degrees, where freezing errs by millimetres.

#### 4.5.4 Magnitudes

1 mm/m across the line: a 1 m/s roll ends 183 mm to the side after 5.1 m; a 0.3 m/s lag 16.5 mm after 0.46 m; along a 3 mm/m slope a 0.5 m/s roll goes 1.62 m downhill (+27 %) and 1.05 m uphill (-18 %) instead of 1.27 m. Pro installers level to about +-0.005 in over the bed [DD-ROLLOFF], i.e. well under 0.1 mm/m.

#### 4.5.5 Persistence

Per venue table: `Slope` seeded once (dive bar: magnitude U[0.5, 2.5] mm/m, direction uniform; pool hall U[0, 0.3]; arena U[0, 0.1]); optional nightly drift after bumps (V2: +-0.3 mm/m, visible coaster moving under a leg). The notebook records the discovered direction ("Table 2 rolls toward the jukebox") once the player has seen three slow balls curl the same way.

#### 4.5.6 Nap (Later)

Rolling only: `G_eff = (5/7) g_t + zeta_n g n_nap` (a pseudo-slope; the pursuit solution stays exact) and `k_i = mu_r g (1 - eta_n v_hat_i . n_nap)`, frozen per chain segment (re-anchoring at each node keeps it consistent; freezing error `<= eta_n k |dbeta| Delta^2/2`, add `|dbeta| <= 0.05 rad` to the refresh rule when `eta_n > 0`). Defaults `zeta_n` = 2e-4, `eta_n` = 0.1, `n_nap = +x` (head to foot) [EQP 7], all EST. Worsted cloth: 0.

#### 4.5.7 Contract additions (WP-1 / WP-5 / WP-6)

`PhysicsParams::TableTilt { Vec2 Slope = 0; double Tolerance = 5e-5; double RefreshMaxInterval = 2.0; Vec2 NapPseudoSlope = 0; double NapResistance = 0; }`; `ValidatePhysicsParams` checks `|s| <= 0.7 mu_r`; `MotionSegment` stores the pursuit data of its chain (`x_i`, `k`, `G`, `c_s`, end kind); `QueuedEventKind::TiltRefresh`; `Math/Scalar.h`: `Expm1`. Detection (WP-5) is unchanged: segments stay quadratic. With `Slope = 0` every existing MOT/COL/VAL test is untouched (HF-B10).

### 4.6 Racks

Rack quality `Q` in [0, 1] (R-mode: from the push-and-lift motion, lifting fast or tilted shifts balls; A/C mode: the racker's habit `H_rack`; NPC personality for others) maps onto `RackGapParams` (ARCH 7.3): `Mean = Jitter = 0.08 mm (1 - Q) + 0.005 mm Q`; a worn bar triangle doubles the back-row values (Later, per-site gaps). The hustler racks with `Q` = 0.2 when money is on the table and lets the wing ball sit loose. Reading the rack (crouch, 2-4 s, light between balls) is free; asking for a re-rack costs NPC goodwill.

### 4.7 Where state lives

| State | Owner | Persists |
|---|---|---|
| `TipState` per cue, chalk cube hollow, own cues | HM data, career save | across sessions |
| Ball chalk marks, pocketed-ball lock, wallet coins | HM / UG | session (marks until wiped or faded) |
| House-cue rack, ball set, table slope, dead spots, cloth wear textures | venue seed + UG | across visits |
| Habits (`H_chalk`, `H_rack`, sleeve tuck, warp check), attributes, XP, notebook | career save | career |
| Pressure, fatigue, sweat, settle | UG per shot | none |

---

## 5. Progression

### 5.1 Attributes

Six execution attributes on 0-100, one noise family each (3.3): **Steadiness** (drift), **Speed Control**, **Spin Touch** (tip placement, elevation), **Bridge Stability** (bridge factor, slip threshold), **Stance**, **Nerve** (pressure gain, flinch, grip tension). A new career starts at 25; a touring pro sits at 85-95. Skill never decays. Every 10 points multiply a channel by the same factor, so progress feels even. Attribute numbers appear only on the league card and one menu page (open question 3). **Habits** (chalk sweep, auto re-chalk after spin shots, ball wiping, warp check, sleeve tuck, rack quality) grow by doing the chore well in R mode (+0.02 per good execution, saturating at 1) and then show as automatic behaviour. **Knowledge** is not a stat for the player: the notebook records facts on first experience ("The big bar ball draws less", "Table 2 rolls toward the jukebox").

### 5.2 XP sources (graded by core data)

| Source | Feeds | Rule (TUNING) |
|---|---|---|
| Long or thin pots made | Steadiness | `10 D`, difficulty `D` = 2 deg / (pocket half-window in OB degrees at that geometry), clamp [0.5, 5] |
| Leave quality after a make (AI evaluator rates the next shot vs a no-position baseline) | Speed Control | `20 max(0, improvement)` |
| Spin shots with rho >= 0.3, no miscue, intended draw/follow distance achieved within 20 % (core states) | Spin Touch | `8 rho / 0.3` |
| Power shots and breaks (balls to the rail, cue ball controlled) | Bridge Stability | 5-15 |
| Stretch, rail and over-ball bridge shots made | Stance | `10 d_s` |
| Makes at P >= 0.5 in real matches | Nerve | `15 P`; never from drills |
| Drills (stop-shot ladder, speed ladder, spot shot, line-up, draw gates) | targeted | tiered; first clear of a tier pays 10x |
| Matches, won or lost | all, small | losses pay 60 % |

Cost of the next point: `100 x 1.08^(x - 25)` XP. Anti-grind: repeated near-identical shots (same binned CB/OB/pocket geometry hash within 20 shots) pay 50 % less each time; drills pay 25 % after a daily soft cap; no timers, energy or boosters.

### 5.3 Unlocks and equipment (physical sidegrades, fictional brands)

| Item | How | Effect | Trade-off |
|---|---|---|---|
| House cues | free | 4.4 | find a straight one |
| First own cue (used, from the bartender) | cash | straight, 19 oz, medium tip, `m/m_e` 20 | care: tip wear now matters |
| Tips soft / medium / hard | cash | retention, glaze rate, sound (4.2) | soft needs shaping, hard glazes |
| Chalk grades | cash | 4.1 | cost only |
| Tip tool (scuffer, shaper) | cheap | enables scuff and shape chores | over-shaping shortens tip life |
| Bridge glove | cash | V_b x1.15, sweat x0.2 | - |
| Low-deflection shaft | reputation tier + cash | `m/m_e` 20 -> 40 | aim habits change (natural pivot 0.68 m) |
| Break cue (phenolic), jump cue | cash | `e_tip` 0.85; jumps where rules allow | poor for spin; banned by some bar rules |
| Techniques: closed bridge, rail bridge, elevated bridge, jump/masse stroke, break stance | mentor lesson + drill | selects the bridge type with lower `m_br0` / higher `V_b0` | - |

Nothing bought with real money changes a simulated parameter, XP rate or cash. The mechanical bridge is always available.

### 5.4 Difficulty presets and assists (the physics is fixed)

| Assist | Pure | Real (default) | Assisted | Relaxed |
|---|---|---|---|---|
| Aim line / ghost ball | off | off | short | long |
| Tip-offset ring, miscue warning at `rho_max(mu at the contact zone)` | off | off | faint | on |
| Steering gain `G_lat` (UE 5.4; 0 = stroke fully straightened) | 0.25 | 0.25 | 0.10 | 0 |
| Human-layer scale `NoiseScale` | 1.0 | 1.0 | 0.6 | 0.3 |
| Pressure effects | on | on | subtle (P x 0.5) | off |
| Any tip contact is a shot | yes | commit-hold | commit-hold | commit-hold |
| Body/cue fouls (RUL F10) | live | live | ghosted | ghosted |
| Call mode (RUL 4.5) | Explicit | ObviousAssist | ObviousAssist | ObviousAssist |
| Stroke report after a shot | Practice | Practice | always | always |

Accessibility menu (separate): disclosed input smoothing for players with tremor; "pull and release" stroke; one-handed layouts; pattern-coded balls; high-contrast numbers; captions for every audio tell ("[tip slips]", "[heartbeat]"); UE 4.8 motion sliders and Reduced motion.

### 5.5 AI profiles on the same attributes

Ratings on a fictional logarithmic scale (+100 points = 2:1 in games), fitted with rbsim round-robins (HF-B09).

| Profile | Rating | Attributes (St/SC/ST/BS/Sta/N) | Synthetic hand: `sigma_aim`, `|bias_aim|`, `sigma_steer`, `sigma_spd`, pause, `p_jab`, `p_head` | Habits `H_chalk`, `H_rack`, warp check | Knowledge |
|---|---|---|---|---|---|
| Tourist | ~250 | 15 all, Nerve 10 | 0.20 deg, 0.25 deg, 3 mm, 20 %, 0.1 s, 0.4, 0.4 | 0.2, 0.2, no | pots only, level-table model, assumes perfect execution |
| Bar regular | ~400 | 35 all, Nerve 40 | 0.10, 0.12, 1.5 mm, 12 %, 0.25 s, 0.2, 0.2 | 0.5, 0.5, no | next-ball position; **knows this table's slope**; hits hard |
| League player | ~500 | 50 all | 0.05, 0.05, 0.8 mm, 7 %, 0.4 s, 0.05, 0.1 | 0.8, 0.7, yes | 2-ball plans, some safeties, models throw and squirt |
| Local hustler | ~600 | 65 all, Nerve 80 | 0.035, 0.02, 0.5 mm, 5 %, 0.5 s, 0, 0.05 | 0.95, 0.2 in money games, yes | 3-ball plans, sandbags until money is down (a visible choice) |
| Road player | ~680 | 75 all | 0.025, 0.01, 0.35 mm, 4 %, 0.5 s, 0, 0 | 1.0, 0.9, yes | full safety and kicking game, K = 8 self-noise samples |
| Touring pro | 750+ | 90 all | 0.015, 0, 0.2 mm, 3 %, 0.6 s, 0, 0 | 1.0, 1.0, yes | full model, K = 16, uses Settle on pressure shots |

The AI reads only what the player could see (no future seeds, no bag state). Its flaws are drawn at animation scale (jab, loose bridge, skipped chalk), so opponents can be scouted. Hot-seat guests use a 50-all profile and gain no career XP (open question 4).

---

## 6. Test cases

Tolerances: "exact" = +-1 unit in the last listed digit (as MOT); integer and hash values bit-exact; process and stroke values 1e-9 relative (transcendental functions through `rb/Math/Scalar.h`). Common stroke setup **S0**: `R` = 0.028575 m; fresh standard tip (`r_dome` 0.0106, `w_tip` 0.01275, `mu` 0.6/0.35, all `c_z` = 1, no overhang); straight cue (`WarpKnown`); `IntendedStroke {phi 0, theta 3 deg, A 0, B 0, V 2 m/s, v_r = v_u = 0, t_c 2.5 s, no Settle, pause 0.4 s, a_c 0, no head move}`; situation closed bridge, `L_b` 0.20 m, `L_bg` 0.80 m, `d_s` 0, P = F = S = 0; key `{MatchSeed 0x5EED, Rack 0, Shot 0, Shooter 1, ShooterShot 0, Pickup 0}`.

### 6.1 Deterministic, exact

| ID | Input | Expected |
|---|---|---|
| HF-T01 Hash | `Mix64(0)`; `HashKeys(1,2,3)`; `HashKeys(0x5EED,0,0,1,5,0)` | `0xE220A8397B1DCDAF`; `0xCD8D705991914EA1`; `0x735A7BD1020B7AA3`, `U01` = 0.45059942105039663 |
| HF-T02 InvNorm | `InvNorm(0.975)`, `(0.02)`, `(0.5)`; `TruncNormal(0)`, `(1)` | 1.959963986; -2.053748909; 0; -2.500000003; +2.500000003 |
| HF-T03 Bags | seed 0x5EED, shooter 1, channel 5 | bag 0 perm [7,2,1,0,3,6,5,4], bag 1 perm [6,5,2,7,4,1,3,0]; n=0: u 0.886118288553, eps 1.181626120615; n=3: eps -1.301025228812; n=15: eps -1.290035935721 |
| HF-T04 Processes | S0 key, t = 2.5 s | `D_lat` = 0.230366244591, `D_lat'` = -3.986599586190; `T_lat` = -0.601466481787, `T_lat'` = 33.890273864740 |
| HF-T05 Full stroke, attributes 25 | S0 | `phi_x` 2.705488099e-4; `theta_x` 0.04542939137; `A_x` 0.005474734399; `B_x` 0.0236292515; `V_x` 2.135377534; `v_r` 0.002086891421; `v_u` -8.749468987e-4; `(a,b)` (0.003993376783, 0.01723563144); `mu` 0.6; no miscue. Channels: `y_g` 2.164390e-4, `z_g` 5.178315e-4, `eps_A` 1.181626121, `eps_B` 0.585275766, `eps_El` -1.085436397, `eps_V` 1.353775335, `E` 1.043936934 |
| HF-T06 Pressure + Settle | S0 with P = 1, `t_s` = 0.8 s | `g` 5, `k_set` 0.3, flinch 0.039537; `phi_x` 1.81489659e-4; `theta_x` 0.0452163169; `A_x` 0.005871399252; `B_x` -0.02811784601; `V_x` 2.21167099 |
| HF-T07 Attributes 100 | S0, all 100 | `phi_x` 5.410976197e-5; `theta_x` 0.05059489154; `A_x` 0.001003295634; `B_x` 0.003192940687; `V_x` 2.04061326 |
| HF-T08 Identity | S0, `NoiseScale` 0 | outputs equal inputs bit-exactly (`theta_x` = 3 deg, `A_x` = `B_x` = 0, `V_x` = 2), `mu` 0.6 |
| HF-T09 Pivot / natural pivot | `y_g` = 1 mm; yaw 1e-3 rad through MOT B.7 squirt | `yaw` 1.25e-3 rad, `A` shift -0.009998906; `L_p` = 0.289895 / 0.368245 / 0.681645 m for m/m_e 15/20/40; net/yaw at `L_bc = L_p` < 2e-4; at `L_bc` 0.228575 m: 0.211535 / 0.379294 / 0.664677 |
| HF-T10 Warp | `s_w` 3 mm, `L` 1.4478 m, S0 key, unknown roll; then `WarpKnown` | `chi` 5.442345308 rad, `dphi_w` -3.088286471e-3, `dth_w` 2.763518818e-3; known: `dphi_w` 0, `dth_w` 4.144218815e-3 |
| HF-T11 Swoop | `a` = 0.48, `b` = 0, `V` = 1, `v_r` = 0 / +0.05 / -0.05 | `RhoEffective` 0.480000 / 0.523210 (> 0.514496: miscue) / 0.435592 |
| HF-T12 Chalk decay | standard grade, pure right English `a` = 0.45, V = 2, repeated, no re-chalk | `q` 0.748235294, `beta` = pi (zone 4, weight 1), severity 1.005; after 8 hits `c` 0.639757125, `rho_max` 0.454283156; after 9 hits `c` 0.605016227, `rho_max` 0.448110250, so hit 10 miscues and hits 1-9 do not |
| HF-T13 Chalking | `c` = [0.5, 0.2, 0.9 x5], 3 twists, cap 1 | `H_chalk` 0: [0.968, 0.454822, 0.931853 x5]; `H_chalk` 1: [0.968, 0.866900, 0.983363 x5]; bar cube (cap 0.7, hollow 0.8), 1 twist: [0.62, 0.299, 0.9 x5] (no zone decreases) |
| HF-T14 Tip edge | `w_tip` / `r_dome` = 12.75/10.6, 12.75/8.96, 12.75/18, 11/18 mm | `rho_edge` 0.601415, 0.711496, 0.354167, 0.305556 |
| HF-T15 Tilt oracle, rolling | `mu_r` 0.01, g 9.80665; (a) `v0` (0.5, 0), `s` (0, 1e-3); (b) `v0` (1.0, 0.3), `s` (2e-3, -1e-3) | (a) `T_stop` 5.124727634 s, stop displacement (1.276273166, -0.045756497) m; at t = 2 s `v` (0.303903300, -0.010810351), `r` (0.803883823, -0.011963026). (b) `T_stop` 9.654203692; at t = 3.3 s `v` (0.645704992, 0.224066759), `r - r0` (2.714259691, 0.868289848). Both equal RK4 (2e5 steps) to 1e-9 |
| HF-T16 Secant chain | HF-T15 (a) with `eps_tilt` 5e-5 | 10 segments, nodes at t = 1.047533, 2.004048, 2.861368, 3.608756, 4.231390, 4.707615, 5.005814, 5.110593, 5.123343, 5.124728 s; max deviation 4.27e-5 m <= `eps_tilt`. Counts: `v0` 1 m/s: 14; 0.3 m/s: 8; `eps` 5e-4: 6 for (a) |
| HF-T17 Tilt, sliding and collinear | stun `v0` (2, 0), `w0` 0, `mu_s` 0.2, `s` (0, 3e-3); collinear `v0` (0.5, 0) rolling, `s` (-3e-3, 0) and (3e-3, 0) | slide ends at 0.291352841 s (level 0.291347489), `v` (1.428571429, -0.006122561), `r` (0.499460866, -0.001070292), 3 segments at 5e-5; frozen-direction error 0.178 mm. Downhill: 1 segment, `T` 6.489103173 s, 1.622275793 m (= v0/(k-|G|), v0^2/(2(k-|G|))); uphill: `T` 4.198831465 s, 1.049707866 m |

### 6.2 Statistical (seeded, so the counts are exact; tolerance +-2 counts for CRT differences)

| ID | Setup | Expected |
|---|---|---|
| HF-S01 Truncated moments | 80 000 bagged draws, seed 0x5EED, shooter 3, channel 8 | mean -0.000247 (+-0.01), sd 0.955868 (theory 0.954597, +-0.005), max \|eps\| <= 2.5000001 |
| HF-S02 Bag coverage | first 4000 draws of HF-S01 | every aligned block of 8 covers bins 0-7; no bin more than twice in any 8 consecutive shots; longest run of the worst bin 2 |
| HF-S03 Process RMS | `D_lat`, `T_lat`, S0 key, 1 kHz over 600 s | 0.99894 and 1.00011 (1 +- 0.03) |
| HF-S04 Routine budget | budget model 3.10 (m/m_e 15), S0 except: 20 000 shots i = 0..19 999 with key {0xB00D, Rack i/20, Shot i, Shooter 1, ShooterShot i}, `t_c = 1.5 + 2.5 U01(HashKeys(99, i))`, V 1.5 m/s | attributes 25: 212 misses (1.06 %); 10: 1344; 40, 60, 85, 100: 0 |
| HF-S05 Max-draw miscues | S0 except `B_i` = -0.61693 (`b` = -0.45), key {0xD4A3, Rack i/20, Shot i, Shooter 2, ShooterShot i}, `t_c = 1.5 + 2.5 U01(HashKeys(98, i))`, 20 000 shots | attributes 10: 3044 / 20 000; 25: 1566; 40: 530; 60, 85, 100: 0 |
| HF-S06 Pressure | as HF-S04, attributes 25, P = 1 | no Settle: 1726 (8.63 %); Settle (`t_s = t_c - 2 s`): 478 (2.39 %); P = 0 with Settle: 80 (0.40 %) |
| HF-S07 Cling weight | one mark, strength 1, radius 2.5 mm; contact at 0 / 0.05 / 0.1 rad | `k_cling` 2.5 / 2.082045 / 1.406170 (`k_venue` 1); random orientation mean weight 0.0019 +- 0.0002 |

### 6.3 Behavioural acceptance

| ID | Requirement |
|---|---|
| HF-B01 | Same input log, seeds and state give an identical `CueStrikeInput` and `ShotResult` hash in UE and rbsim (ROB-10 style). |
| HF-B02 | Release blocker: 3.10 budgets hold with the full core, 10^5 shots per profile. |
| HF-B03 | A miscue happens only when the core's criterion fires (MOT B.4/B.4a); no other code path sets `Miscue`. |
| HF-B04 | Revolver rule: missing coverage 40 % -> 3 auto twists (about 1.2 s at habit 0); 10 % -> 1 twist; aborting after 2 of 5 twists keeps exactly 2 twists of coverage. Ball clearing time grows linearly with balls left; coin insertion is per coin. |
| HF-B05 | Skipping a chore (A/C/P) gives the habitual result bit-exactly regardless of real-time speed; R mode can match or beat it. |
| HF-B06 | Every miss yields a diagnosis within 3 s; constructed cases: bare zone -> "equipment", drift-only perturbation -> "hand drift", zero human layer with steered input -> "input". |
| HF-B07 | Static check: AI code constructs `CueStrikeInput` only through `ExecuteStroke`. |
| HF-B08 | Every channel at 1 sigma moves the rendered tip or cue by >= 1 px at 1440p in the Eyes preset (automated capture). |
| HF-B09 | rbsim round-robins reproduce 2:1 game odds per 100 rating points within +-5 %. |
| HF-B10 | With `Slope = 0`, `ChalkCling` off and `TipTransverseVelocity = 0`, all MOT/COL/VAL/RUL tests pass unchanged. |
| HF-B11 | The same venue seed gives the same house-cue defects, ball set and table slope on every visit; a replay from an older save reproduces with its stored state. |
| HF-B12 | Playtest KPI: counterfactual share of misses caused by the human layer <= 35 % at the start profile, <= 10 % at attributes 85. |

---

## 7. Open questions for the product owner

1. **Stratified bags:** keep them (disclosed on a "How it works" page) or use plain independent draws? Bags remove bad-luck streaks but a very attentive player could sense them.
2. **Alcohol** as a mechanic in the dive bar (one beer calms Nerve, three hurt Steadiness), or cosmetic only for V1 because of PEGI/ESRB descriptors?
3. **Attribute numbers:** show them on the league card and one menu page, or keep progression fully diegetic (stroke steadier, mentor remarks)?
4. **Hot-seat:** guests at 50 in every attribute without career XP (proposal), or both players use career profiles?
5. **Default chores:** Full for the first visit of each venue and Brisk afterwards (proposal), or Brisk from the start?
6. **Money games** with in-game cash (hustling, side bets): acceptable given the simulated-gambling descriptors, or limited to league prize money?

---

## 8. Sources

- [DD-CHALK] Dr. Dave, chalk comparison (no-rechalk miscue counts): https://drdavepoolinfo.com/faq/chalk/comparison/
- [DD-CLING] Dr. Dave, cling/skid/kick: https://drdavepoolinfo.com/faq/throw/cling/ ; TP A.14 (cling multipliers 1.5 / 2.5, "up to 3"): https://drdavepoolinfo.com/technical_proofs/new/TP_A-14.pdf
- [DD-TIPCARE] tip care: https://drdavepoolinfo.com/faq/cue-tip/care/ ; [DD-HARD] tip hardness: https://drdavepoolinfo.com/faq/cue-tip/hardness/
- [DD-MARGIN] cut-shot margin for error: https://drdavepoolinfo.com/faq/cut/margin-for-error/ ; [DD-POCKET] effective pocket size (TP 3.6): https://drdavepoolinfo.com/faq/pocket/size-and-center/
- [DD-ROLLOFF] roll-off: https://drdavepoolinfo.com/faq/table/roll-off/ ; levelling tolerance discussion: https://forums.azbilliards.com/threads/how-level-is-level-for-pool-tables.494810/
- [DD-VISION] vision centre: https://drdavepoolinfo.com/faq/eyes/vision-center/ ; [DD-ELBOW] elbow drop: https://drdavepoolinfo.com/faq/stroke/elbow-drop/
- [DD-STROKE24] "Stroke Video Analysis", Billiards Digest, March 2024: https://drdavepoolinfo.com/bd_articles/2024/march24.pdf ; [DD-SIDESPIN] unintentional side spin: https://drdavepoolinfo.com/faq/squirt/straight-shot/
- [HAAR20] Haar, van Assel, Faisal, "Motor learning in real-world pool billiards", Sci. Rep. 2020: https://www.biorxiv.org/content/10.1101/612218v4.full
- [WILLIAMS02] Williams, Singer, Frehlich, "Quiet eye duration, expertise, and task complexity", J. Motor Behavior 34(2), 2002: https://www.researchgate.net/publication/11316045
- Elite snooker player stroke case study, Physical Activity and Health: https://paahjournal.com/articles/10.5334/paah.111
- [DIGICUE] DigiCue stroke metrics (jab, follow-through, backstroke pause, tip steer): https://github.com/nataddrho/DigiCue-USB/blob/master/README.md
- [WADA] beta-blockers prohibited in-competition in billiards: https://www.drugs.com/wada/p1-beta-blockers.html
- Coin-op separation and returns: https://electronics.howstuffworks.com/question495.htm ; https://www.billiardsforum.com/pool-table-repair/valley-7450-coin-op-pool-table-restoration-magnetic-cue-conversion-project
- Design references: Arma 3 OPREP weapon sway (predictable sway instead of random penalties): https://dev.arma3.com/post/oprep-weapon-sway-fatigue ; Bodycam "Locked and Loaded" overhaul: https://www.escapistmagazine.com/news-bodycam-locked-and-loaded-overhaul-pc-shooters/ ; Tarkov Mag Drills (skill improves information precision): https://escapefromtarkov.fandom.com/wiki/Mag_Drills
- Project specs: MOT B.3-B.8 and impl. notes 1-2, 10; COL 2.1, 3.9.5; EQP 6.2-8.5; RUL 3.2, F5, F7-F11; UE 4.8, 5.3-5.5; ARCH 2, 5.3, 8, 11.
