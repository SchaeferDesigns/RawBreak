import math
L=2.54; W=1.27; R=0.028575
s3=math.sqrt(3)
xfs=L/4; xhs=-L/4
d=s3*R
print("sqrt3R",d, "2sqrt3R",2*d, "4sqrt3R",4*d)
print("foot spot",xfs,"head string",xhs)
# 9-ball new default: 9 on foot spot
print("9ball apex(1) x", xfs-2*d, " back ball x", xfs+2*d)
print("8ball 8 at", xfs+2*d, "back corners x", xfs+4*d, "y", 4*R)
print("10ball 10 at", xfs+2*d, "back row x", xfs+3*d, "y +-", 3*R, R)
# outline triangle
print("outline apex x", xfs-2*R, "outline back x", xfs+4*d+R, "outline back corner y", 4*R+s3*R, "side", 8*R+2*s3*R)
# spotting example
yb=0.02; hw=math.sqrt(4*R*R-yb*yb); print("hw",hw,"spot x", xfs+0.01+hw)
print("spot behind ball on foot spot", xfs+2*R)
# cue ball gap example
gap=1e-3
print("spot behind CB at foot spot", xfs+2*R+gap)
# graze angle thickness
for phi in (70,75,80):
    print(phi, 1-math.sin(math.radians(phi)))
# baulk line
print("baulk x", -L/2+L/5)
# 7ft blackball table 
# lag distance
print("head cushion nose x", -L/2)
# 14.1 rack interference: ball at x = xfs-2.9R
for k in (2.9,3.1):
    print(k, xfs-k*R)
# 15 ball rack all positions
pos=[]
for r in range(5):
    for k in range(r+1):
        pos.append((xfs+r*d,(k-r/2)*2*R))
print(pos)
print("foot cushion nose limit", L/2-R)
