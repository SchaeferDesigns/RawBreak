import numpy as np, math
R=0.028575; m=0.17009713875; K=2270/(2*R*3.49e-3)**1.5; D=2*R
def chain(nb,v0,alpha,gaps=None,dt=1e-6,tmax=0.02):
    x=np.array([i*D for i in range(nb)],float)
    if gaps is not None:
        for i,gp in enumerate(gaps): x[i+1:]+=gp
    v=np.zeros(nb); v[0]=v0
    eta=alpha*math.sqrt(m/2*K)
    def forces(x,v):
        F=np.zeros(nb); active=False
        for i in range(nb-1):
            d=D-(x[i+1]-x[i])
            if d>0:
                dd=v[i]-v[i+1]
                f=max(0.0,K*d**1.5+eta*d**0.25*dd)
                if f>0: active=True
                F[i]-=f; F[i+1]+=f
        return F,active
    # velocity Verlet (velocity-dependent force: use v at half-step predictor)
    F,_=forces(x,v); t=0; quiet=0; started=False
    while t<tmax:
        vh=v+0.5*dt*F/m
        x=x+dt*vh
        F,act=forces(x,vh)   # use half-step velocity for damping
        v=vh+0.5*dt*F/m
        t+=dt
        if act: started=True; quiet=0
        elif started:
            quiet+=1
            if quiet>5 and all(v[i]<=v[i+1] for i in range(nb-1)): break
    return v
def e_of(alpha,v0=1.0,dt=1e-6):
    v=chain(2,v0,alpha,dt=dt); return (v[1]-v[0])/v0
lo,hi=0.0,0.2
for it in range(50):
    mid=0.5*(lo+hi)
    if e_of(mid)>0.95: lo=mid
    else: hi=mid
a95=0.5*(lo+hi); print('alpha(0.95)',a95)
lo,hi=0.0,0.2
for it in range(50):
    mid=0.5*(lo+hi)
    if e_of(mid)>0.93: lo=mid
    else: hi=mid
print('alpha(0.93)',0.5*(lo+hi))
aT=0.03689
for v0 in (0.3,1,5,8): print('e at',v0,e_of(aT,v0))
print('e dt 5us',e_of(aT,1,5e-6))
print('CL-1',chain(2,1,aT))
print('CL-2',chain(3,1,0.0))
print('CL-3',chain(3,1,aT))
v=chain(5,1,aT); print('CL-4',v,'p',v.sum(),'KE',(v**2).sum())
print('CL-5 100um',chain(3,1,aT,gaps=[0,100e-6]))
print('CL-5 10um',chain(3,1,aT,gaps=[0,10e-6]))
