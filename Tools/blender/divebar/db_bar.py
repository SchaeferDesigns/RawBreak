"""H08 bar counter (venue-dive-bar E06, 5.1 H08). Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_bar.py [-- --seed 1958 --preview --no-bake]

BarCounter: the 7.92 m customer bar of The Low Bridge Tavern.
  * lacquered cherry top (MI_DB_Wood_Lacquered_Bar, the drink surface at 1.07 m, E06) with a bullnose on the bartender edge;
  * padded oxblood vinyl armrest on the customer edge (sectioned by seams, rolled over the edge; it rises 3 cm above the top);
  * the die (front face, 0.26 m under the overhang) of vertical tongue-and-groove planks (MI_DB_Wood_PlankWall) over a black
    rubber cove base, wooden knee braces under the overhang;
  * brass foot rail, 51 mm tube at 0.20 m (E06) on die-mounted elbow brackets, ball end caps, sagging a millimetre between
    brackets, one bracket loose;
  * the lift-up service flap at the pool-room end (brass butt hinges) with a swinging half door below, left slightly ajar;
  * the bartender side: a stainless underbar face with a speed rail, the carcass closed to the floor;
  * Deacon's brass plate ("RESERVED - DEACON - SINCE 1979", TXT) screwed to the top in front of stool #10.
Local frame (Blender): front = +X = the customer side, length along Y; the service flap is at the +Y end (Blender +Y = Unreal -Y;
with the level's yaw 90 deg it lands at the venue's high-X end, E06 X 9.14 - 9.75). Pivot: floor, centre of the length, x = 0
at the middle of the top's depth (the top spans x -0.395 .. +0.395 = venue Y 1.60 - 2.39, the die is at x +0.135 = Y 2.13, the
foot rail at x +0.305 = Y 2.30). Spec asserts (hero +-2 mm): length 7.92, depth 0.79, drink surface at 1.070, foot-rail
centre height 0.200 and diameter 0.051; overall height 1.100 (the armrest crown, a design value).
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
from mathutils import Matrix, Vector  # noqa: E402

LENGTH = 7.92
HALF = LENGTH / 2.0
DEPTH = 0.79
X_BACK = -0.395            # bartender edge of the top (venue Y 1.60)
X_FRONT = 0.395            # customer edge (venue Y 2.39)
X_DIE = 0.135              # die face (venue Y 2.13)
Z_TOP = 1.070              # drink surface (E06)
TOP_T = 0.045
Z_TOP_UNDER = Z_TOP - TOP_T
X_ARM0 = 0.262             # armrest starts here
Z_ARM = 1.100              # armrest crown
X_RAIL = 0.305             # foot rail (venue Y 2.30)
Z_RAIL = 0.200
RAIL_R = 0.0255
FLAP_W = 0.61              # E06: X 9.14 - 9.75
Y_FLAP = HALF - FLAP_W     # the flap starts here (Blender +Y end)
BASE_H = 0.10              # rubber cove base
X_UNDERBAR = -0.255        # bartender-side face under the top
RAIL_PROBE = [0.0]       # mid-span of the first foot-rail span (set by foot_rail, used by the diameter check)
STOOL10_Y = 8.60 - (1.83 + 9.75) / 2.0   # stool #10 (E07 X 8.60) relative to the bar centre (venue X 5.79 -> Blender +Y)


def prism_xz(poly_xz, y0: float, y1: float) -> bmesh.types.BMesh:
	"""Prism of a polygon in the XZ plane (counter-clockwise seen from -Y) between y0 and y1."""
	bm = pc.bm_extrude_polygon(poly_xz, 0.0, 1.0)
	for vert in bm.verts:
		x, zz, t = vert.co.x, vert.co.y, vert.co.z
		vert.co = Vector((x, y0 + (y1 - y0) * t, zz))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def top(asset: pc.Asset, rng) -> None:
	# main top: y from -HALF to Y_FLAP - 0.0015 (3 mm gap at the flap), x from X_BACK to 0.372 (the armrest wraps the rest)
	y1 = Y_FLAP - 0.0015
	xb = X_BACK + TOP_T / 2.0      # the slab ends where the bullnose (radius TOP_T / 2) takes over
	bm = pc.bm_box((0.372 - xb, y1 + HALF, TOP_T), ((0.372 + xb) / 2.0, (y1 - HALF) / 2.0, Z_TOP - TOP_T / 2.0))
	# subdivide along the length so the top can carry a millimetre of waviness (an old top is never flat)
	bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.y - e.verts[1].co.y) > 1.0], cuts=40, use_grid_fill=True)
	pc.bevel(bm, 0.004, 2, angle_deg=30.0)
	phase = rng.uniform(0, 6.28)
	pc.deform(bm, lambda p: p + Vector((0.0, 0.0, 0.0009 * math.sin(p.y * 1.7 + phase) * (1.0 if p.z > Z_TOP - 0.01 else 0.3))))
	asset.add(bm, "Wood_Lacquered_Bar", uv="box", grain="y", touch=0.5)
	# bullnose on the bartender edge (a half round glued to the slab)
	nose = pc.bm_cylinder(TOP_T / 2.0, y1 + HALF - 0.004, 16, (0.0, 0.0, 0.0))
	pc.transform(nose, Matrix.Translation((xb, (y1 - HALF) / 2.0, Z_TOP - TOP_T / 2.0)) @ Matrix.Rotation(math.radians(90), 4, "X"))
	asset.add(nose, "Wood_Lacquered_Bar", uv="box", grain="y", touch=0.9)
	# the service flap (lift-up; lies closed) + two brass butt hinges at its joint
	flap = pc.bm_box((0.372 - X_BACK, FLAP_W - 0.0015, TOP_T), ((0.372 + X_BACK) / 2.0, Y_FLAP + 0.0015 + (FLAP_W - 0.0015) / 2.0,
		Z_TOP - TOP_T / 2.0 - 0.0015))
	pc.bevel(flap, 0.004, 2)
	asset.add(flap, "Wood_Lacquered_Bar", uv="box", grain="y", touch=0.8)
	for hx in (X_BACK + 0.12, 0.25):
		leaf = pc.bm_box((0.06, 0.07, 0.0016), (hx, Y_FLAP, Z_TOP + 0.0008))
		asset.add(leaf, "Brass_Worn", uv="box", touch=0.4)
		knuckle = pc.bm_cylinder(0.0045, 0.06, 12)
		pc.transform(knuckle, Matrix.Translation((hx, Y_FLAP, Z_TOP + 0.0045)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
		asset.add(knuckle, "Brass_Worn", uv="cyl", touch=0.4)
	asset.hull_box((0.372 - X_BACK, LENGTH, TOP_T), ((0.372 + X_BACK) / 2.0, 0.0, Z_TOP - TOP_T / 2.0))
	# Deacon's brass plate in front of stool #10
	lab = pc.label_rect("deacon_plate")
	pw, ph = 0.100, 0.026

	def plate_pt(u, v):
		return Vector((0.235 - v, STOOL10_Y + u, Z_TOP + 0.0016))
	plate = pc.surface_patch(plate_pt, (-pw / 2, pw / 2), (-ph / 2, ph / 2), 1, 1, lab)
	asset.add(plate, "Label_Gloss", uv="none")
	backing = pc.bm_box((ph + 0.002, pw + 0.002, 0.0014), (0.235, STOOL10_Y, Z_TOP + 0.0007))
	asset.add(backing, "Brass_Worn", uv="box", touch=0.6)


def armrest(asset: pc.Asset, rng) -> None:
	"""Padded vinyl roll on the customer edge, x 0.262 .. 0.395, crown at 1.10; seams every ~0.9 m pinch the padding."""
	prof = [(X_ARM0, Z_TOP), (X_ARM0 + 0.004, Z_TOP + 0.014), (0.278, Z_ARM - 0.009), (0.300, Z_ARM - 0.002), (0.325, Z_ARM),
		(0.350, Z_ARM - 0.003), (0.372, Z_ARM - 0.012), (0.387, Z_ARM - 0.027), (X_FRONT, Z_ARM - 0.047), (0.393, Z_ARM - 0.066),
		(0.386, Z_ARM - 0.079), (0.372, Z_TOP - TOP_T - 0.008), (0.352, Z_TOP - TOP_T - 0.006), (0.352, Z_TOP - TOP_T)]
	y0, y1 = -HALF, Y_FLAP - 0.004
	n = int((y1 - y0) / 0.02)
	seams = []
	s = y0 + rng.uniform(0.3, 0.6)
	while s < y1 - 0.3:
		seams.append(s)
		s += rng.uniform(0.82, 0.98)
	bm = bmesh.new()
	rings = []
	cx, cz = 0.33, Z_TOP + 0.005     # centre of the profile (for the padding bulge)
	for i in range(n + 1):
		y = y0 + (y1 - y0) * i / n
		d_seam = min([abs(y - sy) for sy in seams] + [9.0])
		pinch = 0.0035 * math.exp(-(d_seam / 0.012) ** 2)
		# padding bulge between the seams + tired, flattened spots where elbows rest
		bulge = 0.0015 * (1.0 - math.exp(-(d_seam / 0.12) ** 2)) - 0.0012 * (0.5 + 0.5 * math.sin(y * 5.3 + 1.1))
		end = min(y - y0, y1 - y)
		cap = min(1.0, end / 0.025)
		ring = []
		for (px, pz) in prof:
			dx, dz = px - cx, pz - cz
			ln = math.hypot(dx, dz)
			if pz > Z_TOP + 0.001:
				k = (-pinch + bulge) / max(ln, 1e-4)
				px, pz = px + dx * k, pz + dz * k
			# round the ends: shrink toward the centre over the last 25 mm
			sh = 0.35 + 0.65 * math.sqrt(max(0.0, 1.0 - (1.0 - cap) ** 2))
			px, pz = cx + (px - cx) * sh, cz + (pz - cz) * sh if pz > Z_TOP else pz
			ring.append(bm.verts.new((px, y, pz)))
		rings.append(ring)
	m = len(prof)
	for i in range(n):
		a, b = rings[i], rings[i + 1]
		for k in range(m - 1):
			bm.faces.new((a[k], b[k], b[k + 1], a[k + 1]))
	bm.faces.new(list(reversed(rings[0])))
	bm.faces.new(rings[-1])
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	asset.add(bm, "Vinyl_Oxblood", uv="box", grain="y", touch=1.0, smooth_angle=50.0)
	asset.hull_box((X_FRONT - X_ARM0, y1 - y0, Z_ARM - (Z_TOP - TOP_T - 0.008)), ((X_FRONT + X_ARM0) / 2.0, (y0 + y1) / 2.0,
		(Z_ARM + Z_TOP - TOP_T - 0.008) / 2.0))
	# seam piping (a thin welt at each seam)
	for sy in seams:
		pts = [Vector((px, sy, pz)) for px, pz in prof[:10]]
		asset.add(pc.bm_tube_path(pts, 0.0016, 6), "Vinyl_Oxblood", uv="box", touch=1.0)


def die(asset: pc.Asset, rng) -> None:
	"""Vertical tongue-and-groove planks over a rubber cove base; the opening under the flap holds the half door."""
	y = -HALF + 0.0
	thick = 0.019
	z0, z1 = BASE_H, Z_TOP_UNDER
	while y < Y_FLAP - 0.02:
		w = min(rng.uniform(0.092, 0.142), Y_FLAP - y)
		gap = rng.uniform(0.0006, 0.0018)
		bow = rng.uniform(-0.0012, 0.0012)
		plank = pc.bm_box((thick, w - gap, z1 - z0), (X_DIE - thick / 2.0, y + w / 2.0, (z0 + z1) / 2.0))
		bmesh.ops.subdivide_edges(plank, edges=[e for e in plank.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 0.5], cuts=6,
			use_grid_fill=True)
		pc.bevel(plank, 0.0015, 1)
		pc.deform(plank, lambda p, bow=bow: p + Vector((bow * math.sin(math.pi * (p.z - z0) / (z1 - z0)), 0.0, 0.0)))
		asset.add(plank, "Wood_PlankWall", uv="box", grain="z", touch=0.0)
		y += w
	asset.hull_box((X_DIE - X_UNDERBAR, Y_FLAP + HALF, Z_TOP_UNDER), ((X_DIE + X_UNDERBAR) / 2.0, (Y_FLAP - HALF) / 2.0, Z_TOP_UNDER / 2.0))
	# rubber cove base, recessed 8 mm
	base = pc.bm_box((0.006, Y_FLAP + HALF, BASE_H), (X_DIE - thick + 0.003 - 0.008 + 0.006, (Y_FLAP - HALF) / 2.0, BASE_H / 2.0))
	pc.bevel(base, 0.002, 1)
	asset.add(base, "Rubber_Black", uv="box", grain="y")
	toe = prism_xz([(X_DIE - 0.008, 0.0), (X_DIE + 0.004, 0.0), (X_DIE - 0.008, 0.012)], -HALF, Y_FLAP)
	asset.add(toe, "Rubber_Black", uv="box", grain="y")
	# carcass behind the planks (plywood), end panel at -Y
	back = pc.bm_box((0.012, Y_FLAP + HALF, z1), (X_DIE - thick - 0.006, (Y_FLAP - HALF) / 2.0, z1 / 2.0))
	asset.add(back, "Plywood_Painted", uv="box")
	end = pc.bm_box((X_DIE - X_UNDERBAR, 0.019, z1), ((X_DIE + X_UNDERBAR) / 2.0, -HALF + 0.0095, z1 / 2.0))
	pc.bevel(end, 0.0015, 1)
	asset.add(end, "Wood_PlankWall", uv="box", grain="z")
	# knee braces under the overhang
	yb = -HALF + 0.35
	while yb < Y_FLAP - 0.2:
		brace = prism_xz([(X_DIE, Z_TOP_UNDER - 0.19), (X_DIE, Z_TOP_UNDER), (0.33, Z_TOP_UNDER), (0.33, Z_TOP_UNDER - 0.02),
			(X_DIE + 0.03, Z_TOP_UNDER - 0.17)], yb - 0.019, yb + 0.019)
		pc.bevel(brace, 0.002, 1)
		asset.add(brace, "Wood_Stained", uv="box", grain="x")
		yb += rng.uniform(1.25, 1.4)


def half_door(asset: pc.Asset, rng) -> None:
	"""The swinging half door under the flap (0.60 wide, 0.10 - 0.95 m), hinged at the -Y post, ~6 deg ajar."""
	w = FLAP_W - 0.02
	door = pc.bm_box((0.028, w, 0.85), (0.0, w / 2.0, 0.10 + 0.425))
	pc.bevel(door, 0.003, 2)
	# a routed panel groove look: an inner panel 4 mm proud
	panel = pc.bm_box((0.006, w - 0.14, 0.55), (0.016, w / 2.0, 0.10 + 0.43))
	pc.bevel(panel, 0.004, 2)
	ajar = Matrix.Translation((X_DIE - 0.014, Y_FLAP + 0.01, 0.0)) @ Matrix.Rotation(math.radians(-6.0 + rng.uniform(-1, 1)), 4, "Z")
	for bm in (door, panel):
		pc.transform(bm, ajar)
	asset.add(door, "Wood_PlankWall", uv="box", grain="z", touch=0.3)
	asset.add(panel, "Wood_PlankWall", uv="box", grain="z", touch=0.6)
	post = pc.bm_box((0.04, 0.02, Z_TOP_UNDER), (X_DIE - 0.02, Y_FLAP + 0.0, Z_TOP_UNDER / 2.0))
	asset.add(post, "Wood_Stained", uv="box", grain="z")
	for hz in (0.25, 0.80):
		hinge = pc.bm_cylinder(0.006, 0.08, 12, (X_DIE - 0.004, Y_FLAP + 0.012, hz))
		asset.add(hinge, "Brass_Worn", uv="cyl")
	asset.hull_box((0.04, w, 0.85), (X_DIE - 0.02, Y_FLAP + w / 2.0 + 0.01, 0.525))


def foot_rail(asset: pc.Asset, rng) -> None:
	y0, y1 = -HALF + 0.12, Y_FLAP - 0.10
	brackets = [y0 + 0.18]
	while brackets[-1] < y1 - 1.3:
		brackets.append(brackets[-1] + rng.uniform(1.15, 1.35))
	brackets.append(y1 - 0.18)
	loose = rng.randrange(1, len(brackets) - 1)

	def sag(y: float) -> float:
		for a, b in zip(brackets, brackets[1:]):
			if a <= y <= b:
				t = (y - a) / (b - a)
				return -0.0011 * math.sin(math.pi * t)
		return 0.0
	pts = []
	n = int((y1 - y0) / 0.05)
	for i in range(n + 1):
		y = y0 + (y1 - y0) * i / n
		pts.append(Vector((X_RAIL, y, Z_RAIL + sag(y))))
	rail = pc.bm_tube_path(pts, RAIL_R, 28, cap=False)
	asset.add(rail, "Brass_Worn", uv="cyl", touch=1.0)
	for y_end, sgn in ((y0, -1.0), (y1, 1.0)):
		cap = pc.bm_lathe([(0.0, -0.001)] + [(RAIL_R * math.cos(math.radians(a)), RAIL_R * math.sin(math.radians(a))) for a in range(0, 91, 10)],
			segments=28, close_bottom=False)
		pc.orient_radial(cap, outward=True)
		pc.transform(cap, Matrix.Translation((X_RAIL, y_end, Z_RAIL)) @ Matrix.Rotation(math.radians(-90.0 * sgn), 4, "X"))
		asset.add(cap, "Brass_Worn", uv="cyl", touch=0.8)
	for k, by in enumerate(brackets):
		droop = 0.012 if k == loose else 0.0
		arm = pc.bm_tube_path([Vector((X_DIE, by, 0.285)), Vector((X_DIE + 0.07, by, 0.28 - droop * 0.3)), Vector((X_RAIL - 0.02, by, 0.245 - droop)),
			Vector((X_RAIL, by, Z_RAIL + RAIL_R * 0.4))], 0.011, 14)
		asset.add(arm, "Brass_Worn", uv="cyl", touch=0.3)
		flange = pc.bm_cylinder(0.032, 0.008, 24)
		pc.transform(flange, Matrix.Translation((X_DIE + 0.004, by, 0.285)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
		asset.add(flange, "Brass_Worn", uv="cyl")
		saddle = pc.bm_torus(RAIL_R + 0.003, 0.004, 28, 8)
		pc.transform(saddle, Matrix.Translation((X_RAIL, by, Z_RAIL)) @ Matrix.Rotation(math.radians(90), 4, "X"))
		asset.add(saddle, "Brass_Worn", uv="cyl")
	RAIL_PROBE[0] = (brackets[0] + brackets[1]) / 2.0
	for a, b in zip(brackets, brackets[1:]):
		asset.hull_box((2 * RAIL_R, b - a, 2 * RAIL_R), (X_RAIL, (a + b) / 2.0, Z_RAIL))


def underbar(asset: pc.Asset, rng) -> None:
	"""Bartender side: a brushed stainless underbar face with a speed rail; the rubber mats are the floor's (M2-A)."""
	face = pc.bm_box((0.012, Y_FLAP + HALF - 0.04, 0.84), (X_UNDERBAR, (Y_FLAP - HALF) / 2.0, 0.10 + 0.42))
	pc.bevel(face, 0.002, 1)
	asset.add(face, "Steel_Stainless", uv="box", grain="y", touch=0.2)
	kick = pc.bm_box((0.012, Y_FLAP + HALF - 0.04, 0.10), (X_UNDERBAR + 0.02, (Y_FLAP - HALF) / 2.0, 0.05))
	asset.add(kick, "Rubber_Black", uv="box", grain="y")
	for ys, ye in ((-3.3, -0.4), (0.4, 2.6)):
		trough = prism_xz([(X_UNDERBAR - 0.11, 0.60), (X_UNDERBAR - 0.11, 0.66), (X_UNDERBAR - 0.105, 0.66), (X_UNDERBAR - 0.105, 0.605),
			(X_UNDERBAR - 0.005, 0.605), (X_UNDERBAR - 0.005, 0.60)], ys, ye)
		asset.add(trough, "Steel_Stainless", uv="box", grain="y")
		for yb in (ys + 0.05, ye - 0.05):
			arm = pc.bm_box((0.11, 0.02, 0.004), (X_UNDERBAR - 0.055, yb, 0.598))
			asset.add(arm, "Steel_Stainless", uv="box")


def build(seed: int) -> pc.Asset:
	rng = rb_bl.rng("BarCounter", 0, seed)
	asset = pc.Asset("BarCounter", "Bar", "H08", "hero")
	top(asset, rng)
	armrest(asset, rng)
	die(asset, rng)
	half_door(asset, rng)
	foot_rail(asset, rng)
	underbar(asset, rng)
	asset.anchor("stool10_plate", (0.235, STOOL10_Y, Z_TOP))
	asset.anchor("service_flap_centre", (0.0, Y_FLAP + FLAP_W / 2.0, Z_TOP))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	if not pc.selected(a.only, "BarCounter"):
		return
	pc.clear_scene_keep_materials()
	asset = build(a.seed)
	obj = asset.build()
	checks = [
		asset.check_slice(Z_TOP - 0.02, Z_TOP + 0.0012, (None, LENGTH), pc.HERO_TOL, "top length (Y)"),
	]
	# the drink surface: highest vertex of the lacquered top away from the armrest and the plate
	tops = [(obj.matrix_world @ vv.co).z for vv in obj.data.vertices if X_BACK - 0.03 < vv.co.x < X_ARM0 - 0.001 and abs(vv.co.y - STOOL10_Y) > 0.1
		and vv.co.y < Y_FLAP - 0.1 and Z_TOP - 0.01 < vv.co.z < Z_TOP + 0.01]
	if not tops or abs(max(tops) - Z_TOP) > pc.HERO_TOL:
		rb_bl.fail(f"BarCounter drink surface {max(tops) if tops else None}, spec {Z_TOP}")
	checks.append({"what": "drink surface z", "measured_m": round(max(tops), 5), "spec_m": Z_TOP, "tolerance_m": pc.HERO_TOL})
	rails = [vv.co for vv in obj.data.vertices if abs(vv.co.x - X_RAIL) < RAIL_R + 0.001 and abs(vv.co.z - Z_RAIL) < RAIL_R + 0.003
		and abs(vv.co.y - RAIL_PROBE[0]) < 0.2]
	rz = [p.z for p in rails]
	rail_d = max(rz) - min(rz)
	if abs(rail_d - 2 * RAIL_R) > pc.HERO_TOL:
		rb_bl.fail(f"foot rail diameter {rail_d:.4f}")
	checks.append({"what": "foot rail diameter / centre z", "measured_m": [round(rail_d, 5), round((max(rz) + min(rz)) / 2.0, 5)],
		"spec_m": [2 * RAIL_R, Z_RAIL], "tolerance_m": pc.HERO_TOL})
	asset.export(out, (DEPTH, LENGTH, Z_ARM), acoustic="wood_panel", wm_res=2048, meta={
		"element": "E06",
		"spec_checks": checks,
		"drink_surface_z_m": Z_TOP, "armrest_crown_z_m": Z_ARM, "die_x_m": X_DIE, "foot_rail": {"x_m": X_RAIL, "z_m": Z_RAIL, "d_m": 2 * RAIL_R},
		"service_flap": {"blender_y_m": [round(Y_FLAP, 4), HALF], "note": "Blender +Y end = venue high X (E06 X 9.14 - 9.75)"},
		"placement_hint_ue_cm": {"location": [579.0, 199.5, 0.0], "yaw_deg": 90.0,
			"note": "E06: X 1.83 - 9.75, top Y 1.60 - 2.39; local +X (customer side) -> venue +Y"},
		"notes": "armrest rises 3 cm above the 1.07 m drink surface; Deacon's plate in front of stool #10"})
	if pc.want_preview():
		pc.preview(obj, "BarCounter", view=(1.0, 0.6, 0.35), zoom=0.35, target=(0.2, 2.6, 0.6))
	rb_bl.log("db_bar: ['BarCounter']")


main()
