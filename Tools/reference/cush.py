import numpy as np, math
R0=0.028575; m0=0.17009713875; g=9.80665; h0=0.03629025
def ec_law(v,slope=0.035): return min(0.97,max(0.60,0.97-slope*max(0.0,v-1.0)))
def rhs(y,M,R,st,ct,muw,mus,seps=1e-9):
    vx,vy,wx,wy,wz=y
    sx=vx+wy*R*st-wz*R*ct; ss=-vy*st+wx*R
    sxc=vx-wy*R; syc=vy+wx*R
    sI=math.hypot(sx,ss); sC=math.hypot(sxc,syc)
    if sI>seps: cp,sp=sx/sI,ss/sI; mw=muw
    else: cp=sp=0.0; mw=0.0
    if sC>seps: cq,sq=sxc/sC,syc/sC; ms=mus
    else: cq=sq=0.0; ms=0.0
    PC=st+mw*sp*ct
    kw=5/(2*M*R)
    return np.array([-(1/M)*(mw*cp+ms*cq*PC),
                     -(1/M)*(ct-mw*st*sp+ms*sq*PC),
                     -kw*(mw*sp+ms*sq*PC),
                     -kw*(mw*cp*st-ms*cq*PC),
                     kw*mw*cp*ct])
def rk4(y,dP,*a):
    k1=rhs(y,*a); k2=rhs(y+0.5*dP*k1,*a); k3=rhs(y+0.5*dP*k2,*a); k4=rhs(y+dP*k3,*a)
    return y+dP/6*(k1+2*k2+2*k3+k4)
def mathavan(v,w,e,M=m0,R=R0,h=h0,muw=0.14,mus=0.2,N=200):
    st=(h-R)/R; ct=math.sqrt(1-st*st); a=(M,R,st,ct,muw,mus)
    y=np.array([v[0],v[1],w[0],w[1],w[2]],float)
    dP=(1+e)*M*v[1]/N
    Wc=0.0
    # compression
    while True:
        yn=rk4(y,dP,*a)
        if yn[1]<=0:
            lo,hi=0.0,dP
            for k in range(60):
                mid=0.5*(lo+hi); ym=rk4(y,mid,*a)
                if ym[1]>0: lo=mid
                else: hi=mid
            ym=rk4(y,hi,*a); Wc+=0.5*hi*(y[1]+ym[1])*ct; y=ym; y[1]=0.0; break
        Wc+=0.5*dP*(y[1]+yn[1])*ct; y=yn
    target=e*e*Wc; Wr=0.0
    while True:
        yn=rk4(y,dP,*a)
        dW=0.5*dP*(abs(y[1])+abs(yn[1]))*ct
        if Wr+dW>=target:
            lo,hi=0.0,dP
            for k in range(60):
                mid=0.5*(lo+hi); ym=rk4(y,mid,*a)
                dWm=0.5*mid*(abs(y[1])+abs(ym[1]))*ct
                if Wr+dWm<target: lo=mid
                else: hi=mid
            y=rk4(y,hi,*a); break
        Wr+=dW; y=yn
    return np.array([y[0],y[1],0.0]),np.array([y[2],y[3],y[4]])
def rolling(vx,vy,R=R0): return np.array([vx,vy,0.]),np.array([-vy/R,vx/R,0.])
if __name__=='__main__':
    # M-1 snooker
    Ms,Rs=0.1406,0.02625
    for V in (0.5,1,2,3):
        v,w=rolling(0,V,Rs); vp,wp=mathavan(v,w,0.98,M=Ms,R=Rs,h=1.4*Rs,muw=0.14,mus=0.212)
        print('M-1',V,-vp[1]/V)
    # M-2
    V=1; a=math.radians(45); v,w=rolling(V*math.cos(a),V*math.sin(a))
    for N in (50,200,1000,20000):
        vp,wp=mathavan(v,w,0.97,N=N); print('M-2 N',N,vp,R0*wp)
    # M-3
    V=2; a=math.radians(30); v=np.array([V*math.cos(a),V*math.sin(a),0]); w=np.array([0,0,1/R0])
    for N in (200,20000):
        vp,wp=mathavan(v,w,0.97,N=N); print('M-3 N',N,vp,R0*wp)
    V=3; a=math.radians(60); v=np.array([V*math.cos(a),V*math.sin(a),0]); w=np.array([1.299038,-0.75,-1.0])/R0
    for N in (200,20000):
        vp,wp=mathavan(v,w,0.97,N=N); print('M-4 N',N,vp,R0*wp)
    for V in (0.5,1,3):
        v,w=rolling(0,V); vp,wp=mathavan(v,w,0.98); print('M-5',V,-vp[1]/V,R0*wp[0]/V)
    print('M-7',[ec_law(x) for x in (0.5,1,3,10,20)])
    # Table 4.7
    for inc in (15,30,45,60,75):
        a=math.radians(inc); v,w=rolling(math.cos(a),math.sin(a))
        e=min(0.97,0.97-0.07*max(0,v[1]-1))
        vp,wp=mathavan(v,w,e)
        print('tab',inc,round(math.hypot(vp[0],vp[1]),3),round(math.degrees(math.atan2(-vp[1],vp[0])),2))
    # e fit table 4.8 (pool geometry)
    for x,yy in ((1.0,None),(1.5,None),(2.0,None),(2.5,None),(3.0,None),(3.5,None)):
        yt=-0.0877*x*x+1.131*x-0.0953; ratio=yt/x
        lo,hi=0.3,1.0
        for k in range(40):
            mid=0.5*(lo+hi); v,w=rolling(0,x); vp,wp=mathavan(v,w,mid,N=400)
            if -vp[1]/x<ratio: lo=mid
            else: hi=mid
        print('efit',x,round(ratio,4),round(0.5*(lo+hi),3))
