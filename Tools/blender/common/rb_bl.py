"""Common library of the RAW BREAK Blender generators (runs INSIDE Blender 5.2; venue-dive-bar 13.1-13.5,
Docs/ue-architecture.md 18.8). Import from a generator with:

	import sys, pathlib
	sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "common"))
	import rb_bl

Conventions (venue-dive-bar 13.3-13.5): metric, 1 BU = 1 m, **Blender scene coordinates = venue frame V = UE world axes** (+X into
the room, +Y right, +Z up); the exporter compensates the handedness flip of the FBX import (UE imports Blender +Y as -Y; the DB-0
axis test SM_DB_AxisTest proves the compensation: its +Y marker must land on UE +Y). Pivots at the floor-contact centre (floor
items), wall plane bottom centre (wall items), ceiling anchor (hanging items); architecture is exported in V coordinates with the
pivot at the V origin (placed at the origin). Seeded determinism (rng(asset_id, instance), no wall clock, sorted iteration); every
export writes Art/<Venue>/Export/<Asset>/SM_DB_<Asset>.fbx + <Asset>.json (one metadata file per asset: bounds, triangle counts,
target dimensions, material slots, collision profile, acoustic material, external inputs for the licence check) and the asset's
own licence-ledger row (source "own") into the package fragment Docs/licenses/ledger/<package>.csv. The importer
(Tools/unreal/editor/rb_import_divebar.py) refuses an asset without a ledger row and re-checks the bounds (VDB-T4).

Geometry helpers build meshes from boxes / cylinders / extruded polygons with bmesh (no operators, background-safe); UV0 is a
world-scale box projection (1 UV unit = 1 m, venue-dive-bar 13.5), UV1 a unique smart-projected unwrap; collision hulls are UCX_
objects named after the render mesh (boxes, 12-sided cylinders). Owner: M2-A.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import sys
from pathlib import Path

import bmesh
import bpy  # noqa: F401 - Blender only
from mathutils import Matrix, Vector

REPO = Path(__file__).resolve().parents[3]
LEDGER_COLUMNS = ["asset_id", "used_by", "source", "source_ref", "author", "licence", "licence_url", "date", "account", "sha256",
	"modified", "ai_generated", "steam_ai_disclosure", "trademark_check", "notes"]
# Frozen by the DB-0 axis test (RawBreak.Unit.Venue.AxisTest): UE's FBX import maps Blender +Y to UE -Y, so every export
# mirrors Y once (vertices y -> -y, winding flipped) and the venue frame survives the round trip unchanged.
MIRROR_Y_ON_EXPORT = True


def log(msg: str) -> None:
	print(f"[rb] {msg}", flush=True)


def fail(msg: str) -> None:
	"""Marks the run as failed for rbbl.py and stops the script."""
	print(f"RBBL_FAIL {msg}", flush=True)
	raise SystemExit(1)


def args(extra: argparse.ArgumentParser | None = None) -> argparse.Namespace:
	"""The generator's arguments after Blender's '--': --seed, --out (export root), --only (asset ids) + the extra parser's."""
	parser = extra or argparse.ArgumentParser()
	parser.add_argument("--seed", type=int, default=1958)
	parser.add_argument("--out", default=str(REPO / "Art" / "DiveBar" / "Export"))
	parser.add_argument("--only", nargs="*", default=[])
	argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
	known, _ = parser.parse_known_args(argv)
	return known


def rng(asset_id: str, instance: int = 0, seed: int = 0) -> random.Random:
	"""Deterministic per asset / instance (venue-dive-bar 9.2 "the 3 mm rule")."""
	digest = hashlib.sha256(f"{asset_id}|{instance}|{seed}".encode("utf-8")).digest()
	return random.Random(int.from_bytes(digest[:8], "little"))


def reset_scene() -> None:
	"""Empty scene, metric units at scale 1.0 (1 BU = 1 m)."""
	bpy.ops.wm.read_factory_settings(use_empty=True)
	scene = bpy.context.scene
	scene.unit_settings.system = "METRIC"
	scene.unit_settings.scale_length = 1.0


def load_json(relative: str) -> dict:
	path = REPO / relative
	if not path.exists():
		fail(f"missing {relative}")
	return json.loads(path.read_text(encoding="utf-8"))


# ------------------------------------------------------------------------------------------------------------------------------
# Materials (slot names ARE the Unreal material instance names: the importer assigns MI_DB_<slot> without a mapping table)
# ------------------------------------------------------------------------------------------------------------------------------


def material(name: str) -> bpy.types.Material:
	mat = bpy.data.materials.get(name)
	if mat is None:
		mat = bpy.data.materials.new(name)
	return mat


def slot_index(obj, name: str) -> int:
	"""Index of the material slot `name` on obj (added when missing)."""
	mat = material(name)
	for i, slot in enumerate(obj.data.materials):
		if slot is not None and slot.name == name:
			return i
	obj.data.materials.append(mat)
	return len(obj.data.materials) - 1


# ------------------------------------------------------------------------------------------------------------------------------
# Mesh building (bmesh; one Builder collects many primitives into one object)
# ------------------------------------------------------------------------------------------------------------------------------


class Builder:
	"""Accumulates primitives (boxes, cylinders, prisms, raw polygons) with material slots into one mesh object."""

	def __init__(self, name: str):
		self.name = name
		self.bm = bmesh.new()
		self.slots: list[str] = []

	def _slot(self, mat: str) -> int:
		if mat not in self.slots:
			self.slots.append(mat)
		return self.slots.index(mat)

	def poly(self, points, mat: str) -> None:
		"""One planar polygon (counter-clockwise seen from the side its normal points to)."""
		verts = [self.bm.verts.new(Vector(p)) for p in points]
		face = self.bm.faces.new(verts)
		face.material_index = self._slot(mat)

	def box(self, x0, x1, y0, y1, z0, z1, mat: str, skip: tuple = ()) -> None:
		"""Axis-aligned box; skip = faces to omit ('-x', '+x', '-y', '+y', '-z', '+z')."""
		if x1 < x0:
			x0, x1 = x1, x0
		if y1 < y0:
			y0, y1 = y1, y0
		if z1 < z0:
			z0, z1 = z1, z0
		if x1 - x0 < 1e-6 or y1 - y0 < 1e-6 or z1 - z0 < 1e-6:
			return
		c = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
		faces = {"-z": (0, 3, 2, 1), "+z": (4, 5, 6, 7), "-y": (0, 1, 5, 4), "+y": (2, 3, 7, 6), "-x": (3, 0, 4, 7), "+x": (1, 2, 6, 5)}
		for key, idx in faces.items():
			if key not in skip:
				self.poly([c[i] for i in idx], mat)

	def oriented_box(self, center, size, rotation: Matrix, mat: str) -> None:
		"""Box of size (sx, sy, sz) centred at center, rotated by the 3x3 matrix rotation."""
		hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
		corners = [Vector((sx * hx, sy * hy, sz * hz)) for sz in (-1, 1) for sy in (-1, 1) for sx in (-1, 1)]
		pts = [Vector(center) + rotation @ c for c in corners]
		# corner order: (-,-,-), (+,-,-), (-,+,-), (+,+,-), (-,-,+), (+,-,+), (-,+,+), (+,+,+)
		quads = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (3, 2, 6, 7), (2, 0, 4, 6), (1, 3, 7, 5)]
		for q in quads:
			self.poly([pts[i] for i in q], mat)

	def cylinder(self, center, radius: float, length: float, mat: str, axis: str = "z", segments: int = 24, caps: bool = True,
			radius_top: float | None = None) -> None:
		"""Cylinder (or cone frustum with radius_top) centred at center along axis x / y / z."""
		rt = radius if radius_top is None else radius_top
		c = Vector(center)
		if axis == "z":
			basis = (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
		elif axis == "x":
			basis = (Vector((0, 1, 0)), Vector((0, 0, 1)), Vector((1, 0, 0)))
		else:
			basis = (Vector((0, 0, 1)), Vector((1, 0, 0)), Vector((0, 1, 0)))
		u, v, w = basis
		bottom, top = [], []
		for i in range(segments):
			a = 2.0 * math.pi * i / segments
			d = u * math.cos(a) + v * math.sin(a)
			bottom.append(c - w * (length / 2) + d * radius)
			top.append(c + w * (length / 2) + d * rt)
		for i in range(segments):
			j = (i + 1) % segments
			self.poly([bottom[i], bottom[j], top[j], top[i]], mat)
		if caps:
			self.poly(list(reversed(bottom)), mat)
			self.poly(top, mat)

	def prism(self, outline, z0: float, z1: float, mat: str, side_mat: str | None = None) -> None:
		"""Vertical prism from a CCW plan outline [(x, y), ...] between z0 and z1."""
		pts = list(outline)
		bottom = [(x, y, z0) for x, y in pts]
		top = [(x, y, z1) for x, y in pts]
		self.poly(list(reversed(bottom)), mat)
		self.poly(top, mat)
		sm = side_mat or mat
		n = len(pts)
		for i in range(n):
			j = (i + 1) % n
			self.poly([bottom[i], bottom[j], top[j], top[i]], sm)

	def transform(self, matrix: Matrix, start_face: int = 0) -> None:
		"""Transforms the vertices of faces created since start_face (use face count before adding)."""
		self.bm.faces.ensure_lookup_table()
		verts = set()
		for f in self.bm.faces[start_face:]:
			verts.update(f.verts)
		for v in verts:
			v.co = matrix @ v.co

	def face_count(self) -> int:
		return len(self.bm.faces)

	def build(self, uv_world: bool = True, uv1: bool = False, weld: bool = True) -> bpy.types.Object:
		if weld:
			bmesh.ops.remove_doubles(self.bm, verts=self.bm.verts, dist=1e-6)
		mesh = bpy.data.meshes.new(self.name)
		self.bm.normal_update()
		self.bm.to_mesh(mesh)
		self.bm.free()
		obj = bpy.data.objects.new(self.name, mesh)
		bpy.context.scene.collection.objects.link(obj)
		for slot in self.slots:
			obj.data.materials.append(material(slot))
		if uv_world:
			uv_world_scale(obj)
		if uv1:
			uv_unique(obj)
		return obj


def uv_world_scale(obj, scale: float = 1.0) -> None:
	"""UV0 = world-scale box projection (1 UV unit = 1 m / scale) on the face's dominant axis (venue-dive-bar 13.5)."""
	mesh = obj.data
	if not mesh.uv_layers:
		mesh.uv_layers.new(name="UV0")
	uv = mesh.uv_layers[0].data
	world = obj.matrix_world
	for poly in mesh.polygons:
		n = poly.normal
		ax = max(range(3), key=lambda i: abs(n[i]))
		for li in poly.loop_indices:
			p = world @ mesh.vertices[mesh.loops[li].vertex_index].co
			if ax == 0:
				u, v = (p.y if n.x > 0 else -p.y), p.z
			elif ax == 1:
				u, v = (-p.x if n.y > 0 else p.x), p.z
			else:
				u, v = p.x, (p.y if n.z > 0 else -p.y)
			uv[li].uv = (u * scale, v * scale)


def uv_unique(obj, margin: float = 0.004) -> None:
	"""UV1 = unique non-overlapping unwrap (smart project) for wear / AO masks (venue-dive-bar 13.5)."""
	mesh = obj.data
	while len(mesh.uv_layers) < 2:
		mesh.uv_layers.new(name=f"UV{len(mesh.uv_layers)}")
	mesh.uv_layers.active_index = 1
	bpy.ops.object.select_all(action="DESELECT")
	bpy.context.view_layer.objects.active = obj
	obj.select_set(True)
	bpy.ops.object.mode_set(mode="EDIT")
	bpy.ops.mesh.select_all(action="SELECT")
	bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=margin)
	bpy.ops.object.mode_set(mode="OBJECT")
	mesh.uv_layers.active_index = 0


# ------------------------------------------------------------------------------------------------------------------------------
# Collision (UCX_<RenderMesh>_<nn>, venue-dive-bar 13.5: boxes, 12-sided cylinders, leg-accurate)
# ------------------------------------------------------------------------------------------------------------------------------


class Collision:
	def __init__(self, render_mesh_name: str):
		self.base = render_mesh_name
		self.objects: list[bpy.types.Object] = []

	def _add(self, builder: Builder) -> None:
		builder.name = f"UCX_{self.base}_{len(self.objects):02d}"
		obj = builder.build(uv_world=False)
		obj.data.materials.clear()
		self.objects.append(obj)

	def box(self, x0, x1, y0, y1, z0, z1) -> None:
		b = Builder("ucx")
		b.box(x0, x1, y0, y1, z0, z1, "UCX")
		self._add(b)

	def cylinder(self, center, radius: float, length: float, axis: str = "z", segments: int = 12) -> None:
		b = Builder("ucx")
		b.cylinder(center, radius, length, "UCX", axis=axis, segments=segments)
		self._add(b)


# ------------------------------------------------------------------------------------------------------------------------------
# Imperfection helpers (venue-dive-bar 9.2)
# ------------------------------------------------------------------------------------------------------------------------------


def jitter(r: random.Random, amount: float) -> float:
	return (r.random() * 2.0 - 1.0) * amount


def add_bevel(obj, width: float = 0.002, segments: int = 2, angle_deg: float = 40.0) -> None:
	"""Bevel modifier on sharp edges (every hard edge bevelled 0.5-3 mm, 9.2); applied by the exporter (use_mesh_modifiers)."""
	mod = obj.modifiers.new("RbBevel", "BEVEL")
	mod.width = width
	mod.segments = segments
	mod.limit_method = "ANGLE"
	mod.angle_limit = math.radians(angle_deg)
	mod.harden_normals = False


# ------------------------------------------------------------------------------------------------------------------------------
# Bounds, validation
# ------------------------------------------------------------------------------------------------------------------------------


def world_bounds(objects) -> tuple[Vector, Vector]:
	"""World-space AABB of mesh objects (evaluated, modifiers applied)."""
	depsgraph = bpy.context.evaluated_depsgraph_get()
	lo = Vector((float("inf"),) * 3)
	hi = Vector((float("-inf"),) * 3)
	for obj in objects:
		if obj.type != "MESH":
			continue
		evaluated = obj.evaluated_get(depsgraph)
		for corner in evaluated.bound_box:
			p = evaluated.matrix_world @ Vector(corner)
			lo = Vector((min(lo.x, p.x), min(lo.y, p.y), min(lo.z, p.z)))
			hi = Vector((max(hi.x, p.x), max(hi.y, p.y), max(hi.z, p.z)))
	return lo, hi


def triangle_count(objects) -> int:
	depsgraph = bpy.context.evaluated_depsgraph_get()
	total = 0
	for obj in objects:
		if obj.type != "MESH":
			continue
		mesh = obj.evaluated_get(depsgraph).to_mesh()
		mesh.calc_loop_triangles()
		total += len(mesh.loop_triangles)
		obj.evaluated_get(depsgraph).to_mesh_clear()
	return total


def assert_dimensions(objects, target_m: tuple[float, float, float], tolerance_m: float) -> None:
	lo, hi = world_bounds(objects)
	size = hi - lo
	for axis, (have, want) in enumerate(zip(size, target_m)):
		if abs(have - want) > tolerance_m:
			fail(f"dimension {'XYZ'[axis]}: {have:.4f} m, spec {want:.4f} m (+-{tolerance_m:.4f})")


# ------------------------------------------------------------------------------------------------------------------------------
# Export
# ------------------------------------------------------------------------------------------------------------------------------


def _mirrored_copies(objects) -> list:
	"""Evaluated copies of objects with y -> -y and the winding flipped (see MIRROR_Y_ON_EXPORT)."""
	depsgraph = bpy.context.evaluated_depsgraph_get()
	copies = []
	for obj in objects:
		evaluated = obj.evaluated_get(depsgraph)
		mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=depsgraph)
		mesh.transform(obj.matrix_world)
		mesh.transform(Matrix.Diagonal((1.0, -1.0, 1.0, 1.0)))
		flip_winding(mesh)
		copy = bpy.data.objects.new(obj.name, mesh)
		bpy.context.scene.collection.objects.link(copy)
		copies.append(copy)
	return copies


def flip_winding(mesh) -> None:
	"""Reverses every face (after a reflection: keeps the normals pointing outward)."""
	bm = bmesh.new()
	bm.from_mesh(mesh)
	bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
	bm.to_mesh(mesh)
	bm.free()


def export_fbx(objects, path: Path) -> None:
	"""FBX for Unreal (venue-dive-bar 13.4; axis handling frozen by the DB-0 axis test, MIRROR_Y_ON_EXPORT)."""
	path.parent.mkdir(parents=True, exist_ok=True)
	originals = list(objects)
	names = [o.name for o in originals]
	export_objects = originals
	if MIRROR_Y_ON_EXPORT:
		for o in originals:
			o.name = o.name + "__src"
		export_objects = _mirrored_copies(originals)
		for copy, name in zip(export_objects, names):
			copy.name = name
			copy.data.name = name
	bpy.ops.object.select_all(action="DESELECT")
	for obj in export_objects:
		obj.select_set(True)
	bpy.ops.export_scene.fbx(
		filepath=str(path),
		use_selection=True,
		apply_unit_scale=True,
		apply_scale_options="FBX_SCALE_UNITS",
		use_mesh_modifiers=True,
		mesh_smooth_type="FACE",
		add_leaf_bones=False,
		bake_anim=False,
		use_custom_props=True,
		use_triangles=True,
		use_metadata=False,
		axis_forward="-Z",
		axis_up="Y",
	)
	if MIRROR_Y_ON_EXPORT:
		for copy in export_objects:
			mesh = copy.data
			bpy.data.objects.remove(copy)
			bpy.data.meshes.remove(mesh)
		for o, name in zip(originals, names):
			o.name = name


def ledger_row(package: str, asset_id: str, generator: str, notes: str = "") -> None:
	"""Upserts the asset's own ledger row (source own, generated by our script) in Docs/licenses/ledger/<package>.csv."""
	path = REPO / "Docs" / "licenses" / "ledger" / f"{package}.csv"
	path.parent.mkdir(parents=True, exist_ok=True)
	rows = []
	if path.exists():
		with path.open("r", encoding="utf-8", newline="") as handle:
			rows = [r for r in csv.DictReader(handle)]
	key = f"SM_DB_{asset_id}"
	# ISO date of the row (venue-dive-bar 13.9): the date the asset first entered the ledger, kept on every re-run (regeneration stays
	# byte-identical); a new asset gets today's date.
	old = next((r for r in rows if r.get("asset_id") == key), None)
	date = (old or {}).get("date") or __import__("datetime").date.today().isoformat()
	row = {c: "" for c in LEDGER_COLUMNS}
	row.update({"asset_id": key, "used_by": package, "source": "own", "source_ref": f"Tools/blender/divebar/{generator}",
		"author": "RAW BREAK (generated)", "licence": "own", "licence_url": "", "date": date, "account": "", "sha256": "",
		"modified": "n", "ai_generated": "n", "steam_ai_disclosure": "n", "trademark_check": "n/a", "notes": notes})
	rows = [r for r in rows if r.get("asset_id") != key] + [row]
	rows.sort(key=lambda r: r["asset_id"])
	with path.open("w", encoding="utf-8", newline="") as handle:
		writer = csv.DictWriter(handle, fieldnames=LEDGER_COLUMNS, lineterminator="\n")
		writer.writeheader()
		writer.writerows(rows)


def export_asset(asset_id: str, objects, out_root: str | Path, meta: dict, collision: Collision | None = None,
		target_m: tuple | None = None, tolerance_m: float = 0.01, package: str | None = None, ledger: bool = True) -> Path:
	"""Exports Export/<Asset>/SM_DB_<Asset>.fbx + <Asset>.json (bounds, triangles, slots, the caller's meta) and the asset's own
	ledger row (package given). target_m asserts the render bounds (VDB-T4: +-2 mm hero, +-1 cm others). The render objects are
	joined into ONE mesh named SM_DB_<Asset> so the UCX hulls match it. Returns the JSON path."""
	mesh_name = f"SM_DB_{asset_id}"
	render = [o for o in objects if o.type == "MESH"]
	if not render:
		fail(f"{asset_id}: nothing to export")
	joined = join(render, mesh_name)
	if target_m is not None:
		assert_dimensions([joined], target_m, tolerance_m)
	folder = Path(out_root) / asset_id
	fbx = folder / f"{mesh_name}.fbx"
	hulls = collision.objects if collision else []
	export_fbx([joined] + hulls, fbx)
	lo, hi = world_bounds([joined])
	data = dict(meta)
	data.update({
		"asset_id": asset_id,
		"mesh": mesh_name,
		"fbx": fbx.name,
		"bounds_min_m": [round(v, 5) for v in lo],
		"bounds_max_m": [round(v, 5) for v in hi],
		"triangles": triangle_count([joined]),
		"material_slots": [m.name for m in joined.data.materials if m is not None],
		"collision_hulls": len(hulls),
		"frame": data.get("frame", "venue"),
		"generator": data.get("generator") or (Path(sys.argv[sys.argv.index("--python") + 1]).name if "--python" in sys.argv else ""),
	})
	if target_m is not None:
		data["target_m"] = [round(v, 5) for v in target_m]
		data["tolerance_m"] = tolerance_m
	data.setdefault("external_inputs", [])
	path = folder / f"{asset_id}.json"
	path.write_text(json.dumps(data, indent=1, sort_keys=True), encoding="utf-8")
	if package and ledger:
		ledger_row(package, asset_id, data["generator"], data.get("ledger_notes", ""))
	log(f"exported {asset_id}: {data['triangles']} triangles, {len(hulls)} hulls, slots {data['material_slots']}, "
		f"bounds {data['bounds_min_m']} .. {data['bounds_max_m']}")
	return path


def join(objects, name: str) -> bpy.types.Object:
	"""Joins mesh objects into one (modifiers applied), named name."""
	depsgraph = bpy.context.evaluated_depsgraph_get()
	bm = bmesh.new()
	slots: list[str] = []
	for obj in objects:
		evaluated = obj.evaluated_get(depsgraph)
		mesh = evaluated.to_mesh()
		remap = []
		for m in mesh.materials:
			n = m.name if m is not None else "MI_DB_Default"
			if n not in slots:
				slots.append(n)
			remap.append(slots.index(n))
		tmp = bmesh.new()
		tmp.from_mesh(mesh)
		tmp.transform(obj.matrix_world)
		for f in tmp.faces:
			f.material_index = remap[f.material_index] if remap else 0
		# append tmp into bm (keeps UV layers by name)
		mesh_tmp = bpy.data.meshes.new("__tmp")
		tmp.to_mesh(mesh_tmp)
		tmp.free()
		bm.from_mesh(mesh_tmp)
		bpy.data.meshes.remove(mesh_tmp)
		evaluated.to_mesh_clear()
	for obj in list(objects):
		data = obj.data
		bpy.data.objects.remove(obj)
		if data is not None and data.users == 0:
			bpy.data.meshes.remove(data)
	mesh = bpy.data.meshes.new(name)
	bm.to_mesh(mesh)
	bm.free()
	out = bpy.data.objects.new(name, mesh)
	bpy.context.scene.collection.objects.link(out)
	for s in slots:
		out.data.materials.append(material(s))
	return out
