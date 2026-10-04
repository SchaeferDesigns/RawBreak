"""Neon signs N1-N5 of The Low Bridge Tavern from our SVG designs (venue-dive-bar 4.3, 5.2 M07, R8; Docs/ue-architecture.md 18.8
M2-A: neon tubes are a lighting element). Runs INSIDE Blender:

  python Tools/blender/rbbl.py run Tools/blender/divebar/db_neon.py

Reads Art/DiveBar/neon/N<k>.svg (Art/DiveBar/neon/make_neon_svgs.py: M / L paths with data-gas / data-run / data-state) and the
placement of each sign from Art/DiveBar/layout.json "neon" (centre of the tube plane, reading direction, front direction, mount).
Builds per sign, in VENUE coordinates (placed at the origin by rb_make_divebar.py, out of Lumen GI - the proxy rect light of
lights.json carries the light, 4.1):
  * 15 mm glass tubes swept along every stroke (10-sided, corners filleted to the 20 mm minimum bend radius of real neon), slot
    MI_DB_Neon_<Gas> (M2-B's emissive instances, cd/m^2 of 4.3); a dead segment (S8: HOLLENBECK Light's "t") in MI_DB_Neon_Dead;
  * crossovers between the strokes of one run: the tube dips 30 mm back behind the letters and is painted black (block-out, R8);
  * electrodes at both ends of every run (the tube bends back into a 22 mm black electrode boot), tube supports every ~0.25 m,
    GTO wire from each electrode to the transformer box, a black metal frame; window signs hang on two chains from the clear pane's
    head, wall signs stand off the wall on the frame.
Also writes Art/DiveBar/Export/Neon/<Asset>/<Asset>.json with the tube lengths and flux per gas (lm/m of 4.3) - the neon proxy
flux of lights.json is flux / pi (VDB-T11). Deterministic. Owner: M2-A.
"""

from __future__ import annotations

import math
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "common"))
import rb_bl  # noqa: E402
from mathutils import Vector  # noqa: E402

PACKAGE = "M2-A"
FOLDER = "Neon"
TUBE_R = 0.0075
BEND_R = 0.020
SEGS = 10
LM_PER_M = {"ClearRed": 320, "StdBlue": 379, "RubyRed": 118, "CobaltBlue": 237, "Green": 1484, "White": 963, "NoviolGold": 962}
M_BLOCKOUT = "MI_DB_Paint_Cabinet"
M_BOOT = "MI_DB_Plastic_Black"
M_FRAME = "MI_DB_Steel_Black"
M_WIRE = "MI_DB_Rubber_Black"
M_CHAIN = "MI_DB_Steel_Zinc"
M_DEAD = "MI_DB_Neon_Dead"


def parse_svg(path: pathlib.Path) -> dict:
	s = path.read_text(encoding="utf-8")
	w = float(re.search(r'data-width-mm="([\d.]+)"', s).group(1)) / 1000.0
	h = float(re.search(r'data-height-mm="([\d.]+)"', s).group(1)) / 1000.0
	paths = []
	for m in re.finditer(r"<path\s([^>]*)/>", s):
		attrs = dict(re.findall(r'([\w-]+)="([^"]*)"', m.group(1)))
		pts = []
		for cmd in re.findall(r"[ML]\s*[-\d.]+\s*,\s*[-\d.]+", attrs["d"]):
			x, y = (float(v) for v in cmd[1:].split(","))
			pts.append((x / 1000.0 - w / 2, h / 2 - y / 1000.0))
		paths.append({"pts": pts, "gas": attrs.get("data-gas", "ClearRed"), "run": int(attrs.get("data-run", "0")),
			"state": attrs.get("data-state", "lit")})
	return {"w": w, "h": h, "paths": paths}


def fillet(pts, r: float):
	"""Rounds polyline corners sharper than 25 deg with arcs of radius r (the minimum bend radius of neon tubing)."""
	if len(pts) < 3:
		return pts
	out = [pts[0]]
	for k in range(1, len(pts) - 1):
		p0, p1, p2 = Vector(pts[k - 1]), Vector(pts[k]), Vector(pts[k + 1])
		a, b = (p0 - p1), (p2 - p1)
		if a.length < 1e-6 or b.length < 1e-6:
			continue
		a.normalize()
		b.normalize()
		cosang = max(-1.0, min(1.0, a.dot(b)))
		ang = math.acos(cosang)  # interior angle
		turn = math.pi - ang
		if turn < math.radians(25.0):
			out.append(pts[k])
			continue
		t = r / math.tan(ang / 2.0)
		t = min(t, 0.45 * (Vector(pts[k - 1]) - p1).length, 0.45 * (Vector(pts[k + 1]) - p1).length)
		rr = t * math.tan(ang / 2.0)
		s0 = p1 + a * t
		s1 = p1 + b * t
		bis = (a + b)
		if bis.length < 1e-9:
			out.append(pts[k])
			continue
		bis.normalize()
		c = p1 + bis * (rr / math.sin(ang / 2.0))
		v0, v1 = s0 - c, s1 - c
		a0 = math.atan2(v0.y, v0.x)
		a1 = math.atan2(v1.y, v1.x)
		da = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
		n = max(2, int(abs(da) / math.radians(15)))
		for i in range(n + 1):
			aa = a0 + da * i / n
			out.append((c.x + rr * math.cos(aa), c.y + rr * math.sin(aa)))
	out.append(pts[-1])
	return out


class Frame:
	"""Sign-local (u right, v up, n front, metres) -> venue coordinates."""

	def __init__(self, sign: dict):
		self.c = Vector(sign["center"])
		self.U = Vector(sign["right"]).normalized()
		self.N = Vector(sign["front"]).normalized()
		self.V = Vector((0.0, 0.0, 1.0))

	def p(self, u: float, v: float, n: float = 0.0) -> Vector:
		return self.c + self.U * u + self.V * v + self.N * n


def tube(b: rb_bl.Builder, pts3, radius: float, mat: str, ref: Vector, caps: bool = True) -> None:
	"""A tube along a 3D polyline; ring frames from the tangent and the fixed reference direction `ref` (the sign normal), with a
	parallel-transport fallback where the tangent turns toward it."""
	pts3 = [Vector(p) for p in pts3]
	clean = [pts3[0]]
	for p in pts3[1:]:
		if (p - clean[-1]).length > 1e-5:
			clean.append(p)
	pts3 = clean
	if len(pts3) < 2:
		return
	rings = []
	prev_n = None
	for k, p in enumerate(pts3):
		if k == 0:
			t = pts3[1] - pts3[0]
		elif k == len(pts3) - 1:
			t = pts3[-1] - pts3[-2]
		else:
			t = (pts3[k + 1] - pts3[k]).normalized() + (pts3[k] - pts3[k - 1]).normalized()
			if t.length < 1e-9:
				t = pts3[k + 1] - pts3[k]
		t.normalize()
		n = ref - t * ref.dot(t)
		if n.length < 1e-3:
			n = prev_n if prev_n is not None else (Vector((0, 0, 1)) - t * t.z)
		n.normalize()
		if prev_n is not None and n.dot(prev_n) < 0:
			n = -n
		prev_n = n
		bb = t.cross(n)
		ring = [p + (n * math.cos(2 * math.pi * i / SEGS) + bb * math.sin(2 * math.pi * i / SEGS)) * radius for i in range(SEGS)]
		rings.append(ring)
	for k in range(len(rings) - 1):
		r0, r1 = rings[k], rings[k + 1]
		for i in range(SEGS):
			j = (i + 1) % SEGS
			b.poly([tuple(r0[i]), tuple(r0[j]), tuple(r1[j]), tuple(r1[i])], mat)
	if caps:
		b.poly([tuple(v) for v in reversed(rings[0])], mat)
		b.poly([tuple(v) for v in rings[-1]], mat)


def length(pts) -> float:
	return sum((Vector(pts[k + 1]) - Vector(pts[k])).length for k in range(len(pts) - 1))


def wall_offset(sign: dict, F: "Frame") -> float:
	"""n (along the sign's front) of its wall plane: the wall coordinate lies on the dominant axis of the front direction."""
	axis = max(range(3), key=lambda i: abs(F.N[i]))
	return -(sign["center"][axis] - sign["wall"]) * F.N[axis]


def build_sign(sign: dict, design: dict, out) -> None:
	rb_bl.reset_scene()
	F = Frame(sign)
	b = rb_bl.Builder(sign["asset"])
	# n of the backing plane: window signs 30 mm behind the tubes; wall signs: the frame 12 mm in front of the wall plane
	back = -0.030
	wall_n = None
	if sign["mount"] == "wall":
		wall_n = wall_offset(sign, F)
		back = wall_n + 0.012
	lengths = {}
	flux = {}
	runs = {}
	for pth in design["paths"]:
		runs.setdefault((pth["run"], pth["gas"]), []).append(pth)
	electrodes = []
	supports = 0
	for (run, gas), strokes in sorted(runs.items()):
		prev_end = None
		for s in strokes:
			pts2 = fillet(s["pts"], BEND_R)
			mat = M_DEAD if s["state"] == "dead" else f"MI_DB_Neon_{gas}"
			pts3 = [F.p(u, v) for u, v in pts2]
			tube(b, pts3, TUBE_R, mat, F.N, caps=False)
			L = length(pts3)
			if s["state"] != "dead":
				lengths[gas] = lengths.get(gas, 0.0) + L
			# the run continues: crossover from the previous stroke's end (black-painted glass behind the letters)
			if prev_end is not None and (prev_end - pts3[0]).length < 0.35:
				d = -0.030
				cross = [prev_end, prev_end + F.N * (d * 0.5), prev_end + F.N * d, pts3[0] + F.N * d, pts3[0] + F.N * (d * 0.5), pts3[0]]
				tube(b, cross, TUBE_R, M_BLOCKOUT, (F.U if abs(F.U.dot((pts3[0] - prev_end).normalized())) < 0.9 else F.V), caps=False)
			else:
				if prev_end is not None:
					electrodes.append(prev_end)
				electrodes.append(pts3[0])
			prev_end = pts3[-1]
			# tube supports every ~0.25 m (clear PK-style post to the backing)
			acc = 0.12
			for k in range(len(pts3) - 1):
				seg = (pts3[k + 1] - pts3[k]).length
				acc += seg
				if acc > 0.25:
					acc = 0.0
					q = pts3[k]
					tube(b, [q - F.N * 0.008, q + F.N * (back + 0.005)], 0.0025, M_FRAME, F.U)
					supports += 1
		if prev_end is not None:
			electrodes.append(prev_end)
	# electrodes: a short leg back, the black boot along -N
	for e in electrodes:
		tube(b, [e, e - F.N * 0.012], TUBE_R, M_BOOT, F.U)
		boot0 = e - F.N * 0.010
		tube(b, [boot0, boot0 + F.N * min(-0.012, back + 0.004)], 0.011, M_BOOT, F.U)
	# frame: a rectangle of 12 mm square tube on the backing plane (+ a middle rail), inset 20 mm from the sign's extent
	w, h = design["w"], design["h"]
	fr = [(-w / 2 + 0.02, -h / 2 + 0.02), (w / 2 - 0.02, -h / 2 + 0.02), (w / 2 - 0.02, h / 2 - 0.02), (-w / 2 + 0.02, h / 2 - 0.02),
		(-w / 2 + 0.02, -h / 2 + 0.02)]
	tube(b, [F.p(u, v, back) for u, v in fr], 0.006, M_FRAME, F.N)
	tube(b, [F.p(-w / 2 + 0.02, 0.0, back), F.p(w / 2 - 0.02, 0.0, back)], 0.005, M_FRAME, F.N)
	# transformer (sign transformer 0.25 x 0.10 x 0.08) at the bottom middle behind the frame, GTO wire to every electrode
	# window signs: behind the frame (room side); wall signs: on the wall just below the sign
	tc = F.p(w / 2 - 0.14, -h / 2 + 0.05, back - 0.04) if wall_n is None else F.p(0.0, -h / 2 - 0.07, wall_n + 0.041)
	start = b.face_count()
	b.box(-0.08, 0.08, -0.035, 0.035, -0.04, 0.04, M_FRAME)
	# orient the box: local x = U, y = N, z = V
	b.bm.faces.ensure_lookup_table()
	verts = set()
	for f in b.bm.faces[start:]:
		verts.update(f.verts)
	for vtx in verts:
		lx, ly, lz = vtx.co.x, vtx.co.y, vtx.co.z
		vtx.co = tc + F.U * lx + F.N * ly + F.V * lz
	for e in electrodes:
		boot_end = e + F.N * (back - (e - F.c).dot(F.N) - 0.004)
		end = tc + F.U * (0.10 if (e - F.c).dot(F.U) > 0 else -0.10)
		corner = boot_end + F.V * (end - boot_end).dot(F.V)
		tube(b, [boot_end, corner, end], 0.0025, M_WIRE, F.N)
	# mounting: window signs hang on two chains from the clear pane's head; wall signs: 4 standoff brackets to the wall
	if sign["mount"] == "window":
		top = sign["hang_z"]
		for su in (-w / 2 + 0.08, w / 2 - 0.08):
			a = F.p(su, h / 2 - 0.02, back)
			links = int((top - a.z) / 0.024)
			for k in range(links):
				c = a + Vector((0, 0, 0.024 * k + 0.012))
				start = b.face_count()
				b.box(-0.004, 0.004, -0.0015, 0.0015, -0.012, 0.012, M_CHAIN)
				ang = math.pi / 2 if k % 2 else 0.0
				b.bm.faces.ensure_lookup_table()
				vs = set()
				for f in b.bm.faces[start:]:
					vs.update(f.verts)
				for vtx in vs:
					x, y, z = vtx.co.x, vtx.co.y, vtx.co.z
					rx = x * math.cos(ang) - y * math.sin(ang)
					ry = x * math.sin(ang) + y * math.cos(ang)
					vtx.co = c + F.U * rx + F.N * ry + F.V * z
	else:
		for su, sv in ((-w / 2 + 0.05, -h / 2 + 0.05), (w / 2 - 0.05, -h / 2 + 0.05), (-w / 2 + 0.05, h / 2 - 0.05), (w / 2 - 0.05, h / 2 - 0.05)):
			tube(b, [F.p(su, sv, back), F.p(su, sv, wall_n)], 0.006, M_FRAME, F.U)
	obj = b.build(uv_world=True)
	for gas, L in lengths.items():
		flux[gas] = round(LM_PER_M[gas] * L, 1)
	total = sum(flux.values())
	meta = {"family": "Neon", "ue_folder": "Lighting/Neon", "profile": "RbVenueProp", "frame": "venue", "pivot": "venue origin",
		"owner": PACKAGE, "generator": "db_neon.py", "sign": sign["id"], "design": f"Art/DiveBar/neon/{sign['id']}.svg",
		"tube_diameter_m": 2 * TUBE_R, "min_bend_radius_m": BEND_R, "tube_length_m": {g: round(v, 3) for g, v in lengths.items()},
		"tube_flux_lm": flux, "tube_flux_total_lm": round(total, 1), "proxy_flux_lm": round(total / math.pi, 1),
		"electrodes": len(electrodes), "supports": supports, "cast_shadow": False, "acoustic_material": "glass",
		"notes": "emissive tubes out of Lumen GI (the proxy rect light lights the room, 4.1); dead segments steady off (S8)"}
	rb_bl.export_asset(sign["asset"], [obj], pathlib.Path(out) / FOLDER, meta, package=PACKAGE)


def main() -> None:
	a = rb_bl.args()
	L = rb_bl.load_json("Art/DiveBar/layout.json")
	for sign in L["neon"]["signs"]:
		if a.only and sign["asset"] not in a.only and sign["id"] not in a.only:
			continue
		design = parse_svg(rb_bl.REPO / "Art" / "DiveBar" / "neon" / f"{sign['id']}.svg")
		build_sign(sign, design, a.out)
	rb_bl.log("db_neon: done")


main()
