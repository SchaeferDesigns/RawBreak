import sys, stroke as ST
import budget3 as B3
gp = float(sys.argv[1]); ST.DEFAULT["drift_gpow"] = gp
print(gp, [(mr, round(B3.run(mr, P=0), 2), round(B3.run(mr), 2), round(B3.run(mr, settle=True), 2)) for mr in (15, 20, 40)])
