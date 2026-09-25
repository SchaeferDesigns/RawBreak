"""Reference computations for physics-collisions.md (RawBreak). Double precision numpy."""
import math
import numpy as np

R = 0.028575          # m  (2.25 in / 2)
m = 0.170097          # kg (6 oz)
g = 9.81              # m/s^2
I = 0.4 * m * R * R
Z = np.array([0.0, 0.0, 1.0])

A_MU, B_MU, C_MU = 9.951e-3, 0.108, 1.088


def mu_alciatore(vrel):
    return A_MU + B_MU * math.exp(-C_MU * vrel)


def ball_ball(r1, v1, w1, r2, v2, w2, e=0.95, mu_fn=mu_alciatore, m1=m, m2=m, Rb=R):
    n = (r2 - r1) / np.linalg.norm(r2 - r1)
    vn = np.dot(v1 - v2, n)
    assert vn > 0, "not approaching"
    s = (v1 - v2) + Rb * np.cross(w1 + w2, n)
    st = s - np.dot(s, n) * n
    smag = np.linalg.norm(st)
    minv = 1.0 / m1 + 1.0 / m2
    Jn = (1 + e) * vn / minv
    I1 = 0.4 * m1 * Rb * Rb
    I2 = 0.4 * m2 * Rb * Rb
    kt = minv + Rb * Rb * (1.0 / I1 + 1.0 / I2)   # tangential inverse eff. mass (=7/m equal balls)
    if smag > 1e-12:
        mu = mu_fn(smag)
        Jt = min(mu * Jn, smag / kt)
        that = st / smag
    else:
        mu, Jt, that = mu_fn(0.0), 0.0, np.zeros(3)
    P1 = -Jn * n - Jt * that
    v1n = v1 + P1 / m1
    v2n = v2 - P1 / m2
    dw1 = np.cross(Rb * n, P1) / I1
    dw2 = np.cross(-Rb * n, -P1) / I2
    return v1n, w1 + dw1, v2n, w2 + dw2, dict(Jn=Jn, Jt=Jt, mu=mu, smag=smag, stick=(Jt < mu * Jn))


def cut_setup(v, phi_deg, wx=0.0, wy=0.0, wz=0.0):
    """CB moving +x at speed v hits OB; line of centers at angle phi (cut angle) from +x (OB to the +y side... )."""
    phi = math.radians(phi_deg)
    r1 = np.array([0.0, 0.0, R])
    n = np.array([math.cos(phi), math.sin(phi), 0.0])
    r2 = r1 + 2 * R * n
    return r1, np.array([v, 0.0, 0.0]), np.array([wx, wy, wz]), r2, np.zeros(3), np.zeros(3), n


def throw_angle_deg(v2, n):
    # angle between OB velocity and line of centers, signed about +z
    vh = v2.copy(); vh[2] = 0
    ang = math.atan2(np.cross(n, vh)[2], np.dot(n, vh))
    return math.degrees(ang)


def alciatore_theta(v, wx, wz, phi_deg):
    phi = math.radians(phi_deg)
    vrel = math.hypot(v * math.sin(phi) - R * wz, R * wx * math.cos(phi))
    if vrel == 0:
        return 0.0
    mu = mu_alciatore(vrel)
    return math.degrees(math.atan(min(mu * v * math.cos(phi) / vrel, 1 / 7) * (v * math.sin(phi) - R * wz) / (v * math.cos(phi))))


print("=== mu(v) table ===")
for vr in [0.0, 0.1, 0.25, 0.5, 1.0, 2.0, 3.0, 5.0]:
    print(f"  vrel={vr:4.2f}  mu={mu_alciatore(vr):.5f}")

print("\n=== Ball-ball tests ===")
# T1 head-on stun
r1, v1, w1, r2, v2, w2, n = cut_setup(1.0, 0.0)
a = ball_ball(r1, v1, w1, r2, v2, w2, e=0.95)
print("T1 head-on stun e=.95:", a[0], a[2], a[1], a[3])
# T2 head-on rolling
r1, v1, w1, r2, v2, w2, n = cut_setup(1.0, 0.0, wy=1.0 / R)
a = ball_ball(r1, v1, w1, r2, v2, w2, e=0.95)
print("T2 head-on rolling v=1:", "v1'", a[0], "v2'", a[2], "R*w1'", R * a[1], "R*w2'", R * a[3], a[4])
# T3 throw, stun, e=1, various speeds and cut 30
for v in [0.447, 1.341, 3.129]:
    for phi in [10.0, 30.0, 45.0]:
        r1, v1, w1, r2, v2, w2, n = cut_setup(v, phi)
        a = ball_ball(r1, v1, w1, r2, v2, w2, e=1.0)
        th = throw_angle_deg(a[2], n)
        print(f"T3 stun e=1 v={v} phi={phi}: throw={th:.4f} deg  alciatore={alciatore_theta(v,0,0,phi):.4f}  stick={a[4]['stick']} mu={a[4]['mu']:.5f}")
        a = ball_ball(r1, v1, w1, r2, v2, w2, e=0.95)
        th2 = throw_angle_deg(a[2], n)
        print(f"      e=0.95: throw={th2:.4f} deg ; |v2'|={np.linalg.norm(a[2]):.5f}  v1'={a[0]}  Rw1'={R*a[1]} Rw2'={R*a[3]}")
# T4 rolling cut 30 medium
r1, v1, w1, r2, v2, w2, n = cut_setup(1.341, 30.0, wy=1.341 / R)
a = ball_ball(r1, v1, w1, r2, v2, w2, e=1.0)
print("T4 rolling v=1.341 phi=30 e=1: throw", throw_angle_deg(a[2], n), "alc", alciatore_theta(1.341, 1.341 / R, 0, 30))
# NB Alciatore's omega_x is the roll spin about the CB's own lateral axis; in our frame rolling +x is w_y = v/R.
# T5 spin-induced throw straight shot, stun, sidespin R*wz = 0.5 v
for v in [0.447, 1.341, 3.129]:
    r1, v1, w1, r2, v2, w2, n = cut_setup(v, 0.0, wz=0.5 * v / R)
    a = ball_ball(r1, v1, w1, r2, v2, w2, e=1.0)
    print(f"T5 SIT v={v} Rwz=0.5v e=1: throw={throw_angle_deg(a[2], n):.4f} alc={alciatore_theta(v,0,0.5*v/R,0):.4f}  OB Rw={R*a[3]}  CB Rw={R*a[1]}")
# T6 gearing outside english
v = 1.0
phi = 30.0
wz = v * math.sin(math.radians(phi)) / R
r1, v1, w1, r2, v2, w2, n = cut_setup(v, phi, wz=wz)
a = ball_ball(r1, v1, w1, r2, v2, w2, e=0.95)
print("T6 gearing: throw", throw_angle_deg(a[2], n), a[4])
# T7 spin transfer max: straight, stun, huge sidespin low speed -> stick? v=0.2, R wz = 0.2 (100%)
r1, v1, w1, r2, v2, w2, n = cut_setup(0.5, 0.0, wz=0.5 / R)
a = ball_ball(r1, v1, w1, r2, v2, w2, e=0.95)
print("T7 sidespin transfer v=.5 Rwz=.5:", "OB Rw", R * a[3], "CB Rw", R * a[1], a[4])

print("\n=== Airborne ball-ball (3D normal) ===")
r1 = np.array([0.0, 0.0, R + 0.02]); v1 = np.array([2.0, 0.0, -0.5]); w1 = np.zeros(3)
r2 = np.array([0.0, 0.0, R]); n3 = np.array([1.0, 0.0, 0.0])
# place r2 so that contact normal is (cos a, 0, -sin a) with sin a = 0.02/(2R)
dz = 0.02
dx = math.sqrt((2 * R) ** 2 - dz ** 2)
r2 = r1 + np.array([dx, 0.0, -dz])
a = ball_ball(r1, v1, w1, r2, np.zeros(3), np.zeros(3), e=0.95)
print("T8 airborne CB hits OB from above: v1'", a[0], "v2'", a[2], a[4])


# ---------------- Cushion: local frame X along cushion, Y into cushion, Z up -------------
def han(vl, wl, h, e_c, mu_c, Rb=R, mb=m):
    st = (h - Rb) / Rb
    ct = math.sqrt(1 - st * st)
    vX, vY, vZ = vl
    wX, wY, wZ = wl
    vc = vY * ct + vZ * st
    assert vc > 0
    PN = (1 + e_c) * mb * vc
    sX = vX + Rb * wY * st - Rb * wZ * ct
    sS = -vY * st + vZ * ct + Rb * wX
    smag = math.hypot(sX, sS)
    Ib = 0.4 * mb * Rb * Rb
    if smag < 1e-12 or (2 * mb / 7) * smag <= mu_c * PN:
        PX, PS = -(2 * mb / 7) * sX, -(2 * mb / 7) * sS
        stick = True
    else:
        PX, PS = -mu_c * PN * sX / smag, -mu_c * PN * sS / smag
        stick = False
    P = np.array([PX, -PS * st - PN * ct, PS * ct - PN * st])
    v_new = np.array(vl) + P / mb
    rI = Rb * np.array([0.0, ct, st])
    w_new = np.array(wl) + np.cross(rI, P) / Ib
    return v_new, w_new, dict(PN=PN, P=P, stick=stick, vc=vc)


def mathavan(vl, wl, h, e_e, mu_w, mu_s, Rb=R, mb=m, N=4000, rk4=True):
    """Mathavan 2010 in local frame; independent variable P_I; RK4 (or Euler) steps."""
    st = (h - Rb) / Rb
    ct = math.sqrt(1 - st * st)
    Ib = 0.4 * mb * Rb * Rb
    k = 5.0 / (2 * mb * Rb)
    SREG = 1e-6

    def deriv(y):
        vX, vY, wX, wY, wZ = y
        sxI = vX + wY * Rb * st - wZ * Rb * ct
        syI = -vY * st + wX * Rb
        sxC = vX - wY * Rb
        syC = vY + wX * Rb
        sI = math.hypot(sxI, syI)
        sC = math.hypot(sxC, syC)
        # unit slip dirs (zero if rolling)
        cI, sIn = (sxI / sI, syI / sI) if sI > SREG else (0.0, 0.0)
        cC, sCn = (sxC / sC, syC / sC) if sC > SREG else (0.0, 0.0)
        muw = mu_w if sI > SREG else 0.0
        mus = mu_s if sC > SREG else 0.0
        PCfac = st + muw * sIn * ct
        dvX = -(1 / mb) * (muw * cI + mus * cC * PCfac)
        dvY = -(1 / mb) * (ct - muw * st * sIn + mus * sCn * PCfac)
        dwX = -k * (muw * sIn + mus * sCn * PCfac)
        dwY = -k * (muw * cI * st - mus * cC * PCfac)
        dwZ = k * (muw * cI * ct)
        return np.array([dvX, dvY, dwX, dwY, dwZ])

    y = np.array([vl[0], vl[1], wl[0], wl[1], wl[2]], dtype=float)
    assert y[1] > 0
    dP = (1 + e_e) * mb * y[1] / N
    W = 0.0

    def step(y, dP):
        if rk4:
            k1 = deriv(y); k2 = deriv(y + 0.5 * dP * k1); k3 = deriv(y + 0.5 * dP * k2); k4 = deriv(y + dP * k3)
            return y + dP * (k1 + 2 * k2 + 2 * k3 + k4) / 6
        return y + dP * deriv(y)
    # compression
    it = 0
    while y[1] > 0:
        yn = step(y, dP)
        if yn[1] <= 0:
            # bisect the step so vY hits 0
            lo, hi = 0.0, dP
            for _ in range(60):
                mid = 0.5 * (lo + hi)
                if step(y, mid)[1] > 0:
                    lo = mid
                else:
                    hi = mid
            yn = step(y, hi)
            W += 0.5 * hi * (y[1] + max(yn[1], 0.0)) * ct
            y = yn
            y[1] = 0.0 if abs(y[1]) < 1e-12 else y[1]
            break
        W += 0.5 * dP * (y[1] + yn[1]) * ct
        y = yn
        it += 1
        assert it < 100 * N
    Wc = W
    target = e_e ** 2 * Wc
    Wr = 0.0
    it = 0
    while Wr < target:
        yn = step(y, dP)
        dW = 0.5 * dP * (abs(y[1]) + abs(yn[1])) * ct
        if Wr + dW >= target:
            lo, hi = 0.0, dP
            for _ in range(60):
                mid = 0.5 * (lo + hi)
                ym = step(y, mid)
                if Wr + 0.5 * mid * (abs(y[1]) + abs(ym[1])) * ct < target:
                    lo = mid
                else:
                    hi = mid
            y = step(y, hi)
            break
        Wr += dW
        y = yn
        it += 1
        assert it < 100 * N
    return np.array([y[0], y[1], 0.0]), np.array([y[2], y[3], y[4]])


def rebound(vn, wn):
    speed = math.hypot(vn[0], vn[1])
    ang = math.degrees(math.atan2(-vn[1], vn[0]))  # angle from cushion line (X), measured into table
    return speed, ang


h_pool = 0.635 * 2 * R
print("\n=== Cushion geometry ===")
st = (h_pool - R) / R
print(f"h={h_pool:.6f} m  sin(theta)={st:.4f} theta={math.degrees(math.asin(st)):.3f} deg  R*cos(theta)={R*math.sqrt(1-st*st):.6f}")
for frac in [0.625, 0.635, 0.645]:
    hh = frac * 2 * R
    s_ = (hh - R) / R
    print(f"  h/D={frac}: h={hh*1000:.3f} mm sin={s_:.3f} theta={math.degrees(math.asin(s_)):.2f} deg dh={R*math.sqrt(1-s_*s_)*1000:.4f} mm")

print("\n=== Han tests (pool: h=0.635D, e_c=0.85, mu_c=0.2) ===")
# C1 rolling perpendicular v=1: velocity +Y into cushion, rolling -> topspin: rolling along +Y means u = v + w x (-R Z) = 0
# w x (-R Z) = -R (w x Z) = -R(-wX Y + wY X) = R wX Y - R wY X -> vY + R wX = 0 -> wX = -vY/R
def rolling_w(vX, vY):
    # u = 0: vX - R wY = 0, vY + R wX = 0
    return np.array([-vY / R, vX / R, 0.0])

for v in [0.5, 1.0, 2.0, 3.0]:
    vl = np.array([0.0, v, 0.0]); wl = rolling_w(0.0, v)
    vn, wn, info = han(vl, wl, h_pool, 0.85, 0.2)
    print(f"C1 Han rolling perp v={v}: v'={vn}, R*w'={R*wn}, stick={info['stick']} ratio(-vY'/v)={-vn[1]/v:.4f}")
for v in [1.0]:
    vl = np.array([0.0, v, 0.0]); wl = np.zeros(3)
    vn, wn, info = han(vl, wl, h_pool, 0.85, 0.2)
    print(f"C2 Han stun perp v={v}: v'={vn}, R*w'={R*wn}, stick={info['stick']}")
# C3 rolling at 45 deg incidence, v=1
alpha = math.radians(45)
vl = np.array([math.cos(alpha), math.sin(alpha), 0.0]); wl = rolling_w(vl[0], vl[1])
vn, wn, info = han(vl, wl, h_pool, 0.85, 0.2)
print(f"C3 Han rolling 45deg v=1: v'={vn}, R*w'={R*wn}, stick={info['stick']}, speed/angle={rebound(vn,wn)}")
# C4 stun 45 deg with running english (wZ) R wZ = +0.5
vl = np.array([math.cos(alpha), math.sin(alpha), 0.0]); wl = np.array([0.0, 0.0, 0.5 / R])
vn, wn, info = han(vl, wl, h_pool, 0.85, 0.2)
print(f"C4 Han stun 45deg RwZ=+0.5: v'={vn}, R*w'={R*wn}, stick={info['stick']}, speed/angle={rebound(vn,wn)}")
vl = np.array([math.cos(alpha), math.sin(alpha), 0.0]); wl = np.array([0.0, 0.0, -0.5 / R])
vn, wn, info = han(vl, wl, h_pool, 0.85, 0.2)
print(f"C4b Han stun 45deg RwZ=-0.5: v'={vn}, R*w'={R*wn}, stick={info['stick']}, speed/angle={rebound(vn,wn)}")

print("\n=== Mathavan 2010 tests ===")
# Paper setup: snooker M=0.1406 R=0.02625 h=7R/5 e=0.98 mu_w=0.14 mu_s=0.212
Rs, Ms = 0.02625, 0.1406
hs = 1.4 * Rs
for V0 in [0.5, 1.0, 2.0, 3.0]:
    vl = np.array([0.0, V0, 0.0]); wl = np.array([-V0 / Rs, 0.0, 0.0])
    vn, wn = mathavan(vl, wl, hs, 0.98, 0.14, 0.212, Rb=Rs, mb=Ms)
    vn2, wn2 = mathavan(vl, wl, hs, 0.98, 0.14, 0.212, Rb=Rs, mb=Ms, N=5000, rk4=False)
    poly = -0.0877 * V0 ** 2 + 1.131 * V0 - 0.0953
    print(f"M1 snooker rolling perp V0={V0}: vY'={vn[1]:.5f} (euler {vn2[1]:.5f}) ratio={-vn[1]/V0:.4f}  exp-poly={poly:.4f} ratio {poly/V0:.4f}  R*w'={Rs*wn}")
# pool params, rolling 45 deg
for V0, a_deg in [(1.0, 45.0), (1.0, 30.0), (2.0, 60.0)]:
    al = math.radians(a_deg)
    vl = np.array([V0 * math.cos(al), V0 * math.sin(al), 0.0]); wl = rolling_w(vl[0], vl[1])
    vn, wn = mathavan(vl, wl, h_pool, 0.98, 0.14, 0.2)
    vh, wh, _ = han(vl, wl, h_pool, 0.85, 0.2)
    print(f"M2 pool rolling V0={V0} alpha={a_deg}: Mathavan v'={vn} spd/ang={rebound(vn,wn)} R*w'={R*wn}\n      Han(e.85,mu.2) v'={vh} spd/ang={rebound(vh,wh)}")
# pool, rolling perpendicular
for V0 in [0.5, 1.0, 2.0, 3.0]:
    vl = np.array([0.0, V0, 0.0]); wl = rolling_w(0.0, V0)
    vn, wn = mathavan(vl, wl, h_pool, 0.98, 0.14, 0.2)
    print(f"M3 pool rolling perp V0={V0}: vY'={vn[1]:.5f} ratio={-vn[1]/V0:.4f} R*w'={R*wn}")

# Fit e(v) so that Han reproduces Mathavan-2009 poly ratio for rolling perpendicular (pool geometry)
print("\n=== Calibration: e_c(v_perp) for Han to match Mathavan(2009) measured ratio ===")
for V0 in [0.3, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5]:
    target = (-0.0877 * V0 ** 2 + 1.131 * V0 - 0.0953) / V0
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        vn, wn, info = han(np.array([0.0, V0, 0.0]), rolling_w(0.0, V0), h_pool, mid, 0.2)
        if -vn[1] / V0 < target:
            lo = mid
        else:
            hi = mid
    # mathavan equiv
    lo2, hi2 = 0.0, 1.0
    for _ in range(40):
        mid2 = 0.5 * (lo2 + hi2)
        vn, wn = mathavan(np.array([0.0, V0, 0.0]), rolling_w(0.0, V0), h_pool, mid2, 0.14, 0.2, N=800)
        if -vn[1] / V0 < target:
            lo2 = mid2
        else:
            hi2 = mid2
    print(f"  V0={V0}: target ratio={target:.4f}  Han e_c={mid:.4f}   Mathavan e_e={mid2:.4f}")

print("\n=== Slate bounce ===")
def table_bounce(v, w, e_t=0.5, mu_t=0.2):
    vz = v[2]; assert vz < 0
    Jz = -(1 + e_t) * m * vz
    u = np.array([v[0], v[1], 0.0]) + np.cross(w, -R * Z)
    un = np.linalg.norm(u)
    if un > 1e-12:
        Jf = min(mu_t * Jz, (2 * m / 7) * un)
        P = Jz * Z - Jf * u / un
    else:
        P = Jz * Z
    v2 = v + P / m
    w2 = w + np.cross(-R * Z, P) / I
    return v2, w2
z0 = 0.1
vimp = math.sqrt(2 * g * z0)
t = math.sqrt(2 * z0 / g)
vz = vimp
print(f"S1 drop from 0.1 m: impact {vimp:.5f} m/s at t={t:.5f}")
hmin = 0.001
k = 0
while True:
    vz = 0.5 * vz
    hb = vz * vz / (2 * g)
    if hb < hmin:
        print(f"   rebound {k+1} would be {hb*1000:.4f} mm < 1 mm -> land at t={t:.5f}")
        break
    t += 2 * vz / g
    k += 1
    print(f"   bounce {k}: vz={vz:.5f} h={hb*1000:.4f} mm next contact t={t:.5f}")
v2, w2 = table_bounce(np.array([1.0, 0.0, -1.0]), np.array([0.0, -20.0, 0.0]))
print("S2 landing v=(1,0,-1), w=(0,-20,0):", v2, "R*w'", R * w2, "u'", v2[:2] + np.cross(w2, -R * Z)[:2])
v2, w2 = table_bounce(np.array([1.0, 0.0, -1.0]), np.zeros(3))
print("S3 landing v=(1,0,-1), w=0:", v2, "R*w'", R * w2, "u'", v2[:2] + np.cross(w2, -R * Z)[:2])

print("\n=== Edge roll-off ===")
for v0 in [0.0, 0.1, 0.3, 0.5, math.sqrt(g * R)]:
    c = 10 / 17 + 7 * v0 * v0 / (17 * g * R)
    print(f"  v0={v0:.4f}: cos={c:.5f} theta_leave={math.degrees(math.acos(min(c,1))):.3f} deg")
print("  sqrt(gR)=", math.sqrt(g * R))

print("\n=== Hertz (TP B.29 data) ===")
Rm = 1.125 * 0.0254
delta = 2 * Rm * 3.49e-3
F = 2270.0
K = F / delta ** 1.5
print(f"delta={delta:.4e} m  K={K:.4e} N/m^1.5")
meff = m / 2
for vv in [0.5, 1.0, 5.0, 10.0]:
    Tc = 3.2145 * (meff ** 2 / (K ** 2 * vv)) ** 0.2
    dmax = (5 * meff * vv * vv / (4 * K)) ** 0.4
    print(f"  v={vv}: Hertz contact time={Tc*1e6:.1f} us  max compression={dmax*1e6:.1f} um  Fmax={K*dmax**1.5:.0f} N")

# 3-ball Hertz chain elastic (reproduce TP B.29)
def chain(nb=3, v0=1.0, e=1.0, dt=1e-8):
    x = np.array([i * 2 * Rm for i in range(nb)], float)
    v = np.zeros(nb); v[0] = v0
    M = 0.170097
    vin = {}
    t = 0.0
    started = False
    while True:
        F = np.zeros(nb - 1)
        for i in range(nb - 1):
            d = 2 * Rm - (x[i + 1] - x[i])
            dd = v[i] - v[i + 1]
            if d > 0:
                if i not in vin:
                    vin[i] = max(dd, 1e-9)
                # Lankarani-Nikravesh damping
                F[i] = K * d ** 1.5 * (1 + 0.75 * (1 - e * e) * dd / vin[i])
                F[i] = max(F[i], 0.0)
        a = np.zeros(nb)
        for i in range(nb - 1):
            a[i] -= F[i] / M
            a[i + 1] += F[i] / M
        v = v + a * dt
        x = x + v * dt
        t += dt
        if np.any(F > 0):
            started = True
        if started and np.all(F == 0) and all(v[i] <= v[i + 1] + 1e-12 for i in range(nb - 1)):
            break
        if t > 0.01:
            break
    return v, t

vv, tt = chain(3, 1.0, 1.0, 5e-9)
print("TP B.29 repro (3 balls, elastic):", vv, "duration", tt)
vv, tt = chain(3, 1.0, 0.95, 5e-9)
print("3 balls e=0.95 (LN damping):", vv, "duration", tt)
vv, tt = chain(2, 1.0, 0.95, 5e-9)
print("2 balls e=0.95 (LN damping):", vv, "duration", tt)
