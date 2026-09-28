"""UE-3 dev maps (Docs/ue-architecture.md 10, 13): the generated materials on the baked 9-ft table in the real renderer.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_materials.py      # first: materials, MPC, textures
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue3.py
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue3.py -- --list    # prints every capture command
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue3.py -- --diff    # after the captures: cloth_occlusion_diff.png
(captures: PowerShell, or Git Bash with MSYS_NO_PATHCONV=1; strict mode = no --allow-shader-errors)

Levels (scratch, /Game/Dev is git-ignored):
  L_DevUE3_Lineup  ARbTable (baked 9-ft pro, M_Rb* part materials) in a closed room (M_RbRoomWall / M_RbRoomFloor) under a
                   rect lamp with an M_RbLampDiffuser panel; the 16 balls of a set in two rows on the cloth, number circles
                   facing the lineup camera (front: cue ball, 1-8; back: 9-15 + the dotted cue ball), plus a mirror pair
                   (8 ball + 12 ball whose number faces the 8's reflection point) for the hit-lit reflection check and a
                   row of spinning instances (fixed BallOmegaLocal, alpha 0 / 0.35 / 1 about the band axis, 1 about Y,
                   dotted cue ball) for the analytic rotation smear, and swatches of the materials no table part uses yet
                   (brass, leather, green cloth instance, cue: UE-4's baked cue mesh when it exists, else a square rod with
                   MI_RbCue_LocalSections) so every generated material goes through a strict capture.
                   Balls are static-mesh actors with scratch instances of M_RbBall (BallNumber / BallColor as ARbBallSet sets).
  L_DevUE3_Cloth   the same room and table with ARbBallRackDemo (a seeded 9-ball rack with random orientations, cue ball on
                   the head spot) - ARbBallSet drives MPC_RbBalls, so the analytic ball occlusion on the cloth is live.
  L_DevUE3_ClothNoOcc  the same with the cloth's BallOcclusion = 0 (scratch instance): --diff writes 8 x |on - off| of the
                   macro view, the occlusion's own contribution.
  L_DevUE3_Motion  the same rack broken at 9 m/s and frozen at t = FREEZE (playback seek + pause): the balls carry their
                   angular velocities (BallOmegaLocal), so the rotation smear of M_RbBall shows on the fast ones.
Cameras are plain CameraActors with a fixed exposure (ARbLookDevCamera is UE-8's).
"""

import math
import os
import struct
import sys
import zlib

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DEV = "/Game/Dev/UE3"
LINEUP_LEVEL = DEV + "/L_DevUE3_Lineup"
CLOTH_LEVEL = DEV + "/L_DevUE3_Cloth"
CLOTH_NO_OCC_LEVEL = DEV + "/L_DevUE3_ClothNoOcc"
MOTION_LEVEL = DEV + "/L_DevUE3_Motion"
FREEZE = 0.25               # [s] shot time of the frozen break (rotation smear check)
MAT = "/Game/Generated/Materials"
BALL_MESH = "/Game/Generated/Balls/SM_RbBall"
R = 2.8575                 # ball radius [cm]
BED = 76.5                 # 9-ft pro bed height [cm] (checked against the table below)
EV100 = 9.0                # fixed exposure of the dev captures (~1000 lux on the bed)

# WPA colours (linear) = ARbBallSet::DefaultBallColor (UE-2), index = number (9-15 use number - 8).
COLORS = [(0.78, 0.77, 0.74), (0.75, 0.50, 0.02), (0.02, 0.06, 0.38), (0.50, 0.03, 0.03), (0.13, 0.03, 0.24), (0.80, 0.20, 0.02),
	(0.02, 0.22, 0.07), (0.22, 0.03, 0.03), (0.025, 0.025, 0.025)]

MEL = unreal.MaterialEditingLibrary

# (label, level, eye (table-local UE cm, z above the bed), target (same) or None = straight down, horizontal fov, output name)
CAMERAS = [
	("UE3_Lineup", "lineup", (0.0, -118.0, 40.0), (0.0, 1.0, 2.0), 40.0, "ball_lineup"),
	("UE3_Stripes", "lineup", (22.5, -52.0, 16.0), (22.5, 9.0, 3.0), 30.0, "stripes_closeup"),
	("UE3_Solids", "lineup", (-18.0, -46.0, 13.0), (-18.0, -9.0, 3.0), 32.0, "solids_closeup"),
	("UE3_Reflection", "lineup", None, None, 26.0, "reflection_closeup"),   # computed from the mirror pair
	("UE3_Rail", "lineup", (-34.0, -22.0, 26.0), (0.0, -74.0, 2.0), 55.0, "rail_pocket"),
	("UE3_ClothGrazing", "cloth", (-92.0, 4.0, 16.0), (63.5, 0.0, 3.0), 79.3, "cloth_grazing"),
	("UE3_ClothMacro", "cloth", (-80.0, -11.0, 6.5), (-63.5, 0.0, 0.5), 38.0, "cloth_macro"),
	("UE3_ClothMacroNoOcc", "cloth_noocc", (-80.0, -11.0, 6.5), (-63.5, 0.0, 0.5), 38.0, "cloth_macro_no_occlusion"),
	("UE3_Overview", "cloth", (-230.0, -190.0, 115.0), (10.0, 0.0, -10.0), 50.0, "overview"),
	("UE3_Motion", "motion", (20.0, -55.0, 45.0), (75.0, 0.0, 0.0), 55.0, "motion_break"),
	("UE3_Smear", "lineup", (-85.0, -25.0, 16.0), (-85.0, 30.0, 3.0), 40.0, "rotation_smear"),
	("UE3_Sight", "lineup", (31.75, -60.0, 12.0), (31.75, -76.0, 4.0), 35.0, "sight_closeup"),
	("UE3_Swatches", "lineup", (-72.0, 0.0, 20.0), (-106.0, 0.0, 2.0), 62.0, "swatches"),
]

# Swatches of the materials no table part uses yet (so every generated material is rendered by a strict capture):
# (label, engine mesh, table-local centre [cm], scale, rotation (pitch, yaw, roll), material).
SWATCHES = [
	("SwatchBrass", "/Engine/BasicShapes/Sphere.Sphere", (-98.0, 0.0, 3.0), (0.06, 0.06, 0.06), (0.0, 0.0, 0.0), "M_RbBrass"),
	("SwatchLeather", "/Engine/BasicShapes/Sphere.Sphere", (-100.0, -12.0, 3.0), (0.06, 0.06, 0.06), (0.0, 0.0, 0.0), "M_RbLeather"),
	("SwatchGreenCloth", "/Engine/BasicShapes/Plane.Plane", (-116.0, 0.0, 0.05), (0.16, 0.30, 1.0), (0.0, 0.0, 0.0), "MI_RbCloth_Green"),
	("SwatchCue", "/Engine/BasicShapes/Cube.Cube", (-129.0, 0.0, 0.65), (0.30, 0.013, 0.013), (0.0, 90.0, 0.0), "MI_RbCue_LocalSections"),
]
# The real cue (UE-4 bake, UV1 = section index) replaces the cylinder swatch once it exists: lying on the cloth along +X behind
# the lineup, tip dome centre at CUE_TIP (table-local cm, z = tip radius), butt raised to its radius (r 6.5 -> 15.9 mm).
CUE_MESH = "/Game/Generated/Cues/SM_Cue_Playing19oz"
CUE_TIP = (74.0, 50.0, 0.65)
CUE_PITCH = -math.degrees(math.atan2(1.59 - 0.65, 147.3))

# Rotation-smear row (plan 4.6): scratch instances with a fixed ball-local spin, alpha = |w| T / (2 pi), T = 1/120 s.
# (number, spin axis in ball-local coordinates, alpha, dotted); the number circles face UE3_Smear.
SPIN_T = 1.0 / 120.0
SMEAR = [(11, (0.0, 0.0, 1.0), 0.0, False), (11, (0.0, 0.0, 1.0), 0.35, False), (11, (0.0, 0.0, 1.0), 1.0, False),
	(3, (0.0, 1.0, 0.0), 1.0, False), (0, (0.3, 1.0, 0.2), 1.0, True)]
SMEAR_ROW_Y = 30.0

MIRROR_BALL = (72.0, 24.0)      # the 8 ball of the mirror pair
MIRROR_SOURCE = (78.6, 24.0)    # the 12 ball beside it (0.9 cm gap)
MIRROR_EYE = (76.0, -12.0, 9.0)  # camera for UE3_Reflection


def fresh_level(path: str) -> None:
	"""Empty level at path. An existing level is replaced by saving a blank map over it (the level editor refuses to create a
	new level where the asset registry still lists the old one, and would log an error on every re-run)."""
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		rb.new_level(path)
		return
	world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
	if world is None or not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
		rb.fail(f"could not replace level {path}")


def world(p):
	return (p[0], p[1], BED + p[2])


def vec(p):
	return unreal.Vector(p[0], p[1], p[2])


def ball_instance(number: int, dotted: bool = False, omega=None, suffix: str = "") -> str:
	name = f"MI_UE3_Ball{number:02d}" + ("_Dotted" if dotted else "") + suffix
	path = f"{DEV}/{name}"
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mi = tools.create_asset(name, DEV, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	MEL.set_material_instance_parent(mi, unreal.load_asset(f"{MAT}/M_RbBall"))
	c = COLORS[number - 8 if number > 8 else number]
	MEL.set_material_instance_scalar_parameter_value(mi, "BallNumber", float(number))
	MEL.set_material_instance_vector_parameter_value(mi, "BallColor", unreal.LinearColor(c[0], c[1], c[2], 1.0))
	MEL.set_material_instance_scalar_parameter_value(mi, "CueBallDots", 1.0 if dotted else 0.0)
	w = omega or (0.0, 0.0, 0.0)
	MEL.set_material_instance_vector_parameter_value(mi, "BallOmegaLocal", unreal.LinearColor(w[0], w[1], w[2], 0.0))
	MEL.set_material_instance_scalar_parameter_value(mi, "ExposureTime", SPIN_T)
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	return path


def facing_rotation(center, toward):
	"""Rotation that points the ball's local +X (number circle) at `toward`, local +Z as close to world up as possible."""
	x = vec(toward) - vec(center)
	x = x.normal()
	up = unreal.Vector(0.0, 0.0, 1.0)
	z = up - x * up.dot(x)
	z = z.normal()
	rot = unreal.MathLibrary.make_rot_from_xz(x, z)
	return (rot.pitch, rot.yaw, rot.roll)


def spawn_ball(number: int, plan_xy, facing, dotted: bool = False, label: str | None = None, material: str | None = None):
	center = world((plan_xy[0], plan_xy[1], R))
	rot = facing_rotation(center, facing)
	actor = rb.spawn_mesh(BALL_MESH, center, scale=(R, R, R), rotation=rot, label=label or f"Ball{number:02d}",
		material_path=material or ball_instance(number, dotted))
	actor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	return actor


def _box(center, size, material: str, label: str, cast_shadow: bool = True):
	actor = rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", center, scale=(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), label=label,
		material_path=material)
	actor.static_mesh_component.set_editor_property("cast_shadow", cast_shadow)
	return actor


def build_room() -> None:
	"""Closed room 800 x 600 x 300 cm (Lumen bounce light, something to reflect), rect lamp 150 x 50 cm, 1 m above the bed."""
	wall, floor = f"{MAT}/M_RbRoomWall", f"{MAT}/M_RbRoomFloor"
	_box((0, 0, -5), (800, 600, 10), floor, "Floor")
	_box((0, 0, 305), (800, 600, 10), wall, "Ceiling")
	_box((405, 0, 150), (10, 620, 300), wall, "WallFoot")
	_box((-405, 0, 150), (10, 620, 300), wall, "WallHead")
	_box((0, 305, 150), (800, 10, 300), wall, "WallLeft")
	_box((0, -305, 150), (800, 10, 300), wall, "WallRight")
	lamp_z = BED + 100.0
	lamp = rb.spawn(unreal.RectLight, (0, 0, lamp_z), (-90.0, 0.0, 0.0), "Lamp")
	lc = lamp.light_component
	lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	lc.set_editor_property("intensity", 4500.0)
	lc.set_editor_property("source_width", 50.0)
	lc.set_editor_property("source_height", 150.0)
	lc.set_editor_property("barn_door_length", 0.0)
	lc.set_editor_property("use_temperature", True)
	lc.set_editor_property("temperature", 4000.0)
	lc.set_editor_property("attenuation_radius", 1200.0)
	lc.set_editor_property("contact_shadow_length", 0.03)
	# Diffuser panel just above the emitting plane (camera-only emissive) and a dark housing around it.
	_box((0, 0, lamp_z + 0.6), (150, 50, 1.0), f"{MAT}/M_RbLampDiffuser", "LampDiffuser", cast_shadow=False)
	_box((0, 0, lamp_z + 6.0), (158, 58, 10.0), f"{MAT}/M_RbCushionRubber", "LampHousingTop")
	sky = rb.spawn(unreal.SkyLight, (0, 0, 250), label="SkyLight")
	sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	sky.light_component.set_editor_property("intensity", 0.0)   # closed room: Lumen bounce only

	ppv = rb.spawn(unreal.PostProcessVolume, (0, 0, 150), label="PostProcess")
	ppv.set_editor_property("unbound", True)
	s = ppv.get_editor_property("settings")
	s.set_editor_property("override_auto_exposure_method", True)
	s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
	s.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
	s.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
	s.set_editor_property("override_auto_exposure_bias", True)
	s.set_editor_property("auto_exposure_bias", -EV100)
	s.set_editor_property("override_vignette_intensity", True)
	s.set_editor_property("vignette_intensity", 0.0)
	s.set_editor_property("override_motion_blur_amount", True)
	s.set_editor_property("motion_blur_amount", 0.0)
	ppv.set_editor_property("settings", s)


def build_table():
	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), label="Table")
	table.set_editor_property("preset", unreal.RbTablePreset.NINE_FOOT_PRO)
	table.set_editor_property("ball_set", unreal.RbBallSetPreset.STANDARD_POOL)
	table.set_editor_property("use_baked_meshes", True)
	bed = table.get_bed_center_world()
	if abs(bed.z - BED) > 1e-3 or abs(bed.x) > 1e-3 or abs(bed.y) > 1e-3:
		rb.fail(f"unexpected bed centre {bed} (expected (0, 0, {BED}))")
	return table


def reflection_camera():
	"""UE3_Reflection: looks at the 8 ball of the mirror pair; the 12 ball's number faces the 8's reflection point."""
	c8 = unreal.Vector(MIRROR_BALL[0], MIRROR_BALL[1], BED + R)
	eye = vec(world(MIRROR_EYE))
	return eye, c8


def mirror_pair() -> None:
	c8 = unreal.Vector(MIRROR_BALL[0], MIRROR_BALL[1], BED + R)
	c12 = unreal.Vector(MIRROR_SOURCE[0], MIRROR_SOURCE[1], BED + R)
	eye = vec(world(MIRROR_EYE))
	to_cam = (eye - c8).normal()
	to_src = (c12 - c8).normal()
	n = (to_cam + to_src).normal()                       # mirror normal at the reflection point
	point = c8 + n * R
	# The 8's own circles face up / down-front, so its black resin mirrors the 12 undisturbed.
	spawn_ball(8, MIRROR_BALL, (c8.x, c8.y + 6.0, c8.z + 10.0), label="Mirror08")
	spawn_ball(12, MIRROR_SOURCE, (point.x, point.y, point.z), label="Mirror12")


def smear_row() -> None:
	"""Balls with a fixed local spin (static meshes: the spin only drives the material's rotation smear)."""
	eye = next(world(c[2]) for c in CAMERAS if c[0] == "UE3_Smear")
	for i, (number, axis, alpha, dotted) in enumerate(SMEAR):
		n = math.sqrt(sum(a * a for a in axis))
		w = alpha * 2.0 * math.pi / SPIN_T
		omega = tuple(a / n * w for a in axis)
		suffix = f"_Spin{i}"
		x = -85.0 + 7.5 * (len(SMEAR) - 1) * 0.5 - 7.5 * i     # left to right as seen from UE3_Smear
		spawn_ball(number, (x, SMEAR_ROW_Y), eye, label=f"Smear{i}_{number:02d}", material=ball_instance(number, dotted, omega, suffix))


def build_lineup_level() -> None:
	fresh_level(LINEUP_LEVEL)
	build_table()
	build_room()
	eye = world(CAMERAS[0][2])
	# Front row: cue ball, 1..8; back row: 9..15 and the dotted cue ball, left to right as seen from the lineup camera
	# (looking along +Y, screen right = -X). Number circles face the camera.
	for i in range(9):
		x = 36.0 - 9.0 * i
		spawn_ball(i, (x, -9.0), eye)
	for i in range(8):
		x = 31.5 - 9.0 * i
		number = 9 + i if i < 7 else 0
		spawn_ball(number, (x, 9.0), eye, dotted=(i == 7), label=f"Ball{number:02d}" + ("_Dotted" if i == 7 else ""))
	mirror_pair()
	smear_row()
	has_cue = unreal.EditorAssetLibrary.does_asset_exist(CUE_MESH)
	for label, mesh, center, scale, rot, material in SWATCHES:
		if label == "SwatchCue" and has_cue:
			actor = rb.spawn_mesh(CUE_MESH, world(CUE_TIP), rotation=(CUE_PITCH, 0.0, 0.0), label="Cue", material_path=f"{MAT}/M_RbCue")
		else:
			actor = rb.spawn_mesh(mesh, world(center), scale=scale, rotation=rot, label=label, material_path=f"{MAT}/{material}")
		actor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	spawn_cameras("lineup")
	rb.spawn(unreal.PlayerStart, (-300.0, 200.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")
	rb.save_current_level(LINEUP_LEVEL)


def cloth_without_occlusion() -> str:
	"""Scratch instance of M_RbCloth with the analytic ball occlusion off (BallOcclusion = 0) for the A/B capture."""
	name = "MI_UE3_Cloth_NoOcclusion"
	path = f"{DEV}/{name}"
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mi = tools.create_asset(name, DEV, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	MEL.set_material_instance_parent(mi, unreal.load_asset(f"{MAT}/M_RbCloth"))
	MEL.set_material_instance_scalar_parameter_value(mi, "BallOcclusion", 0.0)
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	return path


def build_rack_level(path: str, cameras: str, play_break: bool, occlusion: bool = True) -> None:
	fresh_level(path)
	table = build_table()
	if not occlusion:
		# ERbTablePart order: Bed, CushionCloth, RailCaps, Apron, PocketLiners, Sights, Legs.
		parts = [unreal.load_asset(p) for p in (cloth_without_occlusion(), cloth_without_occlusion(), f"{MAT}/M_RbRailWood",
			f"{MAT}/M_RbRailWood", f"{MAT}/M_RbPocketLiner", f"{MAT}/M_RbSight", f"{MAT}/M_RbRailWood")]
		table.set_editor_property("part_materials", parts)
	build_room()
	demo = rb.spawn(unreal.RbBallRackDemo, (0, 0, 0), label="BallRack")
	demo.set_editor_property("table", table)
	demo.set_editor_property("discipline", unreal.RbDiscipline.NINE_BALL)
	demo.set_editor_property("rack_seed", 11)
	demo.set_editor_property("random_orientations", True)
	if play_break:
		demo.set_editor_property("play_break", True)
		demo.set_editor_property("break_speed", 9.0)
		demo.set_editor_property("freeze_at_shot_time", FREEZE)
	spawn_cameras(cameras)
	rb.spawn(unreal.PlayerStart, (-300.0, 200.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")
	rb.save_current_level(path)


def spawn_cameras(level: str) -> None:
	for label, lvl, eye, target, fov, _ in CAMERAS:
		if lvl != level:
			continue
		if label == "UE3_Reflection":
			e, t = reflection_camera()
			eye_w, rot = (e.x, e.y, e.z), rb.look_at_rotation((e.x, e.y, e.z), (t.x, t.y, t.z))
		else:
			eye_w = world(eye)
			rot = rb.look_at_rotation(eye_w, world(target)) if target is not None else (-90.0, -90.0, 0.0)
		cam = rb.spawn(unreal.CameraActor, eye_w, rot, label)
		cam.tags = [label]
		cc = cam.camera_component
		cc.set_editor_property("field_of_view", fov)
		cc.set_editor_property("constrain_aspect_ratio", False)


def read_png_rgb(path: str) -> tuple:
	"""8-bit RGB / RGBA PNG (non-interlaced, as the headless capture writes) -> (width, height, bytearray RGB)."""
	with open(path, "rb") as f:
		data = f.read()
	if data[:8] != b"\x89PNG\r\n\x1a\n":
		rb.fail(f"{path} is not a PNG")
	pos, idat, width, height, ctype = 8, bytearray(), 0, 0, 0
	while pos < len(data):
		length, tag = struct.unpack(">I4s", data[pos:pos + 8])
		body = data[pos + 8:pos + 8 + length]
		if tag == b"IHDR":
			width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
			if depth != 8 or ctype not in (2, 6) or interlace:
				rb.fail(f"{path}: unsupported PNG format (depth {depth}, colour type {ctype}, interlace {interlace})")
		elif tag == b"IDAT":
			idat += body
		pos += 12 + length
	channels = 3 if ctype == 2 else 4
	raw = zlib.decompress(bytes(idat))
	stride = width * channels
	prev = bytearray(stride)
	out = bytearray(width * height * 3)
	p = 0
	for y in range(height):
		ftype = raw[p]
		row = bytearray(raw[p + 1:p + 1 + stride])
		p += 1 + stride
		if ftype == 1:
			for i in range(channels, stride):
				row[i] = (row[i] + row[i - channels]) & 0xFF
		elif ftype == 2:
			for i in range(stride):
				row[i] = (row[i] + prev[i]) & 0xFF
		elif ftype == 3:
			for i in range(stride):
				left = row[i - channels] if i >= channels else 0
				row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
		elif ftype == 4:
			for i in range(stride):
				a = row[i - channels] if i >= channels else 0
				b = prev[i]
				c = prev[i - channels] if i >= channels else 0
				pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
				pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
				row[i] = (row[i] + pred) & 0xFF
		if channels == 3:
			out[y * width * 3:(y + 1) * width * 3] = row
		else:
			rgb = bytearray(width * 3)
			rgb[0::3], rgb[1::3], rgb[2::3] = row[0::4], row[1::4], row[2::4]
			out[y * width * 3:(y + 1) * width * 3] = rgb
		prev = row
	return width, height, out


def write_png_rgb(path: str, width: int, height: int, rgb: bytearray) -> None:
	raw = bytearray()
	for y in range(height):
		raw.append(0)
		raw.extend(rgb[y * width * 3:(y + 1) * width * 3])

	def chunk(tag: bytes, body: bytes) -> bytes:
		return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

	with open(path, "wb") as f:
		f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
			+ chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def occlusion_diff() -> None:
	"""Docs/images/dev/UE-3/cloth_occlusion_diff.png = 8 x |cloth_macro - cloth_macro_no_occlusion| (the analytic ball
	occlusion's contribution: a soft ring of darkened indirect light around each contact point)."""
	folder = os.path.join(unreal.Paths.project_dir(), "Docs", "images", "dev", "UE-3")
	w0, h0, a = read_png_rgb(os.path.join(folder, "cloth_macro.png"))
	w1, h1, b = read_png_rgb(os.path.join(folder, "cloth_macro_no_occlusion.png"))
	if (w0, h0) != (w1, h1):
		rb.fail("cloth_macro and cloth_macro_no_occlusion differ in size")
	diff = bytearray(min(255, 8 * abs(x - y)) for x, y in zip(a, b))
	write_png_rgb(os.path.join(folder, "cloth_occlusion_diff.png"), w0, h0, diff)
	rb.log(f"cloth_occlusion_diff.png: mean |difference| {sum(abs(x - y) for x, y in zip(a, b)) / len(a):.3f} (8-bit)")


def main() -> None:
	levels = {"lineup": LINEUP_LEVEL, "cloth": CLOTH_LEVEL, "cloth_noocc": CLOTH_NO_OCC_LEVEL, "motion": MOTION_LEVEL}
	if "--list" in sys.argv:
		for label, lvl, _, _, _, out in CAMERAS:
			folder = "Docs/images/ue3" if out in ("ball_lineup", "cloth_grazing") else "Docs/images/dev/UE-3"
			print(f"python Tools/unreal/rbue.py capture --map {levels[lvl]} --camera {label} --out {folder}/{out}.png")
		return
	if "--diff" in sys.argv:
		occlusion_diff()
		return
	rb.ensure_dir(DEV)
	for path in (f"{MAT}/M_RbBall", f"{MAT}/M_RbCloth", f"{MAT}/MPC_RbBalls", BALL_MESH, "/Game/Generated/Tables/NineFootPro/SM_Table_Bed"):
		if not unreal.EditorAssetLibrary.does_asset_exist(path):
			rb.fail(f"{path} missing: run rb_make_materials.py (and the UE-1 / UE-2 bakes) first")
	build_lineup_level()
	build_rack_level(CLOTH_LEVEL, "cloth", play_break=False)
	build_rack_level(CLOTH_NO_OCC_LEVEL, "cloth_noocc", play_break=False, occlusion=False)
	build_rack_level(MOTION_LEVEL, "motion", play_break=True)
	rb.log("UE-3 dev maps OK")


main()
