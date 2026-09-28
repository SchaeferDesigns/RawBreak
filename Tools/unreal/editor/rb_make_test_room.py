"""UE-8: generates the M1 level /Game/Generated/Maps/L_M1_TestRoom (Docs/ue-architecture.md 8.3, 11, 12).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_test_room.py            # the M1 map (committed, LFS)
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_test_room.py -- --dev   # + /Game/Dev/UE8/L_UE8_TestRoom

Contents (everything else is built by C++ from these actors at construction / BeginPlay, nothing is hand-placed):
  * World Settings: GameMode = ARbGameMode (the match starts from the URL options, e.g. ?Mode=HotSeat?Game=NineBall);
  * ARbTable (9-ft pro) at the origin, baked meshes (UE-1), its generated materials (UE-3) - the part components are transient;
  * ARbTestRoom at the origin: shell, WPA lamp (>= 520 lux on bed and rails), ambient panels, post-process baseline; the
    generator writes the lamp's underside height into ARbTable::LampUndersideHeight (single source, review R-14);
  * PlayerStart at the head end facing the table;
  * four ARbLookDevCameras with the RbAssetPaths::CaptureCamera tags (Eyes preset) for Tools/unreal/capture_m1.py.
Idempotent: a re-run reloads the map, removes every actor and rebuilds it. After saving, the C++ level validator
(ARbTestRoom::ValidateM1Level) checks the level and prints its report (A2 compares it between runs); any FAIL fails the run.
--dev also writes the same room with flat dev materials on the table (/Game/Dev, git-ignored) for look-dev before UE-3's
materials exist.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402
import rb_m1_layout as layout  # noqa: E402

# Flat dev materials (linear albedo, roughness) per ERbTablePart: Bed, CushionCloth, RailCaps, Apron, PocketLiners, Sights, Legs.
DEV_PART_MATERIALS = [
	("Cloth", (0.030, 0.150, 0.065), 0.85),
	("Cloth", (0.030, 0.150, 0.065), 0.85),
	("Wood", (0.180, 0.065, 0.025), 0.35),
	("Wood", (0.180, 0.065, 0.025), 0.35),
	("Liner", (0.015, 0.012, 0.010), 0.60),
	("Sight", (0.850, 0.830, 0.780), 0.30),
	("Wood", (0.180, 0.065, 0.025), 0.35),
]


def fresh_level(path: str) -> unreal.World:
	"""An empty level at path. rb_common.new_level cannot replace an existing map in the commandlet (the level editor refuses the
	destination), so a re-run loads the map and removes its actors instead."""
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		return rb.new_level(path)
	world = unreal.EditorLoadingAndSavingUtils.load_map(path)
	if world is None:
		rb.fail(f"could not load {path}")
	eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	for actor in eas.get_all_level_actors():
		if not isinstance(actor, unreal.WorldSettings):
			eas.destroy_actor(actor)
	return world


def dev_material(name: str, color: tuple, roughness: float) -> unreal.MaterialInstanceConstant:
	path = f"{layout.DEV_DIR}/MI_UE8_{name}"
	if unreal.EditorAssetLibrary.does_asset_exist(path):
		return unreal.load_asset(path)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	mi = tools.create_asset(f"MI_UE8_{name}", layout.DEV_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	if mi is None:
		rb.fail(f"could not create {path}")
	mel = unreal.MaterialEditingLibrary
	mel.set_material_instance_parent(mi, unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial"))
	mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
	mel.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
	mel.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi)
	return mi


def look_dev_camera(tag: str, eye, rotation, focus_cm: float = 0.0, **props) -> unreal.Actor:
	cam = rb.spawn(unreal.RbLookDevCamera, eye, rotation, tag)
	cam.tags = [tag]
	cam.set_editor_property("preset", unreal.RbCameraPreset.EYES)
	cam.set_editor_property("focus_distance_cm", focus_cm)
	for key, value in props.items():
		cam.set_editor_property(key, value)
	return cam


def build(path: str, dev_materials: bool) -> None:
	world = fresh_level(path)
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbGameMode)

	# The table first: the room and the cameras read its TableSpec / frame at construction.
	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "Table")
	table.set_editor_property("preset", unreal.RbTablePreset.NINE_FOOT_PRO)
	table.set_editor_property("ball_set", unreal.RbBallSetPreset.STANDARD_POOL)
	table.set_editor_property("use_baked_meshes", True)
	if dev_materials:
		table.set_editor_property("part_materials", [dev_material(n, c, r) for n, c, r in DEV_PART_MATERIALS])
	table.rebuild_table()
	bed = table.get_bed_center_world()

	room = rb.spawn(unreal.RbTestRoom, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "TestRoom")
	room.rebuild_room()
	# Single source (R-14): the physics' off-table apex check sees the rendered lamp.
	table.set_editor_property("lamp_underside_height", room.get_lamp_underside_height_meters())
	table.rebuild_table()

	# Head end = core -x = UE -X; the pawn starts 60 cm behind the head rail, facing the table.
	rb.spawn(unreal.PlayerStart, (-205.0, 0.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	# Look-dev capture cameras (Eyes preset: V = 50 deg at any aspect).
	# Overhead: straight down from 2.0 m above the cloth (fits the table + rails at V = 50 deg; image right = foot, up = core +y);
	# the lamp fixture is hidden while it is the view target.
	look_dev_camera(layout.CAM_OVERHEAD, (bed.x, bed.y, bed.z + 200.0), (-90.0, -90.0, 0.0), 200.0, hide_lamp_fixture=True)
	# Chin on the cue (plan 4.2 / 6.3: the single most important look-dev view): placed at runtime from the cue axis.
	look_dev_camera(layout.CAM_CHIN_ON_CUE, (bed.x - 120.0, bed.y, bed.z + 16.0), (0.0, 0.0, 0.0), 0.0,
		placement=unreal.RbLookDevPlacement.CHIN_ON_CUE,
		cue_ball_core=unreal.Vector2D(*layout.CUE_BALL_CORE),
		aim_point_core=unreal.Vector2D(*layout.AIM_POINT_CORE),
		cue_elevation_deg=layout.CUE_ELEVATION_DEG)
	# Ball close-up: low, from the head side of the rack (apex on the foot spot, UE X = 63.5 cm); focus on the rack centre.
	eye = (bed.x + 35.0, bed.y - 30.0, bed.z + 13.0)
	target = (bed.x + 72.0, bed.y, bed.z + 2.9)
	look_dev_camera(layout.CAM_BALL_CLOSEUP, eye, rb.look_at_rotation(eye, target), math.dist(eye, target))
	# Room overview from the head-left corner at standing eye height (under the lamp's underside).
	eye = (-280.0, 215.0, 170.0)
	target = (30.0, 0.0, bed.z + 5.0)
	look_dev_camera(layout.CAM_ROOM, eye, rb.look_at_rotation(eye, target), 0.0)

	rb.save_current_level(path)
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		rb.fail(f"{path} was not written")

	report, ok = unreal.RbTestRoom.validate_m1_level(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world())
	for line in str(report).splitlines():
		rb.log(f"validator {line}")
	if not ok:
		rb.fail(f"{path}: level validator failed")


def main() -> None:
	args = [a for a in sys.argv[1:] if a and not a.lower().endswith(".py")]
	rb.ensure_dir("/Game/Generated/Maps")
	build(layout.MAP, dev_materials=False)
	rb.log(f"M1 test room OK: {layout.MAP}")
	if "--dev" in args:
		rb.ensure_dir(layout.DEV_DIR)
		build(layout.DEV_MAP, dev_materials=True)
		rb.log(f"UE-8 dev room OK: {layout.DEV_MAP}")


main()
