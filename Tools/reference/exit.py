import math, numpy as np
R=0.028575; m=0.17009713875; K=2270/(2*R*3.49e-3)**1.5; D=2*R
def two(v0,alpha=0.03689,dt=1e-6):
    x=np.array([0.,D]); v=np.array([v0,0.]); eta=alpha*math.sqrt(m/2*K)
    started=False; quiet=0; t=0
    while t<0.01:
        d=D-(x[1]-x[0]); f=0.0
        if d>0: f=max(0.0,K*d**1.5+eta*d**0.25*(v[0]-v[1]))
        if f>0: started=True; quiet=0
        elif started: quiet+=1
        if started and quiet>5: return d
        v=v+dt*np.array([-f,f])/m; x=x+dt*v; t+=dt
for v0 in (0.05,0.1,0.3,1,3,8):
    print(v0,'overlap at exit (um)',two(v0)*1e6)
