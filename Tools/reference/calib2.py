import math, numpy as np, importlib.util, sys, io, contextlib
spec = importlib.util.spec_from_file_location("cr", "collide_ref.py")
cr = importlib.util.module_from_spec(spec)
with contextlib.redirect_stdout(io.StringIO()):
    spec.loader.exec_module(cr)
R=cr.R; h=cr.h_pool
def eM(v): return min(0.97, max(0.6, 0.97-0.07*max(0.0, v-1.0)))
print("Mathavan e_c(v) with pool geometry, rolling perpendicular ratios:")
for V0 in [0.5,1.0,2.0,3.0,4.0,6.0]:
    vn,wn = cr.mathavan(np.array([0,V0,0.]), cr.rolling_w(0,V0), h, eM(V0), 0.14, 0.2, N=1000)
    tgt = (-0.0877*V0**2+1.131*V0-0.0953)/V0
    # Han e matching same ratio with mu=0.14
    lo,hi=0,1
    for _ in range(50):
        mid=(lo+hi)/2
        vh,wh,_=cr.han(np.array([0,V0,0.]), cr.rolling_w(0,V0), h, mid, 0.14)
        if -vh[1]/V0 < -vn[1]/V0: lo=mid
        else: hi=mid
    print(f"  V0={V0}: e_M={eM(V0):.4f} ratio={-vn[1]/V0:.4f} (snooker-data target {tgt:.4f})  Han e equiv (mu=.14)={mid:.4f}")
print("Han(mu=.14, e matched) vs Mathavan angles, rolling, V0=1:")
for a in [15,30,45,60,75]:
    al=math.radians(a); vl=np.array([math.cos(al),math.sin(al),0.]); wl=cr.rolling_w(vl[0],vl[1])
    vn,wn=cr.mathavan(vl,wl,h,eM(math.sin(al)),0.14,0.2,N=1000)
    lo,hi=0,1
    V0=math.sin(al)
    for _ in range(50):
        mid=(lo+hi)/2
        vh,wh,_=cr.han(np.array([0,1.0,0.]), cr.rolling_w(0,1.0), h, mid, 0.14)
        vm,wm=cr.mathavan(np.array([0,1.0,0.]), cr.rolling_w(0,1.0), h, eM(1.0),0.14,0.2,N=400) if False else (None,None)
        break
    vh,wh,_=cr.han(vl,wl,h,0.93,0.14)
    print(f"  alpha={a}: Mathavan spd/ang={cr.rebound(vn,wn)}  R*w'={R*wn} | Han(e=.93,mu=.14) spd/ang={cr.rebound(vh,wh)} vz'={vh[2]:.4f}")
