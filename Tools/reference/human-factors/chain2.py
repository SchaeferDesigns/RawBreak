import math, numpy as np
from tilt import Pursuit, g0, roll_case
from chain import root
def relvel(v0, s, eps=5e-5, mur=0.01, cs=1.0):
    P, gt = roll_case(v0, s, mur); k, G = P.k, P.g; T = P.Tstop(); t = 0.0
    xtail = math.sqrt(eps * (k - G) / (4 * cs)); worst = []
    while t < T:
        x_i, X_i = P.state(t) if t > 0 else (P.x0, np.zeros(2)); nx = np.linalg.norm(x_i)
        xh = x_i / nx; ci = float(np.dot(xh, P.Gh)); si = abs(xh[0]*P.Gh[1]-xh[1]*P.Gh[0]); sig = si if ci >= 0 else 1
        D = T - t if (sig <= 1e-12 or nx <= xtail) else min(2.0, T - t, root(cs, k, G, sig, eps, nx))
        tn = min(t + D, T)
        xn, Xn = (np.zeros(2), P.X_inf()) if tn >= T else P.state(tn)
        A2 = (Xn - X_i - x_i * D) / D**2
        w = 0; wa = 0
        for j in range(1, 200):
            tau = D*j/200; xe, _ = P.state(t+tau); vq = x_i + 2*A2*tau
            if np.linalg.norm(xe) > 1e-9:
                err = np.linalg.norm(vq-xe); w = max(w, err/np.linalg.norm(xe))
                ang = math.degrees(math.acos(max(-1,min(1,np.dot(vq,xe)/np.linalg.norm(vq)/np.linalg.norm(xe)))))
                wa = max(wa, ang)
        worst.append((round(t,3), round(nx,4), round(w,4), round(wa,3)))
        t = tn
    return worst
for row in relvel((0.5,0),(0,1e-3)): print(row)
print("eps 5e-4")
for row in relvel((0.5,0),(0,1e-3),eps=5e-4): print(row)
