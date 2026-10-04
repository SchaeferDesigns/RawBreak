"""Generates the M1 level /Game/Generated/Maps/L_M1_TestRoom (Docs/ue-architecture.md 8.3, 11, 12; M2-L 18.7). Owner: M2-L (UE-8
in M1).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_test_room.py            # the M1 map (committed, LFS)
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_test_room.py -- --dev   # + /Game/Dev/UE8/L_UE8_TestRoom

Contents (everything else is built by C++ from these actors at construction / BeginPlay, nothing is hand-placed):
  * World Settings: GameMode = ARbGameMode (the match starts from the URL options, e.g. ?Mode=HotSeat?Game=NineBall);
  * ARbTable (9-ft pro) at the origin, baked meshes, its generated materials - the part components are transient;
  * ARbTestRoom at the origin: shell, WPA lamp (>= 520 lux on bed and rails), ambient panels, post-process baseline; the
    generator writes the lamp's underside height into ARbTable::LampUndersideHeight (single source, review R-14);
  * PlayerStart at the head end facing the table;
  * four ARbLookDevCameras with the RbAssetPaths::CaptureCamera tags (Eyes preset) for Tools/unreal/capture_m1.py;
  * M2-L: the six table look-dev cameras RbCam_TL_* (rb_m1_layout.TABLE_VIEWS) for Tools/unreal/capture_table.py.
Idempotent: a re-run reloads the map, removes every actor and rebuilds it. After saving, the C++ level validator
(ARbTestRoom::ValidateM1Level) checks the level and prints its report (A2 compares it between runs); any FAIL fails the run.
--dev also writes the same room with flat dev materials on the table (/Game/Dev, git-ignored) for look-dev before the generated
materials exist. build() is reused by rb_dev_m2l.py for the 7-ft look-dev room.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402
import rb_m1_layout as layout  # noqa: E402

# Flat dev materials (linear albedo, roughness) per ERbTablePart: Bed, CushionCloth, RailCaps, Apron, PocketLiners, Sights, Legs (the
# M2-L parts keep their defaults).
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


def table_look_dev_cameras(bed, preset: str, exposure_bias_ev: float = 0.0) -> None:
	"""The seven M2-L table look-dev cameras (rb_m1_layout.TABLE_VIEWS): placed at runtime from the table geometry. exposure_bias_ev:
	the look-dev room's exposure calibration (ARbLookDevCamera::ExposureBiasEv, rb_m1_layout.LOOKDEV_EXPOSURE_BIAS_EV), plus the
	view's own offset (rb_m1_layout.LOOKDEV_VIEW_EXPOSURE_OFFSET_EV)."""
	aim = (0.25 * layout.TABLE_LENGTH_M.get(preset, 2.54), 0.0)   # foot spot (rack apex)
	for name, tag, view in layout.TABLE_VIEWS:
		bias = exposure_bias_ev + layout.LOOKDEV_VIEW_EXPOSURE_OFFSET_EV.get(name, 0.0)   # the room's calibration + the view's offset
		if view is None:
			look_dev_camera(tag, (bed.x - 120.0, bed.y, bed.z + 16.0), (0.0, 0.0, 0.0), 0.0,
				placement=unreal.RbLookDevPlacement.CHIN_ON_CUE,
				cue_ball_core=unreal.Vector2D(*layout.CUE_BALL_CORE),
				aim_point_core=unreal.Vector2D(*aim),
				cue_elevation_deg=layout.CUE_ELEVATION_DEG,
				exposure_bias_ev=bias)
			continue
		look_dev_camera(tag, (bed.x, bed.y, bed.z + 100.0), (0.0, 0.0, 0.0), 0.0,
			placement=unreal.RbLookDevPlacement.TABLE_VIEW,
			table_view=getattr(unreal.RbTableLookDevView, view),
			hide_lamp_fixture=(view == "OVERHEAD"),
			exposure_bias_ev=bias)


def albedo_card(bed, preset: str, material_dir: str) -> None:
	"""A known-albedo card (rb_m1_layout.ALBEDO_CARD, three 5 x 5 cm patches, 2 mm thick) lying on the bed next to the right long
	cushion at x = 0.15 m (core), in the chin-on-cue and standing views: the capture's cloth pixels are compared with it."""
	half_w = 0.25 * layout.TABLE_LENGTH_M.get(preset, 2.54)
	for i, color in enumerate(layout.ALBEDO_CARD):
		mi = dev_material_at(material_dir, f"MI_M2L_Card{i}", color, 0.9)
		# Core (0.15 + 0.055 i, -(W/2 - 0.10)) -> UE (x, +y); 1 mm above the cloth.
		loc = (bed.x + 15.0 + 5.5 * i, bed.y + 100.0 * (half_w - 0.10), bed.z + 0.1)
		actor = rb.spawn_mesh("/Engine/BasicShapes/Cube", loc, (0.05, 0.05, 0.002), (0.0, 0.0, 0.0), f"AlbedoCard{i}")
		actor.static_mesh_component.set_material(0, mi)
		actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)


def dev_material_at(directory: str, name: str, color: tuple, roughness: float) -> unreal.MaterialInstanceConstant:
	path = f"{directory}/{name}"
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mi = tools.create_asset(name, directory, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	mel = unreal.MaterialEditingLibrary
	mel.set_material_instance_parent(mi, unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial"))
	mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
	mel.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
	mel.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	return mi


def build(path: str, dev_materials: bool = False, preset: str = "NINE_FOOT_PRO", ball_set: str = "STANDARD_POOL", room: dict | None = None,
		ball_material: str | None = None, m1: bool = True, table_props: dict | None = None, lookdev: bool = False,
		exposure_bias_ev: float = 0.0) -> None:
	"""Builds a test-room level at path around one table of preset. m1: the M1 capture cameras + the M1 validator (L_M1_TestRoom).
	room: ARbTestRoom property overrides (snake_case names; tuples of 3 = linear colours, of 2 = FVector2D). ball_material: the ball
	set's BallMaterialOverride (a placed ARbBallSet, which ARbGameMode uses). lookdev: the six M2-L table cameras and the
	known-albedo card (dev levels only; the M1 map stays as M1 defined it). exposure_bias_ev: exposure calibration of the look-dev
	cameras (0 = the Eyes preset unchanged)."""
	world = fresh_level(path)
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbGameMode)

	# The table first: the room and the cameras read its TableSpec / frame at construction.
	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "Table")
	table.set_editor_property("preset", getattr(unreal.RbTablePreset, preset))
	table.set_editor_property("ball_set", getattr(unreal.RbBallSetPreset, ball_set))
	table.set_editor_property("use_baked_meshes", True)
	for key, value in (table_props or {}).items():
		table.set_editor_property(key, value)
	if dev_materials:
		table.set_editor_property("part_materials", [dev_material(n, c, r) for n, c, r in DEV_PART_MATERIALS])
	table.rebuild_table()
	bed = table.get_bed_center_world()

	test_room = rb.spawn(unreal.RbTestRoom, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "TestRoom")
	for key, value in (room or {}).items():
		if isinstance(value, tuple) and len(value) == 3:
			value = unreal.LinearColor(value[0], value[1], value[2], 1.0)
		elif isinstance(value, tuple) and len(value) == 2:
			value = unreal.Vector2D(value[0], value[1])
		elif isinstance(value, str) and value.startswith("/Game/"):
			value = unreal.load_asset(value)
		test_room.set_editor_property(key, value)
	test_room.rebuild_room()
	# Single source (R-14): the physics' off-table apex check sees the rendered lamp.
	table.set_editor_property("lamp_underside_height", test_room.get_lamp_underside_height_meters())
	table.rebuild_table()

	if ball_material:
		balls = rb.spawn(unreal.RbBallSet, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "BallSet")
		balls.set_editor_property("ball_material_override", unreal.load_asset(ball_material))

	# Head end = core -x = UE -X; the pawn starts 60 cm behind the head rail, facing the table.
	head = 100.0 * (0.5 * layout.TABLE_LENGTH_M.get(preset, 2.54)) + 17.78 + 60.0
	rb.spawn(unreal.PlayerStart, (-head, 0.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	if m1:
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
	if lookdev:
		# M2-L table look-dev cameras and the known-albedo card.
		table_look_dev_cameras(bed, preset, exposure_bias_ev)
		albedo_card(bed, preset, os.path.dirname(path))

	rb.save_current_level(path)
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		rb.fail(f"{path} was not written")

	if m1:
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


if __name__ == "__main__":
	main()
