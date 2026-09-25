import numpy as np
from math import *

g = 9.80665
R = 0.028575
m = 0.170
mu_s = 0.2
mu_r = 0.010
alpha_sp = 10.0
e_slate = 0.6
h_min = 0.002
vzmin = sqrt(2 * g * h_min)
OZ = 0.028349523125
M19 = 19 * OZ
M21 = 21 * OZ
M9 = 9 * OZ
I = 0.4 * m * R * R
z = np.array([0.0, 0.0, 1.0])
deg = pi / 180


def slip(v, w):
    return v + R * np.cross(z, w)


def slide_end(v0, w0):
    u0 = slip(v0, w0); u0[2] = 0
    tau = 2 * np.linalg.norm(u0) / (7 * mu_s * g)
    return tau, u0


def slide_state(r0, v0, w0, tau):
    u0 = slip(v0, w0); u0[2] = 0
    uh = u0 / np.linalg.norm(u0)
    r = r0 + v0 * tau - 0.5 * mu_s * g * uh * tau ** 2
    v = v0 - mu_s * g * uh * tau
    w = w0 + (5 * mu_s * g / (2 * R)) * np.cross(z, uh) * tau
    wz0 = w0[2]
    w[2] = wz0 - np.sign(wz0) * alpha_sp * min(tau, abs(wz0) / alpha_sp)
    return r, v, w


def cue_frame(theta, phi):
    d = np.array([cos(theta) * cos(phi), cos(theta) * sin(phi), -sin(theta)])
    er = np.array([sin(phi), -cos(phi), 0.0])
    eu = np.cross(er, d)
    return d, er, eu


def squirt(a, mr):
    return atan2(2.5 * a * sqrt(1 - a * a), 1 + mr + 2.5 * (1 - a * a))


def strike(V, theta, phi, a, b, M, e_tip, mu_tip=0.6, mu_k=None, mr=None, lam=0.0, e_pinch=0.2,
           es=e_slate, do_slate=True, verbose=False):
    if mu_k is None:
        mu_k = mu_tip
    d, er, eu = cue_frame(theta, phi)
    rho = hypot(a, b)
    c = sqrt(1 - rho * rho)
    Q = R * (a * er + b * eu - c * d)
    n = -Q / R
    rho_max = mu_tip / sqrt(1 + mu_tip ** 2)
    if rho <= rho_max:
        J = (1 + e_tip) * V / (1 / M + (1 / m) * (1 - lam * sin(theta) ** 2 + 2.5 * rho ** 2))
        w = (5 * J / (2 * m * R)) * (a * eu - b * er)
        al = squirt(a, mr) if mr else 0.0
        p = cos(al) * d - sin(al) * er
        miscue = False
    else:
        t = d - np.dot(d, n) * n
        t /= np.linalg.norm(t)
        p = (n + mu_k * t) / sqrt(1 + mu_k ** 2)
        k = np.linalg.norm(np.cross(Q / R, p))
        dp = np.dot(d, p)
        J = (1 + e_tip) * V * dp / (dp ** 2 / M + (1 / m) * (1 + 2.5 * k * k))
        w = (J / I) * np.cross(Q, p)
        miscue = True
    v = (J / m) * p
    Vafter = V - J * np.dot(d, p) / M
    v_tip, w_tip = v.copy(), w.copy()
    if do_slate:
        v, w = slate_reaction_at_zero(v, w, lam, e_pinch, es)
    return dict(J=J, v_tip=v_tip, w_tip=w_tip, v=v, w=w, miscue=miscue, Vafter=Vafter, p=p)


def slate_reaction_at_zero(v, w, lam, e_pinch, es):
    vh = v.copy(); vh[2] = 0
    wn = -v[2]
    if wn > 0:
        e_eff = lam * e_pinch + (1 - lam) * es
        Pi = (1 + e_eff) * wn
        u = vh + R * np.cross(z, w); u[2] = 0
        un = np.linalg.norm(u)
        if un > 1e-12:
            dvh = -min(mu_s * Pi, 2 / 7 * un) * u / un
        else:
            dvh = np.zeros(3)
        vh = vh + dvh
        w = w - (5 / (2 * R)) * np.cross(z, dvh)
        vz = e_eff * wn
        if vz < vzmin:
            vz = 0
        v = vh + vz * z
    return v, w


def slate_impact(v, w, es=e_slate, mus=mu_s):
    wn = -v[2]
    vh = v.copy(); vh[2] = 0
    Pi = (1 + es) * wn
    u = vh + R * np.cross(z, w); u[2] = 0
    un = np.linalg.norm(u)
    if un > 1e-12:
        dvh = -min(mus * Pi, 2 / 7 * un) * u / un
        branch = 'stick' if 2 / 7 * un <= mus * Pi else 'slip'
    else:
        dvh = np.zeros(3); branch = 'none'
    vh2 = vh + dvh
    w2 = w - (5 / (2 * R)) * np.cross(z, dvh)
    vz = es * wn
    return vh2 + vz * z, w2, branch


def Lc(v, w):
    vh = v.copy(); vh[2] = 0
    wh = w.copy(); wh[2] = 0
    return (5 / 7) * vh - (2 / 7) * R * np.cross(z, wh)


def run_to_rolling(r, v, w, t=0.0, nmax=10, log=False):
    """airborne+bounce+slide until rolling; returns r,v,w,t, list of impacts"""
    impacts = []
    n = 0
    while True:
        if v[2] > 0 or r[2] > R + 1e-12:
            tl = (v[2] + sqrt(v[2] ** 2 + 2 * g * (r[2] - R))) / g
            r = r + v * tl - 0.5 * g * z * tl ** 2
            r[2] = R
            v = v - g * z * tl
            t += tl
            n += 1
            v, w, br = slate_impact(v, w)
            impacts.append((t, r.copy(), v.copy(), w.copy(), br))
            if v[2] < vzmin or n >= nmax:
                v[2] = 0
            continue
        u = slip(v, w); u[2] = 0
        if np.linalg.norm(u) > 1e-12:
            tau, _ = slide_end(v, w)
            r, v, w = slide_state(r, v, w, tau)
            t += tau
        return r, v, w, t, impacts


np.set_printoptions(precision=7, suppress=True)
print('v_zmin', vzmin, 'M19', M19, 'M21', M21, 'M9', M9, 'I', I)
print('--- A tests')
# A1
v0 = 1.0
print('A1 tau_roll', v0 / (mu_r * g), 'x', v0 ** 2 / (2 * mu_r * g))
# A2
r0 = np.array([0, 0, R]); v0 = np.array([2.0, 0, 0]); w0 = np.zeros(3)
tau, u0 = slide_end(v0, w0); r, v, w = slide_state(r0, v0, w0, tau)
print('A2', tau, r, v, w, 12 * 4 / (49 * mu_s * g), v[0] / R)
# A3
for mph in (3, 7, 12):
    vv = mph * 0.44704
    tau, _ = slide_end(np.array([vv, 0, 0]), np.zeros(3))
    rr, _, _ = slide_state(r0, np.array([vv, 0, 0]), np.zeros(3), tau)
    print('A3', mph, vv, rr[0], rr[0] / 0.3048)
# A4
wy = 2.5 * 0.5 * 2 / R
print('wy', wy)
v0 = np.array([2.0, 0, 0]); w0 = np.array([0, -wy, 0])
tau, u0 = slide_end(v0, w0); r, v, w = slide_state(r0, v0, w0, tau)
ts = wy / (5 * mu_s * g / (2 * R))
rs, vs, ws = slide_state(r0, v0, w0, ts)
print('A4', u0, tau, r, v, 'stun', ts, rs, vs, ws)
# A5
w0 = np.array([0, wy, 0])
tau, u0 = slide_end(v0, w0); r, v, w = slide_state(r0, v0, w0, tau)
print('A5', u0, tau, r, v)
# A6
w0 = np.array([30.0, 0, 0])
tau, u0 = slide_end(v0, w0)
rh, vh, wh = slide_state(r0, v0, w0, tau / 2)
r, v, w = slide_state(r0, v0, w0, tau)
print('A6', u0, tau, rh, r, v, w, slip(v, w))
# A7 property
rng = np.random.default_rng(1)
maxerr = 0
for i in range(10000):
    v0 = rng.uniform(-1, 1, 3); v0[2] = 0; v0 *= 12 * rng.random() / max(np.linalg.norm(v0), 1e-9)
    w0 = rng.uniform(-1, 1, 3); w0 *= 500 * rng.random() / np.linalg.norm(w0)
    tau, u0 = slide_end(v0, w0)
    r, v, w = slide_state(r0, v0, w0, tau)
    err = np.linalg.norm(v - Lc(v0, w0))
    maxerr = max(maxerr, err, np.linalg.norm(slip(v, w)[:2]))
print('A7 maxerr', maxerr)
# A10
print('A10', 0.1 / (mu_r * g), 20 - 10 * 0.1 / (mu_r * g))

print('--- B tests')
res = strike(2, 0, 0, 0, 0, M19, 1.0)
print('B1', res['v'], res['w'])
print('B2', 2 / (1 + 1 / 3))
res = strike(2, 0, 0, 0, 0, M19, 0.75); print('B3', res['v'])
res = strike(2, 0, 0, 0, 0.4, M19, 0.75); print('B4', res['v'], res['w'], slip(res['v'], res['w']))
res = strike(2, 0, 0, 0, -0.5, M19, 0.75); print('B5', res['v'], res['w'], np.linalg.norm(res['w']) * R / res['v'][0])
res = strike(2, 0, 0, 0.3, 0, M19, 0.75); print('B6', res['v'], res['w'])
res = strike(2, 0, pi / 2, 0.3, 0.2, M19, 0.75); print('B7', res['v'], res['w'])
for a, mr in ((0.5, 15), (0.5, 40), (0.5, 20), (0.25, 20), (0.25, 15), (0.5, 30), (0.25, 30), (0.25, 40)):
    print('B8', a, mr, squirt(a, mr) / deg)
res = strike(2, 0, 0, 0.5, 0, M19, 0.75, mr=20); print('B9', res['v'], atan2(res['v'][1], res['v'][0]) / deg, np.linalg.norm(res['v']))
print('B10 rho_max', 0.6 / sqrt(1.36))
for a in (0.514, 0.5144958, 0.515, 0.6):
    res = strike(2, 0, 0, a, 0, M19, 0.75, mr=None)
    print('B10', a, res['miscue'], res['v'], np.linalg.norm(res['v']), atan2(res['v'][1], res['v'][0]) / deg, res['w'], res['Vafter'])
print('B11', (2 / 3) * R / (R + R / 3))
print('B12', sqrt(0.4 * 0.73 * (1 + m / M19)), sqrt(0.4 * 1 * (1 + 1 / 3)))
res = strike(8.5, 5 * deg, 0, 0, 0.1, M21, 0.85)
print('B13 tip', res['v_tip'], res['w_tip'], 'slate', res['v'], res['w'])
vz = res['v'][2]
print('B13 land', 2 * vz / g, res['v'][0] * 2 * vz / g, vz ** 2 / (2 * g))
res = strike(4, 50 * deg, 0, 0, 0, M9, 0.85)
print('B14 tip', res['v_tip'], res['w_tip'], 'slate', res['v'], res['w'], slip(res['v'], res['w']))
vz = res['v'][2]
print('B14 land', 2 * vz / g, res['v'][0] * 2 * vz / g, vz ** 2 / (2 * g))
for lam in (0.0, 1.0):
    res = strike(2.5, 75 * deg, 0, 0.4, -0.3, M19, 0.73, lam=lam)
    L = Lc(res['v'], res['w'])
    r, v, w, t, imp = run_to_rolling(np.array([0, 0, R]), res['v'], res['w'])
    print('B15 lam', lam, 'J', res['J'], 'Lc', L, 'v_roll', v, 'dir', atan2(L[1], L[0]) / deg,
          'formula', atan2(-0.4 * sin(75 * deg), cos(75 * deg) - 0.3) / deg, 'nimp', len(imp))
res = strike(3, 0, 0, 0.4, 0, M19, 0.73, mr=20)
print('B16', res['v'], res['w'], atan2(res['v'][1], res['v'][0]) / deg, np.cross(slip(res['v'], res['w']), res['v']))
res = strike(2, 10 * deg, 0, 0.4, 0, M19, 0.73, mr=20)
print('B17 tip', res['v_tip'], res['w_tip'], 'after slate', res['v'], res['w'])
L = Lc(res['v'], res['w'])
r, v, w, t, imp = run_to_rolling(np.array([0, 0, R]), res['v'], res['w'])
print('B17 Lc', L, 'v_roll', v, atan2(v[1], v[0]) / deg, 'nimp', len(imp), 'squirt', squirt(0.4, 20) / deg)

print('--- C tests')
print('C1', 1 / (2 * g), 2 / g)
r, v, w, t, imp = run_to_rolling(np.array([0, 0, R]), np.array([1.0, 0, 1.0]), np.zeros(3))
for it in imp:
    print('C2 impact', it[0], it[1][0], it[2], it[3], it[4])
print('C2 final', t, r, v, w)
# C3
vmag = 11.194 * 0.44704
v = np.array([vmag * cos(5 * deg), 0, -vmag * sin(5 * deg)])
w = np.array([0, -34.841 * 2 * pi, 0])
print('C3 pre', v, w)
v2, w2, br = slate_impact(v, w)
sp = np.linalg.norm(v2)
print('C3', br, v2, w2, sp / 0.44704, atan2(v2[2], v2[0]) / deg, w2[1] / (2 * pi), 'dist', v2[0] * 2 * v2[2] / g, v2[0] * 2 * v2[2] / g / 0.3048)
# continue TP B.10 sequence 5 deg
vv, ww = v2, w2
for k in range(2, 6):
    tl = 2 * vv[2] / g
    vv = vv - g * z * tl
    vv, ww, br = slate_impact(vv, ww)
    print('C3 bounce', k, br, np.linalg.norm(vv) / 0.44704, -ww[1] / (2 * pi), atan2(vv[2], vv[0]) / deg, vv[0] * 2 * vv[2] / g / 0.3048)
# TP B.10 12 deg case
v = np.array([vmag * cos(12 * deg), 0, -vmag * sin(12 * deg)])
w = np.array([0, -34.841 * 2 * pi, 0])
for k in range(1, 3):
    v, w, br = slate_impact(v, w)
    print('B10-12deg bounce', k, br, np.linalg.norm(v) / 0.44704, -w[1] / (2 * pi), atan2(v[2], v[0]) / deg, v[0] * 2 * v[2] / g / 0.3048)
    tl = 2 * v[2] / g
    v = v - g * z * tl
# C4 continuity
wn = 0.5
un = 3.5 * mu_s * (1 + e_slate) * wn
v = np.array([un, 0, -wn]); w = np.zeros(3)
v2, w2, br = slate_impact(v, w)
print('C4', br, v2, w2)
# TP A.19 checks
a = 0.5 * cos(45 * deg)
print('A19 draw', atan2(a * sin(4 * deg), cos(4 * deg) - a) / deg, 'follow', atan2(a * sin(3 * deg), cos(3 * deg) + a) / deg)
# TP A.30 efficiency comparison
mr = 6 / 19
eta = 0.87
x = 0.5
vb_eff = (1 + sqrt(eta - (1 - eta) / mr * (1 + 2.5 * x * x))) / (1 + mr + 2.5 * x * x)
# equivalent e
ee = sqrt((eta * (1 + mr) ** 2 - (1 + mr)) / (mr * mr + mr))
vb_rest = (1 + ee) / (1 + mr + 2.5 * x * x)
print('A30', vb_eff, ee, vb_rest, vb_eff / vb_rest)
for e in (0.73,):
    eta2 = ((1 - mr * e) ** 2 + mr * (1 + e) ** 2) / (1 + mr) ** 2
    vb_eff = (1 + sqrt(eta2 - (1 - eta2) / mr * (1 + 2.5 * x * x))) / (1 + mr + 2.5 * x * x)
    print('A30 e=.73', eta2, vb_eff, (1 + e) / (1 + mr + 2.5 * x * x), vb_eff / ((1 + e) / (1 + mr + 2.5 * x * x)))
