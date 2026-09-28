#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RAW BREAK - physics-driven impact sound synthesis (prototype for Docs/specs/audio.md, section 3).

Model (all SI units; DERIVED unless marked ESTIMATE):
  1. Contact force F(t): the core's Hertz + Tsuji contact (physics-collisions 3.9.3):
     F = max(0, K d^1.5 + eta d^0.25 d'),
     eta = alpha_T(e) sqrt(m* K), K = 8.0587e8 N/m^1.5 for two standard balls, e_b = 0.95. Integrated with RK4 at
     4 x 768 kHz. Other contacts (cushion, slate, liner, tip) use the same law with an ESTIMATE stiffness chosen from a
     contact time, and the core's restitution (cushion e_c(v) law, e_slate 0.6).
  2. Radiation of each ball = a free sphere of radius a (Koss & Alfredson 1973; Chadwick et al. 2012): the surface
     normal motion is expanded in Legendre orders n; order n = 1 is the rigid-body translation ("acceleration
     noise"), orders 0/1/2/3 also carry the elastic spheroidal modes of Lamb's sphere. Each order radiates exactly
     (e^{+iwt}, h_n = spherical Hankel of the 2nd kind, V_n = surface normal velocity, A_n = i w V_n acceleration):
         p_n(r, th, w) = -i rho0 c V_n P_n(cos th) h_n(kr) / h_n'(ka) = -rho0 c A_n P_n(cos th) h_n(kr) / (w h_n'(ka))
     The cloth plane adds one image source per ball (ESTIMATE reflection 0.9 with a one-pole cloth roll-off).
  3. Table structure (rail/cabinet, bed, pocket, cue) = ESTIMATE modal banks radiating as baffled pistons.
  4. Scheduling: every event is placed at its exact shot time (integer sample + fractional delay in the frequency
     domain); the selftest checks onset separations of 24.0, 24.4992 and 5.0016 samples at 48 kHz.
  5. Runtime reference (`runtime_render`): the exact time-domain algorithm the C++ port implements (audio.md 3.6);
     `golden_runtime_48k.json` holds its golden vectors.
  6. Presentation (audio.md 4.2): per-impact static gain curve per dynamic-range mode, loudness (BS.1770-4) and
     true-peak metering of the renders.

Outputs are calibrated: WAV sample value = sound pressure / FULL_SCALE_PA (100 Pa = 134 dB SPL peak at full scale),
except the `*_wide/normal/night.wav` presentation renders (digital level of that mode, audio.md 4.2).

Usage (from the repo root, numpy + scipy; the pooltool venv has both):
  Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py              # standard set, analysis, references
  Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py --selftest   # numeric checks against the specs
  Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py --break Tools/rbsim/examples/break9.json
  Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py --analyze some_recording.wav [--full-scale-pa X]
"""

import argparse
import json
import math
import os
import sys
import wave
from dataclasses import dataclass, field

import numpy as np
from scipy import io as sio
from scipy import optimize, signal, special

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out")
REF_DIR = os.path.join(OUT_DIR, "ref")

# ---------------------------------------------------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------------------------------------------------
FS = 48000                      # game / output sample rate [Hz]
OVERSAMPLE = 16                 # force pulse grid = FS * OVERSAMPLE (768 kHz); RK4 runs 4 sub-steps per grid step
FS_HI = FS * OVERSAMPLE
RHO0 = 1.204                    # air density at 20 degC [kg/m^3]
C0 = 343.2                      # speed of sound at 20 degC [m/s]
P_REF = 20e-6                   # [Pa]
FULL_SCALE_PA = 100.0           # calibrated WAVs: 1.0 = 100 Pa (134 dB SPL peak)
TAPER_HI = (20000.0, 23500.0)   # band-limit taper [Hz] (anti-alias; everything above 23.5 kHz is dropped)
TAPER_LO = (5.0, 20.0)          # remove the incompressible near-field DC term

# Runtime reference (audio.md 3.6): the C++ DSP evaluates the pulse at 4 x FS and decimates with this FIR
DEC_FACTOR = 4
DEC_TAPS = 129                  # linear phase, delay 64 samples at 192 kHz = 16 samples at 48 kHz
DEC_CUTOFF_HZ = 24000.0
DEC_KAISER_BETA = 8.0
KERNEL_PRE = 32                 # pre-delay of the per-order ball kernels [samples]
NF_LEAK_HZ = 2.0                # corner of the leaky integrator of the order-1 near-field term

# Presentation per dynamic-range mode (audio.md 4.2; ESTIMATE, tune in the listening test):
#   l_fs  = sound pressure level [dB SPL] that maps to 0 dBFS (sine peak)
#   knee  = physical peak envelope of the table stem at the listener [dB SPL] above which the gain curve compresses
#   ratio = slope of the curve above the knee (1 dB presented per `ratio` dB physical)
# The envelope is computed at plan time from the known future (lookahead 3 ms, release 60 ms, 1 ms smoothing), so
# overlapping clicks of a break cluster share one gain and single clicks keep their waveform.
#   amb   = presentation offset of the ambience/crowd/music buses [dB] (voices: +3 / +4 / +6 dB, audio.md 4.2)
PRESENTATION_MODES = {
    "wide": dict(l_fs=114.0, knee=105.0, ratio=3.0, amb=0.0),
    "normal": dict(l_fs=106.0, knee=97.0, ratio=4.0, amb=-2.0),
    "night": dict(l_fs=100.0, knee=95.0, ratio=10.0, amb=-4.0),
}
PRESENTATION_LOOKAHEAD_S = 0.003
PRESENTATION_RELEASE_S = 0.060
PRESENTATION_SMOOTH_S = 0.001

# Core values (Docs/specs/equipment.md 6.1, physics-collisions.md 3.9.3, Source/.../Compliant.cpp, Cushion.h)
R_STD = 0.028575                # ball radius [m]
M_STD = 0.170097                # ball mass [kg] (core default)
K_CORE = 8.0587e8               # Hertz stiffness ball-ball [N/m^1.5] (Marlow via Alciatore TP B.29)
E_BALL = 0.95                   # ball-ball restitution
ALPHA_T_CORE = 0.036915         # core table value alpha_T(0.95) for its semi-implicit Euler integrator (1 us)
NOSE_HEIGHT_7FT = 0.0362903     # cushion nose height above the cloth [m] (rbsim geometry, 63.5 % of 2R)
E_SLATE = 0.6                   # ball-slate restitution (motion spec C.5)


def cushion_restitution(v):
    """Core e_c(v_perp) law (Cushion.h): e(0.5) = e(1) = 0.97, e(3) = 0.90, e(10) = 0.655, e(20) = 0.60."""
    return float(np.clip(0.97 - 0.035 * max(0.0, v - 1.0), 0.60, 0.97))


# ---------------------------------------------------------------------------------------------------------------------
# Materials and balls
# ---------------------------------------------------------------------------------------------------------------------
@dataclass(frozen=True)
class Material:
    name: str
    E: float        # Young's modulus [Pa]
    nu: float       # Poisson's ratio
    loss: float     # structural loss factor eta (modal damping ratio = eta / 2)


@dataclass(frozen=True)
class BallSpec:
    key: str
    label: str
    R: float
    m: float
    mat: Material

    @property
    def rho(self):
        return self.m / (4.0 / 3.0 * math.pi * self.R ** 3)


def young_from_hertz(K, R, nu):
    """E of two identical spheres from their Hertz constant K = (4/3) E* sqrt(R/2), E* = E / (2 (1 - nu^2))."""
    e_star = 3.0 * K / (4.0 * math.sqrt(R / 2.0))
    return 2.0 * (1.0 - nu * nu) * e_star


NU_PHENOLIC = 0.35      # ESTIMATE (filled thermoset, 0.33-0.38)
E_PHENOLIC = young_from_hertz(K_CORE, R_STD, NU_PHENOLIC)   # DERIVED from the core's K: ~8.9 GPa
PHENOLIC = Material("cast phenolic (derived from the core's Hertz K)", E_PHENOLIC, NU_PHENOLIC, 0.015)  # loss ESTIMATE
POLYESTER = Material("polyester bar set (ESTIMATE)", 5.0e9, 0.36, 0.030)                               # ESTIMATE

BALLS = {
    "std": BallSpec("std", "standard phenolic 57.15 mm / 170.1 g", R_STD, M_STD, PHENOLIC),
    "bar_ob": BallSpec("bar_ob", "worn dive-bar object ball 57.10 mm / 163 g", 0.02855, 0.163, PHENOLIC),
    "bar_poly": BallSpec("bar_poly", "polyester dive-bar ball 57.15 mm / 163 g", R_STD, 0.163, POLYESTER),
    "magnetic_cb": BallSpec("magnetic_cb", "magnetic bar cue ball 57.15 mm / 167 g (homogeneous approx.)", R_STD, 0.167,
                            PHENOLIC),
    "oversized_cb": BallSpec("oversized_cb", "oversized bar cue ball 60.325 mm / 221.1 g", 0.0301625, 0.2211, PHENOLIC),
}


def hertz_K(b1, b2):
    r_star = b1.R * b2.R / (b1.R + b2.R)
    inv = (1.0 - b1.mat.nu ** 2) / b1.mat.E + (1.0 - b2.mat.nu ** 2) / b2.mat.E
    return 4.0 / 3.0 * math.sqrt(r_star) / inv


# ---------------------------------------------------------------------------------------------------------------------
# Contact force pulse (Hertz + Tsuji), exactly the core's law
# ---------------------------------------------------------------------------------------------------------------------
@dataclass
class Pulse:
    F: np.ndarray       # force samples on the FS_HI grid, F[0] at t = 0 [N]
    duration: float     # contact time [s]
    fmax: float         # peak force [N]
    impulse: float      # integral of F dt [N s]
    e: float            # achieved restitution


def _integrate_hertz(v, m_star, K, alpha, sub=4, max_time=0.05):
    """RK4 on d'' = -F/m*, F = max(0, K d^1.5 + eta d^0.25 d'). Returns (F on the FS_HI grid, duration, exit speed)."""
    eta = alpha * math.sqrt(m_star * K)
    h = 1.0 / (FS_HI * sub)

    def acc(d, dd):
        if d <= 0.0:
            return 0.0
        f = K * d ** 1.5 + eta * d ** 0.25 * dd
        return -f / m_star if f > 0.0 else 0.0

    def force(d, dd):
        if d <= 0.0:
            return 0.0
        f = K * d ** 1.5 + eta * d ** 0.25 * dd
        return f if f > 0.0 else 0.0

    d, dd = 0.0, float(v)
    forces = [0.0]
    step = 0
    max_steps = int(max_time / h)
    while step < max_steps:
        k1d, k1v = dd, acc(d, dd)
        k2d, k2v = dd + 0.5 * h * k1v, acc(d + 0.5 * h * k1d, dd + 0.5 * h * k1v)
        k3d, k3v = dd + 0.5 * h * k2v, acc(d + 0.5 * h * k2d, dd + 0.5 * h * k2v)
        k4d, k4v = dd + h * k3v, acc(d + h * k3d, dd + h * k3v)
        d += h / 6.0 * (k1d + 2 * k2d + 2 * k3d + k4d)
        dd += h / 6.0 * (k1v + 2 * k2v + 2 * k3v + k4v)
        step += 1
        if step % sub == 0:
            forces.append(force(d, dd))
        if d <= 0.0 and dd < 0.0:
            break
    forces.append(0.0)
    return np.array(forces), step * h, -dd


_ALPHA_CACHE = {}


def alpha_for_restitution(e):
    """Tsuji alpha_T for a Hertz contact with restitution e (speed independent for the d^1.5 / d^0.25 pair)."""
    if e >= 0.9999:
        return 0.0
    key = round(e, 4)
    if key in _ALPHA_CACHE:
        return _ALPHA_CACHE[key]
    m_star, K = M_STD / 2.0, K_CORE

    def resid(alpha):
        _, _, v_out = _integrate_hertz(1.0, m_star, K, alpha)
        return v_out - e

    alpha = optimize.brentq(resid, 1e-6, 1.5, xtol=1e-7)
    _ALPHA_CACHE[key] = alpha
    return alpha


def hertz_pulse(v, m_star, K, e):
    alpha = alpha_for_restitution(e)
    F, dur, v_out = _integrate_hertz(v, m_star, K, alpha)
    J = float(np.sum(F) / FS_HI)
    return Pulse(F, dur, float(F.max()), J, v_out / v)


def stiffness_for_contact_time(T1, m_star, v=1.0):
    """K such that the undamped Hertz contact time at v is T1: T = 3.2181 (m*^2 / (K^2 v))^(1/5) (physics 3.9.3)."""
    return math.sqrt(m_star ** 2 / (v * (T1 / 3.2181) ** 5))


def half_sine_pulse(J, T, power=1.5):
    """sin^power(pi t / T) pulse with integral J (tip strike, soft drops). The runtime shape for every Hertz contact."""
    n = max(4, int(round(T * FS_HI)))
    t = (np.arange(n + 1)) / n
    shape = np.sin(np.pi * t) ** power
    F = J * shape / (np.sum(shape) / FS_HI)
    return Pulse(F, T, float(F.max()), J, float("nan"))


# Contact presets (ESTIMATE unless noted): contact time at 1 m/s and the effective mass of the struck side
CONTACTS = {
    "cushion": dict(T1=2.5e-3, note="cloth on gum-rubber nose; ESTIMATE 1.3-5 ms (core CLI k_c 1e6 N/m -> 1.3 ms)"),
    "facing": dict(T1=1.2e-3, note="hard-rubber pocket facing; ESTIMATE"),
    "railtop": dict(T1=0.35e-3, note="wooden rail cap (airborne ball); ESTIMATE"),
    "slate": dict(T1=0.45e-3, note="worsted cloth over 1 in slate; ESTIMATE"),
    "liner": dict(T1=1.0e-3, note="pocket liner / gully boot; ESTIMATE"),
}


# ---------------------------------------------------------------------------------------------------------------------
# Lamb's free elastic sphere: spheroidal modes (orders n = 0..3), modal masses for a pole force
# ---------------------------------------------------------------------------------------------------------------------
def _lame(mat):
    lam = mat.E * mat.nu / ((1.0 + mat.nu) * (1.0 - 2.0 * mat.nu))
    mu = mat.E / (2.0 * (1.0 + mat.nu))
    return lam, mu


def _jn3(n, x):
    j = special.spherical_jn(n, x)
    jp = special.spherical_jn(n, x, derivative=True)
    jpp = -2.0 / x * jp - (1.0 - n * (n + 1) / x ** 2) * j
    return j, jp, jpp


def _lamb_rows(n, omega, a, lam, mu, rho):
    cL = math.sqrt((lam + 2 * mu) / rho)
    cT = math.sqrt(mu / rho)
    kL, kT = omega / cL, omega / cT
    f, fp, fpp = _jn3(n, kL * a)
    fp, fpp = fp * kL, fpp * kL ** 2
    g, gp, gpp = _jn3(n, kT * a)
    gp, gpp = gp * kT, gpp * kT ** 2
    srr_a = -lam * kL ** 2 * f + 2 * mu * fpp
    srr_b = 2 * mu * n * (n + 1) * (gp / a - g / a ** 2)
    srt_a = 2 * fp / a - 2 * f / a ** 2
    srt_b = gpp + (n * (n + 1) - 2) * g / a ** 2
    return srr_a, srr_b, srt_a, srt_b


def _lamb_det(n, x, a, lam, mu, rho):
    cT = math.sqrt(mu / rho)
    omega = x * cT / a
    srr_a, srr_b, srt_a, srt_b = _lamb_rows(n, omega, a, lam, mu, rho)
    if n == 0:
        return srr_a
    return (srr_a * srt_b - srr_b * srt_a) / (mu * mu)


def lamb_roots(n, mat, a, rho, x_max=14.0, count=2):
    """First `count` roots x = k_T a of the stress-free spheroidal frequency equation of order n (Lamb 1882)."""
    lam, mu = _lame(mat)
    xs = np.linspace(0.3, x_max, 6000)
    vals = np.array([_lamb_det(n, x, a, lam, mu, rho) for x in xs])
    roots = []
    for i in range(len(xs) - 1):
        if np.sign(vals[i]) != np.sign(vals[i + 1]):
            roots.append(optimize.brentq(lambda x: _lamb_det(n, x, a, lam, mu, rho), xs[i], xs[i + 1], xtol=1e-12))
            if len(roots) >= count:
                break
    return roots


@dataclass
class Mode:
    n: int
    root: int
    x: float          # k_T a
    f: float          # [Hz]
    mass: float       # modal mass for unit pole radial displacement [kg]


def lamb_mode(n, x, ball, root_index):
    mat, a, rho = ball.mat, ball.R, ball.rho
    lam, mu = _lame(mat)
    cL, cT = math.sqrt((lam + 2 * mu) / rho), math.sqrt(mu / rho)
    omega = x * cT / a
    srr_a, srr_b, srt_a, srt_b = _lamb_rows(n, omega, a, lam, mu, rho)
    if n == 0:
        A, B = 1.0, 0.0
    elif abs(srr_a) + abs(srr_b) > 1e-12 * (abs(srt_a) + abs(srt_b)) * mu:
        A, B = srr_b, -srr_a
    else:
        A, B = srt_b, -srt_a
    kL, kT = omega / cL, omega / cT
    r = np.linspace(a * 1e-4, a, 4001)
    f = special.spherical_jn(n, kL * r)
    fp = kL * special.spherical_jn(n, kL * r, derivative=True)
    g = special.spherical_jn(n, kT * r)
    gp = kT * special.spherical_jn(n, kT * r, derivative=True)
    U = A * fp + B * n * (n + 1) * g / r
    V = A * f / r + B * (g / r + gp)
    s = 1.0 / U[-1]
    U, V = U * s, V * s
    integrand = (U ** 2 * 2.0 / (2 * n + 1) + V ** 2 * 2.0 * n * (n + 1) / (2 * n + 1)) * r ** 2
    mass = rho * 2.0 * math.pi * np.trapezoid(integrand, r)
    return Mode(n, root_index, x, omega / (2 * math.pi), float(mass))


_MODE_CACHE = {}


def sphere_modes(ball, f_max=40000.0):
    if ball.key in _MODE_CACHE:
        return _MODE_CACHE[ball.key]
    modes = []
    for n in (0, 1, 2, 3):
        for i, x in enumerate(lamb_roots(n, ball.mat, ball.R, ball.rho, count=2)):
            m = lamb_mode(n, x, ball, i + 1)
            if m.f <= f_max:
                modes.append(m)
    modes.sort(key=lambda m: m.f)
    _MODE_CACHE[ball.key] = modes
    return modes


# ---------------------------------------------------------------------------------------------------------------------
# Radiation
# ---------------------------------------------------------------------------------------------------------------------
def _h2(n, x):
    return special.spherical_jn(n, x) - 1j * special.spherical_yn(n, x)


def _h2p(n, x):
    return special.spherical_jn(n, x, derivative=True) - 1j * special.spherical_yn(n, x, derivative=True)


def radiation_accel(n, f, r, a):
    """Pressure at (r, th) per unit surface normal ACCELERATION amplitude of order n (times P_n(cos th)).
    e^{+iwt} convention, Euler i w rho0 v = -dp/dr: p = -i rho0 c V h_n(kr) / h_n'(ka) for the surface normal VELOCITY
    V; with V = A / (iw):  p / A = -rho0 c h_n(kr) / (w h_n'(ka))."""
    out = np.zeros(len(f), dtype=complex)
    nz = f > 0
    w = 2.0 * np.pi * f[nz]
    k = w / C0
    out[nz] = -RHO0 * C0 * _h2(n, k * r) / (w * _h2p(n, k * a))
    return out


def band_taper(f):
    """Causal 2nd-order Butterworth high-pass at 12 Hz (drops the incompressible DC term without circular pre-ringing)
    times a short zero-phase raised-cosine band limit 20-23.5 kHz (anti-alias for the 48 kHz output)."""
    s = 1j * f / 12.0
    t = (s * s / (s * s + math.sqrt(2.0) * s + 1.0)).astype(complex)
    hi0, hi1 = TAPER_HI
    m = f > hi0
    t[m] *= 0.5 + 0.5 * np.cos(np.pi * np.clip((f[m] - hi0) / (hi1 - hi0), 0.0, 1.0))
    return t


# ESTIMATE: cloth-covered slate as a mirror with a one-pole high-frequency loss
CLOTH_REFLECTION = 0.9
CLOTH_CORNER_HZ = 8000.0


def cloth_reflection(f):
    return CLOTH_REFLECTION / (1.0 + 1j * f / CLOTH_CORNER_HZ)


@dataclass
class Body:
    """A radiating ball. axis = unit vector from the ball centre to the contact point; the force pushes along -axis."""
    ball: BallSpec
    center: np.ndarray
    axis: np.ndarray
    modes: bool = True


@dataclass
class Piston:
    """ESTIMATE structural radiator: modes = [(f Hz, modal mass kg, loss factor, radiating area m^2)], at pos.
    Radiates as a baffled piston (half space above the table) or a free monopole, times a radiation-efficiency
    high-pass sigma(f) = 1 / (1 + (fc/f)^2) for narrow parts (rails, cue) whose front/back short-circuit at low f."""
    name: str
    modes: list
    pos: np.ndarray
    fc: float = 0.0
    baffled: bool = True
    gain: float = 1.0


def body_transfer(f, body, listener, image=True):
    """p(f) / F(f) at `listener` for one ball (direct + cloth image)."""
    G = np.zeros(len(f), dtype=complex)
    sources = [(body.center, body.axis, 1.0)]
    if image and body.center[2] > 0.0:
        mirror = np.array([1.0, 1.0, -1.0])
        sources.append((body.center * mirror, body.axis * mirror, cloth_reflection(f)))
    a, m = body.ball.R, body.ball.m
    w = 2.0 * np.pi * f
    modes = sphere_modes(body.ball) if body.modes else []
    zeta = body.ball.mat.loss / 2.0
    for center, axis, refl in sources:
        d = listener - center
        r = float(np.linalg.norm(d))
        c = float(np.dot(d / r, axis))
        g = c * radiation_accel(1, f, r, a) * (-1.0 / m)             # rigid-body translation (order 1)
        for md in modes:
            wn = 2.0 * np.pi * md.f
            acc = w ** 2 / (md.mass * (wn ** 2 - w ** 2 + 2j * zeta * wn * w))   # pole radial accel per unit F
            g = g + special.eval_legendre(md.n, c) * radiation_accel(md.n, f, r, a) * acc
        G += g * refl
    return G


def piston_transfer(f, piston, listener):
    r = float(np.linalg.norm(listener - piston.pos))
    w = 2.0 * np.pi * f
    k = w / C0
    G = np.zeros(len(f), dtype=complex)
    for fn, M, eta, S in piston.modes:
        wn = 2.0 * np.pi * fn
        acc = w ** 2 / (M * (wn ** 2 - w ** 2 + 1j * eta * wn * w))
        G += RHO0 * S * acc / ((2.0 if piston.baffled else 4.0) * math.pi * r)
    if piston.fc > 0.0:
        G *= 1.0 / (1.0 + (piston.fc / np.maximum(f, 1e-3)) ** 2)
    return piston.gain * G * np.exp(-1j * k * r)


# ESTIMATE structural modal banks (tuned so that at equal speed a cushion thud sits ~18 dB below the on-axis click peak
# and ~6 dB below its SEL; replace with values fitted to recordings, plan 8.5 / audio.md 5.6)
RAIL_BARBOX = [  # coin-op bar box: hollow cabinet with the ball-return channel -> boomier than a pro table
    (82.0, 60.0, 0.15, 0.60),    # cabinet / slate bounce
    (145.0, 15.0, 0.10, 0.25),   # apron panel
    (240.0, 8.0, 0.08, 0.08),    # rail bending 1
    (390.0, 8.0, 0.08, 0.08),    # rail bending 2
    (610.0, 6.0, 0.09, 0.06),
    (950.0, 4.0, 0.10, 0.04),
    (1500.0, 3.0, 0.12, 0.03),
]
RAIL_BARBOX_FC = 400.0
RAIL_PRO = [(95.0, 150.0, 0.18, 0.6), (180.0, 20.0, 0.12, 0.2), (300.0, 10.0, 0.08, 0.08), (520.0, 8.0, 0.08, 0.06),
            (830.0, 6.0, 0.09, 0.04), (1300.0, 4.0, 0.11, 0.03)]
RAIL_PRO_FC = 400.0
BED_MODES = [(95.0, 150.0, 0.15, 1.0), (180.0, 60.0, 0.12, 0.5), (310.0, 30.0, 0.10, 0.3)]
BED_FC = 150.0
POCKET_MODES = [(160.0, 1.5, 0.25, 0.02), (420.0, 0.8, 0.20, 0.01), (900.0, 0.5, 0.25, 0.005)]
POCKET_FC = 300.0
# cue: longitudinal mode c/(2L) ~ 1.5 kHz radiates from the tip/butt faces; bending modes only via eccentricity;
# the bridge and grip hands damp everything (eta ~ 0.08). Free monopole, not baffled.
CUE_MODES = [(330.0, 0.25, 0.08, 2e-5), (870.0, 0.20, 0.08, 1.5e-5), (1520.0, 0.30, 0.06, 3e-4),
             (2600.0, 0.15, 0.08, 1e-5)]


# ---------------------------------------------------------------------------------------------------------------------
# Self-similar Hertz + Tsuji pulse (the runtime representation)
# ---------------------------------------------------------------------------------------------------------------------
_SHAPE_CACHE = {}


def hertz_shape(e, n=257):
    """Dimensionless contact x'' = -(x^1.5 + alpha x^0.25 x'), x(0) = 0, x'(0) = 1. With d0 = (m* v^2 / K)^0.4 and
    t0 = (m*^2 / (K^2 v))^0.2 every Hertz-Tsuji contact is this one curve (alpha depends on e only):
      T = tau_e t0,  F_max = phi_e K d0^1.5,  F(t) = F_max shape(t / T),  J = (1 + e) m* v."""
    key = (round(e, 4), n)
    if key in _SHAPE_CACHE:
        return _SHAPE_CACHE[key]
    alpha = alpha_for_restitution(e)
    h = 2e-5

    def acc(x, xd):
        if x <= 0:
            return 0.0
        fo = x ** 1.5 + alpha * x ** 0.25 * xd
        return -fo if fo > 0 else 0.0

    x, xd, s = 0.0, 1.0, 0.0
    ss, ff = [0.0], [0.0]
    while True:
        k1x, k1v = xd, acc(x, xd)
        k2x, k2v = xd + 0.5 * h * k1v, acc(x + 0.5 * h * k1x, xd + 0.5 * h * k1v)
        k3x, k3v = xd + 0.5 * h * k2v, acc(x + 0.5 * h * k2x, xd + 0.5 * h * k2v)
        k4x, k4v = xd + h * k3v, acc(x + h * k3x, xd + h * k3v)
        x += h / 6 * (k1x + 2 * k2x + 2 * k3x + k4x)
        xd += h / 6 * (k1v + 2 * k2v + 2 * k3v + k4v)
        s += h
        ss.append(s)
        ff.append(max(0.0, x ** 1.5 + alpha * x ** 0.25 * xd) if x > 0 else 0.0)
        if x <= 0 and xd < 0:
            break
    ss, ff = np.array(ss), np.array(ff)
    tau, phi = float(ss[-1]), float(ff.max())
    grid = np.linspace(0.0, tau, n)
    shape = np.interp(grid, ss, ff) / phi
    out = dict(e=e, alpha=alpha, tau=tau, phi=phi, shape=shape, area=float(np.trapezoid(shape, grid / tau)),
               exit_speed=-xd)
    _SHAPE_CACHE[key] = out
    return out


def runtime_pulse(v, m_star, K, e):
    sh = hertz_shape(e)
    t0 = (m_star ** 2 / (K ** 2 * v)) ** 0.2
    d0 = (m_star * v * v / K) ** 0.4
    T = sh["tau"] * t0
    fmax = sh["phi"] * K * d0 ** 1.5
    n = max(4, int(math.ceil(T * FS_HI)))
    t = np.arange(n + 1) / FS_HI
    F = fmax * np.interp(t / T, np.linspace(0, 1, len(sh["shape"])), sh["shape"], right=0.0)
    return Pulse(F, T, fmax, float(np.sum(F) / FS_HI), e)


# ---------------------------------------------------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------------------------------------------------
def pulse_spectrum(pulse, n_out):
    n_hi = n_out * OVERSAMPLE
    x = np.zeros(n_hi)
    L = min(len(pulse.F), n_hi)
    x[:L] = pulse.F[:L]
    return np.fft.rfft(x)[: n_out // 2 + 1] / FS_HI


@dataclass
class Impact:
    time: float                 # shot time of the contact start [s]
    pulse: Pulse
    bodies: list
    pistons: list = field(default_factory=list)
    duration: float = 0.25      # render window [s]
    label: str = ""
    lowpass_hz: float = 0.0     # optional one-pole muffling (sources inside the cabinet)


def render_impact_segment(imp, ears, t0=0.0):
    """Renders one impact at its exact time (sub-sample) for each ear position. Returns (i0, y[n_out, channels]):
    y[k] belongs to output sample i0 + k."""
    n_out = int(round(imp.duration * FS))
    n_out += n_out % 2
    f = np.fft.rfftfreq(n_out, 1.0 / FS)
    Fw = pulse_spectrum(imp.pulse, n_out) * band_taper(f)
    start = (imp.time - t0) * FS
    i0 = int(math.floor(start))
    frac = start - i0
    shift = np.exp(-2j * np.pi * f * frac / FS)
    y = np.zeros((n_out, len(ears)))
    for ch, ear in enumerate(ears):
        G = np.zeros(len(f), dtype=complex)
        for b in imp.bodies:
            G += body_transfer(f, b, ear)
        for p in imp.pistons:
            G += piston_transfer(f, p, ear)
        if imp.lowpass_hz > 0.0:
            G = G / (1.0 + 1j * f / imp.lowpass_hz)
        y[:, ch] = np.fft.irfft(Fw * G * shift, n_out) * FS
    return i0, y


def add_segment(buf, i0, y, gain=1.0):
    a0, a1 = max(i0, 0), min(i0 + y.shape[0], buf.shape[0])
    if a1 > a0:
        buf[a0:a1, :] += gain * y[a0 - i0:a1 - i0, :]


def render_impact(imp, ears, buf, t0=0.0, gain=1.0):
    """Adds the impact into buf (n, channels) at its exact time (sub-sample), one channel per ear position.
    Returns the physical peak of the impact over all ears [Pa] (before `gain`)."""
    i0, y = render_impact_segment(imp, ears, t0)
    add_segment(buf, i0, y, gain)
    return float(np.max(np.abs(y))) if y.size else 0.0


def render_single(imp, listener, pre=0.002):
    n = int(round((imp.duration + pre) * FS))
    buf = np.zeros((n, 1))
    imp_t = Impact(pre, imp.pulse, imp.bodies, imp.pistons, imp.duration, imp.label, imp.lowpass_hz)
    render_impact(imp_t, [listener], buf)
    return buf[:, 0]


# ---------------------------------------------------------------------------------------------------------------------
# Standard scenes
# ---------------------------------------------------------------------------------------------------------------------
def ball_ball_impact(v, b1=BALLS["std"], b2=BALLS["std"], e=E_BALL, duration=0.25):
    """Head-on: b1 (moving +x at v) hits b2 at rest, both resting on the cloth; the contact point is at x = 0.
    Unequal radii put the contact off the equator (equipment 6.3): only the normal component v n_x is compressive."""
    m_star = b1.m * b2.m / (b1.m + b2.m)
    s = b1.R + b2.R
    D = 2.0 * math.sqrt(b1.R * b2.R)                  # horizontal centre distance at contact
    c1 = np.array([-b1.R * D / s, 0.0, b1.R])
    c2 = np.array([b2.R * D / s, 0.0, b2.R])
    n = (c2 - c1) / s
    pulse = hertz_pulse(v * n[0], m_star, hertz_K(b1, b2), e)
    return Impact(0.0, pulse, [Body(b1, c1, n), Body(b2, c2, -n)], [], duration, f"ball-ball {v:g} m/s")


def cushion_impact(v, ball=BALLS["std"], rail_modes=RAIL_BARBOX, rail_fc=RAIL_BARBOX_FC, nose=NOSE_HEIGHT_7FT,
                   duration=0.8):
    """Ball moving -y into a long rail; the nose line at y = 0. Contact above the equator (nose height)."""
    dz = nose - ball.R
    dy = math.sqrt(ball.R ** 2 - dz ** 2)
    center = np.array([0.0, dy, ball.R])
    contact = np.array([0.0, 0.0, nose])
    axis = (contact - center) / ball.R
    e = cushion_restitution(v)
    K = stiffness_for_contact_time(CONTACTS["cushion"]["T1"], ball.m)
    pulse = hertz_pulse(v * abs(axis[1]), ball.m, K, e)
    rail = Piston("rail", rail_modes, contact.copy(), fc=rail_fc)
    return Impact(0.0, pulse, [Body(ball, center, axis)], [rail], duration, f"cushion {v:g} m/s")


LISTENERS = {
    # relative to the contact point; z above the cloth [m]
    "shooter": np.array([-1.00, 0.03, 0.30]),     # down on the shot, 1 m behind the collision, on its line
    "observer": np.array([0.00, 1.20, 0.85]),     # standing at the side rail, side-on to the collision line
    "cushion_front": np.array([0.00, 1.00, 0.30]),  # down on a shot, 1 m in front of the rail being hit
}


# ---------------------------------------------------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------------------------------------------------
def smooth_octave(f, mag2, frac=6):
    """1/frac-octave moving average of a power spectrum on its own (linear) frequency grid (vectorised)."""
    f = np.asarray(f)
    cs = np.concatenate([[0.0], np.cumsum(mag2)])
    lo = np.searchsorted(f, f * 2 ** (-0.5 / frac), side="left")
    hi = np.searchsorted(f, f * 2 ** (0.5 / frac), side="right")
    hi = np.maximum(hi, lo + 1)
    return (cs[hi] - cs[lo]) / (hi - lo)


def analyze(p, fs=FS, window_ms=60.0, onset_db=-30.0):
    """Metrics of one impact recording/render p [Pa]. Onset = first sample within onset_db of the peak."""
    p = np.asarray(p, dtype=float)
    if p.ndim > 1:
        p = p.mean(axis=1)
    peak = float(np.max(np.abs(p)))
    if peak <= 0:
        return {}
    idx_on = int(np.argmax(np.abs(p) >= peak * 10 ** (onset_db / 20.0)))
    a0 = max(0, idx_on - int(0.001 * fs))
    seg = p[a0:a0 + int(window_ms * 1e-3 * fs)]
    n = 1 << int(math.ceil(math.log2(max(len(seg), 1024) * 4)))
    P = np.fft.rfft(seg, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    mag2 = np.abs(P) ** 2
    band = (f >= 20) & (f <= 20000)
    centroid = float(np.sum(f[band] * mag2[band]) / np.sum(mag2[band]))
    fs_ = f[band]
    sm = smooth_octave(fs_, mag2[band])
    sm_db = 10 * np.log10(sm + 1e-300)
    ipk = int(np.argmax(sm_db))
    above = np.where(sm_db >= sm_db[ipk] - 10.0)[0]
    env = np.abs(signal.hilbert(p))
    k = max(1, int(0.0005 * fs))
    env = np.convolve(env, np.ones(k) / k, mode="same")
    ipeak = int(np.argmax(env))
    epk = env[ipeak]

    def decay_time(db):
        thr = epk * 10 ** (db / 20.0)
        after = np.where(env[ipeak:] >= thr)[0]
        return float(after[-1] / fs) if len(after) else 0.0

    sel = 10 * math.log10(np.sum(p ** 2) / fs / P_REF ** 2 + 1e-300)
    return {
        "peak_spl_db": 20 * math.log10(peak / P_REF),
        "sel_db": sel,
        "laf_max_dba": laf_max(p, fs),
        "centroid_hz": centroid,
        "peak_hz": float(fs_[ipk]),
        "band10_lo_hz": float(fs_[above[0]]),
        "band10_hi_hz": float(fs_[above[-1]]),
        "t_minus20_ms": 1e3 * decay_time(-20.0),
        "t_minus40_ms": 1e3 * decay_time(-40.0),
    }


def a_weighting(p, fs=FS):
    """IEC 61672 A-weighting (bilinear transform of the analog prototype)."""
    f1, f2, f3, f4 = 20.598997, 107.65265, 737.86223, 12194.217
    nums = [(2 * np.pi * f4) ** 2 * 10 ** (1.9997 / 20.0), 0, 0, 0, 0]
    dens = np.polymul([1, 4 * np.pi * f4, (2 * np.pi * f4) ** 2], [1, 4 * np.pi * f1, (2 * np.pi * f1) ** 2])
    dens = np.polymul(np.polymul(dens, [1, 2 * np.pi * f3]), [1, 2 * np.pi * f2])
    b, a = signal.bilinear(nums, dens, fs)
    return signal.lfilter(b, a, p, axis=0)


def laf_max(p, fs=FS, tau=0.125):
    """Maximum A-weighted, Fast (125 ms) time-weighted level [dB(A)] of a pressure signal [Pa] (mean over channels
    of the squared signal). A click's LAFmax is what a sound level meter shows; its peak is ~28 dB higher."""
    p = np.asarray(p, dtype=float)
    pa = a_weighting(np.concatenate([p, np.zeros((int(0.5 * fs),) + p.shape[1:])]), fs)
    sq = pa ** 2 if pa.ndim == 1 else np.mean(pa ** 2, axis=1)
    alpha = 1.0 - math.exp(-1.0 / (fs * tau))
    y = signal.lfilter([alpha], [1.0, -(1.0 - alpha)], sq)
    return 10 * math.log10(max(float(y.max()), 1e-300) / P_REF ** 2)


def _k_weighting(x, fs):
    """ITU-R BS.1770-4 K-weighting (pre-filter shelf + RLB high-pass); exact published coefficients at 48 kHz,
    re-derived by the bilinear transform for other rates."""
    if fs == 48000:
        b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
        b2, a2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]
    else:
        f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
        K = math.tan(math.pi * f0 / fs)
        Vh, Vb = 10 ** (G / 20.0), (10 ** (G / 20.0)) ** 0.4996667741545416
        a0 = 1.0 + K / Q + K * K
        b1 = [(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
        a1 = [1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
        f0, Q = 38.13547087602444, 0.5003270373238773
        K = math.tan(math.pi * f0 / fs)
        b2 = [1.0, -2.0, 1.0]
        a2 = [1.0, 2.0 * (K * K - 1.0) / (1.0 + K / Q + K * K), (1.0 - K / Q + K * K) / (1.0 + K / Q + K * K)]
    return signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)


def loudness_lufs(x, fs=FS):
    """Integrated loudness [LUFS] per ITU-R BS.1770-4 (400 ms blocks, 75 % overlap, -70 LUFS absolute and -10 LU
    relative gates). x: digital signal (n,) or (n, channels), full scale = 1; channel weights 1 (L/R/mono)."""
    x = np.asarray(x, dtype=float)
    if x.ndim == 1:
        x = x[:, None]
    y = _k_weighting(x, fs)
    blk, hop = int(0.4 * fs), int(0.1 * fs)
    if len(y) < blk:
        return -math.inf
    z = np.array([np.sum(np.mean(y[i:i + blk] ** 2, axis=0)) for i in range(0, len(y) - blk + 1, hop)])
    lk = -0.691 + 10 * np.log10(z + 1e-300)
    z1 = z[lk > -70.0]
    if not len(z1):
        return -math.inf
    rel = -0.691 + 10 * math.log10(np.mean(z1)) - 10.0
    z2 = z[(lk > -70.0) & (lk > rel)]
    return -0.691 + 10 * math.log10(np.mean(z2))


def true_peak_dbtp(x):
    """True peak [dBTP] with 4x polyphase oversampling (BS.1770-4 Annex 2 method)."""
    x = np.asarray(x, dtype=float)
    if x.ndim == 1:
        x = x[:, None]
    up = signal.resample_poly(x, 4, 1, axis=0)
    return 20 * math.log10(max(float(np.max(np.abs(up))), 1e-300))


def presentation_gain_db(peak_spl_db, mode):
    """Static per-impact gain [dB] of a dynamic-range mode (audio.md 4.2): 0 below the knee, above it the presented
    peak grows 1 dB per `ratio` dB of physical peak. Input: the impact's physical peak at the listener [dB SPL]."""
    m = PRESENTATION_MODES[mode]
    over = peak_spl_db - m["knee"]
    return 0.0 if over <= 0.0 else -over * (1.0 - 1.0 / m["ratio"])


def presentation_gain_envelope(p_phys, mode, fs=FS):
    """Per-sample gain [linear] of the table stem for a dynamic-range mode (audio.md 4.2): peak envelope of the
    physical stem at the listener (max over channels) with 3 ms lookahead and 60 ms release, mapped through the static
    curve presentation_gain_db, smoothed by a 1 ms one-pole on the dB gain. Computed at plan time in the engine."""
    from scipy import ndimage
    a = np.abs(p_phys) if np.ndim(p_phys) == 1 else np.max(np.abs(p_phys), axis=1)
    la = int(round(PRESENTATION_LOOKAHEAD_S * fs))
    la += la % 2
    env = ndimage.maximum_filter1d(a, size=la + 1, origin=-(la // 2), mode="constant")   # max over [k, k + la]
    rel = math.exp(-1.0 / (PRESENTATION_RELEASE_S * fs))
    e = np.empty_like(env)
    acc = 0.0
    for k in range(len(env)):
        acc = max(env[k], acc * rel)
        e[k] = acc
    m = PRESENTATION_MODES[mode]
    over = 20 * np.log10(np.maximum(e, 1e-12) / P_REF) - m["knee"]
    gdb = np.where(over > 0.0, -over * (1.0 - 1.0 / m["ratio"]), 0.0)
    sm = 1.0 - math.exp(-1.0 / (PRESENTATION_SMOOTH_S * fs))
    gdb = signal.lfilter([sm], [1.0, -(1.0 - sm)], gdb)
    return 10 ** (gdb / 20.0)


def mode_full_scale_pa(mode):
    return P_REF * 10 ** (PRESENTATION_MODES[mode]["l_fs"] / 20.0)


def pink_bed(seconds, laeq_db, seed=5, channels=2):
    """Stand-in for the bar ambience in loudness checks only (not a sound design): pink noise scaled to LAeq."""
    rng = np.random.default_rng(seed)
    n = int(seconds * FS)
    X = np.fft.rfft(rng.standard_normal((n, channels)), axis=0)
    f = np.fft.rfftfreq(n, 1.0 / FS)
    X[1:] /= np.sqrt(f[1:, None])
    X[0] = 0.0
    x = np.fft.irfft(X, n, axis=0)
    xa = a_weighting(x)
    laeq = 10 * math.log10(np.mean(xa ** 2) / P_REF ** 2)
    return x * 10 ** ((laeq_db - laeq) / 20.0)


def radiated_energy_free_field(imp, r=5.0, n_dirs=48, f_max=None):
    """Acoustic energy radiated by the balls of a ball-ball impact into free space (no cloth, no structure) [J].
    The collision line is the symmetry axis, so E = 2 pi r^2 / (rho0 c) int d(cos th) int p^2 dt."""
    duration = 0.05
    fs_calc = FS * 4                                   # include ultrasonic energy of the modes
    n_calc = int(round(duration * fs_calc))
    n_hi = n_calc * (FS_HI // fs_calc)
    x = np.zeros(n_hi)
    x[:len(imp.pulse.F)] = imp.pulse.F
    Fw = np.fft.rfft(x)[: n_calc // 2 + 1] / FS_HI
    f = np.fft.rfftfreq(n_calc, 1.0 / fs_calc)
    if f_max:
        Fw = Fw * (f <= f_max)
    mu, wts = np.polynomial.legendre.leggauss(n_dirs)
    total = 0.0
    contact = 0.5 * (imp.bodies[0].center + imp.bodies[1].center)
    for c, wgt in zip(mu, wts):
        s = math.sqrt(max(0.0, 1 - c * c))
        lis = contact + r * np.array([c, s, 0.0])
        G = np.zeros(len(f), dtype=complex)
        for b in imp.bodies:
            G += body_transfer(f, b, lis, image=False)
        P = Fw * G
        e_t = 2.0 * np.sum(np.abs(P[1:]) ** 2) * (f[1] - f[0])     # Parseval, one-sided
        total += wgt * e_t
    return 2.0 * math.pi * r * r / (RHO0 * C0) * total


# ---------------------------------------------------------------------------------------------------------------------
# Runtime approximation (what the Unreal DSP will do) and its error against the exact render
# ---------------------------------------------------------------------------------------------------------------------
def far_order_kernels(ball, taps=512, pre=32, orders=(0, 1, 2, 3)):
    """Per-ball far-field FIR kernels (48 kHz) per Legendre order n, per unit contact FORCE [Pa at 1 m per N],
    delay-free (32-sample pre-delay for the zero-phase band limit). Order 1 = rigid-body translation + order-1 modes;
    orders 0, 2, 3
    = Lamb modes incl. their quasi-static (stiffness-line) part, which is what a side-on listener hears.
      H_far,n(w) = -rho0 c^2 i^(n+1) e^{-ika} / (w^2 h_n'(ka))                 (lim r e^{ik(r-a)} H_A^n)
      p(t) = sum_n P_n(cos th) / r (K_n * F)(t - (r - a)/c)  (+ order-1 near-field term (c/r) int p1 dt)"""
    n_fft = 16384
    f = np.fft.rfftfreq(n_fft, 1.0 / FS)
    w = 2 * np.pi * f
    nz = f > 0
    zeta = ball.mat.loss / 2.0
    out = {}
    for n in orders:
        Hf = np.zeros(len(f), dtype=complex)
        k = w[nz] / C0
        Hf[nz] = -RHO0 * C0 ** 2 * (1j ** (n + 1)) * np.exp(-1j * k * ball.R) / (w[nz] ** 2 * _h2p(n, k * ball.R))
        acc = np.zeros(len(f), dtype=complex)
        if n == 1:
            acc += -1.0 / ball.m
        for md in sphere_modes(ball):
            if md.n == n:
                wn = 2 * np.pi * md.f
                acc += w ** 2 / (md.mass * (wn ** 2 - w ** 2 + 2j * zeta * wn * w))
        H = Hf * acc * band_taper(f) * np.exp(-2j * np.pi * f * pre / FS)
        # sampled impulse response x sample period = irfft(H) (the FS factors cancel)
        out[n] = np.fft.irfft(H, n_fft)[:taps]
    return out


_KERNEL_CACHE = {}
_DEC_FIR = None


def ball_kernels(ball):
    if ball.key not in _KERNEL_CACHE:
        _KERNEL_CACHE[ball.key] = far_order_kernels(ball)
    return _KERNEL_CACHE[ball.key]


def decimation_fir():
    """4x decimation FIR of the runtime pulse (129 taps, Kaiser beta 8, cutoff 24 kHz at 192 kHz, DC gain 1).
    Content folding into 20-24 kHz is removed afterwards by the kernels' band limit."""
    global _DEC_FIR
    if _DEC_FIR is None:
        _DEC_FIR = signal.firwin(DEC_TAPS, DEC_CUTOFF_HZ, window=("kaiser", DEC_KAISER_BETA), fs=FS * DEC_FACTOR)
    return _DEC_FIR


def cloth_iir():
    """First-order IIR of the cloth image (bilinear transform of 0.9 / (1 + s / (2 pi 8 kHz))): (b[2], a[2])."""
    return signal.bilinear([CLOTH_REFLECTION], [1.0 / (2 * np.pi * CLOTH_CORNER_HZ), 1.0], FS)


def nf_leak():
    return math.exp(-2.0 * math.pi * NF_LEAK_HZ / FS)


RUNTIME_LATENCY = DEC_TAPS // 2 // DEC_FACTOR + KERNEL_PRE      # 16 + 32 = 48 samples
SIN15_AREA = float(special.gamma(1.25) / (math.sqrt(math.pi) * special.gamma(1.75)))   # int_0^1 sin^1.5(pi u) du


def runtime_force(v, m_star, K, e, frac, sine=False, shape_n=129):
    """Contact force at 48 kHz exactly as the C++ DSP computes it (audio.md 3.6 step 1): the self-similar shape table
    (hertz_shape_e095.json, 129 points, linear interpolation; or sin^1.5 with the same T and impulse) is evaluated at
    4 x FS on t = j / (4 FS) - frac / FS and decimated with decimation_fir(). G[k] = F_bl((k - 16 - frac) / FS)."""
    sh = hertz_shape(e, n=shape_n)
    t0 = (m_star ** 2 / (K ** 2 * v)) ** 0.2
    d0 = (m_star * v * v / K) ** 0.4
    T = sh["tau"] * t0
    fmax = sh["phi"] * K * d0 ** 1.5
    n48 = int(math.ceil(T * FS + frac)) + 1
    J = DEC_FACTOR * n48
    u = (np.arange(J) / (FS * DEC_FACTOR) - frac / FS) / T
    inside = (u >= 0.0) & (u <= 1.0)
    F4 = np.zeros(J)
    if sine:
        impulse = (1.0 + e) * m_star * v
        F4[inside] = impulse / (T * SIN15_AREA) * np.sin(np.pi * u[inside]) ** 1.5
    else:
        F4[inside] = fmax * np.interp(u[inside], np.linspace(0.0, 1.0, len(sh["shape"])), sh["shape"])
    G = np.convolve(F4, decimation_fir())[::DEC_FACTOR]           # G[k] = sum_m h[m] F4[4k - m]
    return G, T, fmax


def runtime_render(v_n, m_star, K, e, bodies, listener, t_start, n_out, sine=False, image=True):
    """Reference of the C++ impact renderer (audio.md 3.6 steps 1-5), time domain, 48 kHz. Pressure at `listener`
    [Pa] of one impact whose contact starts at t_start [s] after output sample 0. Per ball and path (direct, cloth
    image): S = (t_start + (r - a)/c) FS, i = floor(S), frac = S - i; G = runtime_force(frac); y_n = K_n * G;
    s = sum_n P_n(cos th)/r y_n + (c/(r FS)) leaky_sum(P_1/r y_1); image path through cloth_iir(); s is added at
    output index i - 48 (16 decimation + 32 kernel pre-delay)."""
    out = np.zeros(n_out)
    bc, ac = cloth_iir()
    leak = nf_leak()
    mirror = np.array([1.0, 1.0, -1.0])
    listener = np.asarray(listener, dtype=float)
    for body in bodies:
        kern = ball_kernels(body.ball)
        paths = [(np.asarray(body.center, float), np.asarray(body.axis, float), False)]
        if image and body.center[2] > 0.0:
            paths.append((body.center * mirror, body.axis * mirror, True))
        for center, axis, cloth in paths:
            d = listener - center
            r = float(np.linalg.norm(d))
            c = float(np.dot(d / r, axis))
            S = (t_start + (r - body.ball.R) / C0) * FS
            i = int(math.floor(S))
            G, _, _ = runtime_force(v_n, m_star, K, e, S - i, sine)
            s = np.zeros(len(G) + len(kern[1]) - 1)
            for n, h in kern.items():
                y = np.convolve(G, h) * (special.eval_legendre(n, c) / r)
                s += y
                if n == 1:
                    s += (C0 / (r * FS)) * signal.lfilter([1.0], [1.0, -leak], y)
            if cloth:
                s = signal.lfilter(bc, ac, s)
            a0 = i - RUNTIME_LATENCY
            lo, hi = max(a0, 0), min(a0 + len(s), n_out)
            if hi > lo:
                out[lo:hi] += s[lo - a0:hi - a0]
    return out


def ball_ball_runtime_args(v, b1, b2, e=E_BALL):
    imp = ball_ball_impact(v, b1, b2, e)
    m_star = b1.m * b2.m / (b1.m + b2.m)
    return imp, v * float(imp.bodies[0].axis[0]), m_star, hertz_K(b1, b2)


def runtime_click(v, listener, b1=BALLS["std"], b2=BALLS["std"], e=E_BALL, duration=0.25, pre_s=0.002, sine=False):
    """Runtime model of a head-on click, aligned with render_single(ball_ball_impact(v, ...), listener, pre_s)."""
    imp, vn, m_star, K = ball_ball_runtime_args(v, b1, b2, e)
    return runtime_render(vn, m_star, K, e, imp.bodies, listener, pre_s, int(round((duration + pre_s) * FS)), sine)


GOLDEN_CASES = [
    dict(name="std_1ms_shooter", b1="std", b2="std", v=1.0, listener=[-1.00, 0.03, 0.30], t_start=0.00123456),
    dict(name="std_4ms_observer_sideon", b1="std", b2="std", v=4.0, listener=[0.00, 1.20, 0.85], t_start=0.0020417),
    dict(name="oversized_to_barob_2ms_45deg", b1="oversized_cb", b2="bar_ob", v=2.0,
         listener=[-0.84853, 0.84853, 0.30], t_start=0.0007),
]


def write_golden(path, n_out=1536):
    """Golden vectors for the C++ port of runtime_render (audio.md 14, AU-T11)."""
    cases = []
    for gc in GOLDEN_CASES:
        b1, b2 = BALLS[gc["b1"]], BALLS[gc["b2"]]
        imp, vn, m_star, K = ball_ball_runtime_args(gc["v"], b1, b2)
        p = runtime_render(vn, m_star, K, E_BALL, imp.bodies, np.array(gc["listener"]), gc["t_start"], n_out)
        G, T, fmax = runtime_force(vn, m_star, K, E_BALL, 0.25)
        cases.append({
            "name": gc["name"], "t_start_s": gc["t_start"], "listener_m": gc["listener"], "n_out": n_out,
            "v_n": vn, "m_star": m_star, "K": K, "e": E_BALL, "contact_time_s": T, "fmax_n": fmax,
            "bodies": [{"ball": b.ball.key, "R": b.ball.R, "m": b.ball.m, "center_m": [float(x) for x in b.center],
                        "axis": [float(x) for x in b.axis]} for b in imp.bodies],
            "force_frac_0.25": [float(x) for x in G],
            "p_pa": [float(x) for x in p]})
    b, a = cloth_iir()
    doc = {"description": "Golden vectors of runtime_render (click_synth.py), the reference of the C++ impact renderer "
                          "(audio.md 3.6). Frame: cloth plane z = 0, metres; the listener pressure p_pa[k] is at "
                          "output sample k (48 kHz) for a contact starting at t_start_s. The C++ port must reproduce "
                          "p_pa with residual energy <= -100 dB (AU-T11). force_frac_0.25 = runtime_force(frac=0.25) "
                          "for debugging step 1. Kernels: ball_kernels_<ball>_48k.json; shape: hertz_shape_e095.json.",
           "fs": FS, "c0": C0, "rho0": RHO0,
           "decimation": {"factor": DEC_FACTOR, "taps": [float(x) for x in decimation_fir()]},
           "latency_samples": RUNTIME_LATENCY, "kernel_pre_delay": KERNEL_PRE,
           "cloth_iir": {"b": [float(x) for x in b], "a": [float(x) for x in a]},
           "near_field_leak": nf_leak(), "cases": cases}
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh)


def write_kernels(ball, path):
    kern = ball_kernels(ball)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({"description": f"Far-field radiation FIR kernels of the {ball.label} (E {ball.mat.E / 1e9:.2f} GPa, "
                                  f"nu {ball.mat.nu}, eta {ball.mat.loss}) at 48 kHz per Legendre order n, per unit "
                                  "contact force [Pa at 1 m per N]; 32-sample pre-delay. p(t) = sum_n P_n(cos th)/r "
                                  "(K_n * F)(t - (r-a)/c - 32/fs) + (c/r) int p_1 dt; th = angle between the "
                                  "ball->listener direction and the ball->contact axis; F = contact force at 48 kHz. "
                                  "The C++ DSP computes these at start-up for the device rate (far_order_kernels); "
                                  "this file checks the port.",
                   "fs": FS, "pre_delay_samples": KERNEL_PRE, "ball": ball.key, "radius_m": ball.R, "mass_kg": ball.m,
                   "orders": {str(nn): [float(x) for x in h] for nn, h in kern.items()}}, fh)


def band_error_db(p_ref, p_test, fs=FS, within_db=20.0):
    """(max, energy-weighted rms) difference of the 1/6-octave spectra over 100 Hz-16 kHz, in bands where the reference
    is within `within_db` of its maximum (spectral nulls are excluded: they move with tiny pulse-shape changes)."""
    n = 1 << int(math.ceil(math.log2(len(p_ref) * 2)))
    A = np.abs(np.fft.rfft(p_ref, n)) ** 2
    B = np.abs(np.fft.rfft(p_test, n)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / fs)
    band = (f >= 100) & (f <= 16000)
    sa, sb = smooth_octave(f[band], A[band]), smooth_octave(f[band], B[band])
    da, db_ = 10 * np.log10(sa + 1e-300), 10 * np.log10(sb + 1e-300)
    m = da >= da.max() - within_db
    d = np.abs(da[m] - db_[m])
    w = sa[m] / np.sum(sa[m])
    return float(np.max(d)), float(math.sqrt(np.sum(w * d * d)))


# ---------------------------------------------------------------------------------------------------------------------
# rbsim event log -> break render
# ---------------------------------------------------------------------------------------------------------------------
class Tracks:
    def __init__(self, balls_json):
        self.balls = {}
        for b in balls_json:
            s = np.array(b["samples"], dtype=float)
            self.balls[b["id"]] = dict(t=s[:, 0], pos=s[:, 1:4], state=s[:, 8].astype(int), R=b["radius"], m=b["mass"])

    def pos(self, bid, t):
        b = self.balls[bid]
        return np.array([np.interp(t, b["t"], b["pos"][:, k]) for k in range(3)])

    def speed_series(self, bid, t_grid):
        b = self.balls[bid]
        tt, pp = b["t"], b["pos"]
        dt = np.diff(tt)
        v = np.zeros(len(tt))
        ok = dt > 1e-9
        vv = np.linalg.norm(np.diff(pp, axis=0), axis=1)
        seg_v = np.where(ok, vv / np.where(ok, dt, 1.0), 0.0)
        v[:-1] = seg_v
        idx = np.clip(np.searchsorted(tt, t_grid, side="right") - 1, 0, len(tt) - 1)
        return v[idx], b["state"][idx]


def ball_for(track):
    if abs(track["R"] - R_STD) < 1e-6 and abs(track["m"] - M_STD) < 1e-4:
        return BALLS["std"]
    return BallSpec(f"sim_{track['R']:.5f}_{track['m']:.4f}", "rbsim ball", track["R"], track["m"], PHENOLIC)


def events_to_impacts(doc, rail_modes=RAIL_PRO, rail_fc=RAIL_PRO_FC, coin_op=False, table_length=2.54):
    tr = Tracks(doc["balls"])
    ev = doc["events"]
    impacts = []
    tip_begin = {}
    tip_dur = {}
    for e in ev:
        if e["type"] == "TipContactBegin":
            tip_begin[e["feature"]] = e["t"]
        elif e["type"] == "TipContactEnd" and e["feature"] in tip_begin:
            tip_dur[e["feature"]] = e["t"] - tip_begin[e["feature"]]
    skipped = 0
    gullies = []
    for e in ev:
        typ, t, a, b = e["type"], e["t"], e["a"], e["b"]
        vn = abs(e["vn"])
        nrm = np.array(e["normal"], dtype=float)
        if typ == "CueStrike":
            ball = ball_for(tr.balls[a])
            T = tip_dur.get(e["feature"], 1.0e-3)
            pul = half_sine_pulse(e["jn"], T)
            c = tr.pos(a, t)
            axis = -nrm / np.linalg.norm(nrm)
            cue = Piston("cue", CUE_MODES, c + 0.35 * axis + np.array([0, 0, 0.08]), baffled=False)
            impacts.append(Impact(t, pul, [Body(ball, c, axis)], [cue], 0.3, "cue strike"))
        elif typ == "BallBall":
            if (e["flags"] & 1) or vn < 0.003:
                skipped += 1
                continue
            ba, bb = ball_for(tr.balls[a]), ball_for(tr.balls[b])
            m_star = ba.m * bb.m / (ba.m + bb.m)
            pul = hertz_pulse(vn, m_star, hertz_K(ba, bb), E_BALL)
            ca, cb = tr.pos(a, t), tr.pos(b, t)
            n = nrm / np.linalg.norm(nrm)
            impacts.append(Impact(t, pul, [Body(ba, ca, n), Body(bb, cb, -n)], [], 0.2, f"ball-ball {a}-{b}"))
        elif typ in ("BallCushion", "BallJaw", "BallRailTop"):
            if vn < 0.003:
                skipped += 1
                continue
            ball = ball_for(tr.balls[a])
            kind = {"BallCushion": "cushion", "BallJaw": "facing", "BallRailTop": "railtop"}[typ]
            e_c = cushion_restitution(vn) if kind != "railtop" else 0.5
            K = stiffness_for_contact_time(CONTACTS[kind]["T1"], ball.m)
            pul = hertz_pulse(vn, ball.m, K, e_c)
            c = tr.pos(a, t)
            axis = -nrm / np.linalg.norm(nrm)
            contact = c + ball.R * axis
            impacts.append(Impact(t, pul, [Body(ball, c, axis)], [Piston("rail", rail_modes, contact, fc=rail_fc)], 0.8,
                                  f"{kind} {a}"))
        elif typ == "BallSlate":
            if vn < 0.01:
                skipped += 1
                continue
            ball = ball_for(tr.balls[a])
            K = stiffness_for_contact_time(CONTACTS["slate"]["T1"], ball.m)
            pul = hertz_pulse(vn, ball.m, K, E_SLATE)
            c = tr.pos(a, t)
            c[2] = ball.R
            axis = np.array([0.0, 0.0, -1.0])
            bed = Piston("bed", BED_MODES, c - [0, 0, ball.R], fc=BED_FC)
            impacts.append(Impact(t, pul, [Body(ball, c, axis)], [bed], 0.6,
                                  f"slate {a}"))
        elif typ in ("BallLiner", "BallPocketRim"):
            ball = ball_for(tr.balls[a])
            K = stiffness_for_contact_time(CONTACTS["liner"]["T1"] * (0.5 if typ == "BallPocketRim" else 1.0), ball.m)
            pul = hertz_pulse(max(vn, 0.05), ball.m, K, 0.3)
            c = tr.pos(a, t)
            axis = -nrm / np.linalg.norm(nrm) if np.linalg.norm(nrm) > 0 else np.array([0.0, 0.0, -1.0])
            impacts.append(Impact(t, pul, [Body(ball, c, axis)], [Piston("pocket", POCKET_MODES, c, fc=POCKET_FC)], 0.5,
                                  f"pocket {a}"))
        elif typ == "BallPocketed":
            ball = ball_for(tr.balls[a])
            v_drop = math.sqrt(2 * 9.81 * 0.08)       # ESTIMATE last fall into the pocket bottom
            pul = half_sine_pulse(ball.m * 1.3 * v_drop, 3.0e-3)
            c = tr.pos(a, t)
            impacts.append(Impact(t, pul, [Body(ball, c, np.array([0.0, 0.0, -1.0]), modes=False)],
                                  [Piston("pocket", POCKET_MODES, c, fc=POCKET_FC)], 0.5, f"pocketed {a}"))
            if coin_op:
                # HF-70..72, venue-dive-bar 3.3: the ball runs down the gully to the trap row at the foot end
                # (1.5 s from the foot pockets ... 3 s from the head pockets, 0.6-1.0 m/s) and clicks into the balls
                # already there, muffled by the cabinet (low-pass 1.2 kHz)
                c_tray = np.array([0.5 * table_length + 0.05, 0.0, -0.30])
                t_run = 1.5 + 1.5 * min(1.0, abs(c[0] - c_tray[0]) / table_length)
                gullies.append((t, t + t_run, c.copy(), c_tray))
                tray_ball = BALLS["std"]
                pul2 = hertz_pulse(0.6, ball.m * tray_ball.m / (ball.m + tray_ball.m), hertz_K(ball, tray_ball), E_BALL)
                n_ = np.array([1.0, 0.0, 0.0])
                impacts.append(Impact(t + t_run, pul2, [Body(ball, c_tray - n_ * ball.R, n_, modes=False),
                                                         Body(tray_ball, c_tray + n_ * tray_ball.R, -n_, modes=False)],
                                      [Piston("cabinet", RAIL_BARBOX[:2], c_tray, fc=RAIL_BARBOX_FC)], 0.5,
                                      f"tray {a}", lowpass_hz=1200.0))
    return impacts, tr, skipped, gullies


def rolling_noise(tr, ears, n, seed=7):
    """ESTIMATE cloth rolling rumble: band-passed noise, 40 dB SPL rms at 1 m for 1 m/s rolling (scales with v);
    sliding adds a high hiss. Silent while airborne / in a pocket."""
    rng = np.random.default_rng(seed)
    t = np.arange(n) / FS
    out = np.zeros((n, len(ears)))
    b_lo, a_lo = signal.butter(2, [60, 700], btype="band", fs=FS)
    b_hi, a_hi = signal.butter(2, [1500, 6000], btype="band", fs=FS)
    rms_ref = P_REF * 10 ** (40.0 / 20.0)
    for bid, b in tr.balls.items():
        v, st = tr.speed_series(bid, t)
        if not np.any(v > 0.01):
            continue
        on_cloth = (st == 2) | (st == 3)
        amp = np.where(on_cloth, v, 0.0)
        amp = np.convolve(amp, np.ones(480) / 480, mode="same")
        hiss = np.convolve(np.where(st == 2, v, 0.0), np.ones(480) / 480, mode="same")
        lo = signal.lfilter(b_lo, a_lo, rng.standard_normal(n))
        hi = signal.lfilter(b_hi, a_hi, rng.standard_normal(n))
        lo *= rms_ref / (np.std(lo) + 1e-12)
        hi *= 0.35 * rms_ref / (np.std(hi) + 1e-12)
        src = lo * amp + hi * hiss
        stride = 480
        for k, ear in enumerate(ears):
            idx = np.arange(0, n, stride)
            pos = np.stack([np.interp(t[idx], b["t"], b["pos"][:, j]) for j in range(3)], axis=1)
            r = np.maximum(np.linalg.norm(pos - ear, axis=1), 0.1)
            g = np.interp(np.arange(n), idx, 1.0 / r)
            out[:, k] += src * g
    return out


def gully_noise(gullies, ears, n, seed=11):
    """ESTIMATE coin-op return: a ball rolling down the hard-rubber/plastic gully inside the cabinet. Band-passed noise
    (120-900 Hz) with small periodic bumps (seams every ~6 cm), 55 dB SPL rms at 1 m; the source moves from the pocket
    to the tray."""
    rng = np.random.default_rng(seed)
    out = np.zeros((n, len(ears)))
    bb, aa = signal.butter(2, [120, 900], btype="band", fs=FS)
    rms = P_REF * 10 ** (55.0 / 20.0)
    for t0, t1, p0, p1 in gullies:
        i0, i1 = int(t0 * FS) + int(0.02 * FS), min(int(t1 * FS), n)
        if i1 <= i0:
            continue
        m = i1 - i0
        x = signal.lfilter(bb, aa, rng.standard_normal(m))
        x *= rms / (np.std(x) + 1e-12)
        tt = np.arange(m) / FS
        bumps = 1.0 + 0.6 * np.maximum(0.0, np.sin(2 * np.pi * (0.8 / 0.06) * tt)) ** 8
        env = np.minimum(1.0, tt / 0.05) * np.minimum(1.0, (m / FS - tt) / 0.08)
        u = tt / max(tt[-1], 1e-9)
        for k, ear in enumerate(ears):
            pos = p0[None, :] * (1 - u[:, None]) + p1[None, :] * u[:, None]
            r = np.maximum(np.linalg.norm(pos - ear, axis=1), 0.2)
            out[i0:i1, k] += x * bumps * env / r
    return out


def room_ir(rt60=(0.8, 0.6, 0.4), length=1.2, drr_energy=0.8, seed=3, channels=2):
    """ESTIMATE stochastic dive-bar tail (stand-in for the convolution reverb): 3 bands (<250 Hz, mid, >4 kHz),
    RT60 per band, 4 ms pre-delay, total energy = drr_energy x the direct energy at 1 m."""
    rng = np.random.default_rng(seed)
    n = int(length * FS)
    t = np.arange(n) / FS
    ir = np.zeros((n, channels))
    edges = [(20, 250), (250, 4000), (4000, 20000)]
    for ch in range(channels):
        acc = np.zeros(n)
        for (lo, hi), rt in zip(edges, rt60):
            bb, aa = signal.butter(2, [lo, min(hi, FS / 2 - 100)], btype="band", fs=FS)
            nz = signal.lfilter(bb, aa, rng.standard_normal(n))
            acc += nz * np.exp(-6.9078 * t / rt)
        acc[: int(0.004 * FS)] = 0.0
        acc *= math.sqrt(drr_energy / (np.sum(acc ** 2) / FS)) / math.sqrt(FS)
        ir[:, ch] = acc
    return ir


# ---------------------------------------------------------------------------------------------------------------------
# WAV I/O and plotting
# ---------------------------------------------------------------------------------------------------------------------
def write_wav(path, p, bits=24, full_scale_pa=FULL_SCALE_PA):
    x = np.asarray(p, dtype=float) / full_scale_pa
    if x.ndim == 1:
        x = x[:, None]
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    if peak > 1.0:
        print(f"  warning: {os.path.basename(path)} clips (peak {20 * math.log10(peak):+.1f} dBFS); scaled down")
        x = x / peak * 0.999
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(bits // 8)
        w.setframerate(FS)
        if bits == 16:
            data = np.round(x * 32767).astype("<i2").tobytes()
        else:
            q = np.round(x * 8388607).astype("<i4")
            b = q.view(np.uint8).reshape(-1, 4)[:, :3]
            data = b.tobytes()
        w.writeframes(data)
    return 20 * math.log10(peak) if peak > 0 else -math.inf


def read_wav(path):
    fs, x = sio.wavfile.read(path)
    if x.dtype == np.int16:
        x = x / 32768.0
    elif x.dtype == np.int32:
        x = x / 2147483648.0
    elif x.dtype == np.uint8:
        x = (x - 128) / 128.0
    return fs, x.astype(float)


def svg_plot(path, series, title, xlabel, ylabel, xlim, ylim, xlog=True):
    W, H, L, R_, T, B = 820, 460, 70, 190, 40, 55
    pw, ph = W - L - R_, H - T - B

    def X(x):
        if xlog:
            return L + pw * (math.log10(x) - math.log10(xlim[0])) / (math.log10(xlim[1]) - math.log10(xlim[0]))
        return L + pw * (x - xlim[0]) / (xlim[1] - xlim[0])

    def Y(y):
        return T + ph * (1 - (y - ylim[0]) / (ylim[1] - ylim[0]))

    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="Helvetica,Arial,sans-serif" '
         f'font-size="12"><rect width="100%" height="100%" fill="#ffffff"/>',
         f'<text x="{L}" y="24" font-size="15" font-weight="bold">{title}</text>']
    if xlog:
        ticks = [t for t in (20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000) if xlim[0] <= t <= xlim[1]]
    else:
        ticks = list(np.linspace(xlim[0], xlim[1], 6))
    for t in ticks:
        s.append(f'<line x1="{X(t):.1f}" y1="{T}" x2="{X(t):.1f}" y2="{T + ph}" stroke="#e3e3e3"/>')
        lab = (f"{t / 1000:g}k" if t >= 1000 else f"{t:g}") if xlog else f"{t:g}"
        s.append(f'<text x="{X(t):.1f}" y="{T + ph + 16}" text-anchor="middle" fill="#444">{lab}</text>')
    ystep = 10 if (ylim[1] - ylim[0]) > 30 else 5
    y = math.ceil(ylim[0] / ystep) * ystep
    while y <= ylim[1]:
        s.append(f'<line x1="{L}" y1="{Y(y):.1f}" x2="{L + pw}" y2="{Y(y):.1f}" stroke="#e3e3e3"/>')
        s.append(f'<text x="{L - 6}" y="{Y(y) + 4:.1f}" text-anchor="end" fill="#444">{y:g}</text>')
        y += ystep
    s.append(f'<rect x="{L}" y="{T}" width="{pw}" height="{ph}" fill="none" stroke="#888"/>')
    s.append(f'<text x="{L + pw / 2}" y="{H - 12}" text-anchor="middle">{xlabel}</text>')
    yc = T + ph / 2
    s.append(f'<text x="18" y="{yc}" text-anchor="middle" transform="rotate(-90 18 {yc})">{ylabel}</text>')
    for i, (label, xs, ys, color) in enumerate(series):
        pts = [(X(x), Y(min(max(yv, ylim[0]), ylim[1]))) for x, yv in zip(xs, ys) if xlim[0] <= x <= xlim[1]]
        if pts:
            d = " ".join(f"{px:.1f},{py:.1f}" for px, py in pts)
            s.append(f'<polyline points="{d}" fill="none" stroke="{color}" stroke-width="1.8"/>')
        ly = T + 14 + 18 * i
        s.append(f'<line x1="{L + pw + 12}" y1="{ly - 4}" x2="{L + pw + 32}" y2="{ly - 4}" stroke="{color}" '
                 f'stroke-width="2.5"/>')
        s.append(f'<text x="{L + pw + 38}" y="{ly}">{label}</text>')
    s.append("</svg>")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(s))


PALETTE = ["#1b4f9c", "#2a8a5a", "#c77d0a", "#b3261e", "#6a3fa0", "#0f7c8c", "#555555", "#9c1b73"]


def spectrum_db(p, fs=FS, points=320):
    """1/6-octave smoothed energy spectral density [dB re 20 uPa^2 s/Hz] at `points` log-spaced frequencies."""
    n = 1 << int(math.ceil(math.log2(len(p) * 2)))
    P = np.fft.rfft(p, n) / fs
    f = np.fft.rfftfreq(n, 1.0 / fs)
    band = (f >= 20) & (f <= 22000)
    sm = smooth_octave(f[band], np.abs(P[band]) ** 2, frac=6)
    fx = np.geomspace(20.0, 22000.0, points)
    return fx, np.interp(np.log(fx), np.log(f[band]), 10 * np.log10(sm / (P_REF ** 2) + 1e-300))


# ---------------------------------------------------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------------------------------------------------
def cmd_selftest():
    ok = True

    def check(name, got, want, tol, unit=""):
        nonlocal ok
        good = abs(got - want) <= tol
        ok &= good
        print(f"  [{'ok' if good else 'FAIL'}] {name}: {got:.6g}{unit} (want {want:g} +- {tol:g})")

    print("selftest")
    a = alpha_for_restitution(0.95)
    check("alpha_T(0.95) (core table 0.036915)", a, ALPHA_T_CORE, 5e-4)
    for v, T_us in ((0.5, 378), (1, 329), (5, 238), (10, 207)):
        _, dur, _ = _integrate_hertz(v, M_STD / 2, K_CORE, 0.0)
        check(f"undamped Hertz contact time at {v} m/s (physics 3.9.3)", dur * 1e6, T_us, 1.5, " us")
    p1 = hertz_pulse(1.0, M_STD / 2, K_CORE, 0.95)
    check("peak force at 1 m/s, undamped 952 N (damped a bit lower)", p1.fmax, 940, 25, " N")
    check("self-similar shape, undamped tau = 3.2181", hertz_shape(1.0)["tau"], 3.2181, 2e-3)
    for v in (0.3, 3.0, 12.0):
        rp, ex = runtime_pulse(v, M_STD / 2, K_CORE, 0.95), hertz_pulse(v, M_STD / 2, K_CORE, 0.95)
        check(f"shape-table pulse vs integrated at {v} m/s: F_max ratio", rp.fmax / ex.fmax, 1.0, 2e-3)
        check(f"shape-table pulse vs integrated at {v} m/s: impulse ratio (J = 1.95 m* v)", rp.impulse / ex.impulse,
              1.0, 5e-3)
    check("restitution achieved at 3 m/s", hertz_pulse(3.0, M_STD / 2, K_CORE, 0.95).e, 0.95, 2e-3)
    m025 = Material("nu=0.25", 1e10, 0.25, 0.0)
    b025 = BallSpec("t025", "test", 0.03, 0.2, m025)
    x2 = lamb_roots(2, m025, b025.R, b025.rho, count=1)[0]
    check("Lamb n=2 fundamental, nu = 0.25 (k_T a, literature 2.640)", x2, 2.640, 0.01)
    f = np.array([1.0])
    h = radiation_accel(1, f, 0.1, R_STD)[0]
    check("order-1 radiation, incompressible limit rho0 a^3 / (2 r^2)", h.real / (RHO0 * R_STD ** 3 / (2 * 0.01)), 1.0,
          0.01)
    imp = ball_ball_impact(2.0)
    lis = np.array([-1.0, 0.0, 0.3])
    p = render_single(imp, lis, pre=0.004)
    r = float(np.linalg.norm(lis - imp.bodies[0].center))
    t_arr = 0.004 + (r - R_STD) / C0
    i_arr = int((t_arr - 0.0002) * FS)
    frac_early = float(np.sum(p[:i_arr] ** 2) / np.sum(p ** 2))
    check("causality: energy earlier than (r - a)/c - 0.2 ms", frac_early, 0.0, 1e-4)
    # E5 of the realism plan: two impacts 0.5 ms apart -> onsets 24 samples apart. 0.5 ms is an integer number of
    # samples at 48 kHz, so the fractional path is checked separately with non-integer start times (review 2026-09-28).
    def onset_separation(t1, t2, render):
        n = int(0.3 * FS)
        a, b = render(t1, n), render(t2, n)
        A, B = np.fft.rfft(a), np.fft.rfft(b)
        f = np.fft.rfftfreq(n, 1.0 / FS)
        m = (f > 500.0) & (f < 6000.0)
        ph = np.unwrap(np.angle(B[m] * np.conj(A[m])))
        return -np.polyfit(2 * np.pi * f[m], ph, 1)[0] * FS        # group delay of b vs a [samples]

    def exact_render(t, n):
        buf = np.zeros((n, 1))
        imp_t = ball_ball_impact(2.0)
        imp_t.time = t
        render_impact(imp_t, [lis], buf)
        return buf[:, 0]

    imp2, vn2, ms2, K2 = ball_ball_runtime_args(2.0, BALLS["std"], BALLS["std"])

    def rt_render(t, n):
        return runtime_render(vn2, ms2, K2, E_BALL, imp2.bodies, lis, t, n)

    for t1, t2 in ((0.010, 0.0105), (0.0100073, 0.0105177), (0.0100104, 0.0101146)):
        want = (t2 - t1) * FS
        check(f"E5 scheduling, exact render: onsets {t1 * 1e3:.4f} / {t2 * 1e3:.4f} ms",
              onset_separation(t1, t2, exact_render), want, 0.01, " samples")
        check(f"E5 scheduling, runtime render: onsets {t1 * 1e3:.4f} / {t2 * 1e3:.4f} ms",
              onset_separation(t1, t2, rt_render), want, 0.01, " samples")
    # runtime reference (C++ algorithm) against the exact model, on-axis, 1/6-octave bands (AU-T10)
    ex = render_single(ball_ball_impact(1.0), LISTENERS["shooter"])
    rt = runtime_click(1.0, LISTENERS["shooter"])
    emax, erms = band_error_db(ex, rt)
    check("runtime render vs exact, 1 m/s on-axis, 1/6-oct rms error", erms, 0.0, 0.05, " dB")
    # presentation curve (audio.md 4.2): the loudest summed break peak at the breaker's ears in the renders (127 dB
    # SPL) stays below -1 dBFS in every mode; above that the master true-peak limiter takes over
    for mode in PRESENTATION_MODES:
        pk = 127.0 + presentation_gain_db(127.0, mode) - PRESENTATION_MODES[mode]["l_fs"]
        check(f"presentation {mode}: 127 dB SPL break peak -> {pk:+.1f} dBFS (<= -1)", max(pk, -1.0), -1.0, 1e-9,
              " dBFS")
    # loudness meter: a 997 Hz sine at -20 dBFS peak in one channel reads -23.0 LUFS (BS.1770: 0 dBFS sine = -3.01)
    tt = np.arange(int(5 * FS)) / FS
    check("BS.1770 loudness of a -20 dBFS 997 Hz sine", loudness_lufs(0.1 * np.sin(2 * np.pi * 997 * tt)), -23.01,
          0.05, " LUFS")
    print("selftest", "PASSED" if ok else "FAILED")
    return ok


def cmd_standard(args):
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(REF_DIR, exist_ok=True)
    results = {"meta": {"fs": FS, "full_scale_pa": FULL_SCALE_PA, "c0": C0, "rho0": RHO0,
                        "E_phenolic_pa": E_PHENOLIC, "nu_phenolic": NU_PHENOLIC,
                        "cloth_reflection": CLOTH_REFLECTION, "cloth_corner_hz": CLOTH_CORNER_HZ},
               "balls": {}, "ball_ball": [], "cushion": [], "variants": [], "runtime_model": []}
    print("sphere modes (Lamb, free sphere):")
    for key in ("std", "bar_poly", "oversized_cb"):
        b = BALLS[key]
        modes = sphere_modes(b)
        results["balls"][key] = {"label": b.label, "R": b.R, "m": b.m, "rho": b.rho, "E": b.mat.E, "nu": b.mat.nu,
                                 "loss": b.mat.loss,
                                 "modes": [{"n": m.n, "root": m.root, "k_T_a": m.x, "f_hz": m.f, "modal_mass_kg": m.mass,
                                            "t60_ms": 1e3 * 2.2 / (b.mat.loss * m.f)} for m in modes]}
        print(f"  {b.label}: rho {b.rho:.0f} kg/m^3, E {b.mat.E / 1e9:.2f} GPa -> " +
              ", ".join(f"n{m.n}.{m.root} {m.f / 1000:.1f} kHz" for m in modes[:5]))

    speeds = [0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 12.0]
    spec_series = []
    print("ball-ball (std pair, head-on stun):")
    print("  v m/s | T_c us | F_max N | e | listener | peak dB SPL | SEL dB | centroid Hz | peak Hz | -10 dB band Hz | "
          "t-20 ms | t-40 ms | E_rad/KE")
    for i, v in enumerate(speeds):
        imp = ball_ball_impact(v)
        e_rad = radiated_energy_free_field(imp)
        ke = 0.5 * BALLS["std"].m * v * v
        for lname in ("shooter", "observer"):
            p = render_single(imp, LISTENERS[lname])
            an = analyze(p)
            row = {"v": v, "listener": lname, "contact_us": imp.pulse.duration * 1e6, "fmax_n": imp.pulse.fmax,
                   "e": imp.pulse.e, "e_rad_j": e_rad, "e_rad_over_ke": e_rad / ke, **an}
            results["ball_ball"].append(row)
            print(f"  {v:5.2f} | {row['contact_us']:6.0f} | {row['fmax_n']:7.0f} | {row['e']:.3f} | {lname:8s} | "
                  f"{an['peak_spl_db']:6.1f} | {an['sel_db']:5.1f} | {an['centroid_hz']:7.0f} | {an['peak_hz']:6.0f} | "
                  f"{an['band10_lo_hz']:5.0f}-{an['band10_hi_hz']:5.0f} | {an['t_minus20_ms']:5.2f} | "
                  f"{an['t_minus40_ms']:5.2f} | {e_rad / ke:.2e}")
            write_wav(os.path.join(OUT_DIR, f"ballball_{v:g}ms_{lname}.wav"), p)
            if lname == "shooter":
                fx, dbs = spectrum_db(p[: int(0.06 * FS)])
                spec_series.append((f"{v:g} m/s", fx, dbs, PALETTE[i % len(PALETTE)]))
                if v in (1.0, 4.0):
                    write_wav(os.path.join(REF_DIR, f"ref_ballball_{v:g}ms_shooter.wav"), p)
    svg_plot(os.path.join(REF_DIR, "spectra_ballball.svg"), spec_series,
             "Ball-ball click, std phenolic pair, listener down on the shot 1 m behind (1/6-oct ESD)",
             "frequency [Hz]", "ESD [dB re 20 uPa^2 s/Hz]", (50, 22000), (-150, -60))

    print("variants at 2 m/s (shooter):")
    for b1k, b2k, lab in (("oversized_cb", "bar_ob", "oversized bar cue ball -> worn OB"),
                          ("magnetic_cb", "bar_ob", "magnetic cue ball -> worn OB"),
                          ("bar_poly", "bar_poly", "polyester bar set"),):
        imp = ball_ball_impact(2.0, BALLS[b1k], BALLS[b2k])
        p = render_single(imp, LISTENERS["shooter"])
        an = analyze(p)
        results["variants"].append({"pair": lab, "v": 2.0, "contact_us": imp.pulse.duration * 1e6,
                                    "fmax_n": imp.pulse.fmax, **an})
        print(f"  {lab:38s} T_c {imp.pulse.duration * 1e6:4.0f} us  peak {an['peak_spl_db']:5.1f} dB  "
              f"centroid {an['centroid_hz']:5.0f} Hz  t-40 {an['t_minus40_ms']:5.2f} ms")
        write_wav(os.path.join(OUT_DIR, f"variant_{b1k}_{b2k}_2ms.wav"), p)

    print("cushion (std ball, bar-box rail, head-on):")
    cspec = []
    for i, v in enumerate([0.5, 1.0, 2.0, 4.0, 8.0]):
        imp = cushion_impact(v)
        p = render_single(imp, LISTENERS["cushion_front"])
        # split: ball-only contribution vs structure
        imp_ball = Impact(0.0, imp.pulse, imp.bodies, [], imp.duration)
        pb = render_single(imp_ball, LISTENERS["cushion_front"])
        an = analyze(p, window_ms=300.0)
        row = {"v": v, "contact_ms": imp.pulse.duration * 1e3, "fmax_n": imp.pulse.fmax, "e": imp.pulse.e,
               "ball_share_db": 10 * math.log10(np.sum(pb ** 2) / np.sum(p ** 2)), **an}
        results["cushion"].append(row)
        print(f"  {v:4.1f} m/s  T_c {row['contact_ms']:.2f} ms  F_max {row['fmax_n']:6.0f} N  e {row['e']:.3f}  peak "
              f"{an['peak_spl_db']:5.1f} dB  SEL {an['sel_db']:5.1f}  centroid {an['centroid_hz']:5.0f} Hz  peak "
              f"{an['peak_hz']:5.0f} Hz  t-20 {an['t_minus20_ms']:6.1f} ms  t-40 {an['t_minus40_ms']:6.1f} ms  "
              f"ball share {row['ball_share_db']:5.1f} dB")
        write_wav(os.path.join(OUT_DIR, f"cushion_{v:g}ms.wav"), p)
        fx, dbs = spectrum_db(p[: int(0.4 * FS)])
        cspec.append((f"{v:g} m/s", fx, dbs, PALETTE[i % len(PALETTE)]))
        if v == 2.0:
            write_wav(os.path.join(REF_DIR, "ref_cushion_2ms_barbox.wav"), p)
    svg_plot(os.path.join(REF_DIR, "spectra_cushion.svg"), cspec,
             "Cushion hit, bar-box rail (ESTIMATE modes), listener 1 m in front (1/6-oct ESD)",
             "frequency [Hz]", "ESD [dB re 20 uPa^2 s/Hz]", (30, 22000), (-150, -60))

    print("runtime reference (C++ algorithm: shape table at 4x + decimation FIR, 512-tap per-order kernels, "
          "near field, cloth image) vs exact:")
    for v in (0.25, 1.0, 4.0, 12.0):
        imp = ball_ball_impact(v)
        for lname in ("shooter", "observer"):
            ex = render_single(imp, LISTENERS[lname])
            row = {"v": v, "listener": lname}
            for tag, sine in (("table", False), ("sin15", True)):
                fa = runtime_click(v, LISTENERS[lname], sine=sine)
                L = min(len(ex), len(fa))
                emax, erms = band_error_db(ex[:L], fa[:L])
                pk = 20 * math.log10(np.max(np.abs(fa[:L])) / np.max(np.abs(ex[:L])))
                res = 10 * math.log10(np.sum((ex[:L] - fa[:L]) ** 2) / np.sum(ex[:L] ** 2))
                row.update({f"{tag}_max_err_db": emax, f"{tag}_rms_err_db": erms, f"{tag}_peak_diff_db": pk,
                            f"{tag}_waveform_residual_db": res})
            results["runtime_model"].append(row)
            print(f"  {v:5.2f} m/s {lname:8s}: shape table max {row['table_max_err_db']:4.2f} / rms "
                  f"{row['table_rms_err_db']:4.2f} dB, peak {row['table_peak_diff_db']:+5.2f} dB, waveform residual "
                  f"{row['table_waveform_residual_db']:6.1f} dB | sin^1.5 max {row['sin15_max_err_db']:4.2f} / rms "
                  f"{row['sin15_rms_err_db']:4.2f} dB")
    sh = hertz_shape(E_BALL, n=129)
    with open(os.path.join(REF_DIR, "hertz_shape_e095.json"), "w", encoding="utf-8") as fh:
        json.dump({"description": "Self-similar Hertz + Tsuji contact pulse (core law) for restitution e. For a contact "
                                  "with reduced mass m*, stiffness K [N/m^1.5] and normal speed v: t0 = (m*^2/(K^2 v))^0.2,"
                                  " d0 = (m* v^2/K)^0.4, T = tau t0, F_max = phi K d0^1.5, F(t) = F_max shape(t/T) "
                                  "(shape sampled uniformly on [0, 1], linear interpolation), impulse = (1 + e) m* v.",
                   "e": sh["e"], "alpha_T": sh["alpha"], "tau": sh["tau"], "phi": sh["phi"],
                   "shape_area": sh["area"], "shape": [float(x) for x in sh["shape"]]}, fh, indent=1)
    for key in ("std", "oversized_cb", "bar_ob"):
        write_kernels(BALLS[key], os.path.join(REF_DIR, f"ball_kernels_{key}_48k.json"))
    write_golden(os.path.join(REF_DIR, "golden_runtime_48k.json"))
    print("  wrote golden_runtime_48k.json and ball kernels (std, oversized_cb, bar_ob)")

    # directivity in the horizontal plane (1 m/s, 1.2 m, ear 0.30 m above the cloth): angle from the line of centres
    results["directivity"] = []
    ref_pk = None
    print("directivity, 1 m/s, listener 1.2 m away at 0.30 m height, angle from the line of centres:")
    for deg in (0, 30, 45, 60, 70, 75, 80, 85, 90):
        th = math.radians(deg)
        an = analyze(render_single(ball_ball_impact(1.0), np.array([-1.2 * math.cos(th), 1.2 * math.sin(th), 0.30])))
        ref_pk = an["peak_spl_db"] if ref_pk is None else ref_pk
        results["directivity"].append({"deg": deg, **an})
        print(f"  {deg:3d} deg: peak {an['peak_spl_db']:6.1f} dB ({an['peak_spl_db'] - ref_pk:+5.1f}), SEL "
              f"{an['sel_db']:5.1f}, LAFmax {an['laf_max_dba']:5.1f} dB(A), centroid {an['centroid_hz']:5.0f} Hz")

    # presentation (audio.md 4.2): single on-axis clicks at 1.2 m against the room (76 dBA + mode offset), in LAFmax
    results["presentation"] = []
    print("presentation of single clicks (on-axis, 1.2 m, ear 0.30 m above the cloth) vs the 76 dBA room, LAFmax:")
    for v in (0.25, 1.0, 4.0):
        an = analyze(render_single(ball_ball_impact(v), np.array([-1.2, 0.0, 0.30])))
        row = {"v": v, "peak_spl_db": an["peak_spl_db"], "laf_max_dba": an["laf_max_dba"]}
        parts = []
        for mode, m in PRESENTATION_MODES.items():
            g = presentation_gain_db(an["peak_spl_db"], mode)
            rel = an["laf_max_dba"] + g - (76.0 + m["amb"])
            row[mode] = {"gain_db": g, "peak_dbfs": an["peak_spl_db"] + g - m["l_fs"], "laf_rel_room_db": rel}
            parts.append(f"{mode} {an['peak_spl_db'] + g - m['l_fs']:+5.1f} dBFS, {rel:+5.1f} dB vs room")
        results["presentation"].append(row)
        print(f"  {v:4.2f} m/s: physical peak {an['peak_spl_db']:5.1f} dB, LAFmax {an['laf_max_dba']:5.1f} dB(A) "
              f"({an['laf_max_dba'] - 76.0:+5.1f} vs room) | " + " | ".join(parts))

    # level law vs the realism plan's 8.2 (T23: +7.2247 dB for 2 vs 1 m/s)
    bb = [r for r in results["ball_ball"] if r["listener"] == "shooter"]
    v_ = np.array([r["v"] for r in bb])
    pk = np.array([r["peak_spl_db"] for r in bb])
    sel = np.array([r["sel_db"] for r in bb])
    slope_pk = np.polyfit(np.log10(v_), pk, 1)[0]
    slope_sel = np.polyfit(np.log10(v_), sel, 1)[0]
    d21 = pk[v_ == 2.0][0] - pk[v_ == 1.0][0]
    results["level_law"] = {"peak_db_per_decade": slope_pk, "sel_db_per_decade": slope_sel, "peak_2_vs_1_db": d21,
                            "plan_8_2_db_per_decade": 24.0, "plan_T23_2_vs_1_db": 7.2247}
    print(f"level law: peak {slope_pk:.1f} dB/decade (plan 8.2: 24.0), 2 vs 1 m/s {d21:+.2f} dB (plan T23 +7.22), "
          f"SEL {slope_sel:.1f} dB/decade")
    with open(os.path.join(OUT_DIR, "analysis.json"), "w", encoding="utf-8") as fh:
        json.dump(results, fh, indent=1)
    print(f"wrote {OUT_DIR}")
    return results


def cmd_break(path, args, write_ref=False):
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    bar_box = doc["input"].get("table") == "TABLE_7FT_BAR"
    impacts, tr, skipped, gullies = events_to_impacts(
        doc, RAIL_BARBOX if bar_box else RAIL_PRO, RAIL_BARBOX_FC if bar_box else RAIL_PRO_FC, coin_op=bar_box,
        table_length=float(doc["input"].get("length", 2.54)))
    impacts.sort(key=lambda i: i.time)
    strike = doc["input"]["strikes"][0]
    cb = tr.balls[strike["ball"]]
    p0 = cb["pos"][0]
    phi = strike["phi"]
    aim = np.array([math.cos(phi), math.sin(phi), 0.0])
    left = np.array([-math.sin(phi), math.cos(phi), 0.0])
    head = p0 - 0.55 * aim + np.array([0.0, 0.0, 0.36 - p0[2]])     # breaker's head, eye 0.36 m above the cloth
    ears = [head + 0.0875 * left, head - 0.0875 * left]
    t_end = max([float(doc["stopTime"])] + [g[1] for g in gullies])
    dur = min(t_end + 1.0, args.max_seconds)
    n = int(dur * FS)
    dry = np.zeros((n, 2))
    onsets, peaks = [], []
    for imp in impacts:
        if imp.time < dur:
            i0, y = render_impact_segment(imp, ears)
            add_segment(dry, i0, y)
            peaks.append(20 * math.log10(max(float(np.max(np.abs(y))), 1e-12) / P_REF))
            onsets.append(imp.time)
    # presentation (audio.md 4.2): one plan-time gain envelope per mode on the summed impact stem
    presented = {mode: dry * presentation_gain_envelope(dry, mode)[:, None] for mode in PRESENTATION_MODES}
    cont = rolling_noise(tr, ears, n) + gully_noise(gullies, ears, n)   # continuous layers: physical, no curve
    dry_all = dry + cont
    ir = room_ir()

    def with_room(x):
        return x + np.stack([signal.fftconvolve(x.mean(axis=1), ir[:, ch])[:n] for ch in range(2)], axis=1)

    mix = with_room(dry_all)
    os.makedirs(OUT_DIR, exist_ok=True)
    base = os.path.splitext(os.path.basename(path))[0]
    write_wav(os.path.join(OUT_DIR, f"{base}_dry.wav"), dry_all)
    pk = write_wav(os.path.join(OUT_DIR, f"{base}_room.wav"), mix)
    if write_ref:
        ref_len = int(min(args.ref_seconds, dur) * FS)
        fade = np.linspace(1.0, 0.0, int(0.25 * FS))[:, None]
        clip = mix[:ref_len].copy()
        clip[-len(fade):] *= fade
        write_wav(os.path.join(REF_DIR, f"ref_{base}_room.wav"), clip, bits=24)
    on = np.sort(np.array(onsets))
    gaps = np.diff(on)
    kind = "coin-op bar box" if bar_box else "pro table"
    print(f"break render {base} ({kind}): {len(impacts)} audible impacts ({skipped} pressing/too-slow skipped), "
          f"{len(gullies)} gully runs, "
          f"min onset gap {gaps.min() * 1e6:.0f} us, {np.sum(gaps < 1e-3)} gaps < 1 ms, "
          f"peak {20 * math.log10(np.max(np.abs(mix)) / P_REF):.1f} dB SPL ({pk:+.1f} dBFS at {FULL_SCALE_PA:g} Pa FS)")
    print("  first impacts:", ", ".join(f"{i.label}@{i.time * 1e3:.3f}ms" for i in impacts[:8]))
    print(f"  physical impact peaks at the ears: single impacts max {max(peaks):.1f} dB SPL, median "
          f"{np.median(peaks):.1f} dB SPL; summed stem {20 * math.log10(np.max(np.abs(dry)) / P_REF):.1f} dB SPL")
    bed0 = pink_bed(dur, 76.0)[:n]      # stand-in for the dive-bar room at the table (76 dBA), loudness check only
    for mode, buf in presented.items():
        fs_pa = mode_full_scale_pa(mode)
        stem = with_room(buf + cont) / fs_pa
        bed = bed0 * 10 ** (PRESENTATION_MODES[mode]["amb"] / 20.0) / fs_pa
        tp = true_peak_dbtp(stem)
        lu_stem = loudness_lufs(stem)
        lu_bed = loudness_lufs(bed)
        lu_all = loudness_lufs(stem + bed)
        m = PRESENTATION_MODES[mode]
        print(f"  presentation {mode:6s} (0 dBFS = {m['l_fs']:.0f} dB SPL, knee {m['knee']:.0f} dB, ratio "
              f"{m['ratio']:g}, ambience {m['amb']:+g} dB): table stem true peak {tp:+.1f} dBTP, {lu_stem:.1f} LUFS; "
              f"76 dBA room stand-in "
              f"{lu_bed:.1f} LUFS; together {lu_all:.1f} LUFS")
        write_wav(os.path.join(OUT_DIR, f"{base}_{mode}.wav"), stem, full_scale_pa=1.0)
        if write_ref and mode == "wide":
            ref_len = int(min(args.ref_seconds, dur) * FS)
            fade = np.linspace(1.0, 0.0, int(0.25 * FS))[:, None]
            clip = stem[:ref_len].copy()
            clip[-len(fade):] *= fade
            write_wav(os.path.join(REF_DIR, f"ref_{base}_wide.wav"), clip, bits=24, full_scale_pa=1.0)
    return impacts


DIVEBAR_BREAK_ARGS = ["--table", "7ft-bar", "--balls", "oldbar", "--rack", "8ball", "--rack-gap", "sloppy",
                      "--seed", "13", "--cue", "house", "--ball", "0:-0.62,0.10", "--speed", "8", "--aim", "-5.07",
                      "--offset", "0,-0.05", "--dt", "0.02", "--compact", "--no-states"]


def ensure_divebar_break():
    """Dive-bar 8-ball break (7-ft bar box, oversized 221 g cue ball, house cue, 8 m/s, sloppy rack, 3 balls down).
    Generated with rbsim when the JSON is missing (Release or Debug build of Tools/rbsim)."""
    out = os.path.join(OUT_DIR, "divebar_break8.json")
    if os.path.exists(out):
        return out
    root = os.path.normpath(os.path.join(HERE, "..", ".."))
    for cfg in ("Release", "Debug"):
        exe = os.path.join(root, "build", "Tools", "rbsim", cfg, "rbsim.exe")
        if os.path.exists(exe):
            import subprocess
            os.makedirs(OUT_DIR, exist_ok=True)
            subprocess.run([exe] + DIVEBAR_BREAK_ARGS + ["--out", out], check=True)
            return out
    print("rbsim not built; skipping the dive-bar break (cmake --build build --target rbsim)")
    return None


def cmd_analyze(path, args):
    fs, x = read_wav(path)
    if fs != FS:
        print(f"note: {fs} Hz file; metrics computed at the file rate")
    p = x * args.full_scale_pa
    an = analyze(p, fs=fs, window_ms=args.window_ms, onset_db=args.onset_db)
    an["true_peak_dbtp"] = true_peak_dbtp(x)
    an["loudness_lufs"] = loudness_lufs(x, fs)
    for k, v in an.items():
        print(f"  {k:14s} {v:10.2f}")
    return an


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--break", dest="brk", metavar="RBSIM_JSON")
    ap.add_argument("--analyze", metavar="WAV")
    ap.add_argument("--full-scale-pa", type=float, default=FULL_SCALE_PA,
                    help="--analyze: Pa per full-scale unit (1.0 for relative metrics of uncalibrated recordings)")
    ap.add_argument("--window-ms", type=float, default=60.0)
    ap.add_argument("--onset-db", type=float, default=-30.0)
    ap.add_argument("--max-seconds", type=float, default=12.0)
    ap.add_argument("--ref-seconds", type=float, default=8.0)
    args = ap.parse_args()
    if args.selftest:
        return 0 if cmd_selftest() else 1
    if args.analyze:
        cmd_analyze(args.analyze, args)
        return 0
    if args.brk:
        cmd_break(args.brk, args)
        return 0
    if not cmd_selftest():
        print("selftest failed; renders not written")
        return 1
    cmd_standard(args)
    divebar = ensure_divebar_break()
    if divebar:
        cmd_break(divebar, args, write_ref=True)
    default_break = os.path.normpath(os.path.join(HERE, "..", "rbsim", "examples", "break9.json"))
    if os.path.exists(default_break):
        cmd_break(default_break, args, write_ref=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
