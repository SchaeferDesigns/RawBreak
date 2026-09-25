import numpy as np
from math import *
exec(open('ref.py').read().split("print(\"alpha_sp")[0])
print("masse dir no squirt:", degrees(atan2(-0.4*sin(radians(75)), cos(radians(75))-0.3)))
oz=0.028349523125; M19=19*oz
for lam in (0.0,1.0):
    o=strike(2.5, radians(75), 0, 0.4, -0.3, M19, 0.73, lam=lam, mr_end=None)
    vf=5/7*np.array([o["v"][0],o["v"][1],0])-2/7*R*np.cross(Z,o["w"])
    print("lam",lam,"J=%.6f"%o["J"],"v=",np.round(o["v"],6),"w=",np.round(o["w"],4),"vf=",np.round(vf,6),"dir=%.4f"%degrees(atan2(vf[1],vf[0])), "stick",o["stick"])
mph=0.44704
v0=11.194*mph; th=radians(5)
print("B10 pre: v=(%.6f,0,%.6f) w_y=%.4f"%(v0*cos(th),-v0*sin(th),-34.841*2*pi))
# swerve strike level cue check: theta=0,a=0.4 -> u parallel v
o=strike(3.0,0,0,0.4,0,M19,0.73,mr_end=20)
print("level side:",o["v"],o["w"],"u=",u_of(o["v"],o["w"]))
# elevated swerve with no airborne: theta small? print theta=10 a=.4 V=2
o=strike(2.0,radians(10),0,0.4,0,M19,0.73,mr_end=20)
print("theta10:",{k:(np.round(v,6) if hasattr(v,'__len__') else v) for k,v in o.items()})
vf=5/7*np.array([o["v"][0],o["v"][1],0])-2/7*R*np.cross(Z,o["w"]); print(" vf",vf, degrees(atan2(vf[1],vf[0])))
o=strike(8.5, radians(5), 0, 0, 0.1, 21*oz, 0.85)
print("break:", {k:(np.round(v,6) if hasattr(v,'__len__') else v) for k,v in o.items()})
o=strike(4.0, radians(50), 0, 0, 0, 9*oz, 0.85)
print("jump:", {k:(np.round(v,6) if hasattr(v,'__len__') else v) for k,v in o.items()})
