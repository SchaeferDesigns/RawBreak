"""Wall signs and paper of the dive bar, text only until the DB-5 art (venue-dive-bar 1.3 S1 / S5 / S19 / S20, 5.2 M15 / M25, 8.2).
Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_signs.py [-- --seed 1958 --preview --no-bake --only Sign_CashOnly]

Every readable word comes from Tools/art/text_textures.py (the label atlas, fictional brands only; 8.2: text is ours). Four builds:
  metal      the LOW CLEARANCE 10'-6" road sign (S1 / M25): 1.5 mm steel on four bolts with spacers, the top-right corner bent
             forward where the truck hit it, rusty painted back and edges (MI_DB_Tin_Painted), sheeting face MI_DB_Label_Gloss
  plastic    engraved two-ply plastic plates (CASH ONLY, NO SMOKING, RESTROOMS): 3 mm, rounded corners, two screws
  paper      laser-printed / hand-lettered sheets and flyers (HOUSE RULES, league night, band flyer): 0.25 mm, taped at the top
             corners with masking tape, a bottom corner curling off the wall, a little waviness (9.2)
  cardboard  a torn piece of beer-case cardboard (SHOT & A BEER $6): 3 mm, irregular torn edge, taped
Wall items: pivot on the wall plane, horizontally centred, z = 0 = FLOOR level (the heights below are the mounting heights of
venue-dive-bar 2.3); FRONT = +X into the room. placement_hint_ue_cm in each <Asset>.json is the dev / M2-A placement.
Owner: M2-B (new generator; the architect adds it to db_build_all.py).
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

# asset id, label, build, (width, height) m, centre height m, spec id, placement hint (UE cm, yaw), what
SIGNS = [
	("Sign_LowClearance", "sign_low_clearance", "metal", (0.914, 0.457), 2.40, "M25",
		{"location": [1646.0, 85.0, 0.0], "yaw_deg": 180.0, "note": "S1 over the corridor opening E18 (back wall Y 0.30 - 1.40, top 2.13)"}),
	("Sign_CashOnly", "sign_cash_only", "plastic", (0.300, 0.150), 1.62, "S19",
		{"location": [120.0, 732.0, 0.0], "yaw_deg": -90.0, "note": "right wall above the ATM E03 (X 0.45 - 0.95), faces -Y"}),
	("Sign_NoSmoking", "sign_no_smoking", "plastic", (0.203, 0.254), 1.55, "S5",
		{"location": [0.0, 520.0, 0.0], "yaw_deg": 0.0, "note": "front wall beside the entrance door E02 (Y 5.64 - 6.55), faces +X"}),
	("Sign_HouseRules", "sign_house_rules", "paper", (0.216, 0.279), 1.46, "S20",
		{"location": [1195.0, 732.0, 0.0], "yaw_deg": -90.0, "note": "right wall left of the chalkboard E11 (X 12.30 - 13.20), faces -Y"}),
	("Flyer_League", "flyer_league", "paper", (0.279, 0.432), 1.50, "M15",
		{"location": [985.0, 732.0, 0.0], "yaw_deg": -90.0, "note": "right wall between the dart machine and the jukebox, faces -Y"}),
	("Flyer_Band", "flyer_band", "paper", (0.279, 0.432), 1.43, "M15",
		{"location": [1035.0, 732.0, 0.0], "yaw_deg": -90.0, "note": "overlapping the league flyer, faces -Y"}),
	("Sign_Restrooms", "sign_restrooms", "plastic", (0.360, 0.100), 2.00, "E18",
		{"location": [1590.0, 0.0, 0.0], "yaw_deg": 90.0, "note": "left wall by the corridor opening, the arrow points to it (+X), faces +Y"}),
	("Sign_ShotAndBeer", "sign_shot_and_beer", "cardboard", (0.340, 0.230), 1.52, "S19",
		{"location": [963.0, 0.0, 0.0], "yaw_deg": 90.0, "note": "left wall just past the back bar end (X 9.45), faces +Y"}),
]


def rounded_rect(w: float, h: float, r: float, seg: int = 5) -> list[tuple[float, float]]:
	"""Rounded rectangle outline (y, z) centred on 0, counter-clockwise seen from +X."""
	pts = []
	for cy, cz, a0 in ((w / 2 - r, -h / 2 + r, -90.0), (w / 2 - r, h / 2 - r, 0.0), (-w / 2 + r, h / 2 - r, 90.0), (-w / 2 + r, -h / 2 + r, 180.0)):
		for k in range(seg + 1):
			a = math.radians(a0 + 90.0 * k / seg)
			pts.append((cy + r * math.cos(a), cz + r * math.sin(a)))
	return pts


def plate(outline, x0: float, x1: float, zc: float) -> bmesh.types.BMesh:
	"""A flat plate from an outline in the wall plane (y, z), extruded from x0 (back) to x1 (front)."""
	bm = pc.bm_extrude_polygon(outline, x0, x1)
	# bm_extrude_polygon builds in (x, y) and extrudes along z: map (a, b, t) -> (t, a, zc + b)
	for vert in bm.verts:
		a, b, t = vert.co.x, vert.co.y, vert.co.z
		vert.co = Vector((t, a, zc + b))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def record_face(asset: pc.Asset, bm: bmesh.types.BMesh) -> None:
	"""Remembers the extent of the printed face (y, z in the wall plane) for the VDB-T4 face check: the asset's bounding box also
	holds the tape that overlaps the wall, the screws and the bent corner, which are not the sign's size."""
	ys = [vert.co.y for vert in bm.verts]
	zs = [vert.co.z for vert in bm.verts]
	asset.face_bounds = (min(ys), max(ys), min(zs), max(zs))


def sheet(asset: pc.Asset, name: str, w: float, h: float, zc: float, x_fn, t: float, material: str, nu: int = 1, nv: int = 1, keep=None,
		edge_material: str | None = None) -> None:
	"""A printed sheet of thickness t whose front face carries the label (x_fn(u, v) = the front's distance from the wall): the front
	as one part (label UVs), the back and the rim as another (edge_material, box UVs) sharing the same grid."""
	lab = pc.label_rect(name)

	def pt(u: float, v: float) -> Vector:
		return Vector((x_fn(u, v), u, zc + v))
	front = pc.surface_patch(pt, (-w / 2, w / 2), (-h / 2, h / 2), nu, nv, lab, keep)
	record_face(asset, front)
	asset.add(front, material, uv="none", touch=0.2)
	shell = pc.surface_patch(pt, (-w / 2, w / 2), (-h / 2, h / 2), nu, nv, (0.0, 0.0, 1.0, 1.0), keep)
	count = len(shell.faces)
	pc.thin_shell(shell, -t)
	shell.faces.ensure_lookup_table()
	bmesh.ops.delete(shell, geom=[shell.faces[i] for i in range(count)], context="FACES_ONLY")
	asset.add(shell, edge_material or material, uv="box", touch=0.1)


def screw(asset: pc.Asset, y: float, z: float, x: float, r: float = 0.0045) -> None:
	head = pc.bm_lathe([(0.0, 0.0), (r, 0.0), (r * 0.9, 0.0012), (r * 0.5, 0.0022), (0.0, 0.0025)], segments=16, close_bottom=True)
	pc.transform(head, Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
	asset.add(head, "Steel_Zinc", uv="cyl")


def tape(asset: pc.Asset, y: float, z: float, ang_deg: float, x_at, length: float = 0.075, width: float = 0.024) -> None:
	"""A strip of masking tape over a sheet corner (follows the sheet / wall surface, 0.15 mm thick)."""
	a = math.radians(ang_deg)
	nu, nv = 6, 2
	bm = bmesh.new()
	grid = []
	for i in range(nu + 1):
		row = []
		s = (i / nu - 0.5) * length
		for j in range(nv + 1):
			t = (j / nv - 0.5) * width
			yy = y + s * math.cos(a) - t * math.sin(a)
			zz = z + s * math.sin(a) + t * math.cos(a)
			row.append(bm.verts.new((x_at(yy, zz) + 0.0004, yy, zz)))
		grid.append(row)
	for i in range(nu):
		for j in range(nv):
			bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
	pc.thin_shell(bm, -0.00015)
	asset.add(bm, "Paper_Coaster", uv="box", touch=0.3)


def build_metal(asset: pc.Asset, name: str, w: float, h: float, zc: float, rng) -> float:
	standoff, t = 0.012, 0.0015
	bend_a = math.radians(rng.uniform(7.0, 11.0))

	def bend(y: float, z: float) -> float:
		# the top-right corner (seen from the room) is bent forward along a diagonal fold line
		d = (y - (w / 2 - 0.16)) * 0.7 + (z - zc - (h / 2 - 0.12)) * 0.7
		return max(0.0, d) * math.tan(bend_a)

	def front(u: float, v: float) -> float:
		return standoff + t + bend(u, zc + v) + 0.002 * math.sin(u * 3.1) * math.cos(v * 4.0)

	# the steel sheet as a grid (so the fold and the dents are real geometry); the sheeting IS the front face (no layered label: Nanite
	# merges surfaces a fraction of a millimetre apart, and a covered face bakes black AO); the back faces the wall
	sheet(asset, name, w, h, zc, front, t, "Label_Gloss", 16, 8, edge_material="Tin_Painted")
	for sy, sz in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
		y, z = sy * (w / 2 - 0.055), zc + sz * (h / 2 - 0.055)
		spacer = pc.bm_cylinder(0.009, standoff, 12)
		pc.transform(spacer, Matrix.Translation((standoff / 2, y, z)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
		asset.add(spacer, "Steel_Zinc", uv="cyl")
		screw(asset, y, z, front(y, z - zc) + 0.0002, 0.008)
	return standoff + t + 0.02


def build_plastic(asset: pc.Asset, name: str, w: float, h: float, zc: float, rng, dark: bool) -> float:
	t = 0.003
	# the engraved face is the plate's front; a 0.8 mm backing a little smaller than the face (in the wall's shadow) gives the edge
	sheet(asset, name, w, h, zc, lambda u, v: t, 0.0012, "Label_Gloss", edge_material="Plastic_Black" if dark else "Plastic_White")
	back = plate(rounded_rect(w - 0.004, h - 0.004, 0.008), 0.0, t - 0.0013, zc)
	asset.add(back, "Plastic_Black" if dark else "Plastic_White", uv="box", touch=0.3)
	for sy in (-1, 1):
		screw(asset, sy * (w / 2 - 0.02), zc + (h / 2 - 0.02 if h > 0.2 else 0.0), t + 0.0002)
	return t + 0.003


def build_paper(asset: pc.Asset, name: str, w: float, h: float, zc: float, rng) -> float:
	curl_corner = rng.choice((-1, 1))
	ph = rng.uniform(0, 6.28)

	def x_at(y: float, z: float) -> float:
		v = (z - zc) / h + 0.5            # 0 bottom .. 1 top
		u = y / w + 0.5
		wave = 0.0012 * (1.0 + math.sin(u * 7.0 + ph) * math.sin(v * 5.0 + ph * 0.5))
		# one bottom corner curls off the wall (up to 18 mm), the taped top stays flat
		cu = u if curl_corner > 0 else 1.0 - u
		curl = 0.018 * max(0.0, cu - 0.65) / 0.35 * max(0.0, 0.3 - v) / 0.3
		return 0.0006 + wave * (1.0 - v) + curl ** 1.0 * 1.0

	lab = pc.label_rect(name)
	sheet = pc.surface_patch(lambda u, v: Vector((x_at(u, zc + v), u, zc + v)), (-w / 2, w / 2), (-h / 2, h / 2), 10, 14, lab)
	record_face(asset, sheet)
	# the curl twists its quads by ~1.2 mm, more than the 0.25 mm paper: triangulate BEFORE the shell so the front and the back share
	# one triangulation (a reversed quad splits along the other diagonal, the shells cross and bake / render as a grid of black cells)
	bmesh.ops.triangulate(sheet, faces=sheet.faces[:], quad_method="FIXED", ngon_method="BEAUTY")
	pc.thin_shell(sheet, -0.00025)
	asset.add(sheet, "Label_Atlas", uv="none", touch=0.15)
	for sy in (-1, 1):
		tape(asset, sy * (w / 2 - 0.02), zc + h / 2 - 0.02, sy * rng.uniform(35, 55), x_at, 0.045)
	if rng.random() < 0.6:
		tape(asset, curl_corner * -1 * (w / 2 - 0.02), zc - h / 2 + 0.02, rng.uniform(-50, 50), x_at, 0.045)
	return 0.03


def build_cardboard(asset: pc.Asset, name: str, w: float, h: float, zc: float, rng) -> float:
	t = 0.003
	# torn bottom and right edges: cells along them dropped at random (the printed face is the board's front, as for the metal sign)
	nu, nv = 17, 12
	torn = {(i, j) for i in range(nu) for j in range(nv) if (j == 0 or i == nu - 1) and rng.random() < 0.35}
	sheet(asset, name, w, h, zc, lambda u, v: t, t, "Label_Atlas", nu, nv, keep=lambda s, tt: (int(s * nu), int(tt * nv)) not in torn,
		edge_material="Cardboard")
	for sy in (-1, 1):
		tape(asset, sy * (w / 2 - 0.026), zc + h / 2 - 0.026, sy * rng.uniform(30, 60), lambda y, z: t, 0.055)
	return t + 0.004


def sign(asset_id: str, name: str, kind: str, size, zc: float, spec: str, seed: int):
	rng = rb_bl.rng(asset_id, 0, seed)
	asset = pc.Asset(asset_id, "Signs", spec, "mid")
	w, h = size
	if kind == "metal":
		depth = build_metal(asset, name, w, h, zc, rng)
	elif kind == "plastic":
		depth = build_plastic(asset, name, w, h, zc, rng, dark="restrooms" in name)
	elif kind == "paper":
		depth = build_paper(asset, name, w, h, zc, rng)
	else:
		depth = build_cardboard(asset, name, w, h, zc, rng)
	asset.hull_box((min(depth, 0.02), w, h), (min(depth, 0.02) / 2.0, 0.0, zc))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	built = []
	for asset_id, name, kind, size, zc, spec, hint in SIGNS:
		if not pc.selected(a.only, asset_id):
			continue
		pc.clear_scene_keep_materials()
		asset = sign(asset_id, name, kind, size, zc, spec, a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		w, h = size
		# VDB-T4: the printed face is the sign's size (the label's design size: the atlas cell aspect) and hangs at the spec's mounting
		# height; both are asserted on the face itself (+-2 mm). The bounding box is the face plus the tape lapping onto the wall, the
		# screws and the bent corner - design additions, asserted like the stools' footprints at the mid tolerance (+-1 cm), never a
		# loosened one.
		fy0, fy1, fz0, fz1 = asset.face_bounds
		face = (fy1 - fy0, fz1 - fz0)
		for axis, have, want in (("width", face[0], w), ("height", face[1], h)):
			if abs(have - want) > pc.HERO_TOL:
				rb_bl.fail(f"{asset_id}: face {axis} {have:.4f} m, design {want:.4f} m (+-{pc.HERO_TOL})")
		if abs((fz0 + fz1) / 2.0 - zc) > pc.HERO_TOL:
			rb_bl.fail(f"{asset_id}: face centre height {(fz0 + fz1) / 2.0:.4f} m, spec {zc}")
		check = {"what": "printed face width x height / centre height", "measured_m": [round(face[0], 5), round(face[1], 5),
			round((fz0 + fz1) / 2.0, 5)], "spec_m": [w, h, zc], "tolerance_m": pc.HERO_TOL}
		label_aspect = pc.label(name)["size_m"]
		if abs(label_aspect[0] / label_aspect[1] - w / h) > 0.05:
			rb_bl.fail(f"{asset_id}: face {w} x {h} does not keep the label aspect {label_aspect}")
		asset.export(out, tuple(hi - lo), wm_res=512, collision_profile="RbVenueProp",
			acoustic="paper" if kind in ("paper", "cardboard") else "metal_sheet" if kind == "metal" else "plastic", pivot="wall_plane_floor",
			external_inputs=["Cardboard002"] if kind == "cardboard" else [], meta={
				"element": spec, "kind": kind, "face_m": [w, h], "centre_z_m": zc, "label": name, "spec_checks": [check],
				"placement_hint_ue_cm": hint,
				# 0.25 mm paper: Nanite's position quantisation merges the front and back of the curled corner (the back faces
				# z-fight through as a grid of black cells); the sheets are a few hundred triangles, so they stay classic meshes
				**({"nanite": False} if kind == "paper" else {})})
		if pc.want_preview():
			pc.preview(obj, asset_id, view=(1.0, -0.3, 0.1), zoom=2.0 / max(w, h))
		built.append(asset_id)
	rb_bl.log(f"db_signs: {built}")


main()
