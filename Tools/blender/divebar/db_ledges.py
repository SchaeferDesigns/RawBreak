"""Drink ledges and the column shelf (venue-dive-bar E15, E16, E17; 5.1 H15 shelf, 5.2 M22 / M23). Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_ledges.py [-- --seed 1958 --preview --no-bake]

  ColumnShelf     E15: round 0.40 m plywood disc clamped to column C3 (the column itself is M2-A's architecture), top at 1.12 m,
                  a split steel collar with three gussets, the hook for the plastic triangle rack under it (A5 "under the C3
                  shelf"; E15's "Z 1.40" contradicts "below it" - the hook hangs at 1.00 m, reported); pivot on the column axis at the
                  floor.
  LedgeBack       E16: the back-wall drink rail, 1.50 m x 0.20 m, board 38 mm with its top at 1.07 m (underside 1.032) on three
                  steel L-brackets; wall item (pivot: wall plane, bottom centre; front +X faces the room).
  LedgeLeftWall   E17: the left-wall ledge, 5.40 m x 0.25 m, top 1.07 m, on seven brackets; wall item.
Ring stains, burns and the sticky film come from the coated material (MI_DB_Wood_Ledge: RingDensity / BurnDensity) and the wear mask
(the front edge is the touch band); every board bows a little and sits a millimetre off level (9.2).
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

LEDGE_TOP = 1.07
BOARD_T = 0.038
SHELF_TOP = 1.12
SHELF_R = 0.20
COLUMN_R = 0.114 / 2.0


def board(asset: pc.Asset, length: float, depth: float, rng) -> None:
	bm = pc.bm_box((depth, length, BOARD_T), (depth / 2.0, 0.0, LEDGE_TOP - BOARD_T / 2.0))
	bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.y - e.verts[1].co.y) > 0.5], cuts=max(4, int(length / 0.1)),
		use_grid_fill=True)
	pc.bevel(bm, 0.004, 2)
	tilt = rng.uniform(-0.0012, 0.0012) / length
	bow = rng.uniform(0.0008, 0.0018)
	# front edge rounder (worn), a bow between brackets, a millimetre off level along the length
	pc.deform(bm, lambda p: p + Vector((0.0, 0.0, tilt * p.y - bow * math.sin(math.pi * (p.y / length + 0.5)) ** 2 * 0.3)))
	asset.add(bm, "Wood_Ledge", uv="box", grain="y", touch=lambda co, d=depth: 1.0 if co.x > d - 0.04 else 0.25)
	asset.hull_box((depth, length, BOARD_T), (depth / 2.0, 0.0, LEDGE_TOP - BOARD_T / 2.0))


def brackets(asset: pc.Asset, length: float, depth: float, count: int, rng) -> None:
	for k in range(count):
		y = -length / 2.0 + 0.12 + (length - 0.24) * k / (count - 1) + rng.uniform(-0.02, 0.02)
		z_top = LEDGE_TOP - BOARD_T
		arm = pc.bm_box((depth - 0.03, 0.03, 0.004), ((depth - 0.03) / 2.0, y, z_top - 0.002))
		leg = pc.bm_box((0.004, 0.03, 0.18), (0.002, y, z_top - 0.09))
		gusset = pc.bm_extrude_polygon([(0.004, z_top - 0.16), (0.004, z_top - 0.004), (depth * 0.62, z_top - 0.004)], 0.0, 0.004)
		for vert in gusset.verts:
			x, zz, t = vert.co.x, vert.co.y, vert.co.z
			vert.co = Vector((x, y - 0.002 + t, zz))
		bmesh.ops.recalc_face_normals(gusset, faces=gusset.faces)
		for bm in (arm, leg, gusset):
			asset.add(bm, "Paint_BlackSteel", uv="box")
		for zz in (z_top - 0.04, z_top - 0.14):
			screw = pc.bm_cylinder(0.005, 0.003, 12)
			pc.transform(screw, Matrix.Translation((0.0055, y, zz)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
			asset.add(screw, "Steel_Zinc", uv="cyl")


def ledge(asset_id: str, length: float, depth: float, count: int, spec: str, seed: int) -> pc.Asset:
	rng = rb_bl.rng(asset_id, 0, seed)
	asset = pc.Asset(asset_id, "Ledges", spec, "mid")
	board(asset, length, depth, rng)
	brackets(asset, length, depth, count, rng)
	return asset


def column_shelf(seed: int) -> pc.Asset:
	rng = rb_bl.rng("ColumnShelf", 0, seed)
	asset = pc.Asset("ColumnShelf", "Ledges", "E15", "hero")
	t = 0.020
	# disc with the column hole: a lathe ring
	disc = pc.bm_lathe([(COLUMN_R + 0.002, SHELF_TOP - t), (SHELF_R - 0.004, SHELF_TOP - t), (SHELF_R, SHELF_TOP - t + 0.004),
		(SHELF_R, SHELF_TOP - 0.004), (SHELF_R - 0.004, SHELF_TOP), (COLUMN_R + 0.002, SHELF_TOP), (COLUMN_R + 0.002, SHELF_TOP - t)],
		segments=72, close_bottom=False)
	bmesh.ops.remove_doubles(disc, verts=disc.verts, dist=1e-6)
	bmesh.ops.recalc_face_normals(disc, faces=disc.faces)
	# the plywood was cut by hand: a slightly wobbly edge
	ph = rng.uniform(0, 6.28)
	pc.deform(disc, lambda p: p + (Vector((p.x, p.y, 0.0)).normalized() * 0.0015 * math.sin(3.0 * math.atan2(p.y, p.x) + ph)
		if math.hypot(p.x, p.y) > SHELF_R - 0.01 else Vector((0.0, 0.0, 0.0))))
	asset.add(disc, "Plywood_Painted", uv="box", touch=lambda co: 1.0 if math.hypot(co.x, co.y) > SHELF_R - 0.03 else 0.2)
	asset.hull_cylinder(SHELF_R, SHELF_TOP - t, SHELF_TOP, segments=12)
	# split collar with bolts, three gussets under the disc
	collar = pc.bm_lathe([(COLUMN_R + 0.0005, SHELF_TOP - t - 0.05), (COLUMN_R + 0.006, SHELF_TOP - t - 0.05), (COLUMN_R + 0.006, SHELF_TOP - t),
		(COLUMN_R + 0.0005, SHELF_TOP - t), (COLUMN_R + 0.0005, SHELF_TOP - t - 0.05)], segments=40, close_bottom=False)
	bmesh.ops.remove_doubles(collar, verts=collar.verts, dist=1e-6)
	bmesh.ops.recalc_face_normals(collar, faces=collar.faces)
	asset.add(collar, "Paint_BlackSteel", uv="cyl", touch=0.2)
	for k in range(3):
		a = math.radians(90.0 + 120.0 * k + rng.uniform(-4, 4))
		g = pc.bm_extrude_polygon([(COLUMN_R + 0.006, SHELF_TOP - t - 0.05), (COLUMN_R + 0.006, SHELF_TOP - t), (SHELF_R * 0.75, SHELF_TOP - t)],
			0.0, 0.004)
		for vert in g.verts:
			x, zz, tt = vert.co.x, vert.co.y, vert.co.z
			vert.co = Vector((x, tt - 0.002, zz))
		bmesh.ops.recalc_face_normals(g, faces=g.faces)
		pc.rotate(g, math.degrees(a), "Z")
		asset.add(g, "Paint_BlackSteel", uv="box")
	for sgn in (1.0, -1.0):
		lug = pc.bm_box((0.012, 0.03, 0.04), (sgn * (COLUMN_R + 0.012), 0.0, SHELF_TOP - t - 0.025))
		asset.add(lug, "Paint_BlackSteel", uv="box")
	bolt = pc.bm_cylinder(0.004, 2 * (COLUMN_R + 0.03), 12)
	pc.transform(bolt, Matrix.Translation((0.0, 0.0, SHELF_TOP - t - 0.025)) @ Matrix.Rotation(math.radians(90), 4, "Y"))
	asset.add(bolt, "Steel_Zinc", uv="cyl")
	# the triangle-rack hook (A5): a bent rod from a clamp band on the column, facing +X
	band = pc.bm_lathe([(COLUMN_R + 0.0005, 0.985), (COLUMN_R + 0.004, 0.985), (COLUMN_R + 0.004, 1.01), (COLUMN_R + 0.0005, 1.01),
		(COLUMN_R + 0.0005, 0.985)], segments=40, close_bottom=False)
	bmesh.ops.remove_doubles(band, verts=band.verts, dist=1e-6)
	bmesh.ops.recalc_face_normals(band, faces=band.faces)
	asset.add(band, "Steel_Zinc", uv="cyl")
	hook = pc.bm_tube_path([Vector((COLUMN_R + 0.003, 0.0, 0.9975)), Vector((COLUMN_R + 0.05, 0.0, 0.9975)), Vector((COLUMN_R + 0.065, 0.0, 1.01)),
		Vector((COLUMN_R + 0.066, 0.0, 1.03))], 0.004, 8)
	asset.add(hook, "Steel_Zinc", uv="cyl", touch=0.8)
	asset.anchor("triangle_hook", (COLUMN_R + 0.055, 0.0, 1.0))
	asset.anchor("column_axis", (0.0, 0.0, 0.0))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	built = []
	for asset_id, length, depth, count, spec, hint in (
			("LedgeBack", 1.50, 0.20, 3, "E16", {"location": [1646.0, 635.0, 0.0], "yaw_deg": 180.0,
				"note": "E16 X 16.26 - 16.46, Y 5.60 - 7.10 on the back wall; front faces -X"}),
			("LedgeLeftWall", 5.40, 0.25, 7, "E17", {"location": [1310.0, 0.0, 0.0], "yaw_deg": 90.0,
				"note": "E17 X 10.40 - 15.80, Y 0 - 0.25 on the left wall; front faces +Y"})):
		if not pc.selected(a.only, asset_id):
			continue
		pc.clear_scene_keep_materials()
		asset = ledge(asset_id, length, depth, count, spec, a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		if abs(hi.z - LEDGE_TOP) > 0.004:
			rb_bl.fail(f"{asset_id}: top {hi.z:.4f}, spec {LEDGE_TOP}")
		asset.export(out, (depth, length, hi.z - lo.z), pivot="wall_plane_bottom_centre", acoustic="wood_panel", meta={
			"element": spec, "top_z_m": LEDGE_TOP, "board_m": BOARD_T, "underside_z_m": LEDGE_TOP - BOARD_T,
			"spec_checks": [{"what": "top z", "measured_m": round(hi.z, 4), "spec_m": LEDGE_TOP, "tolerance_m": 0.004}],
			"placement_hint_ue_cm": hint})
		if pc.want_preview():
			pc.preview(obj, asset_id, view=(1.0, -0.7, 0.6), zoom=0.6 if length < 2 else 0.25)
		built.append(asset_id)
	if pc.selected(a.only, "ColumnShelf"):
		pc.clear_scene_keep_materials()
		asset = column_shelf(a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		if abs(hi.z - SHELF_TOP) > pc.HERO_TOL:
			rb_bl.fail(f"ColumnShelf top {hi.z:.4f}, spec {SHELF_TOP}")
		asset.export(out, (2 * SHELF_R, 2 * SHELF_R, hi.z - lo.z), pivot="column_axis_floor", acoustic="wood_panel", meta={
			"element": "E15", "top_z_m": SHELF_TOP, "disc_diameter_m": 2 * SHELF_R, "column_diameter_m": 2 * COLUMN_R,
			"triangle_hook_z_m": 1.0, "spec_note": "E15 says the hook is at Z 1.40 AND below the shelf (A5 'under the C3 shelf'): hung at 1.00 m",
			"placement_hint_ue_cm": {"location": [1372.0, 366.0, 0.0], "yaw_deg": 0.0, "note": "column C3 axis"}})
		if pc.want_preview():
			pc.preview(obj, "ColumnShelf", view=(1.0, -0.8, 0.9), zoom=1.2)
		built.append("ColumnShelf")
	rb_bl.log(f"db_ledges: {built}")


main()
