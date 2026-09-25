import math, numpy as np, sys
sys.path.insert(0,r"C:/Users/Colin/AppData/Local/Temp/claude/C--Users-Colin-Desktop--00000-RawBreak/5b3aed06-5b65-44e3-a364-54bee81bf06a/scratchpad")
from cush import mathavan, R0 as R, m0, h0
g=9.80665
Rc=R*math.sqrt(1-((h0-R)/R)**2)
def run(v0,slope,mur,mus=0.2,L=2.54,rc=Rc):
    x=-L/4; s=v0; T=0.0; dirn=1; dist=0.0
    ec=lambda v: min(0.97,max(0.6,0.97-slope*max(0,v-1)))
    while True:
        wall=L/2-rc
        dwall=wall-dirn*x  # distance to rail ahead
        # sliding phase
        if abs(s-T)>1e-12:
            u=s-T; a_s=-mus*g*math.copysign(1,u); a_T=2.5*mus*g*math.copysign(1,u)
            ts=abs(u)/(3.5*mus*g)
            # does s hit zero first? (backspin case)
            d_s=s*ts+0.5*a_s*ts*ts
            if d_s>=dwall:
                # hits rail while sliding
                tt=(-s+math.sqrt(s*s+2*a_s*dwall))/a_s if a_s!=0 else dwall/s
                s_i=s+a_s*tt; T_i=T+a_T*tt; dist+=dwall; x=dirn*wall
            else:
                if s+a_s*ts<0:
                    raise Exception('reverse')
                s=s+a_s*ts; T=s; dist+=d_s; x+=dirn*d_s; continue
        else:
            dr=s*s/(2*mur*g)
            if dr<dwall: dist+=dr; break
            s_i=math.sqrt(s*s-2*mur*g*dwall); T_i=s_i; dist+=dwall; x=dirn*wall
        # cushion
        vp,wp=mathavan(np.array([0,s_i,0.]),np.array([-T_i/R,0,0.]),ec(s_i),mus=mus,N=200)
        s=-vp[1]; T=R*wp[0]; dirn=-dirn
        if s<1e-4: break
    return dist/L
for slope in (0.07,0.035,0.0):
    for mur in (0.007,0.010):
        print(slope,mur,[round(run(v0,slope,mur),2) for v0 in (4,5,6,7,8)])
