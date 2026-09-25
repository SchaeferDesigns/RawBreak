import numpy as np, math
R=0.028575; m=0.17009713875; K=2270/(2*R*3.49e-3)**1.5; I=0.4*m*R*R
def mub(s): return 9.951e-3+0.108*math.exp(-1.088*s)
def run(v,phi_deg,alpha=0.03689,dt=1e-6):
    phi=math.radians(phi_deg)
    n0=np.array([math.cos(phi),math.sin(phi),0])
    x=[np.array([0,0,R]),np.array([0,0,R])+2*R*n0]
    vv=[np.array([v,0,0.]),np.zeros(3)]; w=[np.zeros(3),np.zeros(3)]
    eta=alpha*math.sqrt(m/2*K); mu0=None; started=False; quiet=0
    def force(x,vv,w):
        nonlocal mu0
        d=x[1]-x[0]; dist=np.linalg.norm(d); n=d/dist; delta=2*R-dist
        if delta<=0: return None
        rel=vv[0]-vv[1]; dd=rel@n
        Fn=max(0.0,K*delta**1.5+eta*delta**0.25*dd)
        s=rel+np.cross(R*w[0]+R*w[1],n); st=s-(s@n)*n; sm=np.linalg.norm(st)
        if mu0 is None: mu0=mub(sm)
        Ft=-mu0*Fn*st/max(sm,1e-3)
        F1=-Fn*n+Ft   # force on ball 1
        T1=np.cross(R*n,F1); T2=np.cross(-R*n,-F1)
        return F1,T1,T2
    t=0
    while t<0.01:
        f=force(x,vv,w)
        if f is None:
            if started: break
            F1=np.zeros(3);T1=np.zeros(3);T2=np.zeros(3)
        else:
            started=True; F1,T1,T2=f
        # semi-implicit Euler
        vv=[vv[0]+dt*F1/m, vv[1]-dt*F1/m]; w=[w[0]+dt*T1/I, w[1]+dt*T2/I]
        x=[x[0]+dt*vv[0], x[1]+dt*vv[1]]
        t+=dt
    v2=vv[1]; return math.degrees(math.atan2(v2[1],v2[0])-phi)
for v in (0.447,1.341,3.129):
    print(v, run(v,30))
