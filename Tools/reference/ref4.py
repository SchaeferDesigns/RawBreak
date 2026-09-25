import numpy as np
from math import *
exec(open('ref.py').read().split("print(\"alpha_sp")[0])
mph=0.44704
v0=11.194*mph; th=radians(5)
v=np.array([v0*cos(th),0,-v0*sin(th)]); w=np.array([0,-34.841*2*pi,0])
wn=-v[2]; Pi=1.6*wn; vh=np.array([v[0],0,0]); u=u_of(vh,w); un=np.linalg.norm(u)
print("slip?", 2/7*un, ">", 0.2*Pi)
mag=min(0.2*Pi,2/7*un); dvh=-mag*u/un
vh2=vh+dvh; w2=w-(5/(2*R))*np.cross(Z,dvh); v2=vh2+0.6*wn*Z
print("C3 post v=%.6f,%.6f  w_y=%.4f |v|=%.6f"%(v2[0],v2[2],w2[1],np.linalg.norm(v2)))
print("flight dist m", 2*v2[0]*v2[2]/g)
oz=0.028349523125
print("M21",21*oz,"M9",9*oz,"M18",18*oz,"M20",20*oz)
print("|vf| masse:", hypot(0.065557,0.615069), hypot(0.126261,1.18461))
o=strike(2.0,0,pi/2,0.3,0.2,19*oz,0.75); print("B7",np.round(o["v"],7),np.round(o["w"],6))
o=strike(2.0,0,0,0.5,0,19*oz,0.75,mr_end=20); print("B8",np.round(o["v"],7))
o=strike(3.0,0,0,0.4,0,19*oz,0.73,mr_end=20); print("B15",np.round(o["v"],7),np.round(o["w"],6),degrees(atan2(o["v"][1],o["v"][0])))
# soft touch: which V gives 0.3 m/s CB with leather 19oz
r=(1+0.73)/(1+m/(19*oz)); print("V for 0.3:",0.3/r," V for 11:",11/1.4390715, " V for 13:",13/1.4390715," 16:",16/1.4390715)
# spin decay: mu_sp from alpha 10 (with g) and pinch J ratio
print("rho_sep e=.73 19oz", sqrt(0.4*0.73*(1+m/(19*oz))))
# energy check B.10 style: stun distance ratio etc
