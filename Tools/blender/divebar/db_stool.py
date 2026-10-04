"""H10 bar stools (procedural variants A / B / C) and the M22 spectator stools (venue-dive-bar 5.1 H10, 5.2 M22, E07 / E17).
Runs INSIDE Blender 5.2: python Tools/blender/rbbl.py run Tools/blender/divebar/db_stool.py [-- --seed 1958 --preview]

  BarStool_A    chrome 4-leg swivel stool, oxblood vinyl, TORN seat (foam exposed, curled vinyl edges)
  BarStool_B    the same frame, oxblood vinyl, a DUCT-TAPED tear (two crossing strips)
  BarStool_C    newer mismatched stool: black vinyl, chrome pedestal base with foot ring (a ball cannot roll under it)
  SpectatorStool  older wooden stool with rungs for the left-wall ledge (M22)

Spec dimensions asserted (VDB-T4, hero +-2 mm): bar stools seat diameter 0.38 m, seat top 0.76 m above the floor; spectator stool
seat top 0.76 m. Pivot at the floor-contact centre, front +X (the stools are round; the tear / tape faces the front). Hulls are
leg-accurate (4 legs + foot ring + seat), so a 57-60 mm ball rolls between the legs under the foot ring (13.5).
Owner: M2-B.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "common"))
sys.path.insert(0, str(HERE))
import rb_bl  # noqa: E402
import db_props_common as pc  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

SEAT_TOP = 0.760
SEAT_R = 0.190
BAND_Z = (0.655, 0.694)
FRAME_Z = 0.640
LEG_R = 0.0125
FOOT_R = 0.232          # leg foot radius from the axis (plan diagonal)
TOP_R = 0.128           # leg top radius at the frame ring
RING_Z = 0.300          # foot ring height
GLIDE_H = 0.014


def cushion_profile(dent: float, rng) -> list[tuple[float, float]]:
	"""Vinyl cushion from the band to the crown (bottom to top), max bulge exactly SEAT_R."""
	prof = [(0.1745, 0.690), (0.1830, 0.694), (0.1880, 0.702), (SEAT_R, 0.715), (0.1893, 0.724), (0.1870, 0.733), (0.1830, 0.741),
		(0.1770, 0.747), (0.1690, 0.7515)]
	r = 0.160
	while r > 0.0:
		z = SEAT_TOP - 0.0075 * (r / 0.17) ** 2 - dent * math.exp(-(r / 0.085) ** 2) + dent * 0.25
		prof.append((r, z))
		r -= 0.0085
	prof.append((0.0, SEAT_TOP - dent * 0.75))
	return prof


def top_height(r: float, dent: float) -> float:
	return SEAT_TOP - 0.0075 * (r / 0.17) ** 2 - dent * math.exp(-(r / 0.085) ** 2) + dent * 0.25


def seat(asset: pc.Asset, vinyl: str, dent: float, rng, tear=None, tape=None) -> None:
	import bmesh

	prof = cushion_profile(dent, rng)
	bm = pc.bm_lathe(prof, segments=72, close_bottom=True)
	# Sitting creases: a little radial waviness on the crown and the upper roll (1 mm).
	phase = rng.uniform(0.0, 6.28)
	for vert in bm.verts:
		r = math.hypot(vert.co.x, vert.co.y)
		if vert.co.z > 0.735 and r > 0.02:
			a = math.atan2(vert.co.y, vert.co.x)
			vert.co.z += 0.0009 * math.sin(7.0 * a + phase) * math.sin(math.pi * min(1.0, r / SEAT_R))
	if tear is not None:
		cx, cy, ax, ay, rot = tear
		foam_faces = []
		for face in bm.faces:
			c = face.calc_center_median()
			if c.z < 0.74:
				continue
			dx, dy = c.x - cx, c.y - cy
			cr, sr = math.cos(rot), math.sin(rot)
			u, w = dx * cr + dy * sr, -dx * sr + dy * cr
			ang = math.atan2(w / ay, u / ax)
			jag = 1.0 + 0.28 * math.sin(5.0 * ang + 1.3) + 0.14 * math.sin(11.0 * ang + 0.4)
			if (u / ax) ** 2 + (w / ay) ** 2 < jag * jag:
				foam_faces.append(face)
		inner = set()
		boundary = set()
		for face in foam_faces:
			for vert in face.verts:
				if all(f in foam_faces for f in vert.link_faces):
					inner.add(vert)
				else:
					boundary.add(vert)
		for vert in inner:
			vert.co.z -= 0.0065 + 0.0015 * math.sin(vert.co.x * 700.0) * math.sin(vert.co.y * 610.0)
		for vert in boundary:
			vert.co.z += 0.0018
		# Split: the foam faces become their own part (own material slot), the vinyl keeps the rest.
		bm.faces.ensure_lookup_table()
		foam_idx = {f.index for f in foam_faces}
		foam_bm = bm.copy()
		foam_bm.faces.ensure_lookup_table()
		bmesh.ops.delete(foam_bm, geom=[f for f in foam_bm.faces if f.index not in foam_idx], context="FACES")
		bmesh.ops.delete(bm, geom=list(foam_faces), context="FACES")
		asset.add(foam_bm, "Foam_Exposed", uv="box", touch=0.6)
	asset.add(bm, vinyl, uv="box", touch=0.0)
	# Touch: the crown and the front roll are where people sit and grab (a second, overlapping "touch" is not possible per
	# part, so the roll gets its own thin welt part with touch 1).
	welt = pc.bm_torus(0.1835, 0.0038, 72, 8)
	pc.translate(welt, (0.0, 0.0, 0.7435))
	asset.add(welt, vinyl, uv="box", touch=1.0)
	band = pc.bm_lathe([(0.0, BAND_Z[0] + 0.004), (0.150, BAND_Z[0] + 0.004), (0.1715, BAND_Z[0]), (0.1760, BAND_Z[0] + 0.004),
		(0.1768, BAND_Z[1] - 0.004), (0.1755, BAND_Z[1]), (0.170, BAND_Z[1])], segments=72, close_bottom=False)
	asset.add(band, "Chrome_Pitted", uv="cyl", touch=0.8)
	if tape is not None:
		for (cx, cy, length, width, rot) in tape:
			strip = tape_strip(cx, cy, length, width, rot, dent, rng)
			asset.add(strip, "Tape", uv="box", touch=0.7)


def tape_strip(cx, cy, length, width, rot, dent, rng):
	"""A duct-tape strip draped on the crown (0.6 mm above the vinyl) with small wrinkles and frayed ends."""
	import bmesh

	bm = bmesh.new()
	nu, nv = 24, 6
	grid = []
	cr, sr = math.cos(rot), math.sin(rot)
	for i in range(nu + 1):
		row = []
		s = (i / nu - 0.5) * length
		for j in range(nv + 1):
			t = (j / nv - 0.5) * width
			if i in (0, nu):
				t *= 0.92 + 0.08 * math.sin(j * 2.1)
			x = cx + s * cr - t * sr
			y = cy + s * sr + t * cr
			r = math.hypot(x, y)
			z = top_height(r, dent) + 0.0006 + 0.00035 * math.sin(s * 180.0 + t * 90.0) + (0.0012 if i in (0, nu) and j % 2 else 0.0)
			if r > 0.176:  # wraps over the roll edge
				z -= (r - 0.176) * 1.6
			row.append(bm.verts.new((x, y, z)))
		grid.append(row)
	for i in range(nu):
		for j in range(nv):
			bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]))
	# thickness 0.3 mm (so it is not a zero-thickness card)
	pc.thin_shell(bm, -0.0003)
	return bm


def leg_frame(asset: pc.Asset, rng, chrome: str) -> None:
	legs = []
	for k in range(4):
		a = math.radians(45.0 + 90.0 * k)
		top = Vector((TOP_R * math.cos(a), TOP_R * math.sin(a), FRAME_Z))
		foot = Vector((FOOT_R * math.cos(a), FOOT_R * math.sin(a), GLIDE_H))
		# slight bend at the top (formed tube) - 3 points
		mid = top.lerp(foot, 0.08) + Vector((0.0, 0.0, 0.008))
		asset.add(pc.bm_tube_path([top + Vector((0, 0, 0.006)), mid, foot], LEG_R, 14), chrome, uv="cyl", touch=0.2)
		glide = pc.bm_cylinder(0.0155, GLIDE_H, 16, (foot.x, foot.y, GLIDE_H * 0.5))
		pc.bevel(glide, 0.002, 2)
		asset.add(glide, "Plastic_Black", uv="box")
		legs.append((top, foot))
		asset.hull_segment(top, Vector((foot.x, foot.y, 0.0)), 0.017)
	# frame ring under the swivel
	ring = pc.bm_lathe([(TOP_R - 0.012, FRAME_Z - 0.004), (TOP_R + 0.016, FRAME_Z - 0.004), (TOP_R + 0.016, FRAME_Z + 0.008),
		(TOP_R - 0.012, FRAME_Z + 0.008)], segments=48, close_bottom=False)
	pc.bevel(ring, 0.0015, 1)
	asset.add(ring, chrome, uv="cyl")
	swivel = pc.bm_cylinder(0.105, 0.016, 40, (0.0, 0.0, FRAME_Z + 0.016))
	pc.bevel(swivel, 0.002, 2)
	asset.add(swivel, "Steel_Black", uv="box")
	# foot ring, welded to the outside of the legs at RING_Z
	t = (RING_Z - GLIDE_H) / (FRAME_Z - GLIDE_H)
	leg_r_at = FOOT_R + (TOP_R - FOOT_R) * t
	ring_r = leg_r_at + LEG_R + 0.0095 - 0.002
	foot_ring = pc.bm_torus(ring_r, 0.0095, 72, 12)
	pc.translate(foot_ring, (0.0, 0.0, RING_Z))
	asset.add(foot_ring, chrome, uv="cyl", touch=1.0)
	for k in range(8):
		a0 = 2.0 * math.pi * k / 8
		a1 = 2.0 * math.pi * (k + 1) / 8
		asset.hull_segment((ring_r * math.cos(a0), ring_r * math.sin(a0), RING_Z), (ring_r * math.cos(a1), ring_r * math.sin(a1), RING_Z), 0.012, 6)


def pedestal_frame(asset: pc.Asset, rng, chrome: str) -> None:
	base = pc.bm_lathe([(0.0, 0.0), (0.228, 0.0), (0.230, 0.006), (0.222, 0.016), (0.18, 0.030), (0.08, 0.046), (0.036, 0.052),
		(0.030, 0.056), (0.0, 0.056)], segments=72, close_bottom=True)
	asset.add(base, chrome, uv="cyl", touch=0.3)
	rubber = pc.bm_lathe([(0.0, 0.0), (0.2285, 0.0), (0.2285, 0.004), (0.0, 0.004)], segments=72)
	asset.add(rubber, "Plastic_Black", uv="box")
	column = pc.bm_cylinder(0.031, FRAME_Z - 0.05, 32, (0.0, 0.0, 0.05 + (FRAME_Z - 0.05) * 0.5), cap=True)
	asset.add(column, chrome, uv="cyl")
	collar = pc.bm_cylinder(0.042, 0.03, 32, (0.0, 0.0, FRAME_Z - 0.02))
	pc.bevel(collar, 0.003, 2)
	asset.add(collar, chrome, uv="cyl")
	ring_r = 0.200
	foot_ring = pc.bm_torus(ring_r, 0.0105, 72, 12)
	pc.translate(foot_ring, (0.0, 0.0, RING_Z))
	asset.add(foot_ring, chrome, uv="cyl", touch=1.0)
	for k in range(3):
		a = math.radians(90.0 + 120.0 * k)
		spoke = pc.bm_tube_path([(0.028 * math.cos(a), 0.028 * math.sin(a), RING_Z), (ring_r * math.cos(a), ring_r * math.sin(a), RING_Z)], 0.008, 10)
		asset.add(spoke, chrome, uv="cyl")
	swivel = pc.bm_cylinder(0.105, 0.016, 40, (0.0, 0.0, FRAME_Z + 0.016))
	pc.bevel(swivel, 0.002, 2)
	asset.add(swivel, "Steel_Black", uv="box")
	asset.hull_cylinder(0.230, 0.0, 0.056, segments=12)
	asset.hull_segment((0.0, 0.0, 0.05), (0.0, 0.0, FRAME_Z), 0.032, 8)
	for k in range(8):
		a0 = 2.0 * math.pi * k / 8
		a1 = 2.0 * math.pi * (k + 1) / 8
		asset.hull_segment((ring_r * math.cos(a0), ring_r * math.sin(a0), RING_Z), (ring_r * math.cos(a1), ring_r * math.sin(a1), RING_Z), 0.012, 6)


def bar_stool(variant: str, seed: int) -> pc.Asset:
	rng = rb_bl.rng(f"BarStool_{variant}", 0, seed)
	asset = pc.Asset(f"BarStool_{variant}", "Stools", "H10", "hero")
	if variant == "A":
		leg_frame(asset, rng, "Chrome_Pitted")
		seat(asset, "Vinyl_Oxblood", dent=0.0045, rng=rng, tear=(0.045, -0.035, 0.062, 0.026, math.radians(28.0)))
	elif variant == "B":
		leg_frame(asset, rng, "Chrome_Pitted")
		seat(asset, "Vinyl_Oxblood", dent=0.0040, rng=rng, tape=[(0.03, 0.02, 0.15, 0.048, math.radians(-15.0)),
			(0.06, -0.01, 0.11, 0.048, math.radians(62.0))])
	else:
		pedestal_frame(asset, rng, "Chrome")
		seat(asset, "Vinyl_Black", dent=0.0015, rng=rng)
	asset.hull_cylinder(SEAT_R, FRAME_Z - 0.008, SEAT_TOP, segments=12)
	return asset


def spectator_stool(seed: int) -> pc.Asset:
	"""M22: older wooden bar-height stool, round seat (worn top), 4 splayed square legs, two rungs."""
	rng = rb_bl.rng("SpectatorStool", 0, seed)
	asset = pc.Asset("SpectatorStool", "Stools", "M22", "mid")
	seat_r, seat_t = 0.180, 0.034
	top_z = SEAT_TOP
	bm = pc.bm_lathe([(0.0, top_z - seat_t), (seat_r - 0.006, top_z - seat_t), (seat_r, top_z - seat_t + 0.008), (seat_r, top_z - 0.010),
		(seat_r - 0.004, top_z - 0.002), (seat_r - 0.012, top_z), (0.0, top_z - 0.0025)], segments=64, close_bottom=True)
	# worn saddle: the middle sits 2.5 mm lower (already in the profile), plus a slight tilt of the old joint
	pc.rotate(bm, rng.uniform(0.4, 0.9), "Y", (0.0, 0.0, top_z))
	asset.add(bm, "Wood_Stained", uv="box", grain="x", touch=0.9)
	apron_z = top_z - seat_t - 0.02
	leg_top_r, leg_foot_r, leg_w = 0.125, 0.205, 0.036
	feet = []
	for k in range(4):
		a = math.radians(45.0 + 90.0 * k)
		top = Vector((leg_top_r * math.cos(a), leg_top_r * math.sin(a), top_z - seat_t))
		foot = Vector((leg_foot_r * math.cos(a), leg_foot_r * math.sin(a), 0.0))
		axis = top - foot
		length = axis.length
		# square leg along the leg axis (box built along +Z, then rotated onto the axis)
		rot = axis.normalized().to_track_quat("Z", "X").to_matrix().to_4x4()
		leg = pc.bm_box((leg_w, leg_w, length), (0.0, 0.0, length * 0.5))
		pc.bevel(leg, 0.003, 2)
		pc.transform(leg, Matrix.Translation(foot) @ rot)
		# the foot is cut flat on the floor: clamp the verts that went below z = 0
		for vert in leg.verts:
			vert.co.z = max(0.0, vert.co.z)
		asset.add(leg, "Wood_Stained", uv="grain", grain="z", touch=0.3)
		feet.append((top, foot))
		asset.hull_segment(foot, top, 0.024, 6)
	for z_r, touch in ((0.26, 1.0), (0.46, 0.4)):
		t = z_r / (top_z - seat_t)
		pts = []
		for k in range(4):
			top, foot = feet[k]
			pts.append(foot.lerp(top, t))
		for k in range(4):
			a, b = pts[k], pts[(k + 1) % 4]
			rung = pc.bm_tube_path([a, b], 0.011, 10)
			asset.add(rung, "Wood_Stained", uv="cyl", touch=touch)
	# apron ring under the seat
	apron = pc.bm_lathe([(0.12, apron_z), (0.145, apron_z), (0.145, top_z - seat_t), (0.12, top_z - seat_t)], segments=48, close_bottom=False)
	asset.add(apron, "Wood_Stained", uv="cyl")
	asset.hull_cylinder(seat_r, apron_z, top_z, segments=12)
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	built = []
	for variant in ("A", "B", "C"):
		asset_id = f"BarStool_{variant}"
		if not pc.selected(a.only, asset_id):
			continue
		pc.clear_scene_keep_materials()
		asset = bar_stool(variant, a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		size = hi - lo
		rb_bl.log(f"{asset_id}: bounds {tuple(round(x, 4) for x in size)}")
		# Spec (H10 / E07): seat diameter 0.38, seat top 0.76 (+-2 mm). The footprint (foot ring / base) is the design value.
		seat_check = asset.check_slice(0.69, 0.80, (2 * SEAT_R, 2 * SEAT_R), pc.HERO_TOL, "seat diameter")
		asset.export(out, (size.x, size.y, SEAT_TOP), acoustic="vinyl_upholstery", meta={
			"element": "E07", "variant": variant, "spec_checks": [seat_check],
			"seat_top_m": SEAT_TOP, "seat_diameter_m": 2 * SEAT_R,
			"notes": {"A": "torn seat, foam exposed", "B": "duct-taped tear", "C": "newer mismatched, pedestal base"}[variant]})
		if pc.want_preview():
			pc.preview(obj, asset_id)
		built.append(asset_id)
	if pc.selected(a.only, "SpectatorStool"):
		pc.clear_scene_keep_materials()
		asset = spectator_stool(a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		size = hi - lo
		# the seat top is the spec value; the footprint is the design value of the splayed legs
		if abs(hi.z - SEAT_TOP) > pc.HERO_TOL:
			rb_bl.fail(f"SpectatorStool seat top {hi.z:.4f} m, spec {SEAT_TOP}")
		asset.export(out, (size.x, size.y, SEAT_TOP), acoustic="wood_panel", meta={"element": "E17", "seat_top_m": SEAT_TOP})
		if pc.want_preview():
			pc.preview(obj, "SpectatorStool")
		built.append("SpectatorStool")
	rb_bl.log(f"db_stool: {built}")


main()
