import math
R=0.028575; g=9.81
d2r=math.pi/180
# TP A.14 throw model (Alciatore). v: CB speed at impact, wx: vertical-plane spin (rolling: wx=v/R), wz: sidespin, phi: cut angle
a,b,c=9.951e-3,0.108,1.088
def mu(vr): return a+b*math.exp(-c*vr)
def throw(v,wx,wz,phi):
    vrel=math.sqrt((v*math.sin(phi)-R*wz)**2+(R*wx*math.cos(phi))**2)
    if vrel==0: return 0.0
    m=mu(vrel)
    t=min(m*v*math.cos(phi)/vrel, 1/7)*(v*math.sin(phi)-R*wz)
    return math.atan(t/(v*math.cos(phi)))/d2r
speeds={'slow':0.447,'medium':1.341,'fast':3.129}
print("Stun CIT (deg):")
for cut in [5,10,20,30,35,45,60]:
    print(cut, {k:round(throw(v,0,0,cut*d2r),3) for k,v in speeds.items()})
print("Rolling CIT (deg):")
for cut in [10,30,45]:
    print(cut, {k:round(throw(v,v/R,0,cut*d2r),3) for k,v in speeds.items()})
# max stun throw slow
best=max((throw(0.447,0,0,x*0.1*d2r),x*0.1) for x in range(1,900)); print("max slow stun",best)
best=max((throw(1.341,0,0,x*0.1*d2r),x*0.1) for x in range(1,900)); print("max med stun",best)
best=max((throw(3.129,0,0,x*0.1*d2r),x*0.1) for x in range(1,900)); print("max fast stun",best)
# SIT straight-on stun, sidespin with SRF=1.25*pE: Rwz = 1.25*pE*v ; sign: outside/inside handled by sign
for pE in [0.25,0.5,1.0]:
    print("SIT straight stun pE",pE,{k:round(throw(v,0,1.25*pE*v/R,0),3) for k,v in speeds.items()})
# gearing outside english at 30 deg: R wz = v sin(phi)
print("gearing 30deg:",throw(1.341,0,1.341*math.sin(30*d2r)/R,30*d2r))
# TP A.5: 90-degree rule with e and mu (half-ball)
def tpa5(e,m,phi):
    th2=math.atan2(min(m*(1+e)*math.cos(phi), math.sin(phi)/7)*1.0*0+0,1) # placeholder
