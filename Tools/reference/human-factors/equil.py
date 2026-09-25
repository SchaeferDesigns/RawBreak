import math
def run(H, nc=18, rho=0.45, V=2.0, cap=1.0, hollow=0.0, shots=200):
    cov=[cap]*7; out=[]
    sev=(0.5+0.25*V)*(0.6+2*rho*rho)
    for s in range(shots):
        n=math.ceil((cap-min(cov))/0.15 - 1e-12) if cap-min(cov) > 0 else 0
        e0=0.6; er=(0.12+0.33*H)*(1-0.7*hollow)
        for _ in range(n):
            cov[0]+= max(0,cap-cov[0])*e0
            for z in range(1,7): cov[z]+=max(0,cap-cov[z])*er
        c=cov[4]; mu=0.35+0.25*c; rmax=mu/math.sqrt(1+mu*mu)
        out.append((round(c,3), n, round(rmax,3)))
        cov[4]*=math.exp(-sev/nc)
    return out
for H in [0,1]:
    o=run(H); print('std H',H, o[-3:], min(x[0] for x in o[20:]), max(x[0] for x in o[20:]))
    o=run(H, nc=10, cap=0.7, hollow=0.8); print('bar H',H, o[-3:], min(x[0] for x in o[20:]), max(x[0] for x in o[20:]))
