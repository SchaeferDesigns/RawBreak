"""Shared helpers of M2-B's dive-bar prop generators (runs INSIDE Blender 5.2; venue-dive-bar 5, 9.2, 13.3-13.5;
Docs/ue-architecture.md 18.8). Import from a generator with:

	import sys, pathlib
	HERE = pathlib.Path(__file__).resolve().parent
	sys.path.insert(0, str(HERE.parent / "common")); sys.path.insert(0, str(HERE))
	import rb_bl, db_props_common as pc

Conventions of every M2-B prop (the contract with M2-A's importer / level generator, written into each <Asset>.json):
  * metric, 1 BU = 1 m; Blender (x, y, z) -> Unreal (x, -y, z) (FBX default axes, Interchange: the model looks the same, only the
    sign of Y flips because Unreal is left-handed). Generators therefore build in Blender coordinates and speak Unreal only in the
    metadata (`*_ue_m` fields have Y negated).
  * FRONT = local +X for every prop that has one (the level generator yaws it into the room); left / right below are as seen
    from the front.
  * pivots: floor items at the floor-contact centre (z = 0); wall items at the wall plane, horizontally centred, z = 0 = FLOOR
    level (so every height in the asset is the absolute height above the finished floor); hanging items at the ceiling anchor.
  * UV0 = world scale (1 UV unit = 1 m) for the tiling CC0 inputs (box / cylindrical / grain-aligned projection); labels use
    UV0 inside their atlas cell; UV1 = unique, non-overlapping (smart project + pack, >= 4 px padding) for T_DB_<Asset>_WM.
  * material slot names are the MI_DB_* names of venue-dive-bar 6.3 (the importer assigns the instances by name).
  * UCX_SM_DB_<Asset>_<nn> convex hulls: leg-accurate (balls roll under stools and tables), closed plinths where the spec says so.
  * every hard edge bevelled 0.5-3 mm (9.2), seeded jitter (rb_bl.rng), shelves sag by a beam model.
  * the wear mask T_DB_<Asset>_WM (UV1, linear RGBA): R = convexity (edge band, Cycles AO "inside"), G = ambient occlusion
    (Cycles AO bake, 0.15 m), B = authored touch mask (hand contact, per-vertex attribute rb_touch), A = height above the floor
    normalised per asset.
Owner: M2-B.
"""

from __future__ import annotations

import csv
import hashlib
import json
import math
import os
import struct
import sys
import zlib
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "common"))
import rb_bl  # noqa: E402

REPO = rb_bl.REPO
TEXTURE_ROOT = REPO / "Art" / "DiveBar" / "Textures"
LEDGER = REPO / "Docs" / "licenses" / "ledger" / "M2-B.csv"
UE_PROPS = "/Game/Generated/Venues/DiveBar/Props"
LEDGER_COLUMNS = ["asset_id", "used_by", "source", "source_ref", "author", "licence", "licence_url", "date", "account", "sha256",
	"modified", "ai_generated", "steam_ai_disclosure", "trademark_check", "notes"]
LEDGER_DATE = "2026-09-29"  # rows are written once (dedup by asset id), so re-runs never change the file

HERO_TOL = 0.002
MID_TOL = 0.01

# Preview colours of the material slots (Blender shape previews only; the look lives in Unreal's MI_DB_* instances).
PREVIEW = {
	"Vinyl_Oxblood": (0.20, 0.03, 0.025, 0.45), "Vinyl_Black": (0.02, 0.02, 0.02, 0.4), "Chrome": (0.8, 0.8, 0.8, 0.08),
	"Chrome_Pitted": (0.75, 0.75, 0.75, 0.15), "Foam_Exposed": (0.75, 0.62, 0.30, 0.9), "Tape": (0.35, 0.36, 0.36, 0.5),
	"Wood_Lacquered_Bar": (0.25, 0.08, 0.03, 0.15), "Wood_PlankWall": (0.20, 0.11, 0.06, 0.7), "Brass_Worn": (0.7, 0.5, 0.2, 0.3),
	"Wood_Stained": (0.14, 0.07, 0.03, 0.5), "Mirror_Aged": (0.9, 0.9, 0.9, 0.02), "Glass_Shelf": (0.7, 0.8, 0.75, 0.02),
	"Laminate_Walnut": (0.18, 0.10, 0.05, 0.35), "Enamel_Green": (0.02, 0.10, 0.04, 0.2), "Enamel_WhiteInt": (0.8, 0.8, 0.75, 0.3),
	"Steel_Black": (0.03, 0.03, 0.03, 0.4), "Steel_Zinc": (0.6, 0.6, 0.58, 0.35), "Felt_Green": (0.03, 0.12, 0.05, 0.9),
	"Chalk_Blue": (0.1, 0.3, 0.7, 0.95), "Paint_Cabinet": (0.03, 0.03, 0.035, 0.3), "Plexi_Emissive": (0.9, 0.3, 0.2, 0.1),
	"Paint_BlackSteel": (0.03, 0.03, 0.03, 0.5), "Plywood_Painted": (0.1, 0.07, 0.05, 0.6), "Rubber_Black": (0.02, 0.02, 0.02, 0.8),
	"Glass_Clear": (0.9, 0.95, 0.92, 0.02), "Glass_Amber": (0.5, 0.25, 0.05, 0.02), "Glass_Green": (0.1, 0.4, 0.15, 0.02),
	"Liquid_Whiskey": (0.5, 0.2, 0.03, 0.02), "Label_Atlas": (0.8, 0.75, 0.6, 0.6), "Aluminium": (0.8, 0.8, 0.82, 0.3),
	"CuproNickel": (0.7, 0.7, 0.68, 0.3), "Plastic_Black": (0.02, 0.02, 0.02, 0.4), "Emissive_Screen": (0.1, 0.5, 0.2, 0.3),
}


# --------------------------------------------------------------------------------------------------------------------
# small maths
# --------------------------------------------------------------------------------------------------------------------


def v(x, y=0.0, z=0.0) -> Vector:
	return Vector((x, y, z))


def ue(p) -> list[float]:
	"""Blender point -> Unreal convention (metres, Y negated), rounded for the metadata."""
	return [round(p[0], 5), round(-p[1], 5), round(p[2], 5)]


def smoothstep(a: float, b: float, x: float) -> float:
	t = min(1.0, max(0.0, (x - a) / (b - a)))
	return t * t * (3.0 - 2.0 * t)


def beam_sag(x: float, span: float, max_sag: float) -> float:
	"""Deflection of a simply supported beam under a uniform load (w(x) = 16 d x (L^3 - 2 L x^2 + x^3) / (5 L^4)), x in [0, L]:
	0 at the supports, max_sag at mid-span (venue-dive-bar 9.2: shelves sag 3-8 mm under bottles)."""
	if span <= 0.0:
		return 0.0
	x = min(max(x, 0.0), span)
	return 16.0 * max_sag * x * (span ** 3 - 2.0 * span * x * x + x ** 3) / (5.0 * span ** 4)


# --------------------------------------------------------------------------------------------------------------------
# scene / materials
# --------------------------------------------------------------------------------------------------------------------


def material(slot: str) -> bpy.types.Material:
	"""The Blender material of a slot MI_DB_<slot> (preview colour only)."""
	name = slot if slot.startswith("MI_") else f"MI_DB_{slot}"
	mat = bpy.data.materials.get(name)
	if mat is None:
		mat = bpy.data.materials.new(name)
		key = name[len("MI_DB_"):] if name.startswith("MI_DB_") else name
		r, g, b, rough = PREVIEW.get(key, (0.5, 0.5, 0.5, 0.5))
		mat.use_nodes = True
		bsdf = mat.node_tree.nodes.get("Principled BSDF")
		if bsdf is not None:
			bsdf.inputs["Base Color"].default_value = (r, g, b, 1.0)
			bsdf.inputs["Roughness"].default_value = rough
			if key.startswith(("Chrome", "Brass", "Steel_Zinc", "Aluminium", "CuproNickel", "Mirror")):
				bsdf.inputs["Metallic"].default_value = 1.0
			if key.startswith(("Glass", "Liquid")):
				bsdf.inputs["Transmission Weight"].default_value = 1.0
	return mat


def _link(obj) -> None:
	bpy.context.scene.collection.objects.link(obj)


# --------------------------------------------------------------------------------------------------------------------
# parts: small meshes built with bmesh, joined into one asset object at the end
# --------------------------------------------------------------------------------------------------------------------


class Part:
	"""One piece of an asset: a bmesh with one material slot, a UV0 projection mode and a touch value."""

	def __init__(self, bm: bmesh.types.BMesh, mat: str, uv: str = "box", touch: float = 0.0, grain: str = "x", uv_scale: float = 1.0,
			smooth_angle: float = 35.0, label_rect=None):
		self.bm = bm
		self.mat = mat
		self.uv = uv            # box | grain | cyl | label | none
		self.touch = touch
		self.grain = grain      # box projection: which axis U should follow on faces that contain it
		self.uv_scale = uv_scale
		self.smooth_angle = smooth_angle
		self.label_rect = label_rect  # (u0, v0, u1, v1) atlas cell for uv == "label"


def bm_box(size, center=(0.0, 0.0, 0.0)) -> bmesh.types.BMesh:
	bm = bmesh.new()
	bmesh.ops.create_cube(bm, size=1.0)
	bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
	bmesh.ops.translate(bm, vec=Vector(center), verts=bm.verts)
	return bm


def bm_cylinder(radius: float, depth: float, segments: int = 24, center=(0.0, 0.0, 0.0), cap: bool = True, radius2: float | None = None) -> bmesh.types.BMesh:
	"""Z-axis cylinder (or cone frustum with radius2 at the top) centred on center."""
	bm = bmesh.new()
	bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segments, radius1=radius, radius2=radius if radius2 is None else radius2,
		depth=depth)
	bmesh.ops.translate(bm, vec=Vector(center), verts=bm.verts)
	return bm


def bm_lathe(profile, segments: int = 32, close_bottom: bool = True, close_top: bool = False, angle: float = 2.0 * math.pi) -> bmesh.types.BMesh:
	"""Revolves a profile [(r, z), ...] (bottom to top) about Z. Points with r == 0 become poles; open ends can be closed."""
	bm = bmesh.new()
	full = abs(angle - 2.0 * math.pi) < 1e-6
	steps = segments if full else segments + 1
	rings = []
	for r, z in profile:
		ring = []
		if r <= 1e-7:
			ring = [bm.verts.new((0.0, 0.0, z))]
		else:
			for i in range(steps):
				a = angle * i / segments
				ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), z)))
		rings.append(ring)
	for k in range(len(rings) - 1):
		a, b = rings[k], rings[k + 1]
		for i in range(segments):
			i1 = (i + 1) % steps if full else i + 1
			if len(a) == 1 and len(b) == 1:
				continue
			if len(a) == 1:
				bm.faces.new((a[0], b[i1], b[i]))
			elif len(b) == 1:
				bm.faces.new((a[i], a[i1], b[0]))
			else:
				bm.faces.new((a[i], a[i1], b[i1], b[i]))
	if full:
		if close_bottom and len(rings[0]) > 2:
			bm.faces.new(list(reversed(rings[0])))
		if close_top and len(rings[-1]) > 2:
			bm.faces.new(rings[-1])
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def bm_tube_path(points, radius: float, segments: int = 12, closed: bool = False, cap: bool = True) -> bmesh.types.BMesh:
	"""Round tube along a polyline (parallel-transport frames)."""
	bm = bmesh.new()
	pts = [Vector(p) for p in points]
	n = len(pts)
	tangents = []
	for i in range(n):
		if closed:
			t = pts[(i + 1) % n] - pts[i - 1]
		elif i == 0:
			t = pts[1] - pts[0]
		elif i == n - 1:
			t = pts[-1] - pts[-2]
		else:
			t = (pts[i + 1] - pts[i]).normalized() + (pts[i] - pts[i - 1]).normalized()
		tangents.append(t.normalized())
	ref = Vector((0.0, 0.0, 1.0)) if abs(tangents[0].z) < 0.9 else Vector((1.0, 0.0, 0.0))
	normal = (ref - tangents[0] * ref.dot(tangents[0])).normalized()
	rings = []
	for i in range(n):
		if i > 0:
			axis = tangents[i - 1].cross(tangents[i])
			if axis.length > 1e-8:
				ang = tangents[i - 1].angle(tangents[i])
				normal = Matrix.Rotation(ang, 3, axis.normalized()) @ normal
		binormal = tangents[i].cross(normal)
		ring = []
		for s in range(segments):
			a = 2.0 * math.pi * s / segments
			ring.append(bm.verts.new(pts[i] + (normal * math.cos(a) + binormal * math.sin(a)) * radius))
		rings.append(ring)
	count = n if closed else n - 1
	for i in range(count):
		a, b = rings[i], rings[(i + 1) % n]
		for s in range(segments):
			bm.faces.new((a[s], a[(s + 1) % segments], b[(s + 1) % segments], b[s]))
	if cap and not closed:
		bm.faces.new(list(reversed(rings[0])))
		bm.faces.new(rings[-1])
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def bm_torus(major: float, minor: float, major_seg: int = 16, minor_seg: int = 8) -> bmesh.types.BMesh:
	"""Torus in the XY plane about Z."""
	bm = bmesh.new()
	rings = []
	for i in range(major_seg):
		a = 2.0 * math.pi * i / major_seg
		c = Vector((math.cos(a) * major, math.sin(a) * major, 0.0))
		d = Vector((math.cos(a), math.sin(a), 0.0))
		ring = []
		for j in range(minor_seg):
			b = 2.0 * math.pi * j / minor_seg
			ring.append(bm.verts.new(c + d * (math.cos(b) * minor) + Vector((0.0, 0.0, math.sin(b) * minor))))
		rings.append(ring)
	for i in range(major_seg):
		a, b = rings[i], rings[(i + 1) % major_seg]
		for j in range(minor_seg):
			bm.faces.new((a[j], b[j], b[(j + 1) % minor_seg], a[(j + 1) % minor_seg]))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def bm_extrude_polygon(poly2d, z0: float, z1: float) -> bmesh.types.BMesh:
	"""Prism from a counter-clockwise XY polygon between z0 and z1."""
	bm = bmesh.new()
	bottom = [bm.verts.new((x, y, z0)) for x, y in poly2d]
	top = [bm.verts.new((x, y, z1)) for x, y in poly2d]
	bm.faces.new(list(reversed(bottom)))
	bm.faces.new(top)
	n = len(poly2d)
	for i in range(n):
		j = (i + 1) % n
		bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	return bm


def orient_radial(bm: bmesh.types.BMesh, outward: bool = True, center=(0.0, 0.0)) -> bmesh.types.BMesh:
	"""Makes the faces of a lathe-like surface point away from (or toward) the Z axis through center. Near-horizontal faces (caps,
	domes, punts) use the direction from the surface's centroid instead, so a closed top faces up on an outer skin and down on an
	inner skin (the inner skin of a lamp shade must face the bulb: shadow depth passes cull back faces, a cap facing away lets the
	light through)."""
	flip = []
	n_verts = max(1, len(bm.verts))
	centroid = Vector((sum(v.co.x for v in bm.verts) / n_verts, sum(v.co.y for v in bm.verts) / n_verts,
		sum(v.co.z for v in bm.verts) / n_verts))
	for face in bm.faces:
		c = face.calc_center_median()
		radial = Vector((c.x - center[0], c.y - center[1], 0.0))
		if abs(face.normal.z) > 0.7 or radial.length <= 1e-5:
			d3 = c - centroid
			ref = d3.normalized() if d3.length > 1e-6 else Vector((0.0, 0.0, 1.0))
		else:
			ref = radial.normalized()
		d = face.normal.dot(ref)
		if abs(d) < 1e-4:
			continue
		if (d < 0.0) == outward:
			flip.append(face)
	if flip:
		bmesh.ops.reverse_faces(bm, faces=flip)
	return bm


def surface_patch(point_fn, u_range, v_range, nu: int, nv: int, label_rect, keep=None) -> bmesh.types.BMesh:
	"""A label / decal patch conforming to a surface: point_fn(u, v) -> Vector (already offset off the surface), u across (left to
	right as seen by the reader), v up. UV0 maps the (u, v) rectangle onto the atlas cell label_rect (Blender UV, bottom-left
	origin). keep(s, t) (s, t in 0..1) drops faces outside a shape (round logos)."""
	bm = bmesh.new()
	uv_layer = bm.loops.layers.uv.new("UV0")
	u0, u1 = u_range
	v0, v1 = v_range
	grid = []
	for j in range(nv + 1):
		row = []
		for i in range(nu + 1):
			row.append(bm.verts.new(point_fn(u0 + (u1 - u0) * i / nu, v0 + (v1 - v0) * j / nv)))
		grid.append(row)
	a0, b0, a1, b1 = label_rect
	for j in range(nv):
		for i in range(nu):
			s, t = (i + 0.5) / nu, (j + 0.5) / nv
			if keep is not None and not keep(s, t):
				continue
			face = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
			for loop, (ii, jj) in zip(face.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
				loop[uv_layer].uv = (a0 + (a1 - a0) * ii / nu, b0 + (b1 - b0) * jj / nv)
	bmesh.ops.delete(bm, geom=[vv for vv in bm.verts if not vv.link_faces], context="VERTS")
	return bm


def surface_disc(point_fn, radius: float, label_rect, rings: int = 8, segments: int = 48) -> bmesh.types.BMesh:
	"""A round label patch (polar grid, smooth rim) on a surface: point_fn(u, v) as in surface_patch, (u, v) in [-radius, radius];
	the label cell maps onto the disc's bounding square."""
	bm = bmesh.new()
	uv_layer = bm.loops.layers.uv.new("UV0")
	a0, b0, a1, b1 = label_rect

	def uv_of(u, v):
		return (a0 + (a1 - a0) * (u / radius * 0.5 + 0.5), b0 + (b1 - b0) * (v / radius * 0.5 + 0.5))

	centre = bm.verts.new(point_fn(0.0, 0.0))
	rings_v = []
	for r in range(1, rings + 1):
		rr = radius * r / rings
		rings_v.append([(bm.verts.new(point_fn(rr * math.cos(2 * math.pi * s / segments), rr * math.sin(2 * math.pi * s / segments))),
			rr * math.cos(2 * math.pi * s / segments), rr * math.sin(2 * math.pi * s / segments)) for s in range(segments)])
	for s in range(segments):
		s1 = (s + 1) % segments
		(va, ua, wa), (vb, ub, wb) = rings_v[0][s], rings_v[0][s1]
		face = bm.faces.new((centre, va, vb))
		for loop, uvp in zip(face.loops, ((0.0, 0.0), (ua, wa), (ub, wb))):
			loop[uv_layer].uv = uv_of(*uvp)
	for r in range(rings - 1):
		for s in range(segments):
			s1 = (s + 1) % segments
			quad = (rings_v[r][s], rings_v[r + 1][s], rings_v[r + 1][s1], rings_v[r][s1])
			face = bm.faces.new([q[0] for q in quad])
			for loop, q in zip(face.loops, quad):
				loop[uv_layer].uv = uv_of(q[1], q[2])
	return bm


def bevel(bm: bmesh.types.BMesh, width: float, segments: int = 2, angle_deg: float = 30.0, profile: float = 0.5) -> bmesh.types.BMesh:
	"""Bevels every edge whose faces meet at more than angle_deg (9.2: no knife edges)."""
	if width <= 0.0:
		return bm
	edges = [e for e in bm.edges if len(e.link_faces) == 2 and e.calc_face_angle(0.0) > math.radians(angle_deg)]
	if edges:
		bmesh.ops.bevel(bm, geom=edges, offset=width, offset_type="OFFSET", segments=segments, profile=profile, affect="EDGES",
			clamp_overlap=True)
	return bm


def thin_shell(bm: bmesh.types.BMesh, thickness: float) -> bmesh.types.BMesh:
	"""Gives an open sheet (tape, labels) a thickness along its vertex normals (negative = behind the front face): a back copy with
	reversed winding plus side quads on the open border. Deterministic replacement for bmesh.ops.solidify, whose output order
	depends on pointer hashing (different geometry hashes on every run)."""
	bm.verts.index_update()
	bm.edges.index_update()
	bm.faces.index_update()
	bm.normal_update()
	front = list(bm.verts)
	faces = list(bm.faces)
	border = []
	for edge in bm.edges:
		if len(edge.link_faces) == 1:
			loop = next(lp for lp in edge.link_faces[0].loops if lp.edge == edge)
			border.append((loop.vert.index, loop.link_loop_next.vert.index))
	back = [bm.verts.new(v.co + v.normal * thickness) for v in front]
	for face in faces:
		bm.faces.new([back[v.index] for v in reversed(face.verts)])
	for ia, ib in border:
		bm.faces.new((front[ib], front[ia], back[ia], back[ib]))
	bm.verts.index_update()
	bm.faces.index_update()
	bm.normal_update()
	return bm


def transform(bm: bmesh.types.BMesh, matrix: Matrix) -> bmesh.types.BMesh:
	bmesh.ops.transform(bm, matrix=matrix, verts=bm.verts)
	return bm


def translate(bm: bmesh.types.BMesh, offset) -> bmesh.types.BMesh:
	bmesh.ops.translate(bm, vec=Vector(offset), verts=bm.verts)
	return bm


def rotate(bm: bmesh.types.BMesh, angle_deg: float, axis: str = "Z", pivot=(0.0, 0.0, 0.0)) -> bmesh.types.BMesh:
	bmesh.ops.rotate(bm, cent=Vector(pivot), matrix=Matrix.Rotation(math.radians(angle_deg), 3, axis), verts=bm.verts)
	return bm


def deform(bm: bmesh.types.BMesh, fn) -> bmesh.types.BMesh:
	"""Applies fn(Vector) -> Vector to every vertex (sag, bow, jitter)."""
	for vert in bm.verts:
		vert.co = fn(vert.co.copy())
	return bm


# --------------------------------------------------------------------------------------------------------------------
# UV projection (UV0 world scale) on the part's bmesh
# --------------------------------------------------------------------------------------------------------------------


def _uv_box(face, co, grain: str):
	n = face.normal
	ax = max(range(3), key=lambda i: abs(n[i]))
	x, y, z = co
	if ax == 2:      # horizontal face
		return (x, y) if grain != "y" else (y, x)
	if ax == 0:      # faces +-X: plane YZ
		return (y, z) if grain != "z" else (z, y)
	return (x, z) if grain != "z" else (z, x)   # faces +-Y: plane XZ


def project_uv0(bm: bmesh.types.BMesh, mode: str, grain: str = "x", scale: float = 1.0, label_rect=None) -> None:
	uv = bm.loops.layers.uv.get("UV0") or bm.loops.layers.uv.new("UV0")
	if mode == "none":
		return
	if mode == "cyl":
		for face in bm.faces:
			# cylindrical about the local Z axis: U = arc length at the mean radius of the face, V = z (world scale)
			c = face.calc_center_median()
			r = max(1e-4, math.hypot(c.x, c.y))
			if abs(face.normal.z) > 0.8:
				for loop in face.loops:
					loop[uv].uv = (loop.vert.co.x * scale, loop.vert.co.y * scale)
				continue
			ref = math.atan2(c.y, c.x)
			for loop in face.loops:
				a = math.atan2(loop.vert.co.y, loop.vert.co.x)
				da = (a - ref + math.pi) % (2.0 * math.pi) - math.pi
				loop[uv].uv = ((ref + da) * r * scale, loop.vert.co.z * scale)
		return
	if mode == "label":
		u0, v0, u1, v1 = label_rect
		zs = [vert.co.z for vert in bm.verts]
		zmin, zmax = min(zs), max(zs)
		for face in bm.faces:
			c = face.calc_center_median()
			ref = math.atan2(c.y, c.x)
			for loop in face.loops:
				a = math.atan2(loop.vert.co.y, loop.vert.co.x)
				da = (a - ref + math.pi) % (2.0 * math.pi) - math.pi
				t = ((ref + da) / (2.0 * math.pi)) % 1.0 if abs(face.normal.z) < 0.8 else 0.5
				s = (loop.vert.co.z - zmin) / max(1e-6, zmax - zmin)
				loop[uv].uv = (u0 + (u1 - u0) * t, v0 + (v1 - v0) * s)
		return
	if mode == "planar_label":
		# flat label facing +X (front): U along -Y (left to right as seen from the front), V along Z
		u0, v0, u1, v1 = label_rect
		ys = [vert.co.y for vert in bm.verts]
		zs = [vert.co.z for vert in bm.verts]
		ymin, ymax, zmin, zmax = min(ys), max(ys), min(zs), max(zs)
		for face in bm.faces:
			for loop in face.loops:
				t = (ymax - loop.vert.co.y) / max(1e-6, ymax - ymin)
				s = (loop.vert.co.z - zmin) / max(1e-6, zmax - zmin)
				loop[uv].uv = (u0 + (u1 - u0) * t, v0 + (v1 - v0) * s)
		return
	for face in bm.faces:
		for loop in face.loops:
			a, b = _uv_box(face, loop.vert.co, grain)
			loop[uv].uv = (a * scale, b * scale)


# --------------------------------------------------------------------------------------------------------------------
# the asset
# --------------------------------------------------------------------------------------------------------------------


class Asset:
	"""Collects parts, joins them into SM_DB_<AssetId>, builds UV1, hulls, bakes the wear mask and exports FBX + JSON."""

	def __init__(self, asset_id: str, family: str, spec_id: str, priority: str = "hero"):
		self.asset_id = asset_id
		self.family = family
		self.spec_id = spec_id
		self.priority = priority
		self.parts: list[Part] = []
		self.hulls: list[bmesh.types.BMesh] = []
		self.anchors: dict[str, list[float]] = {}
		self.instances: list[dict] = []
		self.touch_regions = []
		self.obj = None

	# -- building ------------------------------------------------------------------------------------------------------

	def add(self, bm: bmesh.types.BMesh, mat: str, **kw) -> Part:
		part = Part(bm, mat, **kw)
		self.parts.append(part)
		return part

	def hull_box(self, size, center) -> None:
		self.hulls.append(bm_box(size, center))

	def hull_cylinder(self, radius: float, z0: float, z1: float, center_xy=(0.0, 0.0), segments: int = 12) -> None:
		self.hulls.append(bm_cylinder(radius, z1 - z0, segments, (center_xy[0], center_xy[1], 0.5 * (z0 + z1))))

	def hull_points(self, points, floor_clamp: bool = True) -> None:
		bm = bmesh.new()
		# hulls never reach below the floor (a ball must not be lifted by a leg hull); hanging assets (pivot on the ceiling plane,
		# every z negative) pass floor_clamp=False, or their hulls would collapse onto the pivot plane
		verts = [bm.verts.new((p[0], p[1], max(0.0, p[2]) if floor_clamp else p[2])) for p in points]
		bmesh.ops.convex_hull(bm, input=verts)
		self.hulls.append(bm)

	def hull_segment(self, a, b, radius: float, segments: int = 8) -> None:
		"""Convex hull around a straight rod from a to b (legs, posts)."""
		a, b = Vector(a), Vector(b)
		axis = (b - a).normalized()
		ref = Vector((0.0, 0.0, 1.0)) if abs(axis.z) < 0.9 else Vector((1.0, 0.0, 0.0))
		n1 = axis.cross(ref).normalized()
		n2 = axis.cross(n1)
		pts = []
		for end in (a, b):
			for s in range(segments):
				ang = 2.0 * math.pi * s / segments
				pts.append(end + (n1 * math.cos(ang) + n2 * math.sin(ang)) * radius)
		self.hull_points(pts)

	def anchor(self, name: str, p) -> None:
		self.anchors[name] = ue(p)

	# -- assembly ------------------------------------------------------------------------------------------------------

	def _part_object(self, index: int, part: Part):
		project_uv0(part.bm, part.uv, part.grain, part.uv_scale, part.label_rect)
		me = bpy.data.meshes.new(f"{self.asset_id}_part{index:03d}")
		part.bm.to_mesh(me)
		part.bm.free()
		obj = bpy.data.objects.new(me.name, me)
		_link(obj)
		me.materials.append(material(part.mat))
		attr = me.attributes.new("rb_touch", "FLOAT", "POINT")
		if callable(part.touch):     # per-vertex touch / desilver mask, e.g. a mirror that desilvers from its edges
			attr.data.foreach_set("value", [float(part.touch(vv.co)) for vv in me.vertices])
		else:
			attr.data.foreach_set("value", [float(part.touch)] * len(me.vertices))
		me.shade_smooth()
		if hasattr(me, "set_sharp_from_angle"):
			me.set_sharp_from_angle(angle=math.radians(part.smooth_angle))
		return obj

	def build(self):
		if not self.parts:
			rb_bl.fail(f"{self.asset_id}: no parts")
		objects = [self._part_object(i, p) for i, p in enumerate(self.parts)]
		bpy.ops.object.select_all(action="DESELECT")
		for obj in objects:
			obj.select_set(True)
		bpy.context.view_layer.objects.active = objects[0]
		if len(objects) > 1:
			bpy.ops.object.join()
		obj = bpy.context.view_layer.objects.active
		obj.name = f"SM_DB_{self.asset_id}"
		obj.data.name = obj.name
		me = obj.data
		# UV1: unique unwrap (smart project + pack) - the wear-mask channel.
		if "UV1" not in me.uv_layers:
			me.uv_layers.new(name="UV1")
		me.uv_layers.active = me.uv_layers["UV1"]
		bpy.ops.object.mode_set(mode="EDIT")
		bpy.ops.mesh.select_all(action="SELECT")
		bpy.ops.uv.smart_project(angle_limit=math.radians(60.0), island_margin=0.003, area_weight=0.0, correct_aspect=True,
			scale_to_bounds=False)
		bpy.ops.uv.pack_islands(rotate=True, margin=0.004)
		bpy.ops.object.mode_set(mode="OBJECT")
		me.uv_layers.active = me.uv_layers["UV0"]
		me.uv_layers["UV0"].active_render = True
		# Normalised height above the floor per asset (wear-mask alpha) and a hashed per-vertex value for the preview.
		zs = [vv.co.z for vv in me.vertices]
		zmin, zmax = min(zs), max(zs)
		hattr = me.attributes.new("rb_height", "FLOAT", "POINT")
		hattr.data.foreach_set("value", [(z - zmin) / max(1e-6, zmax - zmin) for z in zs])
		self.obj = obj
		return obj

	def hull_objects(self):
		out = []
		for i, bm in enumerate(self.hulls):
			me = bpy.data.meshes.new(f"UCX_SM_DB_{self.asset_id}_{i:02d}")
			bm.to_mesh(me)
			bm.free()
			obj = bpy.data.objects.new(me.name, me)
			_link(obj)
			out.append(obj)
		return out

	# -- checks --------------------------------------------------------------------------------------------------------

	def check_slice(self, zmin: float, zmax: float, target_xy, tol: float, what: str) -> dict:
		"""Asserts the X / Y extent of the vertices between zmin and zmax (a spec dimension that is not the bounding box, e.g. the
		seat diameter of a stool whose foot ring is wider). Returns the measured values for the metadata."""
		xs, ys = [], []
		for vert in self.obj.data.vertices:
			co = self.obj.matrix_world @ vert.co
			if zmin <= co.z <= zmax:
				xs.append(co.x)
				ys.append(co.y)
		if not xs:
			rb_bl.fail(f"{self.asset_id}: no vertices between z {zmin} and {zmax} ({what})")
		size = (max(xs) - min(xs), max(ys) - min(ys))
		for axis, have, want in zip("XY", size, target_xy):
			if want is not None and abs(have - want) > tol:
				rb_bl.fail(f"{self.asset_id}: {what} {axis} {have:.4f} m, spec {want:.4f} m (+-{tol})")
		return {"what": what, "z_range_m": [zmin, zmax], "measured_m": [round(size[0], 5), round(size[1], 5)],
			"spec_m": list(target_xy), "tolerance_m": tol}

	def slots(self) -> list[str]:
		return [m.name for m in self.obj.data.materials]

	def bounds(self):
		return rb_bl.world_bounds([self.obj])

	# -- export --------------------------------------------------------------------------------------------------------

	def export(self, out_root: str | Path, target_m, meta: dict | None = None, tolerance: float | None = None, wm_res: int = 1024,
			bake: bool = True, collision_profile: str = "RbVenueBlock", acoustic: str = "", pivot: str = "floor_center",
			translucent: bool = False, ray_tracing: bool = True, cast_shadow: bool = True, external_inputs=None) -> Path:
		obj = self.obj or self.build()
		tol = tolerance if tolerance is not None else (HERO_TOL if self.priority == "hero" else MID_TOL)
		rb_bl.assert_dimensions([obj], tuple(target_m), tol)
		# the wear mask is baked BEFORE the UCX hulls exist: the AO bake sees every object in the scene, and a hull enclosing a
		# surface (a sign's face, a flyer, a stool leg) would bake it fully occluded (AO 0 -> maximum grime, a black sign face)
		wm_rel = ""
		wm_path = TEXTURE_ROOT / self.asset_id / f"T_DB_{self.asset_id}_WM.png"
		if bake and not want_bake() and wm_path.exists():
			wm_rel = wm_path.relative_to(REPO).as_posix()   # --no-bake: keep the previous bake
		elif bake:
			bake_wear_mask(obj, wm_path, wm_res)
			wm_rel = wm_path.relative_to(REPO).as_posix()
		hulls = self.hull_objects()
		lo, hi = rb_bl.world_bounds([obj])
		for h in hulls:
			hlo, hhi = rb_bl.world_bounds([h])
			for axis in range(3):
				if hlo[axis] < lo[axis] - 0.02 or hhi[axis] > hi[axis] + 0.02:
					rb_bl.fail(f"{self.asset_id}: hull {h.name} leaves the mesh bounds on {'XYZ'[axis]} ({hlo[axis]:.3f}..{hhi[axis]:.3f})")
		data = {
			"family": self.family,
			"spec_id": self.spec_id,
			"priority": self.priority,
			"owner": "M2-B",
			"ue_dir": f"{UE_PROPS}/{self.family}",
			"ue_mesh": f"{UE_PROPS}/{self.family}/SM_DB_{self.asset_id}",
			"axis_convention": "blender(x, y, z) -> unreal(x, -y, z); *_ue_m fields are already in Unreal axes",
			"front": "+X",
			"pivot": pivot,
			"target_dimensions_m": [round(t, 5) for t in target_m],
			"tolerance_m": tol,
			"material_slots": self.slots(),
			"collision_profile": collision_profile,
			"collision_hulls": len(hulls),
			"acoustic_material": acoustic,
			"nanite": not translucent,
			"translucent": translucent,
			"ray_tracing": ray_tracing,
			"cast_shadow": cast_shadow,
			"wear_mask": wm_rel,
			"uv_channels": {"0": "world scale (1 UV = 1 m) / label atlas cells", "1": "unique (wear mask)"},
			"anchors_ue_m": self.anchors,
			"prop_instances": self.instances,
			"external_inputs": external_inputs or [],
			"ledger_asset_id": f"SM_DB_{self.asset_id}",
		}
		if meta:
			data.update(meta)
		out = Path(out_root)
		path = rb_bl.export_asset(self.asset_id, [obj] + hulls, out, data)
		# rb_bl counts the hull triangles too: record the render mesh alone as well.
		js = json.loads(path.read_text(encoding="utf-8"))
		js["bounds_with_hulls_min_m"], js["bounds_with_hulls_max_m"] = js["bounds_min_m"], js["bounds_max_m"]
		js["bounds_min_m"] = [round(c, 5) for c in lo]
		js["bounds_max_m"] = [round(c, 5) for c in hi]
		js["triangles_render"] = rb_bl.triangle_count([obj])
		js["geometry_sha256"] = geometry_hash(obj)
		path.write_text(json.dumps(js, indent=1, sort_keys=True), encoding="utf-8")
		ledger_own(f"SM_DB_{self.asset_id}", self.spec_id)
		return path


def geometry_hash(obj) -> str:
	"""SHA-256 of the rounded vertex positions and face indices (determinism check: equal seeds -> equal hashes)."""
	h = hashlib.sha256()
	me = obj.data
	for vert in me.vertices:
		h.update(struct.pack("<3i", *(int(round(c * 1e5)) for c in vert.co)))
	for poly in me.polygons:
		h.update(struct.pack(f"<{len(poly.vertices)}I", *poly.vertices))
	return h.hexdigest()


# --------------------------------------------------------------------------------------------------------------------
# licence ledger (own generated assets; CC0 rows come from Tools/art/fetch_cc0.py, fonts from Tools/art/text_textures.py)
# --------------------------------------------------------------------------------------------------------------------


def ledger_rows() -> list[dict]:
	if not LEDGER.exists():
		return []
	with LEDGER.open(newline="", encoding="utf-8") as handle:
		return list(csv.DictReader(handle))


def ledger_append(row: dict) -> None:
	existing = {(r["asset_id"], r["source"]) for r in ledger_rows()}
	if (row["asset_id"], row["source"]) in existing:
		return
	LEDGER.parent.mkdir(parents=True, exist_ok=True)
	new_file = not LEDGER.exists()
	with LEDGER.open("a", newline="", encoding="utf-8") as handle:
		writer = csv.DictWriter(handle, fieldnames=LEDGER_COLUMNS)
		if new_file:
			writer.writeheader()
		writer.writerow({k: row.get(k, "") for k in LEDGER_COLUMNS})


def ledger_own(asset_id: str, spec_id: str) -> None:
	generator = Path(sys.argv[sys.argv.index("--python") + 1]).name if "--python" in sys.argv else "db_*.py"
	ledger_append({"asset_id": asset_id, "used_by": "M2-B", "source": "own", "source_ref": f"Tools/blender/divebar/{generator}",
		"author": "RAW BREAK (procedural generator)", "licence": "own", "licence_url": "", "date": LEDGER_DATE, "account": "",
		"sha256": "", "modified": "n", "ai_generated": "n", "steam_ai_disclosure": "n", "trademark_check": "n/a",
		"notes": f"venue-dive-bar {spec_id}; regenerated by Tools/blender/rbbl.py"})


# --------------------------------------------------------------------------------------------------------------------
# wear-mask bake (Cycles, GPU when available)
# --------------------------------------------------------------------------------------------------------------------


def _setup_cycles(samples: int) -> None:
	scene = bpy.context.scene
	scene.render.engine = "CYCLES"
	scene.cycles.samples = samples
	scene.cycles.seed = 0
	scene.cycles.use_denoising = False
	try:
		prefs = bpy.context.preferences.addons["cycles"].preferences
		prefs.compute_device_type = "OPTIX"
		prefs.get_devices()
		for device in prefs.devices:
			device.use = device.type == "OPTIX"
		scene.cycles.device = "GPU" if any(d.use for d in prefs.devices) else "CPU"
	except Exception:  # noqa: BLE001 - no GPU: CPU bake
		scene.cycles.device = "CPU"
	if scene.world is None:
		scene.world = bpy.data.worlds.new("rb_bake_world")
	scene.world.light_settings.distance = 0.15


def _bake_material(image) -> bpy.types.Material:
	mat = bpy.data.materials.new("rb_wm_bake")
	mat.use_nodes = True
	nt = mat.node_tree
	for node in list(nt.nodes):
		nt.nodes.remove(node)
	out = nt.nodes.new("ShaderNodeOutputMaterial")
	emit = nt.nodes.new("ShaderNodeEmission")
	combine = nt.nodes.new("ShaderNodeCombineColor")
	ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
	ao.inside = True
	ao.only_local = True
	ao.samples = 16
	ao.inputs["Distance"].default_value = 0.010
	invert = nt.nodes.new("ShaderNodeMath")
	invert.operation = "SUBTRACT"
	invert.inputs[0].default_value = 1.0
	nt.links.new(ao.outputs["AO"], invert.inputs[1])
	boost = nt.nodes.new("ShaderNodeMath")
	boost.operation = "MULTIPLY"
	boost.use_clamp = True
	boost.inputs[1].default_value = 2.2
	nt.links.new(invert.outputs[0], boost.inputs[0])
	touch = nt.nodes.new("ShaderNodeAttribute")
	touch.attribute_name = "rb_touch"
	height = nt.nodes.new("ShaderNodeAttribute")
	height.attribute_name = "rb_height"
	nt.links.new(boost.outputs[0], combine.inputs[0])
	nt.links.new(touch.outputs["Fac"], combine.inputs[1])
	nt.links.new(height.outputs["Fac"], combine.inputs[2])
	nt.links.new(combine.outputs[0], emit.inputs["Color"])
	nt.links.new(emit.outputs[0], out.inputs["Surface"])
	tex = nt.nodes.new("ShaderNodeTexImage")
	tex.image = image
	nt.nodes.active = tex
	return mat


def bake_wear_mask(obj, png_path: Path, res: int) -> None:
	"""T_DB_<Asset>_WM on UV1: R convexity, G AO, B touch, A height (venue-dive-bar 6.2)."""
	import numpy as np

	_setup_cycles(32)
	img_em = bpy.data.images.new(f"{obj.name}_wm_em", res, res, alpha=False, float_buffer=True, is_data=True)
	img_ao = bpy.data.images.new(f"{obj.name}_wm_ao", res, res, alpha=False, float_buffer=True, is_data=True)
	mat = _bake_material(img_em)
	saved = [slot.material for slot in obj.material_slots]
	for slot in obj.material_slots:
		slot.material = mat
	me = obj.data
	me.uv_layers.active = me.uv_layers["UV1"]
	bpy.ops.object.select_all(action="DESELECT")
	obj.select_set(True)
	bpy.context.view_layer.objects.active = obj
	bpy.ops.object.bake(type="EMIT", margin=6, margin_type="EXTEND", use_clear=True)
	mat.node_tree.nodes.active.image = img_ao
	bpy.context.scene.cycles.samples = 64
	bpy.ops.object.bake(type="AO", margin=6, margin_type="EXTEND", use_clear=True)
	for slot, m in zip(obj.material_slots, saved):
		slot.material = m
	me.uv_layers.active = me.uv_layers["UV0"]
	em = np.empty(res * res * 4, dtype=np.float32)
	ao = np.empty(res * res * 4, dtype=np.float32)
	img_em.pixels.foreach_get(em)
	img_ao.pixels.foreach_get(ao)
	em = em.reshape(res, res, 4)
	ao = ao.reshape(res, res, 4)
	rgba = np.stack([em[..., 0], ao[..., 0], em[..., 1], em[..., 2]], axis=-1)
	rgba = np.clip(rgba, 0.0, 1.0)[::-1]            # Blender rows start at the bottom; PNG at the top
	write_png(png_path, (rgba * 255.0 + 0.5).astype(np.uint8))
	bpy.data.images.remove(img_em)
	bpy.data.images.remove(img_ao)
	bpy.data.materials.remove(mat)
	rb_bl.log(f"baked {png_path.relative_to(REPO).as_posix()} ({res} px)")


def write_png(path: Path, pixels) -> None:
	"""8-bit PNG from a uint8 numpy array (H, W) / (H, W, 3) / (H, W, 4), rows top to bottom (stdlib zlib, no PIL in Blender)."""
	import numpy as np

	arr = np.ascontiguousarray(pixels)
	h, w = arr.shape[:2]
	channels = 1 if arr.ndim == 2 else arr.shape[2]
	color_type = {1: 0, 3: 2, 4: 6}[channels]
	raw = np.zeros((h, w * channels + 1), dtype=np.uint8)
	raw[:, 1:] = arr.reshape(h, w * channels)

	def chunk(tag: bytes, data: bytes) -> bytes:
		return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

	png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0))
	png += chunk(b"IDAT", zlib.compress(raw.tobytes(), 6)) + chunk(b"IEND", b"")
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(png)


# --------------------------------------------------------------------------------------------------------------------
# previews (shape check only: Cycles, simple studio light; Saved/RbPreview is git-ignored)
# --------------------------------------------------------------------------------------------------------------------


def preview(obj, name: str, res: int = 720, view: tuple = (1.0, -1.2, 0.7), extra=None, floor: bool = True, zoom: float = 1.6,
		target=None) -> Path:
	scene = bpy.context.scene
	for other in bpy.data.objects:
		other.hide_render = other is not obj and other not in (extra or [])
	_setup_cycles(48)
	scene.cycles.use_denoising = True
	scene.render.resolution_x = res
	scene.render.resolution_y = res
	scene.render.film_transparent = False
	lo, hi = rb_bl.world_bounds([obj])
	center = (lo + hi) * 0.5 if target is None else Vector(target)
	size = (hi - lo).length
	cam_data = bpy.data.cameras.new("rb_preview_cam")
	cam_data.lens = 50.0
	cam_data.clip_start = max(0.0005, size * 0.01)
	cam = bpy.data.objects.new("rb_preview_cam", cam_data)
	_link(cam)
	d = Vector(view).normalized()
	cam.location = center + d * size * zoom
	cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
	scene.camera = cam
	lights = []
	for loc, energy in (((3, -3, 4), 900.0), ((-4, -2, 2), 300.0), ((0, 4, 3), 400.0)):
		ld = bpy.data.lights.new("rb_preview_light", "AREA")
		ld.energy = energy * max(1.0, size) ** 2
		ld.size = 2.0
		lo_obj = bpy.data.objects.new("rb_preview_light", ld)
		lo_obj.location = center + Vector(loc) * max(1.0, size)
		lo_obj.rotation_euler = (center - lo_obj.location).to_track_quat("-Z", "Y").to_euler()
		_link(lo_obj)
		lights.append(lo_obj)
	floor_me = bpy.data.meshes.new("rb_preview_floor")
	bm = bm_box((40.0, 40.0, 0.01), (center.x, center.y, (lo.z if floor else lo.z - 3.0) - 0.005))
	bm.to_mesh(floor_me)
	bm.free()
	floor = bpy.data.objects.new("rb_preview_floor", floor_me)
	floor_me.materials.append(material("MI_Preview_Floor"))
	_link(floor)
	if scene.world is None:
		scene.world = bpy.data.worlds.new("rb_world")
	scene.world.use_nodes = True
	bg = scene.world.node_tree.nodes.get("Background")
	if bg is not None:
		bg.inputs[0].default_value = (0.05, 0.05, 0.055, 1.0)
		bg.inputs[1].default_value = 1.0
	out = REPO / "Saved" / "RbPreview" / f"{name}.png"
	out.parent.mkdir(parents=True, exist_ok=True)
	scene.render.filepath = str(out)
	scene.render.image_settings.file_format = "PNG"
	bpy.ops.render.render(write_still=True)
	for o in [cam, floor] + lights:
		bpy.data.objects.remove(o, do_unlink=True)
	rb_bl.log(f"preview {out}")
	return out


def args():
	"""rb_bl.args() + --preview (Cycles shape previews to Saved/RbPreview) + --no-bake (skip the wear-mask bakes)."""
	import argparse

	parser = argparse.ArgumentParser()
	parser.add_argument("--preview", action="store_true")
	parser.add_argument("--no-bake", action="store_true")
	return rb_bl.args(parser)


def want_preview() -> bool:
	return "--preview" in sys.argv


def want_bake() -> bool:
	return "--no-bake" not in sys.argv


_LABELS = None


def label(name: str) -> dict:
	"""A cell of the text-texture atlas (Tools/art/text_textures.py -> Art/DiveBar/Textures/Labels/labels.json): uv_blender
	(u0, v0, u1, v1, bottom-left origin) and size_m (the physical size the label is designed for)."""
	global _LABELS
	if _LABELS is None:
		path = TEXTURE_ROOT / "Labels" / "labels.json"
		if not path.exists():
			rb_bl.fail(f"{path} missing (python Tools/art/text_textures.py)")
		_LABELS = json.loads(path.read_text(encoding="utf-8"))["labels"]
	if name not in _LABELS:
		rb_bl.fail(f"label {name} not in labels.json")
	return _LABELS[name]


def label_rect(name: str, inset_px: float = 2.0) -> tuple:
	"""uv_blender of a label, shrunk by a few texels so mip-mapping never bleeds the neighbour cell in."""
	u0, v0, u1, v1 = label(name)["uv_blender"]
	d = inset_px / 4096.0
	return (u0 + d, v0 + d, u1 - d, v1 - d)


def selected(only: list[str], asset_id: str) -> bool:
	return not only or asset_id in only


def props_out(args) -> Path:
	"""M2-B's export root: Art/DiveBar/Export/Props (the importer scans <root>/**/<Asset>/<Asset>.json)."""
	return Path(args.out) / "Props"


def clear_scene_keep_materials() -> None:
	"""Removes the objects of the previous asset (one Blender process builds many assets)."""
	for obj in list(bpy.data.objects):
		bpy.data.objects.remove(obj, do_unlink=True)
	for me in list(bpy.data.meshes):
		if me.users == 0:
			bpy.data.meshes.remove(me)
