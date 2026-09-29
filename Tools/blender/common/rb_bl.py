"""Common library of the RAW BREAK Blender generators (runs INSIDE Blender 5.2; venue-dive-bar 13.1-13.5,
Docs/ue-architecture.md 18.8). Import from a generator with:

	import sys, pathlib
	sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "common"))
	import rb_bl

Conventions (venue-dive-bar 13.3-13.5): metric, 1 BU = 1 m, venue frame V = UE world axes (+X into the room, +Y right, +Z up);
pivots at the floor-contact centre (floor items), wall plane bottom centre (wall items), ceiling anchor (hanging items); seeded
determinism (rng(asset_id, instance), no wall clock, sorted iteration); every export writes
Art/<Venue>/Export/<Asset>/SM_DB_<Asset>.fbx + <Asset>.json (one metadata file per asset: bounds, triangle counts, target
dimensions, material slots, collision profile, acoustic material, external inputs for the licence check). The importer
(Tools/unreal/editor/rb_import_divebar.py) re-checks the bounds (VDB-T4).
Owner: M2-A (basic helpers implemented by the M2 architect step and smoke-tested with Blender 5.2; UV / bake / UCX / bevel helpers
TODO(M2-A)).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import sys
from pathlib import Path

import bpy  # noqa: F401 - Blender only
from mathutils import Vector

REPO = Path(__file__).resolve().parents[3]


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
	return parser.parse_args(argv)


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


def export_fbx(objects, path: Path) -> None:
	"""FBX for Unreal (venue-dive-bar 13.4; the axis settings are frozen by the DB-0 axis test, M2-A)."""
	path.parent.mkdir(parents=True, exist_ok=True)
	bpy.ops.object.select_all(action="DESELECT")
	for obj in objects:
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
	)


def export_asset(asset_id: str, objects, out_root: str | Path, meta: dict) -> Path:
	"""Exports Export/<Asset>/SM_DB_<Asset>.fbx + <Asset>.json (bounds, triangles, the caller's meta). Returns the JSON path."""
	folder = Path(out_root) / asset_id
	fbx = folder / f"SM_DB_{asset_id}.fbx"
	export_fbx(objects, fbx)
	lo, hi = world_bounds(objects)
	data = dict(meta)
	data.update({
		"asset_id": asset_id,
		"fbx": fbx.name,
		"bounds_min_m": [round(v, 5) for v in lo],
		"bounds_max_m": [round(v, 5) for v in hi],
		"triangles": triangle_count(objects),
		"generator": Path(sys.argv[sys.argv.index("--python") + 1]).name if "--python" in sys.argv else "",
	})
	path = folder / f"{asset_id}.json"
	path.write_text(json.dumps(data, indent=1, sort_keys=True), encoding="utf-8")
	log(f"exported {asset_id}: {data['triangles']} triangles, bounds {data['bounds_min_m']} .. {data['bounds_max_m']}")
	return path


# TODO(M2-A): bevel / jitter helpers (9.2), UV0 world scale + UV1 unique unwrap (13.5), UCX hull builders (boxes, 12-sided
# cylinders), Cycles bakes of the wear mask T_DB_<Asset>_WM (convexity, AO, touch mask, height) and AO, texture export.
