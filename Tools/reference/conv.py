import math, numpy as np, importlib.util, io, contextlib, time
spec = importlib.util.spec_from_file_location("cr", "collide_ref.py")
cr = importlib.util.module_from_spec(spec)
with contextlib.redirect_stdout(io.StringIO()):
    spec.loader.exec_module(cr)
R=cr.R; h=cr.h_pool
cases=[("rolling 45 V1", 45, 1.0, None), ("stun 30 V2 RwZ=+1", 30, 2.0, np.array([0,0,1.0/R])), ("draw 60 V3 Rw=-0.5 back + side -1", 60, 3.0, "draw")]
for name,a,V,wspec in cases:
    al=math.radians(a); vl=np.array([V*math.cos(al),V*math.sin(al),0.])
    if wspec is None: wl=cr.rolling_w(vl[0],vl[1])
    elif isinstance(wspec,str): wl=-0.5*cr.rolling_w(vl[0],vl[1])+np.array([0,0,-1.0/R])
    else: wl=wspec
    ref=cr.mathavan(vl,wl,h,0.97,0.14,0.2,N=20000)
    print(name, "ref v'",ref[0],"R*w'",R*ref[1])
    for N in [50,100,200,400,1000]:
        out=cr.mathavan(vl,wl,h,0.97,0.14,0.2,N=N)
        err=max(np.max(np.abs(out[0]-ref[0])), R*np.max(np.abs(out[1]-ref[1])))
        oute=cr.mathavan(vl,wl,h,0.97,0.14,0.2,N=N,rk4=False)
        erre=max(np.max(np.abs(oute[0]-ref[0])), R*np.max(np.abs(oute[1]-ref[1])))
        print(f"   N={N}: RK4 max err {err:.2e} m/s   Euler max err {erre:.2e} m/s")
