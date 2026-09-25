import math, numpy as np
from tilt import Pursuit, g0, roll_case
C_INT = 2.0 / 81.0

def root(cs, k, G, sig, eps, x):
    a3 = C_INT * cs * k * G * sig; a1 = eps * (k + G); a0 = -eps * x
    D = min(x / (k + G), (eps * x / a3) ** (1 / 3))
    for _ in range(100):
        f = a3 * D ** 3 + a1 * D + a0; fp = 3 * a3 * D * D + a1
        nD = D - f / fp
        if abs(nD - D) < 1e-16 * max(1, D): D = nD; break
        D = nD
    return D

def chain_roll(v0, s, mur=0.01, eps=5e-5, Tmax=2.0, cs=1.0, verbose=False):
    P, gt = roll_case(v0, s, mur)
    k, G = P.k, P.g
    Ghat = P.Gh
    t = 0.0
    nodes = []
    maxdev = 0.0; maxratio = 0.0; vjump = 0.0
    xtail = math.sqrt(eps * (k - G) / (4 * cs))
    Ttot = P.Tstop()
    while True:
        x_i, X_i = P.state(t) if t > 0 else (P.x0, np.zeros(2))
        nx = np.linalg.norm(x_i)
        Trem = Ttot - t
        xh = x_i / nx
        ci = float(np.dot(xh, Ghat)); si = abs(xh[0] * Ghat[1] - xh[1] * Ghat[0])
        sig = si if ci >= 0 else 1.0
        if sig <= 1e-12 or nx <= xtail:
            D = Trem
        else:
            D = min(Tmax, Trem, root(cs, k, G, sig, eps, nx))
        tn = t + D
        if tn >= Ttot * (1 - 1e-15) or D == Trem:
            xn, Xn = np.zeros(2), P.X_inf(); tn = Ttot
        else:
            xn, Xn = P.state(tn)
        A2 = (Xn - X_i - x_i * D) / D ** 2
        # deviation
        dev = 0
        for j in range(1, 400):
            tau = D * j / 400
            xe, Xe = P.state(t + tau)
            q = X_i + x_i * tau + A2 * tau * tau
            dev = max(dev, np.linalg.norm(q - Xe))
        maxdev = max(maxdev, dev)
        vq = x_i + 2 * A2 * D
        vjump = max(vjump, np.linalg.norm(vq - xn))
        nodes.append(tn)
        t = tn
        if t >= Ttot:
            break
    return nodes, maxdev, vjump

if __name__ == "__main__":
    n, dev, vj = chain_roll((0.5, 0), (0, 1e-3))
    print("T16 (a)", len(n), [round(x, 6) for x in n], "maxdev", dev, "vjump", vj)
    for v in [(1.0, 0), (0.3, 0)]:
        n, dev, vj = chain_roll(v, (0, 1e-3)); print(v, len(n), dev, dev / 5e-5, vj)
    n, dev, vj = chain_roll((0.5, 0), (0, 1e-3), eps=5e-4); print("eps 5e-4", len(n), dev / 5e-4, vj)
    n, dev, vj = chain_roll((1.0, 0.3), (2e-3, -1e-3)); print("T15b chain", len(n), dev / 5e-5, vj)
    n, dev, vj = chain_roll((0.5, 0), (0, 3e-3)); print("3mm/m 0.5", len(n), dev / 5e-5, vj)
    n, dev, vj = chain_roll((0.5, 0), (0, 0.5e-3)); print("0.5mm/m 0.5", len(n), dev / 5e-5, vj)
    n, dev, vj = chain_roll((2.0, 0), (0, 2.5e-3)); print("2.5mm/m 2 m/s", len(n), dev / 5e-5, vj)
