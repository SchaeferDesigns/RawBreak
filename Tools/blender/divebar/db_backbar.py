"""H09 back bar (venue-dive-bar E04, 5.1 H09, M05 coolers, M11 mug rack). Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_backbar.py [-- --seed 1958 --preview --no-bake]

BackBar: 7.62 m along the left wall (E04 X 1.83 - 9.45), 0.66 m deep.
  * counter (0 - 0.91 m): stained cabinet doors, the 3-door under-counter cooler (E04 X 2.20 - 4.03) and the 2-door cooler
    (X 7.30 - 8.50) with fogged glass doors, a lit interior (emissive back panel, 5000 K, ~400 cd/m^2: L11 / L12 are M2-A's
    lights), wire shelves and cans; the ice-bin lid (X 6.0 - 6.6); a stained counter top;
  * upper back bar: an aged mirror (MI_DB_Mirror_Aged, desilvering from its edges - per-vertex mask in the wear mask's B channel)
    between pilasters from 1.10 to 2.20 m, three glass shelves per bay on brackets, each sagging by a beam model under its
    bottles (9.2: 3-8 mm), an LED channel under each shelf (the L9 / L10 strips are M2-A's lights; the emissive strip is the
    visible LED), a crown moulding at 2.20 m;
  * the mug-club rack over X 1.95 - 5.95 (2.20 - 2.60 m): two rails of pegs for 30 mugs (M11);
  * prop_instances: ~140 bottles (db_lathe_props.py designs) standing ON the sagged shelves (z from the same beam model) and 30 mugs
    on the pegs, all seeded (spacing +-10 %, yaw random, venue-dive-bar 9.2), for M2-A's importer / the level (ISM).
Local frame (Blender): the wall plane is x = 0 (pivot: wall item, horizontally centred, z = 0 = floor), front = +X toward the
bartender aisle, length along Y; Blender +Y = venue +X with the level's yaw 90 deg (like the bar counter). Spec asserts (hero
+-2 mm): 7.62 x 0.66 x 2.60, counter top 0.91, mirror 1.10 - 2.20.
Owner: M2-B.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "common"))
sys.path.insert(0, str(HERE))
import bmesh  # noqa: E402
import rb_bl  # noqa: E402
import db_props_common as pc  # noqa: E402
import db_lathe_props as lp  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

LENGTH = 7.62
HALF = LENGTH / 2.0
X0_VENUE = (1.83 + 9.45) / 2.0          # venue X of the local origin (5.64)
DEPTH = 0.66
Z_COUNTER = 0.91
TOP_T = 0.035
Z_MIRROR = (1.10, 2.20)
Z_CROWN = 2.20
Z_TOP_ALL = 2.60
UPPER_D = 0.30                           # pilasters / crown depth
SHELF_D = 0.24
XF = DEPTH - 0.065                       # front plane of doors / cooler fronts (the top overhangs it; handles stay inside 0.66)
SHELF_Z = (1.225, 1.555, 1.885)          # glass shelves (top surfaces before sag)
SHELF_T = 0.008
PILASTERS = [-HALF + 0.05, -1.90, 0.0, 1.90, HALF - 0.05]


def vy(x_venue: float) -> float:
	"""Venue X -> local (Blender) Y of the back bar."""
	return x_venue - X0_VENUE


COOLERS = [(vy(2.20), vy(4.03), 3), (vy(7.30), vy(8.50), 2)]
ICE = (vy(6.0), vy(6.6))
MUG_RACK = (vy(1.95), vy(5.95))


def shelf_supports(y0: float, y1: float) -> list[float]:
	mid = (y0 + y1) / 2.0
	return [y0, mid, y1]


def shelf_sag(y: float, supports: list[float], max_sag: float) -> float:
	for a, b in zip(supports, supports[1:]):
		if a <= y <= b:
			return pc.beam_sag(y - a, b - a, max_sag)
	return 0.0


# ---- counter -----------------------------------------------------------------------------------------------------------


def counter(asset: pc.Asset, rng) -> None:
	# carcass + top
	# the carcass is solid only between the coolers: their lit interiors must stay open behind the glass doors (a back panel closes them)
	hc, zc = Z_COUNTER - TOP_T - 0.10, 0.10 + (Z_COUNTER - TOP_T - 0.10) / 2.0
	y = -LENGTH / 2.0
	for a, b, _ in sorted(COOLERS):
		if a - y > 1e-4:
			asset.add(pc.bm_box((XF, a - y, hc), (XF / 2.0, (y + a) / 2.0, zc)), "Plywood_Painted", uv="box")
		asset.add(pc.bm_box((0.07, b - a, hc), (0.035, (a + b) / 2.0, zc)), "Plywood_Painted", uv="box")
		y = b
	if LENGTH / 2.0 - y > 1e-4:
		asset.add(pc.bm_box((XF, LENGTH / 2.0 - y, hc), (XF / 2.0, (y + LENGTH / 2.0) / 2.0, zc)), "Plywood_Painted", uv="box")
	top = pc.bm_box((DEPTH, LENGTH, TOP_T), (DEPTH / 2.0, 0.0, Z_COUNTER - TOP_T / 2.0))
	pc.bevel(top, 0.004, 2)
	asset.add(top, "Wood_Stained", uv="box", grain="y", touch=0.6)
	kick = pc.bm_box((0.02, LENGTH, 0.10), (XF - 0.03, 0.0, 0.05))
	asset.add(kick, "Rubber_Black", uv="box", grain="y")
	asset.hull_box((DEPTH, LENGTH, Z_COUNTER), (DEPTH / 2.0, 0.0, Z_COUNTER / 2.0))
	# fronts: coolers, ice bin, cabinet doors in between
	xf = XF
	occupied = [(a, b) for a, b, _ in COOLERS] + [ICE]
	for a, b, n in COOLERS:
		cooler(asset, a, b, n, rng)
	# ice bin: stainless door below, sliding lid on the counter
	door = pc.bm_box((0.02, ICE[1] - ICE[0] - 0.01, 0.70), (xf + 0.01, (ICE[0] + ICE[1]) / 2.0, 0.12 + 0.35))
	pc.bevel(door, 0.003, 1)
	asset.add(door, "Steel_Stainless", uv="box", grain="y", touch=0.5)
	lid = pc.bm_box((0.45, ICE[1] - ICE[0] - 0.02, 0.006), (0.35, (ICE[0] + ICE[1]) / 2.0, Z_COUNTER + 0.003))
	pc.bevel(lid, 0.002, 1)
	asset.add(lid, "Steel_Stainless", uv="box", grain="y", touch=0.8)
	# cabinet doors in the gaps
	gaps = []
	y = -HALF + 0.03
	for a, b in sorted(occupied):
		if a - y > 0.2:
			gaps.append((y, a - 0.02))
		y = b + 0.02
	if HALF - 0.03 - y > 0.2:
		gaps.append((y, HALF - 0.03))
	for g0, g1 in gaps:
		n = max(1, round((g1 - g0) / 0.55))
		w = (g1 - g0) / n
		for k in range(n):
			yc = g0 + w * (k + 0.5)
			d = pc.bm_box((0.019, w - 0.006, 0.72), (xf + 0.0095, yc, 0.12 + 0.36))
			pc.bevel(d, 0.003, 2)
			asset.add(d, "Wood_Stained", uv="box", grain="z", touch=0.2)
			panel = pc.bm_box((0.006, w - 0.13, 0.52), (xf + 0.021, yc, 0.12 + 0.36))
			pc.bevel(panel, 0.004, 2)
			asset.add(panel, "Wood_Stained", uv="box", grain="z")
			knob = pc.bm_lathe([(0.0, 0.0), (0.011, 0.0), (0.012, 0.006), (0.006, 0.012), (0.015, 0.022), (0.013, 0.028), (0.0, 0.029)], 20)
			pc.transform(knob, Matrix.Translation((xf + 0.019, yc + (w / 2.0 - 0.05) * (1 if k % 2 else -1), 0.70)) @
				Matrix.Rotation(math.radians(90), 4, "Y"))
			asset.add(knob, "Brass_Worn", uv="cyl", touch=1.0)


def cooler(asset: pc.Asset, y0: float, y1: float, doors: int, rng) -> None:
	"""Under-counter glass-door cooler: black body, lit white interior, fogged doors, cans on two wire shelves."""
	xf = XF
	z0, z1 = 0.10, Z_COUNTER - TOP_T - 0.01
	# the black face frame around the opening (a solid front slab here hid the lit interior behind the glass doors)
	for (sy, sz, cy, cz) in ((y1 - y0, 0.05, (y0 + y1) / 2.0, z1 - 0.025), (y1 - y0, 0.05, (y0 + y1) / 2.0, z0 + 0.025),
			(0.03, z1 - z0, y0 + 0.015, (z0 + z1) / 2.0), (0.03, z1 - z0, y1 - 0.015, (z0 + z1) / 2.0)):
		body = pc.bm_box((0.03, sy, sz), (xf - 0.015, cy, cz))
		asset.add(body, "Paint_Cabinet", uv="box")
	# interior box (white, open to the front): back panel emissive
	ix0 = 0.08
	back = pc.bm_box((0.01, y1 - y0 - 0.05, z1 - z0 - 0.08), (ix0, (y0 + y1) / 2.0, (z0 + z1) / 2.0))
	asset.add(back, "Emissive_CoolerPanel", uv="box")
	for sy in (y0 + 0.02, y1 - 0.02):
		side = pc.bm_box((xf - ix0, 0.01, z1 - z0 - 0.06), ((xf + ix0) / 2.0, sy, (z0 + z1) / 2.0))
		asset.add(side, "Enamel_WhiteInt", uv="box")
	for zz in (z0 + 0.04, z1 - 0.03):
		plate = pc.bm_box((xf - ix0, y1 - y0 - 0.04, 0.01), ((xf + ix0) / 2.0, (y0 + y1) / 2.0, zz))
		asset.add(plate, "Enamel_WhiteInt", uv="box")
	cans = ["Can_LanternFlats", "Can_Hollenbeck", "Can_OldCastor"]
	cells = {"Can_LanternFlats": "can_lantern_flats", "Can_Hollenbeck": "bottle_hollenbeck", "Can_OldCastor": "bottle_old_castor"}
	for shelf_z in (z0 + 0.045, z0 + 0.36):
		wire = pc.bm_box((xf - ix0 - 0.04, y1 - y0 - 0.06, 0.004), ((xf + ix0) / 2.0 - 0.01, (y0 + y1) / 2.0, shelf_z))
		asset.add(wire, "Steel_Zinc", uv="box")
		# cans in rows (front two rows only: behind fogged glass), some missing, some turned
		y = y0 + 0.07
		while y < y1 - 0.07:
			for row in range(2):
				if rng.random() < 0.18:
					continue
				kind = cans[int(rng.random() * 3)]
				cx = xf - 0.10 - 0.07 * row + rng.uniform(-0.004, 0.004)
				c = pc.bm_lathe([(0.0, 0.0), (0.0305, 0.0), (0.0331, 0.012), (0.0331, 0.108), (0.0275, 0.1195), (0.0, 0.1185)], 20)
				pc.orient_radial(c, outward=True)
				pc.translate(c, (cx, y, shelf_z + 0.002))
				asset.add(c, "Aluminium", uv="cyl")
				rr = 0.0339    # clear of the 20-gon can (label chord sag < 0.2 mm at 24 segments)
				ang = rng.uniform(-0.6, 0.6)

				def pt(u, v, cx=cx, y=y, ang=ang, sz=shelf_z):
					a = ang + u / rr
					return Vector((cx + rr * math.cos(a), y + rr * math.sin(a), sz + 0.062 + v))
				lab = pc.surface_patch(pt, (-0.09, 0.09), (-0.045, 0.045), 24, 1, pc.label_rect(cells[kind]))
				asset.add(lab, "Label_Gloss", uv="none", smooth_angle=80.0)
			y += 0.068 + rng.uniform(0.0, 0.006)
	# doors: black frames + fogged glass (the bottom 12 cm fogged harder: MI_DB_Glass_Cooler dirt)
	w = (y1 - y0) / doors
	for k in range(doors):
		yc = y0 + w * (k + 0.5)
		fz0, fz1 = z0 + 0.01, z1 - 0.01
		for (sy, sz, cy, cz) in ((w - 0.004, 0.05, yc, fz0 + 0.025), (w - 0.004, 0.05, yc, fz1 - 0.025), (0.045, fz1 - fz0, yc - w / 2.0 + 0.0245, (fz0 + fz1) / 2.0),
				(0.045, fz1 - fz0, yc + w / 2.0 - 0.0245, (fz0 + fz1) / 2.0)):
			fr = pc.bm_box((0.035, sy, sz), (xf + 0.0175, cy, cz))
			pc.bevel(fr, 0.003, 1)
			asset.add(fr, "Plastic_Black", uv="box", touch=0.4)
		gl = pc.bm_box((0.004, w - 0.09, fz1 - fz0 - 0.10), (xf + 0.02, yc, (fz0 + fz1) / 2.0))
		asset.add(gl, "Glass_Cooler", uv="box")
		handle = pc.bm_box((0.02, 0.018, 0.30), (xf + 0.045, yc + (w / 2.0 - 0.045) * (1 if k % 2 == 0 else -1), (fz0 + fz1) / 2.0 + 0.05))
		pc.bevel(handle, 0.004, 2)
		asset.add(handle, "Steel_Stainless", uv="box", touch=1.0)


# ---- upper back bar --------------------------------------------------------------------------------------------------


def upper(asset: pc.Asset, rng, instances: list) -> None:
	# backsplash between the counter and the mirror
	splash = pc.bm_box((0.018, LENGTH, Z_MIRROR[0] - Z_COUNTER), (0.009, 0.0, (Z_COUNTER + Z_MIRROR[0]) / 2.0))
	asset.add(splash, "Wood_PlankWall", uv="box", grain="y")
	ledge = pc.bm_box((0.10, LENGTH, 0.025), (0.05, 0.0, Z_MIRROR[0] - 0.0125))
	pc.bevel(ledge, 0.003, 2)
	asset.add(ledge, "Wood_Stained", uv="box", grain="y")
	# the mirror: one panel per bay, desilvered toward its edges and corners (per-vertex mask -> WM.b)
	for a, b in zip(PILASTERS, PILASTERS[1:]):
		y0, y1 = a + 0.04, b - 0.04
		z0, z1 = Z_MIRROR[0], Z_MIRROR[1] - 0.04
		mir = pc.bm_box((0.006, y1 - y0, z1 - z0), (0.022, (y0 + y1) / 2.0, (z0 + z1) / 2.0))
		bmesh.ops.subdivide_edges(mir, edges=list(mir.edges), cuts=14, use_grid_fill=True)
		ph = rng.uniform(0, 6.28)

		def desilver(co, y0=y0, y1=y1, z0=z0, z1=z1, ph=ph):
			d = min(co.y - y0, y1 - co.y, co.z - z0, z1 - co.z)
			edge = math.exp(-max(d, 0.0) / 0.035)
			blot = 0.5 + 0.5 * math.sin(co.y * 7.1 + ph) * math.sin(co.z * 5.3 + ph * 0.7)
			return min(1.0, edge * (0.7 + 0.6 * blot))
		asset.add(mir, "Mirror_Aged", uv="box", grain="y", touch=desilver)
		backer = pc.bm_box((0.012, b - a, Z_MIRROR[1] - Z_MIRROR[0]), (0.006 + 0.0, (a + b) / 2.0, (Z_MIRROR[0] + Z_MIRROR[1]) / 2.0))
		asset.add(backer, "Plywood_Painted", uv="box")
	# pilasters and the crown
	for py in PILASTERS:
		pil = pc.bm_box((UPPER_D - 0.02, 0.08, Z_CROWN - Z_MIRROR[0]), ((UPPER_D - 0.02) / 2.0, py, (Z_MIRROR[0] + Z_CROWN) / 2.0))
		pc.bevel(pil, 0.004, 2)
		asset.add(pil, "Wood_Stained", uv="box", grain="z", touch=0.2)
		for zz in (Z_MIRROR[0] + 0.03, Z_CROWN - 0.05):
			cap = pc.bm_box((UPPER_D, 0.10, 0.03), (UPPER_D / 2.0, py, zz))
			pc.bevel(cap, 0.005, 2)
			asset.add(cap, "Wood_Stained", uv="box", grain="y")
		asset.hull_box((UPPER_D, 0.10, Z_CROWN - Z_MIRROR[0]), (UPPER_D / 2.0, py, (Z_MIRROR[0] + Z_CROWN) / 2.0))
	crown = prism_xz([(0.0, Z_CROWN - 0.02), (UPPER_D, Z_CROWN - 0.02), (UPPER_D + 0.02, Z_CROWN + 0.01), (UPPER_D - 0.01, Z_CROWN + 0.035),
		(0.0, Z_CROWN + 0.035)], -HALF, HALF)
	pc.bevel(crown, 0.003, 1)
	asset.add(crown, "Wood_Stained", uv="box", grain="y")
	asset.hull_box((UPPER_D + 0.02, LENGTH, 0.055), ((UPPER_D + 0.02) / 2.0, 0.0, Z_CROWN + 0.0075))
	# glass shelves: 3 per bay on brass brackets; sag between brackets under the bottles; LED channels underneath
	designs = list(lp.DESIGNS.keys())
	spirits = [d for d in designs if lp.DESIGNS[d]["kind"] == "spirit"]
	for bay, (a, b) in enumerate(zip(PILASTERS, PILASTERS[1:])):
		y0, y1 = a + 0.045, b - 0.045
		supports = shelf_supports(y0, y1)
		for level, zs in enumerate(SHELF_Z):
			max_sag = rng.uniform(0.003, 0.0075)
			n = max(2, int((y1 - y0) / 0.05))
			bm = bmesh.new()
			grid = []
			for i in range(n + 1):
				y = y0 + (y1 - y0) * i / n
				s = shelf_sag(y, supports, max_sag)
				grid.append([bm.verts.new((x, y, z - s)) for x, z in ((0.03, zs - SHELF_T), (0.03 + SHELF_D, zs - SHELF_T), (0.03 + SHELF_D, zs),
					(0.03, zs))])
			for i in range(n):
				for k in range(4):
					bm.faces.new((grid[i][k], grid[i + 1][k], grid[i + 1][(k + 1) % 4], grid[i][(k + 1) % 4]))
			bm.faces.new(list(reversed(grid[0])))
			bm.faces.new(grid[-1])
			bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
			asset.add(bm, "Glass_Shelf", uv="box", grain="y")
			asset.hull_box((SHELF_D, y1 - y0, SHELF_T + 0.004), (0.03 + SHELF_D / 2.0, (y0 + y1) / 2.0, zs - SHELF_T / 2.0))
			for sy in supports:
				br = prism_xz([(0.022, zs - SHELF_T - 0.08), (0.03, zs - SHELF_T - 0.08), (0.03 + SHELF_D * 0.7, zs - SHELF_T - 0.004),
					(0.03 + SHELF_D * 0.7, zs - SHELF_T), (0.022, zs - SHELF_T)], sy - 0.005, sy + 0.005)
				asset.add(br, "Brass_Worn", uv="box")
			led = pc.bm_box((0.016, y1 - y0 - 0.02, 0.008), (0.03 + SHELF_D - 0.03, (y0 + y1) / 2.0, zs - SHELF_T - 0.004 - shelf_sag((y0 + y1) / 2.0,
				supports, max_sag) * 0.5))
			asset.add(led, "Alu_Trim", uv="box", grain="y")
			strip = pc.bm_box((0.006, y1 - y0 - 0.03, 0.001), (0.03 + SHELF_D - 0.03, (y0 + y1) / 2.0, zs - SHELF_T - 0.0085 - shelf_sag((y0 + y1) / 2.0,
				supports, max_sag) * 0.5))
			asset.add(strip, "Emissive_LedStrip", uv="box")
			# bottles on this shelf: two rows (back row taller), spacing +-10 %, yaw random, top shelf dustier (the level's ISM)
			# one row per shelf (two on the working middle shelf), gaps where bottles ran out: ~150 bottles in all (R15 >= 120)
			rows = (0.03 + SHELF_D * 0.55,) if level != 1 else (0.03 + SHELF_D * 0.72, 0.03 + SHELF_D * 0.30)
			for row, xr in enumerate(rows):
				y = y0 + 0.06 + rng.uniform(0.0, 0.03)
				while y < y1 - 0.06:
					if rng.random() < (0.22 if level == 2 else 0.12):
						y += rng.uniform(0.08, 0.2)
						continue
					pool = spirits if level < 2 else spirits + ["Bottle_OldCastor", "Bottle_Hollenbeck"]
					design = pool[int(rng.random() * len(pool))]
					d = lp.DESIGNS[design]
					if d["H"] + 0.05 > (SHELF_Z[level + 1] - zs if level < 2 else 0.30):
						design = "Bottle_OldCastor" if level == 2 else design
						d = lp.DESIGNS[design]
					zb = zs - shelf_sag(y, supports, max_sag)
					instances.append({"asset": design, "location_ue_m": pc.ue((xr + rng.uniform(-0.01, 0.01), y, zb)),
						"yaw_deg": round(rng.uniform(-35.0, 35.0) + (0.0 if row == 0 else rng.uniform(-10.0, 10.0)), 2),
						"age_bias": 0.25 if level == 2 else 0.0, "group": f"shelf_{bay}_{level}"})
					y += (2.0 * d["R"] + 0.018) * rng.uniform(0.95, 1.25)


def mug_rack(asset: pc.Asset, rng, instances: list) -> None:
	"""M11: a shelf board on the crown with two rows of pegs; 30 mugs of the mug club (names later, TXT)."""
	y0, y1 = MUG_RACK
	for zr, xr in ((Z_CROWN + 0.10, 0.18), (Z_CROWN + 0.30, 0.10)):
		rail = pc.bm_box((0.02, y1 - y0, 0.06), (0.03, (y0 + y1) / 2.0, zr))
		pc.bevel(rail, 0.003, 2)
		asset.add(rail, "Wood_Stained", uv="box", grain="y")
	board = pc.bm_box((0.26, y1 - y0, 0.022), (0.13, (y0 + y1) / 2.0, Z_CROWN + 0.046))
	pc.bevel(board, 0.003, 2)
	asset.add(board, "Wood_Stained", uv="box", grain="y")
	back = pc.bm_box((0.012, y1 - y0, Z_TOP_ALL - Z_CROWN - 0.035), (0.006, (y0 + y1) / 2.0, (Z_TOP_ALL + Z_CROWN + 0.035) / 2.0))
	asset.add(back, "Paneling_Dark", uv="box", grain="z")
	cap = pc.bm_box((0.05, y1 - y0 + 0.02, 0.02), (0.025, (y0 + y1) / 2.0, Z_TOP_ALL - 0.01))
	pc.bevel(cap, 0.004, 2)
	asset.add(cap, "Wood_Stained", uv="box", grain="y")
	asset.hull_box((0.26, y1 - y0, Z_TOP_ALL - Z_CROWN - 0.035), (0.13, (y0 + y1) / 2.0, (Z_TOP_ALL + Z_CROWN + 0.035) / 2.0))
	shapes = ["Mug_A", "Mug_B", "Mug_C"]
	n = 15
	for row, (z_peg, x_mug) in enumerate(((Z_CROWN + 0.057, 0.14), (Z_CROWN + 0.21, 0.09))):
		for k in range(n):
			y = y0 + (y1 - y0) * (k + 0.5) / n + rng.uniform(-0.02, 0.02)
			idx = row * n + k + 1
			if row == 1:
				peg = pc.bm_cylinder(0.006, 0.06, 10)
				pc.transform(peg, Matrix.Translation((0.06, y, z_peg + 0.05)) @ Matrix.Rotation(math.radians(80), 4, "Y"))
				asset.add(peg, "Wood_Stained", uv="cyl")
			instances.append({"asset": shapes[int(rng.random() * 3)], "location_ue_m": pc.ue((x_mug, y, z_peg if row == 0 else z_peg)),
				"yaw_deg": round(-90.0 + rng.uniform(-25, 25), 2), "mug_number": idx, "ribbon": idx == 17, "group": f"mug_rack_{row}",
				"note": "#17 WALT carries a black ribbon (S4)" if idx == 17 else ""})


def prism_xz(poly_xz, y0: float, y1: float) -> bmesh.types.BMesh:
	bm = pc.bm_extrude_polygon(poly_xz, 0.0, 1.0)
	for vert in bm.verts:
		x, zz, t = vert.co.x, vert.co.y, vert.co.z
		vert.co = Vector((x, y0 + (y1 - y0) * t, zz))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def build(seed: int) -> tuple[pc.Asset, list]:
	rng = rb_bl.rng("BackBar", 0, seed)
	asset = pc.Asset("BackBar", "Bar", "H09", "hero")
	instances: list = []
	counter(asset, rng)
	upper(asset, rng, instances)
	mug_rack(asset, rng, instances)
	# the counter top carries a few bottles too (the well speed rail is on the front bar)
	for k, design in enumerate(["Bottle_Korvin", "Bottle_Dockhand", "Bottle_Ember", "Bottle_AshbyRidge"]):
		instances.append({"asset": design, "location_ue_m": pc.ue((0.12, vy(4.40) + 0.13 * k, Z_COUNTER)), "yaw_deg": round(rng.uniform(-30, 30), 2),
			"group": "counter"})
	asset.instances = instances
	return asset, instances


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	if not pc.selected(a.only, "BackBar"):
		return
	pc.clear_scene_keep_materials()
	asset, instances = build(a.seed)
	obj = asset.build()
	checks = [asset.check_slice(Z_COUNTER - 0.01, Z_COUNTER + 0.001, (DEPTH, LENGTH), pc.HERO_TOL, "counter top")]
	tops = [vv.co.z for vv in obj.data.vertices if 0.35 < vv.co.x < DEPTH - 0.01 and Z_COUNTER - 0.02 < vv.co.z < Z_COUNTER + 0.002]
	if abs(max(tops) - Z_COUNTER) > pc.HERO_TOL:
		rb_bl.fail(f"BackBar counter top {max(tops):.4f}")
	asset.export(out, (DEPTH, LENGTH, Z_TOP_ALL), acoustic="wood_panel", pivot="wall_plane_bottom_centre", wm_res=2048, meta={
		"element": "E04",
		"spec_checks": checks,
		"counter_top_z_m": Z_COUNTER, "mirror_z_m": list(Z_MIRROR), "shelf_top_z_m": list(SHELF_Z),
		"coolers_blender_y_m": [[round(c[0], 3), round(c[1], 3), c[2]] for c in COOLERS],
		"placement_hint_ue_cm": {"location": [X0_VENUE * 100.0, 0.0, 0.0], "yaw_deg": 90.0,
			"note": "E04 X 1.83 - 9.45 against the left wall (Y = 0); local +X (front) -> venue +Y"},
		"prop_instances_note": "asset = the instanced asset id (SM_DB_<asset>), location_ue_m in the back bar's Unreal frame (pivot-relative, metres; M2-A's place_prop reads it); bottles stand on the sagged shelves",
		"notes": "L9/L10 LED strips, L11/L12 cooler lights, TV-1, N3 and the POS are M2-A's / later"})
	rb_bl.log(f"BackBar: {len(instances)} prop instances")
	if pc.want_preview():
		pc.preview(obj, "BackBar", view=(1.0, 0.35, 0.2), zoom=0.4, target=(0.3, -1.0, 1.3))
	rb_bl.log("db_backbar: ['BackBar']")


main()
