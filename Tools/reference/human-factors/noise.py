# Independent re-implementation of human-factors.md 3.2 (verifier)
import math
M64 = (1 << 64) - 1

def mix64(x):
    s = (x + 0x9E3779B97F4A7C15) & M64
    z = s
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return z ^ (z >> 31)

def hashkeys(*keys):
    h = 0x243F6A8885A308D3
    for k in keys:
        h = mix64(h ^ (k & M64))
    return h

def u01(h):
    return (h >> 11) * 2.0 ** -53

_a = [-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00]
_b = [-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01]
_c = [-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00]
_d = [7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00]

def invnorm(p):
    plow = 0.02425
    if p <= 0.0:
        return -math.inf
    if p >= 1.0:
        return math.inf
    if p < plow:
        q = math.sqrt(-2 * math.log(p))
        return (((((_c[0]*q+_c[1])*q+_c[2])*q+_c[3])*q+_c[4])*q+_c[5]) / ((((_d[0]*q+_d[1])*q+_d[2])*q+_d[3])*q+1)
    if p > 1 - plow:
        q = math.sqrt(-2 * math.log(1 - p))
        return -(((((_c[0]*q+_c[1])*q+_c[2])*q+_c[3])*q+_c[4])*q+_c[5]) / ((((_d[0]*q+_d[1])*q+_d[2])*q+_d[3])*q+1)
    q = p - 0.5
    r = q * q
    return (((((_a[0]*r+_a[1])*r+_a[2])*r+_a[3])*r+_a[4])*r+_a[5])*q / (((((_b[0]*r+_b[1])*r+_b[2])*r+_b[3])*r+_b[4])*r+1)

PHI_LO = 0.0062096653257761
def truncnormal(u):
    return invnorm(PHI_LO + u * (1 - 2 * PHI_LO))

def bag_perm(seed, shooter, channel, b):
    perm = list(range(8))
    for i in range(7, 0, -1):
        r = hashkeys(seed, shooter, channel, b, 100 + i)
        k = ((r >> 32) * (i + 1)) >> 32
        perm[i], perm[k] = perm[k], perm[i]
    return perm

def bag_u(seed, shooter, channel, n):
    b, j = n // 8, n % 8
    perm = bag_perm(seed, shooter, channel, b)
    return (perm[j] + u01(hashkeys(seed, shooter, channel, b, 200 + j))) / 8.0

def bag_eps(seed, shooter, channel, n):
    return truncnormal(bag_u(seed, shooter, channel, n))

class Process:
    def __init__(self, seed, rack, shot, shooter, channel, flo, fhi, K=6):
        self.f = []
        self.ph = []
        for k in range(K):
            self.f.append(flo + (fhi - flo) * (k + u01(hashkeys(seed, rack, shot, shooter, channel, 2 * k))) / K)
            self.ph.append(2 * math.pi * u01(hashkeys(seed, rack, shot, shooter, channel, 2 * k + 1)))
        self.K = K
    def D(self, t):
        return math.sqrt(2.0 / self.K) * sum(math.cos(2 * math.pi * f * t + p) for f, p in zip(self.f, self.ph))
    def Dp(self, t):
        return -math.sqrt(2.0 / self.K) * sum(2 * math.pi * f * math.sin(2 * math.pi * f * t + p) for f, p in zip(self.f, self.ph))

if __name__ == "__main__":
    print("T01", hex(mix64(0)), hex(hashkeys(1, 2, 3)), hex(hashkeys(0x5EED, 0, 0, 1, 5, 0)), repr(u01(hashkeys(0x5EED, 0, 0, 1, 5, 0))))
    print("T02", invnorm(0.975), invnorm(0.02), invnorm(0.5), truncnormal(0.0), truncnormal(1.0))
    print("T03 perms", bag_perm(0x5EED, 1, 5, 0), bag_perm(0x5EED, 1, 5, 1))
    print("T03 n0", bag_u(0x5EED, 1, 5, 0), bag_eps(0x5EED, 1, 5, 0), "n3", bag_eps(0x5EED, 1, 5, 3), "n15", bag_eps(0x5EED, 1, 5, 15))
    Dl = Process(0x5EED, 0, 0, 1, 1, 0.15, 0.6)
    Tl = Process(0x5EED, 0, 0, 1, 3, 8.0, 12.0)
    print("T04", Dl.D(2.5), Dl.Dp(2.5), Tl.D(2.5), Tl.Dp(2.5))
    # S01/S02
    xs = [bag_eps(0x5EED, 3, 8, n) for n in range(80000)]
    m = sum(xs) / len(xs)
    sd = math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))
    sd1 = math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))
    print("S01", m, sd, sd1, max(abs(x) for x in xs))
    # theory sd of truncated normal at 2.5
    phi = math.exp(-2.5 ** 2 / 2) / math.sqrt(2 * math.pi)
    Z = 1 - 2 * PHI_LO
    print("theory sd", math.sqrt(1 - 2 * 2.5 * phi / Z))
    us = [bag_u(0x5EED, 3, 8, n) for n in range(4000)]
    bins = [int(u * 8) for u in us]
    ok_blocks = all(sorted(bins[i:i + 8]) == list(range(8)) for i in range(0, 4000, 8))
    maxcount = max(max(bins[i:i + 8].count(k) for k in range(8)) for i in range(0, 4000 - 7))
    # longest run of worst bin (bin 0 or 7?) -- consecutive shots in same extreme bin
    def longest_run(bn):
        best = cur = 0
        for x in bins:
            cur = cur + 1 if x == bn else 0
            best = max(best, cur)
        return best
    print("S02", ok_blocks, maxcount, longest_run(0), longest_run(7))
    # S03
    Tt = Process(0x5EED, 0, 0, 1, 3, 8.0, 12.0)
    n = 600000
    s1 = s2 = 0.0
    for i in range(n):
        t = i / 1000.0
        s1 += Dl.D(t) ** 2
        s2 += Tt.D(t) ** 2
    print("S03 rms", math.sqrt(s1 / n), math.sqrt(s2 / n))
