import math
from noise import *
from stroke import chalk_weights, chalk_c
# T09
R=0.028575; rd=0.0106
def alsq(a,mr): return math.atan2(2.5*a*math.sqrt(1-a*a), 1+mr+2.5*(1-a*a))
for mr in [15,20,40]:
    Lp=(R+rd)/(2.5/(3.5+mr))
    y=1e-3; a=-y*0.228575/(R+rd)
    print("T09",mr,round(Lp,6),(y+alsq(a,mr))/y, "at Lp:", (y+alsq(-y*Lp/(R+rd),mr))/y)
print("T09 yaw/Ashift",1e-3/0.8, -(1e-3/0.8)*0.228575/R)
# T10
g=2*0.003/1.4478; chi=2*math.pi*u01(hashkeys(0x5EED,1,10,0))
print("T10",chi,g*math.sin(chi),g*math.cos(chi),g, math.degrees(g))
# T12
q=rd*0.45/(0.01275/2); beta=math.atan2(0,-0.45)
print("T12 q",q,beta,chalk_weights(q,beta))
sev=(0.5+0.25*2)*(0.6+2*0.45**2)
c=1.0
for hit in range(1,12):
    mu=0.35+0.25*c; rmax=mu/math.sqrt(1+mu*mu)
    print(" hit",hit,"c before",round(c,9),"rho_max",round(rmax,9),"miscue" if 0.45>rmax else "")
    c*=math.exp(-sev/18)
print("sev",sev)
# threshold
cth=(0.45/math.sqrt(1-0.45**2)-0.35)/0.25
print("c_th",cth,"hits",-math.log(cth)*1/1.005,"x n_c")
for nc,cap in [(10,0.7),(18,1),(30,1),(45,1)]:
    c=cap; hit=1
    while True:
        mu=0.35+0.25*c; rmax=mu/math.sqrt(1+mu*mu)
        if 0.45>rmax: break
        c*=math.exp(-sev/nc); hit+=1
    print("grade",nc,cap,"first miscue hit",hit, "fresh rho_max", (0.35+0.25*cap)/math.sqrt(1+(0.35+0.25*cap)**2))
# T13 chalking
def chalk(cov,tw,H,cap=1.0,hollow=0.0,glaze=0.0):
    cov=list(cov)
    e0=0.6*(1-0.5*glaze); er=(0.12+0.33*H)*(1-0.5*glaze)*(1-0.7*hollow)
    for _ in range(tw):
        cov[0]=cov[0]+max(0,cap-cov[0])*e0
        for z in range(1,7): cov[z]=cov[z]+max(0,cap-cov[z])*er
    return cov
c0=[0.5,0.2]+[0.9]*5
print("T13",chalk(c0,3,0),chalk(c0,3,1),chalk(c0,1,0,0.7,0.8))
# T14
for w,r in [(12.75,10.6),(12.75,8.96),(12.75,18),(11,18)]: print("T14",(w/2)/r)
print("R/(R+r)",R/(R+0.0106),R/(R+0.018))
