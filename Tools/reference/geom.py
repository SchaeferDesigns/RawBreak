import math
IN=0.0254
D=2.25*IN; R=D/2; m=6*0.028349523125
print("R",R,"D",D,"m(6oz)",m,"I",0.4*m*R*R)
print("5.5oz",5.5*0.028349523125,"density",m/(4/3*math.pi*R**3))
Rov=2.375*IN/2
print("oversized R",Rov,"mass same density",m/(R**3)*Rov**3, "7.8oz",7.8*0.028349523125)
for pct in (0.625,0.635,0.645):
    h=pct*D; th=math.degrees(math.asin((h-R)/R)); print("nose pct",pct,"h",h,"theta",th)
h=0.635*D
print("oversize ball contact angle", math.degrees(math.asin((h-Rov)/Rov)), "h/Dov", h/(2*Rov))
print("contact angle w/ snooker? skip")
# facing back draft contact height
for b in (12,13.5,15):
    print("backdraft",b,"contact z", R*(1+math.sin(math.radians(b))))
def corner(mouth_in, cut_deg, shelf_in, rp, depth_in=2.0):
    M=mouth_in*IN; a=M/math.sqrt(2); phi=180-cut_deg; conv=45-phi
    hh=depth_in*IN
    throat=M-math.sqrt(2)*hh*(1/math.tan(math.radians(phi))-1)
    s=shelf_in*IN
    center_from_mouth=s+rp
    center_from_corner=center_from_mouth-M/2
    return dict(M=M,a=a,phi=phi,conv=conv,throat2in=throat,throat_in=throat/IN,mouth_minus_throat_in=(M-throat)/IN,shelf=s,center_from_mouth=center_from_mouth,center_beyond_corner=center_from_corner)
def side(mouth_in, cut_deg, shelf_in, rp, depth_in=2.0):
    M=mouth_in*IN; conv=cut_deg-90; hh=depth_in*IN
    throat=M-2*hh*math.tan(math.radians(conv))
    s=shelf_in*IN
    return dict(M=M,half=M/2,conv=conv,throat2in=throat,throat_in=throat/IN,shelf=s,center_beyond_nose=s+rp)
print("PRO corner", corner(4.5,142,1.625,0.062))
print("PRO side", side(5.0,104,0.1875,0.0645))
print("TIGHT corner", corner(4.25,142,1.5,0.062))
print("TIGHT side", side(4.75,104,0.125,0.0645))
print("BAR corner", corner(4.875,138,0.25,0.062))
print("BAR side", side(4.75,100,0.0,0.0645))
print("check TDF 141.7",corner(4.5,141.7,1.6,0.06)['mouth_minus_throat_in'],"142.6",corner(4.5,142.6,1.6,0.06)['mouth_minus_throat_in'])
print("pooltool 5.3deg -> cut",135+5.3, corner(118/25.4,140.3,1.6,0.06)['mouth_minus_throat_in'])
for name,Lin,Win in (("9ft",100,50),("8ft pro",92,46),("8ft home",88,44),("7ft bar",80,40),("7ft true",76,38),("7ft 78x39",78,39)):
    L=Lin*IN; W=Win*IN
    print(name,"L",L,"W",W,"L/8",L/8,"W/4",W/4,"footspot x",L/4)
def rack8(xfs):
    out=[]
    for k in range(5):
        for j in range(k+1):
            out.append((xfs+k*D*math.sqrt(3)/2,(j-k/2)*D))
    return out
print("row dx",D*math.sqrt(3)/2,"sqrt3 D",math.sqrt(3)*D, "2*rowdx", 2*D*math.sqrt(3)/2, "4 rows",4*D*math.sqrt(3)/2)
print("triangle inner side 15", D*(4+math.sqrt(3)), (4+math.sqrt(3))*2.25, "10-ball", D*(3+math.sqrt(3)),(3+math.sqrt(3))*2.25, "diamond", D*(2+4/math.sqrt(3)),(2+4/math.sqrt(3))*2.25)
L=100*IN
for p in rack8(L/4): print("%.5f %.5f"%p)
print("sight inset",(3+11/16)*IN)
print("bed height mid",(29.25+31)/2*IN, 29.25*IN, 31*IN)
print("cushion width", 1.875*IN, 2*IN)
print("K66 nose heights 1-3/8,1-13/32,1-7/16", 1.375*IN,(1+13/32)*IN,1.4375*IN, [x*IN/D for x in (1.375,1+13/32,1.4375)])
print("rail 9ft diamond", (114-100)/2*IN, "valley", (93-80)/2*IN)
print("oz to g 18..21", [x*28.349523125 for x in (18,19,20,21,25,10)])
print("inch", [ (x, x*IN) for x in (57,58,59,48,52,36,40,47,29)])
