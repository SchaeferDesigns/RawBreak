"""UE-5b dev map (Docs/ue-architecture.md 10, 11): the first-person pawn and its camera rig in the real renderer.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue5b.py     # inside Unreal: builds /Game/Dev/UE5b/L_UE5b_Pawn
  python Tools/unreal/editor/rb_dev_ue5b.py --capture [<name> ...]      # on the host: the review captures through rbue.py
  python Tools/unreal/editor/rb_dev_ue5b.py --list [<name> ...]         # on the host: prints the capture commands
The captures view through the PAWN's camera (no --camera) and go to Docs/images/dev/UE-5b/<name>.png; each one is
  python Tools/unreal/rbue.py capture --map "/Game/Dev/UE5b/L_UE5b_Pawn?Seed=5" --warmup-seconds 8 --exec-cmds "<commands>" --out ...
(from PowerShell, or with MSYS_NO_PATHCONV=1). rb.Player.Dump -1 logs the rig state every second (EV, focus, lens, pose).

The level: a closed 8 x 6 x 3 m room (Lumen bounce, dim ceiling lights), the baked 9-ft table (ARbTable, flat dev colours on the
engine's BasicShapeMaterial - UE-3 owns the real materials), a pool lamp (rect light 150 x 60 cm, 1 m above the bed, 4000 K) under a
dark shade, a PlayerStart at the head end and ARbGameMode in the World Settings: the game mode spawns the ball set, the cue and the
pawn and starts 9-ball practice (the break: ball in hand behind the head string).
NO post-process volume and no fixed exposure: every capture shows the camera model of the pawn's rig (Eyes by default: V = 50 deg,
pupil DoF, histogram auto exposure EV100 2..11 with the centre-weighted metering mask, 108 deg shutter).
The rb.Match.* commands (UE-6b) place the cue ball; the rb.Player.* commands (UE-5b) put the pawn somewhere, aim, get down, look,
switch the preset; rb.Player.StandInCue 1 shows a two-part cylinder stand-in for the cue until UE-4's cue mesh exists.
Scratch content under /Game/Dev (git-ignored); the script is idempotent.
"""

import os
import subprocess
import sys

DIR = "/Game/Dev/UE5b"
LEVEL = f"{DIR}/L_UE5b_Pawn"
BED = 76.5  # cm: only for the sanity check of the table; the table itself comes from rb::TableSpec at runtime

COLORS = {
	"Cloth": (0.03, 0.16, 0.07),
	"Wood": (0.20, 0.07, 0.025),
	"Liner": (0.015, 0.012, 0.01),
	"Sight": (0.85, 0.83, 0.78),
	"Floor": (0.10, 0.075, 0.055),
	"Wall": (0.36, 0.34, 0.31),
	"Ceiling": (0.45, 0.45, 0.44),
	"Shade": (0.02, 0.025, 0.02),
}

# Head end of the table at x = -127 cm (core -x); the cue ball placed behind the head string; the 9-ball diamond on the foot spot.
PLACE = "rb.Match.Place -0.80 0.10"
STAND = "rb.Player.Teleport -1.85 0.10 0 -22"
AIM = "rb.Player.AimAt 0.635 0.0"
DOWN = f"{PLACE}, {STAND}, rb.Player.StandInCue 1, {AIM}, rb.Player.GetDown"
CAPTURES = {
	# name: (commands, resolution)
	# Walking around: the standing eye (1.65 m) at the head end, looking down the table (cue ball placed).
	"standing": (f"{PLACE}, {STAND}, rb.Player.StandInCue 1, rb.Player.Dump -1", "1920x1080"),
	# Ball in hand (the break): leaning over the head rail to place the cue ball (the view ray on the cloth is the placement point).
	"ball_in_hand": ("rb.Player.Teleport -1.85 0.05 0 -38, rb.Player.Dump -1", "1920x1080"),
	# Down on the shot, aimed at the 9: chin over the cue, the eyes fixate the first ball on the line (the 1), the shaft is soft.
	"down_on_shot": (f"{DOWN}, rb.Player.Dump -1", "1920x1080"),
	# The same with the eyes raised along the line (look input while aiming): the lamp shade comes into view.
	"down_look_up": (f"{DOWN}, rb.Player.Look 0 8, rb.Player.Dump -1", "1920x1080"),
	# Headcam preset (V = 58.7 deg, 1.1 mm aperture = deep focus, fast AE, CA, vignette) down on the same shot.
	"headcam_down": (f"rb.Player.Preset Headcam, {DOWN}, rb.Player.Dump -1", "1920x1080"),
	# Standing at the head end, turned away from the table toward a corner of the dim room: the auto exposure opens up (EV ~4 vs
	# ~7.3 on the table), the adaptation you see when you stand up and look around (plan 4.4).
	"exposure_room": (f"{PLACE}, rb.Player.Teleport -2.2 1.2 150 -12, rb.Player.Dump -1", "1920x1080"),
	# 21:9 (64:27): the same vertical FOV, more to the sides (Hor+, MaintainYFOV).
	"down_on_shot_21x9": (f"{DOWN}, rb.Player.Dump -1", "2560x1080"),
	# Still down after a (scripted) break, the head turned 50 deg to the right to follow the balls: the eyes focus on what they look
	# at (review fix - the focus used to stay on the fixation plane of the aim line, which blurred the whole turned view).
	"down_watch_turned": (f"{DOWN}, rb.Match.Break 9, rb.Player.Look 50 -6, rb.Player.Dump -1", "1920x1080"),
}


def capture_args(name: str) -> list:
	cmds, res = CAPTURES[name]
	return ["Tools/unreal/rbue.py", "capture", "--map", f"{LEVEL}?Seed=5", "--res", res, "--warmup-seconds", "8", "--exec-cmds", cmds, "--out",
		f"Docs/images/dev/UE-5b/{name}.png"]


def host_main() -> int:
	names = [a for a in sys.argv[1:] if not a.startswith("--")] or list(CAPTURES)
	if "--list" in sys.argv:
		for name in names:
			print("python " + " ".join(f'"{a}"' if (" " in a or "?" in a) else a for a in capture_args(name)))
		return 0
	repo = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
	env = dict(os.environ, PYTHONIOENCODING="utf-8", MSYS_NO_PATHCONV="1")
	failed = 0
	for name in names:
		code = subprocess.call([sys.executable] + capture_args(name), cwd=repo, env=env)
		print(f"[ue5b] {name}: {'OK' if code == 0 else 'FAILED'}")
		failed += 1 if code != 0 else 0
	return 1 if failed else 0


if "--list" in sys.argv or "--capture" in sys.argv:
	sys.exit(host_main())

import unreal  # noqa: E402  (inside Unreal from here on)

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


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
	unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
	unreal.MaterialEditingLibrary.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi)
	return mi


def fresh_level(path: str) -> unreal.World:
	"""An empty level at path (a re-run loads the map and removes its actors: the commandlet cannot replace an open map)."""
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


def box(center, size, material, label: str):
	return rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", center, scale=(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), label=label,
		material_path=material.get_path_name())


def rect_light(label: str, location, width: float, height: float, lumens: float, kelvin: float):
	light = rb.spawn(unreal.RectLight, location, (-90.0, 0.0, 0.0), label)
	lc = light.light_component
	lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	lc.set_editor_property("intensity", lumens)
	lc.set_editor_property("source_width", width)
	lc.set_editor_property("source_height", height)
	lc.set_editor_property("barn_door_length", 0.0)
	lc.set_editor_property("use_temperature", True)
	lc.set_editor_property("temperature", kelvin)
	lc.set_editor_property("attenuation_radius", 1500.0)
	return light


def main() -> None:
	if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Generated/Tables/NineFootPro/SM_Table_Bed"):
		rb.fail("baked 9-ft table missing: run rb_bake_table.py first")
	rb.ensure_dir(DIR)
	m = {name: make_dev_material(f"MI_UE5b_{name}", c) for name, c in COLORS.items()}
	world = fresh_level(LEVEL)
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbGameMode)

	# Room 800 x 600 x 300 cm around the table (closed: Lumen bounce light, something behind the player).
	box((0, 0, -5), (800, 600, 10), m["Floor"], "Floor")
	box((0, 0, 305), (800, 600, 10), m["Ceiling"], "Ceiling")
	box((405, 0, 150), (10, 620, 300), m["Wall"], "WallFoot")
	box((-405, 0, 150), (10, 620, 300), m["Wall"], "WallHead")
	box((0, 305, 150), (800, 10, 300), m["Wall"], "WallLeft")
	box((0, -305, 150), (800, 10, 300), m["Wall"], "WallRight")

	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "Table")
	table.set_editor_property("preset", unreal.RbTablePreset.NINE_FOOT_PRO)
	table.set_editor_property("use_baked_meshes", True)
	table.set_editor_property("part_materials", [m["Cloth"], m["Cloth"], m["Wood"], m["Wood"], m["Liner"], m["Sight"], m["Wood"]])
	table.set_editor_property("lamp_underside_height", 1.0)
	table.rebuild_table()
	bed = table.get_bed_center_world()
	if abs(bed.z - BED) > 3.0:
		rb.fail(f"unexpected bed height {bed.z} cm")

	# Pool lamp: rect source 150 x 60 cm, underside 1.0 m above the bed, under a dark shade; dim ceiling lights for the room.
	rect_light("TableLamp", (0.0, 0.0, bed.z + 100.0), 150.0, 60.0, 3500.0, 4000.0)
	box((0.0, 0.0, bed.z + 109.0), (170.0, 80.0, 16.0), m["Shade"], "LampShade")
	rect_light("CeilingLight1", (-220.0, 0.0, 298.0), 60.0, 60.0, 450.0, 3200.0)
	rect_light("CeilingLight2", (220.0, 0.0, 298.0), 60.0, 60.0, 450.0, 3200.0)

	# The pawn starts at the head end, facing the foot (UE +X).
	rb.spawn(unreal.PlayerStart, (-190.0, 0.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log(f"UE-5b dev map OK: {LEVEL}")


main()
