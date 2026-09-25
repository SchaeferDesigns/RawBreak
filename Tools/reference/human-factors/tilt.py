# Independent check of human-factors.md 4.5 (tilted table) - verifier
import math
import numpy as np
g0 = 9.80665
R = 0.028575

def em1(x):  # E_n = -expm1(-n lam) -> helper takes (n*lam)
    return -math.expm1(-x)

class Pursuit:
    """dx/dt = G - k xhat, constant G, k > |G|"""
    def __init__(self, x0, G, k):
        self.x0 = np.array(x0, float); self.G = np.array(G, float); self.k = k
        self.X0 = np.linalg.norm(self.x0)
        self.g = np.linalg.norm(self.G)
        self.Gh = self.G / self.g
        xh = self.x0 / self.X0
        self.c0 = float(np.dot(xh, self.Gh))
        perp = xh - self.c0 * self.Gh
        self.s0 = float(np.linalg.norm(perp))
        self.ep = perp / self.s0 if self.s0 > 0 else np.zeros(2)
        self.p = k / self.g
        self.A = (1 + self.c0) / 2; self.B = (1 - self.c0) / 2
    def t_of(self, lam):
        k, g, p, A, B = self.k, self.g, self.p, self.A, self.B
        return self.X0 * (A * em1((p - 1) * lam) / (k - g) + B * em1((p + 1) * lam) / (k + g))
    def Tstop(self):
        return self.X0 * (self.A / (self.k - self.g) + self.B / (self.k + self.g))
    def x_of(self, lam):
        p, A, B = self.p, self.A, self.B
        return self.X0 * ((A * math.exp(-(p - 1) * lam) - B * math.exp(-(p + 1) * lam)) * self.Gh + self.s0 * math.exp(-p * lam) * self.ep)
    def X_of(self, lam):
        k, g, p, A, B = self.k, self.g, self.p, self.A, self.B
        a = A * A * em1((2 * p - 2) * lam) / (2 * (k - g)) - B * B * em1((2 * p + 2) * lam) / (2 * (k + g))
        b = self.s0 * (A * em1((2 * p - 1) * lam) / (2 * k - g) + B * em1((2 * p + 1) * lam) / (2 * k + g))
        return self.X0 ** 2 * (a * self.Gh + b * self.ep)
    def lam_of(self, t):
        # safeguarded Newton / bisection on t(lam) = t
        if t >= self.Tstop():
            return math.inf
        lo, hi = 0.0, 1.0
        while self.t_of(hi) < t:
            hi *= 2
        lam = 0.5 * (lo + hi)
        for _ in range(200):
            f = self.t_of(lam) - t
            if f > 0: hi = lam
            else: lo = lam
            dt = (self.X0 / self.g) * (self.A * math.exp(-(self.p - 1) * lam) + self.B * math.exp(-(self.p + 1) * lam))
            nl = lam - f / dt
            if not (lo < nl < hi):
                nl = 0.5 * (lo + hi)
            if abs(nl - lam) <= 1e-15 * max(1, lam):
                lam = nl; break
            lam = nl
        return lam
    def state(self, t):
        lam = self.lam_of(t)
        if lam == math.inf:
            return np.zeros(2), self.X_inf()
        return self.x_of(lam), self.X_of(lam)
    def X_inf(self):
        k, g, p, A, B = self.k, self.g, self.p, self.A, self.B
        a = A * A / (2 * (k - g)) - B * B / (2 * (k + g))
        b = self.s0 * (A / (2 * k - g) + B / (2 * k + g))
        return self.X0 ** 2 * (a * self.Gh + b * self.ep)

def rk4_roll(v0, gt, mur, T, n):
    """independent ODE: dv/dt = 5/7 gt - mu_r g vhat, dr/dt = v"""
    G = 5.0 / 7.0 * np.array(gt); k = mur * g0
    def f(y):
        v = y[2:]; sp = np.linalg.norm(v)
        a = G - k * v / sp
        return np.concatenate([v, a])
    y = np.array([0, 0, v0[0], v0[1]], float); h = T / n
    for _ in range(n):
        k1 = f(y); k2 = f(y + h / 2 * k1); k3 = f(y + h / 2 * k2); k4 = f(y + h * k3)
        y = y + h / 6 * (k1 + 2 * k2 + 2 * k3 + k4)
    return y

def rk4_slide(v0, w0, gt, mus, T, n):
    """independent full sliding ODE with 3D spin (horizontal part), MOT conventions"""
    gt = np.array([gt[0], gt[1], 0.0])
    z = np.array([0, 0, 1.0])
    def f(y):
        v = y[3:6]; w = y[6:9]
        u = v + R * np.cross(z, w)
        uh = u / np.linalg.norm(u)
        dv = gt - mus * g0 * uh
        dw = 5 * mus * g0 / (2 * R) * np.cross(z, uh)
        return np.concatenate([v, dv, dw])
    y = np.concatenate([[0, 0, 0], [v0[0], v0[1], 0], w0]); h = T / n
    for _ in range(n):
        k1 = f(y); k2 = f(y + h / 2 * k1); k3 = f(y + h / 2 * k2); k4 = f(y + h * k3)
        y = y + h / 6 * (k1 + 2 * k2 + 2 * k3 + k4)
    return y

def roll_case(v0, s, mur=0.01):
    gt = -g0 * np.array(s)
    return Pursuit(v0, 5.0 / 7.0 * gt, mur * g0), gt

if __name__ == "__main__":
    # HF-T15 (a)
    P, gt = roll_case((0.5, 0), (0, 1e-3))
    print("T15a Tstop", repr(P.Tstop()), "stop disp", P.X_inf())
    x, X = P.state(2.0)
    print("T15a t=2 v", x, "r", X)
    y = rk4_roll((0.5, 0), gt, 0.01, 2.0, 200000)
    print("  RK4 t=2", y, "diff", np.abs(y[2:] - x).max(), np.abs(y[:2] - X).max())
    # (b)
    P, gt = roll_case((1.0, 0.3), (2e-3, -1e-3))
    print("T15b Tstop", repr(P.Tstop()))
    x, X = P.state(3.3)
    print("T15b t=3.3 v", x, "r", X)
    y = rk4_roll((1.0, 0.3), gt, 0.01, 3.3, 200000)
    print("  RK4", y, "diff", np.abs(y[2:] - x).max(), np.abs(y[:2] - X).max())
    # near-stop oracle for (a): integrate RK4 to Tstop - 0.05 s then compare
    P, gt = roll_case((0.5, 0), (0, 1e-3))
    Ts = P.Tstop()
    y = rk4_roll((0.5, 0), gt, 0.01, Ts - 0.01, 400000)
    x, X = P.state(Ts - 0.01)
    print("  RK4 at Tstop-0.01", np.abs(y[2:] - x).max(), np.abs(y[:2] - X).max(), "speed", np.linalg.norm(x))
    # 4.5.4 magnitudes
    P, gt = roll_case((1.0, 0), (0, 1e-3)); print("mag 1 m/s", P.Tstop(), P.X_inf())
    P, gt = roll_case((0.3, 0), (0, 1e-3)); print("mag 0.3 m/s", P.Tstop(), P.X_inf())
    # HF-T17 sliding stun
    mus = 0.2; s = (0, 3e-3); gt = -g0 * np.array(s)
    v0 = np.array([2.0, 0]); w0 = np.zeros(3)
    u0 = v0.copy()  # w=0
    Ps = Pursuit(u0, gt, 3.5 * mus * g0)
    T = Ps.Tstop(); XI = Ps.X_inf()
    Lc0 = v0 - 2.0 / 7.0 * u0
    vend = Lc0 + 5.0 / 7.0 * gt * T
    rend = Lc0 * T + 5.0 / 14.0 * gt * T * T + 2.0 / 7.0 * XI
    print("T17 slide T", repr(T), "level", 2 * 2 / (7 * mus * g0), "v", vend, "r", rend)
    y = rk4_slide(v0, w0, gt, mus, T * (1 - 1e-6), 200000)
    print("  RK4 slide near end", y[:2], y[3:5], "diff r", np.abs(y[:2] - rend).max())
    # frozen-direction error
    uh = u0 / np.linalg.norm(u0)
    # frozen: du/dt = gt - 3.5 mus g uh0 (uh0 fixed) -> not exactly; freeze u_hat for acceleration
    a = gt - mus * g0 * uh
    # end when? use the same T as the exact (as spec compares positions) : compute frozen path at exact T
    rf = v0 * T + 0.5 * a * T * T
    print("  frozen-direction pos at T", rf, "err", np.linalg.norm(rf - rend))
    # collinear
    for sx in (-3e-3, 3e-3):
        P, gt = roll_case((0.5, 0), (sx, 0))
        print("T17 collinear", sx, P.Tstop(), P.X_inf())
