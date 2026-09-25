import math, sys
from noise import *
from stroke import execute, S0, DEG
R = 0.028575
def alsq(a, mr=15): return math.atan2(2.5*a*math.sqrt(1-a*a), 1+mr+2.5*(1-a*a))
def budget(attr, P=0.0, settle=False, N=20000, seed=0xB00D, shooter=1, hk=99, V=1.5, Bi=0.0, mode="pot", NSc=1.0, extra=None):
    I, A, S, T, K = S0()
    A = {k: attr for k in A}
    if isinstance(attr, dict): A = attr
    S = dict(S); S["P"] = P
    if extra: S.update(extra)
    miss = 0; errs = []
    for i in range(N):
        I2 = dict(I); I2["V"] = V; I2["B"] = Bi
        I2["tc"] = 1.5 + 2.5 * u01(hashkeys(hk, i))
        I2["ts"] = I2["tc"] - 2.0 if settle else -1.0
        key = dict(seed=seed, rack=i // 20, shot=i, shooter=shooter, sshot=i, pickup=0)
        r = execute(I2, A, S, T, key, NS=NSc)
        if mode == "pot":
            err = r["phi_x"] - I2["phi"] + alsq(r["a"])
            errs.append(err)
            if abs(err) * 1.0 / (2 * R * math.cos(30 * DEG)) > 2.0 * DEG: miss += 1
        else:
            c = math.sqrt(1 - r["rho"]**2)
            if mode == "miscue_rho":
                if r["rho"] > r["mlim"]: miss += 1
            else:
                if r["miscue"]: miss += 1
    sd = math.sqrt(sum(e*e for e in errs)/len(errs)) if errs else 0
    return miss, math.degrees(sd)
if __name__ == "__main__":
    which = sys.argv[1]
    if which == "S04":
        for a in [25, 10, 40, 60, 85, 100]: print("S04", a, budget(a))
    if which == "S05":
        for a in [10, 25, 40, 60]:
            print("S05", a, budget(a, seed=0xD4A3, shooter=2, hk=98, V=2.0, Bi=-0.61693, mode="miscue_hf"),
                  budget(a, seed=0xD4A3, shooter=2, hk=98, V=2.0, Bi=-0.61693, mode="miscue_rho"))
    if which == "S06":
        print("S06 P1", budget(25, P=1.0)); print("S06 P1 settle", budget(25, P=1.0, settle=True)); print("S06 P0 settle", budget(25, P=0.0, settle=True))
    if which == "INFO":
        print("elev+stance", budget(25, extra=dict(bridge="Elevated", ds=0.5)))
