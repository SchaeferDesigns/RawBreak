"""UE-7 dev map (Docs/ue-architecture.md 10, 11): the info overlay and the replay cameras in the running game.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue7.py
  python Tools/unreal/editor/rb_dev_ue7.py --list          (host side: prints the capture commands of Docs/images/dev/UE-7/)
  python Tools/unreal/editor/rb_dev_ue7.py --capture [name ...]   (host side: runs them, all when no name is given)
(PowerShell, or Git Bash with MSYS_NO_PATHCONV=1.)

The level /Game/Dev/UE7/L_UE7_Match (scratch, git-ignored) runs ARbGameMode (World Settings) with a placed ARbTable (9-ft pro,
baked meshes, flat dev materials) and a placed ARbBallSet (dev ball material with stripe band and number circles until UE-3's
M_RbBall exists), a small closed room, a WPA-style rect lamp 1 m above the bed (= ARbTable::LampUndersideHeight, so the
Overhead replay camera sits just below it) and a fixed exposure. The match is driven by the URbCheatManager commands through
-ExecCmds (they queue behind a shot in flight, so one batch plays several shots); the screenshots are taken by the headless
capture through
  UE7_Player       a standing player at the head end (overlay screenshots; rb.Overlay.InScreenshots 1 puts the Slate overlay
                   into the capture)
  RbReplayCamera   the replay camera itself (tag of every ARbReplayCamera), for the four replay views
"""

import os
import sys

LEVEL = "/Game/Dev/UE7/L_UE7_Match"
OUT = "Docs/images/dev/UE-7"
BREAK = "rb.Match.Break 9"  # UE-6b dev command: cue ball behind the head string, 9 m/s at the apex ball
FOULS = ("rb.Match.Layout 0 -0.8 -0.3 1 0.6 0.3 9 0.7 -0.3, RbStrike 0.4 180, RbPlaceCueBall -0.8 0.3, RbStrike 0.4 180, "
	"RbPlaceCueBall -0.8 -0.3, RbStrike 0.4 180, RbPlaceCueBall -0.8 0.3, RbStrike 0.4 180")

# (png, map URL options, camera, exec commands)
CAPTURES = [
	("overlay_pinned_debug", "?Mode=HotSeat?Seed=12?Rate=0", "UE7_Player", f"rb.Overlay.InScreenshots 1, RbOverlay 2, {BREAK}"),
	("overlay_hidden_mandatory", "?Mode=HotSeat?Seed=12?Rate=0", "UE7_Player", f"rb.Overlay.InScreenshots 1, RbOverlay 0, {FOULS}"),
	# Seed 2: the scripted break is legal (push-out window) - the values RawBreak.Functional.Replay logs ("legal break: ...").
	("overlay_decision", "?Mode=HotSeat?Seed=2?Rate=0", "UE7_Player",
		"rb.Overlay.InScreenshots 1, RbPlaceCueBall -0.754999995 0.0799999982, RbStrike 9 -3.5465219 0 0 -0.1, RbDeclare 1, "
		"RbStrike 0.35 -35.3238182 0 0 0"),
	("replay_shooter", "?Mode=HotSeat?Seed=12?Rate=0", "RbReplayCamera", f"{BREAK}, RbReplay 0 0 0.12"),
	("replay_overhead", "?Mode=HotSeat?Seed=12?Rate=0", "RbReplayCamera", f"{BREAK}, RbReplay 1 0 0.35"),
	("replay_rail", "?Mode=HotSeat?Seed=12?Rate=0", "RbReplayCamera", f"rb.Overlay.InScreenshots 1, {BREAK}, RbReplay 2 0 0.35"),
	("replay_follow", "?Mode=HotSeat?Seed=12?Rate=0", "RbReplayCamera", f"{BREAK}, RbReplay 3 0 0.8"),
]


def capture_args(name: str, options: str, camera: str, cmds: str) -> list:
	args = ["capture", "--map", f"{LEVEL}{options}", "--camera", camera, "--exec-cmds", cmds, "--out", f"{OUT}/{name}.png"]
	if name == "overlay_hidden_mandatory":
		args += ["--warmup-seconds", "9"]  # the post-shot auto-glance (4 s) is over: Hidden mode
	return args


def host_main() -> int:
	"""Host side: --list prints the capture commands, --capture [names] runs them through rbue.py (all when no name)."""
	if "--capture" not in sys.argv:
		for spec in CAPTURES:
			print("python Tools/unreal/rbue.py " + " ".join(f'"{a}"' if " " in a or "?" in a else a for a in capture_args(*spec)))
		return 0
	import subprocess
	repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
	wanted = [a for a in sys.argv[sys.argv.index("--capture") + 1:] if not a.startswith("--")]
	failed = 0
	for spec in CAPTURES:
		if wanted and spec[0] not in wanted:
			continue
		code = subprocess.call([sys.executable, os.path.join(repo, "Tools", "unreal", "rbue.py")] + capture_args(*spec), cwd=repo)
		print(f"[ue7] {spec[0]}: {'OK' if code == 0 else 'FAILED'}", flush=True)
		failed += 1 if code else 0
	return 1 if failed else 0


try:
	import unreal
except ImportError:  # host side (plain Python)
	unreal = None
if unreal is None or "--list" in sys.argv:
	sys.exit(host_main())

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DIR = "/Game/Dev/UE7"
BED_Z = 76.5          # cm (NineFootPro TableSpec::BedHeight; checked against the table below)
BALL_MATERIAL = "/Game/Generated/Materials/M_RbBall"  # RbAssetPaths::MatBall (UE-3)
EV100 = 9.0           # manual exposure (~900 lux on the cloth under the lamp)
MEL = unreal.MaterialEditingLibrary

COLORS = {
	"Cloth": (0.03, 0.16, 0.07),
	"Wood": (0.20, 0.07, 0.025),
	"Liner": (0.015, 0.012, 0.01),
	"Sight": (0.85, 0.83, 0.78),
	"Floor": (0.10, 0.085, 0.07),
	"Room": (0.36, 0.35, 0.33),
}


def make_dev_instance(name: str, color: tuple) -> str:
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
	return path


# --- dev ball material (same layout as UE-2's M_DevBall: ball-local position, no UV seams) ----------------------------

def _node(material, cls, x: int, y: int, **props):
	expression = MEL.create_material_expression(material, cls, x, y)
	for key, value in props.items():
		expression.set_editor_property(key, value)
	return expression


def _link(src, dst, pin: str = "", src_pin: str = "") -> None:
	if not MEL.connect_material_expressions(src, src_pin, dst, pin):
		rb.fail(f"could not connect {src.get_name()} -> {dst.get_name()}.{pin}")


def _if(material, a, const_b: float, greater, less, x: int, y: int):
	node = _node(material, unreal.MaterialExpressionIf, x, y, const_b=const_b)
	_link(a, node, "A")
	_link(greater, node, "A > B")
	_link(less, node, "A < B")
	return node


def make_dev_ball_material() -> str:
	path = f"{DIR}/M_UE7_DevBall"
	rb.delete_asset_if_exists(path)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	m = tools.create_asset("M_UE7_DevBall", DIR, unreal.Material, unreal.MaterialFactoryNew())
	if m is None:
		rb.fail(f"could not create {path}")
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
	base = _if(m, number, 8.5, white, color, -1100, 400)
	band = _if(m, az, 0.5878, base, color, -900, 400)
	striped = _if(m, number, 8.5, band, base, -700, 400)
	bar_z = _if(m, az, 0.13, white, dark, -900, 700)
	bar = _if(m, ay, 0.035, white, bar_z, -700, 700)
	circle = _if(m, ax, 0.9336, bar, striped, -500, 400)
	max_xy = _node(m, unreal.MaterialExpressionMax, -1100, 1300)
	_link(ax, max_xy, "A")
	_link(ay, max_xy, "B")
	max_xyz = _node(m, unreal.MaterialExpressionMax, -900, 1300)
	_link(max_xy, max_xyz, "A")
	_link(az, max_xyz, "B")
	cue = _if(m, max_xyz, 0.978, red, color, -700, 1100)
	albedo = _if(m, number, 0.5, circle, cue, -200, 400)
	slab = _node(m, unreal.MaterialExpressionSubstrateSlabBSDF, 400, 0)
	_link(albedo, slab, "Diffuse Albedo")
	_link(_node(m, unreal.MaterialExpressionConstant, 200, 200, r=0.049), slab, "F0")
	_link(_node(m, unreal.MaterialExpressionConstant, 200, 300, r=0.06), slab, "Roughness")
	if not MEL.connect_material_property(slab, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
		rb.fail("M_UE7_DevBall: could not connect the slab")
	MEL.recompile_material(m)
	unreal.EditorAssetLibrary.save_loaded_asset(m)
	return path


def fresh_level(path: str) -> unreal.World:
	"""An empty level at path (a re-run loads the map and removes its actors: new_level cannot replace a map here)."""
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


def box(center, size, material: str, label: str):
	return rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", center, scale=(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), label=label,
		material_path=material)


def main() -> None:
	rb.ensure_dir(DIR)
	if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Generated/Tables/NineFootPro/SM_Table_Bed"):
		rb.fail("baked 9-ft table missing: run rb_bake_table.py first")
	mats = {name: make_dev_instance(f"MI_UE7_{name}", c) for name, c in COLORS.items()}
	ball_material = BALL_MATERIAL if unreal.EditorAssetLibrary.does_asset_exist(BALL_MATERIAL) else make_dev_ball_material()
	world = fresh_level(LEVEL)
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbGameMode)

	# The table (found by ARbGameMode) with flat dev materials per ERbTablePart; the lamp height is the default 1.0 m.
	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), label="Table")
	table.set_editor_property("preset", unreal.RbTablePreset.NINE_FOOT_PRO)
	table.set_editor_property("use_baked_meshes", True)
	part = [mats["Cloth"], mats["Cloth"], mats["Wood"], mats["Wood"], mats["Liner"], mats["Sight"], mats["Wood"]]
	table.set_editor_property("part_materials", [unreal.load_asset(p) for p in part])
	table.rebuild_table()
	bed = table.get_bed_center_world()
	if abs(bed.z - BED_Z) > 1e-3:
		rb.fail(f"unexpected bed height {bed.z} cm")
	lamp_m = table.get_editor_property("lamp_underside_height")

	# The ball set (found by ARbGameMode, which calls InitForTable) with the dev ball material.
	balls = rb.spawn(unreal.RbBallSet, (0.0, 0.0, 0.0), label="Balls")
	balls.set_editor_property("ball_material_override", unreal.load_asset(ball_material))

	# Room 760 x 540 x 300 cm, closed (Lumen bounce, reflections), leaves 1.9 m behind the head rail for the rail camera.
	box((0, 0, -5), (760, 540, 10), mats["Floor"], "Floor")
	box((0, 0, 305), (760, 540, 10), mats["Room"], "Ceiling")
	box((385, 0, 150), (10, 560, 300), mats["Room"], "WallFoot")
	box((-385, 0, 150), (10, 560, 300), mats["Room"], "WallHead")
	box((0, 275, 150), (760, 10, 300), mats["Room"], "WallLeft")
	box((0, -275, 150), (760, 10, 300), mats["Room"], "WallRight")

	# WPA-style lamp: rect source 150 x 50 cm at the lamp underside, 4000 K (UE-8 builds the real one).
	lamp = rb.spawn(unreal.RectLight, (0.0, 0.0, BED_Z + 100.0 * lamp_m), (-90.0, 0.0, 0.0), "Lamp")
	lc = lamp.light_component
	lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	lc.set_editor_property("intensity", 5000.0)
	lc.set_editor_property("source_width", 50.0)
	lc.set_editor_property("source_height", 160.0)
	lc.set_editor_property("barn_door_length", 0.0)
	lc.set_editor_property("use_temperature", True)
	lc.set_editor_property("temperature", 4000.0)
	lc.set_editor_property("attenuation_radius", 1200.0)
	fill = rb.spawn(unreal.RectLight, (0.0, 0.0, 295.0), (-90.0, 0.0, 0.0), "RoomFill")
	fc = fill.light_component
	fc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	fc.set_editor_property("intensity", 1500.0)
	fc.set_editor_property("source_width", 500.0)
	fc.set_editor_property("source_height", 700.0)
	fc.set_editor_property("attenuation_radius", 1200.0)

	ppv = rb.spawn(unreal.PostProcessVolume, (0.0, 0.0, 150.0), label="PostProcess")
	ppv.set_editor_property("unbound", True)
	pps = ppv.get_editor_property("settings")
	pps.set_editor_property("override_auto_exposure_method", True)
	pps.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
	pps.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
	pps.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
	pps.set_editor_property("override_auto_exposure_bias", True)
	pps.set_editor_property("auto_exposure_bias", -EV100)
	pps.set_editor_property("override_vignette_intensity", True)
	pps.set_editor_property("vignette_intensity", 0.0)
	ppv.set_editor_property("settings", pps)

	# A standing player at the head end, slightly left of the centre line, looking down the table (the overlay screenshots): eye 1.65 m,
	# 16:9 horizontal FOV of the Eyes preset's 50 deg vertical = 79.3 deg.
	eye = (-235.0, -45.0, 165.0)
	cam = rb.spawn(unreal.CameraActor, eye, rb.look_at_rotation(eye, (45.0, 0.0, BED_Z - 10.0)), "UE7_Player")
	cam.tags = ["UE7_Player"]
	cam.camera_component.set_editor_property("field_of_view", 79.3)
	cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
	rb.spawn(unreal.PlayerStart, (-250.0, 0.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log(f"UE-7 dev map OK: {LEVEL} (ball material {ball_material})")


main()
