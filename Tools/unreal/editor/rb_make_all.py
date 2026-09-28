"""Runs every RAW BREAK content generator in dependency order and records the asset metrics of acceptance check A2
(Docs/ue-architecture.md 11, 12). Owner: UE-8.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_all.py                    # regenerate /Game/Generated
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_all.py -- --strict        # every generator must exist (M1 / A2)
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_all.py -- --compare       # fail if the metrics differ from the last run

Order: materials (UE-3; the bakes assign the generated part materials when they exist) -> baked table meshes (UE-1) -> ball
mesh (UE-2) -> cue meshes (UE-4) -> M1 test room level (UE-8, validated by ARbTestRoom::ValidateM1Level). Each generator runs
in this editor process with its own sys.argv; a generator that fails (RBUE_FAIL / exception) stops the run. A generator whose
script does not exist yet is skipped with a warning (fails with --strict).

A2 ("regenerate from scratch; a second run gives the same asset metrics", byte-identical packages are not required because UE
re-saves packages with new GUIDs): after the generators, the metrics of everything under /Game/Generated - static meshes
(LOD0 triangles, bounds, Nanite), materials / instances / parameter collections (parameter names and default values) - plus the
level validator's report are written to Saved/RbLogs/rb_make_all_metrics.json. The previous file is compared first; --compare
turns a difference into a failure.
"""

import json
import os
import runpy
import sys

import unreal

EDITOR_DIR = os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor")
sys.path.insert(0, EDITOR_DIR)
import rb_common as rb  # noqa: E402
import rb_m1_layout as layout  # noqa: E402

# (script, owner) in dependency order.
GENERATORS = [
	("rb_make_materials.py", "UE-3"),
	("rb_bake_table.py", "UE-1"),
	("rb_bake_ball.py", "UE-2"),
	("rb_bake_cue.py", "UE-4"),
	("rb_make_test_room.py", "UE-8"),
]

METRICS = os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "RbLogs", "rb_make_all_metrics.json"))
MEL = unreal.MaterialEditingLibrary


def run_generator(script: str, owner: str, strict: bool) -> bool:
	path = os.path.join(EDITOR_DIR, script)
	if not os.path.exists(path):
		if strict:
			rb.fail(f"generator {script} ({owner}) missing")
		unreal.log_warning(f"[rb] rb_make_all: {script} ({owner}) not present yet - skipped")
		return False
	rb.log(f"rb_make_all: running {script} ({owner})")
	saved_argv = sys.argv
	sys.argv = [path]
	try:
		runpy.run_path(path, run_name="__main__")
	except Exception as error:  # noqa: BLE001 - every generator failure stops the run with its message
		rb.fail(f"{script} ({owner}) failed: {error}")
	finally:
		sys.argv = saved_argv
	return True


def _round(value: float) -> float:
	return round(float(value), 4)


def mesh_metrics(mesh: unreal.StaticMesh) -> dict:
	box = mesh.get_bounding_box()
	nanite = mesh.get_editor_property("nanite_settings")
	try:
		triangles = int(mesh.get_num_triangles(0))
	except AttributeError:
		triangles = -1
	return {
		"triangles": triangles,
		"bounds_min": [_round(box.min.x), _round(box.min.y), _round(box.min.z)],
		"bounds_max": [_round(box.max.x), _round(box.max.y), _round(box.max.z)],
		"nanite": bool(nanite.enabled),
	}


def _color(value) -> list:
	return [_round(value.r), _round(value.g), _round(value.b), _round(value.a)]


def material_metrics(asset) -> dict:
	out = {"class": asset.get_class().get_name(), "scalars": {}, "vectors": {}}
	if isinstance(asset, unreal.MaterialParameterCollection):
		for p in asset.get_editor_property("scalar_parameters"):
			out["scalars"][str(p.get_editor_property("parameter_name"))] = _round(p.get_editor_property("default_value"))
		for p in asset.get_editor_property("vector_parameters"):
			out["vectors"][str(p.get_editor_property("parameter_name"))] = _color(p.get_editor_property("default_value"))
		return out
	is_instance = isinstance(asset, unreal.MaterialInstanceConstant)
	for name in MEL.get_scalar_parameter_names(asset):
		value = MEL.get_material_instance_scalar_parameter_value(asset, name) if is_instance else MEL.get_material_default_scalar_parameter_value(asset, name)
		out["scalars"][str(name)] = _round(value)
	for name in MEL.get_vector_parameter_names(asset):
		value = MEL.get_material_instance_vector_parameter_value(asset, name) if is_instance else MEL.get_material_default_vector_parameter_value(asset, name)
		out["vectors"][str(name)] = _color(value)
	return out


def collect_metrics() -> dict:
	metrics = {"meshes": {}, "materials": {}, "levels": {}}
	for object_path in sorted(unreal.EditorAssetLibrary.list_assets("/Game/Generated", recursive=True, include_folder=False)):
		package = object_path.split(".")[0]
		asset = unreal.load_asset(object_path)
		if isinstance(asset, unreal.StaticMesh):
			metrics["meshes"][package] = mesh_metrics(asset)
		elif isinstance(asset, (unreal.Material, unreal.MaterialInstanceConstant, unreal.MaterialParameterCollection)):
			metrics["materials"][package] = material_metrics(asset)
	if unreal.EditorAssetLibrary.does_asset_exist(layout.MAP):
		world = unreal.EditorLoadingAndSavingUtils.load_map(layout.MAP)
		report, ok = unreal.RbTestRoom.validate_m1_level(world)
		metrics["levels"][layout.MAP] = {"validator_ok": bool(ok), "report": str(report).splitlines()}
	return metrics


def diff(old, new, path: str = "") -> list:
	if isinstance(old, dict) and isinstance(new, dict):
		out = []
		for key in sorted(set(old) | set(new)):
			if key not in old or key not in new:
				out.append(f"{path}/{key}: {'added' if key not in old else 'removed'}")
			else:
				out += diff(old[key], new[key], f"{path}/{key}")
		return out
	return [] if old == new else [f"{path}: {old} -> {new}"]


def main() -> None:
	args = [a for a in sys.argv[1:] if a and not a.lower().endswith(".py")]
	strict = "--strict" in args
	ran = [script for script, owner in GENERATORS if run_generator(script, owner, strict)]
	metrics = collect_metrics()
	metrics["generators"] = ran

	previous = None
	if os.path.exists(METRICS):
		with open(METRICS, "r", encoding="utf-8") as handle:
			previous = json.load(handle)
	os.makedirs(os.path.dirname(METRICS), exist_ok=True)
	with open(METRICS, "w", encoding="utf-8") as handle:
		json.dump(metrics, handle, indent=1, sort_keys=True)

	for level, data in metrics["levels"].items():
		if not data["validator_ok"]:
			rb.fail(f"{level}: level validator failed")
	rb.log(f"rb_make_all: {len(ran)} generator(s) {ran}; {len(metrics['meshes'])} meshes, {len(metrics['materials'])} materials, "
		f"{len(metrics['levels'])} level(s) -> {METRICS}")
	if previous is not None:
		changes = diff(previous, metrics)
		if changes:
			for line in changes[:40]:
				unreal.log_warning(f"[rb] A2 metric changed: {line}")
			if "--compare" in args:
				rb.fail(f"A2: {len(changes)} metric(s) differ from the previous run")
		else:
			rb.log("A2: asset metrics identical to the previous run")


main()
