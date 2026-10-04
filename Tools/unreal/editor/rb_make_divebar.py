"""Builds the dive-bar level /Game/Generated/Maps/L_DiveBar (M2-A; Docs/ue-architecture.md 18.8, venue-dive-bar 4, 12, 13.8).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_divebar.py [-- --no-validate]

From Art/DiveBar/layout.json + lights.json (venue frame V = UE world axes, metres x 100) and the Blender exports of
Art/DiveBar/Export (imported by rb_import_divebar.py; a missing import is done here):
  * the persistent level (non-partitioned; World Settings GameMode ARbGameMode) with the always-loaded sublevels
    L_DiveBar_Geo (architecture, props / greyboxes, collision, ball return, kick plates, audio anchors, NPC / chore markers),
    L_DiveBar_Light_Open (the night rig of 4.2, haze of 4.7), L_DiveBar_Light_LightsUp (the troffers of 4.6) and
    L_DiveBar_Light_AfterHours (state stub: the AfterHours state is a set of intensity factors on the Open rig);
  * the architecture of db_arch.py and the neon signs of db_neon.py at the venue origin; the ceiling fixtures (troffers, diffusers,
    EXIT signs) from their local assets; every element of 2.3: M2-B's mesh when it exists (placed by its pivot / front convention:
    FRONT = local +X yawed into the room, floor items on the floor, wall items on the wall plane with z = floor, hanging items at the
    ceiling anchor), else a greybox composed of GB_* primitives at true size (the level is playable at every stage);
  * ARbTable at (1375.9, 542.7, 0) cm, yaw 0 (SevenFootBar, OldBarOversizedCue, TableIndex 0, tag RbPlayerTable, venue condition
    with the searched VenueSeed, LampUndersideHeight 0.86 m, lamp footprint), ARbVenueInfo (DiveBar, seed, Age 0.80, the lights of
    lights.json with their state factors / animations / neon tube flux), PlayerStart at the head end facing +X;
  * lights of lights.json (4.1-4.3 flags: volumetric shadow rule, neon proxies SpecularScale 0 and flux = tubes / pi, source sizes),
    exponential height fog with volumetric fog + a local fog volume over the lamp (4.7), dust motes / light function when
    rb_make_divebar_fx.py made them;
  * ARbLookDevCamera actors tagged RbCam_DB_V01..V12, RbCam_DB_TH1..TH7, RbCam_Menu_S0..S7 (V10 orthographic, V04 chin on cue);
  * then ARbVenueInfo::ValidateVenueLevel (VDB-T1 lux, VDB-T8, T10, T11, T12 parts, cameras, tables): a FAIL fails the run.
Idempotent (a re-run clears and rebuilds every level of the map). Owner: M2-A.
"""

from __future__ import annotations

import json
import math
import os
import sys

import unreal

EDITOR_DIR = os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor")
sys.path.insert(0, EDITOR_DIR)
import rb_common as rb  # noqa: E402
import rb_import_divebar as imp  # noqa: E402

MAP = "/Game/Generated/Maps/L_DiveBar"  # RbAssetPaths::DiveBarMap
SUBLEVELS = ["Geo", "Light_Open", "Light_LightsUp", "Light_AfterHours"]
REPO = os.path.normpath(os.path.abspath(unreal.Paths.project_dir()))
ART = os.path.join(REPO, "Art", "DiveBar")
VENUE = "/Game/Generated/Venues/DiveBar"
FX_DIR = VENUE + "/FX"
GB_DIR = VENUE + "/Arch/Greybox"

TAG_GEO = "RbDB_Geo"
TAG_CEIL = "RbDB_Ceiling"
TAG_LIGHT = "RbDB_Light"
TAG_FAN = "RbDB_Fan"
TAG_GREYBOX = "RbDB_Greybox"

EAS = None  # EditorActorSubsystem


def cm(v):
	return tuple(100.0 * float(c) for c in v)


def load(rel: str) -> dict:
	with open(os.path.join(ART, rel), "r", encoding="utf-8") as handle:
		return json.load(handle)


# ------------------------------------------------------------------------------------------------------------------------------
# levels
# ------------------------------------------------------------------------------------------------------------------------------


def _levels_by_name(world) -> dict:
	out = {}
	for level in unreal.EditorLevelUtils.get_levels(world):
		out[level.get_outermost().get_name().split("/")[-1]] = level
	return out


def fresh_world() -> tuple:
	"""The persistent map + its sublevels, emptied (re-run) or created. Returns (world, {suffix: ULevel})."""
	rb.ensure_dir("/Game/Generated/Maps")
	if unreal.EditorAssetLibrary.does_asset_exist(MAP):
		world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
		if world is None:
			rb.fail(f"could not load {MAP}")
	else:
		world = rb.new_level(MAP)
	levels = _levels_by_name(world)
	for suffix in SUBLEVELS:
		name = f"L_DiveBar_{suffix}"
		path = f"{MAP}_{suffix}"
		if name in levels:
			continue
		if unreal.EditorAssetLibrary.does_asset_exist(path):
			streaming = unreal.EditorLevelUtils.add_level_to_world(world, path, unreal.LevelStreamingAlwaysLoaded)
		else:
			streaming = unreal.EditorLevelUtils.create_new_streaming_level(unreal.LevelStreamingAlwaysLoaded, path, False)
		if streaming is None:
			rb.fail(f"could not create / add the sublevel {path}")
		levels = _levels_by_name(world)
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	levels = _levels_by_name(world)
	for actor in EAS.get_all_level_actors():
		if isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
			continue
		EAS.destroy_actor(actor)
	missing = [s for s in SUBLEVELS if f"L_DiveBar_{s}" not in levels]
	if missing:
		rb.fail(f"sublevels missing after setup: {missing} (have {sorted(levels)})")
	return world, {s: levels[f"L_DiveBar_{s}"] for s in SUBLEVELS} | {"": levels.get("L_DiveBar")}


def use_level(level) -> None:
	"""Spawns go into this level from now on (persistent level or an always-loaded sublevel)."""
	name = level.get_outermost().get_name().split("/")[-1]
	les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
	if not les.set_current_level_by_name(name):
		rb.fail(f"could not make {name} current")


# ------------------------------------------------------------------------------------------------------------------------------
# assets
# ------------------------------------------------------------------------------------------------------------------------------


class Assets:
	"""asset_id -> (normalized meta, json path, UE mesh) of every Blender export (importing what is missing)."""

	def __init__(self):
		self.meta = {}
		self.meshes = {}
		ledger = imp.load_ledger()
		for json_path in imp.find_assets():
			with open(json_path, "r", encoding="utf-8") as handle:
				meta = imp.normalize_meta(json.load(handle))
			asset = meta.get("asset_id")
			path = f"{meta['_folder']}/{meta['_name']}"
			if not unreal.EditorAssetLibrary.does_asset_exist(path):
				problems = imp.ledger_check(meta, ledger) + imp.metadata_check(meta, json_path)
				if problems:
					rb.log(f"asset {asset} not imported and refused: {problems}")
					continue
				mesh = imp.import_fbx(os.path.join(os.path.dirname(json_path), meta["fbx"]), meta["_folder"], meta["_name"],
					build_nanite=not meta.get("translucent", False), collision=int(meta.get("collision_hulls", 0)) > 0)
				for line in imp.configure_mesh(mesh, meta, json_path):
					rb.log(f"{asset}{line}")
			mesh = unreal.load_asset(path)
			if isinstance(mesh, unreal.StaticMesh):
				self.meta[asset] = (meta, json_path)
				self.meshes[asset] = mesh
		# M2-B's metadata names the 2.3 element it builds ("element": "E16"): the level finds a prop by its element when the layout's
		# asset id differs (LedgeBack for E16's BackLedge, TableLamp_OldCastor for E14's TableLamp, ...).
		self.by_element = {}
		for asset in sorted(self.meshes):
			element = self.info(asset).get("element")
			if element:
				self.by_element.setdefault(str(element), []).append(asset)

	def resolve(self, asset: str, element: str | None = None) -> str | None:
		"""The imported asset for a layout asset id: itself, else the element's only asset (or the one whose id starts with it)."""
		if asset and asset in self.meshes:
			return asset
		candidates = self.by_element.get(str(element), []) if element else []
		prefixed = [c for c in candidates if asset and c.lower().startswith(asset.lower())]
		if len(prefixed) == 1:
			return prefixed[0]
		return candidates[0] if len(candidates) == 1 else None

	def has(self, asset: str) -> bool:
		return asset in self.meshes

	def mesh(self, asset: str):
		return self.meshes.get(asset)

	def info(self, asset: str) -> dict:
		return self.meta[asset][0] if asset in self.meta else {}


# ------------------------------------------------------------------------------------------------------------------------------
# spawning helpers
# ------------------------------------------------------------------------------------------------------------------------------


def rot(pitch=0.0, yaw=0.0, roll=0.0):
	return unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)


def spawn_actor(cls, loc_cm=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), label: str | None = None, tags=()):
	actor = EAS.spawn_actor_from_class(cls, unreal.Vector(*loc_cm), rot(*rotation))
	if actor is None:
		rb.fail(f"could not spawn {cls}")
	if label:
		actor.set_actor_label(label)
	if tags:
		actor.tags = [unreal.Name(t) for t in tags]
	return actor


def mesh_actor(mesh, loc_cm=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), label=None, tags=(), profile="RbVenueBlock",
		shadow=True, materials=None, mobility=unreal.ComponentMobility.STATIC, hidden=False, ray_tracing=True, indirect=True):
	actor = spawn_actor(unreal.StaticMeshActor, loc_cm, rotation, label, tags)
	comp = actor.static_mesh_component
	comp.set_mobility(mobility)
	comp.set_static_mesh(mesh)
	actor.set_actor_scale3d(unreal.Vector(*scale))
	if profile:
		comp.set_collision_profile_name(profile)
	else:
		comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
	comp.set_editor_property("cast_shadow", bool(shadow))
	if not ray_tracing:
		comp.set_editor_property("visible_in_ray_tracing", False)
	if not indirect:
		comp.set_editor_property("affect_dynamic_indirect_lighting", False)
		comp.set_editor_property("affect_distance_field_lighting", False)
	if materials:
		for i, m in enumerate(materials):
			if m is not None:
				comp.set_material(i, m)
	if hidden:
		actor.set_actor_hidden_in_game(True)
		comp.set_editor_property("cast_shadow", False)
	return actor


_GB_MESH = {}


def gb_mesh(name: str):
	if name not in _GB_MESH:
		path = f"{GB_DIR}/SM_DB_{name}"
		mesh = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
		if mesh is None:
			mesh = unreal.load_asset("/Engine/BasicShapes/Cube" if name == "GB_Box" else "/Engine/BasicShapes/Cylinder")
		_GB_MESH[name] = mesh
	return _GB_MESH[name]


def gb_asset(name: str):
	"""A greybox primitive of db_arch.py (None when it was not exported / imported yet)."""
	path = f"{GB_DIR}/SM_DB_{name}"
	return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None


def gb_mat(name: str):
	return imp.fallback_material(f"MI_DB_GB_{name}")


def gb_box(box, mat: str, label: str, tags=(), profile="RbVenueBlock", shadow=True, rotation=(0.0, 0.0, 0.0), hidden=False):
	"""Greybox box [[x0, x1], [y0, y1], [z0, z1]] (metres, venue frame)."""
	(x0, x1), (y0, y1), (z0, z1) = box
	size = (max(1e-3, x1 - x0), max(1e-3, y1 - y0), max(1e-3, z1 - z0))
	centre = cm(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
	return mesh_actor(gb_mesh("GB_Box"), centre, rotation, size, label, (TAG_GEO, TAG_GREYBOX) + tuple(tags), profile, shadow,
		[gb_mat(mat)], hidden=hidden)


def gb_cyl(centre_m, radius, length, mat: str, label: str, axis="z", tags=(), profile="RbVenueBlock", shadow=True):
	rotation = {"z": (0.0, 0.0, 0.0), "x": (90.0, 0.0, 0.0), "y": (0.0, 0.0, 90.0)}[axis]
	return mesh_actor(gb_mesh("GB_Cylinder"), cm(centre_m), rotation, (2 * radius, 2 * radius, length), label,
		(TAG_GEO, TAG_GREYBOX) + tuple(tags), profile, shadow, [gb_mat(mat)])


# ------------------------------------------------------------------------------------------------------------------------------
# geometry sublevel
# ------------------------------------------------------------------------------------------------------------------------------


ARCH = [
	# asset, tags, profile, cast shadow, indirect
	("Arch_Floor", (), "RbVenueBlock", True, True),
	("Arch_Walls", (), "RbVenueBlock", True, True),
	("Arch_Ceiling", (TAG_CEIL,), "RbVenueBlock", True, True),
	("Arch_Plenum", (TAG_CEIL,), "RbVenueProp", True, True),
	("Arch_Columns", (), "RbVenueBlock", True, True),
	("Arch_Doors", (), "RbVenueBlock", True, True),
	("Arch_GlassBlock", (), "RbVenueBlock", False, True),
	("Arch_Street", (), None, False, False),
]


def place_arch(A: Assets) -> int:
	count = 0
	for asset, tags, profile, shadow, indirect in ARCH:
		if not A.has(asset):
			rb.log(f"WARNING: architecture asset {asset} missing (run Tools/blender/rbbl.py run Tools/blender/divebar/db_arch.py)")
			continue
		mesh_actor(A.mesh(asset), label=asset, tags=(TAG_GEO, asset) + tags, profile=profile, shadow=shadow, indirect=indirect)
		count += 1
	for asset, meta in sorted((k, A.info(k)) for k in A.meshes):
		if meta.get("family") == "Neon":
			# the tubes stay visible in reflections but out of Lumen GI (the proxy rect light carries the light, 4.1 / R-06)
			mesh_actor(A.mesh(asset), label=asset, tags=(TAG_GEO, "RbDB_Neon", asset), profile="RbVenueProp", shadow=False, indirect=False)
			count += 1
	return count


def place_ceiling_fixtures(L: dict, A: Assets) -> None:
	els = {e["id"]: e for e in L["elements"]}
	zc = L["shell"]["ceiling_z"]
	for k, (x, y) in enumerate(els["E23"]["cells"]):
		tag = f"RbDB_Troffer_T{k + 1}"
		if A.has("Troffer"):
			mesh_actor(A.mesh("Troffer"), cm((x, y, zc)), label=f"Troffer_T{k + 1}", tags=(TAG_GEO, TAG_CEIL, tag), profile="RbVenueProp")
		else:
			gb_box([[x - 0.6, x + 0.6], [y - 0.3, y + 0.3], [zc - 0.01, zc + 0.1]], "troffer_lens", f"Troffer_T{k + 1}", (TAG_CEIL, tag), "RbVenueProp")
	for k, (x, y) in enumerate(els["M16d"]["cells"]):
		if A.has("HvacDiffuser"):
			mesh_actor(A.mesh("HvacDiffuser"), cm((x, y, zc)), label=f"HvacDiffuser_{k + 1}", tags=(TAG_GEO, TAG_CEIL, "RbDB_Diffuser"),
				profile="RbVenueProp")
	# EXIT signs: L25a over the back exit (faces -X into the corridor), L25b double-faced at the front door (faces +X / -X).
	if A.has("ExitSign"):
		mesh_actor(A.mesh("ExitSign"), cm((20.09, 0.80, 2.44)), (0.0, 0.0, 0.0), label="ExitSign_L25a", tags=(TAG_GEO, "RbDB_Exit"),
			profile="RbVenueProp", shadow=False)
		mesh_actor(A.mesh("ExitSign"), cm((0.35, 6.10, 2.62)), (0.0, 0.0, 0.0), label="ExitSign_L25b", tags=(TAG_GEO, "RbDB_Exit"),
			profile="RbVenueProp", shadow=False)
		# the hanging sign's stem to the ceiling
		gb_cyl((0.35, 6.10, 2.68), 0.008, 0.12, "chrome", "ExitSign_L25b_Stem", profile="RbVenueProp")


def facing_yaw(facing: str) -> float:
	return {"+X": 0.0, "-X": 180.0, "+Y": 90.0, "-Y": -90.0}.get(facing, 0.0)


def infer_facing(box, L: dict) -> str:
	(x0, x1), (y0, y1), _ = box
	X1, Y1 = L["shell"]["main_room"]["x"][1], L["shell"]["main_room"]["y"][1]
	d = {"+X": x0 - 0.0, "-X": X1 - x1, "+Y": y0 - 0.0, "-Y": Y1 - y1}
	return min(d, key=lambda k: d[k])


def place_prop(A: Assets, asset: str, element: dict, box, facing: str, yaw_extra=0.0, offset=(0.0, 0.0), label=None, tags=(), use_hint=True) -> bool:
	"""M2-B's mesh by its pivot convention (FRONT = local +X), or at the metadata's placement_hint_ue_cm (location [cm] + yaw) when the
	generator gives one for its element. The asset is found by id, else by the element (Assets.resolve). False when none exists."""
	asset = A.resolve(asset, element.get("id"))
	if not asset:
		return False
	meta = A.info(asset)
	(x0, x1), (y0, y1), (z0, z1) = box
	yaw = facing_yaw(facing) + yaw_extra
	pivot = str(meta.get("pivot", "floor_center")).lower()
	frame = str(meta.get("frame", "local")).lower()
	hint = meta.get("placement_hint_ue_cm") if use_hint else None
	if hint and "location" in hint:
		loc = tuple(float(c) / 100.0 for c in hint["location"])
		yaw = float(hint.get("yaw_deg", yaw))
	elif frame == "venue":
		loc = (0.0, 0.0, 0.0)
		yaw = 0.0
	elif "wall" in pivot:
		f = {"+X": (x0, (y0 + y1) / 2), "-X": (x1, (y0 + y1) / 2), "+Y": ((x0 + x1) / 2, y0), "-Y": ((x0 + x1) / 2, y1)}[facing]
		loc = (f[0], f[1], 0.0)
	elif "ceil" in pivot or "hang" in pivot:
		loc = ((x0 + x1) / 2, (y0 + y1) / 2, z1)
	else:
		loc = ((x0 + x1) / 2, (y0 + y1) / 2, z0)
	loc = (loc[0] + offset[0], loc[1] + offset[1], loc[2])
	profile = meta.get("profile") or element.get("profile", "RbVenueBlock")
	actor = mesh_actor(A.mesh(asset), cm(loc), (0.0, yaw, 0.0), label=label or f"{element.get('id', asset)}_{asset}",
		tags=(TAG_GEO, "RbDB_Prop", f"RbDB_{element.get('id', asset)}") + tuple(tags), profile=profile,
		shadow=meta.get("cast_shadow", True) is not False, ray_tracing=meta.get("ray_tracing", True) is not False)
	# prop instances the generator lists (e.g. bottles on a back-bar shelf), relative to this prop's pivot
	for inst in meta.get("prop_instances", []) or []:
		sub = inst.get("asset") or inst.get("asset_id")
		pos = inst.get("location_ue_m") or inst.get("pos_ue_m")
		if not sub or not pos or not A.has(sub):
			continue
		c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
		wx, wy = loc[0] + c * pos[0] - s * pos[1], loc[1] + s * pos[0] + c * pos[1]
		mesh_actor(A.mesh(sub), cm((wx, wy, loc[2] + pos[2])), (0.0, yaw + float(inst.get("yaw_deg", 0.0)), 0.0), label=f"{asset}_{sub}",
			tags=(TAG_GEO, "RbDB_Prop"), profile=A.info(sub).get("profile", "RbVenueProp"))
	del actor
	return True


def greybox_parts(element: dict) -> None:
	eid = element["id"]
	tags = (f"RbDB_{eid}",)
	profile = element.get("profile", "RbVenueBlock")
	parts = element.get("parts")
	if not parts:
		gb_box(element["box"], element.get("greybox_mat", "dark_wood"), f"{eid}_GB", tags, profile)
		return
	for k, p in enumerate(parts):
		if p["shape"] == "box":
			gb_box(p["box"], p["mat"], f"{eid}_GB_{k:02d}", tags, profile)
		elif p["shape"].startswith("cyl_"):
			gb_cyl(p["center"], p["radius"], p["length"], p["mat"], f"{eid}_GB_{k:02d}", axis=p["shape"][-1], tags=tags, profile=profile)


def gb_stool(x, y, yaw, seat_z, seat_d, label, tags, variant="A", wooden=False):
	"""Bar stool stand-in: chrome pedestal, foot ring, round vinyl seat (legs thin enough for a ball to roll by)."""
	if wooden:
		for a in range(4):
			ang = math.radians(yaw + 45 + 90 * a)
			gb_cyl((x + 0.15 * math.cos(ang), y + 0.15 * math.sin(ang), seat_z / 2), 0.016, seat_z, "wood_light", f"{label}_Leg{a}", tags=tags)
		gb_cyl((x, y, seat_z - 0.02), seat_d / 2, 0.04, "wood_light", f"{label}_Seat", tags=tags)
		gb_cyl((x, y, 0.30), 0.19, 0.02, "wood_light", f"{label}_Ring", tags=tags, profile="RbVenueProp")
		return
	gb_cyl((x, y, 0.012), 0.21, 0.024, "stool_chrome", f"{label}_Base", tags=tags)
	gb_cyl((x, y, seat_z / 2), 0.025, seat_z - 0.06, "stool_chrome", f"{label}_Post", tags=tags)
	gb_cyl((x, y, 0.32), 0.19, 0.012, "stool_chrome", f"{label}_Ring", tags=tags, profile="RbVenueProp")
	seat = "stool_seat" if variant != "C" else "black_plastic"
	gb_cyl((x, y, seat_z - 0.045), seat_d / 2, 0.09, seat, f"{label}_Seat", tags=tags)


def place_elements(L: dict, A: Assets) -> dict:
	"""Every element of 2.3: M2-B's prop or its greybox. Returns {element id: 'prop' | 'greybox'}."""
	out = {}
	for e in L["elements"]:
		eid, asset = e["id"], e.get("asset", "")
		if eid in ("E23", "M16d"):  # ceiling fixtures (place_ceiling_fixtures)
			continue
		if "array" in e:  # bar stools
			arr = e["array"]
			variants = e.get("variants", ["A"])
			used_prop = False
			for i in range(arr["count"]):
				r = _rng(eid, i)
				x = arr["x0"] + arr["dx"] * i + (r() * 2 - 1) * e.get("jitter_m", 0.03)
				y = arr["y"] + (r() * 2 - 1) * e.get("jitter_m", 0.03)
				yaw = (r() * 2 - 1) * e.get("yaw_jitter_deg", 25.0)
				v = variants[int(r() * len(variants)) % len(variants)]
				box = [[x - 0.2, x + 0.2], [y - 0.2, y + 0.2], [0.0, e["seat_z"]]]
				label = f"{eid}_Stool{i + 1:02d}"
				if place_prop(A, f"{asset}_{v}", e, box, "+X", yaw_extra=yaw, label=label, tags=(f"RbDB_Stool{i + 1:02d}",)):
					used_prop = True
				else:
					gb_stool(x, y, yaw, e["seat_z"], e["seat_d"], label, (f"RbDB_{eid}", f"RbDB_Stool{i + 1:02d}"), variant=v)
			out[eid] = "prop" if used_prop else "greybox"
			continue
		if eid == "E17s":  # spectator stools
			used_prop = False
			for i, (x, y) in enumerate(e["points"]):
				r = _rng(eid, i)
				yaw = 90.0 + (r() * 2 - 1) * 12.0
				box = [[x - 0.2, x + 0.2], [y - 0.2, y + 0.2], [0.0, e["seat_z"]]]
				if place_prop(A, asset, e, box, "+X", yaw_extra=yaw, label=f"{eid}_{i + 1}"):
					used_prop = True
				else:
					gb_stool(x, y, yaw, e["seat_z"], e["seat_d"], f"{eid}_{i + 1}", (f"RbDB_{eid}",), wooden=True)
			out[eid] = "prop" if used_prop else "greybox"
			continue
		if eid == "E08":  # booths: bench - table - bench per set, double-sided backs between sets
			out[eid] = place_booths(e, A)
			continue
		if eid == "E08s":  # booth sconces on the right wall: brass backplate + arm, frosted globe around the light (L13-L15)
			place_sconces(e)
			out[eid] = "greybox"
			continue
		if eid == "E15":  # column C3 drink shelf
			cx, cy = e["center"]
			if not place_prop(A, asset, e, [[cx - 0.2, cx + 0.2], [cy - 0.2, cy + 0.2], [0.0, e["z"][1]]], "+X"):
				gb_cyl((cx, cy, 0.5 * sum(e["z"])), e["radius"], e["z"][1] - e["z"][0], "plywood_painted", "E15_Shelf")
				gb_cyl((cx, cy, e["z"][0] - 0.03), 0.075, 0.06, "black_steel", "E15_Clamp")
				gb_box([[cx - 0.01, cx + 0.01], [cy + 0.06, cy + 0.12], [e["hook_z"] - 0.01, e["hook_z"] + 0.01]], "black_steel", "E15_Hook",
					profile="RbVenueProp")
				out[eid] = "greybox"
			else:
				out[eid] = "prop"
			continue
		if eid == "E24":  # ceiling fan: motor, downrod and 4 blades turning at 40 rpm (ARbVenueInfo spins actors tagged RbDB_Fan)
			place_fan(e, L)
			out[eid] = "greybox"
			continue
		if eid == "C02":  # back-bar bottles (greybox scatter until M2-B's lathe library)
			if A.has("BackBar") and A.info("BackBar").get("prop_instances"):
				out[eid] = "prop"
				continue
			place_bottles(e)
			out[eid] = "greybox"
			continue
		if eid == "E08w":  # Polaroid / flyer wall: paper cards on the brick
			place_polaroids(e)
			out[eid] = "greybox"
			continue
		if eid == "E22" and not A.resolve(asset, eid):  # the dead payphone (S11): steel housing, handset on its hook, keypad, taped slot
			place_payphone(e)
			out[eid] = "greybox"
			continue
		if eid == "E01r" and not A.has(asset):  # cast-iron radiator: 64 mm sections on headers and feet
			(x0, x1), (y0, y1), (z0, z1) = e["box"]
			y = y0 + 0.01
			k = 0
			while y + 0.045 <= y1:
				gb_box([[x0 + 0.01, x1 - 0.01], [y, y + 0.045], [z0 + 0.03, z1 - 0.02]], "radiator", f"E01r_Section{k:02d}", ("RbDB_E01r",))
				y += 0.064
				k += 1
			for zz in (z0 + 0.05, z1 - 0.06):
				gb_box([[x0 + 0.05, x1 - 0.05], [y0, y1], [zz, zz + 0.04]], "radiator", f"E01r_Header{int(zz * 100)}", ("RbDB_E01r",))
			for yy in (y0 + 0.06, y1 - 0.10):
				gb_box([[x0 + 0.04, x1 - 0.04], [yy, yy + 0.04], [0.0, z0 + 0.03]], "radiator", f"E01r_Foot{int(yy * 100)}", ("RbDB_E01r",))
			out[eid] = "greybox"
			continue
		box = e["box"]
		facing = e.get("facing") or infer_facing(box, L)
		if asset and place_prop(A, asset, e, box, facing):
			out[eid] = "prop"
			continue
		greybox_parts(e)
		if eid == "E12":
			place_rack_cues(e)
		out[eid] = "greybox"
	return out


def place_payphone(e: dict) -> None:
	"""E22 greybox (M17 until a real one exists): the payphone on the corridor's left wall (Y 0.30, facing +Y): housing, a black
	handset on its hook with the armoured cord, keypad, coin slot and return, and the 2011 OUT OF ORDER tape across the handset."""
	(x0, x1), (y0, y1), (z0, z1) = e["box"]
	cx = 0.5 * (x0 + x1)
	tags = ("RbDB_E22",)
	gb_box([[x0, x1], [y0, y1 - 0.02], [z0, z1]], "payphone", "E22_Housing", tags)
	gb_box([[x0 + 0.03, x1 - 0.03], [y1 - 0.02, y1], [z0 + 0.03, z1 - 0.26]], "black_steel", "E22_Face", tags)
	for r in range(4):  # keypad 3 x 4
		for c in range(3):
			gb_box([[cx - 0.035 + 0.035 * c - 0.012, cx - 0.035 + 0.035 * c + 0.012], [y1, y1 + 0.008],
				[z1 - 0.40 - 0.04 * r - 0.012, z1 - 0.40 - 0.04 * r + 0.012]], "chrome", f"E22_Key{r}{c}", tags, "RbVenueProp", shadow=False)
	gb_box([[cx + 0.06, cx + 0.10], [y1, y1 + 0.01], [z1 - 0.12, z1 - 0.06]], "chrome", "E22_CoinSlot", tags, "RbVenueProp")
	gb_box([[cx - 0.04, cx + 0.04], [y1 - 0.01, y1 + 0.02], [z0 + 0.06, z0 + 0.12]], "chrome", "E22_CoinReturn", tags, "RbVenueProp")
	gb_box([[x0 + 0.01, x0 + 0.03], [y1, y1 + 0.03], [z1 - 0.25, z1 - 0.20]], "chrome", "E22_Hook", tags, "RbVenueProp")
	gb_cyl((x0 - 0.02, y1 + 0.02, z1 - 0.34), 0.022, 0.21, "black_plastic", "E22_Handset", tags=tags, profile="RbVenueProp")
	gb_cyl((x0 - 0.01, y1 + 0.01, z0 + 0.12), 0.006, 0.28, "chrome", "E22_Cord", tags=tags, profile="RbVenueProp")
	gb_box([[x0 - 0.05, x1 + 0.02], [y1 + 0.036, y1 + 0.038], [z1 - 0.37, z1 - 0.32]], "tape", "E22_OutOfOrderTape", tags, "RbVenueProp", shadow=False,
		rotation=(0.0, 0.0, 0.0))


def place_jelly_jar(lights: dict) -> None:
	"""L26 corridor fixture (4.2, "jelly jar" CFL): a round steel base on the ceiling and the frosted glass jar around the light (emissive,
	no shadow of its own light, out of ray tracing / GI like the sconces)."""
	if "L26" not in lights:
		return
	x, y, z = lights["L26"]["pos"]
	ceiling = 2.44
	gb_cyl((x, y, ceiling - 0.012), 0.075, 0.024, "black_steel", "L26_JellyJar_Base", tags=("RbDB_L26",), profile="RbVenueProp")
	glass = gb_asset("GB_SconceGlass")
	if glass is not None:
		mesh_actor(glass, cm((x, y, z)), (0.0, 0.0, 0.0), (0.85, 0.85, 0.95), label="L26_JellyJar_Glass", tags=(TAG_GEO, TAG_GREYBOX, "RbDB_L26"),
			profile="RbVenueProp", shadow=False, materials=[gb_mat("jar_glass")], ray_tracing=False, indirect=False)


def place_rack_cues(e: dict) -> None:
	"""E12 greybox: the 8 house cues standing in the rack (4 x 57, 2 x 52, 1 x 48, 1 x 36 in) and the rack's sweep volume (2.5 / VDB-T3:
	the racked cues block a cue butt out to V Y 7.20, a hidden RbVenueBlock box over the rack's extent)."""
	(x0, x1), (y0, y1), (z0, z1) = e["box"]
	lengths = [1.448, 1.448, 1.321, 1.448, 0.914, 1.448, 1.219, 1.321]
	r = _rng("E12", 0)
	n = len(lengths)
	for i, length in enumerate(lengths):
		x = x0 + 0.09 + (x1 - x0 - 0.18) * i / (n - 1) + (r() * 2 - 1) * 0.008
		y = y0 + 0.05 + r() * 0.01
		zb = z0 + 0.012
		butt = 0.45 * length
		gb_cyl((x, y, zb + butt / 2), 0.0145, butt, "cue_wood", f"E12_Cue{i}_Butt", tags=("RbDB_E12",), profile="RbVenueProp")
		gb_cyl((x, y, zb + butt + 0.02), 0.0140, 0.04, "black_plastic", f"E12_Cue{i}_Joint", tags=("RbDB_E12",), profile="RbVenueProp")
		gb_cyl((x, y, zb + butt + 0.04 + (length - butt - 0.04) / 2), 0.0075, length - butt - 0.04, "wood_light", f"E12_Cue{i}_Shaft",
			tags=("RbDB_E12",), profile="RbVenueProp")
	gb_box([[x0, x1], [y0, y1], [z0, z1]], "black_rubber", "E12_SweepVolume", ("RbDB_E12", "RbDB_SweepVolume"), "RbVenueBlock", shadow=False, hidden=True)


def _rng(key: str, index: int):
	import hashlib
	import random
	seed = int.from_bytes(hashlib.sha256(f"{key}|{index}|1958".encode()).digest()[:8], "little")
	r = random.Random(seed)
	return r.random


def place_booths(e: dict, A: Assets) -> str:
	y0, y1 = e["y"]
	seat_z, back_top, depth, back_t = e["seat_z"], e["back_top_z"], e["seat_depth"], e["back_thickness"]
	# M2-B's booth kit (db_booth.py): end benches at the row's ends (0.2925 m from the end, facing into the row), back-to-back doubles on
	# the set boundaries, tables at the set centres; the row's centre line from its placement hint (Y 6.785).
	if A.has("BoothBench_End") and A.has("BoothBench_Double") and A.has("BoothTable"):
		hint = A.info("BoothBench_End").get("placement_hint_ue_cm") or {}
		yc = float(hint.get("y_cm", 50.0 * (y0 + y1))) / 100.0
		sets = e["sets"]
		mesh_actor(A.mesh("BoothBench_End"), cm((sets[0][0] + 0.2925, yc, 0.0)), (0.0, 0.0, 0.0), label="E08_BenchEnd_W",
			tags=(TAG_GEO, "RbDB_Prop", "RbDB_E08"), profile=A.info("BoothBench_End").get("profile", "RbVenueBlock"))
		mesh_actor(A.mesh("BoothBench_End"), cm((sets[-1][1] - 0.2925, yc, 0.0)), (0.0, 180.0, 0.0), label="E08_BenchEnd_E",
			tags=(TAG_GEO, "RbDB_Prop", "RbDB_E08"), profile=A.info("BoothBench_End").get("profile", "RbVenueBlock"))
		for k in range(len(sets) - 1):
			mesh_actor(A.mesh("BoothBench_Double"), cm((sets[k][1], yc, 0.0)), (0.0, 0.0, 0.0), label=f"E08_BenchDouble{k + 1}",
				tags=(TAG_GEO, "RbDB_Prop", "RbDB_E08"), profile=A.info("BoothBench_Double").get("profile", "RbVenueBlock"))
		for k, (x0, x1) in enumerate(sets):
			mesh_actor(A.mesh("BoothTable"), cm((0.5 * (x0 + x1), yc, 0.0)), (0.0, 0.0, 0.0), label=f"E08_Table{k + 1}",
				tags=(TAG_GEO, "RbDB_Prop", "RbDB_E08"), profile=A.info("BoothTable").get("profile", "RbVenueBlock"))
		return "prop"
	used = False
	for k, (x0, x1) in enumerate(e["sets"]):
		box = [[x0, x1], [y0, y1], [0.0, back_top]]
		if place_prop(A, e.get("asset", "Booth"), e, box, "-Y", label=f"E08_Booth{k + 1}"):
			used = True
			if A.has(e.get("table_asset", "BoothTable")):
				place_prop(A, e["table_asset"], e, [[x0, x1], [y0, y1], [0.0, e["table"]["z"]]], "-Y", label=f"E08_Table{k + 1}")
			continue
		tags = (f"RbDB_Booth{k + 1}",)
		# benches along X: backs at the set ends (the backs between sets are shared: half thickness each), seats inward
		for side, xb in ((+1, x0), (-1, x1)):
			xs0, xs1 = (xb, xb + back_t / 2) if side > 0 else (xb - back_t / 2, xb)
			gb_box([[xs0, xs1], [y0, y1 - 0.02], [0.0, back_top]], "booth_vinyl", f"E08_B{k + 1}_Back{side:+d}", tags)
			sx0, sx1 = (xs1, xs1 + depth) if side > 0 else (xs0 - depth, xs0)
			gb_box([[sx0, sx1], [y0, y1 - 0.02], [0.0, seat_z - 0.10]], "dark_wood", f"E08_B{k + 1}_Plinth{side:+d}", tags)
			gb_box([[sx0, sx1], [y0, y1 - 0.02], [seat_z - 0.10, seat_z]], "booth_vinyl", f"E08_B{k + 1}_Seat{side:+d}", tags)
		# table 0.76 x 1.07 at z 0.76 on a T-base, against the wall
		tw, tl = e["table"]["size"]
		cx = 0.5 * (x0 + x1)
		gb_box([[cx - tw / 2, cx + tw / 2], [y1 - tl - 0.02, y1 - 0.02], [e["table"]["z"] - 0.03, e["table"]["z"]]], "table_laminate", f"E08_B{k + 1}_Top",
			tags)
		gb_cyl((cx, y1 - tl / 2 - 0.02, (e["table"]["z"] - 0.03) / 2), 0.04, e["table"]["z"] - 0.03, "black_steel", f"E08_B{k + 1}_Post", tags=tags)
		gb_box([[cx - 0.25, cx + 0.25], [y1 - tl / 2 - 0.30, y1 - tl / 2 + 0.26], [0.0, 0.03]], "black_steel", f"E08_B{k + 1}_Foot", tags)
	return "prop" if used else "greybox"


def place_fan(e: dict, L: dict) -> None:
	x, y = e["center"]
	zc = L["shell"]["ceiling_z"]
	bz = e["blade_z"]
	fan_tags = (TAG_CEIL, TAG_FAN, "RbDB_E24")
	gb_cyl((x, y, 0.5 * (zc + bz + 0.10)), 0.012, zc - bz - 0.10, "dark_wood", "E24_Downrod", tags=(TAG_CEIL,), profile="RbVenueProp")
	# the rotating part: one actor with the motor housing; the blades are children (attached) so the actor's yaw turns them all
	hub = mesh_actor(gb_mesh("GB_Cylinder"), cm((x, y, bz + 0.05)), (0.0, 0.0, 0.0), (0.22, 0.22, 0.14), "E24_Fan", (TAG_GEO,) + fan_tags,
		"RbVenueProp", True, [gb_mat("dark_wood")], mobility=unreal.ComponentMobility.MOVABLE)
	blade_mesh = unreal.load_asset(f"{GB_DIR}/SM_DB_GB_FanBlade") if unreal.EditorAssetLibrary.does_asset_exist(f"{GB_DIR}/SM_DB_GB_FanBlade") else None
	for k in range(int(e.get("blades", 4))):
		if blade_mesh is None:
			break
		blade = mesh_actor(blade_mesh, cm((x, y, bz)), (0.0, 90.0 * k + 17.0, 0.0), (1.0, 1.0, 1.0), f"E24_Blade{k}", (TAG_GEO, TAG_CEIL, "RbDB_E24"),
			"RbVenueProp", False, mobility=unreal.ComponentMobility.MOVABLE)
		blade.attach_to_actor(hub, unreal.Name(""), unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)


def place_bottles(e: dict) -> None:
	r = _rng("C02", 0)
	x0, x1 = e["x"]
	y0, y1 = e["y"]
	mats = ["bottle", "bottle", "bottle_green", "bottle_clear", "bottle"]
	n = 0
	for zi, z in enumerate(e["shelves_z"]):
		x = x0 + 0.03
		while x < x1:
			if any(a <= x <= b for a, b in e.get("skip_x", [])):
				x += 0.1
				continue
			h = 0.24 + 0.10 * r()
			rad = 0.032 + 0.012 * r()
			y = y0 + (y1 - y0) * (0.35 + 0.3 * r()) if zi > 0 else 0.45 + 0.1 * r()
			mat = mats[int(r() * len(mats))]
			gb_cyl((x, y, z + h * 0.35), rad, h * 0.7, mat, f"C02_Bottle{n:03d}", tags=("RbDB_C02",), profile="RbVenueProp")
			gb_cyl((x, y, z + h * 0.7 + 0.05), rad * 0.35, 0.10, mat, f"C02_Neck{n:03d}", tags=("RbDB_C02",), profile="RbVenueProp", shadow=False)
			n += 1
			x += e["spacing"][0] + (e["spacing"][1] - e["spacing"][0]) * r()
	rb.log(f"C02: {n} greybox bottles")


def place_polaroids(e: dict) -> None:
	(x0, x1), (_, yw), (z0, z1) = e["box"]
	s = e["scatter"]
	r = _rng("E08w", 0)
	for i in range(s.get("flyers", 0)):
		w, h = s["flyer_size"]
		x = x0 + w + (x1 - x0 - 2 * w) * r()
		z = z0 + h / 2 + (z1 - z0 - h) * r()
		gb_box([[x - w / 2, x + w / 2], [7.314, 7.3175], [z - h / 2, z + h / 2]], "poster", f"E08w_Flyer{i:02d}", ("RbDB_E08w",), "RbVenueProp",
			rotation=((r() * 2 - 1) * 3.0, 0.0, 0.0), shadow=False)
	for i in range(s["count"]):
		w, h = s["size"]
		x = x0 + w + (x1 - x0 - 2 * w) * r()
		z = z0 + h + (z1 - z0 - 2 * h) * r()
		tilt = (r() * 2 - 1) * (2.5 if r() > 0.1 else 6.0)
		gb_box([[x - w / 2, x + w / 2], [7.310 - 0.0004 * (i % 5), 7.3135 - 0.0004 * (i % 5)], [z - h / 2, z + h / 2]], "polaroid", f"E08w_Polaroid{i:02d}",
			("RbDB_E08w",), "RbVenueProp", rotation=(tilt, 0.0, 0.0), shadow=False)
		if r() < 0.6:  # a push pin / tape strip on most of them
			gb_box([[x - 0.012, x + 0.012], [7.3090 - 0.0004 * (i % 5), 7.3100 - 0.0004 * (i % 5)], [z + h / 2 - 0.012, z + h / 2 + 0.006]], "tape",
				f"E08w_Tape{i:02d}", ("RbDB_E08w",), "RbVenueProp", shadow=False)


# Wall frames of layout.json "decor" (normal into the room, along-wall axis, wall plane coordinate).
WALLS = {
	"left": ((0.0, 1.0), "x", 0.0), "right": ((0.0, -1.0), "x", 7.32), "back": ((-1.0, 0.0), "y", 16.46), "front": ((1.0, 0.0), "y", 0.0),
	"corridor_wall": ((0.0, -1.0), "x", 1.40), "corridor_left": ((0.0, 1.0), "x", 0.30),
}


def place_decor(L: dict) -> int:
	"""Frames, posters, flyers and signs on the walls (1.3 S2 / S3 / S14 / S19, 5.2 M14 / M15): a frame box with the content panel in
	front (photo / poster / sign) or a bare paper with tape; content from the fallback material's per-object patterns until the
	text-texture and Higgsfield art exist (DB-4 / DB-5)."""
	n = 0
	for d in L.get("decor", []):
		(nx, ny), axis, w0 = WALLS[d["wall"]]
		u, zc = d["at"]
		w, h = d["size"]
		frame = d.get("frame", "none")
		tilt = float(d.get("tilt_deg", 0.0))
		rotation = (tilt, 0.0, 0.0) if axis == "x" else (0.0, 0.0, tilt)
		tags = ("RbDB_Decor", f"RbDB_{d['id']}")

		def box_at(uc, z, off0, off1, ww, hh, mat, label, shadow=True):
			"""A box centred at (uc along the wall, z), from off0 to off1 in front of the wall plane."""
			if axis == "x":
				y0, y1 = sorted((w0 + ny * off0, w0 + ny * off1))
				b = [[uc - ww / 2, uc + ww / 2], [y0, y1], [z - hh / 2, z + hh / 2]]
			else:
				x0, x1 = sorted((w0 + nx * off0, w0 + nx * off1))
				b = [[x0, x1], [uc - ww / 2, uc + ww / 2], [z - hh / 2, z + hh / 2]]
			gb_box(b, mat, label, tags, "RbVenueProp", shadow=shadow, rotation=rotation)

		if frame != "none":
			border = float(d.get("border", 0.03))
			box_at(u, zc, 0.0, 0.028, w + 2 * border, h + 2 * border, f"frame_{frame}", f"{d['id']}_Frame")
			box_at(u, zc, 0.028, 0.031, w, h, d["kind"], f"{d['id']}_{d['kind'].title()}", shadow=False)
		else:
			box_at(u, zc, 0.001, 0.003, w, h, d["kind"], f"{d['id']}_{d['kind'].title()}", shadow=False)
			if d["kind"] == "poster":  # masking tape over the top corners
				for side in (-1, 1):
					box_at(u + side * (w / 2 - 0.02), zc + h / 2 - 0.004, 0.003, 0.0038, 0.05, 0.022, "tape", f"{d['id']}_Tape{side:+d}", shadow=False)
		n += 1
	return n


def place_hero_props(L: dict, A: Assets) -> int:
	"""Small hero props of M2-B placed by the level (layout.json "hero_props": chalk cubes on the rails / the C3 shelf / under the table,
	drinks at the A7 drink spots, the coin dish, quarters on the rail): only when the asset exists (no greybox for clutter)."""
	n = 0
	for p in L.get("hero_props", []):
		asset = A.resolve(p["asset"])
		if not asset:
			continue
		x, y, z = p["at"]
		for k in range(int(p.get("stack", 1))):
			mesh_actor(A.mesh(asset), cm((x, y, z + k * float(p.get("stack_step_m", 0.0)))), (0.0, float(p.get("yaw_deg", 0.0)) + 23.0 * k, 0.0),
				label=f"{p['id']}_{asset}_{k}", tags=(TAG_GEO, "RbDB_Prop", "RbDB_HeroProp", f"RbDB_{p['id']}"),
				profile=A.info(asset).get("profile", "RbVenueProp"))
			n += 1
	return n


def place_pendants(lights: dict) -> int:
	"""L5-L8 bar mini pendants (4.2): the black enamel cone (GB_Pendant, shadow-casting: the shade keeps the bulb's light off the
	ceiling), the exposed 2200 K filament bulb (emissive, no shadow, hidden from ray tracing - the point light is the source) and the
	cord up to the ceiling. Tagged RbDB_Ceiling (the V10 plan looks through them)."""
	shade, bulb = gb_asset("GB_Pendant"), gb_asset("GB_Bulb")
	n = 0
	for lid in sorted(k for k, j in lights.items() if j.get("group") == "bar_pendants"):
		x, y, z = lights[lid]["pos"]
		tags = (TAG_GEO, TAG_GREYBOX, TAG_CEIL, "RbDB_Pendant")
		if shade is not None:
			mesh_actor(shade, cm((x, y, z)), label=f"Pendant_{lid}", tags=tags, profile="RbVenueProp")
		if bulb is not None:
			mesh_actor(bulb, cm((x, y, z)), scale=(1.15, 1.15, 1.15), label=f"Pendant_{lid}_Bulb", tags=tags, profile=None, shadow=False,
				materials=[imp.fallback_material("MI_DB_Emissive_Bulb2200")], ray_tracing=False, indirect=False)
		top = z + 0.12
		gb_cyl((x, y, 0.5 * (top + 2.74)), 0.0035, 2.74 - top, "cord", f"Pendant_{lid}_Cord", tags=(TAG_CEIL, "RbDB_Pendant"), profile="RbVenueProp",
			shadow=True)
		gb_cyl((x, y, 2.735), 0.045, 0.012, "enamel_black", f"Pendant_{lid}_Canopy", tags=(TAG_CEIL, "RbDB_Pendant"), profile="RbVenueProp")
		n += 1
	return n


def place_sconces(e: dict) -> None:
	"""E08s / L13-L15: brass backplate and arm on the right wall, the frosted globe (GB_SconceGlass) around the light, emissive glass
	that does not shadow its own light and stays out of ray tracing / Lumen GI (the analytic light is the source, 4.1)."""
	glass = gb_asset("GB_SconceGlass")
	for i, (x, y, z) in enumerate(e["points"]):
		yc = y - 0.03  # the light sits at the globe centre (lights.json L13-L15: Y 7.19)
		gb_box([[x - 0.045, x + 0.045], [7.305, 7.32], [z - 0.03, z + 0.14]], "brass", f"E08s_Plate{i + 1}", ("RbDB_E08s",), "RbVenueProp")
		gb_cyl((x, 0.5 * (yc + 7.305), z + 0.10), 0.007, 7.305 - yc, "brass", f"E08s_Arm{i + 1}", axis="y", tags=("RbDB_E08s",), profile="RbVenueProp")
		if glass is not None:
			mesh_actor(glass, cm((x, yc, z)), label=f"E08s_Globe{i + 1}", tags=(TAG_GEO, TAG_GREYBOX, "RbDB_E08s"), profile="RbVenueProp", shadow=False,
				materials=[gb_mat("frosted_glass")], ray_tracing=False, indirect=False)
		else:
			gb_cyl((x, yc, z), 0.06, 0.13, "frosted_glass", f"E08s_Globe{i + 1}", axis="z", profile="RbVenueProp", shadow=False)


def place_lamp(L: dict, A: Assets, lights: dict) -> str:
	"""E14 / H03: M2-B's TableLamp at its ceiling anchors, else a stand-in of three enamel shades on a bar with chains."""
	lamp = L["lamp"]
	cx, cy = lamp["centre"]
	yaw = lamp["yaw_deg"]
	zc = lamp["chain_top_z"]
	lamp_asset = A.resolve(lamp["asset"], "E14")
	if lamp_asset:
		place_prop(A, lamp_asset, {"id": "E14"}, [[cx - 0.64, cx + 0.64], [cy - 0.18, cy + 0.18], [lamp["shade_bottom_z"], zc]], "+X", yaw_extra=yaw,
			label="E14_TableLamp", tags=("RbDB_Lamp",))
		# M2-B's bulbs (LampBulb_A19) at the lamp's anchors bulb_L1..L3 (L3 the mismatched 3000 K one): emissive glass without shadow,
		# out of ray tracing (the analytic lights L1-L3 are the sources, 4.1)
		bulb = A.resolve("LampBulb_A19")
		anchors = A.info(lamp_asset).get("anchors_ue_m", {}) or {}
		if bulb:
			hint = A.info(lamp_asset).get("placement_hint_ue_cm") or {}
			ox, oy, oz = (float(c) / 100.0 for c in hint.get("location", (100.0 * cx, 100.0 * cy, 100.0 * zc)))
			lyaw = math.radians(float(hint.get("yaw_deg", yaw)))
			for lid in ("L1", "L2", "L3"):
				a = anchors.get(f"bulb_{lid}")
				if not a:
					continue
				wx = ox + math.cos(lyaw) * a[0] - math.sin(lyaw) * a[1]
				wy = oy + math.sin(lyaw) * a[0] + math.cos(lyaw) * a[1]
				mat = imp.resolve_material("MI_DB_Emissive_Bulb3000" if lights.get(lid, {}).get("cct_k", 2700) >= 3000 else "MI_DB_Emissive_Bulb2700")[0]
				mesh_actor(A.mesh(bulb), cm((wx, wy, oz + a[2])), (0.0, math.degrees(lyaw), 0.0), label=f"E14_Bulb_{lid}", tags=(TAG_GEO, "RbDB_E14", "RbDB_Lamp"),
					profile=None, shadow=False, materials=[mat], ray_tracing=False, indirect=False)
		return "prop"
	shade = gb_mesh("GB_Shade") if unreal.EditorAssetLibrary.does_asset_exist(f"{GB_DIR}/SM_DB_GB_Shade") else None
	bulb = unreal.load_asset(f"{GB_DIR}/SM_DB_GB_Bulb") if unreal.EditorAssetLibrary.does_asset_exist(f"{GB_DIR}/SM_DB_GB_Bulb") else None
	tags = ("RbDB_E14", "RbDB_Lamp")
	c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
	for lid in ("L1", "L2", "L3"):
		p = lights[lid]["pos"]
		if shade is not None:
			mesh_actor(shade, cm(p), (0.0, yaw, 0.0), label=f"E14_Shade_{lid}", tags=(TAG_GEO, TAG_GREYBOX) + tags, profile="RbVenueBlock")
		if bulb is not None:
			mat = imp.fallback_material("MI_DB_Emissive_Bulb3000" if lights[lid].get("cct_k", 2700) >= 3000 else "MI_DB_Emissive_Bulb2700")
			# out of ray tracing AND Lumen GI: a 30,000 cd/m^2 bulb glass would add a second bulb's flux through the surface cache
			# (the rendered white card of VDB-T1 measured the double count; the point lights L1-L3 are the sources, 4.1)
			mesh_actor(bulb, cm(p), (0.0, 0.0, 0.0), label=f"E14_Bulb_{lid}", tags=(TAG_GEO,) + tags, profile=None, shadow=False, materials=[mat],
				ray_tracing=False, indirect=False)
	bar_z = lamp["bar_z"]
	half = lamp["bar_length"] / 2
	mesh_actor(gb_mesh("GB_Box"), cm((cx, cy, bar_z)), (0.0, yaw, 0.0), (lamp["bar_length"], 0.035, 0.035), "E14_Bar", (TAG_GEO, TAG_GREYBOX) + tags,
		"RbVenueBlock", True, [gb_mat("black_steel")])
	for k, sx in enumerate((-half + 0.05, half - 0.05)):
		x, y = cx + c * sx, cy + s * sx
		gb_cyl((x, y, 0.5 * (bar_z + zc)), 0.004, zc - bar_z, "chain", f"E14_Chain{k}", tags=(TAG_CEIL,) + tags, profile="RbVenueProp", shadow=True)
		gb_cyl((x, y, zc - 0.01), 0.012, 0.02, "black_steel", f"E14_EyeBolt{k}", tags=(TAG_CEIL,) + tags, profile="RbVenueProp")
	# the badge in the bar's middle (L4, emissive 150 cd/m2 - M2-B's MI_DB_Emissive_LampBadge when it exists)
	gb_box([[cx - 0.08, cx + 0.08], [cy - 0.025, cy + 0.025], [bar_z - 0.06, bar_z + 0.02]], "dark_wood", "E14_Badge", tags, "RbVenueProp")
	return "greybox"


def place_volumes(L: dict) -> None:
	# RbBallReturn volume behind the bar (13.5 (4))
	br = L["ball_return"]
	(x0, x1), (y0, y1), (z0, z1) = br["box"]
	vol = spawn_actor(unreal.TriggerBox, cm(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)), label=br["id"], tags=("RbBallReturn",))
	box = vol.get_component_by_class(unreal.BoxComponent)
	box.set_box_extent(unreal.Vector(50.0 * (x1 - x0), 50.0 * (y1 - y0), 50.0 * (z1 - z0)), True)
	box.set_collision_profile_name("RbBallReturn")
	# kick plates: invisible blocking hulls closing floor-level gaps no pawn can reach (13.5 (3))
	for kp in L["kick_plates"]:
		gb_box(kp["box"], "black_rubber", kp["id"], ("RbDB_KickPlate",), "RbVenueBlock", shadow=False, hidden=True)


def place_markers(L: dict) -> None:
	for a in L["audio_anchors"]:
		spawn_actor(unreal.TargetPoint, cm(a["location"]), label=f"RbAudio_{a['anchor']}", tags=(f"RbAudio_{a['anchor']}",))
	for z in L["acoustic_zones"]:
		(x0, x1), (y0, y1), (z0, z1) = z["box"]
		vol = spawn_actor(unreal.TriggerBox, cm(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)), label=f"RbAcousticZone_{z['id']}",
			tags=("RbAcousticZone", f"RbAcousticZone_{z['id']}"))
		box = vol.get_component_by_class(unreal.BoxComponent)
		box.set_box_extent(unreal.Vector(50.0 * (x1 - x0), 50.0 * (y1 - y0), 50.0 * (z1 - z0)), True)
		box.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
	for m in L["npc_markers"]:
		spawn_actor(unreal.TargetPoint, cm(m["location"]), (0.0, m.get("yaw_deg", 0.0), 0.0), label=f"RbDB_NPC_{m['id']}", tags=("RbDB_NPC", f"RbDB_NPC_{m['id']}"))
	for c in L["chore_anchors"]:
		spawn_actor(unreal.TargetPoint, cm(c["location"]), label=f"RbDB_Chore_{c['id']}", tags=("RbDB_Chore", f"RbDB_Chore_{c['id']}"))


# VDB-T1 rendered white card (venue-dive-bar 4.4; capture_divebar.py --lux): reference luminances [lit-equivalent cd/m^2] of the
# emissive cards (12.5 x sqrt(2)^k), their offsets from the card centre [cm] and the camera height above the card [cm]; the analysis
# in capture_divebar.py reads the same constants.
LUX_REFS_NITS = [12.5 * 2.0 ** (k / 2.0) for k in range(13)]
LUX_REF_OFFSET_X_CM = 10.0
LUX_REF_PITCH_CM = 3.5
LUX_REF_FIRST_CM = -21.0
LUX_REF_SIZE_CM = 2.6
LUX_CAMERA_HEIGHT_CM = 35.0
LUX_EXPOSURE_BIAS = -10.0  # manual: the 800 cd/m^2 reference stays below white


def place_lux_rig(L: dict) -> None:
	"""The hidden lux rig: root (RbDB_LuxCard, unscaled) + a 8 x 8 cm Lambertian card (albedo 0.80, roughness 1, F0 0) + 13 black
	emissive reference cards 2.6 cm square at their lit-equivalent luminance (calibration.json EmissiveScale) in a row 10 cm beside it +
	a plain camera 35 cm above looking down (manual exposure, neutral white balance, no curve / bloom / vignette / grain / local
	exposure: the calibration camera's look). ARbVenueInfo::ShowLuxProbe moves the root onto a core point and shows it."""
	movable = unreal.ComponentMobility.MOVABLE
	cube = unreal.load_asset("/Engine/BasicShapes/Cube")
	master = imp.make_fallback_master()
	folder = imp.FALLBACK_DIR
	white = imp.make_instance(folder, "MI_DBA_LuxWhite", master, {"Pattern": 0, "Roughness": 1.0, "F0": 0.0, "DustAmount": 0.0, "Age": 0.0,
		"Emissive": 0.0}, {"BaseColor": (0.8, 0.8, 0.8)})
	cx, cy = L["tables"][0]["location_m"][0], L["tables"][0]["location_m"][1]
	z = 100.0 * L["tables"][0]["bed_height_m"] + 0.1
	tags = ("RbDB_LuxRig",)
	root = spawn_actor(unreal.StaticMeshActor, (100.0 * cx, 100.0 * cy, z), label="RbDB_LuxRig_Root", tags=tags + ("RbDB_LuxCard",))
	root.static_mesh_component.set_mobility(movable)
	root.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
	root.set_actor_hidden_in_game(True)

	def card(label, dx, dy, size, mi):
		a = mesh_actor(cube, (100.0 * cx + dx, 100.0 * cy + dy, z), label=label, tags=tags, profile=None, shadow=False,
			materials=[mi], mobility=movable, hidden=True)
		a.set_actor_scale3d(unreal.Vector(size / 100.0, size / 100.0, 0.002))
		a.attach_to_actor(root, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
		return a

	card("RbDB_LuxRig_White", 0.0, 0.0, 8.0, white)
	for k, nits in enumerate(LUX_REFS_NITS):
		mi = imp.make_instance(folder, f"MI_DBA_LuxRef{k:02d}", master, {"Pattern": 0, "Roughness": 1.0, "F0": 0.0, "DustAmount": 0.0, "Age": 0.0,
			"Emissive": nits, "EmissiveScale": imp.emissive_scale()}, {"BaseColor": (0.0, 0.0, 0.0), "EmissiveColor": (1.0, 1.0, 1.0)})
		card(f"RbDB_LuxRig_Ref{k:02d}", LUX_REF_OFFSET_X_CM, LUX_REF_FIRST_CM + LUX_REF_PITCH_CM * k, LUX_REF_SIZE_CM, mi)
	cam = spawn_actor(unreal.CameraActor, (100.0 * cx, 100.0 * cy, z + LUX_CAMERA_HEIGHT_CM), (-90.0, 0.0, 0.0), label="RbCam_DB_Lux",
		tags=tags + ("RbCam_DB_Lux",))
	cc = cam.camera_component
	cc.set_mobility(movable)
	cc.set_editor_property("field_of_view", 70.0)
	cc.set_editor_property("constrain_aspect_ratio", False)
	pp = cc.get_editor_property("post_process_settings")
	for prop, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_bias", LUX_EXPOSURE_BIAS),
			("auto_exposure_apply_physical_camera_exposure", False), ("auto_exposure_bias_curve", None), ("bloom_intensity", 0.0),
			("vignette_intensity", 0.0), ("film_grain_intensity", 0.0), ("scene_fringe_intensity", 0.0), ("lens_flare_intensity", 0.0),
			("motion_blur_amount", 0.0), ("white_temp", 6500.0), ("white_tint", 0.0), ("local_exposure_highlight_contrast_scale", 1.0),
			("local_exposure_shadow_contrast_scale", 1.0)):
		pp.set_editor_property(f"override_{prop}", True)
		pp.set_editor_property(prop, value)
	cc.set_editor_property("post_process_settings", pp)
	cc.set_editor_property("post_process_blend_weight", 1.0)
	cam.attach_to_actor(root, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
	cam.set_actor_hidden_in_game(True)
	rb.log(f"lux rig: white card + {len(LUX_REFS_NITS)} reference cards ({LUX_REFS_NITS[0]:.1f}-{LUX_REFS_NITS[-1]:.0f} cd/m^2, EmissiveScale "
		f"{imp.emissive_scale()}), camera RbCam_DB_Lux {LUX_CAMERA_HEIGHT_CM:.0f} cm above")


def place_fx(L: dict, A: Assets) -> None:
	"""Dust motes in the lamp cone (FX_DustMotes of db_arch.py with M_DBA_DustMote of rb_make_divebar_fx.py, 4.7)."""
	if not A.has("FX_DustMotes") or not unreal.EditorAssetLibrary.does_asset_exist(f"{FX_DIR}/M_DBA_DustMote"):
		rb.log("dust motes not generated (db_arch.py FX_DustMotes + rb_make_divebar_fx.py) - skipped")
		return
	lamp = L["lamp"]
	cx, cy = lamp["centre"]
	motes = mesh_actor(A.mesh("FX_DustMotes"), cm((cx, cy, 1.20)), (0.0, lamp["yaw_deg"], 0.0), label="FX_DustMotes", tags=("RbDB_FX",), profile=None,
		shadow=False, ray_tracing=False, indirect=False, materials=[unreal.load_asset(f"{FX_DIR}/M_DBA_DustMote")])
	motes.static_mesh_component.set_editor_property("bounds_scale", 1.4)  # the drift / updraft world-position offset


# ------------------------------------------------------------------------------------------------------------------------------
# lights
# ------------------------------------------------------------------------------------------------------------------------------


ANIM = {"tv": "TV", "cycle": "CYCLE", "chase": "CHASE", "headlights": "HEADLIGHTS"}


def venue_light(j: dict, defaults: dict, ramp: float):
	states = dict(defaults.get("states", {}))
	states.update(j.get("states", {}))
	v = unreal.RbVenueLight()
	v.set_editor_property("id", j["id"])
	v.set_editor_property("group", j.get("group", ""))
	v.set_editor_property("intensity", float(j["flux_lm"]))
	v.set_editor_property("open_factor", float(states.get("Open", 1.0)))
	v.set_editor_property("lights_up_factor", float(states.get("LightsUp", 1.0)))
	v.set_editor_property("after_hours_factor", float(states.get("AfterHours", 0.0)))
	v.set_editor_property("ramp_seconds", float(j.get("ramp_s", ramp)))
	a = j.get("animation")
	if a:
		v.set_editor_property("animation", getattr(unreal.RbVenueLightAnimation, ANIM[a["type"]]))
		v.set_editor_property("anim_seed", int(a.get("seed", 0)))
		if a["type"] == "tv":
			v.set_editor_property("anim_depth", float(a["depth"]))
			v.set_editor_property("anim_rate", float(a["rate_hz"]))
		elif a["type"] == "cycle":
			v.set_editor_property("anim_rate", float(a["period_s"]))
			v.set_editor_property("cycle_colors", [unreal.LinearColor(c[0], c[1], c[2], 1.0) for c in a["colors"]])
		elif a["type"] == "chase":
			v.set_editor_property("anim_depth", float(a["depth"]))
			v.set_editor_property("anim_rate", float(a["steps_per_s"]))
		elif a["type"] == "headlights":
			v.set_editor_property("interval_range", unreal.Vector2D(*a["interval_s"]))
			v.set_editor_property("sweep_range", unreal.Vector2D(*a["sweep_s"]))
			v.set_editor_property("yaw_range", unreal.Vector2D(*a["yaw_deg"]))
	if j.get("tubes_lm"):
		v.set_editor_property("tube_flux_lm", float(j["tubes_lm"]))
	v.set_editor_property("outside", bool(j.get("outside", False)))
	v.set_editor_property("enclosed", bool(j.get("enclosed", False)))
	if j.get("emissive_actor"):
		v.set_editor_property("emissive_actor_tag", j["emissive_actor"])
		v.set_editor_property("emissive_nits", 2500.0)
	return v


def kelvin_color(k: float):
	"""Planckian locus approximation (Tanner Helland), linear-ish sRGB; used only for colour tints on top of the temperature."""
	t = k / 100.0
	r = 255.0 if t <= 66 else 329.698727446 * ((t - 60) ** -0.1332047592)
	g = 99.4708025861 * math.log(t) - 161.1195681661 if t <= 66 else 288.1221695283 * ((t - 60) ** -0.0755148492)
	b = 255.0 if t >= 66 else (0.0 if t <= 19 else 138.5177312231 * math.log(t - 10) - 305.0447927307)
	return tuple(max(0.0, min(255.0, c)) / 255.0 for c in (r, g, b))


def spawn_light(j: dict, defaults: dict, quality_index: int = 0):
	kind = j["type"]
	cls = {"point": unreal.PointLight, "rect": unreal.RectLight, "spot": unreal.SpotLight}[kind]
	r = j.get("rot", [0.0, 0.0, 0.0])
	actor = spawn_actor(cls, cm(j["pos"]), (r[0], r[1], r[2]), label=f"LT_DB_{j['id']}", tags=(f"LT_DB_{j['id']}", TAG_LIGHT, f"RbDB_LightGroup_{j.get('group', '')}"))
	comp = actor.get_component_by_class(unreal.LocalLightComponent)
	comp.set_mobility(unreal.ComponentMobility.MOVABLE)
	comp.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	states = dict(defaults.get("states", {}))
	states.update(j.get("states", {}))
	comp.set_editor_property("intensity", float(j["flux_lm"]) * float(states.get("Open", 1.0)))
	comp.set_editor_property("attenuation_radius", 100.0 * float(j.get("attenuation_radius_m", defaults.get("attenuation_radius_m", 14.0))))
	if "cct_k" in j:
		comp.set_editor_property("use_temperature", True)
		comp.set_editor_property("temperature", float(j["cct_k"]))
		tint = j.get("color_tint")
		if tint:
			comp.set_editor_property("light_color", unreal.Color(r=int(255 * tint[0]), g=int(255 * tint[1]), b=int(255 * tint[2]), a=255))
	elif "color" in j:
		c = j["color"]
		comp.set_editor_property("use_temperature", False)
		comp.set_editor_property("light_color", unreal.Color(r=int(round(255 * c[0])), g=int(round(255 * c[1])), b=int(round(255 * c[2])), a=255))
	elif j.get("gases"):
		pass
	comp.set_editor_property("specular_scale", float(j.get("specular", defaults.get("specular", 1.0))))
	comp.set_editor_property("volumetric_scattering_intensity", float(j.get("vol", defaults.get("vol", 0.0))))
	comp.set_editor_property("cast_volumetric_shadow", bool(j.get("vol_shadow", defaults.get("vol_shadow", False))))
	comp.set_editor_property("indirect_lighting_intensity", float(j.get("indirect", defaults.get("indirect", 1.0))))
	shadows = j.get("shadows", defaults.get("shadows", [False, False, False]))
	comp.set_editor_property("cast_shadows", bool(shadows[quality_index]))
	if j.get("contact_shadow"):
		comp.set_editor_property("contact_shadow_length", float(j["contact_shadow"]))
	if j.get("shadow_res"):
		comp.set_editor_property("shadow_resolution_scale", float(j["shadow_res"]))
	if kind == "point":
		comp.set_editor_property("source_radius", 100.0 * float(j.get("source_radius_m", 0.01)))
		if j.get("source_length_m"):
			comp.set_editor_property("source_length", 100.0 * float(j["source_length_m"]))
	elif kind == "rect":
		w, h = j["size_m"]
		comp.set_editor_property("source_width", 100.0 * float(w))
		comp.set_editor_property("source_height", 100.0 * float(h))
		if j.get("barn_door"):
			comp.set_editor_property("barn_door_angle", float(j["barn_door"][0]))
			comp.set_editor_property("barn_door_length", 100.0 * float(j["barn_door"][1]))
	elif kind == "spot":
		comp.set_editor_property("source_radius", 100.0 * float(j.get("source_radius_m", 0.02)))
		inner, outer = j.get("cone_deg", [20.0, 30.0])
		comp.set_editor_property("inner_cone_angle", float(inner))
		comp.set_editor_property("outer_cone_angle", float(outer))
		lf = f"{FX_DIR}/M_DBA_LF_GlassBlock"
		if j.get("animation", {}).get("type") == "headlights" and unreal.EditorAssetLibrary.does_asset_exist(lf):
			comp.set_editor_property("light_function_material", unreal.load_asset(lf))
			comp.set_editor_property("light_function_scale", unreal.Vector(20.3, 20.3, 20.3))
	return actor


def place_fog(LJ: dict) -> None:
	fog = LJ["fog"]
	h = fog["height_fog"]
	actor = spawn_actor(unreal.ExponentialHeightFog, (820.0, 366.0, 0.0), label="DB_HeightFog", tags=("RbDB_Fog",))
	comp = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
	sigma_t = float(h["scattering_per_m"]) + float(h["absorption_per_m"])
	# UE 5.8: volumetric extinction = 0.5 x FogDensity / 1000 per cm (SceneCore.cpp, VolumetricFog.usf) = 0.05 x FogDensity per m
	comp.set_editor_property("fog_density", sigma_t / 0.05)
	comp.set_editor_property("fog_height_falloff", 0.001)
	comp.set_editor_property("fog_max_opacity", 1.0)
	comp.set_editor_property("start_distance", 0.0)
	comp.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
	comp.set_editor_property("enable_volumetric_fog", True)
	comp.set_editor_property("volumetric_fog_scattering_distribution", float(h["phase_g"]))
	alb = [int(round(255 * c)) for c in h["albedo"]]
	comp.set_editor_property("volumetric_fog_albedo", unreal.Color(r=alb[0], g=alb[1], b=alb[2], a=255))
	comp.set_editor_property("volumetric_fog_extinction_scale", 1.0)
	comp.set_editor_property("volumetric_fog_distance", 3000.0)
	lf = fog["local_fog"]
	c = lf["center"]
	lfv = spawn_actor(unreal.LocalFogVolume, cm(c), label="DB_LocalFog_Lamp", tags=("RbDB_Fog",))
	# uniform scale only in 5.8 (LocalFogVolumeCommon.ush): a sphere of the ellipsoid's middle semi-axis, base radius 500 cm
	radius_m = float(lf["semi_axes_m"][1])
	lfv.set_actor_scale3d(unreal.Vector(radius_m / 5.0, radius_m / 5.0, radius_m / 5.0))
	lcomp = lfv.get_component_by_class(unreal.LocalFogVolumeComponent)
	lcomp.set_editor_property("radial_fog_extinction", float(lf["scattering_per_m"]))
	lcomp.set_editor_property("height_fog_extinction", 0.0)
	lcomp.set_editor_property("fog_phase_g", float(lf["phase_g"]))
	a = lf["albedo"]
	lcomp.set_editor_property("fog_albedo", unreal.LinearColor(a[0], a[1], a[2], 1.0))


# ------------------------------------------------------------------------------------------------------------------------------
# persistent level: table, venue info, player start, cameras
# ------------------------------------------------------------------------------------------------------------------------------


def look_rotation(eye, target):
	return rb.look_at_rotation(cm(eye), cm(target))


def place_cameras(L: dict) -> int:
	n = 0
	for set_name in ("views", "trailer", "menu"):
		for c in L["cameras"][set_name]:
			eye, target = c["eye"], c["target"]
			rotation = look_rotation(eye, target)
			if c["tag"].endswith("V10"):
				rotation = (-90.0, -90.0, 0.0)
			cam = spawn_actor(unreal.RbLookDevCamera, cm(eye), rotation, label=c["tag"], tags=(c["tag"],))
			preset = unreal.RbCameraPreset.HEADCAM if c.get("preset") == "Headcam" else unreal.RbCameraPreset.EYES
			cam.set_editor_property("preset", preset)
			cam.set_editor_property("focus_distance_cm", 0.0)
			if "chin_on_cue" in c:
				coc = c["chin_on_cue"]
				cam.set_editor_property("placement", unreal.RbLookDevPlacement.CHIN_ON_CUE)
				cam.set_editor_property("cue_ball_core", unreal.Vector2D(*coc["cue_ball_core"]))
				cam.set_editor_property("aim_point_core", unreal.Vector2D(*coc["aim_point_core"]))
				cam.set_editor_property("cue_elevation_deg", float(coc["elevation_deg"]))
			if c.get("ortho_width_m"):
				comp = cam.get_cine_camera_component()
				comp.set_editor_property("projection_mode", unreal.CameraProjectionMode.ORTHOGRAPHIC)
				comp.set_editor_property("ortho_width", 100.0 * float(c["ortho_width_m"]))
			n += 1
	return n


def place_table(L: dict):
	t = L["tables"][0]
	table = spawn_actor(unreal.RbTable, cm(t["location_m"]), (0.0, float(t["yaw_deg"]), 0.0), label=f"Table_{t['table_index']}",
		tags=(("RbPlayerTable",) if t.get("player_table") else ()))
	table.set_editor_property("preset", unreal.RbTablePreset.SEVEN_FOOT_BAR)
	table.set_editor_property("ball_set", unreal.RbBallSetPreset.OLD_BAR_OVERSIZED_CUE)
	table.set_editor_property("table_index", int(t["table_index"]))
	table.set_editor_property("use_venue_condition", True)
	table.set_editor_property("venue_kind", unreal.RbVenueKind.DIVE_BAR)
	table.set_editor_property("venue_seed", int(t["venue_seed"]))
	table.set_editor_property("first_career_table", bool(t.get("first_career_table", False)))
	table.set_editor_property("lamp_underside_height", float(t["lamp_underside_height_m"]))
	table.set_editor_property("use_lamp_footprint", True)
	fp = t["lamp_footprint_core_m"]
	table.set_editor_property("lamp_footprint_min", unreal.Vector2D(*fp["min"]))
	table.set_editor_property("lamp_footprint_max", unreal.Vector2D(*fp["max"]))
	table.set_editor_property("use_baked_meshes", True)
	table.rebuild_table()
	return table


def place_venue_info(L: dict, LJ: dict):
	info = spawn_actor(unreal.RbVenueInfo, (1375.9, 542.7, 250.0), label="VenueInfo", tags=("RbVenueInfo",))
	info.set_editor_property("venue", unreal.RbVenue.DIVE_BAR)
	info.set_editor_property("venue_kind", unreal.RbVenueKind.DIVE_BAR)
	info.set_editor_property("venue_seed", int(L["tables"][0]["venue_seed"]))
	info.set_editor_property("age", float(L.get("age", 0.8)))
	info.set_editor_property("initial_lighting_state", unreal.RbLightingState.OPEN)
	ramp = float(LJ.get("ramp_seconds", 0.8))
	info.set_editor_property("lights", [venue_light(j, LJ["defaults"], ramp) for j in LJ["lights"]])
	lamp = L["lamp"]
	info.set_editor_property("lamp_efficiency", float(lamp.get("efficiency", 0.6)))
	info.set_editor_property("lamp_cutoff_deg", float(lamp.get("cutoff_deg", 50.2)))
	info.set_editor_property("camera_optics", camera_optics(L))
	return info


def _set_any(obj, names, value) -> None:
	for name in names:
		try:
			obj.set_editor_property(name, value)
			return
		except Exception:  # noqa: BLE001 - UE's snake_case of digits differs between versions
			continue
	rb.fail(f"no property {names} on {obj}")


def camera_optics(L: dict) -> list:
	"""Lenses that differ from the Eyes / Headcam preset (12.3 trailer hooks, ui-ux 6.3 menu stations): ARbVenueInfo applies them
	while the camera is the view target."""
	out = []
	for group in ("views", "trailer", "menu"):
		for c in L["cameras"][group]:
			if not any(k in c for k in ("hfov_deg", "vfov_deg", "focal_mm", "fstop")):
				continue
			o = unreal.RbVenueCameraOptics()
			o.set_editor_property("camera_tag", c["tag"])
			_set_any(o, ("vertical_fov_deg",), float(c.get("vfov_deg", 0.0)))
			_set_any(o, ("horizontal_fov_deg",), float(c.get("hfov_deg", 0.0)))
			_set_any(o, ("focal_length35mm", "focal_length35_mm", "focal_length_35mm"), float(c.get("focal_mm", 0.0)))
			_set_any(o, ("f_stop", "fstop"), float(c.get("fstop", 0.0)))
			out.append(o)
	return out


# ------------------------------------------------------------------------------------------------------------------------------


def main() -> None:
	global EAS
	args = [a for a in sys.argv[1:] if a and not a.lower().endswith(".py")]
	EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	L = load("layout.json")
	LJ = load("lights.json")
	lights = {j["id"]: j for j in LJ["lights"]}
	A = Assets()
	rb.log(f"assets: {len(A.meshes)} imported venue meshes: {sorted(A.meshes)}")
	world, levels = fresh_world()

	# --- persistent level ---
	use_level(levels[""])
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbGameMode)
	table = place_table(L)
	info = place_venue_info(L, LJ)
	ps = L["player_start"]
	spawn_actor(unreal.PlayerStart, cm(ps["location"]), (0.0, float(ps["yaw_deg"]), 0.0), label="PlayerStart")
	n_cams = place_cameras(L)

	# --- geometry ---
	use_level(levels["Geo"])
	n_arch = place_arch(A)
	place_ceiling_fixtures(L, A)
	elements = place_elements(L, A)
	lamp_kind = place_lamp(L, A, lights)
	n_pendants = place_pendants(lights)
	n_decor = place_decor(L)
	n_hero = place_hero_props(L, A)
	place_jelly_jar(lights)
	rb.log(f"hero props of M2-B placed: {n_hero}")
	place_volumes(L)
	place_markers(L)
	place_fx(L, A)
	place_lux_rig(L)

	# --- lighting: the Open rig + haze; Lights-Up troffers; AfterHours stub ---
	use_level(levels["Light_Open"])
	n_lights = 0
	for j in LJ["lights"]:
		if j.get("group") == "troffers":
			continue
		spawn_light(j, LJ["defaults"])
		n_lights += 1
	place_fog(LJ)
	use_level(levels["Light_LightsUp"])
	for j in LJ["lights"]:
		if j.get("group") == "troffers":
			spawn_light(j, LJ["defaults"])
			n_lights += 1
	use_level(levels["Light_AfterHours"])
	spawn_actor(unreal.TargetPoint, (820.0, 366.0, 250.0), label="AfterHours_StateStub", tags=("RbDB_AfterHours",))
	use_level(levels[""])

	if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True):
		rb.fail("could not save the dive-bar levels")
	for suffix in [""] + [f"_{s}" for s in SUBLEVELS]:
		if not unreal.EditorAssetLibrary.does_asset_exist(MAP + suffix):
			rb.fail(f"{MAP + suffix} was not written")
	props = sorted(k for k, v in elements.items() if v == "prop")
	grey = sorted(k for k, v in elements.items() if v == "greybox")
	rb.log(f"L_DiveBar: {n_arch} architecture / neon meshes, {len(props)} elements with M2-B meshes {props}, {len(grey)} greyboxes {grey}, "
		f"lamp {lamp_kind}, {n_pendants} bar pendants, {n_decor} wall frames / posters, {n_lights} lights, {n_cams} cameras, table {table.get_name()}, "
		f"venue info {info.get_name()}")

	if "--no-validate" not in args:
		world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
		report, ok = unreal.RbVenueInfo.validate_venue_level(world)
		for line in str(report).splitlines():
			rb.log(f"validator {line}")
		if not ok:
			rb.fail(f"{MAP}: venue level validator failed")
		rb.log("venue level validator: OK")


main()
