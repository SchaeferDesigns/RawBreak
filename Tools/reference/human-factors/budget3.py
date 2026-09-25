import math, sys
import stroke as ST
from noise import *
R = 0.028575
def run(mr, attr=25, P=1.0, settle=False, mask=(), N=20000, drift_scale=1.0, gpow=None):
    ST.DEFAULT["sigma_drift"] = 0.9e-3 * drift_scale
    I, A, S, T, K = ST.S0(); A = {k: attr for k in A}; S = dict(S); S["P"] = P
    miss = 0
    for i in range(N):
        I2 = dict(I); I2["V"] = 1.5; I2["tc"] = 1.5 + 2.5 * u01(hashkeys(99, i)); I2["ts"] = I2["tc"] - 2.0 if settle else -1.0
        key = dict(seed=0xB00D, rack=i // 20, shot=i, shooter=1, sshot=i, pickup=0)
        r = ST.execute(I2, A, S, T, key, mask=mask)
        a = r["a"]; err = r["phi_x"] + math.atan2(2.5*a*math.sqrt(1-a*a), 1+mr+2.5*(1-a*a))
        if abs(err) / (2 * R * math.cos(math.radians(30))) > math.radians(2.0): miss += 1
    return miss / N * 100
mode = sys.argv[1] if __name__ == "__main__" else ""
if mode == "masks":
    for mr in [15, 20]:
        print(mr, "all", run(mr), "drift only", run(mr, mask=(3,4,5,6,7,8,9)), "tremor only", run(mr, mask=(1,2,5,6,7,8,9)), "pershot only", run(mr, mask=(1,2,3,4)))
if mode == "scale":
    for ds in [float(x) for x in sys.argv[2:]]:
        print(ds, [(mr, round(run(mr, P=0, drift_scale=ds), 2), round(run(mr, drift_scale=ds), 2), round(run(mr, settle=True, drift_scale=ds), 2)) for mr in (15, 20, 40)])
