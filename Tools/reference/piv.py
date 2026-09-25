import math
g=9.80665; R=0.028575; rd=0.0047625; rho=R+rd
def Tp(v0,n):
    c=(10+7*v0*v0/(g*rho))/17; psi=math.acos(min(1,c))
    f=lambda p: rho/math.sqrt(v0*v0+(10/7)*g*rho*(1-math.cos(p)))
    h=psi/n; s=f(0)+f(psi)
    for i in range(1,n): s+=(4 if i%2 else 2)*f(i*h)
    return s*h/3, psi
def Tp_sub(v0,n):
    c=(10+7*v0*v0/(g*rho))/17; psi=math.acos(min(1,c))
    a=v0*v0; b=(20/7)*g*rho
    X=math.sin(psi/2); U=math.asinh(math.sqrt(b/a)*X)
    f=lambda u: 2*rho/math.sqrt(b)/math.sqrt(1-(math.sinh(u)*math.sqrt(a/b))**2)
    h=U/n; s=f(0)+f(U)
    for i in range(1,n): s+=(4 if i%2 else 2)*f(i*h)
    return s*h/3
for v0 in (1e-4,1e-3,0.01,0.05,0.1,0.3,0.5):
    t32,psi=Tp(v0,32); te=Tp_sub(v0,20000); t16=Tp_sub(v0,16); print(v0, round(math.degrees(psi),3), round(t32*1e3,3), round(te*1e3,4), 'rel err simpson32 %.2e'%((t32-te)/te), 'subst16 %.1e'%((t16-te)/te))
print('P-4 v0', math.sqrt(2*0.01*g*0.05), math.sqrt(2*0.01*9.81*0.05))
