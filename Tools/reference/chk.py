p = r'C:/Users/Colin/Desktop/.00000/RawBreak/Docs/specs/physics-motion-and-cue.md'
lines = open(p, encoding='utf-8').read().split('\n')
BS = chr(92)


def ncols(l):
    c = 0
    for i, ch in enumerate(l):
        if ch == '|' and (i == 0 or l[i - 1] != BS):
            c += 1
    return c - 1


prev = None
bad = 0
for n, l in enumerate(lines, 1):
    if l.startswith('|'):
        c = ncols(l)
        if prev is None:
            prev = c
        elif c != prev:
            print("MISMATCH line", n, c, "vs", prev)
            bad += 1
    else:
        prev = None
print("bad", bad, "lines", len(lines))
for n, l in enumerate(lines, 1):
    if l.startswith('|') and (BS + '|') in l:
        print(n, l[:120])
