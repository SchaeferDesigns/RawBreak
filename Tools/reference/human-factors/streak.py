# human-factors.md 3.2 (v1.2): streak-guarded independent draws, product-owner decision Q1.
#
# Replaces the fixed bags of 8 (v1.1). Per shooter key S and channel c, draw n (= ShooterShotIndex) takes
# the first candidate u_{n,Sub} = U01(HashKeys(MatchSeed, S, c, n, Sub)), Sub = 0, 1, ..., whose eighth
# floor(8 u) occurs fewer than 2 times among the eighths of the ACCEPTED draws n-7 .. n-1 of the same (S, c).
# At most 3 eighths are excluded (3 x 2 = 6 <= 7 < 8), so every candidate is accepted with probability >= 5/8
# and the next draw is never known in advance. After kMaxRedraws rejected candidates (probability
# <= (3/8)^32 = 2.4e-14) the last candidate is mapped into the allowed eighths (deterministic fallback) in EXACT
# integer arithmetic (v1.3): N = the candidate's 53-bit integer (u = N 2^-53), m = number of allowed eighths A,
# P = m N, j = P >> 53, u = (A[j] 2^50 + ((P mod 2^53) >> 3)) 2^-53. The floating-point form of v1.2,
# (A[j] + (m u - j)) / 8, rounds m u and can land in eighth A[j] + 1, which may be excluded (fallback_float_fails).
# Streak guard off, or AI rollout keys (Purpose != 0): u = u_{n,0} (plain independent draw, no history).
#
# Mirrors rb/Human/NoiseHash.h (DrawGuarded, PushStreak, RebuildStreakHistory).
from noise import hashkeys, u01, truncnormal

WINDOW = 7        # kStreakWindow
CAP = 2           # kStreakMaxPerEighth
MAX_REDRAWS = 32  # kStreakMaxRedraws


def candidate(seed, skey, channel, n, sub):
    return u01(hashkeys(seed, skey, channel, n, sub))


def eighth(u):
    return int(u * 8.0)  # floor(8 u) for u in [0, 1); 8 u is exact


def guarded_draw(seed, skey, channel, n, history):
    """history: eighths of the accepted draws max(0, n-7) .. n-1 (oldest first). Returns (u, sub)."""
    counts = [0] * 8
    for e in history:
        counts[e] += 1
    u = 0.0
    for sub in range(MAX_REDRAWS):
        u = candidate(seed, skey, channel, n, sub)
        if counts[eighth(u)] < CAP:
            return u, sub
    return fallback([e for e in range(8) if counts[e] < CAP], u), MAX_REDRAWS


TWO53 = 1 << 53


def fallback(allowed, u):
    """Maps the last candidate u = N 2^-53 into the allowed eighths (ascending, m = len >= 5), exactly."""
    n = int(u * TWO53)           # exact: u is a multiple of 2^-53
    assert n * 2.0 ** -53 == u
    p = len(allowed) * n         # < 2^56
    j = p >> 53
    return (allowed[j] * (1 << 50) + ((p & (TWO53 - 1)) >> 3)) / float(TWO53)


def fallback_float_fails():
    """Counter-example to the v1.2 floating-point fallback: lands in the excluded eighth 6."""
    n, allowed = 7205759403792793, [0, 1, 3, 5, 7]
    u = n * 2.0 ** -53
    m = len(allowed)
    j = int(m * u)
    v12 = (allowed[j] + (m * u - j)) / 8.0
    return eighth(v12), eighth(fallback(allowed, u))   # (6, 5): 6 is excluded, 5 = allowed[j]


def push(history, u):
    history.append(eighth(u))
    if len(history) > WINDOW:
        history.pop(0)


def stream(seed, skey, channel, count):
    """Accepted draws n = 0 .. count-1 of one (seed, shooter key, channel): list of (u, sub)."""
    hist, out = [], []
    for n in range(count):
        u, sub = guarded_draw(seed, skey, channel, n, hist)
        out.append((u, sub))
        push(hist, u)
    return out


def rebuild_history(seed, skey, channel, n):
    """History before draw n (the canonical definition: recursion from draw 0)."""
    hist = []
    for k in range(n):
        u, _ = guarded_draw(seed, skey, channel, k, hist)
        push(hist, u)
    return hist


def plain_draw(seed, skey, channel, n):
    return candidate(seed, skey, channel, n, 0)


if __name__ == "__main__":
    s = stream(0x5EED, 1, 5, 16)
    print("T03 first 16 (u, sub, eighth, eps):")
    for n, (u, sub) in enumerate(s):
        print(" ", n, repr(u), sub, eighth(u), repr(truncnormal(u)))
    assert rebuild_history(0x5EED, 1, 5, 16) == [eighth(u) for u, _ in s[-7:]]
    print("fallback: v1.2 float form -> eighth %d (excluded), exact form -> eighth %d" % fallback_float_fails())
    import random
    rng = random.Random(1)
    for _ in range(100000):
        allowed = sorted(rng.sample(range(8), rng.randint(5, 8)))
        u = rng.getrandbits(53) * 2.0 ** -53
        v = fallback(allowed, u)
        assert eighth(v) in allowed and 0.0 <= v < 1.0
    print("fallback: 1e5 random cases land in an allowed eighth")
