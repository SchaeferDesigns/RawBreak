"""UE-1 dev map: the baked 9-ft pro table and the 7-ft bar table, with capture cameras for the geometry screenshots.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_table.py        # first: the baked table meshes
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue1.py
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE1/L_UE1_Tables --camera UE1_Overhead --out Docs/images/ue1/overhead.png
  (every camera: see CAMERAS below; Tools/unreal/editor/rb_dev_ue1.py --list prints the capture commands)

Scratch content (git-ignored /Game/Dev/UE1): the level, flat dev material instances of the engine's BasicShapeMaterial (cloth
green, rail wood, dark liner, pearl sights) - the real Substrate materials are UE-3's. The tables are ARbTable actors: their
part components are transient, so the saved level holds only the actors and BeginPlay builds the tables from the baked
/Game/Generated/Tables/<Preset>/SM_Table_* meshes (the -game capture exercises exactly the M1 path). Plain CameraActors:
ARbLookDevCamera (UE-8) is for the look-dev / acceptance captures of the finished room.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DIR = "/Game/Dev/UE1"
LEVEL = f"{DIR}/L_UE1_Tables"

# Table placements (UE cm): the 9-ft pro table at the origin, the 7-ft bar table beside it.
NINE = {"preset": unreal.RbTablePreset.NINE_FOOT_PRO, "origin": (0.0, 0.0, 0.0), "bed": 76.5, "hl": 127.0, "hw": 63.5}
BAR = {"preset": unreal.RbTablePreset.SEVEN_FOOT_BAR, "origin": (0.0, 520.0, 0.0), "bed": 74.3, "hl": 101.6, "hw": 50.8}

# Linear base colours of the dev materials per ERbTablePart (Bed, CushionCloth, RailCaps, Apron, PocketLiners, Sights, Legs).
COLORS = {
	"Cloth": (0.03, 0.16, 0.07),
	"Wood": (0.20, 0.07, 0.025),
	"Liner": (0.015, 0.012, 0.01),
	"Sight": (0.85, 0.83, 0.78),
	"Floor": (0.18, 0.18, 0.18),
	"BarCloth": (0.02, 0.06, 0.20),
	"BarWood": (0.05, 0.035, 0.025),
}


def cam_name(label: str) -> str:
	return f"UE1_{label}"


def table_point(t: dict, x: float, y: float, z: float) -> tuple:
	"""Table-local UE cm (origin = bed centre on the cloth) -> world."""
	ox, oy, oz = t["origin"]
	return (ox + x, oy + y, oz + t["bed"] + z)


def cameras() -> list:
	"""(label, eye, target, horizontal fov deg). UE axes: +X = foot rail, +Y = core -y (right seen from the head)."""
	n, b = NINE, BAR
	out = []
	# Overheads: straight down (yaw -90: image right = +X foot, image up = core +y).
	out.append(("Overhead", table_point(n, 0.0, 0.0, 900.0), None, 21.0))
	out.append(("Bar_Overhead", table_point(b, 0.0, 0.0, 900.0), None, 17.5))
	# Corner pocket (foot, core +y = UE -Y), seen from the table like a player leaning over it.
	out.append(("PocketCloseup", table_point(n, n["hl"] - 42.0, -n["hw"] + 34.0, 30.0), table_point(n, n["hl"] + 2.0, -n["hw"] - 2.0, -2.0), 50.0))
	out.append(("Bar_PocketCloseup", table_point(b, b["hl"] - 42.0, -b["hw"] + 34.0, 30.0), table_point(b, b["hl"] + 2.0, -b["hw"] - 2.0, -2.0), 50.0))
	# Side pocket (core +y), from the table.
	out.append(("SidePocket", table_point(n, -14.0, -n["hw"] + 42.0, 28.0), table_point(n, 0.0, -n["hw"] - 4.0, -2.0), 50.0))
	out.append(("Bar_SidePocket", table_point(b, -14.0, -b["hw"] + 42.0, 28.0), table_point(b, 0.0, -b["hw"] - 4.0, -2.0), 50.0))
	# Chin on the cue: grazing view from behind the head string toward the foot rail.
	out.append(("ChinOnCue", table_point(n, -95.0, 8.0, 11.0), table_point(n, n["hl"], 0.0, 2.0), 55.0))
	# Pocket mouth from the bed (cut angle, jaws, facings, shelf, drop rounding, liner collar).
	out.append(("PocketMouth", table_point(n, n["hl"] - 12.0, -n["hw"] + 12.0, 9.0), table_point(n, n["hl"] + 6.0, -n["hw"] - 6.0, -4.0), 60.0))
	out.append(("Bar_PocketMouth", table_point(b, b["hl"] - 12.0, -b["hw"] + 12.0, 9.0), table_point(b, b["hl"] + 6.0, -b["hw"] - 6.0, -4.0), 60.0))
	# Three-quarter views: rails, apron, legs / cabinet.
	out.append(("ThreeQuarter", table_point(n, -290.0, 250.0, 110.0), table_point(n, 0.0, 0.0, -25.0), 45.0))
	out.append(("Bar_ThreeQuarter", table_point(b, -240.0, 230.0, 100.0), table_point(b, 0.0, 0.0, -25.0), 45.0))
	return out


def make_dev_material(name: str, color: tuple) -> unreal.MaterialInstanceConstant:
	path = f"{DIR}/{name}"
	rb.delete_asset_if_exists(path)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	mi = tools.create_asset(name, DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	if mi is None:
		rb.fail(f"could not create {path}")
	parent = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
	if parent is None:
		rb.fail("missing /Engine/BasicShapes/BasicShapeMaterial")
	unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
	names = [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(parent)]
	if "Color" not in names:
		rb.fail(f"BasicShapeMaterial has no Color parameter ({names})")
	unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
	unreal.MaterialEditingLibrary.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi)
	return mi


def spawn_table(t: dict, label: str, mats: list) -> unreal.Actor:
	table = rb.spawn(unreal.RbTable, t["origin"], (0.0, 0.0, 0.0), label)
	table.set_editor_property("preset", t["preset"])
	table.set_editor_property("use_baked_meshes", True)
	table.set_editor_property("part_materials", mats)
	table.rebuild_table()
	return table


def main() -> None:
	if "--list" in sys.argv:
		for label, _, _, _ in cameras():
			print(f"python Tools/unreal/rbue.py capture --map {LEVEL} --camera {cam_name(label)} --out Docs/images/dev/ue1/{label.lower()}.png")
		return
	rb.ensure_dir(DIR)
	for preset in ("NineFootPro", "SevenFootBar"):
		if not unreal.EditorAssetLibrary.does_asset_exist(f"/Game/Generated/Tables/{preset}/SM_Table_Bed"):
			rb.fail(f"baked table {preset} missing: run rb_bake_table.py first")
	m = {name: make_dev_material(f"MI_UE1_{name}", c) for name, c in COLORS.items()}
	rb.new_level(LEVEL)

	# Floor (engine plane 1 m, x40) and a dim sky + a low sun for readable shapes; a pool-lamp rect light over each table.
	rb.spawn_mesh("/Engine/BasicShapes/Plane.Plane", (0.0, 260.0, 0.0), scale=(40.0, 40.0, 1.0), label="Floor", material_path=f"{DIR}/MI_UE1_Floor")
	sun = rb.spawn(unreal.DirectionalLight, (0.0, 0.0, 600.0), (-38.0, 25.0, 0.0), "Sun")
	sun.light_component.set_editor_property("intensity", 6.0)  # lux
	sun.light_component.set_editor_property("atmosphere_sun_light", True)
	rb.spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), label="SkyAtmosphere")
	sky = rb.spawn(unreal.SkyLight, (0.0, 0.0, 400.0), label="SkyLight")
	sky.light_component.set_editor_property("real_time_capture", True)
	sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	for t, name in ((NINE, "Lamp9ft"), (BAR, "LampBar")):
		# A diffuser as large as the table, 1.3 m up: even light (~800 lux in the centre) to judge the geometry; the real WPA
		# lamp is UE-8's.
		lamp = rb.spawn(unreal.RectLight, table_point(t, 0.0, 0.0, 130.0), (-90.0, 0.0, 0.0), name)
		lc = lamp.light_component
		lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
		lc.set_editor_property("intensity", 12000.0)
		lc.set_editor_property("source_width", 2.0 * (t["hl"] + 18.0))
		lc.set_editor_property("source_height", 2.0 * (t["hw"] + 18.0))
		lc.set_editor_property("barn_door_length", 0.0)
		lc.set_editor_property("temperature", 4500.0)
		lc.set_editor_property("use_temperature", True)
		lc.set_editor_property("attenuation_radius", 1000.0)

	# Fixed exposure (EV100 = 8.5) so every capture of the geometry is exposed alike (auto exposure over the dark floor blew
	# the bed out).
	ppv = rb.spawn(unreal.PostProcessVolume, (0.0, 0.0, 0.0), label="PostProcess")
	ppv.set_editor_property("unbound", True)
	pps = ppv.get_editor_property("settings")
	pps.set_editor_property("override_auto_exposure_min_brightness", True)
	pps.set_editor_property("auto_exposure_min_brightness", 8.5)
	pps.set_editor_property("override_auto_exposure_max_brightness", True)
	pps.set_editor_property("auto_exposure_max_brightness", 8.5)
	pps.set_editor_property("override_auto_exposure_bias", True)
	pps.set_editor_property("auto_exposure_bias", 0.0)
	pps.set_editor_property("override_vignette_intensity", True)
	pps.set_editor_property("vignette_intensity", 0.0)
	ppv.set_editor_property("settings", pps)

	nine_mats = [m["Cloth"], m["Cloth"], m["Wood"], m["Wood"], m["Liner"], m["Sight"], m["Wood"]]
	bar_mats = [m["BarCloth"], m["BarCloth"], m["BarWood"], m["BarWood"], m["Liner"], m["Sight"], m["BarWood"]]
	spawn_table(NINE, "Table_NineFootPro", nine_mats)
	spawn_table(BAR, "Table_SevenFootBar", bar_mats)

	for label, eye, target, fov in cameras():
		if target is None:
			rot = (-90.0, -90.0, 0.0)
		else:
			rot = rb.look_at_rotation(eye, target)
		cam = rb.spawn(unreal.CameraActor, eye, rot, cam_name(label))
		cam.tags = [cam_name(label)]
		cc = cam.camera_component
		cc.set_editor_property("field_of_view", fov)
		cc.set_editor_property("constrain_aspect_ratio", False)
	rb.spawn(unreal.PlayerStart, table_point(NINE, -200.0, 0.0, 90.0), (0.0, 0.0, 0.0), "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log(f"UE-1 dev map OK: {LEVEL} ({len(cameras())} cameras)")


main()
