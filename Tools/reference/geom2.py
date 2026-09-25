import math
IN=0.0254; D=2.25*IN; R=D/2
h=0.635*D; Rc=math.sqrt(R*R-(h-R)**2); print("h",h,"Rc",Rc,"R-Rc",R-Rc)
def table(name,Lin,Win,cm,ccut,cs,crp,sm,scut,ss,srp):
    L=Lin*IN; W=Win*IN; M=cm*IN; a=M/math.sqrt(2); phi=math.radians(180-ccut)
    print("==",name,"L=%.4f W=%.4f"%(L,W))
    PL=(L/2-a,W/2); PE=(L/2,W/2-a); Mm=((PL[0]+PE[0])/2,(PL[1]+PE[1])/2)
    s=cs*IN; C=(Mm[0]+(s+crp)/math.sqrt(2),Mm[1]+(s+crp)/math.sqrt(2))
    print(" corner(+,+): a=%.6f P_long=(%.6f,%.6f) P_end=(%.6f,%.6f) mouthmid=(%.6f,%.6f) facing_dir_long=(%.5f,%.5f) facing_dir_end=(%.5f,%.5f) capture_center=(%.6f,%.6f) r=%.4f"%(a,*PL,*PE,*Mm,math.cos(phi),math.sin(phi),math.sin(phi),math.cos(phi),*C,crp))
    # facing end points at cushion depth 2in (y=W/2+0.0508)
    cw=0.0508
    Q=(PL[0]+cw/math.tan(phi),W/2+cw); print(" facing long end at depth 50.8mm: (%.6f,%.6f)"%Q)
    Ms=sm*IN; conv=math.radians(scut-90); ssd=ss*IN
    print(" side(0,+): jaw=(+-%.6f,%.6f) facing_dir_from_+jaw=(%.5f,%.5f) capture_center=(0,%.6f) r=%.4f"%(Ms/2,W/2,-math.sin(conv),math.cos(conv),W/2+ssd+srp,srp))
    print(" long-rail nose segs x in [%.6f,%.6f] and [%.6f,%.6f]; end-rail nose seg y in [%.6f,%.6f]"%(-L/2+a,-Ms/2,Ms/2,L/2-a,-W/2+a,W/2-a))
    inset=(3+11/16)*IN
    print(" diamonds long rail x:",["%.4f"%(-L/2+i*L/8) for i in (1,2,3,5,6,7)],"y=+-%.4f"%(W/2+inset))
    print(" diamonds end rail y:",["%.4f"%(-W/2+j*W/4) for j in (1,2,3)],"x=+-%.4f"%(L/2+inset))
    print(" head spot (%.4f,0) foot spot (%.4f,0)"%(-L/4,L/4))
    # 9-ball racks
    xfs=L/4; dx=D*math.sqrt(3)/2
    counts=[1,2,3,2,1]
    for variant,xa in (("9 on spot",xfs-2*dx),("1 on spot",xfs)):
        pos=[]
        for k,n in enumerate(counts):
            for j in range(n): pos.append((round(xa+k*dx,5),round((j-(n-1)/2)*D,5)))
        print(" 9-ball",variant,pos)
    pos=[]
    for k in range(4):
        for j in range(k+1): pos.append((round(xfs+k*dx,5),round((j-k/2)*D,5)))
    print(" 10-ball",pos)
    print(" 8-ball back row x=%.5f  last-ball clearance to foot cushion (center to nose) = %.5f"%(xfs+4*dx, L/2-(xfs+4*dx)))
table("9ft PRO",100,50,4.5,142,1.625,0.062,5.0,104,0.1875,0.0645)
table("7ft BAR",80,40,4.875,138,0.25,0.062,4.75,100,0.0,0.0645)
print("diamond rack inner side", D*(2+1/math.sqrt(3)*2), (2+2/math.sqrt(3))*2.25)
