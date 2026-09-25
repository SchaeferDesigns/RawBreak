import math, numpy as np
R=0.028575; m=0.170097; I=0.4*m*R*R
K=2270.0/(2*1.125*0.0254*3.49e-3)**1.5
A_,B_,C_=9.951e-3,0.108,1.088
def mu(v): return A_+B_*math.exp(-C_*v)
def cci_pair(v, phi_deg, wz=0.0, alpha=0.03689, sreg=1e-3, dt=2e-8, mu_mode="initial"):
    phi=math.radians(phi_deg)
    n0=np.array([math.cos(phi),math.sin(phi),0.0])
    r1=np.array([0,0,0.0]); r2=r1+2*R*n0
    v1=np.array([v,0,0.0]); v2=np.zeros(3); w1=np.array([0,0,wz]); w2=np.zeros(3)
    meff=m/2; eta=alpha*math.sqrt(meff*K)
    mu0=None; started=False; t=0
    while t<0.01:
        d=r2-r1; dist=np.linalg.norm(d); n=d/dist
        delta=2*R-dist
        if delta>0:
            started=True
            ddot=np.dot(v1-v2,n)
            Fn=max(0.0,K*delta**1.5+eta*delta**0.25*ddot)
            s=(v1-v2)+R*np.cross(w1+w2,n); st=s-np.dot(s,n)*n; sm=np.linalg.norm(st)
            if mu0 is None: mu0=mu(sm)
            mu_=mu0 if mu_mode=="initial" else mu(sm)
            Ft=-mu_*Fn*st/max(sm,sreg)
            F1=-Fn*n+Ft
        else:
            F1=np.zeros(3)
            if started: break
        # semi-implicit Euler
        v1=v1+F1/m*dt; v2=v2-F1/m*dt
        tq=np.cross(R*n,F1)
        w1=w1+tq/I*dt; w2=w2+tq/I*dt
        r1=r1+v1*dt; r2=r2+v2*dt; t+=dt
    ang=math.degrees(math.atan2(np.cross(n0,v2)[2],np.dot(n0,v2)))
    return ang, v1, v2, R*w1, R*w2, t
for v,phi in [(0.447,30),(1.341,30),(3.129,30),(1.341,10)]:
    a,v1,v2,rw1,rw2,t=cci_pair(v,phi)
    print(f"CCI v={v} phi={phi}: throw={a:.3f} deg, v2={v2}, Rw2={rw2}, T={t*1e6:.0f}us")
