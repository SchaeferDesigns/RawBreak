import math, numpy as np
from tilt import Pursuit, g0
from chain import root
def chain_pursuit(P, cs, eps=5e-5, Tmax=2.0):
    k, G = P.k, P.g; T = P.Tstop(); t = 0.0; n = 0; mx = 0
    xtail = math.sqrt(eps*(k-G)/(4*cs))
    while t < T:
        x_i, X_i = P.state(t) if t > 0 else (P.x0, np.zeros(2)); nx = np.linalg.norm(x_i)
        xh = x_i/nx; ci = float(np.dot(xh,P.Gh)); si = abs(xh[0]*P.Gh[1]-xh[1]*P.Gh[0]); sig = si if ci >= 0 else 1
        D = T-t if (sig <= 1e-12 or nx <= xtail) else min(Tmax, T-t, root(cs,k,G,sig,eps,nx))
        tn = min(t+D, T); xn, Xn = (np.zeros(2), P.X_inf()) if tn >= T else P.state(tn)
        A2 = (Xn - X_i - x_i*D)/D**2
        for j in range(1,400):
            tau = D*j/400; _, Xe = P.state(t+tau); mx = max(mx, cs*np.linalg.norm(X_i + x_i*tau + A2*tau*tau - Xe))
        n += 1; t = tn
    return n, mx/eps
mus=0.2
P = Pursuit((2.0,0), -g0*np.array((0,3e-3)), 3.5*mus*g0); print("stun 2 m/s 3mm/m", chain_pursuit(P, 2/7))
# draw shot sliding: v0=2, w_y=-87.49 -> u0=(4.5,0)
P = Pursuit((4.5,0), -g0*np.array((0,3e-3)), 3.5*mus*g0); print("draw", chain_pursuit(P, 2/7))
P = Pursuit((2.0,0.85725), -g0*np.array((0,3e-3)), 3.5*mus*g0); print("swerve", chain_pursuit(P, 2/7))
