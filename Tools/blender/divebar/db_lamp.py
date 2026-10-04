"""H03 table lamp: the 3-shade brewery-promo lamp over the coin-op table (venue-dive-bar 4.4, E14, 5.1 H03; the bulbs' LIGHTS are
M2-A's, this is the body). Runs INSIDE Blender 5.2:

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_lamp.py [-- --seed 1958 --preview --no-bake]

TableLamp_OldCastor
  * 1.22 m black steel bar (rectangular tube 40 x 40 mm, end caps), the backlit "Old Castor" badge standing on its middle;
  * three spun-steel dome shades, 0.36 m rim diameter, 0.23 m deep, centres 0.46 m apart: dark green enamel outside with the
    Old Castor logo on both long sides, white enamel inside, rolled rim bead; socket cups joining them to the bar;
  * three frosted A19 bulbs (bulb centres 0.15 m above the shade rims = 1.01 m above the bed; the +X one is the mismatched 3000 K
    replacement, its own material slot);
  * two zinc-plated chains 0.86 m long from eye bolts on the bar to ceiling canopies, the cloth-covered cord wound around the -X
    chain; one shade carries a shallow dent (a masse stroke, TS-5), every shade hangs a fraction of a degree off level (9.2).
Pivot: the ceiling plane (z = 0) midway between the two chain anchors (hanging items pivot at their anchor, 13.4, so the swing
pivot is right); the lamp hangs down to z = -1.137 (shade bottoms at 0.86 m above the 0.743 m bed under a 2.74 m ceiling). Long
axis local +X (placed along the table's long axis, yaw 1.5 deg and +2 cm Y by the level, 9.2). Spec asserts (hero +-2 mm): overall
1.280 x 0.360 m over the shades, shade bottom at -1.137 m, rim diameter 0.360, shade depth 0.230, bar 1.220 m, chain 0.86 m.
Anchors (Unreal axes, metres, pivot-relative): bulb centres L1..L3 (the lights), shade mouths (the optional per-shade fill rects
of 4.4), chain anchors, badge centre.

The other light-fixture bodies of the Open rig (venue-dive-bar 4.2; their lights are M2-A's, placed at the exported anchors):
  Pendant_BarCone   L5-L8 bar mini pendant: 0.20 m black enamel cone (white inside), cord, canopy; pivot on the ceiling plane,
                    bulb centre 1.95 m above the floor (anchor bulb_centre) with the exposed amber LampBulb_ST64 (no shadow);
  LampBulb_ST64     vintage 64 x 140 mm filament bulb, MI_DB_Emissive_Bulb2200;
  Sconce_Frosted    L13-L15 booth sconce: brass wall plate + arm, open-top dusty frosted tulip glass (MI_DB_Emissive_SconceGlass),
                    glass centre 1.50 m (E08), pivot on the wall plane at floor level, no shadow / no ray tracing (it encloses the
                    point light; anchor light_centre).
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

CEILING = 2.74
BED = 0.743
SHADE_BOTTOM_ABOVE_BED = 0.86
Z_RIM = BED + SHADE_BOTTOM_ABOVE_BED - CEILING       # -1.137
SHADE_R = 0.18
SHADE_DEPTH = 0.23
Z_SHADE_TOP = Z_RIM + SHADE_DEPTH                     # -0.907
BULB_ABOVE_RIM = 0.15
Z_BULB = Z_RIM + BULB_ABOVE_RIM                       # -0.987 (1.01 m above the bed)
SHADE_X = (-0.46, 0.0, 0.46)
BAR_LEN = 1.22
BAR_W = 0.040
BAR_H = 0.040
Z_BAR_TOP = -0.857                                    # bar top 1.883 m above the floor (TS-5 "bar Z 1.88")
Z_BAR_BOTTOM = Z_BAR_TOP - BAR_H
CHAIN_X = (-0.55, 0.55)
SHEET = 0.0009                                        # spun steel sheet


def shade_radius(z: float) -> float:
	"""Outer radius of the dome at height z (z from Z_RIM to Z_SHADE_TOP): a bell that flares at the rim."""
	t = min(1.0, max(0.0, (z - Z_RIM) / SHADE_DEPTH))        # 0 at the rim, 1 at the top
	# ellipse-like dome with a small flare near the rim
	phi = t * math.radians(84.0)
	r = (SHADE_R - 0.004) * math.cos(phi) ** 0.85 + 0.004 * (1.0 - t) ** 3
	return max(r, 0.030)


def shade_profile(offset: float) -> list[tuple[float, float]]:
	pts = []
	n = 40
	for i in range(n + 1):
		z = Z_RIM + 0.004 + (SHADE_DEPTH - 0.004) * i / n
		pts.append((shade_radius(z) - offset, z))
	return pts


def shade(asset: pc.Asset, cx: float, rng, dent: bool) -> None:
	# outer skin (green enamel) with the neck, closed on top by the socket cup
	# closed at the neck: the socket cup sits on it and no bulb light may leak up through a gap (a bright ring on the ceiling)
	outer = shade_profile(0.0) + [(0.030, Z_SHADE_TOP), (0.0, Z_SHADE_TOP)]
	bm_o = pc.bm_lathe(outer, segments=72, close_bottom=False)
	pc.orient_radial(bm_o, outward=True)
	# inner skin (white enamel), normals toward the axis / down
	inner = shade_profile(SHEET) + [(0.0, Z_SHADE_TOP - SHEET)]
	bm_i = pc.bm_lathe(inner, segments=72, close_bottom=False)
	pc.orient_radial(bm_i, outward=False)
	rim = pc.bm_torus(SHADE_R - 0.0032, 0.0032, 72, 10)
	pc.translate(rim, (0.0, 0.0, Z_RIM + 0.0032))
	if dent:
		# a shallow dent on the +Y side (a cue butt, TS-5): 4 mm deep, ~30 mm radius, applied to both skins
		centre = Vector((0.0, 1.0, 0.0)).normalized()
		zc = Z_RIM + 0.11
		for bm in (bm_o, bm_i):
			for vert in bm.verts:
				r = math.hypot(vert.co.x, vert.co.y)
				if r < 1e-4:
					continue
				d_ang = math.acos(max(-1.0, min(1.0, (vert.co.x * centre.x + vert.co.y * centre.y) / r)))
				d = math.hypot(d_ang * r, vert.co.z - zc)
				push = 0.004 * math.exp(-(d / 0.03) ** 2)
				vert.co.x -= vert.co.x / r * push
				vert.co.y -= vert.co.y / r * push
	# a fraction of a degree off level (the socket cups are not perfectly square to the bar)
	# (mostly about Y: a tilt about X would widen the 0.36 m rim envelope that VDB-T4 checks)
	tilt = Matrix.Translation((0.0, 0.0, Z_SHADE_TOP)) @ Matrix.Rotation(math.radians(rng.uniform(-0.12, 0.12)), 4, "X") @ \
		Matrix.Rotation(math.radians(rng.uniform(-0.3, 0.3)), 4, "Y") @ Matrix.Translation((0.0, 0.0, -Z_SHADE_TOP))
	for bm in (bm_o, bm_i, rim):
		pc.transform(bm, tilt)
	asset.add(bm_o, "Enamel_Green", uv="cyl", smooth_angle=60.0)
	asset.add(bm_i, "Enamel_WhiteInt", uv="cyl", smooth_angle=60.0)
	asset.add(rim, "Enamel_Green", uv="cyl", touch=0.4)
	# the Old Castor logo on both long sides (round, 0.14 m), 0.3 mm proud of the enamel
	lab = pc.label_rect("shade_old_castor")
	size = 0.14
	z_c = Z_RIM + 0.095
	for side in (1.0, -1.0):
		def point(u, v, side=side):
			z = z_c + v
			r = shade_radius(z) + 0.0008
			a = (math.pi / 2.0 if side > 0 else -math.pi / 2.0) + u / max(r, 0.05)
			return Vector((r * math.cos(a), r * math.sin(a), z))
		patch = pc.surface_disc(point, size / 2, lab, rings=10, segments=48)
		pc.transform(patch, tilt)
		asset.add(patch, "Label_Gloss", uv="none", smooth_angle=80.0)
	# socket cup: joins the shade neck to the bar (the E26 socket sits inside)
	cup = pc.bm_lathe([(0.0, Z_SHADE_TOP - 0.010), (0.024, Z_SHADE_TOP - 0.010), (0.026, Z_SHADE_TOP - 0.002), (0.026, Z_BAR_BOTTOM - 0.002),
		(0.019, Z_BAR_BOTTOM), (0.0, Z_BAR_BOTTOM)], segments=32, close_bottom=True)
	pc.translate(cup, (cx, 0.0, 0.0))
	asset.add(cup, "Steel_Black", uv="cyl", touch=0.2)
	asset.hull_points([Vector((cx + SHADE_R * math.cos(a), SHADE_R * math.sin(a), z)) for a in [2 * math.pi * k / 12 for k in range(12)]
		for z in (Z_RIM,)] + [Vector((cx + 0.04 * math.cos(a), 0.04 * math.sin(a), Z_SHADE_TOP)) for a in [2 * math.pi * k / 8 for k in range(8)]],
		floor_clamp=False)


def bulb(asset: pc.Asset, cx: float, slot: str, c: float = Z_BULB) -> None:
	"""Frosted A19, hanging base-up: glass centre at c, E26 base above it into the socket."""
	glass = [(0.0, c - 0.030), (0.012, c - 0.0285), (0.022, c - 0.022), (0.028, c - 0.012), (0.030, c), (0.0285, c + 0.012),
		(0.024, c + 0.025), (0.018, c + 0.038), (0.0145, c + 0.048), (0.0135, c + 0.052)]
	bm = pc.bm_lathe(glass, segments=36, close_bottom=False)
	pc.orient_radial(bm, outward=True)
	pc.translate(bm, (cx, 0.0, 0.0))
	asset.add(bm, slot, uv="cyl", smooth_angle=70.0)
	base = pc.bm_lathe([(0.0135, c + 0.052), (0.0132, c + 0.056)] + [(0.0132 + (0.0008 if k % 2 else 0.0), c + 0.056 + 0.0022 * k)
		for k in range(1, 9)] + [(0.010, c + 0.076), (0.0, c + 0.078)], segments=24, close_bottom=False)
	pc.orient_radial(base, outward=True)
	pc.translate(base, (cx, 0.0, 0.0))
	asset.add(base, "Aluminium", uv="cyl")


def chain(asset: pc.Asset, x: float, z0: float, z1: float, rng) -> None:
	"""Zinc-plated welded link chain between z0 (bottom) and z1 (top): 22 x 12 mm links, 2.4 mm wire, alternating 90 deg."""
	wire = 0.0012
	length, width = 0.022, 0.012
	pitch = length - 2.0 * wire * 2.0          # inner length: consecutive links interlock
	n = int((z1 - z0) / pitch)
	pitch = (z1 - z0) / n
	sway = rng.uniform(-0.002, 0.002)
	for k in range(n):
		zc = z0 + pitch * (k + 0.5)
		r = width / 2.0 - wire
		straight = length / 2.0 - width / 2.0
		pts = []
		for i in range(12):
			a = math.pi * i / 11
			pts.append(Vector((r * math.cos(a), 0.0, straight + r * math.sin(a))))
		for i in range(12):
			a = math.pi + math.pi * i / 11
			pts.append(Vector((r * math.cos(a), 0.0, -straight + r * math.sin(a))))
		link = pc.bm_tube_path(pts, wire, 6, closed=True, cap=False)
		rot = Matrix.Rotation(math.radians(90.0 * (k % 2) + rng.uniform(-6, 6)), 4, "Z")
		pc.transform(link, Matrix.Translation((x + sway * (k / n), 0.0, zc)) @ rot)
		asset.add(link, "Steel_Zinc", uv="cyl", smooth_angle=70.0)
	# eye bolt on the bar and the ceiling canopy + eye
	eye = pc.bm_torus(0.0075, 0.0022, 16, 6)
	pc.transform(eye, Matrix.Translation((x, 0.0, Z_BAR_TOP + 0.008)) @ Matrix.Rotation(math.radians(90), 4, "X"))
	asset.add(eye, "Steel_Black", uv="cyl")
	canopy = pc.bm_lathe([(0.0, -0.006), (0.030, -0.006), (0.034, -0.003), (0.034, 0.0), (0.0, 0.0)], segments=32, close_bottom=True)
	pc.translate(canopy, (x, 0.0, 0.0))
	asset.add(canopy, "Enamel_WhiteInt", uv="cyl")
	top_eye = pc.bm_torus(0.0075, 0.0022, 16, 6)
	pc.transform(top_eye, Matrix.Translation((x, 0.0, -0.014)) @ Matrix.Rotation(math.radians(90), 4, "X"))
	asset.add(top_eye, "Steel_Zinc", uv="cyl")


def cord(asset: pc.Asset, x: float) -> None:
	"""The cloth-covered cord: out of the bar end, wound loosely around the chain, into the ceiling beside the canopy."""
	pts = [Vector((x - 0.035, 0.0, Z_BAR_TOP - 0.004)), Vector((x - 0.038, 0.0, Z_BAR_TOP + 0.012)), Vector((x - 0.030, 0.004, Z_BAR_TOP + 0.035))]
	z = Z_BAR_TOP + 0.05
	a = math.pi
	while z < -0.03:
		pts.append(Vector((x + 0.014 * math.cos(a), 0.014 * math.sin(a), z)))
		a += 0.35
		z += 0.012
	pts.append(Vector((x + 0.036, 0.004, -0.012)))
	pts.append(Vector((x + 0.040, 0.004, -0.0034)))
	asset.add(pc.bm_tube_path(pts, 0.0033, 8), "Plastic_Black", uv="cyl", smooth_angle=70.0)


def bar_and_badge(asset: pc.Asset) -> None:
	bar = pc.bm_box((BAR_LEN, BAR_W, BAR_H), (0.0, 0.0, 0.5 * (Z_BAR_TOP + Z_BAR_BOTTOM)))
	pc.bevel(bar, 0.003, 2)
	asset.add(bar, "Steel_Black", uv="box", grain="x", touch=0.3)
	asset.hull_box((BAR_LEN, BAR_W, BAR_H), (0.0, 0.0, 0.5 * (Z_BAR_TOP + Z_BAR_BOTTOM)))
	# badge housing standing on the bar: 0.32 x 0.05 x 0.125 m, emissive plates (0.30 x 0.107) on both faces
	w, d, h = 0.32, 0.05, 0.125
	zc = Z_BAR_TOP + h / 2.0
	housing = pc.bm_box((w, d, h), (0.0, 0.0, zc))
	pc.bevel(housing, 0.004, 2)
	asset.add(housing, "Steel_Black", uv="box", grain="x")
	asset.hull_box((w, d, h), (0.0, 0.0, zc))
	lab = pc.label_rect("lamp_badge")
	pw, ph = 0.300, 0.107
	for side in (1.0, -1.0):
		yy = side * (d / 2.0 + 0.001)

		def point(u, v, yy=yy, side=side):
			return Vector((u if side < 0 else -u, yy, zc + v))
		plate = pc.surface_patch(point, (-pw / 2, pw / 2), (-ph / 2, ph / 2), 1, 1, lab)
		asset.add(plate, "Emissive_LampBadge", uv="none")
	# end caps
	for sx in (-1.0, 1.0):
		cap = pc.bm_box((0.004, BAR_W + 0.002, BAR_H + 0.002), (sx * (BAR_LEN / 2.0 + 0.001), 0.0, 0.5 * (Z_BAR_TOP + Z_BAR_BOTTOM)))
		pc.bevel(cap, 0.0012, 1)
		asset.add(cap, "Plastic_Black", uv="box")


def build(seed: int) -> pc.Asset:
	rng = pc.rb_bl.rng("TableLamp_OldCastor", 0, seed)
	asset = pc.Asset("TableLamp_OldCastor", "Lighting", "H03", "hero")
	bar_and_badge(asset)
	for k, cx in enumerate(SHADE_X):
		# built around the local axis, then moved: shade() applies the tilt about (cx, 0, top)
		n0 = len(asset.parts)
		shade(asset, cx, rng, dent=(k == 2))
		for part in asset.parts[n0:]:
			if part.mat in ("Enamel_Green", "Enamel_WhiteInt", "Label_Gloss"):
				pc.translate(part.bm, (cx, 0.0, 0.0))
		asset.anchor(f"bulb_L{k + 1}", (cx, 0.0, Z_BULB))
		asset.anchor(f"shade_mouth_{k + 1}", (cx, 0.0, Z_RIM))
	for x in CHAIN_X:
		chain(asset, x, Z_BAR_TOP + 0.014, -0.020, rng)
		asset.anchor(f"chain_anchor_{'neg' if x < 0 else 'pos'}", (x, 0.0, 0.0))
	cord(asset, CHAIN_X[0])
	asset.anchor("badge", (0.0, 0.0, Z_BAR_TOP + 0.0625))
	return asset


# ---- L5-L8 bar mini pendants and L13-L15 booth sconces (venue-dive-bar 4.2; the lights are M2-A's, these are the bodies) --------

P_BULB = 1.95 - CEILING                               # bulb centre 1.95 m above the floor (-0.79 below the ceiling plane)
P_NECK = P_BULB + 0.085                               # cone neck (top of the shade)
P_RIM = P_BULB - 0.045                                # rim: the exposed bulb hangs 3 cm below it
P_R = 0.10                                            # rim radius (0.20 m cone)
S_Z = 1.50                                            # sconce glass centre above the floor (E08: "sconce per booth at Z 1.50")
S_X = 0.13                                            # glass axis out from the wall plane
S_GLASS = [(0.0, -0.070), (0.028, -0.068), (0.050, -0.045), (0.068, -0.010), (0.078, 0.030), (0.082, 0.065)]


def pendant_radius(z: float) -> float:
	t = min(1.0, max(0.0, (z - P_RIM) / (P_NECK - P_RIM)))   # 0 at the rim, 1 at the neck
	return 0.026 + (P_R - 0.026) * (1.0 - t) ** 1.15


def pendant(seed: int) -> pc.Asset:
	"""Black enamel cone pendant over the bar (L5-L8): ceiling canopy, a straight cloth cord, socket cup, 0.20 m spun cone with a
	rolled rim, white enamel inside; the ST64 bulb is its own asset (LampBulb_ST64, no shadow) at the anchor bulb_centre."""
	rng = pc.rb_bl.rng("Pendant_BarCone", 0, seed)
	asset = pc.Asset("Pendant_BarCone", "Lighting", "L5-L8", "mid")
	n = 32
	zs = [P_RIM + 0.0025 + (P_NECK - P_RIM - 0.0025) * i / n for i in range(n + 1)]
	outer = [(pendant_radius(z), z) for z in zs] + [(0.022, P_NECK), (0.0, P_NECK)]
	bm_o = pc.bm_lathe(outer, segments=56, close_bottom=False)
	pc.orient_radial(bm_o, outward=True)
	inner = [(pendant_radius(z) - 0.0008, z) for z in zs] + [(0.0, P_NECK - 0.0008)]
	bm_i = pc.bm_lathe(inner, segments=56, close_bottom=False)
	pc.orient_radial(bm_i, outward=False)
	rim = pc.bm_torus(P_R - 0.0025, 0.0025, 56, 8)
	pc.translate(rim, (0.0, 0.0, P_RIM + 0.0025))
	# hung a little off plumb (the cord took a set), the same for the whole shade
	tilt = Matrix.Translation((0.0, 0.0, P_NECK)) @ Matrix.Rotation(math.radians(rng.uniform(-1.2, 1.2)), 4, "Y") @ \
		Matrix.Translation((0.0, 0.0, -P_NECK))
	for bm in (bm_o, bm_i, rim):
		pc.transform(bm, tilt)
	asset.add(bm_o, "Enamel_Black", uv="cyl", smooth_angle=60.0, touch=0.2)
	asset.add(bm_i, "Enamel_WhiteInt", uv="cyl", smooth_angle=60.0)
	asset.add(rim, "Enamel_Black", uv="cyl", touch=0.5)
	cup = pc.bm_lathe([(0.0, P_NECK - 0.006), (0.021, P_NECK - 0.006), (0.022, P_NECK + 0.040), (0.016, P_NECK + 0.048), (0.0, P_NECK + 0.048)],
		segments=28, close_bottom=True)
	asset.add(cup, "Steel_Black", uv="cyl", touch=0.3)
	cord = pc.bm_tube_path([Vector((0.0, 0.0, P_NECK + 0.046)), Vector((0.0, 0.0, -0.4)), Vector((0.0, 0.0, -0.012))], 0.0028, 8)
	asset.add(cord, "Plastic_Black", uv="cyl", smooth_angle=70.0)
	canopy = pc.bm_lathe([(0.0, -0.024), (0.040, -0.024), (0.052, -0.004), (0.055, 0.0), (0.0, 0.0)], segments=36, close_bottom=True)
	asset.add(canopy, "Steel_Black", uv="cyl")
	asset.hull_points([Vector((P_R * math.cos(a), P_R * math.sin(a), P_RIM)) for a in [2 * math.pi * k / 12 for k in range(12)]]
		+ [Vector((0.025 * math.cos(a), 0.025 * math.sin(a), P_NECK + 0.048)) for a in [2 * math.pi * k / 8 for k in range(8)]], floor_clamp=False)
	asset.anchor("bulb_centre", (0.0, 0.0, P_BULB))
	return asset


def bulb_st64() -> pc.Asset:
	"""Vintage ST64 filament bulb (64 x 140 mm, amber glass) hanging base-up; pivot at the glass centre (like LampBulb_A19)."""
	asset = pc.Asset("LampBulb_ST64", "Lighting", "L5-L8", "mid")
	glass = [(0.0, -0.034), (0.016, -0.031), (0.027, -0.022), (0.032, -0.008), (0.032, 0.004), (0.029, 0.020), (0.022, 0.040),
		(0.015, 0.060), (0.0135, 0.070)]
	bm = pc.bm_lathe(glass, segments=36, close_bottom=False)
	pc.orient_radial(bm, outward=True)
	asset.add(bm, "Emissive_Bulb2200", uv="cyl", smooth_angle=70.0)
	base = pc.bm_lathe([(0.0135, 0.070), (0.0132, 0.074)] + [(0.0132 + (0.0008 if k % 2 else 0.0), 0.074 + 0.0022 * k) for k in range(1, 9)]
		+ [(0.010, 0.094), (0.0, 0.096)], segments=24, close_bottom=False)
	pc.orient_radial(base, outward=True)
	asset.add(base, "Brass_Worn", uv="cyl")
	asset.hull_cylinder(0.032, -0.034, 0.096, segments=10)
	return asset


def sconce(seed: int) -> pc.Asset:
	"""Booth sconce (L13-L15): a worn brass wall plate and arm holding an open-top frosted tulip glass; the whole fixture glows
	(dusty frosted glass around the bulb), so it casts no shadow and stays out of ray tracing (it encloses the point light, 4.1).
	Pivot on the wall plane at floor level (like the wall signs), front +X; the light sits at the anchor light_centre."""
	asset = pc.Asset("Sconce_Frosted", "Lighting", "E08", "mid")
	zc = S_Z
	plate = pc.bm_cylinder(0.055, 0.010, 32, (0.0, 0.0, 0.0))
	pc.transform(plate, Matrix.Translation((0.005, 0.0, zc - 0.06)) @ Matrix.Rotation(math.radians(90.0), 4, "Y"))
	pc.bevel(plate, 0.0015, 1)
	asset.add(plate, "Brass_Worn", uv="box", touch=0.4)
	arm = pc.bm_tube_path([Vector((0.008, 0.0, zc - 0.050)), Vector((0.060, 0.0, zc - 0.052)), Vector((0.100, 0.0, zc - 0.082)),
		Vector((S_X, 0.0, zc - 0.086))], 0.0055, 10)
	asset.add(arm, "Brass_Worn", uv="cyl", touch=0.3)
	cup = pc.bm_lathe([(0.0, zc - 0.090), (0.024, zc - 0.090), (0.030, zc - 0.074), (0.0, zc - 0.074)], segments=28, close_bottom=True)
	pc.translate(cup, (S_X, 0.0, 0.0))
	asset.add(cup, "Brass_Worn", uv="cyl")
	outer = [(r, zc + z) for r, z in S_GLASS]
	inner = [(max(0.0, r - 0.0030), zc + z) for r, z in reversed(S_GLASS)]
	inner[-1] = (0.0, zc + S_GLASS[0][1] + 0.003)
	glass = pc.bm_lathe(outer + [(S_GLASS[-1][0] - 0.0015, zc + S_GLASS[-1][1] + 0.001)] + inner, segments=48, close_bottom=False)
	bmesh.ops.recalc_face_normals(glass, faces=glass.faces)
	pc.translate(glass, (S_X, 0.0, 0.0))
	asset.add(glass, "Emissive_SconceGlass", uv="cyl", smooth_angle=50.0)
	asset.hull_box((0.012, 0.11, 0.11), (0.006, 0.0, zc - 0.06))
	asset.hull_cylinder(0.08, zc - 0.09, zc + 0.065, center_xy=(S_X, 0.0), segments=10)
	asset.anchor("light_centre", (S_X, 0.0, zc - 0.02))
	return asset


def main() -> None:
	a = pc.args()
	rb_bl.reset_scene()
	out = pc.props_out(a)
	fixtures = []
	if pc.selected(a.only, "Pendant_BarCone"):
		pc.clear_scene_keep_materials()
		p = pendant(a.seed)
		pobj = p.build()
		lo, hi = p.bounds()
		if abs(lo.z - P_RIM) > pc.MID_TOL or abs(hi.z) > 1e-4:
			rb_bl.fail(f"Pendant_BarCone: z {lo.z:.4f}..{hi.z:.4f}, spec {P_RIM:.4f}..0")
		p.export(out, (2 * P_R, 2 * P_R, -P_RIM), pivot="ceiling_anchor", acoustic="metal_thin", meta={
			"element": "L5-L8", "bulb_centre_above_floor_m": 1.95, "ceiling_m": CEILING, "rim_diameter_m": 2 * P_R,
			"bulbs": {"asset": "SM_DB_LampBulb_ST64", "cct_k": 2200, "lumens": 300, "note": "the lights are M2-A's (lights.json L5-L8): "
				"a point light at the anchor bulb_centre (Source Radius 1.5 cm, Length 3 cm), shadows on; place SM_DB_LampBulb_ST64 there "
				"(no shadow, hidden from ray tracing, 4.1)"},
			"placement_hint_ue_cm": {"locations": [[x, 195.0, CEILING * 100.0] for x in (300.0, 500.0, 700.0, 900.0)], "yaw_deg": 0.0,
				"note": "venue-dive-bar 4.2 L5-L8: X 3.0 / 5.0 / 7.0 / 9.0, Y 1.95, bulb Z 1.95; pivot on the ceiling plane"}})
		if pc.want_preview():
			pc.preview(pobj, "Pendant_BarCone", view=(0.6, -1.0, -0.3), floor=False, zoom=1.2, target=(0.0, 0.0, -0.75))
		fixtures.append("Pendant_BarCone")
	if pc.selected(a.only, "LampBulb_ST64"):
		pc.clear_scene_keep_materials()
		b = bulb_st64()
		b.build()
		b.export(out, (0.064, 0.064, 0.130), pivot="glass_centre", acoustic="none", collision_profile="RbVenueProp", cast_shadow=False,
			ray_tracing=False, bake=False, meta={"element": "L5-L8", "notes": "place at Pendant_BarCone's anchor bulb_centre"})
		fixtures.append("LampBulb_ST64")
	if pc.selected(a.only, "Sconce_Frosted"):
		pc.clear_scene_keep_materials()
		s = sconce(a.seed)
		sobj = s.build()
		lo, hi = s.bounds()
		want = (S_X + S_GLASS[-1][0], 2 * S_GLASS[-1][0], S_GLASS[-1][1] + 0.115)
		if abs(lo.x) > 1e-4 or abs(hi.z - (S_Z + S_GLASS[-1][1])) > pc.MID_TOL:
			rb_bl.fail(f"Sconce_Frosted: x from {lo.x:.4f} (wall plane 0), top {hi.z:.4f} (spec {S_Z + S_GLASS[-1][1]:.4f})")
		s.export(out, want, pivot="wall_plane_floor", acoustic="glass", collision_profile="RbVenueProp", cast_shadow=False, ray_tracing=False,
			meta={"element": "E08 / L13-L15", "glass_centre_above_floor_m": S_Z,
				"light": {"cct_k": 2400, "lumens": 250, "source_radius_cm": 3.0, "note": "M2-A's point light at the anchor light_centre"},
				"placement_hint_ue_cm": {"locations": [[x, 732.0, 0.0] for x in (309.5, 502.5, 695.5)], "yaw_deg": -90.0,
					"note": "venue-dive-bar E08: one per booth on the right wall (Y 7.32), at the booth set centres, faces -Y"}})
		if pc.want_preview():
			pc.preview(sobj, "Sconce_Frosted", view=(1.0, -0.8, 0.2), floor=False, zoom=1.4, target=(S_X, 0.0, S_Z))
		fixtures.append("Sconce_Frosted")
	if fixtures:
		rb_bl.log(f"db_lamp fixtures: {fixtures}")
	if not pc.selected(a.only, "TableLamp_OldCastor"):
		return
	pc.clear_scene_keep_materials()
	asset = build(a.seed)
	obj = asset.build()
	checks = [
		asset.check_slice(Z_RIM - 0.001, Z_RIM + 0.02, (None, 2 * SHADE_R), pc.HERO_TOL, "shade rim diameter (Y)"),
		asset.check_slice(Z_BAR_BOTTOM - 0.0005, Z_BAR_TOP + 0.0005, (BAR_LEN + 0.004, None), pc.HERO_TOL, "bar length incl. end caps"),
	]
	lo, hi = asset.bounds()
	if abs(lo.z - Z_RIM) > pc.HERO_TOL:
		rb_bl.fail(f"shade bottom at {lo.z:.4f}, spec {Z_RIM:.4f}")
	asset.export(out, (1.28, 2 * SHADE_R, -Z_RIM), pivot="ceiling_anchor", acoustic="metal_thin", meta={
		"element": "E14",
		"spec_checks": checks,
		"shade_bottom_above_bed_m": SHADE_BOTTOM_ABOVE_BED, "bed_height_m": BED, "ceiling_m": CEILING,
		"lamp_underside_height_m": SHADE_BOTTOM_ABOVE_BED,
		"shade": {"rim_diameter_m": 2 * SHADE_R, "depth_m": SHADE_DEPTH, "centres_x_m": list(SHADE_X)},
		"bulbs": {"cct_k": [2700, 2700, 3000], "lumens": [1100, 1100, 1100], "above_rim_m": BULB_ABOVE_RIM, "asset": "SM_DB_LampBulb_A19",
			"note": "the lights are M2-A's (lights.json L1-L3) at the anchors bulb_L1..L3; place SM_DB_LampBulb_A19 there (no shadow, "
			"hidden from ray tracing, 4.1), the L3 one with MI_DB_Emissive_Bulb3000"},
		"chain_length_m": round(-(Z_BAR_TOP + 0.0), 3),
		"placement_hint_ue_cm": {"location": [1375.9, 544.7, 274.0], "yaw_deg": 1.5,
			"note": "venue-dive-bar E14 / 9.2: centred over the cloth, +2 cm Y, 1.5 deg yaw, pivot on the ceiling plane"},
		"notes": "dent on the +X shade (TS-5); the +X bulb is the mismatched 3000 K replacement"})
	if pc.want_preview():
		pc.preview(obj, "TableLamp_OldCastor", view=(0.5, -1.0, -0.35), floor=False, zoom=0.75, target=(0.0, 0.0, -1.0))
	# the bulb glass as its own asset: it encloses the point light, so it must not cast shadows (4.1: hidden from ray tracing too)
	pc.clear_scene_keep_materials()
	b = pc.Asset("LampBulb_A19", "Lighting", "H03", "mid")
	bulb(b, 0.0, "Emissive_Bulb2700", c=0.0)
	bobj = b.build()
	b.export(out, (0.060, 0.060, 0.108), pivot="glass_centre", acoustic="none", collision_profile="RbVenueProp", cast_shadow=False,
		ray_tracing=False, bake=False, meta={"element": "E14", "notes": "place at TableLamp_OldCastor anchors bulb_L1..L3; L3 = MI_DB_Emissive_Bulb3000"})
	rb_bl.log("db_lamp: ['TableLamp_OldCastor', 'LampBulb_A19']")


main()
