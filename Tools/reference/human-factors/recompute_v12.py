# Recomputes every human-factors.md v1.2 expected value that depends on the per-shot draws after the
# product-owner decision Q1 (streak-guarded independent draws instead of bags of 8; streak.py):
# HF-T03, HF-S01, HF-S02, HF-T05..T07, HF-S04..S06, the 3.10 budget table and the values quoted in 3.10,
# 5.3 and 7. Everything else (hash, InvNorm, processes, warp, chalk, tilt) is unchanged.
#
# Usage: python recompute_v12.py [all|draws|stroke|budget]      (budget takes a few minutes)
import math
import sys

import noise
import stroke as ST
import streak
from noise import truncnormal

ST.DEFAULT["drift_gpow"] = 1.0 / 3.0   # v1.1 refit (9.2 item 3)

_cache = {}


def _u(seed, shooter, channel, n):
    key = (seed, shooter, channel)
    s = _cache.get(key)
    if s is None or len(s) <= n:
        s = streak.stream(seed, shooter, channel, max(n + 1, 2 * (len(s) if s else 0), 64))
        _cache[key] = s
    return s[n][0]


def guarded_u(seed, shooter, channel, n):
    return _u(seed, shooter, channel, n)


def guarded_eps(seed, shooter, channel, n):
    return truncnormal(_u(seed, shooter, channel, n))


# Swap the v1.1 bag functions used by stroke.execute for the v1.2 streak-guarded draws.
ST.bag_u = guarded_u
ST.bag_eps = guarded_eps


def draws():
    s = streak.stream(0x5EED, 1, 5, 16)
    print("HF-T03 eighths n=0..15:", [streak.eighth(u) for u, _ in s])
    print("HF-T03 subs     n=0..15:", [sub for _, sub in s])
    for n in (0, 3, 8, 11, 15):
        u, sub = s[n]
        print(f"HF-T03 n={n}: u {u!r} sub {sub} eps {truncnormal(u)!r}")
    print("HF-T03 first redraw at n =", next(n for n, (_, sub) in enumerate(s) if sub > 0))
    # HF-S01
    N = 80000
    st = streak.stream(0x5EED, 3, 8, N)
    xs = [truncnormal(u) for u, _ in st]
    m = sum(xs) / N
    sd = math.sqrt(sum((x - m) ** 2 for x in xs) / N)
    redrawn = sum(1 for _, sub in st if sub > 0)
    print(f"HF-S01 mean {m!r} sd {sd!r} max|eps| {max(abs(x) for x in xs)!r} redrawn {redrawn} ({redrawn / N:.4%}) max sub {max(sub for _, sub in st)}")
    # HF-S02 (first 4000 draws of HF-S01)
    bins = [streak.eighth(u) for u, _ in st[:4000]]
    worst8 = max(max(bins[i:i + 8].count(k) for k in range(8)) for i in range(0, 4000 - 7))
    worst7 = max(max(bins[i:i + 7].count(k) for k in range(8)) for i in range(0, 4000 - 6))
    best = cur = 0
    prev = -1
    for b in bins:
        cur = cur + 1 if b == prev else 1
        prev = b
        best = max(best, cur)
    freq = [bins.count(k) for k in range(8)]
    aligned_full = sum(1 for i in range(0, 4000, 8) if sorted(bins[i:i + 8]) == list(range(8)))
    red4000 = sum(1 for _, sub in st[:4000] if sub > 0)
    print(f"HF-S02 max count of an eighth in any 8 consecutive: {worst8}; in any 7: {worst7}; longest run {best}; "
          f"eighth counts {freq} (min {min(freq)}, max {max(freq)}); aligned blocks covering all 8: {aligned_full}/500; "
          f"redrawn {red4000}")
    # sanity: pure function (rebuild) == incremental
    for n in (0, 1, 7, 8, 100, 3999):
        h = streak.rebuild_history(0x5EED, 3, 8, n)
        assert h == [streak.eighth(u) for u, _ in st[max(0, n - 7):n]], n
    print("rebuild == incremental: ok")


def stroke():
    I, A, S, T, K = ST.S0()
    r = ST.execute(I, A, S, T, K, swoop_mode="Lb")
    print("HF-T05", {k: repr(r[k]) for k in ["phi_x", "theta_x", "A_x", "B_x", "V_x", "v_r", "v_u", "a", "b", "mu", "y_g", "z_g", "eA", "eB", "eEl", "eV", "E"]}, "miscue", r["miscue"])
    I2 = dict(I)
    I2["ts"] = 0.8
    S2 = dict(S)
    S2["P"] = 1.0
    r = ST.execute(I2, A, S2, T, K, swoop_mode="Lb")
    print("HF-T06 g", r["g"], "k_set", r["ks"], "flinch", repr(r["fl"]), {k: repr(r[k]) for k in ["phi_x", "theta_x", "A_x", "B_x", "V_x"]})
    A3 = {k: 100 for k in A}
    r = ST.execute(I, A3, S, T, K, swoop_mode="Lb")
    print("HF-T07", {k: repr(r[k]) for k in ["phi_x", "theta_x", "A_x", "B_x", "V_x"]})
    r = ST.execute(I, A, S, T, K, NS=0.0)
    print("HF-T08", r["phi_x"], r["theta_x"] == 3 * ST.DEG, r["A_x"], r["B_x"], r["V_x"], r["mu"])


def budget():
    import budget as B

    def with_mr(mr):
        B.alsq = lambda a, m=mr: math.atan2(2.5 * a * math.sqrt(1 - a * a), 1 + m + 2.5 * (1 - a * a))

    with_mr(15)
    print("HF-S04 (m/m_e 15):", [(a, B.budget(a)) for a in (25, 10, 40, 60, 85, 100)])
    print("HF-S05:", [(a, B.budget(a, seed=0xD4A3, shooter=2, hk=98, V=2.0, Bi=-0.61693, mode="miscue_rho")[0]) for a in (10, 25, 40, 60, 85, 100)])
    print("INFO elevated bridge + stance 0.5 (m/m_e 15):", B.budget(25, extra=dict(bridge="Elevated", ds=0.5)))
    for mr in (15, 20, 40):
        with_mr(mr)
        print(f"3.10 m/m_e {mr}: B1@25 {B.budget(25)}, B1@100 {B.budget(100)}, B2 {B.budget(25, P=1.0, settle=True)}, "
              f"B3 {B.budget(25, P=1.0)}, P0 settle {B.budget(25, P=0.0, settle=True)}, B1@40 {B.budget(40)}")


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("all", "draws"):
        draws()
    if which in ("all", "stroke"):
        stroke()
    if which in ("all", "budget"):
        budget()
