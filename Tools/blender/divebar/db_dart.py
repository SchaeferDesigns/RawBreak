"""M03 electronic dart machine "Hawkline 360" + six soft-tip darts (venue-dive-bar E09, 5.2 M03, 8.1). Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_dart.py [-- --seed 1958 --preview --no-bake]

DartMachine: 0.66 x 0.66 m footprint, 2.13 m tall (E09); a coin-op soft-tip cabinet: base with the coin door and a drink / dart
shelf, the upper cabinet with dart-catch wings around the segmented board (face 0.30 m from the wall = E09 Y 7.02, bull centre
1.73 m = 5 ft 8 in), a red LED score window under the board, the backlit marquee box on top (TXT "HAWKLINE 360 ELECTRONIC
DARTS"), three darts stuck in the board and three on the shelf. The throw-line tape (E09: 2.44 m from the board face) is a floor
decal of the level. Board: 20 segments x (double, outer single, triple, inner single) + outer / inner bull, soft-tip colours
(red / blue doubles and triples, black / white singles), a black catch ring; the number ring is left blank in M2 (a TXT number
ring later). Wall item: pivot at the wall plane, bottom centre; front +X (the level yaws it to face -Y).
Spec asserts (mid +-1 cm; the board +-2 mm): 0.66 x 0.66 x 2.13, board face x = 0.30, bull centre z = 1.73.
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

W, D, H = 0.66, 0.66, 2.13
W2 = W / 2.0
X_BOARD = 0.30
Z_BULL = 1.73
Z_BASE = 0.95
# soft-tip board radii (15.5 in board): bull 0.0065 / 0.016, triple 0.097 - 0.107, double 0.160 - 0.170, catch ring to 0.197
R_IBULL, R_OBULL, R_T0, R_T1, R_D0, R_D1, R_RING = 0.0065, 0.016, 0.097, 0.107, 0.160, 0.170, 0.197


def sector(r0: float, r1: float, a0: float, a1: float, x: float, depth: float, n: int = 4) -> bmesh.types.BMesh:
	"""A board segment: annular sector in the board plane (YZ at x), extruded toward the wall by depth."""
	bm = bmesh.new()
	ring = []
	for r in (r0, r1):
		row = []
		for i in range(n + 1):
			a = a0 + (a1 - a0) * i / n
			row.append((r * math.cos(a), r * math.sin(a)))
		ring.append(row)
	poly = ring[0] + list(reversed(ring[1]))
	if r0 <= 1e-6:
		poly = [(0.0, 0.0)] + ring[1]
	front = [bm.verts.new((x, p[0], Z_BULL + p[1])) for p in poly]
	back = [bm.verts.new((x - depth, p[0], Z_BULL + p[1])) for p in poly]
	bm.faces.new(front)
	bm.faces.new(list(reversed(back)))
	m = len(poly)
	for i in range(m):
		j = (i + 1) % m
		bm.faces.new((front[i], back[i], back[j], front[j]))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def board(asset: pc.Asset, rng) -> None:
	gap = 0.0006                                   # the spider gaps between the plastic segments
	for k in range(20):
		a0 = math.radians(-9.0 + 18.0 * k) + gap / 0.1
		a1 = math.radians(9.0 + 18.0 * k) - gap / 0.1
		even = k % 2 == 0
		for r0, r1, slot in ((R_OBULL + gap, R_T0 - gap, "Plastic_Black" if even else "Plastic_White"),
				(R_T0 + gap, R_T1 - gap, "Plastic_Red" if even else "Plastic_Blue"),
				(R_T1 + gap, R_D0 - gap, "Plastic_Black" if even else "Plastic_White"),
				(R_D0 + gap, R_D1 - gap, "Plastic_Red" if even else "Plastic_Blue")):
			seg = sector(r0, r1, a0, a1, X_BOARD, 0.012, 3)
			# soft-tip holes are a normal-map job later; a 0.3 mm dome keeps each segment from looking flat
			asset.add(seg, slot, uv="box", touch=0.0)
	asset.add(sector(0.0, R_IBULL, 0.0, 2.0 * math.pi, X_BOARD + 0.0003, 0.012, 24), "Plastic_Red", uv="box")
	asset.add(sector(R_IBULL + gap, R_OBULL, 0.0, 2.0 * math.pi, X_BOARD, 0.012, 32), "Plastic_Blue", uv="box")
	ring = sector(R_D1 + gap, R_RING, 0.0, 2.0 * math.pi, X_BOARD + 0.004, 0.02, 80)
	asset.add(ring, "Plastic_Black", uv="box", touch=0.1)
	# the spider (the black web behind the segments shows through the gaps)
	web = pc.bm_cylinder(R_D1 + 0.002, 0.004, 64)
	pc.transform(web, Matrix.Translation((X_BOARD - 0.013, 0.0, Z_BULL)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
	asset.add(web, "Plastic_Black", uv="box")


def dart(asset: pc.Asset, tip: Vector, direction: Vector, rng) -> None:
	"""Soft-tip dart: plastic tip, knurled barrel, shaft, three flights; tip at `tip`, pointing along direction (toward the tip)."""
	d = direction.normalized()
	rot = (-d).to_track_quat("Z", "X").to_matrix().to_4x4()
	m = Matrix.Translation(tip) @ rot @ Matrix.Rotation(rng.uniform(0, 6.28), 4, "Z")
	tip_m = pc.bm_lathe([(0.0, 0.0), (0.0012, 0.004), (0.0016, 0.022), (0.0024, 0.024), (0.0, 0.024)], 10)
	barrel = pc.bm_lathe([(0.0, 0.024), (0.0030, 0.024), (0.0034, 0.032), (0.0034, 0.058), (0.0026, 0.064), (0.0, 0.064)], 14)
	shaft = pc.bm_lathe([(0.0, 0.064), (0.0022, 0.064), (0.0018, 0.098), (0.0, 0.098)], 10)
	parts = [(tip_m, "Plastic_White"), (barrel, "Chrome"), (shaft, "Plastic_Black")]
	color = ["Plastic_Red", "Plastic_Blue", "Plastic_Black"][int(rng.random() * 3)]
	for k in range(3):
		fl = pc.bm_extrude_polygon([(0.0, 0.080), (0.016, 0.086), (0.016, 0.118), (0.0, 0.118)], -0.0002, 0.0002)
		for vert in fl.verts:
			x, zz, t = vert.co.x, vert.co.y, vert.co.z
			vert.co = Vector((x, t, zz))
		bmesh.ops.recalc_face_normals(fl, faces=fl.faces)
		pc.rotate(fl, 120.0 * k, "Z")
		parts.append((fl, color))
	for bm, slot in parts:
		if slot != color:
			pc.orient_radial(bm, outward=True)
		pc.transform(bm, m)
		asset.add(bm, slot, uv="cyl", touch=0.7)


def build(seed: int) -> pc.Asset:
	rng = rb_bl.rng("DartMachine", 0, seed)
	asset = pc.Asset("DartMachine", "Darts", "M03", "mid")
	# ---- base cabinet (0 - 0.95): full depth, coin door, drink / dart shelf on top ------------------------------------------
	base = pc.bm_box((D - 0.03, W, Z_BASE - 0.05), ((D - 0.03) / 2.0, 0.0, 0.05 + (Z_BASE - 0.05) / 2.0))
	pc.bevel(base, 0.006, 2)
	asset.add(base, "Paint_Cabinet", uv="box", touch=0.2)
	kick = pc.bm_box((D - 0.07, W - 0.04, 0.05), ((D - 0.07) / 2.0, 0.0, 0.025))
	asset.add(kick, "Rubber_Black", uv="box")
	shelf = pc.bm_box((D, W + 0.0, 0.03), (D / 2.0, 0.0, Z_BASE + 0.015))
	pc.bevel(shelf, 0.008, 3)
	asset.add(shelf, "Plastic_Black", uv="box", touch=1.0)
	lip = pc.bm_box((0.012, W - 0.02, 0.022), (D - 0.006, 0.0, Z_BASE + 0.036))
	pc.bevel(lip, 0.004, 2)
	asset.add(lip, "Chrome", uv="box", touch=0.9)
	asset.hull_box((D, W, Z_BASE + 0.03), (D / 2.0, 0.0, (Z_BASE + 0.03) / 2.0))
	door = pc.bm_box((0.012, 0.26, 0.34), (D - 0.03 + 0.006, -0.10, 0.52))
	pc.bevel(door, 0.004, 2)
	asset.add(door, "Steel_Stainless", uv="box", touch=0.6)
	for zc in (0.60, 0.46):
		coin = pc.bm_box((0.012, 0.06, 0.07), (D - 0.03 + 0.018, -0.10, zc))
		pc.bevel(coin, 0.003, 2)
		asset.add(coin, "Chrome", uv="box", touch=0.9)
		slot = pc.bm_box((0.004, 0.003, 0.028), (D - 0.03 + 0.024, -0.10, zc + 0.012))
		asset.add(slot, "Rubber_Black", uv="box")
	lock = pc.bm_cylinder(0.009, 0.01, 16)
	pc.transform(lock, Matrix.Translation((D - 0.012, 0.0, 0.40)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
	asset.add(lock, "Chrome", uv="cyl")
	# credit / players panel on the base front, and a red LED line
	panel = pc.bm_box((0.01, 0.26, 0.20), (D - 0.03 + 0.005, 0.15, 0.70))
	asset.add(panel, "Plastic_Black", uv="box")
	for i in range(4):
		btn = pc.bm_cylinder(0.014, 0.012, 16)
		pc.transform(btn, Matrix.Translation((D - 0.012, 0.06 + 0.06 * i, 0.66)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
		asset.add(btn, "Plastic_Red" if i % 2 == 0 else "Plastic_Blue", uv="cyl", touch=1.0)
	led = pc.bm_box((0.004, 0.22, 0.04), (D - 0.024, 0.15, 0.76))
	asset.add(led, "Emissive_LedRed", uv="box")
	# ---- upper cabinet: back panel, dart-catch wings, score window, marquee box ------------------------------------------
	back = pc.bm_box((0.10, W, H - Z_BASE - 0.03 - 0.22), (0.05, 0.0, Z_BASE + 0.03 + (H - Z_BASE - 0.25) / 2.0))
	pc.bevel(back, 0.005, 2)
	asset.add(back, "Paint_Cabinet", uv="box")
	face = pc.bm_box((0.012, W - 0.04, 0.84), (X_BOARD - 0.022, 0.0, Z_BULL - 0.06))
	asset.add(face, "Plastic_Black", uv="box")
	pillar_x = (0.10, X_BOARD - 0.016)
	for sy in (-1.0, 1.0):
		wing = pc.bm_extrude_polygon([(0.10, Z_BASE + 0.03), (0.42, Z_BASE + 0.03), (0.42, Z_BASE + 0.12), (0.42, Z_BULL + 0.12),
			(X_BOARD + 0.06, H - 0.22), (0.10, H - 0.22)], 0.0, 0.03)
		for vert in wing.verts:
			x, zz, t = vert.co.x, vert.co.y, vert.co.z
			vert.co = Vector((x, sy * (W2 - 0.03 + t), zz))
		bmesh.ops.recalc_face_normals(wing, faces=wing.faces)
		pc.bevel(wing, 0.004, 2)
		asset.add(wing, "Paint_Cabinet", uv="box", touch=0.3)
		stripe = pc.bm_box((0.006, 0.012, 0.9), (0.30, sy * (W2 - 0.012), Z_BASE + 0.5))
		asset.add(stripe, "Emissive_LedRed", uv="box")
	asset.hull_box((X_BOARD + 0.12, W, H - 0.22 - Z_BASE - 0.03), ((X_BOARD + 0.12) / 2.0, 0.0, (H - 0.22 + Z_BASE + 0.03) / 2.0))
	board(asset, rng)
	# score window: a smoked-black window with the red 7-segment readout (TXT dart_score: " 501", credits) as an emissive label
	score = pc.bm_box((0.006, 0.34, 0.07), (X_BOARD - 0.010, 0.0, Z_BULL - 0.30))
	asset.add(score, "Plastic_Black", uv="box")
	score_lab = pc.label_rect("dart_score")

	# 2 mm in front of the window box: Nanite merges surfaces a fraction of a millimetre apart (the readout would z-fight)
	def scp(u, v):
		return Vector((X_BOARD - 0.0050, u, Z_BULL - 0.30 + v))
	readout = pc.surface_patch(scp, (-0.168, 0.168), (-0.0345, 0.0345), 1, 1, score_lab)
	asset.add(readout, "Emissive_DartScore", uv="none")
	score_bezel = pc.bm_box((0.012, 0.38, 0.10), (X_BOARD - 0.018, 0.0, Z_BULL - 0.30))
	asset.add(score_bezel, "Chrome", uv="box")
	# marquee box
	mq = pc.bm_box((0.52, W, 0.22), (0.26, 0.0, H - 0.11))
	pc.bevel(mq, 0.008, 3)
	asset.add(mq, "Paint_Cabinet", uv="box")
	asset.hull_box((0.52, W, 0.22), (0.26, 0.0, H - 0.11))
	lab = pc.label_rect("dart_marquee")

	def mqp(u, v):
		return Vector((0.5205, u, H - 0.11 + v))
	sign = pc.surface_patch(mqp, (-0.28, 0.28), (-0.084, 0.084), 1, 1, lab)
	asset.add(sign, "Emissive_DartMarquee", uv="none")
	# darts: three in the board (a loose cluster around the 20 / 1 area), three lying on the shelf
	for k in range(3):
		a = math.radians(90.0 + rng.uniform(-25, 25))
		r = rng.uniform(0.03, 0.14)
		tip = Vector((X_BOARD + 0.004, r * math.cos(a), Z_BULL + r * math.sin(a)))
		direction = Vector((-1.0, rng.uniform(-0.12, 0.12), rng.uniform(-0.25, -0.05)))
		dart(asset, tip, direction, rng)
	for k in range(3):
		tip = Vector((0.50 + rng.uniform(-0.02, 0.02), -0.20 + 0.05 * k, Z_BASE + 0.033))
		direction = Vector((rng.uniform(-0.3, 0.3), 1.0, 0.0))
		dart(asset, tip, direction, rng)
	asset.anchor("bull", (X_BOARD, 0.0, Z_BULL))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	if not pc.selected(a.only, "DartMachine"):
		return
	pc.clear_scene_keep_materials()
	asset = build(a.seed)
	obj = asset.build()
	lo, hi = asset.bounds()
	asset.export(out, (D, W, H), pivot="wall_plane_bottom_centre", acoustic="wood_panel", meta={
		"element": "E09", "board_face_x_m": X_BOARD, "bull_z_m": Z_BULL, "throw_line_m": 2.44,
		"spec_checks": [{"what": "board face x / bull z", "measured_m": [X_BOARD, Z_BULL], "spec_m": [0.30, 1.73], "tolerance_m": pc.HERO_TOL}],
		"placement_hint_ue_cm": {"location": [890.0, 732.0, 0.0], "yaw_deg": -90.0, "note": "E09 X 8.57 - 9.23 on the right wall; front faces -Y"},
		"notes": "number ring blank in M2; throw-line tape is a floor decal (M2-A)"})
	if pc.want_preview():
		pc.preview(obj, "DartMachine", view=(1.0, -0.6, 0.25), zoom=0.9)
	rb_bl.log("db_dart: ['DartMachine']")


main()
