import re, sys
lines=open(sys.argv[1],encoding="utf-8").read().split("\n")
for i,l in enumerate(lines,1):
    if l.startswith("|"):
        for m in re.finditer(r"`([^`]*)`", l):
            s=m.group(1)
            for j,ch in enumerate(s):
                if ch=="|" and (j==0 or s[j-1]!="\\"):
                    print(i, s); break
