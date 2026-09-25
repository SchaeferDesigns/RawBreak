# RAW BREAK - Physics Spec: Ball Motion on Cloth, Cue Strike, Airborne Motion

| Field | Value |
|---|---|
| Spec ID | `physics-motion-and-cue` |
| Module | `BilliardsCore` (engine-agnostic C++20, double precision, no exceptions, no RTTI) |
| Scope | Part A: ball motion on cloth (all motion states, closed-form evolution, transition times). Part B: cue strike (cue tip -> cue ball), squirt, miscue, elevated cue (jump / masse). Part C: airborne motion and slate bounces. |
| Out of scope (other specs) | ball-ball collisions, ball-cushion collisions, pockets / pocket rattles, rules, cue placement / collision of the cue body with rails and balls, rendering. |
| Status | Draft v1.1 (2026-09-25): adversarially verified. All equations were re-derived and all test values recomputed independently. Corrections are listed in the "Verification log" at the end. |

Every formula below is plain text. Anything not taken directly from a cited source is marked **DERIVED** (with a short derivation) or **TUNING** (a gameplay/engineering choice that must be calibrated).

---

## 0. Conventions and symbols

### 0.1 Frame and units

- SI units: m, s, kg, rad. Angles are passed to the core in **radians** (pooltool uses degrees; convert at the boundary).
- Right-handed world frame. Origin = center of the table bed. `+x` runs along the table length toward the foot (rack) end, `+y` runs across the width, `+z` points up.
- The cloth surface is `z = 0`. A resting ball's center is at `z = R`.
- `z_hat = (0, 0, 1)`. "Horizontal" means the x-y components; `h` subscripts mean horizontal parts, e.g. `v_h = (v_x, v_y, 0)`.
- Signs used throughout: positive `w_z` = counter-clockwise when seen from above. **Right English** (tip contact right of center, seen from behind the cue) gives **positive** `w_z`.

### 0.2 Symbols

| Symbol | Meaning | Unit |
|---|---|---|
| `R` | ball radius | m |
| `m` | ball mass | kg |
| `I = (2/5) m R^2` | ball moment of inertia about any axis through its center (solid homogeneous sphere) | kg m^2 |
| `g` | gravitational acceleration | m/s^2 |
| `r = (x, y, z)` | ball center position | m |
| `v` | ball center linear velocity | m/s |
| `w` (also written omega) | ball angular velocity, world frame | rad/s |
| `u` | velocity of the ball's material point that touches the cloth, relative to the cloth ("slip velocity") | m/s |
| `mu_s` | ball-cloth sliding friction coefficient | 1 |
| `mu_r` | ball-cloth rolling resistance coefficient (defined as rolling deceleration / g) | 1 |
| `alpha_sp` | angular deceleration of spin about the vertical axis caused by the cloth | rad/s^2 |
| `mu_sp` | spinning friction coefficient in Leckie & Greenspan form, `alpha_sp = 5 mu_sp g / (2R)` | 1 |
| `e_slate` | ball-table (cloth on slate) coefficient of restitution | 1 |
| `tau` | local time since the start of the current motion segment, `tau = t - t0` | s |
| `V` | cue tip speed along the cue axis just before impact | m/s |
| `theta` | cue elevation: angle between cue axis and table plane, butt raised, `0 <= theta < pi/2` | rad |
| `phi` | cue azimuth: direction of the horizontal projection of the stroke, from `+x` toward `+y` | rad |
| `a` | side offset of the tip-ball **contact point** perpendicular to the cue axis (horizontal), divided by R; `a > 0` = right of center | 1 |
| `b` | offset of the contact point in the cue's face plane along the "cue up" direction, divided by R; `b > 0` = above center (follow) | 1 |
| `rho = sqrt(a^2 + b^2)` | normalized contact offset | 1 |
| `M` | cue mass (whole cue) | kg |
| `m_e` | effective shaft "end mass" (squirt) | kg |
| `e_tip` | tip-ball coefficient of restitution | 1 |
| `mu_tip` | tip-ball friction coefficient (chalked) | 1 |
| `r_tip` | radius of curvature of the tip dome | m |
| `lambda` | "pinch" fraction for elevated cues (Part B.8), 0 = sequential, 1 = fully pinched | 1 |

### 0.3 Contact-point slip velocity (definition and sign check)

The cloth contact point of the ball is at `r - R z_hat`, i.e. at the offset `-R z_hat` from the center. Its velocity relative to the (stationary) cloth is

```
u = v + w x (-R z_hat)  =  v + R (z_hat x w)

components:
  u_x = v_x - R w_y
  u_y = v_y + R w_x
  u_z = v_z                (= 0 for a ball on the cloth)
```

Sign check: a ball rolling toward `+x` has topspin; the top of the ball moves forward, so its velocity `w x (R z_hat) = R w_y x_hat` must be `+`, i.e. `w_y > 0`. With `w_y = v_x / R`, `u_x = v_x - R (v_x / R) = 0`, which is the rolling condition. This matches pooltool's `rel_velocity = v + R (z_hat x w)` (Kiefl 2020).

Rolling condition: `u = 0`, equivalently `w_h = (1/R) z_hat x v`, i.e. `w_x = -v_y / R`, `w_y = v_x / R`.

Two identities used below (valid for any horizontal vector `h`):

```
z_hat x (z_hat x h) = -h
(z_hat x h) . h     = 0
```

---

## PART A - Ball motion on the cloth

### A.1 Motion states

| State | Definition | Degree of r(tau) | Ends by transition to |
|---|---|---|---|
| `Stationary` | `v = 0`, `w = 0`, `z = R` | 0 | (only by external event) |
| `Spinning` | `v = 0`, `w_h = 0`, `w_z != 0` | 0 | `Stationary` |
| `Sliding` | on cloth, `u != 0` | 2 | `Rolling` (or `Spinning` / `Stationary` in degenerate cases) |
| `Rolling` | on cloth, `u = 0`, `v != 0` | 2 | `Spinning` or `Stationary` |
| `Airborne` | `z > R` or `v_z > 0` | 2 (in z) | slate impact event |
| `Pocketed` | removed from play (owned by pocket spec) | - | terminal |

Following Leckie & Greenspan (2005, 2006) and pooltool, the motion inside each state is a polynomial in local time `tau` of degree at most 2 for position and at most 1 for angular velocity. This is what makes the event-based simulation possible: ball-ball contact times become roots of quartics, ball-cushion contact times become roots of quadratics.

### A.2 Friction models (the three cloth effects)

1. **Sliding friction** (Coulomb): while `u != 0` the cloth applies the force `F = -mu_s m g u_hat` at the contact point, with `u_hat = u / |u|`. It acts along the **slip** direction, which in general differs from the direction of travel. That difference is what makes swerve and masse curves.
2. **Rolling resistance**: while rolling, the ball decelerates at a constant `mu_r g` along `-v_hat`. Physically this is the cloth "dimple" moving the normal force slightly ahead of the contact point (Alciatore TP B.2). `mu_r` is **defined** here as deceleration / g, which is exactly what Alciatore TP B.2 and Mathavan et al. (2009) measure.
3. **Spinning friction**: a torque about the vertical axis from the finite contact patch. It decelerates `w_z` at a constant rate `alpha_sp` in every on-cloth state (sliding, rolling, spinning). Leckie & Greenspan write the torque as `mu_sp m g R`, which gives

```
alpha_sp = (mu_sp m g R) / I = 5 mu_sp g / (2 R)          [rad/s^2]
mu_sp    = 2 R alpha_sp / (5 g)                             [dimensionless]
```

**Note on pooltool units.** pooltool stores `u_sp = u_sp_proportionality * R` with `u_sp_proportionality = 10*2/5/9 = 0.444 m^-1`, and uses `alpha = 5 u_sp g / (2R)`. That gives `alpha = 0.444 * 2.5 * g = 10.9 rad/s^2` (pooltool uses g = 9.81) whatever the ball size. The constant was presumably meant to reproduce Alciatore's measured ~10 rad/s^2 spin-down (TP B.2); with g = 9.81 it comes out about 9 % high. The L&G-based port by Zitelli uses `mu_sp = 0.044` (constructor default, together with its own R = 0.02625 m). For a pool ball (R = 0.028575 m) that gives `alpha = 37.8 rad/s^2`, about 4x the measured value. **Decision: RawBreak parametrizes by `alpha_sp` directly** and derives `mu_sp` only for reporting.

**DERIVED plausibility check.** For a uniform-pressure circular contact patch of radius `a_c`, the Coulomb spin torque is `(2/3) mu_s m g a_c`, so `mu_sp = (2/3) mu_s a_c / R`. With `alpha_sp = 10 rad/s^2` (`mu_sp = 0.01166`) and `mu_s = 0.2`, this gives `a_c = 3 mu_sp R / (2 mu_s) = 2.5 mm`, a plausible cloth contact-patch radius. The model is physically consistent.

### A.3 Stationary

```
r(tau) = r0,   v(tau) = 0,   w(tau) = 0.   No internal transition.
```

### A.4 Spinning (in place, only w_z)

```
r(tau)   = r0
v(tau)   = 0
w_h(tau) = 0
w_z(tau) = w_z0 - sgn(w_z0) * alpha_sp * tau
duration  tau_spin = |w_z0| / alpha_sp      -> Stationary
```

### A.5 Sliding

Equations of motion (constant `u_hat` is proven below):

```
m dv/dt = -mu_s m g u_hat
I dw/dt = (-R z_hat) x (-mu_s m g u_hat) = mu_s m g R (z_hat x u_hat)      (horizontal torque)
        + spin torque about z: dw_z/dt = -sgn(w_z) alpha_sp
```

**Slip evolution (DERIVED, standard result, e.g. Kiefl 2020, L&G 2005).**

```
du/dt = dv/dt + R z_hat x dw/dt
      = -mu_s g u_hat + (5/2) mu_s g z_hat x (z_hat x u_hat)
      = -mu_s g u_hat - (5/2) mu_s g u_hat
      = -(7/2) mu_s g u_hat
```

`w_z` does not appear because `z_hat x (w_z z_hat) = 0`. So the direction `u_hat` stays **constant** and `|u|` drops linearly:

```
u(tau)   = u0 (1 - tau / tau_slide)
tau_slide = 2 |u0| / (7 mu_s g)
```

**Closed form (vector form, world frame; `u_hat0 = u0 / |u0|` is constant for the whole segment):**

```
r(tau)   = r0 + v0 tau - (1/2) mu_s g u_hat0 tau^2          (z stays R)
v(tau)   = v0 - mu_s g u_hat0 tau
w_h(tau) = w_h0 + (5 mu_s g / (2R)) (z_hat x u_hat0) tau
           i.e. w_x(tau) = w_x0 - (5 mu_s g / (2R)) u_hat0_y tau
                w_y(tau) = w_y0 + (5 mu_s g / (2R)) u_hat0_x tau
w_z(tau) = w_z0 - sgn(w_z0) alpha_sp min(tau, |w_z0| / alpha_sp)     (clamped, see A.7)
```

This is algebraically the same as pooltool's `_evolve_slide_state`, which does the same thing in a frame rotated by the heading. The vector form above is simpler and avoids the rotation.

**Why curves appear (swerve / masse).** The acceleration `-mu_s g u_hat0` is a constant vector, so the path is a **parabola** with its axis along `-u_hat0`. It is a straight line only when `u0` is parallel to `v0`. That happens for pure follow/draw and for pure side spin `w_z` with a level cue, because `w_z` does not enter `u`. The curve comes from any horizontal spin component parallel to the direction of travel. Such a component is produced by side English combined with cue elevation, see Part B.

**State at the end of sliding (Coriolis invariant, DERIVED).** Since `v(tau_slide) = v0 - mu_s g u_hat0 tau_slide = v0 - (2/7) u0`:

```
v_roll = v(tau_slide) = (5/7) v_h0 - (2/7) R (z_hat x w_h0)
```

`L_c := (5/7) v_h - (2/7) R (z_hat x w_h)` is the horizontal angular momentum about the contact point, rotated by 90 deg and scaled: with `H_c,h = I w_h + m R (z_hat x v_h)`, **DERIVED** `L_c = -(5 / (7 m R)) (z_hat x H_c,h)`. Any horizontal force applied **at the contact point** leaves it unchanged. That covers sliding friction and also slate-bounce friction impulses (Part C). Vertical forces through the center leave it unchanged too. Proof for an impulse `P_t` (horizontal) at `-R z_hat`: `dv = P_t/m`, `dw = (-R z_hat) x P_t / I`, so `d L_c = (5/7) P_t/m - (2/7) R z_hat x ((-R z_hat x P_t)/I) = (5/7) P_t/m - (2/7)(5/2) P_t/m = 0`. This invariant is the backbone of several tests and of the masse aiming check (B.8.4).

Rest of the transition logic: at `tau_slide` snap `w_h := (1/R) z_hat x v` (removes round-off) and classify (A.8). In the degenerate case `|v(tau_slide)| < eps_v`, go to `Spinning`/`Stationary`.

### A.6 Rolling

```
v_hat0 = v0 / |v0|           (constant direction; rolling resistance never curves the path)
r(tau)   = r0 + v0 tau - (1/2) mu_r g v_hat0 tau^2
v(tau)   = v0 - mu_r g v_hat0 tau
w_h(tau) = (1/R) z_hat x v(tau)          i.e. w_x = -v_y/R, w_y = v_x/R
w_z(tau) = w_z0 - sgn(w_z0) alpha_sp min(tau, |w_z0|/alpha_sp)
duration  tau_roll = |v0| / (mu_r g)
distance  d_roll   = |v0|^2 / (2 mu_r g)
at tau_roll: v = 0, w_h = 0 -> Spinning if |w_z| > eps_w else Stationary
```

In the L&G model a rolling ball with side spin travels straight. Alciatore TP B.2 analyzes a tiny "ball turn" caused by the cloth dimple. It is not in the baseline; see Open questions.

### A.7 How w_z decays in each state

| State | w_z(tau) | Notes |
|---|---|---|
| Stationary | 0 | - |
| Spinning | `w_z0 - sgn(w_z0) alpha_sp tau` | segment ends exactly when it reaches 0 |
| Sliding | same linear law, **clamped** at 0 | independent of the sliding friction (L&G, pooltool) |
| Rolling | same linear law, **clamped** at 0 | independent of rolling resistance |
| Airborne | constant `w_z0` | no contact, no torque (air neglected, C.1) |

The clamp makes `w_z(tau)` piecewise linear inside a sliding or rolling segment. Positions do not depend on `w_z` in any state, so the clamp does not affect collision root finding. Evaluate `w_z` with the clamp. An extra "w_z reached 0" event is optional and not required.

### A.8 Transition times and state classification

Next internal transition time of a segment that starts at `t0`:

| State | tau_end |
|---|---|
| Sliding | `2 \|u0\| / (7 mu_s g)` |
| Rolling | `\|v0\| / (mu_r g)` |
| Spinning | `\|w_z0\| / alpha_sp` |
| Airborne | `(v_z0 + sqrt(v_z0^2 + 2 g (z0 - R))) / g` (slate contact, Part C) |
| Stationary, Pocketed | +infinity |

**Classification after any event or impulse** (use after collisions, cue strike, slate bounce):

```
1. if pocketed flag                              -> Pocketed
2. if (z - R) > eps_z or v_z > eps_v             -> Airborne
   (v_z < -eps_v at z = R must never reach the classifier: resolve the slate impact first)
3. set z := R, v_z := 0
4. if |u| > eps_v                                -> Sliding
5. else if |v| > eps_v                           -> Rolling   (snap w_h := (1/R) z_hat x v)
6. else if |w_z| > eps_w                         -> Spinning  (snap v := 0, w_h := 0)
7. else                                          -> Stationary (all zero)
```

Recommended tolerances (TUNING, double precision): `eps_z = 1e-9 m`, `eps_v = 1e-9 m/s`, `eps_w = eps_v / R ~= 3.5e-8 rad/s`.

### A.9 Parameters for Part A

| Parameter | Default | Typical range | Tournament-fast preset (clean new worsted, e.g. Simonis 860 / 760) | Slow preset (napped woolen bar cloth, worn/dirty/humid) | Sources |
|---|---|---|---|---|---|
| `g` | 9.80665 m/s^2 | +-0.3 % geographic | same | same | standard gravity |
| `R` | 0.028575 m (2.25 in / 2) | WPA: diameter 2.25 in +- 0.005 in -> R in [28.511, 28.639] mm. The PDF prints "(+.005)", but the plus-minus glyph was lost in that document: it also writes the 62.5-64.5 % rail height as "63 1/2 % (+1 %)". | same | same | WPA Equipment Spec. sec. 16 (and sec. 7 for the glyph evidence) |
| `m` | 0.170 kg | WPA: 156-170 g | same | same | WPA Equipment Spec. sec. 16; pooltool 0.170097 |
| `I` | 5.5524e-5 kg m^2 | - | - | - | `(2/5) m R^2` |
| `mu_s` | 0.20 | 0.15-0.40 (typ. 0.2); snooker measured 0.178-0.245 | 0.17 | 0.26 | Alciatore physical properties; Mathavan et al. 2009; Witters & Duymelinck 1986 (0.14 -> 0.21); pooltool 0.2; Zitelli's L&G port 0.21 (value in the L&G papers themselves not checked) |
| `mu_r` | 0.010 | 0.005-0.015; snooker measured 0.0127-0.0129; Marlow 0.011-0.024 | 0.007 | 0.014 | Alciatore TP B.2 (0.01 measured) and physical properties; Mathavan et al. 2009; pooltool 0.01; Zitelli's L&G port 0.016 |
| `alpha_sp` | 10 rad/s^2 | 5-15 rad/s^2 | 8 | 13 | Alciatore TP B.2 (~10 measured, spin-down video); physical properties page 5-15 |
| `mu_sp` (L&G form) | 0.01166 | 0.0058-0.0175 | 0.00932 | 0.01515 | DERIVED from `alpha_sp` |

The presets are **engineering estimates placed inside the published ranges (TUNING)**. They were not measured on those cloths and must be calibrated (see Implementation notes, calibration). WPA competition requires nap-free worsted cloth (WPA Equipment Spec. sec. 12). Nap drift on bar cloth is not modeled; Mathavan et al. found no measurable nap effect on snooker cloth.

Rolling distance of a ball that starts rolling at 1 m/s: fast preset 7.28 m, default 5.10 m, slow preset 3.64 m (`d = v^2 / (2 mu_r g)`).

**Observed speed dependence (keep the constants in the baseline):**
- `mu_s`: Witters & Duymelinck (1986) found that the sliding coefficient of a pool ball on cloth rises with slip speed from about 0.14 toward an asymptote of about 0.21. Mathavan et al. (2009) measured 0.178-0.245 on snooker cloth and could not confirm the speed trend.
- `mu_r`: Witters & Duymelinck found the rolling resistance independent of speed below 0.5 m/s. Mathavan et al. also report a constant value.
- `alpha_sp`: no speed-dependence data found.
- A speed-dependent `mu_s(|u|)` would break the polynomial form. If it is ever needed, freeze `mu_s := mu_s(|u0|)` per sliding segment (piecewise constant). That keeps all segments polynomial. **DERIVED** recommendation.

---

## PART B - Cue strike (cue tip -> cue ball)

### B.1 Inputs and outputs

Inputs: `V` [m/s], `theta` [rad], `phi` [rad], contact offsets `a`, `b` [1], `M` [kg], `m_e` [kg], `e_tip`, `mu_tip`, `lambda` (B.8), ball parameters, `e_slate`, `mu_s`.
Outputs: ball `v`, `w` right after the strike (including the slate reaction at t = 0+), motion state, flags `miscue`, `separationMargin`, and the cue tip speed after impact `V'` (for animation and audio).

Preconditions: the cue ball is at rest on the cloth (the rules require all balls at rest before a stroke), `0 <= theta < pi/2`, `rho < rho_valid = 0.95` (TUNING, see Implementation note 9; offsets between `rho_max` and `rho_valid` are legal inputs and give a miscue), `V >= 0`. Invalid inputs return an error code (no exceptions).

The model is **instantaneous and point-like**. Tip contact lasts about 1 ms; Alciatore TP B.20 uses 0.8 ms for a hard tip at break speed. Gravity and cloth friction do nothing during that time, apart from the slate reaction handled in B.8.

### B.2 Cue frame

```
d   = ( cos(theta) cos(phi),  cos(theta) sin(phi), -sin(theta) )   cue axis, pointing from butt to tip (stroke direction)
e_r = ( sin(phi),            -cos(phi),             0          )   shooter's "right", always horizontal
e_u = e_r x d = ( sin(theta) cos(phi), sin(theta) sin(phi), cos(theta) )   "cue up", perpendicular to d in the vertical plane
```

`(e_r, d, e_u)` is a right-handed orthonormal triad. Useful products: `e_r x d = e_u`, `e_u x d = -e_r`, `d x d = 0`.

### B.3 Contact point and tip offsets

```
c   = sqrt(1 - a^2 - b^2)
Q   = R ( a e_r + b e_u - c d )          contact point relative to the ball center
n   = -Q / R                             contact normal, pointing into the ball
```

`a` and `b` are the **contact-point** offsets measured in the plane perpendicular to the cue axis ("cue face plane"). That is what a player sees when sighting down the cue. For a level cue, `b` is the height above center.

**Relation to L&G / pooltool offsets (DERIVED, checked against the pooltool source).** L&G's cue-strike formula uses the contact point in ball-frame coordinates, with vertical offset `b_w` measured along world `z` and depth `c_w`. The squared perpendicular offset from the cue line is `a^2 + (b_w cos(theta) - c_w sin(theta))^2`. With `b_w = b cos(theta) + c sin(theta)` and `c_w = c cos(theta) - b sin(theta)`, this equals `R^2 (a^2 + b^2)` in the notation here. pooltool's user-facing `cue.b` is already the cue-face offset used here: its `instantaneous_point` resolver converts it internally with exactly these two formulas. So `b_pooltool = b`. pooltool's side offset has the opposite sign (`a_pooltool > 0` = left English, as documented in its `squirt.py`), so `a_pooltool = -a`.

**Aim-point vs contact-point conversion (DERIVED, for UI).** The tip is a dome of radius `r_tip` whose center lies on the cue axis. Sphere-sphere contact lies on the line joining the centers, so

```
(a, b) = (A, B) * R / (R + r_tip)
```

where `(A, B)` are the normalized offsets of the **cue axis** from the ball center. With `r_tip ~= R/3` (dime/nickel tip shape) the factor is 0.75. Alciatore's tip shape FAQ says the same: the tip center sits at 4/3 of the contact-point offset. So the miscue limit `rho = 0.5` corresponds to a cue-axis offset of about 0.67 R.

### B.4 Miscue criterion (friction cone)

The cue impulse acts along the cue axis `d` at `Q`. The angle psi between `d` and the contact normal `n` satisfies `cos(psi) = d.n = c` and `sin(psi) = rho`. The tip grips without slipping only while `tan(psi) <= mu_tip`:

```
rho <= rho_max = mu_tip / sqrt(1 + mu_tip^2)         (DERIVED; same condition as quoted in the introduction of Kim 2021, who cites Alciatore TP 2.1)
```

| Tip condition | mu_tip | rho_max |
|---|---|---|
| well chalked (default) | 0.6 (Alciatore physical properties) | 0.514 |
| chalk worn / poor (TUNING) | ~0.4 | 0.371 |

`rho_max = 0.514` reproduces the generally accepted miscue limit of about half a ball radius (Alciatore, tip FAQ). Chalk dependence (TUNING): track chalk freshness per shot and interpolate `mu_tip` from 0.6 (fresh) to about 0.35-0.4 (worn or none). No primary measurement was found for unchalked leather.

### B.5 Impulse magnitude

General form (**DERIVED**; Newton restitution on the relative velocity along the impulse direction `p_hat` at the contact point; the cue is a rigid body moving only along its axis):

```
k    = | (Q/R) x p_hat |
dp   = d . p_hat
J    = (1 + e_tip) V dp / ( dp^2 / M + (1/m) (1 + (5/2) k^2) )        [N s]
```

**Normal case (no miscue): `p_hat = d`**, so `dp = 1` and `k = rho`:

```
J = (1 + e_tip) V / ( 1/M + (1/m) (1 + (5/2) rho^2) )
v_ball = J / m = (1 + e_tip) V / ( 1 + m/M + (5/2) rho^2 )         (along d before squirt and slate)
V'     = V - J / M                                                  (cue speed after impact)
```

- With `e_tip = 1` this is exactly the Leckie & Greenspan formula used by pooltool (`2 V0 / (1 + m/M + ...)`).
- It is also exactly Kim (2021), Eq. 83, derived with Stronge's energetic restitution for a gripping tip. Eq. 83 is the leading order in the small shaft end-mass ratio `m_e/m`. Kim's spin, Eq. 84 (`R |w| = (5/2) (b/R) |v|`), matches B.6.
- For a center hit it equals Alciatore TP A.30, Eq. 22: `v_b = (1 + e) V / (1 + m/M)`.

**Model choice.** Alciatore TP A.30 also offers an "efficiency" form based on Coriolis' assumption that the fractional energy loss is independent of offset. At `rho = 0.5` it gives about 14 % less ball speed than the restitution form: 13.6 % for eta = 0.87 and a 19 oz cue, where eta = 0.87 corresponds to e = 0.677 by TP A.30 Eq. 23. At the default e = 0.73 (eta = 0.888) the gap is 10.0 %. RawBreak uses the **restitution form**, which Kim (2021) supports with impact mechanics. It is also what L&G and pooltool use. See Open questions.

**Miscue case (`rho > rho_max`), DERIVED approximation:** the tip slips, and the impulse lies on the edge of the friction cone:

```
t_hat = (d - (d.n) n) / |d - (d.n) n|             tangential direction of the stroke at Q
p_hat = (n + mu_tip,k t_hat) / sqrt(1 + mu_tip,k^2)
```

`mu_tip,k` is the kinetic tip friction; default = `mu_tip`, TUNING range 0.3-0.6. At `rho = rho_max` (with `mu_tip,k = mu_tip`), `p_hat = d`, so the model is continuous. Squirt (B.7) is **not** applied in the miscue branch; the deflection is already in `p_hat`. Set the `miscue` flag. Any randomness (the "click" and erratic outcome) belongs to the game layer and must be passed in as seeded input, never generated inside the core, for determinism.

### B.6 Ball velocity and spin after the tip impulse

```
v = (J / m) p_hat                    (then squirt, B.7, then slate reaction, B.8)
w = (J / I) (Q x p_hat)
normal case:  Q x d = R (a e_u - b e_r)
              w = (5 J / (2 m R)) (a e_u - b e_r)
```

Checks for a level cue, `phi = 0` (`d = x_hat`, `e_r = -y_hat`, `e_u = z_hat`):
- `b > 0` gives `w_y = +(5/2) (J/m) b / R`: topspin/follow. `b < 0` gives draw.
- `a > 0` gives `w_z = +(5/2) (J/m) a / R`: counter-clockwise from above = right English.
- Spin-rate factor `SRF = |w_perp| R / |v| = (5/2) rho` (Alciatore TP A.25/A.30). At the miscue limit `rho = 0.5`, `SRF = 1.25`.
- **Natural roll immediately at `b = 0.4`** (contact height 7R/5 above the cloth): `w_y R = v`, so `u = 0` (Mathavan 2009; Alciatore TP A.18).

**Separation / double-hit margin (DERIVED; generalizes TP A.30, Eq. 14 to e_tip < 1).** The tip leaves the ball cleanly if the ball center moves along `d` faster than the cue after impact: `J/m > V - J/M`. That is equivalent to

```
rho^2 < (2/5) e_tip (1 + m/M)
```

For `e_tip = 0.73` and a 19 oz cue, `rho_sep = 0.620`, which is above `rho_max = 0.514`. So normal shots never trip this. Report `separationMargin = (2/5) e_tip (1 + m/M) - rho^2`. The rules spec can use it together with the object-ball proximity to judge double hits and pushes.

### B.7 Squirt (cue-ball deflection) - Alciatore end-mass model

From Alciatore TP A.31 (the same model is implemented in pooltool `squirt.py`):

```
m_r   = m / m_e                         ball mass over shaft end mass
alpha_sq = atan2( (5/2) a sqrt(1 - a^2),  1 + m_r + (5/2)(1 - a^2) )        [rad]
```

- Sign: `a > 0` (right English) gives `alpha_sq > 0`, so the ball deflects to the **left** (counter-clockwise seen from above).
- TP A.31 derives this for a pure side offset (`b = 0`). The baseline applies it with `a` alone, whatever `b` is, as pooltool does (`get_squirt_angle(m, end_mass, cue.a)`). This is an **approximation for `b != 0`**, not a property of the end-mass model. **DERIVED** under TP A.31's own assumptions (tip grips, round shaft with the same end mass in every transverse direction):
  - The transverse impulse points away from the contact offset `(a e_r + b e_u)`.
  - Its angle is `atan2((5/2) rho c, 1 + m_r + (5/2) c^2)` with `c = sqrt(1 - rho^2)`.
  - Horizontal part: `tan(alpha_h) = (5/2) a c / (1 + m_r + (5/2) c^2)`. There is also an equal-order vertical part: upward for draw, downward (into the slate) for follow.
  - Example, `m_r = 20`, `a = 0.35`, `b = -0.35`: A.31 with `a` alone gives 2.024 deg, the derived horizontal part gives 1.903 deg (-6 %). At `a = 0.25`, `b = -0.4` the gap is -7 %.
  
  No measurement separating the two was found, so the baseline stays with A.31 (see Open questions).
- Apply it once, in the normal (grip) branch only, by rotating the tip-impulse direction about `e_u`:

```
p_hat_sq = cos(alpha_sq) d - sin(alpha_sq) e_r          (replaces d in v = (J/m) p_hat; J and w unchanged)
```

For a level cue this is a rotation of `v` by `+alpha_sq` about `z_hat`, identical to pooltool.

The spin is not modified. Alciatore TP B.7 notes a small squirt-related spin reduction; see Open questions.

Squirt does not depend on speed in this model. Swerve does (it acts during sliding).

| Shaft type | m / m_e | m_e (for m = 0.170 kg) | alpha_sq at a = 0.5 | at a = 0.25 |
|---|---|---|---|---|
| break / jump / high-squirt | 15 | 11.3 g | 3.466 deg | 1.889 deg |
| standard playing shaft (default, TUNING) | 20 | 8.5 g | 2.709 deg | 1.485 deg |
| pooltool default | 30 | 5.7 g | 1.886 deg | 1.040 deg |
| low-deflection shaft | 40 | 4.25 g | 1.446 deg | 0.800 deg |

Range 15-40 and the values 3.466 / 1.446 deg are from Alciatore TP A.31. The shaft-type assignment is TUNING. Only roughly the last 6 inches of the shaft contribute to end mass (TP A.31).

### B.8 Elevated cue (theta > 0): slate reaction, jump, swerve, masse

#### B.8.1 What happens physically

With `theta > 0` the impulse has a downward component `-J sin(theta)`, which drives the ball into the slate:
- **Small elevation (the break, draw shots over a rail, about 3-15 deg):** the tip delivers most of its impulse before a significant ball-table force builds up (Alciatore TP B.10, supported by his high-speed video HSV B.44). The ball then rebounds from the slate and **hops**. His break FAQ adds that "most of the initial bounce ... occurs after the CB leaves the tip". This is the **sequential** model.
- **Jump shots (about 45-60 deg, light jump cue, hard tip):** still largely sequential. The light cue loses most of its forward speed at impact and gets out of the way (Alciatore jump-cue FAQ). The slate rebound launches the ball.
- **Masse (about 60-90 deg, heavy playing cue):** the ball is pinched between tip and slate. Part of the vertical impulse is taken by the slate *during* tip contact, and the upward rebound is largely absorbed by the tip that is still in contact. This is the **pinched** limit.
- Horizontal spin produced with elevation includes a component along the direction of travel (the `a sin(theta)` term). It creates slip sideways to the motion, so the path curves: **swerve** at small `theta`, **masse** at large `theta`. Right English plus elevation curves to the right after the initial squirt to the left.

#### B.8.2 Unified pinch model (DERIVED; lambda is TUNING)

`lambda` in [0, 1] is the fraction of the ball's vertical motion that the slate blocks during tip contact. The slate normal force passes through the ball center, so it adds no torque. The ball's translational inverse mass along `d` becomes `(1/m)(1 - lambda sin^2(theta))`:

```
J = (1 + e_tip) V / ( 1/M + (1/m) (1 - lambda sin^2(theta) + (5/2) rho^2) )
```

- `lambda = 0` gives the free-ball formula of B.5. This is the sequential model used by pooltool and by Alciatore TP B.10.
- `lambda = 1` gives a ball that can only move horizontally during the hit. This matches the kinematics of Alciatore TP A.19 / Coriolis: horizontal velocity `(J/m) cos(theta)`, spin from the full impulse.

Recommended schedule (TUNING, to be calibrated against high-speed video):

```
s(x)   = clamp(x, 0, 1);  smooth(x) = 3 s^2 - 2 s^3
lambda = smooth( (theta - theta0) / (theta1 - theta0) )
playing / break cues (M > 0.35 kg):  theta0 = 30 deg, theta1 = 70 deg
jump cues (M <= 0.35 kg, hard tip):   theta0 = 60 deg, theta1 = 85 deg
e_pinch = 0.2      (rebound of the pinched part; the tip absorbs most of it)
```

#### B.8.3 Slate reaction at t = 0+ (handled inside the strike, not as a scheduled event)

After computing `J`, `p_hat` (with squirt) and `w` (B.5-B.7):

```
v_h  = (J/m) (p_hat)_h                          horizontal part
w_n  = (J/m) (-(p_hat)_z)                       "virtual" downward speed into the slate; in the grip branch
                                                = (J/m) cos(alpha_sq) sin(theta) >= 0; in the miscue branch it can be < 0
                                                (e.g. a level draw miscue kicks the ball slightly upward)
if w_n <= 0:                                    no slate contact; upward or zero vertical speed
    v_z = -w_n
    if v_z < v_z_min (C.4): v_z = 0            TUNING guard, same threshold as C.4 (avoids sub-2-mm micro-hops)
if w_n > 0:
    e_eff = lambda e_pinch + (1 - lambda) e_slate
    Pi    = (1 + e_eff) w_n                     total normal impulse per unit mass
    u     = v_h + R (z_hat x w)                 slip of the bottom contact point
    dv_h  = -min( mu_s Pi, (2/7) |u| ) u_hat    Coulomb friction; stick if (2/7)|u| <= mu_s Pi
    v_h  += dv_h
    w    += -(5 / (2R)) (z_hat x dv_h)          torque of the friction impulse (w_z unchanged)
    v_z   = e_eff w_n
    if v_z < v_z_min (C.4): v_z = 0
v = v_h + v_z z_hat ;  classify (A.8)
```

With `lambda = 0` this is exactly the slate-impact routine of Part C.3 applied to the free-ball tip result. The slate friction reproduces Alciatore's bounce model (TP B.10). The stick branch sets `u = 0` exactly, which gives rolling.

#### B.8.4 Masse / swerve aiming check (Coriolis, DERIVED here with the invariant of A.5)

After the strike, `L_c = (5/7) v_h - (2/7) R z_hat x w_h` does not change through slate bounces, airborne phases (no forces except gravity) and sliding. It is therefore the **velocity at the moment the ball starts rolling** (if nothing else is hit first). With squirt ignored and `phi = 0`:

```
L_c = (5/7)(J/m) ( cos(theta) + b ,  -a sin(theta) , 0 )
final direction delta = atan2( -a sin(theta), cos(theta) + b )      (negative = to the right of the aim)
```

This is Coriolis' masse aiming rule as verified in Alciatore TP A.19 (his `b` is measured below center, i.e. `b_Alciatore = -b`; his angle is positive to the right). It does not depend on `lambda`, `e_slate`, `mu_s`, `e_tip` or `V`. Only the magnitude of `L_c` depends on them. When `cos(theta) + b < 0` the ball ends up coming back (|delta| > 90 deg), as in a strong masse.

TP A.19 also notes that real shots usually end up a little short of this angle. It attributes the shortfall partly to squirt and to the ball being jammed between tip and slate. Expect calibration against video to show slightly less curve than the ideal invariant.

#### B.8.5 Algorithm (reference pseudo-code)

```
CueStrike(ball at rest, V, theta, phi, a, b, cue{M, m_e, e_tip, mu_tip, mu_tip_k}, lambda, e_pinch, table{e_slate, mu_s}):
  validate inputs                                    -> error code on failure
  (d, e_r, e_u) = CueFrame(theta, phi)
  rho = hypot(a, b);  c = sqrt(1 - rho^2);  Q = R (a e_r + b e_u - c d);  n = -Q/R
  rho_max = mu_tip / sqrt(1 + mu_tip^2)
  if rho <= rho_max:                                  # grip
      J     = (1+e_tip) V / (1/M + (1/m)(1 - lambda sin^2 theta + 2.5 rho^2))
      w     = (5 J / (2 m R)) (a e_u - b e_r)
      alpha = SquirtAngle(a, m/m_e)
      p_hat = cos(alpha) d - sin(alpha) e_r
      miscue = false
  else:                                              # miscue, friction-cone edge (lambda ignored)
      t_hat = normalize(d - (d.n) n);  p_hat = (n + mu_tip_k t_hat)/sqrt(1 + mu_tip_k^2)
      k = |(Q/R) x p_hat|;  dp = d.p_hat
      J = (1+e_tip) V dp / (dp^2/M + (1/m)(1 + 2.5 k^2))
      w = (J/I) (Q x p_hat);  miscue = true
  V_after = V - J (d.p_hat) / M
  SlateReactionAtZero(...)   # B.8.3
  state = Classify(...)      # A.8
  return {v, w, state, miscue, separationMargin, V_after}
```

### B.9 Realistic ranges and equipment

**Cue-ball speeds (Alciatore, "typical range of ball speeds"):**

| Shot | Cue-ball speed |
|---|---|
| soft touch | < 1 mph (< 0.45 m/s); design floor about 0.3 m/s |
| slow | 1-2 mph (0.45-0.89 m/s) |
| medium | 2-4 mph (0.89-1.79 m/s) |
| fast | 4-7 mph (1.79-3.13 m/s) |
| power shot | 7-10 mph (3.13-4.47 m/s) |
| powerful break | 25-30 mph (11.2-13.4 m/s) |
| very powerful break | ~35 mph (15.6 m/s) |

**Cue-ball speed / cue speed for a center hit, `(1 + e_tip)/(1 + m/M)`:**
- leather tip, 19 oz: 1.315
- phenolic break tip, 21 oz: 1.439
- phenolic jump cue, 9 oz: 1.110

These are consistent with the 120-150 % curves in TP A.30. So the input `V` spans about 0.23 m/s (0.3 m/s cue ball, leather) up to about 9 m/s (13 m/s break) or 11 m/s (16 m/s). Clamp `V` to [0, 12] m/s in the input layer (TUNING).

| Equipment | Mass | Tip `e_tip` | Other | Sources |
|---|---|---|---|---|
| playing cue | 18-21 oz = 0.510-0.595 kg (default 19 oz = 0.5386 kg) | leather 0.71-0.75 (default 0.73) | `m/m_e` 20 (standard) to 40 (low-deflection) | Alciatore TP A.30, physical properties |
| break cue | 18-21 oz (default 21 oz = 0.5953 kg) | phenolic 0.81-0.87 (default 0.85) | `m/m_e` about 15 | TP A.30 (phenolic about 17 % more break energy than leather) |
| jump cue | about 7-10 oz = 0.20-0.28 kg (default 9 oz = 0.2551 kg; TUNING, typical products) | phenolic 0.85 | short (WPA minimum cue length 40 in = 1.016 m), stiff | Alciatore jump-cue FAQ (shorter, lighter, stiffer, hard tip) |
| WPA limits | max 25 oz = 0.7087 kg | tip width max 14 mm | min length 40 in | WPA Equipment Spec. sec. 17 |
| tip dome radius `r_tip` | about R/3 = 9.5 mm (dime to nickel shape) | - | - | Alciatore tip size/shape FAQ |
| tip friction `mu_tip` | 0.6 chalked | - | - | Alciatore physical properties |
| contact time | about 0.8 ms (hard tip, break) to about 1.5 ms (TUNING, soft tip) | - | only for audio/animation | TP B.20 |

`M` is the whole cue mass. The stress wave crosses the cue within the contact time, so the full cue participates. A small grip-hand contribution may be added as TUNING (0-10 %).

---

## PART C - Airborne motion and slate bounces

### C.1 Ballistic flight

```
r(tau) = r0 + v0 tau - (1/2) g z_hat tau^2
v(tau) = v0 - g z_hat tau
w(tau) = w0                               (all components constant)
```

**Air drag and Magnus are neglected (DERIVED estimate).** Drag is `F_d = (1/2) rho_air C_d (pi R^2) v^2`, with `rho_air = 1.2 kg/m^3` and `C_d ~= 0.5`. At 5 m/s this is about 0.019 N, compared with a weight of 1.67 N. On a 0.13 s break hop at 12 m/s, drag costs under 1 % of speed. The effect on rolling and sliding is already absorbed into `mu_r`.

### C.2 Landing event

The ball lands when `z(tau) = R`, taking the later root:

```
tau_land = ( v_z0 + sqrt(v_z0^2 + 2 g (z0 - R)) ) / g
```

With `z0 = R` and `v_z0 > 0`, this is `tau_land = 2 v_z0 / g` and the apex height is `v_z0^2 / (2 g)`.

Interface to other specs:
- The landing point may lie over a pocket opening or a cushion. The pocket/table-geometry spec must check it before calling C.3.
- While airborne, ball-ball contacts use full 3D distance. That is still a quartic, because every coordinate is quadratic.
- An airborne ball can hit the cushion nose, the rail top, or leave the table. Those cases belong to the cushion and rules specs.

### C.3 Slate impact resolution

Incoming state: `z = R`, `v_z < 0`. Let `w_n = -v_z > 0`.

```
v_z'   = e_slate w_n                       (upward)
Pi     = (1 + e_slate) w_n                 normal impulse per unit mass (acts through the center: no torque)
u      = v_h + R (z_hat x w_h)             slip before impact
if |u| > eps_v:
   dv_h = -min( mu_s Pi, (2/7)|u| ) u_hat
else:
   dv_h = 0
v_h'   = v_h + dv_h
w'     = w - (5/(2R)) (z_hat x dv_h)       only w_x, w_y change; w_z is unchanged
```

- **Stick** branch (`(2/7)|u| <= mu_s Pi`): gives `u' = 0` exactly.
- **Slip** branch: `|dv_h| = mu_s (1 + e_slate) w_n`.

Both branches agree at the boundary. The friction coefficient during the bounce is taken equal to the cloth sliding coefficient `mu_s`, as in Alciatore TP B.10 and pooltool.

This is the vector form of Alciatore TP B.10, which it reproduces to all published digits (Test C3). It is also pooltool's `FrictionalInelasticTable` with one correction. pooltool's stick branch scales `w_z` by 2/7. This was verified in the source, `physics/resolve/ball_table/frictional_inelastic/__init__.py`: the update is `w += (5/7) (-w + (z_hat x v)/R)`, where the `-w` term includes `w_z`. Its `w_x`, `w_y` result is correct, and its slip branch agrees with C.3. Physically, `w_z` cannot change from a horizontal force at the bottom contact point. **Do not copy that.**

The Coriolis invariant `L_c` (A.5) is unchanged by C.3.

### C.4 Bounce termination (Zeno guard)

```
h_min   = 2 mm (TUNING, range 1-5 mm)       -> v_z_min = sqrt(2 g h_min) = 0.1981 m/s
if v_z' < v_z_min: v_z' = 0, z = R, then classify (Sliding/Rolling/...)
N_max   = 10 slate impacts per airborne sequence; on the 10th set v_z' = 0 (hard safety cap)
```

- pooltool uses `min_bounce_height = 0.005` m (5 mm). Alciatore TP B.10 stops bouncing only when **both** hold: the horizontal hop distance is below 0.1 in (2.5 mm) **and** the bounce angle is below 1 deg.
- 2 mm is below what can be seen, and about the scale of cloth compliance, where a rigid-bounce model stops being meaningful anyway.

### C.5 Parameters for Part C

| Parameter | Default | Range | Sources |
|---|---|---|---|
| `e_slate` | 0.6 | 0.5-0.7 | Alciatore physical properties (0.5-0.7), TP B.10 (0.6); pooltool 0.5 |
| friction during bounce | `mu_s` | - | TP B.10, pooltool |
| `h_min` | 2 mm | 1-5 mm | TUNING (pooltool 5 mm) |
| `N_max` | 10 | - | TUNING |
| `e_pinch` | 0.2 | 0-0.4 | TUNING (B.8.2) |

---

## Open questions / calibration backlog

1. **Restitution vs efficiency cue model** (B.5): about 14 % difference in ball speed at maximum English. Resolve with measured spin-to-speed data (Alciatore high-speed video, or our own 240+ fps capture).
2. **`lambda(theta)` schedule and `e_pinch`** for masse and jump (B.8.2) are heuristics. Calibrate curve shapes and hop heights against masse and jump video.
3. **Cloth presets** (A.9) are estimates inside published ranges. Calibrate `mu_r` with the stopping-time test from TP B.2: `mu_r = 2 L / (g T^2)` for a ball rolling a distance L in time T. Calibrate `mu_s` from draw/stun distances, and `alpha_sp` from spin-down video, on a real Simonis 860 table and a bar table.
4. **Speed dependence** of `mu_s` (Witters & Duymelinck) and possibly of `e_slate` and `e_tip` (no data). The baseline keeps them constant.
5. **Squirt effect on spin** (Alciatore TP B.7) and **"ball turn"** of rolling balls with side spin (TP B.2): small effects, not in the baseline.
6. **Combined slip-and-spin contact friction** (spin torque reduced while sliding) would be more accurate but breaks polynomial closure. The baseline keeps L&G's decoupled model.
7. **Chalk model** (B.4): no primary data for `mu_tip` of worn or unchalked tips.
8. **Squirt with combined side and vertical offset** (B.7): the baseline A.31 formula ignores `b`. The isotropic end-mass generalization (DERIVED in B.7) gives 6-8 % less horizontal squirt for typical draw/follow-with-side offsets. It also predicts a small vertical squirt. Decide with high-speed video of side-plus-draw shots.
9. **Residual spin after rolling stops** (A.6, A.7, T-A10): Mathavan et al. (2009) report never seeing a ball keep spinning about the vertical axis after its linear motion ended. Their suspected cause is a disk-like coupling of sidespin and rolling in the cloth, which the decoupled L&G model does not have. With `alpha_sp = 10 rad/s^2` this only matters for slow balls carrying a lot of side spin. Check it against video before adding any coupling.

---

## References

- W. Leckie, M. Greenspan, "An Event-Based Pool Physics Simulator", Advances in Computer Games (ACG 2005), LNCS 4250, Springer 2006. https://link.springer.com/chapter/10.1007/11922155_19
- W. Leckie, M. Greenspan, "Pool Physics Simulation by Event Prediction 1: Motion Transitions", ICGA Journal 28(4), 2005. https://journals.sagepub.com/doi/abs/10.3233/ICG-2005-28403 ; "... 2: Collisions", ICGA Journal 29(1), 2006. https://journals.sagepub.com/doi/10.3233/ICG-2006-29103
- E. Kiefl, "The physics of pool/billiards" (pooltool theory), 2020. https://ekiefl.github.io/2020/04/24/pooltool-theory/ ; pooltool source (`pooltool/physics/evolve/__init__.py`, `physics/resolve/stick_ball/instantaneous_point/__init__.py`, `physics/resolve/stick_ball/squirt.py`, `physics/resolve/ball_table/frictional_inelastic/__init__.py`, `objects/ball/params.py`, `objects/cue/datatypes.py`). https://github.com/ekiefl/pooltool
- J. Zitelli, PoolPhysics (L&G-based port; parameters mu_s = 0.21, mu_r = 0.016, mu_sp = 0.044). https://github.com/jzitelli/PoolPhysics
- D. Alciatore, technical proofs (https://billiards.colostate.edu, mirror https://drdavepoolinfo.com/technical_proofs/new/):
  - TP A.18 (stun/roll distances)
  - TP A.19 (masse aiming, Coriolis)
  - TP A.25 (SRF vs percent English)
  - TP A.30 (cue tip offset, cue weight, speed -> ball speed and spin)
  - TP A.31 (physics of squirt)
  - TP B.2 (rolling resistance, spin resistance, ball turn)
  - TP B.7 (squirt effect on spin)
  - TP B.10 (draw-shot cue-elevation effects / bounce model)
  - TP B.20 (break forces, contact time)
- D. Alciatore FAQ pages:
  - physical properties: https://drdavepoolinfo.com/faq/physics/physical-properties/
  - typical ball speeds: https://drdavepoolinfo.com/faq/speed/typical/
  - tip size and shape: https://drdavepoolinfo.com/faq/cue-tip/size-and-shape/
  - cue-ball hop on the break: https://drdavepoolinfo.com/faq/break/ball-hop/
  - jump cue: https://drdavepoolinfo.com/faq/cue/jump/
  - cloth effects: https://drdavepoolinfo.com/faq/table/cloth-effects/
  - maximum spin: https://drdavepoolinfo.com/faq/sidespin/maximum/
- S. Mathavan, M. R. Jackson, R. M. Parkin, "Application of high-speed imaging to determine the dynamics of billiards", Am. J. Phys. 77(9), 788 (2009). https://pubs.aip.org/aapt/ajp/article/77/9/788/235539 (pdf mirror: https://drdavepoolinfo.com//physics_articles/ajp_09_hsv_article.pdf)
- J. Witters, D. Duymelinck, "Rolling and sliding resistive forces on balls moving on a flat surface", Am. J. Phys. 54(1), 80 (1986). https://ui.adsabs.harvard.edu/abs/1986AmJPh..54...80W/abstract
- H.-C. Kim, "Motions of a billiard ball after a cue stroke", arXiv:2104.11232 (2021). https://arxiv.org/abs/2104.11232
- R. Shepard, "Everything you always wanted to know about cue ball squirt, but were afraid to ask" (2001), cited via TP A.31.
- W. C. Marlow, "The Physics of Pocket Billiards" (1995), cited via Mathavan et al. 2009.
- G.-G. Coriolis, "Theorie mathematique des effets du jeu de billard" (1835), cited via TP A.19 and Kim 2021.
- I. Han, "Dynamics in carom and three cushion billiards", J. Mech. Sci. Technol. 19(4), 2005. Background only; no formula here depends on it.
- WPA, "Recommended Equipment Specifications" (balls sec. 16, cue sticks sec. 17, cloth sec. 12). https://wpapool.com/wp-content/uploads/2024/01/RECOMMENDED-EQUIPMENT-SPECIFICATIONS.pdf
  - On the lost plus-minus glyph in that document, see also the AzBilliards thread "WPA Ball Size Regulations - is there a mistake???" (supporting evidence only). https://forums.azbilliards.com/threads/wpa-ball-size-regulations-is-there-a-mistake.543808/
- pooltool source checked for this verification (main branch, 2026-09):
  - `physics/resolve/stick_ball/instantaneous_point/__init__.py`: `cue_strike`, offset conversion, 2D/3D variants.
  - `physics/resolve/stick_ball/squirt.py`: sign convention.
  - `physics/resolve/ball_table/frictional_inelastic/__init__.py`: stick/slip branches, `min_bounce_height = 0.005`.
  - `objects/ball/params.py`: `u_s = 0.2`, `u_r = 0.01`, `u_sp_proportionality = 10*2/5/9`, `e_t = 0.5`, `g = 9.81`.
- D. Alciatore, TP A.18 values reproduced in T-A3/T-A4. The overspin-follow values have a sign issue; see Implementation note 15.

---

## Implementation notes & pitfalls

1. **Segment representation.** Store per ball `{state, t0, r0, v0, w0, tau_end}` plus cached coefficients:
   - position: `r(tau) = r0 + v0 tau + A2 tau^2`
     - Sliding: `A2 = -(1/2) mu_s g u_hat0`
     - Rolling: `A2 = -(1/2) mu_r g v_hat0`
     - Airborne: `A2 = -(1/2) g z_hat`
   - velocity: `v(tau) = v0 + 2 A2 tau`
   - horizontal spin: `w_h(tau) = w_h0 + Wdot tau`
     - Sliding: `Wdot = (5 mu_s g / (2R)) z_hat x u_hat0`
     - Rolling: `w_h = z_hat x v / R`
   - `w_z(tau)` evaluated with the clamp.
   
   Always evaluate in **local time** `tau = t - t0`, never with global polynomials. That keeps precision over long shots.
2. **Snap at transitions.** Set the defining quantity exactly at every transition:
   - Sliding -> Rolling: `w_h := z_hat x v / R`.
   - Rolling -> end: `v := 0`, `w_h := 0`.
   - Spinning -> end: `w_z := 0`.
   - After a slate stick branch: `u := 0`.
   
   Without snapping, round-off produces micro-sliding segments with `tau_end ~ 1e-15 s`.
3. **Sign conventions.** Use the vector formula `u = v + R z_hat x w` everywhere. Right English is `a > 0` and `w_z > 0` here; pooltool uses the opposite sign for `a`. pooltool takes `theta` and `phi` in degrees, while the core takes radians.
4. **Spin units.** Parametrize spin-down by `alpha_sp` [rad/s^2]. Do not import `mu_sp = 0.044` from L&G ports: it gives about 38 rad/s^2, roughly 4x the measured decay.
5. **Slate impacts never change `w_z`** (pooltool stick-branch pitfall, C.3). Test T-C7 guards this.
6. **Strike at t = 0 includes the slate.** The downward velocity of an elevated-cue strike must be resolved inside the strike routine (B.8.3). Do not schedule it as a separate event at the same timestamp, which risks zero-time event loops and ordering ambiguity.
7. **Squirt is applied exactly once**, and not in the miscue branch. Aim assist and the AI must model squirt as well: the ball leaves at `phi + alpha_sq` for a level cue.
8. **Zeno bouncing.** Always apply `v_z_min` and `N_max` (C.4).
9. **Degenerate divisions.**
   - `u_hat` when `|u| < eps_v`: classify as rolling instead.
   - `v_hat` when `|v| < eps_v`: classify as spinning or stationary.
   - `t_hat` in the miscue branch when `d` is parallel to `n`: that cannot happen for `rho > rho_max > 0`, but guard it.
   - `sqrt(1 - rho^2)` for `rho -> 1`: reject `rho >= 0.95` as invalid input.
10. **Determinism (AI planning, replays, networking).**
    - No randomness inside `BilliardsCore`; the miscue "chaos" and human stroke error come from the game layer as seeded inputs.
    - Compile without `-ffast-math` / `/fp:fast`.
    - Use the same evaluation order on all platforms.
11. **Unreal boundary (left-handed, Z-up, centimeters).** Convert positions with a mirror, e.g. `x_UE = 100 x`, `y_UE = -100 y`, `z_UE = 100 z` (`P = diag(1, -1, 1)`). Angular velocity is a pseudovector: `w_UE = det(P) P w = (-w_x, w_y, -w_z)`. For ball orientation, integrate the quaternion in the core frame and convert the rotation matrix as `R_UE = P R P`. That avoids handedness sign bugs in rolling textures.
12. **Energy sanity check (debug builds).** Kinetic energy `(1/2) m |v|^2 + (1/2) I |w|^2` (plus `m g (z - R)` when airborne) must never increase inside a segment or across a cue-free event.
13. **Cue-body geometry** (minimum elevation to clear rails and balls, shaft touching the cloth at low `b`) belongs to the cue-placement spec. This spec assumes the requested `(theta, a, b)` is physically reachable.
14. **Calibration harness.** Build a small CLI in the CMake test tree that prints stopping distance, stun distance, squirt angle and hop height for given presets, so tuning can be compared directly with video measurements (Open questions 1-3).
15. **Do not "fix" follow shots against TP A.18.** For overspin (follow with `b > 2/5`), TP A.18 Eq. 4 keeps the decelerating sign `d = v t - (1/2) mu g t^2`. During that phase friction actually **accelerates** the ball, so its follow distances with `b/R = 0.5` are too short. For example it prints 0.207 ft at 3 mph, where this model correctly gives 0.2226 ft (0.06784 m; T-A5 scaled by `v^2`). TP A.18's stun-drag, draw and under-spin follow values (`b/R <= 0.4`) are consistent with this spec (T-A3, T-A4).

---

## Test cases

Common constants unless stated otherwise:
- ball and cloth: `g = 9.80665`, `R = 0.028575`, `m = 0.170`, `mu_s = 0.2`, `mu_r = 0.010`, `alpha_sp = 10`
- slate: `e_slate = 0.6`, `h_min = 2 mm` (`v_z_min = 0.1980571`), `lambda = 0`
- cue: squirt off (`alpha_sq = 0`), `M19 = 0.5386409 kg` (19 oz), `phi = 0`, `r0 = (0, 0, R)`

Tolerances:
- "exact": the listed decimals are **rounded** values from a double-precision reference. Two independent reference scripts agree to about 1e-12. Assert `|x - listed| <= 1 unit in the last listed digit`; for example 0.2913475 means +-1e-7. Do **not** use 1e-9 relative against the printed numbers: rounding alone gives up to about 4e-6 relative (e.g. -0.0097048).
- Closed-form identities given in parentheses, e.g. `v = 5/7 v0` or `x = 12 v0^2/(49 mu_s g)`: assert at 1e-9 relative (or 1e-12 absolute near 0), computing the right-hand side in the test.
- "pub": for comparisons with rounded published values. Assert `|x - published| <= 1 unit in the last published digit`, which is about 1e-3 relative for most values. A flat 1e-3 relative would wrongly fail e.g. TP B.10's 0.106 ft (model 0.10582 ft).

### A - Motion on cloth

| ID | Input | Expected | Tol |
|---|---|---|---|
| T-A1 Rolling stop | Rolling, `v0 = (1,0,0)`, `w_z = 0` | `tau_roll = 10.197162 s`; `x(tau_roll) = 5.098581 m`; then Stationary | exact |
| T-A2 Stun shot | Sliding, `v0 = (2,0,0)`, `w0 = 0` | `tau_slide = 0.2913475 s`; `x = 0.4994528 m` (= 12 v0^2/(49 mu_s g)); `v = (1.4285714,0,0)` (= 5/7 v0); `w_y = 49.99375 rad/s` (= v/R); state Rolling | exact |
| T-A3 Alciatore TP A.18 stun distances | same as A2 with v0 = 3, 7, 12 mph (1.341120, 3.129280, 5.364480 m/s) | sliding distance 0.22458, 1.22271, 3.59327 m (published 0.737, 4.012, 11.789 ft) | pub |
| T-A4 Draw b = -0.5 | Sliding, `v0 = (2,0,0)`, `w0 = (0,-87.489064,0)` | `u0 = (4.5,0,0)`; `tau_slide = 0.6555319 s`; `x = 0.8896504 m`; `v = (0.7142857,0,0)`. Stun instant (`w_y = 0`) at `t = 0.5098581 s`, `x = 0.7647872 m`, `v_x = 1.0000000`. Published cross-check, same shot at 3 mph (1.34112 m/s), TP A.18 `b/R = -0.5`: stun distance 0.34389 m = 1.1282 ft (published 1.128 ft), sliding distance 0.40003 m = 1.3124 ft (published 1.312 ft) | exact / pub |
| T-A5 Follow b = +0.5 | Sliding, `v0 = (2,0,0)`, `w0 = (0,+87.489064,0)` | `u0 = (-0.5,0,0)`; `tau_slide = 0.0728369 s`; `x = 0.1508764 m`; `v = (2.1428571,0,0)` (friction accelerates the ball; do **not** compare with TP A.18's 0.207 ft, see Implementation note 15) | exact |
| T-A6 Swerve parabola | Sliding, `v0 = (2,0,0)`, `w0 = (30,0,0)` | `u0 = (2, 0.85725, 0)`; `tau_slide = 0.3169828 s`; `r(tau_slide/2) = (0.2943411, -0.0097048)`; `r(tau_slide) = (0.5433990, -0.0388191)`; `v(tau_slide) = (1.4285714, -0.2449286)`; `w(tau_slide) = (8.5714286, 49.99375, 0)`; `u(tau_slide) = 0` | exact |
| T-A7 Invariant (property) | 10^4 random sliding states, `\|v\| <= 12`, `\|w\| <= 500` | `v(tau_slide) = (5/7) v0 - (2/7) R z_hat x w0`; `\|u(tau)\|` linear in tau; `u_hat` constant | 1e-12 abs |
| T-A8 Spinning | Spinning, `w_z0 = 20` | `w_z(1 s) = 10`; `tau_spin = 2.000000 s`; then Stationary | exact |
| T-A9 Rolling, small side spin | Rolling, `v0 = (1,0,0)`, `w_z0 = 5` | `w_z(0.5 s) = 0` and stays 0; at `t = 10.197162 s` -> Stationary (no Spinning segment emitted) | exact |
| T-A10 Rolling, large side spin | Rolling, `v0 = (0.1,0,0)`, `w_z0 = 20` | Rolling ends at `1.0197162 s` with `w_z = 9.8028379`; Spinning; Stationary at `t = 2.000000 s` | exact |
| T-A11 Classification | `(v, w) = ((1,0,0),(0,1/R,0))` -> Rolling; `((1,0,0),0)` -> Sliding; `(0,(0,0,3))` -> Spinning; `(0,0)` -> Stationary; `v_z = +0.5` -> Airborne | as listed | - |
| T-A12 Energy (property) | random states in every on-cloth state, sample 100 times | kinetic energy non-increasing | 1e-12 rel |

### B - Cue strike

| ID | Input | Expected | Tol |
|---|---|---|---|
| T-B1 L&G/pooltool parity | `V = 2`, `theta = 0`, `a = b = 0`, `M19`, `e_tip = 1` | `v = (3.0404167,0,0)`, `w = 0` | exact |
| T-B2 TP A.30 limits | `e_tip = 1`, `rho = 0`, `m/M = 1/3` | `v/V = 1.5`; with `M -> inf`: `v/V = 2` | exact |
| T-B3 Center, leather | as B1 with `e_tip = 0.75` | `v = (2.6603646,0,0)` | exact |
| T-B4 Natural roll | `e_tip = 0.75`, `b = 0.4` | `v = (2.0400917,0,0)`, `w = (0,71.394286,0)`, `u = 0` -> Rolling | exact |
| T-B5 Max draw | `e_tip = 0.75`, `b = -0.5` | `v = (1.8035574,0,0)`, `w_y = -78.895775`, `SRF = 1.25` | exact |
| T-B6 Right English | `e_tip = 0.75`, `a = 0.3` | `v = (2.2718287,0,0)`, `w = (0,0,+59.628049)` | exact |
| T-B7 Azimuth | `e_tip = 0.75`, `a = 0.3`, `b = 0.2`, `phi = pi/2` | `v = (0, 2.1333540, 0)`, `w = (-37.329028, 0, 55.993542)` | exact |
| T-B8 Squirt angle | `alpha_sq(a = 0.5, m/m_e = 15)`, `(0.5, 40)`, `(0.5, 20)`, `(0.25, 20)` | 3.4657, 1.4463 (TP A.31: 3.466, 1.446), 2.7094, 1.4850 deg | pub / exact |
| T-B9 Squirt in strike | `V = 2`, `a = 0.5`, `m/m_e = 20`, `e_tip = 0.75` | `v = (1.8015412, 0.0852558, 0)` (left); angle +2.7094 deg; `\|v\|` equals the no-squirt value 1.8035574 | exact |
| T-B10 Miscue boundary | `mu_tip = 0.6`, `V = 2`, `e_tip = 0.75`, `M19` | `rho_max = 0.5144958`. `a = 0.514`: grip, `\|v\| = 1.7712`. `a = 0.515`: miscue, `\|v\| = 1.7700`. Continuity: at `a = rho_max (1 -+ 1e-9)` both branches give `\|v\| = 1.7700245`, `w_z = 79.6737`, `V' = 1.4414`. `a = 0.6`: `v = (1.7542, 0.1815, 0)`, direction +5.906 deg, `w_z = 79.385` | 1e-4 |
| T-B11 Tip conversion | cue-axis offset 2/3, `r_tip = R/3` | contact offset 0.5 | exact |
| T-B12 Separation | `e_tip = 0.73`, 19 oz; `e_tip = 1`, `m/M = 1/3` | `rho_sep = 0.6198`; 0.7303 (TP A.30: 0.73) | 1e-4 |
| T-B13 Break hop | `V = 8.5`, `theta = 5 deg`, `b = 0.1`, `M = 0.5953400` (21 oz), `e_tip = 0.85` | after tip: `v = (11.953110, 0, -1.045762)`, `w = (0,104.976106,0)`. After slate (slip branch): `v = (11.618466, 0, 0.627457)`, `w = (0,134.253771,0)`, Airborne. First landing after 0.1279656 s at `dx = 1.486764 m`; apex 0.0200732 m | exact |
| T-B14 Jump shot | `V = 4`, `theta = 50 deg`, center, `M = 0.2551457` (9 oz), `e_tip = 0.85` | after tip: `v = (2.854629, 0, -3.402014)`. After slate (stick branch): `v = (2.039021, 0, 2.041209)`, `w = (0,71.356807,0)`, `u_h = 0`. Apex 0.212434 m; landing after 0.4162907 s at `dx = 0.8488254 m` | exact |
| T-B15 Masse direction (Coriolis) | `V = 2.5`, `theta = 75 deg`, `a = 0.4`, `b = -0.3`, `M19`, `e_tip = 0.73`, `lambda` in {0, 1} | `J = 0.378876` (lambda 0), `0.729707` (lambda 1). Velocity at start of Rolling = `L_c`: `(-0.065557, -0.615069, 0)` (lambda 0), `(-0.126261, -1.184610, 0)` (lambda 1). Direction -96.0839 deg in **both** = `atan2(-a sin(theta), cos(theta) + b)` | 1e-6 |
| T-B16 Level side spin: no swerve | `V = 3`, `theta = 0`, `a = 0.4`, `m/m_e = 20`, `e_tip = 0.73` | `v = (3.0227863, 0.1199320, 0)`, `w = (0,0,105.867527)`, `u` parallel to `v`, so the path is straight at +2.2721 deg until rolling | exact |
| T-B17 Elevated swerve | `V = 2`, `theta = 10 deg`, `a = 0.4`, `m/m_e = 20`, `e_tip = 0.73`, `M19` | after tip and slate: `v = (1.875138, 0.056234, 0.209961)`, `w = (10.180472, 9.574597, 69.506108)`. Velocity at start of Rolling = `(1.417554, -0.042949, 0)`: squirts left, curves to a net -1.7354 deg (right) | 1e-6 |
| T-B18 Invalid input | `rho = 1.0` (and `rho = 0.96`), `theta = pi/2`, `V < 0` | error code, no state change | - |
| T-B19 Coriolis vs TP A.19 published | `delta = atan2(-a sin(theta), cos(theta) + b)` with Alciatore's swerve examples, `a = 0.5 cos(45 deg) = 0.353553`: draw `b = -0.353553`, `theta = 4 deg`; follow `b = +0.353553`, `theta = 3 deg` | `\|delta\|` = 2.1931 deg and 0.7840 deg, both to the right (published 2.193 and 0.784 deg) | pub |
| T-B20 Miscue upward guard (B.8.3) | level cue, `a = 0`, `b = -0.6` (miscue), `mu_tip = mu_tip_k = 0.6`, `e_tip = 0.75`, M19, `V = 2` | `p_hat = (0.9947, 0, +0.1029)` (`w_n < 0`); after the tip `v = (1.7542, 0, 0.1815)`, `w_y = -79.385`. Since `v_z = 0.1815 < v_z_min`, it is set to 0 and the state is on-cloth (Sliding) | 1e-3 |

### C - Airborne and slate

| ID | Input | Expected | Tol |
|---|---|---|---|
| T-C1 Ballistics | `z0 = R`, `v0 = (0,0,1)` | apex 0.0509858 m above rest height; `tau_land = 0.2039432 s` | exact |
| T-C2 Multi-bounce to rolling | `z0 = R`, `v0 = (1,0,1)`, `w0 = 0` | impacts at `t = 0.203943, 0.326309, 0.399729, 0.443780 s`, at `x = 0.203943, 0.291347, 0.343790, 0.375256 m`. First impact is the stick branch: `v_x = 0.714286`, `w_y = 24.996875`, unchanged afterwards. `v_z` out: 0.6, 0.36, 0.216, then 0.1296 < `v_z_min` -> 0. State Rolling from `t = 0.443780 s` | 1e-6 |
| T-C3 Alciatore TP B.10 bounce | pre-impact `v = (4.985123, 0, -0.436142)`, `w = (0, -218.9125, 0)` (11.194 mph at 5 deg down, 34.841 rps backspin) | slip branch; post `v = (4.845558, 0, 0.261685)`: 10.855 mph, 3.091 deg; backspin 32.897 rps (`w_y = -206.7020`); flight distance 0.2586 m (0.848 ft). Continue the free bounce sequence (flight, then C.3, ignore `v_z_min`). Bounces 2-5 must reproduce TP B.10's table: speed 10.658 / 10.542 / 10.473 / 10.432 mph, spin 31.731 / 31.031 / 30.612 / 30.360 rps, angle 1.889 / 1.145 / 0.692 / 0.417 deg, hop 0.500 / 0.297 / 0.177 / 0.106 ft. Same pre-impact speed and spin at 12 deg down: bounce 1 gives 10.300 mph, 30.205 rps, 7.792 deg, 1.906 ft; bounce 2 gives 9.794 mph, 27.423 rps, 4.908 deg, 1.093 ft | pub |
| T-C4 Stick/slip continuity | choose `\|u\| = (7/2) mu_s (1 + e) w_n` | both branches give identical `(v', w')` | 1e-12 |
| T-C5 Zeno cap | `v_z_min = 0`, `e_slate = 0.999` | stops at `N_max = 10` impacts; `v_z = 0` afterwards | - |
| T-C6 Invariant through bounce (property) | random `(v_h, w, w_n)` | `L_c = (5/7) v_h - (2/7) R z_hat x w_h` unchanged | 1e-12 abs |
| T-C7 w_z preserved (property) | random states incl. `w_z != 0`, both branches | `w_z' == w_z` | exact |
| T-C8 Airborne spin constant | any airborne segment | `w(tau) = w0`; horizontal velocity constant | exact |

---

## Verification log

Adversarial verification, 2026-09-25.

**Method**
- Every equation in Parts A-C was re-derived from Newton-Euler for a rigid sphere on a plane with Coulomb friction, and from impulse-momentum plus Newton restitution for the cue.
- All signs were checked against Section 0 (z up, bed at z = 0, `u = v + w x (-R z_hat) = v + R z_hat x w`).
- Every numeric test value was recomputed with an independent Python re-implementation of this spec.
- Parameters were checked against at least two sources: Alciatore's physical-properties page and TP A.18, A.19, A.30, A.31, B.2, B.10 and B.20; Mathavan et al. 2009 (full text); Kim 2021 (full text); the Witters & Duymelinck abstract; the WPA equipment specs; the pooltool source; the Zitelli PoolPhysics defaults.

**Confirmed without change**
- Slip-velocity definition and sign check.
- Sliding equations of motion; `du/dt = -(7/2) mu_s g u_hat` (constant slip direction); closed forms for r, v and w in every state.
- Transition times; the `w_z` clamp.
- Coriolis invariant proof, and its invariance through sliding, flight and slate impacts. Numerically 6e-15 over 10^4 random states.
- Cue frame (right-handed); contact point Q; `Q x d = R (a e_u - b e_r)`.
- General and normal-case impulse formulas: identical to pooltool's `cue_strike` at `e_tip = 1`, and to Kim 2021 Eq. 83.
- Spin, SRF = 2.5 rho, natural roll at b = 0.4, separation condition.
- Friction-cone miscue limit; continuity of the miscue branch; squirt formula and table (TP A.31).
- Pinch model at lambda = 0 and 1; slate impact stick/slip, including continuity.
- Every expected value in T-A1..A12, T-B1..B17 and T-C1..C4: all agree with the recomputation to the printed digits, except the T-A6 rounding below.
- TP B.10 reproduced for all 5 published bounces at 5 deg and both at 12 deg. TP A.18 stun and draw distances reproduced. TP A.19 published angles reproduced.

**Corrections made**
1. **Test tolerances were not assertable.**
   - "exact" was defined as 1e-9 relative, but the expected values are printed to about 7 significant digits, so rounding alone is up to 4e-6 relative. Every exact test would have failed against the printed numbers. Redefined as "+-1 unit in the last listed digit", with closed-form identities still at 1e-9.
   - The "pub" tolerance was redefined the same way; a flat 1e-3 relative fails for 3-digit published values such as 0.106 ft.
2. **T-A6 rounding.** `r(tau_slide/2).x` = 0.29434115 m rounds to **0.2943411**, not 0.2943412.
3. **WPA ball-diameter tolerance.** It is +-0.005 in, not +0.005 in. The PDF lost the plus-minus glyph; its sec. 7 prints "63 1/2 % (+1 %)" for a 62.5-64.5 % range. So R lies in [28.511, 28.639] mm, not [28.575, 28.639] mm.
4. **TP B.10 bounce cutoff.** Alciatore stops bouncing when the hop distance is below 0.1 in **and** the angle is below 1 deg, not "or". The distance is horizontal.
5. **Squirt (B.7).** "Only `a` enters; `b` does not" is not a property of the end-mass model. TP A.31 is derived for `b = 0`. Under its own assumptions (DERIVED here), `b != 0` gives 6-8 % less horizontal squirt plus a small vertical component. The baseline stays with A.31 for pooltool parity; the approximation is labelled and Open question 8 was added.
6. **Wording of `L_c` (A.5).** `L_c` is not proportional to the angular momentum about the contact point. It is that vector rotated by 90 deg: `L_c = -(5/(7 m R)) z_hat x H_c,h`. The statement was corrected; the invariance proof was already right.
7. **pooltool slate stick branch (C.3).** The 2/7 scaling of `w_z` is now confirmed from the source, with the exact update line cited, instead of "appears to". pooltool's threshold is named `min_bounce_height = 0.005`.
8. **pooltool offset mapping (B.3).** pooltool's user-facing `cue.b` already equals this spec's `b`; it converts to world coordinates internally. Only `a` flips sign. The old text implied that pooltool's `b` is world-vertical, which is true only of L&G's original formula.
9. **Invalid-input range.** B.1 said `rho < 1`, but Implementation note 9 rejects `rho >= 0.95`. B.1 now uses `rho_valid = 0.95`, and T-B18 adds `rho = 0.96`.
10. **Upward miscue impulse (B.8.3).**
    - The `w_n >= 0` annotation was only true in the grip branch; a draw miscue gives `w_n < 0`, i.e. an upward kick.
    - Added a `w_n <= 0` branch with the same `v_z_min` guard as C.4, so the ball does not make sub-2-mm micro-hops.
    - New test T-B20.
11. **Source attributions (A.9, A.2).**
    - 0.21 / 0.016 / 0.044 are verified defaults of Zitelli's L&G port; the values in the L&G papers themselves were not checked. Zitelli's own R = 0.02625 m was noted.
    - The pooltool spin constant "evidently chosen with g close to 9" was softened. pooltool uses g = 9.81, which gives 10.9 rad/s^2, about 9 % above the TP B.2 value.
12. **Kim 2021.**
    - Eq. 83 is the leading order in the small end-mass ratio; this qualifier was added.
    - Kim's miscue condition is quoted from Alciatore TP 2.1.
    - The restitution-vs-efficiency gap is 13.6 % at eta = 0.87 (which equals e = 0.677), and 10.0 % at the default e = 0.73.
13. **TP A.18 overspin pitfall (Implementation note 15, T-A5).** TP A.18 uses a decelerating sign even when friction accelerates an overspinning ball, so its `b/R = 0.5` follow distances are about 7 % short: 0.207 ft published versus 0.2226 ft correct at 3 mph. This spec is right; the note stops implementers from "fixing" it. The TP A.18 draw values were added to T-A4 as a published cross-check.
14. **New published-data tests.** T-C3 was extended with TP B.10 bounces 2-5 and the 12 deg case. T-B19 checks the TP A.19 swerve angles, 2.193 deg and 0.784 deg.
15. **Notes added.**
    - B.8.4: real masse shots fall slightly short of the Coriolis angle (TP A.19).
    - Open question 9: Mathavan et al. saw no residual vertical-axis spin after linear motion stopped.

**Remaining doubts, not errors**
- The cue impulse is assumed to act along the cue axis, with squirt added afterwards as a rotation. This follows L&G, pooltool and Alciatore, but it is not a self-consistent grip solution; see Kim 2021 for the full treatment.
- The spin reduction from squirt (TP B.7) is ignored.
- The miscue branch applies restitution along `p_hat` rather than along the normal. It is an approximation, chosen for continuity.
- `lambda(theta)`, `e_pinch`, the cloth presets, the jump-cue mass and the constant `e_slate` at high normal speeds (T-B14 hops 21 cm) are TUNING values that need video calibration.
