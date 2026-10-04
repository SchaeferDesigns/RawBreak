"""H12 CD jukebox "Marquee Starlite CD-100" (Marquee Phonograph Co., 1996; venue-dive-bar E10, 5.1 H12, 8.1). Runs INSIDE
Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_jukebox.py [-- --seed 1958 --preview --no-bake]

Jukebox: 0.80 m wide, 0.70 m deep, 1.47 m tall (E10), a 1990s wall box: black cabinet with brushed-aluminium and chrome trim, an
arched top carrying the backlit marquee (TXT "Marquee - Starlite CD-100"), two colour-cycling plexi light pillars on the front
corners (MI_DB_Emissive_JukeboxPanels: amber -> red -> blue, >= 4 s per step, never a flash: 4.1 / L18), the title-strip window
(two pages of 40 typed strips, one hand-written "DEACON'S PICK", TXT from the jukebox list) behind glass, the control shelf with
keypad, LED credit display and coin entry, a speaker grille of chrome slats, a closed plinth (a ball cannot roll under it).
Band stickers (S16) are decals (later: HIGGS art + TXT). Wall item: pivot at the wall plane, bottom centre; front +X (the level
yaws it to face -Y). Spec asserts (hero +-2 mm): 0.70 x 0.80 x 1.47.
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

D, W, H = 0.70, 0.80, 1.47
Z_BODY = 1.20                    # the arch starts here
W2 = W / 2.0


def prism_yz(poly_yz, x0: float, x1: float) -> bmesh.types.BMesh:
	"""Prism of a polygon in the YZ plane (counter-clockwise seen from +X) between x0 and x1."""
	bm = pc.bm_extrude_polygon(poly_yz, 0.0, 1.0)
	for vert in bm.verts:
		yy, zz, t = vert.co.x, vert.co.y, vert.co.z
		vert.co = Vector((x0 + (x1 - x0) * t, yy, zz))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def arch_points(half_w: float, z0: float, rise: float, n: int = 24) -> list[tuple[float, float]]:
	"""Elliptic arch from (-half_w, z0) over the top to (half_w, z0)."""
	return [(-half_w * math.cos(math.pi * i / n), z0 + rise * math.sin(math.pi * i / n)) for i in range(n + 1)]


def build(seed: int) -> pc.Asset:
	rng = rb_bl.rng("Jukebox", 0, seed)
	asset = pc.Asset("Jukebox", "Jukebox", "H12", "hero")
	# ---- cabinet --------------------------------------------------------------------------------------------------------
	body = pc.bm_box((D - 0.06, W, Z_BODY - 0.06), ((D - 0.06) / 2.0, 0.0, 0.06 + (Z_BODY - 0.06) / 2.0))
	pc.bevel(body, 0.006, 2)
	asset.add(body, "Paint_Cabinet", uv="box", grain="z", touch=0.3)
	plinth = pc.bm_box((D - 0.10, W - 0.06, 0.06), ((D - 0.10) / 2.0, 0.0, 0.03))
	asset.add(plinth, "Rubber_Black", uv="box")
	asset.hull_box((D - 0.02, W, Z_BODY), ((D - 0.02) / 2.0, 0.0, Z_BODY / 2.0))
	# the arched top: shell of the cabinet colour, the marquee panel on its front face, a chrome arch trim
	arch = arch_points(W2, Z_BODY - 0.01, H - Z_BODY + 0.01)
	top = prism_yz(arch, 0.0, D - 0.08)
	asset.add(top, "Paint_Cabinet", uv="box", touch=0.0)
	asset.hull_points([Vector((x, y, z)) for x in (0.0, D - 0.08) for (y, z) in arch[::3] + [arch[-1]]])
	trim = pc.bm_tube_path([Vector((D - 0.075, y, z)) for (y, z) in arch_points(W2 - 0.012, Z_BODY - 0.01, H - Z_BODY - 0.0085 + 0.01 - 0.002, 40)], 0.008, 12)
	asset.add(trim, "Chrome", uv="cyl", touch=0.2)
	# marquee: a curved band of backlit plexi under the arch trim + the name plate (TXT) in the middle
	inner = arch_points(W2 - 0.06, Z_BODY + 0.02, H - Z_BODY - 0.07, 32)
	glow = prism_yz(inner, D - 0.082, D - 0.074)
	asset.add(glow, "Emissive_JukeboxPanels", uv="box")
	lab = pc.label_rect("jukebox_marquee")

	def mq(u, v):
		return Vector((D - 0.070, u, 1.315 + v))
	plate = pc.surface_patch(mq, (-0.26, 0.26), (-0.065, 0.065), 1, 1, lab)
	asset.add(plate, "Emissive_JukeboxMarquee", uv="none")
	frame = pc.bm_box((0.012, 0.56, 0.15), (D - 0.078, 0.0, 1.315))
	pc.bevel(frame, 0.004, 2)
	asset.add(frame, "Chrome", uv="box")
	# ---- front: light pillars on the corners -----------------------------------------------------------------------------
	for sy in (-1.0, 1.0):
		yc = sy * (W2 - 0.05)
		pillar = pc.bm_box((0.05, 0.075, Z_BODY - 0.12), (D - 0.045, yc, 0.10 + (Z_BODY - 0.12) / 2.0))
		pc.bevel(pillar, 0.018, 4)
		asset.add(pillar, "Emissive_JukeboxPanels", uv="box")
		for zz in (0.11, Z_BODY - 0.02):
			cap = pc.bm_box((0.06, 0.085, 0.02), (D - 0.045, yc, zz))
			pc.bevel(cap, 0.005, 2)
			asset.add(cap, "Chrome", uv="box", touch=0.4)
		strip = pc.bm_box((0.006, 0.012, Z_BODY - 0.1), (D - 0.06 + 0.003, sy * (W2 - 0.006), 0.05 + (Z_BODY - 0.1) / 2.0))
		asset.add(strip, "Alu_Trim", uv="box", grain="z")
	# ---- title-strip window (2 pages) ---------------------------------------------------------------------------------
	zc, wh = 0.905, (0.56, 0.42)
	# chrome bezel: a frame of four bars around the window (the pages sit behind the plexi inside it)
	bw = 0.025
	for (sy, sz, cy, cz) in ((wh[0] + 2 * bw, bw, 0.0, zc + wh[1] / 2.0 + bw / 2.0), (wh[0] + 2 * bw, bw, 0.0, zc - wh[1] / 2.0 - bw / 2.0),
			(bw, wh[1], -wh[0] / 2.0 - bw / 2.0, zc), (bw, wh[1], wh[0] / 2.0 + bw / 2.0, zc)):
		bar = pc.bm_box((0.03, sy, sz), (D - 0.055, cy, cz))
		pc.bevel(bar, 0.006, 3)
		asset.add(bar, "Chrome", uv="box", touch=0.5)
	back = pc.bm_box((0.01, wh[0], wh[1]), (D - 0.06, 0.0, zc))
	asset.add(back, "Paint_Cabinet", uv="box")
	for k, cell in enumerate(("jukebox_strips_0", "jukebox_strips_1")):
		yc = (-0.5 + k) * (wh[0] / 2.0)

		def pg(u, v, yc=yc):
			return Vector((D - 0.052, yc + u, zc + v))
		page = pc.surface_patch(pg, (-0.13, 0.13), (-0.19, 0.19), 1, 1, pc.label_rect(cell))
		asset.add(page, "Emissive_JukeboxStrips", uv="none")
	glass = pc.bm_box((0.004, wh[0] + 0.01, wh[1] + 0.01), (D - 0.042, 0.0, zc))
	asset.add(glass, "Plexi_Scratched", uv="box")
	# ---- control shelf: keypad, LED credit display, coin entry ------------------------------------------------------------
	shelf = prism_yz([(-W2 + 0.09, 0.62), (W2 - 0.09, 0.62), (W2 - 0.09, 0.66), (-W2 + 0.09, 0.66)], D - 0.07, D + 0.0)
	asset.add(shelf, "Alu_Trim", uv="box", grain="y", touch=0.9)
	slope = prism_yz([(-W2 + 0.09, 0.56), (W2 - 0.09, 0.56), (W2 - 0.09, 0.62), (-W2 + 0.09, 0.62)], D - 0.07, D - 0.035)
	asset.add(slope, "Paint_Cabinet", uv="box")
	for i in range(2):
		for j in range(6):
			b = pc.bm_box((0.010, 0.022, 0.014), (D - 0.035 + 0.005, -0.27 + 0.03 * j, 0.575 + 0.028 * i))
			pc.bevel(b, 0.003, 2)
			asset.add(b, "Chrome", uv="box", touch=1.0)
	led = pc.bm_box((0.004, 0.10, 0.025), (D - 0.034, 0.10, 0.59))
	asset.add(led, "Emissive_LedRed", uv="box")
	coin = pc.bm_box((0.018, 0.07, 0.10), (D - 0.035 + 0.009, 0.25, 0.56))
	pc.bevel(coin, 0.004, 2)
	asset.add(coin, "Chrome", uv="box", touch=0.9)
	slot = pc.bm_box((0.004, 0.004, 0.03), (D - 0.035 + 0.019, 0.25, 0.585))
	asset.add(slot, "Rubber_Black", uv="box")
	# ---- speaker grille: chrome slats over a black cloth -------------------------------------------------------------------
	cloth = pc.bm_box((0.01, W - 0.24, 0.40), (D - 0.07, 0.0, 0.33))
	asset.add(cloth, "Rubber_Black", uv="box")
	for k in range(9):
		z = 0.15 + 0.042 * k
		slat = pc.bm_cylinder(0.006, W - 0.26, 12)
		pc.transform(slat, Matrix.Translation((D - 0.052, 0.0, z)) @ Matrix.Rotation(math.radians(90), 4, "X"))
		asset.add(slat, "Chrome", uv="cyl", touch=0.3)
	# a strip of brushed trim under the grille and the jukebox's badge dent / chip on the left corner (9.2)
	kick = pc.bm_box((0.01, W - 0.04, 0.035), (D - 0.064, 0.0, 0.085))
	asset.add(kick, "Alu_Trim", uv="box", grain="y", touch=0.1)
	asset.anchor("title_window", (D - 0.05, 0.0, zc))
	asset.anchor("marquee", (D - 0.07, 0.0, 1.315))
	asset.anchor("coin_entry", (D - 0.02, 0.25, 0.58))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	if not pc.selected(a.only, "Jukebox"):
		return
	pc.clear_scene_keep_materials()
	asset = build(a.seed)
	obj = asset.build()
	asset.export(out, (D, W, H), pivot="wall_plane_bottom_centre", acoustic="wood_panel", wm_res=2048, meta={
		"element": "E10", "brand": "Marquee Phonograph Co. 'Starlite CD-100' (fictional, 8.1)",
		"emissive": {"panels_cd_m2": 120, "strips_cd_m2": 200, "marquee_cd_m2": 180, "cycle_period_s": 7.5,
			"note": "L18 is M2-A's rect light; AfterHours idle panels 30 % is a level-side parameter"},
		"placement_hint_ue_cm": {"location": [1130.0, 732.0, 0.0], "yaw_deg": -90.0, "note": "E10 X 10.90 - 11.70 on the right wall; front faces -Y"}})
	if pc.want_preview():
		pc.preview(obj, "Jukebox", view=(1.0, -0.55, 0.3), zoom=1.2)
	rb_bl.log("db_jukebox: ['Jukebox']")


main()
