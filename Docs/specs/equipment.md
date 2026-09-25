# RAW BREAK — Equipment & Geometry Specification

`Docs/specs/equipment.md` · Spec owner: equipment/geometry · Status: v1.0 (2026-09-25)

**Consumers:** BilliardsCore (geometry, presets, validation), physics and rules specs (all geometry comes from here), and 3D art (tables, balls, cues, props, lighting).

**What this spec does not cover:** friction and restitution *laws* and collision resolution belong to the physics spec, and game rules belong to the rules spec. This document only gives the **equipment values** those specs consume. Cloth and cushion coefficients appear here only as clearly labelled presets or hand-off values.

---

## 0. Conventions and tags

### 0.1 Frame and units (project-wide, repeated here for completeness)

- SI units throughout: m, s, kg, rad. Degrees appear only in human-readable tables; code always stores radians.
- 1 in = 0.0254 m **exactly**, and 1 oz (avoirdupois) = 0.028349523125 kg exactly.
- Right-handed world frame:
  - The origin is at the **center of the playing surface**.
  - **+x** runs along the table length toward the **foot rail** (the rack end), so the head rail is at x = −L/2.
  - **+y** runs across the width. Standing at the head end and looking toward the foot (+x), +y points to the **left**.
  - **+z** points up.
- The cloth surface is **z = 0**, so a resting ball's center is at z = R.
- L is the playing length and W the playing width, both measured **cushion nose to cushion nose**, which is how the WPA measures them.
- Ball symbols:
  - R: radius
  - D = 2R: diameter
  - m: mass
  - I = (2/5) m R²: moment of inertia about any axis through the center (uniform solid sphere)
  - r: position
  - v: linear velocity
  - ω (`w` in code): angular velocity, as a world-frame vector in rad/s
- **Contact-point slip velocity with the cloth**, as the physics spec uses it:

  `u = v + ω × (−R ẑ)`

  - The contact point sits at r − R ẑ, and the velocity of the ball material at that point is v + ω × (r_contact − r).
  - Sign check: a ball rolling in +x without slipping has ω = (0, v_x/R, 0). Then ω × (−R ẑ) = −R (ω_y, 0, 0) = (−v_x, 0, 0), so u = 0 as required. The top of the ball moves at ω × (+R ẑ) = (+v_x, 0, 0), which is forward (topspin), as expected.

### 0.2 Tags used in this document

| Tag | Meaning |
|---|---|
| **[WPA-EQ]**, **[BCA]**, etc. | Value taken from the cited source (see §14). |
| **DERIVED** | Computed by us from sourced values. The derivation is shown. |
| **ESTIMATE** | No authoritative source was found. The value is a reasoned default; measure a reference object before final art or tuning. |
| **INTERPRETATION** | The source is ambiguous. Our reading is stated explicitly. |

### 0.3 Pocket and rail naming (used everywhere in code)

| ID | Location (virtual corner/center point) | Notes |
|---|---|---|
| `POCKET_HEAD_RIGHT` | (−L/2, −W/2) | corner |
| `POCKET_HEAD_LEFT` | (−L/2, +W/2) | corner |
| `POCKET_SIDE_RIGHT` | (0, −W/2) | side (middle) |
| `POCKET_SIDE_LEFT` | (0, +W/2) | side (middle) |
| `POCKET_FOOT_RIGHT` | (+L/2, −W/2) | corner |
| `POCKET_FOOT_LEFT` | (+L/2, +W/2) | corner |

The rails are `RAIL_HEAD` (x = −L/2), `RAIL_FOOT` (x = +L/2), `RAIL_LEFT` (y = +W/2) and `RAIL_RIGHT` (y = −W/2). Each long rail consists of two cushion segments separated by its side pocket.

---

## 1. Table presets we ship

| Preset ID | Real-world model | Playing surface L × W | Use in game |
|---|---|---|---|
| `TABLE_9FT_PRO` | WPA 9-ft tournament table (e.g., Diamond Pro-Am with "pro-cut" pockets) | 100 × 50 in = **2.5400 × 1.2700 m** [WPA-EQ] | Tournament and pool-hall levels |
| `TABLE_9FT_TIGHT` | The same table with 4¼-in corner pockets | 2.5400 × 1.2700 m | "Money table" / challenge mode |
| `TABLE_8FT_PRO` | WPA 8-ft ("pro 8") | 92 × 46 in = 2.3368 × 1.1684 m [WPA-EQ] | Optional |
| `TABLE_8FT_HOME` | Common home 8-ft | 88 × 44 in = 2.2352 × 1.1176 m [DD-SIZES] | Optional |
| `TABLE_7FT_BAR` | US coin-op "bar box": Valley 93″ / Great American Legacy / Diamond 7′ | **80 × 40 in = 2.0320 × 1.0160 m** [AZB-VALLEY-SIZE][GA-LEGACY][DIAMOND-PROAM] | **Dive-bar level (default)** |
| `TABLE_7FT_78` | 78 × 39 in "7-ft" (pooltool's default) | 1.9812 × 0.9906 m [PT-SPECS] | Cross-validation against pooltool only |
| `TABLE_7FT_TRUE` | "True" 7-ft / Valley 88″ | 76 × 38 in (Valley 88″ ≈ 75 × 37.5 in) | Optional |

**The 7-ft size is ambiguous in the real world.** Dr. Dave gives a range of 74–78 × 37–39 in for a "7-ft" table and 78–82 × 39–41 in for a "7-ft+" table [DD-SIZES]. Modern US coin-op bar tables are almost all 80 × 40 in (Valley 93″, Great American Legacy 93″ × 53″, Diamond Smart/Pro-Am 7′ at 40 × 80 in). We default to **80 × 40 in**. The WPA equipment spec only defines 9-ft and 8-ft tables. All tables use a playing area that is exactly twice as long as it is wide (L = 2W).

---

## 2. Table body

| Item | 9-ft PRO | 7-ft BAR | Source / tag |
|---|---|---|---|
| Playing surface tolerance | ±1/8 in (±3.175 mm) on each dimension | same (not specified) | [WPA-EQ] §5 |
| Bed (cloth) height above floor | 29¼–31 in (0.74295–0.78740 m); **default 0.765 m** (range midpoint) | **0.743 m** (29¼ in), ESTIMATE/DERIVED (see note 2) | [WPA-EQ] §2 |
| Slate thickness | ≥ 1 in (25.4 mm); tournament tables use 3 equal pieces with a ≥ ¾-in wooden frame | 1 in, one piece (typical for bar boxes) | [WPA-EQ] §4, [DIAMOND-PROAM] |
| Slate flatness | ±0.020 in (0.508 mm) along the length, ±0.010 in (0.254 mm) across the width. Deflection ≤ 0.030 in (0.762 mm) under a 200 lb (90.7 kg) central load. Joints level within 0.005 in (0.127 mm). | — | [WPA-EQ] §4 |
| Rail width incl. cushion | WPA allows 4–7½ in (0.1016–0.1905 m). **Default 7.0 in = 0.1778 m.** | **6.5 in = 0.1651 m** | [WPA-EQ] §6; DERIVED (see note 1) |
| Outside dimensions (L × W × H) | 114 × 64 × 32 in = 2.8956 × 1.6256 × 0.8128 m | 93 × 53 × 31 in = 2.3622 × 1.3462 × 0.7874 m | [DIAMOND-PROAM], [VALLEY-ZD] |
| Rail-cap top above cloth | **≈ 0.048 m** | ≈ 0.048 m | DERIVED/ESTIMATE (see note 2) |
| Table mass (for props/audio only) | ≈ 545 kg (1200 lb) | ≈ 300–320 kg (670–710 lb) | [DIAMOND-PROAM], [VALLEY-ZD] |
| Clearance from rail to obstacles (competition) | ≥ 6 ft (1.83 m) | Bars ignore this, which is why short house cues exist (§8.3) | [WPA-EQ] §19 |

**Note 1 — rail width (DERIVED).** Rail width = (outside dimension − playing dimension)/2:

- Diamond 9-ft: (114 − 100)/2 = 7.0 in.
- Valley 93″: (93 − 80)/2 = 6.5 in. The same holds for the Valley 88″ and 101″ models.

**Note 2 — rail-cap height and bar-box bed height (DERIVED/ESTIMATE).**

- Diamond's overall height is 32 in. With a mid-range bed height of 30.125 in, the rail cap sits about 1.875 in (≈ 47.6 mm) above the cloth.
- Valley's 31-in overall height, minus the same ≈ 1.9 in, gives a bed height of about 29.1–29.25 in.
- Treat both values as ESTIMATE until a real bar box is measured.

**Room clearance for a full 58-in cue** [DD-SIZES]:

| Table | Minimum room size |
|---|---|
| 8-ft | 14′4″ × 18′ (4.37 × 5.49 m) |
| 9-ft | 14′10″ × 19′ (4.52 × 5.79 m) |

Bar rooms are usually smaller. See §8.3.

---

## 3. Table markings: strings, spots and diamonds

### 3.1 Strings and spots

The WPA defines these marks by quarter and half divisions of the playing surface [WPA-RULES] §8. In our frame:

| Name | Definition | Formula | 9-ft (m) | 7-ft BAR (m) |
|---|---|---|---|---|
| Long string | Line down the long center axis | y = 0 | — | — |
| Center string | Line between the side pockets | x = 0 | — | — |
| Head string | Bounds the quarter of the table nearest the head rail | x = −L/4 | x = −0.6350 | x = −0.5080 |
| Foot string | Bounds the quarter nearest the foot rail | x = +L/4 | x = +0.6350 | x = +0.5080 |
| Head spot | Head string ∩ long string | (−L/4, 0) | (−0.6350, 0) | (−0.5080, 0) |
| Center spot | Center string ∩ long string | (0, 0) | (0, 0) | (0, 0) |
| **Foot spot** | Foot string ∩ long string | (+L/4, 0) | **(+0.6350, 0)** | **(+0.5080, 0)** |
| "Kitchen" (above the head string) | Area between the head rail and the head string, **excluding** the string itself | x < −L/4 | — | — |

Only the marks a discipline needs are drawn on the cloth: the foot spot, possibly the head spot and head string, the long string from the foot spot to the foot rail, and the rack outline or alignment marks [WPA-RULES].

**Art notes:**

- A spot is a thin vinyl or paper disc about 20–25 mm in diameter (ESTIMATE). Pencil or chalk lines are also used.
- On bar tables, make the rack outline and head-spot area visibly worn.

### 3.2 Diamonds (sights)

- **Count:** 18 sights, or 17 sights plus a nameplate. They are mounted flush in the rail cap [WPA-EQ] §6.
- **Spacing:** L/8 along the long rails and W/4 along the end rails, measured nose to nose. Because L = 2W, both spacings are equal.
  - 9-ft: 12.5 in = **0.3175 m** [WPA-EQ]
  - 8-ft (92 × 46 in): 11.5 in = 0.2921 m [WPA-EQ]
  - 7-ft BAR: 10 in = **0.2540 m** (DERIVED)
- **Set-back:** each sight's center sits `SIGHT_INSET` = 3 11/16 in = **0.0936625 m** (±3.175 mm) behind the cushion nose, measured perpendicular to the nose line [WPA-EQ].
  - We apply the same set-back to the 7-ft BAR table as an ESTIMATE, since bar boxes do not publish it.
- **Shape:**
  - Round: 7/16–1/2 in diameter (11.11–12.7 mm).
  - Diamond-shaped: from 1 × 7/16 in (25.4 × 11.11 mm) up to 1¼ × 5/8 in (31.75 × 15.875 mm) [WPA-EQ].
  - Material: mother-of-pearl, plastic, or brass on bar boxes (art choice).
- **Position formulas.** Sights sit at z = `RAIL_TOP_Z`.
  - Long rails: x_i = −L/2 + i·L/8 for i ∈ {1, 2, 3, 5, 6, 7}, at y = ±(W/2 + SIGHT_INSET). Index i = 4 is the side pocket, so it has no sight. This gives 6 sights per long rail, 12 in total.
  - End rails: y_j = −W/2 + j·W/4 for j ∈ {1, 2, 3}, at x = ±(L/2 + SIGHT_INSET). This gives 3 sights per end rail, 6 in total.
  - Aiming systems ("diamond systems") also use the **projected diamond lines**: the same x_i and y_j, but on the nose line.

| Sight positions | 9-ft (m) | 7-ft BAR (m) |
|---|---|---|
| Long-rail x_i | ±0.3175, ±0.6350, ±0.9525 | ±0.2540, ±0.5080, ±0.7620 |
| Long-rail y | ±0.7287 | ±0.6017 |
| End-rail y_j | −0.3175, 0, +0.3175 | −0.2540, 0, +0.2540 |
| End-rail x | ±1.3637 | ±1.1097 |

---

## 4. Cushions (rails)

### 4.1 Nose height and contact geometry

- **Nose height:** the cushion nose sits 63.5 % ± 1 % of the ball diameter above the bed [WPA-EQ] §7.
  - `CUSHION_NOSE_HEIGHT` h = 0.635 × 0.05715 = **0.03629 m** (1.429 in).
  - Allowed range: 0.03572–0.03686 m (62.5–64.5 %).
  - pooltool uses 0.64·D = 0.036576 m [PT-SPECS].
- **Contact elevation angle (DERIVED).** A ball resting on the cloth touches the nose line at height h, above its center. The contact normal is tilted downward from horizontal by:

  `θ_c = asin((h − R)/R) = asin(0.27) = 0.27339 rad = 15.664°`

  This is the angle used in Han (2005) and Mathavan et al. (2010) cushion models. It is the physics spec's input.
- **Horizontal contact offset (DERIVED).** When the ball touches the nose, the horizontal distance from the ball center to the nose line is:

  `R_c = sqrt(R² − (h − R)²) = R·cos θ_c = 0.0275137 m`

  This is **1.061 mm less than R**: the ball tucks slightly under the nose.
  - A ball frozen to the `RAIL_LEFT` cushion therefore has its center at y = W/2 − R_c.
  - pooltool uses R here. Keep a compatibility flag (§12).
- **Oversized cue ball (§6.4, DERIVED).** With D = 60.325 mm, the same nose is only at 60.2 % of D, so θ_c drops to 11.72°. The cushion hits the ball lower, and the ball is more prone to climbing or airborne rebounds. Dr. Dave notes that big bar-box cue balls can leave the table on power breaks [DD-BALLW].

### 4.2 Cushion size, profile and rubber

- **Cloth-covered cushion width:** 1 7/8–2 in (**0.047625–0.0508 m**), measured from the outer edge of the feather strip to the nose [WPA-EQ] §7.
  - Note: the WPA PDF prints "2 inches [5.40 cm]", which is a typo; 2 in = 5.08 cm.
  - Default `CUSHION_WIDTH` = 0.0508 m (pooltool uses the same value [PT-SPECS]).
- **Cross-section:** the rubber is triangular. The nose is the forward-most edge. Below the nose, the face slopes back toward the rail, so a ball on the cloth touches **only the nose line** (plus the facings at pockets).
- **Profiles:**

| Profile | Rubber dimensions | Nose height vs. K55 | Typical tables |
|---|---|---|---|
| **K66** | ≈ 1 1/8 in (28.6 mm) across the top, 1 3/16 in (30.2 mm) at the back (glue side) | 1/8 in higher (same rail core) | Standard on most US pool tables |
| **K55** | ≈ 1¼ in (31.8 mm) across the top, 1 5/16 in (33.3 mm) at the back; slightly "meatier" | — | Diamond Pro-Am (listed as "tour specified K-55"), Dynamic II |

  Sources: [CHAMP-K66], [BLACKLABEL-K], [DIAMOND-PROAM], [DYNAMIC].

- **Installed nose height** depends on the rail-core angle [CHAMP-K66]:
  - Typical installations give 1 3/8 in (61 %), 1 13/32 in (62 %) or 1 7/16 in (64 %).
  - Championship recommends 1 13/32 in (35.7 mm).
  - **Game default: 0.03629 m (the WPA 63.5 %).** Both profiles can be built to it.
- **Art profile (ESTIMATE, use a real cross-section photo):** a triangular extrusion with a vertical glue face about 30 mm tall and a top about 29–32 mm deep. The nose is rounded (r ≈ 1–2 mm; pooltool uses a 1 mm nose radius). Cloth wraps over the top into the feather-strip groove.
- **Rail speed (acceptance behaviour) [WPA-EQ] §8.**
  - Test: place a ball on the head spot and shoot it through the foot spot with a center-ball hit, level cue and "firm" stroke. It must travel at least 4–4½ table lengths without jumping.
  - This couples cushion restitution and cloth rolling resistance, so it is a **calibration target for the physics spec** (see §13, T-CAL-1). The WPA does not define "firm", so the physics spec must pick a speed.
- **Rubber:** gum/natural rubber (Dynamic advertises natural rubber [DYNAMIC]). The rail bolts must be torqued so that rebound is uniform along the whole cushion [WPA-EQ].

### 4.3 Pocket facings

- **Material:** hard reinforced rubber, harder than the cushion rubber.
- **Thickness:** 1/16–1/4 in (1.5875–6.35 mm); the WPA prefers at most 1/8 in (**3.175 mm**).
- Both sides of a pocket must have equal facing thickness [WPA-EQ] §9.
- Cloth is wrapped over the facings. Corner pockets must have no folds; side pockets should have minimal overlap [WPA-EQ] §13.

---

## 5. Pockets

### 5.1 Definitions (see the construction in §5.3)

| Term | Definition |
|---|---|
| **Jaw point ("point")** | The **virtual** intersection of the cushion nose line and the facing line, i.e., where the nose "changes direction into the pocket". The real point is rounded (see jaw radius below). |
| **Mouth** | Point-to-point distance across the pocket. This is the WPA/BCA measurement [WPA-EQ] §9. |
| **Cut angle C** | Horizontal angle *inside the cushion material* between the nose line and the facing: 142° ± 1° for corner pockets and 104° ± 1° for side pockets [WPA-EQ]. BCA gives 142° ± 1° and 103° ± 2° [BCA]. |
| **Facing angle from the rail φ** (DERIVED) | φ = 180° − C. This is the angle between the facing direction and the rail direction continuing into the pocket. |
| **Convergence per side β** (DERIVED) | The angle each facing leans toward the pocket axis. Corner: β = C − 135°. Side: β = C − 90°. **WPA values: corner 7°, side 14°.** If β > 0, the throat is narrower than the mouth. This equals pooltool's `corner_pocket_angle` / `side_pocket_angle` convention [PT-LAYOUT]. |
| **Throat** | Width between the facings measured 2 in (50.8 mm) back from the noses. This is Dr. Dave's TDF convention [DD-TDF]. |
| **Shelf** | Distance from the midpoint of the mouth line to the vertical slate cut, measured along the pocket axis, including the bevel [WPA-EQ]. Corner 1–2¼ in and side 0–0.375 in [WPA-EQ]. BCA: corner 1 5/8–1 7/8 in [BCA]. |
| **Drop-point slate radius** | Rounding of the slate edge where the ball falls: 1/8–1/4 in (3.2–6.4 mm) [BCA]. |
| **Vertical pocket angle ("back draft")** | Facings are cut 12°–15° off vertical [WPA-EQ][BCA]. **INTERPRETATION:** the facing is undercut, so its upper edge protrudes toward the opening more than its lower edge. A ball therefore meets the facing above its equator and is pushed down, not up. Support: with a 12°–15° draft, the facing touches a resting ball at z = R(1 + sin β_v) = 34.5–36.0 mm, almost exactly the 36.3 mm cushion-nose height. The facing thus continues the nose contact geometry. Verify on a real table. |
| **Liner** | Plastic, rubber or leather pocket liner and boot. Its upper inner wall must deflect balls downward [WPA-EQ] §10. |
| **Drop pocket / ball return** | Drop pockets must hold ≥ 6 balls. Ball returns must not bounce balls back onto the table [WPA-EQ] §11. |

### 5.2 Pocket presets (mm, with inches in brackets)

| Parameter | WPA range | **9FT_PRO** (default) | 9FT_TIGHT | **7FT_BAR** | Source / tag |
|---|---|---|---|---|---|
| Corner mouth | 114.3–117.5 [4.5–4.625] | **114.3 [4.5]** | 108.0 [4.25] | **123.8 [4.875]** | [WPA-EQ]; Diamond pro-cut is 4½″ corner / 5″ side [AZB-DIAMOND]; Valley league rails 4 7/8″ (older rails ≈ 5″) [AZB-VALLEY-POCKET] |
| Side mouth | 127.0–130.2 [5.0–5.125] | **127.0 [5.0]** | 120.7 [4.75] | **120.7 [4.75]** | same sources; Valley sides ≈ 4¾″ |
| Corner cut angle C | 142° ± 1° | **142°** (β = 7°) | 142° | **138°** (β = 3°), ESTIMATE | Valley corners are described as "pretty close to parallel" [AZB-VALLEY-POCKET] |
| Side cut angle C | 104° ± 1° | **104°** (β = 14°) | 104° | **100°** (β = 10°), ESTIMATE | — |
| Corner shelf | 25.4–57.2 [1–2.25] | **41.3 [1.625]** | 38.1 [1.5] | **6.4 [0.25]**, ESTIMATE | Valley has "no slate shelf" [AZB-VALLEY-POCKET]; tighter pockets use shallower shelves [AZB-DIAMOND] |
| Side shelf | 0–9.5 [0–0.375] | **4.8 [0.1875]** | 3.2 [0.125] | **0**, ESTIMATE | — |
| Vertical back draft | 12°–15° | 12° | 12° | 12° | [WPA-EQ]; installers report >12° is rarely useful |
| Drop-point slate radius | 3.2–6.4 (BCA) | 4.8 | 4.8 | 4.8 | [BCA] |
| Facing thickness | 1.6–6.35 | 3.175 | 3.175 | 6.35 (bar tables use thicker, softer facings), ESTIMATE | [WPA-EQ] |
| Jaw point plan radius r_j | — | **4 mm**, ESTIMATE | 4 mm | 6 mm, ESTIMATE | pooltool uses 20.95 mm corner / 7.95 mm side as tuned values [PT-SPECS] |
| Capture circle radius r_p (sim) | — | corner 62.0, side 64.5 | same | same | [PT-SPECS] |
| **DERIVED: corner throat @ 50.8 mm** | — | **94.19 [3.708]** | 87.84 [3.458] | 115.88 [4.562] | Formula in §5.3 |
| **DERIVED: side throat @ 50.8 mm** | — | **101.67 [4.003]** | 95.32 [3.753] | 102.74 [4.045] | Formula in §5.3 |

**Context values:**

- Diamond "league cut": 5″ corner / 5½″ side [AZB-DIAMOND].
- Older BCA "standard" mouths: corner 4 7/8–5 1/8″ and side 5 3/8–5 5/8″ [BCA].
- Dr. Dave's Table Difficulty Factor treats a corner mouth of 4½–4¾″ and a mouth-minus-throat of 5/8–3/4″ (140.7°–141.7°) as "average" [DD-TDF].
- Our WPA 142° corner gives a mouth-minus-throat of **0.792″**, which falls in the TDF 3/4–7/8″ band. This cross-check was DERIVED and matches the TDF's printed angle-to-difference mapping exactly (see T-POCKET-3).

**Mapping to pooltool parameters (DERIVED):**

- `corner_pocket_angle = C_corner − 135°`
- `side_pocket_angle = C_side − 90°`
- `corner_pocket_width` = mouth
- `side_pocket_width` = mouth

pooltool's defaults (5.3°, 7.14°) correspond to C = 140.3° and 97.1°.

### 5.3 Plan-view construction (DERIVED; all at z = h = `CUSHION_NOSE_HEIGHT`)

**Corner pocket `POCKET_FOOT_LEFT`** (corner point K = (L/2, W/2)). The other three corner pockets are mirror images (x → −x and/or y → −y).

```
a    = M_c / sqrt(2)                         // jaw-point offset from K along each rail
P_L  = (L/2 − a, W/2)                         // jaw point on the long (left) rail
P_E  = (L/2, W/2 − a)                         // jaw point on the end (foot) rail
φ    = π − C_c                                // facing angle from rail direction (38° for WPA)
f_L  = ( cos φ,  sin φ)                       // facing direction from P_L, into the rail
f_E  = ( sin φ,  cos φ)                       // facing direction from P_E
Mid  = ((P_L + P_E)/2)                        // mouth-line midpoint
d    = (1, 1)/sqrt(2)                         // pocket axis, pointing out of the table
throat(depth t measured ⟂ to each rail) = M_c − sqrt(2)·t·(cot φ − 1)
facing length to cushion back = CUSHION_WIDTH / sin φ          (0.0825 m for WPA)
capture center C_cap = Mid + (s_c + r_p)·d     // front of capture circle = slate edge
```

**Side pocket `POCKET_SIDE_LEFT`** (the center of the nose gap is (0, W/2)):

```
P_+  = (+M_s/2, W/2),  P_− = (−M_s/2, W/2)
β_s  = C_s − π/2                              // 14° for WPA
f_+  = (−sin β_s, cos β_s),  f_− = (+sin β_s, cos β_s)
throat(t) = M_s − 2·t·tan β_s
facing length to cushion back = CUSHION_WIDTH / cos β_s        (0.05235 m for WPA)
capture center = (0, W/2 + s_s + r_p)
```

**Rounded jaw points (DERIVED).** A circle of radius r_j is tangent to both the nose line and the facing line.

- Its tangent points sit `r_j / tan(C/2)` from the virtual point along each line. With r_j = 4 mm:
  - corner (C = 142°): 0.344·r_j = 1.377 mm
  - side (C = 104°): 0.781·r_j = 3.125 mm
- Its center sits `r_j / sin(C/2)` from the virtual point, along the bisector, inside the material.
- This is the same construction as pooltool's `dc`/`ds` offsets [PT-LAYOUT].

**Cushion nose segments.** Each segment ends at the tangent point of its rounded jaw:

- Long rail +y: x ∈ [−L/2 + a, −M_s/2] and [+M_s/2, L/2 − a] at y = +W/2.
- End rail +x: y ∈ [−W/2 + a, W/2 − a] at x = +L/2.

**Computed coordinates (m):**

| Element | 9FT_PRO | 7FT_BAR |
|---|---|---|
| Corner a | 0.080822 | 0.087557 |
| P_L (FOOT_LEFT) | (1.189178, 0.635000) | (0.928443, 0.508000) |
| P_E (FOOT_LEFT) | (1.270000, 0.554178) | (1.016000, 0.420443) |
| Mouth midpoint | (1.229589, 0.594589) | (0.972221, 0.464221) |
| f_L | (0.78801, 0.61566) | (0.74314, 0.66913) |
| Corner capture center (r_p = 0.062) | (1.302615, 0.667615) | (1.020552, 0.512552) |
| Side jaw points (SIDE_LEFT) | (±0.063500, 0.635000) | (±0.060325, 0.508000) |
| f_+ (from +x jaw) | (−0.24192, 0.97030) | (−0.17365, 0.98481) |
| Side capture center (r_p = 0.0645) | (0, 0.704263) | (0, 0.572500) |

**Simulation model (hand-off to the physics spec):**

- The pocketed-ball region is a vertical cylinder of radius r_p. It follows pooltool's circle model, with the circle placed so that its front edge lies on the slate cut.
- The pocket interior (liner, drop, rattle between the facings) is modelled by the physics spec. This spec only supplies the geometry above plus the back-draft and drop-point radius.
- For **3D art**, build the real shapes:
  - rail cut along the facing lines;
  - facing rubber of the thickness above;
  - slate cut as a vertical face at the shelf depth, rounded at the top by the drop-point radius;
  - liner cylinder.
- **Pro tables:** leather drop pockets with a basket or net holding ≥ 6 balls.
- **Bar tables:** rubber pocket cups over a gully ball-return. A separate cue-ball return opening is on the end apron. The object balls stay locked until coins are inserted.

---

## 6. Balls

### 6.1 Standard pool balls

| Property | Value | Source |
|---|---|---|
| Diameter | 2¼ in ± 0.005 in = **57.15 mm ± 0.127 mm**. Aramith quotes 57.2 mm. | [WPA-EQ] §16, [ARAMITH-SP] |
| Mass | 5½–6 oz = **156–170 g** allowed. Super Aramith Pro: 170 g (6 oz) nominal. Typical sets weigh 5.8–5.9 oz (164–167 g) [DD-BALLW]. **Default 0.170097 kg (6 oz, same as pooltool).** | [WPA-EQ], [ARAMITH-SP], [PT-BALL] |
| Moment of inertia | I = (2/5) m R² = **5.55558 × 10⁻⁵ kg·m²** (DERIVED for default m, R) | — |
| Density (DERIVED) | m / (4/3 π R³) = 1740 kg/m³ | — |
| Material | Cast phenolic resin, unpolished and unwaxed; cleaned with soap and water or dilute alcohol | [WPA-EQ] |

**Set composition and colors [WPA-EQ] §16:**

- One white cue ball plus 15 object balls.
- **Solids:** 1 yellow, 2 blue, 3 red, 4 purple, 5 orange, 6 green, 7 maroon, 8 black.
- **Stripes:** 9–15 are white with a centered colored band, in the same color order (9 yellow … 15 maroon).
- **Numbers:** each number is printed twice, on opposite sides of the ball, with one of the two upside-down. Black numerals on a white circle. The **6 and 9 are underscored.**
- **Art values (ESTIMATE):**
  - Number circle ≈ 22–24 mm in diameter.
  - Stripe band ≈ ±35° latitude, so about 33 mm tall.
  - The sRGB color references below are for a lookdev start only. Match photos of a real Aramith set under a 5000 K source.

| # | Color | sRGB (ESTIMATE) |
|---|---|---|
| 1 / 9 | yellow | #F2C230 |
| 2 / 10 | blue | #1E3F9A |
| 3 / 11 | red | #C8202E |
| 4 / 12 | purple | #4A2B7A |
| 5 / 13 | orange | #EE6A22 |
| 6 / 14 | green | #11754A |
| 7 / 15 | maroon | #6E1E22 |
| 8 | black | #111111 |

### 6.2 Cue balls

| Type | Diameter | Mass | Where used | Source |
|---|---|---|---|---|
| Standard (Super Aramith Pro, small red logo) | 57.15 mm | 170 g | Pool halls, home tables | [ARAMITH-CB] |
| Pro-Cup "6 red dots" / "Measles" (many red dots) | 57.15 mm | ≈ 170 g | TV and pro events; the dots make spin visible (**use it for the replay camera!**) | [ARAMITH-CB] |
| Red Circle | 57.15 mm | 170 g | Diamond Smart coin-op table: an optical-density sensor separates it, so no size or weight difference is needed | [ARAMITH-CB], [DIAMOND-SMART] |
| **Magnetic** (Aramith Magnetic / Tournament Magnetic) | 57.15 mm | **≈ 167 g** measured on Valley bar boxes (range 164–168 g) | **Modern US coin-op (Valley).** A magnet in the return path diverts it. Aramith says the magnetic material is spread evenly, so treat it as uniform density. Official VNEA cue ball. | [DD-BALLW] (Feb-2012 article with measurements), [ARAMITH-CB] |
| **Oversized** "bar ball" | **2⅜ in = 60.325 mm** (Aramith lists 60.5 and 61.5 mm variants) | **221 g (7.8 oz)** listed; a same-density phenolic ball would be 200 g (DERIVED) | Older US bar boxes; European coin-op tables with *optical* separation use 60.3 mm | [OVERSIZE-CB], [DE-COINOP-CB], [ARAMITH-CB] |

### 6.3 Dive-bar realism: measured wear and mismatch (use it!)

Dr. Dave weighed the balls on eight Valley bar boxes [DD-BALLW]:

| Ball | Average | Minimum | Maximum |
|---|---|---|---|
| Cue balls | 5.89 oz (167 g) | 164 g | 168 g |
| Object balls | 5.75 oz (163 g) | 155 g | 167 g |

The lightest object ball was a 1-ball. Worn balls also get smaller and rack loosely.

**Recommended `BallSet` preset "DIVE_BAR" (sampled per ball at level load, deterministic seed):**

- **Object balls:** m ~ N(0.163, 0.003) clamped to [0.155, 0.167] kg; D ∈ [57.00, 57.15] mm (ESTIMATE).
- **Cue ball:** magnetic, 0.167 kg, D = 57.15 mm.
- **Optional "old bar" variant:** oversized 60.325 mm / 0.221 kg cue ball.

The physics must therefore support **per-ball R, m and I**. Unequal radii also make ball–ball contacts hit off the equator, giving "hop" effects [DD-BALLW].

### 6.4 Other ball standards (notes only)

| Standard | Ball size and mass |
|---|---|
| Snooker | 52.5 mm; pooltool uses 0.140 kg [PT-BALL] |
| Carom | 61.5 mm, ≈ 7.5 oz (213 g) [DD-BALLW] |
| UK "blackball" pub pool | 2 in (50.8 mm) object balls with a 1⅞ in (47.6 mm) cue ball; Aramith sells both sizes [ARAMITH-CB] |

UK pub pool is a different game and table. It is out of scope, but keep it in mind for any UK-set level.

---

## 7. Cloth

| Cloth | Type | Composition | Weight | Speed / notes |
|---|---|---|---|---|
| **Simonis 860** | Worsted, nap-free | 90 % wool / 10 % nylon | ±410 g/m² | The tournament standard; controlled, medium-fast [SIMONIS] |
| Simonis 860 HR | Worsted | 70/30 | ±410 g/m² | Hybrid, a bit faster [SIMONIS] |
| **Simonis 760** | Worsted | 70/30 | ±355 g/m² | Fastest Simonis pool cloth (≈ 10 % faster than 860, retailer claim); high traffic [SIMONIS] |
| **Bar / napped woolen** (e.g., Championship Titan, Valley cloth) | Woolen, **napped** (raised fibres, directional) | ≈ 75 % wool / 25 % nylon | "21 oz" (retailer rating) | Slower and grabbier; pills and wears into visible ball tracks [CHAMP-TITAN] |

- **WPA rule:** non-directional, nap-free worsted cloth that does not pill, 80–85 % worsted wool and 15–20 % nylon (100 % worsted preferred), and no backed cloth. **Colors: yellow-green, blue-green or electric blue only** [WPA-EQ] §12.
- **Colors in practice:**
  - Simonis offers about 26 colors; "Tournament Blue" is the TV favourite [SIMONIS]. Strictly, it is not one of the three WPA colors, so use Electric Blue or Blue-Green for WPA-accurate events.
  - Bars mostly use green, burgundy or blue.
- **Nap direction (bar cloth):** the nap is normally brushed from the head toward the foot (ESTIMATE). Slow balls drift slightly, and balls rolling against the nap slow faster.
  - This is an optional physics feature (`CLOTH_NAP_DIR = +x̂`, asymmetry parameter owned by the physics spec).
- **Hand-off ranges for the physics spec** (Dr. Dave's measured ranges [DD-CONST]):

| Coefficient | Range |
|---|---|
| Sliding friction μ_s | 0.15–0.4 (typical 0.2) |
| Rolling resistance μ_r | 0.005–0.015 |
| Spin (z-axis) deceleration | 5–15 rad/s² |
| Ball–table restitution | 0.5–0.7 |

  - Suggested starting presets (ESTIMATE, inside those ranges): worsted 860 new μ_r ≈ 0.006–0.008; napped bar cloth μ_r ≈ 0.010–0.015, with μ_s slightly higher.
- **Art (bar):** show worn lanes along the head-to-foot line and the break-spot wear, chalk smudges near the pockets, the rack triangle impression, cigarette burns and beer rings on the rails, and a slightly frayed cloth at the pocket points. On pro tables, the cloth is uniform and freshly brushed.

---

## 8. Cues and accessories

### 8.1 Playing cue (the player's own cue)

| Property | Value | Source / tag |
|---|---|---|
| Length | Standard **58 in = 1.4732 m**; common 57–59 in. The WPA requires ≥ 40 in (1.016 m), with no maximum. | [DD-CUELEN], [WPA-EQ] §17 |
| Mass | Typical **19 oz = 0.5386 kg** (common range 18–21 oz = 0.510–0.595 kg); WPA maximum 25 oz = 0.70874 kg | [DD-WEIGHT], [WPA-EQ] |
| Tip diameter | Pool 11–13 mm, most commonly **12.75–13 mm**; WPA maximum 14 mm. Snooker ≈ 10 mm. | [DD-TIP], [WPA-EQ] |
| Tip dome | "Nickel" radius = 10.6 mm or "dime" radius = 8.96 mm (DERIVED from US coin diameters 21.21 mm and 17.91 mm) | [DD-TIP] |
| Tip material | Layered or solid leather; phenolic on break and jump cues | [WPA-EQ], [DD-JUMP] |
| Ferrule | Low-deflection (LD) shafts ≈ 0.5 in (12.7 mm); traditional ≈ ¾–1 in; a metal ferrule may be at most 1 in (25.4 mm) | [PREDATOR-314], [WPA-EQ] |
| Shaft | Length **29 in (0.7366 m)**; two-piece cue with the joint at mid-length | [PREDATOR-314] |
| Taper | **Pro taper:** constant diameter for the first ~12–14 in (30–35 cm), then widening. **European taper:** widens continuously. | [DD-TAPER] |
| Joint and butt diameters | Joint ≈ 21–22 mm; butt at the bumper ≈ 30–32 mm; balance point ≈ 18–19 in from the butt end (ESTIMATE) | ESTIMATE |
| Squirt-relevant effective end mass | "Typical" cue ≈ 5 g (ball-to-end-mass ratio ≈ 30). Low-deflection shafts reduce the mass of the last 5–8 in. | [DD-ENDMASS] |
| Measured deflection ranking | LD shafts (Predator Z-2, 314, OB-1) deflect least; break cues most | [DD-DEFL] |
| Tip–ball contact time | ≈ 1 ms | [DD-ENDMASS] |

### 8.2 Special cues

| Cue | Typical specification | Source / tag |
|---|---|---|
| **Break cue** | 18–21 oz; very hard or phenolic tip ≈ 13–14 mm; stiff shaft | ESTIMATE from market; phenolic tip-ball e = 0.81–0.87 vs. leather 0.71–0.75 [DD-CONST] |
| **Jump cue** | 40 in (the WPA minimum) or 47 in; ≈ **10 oz (0.283 kg)**; phenolic tip 13.75 mm (Predator Air II); shorter, lighter and stiffer; often 3 pieces | [PREDATOR-AIR], [DD-JUMP] |
| Break/jump combination | Break cue whose butt comes off to leave a jump cue | [DD-JUMP] |
| **Bar house cue** (rack on the wall) | One-piece maple, **57 in (1.448 m)** standard; short cues of **48, 52 and 36 in** for walls close to the table; 18–21 oz; 11–13 mm tips (glue-on or screw-on); rubber bumper | [HOUSE-CUES] |

For realism, house cues may be warped and have mushroomed or flat tips. That could be gameplay: more miscues with a worn tip (ESTIMATE, owned by the physics spec).

### 8.3 Short cues in cramped bars (gameplay hook)

The WPA wants 6 ft of clearance, but bar tables often sit 1.2–1.5 m from a wall.

- The game should do a **cue–environment collision check** (butt end vs. walls, other props) when the player aims.
- When the 58-in cue collides, offer the 48- or 52-in house cue.

### 8.4 Mechanical bridge (rest / "rake")

- A stick similar to a cue (≈ 1.45–1.5 m, ESTIMATE) with a head that has grooves or notches at several heights. The head must be smooth so it does not mark the shaft or tear the cloth [WPA-EQ] §18.
- Art (ESTIMATE): the head is ≈ 100–130 mm wide and ≈ 60–80 mm tall, in brass or plastic, with 3–5 V-grooves between 25 and 75 mm above the cloth. Variants: "cross" head and "moose-head" (tall) head.

### 8.5 Chalk

- A cube of about 7/8–1 in (**≈ 22–25 mm**, ESTIMATE; measure a real Master cube) in a printed paper wrapper.
  - One face is used, and it becomes a concave cup with use.
  - Made of silica and corundum (aluminum oxide) with dye and binder [CHALK].
- **Colors:** blue by far the most common, then green. The WPA recommends only green or blue [WPA-EQ] §14.
- **Visual consequences:** chalk dust on the tip, and blue chalk marks on the cue ball after contact ("chalk marks" also cause kicks, which the physics spec may add as an option).

---

## 9. Racks, templates and rack placement

### 9.1 Devices

- **Wooden triangle:** WPA-recommended. Its contact surfaces must be smooth. Plastic racks are discouraged because they flex [WPA-EQ] §16. Bars still commonly use plastic racks (art).
- **Ball rack template ("magic rack"):** a plastic sheet **≤ 0.14 mm thick**. It must not be glued down.
  - Positioned by a pre-drawn line along the long string that passes through its top and bottom holes.
  - Allowed for 8-, 9- and 10-ball and Heyball; **not** allowed for 14.1.
  - It is removed after the break (with limits when balls obstruct removal) [WPA-REGS] §4.
  - A template lying on the rail that a ball touches is a foul [WPA-RULES] 3.15. The rules spec owns this.
  - **Physics:** a sheet of this thickness is negligible (0.14 mm ≪ R). Ignore it in the simulator. Render it as a thin decal that is removed after the break.

**Inner dimensions of a perfectly tight rack (DERIVED).** Offsetting each side of the ball-center polygon outward by R lengthens each side by R·cot(α/2) at each end (α = the interior angle at that end).

| Rack | Inner side length |
|---|---|
| 15-ball triangle | (4 + √3)·D = **0.327587 m** (12.90 in) |
| 10-ball triangle | (3 + √3)·D = **0.270437 m** (10.65 in) |
| 9-ball diamond (60°/120° rhombus) | (2 + 2/√3)·D = **0.180291 m** (7.10 in) |

Real racks are 1–3 mm larger (ESTIMATE).

### 9.2 Rack placement (DERIVED from [WPA-RULES] §4.2, 5.2, 6.2)

Frozen-ball lattice:

- row spacing Δx = (√3/2)·D = **0.0494934 m**
- ball spacing within a row Δy = D
- the rows extend in **+x** (toward the foot rail)

Ball (row k, index j):

`x = x_apex + k·Δx`, `y = (j − (n_k − 1)/2)·D`

| Game | Rows n_k | Apex position | Fixed balls |
|---|---|---|---|
| 8-ball | 1,2,3,4,5 | apex ball on the foot spot: x_apex = +L/4 | 8-ball at row 2 center (x = L/4 + √3·D, y = 0); the two back corners (row 4, j = 0 and 4) are one solid and one stripe; the rest are random |
| 9-ball (**WPA 2025 text**) | 1,2,3,2,1 | **9-ball on the foot spot**, so x_apex = L/4 − √3·D; the 1-ball is at the apex, nearest the head | 9 at the center (row 2, j = 1) |
| 9-ball (legacy / many leagues) | 1,2,3,2,1 | **1-ball on the foot spot**: x_apex = L/4 | 9 at the center |
| 10-ball | 1,2,3,4 | 1-ball on the foot spot: x_apex = +L/4 | 10 at row 2 center (x = L/4 + √3·D) |

**Which 9-ball placement?** The 2025-09-15 WPA rules place the 9 on the foot spot. Earlier editions and many league rule sets are widely reported to put the 1 on the foot spot; we could not verify this against a primary source here. The **rules spec decides**. The geometry supports both via the `RackAnchor` enum.

**Computed rack coordinates (frozen racks, m):**

| | 9-ft (x_fs = 0.635) | 7-ft BAR (x_fs = 0.508) |
|---|---|---|
| 8-ball, row x values | 0.63500, 0.68449, 0.73399, 0.78348, 0.83297 | 0.50800, 0.55749, 0.60699, 0.65648, 0.70597 |
| 8-ball row-4 y values | ±0.11430, ±0.05715, 0 | same |
| 9-ball (9 on spot): apex, center, back | (0.53601, 0), (0.63500, 0), (0.73399, 0) | (0.40901, 0), (0.50800, 0), (0.60699, 0) |
| Gap from 8-ball back row to foot nose | 0.43703 | 0.31003 |

**Rack tightness.** Real racks are never perfectly frozen. Model the gaps with `RACK_GAP_MEAN` / `RACK_GAP_JITTER` (per-contact gap, **0 to 0.2 mm**, ESTIMATE), which the break/physics spec tunes. Worn bar balls give looser racks [DD-BALLW]. pooltool similarly perturbs rack spacing with a small random factor proportional to R.

---

## 10. Lighting

### 10.1 WPA requirements [WPA-EQ] §15

| Requirement | Value |
|---|---|
| Illuminance at **every point** of the bed and rails | **≥ 520 lux** (48 fc) |
| Uniformity | The center must not be noticeably brighter than the rails and corners (screen or reflector advised) |
| Fixture height above the bed, movable fixture | ≥ 40 in = **1.016 m** |
| Fixture height above the bed, fixed fixture | ≥ 65 in = **1.65 m** |
| Glare limit (direct view) | "Blinding" starts at 5000 lux |
| Venue | ≥ 50 lux |

### 10.2 Typical bar lighting (for art direction)

- **Fixture:** a 2–3-shade pendant (a Tiffany-style glass or green or brewery-branded metal shade) hanging **31–36 in (0.79–0.91 m)**, most often ≈ 33–34 in, above the playing surface. The bottom of the shade is ≈ 60–66 in above the floor [LIGHT-BAR].
  - This is much lower than the WPA minimum. That is the look we want: hot pools of light, dark corners and a visible fixture in the POV.
- **Color temperature (ESTIMATE):** bar incandescent or warm LED 2200–2900 K. Tournament and TV lighting is LED 5000–6500 K from large rectangular soft boxes (ESTIMATE).
- **Illuminance under a bar shade (ESTIMATE, DERIVED order of magnitude):**
  - With a ≈ 1600 lm lamp and a reflector concentrating it into ~1.5 sr, the intensity is I ≈ 1000 cd.
  - At d = 0.85 m, E = I/d² ≈ 1400 lux directly below the shade.
  - At the corners, ~1.3 m off-axis, only a few hundred lux, so the table is well below WPA uniformity.
  - Use this as the physically based lighting target for the dive bar.

---

## 11. Constants table (SI, for code)

Values are for `TABLE_9FT_PRO` / `TABLE_7FT_BAR`; a single value applies to both. Angles are in radians, with degrees in brackets. Store all values as `double`.

### 11.1 Balls and global equipment

| Constant | Value | Unit | Tag |
|---|---|---|---|
| `BALL_DIAMETER` | 0.05715 | m | [WPA-EQ] |
| `BALL_RADIUS` | 0.028575 | m | [WPA-EQ] |
| `BALL_DIAMETER_TOL` | 0.000127 | m | [WPA-EQ] |
| `BALL_MASS` | 0.17009713875 | kg | 6 oz, [PT-BALL] |
| `BALL_MASS_MIN` / `BALL_MASS_MAX` | 0.156 / 0.170 | kg | [WPA-EQ] |
| `BALL_INERTIA` | 5.5555809e-5 | kg·m² | DERIVED |
| `BALL_DENSITY` | 1740.4 | kg/m³ | DERIVED |
| `CUEBALL_MAGNETIC_MASS` | 0.167 | kg | [DD-BALLW] |
| `BAR_OBJECT_BALL_MASS_MEAN` / `_MIN` / `_MAX` | 0.163 / 0.155 / 0.167 | kg | [DD-BALLW] |
| `CUEBALL_OVERSIZED_DIAMETER` | 0.060325 | m | [OVERSIZE-CB] |
| `CUEBALL_OVERSIZED_MASS` | 0.2211 (0.2000 at equal density) | kg | [OVERSIZE-CB] / DERIVED |
| `CUSHION_NOSE_HEIGHT` | 0.03629025 | m | [WPA-EQ] 63.5 % D |
| `CUSHION_NOSE_HEIGHT_MIN` / `_MAX` | 0.03571875 / 0.03686175 | m | [WPA-EQ] |
| `CUSHION_CONTACT_ANGLE` | 0.2733943 [15.664°] | rad | DERIVED |
| `CUSHION_CONTACT_OFFSET_XY` (R_c) | 0.0275137 | m | DERIVED |
| `CUSHION_WIDTH` | 0.0508 | m | [WPA-EQ], [PT-SPECS] |
| `CUSHION_NOSE_PROFILE_RADIUS` | 0.001 | m | [PT-SPECS] (ESTIMATE for art) |
| `FACING_THICKNESS` | 0.003175 / 0.00635 | m | [WPA-EQ] / ESTIMATE |
| `POCKET_BACKDRAFT_ANGLE` | 0.2094395 [12°] | rad | [WPA-EQ] |
| `POCKET_DROP_POINT_RADIUS` | 0.0047625 | m | [BCA] |
| `SIGHT_INSET_FROM_NOSE` | 0.0936625 | m | [WPA-EQ] |
| `SIGHT_DIAMETER_ROUND` | 0.0127 | m | [WPA-EQ] |
| `RACK_ROW_SPACING` | 0.0494934 | m | DERIVED |
| `RACK15_INNER_SIDE` / `RACK10_INNER_SIDE` / `RACK9_DIAMOND_INNER_SIDE` | 0.327587 / 0.270437 / 0.180291 | m | DERIVED |
| `RACK_TEMPLATE_MAX_THICKNESS` | 0.00014 | m | [WPA-REGS] |
| `CUE_LENGTH` / `CUE_LENGTH_MIN` | 1.4732 / 1.016 | m | [DD-CUELEN], [WPA-EQ] |
| `CUE_MASS` / `CUE_MASS_MAX` | 0.5386 / 0.70874 | kg | [DD-WEIGHT], [WPA-EQ] |
| `CUE_TIP_DIAMETER` / `_MAX` | 0.01275 / 0.014 | m | [PREDATOR-314], [WPA-EQ] |
| `CUE_TIP_DOME_RADIUS` | 0.0106 (nickel) / 0.00896 (dime) | m | DERIVED |
| `CUE_SHAFT_LENGTH` | 0.7366 | m | [PREDATOR-314] |
| `CUE_FERRULE_LENGTH` | 0.0127 (LD) / 0.0254 (max, metal) | m | [PREDATOR-314], [WPA-EQ] |
| `CUE_END_MASS_TYPICAL` | 0.005 | kg | [DD-ENDMASS] |
| `JUMP_CUE_LENGTH` / `JUMP_CUE_MASS` / `JUMP_CUE_TIP_DIAMETER` | 1.016 / 0.2835 / 0.01375 | m / kg / m | [PREDATOR-AIR] |
| `HOUSE_CUE_LENGTHS` | {1.4478, 1.3208, 1.2192, 0.9144} | m | [HOUSE-CUES] |
| `CHALK_CUBE_EDGE` | 0.022 | m | ESTIMATE |
| `LIGHT_MIN_ILLUMINANCE` | 520 | lux | [WPA-EQ] |
| `LIGHT_GLARE_ILLUMINANCE` | 5000 | lux | [WPA-EQ] |
| `LIGHT_MIN_HEIGHT_MOVABLE` / `_FIXED` | 1.016 / 1.65 | m | [WPA-EQ] |
| `BAR_LIGHT_HEIGHT` | 0.84 (range 0.79–0.91) | m | [LIGHT-BAR] |

### 11.2 Per-table constants

| Constant | `TABLE_9FT_PRO` | `TABLE_7FT_BAR` | Unit | Tag |
|---|---|---|---|---|
| `TABLE_LENGTH` (L) | 2.5400 | 2.0320 | m | [WPA-EQ] / [AZB-VALLEY-SIZE] |
| `TABLE_WIDTH` (W) | 1.2700 | 1.0160 | m | same |
| `TABLE_HALF_LENGTH` / `_HALF_WIDTH` | 1.2700 / 0.6350 | 1.0160 / 0.5080 | m | DERIVED |
| `BED_HEIGHT` | 0.765 | 0.743 | m | [WPA-EQ] / ESTIMATE |
| `RAIL_WIDTH_TOTAL` | 0.1778 | 0.1651 | m | DERIVED |
| `RAIL_TOP_Z` | 0.048 | 0.048 | m | DERIVED/ESTIMATE |
| `SLATE_THICKNESS` | 0.0254 | 0.0254 | m | [WPA-EQ] |
| `SIGHT_SPACING` | 0.3175 | 0.2540 | m | [WPA-EQ] / DERIVED |
| `HEAD_STRING_X` | −0.6350 | −0.5080 | m | DERIVED |
| `FOOT_SPOT_X` (y = 0) | +0.6350 | +0.5080 | m | DERIVED |
| `HEAD_SPOT_X` (y = 0) | −0.6350 | −0.5080 | m | DERIVED |
| `CORNER_POCKET_MOUTH` | 0.1143 | 0.123825 | m | [WPA-EQ] / [AZB-VALLEY-POCKET] |
| `SIDE_POCKET_MOUTH` | 0.1270 | 0.12065 | m | same |
| `CORNER_CUT_ANGLE` | 2.4783675 [142°] | 2.4085544 [138°] | rad | [WPA-EQ] / ESTIMATE |
| `SIDE_CUT_ANGLE` | 1.8151424 [104°] | 1.7453293 [100°] | rad | [WPA-EQ] / ESTIMATE |
| `CORNER_SHELF` | 0.041275 | 0.00635 | m | [BCA] / ESTIMATE |
| `SIDE_SHELF` | 0.0047625 | 0.0 | m | [WPA-EQ] / ESTIMATE |
| `CORNER_JAW_RADIUS` / `SIDE_JAW_RADIUS` | 0.004 / 0.004 | 0.006 / 0.006 | m | ESTIMATE |
| `CORNER_CAPTURE_RADIUS` / `SIDE_CAPTURE_RADIUS` | 0.062 / 0.0645 | 0.062 / 0.0645 | m | [PT-SPECS] |
| `CORNER_JAW_OFFSET_A` (= mouth/√2) | 0.0808223 | 0.0875575 | m | DERIVED |
| `CORNER_THROAT_2IN` | 0.0941884 | 0.1158784 | m | DERIVED |
| `SIDE_THROAT_2IN` | 0.1016683 | 0.1027352 | m | DERIVED |
| `RACK_APEX_X_8BALL` | 0.6350 | 0.5080 | m | DERIVED |
| `CLOTH_PRESET` | WORSTED_SIMONIS_860 | NAPPED_BAR | enum | §7 |
| `CUEBALL_PRESET` | STANDARD_170G | MAGNETIC_167G | enum | §6.2 |

Suggested C++ (BilliardsCore, no Unreal types):

```cpp
struct PocketSpec  { double mouth, cutAngle, shelf, jawRadius, captureRadius; };
struct TableSpec   {
  double length, width, bedHeight, cushionNoseHeight, cushionWidth, railWidthTotal,
         railTopZ, sightInset;
  PocketSpec corner, side;
  double backdraft, dropPointRadius, facingThickness;
};
struct BallSpec    { double radius, mass, inertia; };     // per ball (supports worn / oversized)
inline constexpr TableSpec TABLE_9FT_PRO { 2.54, 1.27, 0.765, 0.03629025, 0.0508, 0.1778, 0.048,
  0.0936625, {0.1143, 2.4783675, 0.041275, 0.004, 0.062}, {0.127, 1.8151424, 0.0047625, 0.004, 0.0645},
  0.2094395, 0.0047625, 0.003175 };
// TableGeometry BuildGeometry(const TableSpec&)  -> nose segments, facing segments, jaw arcs,
//                                                   capture circles, diamonds, spots (§5.3, §3)
// WpaReport       ValidateWpa(const TableSpec&) -> per-field pass/fail vs. §2/§4/§5 ranges
```

---

## 12. Implementation notes and pitfalls

1. **Measure from the cushion nose, never the rail.** L, W, diamonds, strings and spots are all nose-to-nose (WPA). Store the geometry in the world frame, centered at the bed center.
2. **Frozen-to-cushion distance.** A ball touching the nose has its center **R_c = 27.514 mm** (not R) from the nose line, because the nose is above the equator. Use `CUSHION_CONTACT_OFFSET_XY` for collision *times* in the event solver. Keep `bool pooltoolCompat` to switch it to R for cross-validation against pooltool, which uses R in plan view. Rounded jaws need the same treatment: a jaw arc of radius r_j is hit when the plan distance to its center is r_j + R_c.
3. **Cut-angle convention.** The WPA's 142° and 104° are *interior material angles*. Convert them with φ = π − C and β_corner = C − 3π/4 or β_side = C − π/2.
   - A common bug is treating 142° as the angle between the facing and the pocket axis, which produces diverging jaws.
   - pooltool approximates the side-facing slope with `sin(sa)` instead of `tan(sa)`; use `tan` (the difference is 0.06° at 7°).
4. **The mouth is between virtual points.** Rounding the jaws (r_j) does not change the WPA mouth value, but it narrows the effective opening near the points. Keep r_j a separate tuning knob; pooltool's 21 mm corner jaw radius is a tuning value, not a physical measurement.
5. **7-ft ambiguity.** Always select a preset explicitly. Never infer "7-ft" = 78 × 39 in. Unit tests that compare against pooltool must use `TABLE_7FT_78` and pooltool's pocket values.
6. **WPA PDF typos and lost ± signs.** The document shows "+ 1/8" and "(+ )" where ± is meant, and "2 inches [5.40 cm]" where it means 5.08 cm. Use the corrected values above.
7. **Per-ball properties.** Never hard-code a single R or m in the physics. Ball–ball contact normals must be computed in 3D whenever radii differ, because an oversized cue ball hits off the equator.
8. **Symmetry.** Build one corner and one side pocket, then mirror them; do not hand-type 6 pockets. Test mirror symmetry (T-GEOM-6).
9. **Units and precision.** Inches convert exactly by 0.0254. Keep everything in `double`. Convert degrees to radians once, in the preset.
10. **Rack lattice.** Frozen racks create exactly simultaneous contacts, which break naive event solvers. The physics spec must either handle simultaneous events or apply `RACK_GAP_*`. This spec only provides the ideal lattice.
11. **Template.** Do not add the 0.14 mm template to the collision world. It is a visual/rules object.
12. **Art/physics consistency.** The render mesh's nose line, jaw points and slate cut must come from the *same* `BuildGeometry()` output. Export it to the DCC (JSON/CSV), rather than letting artists model pockets by eye.
13. **Bar pocket values are estimates.** Valley cut angles and shelves are based on forum descriptions. Measure a real bar box with the TDF method (mouth, throat 2 in back, shelf) and update §5.2.
14. **Back draft.** Its direction is an INTERPRETATION (undercut facing). Confirm it on a real table before building the 3D pocket rubber or any 3D facing contact model.

---

## 13. Test cases (numerically checkable)

Tolerances are absolute unless stated. Values are from §3–§9. "9-ft" means `TABLE_9FT_PRO` and "7-ft" means `TABLE_7FT_BAR`.

| ID | Input | Expected output | Tolerance |
|---|---|---|---|
| T-UNIT-1 | `BALL_RADIUS` | 0.028575 m | 1e-12 |
| T-UNIT-2 | `BALL_INERTIA` for m = 0.17009713875 kg | 5.5555809e-5 kg·m² | 1e-11 |
| T-UNIT-3 | Oversized cue-ball mass at phenolic density | 0.200051 kg | 1e-5 |
| T-CUSH-1 | h = 0.635·D | 0.03629025 m | 1e-9 |
| T-CUSH-2 | θ_c = asin((h − R)/R) | 0.2733943 rad (15.66427°) | 1e-6 rad |
| T-CUSH-3 | R_c = sqrt(R² − (h − R)²) | 0.02751373 m; R − R_c = 0.00106127 m | 1e-8 |
| T-CUSH-4 | Oversized cue ball (R = 0.0301625), same h | θ_c = 11.7217°; h/D = 0.60158 | 1e-3° / 1e-5 |
| T-CUSH-5 | Ball frozen to `RAIL_LEFT` (9-ft, physical mode) | center y = 0.6074863 m (0.606425 in pooltoolCompat mode) | 1e-7 |
| T-GEOM-1 | 9-ft diamonds | 18 sights; long-rail x ∈ {±0.3175, ±0.6350, ±0.9525} at y = ±0.7286625; end-rail y ∈ {−0.3175, 0, +0.3175} at x = ±1.3636625 | 1e-9 |
| T-GEOM-2 | 7-ft diamonds | long-rail x ∈ {±0.254, ±0.508, ±0.762} at y = ±0.6016625; end-rail x = ±1.1096625 | 1e-9 |
| T-GEOM-3 | Spots | 9-ft: foot (0.635, 0), head (−0.635, 0); 7-ft: foot (0.508, 0), head (−0.508, 0) | 1e-12 |
| T-GEOM-4 | `IsAboveHeadString(x)` on the 9-ft table | x = −0.6351 → true; x = −0.6350 → **false** (the string itself is excluded) | exact |
| T-GEOM-5 | Sum of cushion nose segment lengths, 9-ft (WPA mouths, virtual points) | 2·(L − 2a − M_s) + 2·(W − 2a) = 2·(2.54 − 0.161645 − 0.127) + 2·(1.27 − 0.161645) = 6.719422 m | 1e-6 |
| T-GEOM-6 | Mirror symmetry | For each pocket element e, mirror_x(e) and mirror_y(e) exist among the elements | 1e-12 |
| T-POCKET-1 | 9-ft `POCKET_FOOT_LEFT` jaw points | P_L = (1.1891777, 0.635), P_E = (1.27, 0.5541777); \|P_L − P_E\| = 0.1143 | 1e-7 |
| T-POCKET-2 | 9-ft corner throat 2 in back | 0.0941884 m (3.7082 in) | 1e-7 |
| T-POCKET-3 | Mouth − throat vs. C (corner, 2 in back) | C = 141.7° → 0.7530 in; 142.6° → 0.8710 in; 145.3° → 1.256 in (matches the TDF table boundaries 3/4, 7/8, 1¼ in) | 0.005 in |
| T-POCKET-4 | 9-ft side throat 2 in back | 0.1016683 m (4.0027 in) | 1e-7 |
| T-POCKET-5 | Convergence β | corner C = 142° → 7.000°; side C = 104° → 14.000°; pooltool mapping: 5.3° ↔ C = 140.3° | 1e-9 rad |
| T-POCKET-6 | Jaw rounding r_j = 0.004 | corner tangent offset 0.0013773 m; side 0.0031252 m; corner center distance r_j/sin(71°) = 0.0042305 m | 1e-7 |
| T-POCKET-7 | Capture centers, 9-ft | corner FOOT_LEFT (1.302615, 0.667615); side LEFT (0, 0.704263) | 1e-6 |
| T-POCKET-8 | Capture centers, 7-ft | corner FOOT_LEFT (1.020552, 0.512552); side LEFT (0, 0.5725) | 1e-6 |
| T-POCKET-9 | A ball of D = 57.15 mm must fit through every throat of every preset | throat_2in > D for all presets (smallest: 9FT_TIGHT corner 0.087838 > 0.05715) | — |
| T-RACK-1 | 8-ball rack, 9-ft | 15 centers; apex (0.635, 0); 8-ball (0.7339867, 0); back row x = 0.8329734, y ∈ {0, ±0.05715, ±0.1143} | 1e-7 |
| T-RACK-2 | Frozen rack contacts | min pairwise distance = D, and exactly 30 touching pairs (8-ball rack) | 1e-12 / exact |
| T-RACK-3 | 9-ball, `RackAnchor::NineOnFootSpot`, 9-ft | 9-ball at (0.635, 0); 1-ball at (0.5360133, 0); last ball at (0.7339867, 0); 16 touching pairs | 1e-7 |
| T-RACK-4 | 9-ball, `RackAnchor::OneOnFootSpot`, 7-ft | 1-ball at (0.508, 0); 9-ball at (0.6069867, 0) | 1e-7 |
| T-RACK-5 | 10-ball rack, 9-ft | 1-ball at (0.635, 0); 10-ball at (0.7339867, 0); back row x = 0.7834801; 18 touching pairs | 1e-7 |
| T-RACK-6 | All rack balls stay inside the cushion noses with ≥ R_c clearance, on all presets | pass | — |
| T-RACK-7 | Tight-rack inner sides | 0.3275867 / 0.2704367 / 0.1802911 m | 1e-7 |
| T-WPA-1 | `ValidateWpa(TABLE_9FT_PRO)` | all fields pass | — |
| T-WPA-2 | `ValidateWpa(TABLE_7FT_BAR)` | fails exactly: playing-surface size (not 9-ft or 8-ft), corner mouth (0.123825 > 0.117475), side mouth (0.12065 < 0.127), corner cut angle (138° outside 142° ± 1°), side cut angle (100° outside 104° ± 1°), corner shelf (0.00635 < 0.0254). Passes: bed height (0.743 ≥ 0.74295), rail width, nose height, side shelf. | exact set |
| T-WPA-3 | `ValidateWpa` with h = 0.0355 m | fails the nose-height check (< 0.03571875) | — |
| T-CAL-1 (cross-spec, physics) | WPA rail-speed test: ball on the head spot, center hit, level cue, toward the foot spot, at the physics spec's defined "firm" speed, `CLOTH=WORSTED_860` | travels ≥ 4.0 and ≈ ≤ 4.5 table lengths (≥ 10.16 m path on 9-ft); z stays ≤ R + 1 mm (no jump) | calibration target |

---

## 14. Sources

**Primary sources:**

- **[WPA-EQ]** WPA, *Recommended Equipment Specifications* (effective Nov 2001), §2–19. https://wpapool.com/wp-content/uploads/2024/01/RECOMMENDED-EQUIPMENT-SPECIFICATIONS.pdf
- **[WPA-RULES]** WPA, *Rules of Play* (effective 2025-09-15), §2.1 table markings, §4.2/5.2/6.2 racks, 3.15. https://wpapool.com/wp-content/uploads/2026/01/2026.01.02-WPA-Rules.pdf
- **[WPA-REGS]** WPA, *Rule Regulations* (effective 2025-09-15), Reg. 4 (Ball Rack Template). https://wpapool.com/wp-content/uploads/2025/10/2025.09.15-WPA-Regs-NP.pdf
- **[BCA]** BCA pocket and equipment specifications as republished by: https://www.pooltablefeltcloth.com/cushions-supplies/billiard-congress-of-america-pocket-specs.html and https://dallaspooltable.com/equipment-specifications/

**Dr. Dave Alciatore:**

- **[DD-SIZES]** Table sizes. https://drdavepoolinfo.com/faq/table/sizes/
- **[DD-TDF]** Table Difficulty Factor. https://billiarduniversity.org/documents/BU_table_difficulty_factor.pdf ; https://drdavepoolinfo.com/faq/table/tdf/
- **[DD-POCKET]** Pocket effective size and center. https://drdavepoolinfo.com/faq/pocket/size-and-center/
- **[DD-BALLW]** Ball weight and size effects. https://drdavepoolinfo.com/faq/ball/weight/ and the article "Ball Weight and Size Difference Effects – Part I", Billiards Digest, Feb 2012 (includes the Valley bar-box measurements). https://drdavepoolinfo.com/bd_articles/2012/feb12.pdf
- **[DD-CONST]** Pool physics property constants. https://drdavepoolinfo.com/faq/physics/physical-properties/
- **[DD-ENDMASS]** https://drdavepoolinfo.com/faq/squirt/endmass/
- **[DD-DEFL]** https://drdavepoolinfo.com/faq/squirt/published-data/
- **[DD-CUELEN]** https://drdavepoolinfo.com/faq/cue/length/
- **[DD-WEIGHT]** https://drdavepoolinfo.com/faq/cue/weight/
- **[DD-TIP]** https://drdavepoolinfo.com/faq/cue-tip/size-and-shape/
- **[DD-TAPER]** https://drdavepoolinfo.com/faq/cue/tapers/
- **[DD-JUMP]** https://drdavepoolinfo.com/faq/cue/jump/

**pooltool:**

- **[PT-SPECS] / [PT-LAYOUT] / [PT-BALL]** pooltool source (E. Kiefl): https://github.com/ekiefl/pooltool/blob/main/pooltool/objects/table/specs.py , `.../table/layout.py` , `.../ball/params.py`
- Kiefl, E. (2024). *Pooltool: A Python package for realistic billiards simulation.* JOSS 9(101), 7301. doi:10.21105/joss.07301
- Algorithm write-up: https://ekiefl.github.io/2020/12/20/pooltool-alg/

**Manufacturers and retailers:**

- **[DIAMOND-PROAM]** https://diamondbilliards.com/pages/pro-am (7′ 40 × 80, 8′ 45 × 90, 9′ 50 × 100 in; outside dimensions; weights); the K-55 profile per dealer listing https://weststatebilliards.com/product/diamond-pro-am-pool-table-7-foot-black-prc-finish-4-1-2-pockets-ready-2/
- **[DIAMOND-SMART]** https://weststatebilliards.com/product/smart-pool-table/ (real cue ball, optical-density sensor, red-circle cue ball)
- **[VALLEY-ZD]** https://www.gameroomshop.com/products/valley-panther-zd-11x-led-93-pool-table-coin-operated-with-dba
- **[GA-LEGACY]** https://www.americansupersports.com/great-american-legacy-7-coin-operated-pool-table.html (93 × 53 in outside, 80 × 40 in playing)
- **[DYNAMIC]** https://www.dynamic-billard.de/de/billardtische/pool-billardtische/pool-billardtische-7ft-fuss/ (Dynamic II: K55 natural-rubber cushions; retailer text gives ≈ 200 × 100 cm for 7 ft, to be verified)
- **[CHAMP-K66]** Championship cushion height guide (K66/K55 nose heights vs. rail core angle). https://www.pooltablefeltcloth.com/cushions-supplies/championship-rail-rubber/mounting-guides-technical-specs/cushion-height-k66-k55.html
- **[BLACKLABEL-K]** https://blacklabelbilliards.com/blogs/blog/k-66-vs-k-55-cushions
- **[ARAMITH-CB]** https://aramith.com/cue-ball/ (cue-ball sizes and types)
- **[ARAMITH-SP]** https://www.billardpro.de/super-aramith-pro-pool-billiard-balls-572-mm (57.2 mm, 170 g, phenolic)
- **[OVERSIZE-CB]** https://gamesforfun.com/shop/billiards-pool/billiards-and-pool-table-accessories/billiard-balls-and-triangles/oversize-commercial-cue-ball/ (2⅜ in, 7.8 oz)
- **[DE-COINOP-CB]** https://best-sporty.de/product/spielkugelpoolaramith603mmmnzbillard/ and https://www.heiku.de/Weisse-Billardkugel-mit-Metallkern-572-mm-fuer-Billardtische-mit-Muenzmechanik (60.3 mm optical vs. 57.2 mm magnetic)
- **[SIMONIS]** https://iwansimonis.com/pool-cloth/
- **[CHAMP-TITAN]** https://seyberts.com/products/dark-green-titan-championship-cloth (retailer listing: 21 oz, 75/25, napped)
- **[PREDATOR-314]** https://predatorcues.com/products/predator-314-3-12-75-mm-low-deflection-pool-cue-shaft-for-radial-joint
- **[PREDATOR-AIR]** https://www.pooldawg.com/predator-air-jump-cue
- **[HOUSE-CUES]** https://sherwoodsport.co.uk/products/one-piece-value-snooker-and-pool-cues-48-52-and-57-inch ; https://www.cuesplus.com/store/one-piece-cues
- **[LIGHT-BAR]** https://blattbilliards.com/blogs/news/how-high-should-a-pool-table-light-be ; https://pearsoncues.com/blog/pool-table-light-height/
- **[CHALK]** https://billiardbeast.com/what-is-pool-chalk-made-of/

**Forum field reports** (clearly lower confidence):

- **[AZB-VALLEY-SIZE]** https://forums.azbilliards.com/threads/which-valley-is-the-7-footer.216978/ and https://forums.azbilliards.com/threads/2-table-sizes-5-difference-why.487686/
- **[AZB-VALLEY-POCKET]** https://forums.azbilliards.com/threads/pocket-size-on-valley-7ft-tables-used-in-apa-tournament.543018/
- **[AZB-DIAMOND]** https://forums.azbilliards.com/threads/diamond-league-vs-pro-cut-pockets-for-9ft-pro-am.528493/ and https://forums.azbilliards.com/threads/diamond-pockets-specs.551022/

**Physics background** (for the cushion contact angle hand-off):

- Han, I. (2005). *Dynamics in carom and three cushion billiards.* J. Mech. Sci. Tech. 19(4), 976–984.
- Mathavan, S., Jackson, M. R., Parkin, R. M. (2010). *A theoretical analysis of billiard ball dynamics under cushion impacts.* Proc. IMechE C 224, 1863–1873.
- Leckie, W., Greenspan, M. (2005). *An event-based pool physics simulator.* Advances in Computer Games (LNCS 4250).

---

## 15. Open questions (for the project owner)

1. **Setting:** is the dive bar a US bar or a German "Kneipe"? The choice changes the table (Valley 40 × 80 in with a magnetic cue ball vs. a Dynamic-style ~200 × 100 cm table, often with a 60.3 mm optically separated cue ball), and also the cloth color, chalk brand and props.
2. **9-ball rack anchor:** WPA 2025 text (9 on the foot spot) or the legacy convention (1 on the foot spot)? The rules spec should decide.
3. **Measure a real bar box** (mouth, throat 2 in back, shelf, facing angles, nose height, bed height) to replace the §5.2 ESTIMATEs, and confirm the back-draft direction.
4. **Napped-cloth drift:** do we simulate it (physics cost), or only render it?
5. **Short-cue / wall collision mechanic** (§8.3): in or out of scope for v1 gameplay?
