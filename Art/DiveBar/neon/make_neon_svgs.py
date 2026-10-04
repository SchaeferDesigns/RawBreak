#!/usr/bin/env python3
"""Designs of the five neon signs of The Low Bridge Tavern (venue-dive-bar 4.3, 8.1; fictional brands) -> Art/DiveBar/neon/N1..N5.svg.

  python Art/DiveBar/neon/make_neon_svgs.py

The letters are our own single-stroke neon alphabet (below, drawn in code: straight runs and elliptic arcs, cap height 1), so no
font licence is involved; the SVGs are the design source that Tools/blender/divebar/db_neon.py sweeps into 15 mm glass tubes.
SVG conventions (read by db_neon.py): units mm, y down; one <path> per stroke with only M / L commands; attributes
data-gas (ClearRed, StdBlue, RubyRed, CobaltBlue, Green, White, NoviolGold = M2-B's MI_DB_Neon_<Gas>), data-run (strokes of the
same run are one glass tube joined by black-painted crossovers behind the letters), data-state="dead" (a dead segment, steady
off: S8's "t"); the <svg> carries data-id, data-width-mm / data-height-mm (the backing). The designs read correctly from the
sign's front (db_neon.py places the front toward the street for N1 / N2: the room sees them mirrored, R8). Owner: M2-A.
"""

from __future__ import annotations

import math
from pathlib import Path

HERE = Path(__file__).resolve().parent


def arc(cx, cy, rx, ry, a0, a1, n=None):
	"""Points of an elliptic arc from angle a0 to a1 (degrees, counter-clockwise positive, y up)."""
	n = n or max(4, int(abs(a1 - a0) / 12))
	return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * k / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n + 1)]


# --- the alphabet: glyph -> (advance width, [strokes]); stroke = list of (x, y), cap height 1, x-height 0.62 --------------------
X = 0.62  # x-height


def g_caps():
	G = {}
	G["A"] = (0.72, [[(0, 0), (0.36, 1), (0.72, 0)], [(0.14, 0.4), (0.58, 0.4)]])
	G["B"] = (0.62, [[(0, 0), (0, 1), (0.36, 1)] + arc(0.36, 0.76, 0.24, 0.24, 90, -90)[1:] + [(0, 0.52)],
		[(0.02, 0.52), (0.38, 0.52)] + arc(0.38, 0.26, 0.26, 0.26, 90, -90)[1:] + [(0.0, 0.0)]])
	G["C"] = (0.70, [arc(0.42, 0.5, 0.42, 0.5, 48, 312)])
	G["D"] = (0.70, [[(0, 0), (0, 1), (0.22, 1)] + arc(0.22, 0.5, 0.48, 0.5, 90, -90)[1:] + [(0, 0)]])
	G["E"] = (0.58, [[(0.58, 1), (0, 1), (0, 0), (0.58, 0)], [(0.0, 0.52), (0.46, 0.52)]])
	G["F"] = (0.56, [[(0.56, 1), (0, 1), (0, 0)], [(0.0, 0.52), (0.44, 0.52)]])
	G["G"] = (0.86, [arc(0.42, 0.5, 0.42, 0.5, 45, 340) + [(0.82, 0.42), (0.52, 0.42)]])
	G["H"] = (0.66, [[(0, 0), (0, 1)], [(0.66, 0), (0.66, 1)], [(0, 0.52), (0.66, 0.52)]])
	G["I"] = (0.10, [[(0.05, 0), (0.05, 1)]])
	G["K"] = (0.62, [[(0, 0), (0, 1)], [(0.62, 1), (0.02, 0.40)], [(0.22, 0.60), (0.64, 0)]])
	G["L"] = (0.54, [[(0, 1), (0, 0), (0.54, 0)]])
	G["N"] = (0.68, [[(0, 0), (0, 1), (0.68, 0), (0.68, 1)]])
	G["O"] = (0.80, [arc(0.40, 0.5, 0.40, 0.5, 90, 450, 36)])
	G["P"] = (0.60, [[(0, 0), (0, 1), (0.34, 1)] + arc(0.34, 0.75, 0.25, 0.25, 90, -90)[1:] + [(0, 0.50)]])
	G["R"] = (0.62, [[(0, 0), (0, 1), (0.34, 1)] + arc(0.34, 0.75, 0.25, 0.25, 90, -90)[1:] + [(0, 0.50)], [(0.28, 0.50), (0.64, 0)]])
	G["S"] = (0.62, [arc(0.31, 0.75, 0.27, 0.25, 15, 270)[:-1] + arc(0.31, 0.25, 0.30, 0.25, 90, -165)])
	G["T"] = (0.66, [[(0, 1), (0.66, 1)], [(0.33, 1), (0.33, 0)]])
	G[" "] = (0.36, [])
	return G


def g_lower():
	G = {}
	bowl = lambda cx: arc(cx, X / 2, 0.26, X / 2, 90, 450, 28)  # noqa: E731
	G["a"] = (0.62, [bowl(0.26), [(0.56, X), (0.56, 0.0)]])
	G["d"] = (0.62, [bowl(0.26), [(0.56, 1.0), (0.56, 0.0)]])
	G["e"] = (0.56, [[(0.02, X / 2), (0.52, X / 2)] + arc(0.27, X / 2, 0.25, X / 2, 0, 320)[1:]])
	G["g"] = (0.62, [bowl(0.26), [(0.56, X), (0.56, -0.10)] + arc(0.29, -0.10, 0.27, 0.20, 0, -175)[1:]])
	G["h"] = (0.56, [[(0, 1.0), (0, 0)], [(0.0, 0.36)] + arc(0.26, 0.36, 0.26, 0.26, 180, 0)[1:] + [(0.52, 0.0)]])
	G["i"] = (0.10, [[(0.05, 0), (0.05, X)], [(0.05, 0.80), (0.05, 0.86)]])
	G["l"] = (0.12, [[(0.02, 1.0), (0.02, 0.12)] + arc(0.14, 0.12, 0.12, 0.12, 180, 270)[1:]])
	G["n"] = (0.56, [[(0, 0), (0, X)], [(0.0, 0.36)] + arc(0.26, 0.36, 0.26, 0.26, 180, 0)[1:] + [(0.52, 0.0)]])
	G["o"] = (0.56, [arc(0.27, X / 2, 0.27, X / 2, 90, 450, 28)])
	G["r"] = (0.40, [[(0, 0), (0, X)], [(0.0, 0.34)] + arc(0.26, 0.34, 0.26, 0.28, 180, 80)[1:]])
	G["s"] = (0.48, [arc(0.24, 0.47, 0.20, 0.15, 10, 270)[:-1] + arc(0.24, 0.16, 0.23, 0.16, 90, -165)])
	G["t"] = (0.36, [[(0.12, 0.92), (0.12, 0.12)] + arc(0.26, 0.12, 0.14, 0.12, 180, 280)[1:], [(0.0, X), (0.34, X)]])
	return G


FONT = {**g_caps(), **g_lower()}
TRACK = 0.12  # letter spacing (x cap heights); capitals get CAPS_TRACK
CAPS_TRACK = 0.22


def text(s: str, x0: float, y0: float, cap: float, slant: float = 0.0):
	"""Strokes of a string at (x0, y0) (baseline, metres, y up), cap height cap; slant = italic shear (x += slant * y)."""
	strokes = []
	x = x0
	letters = []
	for ch in s:
		adv, glyph = FONT[ch]
		letter = []
		for st in glyph:
			letter.append([(x + (px + slant * py) * cap, y0 + py * cap) for px, py in st])
		letters.append((ch, letter))
		strokes += letter
		x += (adv + (CAPS_TRACK if ch.isupper() else TRACK)) * cap
	return strokes, x, letters


def width(s: str, cap: float) -> float:
	return sum((FONT[c][0] + (CAPS_TRACK if c.isupper() else TRACK)) * cap for c in s) - TRACK * cap


def rounded_rect(x0, y0, x1, y1, r):
	pts = []
	for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
		pts += arc(cx, cy, r, r, a0, a0 + 90, 6)
	return pts + [pts[0]]


class Sign:
	def __init__(self, sid: str, w: float, h: float, name: str):
		self.sid, self.w, self.h, self.name = sid, w, h, name
		self.paths = []  # (points, gas, run, state)

	def add(self, strokes, gas: str, run: int, state: str = "lit"):
		for st in strokes:
			if len(st) >= 2:
				self.paths.append((st, gas, run, state))

	def svg(self) -> str:
		W, H = self.w * 1000.0, self.h * 1000.0
		out = [f'<svg xmlns="http://www.w3.org/2000/svg" data-id="{self.sid}" data-name="{self.name}" width="{W:.0f}mm" height="{H:.0f}mm" '
			f'viewBox="0 0 {W:.1f} {H:.1f}" data-width-mm="{W:.1f}" data-height-mm="{H:.1f}">',
			f'  <rect x="0" y="0" width="{W:.1f}" height="{H:.1f}" fill="#101010" data-role="backing"/>']
		colors = {"ClearRed": "#ff2a10", "StdBlue": "#3060ff", "RubyRed": "#ff1040", "CobaltBlue": "#2030ff", "Green": "#40ff60", "White": "#ffffff",
			"NoviolGold": "#ffb030"}
		for pts, gas, run, state in self.paths:
			d = " ".join(("M" if k == 0 else "L") + f"{1000 * (x + self.w / 2):.1f},{1000 * (self.h / 2 - y):.1f}" for k, (x, y) in enumerate(pts))
			stroke = "#555555" if state == "dead" else colors[gas]
			out.append(f'  <path d="{d}" fill="none" stroke="{stroke}" stroke-width="15" stroke-linecap="round" stroke-linejoin="round" '
				f'data-gas="{gas}" data-run="{run}" data-state="{state}"/>')
		out.append("</svg>")
		return "\n".join(out) + "\n"


def sign_n1() -> Sign:
	# "Old Castor" (clear red, slanted) over "LAGER" (Noviol gold) with a swash underline; 1.20 x 0.50 m (window, faces the street)
	s = Sign("N1", 1.20, 0.50, "Old Castor LAGER")
	cap = 0.165
	t = "Old Castor"
	st, _, _ = text(t, -width(t, cap) / 2 - 0.03, 0.02, cap, slant=0.18)
	s.add(st, "ClearRed", 1)
	cap2 = 0.085
	t2 = "LAGER"
	st2, _, _ = text(t2, -width(t2, cap2) / 2, -0.19, cap2)
	s.add(st2, "NoviolGold", 2)
	return s


def sign_n2() -> Sign:
	# "OPEN" (clear red) in a rounded blue border; 0.80 x 0.45 m (window, faces the street)
	s = Sign("N2", 0.80, 0.45, "OPEN")
	cap = 0.16
	t = "OPEN"
	st, _, _ = text(t, -width(t, cap) / 2, -cap / 2, cap)
	s.add(st, "ClearRed", 1)
	s.add([rounded_rect(-0.37, -0.195, 0.37, 0.195, 0.06)], "StdBlue", 2)
	return s


def sign_n3() -> Sign:
	# "HOLLENBECK" (standard blue) over "Light" (white; the "t" is dead, S8); 1.70 x 0.42 m, back-bar wall
	s = Sign("N3", 1.70, 0.42, "HOLLENBECK Light")
	cap = 0.10
	t = "HOLLENBECK"
	st, _, _ = text(t, -width(t, cap) / 2, 0.035, cap)
	s.add(st, "StdBlue", 1)
	cap2 = 0.16
	t2 = "Light"
	_, _, letters = text(t2, -width(t2, cap2) / 2 + 0.05, -0.19, cap2, slant=0.12)
	for ch, strokes in letters:
		s.add(strokes, "White", 2, "dead" if ch == "t" else "lit")
	return s


def sign_n4() -> Sign:
	# "POOL" (clear red) with a cue and a ball (Noviol gold); 1.00 x 0.45 m, right wall above the cue rack
	s = Sign("N4", 1.00, 0.45, "POOL")
	cap = 0.19
	t = "POOL"
	st, _, _ = text(t, -width(t, cap) / 2 - 0.03, -0.03, cap)
	s.add(st, "ClearRed", 1)
	s.add([[(-0.46, -0.10), (0.30, -0.145)]], "NoviolGold", 2)
	s.add([arc(0.38, -0.150, 0.055, 0.055, 90, 450, 24)], "NoviolGold", 2)
	return s


def sign_n5() -> Sign:
	# "Lantern Flats" (cobalt blue) + "IPA" (ruby red); 1.00 x 0.45 m, back wall
	s = Sign("N5", 1.00, 0.45, "Lantern Flats IPA")
	cap = 0.10
	t = "Lantern"
	st, _, _ = text(t, -0.44, 0.07, cap, slant=0.10)
	s.add(st, "CobaltBlue", 1)
	t = "Flats"
	st, _, _ = text(t, -0.44, -0.07, cap, slant=0.10)
	s.add(st, "CobaltBlue", 1)
	cap3 = 0.17
	t3 = "IPA"
	st3, _, _ = text(t3, 0.14, -0.08, cap3)
	s.add(st3, "RubyRed", 2)
	return s


LM_PER_M = {"ClearRed": 320, "StdBlue": 379, "RubyRed": 118, "CobaltBlue": 237, "Green": 1484, "White": 963, "NoviolGold": 962}  # 4.3


def main() -> None:
	import json
	summary = {}
	for sign in (sign_n1(), sign_n2(), sign_n3(), sign_n4(), sign_n5()):
		path = HERE / f"{sign.sid}.svg"
		path.write_text(sign.svg(), encoding="utf-8")
		length = {}
		for pts, gas, _, state in sign.paths:
			l = sum(math.dist(pts[k], pts[k + 1]) for k in range(len(pts) - 1))
			length[(gas, state)] = length.get((gas, state), 0.0) + l
		print(f"{sign.sid} {sign.name}: " + ", ".join(f"{g}{'(dead)' if s == 'dead' else ''} {l:.2f} m" for (g, s), l in sorted(length.items())))
		lit = {g: round(l, 3) for (g, st), l in length.items() if st == "lit"}
		flux = {g: round(LM_PER_M[g] * l, 1) for g, l in lit.items()}
		summary[sign.sid] = {"name": sign.name, "tube_length_m": lit, "tube_flux_lm": flux, "total_lm": round(sum(flux.values()), 1),
			"proxy_lm": round(sum(flux.values()) / math.pi, 1)}
	(HERE / "neon_tubes.json").write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
	main()
