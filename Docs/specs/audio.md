# RAW BREAK - Audio Design Spec

| Field | Value |
|---|---|
| Document | `Docs/specs/audio.md` |
| Owner | audio workstream |
| Status | Draft v1.1 (2026-09-28): research, design and a working prototype of the physics-driven synthesis (`Tools/audio/click_synth.py`); adversarial review applied the same day (Review log at the end). No engine work yet. |
| Target | Unreal Engine 5.8.3, Windows x64, 48 kHz output (5.8 default backend on Windows: WASAPI, `AudioMixerModuleName=AudioMixerWasapi` in `BaseWindowsEngine.ini`) |
| Related | [ue5-realism-plan.md](ue5-realism-plan.md) section 8 (**UE 8.x**; this spec replaces 8.2-8.5 in detail), [human-factors.md](human-factors.md) (**HF**), [equipment.md](equipment.md) (**EQP**), [physics-collisions.md](physics-collisions.md) (**COL**), [../ue-architecture.md](../ue-architecture.md) (**ARCH-UE**), [venue-dive-bar.md](venue-dive-bar.md) (**VEN**: layout, cast, coin-op anchors A1-A4, gully splines), [ui-ux.md](ui-ux.md) (**UIX**: audio settings 13.8, subtitles, UI sound tokens 4.6), [../trailer-plan.md](../trailer-plan.md), [../decisions.md](../decisions.md) |
| Prototype | [Tools/audio/click_synth.py](../../Tools/audio/click_synth.py), reference renders and golden data in `Tools/audio/out/ref/` ([README](../../Tools/audio/README.md)) |

Legend (same as the other specs): **DERIVED** = worked out from first principles or the core's own constants, derivation
shown or in the prototype; **ESTIMATE** = a starting value to be fitted against recordings; **VERIFY** = a fact about an
engine API or a licence that must be re-checked in the installed 5.8.3 build / the current licence text before relying on
it; **PO** = product-owner decision needed (collected in section 15).

---

## 0. Summary - the decisions

1. **Every table sound is driven by the simulator.** `ShotResult::Events` (impacts, pockets, tip contacts) and the ball
   tracks (rolling, sliding, airborne) are the only source of table sound. Because a shot is fully pre-simulated before
   its playback starts, all its sounds are scheduled on the audio clock ahead of time, **sub-sample exact**. The prototype
   renders impacts 0.5 ms apart exactly 24.00 samples apart (realism-plan test E5) and, because 0.5 ms is a whole number
   of samples at 48 kHz, also checks non-integer spacings: 24.4992 and 5.0016 samples are reproduced within 0.001
   sample by the exact and by the runtime renderer (AU-T08). Every table in a room has its own shot clock (6.6).
2. **Ball-ball and ball-cushion sounds are synthesised from physics, not sampled.** Contact force = the core's own
   Hertz + Tsuji law (`K = 8.0587e8 N/m^1.5`, `e = 0.95`); the sound field = the exact radiation of a sphere
   (rigid-body "acceleration noise", Koss & Alfredson 1973) plus Lamb's elastic modes; the cushion adds an ESTIMATE modal
   model of the rail and cabinet. Every speed, ball pair (oversized 221 g bar cue ball, worn 155-167 g balls), contact
   angle and listener position gives its own, correct click, with no sample repetition, which matters most in the break
   (79 impacts, 24 of them less than 1 ms apart, in the dive-bar break render).
3. **What the prototype found (section 3.7):** the click is a dry, un-pitched 0.8-4.3 kHz transient (spectral centroid
   1.9 kHz at 0.25 m/s to 2.6 kHz at 12 m/s), **no audible ring**: the lowest elastic mode of a phenolic ball is at
   20.3 kHz. Loudness rises 24.3 dB per decade of speed (the realism plan's v^1.2 law, confirmed within 0.3 dB/decade),
   but the timbre brightens only 16 % from 1 to 12 m/s, not x1.64 as the plan's `cutoff ~ v^(1/5)` rule assumed, so that
   rule is dropped in favour of the synthesis itself (the high-frequency skirt above 5 kHz still rises 4-17 dB relative
   to the peak from 1 to 12 m/s, which the synthesis produces by itself). The click is a **dipole**: exactly in the
   plane perpendicular to the line of centres it is 27-29 dB quieter than on the line (34 dB in the observer example,
   which is also farther away), -10 dB at 60 deg, -21 dB at 75 deg (3.6). Radiated energy is about 1e-4 of the kinetic
   energy, the order measured for plastic spheres (Riner & Petculescu 2010: "on the order of 100 ppm").
4. **The runtime form is cheap and exact.** A contact pulse is one universal curve (self-similar Hertz-Tsuji) scaled in
   time and force; each ball radiates through four fixed 512-tap FIR kernels (one per Legendre order), weighted per
   listener by `P_n(cos th)/r`. The C++ algorithm is implemented bit-for-bit in the prototype (`runtime_render`, golden
   vectors in `golden_runtime_48k.json`) and matches the exact model within 0.02 dB rms (on-axis) / 0.20 dB (side-on)
   in 1/6-octave bands.
5. **Everything that is not a table impact comes from recordings:** royalty-free libraries first (Sonniss #GameAudioGDC,
   Freesound CC0 / CC BY, Pixabay), our own recordings in a real bar as the quality target, ElevenLabs SFX (paid plan
   only) to fill gaps. **Voice lines**: Higgsfield TTS (`text2speech_v2`, `qwen_audio_tts`, `seed_audio`; not the
   "game pipeline only" models). **Music**: diegetic only (jukebox, menu), generated by the PO in Gemini (Lyria 3.5)
   from the six prompts in section 11.3, after the PO has checked the Gemini terms of the day.
6. **Unreal:** a pure C++ DSP core (`RawBreakAudioDsp`, no UObjects, golden-tested against the Python prototype) inside
   per-emitter `USynthComponent`s (`ISoundGenerator`) for the table; MetaSound Sources for foley, ambience, crowd,
   jukebox and voices; Audio Modulation (control buses and mixes) for the mix states and ducking; a submix tree with a
   per-venue Convolution Reverb; Audio Gameplay Volumes for rooms; Audio Insights for metering (its output meter reports
   momentary / short-term / integrated loudness). Quartz is **not** used for the physics events (it quantises to
   musical grid points, not arbitrary times). The sample-exact anchor was checked against the 5.8.3 engine source (5.1).
7. **Realism rules for the mix:** physical levels internally (Pa at 1 m), a wide dynamic range by default, no
   non-diegetic music during play, no sound without a visible or plausible cause, NPC chatter unintelligible unless it is
   addressed to the player. Physical levels cannot be played back 1:1: a click's peak is ~28 dB above its sound-level
   reading, so a break at the breaker's ears (126 dB SPL peak) and a 76 dBA room cannot both fit into 0 dBFS at a
   normal listening loudness. A plan-time gain envelope per dynamic-range mode (4.2) keeps soft shots physical (in
   Wide everything up to a 1 m/s click at 1.2 m) and compresses only the loud part; the prototype renders all three
   modes of the dive-bar break (true peak -1.2 to -1.5 dBTP, integrated -29 / -24 / -20 LUFS with a 76 dBA room).

---

## 1. Inputs: what the simulator and the game provide

### 1.1 Shot data (`rb/Physics/ShotResult.h`)

| `ShotEventType` | Audio use | Fields used |
|---|---|---|
| `CueStrike` | tip hit (AU-01), miscue (AU-02 when `Flags & Miscue`) | `Time` (0), `A` (struck ball), `Normal` (cue direction), `NormalSpeed` = tip speed V, `NormalImpulse` J, `Value` = tip speed after impact |
| `TipContactBegin` / `TipContactEnd` | tip contact duration T_tip (`End - Begin`, 0.8 ms for the 9 m/s break of `break9.json`) | `Time`, `Feature` (strike index) |
| `TipRecontact` | double hit / push tick (AU-03) | `Time`, `A`, `NormalImpulse` |
| `BallBall` | click (AU-20), synthesised | `A < B`, `Normal` (A->B), `NormalSpeed` v_n, `NormalImpulse`; **skip** when `Flags & Pressing` (resting contact inside a CLI island) or v_n < 3 mm/s |
| `BallCushion` | cushion thud (AU-30), synthesised | `A`, `Feature` = CushionId (selects the rail emitter), `Normal` (contact -> centre), v_n |
| `BallJaw` | pocket-facing knock (AU-31), synthesised, harder contact | `Feature` = PocketId, `SubFeature` (jaw side, element) |
| `BallRailTop` | ball on the wooden rail cap (AU-32) | `Feature`, v_n |
| `BallSlate` | landing of a hopping ball (AU-24) | `NormalSpeed = -v_z`, `SubFeature` = bounce index |
| `BallPocketEnter`, `BallPocketRim`, `BallLiner`, `BallPocketExit`, `BallPocketed` | pocket entry, rattle in the throat, liner hit, drop (AU-33..AU-35) | `Feature` = PocketId, `Value`/`NormalSpeed` |
| `BallOffTable` | ball leaves the table: the ball is handed to engine physics (decisions 2026-09-28); its floor bounces and roll are driven by Chaos hit events, not by the core (AU-25) | `Feature` = OffTableReason |
| `BallExternalContact` | ball hits the lamp (rare, AU-10) | `Feature` = Lamp |
| `MotionTransition` | switches the rolling/sliding/spinning texture (AU-22/23) | `From`, `To` |
| `BallAirborne` / `BallLand` | mutes rolling while airborne | - |
| island, observer and diagnostic events | none | - |

Continuous sounds use the **trajectories**, not the events: `BallTrack::Segments` give the exact speed, slip and motion
state at any time (`Playback.h`), so rolling noise is evaluated at the audio clock itself, frame-rate independent.
`ShotResult::CueTips` give the cue's follow-through (cue body sounds, a butt that hits a wall).

### 1.2 Player-model and equipment state (HF, EQP)

| State | Audio effect |
|---|---|
| `TipState` chalk coverage at the contact zone (HF-21) | chalked "thock" vs dry "click": a bare zone shortens the tip contact (x0.8, ESTIMATE) and adds a 3-6 kHz leather tick layer |
| tip hardness (HF-27), loose tip / cracked ferrule (HF-28) | contact time from the tip preset (0.8 ms hard ... 1.5 ms soft, MOT B.20); loose tip adds a 2-3 ms buzz (ESTIMATE 1.1 kHz, Q 12) |
| bridge slip above V_b (HF-12) | cue rattle against the fingers / rail at the forward stroke (library layer) |
| sweat / glove (HF-17), shaft grime (HF-33) | squeak of the shaft sliding through the bridge on practice strokes |
| ball set (EQP 6.2-6.3, HF-42/43): oversized, magnetic, worn, polyester | per-ball radius, mass and material in the synthesis (section 3.4) |
| dead rail spot (HF-52) | that rail zone uses a lower-Q, heavier-damped rail bank and the lower `e_c` the physics already applies |
| pressure `P` (HF-15), tremor gain `g` (HF-05), settle (HF-06) | heartbeat and breathing layers (AU-59), audible tells for the two sub-pixel noise channels (HF-B08) |

### 1.3 Game events (non-physics)

Chores (HF-70..HF-79), match flow (rack, call, foul, frame won), money games (bets, payouts), NPC behaviour (opponent
waiting spot, sharking, reactions), mentor diagnosis (HF 3.9), camera preset, menu. They trigger foley, voice and crowd
sounds through MetaSounds (section 8.4).

---

## 2. Sound event catalogue

Method codes: **SYN** = physics synthesis (section 3); **LIB** = royalty-free library samples (section 9); **REC** = own
recording (target quality, replaces LIB when available); **AI** = ElevenLabs SFX (gap filler); **TTS** = Higgsfield voice.
Round robins: number of distinct takes per trigger (random without immediate repeat). Priority: V1 = first dive-bar
release, V2 = update, L = later. Emitter: where the sound radiates from (section 6.1).

### 2.1 Cue and stroke

| ID | Sound | Trigger / driver | Method | Mapping | Emitter | Prio |
|---|---|---|---|---|---|---|
| AU-01 | Tip strike | `CueStrike` | SYN (ball part: sin^1.5 pulse of duration T_tip and impulse J, radiated by the cue ball) + LIB layer "leather tip thock" (5 speed layers x 6 RR, cross-faded by V) + SYN cue body (longitudinal mode c/2L ~ 1.5 kHz, ESTIMATE) | level and layer from V; tip hardness selects the layer set (soft / medium / hard / phenolic break tip) | cue ball + cue tip | V1 |
| AU-02 | Miscue | `CueStrike` with `Miscue` | SYN with T_tip x0.6 and the reduced impulse + LIB "tip slip" (sharp tick + 30-60 ms scrape, 8 RR) + chalk-dust puff (VFX only) | scrape length ~ tangential slip speed | cue tip | V1 |
| AU-03 | Double hit / push | `TipRecontact` | SYN second tip pulse (small J) | J | cue tip | V1 |
| AU-04 | Scoop (tip digs into the cloth) | `TipTouchesCloth` of the executed stroke | LIB cloth scuff (6 RR) | V | cue tip | V1 |
| AU-05 | Practice strokes | stroke component while down | LIB shaft-through-bridge whisper (skin or rail), per stroke, pitch/level from the stroke speed; sweat/glove variants (HF-17) | stroke speed, bridge type | bridge hand | V1 |
| AU-06 | Bridge slip rattle | HF-12 slip | LIB (6 RR) | V - V_b | bridge hand | V1 |
| AU-07 | Cue handling: lay on the table, lean on the wall, cue rack, cue case zip, joint screw | game | LIB/REC | - | hands / cue | V1 |
| AU-08 | Butt hits wall / stool / lamp (HF-32, HF-78) | cue sweep contact | LIB wood knocks, lamp: metal shade clonk + chain creak loop while swinging | contact speed | contact point / lamp | V1 (lamp V2) |

### 2.2 Balls

| ID | Sound | Trigger / driver | Method | Mapping | Emitter | Prio |
|---|---|---|---|---|---|---|
| AU-20 | Ball-ball click | `BallBall` | **SYN** (section 3) | v_n, both balls' R/m/material, contact axis vs listener | both balls | V1 |
| AU-21 | Break / clusters | many `BallBall` incl. island contacts | SYN, same model; pressing contacts skipped | per contact | balls | V1 |
| AU-22 | Rolling on cloth | segment state Rolling | SYN: band-passed noise (60-700 Hz), rms level 40 dB SPL at 1 m for 1 m/s, amplitude ~ v (ESTIMATE); cloth preset scales it (worn bar cloth +3 dB, fresh worsted -2 dB) | speed from the segment at audio time | ball | V1 |
| AU-23 | Sliding hiss / spinning in place | state Sliding / Spinning | SYN: 1.5-6 kHz noise at 0.35 x the rolling amplitude x slip speed (ESTIMATE) | slip speed | ball | V1 |
| AU-24 | Slate landing (jump, hop after a hard hit) | `BallSlate` | SYN: contact T(1 m/s) 0.45 ms (cloth on slate, ESTIMATE), `e_slate` 0.6, + bed modal bank | -v_z | ball + table bed | V1 |
| AU-25 | Ball off the table: floor bounce, roll away, knock against stool bases (caption "[ball bounces across the floor]", UIX 2.4) | `BallOffTable` hands the ball to Chaos; `OnComponentHit` / rigid-body collision notifies of the loose-ball actor give normal impulse and surface (physical material) per bounce; rolling speed from the body's velocity | SYN ball radiation with a floor contact (VCT on concrete: T(1 m/s) 0.30 ms, e 0.55, no structure bank; ESTIMATE) per hit + LIB chrome stool-base knocks (6 RR) + rolling noise on VCT (200-3000 Hz, +12 dB over cloth at equal speed, tile-joint ticks every 0.305 m; ESTIMATE) | normal impulse -> v_n = J / m | loose-ball actor | V2 |
| AU-26 | Chipped ball tick per revolution (HF-44) | rolling segment, ball flag | SYN tick train at v/(2 pi R) | speed | ball | L |

### 2.3 Cushions, rails, pockets, ball return

| ID | Sound | Trigger / driver | Method | Mapping | Emitter | Prio |
|---|---|---|---|---|---|---|
| AU-30 | Cushion thud | `BallCushion` | **SYN**: contact T(1 m/s) 2.5 ms (ESTIMATE), core `e_c(v)` law, ball radiation + rail/cabinet modal bank (section 3.5) | v_n, table preset (bar box vs pro), dead-rail zone | ball + rail emitter of that CushionId | V1 |
| AU-31 | Jaw / facing knock | `BallJaw` | SYN: harder contact T(1 m/s) 1.2 ms, facing restitution `k_f e_c` | v_n | ball + pocket emitter | V1 |
| AU-32 | Rail cap (wood) | `BallRailTop` | SYN: T(1 m/s) 0.35 ms, e 0.5, rail bank | v_n | ball + rail | V1 |
| AU-33 | Rattle | sequence of `BallJaw` / `BallPocketRim` events | emerges from AU-31 per event (no special sample) | per hit | pocket | V1 |
| AU-34 | Pocket drop, pro table (leather / net) | `BallLiner`, `BallPocketed` | SYN liner hit (T 1 ms, e 0.3) + LIB leather-pocket thump (5 layers x 6 RR) + ball-on-ball clack when the pocket already holds balls (SYN, 0.3-1 m/s) | drop speed, balls already in the pocket | pocket | V2 (pool hall) |
| AU-35 | Coin-op drop into the gully | `BallPocketed` on `TABLE_7FT_BAR` | LIB drop into the rubber gully boot (8 RR) + SYN liner hit | drop speed | pocket | V1 |
| AU-36 | Gully run to the trap | after AU-35 | LIB rolling loop in a plastic channel (4 variants) modulated by speed (0.6-1.0 m/s), 1.5 s from the foot pockets to 3 s from the head pockets (VEN 3.3), muffled by the cabinet; prototype: band-passed noise with seam bumps | path length along the VEN 3.3 gully splines | point moving along the gully spline | V1 |
| AU-37 | Ball into the trap row | end of AU-36 | SYN click (0.6 m/s) against the balls already in the trap, low-passed 1.2 kHz (cabinet) | number of balls already in the trap | trap row, foot end (VEN 3.3) | V1 |
| AU-38 | Cue ball return (scratch, HF-72) | cue ball pocketed on a coin-op table | LIB: roll to the size-gauge separator, knock against the gauge bar, then 2-6 s to the return cup on the wall side (VEN 3.1 A3); oversized ball = lower rumble (-2 semitones); magnetic separation (other venues) = a soft magnetic clack | ball type | gully spline -> separator -> return cup A3 | V1 |
| AU-39 | Object ball stuck in the cue ball chute (HF-73) | seeded fault | LIB knock + bartender key (AU-45) | - | cabinet | V2 |

### 2.4 Coin-op mechanics and chores (HF-70..HF-79)

| ID | Sound | Trigger | Method | Notes | Prio |
|---|---|---|---|---|---|
| AU-40 | Quarter into the coin slide | chore step, per coin | REC/LIB metal: coin drop into the slide slot (10 RR) | 1 s per coin (HF-70); Low Bridge: $1.50 = 6 quarters (VEN 1.1), anchor A1 | V1 |
| AU-41 | Push the slide in / spring return | chore | LIB heavy steel slide (0.6 s push with a resistance ramp) + spring clunk (6 RR) | coin-count jingle when the coin box is full (per venue); anchor A1 | V1 |
| AU-42 | Balls released | after AU-41 | LIB cascade: the balls rumble down and clack into the tray, 1.8-2.5 s (VEN 3.3; 4 variants) | the most recognisable bar-box sound: REC target; anchor A2 | V1 |
| AU-43 | Balls taken from the tray | per ball | SYN clicks (0.2-0.5 m/s) + hand foley | HF-74 | V1 |
| AU-44 | Racking: balls onto the spot, triangle placed, tapping the rack tight, lift | chore steps | SYN ball-ball clicks for ball contacts (low speed, v from the hand animation) + LIB triangle (plastic vs wooden) | rack tightness (HF-55): loose racks "click" when pushed | V1 |
| AU-45 | Bartender unlocks the table (HF-71/73) | chore | LIB keys, lock, coin-box door | anchor A4 (wall-side coin door) | V1 |
| AU-46 | Quarters on the rail, stacking (HF-76) | challenge queue | LIB coin on wood (6 RR) | - | V1 |
| AU-47 | Chalkboard score, 8-ball pocket marker (coaster) | game | LIB chalk on slate board, cardboard coaster | - | V1 |
| AU-48 | Spotting a ball | game | SYN (ball on cloth, 0.1 m/s) + hand | - | V1 |

### 2.5 Chalk, hands, body

| ID | Sound | Trigger | Method | Mapping | Prio |
|---|---|---|---|---|---|
| AU-55 | Chalking | per twist (HF-22), 0.4 s | REC/LIB squeaky twist (12 RR), dry "scratch" when the cube is cupped (HF-23), paper wrapper crinkle on pickup | cube state, drill vs sweep | V1 |
| AU-56 | Chalk cube on the rail / pocket | put down | LIB (6 RR) | - | V1 |
| AU-57 | Towel wipe of cue ball / shaft (HF-40, HF-33) | chore | LIB cloth rub | 2-4 s | V2 |
| AU-58 | Hand on cloth, bridge set, fingers on the rail | stance | LIB soft cloth taps (8 RR) | - | V1 |
| AU-59 | Breathing, exhale on Settle, heartbeat | HF-05/06/15 | LIB breathing set (in-ear perspective, subtle), heartbeat (60-130 bpm from pressure P, level from the tremor gain `g = 1 + P (g_max - 1)`) | the tremor tell of HF-B08 must scale with `g`, so no hard gate: heartbeat level `L = L_max + 20 log10((g - 1)/(g_max - 1))`, inaudible below g = 1.25, L_max = room level -6 dB (in-head sound, not physical); breath while down always (quiet), exhale on Settle; setting *Heartbeat & breath tells* (UIX 13.8); captions "[heartbeat]" | V1 |
| AU-60 | Clothes rustle, sleeve on cloth (HF-60 tell) | movement, sleeve contact | LIB | - | V1 |
| AU-61 | Drink: glass on the rail / table, sip, glass knocked over (HF-79) | game | LIB | - | V1 (knock L) |

### 2.6 Footsteps and furniture

| ID | Sound | Trigger | Method | Notes | Prio |
|---|---|---|---|---|---|
| AU-65 | Footsteps | animation notifies | LIB per surface x shoe (sneakers, boots): dive bar = VCT tile on concrete (VEN 2.2), with a tacky peel on lift on the sticky patches near the bar (VEN material mask), rubber anti-fatigue mats in the bartender aisle (VEN E05); wood (Kneipe), carpet (pool hall), concrete (basement); 12 RR each | speed from the walk speed; the player's own steps are quieter and body-conducted (low shelf +3 dB) | V1 |
| AU-66 | Bar stool scrape, sit, swivel squeak | NPC and player | LIB | - | V1 |
| AU-67 | Leaning on the table: wood creak, rail knock | stance IK | LIB | never while a shot plays (it would suggest a foul) | V2 |

### 2.7 Room ambience per venue (section 12 has the per-venue maps)

| ID | Layer | Method | Level target (dive bar, at the table, ESTIMATE) | Prio |
|---|---|---|---|---|
| AU-70 | Room tone (air, distant street) | LIB/REC stereo bed, 2-4 min seamless | 38-42 dBA when empty | V1 |
| AU-71 | HVAC / ceiling fan | LIB loop, positional | 45 dBA near the vent | V1 |
| AU-72 | Beer cooler / fridge hum with compressor cycles (on 8-15 min, off 5-10 min, start/stop clunks; VEN 10) | LIB loops + start/stop one-shots | 44 dBA at 2 m | V1 |
| AU-73 | Ice machine drop (behind the storage door E19, every 4-7 min, VEN 10), bottle clinks, tap pour, dishwasher | LIB one-shots, scheduled by a bar-activity script | peaks 60-70 dBA at the bar | V1 |
| AU-74 | Neon sign buzz (transformer 120 Hz + harmonics in the US, 100 Hz in Germany; a faint electrode sizzle on the sign with the dead letter, which stays visually steady per VEN 4) | SYN (hum = sum of harmonics with jitter) + LIB sizzle | 35-40 dBA at 1 m | V1 |
| AU-75 | TV (sports broadcast), unintelligible | LIB crowd/announcer bed filtered through a TV speaker IR, no real broadcasts or brands | 55 dBA at 3 m | V1 |
| AU-76 | Door open/close with street burst (traffic, rain on wet nights): street layer +12 dB while the door E02 is open (VEN 10), draft curtain rustle | LIB | - | V1 |
| AU-77 | Restroom hand dryer, flush (distant, occluded) | LIB | - | V2 |
| AU-78 | Soft-tip dart machine (VEN E09 / M03): dart thuds on the plastic board, pull-outs, machine beeps and a generic synthetic announcer ("Double!", fictional voice, no brand jingles), coin drop, an original attract-mode jingle every ~10 min when idle (VEN 10; own composition or generated with the jukebox music, never a real machine's jingle) | LIB + TTS for the announcer | 60-65 dBA peak at 2 m | V1 |
| AU-79 | Cash register / card reader beep | LIB | - | V1 |

### 2.8 Crowd

| ID | Layer | Method | Notes | Prio |
|---|---|---|---|---|
| AU-82 | Walla (unintelligible) | LIB walla beds sized by occupancy: 0-5 patrons = individual murmurs at seats (spatial one-shots), 5-20 = 2-3 beds + one-shots, 20+ = dense bed | never intelligible unless addressed to the player; GDPR: no own recordings of guests' voices | V1 |
| AU-83 | Reactions: "ohh" on a rattle, applause/whoop on a money ball, groan on a scratch, laughter | LIB group reactions (small groups, bar scale, not stadium), 6-10 RR each | gain = crowd attention (how many watch) x stakes; delayed 150-400 ms after the cause (people react) | V1 |
| AU-84 | Other tables (pool hall ~8 tables; any venue with more than one table) | the AI regulars' **real simulated shots** (decisions 2026-09-28): each table's own `ShotResult` drives the same synthesis at the table's audio LOD (6.6); their barks and calls are ducked chatter, captioned only when directed at the player (UIX 2.4) | - | V2 |

### 2.9 Jukebox, music, voice, UI

| ID | Sound | Method | Section | Prio |
|---|---|---|---|---|
| AU-90 | Jukebox (VEN S7/E10: a 1996 CD jukebox): coin, selection buttons, CD carousel whir and disc load between tracks (3-6 s gap), the music through its speakers | LIB mechanics + music (section 11) played through a speaker IR | 11 | V1 |
| AU-100..106 | Voice: opponents, bartender, mentor, regulars, crowd call-outs, referee (arena) | TTS (section 10) | 10 | V1 |
| AU-110 | Menu and UI sounds: the UIX 4.6 tokens (chalk tap, cue tip on chalk, paper rustle, tick, light switch, wooden knock) on the UI bus, cash counting for money games; authored in dBFS (focus tap -30 dBFS), not physical; **no UI sound during a live stroke** (UIX 4.6) | LIB/REC | UIX 4.6 | V1 |
| AU-111 | Menu music | music (section 11) | 11 | V1 |

Every audible tell has a caption ("[tip slips]", "[heartbeat]", "[crowd gasps]"). Audio emits caption events; they are
shown by the UI's `URbSubtitleSubsystem` (UIX 4.2, 13.10) with speaker names; background walla is never captioned.

---

## 3. Physics-driven synthesis (ball-ball, cushion, slate, pocket liner, tip)

### 3.1 Why synthesis for these sounds

- The parameter space is continuous and large: normal speed 0.003-15 m/s, 16 x 16 ball pairs with different masses and
  radii (dive-bar sets: 155-167 g, the oversized 221 g / 60.3 mm cue ball), contact axis vs listener (the click is
  strongly directional), cushion vs facing vs rail cap.
- A break produces dozens of contacts within a few milliseconds (dive-bar render: 79 audible impacts, minimum onset gap
  1 us, 24 gaps below 1 ms). Samples triggered per contact would pile up identical transients; synthesis sums the
  physically correct pulses.
- Replays, slow motion and the trailer need the same shot rendered offline, deterministically, as separate stems.
- It removes the largest licensing and repetition risk from the most frequent sound in the game.

### 3.2 Contact force (DERIVED; the core's law)

The simulator resolves clusters with a Hertz contact and Tsuji damping (COL 3.9.3):

```
m* d'' = -F,   F = max(0, K d^1.5 + eta d^0.25 d'),   eta = alpha_T(e) sqrt(m* K)
K = 8.0587e8 N/m^1.5 (two standard balls, Marlow's data via Alciatore TP B.29);  alpha_T(0.95) = 0.036893
K for other pairs = (4/3) sqrt(R*) / ((1 - nu1^2)/E1 + (1 - nu2^2)/E2),  R* = R1 R2 / (R1 + R2)
```

With `d0 = (m* v^2 / K)^0.4` and `t0 = (m*^2 / (K^2 v))^0.2` the equation becomes `x'' = -(x^1.5 + alpha x^0.25 x')`
for every speed, mass and stiffness: **every Hertz-Tsuji pulse is one universal curve** (the prototype checks this to
0.2 %). The runtime therefore stores one shape table per restitution (`Tools/audio/out/ref/hertz_shape_e095.json`,
129 points) and two constants:

```
T = tau t0,  F_max = phi K d0^1.5,  F(t) = F_max shape(t / T),  J = (1 + e) m* v
e = 0.95: tau = 3.23516, phi = 1.10912 (undamped: tau = 3.21808, the classic 3.2181; phi = 1.14326)
```

| v_n [m/s] | 0.25 | 0.5 | 1 | 2 | 4 | 8 | 12 |
|---|---|---|---|---|---|---|---|
| contact time T [us] (std pair, e 0.95) | 436 | 380 | 331 | 288 | 251 | 218 | 201 |
| peak force [N] | 175 | 402 | 923 | 2121 | 4872 | 11193 | 18210 |

The undamped contact times reproduce COL 3.9.3 exactly (378 / 329 / 238 / 207 us at 0.5 / 1 / 5 / 10 m/s). Marlow's
measured 284 us at 1 m/s would need `K ~ 1.16e9`; the audio follows whatever `K` the physics uses, so a later
recalibration of `K` changes both consistently.

Other contacts use the same law with an effective stiffness chosen from a contact time at 1 m/s
(`K = sqrt(m^2 / (v (T/3.2181)^5))`, struck side fixed, m* = ball mass) and the core's restitution:

| Contact | T at 1 m/s | K [N/m^1.5] | Restitution | Status |
|---|---|---|---|---|
| ball-cushion (cloth on gum rubber) | 2.5 ms | 1.0e7 | core `e_c(v)`: 0.97 up to 1 m/s, 0.90 at 3, 0.655 at 10 | ESTIMATE (1.3-5 ms; the core's CLI `k_c = 1e6 N/m` gives 1.3 ms) |
| ball-facing (hard rubber) | 1.2 ms | 6.3e7 | `k_f e_c` | ESTIMATE |
| ball-rail cap (wood) | 0.35 ms | 1.4e9 | 0.5 | ESTIMATE |
| ball-slate (cloth on slate) | 0.45 ms | 7.4e8 | `e_slate` 0.6 | ESTIMATE |
| ball-liner / gully boot | 1.0 ms | 1.0e8 | 0.3 | ESTIMATE |
| cue tip-ball | T_tip from the event log (0.8-1.5 ms, MOT B.20), sin^1.5 pulse with the logged impulse J | - | - | DERIVED from the core |

All ESTIMATE contact times are measurable from a recording (the pulse width is visible in the waveform) or from 5000 fps
video (section 3.8).

### 3.3 Radiation of a ball (DERIVED)

Koss & Alfredson (1973) showed that the sound of two colliding elastic spheres is the time-correct sum of what each
sphere radiates on its own, driven by the Hertz force. Chadwick, Zheng & James (2012) generalised this "acceleration
noise" for rigid bodies in games. For a sphere it is analytic. Expand the ball's surface normal velocity in Legendre
polynomials about its contact axis (unit vector from the centre to the contact point); order `n` radiates exactly

```
p_n(r, th, w) = -i rho0 c V_n(w) P_n(cos th) h_n(kr) / h_n'(ka)            (V_n = surface normal velocity of order n)
              = -rho0 c A_n(w) P_n(cos th) h_n(kr) / (w h_n'(ka))          (A_n = i w V_n, acceleration; the code form)
                                                  (e^{+iwt}; h_n = spherical Hankel of the 2nd kind; Euler i w rho0 v = -dp/dr)
```

with `k = w/c`, ball radius `a`, and `th` = angle between the ball->listener direction and the contact axis.
(Draft v1 wrote the first form without the factor `-i`, a 90 deg phase error; the prototype always used the correct
acceleration form, checked by the incompressible-limit selftest.)

- **Order 1 = rigid-body translation.** `W_1 = U` = the ball's velocity change along the axis, `dU/dt = -F/m`. Far field
  `p ~ rho0 a^3 cos(th) / (2 c r) d^2U/dt^2` for ka << 1, but at the click's dominant 2-3 kHz `ka ~ 1-1.5`, so the exact
  Hankel ratio matters (the prototype uses it). Low-frequency check: `rho0 a^3 / (2 r^2)` exactly (selftest).
- **The two balls** of a contact accelerate in opposite directions, one ball diameter apart. On the line of centres the
  two dipoles add up with a delay of about 2a/c = 0.17 ms, which peaks near c/(4a) = 3 kHz; side-on they cancel. This is
  why the click is bright and directional.
- **Elastic modes** (orders 0, 1, 2, 3) enter through their modal acceleration `w^2 F / (M_k (w_k^2 - w^2 + 2i zeta w_k w))`
  (pole force, unit pole radial displacement, section 3.4). Below resonance they still radiate their quasi-static
  "squash" (a quadrupole for n = 2), which is all a side-on listener hears.
- **Cloth plane:** one image source per ball mirrored in z = 0 with reflection 0.9 and a one-pole cloth loss at 8 kHz
  (ESTIMATE).
- **Near field:** exact through `h_n(kr)`; in the runtime only order 1 needs it: `(1 + 1/(ikr))`, i.e. add
  `(c/r) * integral(p_1 dt)` (a leaky integrator), relevant below c/(2 pi r) = 550 Hz at 10 cm (macro replay camera).
- **Propagation delay** `(r - a)/c` is applied per ball (up to 9 ms across a 9-ft table; clicks at the far end arrive
  later, as in reality).

### 3.4 Elastic modes of the balls (DERIVED; material partly ESTIMATE)

The ball material is derived from the core's Hertz constant: `E = 2 (1 - nu^2) 3K / (4 sqrt(R/2)) = 8.87 GPa` for
`nu = 0.35` (ESTIMATE for a filled phenolic; pure phenolic resin is 2.5-4.8 GPa, the balls are mineral-filled), density
1740 kg/m^3 from the WPA mass and diameter. Lamb's (1882) stress-free spheroidal frequency equation is solved numerically
(`lamb_roots`; check: `k_T a = 2.640` for the n = 2 fundamental at nu = 0.25, the literature value). Loss factor 0.015
(ESTIMATE, glassy thermosets 0.008-0.03).

| Ball | n=2 fundamental | n=1 | n=3 | n=2, 2nd | T60 of the n=2 mode |
|---|---|---|---|---|---|
| standard phenolic 57.15 mm / 170 g | **20.3 kHz** (k_T a 2.652, modal mass 0.052 kg) | 27.8 kHz | 30.3 kHz | 39.3 kHz | 7.2 ms |
| polyester bar ball, E 5 GPa (ESTIMATE), 163 g | **15.5 kHz** | 21.3 kHz | 23.2 kHz | 30.2 kHz | 4.7 ms (loss 0.03) |
| oversized bar cue ball 60.3 mm / 221 g (1924 kg/m^3) | **18.3 kHz** | 25.0 kHz | 27.3 kHz | 35.5 kHz | 8.0 ms |

**Consequence:** a phenolic pool ball does not ring in the audible band; its click is almost entirely rigid-body
acceleration noise (the realism plan's remark "timbre, not pitch" is right). The modes matter for two things: the side-on
sound (quasi-static quadrupole) and a faint 15-18 kHz "tink" of cheap polyester sets and the oversized cue ball, which
young listeners hear. The synthesis keeps them (cheap), the band limit at 20-23.5 kHz removes what 48 kHz cannot carry.
A magnetic cue ball (iron core) is modelled as homogeneous; its real mode pattern is unknown (section 3.8).

**Sensitivity and published data (review).** No measured resonance of a pool ball was found (web search 2026-09-28;
the AzBilliards thread is anecdotal). The mode frequencies scale with `sqrt(E / rho)`: with the generic phenolic
`E = 5.2 GPa` used in some DEM studies the n = 2 mode drops to 15.5 kHz; with the stiffer `K` that Marlow's 284 us
contact time implies (`E` 12.8 GPa) it rises to 24.4 kHz. The conclusion "no audible ring for most adults" holds over the
whole plausible range; only the faint 15-18 kHz "tink" moves in and out of the young-listener band. Riner & Petculescu
(JASA 128(1), 2010) fitted impact acoustics of polypropylene balls with a force law `F = kappa d^alpha` whose exponent
is 6.25 % below Hertz's 1.5; a phenolic ball may deviate similarly. That changes the level law by ~0.6 dB per decade (peak force ~ v^(2 alpha / (1 + alpha)) = v^1.17 instead of v^1.2)
and is inside the calibration step of 3.8 (fit `K` and, if needed, the exponent from the recorded pulse width at two
speeds).

### 3.5 Structural radiators (ESTIMATE)

Rails, cabinet, slate bed, pocket and cue are modal banks driven by the same contact force and radiating as small
pistons (`p = rho0 S a_k / (2 pi r)` baffled, `4 pi` for the free cue), times a radiation-efficiency high-pass
`1 / (1 + (fc/f)^2)` for narrow parts whose front and back short-circuit at low frequency. Values in the prototype:

| Bank | Modes (f Hz, modal mass kg, loss factor, area m^2) | fc |
|---|---|---|
| Rail + cabinet, **coin-op bar box** (hollow cabinet with the ball-return channel, boomier) | (82, 60, 0.15, 0.60) cabinet/slate, (145, 15, 0.10, 0.25) apron, (240, 8, 0.08, 0.08) and (390, 8, 0.08, 0.08) rail bending, (610, 6, 0.09, 0.06), (950, 4, 0.10, 0.04), (1500, 3, 0.12, 0.03) | 400 Hz |
| Rail + body, pro 9-ft table (545 kg) | (95, 150, 0.18, 0.6), (180, 20, 0.12, 0.2), (300, 10, 0.08, 0.08), (520, 8, 0.08, 0.06), (830, 6, 0.09, 0.04), (1300, 4, 0.11, 0.03) | 400 Hz |
| Slate bed (landings) | (95, 150, 0.15, 1.0), (180, 60, 0.12, 0.5), (310, 30, 0.10, 0.3) | 150 Hz |
| Pocket / gully boot | (160, 1.5, 0.25, 0.02), (420, 0.8, 0.20, 0.01), (900, 0.5, 0.25, 0.005) | 300 Hz |
| Cue (tip strike) | longitudinal (1520, 0.30, 0.06, 3e-4), bending (330 / 870 / 2600 Hz, eta 0.08, S 1-2e-5) | free field |

Tuning target used until recordings exist: at equal speed a cushion thud peaks about 18 dB below the on-axis click and
has about 6 dB less sound exposure. With these values the ball's own radiation is 14-15 dB below the rail's in a cushion
hit, so the thud's character is set by the rail bank: fit it first (section 3.8).

### 3.6 Runtime algorithm (what the C++ DSP does)

Per ball voice and per impact (see section 8 for the classes):

```
input (plan builder, 8.3):  shot time tau, v_n, m*, K, e, this ball (R, m, material), contact axis a_hat,
                            listener position at tau (predicted), cloth image flag
per path (direct; cloth image = centre and axis mirrored in z = 0):
0. place:     r = |listener - centre|, S = F0 + (tau / Rate + (r - a)/c) fs (5.1), i = floor(S), frac = S - i
1. pulse:     T, F_max from 3.2 (tau_e, phi_e, 129-point shape table hertz_shape_e095.json, linear interpolation);
              F4[j] = F_max shape((j/(4 fs) - frac/fs) / T), j = 0 .. 4 ceil(T fs + frac) + 3 (zero outside [0, 1]);
              G[k] = sum_m h[m] F4[4k - m], h = 129-tap Kaiser (beta 8) low-pass, cutoff 24 kHz at 192 kHz, DC gain 1
              (delay 16 samples at 48 kHz; taps in golden_runtime_48k.json). The fraction is applied here, exactly.
2. orders:    y_n = K_n * G  for n = 0..3  (512-tap FIR kernels per ball, per unit force, at 1 m, 32-sample pre-delay;
              computed at start-up for the device rate by the port of far_order_kernels, one set per ball of the set,
              because worn balls differ in mass; the three ref/ball_kernels_*_48k.json files check the port)
3. listener:  cos th = dot(listener - centre, a_hat)/r;  s = sum_n P_n(cos th)/r y_n
              + (c/(r fs)) * leaky_sum(P_1/r y_1)   (near field of order 1; leak exp(-2 pi 2 Hz / fs))
4. image:     s of the image path through the cloth IIR (bilinear 0.9 / (1 + s/(2 pi 8 kHz)); coefficients in the golden file)
5. output:    s is added to the voice's overlap-add buffer at frame i - 48 (16 decimation + 32 kernel pre-delay);
              the plan-time presentation gain of 4.2 multiplies the voice output
output:       the voice emits r_ball * p (pressure referred to 1 m); Unreal's attenuation applies 1/r (Natural Sound, 1 m)
```

`runtime_render` in the prototype is this algorithm line by line; `golden_runtime_48k.json` holds three cases (std pair
1 m/s on-axis, 4 m/s side-on, oversized cue ball into a worn object ball at 45 deg, all with fractional start times)
with the output samples, the force of step 1 and every constant; the C++ port must reproduce them to a residual of
-100 dB (AU-T11). A whole impact is rendered into the voice's overlap-add buffer when its start frame falls into the
next block, so every impact is complete before its first sample is output.

Cost: per ball, path and impact 4 x 512 taps x ~21 force samples = ~43 k multiply-adds; with the image path ~86 k, i.e.
~172 k per ball-ball impact. The worst block of the dive-bar break holds **27 impacts within one 10.7 ms block** (break9:
13), ~4.6 M multiply-adds that must finish inside one callback. Requirements: SIMD inner loops (the SignalProcessing
module's `Audio::ArrayMultiplyAdd...` helpers or ISPC; VERIFY names), the ball voices spread over the source workers
(`AudioNumSourceWorkers` 4, each generator runs on its source's worker), and the AU-T19 measurement. Fallback if the peak
is too high: pre-compute `y_n` (listener independent) for the whole shot on a worker at plan time and keep only steps
0, 3-5 on the audio thread (with an 8-tap fractional-delay FIR for the fraction). Structural banks are 3-7 biquad
resonators per emitter. Rolling noise: one noise generator + 2 biquads per moving ball.

Accuracy of the runtime algorithm against the exact frequency-domain render (prototype, 1/6-octave, 100 Hz-16 kHz,
bands within 20 dB of the maximum): **0.02 dB rms, 0.14 dB max on-axis; 0.20 dB rms, 1.33 dB max side-on**; a plain
sin^1.5 pulse instead of the shape table gives 0.1-0.3 dB rms on-axis and 0.3-0.9 dB side-on (acceptable fallback).
The waveform residual is -36 to -48 dB on-axis but only -15 to -22 dB side-on (small phase differences of the weak
quadrupole terms), which is why the C++ golden test compares against `runtime_render`, not against the exact render.

**Directivity in the game.** Horizontal plane, 1 m/s, listener 1.2 m away with the ear 0.30 m above the cloth, angle
from the line of centres (prototype):

| Angle [deg] | 0 | 30 | 45 | 60 | 70 | 75 | 80 | 85 | 90 |
|---|---|---|---|---|---|---|---|---|---|
| Peak rel. 0 deg [dB] | 0 | -1.7 | -4.5 | -10.0 | -16.2 | -21.0 | -28.0 | -29.2 | -27.3 |
| Centroid [Hz] | 2239 | 2300 | 2361 | 2409 | 2428 | 2422 | 2119 | 2983 | 3831 |

The null is only ~30 deg wide at -20 dB; near it the quasi-static quadrupole ("squash") of the elastic modes takes
over and the click turns brighter. The direct sound keeps this (it is part of the "real" feel: the break is loud behind
the cue ball, a cut shot seen from the side is softer), with an optional floor `DirectivityFloorDb` (default -20 dB,
i.e. `|P_1 weight| >= 0.1`, affects only listeners within ~15 deg of the null plane; ESTIMATE, tune by ear) for
scattering by nearby balls and the rails that the free-sphere model ignores. The **reverb send is driven by the
radiated power** (direction independent, `E_rad` of 3.7), so a side-on click still excites the room correctly; in the
small dive bar (walls 1-3 m from the table) the early reflections of the on-axis lobe fill the null further, which is
why Steam Audio reflections or IR early reflections matter (6.3, 6.4).

### 3.7 Prototype results (spectral analysis report)

`click_synth.py` renders every case below; metrics are computed by `analyze()` on the first 60 ms after the onset
(cushion: 300 ms). Levels are absolute (calibrated sound pressure); "shooter" = listener down on the shot 1 m behind the
collision on its line, eye 0.30 m above the cloth; "observer" = standing at the side rail 1.2 m to the side, 0.85 m above
the cloth. Head-on stun, two standard phenolic balls.

| v_n [m/s] | peak dB SPL shooter | SEL dB shooter | spectral centroid [Hz] | spectral peak [Hz] | -10 dB band [Hz] | decay to -20 / -40 dB [ms] | peak dB SPL observer | E_rad / KE |
|---|---|---|---|---|---|---|---|---|
| 0.25 | 90.0 | 52.8 | 1930 | 1872 | 718-3202 | 0.54 / 0.77 | 56.5 | 4.3e-5 |
| 0.5 | 98.1 | 60.4 | 2084 | 2007 | 771-3478 | 0.50 / 0.73 | 64.4 | 6.6e-5 |
| 1 | 105.7 | 67.8 | 2223 | 2118 | 820-3729 | 0.48 / 0.71 | 71.7 | 9.6e-5 |
| 2 | 113.2 | 75.0 | 2341 | 2215 | 858-3946 | 0.48 / 0.75 | 80.7 | 1.3e-4 |
| 4 | 120.3 | 81.9 | 2444 | 2312 | 894-4128 | 0.48 / 1.77 | 90.9 | 1.8e-4 |
| 8 | 127.1 | 88.6 | 2548 | 2382 | 923-4271 | 0.48 / 2.00 | 100.0 | 2.2e-4 |
| 12 | 131.0 | 92.5 | 2575 | 2414 | 938-4339 | 0.48 / 2.19 | 104.3 | 2.5e-4 |

(The -40 dB tail above 2 m/s is the 20.3 kHz mode. E_rad = acoustic energy radiated into free space, integrated over all
directions, without cloth or table.)

Variants at 2 m/s, shooter: oversized cue ball -> worn object ball: T 298 us, 113.3 dB, centroid 2298 Hz, 18.3 kHz
tail (t-40 1.6 ms); magnetic cue ball -> worn ball: 285 us, 113.3 dB, 2351 Hz; polyester pair: 355 us, 111.3 dB,
2160 Hz, faint 15.5 kHz ring.

Cushion, bar-box rail (ESTIMATE bank), listener 1 m in front of the rail, 0.30 m above the cloth:

| v_n [m/s] | contact [ms] | peak F [N] | e | peak dB SPL | SEL dB | centroid [Hz] | spectral peak [Hz] | decay -20 / -40 dB [ms] |
|---|---|---|---|---|---|---|---|---|
| 0.5 | 2.90 | 102 | 0.970 | 77.7 | 52.7 | 327 | 246 | 33 / 83 |
| 1 | 2.53 | 235 | 0.970 | 85.7 | 60.8 | 362 | 401 | 34 / 76 |
| 2 | 2.21 | 528 | 0.935 | 94.9 | 68.8 | 415 | 402 | 33 / 66 |
| 4 | 1.94 | 1162 | 0.865 | 103.8 | 76.4 | 465 | 403 | 22 / 66 |
| 8 | 1.73 | 2440 | 0.725 | 111.5 | 83.5 | 528 | 404 | 22 / 63 |

Break renders (stereo, breaker's ears 0.55 m behind the cue ball, eye 0.36 m above the cloth): dive-bar 8-ball break
(7-ft bar box, oversized cue ball, house cue at 8 m/s, sloppy rack, 3 balls pocketed with gully runs and tray clicks):
79 audible impacts, minimum onset gap 1 us, 24 gaps < 1 ms, peak 126.2 dB SPL at the ear (the rack impact); `break9`
(9-ft, 9 m/s): 58 impacts, 12 gaps < 1 ms, peak 126.9 dB SPL. The dive-bar peak is not one impact: 14 contacts of the
rack cluster (113.19-113.41 ms) arrive at the ears together 5.7 ms later and add up coherently (single impacts peak at
most 125.4 dB; a per-impact gain law would still leave the sum +5 dB over full scale, which is why 4.2 uses one
envelope for the summed stem). Long-term spectrum of the dive-bar break: peak 406 Hz, centroid 1.1 kHz, i.e. the energy
is dominated by the ESTIMATE bar-box rail bank (25 cushion hits up to 5.25 m/s); if the listening test calls it boomy,
lower that bank first. Presentation of the dive-bar break per dynamic-range mode (4.2; table stem incl. the tail, 76 dBA
pink-noise room stand-in): Wide -1.5 dBTP, -28.6 LUFS stem, -29.0 LUFS with the room; Normal -1.2 dBTP, -23.7 LUFS with
the room; Night -1.2 dBTP, -19.7 LUFS with the room.

Sound-level-meter view (LAFmax, A-weighted, Fast): a click reads ~28 dB below its peak (0.25 / 1 / 4 / 12 m/s at 1 m
on-axis: 62.8 / 77.9 / 92.0 / 102.6 dB(A)). A 1 m/s click at 1.2 m is therefore about as "loud" as the 76 dBA room, a
0.25 m/s click 14 dB below it, a 4 m/s click 15 dB above it: soft shots really are hard to hear in a busy bar.

**Comparison with published data**

| Quantity | Prototype | Published | Verdict |
|---|---|---|---|
| Contact time at 1 m/s | 331 us (329 undamped) | COL 3.9.3: 329 us; Marlow: 284 us (other sources quote ~200 us) | consistent with the physics; K is the physics team's calibration item |
| Tip contact | 0.8 ms logged by the core for the 9 m/s break | Dr. Dave: 0.8 ms hard/fast ... 2-3 ms soft/slow | consistent |
| Radiated fraction of kinetic energy | 1e-4 at 1 m/s (4e-5 ... 2.5e-4 over 0.25-12 m/s) | Riner & Petculescu, JASA 128(1):132 (2010), polypropylene spheres: acoustic-to-incident energy ratio "on the order of 100 ppm" (the companion letter JASA 128(4):1575 constrains the same quantity; Draft v1's "0.23 uJ of 1.6 mJ = 1.4e-4" could not be re-checked from the abstracts) | same order; independent sanity check of the absolute level |
| Level vs speed | peak +24.3 dB/decade, SEL +23.6 dB/decade; 2 vs 1 m/s +7.45 dB | realism plan 8.2: +24.0 dB/decade (v^1.2), test T23 +7.2247 dB | **confirmed** within 0.3 dB/decade; T23 should test the synthesis (+7.45 +- 0.1 dB, test AU-T09) |
| Brightness vs speed | centroid +16 % from 1 to 12 m/s; level relative to the spectral peak at 6 / 8 / 10 kHz: -50.6 / -52.6 / -53.6 dB at 1 m/s, -34.0 / -39.4 / -49.5 dB at 12 m/s | plan 8.2: cutoff x (v/v_ref)^0.2 = x1.64 | **plan rule too strong for the body of the click**: the peak and centroid are pinned by the fixed sphere radiation (ka ~ 1) and the 2a/c interference; only the weak high-frequency skirt follows 1/T (4-17 dB). Use the synthesis; velocity-layered samples carry this by themselves (no extra low-pass tracking) |
| Spectral region | 0.8-4.3 kHz (-10 dB), peak 1.9-2.4 kHz | no measurement of pool balls found; forum statements (AzBilliards, not measured) name 500 Hz-10 kHz and 2.75-4 kHz | plausible; must be checked against recordings |
| Model basis | Hertz + rigid-sphere radiation | Koss & Alfredson 1973 (steel spheres): "classical Hertzian impact theory and acoustical theory is sufficient to define the pressure time waveforms" | method validated in the literature; material data for phenolic are ours |

**What the renders should sound like (listening notes for the PO; Claude cannot listen).** Clicks: a dry, sharp "clack"
with no pitch and no tail, darker and much quieter at 0.25 m/s, hard and bright but not higher in pitch at 8-12 m/s.
Cushion: a short low "thock" around 400 Hz with 20-35 ms body; the bar box sounds hollower than a pro table. Break: the
tip strike, ~113 ms later the rack "crack", a spray of clicks and thuds, rolling rumble, three pocket drops (at 2.26,
3.03 and 4.97 s), each followed at once by the gully run (1.5-3 s long, starting 20 ms after the drop) that ends in a
muffled tray click. Checklist for the listening test (section 15, Q5): (a) does the click sound like a real ball or
like a synthetic "tick"? (b) are soft shots too quiet / too dark? (c) is the cushion thud too boomy or too dull?
(d) does the break sound like a break (spread, loudness, density)? (e) any artefacts (pre-echo, clipping, zipper)?
(f) which dynamic-range mode sounds most like being there (compare `Tools/audio/out/divebar_break8_{wide,normal,night}.wav`
at one fixed monitor volume)? Reference files: `Tools/audio/out/ref/*.wav`; the physical ones are calibrated 100 Pa =
full scale, so raise the monitor volume instead of normalising; `ref_divebar_break8_wide.wav` is the game's default
headphone presentation. None of the renders has a room-tone bed, so every click is more exposed than it will be in the
bar.

### 3.8 Calibration against recordings (the path from ESTIMATE to fitted)

1. Library or own recordings of isolated clicks at known speed (own session: speed from the ball travel time between
   two rail diamonds on video, 240 fps phone is enough) and cushion hits.
2. `click_synth.py --analyze rec.wav --full-scale-pa 1` gives the same metrics as 3.7 (relative levels when the
   recording is not calibrated).
3. Fit in this order: cushion contact time (pulse width in the waveform) -> rail bank (spectral peak, decay) -> cloth
   reflection (comb above 3 kHz) -> ball `E`/`nu` only if the recorded centroid deviates by more than 10 % at equal
   geometry (a change of `K` also belongs to the physics team, COL open question on Marlow's 284 us).
4. Absolute level: a calibrated recording (sound level calibrator, 94 dB at 1 kHz) of a 1 m/s click at 1 m checks the
   105.7 dB peak prediction.
5. The fitted constants go into the `URbVenueAudioProfile` / table preset data (section 8), not into code.

---

## 4. Level, timbre and dynamics

### 4.1 Physical level domain

All emitters are authored in **physical units**: synthesis outputs Pa referred to 1 m; every library sample gets a
measured "reference SPL at 1 m" tag at import (peak, SEL and LAFmax, measured once with the prototype's `--analyze`).
Exceptions, authored in dBFS: UI tokens (UIX 4.6), the in-head heartbeat/breath layer (AU-59) and menu music.

Real-world reference levels at the player's ears in the dive bar (ESTIMATE, to be checked with a sound level meter app in
a real bar): room with jukebox and 10-20 guests 72-80 dBA Leq; rolling balls 45-55 dB; a 1-2 m/s click at 1-2 m 100-113 dB
peak; a hard break 120-127 dB peak. What a listener perceives as loudness of a click is its sound-level reading, not its
peak: LAFmax is ~28 dB below the peak (3.7), so a 1 m/s click at 1.2 m (77 dB(A)) is as loud as the room and a 4 m/s
click 15 dB louder. Clicks "cut through" in real footage because they are short, broadband and far above the room's
spectrum at 2-4 kHz, not because of a peak ratio; phone and camera footage actually limits them hard (6.2).

Consequence for playback: at a normal listening loudness (room around -24 to -30 LUFS) the click peaks of hard shots and
breaks would exceed 0 dBFS by 10-20 dB. They cannot be played physically; section 4.2 defines how they are compressed.

### 4.2 Presentation and dynamic-range modes (Settings > Audio > Dynamic range, UIX 13.8)

```
digital(t) = pressure(t) * g_bus * g_table(t) / P_fs(mode)        P_fs = 20 uPa * 10^(L_fs / 20)
```

* `L_fs` (per mode) = the sound pressure level that maps to 0 dBFS. It sets the loudness of the whole world, since
  every physical bus uses it.
* `g_bus` = fixed per-bus presentation offset (ambience/crowd/music, voices; table = 0 dB).
* `g_table(t)` = **plan-time presentation envelope** of the table stem (ball, rail, pocket, cue voices): when the shot
  plan is built (8.3), the DSP core renders the shot's impacts in mono at the predicted listener position (physical Pa;
  worker thread, ~15 M multiply-adds for a break), takes the peak envelope with 3 ms lookahead and 60 ms release, maps
  it through the static curve (0 dB below `knee`; above it the presented peak rises 1 dB per `ratio` dB) and smooths
  the dB gain with a 1 ms one-pole. The result is stored in the plan as a breakpoint list in shot time (1 ms grid) and
  every table voice multiplies its output by it, sample-exactly. The envelope knows the future, so overlapping clicks of
  a break cluster share one gain, single clicks keep their waveform, nothing pumps, and replays reproduce it
  bit-identically (recomputed for the replay listener). Implemented and measured in the prototype
  (`presentation_gain_envelope`).
* Master: true-peak limiter at -1 dBTP (Submix Dynamics Processor in limiter mode with lookahead, VERIFY property
  names) as a safety net only; with the renders below it acts by at most ~1 dB.
* Engine mapping: the table voices multiply by `g_table(t) / P_fs(mode)` themselves (their output is then in digital
  units referred to 1 m; Unreal's 1/r attenuation follows). Every other physical source is imported with float 1.0 =
  1 Pa = 94 dB SPL at 1 m (per-asset volume `10^((L_ref,1m - 94) / 20)` from its import tag), and the physical submixes
  (`SUBM_RB_Foley`, `_Ambience`, `_Crowd`, `_Voice`, `_Jukebox`) get the mode gain `94 - L_fs + g_bus` [dB]:
  ambience/crowd/music -20 / -14 / -10 dB, voices and foley -20 / -12 / -6 dB in Wide / Normal / Night; lines
  addressed to the player get their +3 / +4 / +6 dB as a per-line volume from `URbVoiceLineSubsystem`. All bus gains
  are <= 0 dB, because Audio Modulation volume buses cannot boost (0..1); `URbAudioSubsystem`
  sets them on the submix output volume (VERIFY property name). Values above 1.0 inside the float mixer are harmless;
  only the master output must stay at or below -1 dBTP.

| Mode | Default for | `L_fs` [dB SPL = 0 dBFS] | Table knee / ratio | Ambience, crowd, music | Voices (addressed) | Session target (10 min, integrated) |
|---|---|---|---|---|---|---|
| **Wide** | Output = Headphones | 114 | 105 dB SPL / 3:1 | 0 dB | +3 dB | -29 +- 2 LUFS (film-like; raise the headphone volume) |
| **Normal** | Output = Speakers | 106 | 97 dB SPL / 4:1 | -2 dB | +4 dB | -24 +- 2 LUFS (the common console reference) |
| **Night** | - | 100 | 95 dB SPL / 10:1 | -4 dB | +6 dB | -20 +- 2 LUFS |

All numbers ESTIMATE until the listening test (15, Q5); they live in `URbAudioSettings`, not in code. Measured on the
prototype's dive-bar break at the breaker's ears (3.7; 76 dBA pink-noise room stand-in):

| Mode | Break true peak | Integrated incl. room | 0.25 m/s click at 1.2 m vs room (LAF) | 1 m/s | 4 m/s | physical |
|---|---|---|---|---|---|---|
| Wide | -1.5 dBTP | -29.0 LUFS | -14.3 dB | +0.8 dB | +5.5 dB | -14.3 / +0.8 / +15.0 dB |
| Normal | -1.2 dBTP | -23.7 LUFS | -12.3 dB | -2.9 dB | +0.3 dB | |
| Night | -1.2 dBTP | -19.7 LUFS | -10.3 dB | -3.9 dB | -2.8 dB | |

Wide keeps everything up to a 1 m/s click at 1.2 m physical and compresses hard shots 3:1 (a 4 m/s click is +5.5 dB
over the room instead of +15); that is the price of fitting a 126 dB break into 0 dBFS at -29 LUFS. Normal and Night
trade more of the hard-shot punch for loudness and turn the room down 2-4 dB, so that a medium click stays within
3-4 dB of the room and soft clicks gain 2-4 dB on it. The first-launch
audio step (UIX 5.6) plays its break sample in the chosen mode with the line "Set your volume so the bar sounds like a
bar, not like a video."

Integrated loudness is measured with the Audio Insights output meter (5.8.3 source: `OutputMeterTraceMessages.h`
carries momentary, short-term and long-term loudness) in the editor, and headless with the prototype's BS.1770-4 meter
on a submix recording (`--analyze` prints LUFS and dBTP). Other UIX 13.8 audio settings handled here: Output
(Headphones = binaural HRTF, Speakers = stereo panning, Surround), Mono audio (sum after spatialisation, keeps the HRTF
level cues as level only), Heartbeat & breath tells (AU-59 on/off; captions replace them), Mute when unfocused, Voice
language, Streamer mode (11.1). Volume sliders (UIX 13.8, 7.2) scale on top of the presentation.

### 4.3 Speed-to-sound mapping for sampled layers

Library layers (tip thock, pocket drop, gully) use velocity layers (5 per sound, >= 6 round robins each) cross-faded
by speed on a log axis, then a fine gain `+24 dB per decade` (the confirmed law of 3.7) inside each layer. No per-layer
low-pass tracking: the body of the click barely brightens with speed (3.7); the velocity layers carry the
high-frequency skirt by themselves. Sampled table layers join the table stem, so the presentation envelope of 4.2
covers them (their physical peak at 1 m is known from the import tag).

---

## 5. Timing and scheduling

### 5.1 From shot time to audio frames

The director's live playback clock is anchored at the tip contact: `ShotTime = (now - ContactTime) * Rate`
(ARCH-UE 5.5). Audio uses **one anchor per shot and table**, shared by every voice of that table (a hall has one
`FRbShotAudioClock` per table, 6.6).

**What the 5.8.3 engine does (source-checked 2026-09-28, `Engine/Source/Runtime/AudioMixer`, `Engine/Classes/Sound`).**
`FMixerDevice::OnProcessAudioStream` renders all sources (`SourceManager->ComputeNextBlockOfSamples()`, spread over the
`AudioNumSourceWorkers` tasks and joined inside the callback) and only afterwards advances the clock
(`UpdateAudioClock(): AudioClock += AudioClockDelta`). Procedural sources render synchronously ("Do rendering
immediately", `FMixerSourceBuffer::GetNextBuffer`), `GetDesiredNumSamplesToRenderPerCallback()` samples per call
(default 1024) from `ISoundGenerator::OnGenerateAudio`. So `FAudioDevice::GetAudioClock()` (a plain `double`, written
by the same render thread) read inside `OnGenerateAudio` is the start time of the block being rendered, and
`llround(AudioClock * fs)` is its device frame. `FSoundGeneratorInitParams` carries `AudioDeviceID`, `SampleRate`,
`AudioMixerNumOutputFrames`, `NumChannels` and `NumFramesPerCallback`. The engine default is 48 kHz with
`AudioCallbackBufferFrameSize=1024`, `AudioNumBuffersToEnqueue=1` (`BaseEngine.ini`).

Design:

1. **Voice setup.** Every table voice's generator returns `GetDesiredNumSamplesToRenderPerCallback() =
   AudioMixerNumOutputFrames * NumChannels`, so it is called exactly once per device block. It stores
   `StartFrame = llround(GetAudioClock() * fs)` in `OnBeginGenerate` and counts its own frames `g`; frame `g` is heard at
   device frame `StartFrame + g + L_src`, where `L_src` is a per-engine constant (buffer queue of the mixer source, 0 or
   one block) measured once in AU-0 and put into `URbAudioSettings`. The component settings that keep this mapping
   exact: pitch 1.0 and no pitch modulation (a resampling source drifts), Doppler off, `bAlwaysPlay`, virtualization
   disabled (`VirtualizationMode` VERIFY on the synth's sound), never stolen (own concurrency, 7.4), and the generator
   runs at the device rate (`InParams.SampleRate`, kernels built for it, 3.6).
2. At `URbShotPlaybackComponent::Play` the table audio component builds the shot's audio plan (8.3) and posts
   `StartShot(ShotId, ContactTime, Rate, VisualLatency)` to the table's thread-safe `FRbShotAudioClock`.
3. The **first** voice callback of that table after the post sets the anchor once (atomic compare-exchange), in device
   frames: `F0 = BlockFrame + max(LeadMin, round((ContactTime + VisualLatency - (PlatformNow + OutputLatency)) * fs))`,
   where `BlockFrame = llround(GetAudioClock() * fs)`, `PlatformNow = FPlatformTime::Seconds()` read in the same
   callback, `OutputLatency` = the device output latency (`AudioCallbackBufferFrameSize x AudioNumBuffersToEnqueue / fs`
   plus the WASAPI device period, measured in AU-T18), `VisualLatency` = render + display latency (default 2 frames at
   the current frame rate) + the user's A/V offset (Settings, +-150 ms), `LeadMin` = 1 block.
4. Every voice converts event `k` to its own frame index: `S_k = F0 + (tau_k / Rate + (r_k - a)/c) * fs - StartFrame -
   L_src` (fraction kept, 3.6 step 0). Relative timing between events **and between voices** is therefore exact:
   all voices share `F0` in device frames and each knows where its own frames land. Only the absolute A/V offset
   carries the anchor's one-off jitter (< 1 block).
5. Events whose frame is already past (the tip strike, when the hand-off took a frame) shift the whole shot by the
   missing frames (`F0` is simply set later), never dropped.

AU-0 must prove: two voices started at different times (the second 3 game frames later) render the same impulse at the
same device frame (0 samples skew in a submix recording), and impulses 0.5104 ms apart in two different voices land
24.50 +- 0.05 samples apart (AU-T08, AU-T21). **Fallback** if a mapping is not constant (e.g. a source that pre-renders
a variable number of blocks): render all table sounds of a table inside one `ISoundGenerator` with 8 output channels
mapped to 8 fixed spatial emitters (4 table quadrants x 2 heights), or a 7.1 bed. Both keep sample accuracy but lose
per-ball positioning, so they are the fallback only.

### 5.2 Playback rate, replay, scrubbing, pause

Replay speeds are 0.05 / 0.1 / 0.25 / 0.5 / 1 / 2x, with pause, hold-scrub and 1-frame steps (UIX 10.4).

| Case | Behaviour |
|---|---|
| Live, rate 1 | as 5.1 |
| Replay, rate s < 1 (slow motion) | **Film style** (default): events spaced by 1/s, each impact rendered at natural pitch and duration; rolling noise pitch-shifted by s^0.5 and low-passed (it would otherwise be a stretched rumble). **Physical style** (photo/trailer option): the whole signal time-stretched by 1/s (the pulse rendered with `T/s`, kernels and banks with all frequencies x s). Only useful down to s ~ 1/40 (1000 fps footage played at 25 fps: the 2 kHz click becomes a 50 Hz thud); below ~1/100 the content falls under 40 Hz and the renderer switches to film style |
| Replay, rate 2x | film style: events spaced by 1/2, natural pitch; rolling pitch x 2^0.5 |
| Scrubbing forward | events fire when the cursor passes them; bursts limited to 8 events per 50 ms; rolling follows the cursor speed; 1-frame steps play the events inside that frame only |
| Scrubbing backward, pause (rate 0) | table voices silent; ambience continues |
| Replay perspective (UIX 10.2 looks) | **Phone** (default in bar, Kneipe, pool hall): listener = the phone at the replay camera, through the Phone chain of 6.2 (the trailer's cold-open sound). **Broadcast** (arena default): listener = a table microphone pair 1.5 m above the bed centre (how televised pool is miked), broadcast-clean, no HRTF. **Clean**: listener at the recorded eye position of the shooter (`EyeTransform` of the stroke record). Camera cuts inside one look do not move the listener |
### 5.3 Why not Quartz for this

Quartz schedules sample-accurately but on musical quantisation boundaries of its clock (bars, beats, sub-divisions,
the smallest being one "tick"); arbitrary times between two ticks are not addressable, a clock fast enough for 20 us
spacing is not a supported use, and every sound would need its own quantized play command (Epic forum and Quartz docs;
`Runtime/AudioMixer/Public/Quartz` exists in 5.8.3). The 5.1 mapping needs none of that. Quartz stays available for the
jukebox (beat-synchronous NPC head nods, V2).

---

## 6. Spatialisation, rooms and reverb

### 6.1 Emitters

| Emitter | Count | Attached to | Carries |
|---|---|---|---|
| Ball voice | 16 (22 for snooker later) | each ball's mesh component (`ARbBallSet`) | clicks, rolling/sliding, landings, liner hits, tip strike (cue ball part) |
| Rail voice | 6 (one per `CushionId` nose segment; its midpoint) | table | cushion rail banks, rail cap, facings next to it |
| Pocket voice | 6 | pocket centres | jaw knocks, drops, gully starts |
| Table body voice | 1 | table centre, under the bed | slate bed, cabinet, gully runs (moving source handled by panning between two virtual points), tray, coin mechanism, ball release |
| Cue voice | 1 | cue tip | cue body, miscue scrape, bridge slip |
| Player foley | 3 | bridge hand, grip hand, feet | chalk, cloth taps, footsteps, clothes |
| NPC emitters | per NPC | head / hands / feet sockets | voice, foley |
| Ambience | per venue (section 12) | fixed props | loops and one-shots |
| Loose ball | 1 per ball off the table (AU-25) | the Chaos-simulated ball actor | floor hits, rolling on the floor, knocks |

The table voices (ball, rail, pocket, body, cue = 30) exist **per table** and are owned by that table's
`URbTableAudioComponent`; tables other than the player's use the reduced LOD tiers of 6.6.

### 6.2 Listener and camera presets

- **Eyes** (default camera): the listener is at the head (between the ears), HRTF on headphones.
- **Headcam** (camera option): optional audio perspective **"Camera mic"**: the listener becomes the camera's own
  microphone: mono-ish (width 0.3), high-pass 120 Hz, low-pass 11 kHz, fast AGC (attack 2 ms, release 400 ms, 6:1),
  soft clip on the break, self-noise at -70 dBFS. This is what Bodycam-style footage sounds like and what sells "is this
  real footage" in clips (PO question Q7).
- **Phone** (Phone replay look, UIX 10.2; trailer cold open; photo mode): listener at the phone camera, mono, high-pass
  200 Hz, low-pass 8 kHz, pumping AGC (attack 5 ms, release 800 ms), limiter with audible clipping on breaks, handling
  noise from the camera shake. Real phone footage limits clicks hard; this chain is what makes the replays read as
  "someone filmed it".
- **Broadcast** (Broadcast replay look): table microphone pair 1.5 m above the bed centre, clean, no HRTF (5.2).
- Both footage chains sit after the presentation of 4.2 on `SUBM_RB_HeadcamMic` (renamed in meaning: "footage
  perspective" submix), bypassed in Eyes.

### 6.3 Spatialiser

What 5.8.3 actually ships (checked in `Engine/Plugins`, 2026-09-28): the attenuation setting "Spatialization Method:
Binaural" needs a spatialization plugin selected in the Windows project settings; without one it falls back to panning.
In-box choices: **Resonance Audio** (Google, Apache 2.0, *beta*, enabled by default: HRTF and room effects),
**Spatialization** (Epic, ITD only: interaural time delay, no HRTF), and **Steam Audio (Deprecated)**, version
"2.0-beta.17", disabled by default: the old bundled plugin, not to be used. Decision: V1 starts with Resonance Audio as
the HRTF spatialiser for headphones (plain panning for Speakers); evaluate **Valve's own Steam Audio UE plugin** (Apache
2.0, GitHub releases; VERIFY that its current release supports UE 5.8) for HRTF plus simulated early reflections,
because a dive bar's walls are 1-3 m from the table and its early reflections are characteristic (they also fill the
click's dipole null, 3.6). Attenuation for table voices: "Natural Sound" falloff with 1 m reference (the synthesis
outputs pressure at 1 m), air absorption from 20 m (irrelevant indoors except the arena), no occlusion for table sounds
(line of sight), Doppler off (5.1).

### 6.4 Reverb per venue

One reverb submix per venue with the **Convolution Reverb** submix effect (`USubmixEffectConvolutionReverbPreset`,
plugin "Synthesis and DSP Effects", enabled by default; 5.8.3 source-checked: the preset has `ImpulseResponse`
(`UAudioImpulseResponse`, which holds the normalization volume and true-stereo options), `BlockSize`,
`bEnableHardwareAcceleration`, and settings `WetVolumeDb`, `DryVolumeDb`, `bBypass`,
`bMixInputChannelFormatToImpulseResponseFormat`, `bMixReverbOutputToOutputChannelFormat`). Sends: table and foley at
their radiated power (3.6), voices, crowd, jukebox. Early reflections (< 30 ms) from Steam Audio if adopted, otherwise
included in the IR.

Dive-bar RT60 (DERIVED, Sabine, VEN 2.2 main room 16.46 x 7.32 x 2.74 m = 330 m^3; lay-in mineral-fibre tiles alpha
0.35 / 0.55 / 0.65, VCT on concrete 0.02-0.03, 40 m^2 of 1970s panelling 0.28 / 0.10 / 0.07, painted brick and block,
booths, stools, 0.25 / 0.45 / 0.55 m^2 per guest, air): empty 0.84 / 0.63 / 0.54 s, 15 guests 0.79 / 0.59 / 0.50 s at
125 Hz / 500 Hz / 2 kHz. If the tiles turn out painted or nicotine-sealed (alpha ~0.2 / 0.3 / 0.35): 1.09 / 0.88 /
0.75 s. Both lie inside VEN 10's 0.6-0.9 s mid-band range. Occupancy changes the decay by only ~5 %, so one IR per venue
is enough; the crowd's level, not the reverb, carries the busy-night feel.

| Venue | RT60 low / mid / high [s] (ESTIMATE) | IR source |
|---|---|---|
| Dive bar | 0.8 / 0.6 / 0.5 (derived above; the tile finish decides between this and 1.1 / 0.9 / 0.75) | V1: IR generated from our own room model (image sources up to order 3 from the Blender geometry + stochastic tail per octave band from Sabine with material absorption, tool `Tools/audio/ir_synth.py`, future); later a sine-sweep IR recorded in a real bar (UE 8.5) |
| German Kneipe (wood panelling) | 0.7 / 0.55 / 0.35 | same |
| Basement (concrete) | 1.0 / 0.8 / 0.6 | same |
| Pool hall (carpet, drop ceiling) | 1.1 / 0.9 / 0.6 | same |
| Arena | 2.2 / 1.8 / 1.2 | same |

Public IR libraries (OpenAIR, CC licences per file) are for reference listening only; a dive-bar IR must match our room.

### 6.5 Rooms and zones

Audio Gameplay Volumes (plugins "AudioGameplay" and "AudioGameplayVolume", both beta and not enabled by default in
5.8.3; VERIFY component names) per acoustic zone: main room, bar counter, restroom corridor (jukebox and crowd occluded
-12 dB and low-passed 2 kHz), outside (street). Zone changes switch the reverb send and apply control-bus mixes (7.3).

### 6.6 Several tables in one room (decisions 2026-09-28)

Venues may hold several tables, each with its own match (pool hall ~8; AI regulars really play; later online players).
Audio never assumes one table:

* One `URbTableAudioComponent` and one `FRbShotAudioClock` per `ARbTable`, bound to that table's playback component
  and director. Photo mode and pause hold every table's clock (UIX 2.4).
* Audio LOD per table, chosen from the listener distance to the table's nearest rail (hysteresis 1 m):

| Tier | When | Voices | Synthesis |
|---|---|---|---|
| T0 | the player's table (always), or any table < 3 m | 30 (6.1) | full (3.6) |
| T1 | 3-10 m | 4 (one per table quadrant, at rail height) | order-1 kernels only, no cloth image, rail banks reduced to 3 modes; each event goes to its nearest quadrant voice |
| T2 | > 10 m or occluded | 1 at the table centre | order-1 kernel, events closer than 5 ms merged, low-pass 6 kHz; rolling as one summed noise |

* Budget in an 8-table hall: 30 + 7 x 4 = 58 synth voices at most (the player standing between tables), typically
  30 + 2 x 4 + 5 x 1 = 43. The presentation envelope (4.2) is computed per table and the loudest table's envelope does
  not duck the others (each table is its own stem; the master limiter is shared).
* Captions only for the player's table and for barks directed at the player (UIX 2.4).
---

## 7. Mixing

### 7.1 Submix graph

```
Master (SUBM_RB_Master: true-peak limiter, dynamic-range mode chain 4.2)
 +- SUBM_RB_World                     diegetic, spatialised
 |   +- SUBM_RB_Table                 ball, rail, pocket, cue voices (synthesis + table samples); the plan-time
 |   |                                presentation envelope (4.2) is applied inside the voices, per table
 |   +- SUBM_RB_Foley                 player and NPC foley, chores, coin-op
 |   +- SUBM_RB_Ambience              beds, one-shots, TV, neon
 |   +- SUBM_RB_Crowd                 walla, reactions
 |   +- SUBM_RB_Voice                 all dialogue (sidechain key for ducking)
 |   +- SUBM_RB_Jukebox               music through the jukebox speaker IR
 +- SUBM_RB_Reverb_<Venue>            convolution reverb (sends from the World submixes)
 +- SUBM_RB_HeadcamMic                footage perspective chains (Camera mic, Phone, Broadcast; 6.2), bypassed in Eyes
 +- SUBM_RB_UI                        non-diegetic UI (no reverb)
 +- SUBM_RB_MenuMusic
```

### 7.2 Control buses (Audio Modulation plugin; disabled by default, enable)

Volume buses bound to the UIX 13.8 sliders through Parameter Patches (slider default in brackets): `CB_RB_Master`
(100 %), `CB_RB_Music` (70 %, jukebox and menu), `CB_RB_Table` (100 %, "Table & balls": all table voices and sampled
table layers, the player's own stroke foley (chalk, bridge, cue handling) and the heartbeat/breath tells, because they
are gameplay feedback), `CB_RB_Ambience` (80 %, ambience + crowd + all footsteps + foley of others), `CB_RB_Voice`
(100 %), `CB_RB_UI` (60 %, "Interface"). State buses: `CB_RB_Duck_Crowd`, `CB_RB_Duck_Music`, `CB_RB_Duck_Ambience`,
`CB_RB_LPF_World` (low-pass frequency parameter for pause/replay). Audio Modulation 2.0 is not enabled by default in
5.8.3 (`AudioModulation.uplugin`); the 5.8.3 headers are `SoundControlBus.h`, `SoundControlBusMix.h`,
`SoundModulationParameter.h`, `SoundModulationPatch.h`.

### 7.3 Mix states (Control Bus Mixes; attack/release in seconds)

| Mix | Active when | Stages | Attack / release |
|---|---|---|---|
| `CBM_RB_Focus` | player down on a shot (option Off / Subtle (default) / Strong) | Subtle: crowd -2 dB, music -2 dB; Strong: -5 dB and ambience -3 dB | 1.5 / 2.0 |
| `CBM_RB_Dialogue` | a voice line addressed to the player plays | crowd -4 dB, music -4 dB, other NPC chatter -6 dB | 0.15 / 0.6 |
| `CBM_RB_Hush` | game ball / hill-hill / big money shot with an audience (HF-15 pressure > 0.7) | crowd -8 dB (people really go quiet), TV -3 dB | 2.0 / 1.0 (released 0.5 s after the balls stop) |
| `CBM_RB_Replay` | replay mode | ambience and crowd -10 dB, LPF 4 kHz on World except Table | 0.3 / 0.3 |
| `CBM_RB_Pause` | pause menu | World LPF 800 Hz, -12 dB | 0.2 / 0.3 |
| `CBM_RB_Restroom` etc. | zone mixes (6.5) | per zone | 0.3 / 0.3 |

Real footage has no ducking; the default keeps it psychoacoustic and small. Signal sidechaining (Dynamics Processor with
an external key on `SUBM_RB_Voice`) is the alternative for `CBM_RB_Dialogue` if event-driven ducking misses lines.

### 7.4 Concurrency and voice budget

| Group | Max | Rule |
|---|---|---|
| Table synth voices | 30 per T0 table, 4 per T1, 1 per T2 (6.6); running continuously while the table is in that tier (an idle generator writes zeros; starting a source takes blocks, so voices are never started per shot) | never stolen, never virtualised |
| Table sampled layers | 24 | stop quietest |
| Foley | 16 | stop oldest |
| Ambience one-shots | 12 | stop farthest |
| Crowd | 8 | stop quietest |
| Voice | 4 (1 addressed + 3 background) | priority: addressed > mentor > chatter |
| Music | 2 (jukebox crossfade) | - |

Project settings under `[/Script/WindowsTargetPlatform.WindowsTargetSettings]` (key names checked in 5.8.3
`WindowsTargetSettings.h`; engine defaults in `BaseEngine.ini`: 48000 / 1024 / 1 / - / 4): `AudioSampleRate=48000`,
`AudioCallbackBufferFrameSize=512`, `AudioNumBuffersToEnqueue=2`, `AudioMaxChannels=96`, `AudioNumSourceWorkers=4`.
512 x 2 gives the same ~21 ms buffer latency as the default 1024 x 1 but halves the anchor granularity and the per-block
peak load of a break cluster (3.6); keep the default if AU-0 shows underruns with WASAPI.

---

## 8. Unreal implementation plan

The Unreal side is frozen for other work packages right now (ARCH-UE); this is the plan for the audio work packages
after M1. Names follow ARCH-UE conventions (`Rb` prefix, code-only creation, headless verification).

### 8.1 Modules

| Module | Type | Depends on | Content |
|---|---|---|---|
| `RawBreakAudioDsp` (new, `Source/RawBreakAudioDsp/`) | Runtime, **no UObjects**, like BilliardsCore | Core only (+ SignalProcessing for SIMD helpers, VERIFY) | contact shape tables, decimation FIR, per-order kernel design (port of `far_order_kernels`: Lamb roots with `std::sph_bessel` / `std::sph_neumann`, FFT design), modal banks, rolling noise, the per-voice event renderer, the plan-time presentation envelope (4.2), a BS.1770 meter for tests; unit-testable without an audio device; golden tests against `Tools/audio/out/ref/golden_runtime_48k.json` and the kernel files |
| `RawBreak` (existing) `Audio/` folder | Runtime | RawBreakAudioDsp, AudioMixer, AudioExtensions, SignalProcessing, MetasoundEngine, AudioModulation (plugin) | components, subsystems, settings (8.2) |
| `RawBreakEditor` | Editor | MetasoundEditor, AudioEditor | headless asset creation: sound wave import, impulse responses, MetaSound Sources via the MetaSound Builder API (`UMetaSoundBuilderSubsystem` in MetasoundEngine, saved with `UMetaSoundEditorSubsystem::BuildToAsset`; both present in 5.8.3), control buses/mixes |

Plugins (5.8.3 descriptors checked): enable Audio Modulation (off by default), AudioGameplay + AudioGameplayVolume
(beta, off by default); already on: Synthesis and DSP Effects, MetaSound, Audio Insights, Resonance Audio (beta; the V1
HRTF, 6.3). Optional later: Valve's Steam Audio plugin (not the deprecated bundled one). Not needed: Audio Subtitles
(the UI has its own subtitle system, UIX), Soundscape (ambience is scripted per venue).

### 8.2 Class catalogue

| Class | Kind | Responsibility |
|---|---|---|
| `Rb::Audio::FContactShape` | DSP data | self-similar pulse table per restitution; `Pulse(v, mStar, K, e) -> T, Fmax`; force at the device rate via 4x evaluation + 129-tap decimation FIR with the fractional start (3.6 step 1) |
| `Rb::Audio::FBallKernels` | DSP data | 4 x 512-tap far-field kernels per ball (computed at start-up for the device sample rate; one set per ball of the active set, since worn balls differ in mass) |
| `Rb::Audio::FModalBank` | DSP | biquad resonator bank with radiation high-pass (rails, bed, pocket, cue) |
| `Rb::Audio::FImpactRenderer` | DSP | renders one impact into a voice buffer (3.6 steps 1-5) |
| `Rb::Audio::FRollingSource` | DSP | noise + filters, speed/state evaluated from the ball's `TrajectorySegment` copy at the audio cursor |
| `Rb::Audio::FShotAudioClock` | thread-safe object, **one per table** | anchor frame F0 in device frames, rate, pause (5.1) |
| `Rb::Audio::FPresentationEnvelope` | DSP | plan-time peak envelope -> gain breakpoints per dynamic-range mode (4.2) |
| `FRbAudioEvent`, `FRbShotAudioPlan` | plain data | per-voice sorted event lists (shot time, kind, parameters, listener-dependent gains) + the presentation envelope of the current mode |
| `URbTableAudioComponent` | component on `ARbTable` (one per table) | owns that table's voices (30 / 4 / 1 by LOD tier, 6.6); on `OnShotStarted` builds `FRbShotAudioPlan` from `FRbShot` (events + tracks) on a worker, maps events to voices, pushes the plan to each generator (a public `PushPlan` that wraps the protected `ISoundGenerator::SynthCommand`); replays reuse it with the replay listener |
| `URbImpactVoiceComponent : USynthComponent` | component (x30 per T0 table) | `CreateSoundGenerator` -> `FRbImpactVoiceGenerator : ISoundGenerator` (one callback per device block, `StartFrame` mapping of 5.1, renders its events, rolling, sampled layers from a PCM bank); outputs r*p referred to 1 m; pitch 1, Doppler off, `bAlwaysPlay`, not virtualised |
| `URbLooseBallAudioComponent` | component on the Chaos loose-ball actor | AU-25 floor hits and rolling from hit notifies (impulse, physical material) |
| `URbTableSampleBank` | `UDataAsset` | sampled table layers (tip, pocket, gully, tray, coin-op) decoded to float PCM at load for the generators (decoder VERIFY: `ICompressedAudioInfo` / sound-wave proxy reader) |
| `URbVenueAudioProfile` | `UDataAsset` | per venue: IR, reverb submix, ambience set, crowd set, jukebox playlist, rail bank and cushion contact time for its table preset, cloth rolling gain, zone mixes |
| `URbAudioSubsystem` | world subsystem | listener, mix states (activate/deactivate control bus mixes), dynamic-range mode, A/V offset, camera audio perspective |
| `URbVoiceLineSubsystem` | world subsystem | line selection (character, category, cooldowns, no repeats), priority, sends subtitle and caption events to the UI's `URbSubtitleSubsystem` (UIX), lip-sync hooks later |
| `URbFoleyComponent` | component on pawn and NPCs | animation-notify-driven MetaSound one-shots (chalk, steps, hands) |
| `ARbJukebox` | actor | coin/selection interaction, playlist, speakers (1-4 emitters), music through `MSS_RB_JukeboxSpeaker` |
| `URbAudioSettings` | `UDeveloperSettings` | per-mode `L_fs`, knee, ratio, bus offsets (4.2), envelope lookahead/release, directivity floor, `L_src`, lead frames, LOD distances, voice budgets |

### 8.3 Data flow per shot

```
Director step 8 (ARCH-UE 5.2): playback Play(shot, anchored at ContactTime)
  -> URbTableAudioComponent::OnShotStarted(FRbShot, ContactTime, Rate)      (the table that played the shot)
       worker task: filter events (1.1), map to voices (6.1), convert core -> UE with FRbCoords, compute per event:
       pulse params (3.2), per-ball listener gains P_n(cos th) at the predicted listener position, r, delays;
       copy each ball's TrajectorySegments (rolling), sampled-layer picks (seeded by the shot hash: replays identical);
       mono render at the predicted listener -> presentation envelope of the current mode (4.2)
  -> PushPlan(voice, plan) x N, FShotAudioClock.StartShot(...)
     (a break's plan is ready in < 20 ms; the tip strike lies LeadMin ahead, 5.1 item 5 covers a late plan)
Audio render thread, every block, per voice:
  anchor F0 (first voice of the table sets it) -> render due events completely into the overlap-add buffer
  (sub-sample) -> rolling at the cursor -> x presentation envelope -> output
Replay: same plan, new anchor, rate s (5.2).  Offline (trailer, MRQ): RawBreakAudioDsp renders stems from the plan with F0 = 0.
```

### 8.4 MetaSounds (everything that is not table synthesis)

Built headless with the MetaSound Builder API (`MetaSoundBuilderSubsystem`, `MetaSoundSourceBuilder`, saved to assets
through the MetaSound Editor Subsystem in the editor commandlet; VERIFY 5.8 Python names):

| Asset | Graph |
|---|---|
| `MSS_RB_OneShotRR` | wave array input, random-no-repeat pick, pitch +-50 cents, gain +-1.5 dB, optional low-pass; used by all foley/chores |
| `MSS_RB_VelocityLayers` | 5 wave arrays + velocity input -> equal-power crossfade of two neighbouring layers + fine gain (4.3) |
| `MSS_RB_AmbienceBed` | two seamless loops with random start offsets, slow gain LFO (+-1 dB, 20-60 s) |
| `MSS_RB_Compressor` | cooler compressor state machine (start one-shot, loop, stop one-shot, randomised cycle) |
| `MSS_RB_NeonHum` | 120/100 Hz + harmonics with jitter, crackle trigger (AU-74) |
| `MSS_RB_Walla` | layered beds + spatial one-shots, density input from occupancy |
| `MSS_RB_Heartbeat` | two-thump generator, bpm and level inputs (AU-59) |
| `MSS_RB_JukeboxSpeaker` | music wave player -> speaker IR (convolution) -> small-speaker EQ (90 Hz-9 kHz, mid bump) -> mild saturation |
| `MSS_RB_Voice` | wave player + distance/occlusion-driven EQ, subtitles metadata |

### 8.5 Asset layout and naming

```
Content/Audio/Table/{Tip,Pocket,Gully,Tray,CoinOp}/SW_RB_<Thing>_<Layer>_<NN>
Content/Audio/Foley/{Chalk,Cue,Hands,Clothes,Rack,Coins,Drinks}/SW_RB_...
Content/Audio/Footsteps/<Surface>/<Shoe>/SW_RB_Step_<Surface>_<Shoe>_<NN>
Content/Audio/Ambience/<Venue>/SW_RB_Amb_<Venue>_<Layer>_<NN>
Content/Audio/Crowd/<Venue>/SW_RB_Crowd_<Kind>_<NN>
Content/Audio/Voice/<Character>/<Category>/VO_RB_<Character>_<Category>_<NNN>
Content/Audio/Music/Jukebox/<Band>/MU_RB_<Band>_<Title>;  Content/Audio/Music/Menu/MU_RB_Menu_<Title>
Content/Audio/IR/<Venue>/IR_RB_<Venue>_<Position>
Content/Audio/MetaSounds/MSS_RB_*, MSP_RB_*
Content/Audio/Mix/SUBM_RB_*, CB_RB_*, CBM_RB_*, MP_RB_* (modulation parameters), PP_RB_* (parameter patches),
                  ATT_RB_* (attenuation), CON_RB_* (concurrency), CRV_RB_* (convolution reverb presets)
```

Raw source files (library WAVs, own recordings, TTS takes, music masters) live **outside Content** in a private store
(not in a public repository: most licences forbid redistribution of the raw files, see 9.1) and are imported by a
headless script (`Tools/unreal/editor/import_audio.py`, future, reads a manifest with loudness tags). Import: 48 kHz,
mono for point sources, stereo only for beds and music. New sound waves default to `SoundAssetCompressionType =
ProjectDefined` (5.8.3 `SoundWave.cpp`); the import script sets it explicitly: PCM for the short table layers that
`URbTableSampleBank` decodes to float at load, Bink Audio for foley, ambience, voice and music (quality VERIFY by ear);
table layers and foley `ForceInline`/retained, ambience and music streamed. Music comes from the shared
`Art/DiveBar/jukebox.json` track list (VEN 8.4, 13.1), so title strips and audio never disagree.

### 8.6 Performance budget (RTX 3070 Ti PC, High preset)

Audio render thread <= 25 % of a 10.7 ms block on average (2.7 ms), <= 60 % peak in a break. Memory: dive bar audio
<= 120 MB resident (samples + IRs), music streamed. Table synthesis worst case: the densest block of the dive-bar break
holds 27 impacts, ~4.6 M multiply-adds, which must be spread over the 4 source workers and vectorised (3.6); the
plan-time mono render for the presentation envelope (~15 M multiply-adds per break) runs on a worker, not on the audio
thread.

### 8.7 Configuration (`Config/`, when unfrozen)

`DefaultEngine.ini`: the WindowsTargetSettings audio keys of 7.4; Audio Modulation enabled; `bUseAudioThread` default.
`DefaultGame.ini`: `URbAudioSettings` defaults. VERIFY every key in 5.8.3 before committing.

### 8.8 Tests

Headless runs use `-NoSound` today (ARCH-UE 9), which creates no audio device at all. Engine-level audio tests must
run without `-NoSound` (`-NullRHI` is fine: audio does not need the renderer); on a machine without an output device the
5.8.3 mixer falls back to its null-device thread (`IAudioMixerPlatformInterface::StartRunningNullDevice`), which renders
at the device rate in real time. Requested change for the `Tools/unreal/rbue.py` owner: a `--sound` switch that drops
`-NoSound` for `RawBreak.Functional.Audio.*` (not done here; Tools/unreal is frozen for this work).

See section 14 for the numeric test list. Offline DSP tests (`RawBreak.Unit.Audio.*`) need no device; engine-level
tests (`RawBreak.Functional.Audio.*`) record `SUBM_RB_Master` with `UAudioMixerBlueprintLibrary::StartRecordingOutput(
WorldContext, ExpectedDuration, Submix)` / `StopRecordingOutput(WorldContext, EAudioRecordingExportType::WavFile, Name,
Path, Submix)` (signatures checked in 5.8.3) and analyse the WAV with the prototype's `--analyze` (metrics, LUFS, dBTP).

### 8.9 Work packages

| WP | Content | Depends on |
|---|---|---|
| **AU-0** spike (1 day) | prove in 5.8.3: a `USynthComponent` + `ISoundGenerator` voice with one callback per block; `GetAudioClock()` read in `OnBeginGenerate` / `OnGenerateAudio` (5.1); measure `L_src`; two voices, the second started 3 game frames later, the same impulse -> 0 samples skew; impulses 0.5104 ms apart in two voices -> 24.50 +- 0.05 samples; submix recording without `-NoSound`, also on the null device | M1 merged |
| AU-1 DSP core | `RawBreakAudioDsp`: shape table, decimation, kernel design, modal banks, rolling, renderer, presentation envelope, BS.1770 meter; golden tests vs `golden_runtime_48k.json` | - (can start now: no engine dependency) |
| AU-2 table audio | voices, plan builder, per-table anchor clocks and LOD tiers (6.6), replays and replay perspectives, loose-ball audio (AU-25), settings | AU-0, AU-1 |
| AU-3 sourcing | library search and licence ledger, import pipeline, loudness tags (section 9) | - (can start now) |
| AU-4 dive-bar ambience | beds, one-shots, neon, cooler, TV, crowd, zones, IR | AU-3, venue geometry |
| AU-5 mix | submixes, control buses, mixes, dynamic-range modes, settings sliders | AU-2 |
| AU-6 voice | characters, line lists, TTS generation, import, `URbVoiceLineSubsystem`, subtitles | PO: credits, voices |
| AU-7 music | PO generates the tracks (11.3); jukebox actor and speaker chain | PO |
| AU-8 foley and chores | chalk, coins, rack, footsteps, cue handling | AU-3, animations |
| AU-9 recording session and calibration | own recordings, IR sweep, fit of 3.8 | PO: venue access |

---

## 9. Sourcing strategy and licences

Rule for every file: licence checked **on the download date**, logged in `Docs/licenses/asset-ledger.csv` (UE 7.3; audio
columns added: original filename, library / bundle and year, author and attribution text, AI tool + plan + prompt ID +
generation date). No CC BY-NC, no "Sampling+", no files with recognisable real brands, voices of identifiable private
persons, or broadcast content.

### 9.1 Decision matrix

| Category | 1st choice | 2nd | 3rd | Never |
|---|---|---|---|---|
| Ball-ball, cushion, facing, rail cap, slate, liner | SYN (own code) | recorded clicks as velocity layers, scheduled the same way (only if the blind comparison AU-T20 prefers them) | - | AI SFX |
| Tip strike, miscue, pocket drops, gully, tray, coin-op, ball release | REC (bar session) | Sonniss GDC | Freesound CC0 / CC BY | AI for these (they are hero sounds) |
| Foley (chalk, hands, cloth, coins, glass, stools, footsteps) | Sonniss GDC | REC | Freesound, Pixabay | - |
| Ambience beds, TV, street, cooler, HVAC | Sonniss GDC | Freesound CC0 | Pixabay, ElevenLabs (paid) | real broadcasts, identifiable voices |
| Crowd walla / reactions | Sonniss GDC | Freesound CC0 (check the uploader recorded it themselves) | ElevenLabs (paid) | own recordings of guests (GDPR) |
| Voice lines | Higgsfield TTS | (later) voice actors | - | cloned voices of real people without written consent |
| Music | Gemini (Lyria 3.5), PO generates | commissioned musician | properly licensed library music | radio music, "sound-alike" prompts naming real artists |

The roadmap's comparison (library recordings vs. physics synthesis vs. AI SFX) is test AU-T20. Higgsfield's sound
models (`mirelo_text_to_audio`, `sonilo_music`) are "game pipeline only" and not usable for us, so the AI candidate is
ElevenLabs SFX, and only if the PO approves a paid plan (Q2).

### 9.2 Sonniss #GameAudioGDC bundles (free, yearly)

Licence (version 2.0, effective 2026-08-27; earlier downloads fall under the version of their download date):
royalty-free, commercial use allowed, **no attribution**, unlimited projects for life, may be sold as incorporated into
the licensee's project and shared with the team, **not** sold or redistributed standalone or as a library, and
**expressly not usable to train AI**. Consequences: fine for the game; never upload bundle files to an AI tool (e.g. as a
reference for ElevenLabs or a voice changer); keep the raw bundle out of any public repository. Download the yearly
bundles (one per year since 2015, each tens of GB, ESTIMATE; ask the PO before downloading), index filenames, search by keyword
(billiard, pool, snooker, chalk, coin, arcade, bar, pub, crowd, walla, fridge, neon).

### 9.3 Freesound

Per file: **CC0** (anything), **CC BY 4.0** (commercial use allowed with attribution in the credits: author, title,
link, licence), **CC BY-NC** not allowed (commercial game), **Sampling+** (retired licence) avoided. Freesound has
billiard/pool recordings of varying quality; each one is logged with its licence snapshot. Watch for field recordings
that contain intelligible speech (GDPR, personality rights): exclude.

### 9.4 Pixabay

Pixabay Content License: free commercial and non-commercial use, no attribution, modification allowed; forbidden:
selling or distributing the content standalone in substantially the same form, using content with recognisable brands
or people in a misleading or commercial-endorsement way, trademark use. In a game the audio is incorporated, so it is
allowed. Risk: uploads are not vetted as rigorously as a curated library (a sound could be ripped from elsewhere) and some
are AI-generated; use Pixabay only for non-hero ambience and keep the page URL and a PDF snapshot in the ledger.

### 9.5 ElevenLabs sound effects (gap filler only)

Paid plans (from Starter) include a commercial licence for generated sound effects; the free plan is non-commercial and
requires attribution (VERIFY on the ElevenLabs pricing and terms pages at generation time; their help-centre page was not
reachable from here). Rules: generate only on a paid plan, keep the invoice and generation history, never feed library
files as prompts/reference, and disclose on Steam (9.8). Use for: distant street textures, TV crowd bed, restroom hand
dryer, odd props; not for table hero sounds.

### 9.6 Higgsfield TTS for voice lines

Terms of Use (effective 2026-07-26): Higgsfield claims no ownership of inputs or outputs and does not restrict commercial
use of outputs (4.4), rights survive cancellation and can be transferred; outputs may not be used to train AI
(5.1(iii), 5.2(iv)); for voice inputs the user must hold the consents of the person whose voice it is (5.3); an output
may not be presented as human-made (5.2(xi)); **third-party models run under the provider's acceptable-use policy as
well** (section 8; the more restrictive terms apply). Help centre: commercial rights are identical on all plans; free
outputs carry a watermark, paid ones do not. Consequences: generate the final takes on a paid plan; the credits say
"Voices: AI text-to-speech (Higgsfield, <model / engine>)" and never invent voice-actor names (5.2(xi)); a later human
voice actor replaces lines one by one without changing IDs.

Available TTS models (Higgsfield model list, 2026-09-28): `text2speech_v2` (engines ElevenLabs, MiniMax, Seed Speech,
Vibe Voice, Cozy Voice; preset or own voices), `qwen_audio_tts` (Alibaba Qwen Audio 3.0 TTS Flash, natural-language
direction of emotion/speed, 13 languages incl. English and German), `seed_audio` (ByteDance Seed Audio 1.0, speech
rate/pitch/loudness controls). **Not usable** for us: `inworld_text_to_speech`, `mirelo_text_to_audio`,
`sonilo_music` (marked "Game pipeline only"). Output: WAV, 48 kHz where offered.

### 9.7 Gemini music (Lyria 3.5 in the Gemini app)

Facts checked on 2026-09-28: the Gemini app generates full tracks up to about 3 minutes with the "Pro" model, 18+ only,
Keep Activity on, delivered as a video file with cover art; every track carries an inaudible **SynthID** watermark;
naming an artist is treated as broad inspiration. Google's Terms of Service (effective 2026-07-30): Google does not claim
ownership of generated content; using AI-generated content to develop machine-learning models is prohibited; similar
content may be generated for others. The Gemini app help pages do **not** state explicitly whether music tracks may be
used commercially (the Gemini API terms, 2026-03-23, are for professional and business use and also disclaim ownership).
Therefore (PO, before generating):

1. Read the current Gemini Apps terms and the generative-AI prohibited-use policy on the day, save them as PDF with the
   date into the ledger folder; use a paid Google AI plan if generation limits or terms differ by plan.
2. If the terms exclude commercial use of app output, generate through the Gemini API / AI Studio (Lyria 3.5 is listed
   there) under the developer terms instead.
3. Accept that AI-generated music is likely **not copyright-protectable** (US Copyright Office position on purely
   AI-generated works); anyone may reuse the tracks, and Content ID claims by third parties on similar music are
   possible: register nothing, keep the generation records to answer claims.
4. Extract audio from the delivered video losslessly (ffmpeg `-c:a copy` or decode to 48 kHz WAV), keep the original.

### 9.8 Steam AI disclosure

Steam's Content Survey requires disclosure of pre-generated AI content that ships with the game (clarified 2026-01-16:
player-facing content only; development tools are exempt). Draft text: *"Some voice performances were created with AI
text-to-speech, some background music (heard on the in-game jukebox and in the menu) was generated with Google's Lyria
model, and a small number of ambient sound effects were generated with ElevenLabs. All of it was selected, edited and
mixed by the developer. The ball, cushion and table sounds are produced by our own physics simulation, not by AI."*

### 9.9 Own recordings (quality target, UE 8.5 kept)

Session plan and equipment as UE 8.5, extended: a coin-op bar box specifically (coin slide, ball release, gully,
tray, cue-ball return), the sound-level-calibrator reference tone for absolute levels, 240 fps phone video for speeds,
and the room IR sweep. Written permission of the bar owner; no guests' voices (GDPR).

---

## 10. Voice lines

### 10.1 Cast (dive bar, V1; the roster of VEN 1.1-1.2, all fictional)

| Character (VEN) | Role | Voice brief (TTS direction) | Lines V1 |
|---|---|---|---|
| **Terri Wisniewski** (58) | owner and bartender, holds the coin-op key (HF-71/73), racks for a tip, last call | woman, late 50s, smoker's rasp, dry, unhurried, northern Ohio accent | 45 |
| **Eddie "Deacon" Marsh** (74) | the mentor on stool #10: all diegetic feedback (HF 1.7, 3.9, HF-10, HF-18 drill) | man, 70s, gravelly, slow, warm, few words | 90 |
| **Big Lou Pruitt** (50s) | opponent, hits hard, knows the table rolls toward the jukebox | man, 50s, loud, big laugh, blunt | 60 |
| **Nina Okafor** (30s) | opponent, Tuesday league captain ("Low Bridge Bombers") | woman, 30s, quick, confident, encouraging, competitive | 60 |
| **Sonny Castellano** (40s) | hustler, Friday money games, sandbagging, loose racks | man, 40s, low, calm, friendly until the money is down | 60 |
| College kids (3 voices) | weekend opponents and loud booth chatter | 20s, mixed, excited, sloppy | 3 x 25 |
| **Ray** (60s) | never plays, watches the TV | man, 60s, mumbles at the TV (chatter layer only) | 15 |

Total V1 about 405 lines, 3 takes each (about 1,200 generations) plus retakes.

### 10.2 Categories and example lines

| Category | Trigger | Examples |
|---|---|---|
| Greeting / challenge | walk up, quarters on the rail | "You got next?" / "Put your quarter up, we'll see." |
| Money talk (HF money games) | bet offer, accept, pay-up | Sonny: "Twenty a rack. Unless that's too rich." / Big Lou: "Double or nothing. Come on." |
| Calls (8-ball rules) | call-shot rules | "Eight, corner pocket." (points) / "Bank it... side." |
| Reactions to own shot | make / miss / scratch / foul | Big Lou: "Oh, come ON." / Nina: "Well, that's not what I meant." / college kid: "Wait, is that bad?" |
| Reactions to player | player's make / miss | Nina: "Nice shape." / Sonny: "Hm." |
| Mentor (max 1 per 3 shots, never during the opponent's turn; UIX mentor setting) | HF 3.9 diagnosis, HF-10 head lift, HF-50 tilt, chalk | Deacon: "Stay down on it." / "Table rolls toward the jukebox, kid." / "Chalk. Every shot." / "That one was your stroke, not the table." |
| Bartender | table key, drinks, last call | Terri: "Table's locked 'cause nobody paid for it." / "Last call, gentlemen, and Nina." |
| Chatter (unintelligible unless close) | idle | small talk lines recorded at low intensity; mixed under walla; Ray at the TV |
| End of game | win / lose | "Good game." (handshake) / Big Lou: "Rack 'em. Again." |

Style rules: short, overlapping, never announcer-like; stumbles and restarts allowed; no brand names, no real places or
people; profanity mild (rating). Lines addressed to the player play only when the speaker faces the player or is within
3 m; everything else goes to the chatter layer.

### 10.3 Generation and post

1. Script per character in a spreadsheet (ID, text, direction, category), IDs = asset names (8.5).
2. Higgsfield: voice pick per character from the preset voices (the PO listens to previews: Q3); `qwen_audio_tts` when
   emotion direction is needed ("tired, amused, mutters"), otherwise `text2speech_v2`. 48 kHz WAV, 3 takes.
3. Post: trim, de-click, loudness -23 LUFS (short-term max -18), then **de-AI**: add 30-60 ms of room tone head/tail,
   random breaths, 0.5-1 dB of level drift; in engine the voice gets the venue reverb and distance EQ, so the dry file
   stays dry. The normalised file says nothing about how loud the person speaks, so the physical import tag (4.1) comes
   from the line's vocal effort in the script (ANSI S3.5 speech levels at 1 m: normal 62, raised 68, loud 75, shouted
   82 dB SPL; bar speech is mostly raised because of the Lombard effect); the TTS direction asks for the same effort.
4. German Kneipe (later): German cast and lines (`qwen_audio_tts` language `de`), written by Claude, checked by the PO.

---

## 11. Music: jukebox and menu

### 11.1 Rules

- Diegetic only during play: the jukebox (paid by NPCs or the player: $1 = 3 songs, a money sink) plays through its own
  speakers and, in the dive bar, two ceiling speakers. No non-diegetic score during play.
- Menu music is separate (non-diegetic). The menu scene "Closing Time" (UIX 6) runs its first 2.5 s with scene sound
  only (rain outside, neon hum, cooler; the venue's *AfterHours* state, 12.1); the menu theme fades in after that at
  about the room level and fades into the venue ambience when a session starts. The theme carries no baked-in room tone
  (the scene provides it).
- **Streamer mode** (UIX 13.8) plays only tracks marked `StreamSafe` in `jukebox.json`: our own tracks (generated or
  commissioned, no third-party licence) that passed a private test upload to YouTube and Twitch without a claim (AI
  music can collide with claimed catalogues); third-party licensed tracks, should any be added later, are never
  `StreamSafe`; when in doubt the jukebox plays the instrumental versions. UIX 13.8's description ("replaces licensed
  jukebox music with original tracks") needs this wording (section 16).
- Chain: track (mastered around -14 LUFS integrated) -> `MSS_RB_JukeboxSpeaker` (speaker IR, 90 Hz-9 kHz, mild
  saturation) -> spatialised emitters -> venue reverb. Playback level ESTIMATE 78-82 dBA at the bar, 72-76 dBA at the
  table.
- Six fictional bands for V1 (names checked against a web search on 2026-09-28 with no exact match for "Blue Chalk
  Kings", "Southside Scratch", "Marquee Static", "Quarter Slot", "Kitchen Line Ramblers" and "The Late Racks"; the
  draft's "Railbird Revival" was replaced because "Railbird" is an existing band and a country/Americana festival in
  Lexington, KY; VERIFY all before release with a trademark and streaming-service search).

### 11.2 Playlist (dive bar, V1)

12-18 tracks (2-3 per band) for about 45 minutes before a repeat; the jukebox remembers what played (no repeat within
a night).

### 11.3 Six Gemini prompts (the PO runs them; Pro model, full length)

Paste each prompt as is. After a good result, ask "Make an instrumental version of the same track" (for scenes with
dialogue) and keep both.

**1. Kitchen Line Ramblers - "Quarter on the Rail" (country)**
> A 1970s honky-tonk country shuffle, about 2 minutes 50 seconds, 118 BPM, key of G major. Twangy clean electric
> guitar with chicken-picking fills, pedal steel, upright bass, brushed snare and a simple piano. Warm, slightly worn
> analog sound, a little tape hiss, not polished. Male baritone lead vocal with a light rasp, relaxed and a bit
> world-weary, harmony vocals on the chorus. Lyrics about putting a quarter on the rail of a pool table in a small-town
> bar and waiting all night to play the winner; chorus hook "put my quarter on the rail". Verse, chorus, verse, chorus,
> pedal-steel solo, final chorus. Do not mention any real brands, real places or real people.

**2. The Blue Chalk Kings - "Scratch on the Eight" (Chicago blues)**
> A slow Chicago-style electric blues, 12-bar shuffle, about 3 minutes, 72 BPM, key of E. Overdriven electric guitar
> with bent notes and slide, amplified harmonica, Hammond organ, walking bass, shuffle drums. Raw live-in-a-club
> sound, a little room echo. Gritty male vocal, weary and humorous. Lyrics about losing a money game of pool by
> scratching on the eight ball and owing the bartender; hook line "I scratched on the eight". Harmonica solo in the
> middle, guitar solo before the last verse. No real brands, places or people.

**3. Marquee Static - "Last Call Lights" (early-80s heartland rock / new wave)**
> An early-1980s heartland rock song with a new wave edge, about 3 minutes, 126 BPM, key of A major. Jangly chorus-
> effect guitars, driving eighth-note bass, gated snare drum, a bright analog synth pad and a short synth lead.
> Punchy but slightly lo-fi, like a single from a small label. Female alto lead vocal, confident and a little
> melancholic, gang vocals on the chorus. Lyrics about the buzzing neon sign of a bar at closing time and driving
> home under streetlights with the windows down; chorus "last call lights". Guitar-and-synth instrumental break.
> No real brands, places or people.

**4. Quarter Slot - "Sticky Floor" (mid-90s alternative / grunge)**
> A mid-1990s alternative rock / grunge song, about 3 minutes 10 seconds, 96 BPM, drop D tuning. Quiet verses with a
> clean, slightly detuned guitar and bass, loud distorted choruses with fuzz guitars and heavy drums. Tired, sarcastic
> male tenor vocal that gets raw in the chorus. Lyrics about the same dive bar every Friday night, a sticky floor, a
> crooked house cue and friends who never leave; chorus hook "stuck to the floor again". Noisy guitar solo before the
> last chorus, abrupt ending. No real brands, places or people.

**5. Southside Scratch - "Break 'Em Loose" (late-60s Southern soul)**
> A late-1960s Southern soul / R&B groove, about 2 minutes 40 seconds, 104 BPM, key of B-flat. Tight horn section
> (trumpet, tenor and baritone sax), clean rhythm guitar, Hammond organ, tambourine, melodic bass, crisp live drums.
> Warm vintage recording with some room sound. Powerful female soul lead vocal with call-and-response backing
> vocals. Lyrics about breaking the rack on a Friday night and letting everything run, feeling lucky; chorus hook
> "break 'em loose". Horn break in the middle, fade-out ending. No real brands, places or people.

**6. The Late Racks - "Closing Time, Table Six" (menu theme, instrumental)**
> An instrumental, slow and atmospheric piece for a game menu, about 2 minutes 30 seconds, loopable (the ending leads
> back into the beginning), 70 BPM, key of D minor. Reverb-drenched baritone electric guitar playing a simple lonely
> melody, soft upright bass, brushed drums with very light swing, distant Rhodes electric piano, a faint vinyl
> crackle. Clean studio recording without any background noise, room sound or ambience. Mood: calm, a bit melancholic,
> cinematic, like the last hour before the lights go out in a small bar. No vocals, no sudden changes.

---

## 12. Venue audio profiles

### 12.1 Dive bar (V1): sound map

Positions are the VEN element IDs and anchors (VEN 2, 3.3); the audio emitters are placed from the same `layout.json`.

| Element (VEN) | Layers | Notes |
|---|---|---|
| Pool table, coin-op bar box, `TABLE_7FT_BAR`, ball set `OldBarOversizedCue` | AU-20..AU-48 | bar-box rail bank (3.5); gully splines, separator and return cup A3, trap row and tray A2, coin slide A1, coin door A4 (VEN 3.3) |
| Jukebox E10 / H12 (1996 CD jukebox, 1.02 m off the head-right corner) | AU-90 | 2 internal speakers + the 2 ceiling speakers (M16) (VEN 10); silence and CD-changer mechanics between songs; track list from `Art/DiveBar/jukebox.json`; the table's roll-off points toward it (HF-50) |
| Back bar E04 with the under-counter coolers, POS, ice bin | AU-72, AU-73, AU-79, Terri | most activity; cooler compressor cycles |
| Neon signs N1-N5 (VEN 4.3) | AU-74 | 120 Hz buzz; the sign with the dead letter only sizzles, never flickers |
| TV-1 (bar) and TV-2 (pool room back wall) | AU-75 | sports-like broadcast, no real leagues or brands; Ray talks to TV-1 |
| Dart machine E09 | AU-78 | college kids on weekends block the dart lane |
| Entrance (glass-block storefront) | AU-76 | street, rain on wet nights |
| Restroom corridor (rear addition) | AU-77, zone mix | occluded |
| Floor | AU-65, AU-25 | VCT tile on concrete (VEN 2.2, oxblood/black checker), sticky patches near the bar, beige VCT in the corridor, rubber mats in the bartender aisle |
| Cue rack E12 | AU-08 | butt knocks a house cue: clatter (VEN TS-2) |

Night arc (occupancy, VEN hours 20:00-02:30): 20:00 about 6 guests -> 22:00 15 -> 00:00 25 on Fridays (crowd layers and
walla density follow; the jukebox plays more often later). Lighting states (VEN 4.6, UIX 6) switch the ambience set:

| State | Ambience |
|---|---|
| *Open* | everything above |
| *Lights-Up* (last call, 02:00) | Terri's line, the troffers switch on (starter tick + a faint 120 Hz ballast hum from the one aged tube, ESTIMATE), jukebox off, walla thins to a few voices, room tone + cooler |
| *AfterHours* (main-menu scene "Closing Time") | no people, rain on the storefront and street heard through the glass block (low-passed 1.5 kHz), neon hum, cooler cycles, one lamp over the table, jukebox idle hum; the menu theme fades in after the first 2.5 s (11.1) |

### 12.2 Later venues

| Venue | Differences |
|---|---|
| German Kneipe | 7-ft coin-op with optical separation and 60.3 mm cue ball (EQP 6.2), wood panelling (warmer, shorter RT60), 100 Hz neon hum, German chatter and cast, a slot machine ("Spielautomat") jingle, tap beer pours, Kneipe music prompts later |
| Basement | 8-ft home table (heavier, less boomy rail bank), boiler, water pipes, fluorescent tube hum, concrete RT60 |
| Pool hall | many tables: distant clicks from other tables (AU-84), carpet, counter, cue-case zips, pro-table leather pockets (AU-34) |
| Arena | referee, audience hush and applause, broadcast PA, large RT60; pro table; replays from broadcast cameras |

---

## 13. Trailer and replay audio

- Every table sound can be rendered **offline and deterministically** from the stored shot (same `RawBreakAudioDsp` code,
  anchor 0), as stems: table, foley, ambience, crowd, voice, music. Frame-exact with Movie Render Queue output. Offline
  stems are rendered physically (no presentation envelope, 4.2) at 32-bit float, so the trailer mix decides the
  dynamics.
- Cold open (trailer-plan 1): phone perspective (6.2), chalk squeak, off-screen "watch this" (TTS or a recorded line),
  no music; the pocket drop is the cut point (trailer-plan 2).
- Slow-motion break (trailer-plan 3): "physical style" rendering (5.2). Footage captured at 1000 fps and played at 25 fps
  is a slowdown of s = 1/40, which turns the 2 kHz click into a ~50 Hz thud that is still the true pulse, stretched;
  layered with a real-speed click at the cut. Deeper slowdowns (the plan says "1000+ fps") push the content below
  40 Hz: there the renderer switches to film style (5.2) and the thud is designed on top.
- Music: the trailer plan requires licence-clean music (commissioned or properly licensed). The Gemini jukebox tracks
  qualify only if the PO's terms check (9.7) covers promotional use; AI music cannot be protected and may draw
  Content ID claims on the trailer's platforms, so a commissioned track is the safe default.
- Loudness for delivery: -14 LUFS integrated, -1 dBTP (ffmpeg `loudnorm`), per trailer-plan post pipeline.

---

## 14. Test cases (numeric)

Prototype values are exact for the Python reference; the C++ port must meet the same tolerances (offline) and the
engine-level tolerances (with a device). The prototype's `--selftest` covers AU-T01..T08, T10 (on-axis 1 m/s), the
presentation cap of T16 and the loudness meter.

| ID | What | Input | Expected | Tolerance |
|---|---|---|---|---|
| AU-T01 | Tsuji alpha | e = 0.95 | alpha_T = 0.036893 (core table 0.036915) | 5e-4 |
| AU-T02 | Contact time, undamped | std pair, v = 0.5 / 1 / 5 / 10 m/s | 378 / 329 / 238 / 207 us (COL 3.9.3) | 1.5 us |
| AU-T03 | Shape table vs integration | v = 0.3 / 3 / 12 m/s | F_max and impulse ratio 1 | 0.2 % / 0.5 % |
| AU-T04 | Universal constants | e = 1 | tau = 3.2181 | 2e-3 |
| AU-T05 | Lamb solver | nu = 0.25, n = 2 | k_T a = 2.640 | 0.01 |
| AU-T06 | Radiation, incompressible limit | n = 1, 1 Hz, r = 0.1 m | rho0 a^3 / (2 r^2) | 1 % |
| AU-T07 | Causality | 2 m/s click, listener at (-1, 0, 0.3) m | energy before (r - a)/c - 0.2 ms | < 1e-4 of total |
| AU-T08 | Sub-sample scheduling (UE E5) | impact pairs at 10.0000 / 10.5000 ms, 10.0073 / 10.5177 ms, 10.0104 / 10.1146 ms | 24.0000, 24.4992, 5.0016 samples (group delay 0.5-6 kHz), exact and runtime renderer; engine: 0.5104 ms in two voices -> 24.50 samples (submix recording) | 0.01 sample offline / 0.05 sample engine |
| AU-T09 | Level law (replaces UE T23) | 2 vs 1 m/s, shooter listener | +7.45 dB peak; 24.3 dB/decade over 0.25-12 m/s | 0.1 dB; 0.3 dB/decade |
| AU-T10 | Runtime vs exact | 0.25 / 1 / 4 / 12 m/s, shooter and observer | 1/6-oct rms error <= 0.05 dB on-axis, <= 0.3 dB side-on | as stated |
| AU-T11 | C++ golden vectors | the 3 cases of `golden_runtime_48k.json` rendered by `RawBreakAudioDsp` offline with its own kernels (step 1 also against `force_frac_0.25`) | residual energy vs `p_pa` | <= -100 dB (Draft v1 compared against the exact render at -50 dB, which even the Python runtime model misses: -48 to -15 dB) |
| AU-T12 | Event coverage | `divebar_break8.json` | voices triggered = 79 impacts + 3 gully runs; pressing contacts silent | exact |
| AU-T13 | Replay determinism | same shot rendered twice offline | bit-identical buffers | exact |
| AU-T14 | Directivity | 1 m/s, shooter vs observer; horizontal scan of 3.6 | 34.0 dB difference (floor off); scan values of 3.6 | 0.5 dB |
| AU-T15 | Radiated energy | 1 m/s, free field | E_rad / KE = 9.6e-5 | 2 % |
| AU-T16 | No clipping, presentation | dive-bar break at the breaker's ears, all three modes, envelope of 4.2 | true peak before the master limiter -1.5 / -1.2 / -1.2 dBTP; after it <= -1 dBTP; limiter gain reduction <= 1 dB | 0.3 dB |
| AU-T17 | Ducking timing | voice line start | crowd -4 dB reached after 0.15 s, released 0.6 s after the line | 20 ms |
| AU-T18 | A/V sync | engine: flash + click test scene, high-speed capture or loopback | audio - video offset | <= 10 ms after calibration |
| AU-T19 | CPU | the densest block of the dive-bar break (27 impacts) on the dev PC | audio render thread <= 60 % of a block at peak | - |
| AU-T20 | Blind comparison (roadmap window 3) | 12 clicks and 6 cushion hits at matched speeds: (a) library recordings (Sonniss/Freesound), (b) synthesis, (c) ElevenLabs SFX if approved; loudness-matched, randomised, in the bar ambience | "which is a real recording?" and a 1-5 realism score by the PO and 2-4 other listeners | synthesis stays first choice if its realism score is not worse than (a) by more than 0.5 |
| AU-T21 | Inter-voice alignment | engine: the same impulse in two ball voices, the second started 3 game frames later | onset difference in a submix recording | 0 samples |
| AU-T22 | Session loudness | 10-minute scripted dive-bar session (ambience, 20 shots, 6 voice lines), per mode | integrated -29 / -24 / -20 LUFS | +-2 LU |
---

## 15. Open questions for the product owner

1. **Bar recording session** (UE 8.5): can you get 3-4 hours in a bar with a coin-op table (with the owner's written
   permission)? It turns every ESTIMATE of 3.2/3.5 into fitted values and gives us the coin-op mechanics and a room IR.
2. **ElevenLabs:** is a paid plan (Starter) acceptable for a handful of ambience gap-fillers, or skip AI sound effects?
3. **Voices:** Higgsfield credit budget for ~1,300 TTS generations (405 lines x 3 takes = 1,215, plus retakes); final
   takes on a paid plan (free outputs are watermarked); please listen to preset voice previews for the characters
   (10.1) or let Claude pick.
4. **Gemini music:** which Google plan do you have; please check the terms of the day (9.7) and run the six prompts
   (11.3); keep the downloads and a PDF of the terms.
5. **Listening test** of `Tools/audio/out/ref/` and `Tools/audio/out/divebar_break8_{wide,normal,night}.wav`
   (checklist in 3.7): do the clicks, cushion and break sound real? Is the strong directivity (27-29 dB quieter
   exactly side-on, -10 dB at 60 deg) right in the direct sound, or should the -20 dB floor be raised?
6. **Dynamic range:** UIX 13.8 proposes Wide for headphones and Normal for speakers. Wide is quiet on purpose
   (-29 LUFS, like a film; raise the headphone volume) so that a 1 m/s click stays physical; is that acceptable, or
   should Wide be louder with harder shots compressed more (4.2)?
7. **Headcam "camera mic" perspective** (6.2): add it as an option together with the Headcam camera?
8. **Repository visibility:** is the GitHub repository private? It must stay private once licensed library audio
   (Sonniss etc.) is imported, because raw files and near-raw assets may not be redistributed.
9. **Sonniss bundles download:** several hundred GB for all years; OK to download (to which drive)?
10. **Band names** (11.1): OK as fictional names? ("Railbird Revival" was renamed "Kitchen Line Ramblers" in review;
    the cast follows the venue spec's proposed roster.)
11. **AI SFX in the comparison test** (AU-T20, roadmap window 3): include ElevenLabs clicks (needs Q2), or compare
    library recordings with the synthesis only?

---

## 16. Changes requested in other specs (not edited here; owners please apply)

| Spec | Section | Change |
|---|---|---|
| ue5-realism-plan | 8.2 | keep the level law (confirmed); drop the `cutoff ~ v^(1/5)` rule; point to audio.md 3 |
| ue5-realism-plan | 8.3 | Quartz is not the mechanism for physics events; per-shot anchor frame + per-voice sub-sample scheduling (audio.md 5.1) |
| ue5-realism-plan | 8.5 | add Pixabay, ElevenLabs (paid only), Higgsfield TTS, Gemini music rows; Sonniss licence 2.0 forbids AI training |
| ue5-realism-plan | 13 | T23 -> AU-T09 (+7.45 +- 0.1 dB from the synthesis), E5 -> AU-T08 |
| ue-architecture | 3, 5.2 step 9, 13 | audio classes (8.2), `OnShotStarted` hook with `ContactTime` and `Rate` from the playback component, work packages AU-0..AU-9 |
| physics-collisions | open question 1/3 | an audio recording session also measures ball-ball contact time (pulse width) and cushion contact time; share the data. Note: the audio cushion contact (2.5 ms at 1 m/s, ESTIMATE) and the CLI's `k_c = 1e6 N/m` (1.3 ms) disagree; both are ESTIMATEs, one measurement should set both |
| ue5-realism-plan | 8.4, 11.2 "Optional: Steam Audio UE plugin" | the Steam Audio plugin bundled with 5.8.3 is "Steam Audio (Deprecated)" 2.0-beta.17; use Valve's own plugin or Resonance Audio (in-box, beta) for HRTF (audio.md 6.3) |
| ue5-realism-plan | 8.3 | voice budget ">= 48 voices for ball impacts" -> 30 synth voices per table + LOD tiers (audio.md 6.6, 7.4) |
| ui-ux | 13.8 | Streamer mode description: "plays only jukebox tracks cleared for streaming (our own tracks)" instead of "replaces licensed jukebox music with original tracks" (audio.md 11.1); Dynamic range row: add the one-line mode descriptions of audio.md 4.2 |
| ui-ux | 5.6 | the audio-output step plays its break sample in the chosen dynamic-range mode with the volume hint of audio.md 4.2 |
| ui-ux | 13.8 | add "Focus while aiming: Off / Subtle / Strong" (default Subtle), the setting behind `CBM_RB_Focus` (audio.md 7.3) |
| venue-dive-bar | 10 | reverb row: RT60 0.8 / 0.6 / 0.5 s derived by Sabine (audio.md 6.4); the ceiling-tile finish (absorbent vs painted) decides between 0.6 and 0.9 s mid, please specify it in the materials |
| ue-architecture | 9.6 / `Tools/unreal/rbue.py` | a `--sound` switch that runs `RawBreak.Functional.Audio.*` without `-NoSound` (audio.md 8.8) |

---

## 17. Sources

Physics and acoustics
- L. L. Koss, R. J. Alfredson, "Transient sound radiated by spheres undergoing an elastic collision", J. Sound Vib.
  27(1):59-75 (1973): https://www.sciencedirect.com/science/article/abs/pii/0022460X73900357
- J. N. Chadwick, C. Zheng, D. L. James, "Precomputed Acceleration Noise for Improved Rigid-Body Sound", ACM TOG 31(4),
  SIGGRAPH 2012: https://www.cs.cornell.edu/Projects/Sound/impact ; https://dl.acm.org/doi/10.1145/2185520.2185599
- A. Petculescu, J. Riner, "Constraining the minute amount of audible energy radiated from binary collisions of light
  plastic spheres...", JASA 128(4):1575 (2010): https://pubmed.ncbi.nlm.nih.gov/20968327/
- J. Riner, A. Petculescu, "Non-Hertzian behavior in binary collisions of plastic balls derived from impact acoustics",
  JASA 128(1):132-136 (2010) (force exponent 6.25 % below Hertz, acoustic-to-incident energy ~100 ppm):
  https://pubmed.ncbi.nlm.nih.gov/20649208/
- G. McLaskey, S. Glaser, "Hertzian impact: experimental study of the force pulse and resulting stress waves", JASA
  (2010): https://courses.cit.cornell.edu/mclaskey/pubs/JASA2010.pdf (measured Hertz pulses, useful for the 3.8 fit)
- Impact noise radiated by collision of two spheres (comparison of simulation, experiment and analysis), J. Mech. Sci.
  Tech. (2011): https://link.springer.com/article/10.1007/s12206-011-0503-z
- H. Lamb, "On the vibrations of an elastic sphere", Proc. London Math. Soc. 13 (1882) (spheroidal/torsional modes;
  frequency equation as in Saviot et al., https://arxiv.org/pdf/cond-mat/0506353)
- Y. Tsuji et al. (1992) and the Hertz constants: physics-collisions.md 3.9.3 and its sources (Alciatore TP B.29,
  Marlow)
- Dr. Dave, cue tip contact time: https://drdavepoolinfo.com/faq/cue-tip/contact-time/
- AzBilliards forum, "Noise frequency of ball impact" (anecdotal, no measurement):
  https://forums.azbilliards.com/threads/noise-frequency-of-ball-impact.230230/
- Phenolic resin modulus range: https://www.researchgate.net/figure/Youngs-modulus-of-phenolic-composite-reinforced-with-varying-SLG-by-weight_fig1_240599858

Unreal Engine 5.8 (items marked "5.8.3 source-checked" were read in the installed engine at
`C:/Program Files/Epic Games/UE_5.8/Engine` on 2026-09-28: `Runtime/Engine/Classes/Sound/SoundGenerator.h`,
`Runtime/AudioMixer/Private/AudioMixerDevice.cpp` (`OnProcessAudioStream`, `UpdateAudioClock`),
`Runtime/AudioMixer/Private/AudioMixerSourceBuffer.cpp` (procedural rendering), `Runtime/AudioMixer/Public/Components/SynthComponent.h`,
`Runtime/AudioMixer/Public/AudioMixerBlueprintLibrary.h`, `Config/BaseEngine.ini`,
`Developer/Windows/WindowsTargetPlatformSettings/Classes/WindowsTargetSettings.h`, the `.uplugin` descriptors of Steam
Audio, Resonance Audio, Spatialization, Synthesis, AudioModulation, AudioGameplayVolume, AudioInsights, MetaSound,
`Synthesis/Classes/SubmixEffects/SubmixEffectConvolutionReverb.h`, `AudioInsights/Public/Messages/OutputMeterTraceMessages.h`,
`MetasoundEngine/Public/MetasoundBuilderSubsystem.h`, `MetasoundEditor/Public/MetasoundEditorSubsystem.h`; everything
else: VERIFY names in the installed build)
- 5.8 release notes (WASAPI default, Audio Insights, Audio Subtitles Beta, MetaSound templates/node configuration, CAT):
  https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes ; https://cdm.link/unreal-engine-5-8-and-audio/
- MetaSounds: https://dev.epicgames.com/documentation/en-us/unreal-engine/metasounds-in-unreal-engine ; Builder API:
  https://dev.epicgames.com/documentation/unreal-engine/metasound-builder-api-in-unreal-engine
- Audio Modulation reference: https://dev.epicgames.com/documentation/unreal-engine/audio-modulation-reference-guide-in-unreal-engine
- Convolution Reverb: https://dev.epicgames.com/documentation/en-us/unreal-engine/convolution-reverb-in-unreal-engine
- Quartz: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-quartz-in-unreal-engine ; forum on
  non-quantised cueing: https://forums.unrealengine.com/t/cueing-sounds-in-a-non-quantized-way-with-quartz/609667
- USynthComponent / ISoundGenerator: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AudioMixer/USynthComponent ;
  https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ISoundGenerator
- Audio Gameplay Volumes: https://dev.epicgames.com/documentation/unreal-engine/audio-gameplay-volumes-overview ;
  Audio Bus: https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-bus-overview ; Submix dynamics key source:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/SubmixEffectDynamicsProcessorSettings?application_version=5.1
- Steam Audio: https://valvesoftware.github.io/steam-audio/ ; https://github.com/ValveSoftware/steam-audio/releases ;
  UE plugin API: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/SteamAudio (the engine-bundled
  plugin is the deprecated 2.0-beta.17)
- Resonance Audio (Google, Apache 2.0): https://resonance-audio.github.io/resonance-audio/
- ITU-R BS.1770-4 loudness and true peak: https://www.itu.int/rec/R-REC-BS.1770 ; Sony ASWG-R001 (-24 LUFS console
  reference) as cited in game-audio loudness practice

Licences and terms (checked 2026-09-28)
- Sonniss GDC bundle licence v2.0: https://sonniss.com/gdc-bundle-license/ ; bundles: https://gdc.sonniss.com/
- Freesound licences: https://freesound.org/help/faq/
- Pixabay Content License summary: https://pixabay.com/service/license-summary/
- ElevenLabs sound effects / commercial use: https://elevenlabs.io/sound-effects ;
  https://help.elevenlabs.io/hc/en-us/articles/13313564601361-Can-I-publish-the-content-I-generate-on-the-platform
- Higgsfield Terms of Use (2026-07-26): https://higgsfield.ai/terms-of-use-agreement ; ownership FAQ:
  https://higgsfield.ai/creator-hub/help-center/account/who-owns-my-generations-and-can-i-use-them-commercially
- Google Terms of Service (2026-07-30): https://policies.google.com/terms ; Gemini API terms (2026-03-23):
  https://ai.google.dev/gemini-api/terms ; Lyria in the Gemini app: https://blog.google/innovation-and-ai/products/gemini-app/lyria-3/ ;
  https://gemini.google/overview/music-generation/ ; help: https://support.google.com/gemini/answer/16901237
- Steam AI disclosure (Content Survey): https://partner.steamgames.com/doc/gettingstarted/contentsurvey
- OpenAIR impulse responses: https://www.openair.hosted.york.ac.uk/

---

## Review log

### 2026-09-28 - adversarial review of Draft v1 (spec + `Tools/audio/click_synth.py`), fixes applied in place

Method: re-ran the prototype (selftest + full set, `Tools/audio/out/run.log`), wrote independent checks (fractional
scheduling, waveform residual of the runtime model, directivity scan, LAFmax, per-block impact density, coherent
summation in the break), read the 5.8.3 engine source and plugin descriptors on this machine, cross-read decisions.md,
roadmap, trailer-plan, venue-dive-bar (VEN), ui-ux (UIX), human-factors (HF), equipment (EQP), physics-collisions (COL)
and ue-architecture (ARCH-UE), and searched the literature for ball resonance and radiated-energy data.

| # | Severity | Finding | Fix |
|---|---|---|---|
| R1 | blocker | **Gain structure impossible.** `P_fs` 100 Pa + 10 dB put 0 dBFS at 124 dB SPL: the 76 dBA room would play at about -45 LUFS, speech lower, yet the targets said -24 / -20 / -16 LUFS. Normal's 1 ms-attack compressor cannot catch a 0.2-0.4 ms click. The "30-45 dB click-to-ambience peak ratio of real footage" was backwards (footage limits clicks; a click's LAFmax is ~28 dB below its peak). | 4.1/4.2 rewritten: per-mode `L_fs`, bus offsets, **plan-time presentation envelope** for the table stem (3 ms lookahead, 60 ms release, static knee/ratio), engine gain mapping (1.0 = 1 Pa, all bus gains <= 0 dB), new targets -29 / -24 / -20 LUFS. Prototype: `presentation_gain_envelope`, BS.1770-4 loudness, true peak, LAFmax, per-mode renders; measured -1.5 / -1.2 / -1.2 dBTP and -29.0 / -23.7 / -19.7 LUFS on the dive-bar break. |
| R2 | blocker | A per-impact gain law would not have worked either: 14 contacts of the rack cluster arrive at the ears within 0.2 ms and add coherently (sum 126.2 dB vs single impacts <= 125.4 dB; +5 dBFS after per-impact gains). | The envelope works on the summed stem (R1); 3.7 documents the cluster. |
| R3 | blocker | Anchor feasibility: "read the device clock in the generator" did not address inter-voice alignment (the two balls of one click are in different voices), the generator chunk size (default 1024 samples per call), pitch/Doppler resampling or virtualisation. | 5.1 rewritten from the 5.8.3 source: the clock advances after source rendering, procedural sources render synchronously; one callback per block, `StartFrame` from `GetAudioClock()` in `OnBeginGenerate`, measured constant `L_src`, pitch 1, Doppler off, `bAlwaysPlay`, no virtualisation, voices never started per shot. AU-0 and the new AU-T21 prove 0-sample inter-voice skew. |
| R4 | major | E5/AU-T08 proved nothing about sub-sample timing: 0.5 ms is exactly 24 samples at 48 kHz. | Selftest now checks 24.4992 and 5.0016 samples (group-delay estimate) for the exact and the runtime renderer; both within 0.001 sample. |
| R5 | major | AU-T11 (C++ vs `ref_ballball_*.wav`, residual <= -50 dB) was unattainable: even the Python runtime model differs from the exact render by -36 to -48 dB on-axis and -15 to -22 dB side-on. The spec's runtime algorithm (4x pulse + "half-band" decimation) was never prototyped; `fast_click` used a 768 kHz pulse and FFT fractional delays. | `runtime_render` implements the C++ algorithm line by line (129-point shape table, 4x evaluation with the fraction, 129-tap Kaiser decimation, per-order kernels, leaky near field, cloth IIR); `golden_runtime_48k.json` (3 cases incl. side-on and the oversized cue ball) + kernel files for 3 balls; AU-T11 now <= -100 dB against these. Accuracy vs exact unchanged (0.02 / 0.20 dB rms). `fast_click` removed. |
| R6 | major | Multi-table decision (2026-09-28) ignored: one shared clock, 30 voices, AU-84 "fake shots". | 6.6 new: per-table component and clock, LOD tiers T0/T1/T2 (30 / 4 / 1 voices), hall budget 43-58 voices, AU-84 uses the AI tables' real shots. |
| R7 | major | Balls off the table: the decision hands them to engine physics; AU-25 had a "floor mini-simulation" and a wood floor. | AU-25 driven by Chaos hit notifies on VCT; `URbLooseBallAudioComponent`; loose-ball emitter. |
| R8 | major | Headless tests: ARCH-UE runs with `-NoSound` (no audio device at all), so the planned submix-recording tests could not run. | 8.8: audio functional tests without `-NoSound` (null-device fallback exists in 5.8.3), requested `--sound` switch for `rbue.py`; recording API signatures checked. |
| R9 | major | HRTF plan pointed at the engine's Steam Audio plugin; in 5.8.3 that is "Steam Audio (Deprecated)" 2.0-beta.17, and "Binaural" needs a selected spatialization plugin. | 6.3: Resonance Audio (in-box, beta) for V1 HRTF, Valve's own Steam Audio plugin as the evaluation candidate; plugin list in 8.1 corrected from the descriptors. |
| R10 | major | Worst-case CPU underestimated ("~3 ms per 100 ms"): the densest block holds 27 impacts (~4.6 M multiply-adds in one callback); the cost ignored the image path. | 3.6/8.6: numbers, SIMD + source-worker requirement, plan-time pre-render fallback; AU-T19 targets that block. |
| R11 | major | Replay audio did not match UIX 10 (speeds 0.05-2x, Phone / Broadcast / Clean looks, 1-frame steps); "physical style at 1/1000 speed gives a deep thud" is wrong (a 2 kHz click would land at 2 Hz). | 5.2 and 6.2: all speeds, per-look listener and chain, physical style limited to s >= ~1/40 (1000 fps at 25 fps); 13 corrected. |
| R12 | minor | Radiation formula in 3.3 (and the code docstring) missed the factor `-i` of the velocity form. | Both corrected; the code always used the correct acceleration form. |
| R13 | minor | Directivity overstated as "27-34 dB side-on"; the null is narrow. | Scan table in 3.6 (-10 dB at 60 deg, -21 at 75, -27..-29 at 80-90 deg); the -20 dB floor affects listeners within ~15 deg of the null plane. |
| R14 | minor | "Brightening only 16 %" hides that the high-frequency skirt rises 4-17 dB (6-10 kHz) from 1 to 12 m/s. | 0/3.7 nuanced; the conclusion (no per-layer low-pass) stands. |
| R15 | minor | Published-data row: the 1.4e-4 figure could not be re-checked; the same authors' JASA 128(1) paper gives "order of 100 ppm" and a force exponent 6.25 % below Hertz. | Row and 3.4 updated (level-law sensitivity -0.6 dB/decade); E sensitivity of the ball modes added (15.5-24.4 kHz, conclusion holds). |
| R16 | minor | VEN inconsistencies: floor is VCT, not wood; cooler cycles 8-15 / 5-10 min; ice machine 4-7 min; dart attract jingle; door +12 dB; jukebox speakers; RT60 0.6-0.9 s. | AU-65, AU-72/73/76/78 and 12.1 fixed; 6.4 derives RT60 by Sabine from VEN 2.2 (0.8 / 0.6 / 0.5 s; 1.1 / 0.9 / 0.75 s with sealed tiles). |
| R17 | minor | UIX inconsistencies: Streamer-mode meaning, no UI sounds during a live stroke, the menu's clean 2.5 s and after-hours scene, which slider controls the player's own foley/heartbeat, focus-ducking setting missing in UIX. | 11.1, AU-110, 12.1 *AfterHours* state, 7.2 slider mapping, section 16 requests; menu prompt 6 no longer bakes in room tone. |
| R18 | minor | HF-B08: the tremor tell must scale with `g`; AU-59 gated it at P > 0.4. | AU-59: continuous level law in `g`, inaudible below g = 1.25. |
| R19 | minor | Listening notes said the gully run starts ~1.3 s after a drop; it starts 20 ms after and lasts 1.5-3 s. | 3.7 corrected; checklist item (f) compares the three modes; the Wide render is committed as `ref_divebar_break8_wide.wav`. |
| R20 | minor | "Railbird Revival" collides with the band Railbird and the Railbird country/Americana festival; voice count 1,200 vs 1,300; physical level of TTS lines undefined; no credits rule for AI voices; trailer music licence. | Band renamed "Kitchen Line Ramblers" (no match found); Q3 reconciled; vocal-effort import tags (ANSI S3.5); credits rule in 9.6; music note in 13. |
| R21 | minor | The roadmap's comparison test (recordings vs synthesis vs AI SFX) was missing. | AU-T20 blind comparison; recorded clicks as a scheduled fallback in 9.1; Q11. |

**Checked and confirmed (no change):** contact law, `alpha_T`, contact times and the self-similar pulse (selftest);
Lamb solver (k_T a 2.640); incompressible dipole limit; level law 24.3 dB/decade; E_rad/KE ~1e-4; 79 impacts / 24 gaps
< 1 ms in the dive-bar break; EQP ball data (60.325 mm / 221.1 g oversized, 163 +- 3 g worn, 167 g magnetic, nose
height 36.29 mm); VEN coin-op timings, anchors A1-A4 and gully splines; the cast against VEN 1.2. 5.8.3 names now
source-checked: `ISoundGenerator` / `FSoundGeneratorInitParams`, `USynthComponent::CreateSoundGenerator` and
`bAlwaysPlay`, `FAudioDevice::GetAudioClock`, the Windows audio config keys, convolution reverb preset fields, MetaSound
builder/editor subsystems, the recording API, Audio Insights loudness fields.

**Still open after the review:** no listening test yet (Q5, Q6); all rail, cabinet, soft-contact, rolling and
presentation values are ESTIMATE; the cushion contact time disagrees with the CLI's `k_c` (both ESTIMATE, section 16);
AU-0 must measure `L_src` and prove the voice mapping; Submix Dynamics Processor and submix output-volume property
names, SIMD helper names and Audio Gameplay Volume component names remain VERIFY; Valve's Steam Audio plugin support
for 5.8 is unknown; Gemini commercial-use terms (Q4).
