import math, numpy as np, importlib.util, io, contextlib
spec = importlib.util.spec_from_file_location("cr", "cr2.py")
cr = importlib.util.module_from_spec(spec)
with contextlib.redirect_stdout(io.StringIO()):
    spec.loader.exec_module(cr)
R=cr.R; g=9.80665; h=cr.h_pool
Rc=math.sqrt(R*R-(h-R)**2)
def eC(vp): return min(0.97, max(0.6, 0.97-0.07*max(0.0, vp-1.0)))
def run(v0, mu_r=0.01, mu_s=0.2, L=2.54, e_law=eC, mu_w=0.14):
    # 1D along x; start at head spot x=-L/4, moving +x, stun (w=0)
    x=-L/4; v=v0; w=0.0   # w = w_y world (topspin for +x motion positive)
    dist=0.0; hits=0
    while True:
        # evolve until stop or cushion
        u=v-R*w
        if abs(u)>1e-9:
            a=-mu_s*g*math.copysign(1,u); wd=(5*mu_s*g/(2*R))*math.copysign(1,u)
            tend=2*abs(u)/(7*mu_s*g)
        else:
            if abs(v)<1e-9: break
            a=-mu_r*g*math.copysign(1,v); wd=a/R; tend=abs(v)/(mu_r*g)
        # time to cushion
        xc = (L/2-Rc) if v>0 else -(L/2-Rc)
        # solve x + v t + a/2 t^2 = xc
        A=0.5*a; B=v; C=x-xc
        ts=[]
        if abs(A)<1e-15: ts=[-C/B]
        else:
            D=B*B-4*A*C
            if D>=0:
                s=math.sqrt(D); ts=[(-B-s)/(2*A),(-B+s)/(2*A)]
        ts=[t for t in ts if 1e-12<t<=tend]
        if ts:
            t=min(ts)
            dist+=abs(v*t+0.5*a*t*t); x=x+v*t+0.5*a*t*t; v=v+a*t; w=w+wd*t
            # cushion: local frame Y into cushion
            sgn=1 if v>0 else -1
            vY=abs(v); wX=-sgn*w   # local w_X = w . X_hat, X_hat = Y x Z ; for +x cushion X_hat=-y -> wX=-w_y ; for -x cushion Y=-x, X=(-x)x z = +y -> wX=+w_y
            vl=np.array([0.0,vY,0.0]); wl=np.array([wX,0.0,0.0])
            vn,wn=cr.mathavan(vl,wl,h,e_law(vY),mu_w,mu_s,N=400)
            v=-sgn*(-vn[1]); w=-sgn*wn[0]
            v= -sgn*abs(vn[1])
            hits+=1
            if hits>60: break
        else:
            t=tend
            dist+=abs(v*t+0.5*a*t*t); x=x+v*t+0.5*a*t*t; v=v+a*t; w=w+wd*t
            if abs(v-R*w)<1e-9 and abs(v)<1e-6: break
            if abs(v)<1e-9: break
            # snap rolling
            u=v-R*w
            if abs(u)<1e-7: w=v/R
    return dist/L, hits
for mu_r in [0.007,0.010]:
    for v0 in [2.0,2.5,3.0,3.13,3.5,4.0,4.5]:
        n,hits=run(v0,mu_r=mu_r)
        print(f"mu_r={mu_r} v0={v0}: lengths={n:.2f} cushion hits={hits}")
print("---- wider speed range ----")
def eConst(vp): return 0.97
def eSoft(vp): return min(0.97, max(0.6, 0.97-0.035*max(0.0, vp-1.0)))
for name,law in [("e_law(0.07)",eC),("e_soft(0.035)",eSoft),("e_const",eConst)]:
    for mu_r in [0.007,0.010]:
        row=[]
        for v0 in [4.0,5.0,6.0,7.0,8.0]:
            n,hits=run(v0,mu_r=mu_r,e_law=law)
            row.append(f"{v0}:{n:.2f}")
        print(name, "mu_r",mu_r, "  ".join(row))
