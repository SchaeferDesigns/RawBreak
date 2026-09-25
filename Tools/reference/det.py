import numpy as np, math
R=0.028575; m=0.17009713875; g=9.80665; mur=0.010; mus=0.2
def quartic_coefs(C1,B1,A1,C2,B2,A2,D):
    dC=C2-C1; dB=B2-B1; dA=A2-A1
    return [dA@dA, 2*dA@dB, dB@dB+2*dA@dC, 2*dB@dC, dC@dC-D*D]
def roots_in(c,lo,hi):
    r=np.roots(c) if any(abs(x)>0 for x in c[:-1]) else []
    out=[]
    for x in r:
        if abs(x.imag)<1e-7 and lo<=x.real<=hi: out.append(x.real)
    # polish
    p=np.poly1d(c); dp=p.deriv()
    res=[]
    for x in sorted(out):
        for k in range(50):
            d=dp(x)
            if d==0: break
            x-=p(x)/d
        res.append((x,dp(x)))
    return res
z=np.zeros(3); X=np.array([1.,0,0]); Z=np.array([0,0,1.])
# D-1
c=quartic_coefs(np.array([0,0,R]),X,-0.5*mur*g*X, np.array([0.5,0,R]),z,z,2*R)
print('D-1',roots_in(c,0,1/(mur*g)), 'x_A',0.5-2*R)
c=quartic_coefs(np.array([0,0,R]),X,-0.5*mur*g*X, np.array([0.5,0.05,R]),z,z,2*R); print('D-2',roots_in(c,0,1/(mur*g)))
c=quartic_coefs(np.array([0,0,R]),X,-0.5*mur*g*X, np.array([0.5,0.06,R]),z,z,2*R); print('D-3',roots_in(c,0,1/(mur*g)))
c=quartic_coefs(np.array([0,0,R]),2*X,-0.5*mus*g*X, np.array([1.0,0.02,R]),-X,0.5*mur*g*X,2*R)
print('D-4 window',4/(7*mus*g),'roots all',roots_in(c,0,100))
c=quartic_coefs(np.array([0,0,R]),np.array([2,0,1.5]),-0.5*g*Z, np.array([0.1,0,R]),z,z,2*R)
rr=roots_in(c,0,3/g); print('D-5',rr)
t=rr[0][0]; p=np.array([2*t,0,R+1.5*t-0.5*g*t*t]); d=np.array([0.1,0,R])-p; print('  n',d/np.linalg.norm(d))
c=quartic_coefs(np.array([0,0,R]),np.array([2,0,1.5]),-0.5*g*Z, np.array([0.4,0,R]),z,z,2*R); print('D-5b',roots_in(c,0,3/g), 'all', np.roots(c))
# D-7
c=quartic_coefs(np.array([0,0,R]),X,-0.5*mus*g*X, np.array([2*R,0,R]),z,z,2*R); print('D-7 f0',c[-1],"f'0",c[-2])
# D-8
yc=0.635-R*math.cos(math.asin(0.27)); a=0.5*mur*g
t=(1-math.sqrt(1-4*a*yc))/(2*a); print('D-8 y',yc,'tau',t, 'Rc',R*math.cos(math.asin((0.03629025-R)/R)))
# D-9
Rc=math.sqrt(R*R-(0.03629025-R)**2); rj=0.004; O=np.array([0.3,0.02,0])
c=quartic_coefs(np.array([0,0,0.]),X,-0.5*mur*g*X, O,z,z,rj+Rc); print('D-9',roots_in(c,0,1/(mur*g)))
print('sphere approx dist', math.sqrt((R+rj)**2-(0.03629025-R)**2), 'exact', rj+Rc)
# Hertz time
from math import gamma
cst=2*0.4*gamma(0.4)*gamma(0.5)/gamma(0.9)*(1.25)**0.4
print('T_H const',cst)
K=2270/(2*0.028575*3.49e-3)**1.5; print('K',K, 'delta',2*0.028575*3.49e-3)
ms=m/2
for v in (0.5,1,5,8,10):
    T=cst*(ms**2/(K**2*v))**0.2; dm=(5*ms*v*v/(4*K))**0.4
    print(v,'T_H us',T*1e6,'dmax um',dm*1e6,'F',K*dm**1.5,'delta_cl mm',1.2*v*T*1e3)
