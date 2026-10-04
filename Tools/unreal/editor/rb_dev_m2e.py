"""M2-E dev level (Docs/ue-architecture.md 18.6.2 item 5): /Game/Dev/M2E/L_TwoTables - two tables in one room.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2e.py

Used by RawBreak.Functional.MultiTable / LooseBall (M2-E) and by M2-C / M2-D for their multi-table checks (UX-T25):
  * World Settings: GameMode = ARbGameMode (one session per table; the tagged table plays the match of the URL options);
  * a 9-ft pro table (TableIndex 0) and a 7-ft coin-op bar table (TableIndex 1, SevenFootBar + OldBarOversizedCue, venue condition
    DiveBar, tagged RbPlayerTable) with different yaws, each under its own lamp (rect light + diffuser box; the table's
    LampUndersideHeight and, for the bar table, the lamp footprint of venue-dive-bar E14 feed the off-table apex check);
  * a closed room (engine cubes, static): floor PM_RbSurface_Vct, walls PM_RbSurface_Concrete, a wood kick plate
    PM_RbSurface_Wood (rb_make_physics.py), two procedural bar stools beside the 7-ft (loose balls roll between their legs),
    dim warm ambient light, the test room's post-process baseline (local exposure, low bloom, white point = lamp + 200 K);
  * an RbBallReturn trigger volume in a corner (a ball resting there goes back automatically);
  * PlayerStart at the 7-ft's head end; ARbLookDevCameras (Eyes preset): RbCam_M2E_Overview (both tables),
    RbCam_M2E_LooseBall (low view along the kick plate beside the 7-ft where the capture shot's ball comes to rest, StoolA's
    legs and the 7-ft's cabinet behind), RbCam_M2E_LooseBallEye (the same spot from a player standing at the 7-ft's +X rail).
Captures (Docs/images/dev/m2e/): the capture shot jumps the oversized cue ball over the 7-ft's +X rail (core +y); it bounces on the
VCT, rolls under StoolA and rests against the kick plate (RbLooseBalls logs where: about (613, 80, 3) cm, resting).
  python Tools/unreal/rbue.py capture --map /Game/Dev/M2E/L_TwoTables --camera RbCam_M2E_LooseBall --warmup-seconds 18
      --exec-cmds "RbPlaybackRate 1, RbPlaceCueBall -0.70 0.16, RbStrike 4 90 15 0 0, RbWait 14, RbLooseBalls"
      --out Docs/images/dev/m2e/loose_ball_floor.png
  (the same with --camera RbCam_M2E_LooseBallEye -> loose_ball_eye.png; --camera RbCam_M2E_Overview without --exec-cmds ->
  two_tables.png: both racks, each on its own table)
Scratch content under /Game/Dev (git-ignored); idempotent. Owner: M2-E.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DIR = "/Game/Dev/M2E"
LEVEL = f"{DIR}/L_TwoTables"
CUBE = "/Engine/BasicShapes/Cube.Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"

ROOM_X = (-620.0, 620.0)
ROOM_Y = (-460.0, 460.0)
ROOM_H = 300.0
WALL = 20.0

# name, preset, ball set, TableIndex, location (cm), yaw (deg), lamp underside above the cloth (m), player table, venue condition
TABLES = [
	("Table_9ft", "NINE_FOOT_PRO", "STANDARD_POOL", 0, (-300.0, -130.0, 0.0), 0.0, 1.016, False, False),
	("Table_7ft", "SEVEN_FOOT_BAR", "OLD_BAR_OVERSIZED_CUE", 1, (300.0, 150.0, 0.0), 90.0, 0.86, True, True),
]
BED = {"NINE_FOOT_PRO": (127.0, 63.5), "SEVEN_FOOT_BAR": (99.0, 49.5)}  # half length / half width of the bed [cm] (lamp size)
AMBIENT_LUMENS = 9000.0  # each of the three warm wall washers (room fill, see main)
LAMP_TEMPERATURE_K = 3600.0  # table lamps; the post-process white point follows them (see main)


def fresh_level(path: str) -> unreal.World:
	"""An empty level at path (a re-run loads the map and removes its actors: the commandlet cannot replace a map)."""
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


def instance(name: str, parent: str, vectors: dict, scalars: dict, fallback: str | None = None) -> unreal.MaterialInstanceConstant:
	"""A material instance under DIR. fallback: (parent, vectors, scalars) of M_RbRoomWall when parent does not exist (the table
	family's materials belong to M2-L and may be renamed; the dev room then still builds)."""
	path = f"{DIR}/{name}"
	rb.delete_asset_if_exists(path)
	if not unreal.EditorAssetLibrary.does_asset_exist(parent) and fallback is not None:
		rb.log(f"{name}: {parent} missing, falling back to {fallback}")
		parent = fallback
		vectors = {"Albedo": next(iter(vectors.values()), (0.05, 0.05, 0.05))}
		scalars = {"Roughness": scalars.get("Roughness", 0.5)}
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	mi = tools.create_asset(name, DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	parent_asset = unreal.load_asset(parent)
	if mi is None or parent_asset is None:
		rb.fail(f"could not create {path} (parent {parent})")
	mel = unreal.MaterialEditingLibrary
	mel.set_material_instance_parent(mi, parent_asset)
	for key, value in vectors.items():
		mel.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(value[0], value[1], value[2], 1.0))
	for key, value in scalars.items():
		mel.set_material_instance_scalar_parameter_value(mi, key, value)
	mel.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi)
	return mi


def box(label: str, center, size, material=None, physical: str | None = None, collision: bool = True) -> unreal.StaticMeshActor:
	actor = rb.spawn_mesh(CUBE, center, scale=(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), label=label)
	comp = actor.static_mesh_component
	comp.set_mobility(unreal.ComponentMobility.STATIC)
	if material is not None:
		comp.set_material(0, material)
	if physical:
		pm = unreal.load_asset(physical)
		if pm is None:
			rb.fail(f"missing {physical}: run rb_make_physics.py first")
		comp.set_phys_material_override(pm)
	if not collision:
		comp.set_collision_profile_name("NoCollision")
	return actor


def cylinder(label: str, center, radius: float, height: float, material) -> unreal.StaticMeshActor:
	actor = rb.spawn_mesh(CYLINDER, center, scale=(radius / 50.0, radius / 50.0, height / 100.0), label=label)
	actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
	actor.static_mesh_component.set_material(0, material)
	return actor


def stool(label: str, x: float, y: float, seat_mat, steel_mat) -> None:
	"""A bar stool stand-in (venue-dive-bar H10 proportions): 36 cm seat at 76 cm, four splayed legs are simplified to four
	vertical 2.2 cm tubes on a 40 cm footprint with a foot ring - what a loose ball meets on the floor."""
	cylinder(f"{label}_Seat", (x, y, 73.0), 18.0, 6.0, seat_mat)
	for i in range(4):
		a = math.radians(45.0 + 90.0 * i)
		cylinder(f"{label}_Leg{i}", (x + 17.0 * math.cos(a), y + 17.0 * math.sin(a), 35.0), 1.1, 70.0, steel_mat)
	# foot ring as four bars between the legs, 28 cm up (a ball rolls under it)
	for i in range(4):
		a0 = math.radians(45.0 + 90.0 * i)
		a1 = math.radians(135.0 + 90.0 * i)
		mx = 0.5 * 17.0 * (math.cos(a0) + math.cos(a1))
		my = 0.5 * 17.0 * (math.sin(a0) + math.sin(a1))
		length = 17.0 * math.sqrt(2.0)
		bar = box(f"{label}_Ring{i}", (x + mx, y + my, 28.0), (length, 1.6, 1.6), steel_mat)
		bar.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(math.atan2(my, mx)) + 90.0), False)


def table_point(location, yaw_deg: float, local_x: float, local_y: float, z: float):
	"""World point of a table-local UE offset (cm) for a table at location with yaw."""
	c = math.cos(math.radians(yaw_deg))
	s = math.sin(math.radians(yaw_deg))
	return (location[0] + c * local_x - s * local_y, location[1] + s * local_x + c * local_y, z)


def look_dev_camera(tag: str, eye, target, focus_cm: float = 0.0) -> None:
	cam = rb.spawn(unreal.RbLookDevCamera, eye, rb.look_at_rotation(eye, target), tag)
	cam.tags = [tag]
	cam.set_editor_property("preset", unreal.RbCameraPreset.EYES)
	cam.set_editor_property("focus_distance_cm", focus_cm)


def main() -> None:
	for preset in ("NineFootPro", "SevenFootBar"):
		if not unreal.EditorAssetLibrary.does_asset_exist(f"/Game/Generated/Tables/{preset}/SM_Table_Bed"):
			rb.fail(f"baked table {preset} missing: run rb_bake_table.py first")
	rb.ensure_dir(DIR)
	world = fresh_level(LEVEL)
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbGameMode)

	# Worn beige VCT, dark green-grey painted block, dark stained wood kick plate, oxblood vinyl seats (the leather slab with its
	# pebbled grain), chrome stool legs (the metal slab with a steel F0): the ball has to read against them in the captures.
	wall = "/Game/Generated/Materials/M_RbRoomWall"
	floor_mat = instance("MI_M2E_FloorVct", "/Game/Generated/Materials/M_RbRoomFloor", {"Albedo": (0.150, 0.132, 0.110)}, {"Roughness": 0.38})
	wall_mat = instance("MI_M2E_Wall", wall, {"Albedo": (0.060, 0.072, 0.064)}, {"Roughness": 0.85})
	wood_mat = instance("MI_M2E_KickWood", wall, {"Albedo": (0.030, 0.015, 0.008)}, {"Roughness": 0.30})
	ceiling_mat = instance("MI_M2E_Ceiling", wall, {"Albedo": (0.03, 0.028, 0.026)}, {"Roughness": 0.9})
	seat_mat = instance("MI_M2E_Vinyl", "/Game/Generated/Materials/M_RbLeather", {"LeatherColor": (0.11, 0.014, 0.012)},
		{"Roughness": 0.38, "GrainStrength": 0.06}, fallback=wall)
	steel_mat = instance("MI_M2E_Steel", "/Game/Generated/Materials/M_RbBrass", {"F0": (0.56, 0.57, 0.58)}, {"Roughness": 0.16},
		fallback=wall)
	housing_mat = instance("MI_M2E_LampHousing", "/Game/Generated/Materials/M_RbRoomWall", {"Albedo": (0.02, 0.07, 0.04)}, {"Roughness": 0.4})
	diffuser_mat = instance("MI_M2E_Diffuser", "/Game/Generated/Materials/M_RbLampDiffuser", {}, {"Luminance": 1800.0})

	# Room: floor top at z = 0 (VCT), walls and ceiling (concrete block).
	cx = 0.5 * (ROOM_X[0] + ROOM_X[1])
	cy = 0.5 * (ROOM_Y[0] + ROOM_Y[1])
	sx = ROOM_X[1] - ROOM_X[0]
	sy = ROOM_Y[1] - ROOM_Y[0]
	box("Floor", (cx, cy, -0.5 * WALL), (sx + 2 * WALL, sy + 2 * WALL, WALL), floor_mat, "/Game/Generated/Physics/PM_RbSurface_Vct")
	box("Ceiling", (cx, cy, ROOM_H + 0.5 * WALL), (sx + 2 * WALL, sy + 2 * WALL, WALL), ceiling_mat)
	concrete = "/Game/Generated/Physics/PM_RbSurface_Concrete"
	box("WallEast", (ROOM_X[1] + 0.5 * WALL, cy, 0.5 * ROOM_H), (WALL, sy + 2 * WALL, ROOM_H), wall_mat, concrete)
	box("WallWest", (ROOM_X[0] - 0.5 * WALL, cy, 0.5 * ROOM_H), (WALL, sy + 2 * WALL, ROOM_H), wall_mat, concrete)
	box("WallNorth", (cx, ROOM_Y[1] + 0.5 * WALL, 0.5 * ROOM_H), (sx, WALL, ROOM_H), wall_mat, concrete)
	box("WallSouth", (cx, ROOM_Y[0] - 0.5 * WALL, 0.5 * ROOM_H), (sx, WALL, ROOM_H), wall_mat, concrete)
	# Wainscot / kick plate along the east wall (dark wood), where a ball that rolls off the 7-ft ends.
	box("KickPlateEast", (ROOM_X[1] - 2.0, cy, 45.0), (4.0, sy, 90.0), wood_mat, "/Game/Generated/Physics/PM_RbSurface_Wood")

	# Tables under their lamps.
	for label, preset, balls, index, loc, yaw, lamp_m, player, venue in TABLES:
		table = rb.spawn(unreal.RbTable, loc, (0.0, yaw, 0.0), label)
		table.set_editor_property("preset", getattr(unreal.RbTablePreset, preset))
		table.set_editor_property("ball_set", getattr(unreal.RbBallSetPreset, balls))
		table.set_editor_property("use_baked_meshes", True)
		table.set_editor_property("table_index", index)
		table.set_editor_property("lamp_underside_height", lamp_m)
		if venue:
			table.set_editor_property("use_venue_condition", True)
			table.set_editor_property("venue_kind", unreal.RbVenueKind.DIVE_BAR)
			table.set_editor_property("venue_seed", 1717)
			table.set_editor_property("use_lamp_footprint", True)
		if player:
			table.tags = [unreal.Name("RbPlayerTable")]
		table.rebuild_table()
		hl, hw = BED[preset]
		lamp_z = table.get_bed_center_world().z + 100.0 * lamp_m
		housing = box(f"{label}_LampHousing", table_point(loc, yaw, 0.0, 0.0, lamp_z + 9.0), (1.35 * hl, 0.62 * hw, 16.0), housing_mat, collision=False)
		housing.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False)
		diffuser = box(f"{label}_LampDiffuser", table_point(loc, yaw, 0.0, 0.0, lamp_z + 0.4), (1.30 * hl, 0.56 * hw, 0.8), diffuser_mat, collision=False)
		diffuser.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False)
		light = rb.spawn(unreal.RectLight, table_point(loc, yaw, 0.0, 0.0, lamp_z - 0.5), (-90.0, yaw, 0.0), f"{label}_Lamp")
		lc = light.light_component
		lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
		lc.set_editor_property("intensity", 3200.0 if index == 0 else 2400.0)
		lc.set_editor_property("source_width", 1.30 * hl)
		lc.set_editor_property("source_height", 0.56 * hw)
		lc.set_editor_property("barn_door_angle", 70.0)
		lc.set_editor_property("barn_door_length", 14.0)
		lc.set_editor_property("temperature", LAMP_TEMPERATURE_K)
		lc.set_editor_property("use_temperature", True)
		lc.set_editor_property("attenuation_radius", 900.0)

	# Two bar stools on the 7-ft's long side facing +X (where a ball jumped over that rail lands and rolls).
	stool("StoolA", 525.0, 80.0, seat_mat, steel_mat)
	stool("StoolB", 500.0, 235.0, seat_mat, steel_mat)

	# Dim warm room light (Lumen bounces it; enough that the eye does not adapt the lit cloth to white): three wall washers.
	# M2-E review: 1500 lm each left ~15 lux on the floor against ~700 lux on the beds; the centre-weighted Eyes metering of the
	# overview (floor between the tables) then pushed both cloths 3+ stops over mid-grey and they clipped to a pale white-blue
	# (together with the missing white point below). AMBIENT_LUMENS gives a dim bar's ~100 lux of fill; the beds stay ~3 stops
	# brighter, so the lit cloth still glows in the overview (Docs/images/dev/m2e/two_tables.png) but reads blue.
	for i, (x, y) in enumerate(((0.0, 380.0), (-420.0, -380.0), (520.0, -380.0))):
		p = rb.spawn(unreal.PointLight, (x, y, 250.0), label=f"Ambient{i}")
		pc = p.light_component
		pc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
		pc.set_editor_property("intensity", AMBIENT_LUMENS)
		pc.set_editor_property("temperature", 2700.0)
		pc.set_editor_property("use_temperature", True)
		pc.set_editor_property("source_radius", 6.0)
		pc.set_editor_property("attenuation_radius", 700.0)

	# Post-process baseline of the test room (ARbTestRoom, plan 4.4 Eyes): local exposure keeps the lit cloth from clipping while the
	# eye adapts to the dim room around it; low bloom (lamp glare only). The cameras' own settings (exposure, metering) blend on top.
	ppv = rb.spawn(unreal.PostProcessVolume, (0.0, 0.0, 150.0), label="PostProcess")
	ppv.set_editor_property("unbound", True)
	pps = ppv.get_editor_property("settings")
	pps.set_editor_property("override_local_exposure_highlight_contrast_scale", True)
	pps.set_editor_property("local_exposure_highlight_contrast_scale", 0.7)
	pps.set_editor_property("override_local_exposure_shadow_contrast_scale", True)
	pps.set_editor_property("local_exposure_shadow_contrast_scale", 0.9)
	pps.set_editor_property("override_bloom_intensity", True)
	pps.set_editor_property("bloom_intensity", 0.15)
	# The eye adapts to the lamps' colour (as ARbTestRoom: white point = lamp temperature + 200 K). M2-E review: without it the
	# 3600 K lamps were seen against a D65 white point - the room turned orange and the blue cloth a pale grey-white.
	pps.set_editor_property("override_white_temp", True)
	pps.set_editor_property("white_temp", LAMP_TEMPERATURE_K + 200.0)
	ppv.set_editor_property("settings", pps)

	# The ball return (a corner "behind the bar" stand-in): profile + tag RbBallReturn.
	ret = rb.spawn(unreal.TriggerBox, (560.0, -400.0, 20.0), label="BallReturn")
	ret.tags = [unreal.Name("RbBallReturn")]
	shape = ret.get_component_by_class(unreal.BoxComponent)
	shape.set_box_extent(unreal.Vector(55.0, 55.0, 20.0))
	shape.set_collision_profile_name("RbBallReturn")

	# The player at the 7-ft's head end (core -x = world -Y for yaw 90), facing the table.
	rb.spawn(unreal.PlayerStart, (300.0, -45.0, 100.0), (0.0, 90.0, 0.0), "PlayerStart")

	look_dev_camera("RbCam_M2E_Overview", (-560.0, 400.0, 230.0), (60.0, -20.0, 40.0))
	# The capture shot (docstring) jumps the oversized cue ball over the 7-ft's +X rail: it bounces on the VCT, rolls between StoolA's
	# legs and comes to rest against the kick plate near (613, 80). A low view along the kick plate (the ball, StoolA's legs, the
	# 7-ft's apron behind), and the player's standing view from the 7-ft's +X rail (past StoolA's seat and legs).
	look_dev_camera("RbCam_M2E_LooseBall", (606.0, -40.0, 16.0), (560.0, 90.0, 12.0), focus_cm=121.0)
	look_dev_camera("RbCam_M2E_LooseBallEye", (385.0, 150.0, 165.0), (590.0, 88.0, 10.0))

	rb.save_current_level(LEVEL)
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
	tables = [a for a in actors if isinstance(a, unreal.RbTable)]
	indices = sorted(t.get_editor_property("table_index") for t in tables)
	tagged = [t for t in tables if unreal.Name("RbPlayerTable") in t.tags]
	if indices != [0, 1] or len(tagged) != 1:
		rb.fail(f"{LEVEL}: tables {indices}, player tags {len(tagged)}")
	rb.log(f"M2-E dev level OK: {LEVEL} (tables {indices}, player table {tagged[0].get_actor_label()})")


main()
