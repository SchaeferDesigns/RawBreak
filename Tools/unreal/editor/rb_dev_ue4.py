"""UE-4 dev maps (Docs/ue-architecture.md 10, 13): the cue - baked meshes, pose at address, automatic minimum elevation over
balls and rails, the wall sweep of a short-cue situation - in the real renderer.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_cue.py          # first: the baked cue meshes
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue4.py
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue4.py -- --list   # prints every capture command
(PowerShell, or Git Bash with MSYS_NO_PATHCONV=1.)

Levels (scratch, /Game/Dev is git-ignored), each a closed room with the 9-ft pro table (UE-1's baked meshes, flat dev
materials), a table lamp, fixed exposure and one ARbCueDemo that builds the scene at BeginPlay through the game's code:
  L_DevUE4_Lineup     every cue preset lying on the bed (Playing, Break, Jump, House): lengths, tapers, sections
  L_DevUE4_Address    the playing cue at address (tip 15 mm behind the cue ball, aimed at the foot spot), chin-on-cue view
  L_DevUE4_Fallback   the Address scene without a material override: ARbCue's fallback material (engine vertex colours)
  L_DevUE4_BallFloor  a ball 0.10 m straight behind the cue ball (plan T13): the butt rises to the ball floor (~21.4 deg)
  L_DevUE4_RailFloor  the cue ball 6 cm from the head cushion, shooting away: the butt rises over the rail (~14.6 deg)
  L_DevUE4_Wall       a wall 1.45 m behind the cue ball: the environment sweep raises the 58 in cue to ~37.4 deg
  L_DevUE4_WallShort  the same wall with the 48 in bar short cue (runtime mesh): playable at ~17 deg
Cue material: UE-3's M_RbCue when it exists, else M_DevCue (Substrate slab: albedo = the mesh's per-section vertex colour,
roughness = its alpha). Balls: ARbBallSet's fallback (engine material in the ball colour) unless UE-3's M_RbBall exists.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DIR = "/Game/Dev/UE4"
OUT = "Docs/images/dev/UE-4"
CUE_MATERIAL = "/Game/Generated/Materials/M_RbCue"  # RbAssetPaths::MatCue (UE-3)
BED = 76.5            # 9-ft pro bed height [cm] (checked against the table below)
HALF_LENGTH = 1.27    # [m]
R = 0.028575          # ball radius [m]
EV100 = 9.0           # manual exposure of the dev captures
MEL = unreal.MaterialEditingLibrary

COLORS = {
	"Cloth": (0.03, 0.16, 0.07),
	"Wood": (0.20, 0.07, 0.025),
	"Liner": (0.015, 0.012, 0.01),
	"Sight": (0.85, 0.83, 0.78),
	"Room": (0.40, 0.39, 0.37),
	"Floor": (0.12, 0.10, 0.09),
	"Wall": (0.55, 0.52, 0.47),
}


def world(x: float, y: float, z: float) -> tuple:
	"""Core table frame [m] (+x foot, +y left, z above the cloth) -> world [cm] for the table at the origin, yaw 0."""
	return (100.0 * x, -100.0 * y, BED + 100.0 * z)


# --------------------------------------------------------------------------------------------------------------------
# Scenes: level -> demo settings + capture cameras (label, eye core m, target core m, horizontal fov)
# --------------------------------------------------------------------------------------------------------------------

AIM = math.degrees(math.atan2(0.0 - 0.1, 0.635 + 0.6))  # cue ball (-0.6, 0.1) -> foot spot


def _chin_on_cue(cue_ball, azimuth_deg: float, elevation_deg: float, back: float, height: float, lateral: float = 0.0):
	"""Eye of the camera rig down on the shot (plan 4.2): e = P_axis(s_e) + h_c n_up + y_vc n_side, looking along the cue."""
	a = math.radians(azimuth_deg)
	t = math.radians(elevation_deg)
	d = (math.cos(t) * math.cos(a), math.cos(t) * math.sin(a), -math.sin(t))
	up = (math.sin(t) * math.cos(a), math.sin(t) * math.sin(a), math.cos(t))
	side = (math.sin(a), -math.cos(a), 0.0)
	contact = (cue_ball[0] - R * d[0], cue_ball[1] - R * d[1], R - R * d[2])
	eye = tuple(contact[i] - back * d[i] + height * up[i] + lateral * side[i] for i in range(3))
	target = (cue_ball[0] + 1.0 * d[0], cue_ball[1] + 1.0 * d[1], R)
	return eye, target


SCENES = {
	"Lineup": {
		"demo": {"scene": "LINEUP", "cue_ball": (0.62, 0.0), "lineup_spacing_cm": 12.0},
		"cameras": [
			("Lineup", (0.35, -1.05, 0.80), (-0.10, 0.0, 0.0), 55.0),
			("LineupTips", (0.78, -0.16, 0.085), (0.60, 0.0, 0.004), 36.0),
			("LineupButts", (-1.02, -0.22, 0.10), (-0.80, 0.0, 0.012), 40.0),
		],
	},
	"Address": {
		"demo": {"cue_ball": (-0.6, 0.1), "object_balls": [(0.635, 0.0)], "azimuth_deg": AIM, "elevation_deg": 5.0, "tip_back_cm": 1.5},
		"cameras": [
			("ChinOnCue",) + _chin_on_cue((-0.6, 0.1), AIM, 5.0, 0.40, 0.085, 0.0) + (60.0,),
			("TipSide", (-0.6 - 0.05, 0.1 - 0.16, 0.035), (-0.6 - 0.04, 0.1, 0.028), 32.0),
		],
	},
	"Fallback": {
		# No material override: ARbCue's own fallback (M_RbCue, else the engine vertex-colour material) - what the game shows
		# before UE-3's M_RbCue exists.
		"demo": {"cue_ball": (-0.6, 0.1), "object_balls": [(0.635, 0.0)], "azimuth_deg": AIM, "elevation_deg": 5.0, "tip_back_cm": 1.5},
		"no_cue_material": True,
		"cameras": [
			("Fallback", (-0.6 - 0.25, 0.1 - 0.30, 0.10), (-0.6 - 0.16, 0.1, 0.03), 40.0),
		],
	},
	"BallFloor": {
		"demo": {"cue_ball": (0.10, 0.20), "object_balls": [(0.0, 0.20), (0.70, 0.25)], "azimuth_deg": 0.0, "elevation_deg": 0.0,
			"tip_back_cm": 0.5},
		"cameras": [
			("BallFloor", (-0.02, 0.20 - 0.62, 0.10), (-0.02, 0.20, 0.06), 40.0),
		],
	},
	"RailFloor": {
		"demo": {"cue_ball": (-HALF_LENGTH + 0.06, 0.10), "azimuth_deg": 0.0, "elevation_deg": 0.0, "tip_back_cm": 0.5},
		"cameras": [
			("RailFloor", (-1.30, 0.10 - 1.15, 0.10), (-1.30, 0.10, 0.06), 24.0),
		],
	},
	"Wall": {
		"demo": {"cue_ball": (-HALF_LENGTH + 0.30, 0.0), "azimuth_deg": 0.0, "elevation_deg": 0.0, "tip_back_cm": 0.5},
		"wall": -HALF_LENGTH + 0.30 - 1.45,
		"cameras": [
			("Wall", (-1.75, -2.05, 0.35), (-1.75, 0.0, 0.40), 62.0),
		],
	},
	"WallShort": {
		"demo": {"cue_ball": (-HALF_LENGTH + 0.30, 0.0), "azimuth_deg": 0.0, "elevation_deg": 0.0, "tip_back_cm": 0.5,
			"cue_length_override_cm": 121.92},
		"wall": -HALF_LENGTH + 0.30 - 1.45,
		"cameras": [
			("WallShort", (-1.75, -2.05, 0.35), (-1.75, 0.0, 0.40), 62.0),
		],
	},
}


def level_path(scene: str) -> str:
	return f"{DIR}/L_DevUE4_{scene}"


def cam_name(label: str) -> str:
	return f"UE4_{label}"


def capture_commands() -> list:
	out = []
	for scene, spec in SCENES.items():
		for label, _, _, _ in spec["cameras"]:
			snake = "".join(("_" + c.lower()) if c.isupper() and i else c.lower() for i, c in enumerate(label))
			out.append(f"python Tools/unreal/rbue.py capture --map {level_path(scene)} --camera {cam_name(label)} --out {OUT}/{snake}.png")
	return out


# --------------------------------------------------------------------------------------------------------------------
# Materials
# --------------------------------------------------------------------------------------------------------------------

def make_flat_material(name: str, color: tuple) -> unreal.MaterialInstanceConstant:
	"""Instance of the engine's BasicShapeMaterial with a flat colour (like the UE-1 / UE-2 dev maps)."""
	path = f"{DIR}/{name}"
	rb.delete_asset_if_exists(path)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	mi = tools.create_asset(name, DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	if mi is None:
		rb.fail(f"could not create {path}")
	parent = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
	if parent is None:
		rb.fail("missing /Engine/BasicShapes/BasicShapeMaterial")
	MEL.set_material_instance_parent(mi, parent)
	MEL.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi)
	return mi


def _node(material, cls, x: int, y: int, **props):
	expression = MEL.create_material_expression(material, cls, x, y)
	for key, value in props.items():
		expression.set_editor_property(key, value)
	return expression


def _link(src, dst, pin: str = "", src_pin: str = "") -> None:
	if not MEL.connect_material_expressions(src, src_pin, dst, pin):
		rb.fail(f"could not connect {src.get_name()}.{src_pin} -> {dst.get_name()}.{pin}")


def make_dev_cue_material() -> str:
	"""Substrate slab: diffuse albedo = vertex colour RGB (the builder's linear section albedo), roughness = vertex alpha."""
	name = "M_DevCue"
	path = f"{DIR}/{name}"
	rb.delete_asset_if_exists(path)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	m = tools.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
	if m is None:
		rb.fail(f"could not create {path}")
	vc = _node(m, unreal.MaterialExpressionVertexColor, -600, 0)  # default output = RGB (float3), pin "A" = alpha
	slab = _node(m, unreal.MaterialExpressionSubstrateSlabBSDF, 0, 0)
	_link(vc, slab, "Diffuse Albedo")
	_link(_node(m, unreal.MaterialExpressionConstant, -300, 400, r=0.04), slab, "F0")
	_link(vc, slab, "Roughness", "A")
	if not MEL.connect_material_property(slab, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
		rb.fail("M_DevCue: could not connect the slab")
	MEL.recompile_material(m)
	unreal.EditorAssetLibrary.save_loaded_asset(m)
	return path


# --------------------------------------------------------------------------------------------------------------------
# Level
# --------------------------------------------------------------------------------------------------------------------

def _enum_value(enum_class, name: str):
	key = name.upper().replace("_", "")
	for attr in dir(enum_class):
		if attr.replace("_", "") == key:
			return getattr(enum_class, attr)
	rb.fail(f"{enum_class} has no member {name}")
	return None


def _box(center, size, material, label: str):
	actor = rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", center, scale=(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), label=label)
	actor.static_mesh_component.set_material(0, material)
	return actor


def build_level(scene: str, spec: dict, mats: dict, cue_material: str) -> None:
	path = level_path(scene)
	rb.new_level(path)
	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), label="Table")
	table.set_editor_property("preset", unreal.RbTablePreset.NINE_FOOT_PRO)
	table.set_editor_property("use_baked_meshes", True)
	table.set_editor_property("part_materials", [mats["Cloth"], mats["Cloth"], mats["Wood"], mats["Wood"], mats["Liner"], mats["Sight"], mats["Wood"]])
	table.rebuild_table()
	bed = table.get_bed_center_world()
	if abs(bed.z - BED) > 1e-3 or abs(bed.x) > 1e-3 or abs(bed.y) > 1e-3:
		rb.fail(f"unexpected bed centre {bed} (expected z = {BED} cm)")

	# Closed room 900 x 600 x 300 cm (Lumen bounce, something for the balls and the lacquer to reflect).
	_box((0, 0, -5), (900, 600, 10), mats["Floor"], "Floor")
	_box((0, 0, 305), (900, 600, 10), mats["Room"], "Ceiling")
	_box((455, 0, 150), (10, 620, 300), mats["Room"], "WallFoot")
	_box((-455, 0, 150), (10, 620, 300), mats["Room"], "WallHead")
	_box((0, 305, 150), (900, 10, 300), mats["Room"], "WallLeft")
	_box((0, -305, 150), (900, 10, 300), mats["Room"], "WallRight")
	if "wall" in spec:
		# The short-cue wall: 10 cm thick, 4 m wide, floor to ceiling, its face at core x = spec["wall"] (StaticMeshActor = BlockAll).
		face = world(spec["wall"], 0.0, 0.0)[0]
		_box((face - 5.0, 0.0, 150.0), (10.0, 400.0, 300.0), mats["Wall"], "ShortCueWall")

	# Table lamp: a diffuser the size of the bed, 1.1 m above the cloth, 4500 K (UE-8 builds the real WPA lamp).
	lamp = rb.spawn(unreal.RectLight, (0.0, 0.0, BED + 110.0), (-90.0, 0.0, 0.0), "Lamp")
	lc = lamp.light_component
	lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	lc.set_editor_property("intensity", 9000.0)
	lc.set_editor_property("source_width", 180.0)
	lc.set_editor_property("source_height", 90.0)
	lc.set_editor_property("barn_door_length", 0.0)
	lc.set_editor_property("use_temperature", True)
	lc.set_editor_property("temperature", 4500.0)
	lc.set_editor_property("attenuation_radius", 1200.0)
	room = rb.spawn(unreal.PointLight, (0.0, 0.0, 280.0), label="RoomLight")
	room.light_component.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	room.light_component.set_editor_property("intensity", 1500.0)
	room.light_component.set_editor_property("source_radius", 20.0)
	room.light_component.set_editor_property("attenuation_radius", 1500.0)

	ppv = rb.spawn(unreal.PostProcessVolume, (0, 0, 150), label="PostProcess")
	ppv.set_editor_property("unbound", True)
	settings = ppv.get_editor_property("settings")
	settings.set_editor_property("override_auto_exposure_method", True)
	settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
	settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
	settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
	settings.set_editor_property("override_auto_exposure_bias", True)
	settings.set_editor_property("auto_exposure_bias", -EV100)
	settings.set_editor_property("override_vignette_intensity", True)
	settings.set_editor_property("vignette_intensity", 0.0)
	ppv.set_editor_property("settings", settings)

	demo = rb.spawn(unreal.RbCueDemo, (0, 0, 0), label="CueDemo")
	demo.set_editor_property("table", table)
	d = spec["demo"]
	demo.set_editor_property("scene", _enum_value(unreal.RbCueDemoScene, d.get("scene", "ADDRESS")))
	demo.set_editor_property("cue_ball", unreal.Vector2D(*d["cue_ball"]))
	demo.set_editor_property("object_balls", [unreal.Vector2D(*b) for b in d.get("object_balls", [])])
	demo.set_editor_property("azimuth_deg", d.get("azimuth_deg", 0.0))
	demo.set_editor_property("elevation_deg", d.get("elevation_deg", 0.0))
	demo.set_editor_property("tip_back_cm", d.get("tip_back_cm", 0.5))
	demo.set_editor_property("lineup_spacing_cm", d.get("lineup_spacing_cm", 10.0))
	demo.set_editor_property("cue_length_override_cm", d.get("cue_length_override_cm", 0.0))
	if not spec.get("no_cue_material"):
		demo.set_editor_property("cue_material_override", unreal.load_asset(cue_material))

	for label, eye, target, fov in spec["cameras"]:
		eye_w = world(*eye)
		cam = rb.spawn(unreal.CameraActor, eye_w, rb.look_at_rotation(eye_w, world(*target)), cam_name(label))
		cam.tags = [cam_name(label)]
		cc = cam.camera_component
		cc.set_editor_property("field_of_view", fov)
		cc.set_editor_property("constrain_aspect_ratio", False)
	rb.spawn(unreal.PlayerStart, (400.0, 260.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")
	rb.save_current_level(path)
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		rb.fail(f"{path} was not written")


def main() -> None:
	if "--list" in sys.argv:
		for line in capture_commands():
			print(line)
			rb.log(line)
		return
	rb.ensure_dir(DIR)
	for preset in ("Playing19oz", "Break21oz", "Jump9oz", "House19oz"):
		if not unreal.EditorAssetLibrary.does_asset_exist(f"/Game/Generated/Cues/SM_Cue_{preset}"):
			rb.fail(f"baked cue {preset} missing: run rb_bake_cue.py first")
	if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Generated/Tables/NineFootPro/SM_Table_Bed"):
		rb.fail("baked 9-ft table missing: run rb_bake_table.py first")
	mats = {name: make_flat_material(f"MI_UE4_{name}", c) for name, c in COLORS.items()}
	cue_material = CUE_MATERIAL if unreal.EditorAssetLibrary.does_asset_exist(CUE_MATERIAL) else make_dev_cue_material()
	rb.log(f"cue material: {cue_material}")
	for scene, spec in SCENES.items():
		build_level(scene, spec, mats, cue_material)
	for line in capture_commands():
		rb.log(line)
	rb.log("UE-4 dev maps OK")


main()
