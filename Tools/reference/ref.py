import numpy as np
from math import *

g = 9.80665
R = 0.028575
m = 0.170
I = 0.4 * m * R * R
mu_s = 0.2
mu_r = 0.010
alpha_sp = 10.0
e_slate = 0.6
Z = np.array([0.0, 0.0, 1.0])

def u_of(v, w):
    return v + np.cross(w, -R * Z)

def cue_frame(theta, phi):
    d = np.array([cos(theta) * cos(phi), cos(theta) * sin(phi), -sin(theta)])
    er = np.array([sin(phi), -cos(phi), 0.0])
    eu = np.cross(er, d)
    return d, er, eu

def squirt(a, mr):
    A = 1 - a * a
    return atan2(2.5 * a * sqrt(A), 1 + mr + 2.5 * A)

def strike(V, theta, phi, a, b, M, e_tip, lam=0.0, e_pinch=0.2, mr_end=None, mu=mu_s, es=e_slate, verbose=False):
    d, er, eu = cue_frame(theta, phi)
    rho2 = a * a + b * b
    c = sqrt(1 - rho2)
    Q = R * (a * er + b * eu - c * d)
    st = sin(theta)
    J = (1 + e_tip) * V / (1 / M + (1 / m) * (1 - lam * st * st + 2.5 * rho2))
    asq = squirt(a, mr_end) if mr_end else 0.0
    p = J * (cos(asq) * d - sin(asq) * er)
    w = (J / I) * np.cross(Q, d)
    # check w formula
    w2 = (J * R / I) * (a * eu - b * er)
    assert np.allclose(w, w2)
    v = p / m
    out = {"J": J, "v_after_tip": v.copy(), "w_after_tip": w.copy(), "squirt_deg": degrees(asq)}
    wn = -v[2]
    if wn > 0:
        e_eff = lam * e_pinch + (1 - lam) * es
        Pi = (1 + e_eff) * wn
        vh = np.array([v[0], v[1], 0.0])
        u = u_of(vh, w)
        un = np.linalg.norm(u)
        if un > 0:
            mag = min(mu * Pi, 2.0 / 7.0 * un)
            dvh = -mag * u / un
        else:
            dvh = np.zeros(3)
        vh = vh + dvh
        w = w - (5 / (2 * R)) * np.cross(Z, dvh)
        v = vh + e_eff * wn * Z
        out["e_eff"] = e_eff
        out["stick"] = (2.0 / 7.0 * un <= mu * Pi)
    out["v"] = v
    out["w"] = w
    return out

def slide(v0, w0, t, mu=mu_s):
    u0 = u_of(v0, w0)
    uh = u0 / np.linalg.norm(u0)
    r = v0 * t - 0.5 * mu * g * uh * t * t
    v = v0 - mu * g * uh * t
    w = w0 + (5 * mu * g / (2 * R)) * np.cross(Z, uh) * t
    return r, v, w

def tau_s(v0, w0, mu=mu_s):
    return 2 * np.linalg.norm(u_of(v0, w0)) / (7 * mu * g)

print("alpha_sp -> mu_sp:", 2 * R * alpha_sp / (5 * g))
print("pooltool u_sp numeric:", 10 * 2 / 5 / 9 * R, " alpha:", 5 * (10 * 2 / 5 / 9 * R) * g / (2 * R))
print("L&G mu_sp=0.044 -> alpha:", 5 * 0.044 * g / (2 * R))
print("contact radius uniform:", 3 * (2 * R * alpha_sp / (5 * g)) * R / (2 * mu_s))

# T: rolling
v0 = 1.0
print("\nROLL v0=1: t=", v0 / (mu_r * g), " d=", v0 * v0 / (2 * mu_r * g))
# T: stun
v0 = np.array([2.0, 0, 0]); w0 = np.zeros(3)
ts = tau_s(v0, w0); r, v, w = slide(v0, w0, ts)
print("\nSTUN: tau=", ts, " r=", r, " v=", v, " w=", w, " 12v^2/(49 mu g)=", 12 * 4 / (49 * mu_s * g))
# T: draw b=-0.5
w0 = np.array([0, -1.25 * 2.0 / R, 0])
ts = tau_s(v0, w0); r, v, w = slide(v0, w0, ts)
print("\nDRAW: w0=", w0, " u0=", u_of(v0, w0), " tau=", ts, " r=", r, " v=", v, " w=", w, " w*R=", w * R)
tstun = 1.25 * 2.0 / R / (5 * mu_s * g / (2 * R))
rs, vs, ws = slide(v0, w0, tstun)
print("   t_stun=", tstun, " r_stun=", rs, " v_stun=", vs)
# follow b=+0.5
w0 = np.array([0, 1.25 * 2.0 / R, 0])
ts = tau_s(v0, w0); r, v, w = slide(v0, w0, ts)
print("\nFOLLOW: u0=", u_of(v0, w0), " tau=", ts, " r=", r, " v=", v)

# massé-like sliding parabola
v0 = np.array([2.0, 0, 0]); w0 = np.array([30.0, 0, 0])
u0 = u_of(v0, w0); ts = tau_s(v0, w0); r, v, w = slide(v0, w0, ts)
print("\nSWERVE-SLIDE: u0=", u0, " tau=", ts, " r=", r, " v=", v, " w=", w, " invariant=", 5/7*v0 - 2/7*R*np.cross(Z, w0))
rh, vh_, wh = slide(v0, w0, ts / 2)
print("   half:", rh, vh_)

# cue strikes
oz = 0.028349523125
M19 = 19 * oz
print("\nM19=", M19, " m/M=", m / M19)
o = strike(2.0, 0, 0, 0, 0, M19, 1.0); print("CENTER elastic V=2:", o["v"], o["w"])
o = strike(2.0, 0, 0, 0, 0, M19, 0.75); print("CENTER e=.75 V=2:", o["v"], o["w"])
o = strike(2.0, 0, 0, 0, 0.4, M19, 0.75); print("NATROLL b=.4:", o["v"], o["w"], " u=", u_of(o["v"], o["w"]))
o = strike(2.0, 0, 0, 0, -0.5, M19, 0.75); print("DRAW b=-.5:", o["v"], o["w"], " SRF=", -o["w"][1] * R / o["v"][0])
o = strike(2.0, 0, 0, 0.3, 0, M19, 0.75); print("RIGHT a=.3:", o["v"], o["w"])
o = strike(2.0, 0, pi / 2, 0.3, 0.2, M19, 0.75); print("RIGHT a=.3,b=.2 phi=90:", o["v"], o["w"])
o = strike(2.0, 0, 0, 0.5, 0, M19, 0.75, mr_end=20); print("SQUIRT a=.5 mr=20:", o["v"], o["squirt_deg"], degrees(atan2(o["v"][1], o["v"][0])))
for mr in (15, 20, 30, 40):
    print("  squirt(0.5,", mr, ")=", degrees(squirt(0.5, mr)), " (0.25)=", degrees(squirt(0.25, mr)))
print("miscue rho_max mu=.6:", 0.6 / sqrt(1 + .36), " mu=.4:", .4 / sqrt(1.16))
print("separation rho (e=.75, 19oz):", sqrt(0.4 * 0.75 * (1 + m / M19)), " elastic 18oz:", sqrt(0.4 * (1 + (6/18))))

# break: V=8.5 m/s, phenolic e=0.85, 21 oz, theta=5deg, b=0.1
M21 = 21 * oz
o = strike(8.5, radians(5), 0, 0, 0.1, M21, 0.85, verbose=True)
print("\nBREAK:", o)
vz = o["v"][2]
print("  hop apex (m):", vz * vz / (2 * g), " flight t:", 2 * vz / g, " flight dist:", o["v"][0] * 2 * vz / g)

# jump: jump cue 9oz, phenolic e=.85, V=4, theta=50, center
M9 = 9 * oz
o = strike(4.0, radians(50), 0, 0, 0, M9, 0.85)
print("\nJUMP:", o)
vz = o["v"][2]
print("  apex:", vz * vz / (2 * g), " t_flight:", 2 * vz / g, " dist:", o["v"][0] * 2 * vz / g)

# swerve: theta=20, a=0.4 right, V=3, 19oz leather e=.73, mr_end=20, lam=0
o = strike(3.0, radians(20), 0, 0.4, 0, M19, 0.73, mr_end=20)
print("\nSWERVE:", o)
v0 = o["v"].copy(); w0 = o["w"].copy()
if v0[2] > 0:
    print("  airborne first")

# masse: theta=75, a=0.4, b=-0.3 (right, below), V=2.5, lam=1 vs 0
for lam in (0.0, 1.0):
    o = strike(2.5, radians(75), 0, 0.4, -0.3, M19, 0.73, lam=lam, mr_end=20)
    print("\nMASSE lam=", lam, o)
    vf = 5 / 7 * np.array([o["v"][0], o["v"][1], 0]) - 2 / 7 * R * np.cross(Z, o["w"])
    print("  final roll vel:", vf, " dir deg:", degrees(atan2(vf[1], vf[0])))

# TP B.10 bounce reproduction
mph = 0.44704
v0mag = 11.194 * mph; th = radians(5)
v = np.array([v0mag * cos(th), 0, -v0mag * sin(th)])
w = np.array([0, -34.841 * 2 * pi, 0])  # backspin for +x motion is negative w_y
vh = np.array([v[0], 0, 0]); wn = -v[2]
u = u_of(vh, w); un = np.linalg.norm(u)
Pi = (1 + 0.6) * wn
mag = min(0.2 * Pi, 2 / 7 * un)
dvh = -mag * u / un
vh2 = vh + dvh; w2 = w - (5 / (2 * R)) * np.cross(Z, dvh)
v2 = vh2 + 0.6 * wn * Z
print("\nB10 bounce: speed mph=", np.linalg.norm(v2) / mph, " spin rps=", -w2[1] / (2 * pi), " angle deg=", degrees(atan2(v2[2], v2[0])), " dist ft=", np.linalg.norm(v2)**2 / g * sin(2 * atan2(v2[2], v2[0])) / 0.3048)

# airborne
vz = 1.0
print("\nAIR vz=1: apex", vz * vz / (2 * g), " t", 2 * vz / g)
# v_z threshold for h_min
for h in (0.001, 0.002, 0.005):
    print("h_min", h, " vz_min", sqrt(2 * g * h))

# Spinning
print("\nSPIN w=20: t=", 20 / alpha_sp)
# unit conversions
print("mph->m/s", [x * mph for x in (1, 2, 4, 7, 10, 25, 30, 35)])
print("oz->kg", [x * oz for x in (8, 9, 10, 18, 19, 20, 21, 25)])
