"""Shared helpers for RAW BREAK editor Python scripts (run INSIDE Unreal via Tools/unreal/rbue.py py ...).

Conventions (Docs/ue-architecture.md section 9.3):
  * every script is idempotent: it deletes / overwrites what it generates, never edits hand-made content;
  * generated content lives under /Game/Generated/** (meshes, materials, levels) or /Game/Dev/** (pipeline tests);
  * a script signals failure with fail("...") which logs the marker RBUE_FAIL (rbue.py turns it into exit code 1).
"""

from __future__ import annotations

import unreal


def log(msg: str) -> None:
	unreal.log(f"[rb] {msg}")


def fail(msg: str) -> None:
	unreal.log_error(f"RBUE_FAIL {msg}")
	raise RuntimeError(msg)


def ensure_dir(path: str) -> None:
	if not unreal.EditorAssetLibrary.does_directory_exist(path):
		unreal.EditorAssetLibrary.make_directory(path)


def delete_asset_if_exists(path: str) -> None:
	if unreal.EditorAssetLibrary.does_asset_exist(path):
		if not unreal.EditorAssetLibrary.delete_asset(path):
			fail(f"could not delete {path}")


def new_level(path: str) -> unreal.World:
	"""Creates (or recreates) an empty, non-partitioned level asset at path and makes it the editor world."""
	delete_asset_if_exists(path)
	les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
	ok = False
	if les is not None:
		try:
			ok = les.new_level(path, False)
		except TypeError:
			ok = les.new_level(path)
	if not ok:
		# Fallback for commandlet contexts without the level editor: blank map + save as.
		world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
		if world is None or not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
			fail(f"could not create level {path}")
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	if world is None:
		fail("no editor world after creating the level")
	return world


def save_current_level(path: str) -> None:
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
		fail(f"could not save level {path}")
	log(f"saved level {path}")


def spawn(actor_class, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), label: str | None = None):
	"""Spawns an actor in the editor world. rotation = (pitch, yaw, roll) in degrees."""
	eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	actor = eas.spawn_actor_from_class(actor_class, unreal.Vector(*location), unreal.Rotator(roll=rotation[2], pitch=rotation[0], yaw=rotation[1]))
	if actor is None:
		fail(f"could not spawn {actor_class}")
	if label:
		actor.set_actor_label(label)
	return actor


def spawn_mesh(mesh_path: str, location, scale=(1.0, 1.0, 1.0), rotation=(0.0, 0.0, 0.0), label: str | None = None, material_path: str | None = None):
	mesh = unreal.load_asset(mesh_path)
	if mesh is None:
		fail(f"missing mesh {mesh_path}")
	actor = spawn(unreal.StaticMeshActor, location, rotation, label)
	comp = actor.static_mesh_component
	comp.set_static_mesh(mesh)
	actor.set_actor_scale3d(unreal.Vector(*scale))
	if material_path:
		mat = unreal.load_asset(material_path)
		if mat is None:
			fail(f"missing material {material_path}")
		comp.set_material(0, mat)
	return actor


def look_at_rotation(eye, target) -> tuple[float, float, float]:
	"""(pitch, yaw, roll) in degrees for a camera at eye looking at target (UE axes, cm)."""
	rot = unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*eye), unreal.Vector(*target))
	return (rot.pitch, rot.yaw, 0.0)
