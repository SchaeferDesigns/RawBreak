"""H05 wall cue rack with its prop house cues and the mechanical bridge, H06 chalk cube (venue-dive-bar E12, 5.1 H04 - H06).
Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_cue_rack.py [-- --seed 1958 --preview --no-bake]

CueRack   E12: 1.00 m wide on the right wall (X 13.30 - 14.30), 0.12 m deep; stained frame from 0.20 to 1.60 m: a butt rest with ten
          felt-lined cups at 0.25 m, a top holder with ten spring clips at 1.40 m, side rails. Eight house cues (4 x 57 in, 2 x 52 in,
          1 x 48 in, 1 x 36 in; the playable cue is M2-F's, these are props) in eight of the ten slots, the mechanical bridge on two
          hooks at the right end: maple shafts, dark stained butts with black rubber bumpers, ivory-white ferrules, blue-chalked
          tips, electrical tape around the short cues' butts (H04), every cue leaning a little differently (9.2). The cue tips stand
          above the frame (a 57 in cue in a 0.25 m cup reaches 1.70 m; E12's "cues to 1.60" is the frame).
          Wall item: pivot at the wall plane, bottom centre (z = 0 = floor), front +X into the room (the level yaws it to face -Y).
ChalkCube H06: 22 mm bar-grade cube, cupped face (4 mm, HF-23 wear), the torn RAIL RAT paper wrapper (TXT) on four sides.
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

WIDTH = 1.00
DEPTH = 0.12
Z_FRAME = (0.20, 1.60)
Z_CUP = 0.25
Z_CLIP = 1.40
SLOTS = 10
INCH = 0.0254
CUES = [57, 57, 57, 57, 52, 52, 48, 36]           # house cues in slots 0..7 (two slots empty: those cues are in play)


def cue(asset: pc.Asset, length_in: float, base: Vector, top_lean: Vector, rng, taped: bool) -> None:
	"""A one-piece house cue standing on its bumper at base, leaning toward top_lean (unit direction)."""
	L = length_in * INCH
	axis = top_lean.normalized()
	r_butt, r_tip = 0.0148, 0.0065
	# radius along the cue: linear taper over the butt, a pro taper over the last 0.3 m of the shaft
	sleeve = min(0.45, L * 0.33)
	prof = [(0.0, 0.0), (r_butt - 0.002, 0.0), (r_butt - 0.0005, 0.004), (r_butt - 0.0005, 0.018)]
	n = 16
	for i in range(n + 1):
		s = 0.018 + (L - 0.042 - 0.018) * i / n
		t = s / L
		r = r_butt + (r_tip - r_butt) * min(1.0, t / 0.75)        # linear butt taper, then the straight "pro taper" shaft
		prof.append((max(r, r_tip), s))
	body = pc.bm_lathe(prof, segments=20, close_bottom=False)
	pc.orient_radial(body, outward=True)
	# split the body into butt sleeve (stained) and shaft (maple) by z
	butt = body.copy()
	bmesh.ops.delete(butt, geom=[f for f in butt.faces if f.calc_center_median().z > sleeve], context="FACES")
	bmesh.ops.delete(body, geom=[f for f in body.faces if f.calc_center_median().z <= sleeve], context="FACES")
	bumper = pc.bm_lathe([(0.0, -0.001), (r_butt - 0.003, -0.001), (r_butt, 0.004), (r_butt, 0.016), (0.0, 0.016)], segments=20)
	ferrule = pc.bm_cylinder(r_tip + 0.0002, 0.022, 20, (0.0, 0.0, L - 0.042 + 0.011))
	tip = pc.bm_lathe([(r_tip, L - 0.020), (r_tip - 0.0002, L - 0.012), (r_tip - 0.001, L - 0.0035), (0.0, L)], segments=20, close_bottom=False)
	pc.orient_radial(tip, outward=True)
	parts = [(body, "CueShaft_Maple", 0.9), (butt, "Wood_Stained", 1.0), (bumper, "Rubber_Black", 0.2), (ferrule, "Plastic_White", 0.4),
		(tip, "Chalk_Blue", 0.0)]
	if taped:
		tape = pc.bm_cylinder(r_butt + 0.0006, 0.11, 20, (0.0, 0.0, 0.10))
		parts.append((tape, "Tape_Electrical", 0.8))
	rot = axis.to_track_quat("Z", "X").to_matrix().to_4x4()
	spin = Matrix.Rotation(rng.uniform(0, 6.28), 4, "Z")
	m = Matrix.Translation(base) @ rot @ spin
	for bm, slot, touch in parts:
		pc.transform(bm, m)
		asset.add(bm, slot, uv="cyl" if slot != "Tape_Electrical" else "cyl", touch=touch, smooth_angle=60.0)


def bridge(asset: pc.Asset, base: Vector, lean: Vector) -> None:
	"""Mechanical bridge: a 1.52 m stick with a chrome bridge head (the head is up), on two hooks."""
	L = 1.52
	axis = lean.normalized()
	rot = axis.to_track_quat("Z", "X").to_matrix().to_4x4()
	m = Matrix.Translation(base) @ rot
	stick = pc.bm_lathe([(0.0, 0.0), (0.0125, 0.0), (0.0135, 0.01), (0.0110, L - 0.05), (0.0100, L - 0.02), (0.0, L - 0.02)], 16)
	pc.orient_radial(stick, outward=True)
	pc.transform(stick, m)
	asset.add(stick, "Wood_Stained", uv="cyl", touch=0.8)
	# head: a flat plate with three notches, 0.13 wide
	poly = [(-0.065, 0.0), (0.065, 0.0), (0.065, 0.045), (0.05, 0.045), (0.045, 0.03), (0.035, 0.03), (0.03, 0.045), (0.01, 0.045),
		(0.005, 0.07), (-0.005, 0.07), (-0.01, 0.045), (-0.03, 0.045), (-0.035, 0.03), (-0.045, 0.03), (-0.05, 0.045), (-0.065, 0.045)]
	head = pc.bm_extrude_polygon(poly, -0.004, 0.004)
	for vert in head.verts:
		x, zz, t = vert.co.x, vert.co.y, vert.co.z
		vert.co = Vector((t, x, L - 0.03 + zz))        # plate in the YZ plane (faces +X)
	bmesh.ops.recalc_face_normals(head, faces=head.faces)
	pc.bevel(head, 0.0015, 1)
	pc.transform(head, m)
	asset.add(head, "Chrome", uv="box", touch=0.6)


def rack(seed: int) -> pc.Asset:
	rng = rb_bl.rng("CueRack", 0, seed)
	asset = pc.Asset("CueRack", "CueRack", "H05", "hero")
	w2 = WIDTH / 2.0
	# frame: side rails, backboard strips, butt rest (with cups), top holder (with clips), a crown
	for sy in (-w2 + 0.025, w2 - 0.025):
		rail = pc.bm_box((0.03, 0.05, Z_FRAME[1] - Z_FRAME[0]), (0.015, sy, (Z_FRAME[0] + Z_FRAME[1]) / 2.0))
		pc.bevel(rail, 0.004, 2)
		asset.add(rail, "Wood_Stained", uv="box", grain="z", touch=0.6)
	for zc, h in ((Z_FRAME[0] + 0.05, 0.10), (Z_CLIP, 0.09), (Z_FRAME[1] - 0.03, 0.06)):
		strip = pc.bm_box((0.02, WIDTH, h), (0.01, 0.0, zc))
		pc.bevel(strip, 0.003, 2)
		asset.add(strip, "Wood_Stained", uv="box", grain="y")
	rest = pc.bm_box((DEPTH - 0.01, WIDTH, 0.05), ((DEPTH - 0.01) / 2.0 + 0.005, 0.0, Z_CUP - 0.025))
	pc.bevel(rest, 0.005, 2)
	asset.add(rest, "Wood_Stained", uv="box", grain="y", touch=0.5)
	holder = pc.bm_box((DEPTH - 0.02, WIDTH, 0.035), ((DEPTH - 0.02) / 2.0 + 0.005, 0.0, Z_CLIP + 0.02))
	pc.bevel(holder, 0.005, 2)
	asset.add(holder, "Wood_Stained", uv="box", grain="y", touch=0.5)
	crown = pc.bm_box((DEPTH - 0.04, WIDTH, 0.025), ((DEPTH - 0.04) / 2.0, 0.0, Z_FRAME[1] - 0.0125))
	pc.bevel(crown, 0.004, 2)
	asset.add(crown, "Wood_Stained", uv="box", grain="y")
	asset.hull_box((DEPTH, WIDTH, Z_FRAME[1] - Z_FRAME[0]), (DEPTH / 2.0, 0.0, (Z_FRAME[0] + Z_FRAME[1]) / 2.0))
	slot_y = [-w2 + 0.09 + (WIDTH - 0.18) * k / (SLOTS - 1) for k in range(SLOTS)]
	x_slot = 0.07
	for y in slot_y:
		cup = pc.bm_lathe([(0.0, Z_CUP - 0.002), (0.019, Z_CUP - 0.002), (0.021, Z_CUP + 0.012), (0.024, Z_CUP + 0.014), (0.024, Z_CUP + 0.002)],
			segments=20, close_bottom=True)
		pc.translate(cup, (x_slot, y, 0.0))
		asset.add(cup, "Felt_Green", uv="cyl")
		clip = pc.bm_tube_path([Vector((0.05, y - 0.012, Z_CLIP + 0.038)), Vector((0.085, y - 0.014, Z_CLIP + 0.040)), Vector((0.093, y - 0.004, Z_CLIP + 0.040)),
			Vector((0.093, y + 0.004, Z_CLIP + 0.040)), Vector((0.085, y + 0.014, Z_CLIP + 0.040)), Vector((0.05, y + 0.012, Z_CLIP + 0.038))], 0.0022, 8)
		asset.add(clip, "Chrome", uv="cyl", touch=0.8)
	# the house cues (lean: the top a few cm toward the wall and sideways; 9.2)
	for k, length in enumerate(CUES):
		slot = [0, 1, 2, 4, 5, 6, 7, 8][k]              # slots 3 and 9 are empty
		base = Vector((x_slot + rng.uniform(-0.003, 0.003), slot_y[slot] + rng.uniform(-0.003, 0.003), Z_CUP + 0.001))
		top = Vector((x_slot - 0.010 + rng.uniform(-0.004, 0.004), slot_y[slot] + rng.uniform(-0.012, 0.012), Z_CUP + 1.0))
		cue(asset, length, base, top - base, rng, taped=length < 57)
	# mechanical bridge in slot 9 on two hooks
	for zh in (0.70, 1.10):
		hook = pc.bm_tube_path([Vector((0.02, slot_y[9] + 0.03, zh)), Vector((0.075, slot_y[9] + 0.03, zh)), Vector((0.09, slot_y[9] + 0.03, zh + 0.02))],
			0.0035, 8)
		asset.add(hook, "Chrome", uv="cyl")
	bridge(asset, Vector((x_slot, slot_y[9], Z_CUP + 0.001)), Vector((-0.004, 0.003, 1.0)))
	asset.anchor("slot_empty_3", (x_slot, slot_y[3], Z_CUP))
	asset.anchor("slot_empty_9", (x_slot, slot_y[9], Z_CUP))
	return asset


def chalk(seed: int) -> pc.Asset:
	rng = rb_bl.rng("ChalkCube", 0, seed)
	asset = pc.Asset("ChalkCube", "Chalk", "H06", "hero")
	s = 0.022
	cup_depth = 0.004
	bm = pc.bm_box((s, s, s), (0.0, 0.0, s / 2.0))
	bmesh.ops.subdivide_edges(bm, edges=list(bm.edges), cuts=10, use_grid_fill=True)
	pc.bevel(bm, 0.0008, 1, angle_deg=40.0)
	cx, cy = rng.uniform(-0.0015, 0.0015), rng.uniform(-0.0015, 0.0015)

	def cupped(p):
		if p.z > s - 1e-5:
			r = math.hypot(p.x - cx, p.y - cy)
			d = cup_depth * max(0.0, 1.0 - (r / 0.0095) ** 2)
			return p - Vector((0.0, 0.0, d))
		return p
	pc.deform(bm, cupped)
	asset.add(bm, "Chalk_Blue", uv="box")
	# wrapper: paper on the four sides, the top edge torn (a few mm below the rim, ragged)
	lab = pc.label_rect("chalk_rail_rat")
	for k in range(4):
		ang = math.radians(90.0 * k)

		def pt(u, v, ang=ang):
			tear = 0.0025 + 0.0012 * math.sin(u * 900.0 + ang * 3.0)
			z = 0.001 + (s - 0.001 - tear) * (v / s + 0.5)
			p = Vector((s / 2.0 + 0.00025, u, z))
			return Matrix.Rotation(ang, 3, "Z") @ p
		patch = pc.surface_patch(pt, (-s / 2.0, s / 2.0), (-s / 2.0, s / 2.0), 10, 4, lab)
		asset.add(patch, "Label_Atlas", uv="none")
	asset.hull_box((s, s, s), (0.0, 0.0, s / 2.0))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	built = []
	if pc.selected(a.only, "CueRack"):
		pc.clear_scene_keep_materials()
		asset = rack(a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		checks = [asset.check_slice(Z_FRAME[0] + 0.01, Z_FRAME[1] - 0.005, (None, WIDTH), pc.HERO_TOL, "frame width")]
		frame_z = [vv.co.z for vv in obj.data.vertices if abs(vv.co.y) > WIDTH / 2.0 - 0.03]
		if abs(min(frame_z) - Z_FRAME[0]) > pc.HERO_TOL or abs(max(frame_z) - Z_FRAME[1]) > pc.HERO_TOL:
			rb_bl.fail(f"CueRack frame z {min(frame_z):.4f} .. {max(frame_z):.4f}, spec {Z_FRAME}")
		if hi.x > DEPTH + pc.HERO_TOL:
			rb_bl.fail(f"CueRack protrudes {hi.x:.4f} m, spec {DEPTH}")
		checks.append({"what": "frame z range / depth", "measured_m": [round(min(frame_z), 4), round(max(frame_z), 4), round(hi.x, 4)],
			"spec_m": [Z_FRAME[0], Z_FRAME[1], DEPTH], "tolerance_m": pc.HERO_TOL})
		asset.export(out, (hi.x - lo.x, hi.y - lo.y, hi.z - lo.z), pivot="wall_plane_bottom_centre", acoustic="wood_panel", meta={
			"element": "E12", "spec_checks": checks, "frame_z_m": list(Z_FRAME), "cues_in": CUES, "cup_z_m": Z_CUP,
			"placement_hint_ue_cm": {"location": [1380.0, 732.0, 0.0], "yaw_deg": -90.0,
				"note": "E12 X 13.30 - 14.30 on the right wall (Y 7.32); front faces -Y"},
			"notes": "prop cues; TS-2: the level may tag the component for the cue-sweep clatter"})
		if pc.want_preview():
			pc.preview(obj, "CueRack", view=(1.0, -0.5, 0.25), zoom=0.8)
		built.append("CueRack")
	if pc.selected(a.only, "ChalkCube"):
		pc.clear_scene_keep_materials()
		asset = chalk(a.seed)
		obj = asset.build()
		asset.export(out, (0.022, 0.022, 0.022), acoustic="none", collision_profile="RbVenueProp", wm_res=128, meta={
			"element": "H06", "cup_depth_m": 0.004, "notes": "one on each long rail, one on the C3 shelf, one under the table (M2-A)"})
		if pc.want_preview():
			pc.preview(obj, "ChalkCube", view=(1.0, -0.8, 1.0), zoom=2.2)
		built.append("ChalkCube")
	rb_bl.log(f"db_cue_rack: {built}")


main()
