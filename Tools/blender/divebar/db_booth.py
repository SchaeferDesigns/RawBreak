"""M02 booths B1 - B3 (venue-dive-bar E08, 5.2 M02): benches and booth tables. Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_booth.py [-- --seed 1958 --preview --no-bake]

The three booth sets (E08 X 2.13 - 4.06, 4.06 - 5.99, 5.99 - 7.92, from the right wall to Y 6.25) are built from:
  BoothBench_End     single-sided bench at each end of the row: 0.585 (seat 0.51 + back) x 1.07 x 1.07
  BoothBench_Double  back-to-back bench between two sets: 1.17 x 1.07 x 1.07
  BoothTable         0.76 x 1.07 laminate top (walnut print, aluminium T-moulding) at 0.76 m on a cast pedestal with an X foot
Seat 0.46, back top 1.07, seat depth 0.51 (E08). Oxblood vinyl (MI_DB_Vinyl_Booth) in vertical channels on the backs, sat-in
seats (two hollows each, a crease), one duct-taped seat, closed plinths (a loose ball cannot vanish under them, 13.5), stained
caps. The table foot leaves the floor open under the table (leg-accurate hulls).
Local frame (Blender): front = +X = the way the (first) seat faces; length along Y = from the wall (+Y end in Blender = the wall end
... the benches are symmetric in Y); pivot at the floor centre. Level placement (M2-A): the benches' X axis along the venue X axis,
the table between two benches; see placement_hint_ue_cm in each JSON.
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

LEN = 1.07
SEAT_Z = 0.46
BACK_TOP = 1.07
SEAT_D = 0.51
HALF_BACK = 0.075          # half the shared back (the end bench's own back is this thick too)
PLINTH_Z = 0.36
TABLE = (0.76, 1.07, 0.76)


def cushion(size, center, bevel_w: float, cuts: int = 8) -> bmesh.types.BMesh:
	bm = pc.bm_box(size, center)
	bmesh.ops.subdivide_edges(bm, edges=list(bm.edges), cuts=cuts, use_grid_fill=True)
	pc.bevel(bm, bevel_w, 3, angle_deg=30.0, profile=0.6)
	return bm


def seat_side(asset: pc.Asset, x_back: float, sign: float, rng, taped: bool) -> None:
	"""One seat + channel-tufted back facing sign * X, the back's rear face at x_back."""
	x_front = x_back + sign * (HALF_BACK + SEAT_D)
	# plinth (closed to the floor) with a recessed toe kick
	x_p0, x_p1 = sorted((x_back, x_front - sign * 0.03))
	pl = pc.bm_box((x_p1 - x_p0, LEN, PLINTH_Z - 0.08), ((x_p0 + x_p1) / 2.0, 0.0, 0.08 + (PLINTH_Z - 0.08) / 2.0))
	pc.bevel(pl, 0.003, 1)
	asset.add(pl, "Paint_Cabinet", uv="box", grain="y")
	x_k0, x_k1 = sorted((x_back, x_front - sign * 0.07))
	toe = pc.bm_box((x_k1 - x_k0, LEN - 0.01, 0.08), ((x_k0 + x_k1) / 2.0, 0.0, 0.04))
	asset.add(toe, "Rubber_Black", uv="box", grain="y")
	# seat deck + cushion (80 mm, sat-in: two hollows, a crease at the back)
	x_s0, x_s1 = sorted((x_back + sign * HALF_BACK, x_front))
	seat = cushion((x_s1 - x_s0, LEN - 0.01, 0.10), ((x_s0 + x_s1) / 2.0, 0.0, SEAT_Z - 0.05), 0.028, cuts=10)
	hollows = [(rng.uniform(-0.3, -0.2), rng.uniform(0.004, 0.009)), (rng.uniform(0.18, 0.3), rng.uniform(0.004, 0.012))]
	xm = (x_s0 + x_s1) / 2.0

	def sag(p):
		if p.z < SEAT_Z - 0.03:
			return p
		dz = 0.0
		for hy, depth in hollows:
			dz -= depth * math.exp(-((p.y - hy) / 0.16) ** 2 - ((p.x - xm - sign * 0.03) / 0.16) ** 2)
		return p + Vector((0.0, 0.0, dz))
	pc.deform(seat, sag)
	asset.add(seat, "Vinyl_Booth", uv="box", grain="y", touch=0.8, smooth_angle=45.0)
	asset.hull_box((abs(x_front - x_back), LEN, SEAT_Z), ((x_front + x_back) / 2.0, 0.0, SEAT_Z / 2.0))
	if taped:
		cx = xm + sign * rng.uniform(-0.05, 0.05)
		for k, ang in enumerate((rng.uniform(-20, 20), rng.uniform(70, 110))):
			a = math.radians(ang)
			pts_len, w = 0.16 + 0.04 * k, 0.048
			bm = bmesh.new()
			grid = []
			for i in range(13):
				row = []
				for j in range(3):
					s = (i / 12.0 - 0.5) * pts_len
					t = (j / 2.0 - 0.5) * w
					x = cx + s * math.cos(a) - t * math.sin(a)
					y = -0.1 + s * math.sin(a) + t * math.cos(a)
					z = sag(Vector((x, y, SEAT_Z))).z + 0.0008 + k * 0.0004
					row.append(bm.verts.new((x, y, z)))
				grid.append(row)
			for i in range(12):
				for j in range(2):
					bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]))
			pc.thin_shell(bm, -0.0003)
			asset.add(bm, "Tape", uv="box", touch=0.5)
	# back: frame + vertical channels, leaning back 9 deg from the seat line
	lean = math.radians(9.0)
	n = 7
	w = (LEN - 0.03) / n
	h = BACK_TOP - 0.05 - (SEAT_Z + 0.02)
	for k in range(n):
		yc = -LEN / 2.0 + 0.015 + w * (k + 0.5)
		ch = cushion((0.07, w + 0.004, h), (0.0, 0.0, h / 2.0), 0.022, cuts=6)
		bulge = rng.uniform(0.004, 0.009)
		pc.deform(ch, lambda p, bulge=bulge: p + Vector((sign * bulge * math.cos(math.pi * p.y / (w + 0.004)) * math.sin(math.pi * min(1.0, max(0.0, p.z / h)))
			if abs(p.y) < (w + 0.004) / 2.0 and sign * p.x > 0 else 0.0, 0.0, 0.0)))
		m = Matrix.Translation((x_back + sign * (HALF_BACK + 0.035 - 0.01), yc, SEAT_Z + 0.02)) @ Matrix.Rotation(-sign * lean, 4, "Y")
		pc.transform(ch, m)
		asset.add(ch, "Vinyl_Booth", uv="box", grain="z", touch=0.3, smooth_angle=45.0)
	# the back panel behind the channels (plywood, painted)
	xb0, xb1 = sorted((x_back, x_back + sign * (HALF_BACK - 0.005)))
	panel = pc.bm_box((xb1 - xb0, LEN, BACK_TOP - 0.03 - PLINTH_Z), ((xb0 + xb1) / 2.0, 0.0, PLINTH_Z + (BACK_TOP - 0.03 - PLINTH_Z) / 2.0))
	asset.add(panel, "Plywood_Painted", uv="box")


def bench(asset_id: str, double: bool, seed: int) -> pc.Asset:
	rng = rb_bl.rng(asset_id, 0, seed)
	asset = pc.Asset(asset_id, "Booths", "M02", "mid")
	if double:
		seat_side(asset, 0.0, 1.0, rng, taped=False)
		seat_side(asset, 0.0, -1.0, rng, taped=True)
		x0, x1 = -HALF_BACK, HALF_BACK
	else:
		x_back = -(HALF_BACK + SEAT_D) / 2.0
		seat_side(asset, x_back, 1.0, rng, taped=False)
		x0, x1 = x_back, x_back + HALF_BACK
	# stained cap on top of the back (touch: people grab it sliding in)
	cap = pc.bm_box((x1 - x0 + 0.03, LEN + 0.01, 0.03), ((x0 + x1) / 2.0, 0.0, BACK_TOP - 0.015))
	pc.bevel(cap, 0.008, 3)
	asset.add(cap, "Wood_Stained", uv="box", grain="y", touch=1.0)
	asset.hull_box((x1 - x0 + 0.03, LEN, BACK_TOP - SEAT_Z), ((x0 + x1) / 2.0, 0.0, (BACK_TOP + SEAT_Z) / 2.0))
	return asset


def table(seed: int) -> pc.Asset:
	rng = rb_bl.rng("BoothTable", 0, seed)
	asset = pc.Asset("BoothTable", "Booths", "M02", "mid")
	tx, ty, tz = TABLE
	t = 0.032
	top = pc.bm_box((tx - 0.006, ty - 0.006, t), (0.0, 0.0, tz - t / 2.0))
	pc.bevel(top, 0.002, 1)
	asset.add(top, "Laminate_Walnut", uv="box", grain="y", touch=0.6)
	# aluminium T-moulding edge band (6 mm proud of the core on each side, full thickness)
	band = pc.bm_box((tx, ty, t), (0.0, 0.0, tz - t / 2.0))
	pc.bevel(band, 0.003, 2)
	# keep only the side faces of the band (the top / bottom are the laminate's)
	bmesh.ops.delete(band, geom=[f for f in band.faces if abs(f.normal.z) > 0.9], context="FACES")
	asset.add(band, "Alu_Trim", uv="box", grain="y", touch=0.8)
	under = pc.bm_box((0.30, 0.30, 0.02), (0.0, 0.0, tz - t - 0.01))
	asset.add(under, "Paint_BlackSteel", uv="box")
	col = pc.bm_cylinder(0.038, tz - t - 0.06, 32, (0.0, 0.0, 0.04 + (tz - t - 0.06) / 2.0))
	asset.add(col, "Paint_BlackSteel", uv="cyl", touch=0.3)
	asset.hull_cylinder(0.04, 0.04, tz - t, segments=8)
	for ang in (35.0, -35.0):
		a = math.radians(ang)
		for sgn in (1.0, -1.0):
			tip = Vector((sgn * 0.30 * math.cos(a), sgn * 0.30 * math.sin(a), 0.0))
			foot = pc.bm_tube_path([Vector((0.0, 0.0, 0.05)), Vector((tip.x * 0.5, tip.y * 0.5, 0.035)), Vector((tip.x, tip.y, 0.022))], 0.018, 10)
			for vv in foot.verts:
				vv.co.z = max(0.004, vv.co.z)
			asset.add(foot, "Paint_BlackSteel", uv="cyl", touch=0.1)
			lev = pc.bm_cylinder(0.018, 0.012, 16, (tip.x, tip.y, 0.006))
			asset.add(lev, "Rubber_Black", uv="cyl")
			asset.hull_segment(Vector((0.0, 0.0, 0.05)), Vector((tip.x, tip.y, 0.02)), 0.02, 6)
	asset.hull_box((tx, ty, t), (0.0, 0.0, tz - t / 2.0))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	built = []
	for asset_id, double, target in (("BoothBench_End", False, (HALF_BACK + SEAT_D, LEN, BACK_TOP)),
			("BoothBench_Double", True, (2.0 * (HALF_BACK + SEAT_D), LEN, BACK_TOP))):
		if not pc.selected(a.only, asset_id):
			continue
		pc.clear_scene_keep_materials()
		asset = bench(asset_id, double, a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		seat_top = max(vv.co.z for vv in obj.data.vertices if abs(vv.co.y) < 0.4 and vv.co.z < SEAT_Z + 0.01
			and abs(vv.co.x - (-(HALF_BACK + SEAT_D) / 2.0 + HALF_BACK + SEAT_D / 2.0 if not double else HALF_BACK + SEAT_D / 2.0)) < 0.12)
		if abs(seat_top - SEAT_Z) > 0.01:
			rb_bl.fail(f"{asset_id}: seat top {seat_top:.4f}, spec {SEAT_Z}")
		asset.export(out, (hi.x - lo.x, LEN + 0.01, BACK_TOP), acoustic="vinyl_upholstery", meta={
			"element": "E08", "seat_z_m": SEAT_Z, "back_top_z_m": BACK_TOP, "seat_depth_m": SEAT_D,
			"spec_checks": [{"what": "seat top z", "measured_m": round(seat_top, 4), "spec_m": SEAT_Z, "tolerance_m": 0.01}],
			"placement_hint_ue_cm": {"note": "row along venue X at Y 6.25 - 7.32 (centre Y 6.785): end benches at X 2.13 + 0.2925 (+X facing) and "
				"7.92 - 0.2925 (-X facing), double benches centred at X 4.06 and 5.99, tables at the set centres X 3.095 / 5.025 / 6.955",
				"y_cm": 678.5, "yaw_deg": 0.0}})
		if pc.want_preview():
			pc.preview(obj, asset_id, view=(1.0, -0.8, 0.5), zoom=1.3)
		built.append(asset_id)
	if pc.selected(a.only, "BoothTable"):
		pc.clear_scene_keep_materials()
		asset = table(a.seed)
		obj = asset.build()
		lo, hi = asset.bounds()
		asset.export(out, TABLE, acoustic="wood_panel", meta={"element": "E08", "top_z_m": TABLE[2],
			"placement_hint_ue_cm": {"note": "set centres X 3.095 / 5.025 / 6.955, Y 6.785; long side (1.07) along venue Y", "yaw_deg": 0.0}})
		if pc.want_preview():
			pc.preview(obj, "BoothTable", view=(1.0, -0.8, 0.6), zoom=1.4)
		built.append("BoothTable")
	rb_bl.log(f"db_booth: {built}")


main()
