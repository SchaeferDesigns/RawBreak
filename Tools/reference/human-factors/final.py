import math, sys
import stroke as ST
from stroke import execute, S0
from noise import *
ST.DEFAULT["drift_gpow"] = 1.0 / 3.0
I, A, S, T, K = S0()
r = execute(I, A, S, T, K, swoop_mode="Lb")
print("T05", {k: repr(r[k]) for k in ["phi_x","theta_x","A_x","B_x","V_x","v_r","v_u","a","b","y_g","z_g"]})
I2 = dict(I); I2["ts"] = 0.8; S2 = dict(S); S2["P"] = 1.0
r = execute(I2, A, S2, T, K, swoop_mode="Lb")
print("T06", r["g"], r["ks"], repr(r["fl"]), repr(r["phi_x"]), repr(r["theta_x"]), repr(r["A_x"]), repr(r["B_x"]), repr(r["V_x"]))
A3 = {k: 100 for k in A}
r = execute(I, A3, S, T, K, swoop_mode="Lb")
print("T07", repr(r["phi_x"]), repr(r["theta_x"]), repr(r["A_x"]), repr(r["B_x"]), repr(r["V_x"]))
if len(sys.argv) > 1:
    import budget as B
    print("S04", [(a, B.budget(a)[0]) for a in (25, 10, 40, 60, 85, 100)])
    print("S06", B.budget(25, P=1.0)[0], B.budget(25, P=1.0, settle=True)[0], B.budget(25, P=0.0, settle=True)[0])
    print("S05", [(a, B.budget(a, seed=0xD4A3, shooter=2, hk=98, V=2.0, Bi=-0.61693, mode="miscue_rho")[0]) for a in (10, 25, 40, 60, 85, 100)])
    print("elev", B.budget(25, extra=dict(bridge="Elevated", ds=0.5))[0])
