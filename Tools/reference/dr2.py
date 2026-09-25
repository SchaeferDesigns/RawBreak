import math
import numpy as np

R = 0.028575
g = 9.80665
MU_R, MU_S = 0.01, 0.2


def polyval(c, t):  # c ascending: c0 + c1 t + ...
    r = 0.0
    for a in reversed(c):
        r = r * t + a
    return r


def deriv(c):
    return [i * c[i] for i in range(1, len(c))]


def trim(c, tol=0.0):
    c = list(c)
    while len(c) > 1 and abs(c[-1]) <= tol:
        c.pop()
    return c


def refine(c, a, b, fa, fb):
    """safeguarded Newton (rtsafe-like) on [a,b] with sign change fa*fb<0."""
    dc = deriv(c)
    if fa > 0:  # orient so f(a)<0
        pass
    x = 0.5 * (a + b)
    lo, hi = (a, b) if fa < 0 else (b, a)
    for _ in range(100):
        f = polyval(c, x)
        df = polyval(dc, x)
        if f < 0:
            lo = x
        else:
            hi = x
        xn = x - f / df if df != 0 else 0.5 * (lo + hi)
        if not (min(lo, hi) < xn < max(lo, hi)):
            xn = 0.5 * (lo + hi)
        if abs(xn - x) < 1e-15 * max(1.0, abs(x)):
            return xn
        x = xn
    return x


def real_roots_in(c, lo, hi):
    """All real roots of ascending-coeff polynomial c in [lo,hi], via recursive derivative isolation."""
    c = trim(c)
    n = len(c) - 1
    if n <= 0:
        return []
    if n == 1:
        r = -c[0] / c[1]
        return [r] if lo <= r <= hi else []
    if n == 2:
        a, b, cc = c[2], c[1], c[0]
        disc = b * b - 4 * a * cc
        if disc < 0:
            return []
        q = -0.5 * (b + math.copysign(math.sqrt(disc), b))
        rs = []
        if q != 0:
            rs.append(cc / q)
        if a != 0:
            rs.append(q / a)
        return sorted(r for r in rs if lo <= r <= hi)
    crit = real_roots_in(deriv(c), lo, hi)
    pts = [lo] + crit + [hi]
    out = []
    for a, b in zip(pts[:-1], pts[1:]):
        fa, fb = polyval(c, a), polyval(c, b)
        if fa == 0:
            if not out or abs(out[-1] - a) > 1e-15:
                out.append(a)
            continue
        if fa * fb < 0:
            out.append(refine(c, a, b, fa, fb))
    if polyval(c, hi) == 0:
        out.append(hi)
    return out


def traj(r0, v0, acc):
    """ascending coeff per axis: r0 + v0 t + 0.5 acc t^2 -> return (C,B,A) arrays"""
    return np.array(r0, float), np.array(v0, float), 0.5 * np.array(acc, float)


def ball_ball_quartic(t1, t2, dist):
    C = t2[0] - t1[0]; B = t2[1] - t1[1]; A = t2[2] - t1[2]
    return [C @ C - dist * dist, 2 * B @ C, B @ B + 2 * A @ C, 2 * A @ B, A @ A]


def first_contact(c, T):
    # approach condition: f decreasing (f' < 0) at root
    dc = deriv(c)
    for r in real_roots_in(c, 0.0, T):
        if polyval(dc, r) < 0:
            return r
    return math.inf


def roll_acc(v):
    v = np.array(v, float)
    return -MU_R * g * v / np.linalg.norm(v)


def show(name, c, T):
    t = first_contact(c, T)
    ref = sorted(r.real for r in np.roots(list(reversed(trim(c)))) if abs(r.imag) < 1e-9 and r.real >= 0)
    print(f"{name}: coeffs(asc)={['%.6e'%x for x in c]}\n   first approach root={t!r}   numpy real roots>=0={ref}")
    return t


# D1: rolling A into stationary B, head-on
A = traj([0, 0, R], [1, 0, 0], roll_acc([1, 0, 0]))
B = traj([0.5, 0, R], [0, 0, 0], [0, 0, 0])
t = show("D1 head-on rolling v=1 into B at 0.5", ball_ball_quartic(A, B, 2 * R), 10.0)
print("   x_A at contact:", t - 0.5 * MU_R * g * t * t, "expected", 0.5 - 2 * R)
# D2: offset 0.05
B = traj([0.5, 0.05, R], [0, 0, 0], [0, 0, 0])
show("D2 offset 0.05", ball_ball_quartic(A, B, 2 * R), 10.0)
# D3: offset 0.06 -> miss
B = traj([0.5, 0.06, R], [0, 0, 0], [0, 0, 0])
show("D3 offset 0.06 (miss)", ball_ball_quartic(A, B, 2 * R), 10.0)
# D4: A sliding stun v=2 (acc = -mu_s g x), B rolling towards A at 1 m/s from (1.0,0.02)
A = traj([0, 0, R], [2, 0, 0], [-MU_S * g, 0, 0])
B = traj([1.0, 0.02, R], [-1, 0, 0], roll_acc([-1, 0, 0]))
T = min(2 * 2 / (7 * MU_S * g), 1 / (MU_R * g))
show(f"D4 sliding A vs rolling B (horizon {T:.5f})", ball_ball_quartic(A, B, 2 * R), T)
# D5: airborne A (partial jump) vs stationary B at 0.1
A = traj([0, 0, R], [2, 0, 1.5], [0, 0, -g])
B = traj([0.1, 0, R], [0, 0, 0], [0, 0, 0])
t = show("D5 airborne A vs B at x=0.1", ball_ball_quartic(A, B, 2 * R), 2 * 1.5 / g)
pA = A[0] + A[1] * t + A[2] * t * t
n = (B[0] - pA) / np.linalg.norm(B[0] - pA)
print("   contact normal:", n, "A pos", pA)
# D5b: B at 0.4 -> A jumps over
B = traj([0.4, 0, R], [0, 0, 0], [0, 0, 0])
show("D5b airborne A over B at x=0.4 (miss)", ball_ball_quartic(A, B, 2 * R), 2 * 1.5 / g)
# D6: post-collision degenerate: touching, v1=.025, v2=.975 both sliding same decel
A = traj([0, 0, R], [0.025, 0, 0], [-MU_S * g, 0, 0])
B = traj([2 * R, 0, R], [0.975, 0, 0], [-MU_S * g, 0, 0])
c = ball_ball_quartic(A, B, 2 * R)
show("D6 post-collision separating (degenerate a4=a3=0)", c, 2 * 0.025 / (7 * MU_S * g))
# D7: touching and approaching at t=0 (frozen rack): A moving into B that touches it
A = traj([0, 0, R], [1, 0, 0], [-MU_S * g, 0, 0])
B = traj([2 * R, 0, R], [0, 0, 0], [0, 0, 0])
c = ball_ball_quartic(A, B, 2 * R)
print("D7 frozen touching & approaching: f(0)=", polyval(c, 0.0), " f'(0)=", polyval(deriv(c), 0.0), "-> immediate collision at t=0")

# Cushion D8: on-table ball rolling +y at 1 m/s toward nose line y=0.635 (9ft), h=0.635D
h = 0.635 * 2 * R
dh = math.sqrt(R * R - (h - R) ** 2)
yc = 0.635 - dh
a = -0.5 * MU_R * g
# y(t) = t + a t^2 = yc
cq = [-yc, 1.0, a]
print("D8 cushion: dh=", dh, " contact y=", yc, " t=", real_roots_in(cq, 0, 1 / (MU_R * g)))
# D9: jaw point (vertical cylinder radius 0.004 at (0.3, 0.1)), ball rolling +x from origin at 1 m/s; contact horizontal distance = r_pt + dh
rp = 0.004
cx, cy = 0.3, 0.02
# horizontal distance^2 between (t + a t^2, 0) and (cx, cy) == (rp + dh)^2
# (x - cx)^2 + cy^2 - D^2 = 0, x = t + a t^2
D = rp + dh
cc = [cx * cx + cy * cy - D * D, -2 * cx, 1 - 2 * cx * a, 2 * a, a * a]
cc = [cx * cx + cy * cy - D * D, -2 * cx * 1.0, 1.0 - 2 * cx * a, 2 * a * 1.0, a * a]
print("D9 jaw point: roots", real_roots_in(cc, 0, 1 / (MU_R * g)), " first approach", first_contact(cc, 1 / (MU_R * g)))
