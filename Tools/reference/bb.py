import numpy as np, math
R=0.028575; m=0.17009713875; g=9.80665
a_mu,b_mu,c_mu=9.951e-3,0.108,1.088
def mub(s,k=1.0): return k*(a_mu+b_mu*math.exp(-c_mu*s))
def resolve(r1,v1,w1,r2,v2,w2,e=0.95,R1=R,R2=R,m1=m,m2=m,k=1.0,verbose=False):
    I1=0.4*m1*R1**2; I2=0.4*m2*R2**2
    d=r2-r1; n=d/np.linalg.norm(d)
    vn=np.dot(v1-v2,n)
    kn=1/m1+1/m2; kt=kn+R1**2/I1+R2**2/I2
    Jn=(1+e)*vn/kn
    s=(v1-v2)+np.cross(R1*w1+R2*w2,n)
    st=s-np.dot(s,n)*n; sm=np.linalg.norm(st)
    if sm<1e-12: Jt=0; th=np.zeros(3); mu=0; br='none'
    else:
        th=st/sm; mu=mub(sm,k); Jc=mu*Jn; Js=sm/kt
        Jt=min(Jc,Js); br='slide' if Jc<Js else 'stick'
    P1=-Jn*n-Jt*th
    v1p=v1+P1/m1; v2p=v2-P1/m2
    w1p=w1+np.cross(R1*n,P1)/I1; w2p=w2+R2*np.cross(n,P1)/I2
    if verbose: print(' mu=%.7f Jn=%.7f Jt=%.7f %s'%(mu,Jn,Jt,br))
    return v1p,w1p,v2p,w2p,dict(mu=mu,Jn=Jn,Jt=Jt,br=br,n=n)
z=np.zeros(3)
def setup(v,phi,w1=z,e=0.95,k=1):
    r1=np.array([0,0,R]); r2=r1+2*R*np.array([math.cos(phi),math.sin(phi),0])
    return resolve(r1,np.array([v,0,0.]),np.array(w1,float),r2,z.copy(),z.copy(),e=e,k=k)
def throw(v2p,n):
    a=math.atan2(v2p[1],v2p[0])-math.atan2(n[1],n[0]); return math.degrees(a)
print("BB-1"); v1p,w1p,v2p,w2p,i=setup(1,0,e=0.95); print(v1p,v2p,w1p,w2p)
print("BB-2"); v1p,w1p,v2p,w2p,i=setup(1,0,[0,1/R,0]); print(i); print(v1p,v2p,R*w1p,R*w2p)
# C.3 on OB
def slate(v,w,e=0.6,mus=0.2):
    wn=-v[2]; vzp=e*wn; Pi=(1+e)*wn
    vh=np.array([v[0],v[1],0]); wh=w.copy()
    u=vh+R*np.cross([0,0,1],w); u[2]=0; um=np.linalg.norm(u)
    dv=-min(mus*Pi,2/7*um)*u/um if um>1e-9 else z
    vp=vh+dv; wp=w-(5/(2*R))*np.cross([0,0,1],dv); vp[2]=vzp
    return vp,wp
vo,wo=slate(v2p,w2p); print(' OB after C.3',vo,R*wo,'vzmin',math.sqrt(2*g*0.002)); print(' CB hop mm',v1p[2]**2/2/g*1000)
print("BB-3 / oracle")
def oracle(v,phi,wx,wz,e=1.0):
    vrel=math.sqrt((v*math.sin(phi)-R*wz)**2+(R*wx*math.cos(phi))**2)
    mu=mub(vrel)
    return math.degrees(math.atan(min(mu*v*math.cos(phi)/vrel,1/7)*(v*math.sin(phi)-R*wz)/(v*math.cos(phi))*(2/(1+e))))
for ph in (10,30,45):
    row=[]
    for v in (0.447,1.341,3.129):
        v1p,w1p,v2p,w2p,i=setup(v,math.radians(ph),e=1.0)
        row.append((round(throw(v2p,i['n']),4),round(oracle(v,math.radians(ph),0,0),4),i['br']))
    print(ph,row)
print("BB-3b e=0.95")
for ph in (10,30,45):
    row=[]
    for v in (0.447,1.341,3.129):
        v1p,w1p,v2p,w2p,i=setup(v,math.radians(ph),e=0.95)
        row.append((round(throw(v2p,i['n']),4),i['br']))
    print(ph,row)
print("BB-4"); v=1.341; v1p,w1p,v2p,w2p,i=setup(v,math.radians(30),[0,v/R,0],e=1.0); print(throw(v2p,i['n']),oracle(v,math.radians(30),v/R,0),i['br'])
print("BB-5")
for v in (0.447,1.341,3.129):
    v1p,w1p,v2p,w2p,i=setup(v,0,[0,0,0.5*v/R],e=1.0); print(v,throw(v2p,i['n']),oracle(v,0,0,0.5*v/R),R*w2p[2],i['br'])
print("BB-6"); v=1; ph=math.radians(30); v1p,w1p,v2p,w2p,i=setup(v,ph,[0,0,v*math.sin(ph)/R]); print(i['Jt'],throw(v2p,i['n']))
print("BB-7"); v1p,w1p,v2p,w2p,i=setup(0.5,0,[0,0,0.5/R]); print(R*w2p[2],R*w1p[2],i)
print("BB-8"); r1=np.array([0,0,R+0.02]); r2=r1+np.array([math.sqrt((2*R)**2-0.02**2),0,-0.02])
v1p,w1p,v2p,w2p,i=resolve(r1,np.array([2,0,-0.5]),z,r2,z,z,verbose=True); print(v1p,v2p,R*w1p,R*w2p)
print("BB-9 invariants")
rng=np.random.default_rng(1); worst=[0,0,0,0]
for it in range(20000):
    R1=rng.uniform(0.028575,0.0302); R2=rng.uniform(0.028575,0.0286); m1=rng.uniform(0.156,0.2); m2=rng.uniform(0.156,0.17)
    n=rng.normal(size=3); n/=np.linalg.norm(n)
    r1=rng.normal(size=3); r2=r1+(R1+R2)*n
    v1=rng.uniform(-10,10,3); v2=rng.uniform(-10,10,3)
    if np.dot(v1-v2,n)<=0: v1,v2=v2,v1
    if np.dot(v1-v2,n)<=0: continue
    w1=rng.uniform(-300,300,3); w2=rng.uniform(-300,300,3)
    v1p,w1p,v2p,w2p,i=resolve(r1,v1,w1,r2,v2,w2,R1=R1,R2=R2,m1=m1,m2=m2)
    I1=0.4*m1*R1**2; I2=0.4*m2*R2**2
    p0=m1*v1+m2*v2; p1=m1*v1p+m2*v2p
    L0=m1*np.cross(r1,v1)+m2*np.cross(r2,v2)+I1*w1+I2*w2
    L1=m1*np.cross(r1,v1p)+m2*np.cross(r2,v2p)+I1*w1p+I2*w2p
    E0=0.5*m1*v1@v1+0.5*m2*v2@v2+0.5*I1*w1@w1+0.5*I2*w2@w2
    E1=0.5*m1*v1p@v1p+0.5*m2*v2p@v2p+0.5*I1*w1p@w1p+0.5*I2*w2p@w2p
    worst[0]=max(worst[0],np.linalg.norm(p1-p0)/np.linalg.norm(p0))
    worst[1]=max(worst[1],np.linalg.norm(L1-L0)/max(np.linalg.norm(L0),1e-30))
    worst[2]=max(worst[2],(E1-E0)/E0)
    worst[3]=max(worst[3],abs(w1p@n-w1@n)+abs(w2p@n-w2@n))
print(worst)
