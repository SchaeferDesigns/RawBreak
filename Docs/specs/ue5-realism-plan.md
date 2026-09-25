# RAW BREAK - UE5 Realism Plan ("real footage" look for first-person pool)

| Field | Value |
|---|---|
| Document | `Docs/specs/ue5-realism-plan.md` |
| Owner | ue-realism workstream |
| Target engine | Unreal Engine 5.8 (released 2026-06-17), C++ project, DX12 / SM6, Windows x64 |
| Reference hardware | NVIDIA RTX 3070 Ti (8 GB VRAM), 2560x1440 output, DLSS |
| Status | Draft v1 (2026-09-25) - research + plan, no engine work done yet |
| Related specs | physics spec(s) for BilliardsCore (ball/cushion/pocket/cue models), rules spec (WPA) |

Legend used in this document:

- **DERIVED** - worked out by us from first principles; derivation shown.
- **ESTIMATE** - a reasonable starting value that must be measured/tuned (reference photos, footage, profiling).
- **VERIFY** - a fact about a tool/engine/licence that was true at research time but must be re-checked
  in the installed 5.8 build or the current licence text before relying on it.

---

## 0. Conventions (shared with all RAW BREAK specs)

- SI units (m, s, kg, rad). Right-handed world frame of the simulation core: origin at the centre of the table
  bed, +x along the table length toward the foot (rack) end, +y across the width, +z up. The cloth surface
  is z = 0; a resting ball centre is at z = R.
- Ball radius R = 0.028575 m (2.25 in / 57.15 mm diameter, WPA), mass m = 0.156 to 0.170 kg (WPA), I = (2/5) m R^2.
- Position r, velocity v, angular velocity w (world frame, rad/s).
- Contact-point slip velocity with the cloth: `u = v + w x (-R z_hat)`.
  Sign check: a ball rolling without slip in +x with speed V has `w = (0, V/R, 0)`:
  `w x (-R z_hat) = -R (w_y y_hat x z_hat) = -R w_y x_hat`, so `u = (V - R w_y) x_hat = 0` if and only if `w_y = V/R`. OK.
- Unreal Engine uses centimetres and a LEFT-handed frame (X forward, Y right, Z up). The conversion between
  BilliardsCore and UE is specified in section 5.6 and is a mandatory, unit-tested adapter (it is the single
  most likely source of "the balls spin the wrong way" bugs).

---

## 1. Executive summary - the decisions

1. **Rendering stack (all production features in 5.8):** Lumen GI + Lumen reflections using **hardware ray
   tracing**, reflections in **Hit Lighting** mode on High/Epic (balls and lacquer are mirror-like and must
   reflect things that are off-screen); **Virtual Shadow Maps**; **Substrate** materials; **Nanite** for the
   environment; **MegaLights** only for venues with many lamps (pool hall, arena); **DLSS 4.5** (Super Resolution
   + Ray Reconstruction) with TSR/FSR/XeSS fallbacks; **path tracer** only as an offline ground-truth reference.
2. **"Footage" look = camera model, not filters.** Two player-selectable looks:
   - **Eyes** (default): rectilinear, human-like FOV, physically derived depth of field from pupil size,
     slow human-like exposure adaptation, head motion partly stabilised (as the vestibulo-ocular reflex does).
   - **Headcam**: a head-mounted action camera: wide FOV + barrel distortion, lateral chromatic aberration,
     exposure-coupled sensor noise, full head motion (a camera has no gaze stabilisation), shutter-accurate
     motion blur.
   - **Broadcast** cameras for replays (the shot is pre-simulated, so replays are free).
   All three are data-driven presets over one physically parameterised camera; every effect has an off switch.
3. **True first-person body.** A full-body character exists in the world at real scale; the camera sits at the
   eyes. We do **not** use UE's First Person Rendering FOV/scale tricks for the hands/cue, because the bridge
   hand must touch the real cloth and the tip must touch the real cue ball. The head is hidden from the camera
   but still casts shadows and appears in ball reflections.
4. **Stroke = player input.** Mouse (or stick) motion drives the cue along its axis; tip speed at contact is
   measured from timestamped raw input (frame-rate independent) and handed to BilliardsCore. Lateral hand drift
   rotates the cue about the bridge ("steering"), exactly like a real crooked stroke.
5. **Table geometry has one source of truth** (the physics TableSpec). Render meshes for bed, cushions, pocket
   jaws and balls are generated from the same numbers the simulator uses (Blender Python / UE procedural), so
   visuals and physics can never disagree.
6. **Lighting in physical units** (lux, lumen, candela, Kelvin, EV100). The table lamp is designed to meet the
   WPA equipment spec (>= 520 lux everywhere on bed and rails) - checked by an in-editor lux probe tool.
7. **Assets:** environment from Fab/Megascans (Fab Standard License allows commercial games), Poly Haven /
   ambientCG (CC0), and our own photogrammetry (RealityScan, free under USD 1M revenue). No real brand logos.
8. **Audio:** own recordings at a real pool hall are the target; Sonniss GDC bundles (royalty-free) and CC0 as
   interim. Impacts are scheduled sample-accurately from the pre-simulated event list; loudness scales with
   Hertz-contact physics.
9. **Performance target:** RTX 3070 Ti, 1440p output, DLSS Quality, "High" preset: P95 GPU frame time <= 16.7 ms
   in the heaviest venue, VRAM <= 6.8 GB. 8 GB VRAM, not raw GPU speed, is the binding constraint.
10. **Steam:** Online Subsystem Steam (achievements, stats, leaderboards, rich presence), Steam Auto-Cloud for
    saves/replays, Steam Input glyphs, Steam Timeline markers ("footage" theme fits Steam Game Recording),
    PSO precaching, Steam Deck as a stretch goal with a dedicated Lumen Lite profile.

---

## 2. Engine baseline: what changed in UE 5.5 - 5.8 and what we use

| Feature | Status history | Use in RAW BREAK |
|---|---|---|
| Lumen (GI + reflections) | Production since 5.0. 5.6: large HWRT performance gains (Epic claims HWRT at 60 fps on current consoles, CPU bottlenecks removed; Tom Looman reports 30-50 % speedups in some scenarios). 5.8: better disocclusion, denoising, memory. | Core GI and reflections. HWRT on all PC presets from Medium up. |
| Lumen Lite | New in 5.8, **Beta**: irradiance-field final gather, about 2x faster than Lumen High, targets 60 fps on PS5 / Switch 2; reflections fall back to SSR on smooth surfaces. | Low preset, min-spec GPUs, Steam Deck profile. |
| Hardware ray tracing | Requires DX12 SM6 + RTX 20xx / RX 6000 / Arc A or newer (UE 5.8 hardware spec). Skinned meshes in RT need the GPU skin cache. | Lumen HWRT, MegaLights shadows, ray-traced reflections of the player body in the balls. |
| MegaLights | 5.5 Experimental -> 5.7 Beta -> 5.8 **Production Ready** (reduced noise, IES for volumetrics, translucency, first-person weapon support). | Pool hall (10-30 table lamps) and arena rigs. Key table lamp may stay a classic VSM light (see pitfalls). |
| Substrate | 5.5 Beta -> 5.7 **Production Ready**. 5.8 adds X-Rite AxF import and an experimental NPR/toon mode (not for us). | All materials. Clear-coat layering, fuzz, SSS, thin film, haze. |
| Path tracer | **Production Ready** since 5.5. | Offline reference renders of the look-dev scene to validate the real-time look. |
| Nanite | Production; 5.8: skinned WPO, better culling. | Environment, props, table body. Balls: Nanite or 128-segment static mesh (see 6.7). |
| Virtual Shadow Maps | Production; 5.6 receiver masks; 5.8 invalidation budget, prefiltered distant (exp.). | Default shadow method. |
| TSR | Production; 5.6/5.8 thin-geometry stability. | Fallback upscaler/AA (also used for Steam Deck). |
| First Person Rendering | Added 5.5, improved 5.6 (separate FP FOV, anti-clipping scale, FP self-shadow via screen traces; needs "Allow Static Lighting" off; no forward/mobile). | Only its **World Space Representation** primitive type (hidden-from-camera body parts that still cast shadows). NOT the FOV/scale tricks (section 5.1). |
| PSO precaching | Production (5.8: graceful failure handling). | Mandatory against shader stutter (a top Steam review complaint). |
| MetaHuman | 5.6: creator integrated in the editor, licence moved under the UE EULA, usable in any engine. 5.8: Mesh-to-MetaHuman, experimental single-camera body capture. | Player body/hands (section 5.2). |
| Game Animation Sample | Updated for 5.8 (500+ animations, motion matching). | Walk-around-table locomotion, idles. |
| DLSS | NVIDIA DLSS 4.5 plugin lists a UE 5.8 build (v8.7.x) on NVIDIA's developer page (VERIFY at download time). Super Resolution + Ray Reconstruction + DLAA work on all RTX GPUs; Frame Generation needs RTX 40+, Multi Frame Generation RTX 50+. | Primary upscaler on NVIDIA. RR on HWRT presets. |

Sources: UE 5.8 release notes, Tom Looman 5.6/5.7/5.8 performance highlights, CG Channel 5.8 overview,
UE 5.5 release notes, Epic "First Person Rendering" doc, UE hardware spec page, NVIDIA DLSS plugin archive
(see section 15).

### 2.1 Project settings baseline (DefaultEngine.ini) - VERIFY names in 5.8

```ini
[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=DefaultGraphicsRHI_DX12
; SM6 only (HWRT, Nanite, VSM, Substrate adaptive)
+D3D12TargetedShaderFormats=PCD3D_SM6

[/Script/Engine.RendererSettings]
r.AllowStaticLighting=False          ; fully dynamic lighting, frees GBuffer bits
r.DynamicGlobalIlluminationMethod=1  ; Lumen
r.ReflectionMethod=1                 ; Lumen
r.Lumen.HardwareRayTracing=True
r.RayTracing=True
r.SkinCache.CompileShaders=True      ; required for skinned meshes in ray tracing
r.Shadow.Virtual.Enable=1
r.Nanite.ProjectEnabled=True
r.GenerateMeshDistanceFields=True    ; SW/Lite fallbacks
r.Substrate=True                     ; Project Settings > Substrate; GBuffer format = Adaptive (see 6.0)
r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True  ; exposure in EV100
r.DefaultFeature.MotionBlur=True
r.AntiAliasingMethod=4               ; TSR (DLSS plugin overrides when active)
r.PSOPrecaching=1
; MegaLights: enable for project in Project Settings > Rendering (VERIFY cvar name)

[/Script/Engine.Engine]
NearClipPlane=1.0                    ; 1 cm; default 10 cm clips the cue under the chin

[/Script/Engine.LocalPlayer]
AspectRatioAxisConstraint=AspectRatio_MaintainYFOV   ; author vertical FOV, ultrawide = Hor+
```

---

## 3. What made "Bodycam" look real, and what transfers to pool

### 3.1 What Reissad Studio did (research summary)

Bodycam (Reissad Studio, two French developers who started as teenagers; Steam Early Access 2024-06-07, UE5)
is the reference for "looks like real footage". Documented techniques:

- **Camera placement and lens:** the camera sits at chest height (a body-worn police camera), with a very wide
  field of view pushed into a "fishbowl" look, plus a post-process material that distorts the image edges, a
  bodycam frame overlay placed in front of the camera, chromatic aberration. Real body cameras have 120-160
  degree diagonal lenses (Axon Body 4), which is what players subconsciously recognise.
- **Image degradation on purpose:** the developers report that degrading the image and overloading texture
  detail made the result read as more real than a clean UE5 frame ("overwhelming the brain with information").
  Sensor noise, motion blur and exposure shifts are part of it.
- **Lighting:** Lumen plus HDRI captures of real light for outdoor environments.
- **Assets:** mainly Quixel Megascans and marketplace assets (scanned = real-world surface statistics).
- **Motion:** camera sway, weapon inertia, procedural recoil, stress-influenced animations; no crosshair,
  minimal HUD.
- **Iteration on real references:** long study of real footage, optics, compression, exposure and sound.
- **Tech upkeep:** a 2025 update moved the game to UE 5.5 and converted about 90 % of textures to Virtual
  Textures for CPU/GPU performance.

### 3.2 Transfer matrix

| Bodycam technique | Transfer to RAW BREAK? | How (section) |
|---|---|---|
| Chest-mounted wide/fisheye camera | Partly. A chest cam cannot see the aiming line. We offer a **head-mounted** action-cam look (Headcam) as an option; default is Eyes. | 4.1, 4.3 |
| Barrel distortion, chromatic aberration | Headcam only, subtle. Off in Eyes (the brain removes the eye's own CA). | 4.3, 4.7 |
| Sensor noise / grain | Yes, but physically coupled to exposure (dark bar = more gain = more noise). | 4.7 |
| Image degradation / compression | Only for "shared clip" / replay export and the Broadcast look. Not in live play (aiming needs detail). | 4.7 |
| Exposure shifts | Yes - the lit table vs. dark room is a 4 EV contrast; realistic adaptation is a key realism cue. | 4.4 |
| Motion blur | Yes, shutter-based; fast balls on the break must streak. | 4.6 |
| Camera sway / weapon inertia | Replaced by body-driven head motion (walking, breathing, sway) and cue dynamics. The cue is supported by the bridge, so it has no "weapon sway". | 4.8, 5.3 |
| Procedural recoil | No recoil; the analogue is follow-through and standing up after the shot. | 5.3 |
| No crosshair / minimal HUD | Yes. No aim line by default; assists are options. Diegetic info (score board on the wall, chalk on the tip). | 5.4 |
| Scanned assets + dense clutter | Yes for environments. The table itself is procedural from the physics spec (exactness beats scans there). | 6.7, 7 |
| HDRI lighting | Limited (interiors). Used for windows/daylight in the pool hall; the main light is physical lamps. | 6.1 |
| Lumen | Yes, HWRT. | 2 |
| Virtual textures | Yes: streaming VT for environments, runtime VT for the cloth wear map. | 6.3 |
| Pixelated faces, ragdolls | No. | - |
| Spatial audio | Yes (Steam Audio or UE built-in + convolution reverb). | 8 |

### 3.3 The reference-footage method (copy this from Bodycam)

1. Shoot reference video at a real pool hall with (a) a phone held at chin-on-cue height, (b) a head-mounted
   action camera, (c) a static wide shot. Include a colour checker in some frames. (User task, section 11.)
2. Build an in-editor **reference comparison mode**: a debug split screen/wipe that plays the reference clip
   (Media Framework) next to the live render, with matched FOV and exposure. Grade the game against it.
3. Keep a path-traced reference of the look-dev scene; Lumen output must stay within tolerance of it
   (automated screenshot tests, section 13).

---

## 4. Camera model (the core of the "footage" look)

### 4.1 Presets

| Preset | Models | FOV / projection | Motion | Post |
|---|---|---|---|---|
| **Eyes** (default) | Human vision | Rectilinear, vertical FOV 50 deg (79.3 deg horizontal at 16:9) | Head translation scaled 0.3, rotation stabilised on gaze target | DoF from pupil, slow adaptation, very light grain, no CA/distortion |
| **Headcam** | Head-mounted action camera | Base horizontal 90 deg + barrel distortion (k1 = 0.12, k2 = 0.02) = 97.5 deg effective | Full head motion + small mount jitter | Fast auto-exposure, exposure-coupled noise, CA, 180-deg shutter blur, vignette |
| **Broadcast** (replays) | TV cameras over/around the table | Long lens 25-40 deg, overhead camera ~90 deg | Tripod/crane, smooth | Broadcast grade, optional score bug; optional compression artefacts |

All presets share one `URawBreakCameraModel` data asset: FOV, distortion coefficients, overscan, sensor size,
aperture law, shutter, exposure law, noise law, motion scales. Every effect has a user slider (comfort).

### 4.2 Eye / camera placement

Real pool geometry (WPA equipment spec): table bed height 0.743-0.787 m above the floor; standing eye height of
adults about 1.50-1.80 m, i.e. roughly 0.75-1.05 m above the bed.

When "down on the shot", good players have the chin close to or touching the cue (Dr. Dave: low stance, chin
near cue; "vision center" = the head position relative to the cue at which a player perceives the shot line
correctly; for many people the cue is under the nose between the eyes, for others under the dominant eye).

Camera placement when down (DERIVED from the stance description; values ESTIMATE):

```
e = P_axis(s_e) + h_c * n_up + y_vc * n_side
  P_axis(s)  = point on the cue axis at distance s behind the tip (see 5.5)
  s_e        = distance tip -> point under the eyes          0.35 .. 0.55 m   (ESTIMATE)
  h_c        = eye height above the cue axis                  0.06 .. 0.18 m   (chin on cue ~0.08 m)
  n_up       = unit vector perpendicular to the cue axis, in the vertical plane containing the axis
  y_vc       = vision-center lateral offset (user calibrated) -0.035 .. +0.035 m (half inter-pupillary distance ~0.032 m)
  n_side     = unit horizontal vector perpendicular to the cue axis
```

- Offer a **vision-center calibration** mini-game (a straight-in shot where the player aligns the cue visually;
  we store y_vc). This is a direct transfer of a real coaching technique.
- The near clip plane must be <= 1 cm (section 2.1): the cue is only 6-18 cm below the eye.

### 4.3 Field of view and projection

Formulas (standard pinhole camera):

```
tan(V/2) = tan(H/2) * (H_px / W_px)          H = horizontal FOV, V = vertical FOV, W_px x H_px output size
natural FOV of a monitor: H_nat = 2 * atan( W_screen / (2 * D_view) )
pixels per radian at image centre: p = (W_px / 2) / tan(H/2)
```

- Example: 27" 16:9 monitor (0.598 m wide) at 0.70 m -> H_nat = 46.2 deg. A geometrically "correct" FOV feels
  claustrophobic in a game; the Eyes default of V = 50 deg (H = 79.3 deg at 16:9) is a compromise (ESTIMATE,
  playtest). Slider range V = 40..70 deg.
- Author **vertical** FOV (MaintainYFOV) so 21:9 players get more side view, not a cropped top/bottom
  (V = 50 deg gives H = 95.7 deg at 21:9).
- Optional **Panini projection** (`r.Upscale.Panini.D`, `r.Upscale.Panini.S`) for players who choose V > 60 deg.
  Keep it OFF by default: it bends straight lines, and straight lines (cue, rails) are aiming references.

**Headcam barrel distortion** (Brown-Conrady radial model, the same family UE's Camera Calibration plugin uses
for its spherical lens model; we implement it ourselves as a post-process material because we need it at
runtime in gameplay). The shader maps each **output** pixel to a **source** pixel (so no inversion is needed):

```
rho_o = |(x_o, y_o)|,  coordinates in units of the base camera's horizontal half-width (x = 1 at the left/right edge)
rho_s = rho_o * (1 + k1 * rho_o^2 + k2 * rho_o^4)        k1, k2 >= 0 -> barrel look (edges compressed)
source = output * (rho_s / rho_o)
```

The scene must be rendered with overscan so that the corners have source data:

```
rho_corner = sqrt(1 + (H_px/W_px)^2)
s_over     = 1 + k1 * rho_corner^2 + k2 * rho_corner^4        (render tan-half-FOV = s_over * tan(H0/2))
H_eff      = 2 * atan( (1 + k1 + k2) * tan(H0/2) )             (content visible at the left/right edge)
```

DERIVED numbers for Headcam defaults (H0 = 90 deg, 16:9, k1 = 0.12, k2 = 0.02): s_over = 1.1926, render
FOV = 100.0 deg horizontal, effective FOV 97.5 deg. Centre resolution drops by 1/s_over; compensate with
+19 % screen percentage on Epic, accept softness below (it is "footage").

- Apply the distortion **after** the temporal upscaler (post-process material at "After Tonemapping", which runs
  at output resolution after TSR/DLSS). Applying it before the upscaler breaks motion-vector reprojection.
- Lateral chromatic aberration: either UE's built-in Scene Fringe (Headcam 0.3-0.6, ESTIMATE) or, better,
  per-channel coefficients in the same distortion pass: `k1_R = k1 * (1 + dCA)`, `k1_B = k1 * (1 - dCA)`,
  dCA = 0.01-0.03 (ESTIMATE).
- Reference FOVs: GoPro-class action cameras ~90 deg (Linear) / ~130 deg (Wide) / ~170 deg (SuperView)
  diagonal-ish; Axon Body 4 body camera 120-160 deg diagonal. Our 97.5 deg horizontal (about 107 deg
  diagonal) is deliberately narrower than a police bodycam: pool needs readable ball sizes at 2 m.

### 4.4 Exposure (physical light units)

Use physical units everywhere: lights in lumen/candela, sky in lux, exposure in EV100.

```
Luminance of a diffuse surface:   L = rho * E / pi                  [cd/m^2]   rho = albedo, E = illuminance [lux]
Reflected-light meter:            EV100 = log2( L * S / K ),  S = 100 (ISO), K = 12.5  ->  EV100 = log2(8 L)
Incident-light meter:             EV100 = log2( E * S / C ),  C = 250
```

DERIVED examples:

| Scene element | E [lux] | rho | L [cd/m^2] | EV100 |
|---|---|---|---|---|
| Cloth under a WPA-compliant lamp | 520 | 0.15 (ESTIMATE) | 24.83 | 7.63 |
| Dive-bar surroundings | 30 (ESTIMATE) | 0.20 | 1.91 | 3.93 |
| Arena venue minimum (WPA: >= 50 lux elsewhere) | 50 | 0.20 | 3.18 | 4.67 |

So the table vs. room contrast is about 3.7 EV. A real camera (and the eye) cannot hold both, and the
visible adaptation when you stand up from the shot and look at the bar is one of the strongest "real" cues.

Recommended UE settings (per preset; ESTIMATE, tune against footage):

| Setting | Eyes | Headcam |
|---|---|---|
| Metering | Auto Exposure Histogram + centre-weighted metering mask | same, stronger centre weight |
| Min / Max EV100 | 2.0 / 11.0 | 2.0 / 11.0 |
| Speed up / down [EV/s] | 1.5 / 0.7 (eye adapts to dark slowly) | 3.0 / 2.0 (camera AE is fast) |
| Exposure compensation | 0 to +0.5 | -0.3 (cameras protect highlights) |
| Local exposure (highlight/shadow contrast) | 0.8 / 0.9 | 1.0 / 1.0 (less tone compression = more "camera") |
| Bloom | Convolution bloom, low intensity (lamp glare) | Standard, slightly higher (lens veiling glare) |

### 4.5 Depth of field when aiming (Eyes preset)

The eye's blur is determined by pupil diameter A and the dioptric distance between focus and object:

```
beta = A * | 1/s - 1/d |          [rad]   angular blur-circle diameter (thin-lens, small angles)
  A = pupil diameter [m]; s = focus distance [m]; d = object distance [m]
blur in pixels = beta * p            (p = pixels per radian, 4.3)
```

DERIVED example: A = 4 mm (ESTIMATE for ~25 cd/m^2 adaptation, cf. Watson & Yellott 2012), focus on the
object ball at s = 1.5 m, cue shaft at d = 0.25 m: beta = 0.01333 rad = 0.764 deg = 20.6 px at 2560 px width
and H = 79.3 deg. So the shaft under the chin is clearly soft while the object ball is sharp - matching real
experience.

Mapping to a UE Cine camera (thin lens): angular blur ~ (f/N) * |1/s - 1/d|, so set **aperture diameter
f/N = A**:

```
f = w_sensor / (2 * tan(H/2));   N = f / A
Example: w_sensor = 36 mm, H = 79.32 deg -> f = 21.71 mm, N = 5.43 for A = 4 mm.
```

Focus logic: focus distance follows the aim target (object ball the ghost-ball line points at; else the cue
ball) with a 150-250 ms ease (eye accommodation latency, ESTIMATE). Headcam: action cameras have tiny apertures
(f ~ 2.7 mm, f/2.5 -> A ~ 1.1 mm) -> nearly everything sharp; use A = 1.1 mm there.

### 4.6 Motion blur and fast balls

- Shutter model: `MotionBlurAmount = shutter_angle / 360`. Headcam 180 deg -> 0.5; Eyes 0.3 (the eye has less
  perceived blur). Max blur 5 % of screen width.
- Break shot: cue ball 11-13 m/s (Dr. Dave: powerful break 25-30 mph). At 60 fps with a 180-deg shutter
  (1/120 s) the ball travels ~10 cm = ~1.8 ball diameters during the exposure -> strong streaks, as in real video.
- **Rotation aliasing:** a rolling ball at 3 m/s spins at w = v/R = 105 rad/s = 100 deg per 60-fps frame. The
  velocity-buffer blur approximates this rotation poorly and the numbers/stripes "wagon-wheel". Mitigation
  (DERIVED): in the ball material, blend albedo toward its average along the rotation direction by
  `alpha = clamp( |w| * t_exposure / (2*pi), 0, 1 )`; at alpha = 1 a striped ball renders as the band-averaged
  colour. Implement via a pre-filtered "rotation-smeared" texture lookup or analytic band average.
- Playback must update ball transforms **without teleport flags** so previous-frame transforms (velocity
  buffer, TSR/DLSS reprojection) are valid. Balls must be Movable.

### 4.7 Sensor artefacts

- **Noise coupled to exposure** (DERIVED): sensor gain doubles per EV of under-exposure; shot-noise relative
  std scales with sqrt(gain):
  ```
  grain = clamp( g0 * 2^( max(0, EV_ref - EV_current) / 2 ), 0, g_max )
  Headcam: g0 = 0.05, EV_ref = 8.0, g_max = 0.35;  Eyes: g0 = 0.015, g_max = 0.06     (ESTIMATE)
  ```
  Drive UE `FilmGrainIntensity` (and the shadows/midtones/highlights split: more grain in shadows) from the
  current adapted exposure every frame. Example: EV 3.9 (dark bar) -> grain 0.207 (Headcam).
- Vignette 0.2-0.4 (Headcam), 0.1 (Eyes).
- Sharpening: `r.Tonemapper.Sharpen` ~0.3-0.5 for Headcam (action cams over-sharpen); DLSS has its own.
- Compression artefacts (blocking/ringing), rolling-shutter skew, 50/60 Hz light flicker: **only** in exported
  replay clips / Broadcast look, never in live play. No strobing ever (photosensitivity).
- Halation around the lamp: a small, warm convolution bloom kernel.

### 4.8 Head and body motion (procedural layer on top of the animation)

All motion is applied to the **head bone of the full-body character** (so shadows and reflections agree with
the view), not as a camera-only shake. Only the Headcam mount jitter is camera-only.

| Motion | Frequency | Amplitude (head) | Source / status |
|---|---|---|---|
| Walking bob (vertical) | ~2.0 Hz (step frequency) | 3 cm peak-to-peak at 0.8 m/s, 4-5 cm at 1.4 m/s | Hirasaki et al. 1999 |
| Walking lateral sway | ~1.0 Hz (stride) | 2-3 cm p-p | ESTIMATE |
| Gaze-stabilising head pitch while walking | ~2 Hz, counter-phase | head keeps pointing at a fixed target | Hirasaki et al. 1999 |
| Breathing | 0.2-0.33 Hz (12-20 breaths/min) | chest 4-12 mm; head standing ~2-3 mm, down on shot ~1-2 mm | breathing literature; head share ESTIMATE |
| Postural sway standing | < 1 Hz (0.1-0.5 Hz band) | ~4-5 mm (3.8 mm eyes open) | posturography literature |
| Postural sway down on shot (bridge hand = 3rd support) | < 1 Hz | ~1 mm | ESTIMATE |
| Hand physiological tremor (grip hand) | 8-12 Hz | tip wobble 0.05-0.2 mm (invisible at rest; scale up for "pressure") | tremor literature; amplitude ESTIMATE |
| Headcam mount jitter (rotation) | 1-4 Hz Perlin | 0.05-0.15 deg | ESTIMATE |
| Stand-up / get-down transition | 0.8-1.5 s ease-in-out | - | ESTIMATE, from mocap |

- **Eyes** preset: translation scale 0.3, head rotation from bob removed (gaze stabilised on a look target).
  This mimics how humans do not perceive their own head bob.
- **Headcam** preset: full head translation and rotation (a camera has no reflex), plus mount jitter.
- A "hold breath / settle" state while in the final stroke (players exhale and hold) reduces breathing and sway
  amplitudes by 70 % over 1-2 s; a design hook for pressure (tournament nerves = tremor gain x5-10).
- Comfort: separate sliders for head bob, sway, shake, motion blur, DoF, grain, CA, distortion; "Reduced motion"
  master toggle.

### 4.9 Camera parameter table (defaults)

| Parameter | Eyes | Headcam | Unit | Range |
|---|---|---|---|---|
| Vertical FOV (authored) | 50 | 58.7 (from H0 = 90 at 16:9) | deg | 40-75 |
| Distortion k1 / k2 | 0 / 0 | 0.12 / 0.02 | - | 0-0.25 / 0-0.08 |
| Overscan s_over | 1.0 | 1.1926 | - | derived |
| Aperture diameter A | 4.0 | 1.1 | mm | 1-7 |
| Shutter angle | 108 | 180 | deg | 0-360 |
| AE speed up / down | 1.5 / 0.7 | 3.0 / 2.0 | EV/s | 0.1-10 |
| Grain g0 / g_max | 0.015 / 0.06 | 0.05 / 0.35 | - | - |
| Near clip | 1.0 | 1.0 | cm | 0.5-2 |
| Head translation scale | 0.3 | 1.0 | - | 0-1 |
| Vision-center offset y_vc | calibrated | calibrated | m | -0.035..0.035 |

---

## 5. First-person body, IK, stroke input, cue collision

### 5.1 Why a true first-person body (and not UE's First Person Rendering tricks)

UE 5.5/5.6 First Person Rendering renders "first person" primitives with their own FOV and an anti-clipping
scale toward the camera. In pool, the bridge hand rests **on the cloth**, the tip **touches the cue ball**, and
the cue must visibly pass over rails and balls. Any FOV/scale mismatch between hands/cue and the world breaks
those contacts. Therefore:

- The full body (MetaHuman or equivalent) is placed in the world at real scale; the camera is attached to a
  socket between the eyes (offset by y_vc).
- Head and neck (the parts the camera sits inside) are hidden from the camera but must still cast shadows and
  appear in ball reflections. Options: (a) First Person Rendering primitive type "World Space Representation"
  for the head mesh; (b) `bOwnerNoSee` + `bCastHiddenShadow`. VERIFY in 5.8 which option keeps the head visible
  in Lumen HWRT reflections (the doc states FP geometry itself gets no HWRT reflections; the world-space
  representation is intended for exactly this).
- Clipping is prevented physically (IK + collision), not by shrinking.

### 5.2 Asset options and licensing

| Option | Quality | Licence (VERIFY at use) | Verdict |
|---|---|---|---|
| **MetaHuman** body (in-editor creator since 5.6) | Excellent skin, hands, nails; full body for shadows/reflections | Since 5.6 covered by the UE EULA; usable in commercial games; UE royalty terms apply above USD 1M | **Recommended** base. Hands visible 100 % of the time -> customise skin detail, nails, forearm hair (cards), sleeves. |
| Fab first-person arm/hand packs | Varies; often arms-only (bad for shadows/reflections) | Fab Standard License: commercial use in your product allowed, no standalone redistribution | Use only for hand pose references or if MetaHuman fails. |
| UE Mannequin / Game Animation Sample | Good rig and locomotion; stylised mesh | Epic content, UE-only use | Use animations (retarget via IK Retargeter), not the mesh. |
| Own scan (RealityScan) | Real hands of a real player | Own | Optional later; needs rig work. |

Animation data: stance, get-down/stand-up, chalking, walking around the table, racking. Sources: Fab mocap packs
(pool-specific packs are rare - check), own markerless capture (phone video; UE 5.8 MetaHuman single-camera
body capture is experimental - VERIFY), and hand-keyed Control Rig poses. The shot itself is procedural (5.3/5.4).

### 5.3 Stance and IK

Solver: Control Rig with Full Body IK (FBIK) on top of an authored stance pose; foot IK to the floor.
WPA/most rule sets require at least one foot on the floor when shooting -> reachability constraint for the
body placement solver (rules spec owns the rule; this doc owns the IK).

IK targets for a shot (cue direction psi, elevation theta, tip offsets from the physics/cue spec):

1. **Cue pose** (5.5): axis through the contact point P on the cue ball, direction `u` (butt -> tip).
2. **Bridge hand:** bridge point B = P_axis(L_b), L_b = bridge length 0.15-0.25 m (ESTIMATE; player setting).
   Cue height at the bridge (DERIVED, centre-ball hit):
   ```
   z_P      = R + R * sin(theta)                 (contact point height for a centre hit)
   z_bridge = z_P + L_b * sin(theta)
   Example: theta = 5 deg, L_b = 0.20 m -> z_bridge = 48.5 mm
   ```
   Bridge type selection (thresholds ESTIMATE, tune with mocap):
   - open (V) bridge: z_bridge 30-70 mm; closed (loop) bridge: 40-80 mm (player preference);
   - elevated (fingertip) bridge: bridge point obstructed by a ball, or z_bridge > 80 mm;
   - rail bridge: bridge point lies over the rail/cushion;
   - mechanical bridge: bridge point beyond reach from a legal foot position.
   Each type = authored hand pose + FBIK effector on the palm, with finger contacts snapped to the cloth
   (z = 0) or the rail cap.
3. **Grip hand:** on the cue at distance L_g from the tip (1.05-1.20 m, ESTIMATE; the grip sits a little
   behind the balance point). Elbow pole vector above the cue plane ("pendulum stroke": forearm vertical at
   contact).
4. **Head:** eye target from 4.2 (chin h_c above the axis). The spine bends to satisfy it; limit spine
   flexion; if unreachable, raise h_c.
5. **Stroke motion:** the cue translates along u (input-driven, 5.4); the grip hand follows the cue (IK),
   the elbow hinges; the bridge hand stays planted. Follow-through after contact: continue the measured tip
   speed with a decaying profile (tau ~ 0.15 s, ESTIMATE); the cue must not pass through the cue ball's
   pre-contact position during the first ~1 ms of physical contact - the ball is already gone in the sim.

### 5.4 Stroke input (mouse / controller as the stroke)

**Sampling.** Enhanced Input aggregates mouse deltas per frame; at 60 fps this quantises velocity and makes it
frame-rate dependent. Capture Windows Raw Input (`WM_INPUT`) through an `IWindowsMessageHandler` registered on
the Windows application, with a QPC timestamp per report (1000 Hz mice deliver ~1 ms spacing). Feed a
lock-free ring buffer read by the game thread.

**Mapping** (hand displacement -> cue displacement along its axis):

```
x_m(t)  = mouse position along the stroke axis [m]  = counts / DPI * 0.0254
v_m     = d x_m / dt
v_c     = G(|v_m|) * v_m                          cue velocity along u
x_c(t)  = integral of v_c dt                      cue displacement (0 = tip touching the ball surface)
G(v)    = G0                                        for v <= v_knee
        = G0 + (G_max - G0) * (v - v_knee)/(v_sat - v_knee)   for v_knee < v < v_sat
        = G_max                                     for v >= v_sat
v_tip is clamped to v_tip_max.
Defaults (ESTIMATE, playtest): G0 = 2.0, v_knee = 0.5 m/s, G_max = 5.0, v_sat = 2.0 m/s, v_tip_max = 12 m/s.
```

Why nonlinear: a 13 m/s power break needs roughly 13/1.33 = 9.8 m/s cue speed (DERIVED with a simple
cue-mass model, M_cue = 0.54 kg, e = 0.75: v_ball/v_cue = M(1+e)/(M+m) = 1.33); a 1:1 mouse cannot do that, but
soft shots need fine control. Dr. Dave's speed classes for reference: soft < 0.45 m/s, slow 0.45-0.9, medium
0.9-1.9, fast 1.9-3.2, power 3.2-4.5 m/s ball speed, powerful break 11-13 m/s.

**Tip speed at contact.** When x_c crosses 0 moving forward, estimate the velocity from the last 15-25 ms of
raw samples with a **quadratic least-squares fit evaluated at the crossing time** (Savitzky-Golay derivative).
A linear fit lags by half the window under acceleration (test case T9). Use a 1-euro filter (Casiez et al.
2012) only for the *visual* cue position, never for the physics velocity.

**Steering (lateral error).** Lateral mouse motion during the forward stroke moves the grip hand sideways by
`y_g = G_lat * dx_mouse` (G_lat = 0.25 default, 0 with "straight stroke assist"). With the bridge as the pivot
(DERIVED):

```
yaw error      d_psi = y_g / L_bg                      L_bg = bridge-to-grip distance (0.6-0.9 m)
tip side shift d_a   = -d_psi * L_bt                   L_bt = bridge-to-tip distance = L_b
Example: y_g = 10 mm, L_bg = 0.8 m, L_bt = 0.2 m -> d_psi = 0.716 deg, tip shifts 2.5 mm to the other side
```

Both the new direction and the unintended side offset go to the physics (squirt/swerve/throw follow naturally).

**Practice strokes vs. the shot (open design question, section 14).** Proposal: strokes are "practice" and stop
3-5 mm short of the ball unless the commit input (hold RMB / right trigger) is held; with "Hardcore" enabled any
tip contact is a shot (real-life behaviour; the rules engine decides fouls).

**Controller:** right stick Y = stroke position (spring-centred), with the same raw-rate sampling via the
platform input layer; stick velocity mapping uses its own G curve; haptics on contact (Steam Input / DualSense
adaptive trigger for the commit).

### 5.5 Cue geometry, aiming collision, minimum elevation

Cue model (ESTIMATE typical playing cue; WPA only limits length >= 1.016 m, mass <= 708.75 g, tip <= 14 mm):
length L = 1.47 m (58 in), tip radius r_t = 6.5 mm (13 mm tip), butt radius r_b = 15.9 mm, linear taper
approximation `r(s) = r_t + (r_b - r_t) * s / L`, s = distance from the tip.

Cue axis (core frame):

```
u        = (cos(theta) cos(psi), cos(theta) sin(psi), -sin(theta))   butt -> tip direction, theta >= 0 = butt raised
P        = contact point on the cue-ball surface (from tip offsets; physics spec defines it)
X(s)     = P - s * u,   s in [0, L]
```

Clearance tests (DERIVED):

- **Ball j** at centre C_j: `s* = clamp( (C_j - P) . (-u), 0, L )`, `dist = |C_j - X(s*)|`;
  clear if `dist >= R + r(s*) + margin` (margin 1 mm; for a tapered cone this closest-axis-point test is a
  close approximation, conservative variant: use r_b).
- **Rails/cushions:** for all s where X(s) lies horizontally outside the cushion nose line, require
  `z(s) - r(s)/cos(theta) >= h_rail(s)` where h_rail is the cushion/rail-cap profile height from the table spec
  (cushion nose at 62.5-64.5 % of the ball diameter per WPA, rail cap higher - from TableSpec).
- **Environment** (walls, pillars, other tables, bar furniture): UE sweep of a capsule chain along the cue
  (`SweepMultiByChannel`) including the backswing distance (cue translated back by the max practice-stroke
  length ~0.25 m) and the player's body.

Minimum elevation: coarse sweep of theta in 0.25 deg steps from the player's desired elevation upward, then
bisection to 0.01 deg on the first clear interval. The player sees the butt rise automatically (option:
manual elevation with a red "blocked" indication).

DERIVED check case: obstacle ball directly behind the cue ball (centre distance D = 0.10 m), centre-ball hit:
the obstacle is at distance D*sin(theta) from the axis and the closest axis point is at s* = D cos(theta) - R,
so `D sin(theta) = R + r(s*)` -> theta_min = 20.79 deg (fixed-point iteration, r(s*) = 6.92 mm).

When the wall prevents a normal stroke (common in dive bars; WPA tournaments require 1.83 m of free space around
the table, bars rarely have it), offer the **short cue** (1.22 m / 48 in and 1.32 m / 52 in, as real bars
keep them) or the mechanical bridge. This is realism as gameplay.

### 5.6 BilliardsCore <-> UE coordinate conversion (mandatory adapter)

UE is left-handed, centimetres. Choose table-local UE axes X = core x, Y = core -y, Z = core z (mirror in y):

```
p_UE [cm]       = 100 * ( x, -y, z )
v_UE [cm/s]     = 100 * ( vx, -vy, vz )
w_UE [rad/s]    = ( -wx, wy, -wz )            angular velocity is a pseudovector: w' = det(M) * M * w, M = diag(1,-1,1)
q_UE (w,x,y,z)  = ( qw, -qx, qy, -qz )        orientation quaternion under the same mirror
```

DERIVED check: core rotation +90 deg about z maps x_hat -> y_hat. q = (cos45, 0, 0, sin45) -> q_UE =
(cos45, 0, 0, -sin45): rotating UE X by -90 deg about Z gives (0, -1, 0), which is the UE image of core y_hat.
Rolling ball in +x: core w = (0, V/R, 0) -> UE w = (0, V/R, 0); rotating the UE top point (0,0,1) about +Y by a
positive angle moves it toward +X. Consistent.

Use `FQuat` directly; never go through `FRotator` for ball orientations (FRotator's pitch/yaw/roll sign
conventions add another layer of confusion). The table actor's world transform is applied after this local
conversion.

### 5.7 Playback of the simulated shot

- The core returns piecewise-polynomial trajectories between events. Rendering evaluates position at the
  exact frame time (no interpolation error).
- **Orientation** is not a polynomial: integrate `q(t + dt) = exp(0.5 * w(t) * dt) (x) q(t)` (world-frame w,
  left multiplication) with sub-steps <= 1 ms from the orientation stored at the last event, renormalise.
  The core should store orientation at each event so drift never accumulates across events.
- Audio and VFX are scheduled from the event list (section 8.3), not from rendering.
- Chaos physics never touches the balls. Chaos is only used for environment sweeps and props.

---

## 6. Rendering the table, balls and props

### 6.0 Material system choice

- **Substrate, Adaptive GBuffer** for the PC project (richer layering: clear coat over wood, fuzz, haze;
  `r.Substrate.BytesPerPixel` default 80). The GBuffer format is project-wide (shader recompile), so decide
  early. If profiling or a Steam Deck target demands it, fall back to Blendable GBuffer (note: haziness is not
  supported in Blendable).
- Base-colour/albedo sanity: dielectric albedo 0.02 (charcoal) to 0.85-0.9 (snow); F0 of common dielectrics
  0.02-0.06; legacy UE mapping F0 = 0.08 * Specular (Karis 2013).

### 6.1 Lighting the table (WPA-correct, physical units)

WPA Recommended Equipment Specifications (tournament equipment):

- Bed and rails >= **520 lux** at every point; the centre should not get noticeably more than rails/corners.
- Fixture >= 1.016 m above the bed if it can be moved aside, >= 1.65 m if fixed.
- "Blinding" starts at 5000 lux direct view; the rest of the venue >= 50 lux.

Illuminance from a small downward source (DERIVED):

```
E(p) = sum_i  I_i(theta_i) * h_i / d_i^3          d_i = |p - light_i|, h_i = light height above the bed, cos(theta_i) = h_i/d_i
Lambertian downward emitter:  I0 = Phi / pi         e.g. a 1600 lm LED -> I0 = 509.3 cd -> 509 lux at 1 m straight below
```

DERIVED example (9-ft table, three bulbs I = 600 cd at h = 1.0 m, x = -0.85 / 0 / +0.85 m): E(centre) =
1130.8 lux, E(corner of the bed at (1.27, 0.635)) = 458.7 lux -> fails 520 lux at the corners and has a 2.5:1
centre/corner ratio. Real tables solve this with wider shades, diffusers and more light sources - so should we.
**Build an editor "lux probe" utility** that evaluates E on a 5 cm grid over bed and rails from the placed lights
(analytic for point/spot/rect lights with IES) and prints min/max/uniformity; the look-dev checklist requires
min >= 520 lux for tournament tables (bar tables may be deliberately worse - that is character).

Light setup per lamp:

- Rect lights (shade opening size, e.g. 0.30 x 0.30 m) or spot lights with a real **source radius** (never 0:
  pinpoint highlights on glossy balls look CG) and a barn-door/IES profile for the shade cut-off.
- Physical intensity units; colour temperature per venue (6.x / 7).
- Emissive lamp diffuser meshes: hide them from Lumen reflections/GI (or zero their emissive in RT) so the ball
  highlight comes from the analytic light only - otherwise every ball shows a double highlight.
- Key table lamps: classic lights with VSM (noise-free highlights on glossy spheres). MegaLights for the many
  secondary lamps of a hall; verify highlight noise on balls before enabling MegaLights on the key lamp.
- Light haze: volumetric fog with low density so the lamp cone reads (dive bar, arena); cost ~0.5-1 ms.

### 6.2 Balls (phenolic resin)

Physical facts: cast phenolic resin (WPA), 57.15 mm, 156-170 g (WPA); Aramith states 100 % high-density
phenolic, density 1.69-1.87 g/cm^3, hardness > 73 HRH, vitrified surface with average roughness Ra = 0.03 um,
high-gloss finish. (170 g in a 57.15 mm sphere = 1739 kg/m^3, consistent.) The WPA asks for unpolished,
unwaxed balls in play. Colours/numbers per WPA: 1 yellow, 2 blue, 3 red, 4 purple, 5 orange, 6 green,
7 maroon, 8 black; 9-15 white with a centred band of the same colours; each number printed twice on opposite
sides (one upside down), black on a white circle; 6 and 9 underscored.

Shading model (Substrate Slab):

| Input | Value | Notes |
|---|---|---|
| F0 | 0.049 (n = 1.57, ESTIMATE within 1.49-1.60 for phenolic resins) | DERIVED `F0 = ((n-1)/(n+1))^2`; legacy Specular = 0.615 |
| Roughness (primary) | 0.03-0.05 new, 0.08-0.15 worn bar balls | Ra 0.03 um -> Rq ~ 0.0375 um < lambda/8 ~ 69 nm: optically smooth (Rayleigh criterion) |
| Second roughness / weight (haze) | 0.15-0.25 / 0.1-0.3 | DERIVED motivation: total integrated scatter (Bennett & Porteus) (4 pi Rq / lambda)^2 = 0.73 at 550 nm means a large share of the specular energy is scattered near-specularly -> a soft haze lobe around the sharp highlight. Tune against photos. |
| Diffuse albedo | linear, ESTIMATE: cue ball 0.75-0.80 (off-white), yellow (0.75, 0.50, 0.02), red (0.50, 0.03, 0.03), black 0.02-0.03 | Measure with a colour checker (section 11). |
| SSS MFP | 0.5-2 mm (ESTIMATE; resin is slightly translucent, softens the terminator on light balls) | Epic preset only if it costs > 0.2 ms |
| Normal | none (geometric smoothness), micro-scratch normal only on worn sets | - |

- **Decal layout generated analytically in the material** from ball-local coordinates: number circles at the
  +/-X local poles, stripe band `|z_local| < sin(half_band_angle)`; glyphs sampled from an atlas via gnomonic
  projection inside the circle. No UV seams, no pole pinching, resolution-independent close-ups. Band and
  circle angular sizes: measure from a reference set (ESTIMATE band half-angle 35-40 deg, circle 40 deg).
- **Spin readability:** offer a "dotted" cue ball (generic red dots; do not copy trademarked designs) - lets
  players see draw/follow/side like on TV.
- **Chalk and dirt accumulation (gameplay-driven):** each ball owns a small decal render target (e.g. 256^2
  octahedral map). Cue-tip contact stamps a blue chalk mark at the contact point (size from tip radius and
  force); ball-ball collisions transfer part of the mark; the player's "clean the cue ball" action (ball in
  hand) wipes it. Fingerprint smudges raise roughness. Driven by the physics event list.
- Reflections: Lumen HWRT with Hit Lighting reflections: each ball mirrors the lamp, the room, the other
  balls and the player. Screen-space reflections cannot work here (the room behind the camera is off-screen).

### 6.3 Cloth (worsted wool)

Facts: WPA: non-directional, nap-free worsted cloth, 80-85 % worsted wool + 15-20 % nylon (100 % worsted
preferred), colours yellow-green, blue-green or electric blue. Retailers list Simonis 860 as 90 % wool / 10 %
nylon, ~410 g/m^2 (the WPA figures and this product differ - irrelevant for rendering, relevant for the
physics spec's cloth parameters).

Shading (Substrate Slab + detail):

| Input | Value (ESTIMATE, match photos) |
|---|---|
| Diffuse albedo | dark and saturated, 0.10-0.20 luminance; hue per cloth colour |
| Roughness | 0.75-0.90 |
| Fuzz amount / roughness / colour | 0.2-0.4 / 0.5 / albedo desaturated and lightened ~1.5x |
| Normal | fine weave, period ~1 mm, plus low-frequency tension wrinkles only near cushions/pockets |
| Anisotropy | slight (0.1-0.2) along the weave |

- The player looks at the cloth at **grazing angles** (5-10 deg when down on the shot): sheen/fuzz dominates
  and the far cloth looks lighter and hazier. This is the single most important look-dev view - author the
  cloth from the chin-on-cue camera, not from above.
- Anti-aliasing at grazing angles: 16x anisotropic filtering, normal-to-roughness compositing on the normal
  map mips (texture "Composite Texture" setting), avoid high-frequency normal detail that DLSS turns into moire.
- **Wear / chalk map:** a runtime virtual texture (or 2 k render target) over the bed storing chalk dust,
  "ball burns" (whitish marks at the break spot and head string area from cue-ball skids and jump landings),
  and lint. Updated from simulation events (sliding at high speed, jump landings, miscues); persisted in the
  save for a player's "home table". Pre-authored initial wear per venue (dive bar heavy, arena fresh).
- Spot stickers (head/foot spot) and faint rack outline as decals.

### 6.4 Wood, lacquer, metal, leather, inlays, the cue, chalk

| Surface | Substrate setup | Parameters (ESTIMATE unless noted) |
|---|---|---|
| Lacquered rail wood | Vertical layer: clear-coat slab over wood slab | coat F0 0.04 (n ~ 1.5), roughness 0.05-0.10 (gloss) / 0.2 (satin), thickness 50-100 um with slight amber absorption for aged lacquer; wood albedo from scans, roughness 0.4-0.6, anisotropy along grain for figured woods |
| Bar-table laminate/formica rails | single slab | roughness 0.3-0.5, cigarette burns / scratches decals (dive bar) |
| Brass pocket irons / corner castings | metal slab | F0 linear (0.910, 0.778, 0.423) (Real-Time Rendering, physicallybased.info), roughness 0.2-0.4, patina/fingerprint roughness mask |
| Leather pocket (drop pockets) | slab + fuzz 0.1 | albedo 0.03-0.08 dark brown, roughness 0.5-0.7, worn edges lighter |
| Pocket net / ball-return gutter (coin-op) | masked slab / metal | - |
| Diamonds/sights (mother of pearl) | slab + Substrate thin film (iridescence) | thin-film thickness 300-600 nm, low roughness; WPA: 18 sights, round 11.1-12.7 mm or diamond-shaped |
| Cushion rubber (visible in pocket jaws) | slab | albedo 0.02-0.04, roughness 0.6 |
| Cue shaft (maple) | clear coat over wood | coat roughness 0.15-0.25 (shafts are satin), chalk-blue smudges near the tip |
| Ferrule | slab | white/ivory, roughness 0.2, slight SSS |
| Leather tip | slab + chalk layer | chalk coverage map decreasing per shot (visible diegetic hint to re-chalk) |
| Chalk cube | slab, high roughness, fuzz | the dent deepens with use (vertex-offset from a usage counter) |

### 6.5 Contact shadows and ball occlusion

- Primary: VSM (with source-radius penumbra) or MegaLights ray-traced shadows; Lumen screen/short-range AO.
- Add light **Contact Shadow Length** ~0.02-0.05 on the key lamp for the ball-cloth contact.
- **Analytic ball occlusion on the cloth** (cheap, robust, DERIVED from the solid angle of a sphere, cf. Quilez):
  a sphere of radius R resting on the plane, seen from a cloth point at horizontal distance rho from the contact
  point, subtends sin^2(alpha) = R^2/d^2 with d^2 = rho^2 + R^2 and elevation cosine R/d; since the sphere never
  goes below the horizon, the cosine-weighted occlusion is exactly
  ```
  Occ(rho) = R^3 / (rho^2 + R^2)^(3/2)       Occ(0) = 1,  Occ(R) = 0.3536,  Occ(2R) = 0.0894
  ```
  Multiply ambient/indirect lighting on the cloth by `prod_i (1 - Occ_i)` for the 16 balls (positions via a
  material parameter collection or a small structured buffer). Near-free and removes the "floating ball" look.

### 6.6 Reflections

- High/Epic: Lumen HWRT reflections, Ray Lighting Mode = Hit Lighting for Reflections (PPV), so reflected
  surfaces are fully lit (lamp, player, room).
- Medium: HWRT with surface-cache lighting. Low/Deck: Lumen Lite -> SSR on smooth surfaces (balls lose
  off-screen reflections; acceptable at that tier).
- Everything the balls can reflect must exist: the room **behind the player**, the player's full body (head
  included, 5.1), the lamp housing. Look-dev check: orbit the camera around a ball at 5 cm.
- The ray-tracing scene must stay lean: HWRT guidance is < 100 k instances after culling (Epic Lumen
  performance guide); merge clutter, use ray-tracing culling (`r.RayTracing.Culling`).

### 6.7 Geometry accuracy

- **Single source of truth:** table dimensions (playing surface 2.54 x 1.27 m for 9 ft, 2.3368 x 1.1684 m for
  8 ft, pocket mouths, cut angles 142 deg corner / 104 deg side, cushion nose height, 18 sights at 31.75 cm
  spacing on a 9-ft) come from the physics TableSpec file. A Blender Python generator (or UE procedural mesh
  builder) produces the render meshes from it; changing a number regenerates both.
- **Ball tessellation** (DERIVED): silhouette sagitta error for N segments on a great circle is
  `e = R (1 - cos(pi/N))`: N = 32 -> 0.138 mm, N = 64 -> 0.034 mm. At 5 cm from the eye (Eyes preset, 1440p)
  one pixel is 0.032 mm, so N >= 64 around the equator (or Nanite) for close-ups. Use a 128-segment sphere
  (~32 k triangles) for LOD0 or Nanite; shading normals analytic from the ball centre.

---

## 7. Environments

### 7.1 Venue briefs

| Venue | Table | Light (ESTIMATE) | Character / key props | Tech notes |
|---|---|---|---|---|
| **Dive bar** | 7-ft coin-op bar box, ball return, possibly a different cue ball (VERIFY with physics/rules owners) | 1-2 lamp shades, 2700 K; neon signs (generic, no real brands); room 10-40 lux | Low ceiling, walls close to the table (short cues), sticky wood/tile floor, jukebox, dartboard, bar stools, TV | Dense clutter = Bodycam "information overload"; volumetric haze; neon = emissive + small rect lights |
| **Basement / cellar** | 8-ft home table (older, worn cloth, slightly unlevel optional) | Fluorescent tube (4000 K, slight green tint) or a single lamp | Wood paneling, concrete, boiler, storage shelves, small window light well | Daylight through a light well (HDRI + directional); steady light, no flicker in play |
| **Pool hall** | Rows of 9-ft tables | 3-bulb or LED lamps per table (3000-4000 K), 10-30 lamps | Carpet, cue racks, counter, scoreboards, other players (ambient NPCs later) | MegaLights; instancing; ray-tracing culling |
| **Tournament arena** | 9-ft TV table, fresh cloth | Uniform LED rig 5000-5600 K, >= 520 lux (typically higher; ESTIMATE 800-1500 lux), venue >= 50 lux, dark stands | Stands, broadcast cameras, referee, scoreboard, sponsor boards (fictional) | Broadcast replays; strongest exposure contrast |

### 7.2 Asset sourcing and licences (VERIFY each licence at download; keep a licence ledger)

| Source | What | Licence summary |
|---|---|---|
| **Fab / Quixel Megascans** | Surfaces (wood floors, concrete, brick, carpet), 3D props | Fab Standard License: commercial use inside your project, modification allowed, distribution as part of the game allowed, no standalone resale/redistribution. Megascans were free until end of 2024 (anything claimed then stays usable); since 2025 most are paid (from ~USD 0.99 per asset) with a free starter selection (1,500+ assets). |
| **Poly Haven / ambientCG** | HDRIs, textures, some models | CC0 (public domain) |
| **Own photogrammetry** | Unique props (a real bar's stools, chalk, cue racks), materials | RealityScan 2.0 desktop (ex-RealityCapture) free under USD 1M revenue; RealityScan mobile app. Scan only objects you own or have permission for; no logos. |
| **Fab marketplace packs** | Bar/pub interiors, furniture | Standard License; check for embedded real brands |
| **MetaHuman** | Player body | UE EULA (section 5.2) |

Trademark rule: no real brands (ball, cloth, table, cue, beer, spirits) in textures, signs or names unless
licensed. Scanned props must be de-branded.

Photogrammetry quick workflow: 60-200 photos, overcast/diffuse light, polariser if available, colour checker in
the first frame -> RealityScan -> decimate/retopologise in Blender -> bake normals/AO -> delight albedo ->
import (Nanite).

### 7.3 Asset ledger

`Docs/licenses/asset-ledger.csv` (to be created with the first asset): asset, source URL, licence, date,
account used, modified yes/no, used in maps.

---

## 8. Audio realism

### 8.1 Sound events (all driven by the simulation event list)

| Event | Physics driver | Notes |
|---|---|---|
| Cue tip -> cue ball | tip speed, offset, miscue flag | tip contact ~1 ms (Dr. Dave); miscue = sharper, higher click + slip squeak |
| Ball -> ball | normal relative speed v_n | contact ~0.3 ms (Dr. Dave); dominant "click" |
| Ball -> cushion | normal speed, cushion segment | cloth-covered rubber thud + wood rail resonance |
| Ball -> rail cap / jaw facing | speed | harder, woodier (jaw facings are hard rubber) |
| Pocket entry / rattle | jaw hits sequence | multiple quick hits; drop into leather pocket vs. gutter (coin-op) rolling |
| Ball rolling on cloth | speed, state (sliding/rolling) | continuous low rumble; sliding adds hiss |
| Ball landing (jump) | vertical speed | slate thump |
| Racking, rack lift, chalking, cue on rail, footsteps, coin mechanism (coin-op) | gameplay | foley |
| Room ambience | venue | room tone, HVAC, fridge hum, distant TV, crowd walla (unintelligible) |

### 8.2 Level and timbre scaling (DERIVED)

Koss & Alfredson (1973) showed that the sound of two colliding spheres is well described by Hertzian impact
plus rigid-body ("acceleration") radiation. With Hertz contact, peak force (hence acceleration and radiated
pressure) scales as v^(6/5) and contact time as v^(-1/5):

```
level(v)  = L_ref + 24 * log10(v / v_ref)       [dB]     (20 log10 of v^1.2)
cutoff(v) = f_ref * (v / v_ref)^(1/5)                    (shorter contact -> brighter)
Example: doubling v_n -> +7.22 dB; 1 -> 10 m/s -> cutoff x 1.585
```

Implementation: velocity-layered samples (>= 5 layers x 6-8 round robins) cross-faded by v_n, then fine gain
per the formula and a low-pass whose cutoff follows cutoff(v). A sphere's own lowest vibration modes are in the
high-kHz range for this size and material, so pitch barely changes with speed - timbre does. Tune by ear against
recordings; the formulas give the right *slope*.

### 8.3 Timing

A break produces dozens of impacts within ~100 ms. Game-thread `PlaySound` calls are quantised to audio
buffer boundaries. Because the shot is pre-simulated, schedule every impact with **sample accuracy** using a
Quartz clock (or a MetaSound that receives the event times as an array and triggers voices with sample
offsets). Voice budget: >= 48 voices for ball impacts, concurrency groups per ball pair, virtualisation for
distant tables.

### 8.4 Spatialisation and rooms

- HRTF binaural for headphones: **Steam Audio** (open source, Apache 2.0, UE plugin; HRTF, occlusion,
  reflections, pathing) or UE's built-in spatialisation. Recommendation: start built-in + convolution reverb,
  evaluate Steam Audio for small rooms (dive bar/basement early reflections are very characteristic).
- **Convolution reverb** (UE Synthesis submix effect) with impulse responses recorded in the real venues
  (sine sweep or balloon pop) - strongest single audio realism win.

### 8.5 Sourcing and licences

| Option | Licence | Verdict |
|---|---|---|
| **Own recordings** in a real pool hall | own (get the owner's written permission; do not record identifiable voices of guests - personal data under GDPR) | **Target** for all ball/cue/table sounds and IRs |
| Sonniss #GameAudioGDC bundles | royalty-free, commercial use, no attribution; no standalone resale; no AI/ML training use | Interim + ambience/foley |
| Freesound | per file: CC0 ok, CC-BY ok with attribution, **CC-BY-NC not allowed** (commercial) | Selective, log in ledger |
| Commercial SFX libraries (e.g. Soundsnap, A Sound Effect) | per-library EULA | If own recordings fail |
| Music | only licensed/royalty-free or original; no radio music in bars | - |

Recording plan (user + helper, ~4 h in a quiet pool hall; ESTIMATE):
32-bit float recorder (e.g. Zoom F3/F6 class), a pair of small-diaphragm condensers (player-ear position,
~0.5-1 m), one close mic (20-30 cm), a contact/piezo mic on the rail, 96 kHz. Takes: ball-ball at ~10 speeds x
10 each; cue strikes (soft to break, miscues); cushion hits per speed; pocket drops (corner/side, leather,
gutter); rattles; rolling passes; jump landings; racking, rack lift, chalking; footsteps on each floor type;
room tone 2 min per venue; sweep IR per venue position.

---

## 9. Performance budget (RTX 3070 Ti, 1440p, DLSS)

### 9.1 Targets

- Output 2560x1440, DLSS Super Resolution Quality (internal ~1707x960) on "High".
- Frame time: **P95 GPU <= 16.7 ms** (60 fps floor) in the heaviest venue (pool hall); 90+ fps in bar/basement.
  NVIDIA Reflex on. Frame Generation is **not available** on the 3070 Ti (DLSS FG needs RTX 40+).
- VRAM: **<= 6.8 GB** total process usage on the 8 GB card (leave headroom for OS/driver/overlay).
- CPU (6-core Zen 3 / Alder Lake class): game thread <= 6 ms, render thread <= 6 ms, RHI <= 4 ms.
- Shot simulation async on a worker thread: break (hundreds of events) < 2 ms; AI planning across worker
  threads within a 0.5-2 s "thinking" window.

### 9.2 GPU budget (High, 60 fps, ESTIMATE - replace with Unreal Insights / ProfileGPU measurements)

| Pass | ms |
|---|---|
| Nanite visibility + base pass + prepass | 2.0 |
| Shadows (VSM, 1-4 key lights) / MegaLights (hall) | 1.5 / 2.5 |
| Lumen GI (HWRT, surface cache) | 3.0 |
| Lumen reflections (HWRT, hit lighting) | 2.0 |
| Translucency + volumetric fog | 0.8 |
| Hands/skin SSS, hair cards | 0.3 |
| Post (DoF, motion blur, bloom, distortion, grain) | 1.2 |
| DLSS SR (1440p output; transformer model is heavier on Ampere - measure) | 1.0 |
| UI | 0.3 |
| **Sum / headroom** | **12.1 / 4.6** |

Epic's own Lumen guidance for comparison: 4 ms (60 fps) / 8 ms (30 fps) at 1080p internal on consoles.

### 9.3 VRAM budget (High)

| Pool | MB |
|---|---|
| Texture streaming pool (`r.Streaming.PoolSize`) | 2500 |
| Nanite streaming pool | 512 |
| Ray tracing (BLAS/TLAS + skin cache) | 500-700 |
| Lumen (surface cache, radiance cache, screen probes) | 300-500 |
| VSM physical pages (4096 x 64 KB) | 256 |
| Render targets @ 960p internal + 1440p output (Substrate ~80 B/px x 1.64 Mpx = 131 MB) | 600-900 |
| Meshes, audio, misc | 800 |
| **Total** | **~5.5-6.2 GB** |

Rule: Megascans at 2k (4k only for hero surfaces), BC7/BC5 compression, virtual textures for large sets.

### 9.4 Scalability matrix (Steam settings menu)

| Setting | Low / Deck | Medium | High (3070 Ti default) | Epic | Cinematic |
|---|---|---|---|---|---|
| GI | Lumen Lite (Beta) | Lumen Lite | Lumen HWRT | Lumen HWRT (higher quality) | Lumen HWRT max |
| Reflections | SSR (via Lumen Lite) | Lumen HWRT, surface cache | HWRT, Hit Lighting | HWRT, Hit Lighting + RR | same, full-res |
| Shadows | VSM lower res (`ResolutionLodBias +1`) | VSM | VSM (+MegaLights in hall) | VSM + MegaLights | same + RT shadows on key lamp |
| Upscaler | FSR/TSR 50-60 % | DLSS/FSR/XeSS Balanced | DLSS Quality | DLSS Quality / DLAA | DLAA |
| Volumetric fog | off | low | on | on | high |
| Ball SSS / haze lobe | off | haze only | on | on | on |
| Texture pool | 1000 MB | 1500 MB | 2500 MB | 3500 MB (>= 12 GB GPUs) | 4500 MB |
| Frame generation | FSR FG optional | optional | n/a on 30-series | DLSS FG (RTX 40+) optional | off |

Plus: FOV, all camera-effect toggles (section 4), frame cap, V-Sync, HDR output (optional, lamp highlights
benefit), Reflex, colour-blind aids (ball numbers overlay/high contrast), subtitles for commentary.

Steam Deck profile: 1280x800 output, Lumen Lite, TSR/FSR from ~50 %, 30-40 fps cap, texture pool 1000 MB,
volumetrics off, Headcam distortion off (cost of overscan).

---

## 10. Steam release checklist (items that affect the tech)

- [ ] Steamworks partner account + Steam Direct fee (USD 100 per app, recoupable) -> App ID. Develop with
      AppID 480 (Spacewar) until then.
- [ ] Engine: Online Subsystem Steam plugin enabled (`DefaultPlatformService=Steam`, `bEnabled=true`,
      `SteamDevAppId`); Steamworks SDK version as pinned by the 5.8 engine (`Engine/Source/ThirdParty/Steamworks`,
      VERIFY; update `SteamVersionNumber` in `Steamworks.build.cs` only if upgrading). `steam_appid.txt` for
      development builds only.
- [ ] Achievements + stats defined in the Steamworks backend; unlocked via the OSS Achievements interface
      (examples: Break and Run, Golden Break, first masse, 100 pocketed). Leaderboards (drill scores).
- [ ] Steam Cloud via **Auto-Cloud** (no code): `%LOCALAPPDATA%/RawBreak/Saved/SaveGames/*.sav` + replay files.
      Replays are stored as shot inputs + table state (tiny, deterministic re-simulation) - ideal for cloud.
- [ ] Steam Input: full controller support, correct glyphs (Common UI / CommonInput platform icons), on-screen
      keyboard for text entry.
- [ ] Rich Presence ("9-ball at the Dive Bar, 5-3").
- [ ] **Steam Timeline API** (`ISteamTimeline`): add markers for golden breaks, runs, fouls - fits the footage
      theme and Steam Game Recording.
- [ ] PSO precaching + bundled PSO cache from playtests; first-launch shader warm-up screen; no hitch > 50 ms
      after warm-up (acceptance test).
- [ ] Crash reporting (UE Crash Reporter to own endpoint or a service), symbols archived per build; GDPR-
      compliant privacy policy if any telemetry.
- [ ] SteamPipe depots (Windows x64), build scripts (`steamcmd` + app_build VDF), beta branch for testers.
- [ ] Store: "Coming Soon" page at least 2 weeks before release; build and store review take several business
      days; content survey; system requirements (min: SM6 GPU, 8 GB VRAM recommended for High).
- [ ] Localisation (German + English at launch).
- [ ] Steam Deck (stretch goal) - compatibility review criteria: full controller support with correct glyphs,
      1280x800/720 support, text >= 9 px at 1280x800 (12 px recommended), default settings playable at 30 fps at
      800p, no launcher that needs a mouse, Proton-compatible middleware (no incompatible anti-cheat).
- [ ] Unreal Engine royalty: 5 % of gross above USD 1M lifetime per title, quarterly reporting (Epic EULA;
      reduced rate programme for simultaneous Epic Games Store launch - VERIFY current terms).
- [ ] Online multiplayer (if/when): host-authoritative shot resolution (shooter sends cue parameters, host
      simulates, broadcasts the event list); no cross-machine floating-point determinism required.

---

## 11. Work plan and user actions ("tell me when to do it")

### 11.1 What Claude can do before the engine is installed

1. Finish specs (this one, physics, rules), BilliardsCore + CMake tests.
2. TableSpec file + Blender Python generator for table/cushion/pocket/ball meshes (render = physics geometry).
3. Ball decal generator (analytic numbers/stripes), cloth/wood material recipes, camera-model math module
   (FOV, distortion, DoF, exposure, grain, lux probe) with unit tests (section 13) - engine-agnostic C++.

### 11.2 User action checklist

| When | Action | Why | Notes |
|---|---|---|---|
| **Now** | Finish installing UE 5.8 (Launcher build is fine) | everything | Also tick "Editor symbols for debugging" if disk allows (~60 GB) |
| **Now** | Install Visual Studio (UE 5.8 docs recommend **VS 2026** for general development) with "Game development with C++" workload | C++ builds | Forum reports of VS 2026 18.8.x issues with 5.8 - if a build fails on toolchain, tell me the exact error; check `Engine/Config/Windows/Windows_SDK.json` for the preferred MSVC |
| **Now** | Blender: current LTS (4.5 LTS) or newer | mesh generator scripts | Tell me the version |
| Phase B (project created) | Log into Fab in the editor; add **Game Animation Sample** (free) and a handful of Megascans surfaces I will list | locomotion, look-dev | Requires your Epic account - I cannot do this |
| Phase B | Download **NVIDIA DLSS 4.5 plugin for UE 5.8** from developer.nvidia.com (free NVIDIA developer account, licence acceptance) | upscaling/RR | Put the plugin folder into `RawBreak/Plugins/` |
| Phase C | Create a MetaHuman in the editor (MetaHuman Creator) | player body/hands | Epic account; pick skin tone/hands you like |
| Phase C | Record 3-5 min phone video of yourself (or a friend) doing pool stances/strokes from the side + a head-mounted/phone chin-height POV clip | IK tuning + reference footage | Section 3.3 |
| Phase D | Visit a pool hall/bar (with permission): reference video, photos with a colour checker (e.g. Calibrite ColorChecker Classic), audio recording session, IRs | colours, sounds, look | Section 8.5 |
| Phase D | Steamworks account + USD 100 app fee; download Steamworks SDK if the engine's pinned version needs updating | Steam features | Only when a store page is planned |
| Optional | Steam Audio UE plugin (GitHub releases) | spatial audio | Apache 2.0 |
| Optional | Sonniss GDC bundles | interim SFX | Free download |
| Optional | RealityScan (phone/desktop) | own scans | Free < USD 1M revenue |

### 11.3 Tooling that would speed things up (ask before installing)

- Enable UE's **Python Editor Script Plugin** + **Remote Execution** so editor work (imports, material setup,
  level assembly, screenshots) can be scripted and verified by Claude.
- Optional: a Blender MCP server for interactive asset work (Python scripts via the command line also work).

---

## 12. Implementation notes & pitfalls

1. **Do not use First Person Rendering FOV/scale** for hands or cue (breaks world contact). Use world-space body.
2. **Near clip plane** default 10 cm clips the cue under the chin -> 1 cm (reversed-Z keeps depth precision).
3. **Handedness/units** (5.6): angular velocity is a pseudovector (sign flips on x and z, not only y). Unit-test
   the adapter; never convert through FRotator.
4. **Motion vectors:** playback must not teleport balls; otherwise no motion blur and DLSS/TSR ghosting.
5. **Rotating balls alias** at >~50 rad/s; use the material rotation-smear (4.6).
6. **Distortion after upscaling** (post-process "After Tonemapping"); render with overscan or corners go black.
7. **Author vertical FOV** (MaintainYFOV); UE's default maintains horizontal FOV and crops ultrawide vertically.
8. **Auto-exposure pumping** when the lamp enters the frame: centre-weighted metering mask, histogram high-
   percent < 100 %, capped speeds.
9. **Double highlights** from emissive lamp meshes + analytic lights: hide emissive diffusers from RT/Lumen
   reflections.
10. **Point/spot lights need a source radius/size** equal to the real emitter; zero-size lights give CG
    pinpoint highlights on balls.
11. **MegaLights noise on glossy spheres:** test before using it on the key table lamp; per-light opt-out.
12. **Screen-space reflections cannot show what is behind the player** - balls need HWRT reflections; build the
    room behind the camera to full quality.
13. **Head hidden but reflected:** verify 5.8 behaviour of "World Space Representation" vs OwnerNoSee in Lumen
    HWRT reflections; skinned meshes in RT need the GPU skin cache.
14. **Substrate GBuffer format is project-wide** (recompile); decide early; Blendable loses haziness.
15. **Grazing-angle cloth** is the hardest view: anisotropic filtering, roughness-from-normal-variance,
    look-dev from the chin-on-cue camera.
16. **Geometry drift:** never hand-model cushions/pockets; regenerate from TableSpec.
17. **8 GB VRAM** is the real limit: 2k textures by default, watch RT memory and the streaming pool.
18. **Shader stutter** kills Steam reviews: PSO precaching + bundled cache + warm-up.
19. **Stroke velocity must be frame-rate independent:** raw input with timestamps, quadratic fit at the
    crossing, not per-frame deltas; do not let frame generation or smoothing filters touch physics input.
20. **Audio timing:** sample-accurate scheduling (Quartz/MetaSounds); enough voices for the break.
21. **Legal:** no real brand logos; licence ledger for every asset; no NC-licensed audio; GDPR for recordings
    of people; Epic "UE-only" content stays inside UE.
22. **Comfort/accessibility:** every camera effect has an off switch; no flicker/strobe; colour-blind aids
    (orange/red/maroon balls are hard to distinguish).
23. **Chaos never simulates balls**; only environment sweeps/props. The simulation result is the truth.
24. **Units in UE materials:** world positions arrive in cm; the analytic ball-AO and decal math must use the
    same unit as R.
25. **Short cues/mechanical bridge** must exist before tight venues ship, or players get stuck against walls.

---

## 13. Test cases (numerically checkable)

Math tests belong to an engine-agnostic `RenderMath` test target (same CMake/unit-test setup as BilliardsCore);
engine tests use UE Automation (functional tests, screenshot comparison, Gauntlet performance runs).

| ID | Function | Input | Expected | Tolerance |
|---|---|---|---|---|
| T1 | F0 from IOR | n = 1.57 | F0 = 0.04919; legacy Specular = 0.6149 | 1e-4 |
| T2 | Horizontal -> vertical FOV | H = 80 deg, 16:9 | V = 50.534 deg | 0.01 deg |
| T3 | Vertical -> horizontal FOV | V = 50 deg, 16:9 / 64:27 | H = 79.317 deg / 95.728 deg | 0.01 deg |
| T4 | Natural monitor FOV | 27" 16:9 (0.59773 m wide), D = 0.70 m | 46.24 deg | 0.02 deg |
| T5 | Distortion overscan | H0 = 90 deg, 16:9, k1 = 0.12, k2 = 0.02 | s_over = 1.19263; render H = 100.04 deg; effective H = 97.49 deg | 1e-4 / 0.02 deg |
| T6 | EV100 from illuminance | E = 520 lux, rho = 0.15 | L = 24.828 cd/m^2, EV100 = 7.634 | 0.005 |
| T7 | Exposure-coupled grain | g0 = 0.05, EV_ref = 8.0, EV = 3.9, g_max = 0.35 | 0.20705 | 1e-4 |
| T8 | Eye DoF blur | A = 4 mm, s = 1.5 m, d = 0.25 m, 2560 px, V = 50 deg 16:9 | beta = 0.013333 rad; p = 1544.04 px/rad; 20.59 px | 0.05 px |
| T9 | Stroke velocity estimator | x(t) = 0.5 t^2 sampled at 1 kHz, window [0.48, 0.50] s | quadratic fit v(0.50) = 0.5000; linear-fit slope = 0.4900 (documents the lag) | 1e-6 |
| T10 | Stroke gain curve | v_m = 0.3 / 1.25 / 2.0 / 3.0 m/s, defaults of 5.4 | v_tip = 0.600 / 4.375 / 10.000 / 12.000 (clamped) m/s | 1e-6 |
| T11 | Steering | y_g = 10 mm, L_bg = 0.8 m, L_bt = 0.2 m | d_psi = 0.7162 deg, tip shift = -2.500 mm | 1e-3 |
| T12 | Bridge height | theta = 5 deg, L_b = 0.20 m, centre hit | z_bridge = 48.497 mm | 0.01 mm |
| T13 | Min cue elevation | obstacle ball 0.10 m straight behind CB, r_t = 6.5 mm, r_b = 15.9 mm, L = 1.47 m, margin 0 | theta_min = 20.787 deg (s* = 64.92 mm, r = 6.915 mm) | 0.01 deg |
| T14 | Coordinate adapter | core p = (1.0, 0.5, R) m | UE = (100, -50, 2.8575) cm | 1e-9 |
| T15 | Quaternion adapter | core q = (cos45, 0, 0, sin45), rotate x_hat | UE q = (cos45, 0, 0, -sin45); rotated UE X = (0, -1, 0) | 1e-12 |
| T16 | Omega adapter | core w = (1, 2, 3) rad/s | UE w = (-1, 2, -3) | exact |
| T17 | Orientation integration | constant w = (0, 10, 0) rad/s for 0.1 s, 1 ms steps, q0 = identity | q = (0.877583, 0, 0.479426, 0) | 1e-9 |
| T18 | Orientation integration (closure) | w = (0, 0, 2 pi) rad/s for 1 s, 1 ms steps | q = +/- identity | 1e-9 |
| T19 | Analytic ball AO | rho = 0, R, 2R | 1.0, 0.353553, 0.089443 | 1e-6 |
| T20 | Sphere tessellation error | R = 28.575 mm, N = 32 / 64 | 0.13760 / 0.03442 mm | 1e-5 mm |
| T21 | Lux probe | 3 point lights I = 600 cd, h = 1.0 m, x = -0.85/0/0.85, y = 0 | E(0,0) = 1130.81 lux; E(1.27, 0.635) = 458.66 lux | 0.05 lux |
| T22 | Lambertian lamp | Phi = 1600 lm, h = 1.0 m straight below | I0 = 509.30 cd, E = 509.30 lux | 0.01 |
| T23 | Impact level law | v = 2 vs 1 m/s; cutoff ratio 10 vs 1 m/s | +7.2247 dB; x1.58489 | 1e-4 |
| T24 | Rolling sign check | ball rolling +x at V = 1 m/s | core w = (0, 34.9956, 0) rad/s; u = 0 | 1e-6 |
| E1 | Performance (Gauntlet) | Pool hall benchmark path, High, 1440p, DLSS Q, RTX 3070 Ti | P95 GPU <= 16.7 ms; VRAM <= 6.8 GB; no hitch > 50 ms after PSO warm-up | - |
| E2 | Look-dev regression | Fixed look-dev camera set (chin-on-cue, overhead, ball close-up) | Screenshot comparison vs approved baseline within automation tolerance; path-traced reference kept for manual A/B | - |
| E3 | Motion vectors | Playback of a 3 m/s rolling ball | Velocity buffer non-zero on ball pixels every frame (no teleports) | - |
| E4 | Lux compliance | Tournament table map | Lux probe min >= 520 lux on bed and rails | - |
| E5 | Audio timing | Break event list with 2 impacts 0.5 ms apart | Rendered audio onsets 0.5 ms +/- 1 sample apart | 1 sample |
| E6 | Head hidden, reflected | Camera at eye socket, ball 20 cm ahead | No head pixels in primary view; head visible in ball reflection (HWRT) | - |

---

## 14. Open questions

1. Default look: **Eyes** (proposed) or Headcam? Is Headcam a mode or a cosmetic "camera" item?
2. Practice strokes: stop short unless "commit" is held (proposed) vs. full realism (tip contact = shot)?
3. Mouse gain curve and a separate "break stance" mode - playtest needed.
4. Steam Deck: target "Verified" at launch or later? (Decides whether Blendable GBuffer/Lumen Lite paths get
   priority.)
5. Online multiplayer at launch? (OSS Steam vs. Online Services, Iris replication.)
6. Venues and table sizes for the first playable (proposal: 9-ft pool hall + 7-ft dive bar). Coin-op cue ball
   (oversize/heavier on some bar tables) - physics/rules owners to confirm whether we model it.
7. Real-brand licensing (balls, cloth, tables) - or fully fictional brands (proposed)?
8. Access to a real pool hall for reference footage, colour checker photos and audio recording?
9. MetaHuman as player body - hands/skin tone customisation for players?
10. VR (Steam's new headset hardware) as a future mode? First-person pool is a natural VR fit; not planned now.
11. May Claude enable Python editor scripting/remote execution (and optionally a Blender MCP) for automation?

---

## 15. Sources

Engine / rendering
- UE 5.8 release notes: https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes
- Tom Looman, UE 5.8 performance highlights: https://tomlooman.com/unreal-engine-5-8-performance-highlights/
- Tom Looman, UE 5.6 performance highlights: https://tomlooman.com/unreal-engine-5-6-performance-highlights/
- Tom Looman, UE 5.7 performance highlights: https://tomlooman.com/unreal-engine-5-7-performance-highlights/
- CG Channel, UE 5.8 key features (release 2026-06-17): https://www.cgchannel.com/2026/06/see-5-key-features-for-cg-artists-in-unreal-engine-5-8/
- Guru3D, UE 5.8 Lumen Lite / MegaLights: https://www.guru3d.com/story/unreal-engine-58-debuts-lumen-lite-and-productionready-megalights/
- UE 5.7 announcement (Substrate production-ready, MegaLights beta): https://www.unrealengine.com/news/unreal-engine-5-7-is-now-available
- UE 5.5 release notes (MegaLights experimental, Substrate beta, path tracer production): https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-5-release-notes
- UE 5.6 announcement (HWRT 60 fps): https://www.unrealengine.com/news/unreal-engine-5-6-is-now-available
- Lumen performance guide: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine
- Hardware ray tracing in UE: https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-ray-tracing-in-unreal-engine
- Substrate overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine
- First Person Rendering: https://dev.epicgames.com/documentation/en-us/unreal-engine/first-person-rendering
- 80.lv, UE 5.5 native first-person rendering: https://80.lv/articles/unreal-engine-5-5-received-native-first-person-rendering-support
- Camera lens calibration (Brown-Conrady spherical model): https://dev.epicgames.com/documentation/unreal-engine/camera-lens-calibration-overview
- UE hardware/software specs (VS 2026, HWRT GPUs): https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-and-software-specifications-for-unreal-engine
- Game Animation Sample for 5.8: https://www.unrealengine.com/tech-blog/download-the-latest-game-animation-sample-project-now-updated-for-ue-5-8
- Online Subsystem Steam: https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-steam-interface-in-unreal-engine
- NVIDIA DLSS UE plugin archive: https://forums.developer.nvidia.com/t/nvidia-dlss-unreal-engine-plugin-archive/377564 ; DLSS page: https://developer.nvidia.com/rtx/dlss
- NVIDIA DLSS 4.5 for UE5 blog: https://developer.nvidia.com/blog/whats-new-for-game-developers-in-nvidia-rtx-dlss-4-5-for-ue5-and-multilingual-ai-characters/
- B. Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 course notes (F0 = 0.08 * Specular).
- H. E. Bennett, J. O. Porteus, "Relation Between Surface Roughness and Specular Reflectance at Normal Incidence", JOSA 51(2), 1961 (TIS).
- I. Quilez, sphere ambient occlusion: https://iquilezles.org/articles/sphereao/
- Physically based values database: https://physicallybased.info/ ; Akenine-Moeller et al., Real-Time Rendering 4th ed. (metal F0 table).

Bodycam / cameras
- Wikipedia, Bodycam (video game): https://en.wikipedia.org/wiki/Bodycam_(video_game)
- 80.lv, Developing a game with realistic graphics in UE (Bodycam interview): https://80.lv/articles/developing-a-game-with-realistic-graphics-in-unreal-engine
- Steam discussion on Bodycam's fisheye: https://steamcommunity.com/app/2406770/discussions/0/6306822998896675912/
- Enduins on Bodycam V0.8: https://www.enduins.com/news/reissad-studio-advances-fps-realism-in-bodycam-ahead-of-major-v0-8-update
- Axon Body 4 product card (120-160 deg diagonal FOV): https://www.axon.com/products/axon-body-4
- GoPro FOV modes: https://community.gopro.com/s/article/What-is-Linear-Field-Of-View-FOV?language=en_US ; https://projectgo.pro/gopro-hero-13-fov/

Human factors
- Hirasaki et al., "Effects of walking velocity on vertical head and body movements during locomotion", Exp. Brain Res. 1999: https://link.springer.com/article/10.1007/s002210050781
- Physiological tremor 8-12 Hz: https://pubmed.ncbi.nlm.nih.gov/943474/
- Postural sway amplitudes: https://www.archives-pmr.org/article/S0003-9993(99)90327-1/abstract ; https://pmc.ncbi.nlm.nih.gov/articles/PMC4736941/
- Breathing rates / motion: https://www.nature.com/articles/s41598-022-12726-z
- A. B. Watson, J. I. Yellott, "A unified formula for light-adapted pupil size", J. Vision 12(10), 2012.
- G. Casiez, N. Roussel, D. Vogel, "1 Euro Filter", CHI 2012.

Pool
- WPA Recommended Equipment Specifications: https://wpapool.com/wp-content/uploads/2024/01/RECOMMENDED-EQUIPMENT-SPECIFICATIONS.pdf
- Dr. Dave, low stance / vision center: https://drdavepoolinfo.com/faq/stance/low/ ; https://drdavepoolinfo.com/faq/eyes/vision-center/
- Dr. Dave, typical ball speeds: https://drdavepoolinfo.com/faq/speed/typical/
- Dr. Dave, ball contact time: https://drdavepoolinfo.com/faq/ball/contact-time/ ; cue tip contact time: https://drdavepoolinfo.com/faq/cue-tip/contact-time/
- Aramith general specifications: https://aramith.com/general-specifications/
- Simonis 860 composition (retailer): https://www.classicbilliards.net/pool-table-felt-cloth/simonis/simonis-860.html
- Phenolic resin refractive indices 1.49-1.60 (patent literature, e.g. US 7323534).

Audio
- L. L. Koss, R. J. Alfredson, "Transient sound radiated by spheres undergoing an elastic collision", J. Sound Vib. 27(1):59-75, 1973: https://www.sciencedirect.com/science/article/abs/pii/0022460X73900357
- Sonniss GDC bundle licence: https://sonniss.com/gdc-bundle-license/
- Steam Audio (Apache 2.0): https://valvesoftware.github.io/steam-audio/downloads.html

Assets / licences
- Fab Standard License: https://www.fab.com/eula ; Fab licences and pricing: https://dev.epicgames.com/documentation/en-us/fab/licenses-and-pricing-in-fab
- CG Channel, Megascans free until end of 2024 / 2025 pricing: https://www.cgchannel.com/2024/10/epic-games-has-made-megascans-free-to-all-but-only-until-the-end-of-2024/
- MetaHuman licensing: https://www.metahuman.com/license ; https://www.cgchannel.com/2025/06/you-can-now-sell-metahumans-or-use-them-in-unity-or-godot/
- RealityScan 2.0: https://www.realityscan.com/news/realityscan-20-new-release-brings-powerful-new-features-to-a-rebranded-realitycapture

Steam
- Steam Deck compatibility review: https://partner.steamgames.com/doc/steamhardware/compat
- ISteamTimeline: https://partner.steamgames.com/doc/api/ISteamTimeline
- Steamworks SDK: https://partner.steamgames.com/doc/sdk
