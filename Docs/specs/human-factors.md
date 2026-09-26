# RAW BREAK - Human Factors Spec: stroke execution, imperfections, equipment state, progression

| Field | Value |
|---|---|
| Spec ID | `human-factors` (prefix **HF**) |
| Modules | New engine-agnostic package `rb::human` inside `BilliardsCore` (work package **WP-11**, headers `rb/Human/*.h`, ARCH 7.5 and 17; ARCH 17 already uses WP-10 for validation and benchmarks; same coding rules as ARCH 2; no Unreal headers). Physics additions in WP-0/1/3/5/6a (listed in 3.11 and 4.5.7; ARCH 8.11). Chores, animation and audio live in the UE game module. |
| Scope | Design principles; catalogue of human, equipment and venue imperfections; the deterministic stroke execution model (intended stroke -> `CueStrikeInput`); tip/chalk/cue/ball/table state models, including table tilt in the event-based physics; progression, difficulty and AI profiles; tests. |
| Builds on | MOT = physics-motion-and-cue (B.3-B.8), COL = physics-collisions (2.1 `k_cling`, 3.9.5 rack gaps), EQP = equipment (6-9), RUL = rules (3.2, F7-F10), UE = ue5-realism-plan (4.8, 5.3-5.5), ARCH = architecture (2, 5, 8, 11). |
| Status | Draft v1.3 (2026-09-26): all seven product-owner questions answered (section 7: Q2 alcohol cosmetic for V1 with an intoxication hook for a later mechanic, Q3 attribute numbers hidden, Q6 money games with in-game cash) and the architecture integration review applied (exact-integer streak fallback 3.2, intoxication hook 3.4, money-game stakes; log 9.5). v1.2: Q1 streak-guarded draws replace the bags of 8; Q4, Q5, Q7 decided (log 9.4). v1.1: after adversarial verification (section 9). Every number in section 6 was produced by a Python reference implementation of this text (hash, streak guard, stroke model, chalk model, tilt oracle, Monte Carlo; `Tools/reference/human-factors/`) and re-derived by a second, independent implementation (v1.1); the draw-dependent values were recomputed for v1.2 with `streak.py` and `recompute_v12.py`. |

Tags: **[SRC]** sourced (section 8), **DERIVED** derived here, **TUNING** gameplay value to calibrate, **EST** estimate. Units SI, angles in radians in code (degrees only in prose). Frames and signs follow MOT 0: `+z` up, right English `a > 0`, cue frame `(e_r, d, e_u)`.

---

## 1. Design principles

1. **Visible cause.** Every imperfection acts on the avatar's hands, cue, tip, balls or table **before** the tip touches the cue ball, and it is drawn in first person (tip wander, bridge creep, bare tip, bowed cue, slow ball curling). Nothing random happens after contact. There is no dice roll on outcomes (make/miss, miscue yes/no, cling yes/no): the core physics decides from perturbed inputs and persistent state.
2. **Input first.** The player's mouse stroke (UE 5.4: aim, tip offset, speed, lateral steering, rhythm) is the main source of error. The avatar adds a thin **human layer** whose size is fitted to a routine-shot budget (3.10). Skill never straightens, smooths or rescales the player's input; that is an Execution assist (5.4).
3. **Deterministic seeded noise.** Noise is a pure function of `(MatchSeed, RackIndex, ShotIndex, ShooterId, Channel, Sub)` through a counter-based hash (3.2). No RNG state, no wall-clock. A replay stores the raw input log, the seeds and the final `CueStrikeInput`, and reproduces bit for bit (ARCH 5.3, 11).
4. **Same model for the AI.** The AI plans a stroke, a *synthetic hand* adds that profile's input flaws (3.8), and the result goes through the same `ExecuteStroke`, the same `CueStrikeInput`, the same simulator and the same rules. The AI never builds a `CueStrikeInput` directly (test HF-B07).
5. **Respect the player's time.** Chore duration is the sum of its physical steps (coins x 1 s, twists x 0.4 s, balls x walking). A partial job leaves partial state (the revolver rule: chalk only what is worn, rack only the balls you have, an aborted chore keeps what was done). Every chore has four speeds: **R** full interactive ritual, **A** hands do it automatically (hold to speed up 2-3x), **C** 1.5 s cut, **P** done while the opponent shoots. A skipped chore produces the character's *habitual* result (seeded, deterministic), so skipping never changes the outcome versus automatic play; only doing it interactively and well can beat the habit. **R results are capped at the habit-1 result**, so the ritual pays off while a habit is being learned but is never mandatory once it is maxed (no compulsory ritual in a long career). Target overhead per 8-ball rack in the dive bar (TUNING): Full 45-90 s, Brisk 15-25 s, Minimal < 8 s. Default (Q5): Full on the first visit of each venue, Brisk afterwards. Saving is never restricted.
6. **Skill reduces but never removes human error.** Every noise channel follows `sigma(x) = sigma_25 * rho^((x-25)/75)` with `rho > 0` (5.1); at attribute 100 the noise is small but not zero. Equipment and table faults never scale with skill; experience shows up as habits (automatic chalking, rolling house cues) and knowledge (the notebook).
7. **Errors carry information.** Each channel has a perceivable tell at 1 sigma: at least a 1-pixel displacement of the rendered tip or shaft at 1440p in the Eyes preset, or, for the two sub-pixel channels (tremor, lateral tip placement; 3.7), a dedicated audio/haptic or replay tell (HF-B08). Every miss gets a counterfactual diagnosis within 3 s (3.9), delivered diegetically (mentor line, replay close-up, notebook).
8. **Fairness caps.** Per-shot draws are truncated at +-2.5 sigma and streak-guarded (Q1, 3.2): a draw whose eighth of the distribution already occurs twice in the shooter's last 7 draws is redrawn, so no 8 consecutive shots contain any eighth more than twice, and the next draw is never predictable. The guard is disclosed on the in-game "How it works" page. No rubber-banding, no hidden streak breakers, no paid performance.
9. **Options and accessibility.** Sliders: Chores (Full / Brisk / Minimal), Imperfections (Sim / Scaled / Low / Off = human-layer scale 1 / 0.6 / 0.3 / 0, the values the presets of 5.4 use), Pressure (On / Subtle / Off), Diegetic-only information (On / Off), and the presets of 5.4. Accessibility options never lock career content; only ranked leaderboards group by preset.

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
| HF-11 | **Swoop / steering at contact.** Steering during the final stroke changes the executed azimuth and tip offset at contact (pivot geometry, 3.5), so a swoop toward the outside pushes the contact past the miscue limit. The sideways tip *velocity* itself barely matters: during the ~1 ms contact only the shaft end mass `m_e` moves sideways with the tip (MOT B.7), so it can exert almost no transverse force [DD-SWOOP] | Tip visibly swings off the aimed spot; outside swoops miscue, inside swoops "save" English | per stroke | input + Steadiness | V1 | HM (no core change) | DERIVED (end-mass grip model, 9.2 item 1): 5 cm/s at 1 m/s changes the friction demand by +-0.003 in rho units and turns the cue ball by about 0.1 deg toward the swoop. Neither is modelled in V1; the tip velocity is output for animation (HF-T11) |
| HF-12 | **Bridge type and bridge slip.** Closed loop constrains best; open, rail, elevated and mechanical bridges less. Above a speed V_b the bridge slips | Hand pose; fingers creep, cue rattles on power shots | per stroke | Bridge Stability | V1 | HM, UA, US | `m_br0` closed 1.0 / open 1.2 / rail 1.4 / elevated 2.0 / mechanical 1.8; `V_b0` 6 / 4 / 3.5 / 2.5 / 3 m/s. EST |
| HF-13 | **Awkward stance** (stretch, one foot down, over a ball) | Strained body, back foot on its toe | per shot (IK) | Stance | V1 | UA -> HM | `m_st = 1 + d_s L` (x2 at x=25, x1.2 at 100). EST |
| HF-14 | **Grip tension and flinch under pressure**: the tip drops and the stroke decelerates | Knuckles whiten; stroke dies | per shot | Nerve | V1 | HM, UA | tip bias -1.5 mm x P x L_N; flinch <= 8 % -> 1 % speed. EST; pressure and gaze [WILLIAMS02] |
| HF-15 | **Pressure scalar** from stakes (incl. money games: bet against cash, Q6), game ball, hill, crowd, shot clock, run length (3.4) | Heartbeat, breathing, crowd ducking; no vignette by default | match state | Nerve | V1 | UG | weights 0.35 / 0.25 / 0.15 / 0.10 / 0.10 / 0.05. TUNING |
| HF-16 | **Fatigue** after 2 h+ of the character's night | Heavier breathing, slower get-down | in-game night | - | V2 | UG -> HM | `m_fat = 1 + 0.2 F`, `F = clamp((in-game hours of this night - 2)/2, 0, 1)`; resets with the next in-game day; off in Practice and hot-seat. Never real-world session time (it would punish long play sessions). EST |
| HF-17 | **Sweaty or sticky bridge hand**, glove | Drag squeak in the bridge; wipe on the jeans | per night | H (glove, wipe) | V2 | UG -> HM | drift x(1 + 0.5 S), V_b x(1 - 0.3 S); glove: sweat term x0.2, V_b x1.15. EST |
| HF-18 | **Vision centre.** The sighting eye is off the cue line, giving a constant aim bias | Found by the mentor's vision-centre drill (UE 4.2 calibration mini-game), which is offered in the first career session, before any match counts | per character | K | V2 | UA | camera eye offset 0-15 mm before calibration, removed by the player's calibrated `y_vc`; no noise channel (the bias is real parallax). [DD-VISION] |
| HF-19 | **Off-hand stroke** | Clumsy pose | per shot | - | Later | HM | all channels x2.5. EST |
| HF-20 | **Alcohol** | V1: drinks are props; bounded camera blur / vignette only while standing or walking, never while down on a shot and never on the cue (UE 4.8 sliders and Reduced motion apply) | session | - | V1 cosmetic, mechanic Later | UA (V1); HM hook | decided (Q2, section 7): cosmetic only in V1; hook `StrokeSituation::Intoxication` (3.4), 0 unless `ProductConfig::Alcohol` = `Mechanic` |

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
| HF-31 | **Warp orientation.** A bowed cue has a fixed tip-direction error that depends on how it is rolled in the hand; experienced players hold the bow up | Roll test on the cloth (visible wobble); habit "bow up". The A-mode pickup always includes a 1-2 s roll test; the habit sets the smallest bow the character notices (1 mm at habit 0, 0.3 mm at habit 1) | per pickup | K, H | V1 | HM | `gamma = k_w 4 s_w / L`, `k_w = s_e/L` (0.31); yaw `gamma sin(chi)`, pitch `gamma cos(chi)`. DERIVED 4.4 |
| HF-32 | **Blocked backstroke, short cues** (walls, stools, lamp) | Butt bumps; offer 48/52 in cue or mechanical bridge | per shot | - | V1 | UG (sweeps UE 5.5), HM | short cue: `V_i` capped at 0.8 x normal max. EST |
| HF-33 | **Shaft grime** | Drag; towel wipe 4 s | per night | H | V2 | HM | drift x(1 + 0.3 grime). EST |
| HF-34 | **Cue mass, LD shaft, break/jump cue** | Weight sticker, case | owned cues | K | V1 | CP (`CueSpec`) | [EQP 8], [MOT B.7] |

### 2.3 Balls, table, rack (section 4.3-4.6)

| ID | Detail - real mechanism | Player sees / hears / does | State | Skill | Prio | Layer | Parameters |
|---|---|---|---|---|---|---|---|
| HF-40 | **Chalk marks on the cue ball** tracked in the ball's body frame; a mark at the ball-ball contact raises friction ("kick") | Blue dots rotate with the ball; wipe with a towel at ball in hand (2-3 s) | per ball until wiped or faded | H (wipe) | V1 visual, V2 physics | HM marks, CP cling | `r_mark` 2.5 mm, `k_chalk` 2.5 (a chalked contact, whatever the venue's ball dirt; 4.3). [COL 2.1] (TP A.14) |
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
| HF-60 | **Shaft, hand, sleeve or chalk touches a ball** (all-ball foul) | Replay shows the contact. Fairness: the stance/IK solver never creates a contact by itself (it keeps 5 mm clearance while getting down); if no clear pose exists, the tight spot is shown (hand hovering, sleeve brushing the cloth) before the player is "down on the shot", where the RUL F10 window starts | - | H (sleeve tuck) | V1 | UG (mesh collision, authoritative in Sim mode for **both** players: the AI avatar uses the same IK and colliders in a match), HM (shaft check for AI planning and rbsim only, 3.6) | [RUL F10] |
| HF-61 | **Double hit / push** with a near (not frozen) ball, from the executed follow-through. Shooting into a ball frozen to the cue ball is legal inside the frozen envelope (RUL F7/F8) | Foul call | - | K | V1 | CP (`ResolveTipRecontact`) | [RUL F7/F8], [ARCH 8.6] |
| HF-62 | **Scoop**: elevated tip digs into the cloth. **Not a foul under WPA 2025** (treated like a miscue, RUL F9); a foul only with `ScoopPolicy = Foul` (house variants) | Scuff sound | - | - | V1 | UA (cue-body model); `ExecuteStroke` also sets `TipTouchesCloth` from the executed pose (3.6) so AI and rbsim see the same flag | [RUL F9] |
| HF-63 | **Chalk left on the rail** becomes an outside object | Cube on the rail | per session | H | V2 | UG | [RUL F5] |
| HF-70 | **Pay per rack**: quarters one by one, push the slide, balls rumble | Clunk, rumble, pocket jingle | wallet | - | V1 | UG, US | $1.00-2.00; 1 s per coin + 2 s |
| HF-71 | **Balls locked after pocketing**; leftover balls rolled into pockets before the next rack. Rules consistency: WPA spotting needs a pocketed ball back (RUL 6.5, 7.5, 8.5, 9.5), and a WPA re-rack after the 8 on the break needs all balls. The bartender unlocks the machine (HF-73 animation, chore speeds R/A/C/P); a re-rack costs a new game's coins. The play state can never get stuck | Loser clears the table | per game | - | V1 | UG | 1.5-3 s per ball |
| HF-72 | **Scratch**: cue ball travels the return, you walk to fetch it | Rolling sound in the return | - | - | V1 | UG, US | 2-6 s return |
| HF-73 | **Cue-ball separation faults** (object ball in the cue-ball chute, weak magnet) | Bartender with a key | per table | - | V2 | UG | rare, seeded, at most 1 per night; chore speeds R/A/C/P apply |
| HF-74 | **Racking by hand** from the tray (only balls that came out); **spotting** one ball at a time | Hands on the triangle | per rack | H | V1 | UG | 15-25 s per 15 balls; 2-3 s per spotted ball |
| HF-75 | **Ball in hand placed by hand** | Hand near clusters | - | H | V1 | UG | [RUL F10/F11] |
| HF-76 | **Calls, 8-ball pocket marker, chalkboard score, quarters on the rail queue** | Gestures, voice line, coaster | per game | - | V1 | UG | [RUL 4.5] |
| HF-77 | **Opponent waiting spot** sometimes in your line; wave to move; hustler sharks (V2) | NPC behind the pocket | - | - | V1 / V2 | UG | - |
| HF-78 | **Lamp swing** after the butt hits it: moving shadows for 5-10 s | Swinging light; the player can reach up and stop it (1 s) instead of waiting | - | - | V2 | UA | - |
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
  double TipVelocityRight, TipVelocityUp; // v_r,i, v_u,i [m/s] swoop from input steering (animation only, HF-11)
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
  double Intoxication = 0;       // I [0, 1], Q2 hook (3.4): 0 unless ProductConfig::Alcohol = Mechanic; 0 = no effect
};
struct NoiseKey { uint64_t MatchSeed; uint32_t RackIndex, ShotIndex, ShooterId, ShooterShotIndex, CuePickupIndex;
                  uint32_t Purpose = 0;         // Purpose != 0: AI rollout keys (never the match stream), 3.2
                  uint32_t AddressIndex = 0; }; // earlier get-downs on this shot: new drift / tremor processes (3.2)
                  // ShooterShotIndex counts this shooter's revealed per-shot draws in the match (3.7), not only shots
struct HumanParams { /* all defaults of 3.3 */ double NoiseScale = 1.0; uint32_t ChannelMask = 0; bool StreakGuard = true;
                     double WarpSightLength = 0.45; };  // s_e [m], UE 4.2 tip-to-eye distance along the cue (4.4)
struct ExecutedStroke {
  CueStrikeInput Strike;         // what the physics gets (MOT B.1, unchanged contract; TipTouchesCloth set here, 3.6)
  Vec2 TipTransverseVelocity;    // (v_r, v_u) [m/s] at contact: animation/audio only, not a physics input (HF-11)
  double Rho, MiscueLimit; TipZone Zone;  // Dome / Overhang / Ferrule (3.6)
  bool PredictedMiscue, DoubleHitRisk, PushRisk, OffsetClamped, ElevationClamped;
  FixedVector<NonTipContact, 4> ShaftContactCandidates; // core geometry check (3.6)
  Breakdown Channels;            // per-channel contributions, for replay and diagnosis (3.9)
};
ExecutedStroke ExecuteStroke(const IntendedStroke&, const ShooterAttributes&, const StrokeSituation&,
                             const TipState&, const CueBodyState&, const CueSpec&, const BallSpec& CueBall,
                             const Vec3& CueBallPosition, Span<const BallObstacle> OtherBalls,
                             const NoiseKey&, const NoiseHistory&, const HumanParams&);  // NoiseHistory: Q1 streak guard (3.2)
HandPose SampleHand(/* same inputs */, double t);  // pose at time t for rendering (3.7)
}
```

The authoritative declarations are the headers `rb/Human/*.h` (ARCH 7.5); this sketch shows the data flow.

`ExecuteStroke` has no side effects and allocates nothing. Equipment and table state is updated **after** the shot by `ApplyShotToEquipment` (4.1-4.3), never inside it.

### 3.2 Noise primitives (exact definitions)

```
Mix64(x)      = SplitMix64Next(copy of x)            (rb/Core/Random.h: add 0x9E3779B97F4A7C15, then mix)
HashKeys(k..) : h = 0x243F6A8885A308D3; for each key k_i (uint64, in order): h = Mix64(h XOR k_i)
U01(h)        = (h >> 11) * 2^-53
InvNorm(p)    = Acklam's rational approximation (no refinement step; |rel err| < 1.15e-9), through rb::Log/Sqrt;
                published coefficients, split at p = 0.02425 (note c4 = -2.549732539343734; a common mistyped
                value breaks the tails, which HF-T02 checks)
TruncNormal(u)= InvNorm(Phi_lo + u (1 - 2 Phi_lo)),  Phi_lo = Phi(-2.5) = 0.0062096653257761   ->  z in [-2.5, 2.5]
```

**Channels** (`ChannelId`): 1 DriftLat, 2 DriftVert, 3 TremorLat, 4 TremorVert, 5 TipA, 6 TipB, 7 Elevation, 8 Speed, 9 Flinch, 10 WarpRoll, 20-27 SyntheticHand (3.8). AI rollout calls replace the `ShooterId` key by the 64-bit value `uint64(ShooterId) | (uint64(Purpose) << 32)` in every key (widen before shifting; `ShooterId` is 32-bit), with `Purpose = 1 + s` for rollout sample `s` in `[0, K)`, so the K samples differ, never reuse match values and cannot peek at the shooter's real future noise. All candidate shots of one decision use the same K sample keys (common random numbers, which lowers the variance of the comparison). Diagnosis re-runs (3.9) use the match key itself with channels masked, so they see exactly the noise of the real shot.

**Streak-guarded draws** (channels 5-9 and 20-22; product-owner decision Q1, v1.2, replaces the bags of 8 of v1.1, which were predictable after 7 revealed draws). Per shooter key `S` and channel `c`, draw `n = ShooterShotIndex`:

```
u(n, Sub) = U01(HashKeys(MatchSeed, S, c, n, Sub))                     candidate Sub = 0, 1, ..., 31
H(n)      = the eighths floor(8 u) of the ACCEPTED draws max(0, n-7) .. n-1 of the same (S, c)
u         = u(n, Sub) for the smallest Sub whose eighth occurs fewer than 2 times in H(n)
fallback  (no Sub in 0..31 accepted, probability < (3/8)^32 = 2.4e-14): A = the allowed eighths ascending
          (m = |A| >= 5), N = the 53-bit integer of u(n, 31) (u = N 2^-53); in exact integer arithmetic
          P = m N (< 2^56), j = P >> 53, u = (A[j] 2^50 + ((P mod 2^53) >> 3)) 2^-53          (lies in eighth A[j])
epsilon   = TruncNormal(u)      (Flinch uses u directly)
```

Draw 0 has an empty history, so the draw sequence of one `(MatchSeed, S, c)` is a pure function of `n` (the recursion from draw 0 is the definition; `NoiseHistory` in `rb/Human/NoiseHash.h` is its incremental cache, and a cache that does not match the key is rebuilt from draw 0, so it can never change a draw). The fallback is integer arithmetic because the floating-point form `(A[j] + (m u31 - j)) / 8` of v1.2 rounds `m u31` and can land in the excluded eighth `A[j] + 1` (`N` = 7205759403792793, `A` = {0, 1, 3, 5, 7} gives eighth 6; `streak.py`); no test value changes, the fallback never occurs in them. Properties: no eighth occurs more than twice in any 8 consecutive draws (a third occurrence in a window of 8 would have met two in its own last 7), so no run is longer than 2; at most 3 eighths are ever excluded (3 x 2 = 6 <= 7), so every candidate is accepted with probability >= 5/8 and the next draw is never known; the marginal distribution stays uniform (the rule is symmetric in the eighths). About 21 % of the draws need a redraw (HF-S01). With `StreakGuard = false`, and always for AI rollout keys (`Purpose` != 0), `u = u(n, 0)`: plain independent draws that never read the history, so rollouts sample the unconditional distribution and learn nothing the player could not see. The guard is explained on the in-game "How it works" page. Oracle: `Tools/reference/human-factors/streak.py`.

**Watchable processes** (channels 1-4) are band-limited sums of `K = 6` cosines, evaluable at any time in O(K):

```
f_k  = f_lo + (f_hi - f_lo) (k + U01(HashKeys(MatchSeed, RackIndex, ShotKey, ShooterId, Channel, 2k))) / K
ph_k = 2 pi U01(HashKeys(MatchSeed, RackIndex, ShotKey, ShooterId, Channel, 2k + 1))
ShotKey = uint64(ShotIndex) | (uint64(AddressIndex) << 32)       (= ShotIndex for the first get-down)
D(t) = sqrt(2/K) SUM_k cos(2 pi f_k t + ph_k),   D'(t) = -sqrt(2/K) SUM_k 2 pi f_k sin(2 pi f_k t + ph_k)
```

Unit mean square; drift band 0.15-0.6 Hz (slow enough to watch and time), tremor band 8-12 Hz. `t` is time since "down on the shot" from the input log, so identical input timing gives identical noise. `AddressIndex` counts the earlier get-downs on this shot (input log): standing up and getting down again starts new processes, as a real hand would, so a dry run cannot reveal the drift of the next address (v1.3; with the v1.2 key the same `D(t)` repeated on every address). HF-T04 and HF-S03 use `AddressIndex` 0 and are unchanged.

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
| `g_max - 1` | 4 | 0.125 | Nerve | pressure gain `g = 1 + P (g_max - 1)`; acts as `g` on tremor, `g^(1/2)` on speed, `g^(1/3)` on drift (refit so that the 3.10 budgets also hold with the standard own cue) |
| `b_grip` | 1.5 mm | 0.125 | Nerve | tip drop at P = 1 |
| bridge excess `m_br0 - 1` | table HF-12 | 0.4 | Bridge Stability | |
| `V_b` | table HF-12 | x(1 + 0.6 (x - 25)/75) | Bridge Stability | x1.15 glove, x(1 - 0.3 S) sweat |
| stance excess | 1.0 | 0.2 | Stance | |

All values TUNING; the fit criterion is the routine budget 3.10. The 1-sigma values are those of the untruncated normal; the realized standard deviation of a truncated draw is 0.9546 sigma.

### 3.4 Situation multipliers

```
P_x     = P (1 - c_calm min(1, I / I_c))                pressure after the intoxication calm (I = Situation.Intoxication, 0 in V1)
L_N     = L(Nerve; 0.125)
g       = 1 + 4 P_x L_N                                 pressure gain (P = pressure in [0, 1])
m_br    = 1 + (m_br0(bridge) - 1) L(BridgeStability; 0.4)
V_b     = V_b0(bridge) (1 + 0.6 (clamp(BridgeStability,0,100) - 25)/75) (Glove ? 1.15 : 1) (1 - 0.3 Sweat)
m_slip  = 1 + 0.5 max(0, V_i - V_b)/V_b                 bridge slip on power shots
m_st    = 1 + d_s L(Stance; 0.2)
m_head  = HeadMovedBeforeContact ? 1.5 : 1
m_stick = 1 + 0.5 Sweat (Glove ? 0.2 : 1)
m_fat   = 1 + 0.2 Fatigue;   m_off = OffHand ? 2.5 : 1
m_rush  = 1 + 0.3 clamp(1 - T_pause/0.2 s, 0, 1)
m_jab   = 1 + 0.5 clamp(-a_c/(10 m/s^2), 0, 1)
m_alc   = 1 + k_dr max(0, I - I_c)/(1 - I_c);   m_alc,t = 1 + k_tr max(0, I - I_c)/(1 - I_c)      drift, tremor (Q2 hook)
E(t)    = 1 + exp(-t/0.8 s) + min(0.5, 0.03 max(0, t - 10 s))            settle-in and long holds
k_set(t): tau = t - t_s; 1 if t_s < 0 or tau < 0; 1 - 0.7 SmoothStep01(tau/1.2 s) for tau < 1.2 s;
          0.3 for tau < 5.2 s; 0.3 + 0.85 SmoothStep01((tau - 5.2 s)/1.5 s) afterwards (ends at 1.15)
```

**Pressure** (UG computes it, same formula for the AI): `P = clamp(0.35 stakes + 0.25 gameBall + 0.15 hill + 0.10 crowd + 0.10 clock + 0.05 run, 0, 1)`, with stakes 0 practice / 0.3 friendly / 0.6 league / 1 final, and for a money game (side bet or hustle, Q6) `stakes = 0.6 + 0.4 clamp((bet/cash - 0.1)/0.4, 0, 1)` with the shooter's in-game cash on hand before the bet (all in: 1; the AI uses its own bankroll, so a rich hustler feels less; `MoneyGameStakes`, TUNING); gameBall = 1 when the shot can win the rack; hill = 1 when either player needs one rack; crowd = min(1, watchers/10); clock = elapsed/limit when a shot clock runs; run = min(1, run length/8). Hot-seat can switch pressure off for both players.

**Intoxication hook (Q2).** V1 ships alcohol as a prop, so `I = StrokeIntoxication(ProductConfig, level)` is 0 and `P_x = P`, `m_alc = m_alc,t = 1` exactly: every value of this spec is unchanged bit for bit. If the product owner later wants real drunkenness, `AlcoholMode::Mechanic` passes the game's intoxication level `I` in [0, 1] (from drinks and in-game time, UG) into the same formulas, for the player and the AI alike: one drink (`I <= I_c`) only calms (pressure gain, flinch and grip drop scale with `P_x`), more drinks add drift and tremor, both visible in the rendered cue (principle 1). Placeholders, inactive in V1: `I_c` 0.25, `c_calm` 0.5, `k_dr` 1, `k_tr` 1 (`HumanParams::Intoxication*`). Camera (UE 4.8): cosmetic V1 effects (bounded blur, vignette) run only while standing or walking, never while down on a shot and never on the cue, so they cannot degrade aiming without a visible cause; with the mechanic, any view sway while down must come from `SampleHand`'s drift (what you see is what hits).

### 3.5 Executed stroke (in this order)

```
R = CueBall.Radius;  L_bc = L_b + R           bridge to cue-ball centre along the cue
x_St, x_ST, x_SC = Steadiness, SpinTouch, SpeedControl;  NS = HumanParams.NoiseScale (0 for masked channels)
t = t_c

# watchable channels (drawn continuously, 3.7)
sigma_dr = sigma_drift L(x_St; 0.2) E(t) k_set(t) g^(1/3) m_br m_st m_head m_stick m_slip m_fat m_off m_alc
y_g  = NS sigma_dr D_lat(t)            grip lateral offset, + = shooter's right (e_r)
z_g  = NS 0.5 sigma_dr D_vert(t)       grip vertical offset, + = up
sigma_t = sigma_tr g k_set(t) m_fat m_off m_alc,t
tr_r = NS sigma_t T_lat(t);  tr_u = NS sigma_t T_vert(t)      tip tremor [m]

# per-shot channels (streak-guarded, truncated)
sA  = hypot(sigma_A L(x_ST;.25), kappa_off L(x_ST;.25) A_i R) m_st m_rush m_head m_fat m_off
sB  = hypot(sigma_B L(x_ST;.2),  kappa_off L(x_ST;.25) B_i R) m_st m_rush m_fat m_off
sTh = sigma_theta L(x_ST;.25) m_br m_st m_slip m_fat m_off
sV  = s_V L(x_SC;.3) (1 + 0.5 max(0, 1 - V_i/1)) m_rush m_jab sqrt(g) m_fat m_off
fl  = NS P_x phi_fl L_N u_Flinch;    bias = -NS b_grip P_x L_N

# warp (equipment, not scaled by NS), section 4.4
gamma = 4 s_w s_e / L_cue^2  (= k_w 4 s_w/L_cue, k_w = s_e/L_cue);  s_e = HumanParams.WarpSightLength
chi = WarpKnown ? 0 : 2 pi U01(HashKeys(MatchSeed, ShooterId, 10, CuePickupIndex))
dphi_w = gamma sin(chi);  dth_w = gamma cos(chi)

# pivot geometry (UE 5.4 steering, DERIVED): grip right -> cue rotates CCW about the bridge -> tip moves left
yaw = y_g / L_bg;  pitch = z_g / L_bg
phi_x   = phi_i + yaw + dphi_w
theta_x = max(ElevationFloor, theta_i + pitch + NS eps_El sTh + dth_w)      (ElevationClamped if the max bites)
A_x = A_i + (-yaw L_bc + tr_r + NS eps_A sA) / R
B_x = B_i + (-pitch L_bc + tr_u + NS eps_B sB + bias) / R
V_x = clamp(V_i (1 + NS eps_V sV) (1 - fl), 0, 12 m/s)
v_r = v_r,i - NS sigma_dr D_lat'(t) L_b/L_bg + NS sigma_t T_lat'(t)          tip velocity (lever = bridge-to-tip L_b),
v_u = v_u,i - NS 0.5 sigma_dr D_vert'(t) L_b/L_bg + NS sigma_t T_vert'(t)     output only (HF-11)
```

The envelope factors (`E`, `k_set`) are treated as constant when differentiating. `L_bc` is the right lever for the axis offset at the ball centre; the tip velocity uses `L_b`, the bridge-to-tip distance. `(v_r, v_u)` are not passed to the physics (HF-11). **Natural pivot (DERIVED, MOT B.7):** a yaw `delta` shifts the contact offset by `a = -delta L_bc/(R + r_tip)`, whose squirt turns the ball back by `(d alpha_sq/da) a`, so the net cue-ball direction error is `delta (1 - L_bc/L_p)` with `L_p = (R + r_tip)/(d alpha_sq/da)|_0`, `d alpha_sq/da|_0 = 2.5/(3.5 + m/m_e)`: 0.290 m house cue (m/m_e = 15), 0.368 m standard (20), 0.682 m LD (40). At the default `L_bc` = 0.229 m a house cue cancels 79 % of the drift yaw, a standard shaft 62 % and an LD shaft 34 % (HF-T09). This is real back-hand-English behaviour and falls out without extra code, but it makes the human layer depend on the cue: the budgets of 3.10 are checked per cue class.

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
TipTransverseVelocity = (v_r, v_u)                     (ExecutedStroke output only; not in the Strike)
Strike.TipTouchesCloth = the tapered cue body of the EXECUTED pose (tip rim, then shaft) reaches z < 0
                         (same geometry as UE 5.5; UE's cue-body model may override it in Sim mode)
PredictedMiscue = rho > MiscueLimit = mu/sqrt(1 + mu^2)                 (prediction; the core decides, MOT B.4)
gap, phi_c      = surface gap and cut angle to the first ball on the cue ball's path (swept corridor of width 2R)
DoubleHitRisk   = SeparationMargin(e_tip, m/M, rho) < 0  or
                  (RulesTolerances::Frozen < gap < CueSpec::FollowThroughDistance and phi_c < GrazeAngle)   (RUL F7 a/b)
PushRisk        = RulesTolerances::Frozen < gap <= RulesTolerances::FrozenEnvelope (5 mm)                 (RUL F7b/F8)
                  A cue ball frozen to that ball (gap <= Frozen, 0.1 mm) shot into it is exempt (F7/F8 envelope): no flag
ShaftContactCandidates: UE 5.5 clearance test (tapered cue r(s) = r_t + (r_b - r_t) s/L, no margin) of the EXECUTED
                  pose against OtherBalls: s* <= 0.025 m Ferrule, <= 0.74 m Shaft, else Butt; plus FloorBall if
                  ElevationClamped by a ball.  UE mesh collision stays authoritative in Sim mode (RUL F10).
```

A flat or small tip therefore miscues at its edge before the friction limit, and a mushroomed rim or a bare zone lowers `mu` exactly where the tip touches. For `q > 1` the dome formula `(a, b)` is only an approximation of the edge contact; it nearly always ends in the miscue branch, so the error stays inside a miscue. The only exception is a grip on the overhang of the flattest house tips (`w_tip` 11 mm, `r_dome` 20 mm: `rho` 0.275-0.287). **Miscues are never rolled**: they come from the executed `rho` (including any steering at contact), the chalk map and the tip geometry.

### 3.7 Visibility and rendering

`SampleHand(t)` returns the grip-hand offset `(y_g(t), z_g(t))`, tip tremor, the warp pose and the ramped per-shot offsets, evaluated with the same functions, so **what you see is what hits**. Per-shot channels (tip placement, elevation, speed, flinch, grip bias) ramp in as `SmoothStep01((t - t_fwd)/0.1 s)` from the start of the final forward stroke `t_fwd`: visible in the rendered cue and in replays, too late to correct with the mouse. **Aborted strokes:** `t_fwd` is the start of a committed forward stroke (commit held; in Pure mode, any forward stroke that comes within 0.1 s of contact at its current speed). If a stroke has shown any part of the ramp and then stops without contact, those draws are spent: `ShooterShotIndex` advances and the next attempt uses the next draw index (new candidates, 3.2). Otherwise a practice stroke would reveal the draw and let the player aim around it. The abort is in the input log, so this stays deterministic. The 1-euro filter stays visual-only (UE 5.4). Tremor at 8-12 Hz is rendered as sampled; aliasing is acceptable.

**Pixel check (DERIVED, 1440p, vertical FOV 50 deg, 1544 px/rad, eye 0.46 m from the tip, UE 4.2):** 1 px at the tip = 0.30 mm. At attribute 25 the channels give: vertical tip placement 1.5 mm, 5 px; elevation 0.4 deg (tip moves 1.4 mm about the bridge), 5 px; drift, seen on the shaft 8 cm under the eye, 5 px; speed 5 %, 6 px per frame at 2 m/s. Two channels stay below a pixel: lateral tip placement (0.20 mm, 0.7 px) and tremor (0.03 mm, 0.1 px at rest and 0.5 px at `g` = 5). Their tells are the replay close-up and stroke report (placement) and the heartbeat/breath audio plus optional haptics that scale with `g` (tremor).

### 3.8 AI: synthetic hand (input layer) + the same human layer

The planner outputs `(phi_p, theta_p, A_p, B_p, V_p, bridge)`. `SyntheticHand(profile, NoiseKey)` produces the `IntendedStroke` with the profile's input flaws, drawn from channels 20-27 (20-22 streak-guarded like 3.2, 23-26 plain):

```
phi_i = phi_p + bias_aim + sigma_aim eps20                   bias_aim: constant per character (vision centre; sign and size seeded, |bias_aim| <= profile value)
y_s   = sigma_steer eps21;  phi_i += y_s/L_bg;  A_i = A_p - y_s L_bc/(L_bg R)      (steering through the pivot)
v_r,i = -(y_s V_p / L_stroke) L_b/L_bg,  L_stroke = 0.15 m;   B_i = B_p;  theta_i = theta_p     (v_r,i: animation only)
V_i   = V_p (1 + sigma_spd eps22);  T_pause = pause_mean (0.7 + 0.6 U23);  a_c = U24 < p_jab ? -5 : 0
t_c   = 1.5 + 1.5 U25 s;  t_s = (uses Settle and P > 0.4) ? t_c - 2 s : -1;  HeadMoved = U26 < p_head
t_fwd = t_c - max(0.1 s, 2 L_stroke / V_p)                    (uniformly accelerated final stroke; rendering only, 3.7)
```

`U_c = U01(HashKeys(MatchSeed, RackIndex, ShotIndex, ShooterId, c, 0))`. `t_fwd` puts the AI's per-shot ramp into its final stroke, as for the player (without it the AI's tip would show its per-shot offsets for the whole time down). Then `ExecuteStroke` runs with the AI's own attributes and a situation built like the player's (pressure incl. money-game stakes, fatigue, intoxication). Profile values are in 5.5. Weak AIs plan with a simplified model (no throw, squirt, swerve, tilt, nominal ball masses) and assume perfect execution; strong AIs score candidates over `K` samples of their own noise using the rollout keys of 3.2 (`Purpose = 1 + s`, sample `s`), i.e. "percentage play".

### 3.9 Counterfactual diagnosis (after every shot)

A shot costs about 100 us (ARCH 1), so the game re-runs the missed shot up to 5 times with the same `NoiseKey`, a `ChannelMask` and state overrides, in this fixed order, and names the **first** change that turns the miss into a make. Each step is a single change against the real shot, not cumulative: (1) fresh chalk and no warp -> "equipment"; (2) level table and clean balls (`Slope = 0`, `NapPseudoSlope = 0`, `ClingFactor = 1`, no chalk marks) -> "table" (for example "the table rolls toward the jukebox"); (3) drift and tremor off -> "hand drift / nerves"; (4) per-shot channels off -> "tip placement / speed"; (5) all human channels off -> "human layer"; otherwise "input" (the player's aim, steering or speed). Without step 2, a miss caused only by roll-off would be blamed on the player's input. Output drives the mentor line (at most one per 3 shots, never during the opponent's turn), the replay close-up and, in Practice or Assisted, a stroke report such as "input 70 % / hand 20 % / chalk 10 %" (shares from the norms of the `Breakdown` contributions).

### 3.10 Routine-shot budget (release criterion) and fit

**Routine shot**: cue ball <= 1 m from the object ball, object ball <= 1 m from the pocket, cut <= 30 deg, medium speed, tip within 0.1 R of centre, comfortable closed bridge. With **perfect input** (no steering, exact aim) the human layer may cause at most:

| Case | Budget | House cue, m/m_e 15 (HF-S04..S06) | Standard own cue, m/m_e 20 | LD shaft, m/m_e 40 |
|---|---|---|---|---|
| B1: P = 0, no Settle, attributes 25 | <= 3 % | 1.07 % (CB direction sigma 0.041 deg) | 1.15 % | 3.71 % (trade-off, see below) |
| B1: P = 0, attributes 100 | <= 0.3 % | 0.00 % (0.0115 deg) | 0.00 % | 0.00 % |
| B2: P = 1, Settle held at contact, 25 | <= 3 % | 1.99 % | 2.20 % | 4.97 % |
| B3: P = 1, no Settle, 25 | <= 10 % | 6.62 % | 9.00 % | 21.7 % |
| Info: attributes 10 (tourist AI) | - | 6.55 % | - | - |
| Info: elevated bridge, stance 0.5, 25 | - | 16.9 % | - | - |

Values of v1.2 (streak-guarded draws, 3.2); with the v1.1 bags every entry was within 0.25 percentage points.

**The budgets must hold for every cue a player can hold at that profile.** The drift yaw is cancelled by squirt only near the natural pivot (3.5: 79 % for the house cue, 62 % standard, 34 % LD). The first fit covered only the house cue, and with the standard own cue B2/B3 came out at 3.6 % and 15.1 % (drift gain `sqrt(g)`). The drift pressure gain is therefore `g^(1/3)`; calm-play values are unchanged. The LD shaft (a reputation-tier unlock) misses the start-profile budgets. That is its disclosed trade-off (5.3, notebook: "the LD shaft forgives less of a wandering stroke"; accepted without an unlock gate, Q7). At Steadiness 40 it drops to 0.27 % (B1).

Budget model: the net cue-ball direction error is `phi_x - phi_i + alpha_sq(a)` (HF-S04..S06 use the house cue, m/m_e = 15); the pot fails when `|error| d_CO / (2 R cos 30 deg)` exceeds 2.0 deg (bar corner pocket at 1 m, effective half-window [DD-POCKET]). The release test **HF-B02** re-checks the budget with the full core (squirt, throw, swerve, cushions) over 10^5 shots per profile and per cue class (m/m_e 15 and 20; LD reported). The same model gives the miscue rate on a maximum draw (`b` aimed at -0.45, fresh chalk, V = 2 m/s): 15.2 % at attribute 10, 8.1 % at 25, 2.6 % at 40, 0 % from 60 up (HF-S05). **Misses from the human layer shrink with skill but never reach exactly zero on hard shots** (long thin cuts, awkward bridges).

### 3.11 Core contract changes (sign-off per ARCH 2, rule 11)

| Change | Owner | Effect on existing tests |
|---|---|---|
| **None to MOT B.4.** The earlier proposal "MOT B.4a `TipTransverseVelocity`" (friction cone tilted by the tip's sideways velocity) is withdrawn. It contradicts the impulse-along-the-axis model of MOT B.5, in which the tip carries no transverse momentum, and it overstated the effect about 15 times (HF-11, 9.2 item 1). A physically consistent V2 option would add the transverse push of the shaft end mass, `dv_CB = v_t / (m/m_e + 1 + 1/k)` for a centre hit (DERIVED, about 0.1 deg per 5 cm/s at 1 m/s), and needs a WP-1 derivation for off-centre hits | - | - |
| `CueSpec` fields are filled per stroke from `TipState` (`TipFriction`, `TipDomeRadius`, `TipRestitution`); `TipTouchesCloth` is set from the executed pose | WP-11 | none |
| New package `rb/Human/{NoiseHash.h, Skill.h, TipState.h, CueState.h, BallMarks.h, HumanModel.h, AiProfiles.h, Chores.h, Progression.h, Venue.h}` (ARCH 7.5; directory layout is WP-0's); depends on Core, Math, Equipment, `Physics/CueStrike.h` and, for the after-shot update, the diagnosis overrides and the venue condition, `Physics/BallBall.h`, `ShotResult.h`, `Simulator.h`; never included by the event loop, the shot record or the rules (checked by the root CMake) | WP-11 (+ WP-0 sign-off) | - |
| `SyntheticHand` + static check "AI builds strikes only via ExecuteStroke" (HF-B07) | WP-11 / AI | - |

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

**Calibration (DERIVED).** Chalking once and then hitting the same zone at `rho = 0.45`, the first miscue comes when `mu(c) < 0.45/sqrt(1 - 0.45^2) = 0.5039`, i.e. `c < 0.6156`, after `-ln(0.6156) n_c / severity = 0.483 n_c` hits (severity 1.005 at V = 2 m/s). Dr. Dave's no-rechalk test at near-maximum English counted Taom 4-6, Master 6-13, Predator 8, Blue Diamond 11, Kamui 15-17 and Magic Chalk up to 29 shots [DD-CHALK]. Fictional grades:

| Grade (fictional) | `n_c` | First miscue at rho 0.45, V = 2 m/s, no re-chalk | Reference | Unlock |
|---|---|---|---|---|
| "Rail Rat" bar cube (dried, cupped) | 10, cap 0.7 | 3rd hit (a fresh chalking only reaches c = 0.7, rho_max 0.465) | below Taom | free in bars |
| "Old Blue" standard | 18 | 10th hit | Master 6-13 | start |
| "Tensile" premium | 30 | 16th hit | Blue Diamond / Kamui | cash |
| "Glasshouse" elite | 45 | 23rd hit | Magic Chalk range | cash + reputation |

Premium chalk never raises `mu` above `mu_fresh`; it only slows the decay. Hardness factor `h`: soft 1.15, medium 1.0, hard 0.7 [DD-HARD].

**Chalking (the revolver rule).** One twist: `c_0 <- c_0 + max(0, cap - c_0) eta_0`, ring zones `c_z <- c_z + max(0, cap - c_z) eta_r`, with `eta_0 = 0.6 (1 - 0.5 glaze)`, `eta_r = (0.12 + 0.33 H_chalk)(1 - 0.5 glaze)(1 - 0.7 hollow)`; `cap` = 1 (own cube) or 0.7 (bar cube). `H_chalk` in [0, 1] is the chalking habit (sweep vs drill). In R mode the player's motion sets the twists and `eta_r` (sweep coverage measured from the input); in A/C/P mode the avatar chalks before every shot (good-player practice, [DD-CHALK]) with `n_tw = ceil((cap - min_z c_z)/0.15)` twists (0 when nothing is missing; `cap`, not 1, so the bar cube does not force useless twists) at `0.4 (1 - 0.3 H_chalk)` s each. DERIVED equilibrium for right English at `rho` 0.45 every shot, standard chalk: the struck rim zone settles at `c` 0.84 (`rho_max` 0.489) at habit 0 and 0.94 (0.505) at habit 1. With the bar cube it settles at 0.44 (0.418) and 0.60 (0.447): full English with bar chalk miscues even with auto-chalking, and the cupped cube shows why. Aborting keeps the twists already done. Dropped or left chalk is HF-63.

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
   k_cling,contact = k_venue + (max(k_chalk, k_venue) - k_venue) min(1, chi_1 + chi_2),  k_chalk = 2.5 [COL 2.1, TP A.14]
   (a full chalk spot on the contact gives the chalked-contact value 2.5 whatever the venue's ball dirt; the earlier
    product form gave 1.3 x 2.5 = 3.25 in the dive bar, above the cling value 2.5 that COL 2.1 takes from TP A.14;
    identical for k_venue = 1)
```

A random orientation puts a single 2.5 mm mark on the contact with an expected weight of 0.19 % (DERIVED, = `r^2/(4R^2)`); straight follow shots keep the mark on the travel great circle, so it comes back far more often, as players report [DD-CLING]. **Core additions (V2):** `SimBall::ChalkMarks`; the simulator integrates orientation of marked balls inside the loop (ARCH v1.2: with the orientation law of `Playback.h`, which the loop already uses, instead of moving it into WP-1; ARCH 15 row 28); `BallBallParams::ClingFactor` becomes `k_venue`, `BallBallParams::ChalkClingFactor` is `k_chalk`; switch `PhysicsParams::ChalkCling` (default off, so all COL tests are unchanged).

### 4.4 House cues and warp

Each venue seeds its wall rack once (`HashKeys(VenueSeed, rackSlot, ...)`): mass U{18, 19, 20, 21} oz; length 57 in (plus 48 and 52 in short cues near walls); bow `s_w` from the distribution in HF-30, direction random; tip: `w_tip` 11-13 mm, `r_dome` 12-20 mm, glaze 0.3-0.9, overhang 0-1 mm, 10 % loose; `m/m_e` = 15; `e_tip` 0.68-0.72 [EQP 8.2]. The same cue in the same slot has the same defects on every visit until the bar "replaces" it (career event).

**Warp (DERIVED).** A circular bow of sag `s_w` over length `L` has curvature `kappa = 8 s_w/L^2` and end slopes `4 s_w/L` relative to the chord. The strike direction is the tip axis, i.e. the local tangent at the tip. The player aligns the part of the cue they see, from the point under the eyes (distance `s_e` from the tip, UE 4.2: 0.35-0.55 m) to the tip; that chord is parallel to the tangent at its midpoint `s_e/2`. The error is therefore `gamma = kappa s_e/2 = 4 s_w s_e/L^2`, i.e. `k_w = s_e/L` = 0.31 at `s_e` = 0.45 m (`HumanParams::WarpSightLength`). Range: 0.14 if only the bridge-to-tip segment is sighted, 0.83 if the grip-to-bridge line is aligned. The earlier TUNING value 0.5 had no derivation. Bow direction `n_w = cos(chi) e_u + sin(chi) e_r`; the tip end points toward `-n_w`, so `dphi_w = gamma sin(chi)` (toward the left for `chi = +90 deg`) and `dth_w = gamma cos(chi)`. A 3 mm bow on a 57-in cue gives `gamma` = 0.148 deg, which turns the object ball of a 1 m routine pot by up to 3.0 deg (bow sideways; the pocket half-window is 2 deg, 3.10). A 1 mm bow turns it by up to 1.0 deg. Holding the bow up (`chi = 0`) turns the whole error into elevation, the trick experienced players use. `WarpKnown` becomes true when the shooter has roll-tested that cue and noticed the bow (R-mode test, or the automatic pickup test when `s_w` is at least the habit's threshold, HF-31); otherwise `chi` is fixed per pickup by channel 10. A hidden warp is therefore at most 1 mm even at habit 0, and the diagnosis (3.9 step 1) names it "equipment" after the first miss it causes.

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

These are the `k = 2/5` forms. Code uses the per-ball inertia factor `k` (ARCH 2 rule 10): rolling `G = g_t/(1 + k)`; sliding pursuit rate `mu_s g (1 + 1/k)`, `c_s = k/(1 + k)`, `L_c = v - c_s u`, and the position term `g_t t^2/(2(1 + k))` (5/14 for `k = 2/5`).

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
- **Error bound (DERIVED).** For the quadratic that matches `r` and `v` at the start and `r` at the end, `|r_seg(tau) - r_exact(tau)| <= (2/81) J Delta^3`, with jerk `J = c_s k |dx_hat/dt| <= c_s k |G| sigma_i / |x(Delta)|` and `|x(Delta)| >= |x_i| - (k + |G|) Delta`. The root rule makes this `<= eps_tilt`. The tail segment's deviation is `<= 4 c_s |x_i|^2/(k - |G|) <= eps_tilt`. Nodes are exact to the `lam` tolerance, so **errors do not accumulate** along the chain. The velocity mismatch at a node is `<= J Delta^2/6` (`J` already contains `c_s`); measured 0.3-0.6 mm/s at `eps_tilt` = 5e-5 m and 1.4 mm/s at 5e-4 m. Inside the last segments of a dying roll the segment velocity is off in direction by 2-11 deg once the ball is slower than 4 cm/s, and by up to 70 deg in the tail segment (below `x_tail`, about 1 mm/s). **Events inside a tilt segment** (ball-ball, cushion, pocket): the resolver takes the position from the segment (consistent with detection, at most `eps_tilt` from the exact path) and the velocity and spin from the exact pursuit state at the event time (one 1-D solve, as for a node). The approach test of COL 2.4 step 1 keeps the segment velocity, which was the one detection used. The segment velocity error then never enters an impulse. Measured maximum position error / `eps_tilt`: 0.30-0.95 over the test cases.
- **Cost.** Segments per phase (HF-T16): 0.3 m/s lag: 8 at `eps_tilt` = 5e-5 m; 0.5 m/s roll (5.1 s): 10; 1 m/s roll (10.2 s): 14; stun slide at 2 m/s: 3. Defaults: `eps_tilt` = 5e-5 m (game), 5e-4 m for AI rollouts (5-8 segments per roll), `RefreshMaxInterval` = 2 s. Weak AIs plan on a level table (knowledge knob, 5.5). Each refresh re-predicts that ball's slots (ARCH 8.3), so a tilted table adds about 10-30 prediction rounds per moving ball: the ARCH 1 budget (typical shot <= 100 us) must be re-measured with tilt on (add a PERF case in WP-10).
- **Why not freeze the direction?** Freezing `u_hat` for a whole 2 m/s stun on 3 mm/m misplaces the ball by 0.18 mm sideways (HF-T17); a dying roll turns by tens of degrees, where freezing errs by millimetres.

#### 4.5.4 Magnitudes

1 mm/m across the line: a 1 m/s roll ends 183 mm to the side after 5.1 m; a 0.3 m/s lag 16.5 mm after 0.46 m; along a 3 mm/m slope a 0.5 m/s roll goes 1.62 m downhill (+27 %) and 1.05 m uphill (-18 %) instead of 1.27 m. Pro installers level to about +-0.005 in over the bed [DD-ROLLOFF], i.e. well under 0.1 mm/m.

#### 4.5.5 Persistence

Per venue table: `Slope` seeded once (dive bar: magnitude U[0.5, 2.5] mm/m, direction uniform; pool hall U[0, 0.3]; arena U[0, 0.1]); optional nightly drift after bumps (V2: +-0.3 mm/m, visible coaster moving under a leg). The notebook records the discovered direction ("Table 2 rolls toward the jukebox") once the player has seen three slow balls curl the same way. Fairness (DERIVED): at 2.5 mm/m a 0.3 m/s lag drifts 42 mm and a 0.5 m/s roll ends 118 mm to the side, so the first table a new career plays on is seeded at <= 1 mm/m (17 mm and 46 mm), and steeper tables come later with a visible coaster.

#### 4.5.6 Nap (Later)

Rolling only: `G_eff = (5/7) g_t + zeta_n g n_nap` (a pseudo-slope; the pursuit solution stays exact) and `k_i = mu_r g (1 - eta_n v_hat_i . n_nap)`, frozen per chain segment (re-anchoring at each node keeps it consistent; freezing error `<= eta_n k |dbeta| Delta^2/2`, add `|dbeta| <= 0.05 rad` to the refresh rule when `eta_n > 0`). Defaults `zeta_n` = 2e-4, `eta_n` = 0.1, `n_nap = +x` (head to foot) [EQP 7], all EST. Worsted cloth: 0.

#### 4.5.7 Contract additions (WP-0 / WP-1 / WP-5 / WP-6a)

`PhysicsParams::TableTilt { Vec2 Slope = 0; double Tolerance = 5e-5; double RefreshMaxInterval = 2.0; Vec2 NapPseudoSlope = 0; double NapResistance = 0; }` (ARCH v1.2: `PhysicsParams::Tilt` of type `rb::TiltParams`, `Motion.h`; the venue table's slope and ball cling arrive through `rb::TableCondition` and `MakePhysicsParams(Spec, Condition)`); `ValidatePhysicsParams` checks `|s| <= 0.7 mu_r`; `MotionSegment` stores the pursuit data of its chain (`x_i`, `k`, `G`, `c_s`, end kind) (WP-1); `QueuedEventKind::TiltRefresh` and the exact re-anchor of events inside tilt segments (WP-6a); `Math/Scalar.h`: `Expm1` (WP-0). Detection (WP-5) is unchanged: segments stay quadratic. With `Slope = 0` every existing MOT/COL/VAL test is untouched (HF-B10).

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

Six execution attributes on 0-100, one noise family each (3.3): **Steadiness** (drift), **Speed Control**, **Spin Touch** (tip placement, elevation), **Bridge Stability** (bridge factor, slip threshold), **Stance**, **Nerve** (pressure gain, flinch, grip tension). A new career starts at 25; a touring pro sits at 85-95. Skill never decays. Every 10 points multiply a channel by the same factor, so progress feels even. Attribute numbers are hidden (decided, Q3): progression is fully diegetic (steadier stroke, mentor remarks, notebook); the `ProductConfig::Attributes` switch that would show them on the league card and one menu page stays only for a later change of mind. **Habits** (chalk sweep, auto re-chalk after spin shots, ball wiping, warp check, sleeve tuck, rack quality) grow by doing the chore well in R mode (+0.02 per good execution, saturating at 1) and then show as automatic behaviour. **Knowledge** is not a stat for the player: the notebook records facts on first experience ("The big bar ball draws less", "Table 2 rolls toward the jukebox").

### 5.2 XP sources (graded by core data)

| Source | Feeds | Rule (TUNING) |
|---|---|---|
| Long or thin pots made | Steadiness | `10 D`, difficulty `D` = 2 deg / (pocket half-window in OB degrees at that geometry), clamp [0.5, 5] |
| Leave quality after a make (AI evaluator rates the next shot vs a no-position baseline) | Speed Control | `20 max(0, improvement)` |
| Spin shots with rho >= 0.3, no miscue, intended draw/follow distance achieved within 20 % (core states) | Spin Touch | `8 rho / 0.3` |
| Power shots and breaks (balls to the rail, cue ball controlled) | Bridge Stability | 5-15 |
| Stretch, rail and over-ball bridge shots made | Stance | `10 d_s` |
| Makes at P >= 0.5 in real matches (league, tournament, money games) | Nerve | `15 P`; never from drills |
| Drills (stop-shot ladder, speed ladder, spot shot, line-up, draw gates) | targeted | tiered; first clear of a tier pays 10x |
| Matches, won or lost | all, small | losses pay 60 % |

Cost of the next point: `100 x 1.08^(x - 25)` XP, i.e. 25 -> 40: 2,715 XP, 25 -> 50: 7,311, 25 -> 85: 125,321, 25 -> 90: 184,725 per attribute. The cost doubles every 9 points while each point buys the same noise factor, so progress per hour slows steadily. **Pacing target (TUNING, fit with playtest telemetry):** about +15 points in the two most-used attributes after the first 10 h, and the first attribute at 85 after roughly 150 h. If the measured XP per hour misses this, change the 1.08 base or the XP amounts, never the noise curves of 3.3. Anti-grind: repeated near-identical shots (same binned CB/OB/pocket geometry hash within 20 shots) pay 50 % less each time; drills pay 25 % after a daily soft cap; no timers, energy or boosters.

### 5.3 Unlocks and equipment (physical sidegrades, fictional brands)

| Item | How | Effect | Trade-off |
|---|---|---|---|
| House cues | free | 4.4 | find a straight one |
| First own cue (used, from the bartender) | cash | straight, 19 oz, medium tip, `m/m_e` 20 | care: tip wear now matters |
| Tips soft / medium / hard | cash | retention, glaze rate, sound (4.2) | soft needs shaping, hard glazes |
| Chalk grades | cash | 4.1 | cost only |
| Tip tool (scuffer, shaper) | cheap | enables scuff and shape chores | over-shaping shortens tip life |
| Bridge glove | cash | V_b x1.15, sweat x0.2 | - |
| Low-deflection shaft | reputation tier + cash | `m/m_e` 20 -> 40 | aim habits change (natural pivot 0.68 m); forgives less of a wandering stroke (3.10: 3.7 % routine misses at Steadiness 25 vs 1.1 %; 0.3 % at 40), disclosed in the shop and notebook; no unlock gate (Q7) |
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
| Body/cue fouls (RUL F10), same for both players | live | live | ghosted | ghosted |
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
| Local hustler | ~600 | 65 all, Nerve 80 | 0.035, 0.02, 0.5 mm, 5 %, 0.5 s, 0, 0.05 | 0.95, 0.2 in money games, yes | 3-ball plans, sandbags until money is down (a visible choice; money games are in, Q6; off only with `MoneyGames::LeaguePrizeOnly`) |
| Road player | ~680 | 75 all | 0.025, 0.01, 0.35 mm, 4 %, 0.5 s, 0, 0 | 1.0, 0.9, yes | full safety and kicking game, K = 8 self-noise samples |
| Touring pro | 750+ | 90 all | 0.015, 0, 0.2 mm, 3 %, 0.6 s, 0, 0 | 1.0, 1.0, yes | full model, K = 16, uses Settle on pressure shots |

The AI reads only what the player could see (no future seeds; its rollout samples draw without the streak history, 3.2). Its flaws are drawn at animation scale (jab, loose bridge, skipped chalk), so opponents can be scouted. Hot-seat guests play at 50 in every attribute and gain no career XP (decided, Q4).

---

## 6. Test cases

Tolerances: "exact" = +-1 unit in the last listed digit (as MOT); integer and hash values bit-exact; process and stroke values 1e-9 relative (transcendental functions through `rb/Math/Scalar.h`). Common stroke setup **S0**: `R` = 0.028575 m; fresh standard tip (`r_dome` 0.0106, `w_tip` 0.01275, `mu` 0.6/0.35, all `c_z` = 1, no overhang); straight cue (`WarpKnown`); `IntendedStroke {phi 0, theta 3 deg, A 0, B 0, V 2 m/s, v_r = v_u = 0, t_c 2.5 s, no Settle, pause 0.4 s, a_c 0, no head move}`; situation closed bridge, `L_b` 0.20 m, `L_bg` 0.80 m, `d_s` 0, P = F = S = 0; key `{MatchSeed 0x5EED, Rack 0, Shot 0, Shooter 1, ShooterShot 0, Pickup 0, Purpose 0, Address 0}`.

### 6.1 Deterministic, exact

| ID | Input | Expected |
|---|---|---|
| HF-T01 Hash | `Mix64(0)`; `HashKeys(1,2,3)`; `HashKeys(0x5EED,0,0,1,5,0)` | `0xE220A8397B1DCDAF`; `0xCD8D705991914EA1`; `0x735A7BD1020B7AA3`, `U01` = 0.45059942105039663 |
| HF-T02 InvNorm | `InvNorm(0.975)`, `(0.02)`, `(0.5)`; `TruncNormal(0)`, `(1)` | 1.959963986; -2.053748909; 0; -2.500000003; +2.500000003 |
| HF-T03 Streak guard | seed 0x5EED, shooter 1, channel 5, draws n = 0..15 | eighths [7,5,3,0,4,5,1,4,7,6,7,0,5,5,2,2]; Sub 0 except n = 8 (Sub 2) and n = 11 (Sub 1); n=0: u 0.973406374675, eps 1.845629422572; n=3: eps -1.340126013891; n=8: u 0.962396260752, eps 1.713107401759; n=11: eps -2.169666181062; n=15: eps -0.347111524623; the history rebuilt from n = 0 equals the incremental one |
| HF-T04 Processes | S0 key, t = 2.5 s | `D_lat` = 0.230366244591, `D_lat'` = -3.986599586190; `T_lat` = -0.601466481787, `T_lat'` = 33.890273864740 |
| HF-T05 Full stroke, attributes 25 | S0 | `phi_x` 2.705488099e-4; `theta_x` 0.05293479862; `A_x` 0.01012217658; `B_x` 0.001521750879; `V_x` 2.034073630; `v_r` 0.001953103889; `v_u` -9.111677241e-4 (tip velocity, output only); `(a,b)` (0.007383310679, 0.001109994419); `mu` 0.6; no miscue. Channels: `y_g` 2.164390e-4, `z_g` 5.178315e-4, `eps_A` 1.845629423, `eps_B` 0.164127879, `eps_El` -0.01036599996, `eps_V` 0.340736300, `E` 1.043936934 |
| HF-T06 Pressure + Settle | S0 with P = 1, `t_s` = 0.8 s | `g` 5, `k_set` 0.3, flinch 0.072544; `phi_x` 1.387895872e-4; `theta_x` 0.05261956401; `A_x` 0.01086040465; `B_x` -0.04940815491; `V_x` 1.925576035 (drift gain `g^(1/3)`) |
| HF-T07 Attributes 100 | S0, all 100 | `phi_x` 5.410976197e-5; `theta_x` 0.05247124335; `A_x` 0.002165156180; `B_x` -0.001228559438; `V_x` 2.010222089 |
| HF-T08 Identity | S0, `NoiseScale` 0 | outputs equal inputs bit-exactly (`theta_x` = 3 deg, `A_x` = `B_x` = 0, `V_x` = 2), `mu` 0.6 |
| HF-T09 Pivot / natural pivot | `y_g` = 1 mm; yaw 1e-3 rad through MOT B.7 squirt | `yaw` 1.25e-3 rad, `A` shift -0.009998906; `L_p` = 0.289895 / 0.368245 / 0.681645 m for m/m_e 15/20/40; net/yaw at `L_bc = L_p` < 2e-4; at `L_bc` 0.228575 m: 0.211535 / 0.379294 / 0.664677 |
| HF-T10 Warp | `s_w` 3 mm, `L` 1.4478 m, `s_e` 0.45 m, S0 key, unknown roll; then `WarpKnown` | `gamma` 2.576182438e-3 rad; `chi` 5.442345308 rad, `dphi_w` -1.919780235e-3, `dth_w` 1.717894002e-3; known: `dphi_w` 0, `dth_w` 2.576182438e-3 |
| HF-T11 Swoop | S0 with `NoiseScale` 0, `A_i` = 0.658058 (`a` = 0.48), `V_i` = 1, `v_r,i` = 0 / +0.05 / -0.05 m/s | `Strike` bitwise identical in all three cases, `PredictedMiscue` false (`rho_max` 0.514496); only `TipTransverseVelocity` differs. Reference analysis (end-mass grip model, not a C++ test): friction demand in rho units 0.42515 / 0.42770 / 0.42265, cue-ball direction +0.0927 deg toward a +5 cm/s swoop (m/m_e 20) |
| HF-T12 Chalk decay | standard grade, pure right English `a` = 0.45, V = 2, repeated, no re-chalk | `q` 0.748235294, `beta` = pi (zone 4, weight 1), severity 1.005; after 8 hits `c` 0.639757125, `rho_max` 0.454283156; after 9 hits `c` 0.605016227, `rho_max` 0.448110250, so hit 10 miscues and hits 1-9 do not |
| HF-T13 Chalking | `c` = [0.5, 0.2, 0.9 x5], 3 twists, cap 1 | `H_chalk` 0: [0.968, 0.454822, 0.931853 x5]; `H_chalk` 1: [0.968, 0.866900, 0.983363 x5]; bar cube (cap 0.7, hollow 0.8), `H_chalk` 1, 1 twist: [0.62, 0.299, 0.9 x5] (no zone decreases; `H_chalk` 0 gives 0.2264 for zone 1) |
| HF-T14 Tip edge | `w_tip` / `r_dome` = 12.75/10.6, 12.75/8.96, 12.75/18, 11/18 mm | `rho_edge` 0.601415, 0.711496, 0.354167, 0.305556 |
| HF-T15 Tilt oracle, rolling | `mu_r` 0.01, g 9.80665; (a) `v0` (0.5, 0), `s` (0, 1e-3); (b) `v0` (1.0, 0.3), `s` (2e-3, -1e-3) | (a) `T_stop` 5.124727634 s, stop displacement (1.276273166, -0.045756497) m; at t = 2 s `v` (0.303903300, -0.010810351), `r` (0.803883823, -0.011963026). (b) `T_stop` 9.654203692; at t = 3.3 s `v` (0.645704992, 0.224066759), `r - r0` (2.714259691, 0.868289848). Both equal RK4 (2e5 steps) to 1e-9 |
| HF-T16 Secant chain | HF-T15 (a) with `eps_tilt` 5e-5 | 10 segments, nodes at t = 1.047533, 2.004048, 2.861368, 3.608756, 4.231390, 4.707615, 5.005814, 5.110593, 5.123343, 5.124728 s; max deviation 4.27e-5 m <= `eps_tilt`. Counts: `v0` 1 m/s: 14; 0.3 m/s: 8; `eps` 5e-4: 6 for (a) |
| HF-T17 Tilt, sliding and collinear | stun `v0` (2, 0), `w0` 0, `mu_s` 0.2, `s` (0, 3e-3); collinear `v0` (0.5, 0) rolling, `s` (-3e-3, 0) and (3e-3, 0) | slide ends at 0.291352841 s (level 0.291347489), `v` (1.428571429, -0.006122561), `r` (0.499460866, -0.001070292), 3 segments at 5e-5; frozen-direction error 0.178 mm. Downhill: 1 segment, `T` 6.489103173 s, 1.622275793 m (= v0/(k-|G|), v0^2/(2(k-|G|))); uphill: `T` 4.198831465 s, 1.049707866 m |

### 6.2 Statistical (seeded, so the counts are exact; tolerance +-2 counts for CRT differences)

| ID | Setup | Expected |
|---|---|---|
| HF-S01 Truncated moments | 80 000 streak-guarded draws, seed 0x5EED, shooter 3, channel 8 | mean -0.001307 (+-0.01), sd 0.954631 (theory 0.954597, +-0.005), max \|eps\| <= 2.5000001; 16 671 draws (20.84 %) needed a redraw, largest `Sub` 9 |
| HF-S02 Streak cap | first 4000 draws of HF-S01 | no eighth more than twice in any 8 consecutive draws (hence no run longer than 2); every eighth drawn 473-519 times (500 +- 30); 824 draws redrawn; every draw equals the draw of the history rebuilt from n = 0 |
| HF-S03 Process RMS | `D_lat`, `T_lat`, S0 key, 1 kHz over 600 s | 0.99894 and 1.00011 (1 +- 0.03) |
| HF-S04 Routine budget | budget model 3.10 (m/m_e 15), S0 except: 20 000 shots i = 0..19 999 with key {0xB00D, Rack i/20, Shot i, Shooter 1, ShooterShot i}, `t_c = 1.5 + 2.5 U01(HashKeys(99, i))`, V 1.5 m/s | attributes 25: 214 misses (1.07 %); 10: 1310; 40: 3; 60, 85, 100: 0 |
| HF-S05 Max-draw miscues | S0 except `B_i` = -0.61693 (`b` = -0.45), key {0xD4A3, Rack i/20, Shot i, Shooter 2, ShooterShot i}, `t_c = 1.5 + 2.5 U01(HashKeys(98, i))`, 20 000 shots | miscue = executed `rho` > `rho_max` (no swoop term): attributes 10: 3039 / 20 000; 25: 1617; 40: 516; 60, 85, 100: 0 |
| HF-S06 Pressure | as HF-S04, attributes 25, P = 1 | no Settle: 1324 (6.62 %); Settle (`t_s = t_c - 2 s`): 397 (1.99 %); P = 0 with Settle: 80 (0.40 %). Same with m/m_e 20: 1800 (9.00 %), 439 (2.20 %); m/m_e 40: 4337, 994, and 741 at P = 0 without Settle |
| HF-S07 Cling weight | one mark, strength 1, radius 2.5 mm; contact at 0 / 0.05 / 0.1 rad | `k_cling` 2.5 / 2.082045 / 1.406170 (`k_venue` 1); random orientation mean weight 0.0019 +- 0.0002 |

### 6.3 Behavioural acceptance

| ID | Requirement |
|---|---|
| HF-B01 | Same input log, seeds and state give an identical `CueStrikeInput` and `ShotResult` hash in UE and rbsim (ROB-10 style). |
| HF-B02 | Release blocker: 3.10 budgets hold with the full core, 10^5 shots per profile and per cue class (m/m_e 15 and 20; LD reported against its disclosed values). |
| HF-B03 | A miscue happens only when the core's criterion fires (MOT B.4); no other code path sets `Miscue`. |
| HF-B04 | Revolver rule: missing coverage 40 % -> 3 auto twists (about 1.2 s at habit 0); 10 % -> 1 twist; aborting after 2 of 5 twists keeps exactly 2 twists of coverage. Ball clearing time grows linearly with balls left; coin insertion is per coin. |
| HF-B05 | Skipping a chore (A/C/P) gives the habitual result bit-exactly regardless of real-time speed; R mode can match or beat it, never beyond the habit-1 result (principle 5). |
| HF-B06 | Every miss yields a diagnosis within 3 s; constructed cases: bare zone -> "equipment", slow cut missed only because of `Slope` -> "table", drift-only perturbation -> "hand drift", zero human layer with steered input -> "input". |
| HF-B07 | Static check: AI code constructs `CueStrikeInput` only through `ExecuteStroke`. |
| HF-B08 | At attribute 25, every channel at 1 sigma moves the rendered tip or shaft by >= 1 px at 1440p in the Eyes preset (automated capture), except the two sub-pixel channels of 3.7 (tremor, lateral tip placement), which must trigger their audio/haptic and replay tells instead. |
| HF-B09 | rbsim round-robins reproduce 2:1 game odds per 100 rating points within +-5 %. |
| HF-B10 | With `Slope = 0` and `ChalkCling` off, all MOT/COL/VAL/RUL tests pass unchanged. |
| HF-B11 | The same venue seed gives the same house-cue defects, ball set and table slope on every visit; a replay from an older save reproduces with its stored state. |
| HF-B12 | Playtest KPI: counterfactual share of misses caused by the human layer <= 35 % at the start profile, <= 10 % at attributes 85. |
| HF-B13 | A stroke that shows part of the per-shot ramp (3.7) and stops without contact advances `ShooterShotIndex`; the next attempt shows different draws, and the replay of the input log reproduces both. |
| HF-B14 | Events inside a tilt segment use the exact pursuit velocity: a slow roll (< 4 cm/s) kissing a ball at `Slope` 2 mm/m gives the same object-ball direction as an RK4 reference within 0.1 deg. |

---

## 7. Product-owner decisions

All seven questions are answered (2026-09-26: Q1, Q4, Q5, Q7 in v1.2, log 9.4; Q2, Q3, Q6 in v1.3, log 9.5). Each answer is one value of `rb::human::ProductConfig` (`rb/Human/Progression.h`) or `HumanParams::StreakGuard`, so a later change of mind is a configuration change, not a code change.

| # | Question | Status | Decision, or default while open |
|---|---|---|---|
| 1 | **Stratified bags** or plain independent draws? A fixed bag of 8 is predictable: after 7 revealed draws (replays and the stroke report show them) the 8th stratum of every bagged channel is known, and a tool reading the replay header could tell the player "next shot: tip lands low". | **Decided** | Neither: **streak-guarded independent draws** (the verifier's alternative). An independent draw is redrawn (`Sub` + 1, deterministic) whenever its eighth already occurs twice in the shooter's last 7 draws of that channel (3.2). At most 3 eighths are ever excluded, so the next draw is never known, and "no eighth more than twice in any 8" still holds. HF-S02 was restated, HF-T03 and HF-S01 recomputed, and every other draw-dependent value (HF-T05..T07, HF-S04..S06, 3.10, 5.3) recomputed with `Tools/reference/human-factors/streak.py` and `recompute_v12.py`. `HumanParams::StreakGuard` (default on) replaces `Stratify`; rollout keys never read the streak history. |
| 2 | **Alcohol** as a mechanic in the dive bar (one beer calms Nerve, three hurt Steadiness), or cosmetic only for V1 because of PEGI/ESRB descriptors? | **Decided** (v1.3) | **Cosmetic only for V1** (`AlcoholMode::CosmeticOnly`, default): drinks are props, `StrokeIntoxication` returns 0, and the UE camera effects (blur, vignette) run only while standing or walking, never while down on a shot or on the cue (HF-20). The owner may want real drunkenness later, so the hook exists now: `StrokeSituation::Intoxication` (3.4) calms the pressure gain at one drink and adds drift and tremor beyond it, for the player and the AI alike; `AlcoholMode::Mechanic` switches it on with placeholder effect sizes (`HumanParams::Intoxication*`) to be specified then. Adding the mechanic is a switch plus tuning, no core redesign; with `I` = 0 every value of this spec is bitwise unchanged. |
| 3 | **Attribute numbers:** show them on the league card and one menu page, or keep progression fully diegetic (stroke steadier, mentor remarks)? | **Decided** (v1.3) | **Hidden**: progression is fully diegetic (`AttributeVisibility::Hidden`, default; 5.1). `LeagueCardAndMenu` stays as a switch only. |
| 4 | **Hot-seat:** guests at 50 in every attribute without career XP, or both players use career profiles? | **Decided** | Guests play at **50 in every attribute** and earn **no career XP** (`HotSeatGuestAttribute` 50, `HotSeatGuestEarnsXp` false). Fatigue stays off in hot-seat (HF-16); pressure can be switched off for both players (3.4). |
| 5 | **Default chores:** Full for the first visit of each venue and Brisk afterwards, or Brisk from the start? | **Decided** | **Full on the first visit of each venue, Brisk afterwards** (`FirstVisitChores`, `ReturnVisitChores`); the player can change the setting at any time (principle 9). |
| 6 | **Money games** with in-game cash (hustling, side bets): acceptable given the simulated-gambling descriptors, or limited to league prize money? | **Decided** (v1.3) | **Yes: side bets and hustling with in-game cash** (`MoneyGames::SideBetsAndHustling`, default). In-game cash is never bought with real money and never cashed out (5.3: nothing bought with real money changes cash); a possible simulated-gambling content descriptor is accepted. A money game feeds the pressure scalar through its stakes, from 0.6 for a small bet to 1 when half the shooter's cash or more is on the table (`MoneyGameStakes`, 3.4), counts as a real match for Nerve XP (5.2), and enables the hustler's sandbagging and loose racks (5.5, 4.6). `LeaguePrizeOnly` stays as a switch (e.g. for a rating region). |
| 7 | **Low-deflection shaft trade-off:** accept that it raises start-profile routine misses (3.10) as a disclosed, physically real trade-off, or gate its unlock behind Steadiness 40, where the difference disappears? | **Decided** | **Accepted as a disclosed, physically real trade-off**, no unlock gate (`LowDeflectionMinSteadiness` 0). Shop and notebook disclose it: 3.7 % routine misses at Steadiness 25 against 1.1 % with the house cue, 0.3 % at 40 (3.10, v1.2 values). |

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
- [DD-SWOOP] Dr. Dave, swoop/swipe stroke (a swoop is equivalent to a straight stroke with a changed direction and contact point; the tip cannot add much sideways force): https://drdavepoolinfo.com/faq/stroke/swoop-swipe/
- [DIGICUE] DigiCue stroke metrics (jab, follow-through, backstroke pause, tip steer): https://github.com/nataddrho/DigiCue-USB/blob/master/README.md
- [WADA] beta-blockers prohibited in-competition in billiards: https://www.drugs.com/wada/p1-beta-blockers.html
- Coin-op separation and returns: https://electronics.howstuffworks.com/question495.htm ; https://www.billiardsforum.com/pool-table-repair/valley-7450-coin-op-pool-table-restoration-magnetic-cue-conversion-project
- Design references: Arma 3 OPREP weapon sway (predictable sway instead of random penalties): https://dev.arma3.com/post/oprep-weapon-sway-fatigue ; Bodycam "Locked and Loaded" overhaul: https://www.escapistmagazine.com/news-bodycam-locked-and-loaded-overhaul-pc-shooters/ ; Tarkov Mag Drills (skill improves information precision): https://escapefromtarkov.fandom.com/wiki/Mag_Drills
- Project specs: MOT B.3-B.8 and impl. notes 1-2, 10; COL 2.1, 3.9.5; EQP 6.2-8.5; RUL 3.2, F5, F7-F11; UE 4.8, 5.3-5.5; ARCH 2, 5.3, 8, 11.

---

## 9. Verification log

Adversarial verification, 2026-09-26. **Method:** a second Python implementation, written from this text alone: hash, Acklam InvNorm, bags, processes, `ExecuteStroke`, chalk map, pursuit closed form, secant chain and budget Monte Carlo. Two independent oracles were added: an RK4 integrator of the full sliding/rolling equations with 3-D spin, and an end-mass grip model of the tip impact (TP A.31 assumptions, 3x3 impulse solve). Every tilt equation was re-derived from Newton-Euler with the MOT 0 conventions (`u = v + R z_hat x w`). Rules claims were checked against rules.md 0, 3.2-3.6 and 12; WP names against architecture.md 2, 3, 11 and 17; sources against Dr. Dave's chalk-comparison and swoop pages. The scripts are in the verifier's scratchpad, not in the repo; they can be ported to `Tools/reference/` with the author's.

### 9.1 Confirmed without change

- **Noise:** HF-T01..T04 and HF-S01..S03 reproduced to every printed digit (S01 sd 0.955868; S02 blocks, cap and runs; S03 RMS).
- **Stroke:** HF-T05 (apart from `v_r`, `v_u`), HF-T07 and HF-T08 reproduced exactly. Pivot sign re-derived from MOT B.2: grip to `+e_r` gives `phi` up and the tip to the left. Natural pivot `L_p` = 0.289895 / 0.368245 / 0.681645 m and HF-T09 are correct.
- **Chalk:** HF-T12 (hit 10), the grade table (3rd / 10th / 16th / 23rd hit), HF-T13 (`H_chalk` 0 and 1) and HF-T14 all check out. `mu` 0.60 fresh to 0.35 bare lies inside MOT B.4's 0.6 to "0.35-0.4". Rim 0.30 and ferrule 0.20 lie below that range on purpose, because those surfaces are not chalked leather (EST). "Premium chalk only slows decay" agrees with Dr. Dave: all chalks perform alike when applied before every shot [DD-CHALK].
- **Tilt:** equations of 4.5.1 re-derived: sliding `du/dt = g_t - (7/2) mu_s g u_hat`, rolling `(5/7) g_t - mu_r g v_hat` with static friction `(2/7) m g_t`, and `L_c` drifting at `(5/7) g_t` in sliding and in flight. The pursuit solution (`t`, `x`, `X`, `T_stop`) was re-derived via `w = tan(beta/2)`. HF-T15 and HF-T17 were reproduced, and RK4 (2e5 steps) agrees to 3e-14 m, also 0.01 s before the stop. The secant chain reproduces HF-T16 (same 10 node times, max 4.27e-5 m) and the counts 14 / 8 / 6; sliding needs 3 segments (a draw needs 4). `(2/81) J Delta^3` is the exact Hermite remainder bound; its Peano kernel keeps one sign, so the bound also holds for vectors. The tail bound is valid, and the measured error / `eps_tilt` is 0.30-0.95. Magnitudes in 4.5.4 and the 0.178 mm frozen-direction error were confirmed.
- **Budget and cling:** HF-S04 and the elevated-bridge row were reproduced within +-1 count (CRT). HF-S07 and the mean mark weight `r^2/(4R^2)` = 0.0019 are correct.
- **Rules:** HF-60 (all-ball fouls, RUL F10), HF-63 (chalk on the rail is an outside object, F5), HF-75 (F10/F11) and the Pure "any tip contact is a shot" versus commit-hold (UE 5.4) are consistent.
- **Determinism:** all noise is a pure function of the keys and the input log. Replays store the final `CueStrikeInput`, so CRT differences in `rb::Cos`/`rb::Log` (ARCH 11, O-2) cannot break them; only re-execution (HF-B01) needs the same platform, as ARCH 11 already states.

### 9.2 Corrections applied

1. **Swoop physics (HF-11, 3.1, 3.5, 3.6, 3.11, HF-T11, HF-S05, HF-B03, HF-B10).** The proposed MOT B.4a tilted the friction cone by the tip's sideways velocity (`d' = V d + v_r e_r + v_u e_u`). MOT B.5's impulse along the axis is the limit in which the tip carries no transverse momentum, so the sideways velocity cannot act there. In the end-mass grip model (TP A.31), 5 cm/s at 1 m/s and `a` = 0.48 changes the friction demand by +0.0026 in rho units at m/m_e 20 (+0.0032 at 15), not +0.043, and turns the cue ball 0.093 deg toward the swoop. That matches Dr. Dave: a swoop equals a straight stroke with a changed direction and contact point [DD-SWOOP]. The core change was withdrawn: `(v_r, v_u)` is now output only, and a V2 formula is noted. HF-S05 was recounted: 3034 / 1566 / 538 / 0.
2. **Lever of the tip velocity** changed from `L_bc` to the bridge-to-tip `L_b` (3.5, 3.8); new HF-T05 `v_r`, `v_u`.
3. **Routine budget fitted for the house cue only (3.3, 3.5, 3.10, 5.3, HF-T06, HF-S06, open question 7).** The first own cue (m/m_e 20, loses natural-pivot cancellation) gave 3.60 % (B2) and 15.1 % (B3), breaking the spec's own release criterion. The LD shaft gave 3.77 / 7.49 / 33.7 %. The drift pressure gain changed from `sqrt(g)` to `g^(1/3)`: house cue 6.55 / 1.82 %, standard 8.78 / 1.98 %; calm-play values are unchanged. The LD result is documented as a trade-off.
4. **Warp coupling (HF-31, 4.4, HF-T10).** `k_w = 0.5` had no derivation. It is now `k_w = s_e/L`, from the chord of the visible cue against the tip tangent: 0.31, so `gamma` = 0.148 deg for a 3 mm bow (was 0.237). `WarpSightLength` was added.
5. **Chalk-mark cling (4.3).** The product form reached 1.3 x 2.5 = 3.25 in the dive bar, above COL 2.1's cling value. It is now an interpolation between `k_venue` and 2.5; HF-S07 is unchanged.
6. **Tilt velocity error (4.5.3, 4.5.7, HF-B14).** The node mismatch bound double-counted `c_s`. Measured mismatch is 0.3-0.6 mm/s, but in the last segments the segment velocity is off by 2-11 deg of direction, and by 70 deg in the tail. Events inside a tilt segment now take the velocity from the exact solution. A PERF re-benchmark was added.
7. **Rules.** HF-62: a scoop is not a foul under WPA 2025 (RUL F9). HF-61: the frozen-ball exemption. `PushRisk` flagged the legal frozen shot and ignored grazes; it was rewritten from RUL F7/F8 with `RulesTolerances::Frozen`/`GrazeAngle`. `TipTouchesCloth` is now set from the executed pose, so the AI and rbsim see scoops. HF-71: coin-op locked balls conflicted with WPA spotting and re-racks; the bartender now unlocks the machine.
8. **Architecture references.** WP-10 is ARCH's validation package, so this package is WP-11. Sign-off is per ARCH 2 rule 11; "2.8" was the FP rule. WP-6 became WP-6a. A note maps the `k = 2/5` constants to the inertia factor (ARCH 2 rule 10).
9. **Determinism.** `Purpose << 32` added to a 32-bit `ShooterId` overflows, so the key is now built as a 64-bit value. All K rollout samples shared one key, so they were identical; now `Purpose = 1 + s`. A practice stroke could reveal the per-shot draw and let the player aim around it; a revealed draw is now spent (3.7, HF-B13).
10. **Diagnosis (3.9, HF-B06)** blamed "input" for misses caused by roll-off; a "table" step was added.
11. **Visibility (principle 7, 3.7, HF-B08).** At 1440p (0.30 mm/px at the tip), tremor (0.1 px at rest) and lateral tip placement (0.7 px) cannot meet the 1-px rule. The spec now gives a pixel budget and names the non-visual tells for these two channels.
12. **Fairness and time.** R results are capped at the habit-1 result, so the ritual is not compulsory in a long career. Fatigue runs on in-game hours, never real session time. Hidden warp is bounded by the automatic roll test (1 mm at habit 0) and the diagnosis. The vision-centre drill comes in the first session. Other changes: lamp stop, separation faults capped at one per night, the IK never creates a body foul, the AI uses the same body colliders, ghosting applies to both players, and the Imperfections slider matches the presets. The first career table is seeded at <= 1 mm/m. XP costs are now given with a pacing target.
13. **Chalk details (4.1, HF-T13).** The twist count used 1 instead of `cap` (the bar cube forced useless twists). When A mode chalks was unspecified; the avatar now chalks before every shot, and the equilibrium coverage is given. Calibration 0.485 -> 0.483 `n_c`. The HF-T13 bar-cube case needs `H_chalk` 1 (0.299; `H_chalk` 0 gives 0.2264).
14. **InvNorm.** A coefficient note was added: a mistyped `c4` moves the tails by about 1e-5. The verifier made exactly that slip, and HF-T02 caught it.

### 9.3 Remaining doubts

- **Bags:** a fixed bag of 8 was predictable from revealed draws. Resolved in v1.2 by the streak guard (Q1, 3.2, 9.4).
- **Miscue criterion:** it remains MOT's calibrated friction-cone heuristic. Worn-leather `mu` 0.35 and rim/ferrule 0.30/0.20 have no primary data (MOT open question 7).
- **Budget model:** it ignores throw, swerve and cushions. With the standard cue, B3 is 9.00 % against 10 % (v1.2 draws; 8.78 % with the v1.1 bags), a thin margin, so HF-B02 with the full core may force a further refit. The dive-bar default oversized cue ball (R 30.16 mm, 221 g) is not in any test setup; its budget numbers are unknown.
- **Warp `k_w`:** it depends on how players actually sight (0.14-0.83); playtest.
- **Nap (4.5.6):** Mathavan et al. found no measurable nap effect on snooker cloth (MOT A.9). The bar-cloth pseudo-slope is unsourced; calibrate before shipping.
- **XP:** no XP-per-hour estimate exists, so the pacing target cannot be checked yet.
- **Tilt performance** against the 100 us typical-shot budget is unmeasured.

### 9.4 Product-owner decisions (v1.2, 2026-09-26)

- **Q1: streak guard instead of bags.** New oracle `Tools/reference/human-factors/streak.py` (the guard of 3.2) and `recompute_v12.py`, which feeds the new draws into the unchanged v1.1 stroke and budget oracles. Before switching, the v1.1 values were reproduced with the old bags (HF-T03, HF-T05..T07 to every printed digit). Changed: HF-T03 (restated), HF-S01 (mean -0.001307, sd 0.954631; 20.84 % of the draws redrawn, largest `Sub` 9), HF-S02 (restated: the cap and the pure-function property instead of aligned blocks, which no longer exist), HF-T05..T07 (per-shot channels; drift, tremor and warp are unchanged), HF-S04..S06 and the 3.10 table (every budget still holds; B3 with the standard cue 9.00 % against 10 %), HF-S05 (3039 / 1617 / 516 / 0), the LD figures of 3.10, 5.3 and Q7 (3.71 %, 0.27 % at 40). Unchanged: HF-T01, T02, T04, T08..T17, HF-S03, HF-S07 (no per-shot draws).
- The draw sequence of one `(MatchSeed, shooter key, channel)` is a pure function of `ShooterShotIndex`: the recursion from draw 0 is the definition and `NoiseHistory` its incremental cache (the oracle checks both agree). The synthetic-hand channels 20-22 use the same guard. Rollout keys take the plain candidate `Sub` 0 without history, so the AI samples the unconditional distribution. With `StreakGuard = false` the plain draws are keyed by `ShooterShotIndex` (v1.1 keyed them by `ShotIndex`, so an aborted stroke would have repeated its draws).
- **Q4, Q5, Q7 decided; Q2, Q3, Q6 open** with the defaults of section 7 behind `ProductConfig` switches (ARCH 7.5). Answered in v1.3 (9.5).

### 9.5 Product-owner answers and architecture integration review (v1.3, 2026-09-26)

- **Q2 alcohol:** cosmetic only for V1, with an explicit hook for a later mechanic: `StrokeSituation::Intoxication` `I` (3.1, 3.4, 3.5: `P_x`, `m_alc`, `m_alc,t`), set by `StrokeIntoxication(ProductConfig, level)`, which is 0 under `CosmeticOnly`. All factors are exactly 1 at `I` = 0, so every expected value of section 6 is unchanged (no oracle rerun needed; `stroke.py` has no intoxication term, which equals `I` = 0). Camera note in HF-20 and 3.4. The attribute-based `EffectiveAttributes` / `AlcoholParams` of v1.2 were removed: one mechanism, acting on the stroke where it is visible.
- **Q3 attribute numbers:** hidden (5.1). **Q6 money games:** in, with in-game cash only; money-game stakes rule in 3.4 (`MoneyGameStakes`, TUNING), Nerve XP from money matches (5.2), hustler behaviour on by default (5.5).
- **Streak fallback (3.2):** the floating-point mapping of v1.2 could land in an excluded eighth after rounding (counter-example in `streak.py`, `fallback_float_fails`); replaced by exact integer arithmetic (`streak.py` and `rb/Human/NoiseHash.h` agree; 1e5 random cases land in an allowed eighth). Probability 2.4e-14 per draw, so no expected value changes; HF-T03, HF-S01 and HF-S02 were re-run with the new oracle and are unchanged.
- **Watchable processes (3.2):** keyed by `ShotIndex` alone, the drift and tremor repeated exactly on every get-down of the same shot, so a player (or a tool reading the replay) could address once, watch `D(t)`, stand up and shoot the second address at a known calm moment. The shot key now carries `AddressIndex` in its high word (`NoiseKey::AddressIndex`, `ProcessShotKey`); identical to v1.2 for the first get-down, so HF-T04 and HF-S03 are unchanged.
- **Synthetic hand (3.8):** `t_fwd` defined, so the AI's per-shot ramp happens in its final stroke like the player's.
- **Re-verification:** `recompute_v12.py` (draws, stroke, budget) reproduces every value of HF-T03, T05..T08, HF-S01, S02, S04..S06, the 3.10 table and the figures of 5.3 and section 7 to the printed digits.
