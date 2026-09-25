import math
g=9.81; R=0.028575; d2r=math.pi/180
mu=0.2; mur=0.01
v=2.0
ts=2*v/(7*mu*g); ds=12*v*v/(49*mu*g); vr=5/7*v
print("stun v=2: ts=%.6f ds=%.6f vr=%.6f"%(ts,ds,vr))
print("then rolling stop: t=%.6f d=%.6f"%(vr/(mur*g), vr*vr/(2*mur*g)))
# follow: v=2, R w = +1 (half-roll topspin)
for Rw in [1.0,-1.0,-2.0,2.0,4.0]:
    u=v-Rw; t=2*abs(u)/(7*mu*g); d= v*t - 0.5*mu*g*t*t*(1 if u>0 else -1)
    print("v=2 Rw=%.1f: t=%.6f d=%.6f vfinal=%.6f"%(Rw,t,d,(5*v+2*Rw)/7))
# general 2D invariant
v0=(1.0,0.5,0.0); w0=(10.0,-20.0,5.0)
wxRz=(R*w0[1], -R*w0[0], 0.0)
vf=tuple(5/7*v0[i]+2/7*wxRz[i] for i in range(3))
u0=(v0[0]+wxRz[0]*-1*-1, 0,0)
# u = v + w x (-R z) = v - w x (R z) = v - (R wy, -R wx, 0)
u=(v0[0]-R*w0[1], v0[1]+R*w0[0], 0)
umag=math.hypot(u[0],u[1]); t=2*umag/(7*mu*g)
print("general: vf=",vf," |u0|=",umag," t_slide=",t)
# lag
L=2.54
print("lag t for mur=0.01 over L:",math.sqrt(2*L/(mur*g)), "v_start", math.sqrt(2*mur*g*L))
# cushion poly
for x in [0.3,0.5,1,1.5,2,2.5,3,3.5]:
    y=-0.0877*x*x+1.131*x-0.0953; print("cush",x,round(y,4),round(y/x,4))
# Mathavan 2014 ideal predictions (W&S): OB 5/7 V cos, CB final angle and speed
for V,th in [(1.539,33.83),(1.032,26.36),(1.364,40.52),(1.731,46.5),(0.942,18.05)]:
    t=th*d2r
    ang=math.atan(math.sin(t)*math.cos(t)/(math.sin(t)**2+0.4))/d2r
    vc=5/7*V*math.sqrt(math.sin(t)**2*(9/25*0+1)) # placeholder
    # ideal final CB velocity: 5/7 * v sin t * that + 2/7 V forward
    vx=-5/7*V*math.sin(t)*math.cos(t); vy=5/7*V*math.sin(t)**2+2/7*V
    print(V,th,"ideal CB ang %.2f speed %.3f OB %.3f"%(ang,math.hypot(vx,vy),5/7*V*math.cos(t)))
# break speeds
for mph in [17,22,24,26,30,35]: print(mph,"mph =",round(mph*0.44704,3),"m/s")
# drop bounce
h=0.05; e=0.5
hs=[h]
while hs[-1]*e*e>=0.005: hs.append(hs[-1]*e*e)
print("bounces",hs)
# total time of infinite bounce series from h: t = sqrt(2h/g)*(1+2e/(1-e))
print("zeno total time", math.sqrt(2*h/g)*(1+2*e/(1-e)))
