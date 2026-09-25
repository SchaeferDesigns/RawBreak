# RAW BREAK - Physics Spec: Collisions (ball-ball, ball-cushion, pockets, slate, special contacts)

| Field | Value |
|---|---|
| Spec ID | `physics-collisions` |
| Module | `BilliardsCore` (engine-agnostic C++20, double precision, no exceptions, no RTTI) |
| Scope | Ball-ball impulse model (throw, spin transfer), ball-ball event detection (quartic roots, touching/simultaneous contacts, the break), ball-cushion models (Han 2005, Mathavan 2010, generic 3D impulse) and their event detection, pockets (jaws, facings, drop edge, capture, rattles, hanging balls), slate/rail-top/off-table routing, persistent contacts and Zeno guards. |
| Depends on | `physics-motion-and-cue.md` (ball states, trajectory polynomials, state classifier A.8, slate impact C.3/C.4), `equipment.md` (table, cushion and pocket geometry, constants §11), `rules.md` (event log §3.3, tolerances §3.6). |
| Status | Draft v1.1 (2026-09-25): adversarially verified, corrections applied in place (see §12 Verification log) |

Every formula is plain text, in the same notation as `physics-motion-and-cue.md` (`x` = cross product, `.` = dot product, `w` = angular velocity). Anything not taken directly from a cited source is marked **DERIVED** (with a short derivation), **TUNING** (an engineering or gameplay choice to calibrate), or **ESTIMATE** (no primary data found).

---

## Decisions at a glance

| Topic | Decision |
|---|---|
| Ball-ball resolution | Rigid-body frictional impulse in full 3D vector form. The friction coefficient comes from Alciatore's speed-dependent fit `mu_b = a + b exp(-c s)`, with `e_b = 0.95` and a Coulomb cap plus a "stop-slip" cap. With `e_b = 1` the throw angles are **identical** to Alciatore TP A.14 (proven in §2.6 and tested). Unequal radii and masses are supported. |
| Ball-ball detection | Quartic `\|dp(tau)\|^2 - (R1+R2)^2 = 0`, solved only on the validity window `[0, tau_max]`. Roots are isolated by **recursive derivative isolation** (quadratic -> cubic -> quartic) and polished with safeguarded Newton. A root is accepted only where the pair is approaching. There is no closed-form Ferrari solver. |
| Touching clusters, the break, persistent contacts | Isolated two-ball hits use the analytic impulse. **Clusters** (three or more balls in or near contact, or a ball pressed against a cushion) and **Zeno chains** go to the **Compliant Local Integrator (CLI)**: Hertz contact (`K = 8.06e8 N/m^1.5`), Tsuji damping (constant restitution), regularized Coulomb friction, fixed `dt = 1 us`. The result is independent of processing order. Rack realism comes from micro-gaps (`RACK_GAP_*`). |
| Cushion, ball on cloth | **Mathavan et al. (2010)**, integrated with RK4 over the normal impulse (N = 200 steps). It includes cushion friction **and** table friction during the impact, with `mu_w = 0.14`, `mu_s = 0.2` and a speed-dependent `e_c(v_perp)`. |
| Cushion, airborne ball | **Generic 3D rigid impulse (GRI)** against the actual contact normal. The ball can be launched up and over the rail. |
| Han (2005) | Kept as a documented reference and "fast" option. Its known structural loss (`(1+e) cos^2(theta) - 1`) is explained in §4.4. |
| Pockets | **Geometric pocket, event-based (Level A)**: nose segments, rounded jaw arcs, facing segments (all use the cushion model), a capture cylinder whose front edge is the slate cut, analytic **pivot over the rounded drop edge**, and the liner wall. Rattles, lip-hangs and bounce-outs emerge from this geometry. pooltool's "enter circle -> teleport" model is rejected. Level B (CLI inside the pocket) is optional. |
| Slate | Impact resolution is `physics-motion-and-cue.md` C.3/C.4 (unchanged). This spec routes each landing to the right surface: cloth, shelf, pocket opening, cushion top, rail cap, or outside the table. |
| Off table | Center crosses the outer rail boundary, or the ball comes to rest on the rail top or frame, or the flight apex reaches the lamp. Events use the names in `rules.md` §3.3. |

---

## 0. Conventions and symbols

### 0.1 Frame (project-wide)

- SI units: m, s, kg, rad.
- Right-handed frame with its origin at the center of the table bed:
  - `+x` points along the length toward the foot (rack) end,
  - `+y` points across the width,
  - `+z` points up.
- The cloth is `z = 0`, so a resting ball's center is at `z = R`.
- Positive `w_z` is counter-clockwise seen from above. Right English (tip right of center) gives `w_z > 0`, as in `physics-motion-and-cue.md` §0.1.

### 0.2 Cloth contact-point velocity

Defined in `physics-motion-and-cue.md` §0.3 and repeated here because the cushion and slate models depend on its sign:

```
u = v + w x (-R z_hat) = v + R (z_hat x w)
u_x = v_x - R w_y ,  u_y = v_y + R w_x ,  u_z = v_z
```

Check: rolling toward `+x` means `w_y = v_x / R`, so `u_x = 0`.

### 0.3 Symbols used in this spec

| Symbol | Meaning | Unit |
|---|---|---|
| `R_i, m_i, I_i = (2/5) m_i R_i^2` | radius, mass and inertia of ball i (per ball; oversized cue balls exist, `equipment.md` §6) | m, kg, kg m^2 |
| `r_i, v_i, w_i` | center position, linear velocity and angular velocity of ball i (world frame) | m, m/s, rad/s |
| `n_hat` | ball-ball unit contact normal, **from ball 1 to ball 2**: `(r2 - r1)/\|r2 - r1\|` | 1 |
| `v_n` | normal approach speed, `(v1 - v2) . n_hat` (> 0 means approaching) | m/s |
| `s` | slip velocity of ball 1's contact material point relative to ball 2's | m/s |
| `s_t` | tangential part of `s` (perpendicular to `n_hat`); `t_hat = s_t / \|s_t\|` | m/s |
| `J_n, J_t` | normal and tangential impulse magnitudes (ball-ball) | N s |
| `e_b` | ball-ball coefficient of restitution (Newton) | 1 |
| `mu_b(s)` | ball-ball friction coefficient as a function of the tangential slip speed | 1 |
| `h` | cushion nose height above the cloth (`CUSHION_NOSE_HEIGHT`) | m |
| `theta_c` | cushion contact elevation angle, `sin(theta_c) = (h - R)/R` | rad |
| `R_c` | horizontal center-to-nose distance at contact, `R cos(theta_c)` | m |
| `X_hat, Y_hat, Z_hat` | cushion-local frame: `Y_hat` horizontal **into** the cushion, `Z_hat = z_hat`, `X_hat = Y_hat x Z_hat` (along the cushion) | 1 |
| `e_c(v_perp)` | ball-cushion restitution (energetic in Mathavan, Newton in GRI) | 1 |
| `mu_w` | ball-cushion sliding friction coefficient | 1 |
| `mu_s` | ball-cloth sliding friction coefficient (from the motion spec) | 1 |
| `P` | accumulated normal impulse at the cushion contact (Mathavan independent variable) | N s |
| `k_hat` | generic contact normal, **from the contact point to the ball center** | 1 |
| `tau` | local time of the current motion segment | s |
| `r_p, C_cap` | capture-circle radius and center of a pocket (`equipment.md` §5.3) | m |
| `r_j` | plan radius of a rounded jaw point | m |
| `r_d` | drop-point (slate edge) rounding radius | m |

### 0.4 Master parameter table

| Parameter | Default | Range | Source |
|---|---|---|---|
| `R` | 0.028575 m | WPA 57.15 mm diameter, tolerance +0.127 mm | WPA Equipment Spec §16 |
| `m` | 0.17009713875 kg (6 oz) | WPA 0.156-0.170 kg | WPA §16; `equipment.md` §11.1 |
| `g` | 9.80665 m/s^2 | - | `physics-motion-and-cue.md` A.9 |
| `e_b` | 0.95 | 0.92-0.98 | Alciatore "physical properties" FAQ; pooltool `e_b = 0.95` |
| `a_mu, b_mu, c_mu` | 9.951e-3, 0.108, 1.088 s/m | fit to Marlow's data (3 points) | Alciatore TP A.14 p.4; pooltool `AlciatoreBallBallFriction` |
| `k_cling` (friction multiplier) | 1.0 | 1.5 dirty balls, 2.5 cling/skid | Alciatore TP A.14 p.16 |
| `h` | 0.03629025 m (63.5 % of D) | 0.03572-0.03686 m (62.5-64.5 %) | WPA §7; `equipment.md` §4.1 |
| `theta_c` | 0.2733943 rad (15.664 deg) | 14.48-16.86 deg | DERIVED (`equipment.md` T-CUSH-2) |
| `R_c` | 0.0275137 m | 0.027347-0.027668 m | DERIVED |
| `e_c(v_perp)` | `clamp(0.97 - 0.035 (v_perp - 1.0), 0.60, 0.97)` (v_perp in m/s) | slope 0-0.07 s/m; 0.98 in Mathavan 2010 | DERIVED/TUNING (§4.8) |
| `mu_w` (cushion friction) | 0.14 | 0.10-0.30 (pooltool `f_c = 0.2`) | Mathavan et al. 2010 |
| `mu_s` (cloth, also used during cushion impacts) | 0.20 | 0.15-0.40 | Alciatore; Mathavan 2009/2010; motion spec |
| facing restitution / friction | `e_f = e_c(v_perp)`, `mu_f = mu_w` | ESTIMATE, tune per preset | §5.5 |
| facing contact on the shelf | plane, `theta = beta_v = 12 deg`, `s_f = 27.573 mm` | `beta_v` 12-15 deg | DERIVED (§5.3), WPA §9 back draft |
| drop-edge trigger radius `a_d` | `r_p + r_d` (66.76 mm corner, 69.26 mm side) | - | DERIVED (§5.3; WPA "shelf includes bevel") |
| `e_slate`, `h_min`, `N_max` | 0.6, 2 mm, 10 | 0.5-0.7, 1-5 mm | motion spec C.5 |
| Hertz stiffness `K` (ball-ball) | 8.06e8 N/m^1.5 | 8.0e8-1.2e9 | DERIVED from Marlow's data via Alciatore TP B.29 (§3.9.3) |
| Tsuji damping `alpha_T` | 0.03689 (`e_b = 0.95`); 0.05242 (`e_b = 0.93`) | - | DERIVED numerically (§3.9.3) |
| CLI step `dt_cli` | 1.0e-6 s | 0.5e-6 to 2e-6 s | DERIVED (e error 3e-5, §3.9.3) |
| cushion stiffness in CLI `k_c` | 1.0e6 N/m | 2e5-5e6 N/m | ESTIMATE (open question 3) |
| `ε_touch` (numerical contact) | 1e-9 m | - | TUNING |
| `v_rest` (inelastic threshold) | 2e-3 m/s | 1e-3-5e-3 m/s | TUNING |
| Zeno window | 8 contacts of the same pair within 10 ms | - | TUNING |
| max events per shot | 20 000 | - | TUNING (pooltool has `max_events`) |
| `RAIL_TOP_Z`, `RAIL_WIDTH_TOTAL` | 0.048 m, 0.1778 m (9-ft) | per preset | `equipment.md` §11.2 |
| lamp height | 1.016 m (movable) / 1.65 m (fixed) minimum; bar preset 0.84 m | per scene | WPA §15; `equipment.md` §11.1 |

---

## 1. Event types owned by this spec

| Internal event | Detected by | Emits to rules log (`rules.md` §3.3) |
|---|---|---|
| `BallBallContact(i, j)` | §3 quartic | `BallBall(a, b, t, n_hat, cutAngle)` |
| `BallNose(i, segment)` | §4.10 | `BallCushion(ball, segment, t, continuesInitialFreeze)` |
| `BallFacing(i, pocket, side)` / `BallJawArc(i, pocket, side)` | §4.10 / §5 | `BallJaw(ball, pocket, side, t, continuesInitialFreeze)` |
| `BallDropEdge(i, pocket)` | §5.4 | `BallPocketEnter(ball, pocket, t)` |
| `BallLiner(i, pocket)` | §5.4 | (liner contact; counts as a rail contact per `rules.md` §2.3) |
| `BallCaptured(i, pocket)` | §5.4 | `BallPocketed(ball, pocket, t)` |
| `BallExitsPocketZone(i, pocket)` | §5.4 | `BallPocketExit(ball, pocket, t)` |
| `BallRailTop(i)` | §6.2 | `BallRailTop(ball, t)` |
| `BallOffTable(i, reason)` | §6.3 | `BallOffTable(ball, reason, t)`, `BallExternalContact(ball, Lamp, t)` |
| `IslandStep` / `IslandExit` | §3.9 CLI | per-contact `BallBall`/`BallCushion` records (§3.9.6) |
| (landing / airborne) | motion spec C.2 | `BallAirborne` / `BallLand` |

After **every** resolution, the ball state is re-classified with the classifier in `physics-motion-and-cue.md` A.8. Any downward `v_z` of a ball that was on the cloth is first resolved by C.3 at the same timestamp (§2.5).

---

## 2. Ball-ball collision: frictional impulse model

### 2.1 Physics in one paragraph

The balls are hard phenolic spheres, so contact lasts only about 0.2-0.4 ms (Marlow; §3.9.3). The collision is therefore treated as instantaneous. It has two parts:

- A **normal impulse** along the line of centers, set by the restitution `e_b`.
- A **tangential friction impulse** at the contact point, opposing the relative slip of the two surfaces.

The friction impulse does three things. It deflects the object ball (OB) off the line of centers ("throw"). It changes both balls' spins ("spin transfer"). And it gives a small vertical impulse whenever the slip has a vertical component (follow/draw).

Throw has two sources, which one formula covers:
- **cut-induced throw (CIT):** slip from the cue ball's tangential velocity in a cut shot;
- **spin-induced throw (SIT):** slip from English.

Friction between balls depends strongly on sliding speed. Alciatore fitted Marlow's measurements with an exponential (TP A.14):

```
mu_b(s) = k_cling * ( a_mu + b_mu * exp(-c_mu * s) )        s = |s_t| in m/s
a_mu = 9.951e-3   b_mu = 0.108   c_mu = 1.088 s/m
mu_b(0) = 0.1180, mu_b(0.5) = 0.0726, mu_b(1) = 0.0463, mu_b(2) = 0.0222, mu_b(5) = 0.0104
```

(Fit points from Marlow's Table 10: 0.11 at 0.0707 m/s, 0.06 at 0.707 m/s, 0.01 at 7.07 m/s. The speeds are Marlow's values times `sin 45 deg` for his 45-degree setup; TP A.14 p.4.)

### 2.2 Kinematics at contact (general 3D, unequal balls)

```
d      = r2 - r1 ,   |d| = R1 + R2 (to tolerance) ,   n_hat = d / |d|
v_n    = (v1 - v2) . n_hat                       must be > 0 (approaching)
contact point on ball 1: r1 + R1 n_hat ; on ball 2: r2 - R2 n_hat
c1     = v1 + w1 x ( R1 n_hat)                   material-point velocity, ball 1
c2     = v2 + w2 x (-R2 n_hat)                   material-point velocity, ball 2
s      = c1 - c2 = (v1 - v2) + (R1 w1 + R2 w2) x n_hat
s_t    = s - (s . n_hat) n_hat ,   t_hat = s_t / |s_t|   (undefined if |s_t| < eps_v)
```

`n_hat` is **not** assumed horizontal. It tilts when one ball is airborne, or when the radii differ (an oversized cue ball's center sits higher, `equipment.md` §12 item 7).

### 2.3 Impulse equations

Let `P1` be the impulse on ball 1 and `P2 = -P1` the impulse on ball 2. Then:

```
k_n = 1/m1 + 1/m2                                   (normal inverse effective mass)
k_t = 1/m1 + 1/m2 + R1^2/I1 + R2^2/I2 = (7/2) k_n   (tangential inverse effective mass)   [DERIVED]

J_n   = (1 + e_b) v_n / k_n                         (= m (1 + e_b) v_n / 2 for equal masses)
J_stk = |s_t| / k_t                                 (impulse that just stops the slip; = m |s_t| / 7 for equal balls)
mu    = mu_b(|s_t|)                                 (evaluated once, at the pre-impact slip)
J_t   = min( mu * J_n , J_stk )                     (Coulomb cap vs. stop-slip cap)

P1 = -J_n n_hat - J_t t_hat
v1' = v1 + P1 / m1
v2' = v2 - P1 / m2
w1' = w1 + ( R1 n_hat) x P1 / I1
w2' = w2 + (-R2 n_hat) x (-P1) / I2  =  w2 + R2 (n_hat x P1) / I2
```

**DERIVED: `k_t`.** A tangential impulse `P` (with `P . n_hat = 0`) on ball 1 at `+R1 n_hat` changes the slip by:

```
ds_t = P (1/m1 + 1/m2) + R1 (dw1 x n_hat) + R2 (dw2 x n_hat)
R1 dw1 x n_hat = (R1^2/I1) (n_hat x P) x n_hat = (R1^2/I1) P
```

The same holds for ball 2, so `ds_t = k_t P`. For solid spheres `R^2/I = 5/(2m)`, which gives `k_t = (7/2) k_n`.

`ds_t` is **parallel to `P`**. The slip therefore shrinks along a fixed direction and cannot curve during the impact. The impulse-level Coulomb model with a stop-slip cap is thus exact for rigid spheres with constant `mu`. The only modelling approximation is that `mu` is frozen at the initial slip speed. This matches TP A.14.

**Properties to check in tests:**
1. Total linear momentum `m1 v1 + m2 v2` is conserved.
2. Total angular momentum about any fixed point, `sum (m r x v + I w)`, is conserved (the impulses are internal and equal-opposite on the common contact point).
3. Kinetic energy never increases.
4. Both balls receive the **same** `dw` for equal radii and inertias ("gear" coupling).
5. The spin component along `n_hat` is unchanged (point contact cannot transmit twist).

### 2.4 Algorithm (step by step)

```
ResolveBallBall(i, j):
 1. n_hat, v_n from §2.2. If v_n <= 0: return (not an impact; see §3.6).
 2. e = (v_n < v_rest) ? 0 : e_b            // micro-collision damping, §7.3
 3. J_n = (1+e) v_n / k_n
 4. s_t from §2.2; if |s_t| < eps_v: J_t = 0 else J_t = min(k_cling*mu_b(|s_t|)*J_n, |s_t|/k_t)
 5. apply P1 to both balls (§2.3)
 6. for each ball that was ON THE CLOTH before the event (state not Airborne):
      if v_z' < 0:  resolve slate impact (motion spec C.3) at the same timestamp
      else if v_z' < v_z_min (motion spec C.4):  v_z' = 0
 7. classify both balls (motion spec A.8); invalidate their cached events
 8. emit BallBall(a, b, t, n_hat, cutAngle) for the rules log
```

`cutAngle` (for `rules.md`) is the angle between the cue ball's pre-impact horizontal velocity and `n_hat`: `acos( v_cb_h_hat . n_hat_h )`, with 0 for a full hit.

### 2.5 Vertical impulses and the table (why step 6 exists)

The vertical slip comes from `(R1 w1 + R2 w2) x n_hat`. With follow, it points down at the contact point: the rolling cue ball's front surface moves downward. Friction then pushes the OB **down** and the CB **up**. The reverse holds for draw.

Test BB-2 gives the size: a rolling CB at 1 m/s hitting head-on gives the CB `v_z' = +0.045 m/s` (a 0.10 mm hop, suppressed) and presses the OB into the cloth with `v_z' = -0.045 m/s`.

Step 6 hands the downward part to the slate impact of the motion spec. That applies the cloth reaction plus a cloth friction impulse `mu_s (1 + e_slate) |v_z|`. This sequential "ball-ball then ball-table" treatment reproduces, to first order, the table-friction coupling that Mathavan et al. (2014) integrate simultaneously.

For an **airborne** cue ball hitting an OB from above (test BB-8), `n_hat` points down, and the OB gets a large downward component that goes straight into C.3. The OB may hop if `e_slate` times that component exceeds `v_z_min`.

### 2.6 Relation to Alciatore's closed-form throw (DERIVED, used as a test oracle)

Take Alciatore's frame: CB speed `v`, OB at rest, cut angle `phi`, spins `w_x` (roll) and `w_z` (side), `e_b = 1`, equal balls. Our model gives:

```
J_n / m = v cos(phi)
J_t / m = min( mu v cos(phi), v_rel / 7 )     v_rel = sqrt( (v sin(phi) - R w_z)^2 + (R w_x cos(phi))^2 )
OB tangential (horizontal) speed = (J_t / m) (v sin(phi) - R w_z) / v_rel
tan(theta_throw) = min( mu v cos(phi) / v_rel , 1/7 ) (v sin(phi) - R w_z) / (v cos(phi))
```

This is exactly TP A.14 eq. (15)-(17) and the MathCAD formula on p.5. For `e_b < 1`, the stop-slip branch becomes `tan(theta) = tan(phi) / (7 (1+e_b)/2)`, so throw grows slightly (1.4799 deg instead of 1.4430 deg at `phi = 10 deg`). The Coulomb branch is unchanged: `tan(theta) = mu`.

**Direction.** Throw deflects the OB from `n_hat` toward the CB's tangential direction of travel. Right English (`w_z > 0`) on a straight shot throws the OB to the **left** (`+y` when shooting along `+x`).

### 2.7 Spin transfer (what the model predicts)

`dw` is the same for both balls: `dw = -(5 J_t / (2 m R)) (n_hat x t_hat)`. The OB therefore receives spin **opposite in sense** to the CB's surface motion, like meshing gears:

- CB side spin gives the OB opposite side spin. The maximum is `5/14 = 35.7 %` of the CB's side spin if the slip stops; typically only 5-20 %, because friction is limited (BB-7: 17.7 % at 0.5 m/s).
- CB follow gives the OB a little draw, and CB draw gives the OB a little follow (BB-2: 11.3 % of `v/R`).

### 2.8 Higher-fidelity option (not baseline)

Mathavan, Jackson & Parkin (2014) integrate the ball-ball impulse together with cloth friction under both balls. pooltool offers it as `FRICTIONAL_MATHAVAN`. Its low-level `collide_balls()` has signature defaults `e_b = 0.89` and `u_b = 0.05`, but the resolver class passes the Alciatore friction (`AlciatoreBallBallFriction`) and the balls' own `e_b` (default 0.95). The resolver uses 1000 iterations. Keep it as an optional A/B model behind the same interface. The baseline's step 6 already captures the leading table-friction effect.

**Pitfall seen in the wild:** a stop-slip spin update of the form `dw = -(5/14)(n x (v1 - v2)/R + w1 + w2)` changes the spin component **along `n_hat`**, which a point contact cannot do. Use `dw = -(5/(14R)) n_hat x s_t`, which equals `-(5/14)[ n x (v1-v2)/R + (w1+w2) - n (n . (w1+w2)) ]` (equal balls).

---

## 3. Ball-ball event detection

### 3.1 Trajectory polynomials

Every non-pocketed ball in a segment follows `r(tau) = C + B tau + A tau^2` (motion spec A.5, A.6, C.1). The coefficients `A = A2` are:

| State | `A` |
|---|---|
| Sliding | `-(1/2) mu_s g u_hat0` |
| Rolling | `-(1/2) mu_r g v_hat0` |
| Airborne | `-(1/2) g z_hat` |
| Spinning, Stationary | `B = A = 0` |

Each segment is valid until its own transition time `tau_end` (motion spec A.8).

For a pair, re-expand both polynomials to a **common origin** `t_ref`, the later of the two segment start times: `C' = C + B dt + A dt^2`, `B' = B + 2 A dt`, `A' = A`. Always re-expand **from the stored segment origin**, never cumulatively.

### 3.2 The quartic

```
dC = C2 - C1 ,  dB = B2 - B1 ,  dA = A2 - A1        (relative motion, ball 2 w.r.t. ball 1)
f(tau) = |dC + dB tau + dA tau^2|^2 - (R1 + R2)^2
       = a4 tau^4 + a3 tau^3 + a2 tau^2 + a1 tau + a0
a4 = dA.dA
a3 = 2 dA.dB
a2 = dB.dB + 2 dA.dC
a1 = 2 dB.dC
a0 = dC.dC - (R1+R2)^2
f'(tau) = 2 (dC + dB tau + dA tau^2) . (dB + 2 dA tau)       (> 0 separating, < 0 approaching)
```

**Validity window:**

```
tau_max = min( tau_end(ball 1), tau_end(ball 2), t_best - t_ref )
```

`t_best` is the earliest event already found in this pass. Using it for pruning is optional, but it gives large speed-ups for AI rollouts.

**Degenerate degrees.** Because `a4 = |dA|^2`, `a4 = 0` forces `dA = 0` and therefore `a3 = 0` as well. The exact cases are:
- `dA != 0`: quartic.
- Equal accelerations, `dA = 0` (e.g. both sliding with the same `mu_s g u_hat`, or both stationary) and `dB != 0`: quadratic (`a2 = |dB|^2 > 0`).
- `dA = dB = 0`: constant (no relative motion, never an event). An exact linear case cannot occur: `a2 = 0` with `dA = 0` means `dB = 0` and so `a1 = 0`.
- A cubic or linear polynomial can appear only after numerical trimming of tiny leading coefficients (§3.3). The isolation algorithm handles every degree natively.
- Never pass a zero leading coefficient to a quartic routine. pooltool PR #354 ("Fix degenerate ball-ball collision roots", **still open** on 2026-09-25) addresses exactly this bug.

### 3.3 Solver strategy (comparison and recommendation)

| Method | Pros | Cons | Verdict |
|---|---|---|---|
| Closed form (Ferrari / Descartes) | fastest in flops | catastrophic cancellation when roots cluster or coefficients span many decades; near-tangency (glancing) misclassification; special-casing for degenerate degrees | **no** |
| Companion-matrix eigenvalues (QR) | robust general-purpose | ~10x slower, needs a linear-algebra kernel, returns complex roots needing imag-part thresholds (pooltool uses `atol = 1e-9`, `rtol = 1e-3`) | fallback / cross-check in tests only |
| Algorithm 1010 (Orellana & De Michele 2020) | accurate, fast, used by pooltool (with a loosened `d2` threshold for Newton's-cradle cases) | complex, all-roots solver, still needs real/positive filtering | acceptable alternative |
| **Recursive derivative isolation + safeguarded Newton, on `[0, tau_max]` only** | real roots only, no imaginary thresholds, exact tangency test via critical points, handles degree 0-4 uniformly, cost bounded, deterministic | slightly more code | **recommended** |

**Recommended algorithm (DERIVED; standard numerical-analysis technique):**

```
RealRootsIn(poly p of degree n, [lo, hi]):
  trim leading zeros (|coef| <= 0 exactly, or <= 1e-14 * max|coef| after scaling, see 3.4)
  if n <= 2: closed form with the stable quadratic formula
             q = -1/2 (b + sign(b) sqrt(b^2 - 4ac)); roots q/a and c/q
             keep roots in [lo, hi]
  crit = RealRootsIn(p', [lo, hi])              // critical points, sorted
  knots = [lo] + crit + [hi]
  for each consecutive pair (k0, k1):           // p is monotone on [k0, k1]
      if p(k0) == 0: record k0
      else if sign(p(k0)) != sign(p(k1)): record SafeNewton(p, k0, k1)
  if p(hi) == 0: record hi
SafeNewton(p, a, b): Newton from the midpoint; any step leaving the current bracket is
  replaced by bisection; the bracket shrinks each iteration; stop when |dx| <= 4 eps |x| or
  after 100 iterations (in practice 4-8).
```

The first acceptable contact is then:

```
FirstContact = smallest root tau* in [0, tau_max] with f'(tau*) < 0   (approaching crossing)
               plus the "already touching" rule of §3.6
```

- **Tangency (grazing).** At a double root, `f` touches zero at a critical point with `f' = 0`. The rule above rejects it; if `f(crit) > -eps_f`, it is a miss. That matches physics: a zero-normal-speed touch carries no impulse.
- **Cost.** A quartic evaluation is 4 FMAs. The full isolation is ~50-150 flops per pair.

### 3.4 Scaling and tolerances

- **Time scaling.** Substitute `tau = T s` with `T = tau_max` (or 1 s if `tau_max` is infinite). Work on `s` in `[0, 1]`, then map back. This keeps coefficient magnitudes comparable: `a4 ~ (mu_r g/2)^2 = 2.4e-3 s^-4` for rolling but `(g/2)^2 = 24 s^-4` for airborne.
- **Newton precision.** Polish to machine precision. A time error `dt` gives a distance error of about `v_n dt`. For `v_n >= 1e-3 m/s` and `dt <= 1e-12 s`, the separation error is `<= 1e-15 m`.
- `eps_f` (tangency) = `2 (R1+R2) * 1e-9 m` (i.e. 1 nm of separation).
- `ε_touch = 1e-9 m`: pairs with `|d| - (R1+R2) <= ε_touch` count as touching.
  - This is **numerical** contact.
  - The rules' `ε_frozen = 0.1 mm` (`rules.md` §3.6) is a separate, human-scale declaration threshold and must not be used by the physics.

### 3.5 Broad phase (optional, speeds up AI)

Skip the quartic when:

```
|dC_h| - (R1+R2) > reach1 + reach2
reach_i = |B_i| tau_max + |A_i| tau_max^2
```

Also skip pairs where both balls are Stationary or Spinning, or either ball is Pocketed or OffTable.

### 3.6 Avoiding re-detection and handling touching balls (t = 0 contacts)

After a resolution the pair is exactly (to rounding) at `|d| = R1 + R2`, so `f(0) ≈ 0` and a root at `tau ≈ +1e-15` appears. The **approach test** removes it:

- **Accept only crossings where `f' < 0`.** After an impact the pair separates (`f'(0) > 0`), so the root at 0 is rejected. A later true re-contact (e.g. a follow shot catching the OB again, or a massé double kiss) is still found, because it is a new downward crossing.
- **Already touching and approaching:** if `f(0) <= 2 (R1+R2) ε_touch` and `f'(0) < 0`, schedule the contact at `tau = 0`. This is the normal case inside a frozen rack, and for a CB frozen to an OB.
- **Touching and not approaching** (`f'(0) >= 0`): no event. This covers two resting frozen balls (`f' = f'' = 0`) and a pair moving apart. **Exception, pressing contact (below).**
- **Pressing contact (touching, zero normal speed, accelerating together).** The case is `f(0) <= 2 (R1+R2) ε_touch`, `|f'(0)| <= 2 (R1+R2) v_eps` (`v_eps = 1e-9 m/s`) and `f''(0) = 2 (dB.dB + 2 dA.dC) < 0`.
  - It arises after an inelastic (`e = 0`, §7.3) micro-impact, or when a ball frozen to another ball (or to a cushion) is driven into it by its own slip acceleration. Example: a CB frozen to an OB with topspin and no speed.
  - With the approach test alone this case produces **no** event: `f` starts at 0 with zero slope and goes negative, so there is no downward crossing. The balls silently interpenetrate until the overlap guard fires.
  - An impulse cannot resolve it, because the normal speed is 0. Schedule a contact at `tau = 0` flagged `pressing` and resolve it **in the CLI** (§3.9) as a two-body (or ball-cushion) island. Test D-12.
  - VERIFICATION NOTE: this rule was missing in draft v1. It was added by the verification pass (see the Verification log).
- **Overlap guard.** If `f(0) < -2 (R1+R2) * 1e-6` (more than 1 µm of overlap), the state is corrupt: log it, treat as touching, and do not move balls.
  - pooltool instead repositions both balls to `R1 + R2 + 1e-6 m` ("make_kiss") before every resolution.
  - We do not: the approach test makes it unnecessary, and moving balls breaks analytic continuity and AI reproducibility.

### 3.7 Simultaneous events and determinism

Events are ordered by the key `(t, typePriority, idA, idB)`, where `typePriority` is BallBall < BallNose/Facing/JawArc < DropEdge/Liner < slate/landing < motion transitions.

Two events count as **simultaneous** if `|t1 - t2| <= 1e-12 s`. Simultaneous events that share a ball are **not** resolved one after another: they form an island (§3.9).

Sources of simultaneity:
- geometric: rack lattice, symmetric setups;
- chained `tau = 0` contacts after an impulse (Newton's-cradle propagation).

The order key keeps replays, networking and AI search bit-identical. Also required: `/fp:precise`, no FMA contraction differences between platforms (motion spec pitfall 10).

### 3.8 What pooltool does (for reference)

- **Resolution.** Instantaneous pairwise impulses applied sequentially. The earliest event is `min()` over a cache dictionary, so exact ties resolve in insertion order: deterministic, but arbitrary.
- **Detection.**
  - Overlapping pairs are scheduled at the current time.
  - Before every ball-ball resolution, `make_kiss` places both balls at a separation of `R1 + R2 + 1e-6 m`: along their velocities if possible, otherwise along the line of centers.
- **Chains.** For balls that keep moving with nearly equal velocities (Newton's cradle), `resolve_continually_touching` lets the "chased" ball take 10 % of the chaser's line-of-centers momentum. It applies when the relative line-of-centers speed is below 0.01 m/s and the velocities are aligned (cosine > 0.9). This stops event explosions. The code comments call it phenomenological, not physics.
- **Safety cap.** A `max_events` limit stops all balls when exceeded.

### 3.9 Touching clusters and the break: sequential vs. simultaneous

#### 3.9.1 The problem

In a frozen rack, one impulse on the apex immediately produces contacts at `tau = 0` in several directions. Real balls are compliant (contact ≈ 0.3 ms, Hertzian force), so all these contacts are active **at the same time** and share the momentum.

Treating them as sequential instantaneous binary collisions (the hard-sphere, event-driven idealization) has two defects:
- **Order dependence.** A symmetric rack hit dead-center can come out asymmetric, depending on which tied contact is processed first.
- **Newton's-cradle artifacts.** Momentum passes through perfectly: CB -> two frozen OBs in line gives `v = (0, 0, 1)` for `e = 1`.

Müller & Pöschel (2011, "Three balls problem revisited") show that event-driven models cannot reproduce multi-contact outcomes; the result depends on the force law.

**Alciatore TP B.29** integrates Hertz contacts for the CB -> 2 frozen OBs case. It gets CB -0.071, OB1 +0.076, OB2 +0.995 (elastic): the CB bounces back slightly and OB1 creeps forward. Hard-sphere sequencing cannot produce this.

#### 3.9.2 Recommendation: Compliant Local Integrator (CLI) islands

**When a ball-ball event (i, j) happens at time `t_e`, build its island:**
1. Start from {i, j} and add every ball whose gap to an island ball is `<= delta_cl`, repeating until nothing more joins.
   - `delta_cl = max(ε_touch, 1.2 v_n T_H(v_n))`, where `T_H` is the Hertz contact time (§3.9.3).
   - A ball closer than this would be reached while the first contact is still active.
   - Example values: 0.40 mm at 1 m/s, about 2.1 mm at 8 m/s.
2. Also add any cushion segment, jaw arc or facing whose contact gap to an island ball is `<= delta_cl`. This is a "frozen on the rail" case.
3. If the island is exactly {i, j} with no cushion, use the **analytic impulse** (§2). This is the normal case: every isolated hit keeps the exact, fast and throw-accurate model.
4. Otherwise, run the CLI on the island.

**CLI model (per step `dt_cli = 1e-6 s`, semi-implicit Euler or velocity Verlet):**

```
Ball-ball normal:   delta = (Ri + Rj) - |rj - ri| ;  if delta > 0:
                    F_n = max(0, K delta^1.5 + eta delta^0.25 ddelta/dt),   eta = alpha_T sqrt(m* K),  m* = mi mj/(mi+mj)
Ball-ball friction: F_t = -mu0 F_n s_t / max(|s_t|, s_reg),  s_reg = 1e-3 m/s,
                    mu0 = k_cling mu_b(|s_t| at first touch of this pair)   (frozen per contact, as in §2.3)
Ball-cushion:       delta = (R + r_n) - dist(center, nose line / arc)  along the contact normal k_hat (§4.1);
                    F_n = max(0, k_c delta + c_c ddelta/dt),  c_c chosen from e_c(v_perp at first touch):
                    zeta = -ln(e)/sqrt(pi^2 + ln(e)^2),  c_c = 2 zeta sqrt(m k_c);  friction mu_w (regularized as above)
Cloth:              balls with z = R stay on the cloth: v_z >= 0 enforced (table reaction absorbs downward forces);
                    cloth sliding friction -mu_s m g u_hat (tiny over ~1 ms; include for consistency)
Torques:            each contact force F at lever r_c (from the center to the contact) adds r_c x F to I dw/dt
```

**Joining and exit:**
- **Joining.** Non-island balls advance analytically. If one comes within `delta_cl` of an island ball, it joins. Contacts between island and non-island balls are tested every step with linear trajectories.
- **Exit.** Return to event mode when all three hold: no contact force has been active for 5 consecutive steps; every island pair (and ball-cushion pair) has `f' >= 0` (separating); and every island pair has a **geometric gap `>= 0`** (`delta <= 0`, no overlap). Then classify every ball (A.8).
  - The gap condition is needed because the clipped Tsuji force `max(0, K delta^1.5 + eta delta^0.25 ddelta/dt)` reaches 0 while the balls still overlap.
  - Verified numerically (2 balls, `e = 0.95`, `dt = 1 µs`): after 5 zero-force steps the residual overlap is 0.37 µm at 0.05 m/s, 1.0 µm at 0.3 m/s and 1.46 µm at 1 m/s. That would trip the 1 µm overlap guard of §3.6.
  - Keep integrating the free motion until the gap opens, which takes a few µs.
- **Safety.** If an island exceeds 50 ms of simulated time, fall back to sequential impulses with deterministic ordering and log it.

**Why Tsuji damping.** The Lankarani-Nikravesh form normalizes damping by the impact speed. That breaks for contacts that start at zero relative speed, which is precisely the frozen-rack case: the tested 3-ball chain collapsed to OB velocities (0.65, 0.65). Tsuji et al. (1992) damping, `eta delta^0.25 ddelta/dt`, gives a **speed-independent** `e` for Hertz contacts (verified numerically: `e = 0.9500` at 0.3, 1 and 5 m/s with `alpha_T = 0.03689`) and has no special case at zero speed.

#### 3.9.3 Hertz constants (DERIVED)

- **Stiffness.** Alciatore TP B.29 uses Marlow's ball data: `R = 1.125 in`, full-compression strain `3.49e-3` (so `delta = 2 R * 3.49e-3 = 1.9945e-4 m`) and force `F = 2270 N`. That gives `F = K delta^1.5` with `K = 2270 / (1.9945e-4)^1.5 = 8.0587e8 N/m^1.5`.
- **Contact time.** Classic Hertz, with `m* = m/2`: `T_H(v) = 3.2181 (m*^2 / (K^2 v))^(1/5)`. The constant is `2.9433 (5/4)^(2/5)`, from `T_H = 2.9433 delta_max / v` and `delta_max = (5 m* v^2 / (4 K))^(2/5)`; `2.9433 = (4/5) B(2/5, 1/2)`. This gives 378 µs at 0.5 m/s, 329 µs at 1 m/s, 238 µs at 5 m/s and 207 µs at 10 m/s. Marlow reports 284 µs, the same order of magnitude. Matching 284 µs at 1 m/s would need `K ≈ 1.16e9`, hence the stated range 8e8-1.2e9.
- **Peak force.** 952 N at 1 m/s (max compression 112 µm) and about 15 kN at 10 m/s (705 µm).
- **Tsuji `alpha_T(e)`.** Found by bisection on a 2-ball Hertz collision: 0.03689 for `e = 0.95` and 0.05242 for `e = 0.93`.
- **Step size.** `dt_cli = 1 µs` gives an `e` error of 3e-5; 5 µs gives 3e-4.

#### 3.9.4 What this buys (numbers from the reference integrator, head-on chains, v_CB = 1 m/s)

| Setup | Sequential hard-sphere (`e = 0.95`) | CLI (Hertz + Tsuji, `e = 0.95`) |
|---|---|---|
| CB -> 1 OB | (0.025, 0.975) | (0.025, 0.975) |
| CB -> 2 frozen OBs | (0.025, 0.024, 0.951) | (-0.0547, 0.1002, 0.9546) |
| same, elastic (`e = 1`) | (0, 0, 1) | (-0.0710, 0.0764, 0.9945) = TP B.29 |
| CB -> 4 frozen OBs | Newton's cradle | (-0.0550, -0.0179, -0.0022, 0.1505, 0.9246) |
| CB -> OB1, 10 µm gap -> OB2 | as above | (-0.0300, 0.0742, 0.9558) |
| CB -> OB1, 100 µm gap -> OB2 | as above | (0.0244, 0.0247, 0.9509) (almost sequential) |

The 10 µm and 100 µm rows show why **micro-gaps matter**. A gap of a tenth of a millimetre turns simultaneous behavior into sequential behavior. This is the physical reason a "tight rack" breaks differently from a loose one.

#### 3.9.5 Rack realism

The rack spawner applies per-contact gaps from `equipment.md`, `RACK_GAP_MEAN` and `RACK_GAP_JITTER` (0-0.2 mm, ESTIMATE). The gaps are drawn from the game layer's seeded RNG; the physics core itself stays deterministic.

Suggested presets (TUNING):

| Preset | Gap mean | Gap jitter |
|---|---|---|
| Tight template | 0.005 mm | ±0.005 mm |
| Good wooden rack | 0.02 mm | ±0.02 mm |
| Sloppy bar rack | 0.08 mm | ±0.08 mm |

Every contact below `delta_cl` goes to the CLI regardless of the gap, so the break always runs in the CLI. At 8-12 m/s the whole rack joins within about 2 ms. That costs 2000 steps x about 40 contacts = 8e4 contact evaluations, each with 2 square roots and about 60 flops, **on the order of 1 ms** of CPU in C++ (ESTIMATE; benchmark it before relying on it for AI break rollouts).

#### 3.9.6 Event records from islands

For the rules log, the CLI emits a `BallBall` record the first time a pair's contact force becomes positive. The record is time-stamped at that step and carries the contact normal. Cushion and jaw contacts are recorded the same way. When a pair separates by more than `ε_leave`, it is re-armed, so a second contact produces a second record.

---

## 4. Ball-cushion collision

### 4.1 Geometry

- **Nose line.** The cushion is modelled by its nose line at height `h`, running along the rail between its end points (`equipment.md` §5.3). The optional nose profile radius `r_n` is 0 in physics; the 1 mm radius in `equipment.md` is for art only.
- **Contact of a ball on the cloth.** The contact point `I` is at height `h`, above the ball's equator:

  ```
  sin(theta_c) = (h - R)/R = 0.27  ->  theta_c = 15.664 deg
  cos(theta_c) = 0.96286
  horizontal center-to-nose distance at contact: R_c = R cos(theta_c) = 27.514 mm   (1.061 mm less than R)
  vector from center to I:  r_I = R (cos(theta_c) Y_hat + sin(theta_c) Z_hat)
  contact normal (I -> center): k_hat = -(cos(theta_c) Y_hat + sin(theta_c) Z_hat)
  ```

- **Local frame.** For a nose segment with inward table normal `n_c` (horizontal, pointing into the playing area), use `Y_hat = -n_c`, `Z_hat = z_hat` and `X_hat = Y_hat x Z_hat`. This is a proper rotation about `z`, so `v` and `w` both transform as `(q . X_hat, q . Y_hat, q . Z_hat)`. Example: on `RAIL_LEFT` (`y = +W/2`) the local and world axes coincide.
- **Jaw arcs.** For a jaw arc (a circle of radius `r_j` at height `h`), use the same formulas with `Y_hat` = the horizontal unit vector from the ball center toward the arc center.

### 4.2 Contact-point velocities (DERIVED; they match Mathavan 2010 eq. 12-13)

```
At the cushion contact I (local components):
  s_X  = v_X + R w_Y sin(theta_c) - R w_Z cos(theta_c)        slip along the cushion
  s_S  = -v_Y sin(theta_c) + v_Z cos(theta_c) + R w_X          slip along S_hat = -sin(theta_c) Y_hat + cos(theta_c) Z_hat ("up the face")
  v_cI = v_Y cos(theta_c) + v_Z sin(theta_c)                   normal approach speed at I (the rotation term cancels)
At the cloth contact C:
  s_XC = v_X - R w_Y ,   s_YC = v_Y + R w_X                    (= the motion spec's u in local axes)
```

### 4.3 Where the rail-induced spin and the small z-component come from

The contact is above the center (`theta_c > 0`), so both the normal and the friction impulses have lever arms that produce the effects below.

- **Running English is induced.** A ball approaching at an angle slips along the cushion with `s_X > 0` for travel toward `+X`. Friction `P_X < 0` gives `dw_Z = -(R/I) P_X cos(theta_c) > 0`. In the local frame that is spin whose cushion-side surface moves backward, i.e. the ball "rolls" along the rail: running English. Alciatore describes this rail-induced English.
- **Follow is converted.** A rolling ball has topspin with `w_X = -v_Y/R` in local axes. Its slip up the face is `s_S = -v_Y (1 + sin(theta_c)) < 0`, so friction acts up the face (`+S_hat`). That impulse has a `+Z` component and raises `w_X`, which reduces the topspin.
  - After the bounce the ball leaves with residual forward spin, which is backspin relative to its new direction. It then slows during a short sliding phase.
  - Test M-5: for a rolling perpendicular hit, `R w_X` goes from -1.0 V0 to about -0.25 V0.
- **Vertical component.** The normal impulse `P k_hat` points down: `-P sin(theta_c)` in `Z`. For a ball on the cloth it presses the ball into the slate, and the cloth reaction cancels it. This is why cushions are built with `h > R`: they do not launch balls.
  - Friction up the face adds `+mu_w P cos(theta_c)` in `Z` at most. With `mu_w = 0.14` this can never exceed the downward part: that would need `mu_w > tan(theta_c) = 0.28`.
  - So in the constrained (Mathavan) model a ball on the cloth never leaves the table at a cushion. Real high-speed hops come from cushion deformation, which is not modeled (open question 4).
  - An **airborne** ball whose center is above `h` meets the nose **below** its equator. The normal then points **up**, and the ball can climb or jump the rail (§4.6, test G-1).

### 4.4 Han (2005) model

Han treats the cushion contact as one instantaneous rigid impact of a free ball at `I` (J. Mech. Sci. Tech. 19(4), 976-984). The version below is written from pooltool's implementation, in our local frame:

```
v_cI = v_Y cos(theta_c) + v_Z sin(theta_c)          (> 0)
P_N  = (1 + e_c) m v_cI                              normal impulse (effective mass m: impulse through the center)
s    = (s_X, s_S) from §4.2 ;  P_stk = (2m/7) |s|    (tangential inverse effective mass at a surface point: 1/m + R^2/I = 7/(2m))
if P_stk <= mu_w P_N:  (P_X, P_S) = -(2m/7) (s_X, s_S)          stick
else:                  (P_X, P_S) = -mu_w P_N (s_X, s_S)/|s|    slide
P (local) = ( P_X ,  -P_S sin(theta_c) - P_N cos(theta_c) ,  P_S cos(theta_c) - P_N sin(theta_c) )
v' = v + P/m
w' = w + (r_I x P)/I :
     dw_X = (R/I)(P_Z cos(theta_c) - P_Y sin(theta_c)) ,  dw_Y = (R/I) P_X sin(theta_c) ,  dw_Z = -(R/I) P_X cos(theta_c)
pooltool then discards the change of v_Z (the table absorbs it).
```

**DERIVED structural limitation.** Consider a frictionless stun ball on the cloth. The horizontal restitution is `v_Y'/v_Y = 1 - (1 + e_c) cos^2(theta_c)`, which is 0.715 for `e_c = 0.85` and `h = 0.635 D`. Even with `e_c = 1` it cannot exceed 0.854.

The cause: the vertical component of the normal impulse is "wasted" on a DOF that the table actually blocks. If the vertical DOF is constrained (rigid slate), the correct frictionless impulse is `P_N = (1 + e) m v_Y / cos(theta_c)`, which gives exactly `v_Y' = -e v_Y`.

This is why Han needs an `e` that is not physically meaningful to match rebound data, and why its rebound angles differ from Mathavan's by about 2.6-5.6 deg (§4.7 table).

### 4.5 Mathavan, Jackson & Parkin (2010) model (baseline for balls on the cloth)

**Assumptions** (Proc. IMechE C 224(9), 1863-1873):
- rigid cushion and rigid slate; the ball stays on the slate (`v_Z = 0` throughout);
- Coulomb friction at the cushion contact `I` (`mu_w`) and at the cloth contact `C` (`mu_s`);
- the table normal impulse follows from the vertical balance.

The independent variable is the accumulated cushion normal impulse `P`, which increases monotonically. Integrating over `P` rather than time is the Stronge approach: it needs no stiffness.

With `sθ = sin(theta_c)`, `cθ = cos(theta_c)`, `k_w = 5/(2 m R)`, and slip directions `(cφ, sφ) = (s_X, s_S)/|s_I|` at `I` and `(cφ', sφ') = (s_XC, s_YC)/|s_C|` at `C`:

```
table normal impulse rate:   dP_C/dP = sθ + mu_w sφ cθ
dv_X/dP = -(1/m) [ mu_w cφ + mu_s cφ' (sθ + mu_w sφ cθ) ]
dv_Y/dP = -(1/m) [ cθ - mu_w sθ sφ + mu_s sφ' (sθ + mu_w sφ cθ) ]
dv_Z/dP = 0
dw_X/dP = -k_w   [ mu_w sφ + mu_s sφ' (sθ + mu_w sφ cθ) ]
dw_Y/dP = -k_w   [ mu_w cφ sθ - mu_s cφ' (sθ + mu_w sφ cθ) ]
dw_Z/dP =  k_w     mu_w cφ cθ
When |s_I| < s_eps: set the mu_w terms to 0 (rolling on the cushion; stick effects neglected, as in the paper).
When |s_C| < s_eps: set the mu_s terms to 0.   s_eps = 1e-6 m/s.
```

We re-derived every sign from `r x P` in the local frame (§4.2). All agree with the paper's eq. (14a-f) and with pooltool's implementation.

**Restitution** uses Stronge's energetic coefficient on the normal work at `I`:

```
dW/dP = v_cI = v_Y cθ                                          (v_Z = 0)
Compression: integrate from P = 0 until v_Y = 0; record W_c = integral of v_Y cθ dP  (> 0)
Restitution: continue until integral over the restitution phase of |v_Y| cθ dP = e_c^2 W_c
```

**Algorithm (DERIVED improvements over the paper's Euler scheme):**

```
MathavanResolve(v_local, w_local):                      // precondition: v_Y > 0, ball on cloth
  if v_Y < v_rest: v_Y := 0; return                     // resting/pressing contact, §7
  e  = e_c(v_Y)                                          // speed law §4.8, evaluated at the pre-impact normal speed
  dP = (1 + e) m v_Y / N,   N = 200                      // RK4 steps (the paper used N = 5000 Euler steps)
  y  = (v_X, v_Y, w_X, w_Y, w_Z)
  loop RK4(y, dP) while v_Y > 0; when a step would make v_Y <= 0, bisect that step (<= 60 halvings) to land on v_Y = 0
  accumulate W_c with the trapezoid rule on v_Y cθ
  loop RK4(y, dP) until W_r reaches e^2 W_c; bisect the last step on the work target
  optional accuracy upgrade: detect the zero crossing of |s_I| or |s_C| inside a step and split the step there
  return v' = (v_X, v_Y, 0), w'; the ball stays on the cloth -> classify (A.8)
```

**Accuracy** against an N = 20 000 reference (tests M-2 to M-4):

| Integrator | Max velocity error |
|---|---|
| RK4, N = 200 | 9e-5 m/s |
| RK4, N = 1000 | 2e-5 m/s |
| Euler, N = 1000 | 1e-4 m/s |

Convergence is only first order when a slip passes through zero, because the right-hand side is discontinuous there. With step splitting it becomes fourth order.

**Cost:** 200 RK4 steps x 4 evaluations x ~60 flops, about 50k flops or 10-20 µs per cushion hit in C++. That is negligible even for AI rollouts.

**Validation of our implementation against the paper.** With the paper's snooker setup (`M = 0.1406 kg`, `R = 26.25 mm`, `h = 7R/5`, `e = 0.98`, `mu_w = 0.14`, `mu_s = 0.212`), a rolling ball hitting perpendicularly rebounds at **0.9107 V0** for any V0 (test M-1).
- This agrees with the rigid-cushion rebound ratio of **about 0.910** that Mathavan et al. (2009) give for lower speeds.
- The 2010 paper itself identified `e = 0.98` and `mu_w = 0.14` jointly, by minimizing the RMS error against the measured rebound speeds for `V0 < 1.5 m/s` (its Fig. 7).
- Note: the paper states `h = 7R/5` "in both snooker and pool". For pool we use the WPA value `h = 1.27 R` instead (§0.4).

### 4.6 Generic 3D rigid impulse (GRI) - airborne balls and every other fixed obstacle

Use GRI for any contact of a **free** ball with fixed geometry at contact point `I`, with unit normal `k_hat` from `I` to the ball center:
- airborne ball vs. nose line, jaw arc, facing edge, liner or rail edge;
- a ball on the rail top;
- the slate (with `k_hat = z_hat` it reproduces motion spec C.3; test G-3).

```
r_I  = -R k_hat
v_I  = v + w x r_I
v_c  = -v . k_hat                                       must be > 0
P_N  = (1 + e) m v_c
s    = v_I - (v_I . k_hat) k_hat                        tangential slip
P_f  = -(2m/7) s               if (2m/7)|s| <= mu P_N   (stick)
     = -mu P_N s/|s|           otherwise                 (slide)
P    = P_N k_hat + P_f
v'   = v + P/m ,   w' = w + (r_I x P)/I
```

Han (2005) is exactly GRI with `k_hat` from §4.1 and `v_Z` discarded afterward.

**For airborne nose contacts** `k_hat` comes from the actual geometry:

```
k_hat = q/|q|
q     = the component of (center - P_a) perpendicular to the nose direction
```

`q` is the vector from the nose line to the center. When the center is above `h`, `k_hat` has `Z > 0` and the ball is thrown up (test G-1: 2 m/s straight in, with the center 10 mm above the nose, gives a 5.36 cm hop).

### 4.7 Recommendation

| Situation | Model | Why |
|---|---|---|
| Ball on the cloth hits a nose line, jaw arc or facing edge | **Mathavan 2010** (§4.5) | Physically constrained by the slate. Includes table friction during impact, which strongly shapes rolling-ball rebounds. Validated against measurements. Cheap enough. |
| Airborne ball, rail-top contacts, liner, slate edge | **GRI** (§4.6) | No slate constraint applies. Rigid impulse is the correct free-body limit. |
| Arcade/AI "fast" mode (optional) | Han (§4.4) with refitted `e_c(v)` | Closed form, but rebound angles are 3-6 deg too flat for rolling balls (table below). |
| Ball pressed into a cushion by another ball, frozen-on-rail kisses | CLI (§3.9) | Simultaneous 3-body contact |

Rolling ball at 1 m/s, pool geometry (`h = 0.635 D`), same friction `mu_w = 0.14`. Rebound angle is measured from the cushion line.

| Incidence (deg) | Mathavan (`e = e_c(v)`, `mu_s = 0.2`): speed / angle | Han (`e = 0.93`, `mu = 0.14`): speed / angle |
|---|---|---|
| 15 | 0.926 / 15.67 deg | 0.925 / 13.06 deg |
| 30 | 0.876 / 33.33 | 0.859 / 28.69 |
| 45 | 0.870 / 51.38 | 0.828 / 45.79 |
| 60 | 0.902 / 67.50 | 0.832 / 62.25 |
| 75 | 0.938 / 79.80 | 0.850 / 76.83 |

(These values were computed with a steeper `e_c` slope, `e_c(v) = min(0.97, 0.97 - 0.07 max(0, v - 1))`. For the 1 m/s cases above, the incident normal speed is at most 0.966 m/s, so the §0.4 default law gives the same `e_c = 0.97`.)

### 4.8 Parameters and the speed-dependent restitution (DERIVED + TUNING)

**Measured data.**
- Mathavan et al. (2009), snooker: a rolling ball hitting perpendicularly rebounds with `y = -0.0877 x^2 + 1.131 x - 0.0953` (m/s), for x from 0.28 to 3.5 m/s.
- The ratio `y/x` is about 0.91 at low speed and falls to 0.84 at 3 m/s and 0.80 at 3.5 m/s: the cushion deforms at high speed. The authors put the rigid-cushion limit at about 2.5 m/s normal speed.

**Fit.** Our Mathavan implementation was run with pool geometry, and `e_c` was solved for so that each measured ratio is reproduced:

| x (m/s) | 1.0 | 1.5 | 2.0 | 2.5 | 3.0 | 3.5 |
|---|---|---|---|---|---|---|
| `e_c` | 0.967 | 0.953 | 0.921 | 0.882 | 0.839 | 0.797 |

A straight line through these points is `e_c ≈ 0.97 - 0.07 (v_perp - 1)`.

**The pool-rail-speed requirement conflicts with this.** WPA §8 requires a firm center-ball shot from the head spot through the foot spot to travel at least 4-4.5 table lengths without jumping (`equipment.md` T-CAL-1). We ran a 1D reference (stun start, sliding -> rolling, perpendicular Mathavan bounces, motion-spec cloth). Number of 9-ft lengths travelled:

| `e_c` slope (per m/s) | cloth `mu_r` | v0 = 4 | 5 | 6 | 7 | 8 m/s |
|---|---|---|---|---|---|---|
| 0.07 (snooker fit) | 0.007 | 3.19 | 3.62 | 3.79 | 3.89 | 4.09 |
| 0.07 | 0.010 | 2.92 | 3.24 | 3.51 | 3.68 | 3.84 |
| **0.035 (default)** | 0.007 | 3.37 | 3.86 | **4.17** | 4.55 | 4.78 |
| **0.035** | 0.010 | 3.05 | 3.63 | 3.89 | **4.18** | 4.49 |
| 0 (constant 0.97) | 0.010 | 3.22 | 3.87 | 4.57 | 5.08 | 5.78 |

A rolling ball loses much more than `e_c` suggests at every perpendicular rail hit. It returns with residual forward spin (§4.3) and re-rolls at only about 0.61 of its approach speed: `(5/7)(0.951) - (2/7)(0.24)`.

**Default (TUNING):** `e_c(v_perp) = clamp(0.97 - 0.035 max(0, v_perp - 1 m/s), 0.60, 0.97)`. Here `v_perp = v_Y`, the horizontal normal approach speed.
- It matches the snooker data within 0.02 up to about 2 m/s.
- It meets WPA §8 for a "firm" 6-7 m/s stroke.
- The WPA does not define "firm". Define it as 6.0 m/s on the fast cloth preset for T-CAL-1, and confirm with a real K66 table (open question 2).

| Parameter | Default | Notes |
|---|---|---|
| `mu_w` | 0.14 | Mathavan 2010. pooltool's default is 0.2 (with Han). Range 0.1-0.3. |
| `mu_s` during impact | the cloth preset's `mu_s` | Ties the cushion to the cloth: fast cloth gives slightly livelier rolling rebounds. |
| `e_c` for GRI (airborne) | the same `e_c(v_c)` law, evaluated at `v_c` from §4.6 | ESTIMATE |

### 4.9 Oversized cue balls

`equipment.md` §4.1: a 60.3 mm bar cue ball meets the same nose at `theta_c = 11.72 deg`. Nothing changes in the formulas, because `theta_c` comes from the per-ball `R`. The ball is simply more prone to airborne rebounds, because the table-reaction margin `tan(theta_c) = 0.207` is smaller.

### 4.10 Cushion event detection

**Nose segment, ball on the cloth (quadratic).**
- Segment from `P_a` to `P_b`, unit direction `d_hat`, length `L_s`, inward normal `n_c`; ball center `p(tau) = C + B tau + A tau^2` with `z = R`.

```
sigma(tau) = n_c . (p_h(tau) - P_a,h)                     signed horizontal distance from the nose line
contact when sigma(tau) = R_c   (R_c = sqrt((R + r_n)^2 - (h - R)^2); = R cos(theta_c) for r_n = 0)
(n_c.A) tau^2 + (n_c.B) tau + (n_c.(C - P_a) - R_c) = 0
accept smallest tau in [0, tau_max] with  n_c.(B + 2 A tau) < 0  (approaching)  and  0 <= d_hat.(p(tau) - P_a) <= L_s
already touching at tau = 0: sigma(0) - R_c <= ε_touch and approaching -> event at 0
pressing at tau = 0: sigma(0) - R_c <= ε_touch, |n_c.B| <= v_eps and n_c.A < 0 (slip acceleration into the rail)
                    -> event at 0 flagged pressing -> ball-cushion CLI island (§3.6, §7.3)
```

In `pooltoolCompat` mode, use `R` instead of `R_c` (`equipment.md` §12 item 2).

**Jaw arc, ball on the cloth (quartic).**
- The arc has center `O` (height `h`), radius `r_j`, and angular extent between its two tangent points (`equipment.md` §5.3).
- The ball touches it when the horizontal distance from the ball center to `O` equals `r_j + R_c`.

```
g(tau) = |p_h(tau) - O_h|^2 - (r_j + R_c)^2     (quartic; same coefficient formulas as §3.2 with dA, dB, dC -> A_h, B_h, C_h - O_h)
accept approaching crossings (g' < 0) where the direction from O to p_h lies inside the arc's angular range
```

**Nose segment, airborne ball (quartic).** Project out the line direction: `Q = I - d_hat d_hat^T` (3x3).

```
q(tau) = Q (C - P_a) + Q B tau + Q A tau^2
|q(tau)|^2 - (R + r_n)^2 = 0                      quartic (as §3.2)
validity: along-segment coordinate in [0, L_s]; the ball center is on the table side, n_c . q >= 0.
          Otherwise the ball is over the rail: the contact is a rail-top contact (§6.2), not a nose contact.
```

**Jaw arc, airborne ball.** An exact sphere-vs-circle contact leads to a degree-8 polynomial. Approximate the arc by its center sphere of radius `r_j`: `|p(tau) - O|^2 = (R + r_j)^2`, which is quartic.
- The resulting error is about 0.13 mm for `r_j = 4 mm`. For a ball on the cloth, the horizontal contact distance becomes `sqrt((R + r_j)^2 - (h - R)^2) = 31.648 mm` instead of the exact `r_j + R_c = 31.514 mm`.
- This approximation is used only for airborne balls.
- Alternative: the degree-8 polynomial can be solved exactly by the same isolation solver (§3.3 is degree-agnostic). See the rim torus in §5.3.

**After resolution.** The approach test removes the root at `tau ≈ 0`, exactly as for ball-ball (§3.6). Store `continuesInitialFreeze` per `rules.md` §3.3: true while the ball has stayed within `ε_leave` of the segment it was frozen to at `t = 0`.

---

## 5. Pockets

### 5.1 Real geometry (from WPA and `equipment.md` §5)

**Jaws.** The mouth is measured between the virtual jaw points: 114.3-117.5 mm for corners and 127.0-130.2 mm for sides.
- Facing cut angles are 142 deg (corner) and 104 deg (side). Each facing therefore **converges** toward the pocket axis, by 7 deg (corner) or 14 deg (side).
- Throat, 2 in back from the points (DERIVED): 94.19 mm (corner) and 101.67 mm (side), for the 9FT_PRO preset.
- Facing length to the cushion back: 82.51 mm (corner) and 52.36 mm (side).

**Jaw points.** In reality they are rounded. The plan radius is `r_j = 4 mm` (ESTIMATE); pooltool uses tuned values of 20.95 mm (corner) and 7.95 mm (side).

**Facings.** Hard rubber, 1.6-6.35 mm thick.
- Back draft 12-15 deg, interpreted as **undercut**: the top edge protrudes, so a ball on the cloth touches the facing **face** at `R (1 + sin(beta_v))` = 34.5-36.0 mm, just below nose height, with the normal tilted down by `beta_v` (§5.3). A falling ball is pushed downward.
- WPA also requires the liner's upper wall to deflect balls downward.

**Shelf and drop.** The shelf is measured from the mouth-line midpoint to the vertical slate cut: 1-2.25 in for corners (preset 41.3 mm) and 0-0.375 in for sides (preset 4.8 mm). The slate's top edge at the cut is rounded with `r_d = 3.2-6.4 mm` (BCA; preset 4.8 mm).

**Capture region** (hand-off from `equipment.md` §5.3). A vertical cylinder of radius `r_p` (62.0 mm corner, 64.5 mm side). Its front edge lies on the vertical slate cut, and its center is `C_cap = Mid + (shelf + r_p) d_hat`. The slate's rounded top edge (radius `r_d`) lies on the shelf side of the cut, so the flat shelf ends at horizontal distance `a_d = r_p + r_d` from `C_cap`. That circle is the drop-edge trigger (§5.3, §5.4).

### 5.2 How pooltool models pockets and why that is less realistic

**pooltool's model:**
- A pocket is a circle `(a, b, r)` in the table plane.
- A ball on the cloth is "pocketed" the instant its **center** enters the circle (a quartic in `tau`). The `CANONICAL` resolver then teleports it to the pocket bottom with zero velocity.
- An airborne ball counts as pocketed if its landing point is inside the circle. Otherwise it counts only if it crosses the circle's cylinder with its exit height `<= 7R/5`, a heuristic standing in for "hit the back of the pocket and dropped".
- The jaws are separate linear and circular cushion segments, so balls can rattle between the points **before** reaching the circle.

**What this cannot reproduce:**
1. A ball that stops with its center just inside the circle but still supported by the slate would **hang on the lip**. Such a ball is always pocketed in pooltool: there is no support test.
2. A ball rolling over the shelf edge slowly pivots and drops; a fast one flies across the hole and hits the liner or the far facing. Both get identical instant capture.
3. There are no rattle-outs from the back of the pocket and no ball jumping out of the pocket. Once the center crosses the circle, the outcome is final.
4. The 7R/5 airborne heuristic replaces geometry (liner and facing heights) with one magic number.
5. There is no drop animation data. The renderer gets a teleport, which ruins the "real footage" goal for close-up camera shots.

### 5.3 Recommended model: event-based geometric pocket ("Level A")

All elements are exact geometry from `equipment.md` §5.3 (`BuildGeometry()`), so the art mesh and physics match.

| Element | Geometry | Contact model | Event detection |
|---|---|---|---|
| Nose segments | lines at `h`, ending at the jaw-arc tangent points | Mathavan / GRI (§4) | §4.10 (quadratic / quartic) |
| Jaw arcs | circle `r_j` at `h`, between its tangent points | Mathavan / GRI | §4.10 (quartic) |
| Facing faces | planes through the facing's plan line at height `h` (`equipment.md` §5.3), undercut by the back-draft angle `beta_v` (normal points into the pocket and down by `beta_v`) | **Ball on the shelf: Mathavan with `theta = beta_v`** (not `theta_c`), `e_f`, `mu_f`. Airborne: GRI | on the shelf: quadratic, center-to-plan-line horizontal distance `s_f = (R - (h - R) sin(beta_v)) / cos(beta_v)` = 27.573 mm (`beta_v = 12 deg`); airborne: plane distance = R (quadratic) |
| Facing top edges | the facing's upper edge, where it meets the cushion top (`equipment.md` §4.2 art profile) | GRI | airborne balls only, as the airborne nose (§4.10) |
| Shelf | slate surface outside the drop edge | cloth motion (motion spec) | - |
| Drop edge (rim) | the capture circle's front arc between the facings. The vertical slate cut is at radius `r_p` from `C_cap`. The top edge is rounded by `r_d`, and the rounding lies **on the shelf side** (WPA: "shelf includes bevel"). The rounding axis is the circle of radius `a_d = r_p + r_d` at `z = -r_d` | analytic pivot (§5.4) | quadratic/quartic: horizontal distance to `C_cap` = `a_d = r_p + r_d` (entering). This is where the rounding starts, **not** `r_p` |
| Hole wall + liner | vertical cylinder of radius `r_p` around `C_cap` (slate cut and liner assumed flush; ESTIMATE); inner surface undercut by `beta_l` = 12 deg. **On the front arc (between the facings) the wall exists only below the rim (`z < -r_d`).** | GRI with `e_l = 0.3`, `mu_l = 0.3` (leather/rubber liner, ESTIMATE) | quartic: horizontal distance to `C_cap` = `r_p - R` (from inside, moving outward); on the front arc accept only contact points with `z < -r_d` |
| Rim from inside (front arc) | the rounded rim as a torus: major radius `a_d`, minor radius `r_d`, center height `-r_d` | GRI with the torus normal | degree 8: with `Q = rho_h^2 + a_d^2 + (z + r_d)^2 - (R + r_d)^2`, solve `Q^2 - 4 a_d^2 rho_h^2 = 0` with `Q > 0` (`rho_h` = horizontal distance to `C_cap`; `rho_h^2` and `z` are polynomials in `tau`). The §3.3 isolation handles degree 8. Needed for balls rattling back toward the table below `z = R`. |
| Capture depth | `z_cap = -R`: the ball is entirely below the slate top | - | root of `z(tau) = z_cap` (quadratic) |

**DERIVED: why the facing is a plane contact for a ball on the shelf.** An undercut face tilted `beta_v` from vertical and passing through the plan line at height `h` touches a ball resting on the cloth at `z = R (1 + sin(beta_v))`. That is 34.52 mm for 12 deg and 35.97 mm for 15 deg, **below** `h = 36.29 mm`, so the contact point lies on the face.
- Contact happens at a center-to-line horizontal distance of `s_f = (R - (h - R) sin(beta_v)) / cos(beta_v)`: 27.573 mm for 12 deg. The ball meets the face before it could reach the edge-contact distance `R_c = 27.514 mm`.
- The contact normal is tilted down by `beta_v` (12 deg), not by `theta_c` (15.66 deg).
- Draft v1 modelled the on-shelf facing contact as an edge at `h` with `theta_c`. The difference is small (0.06 mm, 3.7 deg of normal tilt), but the plane model is the one consistent with `equipment.md` §5.1.
- Jaw arcs keep the edge-at-`h` model: they are the transition between the nose (edge) and the facing (plane). The small mismatch at the arc ends is below the art tolerance.

### 5.4 Pocket-entry state machine

```
OnCloth (event mode) --DropEdge event (center reaches horizontal distance a_d = r_p + r_d from C_cap, moving inward)-->
   emit BallPocketEnter(ball, pocket, t)
   v_perp := component of the horizontal velocity along the drop-edge normal at the crossing point (pointing into the hole)
   rho    := R + r_d                                   (pivot radius about the rounding axis: circle a_d at z = -r_d)
   if v_perp >= sqrt(g rho):   leave the edge immediately -> Airborne (ballistic)     [sqrt(g rho) = 0.5718 m/s for r_d = 4.8 mm]
   else:                       PIVOT macro-step (DERIVED, below), then Airborne from the leave state
Airborne-in-pocket --events: facing edge/face, jaw arc, liner wall, other balls, capture depth-->
   BallCaptured when z <= z_cap         -> Pocketed; emit BallPocketed; remove from all further detection
   or leaves the capture cylinder with z > R + eps and moves over the table -> normal airborne (C.2 landing on the shelf/cloth);
      emit BallPocketExit when its center is back outside the drop-edge circle a_d ("rattled out")
   (a ball moving back toward the table with its center below z = R meets the rounded rim from inside first: torus event, §5.3)
```

**PIVOT macro-step (DERIVED).** A ball rolling without slipping over a convex edge of radius `r_d` behaves as follows:
- Its center moves on a circle of radius `rho = R + r_d` about the edge-rounding axis, and its spin stays at `v/R`.
- Energy gives `(7/10) m (v^2 - v0^2) = m g rho (1 - cos(psi))`, where `psi` is the tilt from vertical.
- The radial balance gives `N = m g cos(psi) - m v^2/rho`.
- Setting `N = 0` gives the leave angle:

```
cos(psi_leave) = (10 + 7 v0^2 / (g rho)) / 17
v_leave        = sqrt( v0^2 + (10/7) g rho (1 - cos(psi_leave)) )
time to leave  T_p = integral_0^psi_leave  rho / v(psi) dpsi ,  v(psi) = sqrt(v0^2 + (10/7) g rho (1 - cos psi))
                Evaluate it with the substitution x = sin(psi/2), u = asinh( sqrt(b/a) x ),  a = v0^2,  b = (20/7) g rho:
                T_p = (2 rho / sqrt(b)) * integral_0^U du / sqrt(1 - (a/b) sinh^2(u)),   U = asinh( sqrt(b/a) sin(psi_leave/2) )
                (smooth integrand; Simpson with 16 panels gives <= 1e-4 relative error for v0 >= 1e-4 m/s)
                Do NOT use plain Simpson in psi: with 32 panels it is off by +466 % at v0 = 1e-4, +32 % at 1e-3 and -1 % at 0.01 m/s
                (the integrand peaks sharply at psi = 0). Enforce v0 >= 1e-4 m/s (T_p ~ ln(1/v0) diverges as v0 -> 0).
state at leave: center = edge axis + rho (sin(psi) n_e + cos(psi) z_hat), velocity = v_leave (cos(psi) n_e - sin(psi) z_hat)
                + the unchanged tangential component along the edge; spin rolling about the edge axis (|w| = v_leave/R)
```

Here `n_e` is the horizontal unit normal of the drop edge at the crossing point (toward the hole), and the "edge axis" point is the rounding axis: horizontal distance `a_d = r_p + r_d` from `C_cap` at `z = -r_d`. At `psi = 0` the center is exactly at the DropEdge event position, so there is no jump.

**DERIVED check of the trigger radius.** The flat shelf ends where the rounding begins, at `a_d = r_p + r_d`. A ball rolling on the flat touches the rounding when its center is directly above that line. Draft v1 triggered at `r_p` (the vertical cut), 4.76 mm too late, which contradicts the pivot geometry and moves the lip-hang threshold.

The pivot assumes rolling without slip up to the leave angle. Near `psi_leave` the normal force goes to 0, so the ball actually starts to slip slightly earlier, as for a sphere on a sphere. This affects only the animation timing (ESTIMATE, not modelled).

| `v0` | Leave angle `psi_leave` (`rho = R`) | `psi_leave` (`rho = R + 4.8 mm`) |
|---|---|---|
| 0 m/s | 53.97 deg | 53.97 deg |
| 0.1 m/s | 52.92 deg | 53.07 deg |
| 0.3 m/s | 43.91 deg | 45.45 deg |
| 0.5 m/s | 17.14 deg | 25.43 deg |

The ball leaves immediately if `v0 >= sqrt(g rho)`: 0.5294 m/s (`rho = R`) or 0.5718 m/s (`rho = R + 4.8 mm`).

During the pivot the ball can touch a facing or another ball. If such an event falls within `T_p`, truncate the pivot at that time. Then resolve the contact by GRI with the ball treated as free, and continue airborne. This is an approximation; exact resolution needs Level B.

### 5.5 What emerges from Level A (behaviors to verify in play-tests)

- **Rattle in the jaws.** A ball hitting a facing on the shelf rebounds (Mathavan, `e_f`) across to the other facing and can come back out. Rattle likelihood grows with approach speed, angle and facing liveliness, with deeper shelves (more shelf between the facings before the drop), and with higher facing convergence (7 deg vs. 3 deg bar pockets). All of these are real-world trends (Dr. Dave pocket FAQ; `equipment.md` §5.2 notes that tighter pockets use shallower shelves).
- **Hanging on the lip.** A ball whose center stops **outside** the drop-edge circle `a_d = r_p + r_d` stays on the slate: there is no creep and settling is not modeled (`rules.md` §3.4). A ball whose center reaches that circle **always** falls, even at `v0 -> 0`, because the pivot leave angle is 54 deg and there is no stable support past the start of the rounding. Physically the balance point is exact. Use `ε_touch` to classify a stopped ball exactly on the circle as not crossed.
- **Bounce-out from the back.** A fast ball flies over the hole, hits the undercut liner wall, and is redirected down (WPA §10). With `e_l = 0.3` it almost never comes back. Raising `e_l` and lowering `beta_l` reproduces "bar pocket spits the ball out" behavior. That is a preset knob (ESTIMATE).
- **Balls jammed in the jaws.** Two balls on the shelf between the facings, both with centers outside the circle, rest on the slate: `OnTable`. If a ball ends up with its center inside the circle but supported, which Level A cannot produce except with pocket-fill, report `SupportedOverPocket(pocket, supporters)` so the rules treat it as pocketed (`rules.md` §3.4).
- **Facing parameters (ESTIMATE).** `e_f = e_c(v_perp)` times `k_f`, with `k_f = 1.0` (9FT_PRO, hard thin facings) and 0.85 (7FT_BAR, thicker, softer facings); `mu_f = mu_w`. Calibrate by video: counts of rattle-out vs. drop for slow rolls into the corner along the rail at 1, 2 and 3 m/s.

### 5.6 Level B (optional, later): local integrator inside the pocket

If close-up drama needs it (a ball riding the rounded edge while touching a facing, two balls falling together, pocket-fill, ball-return gully visuals), run the pocket interior in the CLI (§3.9). The geometry is the same: sphere vs. the rounded slate edge (torus), facing faces and edges, the liner cylinder and other balls. Use `dt = 20 µs` for rigid-contact mode (sequential impulses with position projection, 4 iterations), and switch to Hertz only for ball-ball contacts.

Level B is not needed for rules or AI; Level A's outcome classification is sufficient.

---

## 6. Slate interactions, rail top, leaving the table

### 6.1 Landing routing (the landing event itself is motion spec C.2)

When an airborne ball's center reaches `z = R` at horizontal point `p_h`, check the following in order:

1. `p_h` inside a pocket's capture circle: there is no slate there. The ball continues falling in pocket mode (§5.4). There is no 7R/5 heuristic. A landing point in the rounded annulus `r_p < rho_h < a_d` meets the rim before `z = R`. The torus event of §5.3 must have fired earlier, so resolve it by GRI with the torus normal.
2. `p_h` on the playing surface or shelf: apply motion spec C.3/C.4 (`e_slate = 0.6`, `mu_s`, `h_min = 2 mm`, `N_max = 10`), then classify.
3. `p_h` behind a nose line (over the rail): the cloth is not there. The ball will meet a cushion top or rail cap plane before reaching `z = R`, and §6.2 must have produced that earlier event. Reaching step 3 means a missed event: log it.

Airborne balls also collide with noses (§4.10), jaw arcs and other balls (§3.2, 3D) while in flight. These are ordinary events scheduled by the same queue.

### 6.2 Rail-top contacts

The rail top is two planes (ESTIMATE; `equipment.md` §4.2 art profile):
- **Cushion top.** A sloped plane from the nose line (height `h`) back to the cushion back (`CUSHION_WIDTH` = 50.8 mm) at `RAIL_TOP_Z` = 48 mm. The slope is about 13 deg, and its normal leans toward the table.
- **Rail cap.** The flat plane `z = RAIL_TOP_Z` from the cushion back to the outer rail edge (`RAIL_WIDTH_TOTAL` from the nose).

**Detection.** A plane gives `n_pl . (p(tau) - x0) = R`, which is quadratic. Accept only if the center's horizontal position lies over that plane's strip.

**Resolution.** GRI with `k_hat = n_pl`, `e_rt = 0.5`, `mu_rt = 0.3` (cloth-covered wood, ESTIMATE). Emit `BallRailTop(ball, t)`.

**After contact:**
- Bounce height `< h_min` on the **sloped** cushion top: the ball will roll back toward the table. Hand it to the CLI (rigid mode, `dt = 20 µs`) until it either drops back over the nose (then event mode, airborne) or passes the outer edge.
- Coming to rest on the **flat** cap: `BallOffTable(ball, RestsOnRailOrFrame, t)` (WPA 2.6).
- A ball that touches the rail top and returns to the cloth or enters a pocket is **not** off the table (WPA 2.6). The rules module derives that; physics only reports the events.

### 6.3 Off-table conditions (physics side)

| Condition | Event |
|---|---|
| Center crosses the outer boundary `\|x\| > L/2 + RAIL_WIDTH_TOTAL` or `\|y\| > W/2 + RAIL_WIDTH_TOTAL` | `BallOffTable(ball, Floor, t)`; the ball is frozen out of the simulation (no floor physics) |
| Ball at rest on the rail cap or frame | `BallOffTable(ball, RestsOnRailOrFrame, t)` |
| Flight apex `z_max + R >= z_lamp` (scene lamp underside height; WPA ≥ 1.016 m movable / 1.65 m fixed above the bed; bar preset 0.84 m) and the apex point lies over the lamp's footprint | `BallExternalContact(ball, Lamp, t)`, then `BallOffTable(ball, ExternalObjectRebound, t)` (WPA 2.6: a ball that would have left the table but hit an object and came back is still off) |

The apex check is analytic: `z_max = z0 + v_z0^2/(2g)` at `tau = v_z0/g`. Stopping the ball there is simpler and rule-correct. Lamp bounce-back physics is not simulated. Player and furniture contacts belong to the game layer.

---

## 7. Other interactions and guards against infinite event loops

### 7.1 Ball resting against a cushion ("frozen to the rail")

- **Resting.** A stationary ball with `sigma = R_c` has `f' = 0`, so it generates no event.
- **Rolling along the rail.** A ball rolling parallel to and touching the rail has zero normal speed, so it generates no event either.
- **Spin-driven pressing.** If a sliding ball's slip-induced acceleration pushes it into the rail, it produces repeated tiny impacts. After an `e = 0` micro-impact (`v_Y := 0`), the normal speed is exactly 0 and the approach test alone would find **no** event, so the ball would sink through the nose. The pressing rule of §4.10 catches this at `tau = 0` and hands the ball to a ball-cushion CLI island. §7.3 is the backstop.
- **Hit by another ball.** An OB frozen to the rail and hit by the CB forms an island (§3.9: ball + cushion within `delta_cl`). It is resolved simultaneously in the CLI, which reproduces the "ball frozen on the rail" behavior: the OB is squeezed along the rail rather than bounced sequentially.

### 7.2 Frozen balls at shot start

A CB frozen to an OB and struck toward it gives a contact at `tau = 0` (§3.6, test D-7). The **stroke** itself (tip contact, possible push) is the cue spec's responsibility. Physics sees the CB velocity after the strike and resolves the contact at `t = 0+` (island if more balls are frozen).

The rules' `FrozenToCueBall` uses `ε_frozen = 0.1 mm`, which is independent of `ε_touch`.

### 7.3 Persistent contacts and Zeno chains (ball-ball and ball-cushion)

Four guards, in increasing order of severity:

1. **Approach test** (§3.6, §4.10): no event without a downward crossing of the contact function, **except** the pressing rule (touching, zero normal speed, `f'' < 0`). That rule goes straight to a CLI island, because an impulse cannot resolve a zero-speed contact.
2. **Micro-impacts.** If the approach speed is below `v_rest` (2 mm/s), use `e = 0` (inelastic). Chattering chains then end after a few events.
3. **Zeno detector.** If the same pair (ball-ball, or ball-segment) has **8 contacts within 10 ms**, move the involved balls (and segment) into a CLI island (§3.9). The island runs until contacts open or the balls come to rest. Examples: a CB chasing an OB with nearly equal velocity, or a ball pressed into the rail by spin.
4. **Hard cap.** 20 000 events per shot. If exceeded, stop all balls, log an error with the last 100 events, and report the shot as simulated with `aborted = true`. The rules layer can then decide on a replay; it should never happen in tests.

### 7.4 Airborne ball hitting another ball (3D)

This uses the same quartic (§3.2) with the airborne acceleration `-(1/2) g z_hat` (tests D-5, D-5b). The resolution is §2 with the 3D `n_hat`. The on-cloth partner's downward component goes to C.3 (§2.5).

A jumped CB that lands **on top of** an OB (`n_hat` nearly vertical) gets an upward impulse and can bounce. That is correct physics, and it is where a jump shot "fails" by landing on the ball.

### 7.5 Ball-ball contact with a pocketed ball

In Level A, pocketed balls leave the simulation, so `BallTouchesPocketedBall` can only come from Level B with pocket-fill. With a ball return, pockets never fill. With drop pockets (≥ 6 balls, WPA §11), note it as a Level B feature. The referee is responsible for emptying nearly full pockets (WPA 2.2).

---

## 8. Implementation notes & pitfalls

1. **One contact-normal convention per model.** Ball-ball uses `n_hat` from 1 to 2, with impulse `-J_n n_hat` on ball 1. GRI and cushions use `k_hat` from the contact to the center, with impulse `+P_N k_hat` on the ball. Unit-test the sign of every term against the rolling-ball cases (BB-2, M-2, M-5), which are sign-sensitive.
2. **Freeze `mu_b` at the pre-impact slip speed**, as Alciatore does. Re-evaluating it inside the impulse is a different model and breaks the TP A.14 oracle.
3. **Never let a ball-ball stop-slip update change the spin along `n_hat`** (§2.8 pitfall).
4. **Different radii:** compute `n_hat` in 3D from centers at different heights. Do not flatten it to the table plane (`equipment.md` §12 item 7).
5. **Quartic hygiene:**
   - evaluate in local time from a common origin, and re-expand from stored segment origins;
   - restrict the search to the validity window;
   - handle degree drops natively;
   - never threshold the imaginary parts of complex roots (our real-root isolation has none).
6. **No position "kissing".** Do not move balls to fix separations (pooltool's `make_kiss`). Rely on the approach test. If an overlap above 1 µm is ever detected, it is a bug upstream (missed event or wrong window): assert in debug builds.
7. **Cushion contact distance is `R_c`, not `R`**, for a ball on the cloth. The difference is 1.06 mm, which visibly shifts bank shots. Keep `pooltoolCompat` for cross-validation.
8. **Mathavan implementation:**
   - integrate in the local frame;
   - use `atan2`-free unit vectors, with zero friction when a slip is below `s_eps`;
   - bisect the steps where `v_Y` crosses 0 and where the work target is reached;
   - evaluate `e_c` once, at the pre-impact `v_Y`.
   
   The paper's Euler scheme with N = 5000 is 20x more work for similar accuracy.
9. **Snap after events.** Re-classify with motion spec A.8, including its snaps (for example `w_h := z_hat x v / R` when rolling). This prevents micro-sliding segments of about 1e-15 s.
10. **Determinism:**
    - the CLI uses fixed steps and a fixed iteration order (by ball id);
    - islands are built by sorted ball id;
    - the gap RNG lives in the game layer;
    - no parallel reductions inside the core.
11. **Island cost bound.** A full-rack break is at most about 16 balls x 30 contacts x 2000 steps. Keep contact lists sparse (a uniform grid with 60 mm cells), and never do O(N^2) per step for large islands.
12. **Throw and aim assist.** The AI should aim with the same `ResolveBallBall`; never hard-code "ghost ball" aiming. CLI and impulse throw agree within 0.25 deg (0.04-0.21 deg in CL-6; finite-duration geometry), so aiming through a cluster remains sensible.
13. **Event records for the rules module:**
    - `cutAngle` on `BallBall`;
    - `continuesInitialFreeze` on `BallCushion` and `BallJaw`;
    - `BallPocketEnter`, `BallPocketExit` and `BallPocketed` in order.
    
    The rules module never reads physics internals.
14. **Parameters as data.** All coefficients live in a `CollisionParams` struct selected by table and cloth preset. Tests pin every value explicitly, so default changes cannot silently alter them.
15. **Units at the Unreal boundary.** Handedness and scale conversions follow motion spec pitfall 11. Contact normals are ordinary vectors, while angular velocities are pseudovectors.
16. **Pressing contacts** (added by verification). The approach test alone misses contacts that start at zero normal speed but accelerate together (`f(0) ≈ 0`, `f'(0) ≈ 0`, `f''(0) < 0`). Examples: after `e = 0` micro-impacts, a frozen CB with pure spin, or a ball slip-pressed into a rail. Detect them with the pressing rule (§3.6, §4.10) and hand them to the CLI; never feed them to the impulse resolver, because a zero-speed impulse would loop forever at `tau = 0`.
17. **CLI exit needs geometric separation**, not just zero force (§3.9.2). Otherwise 1-1.5 µm residual overlaps trip the 1 µm corruption guard of §3.6.
18. **Stable quadratic edge cases in `RealRootsIn`.** Use `sign(0) := +1`. If `q = 0` (both `b = 0` and `c = 0`), the roots are `0` and `-b/a = 0`; do not divide `c/q`. Handle degree 1 (`-c/b`) and degree 0 (no root unless the polynomial is identically 0, which means touching: use the §3.6 rules) explicitly.
19. **Pocket rim radius.** The drop-edge trigger, pivot axis and lip-hang threshold all use `a_d = r_p + r_d` (§5.3). The capture cylinder `r_p` is only the hole wall and the capture test. Mixing the two moves the lip-hang threshold by `r_d = 4.76 mm`.

---

## 9. Test cases

Common constants, unless stated otherwise:

| Group | Values |
|---|---|
| Ball | `R = 0.028575 m`, `m = 0.17009713875 kg`, `g = 9.80665 m/s^2` |
| Ball-ball | `e_b = 0.95`, `mu_b` = Alciatore (`k_cling = 1`) |
| Cloth | `mu_s = 0.2`, `mu_r = 0.010` |
| Cushion | `h = 0.635 * 2R = 0.03629025 m` |

All ball-ball tests use the CB at `r1 = (0, 0, R)` moving along `+x` at speed `v`, and the OB at rest at `r2 = r1 + 2R (cos(phi), sin(phi), 0)`. `phi` is the cut angle, and the OB lies on the `+y` side.

Signed throw `theta` is the angle from `n_hat` to the OB's horizontal velocity, positive counter-clockwise about `+z`. Velocities are given **before** the table step of §2.4 step 6 unless stated.

### 9.1 Ball-ball resolution (BB)

| ID | Input | Expected | Tol |
|---|---|---|---|
| BB-1 | head-on stun, `v = 1`, `w = 0` | `v1' = (0.025, 0, 0)`, `v2' = (0.975, 0, 0)`, `w1' = w2' = 0` | 1e-12 |
| BB-2 | head-on rolling, `v = 1`, `w1 = (0, 1/R, 0)` | `mu = 0.0463351`, `J_n = 0.1658447 N s`, `J_t = 0.0076844 N s` (slide). `v1' = (0.025, 0, +0.0451767)`, `v2' = (0.975, 0, -0.0451767)`, `R w1' = (0, 0.8870582, 0)`, `R w2' = (0, -0.1129418, 0)`. After step 6: CB `v_z' -> 0` (hop 0.10 mm, suppressed). The OB goes through C.3 (`e_slate = 0.6`, `mu_s = 0.2`): `v_z = 0.027106 < v_z_min = 0.198057`, so it is set to 0. The cloth friction impulse gives OB `v_x = 0.960543`, `R w_y = -0.076800`. | 1e-6 |
| BB-3 | stun throw table, `e_b = 1` (must equal TP A.14) | `\|theta\|` (deg) for `v` = 0.447 / 1.341 / 3.129 m/s: at `phi = 10`: 1.4430 / 1.4430 / 1.4430 (all stick); at `phi = 30`: 4.7150 (stick) / 3.5491 / 1.6976; at `phi = 45`: 4.9451 / 2.7734 / 1.1273. Sign is negative (the OB is thrown toward the CB's travel direction). Oracle: `atan(min(mu v cos(phi)/v_rel, 1/7) (v sin(phi)) / (v cos(phi)))` | 1e-4 deg |
| BB-3b | same with `e_b = 0.95` | `phi = 10`: 1.4799 at all speeds; `phi = 30`, `v = 0.447`: 4.8353; all slide cases equal the `e_b = 1` values | 1e-4 deg |
| BB-4 | rolling cut, `v = 1.341`, `phi = 30`, `w1 = (0, v/R, 0)`, `e_b = 1` | `\|theta\| = 1.00422 deg` (TP A.14 with `w_x = w_roll`) | 1e-4 deg |
| BB-5 | straight stun with side spin, `phi = 0`, `R w1_z = +0.5 v`, `e_b = 1` | `theta = +4.0856 / +3.5491 / +1.6976 deg` for `v = 0.447 / 1.341 / 3.129` (right English throws the OB to `+y`). OB `R w_z'` = -0.07982 / -0.20793 / -0.23184 | 1e-4 |
| BB-6 | gearing outside English: `v = 1`, `phi = 30`, `R w1_z = v sin(phi)` | `J_t = 0`, `\|theta\| < 1e-9 deg` | - |
| BB-7 | spin transfer: head-on, `v = 0.5`, `R w1_z = 0.5` | OB `R w_z' = -0.0885258`, CB `R w_z' = 0.4114742` (equal `dw`) | 1e-6 |
| BB-8 | airborne CB on OB: `r1 = (0, 0, R + 0.02)`, `v1 = (2, 0, -0.5)`, `w1 = 0`; `r2 = r1 + (sqrt((2R)^2 - 0.02^2), 0, -0.02)` | `v1' = (0.117425, 0, 0.167983)`, `v2' = (1.882575, 0, -0.667983)` (before C.3), stick branch | 1e-6 |
| BB-9 | invariants over 10^5 random pairs (`v` in [0, 10], `w` in [-300, 300] rad/s, random 3D contact geometries, random `R`, `m` within the WPA/oversized ranges) | checked on the §2.3 impulse **before** step 6 (the table step adds external impulses): linear momentum and total angular momentum `sum (m r x v + I w)` about the origin conserved to 1e-12 relative; kinetic energy non-increasing (`dE <= 1e-12`); `w . n_hat` unchanged for each ball | - |
| BB-10 | mirror symmetry: mirror `y -> -y` (flip `w_x`, `w_z`) | the results mirror exactly | 1e-14 |

### 9.2 Detection (D)

| ID | Setup | Expected | Tol |
|---|---|---|---|
| D-1 | A rolling from `(0, 0, R)` at 1 m/s along `+x` (`A = -(1/2) mu_r g x_hat`); B at rest at `(0.5, 0, R)` | `tau = 0.4529079766 s`; `x_A = 0.44285 = 0.5 - 2R` | 1e-10 s |
| D-2 | as D-1 with B at `(0.5, 0.05, R)` | `tau = 0.4837978208 s` | 1e-10 |
| D-3 | as D-1 with B at `(0.5, 0.06, R)` (offset > 2R) | no root (miss) | - |
| D-4 | A stun at 2 m/s (sliding, `A = (-mu_s g/2) x_hat`); B rolling from `(1.0, 0.02, R)` at `(-1, 0, 0)` | window `tau_max = 0.2913475 s` (A's sliding end); real roots at 0.35996 and 0.40844 lie **outside** the window, so no event in this segment | - |
| D-5 | A airborne from `(0, 0, R)`, `v = (2, 0, 1.5)`; B at rest at `(0.1, 0, R)` | `tau = 0.0297042086 s`; `n_hat = (0.710264, 0, -0.703935)` | 1e-9 |
| D-5b | as D-5 with B at `(0.4, 0, R)` | no root (the ball jumps over B) | - |
| D-6 | degenerate: touching at `x = 0` and `2R`, `v1 = 0.025`, `v2 = 0.975`, both sliding along `+x` with equal acceleration | `a4 = a3 = 0`, `a0 = 0`, `f'(0) > 0`: **no event** (no quartic call, no zero-time loop) | - |
| D-7 | frozen and approaching: A at `(0, 0, R)` sliding at 1 m/s toward B at `(2R, 0, R)` | event at `tau = 0` (`f(0) = 0`, `f'(0) = -0.1143`) | exact |
| D-8 | cushion `RAIL_LEFT` (`y = +0.635`): ball rolling from the origin at `(0, 1, 0)` m/s | contact at `y = 0.6074863`, `tau = 0.6267471125 s` (quadratic) | 1e-9 |
| D-9 | jaw arc: `O = (0.3, 0.02)`, `r_j = 0.004`; ball rolling from the origin along `+x` at 1 m/s | `tau = 0.2794758674 s` (quartic; second root 0.32968 is the exit) | 1e-9 |
| D-10 | solver agreement: 10^6 random quartics from random ball pairs and states | the first approach root matches a companion-matrix and Newton-polished reference to 1e-12 s, or both report no root; grazing cases (`f_min` within `eps_f`) are allowed to disagree only as hit/miss with `v_n < 1e-6 m/s` | - |
| D-11 | re-detection guard: after BB-1, re-run detection for the pair | no event at `tau < 1 s` | - |
| D-12 | pressing contact (added by verification): CB at `(0, 0, R)` with `v = 0`, `w = (0, 10, 0)` rad/s (pure topspin, sliding, `A = +(1/2) mu_s g x_hat`), frozen to an OB at rest at `(2R, 0, R)` | `f(0) = 0`, `f'(0) = 0`, `f''(0) = -0.224180 m^2/s^2` -> contact event at `tau = 0` flagged `pressing`, routed to a 2-ball CLI island; **no** interpenetration beyond the Hertz overlap. Without the rule: zero events and a growing overlap (the bug this test guards) | exact (flags) |

### 9.3 Cushions (C, M, G)

All cushion tests are in local axes (`X` along the cushion, `Y` into it, `Z` up) with pool geometry `h = 0.635 D`. "Rolling" means `w = (-v_Y/R, v_X/R, 0)`.

| ID | Input | Expected | Tol |
|---|---|---|---|
| C-G1 | geometry | `sin(theta_c) = 0.27`, `theta_c = 15.664 deg`, `R_c = 0.02751373 m` | 1e-8 |
| C-H1 | Han, `e_c = 0.85`, `mu = 0.2`; rolling perpendicular, `v_Y = V` | `v' = (0, -0.811325 V, -0.137922 V)` (`Z` discarded on the cloth), `R w' = (-0.109354 V, 0, 0)`, slide; linear in V | 1e-6 |
| C-H2 | Han, stun perpendicular, `V = 1` | `v' = (0, -0.735964, -0.406671)`, `R w' = (0.192857, 0, 0)`, stick | 1e-6 |
| C-H3 | Han, rolling at 45 deg, `V = 1` | `v' = (0.528978, -0.553772, -0.168569)`, `R w' = (-0.261784, 0.586870, 0.428784)` | 1e-6 |
| C-H4 | Han frictionless check, `mu = 0`, stun perpendicular | `v_Y'/v_Y = 1 - (1 + e_c) cos^2(theta_c)`, which is -0.715135 for `e_c = 0.85` (`cos^2 = 0.9271`) | 1e-9 |
| M-1 | Mathavan with the paper's snooker values (`M = 0.1406`, `R = 0.02625`, `h = 7R/5`, `e = 0.98`, `mu_w = 0.14`, `mu_s = 0.212`); rolling perpendicular, `V0` in {0.5, 1, 2, 3} | `-v_Y'/V0 = 0.9107` for all `V0` (paper: measured low-speed gradient 0.910) | 0.002 |
| M-2 | Mathavan, pool; `e = 0.97` fixed, `mu_w = 0.14`, `mu_s = 0.2`; rolling at 45 deg, `V = 1` | `v' = (0.543143, -0.679940, 0)`, `R w' = (-0.273446, 0.588273, 0.400870)` | 2e-4 (RK4, N = 200) |
| M-3 | same parameters; stun at 30 deg, `V = 2`, `R w_Z = +1` (`w = (0, 0, 1/R)`) | `v' = (1.407479, -0.921805, 0)`, `R w' = (-0.162184, 0.147018, 1.503729)` | 2e-4 |
| M-4 | same parameters; 60 deg, `V = 3`, `R w = (1.299038, -0.75, -1.0)` (half draw plus side) | `v' = (0.675253, -2.366062, 0)`, `R w' = (0.160138, -0.446190, 0.332884)` | 2e-4 |
| M-5 | pool, `e = 0.98`, rolling perpendicular, any `V0` | `-v_Y'/V0 = 0.9599`, `R w_X' = -0.24558 V0` | 0.002 |
| M-6 | convergence: M-2 with N = 50 / 200 / 1000 RK4 | max error vs. N = 20 000: `<= 4e-4`, `<= 1e-4`, `<= 2e-5` m/s | - |
| M-7 | `e_c` law | `e_c(0.5) = 0.97`, `e_c(1) = 0.97`, `e_c(3) = 0.90`, `e_c(10) = 0.655`, `e_c(20) = 0.60` | 1e-12 |
| G-1 | GRI, airborne ball, center 10 mm above the nose line: `k_hat = (0, -0.936766, 0.349956)`, `v = (0, 2, 0)`, `w = 0`, `e = 0.85`, `mu = 0.2` | `v' = (0, -1.316846, 1.025631)`, `R w' = (-0.499938, 0, 0)`, stick; hop apex `v_z'^2/(2g)` = +0.053633 m | 1e-6 |
| G-2 | as G-1 with topspin `w = (-2/R, 0, 0)` | `v' = (0, -1.116871, 1.560925)`, `R w' = (-1.071366, 0, 0)` | 1e-6 |
| G-3 | GRI with `k_hat = z_hat` equals motion spec C.3: `v = (1, 0, -1)`, `w = (0, -20, 0)`, `e = 0.5`, `mu = 0.2` | `v' = (0.7, 0, 0.5)`, `R w' = (0, 0.1785, 0)` (slide) | 1e-9 |
| CAL-1 | WPA rail speed (`equipment.md` T-CAL-1): stun from the head spot through the foot spot, 9-ft, 1D reference, default `e_c` law | fast cloth (`mu_r = 0.007`), `v0 = 6.0 m/s`: 4.17 lengths (≥ 4.0); default cloth, `v0 = 7.0 m/s`: 4.18; no `z` excursion > 1 mm | ±0.05 lengths |

### 9.4 Compliant islands (CL)

`K = 8.0587e8 N/m^1.5`, `alpha_T = 0.03689`, `dt = 1e-6 s`, equal balls. The head-on chains start with all balls exactly touching, the CB moving at 1 m/s, and no friction or cloth (pure 1D).

| ID | Setup | Expected | Tol |
|---|---|---|---|
| CL-1 | 2 balls | `e = 0.9500` at 0.3, 1 and 8 m/s (speed-independent); final `(0.025, 0.975)` | 3e-4 |
| CL-2 | 3 balls, elastic (`alpha = 0`) | `(-0.07095, 0.07640, 0.99455)`; Alciatore TP B.29 gives -0.071 / 0.076 / 0.995 | 5e-4 |
| CL-3 | 3 balls, `e = 0.95` | `(-0.05469, 0.10011, 0.95458)` | 1e-3 |
| CL-4 | 5 balls, `e = 0.95` | `(-0.05501, -0.01794, -0.00215, 0.15049, 0.92460)`; momentum = 1 exactly; KE ratio 0.8809 | 2e-3 |
| CL-5 | 3 balls with a 100 µm gap between the OBs | `(0.0244, 0.0247, 0.9509)` (≈ sequential); with a 10 µm gap: `(-0.0300, 0.0742, 0.9558)` | 2e-3 |
| CL-6 | 2-ball stun cut in 3D with friction (`mu` frozen at first touch, `s_reg = 1e-3`), `e = 0.95` | throw within 0.25 deg of BB-3b: `v = 0.447`, `phi = 30`: 4.80 deg (impulse 4.84); `v = 1.341`: 3.45 (3.55); `v = 3.129`: 1.49 (1.70). The difference is the finite-duration rotation of `n_hat` | 0.25 deg |
| CL-7 | symmetric 15-ball rack (ideal lattice, zero gaps), CB at 8 m/s dead-center along `-x` into the apex | final velocities mirror-symmetric about `y = 0` to 1e-9 m/s; momentum conserved to 1e-12; KE non-increasing | - |
| CL-8 | order independence: CL-7 with the ball ids permuted | bit-identical results after mapping ids back | exact |

### 9.5 Pockets (P)

9FT_PRO preset (`equipment.md` §5.2): corner mouth 0.1143 m, cut angle 142 deg, shelf 0.041275 m, `r_j = 0.004`, `r_p = 0.062`, `r_d = 0.0047625`.

| ID | Setup | Expected | Tol |
|---|---|---|---|
| P-1 | geometry: throats and facing lengths | corner throat (2 in back) 0.0941884 m, side 0.1016683 m; facing lengths 0.08251 m and 0.05236 m | 1e-6 |
| P-2 | pivot leave angle and time | `v0` = 0, 0.1, 0.3, 0.5 m/s with `rho = R + r_d`: 53.968, 53.071, 45.445, 25.430 deg; immediate leave at `v0 >= 0.5717772 m/s`. Pivot time `T_p` (§5.4 substitution): 630.21 ms at `v0 = 1e-4`, 471.36 ms at 1e-3, 312.50 ms at 0.01, 201.29 ms at 0.05, 153.05 ms at 0.1, 73.71 ms at 0.3, 28.76 ms at 0.5 m/s | 1e-3 deg; `T_p` 1e-3 relative |
| P-3 | ball rolling at 1 m/s along the corner pocket axis (`FOOT_LEFT`) from 0.3 m out | no facing or jaw event (clearance to the facing contact distance `s_f` ≈ 24 mm per side at the drop edge); `BallPocketEnter` at the drop edge (`rho_h = a_d`), immediate leave (about 0.97 m/s > 0.572); then **one `BallLiner`** event: the horizontal path to the back wall is `a_d + r_p - R` ≈ 100 mm, reached after ≈ 0.10 s when the center has dropped only ≈ 49 mm (< 2R); then `BallPocketed` at `z = -R` | - |
| P-4 | lip hang | a ball rolled along the axis to stop with its center 0.5 mm **outside** the drop-edge circle `a_d = r_p + r_d` remains `OnTable` (stationary, not pocketed); one stopping 0.5 mm **inside** `a_d` falls (`BallPocketed`) after a pivot time `T_p` that is finite and computed with the §5.4 substitution. Rolling-stop distance `d = v0^2 / (2 mu_r g)`; e.g. `v0 = 0.0990285 m/s` stops in 0.05 m (`g = 9.80665`) | - |
| P-5 | slow rattle: ball at 1.5 m/s along the long rail, frozen to the rail (`sigma = R_c`), into the corner pocket | at least one `BallJaw` event; outcome logged. This is a behavior regression test: record the outcome once, then pin it | - |
| P-6 | airborne entry: a ball landing with its center inside the capture circle | no C.3 call; `BallPocketed` without any slate bounce | - |

### 9.6 Guards and off-table (Z, O)

| ID | Setup | Expected |
|---|---|---|
| Z-1 | two stationary touching balls; one stationary ball frozen to a cushion | zero events |
| Z-2 | a ball rolling exactly parallel to and touching `RAIL_LEFT` | zero cushion events until it stops |
| Z-3 | a sliding ball whose slip acceleration presses it into a cushion (`u_hat` with an inward component) | fewer than 12 events before the pressing rule (§4.10) or the Zeno island takes over; **the center never gets closer to the nose line than `R_c - 1 µm`**; the ball ends rolling along the rail or leaves it; the event count stays < 100 |
| Z-4 | hard cap: construct a pathological setup (debug hook) exceeding 20 000 events | `aborted = true`, all balls stopped, no hang |
| O-1 | ball launched with `v = (0, 3, 2.5)` from `(0, 0.5, R)` toward `RAIL_LEFT` (9-ft, `W/2 = 0.635`) | no nose contact: the center passes the nose line `y = 0.635` at `t = 0.045 s` with `z = 0.13115 m`; no rail-top contact; it crosses `y = W/2 + RAIL_WIDTH_TOTAL = 0.8128` at `t = 0.104267 s` with `z = 0.23593 m` -> `BallOffTable(Floor)` |
| O-2 | apex check: `v_z0 = 4.5 m/s` from the cloth, bar preset `z_lamp = 0.84 m`, lamp footprint covering the table | `z_max = R + 1.0325 m`, above the lamp -> `BallExternalContact(Lamp)` + `BallOffTable(ExternalObjectRebound)` |

---

## 10. Open questions

1. **Ball-ball restitution vs. speed.** Hertz with viscoelastic damping predicts a slow decrease with speed. No pool-specific data found; `e_b` is constant 0.95. Measure with high-speed video (head-on stun at 0.5-8 m/s).
2. **Cushion calibration on a real K66 pool table.** The only quantitative rail data we found is snooker (Mathavan 2009). We need incident/rebound speed pairs and rebound angles at 15-75 deg for rolling and stun balls, plus a definition of the WPA "firm" speed. The default slope (0.035 s/m) is a TUNING compromise between snooker data and WPA §8.
3. **Cushion and facing compliance for the CLI** (`k_c`) and **contact duration**: no measurements found. `1e6 N/m` is an ESTIMATE. It matters only for frozen-to-rail kisses and pressed contacts.
4. **High-speed cushion deformation.** Balls leaving the table off a rail on hard breaks need nose deformation (the ball sinks into the rubber and meets a lower effective contact point). This is not modeled. Candidate: make `h_eff(v_perp)` decrease with the normal speed above 2.5 m/s. That needs video data.
5. **Back-draft direction.** Undercut is an INTERPRETATION (`equipment.md` §12 item 14). Verify on a real table before relying on facing-face contacts.
6. **Pocket liner geometry and restitution** (`r_liner`, `e_l`, `beta_l`) for leather drop pockets vs. bar rubber cups: ESTIMATE. It matters for "ball spits out of the pocket" behavior.
7. **Rack gap distribution** (`RACK_GAP_*`). It shapes break outcomes decisively (§3.9.4). Ideally measure with a feeler gauge and photos of racks made with wooden racks vs. templates.
8. **Cloth friction during ball-ball contact** (Mathavan 2014 full model) vs. our sequential step-6 approximation. Quantify the difference in follow/draw-shot OB spin before deciding whether to adopt the full model.
9. **Chalk and cling events** (`k_cling` up to 2.5, TP A.14): should the game randomize cling occasionally, e.g. per ball contamination? This is a gameplay decision.
10. **"Firm" = 6-7 m/s is a hard stroke** (13-16 mph; Alciatore's "fast" is 7 mph = 3.1 m/s). If a real K66 table reaches 4 lengths at 4-5 m/s, the model is too lossy. With the default law we get only 3.6-3.9 lengths at 5 m/s.
    - The dominant loss is structural in Mathavan: a rolling ball keeps its forward spin through a perpendicular bounce and re-rolls at about 0.61 of its approach speed.
    - Calibrate before tuning `e_c` further: measure a perpendicular rolling-ball rebound (speed and spin) on the target cloth and cushion. (Added by verification.)
11. **Step 6 table friction for a pressed ball.** C.3 applies friction `mu_s (1 + e_slate) |v_z|` and then C.4 snaps the tiny hop to 0. This is up to 1.6x the table friction impulse that a simultaneous model (Mathavan 2014, ball kept on the table) would apply. The effect is small (BB-2: OB `v_x` 0.9605 vs about 0.966) but systematic. Decide together with question 8. (Added by verification.)
12. **Pocket rim contact from inside** (degree-8 torus event, §5.3) is specified but not yet reference-implemented or tested. Add a golden test when Level A is built: a ball bouncing off the liner back toward the table at `z < R`. (Added by verification.)

---

## 11. Sources

**Alciatore (Dr. Dave):**
- TP A.14, "The effects of cut angle, speed, and spin on object ball throw" (rev. 2025): throw derivation, the `mu = a + b exp(-c v)` fit with `a = 9.951e-3`, `b = 0.108`, `c = 1.088` from Marlow's data, and the cling multipliers. https://drdavepoolinfo.com/technical_proofs/new/TP_A-14.pdf (original host billiards.colostate.edu)
- "Throw - Part II: results", Billiards Digest, Sept. 2006: throw formula and experimental CIT data. https://drdavepoolinfo.com/bd_articles/2006/sept06.pdf
- TP B.29, "Simulation of a CB striking two frozen OBs along their line of centers" (2024): Hertz constant from Marlow's data; -0.071 / 0.076 / 0.995. https://drdavepoolinfo.com/technical_proofs/new/TP_B-29.pdf
- TP B.15, "Pocket geometry calculations" (2013): facing angle vs. mouth-throat difference. https://drdavepoolinfo.com/technical_proofs/new/TP_B-15.pdf
- Pool physics property constants FAQ: `e` ball-ball 0.92-0.98, ball-rail 0.6-0.9, ball-table 0.5-0.7; `mu` ball-ball 0.03-0.08. https://drdavepoolinfo.com/faq/physics/physical-properties/
- Pocket FAQ pages (size and center, rattle, point compression). https://drdavepoolinfo.com/faq/pocket/

**Other papers:**
- S. Mathavan, M. R. Jackson, R. M. Parkin, "A theoretical analysis of billiard ball dynamics under cushion impacts", Proc. IMechE Part C 224(9):1863-1873 (2010), doi:10.1243/09544062JMES1964. PDF mirror: https://drdavepoolinfo.com/physics_articles/Mathavan_IMechE_2010.pdf
- S. Mathavan, M. R. Jackson, R. M. Parkin, "Application of high-speed imaging to determine the dynamics of billiards", Am. J. Phys. 77(9):788-794 (2009): rail rebound polynomial, `e ≈ 0.818` average, cloth `mu_s` 0.178-0.245. https://drdavepoolinfo.com//physics_articles/ajp_09_hsv_article.pdf
- S. Mathavan, M. R. Jackson, R. M. Parkin, "Numerical simulations of the frictional collisions of solid balls on a rough surface", Sports Engineering 17:227-237 (2014).
- I. Han, "Dynamics in carom and three cushion billiards", J. Mech. Sci. Technol. 19(4):976-984 (2005), doi:10.1007/BF02919180. https://link.springer.com/article/10.1007/BF02919180 (equations taken via the pooltool implementation).
- W. Leckie, M. Greenspan, "An Event-Based Pool Physics Simulator", ACG 2005, LNCS 4250 (2006); "Pool Physics Simulation by Event Prediction 1/2", ICGA J. 28(4) 2005 / 29(1) 2006.
- A. G. Orellana, C. De Michele, "Algorithm 1010: Boosting Efficiency in Solving Quartic Equations with No Compromise in Accuracy", ACM TOMS 46(2), Art. 20 (2020).
- T. Müller, T. Pöschel, "Three balls problem revisited: On the limitations of event-driven modeling", Phys. Rev. E 83, 041304 (2011). https://arxiv.org/abs/1009.6153
- Y. Tsuji, T. Tanaka, T. Ishida, "Lagrangian numerical simulation of plug flow of cohesionless particles in a horizontal pipe", Powder Technology 71:239-250 (1992): Hertz contact with speed-independent restitution damping.
- H. M. Lankarani, P. E. Nikravesh, "A contact force model with hysteresis damping for impact analysis of multibody systems", J. Mech. Des. 112(3):369-376 (1990). Considered and rejected (§3.9.2).
- W. J. Stronge, "Impact Mechanics", Cambridge University Press (2000): energetic restitution; Hertz contact.
- W. C. Marlow, "The Physics of Pocket Billiards" (1995), cited via TP A.14, TP B.29 and Mathavan 2010.

**pooltool:**
- Kiefl, "The physics of pool/billiards" (2020): https://ekiefl.github.io/2020/04/24/pooltool-theory/
- Kiefl, "pooltool: the algorithm": https://ekiefl.github.io/2020/12/20/pooltool-alg/
- Resolver and model documentation: https://pooltool.readthedocs.io/en/latest/autoapi/pooltool/physics/index.html and https://pooltool.readthedocs.io/en/latest/resources/custom_physics.html
- Source (read 2026-09-25):
  - `physics/resolve/ball_ball/{friction.py, core.py, frictional_inelastic, frictional_mathavan}`
  - `physics/resolve/ball_cushion/{han_2005, mathavan_2010, core.py}`
  - `physics/resolve/ball_pocket`
  - `physics/resolve/ball_table/frictional_inelastic`
  - `evolution/event_based/detect/{ball_ball.py, ball_cushion.py, ball_pocket.py, quartic_coefficients.py}`
  - `ptmath/roots/{core.py, quartic.py, _quartic_numba.py}`
  - `objects/table/specs.py`, `objects/ball/params.py`, `constants.py`
  
  Repository: https://github.com/ekiefl/pooltool. Degenerate-root fix: PR #354, https://github.com/ekiefl/pooltool/pull/354

**Equipment and rules:**
- WPA, "Recommended Equipment Specifications": §7 cushion height 63.5 % ± 1 % of D; §9 mouths, facing angles 142° and 104°, back draft 12-15°, shelves; §10 liners; §11 drop pockets; §15 lights; §16 balls. https://wpapool.com/wp-content/uploads/2024/01/RECOMMENDED-EQUIPMENT-SPECIFICATIONS.pdf
- WPA, "Rules of Play" (effective 2025-09-15): 2.2 ball pocketed, hanging and supported balls; 2.6 driven off the table; 3.1 and 3.5 related fouls. https://wpapool.com/wp-content/uploads/2026/01/2026.01.02-WPA-Rules.pdf

**Project specs:** `Docs/specs/physics-motion-and-cue.md`, `Docs/specs/equipment.md`, `Docs/specs/rules.md`.

**Reference computations** for every numeric test value above were produced with a standalone double-precision Python reference: impulse models, Mathavan RK4, GRI, derivative-isolation root finder, and the Hertz-Tsuji chain integrator. Port it to the C++ test tree as golden-value generators.

---

## 12. Verification log

Adversarial verification pass on 2026-09-25, done independently of the original author.
- All numbers were recomputed with a fresh double-precision Python/NumPy reference (scripts in the verifier's scratchpad; port them together with the author's reference).
- Primary sources re-read: Alciatore TP A.14 (rev. 5/19/2025) and TP B.29 (PDF text); Mathavan et al. 2010 (IMechE, full text) and 2009 (AJP, full text); WPA Recommended Equipment Specifications (PDF text); Alciatore physical-properties FAQ; pooltool `main` source (`ball_cushion/han_2005/model.py`, `ball_cushion/mathavan_2010/model.py`, `ball_ball/friction.py`, `ball_ball/core.py`, `ball_ball/frictional_mathavan`, `detect/ball_pocket.py`, `objects/ball/params.py`) and PR #354.

### 12.1 Confirmed (no change needed)

| Item | Check | Result |
|---|---|---|
| Ball-ball impulse (§2.2-2.3) | Re-derived from linear momentum and angular impulse `r x P` with lever arms `+R1 n_hat` and `-R2 n_hat`. `ds_t = k_t P_t` with `k_t = (7/2) k_n` holds for any solid-sphere masses and radii. Normal and tangential impulses are decoupled, so the slip direction is fixed and Coulomb + stop-slip is exact for constant `mu`. | correct |
| BB-1 ... BB-8, BB-10 | recomputed | all digits reproduced (BB-2 incl. the C.3 step; BB-3/3b/4/5 equal the TP A.14 oracle to 1e-4 deg; BB-7 -0.0885258; BB-8 stick branch) |
| BB-9 | 2e4 random 3D pairs (unequal R, m) | momentum error 5e-15 relative, angular momentum 2e-14, `dE <= 0`, `w . n_hat` change 2e-13 rad/s absolute (rounding) |
| Alciatore constants | TP A.14 p.4: `a = 9.951e-3`, `b = 0.108`, `c = 1.088`, fitted to Marlow Table 10 (0.11/0.06/0.01 at 0.1/1/10 m/s times sin 45 deg); friction is a function of `v_rel`; throw eqs. (14)-(17), kinematic cap `m (v sin(phi) - R w_z)/7` (identical to our vector stop-slip); multipliers 1.5 (dirty) and 2.5 (cling), "up to 3" | match; pooltool `AlciatoreBallBallFriction` uses the same constants |
| FAQ values | e ball-ball 0.92-0.98, ball-rail 0.6-0.9, ball-table 0.5-0.7; mu ball-ball 0.03-0.08, cloth 0.15-0.4 | match |
| TP B.29 | `K = 2270 / (2 R 3.49e-3)^1.5 = 8.0587e8 N/m^1.5`; result -0.071 / 0.076 / 0.995 | match; CL-2 reproduced -0.07095 / 0.07640 / 0.99455 |
| CLI (CL-1 ... CL-6) | own Hertz + Tsuji integrator | `alpha_T` 0.03692 (e = 0.95) / 0.05246 (e = 0.93), integrator-dependent in the 4th digit; `e` = 0.95002-0.95006 at 0.3-8 m/s; `dt` 5 µs error 2.9e-4; CL-3, CL-4 (KE 0.8809), CL-5 (100 µm and 10 µm gaps) and CL-6 (4.80 / 3.45 / 1.49 deg) all reproduced within their tolerances |
| Mathavan eqs. (§4.5) | re-derived from the free-body impulses at I and C with the vertical constraint; compared term by term with paper eqs. (12)-(14) and pooltool `mathavan_2010` | all signs and factors match; `W` uses `v_Y cos(theta)`; the termination rule `W_r = e^2 W_c` is equivalent to paper eq. (16b) |
| M-1 ... M-7 | own RK4 implementation with bisection on the compression end and the work target | M-1 0.91069 (any V0); M-2/3/4 match the N = 20 000 values, and N = 200 differs by <= 9e-5; M-5 0.95993 / -0.24558 (N = 20 000); M-6 bounds hold (N = 50 error 3.7e-4, close to the 4e-4 bound); §4.7 table and §4.8 `e_c` fit reproduced (0.966 vs 0.967 at 1 m/s) |
| Han (§4.4) vs pooltool | pooltool's frame is x into the cushion, y = -X; mapped term by term. The current code uses the slip direction and the `PzS <= mu PzE` criterion, and discards `v_z` | the spec's version is identical; C-H1 ... C-H4 reproduced |
| GRI (§4.6) | G-1, G-2, G-3 | reproduced (G-3 equals motion spec C.3) |
| CAL-1 and the §4.8 rail-speed table | own 1D reference (stun, slide-to-roll, Mathavan bounces) | all 30 table entries reproduced to 0.01 lengths |
| Detection D-1 ... D-9 | companion-matrix roots + Newton polish | all roots, windows, `n_hat` and misses reproduced (D-4 roots 0.35996 / 0.40844 lie outside the 0.29135 s window; D-5b has complex roots only) |
| Cushion geometry | `theta_c = 15.6643 deg`, `R_c = 27.5137 mm`, `tan(theta_c) = 0.2804`; oversized 60.325 mm ball gives 11.72 deg | match |
| Pocket geometry (P-1) and WPA | WPA §7 (63.5 % ± 1 %), §8 (4-4.5 lengths, "firm" undefined), §9 (mouths 4.5-4.625 in and 5-5.125 in, 142 deg / 104 deg ± 1, back draft 12-15 deg, shelves 1-2.25 in / 0-0.375 in, "shelf includes bevel"), §10 (liner deflects downward), §11 (≥ 6 balls) | match; throats 94.188 / 101.668 mm, facings 82.51 / 52.36 mm |
| Pivot (§5.4) | energy + radial balance re-derived; P-2 angles and `sqrt(g rho)` | correct |
| pooltool descriptions (§3.8, §5.2) | `make_kiss`, `resolve_continually_touching` (10 %, 0.01 m/s, cos 0.9), the 7R/5 airborne pocket heuristic | match the source |

### 12.2 Corrections applied in this pass

| # | Where | Error in draft v1 | Correction |
|---|---|---|---|
| 1 | §3.6, §4.10, §7.1, §7.3, pitfall 16, new test D-12 | **Missing pressing-contact case.** A pair touching with zero normal speed but accelerating together (after `e = 0` micro-impacts, a frozen CB with pure spin, or a ball slip-pressed into a rail) has no downward crossing, so the approach test produced **no event** and the balls interpenetrated. With rounding noise it could instead loop at `tau = 0`. | New rule: touching, `\|f'(0)\| <= tol`, `f''(0) < 0` gives a `pressing` event at `tau = 0`, resolved in the CLI. D-12 pins `f''(0) = -0.224180`. |
| 2 | §3.9.2, pitfall 17 | CLI exit on "zero force for 5 steps" leaves 0.4-1.5 µm residual overlap (clipped Tsuji force), which trips the 1 µm overlap guard of §3.6. | Exit also requires a geometric gap `>= 0`. |
| 3 | §5.3, §5.4, §5.5, §6.1, P-3, P-4, pitfall 19 | Drop edge triggered at `r_p` (the vertical cut). The rounding lies on the shelf side (WPA "shelf includes bevel"), so the pivot and the lip-hang balance point are at `a_d = r_p + r_d`, 4.76 mm earlier. The pivot state at `psi = 0` was inconsistent with the trigger position. | Trigger, pivot axis and lip-hang threshold now use `a_d`. Added the rim-from-inside torus event (degree 8) and restricted the front-arc wall to `z < -r_d`. |
| 4 | §5.4 | `T_p` by 32-panel Simpson in `psi` is badly wrong for slow balls: +466 % at `v0 = 1e-4`, +32 % at 1e-3, -1 % at 0.01 m/s. | Substitution `u = asinh(sqrt(b/a) sin(psi/2))`, error <= 1e-4 with 16 panels. `T_p` golden values added to P-2. |
| 5 | §5.1, §5.3, §0.4 | On-shelf facing contact was modelled as an edge at `h` with `theta_c = 15.66 deg`. With the undercut face that the spec itself assumes, a ball on the cloth touches the **face** at `R(1 + sin(beta_v)) = 34.5 mm < h`, with normal tilt `beta_v`, 0.06 mm earlier. | Mathavan with `theta = beta_v`, `s_f = 27.573 mm`; edges only for airborne balls. |
| 6 | §4.10 | Jaw-arc sphere approximation: numbers off by 1 mm and the error understated (30.618 vs 30.514 mm, "< 0.11 mm"). | 31.648 vs 31.514 mm, error 0.134 mm. |
| 7 | §3.2 | Listed a "linear" degenerate case that cannot occur (`a4 = 0` implies `a3 = 0`; `a2 = 0` implies `a1 = 0`). Called pooltool PR #354 a fix. | Cases rewritten; PR #354 is **open**, not merged. |
| 8 | §3.9.3 | Hertz contact-time constant 3.2145. | 3.2181 = `2.9433 (5/4)^(2/5)`; times 378 / 329 / 238 / 207 µs. `K ≈ 1.16e9` for 284 µs unchanged. |
| 9 | §2.8 | "pooltool `FRICTIONAL_MATHAVAN` uses `e_b = 0.89`, `u_b = 0.05`". | Those are only function-signature defaults. The resolver passes Alciatore friction and the ball `e_b` (0.95). |
| 10 | §4.5 | Said Mathavan identified `e = 0.98` from the 0.910 gradient. | They used a joint RMS fit of `e` and `mu_w` for `V0 < 1.5 m/s` (2010 Fig. 7). The 0.910 figure is from the 2009 paper and agrees. The paper assumes `h = 7R/5` also for pool; we keep WPA. |
| 11 | P-4 | `v0 = 0.09904544 m/s` was computed with `g = 9.81`. | 0.0990285 m/s with `g = 9.80665`. |
| 12 | G-1, §4.6 | hop apex 0.05361 m. | 0.053633 m (`v_z'^2 / 2g`). |
| 13 | §4.4 | Han vs Mathavan angle gap "3-6 deg". | 2.6-5.6 deg (§4.7 table). |
| 14 | pitfall 12 | CLI vs impulse throw "within 0.2 deg". | Within 0.25 deg (0.04-0.21 deg in CL-6). |
| 15 | §3.9.5 | Break CLI cost "well under 1 ms". | "On the order of 1 ms" (8e4 contact evaluations; ESTIMATE, benchmark). |
| 16 | O-1, P-3 | Loose expectations ("either ... or", "liner only if"). | Pinned: O-1 flies over the nose at z = 0.131 m and leaves at t = 0.104267 s. P-3 has one liner contact (path ≈ 100 mm in ≈ 0.10 s, drop ≈ 49 mm < 2R). |
| 17 | pitfall 18 | Stable quadratic formula divides by `q = 0` when `b = c = 0`; degree-1 and degree-0 handling was unspecified. | Edge cases specified. |

### 12.3 Remaining doubts (not errors; recorded as open questions 10-12)

- The rail-speed calibration relies on a hard "firm" stroke (6-7 m/s). The rolling-ball spin-retention loss in Mathavan may make real-table matching hard (Q10).
- Step 6 uses `(1 + e_slate)` table friction for a pressed ball and then snaps the hop (Q11).
- The pivot assumes no slip up to the leave angle. The rim torus event is specified but not yet reference-tested (Q12).
- The facing-plane model inherits the undercut INTERPRETATION (Q5).
- Hertz `K` from a single Marlow data point gives 329 µs versus Marlow's own 284 µs contact time. Either value is within the stated range, but the CL golden values depend on `K`.
- D-10, CL-7 and CL-8 (random-solver agreement, 15-ball rack symmetry, order independence) are property tests and were not re-run here.
