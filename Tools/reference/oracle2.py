import math
d2r=math.pi/180; g=9.81; R=0.028575
# squirt TP A.31
def squirt(br,mr): return math.atan(2.5*br*math.sqrt(1-br*br)/(1+mr+2.5*(1-br*br)))/d2r
print("squirt Players mr=20.151 b/R=0.51/1.125:",squirt(0.51/1.125,20.151))
print("squirt PredZ mr=29.158:",squirt(0.51/1.125,29.158))
print("squirt Stinger mr=12.008 b=0.3in:",squirt(0.3/1.125,12.008))
for mr in [12,20,30,50]:
    print("mr",mr,[round(squirt(b,mr),3) for b in (0.1,0.25,0.5)])
# 30 deg rule ideal: final CB angle from tangent? TP3.3: theta_c = atan( sin cos / (sin^2 + 2/5) ) measured from original direction
def theta30(phi): return math.atan(math.sin(phi)*math.cos(phi)/(math.sin(phi)**2+0.4))/d2r
for f in [0.25,0.5,0.75]:
    phi=math.asin(1-f); print("f",f,"cut",phi/d2r,"defl",theta30(phi))
# with e only (rolling CB): post-impact CB velocity components in (t,n): t: v sin phi, n: v cos phi (1-e)/2 ; final = 5/7 vpost + 2/7 v*orig
def defl_e(phi,e):
    # orig direction y: n-axis at angle phi from y. Build vectors in x,y with y = original direction
    n=(math.sin(phi),math.cos(phi)); t=(math.cos(phi),-math.sin(phi))
    vt=math.sin(phi); vn=math.cos(phi)*(1-e)/2
    vp=(vt*t[0]+vn*n[0], vt*t[1]+vn*n[1])
    vf=(5/7*vp[0], 5/7*vp[1]+2/7)
    return math.degrees(math.atan2(vf[0],vf[1])), math.hypot(*vf)
print("half-ball e=1:",defl_e(30*d2r,1.0)," e=0.94:",defl_e(30*d2r,0.94))
# TP A.5 90-deg with e, mu (stun, constant mu)
def tpa5(e,mu,phi):
    Pt=min(mu*(1+e)*math.cos(phi)/2, math.sin(phi)/7)
    th2=math.atan(Pt/((1+e)*math.cos(phi)/2))/d2r
    th1=math.atan(((1-e)*math.cos(phi)/2)/(math.sin(phi)-Pt))/d2r
    return th2,th1,90-th1-th2
print("TPA5 half-ball e=.94 mu=.06",tpa5(0.94,0.06,30*d2r),"e only",tpa5(0.94,0,30*d2r),"mu only",tpa5(1,0.06,30*d2r))
# stun/rolling kinematics
for v in [0.5,1.0,2.0,3.0]:
    mu=0.2
    print("v",v,"stun: t_slide",round(2*v/(7*mu*g),4),"d_slide",round(12*v*v/(49*mu*g),4),"v_roll",round(5/7*v,4))
# draw: v=2, backspin R w = -v (full draw factor 1): d while sliding and point where CB stops translating
v=2.0; Rw=-2.0; mu=0.2
u=v-Rw; t=2*u/(7*mu*g); print("draw v=2,Rw=-2: t_slide",t,"d_slide",2*(6*v*v-5*v*Rw-Rw*Rw)/(49*mu*g),"v_final",(5*v+2*Rw)/7)
# rolling stop
for v in [0.5,1.0,2.0]:
    mur=0.01; print("roll v",v,"t_stop",v/(mur*g),"d_stop",v*v/(2*mur*g))
# spin decay with Dr Dave 5-15 rad/s^2 -> pooltool u_sp
usp=10*2/5/9*R; print("pooltool u_sp",usp,"wz decel = 5 u_sp g/(2R)",5*usp*g/(2*R))
# cue: TP A.30
def vb(vs,x,mr,eta=1.0):
    k=1+2.5*x*x
    return vs*(1+math.sqrt(1-mr*(1-eta)*(1+k/mr*0) if False else 1))  # placeholder
def vb_el(vs,x,mr): return 2*vs/(1+mr*(1+2.5*x*x))
print("cue center mr=1/3:",vb_el(1,0,1/3),"x=0.5:",vb_el(1,0.5,1/3))
print("w at x=0.5 (vs=1):",5*vb_el(1,0.5,1/3)*0.5/(2*R), "SRF=Rw/v=",2.5*0.5)
# lag time -> mu_r: L=2.54
for t in [6,7,8,9,10]:
    print("lag t",t,"mu_r",2*2.54/(g*t*t))
# 3.2 half-ball stun: CB final speed 5/7 v sin, OB final 5/7 v cos
