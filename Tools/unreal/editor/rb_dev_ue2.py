"""UE-2 dev maps (Docs/ue-architecture.md 10, 13): balls & playback on the 9-ft table in the real renderer.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue2.py
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE2/L_DevUE2_Rack  --camera UE2_Rack      --out Docs/images/dev/UE-2/rack.png
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE2/L_DevUE2_Rack  --camera UE2_RackClose --out Docs/images/dev/UE-2/rack_closeup.png
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE2/L_DevUE2_Rack  --camera UE2_Overhead  --out Docs/images/dev/UE-2/rack_overhead.png
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE2/L_DevUE2_Break --camera UE2_Overhead  --out Docs/images/dev/UE-2/break_overhead.png
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE2/L_DevUE2_Break --camera UE2_Low       --out Docs/images/dev/UE-2/break_low.png
(PowerShell, or Git Bash with MSYS_NO_PATHCONV=1.)

Levels (scratch, /Game/Dev is git-ignored):
  L_DevUE2_Rack    ARbTable (9-ft pro) + ARbBallRackDemo: a seeded 8-ball rack (rules GenerateRack, wooden-rack gaps, random
                   ball orientations) and the cue ball on the head spot, shown by ARbBallSet at BeginPlay.
  L_DevUE2_Break   the same rack, a 9 m/s break simulated at BeginPlay and played back by URbShotPlaybackComponent, frozen at
                   t = FREEZE s (seek + pause) - positions and orientations of a moving shot.
The table meshes are UE-1's; this script adds a stand-in cloth plane, rails and a small closed room with one rect lamp
so the balls have context. Ball material: UE-3's M_RbBall when it exists, else the dev material M_DevBall below (WPA
colour, stripe band |z| < sin 36 deg, white number circles at the local +-x poles with a dark bar as a number stand-in,
a red-dotted cue ball so its rotation is visible) - all from the ball-local position, like the real material.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DEV = "/Game/Dev/UE2"
RACK_LEVEL = DEV + "/L_DevUE2_Rack"
BREAK_LEVEL = DEV + "/L_DevUE2_Break"
BALL_MATERIAL = "/Game/Generated/Materials/M_RbBall"  # RbAssetPaths::MatBall (UE-3)
FREEZE = 0.30        # [s] shot time of the frozen break
RACK_SEED = 7
EV100 = 9.0         # manual exposure of the dev captures (~900 lux on the cloth under the lamp)

MEL = unreal.MaterialEditingLibrary


# --------------------------------------------------------------------------------------------------------------------
# Dev materials (Substrate slab; the project runs Substrate, Docs/ue-architecture.md 2.2)
# --------------------------------------------------------------------------------------------------------------------

def _new_material(name: str) -> unreal.Material:
	path = f"{DEV}/{name}"
	rb.delete_asset_if_exists(path)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	material = tools.create_asset(name, DEV, unreal.Material, unreal.MaterialFactoryNew())
	if material is None:
		rb.fail(f"could not create {path}")
	return material


def _node(material, cls, x: int, y: int, **props):
	expression = MEL.create_material_expression(material, cls, x, y)
	for key, value in props.items():
		expression.set_editor_property(key, value)
	return expression


def _link(src, dst, pin: str = "", src_pin: str = "") -> None:
	if not MEL.connect_material_expressions(src, src_pin, dst, pin):
		rb.fail(f"could not connect {src.get_name()} -> {dst.get_name()}.{pin}")


def _slab(material, albedo, f0: float, roughness: float):
	slab = _node(material, unreal.MaterialExpressionSubstrateSlabBSDF, 400, 0)
	_link(albedo, slab, "Diffuse Albedo")
	_link(_node(material, unreal.MaterialExpressionConstant, 200, 200, r=f0), slab, "F0")
	_link(_node(material, unreal.MaterialExpressionConstant, 200, 300, r=roughness), slab, "Roughness")
	if not MEL.connect_material_property(slab, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
		rb.fail(f"{material.get_name()}: could not connect the slab")
	return slab


def _finish(material) -> None:
	MEL.recompile_material(material)
	unreal.EditorAssetLibrary.save_loaded_asset(material)


def make_surface_material(name: str, albedo, roughness: float, f0: float = 0.04) -> str:
	material = _new_material(name)
	color = _node(material, unreal.MaterialExpressionConstant3Vector, 0, 0, constant=unreal.LinearColor(*albedo, 1.0))
	_slab(material, color, f0, roughness)
	_finish(material)
	return f"{DEV}/{name}"


def _if(material, a, const_b: float, greater, less, x: int, y: int):
	"""If(A, B = const_b): A > B -> greater (also A == B), A < B -> less."""
	node = _node(material, unreal.MaterialExpressionIf, x, y, const_b=const_b)
	_link(a, node, "A")
	_link(greater, node, "A > B")
	_link(less, node, "A < B")
	return node


def make_dev_ball_material() -> str:
	m = _new_material("M_DevBall")
	# Ball-local unit direction (the mesh is a unit sphere scaled per ball): layout without UV seams.
	local = _node(m, unreal.MaterialExpressionLocalPosition, -2000, 0)
	n = _node(m, unreal.MaterialExpressionNormalize, -1800, 0)
	_link(local, n, "VectorInput")
	comps = []
	for i, channel in enumerate(("r", "g", "b")):
		mask = _node(m, unreal.MaterialExpressionComponentMask, -1600, -200 + 200 * i, r=channel == "r", g=channel == "g", b=channel == "b", a=False)
		_link(n, mask)
		absolute = _node(m, unreal.MaterialExpressionAbs, -1400, -200 + 200 * i)
		_link(mask, absolute)
		comps.append(absolute)
	ax, ay, az = comps
	number = _node(m, unreal.MaterialExpressionScalarParameter, -1600, 600, parameter_name="BallNumber", default_value=1.0)
	color_param = _node(m, unreal.MaterialExpressionVectorParameter, -1600, 800, parameter_name="BallColor",
		default_value=unreal.LinearColor(0.75, 0.50, 0.02, 1.0))
	color = _node(m, unreal.MaterialExpressionComponentMask, -1400, 800, r=True, g=True, b=True, a=False)
	_link(color_param, color)
	white = _node(m, unreal.MaterialExpressionConstant3Vector, -1400, 1000, constant=unreal.LinearColor(0.80, 0.79, 0.76, 1.0))
	dark = _node(m, unreal.MaterialExpressionConstant3Vector, -1400, 1100, constant=unreal.LinearColor(0.02, 0.02, 0.02, 1.0))
	red = _node(m, unreal.MaterialExpressionConstant3Vector, -1400, 1200, constant=unreal.LinearColor(0.55, 0.02, 0.02, 1.0))

	base = _if(m, number, 8.5, white, color, -1100, 400)            # stripes: white ball
	band = _if(m, az, 0.5878, base, color, -900, 400)                # |z| < sin 36 deg: coloured band
	striped = _if(m, number, 8.5, band, base, -700, 400)             # only 9..15 get the band
	bar_z = _if(m, az, 0.13, white, dark, -900, 700)                 # number stand-in: a short dark bar along local z
	bar = _if(m, ay, 0.035, white, bar_z, -700, 700)
	circle = _if(m, ax, 0.9336, bar, striped, -500, 400)             # number circles at the local +-x poles (21 deg)
	max_xy = _node(m, unreal.MaterialExpressionMax, -1100, 1300)
	_link(ax, max_xy, "A")
	_link(ay, max_xy, "B")
	max_xyz = _node(m, unreal.MaterialExpressionMax, -900, 1300)
	_link(max_xy, max_xyz, "A")
	_link(az, max_xyz, "B")
	cue = _if(m, max_xyz, 0.978, red, color, -700, 1100)             # dotted cue ball: 6 red dots on the local axes
	albedo = _if(m, number, 0.5, circle, cue, -200, 400)
	_slab(m, albedo, 0.049, 0.06)                                    # phenolic resin F0 (plan 6.2), glossy
	_finish(m)
	return f"{DEV}/M_DevBall"


# --------------------------------------------------------------------------------------------------------------------
# Level
# --------------------------------------------------------------------------------------------------------------------

def _box(center, size, material: str, label: str):
	"""Engine cube (100 cm) scaled to size [cm] at center."""
	return rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", center, scale=(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), label=label,
		material_path=material)


def _camera(label: str, eye, target=None, rotation=None, fov: float = 40.0):
	rot = rotation if rotation is not None else rb.look_at_rotation(eye, target)
	cam = rb.spawn(unreal.CameraActor, eye, rot, label)
	cam.tags = [label]
	cam.camera_component.set_editor_property("field_of_view", fov)
	return cam


def build_level(path: str, materials: dict, ball_material: str | None, play_break: bool) -> None:
	rb.new_level(path)
	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), label="Table")
	table.set_editor_property("preset", unreal.RbTablePreset.NINE_FOOT_PRO)
	table.set_editor_property("ball_set", unreal.RbBallSetPreset.STANDARD_POOL)
	bed = table.get_bed_center_world()
	z = bed.z
	if abs(z - 76.5) > 5.0:
		rb.fail(f"unexpected bed height {z} cm")

	# Room 700 x 500 x 300 cm, closed (Lumen bounce light, something to reflect in the balls).
	_box((0, 0, -5), (700, 500, 10), materials["floor"], "Floor")
	_box((0, 0, 305), (700, 500, 10), materials["room"], "Ceiling")
	_box((355, 0, 150), (10, 520, 300), materials["room"], "WallFoot")
	_box((-355, 0, 150), (10, 520, 300), materials["room"], "WallHead")
	_box((0, 255, 150), (700, 10, 300), materials["room"], "WallLeft")
	_box((0, -255, 150), (700, 10, 300), materials["room"], "WallRight")
	# Stand-in table while ARbTable has no meshes yet (UE-1 builds the real ones from rb::TableGeometry): cloth plane, body,
	# rails with the nose at the bed edge. Skipped as soon as the table builds its own part components (no overlap).
	if table.get_components_by_class(unreal.PrimitiveComponent):
		rb.log("ARbTable builds its own meshes - no stand-in table")
	else:
		rb.spawn_mesh("/Engine/BasicShapes/Plane.Plane", (0, 0, z - 0.05), scale=(2.54, 1.27, 1.0), label="DevCloth", material_path=materials["cloth"])
		_box((0, 0, z - 20.0), (284, 157, 39), materials["rail"], "DevBody")
		_box((0, 71.0, z + 1.75), (284, 15, 5.5), materials["rail"], "DevRailL")
		_box((0, -71.0, z + 1.75), (284, 15, 5.5), materials["rail"], "DevRailR")
		_box((134.5, 0, z + 1.75), (15, 127, 5.5), materials["rail"], "DevRailFoot")
		_box((-134.5, 0, z + 1.75), (15, 127, 5.5), materials["rail"], "DevRailHead")

	# WPA-style lamp: rect source 150 x 50 cm, 1 m above the bed, 4000 K (plan 6.1; UE-8 builds the real one).
	lamp = rb.spawn(unreal.RectLight, (0, 0, z + 100.0), (-90.0, 0.0, 0.0), "Lamp")
	lc = lamp.light_component
	lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	lc.set_editor_property("intensity", 4000.0)
	lc.set_editor_property("source_width", 50.0)
	lc.set_editor_property("source_height", 150.0)
	lc.set_editor_property("use_temperature", True)
	lc.set_editor_property("temperature", 4000.0)
	lc.set_editor_property("attenuation_radius", 1000.0)

	ppv = rb.spawn(unreal.PostProcessVolume, (0, 0, 150), label="PostProcess")
	ppv.set_editor_property("unbound", True)
	settings = ppv.get_editor_property("settings")
	settings.set_editor_property("override_auto_exposure_method", True)
	settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
	settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
	settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
	settings.set_editor_property("override_auto_exposure_bias", True)
	settings.set_editor_property("auto_exposure_bias", -EV100)  # manual: exposure = 2^bias / 1.2 -> EV100 = -bias
	ppv.set_editor_property("settings", settings)

	demo = rb.spawn(unreal.RbBallRackDemo, (0, 0, 0), label="BallRack")
	demo.set_editor_property("table", table)
	demo.set_editor_property("discipline", unreal.RbDiscipline.EIGHT_BALL)
	demo.set_editor_property("rack_seed", RACK_SEED)
	demo.set_editor_property("random_orientations", True)
	if ball_material:
		demo.set_editor_property("ball_material_override", unreal.load_asset(ball_material))
	if play_break:
		demo.set_editor_property("play_break", True)
		demo.set_editor_property("break_speed", 9.0)
		demo.set_editor_property("freeze_at_shot_time", FREEZE)

	# Capture cameras (rbue.py capture --camera <tag>). Table-local UE axes: +X foot, +Y = core -y (right of the shooter).
	_camera("UE2_Rack", (-215.0, 0.0, z + 40.0), (70.0, 0.0, z), fov=40.0)
	_camera("UE2_RackClose", (22.0, -36.0, z + 15.0), (75.0, 0.0, z + 2.0), fov=40.0)
	_camera("UE2_Overhead", (0.0, 0.0, z + 215.0), rotation=(-90.0, -90.0, 0.0), fov=70.0)  # below the 300 cm ceiling
	_camera("UE2_Low", (-150.0, -95.0, z + 55.0), (55.0, 0.0, z), fov=45.0)
	rb.spawn(unreal.PlayerStart, (-300.0, 200.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	rb.save_current_level(path)
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		rb.fail(f"{path} was not written")


def main() -> None:
	rb.ensure_dir(DEV)
	rb.delete_asset_if_exists(DEV + "/Probe/M_Probe")
	materials = {
		"cloth": make_surface_material("M_DevCloth", (0.04, 0.20, 0.07), 0.85),
		"rail": make_surface_material("M_DevRail", (0.10, 0.045, 0.02), 0.35),
		"room": make_surface_material("M_DevRoom", (0.40, 0.39, 0.37), 0.8),
		"floor": make_surface_material("M_DevFloor", (0.12, 0.10, 0.09), 0.6),
	}
	ball_material = None if unreal.EditorAssetLibrary.does_asset_exist(BALL_MATERIAL) else make_dev_ball_material()
	rb.log(f"ball material: {ball_material or BALL_MATERIAL}")
	build_level(RACK_LEVEL, materials, ball_material, play_break=False)
	build_level(BREAK_LEVEL, materials, ball_material, play_break=True)
	rb.log("UE-2 dev maps OK")


main()
