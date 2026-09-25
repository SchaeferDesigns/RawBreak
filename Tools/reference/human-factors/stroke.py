# Independent re-implementation of human-factors.md 3.3-3.6 (verifier)
import math
from noise import *

DEG = math.pi / 180

def L(x, rho):
    x = min(max(x, 0.0), 100.0)
    return rho ** ((x - 25.0) / 75.0)

def smooth01(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)

def E_env(t):
    return 1 + math.exp(-t / 0.8) + min(0.5, 0.03 * max(0.0, t - 10.0))

def k_set(t, ts):
    if ts < 0:
        return 1.0
    tau = t - ts
    if tau < 0:
        return 1.0
    if tau < 1.2:
        return 1 - 0.7 * smooth01(tau / 1.2)
    if tau < 5.2:
        return 0.3
    return 0.3 + 0.85 * smooth01((tau - 5.2) / 1.5)

BR = {"Closed": (1.0, 6.0), "Open": (1.2, 4.0), "Rail": (1.4, 3.5), "Elevated": (2.0, 2.5), "Mechanical": (1.8, 3.0)}

DEFAULT = dict(sigma_drift=0.9e-3, sigma_tr=0.03e-3, sigma_A=0.20e-3, sigma_B=1.5e-3, kappa_off=0.06,
               sigma_theta=0.4 * DEG, s_V=0.05, phi_fl=0.08, gmax1=4.0, b_grip=1.5e-3)

def execute(intended, attrs, sit, tip, key, NS=1.0, mask=(), warp=None, R=0.028575, Stratify=True, swoop_mode="hf"):
    """intended: dict phi, theta, A, B, V, vr, vu, tc, ts, pause, ac, head
       attrs: dict St, SC, ST, BS, Sta, N
       sit: dict bridge, Lb, Lbg, ds, P, F, S, glove, off
       tip: dict r_dome, w_tip, mu_fresh, mu_bare, cov (7 list), overhang
       key: dict seed, rack, shot, shooter, sshot, pickup
       warp: None or dict s_w, Lcue, known"""
    p = DEFAULT
    seed, rack, shot, sh, n = key["seed"], key["rack"], key["shot"], key["shooter"], key["sshot"]
    def ns(ch):
        return 0.0 if ch in mask else NS
    def eps(ch):
        if Stratify:
            return bag_eps(seed, sh, ch, n)
        return truncnormal(u01(hashkeys(seed, rack, shot, sh, ch, 0)))
    def uu(ch):
        if Stratify:
            return bag_u(seed, sh, ch, n)
        return u01(hashkeys(seed, rack, shot, sh, ch, 0))
    t = intended["tc"]
    P = sit["P"]
    LN = L(attrs["N"], 0.125)
    g = 1 + p["gmax1"] * P * LN
    m_br0, Vb0 = BR[sit["bridge"]]
    m_br = 1 + (m_br0 - 1) * L(attrs["BS"], 0.4)
    Vb = Vb0 * (1 + 0.6 * (min(max(attrs["BS"], 0), 100) - 25) / 75) * (1.15 if sit["glove"] else 1) * (1 - 0.3 * sit["S"])
    Vi = intended["V"]
    m_slip = 1 + 0.5 * max(0.0, Vi - Vb) / Vb
    m_st = 1 + sit["ds"] * L(attrs["Sta"], 0.2)
    m_head = 1.5 if intended["head"] else 1.0
    m_stick = 1 + 0.5 * sit["S"] * (0.2 if sit["glove"] else 1.0)
    m_fat = 1 + 0.2 * sit["F"]
    m_off = 2.5 if sit["off"] else 1.0
    m_rush = 1 + 0.3 * min(max(1 - intended["pause"] / 0.2, 0), 1)
    m_jab = 1 + 0.5 * min(max(-intended["ac"] / 10.0, 0), 1)
    E = E_env(t)
    ks = k_set(t, intended["ts"])
    Lb, Lbg = sit["Lb"], sit["Lbg"]
    Lbc = Lb + R
    Dl = Process(seed, rack, shot, sh, 1, 0.15, 0.6)
    Dv = Process(seed, rack, shot, sh, 2, 0.15, 0.6)
    Tl = Process(seed, rack, shot, sh, 3, 8.0, 12.0)
    Tv = Process(seed, rack, shot, sh, 4, 8.0, 12.0)
    sig_dr = p["sigma_drift"] * L(attrs["St"], 0.2) * E * ks * g ** DEFAULT.get("drift_gpow", 0.5) * m_br * m_st * m_head * m_stick * m_slip * m_fat * m_off
    y_g = ns(1) * sig_dr * Dl.D(t)
    z_g = ns(2) * 0.5 * sig_dr * Dv.D(t)
    sig_t = p["sigma_tr"] * g * ks * m_fat * m_off
    tr_r = ns(3) * sig_t * Tl.D(t)
    tr_u = ns(4) * sig_t * Tv.D(t)
    xST, xSC = attrs["ST"], attrs["SC"]
    Ai, Bi = intended["A"], intended["B"]
    sA = math.hypot(p["sigma_A"] * L(xST, .25), p["kappa_off"] * L(xST, .25) * Ai * R) * m_st * m_rush * m_head * m_fat * m_off
    sB = math.hypot(p["sigma_B"] * L(xST, .2), p["kappa_off"] * L(xST, .25) * Bi * R) * m_st * m_rush * m_fat * m_off
    sTh = p["sigma_theta"] * L(xST, .25) * m_br * m_st * m_slip * m_fat * m_off
    sV = p["s_V"] * L(xSC, .3) * (1 + 0.5 * max(0.0, 1 - Vi / 1.0)) * m_rush * m_jab * math.sqrt(g) * m_fat * m_off
    fl = ns(9) * P * p["phi_fl"] * LN * uu(9)
    bias = -ns(9) * p["b_grip"] * P * LN   # bias uses NS (channel of grip = flinch family)
    eA, eB, eEl, eV = eps(5), eps(6), eps(7), eps(8)
    dphi_w = dth_w = 0.0
    if warp is not None:
        gamma = 0.5 * 4 * warp["s_w"] / warp["Lcue"]
        chi = 0.0 if warp["known"] else 2 * math.pi * u01(hashkeys(seed, sh, 10, key["pickup"]))
        dphi_w, dth_w = gamma * math.sin(chi), gamma * math.cos(chi)
    yaw = y_g / Lbg
    pitch = z_g / Lbg
    phi_x = intended["phi"] + yaw + dphi_w
    th_raw = intended["theta"] + pitch + ns(7) * eEl * sTh + dth_w
    theta_x = max(sit.get("floor", 0.0), th_raw)
    A_x = Ai + (-yaw * Lbc + tr_r + ns(5) * eA * sA) / R
    B_x = Bi + (-pitch * Lbc + tr_u + ns(6) * eB * sB + bias) / R
    V_x = min(max(Vi * (1 + ns(8) * eV * sV) * (1 - fl), 0.0), 12.0)
    lever = Lbc if swoop_mode == "hf" else Lb
    v_r = intended["vr"] - ns(1) * sig_dr * Dl.Dp(t) * lever / Lbg + ns(3) * sig_t * Tl.Dp(t)
    v_u = intended["vu"] - ns(2) * 0.5 * sig_dr * Dv.Dp(t) * lever / Lbg + ns(4) * sig_t * Tv.Dp(t)
    # 3.6
    rd, wt = tip["r_dome"], tip["w_tip"]
    a, b = A_x * R / (R + rd), B_x * R / (R + rd)
    rho = math.hypot(a, b)
    clamped = False
    if rho > 0.9:
        a, b = a * 0.9 / rho, b * 0.9 / rho
        rho = 0.9
        clamped = True
    q = rd * rho / (wt / 2)
    beta = math.atan2(-b, -a)
    if q <= 1:
        zone = "Dome"
        mu = tip["mu_bare"] + (tip["mu_fresh"] - tip["mu_bare"]) * chalk_c(tip["cov"], q, beta)
    elif q <= 1 + tip.get("overhang", 0.0) / (wt / 2):
        zone = "Overhang"; mu = 0.30
    else:
        zone = "Ferrule"; mu = 0.20
    c = math.sqrt(1 - rho * rho)
    cospsi = (-a * v_r + c * V_x - b * v_u) / math.sqrt(V_x ** 2 + v_r ** 2 + v_u ** 2) if V_x > 0 else c
    rho_eff = math.sqrt(max(0.0, 1 - cospsi ** 2))
    mlim = mu / math.sqrt(1 + mu * mu)
    return dict(phi_x=phi_x, theta_x=theta_x, A_x=A_x, B_x=B_x, V_x=V_x, v_r=v_r, v_u=v_u, a=a, b=b, rho=rho, mu=mu,
                zone=zone, rho_eff=rho_eff, miscue=rho_eff > mlim, mlim=mlim, y_g=y_g, z_g=z_g, eA=eA, eB=eB, eEl=eEl, eV=eV,
                E=E, g=g, ks=ks, fl=fl, clamped=clamped, sA=sA, sB=sB, sTh=sTh, sV=sV, sig_dr=sig_dr, q=q, beta=beta,
                tr_r=tr_r, tr_u=tr_u, yaw=yaw, pitch=pitch)

def chalk_weights(q, beta):
    wr = smooth01((q - 0.2) / 0.3)
    s = beta / (math.pi / 3)
    s = s - 6 * math.floor(s / 6)
    k0 = int(math.floor(s))
    if k0 >= 6:
        k0 = 5
    f = s - k0
    w = [0.0] * 7
    w[0] = 1 - wr
    w[1 + k0] += wr * (1 - f)
    w[1 + (k0 + 1) % 6] += wr * f
    return w

def chalk_c(cov, q, beta):
    w = chalk_weights(q, beta)
    return sum(wi * ci for wi, ci in zip(w, cov))

def S0():
    intended = dict(phi=0.0, theta=3 * DEG, A=0.0, B=0.0, V=2.0, vr=0.0, vu=0.0, tc=2.5, ts=-1.0, pause=0.4, ac=0.0, head=False)
    attrs = dict(St=25, SC=25, ST=25, BS=25, Sta=25, N=25)
    sit = dict(bridge="Closed", Lb=0.20, Lbg=0.80, ds=0.0, P=0.0, F=0.0, S=0.0, glove=False, off=False)
    tip = dict(r_dome=0.0106, w_tip=0.01275, mu_fresh=0.6, mu_bare=0.35, cov=[1.0] * 7, overhang=0.0)
    key = dict(seed=0x5EED, rack=0, shot=0, shooter=1, sshot=0, pickup=0)
    return intended, attrs, sit, tip, key

if __name__ == "__main__":
    I, A, S, T, K = S0()
    r = execute(I, A, S, T, K)
    for k in ["phi_x", "theta_x", "A_x", "B_x", "V_x", "v_r", "v_u", "a", "b", "mu", "miscue", "y_g", "z_g", "eA", "eB", "eEl", "eV", "E"]:
        print("T05", k, repr(r[k]))
    I2 = dict(I); I2["ts"] = 0.8
    S2 = dict(S); S2["P"] = 1.0
    r = execute(I2, A, S2, T, K)
    print("T06", r["g"], r["ks"], r["fl"], r["phi_x"], r["theta_x"], r["A_x"], r["B_x"], r["V_x"])
    A3 = {k: 100 for k in A}
    r = execute(I, A3, S, T, K)
    print("T07", r["phi_x"], r["theta_x"], r["A_x"], r["B_x"], r["V_x"])
    r = execute(I, A, S, T, K, NS=0.0)
    print("T08", r["phi_x"], r["theta_x"] == 3 * DEG, r["A_x"], r["B_x"], r["V_x"], r["mu"])
