import math, sys
import budget as B
mr = float(sys.argv[1])
B.alsq = lambda a, m=mr: math.atan2(2.5*a*math.sqrt(1-a*a), 1+m+2.5*(1-a*a))
for a in [25, 40]:
    print(mr, a, B.budget(a))
print(mr, "P1", B.budget(25, P=1.0), "P1 settle", B.budget(25, P=1.0, settle=True))
