import math
d2r=math.pi/180
def defl_e(phi,e):
    n=(math.sin(phi),math.cos(phi)); t=(-math.cos(phi),math.sin(phi))
    vt=math.sin(phi); vn=math.cos(phi)*(1-e)/2
    vp=(vt*t[0]+vn*n[0], vt*t[1]+vn*n[1])
    vf=(5/7*vp[0], 5/7*vp[1]+2/7)
    return abs(math.degrees(math.atan2(vf[0],vf[1]))), math.hypot(*vf)
print("half-ball e=1:",defl_e(30*d2r,1.0)," e=0.94:",defl_e(30*d2r,0.94))
best=max((defl_e(x*0.001*d2r,0.94)[0],x*0.001) for x in range(1000,89000)); print("max e=.94",best)
best=max((defl_e(x*0.001*d2r,1.0)[0],x*0.001) for x in range(1000,89000)); print("max e=1",best)
# head-on rolling: final CB speed
print("head-on rolling e=1 final CB speed ratio",2/7, " e=0.94:", 5/7*(1-0.94)/2+2/7)
# stun head-on e=0.94: CB keeps (1-e)/2 v, no spin -> final 5/7 of that
print("stun head-on e=.94 CB residual speed ratio",(1-0.94)/2,"final rolling",5/7*(1-0.94)/2)
