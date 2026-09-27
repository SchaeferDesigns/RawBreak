"""Rounds every number of an rbsim JSON result to 6 significant digits (10 um / 10 us resolution for the viewer) and writes it
compactly. rbsim prints 17 significant digits for bitwise replays; an example meant for the viewer does not need them.

Usage: python round_json.py in.json out.json
"""
import json
import sys


def rounded(x):
    if isinstance(x, float):
        if x != x or x in (float('inf'), float('-inf')):
            return x
        return float('%.6g' % x)
    if isinstance(x, list):
        return [rounded(v) for v in x]
    if isinstance(x, dict):
        return {k: rounded(v) for k, v in x.items()}
    return x


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, encoding='utf-8') as f:
        data = json.load(f)
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(rounded(data), f, separators=(',', ':'), allow_nan=False)
        f.write('\n')


if __name__ == '__main__':
    main()
