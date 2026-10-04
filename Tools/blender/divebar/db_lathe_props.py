"""First lathe-prop set of the dive bar (venue-dive-bar 5.1 H07 / H14, 5.3 C02 / C08, M11): bottles, beer bottles and cans,
glasses (incl. a poured pint), beer mugs of the mug club, the glass ashtray used as the coin dish, the quarter. Runs INSIDE
Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_lathe_props.py [-- --seed 1958 --preview --no-bake --only Bottle_Korvin]

Every asset: pivot at the floor-contact centre (the bottom), front (label) toward +X, real-world sizes (US 750 ml / 1.75 l
spirits, 12 oz bottles and cans, 16 oz shaker pint, US quarter 24.26 x 1.75 mm); the design table DESIGNS is the spec the
generator asserts (+-1 cm, clutter; the quarter +-0.2 mm). Glass is a single two-sided surface (M_DB_Glass, colored
transmittance) with the liquid as its own inner body (fill level per design: back bars are never all full), labels from the text
atlas (TXT, fictional brands, venue-dive-bar 8.1), screw caps / corks / speed pourers on the well bottles. Imported by M2-A's
importer; the back bar (db_backbar.py) places them through its prop_instances.
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

# (asset id) -> design: R body radius, H total height, shoulder / neck shape, label cell + height, glass / liquid slots, fill
DESIGNS = {
	"Bottle_AshbyRidge": dict(kind="spirit", R=0.043, hb=0.175, hs=0.050, rn=0.0145, H=0.300, shoulder="round", label="bottle_ashby_ridge",
		label_z=0.090, glass="Glass_Clear", liquid="Liquid_Whiskey", fill=0.62, closure="cork"),
	"Bottle_Korvin": dict(kind="spirit", R=0.052, hb=0.205, hs=0.060, rn=0.016, H=0.330, shoulder="square", label="bottle_korvin",
		label_z=0.105, glass="Plexi_Scratched", liquid="Liquid_Clear", fill=0.45, closure="pourer"),
	"Bottle_Dockhand": dict(kind="spirit", R=0.041, hb=0.170, hs=0.055, rn=0.0145, H=0.295, shoulder="round", label="bottle_dockhand",
		label_z=0.090, glass="Glass_Clear", liquid="Liquid_Rum", fill=0.35, closure="pourer"),
	"Bottle_Wexmoor": dict(kind="spirit", R=0.040, hb=0.180, hs=0.045, rn=0.0145, H=0.295, shoulder="square", label="bottle_wexmoor",
		label_z=0.095, glass="Glass_Green", liquid="Liquid_Clear", fill=0.78, closure="screw"),
	"Bottle_ElTordo": dict(kind="spirit", R=0.038, hb=0.165, hs=0.070, rn=0.0140, H=0.300, shoulder="slope", label="bottle_el_tordo",
		label_z=0.088, glass="Glass_Clear", liquid="Liquid_Clear", fill=0.55, closure="screw"),
	"Bottle_Ember": dict(kind="spirit", R=0.036, hb=0.160, hs=0.055, rn=0.0135, H=0.280, shoulder="round", label="bottle_ember",
		label_z=0.083, glass="Glass_Clear", liquid="Liquid_Red", fill=0.25, closure="pourer"),
	"Bottle_Frostmint": dict(kind="spirit", R=0.036, hb=0.165, hs=0.050, rn=0.0135, H=0.280, shoulder="slope", label="bottle_frostmint",
		label_z=0.085, glass="Glass_Clear", liquid="Liquid_Green", fill=0.85, closure="screw"),
	"Bottle_OldCastor": dict(kind="beer", R=0.0305, hb=0.105, hs=0.060, rn=0.0125, H=0.235, shoulder="beer", label="bottle_old_castor",
		label_z=0.052, glass="Glass_Amber", liquid="Liquid_Beer", fill=0.30, closure="crown"),
	"Bottle_Hollenbeck": dict(kind="beer", R=0.0305, hb=0.105, hs=0.060, rn=0.0125, H=0.235, shoulder="beer", label="bottle_hollenbeck",
		label_z=0.052, glass="Glass_Amber", liquid="Liquid_Beer", fill=0.92, closure="crown"),
}
CANS = {"Can_LanternFlats": "can_lantern_flats", "Can_Hollenbeck": "bottle_hollenbeck", "Can_OldCastor": "bottle_old_castor"}


# ---- profiles ----------------------------------------------------------------------------------------------------------


def bottle_profile(d: dict) -> list[tuple[float, float]]:
	R, hb, hs, rn, H = d["R"], d["hb"], d["hs"], d["rn"], d["H"]
	pts = [(0.0, 0.006), (R * 0.55, 0.0035), (R - 0.004, 0.0), (R - 0.001, 0.0008), (R, 0.004), (R, hb * 0.5), (R, hb)]
	for i in range(1, 13):
		t = i / 12.0
		if d["shoulder"] == "round":
			f = math.sqrt(max(0.0, 1.0 - t * t))
		elif d["shoulder"] == "square":
			f = 1.0 - t ** 4 if t < 0.8 else (1.0 - 0.8 ** 4) * (1.0 - (t - 0.8) / 0.2) ** 2
		elif d["shoulder"] == "slope":
			f = (1.0 - t) ** 1.2
		else:                                 # beer long-neck: a long ogee shoulder
			f = 0.5 + 0.5 * math.cos(math.pi * t)
		pts.append((rn + (R - rn) * f, hb + hs * t))
	neck_top = H - 0.004
	pts += [(rn * 0.97, hb + hs + (neck_top - hb - hs) * 0.5), (rn, neck_top - 0.012), (rn + 0.0018, neck_top - 0.006),
		(rn + 0.0018, neck_top), (rn - 0.002, H), (0.0, H)]
	return pts


def r_at(profile, z: float) -> float:
	for (r0, z0), (r1, z1) in zip(profile, profile[1:]):
		if z0 <= z <= z1 and z1 > z0:
			return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
	return profile[-1][0]


def liquid_body(profile, z_fill: float, wall: float = 0.0032, bottom: float = 0.008) -> bmesh.types.BMesh:
	pts = [(0.0, bottom)]
	zs = sorted({z for _, z in profile if bottom < z < z_fill} | {bottom + 0.001, z_fill})
	for z in zs:
		pts.append((max(0.001, r_at(profile, z) - wall), z))
	pts.append((0.0, z_fill))
	bm = pc.bm_lathe(pts, segments=40, close_bottom=False)
	pc.orient_radial(bm, outward=True)
	return bm


def label_patch(asset: pc.Asset, cell: str, radius: float, zc: float, width: float | None = None, height: float | None = None,
		slot: str = "Label_Atlas") -> None:
	info = pc.label(cell)
	w, h = info["size_m"]
	w = width or w
	h = height or h
	r = radius + 0.0004
	w = min(w, 2.0 * math.pi * r * 0.98)

	def point(u, v):
		a = u / r
		return Vector((r * math.cos(a), r * math.sin(a), zc + v))
	bm = pc.surface_patch(point, (-w / 2, w / 2), (-h / 2, h / 2), max(8, int(w / 0.006)), 2, pc.label_rect(cell))
	asset.add(bm, slot, uv="none", smooth_angle=80.0)


# ---- bottles ---------------------------------------------------------------------------------------------------------


def closure(asset: pc.Asset, d: dict, rng) -> None:
	rn, H = d["rn"], d["H"]
	kind = d["closure"]
	if kind == "screw":
		cap = pc.bm_lathe([(0.0, H - 0.020), (rn + 0.0028, H - 0.020), (rn + 0.0030, H - 0.001), (rn + 0.0018, H + 0.0015), (0.0, H + 0.0015)],
			segments=32, close_bottom=True)
		asset.add(cap, "Aluminium", uv="cyl", touch=0.6)
	elif kind == "cork":
		cap = pc.bm_lathe([(0.0, H - 0.006), (rn + 0.0060, H - 0.006), (rn + 0.0065, H + 0.010), (rn + 0.0050, H + 0.0135), (0.0, H + 0.0135)],
			segments=32, close_bottom=True)
		asset.add(cap, "Wood_Stained", uv="cyl", touch=0.8)
	elif kind == "crown":
		cap = pc.bm_lathe([(0.0, H - 0.004)] + [(rn + 0.0022 + (0.0006 if k % 2 else 0.0), H - 0.004 + 0.0002 * k) for k in range(1, 2)]
			+ [(rn + 0.0024, H - 0.004), (rn + 0.0024, H + 0.002), (0.0, H + 0.0025)], segments=21, close_bottom=True)
		asset.add(cap, "Paint_Cabinet", uv="cyl")
	else:                                          # speed pourer: black rubber collar + chrome spout at ~35 deg
		collar = pc.bm_lathe([(0.0, H - 0.014), (rn + 0.0025, H - 0.014), (rn + 0.0030, H + 0.004), (rn * 0.6, H + 0.010), (0.0, H + 0.010)],
			segments=24, close_bottom=True)
		asset.add(collar, "Rubber_Black", uv="cyl")
		ang = math.radians(35.0)
		pts = [Vector((0.0, 0.0, H + 0.008)), Vector((0.012 * math.sin(ang) * 0.3, 0.0, H + 0.018)),
			Vector((0.045 * math.sin(ang), 0.0, H + 0.008 + 0.045 * math.cos(ang)))]
		spout = pc.bm_tube_path(pts, 0.0042, 12)
		asset.add(spout, "Steel_Stainless", uv="cyl", touch=0.5)


def bottle(asset_id: str, seed: int) -> pc.Asset:
	d = DESIGNS[asset_id]
	rng = rb_bl.rng(asset_id, 0, seed)
	asset = pc.Asset(asset_id, "Bottles", "C02" if d["kind"] == "spirit" else "H14", "mid")
	prof = bottle_profile(d)
	glass = pc.bm_lathe(prof, segments=48, close_bottom=False)
	pc.orient_radial(glass, outward=True)
	asset.add(glass, d["glass"], uv="cyl", smooth_angle=60.0, touch=0.4)
	z_fill = 0.008 + (d["hb"] + d["hs"] * 0.6 - 0.008) * d["fill"]
	asset.add(liquid_body(prof, z_fill), d["liquid"], uv="cyl", smooth_angle=60.0)
	label_patch(asset, d["label"], d["R"], d["label_z"])
	closure(asset, d, rng)
	asset.hull_cylinder(d["R"], 0.0, d["hb"] + d["hs"] * 0.5, segments=12)
	asset.hull_cylinder(d["rn"] + 0.003, d["hb"] + d["hs"] * 0.5, d["H"], segments=8)
	return asset


def can(asset_id: str, cell: str, seed: int) -> pc.Asset:
	"""12 oz can: 66.2 mm body, 122.2 mm tall, necked top, stay-tab lid."""
	asset = pc.Asset(asset_id, "Cans", "C03", "mid")
	R = 0.0331
	prof = [(0.0, 0.0045), (0.024, 0.001), (0.0265, 0.0), (0.0295, 0.0025), (R, 0.012), (R, 0.108), (0.031, 0.1145), (0.0275, 0.1195),
		(0.0272, 0.1222), (0.0262, 0.1222), (0.0258, 0.119), (0.0, 0.1185)]
	body = pc.bm_lathe(prof, segments=48, close_bottom=False)
	pc.orient_radial(body, outward=True)
	asset.add(body, "Aluminium", uv="cyl", touch=0.5)
	info = pc.label(cell)
	label_patch(asset, cell, R, 0.060, width=2.0 * math.pi * R * 0.985, height=0.094, slot="Label_Gloss")
	tab = pc.bm_box((0.020, 0.012, 0.0008), (0.008, 0.0, 0.1192))
	asset.add(tab, "Aluminium", uv="box")
	asset.hull_cylinder(R, 0.0, 0.1222, segments=12)
	return asset


# ---- glasses ---------------------------------------------------------------------------------------------------------


def pint(asset_id: str, beer: bool) -> pc.Asset:
	"""16 oz shaker pint (h 150 mm, rim 86 mm, base 62 mm, 5 mm heavy base); poured: beer to 128 mm + 12 mm foam, lacing."""
	asset = pc.Asset(asset_id, "Glasses", "H14", "mid")
	rb, rt, h = 0.031, 0.043, 0.150
	outer = [(0.0, 0.0), (rb - 0.002, 0.0), (rb, 0.002), (rb + 0.0004, 0.012), (rt - 0.0015, h - 0.004), (rt, h - 0.001), (rt - 0.001, h)]
	inner = [(rt - 0.0035, h), (rt - 0.0038, h - 0.004), (rb - 0.0025, 0.014), (rb - 0.006, 0.012), (0.0, 0.0115)]
	g = pc.bm_lathe(outer + inner, segments=56, close_bottom=False)
	bmesh.ops.recalc_face_normals(g, faces=g.faces)
	asset.add(g, "Glass_Clear", uv="cyl", smooth_angle=50.0, touch=0.6)
	if beer:
		zf = 0.128

		def rin(z):
			return (rb - 0.0025) + (rt - 0.0038 - rb + 0.0025) * (z - 0.014) / (h - 0.018)
		liquid = [(0.0, 0.0125), (rb - 0.003, 0.0125), (rin(0.03) - 0.0004, 0.03), (rin(zf) - 0.0004, zf), (0.0, zf)]
		lb = pc.bm_lathe(liquid, segments=48, close_bottom=False)
		pc.orient_radial(lb, outward=True)
		asset.add(lb, "Liquid_Beer", uv="cyl", smooth_angle=60.0)
		foam = [(rin(zf) - 0.0006, zf - 0.001), (rin(zf + 0.008) - 0.0005, zf + 0.008), (rin(zf + 0.012) - 0.002, zf + 0.0125),
			(rin(zf) * 0.6, zf + 0.0138), (0.0, zf + 0.014)]
		fb = pc.bm_lathe(foam, segments=48, close_bottom=False)
		pc.orient_radial(fb, outward=True)
		asset.add(fb, "Beer_Foam", uv="cyl", smooth_angle=60.0)
	asset.hull_cylinder(rt, 0.0, h, segments=12)
	return asset


def rocks() -> pc.Asset:
	asset = pc.Asset("Glass_Rocks", "Glasses", "C02", "mid")
	r, h = 0.042, 0.090
	prof = [(0.0, 0.0), (r - 0.003, 0.0), (r, 0.003), (r, h - 0.001), (r - 0.0015, h), (r - 0.003, h - 0.001), (r - 0.003, 0.018),
		(r - 0.006, 0.016), (0.0, 0.0155)]
	g = pc.bm_lathe(prof, segments=48, close_bottom=False)
	bmesh.ops.recalc_face_normals(g, faces=g.faces)
	asset.add(g, "Glass_Clear", uv="cyl", smooth_angle=45.0, touch=0.6)
	asset.hull_cylinder(r, 0.0, h, segments=12)
	return asset


def shot() -> pc.Asset:
	asset = pc.Asset("Glass_Shot", "Glasses", "C02", "mid")
	rb, rt, h = 0.020, 0.025, 0.060
	prof = [(0.0, 0.0), (rb - 0.002, 0.0), (rb, 0.002), (rt, h - 0.001), (rt - 0.0012, h), (rt - 0.0025, h - 0.001), (rb - 0.0025, 0.016),
		(0.0, 0.0155)]
	g = pc.bm_lathe(prof, segments=40, close_bottom=False)
	bmesh.ops.recalc_face_normals(g, faces=g.faces)
	asset.add(g, "Glass_Clear", uv="cyl", smooth_angle=45.0, touch=0.6)
	asset.hull_cylinder(rt, 0.0, h, segments=12)
	return asset


def mug(asset_id: str, shape: str, rng) -> pc.Asset:
	"""Mug-club stoneware mugs (M11): A barrel, B straight with a foot ring, C tapered stein; glazed, handle on +Y."""
	asset = pc.Asset(asset_id, "Mugs", "M11", "mid")
	if shape == "A":
		prof = [(0.0, 0.004), (0.036, 0.0), (0.040, 0.004), (0.046, 0.04), (0.047, 0.075), (0.044, 0.115), (0.043, 0.125),
			(0.0405, 0.125), (0.0415, 0.112), (0.0435, 0.075), (0.0425, 0.035), (0.036, 0.012), (0.0, 0.011)]
	elif shape == "B":
		prof = [(0.0, 0.006), (0.036, 0.006), (0.037, 0.0), (0.042, 0.0), (0.043, 0.008), (0.043, 0.132), (0.0415, 0.134),
			(0.0395, 0.132), (0.0395, 0.014), (0.0, 0.013)]
	else:
		prof = [(0.0, 0.003), (0.046, 0.0), (0.049, 0.006), (0.047, 0.02), (0.041, 0.14), (0.042, 0.146), (0.039, 0.147),
			(0.0375, 0.14), (0.0435, 0.02), (0.042, 0.013), (0.0, 0.012)]
	body = pc.bm_lathe(prof, segments=48, close_bottom=False)
	bmesh.ops.recalc_face_normals(body, faces=body.faces)
	asset.add(body, "Ceramic_Glazed", uv="cyl", smooth_angle=45.0, touch=0.5)
	h = max(z for _, z in prof)
	outer = sorted([(r, z) for r, z in prof[:7] if r > 0.03], key=lambda p: p[1])
	r_side = r_at(outer, h * 0.55)
	top, bot = h * 0.82, h * 0.25
	pts = [Vector((0.0, r_side - 0.003, top)), Vector((0.0, r_side + 0.020, top + 0.004)), Vector((0.0, r_side + 0.036, top - 0.02)),
		Vector((0.0, r_side + 0.037, bot + 0.03)), Vector((0.0, r_side + 0.022, bot)), Vector((0.0, r_side - 0.003, bot - 0.002))]
	handle = pc.bm_tube_path(pts, 0.0065, 12)
	for vert in handle.verts:                       # oval section: flatter across (x)
		vert.co.x *= 1.6
	asset.add(handle, "Ceramic_Glazed", uv="cyl", touch=1.0)
	asset.hull_cylinder(max(r for r, _ in prof), 0.0, max(z for _, z in prof), segments=12)
	asset.hull_points([Vector((sx * 0.012, r_side, top)) for sx in (-1, 1)] + [Vector((sx * 0.012, r_side + 0.043, zz)) for sx in (-1, 1)
		for zz in (top, bot)] + [Vector((sx * 0.012, r_side, bot)) for sx in (-1, 1)])
	return asset


def ashtray() -> pc.Asset:
	"""C08: heavy round glass ashtray (105 mm, 32 mm) with three cigarette rests - now the coin dish (S5)."""
	asset = pc.Asset("Ashtray_Glass", "Clutter", "C08", "mid")
	R, h = 0.0525, 0.032
	prof = [(0.0, 0.0), (R - 0.006, 0.0), (R - 0.002, 0.002), (R, 0.010), (R - 0.001, h - 0.002), (R - 0.004, h), (R - 0.011, h),
		(R - 0.014, h - 0.004), (R - 0.018, 0.016), (R - 0.024, 0.0135), (0.0, 0.013)]
	bm = pc.bm_lathe(prof, segments=72, close_bottom=False)
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	# three rests: notches cut into the rim (vertices near the rest angles pushed down)
	for vert in bm.verts:
		a = math.atan2(vert.co.y, vert.co.x)
		r = math.hypot(vert.co.x, vert.co.y)
		if r > R - 0.016 and vert.co.z > h - 0.006:
			for k in range(3):
				da = abs((a - 2 * math.pi * k / 3 + math.pi) % (2 * math.pi) - math.pi)
				if da < 0.12:
					vert.co.z -= 0.006 * (1.0 - (da / 0.12) ** 2)
	asset.add(bm, "Glass_Ashtray", uv="cyl", smooth_angle=40.0, touch=0.7)
	asset.hull_cylinder(R, 0.0, h, segments=12)
	return asset


def quarter() -> pc.Asset:
	"""H07: US quarter, 24.26 mm x 1.75 mm, 119 reeds on the edge, a raised rim; no mint design (generic relief only)."""
	asset = pc.Asset("Coin_Quarter", "Clutter", "H07", "mid")
	R, t = 0.02426 / 2.0, 0.00175
	n = 119 * 2
	bm = bmesh.new()
	rings = []
	for z, rr in ((0.0, R - 0.0002), (0.0002, R), (t - 0.0002, R), (t, R - 0.0002)):
		ring = []
		for i in range(n):
			a = 2 * math.pi * i / n
			rad = rr - (0.00012 if i % 2 else 0.0) if 0.0 < z < t else rr
			ring.append(bm.verts.new((rad * math.cos(a), rad * math.sin(a), z)))
		rings.append(ring)
	for k in range(3):
		for i in range(n):
			j = (i + 1) % n
			bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]))
	# faces: a raised rim ring + a slightly recessed field (generic relief)
	for z, ring, sgn in ((0.0, rings[0], -1), (t, rings[3], 1)):
		inner = [bm.verts.new((0.92 * v.co.x, 0.92 * v.co.y, z)) for v in ring]
		field = [bm.verts.new((0.90 * v.co.x, 0.90 * v.co.y, z - sgn * 0.00015)) for v in ring]
		for i in range(n):
			j = (i + 1) % n
			bm.faces.new((ring[i], ring[j], inner[j], inner[i]))
			bm.faces.new((inner[i], inner[j], field[j], field[i]))
		bm.faces.new(field)
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	asset.add(bm, "CuproNickel", uv="box", uv_scale=1.0, smooth_angle=30.0, touch=0.8)
	asset.hull_cylinder(R, 0.0, t, segments=12)
	return asset


# ---- main ------------------------------------------------------------------------------------------------------------


def export(asset: pc.Asset, out, target, tol=None, meta=None, preview_zoom=1.6, wm_res=512, translucent=False) -> None:
	obj = asset.obj or asset.build()
	asset.export(out, target, tolerance=tol, wm_res=wm_res, collision_profile="RbVenueProp", acoustic="glass", translucent=translucent,
		meta=dict(meta or {}, placement="prop_instances of db_backbar.py / the level's clutter scatter (M2-A)"))
	if pc.want_preview():
		pc.preview(obj, asset.asset_id, res=480, view=(1.0, -0.6, 0.45), zoom=preview_zoom)


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	built = []
	for asset_id, d in DESIGNS.items():
		if not pc.selected(a.only, asset_id):
			continue
		pc.clear_scene_keep_materials()
		asset = bottle(asset_id, a.seed)
		asset.build()
		lo, hi = asset.bounds()
		# spec: body diameter 2R, height H (+ the closure) - asserted within the clutter tolerance
		export(asset, out, (2 * d["R"] + (hi.x - lo.x - 2 * d["R"]), 2 * d["R"], hi.z), meta={"design": {k: v for k, v in d.items()},
			"body_diameter_m": 2 * d["R"], "glass_height_m": d["H"]}, translucent=True)
		# the glass is H tall; closures (cork, pourer spout) add up to 5 cm on top
		if not (-0.005 < hi.z - d["H"] < 0.05) or abs((hi.y - lo.y) - 2 * d["R"]) > pc.MID_TOL:
			rb_bl.fail(f"{asset_id}: {hi.z:.3f} x {hi.y - lo.y:.3f} vs design {d['H']} x {2 * d['R']}")
		built.append(asset_id)
	for asset_id, cell in CANS.items():
		if pc.selected(a.only, asset_id):
			pc.clear_scene_keep_materials()
			export(can(asset_id, cell, a.seed), out, (0.0662, 0.0662, 0.1222), meta={"spec": "US 12 oz can 66.2 x 122.2 mm"})
			built.append(asset_id)
	for asset_id, beer in (("Glass_Pint", False), ("Glass_Pint_Beer", True)):
		if pc.selected(a.only, asset_id):
			pc.clear_scene_keep_materials()
			export(pint(asset_id, beer), out, (0.086, 0.086, 0.150 + (0.0 if not beer else 0.0)), meta={"spec": "16 oz shaker pint"},
				translucent=True)
			built.append(asset_id)
	for fn, asset_id, target in ((rocks, "Glass_Rocks", (0.084, 0.084, 0.090)), (shot, "Glass_Shot", (0.050, 0.050, 0.060)),
			(ashtray, "Ashtray_Glass", (0.105, 0.105, 0.032))):
		if pc.selected(a.only, asset_id):
			pc.clear_scene_keep_materials()
			export(fn(), out, target, translucent=True)
			built.append(asset_id)
	for shape in ("A", "B", "C"):
		asset_id = f"Mug_{shape}"
		if pc.selected(a.only, asset_id):
			pc.clear_scene_keep_materials()
			asset = mug(asset_id, shape, rb_bl.rng(asset_id, 0, a.seed))
			asset.build()
			lo, hi = asset.bounds()
			export(asset, out, tuple(hi - lo), meta={"spec": "stoneware beer mug, 0.4-0.5 l"})
			built.append(asset_id)
	if pc.selected(a.only, "Coin_Quarter"):
		pc.clear_scene_keep_materials()
		export(quarter(), out, (0.02426, 0.02426, 0.00175), tol=0.0002, meta={"spec": "US quarter 24.26 x 1.75 mm, 119 reeds"},
			preview_zoom=2.5, wm_res=128)
		built.append("Coin_Quarter")
	rb_bl.log(f"db_lathe_props: {built}")


if __name__ == "__main__":
	main()
