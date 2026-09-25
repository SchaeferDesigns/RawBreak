import numpy as np
from math import *
exec(open('ref.py').read().split("print(\"alpha_sp")[0])
# multi-bounce airborne test
def slate(v, w, e=0.6, mu=0.2):
    wn = -v[2]; Pi=(1+e)*wn
    vh = np.array([v[0],v[1],0.0]); u = u_of(vh,w); un=np.linalg.norm(u)
    if un>1e-12:
        mag=min(mu*Pi, 2/7*un); dvh=-mag*u/un
    else: dvh=np.zeros(3)
    vh=vh+dvh; w=w-(5/(2*R))*np.cross(Z,dvh)
    return vh+e*wn*Z, w
r=np.array([0,0,R]); v=np.array([1.0,0,1.0]); w=np.zeros(3); t=0
vzmin=sqrt(2*g*0.002); n=0
while True:
    tl=(v[2]+sqrt(v[2]**2+2*g*(r[2]-R)))/g
    r=r+v*tl-0.5*g*Z*tl*tl; v=v-g*Z*tl; t+=tl; n+=1
    r[2]=R
    v,w=slate(v,w)
    print("impact",n,"t=%.6f x=%.6f vx=%.6f vz_out=%.6f wy=%.6f"%(t,r[0],v[0],v[2],w[1]))
    if v[2]<vzmin:
        v[2]=0; break
print("final state rolling? u=",u_of(v,w))
# rolling with wz
for v0,wz in ((1.0,5.0),(0.1,20.0)):
    tr=v0/(mu_r*g); tz=wz/alpha_sp
    print("roll v0",v0,"wz",wz,"t_roll",tr,"t_wz0",tz, "wz at tr", max(0,wz-alpha_sp*tr), "stationary at", max(tr,tz))
# miscue model
def strike_general(V,theta,phi,a,b,M,e_tip,mu_tip):
    d,er,eu=cue_frame(theta,phi)
    rho=sqrt(a*a+b*b); c=sqrt(1-rho*rho)
    Q=R*(a*er+b*eu-c*d); n=-Q/R
    rho_max=mu_tip/sqrt(1+mu_tip**2)
    if rho<=rho_max: p=d.copy(); mis=False
    else:
        tt=d-np.dot(d,n)*n; tt/=np.linalg.norm(tt)
        p=(n+mu_tip*tt)/sqrt(1+mu_tip**2); mis=True
    k=np.linalg.norm(np.cross(Q/R,p)); dp=np.dot(d,p)
    J=(1+e_tip)*V*dp/((1/m)*(1+2.5*k*k)+dp*dp/M)
    return J*p/m, (J/I)*np.cross(Q,p), mis, J
oz=0.028349523125; M19=19*oz
for a in (0.5, 0.514, 0.515, 0.6, 0.7):
    v,w,mis,J=strike_general(2.0,0,0,a,0,M19,0.75,0.6)
    print("a=%.3f mis=%s v=(%.4f,%.4f) |v|=%.4f wz=%.3f dir=%.3f deg J=%.5f"%(a,mis,v[0],v[1],np.linalg.norm(v),w[2],degrees(atan2(v[1],v[0])),J))
# tip radius conversion
rt=R/3; print("axis 2/3 ->",(2/3)*R/(R+rt))
print("I=",I)
# natural roll height, break CB speed ratios
M21=21*oz
for e,M,lab in ((0.73,M19,'leather19'),(0.85,M21,'phenolic21'),(0.85,9*oz,'jump9')):
    print(lab,"vb/V center=",(1+e)/(1+m/M))
# drag distance: A.18 checks at 3mph b=-0.5 etc
mph=0.44704
for vv in (3*mph,7*mph,12*mph):
    print("stun dist d(b=0) m:",12*vv*vv/(49*0.2*g), " ft:",12*vv*vv/(49*0.2*g)/0.3048)
