import numpy as np, math
R=0.028575; m=0.17009713875; g=9.80665; h=0.03629025
st=(h-R)/R; ct=math.sqrt(1-st*st)
def gri(v,w,k,e,mu):
    rI=-R*k; vI=v+np.cross(w,rI); vc=-v@k
    PN=(1+e)*m*vc; s=vI-(vI@k)*k; sm=np.linalg.norm(s)
    if (2*m/7)*sm<=mu*PN: Pf=-(2*m/7)*s; br='stick'
    else: Pf=-mu*PN*s/sm; br='slide'
    P=PN*k+Pf; I=0.4*m*R*R
    return v+P/m, w+np.cross(rI,P)/I, br
kc=-np.array([0,ct,st])
def roll(vx,vy): return np.array([vx,vy,0.]),np.array([-vy/R,vx/R,0.])
v,w=roll(0,1); vp,wp,b=gri(v,w,kc,0.85,0.2); print('C-H1',vp,R*wp,b)
v=np.array([0,1.,0]); w=np.zeros(3); vp,wp,b=gri(v,w,kc,0.85,0.2); print('C-H2',vp,R*wp,b)
a=math.radians(45); v,w=roll(math.cos(a),math.sin(a)); vp,wp,b=gri(v,w,kc,0.85,0.2); print('C-H3',vp,R*wp,b)
v=np.array([0,1.,0]); vp,wp,b=gri(v,np.zeros(3),kc,0.85,0.0); print('C-H4',vp[1], 1-1.85*ct*ct, ct*ct)
k=np.array([0,-math.sqrt(R*R-0.01**2),0.01])/R; print('k',k)
vp,wp,b=gri(np.array([0,2.,0]),np.zeros(3),k,0.85,0.2); print('G-1',vp,R*wp,b,'apex',vp[2]**2/(2*g))
vp,wp,b=gri(np.array([0,2.,0]),np.array([-2/R,0,0]),k,0.85,0.2); print('G-2',vp,R*wp,b)
vp,wp,b=gri(np.array([1,0,-1.]),np.array([0,-20,0.]),np.array([0,0,1.]),0.5,0.2); print('G-3',vp,R*wp,b)
# Han table at 1 m/s rolling, e=0.93, mu=0.14
for inc in (15,30,45,60,75):
    a=math.radians(inc); v,w=roll(math.cos(a),math.sin(a)); vp,wp,b=gri(v,w,kc,0.93,0.14)
    print('han',inc,round(math.hypot(vp[0],vp[1]),3),round(math.degrees(math.atan2(-vp[1],vp[0])),2),b)
print('tan theta_c',st/ct,'theta_c deg',math.degrees(math.asin(st)),'Rc',R*ct, 'R-Rc',R-R*ct)
# oversized
Rb=0.0603/2; s2=(h-Rb)/Rb; print('oversize theta',math.degrees(math.asin(s2)),'tan',math.tan(math.asin(s2)))
