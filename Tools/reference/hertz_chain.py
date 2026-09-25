import math
import numpy as np

R = 0.028575
m = 0.170097
K = 2270.0 / (2 * 1.125 * 0.0254 * 3.49e-3) ** 1.5


def run_chain(nb, v0, alpha, dt=1e-7, gaps=None):
    """1D chain of nb balls along x, ball 0 moving at v0. Tsuji damping:
    F = K d^1.5 + alpha*sqrt(meff*K)*d^0.25*ddot  (clamped >= 0). Semi-implicit Euler."""
    meff = m / 2
    eta = alpha * math.sqrt(meff * K)
    x = np.zeros(nb)
    for i in range(1, nb):
        x[i] = x[i - 1] + 2 * R + (gaps[i - 1] if gaps is not None else 0.0)
    v = np.zeros(nb); v[0] = v0
    t = 0.0
    started = False
    while t < 0.02:
        d = 2 * R - np.diff(x)             # compression of pair i,i+1
        dd = v[:-1] - v[1:]
        F = np.where(d > 0, K * np.clip(d, 0, None) ** 1.5 + eta * np.clip(d, 0, None) ** 0.25 * dd, 0.0)
        F = np.clip(F, 0.0, None)
        a = np.zeros(nb)
        a[:-1] -= F / m
        a[1:] += F / m
        v = v + a * dt
        x = x + v * dt
        t += dt
        if np.any(F > 0):
            started = True
        if started and np.all(F == 0) and np.all(np.diff(v) >= -1e-12):
            break
    return v, t


def e_of_alpha(alpha, v0=1.0):
    v, _ = run_chain(2, v0, alpha)
    return (v[1] - v[0]) / v0


for target in [0.93, 0.95]:
    lo, hi = 0.0, 2.0
    for _ in range(30):
        mid = 0.5 * (lo + hi)
        if e_of_alpha(mid) > target:
            lo = mid
        else:
            hi = mid
    print(f"target e={target}: alpha={mid:.5f}; check e at v=0.3: {e_of_alpha(mid,0.3):.4f}, v=1: {e_of_alpha(mid,1.0):.4f}, v=5: {e_of_alpha(mid,5.0):.4f}")
    a95 = mid

v, t = run_chain(3, 1.0, 0.0)
print("3 balls elastic:", v, t)
v, t = run_chain(3, 1.0, a95)
print("3 balls e=.95 Tsuji:", v, t)
v, t = run_chain(2, 1.0, a95)
print("2 balls e=.95 Tsuji:", v, t)
v, t = run_chain(5, 1.0, a95)
print("5 balls e=.95 Tsuji:", v, t, "sum", v.sum(), "KE ratio", (v ** 2).sum())
v, t = run_chain(3, 1.0, a95, gaps=[0.0, 10e-6])
print("3 balls e=.95, 10um gap between OBs:", v, t)
v, t = run_chain(3, 1.0, a95, gaps=[0.0, 100e-6])
print("3 balls e=.95, 100um gap between OBs:", v, t)
